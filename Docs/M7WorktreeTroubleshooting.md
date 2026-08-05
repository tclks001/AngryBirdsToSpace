# M7 功能工作树排错记录

> 编码：UTF-8，简体中文。
>
> 文档性质：`feature/m7-buildings` 的增量排错台账，不是共享总文档。
>
> 记录范围：从 2026-07-28 22:59（Asia/Hong_Kong）本工作树会话建立职责约定起，至当前 M7.3 Beam-C3 稳定芯体收口。
>
> 上收关系：集成工作树在合并 M7 阶段提交时，从本文提炼已确认条目到
> [开发排错总文档](DevelopmentTroubleshooting.md)。总文档由集成工作树所有；M7 功能工作树不得直接修改。

## 1. 使用与维护规则

- 本文只记录 M7 工作树新遇到、且尚未完整进入共享总文档的问题。
- 每条至少保留“现象—根因—修复—防回归验证”，不能只写最后一次调参结果。
- 同一问题经历多次错误修复时，应保留错误假设为什么不成立，以及最终采用的权威规则。
- 编辑器截图只能证明视觉现象；编译、NullRHI 自动化、实时 PIE/Chaos 各自只能证明对应层级，不能互相替代。
- 自动化结果必须来自当前工作树、当前二进制和唯一日志。使用固定时间步的 `-benchmark` 不能代替实时 PIE 稳定性证据。
- 每次新增条目后更新“待集成提炼”清单。集成工作树完成上收后，将条目标为“已上收”，不删除历史。
- 本文属于 M7 专属文档。不得在此修改共享契约、M3/M11 行为或集成工作树的构建默认值。

## 2. 已由总文档覆盖、本文不重复展开的问题

以下问题虽然也出现在本会话早期，但当前
[开发排错总文档](DevelopmentTroubleshooting.md) 已有完整条目，因此本文只保留索引：

- DAG2.3 编辑器预览存在、PIE 约 6 秒后因 `IdleValidation` 被事务删除。
- TaskGraph 仍走 Legacy `Algorithm=0`、M6 与 M7.3 同时冻结刚体、建筑合同可能把零栋误判为空关。
- B2/Furnace 的 Tripod 支撑质心、真实接触比例和固定时间步假绿灯。
- DAG 新增源文件改变 Unity 分桶后，匿名命名空间同名函数发生重定义。
- DAG 预算前置终止、真实 Contact DAG、联合支撑凸包、非地基板逐层物理连续性等 DAG1～2.3 问题。

后续集成时，应优先提炼本文第 3～8 节，避免把上述旧条目重复复制到总文档。

## 3. 多工作树、构建与引擎基线

| ID | 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- | --- |
| M7-WT-001 | 合并集成 `master` 后，M7 自身未改 M11，却在 ForceUnity 编译中出现 M11 `IsFiniteVector` 调用歧义 | Unity Build 会把原本位于不同 `.cpp` 的同名内部函数放入同一翻译单元；共享基线中的 M11 源码不满足 Unity 兼容性 | 不在 M7 越权修 M11；由 M11/集成工作树形成共享修复，M7 合并指定 `master` 后用 `-ForceUnity -DisableAdaptiveUnity` 重新全链接 | `git merge-base --is-ancestor <shared-sha> HEAD` 为真；ForceUnity 全链接成功；相关世界合同、M3 首周与 M7 门禁均在 fresh 进程通过 |
| M7-WT-002 | 打开 `.uproject` 提示 `AngryBirdsToSpace`、`ABTSRuntime` 缺失或由不同引擎版本构建 | 曾用 `C:\workspace\UnrealEngine-5.8.0-release` 源码版编译，而项目绑定 `C:\Program Files\Epic Games\UE_5.8` 安装版；两个 Editor BuildId 不同 | 删除“源码版也算 UE 5.8”的错误假设；本工作树固定使用安装版 `Build.bat` 全链接，不复制其他工作树 DLL，不使用 Hot Reload | 安装版 `-ForceUnity -DisableAdaptiveUnity` 全链接成功；fresh Editor 能加载两个模块；后续构建命令必须显式使用安装版绝对路径 |
| M7-WT-003 | 当前工作树未开启 Live Coding，构建仍可能被另一个工作树的 Editor 拦截 | UE 5.8 的 Live Coding 互斥检查按共享 `UnrealEditor.exe` 路径判断，范围大于单个项目 | 先读取活动 Editor 命令行确认归属；只有所有活动 Editor 都未加载当前 M7 工程/DLL 时，才允许对本次构建追加 `-NoHotReloadFromIDE`；不得结束其他工作树进程 | 构建交接记录是否使用该例外；M7 自己的 Editor 运行时禁止绕过，必须正常关闭 |
| M7-WT-004 | M7 实现 DAG4 时修改了共享 `AngryBirdsToSpaceGameDesign.md`，导致集成拒绝接收 | 把共享设计索引误当成 M7 阶段父文档；违反功能工作树所有权 | 恢复为集成 `master` 的精确 blob，单独提交 `M7: remove integration-owned design index change`；后续只链接/修改 `Docs/M7*.md`、`Docs/M73*.md` | 提交前执行所有权审计；交接中 `Shared files changed` 必须为 `none`；共享索引更新由集成工作树重放 |
| M7-WT-005 | 用工作树目录数字 ID 判断职责，或在文档/命令中硬编码托管路径 | Codex 托管 worktree ID 是不稳定实现细节，不代表 M3/M7/M11 身份 | 身份只取 `git rev-parse --show-toplevel`、`git branch --show-current`；运行时项目路径从 Git 根解析 | 每次会话启动记录 root/branch/status/worktree list；代码、配置和交接契约中不得写入 opaque ID |

## 4. DAG3、DAG4：开关、预算、预览与真实运行

| ID | 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- | --- |
| M7-DAG3-001 | 物理测试场三栋建筑选择了 Single/Dual/Seam，但编辑器看起来完全一样，PIE 又全部失败 | 只修改了 Pattern 枚举，`bEnableAnalysis` 与 `bEnableGeometryRewrite` 仍关闭；三栋实际都是同一个普通 DAG2.3 Arch。编辑器显示无碰撞 HISM，PIE 创建真实 Chaos 模块，二者不是同一验证层 | 验收夹具显式开启 DAG3-A/B，并在 Summary 中显示 `DAG3Enabled/DAG3BEnabled/DAG3BApplied/Hash`；将普通 Arch 动态稳定缺口与 DAG3 视觉缺口分开修 | 三模式必须有不同 Realized Hash 与 W/P/受影响体；PIE 三栋 `IdleValidation Accepted=1`；不能因编辑器预览存在就断言动态成功 |
| M7-DAG3-002 | 普通 Arch/DAG2.3 在 30 Hz Chaos 中整体侧滑，静态图与固定时间步测试却通过 | 默认 8/2 求解迭代不足以稳定求解约 33 个刚体的多层接触；这不是几何 COM 倾覆 | 对 M7.3 生成刚体应用可调 32/8 求解迭代；不放宽位移、旋转或速度门槛 | 真实 30 Hz Actor/Chaos 回归通过；几何 Hash 不变；固定时间步结果只作算法回归 |
| M7-DAG3-003 | 提高递归层数后报 `DAG3BNoAcceptedPattern:DAG3BBrickBudgetExceeded`，整栋消失 | DAG3-B 第二遍事务重求解后的真实 Brick 超过夹具 `MaxBrickCount`；fail-closed 不会回退普通建筑 | 保持认证夹具的深度/预算配对；实验深递归时同时提高抽象节点、估算 Brick、编译 Brick 和搜索预算 | 错误必须带实际数/上限；预算充分时同一候选可生成；预算不足时不发布半成品 |
| M7-DAG3-004 | DAG3-C 增加细分后，整体仍像同一栋上下两块建筑 | DAG3-C 认证攻击可达、Drop/Tip/Slide 净空和弱点绑定，不改变宏观轮廓；递归只增加 Macro 内部层级 | 将该现象定义为阶段边界，不用 DAG3-C 参数伪造视觉多样性；轮廓多样性交给 DAG5-B/Beam | DAG3-C 验收 `Playable=1`、攻击净空、弱点数和预算；六栋读形不作为本阶段门槛 |
| M7-DAG4-001 | PIE 中 W/P、Affected、方向箭头和标签留在建筑初始位置，像固定在屏幕上 | 诊断 HISM/TextRender 挂在生成器 Root，设置为 `HiddenInGame=false`，且运行时模块生成后重新创建；它们不是物理砖 | 所有弱点诊断组件设为 Editor-only/游戏中隐藏；PIE 不再创建或显示 | Game World 自动化检查诊断组件不可见；Editor 仍可查看；运行态坍塌不残留覆盖层 |
| M7-DAG4-002 | 三种弱点模式在结构与失效表现上差异很小 | DAG4 使用同一旧主体骨架，只替换内部 Failure Frontier；弱点多样性受上游骨架多样性上限约束 | DAG4 先以静态/Chaos 反事实闭环完成；建筑与弱点联合多样性明确延后到 DAG5/WFC/Beam 后重做 | 文档标记 DAG4 完成但多样性延期；新骨架进入生产前必须逐 Profile 重跑 DAG4 |

## 5. DAG5-A/B：候选搜索、预算与语义轮廓

| ID | 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- | --- |
| M7-DAG5-001 | 深递归以前也能生成，看不出 DAG5-A 新增了什么 | DAG5-A 不负责增加递归深度，而是把“一次随机推导”升级为确定性、有界、多候选完整可行性搜索 | 以逻辑 Seed 派生固定候选序列；每个候选通过预检、DAG2.3、已启用 DAG3、真实 Brick 预算后才发布 | 深递归扫描中 K=8 的成功率显著高于 K=1；同输入候选序列与 SearchHash 可复现；数学无解配置仍前置拒绝 |
| M7-DAG5-002 | 按深递归参数配置后，所有 Building Seed 都报 `DAG5ANoFeasibleCandidate:...DAG3GeneralizedCutFlowBudgetExceeded` | 实例仍开启 DAG3-A 广义小割、B 重写和 C 路由；深图让固定 `8192` 流操作预算耗尽。换 Seed 不能改变同一硬预算瓶颈 | 单独验收 DAG5-A 时关闭 DAG3 A/B/C；联合验收时按图规模提高 Flow/Candidate/Brick 预算，并明确这是另一组夹具 | 单独 DAG5-A 深递归成功；联合模式失败日志指出具体下游门；不得把 `MaxCandidateAttempts` 当成无限重抽 |
| M7-DAG5-003 | `DAG5B_ThroughOpeningWall` 固定报 `DAG5BEstimatedBrickBudgetExceeded` | `MaxEstimatedBrickCount=30`，而 8 Macro + 7 Support Edge 的保守估算为 36；有效上限取估算预算与真实预算较小值 | 提高估算预算到覆盖该轮廓的值，不需要提高已经足够的 `MaxBrickCount=100` | Details 与日志同时显示两个预算；预估 36 的夹具不再在 30 门槛必败 |
| M7-DAG5-004 | `OneSideHighTower` 接入 DAG3 后报 `DAG3CNoPlayablePattern`，误以为是“没有弱点/所有层三柱” | DAG3-A 有合法 Frontier，但 DAG3-B 改写后初始支撑裕量为负，完整建筑未受击就不稳定；C 只是包装上游失败 | 保留 B 的完整态静态门槛；不因想要弱点而接受负支撑裕量。后续在轮廓/支撑共同搜索中选可行候选 | 分阶段诊断 A 成功、B 明确 `InitialSupportMarginTooSmall`、C 不发布假弱点 |
| M7-DAG5-005 | DAG5-B v1 有 Shape/WFC、DAG 和 Brick，但与“轮廓→Seed DAG→Expansion→拟合”设想不契合 | v1 把固定 5～8 个 Macro 直接当最终 DAG，开启后绕过递归 Expander；没有细分 DAG 再拟合包络的中间层 | 保留 v1 为已实现参考；另写轮廓约束递归 DAG 演进设计，并用 DAG5-B v2 + Beam 路线逐步替代整板几何 | 文档明确“已实现、默认关闭/部分延期”，不得用 v1 结果宣称已完成轮廓约束 Expansion |
| M7-DAG5-006 | WFC 生成棱柱上再放棱锥，预览看似丰富但物理上只有线/点接触 | 旧垂直相容规则允许非平顶屋顶原语承载独立上层体量 | 棱柱、棱锥均改为终端屋顶；上方仍有体量时 Domain 强制 Box；需要复合屋顶时必须有平顶过渡层或合并为一个原语 | 多 Archetype/Seed 测试保证 Roof Primitive Terminal；编辑器不再出现尖顶承载独立体量 |
| M7-DAG5-007 | 提高 `GrammarDepth` 后主体轮廓变化不明显，变化集中在屋顶 | 初始 Archetype 已锁定大包络；Stack/Split 多在包络内切分，只有 Setback/Gap/Bridge 明显改变剪影；深度是上限而非每支必达 | 将轮廓复杂度指标从单一 Depth 改为量体数、退台、分支、非对称、桥/空洞等组合；后续由 Profile Resolver 强制里程碑 | 同 Seed 提深度时允许结构复杂度增加但不承诺剪影突变；Profile×Tier 必须用显式视觉里程碑验收 |

## 6. Beam-A：从脚手架中心线到堆放积木

| ID | 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- | --- |
| M7-BA-001 | 初版 Beam-A 像脚手架和方框，只有 XYZ 端点连接，没有 X 梁搭在 Y 梁上的积木感 | IR 把建筑建模为中心线端点 Joint 图，缺少有面积、带上下顺序的 Bearing Contact | Beam-A v2 改为固定截面、长度可变的 XYZ 长条；X/Y 分层交叉，柱落梁、梁落柱、同向叠放均显式记录 Bearing Contact；屋顶用水平层逐级收分 | `DiagonalMemberCount=0`、`BearingContactCount>0`；编辑器能读出上下堆放，而非同高线框交叉 |
| M7-BA-002 | 某些平行梁看起来沿一轴变粗 | 不是截面变粗，而是同一 course 或相邻 Bay 的两根同色 Member 零间隙面贴面，视觉合并成一根双宽梁 | course 内引入最大数量、最小净空和等距排布；空间不足自动降根数；跨 Bay 另做公共边界协调 | 每根 Member 截面仍等于统一参数；同层平行 Member 默认有可见净空；只有显式复合梁允许侧贴 |
| M7-BA-003 | `MaxParallelBlocksPerCourse=2` 后，两个相邻 Bay 仍看见“三根”，中央像一根加粗梁 | 左右 Bay 拓扑各有两根，但公共边界上的两根面贴面；单个 course 的 Gap 求解不知道邻居 | 不合并两个 Bay 的语义 course；公共边界两侧分别内缩半个已求得的实际 Gap，仍保留 2+2 根 | 专项夹具应有 4 个 Member；公共边界净空与组内实际净空相等 |
| M7-BA-004 | 第一版公共边界内缩后，中央净空与组内净空不一致 | 错把 `MinimumParallelBlockGapCM` 当成最终实际 Gap；当可用跨度产生更大等距余量时，边界仍只缩最小值 | 先由数量和跨度求 `ActualGapCM`，再让两侧各内缩 `ActualGapCM/2` | 左/右组内 Gap 与公共边界 Gap 数值一致；只对高度实际相交的相邻 Bay 生效 |
| M7-BA-005 | Z 柱没有跟随已偏移的水平梁，柱高恒定，且只支持固定 X-Y 角点模板 | Z 柱位置从原始 Bay Bounds 重新推导，未消费最终水平 Member 站位，也未覆盖 X-X/Y-Y 同向承托 | X-Y 连接直接消费已生成水平梁交点；对齐的 X-X/Y-Y 在重叠区复用平行梁数量/间隙求解；不对齐则不强行补柱；柱长由上下真实层面决定 | 公共边界偏移后柱梁仍对齐；不同层高产生不同柱长；无对齐站位时稳定拒绝而非斜接 |
| M7-BA-006 | 两根平行梁只因不满足普通最小净空就过早合并成一根 | “允许最多几根”的净空阈值与“两根已近到应合并”的阈值共用一个参数 | 保留 `MinimumParallelBlockGapCM` 管多根排布，新增更小的 `TwoBlockMergeGapCM` 专管 2→1 合并 | 净空位于两阈值之间时保留两根；仅低于 Merge Gap 时合并为居中单根 |
| M7-BA-007 | Beam-A v2 仍出现跨 Volume 悬空、横穿和大块重叠 | 每个 Volume/Bay 独立生成；去重只处理完全同端点 Member，未做全建筑装配 | 增加全局收口：同向合并/裁切、X/Y 错层、Z 柱在水平层切分、短缝补承托、悬空岛补最近承重层；无进展时裁剪孤立冗余 | `RemainingPenetrationCount=0`、`UnsupportedMemberCount=0`；正式 Brick 不得在下游重新排布已验收中心线 |

## 7. Beam-B：Motif、语义屋顶和桥接闭合

| ID | 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- | --- |
| M7-BB-001 | Beam-B 首版出现大量悬空和重叠，虽然 Port/WFC 验证通过 | B 只消费 Beam-A 的 Bay，清空已闭合 Members/BearingContacts；各 Motif 在原始 Bounds 内独立重建。Port 只表示语义兼容，不保证几何接触 | 将 Motif 输出编译回 Beam-A Joint/Member/Assembly IR，并复用同一全局装配收口；预览显示收口后的最终 Member | 无穿透、全 Member Ground 可达、非法短构件为 0；Port 通过不能单独作为结构通过 |
| M7-BB-002 | 蓝色斜撑与柱梁重叠，且可能依赖看不见的铰链/焊接稳定 | 把完整方截面长方体旋转后直接插入节点中心，既无斜切榫头也无可见托座；Chaos 接触会卡接或滑落 | `BracedBay` 保留兼容枚举但退出 WFC 生成域；Beam-B 当前只使用 XYZ 正交积木。未来斜撑必须有可见 BraceSeat、斜切几何和可断连接 | `DiagonalMemberCount=0`；任何重新启用斜撑的阶段需单独做静置、滑落和破坏认证 |
| M7-BB-003 | B 比 A 丢失 Shape Grammar 棱柱/棱锥语义，全部变成平顶矩形木垛 | B 只用 Primitive 限制 Motif 候选，却用 Bay AABB 生成等宽结构，并清空 A 的分层屋顶 | Box Bay 使用 Motif WFC；Prism/Pyramid Bay 复用 Beam-A 逐层收分屋顶，再统一进入 B 装配闭合 | `SemanticEnvelopeViolationCount=0`；棱柱沿短轴收分、棱锥双轴收分；Unsupported/Penetration 为 0 |
| M7-BB-004 | Shape Grammar 的高空 Bridge 被闭合器用超长落地柱“救活”，堵住门洞 | Bridge 只表达悬空体量，WFC 不证明双端承托；通用 GroundReachability 修复不知道门洞语义 | 禁止单边 Cantilever 进入生成域；新增 `SupportedSpan`，只在两端连接不同主体且下方完整 MustVoid 时保留；跨中禁止落地长柱 | BridgedArcology 下方门洞完整；桥体必须经两端指定模块承托，不能靠地面柱 |
| M7-BB-005 | 桥两端仍留可见缝隙或只部分纵梁落在桥托上 | Bridge Bay 与塔体都向各自 Bounds 内缩半截面；旧审计只要端点区域存在任意一条接触就把整端记为通过 | 主桥延伸至权威门洞边界；两端生成模块归属的 BridgeSeat/托块；逐根、逐端审计所有 BridgeRail | 每根纵梁左右端均有局部 Bearing；跨中无地柱；端点错位稳定 fail closed |
| M7-BB-006 | 只给一根悬空横梁补了 Z 柱，其余同层横梁仍悬空 | 支撑搜索每组只选一个 `BestUpper`，没有遍历全部独立上层横梁 | 逐根审计 BridgeRail/Seat 上方最近层全部水平 Member，为每根真正悬空梁在重叠区补局部 `BridgePost` | 默认夹具记录 `Targets=Supported`、`Violations=0`；已有直接承托的梁不重复补柱 |
| M7-BB-007 | 相邻 Bay 的横梁端部差半格未贴合 | Motif 的 `U0/U1` 从 Bounds 两侧各内缩半个截面；Port 相容与 Ground 可达都不要求端部完整覆盖 | 完整横梁延伸到 Bay 实体边界；邻接 Bay 在公共边界精确相接；只有局部短梁保留非连接端内缩 | 固定种子端部覆盖测试；不得以“有任意接触”替代完整接口覆盖 |

## 8. Beam-C、D0、D1/D1.5：荷载、真实接触、难度与屋顶

| ID | 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- | --- |
| M7-BC-001 | 屋顶最上层颜色比下层更“高负载” | 颜色表示 `max(跨度利用率, 悬臂比, 柱长细比)`，不是累计重量；单支点长梁的跨度平方项可高于下层多支点重梁 | 将 UI/文档命名为结构利用率；调试时同时看累计荷载、支点数和 EffectiveSpan | Ground 深灰；蓝/青/黄/橙/红按阈值利用率；不得只凭颜色认定弱点或真实应力 |
| M7-BC-002 | Beam-C Load DAG 通过，但最终 Brick 看起来不接触或意外旁路 | C 最初直接消费上游声明的 BearingContact，没有从最终 Brick AABB 重建真实上下接触 | Beam-C2 从最终 Brick 几何重建接触，比较声明/真实边，计算合力落点、接触覆盖和支撑展宽；闭合后重新跑接触与 Load DAG | `RealContactMismatchCount=0`、阻断型支撑违规为 0；声明图不再是最终物理权威 |
| M7-BC-003 | `SeamRelease Tier 0` 出现宽大上部结构只靠单根细柱，编辑器却判为承重通过 | 旧 C 只证明存在 Ground 可达路径并按面积分荷载，没有验证局部合力是否落在支撑面/凸包，也没有局部抗倾覆门槛 | 对每个承重层计算真实支撑区域与累计合力；不满足时确定性补 Z 柱，并保护门洞/桥洞 ReservedSupportVoids | 真实接触与支撑违规均为 0；`AddedStructuralSupportPostCount` 可大于 0；D1 只编译闭合后的最终 Member |
| M7-BC-004 | D1 建筑不受击也会在 Chaos 中倾倒；承重 DAG、真实接触和静态代理却全部通过 | “存在向下荷载路径”只证明竖向静力可传递；大量贯通楼层的细长自由 Z 柱在无隐式连接的摩擦模型中会因微小偏心先倾倒，再被高重心上部结构放大为连锁坍塌 | 在 Beam-B 与 C2 之间加入 C3 四柱闭合井干芯体：每 Belt 两 X + 两 Y 实体 course、四角 Z 柱分段、Beam-A 重闭合和统一 720cm 全部 Z 站位柱跨；Core Member 不作为弱点候选 | 固定 5 Profile × Tier 0/1 静态矩阵必须全部通过；最终防回归必须追加实时 Chaos 静置 PIE，NullRHI 不能替代 |
| M7-BC-005 | Tier 0 加芯体后超过 49 Brick；事后删屋顶平行梁会破坏屋顶读形，C2 还可能把缺失承托补成更多长柱 | 芯体被当成附加装饰而非原框架替换；在已闭合承重结构后偷删 donor 会改变真实接触与合力，C2 正确修复后又超预算 | D0 先减少重复普通 Bay；C3 在 Host 内优先以普通框架 Assembly 交换四柱芯体预算。保护桥、Void、主屋顶 Crown、檐口与屋脊；单/双 lane 屋顶不可删除，任何内部冗余 lane 回退都要重闭合并保持屋顶指纹 | Tier 0 最终 Brick ≤49 且仍有单主屋顶；四柱闭环、真实接触、屋顶 course/指纹和 C2 后最终预算同时通过，不能只看 C3 改写后的中间数量 |
| M7-BC-006 | 三柱 L 形“芯体”已有 X/Y course，静态日志却仍会出现 `MissingPostBearing`，也不能形成抗双向侧倾的闭合围合 | 只有一个 X/Y 交点的三柱拓扑不是闭环；某一角/边缺失时，重闭合可能保留 course 数却没有四角真实堆放接触 | Catalog v8 升级为矩形四站位；每 Belt 明确生成/复用 X0、X1、Y0、Y1，并逐 Host 校验四角 course-course 与 post-course Bearing。缺任一角稳定拒绝 | `StationPositions.Num()==4`；每 Belt 至少四条 CoreCourse、四角 Bearing 完整；`BeamC3CoreTopologyIncomplete` 夹具 fail closed；5×2 不得降级三柱 |
| M7-BC-007 | C3 预留了 `BeamC2MemberReserve`，但 C2 补柱后最终 Brick 仍可能超过 Tier 上限 | Reserve 只是先验估算；候选需要多少 C2 修复只能从实际接触得知。若只检查“新增柱数量”或 C3 中间数量，就没有约束最终 Assembly | 把 `MaximumFinalMemberCount` 传入 C2；初始、每次补柱、每次重闭合与成功返回均检查实际 `Members.Num()`，容量不足以 `BeamCFinalMemberBudgetExceeded` 失败；C3 最终再防御性复核 | Tier 0/1 分别不超过 49/199；构造剩余容量不足的修复夹具必须在 C2 内失败，而不是等 D1 少编译或事后截断 |
| M7-BC-008 | 旧 v7 日志显示低 Tier 与全量矩阵通过，于是容易误判 Catalog v8 四柱实现已完成 | Catalog 版本、拓扑语义和 Hash 已改变；旧三柱夹具不检查四角接触、全部 Z 站位、严格根系普通楼层网或 C2 最终硬预算，证据不可继承 | v8 重新固定 5 个 Profile Seed × Tier 0/1 正式门槛；记录 Catalog/Resolved/Core/RootedEvidence/Brick Hash，并在最终代码 fresh 编译后用唯一日志重跑；失败项保留明确原因 | 安装版 UE 5.8 的 `BeamC3-Full-ContactFaces-Final.log` 登记了当时的 `ABTS.M73DAG.BeamC3` 14/14 与低 Tier 10/10；`DropTrigger/Tier4/Seed669740` 以 2297 Brick、Rooted=219、最大柱跨 713.04 cm 通过。Column v9 静态收敛见 M7-BC-017，仍须完整 D1、D1.5、5×6 视觉回归与真实 Chaos PIE |
| M7-BC-009 | NullRHI 的四柱拓扑、接触与预算全部通过后，仍可能在实际摩擦、质量和求解器条件下静置倾倒 | 静态几何/Load DAG 不包含 Chaos 的接触迭代、摩擦滑移、微小偏心和连锁碰撞；自动化证据层级不同 | 把可见 PIE 作为后续独立门槛：5 Profile × Tier 0/1 无攻击跑完整 IdleValidation，再做一次受控普通底部支撑击打；记录身份 Hash、首动 Member、位移/转角与坍塌范围 | `Accepted=1` 且无静置连锁坍塌才证明 C3 动态基线；受击局部性与弱点差异仍交给 D2，不得靠隐式锁定或放宽 IdleValidation |
| M7-BC-010 | C3 初次通过、C2 收口后只多出一处 720 cm 柱跨违规；修复时要么找不到合适高度，要么为缺 1 根预算删掉整组框架，触发更多补柱和更长裸 Z 柱 | 旧修复没有继续已有 Host/Tie Plan；高度搜索跳过了违规区间内的可用接触面；C3/C2 两次调用各自看局部预算，且预算供体先删整组普通框架 | final-only 修复继续已认证 Plan，重建 Host 签名和既有 Tie 计数；只在违规区间内按单截面步长搜索；定向 Tie 必须锚到已有 Host 并有两端真实 Z Bearing；C2/C3 使用累计账本。低 Tier 优先安全屋顶内部 lane，Tier 1 净增额度按三 Member 拉结原子校准为 33 | Catalog v8 历史证据：`ColumnBreak/Tier1/Seed710000` 为 163 Brick、Host=3、Tie=3、Rooted=19，最大柱跨 1223→648.00 cm；普通横梁、脱离锚点和重复局部预算均有 fail-closed 自动化。Catalog v9 当前值为 133 Brick、Host=3、Tie=0、Rooted=10、MaxAllZ=612.29，Tie 形态不是固定合同 |
| M7-BC-011 | `TipOver/Tier1` 的 C3/C2/C3 认证和预算均通过，D1 却以 `BeamD1BrickPenetration` 淘汰候选；顶层只显示最后一次 `AllZSpanExceeded`，掩盖了更接近成功的候选 | post-C2 修复为缺失承接面补一个截面高的短 Z 柱时，没有检查该体积是否已被普通水平梁占用，因而形成完整 `36×36×36 cm` 穿透；候选循环只保留最后失败原因 | 在所有承接面补柱前按真实 Member AABB 检查非 Z 占用；被阻挡时回滚当前 Host/Belt 并改选高度或 Host，不放宽 D1 穿透门；为后续门禁增加按 Attempt 的 `CandidateRejected` 诊断 | `TipOver/Tier1/Seed730000` 在 Attempt 0 确定性通过，134 Brick、Rooted=16、最大全部 Z 柱跨 679.02 cm、`StrictPenetrationCount=0`；生产确定性专项通过 |
| M7-BC-012 | 把与芯体“连通”的普通楼层梁全部算作约束后，单个偶然接触便可能给整片塔楼截断 all-Z 柱跨；单锚悬臂也会成为假稳定证据 | 宽泛组件 BFS 可穿过普通 Z 柱、跨楼层和跨语义量体传播；组件只要某处含 X/Y 且碰到一个芯体，就无法证明每条普通 course 位于两个独立根锚之间 | 最终几何上重新派生严格根系网：根链必须由精确 Host course 经真实 Bearing 到同源 `CorePost`；普通网只沿水平—水平 Bearing、同一 `SourceVolumeId` 和 `Section*2.5+Tolerance` 楼层带传播；剪除无锚叶枝，并要求剩余骨干含 X/Y 且连接至少两个不同根锚。Disconnected course 不参与柱跨截断 | `RootedExistingFloorNetworkBracesPeripheralPost` 正例与 `SingleAnchorCantileverNetworkRejected` 反例均通过；Summary 输出 `StabilityRootedExistingCourseCount` 和按轴/中心/长度/Source 排序的 `StabilityRootedEvidenceHash`，完整 C3 为 14/14 |
| M7-BC-013 | C3 排除一个失败 Host 后再次选址时可能无限重试同一矩形，表现为候选搜索不结束 | `SelectCoreHost` 的 `FCoreHost& OutHost` 保留上轮的高 Score 和有效索引；剩余低分候选无法覆盖旧值，而调用方又把旧索引误判为本轮成功 | 每次进入 `SelectCoreHost` 立即以默认 `FCoreHost()` 清空输出，再在当前排除集上独立选优；不得把输出引用同时当作跨轮缓存 | 排除 Host 后要么选到新的签名，要么有界返回 `BeamC3NoClosedCoreHost`；完整 C3 14/14 在固定时限内结束，不再出现 stale OutHost 自旋 |
| M7-BC-014 | 稀疏夹具日志显示已经生成 65 个确定性备用 Belt 高度，实际却没有任何高度进入评分/尝试 | 评分数组在备用高度循环之前构造；随后加入 `CandidateMidZs` 的高度没有同步进入 `ScoredCandidateMidZs`，形成“候选计数存在、求值集合为空”的时序错误 | 先收集已有楼层高度与全部确定性备用高度，再统一计算可复用 course 数并排序；优先复用最多，其次按距目标高度与高度本身稳定排序 | `ExistingPlanRestoresMissingCourse`、`ExistingPlanRepairsPostC2HighZStation` 与完整 C3 14/14 通过；日志中的候选计数必须等于真正进入求值的集合大小 |
| M7-BC-015 | 高 Tier Column 在 C2 重闭合后仍可丢失 Belt 一侧的短 Z 承接面，随后出现 `MissingPostBearing`、`BeamAGlobalAssemblyRebuildFailed` 或剩余 all-Z 超限 | C2/Beam-A 会重新分割、合并计划 Z 链；宽连续区间仍存在并不保证 Belt 上下各有一个真实短 Z 面。补入一截面高残段后，后续全局重闭合又可能消费、重分段或与密集楼层梁冲突 | 保守处理为：按 Host 站位重新标记实际 Z 段、重建 Bearing，只为确实缺失的一侧补一截面短面，并在补入前检查非 Z AABB 占用；不能用“有连续区间”替代真实接触 | `BeamC3-ColumnHighTier-NoShortFaces.log` 保留当时 E5/E6 失败；当前实体闭合与 v9 高 Tier 静态收敛由 M7-BC-017 接续证明。真实 Chaos PIE 仍不得宣称完成 |
| M7-BC-016 | 高 Tier 定向拉结在局部候选中看似可用，最终认证却仍报告 all-Z 超限；或者同一语义量体被 Bay 切分后只能反复增设 Host | 旧链路把 `BayId` 同时当作装配归属和物理断开边界，且局部枚举、插入闭合与最终审计使用了不同的 Source/Bay 判定；精确 Host 四角之外已经严格根系化的 XY 楼层横隔也未被统一视为双向约束端点 | 冻结 source-aware Portal 合同：同 `SourceVolumeId` 可跨 Bay，不同 Source 继续 fail closed；局部与全局使用同一判定；严格 rooted XY floor diaphragm 可作为端点；Portal 必须是两端均有最终真实 Bearing 的水平 Brick。原始 Z Member 的 720 cm 硬门不放宽，必须以实体分段消除超长柱 | 增加同 Source 跨 Bay 正例、跨 Source 反例、rooted diaphragm 正例、单锚/装饰横隔反例、缺任一端 Bearing 反例和局部/全局一致性夹具；Tier 0/1 仍须满足 49/199 与净增 12/33。合同与自动化完成后仍必须人工执行实时 Chaos PIE，NullRHI 不得替代 |
| M7-BC-017 | `ColumnBreak / Tier 4` 的最终 Node 606 会反复返回同一失败 DAG；日志显示新增两根 Z 柱，但权威 Beam-A 闭合后 Member/Bearing/DAG 身份不变，继续重试会耗尽闭合账本，提前批量补 cap 又会给没有根系证据的节点制造假承托 | 两根 25/75 Z lane 会被现有分段柱 merge/split 吞并；“尝试 Added”不是实体 Bearing。旧循环既没有保存已证明的精确 lane 几何，也没有按失败 DAG 身份限制事务；随后审阅还发现 `BestVoid` 首次实际添加 grillage 时没有登记 token，因此同一 Hash 仍可能重复实体事务 | Catalog v9 的唯一配置参数变化是把 E5 `TargetBaySpanCM` 调为 473 cm，版本升级仍改变全局身份 Hash。Beam-C 先证明两条精确 25/75 根系 lane，证据只跨一次权威重闭合；只有下一轮相同几何才能在上梁下一截面生成平行 cap。普通 proposal 被归一化时不提前消费 token；`H1→H2→H1` 非相邻循环 fail closed。修复函数显式输出“本轮实际添加 `BridgeSeat + 2 BridgePost`”，`BestVoid` 与平行 cap 都在重闭合前登记失败 DAG，下一轮修改前即拒绝；短 course 检查真实 lane 间距；C3 屋顶 donor 失败则事务回滚并继续 frame donor | `BeamC3-ColumnHighTier-V9-PhysicalCommitFinal-20260805-1822.log`：E5/E6 均双次重放一致；E5 Attempt 5，1499 Brick、Rooted=159、MaxAllZ=712.19、6/33；E6 Attempt 5，2325 Brick、Rooted=220、MaxAllZ=667.03、1/15。`BeamC3-Full17-V9-PhysicalCommitFinal-20260805-1821.log` 为 17/17，`BeamC3-BeamC13-V9-PhysicalCommitFinal-20260805-1820.log` 为 13/13，`BeamC3-D0-6-V9-PhysicalCommitFinal-20260805-1825.log` 为 D0 6/6；完整 D1、D1.5、5×6 与真实 Chaos PIE 仍未完成 |
| M7-D0-001 | 调节 DifficultyTier 时相邻档视觉变化很小 | 旧 Tier 主要以 3% 尺寸和连续权重微调，且许多解题指标尚未由 D2 消费 | D1.5 同时使用强制语义里程碑与互不重叠 Brick 数量窗，有限候选必须同时满足二者 | 五 Profile × 六 Tier 的 Brick 数逐级落入独立窗口；E1～E6 每一档有明确量体/退台/屋顶/桥接差异 |
| M7-D1-001 | E1/E2 只有架子，E3 才突然出现完整楼体和屋顶 | D0 曾把 Tier 0/1 强制为 Box-only，Prism/Pyramid 权重为 0 | 改为“低分辨率完整建筑”：E1 一个主体+唯一主屋顶，E2 增加第二量体/侧翼且仍保留主屋顶；不靠额外语义量体抬 Brick 数 | 固定 Profile/Seed 逐档观察；E1/E2 已有主体—屋顶层次，E3 仍有清晰复杂度跃升 |
| M7-D1-002 | E1/E2 屋顶只有两三层，仍像方盒，和 E3 差距过大 | 低 Tier 屋顶 course 被 Brick 预算压得过少，短边高度没有转化为足够收分层数 | E1/E2 保留一个主屋顶并提高最低 course；预算紧张时优先降低主体密度/包络，不牺牲屋顶立体感 | 屋顶诊断输出 `RoofBricks`；低 Tier 屋顶有明确收分高度，且 Brick 总数仍落在本 Tier 窗口 |
| M7-D1-003 | `DropTrigger Tier 0 Seed 669740` 屋顶层之间够不到，闭合器在内部补长柱 | 每层梁长度直接取当前层收缩后的长宽；低 course 数导致相邻层偏移过大，上层梁没有跨到下层承托梁 | 上层屋顶梁沿自身长轴延伸到紧邻下层的完整宽度；棱锥逐层应用，棱柱按 X/Y 交替每两层应用 | 固定种子 `Roof Brick=8`、新增补救柱 0、接触/承重违规 0；屋顶层层直接 Bearing |
| M7-D1-004 | Shape Grammar 的 X/Y 密度不平衡，出现某轴极密、另一轴极疏的高塔和勉强贴合层 | Shape 递归方向权重有偏；Beam-A 按局部长轴切 Bay 并各自求站位；WFC 只有局部 Port 相容，不做全局二维柱网平衡 | 平衡 Shape 的 X/Y 切分与尺度，Beam-A 建立/复用双轴结构站位，D1 增加各向密度与接触覆盖门槛 | X/Y 单位长度站位密度比受控；Beam-C2 补柱比例不异常；不再出现近零覆盖的两层承托 |
| M7-D1-005 | 平衡 X/Y 后，一个大屋顶裂成四个小棱锥，失去整体立体感 | 平衡/切分产生多个相邻顶层终端；WFC 对每个叶终端独立分配屋顶原语 | 在 WFC 分配前聚合同标高、水平相邻且覆盖率足够的屋顶终端；聚合 Crown 的长宽比决定 Pyramid/Prism 权重 | 相邻屋顶恢复为大 Crown；不跨大空洞误合并；同输入聚合 Hash 确定 |
| M7-D1-006 | 棱柱屋脊方向和屋顶高度不能稳定表达原终端比例 | 旧原语选择与 terminal 长宽比弱相关；course 数是固定/低阶 Tier 值；屋脊可能被切成多个 Bay | 短边决定屋顶高度，近方形偏 Pyramid、长宽差大偏 Prism；Prism 屋脊沿长轴，顶层强制为一根长轴梁，下层均匀展开 | E1/E2 也使用同一几何规则；屋顶高度约随短边增长；Prism 最顶层只有一根连续长轴屋脊 |
| M7-D1-007 | 提高语义屋顶后，Beam-B 全局闭合循环反复抬升间隙，运行很久或触及轮次上限 | 某些 gap-lift 修复没有减少违规集合；闭合循环缺少 no-progress 判定 | 记录每轮违规签名；无进展时转入明确补支撑/裁剪或 fail closed，不继续重复同一变换 | 闭合轮数有界；固定低 Tier/高屋顶种子不再出现重复相同违规；最终 Penetration/Unsupported 为 0 |

| M7-D1-008 | `ABTSM73BeamD1PreviewActor` 开启 `Spawn Runtime Modules in PIE` 后建筑消失，既没有编辑器预览，也没有真实 Module | PreviewActor 是关卡放置 Actor，其 `BeginPlay` 早于 `AABTSM7GameMode` 动态创建 `AABTSM7BuildingMaterialSystem`；旧逻辑只搜索一次，找不到便静默返回，而 HISM 预览在游戏中默认隐藏 | 将一次性搜索改为有界定时重试：每 0.1 秒查找一次，最多 40 次；找到后生成全部真实 Module、清除定时器并输出成功日志，超时或编译拒绝输出明确诊断；`EndPlay` 清理定时器 | 自动化先在没有 MaterialSystem 时执行同一重试入口，再延迟创建系统并确认生成数等于 Brick 数；日志必须出现 `RuntimeModulesSpawned`，完整 `ABTS.M73DAG.BeamD1` 10/10 通过 |

| M7-D1-009 | 物理测试场点击弹弓后报 `Accepted=7 Rejected=2 Contract=0`，预览夹具阻塞发射 | `AABTSM73StableBuildingActor` 过去没有预览/运行时边界：所有关卡实例都会在 PIE 生成真实 Module；测试场又未开启生产 Building Contract，因此 M6 兼容路径扫描世界中全部 StableBuildingActor。一个深递归夹具有 76cm 内部穿透，另一个 DAG5B+DAG3 夹具无可行候选，二者共同拒绝 | 新增互相独立的 `Participate in PIE Runtime` 与 `Participate in Slingshot Validation Gate`。前者关闭时不生成 Module、不运行 Chaos，并标记 `NotRequired`；后者关闭时保留真实 PIE 物理和内部诊断，但对弹弓门禁公开为 `NotRequired` | `ABTS.M73A.StableBuildingParticipation` 验证两种开关组合；M73A 2/2、Beam-D1 10/10 通过。测试地图中的失败夹具应按验收目的选择“纯预览”或“物理但不阻塞”，不得依靠放宽全局门禁 |

## 9. 待集成工作树提炼

当前建议按以下顺序上收到 [DevelopmentTroubleshooting.md](DevelopmentTroubleshooting.md)：

1. `M7-WT-002`：安装版/源码版 UE BuildId 不一致导致模块缺失。
2. `M7-DAG3-001`、`M7-DAG4-001`：编辑器预览、PIE 真实 Actor 和诊断覆盖层生命周期。
3. `M7-DAG5-001`～`004`：候选搜索不是深度扩展，以及下游预算导致所有 Seed 同门失败。
4. `M7-BA-001`～`007`：Bearing Contact、公共边界实际 Gap、Z 柱站位和全局装配。
5. `M7-BB-001`～`007`：Port 假绿灯、斜撑延期、语义屋顶与桥端逐梁承托。
6. `M7-BC-002`、`003`：声明 Bearing 与最终 Brick 真实接触的权威切换。
7. `M7-D1-003`～`007`：屋顶逐层直接承接、聚合 Crown、长轴屋脊和闭合无进展保护。
8. `M7-BC-004`～`017`：静态 DAG 不等于 Chaos 自稳定、四柱闭环、低 Tier 普通框架替换、
   C2 最终硬预算、严格根系普通楼层网、Host/高度候选时序、短 Z 承接面，以及 v9 的
   17/17、5×2、Drop Tier 4、Source-aware 跨 Bay Portal、按失败 DAG 有界的实体 cap、Column 高 Tier，
   以及尚待执行的完整 D1/D1.5、5×6 与 PIE 证据层。

## 10. 相关文档

- M7 总导航：[M7BuildingDevelopmentRoadmap.md](M7BuildingDevelopmentRoadmap.md)
- 多工作树规范：[ABTSMultiWorktreeDevelopmentGuide.md](ABTSMultiWorktreeDevelopmentGuide.md)
- 共享排错总文档：[DevelopmentTroubleshooting.md](DevelopmentTroubleshooting.md)
- DAG 总路线：[M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)
- DAG5：[M73DAG5CandidateSearchSemanticEnvelopeAndProductionDesign.md](M73DAG5CandidateSearchSemanticEnvelopeAndProductionDesign.md)
- Beam-A：[M73BeamAStructuralIRPreviewDesign.md](M73BeamAStructuralIRPreviewDesign.md)
- Beam-B：[M73BeamBMotifWFCAndGraphGrammarDesign.md](M73BeamBMotifWFCAndGraphGrammarDesign.md)
- Beam-C / C2 / C3：[M73BeamCLoadDAGAndStaticProxyDesign.md](M73BeamCLoadDAGAndStaticProxyDesign.md) · [M73BeamC2RealContactAndLoadClosureDesign.md](M73BeamC2RealContactAndLoadClosureDesign.md) · [M73BeamC3CribCoreStabilityDesign.md](M73BeamC3CribCoreStabilityDesign.md)
- Beam-D0 / D1 / D1.5：[M73BeamD0GameplayProfileCatalogDesign.md](M73BeamD0GameplayProfileCatalogDesign.md) · [M73BeamD1RealBrickAndMaterialRolesDesign.md](M73BeamD1RealBrickAndMaterialRolesDesign.md) · [M73BeamD15VisualComplexityLadderDesign.md](M73BeamD15VisualComplexityLadderDesign.md)
