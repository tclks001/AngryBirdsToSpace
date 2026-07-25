# M7.3-DAG：递归支撑图程序化建筑生成调研与算法设计

> 文档性质：M7.3 新路线的独立调研与算法设计；本轮不修改 C++、地图或资产。
>
> 状态：调研与方案完成；纯数据语法阶段已落地，见 [M73DAG1RecursiveGrammarImplementationDesign.md](M73DAG1RecursiveGrammarImplementationDesign.md)；Scope、稀疏支撑、模块编译与真实接触审计已落地，见 [M73DAG2SpatialLayoutAndModuleCompilationDesign.md](M73DAG2SpatialLayoutAndModuleCompilationDesign.md)。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M7.3 原总体算法](M73ProceduralModularBuildingGenerationResearch.md) · [M7.3-A 稳定建筑](M73AStableBlockBuildingImplementationDesign.md) · [M7.3-B 弱点与难度](M73BWeakPointAndDifficultyDesign.md) · [M7.3-B2 顶部结构弱点](M73B2StructuralWeaknessAndFailureValidationDesign.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md) · [M7 材料与装置](M7BuildingMaterialsAndDevicesDesign.md)

## 1. 结论先行

用户提出的“先生成宏观承载 DAG，再递归把节点分成串联或并联结构，最后推导砖块与弱点”的方向可行，而且比当前固定四柱楼层更适合作为本项目最终 PCG 核心。

但最终实现不能只保留一张抽象 DAG，也不能把一个字母直接等同于一块砖。推荐采用四层表示：

```text
推导树 Derivation Tree
-> 蓝图承载 DAG Blueprint Support DAG
-> 砖块装配图 Brick Assembly Graph
-> 真实接触 DAG Realized Contact DAG
```

对应职责是：

1. 推导树记录“用了哪些递归规则”，用于复现、调试和变异；
2. 蓝图 DAG 决定宏观轮廓、载荷路径、串并联冗余和预期失效切面；
3. 装配图把抽象区域编译为真实砖、刚体组、绳、链和装置；
4. 真实接触 DAG 从最终碰撞形状与 Transform 反建，才是静稳、旁路和 Chaos 验收的物理依据。

新方案的关键变化不是“换一种随机堆砖方式”，而是：

- 弱点属于主体承载图的中下部，不再是完整建筑顶端的附加冠部；
- 生成器先设计完整时稳定、移除目标后失稳的双状态结构；
- 三种无装置弱点由不同图切面定义，而不是同一顶部载荷下 2/3/4 根柱的差别；
- 逻辑意图必须经过真实接触图和 Chaos 反事实验证，不能只信 authored edge；
- M7.1 平面和正式球面共用同一结构 DAG，只在地面适配层分流。

本次调研没有找到商业游戏公开完整采用“递归承重 DAG → 砖块实现 → 内部弱点搜索 → 物理级联验证”的可信证据。能够确认的是，支撑 DAG、图语法、递归模块拼装、结构可行性优化、约束新颖性搜索和 Chaos 层级破坏分别已有成熟先例。因此本方案应定位为“由可靠先例组成的项目原创组合”，不能写成复刻某款商业游戏的内部算法。

## 2. 当前截图为何必然只掉顶部

这不是 Chaos 偶发问题，也不是三种参数差异不够大，而是当前 B2 生成目标与用户现在的 Gameplay 目标不同。

### 2.1 主体始终是四柱整板重复

`FABTSM73StructureBuilder::AddFourColumnStorey` 为每层固定生成四根上下对齐的角柱和一块完整楼板。`SingleTower`、`Gatehouse` 和 `TwinTowerBridge` 都重复调用该函数，因此 B2 Pattern 切换并不改变主楼的基本承载拓扑。

这种主体有两个天然问题：

- 缺少一个角柱时，另外三个角仍可形成覆盖重心的三角支撑域；
- 楼板破坏后，上部同构柱列会竖直下落到下层柱列或楼板上重新落座。

### 2.2 B2 明确从最高处增加冠部

`ABTSM73WeaknessStructureBuilder.cpp` 第 87 行附近寻找 `HighestDeck`，第 102–119 行把最高 Deck 或 Gatehouse 顶部门梁选成 `WeaknessBase`。真实生成顺序是：

```text
先生成一栋已经能独立站立的完整主楼
-> 在最高处加弱支撑、Carrier 和两个 Payload
```

而不是：

```text
在主楼中下部制造承担主体载荷的受控瓶颈
```

### 2.3 三种 Pattern 共用主要可见构件

三种 Pattern 只在 Carrier 下方支撑点数量与 XY 排列上不同：

- `AsymmetricDualSupport`：2 个直接支撑；
- `OffsetSeam`：3 个直接支撑；
- `CriticalCorner`：4 个直接支撑。

它们共用 Carrier、两个石质 Payload、顶部位置及大部分尺寸规则。截图中的主要轮廓正是这些共用构件，局部支撑又容易在投影中遮挡，所以三种外观和击毁结果相近是当前算法的必然输出。

### 2.4 失效闭包被限定为顶部三块

`FABTSM73PostFailureValidator::GatherDescendants` 沿当前 `LowerNodeId -> UpperNodeId` 支撑边向上收集后代，第 179 行固定从 `CarrierNodeId` 开始。Carrier 位于最高处，所以闭包天然只有：

```text
Carrier + Payload 1 + Payload 2
```

主体是 Carrier 的祖先/下方支撑者，永远不会进入该闭包。`ABTSM73B2AutomationTests.cpp` 第 158 行还明确断言 `AffectedNodeIds.Num() == 3`。因此旧测试证明的是“冠部三块会倾覆”，不是“整栋主体会坍塌”。

### 2.5 旧质量门槛允许局部冠部通过

当前默认 `MinWeakCollapseRatio=0.02`。只要冠部三块达到整栋质量的 2%，就能进入弱点规划。该参数适合验证局部进展，却不适合“击中后主体发生明显级联”的新目标。

## 3. 相关研究与工程先例

下表区分“公开资料明确说明”和“本项目迁移推导”。访问日期均为 2026-07-25。

| 来源 | 公开资料能够确认的内容 | 可迁移到本项目的部分 | 不能声称的内容 |
| --- | --- | --- | --- |
| Stephenson、Renz 2016 | 从顶部开始递归向下增加支撑行；枚举分组及中部/两端支撑；拒绝重叠和局部失稳；最终结构可表示为 DAG；全局稳定仍需分析或模拟 | 接触支撑 DAG、局部支撑约束、廉价生成后再做全局物理验证 | 原论文不是“先生成 DAG 再图重写”，也没有实现弱点坍塌优化 |
| Stephenson、Renz 2017 | 弱点定义包含可直接击中、移除后影响大量方块/目标；加入结构鲁棒性、材料、TNT、Agent 可解性和难度验证 | 可达性、受影响质量、材料路径、模拟验证 | 原文主要会保护弱点；本项目反过来保留一个可读弱点并保护旁路 |
| Abdullah 等 2020 | 以 constrained novelty search 生成稳定且能由精确命中触发 domino effect 的 Angry Birds-like 关卡 | 把“完整稳定、命中后级联、多样性”作为并列约束和搜索目标 | 不是 DAG 建筑构造算法 |
| Dormans 2010/2011 | 用图语法递归生成 mission graph，再以 shape grammar 转为空间；规则可重复和递归 | 先生成逻辑承载图，再单独编译几何；图重写历史可调试 | 不包含刚体承重与破坏验证 |
| Müller 等 2006 CGA Shape | 用局部 Scope、Split、Repeat 和组件分解从体量递归细化到建筑构件 | 每个宏观节点带局部坐标、Envelope 和端口；递归产生轮廓多样性 | 只负责几何与风格，不保证物理稳定 |
| Whiting、Ochsendorf、Durand 2009 | 将结构可行性加入程序化砌体建筑；自动调节自由参数以得到结构可行形态 | 语法决定拓扑，约束求解器调尺寸、偏移和支撑域 | 面向压缩型砌体，不包含游戏弱点和动态坍塌 |
| Helms、Shea 2012 | 用面向对象图语法表达 Function–Behavior–Structure，并建议结合搜索与仿真评价 | 类型化节点/边、规则前后置条件、图语法与模拟分层 | 研究案例不是游戏建筑 |
| Shaker 等 2013 / Ropossum | 语法/进化生成物理谜题，再由物理引擎与 Agent 验证可玩性；模拟代价高 | 图约束 → 静力分析 → 少量 Chaos rollout 的三级漏斗 | 有限采样不是数学可解性证明 |
| Dungeon Architect Snap Flow | 官方文档明确使用 LHS→RHS 图规则、执行图、迭代和模块连接 | 与 `A -> A-B`、`A -> A+B` 接近的 UE 工程组织方式 | 模块图没有承重、损伤或坍塌语义 |
| Minecraft Jigsaw | 官方文档说明连接端口、模板池权重、递归深度、最大距离与地形投影 | 终结模块池、端口协议、深度预算、确定性变体和地形适配 | 不做刚体支撑 DAG 或弱点验证 |
| Epic Chaos | Geometry Collection 支持层级 Cluster；Fields 支持 Anchor、Strain、Damage Threshold、Sleep/Disable | 可作为最终动态破坏、休眠和表现层 | Chaos Cluster hierarchy 不是本项目 authored support DAG |

### 3.1 对 Stephenson–Renz 论文的准确理解

论文的实际生成过程是“先几何、后派生图”：

1. 选择顶部块型与 1–3 个峰；
2. 把当前最底行按距离枚举 subset partition；
3. 为每个 subset 尝试中央、两端、中央加两端三种下方支撑；
4. 拒绝重叠，以及不能从中央或两端稳定支撑原底行块的布局；
5. 选择一个合法新底行，重复到目标行数；
6. 最后分配材料并进行全局稳定验证。

论文指出成品可表示为 DAG，且这种表示可能帮助识别弱点，但没有实现“DAG 递归改写”或“弱点攻击后整体坍塌”。本项目的新方案是在它的支撑表示基础上向前推进：把承载图变成生成时的一等数据，而不是生成后的副产品。

### 3.2 商业游戏证据边界

公开资料能证明 Minecraft 等系统会递归拼装模块，也能证明 Dungeon Architect 会执行图规则，但不能证明它们按真实质量、接触面和破坏切面生成可摧毁建筑。对于 Angry Birds 原作、Bad Piggies、World of Goo 等商业游戏，也没有找到公开源码或技术文章证明其使用本方案。因此文档只作机制类比，不推测未公开内部算法。

## 4. 新系统的形式语义

### 4.1 统一方向约定

为兼容本项目现有 `FABTSM73SupportEdge`，物理支撑边统一为：

```text
Support -> Load
LowerNodeId -> UpperNodeId
Ground -> ... -> TopLoad
```

用户表达式仍按阅读顺序从上到下书写：

```text
A-B
```

表示 A 位于上方、B 从下方支撑 A；编译后的物理边是 `B -> A`。

```text
A+B
```

表示 A、B 是同层并行分支。加号只描述并行关系，并不自动说明两者是否缺一不可。

### 4.2 并联必须带支撑策略

`A+B` 至少需要以下语义之一：

| 策略 | 含义 | Gameplay 用途 |
| --- | --- | --- |
| `AllRequired` | 两支撑共同构成稳定接触域，少任意一支可能失稳 | 双支撑弱点、关键角 |
| `AnySufficient` | 任一支撑可独立承载 | 普通区冗余、提高耐打性 |
| `KOfN` | 至少保留 N 条中的 K 条 | 可调结构强度 |
| `Independent` | 各分支承载不同上层体量 | 双塔、分裂轮廓 |

否则“两个并排节点”可能只是视觉并排，无法判断击毁一支后应倒还是不倒。

### 4.3 基础递归规则

#### 串联拆分

```text
SerialSplit(N) -> NUpper-NLower
```

用途：增加高度、楼层、腰部接缝、错位承载和载荷汇聚深度。

约束：

- 两个子节点均能被最小砖块实现；
- 上下 Support Port 可匹配；
- 完整态 COM 在接触域内；
- XY 偏移不超过悬挑与摩擦预算；
- 展开前预留几何与弱点砖预算。

#### 并联拆分

```text
ParallelSplit(N) -> NLeft+NRight
```

用途：双柱、双塔、门洞、前后支撑和并行载荷路径。

约束：

- 指定分裂轴和并联策略；
- 子分支保留最小间隙；
- 若共同承载不可分割上部体量，显式生成 Carrier/Splitter 接口；
- 不得用隐式完整二分图制造未知旁路。

#### 汇聚与跨接

```text
Merge(A+B) -> (A+B)-Carrier
Bridge(A+B) -> (A+B)-Span-(C+D)
```

它们可作为语法糖，最终仍展开成有明确端口与支撑边的串/并联组合。

#### 终止

```text
Atom -> Column | Beam | Deck | WallFrame | CompositeRigidGroup
```

只有终止规则才产生真实砖块。递归节点数量不应直接等于 Chaos 刚体数量。

### 4.4 基准轮廓表达式

```text
独塔：A-B-C-D
拱门：A-(B+C)
门楼：Roof-(LeftPier+RightPier)
双塔桥：(UpperLeft+UpperRight)-Bridge-(LowerLeft+LowerRight)
偏置腰部：Upper-(WeakKey+StrongPivot)-Lower
```

轮廓 Preset 只确定低频拓扑和目标 Envelope。后续递归决定楼层数、分叉、开洞、错位和局部非对称。

## 5. 四层数据模型

### 5.1 推导树

推导树记录图规则的历史，而不是物理支撑关系：

```text
ExpressionNodeId
ParentExpressionNodeId
Operator = Atom | Series | Parallel | Merge
Children[]
RuleId
ExpansionDepth
DerivationPath
DeterministicPathSeed
RemainingBudget
```

随机数建议由 `BuildingSeed + DerivationPath + RuleId` 分层哈希产生，避免给上游插入一次展开后，后续共享 `FRandomStream` 的全部结果漂移。

### 5.2 蓝图承载 DAG

节点代表柱列、塔段、Carrier、桥段、上部体量等结构区域，不必是一块砖：

```text
MacroNodeId
SourceExpressionNodeId
SemanticRole
LocalFrame
LocalEnvelope
BottomSupportPorts[]
TopLoadPorts[]
SidePorts[]
NormalizedHeightRange
BayId
RigidGroupId
EstimatedMassRange
AllowedMaterials[]
MinimumRealizationCost
bMainBody
bWeaknessHelper
```

边代表设计意图：

```text
SupportMacroNodeId -> LoadMacroNodeId
InterfaceId
SupportPolicy
ExpectedContactPatch
ExpectedLoadShare
bRequired
bAllowedOptional
FailureFrontierId
```

### 5.3 砖块装配图

蓝图节点由编译器降低为现有 `FABTSM73BrickNode` 或未来装置节点，并补充：

```text
MacroNodeId
RigidGroupId
IntendedSupportInterfaceIds[]
CollisionShapeContract
MainBody | Helper | Decoration
```

一个宏节点可以编译为：

- 单个大砖刚体；
- 多块独立砖；
- 一个带多个可见网格的复合刚体组；
- 梁柱组合；
- 绳、链、炸药桶或活塞接口。

### 5.4 真实接触 DAG

最终几何、碰撞体、材质、缩放和 Transform 确定后，从实际接触重新建图：

```text
LowerModuleId -> UpperModuleId
ContactPatch
ContactNormal
ContactArea
SeparationOrPenetration
MatchedBlueprintInterfaceId
Classification
```

分类包括：

- `RequiredMatched`：必需接触存在；
- `OptionalMatched`：允许的辅助接触；
- `MissingRequired`：设计支撑未真实接触；
- `UnexpectedBypass`：绕过弱点切面的意外支撑；
- `HarmlessIncidental`：不改变载荷路径的轻微接触。

任何跨越目标 Failure Frontier 的 `UnexpectedBypass` 都应拒绝候选，而不是靠提高弱点伤害掩盖。

## 6. 从 DAG 到真实砖块

### 6.1 Envelope 与端口求解

每个宏节点有局部 Envelope 和上下端口。求解顺序建议从 Ground 向上确定承重点，再从关键 Carrier 向两侧分配宽度：

1. 根据目标占地、高度、轮廓 Preset 给根节点分配 Scope；
2. 执行递归规则并传播子 Scope；
3. 求解 Support Port 的中心、朝向、最小接触面积与间隙；
4. 选择终止原语和标准砖尺寸；
5. 必要时以少量尺寸连续变量调节，而不是任意缩放所有砖；
6. 生成碰撞尺寸后重新检测穿透和实际接触。

### 6.2 砖预算必须在规则应用前检查

每条规则声明：

```text
MinBrickCost
TypicalBrickCost
MaxBrickCost
ReservedWeaknessCost
```

只有满足以下条件才允许展开：

```text
CurrentCommittedCost
+ ChildMinimumCost
+ FoundationReserve
+ WeaknessReserve
<= MaxBrickCount
```

预算不足时应终止当前 Atom、换用更廉价规则或回溯，不应生成完才报 `BrickBudgetExceeded` 并让整栋消失。旧 `51:50` 等错误保留为 Legacy 回归样本；新路线验收“预算内优雅终止”。

### 6.3 材料不能最后无条件随机覆盖

木、石、铁、玻璃会改变质量、COM、惯量、摩擦、弹性和破坏门槛。材料流程应是：

```text
宏节点先声明 MaterialRole/AllowedMaterials
-> 空间求解用估计密度区间
-> 选定真实 M7 Material Profile
-> 重算质量与载荷
-> 必要时局部调节尺寸或换材
-> 再执行弱点和稳定性验证
```

不允许在 TipMargin 验证完成后任意把 Carrier 换成高密度铁块。

## 7. 内部主体弱点的生成

### 7.1 不再附加弱点模块

新流程采用“先找主体切面，再保护该切面”：

1. 生成完整宏观主体 DAG；
2. 枚举中下部 node cut、edge cut、dominator 和小型 minimum cut set；
3. 计算移除候选后失去 Ground 路径或静稳的主体质量；
4. 选出 Failure Frontier；
5. 对 Frontier 周围执行弱点化结构改写；
6. 后续递归不得生成跨越 Frontier 的旁路；
7. 砖块编译和真实接触重建后再次验证。

这里的“articulation-like”只作直觉描述。支撑图有多个 Ground 节点且为有向图，不能直接套普通无向割点。建议增加一个虚拟 `GroundRoot` 连接所有真实 Ground 节点，再对从 GroundRoot 到上部载荷的有向路径求 dominator 或有限大小 cut。

### 7.2 泛化 Failure Frontier

旧 B2 的单 `Candidate + Carrier + Payloads` 应泛化为：

```text
FailureFrontierId
CandidateNodeIds[]
CandidateEdgeIds[]
AffectedRootNodeIds[]
AffectedMainBodyNodeIds[]
FullSupportInterfaces[]
RemainingSupportInterfaces[]
ExpectedCollapseMode
ExpectedDirection
NormalizedWeaknessHeight
TargetAffectedMassRange
ProtectedCutId
```

Helper、Carrier 或人为添加的 Payload 质量不能单独满足“主体弱点”门槛。

### 7.3 三种无装置弱点的新定义

#### 内部单柱 `InternalSingleSupport`

- 中下部一个节点是大量上部主体到 GroundRoot 的 dominator；
- 该柱完整时有足够承载裕量；
- 破坏后上部至少一个主要分支失去 Ground 路径或发生 COM 越界；
- 弱柱周围不得存在隐藏接触旁路。

预期观感：上半部下落、偏转并撞击邻近分支，而不是只有柱顶小块掉落。

#### 内部非对称双支撑 `InternalAsymmetricDualSupport`

- 中部 Carrier 由一强一弱两条路径共同构成 `AllRequired` 支撑域；
- 完整时组合 COM 在完整 Hull 内；
- 弱侧移除后，剩余强侧仍提供转动枢轴，但 COM 越出剩余 Hull；
- 上部主体围绕强侧倾覆并产生二次碰撞。

它不是“任意两柱少一根”，而是完整态与移除态都经过反事实求解的双支撑。

#### 内部偏置接缝 `InternalOffsetSeam`

- 上下两个大体量在中腰处错位；
- 通过窄接缝/弱键和偏心支撑连接；
- 弱键破坏后，上部主体先滑移再倾倒；
- 下方不得有同构宽楼板直接承接原位置。

三种模式必须在 DAG 拓扑、归一化高度、受影响主体、倒塌方向和轮廓留白上均不同，而不是只切换支撑数量。

### 7.4 初版候选范围

建议初值，最终均暴露为参数：

```text
WeaknessNormalizedHeightRange = 0.15–0.60
MinMainBodyAffectedMassRatio = 0.20
TargetMainBodyAffectedMassRatio = 0.35–0.60
MaxSingleHitAffectedMassRatio = 0.75
MinAffectedHeightSpanNormalized = 0.25
MinAffectedMacroNodeCount = 2
MaxBypassSupportEdgeCount = 0
MinTipMarginCM = 8
MaxReseatRisk = 0.35
```

限制最高 20% 区域不是绝对规则；屋顶目标关卡可显式允许。但默认弱点若只影响最高小段，应直接拒绝。

## 8. 稳定性与反事实验证

### 8.1 验证漏斗

```text
抽象 DAG 合法性
-> Scope/端口/预算求解
-> 砖块与碰撞编译
-> 真实接触 DAG 与意图审计
-> 静态载荷和完整态稳定
-> 弱点移除反事实
-> 零穿透检查
-> Hidden Chaos Idle
-> 从 settled transforms 重建接触 DAG
-> 再次执行完整态/失效态验证
-> 少量弱点与非弱点攻击 rollout
-> 接受、回溯或变异
```

廉价图检查负责快速淘汰，Chaos 只验证少量高分候选，避免把生成成本拖到不可用。

### 8.2 静态载荷传播

不能简单把每条路径上的子树质量相加，否则多支撑 DAG 会重复计算上部质量。应对每个上部刚体只计算一次质量，再按 Contact Patch、COM 投影和支撑策略把载荷份额分给下层边。

至少检查：

- DAG 无环；
- 所有主体节点有 Ground 路径；
- Required 接口与真实接触匹配；
- 子结构组合 COM 位于 Contact Hull 内；
- 普通支撑的安全系数；
- 弱点完整态仍有正裕量；
- 最大细长比、悬挑、摩擦需求和相邻质量比；
- Failure Frontier 无意外旁路。

### 8.3 失效态指标

复用 B2 有价值的真实密度 COM、Contact Hull、`InitialSupportMargin`、`TipMargin` 和 `ReseatRisk`，但输入从单 Carrier 泛化为多个 Affected Root 和整个主体闭包。

新增：

```text
MainBodyAffectedMassRatio
AffectedHeightSpanNormalized
AffectedSilhouetteCoverage
AffectedMacroNodeCount
DominatorCoverage
BypassSupportEdgeCount
MinimumFreeTipAngleDegrees
MinimumFreeSlideDistanceCM
PredictedDropEnergy
PredictedSecondaryCollisionCount
```

图上断路不等于视觉上垮塌。还需检查：

- 前 10–20 度倾覆扫掠空间是否被邻砖卡死；
- 滑移模式是否有最小自由距离；
- 下落路径是否会被同构楼层原位接住；
- 弱点攻击收益是否明显高于普通攻击。

### 8.4 Idle 后必须重建接触图

当前系统会执行穿透预检、quiet-window，并在落座 Transform 上 Freeze，但生成期 `StructureData` 没有随预沉降后的实际 Transform 重建。新路线必须把 settled transforms 作为正式 Realized State：

1. 读取所有模块最终 Transform；
2. 重建真实接触边与 Contact Patch；
3. 检查是否出现新旁路或丢失 Required 接触；
4. 重算质量、COM、TipMargin 与 ReseatRisk；
5. 只有 settled 图仍通过才 Freeze 为正式发射初态。

## 9. 稳定、多样、可玩的联合目标

建议先以硬约束建立可行域，再用多目标排序保留差异，而不是把一切压成一个总分。否则搜索容易再次收敛为最宽、最矮、最对称的塔。

### 9.1 硬约束

- `IntactStable = true`；
- 穿透为零或在严格误差内；
- 所有主体有 Ground 路径；
- `BypassSupportEdgeCount = 0`；
- 弱点可从允许攻击方向到达；
- 弱点移除后的主体覆盖率达到最低门槛；
- 普通位置移除不得与弱点同样高效；
- 砖块、刚体、约束和生成时间不超预算。

### 9.2 多目标排序

- 稳定裕量；
- 弱点/非弱点效果差；
- 坍塌方向可读性；
- 目标受影响质量与目标区间距离；
- 二次碰撞潜力；
- 轮廓 Novelty；
- 拓扑签名 Novelty；
- 材料直方图差异；
- 资源与运行成本。

候选池可保留 Pareto 前沿或按轮廓类别分桶，不必只保留单一最高分。

### 9.3 Novelty 签名

建议签名至少包含：

```text
Macro DAG canonical hash
Series/Parallel rule histogram
Silhouette family
Branch count and depth
Opening ratio
Asymmetry
Weakness type and normalized height
Affected mass band
Expected collapse mode
Material histogram
```

## 10. 平面测试台与球面适配

结构递归内核不得直接查询星球中心或连续渲染 Mesh。它只在局部建筑坐标系工作：

```text
+Z = 建筑 Up
+X/+Y = 局部施工平面
```

落地仍复用原三层防曲面策略：

1. 占地范围校验；
2. 局部平坦施工台；
3. 自适应地基脚。

正式球面中，`CellTopo` 始终是地形和建筑 Anchor 的逻辑源；连续球面 Mesh 只提供渲染与物理表面。M7.1 平面测试台使用同一 `GroundAdapter` 接口退化为固定平面。

相同的：

```text
BuildingSeed + GeneratorVersion + Preset
```

在 M7.1 与球面 Anchor 上必须产生一致的推导树、宏观 DAG、局部砖块拓扑、材料和弱点；只允许 FoundationFoot 深度、BuildingPad 与世界 Transform 不同。

## 11. 与现有实现的迁移关系

### 11.1 可复用

- `ABTSM73GroundAdapter`：平面/球面 Anchor、Footprint、FoundationCap/Foot；
- M7 Material Profile：真实密度、摩擦、弹性和破坏成本；
- `WeakPointAnalysis`：射线暴露度、材料成本与非弱点效果对照；
- `PostFailureValidator`：质量/COM、Contact Hull、TipMargin、方向和 ReseatRisk 的数学思路；
- `StableBuildingActor`：编辑器预览、模块生成、Node→Module 映射、Idle quiet-window 和失败清理；
- `MaterialSystem::SpawnBrickModule`：现有 M6/M7 Chaos、损伤和破坏链路；
- 零穿透预检与 M7.1 平面实验台。

### 11.2 Legacy 保留

- `AddFourColumnStorey` 固定楼层；
- 三轮廓硬编码 `switch`；
- `WeaknessStructureBuilder` 的 HighestDeck 冠部；
- 单 Carrier 加两个 Payload 的失效边界；
- `AffectedNodeIds.Num()==3` 的 B2 断言。

这些内容不删除，用于旧地图兼容、视觉对照和回归测试。

### 11.3 建议兼容开关

```text
GenerationAlgorithm = LegacyLayeredAB2 | RecursiveSupportDAG
GeneratorVersion
```

已有 Actor 默认继续使用 Legacy，避免序列化实例突然变化；新测试 Actor 显式选择 `RecursiveSupportDAG`。新 DAG 最终仍编译到 `FABTSM73StructureData`，尽量减少 M6/M7 运行时链路改动。

### 11.4 模块拆分建议

`AABTSM73StableBuildingActor.cpp` 已接近项目 600 行拆分门槛，DAG 逻辑不要继续塞入 Actor。建议：

```text
FABTSM73DAGGrammarExpander
FABTSM73DAGLayoutSolver
FABTSM73ModuleCompiler
FABTSM73ContactGraphBuilder
FABTSM73ContactIntentAuditor
FABTSM73InternalWeaknessPlanner
FABTSM73FailureFrontierValidator
FABTSM73CandidateSearch
FABTSM73RuntimeIdleValidator
```

纯数据生成、World 查询、Actor 生成与 Chaos 验证必须分离。

## 12. 建议编辑器参数

### DAG Generation

```text
GenerationAlgorithm
GeneratorVersion
BuildingSeed
SilhouettePreset / SilhouetteWeights
TargetFootprintCM
TargetHeightCM
MinExpansionDepth
MaxExpansionDepth
ExpansionStepBudget
SeriesRuleWeight
ParallelRuleWeight
OffsetRuleWeight
MergeRuleWeight
SymmetryBias
TaperRange
OpeningRatioRange
MaxLateralOffsetCM
```

### Budget

```text
MaxAbstractNodeCount
TargetGameplayBrickCount
MaxGameplayBrickCount
ReservedWeaknessBrickCount
MaxRigidBodyCount
GenerationAttemptBudget
CandidatePoolSize
```

### Structure

```text
MinSupportContactAreaRatio
MinInitialSupportMarginCM
MinOrdinarySafetyFactor
MaxSlendernessRatio
MaxOverhangCM
ContactSnapToleranceCM
MaxUnexpectedContactAreaCM2
ParallelSupportPolicyWeights
```

### Internal Weakness

```text
WeaknessPlacementMode = InternalFailureFrontier
WeaknessPatternWeights
WeaknessNormalizedHeightRange
TargetMainBodyAffectedMassRatio
MinMainBodyAffectedMassRatio
MaxSingleHitAffectedMassRatio
MinAffectedHeightSpanNormalized
MinAffectedMacroNodeCount
MaxBypassSupportEdgeCount
MinTipMarginCM
MaxReseatRisk
MinFreeTipAngleDegrees
MinFreeSlideDistanceCM
MinWeakPointExposure
ExpectedCollapseDirectionConeDegrees
MaxWeaknessHelperMassRatio
```

### Search / Validation

```text
StaticValidationBudgetMS
IdleValidationSeconds
IdleStableHoldSeconds
IdleValidationMaxSeconds
bRebuildContactsAfterIdle
WeakAttackRolloutCount
NonWeakProbeCount
RepresentativeImpulse
MinimumWeakResponseCM
MinimumWeakResponseDegrees
MaximumNonWeakCollapseRatio
NoveltyWeight
```

## 13. Debug 可视化与日志

编辑器预览层级：

```text
ExpressionTree
MacroDAG
PlacedModules
RealizedContactDAG
FailureFrontier
```

建议显示：

- 规则路径、节点 ID 和规范拓扑 Hash；
- Required/Optional/Missing/Bypass 接触；
- 主体与 Helper 分类；
- 完整态与失效态 COM、Contact Hull；
- Failure Frontier 与受影响主体；
- 预测倾覆箭头、自由扫掠空间；
- 预算消耗、拒绝原因和候选排名。

推荐日志：

```text
[ABTS][M7.3-DAG][Expand] Seed=... Version=... Rule=... Path=... Nodes=... Budget=...
[ABTS][M7.3-DAG][Compile] Macro=... Modules=... RigidBodies=...
[ABTS][M7.3-DAG][Contact] Required=... Missing=... Bypass=...
[ABTS][M7.3-DAG][Weakness] Frontier=... Height=... MainBodyMass=... Span=...
[ABTS][M7.3-DAG][Idle] Settled=... ContactHashBefore=... ContactHashAfter=...
[ABTS][M7.3-DAG][Attack] Type=Weak/NonWeak Collapse=... Move=... Rotation=... Secondary=...
[ABTS][M7.3-DAG][Reject] Stage=... Reason=...
[ABTS][M7.3-DAG][Accepted] TopologyHash=... Score=... Novelty=...
```

## 14. 分阶段实施路线

### M7.3-DAG-1：纯数据 IR 与递归语法

> 工程实现、确定性 Seed、预算终止、自动化测试与验收见 [M73DAG1RecursiveGrammarImplementationDesign.md](M73DAG1RecursiveGrammarImplementationDesign.md)。

- 实现 Series/Parallel 表达式、支撑策略与推导树；
- 固定生成三种基准表达式；
- 实现深度、节点、规则步数和预估砖预算；
- 输出 Macro DAG 和 Debug 文本；
- 暂不替换运行时建筑。

验收：

```text
A-B-C-D
A-(B+C)
(A+B)-C-(D+E)
```

产生不同、无环、可复现的规范拓扑 Hash。

### M7.3-DAG-2：空间求解与模块编译

- 实现 Envelope、Support Port 和 Carrier 接口；
- 终止节点铺砖；
- 从碰撞几何反建 Contact DAG；
- 审计设计接触与真实接触；
- 输出兼容现有 `FABTSM73StructureData`；
- 在 M7.1 通过空载稳定。

本阶段先不生成弱点，避免同时调试语法、几何和破坏。

### M7.3-DAG-3：内部 Failure Frontier

- 枚举内部 dominator、node/edge cut 和小型 cut set；
- 实现三种内部弱点结构改写；
- 泛化 B2 Validator；
- 加入主体质量、跨度、扫掠空间和旁路门槛；
- 新路线不再使用“三节点顶部闭包”验收。

### M7.3-DAG-4：Settled Contact 与攻击对照

- Idle 后重建接触 DAG；
- 对弱点和普通位置执行代表性 Chaos rollout；
- 记录位移、旋转、传播深度、坍塌质量、方向与二次撞击；
- 建立固定 Seed 回归库。

### M7.3-DAG-5：候选搜索、装置与 TaskGraph

- 候选池、回溯、变异和 Novelty Archive；
- 再接绳、链、炸药桶和弹簧活塞；
- 接入 TaskGraph 难度、道路距离、弹弓射界与坍塌安全区；
- 在正式球面 CellTopo Anchor 上批量生成。

## 15. 自动化与人工验收

建议自动化：

```text
ABTS.M73DAG.ExpressionSemantics
ABTS.M73DAG.RecursiveExpansionDeterminism
ABTS.M73DAG.BudgetTermination
ABTS.M73DAG.ContactIntentAudit
ABTS.M73DAG.InternalFailureFrontier
ABTS.M73DAG.MaterialHeightMatrix
ABTS.M73DAG.IdleSettleRevalidation
ABTS.M73DAG.PlanarSphericalTopologyEquivalence
ABTS.M73DAG.WeakVsNonWeakRollout
ABTS.M73DAG.NoveltyBatch
```

核心断言：

- 三种基准表达式的规范拓扑签名不同；
- 相同 Seed/Version 输出完全复现；
- 预算不足时终止或回退，不清空整栋；
- 弱点默认不在最高 20% 且至少影响两个宏节点；
- `AffectedMainBodyNodeIds` 不能只有 Helper/Carrier/Payload；
- 主体受影响质量、建筑高度跨度达到门槛；
- `BypassSupportEdgeCount=0`；
- 完整态稳定，失效态 Tip/Slide 指标通过；
- Idle settled 后仍无新旁路；
- 弱点攻击效果显著大于若干普通位置攻击；
- 木/石/铁/玻璃矩阵使用真实 Profile，不靠降低硬门槛兼容；
- M7.1 与球面模式的局部结构拓扑一致；
- 旧 `ABTS.M73A/B/B2` 继续验证 Legacy 路线。

人工验收不能只看红色弱点高亮。至少观察：

1. 三种内部弱点的主楼轮廓和承载路径明显不同；
2. 击中弱点后，中上部主体产生倾倒、滑移或二次撞击；
3. 不再只掉顶部一小段；
4. 击中普通柱/板仍有局部反馈，但收益明显较低；
5. 完整建筑在开始物理模拟时不弹飞、不自倒；
6. 坍塌方向与留白、材料和弱点位置能够被玩家读懂。

## 16. 风险与回退

| 风险 | 症状 | 处理 |
| --- | --- | --- |
| 抽象图正确、真实碰撞有旁路 | 弱点击碎后建筑仍站立 | 必须从最终 Collision 重建 Contact DAG，跨 Frontier 旁路直接拒绝 |
| 串并联规则产生大量不可解几何 | 回溯次数爆炸 | 端口契约、最小尺寸与预算前置；先限制为正交盒体和少数宏规则 |
| 搜索只剩宽底矮塔 | 稳定但外观重复 | 稳定作为硬门槛；Novelty 和轮廓分桶，不只用单一总分 |
| 弱点过低导致一击清屏 | 全楼瞬间散架 | 限制最大受影响质量；优先 Tip/Slide 和二次碰撞，不追求全部断路 |
| 弱点仍是假弱点 | 图断路但上部原位落座 | 增加扫掠空间、自由滑动距离、ReseatRisk 和 Chaos rollout |
| 材料替换破坏静稳 | Iron/Stone 改变 COM 后自倒 | 材料进入求解循环，换材后重跑全部验证 |
| Idle 改变接触拓扑 | Freeze 后弱点旁路出现 | settled transforms 重建 Contact DAG 并复验 |
| 球面适配污染语法 | 平面通过、球面拓扑变形 | DAG 只输出局部结构；曲率由 GroundAdapter/Pad/Foundation 处理 |
| 刚体数和验证时间超预算 | 大量候选卡顿 | 宏节点不等于刚体；三级漏斗；只对高分候选运行 Chaos |

若新路线尚未完成几何编译，继续保留 Legacy 生成用于 M7.1/M6 联调；不要半迁移后让旧地图失去可用建筑。

## 17. 最终建议

下一步不应继续调整当前 B2 顶冠的尺寸、材料或支撑位置。这些调整只能改变顶部局部坍塌，无法让它成为主体的承载瓶颈。

推荐首先落实 `M7.3-DAG-1`：只做纯数据表达式、递归规则、类型化支撑策略、规范拓扑 Hash 和预算终止。确认 `A-B-C-D`、`A-(B+C)`、`(A+B)-C-(D+E)` 能稳定生成不同 DAG 后，再进入几何编译。这样可把最难的三类问题拆开：

```text
先验证图语法是否正确
-> 再验证图能否编译成无穿透稳定砖块
-> 最后验证内部弱点是否真的触发主体级联
```

本项目最终有价值的 PCG 核心应当是：生成器不仅知道每块砖在哪里，还知道它属于哪个宏观结构、承担哪条载荷路径、为什么是普通支撑或弱点、击毁后哪部分主体会以什么方向垮塌，以及真实 Chaos 是否兑现了这个设计意图。

## 18. 参考资料

### 物理谜题与 Angry Birds PCG

1. Matthew Stephenson, Jochen Renz. *Procedural Generation of Complex Stable Structures for Angry Birds Levels*. IEEE CIG 2016. [DOI](https://doi.org/10.1109/CIG.2016.7860410) · [作者公开 PDF](https://users.cecs.anu.edu.au/~jrenz/papers/stephenson-renz-cig16.pdf)。
2. Matthew Stephenson, Jochen Renz. *Generating Varied, Stable and Solvable Levels for Angry Birds Style Physics Games*. IEEE CIG 2017. [DOI](https://doi.org/10.1109/CIG.2017.8080448) · [作者公开 PDF](https://users.cecs.anu.edu.au/~jrenz/papers/stephenson-renz-cig17.pdf)。
3. Febri Abdullah, Pujana Paliyawan, Ruck Thawonmas, Fitra A. Bachtiar. *Generating Angry Birds-Like Levels With Domino Effects Using Constrained Novelty Search*. IEEE CoG 2020. [DOI](https://doi.org/10.1109/COG47356.2020.9231547)。
4. Lucas Ferreira, Claudio Toledo. *A Search-Based Approach for Generating Angry Birds Levels*. IEEE CIG 2014. [DOI](https://doi.org/10.1109/CIG.2014.6932912)。
5. Noor Shaker, Mohammad Shaker, Julian Togelius. *Evolving Playable Content for Cut the Rope through a Simulation-Based Approach*. AIIDE 2013. [DOI](https://doi.org/10.1609/aiide.v9i1.12690)。
6. Noor Shaker, Mohammad Shaker, Julian Togelius. *Ropossum: An Authoring Tool for Designing, Optimizing and Solving Cut the Rope Levels*. AIIDE 2013. [DOI](https://doi.org/10.1609/aiide.v9i1.12611)。

### 图语法、形状语法与结构生成

1. Joris Dormans. *Adventures in Level Design: Generating Missions and Spaces for Action Adventure Games*. PCGames 2010. [DOI](https://doi.org/10.1145/1814256.1814257) · [公开 PDF](https://www.pcgworkshop.com/archive/dormans2010adventures.pdf)。
2. Joris Dormans. *Level Design as Model Transformation*. FDG 2011. [DOI](https://doi.org/10.1145/2000919.2000921)。
3. Pascal Müller, Peter Wonka, Simon Haegler, Andreas Ulmer, Luc Van Gool. *Procedural Modeling of Buildings*. SIGGRAPH 2006. [DOI](https://doi.org/10.1145/1141911.1141931)。
4. Emily Whiting, John Ochsendorf, Frédo Durand. *Procedural Modeling of Structurally-Sound Masonry Buildings*. SIGGRAPH Asia 2009. [项目页](https://people.csail.mit.edu/ewhiting/projects/siggasia09.html) · [DOI](https://doi.org/10.1145/1618452.1618458)。
5. Bergen Helms, Kristina Shea. *Computational Synthesis of Product Architectures Based on Object-Oriented Graph Grammars*. Journal of Mechanical Design 2012. [DOI](https://doi.org/10.1115/1.4005592)。
6. Francesco Cascone, Diana Faiella, Valentina Tomei, Elena Mele. *A Structural Grammar Approach for the Generative Design of Diagrid-Like Structures*. Buildings 2021. [DOI](https://doi.org/10.3390/buildings11030090)。
7. Zhaoyin Jia, Andrew Gallagher, Ashutosh Saxena, Tsuhan Chen. *3D-Based Reasoning with Blocks, Support, and Stability*. CVPR 2013. [DOI](https://doi.org/10.1109/CVPR.2013.8) · [公开 PDF](http://www.cs.cornell.edu/~asaxena/papers/rgbd-segmentation-3d-reasoning-cvpr13.pdf)。
8. Thomas Lengauer, Robert Endre Tarjan. *A Fast Algorithm for Finding Dominators in a Flowgraph*. TOPLAS 1979. [DOI](https://doi.org/10.1145/357062.357071)。
9. Jacobo Valdes, Robert E. Tarjan, Eugene L. Lawler. *The Recognition of Series Parallel Digraphs*. SIAM Journal on Computing 1982. [DOI](https://doi.org/10.1137/0211023)。

### 可核验的工程参考

1. Dungeon Architect. [Snap Flow：Graph Grammar](https://docs.dungeonarchitect.dev/unreal/snap-flow/snapflow-design-graph/)。
2. Dungeon Architect. [Snap Grid Flow](https://docs.dungeonarchitect.dev/unreal/snap-grid-flow/sgf-introduction/) · [Flow Graph 优化](https://docs.dungeonarchitect.dev/unreal/snap-grid-flow/sgf-optimize-flow-graph/)。
3. Microsoft Learn. [Introduction to Jigsaw Structures](https://learn.microsoft.com/en-us/minecraft/creator/documents/structures/introductiontojigsawstructures?view=minecraft-bedrock-stable)。
4. SideFX. [Labs Building Generator 4.0](https://www.sidefx.com/docs/houdini/nodes/sop/labs--building_generator-4.0.html)。
5. Epic Games. [Chaos Geometry Collection Clustering](https://dev.epicgames.com/documentation/en-us/unreal-engine/cluster-geometry-collections-user-guide-in-unreal-engine) · [Chaos Fields](https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-fields-user-guide-in-unreal-engine)。
