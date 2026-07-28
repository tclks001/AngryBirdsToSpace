# M11.0：终局前置收口设计

> 状态：设计合同、C++、Editor 编译、自动化、固定 Seed 独立进程基线与用户 PIE 视觉/交互验收均已完成。下游纯数据积分已完成 [M11-A](M11AGravityAssistSolverDesign.md)；[M11-B 局部布局与全输入域认证](M11BFinaleLayoutCertificationDesign.md) 的 C++、编译与全新进程自动认证也已完成，待用户 PIE。
>
> 父级：[M11 终局三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md)。
>
> 上游：[Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [M5 背包/加工](M5InventoryCraftingImplementationDesign.md) · [M5.1 世界物品与弹弓装配](M51WorldItemsPlacementSlingshotDesign.md) · [M7 球面建筑集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) · [M7.3-DAG2.3](M73DAG23CumulativeLoadAndJointSupportDesign.md) · [M9 卫星与局部引力](M9SatelliteGravityDesign.md)。
>
> 交接入口：[ABTS 项目工作流](ABTSProjectWorkflow.md)。

## 1. 目标与阶段边界

M11.0 解决 M11 正式开始前的七个缺口：

1. `LaunchSite` 不再生成玻璃双塔建筑，保留经 Task Graph 认证的平整终局施工台；
2. 每个接受的世界只生成一对相邻、专用于 Space 档的太空弹弓槽；
3. 旧的一体式太空弹弓部件拆为两根太空弹弓桩和一根太空弹弓弦；
4. M9 卫星改作强化弹弓阶段的引力练习，并与终局太空弹弓保持足够球面距离；
5. 太空双桩轴与卫星在主星上的投影方向近似共线，使两个系统的空间关系可读，但卫星不处于终局槽的常规近地弹射邻域；
6. 建立 M11 只接受“主星 + 三颗助推行星”的纯 C++ 四天体数据边界，编译期排除 M9 卫星。
7. 将三个普通 TaskGraph 建筑从不再维护的 Legacy 生成链迁移到 M7.3-DAG2.3；M3 只保留 Anchor/施工台/Seed 上游职责。

本阶段不实现：

- 三颗助推行星和 UFO 的最终局部偏移搜索；
- M11 固定步长轨道积分、虚拟动量换能或 B-plane 事件；
- 终局轨迹 HUD、逐行星接近预览、前缀成功集稳定器；
- 四鸟深空演出、星空切换、失败黑屏复位；
- M11.0 以后再处理的球面高度雾/体积云视角问题。

## 2. 总体依赖与权威源

```text
WorldSeed + GeneratorVersion
  -> M3 Task Graph
  -> SatelliteWindow / LaunchSite 联合空间约束
  -> LaunchSite 平整施工台
  -> 唯一 Space-only 槽位对
  -> SpaceStake ×2 + SpaceCord ×1
  -> FABTSM110FinaleLocalFrame
  -> M11 终局局部布局预设
  -> Primary + Assist1 + Assist2 + Assist3 纯数据场景
```

权威边界：

- `CellTopo`/Task Graph 决定 `LaunchSite`、`SatelliteWindow`、共同槽 AnchorCell、占用和可达性；左右物理槽位由认证施工台和冻结槽距派生；
- 连续球面只把 Cell 方向转换为表面位置与法线；
- M7 可以消费 `LaunchSite` 的平整面，但不能再在上面生成建筑；
- M9 卫星在 M1–M10 的世界/Chaos/M6 查询链中继续存在；
- M11 只消费一次构造的纯数据四天体场景，绝不在积分步中扫描 World Actor；
- 同一 WorldSeed、生成器版本和配置 Hash 必须得到相同的任务位置、槽对、局部坐标系和诊断 Hash。

## 3. M3 Task Graph 终局空间合同

### 3.1 唯一 LaunchSite

比赛版 Mission Graph 仍且只能包含一个 `LaunchSite`。它继续要求 `HaveCrystalCore`，并保持主线最终任务身份；变化仅在 Set Piece：

| 旧合同 | M11.0 现行合同 |
| --- | --- |
| `LaunchSite` 是通用 Building Anchor，可生成玻璃 TwinTowerBridge | `LaunchSite` 是终局施工台和 Space-only 槽位 Anchor，不生成任何 M7 建筑 |
| 没有可装配的 Space 世界槽 | 生成且只生成一对相邻 Space-only DirtHole/Slot |
| M9 卫星直接锚定在最终任务附近 | 卫星练习位置与 `LaunchSite` 联合求解并保持远距 |

`LaunchSite` 仍可保留 `bBuildingAnchor`/`BuildingSpawnSite` 作为施工台平整和净空数据来源；“不生成建筑”应由 M7 Profile 与运行时硬防线共同保证，不能通过删除平整 Anchor 让终局槽重新落回坡面。

### 3.2 唯一相邻 Space 槽位对

M3 使用最终认证的 `LaunchSite` Anchor Cell 作为一对槽的共同逻辑锚点。“相邻”指同一施工台内的一对左右世界槽位，不要求占用两个不同 Cell：

```text
AnchorCell   = 最终认证的 LaunchSite Building Anchor
PairMidpoint = QuerySurface(AnchorCell.UnitDirection) + Up * 4cm
LeftSlot     = PairMidpoint - Right * 105cm
RightSlot    = PairMidpoint + Right * 105cm
```

首版 `FinaleSpaceSlotSeparationCM=210`、`SurfaceOffsetCM=4`。槽位对必须同时满足：

- Anchor Cell 属于 `LaunchSite`，并已通过平整、非水和净空认证；
- 左右槽共享 `AnchorCellId + SlotPairId`，各自记录 `Side=Left/Right`；
- 两个槽的世界中心距等于当前配置，并都位于同一平整施工台内；
- 不与道路、建筑 Footprint、工作台/熔炉、资源或其他弹弓槽占用冲突；
- 槽轴具有稳定朝向，不在近零长度或局部切平面退化方向上；
- 每个生成结果恰有一个由 WorldSeed/Task/Anchor 派生的稳定 `SlotPairId != INDEX_NONE`；数值 `0` 可以是合法 Hash，重建不得残留旧槽或累加第二对；
- 槽位标记为 Space-only，普通 Twig/Simple/Reinforced 桩均不能安装。

槽对的可见 Actor 沿用 `AABTSM51SlingshotDirtHole` 表现，逻辑类型为 `EABTSSlingshotSlotKind::FinaleSpace`，左右身份由 `EABTSSlingshotSlotSide::Left/Right` 表示；不得通过颜色、Actor 名称或“恰好位于 LaunchSite”反推 Space 资格。

### 3.3 卫星练习与终局远距

M9 卫星用于强化弹弓阶段教授“轨迹会被局部引力偏转”的直觉。它不能继续贴在终局太空弹弓上方。M3 应联合选择 `SatelliteWindow` 和 `LaunchSite`，并冻结最小球面分离：

```text
AngularSeparation =
    acos(dot(LaunchSite.UnitDirection, SatelliteWindow.UnitDirection))

AngularSeparation >= MinSatelliteLaunchAngularSeparationDegrees
```

M11.0 默认：

```text
MinSatelliteLaunchAngularSeparationDegrees = 55°
```

该值属于 PCG 配置和结果摘要，改变它必须改变配置 Hash/生成结果。候选选择至少使用额外 `5°` 的安全裕量，最终 Validator 仍以认证的 LaunchSite Anchor 与 `SatelliteWindow.SeedCellId` 实测，不靠候选阶段近似值放行。它表达的是主星球面上的最低隔离，不是两个 Actor 的直线厘米距离。

终局槽轴与卫星方向采用如下共线合同：

1. 从太空槽中点沿主星球面求指向 `SatelliteWindow`/卫星下点的初始切向；
2. 将该切向的正方向固定为局部 `Right/Y`，即左槽指向右槽；
3. 局部 `Forward/X = Right × Up`，并保证 `Forward × Right = Up`；
4. 验收时使用点积角差，而不是屏幕截图目测“差不多一条线”。

这里的共线只帮助玩家建立空间关系。M9 练习射击仍从 `SatelliteWindow` 附近的强化弹弓完成；终局太空槽与卫星下点的 `55°` 以上分离确保卫星脱离其常规近地发射邻域。M11 的深空射程远大于 M6，因此最终隔离仍由第 7 节的纯数据白名单保证，不能只依赖“够远”。

### 3.4 生成、验证与重试

本阶段提升 `GeneratorVersion`。每个 Attempt 至少验证：

由于同时加入终局隔离约束并保留全部既有建筑平台认证，默认确定性尝试预算由 8 提升为 16；这只扩展固定 `AttemptIndex` 搜索域，不引入运行时随机回退。

M3 高度场按 `BuildingPadClearanceRingCells + 1` 圈生成确定性平面，其中额外一圈只保护边界 Cell 的邻接坡度计算；水文穿越、可建造性、唯一 Anchor 和主路可达性仍由后续 Planner/Validator 硬验证。

- 恰有一个 `LaunchSite` 和一个 `SatelliteWindow`；
- 两者角距达到配置下限；
- `LaunchSite` 有完整平整/净空施工台；
- 存在且只存在一个合法相邻 Space 槽对；
- `SlotPairId != INDEX_NONE` 且在同一生成结果中唯一；
- 槽对轴与卫星投影方向满足批准角差；
- P0–P5 原有可达性、桥锁、道路和资源验证不被破坏。

任一硬条件失败应拒绝当前 Attempt，并记录明确原因；不得在验证后把卫星、槽或任务偷偷挪到最近可用位置。

建议日志：

```text
[ABTS][PCG][M11.0] LaunchTask=... SatelliteTask=... SeparationDeg=... RequiredDeg=...
[ABTS][M11.0][SlingshotSlots] Standard=... Finale=2 Pair=... AnchorCell=...
[ABTS][M11.0][Reject] Code=... Attempt=... Cell=...
```

## 4. LaunchSite 建筑退役

`AABTSM7GameMode` 的默认 Task Profile 不再包含：

```text
LaunchSite -> Glass -> TwinTowerBridge
```

同时保留运行时防线：即使旧 Blueprint CDO 或历史资产仍序列化了 `LaunchSite` Profile，建筑生成循环遇到该 Task 也必须跳过。这样可以防止旧关卡资产在新 C++ 默认值上继续生成玻璃建筑。

禁止生成建筑不等于删除施工台。M11.0 验收应同时看到：

- `LaunchSite` 地表仍平整且排除了树石 HISM；
- 没有玻璃、地基、建筑碰撞或 Building Actor；
- 只出现一对 Space-only 槽；
- 弹弓前向发射净空不被道路装饰或任务标签占用。

## 5. 太空桩、太空弦和配方迁移

### 5.1 现行物品

新增并进入普通物品目录：

| 枚举 | 显示名 | 角色 |
| --- | --- | --- |
| `EABTSItemId::SpaceStake` | 太空弹弓桩 | 每套需要两根，只能装入 Space-only 槽 |
| `EABTSItemId::SpaceCord` | 太空弹弓弦 | 连接两根已安装的 SpaceStake，完成 `Space` 档 |

旧 `EABTSItemId::SpaceSlingshotPart` 不物理删除，以保留枚举序列化兼容；但必须：

- 标记为隐藏/退役；
- 从 `ABTSGetAllItemIds` 和默认物品 UI 移除；
- 从默认配方目录移除；
- `FindRecipe/Craft` 不再接受旧配方；
- 运行时装配和 M11 门控均不读取它。

### 5.2 现行配方

| RecipeId | 输出 | 站点 | 材料 |
| --- | --- | --- | --- |
| `SpaceStakePair` | `SpaceStake ×2` | 熔炉 | `MetalParts ×6 + Wood ×5` |
| `SpaceCord` | `SpaceCord ×1` | 熔炉 | `MetalParts ×2 + CrystalCore ×1` |

一套完整太空弹弓的总预算仍为：

```text
MetalParts ×8 + Wood ×5 + CrystalCore ×1
```

这与旧 `SpaceSlingshotPart` 配方总成本一致，不因拆分世界组件改变已设计的资源经济。桩配方一次产出两根，避免玩家误做第三根或只做一根后无法理解终局门槛。

### 5.3 装配规则

- `SpaceStake` 只能点击未占用的 Space-only 槽；普通 DirtHole 拒绝；
- Twig/Simple/Reinforced 桩点击 Space-only 槽时拒绝且不扣库存；
- 两根 `SpaceStake` 都安装后，`SpaceCord` 才能选择第一桩和第二桩；
- `SpaceCord` 不能连接非 Space 桩，也不能把一根 Space 桩接到普通槽的桩；
- 连接成功后生成 `EABTSSlingshotTier::Space` 弹弓，并扣除一根弦；
- 一对槽完成连接后不能重复装配，世界中不得出现第二套终局 Space 弹弓；
- 普通 M6 对 Space 档的历史兼容不是 M11 最终发射实现；M11 后续从点击太空弹珠袋进入专用终局模式。

## 6. 终局局部坐标系

M11.0 必须向 M11 提供一个可验证的 `FABTSM110FinaleLocalFrame`：

| 字段 | 合同 |
| --- | --- |
| `LayoutVersion` | 局部预设坐标语义版本，大于零 |
| `LaunchTaskId` / `AnchorCellId` | 回指本次 Task Graph 逻辑源 |
| `SlotPairId` | 唯一槽对稳定身份，`!= INDEX_NONE`；数值 0 合法 |
| `WorldTransform.Location` | 两个 Space 槽世界位置中点，固定不变 |
| Local `X` / Forward | 太空弹弓规范发射前向 |
| Local `Y` / Right | 左槽指向右槽 |
| Local `Z` / Up | 主星中心指向槽中点 |
| `Left/RightSlotWorldLocation` | 经连续表面查询得到的槽表现位置 |

坐标系必须正交、右手且无缩放：

```text
Forward × Right = Up
Origin = (LeftSlot + RightSlot) / 2
```

M11 的三颗助推行星和 UFO 只能存局部偏移，通过该 Transform 转为世界位置。弹珠袋静止位置是相对于本 Frame 的另一个固定局部偏移，不能改写 Frame Origin。禁止在预设、DataAsset、关卡 Blueprint 或测试数据中保存某个 Seed 的绝对世界坐标作为正式布局。

## 7. M9 与 M11 的引力隔离

### 7.1 M9 练习阶段

在普通地表/强化弹弓阶段：

- M9 卫星锚定 `SatelliteWindow`，不再锚定 `LaunchSite`；历史 `FinalAnchorTaskType` 字段只为旧资产序列化兼容保留，运行时忽略非 `SatelliteWindow` 覆盖；
- M9 卫星保持可见；
- M6 预测和实际 Chaos 鸟继续叠加 `ABTSM9Gravity`；
- 玩家通过卫星附近的 `SatelliteWindow` 练习从不同侧经过会产生不同偏转；
- 该练习不承诺永久净增能，也不复用 M11 的虚拟公转动量。

卫星生成还必须通过两项运行时防线：

```text
Distance(SatelliteCenter, FinaleFrame.Origin)
    >= PrimaryRadius * 0.80

abs(dot(FinaleFrame.Right, DirectionToSatelliteInTangentPlane))
    >= 0.98
```

对应默认参数为 `MinFinaleSatelliteDistancePrimaryRadiusRatio=0.80` 与 `MinFinaleSatelliteLateralAlignmentDot=0.98`。不满足时销毁候选卫星并拒绝就绪，不能保留一个“差不多”的错误位置。

### 7.2 M11 终局阶段

M11 的数据端使用固定四体角色：

```text
Primary
AssistPlanet1
AssistPlanet2
AssistPlanet3
```

`EABTSM110FinaleGravityRole` 不提供 Satellite 角色。`FABTSM110FinaleGravityScenario` 固定持有上述四个 Body；其加速度查询只遍历该数组。M9 卫星、UFO、天空球、雾云、HISM、SDF 地表和建筑均不进入该数据结构。

这意味着：

- M11 预览和实际深空播放都不调用 `ABTSM9Gravity`；
- M11 不从 `TActorIterator<AABTSM9Satellite>` 自动收集天体；
- `AABTSM9Satellite::IsM11FinaleGravitySource()` 恒为 `false`，其 `bM11FinaleGravitySource` 也保持 `false`；
- M9 卫星即使仍在同一 World 可见，也不改变任何终局轨迹点或事件；
- UFO 只提供解析命中目标，不产生引力；
- 四鸟表现 Actor 不反向修改数据端的位置/速度。

纯数据边界是最终保证；世界空间距离和 `55°` 隔离只是前置关卡可读性与误触防线。

## 8. 实施顺序

1. 扩展物品枚举、显示名、物品目录和配方；保留旧枚举值但退役旧配方。
2. 让世界装配系统识别 Space-only 槽、SpaceStake 和 SpaceCord。
3. 将 `LaunchSite` 从 M7 默认建筑 Profile 移除；三个普通建筑由 M7 Resolver 接入 DAG2.3，并为旧 CDO 增加 Algorithm/LaunchSite 双防线；M7 在生成前开启 Required-building contract，逐 Actor 注册并在生成结束后封印，预期数、注册数、Accepted 数或设置阶段失败任一不符都禁止 `WorldReady`，必需 Actor 的 `NotRequired` 也按失败处理。
4. 提升 M3 `GeneratorVersion`，加入 `SatelliteWindow ↔ LaunchSite` 最小角距硬约束。
5. 生成唯一 Space 槽对和 `FABTSM110FinaleLocalFrame`，冻结槽轴/卫星投影对齐语义。
6. 建立无 UObject/World/Chaos 依赖的四天体场景结构和加速度查询。
7. 编译、运行自动化，再进行固定 Seed 与多 Seed PIE 验收。

## 9. 验收矩阵

### 9.1 Task Graph 与建筑

- [x] 固定 Seed 重建两次，`LaunchSite`、`SatelliteWindow`、共同 Anchor Cell、`SlotPairId` 和局部坐标系完全一致。
- [x] 生成结果只有一个 `LaunchSite` 和一对 Space-only 槽；左右槽共享 Anchor/Pair，Side 分别为 Left/Right，世界中心距等于 `FinaleSpaceSlotSeparationCM=210`。
- [x] `SatelliteLaunchAngularSeparationDegrees >= 55°` 或当前配置值。
- [x] 槽轴与指向卫星下点的切向夹角处于批准容差。
- [x] `LaunchSite` 保留平整施工台和净空，但没有 Glass/TwinTowerBridge 或其他 M7 建筑。
- [x] 旧 Blueprint 若仍带 LaunchSite Profile，运行时硬防线仍会跳过。
- [x] Workshop、TargetBuilding、FurnaceRuins 均为 `Algorithm=1`，DAG Macro/Support/Hash 有效，不存在 Legacy fallback。
- [x] `BuildingContractSealed Expected=3 Registered=3 SetupRejected=0`；任一预期 Actor 缺失、生成/注册失败，或 Actor `IdleValidation=Rejected/NotRequired`、`Accepted != Expected` 都进入 fail-closed 门控，不可被另外两栋已接受建筑掩盖；合同只检查注册集合，避免无关测试 Actor 误阻断。
- [x] 当前固定世界两次冷启动均为零穿透，三栋逐 Actor `IdleValidation Accepted=1`，且先于 `WorldReady=1`。
- [x] P0–P5、桥锁和主路可达性保持通过。

### 9.2 物品与装配

- [x] 默认物品/配方目录显示“太空弹弓桩”和“太空弹弓弦”，不显示旧“太空弹弓部件”。
- [x] `SpaceStakePair` 一次消耗 `MetalParts 6 + Wood 5` 并产出两根桩。
- [x] `SpaceCord` 消耗 `MetalParts 2 + CrystalCore 1` 并产出一根弦。
- [x] 旧 `SpaceSlingshotPart` 配方无法查询和制作，历史枚举值仍可安全加载。
- [x] Space 组件不能安装到普通槽，普通组件不能安装到 Space-only 槽；失败不扣库存。
- [x] 两桩一弦完成后得到唯一 `Space` 档弹弓。

### 9.3 卫星与纯数据边界

- [x] M9 强化发射的预览与实际鸟仍受卫星引力影响。
- [x] M9 卫星实际锚定 `SatelliteWindow`；历史非 SatelliteWindow 覆盖不会把它放回 LaunchSite。
- [x] 终局太空槽附近不再出现贴脸卫星，常规 M6 轨迹不能把它当成终局近端目标。
- [x] 卫星到 FinaleFrame 原点的距离不低于主星半径 `0.80`，与局部 Right 的切平面对齐点积不低于 `0.98`。
- [x] `FABTSM110FinaleGravityScenario::BodyCount == 4`，角色顺序固定为主星、①、②、③。
- [x] M9 卫星存在、隐藏、移动测试 Transform 或改变 M9 引力参数，都不改变相同 M11 四体输入的加速度结果/Hash。
- [x] 纯数据测试不创建 UWorld、Actor 或 Chaos 场景即可运行。

### 9.4 日志与性能

- [x] 接受的 PCG Attempt 输出卫星—终局角距和槽对身份。
- [x] 被拒绝 Attempt 输出确定性失败码，不出现静默位置修补。
- [x] 100 个 Seed 中所有接受结果均满足角距、唯一槽和原有可达性；全部 Attempt 失败时进入显式拒绝流程，不返回半张地图。
- [x] M11 数据端每次加速度查询只遍历固定四体，不扫描 World。

### 9.5 当前自动验证证据（2026-07-28）

- `AngryBirdsToSpaceEditor Win64 Development` 编译成功。
- `ABTS.M7` 14/14、`ABTS.M73` 13/13；新增 TaskGraph DAG2.3 Profile/旧 CDO 路由与 Required-building contract 的 open/mismatch/setup-reject/exact-match/NotRequired/Accepted-count 自动化通过。
- `ABTS.M110.FinaleFrame`、`FourBodyContract`、`SpaceSlingshotItemContract`、`TaskGraphFinaleSeparation` 共 4 项自动化全部通过。
- `TaskGraphFinaleSeparation` 对 `0–99 / 312503 / 20260727 / 8675309` 共 103 个 Seed 各生成两次；206 次结果全部接受，Task Seed、接受 Attempt 和卫星角距均确定性复现，角距范围为 `60.02°–77.80°`。
- 默认地图以两个全新 `UnrealEditor -game` 进程加载：两次均得到 `Seed=312503 / Version=3 / Attempt=0 / LaunchCell=99 / Pair=1541153380 / Separation=210cm`；卫星 `AngularSepDeg=60.56 / FinaleDistanceRatio=2.057 / LateralDot=1.0000 / FinaleGravitySource=0`；三个 DAG2.3 建筑均完成 `Accepted=1`，随后才出现 `WorldReady=1 / BuildingExpected=3 / BuildingRegistered=3`。
- 用户已在 PIE 中完成配方制作、两桩一弦安装、强化弹弓卫星偏转、终局区域及关联建筑表现验收；M11.0 于 2026-07-28 收口。

## 10. 上下游交接

- M3/Task Graph：现行 `LaunchSite`/`SatelliteWindow` 空间关系和 Space 槽合同以本稿为准；
- M5/M5.1：现行太空物品、配方与装配合同以本稿为准；
- M7：现行普通建筑走 DAG2.3、`LaunchSite` 不生成建筑，以 [M7 球面集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) 为准；
- M9：现行卫星锚定、远距练习和 M11 排除边界以本稿为准；
- M10.1-C：投影工具保持只读，不需要知道 Space 槽或 M9 排除细节；
- M11-A：已消费固定四体角色并完成纯数据积分，见 [M11-A 纯数据求解器](M11AGravityAssistSolverDesign.md)；
- M11-B：继续消费本稿局部坐标系，执行局部布局搜索、认证预设和全输入域验证；当前实现、认证门槛和 M11-C 交接见 [M11-B 详稿](M11BFinaleLayoutCertificationDesign.md)。

返回父级：[M11 终局三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md) · 下游求解器：[M11-A](M11AGravityAssistSolverDesign.md) · 当前下游：[M11-B](M11BFinaleLayoutCertificationDesign.md) · 返回交接入口：[ABTS 项目工作流](ABTSProjectWorkflow.md)
