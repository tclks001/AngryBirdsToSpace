# AngryBirdsToSpace 开发排错记录

> 编码：UTF-8，简体中文。
>
> 用途：沉淀本项目已经遇到或已经被设计约束覆盖的问题。新增问题时，记录“现象—根因—修复—防回归验证”，不要只保留最后一次临时改动。

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

## 6. PCG 与后续玩法预防项

| 风险 | 约束与验证 |
| --- | --- |
| 简易弹弓配方锁死木材来源 | 简易弹弓只消耗树枝和石料；棱喙砍树才提供木材。 |
| 河网纯视觉、主线被随机阻断或绕过 | 河段、道路与桥梁全部是 CellTopo 边状态；生成后按 Key 阶段运行可达性验证。 |
| 建筑在球面随意倾斜或相邻判定依赖世界距离 | 建筑固定于平缓 Cell 中心；朝向使用局部径向 Up；熔炉/工作台等联动只用 `NeighborCellIds`。 |
| 青翎跨河采集绕过桥梁门 | 初版允许短时侦察、自动回归与信息解锁；不允许携带主线资源、建桥或完成关键配方。 |
