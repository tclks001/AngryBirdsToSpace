# M7.3-DAG-4：Settled Contact 与攻击对照

> 状态：阶段已于 2026-07-29 完成。代码、fresh 自动化、三 Pattern/四材料真实 Chaos 对照与用户可见机械响应均已验收；提交 `8a1aab8` 还将弱点/失效模式诊断覆盖层改为仅在编辑器视口可见、PIE/游戏隐藏。生产 Profile 中 DAG3-A/B/C/DAG-4 仍默认关闭；建筑外形与弱点布局多样性不再阻塞本阶段，等待 [DAG5-B/C](M73DAG5CandidateSearchSemanticEnvelopeAndProductionDesign.md) 提供真正不同的建筑骨架与六栋联合选择，并在 DAG5-E 逐栋重新执行本阶段认证。父级设计见 [M7.3-DAG-3 内部 Failure Frontier](M73DAG3InternalFailureFrontierDesign.md)，总路线见 [递归承载 DAG 生成总稿](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)。

## 1. 目标与阶段边界

DAG-4 把 DAG3-C 的静态可玩候选提升为动态认证候选。一个候选必须依次通过：

1. 完整建筑通过现有零穿透与 IdleValidation；
2. 以 Idle 最终 Transform 重建真实接触图；
3. settled 图仍保留全部 Required 接触，且 Failure Frontier 没有新旁路；
4. 从同一 settled 快照执行一次弱点移除和恰好三次普通点移除；
5. 弱点试验产生符合预期方向的主体级响应，且效果显著高于每一个普通点试验；
6. 全部试验结束后精确恢复 settled 完整态，再 Freeze 为正式发射初态。

本阶段不做：

- 不在 rollout 中动态调参，不换弱点材质，不加入隐藏伤害倍率；动态反事实若暴露生成期第二弱点或错误 `W/P` 关系，必须回到 DAG3-B 修正几何并重新认证；
- 不把 Chaos 当成逐位确定的求解器，不按某一帧的精确坐标判定通过；
- 不启用 TaskGraph 默认 Profile，不允许 DAG Reject 回退 Legacy；
- 不实现 WFC 包络、六栋视觉去重、候选池、Encounter 难度消费或装置连锁；
- 不修改 M6 StartupPhysics 契约；DAG-4 只延长同一建筑已有的 `Running` 状态，最终仍以现有 Accepted/Rejected 门通知上游。

## 2. 唯一运行时链路

```text
DAG1/2/2.3
→ DAG3-A Frontier
→ DAG3-B 几何改写
→ DAG3-C 攻击/净空/材料认证
→ Spawn Runtime Modules
→ Penetration=0
→ Hidden Chaos Idle
→ Capture Settled Snapshot
→ Rebuild Settled Contact DAG
→ Revalidate Frontier / COM / Hull / Tip / Reseat
→ Weak Removal Rollout
→ Ordinary Removal Rollout × 3
→ Compare
→ Restore Settled Intact State
→ Freeze
→ IdleValidation Accepted
```

当 DAG-4 关闭时，现有 IdleValidation 路径必须精确不变。启用 DAG-4 时必须同时启用并通过 DAG3-A、DAG3-B、DAG3-C；缺少任一前置结果都立即失败关闭。

## 3. Settled 快照

快照在 Idle 空间门槛已经通过、模块仍处于最终低速 Transform 时采集。每个节点至少保存：

- 稳定 `NodeId`、Macro 身份、主体验证标记和真实材料；
- 原始碰撞尺寸；
- 相对建筑 Anchor 的 settled 位置与旋转；
- 完整世界 Transform；
- 由实际 MaterialSystem Profile 计算的质量；
- 生成期 Required 接触、Ground、DAG3-B `W/P/Affected` 和 DAG3-C 攻击方向。

节点身份来自 `RuntimeModulesByNodeId`，不得按 Actor 枚举顺序或空间最近邻反推。找不到、重复或已经销毁的节点均拒绝。

## 4. Settled Contact 重建

### 4.1 几何口径

每个运行时 Brick 仍是简单盒体，但 settled 后允许小量平移和旋转。重建器使用实际 settled OBB：

1. 把八个盒角变换到建筑局部坐标；
2. 沿局部 Gravity Up 计算上下支撑间隙；
3. 将盒体投影到局部 XY，并构建凸多边形；
4. 对上下投影做凸多边形交集，交集面积作为 Contact Patch；
5. 只有垂直间隙在容差内且交集面积为正时才生成 `Lower → Upper`；
6. Ground 以 Foundation 顶面和实际节点底部重新识别，不沿用生成期布尔值冒充 settled 结果。

接触 Hash 只绑定规范排序后的 Ground 与物理边身份；Contact Area 和量化 Transform 作为诊断 Hash 的输入，但不能因亚厘米沉降把同一拓扑误判为另一栋建筑。

### 4.2 必须复验的内容

- 生成期 Required 接触全部仍存在；
- 完整态全部主体节点仍有 Ground 路径；
- 同时移除完整 Frontier `F=W∪P` 后，`AffectedMainBodyNodeIds` 全部失去 Ground 路径；
- `InternalSingleSupport` 只移除 `W` 后主体断路；
- Dual/Seam 只移除 `W` 后 `P` 与 Load Plate 仍连 Ground；
- settled 完整支撑 Hull 的 COM 裕量继续合格；
- Dual/Seam 的剩余 Hull、TipMargin、方向与 ReseatRisk 继续合格；
- 新接触只有真正跨越 Frontier 并恢复主体 Ground 路径时才计为 Bypass；无关主体内部的新贴合单独记录，不把诊断数量等同于旁路数量。

任何 Missing、Ground 断路、Frontier Bypass、失效方向退化或非法数值均在进入 Chaos 对照前拒绝。

## 5. 普通点选择

普通点不能人工写死，也不能挑一个明显无关的装饰块制造虚假优势。规划器从 settled 图中确定性选择：

1. 排除 `W`、Ground、已销毁节点和非法材料节点；`P` 必须作为优先普通点参加对照，不能因为它可能产生较高收益而跳过；
2. 对每个节点做一次纯图移除，计算失去 Ground 路径的主体质量；
3. 只保留预测影响低于普通点上限的节点；
4. 优先选择迎弹面可读、与弱点高度接近但属于不同 Node 的候选；
5. 按预测影响、迎弹深度、高度差和 NodeId 稳定排序；
6. 选择显式参数要求的 `NonWeakProbeCount`，数量不足即拒绝。

首版正式合同固定为恰好三个普通点，即每栋执行 `1 Weak + 3 Ordinary = 4 Trials`。`NonWeakProbeCount` 保留为显式 Profile 参数只服务后续扩展，但本轮正式默认必须为 3，`MaxTrialCount` 必须至少为 4。当前 DAG3-B 三 Pattern 都只有一个 `W`；若未来改为多节点弱点，必须先定义等成本多节点普通组合，本阶段不以多个普通单点对比一个多点弱点。

## 6. 可复位 Chaos 试验

### 6.1 试验隔离

正式模块在整个 DAG-4 阶段保持原位 Freeze；每次试验都从同一份 settled
快照创建一座远离正式建筑的瞬态 Shadow Island：

1. 复制 Foundation 盒体以及除当前移除节点以外的全部 Brick；
2. Shadow Brick 不注册进 MaterialSystem 的伤害、回收或全局发射集合；
3. 每个 Shadow Brick 仍使用正式模块的 Cube Collision、真实材质 Profile、质量、摩擦、恢复系数和 32/8 per-body solver；
4. Shadow Island 使用正式建筑在 Anchor 处的局部 Gravity Up，以零额外冲量推进真实 Chaos；
5. 在硬时间、试验数与刚体数预算内采样；
6. Trial 结束后销毁整座 Shadow Island，再从未改变的 settled 快照创建下一座。

因此不同试验不会继承损伤、位置或缺砖，正式模块也不会被反复 Teleport 或临时
关闭碰撞。正式 Gameplay 命中仍走现有 MaterialSystem 的伤害与
`BreakModule` 路径；Shadow 移除只服务候选认证。

### 6.2 为什么首版使用“等成本移除”

当前 DAG3-C 保证整栋同材质，弱点与普通砖拥有相同 Profile 和单砖破坏成本。DAG-4 首版比较的是“支付相同破坏成本后，结构得到多少进展”，而不是比较鼠标误差或鸟体碰撞点。真实鸟击、推进冲量和可见操作由第 13 节 PIE 验收补充；候选认证不得靠给弱点额外冲量制造优势。

## 7. 动态指标

每个 Trial 记录：

```text
RemovedNodeIds
DurationSeconds
AffectedMainBodyMassRatio
PredictedAffectedRealizationRatio
MaxDisplacementCM
MaxRotationDegrees
PropagationDepth
DirectionAlignment
SecondaryContactCount
ResponseScore
```

- 节点位移或旋转超过冻结门槛后才计入动态受影响质量，避免把 Chaos 接触抖动算作坍塌；
- `PropagationDepth` 使用 settled Contact DAG 上从移除节点到显著移动节点的最短图距离；
- Drop 使用整体向下位移方向，Tip/SlideThenTip 使用受影响主体的质量加权平面位移与预期方向对齐；
- `SecondaryContactCount` 由运行时模块的只读 Hit 观察记录 rollout 中首次出现、且至少连接一个显著移动主体节点的新接触对；它不复用伤害阈值，也不让遥测反向修改物理；
- `ResponseScore` 由受影响主体质量、位移、旋转、传播深度和二次接触的有界归一化项组成，不能使用随机噪声或帧序号。

## 8. 正式通过门槛

弱点 Trial 必须同时满足：

- 主体受影响质量达到最低值；
- DAG3-B 预测闭包达到最低动态兑现比例；
- 位移或旋转至少一项达到可见响应门槛；
- 方向与 `Drop/Tip/SlideThenTip` 语义一致；
- `ResponseScore` 达到绝对下限。

随后还必须满足：

```text
Weak.ResponseScore
    >= MaxOrdinary.ResponseScore * MinWeakResponseAdvantage

Weak.AffectedMainBodyMassRatio
    > MaxOrdinary.AffectedMainBodyMassRatio
```

所有普通点都要分别保留结果；不能只保存平均值，也不能在遇到强普通点后继续换点直至通过。

## 9. 预算、失败关闭与原子提交

硬预算包括：

- `MaxRigidBodyCount`；
- `NonWeakProbeCount`；
- `MaxTrialCount`；
- `TrialDurationSeconds`；
- `MaxTotalValidationSeconds`；
- settled 接触两两检查上限。

预算非法、超出或状态机中出现 Module 丢失都拒绝。DAG-4 Result 只在完整 settled 审计和全部 Trial 完成后标记 `bAccepted=true`；失败结果保留稳定 RejectReason 和已经完成的诊断 Trial，但正式建筑不发布 Accepted。

失败时使用现有 `RejectRuntimeStructure` 清理整栋，禁止：

- 把 DAG-4 降级为 DAG3-C 成功；
- 只删除失败 Trial；
- 回退普通 DAG2.3 或 Legacy；
- 把验证残留 Transform 当作正式发射初态。

## 10. Profile 与默认值

新增独立 `FABTSM73DAG4ValidationSettings`：

```text
bEnableSettledChaosValidation = false
```

路由规则：

- Workshop / TargetBuilding / FurnaceRuins 默认 Profile：A/B/C/DAG-4 全关闭；
- Legacy migration：DAG-4 强制关闭；
- 显式 Recursive DAG Profile：保留 authored DAG-4；
- DAG-4 开启但 A/B/C 任一未开启：Resolver 与 Actor 均失败关闭；
- 生产默认值在弱点击毁可见 PIE 和全回归完成前不得修改；本阶段人工验收只允许显式测试 Profile opt-in，验收后也不自动改变生产默认。

## 11. 日志

```text
[ABTS][M7.3-DAG4][Settled]
Actor=... Nodes=... Required=... Missing=... New=...
Bypass=... Ground=... BeforeHash=... AfterHash=...

[ABTS][M7.3-DAG4][Trial]
Actor=... Type=Weak/Ordinary Probe=... Removed=...
Mass=... Realized=... Move=... Rotation=... Depth=...
Direction=... Secondary=... Score=...

[ABTS][M7.3-DAG4][Complete]
Actor=... Trials=... WeakScore=... OrdinaryMax=...
Advantage=... Accepted=...

[ABTS][M7.3-DAG4][Reject]
Actor=... Stage=... Reason=...
```

现有 `[M7.3-A][IdleValidation] Accepted=1` 只能在 DAG-4 `Complete Accepted=1` 之后输出。

## 12. 自动化

正式前缀为 `ABTS.M73DAG4.`，至少覆盖：

1. `SettledContact.RebuildAndFrontierAudit`：小量沉降/旋转正例、Required 丢失、新旁路和非法身份负例；
2. `Planner.OrdinaryProbeDeterminismAndBudget`：普通点稳定选择、逆序不变、候选不足和预算失败；
3. `Runtime.PatternMatrixWeakVsNonWeak`：Single/Dual/Seam 真实 30 Hz Physics World，逐栋 settled，并执行恰好一次弱点与三次普通点 Trial；
4. `Runtime.MaterialProfileMatrix`：木、石、铁、玻璃分别在独立真实 30 Hz Physics World 中使用实际 MaterialSystem Profile，弱点与普通点保持同材质、同成本；
5. `Profile.DisabledPrerequisitesAndAtomicFailure`：默认 no-op、Legacy 关闭、缺 A/B/C 拒绝、失败不发布 Accepted；
6. 完整回归：`ABTS.M73DAG3.`、`ABTS.M73DAG.`、`ABTS.M73B.`、`ABTS.M73B2.`、`ABTS.M7.TaskGraphDAG23ProfileRouting`、`ABTS.Contracts.WorldGeneration`。

自动化必须先用自由落体探针证明 Physics Scene 真正推进，并使用 fresh
`UnrealEditor-Cmd -NullRHI` 日志与精确 Success 计数。编译继续强制
`-ForceUnity -DisableAdaptiveUnity`。若 UE 5.8 的全局 Live Coding mutex 被其他工作树 Editor 占用，只能在审计所有 Editor 进程、确认没有进程加载本工作树项目或 DLL 后，按多工作树规范记录并使用已批准的 `-NoHotReloadFromIDE` 例外。

## 13. 人工 PIE 验收（已完成）

自动化通过后，用户已在 M7 专属 `PlanarPhysicsTestMap` 串行完成可见验收：

> 只对显式测试 Profile 打开 DAG3-A/B/C/DAG-4；不得借人工验收修改三套生产 Profile 的默认关闭状态。

1. 三栋完整态先稳定落座，不弹飞、不自倒；
2. 日志证明 settled 接触无 Missing/Bypass；
3. 重启 PIE，用同一 Seed、鸟、拉力分别击打三个普通点和真实 `W`；
4. 普通点有局部物理反馈，但结构进展明显低；
5. `W` 被真实伤害链破坏后，中上部产生 Drop、Tip 或 SlideThenTip；
6. 实际坍塌方向与编辑器中的诊断箭头、轮廓留白一致，允许 Chaos 细节不同；
7. 上部不在原位重新稳定落座，也不只掉最高一小段；
8. 没有 `BuildingGateRejected`、验证残留隐藏模块或正式建筑瞬移。

本次 PIE 证明 Single/Dual/Seam 的 Drop/Tip/SlideThenTip 机械语义可见、完整态与稳定性无误。验收同时暴露两项表现结论：

- 原诊断 HISM/TextRender 附着在生成器 Root 上，建筑坍塌后会留在初始世界位置；`8a1aab8` 已统一设置 `HiddenInGame=true`，编辑器仍可读，PIE/游戏不再渲染，并由 `PatternMatrixPlanarIdle` 防回归；
- 三 Pattern 当前共享“下部主体—狭窄失效接口—上部主体”的近似外壳。DAG-4 只认证弱点相对普通点的真实机械收益，不以当前 Fixture 冒充最终建筑/弱点多样性；六栋视觉去重、弱点高度/侧向/构件类型分布和最终生产启用转交 DAG-5/WFC。

## 14. 实际实现文件

- `Source/ABTSRuntime/Public/Building/ABTSM73DAGFailureFrontierTypes.h`
- `Source/ABTSRuntime/Public/Building/ABTSM73DAG4Types.h`
- `Source/ABTSRuntime/Public/Building/ABTSM73BuildingTypes.h`
- `Source/ABTSRuntime/Public/Building/ABTSM73StableBuildingActor.h`
- `Source/ABTSRuntime/Public/Game/ABTSM7GameMode.h`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAG4SettledContactValidator.h/.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAG4TrialPlanner.h/.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAG4ResponseEvaluator.h/.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAG4RuntimeValidation.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAG4AutomationTests.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAG4RuntimeAutomationTests.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73DAGLoadSupportSolver.cpp`
- `Source/ABTSRuntime/Private/Building/ABTSM73StableBuildingActor.cpp`
- `Source/ABTSRuntime/Private/Game/ABTSM7GameMode.cpp`

## 15. 2026-07-29 实现与证据

源码提交：

- `ec55143`：settled OBB Contact DAG、Frontier/COM/Hull/Tip/Reseat 复验、确定性普通点规划、Shadow Island、真实 Chaos 指标归约、原子接受/拒绝、默认关闭路由与纯数据/运行时测试；
- `9bb8fd2`：补齐木、石、铁、玻璃四种真实 MaterialSystem Profile 的独立运行时矩阵；
- `8a1aab8`：弱点、支点、受影响主体、方向箭头和 Pattern 标签保留编辑器诊断，但统一在 PIE/游戏隐藏；同时增加 Game World 属性回归。该提交是 DAG-4 阶段源码 tip。

动态认证反向暴露并修正了两个生成期缺口：

- Dual/Seam 原 `W/P` 围绕合力点对称，移除 `P` 与移除 `W` 一样会倾覆；现在 `W` 覆盖累计合力，`P` 位于预期失效方向反侧，移除 `P` 保持稳定，移除 `W` 才产生目标 Tip/Seam；
- Single 的底层普通 Tripod 柱 `Node8` 原本也是第二机械弱点，移除后真实坍塌质量比为 `0.761`；现在 Single transaction 的普通 Tripod 围绕累计合力紧凑布置，并以逐柱 N-1 Hull 裕量和跨组净空 fail closed，正式对照中最强普通点坍塌质量降为 `0.000`。

构建：

- 强制 Unity 命令：`Build.bat AngryBirdsToSpaceEditor Win64 Development -WaitMutex -NoHotReload -NoHotReloadFromIDE -ForceUnity -DisableAdaptiveUnity`；
- 最终源码状态 4 actions 编译、链接与 Metadata 全部成功；
- 使用 `-NoHotReloadFromIDE` 前已审计所有 Unreal Editor 进程，确认活动 Editor 均属于其他工作树、没有进程加载本工作树项目或 DLL；这是 UE 5.8 全局 Live Coding mutex 下按 [多工作树协作规范](ABTSMultiWorktreeDevelopmentGuide.md) 批准并记录的单次例外，不是常规构建默认。

fresh `UnrealEditor-Cmd -NullRHI`：

- `ABTS.M73DAG4.`：6/6 Success，报告 `Saved/Automation/DAG4-Final2-20260729-200808/index.json`，日志 `Saved/Logs/DAG4-Final2-20260729-200808.log`；
- 三 Pattern 每栋均为 `Settled=1 Comparison=1 Accepted=1 Trials=4`，正式模块计数 `35 → 35`，Shadow 清理为零；Single/Dual/Seam 弱点相对最强普通点分别为 `15.542× / 1.663× / 1.812×`；
- 四材料分别在独立 Physics World 中为 `Accepted=1 Trials=4`：Wood `10.835×`、Stone `10.837×`、Iron `10.835×`、Glass `10.827×`，且所有正式 Node 始终保留请求材料；
- `ABTS.M73DAG3.`：22/22 Success，日志 `Saved/Logs/DAG3-Final-20260729-195457.log`；
- 旧 `ABTS.M73DAG.`：10/10 Success，日志 `Saved/Logs/DAG-Final-20260729-195544.log`；
- `ABTS.M7`：43/43 Success，包含 DAG-4 六项、M73B/B2 与 TaskGraph 路由，日志 `Saved/Logs/M7-Final2-20260729-200910.log`；
- `ABTS.Contracts.WorldGeneration`：2/2 Success，日志 `Saved/Logs/WorldContract-Final-20260729-195804.log`。

诊断显示修复后的增量证据：

- 强制 Unity Development Editor 全链接成功（5 actions），未使用 Hot Reload；
- `ABTS.M73DAG3.Chaos.PatternMatrixPlanarIdle`：1/1 Success，日志 `Saved/Logs/DAG3B-DiagnosticVisibility-20260729-212103-FreshAutomation.log`；
- `ABTS.M73DAG4.`：6/6 Success，日志 `Saved/Logs/DAG4-DiagnosticVisibility-20260729-212150-FreshAutomation.log`；
- `ABTS.M7`：43/43 Success，日志 `Saved/Logs/M7-DiagnosticVisibility-20260729-212237-FreshAutomation.log`。

DAG-4 至此完成。它证明当前 Fixture 的动态弱点合同，不授权把三套相似外壳用于最终六栋生产建筑；A/B/C/DAG-4 生产默认继续关闭，等 DAG-5/WFC 同时提供语义包络、候选 Novelty 和弱点布局差异后，再用本阶段验证器逐栋重认证。
