# M7.3-DAG-2.1：支撑模式与轻量化构件设计

> 状态：C++ 已实现，Editor 编译、DAG 自动化与 Legacy A/B/B2 回归均通过；等待 M7.1 和球面场景的手工视觉/物理验收。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [DAG-2 空间布局与模块编译](M73DAG2SpatialLayoutAndModuleCompilationDesign.md) · [DAG 总体调研](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) · [M7.3-B2 Legacy 对照](M73B2StructuralWeaknessAndFailureValidationDesign.md)

## 1. 目标与边界

DAG-2.1 将每条已选择的 Logical Support 从“固定双柱”扩展为可审计的支撑模式。目标是以二维支撑域而非粗柱截面积承载楼板，让建筑更轻盈，并把可拆的支撑模式保留为 DAG-3 弱点规划输入。

本阶段实现两、三、四柱，不实现“随机破坏某根柱”“弱点评分”“Beam/Span/斜撑”或装置联动。

```text
Selected Logical Support
-> Support Pattern
-> Physical Columns
-> Plate/Column/Plate Contacts
-> Contact Convex Hull Validation
-> Future Failure Frontier / Weakness
```

## 2. 支撑模式

| `SupportPattern` | 柱数 | 几何 | 用途与未来弱点 |
| --- | ---: | --- | --- |
| `TwoColumnLine` | 2 | 在可行区域较长轴两端排成线 | 窄桥、接缝、故意脆弱的单向支撑；不能承担通用宽楼板 |
| `ThreeColumnTripod` | 3 | 两根构成底边、第三根位于另一侧，形成三角形 | 默认普通楼板；拆掉任意关键柱都可能显著缩小支撑凸包 |
| `FourColumnFootprint` | 4 | 可行区域四角形成矩形 | 宽重载平台、强调稳固的承载层；未来可作为“非弱点强化” |

三柱不是在双柱直线上再加一根，而是必须跨两个局部轴展开。只有这样，上方楼板的质心才由三角形凸包承载。

## 3. 几何约束与真实校验

每根柱的中心先向可行接触区域内缩：

```text
Inset = ColumnWidthCM / 2 + ColumnClearanceCM
```

二柱要求一条轴能容纳两根，三柱和四柱要求 X、Y 两个方向都能容纳两根。若区域不足，DAG-2 拒绝该候选支撑，而不会让柱相交或悄悄退化成其他模式。

静态验证不再把所有接触拼成 AABB。它会收集每个 Plate–Column 接触矩形的四角，建立真实二维凸包，再验证上方节点的局部质心是否位于该凸包内。这样三角形、四角形和线支撑的差异会真正影响生成是否通过。

`FABTSM73DAGPhysicalSupportMapping` 保存 `SupportPattern` 与准确的 `ColumnNodeIds`。DAG-3 可据此识别：三脚架的单柱失效、四角平台的对角/双柱失效，以及两柱线支撑的切断弱点。

## 4. 默认轻量化参数

新 C++ 默认值：

```text
SupportPattern = ThreeColumnTripod
ColumnWidthCM = 56
PlateThicknessCM = 40
ColumnClearanceCM = 3
MinSupportContactAreaRatio = 0.04
bAllowNarrowSupportFallback = true
bAllowAdaptiveColumnWidth = true
MinAdaptiveColumnWidthCM = 24
```

这是一组首轮视觉起点，不是质量参数。对于已经存档的 Actor，原有 `DAGLayoutSettings` 会保留旧的 `88 / 58` 数值，必须在 Details 中手动改成新值。

## 5. 编辑器操作

1. 选中 `ABTSM73StableBuildingActor`，确认 `GenerationAlgorithm = RecursiveSupportDAG`。
2. 在 `ABTS | M7.3-DAG-2 | Layout` 设置：

```text
Support Pattern = ThreeColumnTripod
Column Width CM = 56
Plate Thickness CM = 40
```

3. 保持 `Column Clearance CM = 3`，先不要同时缩小 Target Width/Depth。
   `Min Support Contact Area Ratio = 0.04` 只作用于 DAG；Legacy 建筑仍使用原来的 `MinContactAreaRatio=0.12`。
   保持窄支撑回退与自适应柱宽开启。若一个楼板由左右多条窄入边共同承载，每条入边可降低为一个内部单柱接口；所有入边最终仍按真实接触凸包共同判稳。
4. 对 `TwinTowerBridge`，先使用：

```text
Max Expansion Depth = 0
Expansion Step Budget = 0
```

或一层纯纵向递归：`MaxExpansionDepth=1`、`SeriesRuleWeight=1`、`ParallelRuleWeight=0`。
5. 点击 `Rebuild Preview`，确认三根柱呈三角形，而非同线。之后再 PIE 验证静稳和发射碰撞。

重载平台可改为 `FourColumnFootprint`；窄桥或故意暴露的未来弱点可改为 `TwoColumnLine`。不要通过把普通平台直接降为双柱来替代弱点设计。

## 6. 验收与排错

- 三种 Pattern 都能编译、每条 `DAGPhysicalSupportMapping` 的柱数分别为 2/3/4；
- 同 Seed/参数的柱位置和 Contact DAG 可复现；
- 三柱模式的三个中心不共线，楼板质心位于真实接触凸包内；
- 柱宽 56cm、板厚 40cm 的默认三柱建筑在 M7.1 中无初始穿透、无可见倾覆；
- `ABTS.M73DAG.SupportPatternsAndHullValidation` 成功。

| 现象 | 根因 | 处理 |
| --- | --- | --- |
| `DAGNoFeasibleSupport` | 接触交集无法容纳所选三角/四角支撑 | 增大 Scope、减小柱宽或切换较小 Pattern；不要降低 Clearance 至 0 |
| Parallel 一开启便 `DAGNoFeasibleSupport` | 相邻并联层分支数不同，单条逻辑入边的投影较窄，但多条入边本应共同承载同一楼板 | 开启窄支撑与自适应宽度；窄入边可记录为内部 `SingleColumnInterface`，实际宽度不低于 `MinAdaptiveColumnWidthCM` |
| 嵌套 Series 报 `DAGSeriesScopeTooShort`，提高总高度后仍快速复发 | 同类 Series 曾在每层递归重复均分父 Scope，造成高度预算指数缩水 | 同类 Series/Parallel 在布局阶段关联扁平化，在一个父 Scope 内只分割一次 |
| `COMOutsideSupportHull` | 楼板质心落在实际接触凸包之外 | 改三柱/四柱，增加可行交集，或调整 Plate 尺寸；不要回退到 AABB 判定 |
| `ContactAreaTooSmall` | 细柱总接触面积低于 DAG 专用承压比例 | 默认保持 `0.04`；提高柱宽或柱数。支撑凸包通过不能替代最低承压面积，但无需沿用 Legacy 的 12% |
| 三柱仍看似一条线 | `TwoColumnLine` 被选中，或可行区域过窄而被拒绝 | 选择 `ThreeColumnTripod`；检查 X/Y Scope 是否同时足够 |
| 旧地图仍是粗柱厚板 | Actor 实例序列化保留了 DAG-2.0 旧数值 | 手动设置 `56 / 40` 并 Rebuild Preview |

## 7. 后续接口

DAG-3 弱点规划读取每条 Mapping 的 Pattern 和 `ColumnNodeIds`。候选攻击应以“移除哪根柱会让载荷 COM 离开凸包、且没有低成本旁路”为标准；不再只依据顶部额外弱柱或纯节点度数。

## 8. 本次验证记录

```text
Build AngryBirdsToSpaceEditor Win64 Development: Succeeded

ABTS.M73DAG.BudgetTermination                    Success
ABTS.M73DAG.ExpressionSemantics                  Success
ABTS.M73DAG.RecursiveExpansionDeterminism        Success
ABTS.M73DAG.ScopeLayoutAndModuleCompilation      Success
ABTS.M73DAG.SparseSupportAudit                   Success
ABTS.M73DAG.SupportPatternsAndHullValidation     Success

ABTS.M73A.DefaultStructuresAreStaticallyStable  Success
ABTS.M73B.WeakPointPlanner                       Success
ABTS.M73B2.ParameterMatrix                       Success
ABTS.M73B2.StructuralWeaknessFailure             Success
```
