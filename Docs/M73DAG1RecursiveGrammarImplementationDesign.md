# M7.3-DAG-1：递归支撑图纯数据语法实现设计

> 状态：C++ 已落地并完成 fresh-process 自动化验收；其空间编译接入已由 [M7.3-DAG-2](M73DAG2SpatialLayoutAndModuleCompilationDesign.md) 实现。本文记录 DAG-1 的边界、数据契约与拓扑验证。
>
> 父级：[M7.3-DAG 递归承载图总体设计](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)。直接下游：[DAG-2 空间布局与模块编译](M73DAG2SpatialLayoutAndModuleCompilationDesign.md)。导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M7.3 原总体算法](M73ProceduralModularBuildingGenerationResearch.md) · [M7.3-A Legacy 稳定建筑](M73AStableBlockBuildingImplementationDesign.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md)

## 1. 阶段目标

M7.3-DAG-1 只建立可确定性复现的结构语法内核，不生成砖块、不访问 World，也不启动 Chaos。

本阶段实现：

- 独塔、拱门、双塔桥三种基准表达式；
- `Series` 串联与 `Parallel` 并联；
- `AllRequired / AnySufficient / KOfN / Independent` 并联策略数据语义；
- Atom 的递归串联或并联二分；
- 路径级确定性 Seed；
- 展开步数、抽象节点、预估砖块和弱点预留预算；
- 推导树编译为抽象 Macro 支撑 DAG；
- Ground/Top-load frontier；
- 规范表达式和拓扑 Hash；
- DAG、Ground 路径、预算及树结构验证；
- 自动化测试和唯一成功日志。

本阶段不实现：

- Scope、Envelope、Support Port 和局部坐标求解；
- `FABTSM73BrickNode` 编译；
- 材料、质量、Contact Hull 或弱点；
- 平面/球面 Ground Adapter；
- Actor、编辑器可视化、HISM 或 Chaos；
- 替换 Legacy `AABTSM73StableBuildingActor`。

这条边界用于先证明图语法正确，避免把拓扑错误、砖块穿透和物理失稳同时混在一次调试中。

## 2. 文件与职责

| 文件 | 职责 |
| --- | --- |
| `Public/Building/ABTSM73DAGTypes.h` | DAG-1 的设置、枚举、推导树、Macro 节点、支撑边、轨迹与结果 |
| `Private/Building/ABTSM73DAGGrammarExpander.*` | 基准表达式、递归展开、路径 Seed、Macro DAG 编译、规范表达式 |
| `Private/Building/ABTSM73DAGValidator.*` | 树一致性、DAG 无环、Ground 可达、frontier、预算和规范数据验证 |
| `Private/Building/ABTSM73DAG1AutomationTests.cpp` | 三组纯数据自动化测试 |

所有实现都在 `ABTSRuntime`，没有新增 Build.cs 依赖。

## 3. 方向与表达式约定

### 3.1 用户表达式

表达式按从上到下阅读：

```text
A-B
```

表示 B 从下方支撑 A。

```text
A+B
```

表示 A、B 是同层并行分支。

### 3.2 编译后的支撑边

为兼容现有 M7.3，支撑边统一为：

```text
SupportNodeId -> LoadNodeId
Ground -> Top
```

因此 `A-B` 编译为 `B -> A`。

### 3.3 基准表达式

| Preset | 表达式 | 规范形式 |
| --- | --- | --- |
| `SingleTower` | `A-B-C-D` | `S(N,N,N,N)` |
| `Arch` | `A-(B+C)` | `S(N,P0(N,N))` |
| `TwinTowerBridge` | `(A+B)-C-(D+E)` | `S(P0(N,N),N,P0(N,N))` |

`P0` 中的数字是 `EABTSM73DAGParallelPolicy` 的稳定整数值。它进入规范 Hash，防止拓扑相同但承载语义不同的并联结构被误认为同一个设计。

## 4. 推导树与 Macro DAG

### 4.1 推导树

每个表达式节点保存：

```text
NodeId
ParentNodeId
Operator
ParallelPolicy
AppliedRule
ChildNodeIds
DerivationPath
ExpansionDepth
```

`DerivationPath` 采用稳定路径：

```text
R
R/0
R/0/1
```

数组 NodeId 只用于本次结果内索引；路径用于随机、调试与跨预算前缀稳定性。

### 4.2 递归规则

终端 Atom 可被替换为：

```text
SerialSplit(N)   -> Series(N/0, N/1)
ParallelSplit(N) -> Parallel(N/0, N/1)
```

父节点保留原 `NodeId` 和路径，并变为操作符；两个新 Atom 获得子路径和 `Depth+1`。

### 4.3 Macro DAG 编译

每个终端 Atom 编译为一个 `FABTSM73DAGMacroNode`。它只是待空间求解的结构区域，不是一块砖或 Actor。

递归编译返回两个 frontier：

```text
TopNodeIds
BottomNodeIds
```

- Atom：Top 和 Bottom 都是自身；
- Parallel：合并所有子项的 Top 和 Bottom，不增加支撑边；
- Series：把每个下方子项的 Top 连接到上方子项的 Bottom。

这使 `A-(B+C)` 编译为两条边：

```text
B -> A
C -> A
```

## 5. 确定性随机

不使用一个会被调用顺序影响的共享 `FRandomStream`。每个可展开 Atom 独立计算：

```text
PathSeed = Hash(
    BuildingSeed,
    GeneratorVersion,
    Preset,
    DerivationPath,
    Salt)
```

两个 Salt 分别负责：

- 候选展开优先级；
- 串联/并联规则选择。

结果是：扩大 `ExpansionStepBudget` 时，先前已有的展开路径与规则仍保持不变，新预算只追加后续展开。未来规则表语义改变时必须提高 `GeneratorVersion`。

## 6. 预算与终止

### 6.1 预算参数

```text
MinExpansionDepth
MaxExpansionDepth
ExpansionStepBudget
MaxAbstractNodeCount
MaxEstimatedBrickCount
ReservedWeaknessBrickCount
```

DAG-1 暂用保守估算：

```text
EstimatedBrickCount = TerminalMacroNodeCount
                    + ReservedWeaknessBrickCount
```

DAG-2 引入真实终止原语后，再把每个 Macro 的 `MinBrickCost` 加入估算。

### 6.2 应用规则前检查

每次二分会：

- 将 1 个 Atom 变为操作符；
- 新增 2 个表达式节点；
- 终端 Macro 预估净增加 1。

展开前检查下一步是否超过抽象节点或预估砖块预算。若超过：

- 停止递归；
- `bBudgetTerminated=true`；
- 保留当前完整、合法结果；
- 不像 Legacy `BrickBudgetExceeded` 那样让整栋结果消失。

### 6.3 最小深度

低于 `MinExpansionDepth` 的 Atom 优先展开。如果步数或预算不足以满足请求，生成明确拒绝：

```text
MinimumExpansionDepthUnsatisfied
```

不会静默把未达到下限的结构当作成功。

## 7. 规范表达式与 Hash

规范化规则：

- Atom 统一写为 `N`，不把路径和 NodeId 纳入拓扑；
- 相邻同类 Series 扁平化，保留从上到下的顺序；
- 相同策略的嵌套 Parallel 扁平化；
- Parallel 子项按规范字符串排序，消除左右交换造成的假差异；
- Parallel Policy 进入字符串；
- 使用 `FCrc::StrCrc32` 得到 `CanonicalTopologyHash`。

Hash 只用于快速索引、日志和 Novelty 初筛。需要严格比较时必须同时比较 `CanonicalExpression`，不能把 32 位 Hash 当作无碰撞身份证明。

## 8. Validator

`FABTSM73DAGValidator` 检查：

- Root 存在且没有 Parent；
- NodeId 连续且 Parent/Child 双向一致；
- Atom 没有 Child，Series/Parallel 至少两个 Child；
- 推导树无环且无不可达节点；
- Atom 数等于 Macro 数；
- 预估砖块包含弱点预留且不超限；
- Support edge 的端点合法、无自环、无重复；
- Kahn 拓扑排序覆盖全部 Macro；
- 每个 Macro 都能从 Ground frontier 到达；
- Top-load frontier 没有更上层出边；
- 规范表达式存在。

DAG-1 验证的是拓扑正确性，不表示几何可实现或物理稳定。

## 9. 自动化测试

### `ABTS.M73DAG.ExpressionSemantics`

- 三种基准表达式得到指定规范形式；
- Macro、支撑边、Ground、Top 数量正确；
- 三种拓扑 Hash 不同。

### `ABTS.M73DAG.RecursiveExpansionDeterminism`

- 相同 Seed、Version 和设置结果完全一致；
- 推导节点、轨迹、支撑边和 Hash 一致；
- 扩大步数预算保留旧轨迹前缀；
- 成功时输出：

```text
[ABTS][M7.3-DAG-1][Accepted]
```

### `ABTS.M73DAG.BudgetTermination`

- 预算只能容纳一次二分时，优雅输出五个 Macro；
- 弱点预留包含在预估成本中；
- 不误标为步数终止；
- 无法满足最小深度时明确拒绝。

## 10. 编辑器操作

本阶段没有 Actor、Blueprint、DataAsset 或 Details 参数，不需要在编辑器中创建、放置或修改资产。

运行自动化测试：

1. 编译并启动 Editor；
2. 打开 `Tools -> Test Automation`；
3. 搜索 `ABTS.M73DAG`；
4. 运行以下三项：
   - `ExpressionSemantics`
   - `RecursiveExpansionDeterminism`
   - `BudgetTermination`
5. 三项均应为 Success；
6. Output Log 中应出现 `[ABTS][M7.3-DAG-1][Accepted]`。

也可以通过命令行 fresh Editor 运行，命令见验收记录或本次交付说明。

## 11. 验收标准

- Legacy `ABTS.M73A/B/B2` 代码路径未被修改；
- 三种基准拓扑均能纯数据生成；
- 同 Seed/Version 规范表达式和 Hash 可复现；
- Series 与 Parallel 的支撑方向正确；
- 所有 Macro 有 Ground 路径；
- 预算耗尽保留合法结果；
- 最小深度无法满足时拒绝，不假通过；
- 自动化测试在 fresh process 中全部通过；
- 不产生 `.uasset`、地图或 Blueprint 修改。

### 11.1 本次实际验证

构建：

```text
Build.bat AngryBirdsToSpaceEditor Win64 Development
Result: Succeeded
```

fresh-process DAG-1 自动化：

```text
Automation RunTests ABTS.M73DAG
Found 3 automation tests
BudgetTermination                  Success
ExpressionSemantics                Success
RecursiveExpansionDeterminism      Success
TEST COMPLETE. EXIT CODE: 0
```

固定验收样本：

```text
[ABTS][M7.3-DAG-1][Accepted]
Seed=731011 Version=1 Preset=1
Expr=25 Macro=13 Edges=13 Steps=10
Hash=3042813435
```

Legacy 回归：

```text
ABTS.M73A.DefaultStructuresAreStaticallyStable  Success
ABTS.M73B.WeakPointPlanner                      Success
ABTS.M73B2.ParameterMatrix                      Success
ABTS.M73B2.StructuralWeaknessFailure            Success
TEST COMPLETE. EXIT CODE: 0
```

对应日志：

- `Saved/Logs/CodexM73DAG1Automation_20260725_1.log`
- `Saved/Logs/CodexM73DAG1LegacyRegression_20260725_1.log`

## 12. 排错

| 现象 | 根因 | 处理 |
| --- | --- | --- |
| 同 Seed 改大步数后旧规则改变 | 随机依赖共享流或步数预算 | 随机只依赖路径、Seed、Version、Preset 和 Salt |
| `A+B` 没有任何支撑边 | 这是同层 Parallel 的预期；真正支撑来自包含它的 Series 接口 | 检查外层 Carrier/Series，不要给并排分支凭空加边 |
| Arch 的 Ground 有两个节点 | `A-(B+C)` 的 B、C 都是 Ground frontier | 属于正确编译结果 |
| 预算耗尽却 Reject | 初始 Preset 本身已超过预算，或请求的最小深度无法满足 | 增大预算/步数，或降低 `MinExpansionDepth` |
| 拓扑 Hash 相同但调试路径不同 | 规范化有意忽略 NodeId、路径和 Parallel 左右顺序 | 严格复现看推导树；Novelty 看规范表达式和 Hash |
| DAG-1 通过但未来砖块站不住 | DAG-1 不包含几何、接触和质量 | 由 DAG-2 的 Layout/Contact/Static 验证负责 |
| 新增源文件后 Unity Build 报两个匿名命名空间 `NodeMass` 重定义 | `PostFailureValidator.cpp` 和 `WeakPointAnalysis.cpp` 原有同名内部函数曾处于不同 Unity TU；源文件分桶变化后被合并 | 分别改名为 `FailureProbeNodeMass` 与 `AnalysisNodeMass`，计算和调用语义不变；不要依赖 Unity 分桶隔离同名内部实现 |

## 13. 后续接口

M7.3-DAG-2 已读取：

```text
ExpressionNodes
MacroNodes
SupportEdges
GroundNodeIds
TopLoadNodeIds
CanonicalExpression
CanonicalTopologyHash
```

并以 Scope、Plate 和竖直 Column Pair 编译为 Legacy 可消费的 `FABTSM73StructureData`；详见 [M73DAG2SpatialLayoutAndModuleCompilationDesign.md](M73DAG2SpatialLayoutAndModuleCompilationDesign.md)。DAG-1 本身仍不直接依赖 `AABTSM73StableBuildingActor`。
