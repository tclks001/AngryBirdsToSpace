# ABTS 项目工作流与开发入口

> 编码：UTF-8，简体中文。
>
> 用途：这是后续对话与开发的**轻量入口**。它只维护项目约束、当前阶段、下一步和文档索引；玩法细节、参数、编辑器操作、验收与排错必须链接到对应详稿，不能复制到本文。

## 1. 当前状态

- 项目形态：UE 5.8 C++ 项目；运行时代码主模块为 `Source/ABTSRuntime`。
- 既有集成基线：M1 至 M10 的球面、Task Graph PCG、鸟群、物品/放置、弹弓、Chaos 破坏、建筑、桥梁、卫星与侦察系统均已进入工程；以实际源码和对应设计稿为准。
- 当前验收项：M10 初版已全部完成验收，其中 M10.1-A/B/C 均已通过 PIE；M10.1-D 的通用目标选择与引力走廊不属于本次已验收初版，继续延期。
- 当前阶段：M11.0 已完成用户 PIE 验收；M11-A 无 World/Actor 的纯数据求解器、Editor 编译及全新进程 `ABTS.M11A` 自动化已完成；M11-B 局部布局 C++、Development Editor 编译、冻结预设以及全新进程 Unit/Runtime/ConstructiveSearch/FullInputDomain 自动认证均已完成，待用户 PIE 验收。普通 TaskGraph 建筑继续以 M7.3-DAG2.3 为生产链路。
- 默认下一步：按 [M11-B](M11BFinaleLayoutCertificationDesign.md#12-pie-验收)完成布局实例化、位置/可见性、非阻挡、M9 排除、重复进入与 fail-closed PIE 验收；该项通过前不转入 M11-C。

当前阶段父级入口为：[主设计稿的 M11 终局阶段](AngryBirdsToSpaceGameDesign.md#1-概念与终局)；已完成实施稿为：[M11.0 终局前置收口](M110PreFinaleClosureDesign.md)与 [M11-A 纯数据求解器](M11AGravityAssistSolverDesign.md)；当前实施稿为：[M11-B 局部布局与全输入域认证](M11BFinaleLayoutCertificationDesign.md)；总设计为：[M11 终局三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md)。

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
| 建筑与测试台 | [M7 材料/装置](M7BuildingMaterialsAndDevicesDesign.md) · [M7 球面 DAG2.3 生产集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md) · [M7.3 DAG 总路线](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) · [DAG-2 编译](M73DAG2SpatialLayoutAndModuleCompilationDesign.md) · [DAG2.3 联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md) |
| 卫星、侦察与超视距发射 | [M9 卫星](M9SatelliteGravityDesign.md) · [M10 侦察小地图](M10ScoutMinimapDesign.md) · [M10.1 发射界面总设计](M101BeyondHorizonLaunchInterfaceDesign.md) · [M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md) |
| 终局轨道谜题 | [M11.0 终局前置收口](M110PreFinaleClosureDesign.md) · [M11 算法预演](M11GravityAssistAlgorithmPrevisualization.md) · [M11-A 纯数据求解器](M11AGravityAssistSolverDesign.md) · [M11-B 局部布局与全输入域认证](M11BFinaleLayoutCertificationDesign.md) |
| 资产与排错 | [Low Poly/AI 资产流程](LowPolyAssetProductionAndAIReportWorkflow.md) · [开发排错记录](DevelopmentTroubleshooting.md) |

## 5. 当前验收与交接清单

### M11.0 前置收口交接

1. M3 GeneratorVersion=3、默认最多 16 个确定性 Attempt；`SatelliteWindow` 与最终认证 `LaunchSite` Anchor 的球面角距必须达到配置值，默认 `55°`，结果写入 PCG Summary。
2. M3 只生成 BuildingSpawnSite/Pad；三个普通建筑由 M7 Resolver 强制进入 `RecursiveSupportDAG`。日志必须 `Algorithm=1`、`DAGMacro>0`、`DAGSparse>0`、`DAGHash!=0`，Furnace 必须另有 `DAGMinContact=0.060`，不得回退 Legacy。
3. 旧 Blueprint CDO 的 `Algorithm=0` Profile 必须在 M7 边界升级；`LaunchSite` 保留平整/净空施工台，但默认 Profile 和运行时循环都拒绝该 Task 的建筑。
4. 同一次 PIE 中必须先有 `BuildingContractSealed Expected=3 Registered=3 SetupRejected=0`；三栋普通建筑零穿透、逐栋 `IdleValidation Accepted=1`，且全部通过先于 `WorldReady=1`。StartupPhysics 不能提前冻结验证中的模块；任一 Actor Reject/NotRequired、Setup Reject、必需数量不符或 `Accepted != Expected` 都必须阻止 WorldReady/发射，且门禁不受 HISM Startup Warmup 开关影响。合同激活时只核验逐引用注册的必需集合，不让无关测试 Actor 干扰生产验收。`-benchmark` 固定时间步只能作为算法回归，不能替代实时 PIE；建筑物理门还需不带 `-benchmark` 的实时 30/60/120 FPS、D3D12 fresh game 与可见 PIE/hitch soak。
5. 全图只有一对 Space-only 槽：共享认证 AnchorCell/SlotPairId，Side 为 Left/Right，首版世界中心距 `210cm`。
6. 现行终局配方为 `SpaceStakePair -> SpaceStake ×2`（金属 6、木材 5）和 `SpaceCord`（金属 2、晶体 1）；旧 `SpaceSlingshotPart` 只保留隐藏枚举兼容值。
7. M9 卫星锚定 `SatelliteWindow`，相对终局 Frame 通过距离比例 `>=0.80` 和侧轴对齐点积 `>=0.98`；强化弹弓仍消费 M9 引力。
8. `FABTSM110FinaleLocalFrame` 以槽中点为原点，X=Forward、Y=左→右/卫星切向、Z=径向 Up；正式布局不得硬编码世界坐标。
9. `FABTSM110FinaleGravityScenario` 固定且只包含 Primary+Assist1+Assist2+Assist3；M9 卫星没有角色入口，M11 每帧不得扫描 World 或调用 M9 引力。
10. 编译、4 项 M11.0 自动化、DAG/M7 自动化、103 Seed 双生成验证、固定 Seed 独立进程基线与用户可见 PIE 视觉/交互验收均已通过。

### M11-A 纯数据求解器交接

1. `FABTSM11GravityScenario` 仍固定为主星、①、②、③和一个非引力 UFO；求解器无 World/Actor/Chaos/帧率/随机数入口。
2. SolverVersion 1 使用双精度 velocity-Verlet、`1/120 s` 基础步长和固定最大二分细分；只有当前期望助推行星在平滑作用圈内施力，细分/步数预算耗尽稳定失败。
3. 遭遇按 `InfluenceEnter → ReferenceEnter → Closest → ReferenceExit → InfluenceExit` 推进；只擦 Influence 而未进 Reference 不计为完成助推；自然克隆拟合双曲线渐近方向并冻结 B-plane/走廊，三次确定性归一化后仅沿当前速度方向换能。
4. 当前行星直到 Influence 出口才失活，保证完整淡出外壳入/出对称；自然克隆不预先提交未来物理终止，碰撞、UFO、错序、作用圈、最近点和时限仍由权威轨迹按时间裁决，结果进入规范 64 位验证 Hash。
5. `ABTS.M11A` 8/8 已在全新 `UnrealEditor-Cmd -NullRHI` 进程通过；包括完整外壳能量守恒、渐近线/理想转角、非饱和正负换能、出站分布核、走廊/飞越侧、三位 Mask、qualified-target/独立 TargetContact、Hash golden/敏感性/`±0`、同根及时限优先级、步长收敛、高速穿透和稳定失败。
6. M11-B 必须直接消费该 Request/Result API；测试夹具坐标、巨大走廊和裁剪值不是正式布局参数。

### M11-B 当前实施与验收入口

1. 当前 C++、Development Editor 编译与全新进程 Unit/Runtime/ConstructiveSearch/FullInputDomain 自动认证均已完成；正式参数、信赖域与各级 Hash 已冻结，只以最终代码和 [M11-B 认证报告](M11BFinaleLayoutCertificationDesign.md#92-v1-冻结认证报告)为准。
2. M11-B 必须完成完整 `Yaw × Pitch × Power` 基础域与半格偏移 `F4` 发现、最终精度自适应闭包、唯一 `F4`、局部前缀嵌套、三颗任一助推消融、旁路排除和信赖域重放；有限采样唯一性结论必须始终绑定冻结 Scan Contract。
3. 围绕完整域发现出的唯一 `F4` 族，最终局部精化闭包结果为 `F=(6244,1890,981,558)`、局部分量数 `(1,1,1,1)`、Hit `558`、Bypass `0`；F4 实心瞄准矩形为 `20×18 px`，覆盖 14 个连续 Power 切片。该结论是冻结有限离散扫描合同下的工程证明，不是连续实数域的数学证明，也不宣称全域 `F1/F2` 微拓扑唯一。消融的高精度阶段只复核完整掩码已发现族周边，完整域层面仍以 base + half-cell 发现合同为准。
4. 运行时只能从认证预设和 Finale Frame 一次性编译固定四体 Scenario，并恰好实例化三颗表现行星和一个非引力 UFO；终端 `HitRadius=16000 cm` 是合格拦截包络，实际 UFO 使用更远端独立中心与 `800 cm` 几何接触球。行星③增强虚拟动量及终端 `Q>=0.95` 均属于冻结身份；不得搜索 World、接入 M9 或让 Mesh/World 碰撞成为轨迹权威。
5. 全新进程自动认证和 M11-A/M11.0 回归均已通过；还必须按 [M11-B 详稿](M11BFinaleLayoutCertificationDesign.md#12-pie-验收)完成位置、可见性、非阻挡、M9 排除、重复进入和 fail-closed PIE 验收。Editor PIE 初始化成功后会一次性 persistent 绘制三颗 Influence、24k Approach、16k qualified intercept 与 800 cm physical UFO 诊断线框；它们不是 Actor/碰撞/求解权威，Commandlet/packaged 不绘制。M11-B C++ 没有迁移 `Content/Maps/Test.umap`；PIE 前需让实际地图/GameMode Blueprint 接入 M11 GameMode/Finale System。
6. M11-C 只消费认证 Preset/Result/Trust Regions 及 M11-B 冻结的 nominal Physical Playback；后者从原始 Pouch 状态用同一求解器抵达 800 cm 几何 UFO，不是从合格拦截球内部续算，也不代表任意 F4 输入都会物理接触。M11-C 必须单独冻结玩家 Release 到成功演出的连续接管，不得瞬移、隐藏吸向标准答案或改用 Chaos。完整交接清单见 [M11-B 第 15 节](M11BFinaleLayoutCertificationDesign.md#15-m11-c-交接清单)。在第 5 项完成前，默认下一步仍是 M11-B，不提前切换阶段。

### M11 已冻结的下游门槛

1. 开放完整 `Yaw × Pitch × Power`；行星 ① 的距离形成接近最大功率的连续门槛。
2. 三颗助推行星/UFO 使用相对太空弹弓的局部布局预设，不使用手工地图或绝对坐标。
3. 使用可见、可退出的前缀成功集稳定器，不使用隐藏硬吸附；四鸟同袋并沿一条预计算轨迹编队。
4. 完整 `Yaw × Pitch × Power` 输入域只允许一个连通 `F4` 成功分量；围绕该族的局部前缀嵌套、助推消融和旁路排除也必须通过，这是正式阻断性验收门槛。
5. 同一 World 切换星空并关闭雾云；失败播放到原因可读后黑屏恢复到点击弹珠袋前。

M11.0 的实现与验收见：[M11.0 终局前置收口](M110PreFinaleClosureDesign.md)。唯一积分器、冻结数值合同和自动化证据见：[M11-A 纯数据求解器](M11AGravityAssistSolverDesign.md)。当前布局、认证、Actor 边界、PIE 与 M11-C 交接见：[M11-B 局部布局与全输入域认证](M11BFinaleLayoutCertificationDesign.md)。HUD、稳定器、实飞与完整终局路线见：[M11 算法预演](M11GravityAssistAlgorithmPrevisualization.md)。已验收上游表现见：[M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md)。

## 6. 本文维护规则

- 本文不记录类字段、算法推导、完整操作步骤、长篇调研或问题细节；这些内容只进入对应详稿或排错记录。
- 新增阶段时：新增/更新详稿 → 在主设计稿建立链接 → 在本文更新一行阶段状态和默认下一步。
- 若本文与源码、主设计稿或阶段详稿冲突，优先检查真实源码；确认后修正文档，不依据本文覆盖实现。
