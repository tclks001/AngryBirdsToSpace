# M3 专属工作树排错记录

> 状态：持续更新
>
> 记录范围：自 2026-07-28 M3 功能工作树分离起，在 `feature/m3-pcg-map` 的开发、同步、自动化和 PIE/Standalone 验收中实际遇到的问题。
>
> 上游：[M3R PCG 地图生成改进方案](M3PCGMapImprovementPlan.md) · [项目排错总文档](DevelopmentTroubleshooting.md) · [多工作树协作规范](ABTSMultiWorktreeDevelopmentGuide.md)
>
> 详细设计：[M3R-5.1 卫星预览](M3R51SatellitePreviewDesign.md) · [M3R-5.2/M11 Preview 终局接缝](M3R52M11PreviewFinaleIntegrationDesign.md)

## 1. 文档职责与更新规则

本文件是 M3 功能工作树的增量排错账本，不替代项目排错总文档：

1. M3 工作树每次出现新的、可以复现的问题后，都应在本文件补充“现象—根因—修复—防回归验证”；不要等到集成时再依靠会话记忆恢复。
2. 功能工作树不直接修改 `DevelopmentTroubleshooting.md`。集成工作树合并 M3 时，从本文件筛选已经稳定、具有全项目复用价值的条目整理进总文档。
3. 只写已经实际遇到的问题或已经证明必要的诊断边界。尚未实现的功能、纯设计风险和普通开发进度不冒充故障。
4. 每条记录注明修复归属。若问题属于 M7、M11 或 Integration，M3 只记录如何识别、如何同步修复，不越权修改共享契约。
5. 验收证据必须来自本次修改后的 fresh 进程、唯一日志或可见 PIE；旧日志、Editor 已加载的 CDO 和单纯“编译了某个 `.cpp`”都不是闭环证据。
6. 新条目使用第 15 节模板。若旧结论被后续诊断推翻，应保留演进过程，并明确哪组数值已经作废。

## 2. 快速索引

| ID | 问题 | 当前状态 | 修复归属 |
| --- | --- | --- | --- |
| M3-WT-001 | Codex 管理工作树不在旧 `C:\workspace` 路径 | 已建立固定检查流程 | M3 |
| M3-WT-002 | 普通/Adaptive Unity 编译通过，强制 Unity 才暴露同名私有符号 | 已从 `master` 同步修复并建立构建门 | M11/Integration；M3 回归 |
| M3-WT-003 | 其他工作树 Editor 导致 Live Coding/DLL 锁，容易误杀进程 | 已建立进程归属检查 | 各工作树 |
| M3-WT-004 | 非 Unity 编译暴露稳定 Adapter 缺少日志类别声明 | 已由 `master 3991723` 修复并完成 M3 回归 | Integration；M3 回归 |
| M3-R3-001 | 冻结弹弓参数接入后，攻击走廊和建筑位置看似未变化 | 已修复数据布局；实体仍等 R-6 | M3 + Integration |
| M3-R3-002 | 同一弹弓阶段的建筑距离全部退化为同一个舒适射程 | 已改为逐关递增射程窗口 | M3 |
| M3-R5-001 | 逻辑 Target/Attack Corridor 已生成但画面无法辨认 | 已增加 F7 只读叠层 | M3 |
| M3-R5-002 | 候选预览正确，却被误认为生产世界已经移动 | 已明确 Preview/Test 权威边界 | M3 + Integration |
| M3-R5-003 | 同一 TerrainType 出现深浅块，且块边界与真实地貌分界错位 | 已取消地表 Beat/Theme 调色并改为固定基础色板 | M3 |
| M3-R5-004 | 小地图显示兼容世界颜色，而落点实拍显示当前候选颜色 | 已让材质与小地图共享持久的活动候选 VisualField | M3 |
| M3-R51-001 | 强化弹弓落入地表或两桩悬空/下沉 | 已改为两槽分别查询真实地表 | M3 |
| M3-R51-002 | 强化弹弓没有朝向卫星 | 已改为真实 Pouch 发射帧并加 `<=5°` 门 | M3 |
| M3-R51-003 | `SatelliteGravity=1` 但预览和真实飞行无可见偏转 | 已完成生产档位、真实地表、共享引力链闭环 | M3 + Integration/M6/M9 |
| M3-R51-004 | 自动生成的强化弹弓袋无法点击，其他弹弓正常 | 已使 Pouch 成为唯一 Visibility 点击目标 | M3 |
| M3-R52-001 | 太空槽仍出现在兼容 TaskGraph 旧位置，而非月度道路末端 | Preview/Test 接缝已验收，尚非月度正式布局 | M3 + Integration/M5.1/M11 |
| M3-R52-002 | 起伏地表上的太空槽对因两端高度不同被旧帧校验拒绝 | Preview/Test 校验已改为曲面局部帧 | Integration/M11 |
| M3-X-001 | 建筑日志显示已生成，随后建筑消失，容易误判为 M3 漏生成 | 已建立 M7 Idle Reject 分诊规则 | M7；M3 只分诊 |
| M3-T2B-001 | 风格语义若按名字、地图或位置识别，会随预览与生产身份漂移 | 已改为权威 Actor/组件/结果只读适配 | M3 |
| M3-T2B-002 | M10 落点预览的 SceneCapture 没有跨模块稳定只读入口 | 已记录共享类型接线需求；M3 不越界绕过 | Integration/M10 |
| M3-RIVER-001 | 主线阻断河沿不规则 Cell dual edge 高频蜿蜒，宽河 SDF 放大鼓包 | 代码/自动化/可见 PIE 已验收 | M3 |
| M3-TEST-001 | 100 Seed 性能门单次越线，但固定 Oracle 未变化 | 已建立隔离重跑和证据保留规则 | M3 |
| M3-JURY-004 | 合成 Fixture 绿灯未覆盖动态包络独占冲突和真实固定地图精确身份 | 已补专项失败注入、运行时身份门与 F7 诊断 | M3 |
| M3-JURY-005 | V2 Placement 已冻结，但生产地形/装饰未消费固定六栋空间 | 已接入 Terrain-only Pad、装饰避让与生产净空门 | M3 |
| M3-JURY-006 | 固定 180 cm 裙边让施工台像嵌在地坑里 | 已改为逐栋解析宽缓整地并增加连续性/Chaos 门 | M3 |
| M3-JURY-007 | V3 非方形建筑可能被 X/Y 装反或重复旋转，预发布结构门又依赖最终 LayoutHash | 已冻结路侧攻击轴/占地长轴正交门并按最终 Hash 顺序发布 | M3 |
| M3-JURY-008 | V3 候选合线后装饰累计门稳定出现 2 个 PhysicalBounds 重叠 | 已统一生成过滤与最终生产净空查询 | M3 |
| M3-JURY-009 | V3 建筑已激活但生产地形仍消费六个 V2 Pad，导致五栋主星建筑基部被地表穿过 | 已切换为五个 V3 主星 Pad，并让生产净空与装饰过滤消费同一 V3 快照 | M3 |
| M3-JURY-010 | E1 双座梁/外载冻结身份形成方形占地后，被统一的“Y 必须长于 X”测试误拒绝 | 已按 SurfaceKind 区分主星长轴与卫星方形占地，并保留公共朝向门 | M3 |
| M3-PERF-001 | 连续地表每个顶点重复从 Cell 0 搜索且三角形串行展开，吞噬真实地图启动预算 | P0+P1 候选性能与 V3 identity 已通过；最终联合证据待 M7 实体 Crystal 绑定修复后复证 | M3 |
| M3-HISM-001 | 树石用统一 Pivot 偏移生成，首次进入 Chaos 因地形/实例穿插弹飞 | 已改为生成期碰撞包络贴地、跨类型避让和确定性门禁 | M3；M6 只保留集成诊断 |

## 3. 工作树、同步与构建

### M3-WT-001：不能再假定项目位于 `C:\workspace`

**现象**

- 命令、日志或 Editor 启动参数仍指向旧的 `C:\workspace\AngryBirdsToSpace`，导致看到的是集成工作树或另一份 DLL；当前修改在画面中“没有生效”。
- 仅从目录名判断工作树身份，容易把功能分支操作成集成分支。

**根因**

Codex 内置工作树位于 `C:\Users\mingyangwu\.codex-official\worktrees\...`。工作树目录末段是动态标识，不是稳定路径；唯一可靠身份是 Git 顶层目录、当前分支和状态的组合。

**修复**

每次会话、同步或重型构建前先执行：

```powershell
git rev-parse --show-toplevel
git branch --show-current
git status --short
```

所有 `.uproject`、`-AbsLog`、自动化和 Standalone 参数都使用本次查询得到的绝对路径。M3 功能工作树只把更新后的 `master` 合入自身，不直接移动或推送 `master`。

**防回归验证**

- 当前分支明确为 `feature/m3-pcg-map`；
- 构建命令、运行进程命令行和日志路径都包含同一个工作树绝对路径；
- 合并前工作区无未知修改，并用 `git log --oneline HEAD..master` 确认待同步提交。

### M3-WT-002：普通编译没有暴露 Unity 同翻译单元冲突

**现象**

M3 工作树曾正常编译，但 M7/集成进行默认 Development Editor 全链接时出现：

```text
ABTSM11GravityAssistSolver.cpp(71,72): error C2668: IsFiniteVector 调用不明确
```

冲突来自 `ABTSM11GravityAssist::IsFiniteVector` 与 `ABTSM11FinaleSystem.cpp` 匿名命名空间内的同名函数。

**根因**

Adaptive Unity 的分桶会随源文件集合变化。两个内部辅助函数此前未被编进同一个 Unity 翻译单元，所以 M3 的一次普通编译是假绿灯；集成后的 Unity 分桶把它们放进同一 TU 才暴露未限定调用。

**修复**

实际符号修复由 M11/集成工作树完成并进入 `master`；M3 合并该提交。M3 新增 `.cpp` 私有辅助函数时也应使用阶段专属命名或显式命名空间限定，不能依赖“当前恰好不在同一 Unity TU”。

**防回归验证**

关闭属于当前工作树的 Editor 后，执行强制 Unity Development Editor 全链接：

```text
-ForceUnity -DisableAdaptiveUnity
```

必须看到最终链接成功；只看到对象文件编译完成不算通过。

### M3-WT-003：Live Coding/DLL 锁可能来自另一工作树

**现象**

当前 M3 Editor 已关闭，完整链接仍被 Live Coding 或 DLL 占用阻断；若只按进程名处理，会误关用户正在使用的集成、M7 或 M11 Editor。

**根因**

多个工作树共用同一 UE 安装和部分全局构建基础设施。进程名相同不能证明进程属于当前项目路径。

**修复**

先读取 Unreal/Live Coding 相关进程的完整命令行，确认其 `.uproject` 绝对路径和工作树归属。只结束本任务启动且明确属于当前 M3 工作树的进程；不得批量结束所有 Unreal 进程。确认当前项目未被加载后，构建可使用 `-NoHotReloadFromIDE`，但它不能替代进程归属调查。

**防回归验证**

- 记录占用进程 PID、命令行中的项目路径和处理结果；
- 重试同一完整链接命令；
- 最终交付说明是否仍有本任务启动的进程在运行。

### M3-WT-004：ForceUnity 通过不能掩盖稳定 Adapter 的显式 include 缺失

**现象**

V2 Diagnostics checkpoint 的 `-ForceUnity -DisableAdaptiveUnity` Development Editor 完整链接成功，但普通 Development Editor 构建在单独编译集成工作树拥有的 `ABTSM3WorldContractAdapter.cpp` 时失败：

```text
ABTSM3WorldContractAdapter.cpp(217): error C2065: “LogABTSRuntime”: 未声明的标识符
```

M3 本次修改的三个 `.cpp` 均已成功编译；失败文件不在本 checkpoint 的修改列表中。

**根因**

该稳定 Adapter 直接使用 `LogABTSRuntime`，但当前 `master` 基线没有显式包含声明该日志类别的头文件。Unity 构建恰好从同一翻译单元的其他源文件获得声明，因此 ForceUnity 绿灯不能证明非 Unity 编译自足。

**修复**

修复归属 Integration：原始集成工作树为稳定 Adapter 增加 `ABTSRuntime.h` 显式 include，并随 `3991723` 进入 `master`。M3 未直接修改共享 Adapter；合入更新后的 `master` 后重跑普通 Development Editor 全链接完成闭环。

**防回归验证**

- 普通 Development Editor 和 `-ForceUnity -DisableAdaptiveUnity` 已分别完成最终链接；
- 普通构建不得再依赖 Unity 翻译单元间的间接声明；
- 合入 `3991723` 后的普通构建为 8/8 actions、`Result: Succeeded`；`ABTS.Contracts.WorldGeneration` post-merge fresh 回归为 `2/2 Success`。

## 4. R-3 射程、建筑范围与攻击走廊

### M3-R3-001：冻结参数已接入，但 PIE 中建筑和走廊看似没变

**现象**

代码端已有冻结弹弓构造参数后，PIE 中实体建筑仍在旧位置，攻击走廊看起来也与旧 TaskGraph 相同。

**根因**

这里曾同时存在两个问题：

1. 早期 R-3 只把冻结最大射程当末端拒绝条件，没有把射程用于目标初选、道路到达点和 strict rebuild 后的最终弹弓位置联合求解；
2. 即使 R-3 逻辑区域已经修正，玩家世界中的 M7 实体建筑仍由兼容 TaskGraph 链生成。R-6/Integration 尚未把唯一月度 Candidate 发布给实体生成链，所以观察旧建筑 Actor 不能证明 R-3 未生效。

**修复**

- R-3 显式保存 `LaunchToTargetDistanceCM` 和 `AttackCorridorLengthCM`，并将两者纳入 Candidate Hash；
- 初选、侧路真实到达点和 strict rebuild 都必须满足同一冻结射程窗口；
- 长攻击走廊的全部 Cell 在道路重建前进入保留集合，避免重建时悄悄缩短；
- R-6 前只用 F7 逻辑叠层和 `[ABTS][PCG][EncounterReach]` 日志验收逻辑位置，不用兼容 M7 实体位置作反证。

**防回归验证**

固定展示 Seed 应输出六个非零、顺序正确的发射距离和走廊长度；当前基线为：

```text
LaunchToTargetCM=[986,1805,2631,3143,4697,6382]
AttackCorridorCM=[1907,2697,3594,4388,5926,8295]
```

同时确认 `MonthlyAccepted=0` 时没有宣称实体建筑已经迁移。

### M3-R3-002：不能把每关距离简单设为 `ComfortableReachCM`

**现象**

若同一弹弓阶段的 E1–E3 或 E4–E6 都直接使用同一个 `ComfortableReachCM`，三关距离几乎相同，难度和探索节奏消失。

**根因**

冻结标定给出的是弹弓档位的能力包络，不是每关唯一目标距离。把档位参数一对一映射为关卡距离，会丢掉同档位内的渐进关系。

**修复**

按关卡使用 `ComfortableReachCM` 的递增利用率窗口：

- Simple E1/E2/E3：`10–30% / 25–50% / 45–65%`；
- Reinforced E4/E5/E6：`20–40% / 35–55% / 50–70%`。

窗口可少量重叠以容纳离散球面拓扑，但同一档位的最终厘米距离必须严格递增；任何窗口上界也不能超过 `MaximumReachCM`。

**防回归验证**

- Validator 检查同档位 `LaunchToTargetDistanceCM` 严格递增；
- 100 Seed 接受门保持全通过；
- 修改冻结档位后，Candidate Hash 和厘米日志都随之变化，而不是只改蓝图显示值。

## 5. R-5 候选表现与诊断权威

### M3-R5-001：逻辑区域存在，但画面没有可验收证据

**现象**

自动化显示目标范围和攻击走廊已生成，PIE 中却无法区分这些逻辑区域，难以判断建筑是否离路、走廊是否足够长。

**根因**

Target Footprint、Attack Corridor 等是 Candidate 数据，不对应默认可见 Actor 或材质。仅看兼容世界的建筑和道路会把数据层与实体层混为一谈。

**修复**

Editor 构建增加 F7 只读叠层：红色显示 Target Footprint，橙色显示 Attack Corridor。可用 `-ABTSM3R5LogicRegions` 启动时打开；精确月度候选仍必须同时显式传入预览参数，叠层不得自行选择候选。

**防回归验证**

- F7 可重复开关，关闭后不残留组件或 Actor；
- 调试开关不进入 Candidate/Presentation Hash，不修改 `Input.ini`；
- 缺少精确 Candidate 时明确显示不可用，不回退到 `RetainedCandidates[0]`。

### M3-R5-002：Preview 看起来正确，不代表月度世界已发布

**现象**

F7 中道路、卫星、E5 或太空槽位置正确，但普通启动路径仍显示旧 TaskGraph 布局；容易被误报成“修改没有生效”或反过来误报成“月度布局已验收”。

**根因**

R-3/R-5 同时保留多个候选。显式预览只消费指定 Candidate，不执行 R-4/R-6 的最终唯一候选发布。表现正确与 Gameplay Authority 是两件事。

**修复**

- 所有预览/运行时桥保存 `SourceResultHash + CandidateHash`，按同一身份 Join Spatial、SlotField、Satellite、Finale 和表现数据；
- `RetainedCandidates[0]` 永远不能被当作默认已接受世界；
- Preview/Test 结果保持 `MonthlyAccepted=0`，默认生产路径不因 F7 或预览参数而改变；
- 日志和文档显式写出 `Authority=PreviewTest` 或兼容路径，禁止只写模糊的 `Ready=1`。

**防回归验证**

分别运行“无预览参数”和“显式 Candidate 预览”两条路径：前者保持兼容世界，后者必须打印指定 Candidate 与完整来源 Hash；两者都不得把未决候选发布为月度正式布局。

### M3-R5-003：同色深浅块与真实 TerrainType 分界错位

**现象**

月度表现预览开启后，同一片绿色、土黄色或灰色地貌中出现规则或不规则的深浅区块；这些色块边缘按 Cell/Beat 身份变化，与真正的 TerrainType 线段边界不重合。

**根因**

材质桥曾先在每个 Cell 中心调用带边界插值的 `GetDebugLandColor()`，再按 `VisualBeatId/AccentVariantId/ThemeVariantId` 乘以两级明暗系数，最后把结果写进 `CellVisualLUT`。材质 HLSL 随后仍使用 TerrainType 线段 SDF 选择和混合这些 Cell 颜色。于是 LUT 中同时烘入了“Cell 中心的边界混色”和“月度 Beat/Theme 调色”，而 GPU 分界几何只认识 TerrainType 线段；两套分区来源不同，必然出现同色深浅块及错位边缘。

**修复**

- 地表材质桥不再读取月度 `VisualBeatId`、`AccentVariantId` 或 `ThemeVariantId`，也不再做亮度乘法；
- `CellVisualLUT` 改为读取 Cell 的有效陆地 TerrainType 固定基础色，不在 Cell 中心预先采样或烘焙边界混色；
- TerrainType 异色分界继续使用既有线段 SDF，未修改材质资产；
- 月度 DTO、Visual Beat、ThemeVariant 与 Hash 保留，供 HISM 等非地表表现以后独立消费；运行时门由 `MaterialRhythm` 改为 `MaterialBasePalette`。

**防回归验证**

- `Saved/Logs/M3BasePalette-20260805-182627.log` 中 `ABTS.M3.Monthly.Biome.BaseTerrainPalette` fresh NullRHI 精确 `1/1 Success`，验证 Plain/Forest/Highland/Mountain 各自只有一套基础色，Cell 的高度和湿度标量不会改变同类色；
- fresh runtime 必须输出 `[ABTS][M3R5][MaterialBasePalette] Applied=1 ... VisualBeatConsumed=0 ThemeVariantConsumed=0`，且 Palette Cell 数等于预览 Cell 数；
- 可见 PIE 分别在 Lit/Unlit 检查：同一 TerrainType 内不得再出现深浅块，异色过渡必须贴合 TerrainType 线段 SDF；道路与河流的独立 SDF 不受影响。

### M3-R5-004：小地图采样到兼容世界而非当前候选

**现象**

显式候选预览中，落点远端实拍已经显示当前 Candidate 的地表颜色，但同一落点在小地图上仍显示另一套地貌颜色。取消地表 Beat/Theme 亮度后问题保持不变，说明它不是节拍调色残留。

**根因**

候选预览重建时，材质桥消费的是 `RebuildPlanet()` 栈内临时创建的候选 `PresentationCellStates/PresentationEdgeStates/PresentationVisualField`；`QueryScoutMapTerrainColor()` 却始终查询由兼容 `GeneratedCellStates/GeneratedEdgeStates` 初始化的成员 `TerrainVisualField`。落点红叉与远端实拍使用同一世界位置，投影公式没有错；分叉发生在地表表现权威选择处。基础色函数和 sRGB 转换也不是根因。

**修复**

- 将候选预览的 CellStates、EdgeStates 和 VisualField 一并持久保存在 `AABTSM3Planet`，避免 VisualField 持有 `RebuildPlanet()` 栈数组指针；
- 地表材质桥与 `QueryScoutMapTerrainColor()` 在 PreviewAuthority 生效时消费同一个持久候选 VisualField；关闭预览时两者仍共同消费兼容 `TerrainVisualField`；
- PreviewAuthority 已生效但候选 VisualField 不可用时 fail closed，不静默回退到兼容候选；
- `QuerySurface`、物理、TaskGraph 与稳定合同继续使用兼容世界，不把 Preview/Test 候选误晋升为正式生成结果。

**防回归验证**

- `Saved/Logs/M3ScoutMapPresentationAuthority-20260806-162927.log` 中 `ABTS.M3.Monthly.SatellitePreview.04ScoutMapPresentationAuthority` fresh NullRHI 精确 `1/1 Success`；Candidate 4 共发现 `6064` 个候选/兼容地貌不同的判别样本，其中 `4636` 个 Cell 中心采样明确命中候选基础色而非兼容基础色；
- `Saved/Logs/M3SatellitePreview-20260806-163013.log` 中完整 `ABTS.M3.Monthly.SatellitePreview` fresh NullRHI 精确 `4/4 Success`；
- `Saved/Logs/M3BasePalette-20260806-163059.log` 中固定基础色板 fresh NullRHI 精确 `1/1 Success`；
- 可见 PIE 待用户在同一显式 Candidate 下复查：红叉附近小地图地貌分类必须与落点实拍采用的地表分类一致；真实光照、SceneCapture 和 Toon 后处理造成的像素亮度差异不算分类错误。

## 6. R-5.1 卫星练习链路

### M3-R51-001：强化弹弓生成到地下

**现象**

自动生成的强化弹弓一根或两根桩落入地表，画面中袋口、弦和桩体与坡面明显错位。

**根因**

Candidate 的槽对中点和理想球面位置只适合离散规划。旧运行时把槽对中点当作共同地面，在起伏地表上没有分别解析两根桩的实际高度和法线，因此中点正确也不能保证任一桩底贴地。

**修复**

1. 用两个参考槽各自的 `CellId` 分别调用当前 Planet 的 `QuerySurface`；
2. 每根桩的可视底面落在自身 `WorldLocation`，桩轴使用自身真实地表法线；
3. 禁止把两地表点中点当共同地面；
4. 两根实际桩顶生成正式强化弦，`GetRestPouchTransform()` 成为唯一发射帧；
5. 卫星锚点从该真实 Pouch 帧重新求解并再次落到真实主星地表 Cell。

**防回归验证**

- 两根桩都能解析回各自原始 Cell；
- 两个桩底地表误差均 `<=1 cm`；
- Candidate 与运行时 Pouch 位置差进入日志和失败闭合门，不能只靠截图判断。

### M3-R51-002：强化弹弓没有面对卫星

**现象**

蓝色弹弓—卫星连线与强化弹弓的实际发射正前方夹角明显过大，无法按冻结手感发射。

**根因**

旧布局使用道路切线、局部坡面法线或固定桩距伪造发射帧；M6 实际发射方向来自两根真实桩和弦袋。坡面法线也不等于主星径向，把它用于冻结卫星弧环会进一步旋转空间关系。

**修复**

- 用真实桩顶、弦锚和 Pouch 生成 M6 Sling Frame；
- 卫星弧距以 Pouch 相对主星中心的真实径向为基准；
- 在冻结 30° 弧环内进行有界、确定性的地形补偿，而不是运行时任意搬动卫星；
- 将卫星视线投影到 Pouch 的 `Forward/Right` 平面，朝向误差超过 `5°` 时整套布局 fail closed；补偿角和最终 Transform 纳入 Hash。

**防回归验证**

F7 显示 `SAT FACING <角度> deg`。当前生产档位闭环基线朝向误差为 `0.007°`；早期的 `0.002°`、Cell `4218` 等数据来自理想球面旧链路，不再是验收基线。

### M3-R51-003：开启卫星重力后仍没有轨迹偏转

**现象**

在控制台执行 `abts.Calibration.SatelliteGravity 1` 后，轨迹预览和真实发射仍像没有卫星引力，强化弹弓甚至飞不到卫星线框附近。

**根因**

这不是一个布尔开关问题，而是多段数据链同时不一致：

1. Standalone/game 是独立进程，在 Editor 控制台设置的 CVar 不会自动传播过去；
2. 生产 M6 一度仍使用旧兼容强化速度 `2300 cm/s`，而冻结档位要求 `3300 cm/s`；
3. 候选端使用理想球面位置，运行时使用 TerrainVisualField/`QuerySurface`，Pouch 与卫星中心会相差数百厘米；
4. 候选端以道路切线和固定 `190 cm` 桩距伪造发射帧，运行时却从真实弦袋发射；
5. 如果预览积分、实际飞行和诊断各复制一份引力公式，即使开关值相同也无法证明使用了同一 M9 引力源。

**修复**

- 在实际运行游戏进程的控制台设置 CVar；`-1/0/1` 分别表示冻结默认/强制关闭/强制开启；
- M3 从活跃生产 `AABTSM6SlingshotSystem` 回读 Launch Profile Catalog，生产 Hash 与 Candidate Hash 不一致即 fail closed；
- 候选表面改用 `GetSurfaceRadiusAtDirection()` 读取 TerrainVisualField 真实半径，并与运行时 `QuerySurface()` 对齐；
- 候选和运行时都从真实强化桩、弦端点和 `GetRestPouchTransform()` 建立发射帧；
- 轨迹预演和实际飞行共同调用共享 M9 引力查询，不在 M3 复制引力公式；
- 运行时同时计算 gravity-on 命中和同输入 gravity-off miss，只有存在引力依赖成功集才认证通过。

**防回归验证**

fresh 专项和 Standalone 必须同时给出以下证据：

```text
ProductionProfile=1
TrajectoryCertified=1
GravityOnHits=14
GravityDependentHits=14
LargestSuccessIslandSamples=3
GravityOffMinimumMiss=2756.2 cm
```

当前生产档位 Hash 为 `C2B94139752AD846`，Candidate/运行时 Pouch 与卫星中心差均为 `0.00 cm`。F7 显示 `SAT TRAJECTORY PASS/FAIL`；仅看到 `GravityEnabled=1` 不能作为引力生效证据。

### M3-R51-004：自动生成的强化弹弓袋无法点击

**现象**

点击 M3 自动生成并标黄的强化弹弓袋没有日志，也不进入发射状态；同地图其他弹弓袋可以正常进入。

**根因**

这套练习弹弓是运行时完整装配的。强化桩网格与 Pouch 在鼠标视线中重叠，桩组件同样阻挡 `ECC_Visibility`，点击射线先命中桩并进入“给手持弦选择桩”的无效交互，真正 Pouch 没有得到点击。

**修复**

保留两根已装配强化桩的可见性和玩法碰撞，但让它们忽略 `ECC_Visibility`；运行时检查整根 Cord 恰好只有一个 Visibility 目标，并且该目标名为 `PouchVisual`。不满足该交互合同则练习布局 fail closed。

**防回归验证**

日志必须包含：

```text
[ABTS][M3R5.1][RuntimePractice][Interaction] Ready=1 ... CordVisibilityTargets=1 PouchVisibilityTargets=1
```

PIE 中点击袋口应立即进入强化弹弓发射状态；点击桩体不能抢占袋口射线。

## 7. R-5.2 道路末端太空槽

### M3-R52-001：太空槽仍生成在旧位置

**现象**

额外普通槽场和太空槽模型已经接入测试地图，但 PIE 中唯一太空槽对没有出现在月度道路末端。

**根因**

实体消费链仍读取兼容 TaskGraph 的 `FinaleLaunchFrame`/旧 Cell，而 M3R-5.2 只生成了候选端道路末端提案。没有显式 Candidate 和来源 Hash 的适配器时，M5.1、M11 和 M3 看到的是三套不同局部帧。

**修复**

- M3R-5.2 对每个候选保存精确 `RoadTerminalCellId`、末端候选窗、左右槽真实地表位置和 `ClearanceCellIds`；
- 普通槽场必须先 Join 同一 Candidate 的终局净空，再生成所有指定/道路附加槽，禁止“先生成普通槽，最后覆盖”；
- Integration 的 Preview/Test 适配器把同一帧交给 M5.1 太空槽和 M11 四行星布局，任何 Candidate、Hash 或坐标轴不一致都 fail closed，不回退旧 Cell；
- M3 不硬编码 M11 世界坐标，M11 只相对 `FrameOrigin + Forward/Right/Up` 解析预冻结局部布局。

**防回归验证**

- 太空槽恰好一对，普通槽不进入 `ClearanceCellIds`；
- M3、M5.1、M11 日志输出同一 `Authority=PreviewTest`、Candidate、Anchor Cell 和 Frame 身份；
- 可见 PIE 中太空槽位于道路末端真实地表，三颗助推行星和 UFO 使用同一局部帧；
- 当前接缝仍是 Preview/Test，未完成 R-6/R-7 前不得写成月度正式布局。

### M3-R52-002：真实地表槽对不应被“必须完全水平”拒绝

**现象**

左右太空槽分别贴合起伏地表后存在合理的径向高度差，旧 M11 局部帧校验把它判为不可用。

**根因**

旧校验隐含假设两个槽位处于同一欧氏水平面；球面和起伏 TerrainVisualField 不满足该假设。强行拉平会重新制造悬空或埋地。

**修复**

Preview/Test 帧以两槽真实位置的中点为 Origin，将槽对向量投影到局部切平面得到 Right，再与道路 Forward、主星径向 Up 正交化；允许受限地形倾角，但拒绝退化轴、反手系或过大倾斜。

**防回归验证**

- Frame Origin 与两槽中点一致；
- 左右槽各自贴地，不以共同高度覆盖；
- 当前 Preview/Test 倾角门 `<=45°`，并验证右手系、非退化轴及重复构建 Hash 一致。

## 8. 跨工作树分诊

### M3-X-001：建筑“生成后消失”不是 M3 漏生成

**现象**

建筑标签处没有实体，但日志先出现 `[Generated] ... Accepted=1`，随后出现：

```text
[IdleValidation] ... Accepted=0
[StartupPhysics] WorldReadyBlocked Reason=BuildingGateRejected
```

**根因**

M7.3 的生成期 `Accepted=1` 只表示结构方案和初始穿透门通过。其后的 Idle 稳定门拒绝会执行事务回滚，销毁模块和 Foundation，因此最终画面为空。这与 M3 是否选到了建筑 Cell 是不同阶段的问题。

**修复**

M3 分诊时先按 Actor/Task/Cell 关联完整日志：若存在 Spawn/Generated，随后是 `IdleValidation Accepted=0`，将问题交给 M7，M3 不通过移动建筑、放宽道路或保留失败 Actor 掩盖它。M7 的具体支撑几何、Idle 门和事务回滚修复记录以 [项目排错总文档](DevelopmentTroubleshooting.md) 为准。

**防回归验证**

月度联合验收不仅检查六个 Spawn，还必须等待建筑合同封口，并要求 `Expected=Registered=Accepted=6`、`Rejected=0` 后才发布 WorldReady。

## 9. T2-B 只读风格语义

### M3-T2B-001：按名字或位置猜测风格类别会破坏确定性

**现象**

同一主星在生产表现与月度 Preview/Test 表现中复用 Actor 和组件；卫星/E5 也会从候选结果进入真实运行时 Actor。若适配器按地图名、Actor 名称、位置或当前相机判断类别，切换证据层后会得到不同语义，并可能把未知对象静默归入已有类别。

**根因**

这些外观线索不是权威身份。主星地表、道路和水域实际共用 `ContinuousSurface`；树石由 `ForestHISM`、`RockHISM` 两个权威组件批次承载；月面练习卫星与背面 E5 由 `AABTSM3MonthlySatellitePracticeRuntime` 的精确 Actor 引用和 R-5.1 Candidate/Result Hash 连接。

**修复**

- 新增 M3 只读适配器，只接受精确权威 Actor、组件或已验证的 R-5.1 结果；
- `ContinuousSurface` 发布 `WorldSurface`，两个 HISM 批次发布 `BackgroundProp`；
- 练习卫星与背面 E5 的 Preview/Test 结果及生产 Actor 均发布 `SatelliteTarget`；
- HISM 每个组件只生成一个 `ComponentBatch` 绑定，实例数仅作为只读批次摘要，不形成逐实例注册；
- 未知 Authority、组件、Actor、枚举值或 Hash 不一致一律返回 `None` 并 fail closed；适配器不保存 Profile、Stencil 数字、Authority 或 Hash。

**防回归验证**

- `ABTS.M3.StylizedSemantics` 覆盖完整映射、重复查询确定性、未知对象 fail closed、HISM 批次数量，以及调用前后 Custom Depth/Stencil 状态不变；
- `ABTS.M3.Monthly.SatellitePreview` 同时验证 Preview/Test 结果与生产卫星/E5 Actor 可查询，并比较调用前后的 Preview/Runtime Hash、月度接受状态和 M9 引力开关；
- 提交前全文检查 M3 新增实现不得出现 Custom Depth/Stencil setter 或 raw stencil 解析调用。

### M3-T2B-002：共享落点 SceneCapture 没有稳定只读 getter

**现象**

Integration 需要把地面落点和月面落点分别接到 `GroundLandingPreview`、`SatelliteLandingPreview`，但当前 `AABTSM10ScoutMapSystem::LandingPreviewCamera` 与 `AABTSM101LandingPreviewCamera::SceneCapture` 都是共享 M10 类型的私有成员。M3 无法在所有权范围内提供稳定 owner/component 指针。

**根因**

T2-A 刻意没有接线 Scene Capture；现有 M10 API 只公开激活状态和 RenderTarget，没有公开捕获 owner/component。通过 `GetName()`、地图扫描、组件名或相机位置寻找捕获会重新引入隐式猜测，也会绕过共享类型所有权。

**修复**

M3 不修改 M10 相机、不应用 Profile、不添加后处理，也不扫描猜测组件。Integration 应在共享类型中增加两个 `const` 只读入口：

1. `AABTSM10ScoutMapSystem` 返回当前 `AABTSM101LandingPreviewCamera*`；
2. `AABTSM101LandingPreviewCamera` 返回其现有 `USceneCaptureComponent2D*`，并继续用现有 `GetPreviewSubject()` 显式区分 `PrimaryLanding` 与 `SatelliteLanding`。

Integration 接线必须在 owner/component 不存在、Subject 为 `None` 或类型未知时 fail closed；不得根据地图、名称、Transform 或当前主视图 Profile 回退。

**防回归验证**

- M3 功能提交的变更清单不得包含 `Source/ABTSRuntime/Public/Camera/**`、`Private/Camera/**` 或 `World/ABTSM10ScoutMapSystem*`；
- Integration 定向测试应验证两个 Preview Subject 映射到固定视图类，`None` 不接线，且接线前后 M3 Candidate/Result Hash、M9 引力、轨迹、碰撞均不变；
- SceneCapture 的 Profile、后处理与 Custom Depth 消费只由 Integration 验证，不能用 M3 NullRHI 语义测试替代像素门。

## 10. T3-A1 材质族适配

### M3-T3A1-001：用空材质测试“风格参数缺失”会制造 MID 假错误

**现象**

`ABTS.M3.StylizedMaterials` 最初虽为 `2/2 Success`，日志却在测试期间出现多条 `LogAutomationTest: Error: Condition failed`。最小复现是把完全无参数的 transient `UMaterial` 传给 TerrainMaterialBridge，试图模拟只缺少 `ABTS_*` 风格参数。

**根因**

该 fixture 同时缺少全部既有 `M3_*` Texture/Scalar/Vector 参数。TerrainMaterialBridge 按冻结契约继续注入原 LUT、道路、河流和半径参数时，MID 会对每个不存在的原参数触发引擎诊断；这与“原地形材质仍完整、仅 T3 风格参数缺失”的产品场景不同。仅看 Automation Result 会形成假绿灯。

**修复**

- 生产桥先只读检查八个公共风格参数；缺任一个时 `ApplyStylizedSurfaceParameters()` 返回 false，保留原地形 MID，不阻断生成；
- 自动化不再用破坏原 M3 参数契约的空材质冒充合法地形，改为验证未就绪桥安全拒绝，以及树石风格资产缺失时不发布非法绑定；
- 完整地形 fixture 继续验证全部原 `M3_*` 参数与 `ABTS_*` 参数由同一 MID 消费。

**防回归验证**

- fresh `ABTS.M3.StylizedMaterials` 必须精确 `2/2 Success`、项目 `LogABTSRuntime/LogAutomationController Error=0`；
- 不得用测试成功数掩盖测试期间的项目 Error；UE 初始化期自带的 `UnifiedErrorTest` 噪声需按时间和类别与项目测试区分；
- 风格缺失只允许影响表现，PlanetReady、TaskGraph、实例数、LayoutHash 和月度 ResultHash 必须保持。

### M3-T3A1-002：无 GUI 材质接线必须保留原表达式输出名

**现象**

地形 Custom 节点的 BaseColor 使用默认输出名，而树石 TextureSample 的 BaseColor 使用命名输出 `RGB`。首轮 headless 材质脚本将所有原节点都按空输出名处理，并在发现 `RGB` 时保守中止；未保存任何资产。

**根因**

Material Graph 的连接身份同时包含源表达式和输出名。只保存表达式指针、不保存 `GetMaterialPropertyInputNodeOutputName()` 会让复制后的树石接线丢失准确通道，可能静默改用错误输出。

**修复**

资产脚本先读取 BaseColor 的源节点与实际输出名，再把二者原样连接到 Tint/Lerp 分支；只在所有节点创建和编译成功后保存三个 M3 资产。原共享树石材质不写入。

**防回归验证**

- UE 5.8 只读反射确认地形和两项新材质的 BaseColor/Roughness/Specular/Metallic/Emissive 均有预期节点；
- 两项树石材质必须保持 `Used with Instanced Static Meshes=True`；
- 资产生成失败时不得保存半张材质图，必须核对 `git status --short` 后修正并从唯一基线重试。

## 11. 阻断河连续几何

### M3-RIVER-001：主线阻断河继承 Cell 级高频折角

**现象**

主线阻断河虽然语义上由一个理想球面大圆切面生成，画面却沿 350 条左右的 Voronoi dual 短边逐段左右摆动；DeepRiver 的粗线 SDF 把转角进一步放大成连续的“鼓包—收窄—鼓包”。M8 在任意河段架桥时忠实读取鼠标附近的单条可见河段，所以桥也跟随局部折角倾斜。

**根因**

Hydrology 只把“哪些 Cell 边跨越大圆切面”保存在 `FABTSM3CellEdgeState`，没有保留生成该割集的切面法线。`FABTSM3RiverVisualBuilder` 随后只能直接连接每条 Cell 边的两个 dual corner；闭合拓扑正确，但不规则 Cell 网格的离散误差成为可见中心线高频噪声。仅在材质端做模糊会让水面与 CPU SDF/M8 桥位再次分裂，不能作为最终修复。

**修复**

- Hydrology 为每条阻断河边记录同一个单位 `WaterBarrierPlaneNormal`，自然下游河段保持零向量；
- 河段构建器保留原 dual corner 的共享拓扑和 `SourceEdgeKey`，再把两端确定性投影到该大圆切面；
- Material LUT、CPU 地形/物理 SDF 与最新版 M8 单边语义桥位继续调用同一个 `BuildSegments()`，因此全量构建和单边查询得到同一条平滑线；
- 不改变 `bBlocksOnFoot`、Crossing、RequiredKey、WaterType、河宽、BridgeEdge、可达性或冻结 Compatibility Snapshot Hash。

**防回归验证**

- fresh `ABTS.M3.RiverVisual.BarrierGreatCircleSmoothing` 要求每条水边保留唯一 Source Edge 映射，每条阻断边的起终点都落在同一大圆，且全量/单边构建结果相同；
- `[ABTS][M3][RiverSDF]` 要求 `SmoothedBarrierSegments=BarrierDuals`、`BarrierSmoothingVersion=1`、`DroppedLocalRefs=0`；
- `ABTS.Contracts.WorldGeneration` 与 `ABTS.M110.TaskGraphFinaleSeparation` 必须保持通过，证明稳定导出和 M9/Finale 分离未变；
- 可见 `L_ABTS_M3` PIE 中，固定 Seed `312503` 的主线阻断河应呈连续低频大弧线，不再逐 Cell 左右摆动；桥面仍垂直跨河、两端落在不同河岸。NullRHI 不替代该视觉门。

2026-08-14 fresh 证据：河流平滑 `1/1`、M8 语义桥位 `1/1`、Week One 确定性 `1/1`、世界生成契约 `2/2`、M9/Finale 分离 `1/1`；`L_ABTS_M3` NullRHI 为 `Segments=436 / FlowCenterlines=86 / BarrierDuals=350 / SmoothedBarrierSegments=350 / DroppedLocalRefs=0`。2026-08-15 用户已完成固定 Seed `312503` 的可见 PIE 验收，确认主线阻断河呈连续低频弧线、不再高频蜿蜒，桥面跨河关系保持正确；本条状态晋升为已验收。

## 12. 自动化与性能证据

### M3-TEST-001：单次性能门越线不能被简单忽略或直接定性回归

**现象**

完整 M3 自动化曾出现 `58/59`：Biome 100 Seed 的 `P95=250.469 ms`，比 `250 ms` 门槛高 `0.469 ms`；同一筛选器独立 fresh 重跑为 `236.763 ms`，冻结 Oracle Hash 相同。

**根因**

毫秒级性能门会受同机其他 Editor、编译、杀毒和调度抖动影响；但“可能是抖动”也不能把首次失败改写为通过。必须先区分输出身份变化、算法退化和环境噪声。

**修复**

保留首次失败结果，停止并行重型任务，以相同二进制、相同筛选器、相同 Seed/Oracle 在 fresh 进程中隔离重跑；同时核对调用关系，确认本次修改没有进入失败测试的代码路径。若 Hash 或接受集合变化，按功能回归处理；若结果身份相同但多次仍越门，则按性能回归处理。

**防回归验证**

- 性能报告同时保存 P50/P95/Max、Accepted/Rejected、Oracle Hash 和运行命令；
- 不用完整测试中的第二次缓存运行替换 fresh 首次数据；
- 重型构建、慢速认证和可见 PIE 按多工作树规范串行执行。

## 13. JuryDemo 固定六建筑

### M3-JURY-001：PlacementReady 与 ChaosReady 必须解耦

**现象**

若把“冻结六条放置描述”误解为“六栋建筑已经通过 Chaos”，M3 会继续等待破坏和物理阶段；若只手写尺寸又不绑定几何 Hash，后续建筑变化会静默挤穿道路或水体。

**根因**

静态空间契约与动态物理证据属于不同层级。M3 只需要稳定的 Entry、Seed、Bounds、Pivot、方向和 Pad；最终联合发布才需要 M7 逐栋 ChaosReady。

**修复**

- 使用 M7 Stage 4.5 的六条几何派生描述及 Manifest/Catalog/Descriptor Hash；
- M3 固定 Seed 312503、Candidate 4，并以真实 Pad 确定性派生非水域、非最终道路的 `ReservedPadCellIds`；
- M3 不读取 AttackFace、Weakness 或 Chaos 状态，不搜索其他 Profile/Seed；
- 后续若 Chaos 修复改变静态几何、Pivot 或 Pad，必须提升 M7 Placement 版本并重新冻结 M3 Layout Hash。

**防回归验证**

- `ABTS.M3.Monthly.JuryFixedSix` 验证六条绑定、重复 Hash 和错误身份 fail closed；
- Stage 4.5 只能证明 PlacementReady，最终可见 PIE 仍需 Integration/M7 单独留下 ChaosReady 证据。

### M3-JURY-002：真实 M7 Pad 不应被旧 TargetNoRoad 子集误拒绝

**现象**

`ABTS.M3.Monthly.JuryFixedSix` 合成测试通过，但固定地图 `Seed=312503`、`Candidate=4` 的 fresh NullRHI 在 E2 报 `PadReservation:1`，`PlacementReady=0`。

**根因**

旧 R3 `TargetNoRoadCellIds` 是生成当时的目标保留子集，并未按 M7 Stage 4.5 的真实旋转 Pad 尺寸构造。E2 的 `3 × 3` Pad 样本会落入相邻 Cell；该 Cell 不属于旧子集并不等同于它命中最终道路或水域。合成夹具的 Pad 样本都回落到单个 Target Cell，因此未覆盖此差异。

**修复**

- Jury 固定 Candidate 层从真实旋转 Pad 样本派生排序去重的 `ReservedPadCellIds`；
- Pad 中心优先 Target Anchor；冲突时只在本 Encounter 已有 NoRoad/Footprint Cell 内按固定顺序解析最近可行中心；
- 每个保留 Cell 必须有效、非水域且不属于 `RecomputedRoute.OrderedRoadCellIds`，否则仍 fail closed；
- 保留 Cell 列表纳入 Placement/Layout Hash，但不改写旧 R3 结果、不重开全 Seed 门禁；
- Integration 后续消费 DTO 时负责让道路和装饰避开这组固定保留 Cell。

**防回归验证**

- 保留原失败日志 `M3Jury-FixedMap-20260815-115805-415-FreshRuntime.log` 作为诊断证据；
- `ABTS.M3.Monthly.JuryFixedSix` 注入最终道路 Cell 和水域 Cell，均必须以 `PadReservationFailed` 拒绝；
- 固定地图 fresh NullRHI 必须出现 `PlacementReady=1 Seed=312503 Candidate=4 Buildings=6 ReservedPadCells=<正数>`，且不得出现 `Placement rejected`。
- 最终证据 `M3Jury-FixedMap-Final-20260815-122339-605-FreshRuntime.log` 得到 `ReservedPadCells=52`、`LayoutHash=8AB8D7E4F094072D`；E3 从 Target Cell `2782` 解析到 Pad Center Cell `702`，E5 从 `3368` 解析到 `3367`，其余四栋保持 Target Anchor。

### M3-JURY-003：V2 动态效果包络必须独立于静态 Pad 预留

**现象**

M7 V2 保持六栋 PhysicalBounds 和静态 Pad 不变，但六栋 EffectBounds 均至少在一个水平轴超出 Pad。若 M3 只替换 Descriptor/Geometry Hash 而不消费 EffectBounds，设备运动空间可能穿过道路、水体或相邻建筑；若直接放大静态 Pad，又会改变 M7 已冻结的 36 cm 静态落脚边界。

**根因**

V2 将“静态建筑落脚空间”和“激活后设备/效果运动空间”明确拆成两个语义。`bDynamicEnvelopeRequired=true` 表示 M3/Integration 必须额外保留动态空间，但不授权修改 M7 的 PhysicalBounds、Pad 或 Chaos 状态。

**修复**

- M3 Fixture 切到 Contract V2 / Catalog `11501529584318250152`，逐栋更新 Descriptor、StaticGeometry、ProductionIdentity 与 DeviceAssembly Hash，并保存 PhysicalBounds、EffectBounds；
- 对静态 Pad 和动态 EffectBounds 分别执行旋转后的 `3 × 3` Cell 采样，动态结果保存为 `ReservedDynamicEnvelopeCellIds`，两组列表分别进入 Placement/Layout Hash；
- 动态样本命中最终道路或水域时以 `DynamicEnvelopeReservationFailed` 拒绝；建筑间距使用静态 Pad 与动态水平外接半径的较大值，过近时以 `DynamicEnvelopeSeparationFailed` 拒绝；
- 不修改集成工作树拥有的稳定合同和 Adapter。M3 发布最终 Hash 后，由 Integration 把当前 V1 Adapter 加法式切到 V2。

**防回归验证**

- Development Editor 与 `-ForceUnity -DisableAdaptiveUnity` 均完整链接；本次 Diagnostics 曾在非 Unity 重编稳定 Adapter 时暴露显式 include 缺失，已按 M3-WT-004 由 Integration 修复并从 `master` 回归；
- 前一版证据 `M3Jury-V2-FixedSix-Final-20260815-160925-970-FreshAutomation.log`：当时 `ABTS.M3.Monthly.JuryFixedSix` 精确 `2/2 Success`，逐栋校验全部 V2 Hash、36 cm Pad 边界、动态预留及 Hash tamper；当前门已由 M3-JURY-004 扩展为 `3/3`；
- `M3Jury-V2-ContractValidation-20260815-160449-888-FreshAutomation.log`：稳定合同 `Validation` 精确 `1/1 Success`；
- `M3Jury-V2-FinaleRegression-20260815-161028-955-FreshAutomation.log`：`ABTS.M110.TaskGraphFinaleSeparation` 精确 `1/1 Success`；
- 两次固定地图 fresh NullRHI 日志 `M3Jury-V2-FixedMap-20260815-160534-710-FreshRuntime.log` / `M3Jury-V2-FixedMap-Repeat-20260815-160617-483-FreshRuntime.log` 均得到 `Buildings=6`、`ReservedPadCells=52`、`ReservedDynamicEnvelopeCells=40`、`LayoutHash=7029074579FDC52E`，且无 `Placement rejected`；
- V2 使 E3 Pad Center 从 V1 的 Cell `702` 移到 `703`；这是动态包络避让后的确定性新身份，不得继续使用旧 V1 Layout Hash。

### M3-JURY-004：合成绿灯不能替代动态独占冲突与真实固定地图身份门

**现象**

Fixed-Six V2 初版自动化能证明六条 Fixture、Hash 和动态预留列表存在，但道路/水体注入落在 Pad Center，本质只覆盖 `PadReservationFailed`；真实地图的六个 Pad Center、逐栋 Placement Hash 和 `52/40` 预留数量只存在于人工检索的 fresh 日志，F7 也看不到 Physical/Effect Bounds。

**根因**

合成球的 Cell 间距远大于 Pad/EffectBounds，静态和动态样本通常回落到同一个 Target Cell。若不专门在动态角点方向增加独立 Cell，测试无法证明 `DynamicEnvelopeReservationFailed`；若运行时只检查非零 Layout Hash，也无法阻止真实 Candidate 的 Cell 解析或逐栋身份静默漂移。

**修复**

- 合成测试在 EffectBounds 角点方向加入不会被静态 Pad 采样命中的独立 Cell，分别注入最终道路与水体，精确要求 `DynamicEnvelopeReservationFailed`；
- 将 E2 中心移近 E1，精确要求 `DynamicEnvelopeSeparationFailed`；
- Placement Hash tamper 扩展到 PhysicalBounds、EffectBounds、ProductionIdentity 与 DeviceAssembly；
- `M3R5Smoke` 对真实 Seed `312503` 精确校验六个 Pad Center、六条 Placement Hash、逐栋静态/动态预留数及 `LayoutHash=7029074579FDC52E`；
- F7 叠层使用 Cyan 表示静态 Pad/Cell、Green 表示 PhysicalBounds、Magenta 表示 EffectBounds/动态 Cell，Red/White 分别标出 Target Anchor/最终 Pad Center。

**防回归验证**

- `M3Jury-V2-Diagnostics-20260815-163413-442-FreshAutomation.log`：`ABTS.M3.Monthly.JuryFixedSix` 精确 `3/3 Success`，第三项分别命中动态包络道路、水体与建筑间距三类拒绝；
- `M3Jury-V2-Diagnostics-FixedMap-20260815-163458-671-FreshRuntime.log`：fresh `L_ABTS_M3` R5 smoke 输出 `[ABTS][M3Jury][FixedMapRegression] Passed=1 ... Buildings=6 ReservedPadCells=52 ReservedDynamicEnvelopeCells=40 LayoutHash=7029074579FDC52E`；
- 同一 runtime 日志在启用 `-ABTSM3R5LogicRegions` 时输出 `JuryFixedSixPlacements=6 JuryFixedSixLayoutHash=7029074579FDC52E`；叠层是 Editor-only 诊断，不进入生产 Hash 或稳定合同。
- `M3Jury-V2-Diagnostics-Contract-20260815-163814-618-FreshAutomation.log`：`ABTS.Contracts.WorldGeneration.Validation` 精确 `1/1 Success`；
- `M3Jury-V2-Diagnostics-Finale-20260815-163850-384-FreshAutomation.log`：`ABTS.M110.TaskGraphFinaleSeparation` 精确 `1/1 Success`，候选尝试中的预期 Reject warning 不改变最终 Success 判定。
- 合入 `master 3991723` 后，普通 Adaptive Non-Unity Development Editor 完整链接为 8/8 actions、`Result: Succeeded`；
- `M3Jury-V2-Diagnostics-PostMerge-Contracts-20260815-164309-601-FreshAutomation.log`：`ABTS.Contracts.WorldGeneration` 精确 `2/2 Success`，包含 V2 Adapter 精确导出和原子失败注入；
- `M3Jury-V2-Diagnostics-PostMerge-FixedSix-20260815-164354-852-FreshAutomation.log`：新增 Diagnostics 门在 V2 Adapter 发布基线上仍精确 `3/3 Success`。

### M3-JURY-005：冻结放置已就绪不等于生产地形和装饰已消费

**现象**

固定地图已经稳定输出六条 V2 Placement，M7 也能按精确 WorldTransform 静态注册建筑，但 `AABTSM3Planet` 的生产连续表面仍只为兼容 TaskGraph 的 `BuildingSpawnSites` 建 Pad；固定六栋在日志中是 `Buildings=6`，而生产日志中的兼容 `BuildingSpawnSites` 仍是 `Buildings=4`。同时 Fixed-Six 的道路/水体预留只约束逻辑 Cell，森林和岩石 HISM 仍可能在建筑 PhysicalBounds 或设备 EffectBounds 中生成。若直接进入逐栋 Chaos 激活，建筑可能悬空、切入起伏地表，或让运动设备碰到 M3 装饰。

**根因**

`FABTSM3JuryFixedSixLayoutResult` 原先是放置/诊断结果，没有接入 `FABTSM3TerrainVisualField::SetBuildingPads`；`BuildDecorInstances` 只读取兼容 Building Pad 和 R5 `bDecorationProtected`，没有消费固定六栋的独立 EffectBounds。把六栋追加到公开 `BuildingSpawnSites` 会改变稳定兼容导出，把 EffectBounds 合并进静态 Pad 又会破坏 V2 的静态/动态语义分离，因此都不能作为修复。

**修复**

- M3 在第一次构建兼容 `BuildingSpawnSites` 后创建临时 Terrain Pad 快照，仅向生产与精确 Preview 的 TerrainVisualField 追加六条 Terrain-only Pad；公开 `BuildingSpawnSites` 随后按原流程重建，数量、顺序和稳定合同不变；
- 每条 Terrain-only Pad 精确消费冻结 Placement 的位置、三轴和 `RequiredPadHalfExtentCM`，以冻结位置半径定义切平面；固定 Jury 地图无条件应用这六个生产 Pad，不受旧 Blueprint 的可选兼容 Pad 开关影响；
- 装饰实例在静态 Pad 之外独立检查旋转后的 EffectBounds 水平包络，命中则跳过；不扩大静态 PhysicalBounds，不修改 Placement/Layout Hash；
- `ValidateJuryFixedSixProductionClearance` 对六个 Pad 各取 `3 × 3` 内部样本，检查生产表面对冻结切平面的最大残差，并扫描实际 Forest/Rock HISM 世界变换，要求 Physical/Effect 水平包络重叠数均为零；固定 Seed 下任一条件失败都会让 M3 Presentation fail closed；
- `M3R5Smoke` 新增 `[ABTS][M3Jury][ProductionClearanceRegression]` 门，使 Integration/M7 在逐栋 Chaos 前可用同一个 fresh 地图进程验证 M3 生产消费，而不是只检查布局 DTO。

**防回归验证**

- 普通 Adaptive Non-Unity Development Editor 与 `-ForceUnity -DisableAdaptiveUnity` 均完整链接，`Result: Succeeded`；
- `M3Jury-ProductionClearance-20260815-174312-722-FreshRuntime.log` 与 `M3Jury-ProductionClearance-Repeat-20260815-174430-939-FreshRuntime.log` 两次 fresh `L_ABTS_M3` R5 smoke 均输出 `TerrainPads=6 PhysicalDecorOverlaps=0 DynamicDecorOverlaps=0 DecorRejected=8 MaxPadResidualCM=0.001`；
- 原子注入与实例 Transform fail-closed 加固后的最终 fresh 证据 `M3Jury-ProductionClearance-Final-20260815-174848-153-FreshRuntime.log` 保持相同结果；
- 两次 runtime 的固定身份均保持 `Buildings=6 ReservedPadCells=52 ReservedDynamicEnvelopeCells=40 LayoutHash=7029074579FDC52E`，R5 RuntimeCertification 均为 `Terminal=1 Passed=1 Failed=0`；
- `M3Jury-ProductionClearance-Final-FixedSix-20260815-174933-754-FreshAutomation.log`：最终二进制上的 `ABTS.M3.Monthly.JuryFixedSix` 精确 `3/3 Success`；
- `M3Jury-ProductionClearance-Final-WorldContracts-20260815-174933-794-FreshAutomation.log`：最终二进制上的 `ABTS.Contracts.WorldGeneration` 精确 `2/2 Success`，证明公开兼容站点与 V2 Adapter 边界未漂移；
- 本门只证明 NullRHI 下的生产地形数值与 HISM 空间清空，不替代 M7 实时 Chaos、SceneCapture 像素或最终 E1→E6 可见 PIE。

## 14. 树石 HISM 生成期碰撞

### M3-HISM-001：固定 Pivot 与事后 Chaos 松弛不能证明树石生成合法

**现象**

- `BuildDecorInstances` 曾统一把树、石 Pivot 放在 `SurfaceRadius - 8 cm`，但树和石的简单碰撞底部相对 Pivot 约定不同；石头会直接进入连续地形，倾斜树的高处碰撞包络也可能切入坡面；
- Forest/Forest、Rock/Rock、Forest/Rock 之间没有生成期排斥。首次进入 Chaos 或发射范围批量提升动态代理时，重叠实例通过解穿冲量弹飞；
- 旧 Startup Physics 预热依赖 `BodyInstance` 就绪时序，只做事后 HISM 重叠扫描，既不证明单实例没有穿地，也可能在相同实例规模下得到不同候选数；
- 初版“只把所有碰撞样本向外抬到合法”虽然消除了穿透，但真实网格门显示 `MaxSeatCorrectionCM=345.03`，会把倾斜树整体抬离地面，属于物理绿灯、视觉假绿灯。

**根因**

生成器把渲染 Pivot 当作统一接地合同，并把运行时 Chaos 松弛当作生成合法化步骤。真实权威几何却是每个 `UStaticMesh::BodySetup->AggGeom` 的简单碰撞体；不同网格 Pivot、缩放、坡面旋转和跨类型邻居都会改变它的地表支撑与占用包络。物理场景扫描还受组件/BodyInstance 初始化时序影响，不能成为确定性生成来源。

**修复**

- M3 从每个树石网格的 `AggGeom` 构造碰撞描述；凸包采样全部顶点和下部 20% 三角面心，primitive fallback 使用简单碰撞包络，不再读取渲染包围盒或固定 `-8 cm`；
- 每个候选沿行星径向迭代贴地，所有碰撞支撑样本必须保持 `DecorGroundClearanceCM=2`；额外抬升不得超过“该资产 Pivot 到碰撞底部的固有补偿 + 25 cm”，否则换下一个候选，避免用悬空换取无穿透；
- 森林和岩石共享一个 3D Spatial Hash；窄阶段用简单碰撞包络 OBB 的 15 个分离轴检查，统一要求树—树、石—石、树—石至少保留 `4 cm` 分离；
- `(WorldSeed, CellId, Slot, Attempt, VisualVariantSeed)` 独立派生随机种子，每槽最多尝试 10 次。尝试耗尽即跳过，不强行发布非法实例，也不让前一槽重试次数扰动后续结果；
- 所有候选先进入 CPU 暂存并通过最终全局复核，再批量写入 HISM。任一最终地形/实例门失败时整批 fail closed；成功结果记录版本、Requested/Accepted、各类拒绝、最大贴地修正、最小净空/轴间距和量化 Transform Result Hash；
- 本修复只修改 M3 自有 Terrain/HISM 生成链。集成所有的 M6 预热仍可暂作诊断兼容层，但不再是生成合法性的来源，也不得改写 M3 的确定性结果。

**防回归验证**

- 普通 Adaptive Non-Unity Development Editor 与 `-ForceUnity -DisableAdaptiveUnity` 均完整链接，`Result: Succeeded`；
- `M3-DecorPlacement-20260815-FinalHash.log`：`ABTS.M3.DecorPlacement` 精确 `4/4 Success`，覆盖碰撞支撑贴地、跨树石空间哈希、103 Seed 独立 Attempt 身份和真实生产网格两次完整重建；
- 两次生产重建均得到 `AttemptsPerSlot=10 Requested=8990 AcceptedInstances=6646 RejectedGround=6479 RejectedPairOverlap=22853 MaxSeatCorrectionCM=71.57 MinimumGroundClearanceCM=1.99 MinimumPairAxisGapCM=4.10 ResultHash=-398834407951031673`；数量和 Hash 完全一致，Hash 已覆盖碰撞形状与关键放置参数，初版 `345.03 cm` 抬升与中间 Hash 已作废；`RebuildBudget` 分别为 `5192.327 ms`、`4999.561 ms`，均在 `8000 ms` 内；
- `M3-WorldGenerationContract-20260815-FinalHash.log`：最终二进制上的 `ABTS.Contracts.WorldGeneration` 精确 `2/2 Success`；
- `M3-TaskGraphFinaleSeparation-20260815-FinalHash.log`：最终二进制上的 `ABTS.M110.TaskGraphFinaleSeparation` 精确 `1/1 Success`；
- NullRHI 证明确定性和 CPU 几何合同，不替代可见 PIE/实时 Chaos。集成验收必须在 `L_ABTS_M3` 或 canonical 联合地图首次进入 Chaos/首次发射时确认树石不弹飞、不瞬移、不从地表钻出，并核对 Startup HISM 重叠候选趋近于零；若 M6 扫描仍报候选，按共享热点流程继续分诊，M3 不越权修改 M6。

### M3-JURY-006：固定 180 cm 裙边会把冻结施工面切成六个地坑

**现象**

- 六栋冻结建筑的内部施工面与 M7 Pivot 对齐，但施工面四周像直壁坑口，不是与周围地形连续的平地；
- 旧 `MaxPadResidualCM=0.001` 只证明内部九点落在冻结切平面上，不能证明裙边坡度、法线连续性、外缘回源或 Chaos 三角面一致；
- 六栋原地形相对冻结切平面的局部高差并不相同，固定 `EdgeBlendWidthCM=180` 无法同时处理这些高差。

**根因**

固定六栋的 `WorldLocationCM` 位于冻结基础半径，而生产地形继续叠加正的宏观高度。M3 曾把同一个 `180 cm` SmoothStep 过渡带套到所有 Terrain-only Pad；高差越大，过渡带就越接近竖直切坡。宽裙边还可能互相覆盖，若继续按数组顺序累计 Lerp，会让后一个裙边重复切削前一个承台。内部残差门与这种外部不连续完全正交，因此旧绿灯无法发现画面问题。

**修复**

- 冻结六栋的 `WorldLocationCM`、三轴、`RequiredPadHalfExtentCM`、内部切平面、Placement/Layout Hash 和公开 `BuildingSpawnSites` 均保持不变；不需要 M7 或 Integration 联合升版；
- M3 在安装 Pad 前从未整地 CellTopo 场采样每栋施工区边界及局部探针带，以 `JuryFixedSixMaximumGradeSlopeDegrees=18`、SmoothStep 峰值导数 `1.5` 和 `15%` 安全余量迭代解析逐栋裙边宽度；固定 `180 cm` 不再参与固定六栋生产整地；
- 普通 TaskGraph 施工台仍保留原有可挖可填的顺序合成。仅 `TaskId=INDEX_NONE` 的六条 Terrain-only Jury Pad 使用与数组顺序无关的向下包络，避免宽裙边重叠后重复切地；
- `ValidateJuryFixedSixProductionClearance` 新增逐栋解析峰值坡度、联合场法线步进、未被另一裙边占据的外缘回源、中心抗边界别名探针和裙边 Chaos 组件射线残差门。没有 PhysicsScene 的纯合同夹具跳过 Chaos 射线，但仍执行全部数值整地门；真实地图存在 PhysicsScene 时必须执行 Chaos 门；
- HISM 继续避让静态/动态包络。宽缓整地改变了部分候选的合法贴地结果，因此展示 Seed 的 `DecorRejected` 从旧证据 `8` 更新为 `72`；Physical/Effect 重叠仍必须为零。

**防回归验证**

- 普通 Adaptive Non-Unity Development Editor 与最终 `-ForceUnity -DisableAdaptiveUnity` 均完整链接，`Result: Succeeded`；因另一个工作树 Editor 正在运行且未加载当前工作树，构建按规范使用了 `-NoHotReloadFromIDE`，未结束其他进程；
- `M3Jury-AdaptiveGrade-Final-20260815-201507-056-FreshRuntime.log`：fresh `L_ABTS_M3` R5 smoke 得到逐栋宽度 `1068.2 / 2867.1 / 2581.2 / 3841.6 / 3274.2 / 3632.5 cm`，最大原始高差 `723.6 cm`，解析峰值坡度 `15.78° < 18°`，最大法线步进 `16.42°`，外缘残差 `0.000 cm`；
- `M3Jury-AdaptiveGrade-Final-Repeat-20260815-201937-258-FreshRuntime.log` 重复得到完全相同的宽度、整地/Chaos 指标、HISM 数量与 `QuerySurfaceHash=B35195C5629CB12A`；
- 同一日志的真实 PhysicsScene 门执行 `ChaosSamples=173 MaxChaosResidualCM=1.43`，六栋中心分别为 `5/5、5/5、5/5、5/5、5/5、4/5` 命中，且 `TerrainPads=6 PhysicalDecorOverlaps=0 DynamicDecorOverlaps=0 MaxPadResidualCM=0.001`；R5 终态为 `Terminal=1 Passed=1 Failed=0`；
- 固定身份保持 `Buildings=6 ReservedPadCells=52 ReservedDynamicEnvelopeCells=40 LayoutHash=7029074579FDC52E`，证明没有改动 M7 Pivot 或共享合同；
- 最终 ForceUnity 二进制上的 `M3Jury-AdaptiveGrade-Final-FixedSix-20260815-201623-319-FreshAutomation.log` 为 `3/3 Success`，`M3Jury-AdaptiveGrade-Final-Contracts-20260815-201703-201-FreshAutomation.log` 为 `2/2 Success`，`M3Jury-AdaptiveGrade-Final-Finale-20260815-201739-961-FreshAutomation.log` 为 `1/1 Success`；
- NullRHI 与组件级 Chaos 射线不替代可见地形观感。M7 逐栋 Chaos 激活后，联合可见 PIE 仍需检查六处均呈“内部平坦施工区 → 宽缓裙边 → 原地形”，没有坑壁、折线、碰撞台阶或建筑 Pivot 漂移。

### M3-JURY-007：MapFreezeV3 必须用路侧攻击轴和非方形占地长轴证明没有重复旋转

**现象**

- V3 建筑由 M7 已经执行一次 `Building local +Y → Site +X` 内容到站点旋转；若 M3 再按旧认知补一个 90°，共享 V3 结构门仍可能只因 Z 轴径向正确而放行，但建筑会以窄面朝路；
- 首版 MapFreezeV3 在六个 Site 和 PlacementHash 填完、`LayoutHash` 尚为零时提前调用 `IsStructurallyUsableV3()`，被正确拒绝为 `V3StructuralContract`。这不是空间候选失败，而是发布顺序违反“LayoutHash 非零”的预发布 DTO 约束。

**根因**

共享 V3 DTO 负责槽位、建筑身份、Bounds、支撑面、引力身份和径向 Z 轴等跨模块结构事实，但不推断玩家从道路/弹弓攻击走廊看到的横向轮廓，也不重复 M7 内容轴转换。M3 才拥有道路、Slingshot pocket、Attack Corridor 与地表切帧，因此“Site X 指向攻击来源、水平占地长轴与攻击走廊垂直”的证明必须由 M3 MapFreeze 门给出。另一方面，V3 结构门把非零 LayoutHash 视为完整快照的一部分，不能在 Hash 最终化之前调用。

**修复**

- 新增独立 `FABTSM3JuryMapFreezeV3Result`，固定 `Seed=312503 / Candidate=4 / [E2,E3,E4,E5,E1,E6]`；槽 0、1、2、3、5 在主星使用各自 Encounter 的 `AttackFaceDirection` 建立 Site X，槽 4 使用既有卫星背面预览帧建立 E1 Site X/Z 与真实卫星表面 Pivot；
- 五个主星站点分别用 M7 V3 的精确 `PadBounds`、`EffectBounds` 做旋转后 `3 × 3` Cell 采样，并以完整水平 Pad/Effect 包络加 `180 cm` 余量做逐对分离；卫星 E1 明确不占用第六个主星 Pad/Effect reservation。Surface、Support Center/Radius、Gravity Authority/Hash、PlacementHash 与 LayoutHash 全部进入冻结身份；
- 每栋从真实 `SiteLocalBounds` 推导水平长轴。当前六栋均为 Site Y 长于 Site X；门禁同时要求 `dot(SiteX, AttackCorridor) >= 0.9999` 与 `abs(dot(AttackCorridor, LongAxis)) <= 0.001`。因此 X/Y 装反、M3 再转 90°或只篡改发布 DTO 都 fail closed；
- 先完成六条 PlacementHash，再计算并写入 LayoutHash，最后调用共享 `IsStructurallyUsableV3()`；生产 V2 导出、V2 Terrain Pad、Integration Adapter 与稳定共享契约均不修改，日志明确保持 `ProductionContract=V2 ActivationAllowed=0`。

**防回归验证**

- 普通 Adaptive Non-Unity Development Editor 与 `-ForceUnity -DisableAdaptiveUnity` 均完整链接，`Result: Succeeded`；
- 最终二进制上的 `Saved/Logs/M3MapFreezeV3_FinalRunA.log` 与 `Saved/Logs/M3MapFreezeV3_FinalRunB.log` 两次独立 fresh NullRHI 均为 `2/2 Success / EXIT CODE: 0`，并给出完全一致的 `LayoutHash=3EB6326A2877EE1E`；槽位顺序精确为 `E2,E3,E4,E5,E1,E6`，主星 PadCenter 分别为 `6882/7218/2782/4367/1328`，卫星 E1 为 `-1`，六栋 `CorridorLongAxisAbsDot` 均为 `0.000000000`；
- `ABTS.M3.Jury.MapFreezeV3.01DeterminismAndRoadFacing` 验证五主星/一卫星、非方形 Y 长轴、Site X 路侧朝向、长轴正交、整结果重建和逐栋 PlacementHash 重复一致；
- `ABTS.M3.Jury.MapFreezeV3.02AxisSurfaceSlotBoundsFailureClosure` 对第二次 90° 旋转、错误 Surface、错误 E1 槽位和 Bounds 漂移逐项要求拒绝；
- `Saved/Logs/M3MapFreezeV3_LegacyV2Regression.log`：最终二进制上的现行 `ABTS.M3.Monthly.JuryFixedSix` 保持 `3/3 Success / EXIT CODE: 0`；
- `Saved/Logs/M3MapFreezeV3_WorldContractRegression.log`：共享 `ABTS.Contracts.WorldGeneration` 保持 `3/3 Success / EXIT CODE: 0`，包含 V2 M3 Adapter、V3 DTO 与通用 Validation；
- 以上 NullRHI 只证明 MapFreeze DTO、Cell reservation 和轴向合同，不替代 M7 逐栋实时 Chaos，也不替代最终联合可见 PIE。可见验收仍需从道路/弹弓侧确认看到的是建筑宽面，攻击走廊垂直穿向占地长轴，且月面只有 E1、主星只有 E2～E6。

### M3-JURY-008：候选合线绿灯不能掩盖装饰生成与最终 PhysicalBounds 口径不一致

**现象**

- Integration 将 M3 MapFreezeV3 精确提交 `a6a31362d0ce8b346a145440a2792f44e47f4a01` 合入候选 `52672002a74b57294603cd35f2cdecd9d2e13455` 后，ForceUnity、MapFreezeV3、FixedSix V2、WorldGeneration 与 FinaleSeparation 均通过，但 `ABTS.M3.DecorPlacement.04ProductionMeshDeterministicRebuild` 稳定输出 `PhysicalDecorOverlaps=2 / DynamicDecorOverlaps=0`，使累计门只能达到 `3/4`；
- 同一失败在原 M3 精确 SHA 上独立复现，因此不是 Git 文本冲突、候选合并顺序或 V3 六栋 Hash 被改写。它会阻断 V3 候选晋升，但不证明 MapFreezeV3 的位置、轴向或槽位身份本身错误。

**根因**

`BuildDecorInstances` 在候选落座前只用 Terrain Pad 和动态 `EffectBounds` 排除装饰；对于不要求动态包络的静态建筑，没有直接消费冻结 Placement 的 `PhysicalBounds`。最终 `ValidateJuryFixedSixProductionClearance` 却在 HISM 实例生成后同时检查全部 `PhysicalBounds` 与动态 `EffectBounds`。生成过滤和最终门使用了不同的空间语义与生命周期位置，因而两个确定性装饰落点能够通过生成、随后被累计门拒绝。

**修复**

- M3 新增单一 `GetJuryFixedSixDecorClearanceOverlaps` 查询，以冻结 Placement 的 Site X/Y 为基底，同时计算 `PhysicalBounds` 与按需启用的 `EffectBounds` 水平重叠；
- 候选装饰先完成真实网格碰撞贴地，再以 `CandidateTransform.GetLocation()` 查询最终落座位置；命中任一包络就消耗当前 Attempt、继续尝试同一 Slot 的下一个确定性候选，不发布非法实例；
- 最终生产净空门改为复用同一查询，并把 HISM 世界位置转换回 Planet Local 后检查，消除生成端和验证端的重复实现。修复只修改 M3 自有 Terrain/HISM 代码，不改变共享 V3 DTO、Integration Adapter、V2 生产导出、六栋 Placement/Layout Hash 或地图二进制资产。

**防回归验证**

- 同步 `master 1e4d1564eb885ab13149bc823c13b26c8f7fd67b` 后，UE 5.8 Development Editor `-ForceUnity -DisableAdaptiveUnity` 完整 `19/19`，`Result: Succeeded`；另一个集成工作树 Editor 仍在运行且未加载当前 M3 工程，按规范使用 `-NoHotReloadFromIDE`，未结束其他进程；
- `Saved/Logs/M3V3MergeFix-DecorPlacement-20260815-FreshAutomation.log`：`ABTS.M3.DecorPlacement` 精确 `4/4 Success / EXIT CODE: 0`；两次生产重建均为 `AcceptedInstances=6714 / RejectedProtected=2970 / ResultHash=-3242174576560399102`，且 `PhysicalDecorOverlaps=0 / DynamicDecorOverlaps=0 / DecorRejected=556`。旧失败的 `DecorRejected=553 / Physical=2` 已作废；新增 3 次拒绝来自补齐 PhysicalBounds 过滤后的确定性候选重试；
- `Saved/Logs/M3V3MergeFix-MapFreezeV3-20260815-FreshAutomation.log`：`ABTS.M3.Jury.MapFreezeV3` 精确 `2/2 Success`，仍为 `Mapping=E2,E3,E4,E5,E1,E6 / LayoutHash=3EB6326A2877EE1E / ProductionContract=V2 / ActivationAllowed=0`，六栋 PlacementHash 与 M3-JURY-007 完全一致；
- `Saved/Logs/M3V3MergeFix-FixedSixV2-20260815-FreshAutomation.log`：`ABTS.M3.Monthly.JuryFixedSix` 精确 `3/3 Success`；
- `Saved/Logs/M3V3MergeFix-WorldGeneration-20260815-FreshAutomation.log`：`ABTS.Contracts.WorldGeneration` 精确 `3/3 Success`；
- `Saved/Logs/M3V3MergeFix-FinaleSeparation-20260815-FreshAutomation.log`：`ABTS.M110.TaskGraphFinaleSeparation` 精确 `1/1 Success`；以上五组均来自修复后二进制、独立 fresh NullRHI 进程和唯一日志。最终候选仍需由 Integration 使用新精确 SHA 重建，并在 M7 逐栋实时 Chaos 完成后执行联合可见 PIE。

### M3-JURY-009：V3 生产建筑不能继续坐在 V2 地形施工台上

**现象**

- Integration 已发布 `ProductionContract=V3`，M7 六栋静态建筑也完成注册，但五栋主星建筑的柱脚/基部仍被连续地表穿过；截图可见绿色与沙色地形曲面直接进入建筑占地，而不是在建筑下形成统一水平施工面；
- 旧日志同时给出 `MapFreezeV3 Ready=1` 与 `ProductionClearance Passed=1 TerrainPads=6`，形成“建筑是 V3、地形绿灯仍是 V2”的假绿。

**根因**

`RebuildPlanet()` 在构建 `JuryMapFreezeV3Result` 之前就调用 `AppendJuryFixedSixTerrainPads()`，最终表面和净空门读取的是 `MonthlyJuryFixedSixLayoutResult`。V3 五个主星 Site 的基部都冻结在 `SupportRadiusCM=PlanetRadiusCM`，而旧六 Pad 的位置、建筑映射与水平包络不同；未被 V2 Pad 覆盖的起伏地形因此高于 V3 的 Site-local `Z=0` 平面。六栋 V3 描述的 `SiteLocalBounds.Min.Z=0`，排除了 M7 Pivot 或负基础深度作为根因。

**修复**

- 保留既有 V2 引导表面来生成冻结卫星预览和 MapFreezeV3，确保六栋 Placement/Layout Hash 不漂移；V3 快照完成后，生产表面原子替换为五个 `PrimaryPlanet` Site 派生的地形 Pad，月球 E1 明确不雕刻主星；
- 每个 Pad 直接使用 V3 `WorldTransform` 的 X/Y/Z、冻结 `PadHalfExtentCM` 和 `SupportRadiusCM`，不抬升或重算建筑 Transform；继续复用 M3-JURY-006 的逐栋解析宽缓整地；
- V3 生产表面移除已被五栋建筑替代的 Workshop/Target/Furnace 兼容 Pad，仅保留独立的 M11 `LaunchSite` 平面；装饰生成和最终净空门统一改读 V3 `SiteLocalBounds/EffectBounds`；
- 法线连续性门保持 `18°` 阈值，但从每条固定 32 段改为最大 `60 cm` 空间步长、32～128 段自适应采样，消除宽裙边因单步跨度过大产生的假失败。

**防回归验证**

- `Saved/Logs/M3V3TerrainPads-Final-20260816-012620-477-FreshAutomation.log`：ForceUnity 后 `ABTS.M3.Jury.MapFreezeV3` 精确 `2/2 Success / EXIT CODE: 0`；生产结果为 `Contract=V3 / PrimaryPads=5 / SatellitePads=0 / LayoutHash=3EB6326A2877EE1E`；
- 同一日志中五栋最大施工面残差 `0.001 cm`、最大解析坡度 `15.78°`、最大法线步进 `9.36°`、外缘回源残差 `0.000 cm`，且 Physical/Effect 装饰重叠均为零；
- 失败取证 `M3V3TerrainPads-20260816-010818-514-FreshAutomation.log` 与 `M3V3TerrainPads-R2-20260816-010957-698-FreshAutomation.log` 被保留：前者证明固定 32 段对 38 m 裙边给出 `19.50°` 假拒绝，后者证明盲目扩大裙边会恶化为 `25.88°`，不能靠加宽或放宽阈值掩盖；
- `Saved/Logs/M3V3TerrainPads-Final-20260816-013024-196-FreshRuntime.log`：有 PhysicsScene 的 fresh M3 R5 runtime `Terminal=1 / Passed=1 / Failed=0`，五栋 `ChaosSamples=145 / MaxChaosResidualCM=2.85`，并再次证明 `PrimaryTerrainPads=5 / SatelliteTerrainPads=0`；
- 命令行运行时仍不能替代视觉贴合证据。集成候选须由用户在 canonical `L_ABTS_M10` 可见 PIE 检查五栋基部无穿插、施工面平整且裙边连续。

### M3-JURY-010：方形卫星建筑不能套用主星非方形长轴断言

**现象**

集成候选接受 M7 E1 双座梁/外载冻结身份后，E1 `SiteLocalBounds=(-162,90,0)-(162,414,756)`，Site X/Y 尺寸精确同为 `324 cm`。Map Freeze 已达到 `Ready=1`，攻击走廊长轴点积为 `0`，但 `ABTS.M3.Jury.MapFreezeV3.01DeterminismAndRoadFacing` 仍用统一的 `BoundsSize.Y > BoundsSize.X` 拒绝合法 E1。

**根因**

M3-JURY-007 建立时六栋均为非方形占地，因此测试把“Site Y 是占地长轴”写成所有 SurfaceKind 的统一前置条件。E1 新冻结身份的 Site X/Y 为方形后，不再存在可由尺寸唯一识别的水平长轴；该形状变化不代表朝向丢失，因为同一测试随后仍独立验证攻击走廊对准 Site X，且与冻结 `HorizontalLongAxisWorld` 正交。

**修复**

- 只在 M3 自有 `ABTSM3JuryMapFreezeV3AutomationTests.cpp` 中按 `SurfaceKind` 收窄形状断言：`PrimaryPlanet` 继续要求 `Y > X`，卫星 E1 要求 `X == Y`；
- 六栋共同的 `AttackCorridorWorldDirection -> Site X` 与 `AttackCorridorLongAxisAbsDot` 门保持不变，因此没有放宽旋转、重复旋转或路侧攻击面的判定；
- 不修改 Map Freeze 算法、V3 DTO/共享合同、M7 冻结身份、布局 Hash 或地图资产。

**防回归验证**

- 当前 M3 工作树没有 Editor/Editor-Cmd 进程；使用唯一允许的 UE 5.8 执行普通 Development Editor 编译，`5/5 actions / Result: Succeeded`，未结束或干预其他进程；
- 按集成协调要求不在本修正中运行重型自动化；集成候选须在合入后重跑 `ABTS.M3.Jury.MapFreezeV3`，证明 E1 方形断言与其后的公共朝向断言同时通过；
- 若任一主星占地不再满足 `Y > X`、E1 的 X/Y 不再相等、走廊不再对准 Site X，或走廊与冻结长轴不再正交，测试仍须 fail closed。

### M3-JURY-011：卫星预览不能冻结在即将被替换的 V2 引导地表上

**现象**

- Integration 在 `L_ABTS_M10 + -ABTSM3R5Preview -ABTSM3R5PreviewCandidate=4` 的 D3D11 离屏生产路径中得到 `PreviewAuthority=1`，但 `AABTSM3MonthlySatellitePracticeRuntime` 每次生成都记录 `AnchorCell=855 / CandidateAnchorCell=855 / DeltaFromPreview=1467.80`，随后以 `SatellitePreviewRuntimeDivergence` 失败关闭；M7 E1 Crystal 因而没有可绑定的 Ready runtime；
- 失败基线 `MapFreezeV3 LayoutHash=5485D3F22956AE41`、E1 `PlacementHash=6A303ACBBA0358DB`、所选 Satellite candidate `3E024489860385BF`、Satellite result `B0FAE33A97832A1B`。锚点 Cell 完全一致，排除了方向求解、格点拓扑或 M7 绑定状态机漂移。

**根因**

`RebuildPlanet()` 先把兼容 TaskGraph Pad 与旧 Fixed-Six V2 六 Pad 安装到 `TerrainVisualField`，随后立即通过 `GetSurfaceRadiusAtDirection/GetSurfaceNormalAtDirection` 冻结 SatellitePreview；MapFreezeV3 完成后才把生产表面原子替换为最终五个主星 V3 Pad。生产 runtime 的 `ResolveFacingAlignedSatellitePlacement` 正确调用最终 `QuerySurface()`，因此同一 Anchor Cell 的世界表面位置/法线与过早冻结的 snapshot 相差 `1467.80 cm`。错误侧是临时 V2 表面上的 preview snapshot，不是最终 V3 production surface，也不是运行时 `250 cm` fail-closed 门。

**修复**

- 保留一次不发布日志的 bootstrap SatellitePreview，仅用于得到首轮 MapFreezeV3 和五个主星 Pad；安装最终 V3 地表后，用同一生产 `QuerySurface` 权威重新构造 SatellitePreview，再重建 V3 E1；
- 以最终 MapFreezeV3 再安装一次五 Pad，并立即执行 SatellitePreview whole-struct 与 MapFreezeV3 canonical validation。任何 Pad 安装、最终 preview、最终 E1 或复验失败均清除 Ready 结果，不回退旧 snapshot，不放宽 `MaximumSatellitePreviewDeltaCM=250`；
- 解析门直接用最终 `Planet->QuerySurface(Candidate.SatelliteAnchorDirection)` 重构卫星中心，要求 Cell 与冻结 Cell 相同、中心误差 `<=1 cm`；MapFreeze 门另要求 E1 `SupportCenterWorldCM` 精确连接所选最终表面 candidate。共享稳定契约、manifest、共同地图和 M7 代码均不修改；正式共享 refreeze 仍由 Integration 发布；
- 最终地表中心不能沿用 bootstrap 轨迹证书。pre-binding proxy 与 production Crystal 都在最终中心上使用既有确定性域的 `61 x 31` 采样重新认证；冻结 Pull/Aim 范围、三连通见证、重力关闭 miss、唯一岛和误命中门槛均保持不变。没有合格候选时仍 fail closed；
- Hash 影响被限定为卫星链：所选 Satellite candidate/result、E1 PlacementHash 和总 LayoutHash 必须重冻结；五个主星 E2/E3/E4/E5/E6 的 PlacementHash、PadCenter、Surface、走廊轴和 M7 Catalog 必须逐项保持。

**Timing 与安全异步边界审计**

- 当前真实地图已有 `ABTSM3TaskGraphGenerator` 每个 Attempt 的 Mission/Spatial/BuildingPadReserve/Height/Hydrology/Roads/BuildingPadCertify/Validate 累计 `TimeMS`（Verbose）、月度表现 Planner 的 `PlannerBudget DurationMS`、整次 Planet 的 `RebuildBudget DurationMS`，以及 RuntimeCertification 的 `PlannerMS/RebuildMS/ElapsedSeconds`；这些足以先定位规划或整图重建占比，但尚未把连续表面、材质、HISM、两阶段 MapFreeze 各自拆段；
- 后续 `<=30 s` 冒烟优化可安全异步化的边界仅限不可变输入上的纯 CPU 候选工作：不同月度 retained candidate 的 preview/证书候选构造、连续表面唯一顶点的高度/法线采样缓存、树石候选几何与空间哈希预计算。归并时必须按稳定 CandidateId/顶点索引/Slot 顺序提交，保持首个接受候选和 Hash 不变；
- `bootstrap preview -> 首轮 V3 -> 五 Pad -> final preview -> final V3 -> canonical validation` 是有向依赖链，不可互相并行；所有 UObject/Actor、PMC/碰撞烹饪、MID、HISM 写入和 Chaos 状态创建必须留在 Game Thread。启动 UI 不在本条修改范围。

**防回归验证**

- UE 5.8 普通 Development Editor 编译通过：全量 `27/27 actions / Result: Succeeded`；最终源码调整后的增量编译同样通过。期间未启动或结束其他工作树的 Editor；
- fresh NullRHI `ABTS.M3.Monthly.SatellitePreview`：`Saved/Logs/M3Jury011-SatellitePreview-20260816-033304-FreshAutomation.log`，精确 `4/4 Success`；fresh NullRHI `ABTS.M3.Jury.MapFreezeV3`：`Saved/Logs/M3Jury011-MapFreezeV3-20260816-033304-FreshAutomation.log`，精确 `2/2 Success`；
- 最终冻结身份：Satellite selected candidate `AA569671E58184A1`、Satellite result `3358BC5A456E3CD4`、E1 PlacementHash `1C267DFD88E65BAB`、LayoutHash `44723367D3DAA3A4`、Catalog `21B519761049404B`。五个主星逐项未变：E2 `A91A9FB5D79AE1CE`、E3 `4C41612002CC0208`、E4 `8ACA9CA9BAFE95BD`、E5 `66C8FD0EF4ACD5F2`、E6 `73BC7FE74D3835F7`；
- fresh D3D11 离屏生产路径：`Saved/Logs/M3Jury011-L_ABTS_M10-OffscreenD3D11-20260816-033609.log`。唯一发布的最终 `[SatellitePreview] Result=3358BC5A456E3CD4`，随后 `MapFreezeV3 Ready=1 ... LayoutHash=44723367D3DAA3A4 ... SurfaceAuthority=FinalV3`；runtime 记录 `AnchorCell=754 / CandidateAnchorCell=754 / DeltaFromPreview=0.00`，轨迹 `PracticePassed=1 / FullFrozenCarrierPassed=1 / GravityDependentHits=144 / Island=51 / AimNeighbors=1 / PullNeighbors=1`，最终 `Ready=1 / TrajectoryCertified=1`，进程自行退出；日志不存在 `SatellitePreviewRuntimeDivergence`、`SpawnRejected` 或 `CertificationRejected`；
- 本 M3 工作树的共享 exact V3 seal 仍是集成前旧值，因此 M7 E1 Crystal 正式绑定须等待 Integration 把共享 seal 重冻结为 `44723367D3DAA3A4` 后再跑联合门禁；这里没有回写共享契约，也没有把旧 bootstrap 坐标带回生产快照。

### M3-PERF-001：连续地表重复 Cell 搜索和串行展开占用启动关键路径

**现象**

- final-surface D3D11 基线 `M3Jury011-L_ABTS_M10-OffscreenD3D11-20260816-033609.log` 中，整次 M3 Rebuild 为 `4930.347 ms`，连续地表阶段约 `2948 ms`，是 M3 最大单段瓶颈；
- `SurfaceSubdivision=7` 时共有 `163842` 个唯一顶点和 `983040` 个 PMC 输出顶点。旧实现对每个唯一顶点分别查询 Cell、半径、法线和颜色；其中法线的八个偏移采样都重新从 Cell 0 开始最近 Cell 游走，随后三角形展开仍完全串行。

**根因**

连续地表没有把中央 Cell 当作同一顶点各派生查询的只读权威：中心 Cell 被半径、颜色重复搜索，八个近邻法线采样也丢弃中央 Cell Hint。顶点之间和三角形之间是不可变输入上的纯 CPU 独立工作，却未使用固定输出索引并行；相反，真正必须留在 Game Thread 的 PMC、碰撞、材质提交与纯 CPU 阶段混在同一个无分段计时函数中，难以审计收益或漂移。

**修复**

- `FABTSM3TerrainVisualField::QueryContinuousSurfaceSample` 一次解析中央 Cell，半径与颜色直接复用；曾尝试让八个法线偏移以中央 Cell 为 `StartCellHint`，exact oracle 在 `Sample[12]` 首次发现法线低位不同。恢复旧 Cell-0 路径后差异仍存在，高精度日志最终证明同一 canonical normal 在 ParallelFor worker 与 Game Thread 间约有 `1e-15` 低位差，推翻了“Hint 平台终点就是根因”的早期假设；最终八个偏移继续沿旧 Cell-0 canonical 单算并按顶点索引串行，不保留 Hint+canonical 双算负优化，也不修改 worker 浮点控制寄存器；
- 中央 Cell/radius/color sample 与三角形展开只在预分配数组中按固定索引 `ParallelFor` 写各自槽位；canonical normal、最大法线倾角、极端计数、exact 比较和最终提交严格按升序索引串行归并；
- `UObject`、PMC、Chaos physics、MID/HISM 的写入继续只在 Game Thread；`bootstrap -> final surface` 的 MapFreeze 有向顺序未改；
- 新增 wall 分段日志与 `ABTSM3ContinuousSurface_*` CPU Trace scope。opt-in exact oracle 在 PMC 提交前双跑旧串行/新并行路径，逐 sample 精确比较 Cell/radius/normal/color，逐元素比较最终 Vertices/Triangles/Normals/UV0-3/Colors，并比较与 M3R5 相同算法的 `QuerySurfaceHash`；任何差异清空 section 并令 `RebuildPlanet()` fail closed；
- oracle 同时快照并复验 Layout/Catalog/逐栋 PlacementHash，证明连续地表优化不会改写 V3 身份。未修改 shared contract、manifest、M2/M7、共同地图或两阶段表面权威顺序。

**防回归验证**

- 源码级门：`git diff --check` 通过；调用关系确认只有纯 `FABTSM3TerrainVisualField` 读取进入 worker，PMC/physics/material 调用均位于 `ParallelFor` 返回后的 Game Thread；
- `ABTS.M3.Jury.MapFreezeV3.01DeterminismAndRoadFacing` 显式打开 `abts.M3.ContinuousSurfaceExactOracle=1`，两次重建都必须出现 `Enabled=1 Passed=1`，且既有 LayoutHash 与逐栋 PlacementHash 重复一致；
- UE 5.8 Development 全量 `35/35` 与最终 ForceUnity `8/8` 均为 `Result: Succeeded`；
- 最终 fresh NullRHI：`Saved/Logs/M3ContinuousSurfaceExactOracle-Final-MapFreezeV3-20260816-091351-FreshNullRHI.log`，`ABTS.M3.Jury.MapFreezeV3` 精确 `2/2 Success / EXIT CODE: 0`。第一项两次重建均为 `Enabled=1 / Passed=1 / QuerySurfaceHashLegacy=QuerySurfaceHashOptimized=75D145345C3A03A6 / V3IdentityUnchanged=1`；当前 `Layout=0044C9789AD84147 / Catalog=0B11216D494069A0`，六栋 PlacementHash 为 E2 `A91A9FB5D79AE1CE`、E3 `4C41612002CC0208`、E4 `8ACA9CA9BAFE95BD`、E5 `66C8FD0EF4ACD5F2`、E1 `1C267DFD88E65BAB`、E6 `618C6BB2B4FB749B`；
- fail-closed 诊断日志 `M3ContinuousSurfaceExactOracle-MapFreezeV3-20260816-085943-FreshNullRHI.log`、`...-090708-...` 与 `M3ContinuousSurfaceExactOracle-FieldDiagnostic-20260816-090857-FreshNullRHI.log` 均保留，证明 oracle 先后拒绝 Hint 版本和 worker canonical normal 的低位漂移；NullRHI `Subdivision=1` 计时不冒充生产性能证据，真实 `Subdivision=7` 候选计时单列如下；
- final-surface fresh offscreen D3D11 候选证据：`Saved/Logs/M3ContinuousSurface-FinalSurface-D3D11-20260816-091827.log`，CPU trace 为 `Saved/Profiling/M3ContinuousSurface-FinalSurface-D3D11-20260816-091827.utrace`。`Subdivision=7` 的 `ContinuousSurface WallMS.Total=2428.849`，分段为 Topology `13.038`、BaseSamples `41.311`、CanonicalNormals `1368.613`、TriangleExpand `12.055`、PMC `993.092`、Physics `0.102`；整次 M3 Rebuild `4556.109 ms`。相对历史 `4930.347 / ~2948 ms` 仅能说明候选方向改善：两次运行跨二进制、跨最终 V3 seal，且新运行启用了 CPU trace，不能作为严格同基线 A/B；
- 同次 D3D 的 M3/V3 身份与表面门通过：`MapFreezeV3 Ready=1 / SurfaceAuthority=FinalV3 / Layout=0044C9789AD84147 / Catalog=0B11216D494069A0`，六个 PlacementHash 与 fresh NullRHI 逐项一致，`ProductionClearance Passed=1`，初始 final-surface runtime 为 `DeltaFromPreview=0.00 / Ready=1 / TrajectoryCertified=1 / TrajectoryHash=CB88635D085D213C`，不存在 surface divergence；
- 该 D3D **不构成最终联合通过证据**：M7 实体 Crystal 生产绑定随后触发第二次 `ProductionCrystal=1` 轨迹认证，结果 `PracticePassed=0 / FullFrozenCarrierPassed=0 / GravityOnHits=0 / ResultHash=3A898F529035E194`，继而出现 M3 `CertificationRejected` 与 Integration `E1CrystalTarget Rejected Reason=SatelliteRuntimeBindingRejected`。此拒绝位于外部 M7 实体 Crystal 替换/绑定路径，未放宽任何 M3 容差或轨迹门；保持本候选未提交、停止重型任务，等待 M7 修复合入并同步后再做最终复证。

### M3-JURY-012：固定 E1 Site 的强化弹弓应命中真实冻结建筑模块而非旧代理或 Crystal

**现象**

- 产品语义最终收敛为：E1 Site、卫星、final surface、五垫和当前 yaw 全部固定；强化弹弓只需可靠 first-hit E1 的真实建筑模块，不要求直接击中 `72 cm` Crystal；
- 旧 proxy 的 `51` 点最大成功岛可作为确定性 seed，但不能继续作为生产目标。冻结 Crystal 中心与旧 proxy 相差约 `389.393 cm`，固定 Site 下最佳旧轨迹仍 miss Crystal `47.1 cm`；此前 `721 yaw`/Crystal 精确命中搜索因此作废并撤回；
- 首次模块版 NullRHI 正确选中 `BrickId=4`，但旧 `AABTSCalibrationTargetProxy::ConfigureCube()` 只能生成等边立方体，把真实模块半尺寸 `144/18/18 cm` 扩成 `144/144/144 cm`，自动化以 extent 不一致 fail closed，证明该临时类会制造“命中空 AABB”的假证据。

**根因**

轨迹权威长期绑定在校准代理/Crystal cap 上，没有从公开冻结 E1 descriptor 的真实 `Bricks` 集合派生模块 OBB。与此同时，runtime 临时目标沿用了只支持 cube 的校准 Actor，不能表达非等轴真实模块碰撞。旧 shared V3 seal 还冻结在 `0044C9789AD84147`，因此新 M3 布局即使自身 Ready，也会在 M7 注册前被共享合同 fail closed。

**修复**

- 新权威命名为 `FrozenE1BuildingModules`。从公开 `FABTSM73BuildingFreezeV3Descriptor::Bricks` 读取每个 `BrickId / LocalTransform / DimensionsCM`，不读取 M7 私有 manifest；非生产 preview candidate 也只携带真实 descriptor module，不再携带 cap/旧代理几何；
- 保持 `ProjectionExact=1` Site、卫星中心、final surface、五垫、`Correction=-8.320°` 与 `SiteYaw=0°` 不变。对旧 proxy 完整 `61 x 31` 认证并精确枚举 `144` 个 gravity-dependent seed，只用于确定性筛选真实模块；筛选得到 `BrickId=4 / seed island=24` 后，对其真实 OBB 重新执行完整 `61 x 31` 最终认证；
- 最终门保持 `GravityDependentHits>0`、aim 连通、原 Pull 范围、`SimpleHits=0`、`OutsidePullHits=0` 与 gravity-off miss 原阈值。MapFreeze、candidate hash、runtime snapshot 和 exact binding 均携带同一 `ProductionTargetModuleId`、descriptor hash、world transform、三轴 extent 与 target identity；任何不一致销毁临时目标且 `ProxyFallback=0`；
- runtime 不再生成旧洋红校准 proxy 类。绑定前只生成位于冻结真实模块 transform、使用精确 `UBoxComponent(144,18,18)` 的 module OBB stand-in；真实模块绑定成功时原子退休该 stand-in。现有 M7 Crystal 入口仅保留源码兼容 wrapper，其几何不匹配会被 exact identity 拒绝，不能冒充模块绑定；
- `PendingIntegrationSeal` 解析门按 M7 公开注册算法只读计算 proposed Satellite/Layout/Registration，供 Integration 正式重冻结；M3 未修改 shared seal、manifest、共同地图或 M7 代码。

**防回归验证**

- UE 5.8 Development Editor 普通构建先后 `25/25`、`13/13`、最终测试增量 `4/4 actions`，均 `Result: Succeeded`；`-ForceUnity` 复核目标为 up-to-date / `Result: Succeeded`，未启用 Live Coding 或结束其他进程；
- `Saved/Logs/M3FrozenE1BuildingModules-SatellitePreview-20260816-103107-FreshNullRHI.log`：`ABTS.M3.Monthly.SatellitePreview` 精确 `4/4 Success / EXIT CODE: 0`；`Saved/Logs/M3FrozenE1BuildingModules-MapFreezeV3-20260816-103454-FreshNullRHI.log`：MapFreezeV3 精确 `2/2 Success / EXIT CODE: 0`；
- 固定位置 oracle：Satellite center `(-4063.99,14483.41,-3013.75)`、projected Site `(-3735.87,15423.47,-3769.48)`、`ProjectionExact=1`；真实 target 为 `BrickId=4`、world center `(-3417.04,15424.54,-3778.59)`、half extent `(144,18,18)`、descriptor `A9723A48ADEE487F`、target identity `15CC3CA9C1DF1694`；
- 最终真实模块轨迹为 `ReinforcedHits=59 / GravityDependentHits=59 / Island=24 / AimNeighbors=1 / Pull=[0.850,0.890] / GravityOffMiss=2710.6 / SimpleHits=0 / OutsidePullHits=0 / Hash=72B802EB5C6411A5`；runtime exact OBB 重算保持同一 hash 并达到 `Ready=1 / Collision=1 / M6Target=1 / TrajectoryCertified=1`；
- 新冻结身份：Satellite candidate `3E99A400B52C252C`、Satellite result `0ECC20D82E8E2156`、Layout `CB7C582A402911D5`、Catalog `0B11216D494069A0`、proposed Registration `34F159419F042166`。六栋 PlacementHash 逐项保持 E2 `A91A9FB5D79AE1CE`、E3 `4C41612002CC0208`、E4 `8ACA9CA9BAFE95BD`、E5 `66C8FD0EF4ACD5F2`、E1 `1C267DFD88E65BAB`、E6 `618C6BB2B4FB749B`，证明 Site/建筑位置未移动；
- `Saved/Logs/M3FrozenE1BuildingModules-PendingSeal-20260816-104106-FreshNullRHI.log` 精确发布上述 proposed seal 并 `1/1 Success`；
- fresh offscreen D3D11 `Saved/Logs/M3FrozenE1BuildingModules-L_ABTS_M10-OffscreenD3D11-20260816-103728.log`：`SurfaceAuthority=FinalV3 / DeltaFromPreview=0.00`，真实 module OBB 的生产重算精确为 `72B802EB5C6411A5`，runtime `Ready=1`，进程在 capture 后自行 `exit 0`；`ContinuousSurface=2403.655 ms`，但整次 Rebuild 为 `31193.839 ms`，已略超 `30 s` 目标，主要新增成本是旧 seed 枚举和真实模块完整认证，后续性能工作不得改变任何身份或门槛；
- 同次 D3D **不构成 M7 实体 module 联合绑定通过**：旧 shared seal 先报告 `BuildingContractSealed Expected=0 Registered=0 SetupRejected=1`，未生成六栋实体，因此没有可绑定的真实 `BrickId=4` Actor。Integration 必须先正式发布 proposed exact seal，再由 M7 owner 把实体模块/碰撞集合接到新 exact binding 入口并复跑；本提交保持 `PendingIntegrationSeal`，不伪通过。

### M3-JURY-013：E1 生产目标必须是有序真实 Brick OBB 联集，单 Brick 扩张体不能作为证书

**现象**

- M3-JURY-012 的 `BrickId=4 / Trajectory=72B802EB5C6411A5` 虽然通过单目标自动化，却把真实半尺寸 `(144,18,18) cm` 经 shared 单目标接口的 `ComponentMax` 扩成了 `(144,144,144) cm` 立方体；该证书可在真实长条 Brick 之外的空体积中命中，违反“必须 first-hit E1 真实建筑模块”的产品语义，因此该 BrickId、轨迹及其派生 Satellite/Layout/Registration 身份全部作废；
- 若简单对 3 个 retained candidate 重跑旧 proxy、逐 Brick 选择和完整 `61 x 31` 生产轨迹，生产重建会达到约 `31.5 s`；若直接对所有 54 个 OBB 逐段计算完整 miss clearance，首次真实联集认证约 `41 s`，也无法满足 `RebuildBudget < 8000 ms`。

**根因**

共享校准接口只表达一个轴对齐/等边目标，不足以表达公开 E1 descriptor 中 54 个具有独立 `BrickId / SiteLocalTransform / DimensionsCM` 的非等轴 OBB 联集。旧证书的 hash 只覆盖被扩张的单目标，既没有覆盖有序模块集合，也没有覆盖稳定 first-hit 的 BrickId；runtime 的单 box stand-in 同样无法证明实际 E1 碰撞集合。性能方面，旧实现又把同一组 61 x 31 动力学轨迹对 retained candidate、逐 Brick 和生产目标重复积分。

**修复**

- `TargetAuthority=FrozenE1BuildingModulesUnion` 从公开冻结 E1 descriptor 派生 54 个按 `BrickId` 稳定排序的真实 OBB；TargetIdentity 覆盖 descriptor、固定 Site world transform、每个 BrickId、local transform 与三轴 half extent。任何单 box、max-axis cube 或 hidden proxy 都不能冒充联集；
- 每条轨迹只积分一次，并对有序 OBB 集合做精确 segment-vs-oriented-box first-intersection。按最小命中 alpha、再按 BrickId 稳定决胜；任一真实 E1 Brick first-hit 合法，卫星/月体先命中则拒绝。联集外包 OBB 只作 broadphase，不能产生成功；轨迹 hash 覆盖 TargetIdentity、61 x 31 采样结果及每个成功轨迹的真实 first-hit BrickId；
- 旧 proxy 的 `61 x 31` 成功样本只并行生成一次并作为不可变 seed。固定索引 `ParallelFor` 写各自槽位、串行稳定归并；只对最终选中的 production satellite candidate 执行一次真实联集证书，两个未选 retained candidate 不做 production target certification。gravity-on 失败路径不再为全部约 49k miss 计算昂贵的精确 clearance；真实 broadphase 命中和所有 gravity-off 复证仍做 exact OBB clearance，first-hit、障碍优先级和原门槛不变；
- runtime 预绑定 stand-in 在固定 Site Actor 下创建 54 个 `UBoxComponent`，逐个使用 descriptor 的 local transform 与真实三轴 extent；M6 绑定联集 Actor。M7 cap 入口仅作为公开 Site 锚点，M3 从 cap 反解 Site 后查找唯一、已接受且 transform exact 的 `AABTSM73StableBuildingActor`，再把 gameplay target 原子切换为真实整栋 E1；身份比较覆盖整个联集，`ProxyFallback=0`；
- `GeneratorVersion` 升为 6，缓存 key 版本升为 2；没有修改 shared calibration、shared seal、M7 manifest、共同地图或稳定契约。固定 Site、卫星、yaw、final surface、五垫和六栋 world transform 均不移动。

**防回归验证**

- UE 5.8 普通 Development Editor 最终增量构建 `5/5 actions / Result: Succeeded`，随后最终 `-ForceUnity -DisableAdaptiveUnity` 为 `4/4 actions / Result: Succeeded`；源码只进入 M3-owned preview/runtime/test 与本账本，未启动 Live Coding、未结束其他工作树进程；
- fresh NullRHI `Saved/Logs/M3-HonestUnion-SatellitePreview-Final-4of4-20260816.log`：`ABTS.M3.Monthly.SatellitePreview` 精确 `4/4 Success`。真实联集为 `Modules=54 / WitnessBrickId=16 / ReinforcedHits=44 / GravityDependentHits=44 / Island=6 / AimNeighbors=1 / Pull=[0.900,0.910] / SimpleHits=0 / OutsidePullHits=0 / GravityOffMiss=2907.5`，`TargetIdentity=5F1D6833F37C28C4 / Trajectory=3BE096C16B3D9807`；预绑定 stand-in 精确包含 54 个 box，单一 witness box 明确 fail closed；
- fresh NullRHI `Saved/Logs/M3-HonestUnion-MapFreezeV3-2of2-20260816.log`：`ABTS.M3.Jury.MapFreezeV3` 精确 `2/2 Success`，exact oracle 为 `QuerySurfaceHashLegacy=QuerySurfaceHashOptimized=75D145345C3A03A6 / V3IdentityUnchanged=1`。proposed seal 为 Satellite candidate `34AA95E1E0FA3303`、Satellite result `A13243FDC2E7D83B`、Layout `3E143A25531F3F7A`、Registration `F4B6DDE7F687C766`、Catalog `0B11216D494069A0`；
- 六栋 PlacementHash 为 E2 `A91A9FB5D79AE1CE`、E3 `4C41612002CC0208`、E4 `8ACA9CA9BAFE95BD`、E5 `66C8FD0EF4ACD5F2`、E1 `1C267DFD88E65BAB`、E6 `618C6BB2B4FB749B`，逐项证明位置/方向未变；E1 Placement 也未因 target identity 改变，变化只沿 Satellite candidate/result -> Layout -> Registration 依赖链传播；
- fresh NullRHI `Saved/Logs/M3-HonestUnion-DecorPlacement-4of4-20260816.log`：`ABTS.M3.DecorPlacement` 精确 `4/4 Success`，两次生产重建分别 `6205.926 / 5119.317 ms`，均低于 `8000 ms`；首次真实联集证书 `1065.315 ms`，缓存复用约 `0.03 ms`，`ProductionSweeps=1 / UnselectedProductionSweeps=0`；
- fresh offscreen D3D11 `Saved/Logs/M3-HonestUnion-L_ABTS_M10-OffscreenD3D11-20260816.log`：`MapFreezeV3 Ready=1 / SurfaceAuthority=FinalV3 / Layout=3E143A25531F3F7A`，`ContinuousSurface=2471.450 ms`、整次 `RebuildBudget=6583.687 ms / Passed=1`、`ProductionClearance Passed=1`，runtime 为 `DeltaFromPreview=0.00 / TrajectoryHash=3BE096C16B3D9807 / Ready=1 / TrajectoryCertified=1`；日志不含 `SatellitePreviewRuntimeDivergence`、`SpawnRejected` 或 M3 trajectory rejection；
- 该 D3D 仍**不构成最终 M7 实体联合绑定通过**：功能树的旧共享 seal 在 M7 注册前以 `BuildingContractSealed Expected=0 Registered=0 SetupRejected=1` fail closed，因此没有真实 E1 Actor 可替换 stand-in，最终 StartupFlow 也正确 blocked。只有 Integration 原子发布上述 proposed seal 后才能复跑实体绑定；M3 不越权修改 shared/M7，也不把 stand-in Ready 冒充联合 Ready。

## 15. 新条目模板

```markdown
### M3-<阶段>-<序号>：<短标题>

**现象**

- <玩家画面、日志或构建错误；附最小复现条件>

**根因**

<说明错误发生在哪个数据/生命周期/工作树边界，区分已证实与推测。>

**修复**

<代码、配置或流程上的最终处理；注明归属工作树。>

**防回归验证**

- <自动化筛选器、fresh 运行命令或 PIE 操作>
- <必须出现/不得出现的日志、Hash、数量和视觉结果>
```

更新后还应检查：

1. 是否需要从 M3 主改进稿或阶段详稿建立链接；
2. 是否误写了 Integration/M7/M11 所有权内的修复动作；
3. 是否保留了被推翻的旧诊断值并标明作废；
4. 是否给出了用户能独立复现的验收证据；
5. 下次合入 `master` 时，哪些条目已足够稳定，可由集成工作树整理进总排错文档。
