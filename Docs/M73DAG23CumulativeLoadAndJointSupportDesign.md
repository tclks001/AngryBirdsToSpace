# M7.3-DAG2.3：累计荷载与联合支撑求解

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

## 跨层联合支撑脊柱

递归 Parallel 后，左右可行接口可能处于不同的预估 DAG 层级。若联合组合是覆盖合力点的唯一方案，DAG2.3 允许保留两侧接口；最终 Z 层级由选中的物理 Support DAG 重新求解。

这类柱子是显式的荷载承载脊柱，不是旧实现由错误层级推导出的偶发长柱。未来可将它们标记为更明显、可攻击的主承重构件。

若所有允许接口的联合凸包仍无法覆盖合力点，生成会明确报：

```text
DAGNoJointSupportHull
```

这代表当前 DAG 轮廓确实需要 Beam、悬臂或新的拓扑规则，不能通过把柱子放到无下方接触的位置伪造稳定性。

## 验收

- 默认 TwinTowerBridge，`BuildingSeed=7301`、默认 Layout、`ExpansionStepBudget=1` 成功生成。
- 出现 Parallel 时，左右候选柱脚的联合凸包覆盖累计合力点。
- 静态验证不再以 `COMOutsideSupportHull` 拒绝该默认递归结果。
- `ABTS.M73DAG.StructuralRankAndPhysicalContinuity` 覆盖默认双塔一层展开。
- 完整 `ABTS.M73DAG` 自动化通过。
