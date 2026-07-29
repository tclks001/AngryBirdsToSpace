# M7.3-DAG3-C：攻击可达性、运动净空与候选路由

> 状态：DAG3-C 代码与自动化已于 2026-07-29 完成；下游 [DAG-4 settled Contact 与攻击对照](M73DAG4SettledContactAndAttackRolloutDesign.md) 也已完成代码与 fresh 自动认证。生产 A/B/C/DAG-4 仍默认关闭，等待 DAG-4 用户可见 PIE。父级设计见 [M7.3-DAG-3 内部 Failure Frontier](M73DAG3InternalFailureFrontierDesign.md)，总路线见 [递归承载 DAG 生成总稿](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)。

## 1. 目标与边界

DAG3-C 把 DAG3-B 已经认证的 `W / P / Affected / Direction` 几何候选提升为“可以交给后续动态验证的可玩候选”。本阶段必须同时证明：

1. Failure Frontier 搜索不再局限于单节点和同一 Mapping 的柱组，而能在固定预算内发现物理化的 edge / small-cut；
2. 代表性鸟体能从建筑的权威局部攻击方向到达 `W`，而不是只有零半径视线；
3. 移除 `W` 后，`Drop / Tip / SlideThenTip` 至少拥有冻结门槛要求的静态自由空间；
4. `W` 原子绑定为权威 `WeakPointRecord`，命中成本来自该栋建筑的真实木、石、铁或玻璃 Profile；
5. 只有显式 authored 的 Recursive DAG Profile 可以开启 DAG3-C，默认 Profile 和 Legacy migration 必须继续关闭。

DAG3-C 不执行以下工作：

- 不移除 `W`，不施加代表性鸟击；
- 不用 Chaos 证明实际坍塌、二次碰撞或最终落点；
- 不在 Idle 后重建 settled Contact DAG；
- 不把静态扫掠结果冒充弱点/普通点收益对照；
- 不读取 M3 地形、HISM、其他建筑或弹弓射界来证明世界级走廊；
- 不切换 TaskGraph 默认 Profile；
- 不实现候选池、Novelty、Encounter 消费、WFC 包络或六栋视觉去重。

上述动态门槛属于 DAG-4；世界级射界属于 M10.1-D/集成契约；候选池与难度消费属于 DAG-5。

## 2. 唯一数据链与事务边界

```text
GroundAdapter::Resolve
→ LocalAttackDirection
→ DAG1/2 baseline
→ DAG3-A frontier（含有界 edge/small-cut）
→ frontier × pattern × attack-facing direction transaction
→ DAG3-B second-pass geometry/contact certification
→ DAG3-C attack corridor
→ DAG3-C motion clearance
→ DAG3-C real-profile WeakPoint binding
→ commit TrialData
→ GroundAdapter::AnalyzeFootprint
→ final StabilityValidator
```

启用 C 时，失败方向符号是事务维度。Dual/Seam 优先令弱侧朝向来弹侧，再尝试镜像；方向尝试计入 `MaxRewriteAttemptCount`，并进入 Pattern/C Hash。任一 C 门槛失败只丢弃当前 Trial，不得留下 WeakPoint、材料、Hash 或半套指标，也不得回退普通 DAG2.3/Legacy。

关闭 C 时必须保持 DAG3-B 的旧方向、几何、候选排序与 Hash 路径不变。

## 3. 有界 edge / small-cut

扩展搜索由独立开关控制，默认关闭。启用后：

- 每条候选边使用稳定 `(LowerNodeId, UpperNodeId)`；
- 可直接物理化的单 edge cut 映射为真实可攻击的 Lower Brick；
- 一般 small-cut 对每个受保护 Root 运行 unit-capacity vertex min-cut，Ground 和 Root 不可切；
- 只接受 `1..MaxCutSetSize` 个真实 BrickNode；
- 最大候选数、最大 cut 大小和最大 flow 操作数都是硬预算；
- 超预算明确拒绝，不静默截断；
- Candidate Hash 包含规范排序后的 node/edge 身份；
- 跨多个 Macro Interface、无法收缩到唯一 B Rewrite Intent 的 cut 明确拒绝为非可改写候选，不伪造几何。

旧 DAG3-A/B 关闭扩展搜索时，原 Candidate 集合与 Hash 必须精确不变。

## 4. 建筑局部攻击可达性

`AttackDirection` 表示鸟飞向建筑的局部速度方向。对每个 `W`：

1. 取迎弹面上的有界采样点；
2. 将所有非 `W` Brick AABB 按代表性鸟半径做 Minkowski 膨胀；
3. 从固定 Approach Distance 向采样命中点做线段测试；
4. 统计可达样本比例、最小净空、首个阻挡 NodeId、命中点和稳定 Hash。

默认代表性鸟半径为 42 cm，与当前 Bird Capsule 半径一致。点射线能穿过但缝宽小于鸟直径时必须拒绝。这里只考虑建筑自身 Brick 的遮挡，不读取世界其他 Actor。

## 5. 运动净空

运动扫掠使用已编译 Brick 几何和 `PatternResult` 的权威闭包，不调用 Chaos：

| Motion | 静态门槛 |
| --- | --- |
| `Drop` | 全部 Affected Brick 沿 `-Z` 平移，首个阻挡前自由距离不小于 `MinFreeDropDistanceCM` |
| `Tip` | Affected 围绕 `P` 顶部、沿预期方向离散旋转，自由角不小于 `MinFreeTipAngleDegrees` |
| `SlideThenTip` | 先沿预期方向获得 `MinFreeSlideDistanceCM`，再通过同一 Tip 门槛 |

扫掠对全部 Affected 与全部非 Affected、非 `W/P` Brick 做保守 AABB 检查；平移使用端点包围，Tip 对每个角点求旋转区间内各坐标分量的解析极值，因此不会只靠角步端点漏掉弧中碰撞。步长、角步和总采样数均有硬上限。Single 允许在最小自由下落之后被下层结构接住并产生二次碰撞；本阶段不把这种后续承接误判为“没有 Drop”。

## 6. 材料与 WeakPoint 权威绑定

本阶段继续采用整栋同材质合同：

```text
Node.Material == Node.OriginalMaterial == GenerationSettings.PrimaryMaterial
```

不得把 W 偷换成玻璃，也不得用隐藏伤害倍率。这样现有 DAG2.3 的同材质质量比例仍成立，不需要伪造混材累计荷载。

对实际 Profile 验证速度、摩擦、恢复系数、密度、伤害和速度传递字段均在合法范围，并把这些原始字段全部写入认证结果与 `PlayabilityHash`，避免两个实际行为不同但命中次数相同的 Profile 共享身份。随后计算：

```text
LocalBreakEffort = BreakDamage / DamageAtBreakSpeed
EstimatedHits = ceil(LocalBreakEffort)
```

认证成功后一次性写入：

- `BrickNode.bWeakPoint`、Role、Exposure、AffectedRatio、EstimatedHits、Score；
- 一个与 `PatternResult.WeakNodeIds` 精确对应的 `WeakPointRecord`；
- DAG Pattern、DAG Motion、AffectedNodeIds、方向与静态 Margin；
- `DAGFailurePlayabilityResult` 和独立 `PlayabilityHash`；
- Structure 级弱点/难度摘要。

不调用 Legacy `FABTSM73WeakPointPlanner`，不重新扫描并另选节点，不生成 Reinforcement。混合材质候选必须等逐节点密度感知累计荷载完成后另行设计。

## 7. Profile 路由

新增独立 `FABTSM73DAGFailurePlayabilitySettings`：

```text
bEnablePlayabilityRouting = false
```

门槛为：

- 默认 Workshop / TargetBuilding / FurnaceRuins：A/B/C 全部关闭；
- Legacy migration：C 始终关闭；
- 显式 Recursive DAG Profile：保留 authored C 设置；
- C 开启但 A、B 或 generalized cut 任一未开启：Profile/Actor fail closed；
- C 失败：建筑候选拒绝，不走 Legacy fallback。

显式 opt-in 只表示生产链已有候选入口，不代表生产默认启用。

## 8. 自动化门槛

正式测试前缀为 `ABTS.M73DAG3.C.`，至少覆盖：

1. `GeneralizedCut`：递归深度大于 0、逆序确定性、cut/候选/flow 预算、单入边上游 cut，以及旧 Hash 兼容；
2. `AttackCorridorAndOcclusion`：正向、反向、完全遮挡和窄缝负例；
3. `MotionSweepClearance`：三 Motion 正例与各自阻挡负例；
4. `MaterialProfileMatrix`：三 Pattern × 四材料，材料不变且真实成本排序；
5. `AtomicFailureAndProfileOptIn`：disabled no-op、前置门槛、失败回滚与路由；
6. `Pipeline.PatternsBudgetAndGeneralizedBoundary`：三 Pattern 完整 A→B→C 管线、共享 attempt 预算、跨 Macro Interface cut 拒绝和 edge 身份；
7. `Runtime.WeakNodeDamageRouting`：真实 Actor/Module 能由 NodeId 找到，并使用 MaterialSystem 的实际 Profile；不据此声明坍塌通过。

回归还必须保留：

```text
ABTS.M73DAG3.
ABTS.M73DAG.
ABTS.M73B.
ABTS.M73B2.
ABTS.M7.TaskGraphDAG23ProfileRouting
ABTS.Contracts.WorldGeneration
```

编译使用 `-ForceUnity -DisableAdaptiveUnity`。DAG3-C 完成声明必须给出 fresh NullRHI 日志、精确通过数和禁用路径无漂移证据。

## 9. 2026-07-29 实现与验收证据

实现结果：

- DAG3-A generalized opt-in 增加物理化 direct edge、bounded vertex small-cut、规范 edge 身份与 flow/candidate/cut 硬预算；单入边但 direct edge 不可物理化时仍会搜索上游 small-cut；
- generalized frontier 必须完整归属唯一 `SupportPlate → Columns → LoadPlate` Mapping，跨 Macro Interface 或被篡改的 edge 身份显式 fail closed；
- DAG3-B 的 Dual/Seam 将攻击朝向与镜像作为同一事务的 attempt 维度，C 失败后只丢弃当前 Trial；
- DAG3-C 使用 42 cm 默认鸟半径认证局部攻击走廊，并对 Drop、Tip、SlideThenTip 做有界静态净空认证；
- `Node.Material == Node.OriginalMaterial == GenerationSettings.PrimaryMaterial`，真实 Profile 原始字段进入结果与 Hash，不做弱材质替换；
- 成功时一次性提交 `BrickNode`、`WeakPointRecord`、Structure 摘要和 C Result；任何失败只返回 `bEnabled + RejectReason`，不残留半套指标；
- 默认 Profile、旧 Pipeline overload 与 Legacy migration 均保持 C/generalized 关闭；显式 C 缺少 A、B 或 generalized 任一前置条件即拒绝。

最终 fresh-process 证据：

- `-ForceUnity -DisableAdaptiveUnity` Editor 完整失效编译成功（8 actions，33.05 秒），最终源码状态增量复编再次成功（4 actions，17.66 秒）；
- `ABTS.M73DAG3.C.`：10/10 Success，日志 `Saved/Logs/DAG3C-Hardened-20260729-174912-291-FreshAutomation.log`；
- `ABTS.M73DAG3.`：22/22 Success，日志 `Saved/Logs/DAG3C-Final-FullDAG3-20260729-175003-497-FreshAutomation.log`；
- 旧 `ABTS.M73DAG.`：10/10 Success，日志 `Saved/Logs/DAG3C-Final-LegacyDAG-20260729-175003-515-FreshAutomation.log`；
- `ABTS.M7`：37/37 Success，其中 `ABTS.M73B.` 1/1、`ABTS.M73B2.` 2/2、TaskGraph DAG2.3 Profile Routing 1/1，日志 `Saved/Logs/DAG3C-Final-M7-20260729-175052-985-FreshAutomation.log`；
- `ABTS.Contracts.WorldGeneration`：2/2 Success，日志 `Saved/Logs/DAG3C-Final-WorldContracts-20260729-175052-996-FreshAutomation.log`。

这些证据完成 DAG3-C 的纯数据、完整生成管线和真实运行时伤害路由门槛。[DAG-4](M73DAG4SettledContactAndAttackRolloutDesign.md) 已补齐移除 `W` 后的真实 Chaos 动态候选认证，但用户可见弱点击毁 PIE 仍未通过；生产默认因此继续关闭。
