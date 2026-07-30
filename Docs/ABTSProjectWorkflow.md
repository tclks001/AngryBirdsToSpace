# ABTS 项目工作流与开发入口

> 编码：UTF-8，简体中文。
>
> 用途：这是后续对话与开发的**轻量入口**。它只维护项目约束、当前阶段、下一步和文档索引；玩法细节、参数、编辑器操作、验收与排错必须链接到对应详稿，不能复制到本文。

## 1. 当前状态

- 项目形态：UE 5.8 C++ 项目；运行时代码主模块为 `Source/ABTSRuntime`。
- 既有集成基线：M1 至 M10 的球面、Task Graph PCG、鸟群、物品/放置、弹弓、Chaos 破坏、建筑、桥梁、卫星与侦察系统均已进入工程；以实际源码和对应设计稿为准。
- 当前验收项：M10 初版已全部完成验收，其中 M10.1-A/B/C 均已通过 PIE；M10.1-D 的通用目标选择与引力走廊不属于本次已验收初版，继续延期。
- 当前阶段：M11 v1 的 M11.0/A/B/C 已形成可运行生产基线；M11-A v2.1 单一标准 C++ Core、M11-B v2.1 权威搜索与两个 Editor-only 候选、M11-C v2.1 交互/PIP/轨迹相机已完成实现、功能工作树验收和集成强制 Unity 编译。两个 v2.1 布局仍是 `Candidate / NOT CERTIFIED`，不能替换 v1 生产默认值。
- 并行进度：M3R-0 已进入集成基线，M7 DAG3-A 已进入生产代码；普通 TaskGraph 建筑仍由 M7.3-DAG2.3 生成，后续 M3/M7/M11 并行迭代统一遵守[多工作树协作与集成规范](ABTSMultiWorktreeDevelopmentGuide.md)。
- 默认下一步：从两个 v2.1 候选中明确冻结唯一体验方案，再执行 M11-B v2.2 完整输入域认证和 M11-C v2.2 正式绑定；之后进入 M11-D 四鸟、环境切换、UFO 破坏与救援演出。

当前入口：[M11 v2 优化总设计](M11V2FinaleOptimizationDesign.md) → [M11-B v2.1 候选搜索](M11B21CandidateSearchDesign.md) → [M11-C 交互与确定性实飞](M11CFinaleInteractionAndPlaybackDesign.md)。v1 基线与总路线见：[M11 算法预演](M11GravityAssistAlgorithmPrevisualization.md)。

## 2. 不可违反的项目约束

1. `CellTopo` 永远是球面地表 Gameplay 的逻辑源：地形类型、道路、水网、桥址、建筑/资源逻辑与可达性均由它或 Task Graph 派生；连续球面只负责渲染与碰撞表现。M11 的三颗助推行星和 UFO 只能由 `LaunchSite` 太空弹弓的终局局部布局预设生成，不保存绝对世界坐标，也不反向成为地表逻辑源。
2. M1–M10 的实际飞行与碰撞继续以 M6 为权威链路，HUD、小地图和画中画只能消费其只读预测快照。M11 Space 档必须由同一个专用固定步长求解器同时驱动预演与实飞，不能在 HUD、Chaos 或另一组件中复制第二套轨道积分。
3. 四鸟常规行走与 M1–M10 发射的正式移动路线为 Chaos 刚体碰撞；旧 `ForceSuspension` 与 `LegacySweep` 仅保留为历史对照。M11 Space 深空段由预计算的同源确定性运动接管，四鸟共享权威中心轨迹并组成固定队列，命中目标后才切回 Chaos/剧情表现。
4. 新功能按独立职责拆分 C++ 类；单个源文件接近 600 行时优先拆分。编辑器可调值必须以带注释的 `UPROPERTY` 暴露，不把配置硬编码进 HUD 或临时脚本。

详细依据：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [Chaos 刚体移动](ChaosRigidBodyMovementDesign.md)。

## 3. 后续对话的标准开发流程

1. 先阅读本文、[主设计稿](AngryBirdsToSpaceGameDesign.md)和当前阶段详稿；随后读取真实 `.h/.cpp`，不以旧摘要猜测字段或生命周期。
2. 明确本次只实现的范围、暂不实现内容、编辑器接口和验收标准；新阶段写独立详稿，并在主设计稿/本文中只补链接与状态。
3. 按 `Game / Terrain / Party / Player / Slingshot / Building / World / UI` 的现有职责边界实现；保留用户已有改动与历史对照路径。
4. 完成后先编译 `AngryBirdsToSpaceEditor Win64 Development`，再进行 PIE 视觉/交互验收；任何含源代码的功能合并都要在集成工作树重新编译，若交接指定集成自动化，则在编译成功后执行并核验结果。仅“编译成功”不等于视觉验收通过。
5. 有问题时先以本次运行日志、截图或 Standalone 结果定位；修复后把可复现的问题沉淀到[开发排错记录](DevelopmentTroubleshooting.md)，而不是把排错过程堆入本文。
6. 阶段验收后只更新本文的“当前状态/默认下一步”和对应链接；不要把设计细节复制进本文。

## 4. 文档索引

| 领域 | 优先阅读的详稿 |
| --- | --- |
| 总体玩法与阶段状态 | [主设计稿](AngryBirdsToSpaceGameDesign.md) |
| 并行开发与集成 | [M3/M7/M11 多工作树协作与集成规范](ABTSMultiWorktreeDevelopmentGuide.md) |
| 球面基础与移动 | [M1 独立入口](M1IndependentEntryDesign.md) · [M2 球面](M2PlanetSurfaceDesign.md) · [M2.5 径向引力/跳跃](M25RadialGravityAndJumpDesign.md) · [Chaos 刚体移动](ChaosRigidBodyMovementDesign.md) |
| PCG 与表现 | [Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [M3 地形表现/HISM](M3TaskGraphTerrainPresentationDesign.md) |
| 鸟群、镜头与 UI | [M4 鸟群实现](M4BirdPartyImplementationDesign.md) · [M4 球面镜头](M4MultiCharacterOrbitCameraDesign.md) · [UI 系统](UISystemDesign.md) |
| 物品、放置与通行 | [M5 背包/加工](M5InventoryCraftingImplementationDesign.md) · [M5.1 世界物品/弹弓装配](M51WorldItemsPlacementSlingshotDesign.md) · [M5.2 碰撞/移动](M52CollisionAndMovementDesign.md) · [M8 自动回收/桥梁](M8AutoRecoveryAndBridgesDesign.md) |
| 发射与物理破坏 | [M6 发射/碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M6 视觉表现](M6SlingshotVisualPresentationDesign.md) · [物理破坏调研](PhysicsImpactDestructionResearch.md) |
| 建筑与测试台 | [M7 材料/装置](M7BuildingMaterialsAndDevicesDesign.md) · [M7 球面 DAG2.3 生产集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md) · [M7.3 DAG 总路线](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) · [DAG-2 编译](M73DAG2SpatialLayoutAndModuleCompilationDesign.md) · [DAG2.3 联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md) |
| 卫星、侦察与超视距发射 | [M9 卫星](M9SatelliteGravityDesign.md) · [M10 侦察小地图](M10ScoutMinimapDesign.md) · [M10.1 发射界面总设计](M101BeyondHorizonLaunchInterfaceDesign.md) · [M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md) |
| 终局轨道谜题 | [M11 算法预演](M11GravityAssistAlgorithmPrevisualization.md) · [M11 v2 优化总设计](M11V2FinaleOptimizationDesign.md) · [M11-A/Core](M11AGravityAssistSolverDesign.md) · [M11-B v1 认证](M11BFinaleLayoutCertificationDesign.md) · [M11-B v2.1 候选搜索](M11B21CandidateSearchDesign.md) · [M11-C 交互与实飞](M11CFinaleInteractionAndPlaybackDesign.md) |
| 资产与排错 | [Low Poly/AI 资产流程](LowPolyAssetProductionAndAIReportWorkflow.md) · [开发排错记录](DevelopmentTroubleshooting.md) |

## 5. 当前验收与交接

### 已验收生产基线

- M11.0 已冻结终局局部 Frame、唯一 Space 槽、M9 隔离和四体边界；详见 [M11.0](M110PreFinaleClosureDesign.md)。
- M11-A v1 求解器、M11-B v1 唯一成功族认证及 M11-C v1 交互/确定性播放均已进入生产基线；数值、Hash、自动化和操作细节只查对应阶段详稿。
- 终局轨迹只由同源 M11 Core 驱动；表现 Actor、HUD、Chaos 和 M9 均不得成为第二轨迹权威。

### 当前 v2.1 状态

- M11-A v2.1 已收口为 UE/CLI 共用的标准 C++ Core。
- M11-B v2.1 已完成 4096-work 权威搜索，保留两个 Editor-only 候选；它们尚未经过 v2.2 完整域认证。
- M11-C v2.1 已接入候选比较、发射手感、轨道 HUD/PIP、发布缓存和 Release 后轨迹相机；本轮代码已完成工作树验收并通过集成强制 Unity 编译。

### 下一阻断门槛

1. 明确记录唯一获选候选及全部冻结体验参数。
2. M11-B v2.2 对该候选执行完整 `Yaw × Pitch × Power`、唯一 F4、旁路和助推消融认证。
3. M11-C v2.2 只绑定通过认证的 Bundle，并完成正式 PIE/Standalone；未认证候选不得进入生产默认值。
4. M11-D 再实现四鸟编队、星空/雾云切换、UFO 破坏、白鸟救援和完整成败演出。

## 6. 本文维护规则

- 本文不记录类字段、算法推导、完整操作步骤、长篇调研或问题细节；这些内容只进入对应详稿或排错记录。
- 新增阶段时：新增/更新详稿 → 在主设计稿建立链接 → 在本文更新一行阶段状态和默认下一步。
- 若本文与源码、主设计稿或阶段详稿冲突，优先检查真实源码；确认后修正文档，不依据本文覆盖实现。
