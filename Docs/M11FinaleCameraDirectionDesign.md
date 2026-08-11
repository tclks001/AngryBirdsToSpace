# M11 终局实飞镜头导演与独立验收设计

> 编码：UTF-8，简体中文。  
> 状态：M0/M1/M2 已完成；M3 多行星导演迭代中；连续飞行拖尾 T2-R 已以纯 C++ 三层软 Sprite 替换默认 `DrawPoint`，完成亮面清晰核心增强、自动化与 Rank11 三渲二 fresh 录屏，并于 2026-08-10 通过用户主观验收。
> 所有权：`feature/m11-finale`。本文只描述 M11 实飞镜头与其验收，不修改集成工作树拥有的三渲二实现、共享地图或稳定跨阶段契约。

## 1. 目标与本轮边界

目标是在任意 M11 冻结布局（生产 Rank 0 或编辑器候选 Rank 1–11）上，以同一条确定性最佳路线完整播放主控鸟飞行，并把镜头表现做成可重复比较的独立录屏。最终镜头应接近参考视频的阅读方式：飞向当前行星时逐渐拉近，进入近掠窗口时逐渐拉远，离开后把叙事重心平滑交给下一颗行星；画面始终同时保留主控鸟和当前叙事行星。

M0 已完成独立录制闭环；本轮 M1 只在其上增加只读观测和离线判据：

- 复用当前 `AABTSM11FinaleFlightCamera`，不改变其位置、朝向、FOV、Lag 或切镜逻辑；
- 自动选择 Rank 的 `NominalInput`，走现有 M11-C 求解、发布和权威播放链；
- 启动前可选择 Rank 0–11 与原渲染/三渲二；
- 自动创建只读 SceneCapture、逐帧写 JPG，并在同一 UE 进程内封装 MJPEG AVI，输出 AVI、Manifest 和独立日志；
- Rank 1–11 始终标记为 `UNCERTIFIED`，录屏不构成全操作域认证；
- 每个影像帧同步输出当前鸟/叙事目标、相机和阶段观测，离线量化旧镜头失败；
- 暂不实现四鸟编队、UFO 救援、破碎、音效、三渲二参数调优或共享地图修改。

## 2. 分层架构

```text
冻结布局 / Certified v1
        |
        v
M11 FinaleSystem + NominalInput       只读权威层
        |
        v
M11-C Interaction / PlaybackPlan      既有确定性播放层
        |
        +----> 主控鸟权威位置、速度、事件、Hash
        |
        v
Finale Flight Camera                  镜头消费层
        |
        v
M11 Capture Runner                    独立验收层
        +----> Rank / Stylized 启动选项
        +----> 复制旧相机最终 View / SceneCapture2D
        +----> 连续 JPG / MJPEG AVI / JSON Manifest / AbsLog
```

导演层只消费权威轨迹，不改变求解输入、积分点、事件顺序、命中分类或 Hash。三渲二是相机之外的正交维度：录制工具只调用集成侧公开的 `FABTSStylizedRenderingControl` 开关和 `FinaleSpace` Profile，不复制渲染参数。

## 3. M0 独立录制合同

### 3.1 启动参数

工具由显式参数 `-ABTSM11CameraCapture` 启用。正式入口是从 PowerShell 启动一个 fresh `UnrealEditor.exe -game` 进程。M11 Runner 先设置 Rank/渲染方式并预热渲染，然后创建独立 `SceneCapture2D + TextureRenderTarget2D`；每帧只读复制当前 `PlayerCameraManager` 已结算的位置、旋转、FOV 和后处理，以固定帧率写连续 JPG。至少两个起始帧落盘后才提交 `NominalInput`；`TargetHit` 和终帧停留完成后，Runner 在同一 UE 进程中把 JPG 无重编码封装为 MJPEG AVI、写 Manifest，再请求本进程退出。整个过程不使用 PIE、鼠标键盘、桌面录屏软件或电脑控制，也不依赖窗口焦点。

```powershell
$Editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$Project = '<M11 工作树绝对路径>\AngryBirdsToSpace.uproject'
$Output = '<M11 工作树绝对路径>\Saved\VideoCaptures\M11CameraCapture\<唯一运行名>'
$Log = '<M11 工作树绝对路径>\Saved\Logs\<唯一运行名>.log'

& $Editor $Project /Game/Maps/L_ABTS_M11 `
  -game -dx11 -RenderOffscreen -ForceRes -ResX=1280 -ResY=720 `
  -ABTSM11CameraCapture -ABTSM11CaptureRank=11 `
  -ABTSM11CaptureStylized=1 -ABTSM11CaptureAutoExit=1 `
  -ABTSM11CaptureWarmupFrames=8 `
  -ABTSM11CaptureTerminalHoldFrames=24 `
  -ABTSM11CaptureTimeoutSeconds=300 `
  "-MovieFolder=$Output" '-MovieName=Rank11_Stylized' `
  -MovieFormat=JPG -MovieFrameRate=30 -MovieQuality=90 `
  -NoSound "-AbsLog=$Log"
```

命令返回即代表 JPG 序列和 AVI 已由该 UE 进程同步写完，但退出码本身不是充分证据；调用方仍须检查 AVI 存在且大于 4096 bytes、Manifest `status=Complete`、帧数连续，并确认日志包含唯一成功链路。M0 明确保留少量旧相机起始画面；录制先于发射至少两个落盘帧，满足“开启录屏 → 发射”。SceneCapture 只是验收相机：它复制旧相机最终 View，不成为玩家 ViewTarget，不修改权威相机、轨迹或演出。

| 参数 | 含义 | 默认 / 限制 |
| --- | --- | --- |
| `-ABTSM11CaptureRank=<0..11>` | 本次布局。0 为生产 Certified v1；1–11 为冻结未认证候选。 | `0` |
| `-ABTSM11CaptureStylized=<0|1>` | `0` 原渲染；`1` 开启 `abts.Rendering.Stylized.Enabled` 并选择 `FinaleSpace` Profile。 | `0` |
| `-ABTSM11CaptureAutoExit=<0|1>` | AVI 完成写盘后退出本次 `-game` 进程；正式命令行录制必须为 `1`。 | `1` |
| `-ABTSM11CaptureWarmupFrames=<n>` | 设置渲染方式后、进入发射前的预热帧。 | `30` |
| `-ABTSM11CaptureTerminalHoldFrames=<n>` | `TargetHit` 后保留终帧的帧数。 | `24` |
| `-ABTSM11CaptureTimeoutSeconds=<s>` | 全流程硬超时，超时 fail closed。 | `180` |
| `-MovieFolder=<absolute>` | 连续 JPG、AVI 与 Manifest 的唯一输出目录。 | 必填、必须可写且无同名旧帧 |
| `-MovieName=<plain-name>` | 不含格式 token 和路径分隔符的帧/AVI 基名。 | 必填 |
| `-MovieFormat=JPG` | 固定逐帧中间格式；禁止回退到 UE 旧版原生 AVI 直写。 | standalone 必填 |
| `-MovieFrameRate=<1..120>` | 固定时间步与 AVI 帧率。 | `30` |
| `-MovieQuality=<1..100>` | JPG 质量。 | `90` |
| `-ResX/-ResY` | SceneCapture 与 AVI 分辨率。 | `1280×720` 验收基线 |

同一 `-game` 进程启动后，Rank 和渲染方式被冻结；修改选项必须等待本次进程退出后重新启动。Rank 1–11 仅在显式 `-ABTSM11CameraCapture` 的 Editor Game World 或 PIE 诊断世界中启用，不会进入未显式启用工具的普通 Standalone、打包版本或生产默认路径。

### 3.2 状态机

```text
WarmingRenderMode（不录制，等待渲染资源就绪）
  -> RecordingStarted（至少 2 个起始帧）
  -> WaitingForDependencies
  -> EnteringNominalAttempt
  -> WaitingForLaunch
  -> Recording
  -> HoldingTerminalFrame
  -> MuxingMJPEGAVI
  -> Complete / Failed
```

关键门禁：

1. 只允许一个 ready `FinaleSystem`、一个 ready `InteractionSystem`、一个本地 PlayerController，以及一个与 Finale Slot Pair 精确匹配的太空弹弓 Cord。
2. 自动发射必须通过 `TryEnterFinale` 和 M11-C 的 ReleasePending/PreviewSolve/PlaybackPlan 链；禁止直接搬运鸟或拼造轨迹。
3. 输入必须精确等于当前 Preset 的 `NominalInput`。录制先于发射至少两个落盘帧；Runner 只有在旧飞行相机已成为 ViewTarget、开始消费 authority sample 后，才承认进入飞行录制阶段。
4. 只有 `TargetHit` 才是成功终点；`Failed`、依赖丢失、SceneCapture/RenderTarget 读取失败、帧号不连续、JPEG 身份失败、AVI 封装失败、超时或空 AVI 都失败。
5. Runner 在 `TargetHit` 后完成终帧停留，验证 `000000..N-1` 连续 JPG，再写 RIFF/AVI 主头、`MJPG` 视频流、`movi` 帧块和 `idx1` 索引；只有 AVI 大小有效且 Manifest 写入成功才以退出码 0 结束。

### 3.3 产物身份

Manifest 至少记录：

- 工具版本、UTC 开始/结束时间；
- Rank、`Certified` / `UNCERTIFIED` Authority；
- Stylized 开关、Profile、渲染实现版本；
- Preset Hash、Certified Bundle Hash；候选时附 Candidate Source/Result Hash；
- Released Trajectory Hash、Playback Plan Hash、终局状态；
- AVI 绝对路径、最终字节数、帧数、帧率、分辨率和 JPG 序列通配符；
- 失败原因。

AVI 是视觉证据；Manifest/日志是身份与流程证据。两者缺一不算完成。

从录制合同版本 3 起，Manifest 还必须写出 `stylizedViewClass=FinaleCinematicCapture`、组件注册状态及 resolved policy。它们只证明录制组件已接入预期风格策略，不能单独证明 Tone/Outline 已进入最终像素；最终仍以从 AVI 本体解码的帧为准。

M11 历史分支的录制合同 v4/v5 分别引入逐帧 `.camera-observations.csv` 与 M2 导演扩展；Manifest 记录其路径、Schema、行数和只读诊断摘要。CSV 缺失或行数与影像不一致时整个录制 fail closed。合并集成线独立占用的 T2-C1 v4 全程渲染状态门后，首个无歧义的完整合同为 v6；它同时要求 CSV Schema 2、导演遥测和 `stylizedRuntimeState*` 全程证据。字段、历史证据与离线判据见 [M1 观测设计](M11FinaleCameraObservationAndOfflineCriteria.md)。

### 3.4 M0 fresh-process 基线证据

2026-08-05 以 Rank 11、Stylized 1、DX11、RenderOffscreen、1280×720、30 fps 运行一次完整旧镜头基线：

- UE 进程自动预热、写 4 个起始帧、提交 Rank 11 `NominalInput`，最终到达 `CandidateQualified-UNCERTIFIED TargetHit`；
- 同一进程写出 `000000..000948` 共 949 个连续 JPG，封装 MJPEG AVI 后以状态 0 自行退出；
- AVI `avih.dwTotalFrames`、视频流 `strh.dwLength`、`idx1` 条目均为 949，分辨率 1280×720，时长 31.633333 秒，文件 21757000 bytes，SHA-256 为 `C5B8EB3088B9DD6471DA0808062CBEDB50FC7A6CF7DEF725DE6FB1AF4296A1B3`；
- 抽帧显示发射起点与鸟画面有效，但旧飞行镜头随后长时间只留下浅蓝天空。这是 M0 忠实记录的旧镜头失败基线，不是导演验收通过；后续 M1/M2 应直接用它量化和修复。

### 3.5 Integration 风格化离屏视图复验

2026-08-05 由集成工作树为录制器增加专用 `FinaleCinematicCapture`，保持主视图扩展拒绝未知 Scene Capture，并将风格化实现版本升为 6。随后按第 3.1 节命令以 Rank 11、Stylized 1 运行 fresh DX11 RenderOffscreen：

- 输出 `Rank11-Stylized-FinaleCapture-v6-20260805-230434`，Manifest 合同版本 3、状态 `Complete`、原因 `TargetHit`，并记录 ViewClass、注册和三项 policy 均有效；
- JPG 与从 AVI 本体扫描解码的 MJPEG 帧均为 949，1280×720、30 fps；AVI 19506476 bytes，SHA-256 `FE16E64797EE8101E414E1781C51EE730EF7B9EC45D400966B6B8A81C65CA3A1`；
- AVI 第 3 帧可见带稳定轮廓的鸟、弹弓及远端球体；第 250/500 帧仍能看到鸟和行星轮廓，证明独立 SceneCapture 已实际消费 Outline，不再是仅有实现版本号的假阳性；
- UFO 在终段仍不足以从远景球形代理中可靠辨认，终帧也没有形成可读的鸟/UFO 双目标构图。因此本次只关闭风格化离屏接线缺口，不宣称旧镜头导演验收通过；UFO 可读性仍归 M1–M4。

## 4. 后续镜头导演模型（本轮不实现）

### 4.1 叙事目标与阶段

每个时刻由权威事件和轨迹预测选择一个 `CurrentBody`，阶段分为：

- `CruiseToBody`：下一行星仍远，建立鸟与行星的相对方向；
- `Approach`：逐渐拉近，增强速度感与目标尺度；
- `Periapsis`：近掠窗口逐渐拉远，保证行星边缘与鸟同时可读；
- `Departure`：保持本颗行星作为运动参照，准备目标交接；
- `Handoff`：本颗与下一颗短暂共同参与构图，带迟滞切换；
- `FinalApproach`：以 UFO 代替行星，沿同一双目标构图合同工作。

阶段不按固定秒数硬切。首选权威 Assist Enter/Closest/Exit 和 TargetApproach 事件；事件间用轨迹到目标表面的距离、径向速度符号与预计最近点时间插值。

### 4.2 双目标构图约束

对鸟 `B` 和当前目标球心 `P`：

```text
SurfaceDistance = |B - P| - BodyVisualRadius
RadialSpeed     = dot(Vbird - Vbody, normalize(B - P))
LookAt          = lerp(B, P, TargetWeight)
RequiredFOV     = FitProjectedBounds(BirdBounds, BodyBounds, SafeFrame)
CameraDistance  = clamp(FramingDistance, MinDistance, MaxDistance)
```

导演优先级固定为：

1. 主控鸟必须在 Bird Safe Frame 内；
2. 当前目标必须有可读面积，近掠时允许球体超框但必须保留最近边缘和曲率；
3. 地平线/行星边缘不遮挡鸟；
4. 在满足以上约束后最小化相机加速度、角速度与 FOV 速度；
5. 下一目标只在 Handoff 窗口提高权重，不能导致当前目标提前跳走。

“飞近时拉近、近掠时拉远”采用 Dolly 为主、FOV 为辅：普通段优先移动相机维持透视，只有距离或遮挡达到边界时才改变 FOV。所有权重使用临界阻尼或具有进入/退出迟滞的平滑曲线，禁止按单帧最近目标硬切。

### 4.3 与四鸟编队的未来兼容

当前 `BirdBounds` 只包含主控鸟。四鸟编队接入后只把它替换为编队包围体，并保留同一阶段、目标与安全框合同；不允许为了编队重新定义权威轨迹或候选身份。

## 5. 阶段里程碑与验收

### M0：旧镜头独立录制基线（已完成）

- Rank 0–11 与 Stylized 0/1 可在启动参数中独立选择；
- Rank 11 + Stylized 1 能从 Nominal 起点自动飞到合格终点；
- 使用旧 `AABTSM11FinaleFlightCamera`，不含导演改动；
- fresh `UnrealEditor.exe -game` 命令输出连续 JPG、可读取 MJPEG AVI、`Complete` Manifest 和唯一 AbsLog，AVI 封装完成后自动退出本次 UE 进程；
- Manifest 明确 Rank 11 为 `UNCERTIFIED`。

### M1：导演观测与离线判据（已完成）

详细合同、阈值与四象限证据见 [M11 终局镜头导演观测与离线判据](M11FinaleCameraObservationAndOfflineCriteria.md)。

- 为每帧输出鸟/当前目标屏幕坐标、可见比例、目标像素半径、相机距离、FOV、阶段和切换原因；
- 先在旧镜头上量化离框、目标丢失和镜头跃变，不改变画面；
- Rank 0 与 Rank 11、Stylized 0/1 的指标身份一致且渲染方式不影响阶段决策。

2026-08-05 fresh 四象限基线显示：Rank11 两种渲染均有 946/946 个有效飞行帧丢失当前叙事目标；Rank0 两种渲染均为 391/1140。两组的阶段决策与阈值判据指纹分别跨 Stylized 0/1 相等，证明 M1 指标与渲染方式正交；这些失败是旧镜头的量化结果，不是录制器或 M1 失败。

### M2：单行星 Cruise Lead-in / Lucy 式 Approach / Periapsis 镜头（已完成）

- 只对 Assist1 启用双目标构图；Assist2/3 仍走旧镜头，便于 A/B；
- Cruise 后段提前建立鸟与 Assist1 的相对方向，不等到 Assist1Enter 才开始切入；
- 用 Enter/Closest/Exit 冻结 encounter right/up，以行星中心为视轴锚点，在实际投影平面编排左缘→盘前→右缘的前景穿越；
- Cruise 保留稳定盘外接近段，Approach 提高近星屏幕速度，Periapsis 快速出盘后减速；最近点把 FOV 从 50° 平滑推近到 30°，离开后恢复；
- Approach 后段与 Periapsis 前段共享 C2 连续后拉包络，目标像素半径在最近点形成宽峰而非阶段硬切；
- 主控鸟全程在安全框，目标至少保留可读边缘；
- 无单帧位置、旋转或 FOV 跳变。

详细设计与证据见 [M11 终局镜头 M2：Assist1 双目标 Cruise Lead-in / Approach / Periapsis](M11FinaleCameraM2SingleAssistDirection.md)。2026-08-06 Rank11、Stylized 0 A/B 中，Assist1 Cruise/Approach/Periapsis 当前目标丢失由 413/413 降至 67/413，鸟丢失 0、导演越界 0、位置/旋转/FOV 跳变均为 0；两次 Released Trajectory、PlaybackPlan 与阶段决策身份一致。首版 M2 到第 237 帧才看到 106.38 px 的 Assist1，提前 Lead-in 后第 55 帧即看到远景边缘。M2.1 建立左→右方向，M2.2 修复盘外假穿越，M2.3 再按 Lucy 参考重做速度与推拉：稳定盘外接近段 94 个速度样本，盘外/盘内中位速度为 `8.34 / 80.79 px/s`，近星加速 `9.68×`；行星峰值半径由 `136.87` 提高到 `238.22 px`，FOV 从 50° 平滑缩至 30° 后恢复。鸟心盘内 100 帧、轮廓相交 114 帧、179/179 个可观测帧鸟在行星前，鸟仍零丢失。当前颜色受大气散射影响，不属于 M2 结论。

### M3：三行星连续目标交接

- Assist1→2→3 的 CurrentBody 切换只发生在 Handoff 窗口；
- 切换前后相机位置、速度、朝向和 FOV 一阶连续；
- Rank 0 与 Rank 11 全程无鸟离框、无目标空窗、无 Frenet 翻转。

M3 已开始实现，详细合同与阶段门见 [M3 三行星连续导演与 Handoff](M11FinaleCameraM3MultiAssistHandoff.md)。2026-08-09 Rank11、Stylized 1 迭代已让 Assist1/2/3 进入同一套 Lucy 构图数学，两个 CurrentBody 切换均只发生在 Handoff，三颗左→右均成立，M3 窗口鸟丢失为 0、Approach 累计回撤为 0；但 Handoff 当前目标仍有 22 帧空窗，因此 `m3HandoffPassed=false`，M3 尚未完成。全局 FinalApproach 的鸟丢失与旋转跃变属于 M4，不混入本阶段放行结论。

首轮在三颗 Approach 开始后 0.75 秒均出现一次屏幕方向回跳；初次交叉淡化虽消除了单帧跳变，却把问题摊成 230–242 帧一类的连续慢回摆。最终状态职责改为：鸟优先安全构图只在 Handoff 工作并于末尾 0.50 秒释放，Approach 从第一帧起只运行唯一的 Lucy 位置解算。离线门同时检查负向单帧跳变和历史最右位置后的累计回撤。fresh Rank11 三颗 Assist 的两类回撤均为 0、M3 鸟丢失为 0、三颗左→右均成立，`m3NoApproachReversal=true`；Handoff 22 帧目标空窗仍为独立开放项。

后续节奏复核又确认，单用物理 `Periapsis/Handoff/Approach` 作为镜头状态会让不同候选、不同 Assist 的可用叙事时长失衡：木星没有足够盘外建立段，土星转轴则被压缩在 Handoff 尾部。现将权威 Stage 保持不变，在其上叠加 `OutgoingHold/IncomingReveal/IncomingTrack/IncomingEntryMatch`；下一行星按 AssistEnter 前 3.25 秒反向调度，并为上一行星保留 Closest 后至少 0.75 秒。Assist1 不再是旧 M2 百分比 Lead-in 特例，而把发射点视为虚拟 `LaunchAnchor`，从 playback 0 开始平滑获取火星，完成获取后进入稳定 Track。揭示曲线的末端位置和速度均与 Approach 入口匹配，从而把“何时开始讲下一颗行星”从物理作用域中解耦。该改造属于 M3 内部镜头导演，不改变候选、渲染或 PlaybackPlan。

跨行星空窗现由显式 `DualBodyBridge` 处理：`OutgoingHold` 把镜头扩到 85°，桥接解同时拟合鸟、上一行星和下一行星，并保持上一颗在左、下一颗在右；随后 `IncomingTrack/IncomingEntryMatch` 改用鸟与下一行星的双主体拟合，直到物理 Enter 后 0.50 秒才把位置权威交还 Lucy 穿越解。Rank11 fresh 证据已消除两次“零行星帧”，但远景桥两端仍包含显式匹配切换，旧的严格单镜头位姿/FOV 连续门仍失败；因此本轮只关闭空窗问题，不宣称 M3-B/M3-C 完成。桥接期 `1×→10×→1×` 鸟体视觉倍率现已撤销，鸟体保持原始 Scale；远景运动可读性改由独立连续拖尾承担，不能再用改模型尺寸补偿构图。

拖尾 T2-R 验收后，`M11-CAM-M3-007` 已按状态边界连续性契约完成重构。跨行星阶段现统一为 `OutgoingHold → DualBodyBridge → IncomingReveal → IncomingTrack → IncomingEntryMatch → Authority`：上一状态末姿态就是下一状态初姿态，Reveal 在三主体同框下预转到下一颗 Lucy 朝向，Track 再沿同一朝向从 85° 远景连续推入 50° Lucy 构图，EntryMatch 与 Authority 首帧使用同一个解。不可达的 Reveal 和 Enter 后重复远景的 PostEnterSettle 已消除，鸟体保持恒定 Scale。fresh Rank11 R5 的所有状态边界均低于 `1001 cm / 0.98°`，两次桥 36/36 帧双星同框，M3 鸟丢失 0，旧 `222→223` 倒退已变为单调前进。详细证据与旧离线门的已知语义差异见 M3 设计稿 7.5。

后续 `M11-CAM-M3-008` 将“到下一行星后再回拉到穿越入口”收敛为双星退出即开始的单次构图转换。`IncomingReveal` 与 `IncomingTrack` 不再各自安排朝向、位置和镜头焦段，而从桥接退出首帧到 `IncomingEntryMatch` 入口共享一个前置 Ease-out Match Alpha，同时驱动桥接朝向→Lucy 朝向、三主体拟合→Lucy 位置以及 `85°→50°` FOV。该 Alpha 在起止端均保持平滑，EntryMatch/Authority 端点合同不变。Rank11 fresh 录制中，木星段旧 `431→439 = 706.30→526.65 px` 已变为 `525.75→526.65 px`；土星段旧 `712→730 = 639.58→525.86 px` 已变为 `534.28→525.79 px`，不再出现到达后重新置左的可见回拉。详细证据见 M3 设计稿 7.6。

`M11-CAM-M3-009` 随后修正了上述统一 Alpha 引入的退场镜头回归：上一颗行星的离场保留在 `IncomingReveal` 的 85° 三主体宽景中；`IncomingTrack` 才在已经对齐下一颗 Lucy 朝向的同一视轴上，以独立的二维鸟屏幕锚包络和较慢的深度/FOV 包络推入 50° 穿越入口。由此“上一颗逐渐后退离场”与“鸟提前到达下一颗左侧入口”不再争用相机位置、朝向和焦段的同一权重。Rank11 fresh `M3EgressAnchorR5-20260810-222300` 中，火星在 413–420 帧、木星在 644–667 帧连续退场；鸟在两个交接窗口的最大单帧屏幕位移仅 `10.18/7.40 px`，且 `431→439` 与 `712→730` 均直接收敛到 Lucy 入口，没有二次回拉。详细证据见 M3 设计稿 7.7。

`M11-CAM-M3-010` 又为上一颗行星的 Lucy 前景穿越增加了显式清轮廓合同：`OutgoingHold` 不再能从 `Closest` 首帧抢占相机，而必须等待 `Closest→Exit` 归一化 progress `0.23`；跨行星调度同时硬保留至少 `0.50 s` 拉远、`0.60 s` Bridge、`1.30 s` Reveal、`0.60 s` Track 和 `0.50 s` EntryMatch，时间不足则 fail closed。最终 Rank11 fresh 录屏中，旧问题帧 328/548 都仍是 Authority，分别到 348/568 才开始拉远；整个 Outgoing 段的最小投影轮廓净空为 `2.99/22.83 px`，两段此前验收的上一行星渐退和下一颗左侧直飞入口均未回退。详细算法和证据见 M3 设计稿 7.8。

`M11-CAM-M3-011` 进一步把双星桥接期鸟的纵向位置从算术质心的偶然结果改为显式构图合同。`OutgoingHold → DualBodyBridge → IncomingReveal` 共享 `BirdNdcY=+0.05` 的 canonical 锚：Outgoing 从 Lucy 位置平滑获取，Bridge 固定持有，Reveal 平滑释放；只解 Camera Up，不改双星水平基线和鸟的 X 运动。锚定后重新检查鸟与两星的投影球 margin，必要时只沿镜头后方增加最小距离，因此不会以裁切换取鸟居中。Rank11 fresh `M3BridgeBirdAnchorR7-20260811-114000` 中，用户指出的 575–625 帧纵向范围从 `173.96 px` 降至 `65.70 px`，最大单帧上移从 `19.03 px` 降至 `2.31 px`，并保持单调；两次桥接仍 36/36 帧双星完整同框。详细求解、边界合同和证据见 M3 设计稿 7.9。

`M11-CAM-M3-012` 关闭开局 `IncomingReveal→IncomingTrack` 的轻微回退。该现象早于 011，根因是 Reveal 末帧向 lag 相机位置释放，而 Track 首帧在权重到 1 时直接采用精确导演位置。现在 `LaunchAnchor→Assist1` 全段共用同一开局携带限制器，并把位置/旋转的释放终点明确定义为精确导演 Transform；跨行星链和双星锚不变。Rank11 fresh `M3LaunchBoundaryR8-20260811-134500` 中，40→41 鸟 X 回退从 `-11.540 px` 降为 `-0.069 px`，41 帧后与 R7 构图数值等价，222→223 进入 Authority 仍同向连续。详细合同和证据见 M3 设计稿 7.10。

### M3-T：连续飞行拖尾与鸟体恒定尺寸（T2-R 三层软 Sprite 亮面增强完成）

- 发射全程使用同一组世界空间点粒子，不按行星、镜头状态、Rank 或渲染风格切换；
- 两帧鸟位置之间按世界弧长连续采样并跨帧保存距离余数；64 cm 名义分层采用确定性一维蓝噪声抖动，不再按帧生成同步粒子团；
- 每站点一个粒子并赋亚帧年龄；粒子以轨迹法平面的二维正态速度横移，`3σ`/硬上限为鸟速 `1/5`，横移越快寿命越短，形成慢速密集核心和稀薄外逸层；
- 默认表现由运行时生成的 `64×64` 径向纹理和 Halo/Contrast Shell/Core 三层 `DrawSprite` 构成；加性 Halo 保留暗背景辉光，预乘 AlphaComposite Shell/Core 在火星 319、木星 557、土星 812 附近建立亮面清晰核心；十二个外观 CVar 均可从命令行调整，`RenderMode=0` 只保留旧 `DrawPoint` A/B；
- 拖尾采用世界深度遮挡和屏幕空间恒定 Sprite 直径，近星高屏幕速度自然形成更长尾迹；
- `OutgoingHold/DualBodyBridge/IncomingTrack` 不再缩放 BirdVisual，重置/退出显式恢复原始 Scale；
- 跨状态倒退、硬切和入口失配已由后续 `M11-CAM-M3-007` 连续性重构关闭；拖尾继续与镜头状态正交；
- 不创建共享纹理/Niagara/材质/地图资产，不需要 Editor 操作。

详细结构、生命周期、命令行 A/B 和逐步验收见 [M11 终局连续飞行拖尾演出设计](M11FinaleBirdTrailPresentationDesign.md)。

### M4：UFO 终端与镜头收束

- FinalApproach 同时保持鸟与 UFO；
- Candidate Qualified Endpoint 与 Physical Contact 在 Manifest/镜头状态中明确区分；候选离线录屏从合格点追加确定性可见转移并抵达 800 cm 接触球；
- 本里程碑只处理镜头，不实现 UFO 破碎、救援和五鸟团聚。

2026-08-11 已完成工程落地，独立合同见 [M4：UFO 终端与镜头收束](M11FinaleCameraM4UFOTerminalClosure.md)。旧版在 Assist3 Exit 后退出 M3 导演并恢复瞬时轨迹切线跟随，导致 UFO 在 FinalApproach/Terminal 全程出画且入口旋转跳变；首轮 M4 建立“土星 Lucy→鸟＋土星＋UFO 广角桥→冻结鸟＋UFO 双主体”的连续构图。R2 随后暴露未认证候选仍在 41,250 cm 合格包络结束，故候选离线播放现保留原 Released 前缀并追加显式 `VisibleTerminalTransfer`，沿确定性五次曲线抵达 UFO 800 cm 接触球。fresh Rank11 Stylized1 `M4PhysicalContactR4-20260811-133500` 为 1021 帧 `Complete/TargetHit`；最终 Authority 距离 800 cm，M4 鸟/UFO/身份丢失与位置/旋转/FOV 跳变均为 0，`m4PhysicalContactPassed=true`、`m4TerminalClosurePassed=true`。Rank11 仍为 `UNCERTIFIED`，Released Trajectory Hash 不变；可见接触只属于播放表现计划，不冒充候选认证。

M4 终端构图现不再让鸟/UFO 围绕对称质心漂移，而将 UFO 固定在画面中心：鸟沿同一竖直中心线从 NDC Y `-0.42` 以恒定屏幕速度逼近至 `-0.22`，相机到鸟距离同时以零端斜率从 40000 cm 收至 5000 cm，55 度镜头保持不变。`TerminalAcquire` 直接匹配该解析构图的 progress 0；真实进度由 Assist3 Exit 与 PlaybackPlan 物理接触终点共同定义。该改造只改变 M11 导演与录屏合同 v13，不修改 Released 轨迹、接触曲线或渲染。fresh Rank11 Stylized1 `M4CenteredDollyR9-20260811-160100` 为 1021 帧 `Complete/TargetHit`：UFO 中心误差小于 `0.00002 px`，鸟相对中央竖线的动画 Bounds 摆动小于 `2 px`，相机到鸟距离约从 40015 cm 收至 5009 cm，最终仍精确命中 800 cm 接触球。

### M5：候选与渲染正交回归

- Rank 0、Rank 11 只在 Stylized 1 下制作正式视觉录屏；当前 Stylized 0 受原生大气散射影响为不可读蓝屏，不再重复生成无效视觉证据；
- 同一 Rank 的 Stylized 0/1 仍须通过 renderer-independent Telemetry 比较 Released/Playback Plan、阶段序列和相机数值 Hash；
- TelemetryOnly 仍启动 fresh `-game` 进程、真实发射并完整播放，但跳过 SceneCapture、JPG 和 AVI，只写 CSV/Manifest 后自动退出；
- Rank 1–10 在 Rank 0/11 基础矩阵通过后按需扩展；
- 三渲二像素质量和 Stylized 0 大气散射由集成工作树验收，M11 只验镜头可读性和逻辑身份。

导演时长统一解释为呈现秒：认证 Rank0 的 `PlaybackTimeScale=18` 只加速轨迹钟，进入 Director 前会把拉远、桥接、取得、Track 与 Entry 的秒数字段同比换算到轨迹钟；候选倍率 1 和几何 Progress 门保持不变。生产相机与录屏观测共用该入口，防止认证路线把连续镜头压缩成 1–3 帧。

2026-08-11 M5 基础矩阵完成：Rank0 和 Rank11 的 Stylized 0/1 同协议 Telemetry 分别得到完全相等的 Released、Playback Plan、Stage 与 Camera Hash；两条 Stylized 1 正式录像均 `Complete/TargetHit`、鸟丢失 0、空构图 0、M4 鸟/UFO 丢失 0、800 cm 接触与终端闭合通过。Rank0 时基修正前两个 Handoff 各连续丢鸟 10 帧，修正后归零；Rank11 倍率为 1，既有已验收构图不变。独立合同与产物清单见 [M5：候选与渲染正交回归](M11FinaleCameraM5RenderOrthogonality.md)。

### M6：四鸟编队扩展（依赖未来任务）

- 用编队包围体替换单鸟包围体；
- 四鸟都在安全框，队形 Roll 连续；
- 不改变 M0–M5 的 Rank、轨迹和渲染正交合同。

### M7：玩家自定义 F4 实飞电影镜头（已完成）

详细设计与验收合同见 [`M11FinaleCameraM7CustomLaunchDirection.md`](M11FinaleCameraM7CustomLaunchDirection.md)。本阶段把现有 Nominal 路线导演扩展到玩家在完整 `Yaw × Pitch × Power` 域内提交的任意 F4 可达路线：

- 发射时冻结本次 Released Trajectory 与其 Hash，镜头不再读取随后可能变化的预览结果；
- 由该 Released 事件流一次性建立两段跨行星镜头日程；标准窗口维持 M3 已验收节奏，短窗口按同一阶段顺序自适应压缩；
- 镜头日程或构图失败只能降级到 authority follow 并记诊断，不得把合格 F4 玩法结果转成失败黑屏；
- 独立录屏新增完整的 `Yaw/Pitch/Power` 三元输入，Manifest 冻结输入、Released Hash、Plan Hash 与导演证据；
- 阶段验收至少随机选取两组彼此不同的 Rank 11 F4 输入，在 `Stylized=1` 下分别完成物理 UFO 接触录屏。Rank 11 仍是 `UNCERTIFIED`，两次录屏只证明导演泛用性。

### PIE 实飞启用与冻结语义

当前功能工作树不修改生产默认绑定；在 PIE 发射前使用控制台显式选择导演层：

```text
abts.M11.CameraDirector.M2.Enabled 0
abts.M11.CameraDirector.M3.Enabled 1
```

`M2` 是仅覆盖 Assist1 的历史 A/B 开关；`M2=1, M3=0` 的预期表现就是第一颗行星使用 M2、后续行星回到旧镜头，并不等价于完整终局导演。`M3` 才覆盖三颗行星、双星桥接和 UFO 收束。两个值会在发射首帧冻结，本次飞行中途修改不生效；需要复测时应先结束本次飞行或重新进入 PIE，在下一次发射前设置。发射日志必须包含 `DirectorFrozen M2=0 M3=1 Mode=MultiAssistM3`；若误用 M2-only，日志会输出 `M2OnlyFirstAssist` 警告。

### 终局接管期间的 FXAA 合同

2026-08-11 可见 PIE A/B 已确认，Soft Sprite 尾迹在运动时变淡并不是运动模糊，也不是粒子出生后缓慢亮起：`r.MotionBlurQuality 0` 不改变现象，而关闭时域抗锯齿后立即恢复正常。M11 运行时生成的 PDI Sprite 没有可供 TSR/TAA 使用的可靠运动矢量与响应式历史标记，快速运动的细小半透明点会被时域历史抑制。因此终局 Flight Camera 的正式接管合同固定为 FXAA（`r.AntiAliasingMethod 1`）。

- 只有 `BeginAuthorityFollow` 成功后才保存当前 AA 方法并切换 FXAA；瞄准和未发射阶段不受影响；
- 接管期间每次权威相机更新都会校验 FXAA，防止后续 Profile/CVar 重写重新启用 TSR/TAA；
- `ResetAuthorityFollow` 与 `EndPlay` 都恢复接管前的 AA 方法，失败、重置和 Actor 销毁均不会把 FXAA 泄漏到其他玩法阶段；
- PIE 与独立离线录屏共用同一 Flight Camera 生命周期，因此不再依赖录制脚本额外设置 AA；
- 该合同只改变终局演出的抗锯齿方式，不改变 Stylized Profile、尾迹采样、镜头构图、Released 轨迹或候选身份。

## 6. 性能预算

- M0 Runner 是开发期验收工具：每帧新增一次 1280×720 SceneCapture、同步读回与 JPG 压缩，明确不纳入生产运行时预算；未显式传入 `-ABTSM11CameraCapture` 时零开销、零对象。
- 后续导演：每帧只做固定数量的向量、投影与平滑计算，Game Thread 目标低于 `0.10 ms`。
- 录制开销来自验收专用 SceneCapture、GPU 读回、JPG 与 AVI 写盘，不计入游戏运行时预算；正式对比必须记录实际分辨率、帧数和耗时。

## 7. 明确非结论

- Rank 11 录屏好看不代表它通过 M11-B v2.2 全操作域认证。
- NullRHI 或自动化绿灯不证明 AVI 像素、构图或三渲二质量。
- 三渲二开启后的颜色、描边、抗锯齿和性能由集成工作树继续演进；M11 不冻结这些像素参数。
- M0/M1 只证明独立录制工作流能完整观察并量化旧镜头，不能证明 Lucy 风格镜头已经实现。
