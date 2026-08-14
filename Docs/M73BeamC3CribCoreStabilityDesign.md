# M7.3-Beam-C3：井干式稳定芯体

> **历史设计状态（2026-08-05）：** 本文记录的四柱闭环、Host/Portal/donor 与 post-C2 repair
> 路线已冻结，不再作为新版实现入口。新的物理优先设计、预算下界、文献证据与 Stage-0 结果见
> [M73BeamC3V2FrictionCoreDesign.md](M73BeamC3V2FrictionCoreDesign.md)。本文的旧静态证据继续保留，
> 不能外推为 V2 或真实 Chaos 完成证据。

> 父文档：[M7 建筑系统执行路线](M7BuildingDevelopmentRoadmap.md) · [Beam-C Load DAG 与静态代理](M73BeamCLoadDAGAndStaticProxyDesign.md)
>
> 上游：[Beam-B Motif WFC 与结构图语法](M73BeamBMotifWFCAndGraphGrammarDesign.md)
>
> 下游：[Beam-C2 真实接触与承重收口](M73BeamC2RealContactAndLoadClosureDesign.md) → [Beam-D1 真实 Brick](M73BeamD1RealBrickAndMaterialRolesDesign.md) → Beam-D2 Chaos 认证
>
> 状态：Catalog v9 四柱闭环、根系化定向拉结、普通楼层网根系认证、C2 后有界修复与低 Tier 预算收口已完成；安装版 UE 5.8 下，本工作树相关 Unity 对象重新编译链接后的 `ABTS.M73DAG.BeamC3` 17/17 与 `ColumnBreak / Tier 4/5` 专项均通过，随后 `-ForceUnity -DisableAdaptiveUnity` 全链接成功。完整 D1、D1.5、5 Profile × 6 Tier 生产矩阵和真实 Chaos 静置/攻击 PIE 仍待完成。本阶段不接管 TaskGraph 生产建筑。
>
> 2026-08-05 暂停检查点保留了 E5 `1803>1499` 的历史失败现场；恢复后的证伪过程、未重复的失败方案与
> Catalog v9 收敛证据已追加到
> [M73BeamC3Checkpoint_20260805.md](M73BeamC3Checkpoint_20260805.md)。该静态检查点已解除，但不代表 Chaos 动态验收完成。

## 1. 为什么需要 C3

Beam-C/C2 能证明 Load DAG、真实面接触和静态合力存在，但不能证明无连接的摩擦积木在 Chaos 中
动态自稳定。此前真实 Brick 中存在大量贯通楼层的细长 Z 柱；微小偏心会让柱先倾倒，再由高重心
上部结构触发连锁坍塌。该问题在 Tier 0/1 同样存在，不能通过“高 Tier 才加冗余”处理。

C3 在不使用隐式铰链、锁定、胶合或斜撑的前提下，把一部分普通框架预算改写为可见的四柱井干芯体。
它先于 Beam-C2 改写闭合装配，让真实接触、Load DAG 和 D1 Brick 消费同一份最终几何。C3 首要目标是
消除“不攻击也会倒”，并为后续弱点认证提供稳定基线；它本身不定义弱点。

## 2. 权威执行链

```text
Shape Grammar / WFC semantic silhouette
  -> Beam-A Bay / Joint / Member / Bearing IR
  -> Beam-B Motif + global assembly closure
  -> Beam-C3 closed four-post crib rewrite
       -> audit every Z station and find the worst unbraced interval
       -> choose or construct one compact rectangular four-post host
       -> add/reuse four horizontal courses per belt: X0 + X1 + Y0 + Y1
       -> Beam-A authoritative re-closure and segment all four posts
       -> derive explicit host/tie evidence from exact courses and real Bearing
       -> derive a strictly rooted ordinary floor network from current contacts
       -> replace an ordinary host frame when low-tier budget is tight
       -> protect semantic roof, bridge and reserved void geometry
  -> Beam-C2 exact contact + bounded support closure + Load DAG/static proxy
       -> enforce the absolute final Member ceiling on every pass
  -> Beam-C3 final topology / all-Z-span / budget certification
       -> rebuild host/tie/rooted-floor evidence from post-C2 geometry
       -> if C2 creates one new all-Z violation, continue the certified plan
          with a bounded rooted tie or additional host
       -> rerun C2 and derive the same evidence again before final certification
  -> Beam-D1 one Member : one Brick
  -> Beam-D2 real Chaos certification
```

C3 在临时副本上执行。任何生成、重闭合、预算或最终认证失败都丢弃副本并让 D1 有界候选搜索继续；
不得把半个芯体、超预算结构或只通过初次审计的结构送入真实 Brick。

## 3. 四柱闭环几何合同

每个 C3 Host 是一个紧凑矩形，不再采用三柱 L 形近似：

1. 四个 XY 站位分别是矩形四角，必须同时存在并贯穿当前待修复高度区间；
2. 每道 Belt 由两根 X 向 `CoreCourse` 和两根 Y 向 `CoreCourse` 组成闭合井字圈；
3. X/Y course 以一个积木截面错层堆放，在四角形成可重建的真实上下 Bearing，不依赖同平面穿透；
4. 四个角的 Z Member 在每道 course 上下分段并标记为 `CorePost`；缺少任一角、任一方向 course 或
   任一角 post-course 接触都以 `BeamC3CoreTopologyIncomplete` 失败关闭；
5. `CoreCourse` 和 `CorePost` 都是一对一编译的真实 Brick，不是编辑器线框或不可见约束，也不得进入
   D1 临时弱点候选；
6. 若一个 Host 不能覆盖最差 Z 站位，算法可继续为尚未覆盖的违规区间选择额外 Host；不得用一个远距离
   超大芯体横梁收集整栋建筑的无关荷载。

闭环只提供两个水平方向的摩擦承托与短柱分段，不宣称等价于建筑学刚接核心筒。其动态效果仍由第 8 节
的实时 Chaos PIE 决定。

### 3.1 普通楼层网的严格根系合同

C3 可以消费已经存在的普通 X/Y 楼层梁作为约束证据，但不能把“与芯体连通”直接等同于“已被芯体约束”。
最终认证每次都从当前 Member 与真实 `BearingContacts` 重新推导普通楼层网，并同时满足：

1. 芯体的垂直根链必须从精确的 Host course 出发，沿真实 Bearing 到达同一 Host 的 `CorePost` 角柱；仅有
   `CorePost` Role、空间邻近或任意图连通均不足以建立根；
2. 普通楼层网只能沿水平 Member 之间的真实 Bearing 传播，接触面积至少为半个积木截面；不得穿过普通
   Z 柱把整栋塔楼误认证为同一楼层网；
3. 传播必须留在同一 `SourceVolumeId`，并限制在同一楼层带内，最大 Z 漂移为
   `BlockCrossSectionCM * 2.5 + Tolerance`；
4. 保留的承载骨干必须同时含 X 与 Y course，并直接连接至少两个不同的、已垂直根系化的芯体锚点；
5. 在计入证据前先递归剪除没有根锚点的叶分支，所以单锚悬臂、装饰性边枝和“组件另一处碰到芯体”的
   假阳性都不能获得认证；
6. 未进入上述根系集合的普通 course 不参与柱跨截断。若因此仍有 Z 站位超过 720 cm，候选必须在最终
   all-Z 审计中失败关闭，而不是通过 Role 重标、隐藏连接或宽泛 BFS 放行。

该集合是最终几何的派生证据，不是持久化权威。C2、重闭合或修复改变接触后，必须丢弃旧集合并重新推导。

### 3.2 Source-aware 跨 Bay Portal 拉结合同

`BayId` 表示 Shape Grammar/WFC 的装配粒度和成员归属，不表示两个相邻 Bay 在物理上天然断开。为避免
局部修复已生成有效拉结、最终审计却因 Bay 边界再次忽略它，本轮统一以下合同：

1. 若目标 Z 站位与根锚属于同一 `SourceVolumeId`，Portal 拉结可以跨越 `BayId` 公共边界；不得再以
   `BayId` 相等作为承重连通的必要条件；
2. 局部候选枚举、事务式插入、Beam-A/C2 重闭合后的证据重建和最终 all-Z 审计必须调用同一套
   source-aware 判定。不得出现“局部按 Source 接受、全局按 Bay 拒绝”，也不得为了修一个候选而在
   全局 BFS 中取消 `SourceVolumeId` 边界；
3. 第 3.1 节派生出的严格根系 XY 楼层横隔可以作为双向约束端点：它必须位于同一 Source、具有真实
   水平—水平 Bearing、同时含 X/Y course，并保留至少两个独立垂直根锚。满足这些条件后，Portal 不必
   只连接精确 Host 四角；单锚悬臂、装饰性 course 或仅在空间上接近芯体的组件仍不得作为端点；
4. Portal course 必须是一根真实水平 Brick，并在目标端与根系横隔/Host 端分别形成可由最终 AABB
   重建的承接。若 Portal 连接的是两组水平 course，两个端点都必须具有足够的真实 Bearing 面积；Role、
   距离、图边或贯穿但不接触的线段都不能替代实体承接；
5. C2 或 Beam-A 改变任何 Member 后，Portal 两端接触、严格根系横隔和 Source 身份必须从最终几何重新
   派生。不同 `SourceVolumeId` 的跨源拉结继续 fail closed，除非未来另立显式、可测试的跨源结构合同；
6. Portal 只增加真实的水平约束平面，不豁免原始构件长度。任何单根 Z Member 的原始长度超过
   `MaximumUnbracedCorePostSpanCM = 720 cm` 时仍必须失败关闭；不得用“有效跨已被 Portal 截断”放行
   一根未被实体分段的长柱。

该收口不得改变低 Tier 数量合同：Tier 0/1 的最终上限仍为 49/199，C3 累计净增额度仍为 12/33。
在低预算下应优先复用同源、已根系化的楼层横隔并生成最小真实 Portal 原子；若无法在原预算内形成两端
实体承接，则换确定性候选或失败关闭，不得扩大预算或退化为图上连接。

## 4. 所有 Tier 共用的安全合同

- `MaximumUnbracedCorePostSpanCM = 720 cm`，该门槛审计最终装配中的全部 Z 站位，而非只审计已标记
  `CorePost` 的三四根柱；
- Belt 数量由真实高度与统一柱跨门槛推导，DifficultyTier 不得放宽安全阈值；
- 每个 Host 至少一组完整 `X0/X1/Y0/Y1` course、四个角站位和四角真实 Bearing；
- 禁止 Diagonal Member、隐式约束、门洞内救援 Z 柱，以及用屋顶语义积木伪装芯体；
- C3 后仍须通过 Beam-C2 的真实 AABB 接触、合力/支撑展宽、Load DAG 与 D1 无穿透检查；
- C2 后必须再次认证四柱闭环、全部 Z 柱跨和最终 Member 数，不能把“初次 C3 通过”当成最终结果。

Tier 只改变可用轮廓与 Brick 数量窗。相同高度下，Tier 0 不能拥有比 Tier 5 更宽松的柱跨或缺角芯体。

## 5. Tier 0/1 的预算与屋顶保护

低 Tier 的正确策略是**替换普通框架**，不是在完整旧建筑上追加芯体：

1. D0 在 Tier 0 将同一语义量体的普通 Bay 密度压到一组，使候选先天留出稳定结构空间；
2. C3 优先选择芯体所在 Bay 内可替换的普通 `PostAndLintel/CrossBeam/StackedFrame` Assembly，以完整
   四柱井干结构替代它的重复柱梁表达；
3. 替换不得删除 `CoreCourse/CorePost`、BridgeSeat/Rail/Post、Reserved Void 边界或与屋顶/桥共享的 Member；
4. 主屋顶 Crown、两侧檐口和单根屋脊属于视觉合同。单根/双根 roof course 永不可作为 donor；三根以上
   course 即使存在兼容性回退，也只允许移除内部冗余 lane，并必须重新闭合且保持屋顶指纹、最低 course
   数和 E1/E2 单主屋顶里程碑。正式 v8 验收不得以削掉主屋顶来换取芯体；
5. 若普通框架替换后仍无法在数量窗内形成完整四柱闭环，候选应失败关闭并换确定性候选，不得降级成
   三柱、缺边或悬空芯体。

Tier 0 最终上限仍为 49，Tier 1 仍为 199。C3 的累计净增额度分别为 12 与 33；Tier 1 的 33 对齐真实
接触拉结的最小原子单位——一根水平 course，加上其两端 Z 柱在接触面产生的两个真实分段。该额度并不
扩大 199 根总上限。`BeamC2MemberReserve` 只是候选规划提示，不是允许 C2 超限的
第二份预算。Beam-C2 每次补柱和重闭合后都以实际 `Members.Num()` 对 `MaximumFinalMemberCount` 执行硬检查；
最终 D1 Brick 数必须与最终 Member 数一对一且位于原数量窗。

## 6. Catalog v9、身份与诊断

`FABTSM73BeamC3CribCoreSettings` 属于 D0 私有 Resolved Profile。Catalog v9 的
`ResolvedSettingsHash` 覆盖柱跨、最小芯体力臂、Belt 目标、净增预算、最终 Member 上限、C2 规划预留、
普通框架替换与屋顶保护策略；这些字段不暴露给 M3。v9 的唯一配置参数变化是对
`ColumnBreak / Tier 4` 把 `TargetBaySpanCM` 固定为 473 cm，Tier 5 保持 420 cm；CatalogVersion 升级仍会
使全局 Catalog/Resolved 身份 Hash 改变。前者为最终真实 C2 cap 留出 1499 Brick 窗内的容量，不能外推为
全 Profile 的轮廓变化。

D1 Summary 至少记录：

- `bStabilityCoreCertified`、Host/Belt 数、闭合 CoreCourse 数和四角 Bearing 数；
- `StabilityRootedExistingCourseCount`：最终派生的、通过严格根系合同认证的普通楼层 course 数；
- 复用、插入、移除普通 donor 与净增 Member 数；
- 改写前后最大“全部 Z 站位”无水平约束柱跨；
- 确定性 `StabilityCorePlanHash`、`StabilityRootedEvidenceHash`、Catalog/Resolved Hash 与最终 Brick 数；
- 最终结构闭合的 `ClosurePasses` / `ClosureAdded`，以及每次候选拒绝的 Profile、Tier、Base/Candidate Seed、
  Attempt、Gate、Reason、Member/Brick 实数。

`StabilityRootedEvidenceHash` 对根系普通 course 的轴向、中心、长度和 `SourceVolumeId` 集合排序后取 Hash；
它与 Count 一起区分“数量相同但根系成员不同”的候选。Count/Hash 都必须在 C2 后最终几何上重新计算并进入
确定性对比，不能沿用初次 C3 改写时的缓存值。

关键失败语义包括 `BeamC3NoClosedCoreHost`、`BeamC3CoreTopologyIncomplete`、
`BeamC3AllZSpanExceeded`、`BeamC3CoreBudgetInsufficient`、`BeamC3NetMemberBudgetExceeded`、
`BeamCFinalMemberBudgetExceeded` 和 `BeamC3FinalMemberBudgetExceeded`。失败原因必须进入候选日志，不能静默
回退三柱芯体或另一 Profile。

## 7. 正式自动化门槛

### 7.1 低 Tier 5 × 2 生产矩阵

固定夹具为：

| Profile | Seed |
| --- | ---: |
| ColumnBreak | 710000 |
| SeamRelease | 720000 |
| TipOver | 730000 |
| DropTrigger | 740000 |
| SlideRelease | 750137 |

每个 Profile 的 Tier 0 与 Tier 1 均须同时满足：

1. 在本 Tier 有界候选次数内编译成功，且身份仍是原 Profile/Tier；
2. 每个 Host 恰有四个站位，Belt 含完整两 X + 两 Y course，四角接触完整，且无 Diagonal；
3. C2 后 `bStabilityCoreCertified=true`，全部 Z 站位最大无约束跨度不超过 720 cm；
4. 最终 `MemberCount == BrickCount <= MaximumFinalMemberCount`，Tier 0/1 分别位于 20–49、60–199；
5. 真实接触不一致、阻断型支撑违规、严格穿透和悬空 Member 均为零；
6. 同输入的 Candidate Attempt、Catalog/Resolved Hash、Core Plan Hash 与 Brick Geometry Hash 完全确定。

以下是 Catalog v8 的历史静态基线，只用于比较，不能冒充 v9 结果：

| Profile | Tier 0：Brick / 最大柱跨 / Rooted | Tier 1：Brick / 最大柱跨 / Rooted | Tier 1 Host / Tie |
| --- | ---: | ---: | ---: |
| ColumnBreak | 46 / 371.74 / 0 | 163 / 648.00 / 19 | 3 / 3 |
| SeamRelease | 48 / 202.02 / 12 | 138 / 709.09 / 0 | 2 / 0 |
| TipOver | 48 / 543.28 / 12 | 134 / 679.02 / 16 | 2 / 0 |
| DropTrigger | 49 / 660.00 / 0 | 190 / 699.36 / 22 | 2 / 0 |
| SlideRelease | 49 / 693.00 / 0 | 176 / 637.34 / 10 | 2 / 2 |

Catalog v9 当前 5 × 2 静态回归记录于
`BeamC3-Full17-V9-PhysicalCommitFinal-20260805-1821.log`：

| Profile | Tier 0：Brick / 最大柱跨 / Rooted | Tier 1：Brick / 最大柱跨 / Rooted |
| --- | ---: | ---: |
| ColumnBreak | 46 / 299.74 / 0 | 133 / 612.29 / 10 |
| SeamRelease | 48 / 202.02 / 12 | 138 / 690.24 / 0 |
| TipOver | 48 / 543.28 / 12 | 134 / 631.20 / 16 |
| DropTrigger | 45 / 431.70 / 5 | 188 / 683.94 / 0 |
| SlideRelease | 45 / 436.61 / 5 | 181 / 636.64 / 23 |

其中 `ColumnBreak / Tier 1` 为 Host=3、Tie=0；三个真实闭合 Host 与 Rooted=10 已满足合同，不能把 v8
曾出现的定向 Tie 形态升级为强制身份断言。

`DropTrigger / Tier 0 / Seed 669740` 继续作为屋顶与确定性专项，但不能代替上述 10 个生产夹具。5 × 2
已通过，D0 已以 6/6 复验；仍要重跑完整 D1、D1.5 与 5 × 6 视觉生产矩阵，证明 Catalog v9 没有破坏既有阶梯。

旧 Catalog v7 的三柱数值和 `128/128` 日志仅是历史探索证据，不能作为四柱 v8 的完成证据。本轮最终代码
又经 v9 重新编译相关 Unity 对象并全链接，以 `BeamC3-Full17-V9-PhysicalCommitFinal-20260805-1821.log` 登记
`ABTS.M73DAG.BeamC3` 17/17 与 v9 的 5 × 2 静态门槛通过。17 项包含同 Source 跨 Bay、跨 Source 隔离、
严格根系横隔、单锚悬臂反例、最终 all-Z 审计、现有 Plan 修复、确定性和事务回滚；该结论不外推为
Chaos 动态稳定完成。

### 7.2 高 Tier 专项与剩余证据层

- `DropTrigger / Tier 4 / Seed 669740` 已在 `BeamC3-HighTier-StrictRooted-Final.log` 通过：候选
  Attempt 1，最终 2297 Brick、15 Host、20 Belt、219 Rooted，最大 all-Z 柱跨 713.04 cm，
  `PlanHash=3458810760`、`RootedEvidenceHash=2232029679`；
- `ColumnBreak / Tier 4 / Seed 710000` 已在
  `BeamC3-ColumnHighTier-V9-PhysicalCommitFinal-20260805-1822.log` 通过：Attempt 5，最终 1499 Brick、Rooted=159、
  最大 all-Z 柱跨 712.19 cm，`ResolvedHash=1404937059`、`PlanHash=1954804974`、
  `RootedEvidenceHash=2654780606`，闭合账本为 6 Pass / 33 Added；E5 在该测试中重放，全部身份、
  Attempt、Brick、Rooted、MaxAllZ 与账本完全一致；
- `ColumnBreak / Tier 5 / Seed 710000` 同日志通过：Attempt 5，最终 2325 Brick、Rooted=220、
  最大 all-Z 柱跨 667.03 cm，`ResolvedHash=3234425960`、`PlanHash=3180370346`、
  `RootedEvidenceHash=3782347597`，闭合账本为 1 Pass / 15 Added；E6 也执行同等重放并完全一致；
- Source-aware Portal 固定正反例已进入 17 项 Beam-C3 全套：同 Source 跨 Bay 成功、跨 Source 失败、
  严格根系 XY 横隔可作为端点、单锚/装饰横隔失败、任一端缺 Bearing 失败、局部/最终证据一致，
  原始 Z Member 的 720 cm 硬门保持不变；
- E5 的无 void 平行 cap 只允许在“前一轮两条精确 25/75 根系 lane 已被实体证明、随后权威闭合返回同一
  失败 DAG”时生成。普通 proposal 被归一化时先进入 twin-evidence 阶段，不提前消费 token；`BestVoid`
  grillage 不依赖该证据，但修复函数只有在 `BridgeSeat + 2 BridgePost` 全部实际追加后才返回实体事务标志，
  调用层随即在重闭合前登记该失败 DAG，下一轮在任何再次修改前 fail closed。`H1→H2→H1` 等非相邻循环
  同样 fail closed。每个失败 DAG Hash 只允许一次实体 grillage 事务，后续以
  `BeamCStructuralClosureNoProgress` 拒绝，不能把尝试添加数当作真实 Bearing；
- 低 Tier 5 × 2、D0 6/6、Beam-C 13/13 与 Beam-C3 17/17 已复验；仍须重跑完整 D1、D1.5 与
  5 × 6 生产矩阵，
  随后才进入第 8 节实时 Chaos 门槛。

## 8. PIE 动态验收（后续正式门槛）

NullRHI 只证明确定性几何、预算、静态接触与 Load DAG，不证明 Chaos 摩擦积木已动态稳定。物理测试场中
应对 5 Profile × Tier 0/1 各验一组，并记录 Profile/Tier/Seed、Catalog/Resolved/Core/Brick Hash：

1. 只让待测 D1 Runtime Modules/StableBuildingActor 参与 PIE 与启动门，其他预览夹具设为 NotRequired；
2. 不发射、不触碰，跑完整 `IdleValidation` 窗口；必须无持续倾斜、无连锁坍塌并 `Accepted=1`；
3. 近距离确认四柱、两 X + 两 Y 闭合腰带、四角堆放接触和分段短 Z 柱均为真实 Brick，无穿透；
4. 确认主屋顶、桥、门洞、整体轮廓及 49/199 上限没有因芯体预算重分配被破坏；
5. 对一根底部普通支撑做一次受控击打，记录首动 Member、最大位移/转角与坍塌范围。C3 只要求静置稳定；
   普通击打与弱点击打的差异、局部坍塌比例和解题价值留给 Beam-D2 反事实认证。

若静置仍失稳，应优先检查具体 Host 覆盖、柱段真实接触、摩擦参数与高重心宏体是否需要多个 Host；不得通过
隐式锁定、全局冻结或放宽 IdleValidation 掩盖问题。

## 9. 2026-08-12 Stage 1 冻结与 Stage 2 入口

Stage 1（`CoreAndShared`）在用户完成 WFC、主/子芯体、shared course、差异裙房、raised main 与方形单收缩
子芯体视觉验收后冻结。冻结合同为：

1. `SemanticTerminalDemand -> HighProjectionRegion -> TowerChild` 继续保持空间双射、全高承载和接地；
2. joint selection 的 `SupportProvince -> PodiumMain` 覆盖结果是语义父级权威，子芯体不得在发射阶段按几何距离
   重新选择父级；同一省份的 sibling 必须进入同一主芯体家庭；
3. 单收缩 child 的 lower/upper 都是 36 cm 格量化正方形；lower 只参与接地承载、座面与冲突，terminal 身份和
   父级归属使用 demand-carrying upper；
4. 两遍 raised-main 的第一遍批准高度是第二遍安全上限。第二遍允许因合成 Body 得到更高的 child 分界，但必须
   满足 `FinalMinimumChildSplit >= ApprovedMainTop`，不得要求二者虚假相等；
5. Stage 1 仍只发射 CoreCourse、shared rails 和 bridge diaphragm，Static DAG 必须 Accepted，
   `Physical=NotEvaluated`；Stage 2 之前不运行 Chaos，也不把 roof/shell/完整生产失败算作 Stage 1 失败。

冻结证据：UE 5.8 ForceUnity Development Editor 全链接成功；fresh
`ABTS.M73DAG.BeamC3V3.Staged` 44/44（其中 5 Profile × 6 Tier 为 30/30），且完成标记和进程退出均为 0；
Routing/M73A/M73B/M73B2 非未来阶段门 6/6。完整 `ABTS.M73DAG` 的 202/274 作为 Stage 2+ 负基线保留，
72 项主要落在尚未实现的新 roof/shell/complete-production 路径及旧 Beam-A/B，不能通过删除或放宽来伪造冻结。

Stage 2 下一入口保持原设计：只从 Stage 1 已冻结的 CoreCourse/shared 身份发射可追溯的外框耦合 course；
暂不生成共同外框。每根新构件必须记录源 core、course、目标 WFC 外立面和前后 Hash，无法回溯到 Stage 1
骨架的构件失败关闭。Stage 2 先做静态 DAG 与互斥诊断层视觉验收，不进入 Chaos。
## 18. Stage 3：共同外框与外柱生产合同（2026-08-13）

Stage 3 只消费已经冻结的 Stage 2 `FacadePartition` 与
`FacadeHeightAnchorBand`，不得重新选择芯体、重新搜索耦合点，或从全局轮廓直接补一套与
Stage 2 无关的周边框架。本阶段仍不生成 Floor/Infill/Roof，也不运行 Chaos。

### 18.1 外框

1. 一个双层耦合锚带包含 course `B` 与 `B+2` 的两根同向
   `ThroughCourse`。它只授权 course `B+1` 的一根 `FacadeCourse`。
2. `FacadeCourse` 必须与两根 `ThroughCourse` 垂直：耦合方向为 X 时外框为 Y，耦合方向为
   Y 时外框为 X；三者在 facade endpoint 形成完整 `36×36 cm` 十字承压面。
3. 外框拟合该 course 上包含锚点的 WFC facade span。单根最大 648 cm；若整根候选被已有横轨截断，
   必须保留包含锚点的合法最小段并向两侧无冲突扩展，或把已经占据夹持位置的真实水平构件登记为
   Stage 3 复用外框；不得因一次整根碰撞而静默丢弃该锚带的外框。
4. 外框必须记录 lower/upper Stage 2 member、anchor band、facade partition、source volume 和
   Stage 2 输入 Hash。真实下座只引用 lower member；upper member 是夹持/来源证据，不伪造为下座。

### 18.2 外柱

1. 外柱在同一实际立面平面 `(ComponentId, FaceMask, FacadeCoordinate)` 的相邻外框之间建立网络，
   方向严格为 Z；`FacadePartitionId` 和来源芯体不同不得把同一立面切成互不连接的小片。
2. 外柱底面等于下方外框上表面，顶面等于上方外框下表面；水平站位必须在两根外框共同覆盖区内，
   并保留完整 `36×36 cm` 承压面。
3. 每对相邻外框优先在共同区间两端各放一根；不足 72 cm 时只放中间一根。竖向净跨超过
   720 cm 时拆成若干真实相接的 Z member，每段以下一段为真实下座。
4. facade 在两框之间任一 course 中断、构件穿透、void 冲突或没有共同切向区间时，该柱候选显式
   deferred，不得越过退台或洞口补柱。

### 18.3 诊断停点与验收

`AABTSM73BeamD1PreviewActor` 在 `Stage 3 - Common Exterior Frame` 下提供互斥诊断层：

- `Exterior Frames Only`：只显示本阶段 X/Y 外框；
- `Ground Sill Only`：只显示 course 0 的接地底框；若底框格由既有接地芯体砖占据，则显示该复用砖，
  不再叠放一根同层底框；
- `Ground / First-Frame Columns`：只显示底框到各 facade partition 最低外框之间的 Z 柱；
- `Inter-Frame Exterior Columns`：只显示相邻外框之间的 Z 柱；
- `Stage 1 / 2 / 3 Overview`：只读叠加已生成的三个阶段。Stage 1 芯体与 shared course 固定为
  木色，Stage 2 耦合构件固定为玻璃青色，Stage 3 外框、接地底框及两类外柱固定为钢色。被
  Stage 3 底框账本复用的接地芯体构件按其最终外框职责显示为钢色；该显示不得修改 member、
  Bearing、阶段哈希或生成结果。

前四个单项层都不得混入 Stage 1 芯体、Stage 2 耦合构件或另一类 Stage 3 构件；总览层只允许上述
三阶段的既有构件，不得混入 WFC 体块、intent 或其他诊断层。自动化至少验证阶段前缀
不变、外框上下来源闭合、轴向垂直、真实 Bearing、外柱两端框身份、720 cm 分段、无穿透和确定性
`Stage3PlanHash`。完整 Beam-C 合力门继续推迟到 Floor/Infill/Roof 闭合后；本阶段证据只能写为
`StageLocalDAG=Accepted, CompleteBeamC=NotEvaluated, Physical=NotEvaluated`。

### 18.4 接地底框与首层外柱

1. 底框只消费最终 raised-podium facade authority 在 course 0 的真实外露轮廓；不得用全建筑 AABB
   把凹口、分离基座或必须留空区填成矩形。
2. 底框中心线与外框相同，位于 WFC 外表面向内 18 cm；X/Y 段都位于 ground course 并直接接地。
   同层拐角采用确定性所有权：X 段拥有角部实体，Y 段把该格登记为复用，禁止两个方向正体积穿透。
3. 若底框格已经被接地 `CoreCourse`/shared 等真实水平砖完整占据，账本记录该 member 为
   `bReusesGroundedCoreMember`，不发射重复底框。非接地或不完整覆盖的冲突不得被冒充为可复用座面。
4. 每个 facade partition 只从同方向、同 facade coordinate 的底框切向重叠区选择最多两个确定性站位，
   连接到该 partition 的最低外框；没有投影重叠、途中穿过芯体/既有构件或高度不足一块时显式省略。
5. 落地外柱按不超过 720 cm 的真实 Z brick 分段；首段以底框/复用芯体砖为下座，后续段以下一段为下座，
   末段顶面必须抵达最低外框下表面。它建立静态 ground path，但仍不代表 Chaos 稳定性已经通过。

### 18.5 共同外框闭合合同

Stage 3 的验收对象是整栋建筑的共同立面网络，而不是按芯体或 `FacadePartitionId` 分割的局部薄片：

1. 每个 `FacadeHeightAnchorBand` 必须恰好解析出一个逻辑 `CommonExteriorFrame`。已有芯体横轨、
   Stage 2 耦合横轨、同 course 外框或立面转角节点占据夹持位置时，登记为物理构件复用，禁止重复发砖
   或静默丢框。
2. 多个锚带可以共享同一物理外框或转角节点，但各自的锚带身份必须保留。预览中被 Stage 3 复用的
   Stage 1/2 构件按 Stage 3 外框着色。
3. 相邻高度外框在实际立面平面的切向公共区间生成 Z 柱。对于裙房外扩、塔楼回退形成的内移立面，
   向下路径可以落到同 XY 站位的较低外框、芯体横轨、Stage 2 横轨或接地底框；只要真实接触与无穿透
   成立，不要求柱线在每个 course 都继续属于同一分区。
4. 每个逻辑外框必须具有可追溯的向下路径。新发射外框仍要求完整 `36×36 cm` 双层夹持；复用的立面
   转角节点要求上下锚带均有正面积真实接触。
5. `CommonExteriorFrameCount != FacadeHeightAnchorBandCount`、
   `Stage3AnchorBandWithoutFrameCount != 0` 或
   `Stage3FrameDownwardConnectionViolationCount != 0` 均须使 Stage 3 fail closed。
6. 本合同只证明静态 DAG 和视觉结构闭合，不替代后续 Chaos 稳定性验证。

### 18.6 Stage 3 视觉批准与 Stage 4 顶面闭合待办

Stage 3 视觉验收接受当前共同外框、接地底框和外柱网络，但以下两类构件只作为阶段性静态闭合，
不得固化为最终建筑拓扑：

1. 子芯体、塔楼回退或内院边缘存在并非建筑最外层的侧面。该侧面的耦合锚带和外框不应默认一路接到
   ground sill；Stage 4 必须识别其上方可承接的 Floor/Roof 顶面，并将连接目标改为顶框或楼面边框。
2. 某些内侧或回退立面的外框当前没有适合的向下外柱。Stage 4 不得将其静默忽略，也不得为了满足
   数量门强行穿过内部空间补接地柱；应由顶面/楼面闭合构件提供可追溯的承载路径。
3. Stage 4 必须为每个相关 facade/frame 发布明确的 `GroundSill` 或 `TopSurface` 连接意图，并验证二者
   互斥且完整覆盖；只有真正的建筑外周立面允许保留接地外柱。
4. Stage 4 视觉专项必须复查：内侧子芯体侧面不再生成无意义的通地长柱、缺少下向柱的局部外框已经
   接入顶面、连接处具有真实 `36×36 cm` 接触、无穿透且静态 DAG 仍然闭合。

因此 Stage 3 在 2026-08-13 以“共同外框形态通过、顶面归属延期到 Stage 4”的边界冻结；当前结果不代表
内部立面的最终外柱去向已经批准，也不能以 Stage 3 的临时接地路径替代 Stage 4 验收。

## 19. Stage 4：Facade-to-Top 闭合合同（2026-08-14）

Facade-to-Top 只消费已经批准的 `TopSurfaceIntent` 与 `TopSurfaceFrameSegment`，不得重新选择 WFC 顶面、
立面分区或 Stage 3 外框。每条 `TopSurface` 意图必须生成或复用一条可追溯的物理路径，把对应立面框
闭合到同一意图指定的顶面边框；`GroundSill` 意图保持原有接地路径，不进入本轮替换。

### 19.1 两类闭合路径

1. `ExposedSetbackTop`：在外露退台顶框上生成一根与立面法向一致的水平 `FacadeToTopSeat`，再从座面
   内侧站位生成分段 `FacadeToTopPost` 接到上方立面框。座面不得越过 intent 的裁剪后切向区间；首选层
   被已有真实横轨占据时，可以确定性上移到首个合法 course，并补齐外侧短立柱，但不得穿透 void 或
   未知构件。
2. `DirectStackSeat`：立面框与下方顶框处在齐边叠置接缝时，优先以同站位竖柱或真实接缝直接闭合，
   不为了统一外观强行插入没有 36 cm 净空的垂直水平座。
3. 竖向净跨超过 720 cm 时必须拆成若干真实相接的 Z member；竖柱途中遇到同层真实水平轨道时，
   应在轨道上下分段并复用该交叉节点，禁止穿透，也禁止因单个交叉点丢弃整条闭合路径。

### 19.2 替换、共享与身份

1. 属于 `TopSurface` 的 Stage 3 临时 `GroundExteriorPost`/`ExteriorPost` 必须标记
   `bSuppressedByStage4FacadeToTop`；Stage 4 预览不得继续显示这些通地长柱。
2. `TopSurfaceFrameDeferredJunctionPlan` 必须由闭合路径消费，并记录替代顶框 member；未解析 deferred、
   未绑定 intent、未知冲突或穿透均 fail closed。
3. 多条语义 intent 可以共享同一物理座面、竖柱或完整闭合路径，但每条 intent 的 ID、来源 facade、
   顶框与支撑面身份必须保留在 `SourceIntentIds` 中。不得因物理去重丢失语义需求，也不得为相同路径
   叠放重复砖。
4. Stage 4 当前保留稳定 member 索引：被替换的 Stage 3 member 仍留在计划数组中并以 suppression 标志
   排除预览和本阶段有效几何。完整生产装配必须在 Stage 5 静态 DAG 前按该标志重建/压实，不能把
   “预览隐藏”冒充最终 brick 已删除。

### 19.3 诊断停点与证据边界

`Facade-to-Top Connections` 诊断层与其他 Stage 4 层互斥：玻璃色显示来源顶框和上方立面框，铁色显示
水平 seat，石材色显示新竖柱；被替换的 Stage 3 临时柱不显示。视觉验收必须确认每个非接地立面框都
通过 direct、seat+post 或共享路径闭合到正确 TopSurface，且没有重复路径、无意义通地长柱或悬空框。

当前自动化证明的是 `StageLocalGeometry=Accepted`：演示六栋 6/6、预览合同 1/1、全部 TopSurface
intent 已绑定、deferred 已消费、构件跨度不超过 720 cm、无未知冲突。完整 Floor/Infill/Roof、压实后的
生产积木 DAG、Beam-C 合力门与 Chaos 均为 `NotEvaluated`；必须等待本诊断层视觉批准后再冻结并进入
下一停点。

### 19.4 全阶段 36 cm 结构体素合同

从 Stage 1 到 Stage 4 的所有真实结构 member 都必须能严格分解为
`36 cm × 36 cm × 36 cm` 结构体素。WFC 语义包络仍可保留连续坐标，但它只能提供“可容纳范围”；结构
长度必须向内量化，禁止为了贴合连续表面而把实体端面推出语义轮廓。

1. X member 的积木尺寸只能是 `36n × 36 × 36`，Y member 只能是 `36 × 36n × 36`，Z member 只能是
   `36 × 36 × 36n`，其中 `n` 为正整数；既有 648/720 cm 最大跨度门保持不变。
2. 体素相位属于 member 的结构 lane，而不是强制绑定到世界坐标原点。不同芯体、立面或垂直层可以
   保留已经批准的 0/18 cm lane 偏移；同一 member 内的体素必须连续、同轴、同相位。Stage 2 连续
   facade 法向坐标可保留，但实体长度与切向区间必须量化为完整 36 cm 单元。
3. `AddPlannedMember` 是规划层硬门：非轴对齐或任一尺寸不是 36 cm 整数倍时立即 fail closed。Brick
   Compiler 是生产层第二道硬门：截面必须为 36 cm，且一根 member 只能编译为上述三种
   `36×36×36n` 方向积木。
4. Stage 5 的压实生产 DAG 以及后续 Beam-C 接触分析以结构体素身份
   `(ComponentId, MemberId, LaneOrigin, SegmentIndex)` 为基本单元。接地、面接触、承压面积、穿透与
   空洞由体素的精确 AABB 面关系和整数区间推导，不再以未量化长方体或浮点“近似相交”作为结构事实。
5. 语义账本、member 身份、来源 intent 和 36 cm 体素占用必须同时保留；体素去重只合并物理占用，
   不得丢失多个 `SourceIntentIds` 或 Stage 1→4 的因果路径。

本统一只改变结构构件的量化边界，不改变已经批准的芯体、耦合带、外框、TopSurface 与 Facade-to-Top
拓扑，也不构成完整生产 DAG 或 Chaos 已通过的证据。

### 19.5 Floor / StyleInfill 停点合同

Floor / StyleInfill 只消费已经认证的水平承托面，不从建筑 AABB、未解析 WFC 空间或任意高度重新猜测楼层：

1. 合法平面来自 Stage 3 `CommonExteriorFrame` course 与 Stage 4 `TopSurfaceFrameSegment` surface course。
   候选支座必须属于同一 component、同一下层 course 和同一方向；只连接沿目标轴相邻、切向覆盖至少
   一个完整 36 cm lane 的两条真实水平支座。
2. 新楼面位于支座上方一层，并按层奇偶选择 X/Y 方向，保持分层芯体的交错 course 合同。每个主
   `FloorInfillCourse` 必须记录两个不同的 `RequiredLowerMemberIndices`；仅有语义包络但没有两端真实
   Bearing 的行不得生成。
3. 楼面端点落在支座中心线上以形成正面积承压。若一条候选行被冻结的芯体、外框或竖柱占据，则该行
   明确 deferred；只有至少一条通过语义包含、void 与 immutable-prefix 冲突检查的行，才建立“本支座对
   必须生成一根主 Floor”的绑定义务。不得为满足数量门穿透已有构件。
4. 主 Floor 优先于装饰。`StyleInfillCourse` 只能在同一对已认证支座之间追加；E1 不追加，E2～E6 以
   确定性步长逐档增密。达到预算上限时只允许省略 StyleInfill，不能省略已经建立绑定义务的主 Floor。
5. Floor / StyleInfill 发射前先按 Pyramid/Prism 语义数量预留 Roof/Crown member 预算；预留量、deferred
   数、复用数、绑定违规、冲突数、耗时与独立 Hash 都进入结果摘要。密度参数仍是后续预算/承重联调旋钮，
   本停点不把当前数值冻结为最终平衡值。
6. `Floor / StyleInfill` 诊断层与其他层互斥：玻璃色显示两端支座或既有复用行，铁色显示主 Floor，石材色
   显示 StyleInfill。该层只证明已认证平面间的静态楼面路径和 36 cm 单位化；Roof/Crown、压实生产 DAG、
   Chaos 与可见 PIE 仍为 `NotEvaluated`。
