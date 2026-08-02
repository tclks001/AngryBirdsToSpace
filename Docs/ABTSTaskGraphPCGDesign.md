# ABTS：Task Graph 驱动的球面 PCG 最终核心设计

> 状态：V3 核心管线、M3R-0 首周兼容方案、M3R-1 Schema、M3R-2 多候选路线、M3R-3 六 Encounter 空间候选与 M3R-3.1 普通弹弓槽场数据层已实现；已包含卫星练习区—终局发射区隔离和唯一 Space 槽对合同。M3R-4 独立终结层已达到 M3LocalAccepted（FixtureAuthority，IntegrationPending）；M3R-5 候选绑定 Biome/Envelope 表现层与 R-5.1 卫星/E5 候选预览也已达到 M3LocalAccepted（IntegrationPending），不改写 R-3 身份且不发布 MonthlyAccepted，完整 Subdivision 7 本地性能门已通过。真实 M5.1/M6/M9/M7/桥门与流程权威、R-5/R-5.1 可见 PIE/集成碰撞回归仍待集成，R6 完成后再整体验收；局部 Room 原型留在后续阶段。主设计见 [AngryBirdsToSpaceGameDesign.md](AngryBirdsToSpaceGameDesign.md)，月度地图改进见 [M3R PCG 地图生成改进方案](M3PCGMapImprovementPlan.md)，R4 细化见 [M3R-4 弹道 Witness 与流程闭环设计](M3R4BallisticWitnessAndFlowClosureDesign.md)，卫星预览见 [M3R-5.1](M3R51SatellitePreviewDesign.md)，表现层见 [M3TaskGraphTerrainPresentationDesign.md](M3TaskGraphTerrainPresentationDesign.md)，普通建筑下游见 [M7 TaskGraph DAG2.3 集成](M7TaskGraphSphericalBuildingIntegrationDesign.md)，终局前置修订见 [M11.0](M110PreFinaleClosureDesign.md)。
>
> 目标：先生成可通关、可分支、可被能力门验证的 Gameplay 图，再将它嵌入 `CellTopo`；地形、水网、道路、资源、建筑与弹弓攻击解共同服务该图。连续球面只渲染结果。

## 1. 调研结论与项目取舍

成熟 PCG 很少让单一算法同时负责剧情节奏、可达性、自然地貌和局部美术。可复用的共性是：先规定玩家必须经历的结构，再生成空间，最后填充变化并求解验证。

| 作品/方法 | 可复用方法 | 对 ABTS 的启示 | 不直接照搬 |
| --- | --- | --- | --- |
| 《饥荒》Tasks / Rooms | Task 图、Lock/Key、Room 模板、物理布局失败重试 | 先锁定任务依赖和区域职责，再分配球面 Cell | 不照搬二维岛屿 Voronoi 与大世界内容量 |
| 《Spelunky》 | 先生成保证可通关的 solution path，再从符合出入口契约的房间模板填充，最后放陷阱与奖励 | 主线可达性必须早于地形装饰；Room 原型必须保护入口/出口 | 不使用规则网格房间和平台跳跃模板 |
| 《Unexplored》的 Cyclic Dungeon Generation | 先用任务/锁/钥匙构成主环、支环与捷径，再将图翻译为空间 | 一个后期开启的回环比随机交叉连接更有玩法意义 | 不生成复杂地牢嵌套环，比赛版只保留一个支环 |
| 《XCOM 2》的 Plot and Parcel | 高层 Plot 决定道路和战术结构，手工 Parcel 提供可靠局部质量，再做组合 | Task 区域应由可测试的 Room/SetPiece 原型组合，不应全靠噪声 | 不需要方格街区、掩体网格和大量手工地块 |
| 《Warframe》/模块化关卡 | 带连接器和标签的手工模块按约束拼接 | 弹弓槽、桥址、建筑施工台应是带端口的 Set Piece，而非随机点 | 球面开放地表不适合整图模块拼接 |
| 《Deep Rock Galactic》 | 任务控制洞穴拓扑，空间节点与连接走廊分离，再以体积手段补形 | Task Seed/Room Cluster 与连接 Corridor 应分别生成 | 不需要体素洞穴和任意挖掘 |
| 《No Man's Sky》/Minecraft | 分层、确定性、可按 Seed 重建的连续场；大尺度形状与小尺度细节分离 | Hash 派生随机流、低频高度与局部装饰必须解耦 | 噪声不能决定主线、桥锁和攻击解 |
| Dwarf Fortress / 水文模拟 | 高程、降水、汇流与河流有因果关系，生成后检查世界条件 | 非关键河网可由逻辑高度导出，河宽由汇流量决定 | 比赛版不模拟气候、地质与历史 |
| Red Blob Games Mapgen | 明确承认“为游戏需求设计的不真实地形”；Voronoi/Delaunay、高程、河流分层 | 游戏可读性优先于地理真实性；水文是受约束的表现真实性 | 不要求海岸岛屿或完整蒸发降雨模型 |
| WFC / 约束求解 | 擅长局部相邻规则和样式一致性，但可能矛盾、回溯或失败 | 只用于 Task 内有限 Room/装饰标签，设置步数和回退 | 禁止对 10,242 Cell 全局 WFC，也不让 WFC 决定任务图 |
| Search-based PCG | 生成候选，以可玩性指标打分、修复、重试 | 使用分层验证器和有限候选搜索选择最佳布局 | 不做昂贵遗传算法或运行时长时间优化 |

最终采用“Constructive + Constrained Search + Validation”混合路线：高层结构构造式生成，空间布局在候选集中评分选择，局部地形使用场与规则补形，最后由能力状态求解器验证。算法的作品亮点不是随机量，而是证明道路、水网、建筑、弹弓和任务锁属于同一份可解的球面图。

调研入口与参考资料见文末第 16 节。

## 2. 不变量与层级

```text
WorldSeed
  -> Mission Graph：主线、支线、Key/Lock、节奏预算
  -> Spatial Skeleton：Task Seed、Room、连接走廊、能力门割集
  -> CellTopo Fields：区域、高度、湿度、坡度、可建造性
  -> Hydrology：汇流河网 + Gameplay 强制河障
  -> Transport：道路、桥址、浅滩、后期开启捷径
  -> Set Pieces：出生点、工作台、弹弓槽、目标建筑、熔炉、发射场
  -> Resources / Decor：配方保底、风险收益、HISM
  -> Multi-state Solver：可达性、反绕过、资源经济、攻击解
  -> Continuous Surface / Material / HISM / Actor：只读表现
```

必须始终成立：

- `CellTopo` 是唯一逻辑源。所有状态归属到 `CellId` 或规范化相邻边键 `Min(CellA,CellB), Max(CellA,CellB)`。
- Task Graph 描述“为什么去那里”；Spatial Graph 描述“区域如何相连”；道路和河流描述“怎样走、为何受阻”。三者不能合并成一个 `bRoad` 数组。
- 逻辑高度先于连续网格。不得通过渲染网格最低点反推河流或可建造性。
- 每次重建从全新结果对象开始，不复用旧 `bRoad`、`RoadDistance`、河段、锚点或验证缓存。
- 固定 `WorldSeed + GeneratorVersion + LayoutPolicyVersion + ConfigHash` 必须生成完全一致的逻辑结果。

## 3. 最终数据模型

### 3.1 Mission Task 与连接

```cpp
struct FABTSTaskNode
{
    int32 TaskId;
    EABTSTaskType Type;
    EABTSProgressPhase Phase;
    TSet<FGameplayTag> RequiredKeys;
    TSet<FGameplayTag> ProvidedKeys;
    TArray<EABTSSetPieceType> RequiredSetPieces;
    FInt32Range RegionCellBudget;
    FInt32Range DistanceFromStart;
    int32 SeedCellId;
    TArray<int32> RoomIds;
};

struct FABTSTaskLink
{
    int32 LinkId;
    int32 TaskA;
    int32 TaskB;
    EABTSLinkRole Role; // MainPath / Branch / LockedGate / LateShortcut
    FGameplayTag RequiredKey;
    TArray<int32> CorridorCells;
    TArray<FABTSCellEdgeKey> CorridorEdges;
};
```

`LinkedTaskIds` 不足以表达锁和空间连接，最终实现必须使用显式 `TaskLink`。Task ID 不再假设等于数组下标。

### 3.2 Cell 状态

```cpp
struct FABTSCellPCGState
{
    int32 TaskId;
    int32 RoomId;
    EABTSTerrainType TerrainType;
    float BaseHeight01;
    float Moisture01;
    float BuildSlopeDegrees;
    int32 MainRoadDistance;
    int32 NearestRoadDistance;
    EABTSCellFlags Flags; // Water/Lake/Wetland/Road/Buildable/Reserved...
};
```

### 3.3 边状态

```cpp
struct FABTSCellEdgeState
{
    FABTSCellEdgeKey Key;
    EABTSTransportFlags Transport; // Trail / MainRoad / Bridge
    EABTSWaterEdgeType Water;      // None / Stream / Shallow / Deep / LakeShore
    EABTSCrossingType Crossing;    // None / Ford / FallenLog / BridgeSite / Bridge
    int32 DownstreamCellId;
    float FlowAccumulation;
    FGameplayTag RequiredKey;
    bool bBlocksOnFoot;
};
```

道路和水系以边为主，Cell Flag 只是材质与快速查询的派生缓存。同一条边可以是 `ShallowRiver + BridgeSite`，建桥后切为 `ShallowRiver + Bridge`。

### 3.4 Room 与 Set Piece

Room 是 Task 内的连通 Cell Cluster，至少包含：`RoomId`、`Archetype`、入口边、出口边、Cell 预算、地形倾向、预留 Anchor。Set Piece 是带空间端口的约束模板，例如：

- `StartCamp`：出生 Cell、主路出口、树枝/石料保底槽；
- `WorkbenchPad`：中心与至少一个平缓相邻建筑 Cell；
- `SlingshotPair`：两桩中心、槽轴、允许等级、攻击扇区；
- `TargetBuildingPad`：地基 Footprint、合法攻击正面、道路外距离；内部弱点朝向留给未来 DAG-3；
- `BridgeGate`：两岸 Cell、河边、道路入口/出口和阻断状态；
- `FurnaceLaunchPair`：相邻联动 Cell；
- `SatelliteWindow`：发射槽、卫星方向和轨迹约束。
- `FinaleSpacePair`：`LaunchSite` 的唯一 Space-only 槽对、稳定槽轴、平整净空和终局局部坐标系。

## 4. 比赛版 Mission Graph

主线 Task 类型固定，空间、支线挂接点、Room 原型与晚期回环可变。固定主线保证一周内的通关闭环，PCG 价值体现在多层空间求解，而不是随机删掉核心玩法。

```text
Start -> Workshop -> SlingshotRange -> TargetBuilding -> BridgeGate -> FurnaceRuins -> LaunchSite
                         \-> Scout -> SatelliteWindow -----------/
                                      \-- LateShortcut ----------/
```

| Task | Required | Provided | 硬空间职责 |
| --- | --- | --- | --- |
| Start | 无 | 基础树枝/石料 | 平缓出生点、可见主路、资源保底 |
| Workshop | 无 | `BuildWorkbench` | 至少两个相邻平缓 Cell，邻接联动接口 |
| SlingshotRange | `BuildWorkbench` | `SimpleSlingshotReady` | 简易弹弓槽、教学目标、可验证短程弹道 |
| TargetBuilding | `SimpleSlingshotReady` | `TargetDestroyed + HaveWood` | 道路外目标、遮挡与攻击正面、材料货仓 |
| BridgeGate | `HaveWood` | `BridgeBuilt` | 水系割集、唯一合法主线 Crossing |
| FurnaceRuins | `TargetDestroyed`，若启用主线桥锁再加 `BridgeBuilt` | `ReinforcedSlingshotReady` | 熔炉/工作台相邻条件、平缓遗址 |
| Scout | 简易或强化弹弓 | 探索标记 | 非关键支线，不持有唯一主线 Key |
| SatelliteWindow | `ReinforcedSlingshotReady` | `SatelliteShotSolved + HaveCrystalCore` | 强化弹弓轨迹可解、普通弹弓不可替代；与 LaunchSite 达到终局隔离角距 |
| LaunchSite | `HaveCrystalCore` | 通关 | 与熔炉同区或相邻、无建筑平整施工台、唯一 Space-only 槽对 |

Mission Graph 构造流程：

1. 实例化必需主线节点和 Key/Lock。
2. 从允许挂接点抽取 1 个 Scout/Satellite 支线模板。
3. 在桥后或卫星后增加至多 1 条 `LateShortcut`，其 RequiredKey 必须晚于 BridgeGate，不能提前绕锁。
4. M11.0 联合验证 `SatelliteWindow` 与 `LaunchSite` 的球面最小角距；默认不得小于 `55°`。
5. 执行抽象状态搜索；若 Mission Graph 本身无法按阶段解锁，物理布局开始前就拒绝。

## 5. 确定性随机与尝试协议

禁止让所有阶段共享并顺序消费一个 `FRandomStream`，否则早期多抽一次随机数会改变整张地图。每个阶段使用独立派生 Seed：

```text
StageSeed = Hash(WorldSeed, GeneratorVersion, LayoutPolicyVersion, StageTag, AttemptIndex)
ItemSeed  = Hash(StageSeed, StableTaskId/CellId/EdgeKey, LocalIndex)
```

建议 StageTag：`Mission`、`TaskSeeds`、`Regions`、`Height`、`Hydrology`、`Roads`、`SetPieces`、`Resources`、`Decor`。逻辑结果记录 `GeneratorVersion`，算法升级后旧 Seed 仍可辨识。

M11.0 起完整尝试最多 16 次。一次尝试内优先做局部候选回退；只有结构性失败才进入下一个 Attempt。新版同时验证建筑平台和卫星—终局隔离，扩大的固定预算仍由 `AttemptIndex` 确定性派生，不改变同一配置的可复现性；全部失败时必须拒绝生成，绝不返回半张地图。

`BuildingPadClearanceRingCells=N` 的高度场预处理必须主动压平 `N+1` 圈：前 `N` 圈是认证施工面，额外一圈只作为邻接坡度计算护环。水文仍可否决被河网穿过的候选，最终 BuildingPad Planner/Validator 仍是权威门槛；不得把“压平过”直接当作验收通过。

## 6. 阶段 A：构建 CellTopo 分析缓存

对 10,242 Cell 只计算一次：

1. 规范化全部相邻边键，验证边双向一致、12 个五邻接 Cell 与其余六邻接 Cell。
2. 预计算球面图的 Landmark 距离近似；关键候选之间再执行精确 BFS/Dijkstra。避免每个候选都全图 `Acos`。
3. 为每个 Cell 建立稳定切平面基，供槽轴、河岸、建筑朝向使用；不以世界 Z 判断局部方向。
4. 初始所有 CellState/EdgeState 为默认值；每轮尝试创建新的 `FABTSWorldPCGResult`。

## 7. 阶段 B：空间骨架与 Task Seed

### 7.1 Start

Start 从满足下列条件的候选中按稳定随机抽样 32 个，再评分选择：

- 不在五边形异常点附近的强制要求可以作为软分；
- 有连续 7–12 个可作为平缓地的邻域；
- 到预计 BridgeGate 和 LaunchSite 有足够图距离；
- 局部邻接方向分布均匀，不形成狭窄尖角。

### 7.2 主线走廊骨架

不再用 `Normalize(Lerp(StartDirection, EndDirection, Alpha))` 摆一条直线。改为先生成一条带转折的球面主线骨架：

1. 从 Start 以图距离区间选 16–32 个 LaunchSite 候选，排除近对跖数值退化区。
2. 对每个终点生成 3–5 条带方向惯性的候选骨架路径。代价包含步长、转角、与自身距离、预留支线空间；此时尚不使用最终地形。
3. 沿骨架累计图距离放置主线 Task，但允许在目标距离窗内左右偏移，不要求共线。
4. 对整组 Seed 评分，而不是逐个贪心：

```text
ScoreSeeds =
  + TaskDistanceWindow
  + RegionCapacity
  + BranchSpace
  + BridgeCutPotential
  + ScenicCurvature
  - SeedCrowding
  - AntipodalDegeneracy
  - CorridorSelfApproach
```

5. 从前 K 个布局中用派生随机流选择，避免永远输出唯一最优但保持质量下限。
6. M11.0 还要求 `SatelliteWindow` Seed 相对 `LaunchSite` 达到 `MinSatelliteLaunchAngularSeparationDegrees`；若无候选则拒绝当前 Attempt，不在生成后搬动任务。

### 7.3 支线与回环

SatelliteWindow 不插在主线直线上。它从 SlingshotRange/TargetBuilding 邻近阶段分叉，要求与主路保持最小距离，并在桥后或发射区重新连接。LateShortcut 的两端先在 Mission Graph 确定，空间路径后生成；它在对应 Key 获得前保持阻断。

## 8. 阶段 C：Task Region 与 Room Cluster

禁止用全图“最近 Task Seed”一次性 Voronoi，因为它会让端点 Task 吞掉球面背面、让中间 Task 变成狭窄色带。

采用带预算的多源区域生长：

1. 为 Task 按职责配置 `Min/Target/MaxCells`，比赛版总玩法区只占球面的一部分；未分配区成为 Background/Wilderness，而不是硬塞给最近 Task。
2. 先为每条 TaskLink 预留 2–4 Cell 宽 Corridor Mask。
3. 所有 Task 从 Seed 同步使用优先队列扩张，优先级包含：到 Seed 距离、紧凑度、目标预算、期望地形、与 Corridor 接触、边界长度。
4. 达到 Target 后显著提高扩张代价，达到 Max 后停止；禁止吞并其他 Task、关键 Corridor 和硬保留区。
5. 检查每个 Task Region 连通，入口到出口至少有两条局部候选路线；只有 BridgeGate 允许被设计为单一割口。
6. 剩余 Cell 用低成本扩张为 Wilderness Terrain Patch，不创建 Gameplay Task。

Task 内再生成 1–3 个 Room Cluster：入口 Room、玩法 Room、出口/奖励 Room。Room 采用受限生长或小型 BSP 式预算切分，不做全局 WFC。每个 Room 先声明端口与 Set Piece 配额，再分配地形标签。

## 9. 阶段 D：低频高度场

高度既要给水文输入，也要保护 Gameplay。顺序为“硬锚点 → 低频场 → 约束松弛”，不使用渲染网格反求。

1. 给 Task/Room 指定高度带，而非整个 Task 单一常数，例如 Plain `[0.08,0.20]`、Forest `[0.12,0.35]`、Highland `[0.35,0.65]`、Mountain `[0.65,0.95]`。
2. 建立硬锚点：Start、建筑 Footprint、弹弓槽、道路门口、桥两岸、Furnace/LaunchSite 的高度与最大坡度。
3. 叠加 3–6 个球面 RBF/测地距离低频基元：山丘、山脊、盆地和谷向；Noise 只扰动非关键区。
4. 对 Cell 图进行有限次拉普拉斯松弛，同时钉住硬锚点和山脊/河谷引导点。
5. 从相邻 Cell 高差估算逻辑坡度，若 Set Piece Footprint 超阈值则局部压平并再次松弛；这属于逻辑 PCG 数据，不读取连续网格。
6. 保留 `PreWaterHeight` 与 `FinalHeight`：前者用于自然汇流，后者允许河道和道路有限雕刻。

## 10. 阶段 E：水网与 Gameplay 河障

本项目采用“受约束水文”，不是纯模拟，也不是 BridgeGate 周围随便三个水 Cell。

### 10.1 自然水文骨架

1. 在 `PreWaterHeight` 上运行 Priority-Flood，消除无出口小洼地或将保留洼地标为 Lake Basin。
2. 每个非湖 Cell 选择严格更低的相邻 Cell 为 Downstream；相等时用稳定 Edge Hash 破平，保证无环。
3. 按高度降序累计 `FlowAccumulation = LocalRunoff + UpstreamSum`。
4. 从高地/湿润区选择彼此有最小间距的水源；沿 Downstream 追踪到湖、湿地汇或背景排水区。
5. 按汇流量分为 Stream、ShallowRiver、DeepRiver；低流量支流可裁剪，控制视觉密度。

### 10.2 BridgeGate 强制割集

自然水文只提供候选。BridgeGate 必须满足 Gameplay：

1. 在桥前 Task 集合与桥后 Task 集合之间计算 CellTopo 边界割集。
2. 从割集中选择地形、河向和道路方向合适的 BridgeSite Edge，要求两岸有平缓 Approach Cells。
3. 将自然河道引导到该割集，并扩展为连续水障带；允许小范围调高/调低非锚点 Cell 形成可信河谷。
4. 暂时移除所有 Crossing，用可达性验证证明桥前无法到达桥后；若仍可绕行，则扩展水障、使用深水/湖岸封闭缺口，或换候选割集。
5. 只在选中的 BridgeSite 允许主线 Crossing。浅支流可以另有 Ford，但不得跨越同一主线割集形成绕锁。
6. 桥建成后仅改变该 Edge 的 Crossing 与通行状态，不重建河网和连续球面。

这使“河流真实感”和“任务门”兼容：自然水文决定大部分形态，Gameplay 割集决定唯一不可妥协的过河关系。

## 11. 阶段 F：道路网络

道路在高度和水网确定后生成，使用 A* 或 Dijkstra，不再使用无权 BFS。每条 TaskLink 都有独立路径角色。

```text
Cost(CellA -> CellB) =
    BaseLength
  + SlopePenalty
  + TurnPenalty(previous edge, next edge)
  + TerrainPenalty
  + WaterPenalty/CrossingPenalty
  + SetPieceExclusionPenalty
  + DenseForestPenalty
  - ExistingRoadReuseBonus
  - CorridorPreference
```

硬规则：

- DeepRiver/LakeShore 无合法 Crossing 时不可通行；
- LockedGate 在当前阶段不可通行；
- 建筑 Footprint、弹弓桩和保留河岸不可占用；
- 主路必须经过 BridgeSite Edge，而不是走入一个 `bWater` Cell；
- 路径每步记录 Edge，Cell 的 `bRoad` 仅为派生显示数据。

生成顺序：主线道路 → 支线路径 → 后期开启捷径 → 次要资源 Trail。后生成道路享受复用奖励，形成真实汇合；同时设置最大共线复用，避免所有支路完全塌成一条线。对主路做一次离散平滑：在不增加锁绕过、不越水、不超坡度的前提下替换锯齿拐点。

生成后分别计算：

- `MainRoadDistance`：到主线道路的拓扑距离；
- `NearestRoadDistance`：到任意道路距离；
- `ProgressDistance`：从 Start 沿已解锁交通图的代价距离。

三者不能混为一个 `RoadDistance`。目标价值主要参考 MainRoadDistance，任务进度参考 ProgressDistance。

## 12. 阶段 G：Set Piece、攻击解与资源经济

M3R-4 的不可变候选连接、服务边界、完整输入域与验收状态详见 [M3R-4 弹道 Witness 与流程闭环设计](M3R4BallisticWitnessAndFlowClosureDesign.md)。

现行 R4 v1 是 M3-local FixtureAuthority 终结器：只读连接 R3/R3.1 候选，生产默认保持 `NotEvaluated`，并显式拒绝合成流程快照冒充 Integration。真实 M5.1 制作目录、M6 轨迹、M7 Profile、M9 重力和候选桥门/奖励适配仍是 IntegrationPending；本工作树完成 R6 后才进行整体联合验收。

### 12.1 放置顺序

按照约束最强优先：BridgeGate → SatelliteWindow/LaunchSite 隔离 → FinaleSpacePair → Slingshot/Target Pair → Furnace/Launch Pair → Workshop → Start Resources → 普通资源与装饰。每类先枚举候选，再评分，不在失败后随便找最近 Cell。

`FinaleSpacePair` 只占用 `LaunchSite` 的认证平整施工台，输出唯一 `SlotPairId`、左右槽位置和 `Forward/Right/Up` 局部坐标系。它不是 M7 建筑，也不能和普通 `SlingshotRange` 槽混计。

### 12.2 建筑和相邻联动

- 建筑仅能锚定在逻辑坡度合格的 CellTopo 中心点；Footprint 需要的相邻 Cell 也必须合法。
- Workbench/Furnace 联动使用 `NeighborCellIds` 或显式相邻边判断。
- 建筑入口朝道路，目标的合法攻击正面朝弹弓攻击扇区；未来 DAG-3 可在该正面内继续选择内部 Failure Frontier，但 PCG 当前不生成 `WeakPointId`。

### 12.3 弹弓—目标联合求解

不能先随机放建筑，再期望玩家能打中。每个候选组合执行低成本弹道采样：

1. 枚举弹弓槽 Anchor Pair、允许桩距与槽轴。
2. 枚举目标建筑 Footprint、攻击正面和朝向模板；不假定 DAG2.3 已有内部弱点。
3. 采样若干拉伸量/发射角；沿预测轨迹查询球面逻辑高度、水障和预留碰撞体包围体。目标命中必须由连续线段与球面首次交点确定，撞击速度也在首次进入点插值；段末速度和最近球心点都不能代替首次接触。
4. 简易弹弓至少有一条命中教学/主目标的解；强化弹弓至少有一条进入 SatelliteWindow 后命中高价值目标的解。
5. 需要能力区分时验证上一档完整批准输入域的反例，输入域同时包含档位可用鸟种、目标效果、槽对、发射侧、拉伸量与瞄准平面采样。R4 v1 冻结 `Simple={Red,Blue,Yellow}`、`Reinforced={Red,Blue,Yellow,Black}` 并把 BirdCatalogHash 写入身份/证书；缺鸟、多鸟或重复鸟均 fail closed。当前 Simple/Reinforced 共用速度范围，不能伪造“仅因速度不足而无解”；应由 Black 专属目标效果证明当前能力门，或等待 M6 提供真实档位化发射曲线。

PCG 只保存经验证的 Solution Witness：槽位、目标、参数范围、最小净空。运行时仍允许玩家找到其他解。

### 12.4 资源保底与风险收益

资源生成先满足配方不变量，再随机填充：

- P0 可达区的树枝与石料足够建造简易弹弓，并留 20% 容错；
- 桥前目标货仓提供建桥所需木材；禁止把唯一木材放到桥后；
- 强化/终局材料只在对应能力可达后出现，但失败/落水不得永久锁死；
- 离主路更远、地形风险更高的可选资源价值更高；主线唯一 Key 不放在随机 HISM 上。

R4 Fixture 当前只证明抽象 Key/奖励/桥门流程机以及 M11.0 太空桩、太空弦的终局配方；Workbench、Simple、Bridge、Reinforced 的免费合成步骤不是当前 M5 制作目录的替代。真实配方、Workbench/Furnace 时序和资源来源必须由 Integration 适配后重新闭环，未完成前 `bExternalInputsCertified` 恒为 false。

## 13. 阶段 H：多状态验证、修复与重试

验证器直接读取 Task、Cell 和 Edge 数据，不读取材质、HISM 或连续网格碰撞。

### 13.1 状态搜索

状态至少包含 `CurrentReachableCells + AcquiredKeys + ResourceThresholdFlags`。对比赛版 Key 数量很少，可用 BFS/位集闭包：

| Phase | 初始能力 | 必须可达/可完成 | 必须不可达 |
| --- | --- | --- | --- |
| P0 | 无 | Start、Workshop、基础资源 | 主目标完成、桥后区域 |
| P1 | `SimpleSlingshotReady` | Target 攻击 Solution Witness | Satellite 高价值解 |
| P2 | `TargetDestroyed + HaveWood` | BridgeSite 建造入口 | 未建桥时的桥后主线 |
| P3 | `BridgeBuilt` | FurnaceRuins、强化施工 | 不得被其他 Ford 绕过桥锁 |
| P4 | `ReinforcedSlingshotReady` | SatelliteWindow 强化轨迹 | 普通弹弓替代解 |
| P5 | `HaveCrystalCore` | LaunchSite、唯一 Space 槽对与终局 | 第二对 Space 槽、LaunchSite 建筑 |

### 13.2 验证项

- Task Graph 按 Key 顺序可解，无自锁、无唯一 Key 位于自身锁后。
- 每个 Task Region 连通，所有 Required Set Piece 均在本 Task 或允许边界内。
- 桥前/桥后在无桥时确实分离，建桥后连通；LateShortcut 不提前绕锁。
- 主路连续、坡度合格、只通过合法 Crossing；道路不会穿越建筑 Footprint。
- 水网 Downstream 无环，河段连续，深水不吞掉关键锚点。
- 建筑、弹弓和联动 Cell 的逻辑坡度与邻接关系合法。
- 简易/强化弹弓分别存在所需攻击解和能力区分反例。
- `SatelliteWindow` 与 `LaunchSite` 满足配置的最小球面角距；`LaunchSite` 恰有一个合法 Space-only 槽对。
- 每阶段资源下限满足，关键物资不会因唯一一次失败永久丢失。
- 所有数组、CellId、TaskId、RoomId、EdgeKey 均合法；无旧结果残留。

### 13.3 局部修复优先级

1. 道路失败：换第 K 条路径、调整转角权重或换 BridgeSite Approach。
2. 水障可绕：封闭最短绕行缺口或换割集，不整图加水。
3. Set Piece 无候选：在所属 Room 内局部压平/换原型/扩 1 圈预算。
4. 攻击解失败：旋转槽轴和目标攻击正面、换候选 Cell，再考虑局部降低遮挡。
5. 区域容量失败、Mission 锁失败或多处结构冲突：整次 Attempt 重试。

所有修复有次数上限并写日志，避免无限循环和难以复现的隐式补丁。

## 14. 评分、质量与多样性

通过硬验证的候选再按软指标评分：

```text
FinalScore =
  3.0 * ProgressionReadability
+ 2.5 * BridgeGateClarity
+ 2.0 * AttackSolutionQuality
+ 1.5 * RoadNaturalness
+ 1.0 * RegionCompactness
+ 1.0 * ScenicVariety
+ 1.0 * OptionalExplorationValue
- 2.0 * ExcessiveBacktracking
- 1.5 * RoadZigzag
- 1.0 * TinyTerrainPatches
```

不要只取绝对最高分，否则不同 Seed 会趋同。保留前 3 个通过候选，按归一化权重确定性抽取。多样性来自：空间骨架转折、Task 区形状、支线挂接、河流支系、道路复用、Room 原型和 Set Piece 朝向；不来自破坏 Key 顺序。

## 15. 实现模块、日志与验收

### 15.1 建议模块

| 模块 | 单一职责 |
| --- | --- |
| `FABTSMissionGraphBuilder` | Task/Link/Key 模板实例化与抽象可解性 |
| `FABTSCellGraphCache` | EdgeKey、距离、局部切平面与查询 |
| `FABTSSpatialSkeletonBuilder` | 主线骨架、Task Seed、支线与回环候选 |
| `FABTSRegionGrower` | Task/Room 带预算连通生长 |
| `FABTSHeightFieldGenerator` | 低频高度、锚点、坡度与局部平整 |
| `FABTSHydrologyGenerator` | Priority-Flood、Downstream、汇流和河级别 |
| `FABTSGateCarver` | BridgeGate 割集、水障与反绕过 |
| `FABTSRoadPlanner` | 加权寻路、复用、平滑与距离场 |
| `FABTSSetPieceSolver` | 建筑、弹弓、桥、卫星窗口候选求解 |
| `FABTSBallisticValidator` | 简化弹道、净空和能力区分 |
| `FABTSWorldValidator` | 多阶段可达性、资源、拓扑与诊断 |
| `FABTSWorldPCGOrchestrator` | Stage Seed、Attempt、局部修复和保底 |
| `FABTSM3MonthlySchemaBuilder` | 将已接受世界只读投影为月度 Schema，验证引用/排序并计算独立 64-bit 身份；不得改写 TaskGraph 或共享导出 |
| `FABTSM3MonthlyRouteBuilder` | 构造月度球面路线候选、Corridor 与 `(CellId, IncomingEdgeId)` 道路搜索，计算独立候选池身份；R-2 不发布世界 `LayoutHash` |
| `FABTSM3MonthlyPresentationBuilder` | 对 R-3 每个保留候选构建只读 Biome/Envelope/Beat/装饰计划；逻辑 singleton 只允许显示主题合并，不选择候选、不改写源身份、不发布 MonthlyAccepted |

避免重新把全部阶段塞回 `FABTSM3TaskGraphGenerator.cpp`。各模块输入输出使用普通结构体，便于自动测试；`AABTSM3Planet` 只编排并把最终结果交给表现层。

### 15.2 调试快照

每阶段可选择输出只读 Debug Snapshot：Task Graph、Region、Height、Flow、River Class、Road Cost、Reachability Phase、SetPiece Candidates。建议让材质 Debug Mode 按 LUT 显示这些逻辑层，便于区分“算法没生成”和“材质没渲染”。

日志至少包含：

```text
[ABTS][PCG][Attempt] Seed=%d Version=%d Attempt=%d ConfigHash=%u
[ABTS][PCG][Mission] Tasks=%d Links=%d Keys=%d AbstractSolvable=%d
[ABTS][PCG][Regions] Assigned=%d Wilderness=%d MinTask=%d MaxTask=%d
[ABTS][PCG][Water] Sources=%d Segments=%d Shallow=%d Deep=%d BridgeCut=%d Bypass=%d
[ABTS][PCG][Road] MainEdges=%d BranchEdges=%d AvgSlope=%.2f MaxTurn=%.2f
[ABTS][PCG][SetPiece] Type=%s Candidates=%d ChosenCell=%d Witness=%d
[ABTS][PCG][Validate] Phase=%d Reachable=%d Required=%d Forbidden=%d Result=%d
[ABTS][PCG][Repair] Stage=%s Action=%s Count=%d
[ABTS][PCG][Reject] Stage=%s Code=%s Cell=%d Edge=(%d,%d)
[ABTS][PCG][Route] Stage=M3R1 ...
[ABTS][PCG][Encounter] Stage=M3R1 ...
[ABTS][PCG][Biome] Stage=M3R1 ...
[ABTS][PCG][Quality] Stage=M3R1 ...
[ABTS][PCG][MonthlyRoute] Stage=M3R2 ...
[ABTS][PCG][BiomePresentation] Stage=M3R5 ... MonthlyAccepted=0 PreviewAuthority=0 ...
[ABTS][M3R5][Preview] PreviewAuthority=1 MonthlyAccepted=0 ...
```

### 15.3 自动验收

- 固定 Seed 数量与顺序由阶段 Manifest 冻结；M3R-1 使用 21 个 Compatibility Seeds，M3R-2 使用 200 个 route-only Seeds，M3R-7 另用 1000 Seed 认证清单。每个确定性用例至少运行两次，逻辑结果 Hash 必须一致。
- 连续更换 Seed 重建同一 Actor，结果必须等于 fresh Actor，不允许旧道路/河网残留。
- 100 个 Seed 批量生成：接受结果必须 100% 通过全部门槛；有限 Attempt 全部失败时必须明确拒绝，不得崩溃、死循环、非法索引或返回半张地图。未来只有在真正实现并同样通过 Validator 的保底模板后，才能把“进入保底”计为成功。
- 每个 Seed 通过 P0–P5、桥锁反绕过、弹道 Witness、资源下限和水文无环验证。
- 记录各阶段耗时。比赛版目标：Editor Development 下逻辑 PCG `P95 < 2s`，完整连续网格和 HISM 构建另计但总进入可玩状态控制在 20 秒内。
- 固定展示 Seed 还需人工验收：从出生点能读到道路；河流明确切割路线；目标虽离路但可侦察；桥建造前后路线变化可见；普通/强化弹弓的攻击空间有明显区别。
- M11.0 固定展示 Seed 还需人工验收：卫星练习区不贴近终局施工台；LaunchSite 平整无建筑，且只出现一对相邻太空槽。

## 16. 调研来源

以下资料用于提炼方法，不意味着本项目复制其具体实现：

- Darius Kazemi, [Spelunky Generator Lessons Part 1: Generating the Solution Path](http://tinysubversions.com/spelunkyGen/) 与 [Part 2: Generating the Rooms](https://tinysubversions.com/spelunkyGen2/)：先保证解路径，再用满足端口的模板填充。
- Derek Yu, *Spelunky* 制作资料：随机房间模板与手工设计规则结合。
- Joris Dormans, *Adventures in Level Design / Cyclic Dungeon Generation*；以及 [AI and Games 对 Unexplored 的讲解](https://www.youtube.com/watch?v=LRp9vLk7amg)：用任务、锁和环构造有意义的探索结构。
- Firaxis, [Plot and Parcel: Procedural Level Design in XCOM 2](https://www.youtube.com/watch?v=5jrq5rDI4dk)：高层 Plot 与可测试 Parcel 的分层组合。
- Ghost Ship Games, [Deep Rock Galactic - Procedural Level Generation](https://www.youtube.com/watch?v=cDcoMdHYdQg)：任务拓扑、洞室与连接空间的分层。
- Hello Games, [Continuous World Generation in No Man's Sky](https://www.youtube.com/watch?v=sCRzxEEcO2Y)：确定性、分层连续场与按需重建。
- Kate Compton, [Practical Procedural Generation for Everyone](https://www.youtube.com/watch?v=WumyfLEa6bU)：生成器可控性、分层随机与设计意图。
- Brian Bucklew, [Tile-Based Map Generation using Wave Function Collapse in Caves of Qud](https://www.youtube.com/watch?v=AdCgi9E90jw)；Maxim Gumin, [WaveFunctionCollapse](https://github.com/mxgmn/WaveFunctionCollapse)：局部相邻约束、失败与回退边界。
- BorisTheBrave, [Wave Function Collapse Explained](https://www.boristhebrave.com/2020/04/13/wave-function-collapse-explained/)：将 WFC 视为约束求解，而不是万能地图算法。
- Amit Patel / Red Blob Games, [Mapgen2 Procedural Island Map Generator](https://www.redblobgames.com/maps/mapgen2/)：为 Gameplay 设计地貌、区域/高程/河流分层。
- Barnes, Lehman, Mulla, *Priority-flood: An optimal depression-filling and watershed-labeling algorithm for digital elevation models*：Priority-Flood 水文基础。
- Shaker, Togelius, Nelson, [Procedural Content Generation in Games](https://pcgbook.com/)：Constructive、Search-based 与验证驱动 PCG 的方法框架。
- [Dwarf Fortress World Generation](https://dwarffortresswiki.org/index.php/World_generation)：高程、气候、水文与历史模拟的分层世界生成案例。

## 17. 与当前 M3 演示实现的迁移关系

当前源码的七节点线性模板、最近 Seed 全图分区、BridgeGate 三水 Cell 和无权 BFS 道路只视为 M3 表现联调器。迁移顺序建议：

1. 先引入 `WorldPCGResult + TaskLink + EdgeState`，保留现有渲染接口兼容派生 `bRoad/bWater`。
2. 实现新鲜结果对象、Stage Seed、Mission Graph 和多状态可达性验证。
3. 替换 Task Seed/全图 Voronoi为骨架候选与预算区域生长。
4. 实现逻辑高度、水文与 BridgeGate 割集。
5. 用加权 RoadPlanner 替换无权 BFS。
6. 加入 SetPiece/弹道联合求解与资源保底。
7. 最后扩展 M3 LUT 表现河边、道路边和 Debug Snapshot；表现层不得反向成为 Gameplay 数据源。

首个可验收纵向切片只要求一条主线、一个支线环、一个桥锁、一个目标建筑、一对简易弹弓槽和一个卫星窗口，但必须完整通过同一套 P0–P5 求解器。这样 PCG 的“核心”体现在规则相互作用与可证明通关，而不是地图看起来随机。

### 17.1 V2 已实现范围（2026-07-21）

当前代码已完成：显式九节点 Mission Graph 与九条 `TaskLink`、主线弯曲骨架与支线、带预算连通区域生长和 Wilderness、低频逻辑高度/湿度/坡度、平缓建筑锚点、汇流河边、球面闭合 BridgeGate 割集、显式 `CellEdgeState`、带地形/坡度/水障代价的道路、三类道路距离、桥前不可达/桥后可达验证、阶段派生 Seed、有限 Attempt、兼容 M3 的 `bRoad/bWater` 派生缓存及详细日志。

尚未在本次 V2 中伪造的部分：Room 原型库、真实弹弓弹道 Witness、阶段资源库存求解和失败后的保底模板。这些系统需要 M5–M9 的真实配方、建筑 Footprint、弹弓参数和卫星引力接口；数据结构与模块边界已经预留，接入时不得退回通过渲染碰撞或 HISM 反推逻辑。

### 17.2 M11.0 的 V3 现行修订（2026-07-27）

- `GeneratorVersion` 提升为 3；
- `FABTSM3PCGConfig.MinSatelliteLaunchAngularSeparationDegrees` 默认 `55°`；
- `FABTSM3PCGSummary.SatelliteLaunchAngularSeparationDegrees` 记录实际角距；
- Space 槽对和 `FABTSM110FinaleLocalFrame` 由接受的 `LaunchSite`/`SatelliteWindow` 结果确定；
- M7 不再把 `LaunchSite` 当作建筑任务；其平整施工台改供终局槽使用；
- 完整字段、配方、M9/M11 引力隔离和验收见 [M11.0 终局前置收口](M110PreFinaleClosureDesign.md)。

### 17.3 M3 首周空间节奏修订（2026-07-28）

本修订保持稳定枚举、`GeneratorVersion=3` 和世界生成合同不变，具体实现与月度后续见 [M3R PCG 地图生成改进方案第 12 节](M3PCGMapImprovementPlan.md#12-首周兼容小修方案)。

- 新增 `LayoutPolicyVersion=1`、`ConfigHash` 与 `LayoutHash`，在不破坏 M11 冻结版本身份的前提下区分首周布局策略及其输入/结果；`ConfigHash` 覆盖布局配置、Planet 几何上下文与量化后的有序 `CellTopo`；
- 主线采用可调 120° 总跨度和首周冻结节奏，接受结果必须满足 `MainRouteLengthCM>=18000`；
- `RoadPortalCellId` 与 `BuildingAnchorCellId` 成为两个显式 CellTopo 身份；
- BuildingPad 改为道路前 `Reserve`、高度场压平预留 Anchor、道路后 `Certify`，不得在水文/道路结果出来后偷偷换点；
- 普通三栋建筑的 NoRoad Ring 是 RoadPlanner 硬禁止区，默认最小主路偏移分别为 3/4/5 Cell；最终还必须按实际平台、混合带和道路宽度通过连续几何相交认证，几何候选边预筛阈值由实际尺寸动态推导；
- LaunchSite 保持道路端点与终局施工 Anchor 的双重身份，不套用普通建筑禁入规则，但其高度压平护环必须完整归属于 LaunchSite Task；
- `ProgressDistanceCM/FlowS` 按 Start→Launch 有序主路线段的球面弧长计算；道路外 Cell 继承最近主路投影的纵向进度；
- Validator 以 CellTopo 高度包络和 M4 相机代理验证开局视距：B1 可见、B2/B3 隐藏；
- 新增 `ABTS.M3.WeekOne` Seed 合同、独立弧长/BFS/视距/路线唯一性复算以及配置、几何、拓扑 Hash 敏感性测试，并继续通过 WorldGeneration 和 M11.0 103 Seed 分离回归。

### 17.4 M3R-1 月度 Schema 观测层（2026-07-29）

本修订只建立后续月度求解器的数据语言和观测面，完整实现与证据见 [M3R PCG 地图生成改进方案第 14.4 节](M3PCGMapImprovementPlan.md#144-m3r-1建立月度-schema版本身份与观测面)。

- Mission Task、Route Beat、Encounter、Pocket 和 Biome 使用独立稳定 ID；关联关系通过显式引用保存，不从数组下标或其他 ID 猜测；
- `RouteBeatPlan`、`EncounterContract`、`BiomeDistrict`、`PlayableEnvelope`、`WorldQualityReport` 组成 `FABTSM3MonthlyWorldSchema`；M3R-1 只观察已经接受的 Gen3/Policy1 世界；
- `CompatibilityOracle` 保持 `GeneratorVersion=3 + LayoutPolicyVersion=1` 权威；`MonthlyDevelopment` 仅预留 `LayoutPolicyVersion=2` 身份，在 R-1 不得发布 `MonthlyAccepted`；
- `SourceConfigHash/SourceLayoutHash` 保留旧世界身份，独立 64-bit `SchemaConfigHash/SchemaLayoutHash` 覆盖目录/求解器版本和有序 Schema 内容，不替换旧 32-bit Hash；
- Compatibility Oracle 对 21 个冻结 Seed 逐一保存覆盖 Task、Link、Cell、Edge 与 Summary 全字段的 canonical 64-bit 快照，避免旧 32-bit `LayoutHash` 未覆盖字段发生漂移却漏检；
- `AABTSM3Planet` 暴露只读 Schema 与 Editor-only Debug 索引，并分别输出 Route、Encounter、Biome、Quality 摘要；R-1 不改变正式外观、道路、三栋建筑站点、M7 `Expected=3` 或共享世界生成合同；
- 只读 M7 ProfileDescriptor、M6/M9 SolverVersion 仍是 Integration-owned vNext 需求。R-1 允许其身份为 0 并保持未解析；R-3/R-4 消费正式目录/求解器时必须在缺失或 Hash 不匹配时 fail closed。

### 17.5 M3R-2 多候选球面路线池（2026-07-29）

本修订落实月度路线的独立候选阶段，完整数值、冻结身份、证据和阶段边界见 [M3R PCG 地图生成改进方案第 14.5 节](M3PCGMapImprovementPlan.md#145-m3r-2实现多候选球面路线与道路求解)。

- 每个 Seed 固定尝试 8 个带有序控制点的球面骨架；骨架先转为核心/允许 Corridor，再由以 `(CellId, IncomingEdgeId)` 为状态的确定性整数代价搜索连接；
- Road Context 按 Cell 预留 Terrain、Slope、Water、合法水体穿越、Encounter 软预留、Hard Block 和 Reuse Bias。R-2 使用中性 Context，R-3 必须注入正式状态并重新计算路线指标；
- 唯一 `M3MonthlyAcceptanceProfileV1` 定义路线长度、切向平行移动 Bend、最长直段和非局部 CellTopo 自接近门；路线 Progress 严格累积，`FlowQ` 量化后单调从 0 到 1；
- 正常候选硬门通过后按量化分数和稳定 ID 排序，最多保留 3 个。候选、尝试报告、配置、拓扑和 Context 都进入独立 64-bit Hash；相同输入深度重放必须完全一致；
- 正常候选全部失败只产生确定性 `MonthlyRouteFallback` 中间骨架。R-2 的 `bMonthlyWorldAccepted` 恒为 false，不能把 fallback、Compatibility Oracle 或路线候选池冒充完整月度世界；
- R-2 通过 200 Seed route-only、独立全失败注入、21 Seed Compatibility 快照、旧 Schema/首周/共享合同/M11.0 和 fresh `L_ABTS_M3` 运行时门；首周 Gen3/Policy1 世界及四站点输出保持不变。

### 17.6 M3R-5 候选绑定表现层（2026-07-30）

本修订把 R-3 已冻结的 BiomeDistrict、Playable Envelope、ActiveRole 与路线节拍投影到材质/HISM 消费面，完整状态、数值和证据见 [M3R-5](M3PCGMapImprovementPlan.md#148-m3r-5实现-biomedistrict-与-playable-envelope-表现)，运行时边界见 [地形表现设计第 2.4 节](M3TaskGraphTerrainPresentationDesign.md#24-m3r-5-候选绑定表现层)。

- 为每个 R-3 保留候选生成独立表现快照，不选最终 Candidate；Source Route/Spatial 身份、逻辑 DistrictId 与完整 Envelope 成员关系保持不变；
- `DisplayBiomeArchetype` 只承担视觉平滑。小于 3 Cell 的显示连通块确定性合并；100 Seed 中 2 个逻辑 singleton 与后续 2 Cell 小碎片仅在显示副本修复，最终显示 singleton 为 0、最小显示连通块为 3 Cell，R-3 Hash 不变；
- 显式固定展示运行时为 `PreviewAuthority=1/MonthlyAccepted=0`；默认生产仍使用兼容世界，`QuerySurface`、PVS、Witness 与稳定合同不受预览影响；
- 100 Seed 为 `100/100`、300 个候选计划，ActiveRole 覆盖下限 `786‰`、DeepWild 上限 `0‰`、主题下限 `4`、全局显示邻接边界率上限 `21‰`，Oracle=`6751B93DA5E4C778`，Manifest=`9E5A2FE0E563A7C4`，构造耗时 `P95=127.860 ms/Max=139.359 ms`；
- HISM 保持 `QueryAndPhysics + ABTSDeveloperObstacleChannel + SimulatePhysics=false`，并保护道路、ActiveRole、Target、NoRoad、攻击走廊与水体。完整 Subdivision 7 重建已以 `5.522 s` 通过本地 `<=8 s` 门；可见 PIE 和真实 M6/M9/Character/Visibility 碰撞回归仍为 IntegrationPending。

### 17.7 M3R-5.1 卫星练习区预览（2026-08-01）

本修订把冻结 SatellitePracticePreset 绑定到每个精确 R-3/R-3.1 候选的 E5 槽场，完整算法、身份和图例见 [M3R-5.1](M3R51SatellitePreviewDesign.md)。

- 以 E5 槽场的距离合法参考桩对建立局部发射坐标，用连续地形表面生成卫星中心，再由共享标定函数生成 E5 背面目标 Transform；
- 参考桩对只是确定性诊断，不是 AllowedPair；普通桩仍只受最大弦长、遮挡与资源规则约束；
- F7 增加卫星、E5 代理、参考桩对与空间关系线，并隐藏旧主星 E5 Target Footprint；不生成 M9/M7 Actor，不修改 Biome/兼容世界 Hash；
- 强制 Unity 全链接和 fresh `ABTS.M3.Monthly.SatellitePreview` 2/2 通过；生产 M6/M9/M7 Witness 与可见 PIE 仍为 IntegrationPending。
