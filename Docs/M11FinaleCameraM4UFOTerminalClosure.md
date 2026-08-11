# M11 终局镜头 M4：UFO 终端与镜头收束

## 1. 阶段目标

M4 只负责 Assist3 穿出后至播放终点的镜头导演：

- 在土星前景穿越完成前保持已经验收的 Lucy 单行星构图；
- 从土星离场构图连续取得 UFO，并在 `FinalApproach` 全程同时保持主控鸟与 UFO；
- 播放结束后冻结同一终端构图，不在 `TargetHit` 边界重新求解或切镜头；
- 在镜头状态和录屏证据中明确区分“未认证候选的合格截获终点”与“800 cm 物理接触终点”；
- 离线候选录屏在合格截获点后追加确定性的可见终端转移，使鸟沿平滑曲线到达 UFO 的 800 cm 接触球。

本阶段不修改候选求解结果、Released Trajectory Hash、碰撞、UFO 破坏、救援、四鸟/五鸟编队、三渲二或云层。新增曲线只属于未认证候选的离线播放表现计划，不反向改写候选积分点或认证身份。

## 2. 权威与终点语义

原始轨迹位置、速度和 Assist3 事件仍来自冻结的 `FABTSM11TrajectoryResult`。候选录屏先逐点保留这段权威前缀，再由 `FABTSM11PlaybackPlan` 追加显式标记为 `VisibleTerminalTransfer` 的确定性五次曲线；相机只消费最终播放计划，不通过 Chaos 或运行时追踪临时拉鸟。

终点身份分为：

- `CandidateQualified`：候选模式到达搜索合同中的合格截获点；它证明原候选成功，不能单独作为离线录屏的撞击终点。
- `PhysicalContact`：生产播放计划明确记录 `bPhysicalTargetHit`，到达真实几何接触终点。
- `None`：尚无成功终点身份或失败路径；不得伪装成 UFO 命中镜头。

`candidateQualifiedIntercept=true` 与 `physicalTargetHit=true` 可以按时间顺序同时成立：前者记录转移前的候选资格，后者记录表现曲线确实到达解析接触球；它们不是相互排斥的终点枚举。逐帧 `endpointAuthority` 仍是单值，并按最终播放终点取 `PhysicalContact`。候选 Authority 继续是 `UNCERTIFIED`，Released Trajectory Hash 不变；追加曲线只改变 Playback Plan Hash。

### 2.1 候选可见终端转移

离线候选播放从合格点的位置、速度和估算加速度出发，以五次曲线连接 UFO 朝来向一侧的解析接触点：

- 接触点为 `ContactCenter - TravelDirection * 800 cm`，末端速度继续指向 UFO，避免到点前反向或停顿；
- 采样必须保持鸟心至 UFO 中心距离单调减小，最终样本之前不得提前进入 800 cm 球，最终距离误差不超过 `1e-3 cm`；
- 全段继续执行行星 clearance 检查；持续时间搜索限定为 `0.5–8.0 s`，并检查候选表现专用的加速度、jerk 上限；
- 任一条件不满足时 fail closed，不发布“看似撞上”的候选录像。

## 3. 镜头状态链

### 3.1 Assist3 Authority

Assist3 `Approach/Periapsis` 继续使用 M3 Lucy 求解。只有鸟的前景轮廓完成穿出后，才允许终端取得过程开始；门槛复用已经验收的 `ForegroundTransitClearProgress`，不使用 Rank11 固定帧号。

### 3.2 TerminalAcquire

从 Assist3 清轮廓时刻到 Assist3 Exit，以两段零端斜率 `SmootherStep` 从土星 Lucy Transform 经过“鸟＋土星＋UFO”广角桥，再过渡到 UFO 双主体 Transform：

- 起点 Transform/FOV 必须精确等于当帧 Lucy 解；
- 中点把土星—UFO 连线固定为屏幕 Right，并以 85 度三包围球 fit 保证至少一端叙事目标持续在画；
- 终点 Transform/FOV 必须精确等于 `FinalApproach` 首帧所用的中央 UFO 解析解；
- 过渡期间鸟半径不做视觉缩放；
- 土星可以自然后退离场，UFO 逐渐取得，但不得通过第二套运行时 lag 再次改变同一构图。

### 3.3 FinalApproach

使用播放计划的 `Assist3 Exit→PhysicalContact` 作为单一归一化时间轴，并用解析双锚构图替代原来的对称包围球质心：

- UFO 几何接触中心始终锚在 NDC `(0,0)`，不再随鸟/UFO 算术质心漂移；
- 鸟位于同一竖直中心线下方，NDC Y 从 `-0.42` 线性移动到 `-0.22`；线性锚保证等播放时间内具有基本恒定的屏幕逼近速度；
- 相机到鸟的距离独立使用五次 `SmootherStep` 从 `40000 cm` 收到 `5000 cm`，形成持续推进的 dolly 动感，同时保持 55 度固定镜头；
- 每帧由“鸟→UFO”轴、冻结终局 Up 和目标 NDC/距离解析构造正交相机基底；鸟和 UFO 必须位于相机前方并满足 1.20 投影安全边距；
- `TerminalAcquire` 后半段直接插值到该解析解的 progress 0，FinalApproach 首帧复用同一 Transform/FOV，避免重新接管时硬切；
- 不使用终点附近快速变化的瞬时速度切线决定相机朝向，也不缩放鸟体。

该结构把叙事重心改为“中央 UFO＋下方来袭鸟”：UFO 稳定，鸟以可读且均匀的屏幕运动逼近，世界距离缩短与相机主动 dolly 共同放大速度感。

对候选录屏而言，`FinalApproach` 不再在合格截获点结束，而是继续消费 `VisibleTerminalTransfer`。因此 UFO 双主体构图会沿同一冻结基底持续收紧，直到鸟心到达 800 cm 接触球。

### 3.4 Terminal

达到播放终点后，交互状态停止推进，Flight Camera 保留 `FinalApproach` 最后一帧 Transform/FOV。`Terminal` 只改变观测语义和终点身份，不重新运行相机求解。

## 4. 参数与默认值

- 终端取得起点：`ForegroundTransitClearProgress`（当前 0.23）。
- UFO 双主体 FOV：55 度。
- 双主体投影安全边距：1.20。
- 鸟起始/接触 NDC Y：`-0.42 / -0.22`。
- 相机到鸟起始/接触距离：`40000 / 5000 cm`。
- 鸟视觉缩放：恒定 1 倍，由现有“禁止桥接缩放”合同继续保证。

这些参数属于 M11 C++ 默认值，可由命令行录屏直接验证，不要求编辑器手工创建 Blueprint、Niagara、Sequence 或资产绑定。

## 5. 阶段验收里程碑

### M4-A：状态与纯数学

- 清轮廓门前仍为 Assist3 Authority；门后进入 `TerminalAcquire`。
- Acquire 起点精确等于 Lucy，终点精确等于 FinalApproach 中央 UFO 解析解。
- 冻结终端基底有限、正交且符号连续。
- FinalApproach 的 UFO NDC 恒为 `(0,0)`，鸟 X 恒为 0；鸟在 0、0.5、1.0 进度的 Y 位移两半相等。
- 相机到鸟距离从 40000 cm 单调收束至 5000 cm，鸟/UFO 投影球全过程满足安全边距。
- `CandidateQualified` 与 `PhysicalContact` 能在镜头状态中分别表达。
- 候选表现计划保留原始 Released 前缀，追加段全部标记为 `VisibleTerminalTransfer`，且最后一个 Authority 点精确位于 800 cm 接触球。

### M4-B：离线观测

- `FinalApproach` 与 `Terminal` 中鸟、UFO 投影均非零且保持在安全画幅内。
- Assist3 Exit、FinalApproach→Terminal 边界无位置、旋转或 FOV 硬切。
- FinalApproach 不再出现旧版 UFO 全丢失、鸟丢失或空画面。
- CSV 每帧记录 `endpointAuthority`；Manifest 同时记录合格历史、物理接触、转移起止时刻和最终 Authority 距离。
- `m4PhysicalContactPassed` 必须使用 PlaybackPlan Authority 点验证，不使用带动画摆动的 BirdVisual Bounds 中心代替轨迹原点。

### M4-C：fresh 进程录屏

使用 Rank11、Stylized=1、M3 Director=1 的 fresh `UnrealEditor.exe -game -dx11 -RenderOffscreen` 完成自动发射、录屏、终帧停留、AVI/CSV/Manifest 落盘并自行退出。人工重点检查土星离场、UFO 取得、鸟向 UFO 收束和终帧稳定性。

## 6. 失败关闭

缺少 Assist3 Exit、播放终点、UFO 几何、冻结终端基底不可构造、双主体 fit 非有限或终点身份矛盾时，导演样本/录屏必须失败；不得退回旧版切线追踪并生成看似成功的终局录屏。

## 7. 2026-08-11 实现与证据

- Development Editor 使用 UE 5.8、`-ForceUnity -NoHotReloadFromIDE` 完整链接成功，无编译警告。
- fresh NullRHI `ABTS.M11C.Unit.FlightCameraAuthorityFrame` 通过，冻结 Assist3 清轮廓门、Acquire 起止端点及 `CandidateQualified` 身份；首轮日志为 `Saved/Logs/M11-M4-UFOTerminalBridge-Automation.log`。
- R2 `M4UFOTerminalBridgeR2-20260811-124700` 证明首轮镜头结构连续，但也暴露候选录屏停在 41,250 cm 合格包络、没有抵达 800 cm UFO 接触球；该录像降级为问题证据，不再作为 M4 终端接触验收。
- fresh NullRHI `ABTS.M11C.V2_1`、`ABTS.M11C.CameraCapture.Config` 与 `ABTS.M11C.Unit.FlightCameraAuthorityFrame` 分别通过 3/3、1/1、1/1；最终日志为 `Saved/Logs/M11-M4-CandidateContact-V21-Final.log`、`Saved/Logs/M11-M4-CandidateContact-CaptureConfig-Final.log`、`Saved/Logs/M11-M4-CandidateContact-FlightCamera-Final.log`。录制合同为 v12，CSV Schema 保持 7。
- 新增纯数学回归冻结：候选原始前缀逐样本不变、转移段类型明确、构建可复现、Rank11 最终鸟心至 UFO 中心精确为 800 cm。
- fresh Rank11、Stylized=1、M3/M4 Director=1、`r.VolumetricCloud=0` 命令行录屏 `M4PhysicalContactR4-20260811-133500` 自动完成 1021 帧并退出，结果为 `Complete/TargetHit`；AVI SHA-256 为 `98E576BE5555C52859EF0E3E93B855D8CEA7587703F7651D10C0BFF3301190D3`。
- 合格点后的 `VisibleTerminalTransfer` 为 `30.6807317466→33.0834287947 s`，约 2.403 秒；Released Trajectory Hash 保持 `0x505F3312AC8AE07F`，新 Playback Plan Hash 为 `0x0F0E8466ED5BD3AD`，Authority 仍为 `UNCERTIFIED`。
- v12 Manifest 记录 `candidateQualifiedIntercept=true`、`physicalTargetHit=true`、`visibleTerminalTransfer=true`、`cameraEndpointAuthority=PhysicalContact`；最终 Authority 距离 `799.9999999999568 cm`，`m4PhysicalContactPassed=true`、`m4TerminalClosurePassed=true`。
- 逐帧离线门：TerminalAcquire 56 帧、无叙事目标空窗 0；M4 终端 123 帧、Acquire 56 帧，鸟/UFO/终点身份丢失均为 0，终端位置/旋转/FOV 跳变均为 `0/0/0`。抽检 922、948、970、990、995、1020 帧确认鸟带连续拖尾沿弧线靠近 UFO，995 帧进入 `TargetHit/Terminal` 后冻结同一接触构图；没有通过鸟体缩放补偿远景。
- 中央 UFO 解析构图将录制合同升至 v13。UE 5.8 Development Editor `-ForceUnity -NoHotReloadFromIDE` 完整链接成功；fresh NullRHI `ABTS.M11C.Unit.FlightCameraAuthorityFrame`、`ABTS.M11C.CameraCapture.Config`、`ABTS.M11C.V2_1` 分别为 1/1、1/1、3/3，日志为 `Saved/Logs/M11-M4-CenteredDolly-R9-20260811-UBT.log`、`M11-M4-CenteredDolly-R9-FlightCamera-Fresh.log`、`M11-M4-CenteredDolly-R9-CaptureConfig-Fresh.log`、`M11-M4-CenteredDolly-R9-V21-Fresh.log`。
- fresh Rank11、Stylized=1、M3/M4 Director=1、`r.VolumetricCloud=0` 命令行录屏 `M4CenteredDollyR9-20260811-160100` 自动完成 1021 帧并退出，结果为 `Complete/TargetHit`；AVI SHA-256 为 `D74E62DD9AD5745CDBAAEE429291C72F7E37BD4DF9A35A912F6177DCB8D33CDA`。v13 Manifest 继续通过 `m4PhysicalContactPassed` 与 `m4TerminalClosurePassed`，最终 Authority 距离为 `799.9999999999568 cm`，Released/Playback Plan Hash 分别保持 `0x505F3312AC8AE07F` / `0x0F0E8466ED5BD3AD`。
- TerminalTrack 为 898–1020 共 123 帧：UFO 中心最大误差 `0.000011 px`；鸟视觉 Bounds 相对中央竖线最大摆动 `1.98 px`，从 Y `510.52 px` 稳定移至接触停留段约 `435–436 px`；相机到鸟距离从 `40014.64 cm` 收至 `5009.14 cm`。TerminalTrack 内 M4 位置/旋转/FOV 跳变仍为 `0/0/0`；抽检 898、922、948、970、995 帧确认 UFO 稳居中央、鸟从下方逼近且镜头持续推近。
