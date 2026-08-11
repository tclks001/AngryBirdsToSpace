# M11 终局镜头 M5：候选与渲染正交回归

## 1. 阶段目标

M5 证明 M1–M4 的终局镜头导演只消费 Rank 对应的冻结轨迹、事件、播放计划和主体几何，不消费 `abts.Rendering.Stylized.Enabled`。本阶段采用两层证据：

- `Stylized=1`：生成正式 AVI/JPG/CSV/Manifest，承担镜头可读性和三渲二下的主观验收；
- `Stylized=0/1`：比较同一 Rank 的轨迹、事件/阶段和相机数值 Hash，承担渲染开关正交性验收。

Stylized 0 当前受 UE 原生大气散射影响，终局 SceneCapture 基本为浅蓝画面，连主体轮廓也不可读。因此本阶段不重复生成 Stylized 0 正式视觉录像；该限制属于集成工作树的渲染问题，不作为 M11 导演失败，也不得用不可读 AVI 冒充负面镜头证据。

本阶段不修改三渲二、天空、大气散射、材质、地图、Rank 候选、认证身份、Released 轨迹、播放计划或导演参数值。认证 Rank0 的轨迹按 `PlaybackTimeScale=18` 加速播放，因此本阶段补齐一个通用时钟合同：导演配置中的秒数是“呈现秒”，进入以轨迹秒工作的状态机前按有效回放倍率换算；几何 Progress 门不缩放。候选回放倍率为 1，既有 Rank11 导演结果不变。

## 2. 验收矩阵

| Rank | Stylized 1 正式视觉录屏 | Stylized 0 Telemetry | Stylized 1 Telemetry | 正交比较 |
| --- | --- | --- | --- | --- |
| 0 | 必须 | 必须 | 必须 | 两条同协议 Telemetry 的四项身份/Hash 一致 |
| 11 | 必须 | 必须 | 必须 | 两条同协议 Telemetry 的四项身份/Hash 一致 |

Rank 0 是已认证生产基线；Rank 11 仍为 `UNCERTIFIED`，只代表当前主观效果较好的冻结候选。M5 不能改变或提升任一候选的认证身份。Rank 1–10 只在基础矩阵通过后按需扩展，不阻塞本阶段首轮验收。

## 3. TelemetryOnly 工作流

录制合同 v14 新增：

```text
-ABTSM11CaptureTelemetryOnly=1
```

该模式仍在 fresh `-game` 进程中执行完整流程：

```text
启动进程
  → 设置 Rank / Stylized / M3 Director
  → 等待终局系统与弹弓就绪
  → 发射该 Rank 的名义路线
  → 按固定 30 Hz 消费真实 PlaybackPlan
  → 逐帧读取生产 PlayerCameraManager
  → TargetHit 后保留终帧
  → 写 CSV 与 Manifest
  → 自动退出
```

与正式视觉模式的唯一区别是：TelemetryOnly 不创建 RenderTarget、不注册 SceneCapture 视图、不执行 GPU readback、不写 JPG、不封装 AVI。它仍每 Tick 验证 Stylized 开关及 `FinaleSpace` Profile 没有漂移，并使用 1280×720 作为投影观测的规范画幅。

Manifest 明确记录：

- `telemetryOnly=true`；
- `visualRecordingProduced=false`；
- `captureProtocol=RendererIndependentTelemetry`；
- `cameraObservationAssessment=NumericalOrthogonalityEvidence`；
- `videoPath/frameWildcard` 为空，`videoBytesObserved=0`；
- `m5VisualAcceptanceEligible=false`，禁止把数值探针解释为视觉验收。

## 4. 数值 Hash 合同

### 4.1 身份 Hash

同一 Rank 的 Stylized 0/1 必须满足：

- `releasedTrajectoryHash` 完全一致；
- `playbackPlanHash` 完全一致；
- Rank、Authority、`candidateSourceHash/candidateResultHash` 或 `presetHash/certifiedBundleHash` 一致；
- `interactionState=TargetHit`，M4 物理接触和终端闭合门一致。

这些字段证明渲染开关没有改变候选、轨迹或播放计划。

### 4.2 阶段序列 Hash

`m5StageSequenceHash` 从首个 `Launched` 样本起，对飞行相对帧序号及以下 renderer-independent 字段做稳定 FNV-1a 摘要：

- FlightFrameIndex、PlaybackSeconds；
- InteractionState、Stage、CurrentTarget、FramingTarget；
- StageReason、EndpointAuthority、StageProgress/Duration；
- ShotPhase、ShotReason、ShotProgress/Duration/EndSlope；
- DirectorMode、M2/M3 冻结开关、DirectorBlendAlpha。

时间和进度量化到 `1e-9`。Stylized 标志本身明确不进入 Hash。发射前 `Ready/Aiming/ReleasePending` 样本不进入摘要，因为 SceneCapture GPU 读回会改变这些等待帧数量；Manifest 另存总观测帧数和 `m5HashedFlightFrameCount`，不得把启动吞吐差异误判成导演差异。

### 4.3 相机数值 Hash

`m5CameraNumericsHash` 对每个飞行样本的 FlightFrameIndex、PlaybackSeconds、Camera Location、Rotation 和 FOV 做独立摘要：

- Camera Location：`1e-3 cm`；
- Rotation/FOV：`1e-6 degree`；
- PlaybackSeconds：`1e-9 s`。

相机 Hash 不包含像素、曝光、后处理、轮廓、材质和视觉 Bounds。鸟/UFO 轨迹身份由 Released/Playback Plan Hash 负责，避免骨骼动画 Bounds 抖动污染导演正交合同。

任一摘要为空、帧数不同或同 Rank 的 Stylized 0/1 Hash 不同，M5 必须 fail closed，并保留两份 Manifest/CSV 定位第一处分歧；不得用“画面看起来一样”覆盖数值失败。

### 4.4 同协议比较边界

正交放行必须比较 `TelemetryOnly=1` 的 Stylized 0 与 Stylized 1，不得把 Telemetry 与正式 SceneCapture 录像的相机 Hash 直接比较。SceneCapture 会驱动 SkeletalMesh 的可见性/Bounds 更新，生产相机又会读取鸟视觉包围半径用于三主体 fit；因此“是否执行捕获”是观测协议差异，可能造成亚厘米级相机位置差，但不是 Stylized 开关分支。

正式视觉录屏承担像素与构图验收；同协议 Telemetry 对承担渲染开关的数值正交验收。两层证据共享 Released/Playback Plan 与阶段摘要，但不要求跨协议 Camera Hash 相等。

## 5. 正式视觉录屏

只有 `Stylized=1 && TelemetryOnly=0` 的 fresh 录屏可作为 M5 视觉证据。Rank 0、Rank 11 均须：

- 自动发射、完整经过三颗行星、执行 M4 UFO 终端接触并自动退出；
- Manifest 为 `Complete/TargetHit`；
- M3 鸟持续可读，双星桥接和入口连续；
- M4 鸟/UFO/Endpoint 不丢失，位置/旋转/FOV 跳变为 0；
- `m4PhysicalContactPassed=true`、`m4TerminalClosurePassed=true`；
- 人工抽查 Assist1/2/3 亮面、两次桥接和 UFO 接触段。

三渲二颜色、轮廓质量和像素性能仍由集成工作树负责。M11 只确认当前 Stylized 1 输出足以读取镜头叙事，不冻结渲染实现版本或色彩结果。

## 6. 自动化与失败关闭

`ABTS.M11C.CameraCapture.Config` 冻结以下合同：

- v14 命令行正确解析 TelemetryOnly；非 0/1 值拒绝；
- 视觉模式默认不进入 TelemetryOnly；
- 数值模式保留 Rank、Stylized 和 M3 Director；
- 同一输入的两类摘要可复现；
- 改动相机数值只能改变相机 Hash；改动阶段只能改变阶段 Hash。

TelemetryOnly 仍要求绝对 `MovieFolder`、唯一 `MovieName`、固定帧率、自动退出和完整 CSV/Manifest。依赖、Stylized 状态、阶段时间轴、相机观测、终点或输出任一失败时进程以非零状态退出，不生成 `Complete` Manifest。

## 7. 呈现时钟合同

`FABTSM11FinaleCameraShotSettings` 的时间字段按呈现秒配置，`ForegroundTransitClearProgress` 按物理 Periapsis 归一化进度配置。运行时与录屏观测器均通过同一个换算入口生成播放时钟设置：

- 候选模式有效倍率为 1，所有数值保持原样；
- 认证模式有效倍率为 `PlaybackTimeScale`，所有秒数字段同比放大后再与轨迹事件时间比较；
- 几何清轮廓 Progress 不变；
- 非有限或非正倍率 fail closed；
- 生产相机与 CSV 观测必须使用同一份换算后设置。

这避免认证长轨迹在 18 倍回放下把 2 秒拉远、0.6 秒桥接和 0.5 秒入口压成 1–3 个输出帧，也避免在长 Handoff 中直到下一星 Enter 前才启动导演链。

## 8. 2026-08-11 正式证据

### 8.1 数值正交

四条 Telemetry 均为 fresh UE 5.8 `-game -NullRHI`、1280×720 投影画幅、30 Hz、`Complete/TargetHit`、无 JPG/AVI：

| Rank | Stylized | 总观测/飞行摘要帧 | Released / Plan | Stage / Camera | 结果 |
| --- | ---: | ---: | --- | --- | --- |
| 0 | 0 | 1181 / 1140 | `0x185D3B673C1D52AF` / `0xFCE93D313629C3D0` | `0x9BF92906F7FFAE4C` / `0xF739051F622E6387` | Complete |
| 0 | 1 | 1181 / 1140 | 同上 | 同上 | Complete |
| 11 | 0 | 1022 / 1018 | `0x505F3312AC8AE07F` / `0x0F0E8466ED5BD3AD` | `0xB9DA2451B3909DB5` / `0x1C4A6656BFB270CA` | Complete |
| 11 | 1 | 1022 / 1018 | 同上 | 同上 | Complete |

产物目录：

- `Saved/M11CameraCaptures/M5Orthogonality-Rank0-Stylized0-TelemetryR3-20260811-165100`
- `Saved/M11CameraCaptures/M5Orthogonality-Rank0-Stylized1-TelemetryR3-20260811-165300`
- `Saved/M11CameraCaptures/M5Orthogonality-Rank11-Stylized0-TelemetryR3-20260811-165400`
- `Saved/M11CameraCaptures/M5Orthogonality-Rank11-Stylized1-TelemetryR3-20260811-165500`

### 8.2 Stylized 1 正式视觉

| Rank | 录屏 | 飞行帧 | 鸟丢失/空构图 | M4 鸟/UFO 丢失 | 接触/闭合 | AVI SHA-256 |
| --- | --- | ---: | --- | --- | --- | --- |
| 0 | `M5Visual-Rank0-Stylized1-v14R3-20260811-164900` | 1140 | `0 / 0` | `0 / 0` | `true / true` | `615BD58704E02EF7FFA49AD1D767F964C97E3FEA57E59A54368134073C05F7A5` |
| 11 | `M5Visual-Rank11-Stylized1-v14R3-20260811-165600` | 1018 | `0 / 0` | `0 / 0` | `true / true` | `7297FB57FBA365C7FE068667A8C2BBAFE253332AC967562618C981276985C0B8` |

Rank0 首轮未换算呈现时钟的诊断录像曾在两个 Handoff 各连续丢鸟 10 帧，最大单帧旋转 `144.48°`、FOV 步长 `17.90°`。换算后正式 R3 鸟丢失归零，最大旋转步长降到 `10°`、FOV 步长降到 `3.48°`，并保持最终 800 cm 接触。该首轮产物只作根因证据，不作为 M5 正式基线。

## 9. Editor 操作

无。M5 不创建 Blueprint、Sequence、Niagara、材质或地图资产；全部实现、运行和验收均通过 C++、命令行 fresh 进程和离线比较完成。
