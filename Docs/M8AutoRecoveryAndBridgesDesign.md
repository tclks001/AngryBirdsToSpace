# M8：自动回收与桥梁

> 状态：已实现，待 PIE 验收。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [M5 背包与加工](M5InventoryCraftingImplementationDesign.md) · [M5.1 世界放置](M51WorldItemsPlacementSlingshotDesign.md) · [M7 材料与装置](M7BuildingMaterialsAndDevicesDesign.md) · [M7 球面建筑集成](M7TaskGraphSphericalBuildingIntegrationDesign.md)。

## 1. 范围

M8 补齐“破坏建筑 → 获得材料 → 制作桥梁 → 跨过受河网控制的关口”的最小闭环：

```text
M7 砖块实际销毁
  -> 共享背包直接增加对应材料
  -> 工作台制作 Bridge Kit
  -> 手持 Bridge Kit 点击任意权威河流线段
  -> 生成语义朝向的可碰撞桥面；若该边有空气墙则同步开放
```

本阶段不做材料掉落 Actor、桥梁耐久/破坏、游泳、任务完成 UI 或存档。树石 HISM 的破坏掉落仍沿用后续资源系统扩展；本期“自动回收”严格覆盖 M7 木、石、铁、玻璃砖的实际销毁。

## 2. 自动回收

`AABTSM7BuildingMaterialSystem` 只在一个砖块真正从 HISM 或动态 `AABTSM7BuildingModule` 移除时广播 `OnMaterialRecovered`。仅被撞飞、提升为 Chaos 动态模块、被冻结都不会回收，因而不会复制材料。

`AABTSM8RecoveryBridgeSystem` 订阅该事件并写入共享 `UABTSInventoryComponent`：

| M7 建筑材质 | 背包物品 |
| --- | --- |
| 木 | `Wood` |
| 石 | `Stone` |
| 铁 | `MetalParts` |
| 玻璃 | `Glass` |

默认每块回收 1 个；`Recovery Quantity Per Destroyed Brick` 可在 M8 System 上调整。`Glass` 是本期新增的可显示背包材料，暂未加入其他配方。

## 3. 水边、空气墙与桥

连续球面只提供墙/桥的渲染和碰撞变换；是否阻断、是否可建桥完全读取 `CellTopo` 的 `FABTSM3CellEdgeState`：

- 每条 `bBlocksOnFoot=true` 边生成一个不可见、可碰撞的空气墙；它们形成 Hydrology 生成的闭合河网关口，不能绕行。
- 所有 `Water != None` 的未建边都可以架桥；`Crossing=BridgeSite` 仍是 TaskGraph 认证过的主线推荐过河点，但不再是唯一合法位置。
- M8 直接复用 M3 的河流表现语义：自然水流使用 Cell 中心到中心的流向线，阻断河使用 Voronoi 对偶边。鼠标映射到可见河流线段上的最近点，允许范围为该水型的视觉半宽加 `Bridge Placement Aim Tolerance CM`。
- 桥的长轴始终垂直于上述可见河流切线，局部 Up 来自实际落点的球面法线；跨度由河宽和 `Bridge Bank Overlap CM` 决定，不再取逻辑边中点或 Cell 间距。
- 放置成功后记录该 `FABTSM3CellEdgeKey` 为已建。若该边是阻断河，关闭同 Key 空气墙；普通流向河没有空气墙，只生成可通行桥面。

这意味着架桥仍只消费 `GeneratedEdgeStates` 权威数据，不从 SDF 颜色或碰撞材质反推水域；同时桥位与 M3 实际绘制河流使用同一线段构建器。生成器的唯一 `BridgeSite`/`BridgeBuilt` 主线可达性契约保持不变，自由架桥只是运行时额外通路。

## 4. 编辑器与验收

1. 正式球面地图在 World Settings 使用 `AABTSM8GameMode`（或其 Blueprint 子类），并保留 `AABTSM3Planet` 的 TaskGraph 生成。
2. 为快速验收，在 GameMode 的 `ABTS | M8 | Debug` 勾选 `Seed Debug Maximum Inventory`；所有已知物品以 `Debug Maximum Inventory Quantity`（默认 99）进入背包，且 `Bridge Kit` 排在工具栏首位。
3. PIE 中按 `F7` 打开开发调试显示：绿色粗线/球/箭头表示认证 `BridgeSite`；鼠标附近最近的未建河段会显示一条跨河横线，黄色表示当前可放、橙色表示瞄准距离超限，文字显示 `FLOW/BARRIER`、Cell Key 和距离。再次按 `F7` 关闭；也可用 `-ABTSM3R5LogicRegions` 随 PIE 启动。
4. 正常流程下，红鸟靠近工作台，用 `Wood ×6 + Stone ×2` 制作 `Bridge Kit`；在 HUD/背包点击该物品使其成为手持物。
5. 走到任意河段附近，把鼠标移到水面；确认 F7 黄色横线垂直跨越河流后点击左键。只在河流视觉半宽加容差内、且角色在 `Bridge Placement Reach CM` 内时成功。
6. 分别验收普通 `FLOW` 河段与 `BARRIER` 河段：前者生成桥面，后者还应关闭同 Key 空气墙；其他未架桥的阻断边继续阻挡。
7. 发射撞碎 M7 木/石/铁/玻璃砖，检查 HUD 数量立即增加且日志存在：

```text
[ABTS][M8][Recovery] Material=... Quantity=...
[ABTS][M8][Recovery] Added Item=... Quantity=...
[ABTS][M8][BridgeDebug] Ready=1 Enabled=1 Shortcut=F7 WaterEdges=... CertifiedBridgeSites=... UnbuiltCertified=...
[ABTS][M8][Bridge] Rejected Reason=... NearestWaterEdge=(...,...) WaterType=... Semantic=... AimDistanceCM=... AllowedAimDistanceCM=... PlayerDistanceCM=... ReachCM=... AimPoint=... HitActor=... HitComponent=...
[ABTS][M8][Bridge] Built CellA=... CellB=... WaterType=... Semantic=... CertifiedSite=... AimDistanceCM=... Span=... BarrierOpened=...
```

## 5. 可调参数与排错

| 现象 | 检查 |
| --- | --- |
| 开局没有空气墙/桥位 | 确认 GameMode 为 M8，日志有 `[ABTS][M8] Ready`，且 `WaterEdges>0`；认证主线还应有 `CertifiedBridgeSites>0`。 |
| 点击河道无法放桥 | 按 `F7` 观察最近横线；手持物必须为 `Bridge Kit`。`NotOverWater` 对比 `AimDistanceCM/AllowedAimDistanceCM`，`OutOfReach` 对比 `PlayerDistanceCM/ReachCM`。同一离散水边已建桥后不会重复选择。 |
| 桥放好仍过不去 | 检查成功日志的 `Semantic` 与 `BarrierOpened`。`BarrierDualEdge` 应为 `BarrierOpened=1`；`FlowCenterline` 本来没有空气墙。可调 `Bridge Deck Width CM`、`Bridge Bank Overlap CM` 或 `Barrier Height CM` 做物理表现校准。 |
| 砖块消失但背包未增加 | 确认它是 M7 Brick 而非树石 HISM、绳/链或装置；仅进入 Chaos 不算销毁，必须看到 M7 的实际 Break 日志。 |
