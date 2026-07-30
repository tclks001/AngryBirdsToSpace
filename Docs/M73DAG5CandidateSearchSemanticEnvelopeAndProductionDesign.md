# M7.3-DAG-5：候选搜索、语义轮廓与生产认证设计

> 文档性质：M7.3-DAG-5 的独立工程设计与验收合同。
>
> 状态：DAG5-A 已完成代码、强制 Unity 编译与 fresh 自动化验收，保持显式 opt-in；
> DAG5-B～E 已冻结边界，尚未实现。下一阶段为 DAG5-B。
>
> 父级：[M7.3-DAG 递归承载图总路线](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)。
>
> 前置：[DAG2.3 累计荷载与联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md) ·
> [DAG3-C 攻击可达与候选路由](M73DAG3CAttackReachabilityAndProductionRoutingDesign.md) ·
> [DAG-4 Settled Contact 与攻击对照](M73DAG4SettledContactAndAttackRolloutDesign.md)。
>
> 研究依据：[3D WFC 建筑外观体块与承载 DAG 拟合](M73WFCBuildingEnvelopeAndDAGFittingResearch.md)。
> 生产边界：[M7 TaskGraph 球面建筑接入](M7TaskGraphSphericalBuildingIntegrationDesign.md)。

## 1. 目标

DAG-1～DAG-4 已证明以下链路可以被分别验证：

- 递归语法可以产生确定性的承载拓扑；
- DAG2.3 可以把承载意图编译成真实 Plate、Column 与 Contact DAG；
- DAG3-C 可以证明弱点的攻击可达性和静态运动净空；
- DAG-4 可以用 settled Contact 与真实 Chaos 对照证明弱点相对普通点确有机械收益。

当前剩余问题不是继续增加递归深度，而是把“单次推导、规则方盒子、单栋验收”
提升为“有界搜索、复杂轮廓、六栋联合选择与正式生产认证”。

DAG-5 必须同时解决：

1. 递归层数或规则组合增加后，一次坏推导不应令整个可行 Profile 随机消失；
2. 抽象宏节点预算不能冒充最终实砖预算；
3. 建筑必须出现退台、偏置、门洞、悬挑、桥跨、空腔和高低错落等轮廓复杂性；
4. 六栋建筑必须联合去重，而不是各自通过后仍长得相同；
5. Encounter 难度、视觉元数据和装置必须成为生成约束；
6. 最终六栋必须逐栋重新通过 DAG3-C、DAG-4、PIE 与性能门槛，之后才能切换生产默认。

## 2. 总体链路

```text
Task / Encounter / Site / Difficulty Constraints
  -> DAG5-A：有界候选调度、容量预检、确定性尝试
       -> DAG5-B：Shape Grammar 宏观轮廓
       -> DAG5-B：局部语义 WFC
       -> SemanticEnvelope
          MustOccupy / MayOccupy / MustVoid / Ports / WeaknessSocket
       -> DAG2.3：承载边、累计荷载、联合支撑
       -> ModuleCompiler：一个语义区 -> 1..N 个长方体 Brick
       -> Contact DAG / 静态完整性 / 当前已启用的 DAG3 门槛
  -> DAG5-C：候选池、Novelty Archive、六栋联合选择
  -> DAG5-D：Encounter / Device / TaskGraph 显式接入
  -> DAG5-E：六栋逐栋 DAG3-C + DAG-4 + PIE 最终认证
  -> 单独评审生产默认切换
```

权威边界：

- Shape Grammar 与 WFC 只表达空间和建筑语义，不声明最终承重事实；
- DAG2.3、真实碰撞 Contact DAG 与稳定性验证仍是物理权威；
- DAG5-A 是一个 Profile 内部寻找一个可编译候选；
- DAG5-C 才保存跨候选档案并联合挑选六栋；
- 任一启用阶段失败都拒绝当前候选，不得回退 Legacy；
- DAG5-B 已启用时也不得静默退回旧矩形 Preset 冒充复杂轮廓通过。

## 3. DAG5-A：可行域预检与确定性有界回溯

### 3.1 解决的问题

当前链路只有一次由 `BuildingSeed` 决定的 DAG-1 推导。某次推导可能因为：

- `DAGParallelScopeTooNarrow`；
- `DAGScopeTooSmall`；
- `DAGColumnTooShort`；
- `DAGNoJointSupportHull`；
- `COMOutsideSupportHull`；
- 编译后真实砖块数量超过预算；

而失败。一次推导失败并不能证明同一 Profile 的可行域为空。

另外，`MaxEstimatedBrickCount` 只按宏节点和预留弱点估算，并不等于
`FABTSM73StructureData.Bricks.Num()`。一个宏 Plate 还可能产生多个真实支撑柱，
因此必须增加编译后的硬门槛。

### 3.2 配置合同

使用独立 `FABTSM73DAG5ASettings`：

| 字段 | 含义 |
|---|---|
| `bEnableFeasibilitySearch` | 显式 opt-in；默认关闭以保持旧 DAG2.3 golden |
| `bEnableCapacityPreflight` | 在候选构造前检查硬下界与配置自洽性 |
| `MaxCandidateAttempts` | 候选尝试总预算，不使用墙钟时间决定结果 |
| `MaxCompiledBrickCount` | 编译后实砖硬上限；`0` 表示使用建筑 Profile 上限 |

约束：

- `MaxCandidateAttempts` 是确定性操作预算；
- 不用“运行了多少毫秒”决定接受哪个候选；
- 不自动降低 `MinExpansionDepth`、不削减最低复杂度要求；
- 不把 `ExpansionStepBudget` 静默降为零；
- 不修改 TaskGraph 提供的逻辑 `BuildingSeed` 身份。

### 3.3 容量预检

对于初始终端数 `T0` 和最低递归深度 `Dmin`，所有终端达到最低深度至少需要：

```text
RequiredExpansionSteps = T0 * (2^Dmin - 1)
RequiredTerminalCount  = T0 * 2^Dmin
RequiredExpressionNodes = InitialExpressionNodes + 2 * RequiredExpansionSteps
```

预检至少验证：

- `RequiredExpansionSteps <= ExpansionStepBudget`；
- `RequiredExpressionNodes <= MaxAbstractNodeCount`；
- `RequiredTerminalCount + ReservedWeaknessBrickCount <= MaxEstimatedBrickCount`；
- `RequiredTerminalCount <= EffectiveCompiledBrickLimit`；
- 搜索设置、建筑尺寸和布局最小尺寸均合法。

这些是必要条件而非充分条件。具体宽度、柱高、支撑凸包和真实接触仍由候选实际经过
DAG2.3 才能证明。

### 3.4 确定性候选序列

候选 `0` 必须使用原始 DAG 设置和原始 Seed，以保留可解释性与兼容性。
后续候选 Seed 由以下稳定输入导出：

```text
CandidateSeed = StableHash(
  LogicalBuildingSeed,
  GeneratorVersion,
  Preset,
  AttemptIndex,
  DAG5AAlgorithmVersion)
```

每次尝试必须执行完整的当前纯数据候选链。将来 DAG5-B 接入后，同一调度器改为同时派生
Shape Grammar 与 WFC Seed，无需另造一个无界外层循环。

搜索采用 first accepted：

1. 按稳定候选序列执行；
2. 记录每次 CandidateSeed 和稳定拒绝原因；
3. 候选只有在所有当前启用纯数据门槛与实砖硬预算通过后才可接受；
4. 第一个接受候选原子提交到 `OutData`；
5. 全部失败时 `OutData` 保持空，不泄漏半成品。

### 3.5 实砖硬预算

有效上限为：

```text
EffectiveCompiledBrickLimit =
  Min(
    GenerationSettings.MaxBrickCount,
    DAG5A.MaxCompiledBrickCount > 0
      ? DAG5A.MaxCompiledBrickCount
      : GenerationSettings.MaxBrickCount)
```

每次 `ModuleCompiler` 完成且 Contact 审计通过后检查：

```text
StructureData.Bricks.Num() <= EffectiveCompiledBrickLimit
```

超限必须以 `DAG5ABrickBudgetExceeded` 拒绝当前候选。不得删掉若干支撑柱凑预算，
因为那会使已验证的累计荷载与接触图失效。

### 3.6 结果与诊断

`FABTSM73DAG5AResult` 至少记录：

- 是否启用、预检是否通过、最终是否接受；
- 尝试次数、选中 CandidateIndex 与 CandidateSeed；
- 有效实砖上限和最终实砖数；
- 最低深度所需 Expansion / Terminal 下界；
- 每次候选的稳定拒绝原因；
- 独立 `SearchHash`；它覆盖搜索、DAG、Layout、Building、Frontier、Pattern、
  Playability、Difficulty、按序材质 Profile、攻击方向、候选轨迹/几何身份与三个
  下游结果 Hash；
- 最终 `RejectReason`。

逻辑 Seed 与选中候选 Seed 必须同时可见。重现一个结果不能只记录最终拓扑 Hash。
当前 Hash 使用 UE 5.8 反射结构的规范文本作为完整输入身份；若后续增删或重排
`UPROPERTY`、升级引擎，必须同步提升 `SearchVersion`。

### 3.7 DAG5-A 验收门槛

自动化必须覆盖：

1. A 关闭时，旧 DAG2.3 输出与失败原因不变；
2. 同输入重复运行，选中候选、砖块 Transform、拒绝序列与 `SearchHash` 完全一致；
3. 首次推导不可解但候选域有解时，在预算内找到后续可行候选；
4. 不可行最低递归深度在尝试前稳定拒绝；
5. 编译后 13 块砖、硬上限 12 时拒绝，上限 13 时接受；
6. 拒绝后不发布部分 `StructureData`；
7. 搜索次数不超过 `MaxCandidateAttempts`；
8. 不存在 Legacy fallback，也不通过降低最低复杂度伪装成功。

深递归成功率必须按“逻辑 Seed 是否找到候选”统计，不能按候选尝试次数扩大分母。
固定 Seed 压力矩阵使用与 DAG5-A 相同的完整候选门比较 `K=1` 和 DAG5-A 默认候选预算
`K=8`：

- 每个批准的可行深递归 Profile 使用 128 个固定 Seed；
- `K=8` 的绝对接受率至少为 95%；
- 相对 `K=1`，至少消除 80% 的失败；
- `K=1` 已接受的 Seed 在更大预算下不得回归，且仍须在 attempt `0` 发布相同几何；
- 重复运行的候选轨迹、选中 Seed、几何、`SearchHash` 和 cohort hash 必须一致。

容量预检失败和确定性 Scope 无解必须作为独立负例，不计入可行 Profile 的接受率。当前
`ExpansionStepBudget <= 32` 时，TwinTowerBridge 的最低深度 3 需要 35 次展开，属于
全局容量无解；纯 Parallel 的 160 cm Scope 也属于逐候选 Scope 无解。两者都必须保持
0% 接受，不能靠降低最低深度、放宽 Scope 或切回 Legacy 伪造成功。

本阶段不要求把 A 默认打开到 TaskGraph 生产 Profile。编辑器 Actor 可显式启用以调试；
正式 Profile 切换属于 DAG5-D/E。

### 3.8 落地与验收证据（2026-07-29）

DAG5-A 已落实：

- `FABTSM73DAG5ASettings`、逐候选 Attempt Trace 与 `FABTSM73DAG5AResult`；
- 最低递归深度/节点/估算砖量/根 Scope 容量预检，所有 DAG/Layout 浮点输入
  对 NaN/Inf fail closed；
- 候选 `0` 保留原始逻辑 Seed，后续候选使用版本化稳定 Seed；搜索严格 first accepted，
  全失败时不发布任何部分 `StructureData`；
- 每个候选仍执行完整的现行 DAG2.3、DAG3-B/C 可选链和静态稳定性验证；
- 编译后以 `Bricks.Num()` 执行真实物理砖硬预算，不删承重构件凑预算；
- Actor 的 `LastDAG5AResult` Details 与日志暴露输入 Seed、选中 Seed、尝试次数、
  实砖上限、完整链 `SearchHash` 与拒绝原因；GenerationSummary 同步启用/接受状态、
  尝试次数、选中 Seed、实砖上限与 Hash；
- `bEnableFeasibilitySearch=false` 保持旧 one-shot DAG2.3 路径及生产 Profile 不变。

首次落地源码证据：

- `Saved/Logs/DAG5A-20260729-223025-ForceUnity-Build.log`：
  `-ForceUnity -DisableAdaptiveUnity`，4 actions，`Result: Succeeded`；
- `Saved/Logs/DAG5A-20260729-223102-FreshAutomation.log`：
  `ABTS.M73DAG.DAG5A.` 11/11 Success；
- `Saved/Logs/DAG5A-20260729-223216-M7Regression-FreshAutomation.log`：
  完整 `ABTS.M7` 54/54 Success，包含旧 DAG2.3、DAG3、DAG4 与
  `TaskGraphDAG23ProfileRouting`；
- `Saved/Logs/DAG5A-20260729-223333-WorldContracts-FreshAutomation.log`：
  `ABTS.Contracts.WorldGeneration` 2/2 Success。

深递归固定 Seed 补充证据：

| Profile | 深度 | `K=1` | `K=8` | `K=8` 救回 | 结论 |
|---|---:|---:|---:|---:|---|
| F-Single-D2 | 2 | 121/128（94.53%） | 128/128（100%） | 7/7 | 通过 |
| F-Arch-D2 | 2 | 87/128（67.97%） | 128/128（100%） | 41/41 | 通过 |
| F-Twin-D2 | 2 | 40/128（31.25%） | 126/128（98.44%） | 86/88 | 通过 |
| F-Arch-D3 | 3 | 35/128（27.34%） | 122/128（95.31%） | 87/93 | 通过 |

四个可行 Profile 的默认 `K=8` 合计为 `504/512 = 98.44%`；所有 `K=1` 已接受
Seed 均保持 attempt `0` 几何一致，回归数为 0。`K=1` 还逐 Seed 与独立执行的
`BuildWithFailurePattern + 实砖预算 + StaticStability` oracle 对照，576 个逻辑
Seed 的接受状态全部一致，接受时几何一致；因此不是搜索入口与自身做循环证明。
每个 Profile 的每档 K 各复跑 8 个 Seed，Attempt Trace、选中结果、`SearchHash`
和几何 mismatch 均为 0。Twin-D3 容量无解和 Single-D2 纯 Parallel Scope 无解
两个 32-Seed 对照组均为 0%，前者在尝试前拒绝，后者每个候选均以
`DAG5AParallelScopeTooNarrow` 拒绝。拒绝候选还实际覆盖
`DAGNoJointSupportHull`、`DAGNoLoadSupportCandidates`、`DAGUnexpectedBypass`、
`DAG5ABrickBudgetExceeded` 和静态稳定性拒绝，因此不是只测语法展开成功。

- `Saved/Logs/DAG5A-DeepSweep-Final-20260729-231623-ForceUnity-Build.log`：
  `-ForceUnity -DisableAdaptiveUnity`，4 actions，`Result: Succeeded`；
- `Saved/Logs/DAG5A-DeepSweep-FinalA-20260729-231655-FreshAutomation.log` 与
  `Saved/Logs/DAG5A-DeepSweep-FinalB-20260729-231754-FreshAutomation.log`：
  两个独立 fresh NullRHI 进程均 Success，17 条统计/摘要完全一致，cohort mismatch
  为 0；
- `Saved/Logs/DAG5A-DeepSweep-Final-FullDAG5A-20260729-231850-FreshAutomation.log`：
  `ABTS.M73DAG.DAG5A.` 12/12 Success；
- `Saved/Logs/DAG5A-DeepSweep-Final-M7Regression-20260729-231936-FreshAutomation.log`：
  完整 `ABTS.M7` 55/55 Success。

DAG5-A 是默认关闭的纯数据调度/预算阶段，不新增可见建筑轮廓，因此本阶段无需单独
PIE 视觉验收；轮廓可见性从 DAG5-B 开始验收。

## 4. DAG5-B：Shape Grammar + 局部 WFC 复杂轮廓

### 4.1 两者不是同一个算法

Shape Grammar 与 WFC 都操作建筑语义，但职责不同：

- Shape Grammar 负责低频、层级化、可解释的宏观体量；
- WFC 负责固定局部格内的邻接相容与细部组合；
- DAG2.3 负责承载、接触与稳定。

本项目不使用 UE PCG Shape Grammar 节点。二者都实现为纯 C++、确定性、有界的数据层。

### 4.2 Shape Grammar 职责

宏观规则至少能表达：

- `Stack`：体量上下叠置；
- `SplitHorizontal`：左右或前后分区；
- `Setback`：上层退台；
- `Offset`：楼层质心偏置；
- `Bridge`：两体量之间形成桥跨；
- `CarveThroughOpening`：门洞或贯穿空腔；
- `Cantilever`：局部悬挑；
- `Crown`：非统一屋顶线；
- `AsymmetricHeight`：高低塔。

Shape 推导输出带稳定路径的 Volume Scope，而不是 Brick Actor。

### 4.3 局部语义 WFC 职责

在建议的 `5~9 × 3~5 × 4~8` 有界格中，对 Shape Scope 内的局部单元求解：

- Foundation；
- Floor / Deck；
- WallPier；
- Frame；
- DoorVoid；
- WindowVoid；
- BeamZone；
- Roof；
- Cantilever；
- MustVoid。

WFC 只处理邻接、边界、主题权重和硬锚点。矛盾时在有限传播/回溯预算内失败，
由 DAG5-A 更换 Shape/WFC 候选；不得偷偷返回旧矩形塔。

### 4.4 稳定中间接口

`FABTSM73SemanticEnvelope` 至少包含：

- `MustOccupy`；
- `MayOccupy`；
- `MustVoid`；
- Support / Load / Bridge / Frame Ports；
- Weakness Socket；
- Attack Clearance；
- 每个语义区域的 Shape Derivation Path；
- `SilhouetteFeatureMask`；
- `EnvelopeHash`。

轮廓特征掩码至少包括：

- `Setback`；
- `FootprintCentroidShift`；
- `ThroughOpening`；
- `HeightAsymmetry`；
- `BridgeSpan`；
- `Cantilever`；
- `NonUniformRoofline`。

每个 DAG5-B 验收候选至少命中两项特征；四种基准轮廓族合计覆盖主要特征。

### 4.5 接入现有砖块生成

DAG5-B 不是先生成漂亮模型再强行拟合承重树。正确链路是：

1. SemanticEnvelope 限定宏节点允许占据、禁止占据与端口；
2. DAG2.3 在端口和 MustVoid 约束下求承重边、累计荷载和联合支撑；
3. `FABTSM73DAGModuleCompiler` 将一个语义区编译为 `1..N` 个长方体 Brick；
4. Contact DAG 从最终碰撞几何重建真实承重关系；
5. MustVoid 被占用、MustOccupy 未覆盖或出现意外旁路时拒绝候选。

编译词汇至少补齐：

- 分段/偏置 Deck；
- 门窗 Frame；
- 短墙垛；
- 桥梁；
- 局部悬挑；
- 高低屋顶 Cap。

初版仍可全部使用长方体砖。复杂性来自占据、空洞、偏移和组合，而不是更换材质或
只给完整方板继续递归。

### 4.6 DAG5-B 验收门槛

- 同一输入复现相同 Shape Derivation、WFC Collapse、Envelope、DAG 和 Brick；
- 至少有退台塔、偏置桥、门洞墙、单侧高塔四种明显不同轮廓族；
- 每个候选至少命中两项轮廓特征；
- `MustVoid` 零 Brick，`MustOccupy` 全覆盖；
- 宏观包络、最终 Brick AABB/占据和可见轮廓一致；
- 所有承重事实均通过 DAG2.3 与 Contact DAG；
- 复杂轮廓接入现有 Runtime Module，不另建不可破坏展示壳；
- B 无解时拒绝当前候选，由 A 换候选；禁止旧 Preset fallback。

## 5. DAG5-C：候选池与六栋联合去重

DAG5-C 才建立跨候选 `NoveltyArchive`。它消费已经通过 A/B 和 DAG3-C 静态门槛的候选，
不在一个失败候选内部继续无界搜索。

联合签名至少包括：

- `EnvelopeHash` 与轮廓特征；
- 分层占据图和体量质心序列；
- Support DAG signature；
- 材质分布；
- Frontier / Pattern / `W/P/Affected/Direction` 弱点签名；
- 攻击方向、预计运动与建筑高度分布。

正式退出门槛：

- 恰好选出六栋；
- 至少覆盖四种轮廓族；
- 六栋联合签名唯一；
- 相邻建筑的轮廓、材质和弱点组合不重复；
- 不允许六栋都退化为“上下两块、中间细腰”；
- 每栋先通过 DAG3-C 静态可玩门槛。

## 6. DAG5-D：Encounter、装置与 TaskGraph 显式接入

DAG5-D 让生成器消费 Encounter 难度与视觉元数据：

- 道路阶段和建筑序号；
- 目标材料与混合材料策略；
- 弹弓射界和攻击方向；
- 建筑高度、轮廓族与视觉地标要求；
- 目标弱点运动类型；
- 坍塌安全区；
- 绳、链、炸药桶和弹簧活塞装置槽。

约束：

- M7 只消费稳定合同，不反向读取或修改 M3 内部算法；
- `LaunchSite` 不生成普通玻璃建筑；
- 装置必须进入 Contact/Failure 审计，不能制造绕过弱点的意外捷径；
- 难度和视觉元数据必须实际改变候选约束，不能只写日志；
- 生产仍保持显式 opt-in，不在本阶段顺手切默认。

## 7. DAG5-E：六栋联合调优、动态重认证与生产切换

DAG5-E 对 C 选出的最终六栋逐栋重新执行：

1. DAG3-C 攻击可达、净空、真实材料与弱点绑定；
2. Idle 后 settled Contact 重建；
3. `1 Weak + exactly 3 Ordinary` 独立 Shadow Island Chaos 对照；
4. 实际材料下的绝对响应、相对收益与方向门槛；
5. 球面 CellTopo Anchor、PIE 可见稳定性和完整玩法；
6. 确定性、性能和候选预算回归。

最终产物是带版本、Seed、Profile、EnvelopeHash、TopologyHash、PatternHash、
ValidationHash 的 Certification Manifest。

只有六栋全部通过后，才允许以单独提交评审：

- 打开 TaskGraph DAG5 生产默认；
- 停止旧矩形 DAG2.3 Profile 作为普通建筑默认；
- 保留显式失败，禁止 Legacy fallback。

## 8. 阶段总表

| 阶段 | 核心职责 | 主要产物 | 退出门槛 |
|---|---|---|---|
| DAG5-A（已完成） | 单 Profile 可行域预检、确定性候选尝试、实砖硬预算 | Capacity Report、Attempt Trace、原子 `StructureData` | 可行域不因一次坏推导消失；不可行输入 fail closed；默认关闭兼容 |
| DAG5-B | Shape Grammar 宏观轮廓 + 局部语义 WFC + Brick 接入 | Shape Derivation、SemanticEnvelope、复杂轮廓 Brick Assembly | 四类明显轮廓；MustVoid/MustOccupy 与真实承重全通过 |
| DAG5-C | 跨候选 Novelty 与六栋联合选择 | Six-Building Manifest、联合签名 | 恰好六栋、签名唯一、至少四类轮廓、逐栋 DAG3-C |
| DAG5-D | Encounter/视觉元数据/装置/TaskGraph opt-in | Profile Catalog、Encounter Binding、Device Plan | 元数据实际消费；装置无旁路；生产仍显式启用 |
| DAG5-E | 建筑与弱点联合调优、DAG4 重认证、生产切换 | Certification Manifest | 六栋逐栋动态认证、PIE、性能和确定性全通过 |

## 9. 非目标与禁止项

- 不保证任意递归深度和任意预算都存在可行建筑；
- 不通过偷偷降低最低复杂度让搜索“成功”；
- 不让 WFC 直接生成最终物理砖或承重边；
- 不用同一完整方板的更多递归冒充轮廓复杂性；
- 不在 DAG5-A/B 阶段提前宣称六栋联合多样性完成；
- 不在 DAG5-D 前改 TaskGraph 生产默认；
- 不在 DAG5-E 全部门槛通过前开启生产；
- 任一阶段都禁止 DAG Reject 后回退 Legacy。

## 10. 文档所有权与后续链接

本 M7 功能工作树只维护 M7/DAG 子设计与 M7 源码。
共享的 `Docs/ABTSProjectWorkflow.md`、`Docs/AngryBirdsToSpaceGameDesign.md`、
`AGENTS.md` 和多工作树规范不在本工作树修改；需要加入全局索引时，
由集成工作树在合并阶段补链。
