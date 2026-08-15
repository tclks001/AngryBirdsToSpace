# ABTS 项目工作流与开发入口

> 编码：UTF-8，简体中文。
>
> 用途：这是后续对话与开发的**轻量入口**。它只维护项目约束、当前阶段、下一步和文档索引；玩法细节、参数、编辑器操作、验收与排错必须链接到对应详稿，不能复制到本文。

## 1. 当前状态

- 三渲二非 M7 材质基线已形成；T4-A0/A1、T4-A2.1～A2.4 与 T4-A3.1/A3.2 均为 `IntegrationAccepted`。当前 T4-A3.3 已完成唯一 M11 环境来源租约、歧义 fail-closed、来源销毁与世界 teardown/cleanup 的幂等恢复；UE 5.8 ForceUnity 和 fresh NullRHI `T4A3_` 4/4 已通过，阶段保持 `ImplementationComplete（VisibleValidationPending）`，待终局退出/失败/PIE Stop 可见恢复验收后进入 T4-B。云场、PIP、高空星空与详细证据见 [T4 球面环境与光照](ABTSToonStylizedRenderingT4.md)。

- 项目形态：UE 5.8 C++ 项目；运行时代码主模块为 `Source/ABTSRuntime`。
- 既有集成基线：M1 至 M10 的球面、Task Graph PCG、鸟群、物品/放置、弹弓、Chaos 破坏、建筑、桥梁、卫星与侦察系统均已进入工程；以实际源码和对应设计稿为准。
- 当前验收项：M10 初版已全部完成验收，其中 M10.1-A/B/C 均已通过 PIE；M10.1-D 的通用目标选择与引力走廊不属于本次已验收初版，继续延期。
- M3：DDL 评审路线已切换为 `JuryDemoFixedSixV1`。M3 J1/J2 已冻结 World Seed `312503`、Candidate `4`、六条 E1–E6 放置和 Layout Hash `0x8AB8D7E4F094072D`；Integration J3 候选已完成加法式 DTO 与原子导出，ForceUnity、WorldGeneration 2/2、M3 Fixed-Six 2/2、FinaleSeparation 1/1 和 M7 Stage 4.5 1/1 均通过，状态为 `ContractReady / IntegrationPending`。旧 R4 完整 Witness、R6 泛化 Profile 选择与 R7 全 Seed 认证保留为后续项，不再阻断固定六建筑交付。
- M7：固定六栋的 Stage 4.5 放置描述已冻结，Manifest Hash `2324068295`、Catalog Hash `13889440156022460967`；这只证明 PlacementReady，不证明 ChaosReady。普通 TaskGraph 三建筑仍走 DAG2.3；固定评审路线下一步是 J4 精确消费，不再等待通用 Profile Catalog、Weakness 或全 Seed 可行性。
- M11：v1 的 M11.0/A/B/C 是生产基线；A/B/C v2.1 的 Core、Editor-only 候选和交互表现已进入 `master`。M3R-5.2 道路末端帧与 M5.1 双槽、M11 3+1 表现已在 `L_ABTS_M11` 完成自动化、fresh NullRHI 与 Visible PIE，接缝为 `IntegrationAccepted`。集成工作树已新增独立的 `PresentationAccepted v1` 稳定合同；它明确不等于 M11-B `StrictCertified`，当前冻结生产绑定仍为 `Unbound`，所以 Rank12 仍不能替换 v1 默认值。
- 当前下一步：待 J3 候选接收并进入 `master` 后，由 M7 合并该 `master`，实现 J4 的六条精确解析、静态注册与单关动态化；随后回到 Integration 执行 J5 fresh NullRHI 与一次完整 E1→E6 可见 PIE。该路线不恢复 Profile/Seed 搜索，也不以 J3 数据合同或 Stage 4.5 代替 Chaos 证据。T4-A3.3 可见恢复验收、M11 Rank12 表现扫描与严格拓扑认证继续按各自详稿并行维护，生产 Binding 在联合门通过前保持 `Unbound`。

当前入口：[Fixed-Six 世界生成合同](JuryDemoFixedSixWorldGenerationContract.md) · [M3 Fixed-Six 计划](M3JuryDemoFixedSixIntegrationPlan.md) · [M7 Stage 4.5 放置冻结](M73BeamStage45PlacementFreezeDesign.md) · [三渲二与全局风格化渲染](ABTSToonStylizedRenderingDesign.md) · [M11 v2](M11V2FinaleOptimizationDesign.md) · [多工作树规范](ABTSMultiWorktreeDevelopmentGuide.md)。

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
| PCG 与表现 | [M3 Fixed-Six 计划](M3JuryDemoFixedSixIntegrationPlan.md) · [Fixed-Six 世界生成合同](JuryDemoFixedSixWorldGenerationContract.md) · [M3R 月度地图改进](M3PCGMapImprovementPlan.md) · [Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [M3 地形表现/HISM](M3TaskGraphTerrainPresentationDesign.md) |
| 鸟群、镜头与 UI | [M4 鸟群实现](M4BirdPartyImplementationDesign.md) · [M4 球面镜头](M4MultiCharacterOrbitCameraDesign.md) · [统一镜头视觉优化](ABTSCameraVisualOptimizationDesign.md) · [UI 系统](UISystemDesign.md) |
| 物品、放置与通行 | [M5 背包/加工](M5InventoryCraftingImplementationDesign.md) · [M5.1 世界物品/弹弓装配](M51WorldItemsPlacementSlingshotDesign.md) · [M5.2 碰撞/移动](M52CollisionAndMovementDesign.md) · [M8 自动回收/桥梁](M8AutoRecoveryAndBridgesDesign.md) |
| 发射与物理破坏 | [M6 发射/碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M6 视觉表现](M6SlingshotVisualPresentationDesign.md) · [M6/M9 弹弓与卫星标定](M6M9SlingshotSatelliteCalibrationDesign.md) · [物理破坏调研](PhysicsImpactDestructionResearch.md) |
| 建筑与测试台 | [M7 球面生产集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) · [M7.3 DAG 总路线](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) · [DAG3-C 可玩候选](M73DAG3CAttackReachabilityAndProductionRoutingDesign.md) · [DAG-4 动态认证](M73DAG4SettledContactAndAttackRolloutDesign.md) · [DAG-5 六栋路线](M73DAG5CandidateSearchSemanticEnvelopeAndProductionDesign.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md) |
| 卫星、侦察与超视距发射 | [M9 卫星](M9SatelliteGravityDesign.md) · [M10 侦察小地图](M10ScoutMinimapDesign.md) · [M10.1 发射界面总设计](M101BeyondHorizonLaunchInterfaceDesign.md) · [M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md) |
| 终局轨道谜题 | [M11 算法预演](M11GravityAssistAlgorithmPrevisualization.md) · [M11 v2 优化总设计](M11V2FinaleOptimizationDesign.md) · [M3R-5.2/M11 Preview 集成](M3R52M11PreviewFinaleIntegrationDesign.md) · [M11-A/Core](M11AGravityAssistSolverDesign.md) · [M11-B v1 认证](M11BFinaleLayoutCertificationDesign.md) · [M11-B v2.1 候选搜索](M11B21CandidateSearchDesign.md) · [M11-C 交互与实飞](M11CFinaleInteractionAndPlaybackDesign.md) · [PresentationAccepted 稳定合同](M110PresentationAcceptanceContract.md) |
| 视觉风格、资产与排错 | [三渲二与全局风格化渲染](ABTSToonStylizedRenderingDesign.md) · [T2-A 主视图描边与共享语义契约](ABTSToonStylizedRenderingT2A.md) · [Low Poly/AI 资产流程](LowPolyAssetProductionAndAIReportWorkflow.md) · [开发排错记录](DevelopmentTroubleshooting.md) |

## 5. 当前验收与跨工作树交接基线

### 5.1 集成基线

- 当前 `master` 已通过 M3、M7、M11 累积候选的强制 Unity 集成编译；各阶段的精确自动化与 PIE 证据仍以对应详稿为准。
- M3 只拥有路线、Encounter、Biome、槽场和站点等逻辑布局；M7 只拥有建筑候选、Profile、实体生成与建筑物理；M11 只消费 Finale Frame，不读取 M3 原始数组。
- 稳定合同、M5.1/M6/M9、共享物品与默认绑定、主地图和共同 PIE 均由集成工作树修改和验收。

### 5.2 进度卡点矩阵

| 需求 | 已完成生产者 | 尚缺消费者/前置 | 当前阻断门 |
| --- | --- | --- | --- |
| M3R-3.1 普通槽场 | M3 已生成候选槽场；Integration 已实现最小快照消费接缝、DirtHole 批回滚、M6 三维净空与失败原子状态，装配 2/2、槽 Actor 1/1，兼容世界 PIE 已通过 | M3R-4/R-6 产出唯一 Candidate、最终 `LayoutHash` 和正式导出；Integration 再绑定生产入口 | 未接受唯一月度 Candidate 前不得读取 `RetainedCandidates[0]`；月度实体槽与六关联合 Visible PIE 未通过前保持 `IntegrationPending` |
| M3R-4 六关 Ballistic Witness | M3 已有六 Encounter、攻击走廊和候选空间；Integration 已冻结 M6/M9 Launch/Preset V0，普通 M6 三档实飞已消费同一目录并公开非零 `LaunchProfileHash`，生产消费 1/1 与标定 6/6 已通过 | Integration 继续提供实际 pouch/camera frame 与 M9 引力查询适配器；M7 提供已认证 ProfileDescriptor 目录 | 标定积分器不是生产 Witness 权威；只有 Launch/Preset 身份可跨 Seed，`GravitySnapshotHash` 仅为场景实例证据。实际 frame/M9 适配器或 M7 目录任一未就绪时，M3 仍为 `IntegrationPending` |
| JuryDemo Fixed-Six 六栋建筑 | M7 Stage 4.5 已冻结六条放置描述；M3 J1/J2 已冻结单一 Seed/Candidate、六个 Pad 与 Layout Hash；Integration J3 候选已通过 ForceUnity 与四组 fresh 自动化，以加法式 DTO 发布精确身份 | 候选进入 `master` 后，M7 J4 按 Entry/Tier/Seed/Descriptor Hash 精确消费，六栋静态注册、仅当前 Encounter 动态化；Integration 再做 J5 | J3 数据合同不等于实体注册或 ChaosReady；J4/J5 未通过前保持 `IntegrationPending`，禁止回退 Profile/Seed 搜索 |
| M3R-5 Biome/Envelope 表现 | M3R-3 已提供逻辑结果 | 仅需 M3 自有表现消费 | 可与共享接口工作并行，但最终须在六关世界重新做性能与 PIE |
| M11 v2.2 与 M11-D | M11 v2.1 Core、候选与交互已完成；Integration 已建立 `PresentationAccepted v1` 稳定合同，Rank12 的严格 v3 扫描已有早停证据 | M11 先实现完整表现兼容扫描并提交 Rank12 Manifest；严格拓扑认证可独立继续；M11-D 的 Party、环境和共享资产由 Integration 接线 | `PresentationAccepted` 不授予 `StrictCertified`；当前生产 Binding 为 `Unbound`，未完成全域证据、联合构建和可见 PIE 前不得进入生产 |

### 5.3 固定交接顺序

1. M7 Stage 4.5 与 M3 J1/J2 的冻结身份先在候选分支对齐；只接受交接的精确提交，不从移动中的分支尖端猜测内容。
2. Integration J3 发布兼容旧 `Sites` 的 `JuryDemoFixedSixV1` DTO，并对缺失、重复、乱序和 Hash 漂移原子 fail closed。
3. J3 通过联合合同门并进入 `master` 后，M7 工作树只合并该 `master`，在自有文件中完成 J4；不得修改稳定合同或从 M3 内部数组重建 Seed/Entry。
4. Integration 合并 M7 J4 精确提交后执行 J5：六栋静态注册、单关动态化、失败注入、WorldReady 时序与完整 E1→E6 fresh 可见 PIE。
5. 只有 J5 通过才将固定六建筑提升为 `IntegrationAccepted`；逐栋 `ChaosReady` 仍按实际证据记录。旧完整 Witness、全 Seed 与泛化目录继续作为 Deferred，不反向扩张本批次门槛。
6. M11、T4 与其他共享资产任务可按各自路线推进；进入共享地图、默认绑定或正式可见 PIE 时回到 Integration 串行调度。

## 6. 本文维护规则

- 本文不记录类字段、算法推导、完整操作步骤、长篇调研或问题细节；这些内容只进入对应详稿或排错记录。
- 新增阶段时：新增/更新详稿 → 在主设计稿建立链接 → 在本文更新一行阶段状态和默认下一步。
- 若本文与源码、主设计稿或阶段详稿冲突，优先检查真实源码；确认后修正文档，不依据本文覆盖实现。
