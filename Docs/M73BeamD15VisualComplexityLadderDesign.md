# M7.3-Beam-D1.5：视觉复杂度阶梯

> 上游：[Beam-D0 Profile Catalog](M73BeamD0GameplayProfileCatalogDesign.md) → [Beam-D1 真实 Brick](M73BeamD1RealBrickAndMaterialRolesDesign.md)
>
> 并行结构门槛：[Beam-C3 井干式稳定芯体](M73BeamC3CribCoreStabilityDesign.md)；下游：Beam-D2 弱点、Chaos 与 `Profile × Tier` 解题认证
>
> 状态：视觉阶梯与低 Tier 完整轮廓已通过用户编辑器读形；Catalog v8 的 C3 四柱芯体独立
> 5 × 2 静态门槛已通过。本阶段只认证视觉复杂度，不认证动态稳定或解题难度。

## 1. 目标与边界

`DifficultyTier=0..5` 必须形成六档肉眼可辨、严格递增的建筑复杂度。外部仍只提供
`GameplayProfileId + DifficultyTier + BuildingSeed`；Shape Grammar 深度、Bay 密度、并行积木数、
候选次数和 Brick 数量窗全部是 M7 内部策略。

本阶段同时使用两道门槛：

1. **强制复杂度里程碑**：每档必须解锁新的语义量体或拓扑操作，不能只把已有方框同比放大；
2. **Brick 数量目标**：六档数量窗互不重叠，实际 D1 Brick 数必须落入所属窗口。

弱点暴露、瞄准容差、错误击打后果、Collapse Intent 和 Chaos 结果仍由 Beam-D2 认证。

## 2. 六档权威视觉阶梯

| Tier | 玩家读形 | 强制语义里程碑 | 目标 Brick 数 |
| --- | --- | --- | ---: |
| 0 / E1 | 单体教学建筑 | 单一主量体；纵向叠层；Box 主体 + 唯一至少八层梁高的棱柱/棱锥终端屋顶；不要求完整 primitive 多样性 | 20–49 |
| 1 / E2 | 主体加一翼 | 第二量体；纵向叠层；Box 主体 + 唯一至少十层梁高的主屋顶；不要求完整 primitive 多样性 | 60–199 |
| 2 / E3 | 完整三段轮廓 | 第三量体；强制退台层；解锁屋顶形体与 Motif 多样性 | 300–799 |
| 3 / E4 | 复杂分区 | 主量体强制水平分裂并继续退台；更密 Bay/语法层 | 800–1499 |
| 4 / E5 | 跨接复合体 | 至少一条合法 SupportedSpan（有对应原型时）；高密承重格架 | 1500–2499 |
| 5 / E6 | 地标级轮廓 | 在 E5 宏观关系上继续深层退台/冠部递归，并保留至少一条合法跨接（有对应原型时） | 2500–4999 |

不同 Profile 保留自己的原型比例和解题语义；“有对应原型时”是指
`TwinTowerComplex / BridgedArcology / SpiredCampus`。E6 以复合冠部而非第二条桥作为新增里程碑，避免把多桥闭合失败
误当成视觉难度。`TerracedCitadel` 不凭空制造无法解析端点的桥，
但必须通过更深的水平分裂、退台和 Brick 数量窗达到同档视觉复杂度。

`ColumnBreak` 使用经 Beam-C2 认证的 Profile 专项高阶窗口：E4 上限为 1299，E5 为
1300–1499，E6 为 1500–3499。它的 E6 增量来自稳定双塔包络、四道平行积木和更密 Bay，而不是继续增加会破坏
承重闭合的轮廓递归。Profile 专项窗口仍须与自身相邻 Tier 严格递增，且只属于 D0 内部 Recipe。

## 3. 生成与认证流程

```text
Profile × Tier × BaseSeed
  -> D0 Visual Recipe（里程碑 + Brick 数量窗 + 内部生成参数）
  -> bounded deterministic candidate attempts
       -> Shape Grammar milestone audit
       -> Beam-A/B/C
       -> D1 one Member : one Brick compile
       -> Brick count window audit
  -> first accepted candidate
  -> Summary 记录 attempt、目标窗、量体/Motif/跨接证据与 Hash
```

候选搜索只在同一 `Profile × Tier × BaseSeed` 身份内派生内部 Seed；不得替换 Profile 或 Tier。
候选上限固定，全部失败时以稳定原因 fail closed。目标窗不是“尽量接近”的评分，而是正式接受条件。

## 4. 参数策略

- E1 使用三段可读语义轮廓、每量体一个结构 Bay、2 根平行视觉站位、单层 Motif Grammar，关闭完整 primitive/motif 强制多样性，但强制最高合法终端量体成为唯一主屋顶。减少重复 Bay 并允许 C3 在选中 Host 中以四柱芯体替换一组普通框架；不得删除语义量体、主屋顶 Crown、檐口或屋脊。
- 屋顶原语分配前先把同高、相邻且覆盖率足够的暴露终端合并为较大的 Crown；WFC 再按合并终端的 X/Y 长宽比选择形体。近方形终端偏向 Pyramid，长条终端偏向 Prism，且 Prism 屋脊沿长轴。
- 屋顶高度按短边与 Brick 截面量化：目标高度为短边的约 90%，E1/E2 仍分别保留至少 8/10 个 course。独占承重体时从 Box 主体重分配高度；共享承重体时保持接触底面并向下量化顶面，避免制造缝隙或重叠。
- `RoofCourseBrickCount` 作为读形诊断进入 D1 Summary；自动化同时检查屋顶唯一性、Box 主体和最低屋顶 course 数。
- 其余暴露终端在 E1/E2 强制保持 Box；E3 才首次要求 Box、Prism、Pyramid 完整多样性，避免低 Tier 提前获得高档轮廓密度。
- 每升一档至少提高语义里程碑，并提高尺寸、语法深度、Bay 密度、并行积木或屋顶 course 上限中的两项。
- Shape Grammar 的里程碑覆盖发生在随机规则选择之前，因此不会因 Seed 缺失。
- 预算随 Tier 提升，但仍受既有 Beam-A/B/C 项目级硬上限约束；硬门槛不作为难度旋钮。C3 的统一柱跨与四柱闭环对 E1–E6 相同。
- Brick 数量统计发生在 C3 改写、C2 真实接触/收口及 C3 最终认证之后，使用最终 D1 一对一绑定数；C2 规划 Reserve 不得加在 Tier 数量窗之外。

## 5. 自动化门槛

1. 六档 Recipe 合法，Brick 数量窗严格递增且互不重叠；
2. 5 个 Profile × 6 个 Tier 均能在候选上限内生成并通过 D1；
3. 每个结果的 `BrickCount` 落入本 Tier 窗口；
4. 同一 Profile 内，相邻 Tier 的 BrickCount 严格增加；
5. Tier 里程碑证据满足量体、Motif、跨接或语法规则要求；E1/E2 必须恰有一个 Prism/Pyramid 屋顶且至少保留一个 Box 主体；
6. 同输入选择相同 attempt、Hash 与 Brick 几何；
7. 候选搜索不改变 `ResolvedM7ProfileId`；
8. Beam-D2 gameplay 指标保持现状，不作为 D1.5 接受条件。

C3 接入后还必须区分两张矩阵：5 × 6 D1.5 矩阵验证视觉里程碑与 Brick 递增；5 Profile ×
Tier 0/1 验证四柱闭环、全部 Z 站位柱跨和最终预算。两张矩阵都必须通过，但都不能替代实时 Chaos PIE。

## 6. 编辑器验收

在同一个 `M7.3 Beam-D1 Real Brick Preview` Actor 上固定 Profile 与 Seed，只依次修改
`DifficultyTier=0..5`：

- E1 应显著最简单，但必须能读成一栋具有主体和至少八层收分屋顶的完整建筑，而不是纯方框架；
- E2 应出现第二量体或侧翼，同时仍只有一个主屋顶；E3 才出现多个屋顶形体和完整 primitive 多样性；
- 每升一级都应看到新的量体/分裂/退台/跨接以及明显增加的积木密度；
- `Last Result` 中的 Brick 数必须处于显示的目标窗内；
- `Visual Candidate Attempt`、语义量体数、Motif 数和 SupportedSpan 数可用于解释结果；
- 本验收不要求弱点更隐蔽、瞄准更难或 Chaos 倒塌更复杂。

## 7. 排错

| 症状 | 根因 | 处理 |
| --- | --- | --- |
| `BeamD15NoCandidateInBrickWindow` | 所有候选均未同时满足里程碑和数量窗 | 查看日志中的最后 Brick 数与失败阶段，校准本档内部 Recipe，不扩大外部 API |
| 相邻 Tier 外观相似 | 只改了连续尺度，强制里程碑未生效 | 检查 `ComplexityMilestoneTier` 和 Summary 语义证据 |
| E1 仍很大 | 使用了旧 D0 Resolver 或旧 Actor 缓存 | fresh Editor 重载并确认 Resolved Settings Hash 变化 |
| 高 Tier 超预算 | Bay/Member/Grammar 项目级预算被触发 | 收紧该 Tier 数量窗或内部密度；不得抬高共享硬门槛掩盖问题 |

## 8. 2026-08-03/04 实现与自动化证据

- Catalog 版本提升为 6；D0 Resolver 以一张六档 Recipe 同时解析里程碑、低 Tier 唯一终端屋顶及其 course 数、屋顶合并/长宽比/短边高度合同、软量体目标、Bay/Motif 密度、Brick 数量窗和 Beam-C2 承重收口策略。
- `ComplexityMilestoneTier=-1` 保留旧自由 Shape Grammar；D1.5 的 `0..5` 路径才启用主量体/侧翼/分裂/退台/跨接里程碑。
- `TargetVolumeCount` 是软目标，实际取 `min(TargetVolumeCount, MaxVolumeCount)`，不改变项目级硬预算。
- D1 在同一 Profile×Tier 内进行有界候选搜索（E5 最多 10 次，E6 最多 12 次）；首个同时满足装配、真实接触、承重收口、里程碑和数量窗的候选成为权威结果。
- 合并、按短边加高屋顶后的固定验收种子范围：E1 45–48、E2 111–183；两档仍分别位于原 20–49、60–199 窗口，且每个样本均为 Box 主体加恰好一个 Prism/Pyramid 屋顶。E1 产生 14–19 个 Roof Brick，E2 产生 18–37 个 Roof Brick。
- 低 Tier 屋顶采用逐层承托长度：每层仍按自身语义包络决定横向站位，但沿积木方向继承紧邻下层宽度。Pyramid 每层修正，Prism 随 X/Y 交替铺设每两层修正一次；因此减少 course 数只会增大轮廓台阶，不会切断层间接触。
- 由于既有 Brick 窗口仍可满足，本次没有改动 `MinimumParallelBlockGapCM` 或独立的 `TwoBlockMergeGapCM`，避免让屋顶修正改变主体结构密度。
- 低 Tier 校正后 fresh NullRHI `ABTS.M73DAG.BeamD15.*`：3/3，通过 30 个 Profile×Tier 子样本；E1/E2 的单屋顶与 Box 主体断言全部通过。
- 固定问题种子 `DropTrigger / Tier 0 / 669740` 生成 43 块 Brick、19 块 Roof Brick，`StructuralClosurePassCount=0`、`AddedStructuralSupportPostCount=0`、真实接触不一致与剩余承重违规均为 0；该断言证明加高屋顶自身直接承托，而不是靠后续闭合补柱。
- 低 Tier 校正后 fresh NullRHI `ABTS.M73DAG.BeamD0.*`：6/6；`ABTS.M7`：123/123。
- Development Editor 使用 `-ForceUnity -DisableAdaptiveUnity` 完整链接通过。
- 未修改共享合同、配置、Build.cs 或生产 DAG2.3 默认绑定；地图中既有用户改动未纳入本阶段。

Beam-C2、Catalog v6 屋顶终端合并、短边高度、长轴屋脊与逐层承托长度接入后的最终回归为 `ABTS.M73DAG.BeamD1` 9/9
（视觉矩阵、Column 高阶承重与低 Tier 固定屋顶专项）和 `ABTS.M7` 123/123；30 组样本的真实接触不一致、阻断支撑违规均为零。

## 9. Catalog v7 历史结果与 Catalog v8 静态门槛

- v7 将 E1 的 `MaximumBaysPerVolume` 从 2 收敛为 1，并证明低 Tier 可以在不删除量体和主屋顶的
  前提下为稳定结构让出预算；其三柱芯体 Brick 数与旧 NullRHI 通过记录只作历史基线；
- v8 把芯体合同升级为四柱矩形闭环：每 Belt 两 X + 两 Y course、四角真实接触，并在 C2 后审计
  全部 Z 站位和最终 Member 上限；
- Tier 0 优先用 Host 内普通框架替换支付芯体预算。单/双 lane 屋顶、檐口和单根屋脊不可作为 donor；
  任何兼容性回退都不得改变单主屋顶指纹或最低 course 数；
- v8 的正式低 Tier 门槛是固定 Seed 的 5 Profile × Tier 0/1；本轮 fresh v8 日志已 10/10 全绿。
  完整 5 × 6 视觉矩阵仍需独立回归，旧 v7 的“低 Tier 10 组已通过”结论仍不作为 v8 证据；
- 动态静置证据继续以 [Beam-C3 设计稿](M73BeamC3CribCoreStabilityDesign.md) 的 PIE 门槛为准。
