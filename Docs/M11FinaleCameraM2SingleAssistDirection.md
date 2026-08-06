# M11 终局镜头 M2：Assist1 Cruise Lead-in / Approach / Periapsis

> 编码：UTF-8，简体中文。
>
> 状态：M2.3 Lucy 式速度节奏与近星推拉已实现并通过离屏视觉与离线判据复核。
>
> 所有权：`feature/m11-finale`。本阶段不修改求解器、候选、PlaybackPlan、共享地图、三渲二或二进制资产。

## 1. 目标与边界

M2 只对 Assist1 启用双目标镜头：相机同时读取主控鸟和 Assist1 视觉球体。为避免玩家在长 Cruise 中看不到飞行目标，导演在 `CruiseToBody` 进度 15% 时开始预进入、50% 时完成双目标构图；真正进入 `Approach` 后保持满权重，Periapsis 保留行星边缘与鸟并逐渐拉远。Assist2、Assist3、UFO 和所有非 M2 窗口继续使用旧 `AABTSM11FinaleFlightCamera` 目标变换，便于同一次录制工具做严格 A/B。

M2 不改变鸟的权威位置、速度、事件、Result Hash、PlaybackPlan Hash 或候选认证身份。Rank11 仍为 `UNCERTIFIED`。

## 2. 数据流与所有权

```text
ReleasedPlaybackPlan / authority events       不变
                 |
                 v
InteractionSystem
  - 采样鸟的权威位置/速度
  - 用共享阶段解析器得到 Stage/Assist/Progress
  - 从冻结 Preset + Finale Frame 得到 Assist1 球心/半径
                 |
                 v
FinaleFlightCamera
  - 先计算旧镜头 Baseline Transform
  - 仅在 M2 Assist1 窗口计算双目标 Transform
  - 按平滑权重与 Baseline 混合
                 |
                 v
Player ViewTarget / M11 Capture Runner
  - 只读观测，不反向驱动相机
```

阶段解析器是 M1 观测和 M2 导演的共同纯数据入口；禁止出现“录制器认为是 Approach、相机认为不是”的第二套判定。

## 3. M2.1 单行星构图模型（历史基线）

先用既有平行运输相机得到 `BaselineTransform`。M2 在其基础上工作：

1. 保留旧镜头的沿轨 Baseline Transform、transported Up 和原始 50° FOV；
2. InteractionSystem 从同一权威 PlaybackPlan 的 Assist1 Enter、Closest、Exit 事件冻结一套 encounter basis：Closest 径向的反方向为画面上方，Closest 速度在径向平面上的投影为时间正向的画面右方；事件缺失、退化或非有限时 fail closed；
3. `CruiseToBody` 进度 15% 后用五次 `SmootherStep` 提升导演权重，进度 50% 时达到 1；最后 25% Cruise 才逐步转入冻结构图，Approach 入口不再重新混合或翻面；
4. 相机主体仍以“行星中心 → 鸟”的外法线为基础，只向冻结 encounter side 偏转最多 8°，保留深度视差同时避免 30° 侧视把两主体送出纵向视锥；
5. 拉近/拉远不再在 Closest 处切换两个阶段曲线。Approach 65% 前不额外后拉，后 35% 用五次曲线抵达最大后拉量的 10%；Periapsis 从同一值、零速度和零加速度继续，在前 80% 释放剩余后拉，最大增加 4500 cm；
6. 位置平滑后，在实际相机位置重新用鸟/行星方向和角半径求双目标外边界中点；视轴 Up 使用冻结 encounter up，使鸟在接近段位于行星左侧、飞掠后自然移到右侧，而不是相机随鸟绕行导致画面方向反复；
7. 与旧追尾镜头过渡时，相机偏移方向围绕鸟做球面插值、距离单独插值，避免直线弦穿过鸟和近裁面；Periapsis 后段仍在 Assist1 Exit 前退回旧镜头，非 Assist1 窗口严格保持旧分支。

阶段权重：

```text
CruiseBlend     = SmootherStep((CruiseProgress - 0.15) / 0.35)
ApproachBlend   = 1
PeriapsisBlend  = SmootherStep((1 - PeriapsisProgress) / ExitBlendFraction)
ApproachRetreat = 0.10 * MaxRetreat * SmootherStep((ApproachProgress - 0.65) / 0.35)
PeriapsisRetreat= MaxRetreat * (0.10 + 0.90 * SmootherStep(PeriapsisProgress / 0.80))
FlybyAngle      = 0 .. 8 degrees, frozen encounter side
CameraOffsetDir = RotateToward(RadialOut, EncounterSide, FlybyAngle)
CameraDistance  = Lerp(BaselineDistance, DirectedDistance, StageBlend)
ViewRotation    = ReframeFromSmoothedLocation(Bird, Assist1)
```

Approach 终点与 Periapsis 起点的后拉量、一阶导数和二阶导数完全相等，避免“到最近点先刹停、下一帧突然后拉”。M2.1 保持原 FOV 和 `M2FollowLagSpeed=30`；实测把响应降到 14 会在 Rank11 高速段积累位置滞后，反而造成双主体纵向分离，故不把相机迟滞当作电影感来源。`fovDeltaDegrees` 继续为 0，后续只有在几何位置达到边界且 M1 指标证明必要时才引入受速率限制的 FOV 辅助。

M2.1 只保证相对 X 左→右。复核实际画面后发现鸟心始终在行星盘外，最小边缘间距仍为 63.44 px；因此本节保留为连续后拉和冻结画面基的历史基线，不再作为当前 Lucy 式构图结论。

### 3.1 M2.2 行星锚定的前景穿越（遮挡关系基线）

M2.2 保留 M2.1 的 Cruise Lead-in、C2 后拉包络、50° FOV 和旧镜头退出合同，但替换单行星的空间构造：

1. 最近点径向只承担相机深度方向，`EncounterScreenRight` 仍取权威最近点速度的时间正向切线，`EncounterScreenUp` 改为轨道/最近点平面法线；
2. 行星中心固定为视轴锚点。导演在行星投影平面指定一条略低于中心的弦：Cruise/Approach 入口为左侧 `-1.60 R`，Approach 用五次曲线抵达中心，Periapsis 前 55% 用五次曲线抵达右侧 `+1.60 R`，垂直偏移保持 `-0.28 R`；
3. 相机位于“投影弦点 → 鸟”的延长线上，最小相机到鸟距离 4000 cm，并叠加原 C2 后拉；因而深度次序固定为 `Camera → Bird → Planet`，鸟不是盘后穿越或二维贴花；
4. 目标弦点不是在冻结世界平面里计算一次，而是在最终行星中心视线的 Right/Up 平面内迭代四次。这样相机转向不会把 `1.60 R` 压缩成接近盘心的假位置；
5. Cruise 的画面轴建立与导演混合使用同一个权重，避免先按旧 Up 拉近、后在 Approach 入口突然翻向冻结轴；位置平滑后仍重新瞄准行星中心；
6. `Approach` 末帧与 `Periapsis` 首帧在穿越 X、相机位置、旋转和后拉量上连续；目标曲线与后拉曲线均使用五次 SmootherStep，阶段边界一、二阶导数为零。

```text
TransitX(Cruise)    = -1.60 R
TransitX(Approach)  = Lerp(-1.60 R, 0, SmootherStep(ApproachProgress))
TransitX(Periapsis) = Lerp(0, +1.60 R,
                             SmootherStep(PeriapsisProgress / 0.55))
TransitY             = -0.28 R
TargetPlanePoint     = Planet + CameraRight * TransitX + CameraUp * TransitY
CameraLocation       = Bird + Normalize(Bird - TargetPlanePoint)
                              * (max(BaselineBirdDistance, 4000 cm) + Retreat)
ViewRotation         = LookAt(Planet, FrozenEncounterUp)
```

这里的 `TransitX/Y` 是导演目标值；离线验收读取最终平滑相机的真实投影，不用目标值替代实拍结果。Lucy 式通过条件也不再只是 X 符号，而是同时要求左→右、鸟心进入盘面、鸟/行星轮廓连续相交，以及所有可观测穿越帧中鸟的相机深度小于行星深度。

### 3.2 M2.3 Lucy 式速度节奏与近星推拉

对 `lucy_ega1_pov-approach_1080p60.mp4` 与 `lucy_ega1_pov-close_1080p60.mp4` 抽样量化后，参考亮点在远段的横向速度约为 `40 px/s`，进入盘面和中心附近后提高到约 `100–105 px/s`，离开时再逐步降到 `10–30 px/s`。参考行星半径从远段约画面高度 `6%` 加速增大，近星段峰值约 `70%`，随后快速缩小。项目不照搬其极端满屏尺度，而是保留同样的“远慢、近快、离开减速”和“近星峰值、离开回拉”关系。

M2.2 的穿越目标在 Cruise 固定为 `-1.60 R`，Approach 又使用两端零速度的 SmootherStep，因此实际画面会迅速贴到左缘、停驻，再缓慢横穿；固定 50° FOV 只让行星半径从约 101 px 增至 137 px，也不足以放大近星速度。M2.3 改为：

```text
Cruise:
  TransitX = Lerp(-3.00 R, -1.35 R,
                  t * (0.8 + 0.2 t))
  t = Saturate((CruiseProgress - 0.15) / 0.85)

Approach:
  TransitX = Lerp(-1.35 R, +0.55 R,
                  p * (0.6 + 0.4 p))
  FOV      = Lerp(50°, 30°, SmootherStep(p))

Periapsis:
  TransitX = Lerp(+0.55 R, +2.50 R,
                  1 - (1 - Saturate(p / 1.00))²)
  FOV      = Lerp(30°, 50°, SmootherStep(p / 0.80))
```

- Cruise 运动从 Lead-in 开始即与导演接管同步，不等到满权重后再把鸟送到远端，稳定构图后从约 `2.1 R` 单调接近；
- Approach 映射具有非零初速且持续加速，鸟从盘外进入、穿过盘心附近，并在物理 Closest 时到达右半盘；
- Periapsis 使用快出慢收的二次曲线，先迅速离开行星轮廓，再逐步减速；
- 30° 最近点镜头通过平滑缩窄 FOV 完成。Rank11 的鸟—行星物理距离决定了相机若保持 `Camera → Bird → Planet` 深度顺序就不能再向行星推进一倍，因此用窄视角完成可控推近，避免穿过鸟或反转遮挡；
- M2 专用响应从 30 提高到 60，只减少已平滑导演曲线的构图迟滞；权威鸟位置、事件和时间均不变。最大单帧位置/旋转仍低于 M1 门。

## 4. 开关与 A/B 合同

运行时控制台变量：

```text
abts.M11.CameraDirector.M2.Enabled 0|1
```

值在 `BeginAuthorityFollow` 时冻结，本次发射中途修改不会改变导演分支。录制器增加进程启动参数：

```text
-ABTSM11CaptureDirectorM2=0|1
```

默认 `0`，以保持 M0/M1 旧镜头兼容。Manifest 和 CSV 必须记录实际冻结模式与逐帧导演混合权重。录制合同升级为 5，观测 CSV Schema 升级为 2；离线工具继续兼容 M1 Schema 1。

## 5. 验收里程碑

### M2-A：纯数学与生命周期

- 共享阶段解析器覆盖 Assist1 Cruise/Approach/Periapsis 与阶段进度；
- M2 只在 Assist1 Cruise 后段、Approach、Periapsis 返回非零权重；
- 双目标 Transform 有限、Up 不翻转、入口和出口权重为 0；
- M2=0 时输出与旧镜头分支一致；
- 开关在一次 authority follow 内冻结。

### M2-B：Rank11 A/B 命令行录制

- 同一 Rank11、Stylized 0 下分别录制 M2=0 与 M2=1；
- 两次 Released Trajectory/PlaybackPlan/阶段决策身份一致；
- M2=1 的 Assist1 Cruise/Approach/Periapsis 当前目标丢失帧显著少于旧镜头；
- 首次看到 Assist1 的帧号和当时像素半径必须显著早于/小于旧导演，避免“出现太晚且一出现就过大”；
- 鸟在 M1 安全框代理内，无位置/旋转/FOV 单帧跃变；
- Assist2/3 和 FinalApproach 的导演混合权重严格为 0。

### M2-C：视觉验收

命令行 AVI 需人工检查 Cruise Lead-in、Assist1 Enter、Approach 中段、Closest、Periapsis 中段和 Exit：鸟先在行星外持续接近，随后作为前景主体加速横穿行星盘面并由右侧减速离场；行星从远处小轮廓连续增大、在近星点形成峰值后缩小，不出现硬切或突发 Roll。离线门同时要求鸟心盘内至少 3 帧、轮廓相交连续至少 3 帧、可观测穿越帧全部为鸟在前；盘内中位横向速度至少为稳定盘外接近段的 1.25 倍，行星峰值半径至少为 Approach 开头的 1.75 倍且最低 FOV 不高于 35°。NullRHI 与 CSV 不能替代视觉层；当前任务未获 GUI/可见 PIE 授权，采用离屏 AVI 作为可交付视觉证据。

## 6. 2026-08-06 Rank11 验收结果

所有录制均为 fresh `UnrealEditor.exe -game -dx11 -RenderOffscreen`、1280×720、30 fps、Rank11 Nominal；Rank11 身份仍为 `UNCERTIFIED`。A/B 只改变 `-ABTSM11CaptureDirectorM2`：

| 指标 | M2=0 旧镜头 | M2=1 提前 Lead-in 导演 |
| --- | ---: | ---: |
| 总帧 / 有效飞行帧 | 949 / 946 | 949 / 946 |
| Assist1 导演资格窗口帧 | 413 | 413 |
| 非零导演混合帧 | 0 | 380 |
| Assist1 当前目标丢失 | 413 | 67 |
| Cruise 当前目标丢失 | 220 | 52 |
| Assist1 首次可见帧 / 像素半径 | 不可见 | 55 / 47.26 px |
| 鸟丢失 | 0 | 0 |
| 导演越界帧 | 0 | 0 |
| 位置 / 旋转 / FOV 跳变 | 0 / 0 / 0 | 0 / 0 / 0 |
| 最大单帧位置 / 旋转 / FOV 变化 | 1360.29 cm / 1.52° / 0° | 1598.60 cm / 9.43° / 0° |

- 两次 `ReleasedTrajectoryHash=0x505F3312AC8AE07F`、`PlaybackPlanHash=0x76B24AB41B6E8B63`，阶段决策指纹相等；离线 `director-ab --require-pass` 通过；
- 首版 M2 到第 237 帧才第一次看到 Assist1，目标像素半径已达 106.38 px；提前 Lead-in 后第 36 帧开始混合，第 55 帧首次看到 47.26 px 的 Assist1 边缘，第 70 帧已有 87.8% 可见，第 80 帧完整入画，之后连续增大；正式 `Assist1Enter` 仍保持在第 223 帧，权威阶段未被篡改；
- Stylized 1 使用同一导演数值再次录制 949 帧，鸟丢失 0、导演越界 0；关键帧可见鸟和 Assist1 轮廓从远处逐渐建立，Approach 中行星轮廓增大，Periapsis 后同时后拉并缩小；
- 最终 Stylized 0/1 的阶段决策、阈值判据和原始相机观测三类指纹全部相等，证明渲染开关没有改变 M2 导演数值；
- 当前大气散射仍使实体颜色不可读，本结论只验收镜头位置、轮廓同框和运动连续性，不评价三渲二颜色质量；
- 首版 M2 证据目录保留为 `Saved/M11CameraCaptures/M2A-20260806-104403/`；提前 Lead-in 最终证据目录为 `Saved/M11CameraCaptures/M2EarlyApproach-20260806-134308/`，A/B 报告为 `M2EarlyApproach-director-ab-report.json`，最终 Stylized 1 AVI 为 `Stylized1/M2EarlyApproach_Rank11_Stylized1.avi`，SHA-256 `F9E1F3E8C01DF2FFAD4366B53CA266D61F870C708F37C5357E76AEBF185B977B`。

### M2.1 Lucy 式单行星编排增量

最终录制为 `Saved/M11CameraCaptures/M2LucySingleEncounterLR-20260806-150622/Stylized1/`，只重新编排行星与主控鸟的镜头关系，不改变候选、三渲二或权威飞行：

| 指标 | M2 提前 Lead-in | M2.1 Lucy 式编排 |
| --- | ---: | ---: |
| Approach 开头鸟相对行星 X 均值 | +284.30 px | -51.29 px |
| Periapsis 可观测末段鸟相对行星 X 均值 | +147.16 px | +242.41 px |
| 左→右叙事方向成立 | 否 | 是 |
| 最近点 ±1 秒行星半径最大加速度 | 42.11 px/s² | 25.20 px/s² |
| 最近点 ±1 秒行星半径最大 jerk | 463.72 px/s³ | 48.87 px/s³ |
| Assist1 鸟丢失帧 | 0 | 0 |
| Assist1 目标丢失帧 | 67 | 70 |

- 第 270–360 帧高频关键帧显示：接近时鸟位于行星左下方，约在 Closest 前后穿过构图中轴，离开时位于右上方；行星轮廓在最近点形成宽峰后连续缩小，没有阶段切镜；
- 位置 / 旋转 / FOV 跳变仍为 0 / 0 / 0，最大单帧旋转 13.14°，低于 M1 的 15°门；导演资格窗内鸟丢失为 0，目标多 3 帧丢失属于冻结方向和早期可读阈值的整体构图取舍，不以破坏主控鸟可读性换回；
- `ReleasedTrajectoryHash=0x505F3312AC8AE07F`、`PlaybackPlanHash=0x76B24AB41B6E8B63` 与前版一致；AVI 950 帧、1280×720、30 fps，SHA-256 `D9D8D320C24602BA05B5395B5EE1E4D7325F26472DFB1E79587B72990ACE99EC`；
- 离线工具只在鸟与当前行星都满足可观测阈值时统计左右关系，并单列 Approach/Periapsis 边界两侧各 30 帧的导数极值，避免 Exit 出画后的半径归零污染最近点平滑性结论。
- 最终 `-ForceUnity -DisableAdaptiveUnity` Development Editor 全链接通过；fresh NullRHI `ABTS.M11C.Unit.FlightCameraAuthorityFrame` 与 `ABTS.M11C.CameraCapture.Config` 均精确发现 1 项、成功 1 项。

### M2.2 Lucy 式前景穿越增量

M2.1 的左→右符号成立，但 193 个 Approach/Periapsis 可观测帧中鸟心进入行星盘和轮廓相交均为 0，最近边缘仍相隔 63.44 px。M2.2 以行星锚定投影弦替换盘外侧视，最终 fresh 录制位于 `Saved/M11CameraCaptures/M2LucyForegroundTransitB-20260806-155659/Stylized1/`：

| 指标 | M2.1 盘外左→右 | M2.2 前景穿越 |
| --- | ---: | ---: |
| Approach 开头鸟相对行星 X 均值 | -51.29 px | -66.36 px |
| Periapsis 可观测末段鸟相对行星 X 均值 | +242.41 px | +295.22 px |
| 鸟心进入行星盘帧 | 0 | 118 |
| 鸟/行星轮廓相交帧 / 最长连续段 | 0 / 0 | 123 / 123 |
| 可观测穿越帧鸟在行星前 | 193 / 193 | 179 / 179 |
| 最小鸟心—行星中心距离 | 2.61 R | 0.266 R |
| Assist1 鸟丢失 / 目标丢失 | 0 / 70 | 0 / 63 |
| 首次 Cruise 目标可见帧 | 60 | 52 |
| 最大单帧旋转 | 13.14° | 10.37° |
| 最近点 ±1 秒行星半径 jerk | 48.87 px/s³ | 43.58 px/s³ |

- 第 100/120/200/223 帧可见鸟在左缘建立并随行星拉近；第 260 帧进入盘内中部，第 300/330 帧位于右半盘/右缘，第 350/380 帧从右侧离场并随行星拉远；
- 离线 `foregroundTransitObserved=true`，且位置 / 旋转 / FOV 跳变仍为 0 / 0 / 0；`ReleasedTrajectoryHash=0x505F3312AC8AE07F`、`PlaybackPlanHash=0x76B24AB41B6E8B63` 未改变；
- AVI 为 949 帧、1280×720、30 fps，`avih.dwTotalFrames`、视频流 `strh.dwLength` 与 `idx1` 条目均为 949，SHA-256 `666A229FB03AE4514A2F9BA068C813FEC4E51588B203E6B948247E56B561B85D`；
- 全局 `criteriaPassed=false` 是预期结果：M2 只导演 Assist1，Assist2、Assist3 与 UFO 仍使用旧镜头，不应把后续阶段失败归到本次单行星验收；
- 当前大气散射仍使实体颜色不可读，三渲二轮廓足以验证遮挡关系；颜色与轮廓像素质量继续由集成工作树负责。

### M2.3 Lucy 式速度与镜头距离增量

最终 fresh 录制为 `Saved/M11CameraCaptures/M2LucyPacingResponsive-20260806-173113/Stylized1/`。与 M2.2 使用同一 Rank11、Stylized 1、1280×720、30 fps、权威轨迹和事件：

| 指标 | M2.2 前景穿越 | M2.3 速度与推拉 |
| --- | ---: | ---: |
| 稳定盘外接近速度样本 | 0 | 94 |
| 盘外中位横向速度 | 无 | 8.34 px/s |
| 盘内近星中位横向速度 | 16.67 px/s | 80.79 px/s |
| 近星 / 盘外速度比 | 无 | 9.68× |
| 行星峰值半径 | 136.87 px | 238.22 px |
| 峰值 / Approach 开头半径 | 1.36× | 2.33× |
| 最低 FOV | 50° | 30° |
| 鸟心盘内 / 轮廓相交帧 | 118 / 123 | 100 / 114 |
| 可观测穿越帧鸟在前 | 179 / 179 | 179 / 179 |
| Assist1 鸟丢失 / 目标丢失 | 0 / 63 | 0 / 63 |
| 最大单帧位置 / 旋转 / FOV 变化 | 1598.60 cm / 10.37° / 0° | 2186.70 cm / 10.39° / 0.53° |

- 第 110–223 帧鸟在行星左侧约 `2.14 R → 1.09 R` 持续接近；第 240–320 帧从左侧盘内加速经过中心并到达右半盘；第 330–390 帧快速出盘后减速；
- 行星半径从 Approach 开头约 102 px 增大到 238 px，随后恢复到约 107 px；FOV 由 50° 平滑缩至 30° 并恢复，位置 / 旋转 / FOV 跳变均为 0；
- 新增 `nearPlanetScreenSpeedupObserved=true`、`closestLensZoomObserved=true` 和组合门 `lucyPacingObserved=true`，旧 M2.2 在新门下为 false；
- AVI 为 949 帧，`avih.dwTotalFrames`、视频流 `strh.dwLength`、`idx1` 与 JPG 数量全部为 949，SHA-256 `CB78A63F8E09F9907C7B17D5FDC67E399A126A1B2159CDFC55A8EE255256DDF7`；
- 当前画面仍受大气散射影响，只验收轮廓、遮挡、速度和屏占关系，不评价颜色。

## 7. 性能与失败策略

- 每帧增加固定数量向量、三角函数和一次 Transform 混合；不扫描 Actor、不分配 UObject、不读像素，Game Thread 目标 `<0.10 ms`；
- 只有 M2 开启且处于 Assist1 窗口时计算双目标变换；其他阶段仅多一次分支；
- 权威事件、目标半径或有限性不满足时 fail closed，不回退到最近 Actor；
- 双目标数学失败时本次交互失败并保留明确日志，禁止静默使用未经标记的半套导演结果。

## 8. 明确非结论

- M2 不处理 Assist2/3 交接，归 M3；
- M2 不处理 UFO 构图，归 M4；
- M2 不评价大气散射、颜色、轮廓或三渲二像素质量；
- Rank11 Nominal 录屏不能替代 M11-B v2.2 全操作域认证。
