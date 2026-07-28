# M11-C：终局轨道交互、全景 HUD 与确定性实飞

> 状态：M11-B 已完成用户 PIE 验收；M11-C 初版与本轮 PIE 回归修复的 C++、强制完整 Unity 全链接、全新进程 Unit/Runtime、21,025 输入/558 F4 接管闭包及上游快速回归均已通过；等待用户按第 12 节重新完成有渲染 PIE。
>
> 父级：[M11 终局三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md)。
>
> 上游：[M11-B 局部布局与全输入域认证](M11BFinaleLayoutCertificationDesign.md) · [M11-A 纯数据引力弹弓求解器](M11AGravityAssistSolverDesign.md) · [M11.0 终局前置收口](M110PreFinaleClosureDesign.md)。
>
> 表现语义上游：[M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md)。
>
> 集成约束：[多工作树协作与集成规范](ABTSMultiWorktreeDevelopmentGuide.md) · [项目工作流](ABTSProjectWorkflow.md)。

## 1. 一句话目标

M11-C 把 M11-B 的冻结终局布局和 M11-A 的唯一积分器接入玩家操作：玩家可以在 Space 弹弓上自由调整 `Yaw × Pitch × Power`，通过逐目标预览、轨道全景图和可退出的前缀稳定器理解三次引力弹弓；Release 后只重放同一输入的确定性轨迹，并在已经取得 F4 合格拦截后，以明确可见、位置/速度/加速度连续的终端转移接入 800 cm 物理 UFO 演出尾段。

本阶段不把标准答案显示给玩家，不在 Release 时改写玩家输入，不使用 Chaos 推进深空轨迹，也不让 M9 卫星或表现 Actor 成为轨迹权威。

## 2. 本阶段边界

### 2.1 必须交付

1. Space 专用弹弓进入 M11 终局瞄准，普通 M6 弹弓路径保持原样；
2. 当前输入的合格轨迹和同输入 800 cm 物理轨迹均由 M11-A 求解器生成；
3. 异步预演只发布最新 revision，同一输入的 Preview/Release 共享结果和 Hash；
4. `Free → NearFn → StableFn` 前缀稳定器，支持降敏、Core Clamp、滞回释放和显式取消；
5. 按最早未完成助推顺序切换行星 ①、②、③、UFO 的接近预览；
6. 圆形终局轨道图：拟合轨迹平面、发射点在左、自适应完整构图、主星绝对经纬网、三行星/UFO 简笔图形和球后虚线；
7. Release 后以缓存轨迹按绝对求解器时间插值移动，不按渲染帧积分；
8. F4 到 800 cm UFO 的完整接管闭包：同输入可物理命中时保持同输入到底，否则只在 F4 之后显示 C2 `TerminalTransfer` 再接 nominal 物理尾段；
9. 终局结果与 M9 的存在、位置、可见性和引力参数隔离；
10. 错误发射的有界可读镜头、黑屏内安全原位恢复和可立即重试的最小失败闭环；
11. Development Editor 全链接、全新进程自动化和 PIE 验收清单。

### 2.2 延期到 M11-D

- 四鸟同时进入弹珠袋及完整编队标架；
- 星空材质切换、雾云关闭、曝光和环境状态快照；
- 白鸟救援、UFO 破坏、接触后的局部 Chaos 演出；
- 四鸟 Party、星空/雾云和剧情镜头共同参与的完整 Attempt Snapshot 与失败演出扩展；
- 最终音频、镜头节奏和剧情收尾。

M11-C 先用当前受控鸟证明瞄准、预演、轨迹权威、连续实飞、接管合同和最小失败恢复；M11-D 只能扩展四鸟/环境/剧情快照并消费这一条冻结播放计划，不能再建立第二套飞行模拟。

## 3. 权威数据流

```text
AABTSM11FinaleSystem Ready
  -> 校验 Finale Frame + 冻结 Preset/Scenario/Certification/Trust/Playback 身份
  -> 玩家 raw Yaw/Pitch/Power
  -> FABTSM11PrefixStabilizer
       desired input（从不吸向答案）
       controlled input（仅 Stable 时 Clamp 到已取得前缀 Core）
  -> Preset.BuildRequest(controlled input, 0x7)
  -> FABTSM11GravityAssistSolver::Solve
  -> FABTSM11PrefixClassifier::Classify
  -> immutable prediction/result/hash
  -> HUD + 逐目标预览 + Release
  -> FABTSM11PlaybackPlan
  -> 绝对轨迹时间插值
  -> Finale Frame 单向变换到 World 表现
```

只有 M11-A Solver 可以推进轨道。HUD、Scene Capture、Body/UFO Actor、M6、M9 和鸟 Actor 的 World Transform 都不能反向改变 Request、Result、分类或 Hash。

### 3.1 进入前的 fail-closed 身份门

`ValidateInteractionContract` 必须核对：

- `AABTSM11FinaleSystem` 已 Ready；
- Frame 可用且布局版本、槽对和比例合法；
- Layout、LaunchModel、Solver、Hash Schema、Scan 和 Physical Playback 合同版本；
- Preset Source、Preset、Scenario、Certification、Nominal、Physical Playback、Certified Bundle Hash；
- F1/F2/F3 Trust Region 的 PrefixLevel、边界、margin 和 Region Hash；
- 上述值与 `MakeCertifiedV1()` 冻结清单完全一致。

任一未知版本、零 Hash、运行时漂移或不匹配都拒绝进入。不能退回未认证默认参数，也不能临时在 World 中搜索三颗行星重建 Scenario。

## 4. 交互状态与输入路由

### 4.1 玩法状态

```text
Locked -> Ready -> Aiming -> ReleasePending -> Launched -> TargetHit
                    ^               |             |
                    |               v             v
                    +----------- Recovering <- Failed
```

- `Locked`：上游身份尚未通过；
- `Ready`：终局系统可用，等待 Space 弹弓交互；
- `Aiming`：接受三轴输入、发布预演、驱动袋和 HUD；
- `ReleasePending`：冻结当前 controlled input，等待完全相同 revision 的结果；
- `Launched`：按冻结 Playback Plan 移动；
- `TargetHit`：实际 800 cm UFO 接触完成；不是 16,000 cm F4 合格拦截；
- `Failed`：有真实尝试时播放有界失败反馈并淡入黑屏；无尝试的初始化/身份失败保持 fail-closed；
- `Recovering`：已在全黑帧中原位恢复，等待淡出完成并回到 `Ready`。

M11-D 可以在 `TargetHit` 后接救援演出，并扩展 `Failed/Recovering` 的四鸟、环境和剧情镜头；不得改变 M11-C 已冻结的轨迹结果或最小安全恢复时序。

### 4.2 Space 与普通弹弓隔离

`AABTSM11PlayerController` 派生自 M6 Controller：

- `IsFinaleSpaceSlingshot()==true` 时由 M11 拦截，M11 依赖无效也 fail-closed，绝不落入 M6 Chaos Release；
- Twig/Simple/Reinforced 等普通弹弓继续走 M6 public 状态机；
- 进入终局的那次鼠标按下/抬起只负责进入，必须等到后续一次新的按下再抬起才允许 Release；
- M11 活跃时使用隐藏光标的 `GameOnly` 相对鼠标输入，消费瞄准轴、滚轮、Release 和取消，并暂时关闭 Actor 点击/悬停事件；焦点丢失会清除已武装 Release，退出后恢复原控制和点击设置；
- 袋和鸟的表现位置随 `Yaw/Pitch/Power` 移动，求解器 Pouch 初始位置始终保持冻结的 canonical 值；
- M6 原绑定即使仍被调用，也因 Space 从未进入 M6 状态而无权发射。

## 5. 异步预演与 Release 身份

### 5.1 Latest-only 预演

每次有效输入变化递增 `AimRevision`。预演采用：

- 同时最多一个后台求解；
- 新输入到来时只标记 dirty，不为每个鼠标采样无限排队；
- 后台只捕获 Preset、Request、输入和 revision 的值拷贝，并只返回纯数据 `TFuture`；
- `AABTSM11FinaleInteractionSystem::Tick` 在 Game Thread 轮询并一次性 `Consume()` 已完成 future，再发布与当前 revision 和输入完全相同的结果；
- 后台线程不得直接排入 Scene Capture 或其他 Renderer 工作；Capture 只标记 dirty，并在同一 Actor 原生 Tick 尾部执行；
- stale 结果被丢弃，不能驱动稳定器、HUD 或 Release；
- nominal 800 cm 轨迹在初始化后单独异步求一次并缓存。

### 5.2 Release

按下 Release 后：

1. 冻结当前 `controlled input`；
2. 若最新预演的输入和 revision 完全一致，直接复用；
3. 否则保持 `ReleasePending`，求解这一个精确输入；
4. qualified 与同输入 physical 请求都从同一 Pouch 初态、时间零、四体和助推资格开始；
5. Playback Plan 记录 released trajectory、physical trajectory、计划版本和 Plan Hash；
6. 深空鸟只按缓存点插值，不再查询鼠标、M9、Actor 碰撞或 Chaos。

## 6. 前缀成功集稳定器

### 6.1 冻结语义

对 F1/F2/F3 的每个认证 Trust Region：

- `Core`：唯一允许 Stable 捕获和 controlled Clamp 的区域；
- `CaptureEnvelope = Core + 1 × FinalPrecision`：只进入 `NearFn` 并降低灵敏度；
- `ReleaseEnvelope = Core + 2 × FinalPrecision`：只观察未钳制的 desired input；
- raw/effective 同在 Core、权威分类满足 `IsF(n)` 并持续 `0.20 s` 才进入 `StableFn`；
- Near 默认灵敏度比例 `0.45`；
- Stable 后 desired input 越出 ReleaseEnvelope 持续 `0.16 s`，解除保护并回到自由输入；
- 显式取消立即解除保护；
- 稳定器只保护已经找到的最高前缀，绝不向 nominal input、下一行星或 F4 中心移动。

F2/F3 的冻结 box 可以几何相同，但 PrefixLevel 和 Region Hash 不同，运行时不能按几何去重。

### 6.2 玩家可见反馈

HUD 只显示 `FREE / NEAR F1..F3 / STABLE F1..F3`、当前目标和功率条。它不显示 nominal `Yaw/Pitch/Power`、标准轨迹、精确修正量或隐藏自动瞄准。

## 7. 逐目标接近预览

目标由当前预测的 `ValidAssistMask` 选择最早未完成项：

| ValidAssistMask | 远端预览 |
|---|---|
| `0` | 行星 ① |
| `bit0` | 行星 ② |
| `bits0..1` | 行星 ③ |
| `0x7` | UFO |

选择器使用 `0.20 s` 前进、`0.35 s` 回退滞回，避免输入边界附近闪烁。行星画面围绕权威轨迹的真实最近接近点构图；未进入目标区时也显示真实 miss，不伪造成功。UFO 预览使用独立 `GeometricContactCenterCM`，只有同输入 physical result 的 `TargetContact` 才表示实际接触。

Scene Capture 复用一个 transient Render Target，限频手动捕获，并只显示 M11-B 明确注册的表现 Actor。目标中心仍从 Preset 经 Finale Frame 变换得到，Actor Transform 只用于显示而非权威。

## 8. 轨道全景图

M11-C 在 M11 自有实现中冻结并复现 M10.1-C 的投影语义，避免修改共享 M10 文件：

1. 对完整预测/播放路径做弧长加权 PCA，得到轨迹最佳拟合平面；
2. 退化时使用发射方向、主星方向的确定性 fallback；
3. 调整平面 X 轴符号，使发射点始终位于图左侧；
4. 全部 3D 点正交投影到拟合平面；
5. 由完整投影路径的内容包围范围自适应构图，不强制显示完整主星；
6. 对主星、三颗行星做解析投影球遮挡，并在折线跨越球轮廓时插入解析交点；
7. 只有确实位于任一天体后方的轨迹段画虚线，其他段画实线；
8. 主星绝对经纬网以 World 坐标定义，再变换到终局局部和平面；三颗助推星不画经纬网；
9. 三颗行星和 UFO 使用简单平面线条 glyph，不依赖静态网格轮廓；
10. 保留段类型/遮挡变化后抽稀到可控绘制点数，分类仍使用完整求解器 Result；
11. 轨迹、主星、助推星作用圈、全部行星/UFO glyph 都先在归一化图空间裁剪到单位圆，禁止任何图元越出左下圆形面板。

圆形轨道图放在屏幕左下、侦察圆下方和物品 HUD 旁；终局层先绘制，M10/M5 既有 HUD 后绘制，保证物品与 modal 不被覆盖。远端接近预览位于屏幕上方中部。

## 9. 确定性实飞与终端接管

### 9.1 玩家权威段

播放点直接来自 Release 的 `FABTSM11TrajectoryResult`。鸟的位置由 Finale Frame 变换后无 Sweep 设置；碰撞与 Chaos 在深空段保持关闭。播放器以绝对求解器时间采样相邻点，位置/速度使用时间 Hermite 插值，渲染帧率和播放倍速只改变“当前显示到哪个绝对时间”，不改变轨迹本身。

失败输入只播放自己的权威结果，不得接入 nominal 尾段。

### 9.2 同输入物理命中优先

只有 `Classification.IsF(4)` 后才允许成功演出：

- 若同一 Release 输入的 Physical Request 自己抵达 800 cm UFO，计划从头到尾保持该输入；
- 若同输入 physical miss，则保留玩家 qualified 轨迹到 F4 终点，再进入显式终端转移；
- 16,000 cm qualified `TargetHit` 不能冒充 800 cm `TargetContact`。

### 9.3 可见 C2 TerminalTransfer

终端转移使用版本化五次多项式，起点取玩家 F4 终点的 `(p,v,a)`，终点取冻结 nominal physical 轨迹中更晚的 `(p,v,a)`：

- 位置、速度和加速度在两端连续；
- 在冻结时长集合中按从短到长的固定顺序选第一条合格桥；
- 以固定采样步长检查主星和三颗行星的解析球净空；
- 限制最大加速度和 jerk；
- 接入点之后只播放 nominal 物理尾段，最终以 800 cm `TargetContact` 结束；
- 计划记录 transfer 起止时间、segment kind 和非零 Plan Hash；
- HUD 将该段画成明确的琥珀色实线，并标注 `TERMINAL TRANSFER`；
- 该段绝不因“接管”画虚线；虚线仍只表示被天体遮挡。

这不是 Release 时的隐藏吸附：玩家输入、玩家轨迹和 F4 结果都保持不变，转移只在玩家已经完成三次助推和合格拦截后出现。

### 9.4 失败安全与恢复

错误发射仍先播放自己的权威点列。表现终点由与实飞相同的 Hermite 插值求得：按鸟体净空半径检测主星和三颗行星，停在第一次解析体碰撞前；没有体碰撞的长时间 miss 最多展示 `6 s` 实时时长，避免失败轨迹把玩家锁住数十秒。

到达表现终点后执行冻结的最小失败时序：

1. 保留可读失败画面 `1.25 s`；
2. 用 `0.60 s` 淡入全黑；
3. 在全黑阶段只恢复一次鸟、弹弓袋、Party 模式和输入/点击路由；
4. 至少提交一帧全黑，再保持 `0.40 s` 并用 `0.45 s` 淡出；
5. 回到 `Ready`，下一次点击可重新进入终局。

大帧间隔跨越恢复边界时也必须钳制到全黑边界，不能把全黑帧整个跳过。鸟回位的顺序固定为“保持碰撞关闭 → 传送到进入终局前的 Transform → 恢复运动/碰撞”，防止鸟在失败端点重新启用碰撞后嵌入主星。黑屏覆盖层在 `Super::DrawHUD()` 后绘制，确保库存、Party HUD 和 modal 都不能盖到黑屏上方。M11-D 只扩展四鸟、环境和剧情快照，不替换该安全恢复原语。

## 10. 代码落点

| 文件/类型 | 职责 |
|---|---|
| `ABTSM11FinaleInteractionTypes.*` | 稳定器、目标选择、播放、失败恢复、输入 Release 门和轨道图纯数据合同 |
| `ABTSM11FinalePlayback.cpp` | 同输入优先、C2 转移、nominal tail、绝对时间采样和 Plan Hash |
| `ABTSM11OrbitalDiagram.cpp` | 拟合平面、投影、自适应构图、解析遮挡和绝对经纬网 |
| `AABTSM11FinaleInteractionSystem` | 身份门、纯数据 future 回收、Release、单鸟播放、Game Thread Scene Capture、失败恢复和状态机 |
| `AABTSM11PlayerController` | Space/M6 fail-closed 分流、进入手势隔离、相对鼠标捕获与 M11 输入 |
| `AABTSM11FinaleHUD` | 圆形裁剪后的终局轨道图、远端预览、稳定器/目标/功率反馈和失败黑屏 |
| `AABTSM11GameMode` | M11-B Ready 后创建唯一 Interaction System，并使用 M11 Controller/HUD |

`ABTSM11FinaleSystem`、M11-A Solver、M11-B Preset/认证、M6、M9 和 M10.1-C 共享源码都不为本阶段改变算法语义。

## 11. 自动化与编译门槛

### 11.1 M11-C 快速单元组

| 测试 | 阻断内容 |
|---|---|
| `ABTS.M11C.Unit.Stabilizer` | Near/Core/Stable、捕获/释放滞回、raw/effective 分离、F2/F3 身份和取消 |
| `ABTS.M11C.Unit.TargetSelector` | 最早未完成助推、前进/回退滞回、qualified 与 physical UFO 语义 |
| `ABTS.M11C.Unit.PreviewReleasePlayback` | Preview/Release Hash、同输入直达、C2 转移、30/60/120 Hz 绝对时间采样 |
| `ABTS.M11C.Unit.OrbitalDiagram` | 完整构图、发射点在左、球后虚线、可见实线和主星绝对经纬网 |
| `ABTS.M11C.Unit.PIERegressionContracts` | 进入手势 Release 门、焦点丢失、表现袋/权威 Pouch 分离、失败全黑恢复与 Hermite 体净空终点、圆形 glyph 裁剪 |

### 11.2 F4 接管闭包

`ABTS.M11C.Certification.TerminalTransferDomain` 必须：

1. 按 M11-B 最终精度重扫闭包 `Yaw[-2.25,3] × Pitch[27,34] × Power[0.925,1]`；
2. 得到 `21,025` 个样本和冻结的 `558` 个 F4；
3. 对每个 F4 输入重新求 qualified 与同输入 physical 结果；
4. 对每个输入建立“同输入直达”或“可见 C2 转移 + nominal tail”计划；
5. 全部计划非零 Hash、通过解析净空与运动上界，并最终抵达 800 cm physical UFO；
6. 任何一个 F4 无法建立安全计划都阻断 M11-C。

### 11.3 提交前回归

- `AngryBirdsToSpaceEditor Win64 Development` 默认 Unity 全链接；
- `ABTS.Contracts`；
- `ABTS.M110`；
- `ABTS.M11A`；
- `ABTS.M11B.Unit`；
- `ABTS.M11B.Runtime`；
- 全部 `ABTS.M11C`；
- `git diff --check`。

M11-A Solver 和 M11-B 布局/Hash 未改变时，不重复昂贵的 M11-B ConstructiveSearch/FullInputDomain；M11-C 自己仍必须执行第 11.2 节的 558 样本接管闭包。

### 11.4 本次执行结果

| 门槛 | 结果 | 证据 |
|---|---:|---|
| Development Editor 强制完整 Unity 全链接 | 通过 | `-ForceUnity -DisableAdaptiveUnity`，`Result: Succeeded` |
| `ABTS.M11C.Unit` | `5/5` | `Saved/Logs/M11C-20260729-030948-PIEFix-Unit-Final2.log` |
| `ABTS.M11C.Runtime` | `1/1` | `Saved/Logs/M11C-20260729-031028-PIEFix-Runtime-Final2.log` |
| `ABTS.M11C.Certification.TerminalTransferDomain` | `1/1` | `Saved/Logs/M11C-20260729-000655-TerminalTransferDomain-Final.log` |
| `ABTS.Contracts` | `2/2` | `Saved/Logs/M11C-20260729-031125-PIEFix-Regression-Contracts-Final2.log` |
| `ABTS.M110` | `4/4` | `Saved/Logs/M11C-20260729-031205-PIEFix-Regression-M110-Final2.log` |
| `ABTS.M11A` | `8/8` | `Saved/Logs/M11C-20260729-031251-PIEFix-Regression-M11A-Final2.log` |
| `ABTS.M11B.Unit` | `8/8` | `Saved/Logs/M11C-20260729-031333-PIEFix-Regression-M11B-Unit-Final2.log` |
| `ABTS.M11B.Runtime` | `4/4` | `Saved/Logs/M11C-20260729-031414-PIEFix-Regression-M11B-Runtime-Final2.log` |
| `ABTS.M7`（含 M7.3） | `14/14` | `Saved/Logs/M11C-20260729-031453-PIEFix-Regression-M7-Final2.log` |

接管闭包现场重扫得到 `21,025` 个样本、`558` 个 F4，计划分布为 `DirectPhysical=1`、`VisibleTerminalTransfer=557`、失败 `0`。每条计划都由当前输入重新求 qualified/physical 结果，再接受冻结 nominal tail；没有把标准输入扩大成玩家成功域。

强制 Unity 首轮复现并确认了并行工作树报告的符号问题：`ABTSM11FinaleSystem.cpp` 匿名命名空间的通用 `IsFiniteVector` 会与 `ABTSM11GravityAssist::IsFiniteVector` 进入同一翻译单元并造成未限定调用歧义。现已改为职责唯一的 `IsFiniteFinaleBoundaryVector`。同一门槛还发现并修复了 M11-C 两个 cpp 各自名为 `SameInput` 的 Unity 重定义，分别改为 `SameInteractionInput` 和 `SameSolvedInput`；最终强制 Unity 全链接通过。

本轮 PIE 报告的 `FAppTime` ensure 根因也已确认：旧实现从普通 ThreadPool worker 直接 `AsyncTask(GameThread)` 发布结果，worker 没有从 Game Thread 继承 `FAppTime` 上下文，随后触发的 Scene Capture/Renderer 工作会沿错误上下文链进入渲染。现改为 worker 只返回纯数据 future，由 Actor 原生 Game Thread Tick 一次性消费；Scene Capture 只在该 Tick 尾部执行。自动化使用 `NullRHI`，因此能验证合同与生命周期但不能证明实际 Renderer 像素路径；第 12 节有渲染 PIE 仍是最终验收门槛。

## 12. PIE 验收

### 12.1 进入与输入

- [ ] 实际验收地图使用 M11 GameMode；日志先出现 M11-B Ready，再出现 M11-C Entry Ready；
- [ ] 点击唯一 Space 弹弓进入终局瞄准，松开这次进入点击不会立即发射；必须再次按下并松开才会 Release；
- [ ] 鸟与袋进入后立即采用当前输入的拉弓表现位置，第一次鼠标移动就连续跟随；表现拉伸不改变预演/Release 的 canonical Pouch 初态；
- [ ] 终局内为隐藏光标相对鼠标输入；切出窗口、切回窗口或使用一次右键镜头操作后不会丢失瞄准，也不会因遗留 Release 自动发射；
- [ ] 普通弹弓仍按 M6 发射；Space 弹弓不会触发 Chaos Release；
- [ ] 鼠标可调整 Yaw/Pitch，滚轮可连续调整 Power；初始输入不是 nominal 标准答案；
- [ ] `R` 可退出 Stable 或重置当前 M11-C 尝试。

### 12.2 HUD 与可读性

- [ ] 左下圆形轨道图不遮挡侦察图、物品栏和弹弓；
- [ ] 主星、三颗行星、作用圈、UFO 和全部轨迹线都被严格裁剪在圆形面板内，没有行星或 glyph 越界；
- [ ] 轨迹完整显示且发射点在左，偏转时拟合平面与构图稳定；
- [ ] 主星绝对经纬网随视角/发射方向表达空间关系，三颗助推星不画经纬网；
- [ ] 天体后方轨迹为虚线、无遮挡轨迹为实线；琥珀色 TerminalTransfer 仍为实线；
- [ ] 行星 ①→②→③→UFO 预览只在对应助推有效后推进，边界移动时无高频闪烁；
- [ ] 连续进入、退出并重新进入终局，远端预览正常刷新；日志中不再出现 `FAppTime`/`IsInGameThread()` ensure；
- [ ] 未成功接近时显示真实 miss，不把 16k qualified 包络画成 800 cm UFO 接触；
- [ ] HUD 不显示 nominal 轨迹、标准三轴数值或精确修正箭头。

### 12.3 稳定器

- [ ] 进入 Trust 外一格附近只降敏并显示 `NEAR Fn`，不改变 desired input；
- [ ] 在 Core 且 Fn 连续成立后显示 `STABLE Fn`，只限制已取得前缀；
- [ ] 继续寻找下一行星时仍有可玩调整范围；
- [ ] 向 ReleaseEnvelope 外持续推动或按取消后恢复完整输入；
- [ ] F2/F3 即使几何 box 相同，也能按权威分类逐级显示。

### 12.4 Release 与实飞

- [ ] Release 后轨迹与最后一帧预演一致，日志的 Source/Plan Hash 非零；
- [ ] 30/60/120 fps 或明显帧率波动不改变路径和结局；
- [ ] 失败输入只沿自己的轨迹飞偏/撞击，不突然切向标准答案；
- [ ] 撞主星/行星的错误发射在鸟体接触前结束表现，随后出现完整黑屏并恢复；鸟不会先重新启用碰撞再传送，也不会扎入地表卡死；
- [ ] 没有体碰撞的长 miss 在有限时间内进入失败黑屏，不会按求解器几十秒全时长锁住玩家；
- [ ] 恢复只发生一次，至少显示一帧完全黑屏；淡出后鸟、弹弓袋、Party 模式和输入路由可立即再次使用；
- [ ] F4 同输入能物理接触时直接抵达 UFO；
- [ ] F4 同输入不能接触时，先完整抵达自己的 qualified endpoint，再显示琥珀色连续转移并接 nominal tail；
- [ ] 转移起止没有位置、朝向或速度跳变，不穿主星/三颗行星；
- [ ] 只有抵达 800 cm physical UFO 后进入 `TargetHit`；
- [ ] 移动/隐藏 M9 卫星或修改其引力参数不改变预演、Hash 或实飞。

### 12.5 期望日志

```text
[ABTS][M11-C][GameMode] Entry Ready=1 StartCell=...
[ABTS][M11-C][Interaction] Ready ...
[ABTS][M11-C][Release] Source=0x... Plan=0x... F4=... Physical=... Transfer=...
[ABTS][M11-C][Failure] Begin Reason=... Hold=... FadeIn=... Black=... FadeOut=...
[ABTS][M11-C][Failure] RestoredAtBlack Reason=...
[ABTS][M11-C][Failure] RecoveryComplete Reason=...
[ABTS][M11-C][Playback] TargetHit Plan=0x... Transfer=...
```

任一 `InteractionContract`、`ReleasePreviewIdentityMismatch`、`PlaybackSamplingFailed` 或上游 M11-B Rejected 都是阻断错误。

## 13. M11-D 交接清单

1. M11-D 只把四鸟编队映射到同一 `FABTSM11PlaybackPlan` 的切线/法线标架，不复制积分器；
2. 环境切换和完整 Attempt Snapshot 必须扩展 M11-C 已有的鸟/弹弓/输入最小恢复原语，在进入终局前保存，并在同一全黑恢复点原位恢复；
3. `TargetHit` 是 800 cm UFO 接触，之后才允许白鸟救援、UFO 局部 Chaos 和剧情；
4. M11-C 的 `VisibleTerminalTransfer` 必须继续明确呈现，不得被镜头剪辑伪装成普通求解器轨迹；
5. 失败镜头使用 M11-C 已分类的最早可证原因及冻结 Presentation End；没有公开证据时使用通用 miss，不在表现层重算物理或延长到未裁剪的求解器全时长；
6. M11-D 不改变 Preset、Scenario、Trust、Transfer Contract 或 Plan Hash；如需改变，必须回到 M11-C 重新跑 558 样本闭包。

## 14. 多工作树集成交接

本稿和 `ABTSM11*` 实现属于 M11 专属工作树。集成所有者合并时应：

1. 先确认共同 base 与 M3/M7/M11 各工作树交接单；
2. 只挑选 M11 所属提交，不从工作树目录直接复制整个工程；
3. 重点审查 `AABTSM11GameMode` 的并行冲突，保留 M11-B 初始化顺序后再接 M11-C；
4. 在合并后的默认 Unity Build 中验证 `ABTSM11FinaleSystem.cpp` 的边界辅助函数不会与 `ABTSM11GravityAssist::IsFiniteVector` 同名冲突；
5. 在原始集成仓库更新共享 [项目工作流](ABTSProjectWorkflow.md)，把当前实施稿链接改为本文，并记录 M11-C 自动化/PIE 状态；
6. 在合并产物上重新执行第 11.3 节快速回归和第 12 节 PIE。

M11 工作树不直接修改共享项目工作流、M3/M7/M10 源码、配置、Build.cs、Target 或生产地图资产。

返回父级：[M11 终局三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md) · 上游：[M11-B 局部布局与全输入域认证](M11BFinaleLayoutCertificationDesign.md) · 返回入口：[ABTS 项目工作流](ABTSProjectWorkflow.md)。
