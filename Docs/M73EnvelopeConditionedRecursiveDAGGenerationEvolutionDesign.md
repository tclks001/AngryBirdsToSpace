# M7.3：轮廓约束递归 DAG 建筑生成演进设计

> 文档性质：M7 后续建筑生成演进的参考设计，不表示本文链路已经实现或已经通过生产验收。
>
> 执行状态：Shape Grammar / WFC 语义轮廓前端继续复用；本文原定的 Plate 拉伸、整板楼层拟合
> 和基于 Plate 的后续递归目前**暂不进行**。当前实现路线改为
> [长条梁式结构](M73BeamBlockStructuralGenerationResearch.md)，并从
> [Beam-A](M73BeamAStructuralIRPreviewDesign.md) 建立 Bay / Joint / Member / Assembly IR。
>
> 设计决定：采用用户提出的“Shape Grammar / WFC 生成外轮廓和初始 DAG，
> Expansion 生成完整细分 DAG，楼板拟合轮廓，最后联合补柱与弱点”的串行架构。
>
> 当前基线：[DAG5-A/B 候选搜索与语义轮廓](M73DAG5CandidateSearchSemanticEnvelopeAndProductionDesign.md)
> 已完成可复现的候选搜索、四类语义包络、局部 WFC、Support Port 和真实 Brick 接入；
> 它作为语义包络原型与回归基线保留，但不再被视为最终建筑生成架构。
>
> 轮廓前端：[DAG5-B v2 复杂建筑轮廓预览](M73DAG5Bv2ComplexSilhouettePreviewDesign.md)
> 已实现递归 Shape Grammar、体量邻接 WFC 与 Box/Prism/Pyramid 编辑器程序化预览；
> 当前待用户视觉验收，尚未进入 Seed DAG、Expansion 或物理建筑链。
>
> 父级：[M7.3-DAG 递归承载图总体设计](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)。
> 相关研究：[3D WFC 建筑外观体块与承载 DAG 拟合](M73WFCBuildingEnvelopeAndDAGFittingResearch.md)。
> 梁式结构演进：[长条形积木建筑生成调研](M73BeamBlockStructuralGenerationResearch.md)。
> 总导航：[M7 建筑系统文档导航与执行路线](M7BuildingDevelopmentRoadmap.md)。
> 复用链路：[DAG-1 递归语法](M73DAG1RecursiveGrammarImplementationDesign.md) ·
> [DAG-2 空间布局与模块编译](M73DAG2SpatialLayoutAndModuleCompilationDesign.md) ·
> [DAG2.3 累计荷载与联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md) ·
> [DAG3 内部 Failure Frontier](M73DAG3InternalFailureFrontierDesign.md) ·
> [DAG-4 Settled Contact 与攻击对照](M73DAG4SettledContactAndAttackRolloutDesign.md)。

## 1. 问题与设计结论

当前系统已经分别证明了三件事：

1. DAG-1 Expansion 能确定性地把抽象承载节点展开成更深的 Series / Parallel DAG；
2. DAG5-B 能通过 Shape Grammar、局部 WFC、`MustVoid` 和 Support Port 生成明显不同的
   语义轮廓；
3. DAG2.3、DAG3 和 DAG-4 能验证最终砖块的稳定性、弱点切面和 Chaos 响应。

但当前 DAG5-B 和 DAG-1 不是串行关系。启用 `bEnableSemanticEnvelope` 时，
`FABTSM73DAGBuildingPipeline` 直接消费
`FABTSM73DAG5BSemanticEnvelopeBuilder::Build` 输出的 Macro Graph；
未启用时才调用 `FABTSM73DAGGrammarExpander::Generate`。语义路径创建的 Macro Node
`ExpansionDepth` 固定为 `0`。

因此当前可见结果本质是：

```text
固定 ShapeFamily
  -> 直接 Macro Graph
  -> 局部 WFC 掩码与 Support Port
  -> 一组最终 Plate / Column
  -> 可选的事后 Failure Pattern 改写
```

它可以证明“轮廓语义能够约束真实砖块”，却不能证明：

- Shape/WFC 输出能够继续递归成长为大型建筑；
- 增加 Expansion 深度会在既有轮廓内增加结构复杂度；
- 楼板能够根据轮廓分别伸缩、分段并避开空洞；
- 弱点与结构骨架是联合生成，而不是对成品柱网事后删改。

正式演进链路定为：

```text
Encounter / Site / Difficulty
  -> Shape Grammar：宏观体块与 Chunk Graph
  -> 局部 WFC：语义占据、空洞与边界端口
  -> SemanticEnvelope
  -> Seed Macro DAG
  -> Envelope-conditioned Expansion
  -> Expanded Macro DAG
  -> DAG 到轮廓的受限空间嵌入
  -> 楼板分片、牵拉与尺寸拟合
  -> Failure Frontier 预留
  -> 柱网与联合支撑补全
  -> Brick Assembly
  -> Realized Contact DAG
  -> 静态、settled Contact、Chaos 与 Novelty 认证
```

Shape/WFC 与 Expansion 必须串行，而不是二选一。

## 2. 目标与非目标

### 2.1 目标

- Shape Grammar 决定建筑的大轮廓、体量层级、退台、偏置、桥接、开洞和高低关系；
- WFC 在每个有限局部格内决定语义相容、空洞、框架和可用 Support Port；
- 从语义包络提取一张粗粒度初始 DAG，而不是直接生成最终砖块 DAG；
- Expansion 在每个轮廓 Scope 内递归增加楼层、分支、汇聚、横跨和内部开洞；
- 将扩展后的楼板节点拟合到各自轮廓截面，不再只生成相同宽度的完整方板；
- 在同一求解事务内预留弱点切面、补足普通支撑并验证无旁路；
- 通过多 Chunk 拼接扩大建筑物理范围，而不是只提高单一 WFC 网格分辨率；
- 保持纯 C++、确定性、有界搜索、失败关闭和现有 Chaos 运行时兼容。

### 2.2 非目标

- 不让 WFC 直接决定最终物理砖或最终承重边；
- 不追求任意连续曲面、斜墙或任意网格体建筑；
- 不以全地图、无限体素或无界 WFC 生成建筑；
- 不假设任意 Shape、任意递归深度和任意弱点组合都存在物理解；
- 不以删除承重构件、降低最小深度或回退 Legacy 的方式掩盖无解；
- 不以外观壳遮盖真实物理砖，所有可见主体仍来自现有 Brick / Module 链；
- 不在本参考设计中改变 M3 Encounter、TaskGraph 或共享世界生成合同。

## 3. 各层权威边界

| 层 | 回答的问题 | 不能声称的内容 |
|---|---|---|
| Shape Grammar | 建筑由哪些宏观体量组成，如何 Stack / Split / Offset / Bridge / Carve | 最终柱子位置与真实稳定性 |
| 局部 WFC | 哪些局部语义相邻，哪里必须实体、可选实体或必须空 | 最终承重边与弱点机械效果 |
| Seed Macro DAG | 哪些宏观区域必须建立粗粒度载荷关系 | 已经拥有足够的内部复杂度 |
| Expanded Macro DAG | 每个 Scope 内的完整承载意图、串并联和汇聚结构 | 最终碰撞接触一定成立 |
| Layout / Plate Fitting | DAG 节点如何落入轮廓，楼板如何分片和缩放 | Chaos 下必然稳定或按预期坍塌 |
| Support / Weakness Synthesis | 哪些柱是普通支撑，哪个切面是弱点，如何避免旁路 | 最终实际接触事实 |
| Realized Contact DAG | 最终碰撞体之间真实存在的接触和载荷路径 | 冲击后的动态效果 |
| DAG-4 / Chaos | 玩家击中弱点后是否产生预期主体运动 | 其他 Seed 或其他建筑自动成立 |

任何下游物理层都可以拒绝上游候选；拒绝后回到 DAG5-A 的有界候选序列，不得修改
已经失败的事实或切回旧矩形 Preset。

## 4. 稳定中间数据

### 4.1 Shape Chunk Graph

大型建筑先表示为少量 Chunk，而不是一个不断放大的 WFC 网格：

```text
ShapeChunkId
ParentChunkId
DerivationPath
LocalFrame
NormalizedBounds
ChunkRole
NeighborChunkIds[]
BoundaryPortContracts[]
ThemeTag
Seed
```

Shape Grammar 在 Chunk 层执行：

- `Stack`：垂直叠加体量；
- `SplitHorizontal`：左右或前后分区；
- `Setback` / `Offset`：改变上层占地与质心；
- `Bridge`：连接两个已有 Chunk；
- `CarveThroughOpening`：建立必须贯通的空域；
- `Cantilever`：建立带回锚要求的悬挑体量；
- `Crown` / `AsymmetricHeight`：制造非统一屋顶线。

Chunk Graph 只确定低频轮廓和相邻关系，不生成 Brick。

### 4.2 SemanticEnvelope

每个 Chunk 内运行现有有限格 WFC，输出：

```text
MustOccupy
MayOccupy
MustVoid
FloorCarrierBands
SupportPorts
LoadPorts
BridgePorts
FramePorts
WeaknessCandidateSockets
AttackClearance
ShapeDerivationPath
ChunkBoundaryPorts
EnvelopeHash
```

现有 `FABTSM73SemanticEnvelope`、Cell、Scope、Port、Hash 和审计逻辑继续复用。
需要新增 Chunk 身份和跨 Chunk 边界端口，而不是把 `GridSizeX/Y/Z` 无限增大。

### 4.3 Seed Macro DAG

Seed DAG 是轮廓的结构摘要，不是最终承载图。节点粒度应对应：

- 地基岛；
- 一段主要楼板带；
- 一座塔体或附楼；
- 一段桥跨；
- 一个必须保留的门洞/空腔两侧框架；
- 一个候选 Failure Frontier 上下游体量。

建议节点至少包含：

```text
SeedNodeId
SourceChunkId
SourceSemanticRegionIds[]
SemanticRole
AllowedExpansionScope
RequiredBoundaryPorts[]
ForbiddenVolumes[]
MinExpansionDepth
MaxExpansionDepth
EstimatedCostRange
bGround
bLoadTerminal
bFrontierCandidate
```

边由上下游语义区之间的 Support / Load / Bridge Port 推导。初始图必须：

- 有向无环；
- 所有非 Ground 节点可达某个 Ground；
- 不穿越 `MustVoid`；
- 不把一个 WFC Cell 等同于一块砖；
- 不提前固定最终柱数。

### 4.4 Expanded Macro DAG

Expansion 输出的每个节点继承轮廓上下文：

```text
MacroNodeId
SourceSeedNodeId
SourceChunkId
DerivationPath
ExpansionDepth
SemanticRole
AllowedScope
RequiredPorts[]
ForbiddenVolumes[]
EstimatedMassRange
FailureRole = Ordinary | WeakCandidate | PivotCandidate
```

这使同一递归规则在不同轮廓区域产生不同空间结果。例如同样的 Parallel：

- 在窄塔中形成两条近距离柱链；
- 在桥跨下形成左右塔肢；
- 在门洞两侧形成分离框架；
- 在退台层形成偏置的联合支撑。

## 5. 生成算法

### 5.1 阶段 A：宏观轮廓和 Chunk 拼接

1. 根据 Encounter 的占地、高度、攻击方向和视觉标签选择 Shape Root；
2. 在 Shape 操作预算内推导 Chunk Graph；
3. 为每个 Chunk 分配局部坐标、目标尺寸和边界端口；
4. 先写入门洞、桥下、攻击通道等硬 `MustVoid`；
5. 在每个 Chunk 内运行有限 WFC；
6. 对相邻 Chunk 执行边界端口相容传播；
7. 任一 Chunk 无解时拒绝整个候选，由 DAG5-A 更换 Shape/WFC Seed。

同一 Chunk 的 WFC 仍建议保持约 `5~9 × 3~5 × 4~8`。建筑变大时增加 Chunk 数量，
而不是让单次传播域按体积无界增长。

### 5.2 阶段 B：从轮廓提取初始 DAG

对每个连通 `FloorCarrierBand` 生成候选载荷节点，对 Foundation / ColumnZone /
BridgePort 生成候选支撑节点，再按端口建立粗粒度边：

```text
Semantic regions
  -> merge by Chunk、height band、connectivity and role
  -> Seed nodes
  -> candidate support edges through compatible ports
  -> remove cycles
  -> verify ground reachability
```

这一阶段只保留结构必需关系。局部冗余、柱数和弱点切面由后续 Expansion 与联合补全决定。

### 5.3 阶段 C：轮廓约束递归 Expansion

Expansion 不再对统一根盒体盲目二分，而是对每个 Seed Node 的
`AllowedExpansionScope` 递归：

1. 读取当前节点的可用体积、语义角色、端口、空洞和剩余成本；
2. 枚举与该 Scope 相容的规则；
3. 预计算每条规则的最小子 Scope、最小 Brick 成本和弱点预留；
4. 按稳定 Seed 排序尝试；
5. 将子 Scope 与 SemanticEnvelope 相交；
6. 若子节点无法连接必需端口、侵入 `MustVoid` 或预算不足，则回溯该规则；
7. 到达最小深度后，允许在局部空间不足时终止；
8. 输出完整 Expanded Macro DAG。

建议新增的规则约束：

| 规则 | 轮廓约束 |
|---|---|
| `SeriesSplit` | 子节点沿载荷方向分层；分界必须落在合法 FloorCarrierBand |
| `ParallelSplit` | 两条分支必须分别占据不重叠可用区，并在下游拥有联合支撑空间 |
| `OffsetSeries` | 上下子 Scope 允许横向偏移，但投影和回锚满足稳定下界 |
| `BridgeSplit` | 两端必须连接不同支撑岛，桥下保持 `MustVoid` |
| `Merge` | 汇聚节点必须拥有足够 Plate 面积容纳多路载荷端口 |
| `OpeningPreservingSplit` | 子 Scope 分布在门洞/空腔两侧，禁止跨空洞生成整板 |

深度是每个 Scope 的局部约束，不是所有初始节点统一复制同一深度。狭窄塔尖可以较早终止，
宽大主楼可继续展开；`MinExpansionDepth` 仍必须在候选的可行域预检中得到满足。

### 5.4 阶段 D：Expanded DAG 的受限空间嵌入

空间求解按以下顺序进行：

1. 固定 Chunk 边界、`MustVoid`、跨 Chunk Port 和 Ground Anchor；
2. 固定 Expanded DAG 中的必需先后与支撑关系；
3. 为每个楼板节点选择目标高度带；
4. 根据其子树载荷、相邻端口和轮廓截面求二维目标 Footprint；
5. 对并联分支分配互不冲突的子区域；
6. 对汇聚节点保留联合支撑凸包；
7. 对偏置、悬挑和桥跨执行回锚与净空约束；
8. 无合法嵌入时拒绝候选，不把所有节点压回中央方盒。

布局目标是“Expanded DAG 拟合轮廓”，不是“轮廓强行拟合已经定型的方塔 DAG”。

### 5.5 阶段 E：楼板分片、牵拉与尺寸拟合

用户提出的“牵拉楼板”实现为受限 Footprint Fitting：

```text
目标楼板区域 =
  当前高度带内的 MustOccupy
  ∩ 当前 Macro Node 的 AllowedScope
  ∩ 与必需 Load/Support Port 相连的区域
  - MustVoid
```

对目标区域执行：

1. 将占据格合并为若干最大轴对齐矩形；
2. 以 Macro Node 的承重点和端口为锚选择必要矩形；
3. 沿 X/Y 方向连续缩放标准楼板，使边界贴合目标 Footprint；
4. 非凸、L 形或被门洞切开的区域必须拆成多块楼板；
5. 桥跨只连接指定两端，不得以完整大板填平桥下空域；
6. 尺寸必须落在可制造范围和长宽比门槛内；
7. 相邻楼板只在设计接口处建立预期接触。

“牵拉”只作用于楼板尺寸和位置，不允许任意缩放全部砖块，也不允许一块板跨越
`MustVoid`。这能保留现有长方体 Brick 词汇，同时让最终结构服从不规则轮廓。

### 5.6 阶段 F：弱点预留与普通支撑联合补全

弱点不能在完整柱网生成后再事后删柱。推荐顺序：

1. 在 Expanded Macro DAG 上枚举满足高度、受影响质量和可攻击性的 Failure Frontier；
2. 选择 `WeakCandidate`、`PivotCandidate` 与普通支撑区域；
3. 为目标 Frontier 预留攻击净空、自由倾倒/滑移空间和禁止旁路区；
4. 在剩余 Support Port 内求解普通柱和联合支撑；
5. 完整态验证所有载荷都能到 Ground，COM 位于支撑凸包安全区；
6. 失效态移除弱点后验证目标主体产生 Tip / Drop / SlideThenTip，且无重新落座捷径；
7. 若支撑补全只能跨过弱点形成旁路，则拒绝该候选。

因此“补柱子”和“补弱点”是同一约束求解问题：

```text
完整态必须稳定
AND 弱点失效态必须具有目标运动
AND 普通支撑不能形成跨 Frontier 旁路
```

DAG3 既有 W/P 分区、FrontierHash、PatternHash、攻击净空和 DAG-4 动态认证继续复用，
但 Weakness Planner 的输出要前移为支撑补全的输入。

### 5.7 阶段 G：编译与物理认证

后续权威链保持不变：

```text
Expanded DAG + Fitted Plates + Solved Supports
  -> FABTSM73StructureData
  -> Runtime Modules / collision
  -> Realized Contact DAG
  -> MustOccupy / MustVoid / Required Contact / Bypass audit
  -> Idle settled Contact rebuild
  -> Weak vs ordinary Chaos rollout
```

Shape/WFC、Seed DAG 或 Expanded DAG 都不能代替 Realized Contact DAG。

## 6. 大型建筑的 Chunk 化扩展

### 6.1 分辨率与物理范围分离

当前 `GridSizeX/Y/Z` 同时承担语义分辨率，但建筑 Target Width / Depth / Height 仍是单一
目标盒体。只增大 Grid 会得到更细的同一轮廓，不会自然得到更多宏观塔体和附楼。

新设计拆成两个维度：

```text
ChunkGraphExtent = 建筑宏观范围与体量数量
LocalWFCGridSize = 每个 Chunk 的局部语义分辨率
```

例如：

```text
2 × 1 Chunk：主塔 + 附楼
2 × 2 Chunk：双塔 + 桥 + 前厅
L 形 Chunk：主楼 + 侧翼
3 层 Stack：底座 + 主体 + 偏置冠部
```

### 6.2 跨 Chunk 合同

相邻 Chunk 只能通过显式边界 Port 连接：

```text
BoundaryPortId
SourceChunkId
TargetChunkId
Role = Support | Load | Bridge | Passage | MustRemainOpen
LocalPatch
MinimumContactArea
MaximumOffset
RequiredSemanticPair
```

局部 WFC 必须在边界格满足 Port 语义；Seed DAG 必须包含相应跨 Chunk 边；Module Compiler
必须生成匹配的实际接触。三者 Hash 不一致时拒绝候选。

### 6.3 有界求解

- Shape Chunk 数有硬上限；
- 每个 Chunk 有独立传播和回溯预算；
- 跨 Chunk 只传播边界端口，不传播整个邻居域；
- Expansion 预算按 Chunk/Seed Node 分配，同时有全局上限；
- 先验证 Chunk 局部可行性，再验证跨 Chunk DAG；
- 只有通过静态漏斗的少量候选进入 DAG-4 Chaos。

这保证扩大结构不等于指数放大全局 WFC。

## 7. 候选搜索与确定性

### 7.1 候选身份

一个候选的随机身份拆成稳定子 Seed：

```text
ShapeSeed
ChunkWFCSeed[ChunkPath]
SeedDAGSeed
ExpansionSeed[DerivationPath]
EmbeddingSeed
SupportSeed
WeaknessSeed
MaterialSeed
```

均由以下稳定输入派生：

```text
LogicalBuildingSeed
+ GeneratorVersion
+ CandidateIndex
+ StablePath
+ StageVersion
```

禁止用共享 `FRandomStream` 的调用顺序隐式决定所有阶段，否则上游插入一个 Chunk 会使
整栋建筑的弱点、材料和后续分支全部漂移。

### 7.2 有界搜索漏斗

DAG5-A 的候选尝试扩展为以下顺序：

1. Shape / Chunk 容量预检；
2. 局部 WFC 可解性；
3. Seed DAG 无环与 Ground 可达；
4. Expansion 最小深度、节点和 Brick 成本预检；
5. 受限空间嵌入与楼板拟合；
6. Failure Frontier 与支撑联合补全；
7. 累计荷载、COM、接触和 MustVoid 审计；
8. DAG3-C 攻击可达性；
9. 仅高分候选运行 DAG-4 动态认证；
10. DAG5-C 再进行六栋联合 Novelty 选择。

每个阶段必须原子提交。任一阶段失败时不得向下游泄漏部分 Graph、Layout 或
`StructureData`。

### 7.3 规范身份

最终 `SearchHash` 应覆盖：

- Shape Chunk Graph 与推导路径；
- 每个 Chunk 的 WFC Collapse 与边界 Port；
- SemanticEnvelope；
- Seed DAG；
- Expanded DAG 和 Expansion 路径；
- Plate Fit / Support / Weakness Plan；
- Brick Transform、Material Profile 与 Realized Contact DAG；
- 静态、攻击可达和动态认证结果。

任何新增字段或改变规范排序时提升对应 Stage Version。

## 8. 建议的未来配置

以下字段是未来演进接口，不表示当前 Actor 已拥有：

### Envelope / Chunk

```text
bEnableEnvelopeConditionedExpansion
ShapeGrammarVersion
MinShapeChunkCount
MaxShapeChunkCount
MaxChunkGraphDepth
LocalWFCGridSizeX/Y/Z
MaxWFCPropagationOperationsPerChunk
MaxWFCBacktrackStepsPerChunk
MaxCrossChunkPortCount
```

### Seed DAG / Expansion

```text
SeedDAGVersion
MinSeedMacroNodeCount
MaxSeedMacroNodeCount
MinExpansionDepthByRole
MaxExpansionDepthByRole
ExpansionStepBudgetPerChunk
GlobalExpansionStepBudget
Series / Parallel / Offset / Bridge / Merge Weights
```

### Layout / Plate Fitting

```text
MinPlatePatchWidthCM
MaxPlatePatchAspectRatio
MaxPlatePatchesPerMacro
PlateFitSnapCM
MaxAllowedEnvelopeMissRatio
MaxAllowedMayOccupySpillRatio
MaxCantileverRatio
MinBridgeEndContactCM
```

`MustVoid` 违规始终为零容忍，不提供可调放宽比例。

### Support / Weakness

```text
MinFailureFrontierHeightNormalized
MaxFailureFrontierHeightNormalized
ReservedWeaknessBrickCount
MinOrdinarySupportSafetyFactor
MinCompleteStateSupportMarginCM
MaxBypassSupportEdgeCount = 0
MaxSupportCompletionAttempts
```

## 9. 代码演进边界

### 9.1 复用

- `FABTSM73DAG5BSemanticEnvelopeBuilder` 的 Shape/WFC、Cell、Port、Hash 和审计；
- `FABTSM73DAGGrammarExpander` 的确定性路径 Seed、Series/Parallel IR 和预算终止；
- DAG5-A 候选搜索、原子提交和实砖硬预算；
- DAG2.3 累计荷载、联合支撑和真实接触；
- DAG3 Frontier、Pattern、攻击净空和 DAG-4 动态对照；
- `FABTSM73DAGModuleCompiler`、GroundAdapter、Runtime Module 和 IdleValidation。

### 9.2 必须重构

- 语义路径和 GrammarExpander 由互斥分支改为串行阶段；
- Semantic Builder 不再把 4~8 个直接 Macro Node 当作最终 Graph；
- Layout Solver 从“一个 Macro 一块 Plate”升级为 Expanded DAG 的多 Plate Patch 拟合；
- Failure Pattern 从成品结构事后改写前移为支撑补全约束；
- 单一 WFC 网格升级为 Chunk Graph + 局部 WFC；
- DAG5-C 的 Novelty 签名必须基于新 Expanded DAG、Plate Fit 和联合弱点结果。

### 9.3 建议的新纯数据职责

```text
FABTSM73ShapeChunkGraphBuilder
FABTSM73SeedDAGBuilder
FABTSM73EnvelopeConditionedExpander
FABTSM73ExpandedDAGLayoutSolver
FABTSM73PlateFootprintFitter
FABTSM73SupportWeaknessSynthesizer
FABTSM73EnvelopeDAGContactAuditor
```

名称仅用于职责拆分；实现时可按现有文件规模调整，但不得把所有逻辑继续塞回
`AABTSM73StableBuildingActor`。

## 10. 兼容与迁移顺序

当前 DAG5-B 四类 Fixture 保留为 `SemanticEnvelopePrototype` golden，证明轮廓、
WFC、Port 和真实 Brick 合同未回归。新链路使用独立显式开关，在完成认证前不替换
TaskGraph 生产默认。

DAG5-B v2 已先行实现本文链路的“Shape Grammar + WFC + 多形体轮廓预览”部分。
它输出编辑器可见的语义体量，不生成物理建筑。后续串行接口切片应消费这份体量结果，
而不是重新创建另一套轮廓生成器。

建议按以下可回归切片演进：

1. **串行接口切片**：Semantic Builder 输出 Seed DAG；Expansion 深度 0 时必须复现
   当前四类 Fixture；
2. **轮廓约束 Expansion**：允许 Seed Node 在自身 Scope 内递归，验证深度增加会改变
   内部层级和支撑路径；
3. **楼板拟合切片**：支持一节点多 Plate Patch、非凸轮廓、门洞和桥下空域；
4. **支撑/弱点联合切片**：Frontier 预留先于柱网补全，消除事后改写无解；
5. **多 Chunk 切片**：实现边界 Port 和大型轮廓拼接；
6. **联合候选切片**：DAG5-C 基于最终架构选择六栋；
7. **生产认证切片**：DAG5-D/E 消费 Encounter 元数据并逐栋重跑静态、PIE、Chaos 和性能。

迁移期间允许两条显式测试链并存：

```text
CurrentSemanticEnvelopePrototype
EnvelopeConditionedRecursiveDAG
```

但新链失败不得自动回退旧链冒充成功。生产默认切换必须是单独评审提交。

## 11. 正式验收门槛

### 11.1 纯数据与确定性

- 同一完整输入重复生成的 Chunk Graph、WFC、Seed DAG、Expanded DAG、Plate Fit、
  Weakness Plan、Brick 和所有 Hash 完全一致；
- Seed DAG 和 Expanded DAG 均无环，所有主体节点 Ground 可达；
- 每个 Expanded Node 可追溯到 Seed Node、Chunk、Semantic Region 和 Derivation Path；
- 所有搜索均受明确操作预算约束；
- 失败候选不发布部分结果，且不存在 Legacy fallback。

### 11.2 轮廓与递归联合验收

- 递归深度增加时，不只是上下整体重复，而会在包络允许范围内增加楼层、分支、
  汇聚、桥跨或局部非对称；
- 至少覆盖退台主塔、双塔桥、贯穿开洞、主楼加附楼、高低错落和 L 形体量；
- 楼板 Footprint 随高度和 Shape Scope 改变；
- 非凸截面由多块 Plate 构成，不得生成跨 `MustVoid` 的完整方板；
- `MustVoid` 零 Brick，必需 `MustOccupy` 和跨 Chunk Port 全部实现；
- 增加 Chunk 数会扩大宏观结构，而增加局部 Grid 分辨率只改变局部语义细节。

### 11.3 支撑与弱点联合验收

- 完整态通过累计荷载、联合支撑、COM/Hull 和 Idle 稳定；
- 弱点属于 Expanded DAG 的主体 Failure Frontier，而不是顶端附加模块；
- 弱点失效后产生目标 Tip / Drop / SlideThenTip；
- `BypassSupportEdgeCount == 0`；
- 弱点攻击显著优于三个普通攻击点；
- 支撑柱数量和位置随轮廓、Expanded DAG 与弱点共同变化，不固定为三柱或四柱模板。

### 11.4 大型结构和生产验收

- 至少有三个多 Chunk 固定 Seed：双体、L 形和主楼加附楼；
- 跨 Chunk 支撑在设计 Port、实际接触和 Contact DAG 中三方一致；
- 六栋候选的轮廓、Expanded DAG、楼板占据和弱点联合签名不重复；
- 每栋通过 Editor 读形、PIE 完整性、DAG3-C、DAG-4 和性能门槛；
- 正式生产切换前仍执行 `-ForceUnity -DisableAdaptiveUnity` 与 fresh 自动化。

## 12. 建议诊断与拒绝码

诊断视图应能逐层切换：

```text
Shape Chunk Graph
Local WFC / SemanticEnvelope
Seed Macro DAG
Expanded Macro DAG
Plate Fit Patches
Support / Weakness Plan
Realized Contact DAG
```

建议稳定拒绝码：

```text
EnvelopeExpansionNoSeedDAG
EnvelopeExpansionCycle
EnvelopeExpansionGroundUnreachable
EnvelopeExpansionNoFeasibleRule
EnvelopeExpansionScopeTooSmall
EnvelopeExpansionBudgetExceeded
PlateFitNoCarrierBand
PlateFitMustVoidOverlap
PlateFitPatchBudgetExceeded
CrossChunkPortMismatch
WeaknessReservationNoFrontier
SupportCompletionNoStableSolution
SupportCompletionUnexpectedBypass
```

每个拒绝都记录 CandidateIndex、Stage Seed、Chunk/Node/Path 和当前预算。

## 13. 对现有 DAG5 路线的影响

- DAG5-A 仍是正确的有界候选调度器；
- 当前 DAG5-B 作为“语义轮廓能进入真实物理砖”的原型阶段保留并冻结回归；
- 本文链路是 DAG5-B 后续的建筑骨架演进，不否定已完成的四类轮廓验收；
- DAG5-C 的候选池基础设施可以复用，但正式六栋 Novelty 基线应等待新链路输出；
- DAG5-D/E 的 Encounter 接入与生产认证目标不变；
- DAG-4 的弱点多样性联合调优应在新 Expanded DAG 和支撑/弱点联合补全后进行。

最终目标不是让 WFC、Shape Grammar、Expansion 或弱点规划中的某一层单独“生成建筑”，
而是让它们通过稳定中间合同共同生成一栋：

```text
轮廓可读
+ 递归结构丰富
+ 完整态稳定
+ 弱点机械效果明确
+ 可由真实 Brick 和 Contact DAG 证明
```
