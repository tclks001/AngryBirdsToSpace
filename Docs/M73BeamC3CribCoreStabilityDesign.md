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
