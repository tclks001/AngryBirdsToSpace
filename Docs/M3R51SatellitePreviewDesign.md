# M3R-5.1 卫星练习区与 E5 背面目标预览设计

> 状态：M3LocalAccepted（PreviewAuthority，IntegrationPending）
> 父文档：[M3 PCG 地图生成改进方案](M3PCGMapImprovementPlan.md#1481-m3r-51卫星练习区与-e5-背面目标预览)
> 表现父文档：[M3 TaskGraph 地形表现设计](M3TaskGraphTerrainPresentationDesign.md#25-m3r-51-卫星与-e5-预览叠层)
> 相关冻结参数：[M6/M9 弹弓与卫星标定模式](M6M9SlingshotSatelliteCalibrationDesign.md)

## 1. 目标与非目标

本阶段让 R-5 固定候选预览能够回答两个空间问题：M9 练习卫星相对 E5 强化弹弓槽场生成在哪里，以及 E5 建筑的代理目标是否确实位于卫星背面。它用于月度地图尚未发布唯一 Candidate 时的开发可视检查。

本阶段不生成真实 M9 卫星 Actor，不生成 M7 建筑 Actor，不执行 M6/M9 弹道认证，也不选择最终月度 Candidate。R-5.1 结果的 `bMonthlyWorldAccepted` 永远为 false；真实 Actor、碰撞、引力与成功岛仍由 R-4/R-6 后的 Integration 权威接入。

## 2. 输入权威和候选绑定

每个预览候选必须同时匹配：

- R-3 `SourceRouteCandidateId + SpatialCandidateHash`；
- R-3.1 `SourceSpatialCandidateHash + SlingshotFieldCandidateHash`；
- E5（`OrderIndex=4`）的 `EncounterId` 与同一 `EncounterRequired` 槽场；
- R-3 已签名的 `LaunchProfileVersion/Hash` 与 `SatellitePracticePresetVersion/Hash`；
- 当前 Planet 的连续地形表面查询结果。

构造器重新调用共享 `MakeFrozenLaunchProfileCatalogV0()` 和 `MakeFrozenSatellitePracticePresetV0()`，并要求其版本/Hash 与 R-3 `FrozenCalibrationBatch` 完全一致。它不读取校准蓝图，也不在 M3 复制一套可漂移的卫星参数。

R-5.1 为 E5 槽场选择一个确定性的“参考桩对”，只用于建立发射局部坐标和 F7 显示。选择按“中点最接近 E5 Slingshot Pocket、跨度较大、CellId 较小”稳定排序，并要求实际连续表面上的桩距不超过 R-3.1 `MaxCordLengthCM`。该参考对不是 AllowedPair：玩家仍可自由选择任何满足长度、遮挡和资源规则的普通桩连接。

## 3. 卫星和 E5 Transform

参考桩对表面位置的中点定义 Sling Center，径向方向定义 `LaunchUp`；指向 E5 原地目标 Anchor 的切向投影定义 `LaunchForward`，叉积定义 `LaunchRight`。冻结 Preset 的卫星弧距与方位角在该局部系内求出 `SatelliteAnchorDirection`。

卫星中心不是绝对世界坐标：

```text
SatelliteRadius = PrimaryRadius * SatelliteRadiusPrimaryRatio
SatelliteCenter = QuerySurface(SatelliteAnchorDirection)
                + SurfaceNormal * PrimaryRadius
                * SatelliteCenterClearancePrimaryRatio
```

E5 目标 Transform 由共享 `BuildSatelliteTargetWorldTransform()` 计算。它以卫星球面、冻结的 `BacksideAngleDeg/TargetLocalAzimuthDeg`、目标半尺寸和表面净空为输入，保证代理盒体落在卫星背面。Launch Profile Hash、Satellite Preset Hash、E5 槽场身份、参考桩对、全部世界 Transform 和背面判定都进入 Candidate Hash；结果 Hash 不进入 R-5 Biome Hash 或兼容世界 Hash。

## 4. F7 预览表现

在带精确候选启动参数的 PIE 中按 `F7`：

- 蓝色线框球：冻结参数生成的 M9 练习卫星；
- 洋红线框盒：卫星背面的 E5 目标代理；
- 黄色球与连线：本候选用于建立局部坐标的参考桩对；
- 绿色点：参考鸟袋位置；
- 青线：参考发射点到卫星中心的空间关系；
- 红色/橙色：既有目标范围和攻击走廊；E5 原主星 Target Footprint 在该叠层中隐藏，避免同时显示两个 E5 目标。

快捷叠层不创建组件或 Actor，不修改输入映射，也不进入确定性身份。无精确候选、候选 Join 失败、冻结参数不匹配或目标不在背面时均 fail closed。

推荐启动参数：

```text
-ABTSM3R5Preview -ABTSM3R5PreviewCandidate=4 -ABTSM3R5LogicRegions
```

## 5. 自动验收与当前证据

- 强制 Unity/禁用 Adaptive Unity 的 Development Editor 全链接通过；新实现的私有辅助符号全部显式命名空间限定，避免 Unity 合并单元歧义。
- fresh NullRHI `ABTS.M3.Monthly.SatellitePreview` 精确发现并通过 `2/2`：候选绑定/确定性核心与失败闭合。
- 展示 Seed `312503` 生成 3 个候选，`SourceSpatial=16A44AF72C58261E`、`SourceFields=E7EA3FB5463E395B`、`Result=5CEF57BB1A3C245F`。
- 每个候选均验证冻结 Preset 身份、非零卫星半径、E5 背面关系、目标盒球面贴合、Candidate/Result Hash 重算和重复重建 whole-struct 一致。

## 6. 集成交接清单

保持 IntegrationPending，直到 R-4/R-6 选出唯一 Candidate 后由集成工作树完成：

1. 把唯一候选的局部卫星布局转换为 M9 生产 Actor 配置；不得重新读取 `RetainedCandidates[0]` 或重新选择桩对。
2. E5 的 M7 Profile/Footprint/AttackFace 必须在卫星局部切平面实例化，并保持与 R-4 Witness 使用的目标代理一致。
3. 用真实 Reinforced M6 发射局部帧、真实 M9 引力查询和真实 M7 Bounds 重算 E5 Positive Witness；预览 Hash 不能充当弹道认证。
4. Visible PIE 验证卫星不在普通弹弓射程、强化弹弓可利用引力到达背面目标，并验证该卫星不进入 M11 四体积分数据。
5. 完成 Character/Visibility/M6 动态代理/M9 穿行碰撞回归后，才能将本阶段从 PreviewAuthority 晋升为 IntegrationAccepted。
