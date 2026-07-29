# M11-B v2.1：标准 C++ 候选布局搜索与快速同源重放

> 状态：**已实现并通过自动验收**。标准 C++ 搜索器、独立 CLI、Python 标准库分片调度、断点恢复、C++ 权威合并、4 个 Candidate Manifest、UE 快速同源重放均已落地。下一阶段为 [M11-C v2.1 候选手感循环](M11CFinaleInteractionAndPlaybackDesign.md)；本阶段明确**没有**执行 M11-B v2.2 完整输入域认证。
>
> 父级：[M11 v2 终局引力弹弓优化总设计](M11V2FinaleOptimizationDesign.md) · [M11-B 终局布局搜索与全输入域认证](M11BFinaleLayoutCertificationDesign.md)。
>
> 上游：[M11-A v2.1 标准 C++ 单一权威内核](M11AGravityAssistSolverDesign.md)。
>
> 返回总预演：[M11 三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md)。

## 1. 本阶段交付边界

M11-B v2.1 只回答“哪些紧凑、强偏转、非共线的局部布局值得进入 PIE 体验”，不回答“完整输入域内是否只有一族解”。

本阶段已经交付：

- 与 Unreal 无关的 `ABTS::M11Search` 标准 C++ 搜索层；
- 共同编译生产 `M11Core` 的 `ABTSM11SearchCLI`；
- 确定性工作索引、分片、原子检查点、严格身份校验和断点恢复；
- 仅负责编排进程的 Python 标准库脚本；
- 由 C++ 重新求值、全局去重和排序后签发的 Candidate Manifest；
- 在 UE 中重建候选并比较 Core/UE 全结果的快速同源回放门。

本阶段没有交付：

- `Yaw × Pitch × Power` 全输入域扫描、半格发现、递归边界细化或唯一连通分量证明；
- 正式 Trust Regions、`CertificationHash` 或 `CertifiedBundleHash`；
- M11-C 鼠标手感、轨迹 HUD、前缀成功集稳定器或确定性播放；
- Shipping/生产默认布局绑定；
- Python、NumPy、CuPy 或 CUDA 版积分器。

因此所有输出的状态只能是 `Candidate`。Manifest 中 `certificationHash` 与 `certifiedBundleHash` 均保持零值。

## 2. 权威结构

```text
M11Core（标准 C++，唯一积分/事件/Hash 权威）
  └─ M11Search（标准 C++，候选构造、门禁、排名）
      └─ ABTSM11SearchCLI
          ├─ search：确定性分片、检查点、局部 Candidate
          └─ merge：校验全部分片、重算 Accepted、全局排序并签发 Manifest

Python/m11_search.py
  ├─ 写入不可变 plan.json
  ├─ 并行启动 CLI 分片
  ├─ 调用 CLI merge
  └─ 不计算轨迹、不分类、不排名、不生成权威 Hash
```

生产 Core 源码身份仍为：

```text
970656c1734da37f26ea9a45be4adb4befb95394cb50c6cb412c8b5e5b9fc3a0
```

本次冻结的搜索工具源码身份为：

```text
3016fde8c759d48d584cb7f5b8bd3de568b3ad7127115b15bb53e6d5b2b1d79d
```

搜索工具和 Python 脚本发生变化会改变 Search Source Hash，但不会伪装成生产 Core 算法变化。

## 3. v2.1 候选合同

### 3.1 发射模型

候选搜索直接使用 M6 的线性功率语义：

| 项 | 值 |
| --- | --- |
| Yaw | `[-18°, +18°]` |
| Pitch | `[0°, 60°]` |
| Power | `[0, 1]`，连续可调 |
| 发射速度 | `lerp(900, 2300, Power) cm/s` |
| nominal | `Yaw=0° / Pitch=30° / Power=1.0` |
| 成功模拟时限 | `60 s` |

`Power=0.90` 只比最大功率速度低约 6%，所以快速门采用“不得完成三助推并命中”的可验证合同，不伪造“绝对碰不到第一颗行星”的错误结论。第一颗行星仍通过较远首遇时间和窄走廊，使低功率探针最多完成 1–2 次助推。

### 3.2 产品硬门

| 门 | v2.1 值 |
| --- | --- |
| 总飞行时间 | `<= 60 s` |
| 最长无作用圈 coast | `<= 11 s` |
| 单星 Influence 停留 | `[3, 8] s` |
| 每次实际偏转 | `>= 0.30 rad` |
| 每次正能量增益 | `>= 50,000 cm²/s²` |
| 每次走廊质量 | `>= 0.05` |
| 相邻布局折线最小转角 | `>= 0.30 rad` |
| 行星碰撞净空 | `>= 1,200 cm` |
| `Power=0.90` | 不得完成三助推或命中 |
| nominal 邻域 | `Yaw/Pitch ±0.25°`、`Power -0.005`；实际最多 6 点，至少 4 点存活 |
| 消融 | Mask `0x6 / 0x5 / 0x3 / 0x0` 均不得命中 |

nominal 位于 Power 上边界，因此 `Power + 0.005` 合法地落在声明域外；这里是 6 点单侧快速鲁棒门，不冒充 M11-B v2.2 的三维闭包认证。

搜索范围包括：

- Influence 半径 `8,000–17,000 cm`；
- 三星基础 `Mu = 8e9 / 1.4e10 / 2.4e10 cm³/s²`，再乘 `0.7–2.2` 确定性比例；
- 虚拟动量速度 `2,200–5,200 cm/s`；
- 合格终端拦截半径 `4,500–12,000 cm`。

最后一项是 v2.1 的**合格接近球**，不是 UFO 的 `800 cm` 几何接触球。物理 UFO 接管与最终完整认证仍分别属于 M11-C v2.1/v2.2 和 M11-B v2.2。

## 4. 构造算法

每个全局工作索引用固定 Seed 的 Halton 维度生成一组半径、`Mu` 比例、虚拟动量、影响参数和相遇节奏；线程完成顺序不进入身份。

单个工作项按以下顺序构造：

1. 用 nominal 发射生成不含有效助推的初始弧；
2. 在弧上按首遇时间放置行星①，枚举局部时间、B-plane 两轴偏置、飞越侧和虚拟动量方向；
3. 用原始 encounter 反推出 B-plane 中心和允许侧，再以冻结合同重放；
4. 对 nominal 邻域作逐级鲁棒筛选，只保留正能量、正确侧、足够偏转、停留时间和净空均合格的前缀；
5. 沿行星①出口后的权威弧放置行星②，再沿行星②出口后的弧放置行星③；
6. 阶段排名同时考虑既有布局转角和“上一行星→当前行星→当前出射方向”的预测转角，避免再次收敛成近直线；
7. 在行星③出口后 `3–7 s` 的五个确定性时间切片构造合格目标球，以覆盖 nominal 和至少四个局部探针，并优先最大化完整折线最小转角；
8. 执行完整 nominal 节奏门、低功率门、局部鲁棒门和四种消融门；
9. C++ 按确定性质量顺序和 `3,500 cm` 布局差异门保留全局 Top-K。

该算法不使用另一套 patched-conic 近似积分器；所有构造尝试最终都由同一个 `M11Core::GravityAssistSolver::Solve` 精确求值。

## 5. CLI 与断点恢复

独立搜索：

```powershell
& .\Intermediate\M11CoreStandalone\bin\ABTSM11SearchCLI.exe search `
  --output <绝对目录> --work-items 256 `
  --shard-index 0 --shard-count 1 --threads 8 `
  --top-k 5 --seed 296882177 --checkpoint-every 8 --json
```

Python 标准库编排：

```powershell
python .\Tools\M11Core\Python\m11_search.py run `
  --output .\Intermediate\M11B21CandidateLibrary `
  --work-items 256 --shards 4 --threads-per-shard 2 `
  --top-k 5 --checkpoint-every 8
```

恢复与状态：

```powershell
python .\Tools\M11Core\Python\m11_search.py status `
  --output .\Intermediate\M11B21CandidateLibrary

python .\Tools\M11Core\Python\m11_search.py run `
  --output .\Intermediate\M11B21CandidateLibrary `
  --work-items 256 --shards 4 --threads-per-shard 2 `
  --top-k 5 --checkpoint-every 8 --resume
```

每个检查点冻结工具身份、Core/Search Source Hash、Seed、全局工作量、分片身份、下一局部索引、状态/JSONL 字节数与内容 Hash。恢复时任一项不匹配都会 fail closed。C++ `merge` 再次检查全部分片覆盖无重无漏，并重新求值所有 Accepted 记录后才输出全局 Manifest。

## 6. 256 项候选库结果

正式运行：

| 项 | 结果 |
| --- | --- |
| 全局工作项 | `256` |
| 分片 | `4 × 64` |
| C++ 求解调用 | `127,203` |
| 分片累计墙钟 | `232.080 s` |
| Accepted | `4` |
| 全局选中 | `4` |
| Evaluation Aggregate | `0xbb3b73fe29426372` |
| Candidate Aggregate | `0xb03d33f10710f59a` |

候选库：

| 排名 / Work | Candidate / Request / Result / Score Hash | 总时长 / 最大 coast | 最小布局转角 | 三次停留时长 | 三次实际偏转 | 鲁棒 / 低功率完成数 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 / `166` | `bd7d63e871c524bf` / `a40f917f70db40ab` / `b2987a35306c3654` / `facaab57a03dd3be` | `36.117 / 6.841 s` | `0.4094 rad` | `7.93 / 5.91 / 5.73 s` | `0.606 / 0.500 / 0.532 rad` | `4 / 2` |
| 2 / `210` | `8401b7607117ba97` / `9cef93a75d999a93` / `49e478a34b8705ce` / `34c6f06ffab4e709` | `36.389 / 6.081 s` | `0.3783 rad` | `7.95 / 7.21 / 5.39 s` | `1.271 / 0.451 / 0.443 rad` | `4 / 2` |
| 3 / `121` | `5047bc74651ed692` / `936c0f47a596639a` / `995e8e1dfb10ee9f` / `f6011b6f5d72bf47` | `43.379 / 7.625 s` | `0.3805 rad` | `7.90 / 7.79 / 4.28 s` | `0.581 / 0.531 / 0.396 rad` | `4 / 1` |
| 4 / `122` | `3139002725dbe64e` / `63697c0b2e7cf486` / `9e75a4833d4a8d1f` / `abdc3a6c77f425b5` | `35.278 / 7.493 s` | `0.3470 rad` | `7.68 / 4.26 / 4.56 s` | `0.307 / 1.169 / 0.484 rad` | `4 / 1` |

完整 Manifest 位于：

```text
Intermediate/M11B21CandidateLibrary/merged/candidates/
```

四个 Manifest 的状态均为 `Candidate`，四种消融结果均为 `hitTarget=false`。

## 7. 自动验收

| 门 | 结果 |
| --- | --- |
| 标准 C++ Release 构建 | 通过，MSVC `14.44.35207`、C++20、`/fp:precise` |
| CTest | `4/4`：Core conformance、Core 无 UE 依赖、Search 确定性、Search 无 UE 依赖 |
| Python 四分片 + C++ merge smoke | 通过 |
| Python 同计划 `--resume` | 通过，结果身份不变 |
| Development Editor 默认链接 | 通过 |
| Development Editor `-ForceUnity -DisableAdaptiveUnity` | 通过 |
| `ABTS.M11B.V2_1.PortableCandidateReplay` | `1/1`，全新 `UnrealEditor-Cmd -NullRHI` |
| `ABTS.M11A` 回归 | `15/15` |
| `ABTS.M11B.Unit` 回归 | `8/8` |
| `ABTS.M11B.Runtime` 回归 | `4/4` |

UE 快速重放冻结候选 `Work=166`，在 UE 内重新执行候选构造，并确认：

```text
Source  = 0xbd7d63e871c524bf
Request = 0xa40f917f70db40ab
Result  = 0xb2987a35306c3654
Score   = 0xfacaab57a03dd3be
```

随后同一 Request 分别由标准 C++ Core 与 UE facade 求解，完整结果逐字段相等。证据日志：

```text
Saved/Logs/M11B21_PortableCandidateReplay_Final_20260729.log
Saved/Logs/M11B21_Regression_M11A_20260729.log
Saved/Logs/M11B21_Regression_M11BUnit_20260729.log
Saved/Logs/M11B21_Regression_M11BRuntime_20260729.log
```

## 8. 交给 M11-C v2.1 的验收重点

建议先按排名体验 `166`，再比较 `210 / 121 / 122`：

- `166` 的三次偏转最均衡、非共线度最高，适合作为首选；
- `210` 的第一次偏转最强，可验证“引力弹弓感”是否过猛；
- `121` 总时长最长但低功率只完成一次助推，可用于比较功率门感受；
- `122` 总时长最短，第二次偏转最强，可用于比较“激流勇进”节奏。

若 PIE 只暴露鼠标、映射、HUD 或异步延迟问题，留在 C v2.1 修复；若四个候选都被判定为近星停留过长、转角读不清、成功岛过窄或布局观感不佳，则返回本阶段修改搜索合同并重新生成 Candidate Source Hash。

体验批准前不得运行 M11-B v2.2 慢认证；体验批准后必须冻结唯一候选及完整数值身份，再对完整 `Yaw × Pitch × Power` 域、旁路、晚到、错序、重复助推和全部消融执行正式认证。
