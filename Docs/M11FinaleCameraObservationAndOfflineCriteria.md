# M11 终局镜头导演观测与离线判据（M1）

> 编码：UTF-8，简体中文。
>
> 状态：已实现并完成旧镜头四象限基线。
> 所有权：`feature/m11-finale`。本阶段只观测既有镜头，不修改相机、轨迹、候选、三渲二参数或共享地图。

## 1. 目标与非目标

M1 把 M0 的“可以稳定录完整段视频”推进为“可以逐帧解释旧镜头为什么失败”。每一个落盘 JPG 都有一行同编号观测；离线工具只读 CSV，量化鸟离框、当前叙事目标丢失与镜头跃变，并比较同一 Rank 在 Stylized 0/1 下的导演身份。

M1 不实现镜头导演，不改变 `AABTSM11FinaleFlightCamera` 的位置、旋转、FOV、Lag、ViewTarget 或切换逻辑。观测器也不读像素，因此其“几何可见”结论不等同于颜色、轮廓、雾化或三渲二像素可读性。

## 2. 数据来源与阶段判定

### 2.1 唯一目标身份

当前目标只从冻结布局和权威播放事件解析，禁止按 Actor 名称、离鸟最近物体、屏幕中最大球体或当前 PIP 猜测：

- `Assist1/2/3`：使用布局的 canonical center 与 `VisualRadiusCM`，经当前 Finale frame 变换到世界空间；
- `UFO`：使用终局目标的几何接触中心和半径；
- 主控鸟：读取本次 Attempt Bird 的视觉 Skeletal Mesh 包围球；Actor bounds 只作视觉组件缺失时的保守回退。

### 2.2 诊断阶段

阶段只由 `AssistEnter / ClosestApproach / AssistExit / TargetHit` 的权威时间与当前 playback time 决定：

| 时间区间 | 阶段 | 当前目标 | 切换原因 |
| --- | --- | --- | --- |
| 发射前 | `PreLaunch` | Assist1 | `AwaitingLaunch` |
| 发射至 Assist1 Enter | `CruiseToBody` | Assist1 | `LaunchToAssist1` |
| Assist N Enter 至 Closest | `Approach` | Assist N | `AssistNEnter` |
| Assist N Closest 至 Exit | `Periapsis` | Assist N | `AssistNClosestApproach` |
| Assist N Exit 至下一 Assist Enter | `Handoff` | Assist N+1 | `AssistNExitToAssistN+1` |
| Assist3 Exit 至命中 | `FinalApproach` | UFO | `Assist3Exit` |
| `TargetHit` 后终帧停留 | `Terminal` | UFO | `TargetHit` |

发射后若权威事件不完整，观测器 fail closed；不会回退到按距离选目标。

## 3. 逐帧 CSV 合同

M1 基线录制合同版本为 4。成功录制除 JPG、AVI、Manifest 外，必须写出：

```text
<MovieFolder>/<MovieName>.camera-observations.csv
```

CSV schema 版本为 1，共 36 列：

- 身份与时间：`schemaVersion`、`frameIndex`、`captureSeconds`、`playbackSeconds`、`interactionState`；
- 导演语义：`stage`、`currentTarget`、`stageReason`；
- 鸟：世界坐标、屏幕坐标、相机深度、像素半径、可见比例；
- 目标：世界坐标、屏幕坐标、相机深度、像素半径、可见比例；
- 相机：世界坐标、Pitch/Yaw/Roll、到鸟/目标距离、FOV；
- 连续性：相邻捕获帧的位置、旋转和 FOV 变化量。

CSV 行数必须严格等于 JPG/AVI 帧数，`frameIndex` 必须为 `0..N-1`。写 CSV 失败或行数不一致会使整个录制失败，不允许只留下视频并报告成功。

### 3.1 M2 向后兼容扩展

M2 把录制合同升级到 5、CSV Schema 升级到 2，并在原 36 列基础上增加 `directorMode`、`directorM2FrozenEnabled`、`directorBlendAlpha`。Manifest 同步记录请求模式、实际冻结模式、混合帧数、非 Assist1 越界帧数和最大混合权重。离线工具同时接受 Schema 1/2；旧 M1 四象限 CSV 不需要重录。

### 3.2 v6 集成合同

2026-08-06 合并 `master` 时发现版本号发生并行碰撞：M11 历史 v4/v5 分别表示 M1 观测与 M2 导演扩展，而集成线也曾独立用 v4 表示“录制期间三渲二 Enabled/Profile 必须逐帧保持”的 fail-closed 门。已有 v4/v5 产物的身份不可重写，因此合并结果不复用 v5，统一升级为 v6。

v6 是首个无歧义的完整集成合同：它同时要求 CSV Schema 2/M2 导演遥测，以及 `stylizedRuntimeStateMaintained`、`stylizedRuntimeStateFailureFrame` 两项全程渲染状态证据。逐帧状态检查发生在观测与图像捕获之前；发生漂移时必须在该帧停止录制、写失败 Manifest，不能把半段普通渲染视频标为 `Complete`。后续并行开发修改录制合同前，必须先合并最新 `master` 并从当前最高版本单调递增，不能只基于功能分支旧基线自行占用版本号。

### 3.3 M3 三行星扩展

M3 把录制合同升级到 7、CSV Schema 升级到 3，并增加 `framingTarget`、`stageProgress`、`stageDurationSeconds`、`directorM3FrozenEnabled`。`currentTarget` 表示叙事 CurrentBody；`framingTarget` 表示实际提供行星中心、半径和冻结 encounter basis 的 Assist。两者分离后，离线工具可以验证 CurrentBody 只在 Handoff 切换，同时防止相机在后台偷偷使用另一颗行星而没有留下证据。

Schema 3 的阶段决策指纹同时纳入 CurrentBody、FramingBody、阶段进度和真实阶段时长。Schema 4 再加入 `shotPhase/shotReason/shotProgress/shotDurationSeconds/shotEndSlope`，把独立镜头叙事状态、提前揭示时长与入口速度匹配纳入决策身份。离线工具继续接受 Schema 1/2/3，并为缺失的新列填入明确的旧版默认值；旧 M1/M2/M3 证据不需要重录。M3 报告按 Assist1/2/3 分列鸟/目标丢失、导演帧、提前揭示帧和左→右穿越，并单列 ShotPhase 数量、两次 Handoff 的切换位置、目标空窗与连续性门。详见 [M3 三行星连续导演与 Handoff](M11FinaleCameraM3MultiAssistHandoff.md)。

Schema 5 保持列结构不变，扩展 `shotPhase` 合法值为 `IncomingReveal/IncomingTrack/IncomingEntryMatch`，并把 Assist1 纳入同一 Incoming 状态合同。离线工具新增 `m3FirstBodyAcquisitionPassed`：按 playback seconds 要求发射后 0.10 秒内启动导演混合、0.75 秒内首个目标首次可见、1.00 秒内完整入画，并保证首段鸟不丢失。Schema 1–4 继续兼容读取，不追溯应用该新门。

录制合同 v10、CSV Schema 6 新增 `DualBodyBridge` 以及上一/下一桥接行星各自的标签、屏幕坐标、像素半径和可见比例。`m3DualBodyBridgePassed` 要求桥接状态实际出现、每个桥接帧两颗行星均可见、跨行星叙事窗口不存在两颗行星同时不可见的帧、桥接全过程鸟不离框，且桥接远景中的鸟半径至少 2 px。该门只证明三主体远景桥和无空窗，不豁免 `m3HandoffPassed` 原有的相机位置、旋转与 FOV 连续性要求。

M4 首轮将录制合同升至 v11、CSV Schema 升至 7。逐帧新增 `endpointAuthority=None|CandidateQualified|PhysicalContact`；`TerminalAcquire` 复用桥接列记录 Assist3 与 UFO 的取得过程。Manifest 新增 Acquire 帧/无叙事目标帧、`m4TerminalFrameCount`、鸟/UFO 丢失帧、终点身份缺失帧、M4 位姿/FOV 跳变帧和 `m4TerminalClosurePassed`。该门要求 Acquire 期间土星/UFO 至少一个持续可见，FinalApproach/Terminal 实际出现，鸟中心全程在画且投影半径至少 1 px、UFO 全程可读，终点身份明确且不存在既定单帧硬切。远景鸟依靠已验收的连续拖尾增强可读性，不恢复镜头阶段鸟体缩放。

候选离线录屏加入可见终端转移后，录制合同升至 v12，CSV Schema 仍为 7。Manifest 新增 `visibleTerminalTransfer`、`terminalTransferStartSeconds`、`terminalTransferEndSeconds`、`m4FinalBirdToUFODistanceCM`、`m4PhysicalContactRadiusCM` 与 `m4PhysicalContactPassed`。候选的 `candidateQualifiedIntercept` 是原始轨迹资格历史，允许与转移后的 `physicalTargetHit` 同时为真；逐帧单值 `endpointAuthority` 则以最终终点为准取 `PhysicalContact`。`m4TerminalClosurePassed` 额外要求最终 PlaybackPlan Authority 点到 UFO 几何接触中心的距离命中 800 cm 接触半径。该距离必须从播放计划 Authority 点计算，不能用带动画摆动的 BirdVisual Bounds 中心代替。Released Trajectory Hash 必须保持不变，Playback Plan Hash 应因显式 `VisibleTerminalTransfer` 改变，候选 Authority 仍为 `UNCERTIFIED`。

中央 UFO 终端 dolly 将录制合同升至 v13，CSV Schema 仍为 7。相机从 PlaybackPlan 的 Assist3 Exit 和最终播放时刻补齐 FinalApproach 的真实 `stageProgress/stageDurationSeconds`，而不是沿用事件解析器原先恒为 0 的占位进度。纯数学门要求 UFO NDC 全程为 `(0,0)`、鸟 X 为 0、鸟 Y 按进度线性从 `-0.42` 到 `-0.22`，并验证 0→0.5 与 0.5→1 的屏幕位移相等；相机到鸟距离则独立从 40000 cm 平滑降至 5000 cm。像素录屏仍须复核动画 Bounds 摆动、旋转连续性和接触帧可读性，不能只以解析 NDC 绿灯代替画面验收。

v13 fresh Rank11 Stylized1 `M4CenteredDollyR9-20260811-160100` 的 TerminalTrack 共 123 帧：UFO 几何中心最大像素误差 `0.000011 px`，鸟视觉 Bounds 相对中央竖线最大偏差 `1.98 px`；相机到鸟距离由 `40014.64 cm` 收至 `5009.14 cm`，M4 位置/旋转/FOV 跳变为 `0/0/0`，最终 Authority 距离 `799.9999999999568 cm`。这组数据作为解析门映射到实际 SceneCapture 的首个基线；鸟 Y 的亚像素级非恒速变化来自动画 Bounds 中心，不改变 actor-origin 的线性 NDC 合同。

M5 将录制合同升至 v14，CSV Schema 仍为 7。`-ABTSM11CaptureTelemetryOnly=1` 保留完整 fresh `-game` 发射、PlaybackPlan、固定 30 Hz 相机观测和自动退出，只跳过 RenderTarget、SceneCapture、JPG 与 AVI。Manifest 新增 `telemetryOnly`、`visualRecordingProduced`、`m5EvidenceMode`、`m5VisualAcceptanceEligible`、`m5StageSequenceHash`、`m5CameraNumericsHash`、`m5HashedFlightFrameCount`、摘要量化合同及有效性标志。摘要只从首个 `Launched` 样本起按飞行相对帧号计算；SceneCapture 负载造成的发射前等待帧差异保留在原始 CSV/总帧数中，但不得污染导演正交 Hash。Stylized 0 只允许作为数值正交证据；只有 Stylized 1 的正式 SceneCapture 录屏可承担视觉验收。同 Rank 的 Stylized 0/1 必须在同为 TelemetryOnly 的协议内比较；不能把 SceneCapture 与 NullRHI 的相机 Hash 直接比较，因为前者会改变 SkeletalMesh Bounds 更新时机。认证回放还必须把按呈现秒配置的镜头时长乘以有效 PlaybackTimeScale 后再交给以轨迹秒工作的 Stage Resolver，几何 Progress 门不缩放；生产相机与观测器使用同一换算结果。

M2 A/B 使用：

```powershell
python Tools/M11Camera/analyze_camera_observations.py `
  <legacy.camera-observations.csv> `
  <directed.camera-observations.csv> `
  --comparison-mode director-ab `
  --require-pass `
  --output <director-ab.camera-report.json>
```

该门要求两条阶段决策指纹相等、旧镜头混合帧为 0、导演镜头只在 Assist1 Cruise/Approach/Periapsis 混合、鸟不丢失、当前目标丢失显著改善且相机无跳变。报告额外记录 Cruise 混合帧、Cruise 目标丢失帧、首次 Cruise 混合帧和首次 Cruise 目标可见帧，用于防止“Approach 出现太晚且首次出现已经过大”的假通过。

### 3.4 投影与可见比例

观测器用录制帧相同的最终相机 View、水平 FOV 和分辨率做解析投影。视觉包围球投影为屏幕圆，其可见比例定义为“圆的轴对齐包围方形与画幅相交面积 / 包围方形面积”，并限制在 `[0,1]`。深度不为正时像素半径和可见比例为 0。

这个比例是稳定、便宜、与渲染风格无关的构图代理，不是遮挡查询，也不证明目标实际有颜色或轮廓。尤其在近掠时，大球体可能超框但仍保留边缘；M2 起可在同一 CSV 上增加更严格的地平线/遮挡判据，而不修改 M1 原始数据。

## 4. 离线判据

工具入口：

```powershell
python Tools/M11Camera/analyze_camera_observations.py `
  <run-a.camera-observations.csv> `
  <run-b.camera-observations.csv> `
  --output <comparison.camera-report.json>
```

默认只要输入结构有效就退出 0，因为“旧镜头未通过构图判据”正是 M1 所需证据；以后需要作为导演质量门时显式加入 `--require-pass`，不通过返回 2。输入/Schema/数值非法返回 1。

默认逐帧门限只作用于 `Launched` 和 `TargetHit`：

| 判据 | 默认阈值 | 失败语义 |
| --- | --- | --- |
| 鸟可见 | `birdVisibleRatio >= 0.5` | 鸟离开安全构图代理 |
| 当前目标可见 | `targetVisibleRatio > 0.01` 且 `targetPixelRadius >= 4` | 当前叙事目标离框、在相机后或小到不可读 |
| 相机位置连续 | 单帧变化 `<= 5000 cm` | 位置跃变 |
| 相机旋转连续 | 单帧变化 `<= 15°` | 朝向跃变 |
| FOV 连续 | 单帧变化 `<= 2°` | 焦段跃变 |

M3 的 Approach 方向门不能只检查单帧大跳。对每颗 Assist 的可观测 Approach 相对 X，工具同时记录 `maximumApproachBackwardJumpPixels` 和 `maximumApproachBackwardExcursionPixels`；后者维护历史最大相对 X，并计算后续帧相对该最大值的累计回撤。任一累计回撤超过 20 px 时，`m3NoApproachReversal=false`。这可识别“每帧只退几像素、持续十余帧”的慢回摆。

报告同时给出失败帧数、最长连续失败、最大跃变量，以及每个连续 Stage/Target 窗口的帧范围和极值。阈值是 M1 的离线诊断基线，不是最终 M2–M5 镜头质量目标。

## 5. 渲染正交身份

fresh 进程可能因渲染预热在发射前多等待一帧；呈现 Actor 在同一 playback time 的小量浮点位置也可能受帧调度影响。把绝对 `frameIndex` 或全部原始浮点数混入“阶段决策 Hash”会把调度噪声误报成导演分支变化。因此报告明确分为三类指纹：

1. `decisionFingerprintSha256`：只对有效飞行帧的 playback time、Interaction State、Stage、CurrentTarget 和 StageReason 哈希；
2. `criteriaFingerprintSha256`：对同一飞行序列逐帧的鸟丢失、目标丢失、位置/旋转/FOV 跃变布尔判据哈希；
3. `observationFingerprintSha256`：对飞行段原始世界位置和相机数值哈希，只用于追踪呈现浮点差异，不作为 M1 正交放行门。

同一 Rank 的 Stylized 0/1 必须同时满足前两类指纹相等，才有 `allIdentityFingerprintsEqual=true`。发射前等待帧不同不会改变结论；阶段或任一阈值分类不同则必须失败。M5 在导演实现稳定后，把同协议 TelemetryOnly 对收紧到轨迹、事件、阶段与相机数值 Hash 全等；视觉 SceneCapture 与 NullRHI Telemetry 不跨协议强求 Camera Hash 相等。

## 6. 2026-08-05 旧镜头四象限证据

四次均为 fresh `UnrealEditor.exe -game -dx11 -RenderOffscreen`、1280×720、30 fps、旧 `AABTSM11FinaleFlightCamera`、合同版本 4；均到达 `TargetHit`，CSV 与影像帧数一致：

| Rank / 渲染 | 总帧 | 飞行帧 | 鸟丢失 | 当前目标丢失 | 空构图 | 位置/旋转/FOV 跃变 | 阶段窗口 |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| Rank11 / Stylized 0 | 949 | 946 | 0 | 946 | 0 | 0 / 0 / 0 | 11 |
| Rank11 / Stylized 1 | 949 | 946 | 0 | 946 | 0 | 0 / 0 / 0 | 11 |
| Rank0 / Stylized 0 | 1146 | 1140 | 0 | 391 | 0 | 0 / 0 / 0 | 11 |
| Rank0 / Stylized 1 | 1147 | 1140 | 0 | 391 | 0 | 0 / 0 / 0 | 11 |

结论：

- Rank11 两种渲染的导演决策、阈值判据和原始观测指纹均相等；
- Rank0 两种渲染的导演决策与阈值判据指纹相等，`allIdentityFingerprintsEqual=true`；Stylized 1 只在发射前多 1 帧，原始呈现浮点指纹不同，不影响阶段和判据身份；
- 旧镜头没有单帧位置、旋转或 FOV 跃变，主控鸟在本解析包围体判据下也没有丢失；主要失败是“当前权威叙事目标”长期离框。Rank11 全部 946 个有效飞行帧均失败，Rank0 为 391/1140；
- `target lost` 指当前应该参与构图的 Assist/UFO 不可见，不等价于画面中完全没有任何行星；这解释了肉眼仍可能看到另一颗球体轮廓而判据继续失败的情况；
- Rank11 仍为 `UNCERTIFIED`。本次只是 Nominal 路线的镜头诊断，不能替代全操作域认证，也不评价大气散射或三渲二颜色质量。

报告产物：

- `Saved/VideoCaptures/M11CameraCapture/M1-Rank11-Orthogonality-20260805-234749.json`
- `Saved/VideoCaptures/M11CameraCapture/M1-Rank0-Orthogonality-20260805-234749.json`

## 7. M1 完成门

- [x] 每个影像帧都有同编号、有限数值的观测行；
- [x] 当前目标和阶段只来自权威布局/事件，缺失时 fail closed；
- [x] 离线工具能量化旧镜头失败，并允许旧基线以诊断结果正常出报告；
- [x] Rank0、Rank11 × Stylized 0、1 完成 fresh 四象限录制；
- [x] 同一 Rank 的阶段决策与阈值判据身份跨渲染一致；
- [x] 未修改旧相机、候选、权威轨迹、三渲二参数、地图或资产。

M2 已在 Assist1 落地双目标 Cruise Lead-in / Approach / Periapsis 镜头，并通过复用本 CSV 与离线工具完成 A/B；结果见 [M11 终局镜头 M2 设计](M11FinaleCameraM2SingleAssistDirection.md)。
