# ABTS 项目工作流与开发入口

> 编码：UTF-8，简体中文。
>
> 用途：这是后续对话与开发的**轻量入口**。它只维护项目约束、当前阶段、下一步和文档索引；玩法细节、参数、编辑器操作、验收与排错必须链接到对应详稿，不能复制到本文。

## 1. 当前状态

- 项目形态：UE 5.8 C++ 项目；运行时代码主模块为 `Source/ABTSRuntime`。
- 既有集成基线：M1 至 M10 的球面、Task Graph PCG、鸟群、物品/放置、弹弓、Chaos 破坏、建筑、桥梁、卫星与侦察系统均已进入工程；以实际源码和对应设计稿为准。
- 当前验收项：M10.1-A（强化弹弓小地图轨迹与红色落点）和 M10.1-B（远端落点画中画）均已完成 PIE 验收；M10.1-C（星球尺度拟合截面轨道全景图）已完成 C++ 与编译，待 PIE 视觉验收。
- 默认下一步：按 M10.1-C 详稿完成 PIE 视觉/交互验收并回写结果。完整目标选择、影响事件标签与引力走廊属于 M10.1-D，尚未授权实现。

当前阶段父级入口为：[M10.1 超视距目标与引力走廊](M101BeyondHorizonLaunchInterfaceDesign.md)；当前实现与交接详稿为：[M10.1-C 星球尺度拟合截面轨道全景图](M101COrbitalOverviewDiagramDesign.md)。

## 2. 不可违反的项目约束

1. `CellTopo` 永远是球面 Gameplay 的逻辑源：地形类型、道路、水网、桥址、建筑/资源逻辑与可达性均由它或 Task Graph 派生；连续球面只负责渲染与碰撞表现。
2. M6 的实际飞行与碰撞是权威物理链路；HUD、小地图、远端画中画和后续走廊只能消费其只读预测快照，不能维护第二套积分器。
3. 四鸟正式移动路线为 Chaos 刚体碰撞；旧 `ForceSuspension` 与 `LegacySweep` 仅保留为历史对照。
4. 新功能按独立职责拆分 C++ 类；单个源文件接近 600 行时优先拆分。编辑器可调值必须以带注释的 `UPROPERTY` 暴露，不把配置硬编码进 HUD 或临时脚本。

详细依据：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [Chaos 刚体移动](ChaosRigidBodyMovementDesign.md)。

## 3. 后续对话的标准开发流程

1. 先阅读本文、[主设计稿](AngryBirdsToSpaceGameDesign.md)和当前阶段详稿；随后读取真实 `.h/.cpp`，不以旧摘要猜测字段或生命周期。
2. 明确本次只实现的范围、暂不实现内容、编辑器接口和验收标准；新阶段写独立详稿，并在主设计稿/本文中只补链接与状态。
3. 按 `Game / Terrain / Party / Player / Slingshot / Building / World / UI` 的现有职责边界实现；保留用户已有改动与历史对照路径。
4. 完成后先编译 `AngryBirdsToSpaceEditor Win64 Development`，再进行 PIE 视觉/交互验收；仅“编译成功”不等于视觉验收通过。
5. 有问题时先以本次运行日志、截图或 Standalone 结果定位；修复后把可复现的问题沉淀到[开发排错记录](DevelopmentTroubleshooting.md)，而不是把排错过程堆入本文。
6. 阶段验收后只更新本文的“当前状态/默认下一步”和对应链接；不要把设计细节复制进本文。

## 4. 文档索引

| 领域 | 优先阅读的详稿 |
| --- | --- |
| 总体玩法与阶段状态 | [主设计稿](AngryBirdsToSpaceGameDesign.md) |
| 球面基础与移动 | [M1 独立入口](M1IndependentEntryDesign.md) · [M2 球面](M2PlanetSurfaceDesign.md) · [M2.5 径向引力/跳跃](M25RadialGravityAndJumpDesign.md) · [Chaos 刚体移动](ChaosRigidBodyMovementDesign.md) |
| PCG 与表现 | [Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [M3 地形表现/HISM](M3TaskGraphTerrainPresentationDesign.md) |
| 鸟群、镜头与 UI | [M4 鸟群实现](M4BirdPartyImplementationDesign.md) · [M4 球面镜头](M4MultiCharacterOrbitCameraDesign.md) · [UI 系统](UISystemDesign.md) |
| 物品、放置与通行 | [M5 背包/加工](M5InventoryCraftingImplementationDesign.md) · [M5.1 世界物品/弹弓装配](M51WorldItemsPlacementSlingshotDesign.md) · [M5.2 碰撞/移动](M52CollisionAndMovementDesign.md) · [M8 自动回收/桥梁](M8AutoRecoveryAndBridgesDesign.md) |
| 发射与物理破坏 | [M6 发射/碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M6 视觉表现](M6SlingshotVisualPresentationDesign.md) · [物理破坏调研](PhysicsImpactDestructionResearch.md) |
| 建筑与测试台 | [M7 材料/装置](M7BuildingMaterialsAndDevicesDesign.md) · [M7 球面集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md) · [M7.3 DAG 总路线](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) |
| 卫星、侦察与超视距发射 | [M9 卫星](M9SatelliteGravityDesign.md) · [M10 侦察小地图](M10ScoutMinimapDesign.md) · [M10.1 发射界面总设计](M101BeyondHorizonLaunchInterfaceDesign.md) · [M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md) |
| 资产与排错 | [Low Poly/AI 资产流程](LowPolyAssetProductionAndAIReportWorkflow.md) · [开发排错记录](DevelopmentTroubleshooting.md) |

## 5. 当前验收与交接清单

### M10.1-C PIE 验收与交接

1. 以青翎完成一次侦察；强化弹弓 `Pulling` 的路径达到可调阈值，或落点离开主视图/被主星遮挡时，确认圆图自动出现，不要求落点进入侦察圆。
2. 确认圆图位于侦察圆下方、物品 HUD 左侧相邻区域，不遮挡弹弓；改变窗口尺寸后仍在安全区。
3. 确认弹弓在图左侧、完整预测轨迹始终进入圆图安全边距；轨迹变长时自动拉远，主星允许被裁切。
4. 调整会产生三维偏转的发射方向，确认全部轨迹点投影连续，截面不无故左右/上下翻转。
5. 确认主星只显示基础正球与世界绝对经纬网，不显示 HISM、SDF、高程、道路、建筑、资源或侦察弧；卫星不显示经纬网。
6. 确认原始三维轨迹位于主星或卫星后方时为虚线，前方为实线，切换处稳定。
7. 松开发射或使预测失效，确认圆图当帧退出；核对没有新增 SceneCapture、RenderTarget 或 HUD 积分器。

严格参数、数学、验收与排错见：[M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md)。父级边界见：[M10.1 发射界面总设计](M101BeyondHorizonLaunchInterfaceDesign.md)；上游同源数据见：[M6 发射/碰撞](M6SlingshotLaunchAndImpactDesign.md)与[M9 卫星](M9SatelliteGravityDesign.md)。

## 6. 本文维护规则

- 本文不记录类字段、算法推导、完整操作步骤、长篇调研或问题细节；这些内容只进入对应详稿或排错记录。
- 新增阶段时：新增/更新详稿 → 在主设计稿建立链接 → 在本文更新一行阶段状态和默认下一步。
- 若本文与源码、主设计稿或阶段详稿冲突，优先检查真实源码；确认后修正文档，不依据本文覆盖实现。
