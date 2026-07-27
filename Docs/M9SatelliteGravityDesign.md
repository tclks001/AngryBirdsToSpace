# M9：卫星与局部引力

> 状态：已实现，待 PIE 验收。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M2 球面基础](M2PlanetSurfaceDesign.md) · [Chaos 刚体移动](ChaosRigidBodyMovementDesign.md) · [M6 发射与碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M10.1 超视距目标与引力走廊](M101BeyondHorizonLaunchInterfaceDesign.md) · [Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md)。

## 1. 范围与职责

M9 生成一颗位于主线最终任务附近的小卫星，并为鸟的移动/弹道提供局部引力：

```text
TaskGraph 最终 Task 的 Seed Cell（逻辑锚点）
  -> 主星连续表面查询（仅取得表现位置/法线）
  -> AABTSM9Satellite 独立灰色球面
  -> M9 Gravity Query
  -> Chaos 鸟 / 历史力悬挂鸟 / M6 预测弹道
```

卫星不读取或修改主星的道路、水网、Cell 状态、SDF、HISM、建筑和资源。它有自己的轻量 `CellTopo`，但此阶段不在其上生成任何 Task、材料或建筑；其 CellTopo 仅满足“卫星本身也是可扩展球面实体”的拓扑基础。

## 2. 默认空间与拓扑规格

在 `AABTSM9GameMode` 中，默认锚点为 `LaunchSite`，即当前主线最终 Task。它的 Seed Cell 是唯一的逻辑位置源；主星 SDF/连续网格不参与“最终任务在哪里”的判定。

| 项目 | 默认值 | 含义 |
| --- | ---: | --- |
| 卫星逻辑细分 | Sub=2 | 独立 `AABTSM2Planet` CellTopo。 |
| 卫星渲染细分 | Sub=4 | 独立 ProceduralMesh 连续表面。 |
| 半径 | 主星半径 × 1/8 | `Satellite Radius Primary Ratio=0.125`。 |
| 卫星球心离主星任务地表距离 | 主星半径 × 1/8 | `Satellite Center Clearance Primary Radius Ratio=0.125`。 |
| 材质 | 纯灰色 | 无 SDF、无地形分层、无 HISM。 |

“球心离地面距离”从最终 Seed Cell 对应的主星 `QuerySurface` 结果沿局部径向法线量取。因此即使主星有 PCG 高度变化，卫星仍和最终地表保持配置的几何间距。

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
- M6 浅色虚线预测弹道：使用与运行时同一 `ABTSM9Gravity::GetSatelliteAcceleration` 查询。

本期暂不向 M7 建筑块、HISM 动态代理或静态场景物体叠加卫星引力，以免改变已验收的建筑结算与回收时机；后续若扩展为完整双天体物理，必须让它们同样消费这个只读查询，而不能各自复制公式。

## 4. 编辑器操作与验收

1. 将球面地图的 GameMode Override 改为 `AABTSM9GameMode` 或其 Blueprint 子类。
2. 在 `ABTS | M9 | Placement` 保持默认 `Final Anchor Task Type=LaunchSite`；可修改三项比例调整卫星大小和位置。
3. 在 `ABTS | M9 | Gravity` 调整 `Satellite Surface Gravity Primary Ratio`；设为 `0` 可保留卫星视觉但关闭其引力。
4. PIE 后检查日志：

```text
[ABTS][M9] Satellite ready Task=... Cell=... Radius=1250.0 Clearance=1250.0 Gravity=245.0 LogicalSub=2 RenderSub=4
```

5. 观察 `LaunchSite` 附近上方有一颗纯灰色小球；应无道路、水体、SDF 色块、树石 HISM、建筑或拾取物。
6. 在其附近进入 M6 拉弓，浅色虚线末段应向卫星偏折；发射鸟的实际轨迹应与预览同方向偏折。离卫星远时偏折快速减弱。

## 5. 排错

| 现象 | 原因与处理 |
| --- | --- |
| 没有卫星 | 检查 GameMode 是 M9；确认 PCG `LaunchSite` 存在并查看 `[M9] Satellite ready` / rejected 日志。 |
| 卫星位置不在终局附近 | 检查 `Final Anchor Task Type`，默认应为 `LaunchSite`；不要用连续表面坐标手工决定任务锚点。 |
| 有卫星但弹道不偏折 | 检查 `Satellite Surface Gravity Primary Ratio` 是否为零，以及 M6 是否在正式球面模式而非 M7.1 平面测试台。 |
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

M9 的一颗静止卫星和逆平方引力继续作为普通球面关卡的局部偏转基线。固定点质量只能改变轨迹方向，不能让原本受中心天体束缚的飞行获得永久净能量；M11 因而不会把三颗终局行星做成三个 `AABTSM9Satellite`。终局采用独立普通 Actor、固定布局、虚拟公转动量与同源确定性求解器，详见 [M11 三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md)。
