# M7.3：长条形积木建筑生成调研与演进方案

> 文档性质：DAG5-B v2 之后的结构编译层调研稿；方案已被采纳，当前由
> [M7.3-Beam-A 结构 IR 与编辑器预览](M73BeamAStructuralIRPreviewDesign.md) 开始分阶段实现，
> 尚未接管 TaskGraph 生产链。
>
> 研究问题：如何用固定截面、离散长度的长条积木生成三维轻型建筑，同时兼容现有
> Shape Grammar + WFC 语义轮廓、Expanded DAG、弱点规划、真实 Brick 和 Chaos 验证。
>
> 上游：[DAG5-B v2 复杂轮廓预览](M73DAG5Bv2ComplexSilhouettePreviewDesign.md) ·
> [轮廓约束递归 DAG 演进设计](M73EnvelopeConditionedRecursiveDAGGenerationEvolutionDesign.md)。
>
> 下游复用：[DAG2.3 累计荷载与联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md) ·
> [DAG3 内部 Failure Frontier](M73DAG3InternalFailureFrontierDesign.md) ·
> [DAG-4 动态认证](M73DAG4SettledContactAndAttackRolloutDesign.md)。
>
> 总导航：[M7 建筑系统文档导航、模块清单与执行路线](M7BuildingDevelopmentRoadmap.md)。
>
> Beam-A v2 决策：首版“共同端点杆系方框”只保留为历史验证；当前结构 IR 改用固定截面、
> 可变长度的 XYZ 长条积木和 Member-to-Member Bearing Contact。屋顶首版不使用斜杆，而以
> X/Y 交替、逐层收分的水平积木拟合 Prism/Pyramid。

## 1. 结论摘要

不能把现有 `Plate` 简单替换成若干平行木条，也不能让 WFC 直接逐格选择最终物理木条。
前者仍然保留“逐层楼板”的隐藏拓扑，后者会把三维连接、跨距、荷载和弱点可玩性全部塞进
局部邻接约束，迅速造成无解或生成看似连通、实际不可承重的结构。

建议采用以下分层链路：

```text
Shape Grammar
  -> Chunk / Semantic Volume / MustVoid / 外形轮廓
  -> 局部语义 WFC：结构区域、空洞、边界端口、结构家族
  -> Structural Bay Graph：结构跨、节点带和允许连接
  -> Beam Motif WFC：在每个有限 Bay 中选择梁式结构基元
  -> Beam Graph Grammar：对 Joint / Member / Assembly 做受限拓扑改写
  -> Bounded Member Selection：从有限候选杆中选择必要构件
  -> Load DAG Extraction：从真实搭接和重力方向提取承重 DAG
  -> DAG2.3 荷载、DAG3 弱点、DAG-4 Chaos
  -> 最终 Brick Assembly
```

核心变化是同时维护两张图：

1. **Beam Assembly Graph**：无方向或混合方向，描述木条、节点、交叉、搭接和装配；
2. **Load DAG**：按重力方向描述荷载如何传到 Ground，供现有承重与弱点链使用。

三维梁结构不是楼板树。它允许一根横梁同时承载多根立柱，也允许一组垂直于横梁的次梁
把荷载传给两侧框架。几何连接图可以有横向闭环，但首版权威 Load Graph 仍须保持 DAG。

## 2. 为什么平板递归在三维梁结构上失效

### 2.1 Plate 天然携带二维连续支撑语义

一块平板隐含了以下能力：

- 板面内任意位置都能成为上层柱脚；
- 荷载可以在板面内向多个支点重新分配；
- 沿 X/Y 切分后，子板仍然是合法承重构件；
- 板的接触面大，Chaos 中对轻微位置误差不敏感。

因此旧链可以使用：

```text
Plate
  -> Split Plate
  -> Fit Footprint
  -> 在重心附近补柱
```

### 2.2 Beam 只在有限节点和有限跨内承重

长条木梁没有连续二维支撑域。上层构件必须落在：

- 梁端；
- 梁上的显式承重点；
- 两梁交叉形成的搭接节点；
- 柱头或经过认证的悬臂根部。

一根梁沿长度“一分为二”后还必须补接头；沿宽度切分则会改变截面，不再属于同一种积木。
因此梁结构的递归对象必须从“实体盒子”改成“节点—杆件—装配体”。

### 2.3 第三个维度不能只靠完整楼板掩盖

二维 Angry Birds 中，横板天然垂直于画面，所有承重点都落在同一平面。扩展到三维后至少有：

- X 向主梁；
- Y 向主梁或次梁；
- Z 向柱；
- 可选 XZ/YZ 斜撑；
- X/Y 梁交叉时的上下层序；
- 前后两排框架之间的横向稳定构件。

如果只生成“每层一组 X 梁 + 竖柱”，从侧面看仍会退化成二维结构；如果 X/Y 梁无规则
交叉，又会产生穿透、悬空接触和不明确的荷载方向。

## 3. 相关研究与可借鉴结论

### 3.1 Angry Birds 稳定结构生成

Stephenson 与 Renz 的 Angry Birds 结构生成器直接用多种二维物体构造复杂稳定结构，
并用结构属性形成适应度反馈，而不是依赖预制整栋模板。这证明“有限积木词汇 +
构造式生成 + 后验评价”适合物理破坏谜题，但其物体关系仍主要是二维层叠，不能直接解决
本项目的三维跨向问题。[论文与 PDF](https://users.cecs.anu.edu.au/~jrenz/papers/stephenson-renz-cig16.pdf)

Jiang 等使用建筑构造语法生成中式/日式 Angry Birds 结构，说明风格化结构可以由
可解释的局部构造规则产生，而不必让搜索器从所有砖块排列中盲搜。
[论文](https://arxiv.org/abs/1604.07906)

近年的稳定结构生成仍普遍采用“生成后验证/修复”：GAN 工作将关卡编码成网格再解码为
物体，强调稳定结构的数据表示；稳定化工作则检测局部间隙并修补。
这支持本项目继续保留 DAG5-A 有界候选与 fail-closed，而不是要求一次 WFC 永远成功。
[稳定结构 GAN](https://arxiv.org/abs/2309.02614) ·
[生成后稳定化](https://cspages.ucalgary.ca/~richard.zhao1/publications/2025aiide-stabilizing_angry_birds_levels.pdf)

### 3.2 桁架、空间框架和梁格

桁架设计研究通常先把结构表示成 Joint 与 Member 的图，再通过语法规则插入节点、增加或
翻转杆件。相关工作展示了以三角形细分、区域伸缩和对角线翻转作为可解释规则，并把规则
前置条件用于维持结构稳定性。这比“递归切实体”更适合作为长条积木的拓扑生成模型。
[ASME Truss Design Grammar 案例](https://doi.org/10.1115/1.4070095) ·
[空间杆系 Shape Grammar](https://www.jcad.cn/article/id/3049fcf5-2408-4020-b971-479343f7686c)

Ground Structure Method 的共同思想是：先在允许区域内建立一张有限候选杆图，再根据
结构目标选择或删除杆件。工程研究也指出，纯优化常产生难以制造的过度复杂结构，因此
需要显式复杂度和可制造性控制。这适合转化为本项目的“有限候选梁 + 离散长度 +
构件预算”，但不适合直接运行昂贵的连续拓扑优化。
[桁架布局与复杂度控制](https://www.mdpi.com/2076-3417/14/18/8157) ·
[非凸区域 Ground Structure](https://pubs.cstam.org.cn/article/doi/10.7511/jslx20170710003)

梁格（grillage）用相互垂直的主梁和横梁替代完整平板，是从当前 Plate 过渡到 Beam 的
直接工程类比。相关研究把梁格作为平板的轻量替代，并发现支柱位置和局部梁尺寸必须联合
设计，不能先定整板再机械替换为木条。
[优化梁格楼盖研究](https://eprints.whiterose.ac.uk/id/eprint/231908/)

空间框架则以三维节点和互锁杆件形成轻型刚性结构。对游戏最有用的不是照搬工程规格，
而是“通过三角形/四面体单元获得横向刚度”的原则：只有竖柱和正交横梁的矩形框会成为
易侧移机构，必须有斜撑、刚接节点或三维闭合单元。

### 3.3 木垛、交叉梁与互承结构

交替 X/Y 方向叠放木条的 `wood crib/box crib` 是与用户示意图最接近的真实结构：
上下层只在若干交叉点承压，通过交错方向形成开放但稳定的承重体。工程资料表明，其容量
和刚度受交叉接触点数量、接触面积和木材属性影响。
[CDC 木垛工程方法](https://stacks.cdc.gov/view/cdc/206317/cdc_206317_DS1.pdf)

互承框架（reciprocal frame）让每根梁既承载邻梁、又被邻梁承载，可以用短构件跨越更大
范围并形成很轻的轮廓。但它的荷载关系天然成环，而且力主要通过中间搭接点的弯曲和剪切
传递，几何与接头求解明显比普通端部支撑复杂。
[互承框架行为综述](https://www.structuremag.org/article/reciprocal-frame-structures/) ·
[大型木构互承框架](https://www.nature.com/articles/s41467-025-66491-4)

这意味着首版不应生成真正的互承闭环。后续若需要，应把一个已通过专门求解和 Chaos
认证的互承环压缩成单个 `ReciprocalAssembly`，再接入外部 Load DAG。

### 3.4 WFC 的正确角色

WFC 适合传播有限邻接和端口相容性，也支持外部约束与三维 Tile；但维度和 Tile 数增长会
迅速增加求解成本。原始实现也明确把邻接约束、约束合成和 3D 扩展作为局部模型处理。
[mxgmn/WaveFunctionCollapse](https://github.com/mxgmn/WaveFunctionCollapse)

因此本项目的 WFC 应选择“结构 Motif 和边界 Port”，不能直接决定每根最终物理梁，也
不能替代全局 Ground 可达、重心、跨距、弯曲或弱点可玩性验证。

### 3.5 游戏应用启示

这些游戏不是程序生成建筑算法的直接样板，但它们验证了玩家如何阅读离散物理构件：

| 游戏 | 可借鉴点 | 不应照搬 |
|---|---|---|
| World of Goo | 少量节点和杆件即可让支撑、摆动和坍塌方向可读 | 主要是二维、柔性连接 |
| Poly Bridge | 梁、支点、资源预算和模拟结果形成清晰闭环 | 玩家手工设计，不解决自动生成 |
| Besiege | 大量离散部件可组合为可破坏机器，部件功能和连接方式需要明确 | 目标是机器装配，不是静态建筑 PCG |
| Bad Piggies | 少量标准件、有限连接位和多次试验能产生丰富物理结果 | 通常使用规则连接网格，结构尺度较小 |

官方资料分别将它们描述为物理建造、桥梁模拟、Block 机器和零件载具系统：
[World of Goo](https://store.steampowered.com/app/22000/World_of_Goo/) ·
[Poly Bridge](https://store.steampowered.com/app/367450/Poly_Bridge/) ·
[Besiege](https://www.besiegethegame.com/) ·
[Bad Piggies](https://www.rovio.com/games/bad-piggies/)。

对本项目最重要的共同点是：玩家必须能看见承重点、杆件方向和结构冗余。过度优化但视觉
不可读的杆系不适合作为 Angry Birds 式目标建筑。

## 4. 建议的数据表示

### 4.1 Beam Joint

```text
FBeamJoint
  StableJointId
  LocalPosition
  AllowedAxisMask
  JointRole
    GroundFoot
    ColumnHead
    BeamEnd
    CrossBearing
    CantileverRoot
    BraceNode
    ChunkPort
  ContactLane
  MaxIncidentMembers
  Required / Optional
```

`ContactLane` 是三维梁式生成不可缺少的字段。X 梁、Y 梁和柱不能都占据同一中心点；
它们需要约定微小但确定的 Z/X/Y 层偏移，使碰撞面真实搭接而不是互相穿透。

### 4.2 Beam Member

```text
FBeamMember
  StableMemberId
  JointA / JointB
  AxisClass = X / Y / Z / DiagonalXZ / DiagonalYZ
  LengthClass = Short / Medium / Long
  CrossSectionClass
  MemberRole
    Post
    PrimaryBeam
    SecondaryBeam
    Tie
    Brace
    Cantilever
    WeakLink
  SupportMode
  MaterialProfile
```

首版应保持固定或极少数离散截面，只允许离散长度，不允许对每根木条任意非均匀缩放。

### 4.3 Beam Assembly

```text
FBeamAssembly
  AssemblyId
  MotifType
  JointSet
  MemberSet
  BoundaryPorts
  InternalLoadSummary
  bMayCondenseToDAGNode
```

Assembly 是连接 WFC 与 DAG 的中间层。它不是预制整栋建筑，而是可验证的小型结构词：

- `PostAndLintel`：两柱一梁；
- `PortalFrame`：门式框架；
- `CrossBeamBay`：X/Y 正交搭接梁；
- `TwoLayerCrib`：两层交错木垛；
- `LadderFrame`：两根主梁加若干横梁；
- `TransferFrame`：上部多个承点汇入下部少数支点；
- `CantileverBay`：带明确根部的悬挑；
- `BracedBay`：带 XZ/YZ 斜撑的稳定跨；
- `BridgeBay`：跨 MustVoid 的长梁组合；
- `ReciprocalAssembly`：后续阶段，首版禁用。

## 5. 与 Shape Grammar + WFC 的接法

### 5.1 Shape Grammar 继续只决定低频轮廓

现有 DAG5-B v2 的 `Box / Prism / Pyramid` 仍然是语义外包络，不直接变成实心物理块。
每个终端 Volume 输出：

```text
AllowedStructuralDomain
MustOccupy
MayOccupy
MustVoid
Top / Bottom / X± / Y± Ports
PreferredFrameAxis
RoofTerminal
```

Prism/Pyramid 只决定屋顶梁的高度场和允许区域，不直接作为一块承重棱柱/棱锥。

### 5.2 WFC 从形体选择升级为 Motif 与 Port 选择

每个有限 Bay 的 WFC Domain 建议包含：

```text
Empty
PostAndLintelX
PostAndLintelY
CrossBeam
CribXOverY
CribYOverX
PortalX
PortalY
BraceXZ
BraceYZ
CantileverX±
CantileverY±
TransferX
TransferY
RoofRafterX
RoofRafterY
```

Tile 边界不是颜色，而是结构端口签名：

```text
MemberAxis
JointLane
LoadCapacityClass
RequiresSupportBelow
ProvidesSupportAbove
MustContinue
MustTerminate
VoidClearance
```

WFC 只保证局部可拼接。Collapse 后必须把相邻 Tile 的同名 Port 合并成统一 Joint，并由
后续图验证处理全局结构。

### 5.3 两级 WFC，避免状态爆炸

```text
Volume WFC
  决定 Box/屋顶语义、结构家族和空洞

Bay Motif WFC
  在单个 Chunk 内决定梁式 Motif 和端口
```

不得构造一个覆盖整栋建筑、同时包含外形、材质、梁长、接头、弱点和物理状态的超级
WFC Tile Set。

## 6. Beam Graph Grammar

梁式递归应直接改写图，而不是切分实体：

| 规则 | 图变换 | 主要前置条件 |
|---|---|---|
| `SubdivideMember` | 一根长梁变为两根短梁 + 中间 Joint | 中间 Joint 有支撑或允许接头 |
| `PostPairToPortal` | 两个柱头之间增加横梁 | 跨距、净空和长度档可行 |
| `BeamToGrillage` | 主梁复制为平行梁并加入横向次梁 | 宽度足够、MustVoid 不被跨越 |
| `AlternateCribLayer` | 在上层加入垂直于下层的两根梁 | 至少形成两个有效交叉承点 |
| `AddTransferBeam` | 多个上部 Joint 汇入一根下部梁 | 合力点落入支撑区间 |
| `AddCantilever` | 梁端越过外侧支点 | 根部力矩和悬挑比在预算内 |
| `TriangulateBay` | 矩形框增加一根斜撑 | 攻击走廊和 MustVoid 允许 |
| `ForkFrame` | 一个框架向 X/Y 分叉成两个子框架 | 两个分支都有 Ground 路径预算 |
| `MergeFrame` | 两个上游框架汇聚到共同承重梁 | 接触区、梁长与累积荷载可行 |
| `TerminatePort` | 封闭不再继续的可选端口 | 不破坏 Required Port |

每条规则必须声明：

```text
MinJointCost
MinMemberCost
MinBrickCost
RequiredPorts
CreatedPorts
ForbiddenVoidIntersection
LoadDAGDelta
WeaknessReserve
```

这样 DAG5-A 可以在执行规则前做成本预检，而不是递归很深后才发现 Brick 超预算。

## 7. 从 Beam Assembly Graph 提取 Load DAG

### 7.1 支撑边来自显式 Bearing，不来自 AABB 碰巧相交

每个上部构件必须记录一个或多个 `BearingContact`：

```text
SupportedMember
SupportingMemberOrGround
ContactCenter
ContactNormal
ContactArea
AlongMemberParameter
LoadShare
```

只有设计接触能够进入 Load DAG。碰撞审计发现的额外接触需要标记为意外旁路，不能悄悄
提高静态稳定性。

### 7.2 横梁的荷载分配

对于由两个或更多支点承载的横梁：

1. 累计梁自重和所有上部 Joint 的点荷载；
2. 计算合力位置；
3. 检查合力是否落在外侧支点区间或支撑凸包内；
4. 按支点位置计算确定性的近似反力；
5. 把反力沿 Load DAG 传给下层构件。

这把 DAG2.3 的“Plate 合力点 + 联合支柱”推广为“Beam 支点区间 + 多点反力”。

### 7.3 首版禁止真实互承环

普通交叉梁必须有明确上下顺序，例如：

```text
Y Secondary Beam
  -> bearing on
X Primary Beam
  -> bearing on
Z Posts
```

若提取出的 Load Graph 含环，首版以稳定 Reject 拒绝。未来的
`ReciprocalAssembly` 必须先在 Assembly 内部求解，再把整个强连通分量压缩成一个
DAG 节点；不能让现有 DAG2.3 在环上错误累计荷载。

## 8. 轻量结构的静态筛选

不建议首版实现完整工程 FEM，但至少需要以下代理指标。

### 8.1 梁跨与弯曲代理

对均布荷载简支梁可用量级关系：

```text
MaxMoment ~ w * L^2 / 8
Deflection ~ 5 * w * L^4 / (384 * E * I)
```

本项目不追求工程精度，只需把材料、截面、跨度和累积荷载归一化为
`SpanUtilization`，超过 Profile 上限即拒绝或插入中间支点。

### 8.2 柱细长比与侧移机构

- 柱高/截面超过上限时必须增加横向联系或分段；
- 每个多层主体至少要在 XZ、YZ 中各存在足够的抗侧移路径；
- 只有四边形而无斜撑/刚接语义的框架标记为 Mechanism；
- 三角形和三维闭合单元计入 `LateralRigidityRank`。

### 8.3 接触与偏心

- 梁端悬出接触点的长度受 `MaxOverhangRatio` 限制；
- 交叉接触面积不能小于材料 Profile 下限；
- 上部合力不能落在支持区间之外；
- 搭接 Lane 必须与最终 Brick 碰撞体完全一致。

## 9. 弱点与可玩性

梁式结构比整板更容易产生可读弱点，但也更容易把所有结构做成均匀网格。弱点候选应来自
Load DAG 的 Failure Frontier，而不是随机把一根梁染色：

- 单根 Transfer Beam；
- 悬臂根部；
- 一对并联支撑中的一根；
- Portal 顶梁与柱的关键接点；
- CrossBeam/Crib 中承担主要反力的交叉点；
- Braced Bay 的单根斜撑；
- 两个 Chunk 之间的 Bridge Port。

候选必须同时满足：

```text
击中前静态稳定
击中后主体响应达到阈值
普通构件对照不产生同等响应
弱点从预期攻击方向可见且可达
不形成整栋只有一个固定模板弱点
```

长条构件还允许“部分跨度断裂”形成旋转、滑移和逐层剥落，比完整楼板直接翻倒更接近
Angry Birds 的动态可读性。

## 10. 物理实现建议

### 10.1 构件词汇

首版只使用：

```text
统一方形截面
Short / Medium / Long 三档长度
X / Y / Z 三个正交方向
可选 XZ / YZ 斜撑
```

不要同时引入任意角度、任意截面、任意长度和互承闭环。复杂性应主要来自拓扑和空间组合，
不是连续参数数量。

### 10.2 接触几何

- 梁端必须有足够的实际搭接长度；
- X/Y 梁交叉时使用确定性 Lane 高差；
- 柱头接触梁底，不把柱中心穿进梁；
- 预览 Mesh、静态验证 AABB、Chaos 碰撞和 Contact DAG 使用同一 Transform；
- 不通过隐藏约束把视觉上未接触的木条粘在一起。

### 10.3 Chaos

生成阶段使用纯数据静态筛选，只有少量高分候选进入 Chaos：

```text
Beam Graph validity
  -> Load DAG
  -> span / hull / contact / mechanism audit
  -> no penetration
  -> IdleValidation
  -> Weak vs Ordinary paired trials
```

## 11. 有界搜索与确定性

完整生成身份至少包含：

```text
GeneratorVersion
BuildingSeed
ShapeGrammarHash
VolumeWFCHash
BayMotifWFCHash
BeamGrammarHash
BeamAssemblyHash
LoadDAGHash
WeaknessPlanHash
BrickTransformHash
```

硬预算建议：

```text
MaxBaysPerChunk
MaxMotifWFCOperations
MaxBeamGrammarSteps
MaxBeamGraphBacktracks
MaxJointCount
MaxMemberCount
MaxBearingCount
MaxBrickCount
MaxChaosCandidates
```

失败必须返回稳定原因，例如：

```text
BeamNoPortCompatibleMotif
BeamMemberBudgetExceeded
BeamUnsupportedJoint
BeamSpanUtilizationExceeded
BeamLoadDAGCycle
BeamLateralMechanism
BeamBearingAreaInsufficient
BeamUnexpectedContactBypass
BeamWeaknessFrontierUnavailable
```

不得在失败时回退为完整平板。

## 12. 建议实施阶段

### Beam-A：纯数据 IR 与编辑器线框预览

- 实现 Joint / Member / Assembly；
- 从一个 DAG5-B v2 Box Volume 生成 Structural Bay Graph；
- 预览 X/Y/Z 梁、Joint、Port 和 Contact Lane；
- 不生成物理 Brick。

### Beam-B：Motif WFC 与图语法

- 实现 6～8 个首版 Motif；
- 实现 `BeamToGrillage`、`AlternateCribLayer`、`TriangulateBay` 等规则；
- 验证深度增加会增加拓扑，而不是只细分同一外盒；
- 保持 MustVoid 和跨 Chunk Port。

### Beam-C：Load DAG 与静态代理

- 从 Bearing Contact 提取 DAG；
- 把 DAG2.3 Plate 合力推广到 Beam 多点反力；
- 加入跨距、悬挑、接触、侧移机构和环检测；
- 禁止真实 Reciprocal Assembly。

### Beam-D0：Profile Catalog、Difficulty Curve 与 Settings Resolver

- 外部只保留 `GameplayProfileId + DifficultyTier`；
- 在 M7 内一次性解析 Shape、Beam-A/B/C、材料角色和弱点意图；
- Profile/Tier 可以改变玩法和内部生成策略，但不能改变项目级预算及验证硬门槛；
- 详见 [Beam-D0 独立设计稿](M73BeamD0GameplayProfileCatalogDesign.md)。

### Beam-D1：真实 Brick 与材料角色

- 把 Member 编译为固定截面、离散长度的现有模块；
- 把 Catalog 的材料/装置意图映射为真实物理角色；
- 保持 resolved Profile 身份，不在编译阶段重新随机选型。

### Beam-D2：弱点、Chaos 与 Profile×Tier 认证

- 基于 Beam-C Load DAG 复用 DAG3/DAG-4；
- 联合真实接触、落稳、攻击对照与失效签名；
- 认证完整 Profile×Tier 输入域，而不是只验证若干好看的种子。

### Beam-E：Catalog 冻结与六栋生产接入

- 接入 DAG5-A 候选搜索和 DAG5-C Novelty；
- 六栋建筑同时消费轮廓、Beam Graph、Load DAG 与弱点签名；
- 接入 Encounter 难度与视觉元数据；
- Catalog 通过认证后冻结，并由集成工作树提供共享 vNext 合同；
- 完成 Editor、PIE、30/60/120 FPS 和联合地图验收。

## 13. 正式验收建议

### 13.1 数据门槛

- 同一输入所有 Hash 完全一致；
- Beam Assembly Graph 无孤立 Required Port；
- Load DAG 无环且所有主体构件 Ground 可达；
- 无 Member 穿越 `MustVoid`；
- 所有 Bearing 都能映射到最终真实接触；
- Brick、Joint、Member 和 Contact 数量均在硬预算内。

### 13.2 视觉门槛

- 从正面和侧面都能看到结构层次，不退化为二维板墙；
- 至少同时出现 X 梁、Y 梁和 Z 柱；
- 不依赖完整平板遮盖三维拓扑；
- 四个结构家族在剪影和承重路径上都可区分；
- 玩家可以从外观判断主要支点、悬挑和跨接关系。

### 13.3 物理门槛

- 初始 Idle 全部接受；
- 无隐藏碰撞、无意外旁路、无梁柱穿透；
- 弱点受击产生目标主体运动；
- Ordinary 对照显著弱于 Weak；
- 同一固定步长重复运行结果在现有 M7 容差内稳定。

## 14. 对现有设计的修订结论

[轮廓约束递归 DAG 演进设计](M73EnvelopeConditionedRecursiveDAGGenerationEvolutionDesign.md)
中的 `Plate Footprint Fitting` 不应继续作为唯一结构实现。它应提升为通用
`Structural Footprint Fitting`，下分：

```text
Plate Patch Fitting       旧兼容/重型建筑
Beam Assembly Fitting     轻型可破坏建筑，建议成为 M7 正式方向
Hybrid Fitting            少量平台 + 大量梁杆
```

现有 Plate DAG 可以继续作为宏观荷载意图和兼容基线，但最终长条建筑必须经过独立的
Beam Assembly Graph、Bearing Contact 和 Load DAG 提取。这样才能既保留已经实现的
Shape Grammar/WFC 轮廓与 DAG3/DAG-4，又真正解决第三维、结构轻盈化和动态多样性。
