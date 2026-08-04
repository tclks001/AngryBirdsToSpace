# M7.3-Beam-D0：Gameplay Profile Catalog、Difficulty Curve 与 Settings Resolver

> 父文档：[M7 建筑生成演进路线](M7BuildingDevelopmentRoadmap.md)
> 上游：[M7.3-Beam-C Load DAG 与静态传力代理](M73BeamCLoadDAGAndStaticProxyDesign.md) · [Beam-C2 真实接触与承重收口](M73BeamC2RealContactAndLoadClosureDesign.md)
> 后续：[Beam-D1 真实 Brick/材料角色](M73BeamD1RealBrickAndMaterialRolesDesign.md) → [Beam-D1.5 视觉复杂度阶梯](M73BeamD15VisualComplexityLadderDesign.md)，Beam-D2 弱点/Chaos/Profile×Tier 认证，Beam-E Catalog 冻结与 M3 六栋生产接入
> 状态：首版 C++ 与自动化已完成；不修改共享世界生成合同，不接管 TaskGraph 生产建筑。

## 1. 目标

Beam-D0 把地图生成方需要理解的建筑输入收敛为两个 gameplay 维度：

- `GameplayProfileId`：选择解题语义，例如断柱、接缝释放、倾倒、下落或滑移；
- `DifficultyTier`：在同一解题语义内派生目标伤害成本、弱点暴露、瞄准容差、普通支撑冗余和弱点收益。

二者在 M7 内部经过唯一 `Settings Resolver`，一次性解析为 Shape Grammar、WFC、Beam-A/B/C
以及后续 Beam-D1/D2 所需的完整参数。M3 不传语法深度、WFC 预算、Brick 数量、材料比例、
Chaos 迭代数、落稳阈值或认证容差。

## 2. 阶段边界

本阶段包含：

1. M7 私有、版本化且有稳定 Hash 的 Profile Catalog；
2. 每个 Profile 的单调 Difficulty Curve；
3. `(GameplayProfileId, DifficultyTier, DeterministicSeed)` 到完整 Beam 设置的一次性解析；
4. 精确 `ResolvedM7ProfileId`、`ProfileCatalogHash` 与 `ResolvedSettingsHash`；
5. 未知 Profile、越界 Tier、重复 ID 和非法曲线的稳定拒绝；
6. 自动化证明难度影响玩法指标与内部生成策略，但不修改项目级硬预算和验证门槛。

本阶段不包含：

- 共享 `FABTSBuildingGenerationContract` 的字段扩展；
- M3 对 Profile 的选择、六栋 Encounter 排布或生产切换；
- 真实材料资产、真实 Brick、弱点改写、Chaos 落稳和攻击认证；
- 在旧 `TaskType`、`LayoutArchetypeId` 或 `VisualThemeId` 中夹带 Profile 语义。

共享合同属于集成工作树。正式 vNext 字段与 M3 接线只在 Beam-E 通过版本化契约流程完成。

## 3. 权威数据流

```text
External gameplay key
  GameplayProfileId + DifficultyTier + DeterministicSeed
    -> M7 private Profile Catalog lookup
    -> per-profile Difficulty Curve evaluation
    -> one Settings Resolver
       -> Shape Grammar / silhouette policy
       -> Beam-A stacking policy
       -> Beam-B motif grammar policy
       -> Beam-C fixed validation policy
       -> Beam-D1 material/device intent
       -> Beam-D2 weakness/collapse certification intent
    -> ResolvedM7ProfileId + ProfileCatalogHash + ResolvedSettingsHash
```

Resolver 之后，下游不得再随机重选 Profile。种子可以改变同一 Profile×Tier 内的确定性实例，
但不得改变 `ResolvedM7ProfileId` 或 Catalog 身份。

## 4. 首版 Profile Catalog

首版提供五个语义家族：

| GameplayProfileId | 弱点意图 | 预期失效 | 首版轮廓倾向 |
| --- | --- | --- | --- |
| `ColumnBreak` | 断柱 | 渐进折叠 | 双塔、竖向堆叠 |
| `SeamRelease` | 接缝释放 | 桥接释放 | 双端承托、桥体与门洞 |
| `TipOver` | 倾倒 | 定向倾倒 | 高瘦塔、棱柱/棱锥屋顶 |
| `DropTrigger` | 下落触发 | 受控下落 | 台地与悬挂质量语义 |
| `SlideRelease` | 滑移释放 | 侧向滑移 | 横向展开与错台 |

这些名称是首版 M7 私有 Catalog 键，不等同于已冻结的共享契约。Beam-D1/D2 若证明某个家族
无法形成稳定可玩解，应修改 Catalog 版本并重新认证，不得静默把它映射成另一种失败模式。

## 5. Difficulty Curve

Tier 首版有效域为 `0..5`，每个 Profile 独立配置基值和增量，派生：

- `TargetDamageCost`：随 Tier 非递减；
- `WeaknessExposureRatio`：随 Tier 非递增并有下限；
- `AimToleranceDegrees`：随 Tier 非递增并有下限；
- `SupportRedundancy`：按若干 Tier 增加一个离散等级；
- `WeaknessRewardMultiplier`：随 Tier 非递减；
- `SolutionSteps`：首版固定为 1，避免在单弱点玩法尚未认证前引入多步解。

Tier 还可温和改变轮廓尺寸、语法深度、Bay 间距和并行积木数量。它不能改变 WFC/图语法
操作预算、Load DAG 数量预算、承压面积下限、跨度/悬臂/长细比硬门槛或侧向机制合同；这些是
项目级安全参数，不是地图难度旋钮。

## 6. 身份与确定性

- `ResolvedM7ProfileId = <GameplayProfileId>_T<DifficultyTier>`；
- `ProfileCatalogHash` 覆盖 Catalog 版本、所有 Profile 语义、几何策略和完整 Difficulty Curve；
- `ResolvedSettingsHash` 额外覆盖精确 Tier、种子、派生玩法指标和派生生成策略；
- Beam-C2 的真实接触容差、支撑覆盖/跨度门槛和结构收口预算属于行为相关设置，必须进入
  `ResolvedSettingsHash`；它们仍是 M7 内部项目级策略，不成为 M3 输入；
- Catalog 定义先按 ID 排序再 Hash，源数组顺序不影响身份；
- 改变任何行为相关字段必须改变 Catalog Hash；
- 未知 ID、越界 Tier、重复 ID 或非法数据全部 fail closed。

## 7. 与后续阶段的分工

- **Beam-D1**：把 `MaterialPalette`、`DeviceIntent` 和 Beam Member 编译为真实 Brick/材料角色；
- **Beam-D2**：把 `WeaknessIntent`、`CollapseIntent` 接入 Failure Frontier、真实接触、Chaos，
  并对完整 `Profile×Tier` 矩阵认证；
- **Beam-E**：冻结通过认证的 Catalog，提出共享 vNext 合同，M3 只选择两个 gameplay 键并消费
  精确 resolved 身份，最终接入六栋生产建筑。

## 8. 自动化门槛

1. 默认 Catalog 合法、非空、Hash 非零；
2. 5 个 Profile × 6 个 Tier 全部解析，产生 30 个精确 resolved ID；
3. 所有 Difficulty 指标满足规定的单调性；
4. 同输入解析结果和 Hash 完全一致；
5. 未知 Profile、越界 Tier、重复 Profile ID 稳定拒绝；
6. Profile/Tier 改变内部生成策略，但项目级预算和 Beam-C 硬门槛完全不变；
7. Catalog 定义顺序不改变 Hash，行为字段变更必须改变 Hash。

Beam-D0 为纯数据阶段，不要求新建或修改地图资产，也不以可见 PIE 代替自动化。

## 9. 2026-08-03 首版实现证据

- 强制 Unity、禁用 Adaptive Unity 的 Development Editor 编译通过；
- fresh NullRHI `ABTS.M73DAG.BeamD0.` 专项 6/6；
- fresh NullRHI `ABTS.M73DAG.Beam` 路线回归 37/37；
- fresh NullRHI `ABTS.M7` 全量回归 110/110；
- 未修改共享合同、配置、Build.cs 或地图资产，现行 DAG2.3 生产绑定保持不变。

## 10. Catalog v3 与 Beam-C2

- Catalog 版本提升为 3，Resolved Settings Hash 覆盖真实接触与承重收口全部行为参数；
- 高 Tier 候选次数和 Brick 窗口按 30 组生产矩阵重新校准；
- `ColumnBreak` E5/E6 使用稳定双塔和密度增长，不以不稳定的深轮廓递归冒充复杂度；
- Catalog 仍只暴露 `GameplayProfileId + DifficultyTier + DeterministicSeed`，不向 M3 暴露收口参数。

## 11. Catalog v6 与按终端比例生成的立体屋顶

- E1/E2 不再把 Prism/Pyramid 权重归零；Catalog v6 为两档启用 `bRequireSingleTerminalRoof`，并分别保留至少 8/10 个屋顶 course。
- Shape Grammar 在 primitive WFC 之前先合并同高、相邻且覆盖率足够的屋顶终端；低 Tier 再从合并后的 Crown 中选择最高、面积最大的唯一屋顶，其余终端保持 Box。
- 屋顶目标高度取合并终端短边的约 90%，并量化为完整 Beam-A 截面；E1/E2 的 8/10 course 是下限，不是固定低高度。独占承重体把高度边界返还给直接承托的 Box；共享承重体保持接触底面并量化顶面，不新增量体、不制造缝隙。
- WFC 依据合并终端的长宽比加权选择形体：接近方形时偏向 Pyramid，长宽差较大时偏向 Prism；Prism 收分短轴并把屋脊对齐长轴。
- E3 仍是首次强制完整 primitive 多样性的档位，因此 E1/E2 获得建筑读形，但不会偷跑 E3 的轮廓复杂度。
- 单屋顶策略、屋顶合并、短边高度比例、长宽比阈值、屋顶 course 数及 Catalog v6 均进入 Catalog/Resolved Settings Hash；M3 外部输入合同保持不变。
- D1 Summary 记录 `RoofCourseBrickCount`，Preview 日志直接输出 `RoofBricks=`，用于区分“语义上是屋顶”和“实际已有足够层数可读”两件事。
- 固定矩阵仍满足原 Brick 窗口，因此没有调整平行积木最小间距或二并一阈值。
- fresh NullRHI `ABTS.M73DAG.BeamD0.*` 6/6、`ABTS.M73DAG.BeamD15.*` 3/3、`ABTS.M7` 123/123；强制 Unity、禁用 Adaptive Unity 的 Development Editor 完整链接通过。
