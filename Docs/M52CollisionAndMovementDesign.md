# M5.2：碰撞与移动效果优化

> 状态：C++ 已实现；需要在 M3/M5 地图中进行 PIE 视觉手感验收。

## 1. 目标与边界

M5.2 让 M3 的树木、岩石 HISM 具备静态查询碰撞；它们在 M6 发射模式实现前不模拟刚体、不移动、也不产生破坏或掉落。鸟维持受控的径向力模型，不切换为 Chaos 刚体；但角色胶囊将真实 Sweep 连续地表、HISM 与之后的建筑碰撞，从而避免高处下落穿进地形后再被悬挂弹簧推出。

CellTopo/TaskGraph 仍是逻辑源。SDF 材质只负责 GPU 表现，碰撞不会读取像素颜色；CPU 使用同一份 `FABTSM3TerrainVisualField` 的道路、河流、地形特征线段复算 SDF 权重。

## 2. 接触链路

```text
力模型（重力、输入、空气阻力）
  -> Velocity
  -> Capsule Sweep（连续地形 / HISM / 建筑）
  -> 命中法线移除朝内速度 + 二次滑动 Sweep
  -> ImpactPoint 的 UnitDirection
  -> QuerySurfacePhysics
  -> 地形/道路/河流 SDF 权重
  -> 插值 GroundDragPerSecond 与 Restitution
```

`UABTSRadialSurfaceSuspensionComponent` 保留接地迟滞、贴地稳定和跳跃后短暂禁用支撑，但不再承担穿透恢复职责。地形 Sweep 命中才是位置接触的权威。悬挂的 `bSupportActive` 可在接近地面时提前开始阻尼，但只有 `bGrounded=true` 且下落速度不超过 `MaxGroundFollowDescentSpeedCMPerSec` 时才允许高度跟随；跳跃落地不能被提前吸附到地面。

## 3. CPU SDF 物理采样

`AABTSM3Planet::QuerySurfacePhysics` 输出：最近逻辑 Cell、两类地形的线段 SDF 插值权重、道路权重、河流权重、连续表面法线、最终阻力与恢复系数。

物理混合宽度 `SurfacePhysicsBlendWidthCM` 独立于 `TerrainBlendWidthCM`。前者控制脚感和反弹过渡，后者只控制颜色；默认同为 240cm，但美术调整颜色时不会无意改变移动手感。

最终参数按以下顺序合成：

1. 两个最近地形特征按 SDF 权重插值；
2. 道路 SDF 覆盖混合道路参数；
3. 河流 SDF 最后混合河流参数。

默认配置中，森林阻力较大、山体阻力较低且恢复系数较高、道路较顺滑、河流带阻力最高。所有值均在 `BP_ABTSM3Planet` 的 `ABTS|M5.2|Physics` 分类中暴露。

## 4. 空气阻力

`AirDragPerSecond` 为常驻阻力，始终沿速度反方向作用，包含径向跳跃速度与切向空中速度。原有 `AirTangentDragPerSecond` 仍负责额外的空中切向控制阻尼。这样持续按方向键时速度会收敛，不会在长时间滞空中无上限加速。

## 5. HISM 碰撞规则

`ForestHISM` 与 `RockHISM`：

- `QueryOnly`，Object Type 为 `WorldStatic`，各通道 Block；
- 不启用 `SimulatePhysics`；
- 保留为 HISM，不在 M5.2 将每个实例变为 Actor；
- 未来 M6 命中后，才允许按实例索引隐藏实例并转为对象池中的动态破坏代理。

鸟群彼此仍显式忽略；工作台和熔炉应继续使用“点击/放置/发射可命中、鸟 Ignore”的交互碰撞 Profile，不能把它们设为鸟的静态阻挡物。

## 6. 编辑器与验收

1. 关闭 PIE，编译并重开 Editor；打开使用 `ABTSM3Planet` 与 ForceSuspension 的 M5 地图。
2. 选择 `BP_ABTSM3Planet`，确认 `ForestHISM`/`RockHISM` 的 Collision Enabled 显示为 Query Only。编辑器中 HISM 所使用 StaticMesh 必须有 Simple Collision；若资源没有，请在 Static Mesh Editor 生成/导入合适的凸包碰撞。
3. 在 `ABTS | M5.2 | Physics` 调整不同地表的 `GroundDragPerSecond` 与 `Restitution`；推荐先保留默认值。
4. 让鸟从高坡跳下：胶囊应在地表命中时停止/反弹，不可先进入 Mesh 再被推出。
5. 分别在草地、山地、道路和河流带移动：速度收敛、滑动和轻微反弹应可随区域改变且过渡连续。
6. 持续在空中按住移动键：速度应因空气阻力收敛。
7. 撞向树和岩石：鸟不穿过实例；树石不移动、不掉落、不被破坏。

## 7. 排错

| 现象 | 检查 |
| --- | --- |
| 树/石可见但仍可穿过 | 检查 Static Mesh 是否有 Simple Collision；HISM QueryOnly 不会为无碰撞网格自动生成形状。 |
| 鸟撞树后不能移动 | 检查树/石没有错误设置为 `PhysicsBody`；鸟群仍应 Ignore Pawn。移动应使用 Sweep-and-Slide，不应启用鸟的 Simulate Physics。 |
| 落地仍穿透 | 确认 Force 模式没有再把 `ContinuousSurface` 加入 Ignore 列表；检查 HISM/地形碰撞是否已完成重建。 |
| 道路视觉变化但脚感不变 | 调整 `SurfacePhysicsBlendWidthCM` 与 Road Physics，而非 `TerrainBlendWidthCM`。 |
