# M7.3-DAG2.2 自适应楼板与支撑几何设计

> 父级：[M7.3-DAG-2 空间布局与模块编译](M73DAG2SpatialLayoutAndModuleCompilationDesign.md)。前置：[M7.3-DAG2.1 支撑模式](M73DAG21SupportPatternsDesign.md)。后续阶段：[M7.3-DAG2.3 累计荷载与联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md)。
>
> 状态：C++ 已实现并由 DAG2.3 接入球面 TaskGraph 生产链；现行 Profile、运行门禁与验收见 [M7 球面 TaskGraph 集成](M7TaskGraphSphericalBuildingIntegrationDesign.md)。

## 目标

解决递归拱门与双塔中参数可行区间互相冲突的问题：扩大 Layout 时不应频繁因 `ContactAreaTooSmall` 或 `COMOutsideSupportHull` 失败，缩小时也不应立即因 `DAGParallelScopeTooNarrow` 失败。

本阶段不修改 Macro DAG 拓扑，只在 DAG-2 从逻辑支撑降级为物理楼板、柱组时调整局部几何。

## 求解顺序

1. Parallel 仍按表达式分割 XY Scope。
2. 开启自适应后，分支使用 `MinAdaptivePlateExtentCM` 作为安全下限，`MinPlateExtentCM` 保留为正常美术目标。
3. 小于正常目标的楼板在 `MinAdaptivePlateThicknessCM` 与 `PlateThicknessCM` 间连续插值。
4. 每条候选支撑按 `sqrt(MinSupportContactAreaRatio × LoadPlateArea / ColumnCount)` 反推最低柱宽。
5. 当前柱组无法满足时，依次尝试四柱、三柱、二柱，再使用既有窄接口回退。
6. 实际模式和柱宽写入物理 Support Mapping，编译、验证和未来弱点分析共享同一结果。

## 编辑器参数

参数位于 `ABTSM73StableBuildingActor > DAG Layout Settings`。

| 参数 | 默认值 | 作用 |
|---|---:|---|
| `bEnableAdaptiveGeometry` | true | 开启 DAG2.2 |
| `MinAdaptivePlateExtentCM` | 42 cm | 深层 Parallel 的局部楼板最小边长 |
| `MinAdaptivePlateThicknessCM` | 24 cm | 窄楼板最小厚度 |
| `MaxAdaptiveColumnWidthCM` | 96 cm | 为满足接触面积允许增长到的最大柱宽 |
| `bAllowAdaptiveColumnCount` | true | 当前模式不足时允许使用更多承重柱 |

`MinPlateExtentCM`、`PlateThicknessCM`、`ColumnWidthCM` 仍是正常视觉目标，不再同时承担绝对失败阈值。

## 验收

- 相同 Seed 下提高拱门、双塔的 Expansion Step Budget，失败率明显降低。
- 窄分支楼板会变窄、变薄，但不得低于安全下限。
- 宽楼板柱体自动变粗，必要时增加柱数。
- 不出现接触面积不足、质心落在支撑凸包外、贯通多层柱或无柱楼板。
- `ABTS.M73DAG.AdaptivePlateAndColumnGeometry` 与完整 `ABTS.M73DAG` 自动化通过。

## 仍可拒绝的情况

自适应不是无限缩放。如果 Scope 小于 `MinAdaptivePlateExtentCM`，或满足接触面积所需柱宽超过 `MaxAdaptiveColumnWidthCM` 且四柱仍放不下，生成仍会拒绝。这表示拓扑在当前总体尺寸下确实没有可靠物理解，而不是参数死区。
