# M7 评委演示六栋垂直切片冲刺设计

> 状态：2026-08-15；Stage 1～5.5 静态生产与装置装配已完成。进入完整建筑 Chaos 前，先执行
> [JuryDemo 静态封口与 Fixed-Six V2 交接](M7JuryDemoStaticSealAndContractV2Handoff.md)，固定跨工作树
> 顺序并验证最终静态 Bounds/Pad；Chaos 仍为 `NotEvaluated`。
>
> 上游：[Beam-C3 V3 骨架优先生成](M73BeamC3V3SkeletonFirstBuildingGenerationDesign.md)
>
> 排错账本：[M7WorktreeTroubleshooting.md](M7WorktreeTroubleshooting.md)

## 1. 目标与明确删减

DDL 为 2026-08-19。M7 建筑冲刺只交付一条可由评委复现的六档垂直切片，不再把“任意 Profile × 任意
Tier × 任意 Seed 均可生成”作为本轮发布前置条件。六栋必须分别展示 E1～E6，合计覆盖五种既有视觉
Profile；每栋绑定固定 Profile、Tier、Base Seed、WFC/阶段 Hash，运行时禁止扫描或静默换种子。

本轮保留：Beam + 分层芯体、真实全建筑 Brick、六档复杂度、单栋按需 Chaos、炸药桶与活塞装配。
本轮放弃：弱点/Failure Frontier、绳索、全 Seed 可行性、5×6 Chaos、六栋同时全动态、任意形体的
完美 Stage 4。删减只缩小输入域，不允许放宽 36 cm 截面、720 cm 上限、无正体积穿透、真实接触、
向地可达和 fail-closed 合同。

## 2. 演示六栋清单 v1

| 条目 | 档位 | Profile | Base Seed | 视觉职责 |
| --- | --- | --- | ---: | --- |
| `DemoE1ColumnBreak` | E1 | `ColumnBreak` | 710000 | 最低复杂度、低预算清晰轮廓 |
| `DemoE2DropTrigger` | E2 | `DropTrigger` | 740000 | 低中档体量与悬置语义 |
| `DemoE3SlideRelease` | E3 | `SlideRelease` | 750137 | 横向退台/滑移风格差异 |
| `DemoE4TipOver` | E4 | `TipOver` | 730000 | 高耸芯体与复杂度过渡 |
| `DemoE5SeamRelease` | E5 | `SeamRelease` | 720000 | 双芯/shared-course/桥型代表 |
| `DemoE6TipOver` | E6 | `TipOver` | 750000 | 已反复诊断的最高复杂度代表 |

清单版本固定为 `ManifestVersion=1`。它是演示输入白名单，不是候选搜索结果；条目失败时必须停止并修复
该条目，不能自动改 Profile、Tier 或 Seed。五种 Profile 均至少出现一次，TipOver 复用在 E4/E6 是为了
保留已验收的高层复杂形态并降低冲刺风险。

## 3. 12 小时 M7 冲刺排期

| 时间盒 | 交付 | 硬停止点 |
| --- | --- | --- |
| 0～0.5 h | 冻结清单、Manifest/Hash、六栋 Stage 0/3 视觉 | 任一条目身份不稳定即停止 |
| 0.5～4.5 h | 最小 Stage 4：Floor/Infill/Roof 与顶面闭合 | 单一失败身份 30 分钟未定位，先补诊断 |
| 4.5～6.5 h | 新 Stage 5：完整积木 Bearing/Load DAG 与 D1 一对一绑定 | 禁止回退 legacy StaticDAG 假绿 |
| 6.5～8 h | 炸药桶/活塞使用确定性语义槽位装配 | 自动布局不收敛则固定合法槽位 |
| 8～11 h | 单栋激活 Chaos；其余建筑保持静态 HISM | 同一结构修复两次仍失败，降高度或换清单版本 |
| 11～13 h | 六栋串行回归、生成时间与内存/Actor 数验证 | 单栋生成目标约 10 s，上限 60 s |
| 13～14 h | 缓冲、文档、精确 SHA 交接 | 不新增范围 |

目标是 12 小时完成，14 小时是风险上限。任何阶段超出时间盒不得通过无界调参、Seed 扫描或重复同一
失败方案追回进度；应保留首个失败证据，缩小形体或切换到新的显式 Manifest 版本。

## 4. 生成与运行时性能边界

1. 编辑器/静态生成只运行当前选中的一个固定条目；六栋批量验证串行执行并逐叶记录时间。
2. 游戏中只有当前被攻击的建筑转换为真实动态 Brick/Chaos；其余五栋保留静态 HISM 表现。
3. 禁止一次性把六栋数千个积木 Actor 全部提升为动态刚体。
4. Stage 4/5 每增加一个阶段都记录独立耗时，不能把进程启动或资产加载误归因到算法。
5. 单栋超过 10 秒先剖析；超过 60 秒直接拒收。最终评委路径不得运行 5×6 或任意 Seed 搜索。

## 5. 当前视觉冻结门

用户在 `PlanarPhysicsTestMap` 中摆放六个 `ABTSM73BeamD1PreviewActor`，每个 Actor 从
`Demo Six-Building Entry` 下拉框选择唯一条目。每栋分别验收：

1. `Generation Stop Stage = Stage 0`、`Stage 1 Diagnostic Layer = WFC Semantic Envelope`：轮廓完整、
   风格/档位可区分、保留洞口与桥语义；
2. `Generation Stop Stage = Stage 3`、`Stage 3 Diagnostic Layer = Stage 1 / 2 / 3 Overview`：芯体、耦合
   构件、共同外框和外柱因果关系正确，没有大片缺框、孤立分体或明显向内生长；
3. Output Log 出现 `DemoManifestApplied`，其 Entry/Profile/Tier/Seed 与本表一致；
4. 当前只批准 DAG/视觉，`Physical=NotEvaluated`。六栋全部批准后清单 v1 冻结，再进入 Stage 4。

## 6. 自动化合同

- `ABTS.M73DAG.BeamC3V3.Demo.SixBuildingManifest`：恰好六项、E1～E6 完整、五 Profile 覆盖、身份唯一；
- `ABTS.M73DAG.BeamC3V3.Demo.Stage3FrozenEntries`：六个独立叶分别生成 Stage 3，要求静态 DAG、无穿透、
  每个锚带有外框、每个外框有向下路径，并打印 Manifest/WFC/Stage3 Hash；
- 本门不证明 Stage 4、完整 D1、Chaos、装置行为或生产 M3→M7 接入。

## 7. Stage 4：完整视觉建筑的最小实现顺序

Stage 4 只消费已冻结的 `ResolvedFacadeEnvelope`、Stage 1 芯体、Stage 2 耦合锚带和 Stage 3 共同外框，
不得重新选择 Seed、重算芯体或绕过 Stage 2/3 因果链。它的完成定义是六栋演示建筑形成可视觉交付的
Floor/Infill/Roof 全积木几何；完整积木承重 DAG 仍由 Stage 5 负责，Chaos 仍为 `NotEvaluated`。

按以下可停止点依次实现，每个停点视觉/静态合同通过后才进入下一项：

1. **TopSurface 归属账本**：逐个 Stage 3 facade/frame 判定最终向下意图。真正建筑外周绑定
   `GroundSill`；内侧、退台和落在裙房/楼体顶面上的局部立面绑定唯一 `TopSurface`。发布来源 WFC
   顶面、XY 区间、course、目标 frame、归属原因和稳定 Hash；两类归属互斥且必须完整，先只诊断，
   不生成 Floor/Roof；
2. **顶面/楼面边框**：沿已登记 TopSurface 的真实 WFC 轮廓生成可承托的 X/Y 边框，并复用已有
   core/frame member；不得填入 ProtectedVoid，也不得用全局 AABB 封平凹口；
3. **Facade-to-Top 闭合**：把 M7-BC-095 中内侧、退台和缺少下向柱的外框接到对应顶面边框，替换
   Stage 3 临时的无意义接地长柱；要求真实 `36 x 36 cm` 接触、无正体积穿透及向既有骨架可达；
4. **Floor / StyleInfill**：只在已认证水平面和相邻支座之间补楼面及风格填充。低 Tier 优先稀疏，
   E5/E6 才使用剩余预算提高密度；装饰不得抢占芯体、外框、主楼面或屋顶预算；
5. **Roof / Crown**：按固定 WFC Box/Prism/Pyramid 语义闭合屋顶。每根 roof course 必须消费真实
   `RoofSeat`，遵守 36 cm 量化、720 cm 上限和 ProtectedVoid；禁止 rescue post 或悬空装饰；
6. **Stage 4 总览与冻结**：提供互斥的 `TopSurface Intent`、`Floor / Top Frames`、
   `Facade-to-Top Connections`、`Infill`、`Roof` 和 `Stage 1～4 Overview`。只跑演示六栋，检查归属完整、
   无穿透、局部静态座面、稳定 Stage4 Hash 和独立耗时；用户视觉批准后才进入 Stage 5。

止损规则保持不变：同一失败身份 30 分钟内不能定位时先补拒绝账本；不得扩大候选次数、扫描 Seed、
放宽 720 cm/36 cm/接触合同或借用 legacy complete-production 凑绿。

### 7.1 TopSurface 账本首停点状态（2026-08-13）

已实现只读三态账本和独立诊断层，且不发射任何 Stage 4 brick。`TopSurface` 已拆成明确的
`ExposedSetbackTop` 与 `DirectStackSeat`：前者覆盖裙房、肩部及任意高层回退的外露顶面，后者只处理
没有外露肩部的真实齐边叠置接缝。两者都必须绑定最终 WFC/抬高外壳中的来源 Volume、量化 course 和
至少 36 cm 支撑区间。六栋固定清单现在均 `Unresolved=0`；E6 为 `Ground=10 / Top=24`，其中
`Setback=19 / Stack=5`。静态首停点已闭合，等待用户视觉批准后进入第 2 步发射顶面边框。

### 7.2 顶面/楼面边框停点状态（2026-08-13）

TopSurface 视觉验收通过后，第二停点已进入实现：逐个已解析 TopSurface 将真实切向支撑区间栅格化为
36 cm 边框单元，按外露肩部/齐边叠置语义分别选择 facade 外侧/内侧半格，并在不跨凹口的前提下合并为
不超过 648 cm 的水平砖。已有同层水平构件登记为物理复用；与 Stage 3 临时外柱相交的单元登记为下一停点
必须替换的 facade junction，顶框在其两侧分段，禁止重复穿透或把 Z 柱冒充水平框。ProtectedVoid、部分碰撞和
无账本来源继续失败关闭。独立 `Floor / Top Frames` 层以钢色显示新增段、玻璃色显示水平复用节点、石材色显示
待替换临时柱。当前六栋静态门 6/6、预览合同 1/1；E3 有 1 个显式待替换 junction，其他五栋为 0。
现交由用户视觉批准，批准前不进入 Facade-to-Top 闭合。

### 7.3 Roof / Crown 与总览冻结（2026-08-15）

Floor / StyleInfill 已通过用户视觉验收。Roof / Crown 最终不再拟合 WFC Crown 高度，而只消费其
Pyramid/TriangularPrism 语义，在真实 Crown 基座上生成逐层相邻的确定性 36 cm 阶梯屋顶。X/Y course
严格交替；Pyramid 在相应方向每次从两侧各内缩一格，Prism 只收缩 taper 轴。奇数宽度终止于 1，偶数
宽度终止于 2，禁止 `2 -> 1`，从而避免 18 cm 半格相位。`RoofPost`、deferred、occluded、unsupported
均必须为零。互斥诊断层为：

- `5 - Roof / Crown`：玻璃色确定性 voxel course，铁色 RoofPost（冻结结果必须为零），石材色其他 Stage 4 构件；
- `6 - Stage 1 / 2 / 3 / 4 Overview`：木/玻璃/铁/石材分别表示 Stage 1/2/3/4。

旧梳齿、逐 course 百叶、稀疏门架和居中 `2 -> 1` 均保留为失败基线，不再重复。最终候选已通过 UE 5.8
ForceUnity Development Editor 全链接及 fresh 固定演示六栋 6/6；自动化逐屋顶验证相邻 course、X/Y
交替、footprint 奇偶性、最终宽度、36 cm 单位化以及零 RoofPost/deferred/occluded/unsupported。证据为
`M7-Stage4-CrownVoxel-Parity-Demo6-20260815.log`。用户随后完成 `Roof / Crown` 与 Stage 1～4 总览视觉
验收，因此 Stage 4 正式冻结。冻结不包含生产积木承重 DAG、Beam-C 合力门或 Chaos；这些从 Stage 5
开始实施。

### 7.4 Stage 4.5 放置描述冻结（2026-08-15）

Stage 4 视觉冻结后新增一个不扩大动态完成定义的交接点：从六栋真实 Stage 4 active 静态 member 提取
Bounds、XY 占地、原点 Pivot、`Z=0` 地面、方向与 36 cm Pad 安全边，并发布只读目录。提取时排除已经由
Facade-to-Top 替换的 suppressed Stage 3 临时柱，对 active AABB 全体执行正体积相交检查和落地检查；排序
AABB/结构行分别形成几何与结构 Hash，因此不是手写六组尺寸，也不依赖 member 数组索引。

统一三轴 36 cm 网格修正后，冻结身份为 Schema `1`、Manifest Version `1`、Manifest Hash `2324068295`、
Catalog Hash `11501529584318250152`。过滤器 `ABTS.M73DAG.BeamC3V3.Demo.Stage45PlacementFreeze` 重新生成六栋并逐字段
反验提交目录；完整字段与集成使用约定见
[Stage 4.5 建筑放置冻结设计](M73BeamStage45PlacementFreezeDesign.md)。本门通过后 M3 可只等待集成工作树
消费该目录，不再等待 M7 的 Stage 5/Chaos/破坏/弱点/六栋动态并发/完整 PIE；这些未完成项的证据身份保持
`NotEvaluated`，不得由 Stage 4.5 推断。

## 8. Stage 5：生产积木与承重 DAG

详细生成顺序、fail-closed 合同和第一停点验收见
[M73BeamStage5ProductionDAGDesign.md](M73BeamStage5ProductionDAGDesign.md)。Stage 5 只消费冻结 Stage 4 的
active member：先过滤并压缩 suppression，再重建实际 BearingContacts，运行只读 Load DAG，最后形成
`BrickId == MemberId == LoadNodeId` 的一对一绑定。禁止回到旧 `CompleteStaticDAG`、禁止结构失败后换 Seed、
禁止由 Beam-C 插入修复柱改变已批准外观。

验证仍按 E1 单栋首停，再跑固定六栋；不恢复 5×6 或全 Seed 门。Stage 5 已在修正后的统一三轴
36 cm 格上固定六栋 6/6。Chaos、弱点和绳索保持 `NotEvaluated`；炸药桶/活塞进入独立 Stage 5.5，
不得反向改写 Stage 5 建筑身份。详细合同见
[Stage 5.5 炸药桶/活塞装配设计](M73BeamStage55DeviceAssemblyDesign.md)。

## 9. Stage 5.5：炸药桶/活塞首版装配

首版只服务固定六栋，每栋确定性发射一个装置。炸药桶占 `2×2×5` 个 36 cm cell；活塞按轴向占
`6×2×2`、`2×6×2` 或 `2×2×6` cell。候选必须位于 WFC 语义实体内、不与任何 Stage 5 brick 或
ProtectedVoid 正体积相交，并具有完整接地底面或可达地面的真实积木支座。装置保持单一刚体，不拆成
伪积木节点；独立发布 Slot、Device Load DAG 与 Assembly Hash。编辑器下拉项
`Stage 5.5 - Barrel / Piston Assembly` 同时显示完整建筑和真实装置资产。当前固定六栋 6/6，包含炸药桶和
X/Z 向活塞；装置触发后的真实 Chaos 效果仍为 `NotEvaluated`。

## 10. Chaos 前静态封口与跨工作树顺序

Stage 5/5.5 完成后不直接进入 Chaos。先由 M7 用真实生产 Brick 与 Device 验证六栋是否仍位于 Stage 4.5
`LocalBounds` 和 Pad 安全边内，并量化装置效果走廊；随后固定按
`M7 → Integration → M3 → Integration → M7 → Integration → M7 Chaos → Integration` 推进。

详细字段、判定矩阵、V2 所有权与交接清单见
[M7 JuryDemo 静态封口与 Fixed-Six V2 交接设计](M7JuryDemoStaticSealAndContractV2Handoff.md)。M7 不修改
共享合同或 M3 Fixture；若静态物理 Bounds 超界，本阶段只发布精确需求并停止 J4。只有静态联合门通过后，
Chaos 才允许在冻结包络内修改 Stage 5/5.5 与物理参数；修改 Stage 4 前缀必须显式重新开冻。

### 10.1 Fixed-Six V2 静态消费完成（2026-08-15）

固定顺序第 5 步已在 M7 完成：合并 `master@3991723` 后，按 V2 `ManifestEntryId` 精确重建并核验
E1～E6，只有六项身份全部匹配才原子生成和注册。最终六栋共 `5736` 块静态 HISM 积木和 `6` 个静态装置，
注册结果 Hash 为 `3948236352584381910`；快照漂移、顺序篡改和不完整批次均失败关闭，不回退旧生成路径。
新自动化 `J4V2Consumer` 为 `2/2`，并验证全局 LaunchPhysics 后仍为 `Promoted=0 / Activated=0`；
既有 J4 静态封口为 `1/1`，世界生成合同为 `2/2`。

该结果完成的是 `StaticRegistration`，不是 Chaos 激活或可见 PIE。下一所有者为 Integration：合并 M7
精确提交，在 canonical 地图与 M6 共同门完成 `Expected=6 / Registered=6 / SetupRejected=0` 的静态联合验收。
其后 M7 才研究当前 encounter 的动态激活；只要 Stage 4 前缀、Pivot、Bounds、Pad 与共享身份不变，
包络内的 Stage 5/5.5、材料、Solver 与装置效果调整不触发重新冻结。
