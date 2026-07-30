# M5.1：世界物品、手持放置与弹弓装配

> 状态：兼容 TaskGraph 槽、M11.0 Space 槽及通用月度槽快照消费端已实现；M6 三维连弦与失败原子状态已通过自动化和兼容世界 PIE。R3.1 月度实体槽仍等待 R4/R6 选出唯一 Candidate 后再做六关联合 PIE。
>
> 本阶段只实现基础物品刷新/自动拾取、手持栏、工作台/熔炉放置、弹弓槽、桩与弦的装配规则。真实资产、建筑模块和弹射行为仍属于后续阶段。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M5 背包/加工](M5InventoryCraftingImplementationDesign.md) · [UI 系统](UISystemDesign.md) · [M5.2 碰撞与移动](M52CollisionAndMovementDesign.md) · [M6 弹弓发射](M6SlingshotLaunchAndImpactDesign.md) · [M11.0 终局前置收口](M110PreFinaleClosureDesign.md)

## 1. 逻辑源约束

- 当前首周兼容世界继续由 TaskGraph 的 `SlingshotRange` Task 决定普通弹弓槽所在区域；每个该 Task 使用 Seed Cell 和一个同 Task 的直接邻居 Cell 生成一对普通 DirtHole。
- 月度世界只允许消费 `FABTSM51OrdinarySlingshotSlotSnapshot`：它仅包含 `LayoutHash / CandidateHash / SlotGroups / MaxCordLengthCM`，不包含 M3 原始候选数组、Encounter/Field 配对权限或 UObject 引用。快照必须在 WorldSystem `BeginPlay` 前显式配置并整体通过校验；身份缺失、组内槽不足、Cell 重复或越界时生成计划为空，且不得静默回退 TaskGraph。
- 当前 R3.1 的三个 `RetainedCandidates` 都未被月度世界接受，生产入口不会构造上述快照，也不得读取 `[0]`。R4/R6 冻结唯一 Candidate 和最终 `LayoutHash` 后，再补 M3 导出与生产入口自动配置。
- 正式绑定时 WorldSystem 必须 deferred spawn，并在 `FinishSpawning` 前注入快照；生产者还必须把快照 `LayoutHash/CandidateHash` 与活动月度世界的已接受身份逐项核对。本轮非零身份/结构校验不替代该生产一致性门。
- M11.0 起，唯一 `LaunchSite` 另生成且只生成一对 Space-only 槽；左右槽共享同一个认证 AnchorCell 和 PairId，在同一平整、非水、未占用施工台内以 `210cm` 世界中心距相邻摆放，不能由普通 `SlingshotRange` 数量推导。
- 平地放置只接受 CellTopo 中 `bBuildable && !bWater && 未占用` 的 Cell，落点固定为 Cell 中心。
- 连续球面只把 Cell 方向转换为可见位置和法线，不负责决定合法性。
- 普通弦只使用两桩顶部端点的世界空间厘米距离门；兼容世界默认 `1200cm`，月度世界使用快照的 `MaxCordLengthCM`。Field/Encounter/槽组身份不限制两根普通桩能否连接。
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
- M10 起，手持一个树枝点击空 DirtHole 会直接安装 `Twig` 树枝桩；它与简易桩是不同配对类型，不需要先加工。
- M11.0 起，`EABTSItemId::SpaceStake`（太空弹弓桩）只能安装到 `LaunchSite` 的 Space-only 槽；普通桩点击 Space-only 槽、太空桩点击普通槽都必须拒绝且不扣库存。
- DirtHole 记录 CellId、槽类型和已安装桩；槽类型是 Gameplay 数据，不能从颜色、Actor 名称或所在位置反推。
- 桩安装成功后扣除一个对应物品。

### 5.2 弦

- 简易弦只能连接两个简易桩，强化弦只能连接两个强化桩。
- M10 起，植物纤维作为 `Twig` 弦材料，只能连接两个树枝桩；仍沿用“先选第一桩、再点第二桩”的两次点击规则。
- M11.0 起，`EABTSItemId::SpaceCord`（太空弹弓弦）只能连接同一 Space-only 槽对中的两根 `SpaceStake`；成功后生成 `EABTSSlingshotTier::Space`。
- 玩家手持弦点击第一根桩时只记录选择，不消耗物品。
- 点击第二根同类、未连接桩后，先构造无副作用的三维连接查询：

```text
Candidate = Segment(StakeA.VisualTop, StakeB.VisualTop)
LengthCM <= ActiveMaxCordLengthCM
Distance(Candidate, ThirdStakeCenterLine)
  > CandidateCordRadius + ThirdStakeRadius + Clearance
Distance(Candidate, ExistingCordSegment)
  > CandidateCordRadius + ExistingCordRadius + Clearance
```

- 所有向量、长度和半径必须是有限值，候选弦与既有弦不得退化；交叉、接触或恰好落在净空边界上均拒绝。实现使用 `FMath::SegmentDistToSegmentSafe`，不依赖 NoCollision 表现 Mesh 的 LineTrace。
- 类型、长度或障碍检查均在生成 Actor、扣库存和写入 `HasCord` 前完成。只有 Cord Actor 生成且 `RemoveItem` 成功后才一次提交两端状态；任一步失败均保持库存、有效 Cord 数及两端 `HasCord` 不变。
- 不满足条件时，第二次点击的合法桩成为新的首选桩；槽组、FieldId 与 EncounterId 不参与配对判断。
- 两根已经连接的桩不能再次连接。

## 6. 编辑器步骤

1. 复制 `L_ABTS_M5` 为 `L_ABTS_M51`，或直接在当前测试地图中切换 GameMode。
2. World Settings 的 `GameMode Override` 设置为 `ABTSM51GameMode`。
3. 不要手工放置工作台、熔炉、普通 DirtHole、Space-only 槽或拾取物。
4. 保存并重新打开编辑器，以刷新新增 C++ 类与输入映射。
5. 当前占位表现：拾取物为小球、DirtHole 为扁圆柱、桩为细长圆柱、弦为细长方条、站点为方块。

## 7. 验收清单

### 7.1 初始化

日志应包含：

```text
[ABTS][M5][Inventory] ... PrototypeSeed=0
[ABTS][M5.1][OrdinarySlots] Source=CompatibilityTaskGraph Accepted=1 Holes=... MaxCordLengthCM=1200 ...
[ABTS][M11.0][SlingshotSlots] Standard=... Finale=2 Pair=... AnchorCell=...
[ABTS][M5.1][PickupPCG] Spawned=... PatchRadiusRad=...
[ABTS][M5.1] World ready ...
```

兼容地图有多个 `SlingshotRange` Task 时普通 DirtHole 数量为每 Task 两个。未来月度模式的 `Source=AcceptedSnapshot` 必须为快照中的每个 Slot Cell 精确生成一个 DirtHole，任何一项失败都回滚本批普通槽；无论普通槽来源为何，全图仍只能有一对由唯一 `LaunchSite` 生成的 Space-only 槽。

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
5. 手持弦依次点击同类两桩；世界距离不超过活动上限且不穿过第三桩/既有弦时，生成连接并消耗一根弦。
6. 类型不同、超长、相交、接触、近失配或无效几何时不生成也不消耗，失败前后两端 `HasCord` 不变。
7. 站点和弹弓占位物不应卡住角色移动。
8. 太空桩只能安装到 Space-only 槽；普通桩与太空桩交叉尝试均拒绝且不扣库存。
9. 两根太空桩只能由一根太空弦连接，完成后弹弓档位为 `Space`，且不能生成第二套终局弹弓。

### 7.4 自动化门

必须在 fresh `UnrealEditor-Cmd -NullRHI` 进程中精确通过：

- `ABTS.M51.SlingshotAssembly.Geometry`：最大长度边界、超长、第三桩、既有弦、近失配、高度差、NaN/Inf 和退化段；
- `ABTS.M51.SlingshotAssembly.Runtime`：快照结构/Cell 计划、普通成功、超长/第三桩/既有弦失败原子状态，以及 M11.0 Space Pair 回归；
- `ABTS.M51.OrdinarySlots.Runtime`：接受快照的实际 DirtHole 数、最大弦长发布、初始化幂等、终局双槽隔离，以及无效 Cell 的普通槽全批回滚。

这三个测试通过只证明通用消费端和装配规则；R3.1 月度实体槽仍需等待唯一 Candidate 导出后做 M3R-6/R-7 Visible PIE。

### 7.5 集成 PIE 结论

2026-07-30，用户已在集成工作树完成本轮 M5.1/M6 兼容世界可见 PIE，结论通过。

该结论验收的是通用消费端和兼容世界，不代表 R3.1 月度实体槽已经生成；后者仍须等待 R4/R6 唯一 Candidate 导出并完成六关联合 PIE。

## 8. 资产接入接口

正式资产可通过派生 Blueprint 替换以下 Class 属性，不改变规则：

- `PickupClass`
- `DirtHoleClass`
- `StakeClass`
- `CordClass`
- `CraftingStationClass`

所有正式模型必须检查 Pivot、局部 +Z/前向、Visibility 点击碰撞与 Pawn 响应。

`LaunchSite` 槽对、太空桩/弦配方、局部坐标系及 M11 门控的完整现行合同见 [M11.0 终局前置收口](M110PreFinaleClosureDesign.md)。
