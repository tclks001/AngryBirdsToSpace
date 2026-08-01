# M3R-5.1 卫星练习区与 E5 背面目标预览设计

> 状态：M3LocalAccepted（PreviewAuthority，IntegrationPending）
> 父文档：[M3 PCG 地图生成改进方案](M3PCGMapImprovementPlan.md#1481-m3r-51卫星练习区与-e5-背面目标预览)
> 表现父文档：[M3 TaskGraph 地形表现设计](M3TaskGraphTerrainPresentationDesign.md#25-m3r-51-卫星与-e5-预览叠层)
> 相关冻结参数：[M6/M9 弹弓与卫星标定模式](M6M9SlingshotSatelliteCalibrationDesign.md)

## 1. 目标与非目标

本阶段让 R-5 固定候选预览能够回答两个空间问题：M9 练习卫星相对 E5 强化弹弓槽场生成在哪里，以及 E5 建筑的代理目标是否确实位于卫星背面。它用于月度地图尚未发布唯一 Candidate 时的开发可视检查。

本阶段的数据结果不生成真实 M9/M7 Actor，不执行 M6/M9 弹道认证，也不选择最终月度 Candidate；`bMonthlyWorldAccepted` 永远为 false。为支持冻结参数的手感复测，显式精确预览另设一个非生产诊断桥：它在会话中持久化当前 Candidate 身份，以参考槽的真实地表 Cell 生成强化桩和弦，再从物理弦袋帧解析对应的卫星/E5 布局；它替换兼容 TaskGraph 生成的旧 M9 实例，开启真实卫星与 E5 代理碰撞，并绑定 M6 PracticeTarget。该桥不进入普通启动路径，不构成 R-4 Witness 或 R-6 实体发布。

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

F7 快捷叠层本身不创建组件或 Actor，不修改输入映射，也不进入确定性身份。显式精确预览的运行时诊断桥激活后，叠层优先读取运行时快照，因此黄色桩位、绿色弦袋点、蓝色卫星和洋红 E5 盒与真实 Actor 一致；诊断桥未激活时才显示只读 Candidate 估计。无精确候选、候选 Join 失败、冻结参数不匹配或目标不在背面时均 fail closed。

推荐启动参数：

```text
-ABTSM3R5Preview -ABTSM3R5PreviewCandidate=4 -ABTSM3R5LogicRegions
```

运行中可直接切换：

```text
abts.Calibration.SatelliteGravity -1  // 使用冻结 Preset 默认值
abts.Calibration.SatelliteGravity 0   // 关闭卫星重力，保留碰撞与布局
abts.Calibration.SatelliteGravity 1   // 开启卫星重力
```

重力开关只改变真实 M9 Actor 的 `bGravityEnabled`，不改变会话布局快照 Hash。运行时快照持久化 `SourcePreviewResultHash`、`CandidateHash`、Launch/Preset 身份、两个强化桩 Cell 与精确地表点、实际 Pouch Transform、重新解析的卫星锚点 Cell、卫星/E5 Transform、`BaselineGravitySnapshotHash` 与组合 `RuntimeLayoutSnapshotHash`；M6 轨迹预演和实际飞行都继续调用共享 M9 引力查询，不在 M3 复制积分公式。

### 4.1 真实 Cell 落地规则

Candidate 中的桩对中点只适合无 Actor 的规划预览，不能作为曲面地形上的 Actor 根点。运行时必须按以下顺序解析：

1. 用 `ReferenceSlotACellId/ReferenceSlotBCellId` 读取两个 `LogicalCells[].UnitCenter`，分别调用当前 Planet 的 `QuerySurface`；
2. 每根强化桩的可视底面落在各自返回的 `WorldLocation`，桩轴使用该次查询的真实地表法线；不得把两个地表点的空间中点当作共同地面；
3. 用两根实际桩顶生成正式 M5.1 强化弦，弦的 `GetRestPouchTransform()` 成为本会话唯一发射局部帧；
4. 冻结 Preset 的卫星弧距从实际 Pouch 的 `Forward` 与 Pouch 相对主星中心的真实径向重新求锚点方向，再次查询真实主星地表 Cell；卫星中心和 E5 背面代理均从该解析结果生成；
5. 卫星弧距以 Pouch 相对主星中心的真实径向为基准，而不是把局部坡面法线误作主星径向；在冻结的 30° 弧环上确定性搜索方位修正；
6. 把卫星中心视线投影到实际 Pouch 的 `Forward/Right` 平面后，与 Pouch `Forward` 的夹角必须 `<=5°`；
7. 任一 Cell 无效、地表查询失败、桩底误差超过 `1 cm`、朝向误差超过 `5°`、卫星锚点失败或碰撞/M6 绑定失败时，整套诊断布局 fail closed。

运行时使用正式 `AABTSM51SlingshotStake/AABTSM51SlingshotCord`，不再依赖 M7 TestStage 的固定间距整套弹弓 Actor。这样既消除了曲面中点下沉，也保证后续 M6 读取的就是画面中的真实弦袋。

## 5. 自动验收与当前证据

- 强制 Unity/禁用 Adaptive Unity 的 Development Editor 全链接通过；新实现的私有辅助符号全部显式命名空间限定，避免 Unity 合并单元歧义。
- fresh NullRHI `ABTS.M3.Monthly.SatellitePreview` 精确发现并通过 `3/3`：候选绑定/确定性核心、失败闭合、真实 Cell 桩底、运行时快照/碰撞/M6 绑定/重力切换。
- 展示 Seed `312503` 生成 3 个候选，`SourceSpatial=16A44AF72C58261E`、`SourceFields=E7EA3FB5463E395B`、`Result=5CEF57BB1A3C245F`。
- 每个候选均验证冻结 Preset 身份、非零卫星半径、E5 背面关系、目标盒球面贴合、Candidate/Result Hash 重算和重复重建 whole-struct 一致。
- 2026-08-01 生产档位闭环后的展示 Seed 证据为：强化桩 `CellA=2646/ResolvedA=2646`、`CellB=2634/ResolvedB=2634`，两个桩底误差均为 `0.000 cm`；Candidate 与运行时弦袋、卫星中心偏差均为 `0.00 cm`。冻结 30° 弧环施加 `-9.200°` 的真实地形法线补偿后解析到锚点 Cell `3378`，M6 发射帧朝向误差为 `0.007°`。强制 Unity 全链接与 fresh NullRHI 专项 `3/3` 通过；F7 在弦袋处显示 `SAT FACING` 与 `SAT TRAJECTORY` 作为可见验收证据。此前 `2646/2647`、`-7.435°`、Cell `4218` 是候选端仍使用理想球面位置时的旧诊断值，不再作为当前验收基线。

### 5.1 生产 M6 档位接通后的卫星闭环修复（2026-08-01）

集成工作树提交 `ac185d7` 使普通玩法的 M6 正式读取冻结 Launch Profile；强化弹弓最大速度由旧兼容值 `2300 cm/s` 恢复为冻结值 `3300 cm/s`。M3R-5.1 在此基础上补齐了其余断链：

- `FPlanetMonthlySatellitePreviewSurface` 不再调用基类理想球面的 `GetSurfaceWorldLocation()`，而是使用 `GetSurfaceRadiusAtDirection()` 取得 TerrainVisualField 的真实半径；候选端与运行时 `QuerySurface()` 因而使用同一地表，展示 Seed 的 `CandidatePouchDelta` 与 `DeltaFromPreview` 均由数百厘米归零为 `0.00 cm`；
- 参考槽对按真实强化桩高度、两端弦锚和 `GetRestPouchTransform()` 的同构几何建立 M6 Sling Frame，不再以“道路目标切线 + 固定 190 cm”伪造发射帧；候选版本晋升为 `GeneratorVersion=4 / LayoutPolicyVersion=3`；
- 冻结 30° 卫星弧环只允许 `±15°` 的确定性地形法线补偿，并把补偿角、朝向误差、真实锚点和卫星中心纳入 Candidate Hash；运行时只重放该补偿，禁止重新进行 `±90°` 搜索后把卫星搬离候选；
- M3 必须从活跃生产 `AABTSM6SlingshotSystem` 回读 Launch Profile Catalog，生产 Hash 与 Candidate `LaunchProfileHash` 不相同即 fail closed；
- 运行时用生产强化档位、实际弦端点、实际 M6 相机投影平面、真实主星/卫星参数和 E5 OBB 重算成功集。M3 练习门要求至少 3 个相连样本、跨相邻瞄准点、存在 gravity-on hit 且对应 gravity-off miss `>=60 cm`、简易弹弓不可解、认证功率带外不可解。理想球冻结认证仍额外要求跨相邻功率刻度，其结果以 `FullFrozenCarrierPassed` 单独记录，不冒充真实地形练习门；
- F7 除 `SAT FACING` 外新增 `SAT TRAJECTORY PASS/FAIL DEP=<n> ISLAND=<n>`，直接显示引力依赖命中数与成功岛大小。

fresh NullRHI `ABTS.M3.Monthly.SatellitePreview` 精确 `3/3` 通过。展示 Seed 的生产档位 Hash 为 `C2B94139752AD846`，真实强化发射找到 `GravityOnHits=14`、`GravityDependentHits=14`、`LargestSuccessIslandSamples=3`，关闭卫星引力后的最小偏离为 `2756.2 cm`；最佳样本为 `Aim=(-169,0) cm / Pull=0.860`。`L_ABTS_M10 -game -ABTSM3R5Smoke` 的独立 Standalone 进程也以 `RuntimeCertification Terminal=1 Passed=1 Failed=0` 正常退出，并记录 `ProductionProfile=1 TrajectoryCertified=1`。这组证据证明控制台从 `0` 切到 `1` 不只是改变布尔值，而是存在只在卫星引力开启时可达 E5 的实际轨迹。

## 6. 集成交接清单

保持 IntegrationPending，直到 R-4/R-6 选出唯一 Candidate 后由集成工作树完成：

1. 把当前会话诊断快照升级为唯一候选的正式 M9 生产 Actor 配置；不得重新读取 `RetainedCandidates[0]`、重新选择桩对，或把显式预览桥误当默认发布路径。
2. E5 的 M7 Profile/Footprint/AttackFace 必须在卫星局部切平面实例化，并保持与 R-4 Witness 使用的目标代理一致。
3. 用真实 Reinforced M6 发射局部帧、真实 M9 引力查询和真实 M7 Bounds 重算 E5 Positive Witness；预览 Hash 不能充当弹道认证。
4. Visible PIE 验证卫星不在普通弹弓射程、强化弹弓可利用引力到达背面目标，并验证该卫星不进入 M11 四体积分数据。
5. 完成 Character/Visibility/M6 动态代理/M9 穿行碰撞回归后，才能将本阶段从 PreviewAuthority 晋升为 IntegrationAccepted。
