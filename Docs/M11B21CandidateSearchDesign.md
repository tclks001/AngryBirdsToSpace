# M11-B v2.1：标准 C++ 候选布局搜索、前缀成功集与快速同源重放

> 状态：**Search Contract / Algorithm / Candidate Manifest v3、权威 4096-work 搜索与全局 merge、Editor Catalog 重冻以及最终 UBT/fresh-process 自动门均已完成；其上另以不覆盖 v3 的 Additive Search v4 条件粒子束构造器完成 4 个正式种子搜索（3 个成功、1 个正常无解退出）**。当前 Editor Catalog 仍保留本稿 v3 Rank 1/2；v4 结果尚未绑定，只是新的 `Candidate / NOT CERTIFIED` 手感比较池。本阶段不执行 M11-B v2.2 完整输入域认证，不生成正式认证 Hash，也不改变 production v1。
>
> 父级：[M11 v2 终局引力弹弓优化总设计](M11V2FinaleOptimizationDesign.md)
>
> 上游：[M11-A v2.1 标准 C++ 单一权威内核](M11AGravityAssistSolverDesign.md)
>
> 下游：[M11-C v2.1 终局交互与确定性播放](M11CFinaleInteractionAndPlaybackDesign.md)
>
> 正式认证：[M11-B 终局布局搜索与全输入域认证](M11BFinaleLayoutCertificationDesign.md)
>
> 增量构造器：[Additive Search v4 条件粒子束逐星构造器](M11B21ConditionalParticleBeamSearchDesign.md)

## 1. 阶段目标与边界

M11-B v2.1 回答的是：

> 哪些紧凑、强偏转、非共线、节奏不拖沓，并且具有逐级收窄成功区的局部布局，值得进入 M11-C v2.1 PIE 手感比较？

它不回答：

> 完整 `Yaw × Pitch × Power` 输入域中是否只存在唯一一族解？

后一个问题仍由 M11-B v2.2 的完整认证负责。本阶段的 5000 点撒点、比例和凸包只能作为候选级统计证据，不能代替像素/半格边界细化、连通分量证明、旁路/错序/迟到排除和全消融证明。

本阶段交付：

- 不依赖 Unreal 的 `ABTS::M11Search` 标准 C++ 搜索层；
- 与 production 共用相同 `M11Core` 求解器源文件的 `ABTSM11SearchCLI`；
- 确定性全局 work index、分片、检查点、恢复、合并和 Top-K；
- 三段真实偏转及左右换侧可读性度量；
- 两套各 5000 点、职责严格分开的低差异输入语料；
- S1、S2、S3 的比例硬门和各自独立的二维凸包硬门；
- 完整自描述 Manifest、工具/源码身份和合并 Aggregate；
- Python 仅负责 CLI 进程调度，不积分、不分类、不计算 Hash、不排名。

所有输出均为 `Candidate / NOT CERTIFIED`，`CertificationHash`、`CertifiedBundleHash` 和临时 Trust Region Hash 均保持零。

## 2. 权威结构与版本

```text
M11Core（标准 C++ 唯一积分、事件与 Hash 权威）
  └─ M11Search（构造、硬门、统计、软评分、排名）
      └─ ABTSM11SearchCLI
          ├─ --describe-contract
          ├─ search：分片、检查点、evaluation、Candidate
          └─ merge：验证分片、重算 Accepted、全局排名

Python/m11_search.py
  ├─ 冻结 orchestration_plan.v3
  ├─ 并行启动 CLI
  └─ 不实现任何物理或判定语义
```

| 项 | 当前值 |
| --- | --- |
| `SearchContractVersion` | `3` |
| `SearchAlgorithmVersion` | `3` |
| `CandidateManifestVersion` | `3` |
| `contract_descriptor` envelope | `abts.m11b21.contract_descriptor.v1` |
| Production Core Source SHA-256 | `970656c1734da37f26ea9a45be4adb4befb95394cb50c6cb412c8b5e5b9fc3a0` |

Search v2→v3 会使旧 Candidate、Score/Source Hash、plan/checkpoint、Aggregate 和 Editor Candidate Catalog 身份全部失效，必须重新搜索和冻结；production v1 Solver/Bundle 不受影响。

## 3. 构造阶段的偏转可读性

### 3.1 交替侧向只是构造偏好，真实偏转才是最终证据

每个 work item 由 Halton base 73 确定一个严格交替的首选过星侧：

```text
+ / - / +
或
- / + / -
```

在 Assist2、Assist3 构造阶段，搜索器直接复用已有 nominal 求解结果，按与最终 `PopulateMetrics` 完全相同的表现平面法线和入/出速度叉积，计算：

- `SignedLateralTurnRadians[3]`；
- `PartialAlternationCount`。

鲁棒门之后，Assist2 优先保留能形成第一次真实换侧的候选；Assist3 优先保留真实换侧次数最大的候选，能得到两次时淘汰零次/一次候选。该引导不增加任何 solve，也不会用“预设的过星侧标签”冒充真实偏转。

### 3.2 最终可读性门

最终候选仍必须同时满足：

- 每颗行星真实速度偏转 `>= 0.30 rad`；
- 每颗偏转轴在最终展示平面法线上的绝对投影 `>= 0.25`；低于该门槛的三维转弯即使数值角度足够，也因二维全景中不可读而拒绝；
- 最少一次相邻真实侧向符号翻转；
- 三颗行星布局最小转角 `>= 0.30 rad`；
- 总时长 `<= 60 s`；
- 最长无明显引力偏转 coast `<= 11 s`；
- 每段行星影响时长在 `3–8 s`；
- 四项行星消融均不能命中目标。

软评分继续奖励更大的可读偏转和两次换侧，因此两次换侧的 `-/+/-`、`+/-/+` 会优先于只换侧一次的候选。

### 3.3 低功率门

`Power=0.90` 的轨迹允许擦入扩大后的 Influence shell；扩大引力范围后，禁止任何 shell enter 会与目标体验矛盾。硬门拒绝的是：

- 已取得合格 Assist1 前缀；或
- 已命中目标。

换言之，低功率可以被行星轻微影响，但不能真正“用上”第一颗行星形成合格助推。构造阶段与最终候选门共用同一个 `ShouldRejectLowPowerResult` 判定，避免两处语义漂移。

## 4. 两套 5000 点输入语料

Launch 域与 M6 线性功率语义一致：

| 维度 | 范围 |
| --- | --- |
| Yaw | `[-18°, +18°]` |
| Pitch | `[0°, 60°]` |
| Power | `[0, 1]` |
| 速度 | `lerp(900, 2300, Power) cm/s` |
| nominal | `Yaw=0° / Pitch=30° / Power=1` |

### 4.1 ScreenAim 5000：候选手感权威

固定种子 Halton base 2/3 在完整 Yaw/Pitch 可行范围撒 5000 点，Power 固定为 `NominalInput.Power`。它是以下项目的唯一权威数据源：

- `S1/S0`、`S2/S1`、`S3/S2` 的候选级成功点比例；
- S1、S2、S3 的独立 Yaw-Pitch 凸包；
- Prefix retention 与 Prefix hull 的 UX 评分。

这与玩家当前候选手感问题相匹配：先观察近最大功率下，鼠标二维可行域是否呈现合理的逐级收窄关系。

### 4.2 FullLaunchDomain 5000：Power 维度诊断

另一套固定种子 Halton base 2/3/5 在完整 `Yaw × Pitch × Power` 域独立取 5000 点，只记录：

- S1–S4 嵌套计数；
- 逐级保留率；
- Power 维度是否出现明显异常的快速诊断。

它不参与候选接受、二维凸包或 UX 排名。5000 个三维点无法证明完整输入域唯一性，因此不得把它写成 M11-B v2.2 认证。

### 4.3 Conditional 512：局部诊断

每个集合另有 512 个确定性局部探针，用于解释稀疏父集附近发生了什么。Conditional：

- 始终单独标记为 local evidence；
- 不并入 ScreenAim 或 FullLaunchDomain；
- 不改变比例、凸包、UX 分数或候选接受结果。

### 4.4 fail-closed

两套权威样本都必须精确完成 5000 次 Build+Solve。任何一次失败都会立即拒绝该 work item，并记录域、样本序号和失败原因；不得把 4999 个成功样本报告成 5000。

## 5. 前缀成功集比例与独立凸包

集合严格嵌套：

- S1：取得第一颗行星合格助推；
- S2：依次取得第一、第二颗行星合格助推；
- S3：依次取得三颗行星合格助推；
- S4：S3 且命中候选目标，仅作诊断。

S1、S2、S3 的 ScreenAim 逐级比例必须先通过硬门：

```text
0.08 <= |Sn| / |Sn-1| <= 0.55
```

`[0.15, 0.40]` 是 Prefix retention 软评分的满分平台；硬边界与满分平台之间线性衰减。这样既能拒绝“第一颗行星几乎占满全部鼠标范围”，也不会强迫每一级精确等于 `1/4`。

比例门通过后，才分别对 S1、S2、S3 的 ScreenAim 成员构造凸包。凸包证据：

- 只含对应的无偏 ScreenAim 命中点；
- 不加入 nominal anchor；
- 不加入 FullLaunchDomain 点；
- 不加入 Conditional 点；
- 不把多个成功集混成一个凸包。

当前宽几何硬门为：

| 项 | 门槛 |
| --- | ---: |
| 证据点 | `>= 3` |
| Hull 顶点 | `>= 3` |
| 面积 | `>= 0.0001 deg²` |
| Yaw span | `>= 0.01°` |
| Pitch span | `>= 0.01°` |

这些门只排除点/线退化；成功岛连通性和唯一性仍留给 v2.2。

## 6. 评分、选择与候选冻结

产品硬门、低功率门、局部鲁棒门和四项消融门全部通过后，才执行双 5000 与 Conditional，避免把固定成本乘到每个构造 stage。

软评分为 `0..100`：

| 分量 | 权重 |
| --- | ---: |
| ScreenAim prefix retention | 30 |
| ScreenAim independent hull | 20 |
| 真实偏转可读性 | 25 |
| 实际侧向交替 | 15 |
| 60 秒内节奏 | 10 |

全局 merge 先按软评分和确定性 tie-break 排名，再按 `MinimumDiversityDistanceCM` 去除近重复布局。只有 merge 后的全局候选能够进入 Editor Candidate Catalog。

## 7. CLI、Python 与来源证明

```powershell
& .\Intermediate\M11CoreStandalone\bin\ABTSM11SearchCLI.exe `
  --describe-contract --json

python .\Tools\M11Core\Python\m11_search.py run `
  --output .\Intermediate\M11B21V3CandidateSearch `
  --work-items 4096 --shards 8 --threads-per-shard 2 `
  --top-k 8 --checkpoint-every 8
```

v3 输出包含：

- canonical contract JSON 与 FNV-1a contract hash；
- production Core/Search 源文件清单与 SHA-256；
- 编译器、架构、C++ 标准、浮点和优化合同；
- 完整 launch/layout/scenario/target/solver 参数；
- `orchestration_plan.v3`；
- `checkpoint/evaluation/shard_summary/merge_summary/candidate.v3`；
- 两套 5000 sampling semantics、失败计数、选择范围和 Aggregate。

Resume/merge 会校验可执行文件 SHA、Core/Search Source SHA、Seed、work 数、分片身份、JSONL 字节数和内容 Hash；任一不匹配均 fail closed。

## 8. 本轮搜索结果

> 本节只登记当前 v3 统一搜索结果。旧 v1/v2 work、排名、Aggregate 和 Catalog 身份已失效，不得用于当前 PIE。

权威产物为 `Intermediate/M11B21V3ReadableGate_4096/merged/summary.json` 与同目录 `candidates/*.json`。merge 已完整回放全部分片并得到：

| 项 | 权威值 |
| --- | --- |
| Summary schema / 状态 | `abts.m11b21.merge_summary.v3` / `passed=true` / `Completed` |
| Work / Shard | `4096` work，`8` shards，`4096/4096` evaluated |
| Accepted / Selected | `2 / 2`（请求上限为 `5`，没有用未通过硬门的结果补足名额） |
| Solver 调用 | 分片 `1,938,854`；merge replay `26,511` |
| 分片累计执行时间 | `4912.003924 s`；`394.717519` solver invocations / cumulative shard second |
| Evaluation Aggregate | `0xac04988c81e25849` |
| Candidate Aggregate | `0xbfeaae4610d4c406` |
| Contract Hash | `0x1e9f208e738a6ef7` |
| Search Source SHA-256 | `27269434b7dff48c26149179776589faa67f2c0ef428849a4833e49deb817738` |
| Production Core Source SHA-256 | `970656c1734da37f26ea9a45be4adb4befb95394cb50c6cb412c8b5e5b9fc3a0` |
| 合同版本 | Search Contract `3` / Algorithm `3` / Candidate Manifest `3` |

两个候选的四类冻结身份如下；四类身份分别是 Candidate Source、nominal Request、nominal Result 与 Score，任一不匹配都必须 fail closed：

| Rank | Work | CandidateId | Candidate Source | nominal Request | nominal Result | Score |
| ---: | ---: | --- | --- | --- | --- | --- |
| 1 | `2278` | `m11b21-aaae0dd44f14f785` | `0xaaae0dd44f14f785` | `0x5ecc893f6eb7003d` | `0xb47d8314ebe69376` | `0xd6e03f2d9e0f3b8b` |
| 2 | `772` | `m11b21-e2c810b38f338e06` | `0xe2c810b38f338e06` | `0x5c07be6f9371448e` | `0xe465b9c154c235a1` | `0xdd1613e3dbb4c1b0` |

节奏与几何摘要：

| Rank | 总时长 | 最长 coast / 末段 coast | 总影响时长 | 三段布局转角 | 最小可读偏转 | 换侧次数 | Robust survivor |
| ---: | ---: | ---: | ---: | --- | ---: | ---: | ---: |
| 1 | `31.268136 s` | `5.065894 / 1.967285 s` | `19.340426 s` | `0.377665 / 0.533365 / 0.385783 rad` | `0.304224 rad` | `2` | `4` |
| 2 | `31.223673 s` | `4.513232 / 2.050117 s` | `17.604061 s` | `0.348798 / 0.346212 / 0.386060 rad` | `0.393788 rad` | `1` | `4` |

三段真实速度偏转、在最终表现平面内的有符号换侧、偏转轴投影和 Influence 时长为：

| Rank / Assist | 实际偏转 | 有符号侧向偏转 | 轴投影绝对值 | Influence 时长 | 入圈前 coast |
| --- | ---: | ---: | ---: | ---: | ---: |
| 1 / A1 | `0.590804 rad`（`33.85°`） | `+0.590804 rad` | `0.934093` | `7.923236 s` | `4.333189 s` |
| 1 / A2 | `0.306536 rad`（`17.56°`） | `-0.306536 rad` | `0.992458` | `7.507412 s` | `0.561342 s` |
| 1 / A3 | `0.645047 rad`（`36.96°`） | `+0.645047 rad` | `0.489981` | `3.909778 s` | `5.065894 s` |
| 2 / A1 | `0.404215 rad`（`23.16°`） | `-0.404215 rad` | `0.974204` | `7.917075 s` | `2.597333 s` |
| 2 / A2 | `0.552443 rad`（`31.65°`） | `+0.552443 rad` | `0.971059` | `4.107608 s` | `4.458930 s` |
| 2 / A3 | `0.628360 rad`（`36.00°`） | `+0.628360 rad` | `0.976836` | `5.579378 s` | `4.513232 s` |

因此 Rank 1 呈 `+ / - / +` 两次真实换侧，Rank 2 呈 `- / + / +` 一次真实换侧；两者每段轴投影均超过合同门槛 `0.25`，不是只在三维中转弯、投到轨道全景后消失的假可读解。

ScreenAim 权威样本均为独立的固定种子 5000 点 Yaw/Pitch 撒点，Power 固定 nominal。下表中的比例按嵌套前缀计算，即 `S1/S0`、`S2/S1`、`S3/S2`：

| Rank / Set | 命中数 | 前缀比例 | Hull 证据点 | Area | Yaw span | Pitch span | Normalized area / compactness | nominal / 合规 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | --- |
| 1 / S1 | `660` | `0.132000` | `660` | `331.607413 deg²` | `16.040039°` | `38.826398°` | `0.153521951 / 0.532466` | 包含 / 是 |
| 1 / S2 | `74` | `0.112121` | `74` | `66.516190 deg²` | `8.156250°` | `21.243713°` | `0.030794533 / 0.383890` | 包含 / 是 |
| 1 / S3 | `26` | `0.351351` | `26` | `19.657068 deg²` | `6.093018°` | `14.659351°` | `0.009100494 / 0.220075` | 包含 / 是 |
| 2 / S1 | `544` | `0.108800` | `544` | `393.512510 deg²` | `13.385742°` | `48.212163°` | `0.182181718 / 0.609761` | 包含 / 是 |
| 2 / S2 | `96` | `0.176471` | `96` | `55.771759 deg²` | `5.449219°` | `21.444902°` | `0.025820259 / 0.477261` | 包含 / 是 |
| 2 / S3 | `8` | `0.083333` | `8` | `1.119872 deg²` | `1.054688°` | `4.983996°` | `0.000518459 / 0.213043` | 包含 / 是 |

两候选的 S1–S3 比例均落在 `[0.08, 0.55]` 硬门内，且每一级 Hull 都只由该级 ScreenAim 成员构成、证据点数与成员数相等、包含 nominal 并通过宽几何门。Rank 2 的 S3 已接近 `0.08` 下界，因此尤其需要 PIE 检查是否过窄；这些统计不证明连通性或唯一性。

FullLaunchDomain 是另一套固定种子 5000 点 `Yaw × Pitch × Power` 诊断语料，不参与接受、Hull 或排序：

| Rank | S1 | S2 | S3 | S4 target | 嵌套比例 `S1/S0, S2/S1, S3/S2, S4/S3` |
| ---: | ---: | ---: | ---: | ---: | --- |
| 1 | `96` | `4` | `2` | `1` | `0.019200, 0.041667, 0.500000, 0.500000` |
| 2 | `102` | `2` | `0` | `0` | `0.020400, 0.019608, 0, 0` |

每个候选的 ScreenAim 5000 与 FullLaunchDomain 5000 都完整执行，二者 `solveFailureCount=0`；每集 512 点 Conditional 诊断也为 `solveFailureCount=0`。低功率探针记录的原始 `CompletedAssistCount` 分别为 `3` 和 `2`，但该字段只记录 encounter 完成数，不等于合格助推；两个候选均因“低功率未取得合格 Assist1 前缀且未命中目标”而通过正式低功率判定。

这两个产物仍明确写入 `certification.status=not-certified`，`CertificationHash=0x0000000000000000`，`CertifiedBundleHash=0x0000000000000000`。4096-work 随机/低差异候选统计只足以决定“值得进入 Editor PIE 比较”，不能替代 M11-B v2.2 的完整输入域、连通分量、边界细化、错序/迟到排除与逐星消融认证。

### 8.1 Additive Search v4 不覆盖本节 v3 结果

为验证“沿父前缀逐星放置行星并逐级收窄”的效率，后续新增了条件粒子束构造器。它使用独立的 Particle Beam Contract/Algorithm/Manifest `1/4/1`，但最终仍调用本稿冻结的 Search v3 接受门。

三个成功种子分别以 `47,925–83,097` 次 solve 得到 `2/2/5` 个 accepted candidate，接受吞吐为 v3 4096-work 基线的 `38.63× / 40.46× / 58.33×`；另一个种子在 Stage 3 正常无解退出，只消耗 `10,270` 次 solve。其中 Seed `296883217` 选出的强偏转候选把三次实际偏转提高到约 `36.44° / 34.38° / 65.62°`，总时长为 `27.843 s`；但仍未进入 Catalog，也没有做 PIE 或 v2.2 认证。

v4 算法、四个种子的完整效率数据、4 个不重复候选的时长/偏转、ScreenAim 5000、Holdout 512、FullLaunchDomain 5000 与 Hull 分析见 [Additive Search v4 子稿](M11B21ConditionalParticleBeamSearchDesign.md)。在显式 Catalog 更新前，本节 v3 Rank 1/2 仍是当前 Editor-only PIE 基线。

## 9. 自动化与验收门

代码门：

- 固定 MSVC 14.44、C++20、`/fp:precise` 的 portable Configure/Build/CTest；
- `--self-test` 两次重放位级确定；
- construction policy 覆盖 `+/-/+` 的 0/1/2 次部分换侧；
- 两套样本各精确 5000、零 solve failure；
- ScreenAim 与 FullDomain 分别保持 S1–S4 嵌套；
- ScreenAim 比例硬门先于 hull；
- hull evidence count 恒等于 ScreenAim member count；
- FullDomain/Conditional 改变不得影响 UX 评分；
- v3 Manifest、checkpoint 和 merge replay 身份闭合；
- Candidate Catalog 对冻结 v3 身份重建并与 UE facade 同源重放；
- v4 Contract/Hash、非法参数 fail-closed、1/4 线程确定性及阶段 Solve 账本闭合。

本轮代码门结果：

| 门禁 | 结果 |
|---|---:|
| 标准 C++ clean Release + CTest | `5/5` |
| Development Editor 默认全链接（当前 v4） | 通过 |
| Development Editor 强制 Unity 全链接（v3 归档） | 通过 |
| `ABTS.M11B.V2_1`（当前 v4） | `5/5` |
| `ABTS.M11B.Unit / Runtime`（v3 归档） | `8/8 + 4/4` |
| `ABTS.M11C.Unit / Runtime / V2_1` | `8/8 + 2/2 + 2/2` |
| `ABTS.M11A.V2_1 / M110 / Contracts.WorldGeneration` | `1/1 + 4/4 + 2/2` |

v3 收口归档共 `33/33` 项 fresh-process 自动化成功；当前 v4 增量另通过 fresh `ABTS.M11B.V2_1 5/5`，日志为 `Saved/Logs/M11B21-V4-Final63b-20260730-191819-FreshAutomation.log`。Portable clean Release + CTest 为 `5/5`，已注册的 particle-beam 成功夹具以 `Contract=0xaccb3830e7ed8d7e` 完成整条成功链；UE 夹具还完成 `1 / 4` 线程整链一致性对照。未在本次增量提交中重跑的 v3/M11-C/上游行不冒充当前源码的新鲜证据。阶段完成前仍需用户按 M11-C v2.1 清单完成有渲染 PIE 手感验收。

PIE 手感通过前不启动 M11-B v2.2 完整认证。任何候选参数调整都会使对应 source/result/score Hash 和 Catalog 身份失效，必须重新搜索、merge 和冻结。
