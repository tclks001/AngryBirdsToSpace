# M7.3-DAG2.3：累计荷载与联合支撑求解

> 状态：DAG2.3 已成为球面 TaskGraph 普通建筑的生产求解器；Editor 编译、DAG/M7 自动化、固定世界基线及最终二进制三次 fresh D3D12 实时 60 FPS 通过。Legacy 仅保留历史代码/序列化诊断，不参与生产回退或阻断性验收。
>
> 父级：[M7.3-DAG-2 空间布局与模块编译](M73DAG2SpatialLayoutAndModuleCompilationDesign.md)。生产集成：[M7 TaskGraph 球面建筑](M7TaskGraphSphericalBuildingIntegrationDesign.md)。平面验证：[M7.1 测试台](M71PlanarPhysicsTestStageDesign.md)。前置：[M7.3-DAG2.2 自适应楼板与支撑几何](M73DAG22AdaptiveGeometryDesign.md)。后续研究：[建筑语义 WFC 与 DAG 拟合](M73WFCBuildingEnvelopeAndDAGFittingResearch.md)。

## 目标

将 DAG-2 的物理构件生成从“每条 Logical Support 独立生成柱组”改为“每块 Load Plate 按其累计荷载选择联合柱组”。

本阶段解决 Parallel 细分后左右候选支撑均存在、但旧稀疏筛选仅保留单侧支撑，从而触发 `COMOutsideSupportHull` 的问题。

Macro DAG 仍负责建筑轮廓和允许承载关系；DAG2.3 不允许任意跨越空洞生成柱子。

## 荷载状态

每个 Macro Plate 初始化自身质量：

```text
Mass = PlateWidth × PlateDepth × PlateThickness
FirstMoment = Mass × PlateCenterXY
```

按由高到低的 DAG 层级处理。上方 Load 已确定的柱脚把荷载按柱脚数量均分，传递到其下方 Support Plate：

```text
LowerMass += ShareMass
LowerFirstMoment += ShareMass × ColumnFootXY
```

当前阶段是静态重力近似，不使用材质密度差和横向撞击；后续可将材料密度、炸药冲量和弹簧推力加入同一 First Moment / 力矩接口。

该 Plate 的累计合力点为：

```text
ResultantXY = FirstMoment / Mass
```

## 联合支撑组求解

对每个非地基 Load Plate：

1. 从 Support DAG 获取所有与其 XY 相交的候选承载区域。
2. 在不超过 `MaxLogicalSupportsPerLoad` 的范围内枚举候选组合。
3. 对组合内全部柱子共同反推柱宽，使总接触面积高于 `MinSupportContactAreaRatio`；额外保留 5% 数值安全裕度。
4. 以实际柱顶接触面的四角构建联合凸包。
5. 仅接受包含 `ResultantXY` 的组合；在可行组合中选择合力点到凸包边界裕度最大的组合。
6. 把选中的 Support Mapping 和柱脚荷载传递给下一层。

这意味着左右 Parallel 分支不必各自独立支撑整块桥面；二者可以共同提供一组覆盖桥面合力点的支撑。

### 支撑几何与荷载一致性合同

Layout 候选筛选与载荷求解器必须使用唯一的 `FABTSM73DAGSupportGeometry`。载荷求解器将已经通过凸包、接触面积和逐对 AABB 净空检查的柱脚中心写入 `SelectedSupport.RealizedColumnCenters`；模块编译器不得再次推导，而是原样生成这组中心，缺失即拒绝。这样静态凸包与 Chaos 实际接触体共享同一份权威数据。

`ThreeColumnTripod` 的三根等截面柱必须以承载区域中心为接触质心。长轴为 X 时使用：

```text
(-a, -b/2), (+a, -b/2), (0, +b)
```

长轴为 Y 时使用其转置：

```text
(-a/2, -b), (-a/2, +b), (+a, 0)
```

旧点位 `(-a,-b)、(+a,-b)、(0,+b)` 的等面积质心位于 `(0,-b/3)`，而累计荷载仍按三柱各 `1/3` 传播；对中心载荷，顶点柱实际需要承担约 50%，因此会被模型低估 50%。固定世界 Furnace 的 Node 6/9 正是连续两层的顶点柱，这会让铁结构在变步长 PIE 中持续产生偏转微振。

候选阶段只知道最小自适应柱宽，最终阶段还会根据整块 Load Plate 的接触面积反推更宽的柱。因此最终宽度下若 Tripod/FourColumn 不再满足净空，求解器按配置单调降为 TwoColumn、必要时 SingleColumn；每次柱数改变后必须重新计算整组共享柱宽，再重新检查所有接口。收敛上界按整个支持组的可降级次数计算，不能用固定三轮，否则多个接口先后失配时会误拒绝本来可行的支撑组。

Furnace 生产 Profile 另有 `MinSupportContactAreaRatio=0.06` 的安全底线，柱宽仍由 DAG2.3 按面积反推并保留 5% 数值余量。该底线不仅写入 C++ 默认 Profile，也在 `ResolveRuntimeProfile` 边界对旧 Blueprint CDO 中已序列化的显式 DAG Profile 取 `Max(旧值, 0.06)`；它不改变 Preset、Scope 或其他任务的可编辑参数。运行日志以 `DAGMinContact=0.060` 证明实际生效。

当前 First Moment 传播仍是中心载荷的等权近似。将来若引入偏心质量、爆炸冲量或装置力，必须改为满足 `Σw=1` 且 `Σ(w×Contact)=ResultantXY` 的非负重心权重；不得再次假设任意三点都能等权承载偏心合力。

## 跨层联合支撑脊柱

递归 Parallel 后，左右可行接口可能处于不同的预估 DAG 层级。若联合组合是覆盖合力点的唯一方案，DAG2.3 允许保留两侧接口；最终 Z 层级由选中的物理 Support DAG 重新求解。

这类柱子是显式的荷载承载脊柱，不是旧实现由错误层级推导出的偶发长柱。未来可将它们标记为更明显、可攻击的主承重构件。

若所有允许接口的联合凸包仍无法覆盖合力点，生成会明确报：

```text
DAGNoJointSupportHull
```

这代表当前 DAG 轮廓确实需要 Beam、悬臂或新的拓扑规则，不能通过把柱子放到无下方接触的位置伪造稳定性。

## TaskGraph 生产接入

现行链路为：

```text
M3 BuildingSpawnSite / Pad
-> M7 TaskGraph DAG Profile Resolver
-> DAG-1 Grammar
-> DAG2.1/2.2/2.3
-> FABTSM73StructureData
-> GroundAdapter
-> Runtime Module / IdleValidation
```

- M3 只提供任务、Anchor、施工台与确定性 Seed 上游，不调用 Legacy 建筑生成器；
- Workshop、TargetBuilding、FurnaceRuins 均强制 `RecursiveSupportDAG`；
- 旧 Blueprint CDO 中的 Legacy Profile 在 M7 边界升级为安全 DAG2.3 Profile，不能静默复活旧链；
- 当前 Target 使用 Budget=0 `TwinTowerBridge`，包含 Parallel 与联合支撑；Workshop/Furnace 使用 Budget=0 `SingleTower`；
- DAG 失败必须带确定性 Reject，禁止回退 Legacy；
- DAG2.3 不执行 B/B2 WeakPointPlanner。模块仍可击打/破坏，但正式内部弱点需由 DAG-3 实现。
- 任一生成或 Idle `Rejected` 必须撤销模块与 Foundation 碰撞，并阻断 `WorldReady`/发射；Pending/Running 必须持续等待，不能由可选的 M6 HISM 暖机开关绕过。
- M7 必须在生成前登记必需 Actor 数并在尝试完成后封口；合同激活后只检查注册集合，且每个必需 Actor 都必须显式 `Accepted`。MaterialSystem/Profile/Class/Actor 缺失、`Registered != Expected`、`Accepted != Expected` 或必需 Actor 为 `NotRequired` 均为 Reject，不能把零 Actor 或关闭 Idle 验证当成合法通过。

## 验收

已完成：

- 默认 TwinTowerBridge，`BuildingSeed=7301`、默认 Layout、`ExpansionStepBudget=1` 成功生成；
- 出现 Parallel 时，左右候选柱脚的联合凸包覆盖累计合力点；
- 静态验证不再以 `COMOutsideSupportHull` 拒绝该默认递归结果；
- `ABTS.M73DAG.StructuralRankAndPhysicalContinuity` 覆盖默认双塔一层展开；
- `ABTS.M73DAG` 9/9、`ABTS.M73` 13/13、`ABTS.M7` 14/14 自动化通过；
- `ABTS.M73DAG.SupportPatternsAndHullValidation` 同时覆盖宽向/深向 Tripod 的等面积接触质心、方柱 AABB 净空边界、最终宽度单接口降级和多接口级联降级；编译后柱质心仍与 Load Plate 中心重合；
- `ABTS.M7.TaskGraphDAG23ProfileRouting` 覆盖三套生产 Profile、旧 CDO 迁移、Furnace 显式旧 DAG 的 6% 运行时底线、13/17/13 模块 golden 拓扑与 Hash、LaunchSite 拒绝及 Ready/Waiting/Rejected 世界门禁；
- 当前固定世界两次冷启动的三栋建筑均为 `Algorithm=1`、`DAGMacro>0`、`DAGSparse>0`、`DAGHash!=0`，零穿透且逐栋 Idle 通过；
- 两次均为 `Expected=3 Registered=3 SetupRejected=0`；`WorldReady=1` 晚于三栋 Idle terminal，最终 `BuildingAccepted=3 BuildingRejected=0`，无 TaskGraph `Algorithm=0`。
- 最终二进制三次不带 `-benchmark` 的 fresh D3D12 实时 60 FPS 均为三栋 `TimedOut=0 Accepted=1`；Furnace 旋转 `0.08°/0.09°/0.08°`、`DAGMinContact=0.060`，门禁 `3/0/3/3`，且 `WorldReady=1`、无 ABTS Error/Blocked。

仍需可见 PIE：

- 冷启动证据不得只使用 `-benchmark`：该参数会启用固定时间步，不能代表编辑器 PIE 的变步长 Chaos；至少保留实时 30/60/120 FPS 与可见 PIE/hitch soak；
- 检查三套 Scope 的外观、施工台接缝和主承重柱可读性；
- 实际击打三种材料，验证激活、损伤、坍塌与 M8 回收；
- 确认 LaunchSite 只有 Space-only 槽，没有建筑。
