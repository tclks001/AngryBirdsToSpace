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
