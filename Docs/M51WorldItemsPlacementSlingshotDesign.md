# M5.1：世界物品、手持放置与弹弓装配

> 状态：C++ 机制已实现，使用 Engine 基础形体作为资产回退，等待编辑器视觉与操作验收。
>
> 本阶段只实现基础物品刷新/自动拾取、手持栏、工作台/熔炉放置、弹弓槽、桩与弦的装配规则。真实资产、建筑模块和弹射行为仍属于后续阶段。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M5 背包/加工](M5InventoryCraftingImplementationDesign.md) · [UI 系统](UISystemDesign.md) · [M5.2 碰撞与移动](M52CollisionAndMovementDesign.md) · [M6 弹弓发射](M6SlingshotLaunchAndImpactDesign.md)

## 1. 逻辑源约束

- TaskGraph 的 `SlingshotRange` Task 决定弹弓槽所在区域。
- 每个该 Task 使用 Seed Cell 和一个同 Task 的直接邻居 Cell 生成一对 DirtHole。
- 平地放置只接受 CellTopo 中 `bBuildable && !bWater && 未占用` 的 Cell，落点固定为 Cell 中心。
- 连续球面只把 Cell 方向转换为可见位置和法线，不负责决定合法性。
- 弹弓桩相邻使用两个 Cell 单位方向的球面夹角，不使用欧氏世界距离。
- 资源 SDF 只在 CellTopo 中心采样并决定生成概率，世界 Mesh 不持有资源状态。

## 2. 手持栏

底部 HUD 由左到右为：

```text
[BAG] [快捷栏 1..8]   [HELD]
```

- 点击已有快捷栏物品，将它设为当前手持物。
- 点击空快捷栏格打开背包/加工界面。
- 点击 `BAG` 或按 K 打开背包/加工界面。
- `HELD` 独立显示当前手持物与剩余数量；点击它清空手持。
- 物品因放置消耗到零时自动清除手持状态。

## 3. 世界基础物品

M5.1 不再注入测试库存。世界生成使用三组以 Start Task 内 Cell 为中心的确定性低频球面资源斑块，因此不同 WorldSeed 下出生区域仍具备首轮树枝、石料与植物纤维：

```text
AngularDistance = acos(max(dot(CellDirection, ResourceCenter_i)))
SignedDistance = AngularDistance - ResourcePatchRadiusRadians
Eligible = SignedDistance <= BoundaryJitter
```

随后依据 Cell 的地形和湿度选择物品：

- 树枝斑块中少量生成木材，以便尚未实现建筑掉落的本阶段仍可验收熔炉组件；
- 高湿度 Cell 优先植物纤维；
- 其余合法 Cell 生成石料。

玩家当前主控鸟进入 `AutoPickupRadiusCM` 后，物品直接加入四鸟共享库存并销毁世界 Actor。拾取不要求点击。

## 4. 工具放置

工作台组件和熔炉组件属于可放置工具。

1. 从快捷栏点击工具，使其进入 HELD。
2. 左键点击地面。
3. 系统取鼠标命中的球面方向，并选择最近的合法 CellTopo Cell。
4. 站点生成在该 Cell 中心，姿态使用连续表面法线表现。
5. 只有生成成功后才扣除一个组件。
6. 每个 Cell 只能占用一次。

站点仍只阻挡 Visibility 点击查询，不阻挡鸟的 Pawn Sweep，避免再次出现靠近建筑后无法移动。

## 5. 弹弓桩与弦

### 5.1 桩

- 简易/强化弹弓桩只能点击空 DirtHole 安装，不能点击普通地面自由放置。
- DirtHole 记录 CellId 和已安装桩。
- 桩安装成功后扣除一个对应物品。

### 5.2 弦

- 简易弦只能连接两个简易桩，强化弦只能连接两个强化桩。
- 玩家手持弦点击第一根桩时只记录选择，不消耗物品。
- 点击第二根同类、未连接桩后，计算：

```text
ArcRadians = acos(dot(StakeA.UnitDirection, StakeB.UnitDirection))
```

- `ArcRadians <= MaxStakeArcRadians` 时生成弦并扣除一个物品；默认阈值 `0.12 rad`。
- 不满足类型/弧度条件时不消耗，第二次点击的桩成为新的首选桩。
- 两根已经连接的桩不能再次连接。

## 6. 编辑器步骤

1. 复制 `L_ABTS_M5` 为 `L_ABTS_M51`，或直接在当前测试地图中切换 GameMode。
2. World Settings 的 `GameMode Override` 设置为 `ABTSM51GameMode`。
3. 不要手工放置工作台、熔炉、DirtHole 或拾取物。
4. 保存并重新打开编辑器，以刷新新增 C++ 类与输入映射。
5. 当前占位表现：拾取物为小球、DirtHole 为扁圆柱、桩为细长圆柱、弦为细长方条、站点为方块。

## 7. 验收清单

### 7.1 初始化

日志应包含：

```text
[ABTS][M5][Inventory] ... PrototypeSeed=0
[ABTS][M5.1][SlingshotSlots] Holes=2
[ABTS][M5.1][PickupPCG] Spawned=... PatchRadiusRad=...
[ABTS][M5.1] World ready ...
```

地图有多个 SlingshotRange Task 时 DirtHole 数量为每 Task 两个。

### 7.2 拾取与手持

1. 开局背包为空。
2. 靠近基础物品后自动消失并加入快捷栏。
3. 点击已有快捷栏物品，HELD 显示同一物品。
4. 点击 HELD 后清空，不改变库存数量。

### 7.3 放置与装配

1. 加工获得工作台/熔炉组件后可放在平缓 Cell 中心。
2. 水域、陡坡或已占用 Cell 不允许放置，且不扣物品。
3. 普通地面点击不能放置弹弓桩。
4. 点击 DirtHole 可安装手持同类桩。
5. 手持弦依次点击同类相邻两桩后生成连接条并消耗一根弦。
6. 类型不同或角距过大时不生成也不消耗。
7. 站点和弹弓占位物不应卡住角色移动。

## 8. 资产接入接口

正式资产可通过派生 Blueprint 替换以下 Class 属性，不改变规则：

- `PickupClass`
- `DirtHoleClass`
- `StakeClass`
- `CordClass`
- `CraftingStationClass`

所有正式模型必须检查 Pivot、局部 +Z/前向、Visibility 点击碰撞与 Pawn 响应。
