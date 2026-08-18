# ABTS 发行收敛账号交接 Checkpoint（2026-08-17）

> 目标：在不丢失当前多工作树上下文的前提下，把 8 月 18 日发行收敛交给另一账号继续执行。
> 本文记录的是证据状态，不把“源码存在”“自动化通过”和“玩家实测通过”混为一谈。

## 0. 2026-08-18 接手后 P0 关闭与新包

### 2026-08-18 可见回归后替代候选

玩家对 `candidate-9f24bcd-20260818-Shipping` 的首次可见检查发现新的原子 P0：

- 地图没有普通弹弓槽；
- 地图没有任何掉落材料；
- 开局没有提示；
- Development 使用任意 Cell 放桩 Debug 后，匹配的弹弓弦也无法连接两根桩。

该 `9f24bcd` 包不得继续测试或晋级。`a2534e2` 的 Development/Shipping
只用于中间诊断，也不得交付。唯一替代候选的代码身份为：

`7d0793a80c0863c0735829c5ddec5aae3ad0d844`
`test(release): expose shipping starter inventory state`

其功能修复提交为父提交：

`a2534e2d89659a0989e797c5a93529d60cff85ea`
`fix(release): restore production slingshot resources`

根因和修复：

- Candidate 4 原始槽规划成功，为 8 组 × 7 槽，普通弦权威为 1200 cm。
- 旧发行适配先删除最终 V3 表面上的水面、建筑和重复槽，再只从幸存槽
  启动 BFS；某组全部原始槽失效时搜索队列为空，产生结构非法的空组。
- 生产代码随后把该失败结果配置为 `PreviewTest`，把普通弦最大长度归零；
  M5.1 槽门 fail closed 后不再生成材料，也不发布 `Guide.World.Ready`。
- 新适配保留每组全部原始 Cell 作为稳定拓扑遍历根，即使根本身非法，也能
  穿过水面/建筑 envelope 找到最终合法地表；每组必须原子得到 12 个全局唯一槽。
- 已激活的冻结 Candidate 4 以 `AcceptedMonthly` 权威配置。显式 Preview/Test
  的身份和 fail-closed 行为保持不变。

新 fresh 联合自动化精确 8/8 Success：

- `ABTS.Guide.P0.EventCatalog`
- `ABTS.Guide.P0.FastForwardAndSubjectIsolation`
- `ABTS.Guide.P0.RuleSequence`
- `ABTS.M11C.Unit.FinaleEndScreenPolicy`
- `ABTS.M11C.Unit.PreviewReleasePlayback`
- `ABTS.M51.OrdinarySlots.PreviewAdapter`
- `ABTS.M51.SlingshotAssembly.Runtime`
- `ABTS.Presentation.Opening.Timeline`

日志：

`G:\ABTS\Logs\RC9.3\Integration-P0-candidate-7d0793a-20260818-Combined-FreshNullRHI.log`

其中 M5.1 自动化新增覆盖：

- 全部原始组根失效时仍能穿越到合法生产地表；
- 无法达到 12 槽时保持输入快照不变并 fail closed；
- `AcceptedMonthly` 快照保持 1200 cm 普通弦权威；
- Development 任意 Cell 放置的两根匹配普通桩能够用普通弦连接。

最终替代包：

- Development：
  `G:\ABTS\Artifacts\RC9.3\candidate-7d0793a-m51resources-20260818-Development`
- Shipping：
  `G:\ABTS\Artifacts\RC9.3\candidate-7d0793a-m51resources-20260818-Shipping`

两个 BuildCookRun 均为完整 cook，均为
`BUILD SUCCESSFUL / AutomationTool ExitCode=0`。日志：

- `G:\ABTS\Logs\RC9.3\BuildCookRun-candidate-7d0793a-m51resources-20260818-Development.log`
- `G:\ABTS\Logs\RC9.3\BuildCookRun-candidate-7d0793a-m51resources-20260818-Shipping.log`

Shipping packaged D3D11 player-path trace 直接证明：

- `WorldReady=1 / WorldFailed=0`
- 六栋建筑 `Accepted=6 / Rejected=0`
- `M51 Ready=1 / Rejected=0`
- `OrdinarySlots=96`
- `Pickups=62`
- `MaxCord=1200`
- `Authority=AcceptedMonthly`
- `StarterBranch=0 / StarterFiber=0 / InventoryStacks=0`
- `Guide=Guide.P0.CollectResources`

证据：

`G:\ABTS\Logs\RC9.3\StartupTrace-candidate-7d0793a-m51resources-20260818-Shipping.log`

Development packaged trace 同样证明 96 槽、62 材料与 1200 cm 权威；其
Blueprint 调试满背包为 `Branch=99 / Fiber=99 / Stacks=16`，所以 Guide 按设计
快进到 `InstallTwigStakes`，不得用于替代 Shipping 的初始收集提示证据。

包文件 SHA-256 清单：

`G:\ABTS\Logs\RC9.3\Manifest-candidate-7d0793a-m51resources-20260818.txt`

本轮只获得无 GUI 和自动化证据。玩家仍需在新的 `7d0793a` Shipping 包中可见确认：

- 地图上能看到并使用普通弹弓槽；
- 地图材料可见并可拾取；
- 开局“先收集材料”提示可见；
- 拾取/制作普通桩和普通弦后能够连接；
- 后续此前未测试项目继续保持未验收。

接手后已恢复被外部工具回退/删除的四个工作树 Git 引用，并在集成候选
`integration/candidate-rc93-exact-bricks` 上完成第 6 节 P0。当前发行代码身份为：

`9f24bcd4d411b7003da397b2fa6c5097a4685418`
`fix(release): accept long valid terminal arcs`

该修复保持 HUD 与实际 Playback 消费同一个权威 plan：

- 将 F4 合格包络到物理接触的候选时长上限从展示用的 12 秒扩展到 600 秒；
- 保留逐样本 acceleration、jerk、净空、朝向、单调接近和精确接触门；
- 对无法构造“起点切向且穿过 UFO 中心”的近径向输入，确定性搜索 UFO
  物理接触球上的精确接触点并构造真实圆弧，不退回 Canvas 假轨迹或普通位置插值；
- 增加 Source、中心圆、接触球圆、时长候选和形状候选计数，失败仍然 fail closed。

### Fresh 构建与聚焦自动化

唯一引擎：

`C:\Program Files\Epic Games\UE_5.8`

结果：

- Development Editor 普通全链接：Succeeded。
- Development Editor `-ForceUnity -DisableAdaptiveUnity` 全链接：Succeeded。
- fresh NullRHI 聚焦测试：3/3 Success。
  - `ABTS.Presentation.Opening.Timeline`
  - `ABTS.M11C.Unit.FinaleEndScreenPolicy`
  - `ABTS.M11C.Unit.PreviewReleasePlayback`
- 完成标记：`**** TEST COMPLETE. EXIT CODE: 0 ****`
- 日志：
  `G:\ABTS\Logs\RC9.3\Integration-P0-candidate-9f24bcd-20260818-ThreeFocused-FreshNullRHI.log`

第 5 节记录的邻域输入失败已关闭；本次 fresh
`PreviewReleasePlayback` 不再报告 `NoGeometricCandidate`。

### 新 Development / Shipping 包

两个 BuildCookRun 均使用上述唯一 UE 5.8，均为完整 cook，结果均为
`BUILD SUCCESSFUL / ExitCode=0`：

- Development：
  `G:\ABTS\Artifacts\RC9.3\candidate-9f24bcd-20260818-Development`
- Shipping：
  `G:\ABTS\Artifacts\RC9.3\candidate-9f24bcd-20260818-Shipping`

UAT 日志：

- `G:\ABTS\Logs\RC9.3\BuildCookRun-candidate-9f24bcd-20260818-Development.log`
- `G:\ABTS\Logs\RC9.3\BuildCookRun-candidate-9f24bcd-20260818-Shipping.log`

包清单及 SHA-256：

`G:\ABTS\Logs\RC9.3\Manifest-candidate-9f24bcd-20260818.txt`

### Packaged player-path smoke

Development 与 Shipping 都从归档包的实际可执行文件启动，使用 D3D11 离屏渲染，
不带 `-unattended`，因此走真实前台门禁。两个进程均达到：

- `World=L_ABTS_M11`
- `AuthorityReady=1`
- `PresentationReady=1`
- `WorldReady=1`
- `WorldFailed=0`
- `BuildingAccepted=6`
- `BuildingRejected=0`
- `Expected=6`
- `Registered=6`
- `SetupRejected=0`

证据：

- `G:\ABTS\Logs\RC9.3\StartupTrace-candidate-9f24bcd-20260818-Development-playerpath.log`
- `G:\ABTS\Logs\RC9.3\StartupTrace-candidate-9f24bcd-20260818-Shipping-playerpath.log`

Smoke 完成后只结束了本次命令启动且通过绝对可执行路径和 trace 参数确认归属的
两个包进程，没有结束其他 Unreal 或用户进程。

### 仍需玩家可见验收

上述证据关闭代码、自动化、Cook、Package 和无 GUI startup/world-ready P0，
但不能替代玩家可见手感/像素验收。第 6 节第 5 项仍需使用新 Shipping 包逐项检查：

- Shipping 进入正确 V3 世界；
- 低积木建筑静态/Chaos 材质一致；
- 蓝鸟命中后能够回归；
- E5 有足够槽安装普通与强化弹弓；
- 黄鸟穿木后速度保留；
- 左下轨迹末端为连续圆弧；
- 终局动画后停在 `F I N E` 页面并能正常退出；
- 开局五只鸟在坡面上均不悬空。

在玩家完成这组可见回归前，新包状态是“可交付验收候选”，不是“玩家最终验收通过”。

## 1. 接手入口

- 原始集成工作树：`C:\workspace\AngryBirdsToSpace`
- 集成分支：`integration/candidate-rc93-exact-bricks`
- Checkpoint HEAD：`26b88d0606202203b13d573e6ab29c5aacf69dfc`
- UE 唯一允许版本：`C:\Program Files\Epic Games\UE_5.8`
- 首先完整阅读：`Docs/ABTSMultiWorktreeDevelopmentGuide.md`
- 当前发行范围与玩家引导队列：`Docs/ABTSReleaseScope20260818.md`
- 当前集成工作树在写入本文前是干净的；不要使用 `git add .`、共享 stash、强制 reset 或 clean。

工作树状态：

- Integration：HEAD `26b88d0`，本轮唯一集成写入者。
- M11：`feature/m11-finale`，HEAD `ae0eff75`，干净。
- M3：`feature/m3-pcg-map`，HEAD `023fe849`，干净。
- M7：`feature/m7-buildings`，HEAD `5259026a`，存在用户二进制资产改动
  `Content/Maps/PlanarPhysicsTestMap.umap`；不得覆盖、清理或纳入集成提交。

## 2. 最近的集成提交

从新到旧：

- `26b88d0 fix(release): ground opening cast on production surface`
  - 生产开局动画按最终主星地表逐鸟投影。
  - 四只真实 Party 鸟保留各自模型 pivot/离地间隙；第五只白鸟使用同批真实鸟的平均间隙。
  - White 的飞行/吸走 cue 不投影；真实鸟在 0 秒和 42 秒仍精确混合到生产 transform。
  - 同时补回 M11 cherry-pick 冲突中遗漏的 Finale audio enum/声明，使 Editor 重新可编译。
- `8c6ba66 fix(m11): smooth terminal fallback and end screen`
  - 来自 M11 `ae0eff75`。
  - 增加生产终端圆弧回退和成功动画后的 `F I N E / END GAME` 退出页。
  - **当前不能视为验收通过**：见第 5 节精确失败。
- `a0d9252 docs(release): queue next-package player guidance`
  - 只记录下一版本教学需求，没有实现 UI。
- `93fb25e fix(release): unblock reinforced slingshot progression`
  - release-only ordinary slot 快照扩展到每簇 12 个可用槽；用于越过 E5 河岸坏槽造成的卡关。
  - 强化弹弓桩和强化弹弓袋/弦的 MetalParts 消耗分别降为 1。
  - 黄鸟确实清除木质 M7 brick 后保留 94% 速度，营造穿透感。
- `64695b7 Bound flight settlement return without collision fallback`
  - 针对蓝鸟命中后长时间不回归的源码修复；尚无玩家对该提交打包后的确认。
- `d568238 Fix release walk return and resource rewards`
  - 1--4 直接选鸟、Shipping 隐藏月球下方 debug 强化弹弓、树/石头摧毁各奖励 2 Wood/Stone 等。
- `0ce8335 Keep fixed-six material stable through Chaos activation`
  - 低积木全 Chaos 保底路线的静态/动态材质统一。
- `ded3922 Add compact full-Chaos release building fallback`
  - E1/E2 保留，E3--E6 压到 E1--E2 数量区间；共 1,246 bricks，单栋不超过 384。
- `46a4aef Fix Shipping trajectory and recipe routing`
  - Shipping 轨迹/侦察图和制作配方路由修复，仍需要最终候选玩家回归。

## 3. 玩家已经确认通过（不要重开根因调查）

玩家已在 `candidate-c9037ca-20260817-Development` 运行确认：

- 第一段加载结束后不再闪错误世界帧；
- 开局镜头与鸟动画正确；
- 风格化天空、星空、低模云正确；
- 终局底部四个按钮可点击。

后续候选只做普通回归；除非新包肉眼回退，否则不要重新调查这些旧根因。

玩家也已接受“低积木全 Chaos”作为发行保底方向，允许其动力传播不完美；材质统一后应优先产出可进入、可游玩的 Shipping，再继续改善其他问题。

玩家随后在最新 Shipping 包
`candidate-64695b7-releasefixes-r2-20260817-Shipping` 中实测确认以下四项通过：

- 退出发射模式、返回行走时，建筑顶部不再延迟突然掉落；
- 数字键 `2` 可以选择青翎；
- Shipping 中月球下方的 debug 强化弹弓已隐藏；
- 摧毁树和石头会把对应资源奖励直接加入背包。

这四项已经从开放问题队列移除。除非后续包出现肉眼可见回退，新会话不得重复修复或重开根因调查。**除本文明确写为玩家实测通过的项目外，其他问题均未验收。**

## 4. 现有可运行包与边界

最新已归档包（不包含 `93fb25e`、`8c6ba66`、`26b88d0`）：

- Development：
  `G:\ABTS\Artifacts\RC9.3\candidate-64695b7-releasefixes-r2-20260817-Development\Windows`
- Shipping：
  `G:\ABTS\Artifacts\RC9.3\candidate-64695b7-releasefixes-r2-20260817-Shipping\Windows`

它们是当前最近的可运行基线，不是最终验收包。当前 HEAD 尚未重新 Cook/Package；这是有意停止在可审计 checkpoint，而不是遗漏。

其中 Shipping 包只对第 3 节列出的四个最新项目取得了玩家验收，不代表该包或其他功能已整体通过。

更早、可恢复的低积木材质稳定 Shipping floor：

`G:\ABTS\Artifacts\RC9.3\candidate-0ce8335-lowbrick-fullchaos-materialstable-20260817-Shipping`

## 5. 本 checkpoint 的验证结果

### 编译

UE 5.8 `AngryBirdsToSpaceEditor Win64 Development`：**Succeeded**。

第一次编译发现 M11 合并时只保留了
`ABTSM11ResolveFinaleCompletionAudioCue` 实现、却漏掉类型/声明；已在 `26b88d0` 修复。第二次完整增量链接成功。

### 聚焦 NullRHI

日志：

`G:\ABTS\Logs\RC9.3\Checkpoint-Opening-M11-20260817-FreshNullRHI.log`

结果：

- `ABTS.Presentation.Opening.Timeline`：Success。
- `ABTS.M11C.Unit.FinaleEndScreenPolicy`：Success。
- `ABTS.M11C.Unit.PreviewReleasePlayback`：**Fail**。

精确失败：

`Neighbor F4 transfer failed Input=(-0.1875,29.75,0.971875), SourceTime=577.057443848, Failure=NoValidCandidateCircularContactTransfer:NoGeometricCandidate`。

因此不得宣称 M11 新生产圆弧已通过，也不得直接把当前 HEAD 晋升为最终 Shipping。正确修复必须继续让 HUD 和实际 Playback 消费同一个权威 plan；不能只在 Canvas 上另画一条假圆弧。需要为“无圆几何候选”的邻域提供连续、密采样、最终精确接触 UFO 的稳健回退，并重新通过上述测试。

## 6. 接手后的 P0 顺序

1. 修复或安全回退 M11 `PreviewReleasePlayback` 的无几何候选失败；保留 `F I N E / END GAME` 正常退出页。
2. 重新运行第 5 节三个聚焦测试；全部通过后才打新包。
3. BuildCookRun 新 Development 和 Shipping 到 `G:\ABTS\Artifacts\RC9.3`，包名包含精确短 SHA。
4. 先做无 GUI 的 packaged startup/world-ready smoke，再交给玩家做可见验收。
5. 下列项目仍未获得玩家验收，玩家优先检查：
   - Shipping 能进入正确 V3 世界；
   - 低积木建筑静态/Chaos 材质一致；
   - 蓝鸟命中后能够回归；
   - E5 有足够槽安装普通与强化弹弓；
   - 黄鸟穿木后速度保留；
   - 左下轨迹末端为连续圆弧；
   - 终局动画后停在 `F I N E` 页面并能正常退出；
   - 开局五只鸟在坡面上均不悬空。

## 7. 仍需跟进的问题

### 已关闭：发射转行走时建筑延迟垮塌

玩家已在最新 Shipping 包中确认，“退出发射模式、返回行走时建筑顶部才突然掉落”问题已经解决。该验收同时覆盖此前在模式切换时才暴露的建筑延迟垮塌现象。

不要继续把它列为建筑第一号问题，也不要在没有新包可见回退证据时重新修改 flight-to-walk settlement/freeze。低积木方向本身已被玩家接受。

### 其他未完成/未验收

- 偶现发射视角穿到地底：低优先级，已记录。
- `46a4aef` 的 Shipping 轨迹/侦察图与制作配方路由需要新包回归。
- `93fb25e` 的 E5 槽扩展、强化配方 1 铁、黄鸟木穿透尚未打包给玩家。
- `26b88d0` 的开局逐鸟贴地只有编译和纯 Timeline 证据，没有可见打包证据。

数字 `2` 选青翎、Shipping 隐藏月下 debug 弹弓、树/石奖励以及发射转行走时建筑延迟垮塌均已玩家验收，不属于本节。除此之外，不得从“源码存在”“自动化通过”或本次四项验收推导任何其他问题已经通过。

## 8. 下一版本玩法引导（已排队，尚未实现）

`a0d9252` 已把以下内容写入发行范围文档：

- 普通/强化弹弓桩必须匹配普通/强化弹弓弦，并给出不匹配提示；
- 制作失败时明确显示缺少的材料或工作站；
- 指示月球背面存在 E1，并说明水晶来源；
- 教学终局每个窗口和按钮，重点说明轨迹全览的观察点选择与视图移动；
- 需要工作台/熔炉的配方页同时显示相应工具贴图，不能只靠小字。

要求：可关闭、不遮挡被教学控件、不改变轨迹/制作/库存权威；新玩家与已完成教学的存档都要测。先完成 P0 候选，再实施该队列。

## 9. 协作与额度约束

- 同一工作树同一时刻只有一个 tracked-file writer。
- Integration 负责 master/候选、共享契约、共同地图和最终打包。
- M3/M7/M11 只能在各自所有权内提交，由 Integration 审查后 cherry-pick。
- 重型 Build/Cook/Package/D3D/Chaos 串行；NullRHI 最多两个并行。
- 不结束用户或其他工作树的 Unreal 进程；禁用 Live Coding/Hot Reload。
- 当前账号额度只剩约 4%，因此本 checkpoint 后不再扩展实现。接手账号应从第 6 节继续，不重复大范围历史审查。
- 简单日志提取/机械检查用低成本模型；跨系统契约、发行裁决和失败圆弧算法才使用高能力模型。
