# M11 终局镜头 M3：三行星连续导演与 Handoff

> 编码：UTF-8，简体中文。
> 状态：实现与离屏复核进行中；当前只通过生命周期、目标顺序与部分连续性门，尚未完成 Rank 0/Rank 11 的最终视觉验收。
> 所有权：`feature/m11-finale`。不修改候选、求解、PlaybackPlan、共享地图、三渲二或二进制资产。

## 1. 目标与非目标

M3 把 M2.3 已验收的 Lucy 式单行星构图复用于 Assist1、Assist2、Assist3，并为两次行星交接建立显式 `Handoff`。本阶段要求：

1. `CurrentBody` 只在 `Handoff` 内按 Assist1 → Assist2 → Assist3 单调切换；
2. 三颗行星各自保持“盘外接近 → 前景穿越 → 右侧离开”的屏幕方向；
3. 交接期间优先保证主控鸟可读，相机位置、旋转和 FOV 不发生单帧硬切；
4. Rank 0、Rank 11 最终均需通过鸟不离框、叙事目标无空窗和无 Frenet 翻转的离线门；
5. 渲染模式不得改变权威轨迹、阶段决策或导演数值。

M3 不处理 UFO 收束，`FinalApproach/Terminal` 仍归 M4；也不评价当前大气散射、颜色或三渲二像素质量。

## 2. 权威数据流

```text
Released PlaybackPlan / Assist Enter-Closest-Exit
                         |
                         v
CameraDirector::ResolveStage
  - CurrentBody / FramingBody
  - Stage / Progress / Duration / Reason
  - 只允许在 Handoff 切换 CurrentBody
                         |
                         v
InteractionSystem
  - 由冻结 Preset + Finale Frame 取得行星中心和视觉半径
  - 由同一权威事件冻结每颗行星 encounter right/up
                         |
                         v
FinaleFlightCamera
  - M2.3 Lucy 构图复用于 Assist1/2/3
  - Handoff 使用实际秒数建立新目标，不按窗口百分比假定时长
  - 过渡中限制单帧位置/旋转并重新求鸟优先视轴
                         |
                         v
Capture Runner / CSV Schema 3 / Offline Analyzer
```

阶段解析器仍是纯数据函数；相机和录制器禁止各自维护另一套目标选择规则。事件、目标半径或 encounter basis 不可用时 fail closed，不按最近 Actor 回退。

## 3. 当前实现合同

### 3.1 开关与冻结

控制台：

```text
abts.M11.CameraDirector.M3.Enabled 0|1
```

命令行录制：

```text
-ABTSM11CaptureDirectorM3=0|1
```

M2、M3 互斥；两者同时请求时录制器拒绝启动。开关在 `BeginAuthorityFollow` 冻结，飞行途中修改全局 CVar 不改变本次导演分支。

### 3.2 单行星复用

Assist2、Assist3 使用与 M2.3 相同的构图数学：行星锚定投影弦、`Camera → Bird → Planet` 深度顺序、Approach 50° → 30° 缩窄、Periapsis 恢复至 50°，并各自使用权威事件冻结的时间正向 screen-right 和 encounter-up。M3 不复用 Assist1 的世界方向常量。

### 3.3 Handoff 过渡

两次 Handoff 的物理时长不同，因此进入新目标的导演权重使用实际经过秒数：

```text
HandoffBlend = SmootherStep(HandoffElapsedSeconds / LeadInSeconds)
```

当前 `LeadInSeconds = 0.40 s`。Handoff 与 Approach 的职责严格分离：Handoff 在目标刚切换时使用鸟锚定安全构图，最后 `0.50 s` 才以五次平滑权重释放到下一颗行星的 Lucy 构图；Approach 从第一帧起只使用唯一的行星锚定 Lucy 位置解算，不再拥有第二套位置收敛器或按秒关闭的分支。Handoff/Approach 均保留 10° 单帧旋转保护，但旋转保护不改写相机位置。这样下一行星进入 Approach 前已经完成构图交接，阶段内部不会因安全解算器追赶普通解算器而反向移动。

Rank11 首轮抽帧同时暴露了一个必须单列的几何约束：第一次交接起点，两颗行星相对当前相机约相隔 123°，常规镜头无法在保持鸟当前像素尺度时同时容纳两颗行星。单纯延长混合、减慢响应或做世界 Transform 插值只能把问题变成更长的空背景；不能把这些轮次写成“目标无空窗已通过”。最终方案需在以下导演选择中完成一种并重新验收：

- 受控鸟锚定摇摄，把短暂无行星画面定义成有意图的转场并修订 M3 目标门；
- 提前建立可同时读到当前行星与下一行星的广角/远景桥接，但必须证明鸟仍可读；
- 设计满足屏幕匹配的显式切镜，并相应把连续性合同从单镜头 C1 改为切点两侧的屏幕连续。

在选择完成前，离线工具继续把 Handoff 目标丢失报告为失败，不做豁免。

### 3.4 权威阶段与镜头叙事阶段分层

`Stage` 继续只描述权威轨迹的 `Cruise/Handoff/Approach/Periapsis`，不得为了镜头节奏改写 Assist Enter/Closest/Exit。其上新增只影响构图的 `ShotPhase`：

- `Authority`：直接消费当前权威阶段；
- `OutgoingHold`：Closest 后短暂维持上一行星的 Lucy 离场构图；
- `IncomingReveal`：从当前实际 View 以零权重开始，逐渐获取下一行星与 encounter 画面轴；
- `IncomingTrack`：获取完成后稳定维持鸟与当前目标的双主体构图；
- `IncomingEntryMatch`：进入前的最终匹配段，位置、朝向和权重已经与 Approach 入口一致。

每次下一行星揭示从 `AssistEnter - 3.25 s` 反向调度，同时保证上一颗行星 Closest 后至少保留 `0.75 s`。上一镜头使用 `0.40 s` 从离场构图释放；新镜头用 Hermite 曲线从 `-3.00 R` 连续移动至 `-1.35 R`，其末端屏幕速度按下一段 Approach 的入口速度解析匹配，最后 `0.50 s` 进入 `IncomingEntryMatch`。这使镜头可以从火星 Periapsis 后段借时间提前揭示木星，也可以在土星物理 Handoff 的前段开始逐渐转轴，而不改变轨迹、CurrentBody 或碰撞语义。

Assist1 使用相同状态而不再依赖 M2 的 Cruise 百分比：发射时间 `0 s` 被视为虚拟 `LaunchAnchor`，发射第一帧即进入 `IncomingReveal`；混合权重从 0 平滑增加，因此保留准确发射构图而不硬切。首段没有上一颗行星的大角度交接，鸟锚定安全桥在固定 `0.40 s` 内释放到火星双主体解；之后进入 `IncomingTrack`，并在 Assist1 Enter 前复用同一 `IncomingEntryMatch`。后续 Assist 仍保留跨行星长弧预转轴，不因首段修复重新压缩土星转轴。

`OutgoingHold` 即使已经跨过物理 AssistExit，也必须保持上一颗行星的 `+2.50 R` Lucy 离场标记；若误用下一目标 Handoff 的 `-3.00 R → -1.35 R` 曲线，会产生一次从画面右侧倒回左侧的伪穿越。

## 4. 录制与离线合同

录制合同 v8、观测 Schema 4 引入了原有三类过渡状态。首段统一后合同升为 v9、Schema 5；列结构不变，但 `shotPhase` 的合法语义新增 `IncomingTrack`，并要求 Assist1 也实际出现 Incoming 状态。除 Schema 3 字段外包括：

- `currentTarget`：叙事 CurrentBody；
- `framingTarget`：实际用于构图的 Assist；
- `stageProgress`、`stageDurationSeconds`：验证交接使用真实时间；
- `directorM3FrozenEnabled`：本次 authority follow 冻结值。
- `shotPhase`、`shotReason`：当前独立镜头叙事状态与调度原因；
- `shotProgress`、`shotDurationSeconds`、`shotEndSlope`：提前揭示曲线及 Approach 入口速度匹配的可复核参数。

离线报告新增：

- 三颗 Assist 的导演帧、鸟/目标丢失、Approach/Periapsis 左→右关系；
- Approach 的最大负向单帧变化及“相对 X 已到最右值后的最大累计回撤”；累计回撤超过 20 px 即判为方向反转；
- CurrentBody 切换帧及其 Stage；
- Handoff 目标丢失总帧和最长连续段；
- `m3AllAssistsDirected`、`m3AssistSwitchesOnlyInHandoff`、`m3HandoffPassed`。
- `m3ShotPhaseCounts`、每颗行星的 `incomingShotFrames` 与 `m3ShotPlanPassed`，防止实现退回到“只在物理 Handoff 末尾急转”的旧状态机；
- `m3FirstBodyAcquisitionPassed`：Schema 5 下要求导演混合在 playback `0.10 s` 内启动、Assist1 在 `0.75 s` 内首次可见、`1.00 s` 内完整入画且首段鸟不丢失。判据使用播放秒数，不使用候选相关的 Cruise 百分比或固定帧号。

`m3HandoffPassed` 必须同时满足：三颗 Assist 均被导演、恰有两次顺序切换且都发生于 Handoff、当前 Schema 要求的 ShotPhase 均实际出现、首个目标获取门通过、Handoff 当前构图目标和 M3 窗口鸟均不丢失、M3 窗口无位置/旋转/FOV 跳变。M4 前的 UFO 失败不得冒充 M3 通过或失败；报告需分层呈现。

## 5. 阶段验收

### M3-A：纯数学、配置与生命周期

- M3 对三颗 Assist 的 Cruise/Handoff/Approach/Periapsis 有资格，M2 仍只覆盖 Assist1；
- M3 把发射点视为 Assist1 的虚拟来源，首段必须依次经过 `IncomingReveal → IncomingTrack → IncomingEntryMatch`；
- M2/M3 互斥，默认均关闭，开关在一次飞行内冻结；
- CurrentBody 只在 Handoff 切换，顺序严格为 1 → 2 → 3；
- 任一行星事件、半径、冻结画面基或有限性失败时 fail closed；
- 合同版本、命令行解析、Manifest 和 CSV Schema 由精确自动化测试冻结。

### M3-B：Rank11 首轮导演复核

- 三颗行星均出现 Lucy 式左→右前景穿越；
- 两次 Handoff 无鸟离框、无位置/旋转/FOV 单帧跳变；
- 单独报告 Handoff 当前目标空窗，不用全局 UFO 指标污染结论；
- 离屏 AVI 与逐帧 CSV 帧数一致，Manifest 为 `Complete/TargetHit`。

### M3-C：Rank0/Rank11 正交验收

- Rank0、Rank11 各自在 Stylized 0/1 下阶段决策和阈值分类指纹一致；
- 两个 Rank 的 M3 窗口均为鸟丢失 0、目标空窗 0、位置/旋转/FOV 跳变 0；
- 人工检查两次 Handoff 没有无动机的天空摇摄、硬切、Roll 翻面或重复停顿；
- 只有本门通过后，主设计稿中的 M3 才可标为完成。

## 6. 当前非结论

- Rank11 仍是未认证候选；本阶段录屏不提升候选认证身份；
- M3 命令行成功、全程命中和三颗行星均被导演，不等于 Handoff 视觉门通过；
- 当前三渲二只能帮助检查轮廓与遮挡，颜色不可读不属于 M3；
- UFO、终端停留和最终镜头收束归 M4。

## 7. 2026-08-09 Rank11 首轮证据

最终保留的首轮录制为 `Saved/M11CameraCaptures/M3MultiAssistK-20260809-180742/Stylized1/`：fresh `UnrealEditor.exe -game -dx11 -RenderOffscreen`、Rank11、Stylized 1、1280×720、30 fps。Manifest 为 v7、`Complete/TargetHit`、`M3MultiAssist`，JPG、CSV 和 AVI 均为 949 帧；三渲二运行状态全程保持，Released Trajectory Hash 为 `0x505F3312AC8AE07F`，PlaybackPlan Hash 为 `0x76B24AB41B6E8B63`，AVI SHA-256 为 `9F7F4921B83A828E5C6CF86928AA314F6F599C31F6E46864C37A6E01F3F2E671`。

| 指标 | 首轮结果 |
| --- | ---: |
| M3 导演窗口帧 | 895 |
| Assist1/2/3 均被导演 | 是 |
| Assist1→2→3 切换只在 Handoff | 是 |
| 左→右穿越判据 | Assist1、Assist3 通过；Assist2 未通过 |
| M3 窗口鸟丢失 | 0 |
| Handoff 当前目标丢失 | 22 帧，最长连续 11 帧 |
| M3 窗口位置 / 旋转 / FOV 跳变 | 0 / 0 / 0 |
| `m3HandoffPassed` | `false` |

抽帧显示 Assist1、Assist3 保持左→右前景穿越；Assist2 的 Approach 可观测开头相对 X 已为正，说明交接收敛占用了其盘外左侧建立段，尚未通过 Lucy 方向门。两次 Handoff 改成鸟锚定摇摄后，鸟不再随目标切换离框，但会分别经过短暂的“只有鸟、没有叙事行星”画面。这个结果完成 M3-A，并证明 M3-B 工程链可复核；M3-B 视觉门和 M3-C 均未完成，也不触发 Rank0 四象限验收。

最终 Development Editor `-ForceUnity -DisableAdaptiveUnity` 全链接通过；fresh NullRHI `ABTS.M11C.Unit.FlightCameraAuthorityFrame` 与 `ABTS.M11C.CameraCapture.Config` 均精确发现 1 项并成功 1 项。Python 离线工具通过 `py_compile`，工作区通过 `git diff --check`。

### 7.1 Approach 状态职责与连续方向

首轮录制在三颗行星的 Approach 开始后恰好 0.75 秒发生屏幕方向回跳：帧 `245→246`、`476→477`、`767→768` 的鸟—行星相对 X 分别单帧回退约 `134 / 214 / 83 px`。初次修复用 0.75 秒交叉淡化消除了边界单帧跳变，但用户复核又发现帧 `230→242` 存在连续慢回摆：Assist1 相对 X 先前进到 `-39.07 px`，再退到 `-88.30 px`。旧离线门只检查单帧是否后退超过 20 px，因此错误地产生 `m3NoApproachReversal=true`。

根因不是权威轨迹、encounter basis 或阶段时间倒转，而是 Approach 同时运行“鸟优先安全构图”和“行星锚定 Lucy 构图”。两套位置解算器在 0.75 秒内给出相反的屏幕运动，交叉淡化只能把一次跳变摊成长回摆。最终实现删除 Approach 的安全位置解算和释放计时；Approach 始终由唯一 Lucy 位置解算器负责。鸟优先安全构图只属于 Handoff，并在 Handoff 最后 `0.50 s` 释放完毕；最终位置确定后再计算行星锚定旋转，并应用 10° 旋转步长保护。

离线工具保留 `approachBackwardJumpFrames` / `maximumApproachBackwardJumpPixels`，并新增 `maximumApproachBackwardExcursionPixels`：对每帧相对 X 维护历史最大值，统计从该最大值向左的累计回撤；`m3NoApproachReversal` 要求三颗行星的最大累计回撤均不超过 20 px。因此旧录屏现在会正确失败，Assist1/2/3 分别检出约 `49.23 / 146.65 / 100.17 px` 回撤。

最终 fresh 录制为 `Saved/M11CameraCaptures/M3ApproachContinuityFinal-20260809-200351/Stylized1/`。三颗 Assist 的负向单帧跳变和最大累计回撤均为 0，三颗 `leftToRightObserved=true`，M3 窗口鸟丢失为 0；帧 `230→242` 从 `-98.02 px` 单调前进到 `-89.01 px`，原边界 `245→246` 为 `-86.12→-85.82 px`。Handoff 当前目标空窗仍为既有 22 帧，继续由 `M11-CAM-M3-001` 跟踪，不属于本修复。

该录制为 949 帧、`Complete/TargetHit`，权威轨迹 Hash `0x505F3312AC8AE07F`、PlaybackPlan Hash `0x76B24AB41B6E8B63` 与此前一致；AVI SHA-256 为 `EE5554B83206186BFD10DDB50F167A103EF8A8FA5133202C31B7C49389D32379`。

### 7.2 多行星节奏与预转轴改造

`M3ApproachContinuityFinal` 证明了 Approach 内部不再倒退，但也暴露了上一版状态机仍把叙事节奏绑在物理阶段边界上：火星从画面穿出后仍被 Periapsis 持有到帧 415，木星要到帧 454 才接管且入口已经贴盘；土星在帧 634 进入物理 Handoff 后，导演权重直到帧 731 才开始变化，最后约 15 帧集中完成迎合左→右穿越的视角偏转。

本轮采用 3.4 的双层状态方案，不改变 Rank11 路线和权威事件。最终 fresh 录制为 `Saved/M11CameraCaptures/M3ShotStateFinalC-20260809-211423/Stylized1/`：合同 v8、CSV Schema 4、Rank11、Stylized 1、`Complete/TargetHit`，JPG/CSV/AVI 均为 949 帧。木星 `IncomingReveal` 为帧 357–438、帧 439–453 为 `IncomingEntryMatch`，第 368 帧开始可见，较旧版第 454 帧 Approach 提前 86 个可见帧；土星 `IncomingReveal` 为帧 648–730、帧 731–745 为 `IncomingEntryMatch`，第 666 帧开始可见。

土星帧 648–746 的最大单帧旋转为 `9.069213°`、触及 10° 保护的帧数为 0、平均单帧旋转为 `2.265°`；不再在 731–745 集中偏转。M3 窗口鸟丢失、位置/旋转/FOV 跳变均为 0，三颗 Assist 的 Approach 单帧负跳与累计回撤均为 0，三颗左→右判据均成立，`m3ShotPlanPassed=true`。

当前新 FramingBody 在过渡初段仍有 Assist2 11 帧、Assist3 18 帧未入画；其中土星过渡可见短暂只剩鸟的画面，因此 `m3HandoffPassed=false`。这是 `M11-CAM-M3-001` 已记录的 123° 跨行星单镜头几何空窗，仍需在广角桥、显式匹配切镜或有意图的无行星转场中另选方案；本轮不放宽目标门，也不据此宣称 M3-B/M3-C 完成。

### 7.3 发射后首个目标获取状态

`M3ShotStateFinalC` 中发射从第 3 帧进入 `CruiseToBody`，但 M3 ShotPlan 只遍历 Assist2/3；Assist1 仍使用 `CruiseLeadInStartFraction=0.15` 和 `CruiseLeadInBlendFraction=0.35`。Rank11 首段约 7.3 秒，因此导演前约 1.1 秒完全不接管：第 35 帧权重仍为 0，第 52 帧火星才擦入画面，第 70 帧才完整可见。第 50 帧火星投影半径已有约 46.5 px，但球心在 `Y=-52.9 px`，确认根因是相机接管延迟而不是目标过小。

现已让 ShotPlan 从 Assist1 开始，并把发射时间作为虚拟来源。发射首帧的导演权重从 0 连续增加，首段鸟锚定桥只在固定 0.40 秒内释放；进入 Track 后继续沿整个 Launch→Assist1Enter 的 Hermite 轨迹从 `-3.00 R` 向 `-1.35 R` 运动，末端仍解析匹配 Approach。录制合同升 v9、CSV Schema 升 5，离线工具增加首目标获取秒数门，同时继续接受 Schema 1–4。Development Editor 编译以及 `ABTS.M11C.Unit.FlightCameraAuthorityFrame`、`ABTS.M11C.CameraCapture.Config` fresh NullRHI 精确单测均通过。

fresh Rank11 Stylized1 `M3LaunchAcquire-20260810-111225` 为 `Complete/TargetHit`，AVI/JPG/CSV 均为 949 帧。导演混合从第 3 帧、`0.033 s` 启动；火星第 7 帧、`0.167 s` 首次可见，第 8 帧、`0.200 s` 完整入画，首段鸟丢失 0，`m3FirstBodyAcquisitionPassed=true`。三颗 Assist 导演窗口的位置/旋转/FOV 跳变均为 0，三颗 Approach 累计回撤仍为 0，轨迹与 PlaybackPlan Hash 未改变。既有 Assist2/3 新目标几何空窗仍为 11/18 帧，因此总报告继续令 `m3HandoffPassed=false`，只由 `M11-CAM-M3-001` 跟踪，不影响本条首目标获取结论。

### 7.4 双行星远景桥与三主体构图

Rank11 的两次跨行星交接不能在原 50° Lucy 近景中同时容纳上一行星、下一行星与可读的鸟。当前实现新增固定 `0.60 s` 的 `DualBodyBridge`：上一行星与下一行星决定稳定的屏幕左右轴，权威平行运输 Up 决定屏幕上方；三颗包围球按 16:9、85° FOV 和 1.15 安全边距共同求解相机距离。`OutgoingHold` 同步把镜头从原镜头平滑扩至 85°，桥接远景把鸟的视觉组件临时放大到 10 倍；该倍率在下一行星 Track 内退回 1，不改变碰撞、轨迹或命中。

桥后不直接恢复旧追星相机，而是使用“鸟＋下一行星”双主体拟合逐渐把 85° 收回 50°。`IncomingEntryMatch` 跨过物理 AssistEnter 再保持 0.50 秒，之后 Approach/Periapsis 直接消费连续的 Lucy 构图解，避免位置 lag 再次把鸟甩出画面。合同升至 v10、CSV Schema 升至 6；逐帧同时记录两颗桥接行星的投影与可见比例。

最终 fresh 录制为 `Saved/M11CameraCaptures/M3DualBodyBridgeFinal4-20260810-133000/Stylized1/`：Rank11、Stylized 1、`Complete/TargetHit`，JPG/CSV/AVI 均为 949 帧。两次桥共 36 帧，36/36 帧双行星同框，跨行星窗口零行星帧 0，Handoff 当前目标丢失 0，M3 窗口鸟丢失 0，三颗 Approach 累计回撤仍为 0，`m3DualBodyBridgePassed=true`、`m3ShotPlanPassed=true`。

本轮没有把显式远景匹配伪装成严格连续单镜头。桥接两端的世界相机位姿仍超过旧 M3-B 的单帧阈值，报告继续为 `m3HandoffPassed=false`；全局 FinalApproach/UFO 的 8 帧鸟丢失仍归 M4。因而当前结论是“无行星空窗与三主体远景桥已实现”，不是“M3 整体验收完成”。

### 7.5 跨状态连续性契约与入口匹配修复

`M11-CAM-M3-007` 的根因不是单颗行星几何，而是旧状态机在 `OutgoingHold → DualBodyBridge → IncomingTrack → IncomingEntryMatch → Authority` 边界更换三套互不相容的相机基底。当前实现把跨行星过程重构为一条共享构图链：`OutgoingHold` 从上一颗 Lucy 构图连续扩展到鸟、上一颗和下一颗的三主体拟合；`DualBodyBridge` 保持同一个拟合解；`IncomingReveal` 在三主体仍同框时逐渐预转到下一颗 Lucy 朝向；`IncomingTrack` 沿该朝向从三主体远景连续推入下一颗 Lucy 构图；`IncomingEntryMatch` 与下一帧 `Authority/Approach` 使用完全相同的位置、旋转和 FOV 解。所有阶段使用自身局部进度，原先因判定顺序导致不可达的 `IncomingReveal` 已恢复，物理 Enter 后重复远景的 `PostEnterSettle` 已删除。

运行时不再让各状态首帧释放累计相机跟随误差：跨行星构图直接消费完整约束 Transform，首颗行星在导演权重达到 1 后同样直接进入连续构图。桥接左右方向不再根据逐帧 BaselineView 翻转，而由平行运输 Up 与行星连线确定；鸟体视觉 Scale 全程保持 1 倍，远景可读性只由独立拖尾承担。

fresh Rank11 Stylized1 离屏录制为 `Saved/M11CameraCaptures/M3StateContinuityR5-20260810-205922/Stylized1/`：合同 v10、RenderVersion 19、`Complete/TargetHit`、JPG/CSV/AVI 均为 949 帧，Released Trajectory Hash `0x505F3312AC8AE07F`、PlaybackPlan Hash `0x76B24AB41B6E8B63`，AVI SHA-256 `23C46A89B7C013932DE9CFFBBD6E58DCB42BDC5151EDF12A1D78AD53F21184F9`。三颗 Assist 的 M3 鸟丢失为 0、Approach 累计回撤均低于 1 px，两次 `DualBodyBridge` 共 36 帧且 36/36 双行星同框。旧问题帧 `222→223` 的鸟屏幕 X 现为 `516.52→517.44 px`，不再倒退；16 个状态边界的单帧相机变化全部低于 `1001 cm / 0.98°`，FOV 连续，其中两次桥接入口分别为 `575 cm / 0.16°` 与 `418 cm / 0.02°`，两次 EntryMatch→Authority 分别为 `746 cm / 0.35°` 与 `893 cm / 0.28°`。逐帧目检确认上一行星离场、双星远景、下一行星进场没有单帧硬切或鸟体消失。

现有离线工具的总 `criteriaPassed` / `m3HandoffPassed` 仍为 `false`，不能伪报绿灯：全局 8 帧空构图和 4 个大旋转帧全部发生在未改动的 FinalApproach/UFO；M3 内的 15 帧 `targetLost` 来自旧工具在首目标获取时仍以物理 CurrentTarget 记账，而画面中的导演 FramingTarget 已可见。工具还用固定 `5000 cm` 世界位移阈值评价所有连续远景运动，因场景尺度而把平滑的同状态推拉标为跳变。M11 本轮没有越权修改集成工作树所有的离线工具；关闭 `M11-CAM-M3-007` 依据精确状态端点自动化、边界逐帧审计与像素目检，M4 和工具判据升级分别保留为后续事项。

### 7.6 双星退出到穿越入口的屏幕锚提前匹配

`M11-CAM-M3-008` 继续处理状态边界已经连续后暴露的同状态构图时序问题。旧实现虽然在 `IncomingTrack` 末端准确到达下一颗 Lucy 构图，却仅在 Track 内使用对称 SmoothStep 混合位置；鸟会先随物理轨迹移到下一行星右下侧，随后在最后约 8–18 帧被相机拉回左侧入口。修正后，下一颗 Lucy 构图匹配从 `DualBodyBridge` 退出首帧即开始，横跨完整 `IncomingReveal + IncomingTrack`，以二次 Ease-out 后接 SmoothStep 的确定性曲线前置主要构图变化。相机位置、桥到 Lucy 的朝向和 `85°→50°` FOV 共用同一个 Match Alpha；`IncomingEntryMatch` 仍保持精确 Lucy 端点，因此不引入新的状态边界切换。

fresh Rank11 Stylized1 离屏录制为 `Saved/M11CameraCaptures/M3TrackMatchR5-20260810-213900/Stylized1/`：合同 v10、RenderVersion 19、`Complete/TargetHit`、JPG/CSV/AVI 均为 949 帧，Released Trajectory Hash `0x505F3312AC8AE07F`、PlaybackPlan Hash `0x76B24AB41B6E8B63`，AVI SHA-256 `2BAE60A3C47E26FA2E1336477372C613F48E54D709F5F31E3888ABF490251CCB`。旧木星段帧 431 的鸟屏幕 X 为 `706.30 px`、到 439 突然回到 `526.65 px`；新录制在 431 已为 `525.75 px`，439 为 `526.65 px`。土星段从旧 `712→730 = 639.58→525.86 px` 收敛为新 `534.28→525.79 px`。两段在用户指出的窗口内都已处于下一行星左侧穿越入口，不再先越过右侧再回拉；逐帧图像同时确认下一行星全程保留。fresh NullRHI `ABTS.M11C.Unit.FlightCameraAuthorityFrame` 精确 1/1 成功，日志为 `Saved/Logs/M11-CameraTrackMatch-R5-20260810-FreshAutomation.log`。

### 7.7 上一行星退场与下一穿越入口的解耦

7.6 的统一 Match Alpha 虽消除了晚回拉，却把相机位置、朝向和 FOV 一起前置，导致 `IncomingReveal` 刚离开双星桥接时就过早丢掉上一颗行星。最终方案不再让一个权重同时承担“上一颗退场”和“下一颗入场”两项职责：`IncomingReveal` 保持 85° 三主体宽景，并在该宽景中完成桥接朝向到下一颗 Lucy 朝向的预转；进入 `IncomingTrack` 后，朝向保持 Lucy 不变，另用两条独立包络求解相机。屏幕平面包络逐步把鸟的二维 NDC 锚点从三主体宽景位置移动到 Lucy 入口，深度/FOV 包络则较慢地从 85° 宽景推至 50° 近景。求解器直接解析相机 Right/Up 分量满足鸟的屏幕锚点，Forward 分量保持深度包络，因此不会退化为鸟与行星双锚点的病态求交。

fresh Rank11 Stylized1 最终录制为 `Saved/M11CameraCaptures/M3EgressAnchorR5-20260810-222300/Stylized1/`：合同 v10、RenderVersion 19、`Complete/TargetHit`、JPG/CSV/AVI 均为 949 帧，Released Trajectory Hash `0x505F3312AC8AE07F`、PlaybackPlan Hash `0x76B24AB41B6E8B63`，AVI SHA-256 `7CAF922ABAC238EA67427A198FE27CED52033CA46C49E512E7F8F5F72165432A`。首个交接中火星在 413–420 帧连续向左上缩退并离场，鸟的 `431→439` 屏幕位置为 `(561.75, 401.94)→(526.65, 381.47) px`；第二个交接中木星在 644–667 帧连续向右下缩退并离场，鸟的 `712→730` 为 `(539.01, 366.53)→(525.79, 379.21) px`。两个交接窗口内鸟的最大单帧屏幕位移分别为 `10.18 px` 和 `7.40 px`，M3 导演窗口鸟丢失为 0，`m3NoApproachReversal=true`。逐帧目检确认上一颗行星退场、下一颗行星建立和鸟进入左侧穿越入口构成一个连续镜头。fresh NullRHI `ABTS.M11C.Unit.FlightCameraAuthorityFrame` 精确 1/1 成功，日志为 `Saved/Logs/M11-CameraEgressAnchor-R5-20260810-FreshAutomation.log`。离线总报告仍会受既有全局固定世界位移阈值和 M4 终段等历史门影响而标记 BaselineFailureObserved，本节只关闭 `M11-CAM-M3-009` 的交接构图回归，不将其冒充为整体 M3 绿灯。

### 7.8 前景穿越清轮廓门与跨行星时间预算

`M11-CAM-M3-010` 处理的不是单颗行星构图，而是镜头调度器允许 `OutgoingHold` 在物理 `Closest` 首帧立即接管的问题。旧 Rank11 录屏中，火星第 328 帧、木星第 548 帧刚进入 Periapsis，鸟心距行星中心仅约 `0.60 R`，轮廓仍深在盘内；此时 `OutgoingHold` 已开始把 FOV 和相机 Transform 拉向双星远景。`MinimumDepartureHoldSeconds` 只保护后续 Reveal 起点，无法约束更早的 Outgoing 起点，因此两颗行星稳定复现“尚未穿出就脱镜”。

修正后，调度器首先在每段 `Closest→Exit` 上计算归一化前景清轮廓门，默认 progress 为 `0.23`；到达该门之前始终保持 Authority/Lucy 穿越构图。之后才允许 `OutgoingHold` 使用五次 SmootherStep 拉远。跨行星时间由硬预算倒排：固定保留 `0.60 s DualBodyBridge + 1.30 s IncomingReveal + 至少 0.60 s IncomingTrack + 0.50 s IncomingEntryMatch`，同时在清轮廓门后至少留出 `0.50 s` Outgoing 拉远；窗口充足时仍使用首选 `2.00 s` 拉远。若候选事件间隔无法同时满足清轮廓、拉远和下一颗入口链，ShotPlan 直接 fail closed，不通过压缩 Track 或提前抢占前景穿越来伪造可用镜头。

fresh Rank11 Stylized1 离屏录制为 `Saved/M11CameraCaptures/M3TransitClearR6D-20260810-230300/Stylized1/`：合同 v10、RenderVersion 19、`Complete/TargetHit`，JPG/CSV/AVI 均为 949 帧，原生体积云关闭，AVI SHA-256 `5E18D6FA1EC598F4468E6E3CDBA944FF76F75AC90DE501B3DEB44D3BAFCA1B5D`。旧问题帧 328/548 均继续处于 Authority；火星和木星分别到第 348/568 帧才进入 `OutgoingHold`。以鸟与行星投影包围圆计算，两个 Outgoing 首帧净空为 `42.34 / 39.26 px`，整个拉远段最小净空为 `2.99 / 22.83 px`；逐帧图像确认两段都先完整穿出轮廓，再平滑拉远。两次桥仍为 36 帧且 36/36 双行星同框，M3 鸟丢失为 0、`m3NoApproachReversal=true`；火星 413–421 和木星 644–667 的渐退镜头以及后续 Lucy 左侧直飞入口均保留。Development Editor `-ForceUnity -DisableAdaptiveUnity -NoHotReloadFromIDE` 完整链接成功，fresh NullRHI `ABTS.M11C.Unit.FlightCameraAuthorityFrame` 精确 1/1 成功。离线总报告仍受既有全局/M4 固定阈值及远景鸟像素门影响而为 `BaselineFailureObserved`，本节只关闭过早脱镜，不把它写成整体 M3 放行。

### 7.9 双星桥接鸟纵向锚与投影安全拟合

`M11-CAM-M3-011` 处理 `OutgoingHold` 向双星桥接构图收敛时，鸟被无意抬到画面上部的问题。旧桥接只把两颗行星连线固定为 Camera Right，再用 transported Up 决定剩余旋转；位置求解把鸟与两颗行星的世界坐标做算术平均，并且只沿 Camera Forward 解一个包围距离。该解能保证三主体大致入画，却没有鸟的屏幕纵向合同。Rank11 木星→土星段中，鸟在行星连线的 Up 正侧很远，因此 575–625 帧随桥接旋转从 `436.54 px` 抬到 `276.23 px`，范围达 `173.96 px`，最大单帧上移 `19.03 px`；运动分解确认这是相机贡献，不是鸟的物理轨迹抬头。

修正后，`OutgoingHold → DualBodyBridge → IncomingReveal` 共用一个 canonical 鸟纵向锚。默认 `DualBodyBridgeBirdNdcY=+0.05`，即 720p 的约 `342 px`；Outgoing 用五次 SmootherStep 从当前 Lucy 鸟 Y 获取该锚，Bridge 全段持有，Reveal 再以零端斜率释放到既有三主体/Track 起点。求解器只调整 Camera Up，不改变行星连线的 Camera Right、鸟的屏幕 X 或已验收的左→右轨迹。每次纵向平移后，以鸟和当前有效的两颗行星投影球重新校验 `1.15` margin；若任一球会裁切，只沿 `-Forward` 二分求最小补充距离，再重新满足同一鸟 Y 锚。Outgoing 的下一颗行星按原 IncomingFitAlpha 从零半径虚拟主体连续生长，避免安全校验提前把尚未叙事出现的行星强拉入画面。

fresh Rank11 Stylized1 离屏证据为 `Saved/M11CameraCaptures/M3BridgeBirdAnchorR7-20260811-114000/`：合同 v10、RenderVersion 19、M3、`r.VolumetricCloud=0`，Manifest 为 `Complete/TargetHit`，JPG/CSV/AVI 均为 949 帧；AVI 171389442 bytes，SHA-256 `2838D7F9E23D9B6F77E04A53AACF69D477A20DFAD08F9626810A568775943C09`，ReleasedTrajectory/PlaybackPlan 仍为 `0x505F3312AC8AE07F / 0x76B24AB41B6E8B63`。575–625 帧鸟 Y 变为 `407.71→342.01 px`，范围降到 `65.70 px`，最大单帧上移降到 `2.31 px`，最大纵向二阶差分由 `6.99` 降到 `0.11 px/frame²`，且全段不再反向；568–627 整个第二 Outgoing 也单调收敛到 `342 px`。两次 Bridge 仍为 36/36 帧双星完整同框，两个 Outgoing 的鸟和离场行星 visible ratio 全程为 1；逐帧检查 575/592/616/628/646/684 确认木星拉远、土星入画和后续退场/直飞入口连续。fresh NullRHI `ABTS.M11C.Unit.FlightCameraAuthorityFrame` 精确 1/1 成功，并新增非共面三主体、鸟 Y 锚、两星水平基线、三投影球 margin 及既有五个状态边界连续断言。现有离线总报告中的全局/M4 与远景鸟像素门仍是独立问题，本节不据此宣称整体 M3 放行。

### 7.10 开局 Reveal→Track 的限制器终点连续合同

`M11-CAM-M3-012` 关闭发射后第一个状态边界上的轻微回退。该现象不是 7.9 鸟 Y 锚引入的回归：R7 第 40→41 帧由 `IncomingReveal` 进入 `IncomingTrack` 时，鸟屏幕 X 从 `523.37` 回到 `511.83 px`；R6D 同一边界数值一致，较早 R5 也有同类约 12 px 回退。根因是开局鸟携带限制器在 Reveal 末帧仍向“标准 lag 后的位置”释放，而 Track 首帧因导演权重精确到 1 立即绕过限制器，直接采用精确导演 Transform。近鸟约 4 千厘米、行星约 15.5 万厘米的视差把残余相机位置误差放大成鸟跳变，而行星球心只移动约 0.4 px。

修正后，非跨行星的 `LaunchAnchor→Assist1` incoming 全段保持同一个开局限制器所有者；位置释放从安全携带位置沿绕鸟方向/距离包络收敛到精确 `DesiredTransform.Location`，不再以标准 lag 位置为终点。旋转的运行时响应也随同一 release alpha 从 lag 平滑收敛到 1，使 release 完成时同样消费精确导演朝向。跨行星连续链、Authority Approach/Periapsis、双星鸟 Y 锚和镜头 FOV 均不改。纯数学判据冻结 release alpha 0 精确等于安全位置、alpha 1 精确等于导演位置，防止未来再次出现“状态边界换解算器终点”。

fresh Rank11 Stylized1 离屏证据为 `Saved/M11CameraCaptures/M3LaunchBoundaryR8-20260811-134500/`：合同 v10、RenderVersion 19、M3、`r.VolumetricCloud=0`，Manifest 为 `Complete/TargetHit`，JPG/CSV/AVI 均为 949 帧；AVI 171387250 bytes，SHA-256 `99967DB08C2CC3A2ADC97FAF40613C137BD2DAB0B037F7D61C1C2789298AB039`，ReleasedTrajectory/PlaybackPlan 仍为 `0x505F3312AC8AE07F / 0x76B24AB41B6E8B63`。旧 40→41 鸟 X 步长 `-11.540 px` 降为 `-0.069 px`，相机位置步长从约 `610.62 cm` 回到 `528.33 cm`，与相邻 `526.56/528.64 cm` 连续；41 帧之后相对 R7 的最大鸟投影差小于 `0.00004 px`、最大相机坐标差小于 `0.00012 cm`。222→223 的 EntryMatch→Authority 鸟 X 仍同向前进 `+0.925 px`，未把释放误差推迟到火星入口。Development Editor ForceUnity 完整链接成功，fresh NullRHI `ABTS.M11C.Unit.FlightCameraAuthorityFrame` 精确 1/1 成功（`Saved/Logs/M11-LaunchBoundary-R8-20260811-FreshAutomation.log`）。
