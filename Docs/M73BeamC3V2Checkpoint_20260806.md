# M7.3 Beam-C3 V2 Stage 1 检查点（2026-08-06）

## 1. 结论

四面接地耦合外框已经进入 D0→D1 生产链；历史基础 G0、`DropTrigger.E3`、`TipOver.E1` 与当前
E6 shared G0 均有正证据。最大边界 `SeamRelease.E6` 仍未通过，但预算阻断已经解决：Catalog v11
把平行 Brick 最小净空固定为 40 cm 后，最终候选为 4358/4999。严格桥端双 cell 和两条 Beam-B
权威外轨的共享 course 都已进入真实几何；当前只剩一个 Beam-C Bearing 闭合缺口。

本轮已按停止合同冻结，不运行 G3、G4、人工视觉或完整建筑 Chaos。endpoint/root 配对和最终 Member
预算都不再是当前问题；下一次实验必须先用最小夹具解释 Member 1879 的 cross-bearing 为什么没有在
权威 reclose 后成为 Bearing candidate，不得直接重跑第三种生产几何。

## 2. 工作区身份

- 工作树：`C:\Users\mingyangwu\.codex-official\worktrees\9418\AngryBirdsToSpace`；
- 分支：`feature/m7-buildings`；
- 起始基线：`e6d74a7852efb91a846d89d1d548efe9ef67d10d`；
- 状态：源码、文档和新测试尚未提交；本检查点不能作为精确交付 SHA；
- `Content/Maps/PlanarPhysicsTestMap.umap` 是开始前已有的脏二进制资产，本轮没有覆盖或暂存；本子任务
  开始与结束的文件字节 SHA-256 均为
  `57DC0509B5566449FCEBB33C70DA13FB9E61A624F3FCC59A5D3E953F9AB89FCD`；
- 未获 GUI 授权，因此没有启动图形化 Editor、可见 PIE 或人工视觉验收。

## 3. 当前实现合同

### 3.1 几何

- 生产截面固定为 36 cm，不引入逐 Member 粗细变体；
- 单 cell 使用六 course 宏带：X-through / Y-facade / X-through / Y-through /
  X-facade / Y-through；
- through 是芯体 rail 的延长，不叠加同轴 Brick；
- facade 被相邻 through course 上下夹持；外柱位于 facade/through 交点并在宏带间分段；
- 四面完整、最低外柱段接地、所有新结构轴长和柱段均不超过 720 cm；
- V2 以计划内结构表达替换冲突 body；不调用旧 C3 Host/Portal/donor/post-C2 repair。

### 3.2 根权威与保护空间

- D1 在 Beam-A `AdjacentBayIds` 图上建立局部竖向权威链；
- 根 Bay 必须 `MinZ=0` 且满足 XY 放置；
- 每个向上邻接同时要求上下表面接触和正 XY 投影重叠；
- `HighestReachZ >= RequiredZ` 才能成为 cell 根；
- 链允许经过确实相接的 stacked semantic volume，witness 中全部 Bay/Source 身份进入
  `RootAuthorityCrc32`；未进入该链的 Source 不能作为权威；
- `ReservedSupportVoid` 另作全局几何审计，局部可达不能授权跨 Void，也不能借用 witness 之外的
  无关 Source。

### 3.3 E6 原子双 cell

`MaximumCellCount` 是上限而不是 quota。一般 Profile 仍只生成一个强制 anchor；当前只有精确
`SeamRelease.E6`、`BridgedArcology` 且存在合法 `SupportedSpan` 时，才要求同一桥跨的双端 cell：

1. 选择一个有完整负/正 endpoint 的确定性 `SpanVolumeId`；
2. endpoint 根权威必须包含其 `SupportBayId`，只允许以该支撑的 `SourceVolumeId` 作明确 fallback；
3. 负端 cell 最大边、正端 cell 最小边分别贴合 `BearingPlaneCM`；
4. 垂直于桥轴的 cell 包络覆盖全部 `RailStationsCM ± 18 cm`；
5. 两个根 Source 不同、正体积不重叠、均不侵入保护空间。

E6 的两端和共享 course 是一个原子结构合同；任一条件失败即 fail closed，不退回单 cell，也不搜索
任意第二/第三/第四 cell。

### 3.4 预算与身份

- 每个请求只生成一份计划；
- 所有计划使用精确现有 Member 删除并集，而不是逐 cell 重复抵扣 donor；
- `ProjectedPreClosure = Existing - RemovedUnion + Planned`；
- 闭合预留为 `4 * RailCount * MaximumMacroBandCount`；
- 一般单 cell 受硬最终上限约束；E6 双 cell 与共享 course 作为整体同时满足硬上限减闭合预留，不以
  删除任一端降级；
- Summary 记录 Requested、Selected、BudgetLimited、GeometryLimited、ClosureReserve；
- PlanSet canonical 当前为 v6：除 root、SpanVolume、SupportBay、端点符号、请求/接纳/跳过、预算预留
  和几何身份外，E6 原子双 cell 还纳入 Beam-B 权威外轨中心高、两条 rail station、共享 course 层位
  和覆盖范围。可替换 `BridgeRail` 集合在 plan-set 范围由这些输入确定性派生，但当前不直接写入
  Plan canonical；替换结果仍须由最终真实 Geometry/DAG 证明；
- `PhysicalStability=NotEvaluated` 固定为真值，不以 DAG 结果暗示 Chaos 稳定。

### 3.5 已知未实现边界

- D0 的 `MaximumFallbackLevel`、course/cell reduction 当前只进入 Validate 和 Resolved Hash；D1
  尚未消费，生产只有一个主拓扑，失败即 fail closed；
- 一般超宽语义主体尚不会自动拆成多个 cell；除高 Tier `SupportedSpan` 端点对尝试外，只选择第一个
  确定性根体；
- 一般多 cell 仍未实现；E6 endpoint pair 派生失败、第二 cell 预算不足或共享 course 不完整时立即
  拒绝候选；
- 基础 G0 的双 cell 是手工 FBox 预算测试；新增 E6 共享-course G0 直接覆盖 rail authority、逆序重放、
  超长和预算 fail closed，生产专项另覆盖 D1 BridgeEndpoint/root 派生；
- `FinalGeometry:v1` 按计划顺序哈希 Kind/Axis/Role、量化 AABB Min/Max 和 Length。

## 4. 预算分区证据

V2 接入后，旧 E1 `20–49` 窗在候选搜索前已不可行：已观测的最小生产需求为 55；临时上限 59
仍失败；只用于测量的高上限诊断得到 63～84。当前权威通用窗口改为：

| Tier | 当前 Brick 窗 |
| --- | ---: |
| E1 | 20–99 |
| E2 | 100–299 |
| E3 | 300–799 |
| E4 | 800–1499 |
| E5 | 1500–2499 |
| E6 | 2500–4999 |

这保持六档严格不重叠，也没有修改 36 cm 截面、DAG、720 cm、穿透或 Reserved Void 合同。当前只
有 `TipOver.E1=84` 的一个低 Tier 正样本，不能声明 5×2 已通过。

## 5. 构建身份

使用唯一允许的安装版引擎：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  AngryBirdsToSpaceEditor Win64 Development `
  '-Project=C:\Users\mingyangwu\.codex-official\worktrees\9418\AngryBirdsToSpace\AngryBirdsToSpace.uproject' `
  -WaitMutex -ForceUnity -NoHotReloadFromIDE
```

完整链接成功，权威无后缀模块：

- `Binaries/Win64/UnrealEditor-ABTSRuntime.dll`；
- Size `9,418,240` bytes；
- SHA-256 `8BC644461FA9C585D6216AA926A470628BA5E3E1B53FD3F9CB1BA2E287C134D8`。

首次未带 `-NoHotReloadFromIDE` 的构建因另一个工作树已有 Editor 而生成 `-0001` 热重载 DLL，不算
证据；没有结束该 Editor。上述重建删除了热重载产物并链接无后缀模块。

## 6. 权威自动化证据

| 门 | Filter / 身份 | 结果 | 核心证据 | 日志 |
| --- | --- | --- | --- | --- |
| 历史夹具 | `MassiveXYCrib.Tier0BudgetProof` | 1/1 Pass | 冻结旧 E1=49 数学合同，不读取当前 V2 Catalog | `Saved/Logs/M73-BeamC3V2-HistoricalTier0Budget-Compliant-20260806.log` |
| D0 | `ABTS.M73DAG.BeamD0` | 6/6 Pass | Catalog/Hash、5×6 Recipe 解析、determinism、fail-closed、hard-gate isolation；不代表生产 30 格通过 | `Saved/Logs/M73-BeamC3V2-D0-Compliant-20260806.log` |
| G0 / 历史基础二进制 | `ABTS.M73DAG.BeamC3V2.GroundedCoupledFrame` | 3/3 Pass | Topology、DAG、FailClosed；手工 FBox 在 64 上限接纳 2×28 Member，56 上限保留 1 cell 且 BudgetLimited=1；本轮共享代码后未重跑 | `Saved/Logs/M73-BeamC3V2-G0-Compliant-20260806.log` |
| G0-E6-shared | `...GroundedCoupledFrame.SeamReleaseE6SharedCourse` | 1/1 Pass | 两条 authority rail、输入逆序重放、756 cm fail closed、63/64 精确预算边界 | `Saved/Logs/M73-BeamC3V2-SeamRelease-E6-BridgeShared-G0-R2-20260806-1949.log` |
| G1 | `DropTrigger.E3 / 740000` | Pass | Attempt 0；711 Brick；Cells 1/1；Plan `1757119523`；Geometry `1454963307`；DAG `3490125525`；Max 720/684 cm；222.92 ms | `Saved/Logs/M73-BeamC3V2-G1-DropTrigger-E3-Compliant-20260806.log` |
| G2-low | `TipOver.E1 / 730000` | Pass | Attempt 0；84 Brick；Cells 1/1；Plan `2768066177`；Geometry `1195178146`；DAG `677265849`；Max 288/180 cm；4.79 ms | `Saved/Logs/M73-BeamC3V2-G2-TipOver-E1-Compliant-20260806.log` |
| G2-high / pre-pair | `SeamRelease.E6 / 720000` | Fail | 12 attempts；71,270.85 ms；8 个 Beam-B 成功候选均未派生 endpoint pair；末项 ClosureStalled Resultant 6 / Spread 4 / Members 4849 | `Saved/Logs/M73-BeamC3V2-G2-SeamRelease-E6-EndpointPair-Retry-20260806.log` |
| G2-high / shared | `...SeamReleaseE6SharedCourseProductionSeam` | Fail | 双 cell/共享几何成功；`4671+241+491-186=5217>4999` | `Saved/Logs/M73-BeamC3V2-SeamRelease-E6-BridgeShared-Production-R2-20260806-1950.log` |
| D0 / gap 72 probe | `ABTS.M73DAG.BeamD0` | 6/6 Pass | Catalog/Hash 一致；只作第一个离散减密原型 | `Saved/Logs/M73-BeamC3V2-ParallelGap72-BeamD0-20260806-205544.log` |
| Beam-A / gap 72 probe | `ABTS.M73DAG.BeamA` | 11/11 Pass | 上游装配合同未因减密回归 | `Saved/Logs/M73-BeamC3V2-ParallelGap72-BeamA-20260806-205641.log` |
| G2-high / gap 72 | 固定 E6 专项 | Fail | 3625/4999；4 个 DAG 节点停滞；过疏，不继续 | `Saved/Logs/M73-BeamC3V2-SeamRelease-E6-Gap72-Production-20260806-205741.log` |
| D0 / gap 40 final | `ABTS.M73DAG.BeamD0` | 6/6 Pass | Catalog v11；5×6 全部解析为 min gap 40 / merge gap 4 | `Saved/Logs/M73-BeamC3V2-ParallelGap40-BeamD0-20260806-210059.log` |
| G2-high / gap 40 | 固定 E6 专项 | Fail | 4552/4999；4 个 DAG 节点停滞；预算已解决 | `Saved/Logs/M73-BeamC3V2-SeamRelease-E6-Gap40-Production-20260806-210154.log` |
| G0 / final all-Z | `...FinalAllZSpanFailClosed` | 1/1 Pass | 756 cm rogue Z 被拒绝并清除 DAG certificate | `Saved/Logs/M73-BeamC3V2-FinalAllZSpan-20260806-211749.log` |
| G2-high / parallel cap | 固定 E6 专项 | Fail | 4363/4999；rooted grillage 可达；最终剩 2 节点 | `Saved/Logs/M73-BeamC3V2-SeamRelease-E6-Gap40-TransferGrillage-20260806-211841.log` |
| G2-high / resultant cross-bearing | 固定 E6 专项 | Fail / 当前阻断 | 4358/4999；6 个待修节点消除 5 个；最终仅 Member 1879 | `Saved/Logs/M73-BeamC3V2-SeamRelease-E6-Gap40-ResultantCrossBearing-20260806-2129.log` |

历史夹具、D0、G0、G0-E6-shared、G1、G2-low 和本表后续 gap/closure 专项均使用
`-NoSound -NoMessaging`。旧 G2-high/pre-pair 负日志缺少这两个 flag，只作为历史诊断/停止证据；
它已经被后续精确专项取代，不为补命令格式重复运行。

G2-high 的 Attempt 2 是最清楚的容量样本：`Required=31 Capacity=30 Members=4969/4999`；Attempt 7
为最终 Member `5013>4999`；其它 Beam-B 成功项多为 `ClosureStalled`。所有 8 项都先输出：

```text
EndpointPairUnavailable RequiredSpans=1 Span=0 Endpoints=2 EligibleRoots=2
```

因此该次运行没有证实双 cell 的效果，只证实严格双端派生仍缺少某项前置关系。

`Saved/Logs/M73-BeamC3V2-G2-SeamRelease-E6-EndpointPair-20260806.log` 因命令引用错误没有出现
`Test Started`，明确排除为自动化证据；只有带 `-Retry` 的日志是该实验的权威负证据。

## 7. 已证伪并撤回的路线

| 路线 | 证据 | 结论 |
| --- | --- | --- |
| 旧 E1 上限 49 内继续换 Seed | V2 最少需 55 | 预算窗与结构合同矛盾，必须在 D0/D1.5 明示重分区 |
| E1 临时上限 59 | 仍无候选 | 不能用小幅 cap 调整代替下界测量 |
| E6 4999→8191 | 约 86.09 s 后仍全失败，最佳出现 Resultant 5 / Spread 5 | 预算不是充分根因；8191 已撤回 |
| cell 自动填到 Tier 上限 | E3 6/6 失败，最接近 778/799、Required 22 / Capacity 21 | MaximumCellCount 不是 quota |
| 桥端双 cell 后直接跑 E6 | 8 个 Beam-B 成功项全部 PairUnavailable | 双 cell 实际未进入几何，不能以 E6 结果评价其结构收益 |
| gap 72 后继续扫第三个间距 | 3625/4999 但仍 4 节点停滞；gap 40 已有 447 余量且保留三轨 | 间距只解决密度，不解决缺失支承拓扑；固定 40，不再扫 gap |
| 与 Upper 平行的 quarter-lane cap | reclose 把一根 cap 抬高并脱离双根柱；另一根未覆盖真实 resultant | 已由 load-resultant cross-bearing 取代，不再重试比例或 cap |

旧 `MassiveXYCrib.Tier0BudgetProof` 原先把当前 Catalog 和历史 49 窗绑在一起，E1 改为 99 后会让宽
回归必然失败。本检查点已把它冻结为不读取当前 Catalog 的旧 49 窗数学夹具；当前 V2 窗由 D0/D1.5
生产测试负责，不能再用 Stage-0 布尔结论约束 V2。

正式单 cell E6 在 4999 上限下约 92.98 s 仍失败；8191 诊断约 86.09 s 仍失败。这两项已经足以停止
cap 路线，不需要第三次确认。

## 8. 当前门禁状态

| 阶段 | 状态 |
| --- | --- |
| G0 纯数据 | 历史基础 3/3；当前二进制仅 E6 shared 1/1 Pass |
| G1 DropTrigger.E3 | Pass |
| G2 TipOver.E1 | Pass |
| G2 SeamRelease.E6 | **Fail / 当前阻断** |
| G3 五 Profile E3 | 未运行，禁止越级 |
| G4 5×6 / 30 leaves | 未运行，禁止越级 |
| 人工视觉 | 未运行；无 GUI 授权 |
| 完整建筑 Chaos | 按设计延期至视觉批准后；未运行 |

## 9. E6 原子双芯体共享 course 实验结果

本轮只实现用户指定的 `SeamRelease.E6` 双端桥跨，不实现一般多 cell；外层 Building Seed 为
`720000`，生产专项固定实际 Candidate Seed 为 `972217611`：

1. 必须恰有两个同一 `SupportedSpan` 的 endpoint cell；任一端缺失即 fail closed；
2. 两端都复制并规范化 Beam-B 的权威 `RailCenterZCM` 与恰好两条外轨 `RailStationsCM`；不得把
   `RailCount=4` 误当成桥轨数量，也不得虚构内轨；
3. 在两端共同 course 中选择最接近 Beam-B rail、且相位差不超过 `2 * JointMergeTolerance` 的一层；
   生产样本选中 course 44、中心高 `1602 cm`，与 Beam-B rail 的相位差为 `0.96 cm`；
4. 每个 authority station 的 embedded shared span 必须自身不超过 720 cm，否则 fail closed；生产
   几何用一个 SharedCourse 吸收一侧 core 延伸，并保留另一侧一个 far stub，形成最少两件覆盖；相邻
   垂直 course 只延长到两条 rail station，不延长到整个 cell 外边界，以免穿透外立面 Z 柱；
5. 只有被计划共享 rail 在轴、station、Z 相位一致且沿跨度完整覆盖的既有受保护 `BridgeRail` 才能被
   替换；其它受保护构件仍 fail closed。身份升级为 `PlanSet:v6`、`SharedCourse:v2`。

毫秒级 G0 共享-course 叶子通过，覆盖正向生成、输入逆序重放、756 cm 超长 fail closed 和差一个
Member 的预算 fail closed：

`Saved/Logs/M73-BeamC3V2-SeamRelease-E6-BridgeShared-G0-R2-20260806-1949.log`

首次生产运行在计划阶段发现 sandwich course 延到完整 cell 边界会穿透 Y 面外柱，保留为负证据：

`Saved/Logs/M73-BeamC3V2-SeamRelease-E6-BridgeShared-Production-20260806-1947.log`

收窄到真实 rail station 后，生产几何成功生成：52 个 local course、course 44 / `Z=1602 cm`、两条
共享 rail、R4 core 密度不变、计划 540 Member。随后 Beam-C 全局闭合仍以
`BeamCFinalMemberBudgetExceeded:5217>4999` 拒绝：

```text
旧双 cell 独立 course：4706 + 235 + 464 - 181 = 5224
新双 cell 共享 course：4671 + 241 + 491 - 186 = 5217
```

其中四项依次为闭合前 Member、由最终计数反推的 Beam-C 提交几何量、全局 reclose split、merge；
日志没有把 235/241 单列为 proposal 计数。共享 course
使最终数仅减少 7；支撑需求反而增加 6，`split - merge` 成本由 283 增至 305。权威负日志：

`Saved/Logs/M73-BeamC3V2-SeamRelease-E6-BridgeShared-Production-R2-20260806-1950.log`

因此已经证伪“只把两端同向 rail 改成连续共享 course，就足以让 E6 越过 4999”的假设。它解决了
双 cell 的几何连接表达，却没有解决主导性的 Beam-C 闭合成本。

## 10. 禁止重复

- 不再直接重跑相同 `SeamRelease.E6 / 720000`；
- 不提高 4999，也不调 Member/Joint/Bearing/Support cap；
- 不尝试 3/4 cell，不把 `MaximumCellCount` 当 quota；
- 不把 R4 core rail 数当成 Beam-B bridge rail 数，不派生或虚构额外内桥轨；
- 不再用 Beam-B/C3 中点高度；Beam-A Bearing broadphase 使用 0.5 cm 精确 face bucket，必须采用真实
  C3 lattice Z；
- 不把 sandwich course 延到完整 cell 外边界；只延到权威 rail station；
- 不以 R3/R2、Seed、cap、容差或额外 rail 扫描重复共享-course 路线；
- 不测试第三个平行最小间距；固定 `MinimumParallelBlockGapCM=40 cm`、`TwoBlockMergeGapCM=4 cm`；
- 不再生成与失败 Upper 平行的 25/75 transfer cap，也不改变 support ratio、closure pass 或 resultant margin；
- 不同时改变 rail、course、宏带、预算和 cell 数；
- 不复用旧 C3 Host/Portal/donor/post-C2 repair；
- 不运行 G3/G4 或宽矩阵来猜测 endpoint-pair 根因；
- 不把 NullRHI DAG、截图或旧 Stage-0 独立 Chaos 当成完整建筑物理稳定；
- 不覆盖、暂存或提交 `PlanarPhysicsTestMap.umap`。

## 11. 继续条件

当前 G2 `SeamRelease.E6` 仍失败，G3/G4、人工视觉和完整建筑 Chaos 继续禁止运行。40 cm 已解决原来的
218 Member 缺口，当前不是容量问题。恢复生产实验前必须建立毫秒级 Member 1879 fixture，输出目标
cross-bearing 在 Beam-A reclose 前后的 Member/Role/AABB/owner/Bearing 差异，确认它被 merge、迁移、
重建还是没有形成接触。只有该夹具证明一项原子几何修复会改变最终 Bearing/DAG 身份后，才允许一次
相同 Profile/Tier/Seed 的 E6 专项；不能以参数排列、额外 rail、第三个 gap 或增加 pass 代替。

本轮没有验证物理稳定性。生产共享 rail 的装配所有权仍归负端 C3 cell；旧 Beam-B `BridgeSeat` 顶面
为 `1583.04 cm`、新共享 rail 底面为 `1584 cm`，当前承接来自 C3 sandwich，而不是旧 seat。DAG
可按真实 Bearing 验证，但 `SupportedSpan` 语义归属与 Chaos 接触必须在视觉语言通过后单独评审，不能
由本轮 G0 或 NullRHI 结果外推。

当前 `bSharedCoursePairCertified` 证明的是两条计划 shared rail 与四侧 sandwich contact，并未证明每个
authority station 都实际消费了一个既有 Beam-B `BridgeRail`；E6 G0 还以空 Assembly 建夹具。后续若
重启该架构，必须把“实际替换证据、目标 Span owner、共享所有权、BridgeSeat 保留或退役”作为先于
预算修复的语义合同，不能把当前 certificate 称为 Beam-B rail 替换证书。

## 12. 40 cm 减密与转承闭合增量检查点

本轮只改变平行 Brick 最小净空，36 cm 截面和 4 cm 两根合并门保持不变。72 cm、40 cm 是仅有的两个
离散原型；40 cm 使主体典型交点由 `4×4` 降为 `4×3`，同时保留三轨形态。Catalog 升至 v11，Resolved
Hash 加入截面、最小平行净空和两根合并门，避免几何改变而身份不变。UE 5.8 ForceUnity 完整链接成功；
当前无后缀 DLL 为 9,425,408 bytes，SHA-256
`29B4BC120D7556585B80179095FFCF650D2C7ED030726E314FCCEEE1B83D12AC`。源码未提交，二进制身份只对应
当前工作区。

恢复 deferred rooted grillage 后，V2 会在每次 Beam-A reclose 后立即重跑完整 Beam-C DAG、V2 几何和
所有最终 Z 段跨度门；`bPhysicalStabilityEvaluated` 仍为 false。第一轮 parallel cap 把最终失败从预算
变成两个节点；第二轮 resultant cross-bearing 进一步只剩 Member 1879。最后失败身份：Axis X、Role
SecondaryBeam（枚举 Role=2）、Length 268.62、Supports 1、Resultant/Spread 均无效、Coverage/Span 0.134；仅有旧右端
Y 梁支承。新 Y 向 cross-bearing 虽被提交且 Beam-A 报 Shift=0，但在权威 reclose 后没有出现在该节点的
Bearing candidate 集合中。该差异必须在更小夹具内解释，不能靠第三次 E6 运行猜测。

地图 SHA-256 仍为
`57DC0509B5566449FCEBB33C70DA13FB9E61A624F3FCC59A5D3E953F9AB89FCD`；未启动图形化 Editor、可见 PIE
或 Chaos，未验证其它 Profile/Tier，因此没有可供用户视觉验收的完整矩阵。
