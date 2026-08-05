# AngryBirdsToSpace 开发排错记录

> 编码：UTF-8，简体中文。
>
> 用途：沉淀本项目已经遇到或已经被设计约束覆盖的问题。新增问题时，记录“现象—根因—修复—防回归验证”，不要只保留最后一次临时改动。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M3 地形表现](M3TaskGraphTerrainPresentationDesign.md) · [Chaos 刚体移动](ChaosRigidBodyMovementDesign.md) · [M6/M9 标定模式](M6M9SlingshotSatelliteCalibrationDesign.md) · [M9 卫星](M9SatelliteGravityDesign.md) · [M7.3-A 稳定建筑](M73AStableBlockBuildingImplementationDesign.md) · [M7.3-B 弱点与难度](M73BWeakPointAndDifficultyDesign.md) · [M7.3-B2 结构失效验证](M73B2StructuralWeaknessAndFailureValidationDesign.md) · [M7.3-DAG 递归主体建筑](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) · [M7.3-DAG-2 空间布局](M73DAG2SpatialLayoutAndModuleCompilationDesign.md) · [M7.3-DAG-2.1 支撑模式](M73DAG21SupportPatternsDesign.md)
>
> 持续追加的阶段原始账本：[M3 工作树排错记录](M3WorktreeTroubleshootingLog.md) · [M7 工作树排错记录](M7WorktreeTroubleshooting.md) · [M11 工作树排错记录](M11WorktreeTroubleshooting.md)

## 1. 使用规则

- 所有运行时判断优先依赖当前进程的日志、截图或 Standalone 结果；旧 PIE 日志不能作为新修改的验收证据。
- `CellTopo` 是球面逻辑源。连续球面、材质、水体 Mask、HISM 实例和碰撞命中均不得反向成为资源、建筑、道路、水网或可达性的逻辑来源。
- 单次修复改变了类、组件、Blueprint 资产、材质或地图时，必须在本文中补充验证路径。
- 本文记录的是问题与处理；每个里程碑的完整编辑器操作仍以对应 `M*...Design.md` 为准。

## 2. 构建与模块

| 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| 添加 M1/M2 C++ 类后编辑器找不到类 | `.uproject` 未注册 Runtime 模块，或 Editor 使用旧 DLL。 | 在 `.uproject` 注册 `ABTSRuntime`，主模块明确依赖它；关闭编辑器后完整编译 `AngryBirdsToSpaceEditor Win64 Development`。 | 启动新 Editor 后在 Class Viewer 搜索 `ABTSM1GameMode`、`ABTSM2Planet`。 |
| UE 5.8 编译 HUD 准星时报 `UCanvas` 没有 `DrawLine` | 调用了错误的绘制对象接口。 | 使用 `AHUD::DrawLine`，而不是 `UCanvas::DrawLine`。 | Development Editor 编译成功；PIE 有中心准星。 |
| 源文件职责膨胀 | 入口 GameMode 被当作玩法总线。 | 保持 `Game / Player / Planet / UI` 分目录；新玩法以独立组件或模块加入。 | 每个文件保持小于 600 行；M1/M2 不把 PCG、库存或鸟群实现塞进 GameMode。 |
| 为注册项目 Global Shader，把整个 `ABTSRuntime` 改为 `PostConfigInit` 后 fresh Editor 在动画 CDO 构造阶段断言 `AnimationDataController` 未加载；同时 Scene View Extension 报 `GEngine` 尚未建立 | Global Shader 类型确实必须在引擎 Shader 初始化前注册，但 Gameplay Runtime 含大量 UObject 和内容构造链，不能整体提前加载；`PostConfigInit` 时也尚不满足 View Extension 的引擎生命周期前置条件 | 拆出不依赖 Gameplay/UObject 的轻量 `ABTSRender` 早加载模块，只在其中注册 Global Shader；`ABTSRuntime` 恢复 `Default`。View Extension 延迟到 `GetOnPostEngineInit()` 创建，关停时解绑委托并释放扩展 | UE 5.8 `-ForceUnity -DisableAdaptiveUnity` 全链接；fresh `UnrealEditor-Cmd -NullRHI` 无 GEngine ensure、模块断言或 Critical Error；fresh D3D12 能编译项目 Shader 并完成 T0 A/B 截图与 ProfileGPU |

## 3. M1 独立入口

| 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| 启动时进入错误地图或使用默认 Pawn | Project/World Settings 未指定 M1 GameMode/地图。 | 按 [M1IndependentEntryDesign.md](M1IndependentEntryDesign.md) 设置 `/Game/Maps/L_ABTS_M1` 与 `ABTSM1GameMode`。 | PIE 与 Standalone 均显示 M1 HUD，并输出 `[ABTS][M1]` 日志。 |
| 美术 Blueprint 替换后相机失效 | 删除、改名或重新附着 C++ Default Subobject。 | 保留 `BirdVisual`、`CameraBoom`、`FollowCamera` 的名称和层级，仅替换网格/材质。 | 新开 Editor 后，Blueprint 与 Standalone 均可控制镜头。 |

## 4. M2 球面拓扑、绕序与法线

### 4.1 三角形绕序全反

| 项目 | 记录 |
| --- | --- |
| 现象 | `AABTSM2Planet` 的连续球面三角形绕序反向；初版曾在渲染输出阶段逐面交换 `Y/Z`，表面可见但逻辑源仍错误。 |
| 根因 | 初次修复错误地把“代数叉积朝外”当作 PMC 正面标准。截图证明该顺序在 `UProceduralMeshComponent` 中产生错误的球外可见面。UE PMC 的实际正面索引顺序与 Terra `PrimalTris` 相同：对同一组顶点，它的代数叉积朝内，但渲染前面与显式 `+UnitCenter` 法线仍分别由索引绕序和顶点法线控制。细分会保持父三角形手性，因此源 20 面的错误会扩散至全部 `20 * 4^7 = 327,680` 个面。 |
| 正确约定 | ABTS PMC 使用 Terra `(A,B,C)` 的初始 20 面顺序；该顺序在本项目中是球外可见的 PMC front face。顶点法线独立取 `+UnitCenter`，保持光照法线朝外。`cross(B-A,C-A)` 的符号不能单独代表 PMC 的剔除正面。 |
| 修复 | 使用 Terra 原始 20 面顺序，删除 `BuildContinuousSurface` 中逐面交换索引的掩盖性修正；`InwardTriangles` 改为统计“与 PMC 正面约定相反”的面，要求为 0。 |
| 防回归 | 固定 Sub=7 日志必须有 `Triangles=327680 InwardTriangles=0 Ready=1`。若非 0，修复 `BuildUnitIcosphere` 的源面顺序，禁止只在渲染阶段翻面。 |

### 4.2 CellTopo 与连续球面的职责混淆

| 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| 地形/道路/水网通过材质像素、网格三角形或 HISM InstanceId 判定 | 将表现层误当作逻辑层。 | 所有逻辑只写 `CellId`、`NeighborCellIds` 和相邻 Cell 边状态；连续球面只消费数据。 | M3+ 的资源、道路、河段、桥梁和建筑均可由 CellTopo 数据重建。 |
| `ABTSM3Planet` 下有 HISM 组件但场景中没有任何实例 | Actor 的 `Forest/Rock Instance Mesh` 为空；旧逻辑仍用它覆盖地图实例或 Blueprint 组件已经配置的 `Static Mesh`，将可绘制网格清空。 | 网格按 Actor 字段、组件字段、Engine Basic Shape 占位的顺序解析；只有 Actor 字段非空时才覆盖组件。查看 `[ABTS][M3][HISM]` 的 Mesh、EligibleCells 和 Instances。 | 未配置美术资产时仍可看到 Cone/Cube 占位；配置组件网格后重建不会被清空。 |
| HISM 实例存在，但树和岩石材质不显示或回退默认材质 | 网格材质没有编译 `Instanced Static Meshes` 着色器排列；普通 StaticMesh 正常不代表 HISM 可用。 | 在材质 Details → Usage 启用 `Used with Instanced Static Meshes` 并保存；检查 `[ABTS][M3][HISM]` 的 `ForestMaterialsValid` / `RockMaterialsValid`。 | `M_PineTree`、`M_Stone` 均输出 MaterialsValid=1，fresh Editor 中不再回退默认材质。 |
| 在同一 `ABTSM3Planet` 上更换 `WorldSeed` 后，道路仍保留旧走向或道路数量异常增加 | `TArray::SetNum` 在长度不变时保留已有 CellState；而道路生成只新增 `bRoad=true`，未清理上一次的 `bRoad` 与 `RoadDistance`。 | 每次 TaskGraph 生成前对 `OutCellStates` 先执行 `Reset()`，再按当前 CellTopo 数量 `SetNum()`。 | 连续以不同 Seed 重建时，第二次道路集合只由第二次 TaskGraph 生成。 |
| 新 RoadPlanner 在第一个 Link 上长时间无响应，PCG 日志停在 Hydrology 之后 | A* 开放集使用 `TArray::HeapPop` 的默认收缩策略，每次 Pop 都可能触发内存重分配；10242 Cell 搜索退化为分钟级。 | 开放集预留 Cell 数量，并以 `HeapPop(Node, EAllowShrinking::No)` 弹出；保留最大 Pop 次数防御。 | 固定 Seed 的 Mission/Spatial/Height/Hydrology/Roads/Validate 逻辑阶段总计约 10ms，不再超时。 |
| 多次运行后 Cell 数量异常 | 重建函数追加旧网格/拓扑状态。 | 每次 `BuildUnitIcosphere` 清空顶点和三角形；`RebuildPlanet` 重新构造逻辑与表现。 | Sub=5 始终 10,242 Cells，Sub=7 始终 327,680 Triangles。 |

## 5. M2 球面角色与相机

| 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| 角色在极点不能移动或镜头突然翻转 | 使用固定世界 XY/经纬角做移动基向量，在极点退化。 | 每帧由 `RadialUp=normalize(CharacterLocation-PlanetCenter)` 建立局部切平面；运动和相机前向均投影到该平面。 | 从北极移动到赤道和南半球，WASD/鼠标不翻转。 |
| 角色 Down 不朝球心 | Actor 姿态仍使用世界 Z Up。 | 使用 `MakeFromXZ(TangentForward, RadialUp)` 写入 Actor 旋转；本地 -Z 即球心方向。 | 在不同球面位置观察角色，局部 +Z 始终背离球心。 |
| M2 运行时仍是 M1 角色 | `BP_ABTSM2GameMode` 资产序列化覆盖了新 C++ 的 DefaultPawnClass。 | M2 GameMode 在 BeginPlay 兼容替换已生成的 M1 Pawn；随后在编辑器更新 Blueprint 的 Default Pawn Class。 | 本次启动日志出现替换日志一次，且最终 Pawn 是 `AABTSM2BirdCharacter`。 |
| 角色未实现重力却下落/浮空 | 普通 `CharacterMovement` 默认世界重力仍生效，或未把角色投影回球壳。 | M2 设置 `MOVE_Flying`、`GravityScale=0`，并每帧投影到 `PlanetRadius + CapsuleHalfHeight`。 | 本期角色保持在球壳；文档不将其标记为真实重力。 |
| 相机在无输入帧慢慢漂移 | Controller/Camera 的默认同步改写姿态。 | M2 每帧从明确的切线前向和径向 Up 用 `MakeFromXZ` 写入 ControlRotation。 | 静止 10 秒后，角色 Up 与镜头朝向不漂移。 |

## 6. M2.5 径向引力、碰撞与跳跃

| 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| 加入引力后角色仍被锁死在地表，无法跳跃 | M2 表面组件每帧强制投影位置，覆盖了物理速度。 | M2.5 只复用该组件的径向姿态/相机，调用 `SetProjectToBaseSurface(false)`，位置交给 M2.5 Movement。 | Space 后角色先离地、后回落，不会同帧被拉回。 |
| 跳跃沿世界 Z 而非表面法线 | Jump 使用固定 `FVector::UpVector`。 | 接地时使用 `normalize(CharacterLocation-PlanetCenter)` 作为跳跃方向。 | 南半球跳跃仍离开球面，而不是朝世界上方。 |
| 引力方向在绕球移动时错误 | 一次缓存重力向量后不再更新。 | 每 Tick 按当前角色位置重算 `RadialUp` 和 `GravityDir=-RadialUp`。 | 从北极到赤道再到南半球，引力始终指向球心。 |
| 碰撞后持续向阻挡体加速、穿透或抖动 | Sweep 命中后未移除朝碰撞面的速度分量。 | 对 `ImpactNormal` 投影速度，移除 `dot(Velocity, Normal)<0` 的分量。 | 撞 Cube 后停止/滑动，不穿透。 |
| 角色只有初始点能跳，修正阈值后又变成任何位置都不能跳 | 严格以 `RadialSpeed <= 0` 判定会受曲率误差影响；只读取上一帧的 `bGrounded` 又会让跳跃依赖 Tick 顺序，空格在瞬时非接地帧被永久丢弃。 | 消费跳跃前先按当前位置刷新几何接地；向外速度未超过 `UngroundSpeedCMPerSec` 时保持接地；用 `JumpBufferSeconds` 短暂保留输入，接地时清除径向速度。 | 落地、持续移动、转向和松键后分别按空格，均能稳定起跳；空中不能二段跳。 |
| 地面惯性滑动、反向时横向漂移异常 | 输入加速度不断叠加到旧切线速度，旧方向需要很久才能抵消。 | 地面状态按输入计算目标切线速度，再分别以地面加速度和刹车速度向目标收敛；仅空中保留加速度式控制。 | 松键后快速、平滑停下；左右反向没有绕弧漂移或持续滑行。 |
| 跳跃仍无效且原因不明 | 仅凭最终画面无法区分输入没有到达、没有发现星球、未接地、跳跃被接受后立刻落回地面。 | 搜索 `[ABTS][M2.5][Jump]` 和 `[ABTS][M2.5][Ground]`。日志按输入到角色、输入缓冲、接地状态、跳跃接受/缓冲超时的顺序输出关键状态。 | 按一次空格即可在 Output Log 中复现完整链路；根据首个缺失或警告节点定位责任模块。 |
| M3 高度平滑后角色爬坡强烈卡顿 | 旧移动始终把速度投影到径向切平面，并在接地帧继续施加向内重力；胶囊因此不断撞入坡面，Sweep 丢失位移后再被硬贴到新高度。胶囊中心只加 `HalfHeight` 也没有补偿坡面法线倾斜，侧面会轻微嵌入三角面。 | 接地移动改为投影到 `GetSurfaceNormalAtDirection` 的平滑地表切平面；接地时不重复施加重力；用胶囊对斜面的 support offset 计算中心高度；接地进入/退出使用不同容差，并限速修正残余径向误差。角色姿态与 Down 仍只采用球心径向。 | 连续上坡时速度稳定，不再出现“碰撞停顿—径向瞬移”循环；`[ABTS][M2.5][Ground]` 不应在普通坡面上逐帧 Grounded/Airborne 抖动，空格跳跃仍出现 `Accepted`。 |
| ForceSuspension 从高处落下时穿入地形后才弹出 | Force mover 曾为避免悬挂与地形重复接触，显式忽略 `ContinuousSurface`；落地仅由解析半径场的弹簧恢复，无法阻止高速位移越过 Mesh。 | M5.2 将连续地形纳入 Capsule Sweep，命中点按 CPU `QuerySurfacePhysics` 复算道路/河流/地形 SDF，并用其阻力和恢复系数处理接触；悬挂只用于近地稳定。 | 从高坡落下时先出现 Sweep 命中，再停止或按表面恢复系数反弹；不可再观察到角色进入地面后被推出。 |
| M6 动态代理能生成但撞不到其他 HISM | HISM 使用 `QueryOnly` 时可被 Character Sweep/射线命中，但不会进入 Chaos 刚体接触求解，动态代理无法产生连锁物理碰撞。 | M6 将树石 HISM 改为 `QueryAndPhysics + WorldStatic`，仍保持 `SimulatePhysics=false`；只有被撞开的单个实例提升为模拟刚体 Actor。 | 发射鸟可命中 HISM；动态代理可撞击其他实例；全图 HISM 不发生移动或自主模拟。 |
| 切换到 M10 后尚未侦察便只有约 3–4 FPS，`StartupPhysics` 永不完成 | 并非 M10 底图或 HUD；日志没有 `[M10][Reveal]`。旧启动预结算把 `5149` 棵树和 `3377` 块石头全部从 HISM 转为 `8526` 个独立 Chaos Proxy；每个 Proxy 逐帧施加径向力。约 1400 个物体持续滑落，15 秒超时又只重启监视窗口，形成永久低帧。 | 启动预结算改为：建筑单独严格结算；HISM 先做空间粗筛和 Body 重叠测试，只把候选以默认 384 个分批运行 0.75 秒；每批 Transform 回写原 HISM并销毁临时 Proxy；冻结 Proxy 关闭 Tick；正式发射只局部激活已有 Proxy。建筑阶段设 6 秒硬截止，任何异常都不能无限阻塞。 | fresh M10 固定 Seed：`HISMInstances=8526 Candidates=3306 BatchLimit=384 ScanMs≈35`；批次期间帧号每秒推进约 50–100，而非 3–4；最终出现 `BatchRestored ... DynamicProxies=0` 和 `Complete WorldReady=1 ... StaticProxies=0`。若没有 `[M10][Reveal]`，不得把启动卡顿归因于小地图。 |
| 想测试 M6 但没有可用弹弓 | 正常流程要求先拾取、加工并完成两次插桩/连弦，难以快速反复验证瞄准和碰撞。 | 在 `BP_ABTSM6GameMode` 勾选 `Spawn Debug Slingshots At Start`；系统在出生 Cell 周围生成四向简易与四向强化完整弹弓。 | 日志出现 `[ABTS][M6][DebugSlingshots] ... Spawned=8 Simple=4 Reinforced=4`；点击任意弦可立即进入相应测试。 |
| 切换主控后新鸟看似自行滑行，空格暂时无效，并伴随约 4 秒 `HandoffDiag` | 根因由 M4 引入：`AABTSBirdParty` 与两套鸟移动组件都在 `TG_PrePhysics`，却没有 Tick 先后约束。移动组件可能先消费并清空输入，鸟群再写入跟随输入，导致该命令跨帧残留；Tab 恰好在交接帧改变角色后，旧角色命令、旧队列路径和旧 `bWasGrounded` 会污染新角色。日志中切到正在执行跟随跳跃的 `ForceGround=0` 鸟时，清速度但保留半空位置还会让重力坠落看似自主滑行，空中自然不能跳。`HandoffDiag` 只是每次 Tab 固定开启 4 秒的诊断窗口。 | 建立确定顺序 `BirdParty Tick -> Radial/Force MovementComponent Tick`，保证当帧跟随命令当帧消费；切换后暂停跟随输出 0.05 秒，覆盖输入交接边界；每次重建队列都清空旧角色的 Path、JumpEvents、距离与接地历史，并从当前位置重新播种。新主控若未接地，再按 CellTopo 派生连续地表半径做确定性落地交接；接地径向速度归零，贴地高度只由悬挂跟随处理。 | 切换日志必须包含 `PartyTickBeforeMovement=1 Pause=0.05`；切到空中跟随鸟时出现 `[ABTS][M4][HandoffGround] Snapped=1 ... AfterGround=1`，首个快照为 `ForceGround=1 / ForceVel=0 / Pending=0`。新主控不得出现 `PartyMoveAccepted`，切换后不得沿旧面包屑突然加速；正常跳跃仍输出 `Accepted`，4 秒后 HandoffDiag 停止。 |
| 旧坡面修正后仍有逐三角形卡顿 | 程序化地面使用 `Complex As Simple`；平滑顶点法线只影响渲染，Sweep/Chaos 接触仍读取三角形几何面法线。继续调整贴地速度无法消除接触法线跳变。 | 选择 `ForceSuspension`：`ContinuousSurface` 不再参加玩家障碍 Sweep，移动读取 CellTopo 派生表面查询并通过径向弹簧支撑；其他组件和 Actor 仍可参与 Sweep。使用 `ABTSMovementModeSelector` 在关卡中切换新旧模式。 | 日志为 `Active=ForceSuspension LegacyTick=0 ForceTick=1`；连续爬坡没有周期性停顿，Cube 仍能阻挡玩家。 |
| M3 玩家仍从地图 PlayerStart 开始，或传送后弹飞 | PlayerStart 是 UE 创建 Pawn 的临时入口；TaskGraph/Planet 和 Pawn 的 BeginPlay 顺序不固定，且传送前可能已经积累径向速度。 | `ABTSM3GameMode` 延迟查询 Start Task 首个道路 Cell，使用 M3 表面半径 + Capsule Half Height 放置角色；传送前清零 M2.5 速度、接地和跳跃缓冲。 | 日志仅出现一次 `[ABTS][M3][Spawn] Player placed at Start road`；改变 WorldSeed 后出生位置随 Start Task 改变。 |
| M4 鸟碰到一起后主控和跟随鸟都无法移动，且日志已显示 `PawnCollisionIgnored=4` | 鸟 Capsule 的实例响应确实已设为 `ECC_Pawn=Ignore`，但 ForceSuspension 的手动 Sweep 使用 `SweepSingleByProfile("Pawn")`，查询时重新载入默认 Pawn Profile，从而丢弃实例覆盖并继续把鸟当成阻挡物。舒适距离内缺少 Separation 又使接触更容易持续。 | ForceSuspension 改用 `SweepSingleByChannel`，传入 Capsule 当前 `ObjectType` 和 `GetCollisionResponseToChannels()`；同时在移动查询中显式忽略其他鸟 Actor，防止未来鸟模型的附加碰撞组件重新造成阻挡。过近时始终计算 Separation，完全重叠时使用稳定切向回退方向。Legacy 的组件 Sweep 原本就读取实例响应。 | 鸟相互接触后主控 WASD 仍产生实际位移，跟随鸟也能拉开；Cube、建筑等 WorldStatic 仍正常阻挡。 |
| M4 两只鸟进入分离距离后疯狂原地旋转，尤其在较小的 Queue/Follow/Separation 参数下明显 | 旧逻辑以“分离向量是否非零”为开关，再把合力归一化并固定施加 `0.65` 输入；刚越过分离边界的极小斥力也会变成强加速。惯性反复穿越阈值，使斥力开关与朝向逐帧翻转；非跟随状态还错误混入了朝目标的拉力。 | 分离改为随侵入深度平方渐强的连续输入；跟随 Arrival 与分离独立计算，只有 `bFollowing` 时才加入拉力；保留合力幅值而非先归一化为固定输入，并为近似抵消的合力设置小死区。完全重叠使用按 BirdId 成对反向的稳定切向逃逸方向。 | 使用 `Queue=80, Start=100, Stop=90, Separation=75`，让任意两鸟贴近；鸟应平滑分开并减速，不在 75 cm 边界反复横跳，模型朝向不连续翻转。 |
| M5 主控靠近工作台或熔炉占位方块后完全不能移动 | `ABTSCraftingStation` 的 Engine Cube 使用 `BlockAllDynamic`。ForceSuspension 对 Pawn 通道执行多子步 Capsule Sweep；接触方块后每步都产生阻挡命中并移除速度，滑动无法离开时看起来像输入失效。 | M5 站点改为 `QueryOnly`，默认忽略所有碰撞通道，仅 `Visibility=Block` 以保留 PlayerController 左键点击。它们仍可进行世界距离范围检测，但不会再进入角色 Sweep。M5.1 正式建筑应单独提供经移动回归验证的实体碰撞。 | 主控可穿过或贴近两个 M5 占位方块并继续正常走动、跳跃；鼠标左键仍能点击方块打开加工界面。 |
| M4 无法获得稳定俯视角，或靠近其他鸟时相机突然跳到近处/其他位置 | M4 仍直接使用每只鸟的 SpringArm；旧 Pitch 旋转符号/范围不适合固定俯视，SpringArm Camera Collision 又会在其他鸟 Capsule 进入探针时瞬间缩短。Possess 新鸟还会自动把 ViewTarget 切到新 Pawn。 | M4 PlayerController 关闭自动 Camera Target 管理，创建唯一的 `ABTSM4PartyCamera`；相机按当前主控的径向 Up 计算高位俯视位置并始终 LookAt 主控。主控切换只改变目标，通过位置与旋转 Lag 插值。鸟 Capsule 忽略 Camera 通道。 | 启动日志包含 `[ABTS][M4][Camera] ... DownAngle≈25`；靠近鸟群时无距离突变，Tab/头像切换时相机平滑过渡。 |
| M4 按 W/A/S/D 时视角绕到不同方向，且游戏内无法主动调整视角 | 临时队伍相机用主控 `ActorForward` 计算相机后方；角色朝向随每个移动输入变化，导致相机方位所有权落在角色而不是玩家。 | 使用持久球面 OrbitForward/Elevation/Distance；WASD 改为相机相对切向移动，角色转向不写相机。RMB 拖拽 Orbit、滚轮缩放、R 手动回正；切换主控保留 Orbit，仅对 Pivot 做球面 Cubic Blend。 | 依次按 WASD 相机 Yaw 不变；RMB 可自由调节；切到朝向相反的鸟仍保持原观察方向并平滑迁移。 |
| M4 右键 Orbit 到一定角度后画面侧倾、旋转 90°或完全倒置 | 相机使用 `MakeFromX(LookDirection)`，只约束 Camera Forward，没有定义屏幕 Up；接近俯视或跨象限时 UE 可选择另一组合法 Y/Z 轴。`RInterpTo` 对欧拉角插值又可能跨越 ±180°放大翻转。 | 以 `LookDirection + Pivot RadialUp` 双约束构造 `MakeFromXZ`：屏幕 Up 为径向 Up 在图像平面的投影。旋转改用最短弧 `QInterpTo`，并在每帧插值后再次按当前径向 Up 重建正交基，彻底剔除 Roll。 | 任意持续水平/垂直拖动时地表“上方”保持一致，画面不侧倾或倒置；Orbit 只改变方位角和俯视角。 |
| M4 向上拖拽时仍在俯视角就提前停止，无法经过水平视角进入仰视 | Elevation 被关卡参数限制在旧的正角度区间 `35°–76°`，只能表达相机高于切平面的俯视。若只把下限改成负数，旧遮挡逻辑又会用 `280 cm` 硬下限把负角度相机推入地形。 | 将 Elevation 定义为有符号俯仰角，运行时固定限制为 `[-85°, +85°]`；保留旧 Min/Max 字段仅用于资产兼容。遮挡命中时取消不安全的最小臂长，允许相机优先收缩到碰撞面之前。径向 Up 投影和 Roll Lock 保持不变。 | RMB 竖直拖拽可从 `+85°` 俯视连续通过 `0°` 到 `-85°` 仰视；两端停止但不翻转；负角度靠近地表时相机缩短距离且不穿入地形。 |

## 6. PCG 与后续玩法预防项

| 风险 | 约束与验证 |
| --- | --- |
| 简易弹弓配方锁死木材来源 | 简易弹弓只消耗树枝和石料；棱喙砍树才提供木材。 |
| 河网纯视觉、主线被随机阻断或绕过 | 河段、道路与桥梁全部是 CellTopo 边状态；生成后按 Key 阶段运行可达性验证。 |
| 建筑在球面随意倾斜或相邻判定依赖世界距离 | 建筑固定于平缓 Cell 中心；朝向使用局部径向 Up；熔炉/工作台等联动只用 `NeighborCellIds`。 |
| 青翎跨河采集绕过桥梁门 | 初版允许短时侦察、自动回归与信息解锁；不允许携带主线资源、建桥或完成关键配方。 |
### 河道呈六边形拼接

根因是表现层把 Cell 的 `bWater` 当作完整水面，材质只按最近 Cell/Voronoi 区域着色，河流边线没有进入独立距离场。修复后由 CellTopo 水边生成 `M3_RiverSegmentLUT`，Custom HLSL 对每条语义化线段计算像素到线段的最近距离，并以半宽和颜色混合宽度生成 River Mask；自然水文使用 Cell 中心流向线，Gameplay 割集使用 dual edge，地形高度也只在线段附近局部下凹。若仍出现六边形边界，检查运行日志 `[ABTS][M3][RiverSDF] Segments=...`，以及材质 Custom 是否包含 `RiverSegmentLUT`、`RiverSegmentCount`、`RiverColor` 三个输入。

### 河流变成一排互不相连的短条纹

根因一是混淆了图边的两种几何表达：自然水文 `CellEdge(A,B)` 表示从 A 流向 B，视觉中心线必须连接两个 Cell 中心；A/B 的 Voronoi 公共边（dual edge）与流向垂直，只适合表达 Gameplay 割集水障。根因二是局部河段 LUT 曾按固定一环邻居登记，河宽与混合带超出一环后会在 Cell 行切换处被硬裁断。修复后由 `FABTSM3RiverVisualBuilder` 按边语义选择 centerline/dual，并按实际 SDF 影响半径建立局部索引。验收日志要求 `FlowCenterlines > 0`、`BarrierDuals > 0` 且 `DroppedLocalRefs=0`。

### 道路和其他地形交界仍呈六边形折线

道路的根因是曾用 `bRoad` 填充整块 Cell，现由 `Transport CellEdge` 构造 Cell 中心连续线并写入 `M3_RoadSegmentLUT`。地形曾尝试简化 dual-edge 外轮廓，但失败原因是材质的基础归属仍取最近 Cell，轮廓之外依然保留 Voronoi 六边形。最终修复由 `FABTSM3TerrainFeatureVisualBuilder` 连接同类相邻 Cell 中心，材质比较四类线网的最近距离，以 line-feature Voronoi 完全替代 Cell-center Voronoi。验收日志要求 `[ABTS][M3][LinearSDF] RoadSegments > 0`、`TerrainFeatures > 0`、`DroppedRoadRefs=0`；`PrunedTerrainRefs` 是三环候选压缩统计，不要求为 0。

### 树木 Pivot 正确但在坡面上严重侧倒

根因是树木局部 `+Z` 完全使用连续地表法线；地表法线适合岩石贴坡，却不等于树木生长方向。修复后树木以球心径向为主，在径向与地表法线之间按 `ForestSurfaceNormalBlend` 做归一化插值，默认 `0.2`；岩石不受影响。验收 `[ABTS][M3][HISM]` 中 `MaxAppliedTilt` 应明显小于 `MaxSurfaceTilt`。若仍显得过斜，将权重降至 `0.1`；设为 `0` 可使所有树完全径向直立。

### 地表出现六边形黑色阴影斑纹

根因是几何半径曾直接取最近 Cell 的离散逻辑高度，产生六边形台阶；法线又只用约 `8cm` 的差分半径，跨台阶时得到近乎水平或翻折的异常法线。修复后高度在 CellTopo 三角形内做重心插值，法线以默认 `160cm` 的正交+对角中心差分平滑。检查 `[ABTS][M3][SurfaceNormals]`：若 `ExtremeOver80` 很大，先确认 `SurfaceNormalSmoothingDistanceCM` 未被旧 Blueprint 覆盖为极小值；可在 `120–240cm` 之间调节，数值越大越平缓。

## 7. M7.3-A 稳定积木建筑

| 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| `ABTSM73StableBuildingActor` 沿 Z 拖动时只有枢轴/主体暂时移动，基座保持原位；松手后整栋又被拉回测试台 | 平面 Ground Adapter 在每次 `OnConstruction` 中无条件把 Actor Location 沿 Stage Up 投影回 Floor，覆盖了编辑器 Z 变换；FoundationCap/Feet 又是 Static Mobility，无法跟随世界变换。 | 增加 `bSnapPlanarAnchorToTestStage`，默认关闭并以 Actor 完整 XYZ 作为局部施工平面；只有显式打开才执行贴地投影。FoundationCap/Feet 改为 Movable Mobility，但保持 `SimulatePhysics=false` 和阻挡碰撞。 | 在 M7.1 中分别沿 X/Y/Z 拖动并松手，主体、Cap、Feet 和枢轴始终整体移动且不回弹；打开 Snap 后才自动落到测试台；退出 PIE 不再出现 `FoundationCap 的移动性必须为可移动`。 |

## 8. M7.3-B 弱点与难度

| 症状 | 根因 | 修复 | 验收 |
| --- | --- | --- | --- |
| `TwinTowerBridge` 报 `InsufficientWeakPoints:0:1`，但肉眼看上层有很多木砖 | 使用按块数直觉设置了过高失撑比例；铁质桥面和 B2 载荷按真实密度计算后占比与块数直觉不同 | 弱点指标坚持 `体积 × M7 Profile 密度`；B2 当前默认 `MinWeakCollapseRatio=0.02`，并新增 Tip/Reseat 硬门槛，不退回按块计数 | `ABTS.M73B.WeakPointPlanner` 与 `ABTS.M73B2.StructuralWeaknessFailure` 均通过，TwinTowerBridge 产生有效 `AffectedNodeIds` |
| Editor 与 PIE 的 WeakPoint NodeId 不一致 | Editor 无 MaterialSystem 时使用共享默认 Profile，而 Runtime MaterialSystem 的可编辑 Profile 已改变强度/密度排序 | 默认 Profile 由 `FABTSM7MaterialProfileLibrary` 单点提供；Runtime 通过 `CopyMaterialProfiles` 注入实际数据。若手工改 Profile，按 Runtime 日志作为最终结果并同步调参 | 对照 `[M7.3-B][WeakPoint]`；同一 Profile、Seed 和朝向下 NodeId 必须一致 |
| 弱点规划失败后仍残留半块玻璃或强化材料 | 直接在输入 Data 上边筛选边改写，失败没有回滚 | Planner 在副本上事务式规划，只在全部难度门槛通过后 Move 回输出 | 设置不可能窗口后稳定拒绝，`WeakPoints` 为空且所有 `Material == OriginalMaterial` |
| 弱点击碎但普通低层梁更容易导致整栋倒塌 | 只选择弱点，没有检查其他高影响节点的单位材料成本 | 计算 `PredictedNonWeakEffect`，有限强化高影响非弱点，并要求 `WeakPointAdvantage >= MinWeakPointAdvantage` | 默认弱点单位成本结构效果至少为普通攻击的 1.5 倍；实际 M7.1 对照击打仍需验收 |
| 把所有关键梁强化为 Iron 后建筑质量和二次冲击异常 | 铁密度远高于木，大型铁楼板会显著改变总质量和 Chaos 响应 | 默认强化使用 Stone，并限制数量；Iron 只作为显式硬难度选择，换料后重算质量与难度 | 空载验证无弹飞；弱点最终比例仍在窗口内；普通攻击不再一碰全倒 |
| 空载日志 `MaxMove` 约 4–6cm 且旋转很小，却被判不稳定 | 旧验证将沿重力轴的小幅接触落座和施工平面内失稳漂移合并成一个总位移 | 将位移分解为 `MaxDrift` 与 `MaxSettlement`；默认分别限制 4cm/6cm，总位移只保留诊断 | 五层平面单塔换料后 `MaxDrift` 仍在 4cm 内、`MaxSettlement` 在 6cm 内则通过；真实掉落一层会远超沉降上限 |
| Gatehouse 或 TwinTowerBridge 将 `Levels` 调到 5 后预览全部消失，并显示 `BrickBudgetExceeded:51:50`（新版 Twin 为 `53:50`） | Builder 在生成完几何后执行 `MaxBrickCount` 硬预算。双塔主体每层 10 块；Gatehouse 总数为 `10*Levels+1`，TwinTowerBridge 总数为 `10*Levels+3`。默认预算 50，因此 5 层必然拒绝；`RebuildPreview` 在生成前先清空旧实例，使拒绝结果表现为“全部消失”。 | 这不是材质/HISM 丢失。临时测试可提高 Actor 的 `GenerationSettings.MaxBrickCount`；正式关卡优先保持 4 层以内，或在后续将非关键构件合并/HISM 化后再提高预算。 | 分别测试 Levels 4/5：默认预算下 4 层可生成，5 层稳定输出明确的 `BrickBudgetExceeded`；提高 MaxBrickCount 后 5 层恢复，且 active body 计数纳入性能验收。 |

## 9. M7.3-B2 结构弱点与失效验证

| 症状 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| Gatehouse 增加顶部关键角后报 `BrickPenetration`，或普通木楼板成为比玻璃弱柱更有效的旁路目标 | 将关键角顶冠放在塔顶木 Deck 时，新增支撑与中央门梁同高；简单向外挪开后，木 Deck 又会承托整个弱点段，破坏难度优势 | Gatehouse 的 `CriticalCorner` 改为落在两塔共用的顶层铁质门梁上：门梁顶部与弱支撑底部只接触，且强门梁不会成为低成本旁路弱点 | `ABTS.M73B2.StructuralWeaknessFailure` 中 Gatehouse 静态/优势校验通过；M7.1 预览无重叠和初始弹飞 |
| `TwinTowerBridge + Iron` 报 `B2TipMarginTooSmall:4.005:8.000`，而较轻 Carrier 可以通过 | Carrier 使用 `PrimaryMaterial`，Payload 固定为 Stone；Iron 的 `7.85g/cm3` 密度使受影响 COM 更受 Carrier 本体支配。只有比例偏置 `WeaknessBiasRatio=0.72` 时，换料后的 COM 没有足够绝对越界余量；这不是浮点误差 | 保留比例偏置，并在其后沿预测倾覆方向追加共享的 `WeaknessTipReserveCM=5.0`；Carrier 与 Payload 整体平移，随后再执行最终 Failure Probe，不用降低 `MinTipMarginCM` 掩盖失败 | 同一 Twin/Iron 用例由旧 `TipMargin=4.005cm` 提高到 `9.010cm`；192 组 Seed×轮廓×材质×层数矩阵全部满足 `Initial>=2`、`Tip>=8`、`Reseat<=0.35` |
| 弱点日志仍是 `[M7.3-B][WeakPoint]`，误以为 B2 没有运行 | 为兼容既有检索保留了日志标签，B2 没有另起一套平行弱点系统 | 通过 `Pattern/Collapse/InitialMargin/TipMargin/Reseat/Affected` 新字段识别 B2；Summary 同步显示模板和失效指标 | 默认三种轮廓日志均包含这些字段，`Affected>0`，Summary 与 Primary WeakPoint 一致 |
| 图探针通过但 Carrier 在 Chaos 中只局部碎落 | 美术或运行时把 B2 的单 Carrier 静默拆成多个独立刚体，破坏了一个 COM/Contact Hull 的验证前提 | 当前 Carrier 必须对应一个完整刚体；未来如需拼块外观，新增显式 RigidGroup 后再扩展验证 | Runtime Node→Module 映射中 Carrier 唯一；M7.1 击中弱支撑后 Carrier 与 Payload 作为同一载荷闭包倾覆 |
| 对角布置的两个石质 Payload 在物理启动时互相顶开 | Payload 是轴对齐盒体；只沿对角线使用固定中心距离会让 X/Y 两轴投影仍重叠 | 按垂直方向在 X/Y 上的最大投影反算中心半距，并保留 4cm 间隙；尺寸变化后重跑静态穿透校验 | TwinTowerBridge 不再出现 Payload-Payload `BrickPenetration`，空载不自发弹开 |
| Twin/Iron 空载在固定 `1.25s` 截止时由 Payload 产生 `MaxDrift=4.48cm`，主体旋转仅 `0.57°`，因此被误判失败 | 固定时刻只比较累计位移，没有判断接触是否已经收敛；截止时线速度仍为 `7.68cm/s`、角速度为 `1.63deg/s`。预校验得到 `Pairs=0/Repairs=0`，把 Tip Reserve 从 `5.0` 降到 `4.25` 也只把漂移改为 `4.42cm`，共同排除了初始穿透和 COM Reserve 是主因 | Idle 改为最短观察 `1.25s`、所有刚体连续低于 `4cm/s` 与 `1.5deg/s` 达 `0.45s` 才收敛、硬上限 `6s`；预校验复用 M7 `PenetrationValidator`，通过后 `Freeze()` 保留实际落座 Transform | fresh M7.1 运行在 `3.63s` 完成、`Stable=0.47`、`TimedOut=0`；最终 `MaxDrift=3.02cm`、`MaxSettlement=2.95cm`、`MaxRotation=0.53°`、速度 `1.44cm/s / 1.41deg/s`、`Accepted=1` |
| 四个 Seed 下 SingleTower 弱点仍只沿局部 Y 倾倒，或多塔弱点朝中心 Connector | 旧双支撑模板只用 `Corner.Y`，忽略 X 象限；多塔若原样保留内向弱侧，则会被中心 Connector 遮挡，或为了避让而离开承台 | `AsymmetricDualSupport` 的整条支撑线由 `Corner.X` 选择 `+X/-X`，弱柱由 `Corner.Y` 选择，`TipDirection` 使用完整四象限 Corner；Gate/Twin 将朝中心的弱侧镜像至所选塔外侧 | 192 组矩阵通过；四象限断言明确针对 SingleTower/双支撑，Gate/Twin 验证弱侧始终朝所选塔外侧，不要求同一多塔轮廓保留四个最终局部象限 |
| `IdlePenetrationValidation` 显示 `Repairs>0`，微调后看起来仍能站立 | 微调改变了真实 Module Transform，但 Builder 支撑边、Contact Hull 和 B2 `TipMargin` 仍基于原生成几何；继续验收会产生数据与 Chaos 不一致的假通过 | M7.3-A 将任意 `Repairs>0`、`LargeErrors>0` 或 `RemainingSmall>0` 直接判为 `IdlePenetrationInvalid`，不进入 quiet-window；从 Pivot、Simple Collision、生成尺寸和间隙上消除穿透 | 合格建筑预检必须为 `Repairs=0 LargeErrors=0 RemainingSmall=0`；出现修复时日志紧接 `PenetrationRejected=1 ... Accepted=0` |
| 三种 B2 Pattern 看起来相同，击毁弱点只掉顶部小段 | 基础主楼仍是四柱整板重复；`WeaknessStructureBuilder` 从 `HighestDeck` 添加共用的 Carrier 与两个 Payload；`PostFailureValidator` 从最高处 Carrier 向上收集，测试又固定断言受影响节点为 3 | 不继续用尺寸/材料掩盖；B2 保留为 Legacy 顶冠对照，新路线改为递归主体 Macro DAG，并从中下部 Failure Frontier 推导弱点，最终以真实 Contact DAG 和 Chaos 反事实验证 | 新路线要求主体受影响质量/高度跨度达到门槛、无旁路、弱点攻击显著优于普通攻击；详见 [M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) |

## 10. M7.3-DAG 递归主体建筑

| 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| 新增 DAG-1 源文件后，Unity Build 报 `WeakPointAnalysis.cpp` 与 `PostFailureValidator.cpp` 的匿名命名空间 `NodeMass` 重定义 | 两个旧 `.cpp` 都定义了同签名内部函数；此前恰好位于不同 Unity 编译单元，新文件改变 Adaptive Unity 分桶后进入同一 TU | 分别改为 `AnalysisNodeMass` 和 `FailureProbeNodeMass`，不改变质量公式与调用结果 | Editor 构建成功；fresh-process `ABTS.M73A/B/B2` 四项 Legacy 回归全部 Success |
| 扩大 `ExpansionStepBudget` 后旧递归路径改变 | 使用共享 `FRandomStream` 会让随机结果依赖调用总次数和遍历顺序 | DAG-1 以 `Seed + Version + Preset + DerivationPath + Salt` 生成路径级随机；候选顺序和规则选择使用不同 Salt | `ABTS.M73DAG.RecursiveExpansionDeterminism` 对短/长预算比较轨迹前缀 |
| 预估砖预算耗尽时整栋生成失败 | 若沿用 Legacy 的生成后总量检查，会再次出现 `51:50` 式整栋消失 | 每次规则应用前检查下一步抽象节点和预估砖成本；超限时保留当前合法 DAG 并标记 `bBudgetTerminated` | `ABTS.M73DAG.BudgetTermination` 验证仅一次二分可用时仍成功输出五个 Macro |
| DAG-1 图合法，但 DAG-2 生成 `DAGNoFeasibleSupport` | 并联/串联 Scope 后上下 Plate 的 XY 交集无法容纳双柱与净空；抽象逻辑不能凭空生成斜向支撑 | 调大目标 Scope、减少并联深度或 Gap；远程承载应交给未来显式 Beam/Span 原语，不能降低为未经审计的旁路柱 | `SelectedSupports` 全部对应实际 Plate→Column→Plate，任何无可行支撑的结构明确拒绝 |
| `TwinTowerBridge` 在旧 DAG-2 中 `MaxExpansionDepth=1` 出现 `DAGNoFeasibleSupport:0` | 双塔基准与递归 Parallel 产生不同数量的横向分支；中央 Plate 的左右窄入边需要共同承载，旧实现却要求每条入边单独容纳完整柱组 | DAG-2.1 已通过关联扁平化、内部单柱接口和自适应柱宽修复；无需再强制 `ParallelRuleWeight=0`。Beam/Span 仍用于未来真正无投影交集的跨距连接 | `ABTS.M73DAG.AssociativeNestingAndParallelFallback` 的 Twin 纯 Parallel 递归通过，Mapping 记录实际 Pattern 与柱宽 |
| DAG-2 预览正常，但 PIE 约 6 秒后整栋消失，日志 `IdleValidation ... TimedOut=1`，同时 Drift/Settlement/Rotation 都低于门槛 | 运行时把每块 Plate/Column 交给 Chaos 做隐藏静稳；接触堆在微小振动下可能长期保持 Awake，单个刚体线速度仅略高于 Quiet Window 阈值，旧逻辑把“超时”一律等同真实失稳并事务回滚 | 超时只在位移、平面漂移、沉降或旋转超过门槛时拒绝；若这些空间指标都合格，冻结全部模块并以 `BoundedTimeout=1 Accepted=1` 接受。真实滑移/倾覆仍拒绝 | 例如 `MaxDrift=2.31<4`、`MaxSettlement=1.03<6`、`MaxRotation=0.31<2`，即使 `MaxLinearSpeed=4.05`、`Awake=25`，也会保留建筑；超过任一空间门槛仍 `Accepted=0` |
| 开启递归 Parallel 后先报 `DAGSeriesScopeTooShort`，提高 `TargetHeightCM` 又报 `DAGNoFeasibleSupport` | 布局树曾对嵌套同类操作符重复均分 Scope；不同分支数的并联层又要求每条窄入边独立容纳完整三柱组。扩大总高度不能修复横向接口 | 同类 Series/Parallel 在布局时关联扁平化；窄入边可降为内部单柱接口并在 `MinAdaptiveColumnWidthCM` 下限内自适应宽度，多条入边以真实接触凸包共同判稳 | `ABTS.M73DAG.AssociativeNestingAndParallelFallback` 同时覆盖深层纯 Series 与 Twin 纯 Parallel 递归，实际 Pattern/柱宽保存在 Mapping |
| 扩大 `ExpansionStepBudget` 后报 `DAGSeriesScopeTooShort` / `DAGColumnTooShort`，或楼板间缺柱、出现贯通长柱 | 旧布局把表达式递归 Scope 的局部 Z 当作物理高度，但最终承载边在之后才选择；混合 Series/Parallel 后视觉层级与 Support DAG 不一致 | Scope 仅切分 XY；最终 Support DAG 以最长路径统一求结构层级和 Z；只接受相邻层物理支撑，非地基板必须拥有实际入支撑 | `ABTS.M73DAG.StructuralRankAndPhysicalContinuity` 高预算检查逐板连续性、单层跨度与最小柱高 |
| 拱门/双塔放大 Layout 后报 `ContactAreaTooSmall`，缩小后报 `DAGParallelScopeTooNarrow` | 固定柱宽和板厚使 Parallel 最小宽度与最低接触比例形成无解区间 | DAG2.2 将正常视觉目标和安全下限解耦；窄分支降低局部板宽/板厚，宽楼板按面积反推柱宽并可增加柱数 | `ABTS.M73DAG.AdaptivePlateAndColumnGeometry` 覆盖宽板和窄 Parallel |
| Parallel 后左右支撑区域均存在，但报 `COMOutsideSupportHull` | 旧 DAG2.2 按单条候选边独立计算接触面积并按距离截断；最终仅保留一侧柱组，或把左右联合承载误当成独立承载 | DAG2.3 从顶层向下传播累计质量与一阶力矩，以每块 Load Plate 的联合柱脚凸包覆盖累计合力点为选组条件 | 默认 TwinTowerBridge（Seed=7301、Budget=1）和 `ABTS.M73DAG.StructuralRankAndPhysicalContinuity` |
| 物理预览有柱，但审计报 `DAGMissingRequiredContact` 或 `DAGUnexpectedBypass` | authored 图与最终碰撞盒的真实触碰不同：柱太短、Clearance 过大，或 Plate 体量横向接触形成旁路 | 只以重建后的 Realized Contact DAG 判定；调 `PlateThicknessCM`、`ColumnClearanceCM`、Scope/Gap 后重新构建 | `ABTS.M73DAG.ScopeLayoutAndModuleCompilation` 要求 Missing=0 且 Bypass=0 |
| Idle 验证失败后建筑、地基一起消失，或担心不可见地基仍阻挡 Gameplay | 失败统一进入 `RejectRuntimeStructure`；这是刻意的事务回滚，而不是简单隐藏主体。继续保留失败模块或 Foundation 会让无效结构参与 M6/M7 碰撞 | 销毁所有 Runtime Module，清空 RuntimeModules、Node→Module 映射和 Idle 缓存，停止 Tick；隐藏 FoundationCap、清空 FoundationFeet，并把二者碰撞设为 `NoCollision`。后续重试通过 `UpdateFoundationComponents` 恢复 Foundation 可见性、实例与 `QueryAndPhysics` 碰撞，再重新装配验证 | `Accepted=0` 后场景中无该建筑模块、鸟和弹丸不会撞到隐形 Foundation；修复参数并重新初始化后 Foundation 和模块完整恢复，重新执行穿透与 quiet-window 验证 |
| 固定世界的 TargetBuilding 日志为 `Algorithm=0`，同一 Seed 的 Legacy Gatehouse 顶部 Payload 偶发旋转 `4.68°–5.87°`，而 `StartupPhysics Complete` 又先于 Actor Idle 结果 | 生产 TaskGraph 仍调用已退役的 Legacy/B2 顶冠；同时 M6 用更宽速度阈值监视同一批模块，并可能在 M7.3 完成历史位移/旋转判定前调用 `FreezeDynamicModules`，污染 quiet window；若只枚举已存在 Actor，MaterialSystem/Profile/Class/Spawn 失败还会把零栋误判成合法空关 | 三类普通 Task 统一经 `FABTSM7TaskGraphDAG23ProfileResolver` 进入安全 DAG2.3 Profile；旧 Blueprint CDO 的 Legacy 条目在边界升级且不得回退。M7 先登记必需 Actor 数并逐 Actor 注册、封口；M7.3 Actor 独占 Idle/Freeze，拒绝统一撤销 Foundation；M6 只检查合同注册集合，并仅在数量相符、Setup 未拒绝、每栋均显式 Accepted 时放行，Rejected/NotRequired 一律失败 | `ABTS.M7` 14/14、`ABTS.M73` 13/13；当前世界两次冷启动均 `BuildingContractSealed Expected=3 Registered=3 SetupRejected=0`、`Algorithm=1`、零穿透、三栋 Idle Accepted，Target MaxRotation=0.36°，`WorldReady` 最终含 `BuildingAccepted=3 BuildingRejected=0 BuildingExpected=3 BuildingRegistered=3` 且晚于所有 Idle |
| B2/Furnace 标签处没有建筑，但日志先有 `Bricks=13 Supports=18 Generated Accepted=1`，随后 `IdleValidation TimedOut=1 MaxRotation=2.02 Node=6/9 Accepted=0` 并 `WorldReadyBlocked` | 这不是 M3 或 DAG 漏生成；Idle Reject 会事务删除模块和 Foundation。旧 Tripod 点位 `(-a,-b)、(+a,-b)、(0,+b)` 的等面积质心偏到 `-b/3`，但求解器仍按三柱各 1/3 传播载荷，使顶点柱真实 50% 反力被低估；铁材质又只有约 4.2% 实际接触比，变步长 PIE 会产生边界微振。旧 `-benchmark` 使用固定时间步，因此会给出不代表 PIE 的假绿灯 | Layout 与载荷求解统一调用共享支撑几何；求解器保存已验收的权威柱中心，模块编译器只消费、不二次推导。Tripod 改为接触质心居中的 `(-a,-b/2)、(+a,-b/2)、(0,+b)`（另一轴转置），并逐对检查方柱 AABB 净空；最终柱宽放不下时显式降级并重算接触面积。Furnace 默认和旧显式 DAG CDO 都在 Resolver 边界强制 `MinSupportContactAreaRatio>=0.06`。保留 2°、6 秒、Reject cleanup 和必需建筑门禁，不增加验收专用阻尼 | `ABTS.M73` 13/13、`ABTS.M7` 14/14；`SupportPatternsAndHullValidation` 覆盖两轴质心、净空边界、单/多接口降级和编译结果；`TaskGraphDAG23ProfileRouting` 覆盖 6% 旧 CDO 升级、实际接触比和柱细长比。最终二进制三次不带 `-benchmark` 的 fresh D3D12 实时 60 FPS 中，Furnace 均 `TimedOut=0 Accepted=1`，旋转 `0.08°/0.09°/0.08°`、`DAGMinContact=0.060`，最终门禁均 `Accepted/Rejected/Expected/Registered=3/0/3/3` 且 `WorldReady=1`；仍需保留可见 PIE/hitch soak |

## 11. M10.1 远端落点画中画

| 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| PIE 中画中画边框和 `LANDING PREVIEW` 标题正常出现，日志也有 `Camera spawned` / `Activated`，但内部始终呈深色黑屏 | SceneCapture 与 RenderTarget 实际已经创建并捕获；问题发生在 HUD 合成。UE 5.8 的 `SCS_FinalColorLDR` 只保证 RGB，默认桌面 Tonemapper 在未启用 Alpha 传播时把输出 Alpha 保持为 0。`UCanvas::K2_DrawTexture` 默认使用 `BLEND_Translucent`，因此有效 RGB 被零 Alpha 完全滤掉，只剩画框底色 | 绘制远端 RenderTarget 时显式传入 `BLEND_Opaque`，直接消费捕获的 RGB；不为单个 HUD 画中画全局开启 `r.PostProcessing.PropagateAlpha` | Editor Development 编译成功；PIE 重复 M10.1-B 流程时画框内能看到落点地形和浅色末段轨迹，松开左键或落点离开侦察圆后当帧隐藏；日志不出现新的 Renderer/Material Error |

## 12. M9 卫星与标定入口

| 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| 卫星视觉存在，但强化弹弓不调高功率就完全够不到；标定日志中 `SatelliteRadiusFromPrimary` 接近预期值的两倍 | 卫星以 `FTransform::Identity` deferred spawn，`ConfigureFromPrimaryPlanet/Direction` 又在 Finish 前移动原生 Root；随后把已经移动后的 `GetActorTransform()` 传给 `FinishSpawningActor`。UE 5.8 会相对缓存的原始 deferred Transform 重算模板并再次组合这段平移，导致卫星中心位移近似翻倍 | Finish 时传回原始 `FTransform::Identity`；卫星保存 `ConfiguredCenterWorld`，Finish 后用 `IsAtConfiguredCenter()` fail closed，避免生命周期调整再次静默改变位置。隔离标定预设不写回生产 M9 默认值 | 按[标定详稿第 7.3 节](M6M9SlingshotSatelliteCalibrationDesign.md#73-生产-m9-deferred-transform-回归)单列运行生产回归：标定载体中主星半径约 `10000cm`、半径/离地比例均为 `0.125` 时，卫星球心距主星约 `11480cm` 而非 `22960cm`；生产 M9 必须唯一 ready、`Radius=1250.0 Clearance=1250.0 Gravity=245.0 FinaleGravitySource=0`，且无 center/rejected/error。标定侧还须以真实 M6 pouch 和相机 `Look/ScreenUp/ScreenRight` 投影平面的离散可达 Pull × 规则 `AimPlaneOffsetCM` 网格证明 Reinforced 存在成功岛、Simple 和认证功率带外均为 0 命中；当前 fresh 自动化/runtime 与可见 PIE 证据未补齐前不得写成已通过 |

## 13. 多工作树排错提炼与持续同步

### 13.1 总文档与阶段账本的职责

本节是三份功能工作树排错账本的集成提炼层，不取代原始记录：

- M3、M7、M11 工作树继续在各自账本中追加完整的“现象—根因—修复—防回归验证”、失败假设、提交和阶段证据；不得因总文档已摘录而删除历史。
- 集成工作树只把根因已经稳定、能跨阶段复用或会影响交界验收的结论整理到本文。仍在验证的推断必须保留“开放/候选未认证”等状态。
- 总文档不复制会持续变化的整段参数和逐次实验数据；需要复盘时以子文档中的原始 ID、日志和提交为准。
- 后续摘录先比较上次基线之后的子文档差异，再更新本文和基线；功能工作树不得直接修改本文。

| 原始账本 | 主要职责 | 本次摘录基线（截至 2026-08-05） |
| --- | --- | --- |
| [M3 专属工作树排错记录](M3WorktreeTroubleshootingLog.md) | 月度 PCG 候选、真实地表、弹弓/卫星/槽位消费链、M3 与 M5.1/M6/M7/M9/M11 的分诊 | `2ddc974978c4c717db967b048209258dbca80c04` |
| [M7 功能工作树排错记录](M7WorktreeTroubleshooting.md) | 建筑候选搜索、结构 IR、真实接触、Chaos 稳定门、难度与视觉阶梯 | `fdf45d4875b7a9b30967f961d5f4acd00d4a07f9` |
| [M11 工作树排错记录](M11WorktreeTroubleshooting.md) | 终局 Core、候选/认证/绑定、异步生命周期、HUD/PIP、权威路径播放 | `51b391d8e1068d1c2a030fe64667ec21418de33c` |

### 13.2 跨阶段统一诊断顺序

三个工作树反复出现的共同根因不是“算法没运行”，而是观察者读取了错误的权威层。统一按以下顺序排查：

1. **先确认工作树和二进制身份。** 记录 `git rev-parse --show-toplevel`、当前分支、`git status --short`、`.uproject` 绝对路径、Editor 命令行和日志路径。不能用 Codex 工作树目录编号或进程名推断归属。
2. **再标明证据层。** 区分编辑器预览、Preview/Test Candidate、正式生产消费、NullRHI 数据合同、实时 Chaos、SceneCapture 像素和可见 PIE；任一层通过都不能代替另一层。
3. **找到本阶段唯一权威。** M3 以带 `SourceResultHash + CandidateHash` 的候选/正式发布结果为准；M7 以最终 Brick 重建的 Realized Contact DAG 和建筑合同为准；M11 以标准 C++ Core 的 Result、事件序列和认证 Hash 为准。
4. **沿链路找第一个缺失证据。** 例如“建筑不见了”应按 Candidate → Spawn → Generated → IdleValidation → BuildingGate → WorldReady 查找；“轨迹没偏转”应按 Production Profile → Pouch Frame → Surface → Gravity Query → Preview/Flight parity 查找。不要从最终截图反推上游一定没有执行。
5. **保存可复现身份。** 日志至少包含 Seed、算法/版本、Profile、布局或候选 Hash、权威状态和拒绝原因。确定性问题先比较身份是否相同，再比较画面或性能。
6. **分层验收。** 单元/合同自动化证明纯数据，fresh NullRHI 证明无渲染生命周期，实时运行证明 Chaos/线程时序，可见 PIE 证明构图、像素、输入和手感。`-benchmark` 固定时间步不能代替实时 PIE。
7. **失败应 fail closed。** 候选、建筑或认证失败时不得回退旧布局、发布半成品、保留隐形碰撞或把 Preview/Test 晋升为生产结果。

### 13.3 M3：候选、真实世界与消费链

| 现象或风险 | 稳定结论 | 首要回归证据 |
| --- | --- | --- |
| 冻结射程已经接入，实体建筑和走廊却看似仍在旧位置 | 冻结档位是能力包络，不是每关固定距离；候选逻辑位置与兼容 TaskGraph 实体是两条链。R-6 正式发布前只能用候选叠层和身份日志验收，不能用旧实体作反证。 | 同档位各关厘米距离严格递增；Candidate Hash 随档位变化；日志明确 `MonthlyAccepted` 和 Authority。详见子文档 `M3-R3-001/002`。 |
| F7/测试地图中的区域、卫星或终局槽位置正确 | Preview/Test 只消费显式候选，不代表月度世界已经发布。禁止默认取 `RetainedCandidates[0]`，跨消费者必须用同一 `SourceResultHash + CandidateHash` Join。 | 分别运行无预览参数和显式 Candidate；前者不迁移生产世界，后者打印完整来源身份且仍为 `MonthlyAccepted=0`。详见 `M3-R5-001/002`。 |
| 弹弓埋地、朝向错误，或候选预测与实际发射不同 | 球面规划点不是运行时发射帧。两根桩分别查询真实地表，最终朝向、卫星锚点和轨迹都从真实桩顶及 `GetRestPouchTransform()` 派生；不得用道路切线、共同高度或固定桩距伪造。 | 两桩贴地误差、Pouch/卫星中心差、朝向误差和补偿角进入日志与 Hash。详见 `M3-R51-001/002`。 |
| `SatelliteGravity=1` 但没有可见偏转 | 布尔开关不能证明生产链闭环。生产 M6 必须消费冻结 Launch Profile；候选和运行时共用真实表面/Pouch 帧；预演与实飞共用 M9 引力查询，不复制公式。 | Profile/Candidate Hash 相符；同输入 gravity-on 命中且 gravity-off miss；fresh Standalone 在实际游戏进程设置 CVar。详见 `M3-R51-003`。 |
| 自动装配的弦袋无法点击 | 视觉上重叠的桩组件先阻挡了 `ECC_Visibility`，交互射线没有到达 Pouch。装配后 Pouch 应是 Cord 唯一 Visibility 点击目标。 | `CordVisibilityTargets=1`、`PouchVisibilityTargets=1`，PIE 点击袋口进入发射且桩不抢占。详见 `M3-R51-004`。 |
| 太空槽在起伏球面被旧“共面/同高”门拒绝 | 两端应各自贴合真实地表；局部帧用两槽中点、切平面 Right、道路 Forward 和径向 Up 正交化，检查右手系与有界倾角，而不是强行拉平。 | 两槽各自贴地、Origin 等于中点、Frame 非退化且 Hash 可复现。详见 `M3-R52-001/002`。 |
| 建筑先生成后消失 | `[Generated] Accepted=1` 只表示生成期通过；后续 M7 Idle Reject 会事务删除模块和 Foundation。M3 不应通过移动 Cell 或保留失败 Actor 掩盖它。 | 等待建筑合同封口，要求 `Expected=Registered=Accepted` 且 `Rejected=0` 后才发布 WorldReady。详见 `M3-X-001`。 |
| T2-B 风格语义在 Preview/Test 与生产运行时漂移，或 HISM 产生逐实例注册 | 地图名、Actor 名称、Transform 和当前相机都不是权威身份。M3 只读适配器只接受精确 Planet/Component、卫星运行时 Actor 或已验证 Candidate/Result；未知对象返回 `None`。树石以 HISM 组件批次发布，不逐实例展开。适配器不保存 Profile、Stencil、Authority 或 Hash。 | `ABTS.M3.StylizedSemantics` 验证映射、确定性、批次粒度、fail closed 及 Custom Depth/Stencil 状态不变；`ABTS.M3.Monthly.SatellitePreview` 验证 Preview 与生产卫星/E5 语义且 Result/Candidate/Runtime Hash、月度权威和引力开关不变。详见 `M3-T2B-001`。 |
| Integration 需要接线地面/月面 SceneCapture，但 M3 没有稳定访问入口 | 捕获 owner/component 位于共享 M10 私有成员，功能树不能靠名称或组件扫描绕过所有权。M3 只记录需求；Integration 应在共享类型增加 const getter，并用现有 Preview Subject 显式映射 `GroundLandingPreview` / `SatelliteLandingPreview`，缺失或未知时 fail closed。 | M3 提交不得修改共享 Camera/M10 类型；Integration 后续测试检查 Subject 映射及接线前后 M3/M9 Gameplay 身份不变。详见 `M3-T2B-002`。 |
| 性能门单次轻微越线 | 保留首次失败；先核对 Seed/Oracle/Hash，再停止并行重型任务，以相同二进制 fresh 隔离重跑。既不能直接忽略，也不能在身份未变时立即断言算法回归。 | 同时保存 P50/P95/Max、接受数、Oracle Hash、命令和首次失败日志。详见 `M3-TEST-001`。 |

### 13.4 M7：语义结构、最终几何与物理权威

| 现象或风险 | 稳定结论 | 首要回归证据 |
| --- | --- | --- |
| 同为 UE 5.8 却提示模块缺失或版本不同 | 安装版与源码版 Editor 的 BuildId 不同。必须用项目绑定的安装版全链接，不能复制其他工作树 DLL 或依赖 Hot Reload。 | 安装版默认构建和 `-ForceUnity -DisableAdaptiveUnity` 全链接成功，fresh Editor 可加载两个模块。详见 `M7-WT-002`。 |
| 编辑器中建筑/弱点存在，PIE 却不同或失败 | 无碰撞 HISM 预览、运行时 Chaos 模块和诊断覆盖层是三种生命周期。诊断组件不得进入 Game World；视觉预览不证明 Idle 通过。 | Summary 含启用状态和 Realized Hash；实时 PIE 所有必需建筑 `IdleValidation Accepted=1`；游戏中无诊断组件。详见 `M7-DAG3-001`、`M7-DAG4-001`。 |
| 所有 Seed 都在同一预算门失败，或提高深度后整栋消失 | 候选搜索不是无限重抽；下游流、估算 Brick、真实 Brick、递归和搜索预算必须成组匹配。预算不足应在规则应用前终止并保留合法抽象前缀，或明确拒绝，不发布半成品。 | 日志给出实际值/上限和具体下游门；K>1 对可解域提高成功率，数学无解配置仍前置拒绝。详见 `M7-DAG3-003`、`M7-DAG5-001`～`004`。 |
| Port/WFC/DAG 通过但积木悬空、重叠或桥端留缝 | 语义兼容、中心线 Joint 或 authored Bearing 都不是最终物理接触。所有 Motif 必须编译回统一 Assembly IR，并做全建筑闭合；桥端按每根梁、每一端审计 Bearing。 | `RemainingPenetrationCount=0`、`UnsupportedMemberCount=0`、桥端逐梁承托、跨中无补救地柱。详见 `M7-BA-*`、`M7-BB-*`。 |
| Load DAG 通过但最终 Brick 有旁路或局部倾覆 | 最终 Brick AABB 重建的 Real Contact 才是权威；承重层还要检查累计合力是否落入联合支撑区域，不能只证明存在 Ground 可达路径。 | `RealContactMismatchCount=0`、支撑违规为 0；补柱后重新运行接触和 Load DAG。详见 `M7-BC-002/003`。 |
| 屋顶分层够不到、裂成多个小屋顶或闭合循环耗时失控 | 屋顶要逐层直接 Bearing；相邻同标高终端先聚合 Crown，Prism 屋脊沿长轴；每轮闭合记录违规签名，无进展则转入明确补支撑、裁剪或 fail closed。 | 固定种子屋顶无补救长柱，聚合不跨大空洞，闭合轮数有界且最终穿透/悬空为 0。详见 `M7-D1-003`～`007`。 |
| Chaos 固定时间步通过，实时 PIE 仍漂移或拒绝 | 静态几何正确不等于变步长接触稳定。保留严格空间门、quiet window、硬上限和事务回滚；`-benchmark` 只用于算法回归。 | fresh 实时运行记录 Drift/Settlement/Rotation、速度、Awake、TimedOut 和合同封口；失败后无模块或隐形 Foundation。 |

### 13.5 M11：唯一 Core、认证与表现消费

| 现象或风险 | 稳定结论 | 首要回归证据 |
| --- | --- | --- |
| 单文件/Non-Unity 编译通过，集成 Unity 构建发生调用歧义或重定义 | 匿名命名空间不能防止 Unity 同翻译单元冲突；私有辅助函数也必须使用文件职责唯一名称。 | 默认 Development Editor 与强制 Unity 全链接都通过。详见 M11 子文档第 2 节。 |
| Worker 发布结果时触发 GameThread/Renderer ensure，或快速输入显示旧结果 | Worker 只计算纯数据并返回 `TFuture`；原生 Game Thread Tick 轮询并发布。求解采用 revision/dirty/latest-only 语义，丢弃 stale result；SceneCapture 不随每次微调高频重拍。 | worker 不触碰 UObject/HUD/SceneCapture；日志有 solve、端到端 latency、discarded；fresh 可见 PIE 无线程 ensure。 |
| 终局相机没有继承已经调好的 Blueprint 参数 | 直接生成原生基类绕过了 M6 运行时选择的 BP 子类。M11 必须解析并生成唯一 M6-owned 精确相机类，歧义时 fail closed。 | 自动化验证精确类相等，日志输出 `CameraClassParity`，可见 PIE 比较构图。 |
| SceneCapture 中有目标却没有解析轨迹，或 PIP 随微调乱晃 | SceneCapture 只能捕获场景组件，纯数据轨迹必须由 HUD 叠加；背景相机按目标缓存，同目标新解只更新线条。AUTO 与 Probe 共用同一权威结果和框体，但观察锚点不同。 | ResultHash 不因 AUTO/Probe 改变；同目标背景稳定、轨迹即时更新；切目标才重构图。 |
| PIE 手感或 5000 点 ScreenAim 合格，却被误认为可正式绑定 | Candidate、Certified、Production Binding 是三个状态。二维满功率 ScreenAim 只证明手感/Hull，不能证明三维 `Yaw × Pitch × Power` 完整输入域。 | 报表标明 Domain、Power slice、采样和邻接；正式门覆盖完整 Power，且 F4 六邻域唯一连通、前缀集合嵌套、消融/错序/多圈/旁路失败。 |
| 轨迹最终到达 UFO，但 `TargetHit` 早于第三次引力弹弓退出 | 事件顺序是认证语义的一部分，不能只检查“曾进入所有包络”。 | Core/CLI/Runtime 统一要求 `Assist3 Exit → TargetApproach → TargetHit`，错序命中归为失败。 |
| 放大模型后出现视觉穿模，或统一缩放后候选失效 | `VisualRadius`、解析 `CollisionRadius`、`InfluenceRadius` 职责不同；离散积分、角域、步长、事件阈值和评分也破坏简单相似缩放。 | 尺度变化后重新跑 Core parity、扫掠、事件、Hull 和完整域认证；最小近掠距离包含鸟体净空，候选 Hash 改变。 |
| HUD 控件绘制位置与点击热区偏移，或全览图元溢出圆框 | 输入必须依次转换 raw viewport、`UnscaledViewRect` origin/size、DPI 和 Canvas logical space；所有线、圆、文本和 hit test 共用变换与圆裁剪。 | 自动化覆盖 DPI≠1、非零 viewport origin 和线宽/文本 bounds；可见 PIE 改窗口尺寸后仍像素对齐。 |
| 发射后鸟/UFO 在远景中不可读 | 当前仅确认可能由 Flight Camera lag 和共享雾/云叠加造成，仍是开放项；不得把推断写成已修复。 | 分别做无 lag、无雾云和同时修改的 fresh 可见 PIE A/B；共享天空/地图修改仍由集成工作树执行。 |

### 13.6 后续摘录流程

1. 功能工作树在原条目追加新证据、被推翻的假设和当前状态，不删除历史，也不直接修改本文。
2. 集成时按本节记录的摘录基线查看三份子文档增量；只上收已稳定根因、跨工作树分诊规则和正式验收门。
3. 若子文档条目仍为“开放/候选未认证”，本文只保留风险、验证方法和所有权，不写成已解决。
4. 更新本文后，把对应子文档最新提交写成新的摘录基线；三个子文档仍作为完整证据源继续维护。
5. Markdown-only 摘录不要求 UE 编译；若摘录同时修改源码、配置、Blueprint、地图或稳定契约，则按多工作树规范执行相应构建、自动化和 PIE 门。

## 14. 风格化描边的锯齿、抖动与真实 RHI 绑定

| 现象 | 根因 | 修复 | 防回归验证 |
| --- | --- | --- | --- |
| T2-A 静态截图能看到描边，但地形、鸟、物体和建筑边缘有明显像素锯齿；相机或对象运动时线条抖动 | 初版轮廓订阅 `Tonemap` 后扩展点，晚于 UE 5.8 的 TSR/TAA。Temporal Jitter 改变每帧 Depth/Normal 边缘采样位置，而硬四邻域 `max` 把细小变化放大为二值跳变；最终线条没有再经过时域稳定 | 把轮廓独立为 `AfterDOF` 的 `ABTS Stylized OutlinePreTSR`，在 TSR/TAA 前用深度与法线八邻域累积连续覆盖率；按内部/最终 Viewport 比例换算宽度。T1 色调保留在 Tonemap 后的独立 `ABTS Stylized Tone`，运行身份升级为版本 3 | 强制 Unity；fresh NullRHI `ABTS.Rendering.Toon`；fresh D3D12 8/8 截图；Style On 的两个 pass 合计 `<=1.5 ms @ 1080p` 且 Outline `<=1.0 ms`；可见 PIE 旋转相机并让鸟/建筑运动，确认轮廓不再出现不可接受的闪烁。静态图不能替代最后一项 |
| NullRHI 自动化通过，但真实 D3D12 首帧在新描边 Shader 上报告 `View` 未绑定并因 `Missing uniform buffer` 退出 | `ViewportUVToBufferUV` 间接读取 `FViewUniformShaderParameters`；NullRHI 不执行真实绘制，无法暴露 Shader 参数绑定缺口 | 在描边 pass 参数结构中显式声明 `SHADER_PARAMETER_STRUCT_REF(FViewUniformShaderParameters, View)`，并绑定当前 `View.ViewUniformBuffer` | 每次改变后处理阶段、Scene Texture 或 Viewport UV 换算后，都必须重新起 fresh 真实 RHI 进程完成 Shader 冷编译和至少一轮 8/8 截图；不得以编译或 NullRHI 通过代替 |
