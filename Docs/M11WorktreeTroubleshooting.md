# M11 工作树排错记录

> 编码：UTF-8，简体中文。
>
> 状态：持续维护；本次整理覆盖 2026-07-28 22:53 进入 M11 专属工作树之后至 2026-08-04 的问题。
>
> 上位总表：[开发排错总文档](DevelopmentTroubleshooting.md) · 工作树规则：[多工作树协作集成规范](ABTSMultiWorktreeDevelopmentGuide.md)
>
> 相关设计：[M11-A 求解器](M11AGravityAssistSolverDesign.md) · [M11-B 布局与认证](M11BFinaleLayoutCertificationDesign.md) · [M11-B v2.1 候选搜索](M11B21CandidateSearchDesign.md) · [M11-C 交互与播放](M11CFinaleInteractionAndPlaybackDesign.md) · [M11 终局 HUD 优化](M11CFinaleLaunchHUDOptimizationDesign.md) · [M11 v2 优化总设计](M11V2FinaleOptimizationDesign.md) · [M3R-5.2/M11 预览接线](M3R52M11PreviewFinaleIntegrationDesign.md)

## 1. 用途、边界与更新规则

本文是 `feature/m11-finale` 工作树的增量排错账本，不替代共享的
[开发排错总文档](DevelopmentTroubleshooting.md)。M11 工作树只在本文记录；每次合并到集成工作树时，由集成工作树筛选、去重并整理进总文档。

记录规则：

- 每条必须保留“现象—根因—处理—防回归验证”，不能只写最后一次参数改动。
- `【已修复】` 表示代码或合同已经落地；`【设计已替代】` 表示历史故障仍值得保留，但当前交互合同已经改变；`【候选未认证】` 表示只足够做 PIE 比较；`【开放】` 表示仍需证据或修复。
- 候选布局的 PIE 手感、5000 点 ScreenAim 统计和 M11-B v2.2 完整认证是三种不同证据，禁止互相代替。
- 自动化若使用 `NullRHI`，只能证明数据、生命周期和绘制命令合同；SceneCapture、材质、光照、实际像素以及镜头构图仍以 fresh-process 可见 PIE 为最终证据。
- 后续 M11 会话发现新问题时，先更新对应条目；同一根因的复发补充证据，不重复新增近义条目。

## 2. 构建、线程与异步生命周期

| 现象 | 根因 | 处理 | 防回归验证 |
| --- | --- | --- | --- |
| 【已修复】M7 工作树做默认 Development Editor Unity 全链接时，M11 的 `ABTSM11GravityAssistSolver.cpp` 报 `C2668: IsFiniteVector 调用不明确` | Unity Build 把多个 `.cpp` 合入同一翻译单元；`ABTSM11GravityAssist::IsFiniteVector` 与 `ABTSM11FinaleSystem.cpp` 匿名命名空间中的通用 `IsFiniteVector` 同时进入候选集。类似地，M11-C 两个 `.cpp` 各自的 `SameInput` 也会在强制 Unity 下重定义。单文件编译通过不能证明 Unity 安全。 | 将职责不唯一的帮助函数改为文件域唯一名称：`IsFiniteFinaleBoundaryVector`、`SameInteractionInput`、`SameSolvedInput`。修复起点为提交 `85e9088`，后续 Unity 门一并收口。 | 关闭本工作树 Editor，分别执行默认 Development Editor 和强制 Unity 全链接；不能只跑 non-unity 或增量编译。 |
| 【已修复】进入终局发射模式时偶发 `Ensure condition failed: IsInGameThread()`，堆栈落在 `FAppTime`、Renderer、RenderCore | 旧实现从普通 ThreadPool worker 直接 `AsyncTask(GameThread)` 发布结果。该 worker 不是由 Game Thread/TaskGraph 继承出来的任务，没有继承 `FAppTime` 上下文；从它派生的 SceneCapture/Renderer 工作沿错误上下文进入渲染线程。 | worker 只计算并返回纯数据 `TFuture`；由终局 Actor 的原生 Game Thread Tick 轮询、消费和发布，SceneCapture 只在该 Tick 尾部触发。v2.1 又把捕获收敛为首次有效结果或目标切换，同目标新结果只更新 HUD 局部轨迹。落地于 `b37968b` 及后续 v2.1。 | 自动化验证 worker 不触碰 UObject、HUD、SceneCapture；fresh 可见 PIE 连续进入/退出/重进终局并快速调参，日志不得再出现 `FAppTime`/`IsInGameThread()` ensure。`NullRHI` 不能代替这项渲染验收。 |
| 【已修复】移动一次输入后可能约半秒才更新轨道全览，连续移动还会显示过时结果 | 每个输入都执行较长轨迹积分；旧链路没有严格的 revision/最新结果语义，过时求解和 SceneCapture 更新会挤占后续输入，渲染捕获又被错误地绑定到每次鼠标微调。 | 同时最多保留一个 in-flight 纯数据求解；新输入只更新 revision/dirty，Tick 只发布仍与当前输入一致的结果并丢弃 stale result。捕获按目标缓存，同目标只刷新局部线条。标准 C++ Core 通过稀疏输出、宏步进和分段细化降低单解成本。 | HUD/日志同时显示 solve ms、端到端 latency 和 discarded 数；快速往返调参后只允许最新 revision 生效。性能门看同帧/下一帧反馈，不能只看单次积分耗时。 |

## 3. 终局输入、失败恢复与相机

| 现象 | 根因 | 处理 | 防回归验证 |
| --- | --- | --- | --- |
| 【设计已替代】早期终局发射的鼠标被接管且隐藏，袋子不直接跟随鼠标、左右方向相反、灵敏度和普通弹弓不同；进入模式的同一次松键还可能没有发射，之后需要再次点击 | 初版把相对鼠标位移直接映射成求解器 Yaw/Pitch，并同时承担“点击进入”和“松开发射”两种手势；视觉袋位置与 canonical solver pouch 未分层，输入捕获和 release gate 也不清晰。 | `b37968b` 先分离进入点击与后续有意的按下/松开，并修正方向、焦点丢失和视觉袋映射。HUD-1A/B 后正式替代旧合同：点击太空弹弓只进入控制台；鼠标可见、`GameAndUI`；Yaw/Pitch/Power 由三旋钮和 `1x/0.1x/0.01x` 档控制；只有 `LAUNCH` 提交。视觉袋可动但 canonical pouch 不动。 | 验收必须按当前 HUD 合同，不再用“与 M6 松鼠标立即发射完全相同”作为预期。检查进入点击不误发射、旋钮捕获互斥、LAUNCH 按下/释放只提交一次、焦点丢失取消捕获。 |
| 【已修复】错误发射后鸟返回主星附近并扎进地表，既不出现失败黑屏，也无法继续 | 失败表现沿权威积分点直接走到解析球体内部或恢复碰撞早于安全回位；失败时间线没有统一的可读停留、全黑和恢复边界，长 miss 还可能没有有界终点。 | 表现终点解析到 `BodyRadius + BirdClearance`，不改权威结果/Hash；失败统一走“可读停帧 → 淡黑 → 全黑安全恢复 → 淡出黑屏”。全黑时先关闭碰撞，再恢复原始 Transform/Movement，最后恢复碰撞；长 miss 也有上限。落地于 `b37968b`。 | `ABTS.M11C.Unit.PIERegressionContracts` 覆盖时间线合同；可见 PIE 分别测试撞主星、撞助推行星、飞歪和长时间无事件，均须黑屏恢复到进入终局前，且鸟不穿地、不残留禁用碰撞。 |
| 【已修复】太空弹弓相机在 Blueprint 中调整后，与前面普通弹弓相机的最终效果不同 | M11 曾直接生成原生 `AABTSM6SlingshotCamera::StaticClass()`，绕过 M6 运行时实际配置/生成的 Blueprint 子类，所以 BP 默认值和组件配置没有继承。 | M11 从唯一的 M6-owned 运行时相机解析精确类并生成同一子类；找不到或出现多个候选时 fail closed，同时输出 `CameraClassParity`。修复提交 `5af544e`。 | 自动夹具加载 `/Game/Blueprints/BP_ABTSM6SlingshotCamera` 并验证精确类相等；可见 PIE 对比普通/太空弹弓相同 BP 参数的构图，而不是只检查 C++ 基类。 |
| 【已修复】发射后主镜头停在太空弹弓原处，不跟随权威轨迹中的鸟 | M11-C 只插值移动鸟体，没有为发射态建立独立 ViewTarget 和沿轨道更新的相机姿态。 | 新增瞬态 `AABTSM11FinaleFlightCamera`，只消费权威位置/速度；用平行运输维护沿轨 Up，不参与 Chaos、积分或命中。Release 时切换 ViewTarget，失败/重置先回 Aim Camera，再交还 Party Camera。落地于 `9e504e7`。 | 自动化验证相机不改权威状态；可见 PIE 检查发射、三次近掠、成功/失败恢复期间 ViewTarget 顺序和连续性。 |
| 【开放】实飞一段后鸟离开画面；远处 UFO 也在世界中但几乎不可见，画面整体被浅蓝雾化 | 当前证据支持两个叠加原因：飞行相机以有限 `FollowLagSpeed` 追赶每帧按权威路径跳转的鸟，高速段稳态跟随误差会放大并把鸟推离视锥；关卡仍有 `ExponentialHeightFog`、`VolumetricCloud`、`SkyAtmosphere`，终局尺度下远处小物体与蓝色背景对比被吃掉。暂未发现 MaxDrawDistance/Hidden 直接剔除的证据。这一根因仍是待验证推断。 | 尚未修复。先做 A/B：关闭雾云但不改相机；再把相机位置改为权威同步基座、仅平滑局部表现偏移，并记录 Bird–Camera 距离和视锥状态。若只关闭雾云就恢复 UFO，则把终局天空/雾状态纳入 M11-D/集成接线；共享地图灯光资产仍由集成工作树修改。 | fresh 可见 PIE 分别验证无 lag、无雾云、两者同时开启；全程鸟须在安全框内，三颗行星/UFO 在预期距离保持可读。修复前不得把此项标为光照已确认。 |

## 4. 画中画、轨迹引导与表现语义

| 现象 | 根因 | 处理 | 防回归验证 |
| --- | --- | --- | --- |
| 【已修复】远端画中画只能看见行星模型，看不到预计轨迹；若直接随每个新解重摆相机，画面还会随微小调参乱晃 | SceneCapture 只能捕获注册到场景的组件，解析轨迹只是纯数据，不会自动出现在捕获纹理中；把相机姿态绑定每次最近点又会造成构图抖动和高频捕获。 | SceneCapture 只画当前目标 Actor；HUD 在同一 PIP 框上叠加当前权威结果的局部轨迹。目标始终居中，视向固定为“上一目标 → 当前目标”（首段为 pouch → Planet1），Up 固定为 Finale 局部 `+Z`；同目标的新解不重拍背景，只更新线条。另提供单目标 Wedge。落地于 `9e504e7`。 | 可见 PIE 检查三个行星和 UFO：轨迹必须与目标同时可见；轻微调旋钮时背景不乱晃、线条即时移动；切换目标时才允许背景重构图。 |
| 【已澄清】AUTO PIP 开关前后看到的内容不像同一台画中画，玩家难以判断两者关系 | AUTO 和 Probe 共用同一条权威轨迹，但观察锚点不同：AUTO 按当前最深前缀自动选择下一目标；Probe 固定在玩家从轨道全览点选的轨迹时刻/局部 frame，直到 REBASE、重新点选或恢复 AUTO。二者不是两套积分器，也不应互相覆盖选择状态。 | HUD-1C 明确两种状态和切换操作；`8b6eeee` 统一物理 RenderTarget、屏幕矩形和边框，保留不同的标题/局部轨迹语义。低模资产以后只替换 SceneCapture 中的目标 Actor 表现，不替换全览解析图标或求解器半径。 | 固定一条轨迹分别切 AUTO/Probe：权威 ResultHash 不变；AUTO 随前缀换目标，Probe 保持所选时刻；REBASE 只更新 Probe 基准；画框位置和尺寸不跳。 |
| 【已修复】AUTO PIP 与玩家点选轨迹后的 Probe PIP 大小和位置不同，切换时画框跳动 | 自动预览和轨迹探针走了两条独立绘制路径，各自计算 RenderTarget 矩形、边框和标题。 | 两套语义共享同一个 RenderTarget、布局函数和框体，只替换相机/叠加信息；修复提交 `8b6eeee`。 | 在同一分辨率下反复切换 AUTO/Probe，框体左上角、尺寸、边框厚度和标题基线完全不跳动。 |
| 【已澄清】F4 合格终端拦截包络大于 UFO 视觉本体时，权威轨迹可能未碰视觉网格就判定成功 | “可玩的终端资格”与“物理可见接触”曾被混为同一半径。大包络用于保持 F4 成功岛可玩，若直接把它当撞击点就会出现隔空命中。 | 保持 `Qualified TargetHit/TargetApproach` 与约 800–1000 cm 的 `Physical TargetContact` 分层。进入合格包络只确认终局传递；表现轨迹再沿冻结的确定性 terminal transfer 到 UFO 视觉接触点，之后才播放撞击/救援。不能用 Chaos 把鸟临时硬拉并反向修改求解结果。 | 日志和播放事件必须能区分 Qualified Endpoint 与 Physical Contact；白闪、UFO 受击、救援只允许发生在可见接触后。Hash/认证仍以冻结权威结果为准。 |

## 5. 求解器性能、候选构造与认证方法

| 现象 | 根因 | 处理 | 防回归验证 |
| --- | --- | --- | --- |
| 【已修复流程】早期一次 M11-B 候选搜索耗时约 7.5 小时，而且每调一次手感参数都可能重新付出 UE 编译和完整认证成本 | 搜索与 UE Runtime 耦合，候选构造、手感筛选和全域认证没有分阶段；旧全局 4096-work 枚举还对大量显然失败布局做完整求解。 | M11-A v2.1 提取不依赖 UE 的标准 C++ `M11Core`，UE 只保留显式适配层；M11-B CLI 与 Runtime 编译同一份 Core。Python 仅负责启动/分片/恢复/聚合 C++ CLI，不积分、不分类、不算 Hash、不排名。正式顺序改为：B v2.1 找候选 → UE parity 快测 → C v2.1 PIE 手感 → B v2.2 完整认证 → C v2.2 正式绑定。 | standalone corpus 与 UE corpus 的输入、结果、事件和 Hash 逐项 parity；产物记录 Core/Tool Source Hash。任何 Python 数值重实现都视为第二权威并拒绝。 |
| 【已修复构造策略】旧候选常在三颗行星同侧画平缓弧线，偏转不明显；Planet1 几乎覆盖全部可行鼠标区，Planet1/2/3/UFO 又接近共线 | 旧固定候选池和总分更重视“能依次到达”，没有把投影后真实转角、左右换侧、ScreenAim 前缀留存率和各级独立凸包作为足够强的构造门。 | 新增逐星条件粒子束构造器：先在父可行域边缘提出并锁定 Planet1，再只在通过父前缀的样本上提出下一星；每级检查真实偏转、换侧、无偏 5000 点 ScreenAim 比例和独立凸包。旧候选保留为对照，不被新构造器覆盖。 | 每个候选报告三次实际偏转角、符号/换侧、总飞行时间、S1/S0、S2/S1、S3/S2、各级 Hull 图；最终仍须 PIE 判断“引力弹弓感”，统计不能单独放行。 |
| 【候选未认证】在 Candidate 353 上局部提高 `F3/F2` 并合并 F4 后，新候选的偏转明显变小，还失去原 Rank 3 的交叉换侧性质 | 局部目标函数主要奖励前缀留存率和 F4 连通，未把基线候选的每段偏转下限、符号序列和投影路径相似度设为不可牺牲约束；优化器因此可用“把轨迹拉直”换取更大的成功域。 | 后续局部搜索必须同时约束三次最小偏转、换侧符号/次数、相对基线的轨迹距离和总时长；若这些门失败，即使 `F3/F2` 或 F4 topology 更好也只能作为诊断候选，不能覆盖原 Rank。 | 每轮局部优化前后并排输出偏转角、符号序列、ScreenAim/Hull、F4 分量和 flight time；PIE 必须能从全览直接辨认交叉偏转。 |
| 【已修复节奏目标】v1 权威轨迹可长达约 558–700 秒，行星之间存在很长的无偏转航段 | v1 的远场合同允许 700 秒，尺度、速度和虚拟动量使慢速长航成为合法解；求解正确不等于一分钟内有可玩的演出节奏。 | v2 将目标总时长收敛到 60 秒内，单个强偏转区约 5 秒；提高引力作用圈、虚拟动量和初速度，并在搜索评分中加入时长与平淡期门。稀疏输出和事件附近细化同时降低预览点数。 | 候选报告总时长、三段进入/退出时间和段间最长平淡期；可见 PIE 以正常播放速度完整观看，不能用快放掩盖 700 秒权威轨迹。 |
| 【候选未认证】Rank 3 在 v2.2 预认证中早停：稀疏/half-cell/局部细化都出现多个 F4 六邻域分量 | 这不是单纯网格太粗造成的“对角线误断”。递归细化后仍存在一个主岛和多个极小真实碎片：终端包络、Assist3 出口和方向锥相交形成狭窄旁瓣。把六邻域改成 18/26 邻域只会掩盖拓扑，不会证明输入域唯一。典型证据：half-cell `F1/F2/F3/F4=2067/553/72/27` 且 F4 为 9 分量；更细桥区 F4 仍有一个 1521 点主岛和 31 个不超过 7 点的碎片。 | 认证按设计早停，没有伪造通过。先试 UFO 小范围平移/半径和方向锥修复；不足时回到 Assist3 B-plane/虚拟动量/terminal mapping 做局部重构，再从粗网格重新认证。 | 正式门仍是完整域六邻域唯一 F4 连通分量、`F4 ⊂ F3 ⊂ F2 ⊂ F1`、边界递归闭包、单星消融/错序/多圈/迟到旁路失败。不得只报告最大分量占比。 |
| 【已修复认证语义】Candidate 353 在 PIE 中看似正常，但日志/认证出现 `TargetHit` 早于 `Assist3 Exit` | 临时候选分类只检查“最终完成三次 assist 且曾进入目标包络”，没有把事件发生顺序纳入 F4；较大的终端包络与第三颗行星作用区重叠，轨迹在尚未合法退出 Assist3 时就先触发 TargetHit。视觉上仍像飞过第三星，因此 PIE 不容易察觉。 | 收紧成功事件序列为 `Assist3 Exit → TargetApproach → TargetHit`，CLI、Core 测试和认证分类使用同一语义。Candidate 353 及其派生 Rank 只能继续作为 preview，不能以旧计数晋级认证。 | 测试显式断言事件单调顺序；错序命中必须归为失败。PIE“看不出异常”不是事件序列证据。 |
| 【已澄清】同一候选的“5000 → 727 → 185 → 29 → 10”看起来接近理想，但 half-cell 网格的比例和分量变化很大 | 前者是在最大 Power 下对二维 ScreenAim 做固定 5000 点撒点，衡量鼠标手感/凸包；后者是 `Yaw × Pitch × Power` 三维结构网格及 half-cell 偏移，衡量体积与六邻域连通性。采样维度、测度和邻接规则都不同，计数不可直接比较。窄斜带还可能在粗六邻域中暂时断开。 | 所有报表强制标注 Domain、Power slice、屏幕坐标映射、采样种子/分辨率和邻接规则。ScreenAim 只决定可玩比例/Hull，局部细化解释粗网格断带，完整三维网格决定认证。 | 任何比例都必须附 `ScreenAim@Power=...` 或 `FullLaunchDomain/Grid` 标签；不得拿 2D 满功率结果证明低功率无旁路或三维 F4 唯一。 |
| 【候选未认证】Rank 8 的 PIE 和满功率 Hull 合格，但全 Power 粗扫发现 F1 最低 Power 约 0.75，未达到预期 0.88 门槛 | “把 Planet1 放远即可严格单调提高 Power 门槛”只是近似直觉。改变初始速度还会改变到达相位、入射方向和引力捕获，四天体刚性平移也不保证 F1 阈值按距离单调变化。若局部认证从 0.875 起扫，更不能证明 0.875 以下不存在旁路。 | 先在完整 Power 范围做廉价粗扫并定位 Planet1 初次可达区间，再在近最大 Power 的 `Yaw × Pitch × Power` 细扫；Planet1 径向外移约 5900 cm 只作为 Rank10 构造起点，后续允许各星小幅角向修复。 | 慢认证前必须输出全 Power 粗扫、最低合格 Power 和低功率旁路计数；最终仍需覆盖完整 Power 闭包，不能因为设计上“第一星较远”就删除 Power 维。 |
| 【候选未认证】仅按统一比例放大视觉/碰撞/引力半径、星间距、初速度和时间步后，原候选没有自动保持可用 | 连续物理在所有有量纲参数严格相似变换时才可能保持无量纲轨迹；实际搜索还含固定的角域、时间上限、步长/扫掠误差、事件阈值、方向锥、评分和凸包门。时间插值若按错误方向缩放还会改变离散积分误差，因此“所有数值乘同一倍数”不等于认证不变。 | 首次放大实验按失败证据保留（`2388762`），没有硬绑定；随后用逐星搜索重新调距离/角度和 terminal 参数，产出视觉半径 5000 cm、解析碰撞半径 5500 cm 的 Rank11 候选（`3ff0485`，PIE 暴露于 `910a52f`）。 | 每次尺度变化都重新跑 Core parity、扫掠碰撞、事件顺序、ScreenAim/Hull 和完整域认证；候选身份与 Hash 必须改变。Rank11 仍是 Candidate/NOT CERTIFIED。 |
| 【已澄清】提高三颗助推行星的视觉半径后，担心解析轨迹未碰撞但画面穿进模型 | VisualRadius、CollisionRadius 和 InfluenceRadius 是三种职责；只放大视觉而不提高解析 swept collision 半径就会出现视觉穿模。 | 当前放大候选以 VisualRadius 5000 cm、CollisionRadius 5500 cm 留出鸟体和数值净空；碰撞继续由标准 C++ 解析扫掠判断，不交给 Chaos。InfluenceRadius 可更大，但不能反向当实体表面。 | 测试最小近掠距离必须大于 `CollisionRadius + BirdClearance`；可见 PIE 检查近掠外轮廓。替换低模后须按实际包围球重新核对，而不是只改缩放。 |

## 6. 轨道全览 HUD 的坐标、模式与裁剪

| 现象 | 根因 | 处理 | 防回归验证 |
| --- | --- | --- | --- |
| 【已修复】按钮画在一个位置，实际点击热区整体偏到左侧；第一轮修复后 SELECT/MOVE 等按钮仍残留偏移 | 这是两个叠加坐标变换。第一层：`UCanvas` 用 DPI 调整后的 `ClipX/ClipY` 绘制，输入仍是原始 viewport 像素；第二层：`GameViewportClient` 会按 `SceneView::UnscaledViewRect.Min` 平移玩家 Canvas，鼠标仍相对完整 viewport。只修 DPI，没有扣除 player-view origin，因而残留固定偏移。 | `9836a0a` 统一 raw viewport → HUD Canvas logical space；`d0fcf16` 再纳入 `UnscaledViewRect` 的 origin/size。所有按钮、旋钮、轨迹点选、拖拽、滚轮和 release hit test 都只消费转换后的 HUD 坐标。 | 自动化覆盖 DPI≠1 和非零 viewport origin；在 PIE 实测窗口缩放、编辑器嵌入视口、不同分辨率，点击每个控件中心和边缘均与绘制一致。 |
| 【已修复】ROTATE 拖动不符合直觉，稳定 Up 约束让全览像被强行扶正；随后上下拖动方向又与画面旋转相反 | 把局部 Up 固定为世界/Finale Up 会抵消玩家希望的自由轨迹球滚转；屏幕 Y 向下与轨迹球俯仰符号又被映射了两次。 | `026b60b` 改为绕全览中心的自由 trackball/四元数旋转，不再每帧重建固定 Up；`c79ae0e` 修正垂直符号。随后 `f8191da` 将模式改名为 MOVE：左键平移枢轴，右键绕枢轴旋转，初始枢轴为包围球中心；SELECT 不读取或改动枢轴。 | 右拖上下左右时目标运动方向与手势一致，连续斜拖允许自然滚转；切换 SELECT 不改变视图枢轴，RESET 恢复包围球中心。 |
| 【已修复】主星蓝色外圈、经纬线、行星影响圈或轨迹线可能越过圆形轨道全览边界 | 早期只裁剪轨迹中心点或部分图元，忽略线宽、圆半径、相交线段和文本真实尺寸；主星投影圆靠近边缘时尤其明显。 | `f1e5be6` 将主星/网格、行星/UFO、影响圈、实虚轨迹、Hover/Probe 全部送入统一圆裁剪，按线宽向内缩；标签按实际文本 bounds 收口。只有外框本身允许落在边缘。 | `ABTS.M11C.HUD.Unit` 与 `ABTS.M11C.Unit` 覆盖几何合同；可见 PIE 在平移、缩放、旋转及极端候选下检查所有图元，无一像素明显溢出圆框。 |

## 7. M3 生成世界与候选预览接线

| 现象 | 根因 | 处理 | 防回归验证 |
| --- | --- | --- | --- |
| 【已修复接线】M11 候选最初只在本地独立预览坐标中成立，不能证明接入 M3 Task Graph 生成世界后仍相对终局弹弓正确摆放 | 候选使用终局局部布局预设，但 M3 生成的 launch frame、月度世界位置和编辑器预览入口没有统一桥接；若退回世界坐标硬编码，会随世界生成结果失效。 | M3R-5.2 输出冻结的 Finale Preview Frame，M11 只把候选局部位置变换进该 frame；提交 `a87cfda` 接通，`5b62856` 补充月度 frame 验证。PIE 前可用 `abts.M11.CandidateRank <n>` 切换 Candidate Catalog，不修改 production 认证绑定。 | 对多个月度/M3 frame 验证平移旋转兼容、哈希和局部相对关系；控制台必须明确显示 `EDITOR CANDIDATE / NOT CERTIFIED`、Rank 和 Source Hash。共享地图、Task Graph 稳定契约仍由集成工作树修改。 |

## 8. 集成工作树摘录清单

集成工作树每次整理本文时，至少检查：

- 本轮新增条目是否已经有稳定根因，而不是只有参数猜测；
- 已修复项是否包含 commit、自动化或可见 PIE 中至少一种可重放证据；
- Candidate、Certified 和 Production Binding 是否被明确区分；
- `NullRHI` 的结论是否被误写成 SceneCapture/光照像素验收；
- 是否涉及共享地图、天空/雾云、默认 Blueprint 或稳定契约；若涉及，只摘录结论，实际修改仍留在集成工作树；
- 摘录完成后不删除本文历史，后续复发继续在原条目追加日期和新证据。
