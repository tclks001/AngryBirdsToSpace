# M7.3-DAG-3：内部 Failure Frontier

> 状态：DAG3-A 纯数据“失效前沿发现”已落地并完成自动化基线；DAG3-B 三种纯数据、同材质的宏观接口两遍事务改写已经完成代码、DAG3 全前缀 11/11 及 DAG2.3/M7 路由/世界契约/B2/M10 smoke 回归，当前阶段待可见几何/PIE 验收。生产 Profile 继续同时关闭分析与几何改写，因此现有 DAG2.3 的 13/17/13 模块、`DAGTopologyHash`、材质和 `WeakPoints=0` 合同均不得改变。DAG3-C 攻击可达性/材质/Profile 路由、DAG-4 settled/Chaos 对照及正式可见 PIE 仍未完成，不能据此宣称内部弱点玩法已经上线。
>
> 父级：[递归承载 DAG 生成总稿](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)。前置：[DAG2.3 累计荷载与联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md)。生产边界：[M7 TaskGraph 球面建筑集成](M7TaskGraphSphericalBuildingIntegrationDesign.md)。后续：[语义 WFC 与 DAG 拟合](M73WFCBuildingEnvelopeAndDAGFittingResearch.md)。

## 1. 目标与阶段边界

DAG-3 的玩家目标不是把一块顶部砖涂成弱材质，而是在建筑主体内部生成可读的承载瓶颈：

```text
完整态：Ground -> 内部承载前沿 -> 中上部主体
击毁前沿：中上部主体失去全部设计承载路径
```

为避免同时调试图论、几何改写、材质破坏和 Chaos，DAG-3 分为以下收敛顺序：

| 子阶段 | 内容 | 当前状态 |
| --- | --- | --- |
| DAG3-A | 从 DAG2.3 编译后的物理接触 DAG 发现、度量和稳定选择内部 Failure Frontier | 已实现 |
| DAG3-B | 把物理前沿提升为稳定 Macro 接口 Intent，并以第二遍 DAG2.3 重求解实现 `InternalSingleSupport`、`InternalAsymmetricDualSupport`、`InternalOffsetSeam` | 代码与自动化/兼容回归完成，待可见几何/PIE |
| DAG3-C | 加入扫掠空间、攻击可达性、材质与 Profile 路由，并以显式 opt-in 接入生产候选 | 未实现 |
| DAG-4 | Idle 后重建 settled Contact DAG，并执行弱点/非弱点 Chaos 对照 | 未实现 |

DAG3-A 明确不做以下事情：

- 不增删或移动任何砖块；
- 不替换材质，不创建 `WeakPointRecord`；
- 不改变 DAG2.3 `DAGTopologyHash`；
- 不启用生产 Profile；
- 不以静态图切断代替完整态稳定、失效态位移和实际击打验收；
- 不承担 WFC 包络、六栋去重建筑或 Encounter 难度消费。

DAG3-B 的本轮边界同样明确：

- 只改变纯数据布局、支撑列角色和编译后的结构候选，不在已编译 `Bricks` 上事后挪砖；
- 整栋保持原有单一材料，不创建 `FABTSM73WeakPointRecord`，不调用 Legacy B/B2 `WeakPointPlanner`；
- 不增加隐藏伤害倍率，不把“未来可破坏支撑”提前包装成已经可玩的弱点；
- 不执行攻击扫掠、地形/建筑遮挡射界、真实鸟击打或弱点/非弱点 Chaos 对照；
- 不修改 TaskGraph 生产 Profile 默认值，不允许 DAG Reject 回退 Legacy；
- 不以纯数据 Tip/Slide 代理替代 DAG-4 settled Contact、实际自由位移、二次碰撞和可见 PIE。

## 2. 输入与唯一数据链

第一遍分析器只读取 DAG2.3 已编译并通过真实接触审计的 `FABTSM73StructureData`：

```text
同一份 DAG-1 Macro Graph
-> 第一遍 DAG2.1 / DAG2.2 / DAG2.3 Layout + LoadSupportSolver
-> 第一遍 ModuleCompiler + DAGContactGraphBuilder
-> DAG3-A FailureFrontierAnalyzer
-> 将物理 Frontier 提升为稳定 Macro Interface Intent
-> 第二遍 Layout + LoadSupportSolver（带 Intent 约束）
-> 第二遍 ModuleCompiler + DAGContactGraphBuilder
-> 完整态 StabilityValidator + 第二遍 FailureFrontierAnalyzer
-> DAG3-B Pattern 静态反事实验证
-> GroundAdapter footprint
-> Actor 最终 StabilityValidator
```

关键输入是：

- `Bricks`：稳定 `NodeId`、`MacroNodeId`、局部 AABB、材质；
- `SupportEdges`：由实际碰撞几何重建的 `LowerNodeId -> UpperNodeId`；
- `GroundNodeIds`：全部落地节点；
- `DAGPhysicalSupportMappings`：一组联合支撑柱与其 Load Plate 的权威对应；
- 运行时 Material Profile：用于按真实密度计算主体质量，而不是只数砖块。

`DAGFailureFrontierAnalysis` 存回第一遍 `FABTSM73StructureData`，但它是派生诊断数据，不拥有几何。DAG3-B 不能在这份已编译数据上直接移动 Column/Plate；`RealizedColumnCenters` 是 DAG2.3 已接受的权威几何，绕过 LoadSupportSolver 会让累计合力、联合 Hull、接触面积和实际 Collision 相互漂移。

### 2.1 宏观接口 Intent

DAG3-B 先把选中的物理 Candidate 解析回唯一的 Macro 接口：

```text
SourceFrontierHash
+ SupportMacroNodeId
+ LoadMacroNodeId
+ sorted AffectedMacroNodeIds
+ resolved Pattern
+ deterministic local failure direction
= FABTSM73DAGFailureRewriteIntent
```

- 物理 Column/cut set 必须映射到同一个 `LoadPlateNodeId` 和无歧义的 Macro Support/Load 接口；
- 基线 `NodeId` 只用于第一遍取证，第二遍编译可能重新分配物理节点，因此不得把旧 NodeId 当作改写地址；
- `AffectedMacroNodeIds` 是第二遍偏置、旁路保护和主体闭包的稳定边界；
- 找不到 Mapping、跨多个 Load Plate、接口歧义或方向退化时必须稳定拒绝。

### 2.2 两遍事务重求解

第二遍从同一份 Macro Graph 重新执行 Scope、累计荷载与联合支撑求解。`FABTSM73DAGLayoutSolver` 和 `FABTSM73DAGLoadSupportSolver` 只接受可空的 Rewrite Intent；关闭 DAG3-B 时继续走原 DAG2.3 路径。启用时：

1. Intent 约束目标 Macro 接口及跨 Frontier 的允许支撑；
2. `InternalOffsetSeam` 在 Layout 层移动整个受影响 Macro 闭包，而不是编译后挪砖；
3. LoadSupportSolver 重新生成权威 `RealizedColumnCenters` 及逐柱 Role；
4. ModuleCompiler 从第二遍 Layout 重新生成完整候选和 Mapping；
5. ContactGraphBuilder 重建真实接触，`MissingRequired` 或 `UnexpectedBypass` 均拒绝；
6. 先以 `FABTSM73StabilityValidator` 验证第二遍完整态并重新执行 `FABTSM73DAGFailureFrontierAnalyzer`，再由 Pattern 静态反事实认证 `W/P`；全部通过后才把候选整体提交到 `FABTSM73StructureData`。

任一步失败都拒绝整个建筑候选，不保留半改写数据，也不回退第一遍普通 DAG2.3 建筑。改写必须在 `GroundAdapter::AnalyzeFootprint` 前完成；否则 Offset Seam 改变的 Bounds、Ground 支点和施工台仍会使用陈旧数据。

## 3. 虚拟 GroundRoot 与定向切断语义

多地基建筑不能把任意一个地面节点当作唯一根。DAG3-A 在概念上建立一个不对应刚体的 `GroundRoot`，它指向所有 `GroundNodeIds`：

```text
Virtual GroundRoot
  -> GroundNode 0
  -> GroundNode 1
  -> ...
```

对候选移除集合 `C` 和其保护根集合 `R`：

1. 在忽略 `C` 的图上，从 `R` 向上收集 `ExpectedAffected`；
2. 在同一图上，从全部地面节点向上收集 `ReachableFromGround`；
3. `ActualAffected = ExpectedAffected - ReachableFromGround`；
4. 当 `ExpectedAffected` 非空且 `ActualAffected == ExpectedAffected` 时，该候选是完整定向切断。

这套定义能区分：

- 单链中的内部支撑：移除后上部全部失去地面路径；
- 双塔桥中的单侧柱：另一侧仍构成旁路，不是 dominator；
- 同一联合支撑接口的完整柱组：整组移除后才能切断 Load Plate。

DAG3-A 当前枚举两类物理移除原语：

| 类型 | 来源 | 用途 |
| --- | --- | --- |
| `DirectedNodeCut` | 每个非地面且拥有上游子节点的物理节点 | 发现单支撑内部瓶颈 |
| `SupportInterfaceCutSet` | `DAGPhysicalSupportMapping.ColumnNodeIds` | 发现联合支撑接口的小型 cut set |

同一 `LoadPlateNodeId` 由多条真实 Mapping 联合承载时，DAG3-A 会合并这些 Mapping 的柱节点并产生一个有界接口 cut set；每条 Mapping 都必须存在 `SupportPlate -> Column -> LoadPlate` 两段真实接触边。合并后的物理柱数超过 `MaxCutSetSize` 时不产生该组合候选，不做静默截断。任意 edge cut、跨不同 Load Plate 的组合和按“逻辑支撑组”而非物理柱计预算尚未进入首版；DAG3-B 若确有需要，只能增加有上界的搜索，不能直接做指数级全组合。

DAG3-A 故意保持 Pattern 无关。完整 directed cut 只能证明“全部 Ground 路径被切断”，不能据此把双柱集合命名为 `InternalAsymmetricDualSupport`：后者应保留强侧 Ground 路径，并由剩余支撑 Hull/COM 越界产生倾覆；`InternalOffsetSeam` 也必须由接缝、偏置和滑移几何定义。三种 Pattern 的分类与 `FABTSM73DAGFailurePatternSettings.Pattern` 过滤均留到 DAG3-B，DAG3-A 输出不得被下游当作已认证的改写类型。

### 3.1 Frontier、Weak 与 Pivot 的分离

DAG3-B 把完整接口边界记为 `F`，把未来攻击移除集合记为 `W`，把允许保留的强侧枢轴/偏心承托记为 `P`：

```text
F = W ∪ P
W ∩ P = ∅
```

统一合同是：

1. 完整态通过累计荷载、真实接触与静稳；
2. 同时移除 `F` 时，受保护主体闭包完全失去 Ground 路径，且边界外 `BypassSupportEdgeCount=0`；
3. Pattern 反事实只移除 `W`；
4. `P` 是显式允许的剩余支撑，不计为意外旁路；除 `P` 外不得存在第三条跨 Frontier 的承载路径；
5. `WeakNodeIds` 与 `RemainingSupportNodeIds` 分开记录，不能把 DAG3-A 的完整 `CandidateNodeIds` 原样解释成玩家需要全部摧毁的弱点集合。

因此，一个两节点 `SupportInterfaceCutSet={A,B}` 只能证明“A、B 全部移除后断路”。只有进一步指定 `W={A}`、`P={B}`，并证明 A 移除后 B 仍连接 Ground、但上部累计 COM 越出 B 的剩余 Contact Hull，才能命名为 `InternalAsymmetricDualSupport`。若 A、B 必须全部摧毁才产生效果，它仍只是两节点完整割集，不是非对称双支撑 Pattern。

### 3.2 三种 Pattern 的正式语义

| Pattern | `W` / `P` | 完整态 | 只移除 `W` 后的静态语义 | 预期运动 |
| --- | --- | --- | --- | --- |
| `InternalSingleSupport` | 一个真实承重支撑；`P` 为空 | 单支撑接触面积与载荷裕量合法 | 受影响主体闭包完全失去 Ground 路径 | `Drop`，允许随后偏转/二次撞击 |
| `InternalAsymmetricDualSupport` | 一条弱侧路径 + 一条强侧枢轴路径 | 组合 Hull 覆盖累计 COM | 强侧仍连 Ground，但累计 COM 越出强侧剩余 Hull；除强侧外无旁路 | `Tip`，围绕强侧倾覆 |
| `InternalOffsetSeam` | 弱键 + 显式偏心承托 | 上下主体在中腰形成非零错位且完整接触合法 | 弱键移除后保留沿确定方向的滑移/倾覆代理，并拒绝同构宽板原位承接 | `SlideThenTip` |

`TwoColumnLine`、`ThreeColumnTripod`、`FourColumnFootprint` 只是 DAG2.1 的物理接触 Footprint；柱数和 `SupportPattern` 不能替代上述反事实。三种 DAG3-B Pattern 必须在 Macro 接口约束、`W/P` 分区、几何、预期方向和 `RealizedPatternHash` 上真实不同，不能只回显不同枚举。

### 3.3 请求模式与有界尝试

`FABTSM73DAGFailurePatternSettings.Pattern` 为显式请求或 `Auto`：

- 显式 Pattern 只尝试该改写；不可行时明确拒绝，不静默换成另一 Pattern；
- `Auto` 按已接受 Frontier 的稳定顺序遍历，并以 `BuildingSeed ^ FrontierHash` 对三种 Pattern 基序做确定性轮转；它在 `MaxRewriteAttemptCount` 内选择第一个完整通过的事务，相同输入必须得到相同轮转与结果；
- 超过尝试预算、砖预算或接口不兼容时失败关闭；
- 排序、方向、数组和 Hash 均使用稳定 Macro 身份与量化值，不依赖 `TMap` 遍历顺序或第二遍临时 NodeId。

## 4. 主体影响门槛

静态“图被切断”还不足以成为玩家弱点。每个候选还要通过：

| 参数 | 默认值 | 含义 |
| --- | ---: | --- |
| `MinNormalizedHeight` | 0.15 | 候选不能退化为最底部地基拆除 |
| `MaxNormalizedHeight` | 0.60 | 候选不能回到最高处局部顶冠 |
| `MinMainBodyAffectedMassRatio` | 0.20 | 至少影响 20% 主体质量 |
| `TargetMainBodyAffectedMassRatio` | 0.45 | 确定性排序优先接近 45% |
| `MaxMainBodyAffectedMassRatio` | 0.75 | 防止单击清空整栋 |
| `MinAffectedHeightSpanNormalized` | 0.25 | 受影响主体必须跨越足够高度 |
| `MinAffectedMacroNodeCount` | 2 | 至少影响两个宏节点 |
| `MaxBypassSupportEdgeCount` | 0 | 默认不允许任何承载旁路 |
| `MaxCutSetSize` | 4 | 联合接口移除集合的硬上限 |
| `MaxCandidateCount` | 128 | 候选总数硬预算；超出时拒绝而不是静默截断 |

质量按 `体积 × MaterialProfile.DensityGPerCubicCM` 计算。`FABTSM73BrickNode.bFailureFrontierMainBody` 提供显式主体标记；辅助几何可以标为 `false`，从质量、主体 Bounds、受影响高度跨度和 Frontier 身份中排除。DAG3-B 在引入新的 Helper/Carrier/Payload 时必须显式分类，不能靠大体积装饰抬高坍塌比例或改变归一化高度。

`BypassSupportEdgeCount` 统计从保护闭包外进入闭包、且仍能把闭包节点连接到 GroundRoot 的物理支撑边。默认门槛为零；完整 directed cut 和旁路计数是两个独立诊断，日志与测试都要保留。

### 4.1 材料质量口径

DAG3-A 使用真实材料密度，但 DAG2.3 当前的 First Moment 仍以几何体积近似质量。现行三套生产建筑各自使用单一材料，因此同一建筑内密度是共同系数，不会改变相对质量比例或重心；DAG3-A 的只读发现与现有支撑解相容。

DAG3-B/C 不得在这个前提未解决时混合“弱材质支撑 + 强材质主体”并宣称同一权威质量模型。正式路线只能二选一：

1. DAG3-B 首版保持整栋材料一致，仅靠内部几何改写形成弱点；或
2. 先把 DAG2.3 累计荷载升级为逐节点材料密度感知，再允许材料改写，并重新跑完整态支撑求解。

当前选择第一条作为最小闭环；任何混合材料生产启用都必须先完成第二条。

### 4.2 DAG3-B 静态门槛

DAG3-B 不复用 Legacy B 的 `MinWeakCollapseRatio=0.02`。源 Frontier 及第二遍结果都必须继续满足本节 20%–75% 主体质量、15%–60% 高度、25% 高度跨度和至少两个 Macro 的 DAG3 门槛。

本轮 Pattern 事务使用 `FABTSM73DAGFailurePatternSettings` 的改写参数，并把现有 `FABTSM73DifficultySettings` 静态门槛复制到 Macro Intent：

| 参数 | 初始值 | 作用 |
| --- | ---: | --- |
| `MaxRewriteAttemptCount` | 48 | “Frontier × Pattern”事务硬上限 |
| `ContactAreaSafetyFactor` | 1.10 | 第二遍接口所需接触面积的安全系数 |
| `DifficultySettings.MinInitialSupportMarginCM` | 2 | 完整态累计 COM 位于完整支撑 Hull 内的最小裕量 |
| `DifficultySettings.MinTipMarginCM` | 8 | 复制为 Intent 的 `MinPostFailureTipMarginCM`；Dual/Seam 移除 `W` 后累计 COM 越出剩余 Hull 的最小裕量 |
| `DifficultySettings.MaxReseatRisk` | 0.35 | 仅 Dual/Seam 存在剩余 `P` 时使用的最大原位承接静态代理 |
| `OffsetSeamShiftRatio` | 0.18 | 沿确定性局部方向移动受影响 Macro 闭包的比例 |
| `MinOffsetSeamShiftCM` | 36 | Offset Seam 的最小非零错位 |

这些门槛来自 B2 可复用的 Contact Hull/COM 数学，但不复用 B2 的“单 Candidate + 单 Carrier + 顶部三节点闭包”合同：

- Single 移除后没有剩余支撑 Hull，其核心门槛是完整 Ground 断路，不以伪造的巨大 `TipMargin` 通过；它记录 `ReseatRisk` 作为诊断，但本阶段不以 `MaxReseatRisk` 拒绝，真实 Drop/Reimpact 留到 DAG-4；
- Dual 的强侧 `P` 是允许保留的 Ground 路径，必须使用整个受影响主体的累计质量验证剩余 Hull；
- Offset Seam 还必须记录真实错位和静态滑移/承接代理。

无论 Pattern，第二遍都必须满足：

- `NodeId`、Mapping 和逐柱 Role 一一对应，数组长度与排序稳定；
- `DAGMissingRequiredContactCount=0`、`DAGUnexpectedBypassCount=0`；
- 零初始穿透、所有主体节点完整态有 Ground 路径；
- 实际砖数不超过 `GenerationSettings.MaxBrickCount`；
- 所有砖的 `Material` 与 `OriginalMaterial` 保持不变，`WeakPoints` 为空；
- 失败不污染第一遍数据，不产生部分 Material/Role/Mapping 改写。

`MinFreeTipAngleDegrees`、`MinFreeSlideDistanceCM`、真实摩擦滑移和二次撞击尚未在 DAG3-B 冻结为动态门槛；它们属于 DAG3-C 扫掠空间和 DAG-4 Chaos 反事实，不能从本节静态数值推导为“实际一定会倒”。

## 5. 确定性选择与身份

候选输入先按以下稳定身份排序并去重：

```text
CandidateKind
+ sorted CandidateNodeIds
+ sorted ProtectedRootNodeIds
```

通过门槛的候选排在拒绝候选之前；随后依次按：

1. 与 `TargetMainBodyAffectedMassRatio` 的距离；
2. cut set 节点数；
3. Candidate Kind；
4. `FrontierHash`；

选择唯一候选。

`FrontierHash` 绑定候选类型、候选/保护节点、受影响主体节点与宏节点、量化后的高度/质量/跨度以及旁路数。浮点量在 Hash 前量化到 `1e-4`，数组全部排序；合法 CRC 恰为零时映射到非零身份，以保留零作为“未选择”哨兵。因此倒序输入 `Bricks`、`SupportEdges`、Ground、Mapping 和 Mapping 内 Column 都不应改变结果，非主体 Helper 的加入也不应改变同一主体前沿的身份。

`FrontierHash` 与 DAG2.3 `DAGTopologyHash` 是两个不同身份：

- `DAGTopologyHash`：抽象递归拓扑；
- `FrontierHash`：编译后物理接触图上的某个失效前沿。

DAG3-A 禁止覆盖或复用 `DAGTopologyHash`。

DAG3-B 再把身份拆成：

- `SourceFrontierHash`：第一遍选中物理 Frontier 的只读来源证明；
- `RealizedPatternHash`：第二遍的 Resolved Pattern、来源 Hash、Macro/Plate 接口、`W/P` 与受影响主体节点、量化方向和静态 Margin/Reseat/Offset 指标的独立改写身份。

第二遍编译后物理 `NodeId` 可以变化，因此 `SourceFrontierHash` 不能被重算为“看似同一个”结果，也不能被 `RealizedPatternHash` 覆盖。`DAGTopologyHash` 继续只标识同一份抽象 Macro DAG；三种 Pattern 即使共用它，也必须拥有不同且非零的 `RealizedPatternHash`。

## 6. 失败关闭与诊断

分析已启用时，以下情况必须显式拒绝当前建筑候选：

| Reject | 含义 |
| --- | --- |
| `DAG3SettingsInvalid` | 门槛区间或预算非法 |
| `DAG3StructureMissing` | 没有砖块或 Ground |
| `DAG3InvalidOrDuplicateNode` | 节点身份无效/重复 |
| `DAG3NodeGeometryInvalid` | 节点位置/尺寸非有限或尺寸非正 |
| `DAG3SupportEdgeInvalid` | 支撑边端点无效或自环 |
| `DAG3DuplicateSupportEdge` | 物理接触 DAG 含重复边 |
| `DAG3SupportGraphCycle` | 编译后支撑图不是 DAG |
| `DAG3GroundNodeInvalid` | Ground 指向不存在节点 |
| `DAG3BaselineNoGroundPath` | 未移除任何候选时已有节点不连 Ground |
| `DAG3BoundsInvalid` | 建筑高度不可用 |
| `DAG3SupportMappingInvalid` | Mapping 端点、Column 或两段真实接触不完整 |
| `DAG3CandidateBudgetExceeded` | 候选超过硬预算 |
| `DAG3MaterialProfileMissing` | 主体节点没有真实材料 Profile |
| `DAG3MaterialProfileInvalid` | 材料密度非有限或非正 |
| `DAG3MainBodyMassMissing` | 没有可计量主体 |
| `DAG3MainBodyBoundsInvalid` | 主体 Bounds 或主体高度不可用 |
| `DAG3NoAcceptedFailureFrontier` | 有候选，但没有候选通过全部门槛 |

单个候选还会记录高度、完整切断、旁路、主体质量、主体跨度和宏节点数的稳定 RejectReason。候选拒绝不应被吃掉；后续候选搜索需要用这些原因判断是换 Seed、换结构改写还是修正包络。

当 `bEnableAnalysis=false` 时，分析器返回成功但结果保持 `bEnabled=false`、`bAccepted=false`、零候选和零 Hash。这是生产兼容旁路，不代表已经接受了一个弱点。

## 7. 生产接线与日志

`FABTSM7TaskGraphBuildingProfile` 和 `AABTSM73StableBuildingActor` 的 DAG3 链路分别持有 `FABTSM73DAGFailureFrontierSettings` 与 `FABTSM73DAGFailurePatternSettings`，并由 `FABTSM73DAGBuildingPipeline::BuildWithFailurePattern` 执行显式纯数据候选路径。生产默认值必须保持：

```text
bEnableAnalysis = false
bEnableGeometryRewrite = false
```

因此旧 Blueprint CDO、新 C++ 默认 Profile 和现行三栋 TaskGraph 建筑既不运行阻断性 DAG3-A 分析，也不执行 DAG3-B 第二遍重求解。只有显式 opt-in 的测试/未来候选 Profile 才能运行改写；一旦 opt-in，找不到 Frontier、Macro Intent 不合法、第二遍求解失败或 Pattern 反事实失败都必须拒绝，不能返回第一遍普通结构冒充成功。

`FABTSM7TaskGraphDAG23ProfileResolver` 不把退役的 B/B2 开关解释为 DAG3-B：默认 Profile 与 Legacy migration 都保持几何改写关闭；若输入本来就是显式 authored DAG Profile，则保留其独立的 DAG3-B settings。这个保留能力只是 opt-in 通道，不改变当前生产默认，也不代表 DAG3-C 弱材质/攻击玩法已接线。

生成日志增加：

```text
DAG3Enabled=<0|1>
DAG3Candidates=<N>
DAG3Accepted=<accepted candidate count>
DAG3Hash=<selected frontier hash>
DAG3BEnabled=<0|1>
DAG3BApplied=<0|1>
DAG3BPattern=<resolved enum>
DAG3BHash=<realized pattern hash>
```

当前生产基线必须继续看到：

```text
DAG3Enabled=0 DAG3Candidates=0 DAG3Accepted=0 DAG3Hash=0
DAG3BEnabled=0 DAG3BApplied=0 DAG3BPattern=0 DAG3BHash=0
WeaknessPlanner=0 WeakPoints=0
Bricks=13/17/13
```

禁用 DAG3-B 时，`FABTSM73DAGFailurePatternResult` 必须保持未启用、未应用、零 Hash 和空 `WeakNodeIds/RemainingSupportNodeIds`。启用候选时，结果分别记录 Resolved Pattern、`SourceFrontierHash`、`RealizedPatternHash`、稳定 Macro 接口、`WeakNodeIds`、`RemainingSupportNodeIds`、预期运动/方向及静态 Hull 指标。

DAG3-B 仍不写 `WeakPoints`。成功候选另以 `[ABTS][M7.3-DAG3B][Pattern]` 记录 `SourceHash/RealizedHash`、Macro 接口、`Weak/Pivot`、完整态/失效态 Margin、`Reseat`、`Offset` 与尝试次数；拒绝候选记录 `[ABTS][M7.3-DAG3B][Reject]`。这些 Pattern/Weak Node 字段只表示“纯数据改写意图与静态反事实已建立”，不是 DAG3-C 已完成材料/攻击路由，也不是实际弱点击毁已通过。

## 8. 自动化验收

DAG3-A 新增六项纯数据自动化：

```text
ABTS.M73DAG3.DirectedCutSemantics
ABTS.M73DAG3.FrontierEnumerationDeterminism
ABTS.M73DAG3.MainBodyMassAndSpanGates
ABTS.M73DAG3.FrontierBypassAudit
ABTS.M73DAG3.BudgetAndDisabledRegression
ABTS.M73DAG3.ProductionPresetDiscovery
```

覆盖：

- 单链 node dominator；
- 多 Ground 双路径中的单侧旁路，以及由两条合法 Mapping 聚合出的完整联合 cut set；
- 合成接口先通过真实 `DAGContactGraphBuilder` 审计；
- 输入数组与 Mapping 内 Column 倒序后的候选、度量与 Hash 完全一致；
- 辅助 Payload 不能改变主体质量、主体 Bounds、归一化跨度或 Frontier 身份；
- 高度、质量、跨度、宏节点数的等号/越界与明确 RejectReason；
- 未移除候选时已有孤立节点会失败关闭；
- 候选预算失败关闭；
- 生产 Profile 默认关闭；
- Workshop/Target/Furnace 仍为 13/17/13 模块与原 DAG2.3 Hash；
- 三个生产 Preset 在只读 opt-in 下都能发现无旁路、跨多个宏节点的静态前沿。

DAG3-B 本轮新增的实际 Automation Path 为：

```text
ABTS.M73DAG3.Rewrite.PatternGeometryMatrix
ABTS.M73DAG3.Rewrite.RealizedContactAndIntactStability
ABTS.M73DAG3.Rewrite.CounterfactualSemantics
ABTS.M73DAG3.Rewrite.DeterminismAndIdentity
ABTS.M73DAG3.Rewrite.BudgetDisabledAndAtomicFailure
```

| Automation Path 后缀 | 本轮实际断言 |
| --- | --- |
| `PatternGeometryMatrix` | 三个显式 Pattern 均从同一生产 Fixture 构建；几何与 `RealizedPatternHash` 两两不同；Macro 拓扑 Hash 不变；柱数、Role、运动、`W/P`、非零方向和 Offset 字段符合各 Pattern |
| `RealizedContactAndIntactStability` | 第二遍 `Missing/Unexpected=0`，Mapping Role 一一对应且两段真实接触存在，完整态 Stability 通过；节点身份和单一原材质保持，`WeakPoints/StructuralWeaknessIntents/FailureProbeResults` 均为空 |
| `CounterfactualSemantics` | 对三者移除完整 `F` 都断路；Single 只移除 `W` 即断路且没有 Tip Margin；Dual/Seam 只移除 `W` 时 `P` 仍连 Ground，并满足 Tip Margin 与 `MaxReseatRisk`；Offset 另满足最小非零偏移 |
| `DeterminismAndIdentity` | 三种显式 Pattern 及 `Auto` 同一输入重复构建的几何、Pattern Result 与第二遍 Frontier Analysis 完全相同；`Auto` 必须解析到一个已认证的显式身份；NodeId 连续稳定；`DAGTopologyHash` 保持基线值，`SourceFrontierHash` 与 `RealizedPatternHash` 均非零且互不相等 |
| `BudgetDisabledAndAtomicFailure` | 默认 Profile 同时关闭 A/B；禁用时物理数据精确 no-op；缺少已接受 Frontier、零尝试预算、单次预算耗尽均返回精确 Reject，保留基线几何且不提交 Pattern Hash |

本节只记录已经落地的测试 API 名称和实际覆盖面。DAG3-A 的倒序输入测试继续证明来源 Frontier 身份不依赖输入数组顺序；本轮 `DeterminismAndIdentity` 只声明它实际覆盖的完整重复构建，不把未执行的 DAG3-B 数组变换写成已验收。

2026-07-29 的 DAG3-B 代码切片证据：

- `AngryBirdsToSpaceEditor Win64 Development -ForceUnity -DisableAdaptiveUnity`：加入最终 `Auto` 成功路径断言后复编 14.72 秒成功；
- fresh `UnrealEditor-Cmd -NullRHI`，`ABTS.M73DAG3.Rewrite`：5/5 Success；
- 独立 Rewrite 日志：`Saved/Logs/DAG3B-Rewrite-20260729-124909-FreshAutomation.log`；
- fresh `ABTS.M73DAG3.`：11/11 Success，最终日志 `Saved/Logs/DAG3B-FinalAuto-DAG3-20260729-130243.log`；
- fresh 旧 DAG2.3 `ABTS.M73DAG.`：9/9 Success，日志 `Saved/Logs/DAG3B-Final-DAG23-20260729-125634.log`；
- fresh `ABTS.M7.TaskGraphDAG23ProfileRouting`：1/1 Success，日志 `Saved/Logs/DAG3B-Final-M7Routing-20260729-125704.log`；
- fresh `ABTS.Contracts.WorldGeneration`：2/2 Success，日志 `Saved/Logs/DAG3B-Final-WorldContracts-20260729-125734.log`；
- fresh `ABTS.M73B2.`：2/2 Success，日志 `Saved/Logs/DAG3B-Final-M73B2-20260729-125803.log`；
- fresh M10 `-NullRHI -ExecCmds="t.MaxFPS 60" -Seconds=45` smoke：三栋保持 13/17/13、原 `DAGTopologyHash`、DAG3-A/B 全部关闭、零穿透，Idle 3/3 Accepted，最终 `WorldReady=1 BuildingAccepted/Rejected/Expected/Registered=3/0/3/3`，且无 Error/Blocked 或 DAG3-B Pattern/Reject 诊断；日志 `Saved/Logs/DAG3B-Final-M10-Smoke-20260729-125852.log`。

2026-07-29 的 DAG3-A 基线历史证据（不覆盖本轮 DAG3-B 新代码）：

- `AngryBirdsToSpaceEditor Win64 Development -ForceUnity -DisableAdaptiveUnity`：成功；
- fresh `UnrealEditor-Cmd -NullRHI`，`ABTS.M73DAG3`：6/6 Success；
- fresh `UnrealEditor-Cmd -NullRHI`，旧 DAG2.3 稳定过滤器 `ABTS.M73DAG.`：9/9 Success；
- fresh `UnrealEditor-Cmd -NullRHI`，`ABTS.M7.TaskGraphDAG23ProfileRouting`：1/1 Success；
- fresh `UnrealEditor-Cmd -NullRHI`，`ABTS.M7`：当前二进制快照 20/20 Success；
- fresh `UnrealEditor-Cmd -NullRHI`，`ABTS.Contracts.WorldGeneration`：2/2 Success。
- fresh `/Game/Maps/L_ABTS_M10 -game -NullRHI -ExecCmds="t.MaxFPS 60"`：三栋均 `DAG3Enabled/Candidates/Accepted/Hash=0/0/0/0`、13/17/13 模块及原 DAG Hash，逐栋 `IdleValidation Accepted=1`，最终 `WorldReady=1 BuildingAccepted/Rejected/Expected/Registered=3/0/3/3`。

`Automation RunTests ABTS.M7` 使用前缀匹配；20/20 包含 M73/DAG3 测试，只是本次二进制快照，未来新增 `ABTS.M7*` 后数量会变化。正式回归以带尾点的 `ABTS.M73DAG.`、`ABTS.M73DAG3.` 和精确 M7 路由 Path 为稳定门槛，不能长期硬编码 20。

DAG3-A 的历史证据只覆盖只读发现；DAG3-B 的 5 项 Rewrite 证据与随后取得的完整 DAG3、旧 DAG2.3、M7 路由、世界契约、B2 和 M10 smoke 日志共同构成本轮代码/纯数据验收。它们仍不包含可见 Pattern 读形、实际攻击、自由 Drop/Tip/Slide、二次撞击或 settled Contact 重建，不能据此宣称玩法已经上线。

## 9. DAG3-B 验收与后续正式门槛

### 9.1 DAG3-B 代码切片

DAG3-B 自身至少要证明：

1. 三种 Pattern 都经过“第一遍发现 → Macro Intent → 第二遍重求解 → 重新编译/接触审计”，而不是编译后移动 `Bricks`；
2. `InternalSingleSupport` 的 `W` 单点移除造成完整 Ground 断路；
3. `InternalAsymmetricDualSupport` 的 `W` 单点移除后 `P` 仍连 Ground，但主体累计 COM 越出剩余 Hull；
4. `InternalOffsetSeam` 具有真实非零 Macro 闭包偏移、弱键/偏心承托分区，并通过剩余 Pivot、Tip Margin 与 `ReseatRisk` 静态代理约束原位承接；
5. 三者的几何、逐柱 Role/数量、`W/P`、运动和 `RealizedPatternHash` 形成不同 Pattern 身份；预期方向必须非零且确定性复现，但不要求三个 Pattern 人为使用三条不同轴向；
6. 第二遍真实接触 `Missing=0`、`Unexpected=0`，完整态零穿透、静态稳定；
7. 相同输入的几何、Pattern Result、第二遍 Frontier Analysis 与连续 NodeId 完全复现；`DAGTopologyHash` 保持基线值，`SourceFrontierHash` 与 `RealizedPatternHash` 均非零且互不相等；
8. disabled 精确 no-op；缺少已接受 Frontier、零尝试预算和一次尝试预算耗尽都精确拒绝，失败输出保留第一遍基线几何且不提交 Pattern Hash；
9. 整栋材料未改变、`WeakPoints=0`，生产 Profile 仍关闭；
10. 旧 `ABTS.M73DAG.`、M7 Profile Routing 和世界契约回归继续通过。

纯数据自动化可以完成这些断言；若本轮未执行可见 Pattern 预览，则阶段状态必须写成“代码/自动化完成，待可见几何验收”，不能宣称玩家已经能读懂三种结构。

### 9.2 DAG3-C、DAG-4 与生产启用

后续不得仅把 `bEnableAnalysis` 或 `bEnableGeometryRewrite` 打开便宣布 DAG-3 完成。生产候选至少还要满足：

1. 三种内部改写都在编译后真实接触 DAG 中保留选定 Frontier；
2. 完整态继续通过零穿透、静态稳定与 IdleValidation；
3. 移除前沿后达到主体 Tip/Slide、自由位移和 `ReseatRisk` 门槛；
4. settled transforms 重建 Contact DAG 后仍为 `BypassSupportEdgeCount=0`；
5. 弱点攻击效果显著高于多个普通攻击点；
6. 木/石/铁/玻璃使用真实材料 Profile；
7. 候选生成有固定 Seed、时间与刚体预算；
8. TaskGraph 生产仍禁止 Legacy fallback；
9. 可见 PIE 中弱点位置、坍塌方向和留白能被玩家理解；
10. 在 WFC/Encounter 接入前，不能用三套相同外壳冒充六栋视觉不重复建筑。

DAG3-C 的“显式 opt-in 生产候选”不等于切换 TaskGraph 默认值。只有 DAG-4 settled Contact、弱点/多个普通点 Chaos 对照，以及串行正式可见 PIE 都通过后，才能另行评审生产默认启用。

## 10. 实现文件

- `Source/ABTSRuntime/Public/Building/ABTSM73DAGFailureFrontierTypes.h`
- `Source/ABTSRuntime/Public/Building/ABTSM73DAGTypes.h`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAGFailureFrontierAnalyzer.h/.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAGFailurePatternRewriter.h/.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAGBuildingPipeline.h/.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAGLayoutSolver.h/.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAGLoadSupportSolver.h/.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAGModuleCompiler.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAG3AutomationTests.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73StructureData.h`
- `Source/ABTSRuntime/Public/Building/ABTSM73BuildingTypes.h`
- `Source/ABTSRuntime/Public/Building/ABTSM73StableBuildingActor.h`
- `Source/ABTSRuntime/Private/Building/ABTSM73StableBuildingActor.cpp`
- `Source/ABTSRuntime/Public/Game/ABTSM7GameMode.h`
- `Source/ABTSRuntime/Private/Game/ABTSM7GameMode.cpp`
