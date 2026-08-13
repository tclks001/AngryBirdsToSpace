# M7 评委演示六栋垂直切片冲刺设计

> 状态：2026-08-13 建立；Stage 1～3 与演示六栋清单 v1 已通过用户视觉验收并冻结，开始 Stage 4。
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
