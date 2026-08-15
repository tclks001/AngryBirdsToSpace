# M7.3 Map Freeze V3 Chaos 研究 checkpoint

> 日期：2026-08-16
> 分支：`feature/m7-buildings`
> 合并基线：`master@f11917c` 已通过 merge commit `98b9141` 进入本工作树
> 状态：V3 同物理身份夹具已完成代码迁移并编译；生产合同在 E1 前 fail closed，未进入 Chaos，E2～E6 未运行

## 1. V3 研究顺序与身份

- `E1`～`E6` 只表示建筑复杂度，研究仍严格按 `E1 → E2 → E3 → E4 → E5 → E6` 串行。
- Map Freeze V3 的遭遇槽顺序是 `E2 / E3 / E4 / E5 / E1 / E6`。夹具按
  `ManifestEntryId` 查找复杂度，禁止用 E1～E6 枚举下标索引合同数组。
- E1 必须解析为 `ComplexityTier=0 / EncounterSlot=4 / Surface=Satellite`，支撑中心来自
  `V3Envelope.SupportCenterWorldCM`；E2～E6 必须解析为 `PrimaryPlanet`。
- 夹具先调用生产 `TryExportBuildingGenerationContract`，再调用
  `FABTSM73JuryDemoFixedSixRegistration::BuildStaticPlan`。只有完整 V3 合同、布局、逐站点
  Placement、Descriptor、StaticGeometry、Production、Device 与 Gravity 身份一致时才生成刚体。
- 动态体集合来自生产静态计划的全部 Bricks、Devices 与 Caps；E1 的两根 Crystal 座梁、唯一
  Crystal cap 和设备不得从 Chaos 夹具省略。
- 刚体通过 `AABTSM7BuildingMaterialSystem` 的生产 Spawn/`BeginLaunchPhysics` 路径配置，复用
  `FABTSM7ChaosBodyProfile`、当前 `FABTSM7ChaosWorldProfile`、60 Hz 外步和径向恒加速度模型。

## 2. 首次 E1 取证

### R1：旧低精度 M3 世界不再合法

旧夹具把 `SurfaceSubdivision` 强制设为 `1`、`InstancesPerCell` 设为 `0`。Map Freeze V3 的生产
清场门在该非生产表面上报告：

```text
ProductionClearance Passed=0
Failure=ChaosSurfaceMiss:1:Ray=0:Sample=16
MaxChaosResidualCM=467.15
Authority=M3RuntimeSurface
```

这两个测试专用覆盖已经移除。使用生产默认世界后，清场门变为 `Passed=1`、
`MaxChaosResidualCM=1.43`。因此 R1 不是建筑稳定性失败，而是旧夹具世界身份失败。

日志：`Saved/Logs/M7-Chaos-MapFreezeV3-E1-20260816-R1.log`、
`Saved/Logs/M7-Chaos-MapFreezeV3-E1-20260816-R2.log`。

### R2：Map Freeze V3 与当前 M7 Building Freeze V3 身份不一致

R2 在生产 M3 表面重建和清场通过后，仍在合同导出前 fail closed：

```text
MapFreezeV3 Ready=0 Reason=FrozenCatalogMismatch Failure=FrozenV3Catalog
JuryFixedSix Exported=0 Failure=MapFreezeV3 Authority=FailClosed
```

精确差异：

| 身份 | master 发布的 Map Freeze V3 | 当前 M7 E1 外载版本 |
| --- | ---: | ---: |
| Catalog Hash | `8960617043786800590` | `2428875568906321995` |
| 静态模块总数 | `5746` | `5748` |
| Registration Result Hash | `4923733484321510334` | `8220727697792096023` |

当前 M7 身份来自 `b1e6fc6 M7: certify E1 crystal seat external loads`，包含两根真实 Crystal 座梁；
`f11917c Publish Map Freeze V3 production contract` 的提交时间更晚，但共享 DTO/Map Freeze 仍冻结
较早的 `8a4892d` M7 Catalog。不能在 M7 夹具里绕过这项差异，否则结果不再是生产物理身份。

## 3. 交叉门结果

- UE 5.8 Development Editor 全链接：成功。
- 当前 M7 `ABTS.M73DAG.BuildingFreezeV3`：`6/6` 成功；Catalog
  `2428875568906321995`，六 Actor 共 `5748` 模块，E1 为 complexity 0 / encounter 4。
  日志：`Saved/Logs/M7-BuildingFreezeV3-PostMapMerge-20260816.log`。
- 共同 `ABTS.Contracts.WorldGeneration`：`1/3` 成功；`V3DTO`、`M3Adapter` 因上述 Catalog
  差异失败，`Validation` 成功。日志：
  `Saved/Logs/M7-Contracts-WorldGeneration-PostMapMerge-20260816.log`。
- E1 没有进入动态观测阶段，因此没有 quiet、漂移、沉降或旋转结论；E2～E6 未运行。
- 未启动 GUI、可见 PIE 或 D3D12；未修改共享合同、M3 生产代码、Physics 配置或地图。

## 4. 恢复条件

由原始集成工作树明确选择并发布一个原子一致基线：

1. 接受当前 E1 外载版本：更新共享 V3 Catalog/逐项 Production、Static、Descriptor、Bounds，
   由 M3 重新冻结六个 Placement/Layout，并更新共同 Registration/StaticModule 门；或
2. 明确回退 M7 到 master 已接受的 `8a4892d` 建筑身份，并说明 E1 双座梁/外载提交不再属于 V3。

当前功能工作树不得自行修改共享 DTO、M3 Map Freeze、M6 共同门或 `master`。新的一致 `master`
进入后，先要求 `ABTS.Contracts.WorldGeneration` 全绿，再从 E1 重新开始，任何 E1 问题未解决前
不运行 E2。
