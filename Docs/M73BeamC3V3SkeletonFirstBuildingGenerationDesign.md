# M7.3 Beam-C3 V3：以芯体骨架为起点的四面接地耦合建筑生成

> 状态：2026-08-07 已发生两次视觉拒收。首轮实现没有显式接地分层芯体，并把固定截面的 Crown
> `RoofCourse` 生成为顶部井干塔；grounded rewrite 修正该问题后虽再次取得静态全绿，但第二轮编辑器
> 检查又证明 shared course、建筑组外框和芯体交叉承压合同仍不完整。第 14、15 节结果只保留为对应
> 缺失合同的反例/恢复基线；第 16 节为当前冻结、尚待实现与重新取证的合同。
> 第一阶段只验证确定性几何、真实 Bearing、Load DAG、静态代理与 Brick 预算；完整建筑 Chaos、静置
> 稳定、扰动与攻击认证全部延期到视觉批准之后，当前仍为 `Physical=NotEvaluated`。
>
> 上游：[DAG5-B v2 Shape Grammar / WFC](M73DAG5BShapeGrammarWFCAndBeamEvolutionDesign.md) ·
> [Beam-D0 Profile Catalog](M73BeamD0GameplayProfileCatalogDesign.md)
>
> 下游：[Beam-C Load DAG](M73BeamCLoadDAGAndStaticProxyDesign.md) ·
> [Beam-D1 真实 Brick](M73BeamD1RealBrickAndMaterialRolesDesign.md) ·
> [Beam-D1.5 视觉复杂度](M73BeamD15VisualComplexityLadderDesign.md)
>
> 检查点：[V3 Stage-1 2026-08-07](M73BeamC3V3Checkpoint_20260807.md)
>
> 历史路线：[第一版四柱闭环](M73BeamC3CribCoreStabilityDesign.md) ·
> [V2 分层芯体与后插外框](M73BeamC3V2FrictionCoreDesign.md) ·
> [V2 停止检查点](M73BeamC3V2Checkpoint_20260806.md)

## 1. 决策与阶段边界

V3 不再把 Beam-A/Beam-B 已经生成完的建筑当成不可改变的主体，再向其中插入芯体并让 Beam-C
反复补闭合。WFC 结果只负责表达建筑想要占据的语义包络；真正的实体建筑从芯体骨架开始，由芯体
向外生成四面接地耦合外框、楼层、外柱、桥和屋顶。

第一阶段的完成条件只有以下三项：

1. 五个 Profile × 六个 Tier 的 30 个独立生产叶均能确定性生成；
2. 每个结果通过预算、轴向长度、无正体积穿透、真实 Bearing、全 Member 向地可达、Beam-C Load DAG
   和现有 D1/D1.5 静态门；
3. 生成结果可由用户在编辑器中逐格进行视觉验收，并明确保留 Profile、Tier、主屋顶、桥/门洞和
   四面耦合结构语言。

本阶段固定：

- `bPhysicalStabilityEvaluated=false`；
- 不运行 Stage-0 Chaos、完整建筑 Chaos、可见 PIE 静置、扰动或攻击认证；
- 不以 DAG 通过、NullRHI、截图或编辑器预览宣称摩擦稳定；
- Brick 保持统一 `36 cm × 36 cm` 截面，不引入逐 Member 粗细；
- Beam-C 是只读审计器，不在 V3 路径运行 `GenerateWithStructuralClosure`，不新增 rescue post、
  transfer seat 或 rooted grillage。

视觉批准是进入物理研究的人工门，不是自动化可替代的布尔值。当前第二轮视觉拒收尚未关闭，因此
不得把此前 `G0/G1/30/30/32/32` 静态结果解释为当前结构已经完成，也不得进入 Chaos。

## 2. 为什么必须改为 skeleton-first

当前 V2 生产顺序是：

```text
WFC silhouette
  -> Beam-A 完整主体
  -> Beam-B Motif 与桥完整主体
  -> C3V2 后插芯体/外框并删除冲突构件
  -> Beam-A 全局重闭合
  -> Beam-C 结构修补
  -> C3V2 最终认证
```

它会产生三个结构性矛盾：

1. 上游已消费大部分 Brick、Joint 和 Bearing 容量，芯体只能与既有实体竞争空间和预算；
2. 后插结构与旧梁在同一位置发生 merge、split、shift 或所有权迁移，计划内 `Added` 不等于最终
   Bearing 真实存在；
3. Beam-C 被迫既当审计器又当生成器，最后一个闭合缺口会再次改变几何并要求全局重认证。

V3 把顺序反转：先确定承重站和实际接触，再生成会向这些支座送载的构件。最终 Beam-C 只回答
“已生成的建筑是否满足合同”，不再回答“还能补什么才能过”。

## 3. 权威输入与非权威输入

### 3.1 权威输入

每个候选只消费：

- `GameplayProfileId`、`DifficultyTier`、Base Seed、Candidate Seed；
- D0 的 Catalog/Resolved Settings、Brick 窗、候选上限、轮廓与风格离散 Recipe；
- Shape Grammar / WFC 的 `FABTSM73DAG5BV2GenerationResult`：
  `VolumeId`、`LocalBounds`、`Role`、`Primitive`、SupportedSpan 双端和 Grammar/WFC Hash；
- 统一 Beam-A 截面、接触容差、Member/Joint/Bearing 硬容量；
- Beam-C 的真实接触、支撑展开、合力、跨度、细长比和 DAG 硬门。

### 3.2 非权威输入

以下内容不得成为 V3 生成前提：

- 旧 Beam-A/Beam-B 已生成的 Member、Bearing、Motif placement 或桥 rail；
- C3V1 Host/Portal/donor、RootedExistingCourse、post-C2 repair；
- C3V2 的 Beam-B rail 替换证书、负端 cell 装配所有权或 Member 1879 修复现场；
- 任意编辑器实例中的序列化旧 C3 开关；
- 通过 Seed/Attempt 猜测出来但没有进入 Resolved Hash 的参数。

WFC 是形体/语义合同，不是最终构件清单。V3 可以改变旧 Beam-A/Beam-B 的实体表达，但必须保持
WFC 的实体包络、当前已登记的 SupportedSpan void、屋顶原语和 Profile/Tier 视觉里程碑。Stage-1
上游尚未提供一般 MustVoid；未来接入时必须把它作为新的权威输入和硬门，不能把“未来可登记”写成
当前已经验证。

## 4. 新的中间合同

V3 使用独立中间结果，最终编译为现有 Beam-A 兼容 IR。核心数据概念如下。

### 4.1 Semantic Envelope Authority

对 WFC volumes 做稳定排序和量化，生成：

- `OccupiedVolume`：Foundation/Body/Annex/Crown/SupportedSpan 的实体目标；
- `ProtectedVoid`：Stage-1 当前仅包含 SupportedSpan 下方开洞；量体分割缝和 MustVoid 必须在上游
  明确登记后才能扩展此集合；
- `GroundedComponent`：XY 投影连通且存在 `MinZ=0` 根的量体组件；
- `VerticalReachWitness`：从接地量体经实体上下表面与正 XY 重叠到目标量体的逐跳链；
- `EnvelopeCrc32`：绑定排序后的 Volume/Role/Primitive/Bounds/Span endpoints 和上游 Grammar/WFC Hash。

空间接近、相同语义根字符串或全局最高量体不能代替逐跳 witness。

### 4.2 Core Plan

每个需要独立承重的 GroundedComponent 至少有一个 `CoreCell`。一个 cell 记录：

- Grounded root、witness、服务的 volume 集和合法 Z 范围；
- core XY 包络、每向 rail station、course plane、macro-band plane；
- 四个 facade plane 及其随轮廓层级变化的拟合范围；
- 与其它 core 的共享 course 或 SupportedSpan 桥接关系；
- 预估/实际 Member 数和稳定 plan hash。

cell 数由真实的接地组件、超出单 cell 720 cm 可达范围的轮廓分区和 SupportedSpan 双端决定，
不是 `MaximumCellCount` quota。若最低必要 cell 数超过本 Tier 的预算或几何上限，候选在 emission 前
失败关闭，不枚举无结构理由的额外 cell。

### 4.3 Certified Support Plane / Station

V3 在发射构件前登记支座：

- `GroundSeat`：构件底面位于 `Z=0`；
- `CoreCrossSeat`：上下相邻正交 core course 的真实承压矩形；
- `FacadeSandwichSeat`：facade rail 被相邻 through course 上下夹持；
- `ExteriorPostSeat`：Z 柱段的底/顶分别落在已登记的外框水平层；
- `SharedCourseSeat`：一根共享 course 在两个独立接地 core 内各有真实 sandwich；
- `RoofSeat`：屋顶每根 course 在紧邻下层至少消费一个足够覆盖的支座；
- `SpanEndpointSeat`：SupportedSpan 每条 rail 在负/正端各消费一个指定支座。

每个非接地 Member 在创建时必须引用至少一个已存在的下方 seat；长水平 Member 还必须满足 Beam-C
单支点覆盖或分离支点跨度要求。没有 seat 就不发射 Member，不能先生成再期待 Beam-C 修复。

### 4.4 Member Provenance / Ownership

每个计划构件至少绑定：

- `OwnerKind = CoreCell / ShellFace / BuildingGroupShell / Floor / Roof / SupportedSpan / StyleInfill`；
- `OwnerId`、`SourceVolumeId`、course/band/station；
- 计划 Axis、Role、量化端点和支持 seat IDs。

闭合后允许 MemberId 重排，但必须用上述 canonical 几何和 owner 重建最终对应关系。SupportedSpan
共享 course 归 Span owner，并同时记录两个 core endpoint，不再默认为负端 core 独占。

## 5. 生成算法

### 5.1 G0：解析与前置可行性

1. Resolve D0 Profile/Tier 和 Candidate Seed；
2. 运行现有 Shape Grammar/WFC，取得语义 volumes；
3. 量化所有 bounds，拒绝非有限、退化或超出统一截面网格容差的输入；
4. 建立实体相接图、GroundedComponent、VerticalReachWitness 和 ProtectedVoid；
5. 对每个组件计算最低 core/cell、course、macro-band、四面外框、屋顶和 span Member 下界；
6. 若 `LowerBound > TierMaximum`、最低结构超过 IR 容量、轮廓不能容纳最小 cell，或任何必要轴向
   构件无法在 720 cm 内分段并取得支座，立即 fail closed；
7. 输出 `EnvelopeHash/CoreLowerBound/BudgetMargin`，不进入候选生成。

预算是确定性计划的精确计数或保守下界，不是根据随机种子的模糊估算。Candidate Seed 改变 WFC
形体后会重新计算；同一候选的计划值与实际 emission 必须逐项对账。

### 5.2 G1：先生成芯体骨架

1. 对每个 GroundedComponent 按稳定顺序选择最少 root cells；
2. cell 平面先由可用轮廓宽度和 720 cm 上限确定，再由 Tier Recipe 选择 2/2/3/3/4/5 rail；
3. core course 严格 X/Y 交替，最外 rail 优先贴近可用承压边界，中间 rail 只在预算充足时加入；
4. 相邻 course 的交叉区登记真实 Bearing，最低 course 直接 Ground；
5. course 高度覆盖该 cell 的 vertical witness；任何上部量体必须能追溯到一个接地 core；
6. 两个平行 core 需要结构连接时，优先生成归属明确的 shared course，使同一根构件分别嵌入两端
   sandwich；不得先生成两根重叠 rail 再用去重假定它们共享。

本阶段不加入跨越多个结构带的无围束全高 Z core post。Stage-1 只在相邻、已认证水平带之间生成
短分段 Z post，且每段必须同时引用下方 seat；未来额外内部 Z 仍是物理阶段的独立有限候选。

### 5.3 G2：由芯体拟合四面接地耦合外框

第 13 节冻结后的 Stage-1 实现把早期六-course 草图收敛为一个两层 cell band：

```text
a+0  X：所有 occupied cell 的南/北 canonical 边
a+1  Y：所有 occupied cell 的西/东 canonical 边
相邻 band 之间：只在共同 occupied node 生成分段 Z post
```

四面 facade 不再是事后选中的 Bay AABB，而是上述 cell union 的真实外边界：

1. 在 band 高度求实体轮廓的 XY 连通分量；
2. 将 core through course 分段延伸至该分量四面 facade station；
3. facade rail 只跨越有实体语义的区间，不跨 ProtectedVoid；
4. 每面登记至少两个不同的 exterior Z-post station；每段由 seat DAG 向地可达，不能由同面的水平梁
   代替这个证书；
5. setback/offset 使 facade plane 改变时，在相邻 band 的共同投影内生成 transfer course，且它必须
   先获得两个分离支座；无法形成支座时降低轮廓高度/复杂度的预登记 fallback 或失败关闭；
6. 单根 through/facade/post 均不超过 720 cm；较宽面必须拆成多 cell 或多个被支座覆盖的实体段，
   不能用端对端超长梁。

这样四面外框是 core 的直接延伸，不是套在旧建筑之外的第二套结构。

### 5.4 G3：楼层、外柱和轮廓填充

1. 每个 WFC Body/Annex 层只在已有 macro band/support plane 上生成主楼层；
2. 楼层 X/Y course 从 core/through/facade station 中选取，不建立一套与 core 无关的柱网；
3. 需要视觉密度时，StyleInfill 只能放在两个已认证支座之间，并受本 Tier 剩余预算控制；
4. 外墙 Z 柱只插在相邻已认证水平层之间，底/顶接触同时可重建；
5. 无承重作用的装饰优先减少，不能删除 core、四面 facade、主屋顶或 SupportedSpan 支座来凑预算；
6. 每个 emission 后只做局部确定性去重；不调用旧 donor、Portal 或 post-C2 repair。

### 5.5 G4：SupportedSpan 与 SeamRelease

SupportedSpan 是 skeleton-first 的一等语义，而不是 Beam-B 先生成后再被 C3 替换：

1. WFC 必须提供同一 span 的负/正 support volume；
2. 两端 support volume 各自必须属于 GroundedComponent 并有 core witness；
3. 每条 span rail 直接生成在两端共享 course/seat 上，owner 为 SupportedSpan；
4. rail 两端逐根取得真实 Bearing，中间不生成 ground rescue post，桥下 ProtectedVoid 保持为空；
5. span 超过 720 cm 时使用多个在已认证中间体上承接的实体段；没有合法中间体则候选失败，
   不能用共线端接触冒充竖向 Bearing；
6. SeamRelease 的弱点只从非 core、非主支座的显式 connector 候选中选择，不能破坏完整建筑的初始 DAG。

V2 的“两个 core 共用 course”几何关系可以作为这一阶段的局部构造知识，但不继承旧 Beam-B rail
替换、负端 owner、BridgeSeat 相位或 E6 特判。

### 5.6 G5：屋顶和风格

1. Crown/Prism/Pyramid 继续由 WFC 原语决定；
2. 每个 roof course 使用现有语义 taper 规则，但其下方 station 必须来自 core/shell 的最高认证层；
3. E1/E2 保持唯一主屋顶和至少 8/10 个屋顶 Brick；E3 保持 Prism + Pyramid 里程碑；
4. 五种 Profile 的差异由既有 archetype、material palette、weakness/device/collapse intent 和
   StyleInfill pattern 表达，不复制五套结构算法；
5. 材料与弱点选择在最终 DAG 通过后编译，CoreCourse/Core-support station 永不成为临时弱点候选。

### 5.7 G6：一次规范化与只读静态审计

1. 生成计划提交为 Beam-A 兼容 Joint/Member/Assembly IR；
2. 只允许一次确定性规范化：精确去重、合法分段、重建 Bearing 和 identity；
3. 规范化前后逐 owner 对账，任何计划 Member 被 merge/shift 后失去 seat，立即失败；
4. 调用 Beam-C `Generate`，禁止 `GenerateWithStructuralClosure`；
5. 要求 RealContactMismatch、GroundUnreachable、Cycle、BearingArea、Resultant、Spread、Span、
   Cantilever、ReactionBalance 和严格穿透阻断项均为零；
6. Beam-D1 一 Member : 一 Brick 编译，随后检查 Brick 窗和视觉里程碑；
7. 输出 `Envelope/CorePlan/SupportPlan/FinalGeometry/LoadDAG/BrickGeometry` Hash。

Beam-C 若失败，错误必须指出最早失败的 owner/member/support seat。当前候选被拒绝；不得在同一
V3 结果后继续补结构。

## 6. 预算与 Tier 策略

第一阶段沿用当前六个互不重叠的 D1.5 Brick 窗，不改变 Brick 粗细。低 Tier 的处理顺序固定为：

```text
降低 WFC body 总高或目标量体数
  -> 减少 StyleInfill / 非承重外层重复 course
  -> 保留最小 core + 四面 facade + 主屋顶
  -> 使用已登记的较少 cell / 较少 rail Recipe
  -> fail closed
```

不允许：

- 在生成器内静默抬高 Tier 上限；
- 删除一面外框、桥端支座或主屋顶；
- 将 cell 上限填满以增加 Brick 数；
- 用无支座装饰 Brick 人工填到 Tier 下限；
- 通过额外候选次数掩盖同一确定性下界失败。

若结构本体低于高 Tier 的最小 Brick 数，新增内容必须是消费已认证支座的可见结构密度：更完整的
楼层网、更多内部 rail、更多分段外柱或真实承接的 facade bay；每一项都进入 plan/hash/DAG，不能是
审计外装饰。

## 7. 身份、诊断与 fail-closed

每个候选至少记录：

```text
Profile/Tier/BaseSeed/Attempt/CandidateSeed
CatalogVersion/ProfileCatalogHash/ResolvedSettingsHash
GrammarHash/WFCHash/EnvelopeHash
GroundedComponentCount/CoreCellCount/SupportPlaneCount
BudgetLowerBound/PlannedMembers/ActualMembers/BrickWindow
CorePlanHash/SupportPlanHash/FinalGeometryHash/LoadDAGHash/BrickGeometryHash
StageTimingMs=Profile,Shape,Envelope,Core,Shell,Roof,Normalize,BeamC,Compile,Total
PhysicalStability=NotEvaluated
```

错误摘要同时保留 First/Best/Last，但“Best”只按同时接近全部硬门排序。进入过分支、计划 Added 数、
临时 Member 数或进程退出 0 都不是通过证据。

## 8. 自动化阶梯

前一层未通过时不得运行后一层。实现后的实际测试身份为：

1. `ABTS.M73DAG.BeamC3V3.Stage1.G0`：16 个计划、确定性、容量和反伪造叶；
2. `ABTS.M73DAG.BeamC3V3.Stage1.G1.Boundary`：4 个 E1/E3/E6 边界叶；
3. `ABTS.M73DAG.BeamC3V3.Stage1.Matrix`：5 Profile × 6 Tier 的 30 个独立叶；
4. 上述前缀合计 50 个 Stage-1 叶；本轮分别 fresh 运行三个子门，未用父前缀结果替代它们；
5. `ABTS.M73DAG.BeamD15`：30 个视觉复杂度生产叶，加
   `ColumnHighTierClosure`、`LowTierRoofBearingContinuity`，合计 32 个静态叶；
6. 用户人工视觉验收；
7. 另立 V3 物理设计与实时 Chaos 矩阵。

30 格矩阵和 BeamD15 只作里程碑，不作调参内循环。第一阶段任何命令都不得匹配 Chaos 前缀，
也不得运行可见 PIE。

## 9. 视觉验收合同

自动化通过后，用户在编辑器中逐格检查：

- 芯体的交替 XY course 可读，但不会像一根不透明实心柱；
- through/facade/sandwich/post 的关系与设计图一致，四面均落地；
- setback、双塔、桥、侧翼和尖顶仍贴合 WFC 轮廓；
- E1/E2 是较矮、较疏但完整的建筑，不是被裁掉屋顶的骨架；
- E3～E6 逐档增加可读量体和结构密度，不只增加隐藏 Brick；
- 五个 Profile 的剪影、材质/弱点位置和结构节奏可区分；
- 无超长穿屏梁、重叠加粗假梁、堵住门洞的地柱或漂浮外立面。

视觉不批准时只修改形体/结构语言并重跑静态门；不提前投入 Chaos，以免物理调优绑定尚未冻结的
外观拓扑。

## 10. 已冻结且不得重复的路线

- 不修 V2 Member 1879 来证明 V3；V3 不消费该 post-hoc failure DAG；
- 不继续扫描 12/40/72 cm 或第三个平行净空；V3 首轮沿用当前 40/4 cm 身份，只有新的预算下界
  证明要求改变时才另立实验卡；
- 不再提高 4999、Support/Joint/Member cap、闭合 pass、resultant margin 或容差；
- 不以 3/4 cell quota、Seed、Attempt、R2/R3/R4 或额外 rail 枚举代替结构原因；
- 不复用 Host/Portal/donor/post-C2 repair；
- 不让 Beam-C 为 V3 增加 transfer cap、BridgeSeat 或 rescue post；
- 不先跑 30 格再从末项反推局部错误；
- 不把 Stage-0 Wood-on-Wood 原型外推为新建筑的材料或物理证据。

## 11. 实验卡与停止条件

当前实验卡：

| 项目 | 冻结内容 |
| --- | --- |
| 唯一架构假设 | 从 WFC 轮廓先生成接地 core/support stations，再让外框、楼层、桥和屋顶消费这些支座，可由构造保证静态 DAG 闭合 |
| 主变量 | 生产顺序从 post-hoc insertion 改为 skeleton-first；首轮不改变截面、DAG 阈值或物理参数 |
| 最小 Filter | `V3.G0`，随后单 cell `V3.G1`；未产生预期 Hash/seat/DAG 不运行生产叶 |
| 成功 | 规范化前后 support ownership 可重建，Beam-C 只读审计通过，三个边界叶通过后才扩到 5×6 |
| 失败 | 同一最终拒绝身份两次、单假设两轮、连续 60 分钟无新可排除结论或同路线 4 小时未越过当前最小门，以先到者停止 |
| 回滚 | V3 新文件与 D1 显式路由隔离；旧 V2 保留取证但不作为 V3 fallback |
| 物理 | 第一阶段固定 `NotEvaluated`，不运行 Chaos |

每 60 分钟更新 checkpoint/排错账本。停止时必须记录精确源码/二进制身份、命令、预期/实际测试数、
唯一日志、失败 Hash、已排除假设和下一项毫秒级证伪实验。

## 12. 迁移与交付

1. 新增隔离的 V3 types/generator/tests；
2. D1 生产编译器增加显式 skeleton-first 路由，成功路径不再调用 Beam-A.Generate、Beam-B.Generate、
   C3V2.Generate 或 Beam-C structural closure；
3. 旧 V1/V2 测试与源码保留为历史取证，不作为 V3 fallback；
4. 先通过 G0/G1/三个边界叶，再运行 30 个独立 D1.5 叶；
5. 静态 30/30 后交付用户人工视觉步骤与身份表；
6. 视觉批准后另立物理阶段设计，重新定义材料、质量、摩擦、实时帧率、静置、扰动和攻击门；
7. 未经第 6 步，交接一律写 `PhysicalStability=NotEvaluated`。

## 13. 首版实现冻结：36 cm 整数格构合同

为把第 5 节从结构意图收敛成可证明、可预检的首版算法，V3 Stage-1 冻结以下实现合同。第 13 节的
cell/edge/post 栅格描述的是由显式 core 派生的 shell，不是芯体自身；芯体由第 15 节新增的独立
`FCoreCellPlan` 和每层两根纯 XY `CoreCourse` 定义，不含 Z core post。后续若视觉验收要求改变外观，
可以修改轮廓切片和密度 Recipe，但不得在一次候选内部改用连续参数搜索。

### 13.1 坐标与结构带

- 固定截面 `B=36 cm`；XY 站点、水平层中心和分段端点均使用整数格构，最终发射时才转换为浮点坐标。
- 一个结构带恰好由相邻两层组成：下层沿 X、上层沿 Y，或下层沿 Y、上层沿 X。下层中心为 `18 + 36n`，上层中心为 `18 + 36(n+1)`，两层以完整的 `36 cm` 面接触。
- shell 接地带固定从 `n=0` 开始，下层实体底面为 `Z=0`。后续 shell 结构带之间只在格点发射外框 Z
  柱；柱的底、顶端面分别接触前一带上层和后一带下层。它们是 core-derived shell，不计入 CoreCourse。
- 相邻结构带的默认节距不大于 `648 cm`；Z 柱实体长度必须处于 `36..720 cm`。没有上下两个已登记水平面时不得发射柱。
- 所有水平边按格点分段，单段最大 `648 cm`，为 `720 cm` 硬门和量化误差保留余量。相邻同轴段只允许端面相接，不允许正体积重叠。

### 13.2 Cell、边与承压证明

一个矩形 cell 的 lower course 发射两条同向对边，紧邻的 upper course 发射另外两条正交对边；相邻 cell 对 canonical 边去重。由此得到以下构造证明：

1. 接地 lower course 是唯一的根；不得利用 Beam-C 对浮空 Z 柱的宽松 ground 判定。
2. 每根 upper course 在两个端部格点与 lower course 交叉承压。
3. 每根非接地 lower course 至少引用一个已登记的下方 seat；一般单座/悬臂在 Stage-1 只证明 DAG
   可达，不构成物理稳定证据。
4. 每根 Z 柱由前一结构带的 upper course 承压，并托住后一结构带的 lower course。
5. `SupportedSpan` 和首层屋顶必须具有两个分离支点；一般水平段仍交给 Beam-C 的支撑展开、合力、
   悬臂和跨度静态门。Stage-1 不把这些代理量外推为摩擦稳定。

对 `nx × ny` 个完整矩形 cell，一个结构带的精确水平成员数为：

```text
X 层 = nx × (ny + 1)
Y 层 = (nx + 1) × ny
带间 Z 柱 = (nx + 1) × (ny + 1)
```

预算台账从 canonical edge/post/span/roof 集合直接取整数计数；planned 与 emitted 必须逐项相等。该数字不是 Seed 统计估算。

### 13.3 轮廓、退台与独立接地组件

- 每个 course 高度对非 `SupportedSpan` WFC volume 做切片并栅格化；一个 cell 只有在其四条将要
  发射的 `36 cm` 实体边都被 `Body ∪ Crown` 语义盒并集覆盖时才可占用。中心点只用于稳定选择
  provenance，不能代替完整实体包络证明；连通 cell 形成独立接地组件。
- 上层 occupied cell 自顶向下投影到下一结构带，保证退台、偏移体和高层构件都有实体承重路径；若投影落入普通可占用空间，可加入结构 cell；若落入登记的 bridge undercroft / ProtectedVoid，必须 fail closed。
- 每个独立组件自身都必须含接地带和 X/Y 双向 course。Beam-C 的全局“存在 X 和 Y”检查不能替代组件级证明。
- Tier 密度只通过有限、对称、确定的 cell subdivision 与结构带 schedule 增加；禁止扫描 Seed、间距、相位、rail 数或候选修复路径。

### 13.4 SupportedSpan 的共享 course

- shared bridge course 的轴与 `SpanAxis` 相同；X span 固定在绝对偶数 course、Y span 固定在绝对奇数
  course。两端 `C-1` 和 `C+1` 都必须是端点显式 core 中与桥轴正交的 CoreCourse，形成
  lower/shared/upper sandwich。
- SupportedSpan/shared requirement 在 core/shell 几何规划前冻结，先确定绝对 course 相位、opening、
  ProtectedVoid 和两端 required core top。各 component 建立接地 core 后，待两端 CoreCourse 都可引用时，
  才从负端到正端物化唯一 shared member；不得先生成两根局部 rail 再删除或合并。
- opening 下方不投影 cell、不发射 ground/Z rescue post。共享 course 必须在两端各取得真实 Bearing，并满足 `SeatPositive-SeatNegative <= 720 cm`。
- 当前 5×6 WFC opening 加两端座预计仍低于硬门；代码仍须预检并以 `BeamC3V3SupportedSpanMemberTooLong` 失败关闭。没有合法中间实体时不得用若干端接段伪装跨梁。

### 13.5 屋顶

- RoofBottom 取该 component 的实际连续 CoreTop；若 span 驱动 core 合法上延，屋顶必须从上延完成后才
  开始。Crown/Prism/Pyramid 按 36 cm 高度切为交替 X/Y course，水平边继续按 `648 cm` 分段。
- 第一层屋顶之前必须存在一对正交、不同站位的 `RoofSeat`；当前冻结的最小站位中心距为一个
  `36 cm` 截面，实体可相接但不得正体积重叠。之后每层直接 cross-bear 在紧邻下层，不添加屋顶
  rescue post。
- 沿承托轴只为覆盖声明 lower seat 作精确延伸；横向站位候选只取允许区间边界和同 slab 障碍物的
  非穿透边界，逐个验证后选择最外侧两个、中心距至少 36 cm 的可行站位。这是有限确定集合，不是连续
  坐标扫描。
- 上层屋顶 cell 向下层投影形成嵌套承重 footprint；最顶层退化时保留一个 canonical ridge cell，而不是发射无支撑装饰梁。
- E1/E2 的 8/10 个屋顶 Brick 是该承重屋顶的一部分，不是审计外填数。

### 13.6 单次发射和静态审计顺序

```text
Envelope/Cell/Plane/Span/Roof plan
  -> exact budget preflight
  -> canonical Joint/Member/Assembly emission
  -> 一次 RebuildBearingContacts
  -> 正体积 AABB 穿透门
  -> Beam-C Generate（只读）
  -> D1 one-Member:one-Brick
```

`RebuildBearingContacts` 之后禁止移动或改长任何构件；V3 生产路径禁止调用 `CloseGeneratedAssembly` 和 `GenerateWithStructuralClosure`。若 Beam-C 拒绝，记录首个 owner/member/support 身份并拒绝当前计划，不得在其后插梁、补柱或迁移相位。

## 14. 首轮 Stage-1 历史结果（已被视觉拒收，2026-08-07）

### 14.1 实际生产路径

当前 D1 成功路径已落实为：

```text
D0 Profile/Tier + Shape Grammar/WFC
  -> semantic envelope / directed grounded roots
  -> exact four-edge solid raster
  -> two-course cell bands + segmented Z posts
  -> SupportedSpan shared course + protected undercroft
  -> load-bearing roof
  -> exact IR-cap / Brick-budget preflight
  -> one canonical emission
  -> one RebuildBearingContacts
  -> predicted Bearing pair set == actual pair set
  -> Beam-C Generate (read-only)
  -> D1 one-Member:one-Brick compile
```

成功候选的结构阶段固定只执行一次。WFC 可在尚未得到语义有效形体前使用既有有限 Attempt；第一个语义
有效形体一旦进入 V3，任何几何、预算、Bearing、Beam-C 或 D1 失败都立即拒绝，不再换密度、补柱或
运行 closure。

最终硬门还包括：

- 非 span Member 的 owner、component 与 source 必须一致；`CorePost` 只能向声明 source 做受控向下投影；
- 外立柱 fallback 必须属于同一 root、在当前 slice 活跃，且站位位于声明 source 的半砖 halo；
- 实体包络以发射边的完整 `36 cm` 固体对 `Body ∪ Crown` 并集检查，切分只合并机器精度重复点；
- 每个组件的每一面至少有两个不同、由 seat DAG 证明接地的 Z-post XY 站位；水平周边梁不能代签；
- `RebuildBearingContacts` 后实际 pair 集合必须与计划集合完全相等，重复或等数量替换均失败关闭；
- `ProtectedVoid`、正体积穿透、planned/emitted 数量、IR 容量和所有 identity hash 均在 Beam-C 前硬验。

### 14.2 冻结密度 Recipe

Recipe 是 Profile/Tier 的离散合同，不依赖 Seed、Attempt 或相邻参数扫描。基础表为：

| 难度 | E1 | E2 | E3 | E4 | E5 | E6 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| 水平 cell units | 18 | 12 | 11 | 8 | 9 | 8 |
| 垂直 band units | 18 | 14 | 10 | 8 | 7 | 5 |

为落入各 Profile 已解析且互不重叠的 Brick 窗，冻结以下显式覆盖：

- `TipOver.E2 = 10/14`；
- `TipOver.E4 = 8/7`；
- `ColumnBreak.E5 = 9/6`；
- `TipOver.E5 = 7/6`；
- `TipOver.E6 = 8/4`。

格式均为 `horizontal/vertical`。任何视觉调整必须新建实验卡并重新跑静态门，不得在生产候选内搜索。

### 14.3 首轮 5×6 静态结果（假绿基线）

下表每格为 `Brick 数 / 每组件每面最少不同接地 Z-post 站位数`：

| Profile | E1 | E2 | E3 | E4 | E5 | E6 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| ColumnBreak | 30 / 2 | 152 / 3 | 301 / 3 | 872 / 2 | 1475 / 3 | 1597 / 3 |
| DropTrigger | 30 / 2 | 139 / 2 | 386 / 2 | 1173 / 3 | 2134 / 3 | 3970 / 3 |
| SeamRelease | 30 / 2 | 118 / 2 | 418 / 3 | 948 / 2 | 1777 / 3 | 3495 / 3 |
| SlideRelease | 30 / 2 | 139 / 2 | 358 / 2 | 928 / 2 | 1883 / 3 | 3355 / 5 |
| TipOver | 38 / 2 | 110 / 2 | 324 / 2 | 812 / 2 | 1643 / 2 | 2636 / 3 |

所有结果都位于各自 Resolved Brick 窗内、最大 Member/Z 段不超过 720 cm，且日志固定
`StructuralAttempts=1`、`ClosurePasses=0`、`ClosureAdded=0`、`Physical=NotEvaluated`。

### 14.4 首轮自动化证据（仅供反例取证）

- ForceUnity Development Editor 完整链接：成功，约 22 秒；
- `ABTS.M73DAG.BeamC3V3.Stage1`：49/49，日志
  `Saved/Logs/BeamC3V3-Stage1-All-FinalAudit-20260807.log`；
- 其中独立 5×6 Matrix：30/30，日志
  `Saved/Logs/BeamC3V3-Stage1-Matrix-FinalAudit-20260807.log`；
- `ABTS.M73DAG.BeamD15`：32/32，包含 30 个生产叶、`ColumnHighTierClosure` 和
  `LowTierRoofBearingContinuity`，日志
  `Saved/Logs/BeamD15-V3-Static-FinalAudit-20260807.log`。

每个 fresh NullRHI 进程约 30 秒，其中大部分为 UE 启动；旧的约 20 分钟 D1.5 路径没有被沿用。
本节没有运行 Chaos、图形化 Editor、可见 PIE、静置、扰动或攻击测试。

### 14.5 当前结论

本节的旧 Stage-1 结果只证明旧静态门可以通过，不能再作为“以接地芯体为骨架”的完成证据。用户视觉
验收已经证明旧门缺少关键结构身份：画面顶部的井干塔实际属于 Roof，主体内部没有独立、从地面连续
向上的分层芯体。因此旧 `49/49`、`30/30` 和 `32/32` 是需要保留的假绿反例，不允许据此合并或进入
Chaos。视觉批准前只允许修正形体、轮廓拟合和结构语言并重跑本节静态门；视觉批准后才能另立物理
阶段，重新定义材料、质量、摩擦、solver、静置与扰动合同。

## 15. 视觉拒收后的纠正合同（2026-08-07）

### 15.1 根因

旧实现把整栋 Body 的 occupied raster cell 数记为 `CoreCellCount`，把内部水平楼层边标成
`Floor/CoreCourse`，而真正的 `CoreCell` 只拥有稀疏 Z post；它没有一套独立的 compact core footprint、
course 序列或来源身份。与此同时，Crown 阶段在固定共同 XY 交集上逐层交替发射两根
`Roof/RoofCourse`，E3～E6 会消耗较大的 Crown 高度，因而在建筑顶部形成与分层芯体外观相同的直塔。

坐标编译与预览没有把地面构件上移：Member 端点原样进入 Joint，D1 Brick 位于端点中点，Preview
也没有按 Role 添加 Z 偏移。错误属于生成顺序和身份模型，不能通过移动 Actor 或把顶部塔整体下移修复。

### 15.2 冻结生成顺序

```text
WFC semantic Body/Crown/SupportedSpan
  -> Freeze Shared Requirement / Phase / Void / Endpoint Required Top
  -> Explicit Grounded Core（Body 基座，必要时连续上延）
  -> Four-face Core-derived Coupled Shell
  -> Floor / Style Infill
  -> Per-slice Tapered Roof
  -> Bind Shared Course to both completed endpoint cores
  -> one canonical emission / Bearing rebuild / read-only Beam-C / D1
```

1. 每个接地 component 至少有一个显式 `FCoreCellPlan`；基座来源必须是 Body，最低层实体底面必须等于
   component ground plane，course 以纯 XY 交替并默认连续到约定 Body 顶部。本阶段不允许用全高 Z 柱
   冒充芯体。只有合法 SupportedSpan 的 shared course 高于某端 Body 顶部时，才允许同一个已接地芯体
   沿原站位连续进入 Crown，并且只延伸到 `shared course + upper sandwich` 所需的精确 course；延伸段的
   每个实体仍须由 `Body ∪ Crown` 完整覆盖，禁止新建悬空 Crown 芯体或无语义目的的顶部直塔。
2. shell 的 through/facade/floor/exterior-post 必须记录 `OriginCoreCellId` 和向内依赖，依赖链最终落到
   同一芯体的 `CoreCourse`；外框自身接地不能代替“由芯体向外生成”的证明。
3. shared requirement 在 shell 几何规划前冻结；具体 shared member 在两端 CoreCourse 都存在后绑定，并
   在完整计划中恰好记录两个不同 component 的 core endpoint。X 向桥只占绝对偶数 course，Y 向桥只占
   绝对奇数 course；两端 `c-1` lower seat 与 `c+1` upper sandwich 必须都是与桥轴正交的真实
   CoreCourse，不得借 Floor、Shell 或 Roof seat 假绿。
4. Roof 最后生成，只能拥有 `RoofCourse` 身份，并从该 component 的实际连续芯体顶层之后开始。
   Prism/Pyramid 每层 footprint 必须按该高度的 Crown slice 退缩；禁止在固定共同交集上重复同一截面
   形成竖直井干塔。沿承托轴只允许延到声明 lower seat，横向站位从包络边界与障碍物非穿透边界构成的
   有限候选集中确定性选择，不进行连续位置扫描。
5. `CorePlanHash` 只消费显式 core 及其成员；shell/shared/roof 分别进入 support/final identity，不能再
   用“所有非桥 Member 的 Hash 非零”冒充存在芯体。

这里的“先”是权威需求和依赖顺序，不是 `Plan.Members` 数组或施工帧顺序。实现会在 component 内先追加
core-derived shell/roof、在所有 endpoint core 可引用后追加 shared member；所有成员随后才一次 canonical
emission 并统一重建 Bearing，所以数组追加顺序不参与承重证明。

### 15.3 防假绿静态门

生产 validator 和自动化必须同时拒绝：芯体整体抬高一个 course、course 0 或 Body 高度内的 core source
换成 Crown、shell 清掉 core origin/向内依赖、shared endpoint 换成 Floor/Roof seat、无 shared 需求的
Crown core、以及悬空、断层或超过精确 required top 的 Crown extension。由真实 span 驱动、从地面连续
且精确限高的 Crown extension 是合法同一芯体，不属于上述反例。矩阵至少断言：

- `ExplicitCoreCellCount >= GroundedComponentCount`；
- `GroundedCoreCellCount == ExplicitCoreCellCount`；
- `SuspendedCoreCount == 0`；
- `CoreDerivedShellMemberCount == ShellMemberCount`；
- `SharedCourseNonCoreEndpointViolationCount == 0`。

### 15.4 本轮实验卡与停止条件

- 唯一假设：缺失显式 Body core 与 roof/core 身份隔离，是顶部假芯体和主体无芯体的共同根因；
- 主变量：生成拓扑和 provenance；不调整 Seed、Attempt、Brick 截面、摩擦、solver 或连续密度参数；
- 最小验证：先跑 topology anti-fake，再跑一个普通边界叶和 `SeamRelease.E6`；两者通过后才扩大到受影响
  G0/G1，最后才跑 5×6 与 BeamD15；
- 静态成功判据：自动化能证明上述顺序、接地/lineage/相位/屋顶包络和预算合同；本轮已经满足；
- 人工成功判据：新预览中 core 确实从地面开始、四面外框由它向外展开且 roof 随 Crown 退缩；尚待用户验收；
- 失败判据：同一输入的最终几何/失败身份连续两次不变，或两次实现—专项循环仍不能通过最小门；
- 回滚点：本轮修改前 V3 checkpoint。宽矩阵不作为调试器，Chaos 仍为 `Physical=NotEvaluated`。

### 15.5 Shared course 相位与连续上延

最初把 shared course 放在任意可用层时，E5/E6 都会报 `EndpointSeatUnavailable`。原因不是“已有两座
芯体仍不够”，而是桥轴与芯体绝对 course 奇偶相位不一致：桥下层可能与桥同轴，几何上无法形成交叉
承压座。固定相位后，E5 进入真实最终计数；E6 又暴露第二个独立问题——较矮端芯体在 Body 顶部终止，
而合法桥位于 Crown 内。

最终合同不把 Crown 当作第二个芯体来源，而是计算每个 endpoint 的精确必需顶层：无 span 时为 Body
顶层；有 span 时取所有相接 shared course 的 `c+1`。生成器从地面连续发射到该层，接缝处优先使用
Body source，再使用同 root 的 Crown source。validator 从实际 shared endpoint 反推同一精确高度，少一层、
多一层、断层或悬空上段都失败关闭。这样 E6 的上部 course 是接地芯体的连续部分，不是屋顶上的独立
“假芯体”。

专项结果：

- `ColumnBreak.E5`：1951 Brick，显式/接地芯体均为 4，悬空芯体 0；
- `ColumnBreak.E6`：2515 Brick，显式/接地芯体均为 4，shared course 2，悬空芯体 0；
- 最终矩阵中的 `SeamRelease.E6`：4907 Brick，显式/接地芯体均为 6，shared course 2，悬空芯体 0；
- E6 日志：`Saved/Logs/BeamC3V3-ColumnBreak-E6-GroundedExtension-20260807-113711.log`；
- anti-fake 日志：`Saved/Logs/BeamC3V3-GroundedTopology-CrownExtension-20260807-113926.log`。

### 15.6 屋顶纠正与有限几何解

删除固定 footprint 的 Crown crib 后，`DropTrigger.E4` 先暴露 roof envelope 与承托合同不一致：拓扑允许
屋顶沿梁轴延至 lower seat，而 envelope 只接受原始 Crown AABB。把 envelope 精确扩到声明 seat 后，又
暴露 2 cm 的真实 roof/shell 正体积穿透。最终只在同一高度、同一 slab 的实际障碍物上构造有限候选：
包络最小/最大站位及障碍物非穿透边界，逐个验证后选择最外侧且至少相距一个 36 cm 截面的两站。候选集
由输入几何唯一确定，不读取 Seed/Attempt，也不扫描连续坐标。

`DropTrigger.E4` 最终以 1800 Brick 通过，6 个显式芯体全部接地、1148 个 shell member 全部具有 core
lineage，日志为 `Saved/Logs/BeamC3V3-DropTrigger-E4-NonPenetratingRoof-20260807-115021.log`。

### 15.7 预算重分区与 Stage-1 最终证据

接地芯体与四面外框成为真实输出后，旧数量窗不再代表同一结构合同。只运行一次 5×6 计数调查，得到的
是确定性候选实际发射 Brick 数，不是按 Seed 估算：通用 E1/E2/E3 分别落在 84–110、226–304、493–676；
通用 E4/E5/E6 分别落在 1685–1902、2284–3084、3780–4907。`ColumnBreak` 的 E4/E5/E6 为
1348/1951/2515。随后一次性冻结以下互不重叠窗口：

| Tier | 通用窗口 | `ColumnBreak` 专项窗口 |
| --- | ---: | ---: |
| E1 | 20–149 | 同通用 |
| E2 | 150–349 | 同通用 |
| E3 | 350–799 | 同通用 |
| E4 | 800–2099 | 800–1599 |
| E5 | 2100–3399 | 1600–2199 |
| E6 | 3400–5499 | 2200–3499 |

在冻结窗口前，只做过预登记的 H8/H9/H10 有界诊断，实际计数为 1472/1348/1348；出现平台后立即停止，
没有继续 H11/H12，也没有做 Seed、Attempt、密度邻域或连续间距搜索。计数调查日志为
`Saved/Logs/BeamC3V3-Matrix-CountSurvey-20260807-114126.log`。

最终 fresh NullRHI 静态证据：

| 门 | 结果 | 日志 |
| --- | ---: | --- |
| `Stage1.G0` | 16/16 | `Saved/Logs/BeamC3V3-G0-AfterGroundedRewrite-20260807-115156.log` |
| `Stage1.G1` | 4/4 | `Saved/Logs/BeamC3V3-G1-AfterGroundedRewrite-20260807-115431.log` |
| `Stage1.Matrix` | 30/30 | `Saved/Logs/BeamC3V3-Matrix-Final-20260807-115526.log` |
| `BeamD15` | 32/32 | `Saved/Logs/BeamD15-V3-GroundedRewrite-20260807-115751.log` |

同输入 determinism 另以 `Saved/Logs/BeamC3V3-Determinism-AfterGroundedRewrite-20260807-115108.log`
通过，ForceUnity Development Editor 完整链接通过。全部结果仍明确为 `Physical=NotEvaluated`；它们只证明
当时已有的 DAG、几何、预算和身份合同，不构成用户视觉批准，也不证明纯摩擦 Chaos 稳定性。第二轮视觉
检查又证明这些门没有覆盖第 16 节的三项合同，因此本节结果进一步降级为新实现的对照基线，不能作为合并
或进入物理阶段的证据。

## 16. 第二轮视觉拒收与共同建筑组纠正合同（2026-08-07）

### 16.1 观察、根因与证据边界

用户将手工双芯示意与当前编辑器结果逐项对照后，确认 grounded rewrite 的总体方向正确，但仍有三个
结构性错误：

1. 当前所谓 shared course 是另外生成在两芯之间的两根桥 rail，只用很短的端部搭到芯体；两端芯体原有
   course rail 没有被同一构件替换。桥洞内除这两根 rail 外也没有完整的正交 course band，因此“有两个
   endpoint core id”不等于几何上真正共享；
2. shell 仍按每个 grounded component 独立栅格化、独立要求四面 facade/post，结果是多个芯体各自长出
   一片薄楼体。它们只有上述两根 rail 偶然相接，没有成为由多个芯体共同承托的同一栋建筑；
3. 芯体 X/Y course 在外侧交叉处只形成半砖尺度的搭接，视觉上表现为构件端部退后半格。原 rail 在
   station bounds 终止时，两轴投影只有 `18 cm × 18 cm = 324 cm²`，不是统一截面要求的完整
   `36 cm × 36 cm = 1296 cm²` 承压面。

这些问题不是 Seed、密度、预算窗口或 Chaos 参数造成的。旧 validator 分别只证明“span 有两根 rail”、
“每 component 有四面 shell”和“存在正面积 Bearing”，恰好允许三种错误同时静态全绿。因此前一轮
`G0 16/16`、`G1 4/4`、`Matrix 30/30`、`BeamD15 32/32` 只能作为缺失合同的固定反例；当前修复必须先
新增反伪造合同，再讨论数量窗或视觉结果。

### 16.2 合同 A：真正的双芯共享 course 与完整桥带

shared course 不再表示“跨空洞新增一根接触两端的梁”，而表示“两个 endpoint core 的同一 course/rail
槽位由同一个实体 Member 共同实现”。每个 shared course 固定满足：

1. 两端 core 必须具有相同轴向、绝对 course 和横向 lane 对齐；对每条 lane，只物化一个
   `SharedCourse MemberId`；
2. 该同一个 MemberId 同时出现在两个 endpoint core 的 `(CourseIndex, RailIndex)` 槽位中。两端原局部
   rail 必须被替换并从最终 Member 集移除，禁止“局部 rail 仍在、桥 rail 另加”的三件套假共享；
3. shared rail 的实体范围必须从负端芯体最外物理面连续贯穿 opening，直到正端芯体最外物理面，而不是
   只跨两芯内侧面。每条 rail 在每个 endpoint core 内至少取得两个完整下方 core cross-seat，合计至少
   四个 endpoint-core lower contacts；上方 endpoint course 同样必须实际压在这组 shared rails 上；
4. 全贯穿单 Member 仍受 `720 cm` 硬门。超过时若没有 WFC 明示且已认证的中间实体支座，当前候选直接
   `SharedCourseFullTraversalTooLong` 失败；禁止用共线端接短段伪装同一 shared member；
5. 一个最小完整桥带不是裸露的两根纵梁，而是 `C` 层两根平行贯穿 shared rails 加 `C+1` 层至少一组
   同时承压于两根 rail 的正交 bridge diaphragms。若语义高度允许，可在 `C+2` 再形成下一组纵向 shared
   course；所有 bridge diaphragm 都必须进入 owner、seat DAG、预算和 Hash；
6. opening 下方 `ProtectedVoid` 保持为空，不为满足桥带合同加入 ground/rescue post。桥带只从两端接地
   芯体及其共同外框取得向地路径。

反伪造门必须删除任一 endpoint core 槽位引用、保留被替换局部 rail、缩短 shared rail 使其不再穿过
任一芯体、删除全部 bridge diaphragm，或把四个 endpoint lower contacts 改成只有两次端接；以上任一
变异都必须在 Beam-C 前 fail closed。

### 16.3 合同 B：一个建筑组共同外框

一个候选可以保留多个 `GroundedComponent` 和多个 `FCoreCellPlan`，因为它们分别证明接地来源；但它们
不能各自升级成一栋独立薄楼体。Stage-1 新增候选级 `FBuildingGroupPlan`：

1. 同一 WFC 建筑候选中的全部 Body/Crown components、core cells 和 SupportedSpan 归入一个稳定排序的
   building group；group 保存显式 component/core/span member 列表，不能用连续 MemberId 区间暗示归属；
2. 四面 facade、共同水平 band 和 exterior posts 按整个 building group 的轮廓包络生成。原每 component
   的四面证书降为局部 provenance，不再强迫每个芯体长出自己的四面封闭薄片；
3. 每个共同 band 包含 group perimeter 和从各 core course 向 group perimeter/相邻 core 延伸的 through
   spokes。每个 core 至少有两轴 lineage 进入共同外框，全部 common-shell member 仍可沿真实 seat DAG
   回到一个或多个显式接地 core；
4. SupportedSpan/shared course 是共同建筑内部的结构关系，不是把两栋独立塔暂时连在一起。最终静态图
   必须覆盖 group 内全部 cores，且 group 的四个外立面各有至少两个不同的 grounded exterior-post
   stations；验收统计在 group 级进行；
5. local shell 仍只拟合 `Body ∪ Crown`；building-group perimeter 只允许在整体语义轮廓外使用冻结的一个
   `B=36 cm` facade halo/外挑，并继续减去 `ProtectedVoid`。共同外框不得为了视觉连成一体而填死桥洞、
   门洞或 WFC 已登记空隙；必要水平段按既有 `648/720 cm` 规则分段并逐段取座。

反例固定为“六个 core 各自具有局部四面 mask、但 group 没有共同 perimeter/spoke 或最终图分成多个
互不连通子图”。这种结果即使每个局部 component 都 Ground 可达，也必须以
`BuildingGroupCommonFrameDisconnected` 失败。

### 16.4 合同 C：完整 `36 × 36 cm` 承压面与外挑

Brick 截面继续冻结为 `B=36 cm`，不通过改粗构件修复。几何规则改为：

1. 每条 core X rail 在其两个 Y cross stations 外各延长 `B/2=18 cm`；每条 core Y rail 在其两个 X
   cross stations 外也各延长 `18 cm`。因此每个相邻正交 core crossing 的两个水平投影维度都必须
   `>=36 cm`，实际 Bearing 面积必须 `>=1296 cm²`；
2. 只检查“正面积”或总 Bearing 数不再合格。validator 必须逐 crossing 检查 X/Y overlap、接触 Z 面、
   owner/course 与完整面积；将任一 rail 端点退回半格的反例必须稳定报 `CoreBearingAreaInsufficient`；
3. core-derived through/facade 构件从上述物理 cap 的外端面继续向外，至少显式外挑一个 `B=36 cm`
   单元或一直延到共同外框。它不能把 core cap 截短，也不能与 cap 产生正体积穿透；同轴相邻段只在完整
   端面相接；
4. 外挑、分段和共同外框全部进入 planned/emitted 对账、包络、穿透、seat DAG 和 identity Hash。若 WFC
   实体包络容不下一个完整 cap 加外挑，候选失败关闭，而不是缩回半格保持绿灯。

### 16.5 本轮有限状态机、固定测试与停止条件

本轮不把 5×6 或 BeamD15 当调试循环。状态只允许按下列单向顺序推进：

```text
S0 冻结三项合同与旧失败几何身份
  -> S1 三个纯数据/最小几何 anti-fake：SharedSlot、BuildingGroup、FullFaceBearing
  -> S2 固定 SeamRelease.E6：双芯/多芯、完整桥带、共同外框、ProtectedVoid
  -> S3 一个无 Span 普通边界叶：共同外框不得依赖 shared course 才连通
  -> S4 受影响 G0 / G1
  -> S5 一次 5×6 Matrix 计数与静态门，随后一次 BeamD15
  -> S6 用户编辑器视觉验收
  -> 视觉批准后另立 Chaos 阶段
```

执行约束如下：

- 每一状态都固定 Profile/Tier/Seed、预期 Member/slot/contact/group 关系和唯一失败码；前一状态未通过时
  不运行后一状态；
- 本轮唯一主变量是 owner/topology/geometry contract。`36 cm` 截面、Catalog Recipe、Seed、Attempt、
  min gap、merge gap、预算 cap、720 cm、Bearing 容差和 Chaos 参数全部冻结；禁止网格搜索、连续扫描或
  为某格加入 Profile/Tier 特判；
- Matrix 只有在 S1～S4 通过后运行一次。若共同外框使固定候选整体越出旧 Brick 窗，只允许输出一次
  精确 `core/shared/common-shell/roof` 计数调查并回到预算设计层；不得在矩阵内换密度追窗；
- 同一最终 `FailureCode + PlanHash + FinalGeometryHash + LoadDAGHash` 连续两次不变，或同一假设完成两次
  “实现—专项”循环仍未越过当前状态，立即停止该路线；连续 60 分钟没有新增可排除结论或累计 4 小时
  未越过当前最小门，以先到者建立 checkpoint 并做设计评审；
- 每次停止记录精确源码/二进制身份、固定输入、预期/实际测试数、唯一日志、首个失败 member/slot/contact、
  已排除假设和下一项毫秒/秒级证伪测试。候选数、临时 Added、局部 mask 或宽门运行时间不算进展。

当前证据层仍固定为 `DAG + 视觉`。本节实现完成前不运行 Chaos；完成并通过静态门后，也只交给用户进行
编辑器视觉验收。只有用户明确批准共同外框、共享桥带和完整承压形态后，才为真实摩擦、质量、solver、
静置、扰动和攻击建立新的物理设计与认证矩阵。

## 17. 可停止、可追溯的生产阶段合同（2026-08-07）

### 17.1 术语迁移与总原则

第 8、14～16 节历史上把“从 WFC 到完整建筑并通过静态 DAG”整体称为 `Stage-1`。该名称从本节起只作
历史日志标签，不再表示现行生产停点。现行阶段固定为：

| 阶段 | 唯一生产输出 | 当前状态 |
| --- | --- | --- |
| Stage 0 `SemanticEnvelope` | Shape Grammar + WFC 语义包络、ProtectedVoid 和稳定身份 | 已有基准 |
| Stage 1 `CoreAndShared` | 显式接地分层芯体、shared 配对、slot 替换后的长 shared course、bridge diaphragm | 本轮实现与视觉验收 |
| Stage 2 `CouplingCourses` | 从不可变 Stage 1 member 向四面目标立面伸出的耦合长砖 | Stage 1 视觉批准后实现 |
| Stage 3 `CommonExteriorFrame` | 只能从 Stage 2 端点闭合的候选级共同外框与外柱 | Stage 2 视觉批准后实现 |
| Stage 4 `FloorInfillRoof` | 楼板、非结构填充和屋顶/风格构件 | Stage 3 视觉批准后实现 |
| Stage 5 `StaticDAG` | 对 Stage 0～4 不可变几何的完整 Bearing/Load DAG 与 Brick 静态门 | Stage 4 视觉批准后实现 |

Chaos 不属于 Stage 0～5。只有 Stage 5 与用户视觉验收均通过后，才另立物理研究阶段。现阶段统一记录
`PhysicalStability=NotEvaluated`。

每个阶段必须真正停止生成：选择 Stage 1 时不得先生成共同外框、楼板或屋顶再把它们隐藏。下游阶段只读
消费上一阶段冻结结果并追加本阶段 delta，不得移动、缩短、合并或替换已获视觉批准的上游 member。若上游
Hash 改变，下游视觉批准自动失效。

### 17.2 不可变阶段数据与身份链

实现依次持有 `Stage0SemanticPlan -> Stage1CoreSharedPlan -> Stage2CouplingPlan ->
Stage3CommonFramePlan`。当前 C++ 可以先在同一个 `FPlan` 内保存这些分区，但必须满足等价约束：

1. 每个 member 记录 `ProducedStage`、`BuildingGroupId`、`SourceVolumeId`、`OriginCoreCellId`、
   `ParentMemberIds/RequiredInwardMemberIndices`、`SharedSpanId`、`AnchorBandId` 与稳定 member signature；
2. `Stage1Hash = Hash(Stage0Hash, CoreCellPlan, SharedIntent, CoreAndSharedMembers)`；以后每级 Hash 都显式包含
   上一级 Hash 与本级 delta；
3. Stage 2 member 的 parent 必须是 Stage 1 的 CoreCourse/SharedCourse；Stage 3 member 的 parent 必须是
   Stage 2 coupling endpoint。任何无法沿 provenance 回溯到 Stage 1 接地芯体的外框构件都失败关闭；
4. Stage 3 禁止重新执行一套与芯体无关的全局 perimeter raster。多个芯体属于同一建筑时，它们共同支撑
   一个 building-group 外框，而不是各自生成薄片后再偶然相接；
5. 阶段下拉项是生产 API，不是显示过滤器。尚未实现的阶段被选择时必须返回明确的
   `BeamC3StageNotImplemented`，不得回退到旧完整生成路径。

### 17.3 Stage 1 权威生成顺序

Stage 1 必须按下列顺序单向执行，并在最后一行停止：

```text
读取已接受 Shape Grammar / WFC
  -> 建立 SemanticEnvelope、GroundedComponent 与 ProtectedVoid
  -> 依次规划 CoreCell footprint / ground plane / required top，并用已冻结的 component 语义共同约束选择
  -> 在规划时共同约束 SupportedSpan 两端芯体的平行位置和 lane 相位
  -> 完成全部 core 的预测槽位后，冻结 SharedCourseIntent（两端 core、轴、course、lane、opening）
  -> 在纯数据 Plan 中生成每个芯体的连续纯 XY CoreCourse
  -> 用同一个长 Member 替换两个 endpoint core 的对应 course/lane 槽位
  -> 发射 bridge diaphragm 并重建本阶段 Bearing/Load DAG
  -> 冻结 Stage1Hash，停止；不得生成 coupling、shell、floor、post 或 roof
```

这里的“规划/生成”只向纯数据 `Plan` 追加预测构件，并不等于已经向 Beam-A 或预览场景发射实体。
只有全部 core、shared slot replacement 与 diaphragm 都完成并冻结 Stage1Hash 后，才统一进行 canonical assembly
发射。因此，后一个芯体的选址可以读取前一个芯体已冻结的 lane/station 约束，但用户看到的 Stage 1 结果仍然来自
同一个完整、不可变的 Stage 1 计划，不会出现“先显示孤立芯体、再用后处理桥条补接”的生产路径。

Stage 1 的 core footprint 必须来自 WFC Body 的接地实体，最低砖底面等于 ground plane。需要 shared course
的两个 core 在选址时即满足平行、同相、同 lane；不得等生成两个任意芯体后再用短桥补接。shared rail
继续遵守第 16.2 节：它是同时属于两个芯体槽位的单一长 Member，而不是两根局部 rail 加一根桥 rail。

Stage 1 不应用完整建筑 Brick 数量窗、四面外框证书、外柱密度、楼板或屋顶门；这些是后续阶段合同。它
仍须通过本阶段静态门：全部 core 从地面可达、每层正交 course 有真实 lower bearing、shared slot 替换
完整、bridge diaphragm 有真实 shared-rail seat、ProtectedVoid 无穿入、无正体积穿透、member/span 上限
合法、planned/emitted 一一对应。该静态门不推断摩擦稳定性。

### 17.4 Stage 1 三种诊断层

物理测试场中的 `AABTSM73BeamD1PreviewActor` 提供两个下拉项：`Generation Stop Stage` 决定真实生产停点，
`Stage 1 Diagnostic Layer` 决定当前显示的证据层。Stage 1 验收必须能独立切换：

1. **WFC Semantic Envelope**：按真实 Box/Prism/Pyramid 形态显示全部 WFC volume，并区分 Body/Crown、
   SupportedSpan 与 opening/ProtectedVoid；显示 `GrammarHash/WFCHash/EnvelopeHash`。本层不生成 V3 member；
2. **Core Placement Intent**：只显示每个候选芯体的接地 footprint、预测完整体积、core id/source
   component，以及每个 `SharedCourseIntent` 的两端 core、轴、course/lane 与跨越范围；不得同时绘制 WFC
   包络或 ProtectedVoid。本层显示的是发射前冻结意图，不得从最终短桥位置反推或伪造配对；
3. **Core + Shared Courses**：只显示 Stage 1 实际 member，按 CoreCourse、SharedCourse、BridgeDiaphragm
   区分；不得出现 Through/Facade/Floor/ExteriorPost/Roof 或 BuildingGroupShell。shared member 必须在
   两端芯体槽位中呈现为同一个长构件。

三层的可见对象集合必须两两互斥：WFC 层只含语义包络/ProtectedVoid，Intent 层只含 core/pair intent，
Member 层只含实际 core/shared/diaphragm。三层必须消费同一个 `Stage0Hash/Stage1Hash`，Actor 切换诊断层不得重新选择 Seed、Attempt、密度或 core
位置。日志至少输出 Profile、Tier、Seed、选中的生产停点/诊断层、三个 Hash、volume/core/shared/member
计数、静态 DAG 结果与 `Physical=NotEvaluated`。

### 17.5 本轮验收和停止条件

本轮只实现 Stage 0/1 的真实停点与上述三层诊断，不实现 Stage 2～5，不运行 5×6 宽矩阵、BeamD15、
Chaos、PIE 静置、扰动或攻击。代码门按以下顺序执行：

1. 纯数据测试证明 Stage 0 不调用 V3 member 生成；
2. 固定普通叶证明 Stage 1 只有 CoreCourse；固定 SeamRelease shared 叶证明 long shared slot 与 diaphragm；
3. 反例删除 endpoint slot、把 shared 缩短成 opening-only、注入 shell/roof member 时在 Stage 1 门失败；
4. ForceUnity Development Editor 完整链接；
5. 用户在 Editor 依次检查三层并明确批准，之后才设计 Stage 2。

若固定 Stage 1 叶失败，调试只允许检查 core 选址、shared intent/slot、bearing、ProtectedVoid、身份与阶段
边界。不得通过修改 Seed、Attempt、36 cm 截面、密度、Brick 窗、720 cm 或 Chaos 参数取得绿灯；同一
失败身份两次不变或两轮实现仍不能越过最小门时建立 checkpoint，禁止提前运行宽门。

## 18. 相邻 WFC 基座语义合并与多轨方形芯体（2026-08-07）

### 18.1 派生语义，不改写 WFC

Stage 0 的 Shape Grammar/WFC 输出继续作为不可变输入。Beam-C3 在 `GroundedComponent` 与
`CoreCellPlan` 之间新增派生数据 `CoreMergeRegion`：它只表示若干相邻接地语义体可以共同提供一个
连续芯体基座，不删除、不移动、不扩张任何 WFC volume，也不得用各 volume 的联合 AABB 填平 L 形
缺口或 `ProtectedVoid`。

两个原接地 component 只有同时满足以下条件时才加入同一 merge region：

1. 属于同一个 WFC building path，ground plane 相同；
2. 两个接地基座在 XY 上有正面积重叠，或在一个轴上整面相接且另一个轴有正长度重叠；
3. 不是只有角点相触，不跨越正宽度间隙、SupportedSpan opening 或 ProtectedVoid；
4. 合并后的芯体每一层、每一根完整 rail 都能被原始 Body/Crown 实体并集覆盖。

若上述任一条件不成立，不得靠扩大容差、填洞或改变 WFC 来强行合并。当前 Stage 1 对“基座相邻、但
找不到覆盖全部来源且全高连续的多轨 footprint”稳定 fail closed；是否拆回多个 region/core 留到本轮
视觉验收后显式设计，不允许静默回退。每个 region 记录来源 volume/component、精确接地基座片段、合并包络、轨数与
独立 `CoreMergeRegionHash`，使 `EnvelopeHash -> CoreMergeRegionHash -> CorePlanHash` 可追溯。

### 18.2 近方形芯体与离散多轨合同

芯体候选仍只在 36 cm 整数格上枚举，并优先选择可连续贯穿所需高度的近方形最大内接矩形；候选
实体必须逐 rail、逐 course 通过原 WFC 实体并集覆盖检查。合并区域替代的原接地 component 数为
`N` 时，目标轨数冻结为：

```text
RailCount = clamp(ceil(2 * sqrt(N)), 2, 5)
```

因此 `N=1` 使用 2 轨，`N=2` 使用 3 轨，`N=3..4` 使用 4 轨，`N=5..6` 使用 5 轨。轨站在芯体
两端之间按整数格确定性均匀分布，包含两端且不得重复。相邻 X/Y course 的理论完整承压交叉点从
固定 4 个提升为 `RailCount^2`；这才是大芯体增加接触面积的来源，而不仅是把两根外缘 rail 拉长。

成员数量仍是精确生成值而非 Seed 估算：高度为 `C` 个 course 的芯体使用
`C * RailCount` 根砖。预算诊断除 brick/member 数外同时记录总 member 长度、最大单件跨度与完整
承压交叉点数；当前阶段不得仅因长砖“只算一块”就宣称更省材料或更稳定。任何 member 继续受
720 cm 硬门约束。

### 18.3 Stage 1 顺序、诊断与回退

Stage 1 顺序调整为：

```text
WFC Semantic Envelope
  -> 原始垂直接地 component
  -> CoreMergeRegion（相邻基座合并、来源与空洞保持）
  -> 芯体体积/轨数预判与 shared pairing intent
  -> 连续接地多轨 CoreCourse
  -> shared slot 替换、长 SharedCourse 与 bridge diaphragm
  -> Stage 1 静态 Bearing/Load DAG，停止
```

`Stage 1 Diagnostic Layer` 增加独立的 **Core Merge Regions** 层；四层仍两两互斥：

1. WFC Semantic Envelope：只显示 WFC/ProtectedVoid；
2. Core Placement / Pairing Intent：只显示最终 core 体积与 shared 配对；
3. Core + Shared Courses：只显示实际 Stage 1 members；
4. Core Merge Regions：只显示派生 region 的精确接地来源片段，不显示 WFC 外壳或最终 member。

本轮只验证新增合并/多轨定向合同与既有 Stage 1 静态门，随后停止供 Editor 视觉验收；不运行
Stage 2、5x6 宽矩阵、BeamD15、PIE 或 Chaos。若同一失败身份连续两次不变，立即 checkpoint，禁止
以 Seed、密度、砖块粗细、预算窗口或物理参数调参追绿。

## 19. 耦合裙房与接地芯体层级（2026-08-08）

### 19.1 取代“一个语义根只有一个等截面全高芯体”假设

相邻楼体在接地层耦合后，不再要求同一截面同时适配宽阔的共同基座和上方狭窄的所有塔楼。Stage 1 改为两级纯 XY 芯体计划：

1. `PodiumMain`：位于耦合后的共同裙房内，截面尽可能大且接近正方形，只贯穿裙房高度；
2. `TowerChild`：位于每个从裙房顶面继续上升的独立 WFC 分支内，截面由该分支的连续容纳范围决定；
3. 两类芯体都从真实 ground plane 开始。`TowerChild` 不是悬空放在 `PodiumMain` 上的细柱；它在裙房内与主芯体并行穿过，保留独立接地路径。两者的层级关系是 WFC 荷载区归属和后续外框路由关系，不伪冒成已证明的 Chaos 承载关系。

若没有派生的耦合裙房，或某个 SupportedSpan 要求芯体贯穿至裙房以上，则使用一个连续接地芯体，不得为强行形成层级而破坏 shared-course 端点合同。

### 19.2 高度和截面只能由语义包络与硬门派生

本合同不使用 `1000 cm`、`300 cm` 等造型常量。所有平面站位和高度均量化到 36 cm 格；为保证相邻 X/Y course 成对，裙房顶高度还量化到 72 cm 双 course。

```text
ShortestGroundBranchTop = min(original grounded branch MaxZ)
RawPodiumTop             = 0.5 * ShortestGroundBranchTop
PodiumTopCourse          = largest even 36-cm course not above RawPodiumTop
                         and leaving a complete XY pair above and below
```

该值是当前 WFC 分支高度的确定函数，不是按 Seed 试探的参数。输入太矮、无法同时容纳裙房和上部分支时，不启用层级。

芯体水平 member 的站距最多为 18 格；由于两端各伸出半个 36 cm 截面，最大实体长度是 `(18 + 1) * 36 = 684 cm`，对 720 cm 硬上限保留一格安全余量。候选截面按以下顺序唯一决定：

1. 每一层、每一根完整 rail 都被当层 WFC Body/Crown 实体并集覆盖；
2. 贯穿语义要求的全部 course；
3. 优先最大化短边，其次最小化长宽差，再最大化面积；
4. `PodiumMain` 距耦合基座的 XY 中心最近；`TowerChild` 距对应上部分支中心最近；
5. 仍并列时使用最小 Y、最小 X 作确定性 tie-break。

### 19.3 子芯体、shared course 与可追溯身份

每个 `CoreCellPlan` 公布 `HierarchyRole`、`TopCourseIndex` 和 `PodiumMainCoreCellId`。`PodiumMainCoreCellId` 只表示子芯体属于哪个共同裙房，不可被 Bearing DAG 当成悬空支座。每个芯体的 member 数是精确值 `TopCourseIndex * RailCount`。

上部分支候选从“最低面等于裙房顶面且继续向上”的原 WFC Body 开始，按语义路径去重。子芯体从地面向上做逐 course 连续覆盖检查，不使用“上部空间容得下就当作下部已有支撑”的推断。

Shared course 继续是同时替换两个端点芯体槽位的单一长 member。端点必须选择实际覆盖 shared course 高度的芯体，且在规划时已满足横向 lane 对齐和 720 cm 上限。本轮不改 shared-course 槽位替换几何；若某个 span 不能与层级芯体同时成立，该端点使用原连续接地芯体而不是降级为短桥。

### 19.4 本轮验收边界

本轮只修改 WFC 接地耦合高度、Stage 1 芯体层级计划、对应诊断身份和静态 Bearing/Load DAG。不实现 Stage 2 外伸耦合点、Stage 3 共同外框、5x6 宽矩阵、Chaos 或可见 PIE。验证顺序是：纯数据层级夹具 -> Stage 1 专项 -> ForceUnity Development Editor 编译 -> 单一固定身份 NullRHI。任一同身份失败连续两次后立即停止并记录 checkpoint，不进入参数扫描。

## 20. 交错接地复合芯体合同（2026-08-08）

本节取代第 19 节中“主芯体先固定、子芯体只单向避让主芯体”的临时实现。`PodiumMain` 与全部 `TowerChild` 仍各自从真实 ground plane 起建，但在同一 `CompositeCoreGroup` 内统一规划成一副交错 XY 晶格；它们可以在 XY 投影上部分重合，不能在同一层产生正体积重合。

### 20.1 全局层相位与轨道占位

同一建筑的绝对 course 相位唯一冻结：偶数 course 只允许 X rail，奇数 course 只允许 Y rail。每个芯体仍保持“每层只有一个方向”，不得为某个子芯体单独交换相位。规划器按稳定顺序先登记主芯体，再按 WFC 分支路径登记子芯体，但每个候选都必须读取整个复合组已经冻结的 lane reservation：

1. 两条同层 X rail 若 Y station 相同且 X 实体区间有正长度重叠，则冲突；
2. 两条同层 Y rail 若 X station 相同且 Y 实体区间有正长度重叠，则冲突；
3. 相差一个 36 cm 网格的平行 rail 只允许面接触，不算正体积冲突；
4. 后加入的子芯体必须同时检查主芯体和全部已接纳子芯体，不能只避让主芯体；
5. 候选枚举、排序和 tie-break 全部确定化，不允许换 Seed、扫间距或尝试密度来碰运气。

层级主芯体至少使用 3 条离散轨道。原因不是风格密度，而是双轨主芯体只有两条外缘轨道：子芯体避开同层外缘后，不可能再跨过主芯体内部形成双向正交承压；第三条内部轨道是“无同层重合”与“有跨芯 Bearing”能够同时成立的最小拓扑条件。实际轨数仍受来源基座数、36 cm 量化、完整覆盖与最多 5 轨硬门约束。

这是一套“统一冲突表、稳定优先级”的有界联合规划，不声称已经证明全局最优。若主芯体优先级使合法子芯体不存在，必须以稳定的 `CompositeCoreLaneUnavailable` 失败关闭，回到规划合同评审，不得调参追绿。

### 20.2 跨芯承压耦合

允许投影重合的目的不是把两副芯体画在一起，而是让相邻的正交 course 形成真实承压面。每个 `TowerChild` 与其 `PodiumMain` 在共同高度范围内必须同时具备：主 X/子 Y 交叉，以及子 X/主 Y 交叉。生产计划发射后必须从真实 member 几何重建 Bearing DAG，并记录 parent/child 的跨芯接触数；若共同高度足以包含两种界面却缺少任一方向，或跨芯接触总数为零，以 `CompositeCoreBearingMissing` 失败关闭。

跨芯接触只是 Stage 1 的静态几何与 DAG 证据，不等于 Chaos 稳定性结论。所有芯体仍需独立接地；`PodiumMainCoreCellId` 继续只是语义归属，不能被解释为悬空子芯体的虚构底座。

### 20.3 每芯体来源边界

合并组件可以同时包含高低不同的 Body/Crown 分支，因此 Body/Crown 合法性不能使用整个组件的最高 Body 层。每个 `CoreCellPlan` 冻结自己的 `BodyTopCourseIndex`：低于该值的每根 rail 必须完全被 Body 实体并集覆盖且使用 Body source；从该值起才允许使用 Crown source，同时仍须被该芯体路径的 Body/Crown 实体并集完整覆盖。

这项证据专门排除 Seam Release E6 的旧失败：较短分支已进入 Crown 时，另一较高分支仍是 Body，不得再用后者的高度否决前者。反例是把某芯体 `BodyTopCourseIndex` 之前的 source 改成 Crown，必须稳定失败。

### 20.4 Stage 1 视觉证据与停止点

原四个互斥诊断层继续保留，并新增两个只读切片：`Composite Core X Lanes` 只显示实际 X core courses，`Composite Core Y Lanes` 只显示实际 Y core courses；主芯体与子芯体使用不同材质。两个切片不增加生产阶段、不重新生成计划，也不显示 WFC、merge region、shared bridge 或其他方向构件。

本轮验收仍停在 Stage 1：设计合同 -> 每芯体来源边界夹具 -> 复合 lane/跨芯 Bearing 夹具 -> ForceUnity 编译 -> 固定 Stage 1 NullRHI。通过后交给用户在 Editor 检查全体、X-only、Y-only 三种实际构件视图；不运行 Stage 2、5x6、BeamD15、Chaos 或可见 PIE。

## 21. 不等截面芯体的 shared lane 与 720 cm 分段合同（2026-08-08）

本节取代第 17、19 节中“shared 两端必须同轨数、同横向站位，且永远由一根 member 同时替换两个槽位”的临时限制；第 20 节的交错接地、真实 Bearing 与无正体积冲突合同保持不变。SupportedSpan 不再禁止 `PodiumMain + TowerChild` 层级。每个 span 端点必须从实际覆盖全部 shared course 及其上下夹层的接地芯体中选择，并优先选择沿 opening 最近的合格芯体，不能固定取 component 的第一个芯体。

### 21.1 逻辑 shared lane

两端芯体可以有不同截面和轨数。按桥轴的横向宽度、平面面积和稳定 core id 唯一确定较细端（donor）与较粗端（receiver）；每条 donor rail 冻结为一条逻辑 `SharedCourseLanePlan`。该 lane 的横向 36 cm 实体必须完整落入 receiver 芯体的横向范围，否则以 `SharedCourseCrossLaneUnavailable` 失败关闭，不能移动芯体、改 Seed 或扩大容差追绿。

逻辑 lane 沿桥轴从两个芯体最外侧物理端面中的较小值连续覆盖到较大值，因此视觉上是“较细芯体的对应 rail 一直延长到较粗芯体的另一端”。如果 receiver 在同 course、同轴、同横向站位已有局部 rail，它与 shared lane 发生正体积冲突：必须删除该局部 rail，并让该逻辑槽位引用 shared lane 的跨芯段；若没有同站位 rail，则保留 receiver 的原有 rails，不能删除最近轨或伪造同槽位。每条 lane 记录 donor/receiver core、course、横向站位、要求覆盖区间、全部分段、唯一跨芯段和 receiver 冲突省略事实。

### 21.2 确定性 720 cm 分段

完整 lane 长度不超过 720 cm 时使用一个 member。超过上限时必须在 36 cm 端面格上确定性分段：

1. 恰有一个 `CrossCore` 中段；它的长度不超过 720 cm，并在 bridge opening 两端分别进入两个芯体至少完整 36 cm，因而确实跨越两个芯体；
2. 中段之外的负端、正端余量只位于各自芯体内，按从外向内的稳定顺序切成若干不超过 720 cm 的 tail；相邻段只能端面相接，不得重叠或留缝；
3. donor 槽位引用 `CrossCore` 段；receiver 仅在删除了同站位冲突 rail 时引用同一段。槽位只是逻辑 lane 的身份锚，不代表单个中段覆盖整个芯体；完整承压必须按同一 lane 的所有段的并集验证；
4. 上下正交 core course、bridge diaphragm 和 Bearing DAG 必须消费真实分段几何。对每个被替换槽位，lane 分段并集必须覆盖该芯体整条 rail，并共同提供原本各交点的 36×36 cm 承压；不得用元数据把无接触 tail 计为支撑；
5. opening 加两端各 36 cm 若本身已经超过 720 cm，则没有合法跨芯中段，稳定失败 `SharedCourseCrossSegmentTooLong`。不得放宽 720 cm 或把 opening-only 短桥标成 shared。

`SharedCourseCount` 从本节起表示逻辑 lane 数，另行记录物理 `SharedCourseSegmentCount`、`SharedCourseCrossCoreSegmentCount` 和 `SharedCourseConflictOmissionCount`。旧的 `ReplacementSlotCount == SharedCourseCount * 2` 不再成立；新合同要求每条 lane 恰有一个 donor 槽位，receiver 槽位数由是否真实冲突决定，且每个冲突必须一删一替。canonical Hash 必须包含 lane、分段角色、分段顺序和冲突省略身份。

### 21.3 本轮验证和停止点

验证顺序固定为：不等截面 lane 纯数据正例 -> receiver 同站位冲突删除反例 -> 超 720 cm 三段/多段正例 -> 删除中段、制造缝隙、令中段只触及单端的反例 -> ForceUnity -> 固定 `SeamRelease.E6` 与 Stage 1 staged 静态门。只证明 DAG、包络、无穿透、承压和身份合同；不运行 Stage 2、5x6、BeamD15、可见 PIE 或 Chaos。相同失败身份连续两次即 checkpoint，不改 Seed、密度、芯体截面、预算或容差重复试验。

## 22. SharedEndpoint 可达性与桥端预留合同（2026-08-09）

本节解决第 21 节中“合法跨芯中段下界已经超过 720 cm”时的唯一允许处理顺序。分段器不能把一个
必须同时进入两芯的中段切成两根悬空拼接；规划器也不能继续移动普通 `TowerChild` 猜位置。首先运行
不发射 member 的 `SharedEndpointReachability`：在固定 WFC 语义包络中按 36 cm 网格枚举最小接地桥端
cell，逐 course 检查 Body/Crown 全实体覆盖，并输出两侧 opening inset 与理论最短跨芯段。

判定只有两种结果：

1. 若不存在两端可达组合，或 `OpeningLength + NegativeInset + PositiveInset + 2*36 cm > 720 cm`，
   Beam-C3 必须停止并把问题返回 Shape Grammar/WFC 的 bridge-abutment 可行性门；
2. 若存在组合，则说明 WFC 可行而当前 core 归属错误。规划器必须生成专用、连续接地到
   `HighestSharedCourse + 2` 的 `SharedEndpoint`，不得修改 WFC、Seed、密度、预算、容差或 720 cm。

`SeamRelease.E6` 固定输入的纯数据证据为：负端最佳 inset `72.24 cm`、正端最佳 inset `30 cm`、
opening `293.76 cm`，因此理论最短跨芯段是 `468 cm`；枚举 1264 个最小 cell 的测试体耗时约
`83～105 ms`。这证明旧 `936 cm` 不是上游轮廓无解，而是选中了退后过远的普通高层子芯体。

### 22.1 生成顺序

同一 Stage 1 计划内的顺序冻结为：

```text
WFC/SupportedSpan
  -> SharedEndpointReachability
  -> 按 span/side 预留 SharedEndpoint cell
  -> PodiumMain 后的普通 TowerChild 避让预留 lane
  -> 发射所有接地芯体
  -> 按 SharedEndpointSpanVolumeId + side 精确绑定两端
  -> shared lane / 唯一 cross-core segment / diaphragm
  -> 静态 DAG
```

桥端预留必须先于普通 `TowerChild` 选址；否则普通子芯体会占用所有可行同层 lane，再把“规划顺序错误”
伪装成 `CompositeLaneUnavailable`。`SharedEndpoint` 自身已有从地面到桥层的连续承重路径，因此不强制与
`PodiumMain` 投影重叠；它仍必须无同层正体积冲突。Stage 3 的共同外框负责把同一建筑中的多个接地
芯体融合成整体，Stage 1 不得为提前冒充共同外框而强制桥端芯体穿入主芯体。

### 22.2 身份与验收

每个专用端点记录 `SharedEndpointSpanVolumeId` 和负/正侧身份。最终 endpoint selector 对精确匹配身份的
`SharedEndpoint` 具有绝对优先级；普通 `TowerChild` 即使沿桥轴更近，也不能抢占配对。每条 lane 的
唯一 `CrossCore` member 必须引用这两个 core id、真实进入两端且不超过 720 cm。

本轮通过条件只包括毫秒级 reachability、固定 `SeamRelease.E6` Stage 1、全部 Staged 静态合同和
ForceUnity Development Editor 链接。通过后停止供用户视觉检查；不运行 5×6、BeamD15、Stage 2+、
可见 PIE 或 Chaos，`PhysicalStability=NotEvaluated`。

## 23. SharedEndpoint 最小见证与生产足迹分离（2026-08-09）

第 22 节的 `1x1` cell 只回答“WFC 中是否存在一条接地、全高、可跨芯的路径”，不得再直接作为
生产芯体。Stage 1 生产桥端改为有界矩形规划：在各端固定 WFC Body/Crown 实体并集内枚举 36 cm
量化足迹，单轴最多 18 格；每个候选逐 course 验证两条交替 X/Y rail 的完整实体覆盖、接地 source、
复合 lane 冲突、opening 横向交叠和 720 cm。评分顺序冻结为：最大化两端共同短边、最小化长宽差、
最大化面积、缩短 opening inset、靠近 span 横向中心，最后按接触数和格点稳定 tie-break。

两端不是独立贪心。先处理的一端只有在另一端存在同一组横向 rail stations、且能够达到同等最小短边时
才可接纳；后处理的一端必须精确复用已冻结横向 stations。普通 `TowerChild` 继续在此后避让完整生产足迹。
这保证 shared rail 在两端具有真实同站位夹层，而不是只落入对端 AABB。

### 23.1 当前 Stage 1 的超长限制

第一次扩大固定 E6 后，252 cm 的两端 tail 各只有一个外侧 36 cm 下承座。包络和逻辑 lane 并集合同
虽然成立，但 Beam-C 静态合力分别落在单支座凸包之外；把 tail 当作“由整条 lane 元数据托住”会制造
假 DAG。因此当前生产选择还必须满足：从负端最外物理端面到正端最外物理端面的完整 shared lane
不超过 720 cm。固定 E6 因而使用一根真实 member，而不是生成单支点 tail。

这不是放弃第 21 节的通用分段格式。若以后重新启用超长 tail，必须先新增可被 Beam-C 识别的真实内侧
承座、合法端部传力模型或等价结构，并证明每段合力位于真实支座域；不得放宽
`BeamCSupportResultantOutsideHull`。在此之前，桥端尺寸是在 WFC、共同横向轨位、双方共同短边和 720 cm
完整 lane 约束下的最大值，不是全包络无条件最大值。

### 23.2 验收与停止点

固定 `SeamRelease.E6` 必须同时证明：两端 X/Y 尺寸都大于 `1x1` 见证、端点身份不变、每条 lane
仅一个物理 segment、跨芯 member 不超过 720 cm、Stage 1 静态 DAG 通过。随后只运行 staged 静态回归并
交给用户视觉检查；仍不运行 5x6、BeamD15、Stage 2+、Chaos 或可见 PIE。

## 24. 独立高层投影分区与逐区子芯体绑定（2026-08-09）

### 24.1 根因与取代关系

旧 Stage 1 虽然从耦合基座上方的多个 Body 分支收集子芯体候选，却使用
`RootPath(DerivationPath)` 去重。同一语义根下的两个独立高层投影因此可能只发射一个
`TowerChild`；已有验证又只检查“已经发射的 core 是否自洽”，没有反向证明每个高层投影都被
core 覆盖，所以会出现 WFC 轮廓中心可见而芯体层为空的静态假绿灯。

本节取代第 19、20 节中“按语义路径登记普通子芯体”的实现细节。主芯体只服务耦合基座、
全部芯体真实接地、统一交错 XY course、shared endpoint/shared course 和 720 cm 合同保持不变。

### 24.2 高层投影分区算法

1. 只对已经形成耦合基座层级的 component 建立高层投影分区；`PodiumMain` 的顶层严格冻结在
   `PodiumTopCourse`，不得沿任一高塔继续生长。
2. 在耦合基座顶面及其上方首两层观察非 `CoupledGround` 的 Body 体积。候选必须穿过基座顶面，
   且至少延续两个完整 36 cm course，短暂檐口或仅贴顶装饰不建立子芯体。
3. 将候选投影到 XY 平面。两个投影只有在具有正面积重叠，或共享一段正长度完整边时才连边；
   点接触和正宽间隙保持为不同分区。按稳定 Volume ID 排序后做确定性连通分量遍历，每个分量
   生成一个 `FHighProjectionRegionPlan`。
4. 分区身份记录 component、基座顶层、精确 source volume 集合、联合局部边界和最终绑定的
   core id。`RootPath` 不再参与分区身份或去重。
5. 每个分区必须独立生成且只生成一个接地 `TowerChild`。候选以分区 XY 中心为目标，并继续遵守
   36 cm 量化、Body/Crown 完整覆盖、统一 lane reservation、无正体积冲突和单构件 720 cm 上限。
6. 为避免某个子芯体借用相邻塔体通过，基座顶面以上的首两个子芯体 course 必须完全落在该分区
   自己的 source Body 并集中；不能只落在整个 component 或合并 AABB 中。

这一定义允许一个宽耦合基座上同时存在一个短粗 `PodiumMain` 和多个接地、较瘦高的
`TowerChild`。它没有把主芯体向上分叉，也没有用悬空子芯体冒充承重路径。

### 24.3 失败关闭与 Stage 1 验收

层级计划必须同时满足：

- 高层投影分区数等于已绑定分区数，也等于普通 `TowerChild` 数；
- 每个分区的 source 集合非空且不与另一分区重复；
- 每个分区唯一绑定同 component 的接地 `TowerChild`，region/core 双向引用一致；
- 每个 `TowerChild` 恰好属于一个分区，并指向止于同一 `PodiumTopCourse` 的 `PodiumMain`；
- 任一分区缺 core、一个 core 绑定多区、source 越界或首两层借用邻区，Stage 1 都必须失败关闭。

固定 `SeamRelease.E6` 当前静态证据为：`HighRegions=6`、`BoundHigh=6`、
`Children=6`、`Main=3`、总 `Cores=11`、`Members=1165`，Static DAG Accepted；其中另有
2 个 `SharedEndpoint`，不计入普通高层分区。该结果只证明 Stage 1 几何、身份和静态 DAG，
`PhysicalStability=NotEvaluated`。下一停止点仍是用户在 Editor 中比较 WFC 包络与
`Core + Shared Courses`；视觉批准前不进入 Stage 2+、5x6、BeamD15 或 Chaos。

## 25. 最高合法语义基座分隔与联合轨道预留（2026-08-09）

### 25.1 取代固定半高基座

本节取代第 19.2 节以“最短接地分支高度的一半”直接决定耦合基座顶面的临时规则。旧规则虽然确定，
但不对应 Shape Grammar/WFC 的形态分隔：它可能只形成一层很矮的耦合裙房，也无法表达用户在语义
包络中看到的 setback、塔身起点或裙房顶面。

新的 `SemanticPodiumPlan` 先保留旧半高结果作为可证明的下界和无合法候选时的 fallback，再从同一
语义根内所有非 Crown 体积的起始 Z 收集候选分隔。候选按高度从高到低检查，必须同时满足：

1. 高于旧半高下界，且向下量化到偶数个 36 cm course，即完整 72 cm XY 对；
2. 不越过任何关联 `SupportedSpan` 的开口下表面，不以抬高裙房填死桥下空间；
3. 不会把 Crown/屋顶吞入基座；
4. 每个被耦合的语义根在分隔上方仍有至少两个完整 course 的非 Crown Body 延续，足以生成独立
   高层投影和接地子芯体；
5. 量化后仍严格高于旧下界。若最高候选不合法，按同一列表下降到下一候选；全部不合法时才使用旧下界。

选择完成后不能只提高一个 `PodiumTopCourse` 数字。Shape Grammar 必须真实重切语义体积：删除完全位于
原始分隔以下、已经被共同基座吸收的体积；把跨分隔或位于其上的体积下界重切到量化顶面；插入从真实
ground plane 到量化顶面的 `CoupledGround`；随后重建连续 Volume ID，并把 `SupportedSpan` 两端支撑
ID 重映射到保留体积或新的共同基座。零高度体积、稀疏 ID 或无法重映射的 span 必须失败关闭。

### 25.2 主芯体必须面向全部上部投影联合选址

抬高基座后，`PodiumMain` 不再只按“最大、方正、靠近基座中心”独立选址。规划器在发射主芯体前先用
与第 24 节相同的 XY 连通规则提取分隔上方全部独立高层投影。每个主芯体候选必须在每个投影的可用
36 cm 栅格内部同时提供至少一个严格内置的 X station 和一个 Y station；这里的“严格内置”排除刚好
落在投影边界的 station，因为边界 station 会使子芯体在“同向 rail 重合”和“没有正交承压”之间二选一。

只有覆盖全部上部投影的主芯体候选，才继续按短边、方正度、面积、中心距离和稳定格点 tie-break 评分。
随后每个 `TowerChild` 仍独立检查整个复合组的 lane reservation、与主芯体的双向正交承压、首两层本区
source 覆盖和连续接地路径。普通子芯体不再承担旧的 shared-opening reachability；桥端可达性由第 22、
23 节的专用 `SharedEndpoint` 预留和身份负责。

所有网格坐标都是有符号整数，`-1` 是合法的 `-36 cm` station，不能再与 `INDEX_NONE` 共用。候选搜索
必须使用独立 `bSelected` 状态表示是否选中；任何空间坐标、course 或 lane 值都不得充当“未找到”哨兵。

### 25.3 固定身份与本轮停止点

固定 `SeamRelease.E6` 的三个耦合基座从旧半高下界提升到最高合法语义分隔：

- `Arcology/Core`：`504 -> 1152 cm`；
- `Arcology/East`：`648 -> 2448 cm`；
- `Arcology/West`：`576 -> 1368 cm`。

最终 Stage 1 为 `Volumes=25`、`Cores=11`、`Main=3`、`Children=6`、`HighRegions=6`、
`BoundHigh=6`、`PairIntents=1`、`Shared=4`、`Members=1533`，Static DAG Accepted，
`Physical=NotEvaluated`。Shape Grammar V2 静态套件 8/8、Beam-C3 V3 Staged 7/7；本轮不运行
Stage 2+、5x6、BeamD15、Chaos 或可见 PIE。下一停止点是用户在 Editor 的 WFC 包络与
`Core + Shared Courses` 两层视觉验收，重点确认高基座确实到达语义分隔、主芯体位于共同基座内，
且每个上部投影仍有独立接地子芯体。

## 26. Stage 1-only 5×6 矩阵首轮结果（2026-08-09）

视觉验收通过后新增 `ABTS.M73DAG.BeamC3V3.Staged.Stage1CoreAndSharedMatrix`。该复杂测试复用冻结的
5 Profile × 6 Tier 身份，但每个叶只调用 `GenerateStagePreview(CoreAndShared)`；它不进入共同外框、
Floor/Infill/Roof、D1 Brick 或 Chaos。每叶检查 Stage 1 身份、静态 DAG、全部 core 接地、逐高层投影
绑定、只含 Core/Shared/Diaphragm member、720 cm、无包络/void/penetration/seat 违反、Hash 与
`Physical=NotEvaluated`。这取代旧 `Stage1.Matrix` 对完整成品和已过时双端 replacement-slot 等式的
混合验收用途。

Fresh NullRHI 精确发现 30 项，结果为 11/30 通过：

| Profile | E1 | E2 | E3 | E4 | E5 | E6 |
| --- | --- | --- | --- | --- | --- | --- |
| ColumnBreak | Pass | EmptyBand | Pass | MainCore | NoCandidate | Pass |
| DropTrigger | Pass | Pass | MainCore | MainCore | MainCore | NoCandidate |
| SeamRelease | Pass | EmptyBand | MainCore | MainCore | Pass | Pass |
| SlideRelease | Pass | EmptyBand | MainCore | MainCore | MainCore | NoCandidate |
| TipOver | Pass | EmptyBand | MainCore | MainCore | MainCore | Pass |

失败分为三个确定性签名：

1. `BeamC3V3EmptyOccupiedBand`：4 格，均为 E2。Stage 1 在发射芯体前仍构造供未来 shell/floor 使用的
   band schedule，并把没有外框占用的中间带当成当前阶段失败；应先证明这些 bands 是否为 core 规划所需，
   不得通过填充 WFC 空层恢复绿灯。
2. `BeamC3V3ContinuousGroundedCoreUnavailable ... HighProjectionEntries=N`：12 格。第 25 节要求单个
   `PodiumMain` 在每个上部投影内部提供 X/Y station；当一个宽耦合基座包含 3～6 个分离投影时，单一
   最多 648 cm 足迹不总能满足。后续应在“多个 PodiumMain 组成接地基座网络”与“主/子芯体经其它
   接地芯体传递复合归属”之间先立合同；不得直接扫描轨数、缩小投影或放宽 720 cm。
3. `BeamC3StagePreviewNoSemanticCandidate:Attempts=N:Last=`：3 格。候选层丢失最后/最佳拒因，是独立
   诊断 bug。必须先保留每次 Stage 1 reject identity，再判断它是否归属前两类；`Attempts=10/12` 不能
   被解释为继续增加 Attempt 的理由。

三个代表叶已在独立 fresh 进程复现同一签名：`ColumnBreak.E2`、`DropTrigger.E3`、
`DropTrigger.E6`。因此 Stage 1 的视觉方案已经批准，但正式 30/30 静态矩阵尚未完成，不能冻结为
生产完成，也不能进入 Stage 2 或 Chaos。本轮停止在失败分类，不修改 Seed、语义基座高度、轨数、
预算、容差或物理参数。

## 27. Stage Preview 候选拒因身份（2026-08-10）

`GenerateStagePreview` 不得把循环结束时的临时 `CandidateError` 当作候选搜索的权威失败身份。
Profile 和 Shape Grammar 的失败路径会填写该字符串，但 `MeetsSemanticVisualMilestone == false` 不会；
因此全部候选均在语义里程碑被拒时，旧汇总会稳定退化为 `Last=` 空串。

Stage Preview 现在为每次候选拒绝独立记录 Attempt、CandidateSeed、Gate 和 Reason，并在最终失败中发布：

- Profile / Silhouette / SemanticMilestone 三类拒绝计数；
- 最后一次拒绝的完整身份；
- 按“违反谓词数、体积缺口、跨度缺口、屋顶缺口”确定性排序的最佳语义候选身份。

该排序只用于诊断，不改变候选选择、Attempt 上限、Seed、WFC、Stage 1 几何或验收门。自动化
`ABTS.M73DAG.BeamC3V3.Staged.StagePreviewFailureIdentity` 允许目标叶未来生成成功；若仍失败，则要求
上述身份全部非空，从而不会把当前失败固化成长期预期。

三个原 `NoSemanticCandidate` 叶的 fresh 隔离结果证明它们都尚未进入芯体规划：Profile 和
Silhouette 拒绝数均为 0，全部 Attempt 只因语义 Volume 数低于本 Tier 下限被拒；跨度和屋顶条件已满足。

| 叶 | Attempts | 最佳 Volume | 要求 Volume | 缺口 |
| --- | ---: | ---: | ---: | ---: |
| ColumnBreak E5 | 10 | 20 | 21 | 1 |
| DropTrigger E6 | 12 | 22 | 25 | 3 |
| SlideRelease E6 | 12 | 21 | 25 | 4 |

这三项不是 `EmptyOccupiedBand`、单一 `PodiumMain`、积木预算、720 cm 或静态 DAG 失败。诊断修复完成后
仍按第 26 节顺序进入 occupied-band 阶段边界调查；不得因为 Volume 缺口较小就增加 Attempt 或扫描 Seed。

## 28. Stage 1 的 band 阶段边界（2026-08-10）

`ComponentBands`、`BandBaseCourseIndices`、occupied-cell raster、外部空域 mask 和 X/Y/Z shell 槽位只服务于
`CompleteStaticDAG` 的共同外框、楼层、立柱和屋顶；`CoreAndShared` 的 `PodiumMain`、`TowerChild`、
`SharedEndpoint`、shared course 与 diaphragm 均不读取这些数据。因此 Stage 1 不得提前构造未来外框 band，更不得把
语义轮廓中刻意留空的中间高度切片解释成 `EmptyOccupiedBand`。

生产实现现在只在 `CompleteStaticDAG` 建立 band schedule。Stage 1 的组件合同明确要求
`BandBaseCourseIndices` 为空。`ColumnBreak.E2`、`SeamRelease.E2`、`SlideRelease.E2`、`TipOver.E2`
均已在独立 fresh NullRHI 进程通过，证明该修改是阶段隔离，而不是向空层填充几何。

## 29. 多 PodiumMain 接地网络合同（2026-08-10）

一个 `CoupledGround` component 可以包含多个相互分离的高层投影。单个最长 720 cm 的主芯体不再被要求同时在
所有投影内部提供 X/Y station；同一 `CoreMergeRegion` 可以拥有多个真正接地、止于同一
`PodiumTopCourse` 的 `PodiumMain`。所有主芯体共享 component、merge-region 和 composite-group 身份，但每个
`TowerChild` 必须显式绑定恰好一个主芯体，并通过实际几何重建出双向正交 bearing contact。

选择过程必须是确定性的覆盖问题：先按稳定 Volume ID 提取高层投影分区，再枚举完全位于 WFC 实体并满足 36 cm
量化、720 cm、接地 source、完整 course 覆盖和 lane 无冲突的主芯体候选。候选覆盖一个分区，当且仅当其 X、Y
stations 在该分区入口各有至少一个严格内置 station。优先使用最少主芯体覆盖全部分区；同数目下依次最大化最小
短边、方正度、面积与承压余量，最后按格点坐标稳定决胜。禁止通过放宽 720 cm、缩小投影、扫描 Seed 或增加轨数
替代这一合同。

每个 `FHighProjectionRegionPlan` 同时发布 `BoundPodiumMainCoreCellId` 与 `BoundCoreCellId`，形成
`Region -> PodiumMain -> TowerChild` 的可追溯关系。验证必须确认父主芯体属于同 component/merge/group、真实接地、
止于相同 podium seam，且子芯体的 `PodiumMainCoreCellId` 与分区反向引用一致。Stage 3 才负责用共同外框把该接地
网络视觉和结构上闭合为同一建筑；Stage 1 不生成外框，也不以悬空传力冒充归属。

## 30. 高 Tier 宏观里程碑与加权细节的阶段边界（2026-08-10）

Shape Grammar 的 `ComplexityMilestoneTier` 只负责保证可读的宏观形态：高 Tier 的深度 0 水平分裂和深度 1
setback 完成后，后续深度必须回到既有的 Stack/Setback/SplitX/SplitY 加权规则。旧实现却在 milestone 分支末尾对
所有剩余深度无条件返回 `Stack`，使 72 个 `TargetVolumeCount` 软叶预算退化成每个 root 的线性堆叠；无论增加多少
Candidate Attempt，最终体积数都受 root 数与 grammar depth 限制，稳定停在 18～22，而 E5/E6 静态语义门要求
21/25。

修复不降低 `MinimumVolumes`，也不提高 Attempt、扫描 Seed、改变 36 cm 积木、WFC 尺寸、720 cm 或物理参数。
Tier 0/1 仍在深度 0 强制一个可读的基础 stack；Tier 2+ 完成显式 macro step 后进入原加权细节。
`BridgedArcology` 是例外：它的连续上层层级决定 supported-span 两端的高度、inset 和可预留 lane，且当前
`SeamRelease.E6` 已通过视觉和 shared-course 合同；因此桥接原型在显式 split/setback 后继续保留线性 stack，不因
无关 Profile 的 volume 缺口改变桥端拓扑。验证顺序为：先只跑原三个语义失败叶，再复核已视觉批准的
`SeamRelease.E6` 和多主芯体夹具；只有这些边界通过后才允许再次运行一次 Stage 1-only 5×6 矩阵。

## 31. Stage 1 裙房分隔与芯体空间覆盖诊断合同（2026-08-10）

静态 DAG、`HighRegions == BoundHigh == TowerChildren` 和全部芯体接地只能证明已经声明的传力身份闭合，不能证明
耦合基座选择了视觉上最高的合法分隔，也不能证明芯体在宽基座内分布合理。Stage 1 因此增加两组不改变几何的
结构化诊断。

语义裙房选择必须逐候选发布 `Scope / LegacyTopZ / SemanticSeamZ / QuantizedTopZ /
ProtectedSpanTopZ / Accepted / RejectReason`。每个 applied root plan 及只读的
`AllCoupledRootsCommonProbe` 都发布 selection 汇总；候选数必须闭合，拒绝项必须有首个 fail-closed 原因，使用
semantic seam 时必须恰有一个最高候选被接受。common probe 只回答“所有已耦合 root 共同使用一个 seam 时会怎样”，
不修改当前逐 root 几何。

每个 `CoreMergeRegion` 同时以 36 cm 网格对精确 `GroundSourceBounds` 并集做覆盖审计，分别记录
`PodiumMain` 覆盖、全部接地芯体覆盖、未覆盖格数、最大无芯体半径，以及基座质心到最近芯体矩形的距离。首轮硬门
只要求一 region 一诊断、总格数恒等式闭合且数值有限；覆盖率和距离先作为视觉质量观测项，不在看完 5×6 分布前
拍脑袋冻结阈值，也不以失败重采样 Seed。

fresh Stage 1-only 5×6 仍为 30/30、静态 DAG 全通过，但新指标证明旧绿灯不足：

| E6 | Main 覆盖 | 全芯体覆盖 | 未覆盖格 | 最大空洞半径 | 质心间隙 |
| --- | ---: | ---: | ---: | ---: | ---: |
| ColumnBreak | 43.2% | 51.2% | 606 | 234.691 cm | 0 cm |
| DropTrigger | 16.731% | 29.4402% | 2193 | 572.049 cm | 180 cm |
| SeamRelease | 36.8182% | 39.4318% | 1176 | 381.838 cm | 0 cm |
| SlideRelease | 23.2425% | 31.4204% | 1912 | 587.694 cm | 296.864 cm |
| TipOver | 38.5714% | 43.3333% | 952 | 330.926 cm | 108 cm |

`DropTrigger.E6` 的 7 个高层分区全部绑定成功，问题却仍然存在，说明根因不再是漏发 `TowerChild`。当前
set-cover 只把“覆盖所有高层投影且主芯体数量最少”作为组合目标；面积、最大空洞和基座中心没有进入目标函数，
所以两个合法主芯体可以集中在投影入口附近而留下大面积中空。后续修复应先把基座网格或连通支承需求点加入有限
覆盖合同，再决定是否允许在预算内增加 main；不得直接调轨距、Seed、Attempt、容差或 720 cm。

`ColumnBreak.E6` 的 Left、Right 和 common probe 均为 `Candidates=0`，并分别回退到 720/504 cm。该结果不是
“更高候选被 supported-span 或 Crown 明确拒绝”，而是当前候选枚举只读取非 Crown volume 的 `Min.Z`，截图中的
更高可见分隔没有以符合该谓词的上部 Body 起始面进入候选集合。下一步必须先把可见分隔的 WFC 边界身份分类为
Body 起点、下部体积终点、截面变化面或 Crown 边界，再扩展候选合同；禁止直接抬高固定厘米数，或删除 Crown/span
保护来制造候选。

本节结果把 Stage 1 状态修正为：生成与静态身份矩阵完成，视觉空间合同尚未达标。仍不进入 Stage 2 或 Chaos。

## 32. Crown 分隔见证与耦合基座支撑锚点（2026-08-10）

第 31 节的零候选并不是 ColumnBreak 没有更高形态分隔，而是可见分隔在该候选中表现为 `Crown.Min.Z`。
候选枚举不得继续排除 Crown 起点，也不得把整个 Crown 吞进裙房。现在每个高于 legacy top 的稳定 volume 起点都可
成为语义边界见证；若同一高度只有 Crown 起始而没有 Body 起始，则把它标记为 `StartsCrown`，裙房顶量化到该面下方
至少两个 36 cm Body course。重切只发生到量化后的裙房顶，Crown 本身保持原边界和 source 身份。候选仍逐项受
incident supported-span 下表面、较低 Crown 已开始、每个 root 至少两层 Body 延续约束，按高度从高到低选择首个
合法项；不能把“最高可见面”直接等同于“最高合法裙房面”。

固定 `ColumnBreak.E6` 由零候选回退改为明确选择：Left 的 seam/top 为 `1447.190/1368 cm`，Right 为
`1126.436/1008 cm`；更高面分别以 `AboveIncidentSupportedSpanUnderside` 或 `ConsumesCrown:<Root>` 明确拒绝。
`ColumnBreak.E5` 为 Left `1495.333/1368 cm`、Right `1301.332/1224 cm`。`SeamRelease.E6` 与共享桥隔离叶保持
Static DAG Accepted。诊断必须同时发布 `StartsCrown`，以后不允许 Crown 边界再次静默消失。

宽耦合基座的 main set-cover 还增加一个不依赖 Seed 或经验阈值的空间合同。规划器以精确
`GroundSourceBounds` 并集建立 36 cm 占用栅格，先求占用格质心，再选择离质心最近、且实际位于基座内的格心作为
`PodiumSupportAnchor`；因此 L/U 形或带保留空洞的基座不会把锚点放在虚空中。覆盖所有高层投影仍是硬门，最少 main
仍是第一数量目标；同数量组合优先选择覆盖支撑锚点者。若没有任何最小组合覆盖锚点，只允许增加一个与现有 main
无同轴 lane 冲突、完整接地且 course 全包络的 main。不存在这种候选时以
`BeamC3V3PodiumSupportAnchorCoreUnavailable` 失败关闭，禁止改 Seed、Attempt、轨距、容差或 720 cm 继续搜索。

支撑锚点只解决“共同基座中央没有主芯体”的确定失败模式，不把覆盖率、最大空洞或 Chaos 稳定性伪装成已完成。
每个 coverage diagnostic 发布锚点坐标与 `AnchorCovered`；有 `PodiumMain` 的 region 必须为真，计数汇总必须为零。
`DropTrigger.E6` 因此从 2 main/9 cores/922 members 变为 3 main/10 cores/1060 members，全芯体覆盖由
29.4402% 提升到 34.8456%，最大空洞由 572.049 降到 494.263 cm，质心间隙由 180 降到 0；这仍是待视觉判断的
稀疏程度，不是 Chaos 结论。`SlideRelease.E6` 同时从 31.4204%/587.694 cm/296.864 cm 改善为
38.3070%/334.819 cm/0 cm，证明合同也处理了未截图的同类风险。

验证顺序严格为 ColumnBreak E6 单叶、ColumnBreak E5 与 SeamRelease E6 边界回归、DropTrigger E6、三投影宽基座
夹具，最后一次完整 Stage 1-only 5×6。最终矩阵 30/30，首叶到末叶约 396.3 秒；全部
`StaticDAG=Accepted`、`Physical=NotEvaluated`。ForceUnity Development Editor 全链接成功。本轮没有运行 Stage 2+、
BeamD15、Chaos、Editor 或可见 PIE；Stage 1 仍需用户按 WFC/Placement/Core 三诊断层复核新几何。

## 33. Stage 1 空间覆盖与高层持续绑定诊断（2026-08-10）

`HighProjectionRegions == BoundHighProjectionRegions == TowerChild` 只能证明裙房顶面入口处存在一一绑定，不能证明主芯体在耦合基座上分布合理，也不能证明子芯体沿对应 WFC 高层分支持续到顶。Stage 1 因此增加下列只读诊断；这些数据不得参与候选排序、Seed、预算、720 cm、容差或物理参数：

1. 每个 `GroundSourceVolume` 发布原始接地组件身份、36 cm 栅格总数、PodiumMain/任意芯体覆盖数和未覆盖数；若 Shape Grammar 已把多个来源折叠为单个 `CoupledGround/Cell`，诊断必须如实显示来源身份已收敛，不能伪造上游子来源。
2. 任意芯体未覆盖格按四邻接形成岛，记录格数、XY 边界和接触 `-X/+X/-Y/+Y` 的方向掩码。
3. 发布 PodiumMain 联合边界相对 WFC 基座四边的缩进、每个 main 覆盖的高层分区/接地来源/基座格、support anchor 身份，以及每对 main 的投影重叠面积。
4. 每个高层入口 seed 发布 Volume ID、路径、XYZ bounds 和 region；每对 seed 发布 XY 邻接判据与 Z 重叠，避免把“同高”误当成“相连”。
5. 从 `PodiumTopCourse` 起逐个 36 cm course 重建 XY 连通分区，记录 source 集合、bounds、分叉账本和仍能到达该高度的 TowerChild。终端分支若没有子芯体继续到达，必须显式发布空绑定，而不能由入口绿灯掩盖。

### 29.1 TipOver E6 定点结论

固定 `TipOver.E6` 的 fresh NullRHI 结果仍为 Static DAG Accepted、Physical NotEvaluated。其耦合基座为单个 `CoupledGround/Cell/0`，XY 范围 `[-1066.24,1066.24] x [-501.84,501.84] cm`，顶面 `1440 cm`。裙房顶面已有 7 个互不相连的高层 seed，分别绑定 7 个接地 TowerChild；逐层账本没有出现“一入口向上分成三柱”的 split。因此本例的高柱视觉缺口不是入口 region 合并错误。

真实缺口是持续高度合同：西侧中央高柱的 child 3 固定 footprint 为 `X=-360..-108, Y=-144..108 cm`，只生成到 course 90 / `3240 cm`；WFC 同一分支持续经过 Crown volumes 3/4/5 到 `3876 cm`。从 course 90 起该高层切片的 child binding 为空。东侧中央 child 4 到 course 107 / `3852 cm`，只剩 `24 cm` 非完整尾层未覆盖。当前 child selector 选择一个从地面到顶部保持不变的矩形 footprint，并在每层要求完整 X/Y rail 被 Body/Crown 覆盖；同时 composite lane conflict 禁止复用已被 PodiumMain 占用的同向 station。西支继续进入收缩 Crown 所需的内移 station 已被相邻 main 占用，所以所有候选都在 Crown 起点前终止。现行合同只检查 region 在入口绑定一个 child，没有检查 branch terminal 的持续绑定。

### 29.2 待解决：PodiumMain 耦合与空间分布

TipOver E6 选出 3 个 `648 x 648 cm` PodiumMain。联合边界相对基座 `-X/+X/-Y/+Y` 仍缩进 `130.24/166.24/105.84/177.84 cm`；总计 1680 个基座格中，main 覆盖 921 格，任意芯体覆盖 951 格，729 格未覆盖并形成一个接触四边的连通岛。main 0/1 投影重叠 `44064 cm^2`，main 1/2 重叠 `22032 cm^2`。

该现象登记为待解决的“主芯体耦合/均匀覆盖”问题。当前 set-cover 的目标依次是最少 main 数、覆盖全部入口高层投影、同数时覆盖 support anchor，再按单候选尺寸和距离稳定排序；它没有最小化 main 重叠、最大未覆盖岛、四向缩进或覆盖离散度。后续修复必须先建立有限、可反证的空间分布目标，并同时保护 TowerChild 的全高可用 lane；不得以扩大 Attempt、扫描 Seed、调轨距、放宽 720 cm 或放宽几何容差代替。

下一步最小顺序冻结为：先为“每个最终高层终端分支均有接地子芯体持续到其最高完整 course”建立观察/失败合同，再设计 PodiumMain 联合覆盖与重叠目标；只跑 `TipOver.E6` 和最小夹具。两者成立后才回到 5x6。Stage 2、BeamD15、Chaos 和可见 PIE 均不在本轮证据范围。

## 34. 固定 footprint 的 PodiumMain / TowerChild 联合选择（2026-08-10）

本轮不再先冻结 PodiumMain、发射其 members，再让 TowerChild 在剩余 lane 中碰运气。规划顺序改为：

1. 以每个高层投影 region 的 WFC Body/Crown 包络为权威输入，在 36 cm 整数格上枚举接地、固定 footprint 的
   TowerChild 见证；逐 course 交替验证完整 X/Y rails，求该 region 在当前固定-footprint 合同下可持续到达的最高
   完整 course，只保留达到该高度的候选。此处不发射任何 member，也不让 PodiumMain 先占 lane。
2. 每个 PodiumMain 候选除入口 coverage mask 外，再发布 full-height compatibility mask：只有存在一个全高 child
   footprint 与该 main 无同向 station 冲突、并能形成双向正交接触时，才可声明支持对应 region。候选保留键由单独
   coverage 改为 `coverage + compatibility`，避免视觉较大的 main 把关键 child lane 提前裁掉。
3. 用有界、确定性的 set-cover 搜索 main 组合。硬门依次为：覆盖全部 full-height region、main 之间无 composite
   lane 冲突；数量目标仍优先最少 main，同数时优先覆盖 podium support anchor，再按稳定候选顺序决胜。完整组合
   为空时以 `BeamC3V3JointCoreSelectionUnavailable` 失败关闭。
4. 冻结所选 main 后，只从第一步保留的全高 child 集合中落地 TowerChild；候选仍须通过所有 selected main、已选
   sibling、shared endpoint reservation 和双向 main coupling 的最终检查。若组合在这些后置约束下失效，以
   `BeamC3V3FullHeightChildJointSelectionUnavailable` 失败关闭，不允许退回较矮 child。第一轮仍禁止分段或逐层漂移
   footprint；后续若必须引入可变 footprint，应作为新的结构合同和独立视觉阶段，而不是隐藏回退。

新增 `FFullHeightChildCandidateDiagnostic`，逐 region 发布枚举数、非法格点、接地来源、WFC 包络、全高见证、
main lane、main coupling、sibling lane、shared reservation、最终 joint-feasible 数、所选 main/child bounds 和
明确原因。新增 `FJointCoreSelectionDiagnostic`，逐 component 发布 region 数、main 候选数、零 full-height
compatibility 的 main 数、访问的完整 main 组合数、可行组合数、所选 main 数和最终原因。测试必须验证诊断计数与
region/component 身份闭合，并直接断言每个 terminal branch 都有 `TopCourseIndex >= terminal SliceCourse` 的接地
TowerChild；不能再用 `High == Bound == Children` 代替全高合同。

固定 TipOver E6 的结果证明该顺序改变了权威几何，而非只增加日志：803 个 retained main 候选中有 252 个对所有
全高 child 均无兼容性；搜索访问 21,600 个完整可行 main 组合并选出 3 个 main。西中央 child 从旧的 course 90 /
3240 cm 扩展到 required course 107 / 3852 cm，东中央 child 同为 course 107；其余五个 region 分别达到各自
required course 50/50/60/60/60。纯数据夹具还固定“存在零兼容 main，但 child-first compatibility 会迫使选择另一组
main”的反例，防止以后恢复 main-first 路径。

证据限定为 Development Editor 编译、fresh 纯数据夹具与 fresh `TipOver.E6` Static DAG；Physical 仍为
NotEvaluated。本轮没有运行 5×6、Stage 2+、BeamD15、Chaos 或可见 PIE。第 33.2 节登记的 main 重叠与基座均匀覆盖
仍是独立待解决问题；本轮只保证全高 child lane 不再被 main-first 选择静默消费。

## 35. 终端需求全集与空间主芯体/全高子芯体联合选择（2026-08-11）

本节覆盖第 34 节中“每个 TowerChild 必须与某个 PodiumMain 形成直接双向正交接触”的过强前提，但不修改其历史取证。
`TipOver.E6/710000` 证明：如果先由已有 PodiumMain 推导高层 region，再从这些 region 选择 child，未生成 main 的区域就不会
产生 child demand，而缺少 child demand 又会让 main set-cover 认为该区域没有义务，形成闭环自证。另一个反例
`DropTrigger.E5` 中，一个合法终端 region 对所有 retained main 的直接接触候选数为零；若仍把直接接触当作存在门，搜索在根
状态即失败，尽管该 child 本身可以独立接地、通顶并在后续 Stage 2 通过外框建立建筑级耦合。

Stage 1 的权威顺序冻结为：

1. 从 WFC 的 Body/Crown course slice DAG 直接建立 terminal demand 全集。每个最高完整 course 的叶分支都是独立需求；该集合
   在生成 PodiumMain 之前完成，不能由 main coverage、入口 region 或已生成 child 反推。
2. 在尚未发射任何 member 时，为每个 terminal demand 枚举接地、固定 footprint、逐 course 完整 X/Y 交替的全高
   TowerChild 候选。候选必须位于对应 WFC 包络内、达到 terminal course，且分别记录 WFC、接地、main lane、sibling lane、
   shared reservation 与最终选择拒因。
3. 独立枚举 PodiumMain 候选，并用其 `CoverageMask` 表示 main rails 穿过哪些裙房入口 footprint。用有界 set-cover 选择可覆盖
   全部空间入口需求、main 间无同向 lane 冲突的组合；同数时继续使用支撑锚点和稳定几何顺序决胜。
4. 对每个空间 main 组合进行精确 child assignment：每个 terminal demand 必须选择一个独立接地、全高、与全部 selected main、
   已选 sibling 和 shared reservation 无 member/lane 冲突的 child。直接 main/child bearing 是优先项而不是存在前提；若没有
   直接接触，Stage 1 记录最近 PodiumMain 为归属身份，但不得伪造 bearing。实际存在的正交接触仍须在至少三个共同 course 上
   双向闭合。
5. main/child 联合选择成功后，才预计算并预留精确 shared endpoint，随后按已冻结几何发射 PodiumMain、TowerChild 和 shared
   course。任何终端 demand 未绑定、局部/全局身份不闭合或最终发射偏离预留均 fail closed。

诊断必须同时发布 terminal demand 总数、逐 demand 稳定局部 ID、全高候选数和拒因；逐 main 候选的空间覆盖 mask；搜索访问状态、
最大部分覆盖 mask、各 region 空间 main 候选数、最终 main 集；以及每个 child 的直接接触或最近归属。诊断数组下标不得在从局部
component 汇入全局 summary 时原地改写；`LocalProjectionIndex` 与全局 region index 分开保存，以免不同 component 的记录互相覆盖。

搜索保持有界且可复现：对等价 main 选择状态做规范化去重，对 main 组合的精确 child feasibility 缓存结果，并优先分配候选数最少
的 terminal demand。单叶运行达到十分钟而没有新阶段证据时必须停止并先补阶段计时/剪枝，不允许改为扫描 Seed、增加 Attempt、
放宽 720 cm、轨距或几何容差。该优化把代表性 `TipOver.E6/730000` 从超过十分钟未完成降至约 118 秒，并使最终各 Profile 的六档
矩阵在约 81--169 秒内完成。

最终静态证据为：固定 `TipOver.E6/710000` 得到 `Main=3`、`Children=8`、`TerminalRequired=8`、`TerminalBound=8`、
`StaticDAG=Accepted`；五个 Profile 各 6/6，合计 30/30。所有叶仍为 `Physical=NotEvaluated`，没有运行 Stage 2、BeamD15、
Chaos 或可见 PIE。第 33.2 节和 M7-BC-064 的 PodiumMain 重叠、四向缩进与均匀覆盖仍是独立视觉质量问题，不得把本节的全高闭合
外推为该问题已解决。
