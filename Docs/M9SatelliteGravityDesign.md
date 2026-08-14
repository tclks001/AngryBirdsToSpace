# M9：卫星与局部引力

> 状态：生产 M9 基线已实现；deferred transform 修复已通过 fresh 生产回归。独立 M6/M9 标定模式的碰撞一致预测与月面着陆画中画已落地，画中画和参数手感于 2026-07-31 通过可见 PIE，Launch/Preset 参数已冻结为 V0；卫星实飞主镜头仍延期到[统一镜头视觉优化](ABTSCameraVisualOptimizationDesign.md)。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M2 球面基础](M2PlanetSurfaceDesign.md) · [Chaos 刚体移动](ChaosRigidBodyMovementDesign.md) · [M6 发射与碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M6/M9 标定模式](M6M9SlingshotSatelliteCalibrationDesign.md) · [统一镜头视觉优化](ABTSCameraVisualOptimizationDesign.md) · [M10.1 超视距目标与引力走廊](M101BeyondHorizonLaunchInterfaceDesign.md) · [Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [M11.0 终局前置收口](M110PreFinaleClosureDesign.md)。

## 1. 范围与职责

M9 生成一颗位于强化弹弓练习支线 `SatelliteWindow` 上方的小卫星，并为鸟的移动/弹道提供局部引力：

```text
TaskGraph SatelliteWindow 的 Seed Cell（逻辑锚点）
  -> 主星连续表面查询（仅取得表现位置/法线）
  -> AABTSM9Satellite 独立灰色球面
  -> M9 Gravity Query
  -> Chaos 鸟 / 历史力悬挂鸟 / M6 预测弹道
```

卫星不读取或修改主星的道路、水网、Cell 状态、SDF、HISM、建筑和资源。它有自己的轻量 `CellTopo`，但此阶段不在其上生成任何 Task、材料或建筑；其 CellTopo 仅满足“卫星本身也是可扩展球面实体”的拓扑基础。

## 2. 默认空间与拓扑规格

在 `AABTSM9GameMode` 中，现行锚点固定为 `SatelliteWindow`。它的 Seed Cell 是唯一的卫星下点逻辑位置源；主星 SDF/连续网格不参与“卫星练习区在哪里”的判定。历史 `FinalAnchorTaskType` 字段只为旧 Blueprint/序列化兼容保留，运行时忽略任何非 `SatelliteWindow` 覆盖，不能再把卫星放回 `LaunchSite`。

| 项目 | 默认值 | 含义 |
| --- | ---: | --- |
| 卫星逻辑细分 | Sub=2 | 独立 `AABTSM2Planet` CellTopo。 |
| 卫星渲染细分 | Sub=4 | 独立 ProceduralMesh 连续表面。 |
| 半径 | 主星半径 × 1/8 | `Satellite Radius Primary Ratio=0.125`。 |
| 卫星球心离主星任务地表距离 | 主星半径 × 1/8 | `Satellite Center Clearance Primary Radius Ratio=0.125`。 |
| 材质 | 纯灰色 | 无 SDF、无地形分层、无 HISM。 |

“球心离地面距离”从该 `SatelliteWindow` Seed Cell 对应的主星 `QuerySurface` 结果沿局部径向法线量取。因此即使主星有 PCG 高度变化，卫星仍和练习区地表保持配置的几何间距。

### 2.1 M11.0 的终局隔离与共线防线

M3 V3 首先保证 `SatelliteWindow` 与最终认证 `LaunchSite` Anchor 的球面角距不低于 `55°`。M9 生成卫星后再相对 `FABTSM110FinaleLocalFrame` 执行运行时验证：

```text
Distance(SatelliteCenter, FinaleOrigin)
    >= PrimaryRadius * MinFinaleSatelliteDistancePrimaryRadiusRatio

abs(dot(FinaleRight, TangentDirectionToSatellite))
    >= MinFinaleSatelliteLateralAlignmentDot
```

首版默认分别为 `0.80` 和 `0.98`。前者避免卫星贴近终局发射邻域；后者保证两个太空桩的左—右轴和卫星下点方向近似共线。不满足任一条件时应销毁候选卫星并输出拒绝日志，不能静默使用旧位置。

## 3. 引力模型

卫星引力在其自身表面取配置加速度，空间中按反平方衰减：

```text
ToSatellite = SatelliteCenter - Position
d           = max(length(ToSatellite), SatelliteRadius)
aSatellite  = normalize(ToSatellite) * SurfaceGravity * (SatelliteRadius / d)^2
```

默认 `Satellite Surface Gravity Primary Ratio=0.25`，因此卫星表面加速度为 `980 × 0.25 = 245 cm/s²`。卫星引力与主星径向引力相加，而非替换主星引力。

本期接入：

- Chaos 刚体鸟：每个物理 Tick 叠加卫星加速度；
- 旧力悬挂移动：作为额外力保留，用于对照模式和发射飞行；
- M6 浅色虚线预测弹道：使用与运行时同一 `ABTSM9Gravity::GetSatelliteAcceleration` 查询；
- M6 预测枚举当前 M9 卫星球快照，并在每个积分步统一比较主星、所有卫星扩张球与显式 E5 OBB 的最早交点；以实际鸟碰撞半径在 `PrimarySurface / SatelliteBody / SatelliteE5` 的真实最早终点截断，不再画出穿月路径或受检查顺序影响。

引力查询公式仍是生产共享权威；预测器的固定步长与 Chaos 物理步进并不相同，因此不能宣称二者逐帧数值完全重合。隔离标定 context 会把认证的 0.04 s/30 s 输入域同步给 M6 预览与实际超时，但普通生产 M9 仍使用既有 M6 预算。验收目标是终点碰撞分类、偏转方向和玩家可读路径一致。

本期暂不向 M7 建筑块、HISM 动态代理或静态场景物体叠加卫星引力，以免改变已验收的建筑结算与回收时机；后续若扩展为完整双天体物理，必须让它们同样消费这个只读查询，而不能各自复制公式。

## 4. 编辑器操作与验收

1. 将球面地图的 GameMode Override 改为 `AABTSM9GameMode` 或其 Blueprint 子类。
2. M11.0 后卫星强制使用 `SatelliteWindow`；不要再通过历史 `Final Anchor Task Type` 把它改成 `LaunchSite`。可修改卫星大小/离地比例以及终局隔离参数。
3. 在 `ABTS | M9 | Gravity` 调整 `Satellite Surface Gravity Primary Ratio`；设为 `0` 可保留卫星视觉但关闭其引力。
4. PIE 后检查日志：

```text
[ABTS][M9] Satellite ready Task=... Cell=... Radius=1250.0 Clearance=1250.0 Gravity=245.0 LogicalSub=2 RenderSub=4 AngularSepDeg=... FinaleDistanceRatio=... LateralDot=... FinaleGravitySource=0
```

5. 观察 `SatelliteWindow` 上方有一颗纯灰色小球，且它远离 `LaunchSite` 终局施工台；卫星自身应无道路、水体、SDF 色块、树石 HISM、建筑或拾取物。
6. 在其附近进入 M6 拉弓，浅色虚线末段应向卫星偏折；发射鸟的实际轨迹应与预览同方向偏折。离卫星远时偏折快速减弱。
7. 预览判定 `SatelliteBody` 时不得继续穿过卫星，实飞应在相同一侧发生碰撞。隔离标定 context 中，月面画中画只能由当前卫星的 `SatelliteBody/SatelliteE5` 终点开启；它应使用与地面相同的相机距离、默认 `45°` 固定俯视和卫星径向 Up，且在背光面保持可读。非月面终点必须关闭月面画中画。

## 5. 排错

| 现象 | 原因与处理 |
| --- | --- |
| 没有卫星 | 检查 GameMode 是 M9；确认 PCG `SatelliteWindow` 与有效 FinaleFrame 均存在，并查看 `[M9] Satellite ready` / rejected 日志。 |
| 卫星仍贴近终局 | 检查 M3 GeneratorVersion、`SatelliteWindow ↔ LaunchSite` 实测角距以及 M9 的 `FinaleDistanceRatio/AlignmentDot` 拒绝日志；不要用历史 `FinalAnchorTaskType` 覆盖。 |
| 有卫星但弹道不偏折 | 检查 `Satellite Surface Gravity Primary Ratio` 是否为零，以及 M6 是否在正式球面模式而非 M7.1 平面测试台。 |
| 预测路径穿过卫星或比实飞晚碰撞 | 核对 M6 预览使用的实际鸟碰撞半径和 `TerminalType=SatelliteBody`；不要重新用可视 Bounds 猜半径。 |
| 月面画中画全黑、显示整颗卫星或错误开启 | 仅隔离标定 context 启用；先核对终点必须为当前卫星的 `SatelliteBody/SatelliteE5`，再检查 `LandingViewCameraDistanceCM`、`SatelliteLandingViewPitchDegrees`、径向 Up、ShowOnly 主体、持久 ViewState、隐藏预热帧和 T4-A2.4 v63 的 `GroundDay` 表面/深空背景分离合同。 |
| 近月镜头不切换或翻转 | 仅隔离标定 context 启用；核对 M6 Camera 的 Satellite phase、进入/退出滞回与目标 context。 |
| 卫星表面出现彩色地形/资源 | 这不属于 M9；确认生成的是 `AABTSM9Satellite`，而不是误放置的 `AABTSM3Planet`。 |

## 6. M9 开发调试选项

`AABTSM9GameMode` 的 `ABTS | M9 | Debug` 提供以下仅用于 PIE 验证的开关：

- `Enable Developer Walk`：开启后，四只鸟在普通行走模式下忽略树、石头等 HISM、M7 建筑块与地基、以及 M8 河道空气墙；连续球面地表和桥面仍保持碰撞托举。`Developer Walk Speed Multiplier` 默认为 `4.0`，只放大行走加速度与最高地面速度，不改变跳跃速度和 M6 发射初速度。
- `Allow Developer Any Cell Slingshot Stake Placement`：开启后，手持弹弓桩时可左键点击任意未占用的 `CellTopo` Cell 中心安装，无需 `SlingshotDirtHole`，也不检查该 Cell 是否为水域或可建造地。弹弓弦的同类型与 `MaxStakeArcRadians` 相邻规则继续生效。

PIE 日志应出现：

```text
[ABTS][M9][Debug] DeveloperWalk=1 Birds=4 SpeedMultiplier=4.0 AnyCellStake=1
[ABTS][M5.1][DebugStake] Installed=... Cell=... AllowAnyCell=1
```

## 7. M11 下游边界

M9 的一颗静止卫星和逆平方引力继续作为强化弹弓阶段的局部偏转练习。固定点质量只能改变轨迹方向，不能让原本受中心天体束缚的飞行获得永久净能量；M11 因而不会把三颗终局行星做成三个 `AABTSM9Satellite`。

M11 数据端固定为 `Primary + AssistPlanet1 + AssistPlanet2 + AssistPlanet3`。`EABTSM110FinaleGravityRole` 没有 Satellite 角色，`AABTSM9Satellite::IsM11FinaleGravitySource()` 恒为 `false`；终局预览/实飞均不得调用 M9 Gravity Query。前置隔离见 [M11.0](M110PreFinaleClosureDesign.md#7-m9-与-m11-的引力隔离)，终局算法见 [M11 三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md)。

## 8. 强化弹弓练习标定

生产 `SatelliteWindow` 不承担参数试验。卫星尺度、离地、局部背面 E5 立方体和表面引力先在不生成 M7/PCG 建筑的隔离 GameMode 中认证；旧 M9 地图只作为主星、角色、相机与 M6/M9 运行环境载体。隔离标定 V0 已冻结为独立原生 factory，不能写回生产 M9 的默认表面引力比 `0.25`；具体数值与身份只记录在标定详稿，避免本生产总稿混淆两套用途。

M3/M7 只能跨地图/Seed 消费可移植的 `LaunchProfileHash`、射程包络与 `SatellitePracticePresetHash`。`GravitySnapshotHash` 含本次场景解析出的卫星相对向量，只是 baseline scene-instance 证据；它不是稳定 Catalog 身份。标定固定步长积分器只用于确定性认证和 M3R-4.1 预筛，也不替代生产 `ABTSM9Gravity::GetSatelliteAcceleration` 与实际 M6 预览/实飞链路。

本轮还包含一项与标定入口分离的生产修复：`AABTSM9GameMode` deferred spawn 在 Finish 前已配置卫星原生 Root，`FinishSpawningActor` 必须继续使用原始 `FTransform::Identity`，随后由 `IsAtConfiguredCenter()` fail closed，避免同一平移被组合两次。隔离标定 smoke 不能替代该回归；必须另跑 [标定详稿第 7.3 节](M6M9SlingshotSatelliteCalibrationDesign.md#73-生产-m9-deferred-transform-回归) 的生产 M9 门，并核对默认 `Radius=1250.0 Clearance=1250.0 Gravity=245.0`、`FinaleGravitySource=0` 以及唯一 ready 日志。故障特征亦收录于[开发排错记录](DevelopmentTroubleshooting.md#12-m9-卫星与标定入口)。

完整入口、离散玩家可达 Pull × `AimPlaneOffsetCM` 网格、表面 E5 立方体、碰撞一致预测、月面画中画、卫星相对镜头和验收命令见 [M6/M9 弹弓与卫星标定模式](M6M9SlingshotSatelliteCalibrationDesign.md)。fresh 6/6 自动化、标定 runtime smoke、生产 M9 回归及可见 PIE 已为参数 V0 留证；其可移植身份由原生 factory 提供给 M3。卫星实飞主镜头仍延期到[统一镜头视觉优化](ABTSCameraVisualOptimizationDesign.md)，不属于本次参数冻结。
