# M7.3：程序化模块化建筑生成调研与算法设计

> 文档性质：M7.3 前置调研与生成算法设计。  
> 本文负责“如何生成稳定、多样、可读且有明确弱点的积木建筑”；现有材料碰撞、累计损伤、爆炸与冲击实现仍以 [M7BuildingMaterialsAndDevicesDesign.md](M7BuildingMaterialsAndDevicesDesign.md) 为准。  
> 本阶段不在本文中实现 C++，也不改变 `CellTopo` 作为正式球面世界逻辑源的约束。

## 1. 结论先行

M7.3 不应采用“在轮廓内随机堆砖，然后交给 Chaos 自己决定是否站住”的方法，也不应仅靠大量隐藏约束把随机建筑强行焊死。适合本项目的最终方案是：

```text
结构语法生成候选
-> 几何接触与支撑图构建
-> 静态承重/重心/穿透校验
-> 有目的地合成弱点和连锁装置
-> 短时 Chaos 空载稳定性预模拟
-> 弱点与非弱点攻击探针
-> 多目标评分、变异和筛选
-> 输出静态 HISM + 动态模块/约束描述
```

这是一种“语法生成 + 图约束 + 搜索式 PCG + 物理验证”的混合方案。各部分职责必须分开：

| 层 | 解决的问题 | 不应承担的问题 |
| --- | --- | --- |
| 外轮廓/结构语法 | 建筑像什么、占地多大、层数和空洞如何分布 | 不能单独证明物理稳定 |
| 模块连接规则 | 哪类构件可以接到哪里 | 不能只凭连接器就判断承重 |
| 支撑图与静态分析 | 初始是否有承重路径、重心是否合理、弱点影响多大 | 不能替代 Chaos 最终验证 |
| Chaos 预模拟 | 验证数值误差、接触面与约束是否会自行发散 | 不适合在海量候选上长时间暴力模拟 |
| 攻击探针与评分 | 弱点是否有效、非弱点是否有抵抗、难度是否落在目标窗口 | 不负责生成外观 |

最终生成器应追求以下状态：

- 无外力时可靠站立，发射开始后也不会因初始接触误差自行弹飞；
- 击中普通外墙时发生局部移动、开裂或少量掉块，但不会立刻整栋消失；
- 击中明确弱点后，支撑路径减少、重心移出支撑区或装置触发，产生可读的逐级失稳；
- 相同难度可出现不同轮廓、材料分区、弱点机制和坍塌方向；
- 强度参数改变的是安全系数、冗余度、连接强度和材料配置，而不只是统一增加所有物块生命值。

## 2. 调研边界与资料可信度

商业游戏通常不会公开完整关卡生成和结构求解源码，因此本文把资料分为三类：

1. **已公开论文/官方文档确认**：可作为算法或引擎实现依据；
2. **游戏中可直接观察的设计机制**：只总结可见规律，不声称掌握其内部源码；
3. **针对本项目的工程推导**：明确标为推荐方案，不伪装成原作实现。

## 3. 案例与算法启示

### 3.1 Angry Birds 稳定结构程序生成研究

Stephenson 与 Renz 在 2016 年发表的《Procedural Generation of Complex Stable Structures for Angry Birds Levels》是与本项目最接近的公开算法。论文的核心不是随机堆叠，而是从上层构件出发递归生成下层支撑：

- 从建筑顶部的峰值或目标对象开始；
- 将当前底层构件按水平距离切分为若干子集；
- 为每个子集枚举中部支撑、两侧支撑、以及中部加两侧支撑；
- 候选必须无重叠，并保证每个上层块获得中部支撑，或同时获得左右支撑；
- 支撑关系可表达为一个或多个有向无环图，节点为构件，边为“下方构件支撑上方构件”；
- 局部支撑成立不代表整栋全局稳定，生成后仍需全局分析；
- 理论稳定也可能受物理引擎离散误差影响，因此最可靠的最终判断仍是短时物理模拟，检测构件位移；
- 不稳定候选应拒绝并重新生成，而不是在运行时祈祷它不倒。

2017 年的后续研究进一步把“多样、稳定、可解”同时作为生成目标；2014 年与 2019 年的相关工作则说明 Angry Birds 类关卡适合使用搜索、进化和适应度评价，而不是只执行一次随机采样。

对 M7.3 的直接迁移：

- 将论文的二维“支撑行”扩展为三维“支撑平面/支撑 Bay”；
- 先生成支撑关系，再实例化砖块，不以最终砖块随机位置反推结构；
- 每个非接地模块必须存在通往地基节点的支撑路径；
- 生成器必须保留候选拒绝和重试预算；
- “稳定性”和“可击破性”都要进入评分函数。

### 3.2 Red Faction: Guerrilla / Geo-Mod 2.0

公开资料确认 Geo-Mod 2.0 使用预破碎网格，并使其响应应力和动能冲击；建筑可以在关键结构点被战略性削弱，随后发生整体坍塌。

最重要的启示是：爽点不来自“任意位置扣完 HP 后整栋消失”，而来自关键承重路径被逐步切断。对本项目而言：

- 单块损伤系统之外，还需要建筑级支撑图；
- 弱点应同时满足“局部较易破坏”和“破坏后影响大量上层质量”；
- 普通区域应有材料强度或支撑冗余，击中后可以传递冲量但不会无条件全倒；
- 支撑图中的割点、桥边和支配节点是天然弱点候选。

### 3.3 Minecraft Jigsaw / Template Pool 类模块拼装

Minecraft 的 Jigsaw 思路可概括为：预制片段携带连接器，连接器从带权模板池中选择兼容片段，并受生成深度、朝向和包围盒冲突限制。它非常适合生成宏观空间结构，例如塔段、门楼、桥段和房间序列。

对 M7.3 的适用范围：

- 用“Foundation / SupportBay / Deck / Tower / Bridge / Roof / DeviceChamber”等宏观模块拼出结构骨架；
- 每个插槽声明允许连接的模块类别、朝向、尺度范围和权重；
- 使用包围盒或 OBB 拒绝相互穿插的模块；
- 用最大深度、块预算和必需终止模块防止无限扩展。

局限：连接器兼容只说明几何能接上，不能说明重心、接触面积和承载能力足够。因此 Jigsaw 只能承担宏观拼装，不能单独承担物理稳定性。

### 3.4 Shape Grammar / Split Grammar

Müller 等人的建筑程序建模研究通过形状语法把建筑体量递归拆分为楼层、立面、窗洞和装饰区域。它适合控制外轮廓和风格一致性。

本项目可用形状语法表达：

- 塔、门楼、双塔、桥架、阶梯、悬臂、漏斗、倒梯形等轮廓；
- 楼层宽度序列；
- 中空区域、窗洞、攻击通道和装置舱；
- 对称、近似对称和受控不对称；
- 材料分区与重复节奏。

但形状语法只负责“长成什么样”。规则产生的每个体量仍必须转成支撑图，并通过静态与动态稳定性校验。

### 3.5 WFC、Townscaper 类局部邻接生成

这类系统最值得借鉴的是“局部邻接合法性”：一个模块的边、角、上下表面只允许与若干兼容模块相邻，因此即使组合很多，视觉风格仍然统一。

适合 M7.3 的用途：

- 柱、梁、墙片、窗口、护栏、屋顶块之间的局部外观规则；
- 材料花纹、砖缝错位、边角收口和装饰变化；
- 在已经稳定的结构骨架外生成非承重表皮。

不适合：单独用局部熵最小策略决定整栋承重。局部合法并不代表全局有地基路径，更不代表被击中后有可玩的坍塌模式。

### 3.6 Besiege、World of Goo、Bridge Constructor 类连接结构

这些游戏可观察到的共同设计规律是：刚性支撑、拉索和可断连接件承担不同受力角色；连接拓扑改变后，结构行为随之改变。

对本项目的启示：

- 砖块接触主要承压；
- 绳和锁链只能承拉，不能作为抗压柱；
- 绳更柔软、更易断，适合作为明显弱点和摆锤机构；
- 锁链承载高、可形成高价值悬挂体，但仍需可断阈值；
- 柔性链条不应依赖每段极短刚体无限串联，应控制段数和约束数量；
- 刚性建筑主体不应全部靠 Physics Constraint 强行焊接。

### 3.7 Teardown 等局部破坏游戏

从可观察机制看，局部材料被移除后，剩余结构的连通性、支撑关系和剩余体量决定后续脱落。对 M7.3 的意义是：

- 弱点破坏后的效果必须来自结构状态改变，而不是只播放预设倒塌动画；
- 可以只让当前目标建筑的有限模块进入真实刚体，远处建筑保持静态；
- 视觉碎块与决定 Gameplay 的结构模块应分层，避免刚体数量失控。

本文不声称这些商业游戏使用了某一特定未公开算法；这里只迁移公开可观察到的玩法规律。

### 3.8 Unreal Engine Chaos 官方能力与限制

UE 5.8 官方文档确认：

- Physics Sub-Stepping 能提高复杂物理模拟的精度和稳定性，但会增加 CPU 成本；更小的子步时间通常更稳定；
- Physics Constraint 提供线性/角度断裂阈值、软约束刚度与阻尼、Projection、Shock Propagation 和 Plasticity 等能力；
- Projection 可以在较低迭代次数下让链条更硬，但属于求解后的修正；
- Shock Propagation 可增强约束链刚性，但可能沿链条注入能量，不能作为所有建筑的默认稳定补丁。

对 M7.3 的落地原则：

- 初始稳定优先依赖正确几何、接触面、重心和无穿透；
- 绳、锁链和少量明确的粘结接头才使用可断约束；
- 进入动态模拟前继续使用项目已有的穿透校验修复；
- 物理预模拟和正式发射阶段建议开启合适的 Sub-Stepping；
- 不用极硬约束掩盖错误生成，因为断裂后会瞬间释放积累误差，反而导致爆炸式弹飞。

## 4. 三个核心矛盾

### 4.1 稳定与可破坏

如果所有构件都只靠窄接触面站立，建筑会自行倾倒；如果所有构件都用金属、宽底座和多重焊接，玩家又无法制造连锁坍塌。

解决方式不是在两个极端之间选一个，而是分区：

- **基础稳定区**：宽地基、高摩擦、较高安全系数；
- **传力区**：把上部重量汇聚到少量可读承重路径；
- **弱点区**：局部强度较低、支撑冗余较少，但未受击时仍有安全裕度；
- **效果区**：失去支撑后具有足够质量、落差和碰撞路径，能产生二次破坏；
- **装置区**：炸药桶或活塞放大已被玩家正确触发的结构变化。

### 4.2 多样性与可验证性

完全自由的随机摆放组合最多，但验证成本和失败率也最高。只使用少数手工模板最稳定，却容易重复。

推荐使用“有限结构原型 + 连续参数变化 + 模块替换 + 评分筛选”：

- 原型保证基本承重逻辑；
- 尺寸、层数、空洞、材料、装置和弱点位置提供组合空间；
- 搜索与稳定性验证过滤坏样本；
- Novelty/距离指标防止优化器收敛到一种最安全的塔。

### 4.3 物理真实与玩法可读

纯物理最真实的弱点未必能被玩家看懂，纯视觉标记又可能与实际结构无关。弱点必须同时满足：

- 真实地改变支撑图；
- 从主要发射方向可见或可通过简单侦察发现；
- 材料和形状提供视觉语言，例如玻璃柱、单根木梁、悬挂绳、外露炸药桶；
- 击中后 0.2–1.5 秒内出现明确结构反馈，而不是很久以后随机倒塌；
- 非弱点受击也有局部反馈，避免玩家误以为输入无效。

## 5. M7.3 推荐的数据模型

### 5.1 建筑局部坐标系

正式球面地图中，建筑锚点仍来自 `CellTopo`。生成和结构分析统一在锚点的局部切平面进行：

```text
LocalZ = CellTopo 中心的径向向上
LocalX = 推荐发射方向投影到局部切平面后的方向
LocalY = LocalZ × LocalX
```

所有体量、重心、轮廓和攻击可见度先在此局部系内计算，最后转换为世界变换。连续球面只提供最终渲染高度与碰撞表面，不成为建筑归属或生成逻辑源。

M7.3 同时必须兼容 M7.1 平面物理测试台。生成器不得直接把“星球中心、径向方向、球面半径”写死在建筑语法、支撑图或弱点算法中，而应通过统一的建筑承载面查询接口获取：

```text
GroundMode = SphericalCellTopo | PlanarTestStage
AnchorTransform
GravityUp
QueryGround(PointInAnchorPlane) -> Position, Normal, Height, CellId(optional)
CanModifyPresentationSurface
```

- 正式球面地图：`AnchorTransform` 来源于建筑 Anchor Cell，`GravityUp` 为 Anchor Cell 的径向向上，Ground Query 查询 `CellTopo` 驱动的连续球面；
- M7.1 平面测试台：`AnchorTransform` 来源于测试台中可摆放的生成器 Actor，`GravityUp` 为测试台局部 `+Z`，Ground Query 查询 Floor 平面或其碰撞表面；
- 建筑主体、支撑图、弱点、材料、装置、攻击探针和评分逻辑在两种模式中完全共用；
- 地形适配层根据 Ground Mode 产生不同结果，但输出同一种 `FoundationCap` 顶面和 Ground Node 契约。

因此，同一个 `BuildingSeed + Preset` 应能先在 M7.1 中生成、平移、旋转、放置和击打，再放到正式球面 Anchor 上验证。两处建筑拓扑、材料、弱点和局部尺寸应保持一致，差异只允许出现在承载面适配层和世界坐标变换。

### 5.2 球面/平面统一的三层承载面适配

球面即使在逻辑坡度较低时仍有不可忽略的系统性曲率；同时连续高度场还可能包含局部起伏。M7.3 使用以下三层算法处理，并让 M7.1 平面实验台通过同一流程退化运行：

```text
占地范围校验
-> 局部平坦施工台
-> 自适应地基脚
-> 平整 FoundationCap
-> 可破坏建筑主体
```

#### 第一层：占地范围校验

建筑合法性不能只读取 Anchor Cell 中心的 `bBuildable` 或单点坡度。生成宏观轮廓后，应使用真实 Footprint 在承载面上采样：

- 轮廓覆盖的全部 `CellTopo` 是否允许建筑、是否包含水域、道路或已占用 Cell；
- Footprint 中心、角点、边中点以及主承重点的连续表面高度和法线；
- 球面曲率造成的系统性高差；
- 地形起伏造成的额外高差；
- 最大局部坡度、最大所需地基深度和最大圆心角跨度；
- 施工台边缘是否会切入河道、道路或其他 Gameplay 区域。

球半径为 `R`、采样点距 Anchor 切平面中心的水平距离为 `ρ` 时，仅球面曲率产生的近似下沉量为：

```text
CurvatureDropCM ≈ ρ² / (2R)
```

占地分析建议输出：

```text
FBuildingFootprintAnalysis
{
    GroundMode
    CoveredCellIds
    SamplePoints
    MinGroundHeightCM
    MaxGroundHeightCM
    MaxTerrainDeltaCM
    CurvatureDropCM
    MaxSlopeDegrees
    MaxFoundationDepthCM
    AngularSpanDegrees
    bRequiresMultiplePlatforms
    bAccepted
    RejectReason
}
```

若高差、坡度、地基深度或球面跨度超限，应依次尝试换 Anchor、旋转 Footprint、缩小建筑或拆分成多个平台；不能用无限向下拉长底砖来掩盖不合适的生成位置。

在 M7.1 平面实验台中仍执行相同采样与占地碰撞校验，但理想 Floor 上应得到：

```text
CurvatureDropCM = 0
MaxTerrainDeltaCM = 0
MaxSlopeDegrees = 0
```

这样可以验证算法没有错误地依赖星球对象，同时也允许未来在测试台上放置斜坡、台阶或不平测试网格，复现正式地图的地基问题。

#### 第二层：局部平坦施工台

正式球面地图中，在通过占地校验的 Anchor 周围生成由 `CellTopo` 逻辑数据声明的 `BuildingPad`。施工台内部使用统一局部平面，外缘平滑过渡回原连续地表。

设 Anchor 表面位置为 `P0`，Anchor 径向向上为 `U`，从星球中心指向采样点的单位方向为 `D`，施工平面与该射线的交点半径可写为：

```text
PlaneRadius(D) = dot(P0 - PlanetCenter, U) / dot(D, U)
FinalRadius = lerp(PlaneRadius, OriginalTerrainRadius, PadEdgeBlend)
```

其中 `PadEdgeBlend` 在施工台核心区为 `0`，在外缘混合带平滑过渡到 `1`。`BuildingPad` 至少记录：

```text
AnchorCellId
LocalFootprint
PadCoreExtentCM
PadBlendWidthCM
TargetPlane
AffectedCellIds
```

该过程不违反 `CellTopo` 逻辑源原则：施工台的 Anchor、范围、占用和合法性由 PCG/CellTopo 数据决定，连续球面只负责按这些逻辑数据生成最终高度、碰撞和表现。

M7.1 平面实验台也创建同一种逻辑 `BuildingPad` 描述，但默认 Floor 已与目标平面重合，因此：

- 不修改测试台 Floor 网格；
- `PadHeightCorrectionCM = 0`；
- 仍输出相同的目标平面、边界和 Foundation 接口；
- 可显示施工台轮廓 Debug Draw，方便检查 Footprint 与攻击方向；
- 若测试台使用斜坡或不平网格，可选择只生成地基脚而不改动人工测试场景。

#### 第三层：自适应地基脚

即使施工台已平整，连续网格离散、碰撞误差和施工台边缘仍可能留下少量高度差。生成器应在实际承重点下生成专用 `FoundationFoot`，而不是缩放普通木、石、铁或玻璃砖。

地基脚布置在：

- 建筑 Footprint 角点；
- 主承重柱下方；
- 长梁端部；
- 石质配重或高质量模块下方；
- 支撑图中被标记为 Ground Support 的节点下方。

每个地基脚的顶面都落在统一 `FoundationCapPlane`，底部沿 `-GravityUp` 延伸并略微嵌入查询到的地面：

```text
FootLengthCM =
    FoundationCapHeightCM
  - QueriedGroundHeightCM
  + FoundationEmbedDepthCM
```

推荐输出层级：

```text
Ground Surface
-> FoundationFoot：吸收局部高度差，提供实际接触
-> FoundationCap：统一平坦顶面，作为建筑支撑图的 Ground
-> Gameplay Foundation Brick：可选、可破坏的可见地基层
-> Building Body
```

`FoundationFoot/FoundationCap` 与普通 Gameplay 砖必须分离，避免向下缩放普通砖同时改变质量、转动惯量、重心、受击面积和结构难度。默认适配层不作为弱点；若希望玩家从底部击毁建筑，应在其上生成独立的可破坏地基柱或接缝。

在 M7.1 理想平面上，所有地基脚长度应相同或在容差内完全一致；这本身是一项回归测试。如果同一建筑在平面测试台出现明显不同的 Foot 长度，说明局部坐标、Pivot、缩放或 Ground Query 存在错误。

#### 大跨度建筑的平台拆分

不应让一栋大型刚性建筑的每个底砖分别沿各自球面法线倾斜，因为这会令柱子相互发散、横梁接触产生楔形缝隙，并使 Chaos 在解冻时挤压弹开。

- 小中型单体建筑使用一个 Anchor 径向局部平面；
- 当 `AngularSpanDegrees`、`MaxFoundationDepthCM` 或地形高差超限时，拆成多个子平台；
- 每个子平台拥有自己的 Anchor、BuildingPad、FoundationFoot 和 FoundationCap；
- 子平台之间使用桥梁、绳、锁链或明确的连接结构；
- 平台连接可成为可读弱点，但必须独立通过空载稳定性验证。

M7.1 中也必须能生成同样的多平台拓扑。平面模式下各平台 Up 相同，但平台之间的连接、弱点和攻击效果保持不变，以便先在测试台上调试。

### 5.3 Structure Node

每个可影响承重和破坏的模块记录：

```text
NodeId
ModuleKind
Material
LocalTransform
DimensionsCM
MassEstimate
CenterOfMass
IsGrounded
IsGameplayCritical
IsDecorative
CurrentSupportLoad
StrengthCapacity
WeakPointRole
```

### 5.4 Structure Edge

边不只表示相邻，而表示明确的力学关系：

```text
FromNode -> ToNode
Relation = Support | Bond | Rope | Chain | Device
ContactArea
ContactNormal
LeverArm
CompressionCapacity
TensionCapacity
ShearCapacity
LinearBreakThreshold
AngularBreakThreshold
```

- `Support`：下方构件通过接触承压；
- `Bond`：少量隐藏粘结或插接关系，可承拉/剪切并可断裂；
- `Rope`：只承拉、柔性、低断裂阈值；
- `Chain`：只承拉、强于绳；
- `Device`：炸药桶、活塞与其影响目标之间的逻辑连接。

### 5.5 建筑级输出

```text
BuildingId / Seed
AnchorCellId
GroundMode
LocalFrame
FootprintAnalysis
BuildingPads[]
FoundationFeet[]
SilhouetteType
Bounds
Nodes[]
Edges[]
GroundNodes[]
WeakPointNodes[]
AttackSolutions[]
IdleStabilityScore
DifficultyScore
DiversitySignature
```

该数据应独立于 Actor 生命周期。生成器先输出纯数据描述，再由表现层实例化 HISM、`AABTSM7BuildingModule` 和必要 Constraint。

## 6. 分层生成算法

### 6.1 第 1 层：采样设计意图

首先确定这栋建筑想带来什么体验，而不是立刻随机放砖：

- 规模和块预算；
- 外轮廓类型；
- 主攻击方向与允许的侧翼角度；
- 目标鸟数/目标命中次数；
- 预期弱点数量；
- 预期坍塌方向；
- 材料预算；
- 是否包含悬挂、爆炸或活塞机制；
- 结构冗余和目标安全系数；
- 目标被弱点攻击后失去支撑的质量比例。

完成宏观 Footprint 后，必须先执行第 5.2 节的三层承载面适配，再进入最终砖块铺设与支撑图验证。建筑主体始终生成在 `FoundationCap` 提供的统一局部平面上，而不是直接贴合球面逐块弯曲。

### 6.2 第 2 层：宏观结构语法

推荐的结构原语：

| 原语 | 职责 | 常见变化 |
| --- | --- | --- |
| `Foundation` | 地基与接地节点 | 单底座、分离双底座、阶梯底座 |
| `SupportBay` | 两柱一梁或多柱一板 | 柱距、层高、单柱/双柱、材料 |
| `FloorDeck` | 汇集上层重量 | 宽度、厚度、悬挑量 |
| `TowerCore` | 垂直主体 | 收分、扩张、偏心、空洞 |
| `BridgeSpan` | 跨越两个支点 | 梁桥、吊桥、拱形视觉外壳 |
| `Cantilever` | 制造偏心与侧向倒塌 | 悬挑长度、配重、拉索 |
| `SuspendedPod` | 悬挂重物或目标舱 | 绳/链数量、摆动空间 |
| `DeviceChamber` | 装置与目标结构关系 | 桶舱、活塞方向、遮挡程度 |
| `RoofCap` | 收顶和轮廓识别 | 平顶、尖顶、双翼顶 |

语法示例：

```text
Building
-> Foundation + TowerCore + RoofCap
-> Foundation + SupportBay*2 + FloorDeck + SuspendedPod
-> FoundationPair + BridgeSpan + DeviceChamber
-> Foundation + TowerCore + Cantilever + CounterWeight
```

这一层只生成 Structure Node Graph 的粗节点和槽位，不直接生成最终每块砖。

### 6.3 第 3 层：体素/格架离散与砖块铺设

将宏观体量投影到统一建筑格架：

- 水平格距由标准砖短边和目标缝隙确定；
- 层高由标准砖厚度决定；
- 长砖优先作为梁，竖砖优先作为柱；
- 相邻层砖缝应错位，避免所有竖向缝隙贯通；
- 承重梁的有效落座长度必须高于下限；
- 避免极端细长砖作为普通直立主柱；
- 建筑表面可在不改变承重骨架的前提下替换尺度或增加装饰块。

这里可以使用局部邻接规则或 WFC 做“铺砖外观”，但承重节点位置不可被其随机改坏。

### 6.4 第 4 层：构建接触和支撑图

对所有候选模块执行 OBB 接触检测：

1. 找出实际接触或在 Snap 容差内的面；
2. 计算接触面积、法线和接触中心；
3. 若接触法线大致朝上，建立 `Support` 边；
4. 若由语法明确声明粘结，建立 `Bond` 边；
5. 绳/链建立只能承拉的约束边；
6. 标记地基接地节点；
7. 检查每个非装饰节点是否能沿支撑/连接边到达 Ground。

支撑图最好保持“从支撑者指向被支撑者”的 DAG。允许桥梁等结构存在横向连接，但承重传播层应单独投影为无环图，避免负载递推陷入环。

### 6.5 第 5 层：静态稳定性分析

至少执行以下廉价校验：

#### 无穿透

- AABB 粗筛后使用 OBB/形状检测；
- 小于容差的穿透沿最小分离轴回正；
- 大于容差的穿透直接拒绝候选并记录节点；
- 复用 M7 已有进入 Chaos 前的穿透校验思想，但生成期应尽量做到零修复。

#### 重心投影

对每个上层连通子结构计算总重心，将其沿重力方向投影到有效支撑面：

```text
SubtreeCOM = sum(NodeMass * NodeCOM) / sum(NodeMass)
Stable if Project(SubtreeCOM) lies inside SupportPolygon with Margin
```

支撑多边形由实际接触斑块的并集或凸包近似。`Margin` 随目标结构强度增加，弱点区域可以较小，但不能为负。

#### 滑动近似

以物理材质静摩擦系数估计倾斜接触面的抗滑条件。生成时不允许主要地基依赖接近极限的摩擦力站立；摩擦应是安全余量，不是唯一支撑。

#### 负载与安全系数

从顶部向地基递推质量，将上层重量按接触面积和相对重心分配给支撑边：

```text
EdgeLoad = AssignedMass * Gravity
SafetyFactor = EdgeCapacity / EdgeLoad
```

普通主支撑应满足目标安全系数；弱点支撑允许更低，但在空载状态仍必须高于 1，并保留数值误差余量。

#### 几何限制

- 最大悬挑长度；
- 最小接触面积比例；
- 最大细长比；
- 相邻质量比上限；
- 单一玻璃柱不可默认支撑巨大石质上层，除非它就是经过验证的弱点；
- 绳/锁链不可产生抗压支撑边；
- 炸药桶和活塞不能与其他模块初始穿插。

### 6.6 第 6 层：弱点合成

弱点不应随机挑一块改成玻璃。应先分析支撑图，再选择高影响节点。

#### 候选发现

- 无向化后的割点和桥边；
- 支撑 DAG 中控制大量节点通往 Ground 的支配节点；
- 当前负载高、冗余支撑少的节点；
- 移除后会让某个子结构重心移出支撑多边形的节点；
- 从主要攻击方向可见、可达且没有被不可破坏块完全遮挡的节点。

#### 弱点评分

```text
WeaknessScore =
    UnsupportedMassRatio
  * ExposureFromAttackDirection
  * VisualReadability
  * ChainReactionPotential
  * CollapseDirectionQuality
  / max(LocalBreakEffort, Epsilon)
```

其中：

- `UnsupportedMassRatio`：移除后失去 Ground 路径或变为不稳定的质量比例；
- `ExposureFromAttackDirection`：从弹弓射界能否直接击中；
- `VisualReadability`：材料、轮廓、留白和装置是否让玩家能识别；
- `ChainReactionPotential`：掉落块能否撞击下游构件或装置；
- `CollapseDirectionQuality`：是否朝安全且可观察区域坍塌；
- `LocalBreakEffort`：考虑材料阈值、尺寸、角度和遮挡后的预估破坏成本。

#### 弱点类型

| 类型 | 结构做法 | 视觉语言 | 预期结果 |
| --- | --- | --- | --- |
| 单柱弱点 | 上层重量汇聚到一根木/玻璃柱 | 柱周围留空 | 柱断后上层偏转下落 |
| 双支撑择一 | 一强一弱两条路径 | 弱侧材料反差 | 击破弱侧后整体向一边倾倒 |
| 拉索弱点 | 悬挑由绳/链保持 | 外露拉索 | 拉索断裂后侧翼下摆 |
| 接缝弱点 | 上下体量通过少量窄连接相接 | 明显腰部收窄 | 上部滑落或旋转 |
| 炸药桶弱点 | 桶的爆炸范围覆盖关键边 | 桶部分遮挡但可见 | 正确命中后切断多条支撑 |
| 活塞弱点 | 活塞轴向指向承重块或配重 | 方向明确 | 推出支撑区而非随机爆散 |
| 配重弱点 | 悬臂依赖可破坏配重/锁链 | 两侧重量对比 | 解除配重后悬臂翻转 |

#### 防止两种失败

- 一击全倒：限制单弱点的 `UnsupportedMassRatio` 上限，或让弱点先造成倾斜，再通过二次碰撞完成坍塌；
- 击中无效：要求弱点被破坏后在规定时间内产生最小位移、支撑路径变化或装置反馈。

### 6.7 第 7 层：材料与装置分配

材料不是随机皮肤，而是结构角色：

| 构件 | 当前项目物理特征 | 推荐生成角色 | 禁忌 |
| --- | --- | --- | --- |
| 木 | 密度低、摩擦高、较易推动/破坏 | 横梁、次级柱、可读弱点、连锁传播 | 不要让所有外墙都是同强度木块而一碰全散 |
| 石 | 重、摩擦高、推动传递较低 | 地基、压重、坠落锤、稳定主体 | 大量高处石块会造成空载数值压力和过强二次冲击 |
| 铁 | 最重、强度高、推动传递低 | 核心梁、保护壳、冗余支撑、锁定方向 | 不能泛滥，否则建筑怎么撞都不倒 |
| 玻璃 | 破坏阈值最低、摩擦较低 | 明确脆弱面、隔断、触发器、弱支柱 | 不要作为普通巨大上层的唯一主柱，除非通过验证 |
| 绳 | 木材 Profile，预期柔性且易断 | 单点悬挂、摆锤、明显弱点 | 不承压，不做刚性柱 |
| 锁链 | 铁材 Profile，预期更强 | 高价值悬挂、双链冗余、延迟失稳 | 不应不可断，也不应使用过多小段刚体 |
| 炸药桶 | 破坏后近处摧毁、远处冲击 | 切断关键支撑、制造二阶段连锁 | 不能只为装饰放在爆炸范围无关键节点的位置 |
| 弹簧活塞 | 破坏后沿轴向产生定向效果 | 推出梁、配重或上层模块 | 轴向不可随机，必须指向预期失稳目标 |

当前源码默认 Profile 已为木、石、铁、玻璃配置不同的摩擦、弹性、密度、击退/破坏速度和推动传递。M7.3 应读取真实 `FABTSM7MaterialProfile`，不要复制一套脱节的生成常量。质量估算也应使用相同密度和实际碰撞体积：

```text
MassEstimate = CollisionVolume * Density
```

### 6.8 第 8 层：Chaos 空载预模拟

静态分析通过后，在隐藏验证场景或专用离线验证世界中执行短时 Chaos 模拟：

1. 以与正式关卡相同的碰撞体、质量和 Physical Material 实例化；
2. 先运行穿透校验；
3. 开启与正式发射阶段相同的重力和 Sub-Stepping；
4. 不施加鸟冲击；
5. 模拟 `StabilitySimulationSeconds`；
6. 记录各块最大位置变化、角度变化、接触丢失和约束断裂；
7. 超阈值则拒绝，或只做有限的 Snap/扩大接触面修复后重测；
8. 可再施加极小扰动，排除“理论上静止、轻触即自毁”的候选。

建议避免用“模拟开始后立刻冻结若干秒”掩盖问题，因为玩家发射阶段一旦解冻，积累的错误仍会出现。

### 6.9 第 9 层：攻击探针与可玩性验证

只验证空载稳定还不够。每栋候选至少运行两类简化攻击：

#### 弱点攻击

- 从允许的弹弓方向向弱点施加目标鸟种的代表性冲量；
- 记录弱点是否损伤/断裂；
- 记录失去支撑的质量比例、最大连锁深度、坍塌方向和静止时间；
- 估算达到目标结果需要的鸟数。

#### 非弱点攻击

- 对普通外墙、强支柱和屋顶各采样少量撞击；
- 要求产生局部反馈，但整体失稳程度显著低于弱点攻击；
- 若任意撞击都整栋倒塌，则拒绝或增加冗余；
- 若所有撞击都无明显结构变化，则降低强度、减少冗余或暴露弱点。

较昂贵的 Chaos rollout 只对静态筛选后的少量候选执行。早期候选用图分析和冲量近似淘汰，降低生成时间。

### 6.10 第 10 层：搜索、变异与多目标筛选

推荐保留一个小型候选池，而不是找到第一个稳定样本就结束：

```text
Population = GenerateCandidates(Seed, AttemptBudget)

for Candidate in Population:
    if not GeometryValid: reject
    BuildSupportGraph
    if not StaticStable: reject
    SynthesizeWeakPoints
    if not IdleChaosStable: reject
    EvaluateWeakAndNonWeakAttacks
    Score(Candidate)

while BudgetRemains:
    Select promising + novel candidates
    Mutate silhouette/material/support/device parameters
    Revalidate affected layers

return best candidate inside target difficulty window
```

不要只最大化一个分数。建议使用加权多目标或 Pareto 筛选，并保留 Novelty Archive，避免所有建筑收敛为相同宽底塔。

## 7. 外观多样性的正交来源

多样性应来自多个互相较独立的轴，而不是对每块砖做完全随机旋转：

### 7.1 外轮廓族

- 单塔；
- 双塔门楼；
- 桥架；
- 阶梯塔；
- 上宽下窄的高风险塔；
- 下宽上窄的堡垒；
- 偏心悬臂；
- 吊舱；
- 双翼/三叉轮廓；
- 中空框架；
- 坡地顺应的分层地基。

### 7.2 轮廓内部参数

- 每层宽度序列；
- 柱距和层高；
- 实心率/空洞率；
- 对称度；
- 中心偏移；
- 悬挑长度；
- 顶部形状；
- 支撑 Bay 数量；
- 砖块尺度组合；
- 局部旋转和错缝方式。

### 7.3 材料分区

- 下石上木；
- 铁核心 + 木外框；
- 玻璃弱腰；
- 石质配重 + 绳索悬臂；
- 双塔采用不同材料；
- 同轮廓使用不同弱点材料，但保持相近难度。

### 7.4 装置和连锁模式

- 无装置的纯承重弱点；
- 炸药桶切断核心；
- 活塞推出上层；
- 悬挂重物撞击下层；
- 双链冗余，需要先断一条再推动；
- 玻璃触发器保护炸药桶；
- 配重坠落触发二次撞击。

### 7.5 多样性签名

为每栋建筑记录离散/连续特征：

```text
SilhouetteType
HeightWidthRatio
LayerWidthSequence
VoidRatio
Symmetry
MaterialHistogram
SupportGraphDegreeHistogram
WeakPointType
DevicePattern
CollapseDirection
```

新候选与最近已选建筑的签名距离过小，则降低其选择权重。这样可以避免仅靠随机 Seed 产生视觉上近似相同的建筑。

## 8. 编辑器可调参数建议

### 8.1 规模与轮廓

| 参数 | 含义 |
| --- | --- |
| `FootprintSizeCM` | 建筑局部切平面占地范围 |
| `TargetHeightCM` | 目标高度 |
| `TargetBlockCount` | Gameplay 结构块预算，不含纯装饰 |
| `SilhouetteType` | 轮廓类型或带权类型池 |
| `LayerCountRange` | 层数范围 |
| `HeightWidthRatioRange` | 高宽比范围 |
| `Symmetry` | 0 为不对称，1 为严格镜像 |
| `VoidRatio` | 建筑内部和立面空洞比例 |
| `CantileverRatio` | 悬挑结构占比 |
| `RoofStyleWeights` | 顶部轮廓权重 |

### 8.2 承载面、施工台与地基

| 参数 | 含义 |
| --- | --- |
| `GroundMode` | 正式球面或 M7.1 平面测试模式；通常由宿主自动决定 |
| `FootprintSampleSpacingCM` | 占地范围内的地面采样间距 |
| `MaxBuildingPadSlopeDegrees` | 允许生成施工台的最大地形坡度 |
| `MaxTerrainDeltaCM` | Footprint 内允许的原始地表最大高差 |
| `MaxFoundationDepthCM` | 自适应地基脚允许补偿的最大深度 |
| `FoundationEmbedDepthCM` | 地基脚插入地面的微小深度 |
| `FoundationTopClearanceCM` | FoundationCap 与地表最高点之间的安全余量 |
| `BuildingPadBlendWidthCM` | 球面施工台过渡回原地形的混合宽度 |
| `MaxSinglePlatformAngularSpanDegrees` | 单个刚性平台允许跨越的最大球面圆心角 |
| `MinFoundationSupportCount` | 单个平台最少地基脚数量 |
| `AllowMultiPlatformSplit` | 超限时是否允许拆成多个子平台 |
| `ModifyPlanarTestFloor` | M7.1 是否允许修改测试地面；默认关闭 |
| `DrawFootprintDebug` | 显示采样点、Pad 轮廓、地基脚和拒绝原因 |

建议初始值：

```text
MaxBuildingPadSlopeDegrees = 6°
MaxFoundationDepthCM = 120cm
FoundationEmbedDepthCM = 3cm
FoundationTopClearanceCM = 2cm
BuildingPadBlendWidthCM = 150cm
MaxSinglePlatformAngularSpanDegrees = 5°
MinFoundationSupportCount = 4
ModifyPlanarTestFloor = false
```

### 8.3 结构与稳定性

| 参数 | 含义 |
| --- | --- |
| `StructuralStrength` | 高层抽象强度，映射到安全系数、材料和连接阈值 |
| `TargetSafetyFactor` | 普通主支撑的最低安全系数 |
| `SupportRedundancy` | 平均独立接地路径数量 |
| `MinContactAreaRatio` | 有效接触面积/上层底面积下限 |
| `SupportMarginCM` | 重心投影到支撑边界的最小余量 |
| `MaxOverhangCM` | 普通悬挑上限 |
| `MaxSlendernessRatio` | 直立构件最大细长比 |
| `MaxSupportedMassRatio` | 单支撑块可承载质量相对自身质量上限 |
| `UseBondConstraints` | 是否允许生成少量可断粘结边 |
| `BondDensity` | 可断粘结边密度，而非全建筑焊死 |

### 8.4 弱点与难度

| 参数 | 含义 |
| --- | --- |
| `WeakPointCount` | 目标弱点数量 |
| `WeakPointExposure` | 从推荐攻击方向的最低暴露度 |
| `WeakPointCollapseRatio` | 弱点触发后目标失稳质量比例 |
| `WeakPointReadability` | 材料反差、留白和装置可见度权重 |
| `NonWeakResistance` | 普通区域相对弱点的抵抗倍率 |
| `TargetBirdHits` | 达成主要坍塌目标的期望命中次数 |
| `AttackDirectionConeDeg` | 允许的发射方向锥角 |
| `AllowHiddenWeakPoint` | 是否允许需要侦察或侧击的弱点 |
| `MaxSingleHitCollapseRatio` | 防止一击无脑全倒 |

### 8.5 材料与装置预算

| 参数 | 含义 |
| --- | --- |
| `MaterialMix` | 木/石/铁/玻璃目标比例或权重 |
| `FoundationMaterialWeights` | 地基材料池 |
| `WeakPointMaterialWeights` | 弱点材料池 |
| `DeviceDensity` | 装置数量相对块数的比例 |
| `ExplosiveBarrelBudget` | 炸药桶数量范围 |
| `SpringPistonBudget` | 活塞数量范围 |
| `SuspensionRatio` | 绳/链结构占比 |
| `RopeToChainRatio` | 绳与链的分配比例 |

### 8.6 验证与性能

| 参数 | 含义 |
| --- | --- |
| `GenerationAttemptBudget` | 语法候选最大尝试数 |
| `StaticValidationBudgetMS` | 静态分析预算 |
| `StabilitySimulationSeconds` | 空载 Chaos 验证时长 |
| `MaxIdleDisplacementCM` | 空载最大允许位移 |
| `MaxIdleRotationDeg` | 空载最大允许旋转 |
| `MinPerturbationSurvivalSeconds` | 小扰动后最低稳定时间 |
| `PhysicsRolloutBudget` | 攻击模拟数量预算 |
| `MaxActiveRigidBodies` | 单建筑最大 Gameplay 刚体数 |
| `MaxConstraintCount` | 单建筑最大约束数 |
| `CandidatePoolSize` | 最终候选池大小 |
| `NoveltyWeight` | 多样性在筛选中的权重 |

`StructuralStrength` 不应直接乘到全部 `BreakDamage`。推荐映射如下：

```text
StructuralStrength
-> TargetSafetyFactor
-> SupportRedundancy
-> Material role weights
-> Bond density / break threshold
-> Weak-point local contrast
```

这样高强度建筑仍有弱点，只是需要更精确、更强鸟种或更多阶段攻击。

## 9. 评分模型

### 9.1 基础指标

```text
IdleStability
WeakPointEffectiveness
NonWeakResistance
CollapseReadability
AttackAccessibility
TargetHitAccuracy
StructuralDiversity
SilhouetteNovelty
MaterialEntropy
PerformanceCost
```

### 9.2 建议归一化

- `IdleStability = 1 - normalized(max displacement, max rotation)`；
- `WeakPointEffectiveness` 取弱点攻击后的目标失稳质量比例与目标值的接近度；
- `NonWeakResistance` 取普通攻击造成的失稳质量比例是否落在允许区间；
- `CollapseReadability` 综合连锁层数、延迟、方向一致性和可见质量；
- `AttackAccessibility` 综合遮挡、射界、距离和允许角度；
- `PerformanceCost` 综合刚体、约束、接触对和预估碎块数。

### 9.3 可行性硬门槛

以下条件不进入加权评分，直接拒绝：

- 初始大穿透；
- 任一 Gameplay 关键节点无 Ground 路径；
- 空载模拟发生断裂、倾倒或超阈值位移；
- 没有任何可达弱点；
- 弱点攻击与普通攻击效果差异低于下限；
- 坍塌主要落向玩家出生区、弹弓位置或不可接受区域；
- 刚体/约束预算超限；
- 主要攻击解被地形或其他不可破坏物完全阻挡。

### 9.4 一个可用的总评分

```text
FinalScore =
    0.22 * IdleStability
  + 0.20 * WeakPointEffectiveness
  + 0.14 * NonWeakResistance
  + 0.12 * CollapseReadability
  + 0.10 * AttackAccessibility
  + 0.08 * TargetHitAccuracy
  + 0.08 * StructuralDiversity
  + 0.06 * MaterialEntropy
  - PerformancePenalty
```

实际实现时应先满足难度窗口，再在窗口内按多样性和成本选择；否则单纯最大化总分可能不断偏向最宽、最稳、最无聊的建筑。

## 10. 与现有 M7 工程的接口关系

### 10.1 可直接复用

现有工程已具备：

- `FABTSM7BrickSpec`：材料与最终尺寸；
- `FABTSM7SuspensionSpec`：绳/链类型、长度和半径；
- `FABTSM7DeviceSpec`：炸药桶/活塞类型与尺寸；
- `FABTSM7MaterialProfile`：击退/破坏速度、摩擦、弹性、密度、累计损伤和推动传递；
- 四种砖块 HISM；
- HISM 命中后提升为 `AABTSM7BuildingModule` 动态代理；
- 发射阶段统一启用平面/球面重力；
- 炸药桶径向近毁远推、活塞定向近毁远推；
- 进入 Chaos 前的初始穿透校验；
- 发射结束后的动态物体静止检测和冻结。

### 10.2 M7.3 应新增但本文不实现的模块边界

建议后续拆分为若干小类，避免把所有生成逻辑写进 MaterialSystem：

| 模块 | 职责 |
| --- | --- |
| `ABTSM73BuildingGenerator` | 编排生成尝试、Seed 派生、候选池和最终选择 |
| `ABTSM73StructureGrammar` | 宏观轮廓和结构原语展开 |
| `ABTSM73BrickLayoutBuilder` | 体量离散、铺砖、错缝和局部邻接 |
| `ABTSM73SupportGraphBuilder` | 接触检测与支撑/连接图构建 |
| `ABTSM73StaticStabilityValidator` | 重心、支撑面、负载、安全系数和穿透校验 |
| `ABTSM73WeakPointPlanner` | 割点/支配节点分析、弱点与装置合成 |
| `ABTSM73PhysicsValidator` | 空载及攻击 Chaos rollout |
| `ABTSM73CandidateScorer` | 难度、多样性、成本和 Novelty 评分 |
| `ABTSM73BuildingSpawner` | 将纯数据描述转换成 HISM、Actor 和 Constraint |
| `ABTSM73GroundAdapter` | 统一封装正式球面与 M7.1 平面的 Ground Query、Footprint 分析和 Anchor Frame |
| `ABTSM73BuildingPadBuilder` | 输出球面施工台逻辑数据；平面模式下退化为空变形 Pad |
| `ABTSM73FoundationBuilder` | 根据承重点生成 FoundationFoot、FoundationCap 和 Ground Node |

单个类保持单一职责；任何类接近 600 行时继续拆分规则、图算法或验证器。

### 10.3 运行时表示

为了符合现有性能架构：

- 发射前：普通砖块保持 HISM/静态碰撞，装置和绳链可为轻量 Actor/Component；
- 进入发射阶段：当前目标建筑的 Gameplay 结构块提升为动态 Actor，或按已有通路统一激活；
- 结构图只作为逻辑、损伤和调试数据，不要求每条 Support 边都生成 Physics Constraint；
- 支撑失效后依靠真实重力、接触与少量连接约束坍塌；
- 发射结束：等待全部相关刚体长期无明显位移后冻结，保留最终位置；
- 装饰表皮不参与支撑图，可保持 HISM 或随父结构批量隐藏。

## 11. 性能与降级方案

### 11.1 生成期分层淘汰

```text
100% 候选：语法、预算、AABB/OBB、支撑路径
约 20%：重心、负载和弱点图分析
约 5%：Chaos 空载预模拟
约 1–3%：弱点/非弱点攻击 rollout
最终：在难度窗口内按 Novelty 选择
```

不要对每个随机候选直接跑完整 Chaos。

### 11.2 缓存

- 用 `Hash(GeneratorVersion, Seed, PresetId, MaterialProfileVersion)` 作为缓存键；
- 编辑器阶段可预烘焙一批通过验证的建筑描述；
- 比赛运行时优先从验证库按 Seed 选择并做尺度/材料安全变体；
- Material Profile 改变后使相关稳定性缓存失效。

### 11.3 失败回退

生成预算耗尽时：

1. 降低层数和块预算；
2. 减少悬挑、绳链和装置数量；
3. 提高基础宽度与接触面积；
4. 回退到同轮廓族的已验证结构原型；
5. 绝不输出未通过空载稳定性门槛的候选。

## 12. 调试可视化与日志

建议提供编辑器/PIE 调试开关：

- 节点 OBB 和实际接触面；
- Ground 节点绿色、无支撑节点红色；
- Support 边蓝色、Bond 黄色、Rope 橙色、Chain 灰色；
- 边负载/容量和安全系数热力图；
- 子结构重心点与支撑多边形；
- 割点、桥边、支配节点和最终弱点编号；
- 推荐攻击锥、遮挡线和预计坍塌方向；
- Chaos 预模拟最大位移向量；
- 候选拒绝原因和评分分项。

推荐日志格式：

```text
[ABTS][M7.3][Generate] Seed=... Preset=... Attempt=... Nodes=... Edges=...
[ABTS][M7.3][Reject][Penetration] A=... B=... Depth=...
[ABTS][M7.3][Reject][Support] Node=... NoGroundPath=1
[ABTS][M7.3][Reject][COM] Subtree=... Margin=-...cm
[ABTS][M7.3][WeakPoint] Node=... UnsupportedMass=... Exposure=... Score=...
[ABTS][M7.3][IdleValidation] Seconds=... MaxMove=... MaxRotation=... Accepted=...
[ABTS][M7.3][AttackValidation] Type=Weak/NonWeak CollapseRatio=... Settled=...
[ABTS][M7.3][Accepted] Score=... Difficulty=... Novelty=... Cost=...
```

## 13. M7.3 分阶段实施建议

### M7.3-A：稳定积木建筑

> 独立工程实现、编辑器步骤、日志和验收见 [M73AStableBlockBuildingImplementationDesign.md](M73AStableBlockBuildingImplementationDesign.md)。

- 只支持木/石/铁/玻璃砖；
- 先做单塔、门楼和双塔桥三种轮廓；
- 建立球面/M7.1 平面统一 Ground Adapter；
- 实现 Footprint 占地采样、球面 BuildingPad 和自适应 FoundationFoot；
- 保证相同 Seed/Preset 可在 M7.1 平面测试台直接生成、变换、放置和击打；
- 生成支撑 DAG；
- 实现穿透、接触面积、重心和 Ground 路径校验；
- 执行空载 Chaos 预模拟；
- 暂不生成绳链和装置。

### M7.3-B：弱点与难度

- 加入割点/支配节点分析；
- 生成单柱、接缝和材料弱点；
- 运行弱点与非弱点攻击探针；
- 根据 `TargetBirdHits` 筛选；
- 加入调试热力图。

### M7.3-C：装置连锁

- 加入绳、锁链、炸药桶和弹簧活塞；
- 明确绳链只承拉；
- 装置必须绑定结构目标和预期效果；
- 验证装置不在空载阶段自触发；
- 验证爆炸/活塞不会无差别清空整栋。

### M7.3-D：多样性搜索与 PCG 集成

- 候选池、变异和 Novelty；
- 与 Task Graph 的建筑难度、道路距离和攻击方向连接；
- 在 `CellTopo` 建筑 Anchor 上生成；
- 验证地形遮挡、弹弓射界和坍塌安全区；
- 建立固定 Seed 回归样本库。

## 14. 验收标准

### 14.1 稳定性

- 固定 Seed 批量生成至少 100 栋候选，最终输出建筑 100% 通过空载验证；
- 同一 `BuildingSeed + Preset` 可分别生成到 M7.1 平面测试台和正式球面 Anchor，建筑主体拓扑、材料、弱点和局部尺寸一致；
- M7.1 可直接在编辑器中放置生成器 Actor，运行后生成完整建筑并使用已有弹弓击打；
- 理想平面测试中 `CurvatureDropCM`、`MaxTerrainDeltaCM` 和 `MaxSlopeDegrees` 为零或在数值容差内；
- 正式球面建筑通过完整 Footprint 校验，不得只用 Anchor Cell 中心坡度判断；
- 球面施工台核心区与 `FoundationCap` 形成统一局部平面，建筑主体不得逐砖沿各自径向弯曲；
- 所有 FoundationFoot 深度不超过 `MaxFoundationDepthCM`，超限候选必须换位置、缩小或拆平台；
- 发射阶段开始后，无鸟撞击时建筑不出现可见弹起、自旋、滑移或自行断裂；
- 初始最大穿透不超过修复容差，超过者必须拒绝而非带病运行；
- `MaxIdleDisplacementCM`、`MaxIdleRotationDeg` 在预设阈值内；
- 绳/链没有初始爆冲，炸药桶和活塞不会因初始接触自动触发。

### 14.2 可破坏性

- 对每栋建筑至少存在一个经过验证的可达弱点；
- 弱点攻击的失稳质量比例显著高于普通区域攻击；
- 普通区域受击有局部反馈，但不应频繁一击全倒；
- 建筑强度提高后仍保留解，只改变所需精度、鸟种、命中次数或攻击顺序；
- 弱点触发后能观察到支撑失效、倾斜、坠落或装置连锁，而非直接全体隐藏。

### 14.3 多样性

- 同一规模档至少覆盖三种以上轮廓族；
- 连续十栋建筑不应出现相同轮廓、相同材料直方图和相同弱点类型的完全重复组合；
- 多样性不依赖大幅随机旋转单块导致结构噪声；
- 不同 Seed 结果可复现。

### 14.4 性能

- 单建筑 Gameplay 刚体、Constraint 和装置数量在预算内；
- 生成验证有明确时间上限和回退路线；
- 正式地图只激活当前目标建筑的物理模块；
- 空载/攻击 rollout 可在编辑器预烘焙，运行时可选择缓存结果。

## 15. 风险与排错

| 症状 | 可能根因 | 建议处理 |
| --- | --- | --- |
| 建筑刚启用物理就弹飞 | 初始穿透、重复缩放、过硬约束、质量差过大 | 检查最终碰撞尺寸；生成期 OBB 校验；进入 Chaos 前穿透校验；避免全体硬约束 |
| 建筑无外力缓慢倾倒 | 重心投影接近支撑边界、地基过窄、接触面积不足 | 增大 Support Margin；重算子结构 COM；扩大落座长度 |
| 弱点击碎但建筑不动 | 弱点不是实际支撑路径、存在未建模冗余接触 | 从真实接触重建支撑图；检查移除后 Ground 路径；减少隐藏冗余 |
| 哪里都能一击全倒 | 安全系数过低、单点支撑过多、普通区无冗余 | 提高普通区安全系数；限制单次坍塌比例；弱点与普通区使用不同结构角色 |
| 怎么撞都不倒 | 铁材泛滥、Bond 过密、弱点遮挡、阈值只按 HP 增长 | 减少 Bond；保留割点/支配节点；按目标鸟数验证；提高弱点暴露度 |
| 绳链抖动或注入能量 | 段数过多、约束误差、Shock Propagation/Projection 过强 | 减段；合理 Sub-Step；降低修正强度；检查锚点初始距离 |
| 多样性优化后只剩宽底塔 | 评分只奖励稳定、缺少 Novelty | 难度先做窗口约束；加入轮廓签名距离和候选档案 |
| 炸药桶只是装饰 | 爆炸范围内没有关键边或有效二次目标 | 装置必须记录 Structural Target；生成后跑攻击探针 |
| 活塞效果方向随机 | 轴向未绑定预期失稳模式 | 以目标支撑面/配重为轴向目标，验证推出支撑区的距离 |

## 16. 最终推荐

M7.3 的核心作品价值不应是“能随机生成很多砖”，而应是：生成器知道每块砖为什么在那里、它支撑了谁、玩家应该打哪里、打中之后结构为什么会倒。

优先实现顺序是：

1. 支撑图和静态稳定校验；
2. 空载 Chaos 验证；
3. 图分析驱动的弱点；
4. 弱点/非弱点攻击对照；
5. 轮廓和材料多样性；
6. 绳、链、炸药桶和活塞的连锁语法；
7. 候选搜索与 Novelty 筛选。

若时间有限，宁可只支持三种轮廓并保证每种都稳定、有解、有明显弱点，也不要生成十几种无法解释承重关系的随机建筑。

## 17. 参考资料

### 论文

1. Matthew Stephenson, Jochen Renz. *Procedural Generation of Complex Stable Structures for Angry Birds Levels*. IEEE CIG 2016. DOI: [10.1109/CIG.2016.7860410](https://doi.org/10.1109/CIG.2016.7860410). [开放 PDF](https://users.cecs.anu.edu.au/~jrenz/papers/stephenson-renz-cig16.pdf)。
2. Matthew Stephenson, Jochen Renz. *Generating Varied, Stable and Solvable Levels for Angry Birds Style Physics Games*. IEEE CIG 2017. DOI: [10.1109/CIG.2017.8080448](https://doi.org/10.1109/CIG.2017.8080448)。
3. Lucas Ferreira, Claudio Toledo. *A Search-Based Approach for Generating Angry Birds Levels*. IEEE CIG 2014. DOI: [10.1109/CIG.2014.6932912](https://doi.org/10.1109/CIG.2014.6932912)。
4. Laura Calle, Juan J. Merelo, Antonio Mora-García, José-Mario García-Valdez. *Free Form Evolution for Angry Birds Level Generation*. EvoApplications 2019. DOI: [10.1007/978-3-030-16692-2_9](https://doi.org/10.1007/978-3-030-16692-2_9)。
5. Pascal Müller, Peter Wonka, Simon Haegler, Andreas Ulmer, Luc Van Gool. *Procedural Modeling of Buildings*. SIGGRAPH 2006. DOI: [10.1145/1141911.1141931](https://doi.org/10.1145/1141911.1141931)。
6. Julian Togelius, Georgios N. Yannakakis, Kenneth O. Stanley, Cameron Browne. *Search-Based Procedural Content Generation*. EvoApplications 2010. DOI: [10.1007/978-3-642-12239-2_15](https://doi.org/10.1007/978-3-642-12239-2_15)。

### 引擎与游戏机制资料

1. Epic Games. [Physics Constraint Reference](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-constraint-reference-in-unreal-engine)。
2. Epic Games. [Physics Sub-Stepping](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-sub-stepping-in-unreal-engine)。
3. Epic Games. [Chaos Destruction Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-destruction-overview)。
4. Red Faction Wiki. [Geo-Mod 2.0](https://www.redfactionwiki.com/wiki/Geo-Mod_2.0)。该页用于确认预破碎、应力/动能响应及战略性削弱关键结构点的公开描述。
