# AngryBirdsToSpace 游戏设计稿

> 状态：立项设计稿。编码：UTF-8，简体中文。
>
> 目标：在一个月内完成一款以 PCG 和物理为核心的第三人称 3D 小行星采集、建造与发射游戏；第一周完成中期评审可实际游玩的闭环。

## 0. 文档导航

本稿只维护全局玩法、阶段状态和跨系统约束；实现参数、编辑器步骤、验收与排错保留在对应详稿中。

> 新对话或阶段交接请先阅读：[项目工作流与开发入口](ABTSProjectWorkflow.md)。

- 入口与球面基础：[M1](M1IndependentEntryDesign.md) · [M2 CellTopo/连续球面](M2PlanetSurfaceDesign.md) · [M2.5 径向引力与跳跃](M25RadialGravityAndJumpDesign.md) · [Chaos 刚体移动（当前正式路线）](ChaosRigidBodyMovementDesign.md) · [力悬挂移动（历史对照）](ForceSuspensionMovementDesign.md)
- PCG 与地形：[M3R 月度地图改进](M3PCGMapImprovementPlan.md) · [Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [M3 地形表现与 HISM](M3TaskGraphTerrainPresentationDesign.md)
- 鸟群、相机与 UI：[鸟群跟随 Gameplay](BirdPartyFollowingGameplayDesign.md) · [M4 工程落地](M4BirdPartyImplementationDesign.md) · [M4 Orbit Camera](M4MultiCharacterOrbitCameraDesign.md) · [UI 系统](UISystemDesign.md) · [CuteBird 迁移与动画](CuteBirdMigrationAndAnimationDesign.md)
- 物品与世界交互：[M5 背包/加工](M5InventoryCraftingImplementationDesign.md) · [M5.1 世界物品/放置/装配](M51WorldItemsPlacementSlingshotDesign.md) · [M5.2 碰撞与移动](M52CollisionAndMovementDesign.md) · [M8 自动回收与桥梁](M8AutoRecoveryAndBridgesDesign.md)
- 弹弓与物理破坏：[M6 发射与碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M6 弹弓视觉](M6SlingshotVisualPresentationDesign.md) · [物理碰撞破坏调研](PhysicsImpactDestructionResearch.md)
- 建筑：[M7 球面 TaskGraph 集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md) · [M7.3 DAG 总路线](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) · [DAG2.3 联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md) · [DAG3-C 可玩候选](M73DAG3CAttackReachabilityAndProductionRoutingDesign.md) · [DAG-4 动态认证](M73DAG4SettledContactAndAttackRolloutDesign.md) · [DAG-5 六栋路线](M73DAG5CandidateSearchSemanticEnvelopeAndProductionDesign.md) · [建筑语义 WFC 调研](M73WFCBuildingEnvelopeAndDAGFittingResearch.md)
- 卫星、侦察与终局：[M9 卫星](M9SatelliteGravityDesign.md) · [M10 侦察小地图](M10ScoutMinimapDesign.md) · [M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md) · [M11 算法预演](M11GravityAssistAlgorithmPrevisualization.md) · [M11 v2 优化](M11V2FinaleOptimizationDesign.md) · [M11-A/Core](M11AGravityAssistSolverDesign.md) · [M11-B v1 认证](M11BFinaleLayoutCertificationDesign.md) · [M11-B v2.1 候选搜索](M11B21CandidateSearchDesign.md) · [M11-C 交互与实飞](M11CFinaleInteractionAndPlaybackDesign.md)
- 资产与工程参考：[Low Poly/AI 资产工作流](LowPolyAssetProductionAndAIReportWorkflow.md) · [开发排错记录](DevelopmentTroubleshooting.md)

## 1. 概念与终局

一只无颜色的白色 CuteBird 被外星人抓走，四只具有不同撞击特性的彩色小鸟被困在一颗程序化生成的小行星上。玩家沿主路推进、在预生成的弹弓槽中安装弹弓桩与弹弓弦，把同伴发射到远离道路的结构化建筑；建筑的物理连锁坍塌暴露并产出材料，小鸟自动回收材料。玩家据此加工更强的弹弓组件，利用球面地形、河网、桥梁和低轨卫星的引力走廊击溃更高价值目标，最终完成钢铁太空弹弓，通过三颗固定行星的连续引力弹弓命中遥远 UFO，并救回白色小鸟。

```text
沿主路发现弹弓槽
-> 组装简易弹弓
-> 发射鸟摧毁近处模块化建筑
-> 自动回收材料并加工强化组件
-> 借河谷、桥梁与卫星引力走廊击毁远端高价值建筑
-> 找到太空弹弓槽并完成钢铁太空弹弓
-> 精调一条依次经过三颗行星的终局轨迹
-> 命中 UFO，四鸟完成攻击并救回白色小鸟
```

本作不复刻既有弹弓游戏。鸟名、轮廓、动作、音效、任务叙事和美术资产均使用原创设计；核心是“在球面环境中弹射同伴解决采集与建造问题”。

最终发射必须同时是可解的轨道谜题和完整演出：玩家从全景轨迹图理解三次偏转与增能，在完整 `Yaw × Pitch × Power` 输入域内找到唯一连通路径族；四鸟从同一太空弹珠袋沿预计算权威轨迹组成队列，依次近掠三颗行星并命中 UFO。终局不切换地图；三颗助推行星和 UFO 由 Task Graph 世界中的太空弹弓局部布局预设生成。入口收口见 [M11.0](M110PreFinaleClosureDesign.md)，物理取舍、稳定器和验收门槛见 [M11 算法预演](M11GravityAssistAlgorithmPrevisualization.md)。

## 2. 设计支柱

| 支柱 | 玩家感受 | 实现约束 |
| --- | --- | --- |
| 弹射是主动作 | 弹弓是攻击建筑、投送鸟和读取弹道地形的工具。 | 每次组件升级都必须打开更远、更难或受引力影响的目标。 |
| 物理有后果 | 柱、梁、绳索、活塞和炸药会在撞击后连锁反应。 | 高价值材料必须从可读承重构件和结构坍塌中取得；严格内部弱点由后续 DAG-3 补齐。 |
| PCG 决定路线 | 主路、远路距离、河网、建筑、发射槽与卫星共同构成关卡。 | 固定 Seed 可复现；道路/水域/引力必须改变可攻击目标和最优弹道。 |
| 低学习成本 | 玩家总能知道下一种稀缺资源和下一项建造。 | 仅三次核心升级，不做复杂科技树。 |
| 终局有反差 | 地表阶段明亮亲和，离开时才显露宇宙。 | 普通探索不刻意强调太空视觉。 |

## 3. 资源、建筑与主循环

### 3.1 资源

| 资源 | 初始分布 | 大量来源 | 用途 |
| --- | --- | --- | --- |
| 树枝 | 全图低价值拾取 | 道路、河岸、废墟旁 | 蓝鸟临时弹弓桩、低级弹弓组件。 |
| 石料 | 全图低价值拾取 | 道路边、岩地、建筑残片 | 简易弹弓桩、桥梁维护、加工。 |
| 木材 | 建筑的结构/货仓产出 | 木屋、仓库、塔架 | 弹弓桩、弹弓弦、桥梁、熔炉。 |
| 金属部件 | 中价值建筑产出 | 碉堡、工坊、矿业设施 | 强化弹弓桩、活塞/结构配方。 |
| 晶体核心 | 高价值建筑产出 | 电厂、远端设施、卫星遗迹 | 能量弹弦、太空弹弓与终局。 |

树枝和石料可直接拾取，作为保证首轮组装的保底资源；木材、金属和晶体核心不以独立地面物体大量生成，主要由建筑坍塌暴露。高价值材料由当前发射鸟在飞行结束、掠过或落地后的自动回收范围写入全队库存，不要求玩家逐块走近拾取。

### 3.2 建造与配方

初版只有工作台、熔炉、预生成弹弓槽和可制作的桩/弦组件。弹弓不是可自由摆放建筑：一对槽位插入弹弓桩并连接弹弓弦后才成为完整弹弓。熔炉用于加工强化桩/弦以及 `SpaceStake ×2 + SpaceCord ×1`；旧的一体式太空弹弓部件配方退役，不引入燃料、电网、运输带或完整自动化工厂。

| 阶段 | 建造 | 建议材料 | 结果 |
| --- | --- | --- | --- |
| 0 | 树枝临时桩 | 1 树枝 | 蓝鸟在枝条槽位快速近射。 |
| 1 | 工作台 | 树枝 + 石料 | 开启桩、弦与桥梁配方。 |
| 2 | 简易弹弓 | 木桩 + 植物纤维弦 | 发射红鸟与黄鸟；桩距决定基础射程。 |
| 3 | 熔炉 | 石料 + 木材 | 加工金属部件和强化组件。 |
| 4 | 强化弹弓 | 金属桩 + 强化弹弦 | 发射黑鸟，并显示卫星引力走廊弹道。 |
| 5 | 钢铁太空弹弓 | 两根太空弹弓桩 + 一根太空弹弓弦 | 只能装入 LaunchSite 唯一专用槽，解锁终局集体发射。 |

首轮试玩的目标通关时间为 10 至 15 分钟；具体数量在试玩后调整。

## 4. 鸟群、第三人称与弹射

### 4.1 鸟群

| 鸟 | 初始状态 | 能力 | 物理结果 | 玩法职责 |
| --- | --- | --- | --- | --- |
| 红鸟 `绯翼` | 开局主控；简易弹弓 | 均衡冲击、稳定回收 | 对柱/梁/货仓的通用破坏与可靠落点。 | 基础建筑破坏、建造和通用弹道。 |
| 蓝鸟 `青翎` | 开局同行；树枝槽 | 以树枝作为临时弹弓桩立即近射、侦察/拍照 | 威力低，只能拆开近处松散材料或在建筑附近自动回收。 | 教学、近距离试射、发现道路外目标。 |
| 黄鸟 `棱喙` | 简易弹弓 | 高速穿透、木质结构伤害加成 | 更易切断木柱、拉索和木制货仓支撑。 | 木屋/仓库的高效破坏者。 |
| 黑鸟 `玄爪` | 强化弹弓 | 爆破冲击和范围推力 | 触发炸药桶、活塞、重物和金属结构的连锁坍塌。 | 中后期碉堡、电厂与卫星目标。 |

所有鸟都有捡拾能力，都可作为主控。

### 4.2 控制规则

1. 第三人称相机控制当前主控鸟。
2. `Tab` 循环切换主控鸟；HUD 提供鸟头像按钮，可用鼠标直接切换。
3. 非主控鸟在球面切向目标点上跟随主控鸟，再受径向重力贴地。
4. 发射、撞击建筑或距离过远时暂时脱队；飞行结束后自动回收附近已暴露的材料，再归队。
5. 初版不建设全球 NavMesh。跟随采用球面方向追踪、简单避障与最大距离保护；卡住的鸟以短距离飞跃/回收至安全位置并播放落羽特效。

鸟群跟随采用无环队列链而非所有鸟直接追逐主控：跟随目标来自前导鸟的 CellTopo 路径历史，舒适距离内不施加移动力，落后后才以 Arrival、路径约束和 Separation 组合追赶。跳跃沿路径传播 Jump Event，后排到达跳点且高度差确有需要时才依次跳跃；空中仅施加受限切向修正。M4 详细 gameplay 规则见 [BirdPartyFollowingGameplayDesign.md](BirdPartyFollowingGameplayDesign.md)。

M4 的 C++ 落地、头像/模型配置、HUD 操作和验收步骤见 [M4BirdPartyImplementationDesign.md](M4BirdPartyImplementationDesign.md)。弹弓在本阶段只保留四鸟资格查询接口，实际发射延后验收。

CuteBird 的 UE 5.1 → UE 5.8 安全迁移、Red/Blue/Yellow/Black 的外观预设映射、首版动画清单、Root Motion 约束和后续 Skeletal Mesh 表现层接口见 [CuteBirdMigrationAndAnimationDesign.md](CuteBirdMigrationAndAnimationDesign.md)。CuteBird 只替换鸟的表现层，不替代 ABTS 的角色物理、球面移动或鸟群逻辑。

小鸟之外的低模资产采购、AI 辅助生成、Blender 收口、弹弓桩/弦/袋的 Socket 与骨骼契约，以及赛后 AI 使用报告模板见 [LowPolyAssetProductionAndAIReportWorkflow.md](LowPolyAssetProductionAndAIReportWorkflow.md)。所有弹弓视觉资产都必须遵守“两桩、两段弦、中央弹丸袋”的结构；槽位与发射逻辑仍由 CellTopo Anchor Pair 决定。

M4 最终视角采用玩家持有的球面 Orbit Camera：相机方位独立于角色朝向，WASD 使用相机相对切向基准，RMB 调整环绕和俯视角，滚轮缩放；切换主控时保留 Orbit 状态并只平滑迁移注视锚点。详细方案与调研依据见 [M4MultiCharacterOrbitCameraDesign.md](M4MultiCharacterOrbitCameraDesign.md)。

### 4.3 弹弓

- 主路与指定发射台 Cell 预生成一对弹弓槽；将同级的两枚弹弓桩插入槽位并连接弹弓弦后，才进入发射模式。树枝槽只允许青翎快速近射。
- 两桩间的球面切线轴决定基础发射朝向，槽位间距、桩材质和弹弦等级决定安全蓄力/初速度上限；玩家拖拽或摇杆在限制范围内微调切线方向与蓄力长度。
- 预览显示近地抛物线、地形首次碰撞点；强化弹弓另显示低轨卫星引力走廊后的偏转段与预测落点。
- 发射鸟使用刚体初速度，并持续施加径向球面重力：

```text
GravityDirection = normalize(PlanetCenter - BirdWorldPosition)
Force = GravityDirection * Mass * GravityAcceleration
```

- 发射失败不会永久失去鸟；落地后可重新操控或自动归队。

## 5. 物理建筑破坏、自动回收与建造

| 对象 | 交互 | 初版物理结果 |
| --- | --- | --- |
| 木屋/仓库 | 红鸟通用冲击或黄鸟高速撞击 | 木柱、横梁、拉索断裂，货仓和木材暴露。 |
| 碉堡/工坊 | 红鸟定点撞击、黑鸟爆破 | 外壳开裂，活塞/配重推动承重件，掉落金属部件。 |
| 电厂/卫星遗迹 | 黑鸟爆破 | 炸药桶、能量核心或支撑系统触发多阶段坍塌，暴露晶体核心。 |
| 弹簧活塞、绳索、炸药桶 | 鸟或建筑碎块碰撞 | 推动、拉断、爆破，形成模板定义的连锁反应。 |
| 暴露材料 | 发射鸟飞越、落地或自动回收半径 | 写入全队库存；可保留少量视觉碎块，但不要求地面逐个拾取。 |

建造只允许绯翼执行，且所有建造逻辑以 `CellTopo` 为唯一判定源：玩家选择目标 Cell 后，建筑固定落在该 Cell 的球面中心位置；连续地表只负责查询该点的最终渲染高度、碰撞和表现，不参与建造归属、合法性或相邻关系判定。建筑朝向采用主控鸟前向投影到目标 Cell 切平面的方向，局部向上方向为该 Cell 中心的径向方向。

建筑只能建在坡度平缓的 CellTopo 中心点。PCG 在生成 CellTopo 高度场后，为每个 Cell 预计算 `BuildSlope`（以该 Cell 及其邻居的逻辑高度值估计）；连续表面只将此高度场插值为最终可见高度。仅 `BuildSlope <= BuildableSlopeThreshold`、非河道/湖泊、未被资源或建筑占用的 Cell 可建造；预览直接显示目标 Cell 的可建、不可建原因。首版不做任意位置摆放、自由模块拼装或运行时整地。

建筑联动只使用 `Cell.NeighborCellIds`，不使用世界距离：熔炉必须位于工作台的相邻 Cell；钢铁太空弹弓必须位于发射遗址 Cell 或其相邻 Cell；弹弓只能由预生成槽位中两个相邻或指定配对的槽位组成。每类可建造设施首版最多一座，并保持 Cell 占用互斥。

物理边界：只有当前发射鸟、当前目标建筑的有限模块、少量碎块及其装置使用真实刚体。每个初版建筑模板限制为 12–25 个活跃刚体、1–3 个连锁装置；草、远景树、普通岩石和远景建筑保持 HISM/静态网格。禁止全地形破坏、大量同步建筑坍塌、复杂布料或持续燃烧。

## 6. PCG 小行星与地形

> Task 图驱动的区域分配、道路/河网约束、桥梁状态和生成后可达性验证见 [ABTSTaskGraphPCGDesign.md](ABTSTaskGraphPCGDesign.md)。本章只保留主设计约束。
>
> M2 的 CellTopo/连续表面职责、编辑器步骤和验收见 [M2PlanetSurfaceDesign.md](M2PlanetSurfaceDesign.md)。

### 6.1 拓扑与几何预算

| 层 | 规格 | 数量 | 职责 |
| --- | --- | ---: | --- |
| 逻辑/PCG | 正二十面体 `CellTopo Sub=5` | `10 * 4^5 + 2 = 10,242` Cell | 资源区、地块、资源查询、探索。 |
| 渲染 | 连续球面 `Sub=7` | `20 * 4^7 = 327,680` 三角形 | 低模地表、高度起伏、地面碰撞。 |
| 对应关系 | 渲染层高两级 | 平均约 `32` 三角形/Cell | 供逻辑 Cell 产生连续轮廓。 |

`Sub=7` 对 `Sub=5` 的平均映射是约 32 个渲染三角形/Cell。**CellTopo 永远是球面的逻辑源**：地块、资源、建筑、道路、水网、探索、可达性和任务验证均以 CellId、Cell 中心及 `NeighborCellIds` 表达；连续球面不拥有独立玩法状态，只负责按 CellTopo 数据生成高度、碰撞、材质和 HISM/Actor 表现。初版不要求运行时改变 Continuous Surface；资源变化通过独立 HISM/Actor 表现。

### 6.2 生成流程

```text
WorldSeed
-> 生成主路、道路距离场和按难度排列的区域 Task
-> CellTopo Sub=5 标记地块、可建造中心、弹弓槽和建筑候选位
-> 在 CellTopo 图上生成河网、道路、桥址、发射走廊与区域路线
-> 按距主路图距离生成模块化建筑、价值、结构难度和资源货仓
-> 生成潮汐锁定低轨卫星及其小型 CellTopo、地表目标和主星引力走廊
-> 由 Cell 地块、河网、道路和低频场重算球面高度
-> Continuous Surface Sub=7
-> 由 CellTopo 数据驱动连续材质、HISM/模块化建筑、道路、水体和卫星表现
```

### 6.3 大区与地块

低频场至少生成起始平原、主路走廊、河岸/湿地、建筑带、远端高难度地块和发射遗址周边。大区记录地块与建筑生成倾向；区块内部再生成草地、疏林、密林、碎石坡、河谷、陨石坑、建筑施工台和卫星可观测区。

WFC 必须局部化：区块独立求解、最大回溯步数、确定性随机、失败时回退到邻居计数规则扩散。禁止对完整 10,242 个 Cell 做无上限的全局 WFC。

高度场原则：主路和弹弓槽保持平缓，河谷、水面与桥址形成可读弹道走廊，远路区域允许山脊、陨石坑和遮挡地形创造高难度射界；发射遗址与候选建造 Cell 保持平整。初版不追求高保真连绵山脉。

### 6.4 道路、水网与区域路线

道路和水网均是 `CellTopo` 图上的逻辑网络，而非材质绘制后再反查的效果。河段记录为有向相邻 Cell 边 `FromCellId -> ToCellId`，并带有汇流量、宽度等级、两岸 Cell 和可跨越状态；道路记录为相邻 CellId 序列。连续球面根据这些图数据绘制河道、水面、湿润带、道路、桥和浅滩，但不改变其逻辑拓扑。

河网沿由高度场导出的相邻 Cell 下游关系生成，要求无环、可汇流，并在终点形成小湖或湿地。首版按汇流量划分为细流、浅河、深河/湖泊：细流可直接通过；浅河只能从 `Ford`（浅滩）、`FallenLog`（倒木）或 `Bridge`（桥）边跨越；深河和湖泊不可步行跨越、不可建造。鸟落入水体时播放落水反馈并回收至最近可通行河岸 Cell，不造成永久失败。

水域是步行、建筑破坏与弹道空间的共同规则，而不是纯惩罚：河岸/湿润带 Cell 提高树枝和低价值材料密度，湿地降低木屋/塔架地基稳定性，水面提供开阔射界。关键浅河边预留桥址；玩家以建筑回收的木材制作桥梁，将该相邻 Cell 边的 `CrossingType` 切换为 `Bridge`。深河和湖泊既阻止步行，也会吞没普通碎块；鸟落水回收至安全河岸 Cell，高价值核心若未被自动回收而落水则在下游回收点重置，不能永久锁死主线。首版不做游泳、浮力、任意倒木搭桥或动态水文。

道路按 Cell 邻接寻路形成主路与少量支路；主路是队伍跟随、相机引导、弹弓槽和机制教学的核心走廊。每 Cell 计算到主路的图距离 `RoadDistance`：距离越远，建筑越隐蔽、资源货仓价值越高、结构越复杂、攻击所需的射程/弹道精度越高；高价值建筑不得只凭主路视域直接发现，须经青翎侦察或高处观察标记。道路与深河相交时必须生成桥址，和浅河相交时必须生成浅滩或桥址。生成完成后必须在 CellTopo 图上验证：主路可连续推进至下一发射台；每个主线建筑均有不绕过能力门的可攻击发射槽；桥前后道路与高价值建筑可达性符合阶段锁。

### 6.5 材质、HISM 与资源

连续地表使用纯色或少量色块，不再依赖三平面 SDF 纹理/法线解释局部网格。SDF/Field Query 仅负责连续的密度、坡度、地块类型、距资源区距离等查询；细节由 HISM 草、树、岩石、树枝和晶体表现。

资源必须可复现：

```text
PlacementRandom = Hash(WorldSeed, CellId, ResourceType, LocalIndex)
```

| 对象 | 默认表示 | 交互时变化 |
| --- | --- | --- |
| 草、装饰石、远景建筑 | HISM/静态网格，无碰撞或简易阻挡 | 不变。 |
| 当前目标建筑 | DAG Plate/Column 以落座冻结态显示，主承重柱可读 | 撞击时激活有限刚体并进入材料损伤、坍塌和回收链。 |
| 建筑材料货仓 | 建筑内部逻辑库存与可见容器 | 关键承重/触发条件满足后暴露并自动回收。 |
| 小型视觉碎块 | Instanced/Actor 混合 | 计时清理；不决定材料库存。 |

## 7. 视觉、青翎小地图与终局

地表阶段使用正常日光、天空和环境光，重点是森林、岩区、资源和角色的清晰辨识。不要在此阶段用极暗背光、宇宙星空或强烈行星轮廓压过采集建造体验。

青翎是“近射侦察相机”：它可在主路旁树枝槽立刻近射，飞行期间短暂切入垂直向下的侦察镜头并拍照。拍照覆盖范围写入小地图，显示道路外被地形遮挡的建筑、弹弓槽、桥址、卫星引力走廊和发射遗址方向；它也能撞开近处建筑旁的松散低价值材料并自动带回。落地后青翎归队，不做永久自由飞行或主线高价值材料的跨河物流。

### 7.1 低轨卫星与引力走廊

地图可生成少量低轨、同步且潮汐锁定的卫星。卫星使用独立的小型 `CellTopo Sub=2/3`，在其表面生成极少量高价值建筑和稀有资源目标；它们不是第二张完整开放地图。卫星相对于主星维持固定方向，主星对应下方的发射区域生成可预测的引力走廊：高抛射的鸟在该区域受到额外卫星引力，弹道因此偏转并可能到达通常射程外的建筑或卫星目标。

卫星引力只作用于进入走廊的远距离发射，不改变普通近地攻击；强化弹弓显示主星重力段、卫星影响段和预测落点。鸟抵达卫星后进入低重力短时行动/回收状态，超时、落水、碰撞失败或主动召回时回到最近主星安全弹弓槽。首版只需一颗卫星、一个可攻击目标和一条引力走廊。

## 8. 模块边界

可复用现有项目的球面拓扑、World Seed、连续 Surface、高度采样、HISM、空间投影命中、LUT 高亮经验和相机绕球基础。

新玩法必须独立于旧回合制 Gameplay：新地图、新 GameMode、新 UI、新存档数据。旧棋子、阵营、回合、Cell 点击移动和 NPC 系统只保留在旧模式作技术备份。

| 新模块 | 职责 |
| --- | --- |
| `AngryBirdsToSpace` Runtime Module | 资源、配方、任务、建造、PCG 编排。 |
| `ABTSBirdPawn` / `ABTSBirdPartyComponent` | 第三人称主控、切换、跟随、回收。 |
| `ABTSResourceComponent` | 低价值拾取、建筑货仓、发射鸟自动回收与全队库存。 |
| `ABTSConstructionComponent` | 基于 CellTopo 的建筑占用、坡度/水体校验、配方与相邻联动验证。 |
| `ABTSPlanetPCGComponent` | 主路距离场、Cell 地块/高度、河网/道路/桥址、弹弓槽、建筑/卫星生成与路线验证。 |
| `ABTSBuildingPhysicsComponent` | 建筑模板、承重关系、刚体激活、连锁装置、货仓暴露与摧毁状态。 |
| `ABTSSlingshotComponent` | 槽位、桩/弦组件、配对、射程与弹道预览。 |
| `ABTSPhysicsInteractionComponent` | 建筑撞击、弹射、主星径向重力、卫星引力走廊与水体回收。 |
| `ABTSDiscoveryComponent` | 青翎拍照、探索数据、小地图。 |

## 9. 一周中期评审纵向切片

> M1 的 C++ 职责、编辑器建图步骤、验收与排错见 [M1IndependentEntryDesign.md](M1IndependentEntryDesign.md)。
> M2.5 的径向引力、碰撞、跳跃与编辑器验收见 [M25RadialGravityAndJumpDesign.md](M25RadialGravityAndJumpDesign.md)。
> 当前已验收的 Chaos 刚体球面移动、碰撞与编辑器配置见 [ChaosRigidBodyMovementDesign.md](ChaosRigidBodyMovementDesign.md)。旧 `ForceSuspension`/`LegacySweep` 仅作为对照路线保留。
> M6/M7 物理碰撞破坏爽感调研、阈值悖论分析和后续实现指导见 [PhysicsImpactDestructionResearch.md](PhysicsImpactDestructionResearch.md)。
> M7.1 平面物理测试台、可拖拽关卡 Actor、四档完整弹弓与编辑器搭建步骤见 [M71PlanarPhysicsTestStageDesign.md](M71PlanarPhysicsTestStageDesign.md)。该测试台与正式 CellTopo/球面 PCG 并行，不改变正式地图逻辑源。

初版目标：先完成一个目标的完整纵向切片，验证 PCG 与物理弹射真正相互决定；暂不扩展为多建筑、多卫星或完整资源阶梯。评审视频应保留地图生成、道路/水网、建筑坍塌、卫星引力和终局的过程证据。

| 编号 | 模块 | 中期验收 |
| ---: | --- | --- |
| M1 | 独立入口 | 新地图、GameMode、第三人称绯翼、基础 HUD 可启动；不加载旧回合 Gameplay。 |
| M2 | 球面环境 | `CellTopo Sub=5` 与 `Continuous Surface Sub=7` 可生成、可碰撞、可行走。 |
| M3 | 主路与月度 Encounter PCG | R-0 已验收；R-1/2/3/3.1 的路线候选、六 Encounter 逻辑和普通槽场已进入集成基线。R-3.1 的通用 M5.1/M6 消费规则已通过自动化，但月度实体槽仍等待 R-4/R-6 唯一 Candidate；R-4/R-6 还依赖真实 M6/M9 预测、M7 Profile Catalog 和 vNext 建筑合同。详见 [M3R 路线](M3PCGMapImprovementPlan.md)。 |
| M4 | 鸟群 | 四鸟可见；Tab/HUD 切换；蓝鸟树枝近射、红/黄简易弹弓、黑鸟强化弹弓的入口与限制明确。 |
| M5 | 加工与组件 | 共享物品栏、背包/加工界面、红鸟加工权限、附近工作台/熔炉配方和制作数量流程已实现；正式站点放置、拾取与弹弓组件表现进入 M5.1。详见 [M5InventoryCraftingImplementationDesign.md](M5InventoryCraftingImplementationDesign.md)。 |
| M5.1 | 世界物品与放置 | CellTopo/SDF 基础物品刷新与自动拾取、独立手持栏、工作台/熔炉平地放置、TaskGraph 兼容槽、已接受月度槽快照消费接缝及桩/弦两次点击装配；普通弦使用世界厘米长度和三维障碍门，失败不留下库存/Actor/端点半状态。详见 [M51WorldItemsPlacementSlingshotDesign.md](M51WorldItemsPlacementSlingshotDesign.md)。 |
| M6 | 弹弓、发射与碰撞 | 两桩+弹弦组成简易弹弓；槽位朝向/间距影响预览弹道和初速。已连接弹弓进入瞄准/拉伸/万有引力预测/发射闭环；HISM 按命中提升动态代理并支持连锁破坏，黑鸟支持手动/延时爆炸，落地静默后物体冻结与队伍回归。玩法见 [M6SlingshotLaunchAndImpactDesign.md](M6SlingshotLaunchAndImpactDesign.md)，双弦、弹珠袋和模型协议见 [M6SlingshotVisualPresentationDesign.md](M6SlingshotVisualPresentationDesign.md)。 |
| M7 | 建筑生成与破坏 | 木/石/铁/玻璃材质 Profile 驱动 Chaos 摩擦、弹性、密度、推动传递与累计损伤。TaskGraph 三类普通建筑已由 M7 Resolver 接入 M7.3-DAG2.3；M3 只提供 Anchor/Pad。M7 显式登记必需 Actor 集合，每栋 Actor 独占 IdleValidation/Freeze；M6 只在数量相符、Setup 未拒绝且全部 Actor 通过时发布 WorldReady。详见 [M7 球面集成](M7TaskGraphSphericalBuildingIntegrationDesign.md)。 |
| M7.1 | 平面物理测试台 | 独立平面 GameMode、可摆放 Floor/PlayerStart、树石 HISM、四材质砖、四档完整弹弓和模块化建筑锚点；支持编辑器实时变换、平面 Chaos 移动与恒向重力弹射。详见 [M71PlanarPhysicsTestStageDesign.md](M71PlanarPhysicsTestStageDesign.md)。 |
| M8 | 自动回收与桥梁 | M7 砖块在真实破坏时直接回收到共享背包；`CellTopo` 的 `bBlocksOnFoot` 水边生成空气墙，只有消耗桥梁组件并放置在 `BridgeSite` 边上才会打开通路。详见 [M8AutoRecoveryAndBridgesDesign.md](M8AutoRecoveryAndBridgesDesign.md)。 |
| M7.3-A | Legacy 稳定建筑基础 | 固定层轮廓与旧 Ground/Idle 基线已完成；GroundAdapter、StructureData、Runtime Module 和 IdleValidation 继续被 DAG 复用。其 `LegacyLayeredAB2` 生成器已退出 TaskGraph 生产，仅保留历史诊断。详见 [M73A](M73AStableBlockBuildingImplementationDesign.md)。 |
| M7.3-B/B2 | Legacy 弱点对照 | 旧图选点与顶部 Carrier/Payload 失效仍保留为历史自动化证据，但不参与现行 TaskGraph 生成或验收，也不能作为 DAG Reject 的回退。详见 [M73B](M73BWeakPointAndDifficultyDesign.md)与 [M73B2](M73B2StructuralWeaknessAndFailureValidationDesign.md)。 |
| M7.3-DAG | 递归主体承载图 PCG | DAG2.3 仍是普通建筑生产默认；DAG3-A/B/C、DAG-4 与 DAG5-A 已完成并进入集成基线，但继续显式关闭。当前推进 DAG5-B/C 的复杂轮廓与六栋联合选择，之后才由 DAG5-D/E 接 M3 Encounter 并评审生产切换。详见 [总体设计](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)与 [DAG-5](M73DAG5CandidateSearchSemanticEnvelopeAndProductionDesign.md)。 |
| M7.3-DAG-1 | 纯数据递归语法 | 已建立独塔、拱门、双塔桥基准表达式、路径级确定性递归、预算终止、Macro 支撑 DAG 和拓扑 Hash；现由 DAG-2/2.3 编译后进入生产 Actor。详见 [DAG-1](M73DAG1RecursiveGrammarImplementationDesign.md)。 |
| M7.3-DAG-2 | 空间布局与模块编译 | 将 DAG-1 Macro 降低为 Plate/Column 与 Realized Contact DAG，复用 GroundAdapter 和 Chaos 装配；已经由 DAG2.3 扩展并接管球面 TaskGraph。详见 [DAG-2](M73DAG2SpatialLayoutAndModuleCompilationDesign.md)。 |
| M7.3-DAG-2.1 | 支撑模式与轻量化 | Logical Support 可降低为两柱线、三柱三脚架或四柱角点支撑，以真实凸包校验；现行生产默认三柱、56cm 柱、40cm 板。详见 [DAG2.1](M73DAG21SupportPatternsDesign.md)。 |
| M7.3-DAG-2.2 | 自适应几何 | 对窄 Parallel 分支和宽板自适应 Plate/Column 尺寸与数量，避免用非法旁路伪造支撑；已成为生产编译链一部分。详见 [DAG2.2](M73DAG22AdaptiveGeometryDesign.md)。 |
| M7.3-DAG-2.3 | 累计荷载与联合支撑 | 从高到低传播累计质量/一阶力矩，以联合柱脚凸包覆盖合力点。现行 Target 使用 Budget=0 TwinTowerBridge，确保 Parallel 联合支撑进入生产；自动化与两次球面冷启动通过，待可见击打验收。详见 [DAG2.3](M73DAG23CumulativeLoadAndJointSupportDesign.md)。 |
| M8 | 自动回收与桥梁 | 发射鸟自动回收暴露材料；以回收木材建桥，水网和道路边状态正确更新。 |
| M9 | 卫星与强化弹弓 | 生成一颗 `CellTopo Sub=2`、渲染 `Sub=4` 的纯灰卫星；以 `SatelliteWindow` Seed 锚定，局部逆平方引力叠加到鸟体与预览弹道。M3 V3 保证它远离 LaunchSite 且与终局双桩轴近似共线；M11 数据端明确排除该卫星。详见 [M9SatelliteGravityDesign.md](M9SatelliteGravityDesign.md)。 |
| M10 | 青翎侦察小地图 | 树枝与植物纤维在弹弓槽装配 Twig 弹弓，仅青翎可发射；完整发射结束后以最终落点固化球面侦察圆盘，显示 SDF 地形、道路、河网、树石、建筑和四鸟位置，并持续跟踪 Chaos 位移与破坏。详见 [M10ScoutMinimapDesign.md](M10ScoutMinimapDesign.md)。 |
| M10.1 | 道路外目标与引力走廊 | 强化弹弓 Pulling 时以三层视图辅助超视距发射：保留弹弓近端主视图；侦察范围内的预测落点启用远端落点画中画；轨迹足够长或落点离开主视距时自动显示左下圆形轨道全景图。M10.1-A/B/C 初版均已完成 PIE 验收；C 已实现整条轨迹的最佳拟合平面、正交投影与凸包自适应取景，使弹弓固定在图左侧，并以理想主星的世界绝对经纬网、无经纬网卫星以及球后虚线/可见实线解释空间偏转。通用目标选择与走廊 M10.1-D 延期。详见 [M10.1 总设计](M101BeyondHorizonLaunchInterfaceDesign.md)与 [M10.1-C 实现详稿](M101COrbitalOverviewDiagramDesign.md)。 |
| M11.0 | 终局前置收口 | LaunchSite 不生成玻璃建筑，只生成一对 Space-only 槽；拆分太空桩/弦配方；将 M9 卫星远置为强化练习；导出终局局部坐标系和只含主星+三助推行星的纯数据边界。代码、编译、自动化、固定 Seed 独立进程基线与用户 PIE 视觉/交互验收均已完成。详见 [M11.0](M110PreFinaleClosureDesign.md)。 |
| M11 | 三重引力弹弓终局 | v1 的 M11.0/A/B/C 是当前生产基线；v2.1 已完成标准 C++ Core、两个 Editor-only 候选和交互/PIP/轨迹相机实现并进入集成基线。两个候选仍未认证；下一步是冻结一个体验方案，再完成 B v2.2 全输入域认证、C v2.2 正式绑定和 M11-D 演出。详见 [M11 v2](M11V2FinaleOptimizationDesign.md)。 |

初版演示顺序：展示固定 Seed 生成的主路、河网、桥址、道路外建筑、弹弓槽与卫星；青翎从树枝槽近射侦察并标记目标；玩家收集保底树枝/石料，在工作台加工两桩与弹弦；组装简易弹弓并发射红鸟或黄鸟击中 DAG 建筑的可读主承重柱；建筑模块连锁坍塌，发射鸟自动回收材料；以木材修桥或加工强化部件；在 `SatelliteWindow` 展示卫星引力偏转；加工两根太空桩和一根太空弦，在无建筑 LaunchSite 完成钢铁太空弹弓；由局部预设生成终局三行星并依次完成三次引力弹弓，命中 UFO、救出白色小鸟。

中期不要求完成昼夜、天气、大规模 HISM 装饰。

### 第一周日程



## 10. 第二至四周



## 11. 风险与性能预算

| 风险 | 原因 | 控制方案 |
| --- | --- | --- |
| 全局 WFC 卡顿 | 10,242 Cell 回溯规模过大。 | 局部求解、步数上限、规则扩散回退。 |
| Surface 重建慢 | Sub=7 有 327,680 三角形。 | 仅新局/显式重建；首版静态。 |
| 物理拖慢 | 多建筑模块和连锁装置同时激活。 | 同时活跃刚体不超过 50；单建筑 12–25 个刚体、1–3 个装置；睡眠与计时清理。 |
| HISM/Actor 爆量 | 所有资源都生成 Actor。 | 远景 HISM，靠近或交互再替换为 Actor。 |
| 球面跟随不稳定 | 全球 NavMesh 不适配。 | 球面切向追踪 + 距离阈值回收。 |
| 弹弓与地形脱节 | 建筑可在任何位置用同样弹道摧毁。 | 以主路距离、发射槽轴向、水域/山脊遮挡和卫星走廊生成并验证每个目标的攻击解。 |
| 河网阻断主线 | 河道和道路随机生成使目标区域不可达。 | 将河道/道路/桥址全部表示为 CellTopo 边；生成后执行主线路径验证，失败则用同 Seed 派生重试或修复桥址。 |
| 首周范围失控 | 同时实现多建筑、多卫星与完整资源阶梯。 | 首版仅一个目标建筑、一个桥址、一个弹弓槽对、一颗卫星和一条引力走廊；其余在纵向切片验收后加入。 |

中期性能目标：进入固定 Seed 至可控状态不超过 20 秒；常规探索活跃刚体不超过 50；优先稳定 60 FPS。

## 12. 验收与暂不做

初版验收：在固定 Seed 下，玩家可完成“主路推进/青翎侦察 -> 拾取树枝石料 -> 工作台加工桩与弦 -> 组装简易弹弓 -> 发射红鸟或黄鸟击溃一座道路外模块化建筑 -> 自动回收材料 -> 加工强化组件或建桥 -> 展示卫星引力偏转 -> 钢铁太空弹弓终局”。必须可见并记录：主路距离影响建筑生成位置/价值/难度，弹弓槽轴和地形/水域决定攻击解，建筑刚体/装置发生连锁破坏，卫星改变强化弹道。所有任务、道路、水网、建筑、弹弓槽、卫星和可达性仍须以 CellTopo 为逻辑源；连续球面只作为可见与可碰撞表现。

初版暂不做：多目标建筑生态、多个卫星、完整卫星探索地图、任意插桩/任意倒木搭桥、游泳/浮力/动态水文、昼夜、天气、运行时 Continuous Surface 变形、洞穴、体素地形破坏、完整自动化工厂、电网、多人联网、大规模战斗/AI、无限宇宙或跨星球旅行。
