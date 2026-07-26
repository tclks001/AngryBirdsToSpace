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
  -> 手持 Bridge Kit 点击 BridgeSite
  -> 移除该水边空气墙并生成可碰撞桥面
```

本阶段不做材料掉落 Actor、桥梁耐久/破坏、游泳、任意河段架桥、任务完成 UI 或存档。树石 HISM 的破坏掉落仍沿用后续资源系统扩展；本期“自动回收”严格覆盖 M7 木、石、铁、玻璃砖的实际销毁。

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
- 只有 `Crossing=BridgeSite` 的未建边是合法桥位。桥不是自由放置在连续水面上的 Actor；玩家的鼠标方向会映射到最近 `BridgeSite` 边，超过 `Max Bridge Placement Snap Degrees` 即拒绝。
- 放置成功后记录该 `FABTSM3CellEdgeKey` 为已建，关闭同一边的空气墙，并生成带 `BlockAll` 碰撞的简易立方体桥面。桥的长轴沿两侧 Cell 中心的切线方向，局部 Up 为桥位径向方向。

这意味着水体玩法与 TaskGraph 的 `BridgeBuilt` 可达性校验一致，不会从 SDF 颜色、地表网格或物理命中反推水域逻辑。

## 4. 编辑器与验收

1. 正式球面地图在 World Settings 使用 `AABTSM8GameMode`（或其 Blueprint 子类），并保留 `AABTSM3Planet` 的 TaskGraph 生成。
2. 为快速验收，在 GameMode 的 `ABTS | M8 | Debug` 勾选 `Seed Debug Maximum Inventory`；所有已知物品以 `Debug Maximum Inventory Quantity`（默认 99）进入背包，且 `Bridge Kit` 排在工具栏首位。
3. 正常流程下，红鸟靠近工作台，用 `Wood ×6 + Stone ×2` 制作 `Bridge Kit`；在 HUD/背包点击该物品使其成为手持物。
4. 沿道路走到 TaskGraph 的 `BridgeSite` 河道关口（可使用 M7 位置调试经纬度辅助）；对河道处点击左键。只在桥位附近、且角色在 `Bridge Placement Reach CM` 内时成功。
5. 观察成功后出现简易桥面、该处空气墙失效，鸟可沿桥穿过；其他河道边仍阻挡通行。
6. 发射撞碎 M7 木/石/铁/玻璃砖，检查 HUD 数量立即增加且日志存在：

```text
[ABTS][M8][Recovery] Material=... Quantity=...
[ABTS][M8][Recovery] Added Item=... Quantity=...
[ABTS][M8][Bridge] Built CellA=... CellB=... Span=...cm
```

## 5. 可调参数与排错

| 现象 | 检查 |
| --- | --- |
| 开局没有空气墙/桥位 | 确认 GameMode 为 M8，日志有 `[ABTS][M8] Ready`；PCG 必须已生成 `BridgeSite`。 |
| 点击河道无法放桥 | 手持物必须为 `Bridge Kit`，目标必须接近唯一未建 `BridgeSite`，并位于 `Bridge Placement Reach CM` 内；普通 Stream/DeepRiver 是故意拒绝的。 |
| 桥放好仍过不去 | 检查桥位是否是 `BridgeSite`，以及日志的 CellA/CellB；可适度增大 `Bridge Deck Width CM` 或 `Barrier Height CM` 仅用于物理表现调试，不能将普通河道改为可通行。 |
| 砖块消失但背包未增加 | 确认它是 M7 Brick 而非树石 HISM、绳/链或装置；仅进入 Chaos 不算销毁，必须看到 M7 的实际 Break 日志。 |
