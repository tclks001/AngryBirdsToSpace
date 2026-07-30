# M6/M9：弹弓与卫星标定模式

> 状态：`integration/m9-satellite-e5-20260730` 候选实现已落地；2026-07-30 已通过强制 Unity 编译、fresh 6/6 自动化、标定 runtime smoke、生产 M9 回归及 M11 隔离回归。卫星碰撞一致预测、背面 E5 画中画和卫星相对镜头仍待可见 PIE。只有可见 PIE 通过后，本文 V0 候选才可作为 M3R-4.1 与 M7 卫星攻击面的冻结输入；这里的 V0 是候选阶段名，序列化 `PracticePreset.Version` 已升为 2。
>
> 父级：[M6 发射、弹道与碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M9 卫星与局部引力](M9SatelliteGravityDesign.md) · [M3R 月度地图改进](M3PCGMapImprovementPlan.md)
>
> 交接：[项目工作流](ABTSProjectWorkflow.md) · [主设计稿](AngryBirdsToSpaceGameDesign.md) · [M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md)

## 1. 为什么需要独立标定模式

月度道路、六栋 M7 建筑及其攻击面尚未冻结时，不能根据旧地图的建筑坐标反推弹弓能力。旧地图在本阶段只提供主星、角色、相机和 M6/M9 运行环境，是标定载体而不是布局依据；专用入口不生成 M7/PCG 建筑。需要先冻结的是：

- Twig、Simple、Reinforced 的 Pull→初速度曲线；
- 60%–85% 功率下的舒适射程、100% 功率附近的极限射程及档位差；
- 拉弓距离、滚轮功率步长、瞄准灵敏度和最大瞄准平面偏移；
- 卫星半径、球心离主星表面距离、表面引力比例；
- 卫星背面代理目标的局部角度、方位角、立方体半边长和表面间隙；
- 当前 Party 实际弹射碰撞半径的运行时快照；
- 强化档卫星练习的离散连通成功岛、区间外反例和可见 PIE 中的连续操作手感。

本阶段明确不冻结：

- 六栋建筑和卫星在最终 Seed 中的绝对世界坐标；
- M7 Shape Grammar/WFC 的实体结构和弱点；
- M3 最终道路、PVS、侦察引导和 Encounter Cell 距离窗。

## 2. 隔离入口

标定模式不新增正式地图，复用旧 `L_ABTS_M9`：

```text
/Game/Maps/L_ABTS_M9?game=/Game/Blueprints/BP_ABTSSlingshotSatelliteCalibrationGameMode.BP_ABTSSlingshotSatelliteCalibrationGameMode_C
```

`AABTSSlingshotSatelliteCalibrationGameMode` 直接继承 `AABTSM6GameMode`，再显式组合一颗练习卫星、标定 Rig 和 M10 侦察/轨道表现。它不继承 M7→M8→M9 的生产 GameMode 链，因此：

- 不生成或等待 M7 建筑；
- 不修改生产 M9 的 `SatelliteWindow` 与 Finale 隔离规则；
- 不让旧地图的建筑生成成功与否阻塞标定；
- 普通 M6/M9/M10 与 M11 Space 档不调用标定配置时保持原参数。

标定局部坐标以本次玩家出生点为原点，以出生点径向为 Up，以玩家初始切平面前向为 Forward。卫星和目标都由比例、角度和局部距离生成。离散卫星解不再从理想起点猜测发射方向：Rig 必须从实际生成的 Reinforced 弹弓读取 cord 两端、`SlingCenter`、`SlingUp/Forward/Right`、真实 `RestPouch` 和鸟在袋中的偏移；同时从实际 M6 瞄准相机捕获 `CameraLook`、`CameraScreenUp` 与 `CameraScreenRight`。认证样本的 Aim 偏移沿真实鼠标投影平面的屏幕基轴构造，而不是把 `SlingUp/SlingRight` 当作屏幕方向。

本候选同时修复生产 `AABTSM9GameMode` 的 deferred spawn 双重平移：卫星在 Finish 前已移动原生 Root，`FinishSpawningActor` 必须传回最初的 `FTransform::Identity`，并用 `IsAtConfiguredCenter()` fail closed。该修复不改变生产 M9 的配置参数，但会纠正错误世界位置，必须单列执行第 7.3 节生产回归，不能只用隔离 GameMode 的 smoke 代替。

## 3. 数据边界

### 3.1 LaunchProfileCatalog V0

`FABTSM6LaunchProfileCatalog` 是三档普通弹弓的版本化数据源。每档记录：

- `MinimumSpeedCMPerSec`、`MaximumSpeedCMPerSec`；
- `PowerExponent`，公式为
  `Speed = Lerp(Min, Max, PullAlpha ^ PowerExponent)`；
- `MinimumPullDistanceCM`、`MaximumPullDistanceCM`；
- `InitialPullAlpha`；
- `PullPowerWheelStep`；
- `AimSensitivityScale`、`MaximumAimPlaneOffsetCM`；
- `ComfortablePullMinimum/Maximum`。

`BP_ABTSSlingshotSatelliteCalibrationGameMode` 的 `LaunchProfileCatalog` 是三档曲线、拉距、滚轮、Aim 手感和共享阻力的唯一人工调参入口。相机构图不再作为该目录中的第二套可编辑参数；运行时从 `BP_ABTSM6SlingshotCamera` 的实际实例读取以下四项快照，放入解析后目录并签入 `LaunchProfileHash`：

- `AimCameraDistanceCM`；
- `AimCameraPitchDegrees`；
- `AimTargetForwardDistanceCM`；
- `AimTargetHeightCM`。

当前候选值：

| Tier | 初速度范围 | 指数 | 舒适功率 |
| --- | ---: | ---: | ---: |
| Twig | 700–1700 cm/s | 1.15 | 60%–85% |
| Simple | 900–2300 cm/s | 1.08 | 60%–85% |
| Reinforced | 1050–3300 cm/s | 1.00 | 60%–85% |

三档共享：

- `MinimumPullDistanceCM=120`、`MaximumPullDistanceCM=430`；
- `InitialPullAlpha=0.55`、`PullPowerWheelStep=0.04`，因此认证功率点必须来自玩家用滚轮实际可进入的档位；滚轮步长的合法下限是 `0.01`（完整合法范围 `0.01..1.0`），小于该值必须 fail closed，不能借近似连续的超小步长扩大成功岛；
- `AimSensitivityScale=1.0`、`MaximumAimPlaneOffsetCM=260`；
- 相机构图以 `BP_ABTSM6SlingshotCamera` 的 Class Defaults 为准；原生类的 1150 cm、18°、900 cm、245 cm 只是在该蓝图未覆盖时使用的回退值；
- `FlightAirDragPerSecond=0.08`。

目录解析必须恰好得到 Twig、Simple、Reinforced 各一项；缺项、重复、非有限值、不可进入的初始功率和反向速度域均 fail closed。实际 Camera Blueprint 的构图值同样必须合法，否则标定入口 fail closed。运行时不得用 Catalog 反写 Camera Actor。Space 档不进入此目录，继续服从 M11 的同源固定步长求解器。

### 3.2 SatellitePracticePreset V0（序列化 Version 2）

`BP_ABTSSlingshotSatelliteCalibrationGameMode` 的 `PracticePreset` 是卫星局部布局和背面目标的唯一人工调参入口。原生构造函数只提供新建蓝图时的安全候选值；运行时不再接受几何 CVar 或命令行覆盖，避免 Editor 中显示的值与实际生成值不一致。

| 字段 | 当前候选 | 含义 |
| --- | ---: | --- |
| `Version` | 2 | E5 表面立方体几何；旧球靶 Version 1 身份作废 |
| `SatelliteRadiusPrimaryRatio` | 0.125 | 卫星半径/主星基础半径 |
| `SatelliteCenterClearancePrimaryRatio` | 0.5 | 卫星球心离主星连续表面的距离/主星基础半径 |
| `SatelliteAnchorArcDegrees` | 30° | 出生点到卫星锚方向的主星表面弧角 |
| `SatelliteSurfaceGravityPrimaryRatio` | 2.0 | 卫星/主星表面重力比 |
| `TargetBody` | `PracticeSatellite` | 目标所属天体身份 |
| `BacksideAngleDeg` | 178° | 从“卫星面向 Reinforced RestPouch 方向”量取的背面角 |
| `TargetLocalAzimuthDeg` | 20° | 绕面向发射点轴的局部方位 |
| `TargetProxyRadiusCM` | 420 cm | 兼容字段名；Version 2 语义为 E5 立方体半边长 |
| `TargetSatelliteClearanceCM` | 20 cm | E5 底面与卫星理想球面的间隙 |
| `PullMinimum/Maximum` | 0.75 / 1.0 | 当前蓝图候选的离散认证功率域 |

E5 由卫星局部正交基构造并附着到卫星 Actor，局部 `+Z` 始终指向卫星外侧：

```text
TargetCenter = SatelliteCenter
             + Outward
             * (SatelliteRadius + CubeHalfExtent + SurfaceGap)
```

因此 E5 底面位于 `SatelliteRadius + SurfaceGap`，不会漂浮到旧球靶高度，也不会嵌入卫星理想球面。`TargetBody` 不是仅供显示的标签：非 `PracticeSatellite` 值必须拒绝，不能把拼写错误静默解析到主星或任意 Actor。

`BirdCollisionRadiusCM` 不再是蓝图可编辑参数。Rig 从当前四鸟的实际弹射碰撞体读取最大半径并在构建场景后写入快照；当前 Chaos 球权威值为 `42 cm`。该值参与 E5 OBB、主星/卫星扩张球扫掠和 `SatellitePracticePresetHash`。原生 factory 仍只提供新建蓝图的安全回退（净空比 `0.125`、引力比 `0.45`、认证上限 `0.95`）；它不覆盖上表的当前蓝图候选。

### 3.3 三种身份及可移植边界

- `LaunchProfileHash`：目录版本、三档曲线、滚轮与 Aim 手感字段、四项真实相机构图参数及共享阻力；
- `BaselineGravitySnapshotHash`（代码访问名仍为 `GravitySnapshotHash`）：本次运行场景中主星尺度/表面重力、卫星相对球心的世界向量、尺度、表面重力、初始启用状态与阻力；
- `SatellitePracticePresetHash`：局部布局、目标参数和扫掠域。

`LaunchProfileHash` 与 `SatellitePracticePresetHash` 使用量化数值和稳定字符串字节，不使用 Actor 指针、`FName` 进程索引、CellId 或绝对世界原点，是可跨地图/Seed 携带的候选身份。Preset Hash 必须在解析实际鸟碰撞半径之后计算，且只在相同碰撞契约下可移植；Version 1 球靶的旧 Hash 已作废。`GravitySnapshotHash` 只保证同一场景实例的平移不变；它包含实际卫星相对世界向量与连续地表解析结果，会随 Seed、地形或整体朝向变化，因此只是 baseline scene-instance 证据，**不得**作为 M3 跨 Seed 的稳定目录身份。

`abts.Calibration.SatelliteGravity` 在运行中切换实际卫星引力，并在 HUD 单独显示当前 `ON/OFF`；它不改写 baseline hash。看到 `Gravity=OFF` 与原 baseline hash 同时存在是预期行为，不表示 OFF 状态被签入该 Hash。M3/M7 保存可移植版本/哈希时只使用 `LaunchProfileHash` 与 `SatellitePracticePresetHash`；场景 Witness 若保存 `GravitySnapshotHash`，必须明确它是该 Witness 的实例快照并在重算时重新生成。

## 4. 场景与 HUD

标定 Rig 在出生点附近生成：

- Twig、Simple、Reinforced 各一套完整可交互弹弓；
- 每档一枚舒适射程代理和一枚极限射程代理，共六枚；
- 一枚附着于卫星局部坐标的背面目标代理。

六枚射程代理仍是无物理阻挡的 Query-only 球体；E5 则是附着在卫星表面的阻挡 `Engine Cube`。实际 Chaos blocking contact 是首要命中证据，Rig 在 `TG_PostPhysics` 对鸟的上一帧→本帧位置执行精确 sphere-vs-OBB 扫掠作为高速穿越与同帧 E5/卫星体仲裁回退；卫星体使用实际鸟半径扩张后的理想球。Chaos 的 E5/body 回调只记录同帧候选，Rig 必须在同一段上统一比较 `TargetAlpha` 与 `SatelliteAlpha` 后双向定案，因此 E5-first 也能被更早的卫星体改判，body-first 也能被更早的 E5 改判；首个已定案帧之后不得被反弹或持续接触重写。M6 从 `Flying/Settling` 切到 `Returning` 的首帧仍只暴露一次 pending completion sample。

M6 只读预测快照现在携带完整路径、最近卫星净空以及 `PrimarySurface / SatelliteBody / SatelliteE5` 终点类型。每个积分步必须先求 E5 OBB、所有卫星扩张球和主星表面的候选交点，再以全局最小 Alpha 截断；不能因代码检查顺序固定偏向卫星。标定 Rig 显式把练习卫星、E5 Actor、OBB 半边长以及认证用的 `0.04 s × 30 s` 积分域注册给 M6/M10，使标定 HUD 预览、成功岛认证与实际超时使用同一时间域；普通 M6/M9 未注册该 context 时继续使用既有预测预算。Pulling 轨迹进入卫星/E5 邻域时，E5 画中画优先于普通主星落点预览，复用同一 SceneCapture/RenderTarget，只显示卫星、E5 和局部轨迹，并使用无光照 `SCS_BaseColor` 指引视图避免背面全黑；它不等于修改了世界真实背面照明。

同一 M6 相机在实飞时按 `PrimaryFollow → SatelliteApproach → SatelliteOrbit → E5Approach → E5Impact` 过渡，以稳定轨道侧视法线避免近月翻转；Orbit 与 E5Approach 分别使用不同的进入/退出阈值，避免鸟在边界附近时来回刷阶段。E5 命中保持 `1.2 s` 后确定性回收，回收期间锁回主星 frame，直到下一次发射；这些表现仅在显式标定 context 中启用。

HUD 显示：

- 两个可移植候选哈希、一个 baseline 场景快照哈希和当前卫星引力开关；
- 三档舒适/极限地表弧长，单位同时给出米和主星半径比；
- 离散功率 × 规则 Aim 网格的成功岛摘要；
- 当前或上一发的 Tier、Pull、Aim、初速度、落点弧长、最高点、路径长度、飞行时间、目标和是否先撞卫星。

真实发射记录由 M6 在标定模式下广播。普通 `OnLaunchCompleted` 不变，M10 的既有消费者不需要感知该委托。

## 5. 标定顺序

### 5.1 先关闭卫星引力

控制台输入：

```text
abts.Calibration.SatelliteGravity 0
```

依次使用三档弹弓，比较 HUD 射程代理和真实落点。验收：

- 同档 60%–85% 功率有明显角度容错；
- 极限射程需要接近满功率；
- 相邻档舒适射程至少相差约 25%；
- Twig、Simple、Reinforced 的能力升级在不看 HUD 数字时也可辨认。

### 5.2 固定 Reinforced 后开启卫星

```text
abts.Calibration.SatelliteGravity 1
```

调整顺序固定为：卫星半径 → 离地距离 → 背面目标局部角度/尺寸 → 卫星表面引力。禁止先增大卫星引力来掩盖强化弹弓本身射程不足。

在 `BP_ABTSSlingshotSatelliteCalibrationGameMode` 的 Class Defaults 中修改 `PracticePreset`，保存并重新进入 PIE；现有卫星、目标和 Hash 不热更新。三档发射参数在同一蓝图的 `LaunchProfileCatalog.Profiles` 中修改；相机距离、俯仰与观察点只在 `BP_ABTSM6SlingshotCamera` 中修改。运行日志的 `[ABTS][Calibration][ProfileCatalog] Ready` 必须显示实际 `CameraClass` 和最终四项构图值。

只有 `abts.Calibration.SatelliteGravity -1/0/1` 保留为诊断用 live 开关：`-1` 恢复本次 baseline、`0` 关闭、`1` 开启。它只做同一布局的引力 A/B 对照，不是布局参数源。

## 6. 成功岛与反例

确定性认证模型读取真实 Reinforced cord/pouch frame，以 `(Pull, AimInPlaneOffsetCM, AimOutOfPlaneOffsetCM)` 组成批准的离散全域：

- Pull：从 `InitialPullAlpha=0.55` 按 `WheelStep=0.04` 枚举所有玩家可进入档位；当前蓝图认证带为 75%–100%，即 `0.75/0.79/0.83/0.87/0.91/0.95/0.99/1.00`；
- InPlane：`-260..260 cm`，41 点，沿实际相机鼠标投影平面的 `CameraScreenUp`（LaunchFrame 的 `AimInPlaneAxisWorld`）；
- OutOfPlane：`-80..80 cm`，5 点，沿实际相机鼠标投影平面的 `CameraScreenRight`（LaunchFrame 的 `AimOutOfPlaneAxisWorld`）；
- `length(AimPlaneOffset) > 260 cm` 的圆盘外组合跳过，不计作已采样输入；
- 固定步长：0.04 s；最长 30 s；该两项同时配置 M6 标定预览和实际标定发射超时，不允许另留一套 0.075 s/12 s 的屏幕轨迹域。

LaunchFrame 同时保存真实相机的 `CameraLook` 为 `AimPlaneNormalWorld`，并保存与之正交的 ScreenUp/ScreenRight；从 Camera Blueprint 采样的四项构图快照进入解析后 Catalog 和 `LaunchProfileHash`。每个样本用与 M6 `UpdatePouchAndPreview/ComputeLaunchVelocity` 相同的鼠标投影平面、pouch 位置、发射方向和鸟偏移构造初始状态，再用同一局部两体快照分别积分“卫星引力开/关”。每步用实际鸟半径对 E5 OBB 做精确圆角扫掠，并对卫星扩张球和主星扩张球求最早线段交点；最近目标净空同样按整段精确求值，不再用四点采样估计。只有同时满足下列条件的样本进入成功集：

1. 引力开启时先命中 TargetProxy；
2. 没有先撞卫星或主星；
3. 同一输入关闭卫星引力后不命中；
4. 关闭引力后的最近净空不少于 60 cm。

在离散三维网格上以六邻接求连通分量。正式通过要求：

- 最大分量不少于三个样本；
- 最大分量同时跨越相邻 InPlane Aim 和相邻可进入 Pull 档；
- Simple 满功率在同一瞄准域命中数为零；
- Reinforced 在所有玩家可进入、但位于当前 75%–100% 认证带之外的 Pull 档命中数为零；
- 重复计算的结果哈希和计数严格一致。

这里的“全域”仅指上述离散 Pull × 规则 Aim 网格，不能表述为数学上的连续输入域。离散连通岛是自动化结构门；玩家在格点之间是否仍有自然容错，必须由第 8 节 Visible PIE 决定。该门不允许忽略卫星球体碰撞，也不能用一个孤立格点冒充可操作成功岛。

## 7. 自动化与运行时认证

### 7.1 纯数据自动化

```powershell
$EditorCmd = "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Project = "$ProjectRoot\AngryBirdsToSpace.uproject"
$Log = "$ProjectRoot\Saved\Logs\M6M9-Calibration-FreshAutomation.log"

& $EditorCmd $Project `
  -unattended -nop4 -NullRHI -NoSound -NoMessaging `
  "-ExecCmds=Automation RunTests ABTS.Calibration;Quit" `
  "-TestExit=Automation Test Queue Empty" `
  "-abslog=$Log"
```

必须精确发现并通过：

```text
ABTS.Calibration.ProfileCatalog
ABTS.Calibration.SatellitePreviewGeometry
ABTS.Calibration.StableHashes
ABTS.Calibration.SuccessIsland
ABTS.Calibration.SweptCollision
ABTS.Calibration.TargetGeometry
```

其中 `ProfileCatalog` 覆盖 `PullPowerWheelStep=0.005` 的 fail-closed 反例；四项 Aim 相机构图均进入哈希输入；`StableHashes` 验证相机构图与实际鸟碰撞半径会改变身份；`TargetGeometry` 覆盖贴地立方体局部 frame、平移不变与错误配置拒绝；`SweptCollision` 覆盖精确球/OBB 圆角、零长度/变换不变及 60 cm 净空门；`SatellitePreviewGeometry` 覆盖从完整弯曲路径选择最近画中画线段。

门槛是精确 6/6 Success、唯一 `TEST COMPLETE. EXIT CODE: 0`、进程退出码 0，且没有本项目 Fatal、assert、ensure 或 `LogABTSRuntime: Error`。零匹配、少于六项、只看进程退出码或复用旧日志均失败。

### 7.2 fresh 旧地图认证

```powershell
$Log = "$ProjectRoot\Saved\Logs\M6M9-Calibration-FreshRuntime.log"
& $EditorCmd $Project `
  "/Game/Maps/L_ABTS_M9?game=/Game/Blueprints/BP_ABTSSlingshotSatelliteCalibrationGameMode.BP_ABTSSlingshotSatelliteCalibrationGameMode_C" `
  -game -NullRHI -unattended -nop4 -nosplash -NoSound -NoMessaging `
  -ABTSCalibrationSmoke "-abslog=$Log"
```

唯一通过终态：

```text
[ABTS][Calibration][RuntimeCertification] Terminal=1 Passed=1 Failed=0
```

它同时验证：

- 三套弹弓、六枚 Query-only 射程球和一枚阻挡 E5 立方体均生成；
- `[ABTS][Calibration][E5]` 显示 `Shape=Cube HalfExtent=420 SurfaceGap=20`，`[Ready]` 显示实际 `BirdCollisionRadius=42`；
- 真实 Reinforced cord/pouch frame 与相机 `Look/ScreenUp/ScreenRight` 鼠标投影平面已捕获；
- 三档射程包络有效；
- 成功岛通过、Simple 满功率反例成立、Reinforced 认证功率带外命中为零；
- 两个可移植身份哈希与一个 baseline 场景快照哈希均非零；
- baseline 引力开启且 M10 ScoutMap 已显式揭示；
- 世界中 `AABTSM73StableBuildingActor` 数量为零。

通过日志的 `Reason` 必须同时含 `Slingshots=3 Targets=7 Envelopes=3 Sweep=1 SimpleHits=0 OutsidePullHits=0 Gravity=1 ScoutMap=1 Buildings=0`。缺任一字段、出现多个终态或进程退出码非零均失败。该 smoke 不执行真实鼠标发射、SceneCapture 像素检查或相机演出，不能替代第 8 节。

### 7.3 生产 M9 deferred transform 回归

隔离 smoke 使用 `ConfigureFromPrimaryDirection`，不能独自证明生产 `ConfigureFromPrimaryPlanet` 链路。先重跑 M11.0 的稳定分离用例：

```powershell
$Log = "$ProjectRoot\Saved\Logs\M6M9-M110-Separation-Regression.log"
& $EditorCmd $Project `
  -unattended -nop4 -NullRHI -NoSound -NoMessaging `
  "-ExecCmds=Automation RunTests ABTS.M110.TaskGraphFinaleSeparation;Quit" `
  "-TestExit=Automation Test Queue Empty" `
  "-abslog=$Log"
```

该入口必须精确 1/1 Success。随后以 fresh 生产 M9 GameMode 启动旧图，并由调用脚本只结束自己记录的 PID：

```powershell
$Log = "$ProjectRoot\Saved\Logs\M6M9-ProductionM9-FreshRuntime.log"
$M9 = Start-Process $EditorCmd -WindowStyle Hidden -PassThru -ArgumentList @(
  $Project,
  "/Game/Maps/L_ABTS_M9?game=/Script/ABTSRuntime.ABTSM9GameMode",
  "-game", "-NullRHI", "-unattended", "-nop4", "-nosplash",
  "-NoSound", "-NoMessaging", "-ExecCmds=t.MaxFPS 60",
  "-abslog=$Log"
)
if (-not $M9.WaitForExit(60000)) {
  Stop-Process -Id $M9.Id
}
```

生产回归要求恰好一条 `[ABTS][M9] Satellite ready`，默认值仍为 `Radius=1250.0 Clearance=1250.0 Gravity=245.0`，并有 `FinaleGravitySource=0`；不得出现 `deferred finish changed center`、`Satellite rejected`、Fatal、assert 或 ensure。`IsAtConfiguredCenter()` 是生产世界位置门；隔离标定蓝图的布局/引力参数不能泄漏到生产 M9 的 `0.25` 表面引力比。

### 7.4 当前候选自动化留证

2026-07-30 在 `integration/m9-satellite-e5-20260730` 当前候选上重跑并通过：

- 强制 Unity：`-ForceUnity -DisableAdaptiveUnity -NoHotReload -NoHotReloadFromIDE`，`Result: Succeeded`；
- `ABTS.Calibration.*`：精确 6/6 Success，`TEST COMPLETE. EXIT CODE: 0`；
- 标定 runtime：唯一 `Terminal=1 Passed=1 Failed=0`，组合原因为 `Slingshots=3 Targets=7 Envelopes=3 Sweep=1 SimpleHits=0 OutsidePullHits=0 Gravity=1 ScoutMap=1 Buildings=0`；
- 蓝图相机构图：`BP_ABTSM6SlingshotCamera_C`，`Distance=1500 cm`、`Pitch=-3°`、`TargetForward=900 cm`、`TargetHeight=245 cm`；
- 实际 Reinforced 成功岛：`Hits=12`、`Pull=[0.79,0.87]`、`AimInPlane=[-195,-169] cm`、`LargestIsland=5`，Aim/Pull 邻接均成立；同输入关闭卫星引力后最小错失 `2373.3 cm`，`SimpleHits=0 OutsidePullHits=0`；
- 标定场景解析值：`SatelliteRadius=1250 cm`、`Clearance=5000 cm`、`SurfaceGravity=1960 cm/s²`，E5 半边长 `420 cm`、表面间隙 `20 cm`；
- 生产 M9 fresh runtime：唯一 `ABTSM9GameMode`、唯一 `Satellite ready`、零 rejected/transform error；
- `ABTS.M110.TaskGraphFinaleSeparation` 与 `ABTS.M11C.Runtime.ContractRoutingAndM9Isolation`：各 1/1 Success。

本次 runtime 的候选身份为 `LaunchProfileHash=2920060455991611804`、`SatellitePracticePresetHash=4556705819126274791`、`SweepResultHash=2476487998726195302`；`BaselineGravitySnapshotHash=5302779937323207981` 只记录该场景实例，不作为跨 Seed 身份。上述数据是自动化留证，不等于第 8 节可见 PIE 已通过，也不把 V0 提前标记为冻结。

## 8. 可见 PIE 验收

自动化不能代替手感与可读性。使用第 2 节 URL 启动可见 PIE 后逐项确认：

- [ ] 三档能力与弹道差异不看 HUD 数字也可辨认；共享的初始 Pull、`0.04` 滚轮步长和真实相机屏幕平面 Aim 操作不会造成突跳、轴向错位或不可进入档位；
- [ ] HUD 舒适和极限射程与真实落点相符；
- [ ] 强化档存在玩家可重复找到的连续背面目标成功岛；
- [ ] 同一输入关闭卫星引力后明显飞偏；
- [ ] Simple 满功率不能完成卫星背面目标；
- [ ] E5 立方体位于远离主星的卫星半球，底面贴近理想球面且不漂浮、不嵌入；
- [ ] 预测终点为 E5 时，实际 `42 cm` 鸟不会先撞卫星；预测为 `SatelliteBody` 时，实飞确实先撞卫星；
- [ ] 卫星背面画中画在背光面仍清晰，只显示卫星、E5 与局部轨迹；切回普通主星落点预览后无残留 ShowOnly/BaseColor 状态；
- [ ] `PrimaryFollow → SatelliteApproach → SatelliteOrbit → E5Approach → E5Impact` 无翻转或跳切；命中保持约 `1.2 s` 后回收，失败回收也恢复主星 frame；
- [ ] M10.1 轨道全景图能清晰展示卫星造成的偏转；
- [ ] 自动化岛集中在约 79%–87%，连续鼠标输入下仍有自然容错；满功率属于认证域但不是唯一正确解；
- [ ] 普通 M6/M9 与既有 M10.1-B 主星落点画中画行为无回归；
- [ ] 日志中的真实发射遥测字段完整且没有 Fatal、assert 或 ensure。

可见 PIE 通过后，把实测两个可移植 Hash、该场景的 baseline `GravitySnapshotHash`、射程包络和成功岛手感结论写入本节验收记录，再将状态改为“V0 已冻结”。自动化或 runtime smoke 通过不能提前勾选本节。

## 9. 下游交接

M3R-4.1 只消费：

```text
SlingshotTier
LaunchProfileHash
ComfortableReachEnvelope
MaximumReachEnvelope
GravitySnapshotHash
SatellitePracticePresetHash
TargetProxy / AttackFace
```

其中只有 `LaunchProfileHash`、射程包络和 `SatellitePracticePresetHash` 是跨 Seed 输入；`GravitySnapshotHash` 是每个 Witness 解析场景后的实例证据，不能作为全局 Catalog 身份。R3 用射程包络粗筛站点。

本文的固定步长二体积分器是标定认证与 M3R-4.1 前置预筛模型，不是生产 M6/M9 的权威实飞 Provider。M3R-4 在宣称生产 Witness 前，仍须由 Integration 提供经批准的只读生产适配器，并用实际 M6/M9 预览/实飞做重放误差门；M3 不复制速度曲线、pouch 几何或引力公式。

M7 后续只需让实际卫星建筑的认证 AttackFace 消费已通过的卫星局部 surface frame 与 OBB 半边长；结构生成、弱点稳定性和视觉轮廓由 M7 自己认证，但不得反向改变已冻结的 M6/M9 参数。若实体攻击面必须改变代理几何，视为跨阶段契约变更，回到 Integration 重新跑完整离散认证网格与可见 PIE。
