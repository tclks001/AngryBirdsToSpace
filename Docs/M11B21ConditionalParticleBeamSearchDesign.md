# M11-B v2.1：条件粒子束逐星构造器（Additive Search v4）

> 状态：**Additive Search v4 构造器已实现，并完成 4 个正式种子的标准 C++ 搜索取样（3 个成功、1 个在第三阶段正常无解退出）**。v4 只新增候选构造路径；最终接受仍调用冻结的 Search v3 候选审计。4 个原始入选布局位于 Editor-only Candidate Catalog 的 Rank 3–6；Rank 3 上游映射候选 353 追加为 Rank 7，供 M11-C v2.1 PIE 手感比较。它们仍全部是 `Candidate / NOT CERTIFIED`，没有替换当前 v3 Rank 1/2，也没有进入 production 绑定。
>
> 父级：[M11-B v2.1 标准 C++ 候选布局搜索](M11B21CandidateSearchDesign.md) · [M11 v2 终局优化总设计](M11V2FinaleOptimizationDesign.md)
>
> 上游：[M11-A v2.1 标准 C++ 单一权威内核](M11AGravityAssistSolverDesign.md)
>
> 下游：[M11-C v2.1 候选手感验收](M11CFinaleInteractionAndPlaybackDesign.md) · [M11-B v2.2 全输入域认证](M11BFinaleLayoutCertificationDesign.md)

## 1. 为什么保留 v3、另加 v4

Search v3 的 4096-work 全局搜索同时改变三颗行星与目标参数，再对大量完整构造执行求解。它已经找到并冻结两个可供 Editor PIE 比较的候选，但本轮基线成本为：

- `4096` work；
- `1,938,854` 次 solver invocation；
- `2` 个 accepted candidate；
- `1.031537` accepted candidate / 百万次 solve；
- 8 个分片累计 `4912.003924 s`。

当前 v3 Candidate Source 继续保留：

| Rank | Candidate Source | nominal Request | nominal Result | Score |
| ---: | --- | --- | --- | --- |
| 1 | `0xaaae0dd44f14f785` | `0x5ecc893f6eb7003d` | `0xb47d8314ebe69376` | `0xd6e03f2d9e0f3b8b` |
| 2 | `0xe2c810b38f338e06` | `0x5c07be6f9371448e` | `0xe465b9c154c235a1` | `0xdd1613e3dbb4c1b0` |

v4 不改写 v3 的合同、Manifest 或排序语义，也不把 v3 产物标成失效。它只把“同时猜完整四体布局”改为“沿实际父前缀逐星放置、逐级筛选”，然后把少量幸存布局交还给冻结 v3 审计。两类产物使用独立 schema 和构造 Hash，可以并排保存、比较和删除。

## 2. 权威与版本

```text
M11Core（唯一积分、事件、分类与 Result Hash 权威）
  └─ M11Search
      ├─ Search v3：既有 4096-work 构造与最终候选审计
      └─ Additive Search v4：条件粒子束构造
           └─ 幸存布局 ──> 冻结 Search v3 最终审计
```

| 项 | 值 |
| --- | --- |
| Particle Beam Contract | `1` |
| Particle Beam Algorithm | `4` |
| Particle Beam Manifest | `1` |
| 嵌套 Evaluation Contract / Algorithm / Candidate Manifest | `3 / 3 / 3` |
| Production Core Source SHA-256 | `970656c1734da37f26ea9a45be4adb4befb95394cb50c6cb412c8b5e5b9fc3a0` |
| 本轮 Search Source SHA-256 | `63bb6b7706a99e9151ca315906b7c8ed98b13f75d2da057110e370716aa335d1` |

Python 不参与本构造器的积分、前缀分类、Hull、Hash 或排名。当前正式结果由标准 C++ CLI 直接生成；后续报告脚本只读 Manifest。

## 3. 条件粒子束算法

### 3.1 根参数与固定输入粒子

正式搜索从 `64` 个确定性根参数开始。每个根由 Construction Seed 与 Halton 序列确定引力范围、冲量、B-plane、首个遭遇时间和过星侧；两段构造 coast 额外限制在 `2.5–5.5 s`。尚未构造的行星和目标先停放到不会影响当前前缀的位置。

构造器只生成一次固定输入粒子集：

- `1024` 个固定种子 Halton `Yaw × Pitch` 探索点，Power 固定 nominal；
- 另加 nominal 输入作为 anchor，但不计入保留率分母；
- Assist1、Assist2、Assist3 都只在其父节点实际存活的输入成员上继续计算。

因此 S2 的分母是“真实通过该父节点 S1 的输入”，S3 的分母是“真实通过同一父节点 S2 的输入”；构造器不使用父 Hull 包含关系冒充成员资格。

### 3.2 沿父轨迹逐星放置

对第 `n` 颗行星，构造器以父 nominal 轨迹的上一颗行星 Exit 为起点，在预期下一次遭遇附近采样：

- 时间：`5` 档；
- Influence 半径：`4` 档；
- 切向 impact：`4` 档；
- 径向 offset：`3` 档；
- 虚拟动量扇区：`7` 档。

行星中心由父轨迹采样点与局部 impact basis 决定，而不是在世界坐标盒中盲猜。三颗行星分别使用逐步扩大的动量方向扇区，使后续助推能够形成明显转折与换侧。零 solve 的 Influence-shell 交叉预测先估计几何覆盖，再按空间多样性选出最多 `96` 个 nominal proposal。

Shell 交叉只是宽松预测：真正的合格前缀还要通过 B-plane、持续时间、能量、净空和可读偏转门。为避免过早把可行解剪掉，几何预测目标故意比最终期望的 `0.25` 保留率更宽；最终比例只能由实际积分后的父成员计数决定。

### 3.3 三层求解预算与 Beam

每个 Assist 的流水线固定为：

```text
几何 proposal
  → 最多 96 个 nominal solve
  → 最多 48 个 proposal × 最多 48 个父粒子的 coarse gate
  → 最多 12 个 proposal × 完整父粒子的 refinement gate
  → 位置多样性去重
  → 最多 12 个 Beam survivor
```

每一级必须满足：

- nominal 仍取得当前完整前缀；
- 实际父成员保留率处于探索宽门 `[0.05, 0.60]`；
- `[0.15, 0.40]` 为优选区，`0.25` 为评分中心；
- Hull 非退化；
- 实际偏转、轴投影、Influence 时长、coast 与换侧进入阶段评分；
- Assist3 还必须通过 `MinimumRobustSurvivorCount + 1` 的局部前缀保护，本轮即至少 `5` 个 robust prefix survivor。

Child Beam 只携带当前 proposal 的实际存活粒子及其新结果。排序和多样性 tie-break 全部确定；并行只改变任务执行时序，不改变结果顺序或 Hash。

### 3.4 目标构造、独立 Holdout 与最终审计

三颗行星完成后：

1. 先复用 v3 `BuildTarget`，以更严格的构造期 robust guard 和扩大后的覆盖 margin 生成初始目标；
2. 从 Assist3 Exit 后 `3–11 s` 的 nominal arc 上取 `9` 个时间位置，并尝试 `3` 档目标半径；
3. 要求最终布局最小转角至少为 v3 `0.30 rad`，构造时另加 `0.03 rad` guard；
4. 要求 nominal 命中且至少 `4` 个 v3 robust 输入命中，优先更大布局转角、更多 robust survivor、更小目标半径；
5. 使用与探索集种子不同的 `512` 点 Holdout，重新检查 S1–S3 的 `[0.08, 0.55]` 比例门和独立 Hull；
6. 只把最多 `6` 个 Holdout survivor 送入冻结 v3 最终审计。

冻结 v3 最终审计继续执行：

- nominal F4、节奏、布局、可读偏转和低功率门；
- ScreenAim `5000`；
- FullLaunchDomain `5000`；
- 每集 Conditional `512`；
- robust survivor；
- 四项消融；
- v3 soft score、Candidate Source/Request/Result/Score Hash。

最后再以 v3 `MinimumDiversityDistanceCM=3500` 选 Top-K。因此 v4 只提高“把什么送进昂贵审计”的效率，不降低候选接受标准。

## 4. 正式构造参数

| 参数 | 正式值 |
| --- | ---: |
| Root / Exploration / Holdout | `64 / 1024 / 512` |
| Geometry time/radius/impact/radial/momentum | `5 / 4 / 4 / 3 / 7` |
| Nominal / Coarse / Refinement proposal budget | `96 / 48 / 12` |
| Coarse particle limit / Beam width | `48 / 12` |
| 最多最终审计 | `6` |
| 构造探索保留率 | `[0.05, 0.60]` |
| 优选保留率 / 中心 | `[0.15, 0.40] / 0.25` |
| Beam 位置多样性 | `1800 cm` |
| 构造 inter-encounter coast | `2.5–5.5 s` |
| 期望实际偏转 | `0.45–0.85 rad` |
| 期望 Influence | `4–6.5 s` |
| 期望最大 coast / 总时长 | `5.5 s / 42 s` |

可复现 CLI 形式：

```powershell
& .\Intermediate\M11CoreStandalone\bin\ABTSM11SearchCLI.exe `
  particle-beam-search `
  --output <absolute-empty-output-dir> `
  --threads <N> --top-k 2 `
  --seed <construction-seed> `
  --roots 64 --beam-width 12 `
  --exploration-samples 1024 `
  --holdout-samples 512 --json
```

输出目录必须为空；CLI 同时写 `summary.json` 和 `candidates/candidate_rank_*.json`。所有种子、合同字段、Solve 分项、Aggregate、源码身份和 Candidate v3 Manifest 都进入输出。

## 5. 四个正式种子的搜索效率

v3 基线为 `1.031537` accepted / 百万次 solve。墙钟受机器负载和线程调度影响，只作诊断；solver invocation 和接受吞吐才是跨运行的主要效率指标。

| Construction Seed | Beam S1/S2/S3 | Audited | Accepted | Selected | Solver calls | Wall clock | Accepted / M solve | 相对 v3 |
| ---: | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `296882177` | `4 / 1 / 0` | `0` | `0` | `0` | `10,270` | `22.511 s` | `0` | `0×` |
| `296883201` | `7 / 10 / 5` | `2` | `2` | `2` | `50,186` | `70.823 s` | `39.8518` | `38.63×` |
| `296883202` | `6 / 11 / 6` | `2` | `2` | `2` | `47,925` | `57.258 s` | `41.7319` | `40.46×` |
| `296883217` | `6 / 4 / 5` | `5` | `5` | `2` | `83,097` | `48.051 s` | `60.1706` | `58.33×` |

关键观察：

- `296883201` 与 `296883202` 虽然构造 Hash、Score Hash 和 Solve 分项不同，最终选出的两个 Candidate Source 完全相同：`0xcdc6e41075d99493` 与 `0x80d274a67e1e9944`。这说明构造器能从相邻种子回到同一布局族。
- `296883202` 是本轮总 Solve 最少的成功搜索，仅为 v3 基线的约 `1/40.46`。
- `296883217` 因把 `5` 个候选全部送入每个 `12,059` Solve 的冻结 v3 审计，总 Solve 高于另外两次；但它得到 `5` 个 accepted candidate，接受吞吐达到 v3 的 `58.33×`，最终按布局多样性选出 `2` 个。
- `296882177` 在第三颗行星阶段以 `Stage3NominalEmpty` 正常结束；这说明 v4 显著提高成功构造的吞吐，但并非每个 Construction Seed 都保证找到解。本轮按种子计的成功率为 `3/4`，失败种子只消耗 `10,270` 次 Solve，没有进入 Holdout 或昂贵的最终审计。
- 三个成功结果都精确写 `Completed`，各自完成冻结 v3 审计并由多样化选择器保留
  `2` 个候选；没有用 rejected layout 补足名额。

## 6. 最优候选的节奏与偏转

下表列出 4 个不重复的已选布局。角度为三次实际速度偏转，不是预设过星侧或仅由行星中心形成的布局夹角。

| Seed / Rank / Candidate Source | v3 Soft | 总时长 | 最大 / 最终 coast | 三次实际偏转 | Influence 时长 | 有符号换侧 |
| --- | ---: | ---: | ---: | --- | --- | --- |
| `217 / 1 / 0xed74ffaf0de8028f` | `83.725` | `27.843 s` | `5.101 / 0.678 s` | `36.44° / 34.38° / 65.62°` | `7.574 / 6.264 / 4.985 s` | `- / + / -`，2 次 |
| `217 / 2 / 0xf22ad256fd791e07` | `83.075` | `30.086 s` | `5.101 / 0.982 s` | `36.44° / 34.38° / 60.19°` | `7.574 / 6.264 / 6.078 s` | `- / + / -`，2 次 |
| `201/202 / 1 / 0xcdc6e41075d99493` | `75.081` | `27.421 s` | `5.294 / 0.927 s` | `21.63° / 21.53° / 44.89°` | `7.528 / 5.569 / 3.904 s` | `+ / - / +`，2 次 |
| `201/202 / 2 / 0x80d274a67e1e9944` | `74.118` | `30.371 s` | `5.294 / 1.260 s` | `21.63° / 21.55° / 37.51°` | `7.528 / 5.571 / 4.630 s` | `+ / - / +`，2 次 |

对应三段布局转角：

| Candidate Source | A1 | A2 | A3 | Robust survivor |
| --- | ---: | ---: | ---: | ---: |
| `0xed74ffaf0de8028f` | `0.320220 rad` | `0.395181 rad` | `0.517993 rad` | `5` |
| `0xf22ad256fd791e07` | `0.320220 rad` | `0.333497 rad` | `0.380996 rad` | `5` |
| `0xcdc6e41075d99493` | `0.415448 rad` | `0.476262 rad` | `0.306782 rad` | `4` |
| `0x80d274a67e1e9944` | `0.374148 rad` | `0.392506 rad` | `0.372423 rad` | `4` |

数值手感代理表明：

- 4 个布局都把完整航程压在 `27.4–30.4 s`，最长空驶不超过 `5.3 s`；
- `217` 布局族的前两次偏转约 `34–36°`，第三次约 `60–66°`，比 `201/202` 布局族的前两次约 `21.5°` 更有“引力弹弓”读感；
- 每个布局都有两次真实换侧，不是三颗行星同侧平滑绕过；
- 第一段 Influence 仍约 `7.5 s`，高于 `4–6.5 s` 的满分区但没有超过冻结 v3 的 `8 s` 硬门；这应在 PIE 中重点观察。

以上只能称为数值手感代理。没有使用 M11-C 输入、相机、PIP 和轨道全景进行有渲染 PIE，因此不能宣称玩家手感已经通过。

## 7. ScreenAim 5000、Holdout 512 与 Hull

### 7.1 冻结 v3 ScreenAim 5000

下表只列 S1–S3。比例依次为 `S1/S0`、`S2/S1`、`S3/S2`；Area 和 span 都来自该级自身 5000 点 ScreenAim 成员的凸包。

| Candidate / Set | Count | 比例 | Hull area | Yaw span × Pitch span |
| --- | ---: | ---: | ---: | --- |
| `ed74… / S1` | `727` | `0.145400` | `438.934 deg²` | `21.193° × 33.955°` |
| `ed74… / S2` | `185` | `0.254470` | `101.432 deg²` | `12.858° × 17.092°` |
| `ed74… / S3` | `29` | `0.156757` | `25.603 deg²` | `6.688° × 13.970°` |
| `f22a… / S1` | `727` | `0.145400` | `438.934 deg²` | `21.193° × 33.955°` |
| `f22a… / S2` | `185` | `0.254470` | `101.432 deg²` | `12.858° × 17.092°` |
| `f22a… / S3` | `29` | `0.156757` | `32.780 deg²` | `8.086° × 14.970°` |
| `cdc6… / S1` | `538` | `0.107600` | `309.103 deg²` | `18.562° × 29.011°` |
| `cdc6… / S2` | `202` | `0.375465` | `171.460 deg²` | `15.181° × 24.402°` |
| `cdc6… / S3` | `45` | `0.222772` | `65.288 deg²` | `9.940° × 22.140°` |
| `80d2… / S1` | `538` | `0.107600` | `309.103 deg²` | `18.562° × 29.011°` |
| `80d2… / S2` | `196` | `0.364312` | `169.552 deg²` | `15.181° × 24.402°` |
| `80d2… / S3` | `49` | `0.250000` | `64.121 deg²` | `8.429° × 21.262°` |

全部 S1–S3 比例落在冻结 v3 `[0.08, 0.55]` 硬门内，主 ScreenAim Hull 均非退化并包含 nominal。`217` 布局族在 S2 达到接近期望的 `1/4`，但 S1 与 S3 约为 `14–16%`，仍小于玩法草案中每级约 `1/4` 的理想值；它们是可比较候选，不是已经达到精确面积目标的最终答案。

### 7.2 独立 Holdout 512

| Candidate Source | S1 count / ratio | S2 count / ratio | S3 count / ratio |
| --- | --- | --- | --- |
| `0xed74ffaf0de8028f` | `74 / 0.144531` | `20 / 0.270270` | `5 / 0.250000` |
| `0xf22ad256fd791e07` | `74 / 0.144531` | `20 / 0.270270` | `4 / 0.200000` |
| `0xcdc6e41075d99493` | `53 / 0.103516` | `22 / 0.415094` | `6 / 0.272727` |
| `0x80d274a67e1e9944` | `53 / 0.103516` | `21 / 0.396226` | `3 / 0.142857` |

Holdout 与构造探索使用不同种子，4 个布局都再次通过 S1–S3 比例与 Hull 宽门。但 S3 只有 `3–6` 个样本，足以排除明显退化，不足以给出高精度面积估计，也不能证明成功集连通。

### 7.3 FullLaunchDomain 5000 诊断

| Candidate Source | S1 / S2 / S3 / S4 count | 嵌套比例 |
| --- | --- | --- |
| `0xed74ffaf0de8028f` | `78 / 21 / 1 / 1` | `0.015600 / 0.269231 / 0.047619 / 1.000000` |
| `0xf22ad256fd791e07` | `78 / 21 / 1 / 1` | `0.015600 / 0.269231 / 0.047619 / 1.000000` |
| `0xcdc6e41075d99493` | `73 / 34 / 10 / 6` | `0.014600 / 0.465753 / 0.294118 / 0.600000` |
| `0x80d274a67e1e9944` | `73 / 32 / 9 / 3` | `0.014600 / 0.438356 / 0.281250 / 0.333333` |

FullLaunchDomain 包含 Power，只是诊断语料，不参与 v2.1 接受。`217` 布局族在 5000 个三维点中只有 1 个 S3，说明它对 Power/角度联合变化较敏感；这正是必须保留可调 Power 并在 v2.2 做完整输入域认证的原因，而不是可以忽略 Power 维度的证据。

## 8. 结果分析与下一步

当前数值排序建议：

1. `0xed74ffaf0de8028f`：偏转最强、总时长短、S2 接近 `1/4`，优先作为新的 PIE 手感候选；
2. `0xf22ad256fd791e07`：同一前两级成功集、第三次偏转略弱但 Influence 更平缓，作为同族 A/B 对照；
3. `0xcdc6e41075d99493`：三层成功岛面积更大、第三层 Holdout 更稳定，但前两次偏转较弱，适合作为“可瞄准性优先”对照；
4. `0x80d274a67e1e9944`：同族备用，不优先于前三者。

其中 `ed74…` 的 ScreenAim Hull 面积为
`438.934 → 101.432 → 25.603 → 7.703 deg²`，逐级面积比例为
`23.1% → 25.2% → 30.1%`；这比单看离散成员比例更接近“每次约缩到
1/4”的屏幕空间目标。Hull 仍可能跨过内部失败点，所以这一结论只能说明
外包络递进形状合适，不能把 Hull 内部全部当作成功集。

仍有四项明确限制：

- v4 原始候选已作为 Editor-only Rank 3–6 写入 Candidate Catalog，候选 353 追加为 Rank 7；Rank 1/2 仍保留为 v3 基线，Rank 0 仍是 production Certified v1；
- 没有完成 M11-C v2.1 有渲染 PIE，故“手感”只通过数值代理；
- 5000 点 Monte Carlo 与凸包不证明唯一连通分量，也不证明 Hull 内部处处成功；
- 没有执行 M11-B v2.2 的完整 `Yaw × Pitch × Power` 认证、边界细化、错序/迟到排除与全消融唯一性证明。

正确交接顺序不变：

```text
保留 v3 当前候选
  → 选择少量 v4 Candidate 做 Editor-only Catalog 候选更新
  → M11-C v2.1 有渲染 PIE 比较
  → 若手感不合格，调构造参数并重搜
  → 用户批准后冻结唯一布局/输入合同
  → M11-B v2.2 完整输入域认证
  → M11-C v2.2 正式绑定
```

### 8.1 Editor PIE 候选选择

停止 PIE 后在编辑器输出日志命令框设置 Rank，再重新启动 PIE：

| 命令 | 布局 |
| --- | --- |
| `abts.M11.CandidateRank 0` | production Certified v1 |
| `abts.M11.CandidateRank 1` | v3 基线 `0xaaae0dd44f14f785` |
| `abts.M11.CandidateRank 2` | v3 基线 `0xe2c810b38f338e06` |
| `abts.M11.CandidateRank 3` | v4 `0xed74ffaf0de8028f` |
| `abts.M11.CandidateRank 4` | v4 `0xf22ad256fd791e07` |
| `abts.M11.CandidateRank 5` | v4 `0xcdc6e41075d99493` |
| `abts.M11.CandidateRank 6` | v4 `0x80d274a67e1e9944` |
| `abts.M11.CandidateRank 7` | Rank 3 上游映射候选 353 `0xb3e0f00ca35d499a`；Core 临时分类下 half-cell F4 单连通岛，但名义轨迹为 TargetHit 早于 Assist3 Exit，仅供 PIE 研究 |

Rank 3–7 冻结完整布局与 Candidate 身份，PIE 启动时只做结构和
Candidate Source Hash 校验，不重新执行粒子束搜索。修改 Rank 后必须停止并重启
PIE，因为 GameMode 只在终局系统初始化时读取该 CVar。

## 9. 本轮可复现与自动化证据

- Production Core / Search Source SHA-256：
  `970656c1734da37f26ea9a45be4adb4befb95394cb50c6cb412c8b5e5b9fc3a0` /
  `63bb6b7706a99e9151ca315906b7c8ed98b13f75d2da057110e370716aa335d1`；
- 标准 C++ clean Release + CTest：`5/5`，其中已注册的
  `M11Search.ParticleBeamSuccessfulFixture` 完成 `83,097` 次 solve，
  `Contract=0xaccb3830e7ed8d7e`，整链通过；
- Development Editor 全链接：`Succeeded`；fresh
  `ABTS.M11B.V2_1`：`5/5`，唯一完成标记且无自动化错误，日志
  `Saved/Logs/M11B21-V4-Final63b-20260730-191819-FreshAutomation.log`；成功夹具
  以 `1 / 4` 线程分别完成整链，并逐项核对账本、Aggregate 与有序候选身份；
- 两个正式成功根的只读报告通过 fail-closed Manifest 串联检查，报告 Hash 为
  `5f2823160252bd3caea66af71ffd3d6feb4c80b8daef4d8f66fa3615485c8c9b`，
  Cross-root Identity 为
  `b80c34b45535d16c2ea43053ac3011d6443a6bacb63aaadcea8070df2e64a655`，
  报告工具自身 SHA-256 为
  `2dc939c34223edc87f975128426f6ff61ffe820c230c25a9586fefef0eec2980`；
- 同一输入连续生成两次报告，15 个 JSON/CSV/Markdown/SVG/PNG 产物逐文件
  SHA-256 全部一致。summary 还会权威列出有序 TopCandidates，报告逐 rank
  校验候选文件相对路径、完整字节数与精确字节 Hash。报告脚本只读 Manifest，
  不重算积分、不改分类、不参与排序。

返回父级：[M11-B v2.1 候选搜索](M11B21CandidateSearchDesign.md) · [M11 v2 优化总设计](M11V2FinaleOptimizationDesign.md)。
