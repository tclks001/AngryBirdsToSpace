# M11 v2：终局引力弹弓优化总设计与阶段边界

> 状态：**M11-A v2.1 已完成；M11-B v2.1 Search v3、M11-C v2.1 表现/相机代码及当前 v3 Candidate Catalog 均已实现；其上新增的 Additive Search v4 条件粒子束构造器已完成 4 个正式种子搜索（3 个成功、1 个正常无解退出）**。v4 把成功运行的接受吞吐提高到 v3 4096-work 基线的约 `38.63–58.33×`，得到 4 个不重复的已选布局，但没有覆盖当前 v3 Rank 1/2、没有写入 Catalog，也没有通过有渲染 PIE。所有新旧结果仍只能称为 `Candidate / NOT CERTIFIED`，不得进入参数冻结或 production 绑定。后续仍按 **M11-C v2.1 候选手感循环 → 参数冻结 → M11-B v2.2 完整认证 → M11-C v2.2 正式绑定** 推进。M11-B/C v1 继续作为生产基线。
>
> 父级：[M11 终局三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md)。
>
> v1 基线：[M11-A 求解器](M11AGravityAssistSolverDesign.md) · [M11-B 布局搜索与全输入域认证](M11BFinaleLayoutCertificationDesign.md) · [M11-C 交互与确定性实飞](M11CFinaleInteractionAndPlaybackDesign.md)。
>
> v2.1 子稿：[M11-B v2.1 标准 C++ 候选布局搜索与快速同源重放](M11B21CandidateSearchDesign.md) · [Additive Search v4 条件粒子束逐星构造器](M11B21ConditionalParticleBeamSearchDesign.md)。
>
> 交互参照：[M6 弹弓发射与碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M6 弹弓视觉表现](M6SlingshotVisualPresentationDesign.md)；HUD 语义参照：[M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md)。
>
> 集成约束：[多工作树协作与集成规范](ABTSMultiWorktreeDevelopmentGuide.md) · [项目工作流](ABTSProjectWorkflow.md)。

## 1. 优化目标

本轮优化解决三个相互关联但权威归属不同的问题：

1. 终局轨迹预演必须从约半秒级反馈降到近实时，且长轨迹不能靠每 `1/120 s` 机械保存一个权威点；
2. 三颗助推行星必须表现为明显的引力转向与加速源，而不是排列在 UFO 前方的一串障碍；成功原始轨迹应在 60 秒内完成，三次近星扭转之间没有过长空驶；
3. Space 弹弓必须恢复普通弹弓的可见光标、按住—拖动—松开发射、方向和可调功率手感，预演、HUD 与 Release 必须使用同一个输入。

这三个目标不能在同一层用“调几个参数”混合完成。M11-A 只提供快速、稳定、可度量的积分能力；M11-B 决定实际场景和唯一成功岛；M11-C 才负责玩家手势、预演调度和播放。

当前已验收的 M11-B/C v1 仍绑定 `SolverVersion=1 / HashSchemaVersion=1` 及其冻结 Hash。本轮新增 A v2 不会自动替换 v1，也不会使既有验收失效。

## 2. 三阶段唯一权威边界

| 阶段 | 唯一权威职责 | 明确不做 |
| --- | --- | --- |
| **M11-A v2/v2.1：纯数据求解器与可移植内核** | 数值积分、解析事件、确定性、版本化 Hash、远场性能、强引力稳定性、中性轨迹诊断；v2.1 再把这些语义收敛为 UE、CLI 和 Python Binding 共同编译的标准 C++ 单一权威 | 不选行星位置或产品参数，不定义发射输入域，不搜索/认证成功岛，不处理鼠标、弹弓袋、HUD、异步队列或播放；不允许复制出另一份 Python/GPU 权威积分器 |
| **M11-B v2.1/v2.2：布局候选与认证** | v2.1 搜索并快速重放三行星/UFO 候选库；v2.2 对体验批准后冻结的唯一候选执行完整 `Yaw × Pitch × Power`、唯一 F4、旁路和消融认证 | 不接鼠标，不移动弹弓袋，不实现预演调度、HUD、稳定器或播放时间轴；v2.1 Candidate 不得自称 Certified Bundle |
| **M11-C v2.1/v2.2：轨道交互优化** | v2.1 在 Editor-only 候选模式中完成 M6 一致手感与 PIE 体验循环；v2.2 只绑定 v2.2 Certified Bundle，完成正式稳定器、播放和最终 PIE/Standalone 验收 | 不改引力、布局、成功分类、认证输入域或 Trust Region，不用表现层把失败轨迹“修正”为成功，不允许正式构建加载未认证 Candidate |

依赖顺序固定为：

```text
M11-A v2 已验收数值基线
  -> M11-A v2.1 标准 C++ 单一权威 + 搜索前 CLI/UE 一致性门
    -> M11-B v2.1 标准 C++ 候选构造 + 3–5 个候选库 + UE 快速重放
      -> M11-C v2.1 Editor-only 候选手感与 PIE 循环
        -> 体验批准并冻结全部数值/输入身份
          -> M11-B v2.2 独立 CLI 完整输入域与消融认证
            -> M11-C v2.2 Certified Bundle 正式绑定与最终 PIE/Standalone
```

后续阶段不得逆向修改上游权威。C v2.1 若只发现鼠标、映射实现或 HUD 调度问题，应留在 C 修复；若发现轨迹太长、偏转不明显、成功岛太窄或布局观感不佳，应退回 B v2.1 换候选或重新搜索；若必须改变积分、事件、换能公式或 Hash 语义，应退回 A 正式升版，并使现有 v2 候选与认证身份失效。B 若发现数值性能或稳定性不足，也必须退回 A，而不是在搜索器、Python 或 GPU 内复制积分公式。

## 3. v2.1/v2.2 正式实施顺序

### 3.1 M11-A v2.1：标准 C++ 可移植单一权威

v2.1 是代码边界重构，不是物理重调。目标是把 M11-A 的纯数据部分提取成不包含 Unreal Header、反射宏、`FVector`、`TArray`、`FMath`、`ParallelFor` 或 UObject 生命周期的标准 C++ 内核；UE Adapter、独立 CLI 和未来 Python Binding 都必须**共同编译同一份内核源码**，禁止把“干净拷贝”演变成第二份可独立修改的求解器。

已落地结构为：

```text
M11Core（标准 C++，唯一数值与 Hash 权威）
  ├─ M11CoreConformance（A v2.1 独立 Release 一致性工具）
  ├─ M11SearchCLI / M11CertifyCLI（B v2.1/v2.2 后续前端）
  ├─ Python Binding 或批处理协议（B 阶段只做调用与数据搬运）
  └─ UE Adapter（标准类型 <-> FVector/TArray，运行时与 PIE）
```

因此 A v2.1 的完成门是“同一 Core 已能被独立工具与 UE 共同编译并逐字段重放”；搜索/认证命令行参数、分片、检查点和报告格式仍属于 B，不在 A 中提前复制一套候选或分类逻辑。

v2.1 必须保持：

- `SolverVersion=2 / HashSchemaVersion=2` 的积分、事件优先级、根定位次数、分类事实和 Hash 字节语义不变；
- 已验收 `1/1` 路径与 golden 不变；
- CLI 与 UE 使用同一标准 C++ 数据结构或无损显式转换，不在 Adapter 中补物理规则；
- 工具构建版本与轨迹身份分离；只重组编译边界时可提升 `ToolBuildVersion`，不得伪装成新的 Solver 语义；
- 独立工具可由 MSVC/CMake/Ninja 等普通 C++ 工具链编译，不需要 `.uproject`、UnrealBuildTool、Editor、Content、DDC 或 Asset Registry。

在第一次 B v2.1 搜索之前必须先通过 CLI/UE 一致性门，不能等候选找到后才证明离线工具可信。固定对照语料至少覆盖 nominal、近边界命中、天体碰撞、错误顺序、逐星消融、晚到、宏步回退和强助推样本，并逐项比较：

- Request 规范化身份与 Result Hash；
- Termination、事件序列、事件时间、助推有效 Mask；
- 权威点列数量及冻结的关键位置/速度；
- 串行、标准 C++ 并行、CLI 重跑和 UE Adapter 重放结果；
- v1 golden 与现有 M11-A/B/C 快速回归。

只有该门通过，B v2.1 才能把 CLI 结果视为权威候选依据。

### 3.2 M11-B v2.1：候选构造与候选库

B v2.1 使用标准 C++ `M11Core` 做全部精确轨迹求值。实际落地由标准 C++ `M11Search` 负责参数构造、分类、排名、Hash 与 Manifest，独立 C++ CLI 负责搜索/合并；Python 标准库脚本只负责不可变计划、进程分片、状态与恢复调度，不生成候选数值身份。

本阶段先执行分层构造搜索和候选级统计，不执行耗时极大的完整输入域认证。候选必须先满足第 6 节的强助推、非共线、60 秒、coast、碰撞净空、顺序和初步消融硬门；目标保留 **3–5 个**布局与空间构型明显不同的候选，避免一次 PIE 手感否决就重新进行全量搜索。如果全局搜索只有更少结果通过全部硬门，则保留全部真实通过者并显式记录名额缺口，不得用失败项或重复布局填满配额。本轮 4096-work 搜索因此只保留实际通过的 2 个候选。

候选通过产品硬门后，执行两套互不混用的固定种子 Halton 语料：

- `ScreenAim 5000`：完整 Yaw/Pitch，Power 固定为 nominal；它是 S1–S3 候选比例硬门、独立二维凸包和 UX 评分的唯一权威；
- `FullLaunchDomain 5000`：完整 `Yaw × Pitch × Power`，只诊断 Power 维度，不参与候选接受、凸包或排名；
- 每集 512 个 Conditional 点只作局部解释，不并入任一无偏语料。

`S1/S2/S3` 分别表示已取得前一、前二、前三颗行星合格助推的嵌套前缀集，`S4` 仅作为命中候选目标球的诊断集。这些撒点都不是逐像素穷举，也不是 v2.2 认证。

`S1/S2/S3` 各自只用对应 ScreenAim 成员计算 Yaw-Pitch 凸包，不加入 FullDomain、Conditional 或人工 nominal anchor。每级 ScreenAim 保留率先通过 `[0.08, 0.55]` 硬门，再检查凸包；`[0.15, 0.40]` 为软评分满分平台。三次实际速度偏转、相对布局平面的侧向转轴投影、相邻助推左右交替次数和节奏余量共同奖励可读的“左—右—左/右—左—右”轨迹。

每个候选必须输出不可变 Candidate Manifest：

- `CandidateId`、`CoreVersion`、Solver/Hash 版本和 Source Hash；
- 终局局部布局、三颗行星/UFO 参数、LaunchModel 和 Scan Contract；
- nominal 输入、Termination、事件序列、节奏诊断和轨迹 Hash；
- 初步成功岛中心/边界摘要、逐星消融摘要和已知失败区域；
- 生成命令、随机种子、工具版本和可复现输入文件。

此时状态只能是 `Candidate`。不得生成正式 `CertificationHash`，不得命名为 `CertifiedBundle`，也不得覆盖 M11-B/C v1 的生产默认值。

`SearchContractVersion=3 / SearchAlgorithmVersion=3 / CandidateManifestVersion=3` 已实现上述统计和评分语义。旧 v1/v2 搜索候选、排名、Score/Source Hash、Aggregate 和 Catalog 身份全部失效；最终候选表、命令和工具身份以 [M11-B v2.1 子稿](M11B21CandidateSearchDesign.md) 的合并记录为准。无论候选级撒点结果多好，这些输出仍只是 `Candidate`，不构成完整输入域唯一性认证。

### 3.3 候选进入 PIE 前的快速跨前端重放

每个进入 C v2.1 的候选先通过分钟级而非小时级的快速门：

1. CLI 对 nominal 和固定小样本重复运行，结果与 Hash 一致；
2. UE Adapter 重放 nominal，结果与 CLI 一致；
3. 重放初步成功岛中心、各轴边界内外、低功率失败、错序和碰撞代表样本；
4. 三颗行星逐一消融与全消融的代表样本不形成 nominal F4；
5. Actor 的终局局部坐标、缩放和被消费参数与 Candidate Manifest 一致；
6. 未认证身份在日志和 HUD 中明确显示为 Candidate，不出现“认证通过”措辞。

该门只证明“标准 C++ 候选进入 UE 后没有变质”，不证明全输入域唯一性。

### 3.4 M11-C v2.1：Editor-only 候选手感循环

C v2.1 增加显式开发候选模式，用于在 PIE 中完成：

- 与普通 M6 弹弓一致的可见光标、按住—拖动—同次松开发射；
- 拖动只控制袋位置与发射方向；Power 与 M6 一样由滚轮独立控制，每格步长为 `0.08`；
- latest-only 预演、轨道 HUD、接近预览、失败恢复和 60 秒内确定性播放；
- 接近 PIP 第一目标使用“发射袋→行星①”，之后使用“上一目标中心→当前目标中心”为固定视轴，以 Finale Local `+Z` 为恒定 up；同一目标内只叠加当前权威 prediction 的真实 closest 局部轨迹，不随鼠标微扰重新旋转、平移或缩放；
- 主视图只显示选择器当前锁定目标的一个 Wedge；目标切换立即替换，空间/时间迟滞只控制同一目标的显示/隐藏；
- Release 后由 M11-only 相机消费权威轨迹的 World Position/Velocity，沿切线跟随并平行运输 Up；恢复、复位和退出时先切回 Aim Camera；
- 目标选择的昂贵轨迹几何只在 `ResultHash + LatchedTarget` 改变时重建，PIP 的 Scene Capture 只在首次有效结果或目标切换时捕获，当前轨迹由 HUD 叠加；
- 对候选成功岛宽度、三次转向可读性、节奏、镜头和前缀成功集稳定器手感的人工验收。

候选模式默认关闭：`abts.M11.CandidateRank=0` 始终使用 production Certified v1。体验候选前，必须在 Editor 控制台显式设置 `abts.M11.CandidateRank N`，其中当前 Rank 1–2 是保留的 v3 基线、Rank 3–6 是 v4 手感候选、Rank 7 是由 Rank 3 派生的上游映射候选 353，并重新启动 PIE；非 PIE Editor World、Standalone 和非 Editor 构建忽略该候选请求并保持 production v1。Rank 7 仅供研究：其名义轨迹在 Assist3 Exit 前已经 TargetHit，不满足正式运行时 F4 事件顺序。

候选模式必须由 `WITH_EDITOR`、显式开发开关或等价 fail-closed 边界隔离；Shipping/正式 Standalone 不得加载 Candidate。v2.1 可使用局部快速扫描生成的临时 Trust Region 测试降敏和边界感，但它不能作为最终认证 Trust Region，也不能进入生产 Bundle。

PIE 反馈按以下规则路由：

| 发现的问题 | 回退位置 | 是否重新搜索 |
| --- | --- | --- |
| 光标、拖动方向、袋跟随、松开发射、HUD 或 latest-only 调度 | C v2.1 | 否 |
| 屏幕手势到 Yaw/Pitch/Power 的实现偏离已冻结 LaunchModel | C v2.1 修复到一致 | 否 |
| 决定修改 LaunchModel 范围、符号、边界或 Power 定义 | B v2.1 | 是，旧候选失效 |
| 轨迹太长、coast 太平、转角不足、行星近共线或成功岛太窄 | B v2.1 换候选/重搜 | 是 |
| 积分、步长语义、事件优先级、换能或 Hash 需要变化 | A 正式升版 | 是，所有旧候选失效 |
| 只改图元、颜色、镜头或不改变时间函数的视觉表现 | C v2.1 | 否 |
| 依靠播放倍速掩盖布局中的长 coast | 禁止留在 C 修补，退回 B v2.1 | 是 |

候选可以反复“快速重放 → PIE → 调整/换候选”，但不得在每轮手感试验前执行完整认证。

### 3.5 体验批准与冻结门

用户确认候选手感后，先冻结再认证。冻结集合至少包括：

- M11Core/Solver/Hash 版本、编译浮点策略和事件合同；
- 行星/UFO 终局局部布局、`Mu`、Influence/Reference 半径、虚拟动量与 B-plane；
- LaunchModel 的 Yaw/Pitch/Power 范围、符号、映射和完整认证输入域；
- Scan/Certification Contract、60 秒、各段 coast、dwell、转角、碰撞和旁路门槛；
- Target/Bypass、错序、重复助推、晚到、消融和 F1–F4 语义；
- Candidate Source Hash 与体验批准记录。

冻结后不得在 UE Details 面板或 Python 脚本中临时修改数值。任何影响上述身份的变化都回到 B v2.1，并产生新的 CandidateId/Hash。

### 3.6 M11-B v2.2：独立完整认证

B v2.2 只接收体验批准并冻结的候选。正式慢认证由独立 `M11CertifyCLI` 调用同一标准 C++ `M11Core` 执行，Python可以负责分片、进度、断点恢复和报告，但每个样本的积分、F1–F4 分类与 Hash 均来自标准 C++。

完整门包括第 6.4 节全部要求，并额外保证：

- 串行、并行、分片和断点续算得到相同 Certification Hash；
- base grid、half-cell offset、边界精化与最终闭包覆盖完整 `Yaw × Pitch × Power`；
- 每颗逐一消融与全消融均按同一完整域认证，而非只测试 nominal；
- 成功域只有一个满足最低可玩宽度的连通 F4 家族；
- 审计时域覆盖晚到、多圈、重复助推、错序和几何旁路；
- 输出不可变 `CertifiedBundle`、认证报告、输入清单、工具身份和可复现命令。

UE 不必再次耗时数小时扫描全域，但必须用 fresh process 读取 Bundle、校验 Hash，并快速重放 nominal、认证边界代表样本和 fail-closed 样本。

### 3.7 M11-C v2.2：认证绑定与最终验收

C v2.2：

- 关闭或编译隔离 Candidate 入口，生产路径只接受版本与 Hash 完全匹配的 Certified Bundle；
- 绑定由 B v2.2 认证得到的正式 Trust Regions；
- 保证 Preview、HUD、稳定器、Release 和 Physical Playback 消费同一输入与 Bundle 身份；
- 对损坏、缺失、版本不匹配或未认证 Bundle fail closed；
- 完成 fresh PIE 的进入/退出、成功、失败黑屏与恢复、重复进入验收；
- 最后完成 fresh Standalone 或目标打包生命周期验证。

## 4. Python 与 GPU 优化实验方案

### 4.1 权威关系

`NumPy` 是 CPU 数组库，不会自动调用 NVIDIA GPU。若需要 Python GPU 实验，应显式使用 CuPy、CUDA Kernel 或等价 GPU 后端。无论采用哪种工具，正式权威关系固定为：

| 工作 | 允许实现 | 权威性 |
| --- | --- | --- |
| 参数生成、搜索策略、Pareto 排名、分片、断点恢复、报告和可视化 | Python + NumPy | 编排/分析，不决定轨迹真值 |
| 大批量远场或固定步粗筛 | 可选 CuPy/CUDA Probe | 近似筛选，不得签发 Candidate/Certification Hash |
| 候选精确重算 | 标准 C++ `M11Core` | 权威 |
| 完整输入域与消融认证 | Python 编排标准 C++ `M11Core` / 独立 CLI | 权威 |
| UE 快速一致性与正式运行 | UE Adapter 调用同一 `M11Core` | 权威前端 |

不得维护一份 NumPy/CuPy 版“同等求解器”作为第二权威，也不得让 GPU 分类直接进入 Certified Bundle。

### 4.2 Python CPU 基线

先完成 Python + 标准 C++ 的 CPU 基线，再决定 GPU 是否值得引入：

- Python 通过 Binding、稳定 C ABI 或批处理 CLI 调用 M11Core；
- 单次跨语言调用应提交一批 Request，避免逐积分步跨边界；
- 轨迹点可选输出，粗排阶段优先只返回 Termination、最小距离、助推 Mask、总时长、偏转和节奏摘要；
- 搜索任务按 Candidate/Input 索引确定性分片，结果按索引归并，不按线程完成顺序改变身份；
- 环境、依赖、命令、种子和中间检查点必须可重放；
- Python 崩溃或重启后从已校验检查点续算，不重复数小时已完成分片。

CPU 基线本身已经消除 UnrealEditor 启动、资产扫描、DDC、DLL 占用与 `.uproject` 临时副本成本，因此也是 GPU Probe 的公平比较对象。

### 4.3 可选 CuPy/CUDA 粗筛 Probe

GPU 只适合批量并行度高、结构相似的粗筛。M11 精确求解包含自适应步长、不同终止时间、事件优先级、swept sphere、根定位、递归细分和提前退出，容易产生线程分歧；不得预设 GPU 一定比优化后的 C++ CPU 快。

Probe 应满足：

- 整批状态常驻 GPU，只回传摘要，禁止每个积分步往返 Python；
- 优先筛除远离产品门槛的明显失败布局，阈值附近全部提升给标准 C++；
- 使用保守 guard band；GPU 可多报假阳性，不能依靠激进阈值制造隐藏假阴性；
- 随机抽检被 GPU 淘汰的样本，并重点审计 near-boundary、强曲率、事件同时发生和长短轨迹混合批次；
- 所有进入 3–5 候选库的布局和输入均由 M11Core 从初态精确重算；
- GPU 结果不参与 Result Hash、Candidate Hash、Certification Hash 或正式唯一性证明。

### 4.4 Benchmark 与 go/no-go

GPU Probe 只有通过独立基准才接入 B v2.1；否则保留 CPU 路线，不阻塞里程碑。基准固定同一批代表性候选和输入，至少报告：

- C++ Release 单线程、CPU 并行和 GPU 的端到端轨迹/秒；
- Kernel、主机/设备传输、Python 调度和结果回收耗时；
- 远场、三次近星、早停失败、60 秒成功和更长审计轨迹分别吞吐；
- 对 M11Core 的假阳性、假阴性、事件/终止差异与 near-boundary 距离；
- 批大小、GPU 显存峰值、CPU/GPU 利用率和可复现环境。

建议 go 条件为：在目标搜索批量下，计入全部传输和编排后的端到端吞吐至少达到 C++ CPU 并行基线的 `2×`，代表性/边界语料中无 guard-band 外假阴性，且所有提升候选可被 M11Core 稳定复算。达不到即 no-go；GPU 代码不进入正式认证依赖。

2026-07-29 的本机环境盘点为 Python `3.14.6`，尚未安装 NumPy、CuPy、pybind11 或 pytest；GPU 为 NVIDIA GeForce RTX 2080 8 GB，当前没有 `nvcc`。M11-A v2.1 不为未来实验擅自安装 Python/CUDA 依赖。M11-B v2.1 应先建立隔离、可复现的 CPU 环境和标准 C++ 吞吐基线，再决定是否单独配置与现有驱动相容的 CuPy/CUDA Probe；缺少 GPU 环境不阻塞 CPU 候选搜索。

### 4.5 风险与验收门

| 风险 | 控制措施 | 阻断验收 |
| --- | --- | --- |
| 标准 C++ 与 UE 形成两套算法 | UE Adapter 与 CLI 编译同一源码，Adapter 只转换类型 | 搜索前 CLI/UE parity corpus 全通过 |
| Python/CuPy 浮点、FMA 或归约顺序改变边界分类 | Python/GPU 只编排/粗筛，边界全部交回 M11Core | Candidate 与 Certified Hash 只能由 M11Core 产生 |
| GPU 线程分歧使“加速”负收益 | 先测 CPU Release 基线，按真实混合轨迹做端到端 benchmark | 未达到 go 条件则关闭 GPU 路径 |
| GPU 粗筛漏掉可玩候选 | guard band、淘汰集抽检、候选精确复算 | guard-band 外发现假阴性即停用/修正 Probe |
| Python 环境或断点文件不可复现 | 固定依赖、版本、种子、分片索引与输入 Hash | clean 环境可续算并得到同一聚合 Hash |
| 未认证 Candidate 泄漏到生产 | Editor-only CandidateMode，正式读取 fail closed | Shipping/Standalone 无 Candidate 加载入口 |
| PIE 调参后认证对象悄然变化 | 冻结 Manifest 覆盖全部数值和输入映射 | 任一身份变化必须生成新 CandidateId 并重回 B v2.1 |
| 完整认证被 GPU/近似扫描替代 | v2.2 仅由标准 C++ 完整域分类 | 认证报告记录每一分片 Core/Contract/Hash 身份 |

## 5. M11-A v2：已完成的数值优化基线

### 5.1 版本与向后兼容

- 新增显式 `SolverVersion=2 / HashSchemaVersion=2`；只接受 `1/1` 或 `2/2`，混配和未知版本 fail closed。
- `FABTSM11SolverConfig::MakeV2()` 是 v2 的唯一默认构造入口。
- v1 必须保持 `MaximumCoastStepExpansionDepth=0`，继续走原有逐字节数值与 Hash 路径。
- v2 新配置字段只在 HashSchema 2 中进入 Result Hash 和 Finale Preset Source Hash，不能污染 HashSchema 1 字节流。
- 当前 `MakeCertifiedV1()`、M11-B v1 预设和 M11-C v1 运行路径不切换到 v2。

### 5.2 二进制幂远场扩步

基础积分器、事件优先级和固定次数根定位保持不变。v2 只在轨迹位于全部三颗助推行星作用圈之外时，从以下确定性步长集合选择满足既有误差、尺度和引力时间尺度约束的最大值：

\[
\Delta t=\Delta t_0 \cdot 2^k,\qquad
k\in[-D_{subdivide},D_{coast}]
\]

默认 `D_coast=6`，因此理论最大步长为基础步长的 64 倍。进入任意助推作用圈后，最大步长立即回到基础 `FixedTimeStepSeconds`，必要时仍只按二进制幂细分。这样：

- 远离行星的长空驶不会产生数万冗余权威点；
- 强引力近星段继续保持 v1 的细粒度积分；
- 每个大于基础步长的宏步先使用 Verlet 二次位置弧的精确弓高，对 4 个碰撞球、3 组 Influence/Reference 球、资格/几何目标球和主星模拟边界做保守拓扑认证；无法证明整步不跨界时按二进制幂回退到基础步长；
- 认证后的大步仍经过同一线段—球 swept 根、事件时间比较和 `Body > Target > WrongOrder` 优先级，强曲率轨迹不能因端点弦未入球而漏掉事件，也不能因端点弦假穿球而降低到宏步精度；
- 不读取渲染 `DeltaSeconds`，串行和并行求解仍必须位级一致。

### 5.3 中性节奏与偏转诊断

`FABTSM11TrajectoryResult::BuildPacingDiagnostics()` 从权威点列和事件列派生：

- 每颗已观测助推星的 `Enter / Closest / Exit` 时间；
- 每段入站 coast、作用圈停留时长、末段 coast、总 coast 和最长 coast；
- 实际入/出速度方向夹角；
- 自然双曲线转角、入/出速度和已应用能量；
- 总飞行时间、总作用圈时长和目标命中时间；
- 对续算结果，明确记录本结果实际包含的首/末助推序号和连续后缀数量。

这些字段不改变轨迹，也不进入轨迹 Hash。A 只报告“发生了什么”；“60 秒”“约 5 秒”“最小可见转角”等好玩与否的阈值由 B v2 冻结。

### 5.4 本阶段明确不做

M11-A v2 不包含：

- 三颗生产行星的实际位置、`Mu`、Influence/Reference 半径或虚拟速度；
- 非共线布局、最小转角、5 秒近星区、60 秒全程和最大平淡 coast 等产品门槛；
- Yaw/Pitch/Power 范围及其与 M6 的一致性；
- F1–F4、唯一连通分量、Trust Region、逐星消融和旁路认证；
- 鼠标可见性、横向方向、弹弓袋跟随、一次松开发射；
- 预演队列、80 ms 节流、HUD、失败黑屏、TerminalTransfer 或播放倍速；
- Actor、地图、资产、M6 源码和共享世界 Contract。

## 6. M11-B v2：布局搜索、候选体验与全域认证

### 6.1 发射域

B v2 必须先冻结与普通 M6 弹弓实际可达范围一致的 LaunchModel：

- Yaw/Pitch 的中心、符号、上下限和球面方向映射与 M6 对齐；
- Power 保持连续可调，完整认证仍扫描 `Yaw × Pitch × Power` 三维域；
- 第一颗行星放远只用于让低功率轨迹自然无法到达，不能据此把认证 Power 固定为最大值；
- 成功岛必须留有可玩的有限宽度，不能退化为单个浮点输入。

“角度范围一致”由 B 冻结权威输入域，由 C 实现相同手势映射；A 不参与。

### 6.2 强助推与非共线布局

B v2 显式选择 Solver/Hash 2/2，并搜索、冻结：

- 更大的三颗行星作用圈、自然引力参数和虚拟动量预算；
- 明显不同方向的三次近星飞越，使行星和 UFO 不再近似共线；
- 每颗助推的最小实际速度转角、最小有效能量交换和碰撞净空；
- 每颗助推相对轨迹拟合平面的有符号侧向转轴投影（绝对投影硬门 `>= 0.25`），以及相邻两次助推的左右交替次数；
- 固定行星位置下唯一的 `① -> ② -> ③ -> UFO` 成功路径族；
- 关闭任意一颗或全部玩法助推后都不存在 F4。

行星参数必须由完整求解与消融证明其必要性，不能只凭轨道 HUD 看起来弯曲。

B v2.1 的候选级可玩性探针分为两套：固定 nominal Power 的 `ScreenAim 5000` 决定 `S1/S0`、`S2/S1`、`S3/S2` 比例、各自独立凸包和 UX 评分；三维 `FullLaunchDomain 5000` 只诊断 Power 维度。每集 Conditional 512 也只作局部诊断。ScreenAim 比例先过 `[0.08,0.55]` 硬门，随后才检查凸包；这些结果仍不能证明连通分量唯一、旁路不存在或 Power 全域闭包，后者属于 B v2.2。

### 6.3 60 秒“激流勇进”节奏

B v2 在长时间搜索开始前，把以下数值写入新的 Scan/Certification Contract：

- 成功原始轨迹 `TotalFlightTimeSeconds <= 60 s`；
- 每颗行星作用圈停留时间以约 5 秒为目标，并冻结可接受窗口；
- 首段、两段中场和末段分别冻结最大 coast，不允许用一个总平均值掩盖某段长时间平飞；
- 每颗助推冻结最小实际偏转角，保证玩家能在轨道图和近星预览中看到明显转向。

初始搜索建议以 `3–7 s` 作用圈停留和 `<=10 s` 单段 coast 作为种子目标；它们在 B v2 正式搜索前仍需结合镜头与速度标尺校准，然后作为版本化硬门冻结。

成功时限和旁路审计时域必须分离：成功要求在 60 秒内发生，但认证仍需积分到足以暴露晚到、多圈、重复助推、错序和旁路的更长审计上限，不能通过把求解时域截到 60 秒伪造唯一性。

### 6.4 v2.2 正式认证门

B v2.2 不得继承 v1 的认证 Hash，也不得把 v2.1 的 Candidate Hash 当成认证 Hash。至少需要：

1. 提升 Preset、LaunchModel、ScanContract、Certification、PhysicalPlayback 和 Bundle 身份；
2. ConstructiveSearch 重复运行得到同一布局与 Hash；
3. base grid、half-cell offset、边界精化和最终精度闭包覆盖完整三维输入域；
4. 全域只有一个连通 F4 家族，且 `F4 ⊂ F3 ⊂ F2 ⊂ F1`；
5. Power 全范围的低功率失败、成功岛和高功率边界均被扫描；
6. 三颗逐一消融和全消融后 F4 为零；
7. TargetContact/Bypass、错序、重复助推、多圈和晚到轨迹全部拒绝；
8. 冻结新的 nominal 与 physical playback 轨迹 Hash。

M11-C v2.1 可以在 Editor-only 模式快速重放未认证 Candidate；只有以上全部通过后，M11-C v2.2 和正式运行路径才能消费 2/2 Certified Bundle。

## 7. M11-C v2：候选手感与正式终局轨道交互

### 7.1 与普通弹弓一致的操作

Space 弹弓改为与普通弹弓相同的一次完整手势：

1. 玩家按下弹弓袋；
2. 可见光标直接拖动弹弓袋，左右、上下方向与普通弹弓一致；
3. 同一次按下松开后立即 Release，不再进入隐藏光标相对输入，也不再要求第二次点击；
4. 拖动只改变袋位置以及 Yaw/Pitch，不改变 Power；Power 使用 M6 的独立滚轮通道，每格 `0.08`，滚轮向下增加、向上降低；
5. 袋的屏幕位置与 Yaw/Pitch 共用一套方向映射，滚轮 Power、HUD 数值、预演 Request 和 Release Request 共用同一个功率值。

若需要从 M6 抽取共享只读输入适配器，M11 工作树只提交接口需求；对 `ABTSM6*` 共享热点的实际修改由集成工作树完成。

以上是对早期“拖动距离连续控制 Power”描述的正式更正，依据是当前已验收 M6 的实际输入合同：`UpdateAimFromCursor` 只更新瞄准平面偏移，`AdjustPullPower` 才以 `PullPowerWheelStep=0.08` 更新功率。C v2.1 的目标是复用这套玩家手感，不在 M11 内另造拉距功率语义。

### 7.2 近实时预演

- 移除固定 80 ms 提交间隔；
- 同时最多一个求解任务，输入变化只保留 latest revision，完成后直接消费最新待算输入；
- 过期 generation 结果绝不发布到 HUD、稳定器或 Release；
- Release 若与最后已发布预演拥有完全相同的输入和 Bundle 身份，直接复用同一 Result，禁止重复求解；
- 权威完整点列只求解一次，HUD 使用独立的确定性简化副本，不能为了绘图复制分类积分；
- HUD 必须显示最后一次已发布预演的求解耗时、提交到发布的总延迟、`IN FLIGHT / STALE / LATEST` 状态以及被丢弃的过期结果数量；`STALE` 只能表示仍在显示上一份已发布结果，不能把过期结果伪装成当前输入；
- 输入变化到轨道 HUD 的目标为 P95 不超过一个渲染帧、P99 不超过两个渲染帧，且 stale publish 为零。

A v2 负责降低单次求解成本，C v2 负责消除节流、重复求解和过期任务；任何一方单独完成都不能宣称“同帧实时”。

### 7.3 HUD、稳定器与播放

- 轨道图继续遵循 M10.1-C 的拟合平面、发射点朝左、凸包 framing、球后虚线和圆形裁剪；
- 三颗行星和 UFO 的所有图元必须留在圆形视口内；
- PIP 始终把当前目标置于中心；第一目标以“发射袋→行星①”定义固定视轴，之后为“上一目标→当前目标”，并以 Finale Local `+Z` 定义恒定 screen up；目标不变时鼠标微扰只更新当前权威局部轨迹叠加，不改变相机 Transform 或 framing；
- 主视图只显示一个当前目标 Wedge；目标切换立即替换，安全边距和迟滞只控制显示/隐藏，不显示 nominal 解或精确修正量；
- Release 后隐藏瞄准用 PIP/Wedge，切换到只消费权威轨迹位置/速度的 M11 飞行相机，沿轨迹切线跟随并平行运输 Up；相机响应按实际播放倍率缩放，不读取 Chaos/Movement，也不反向改写轨迹；
- `ResultHash + LatchedTarget` 是接近几何缓存键；相同结果/目标的每帧 HUD 不再扫描完整点列。Scene Capture 只在首次有效结果或目标切换时捕获目标 Actor，当前局部轨迹由 HUD 叠加，因此每次求解完成不会重新触发 Renderer；
- v2.1 HUD 必须常驻显示 `EDITOR CANDIDATE / NOT CERTIFIED`，并显示候选 `Rank`、`GlobalWorkIndex`、Candidate Source、Nominal Request、Nominal Result 与 Score Hash；候选身份不得借用 `CertificationHash` 或 `CertifiedBundleHash`；
- v2.1 可用明确标记的临时局部区域测试稳定器手感；v2.2 的正式前缀成功集稳定器只消费 B v2.2 冻结 Trust Regions，可降敏和限制在当前成功前缀，不得吸向 nominal 答案；
- v2.1 播放 Candidate 只用于 Editor PIE 体验，HUD 必须标为 `RAW 1X CANDIDATE PLAYBACK / QUALIFIED ENDPOINT`；这里的终点是候选求解器的 qualified target radius，不是已认证的 800 cm UFO 物理接触，也不允许接入 Certified nominal tail 或终端转接。v2.2 播放直接消费 B v2.2 已认证的 `<=60 s` 原始轨迹。可按事件做镜头和轻量表现节奏，但不能用大倍率时间压缩掩盖 B 中过长的物理 coast，也不能改变事件或结局；
- 候选稳定器只可显示 `CANDIDATE PRECISION`、`CANDIDATE PREFIX HELD`、`TEMPORARY` 等候选措辞；不得把临时 Trust Region 或前缀保持状态称为 Certified；
- 失败黑屏和恢复保持游戏线程权威，不能从后台求解线程触发渲染或 SceneCapture。

### 7.4 M11-C v2.1 正式验收门

C v2.1 只有同时通过以下门槛，才可由用户选择候选并进入“参数冻结”：

1. **构建与自动化**：默认 Development Editor 全链接和强制 Unity 全链接均通过；Candidate Catalog/身份拒绝、latest-only 合并、过期结果丢弃、preview/release 同输入身份、候选原始播放与失败恢复均有自动化覆盖。
2. **Editor-only 失效闭合**：只有 Editor PIE 加显式候选开关可加载 Candidate；关闭开关、正式 Standalone 与非 Editor 构建继续走 v1 生产路径或 fail closed，不得静默回退到未认证候选。
3. **身份可见且同源**：HUD 常驻显示 `EDITOR CANDIDATE / NOT CERTIFIED`、Rank、Global Work 和四类 Hash；日志、HUD、运行时布局与标准 C++ 重建出的同一候选逐字段一致。
4. **M6 输入同手感**：可见光标按住并拖动袋控制方向；滚轮每格 `0.08` 独立控制 Power；同一次按下松开立即发射；预演 Request 与 Release Request 使用 HUD 所示同一组 Yaw/Pitch/Power。
5. **latest-only 预演与表现缓存**：快速连续拖动时最多一个后台求解；HUD 明确区分 `IN FLIGHT`、`STALE` 与 `LATEST`，过期 generation 不发布；统计得到 stale publish 为零，并记录 solve/总延迟与 discard 数。相同 `ResultHash + LatchedTarget` 不重复构建局部几何，Scene Capture 不随每次求解重捕获，只在首次有效结果或目标切换触发。
6. **候选路径诚实播放**：F4 候选按求解器时间戳以原始 `1×` 轨迹播放到 qualified endpoint；不生成物理 UFO 接触、不拼接 Certified nominal tail、不进行终端转接，也不以表现层改写成功分类。
7. **稳定器边界**：临时区域只降敏并保持已取得的候选前缀，不吸向 nominal；HUD 全程使用 Candidate/Temporary 措辞，R 可复位尝试。
8. **PIE 表现回归**：袋从首次按住即随光标移动；三颗行星/UFO 图元不越出圆形轨道视口；PIP 第一段固定 pouch→planet1、之后固定 previous-target→current-target 视轴和 local `+Z` up，局部当前轨迹可见且微调不乱晃；主视图始终最多一个 Wedge；Release 后瞄准引导隐藏，相机按播放倍率沿权威轨迹跟随并在恢复时归还 Aim Camera；失败轨迹在预定时限内进入完整黑屏并恢复到入场前状态；重复进入、发射、失败与复位不会从后台线程触发渲染时间上下文报错。

自动门与 PIE 证据必须分别留档。自动化通过不能代替候选手感批准；用户批准也不能代替 Editor-only、身份和 stale publish 门。完成本节只允许冻结候选参数，仍不构成 B v2.2 全输入域唯一性认证。

C v2.1 首轮实现保持 M11 专属边界：Candidate Catalog 从标准 C++ 工作项重建并核对冻结身份；Finale System/GameMode 只通过显式 Editor PIE 候选 Rank 进入未认证模式；Interaction System/PlayerController 承担 M6 同手感输入、绝对光标位移降敏、latest-only 求解、临时稳定器、任意活动阶段 R 复位与原始播放；Presentation/HUD 使用稳定 PIP 局部当前轨迹与单目标 Wedge，飞行相机只消费 Release 的权威播放样本；目标选择几何和 Scene Capture 分别按结果/目标身份与首结果/目标切换缓存。轨道取景在完整轨迹之外仅纳入三颗助推行星和 UFO 的因果图元，仍不强制画出完整主星；HUD 只读这些已发布状态并明确展示候选身份。该轮不修改 M6、Config、共享工作流或 v1 生产默认值。

## 8. 版本与认证失效矩阵

| 变化 | v1 B/C 是否失效 | v2 后续动作 |
| --- | --- | --- |
| 新增 A v2，但保持 1/1 路径和 golden 不变 | 否 | B v2 显式选择 2/2 |
| A v2.1 仅抽取标准 C++ 边界，所有语义与 Hash 保持一致 | 否 | 先通过 CLI/UE parity gate；既有 Candidate 仍须按 Source Hash 重新核对 |
| Search Contract/Algorithm/Manifest `2→3` | production v1 不失效 | 全部旧 Candidate、Score/Source Hash、plan/checkpoint、Aggregate 和 Catalog 身份失效；重新搜索、merge 和冻结 |
| 修改 v2 coast 深度、积分、事件或其他 HashSchema 2 配置 | 不影响 v1 | 所有 v2 Candidate/Certified Bundle 失效；回到 A 升版与 B v2.1 重搜 |
| 修改生产行星位置、`Mu`、作用圈、虚拟动量或 B-plane | 不影响冻结 v1 文件本身 | 生成新 CandidateId，重走 B v2.1 → C v2.1；体验冻结后再做 v2.2 全认证 |
| 修改 LaunchModel 的 Yaw/Pitch/Power 范围或映射 | 当前 v1 不自动切换 | 候选与认证域均失效；重跑完整三维域并更新 C 映射 |
| 只修改 C 的输入调度、HUD 或表现，未改 Request/Bundle | A/B 数值认证不失效 | 跑 C 单元、运行时和 preview/release 身份回归 |
| C 为“改善手感”暗改输入、引力或轨迹 | 禁止 | 退回 B/A 正式升版与认证 |
| 只修改 Python 调度、报告或断点实现，M11Core 输入输出未变 | 否 | 重跑工具可复现/聚合 Hash 门；数值认证身份不变 |
| 修改 GPU Probe 或其近似阈值 | 否 | 重跑 benchmark 与淘汰集抽检；不得改变正式认证结果 |

## 9. M11-A v2/v2.1 已完成代码落点

已验收的 A v2 基线仅修改 M11 专属文本：

- `ABTSM11GravityAssistTypes.*`：2/2 配置与中性 pacing/turn diagnostics；
- `ABTSM11GravityAssistNumerics.cpp`：Influence 外二进制幂扩步；
- `ABTSM11GravityAssistHash.cpp`：HashSchema 2 的 coast policy 身份；
- `ABTSM11FinaleLayoutHash.cpp`：未来 B v2 Preset Source Hash 同步覆盖新字段；
- `ABTSM11GravityAssistAutomationTests.cpp`：版本隔离、性能、宏步事件、强助推、诊断与并行确定性。

该基线未修改共享 Contract、M6、Config、Build.cs、Target、地图或资产。

M11-A v2.1 在此基础上实际落地：

- `Source/ABTSRuntime/Public/M11Core/`：标准 C++ 数学、数据合同、求解器与一致性语料接口；
- `Source/ABTSRuntime/Private/M11Core/`：唯一的 Types、Hash、Numerics、Encounter、SolveSupport 与 Solver 实现；
- `ABTSM11GravityAssistCoreAdapter.*`：标准类型与 UE `FVector/TArray/FString` 的无损显式转换；
- 原 `World/ABTSM11GravityAssist*.cpp` 算法实现退役，World Solver/Types 仅保留兼容 Facade，因此不存在第二份可分叉物理实现；
- `Tools/M11Core/`：固定 MSVC 14.44、x64、C++20、`/fp:precise` 的 CMake/Ninja Release 构建、机器可读重放、源码身份和 UE 依赖门；
- `ABTSM11GravityAssistAutomationTests.cpp`：11-case 共享语料、全字段非默认转换哨兵和 UE Adapter 精确 parity。

独立工具把生产 Core 源码身份与 conformance/tool 身份分离：修改测试或命令行不会伪装成物理版本变化。源码身份按仓库相对路径排序、换行规范化后做 SHA-256；新增未分类 Core `.h/.cpp` 会在配置期失败。不得创建类似旧 `AngryBirdsToSpace_M11Scratch` 的完整 UE 工程副本，也不得复制一份长期分叉的求解器源码。

## 10. M11-A v2 已通过自动化门槛

### 10.1 A v2 继承门

全新 `UnrealEditor-Cmd -NullRHI` 的 `ABTS.M11A` 应严格发现并通过 15 项（原 8 项 + v2 6 项 + v2.1 parity 1 项）。v2 新门包括：

| 测试 | 阻断门 |
| --- | --- |
| `V2.AdaptiveCoastAndDeterminism` | 重复点/事件/Hash 位级一致；与 v1 终止拓扑一致；权威点数至少减少 8×；终态位置/速度在批准误差内；coast policy 进入 v2 Hash |
| `V2.MacroStepSweptEvents` | 大步两端均在球外时仍解析命中目标和天体；强主星曲率造成“二次弧命中、端点弦未命中”时，宏步拓扑认证强制回退且不漏目标 |
| `V2.StrongAssistAndPacingDiagnostics` | 强作用圈与虚拟动量下完成有序遭遇、无穿透、明显转角、有效换能且重复一致 |
| `V2.PacingDiagnosticsContract` | 三段 coast/dwell/turn/总时长精确；乱序和非连续事件拒绝；续算前缀语义明确 |
| `V2.SerialParallelDeterminism` | 多输入串行与 `ParallelFor` 结果逐项一致 |
| `V2.ProductionLikeThreeAssistCanary` | test-only 使用冻结 v1 nominal 构造三次有效助推 workload，再只把 Request 升为 2/2；必须保持完整事件拓扑、三次非零 shooting、资格/净空、重复结果及冻结 v2 Hash |
| `V2_1.PortableCoreParity` | 独立 Core 与 UE Adapter 共同枚举固定 11-case corpus；Request 身份、Result、事件、点列、串并行结果和 Hash 精确一致；全字段非默认 Request/Result/Pacing 与全部枚举双向往返；非法输入输出保持空净 |

完整门禁：

1. 默认 Development Editor 全链接；
2. 强制 Unity 全链接，防止同翻译单元同名辅助函数再次冲突；
3. fresh-process `ABTS.M11A` 15/15；
4. `ABTS.M11B.Unit`、`ABTS.M11B.Runtime`、`ABTS.M11C.Unit` 快速回归，证明生产 v1 未被切换；
5. HashSchema 1 golden 仍为 `0xd78e8f7153cca7f1`；
6. `git diff --check` 无错误。

墙钟时间受机器负载影响，只记录诊断；确定性 Point 数、事件、Hash 和误差是硬门。若 v1 身份与现有 Bundle Hash 均未变化，本轮无需重跑约七小时级 M11-B v1 慢认证；B v2 一旦选择 2/2，则必须重跑全部慢认证。

### 10.2 A v2.1 新增阻断门

v2.1 在开始 B v2.1 搜索前还必须通过：

1. 标准 C++ Core 可在不包含 UE Header、Module 或 `.uproject` 的独立 Release 构建中编译、链接和运行；
2. UE Adapter 与独立 conformance CLI 编译同一 Core 源文件，仓库中不存在第二份算法拷贝；B 后续加入的 SearchCLI/CertifyCLI 也只能复用该 Core；
3. 固定 parity corpus 的 CLI/UE Request、Termination、事件、关键状态和 Result Hash 逐项一致；
4. CLI 串行、CPU 并行、重复运行和不同分片顺序得到相同逐样本结果；
5. v1 golden、A v2.1 15/15 与 M11-B/C v1 快速回归全部保持；
6. 非法版本、损坏输入、NaN/Inf、未知字段和 Hash 不匹配全部 fail closed；
7. 独立工具输出可复现命令、Core/Tool 版本、输入 Hash 和结果摘要；
8. `git diff --check`、默认 Editor 全链接与强制 Unity 全链接通过。

Python Binding、搜索编排器和 GPU Probe 不属于 A v2.1 数值正确性的阻断实现；但 B v2.1 若使用它们，必须先分别通过第 4 节对应门槛。

## 11. 2026-07-29 M11-A v2.1 归档基线

以下是 M11-A v2.1 当日归档门禁，不代表后文 Search v3 / M11-C v2.1 的当前门禁结果：

| 门禁 | 结果 | 日志 |
| --- | --- | --- |
| 标准 C++ clean Release + CTest | `2/2` | `Tools/M11Core/BuildAndRunPortableConformance.ps1 -Clean` |
| 11-case standalone corpus | `11/11`，expected/repeated/parallel 全通过 | `Intermediate/M11CoreStandalone/bin/ABTSM11CoreConformance.exe --json` |
| Development Editor 默认全链接 | 通过 | UBT `Result: Succeeded` |
| Development Editor `-ForceUnity -DisableAdaptiveUnity` | 通过 | UBT `Result: Succeeded` |
| `ABTS.M11A` | `15/15` | `Saved/Logs/M11A21_Final_M11A_20260729.log` |
| `ABTS.M11B.Unit` | `8/8` | `Saved/Logs/M11A21_Final_M11BUnit_20260729.log` |
| `ABTS.M11B.Runtime` | `4/4` | `Saved/Logs/M11A21_Final_M11BRuntime_20260729.log` |
| `ABTS.M11C.Unit` | `5/5` | `Saved/Logs/M11A21_Final_M11CUnit_20260729.log` |
| `ABTS.M11C.Runtime` | `1/1` | `Saved/Logs/M11A21_Final_M11CRuntime_20260729.log` |
| `ABTS.M110` | `4/4` | `Saved/Logs/M11A21_Final_M110_20260729.log` |
| `ABTS.Contracts.WorldGeneration` | `2/2` | `Saved/Logs/M11A21_Final_ContractsWorldGeneration_20260729.log` |

最终 fresh-process 自动化中：

- `ABTS.M11A` 15/15 成功；
- standalone 与 UE 共同重放 11 类固定输入，Corpus Aggregate 为 `0x4c7af90ade0a28e7`；v1/v2 portable golden 分别为 `0xd78e8f7153cca7f1` / `0xa12de4bf0ac1c0d7`；
- 生产 Core Source Hash 为 `970656c1734da37f26ea9a45be4adb4befb95394cb50c6cb412c8b5e5b9fc3a0`，最终 Conformance/Tool Source Hash 为 `7d9f2a2e941429830ea9cd1eb181df89783de635ed997b7429dce36d1826e1b2`；
- 独立可执行文件不链接 UnrealEditor 或 ABTSRuntime DLL，且源码 include 门未发现 UE/项目依赖；
- 120 秒远场夹具由 v1 的 14,401 点降为 v2 的 901 点，减少约 `15.98×`；
- 同一夹具数次诊断墙钟约由 `5.5–6.0 ms` 降为 `0.55–0.61 ms`；
- 强助推夹具实际转角约 `46.23°`，自然转角约 `52.54°`，能量交换约 `539,024 cm²/s²`；
- 冻结 v1 nominal 转为 2/2 的跨阶段 workload canary 仍以三次助推命中：34,852 点、`558.161570478 s`，数次诊断墙钟约 `28–31 ms`，Hash 为 `0xf3c40a0ab6e0faa1`；
- v1 golden Hash 仍为 `0xd78e8f7153cca7f1`。

以上是算法夹具。三助推 canary 只证明 A v2 能正确且更快地承载真实 encounter/shooting 工作量，不把 v1 布局升级为 v2 生产预设，也不等于 C 已达到同帧反馈；后者仍依赖 B v2 的 60 秒紧凑路径和 C v2 的 latest-only 调度。

### 11.1 Search v3 权威候选结果

`Intermediate/M11B21V3ReadableGate_4096/merged/summary.json` 已完成并通过 merge：`4096/4096` work evaluated、`2` accepted、`2` selected，8 个分片累计 `4912.003924 s`，分片 solver 调用 `1,938,854`，merge replay 调用 `26,511`。统一身份如下：

| 身份项 | 值 |
| --- | --- |
| Evaluation / Candidate Aggregate | `0xac04988c81e25849` / `0xbfeaae4610d4c406` |
| Contract Hash | `0x1e9f208e738a6ef7` |
| Search Source SHA-256 | `27269434b7dff48c26149179776589faa67f2c0ef428849a4833e49deb817738` |
| Production Core Source SHA-256 | `970656c1734da37f26ea9a45be4adb4befb95394cb50c6cb412c8b5e5b9fc3a0` |
| 版本 | Search Contract `3` / Algorithm `3` / Candidate Manifest `3` |

| Rank / work | Candidate Source | nominal Request | nominal Result | Score |
| --- | --- | --- | --- | --- |
| 1 / `2278` | `0xaaae0dd44f14f785` | `0x5ecc893f6eb7003d` | `0xb47d8314ebe69376` | `0xd6e03f2d9e0f3b8b` |
| 2 / `772` | `0xe2c810b38f338e06` | `0x5c07be6f9371448e` | `0xe465b9c154c235a1` | `0xdd1613e3dbb4c1b0` |

两候选的节奏与可读性证据为：

| Rank | 总时长 | 最长 / 最终 coast | 三段实际偏转 | 有符号换侧 | 三段轴投影 | 三段 Influence 时长 |
| ---: | ---: | ---: | --- | --- | --- | --- |
| 1 | `31.268136 s` | `5.065894 / 1.967285 s` | `0.590804 / 0.306536 / 0.645047 rad` | `+ / - / +`，2 次 | `0.934093 / 0.992458 / 0.489981` | `7.923236 / 7.507412 / 3.909778 s` |
| 2 | `31.223673 s` | `4.513232 / 2.050117 s` | `0.404215 / 0.552443 / 0.628360 rad` | `- / + / +`，1 次 | `0.974204 / 0.971059 / 0.976836` | `7.917075 / 4.107608 / 5.579378 s` |

两者三段布局转角分别为 `0.377665 / 0.533365 / 0.385783 rad` 与 `0.348798 / 0.346212 / 0.386060 rad`，均通过 `0.30 rad` 布局门；最小可读实际偏转为 `0.304224 rad` 与 `0.393788 rad`。

ScreenAim 固定 nominal Power 的 5000 点 UX 权威证据如下；每格为 `count / 嵌套比例 / hull area / yaw span × pitch span`，三层 Hull 均包含 nominal、证据数等于对应 count 且合规：

| Rank | S1 | S2 | S3 |
| ---: | --- | --- | --- |
| 1 | `660 / 0.132000 / 331.607413 deg² / 16.040039° × 38.826398°` | `74 / 0.112121 / 66.516190 deg² / 8.156250° × 21.243713°` | `26 / 0.351351 / 19.657068 deg² / 6.093018° × 14.659351°` |
| 2 | `544 / 0.108800 / 393.512510 deg² / 13.385742° × 48.212163°` | `96 / 0.176471 / 55.771759 deg² / 5.449219° × 21.444902°` | `8 / 0.083333 / 1.119872 deg² / 1.054688° × 4.983996°` |

独立 FullLaunchDomain 5000 点只作 Power 维度诊断：Rank 1 的 `S1/S2/S3/S4` 计数为 `96/4/2/1`，嵌套比例为 `0.019200/0.041667/0.500000/0.500000`；Rank 2 为 `102/2/0/0`，比例为 `0.020400/0.019608/0/0`。每个候选的 ScreenAim 与 FullLaunchDomain 都精确执行 5000 次且分别为零 solve failure，Conditional 诊断同样为零 failure。

上述结果只证明候选级节奏、可读性、前缀比例和宽几何合规。两个 manifest 均明确为 `not-certified`，`CertificationHash` 与 `CertifiedBundleHash` 均为零；它们不能替代 v2.2 的完整输入域唯一性、连通分量、边界细化、错序/迟到和消融认证。Rank 2 的 S3 比例 `0.083333` 接近硬下界，必须在有渲染 PIE 中重点判断是否过窄。完整候选统计见 [M11-B v2.1 子稿第 8 节](M11B21CandidateSearchDesign.md)。

### 11.2 自动化与历史基线

Search v2 候选结果已经被 v3 合同明确作废，仅保留为历史对照。当前 v3 Catalog、UBT 与 fresh-process 证据如下：

| 门禁 | 本轮结果 | 证据 |
| --- | ---: | --- |
| 标准 C++ Release + CTest | `5/5` | `Tools/M11Core/BuildAndRunPortableConformance.ps1` |
| 1024 组 Search v2 历史搜索 | 已失效 | 不得使用旧 Aggregate/Catalog |
| Development Editor 默认全链接 | 通过 | UBT `Result: Succeeded` |
| Development Editor 强制 Unity 全链接 | 通过 | `-ForceUnity -DisableAdaptiveUnity`，UBT `Result: Succeeded` |
| `ABTS.M11B.V2_1`（v3 收口归档） | `2/2` | `Saved/Logs/M11V3-20260730-002144-ABTS-M11B-V2_1.log` |
| Additive Search v4 增量门 | Portable `5/5` + fresh `ABTS.M11B.V2_1 5/5` | `BuildAndRunPortableConformance.ps1`、`Saved/Logs/M11B21-V4-Final63b-20260730-191819-FreshAutomation.log` |
| `ABTS.M11B.Unit / Runtime` | `8/8 + 4/4` | `Saved/Logs/M11V3-20260730-002705-ABTS-M11B-Unit.log`、`...-002746-ABTS-M11B-Runtime.log` |
| `ABTS.M11C.Unit / Runtime / V2_1` | `8/8 + 2/2 + 2/2` | `Saved/Logs/M11V3-20260730-002336-ABTS-M11C-Unit.log`、`...-002625-ABTS-M11C-Runtime.log`、`...-002241-ABTS-M11C-V2_1.log` |
| 上游快速回归 | `M11A.V2_1 1/1 + M110 4/4 + Contracts 2/2` | `Saved/Logs/M11V3-20260730-002829-ABTS-M11A-V2_1.log`、`...-002908-ABTS-M110.log`、`...-002950-ABTS-Contracts-WorldGeneration.log` |

v3 收口时九组过滤器合计 `33/33` 成功，并分别具有唯一的 `TEST COMPLETE. EXIT CODE: 0`；Additive Search v4 另在当前源码上通过 Portable `5/5`（含已注册整链 particle fixture）与 fresh `ABTS.M11B.V2_1 5/5`，其中 UE 成功夹具还完成 `1 / 4` 线程整链一致性对照。未在本次增量提交中重跑的 v3/M11-C/上游行仅作为归档证据。本轮不改变 production `SolverVersion=1 / HashSchemaVersion=1` 路径，也不以 NullRHI 自动门替代用户有渲染 PIE 或 M11-B v2.2 完整输入域认证。

### 11.3 Additive Search v4 条件粒子束结果

Search v4 作为 v3 旁路构造器存在：先沿同一父前缀的实际存活输入逐星放置行星、做 coarse/refinement Beam 与独立 Holdout，最后把极少量布局交还给冻结 v3 的 ScreenAim 5000、FullLaunchDomain 5000、Conditional、robust、低功率和全消融审计。它不改变当前 v3 Candidate Source，也不自动更新 Editor Candidate Catalog。

| Seed | Solver calls | Accepted / Selected | Accepted / M solve | 相对 v3 |
| ---: | ---: | ---: | ---: | ---: |
| `296882177` | `10,270` | `0 / 0` | `0` | `0×` |
| `296883201` | `50,186` | `2 / 2` | `39.8518` | `38.63×` |
| `296883202` | `47,925` | `2 / 2` | `41.7319` | `40.46×` |
| `296883217` | `83,097` | `5 / 2` | `60.1706` | `58.33×` |

Seed `177` 在 Stage 3 正常无解退出；Seed `201/202` 重复找到 Candidate Source `0xcdc6e41075d99493` 与 `0x80d274a67e1e9944`；Seed `217` 的两个多样化结果为 `0xed74ffaf0de8028f` 与 `0xf22ad256fd791e07`。后两者总时长分别为 `27.843 / 30.086 s`，三次实际偏转分别约为 `36.44° / 34.38° / 65.62°` 与 `36.44° / 34.38° / 60.19°`，都发生两次真实换侧。

数值代理支持优先把 `0xed74ffaf0de8028f` 纳入下一轮 Editor-only PIE 比较，但它的 ScreenAim S1/S2/S3 比例为 `0.145400 / 0.254470 / 0.156757`，尚未达到每级约 `1/4` 的理想面积；完整三维 5000 点诊断也只有 `78 / 21 / 1 / 1` 个 S1/S2/S3/S4 成员。它仍是 `Candidate / NOT CERTIFIED`。算法、Holdout、Hull 和全部候选分析见 [Additive Search v4 子稿](M11B21ConditionalParticleBeamSearchDesign.md)。

### 11.4 Rank 3 v2.2 认证早停

`Rank 3 / CandidateSourceHash=0xed74ffaf0de8028f` 已按用户 PIE 选择冻结为唯一
v2.2 输入。独立标准 C++ 认证工具已经落地规范化三维索引、分片/恢复/合并和
六邻域统计。`2° × 3° × 0.025` 的 `16,359` 点稀疏扫描得到
`F4=4 / Components=2`；`1° × 1.5° × 0.0125` 的 `122,877` 点半步扫描得到
`F4=27 / Components=9`。两轮均满足 `F4 ⊂ F3 ⊂ F2 ⊂ F1`，且半步 F4 只出现
在 Power 索引 `71..80/80`，但唯一六邻域 F4 门失败。

本轮因此停在 B v2.2 发现阶段，没有生成 Trust Region、CertificationHash 或
CertifiedBundle，也没有进入 C v2.2。详细证据和后续约束见
[M11-B 设计稿第 7.5 节](M11BFinaleLayoutCertificationDesign.md)。

后续三层局部递归进一步确认：`0.5°/0.75°/0.00625` 层为
`F4=209 / Components=22`，`0.25°/0.375°/0.003125` 层为
`F4=1664 / Components=40`；针对离群锚点到主岛的
`0.125°/0.1875°/0.0015625` 桥区扫描仍得到
`F4=1565 / Components=32`。最后一层 F1/F2/F3 已全部连通，F4 最大分量为
1421 点，其余 31 个分量不超过 7 点，说明 Rank 3 是“主成功岛 + 终端命中碎片”，
而不是单纯被粗网格切断的一条连续成功带。Rank 3 继续保持未认证状态。

只移动 UFO 的后续实验共检查 105 个不重复位置，覆盖全方向 ±2000 cm、改善方向
至约 9000 cm 以及最优附近 500 cm 精扫。最佳
`Offset=(2500,1000,-9000) cm` 把第一层主 F4 分量从 `55/209` 扩大到
`266/325`，碎片点由 154 降至 59，但仍有 15 个六邻域分量。因此“仅微调 UFO
位置”被否决：不生成派生候选，不进入更细认证。下一轮应回到 B v2.1 联合调整
终端几何/方向资格，或重搜第三颗行星至 UFO 的末段映射。

2026-07-31 的后续联合诊断进一步排除了三个低成本修补方向：缩小 UFO
HitRadius、按名义到达速度/命中面裁剪，以及在约 30 m 范围内联合微调行星③与
UFO。联合微调可把粗层 F4 碎片点从 59 降至 19，但半步 42025 点复核仍得到
`F4 Components=32 / Largest=1000 / Total=1083`。因此该结果不生成新 Rank、不进入
PIE 绑定，也不启动完整输入域认证；下一轮边界提升为“保留 Rank 3 前两次助推，
重新构造第三次 B-plane/虚拟动量与 UFO 的末段映射”。详细参数和证据见
[M11-B 设计稿 7.7](M11BFinaleLayoutCertificationDesign.md#77-rank-3-终端拓扑联合修复实验2026-07-31)。

随后执行的行星③至 UFO 末段重映射共搜索 1408 个主样本，并逐轮把 5733 点
F4 碎片从 22 压到 3；最佳研究候选的 42025 点复核仍为
`F4=1373 / Largest=1352 / Components=12 / Fragments=21`。终端走廊质量阈值、
命中半径和沿出口方向移动 UFO 均不能消除残余。该候选不绑定、不认证；完整参数、
Hash 和搜索轨迹见
[M11-B 设计稿 7.8](M11BFinaleLayoutCertificationDesign.md#78-rank-3-行星至-ufo-末段重映射搜索2026-07-31)。

2026-08-01 开放行星②上游映射后，局部候选 353 首次在 5733 点与 42025 点
两级网格均保持 F4 严格单岛；半步结果为 `F4=1004 / Components=1 / Fragments=0`，
且不同线程数重放 Hash 一致。它已成为唯一待认证输入，但仍未生成 Certification/
Bundle Hash、未加入 PIE Rank 或生产绑定。参数与后续完整认证边界见
[M11-B 设计稿 7.9](M11BFinaleLayoutCertificationDesign.md#79-行星②上游映射搜索与单岛候选-3532026-08-01)。

## 12. 多工作树交接

M11 专属工作树继续不直接修改下列共享热点：

- `Docs/ABTSProjectWorkflow.md`；
- `Docs/ABTSMultiWorktreeDevelopmentGuide.md`；
- `Docs/AngryBirdsToSpaceGameDesign.md`；
- `Docs/M110PreFinaleClosureDesign.md`；
- `Public/Contracts/**`、`Private/Contracts/**`、`ABTSM110*`；
- `ABTSM6*` 共享热点及任何共享资产。

合并后由集成工作树：

1. 在项目工作流中加入本稿链接、A v2/v2.1 状态以及 B v2.1 → C v2.1 → B v2.2 → C v2.2 的交接门；
2. 若 C v2 需要共享 M6 输入适配器，以向后兼容接口单独集成；
3. B v2.1 候选体验期保持生产 v1，不把 Candidate 复制到正式默认值；
4. 用户批准候选并冻结后，由独立标准 C++ 工具执行 B v2.2 全输入域与消融慢认证；
5. B v2.2 认证通过后才允许 C v2.2 在生产路径绑定 2/2 Certified Bundle。

上游与返回父级：[M11 算法预演](M11GravityAssistAlgorithmPrevisualization.md) · v1 基线：[M11-A](M11AGravityAssistSolverDesign.md) · [M11-B](M11BFinaleLayoutCertificationDesign.md) · [M11-C](M11CFinaleInteractionAndPlaybackDesign.md)；M11-B v2.1 的 v3 基线见 [候选搜索子稿](M11B21CandidateSearchDesign.md)，新构造器见 [Additive Search v4 子稿](M11B21ConditionalParticleBeamSearchDesign.md)。当前工作点是 **保留 v3 Catalog → v4 新候选已生成但未绑定 → 选择少量候选更新 Editor-only 比较池 → M11-C v2.1 用户有渲染 PIE → 参数冻结 → M11-B v2.2**。
