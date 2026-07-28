# M7.3-DAG-2：空间布局与模块编译设计

> 状态：C++ 已实现；经 DAG2.1/2.2/2.3 扩展后已接管 TaskGraph 普通建筑生产路径。候选筛选与载荷求解共享唯一支撑几何，求解器保存最终柱中心，编译器只消费该结果；Tripod 接触质心偏置已修复。Editor 编译、自动化与最终二进制三次 fresh D3D12 实时 60 FPS 通过，待补可见 PIE 外观/击打验收。
>
> 父级：[M7.3-DAG 递归承载图总体设计](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)。前置：[DAG-1 递归语法](M73DAG1RecursiveGrammarImplementationDesign.md)。子阶段：[DAG-2.1 支撑模式](M73DAG21SupportPatternsDesign.md) · [DAG-2.2 自适应几何](M73DAG22AdaptiveGeometryDesign.md) · [DAG-2.3 累计荷载与联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md)。导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M7.3-A Legacy 对照](M73AStableBlockBuildingImplementationDesign.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md)

## 1. 目标与边界

本阶段把 DAG-1 的 `FABTSM73DAGMacroNode` 编译为现有 M7.3 可放置、可预览、可生成 Chaos 模块的 `FABTSM73StructureData`。

编译链为：

```text
Derivation Tree
-> Blueprint Support DAG
-> Scope Layout + Selected Sparse Supports
-> Plate / Support Pattern BrickNodes
-> Realized Contact DAG
-> 既有 Ground Adapter / Preview / Runtime Chaos
```

本阶段仅使用局部轴对齐的水平 Plate 与竖直 Column；不实现任意角度板、梁/悬臂、内部弱点、攻击 rollout、炸药桶或活塞。`M7.3-A/B/B2` 只保留历史对照，不再拥有 TaskGraph Profile。DAG-2 路径跳过旧 B/B2 顶冠规划；正式内部弱点只能由 DAG-3 从主体 Failure Frontier 推导。

## 2. Scope 布局

每个递归表达式先拥有一个根 Scope：`TargetWidthCM × TargetDepthCM × TargetHeightCM`。递归下放时：

- `Series` 只表达先后承载关系，所有子项保留同一 XY Scope；最终结构层级与 Z 高度由已选 Support DAG 的最长路径统一求解。`SeriesGapCM` 现仅为旧序列化兼容保留字段，不参与运行时布局；
- `Parallel` 默认沿局部 X 切分；`bAlternateParallelAxes` 为真时，按深度交替 X/Y；
- 终端 Atom 获得不重叠 Scope，并编译为一块 Plate；
- Plate 尺寸为 Scope 的 XY 投影乘 `PlateFootprintRatio`，厚度为 `PlateThicknessCM`，且任一平面尺度不得小于 `MinPlateExtentCM`。

因此 Macro Node 是“可施工体积”，不是一块固定砖。Scope 先解决空间冲突，Plate/Column 后解决真实接触，不能从 DAG 的节点数量直接推断砖块数量。

## 3. 从完整候选边到稀疏支撑

DAG-1 的一层 `A+B` 到相邻层 `C+D` 可以产生多条合法候选边，但不能把每条候选都降低为柱，否则会出现完整二分柱网、旁路过多且玩法不可读。

每条候选 `Support Plate -> Load Plate` 必须同时满足：

1. 两 Plate 的 XY 投影有交集；
2. 交集足以放下所选二/三/四柱模式、`ColumnClearanceCM` 与 `ColumnWidthCM`；
3. 两 Plate 之间的净高度不少于 `MinColumnHeightCM`；
4. 同一 Load 最多选 `PreferredLogicalSupportsPerLoad` 条，硬上限为 `MaxLogicalSupportsPerLoad`。

候选按可用交集与局部代价稳定排序，再选取有限条。无可行候选时拒绝 `DAGNoFeasibleSupport`：这意味着抽象 DAG 合法，但现阶段“竖直柱”原语无法实现；未来应由显式 Beam/Span 节点处理，而不是偷偷添加斜柱或旁路。

## 4. 模块编译与真实接触审计

- 每个 Macro 降低为一个 `Plate` BrickNode；
- 每条已选逻辑支撑的两/三/四柱模式、轻量化默认参数与凸包校验见 [M7.3-DAG-2.1](M73DAG21SupportPatternsDesign.md)；
- Layout 候选筛选与载荷求解器必须调用同一个 `FABTSM73DAGSupportGeometry`；求解器把已经通过凸包、净空和接触面积检查的中心写入 `SelectedSupport.RealizedColumnCenters`，模块编译器只消费这组权威中心，缺失即拒绝。禁止在编译阶段再次推导点位，使“静态验收的凸包”与“Chaos 实际收到的接触柱”保持同源；
- 所有节点写回现有 `FABTSM73StructureData.Bricks`，因此 Editor HISM Preview、M7 Runtime Module、Ground Adapter、M7.1 平面测试台、球面地基脚与 Chaos 生命周期均复用；
- 编译后从最终轴对齐碰撞盒反建 `Realized Contact DAG`，并审计每条已选逻辑支撑必须实现为 `SupportPlate -> Column -> LoadPlate`。

审计若缺少必要接触，拒绝 `DAGMissingRequiredContact`；若生成非拓扑预期的承载旁路，拒绝 `DAGUnexpectedBypass`。这是 DAG-1 的 authored intent 与 Chaos 实际碰撞之间的防漂移边界。

## 5. 代码职责

| 文件 | 职责 |
| --- | --- |
| `ABTSM73DAGLayoutSolver.*` | Scope 递归切分、Plate 位置/尺寸、候选几何筛选与稀疏边选择 |
| `ABTSM73DAGSupportGeometry.*` | Layout/载荷求解共享的二/三/四柱中心与方柱 AABB 净空检查；保证 Tripod 等面积接触质心落在承载区域中心 |
| `ABTSM73DAGModuleCompiler.*` | 将 Macro Plate 和已选支撑降低为 BrickNode |
| `ABTSM73DAGContactGraphBuilder.*` | 从最终碰撞盒反建真实接触图并审计 |
| `ABTSM73DAGBuildingPipeline.*` | 串联 DAG-1、Layout 与 Compiler |
| `ABTSM73DAG2AutomationTests.cpp` | Scope/编译、稀疏支撑和确定性自动化 |
| `AABTSM73StableBuildingActor` | 历史手工 Actor 可读取兼容枚举；TaskGraph Resolver 始终传入 DAG，Actor 复用预览与运行时装配 |
| `FABTSM7TaskGraphDAG23ProfileResolver` | 三类生产 Profile、旧 Blueprint CDO 升级和禁止 Legacy fallback |

## 6. 编辑器操作

1. 打开 M7.1 的 `PlanarPhysicsTestMap`，放置或选中 `ABTSM73StableBuildingActor`。
2. 在 Details 设置 `GroundMode = PlanarTestStage`；若需要自由编辑器放置，保持 `bSnapPlanarAnchorToTestStage = false`。
3. 在 `ABTS|M7.3-A|Generation` 将 `GenerationSettings.GenerationAlgorithm` 设为 `RecursiveSupportDAG`，并将 `bGenerateStructuralWeakness` 关闭。
4. 在 `ABTS|M7.3-DAG-1|Generation` 选择 `Preset`（`SingleTower`、`Arch` 或 `TwinTowerBridge`），设置 Seed；相同参数必须得到相同构造。
5. 在 `ABTS|M7.3-DAG-2|Layout` 先使用默认值：

```text
TargetWidthCM=460, TargetDepthCM=300, TargetHeightCM=760
PlateThicknessCM=40, ColumnWidthCM=56
SupportPattern=ThreeColumnTripod
PreferredLogicalSupportsPerLoad=2, MaxLogicalSupportsPerLoad=2
```

6. 点击 `Rebuild Preview` 或移动 Actor 触发构建。然后 PIE，确认静态预览、运行时模块和地基共同出现。

TaskGraph 场景不需要手工执行第 3–5 步；M7 Resolver 自动提供已批准的 DAG Profile。Blueprint 中把 Profile 改回 Legacy 也只会触发兼容升级，不会恢复旧生产链。

## 7. 验收

- 三个 Preset 均能生成 Plate + Column，不出现重叠、空预览或初始弹飞；
- 相同 Seed、参数得到相同 Brick 数、Transform、稀疏支撑数和 Topology Hash；
- Tripod 在长轴为 X/Y 的两种输入下均满足三柱等面积接触质心与区域中心误差 `<0.01cm`，且任意两根方柱至少沿一个轴保留 `ColumnClearanceCM`；
- 极限区域必须稳定区分不可行/可行边界；最终接触面积反推使柱宽变大时，按配置从 Tripod/FourColumn 降为 TwoColumn、必要时 SingleColumn，并在每次降级后重算全组柱宽，不得把本可行的窄接口误报为 `DAGNoJointSupportHull`；
- `PreferredLogicalSupportsPerLoad=1` 时，Arch 的两条候选不会退化为完整二分柱网；
- 平面测试台可拖动、击打，球面模式仍通过原有 Ground Adapter；
- 日志含 `Algorithm=1`、`DAGMacro`、`DAGSparse`、`DAGHash`；
- TaskGraph 中不存在 Legacy 路由或 DAG Reject 后的 Legacy fallback；
- 所有 M7.3 Actor 的 Idle terminal 必须先于 M6 `WorldReady=1`，避免启动预热提前冻结。

本次自动化已通过：

```text
ABTS.M73DAG.BudgetTermination                  Success
ABTS.M73DAG.ExpressionSemantics                Success
ABTS.M73DAG.RecursiveExpansionDeterminism      Success
ABTS.M73DAG.ScopeLayoutAndModuleCompilation    Success
ABTS.M73DAG.SparseSupportAudit                 Success
[ABTS][M7.3-DAG-2][Accepted]
Preset=1 Seed=731022 Macro=3 Sparse=1 Bricks=5 PhysicalEdges=4 Hash=1987612131
```

## 8. 排错

| 现象/日志 | 根因 | 处理 |
| --- | --- | --- |
| `DAGParallelScopeTooNarrow` | 并联 Scope 在 Gap、最小 Plate 尺寸后不足 | 增大目标宽/深，降低 `ParallelGapCM` 或减少递归并联深度 |
| `DAGNoFeasibleSupport` | 上下 Plate 无可放置所选支撑模式的投影交集 | 改 Preset/Seed、增大 Plate Scope、使用较小支撑模式，或留待后续 Beam/Span 原语 |
| `DAGColumnTooShort` | 上下板净高度低于最小柱高 | 增大 `TargetHeightCM`、减少 Series 层数或降低 `MinColumnHeightCM` |
| `DAGMissingRequiredContact` | 编译后的柱未真正接触任一板 | 检查 Plate 厚度、Column Clearance、缩放和碰撞网格 Pivot；不要只改 authored edge |
| `DAGUnexpectedBypass` | 最终盒体产生了未授权横向/斜向接触 | 增大 Scope/Series Gap，缩小 Plate Footprint 或调整候选选择 |
| 提高 Budget 后实际 Plate/Column 数高于预期 | 当前 DAG-1 预算估算以 Macro 为主，尚未形成获批准的编译后物理砖数门槛 | 生产首版保持 Budget=0；启用 Budget=1 前补 post-compile hard guard 与 TaskGraph Seed sweep |

## 9. DAG-3 接口

DAG-3 必须以 `SelectedSupports` 和重建后的 `SupportEdges` 为输入，在主体中下部寻找 Failure Frontier；不能回到 B2 的“顶端加一组弱柱”。候选弱点应同时满足：移除后的受影响质量/高度跨度、无低成本旁路、攻击可达性和 Chaos 反事实坍塌。Beam/Span、任意朝向 Plate、装置和更复杂材料组也应在该真实接触图契约下扩展。
