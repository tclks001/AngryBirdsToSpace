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
-> Terrain / River / Road Graph：赋予高度、河道、道路、桥址和资源
-> Validation：以当前能力集合验证可达性与反绕过
-> Continuous Surface / HISM / Actor：只渲染和碰撞
```

`CellTopo` 是唯一逻辑源。每个 Task、资源、建筑、道路、河段、桥梁、可达性判定均必须可追溯至 `CellId` 或相邻 Cell 边；不得根据连续网格三角形、材质水体 Mask 或 HISM 实例归属决定 Gameplay。

## 2. 数据模型

### 2.1 Gameplay Task Graph

每个 Task 是一个“有玩法职责的区域”，而不是一个纯地形标签。

| 字段 | 含义 |
| --- | --- |
| `TaskId` | 稳定 ID，供 Seed 与日志使用。 |
| `TaskType` | `Start` / `Workshop` / `Forest` / `BridgeGate` / `Rock` / `FurnaceRuins` / `LaunchSite` / `SideResource` / `Scout`. |
| `RequiredKeys` | 进入或完成该 Task 所需能力/状态。 |
| `ProvidedKeys` | 完成后授予的能力或资源状态。 |
| `RequiredSetPieces` | 必须容纳的树、桥址、固化石料、遗址等。 |
| `RoomArchetypes` | Task 内 Cell Cluster 的地块和内容配额。 |
| `bMainPath` | 是否为必经主线。 |

Key 不是背包物品本身，而是可达性验证的抽象状态：`BuildWorkbench`、`LaunchPrismBeak`、`HaveWood`、`BridgeBuilt`、`LaunchDarkClaw`、`HaveCrystal`。

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

## 3. 首版任务图模板

首周不随机增删主线 Task，只随机空间位置、区域形状、支线和表现。固定模板保证评审闭环稳定。

```text
Start -- Workbench -- Forest -- [Shallow River / BridgeGate] -- Rock -- FurnaceRuins -- LaunchSite
                      \-- Scout / SideResource --/
```

| Task | 所需 Key | 提供 Key / 内容 | 空间约束 |
| --- | --- | --- | --- |
| Start | 无 | 初始树枝、石料、队伍出生点 | 必有平缓 Cell 与道路入口。 |
| Workbench | 无 | 可建工作台与简易弹弓 | 至少一对相邻平缓建筑 Cell。 |
| Forest | `LaunchPrismBeak` | 可砍树、木材 | 至少一棵关键树可被弹弓路线命中。 |
| BridgeGate | `HaveWood` | 桥址、对岸道路 | 是主线唯一必需阻断边。 |
| Rock | `BridgeBuilt + LaunchDarkClaw` | 固化石料、晶体 | 至少一处可从强化弹弓服务区触达。 |
| FurnaceRuins | `HaveCrystal` | 熔炉与太空弹弓施工邻接条件 | 非水、平缓、遗址锚点明确。 |
| LaunchSite | 全部主线 Key | 终局 | 与遗址同 Cell 或相邻 Cell 可施工。 |

桥梁建成前，主线图不得从其他边绕过 `BridgeGate`；桥梁建成后，Rock 及后续 Task 必须可达。

## 4. CellTopo 落地算法

### 4.1 分配 Task 种子与区域

1. 选择 Start Cell，要求 `BuildSlope` 合格且非水体。
2. 按主线顺序从 Start 沿球面图选择 Task Seed；最小图距离、最大图距离和与已分配 Task 的缓冲距离均为可配置范围。
3. 每个 Task 从 Seed 向外进行受限 BFS，直到满足最小 Cell 数；扩张禁止吞并已分配区域和预留河道走廊。
4. 在 Task 内再切分 1–3 个 Room Cluster，例如 Forest 的密林、河岸、倒木点。
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
2. 在 BridgeGate 处强制选择一条主线连接边作为浅河；两岸分别属于 Forest 与 Rock 侧的主线可达区。
3. 河宽由汇流量分级，而非由材质宽度决定：细流可直接过，浅河需 Crossing，深河/湖泊封锁。
4. 禁止河流占用 Start、工作台、熔炉和终局施工 Cell；河岸/湿地是资源分布加权标签，不是独立自由形状。
5. 深河只用于边界、远景和支线分隔，不能让玩家必须依赖尚未设计的飞行或游泳机制。

### 5.2 道路

道路在逻辑连接确定后，以 CellTopo 最短路径生成。寻路代价应优先经过平缓、非水、低装饰密度 Cell，避开深河、湖泊、陡坡和密林核心；跨河仅允许经过 Ford、FallenLog、BridgeSite 或 Bridge。

道路是软引导而非快捷数值系统：用于主控/跟随路径偏好、相机视线和玩家视觉导航。道路不能改变锁的可达性。

### 5.3 桥梁状态变化

```text
初始：BridgeSite 边 = ShallowRiver + bBlocksOnFoot
砍树并拥有木材：玩家可在桥址执行建造
建成：Crossing = Bridge，边变为可步行，重新计算可达性
```

桥梁是边状态变更，不改变 CellTopo、不挖连续地形、不重建河网。

## 6. 生成后验证

验证应使用同一份 `FABTSCellEdgeState` 和能力状态，不使用渲染碰撞、道路网格或水体材质。

| 阶段 | 允许 Key | 必须可达 | 必须不可达 |
| --- | --- | --- | --- |
| P0 | 无 | Start、Workbench、简易弹弓建造点 | Rock、后续遗址。 |
| P1 | `LaunchPrismBeak` | Forest、关键树 | Bridge 后的 Rock。 |
| P2 | `HaveWood` | BridgeSite | Rock，直到桥建成。 |
| P3 | `BridgeBuilt + LaunchDarkClaw` | Rock、关键固化石料 | 终局前的施工完成状态。 |
| P4 | `HaveCrystal` | FurnaceRuins、LaunchSite | 无。 |

此外验证：

- 每个必需建筑存在合法 Cell；熔炉存在工作台相邻 Cell。
- 必需资源量满足配方下限；关键树、桥址和关键固化石料不被水体/建筑占用。
- 主线路径不会被河网意外封死，也不会绕开 BridgeGate。
- 连续地表对所有逻辑 Cell 中心均能查询到可用渲染高度；该项只验证表现映射，不反写逻辑。

## 7. 失败重试、可复现性与日志

每次完整尝试使用派生种子：

```text
AttemptSeed = Hash(WorldSeed, RetryIndex)
```

限制最大尝试次数，例如 8。若均失败，输出每项验证的失败原因，并切换到同一 WorldSeed 对应的保底模板；保底模板的 Task 图、关键桥址和必需 Set Piece 均固定，局部 Decor 仍可随机。

每次生成至少记录：

```text
[ABTS][PCG] Seed=%d Retry=%d Tasks=%d Cells=%d Rivers=%d Roads=%d BridgeEdges=%d
[ABTS][PCG] Reachability P0=%d P1=%d P2=%d P3=%d P4=%d
[ABTS][PCG] Reject=%s
```

## 8. 首周范围与后续扩展

首周只实现固定主线模板、一个 BridgeGate、少量支线、一条主道路和由少数逻辑河段构成的水网。可将复杂 WFC、多个岛屿、动态洪水、道路速度、多个桥梁类型和全局生态密度留到后续。

验收时，使用固定 Seed 演示：道路引导至森林；砍树得到木材；桥前河流明确阻断；建桥后道路通向岩区；日志显示每一阶段的 CellTopo 可达性结果；连续地表、水体、道路和 HISM 与逻辑图一致。
