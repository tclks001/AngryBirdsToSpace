# ABTS：Task Graph 驱动的球面 PCG 设计

> 状态：实现前详稿。主设计见 [AngryBirdsToSpaceGameDesign.md](AngryBirdsToSpaceGameDesign.md)。
>
> 目标：先生成可通关、可分支、由能力门决定探索倾向的玩法图，再把它嵌入 `CellTopo`；连续球面仅表现最终逻辑结果。

## 1. 核心原则

借鉴《饥荒》的不是二维 Voronoi 形状，而是“任务图先行、物理布局后置、生成后验证”的层次。

```text
WorldSeed
-> Gameplay Task Graph：区域、锁、钥匙、主线与支线
-> CellTopo Spatial Graph：为每个 Task 分配连通 Cell，确定边界与连接边
-> Terrain / River / Road / Target Graph：赋予高度、河道、道路、弹弓槽、建筑目标和卫星引力走廊
-> Validation：以当前能力集合验证可达性与反绕过
-> Continuous Surface / HISM / Actor：只渲染和碰撞
```

`CellTopo` 是唯一逻辑源。每个 Task、资源、建筑、道路、河段、桥梁、可达性判定均必须可追溯至 `CellId` 或相邻 Cell 边；不得根据连续网格三角形、材质水体 Mask 或 HISM 实例归属决定 Gameplay。

## 1.1 M3 表现层接口边界

本稿负责生成逻辑 TaskGraph、Cell 区域、道路/水网/桥址边状态与建筑锚点；M3 表现实现、SDF 材质、连续网格径向高度和 HISM 规则见 [`M3TaskGraphTerrainPresentationDesign.md`](M3TaskGraphTerrainPresentationDesign.md)。两稿通过只读接口衔接：

```text
TaskGraph PCG -> CellId / TerrainType / bRoad / bWater / bBuildingAnchor
             -> M3 TerrainVisualField / Material / HISM / BuildingSpawnSite
```

其中 `bWater` 是 TaskGraph/BridgeGate 服务玩法的逻辑标签，而非由连续表面最低点、材质蓝色像素或 HISM 位置推导。M3 可以把它渲染为水色或低频下凹，但不能改变其逻辑归属或可达性。

## 2. 数据模型

### 2.1 Gameplay Task Graph

每个 Task 是一个“有玩法职责的区域”，而不是一个纯地形标签。

| 字段 | 含义 |
| --- | --- |
| `TaskId` | 稳定 ID，供 Seed 与日志使用。 |
| `TaskType` | `Start` / `Workshop` / `SlingshotRange` / `TargetBuilding` / `BridgeGate` / `SatelliteWindow` / `FurnaceRuins` / `LaunchSite` / `Scout`. |
| `RequiredKeys` | 进入或完成该 Task 所需能力/状态。 |
| `ProvidedKeys` | 完成后授予的能力或资源状态。 |
| `RequiredSetPieces` | 必须容纳的弹弓槽、桥址、模块化建筑、卫星窗口、遗址等。 |
| `RoomArchetypes` | Task 内 Cell Cluster 的地块和内容配额。 |
| `bMainPath` | 是否为必经主线。 |

Key 不是背包物品本身，而是可达性验证的抽象状态：`BuildWorkbench`、`SimpleSlingshotReady`、`TargetDestroyed`、`HaveWood`、`BridgeBuilt`、`ReinforcedSlingshotReady`、`SatelliteShotSolved`、`HaveCrystalCore`。

### 2.2 CellTopo Spatial Graph

```cpp
struct FABTSCellEdgeState
{
    int32 CellA;
    int32 CellB; // 必须互为 NeighborCellIds
    EABTSEdgeType Type; // Land / Road / Stream / ShallowRiver / DeepRiver / LakeShore
    EABTSCrossingType Crossing; // None / Ford / FallenLog / BridgeSite / Bridge
    bool bBlocksOnFoot;
};
```

河流与道路均保存为相邻 Cell 边集合；同一条边允许同时是 `Road + Bridge`，但深河边不允许无 Crossing 的道路穿越。

## 3. 初版任务图模板

初版只生成一个高价值目标和一颗卫星；不随机增删主线 Task，只随机空间位置、区域形状和表现。固定模板保证纵向切片稳定。

```text
Start -- Workbench -- SlingshotRange -- TargetBuilding -- [BridgeGate] -- FurnaceRuins -- LaunchSite
                                \-- Scout -- SatelliteWindow --/
```

| Task | 所需 Key | 提供 Key / 内容 | 空间约束 |
| --- | --- | --- | --- |
| Start | 无 | 初始树枝、石料、队伍出生点 | 必有平缓 Cell、道路入口和树枝槽。 |
| Workbench | 无 | 工作台、桩/弦配方 | 至少一对相邻平缓建筑 Cell。 |
| SlingshotRange | `BuildWorkbench` | 配对弹弓槽与攻击教学射界 | 槽轴、地形及水域共同保证唯一目标可被攻击。 |
| TargetBuilding | `SimpleSlingshotReady` | 木材/金属部件货仓 | 在主路外，`RoadDistance` 高于阈值，视域受遮挡。 |
| BridgeGate | `HaveWood` | 桥址、对岸道路 | 可选或主线阻断边；初版只需一个。 |
| SatelliteWindow | `ReinforcedSlingshotReady` | 卫星引力走廊与高价值攻击解 | 位于潮汐锁定卫星下方的主星发射范围。 |
| FurnaceRuins | `TargetDestroyed` | 熔炉与太空弹弓施工邻接条件 | 非水、平缓、遗址锚点明确。 |
| LaunchSite | `HaveCrystalCore` 或初版替代配方 | 终局 | 与遗址同 Cell 或相邻 Cell 可施工。 |

桥梁建成前，若 BridgeGate 被设为主线锁，则不得从其他边绕过它；桥梁建成后，对岸道路和后续 Task 必须可达。初版可让桥梁作为同一目标的第二个材料用途，而不以它承载全部主线内容。

## 4. CellTopo 落地算法

### 4.1 分配 Task 种子与区域

1. 选择 Start Cell，要求 `BuildSlope` 合格且非水体。
2. 按主线顺序从 Start 沿球面图选择 Task Seed；最小图距离、最大图距离和与已分配 Task 的缓冲距离均为可配置范围。
3. 每个 Task 从 Seed 向外进行受限 BFS，直到满足最小 Cell 数；扩张禁止吞并已分配区域和预留河道走廊。
4. 在 Task 内再切分 1–3 个 Room Cluster，例如主路旁的树枝槽、河岸桥址、道路外的建筑施工台和卫星发射窗口。
5. 若任何必需 Set Piece 没有足够连续且平缓的候选 Cell，则丢弃本次布局并以 `Hash(WorldSeed, RetryIndex)` 重试。

首版可用拓扑图距离衡量远近；不依赖世界坐标直线距离判定逻辑连通。

### 4.2 主线与支线连接

任务连接优先生成一棵主线树，再添加有限回环：

- 主线相邻 Task 必须由至少一条 `Land` 或可解锁 `Crossing` 边相连。
- 支线从已解锁主线 Task 分叉，奖励仅提供资源、侦察或后期捷径，不能持有主线唯一 Key。
- 至多增加一个后期开启的回环；它应减少折返，但不能绕过 BridgeGate。
- 生成道路前，先将任务连接转换为 CellTopo 上的路径走廊。

## 5. 河网、道路与能力门

### 5.1 河网

河网以 CellTopo 的逻辑高度作为输入，以 Task 边界与 BridgeGate 作为硬约束。

1. 保留旧项目的 Priority-Flood 填洼和无环下游父链思路，生成可汇流的 `FromCell -> ToCell` 河段。
2. 在 BridgeGate 处强制选择一条任务连接边作为浅河；两岸分别属于主路与对岸目标/遗址侧的可达区。
3. 河宽由汇流量分级，而非由材质宽度决定：细流可直接过，浅河需 Crossing，深河/湖泊封锁。
4. 禁止河流占用 Start、工作台、熔炉和终局施工 Cell；河岸/湿地是资源分布加权标签，不是独立自由形状。
5. 深河只用于边界、远景和支线分隔，不能让玩家必须依赖尚未设计的飞行或游泳机制。

### 5.2 道路

道路在逻辑连接确定后，以 CellTopo 最短路径生成。寻路代价应优先经过平缓、非水、低装饰密度 Cell，避开深河、湖泊、陡坡和密林核心；跨河仅允许经过 Ford、FallenLog、BridgeSite 或 Bridge。

道路是软引导而非快捷数值系统：用于主控/跟随路径偏好、相机视线、弹弓槽布置和玩家视觉导航。计算每 Cell 到主路的图距离 `RoadDistance`；建筑价值、结构模板复杂度、资源货仓与视域遮挡均由该值分级。道路不能改变锁的可达性。

### 5.3 桥梁状态变化

```text
初始：BridgeSite 边 = ShallowRiver + bBlocksOnFoot
摧毁建筑并回收木材：玩家可在桥址执行建造
建成：Crossing = Bridge，边变为可步行，重新计算可达性
```

桥梁是边状态变更，不改变 CellTopo、不挖连续地形、不重建河网。

## 6. 建筑、弹弓槽与卫星约束

### 6.1 结构化建筑

建筑不是独立资源点。每个目标建筑以 `CellTopo` 建筑施工台为锚点，并从模板生成 `Anchor`（地基）、`LoadPath`（承重件）、`Payload`（货仓）、`Trigger`（绳索/活塞/炸药）与 `WeakPoint`（可读弱点）。道路距离越大，模板允许的模块数、装置数和货仓价值越高；初版只选一座 12–25 刚体、1–3 个装置的目标建筑。

建筑逻辑库存仅在 Payload 暴露时变为可自动回收；视觉碎块不会决定材料数量。鸟撞击后，由当前发射鸟的飞越、落地或回收半径把已暴露货仓写入全队库存。

### 6.2 弹弓槽

弹弓槽是 PCG 预放置的 CellTopo 锚点对，带有槽位朝向、配对 ID、允许组件等级、可攻击方向扇区和最小/最大桩距。两桩与弹弦完成后才产生完整弹弓。目标建筑生成后必须验证：至少一个相应等级槽位存在不被山脊/深水立即遮挡的预测攻击解；因此建筑位置、槽轴和地形不能彼此解耦。

### 6.3 卫星与引力走廊

卫星相对主星的方向由 WorldSeed 固定；卫星使用独立 `Sub=2/3` CellTopo，只生成少量高价值建筑。主星上位于卫星下方的发射槽可获得 `SatelliteWindow` 标签。强化弹弓的高抛轨迹进入窗口后附加卫星引力；PCG 必须验证该偏转让至少一个远端/卫星目标可命中，且普通简易弹弓无法替代。

## 7. 生成后验证

验证应使用同一份 `FABTSCellEdgeState` 和能力状态，不使用渲染碰撞、道路网格或水体材质。

| 阶段 | 允许 Key | 必须可达 | 必须不可达 |
| --- | --- | --- | --- |
| P0 | 无 | Start、Workbench、树枝槽 | 道路外目标的完整攻击解。 |
| P1 | `SimpleSlingshotReady` | SlingshotRange、TargetBuilding 攻击解 | 卫星目标/强化攻击解。 |
| P2 | `TargetDestroyed + HaveWood` | BridgeSite、熔炉材料 | 被桥锁保护的对岸路线（若启用）。 |
| P3 | `ReinforcedSlingshotReady` | SatelliteWindow、卫星偏转攻击解 | 无法由普通弹弓替代的高价值目标。 |
| P4 | `HaveCrystalCore` | FurnaceRuins、LaunchSite | 无。 |

此外验证：

- 每个必需建筑存在合法 Cell；熔炉存在工作台相邻 Cell。
- 必需低价值资源满足首轮桩/弦配方；目标建筑货仓满足强化或终局材料下限。
- 目标建筑的 `RoadDistance`、视域遮挡、弱点朝向、弹弓槽攻击解和货仓暴露条件均符合模板。
- 主线路径不会被河网意外封死，也不会绕开启用的 BridgeGate。
- 卫星引力走廊存在强化弹弓的实际可命中轨迹。
- 连续地表对所有逻辑 Cell 中心均能查询到可用渲染高度；该项只验证表现映射，不反写逻辑。

## 8. 失败重试、可复现性与日志

每次完整尝试使用派生种子：

```text
AttemptSeed = Hash(WorldSeed, RetryIndex)
```

限制最大尝试次数，例如 8。若均失败，输出每项验证的失败原因，并切换到同一 WorldSeed 对应的保底模板；保底模板的 Task 图、关键桥址和必需 Set Piece 均固定，局部 Decor 仍可随机。

每次生成至少记录：

```text
[ABTS][PCG] Seed=%d Retry=%d Tasks=%d Cells=%d Roads=%d Rivers=%d BridgeEdges=%d Targets=%d Satellites=%d
[ABTS][PCG] Target=%d RoadDistance=%d SlotPair=%d AttackSolution=%d SatelliteSolution=%d
[ABTS][PCG] Reachability P0=%d P1=%d P2=%d P3=%d P4=%d
[ABTS][PCG] Reject=%s
```

## 9. 初版范围与后续扩展

初版只实现固定 Task 模板、一条主路、一个弹弓槽对、一座模块化目标建筑、一个可选桥址、一颗卫星及一条强化引力走廊。可将多目标建筑生态、多个卫星、复杂 WFC、动态洪水、道路速度、多个桥梁类型和全局生态密度留到后续。

验收时，使用固定 Seed 演示：道路引导至弹弓槽；道路外目标因距离和遮挡需要正确槽位与弹道；建筑连锁坍塌后由发射鸟自动回收货仓；桥梁、水网与卫星引力走廊产生可见且可验证的物理/可达性效果；日志显示每阶段 CellTopo 可达性和攻击解；连续地表、水体、道路、建筑与卫星表现均与逻辑图一致。
