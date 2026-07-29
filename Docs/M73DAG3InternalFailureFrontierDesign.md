# M7.3-DAG-3：内部 Failure Frontier

> 状态：DAG3-A 纯数据“失效前沿发现”已落地并完成自动化基线；生产 Profile 默认关闭分析，因此现有 DAG2.3 的 13/17/13 模块、`DAGTopologyHash`、材质和 `WeakPoints=0` 合同均未改变。DAG3-B 三种结构改写、生产启用以及 DAG-4 settled/Chaos 对照仍未完成，不能据此宣称内部弱点玩法已经上线。
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
| DAG3-B | 围绕选中前沿实现 `InternalSingleSupport`、`InternalAsymmetricDualSupport`、`InternalOffsetSeam` 三种几何改写 | 未实现 |
| DAG3-C | 加入扫掠空间、攻击可达性、材质与 Profile 路由，并以显式 opt-in 接入生产候选 | 未实现 |
| DAG-4 | Idle 后重建 settled Contact DAG，并执行弱点/非弱点 Chaos 对照 | 未实现 |

DAG3-A 明确不做以下事情：

- 不增删或移动任何砖块；
- 不替换材质，不创建 `WeakPointRecord`；
- 不改变 DAG2.3 `DAGTopologyHash`；
- 不启用生产 Profile；
- 不以静态图切断代替完整态稳定、失效态位移和实际击打验收；
- 不承担 WFC 包络、六栋去重建筑或 Encounter 难度消费。

## 2. 输入与唯一数据链

分析器只读取 DAG2.3 已编译并通过真实接触审计的 `FABTSM73StructureData`：

```text
DAG-1 Grammar
-> DAG2.1 / DAG2.2 / DAG2.3 Layout + ModuleCompiler
-> DAGContactGraphBuilder
-> GroundAdapter footprint
-> DAG3-A FailureFrontierAnalyzer（只读）
-> StabilityValidator
```

关键输入是：

- `Bricks`：稳定 `NodeId`、`MacroNodeId`、局部 AABB、材质；
- `SupportEdges`：由实际碰撞几何重建的 `LowerNodeId -> UpperNodeId`；
- `GroundNodeIds`：全部落地节点；
- `DAGPhysicalSupportMappings`：一组联合支撑柱与其 Load Plate 的权威对应；
- 运行时 Material Profile：用于按真实密度计算主体质量，而不是只数砖块。

`DAGFailureFrontierAnalysis` 存回 `FABTSM73StructureData`，但它是派生诊断数据，不拥有几何。后续改写必须生成一份新的候选结构并重新走 DAG2.3/接触审计，不能直接把分析结果当作已兑现的物理弱点。

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

DAG3-A 故意保持 Pattern 无关。完整 directed cut 只能证明“全部 Ground 路径被切断”，不能据此把双柱集合命名为 `InternalAsymmetricDualSupport`：后者应保留强侧 Ground 路径，并由剩余支撑 Hull/COM 越界产生倾覆；`InternalOffsetSeam` 也必须由接缝、偏置和滑移几何定义。三种 Pattern 的分类与 `RequestedPattern` 过滤均留到 DAG3-B，DAG3-A 输出不得被下游当作已认证的改写类型。

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

`FABTSM7TaskGraphBuildingProfile` 和 `AABTSM73StableBuildingActor` 已接入 `DAGFailureFrontierSettings`。默认值为：

```text
bEnableAnalysis = false
```

因此旧 Blueprint CDO、新 C++ 默认 Profile 和现行三栋 TaskGraph 建筑都不会运行阻断性 DAG3-A 分析。只有显式 opt-in 的测试/未来候选 Profile 才会在分析失败时拒绝生成。

生成日志增加：

```text
DAG3Enabled=<0|1>
DAG3Candidates=<N>
DAG3Accepted=<accepted candidate count>
DAG3Hash=<selected frontier hash>
```

当前生产基线必须继续看到：

```text
DAG3Enabled=0 DAG3Candidates=0 DAG3Accepted=0 DAG3Hash=0
WeaknessPlanner=0 WeakPoints=0
Bricks=13/17/13
```

启用候选分析时，Editor 可通过 `FABTSM73GenerationSummary` 查看候选数、通过数、主体质量比例、跨度、旁路数和独立 Frontier Hash。DAG3-A 不输出 Pattern。

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

2026-07-29 的本工作树证据：

- `AngryBirdsToSpaceEditor Win64 Development -ForceUnity -DisableAdaptiveUnity`：成功；
- fresh `UnrealEditor-Cmd -NullRHI`，`ABTS.M73DAG3`：6/6 Success；
- fresh `UnrealEditor-Cmd -NullRHI`，旧 DAG2.3 稳定过滤器 `ABTS.M73DAG.`：9/9 Success；
- fresh `UnrealEditor-Cmd -NullRHI`，`ABTS.M7.TaskGraphDAG23ProfileRouting`：1/1 Success；
- fresh `UnrealEditor-Cmd -NullRHI`，`ABTS.M7`：当前二进制快照 20/20 Success；
- fresh `UnrealEditor-Cmd -NullRHI`，`ABTS.Contracts.WorldGeneration`：2/2 Success。
- fresh `/Game/Maps/L_ABTS_M10 -game -NullRHI -ExecCmds="t.MaxFPS 60"`：三栋均 `DAG3Enabled/Candidates/Accepted/Hash=0/0/0/0`、13/17/13 模块及原 DAG Hash，逐栋 `IdleValidation Accepted=1`，最终 `WorldReady=1 BuildingAccepted/Rejected/Expected/Registered=3/0/3/3`。

`Automation RunTests ABTS.M7` 使用前缀匹配；20/20 包含 M73/DAG3 测试，只是本次二进制快照，未来新增 `ABTS.M7*` 后数量会变化。正式回归以带尾点的 `ABTS.M73DAG.`、`ABTS.M73DAG3.` 和精确 M7 路由 Path 为稳定门槛，不能长期硬编码 20。

DAG3-A 不改变几何、材质或 Chaos，因此本切片不要求以可见 PIE 证明“弱点击毁”。生产启用前仍必须补做 DAG3-B/C 自动化、DAG-4 settled 接触/攻击对照，以及正式可见 PIE。

## 9. DAG3-B/C 正式验收门槛

后续不得仅把 `bEnableAnalysis` 打开便宣布 DAG-3 完成。生产候选至少还要满足：

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

## 10. 实现文件

- `Source/ABTSRuntime/Public/Building/ABTSM73DAGFailureFrontierTypes.h`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAGFailureFrontierAnalyzer.h/.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAG3AutomationTests.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73StructureData.h`
- `Source/ABTSRuntime/Public/Building/ABTSM73BuildingTypes.h`
- `Source/ABTSRuntime/Public/Building/ABTSM73StableBuildingActor.h`
- `Source/ABTSRuntime/Private/Building/ABTSM73StableBuildingActor.cpp`
- `Source/ABTSRuntime/Public/Game/ABTSM7GameMode.h`
- `Source/ABTSRuntime/Private/Game/ABTSM7GameMode.cpp`
