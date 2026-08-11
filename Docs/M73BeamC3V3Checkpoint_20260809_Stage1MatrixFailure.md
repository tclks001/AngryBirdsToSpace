# M7.3 Beam-C3 V3 Stage 1-only 5×6 失败检查点（2026-08-09）

## 1. 目标与边界

- 分支：`feature/m7-buildings`。
- 视觉状态：Stage 1 的 WFC、core placement/pairing intent、core/shared member 三层已由用户批准。
- 目标：验证当前设计在 5 Profile × 6 Tier 的 30 个固定生产身份上都能停在 `CoreAndShared` 并通过
  静态 DAG。
- 不运行 Stage 2+、旧完整 `Stage1.Matrix`、BeamD15、Chaos、可见 PIE 或 Editor。
- 不修改 Seed、36 cm 截面、720 cm、语义基座高度、轨数、预算、容差或物理参数。
- 用户修改的 `Content/Maps/PlanarPhysicsTestMap.umap` 未读取、未修改、未还原、未暂存。

## 2. 新增权威测试入口

新增复杂测试：

```text
ABTS.M73DAG.BeamC3V3.Staged.Stage1CoreAndSharedMatrix
```

它精确枚举 30 个固定 Profile/Tier/Seed 叶，每叶只调用
`GenerateStagePreview(CoreAndShared)`，并检查：

- Stage 1 身份、`Physical=NotEvaluated`；
- 静态 DAG Accepted；
- 全部 explicit core 接地且无 suspended core；
- `HighRegions == BoundHigh == TowerChildren`；
- 无 building group/shell/roof，member 仅为 CoreCourse、SharedCourse、BridgeDiaphragm；
- 最大 member 不超过 720 cm；
- 无 envelope/protected-void/penetration/seat mismatch；
- planned/emitted/member 数一致且 Stage 1 Hash 非零。

旧 `ABTS.M73DAG.BeamC3V3.Stage1.Matrix` 会继续生成完整建筑，并含已被新版 shared lane 取代的
双端 replacement-slot 等式，不再作为本阶段权威矩阵。

## 3. Fresh 矩阵结果

UE 5.8 ForceUnity Development Editor：4/4 actions，成功，约 9 秒。

```text
Found=30
Passed=11
Failed=19
TestElapsed≈55s
Physical=NotEvaluated
```

| Profile | E1 | E2 | E3 | E4 | E5 | E6 |
| --- | --- | --- | --- | --- | --- | --- |
| ColumnBreak | Pass | EmptyBand | Pass | MainCore | NoCandidate | Pass |
| DropTrigger | Pass | Pass | MainCore | MainCore | MainCore | NoCandidate |
| SeamRelease | Pass | EmptyBand | MainCore | MainCore | Pass | Pass |
| SlideRelease | Pass | EmptyBand | MainCore | MainCore | MainCore | NoCandidate |
| TipOver | Pass | EmptyBand | MainCore | MainCore | MainCore | Pass |

失败计数：

- `BeamC3V3EmptyOccupiedBand`：4；
- `BeamC3V3ContinuousGroundedCoreUnavailable`：12；
- `BeamC3StagePreviewNoSemanticCandidate:Last=`：3。

主日志：

- `Saved/Logs/BeamC3V3-Stage1CoreAndShared-Matrix-20260809-1.log`。

三类代表失败均在独立 fresh 进程精确 Found=1 并复现：

- `ColumnBreak.E2`：`EmptyOccupiedBand:Component=0:Band=26`；
- `DropTrigger.E3`：`ContinuousGroundedCoreUnavailable:...HighProjectionEntries=3`；
- `DropTrigger.E6`：`NoSemanticCandidate:Attempts=12:Last=`。

隔离日志：

- `Saved/Logs/BeamC3V3-Stage1Matrix-Isolated-ColumnBreak-E2-20260809-1.log`；
- `Saved/Logs/BeamC3V3-Stage1Matrix-Isolated-DropTrigger-E3-20260809-1.log`；
- `Saved/Logs/BeamC3V3-Stage1Matrix-Isolated-DropTrigger-E6-20260809-1.log`。

## 4. 当前结论与禁止重复路线

Stage 1 的视觉方案已批准，但正式生产矩阵没有完成，当前不能宣布 Stage 1 冻结，也不能进入 Stage 2
或 Chaos。失败不是随机 Seed、总体预算或 Tier 单调增大造成：例如多个 E6 通过而相邻 E2～E5 失败。

禁止通过增加 Attempt、扫描 Seed、调高/调低 semantic seam、放宽 720 cm/容差、缩小子芯体或直接
把主芯体轨数改为 4/5 来追绿。上述动作都没有分别处理三个权威失败身份。

## 5. 下一最小顺序

1. 修复 `GenerateStagePreview` 的候选拒因聚合，使 `Last/Best` 永不为空；只重跑一个 NoCandidate 叶。
2. 证明 Stage 1 是否需要 occupied band；若只服务未来 shell/floor，把构造和 `EmptyOccupiedBand` 门移到
   对应未来阶段；只重跑 `ColumnBreak.E2`。
3. 为宽耦合基座立“多个接地 PodiumMain 组成共同基座网络”或等价主/子归属合同；先做一个包含 3 个
   分离高层投影的纯数据 fixture，再重跑 `DropTrigger.E3`。
4. 三个代表叶通过后才重跑 30 格矩阵；不得用矩阵作为内部盲调循环。

## 6. 2026-08-10：顺序 1 已完成

`GenerateStagePreview` 的候选拒因聚合已修复。最终失败现在包含拒绝门计数、最后一次 Attempt/Seed/Gate/Reason，
以及按违反谓词数和缺口排序的最佳语义候选；没有改 Seed、Attempt 上限、WFC 或任何芯体几何。

新增诊断契约：

```text
ABTS.M73DAG.BeamC3V3.Staged.StagePreviewFailureIdentity
```

Fresh NullRHI 精确 Found=1、Passed=1。三个原空诊断叶在独立 fresh 进程中均精确 Found=1 并按预期保留
产品失败，但已给出完整原因：

- `ColumnBreak.E5`：10/10 均为 SemanticMilestone；最佳 `Volumes=20`，要求 21；
- `DropTrigger.E6`：12/12 均为 SemanticMilestone；最佳 `Volumes=22`，要求 25；
- `SlideRelease.E6`：12/12 均为 SemanticMilestone；最佳 `Volumes=21`，要求 25；
- 三项的 Profile/Silhouette 拒绝数均为 0，跨度和屋顶条件均满足。

日志：

- `Saved/Logs/BeamC3V3-StagePreviewFailureIdentity-Best-20260810-103309.log`；
- `Saved/Logs/BeamC3V3-Stage1-NoSemantic-ColumnBreak-E5-20260810-103406010.log`；
- `Saved/Logs/BeamC3V3-Stage1-NoSemantic-DropTrigger-E6-20260810-103435697.log`；
- `Saved/Logs/BeamC3V3-Stage1-NoSemantic-SlideRelease-E6-20260810-103504391.log`。

结论：这三项在 Stage 1 芯体生成之前即被 WFC 语义复杂度门拒绝，不属于 EmptyBand、PodiumMain、预算、
720 cm 或静态 DAG。下一步仍是顺序 2：调查并隔离 Stage 1 的 occupied-band 阶段污染，只复跑
`ColumnBreak.E2`，不得提前修改语义 Volume 门或进入多主芯体实现。

## 7. 2026-08-10：顺序 2 已完成

代码审计确认 `ComponentBands` 只被完整建筑的 shell/floor/post/common-frame 路径消费；Stage 1 在 core/shared
发射完成后会直接结束，不读取任何 band raster。原 `EmptyOccupiedBand` 是未来阶段预计算污染当前阶段，而不是
WFC 或芯体不闭合。

修复后 `MakeBandSchedule`、occupied-cell raster 和外部空域 mask 只在 `CompleteStaticDAG` 构造；新增矩阵合同要求
Stage 1 的 `BandBaseCourseIndices` 全部为空。四个原 E2 叶均在独立 fresh NullRHI 进程精确 Found=1、Passed=1：

- `ColumnBreak.E2`：Cores=3、Main=1、Children=2、High=2、Bound=2、Members=240；
- `SeamRelease.E2`：同一层级身份，Members=190；
- `SlideRelease.E2`：同一层级身份，Members=164；
- `TipOver.E2`：同一层级身份，Members=206。

全部 `StaticDAG=Accepted`、`Physical=NotEvaluated`。日志前缀为
`BeamC3V3-Stage1-BandBoundary-*-20260810-*`。下一步严格进入顺序 3：先冻结多 `PodiumMain` 接地网络合同和三投影
纯数据 fixture，再修改生产选择器并只重跑 `DropTrigger.E3`。

## 8. 2026-08-10：顺序 3 已完成

新增纯数据三投影夹具：一个 2160×720×360 cm 的共同耦合基座，上方有三个彼此分离的 360×720×720 cm
高层投影。修改前该夹具稳定复现
`BeamC3V3ContinuousGroundedCoreUnavailable:...HighProjectionEntries=3`；修改后由有界确定性 set-cover 选择多个
接地 `PodiumMain`，每个 `HighProjectionRegion` 同时发布直接 main 与 child，形成
`Region -> PodiumMain -> TowerChild` 双向身份。

选择器按 coverage mask 保留有界优质候选，先最小化主芯体数量，再按短边、方正度、面积、中心距离和格点稳定决胜；
组合必须无同轴 lane 冲突。每个 child 在全部 mains 中选择具有真实双向正交 bearing 的直接父项。没有放宽 720 cm、
轨数、容差、WFC 包络或接地要求。

fresh 证据：

- `Staged.MultiPodiumMainCoverage`：1/1；
- 原代表失败 `Stage1CoreAndSharedMatrix.DropTrigger.E3`：1/1；
- 原双投影 `Staged.GroundedPodiumCoreHierarchy`：1/1；
- `Stage1CoreAndSharedMatrix.SeamRelease.E6`：1/1，shared endpoint/course 保持闭合。

日志：`BeamC3V3-MultiPodiumMain-Fixture-Before-20260810.log`、
`BeamC3V3-MultiPodiumMain-Fixture-After-20260810.log`、
`BeamC3V3-MultiPodiumMain-DropTrigger-E3-20260810.log`。

## 9. 2026-08-10：剩余语义缺口与最终 30/30

第一次多主芯体后矩阵为 27/30；全部 12 个旧 `ContinuousGroundedCoreUnavailable` 和 4 个旧 EmptyBand 均已通过，
只剩先前诊断出的三个 Shape Grammar 语义体积缺口。根因是高 Tier 完成显式 macro split/setback 后，milestone 分支仍在
每个后续深度无条件返回 `Stack`，使 72 个软叶预算退化为线性堆叠；增加 Attempt 只能更换比例，不能恢复横向分支。

修复让非桥接原型在宏观里程碑后回到原加权细节规则，不降低 `MinimumVolumes`、不增加 Attempt 或扫描 Seed。
第一次普遍开放后 `SeamRelease.E6` 报 shared endpoint lane 冲突，证明 `BridgedArcology` 的桥端层级是独立拓扑合同；
因此桥接原型保留已视觉批准的线性上层层级，非桥接 TwinTower/TerracedCitadel 才释放加权细节。

最终 fresh 证据：

- Shape Grammar V2：8/8；
- `ColumnBreak.E5`、`DropTrigger.E6`、`SlideRelease.E6`：各 1/1；
- `SeamRelease.E6`：1/1；
- `MultiPodiumMainCoverage`、`GroundedPodiumCoreHierarchy`：各 1/1；
- `Stage1CoreAndSharedMatrix`：精确 Found=30、Passed=30、Failed=0，首叶开始至末叶完成约 276.9 秒。

最终矩阵日志：`Saved/Logs/BeamC3V3-Stage1Matrix-Final-20260810.log`。所有叶均为 Stage 1 静态 DAG，
`PhysicalStability=NotEvaluated`；未运行 Stage 2+、BeamD15、Chaos、可见 PIE 或 Editor。当前可宣布 Stage 1 的
5×6 静态门完成，但 Shape Grammar 加权细节改变了非桥接 E5/E6 的 WFC 形态，仍需按既定流程由用户做视觉复核后才能
进入 Stage 2。

## 10. 2026-08-10：视觉拒收后的诊断合同与第二次 5×6

用户在 PIE 中确认两项旧静态门未表达的缺口：`ColumnBreak.E6` 未选择预期的最高共同裙房分隔，
`DropTrigger.E6` 的耦合基座中心存在大片无芯体区域。本轮没有修改任何生成几何、Seed、Attempt、轨距、预算、
容差、720 cm、地图或物理参数，只增加诊断与 anti-fake 合同：

- Shape Grammar 的每个语义裙房候选发布 scope、legacy/seam/quantized top、span cap、接受状态和首拒原因；
- 每个 applied root 及只读 building-wide common probe 发布候选计数与最终选择；
- 每个 core merge region 以 36 cm 精确基座并集审计 main/全部芯体覆盖、未覆盖格、最大空洞半径和质心间隙；
- 硬门要求诊断一一对应、计数恒等式闭合、拒因不丢失且数值有限；视觉质量阈值尚未冻结。

验证：

- UE 5.8 Development Editor 增量编译成功，约 61.74 秒；
- fresh NullRHI `ABTS.M73DAG.BeamC3V3.Staged.Stage1CoreAndSharedMatrix` 精确 30/30，首叶到末叶约
  286.8 秒，退出码 0；
- 日志：`Saved/Logs/BeamC3V3-Stage1Matrix-Diagnostics-20260810.log`；
- 所有叶仍为 `StaticDAG=Accepted`、`Physical=NotEvaluated`，未运行 Stage 2+、BeamD15、Chaos 或可见 Editor/PIE。

根因结论：

1. `ColumnBreak.E6` 的 Left/Right/common 三个 selection 都是 `Candidates=0`，分别回退 720/504 cm。
   当前算法只把非 Crown volume 的 `Min.Z` 当 seam；截图中的更高可见分隔根本未进入候选集合，而不是被 span
   或 Crown 门明确拒绝。下一最小步骤是分类这些 WFC 可见边界的真实身份，再扩展候选合同，禁止固定抬高。
2. `DropTrigger.E6` 的 `High=Bound=Children=7`，所以旧“每个高层投影一个 child”合同已经成立；但两个 main
   加全部 7 个 child 仅覆盖基座 29.4402%，留下 2193 格，最大空洞 572.049 cm，质心间隙 180 cm。
   根因是 set-cover 只优化高层投影覆盖和最少 main，没有基座空间覆盖目标。
3. 同类风险并非只存在于两张截图。E6 全芯体覆盖率依次为 ColumnBreak 51.2%、DropTrigger 29.4402%、
   SeamRelease 39.4318%、SlideRelease 31.4204%、TipOver 43.3333%；SlideRelease E6 最大空洞更高，达到
   587.694 cm，质心间隙 296.864 cm。
4. 全矩阵明确候选拒因统计为 `ConsumesCrown` 21 次、`AboveIncidentSupportedSpanUnderside` 12 次；不再存在
   “候选存在但原因丢失”。零候选则被单独识别为候选枚举覆盖缺口。

因此上一节“Stage 1 的 5×6 静态门完成”仍成立，但不能再推导“Stage 1 视觉合同完成”。状态停在 Stage 1；
下一轮先修语义边界候选覆盖，再为宽基座设计确定性支承需求点/空间覆盖目标，分别用固定叶验证后才重跑一次矩阵。

## 11. 2026-08-10：语义边界与基座支撑锚点已实施

ColumnBreak 的 volume inventory 证明所有 legacy top 以上的可见分隔均以 Crown 起点出现，旧枚举提前排除 Crown，
所以必然得到 `Candidates=0`。修复后 Crown onset 可作边界见证，但裙房顶必须在其下保留至少两个 36 cm Body course；
Crown 不被删除或重切，span 下表面、较低 Crown 和逐 root Body 延续继续 fail closed。`StartsCrown` 已进入逐候选和
selection 日志。

固定叶：

- `ColumnBreak.E6` 1/1：Left `720 -> 1368 cm`，Right `504 -> 1008 cm`；更高候选均有精确拒因；
- `ColumnBreak.E5` 1/1：Left/Right 为 `1368/1224 cm`；
- `SeamRelease.E6` 1/1：共享桥和静态 DAG 未回归。

宽基座选择器随后增加精确占用栅格支撑锚点：锚点是最接近基座占用格质心的真实格心，不能落入语义空洞。
最少高层投影 set-cover 同数目时优先覆盖锚点；若仍未覆盖，只允许增加一个无 lane 冲突且完整接地的 main，否则
`BeamC3V3PodiumSupportAnchorCoreUnavailable`。覆盖诊断新增 `AnchorX/Y/AnchorCovered`，有 main 的 region 必须
覆盖锚点。

- `DropTrigger.E6` 1/1：Main `2 -> 3`、Cores `9 -> 10`、Members `922 -> 1060`，全芯体覆盖
  `29.4402% -> 34.8456%`，最大空洞 `572.049 -> 494.263 cm`，质心间隙 `180 -> 0 cm`；
- `Staged.MultiPodiumMainCoverage` 三投影宽基座夹具 1/1；
- 完整 fresh `Stage1CoreAndSharedMatrix` 精确 30/30、失败 0，约 396.3 秒；
- UE 5.8 ForceUnity Development Editor 全链接成功，10/10 actions。

日志：

- `Saved/Logs/BeamC3V3-ColumnBreak-E6-CrownBoundaryFix-20260810.log`；
- `Saved/Logs/BeamC3V3-ColumnBreak-E5-CrownBoundaryFix-20260810.log`；
- `Saved/Logs/BeamC3V3-SeamRelease-E6-CrownBoundaryFix-20260810.log`；
- `Saved/Logs/BeamC3V3-DropTrigger-E6-PodiumAnchor-20260810.log`；
- `Saved/Logs/BeamC3V3-MultiPodiumMainCoverage-PodiumAnchor-20260810.log`；
- `Saved/Logs/BeamC3V3-Stage1Matrix-SemanticBoundary-PodiumAnchor-20260810.log`。

所有结果仍为 `Physical=NotEvaluated`；没有运行 Stage 2+、BeamD15、Chaos、Editor 或可见 PIE。用户修改的
`Content/Maps/PlanarPhysicsTestMap.umap` 未读取、未修改、未还原、未暂存。下一停止点是用户视觉复核；覆盖率和
最大空洞仍是观测项，不能把本轮锚点合同外推为物理稳定完成。

## 2026-08-10 TipOver E6 定点诊断补记

本轮只增加诊断并运行单叶 `ABTS.M73DAG.BeamC3V3.Staged.Stage1CoreAndSharedMatrix.TipOver.E6`，没有修改生成排序、几何、Seed、预算、轨距、720 cm、容差或物理参数，也没有运行 5x6、Chaos 或可见 PIE。`PlanarPhysicsTestMap.umap` 是用户测试改动，本轮未读取、未修改、未还原。

- Development Editor 全链接成功；最终 fresh NullRHI 单叶 Success。
- 日志：`Saved/Logs/BeamC3V3-TipOver-E6-RootCause-Final-20260810.log`。
- 身份：Volumes 26，Cores 10，PodiumMain 3，TowerChild 7，High/Bound 7/7，Members 1314，Static DAG Accepted，Physical NotEvaluated。
- 基座：单个 `CoupledGround/Cell/0`，1680 格；main 921，任意芯体 951，未覆盖 729；main 联合边界四向缩进 `130.24/166.24/105.84/177.84 cm`。
- main 重叠：core 0/1 为 `44064 cm^2`，core 1/2 为 `22032 cm^2`；现有最小 set-cover 不含均匀覆盖或重叠惩罚。主芯体耦合/空间分布登记为待解决问题，本 checkpoint 不修改它。
- 高层：PodiumTop `1440 cm` 处已经有 7 个独立 seed 且分别绑定 7 个 child；没有逐层 split。西中央 child 3 在 `3240 cm` 终止，而其 WFC Crown 分支持续到 `3876 cm`；东中央 child 4 到 `3852 cm`，WFC 顶为 `3876 cm`。入口绑定合同因此漏掉了全高持续覆盖。

下次恢复时禁止再从“入口 region 被错误合并”开始。最小实验应先建立 terminal branch 到最高完整 course 的持续绑定合同，再处理 PodiumMain 联合分布；两者都只用 TipOver E6 和纯数据夹具验证，未过前不回到 5x6。

## 2026-08-11 Terminal demand 全集与联合选择闭环

本轮解决的不是 `TipOver.E6/710000` 的 Seed 特判，而是 Stage 1 的需求生成顺序。旧流程先由已选 PodiumMain/入口投影形成
region，再给 region 找 child；710000 右侧未生成 main 后，对应两条终端高柱也不会进入 child 需求集合，造成“没有 main 所以
没有 child、没有 child 所以不需要 main”的循环论证。730000 恰好由现有入口集合覆盖，因此此前修复只在该形态上呈现为通过。

最终修复：

- 先从 WFC Body/Crown course-slice DAG 生成与 main 无关的 terminal demand 全集；710000 精确得到 8 个终端需求；
- 为每个需求先枚举独立接地、固定 footprint、达到最高完整 course 的 child 候选；
- main set-cover 只按裙房入口的空间 `CoverageMask` 负责基座覆盖，再对每个 main 组合做全部 child 的精确无冲突 assignment；
- child 与 main 的直接 bearing 改为优先项而非存在门。无直接接触的接地 child 记录最近 main 为 Stage 1 归属，真正建筑级耦合留给
  Stage 2；实际存在的 bearing 仍须双向闭合；
- shared endpoint 在发射 member 前按最终 main/child 几何精确预计算和预留；
- 局部 `LocalProjectionIndex` 与全局 region ID 分离，修复汇总时原地改写下标导致的诊断记录碰撞；
- main 状态规范化去重、child feasibility 缓存、最少候选 region 优先，使单叶不再进入无证据的长时间组合搜索。

没有采用且不得重复的路径：为 710000 添加 Seed/Profile 特判；从已生成 main 反推需求；要求每个接地 child 必须直接接触 main；
增加 Attempt、扫描 Seed、放宽 720 cm/轨距/容差；以及在超过十分钟仍无阶段输出时继续等待。`DropTrigger.E5` 的诊断反例显示某
terminal region 对 retained main 的直接接触候选数为零，证明直接 bearing 存在门本身就是错误前提，而不是预算或随机形态问题。

最终 fresh NullRHI 证据：

- `Staged.GroundedPodiumCoreHierarchy` 1/1：`Saved/Logs/M73-BeamC3V3-GroundedHierarchy-SpatialMain-Rerun-20260811.log`；
- `Staged.TerminalBranchDemandSplit` 1/1：`Saved/Logs/M73-BeamC3V3-TerminalSplit-SpatialMain-Final-20260811.log`；
- 固定 `TipOver.E6/710000` 1/1：Main 3、Children 8、Terminal 8/8、Static DAG Accepted，日志
  `Saved/Logs/M73-BeamC3V3-TipOverE6-710000-SpatialMain-Final-20260811.log`；
- 最终 5×6：ColumnBreak、TipOver、SeamRelease、SlideRelease、DropTrigger 各 6/6，总计 30/30；对应日志前缀
  `Saved/Logs/M73-BeamC3V3-Stage1-FinalMatrix-*-20260811.log`；
- 单 Profile 六档耗时约 81--169 秒；所有结果均为 `Physical=NotEvaluated`。

本轮未运行 Stage 2+、BeamD15、Chaos、Editor 或可见 PIE。`Content/Maps/PlanarPhysicsTestMap.umap` 是用户测试改动，未读取、
未修改、未还原、未暂存。M7-BC-064 的 PodiumMain 重叠与基座均匀覆盖仍保持待解决；当前可确认的是 Stage 1 终端需求与静态
DAG 合同闭合，下一停止点仍是用户视觉复核。
