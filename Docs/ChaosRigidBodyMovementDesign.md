# Chaos 刚体球面移动与碰撞设计

> 状态：已实现并完成 PIE 验收。当前推荐的角色移动路线为 `ChaosRigidBody`。
>
> 前置：球面环境见 [M2PlanetSurfaceDesign.md](M2PlanetSurfaceDesign.md)，M2.5 的历史运动路线见 [M25RadialGravityAndJumpDesign.md](M25RadialGravityAndJumpDesign.md)，地表碰撞/材质物理参数见 [M52CollisionAndMovementDesign.md](M52CollisionAndMovementDesign.md)。

## 1. 目标与范围

本方案将四只鸟的角色碰撞和位移交给 Chaos 刚体，解决旧 `ForceSuspension` 与 `LegacySweep` 路线在连续球面、坡面和切换主控后可能出现的贴地漂移、接地不稳定与跳跃失败问题。

它保留项目的球面规则：重力永远指向球心；角色的视觉 Up 始终朝外径向；WASD 永远在当前球面切平面内移动。`CellTopo` 仍是地形、道路、水网和玩法的唯一逻辑源。连续网格及 Chaos 碰撞只负责表现与物理接触，不能反向修改 CellTopo。

本阶段不实现网络预测、游泳、移动平台、全物理鸟群互撞，或由地面材质驱动的复杂各向异性摩擦。M6 发射中的弹道和冲击可复用同一刚体及径向重力接口。

## 2. 运行结构

```text
AABTSM25BirdCharacter
├─ ChaosPhysicsSphere（Chaos 模拟根刚体）
│  ├─ Simple sphere collision
│  ├─ QueryAndPhysics + CCD
│  ├─ Object Type: Pawn
│  └─ 负责地形、HISM、建筑的物理接触
├─ CapsuleComponent（附着到球体；NoCollision）
├─ BirdVisual（只负责可见模型与朝向）
└─ UABTSChaosBirdMovementComponent
   ├─ 每帧施加指向球心的重力
   ├─ 将输入速度收敛至球面切向目标速度
   ├─ 依据碰撞法线判定接地
   └─ 接地后施加径向跳跃速度
```

角色切换到 `ChaosRigidBody` 时，`ChaosPhysicsSphere` 从初始胶囊附着状态切换为 Actor 根组件；原胶囊附着到球体并关闭碰撞。刚体使用引擎 `/Engine/BasicShapes/Sphere` 的可模拟简单球体碰撞，半径按原胶囊半径缩放。

不能用“Complex Collision As Simple”替代该球体的动态碰撞体：Chaos 不支持将复杂三角网格作为可模拟刚体的简单碰撞。因此小鸟本身稳定的物理解是简单球形刚体；地表可作为静态复杂网格参与碰撞。

## 3. 力、控制与接地

每个物理步先计算当前位置的径向外侧单位向量 `Up`：

```text
Up = Normalize(BirdPosition - PlanetCenter)
GravityForce = -Up * Mass * GravityAcceleration
```

WASD 先由 M4 Orbit Camera 计算相机相对的切向方向，再投影到 `Up` 的正交平面。移动组件把当前切向速度向目标速度收敛：有输入时使用 `GroundAccelerationCMPerSec2`，无输入时使用 `GroundBrakingCMPerSec2`。因此输入是“受限速度伺服”，而不是无限叠加的推力。

碰撞回调中，若 `ImpactNormal` 与命中点径向 Up 的夹角不大于 `CollisionGroundMaxAngleDegrees`，角色标记为接地。接地时按 Space 将刚体速度改为“保留切向速度 + `Up * JumpSpeedCMPerSec`”，并立即清除接地标记，避免本帧重复起跳。空中会施加 `AirDragPerSecond`，发射状态使用单独的弹道空气阻力。

鸟群彼此使用 Pawn 通道 `Ignore`，避免队列跟随或主控切换时互相顶死；树、岩石、地表和建筑保持其 WorldStatic/交互对象的阻挡响应。

## 4. 编辑器配置

1. 在目标地图中放置唯一一个 `ABTSMovementModeSelector`。
2. 在 Details 的 `ABTS | Movement` 中，将 `Movement Mode` 设为 `Chaos Rigid Body`。
3. 将所有此前为定位问题而增加的 M4 跟随、切换、输入、接地相关实验开关关闭，使用正常的鸟群跟随和控制链路。
4. 确认 `ABTSM3Planet` 的连续地表启用碰撞；树木、岩石等 HISM 需要其 Static Mesh 拥有可用的 Simple Collision，并在实例组件上启用碰撞。
5. 鸟群、工作台和熔炉不能作为 Pawn 阻挡物：它们应忽略 Pawn 通道；需要点击或射线命中的交互仍可保留 Query 碰撞。
6. 若需调手感，选择鸟 Blueprint/C++ 默认对象中的 `ChaosMovement`，在 `ABTS | Chaos Movement` 下调节：
   - `GravityAccelerationCMPerSec2`：径向重力；默认 980。
   - `MaxGroundSpeedCMPerSec`、`GroundAccelerationCMPerSec2`、`GroundBrakingCMPerSec2`：行走速度与加减速。
   - `JumpSpeedCMPerSec`：跳跃初速度；默认 620。
   - `CollisionGroundMaxAngleDegrees`：可视为地面的最大坡角；默认 55 度。
   - `AirDragPerSecond`：空中速度衰减。

同一关卡只应放置一个 `ABTSMovementModeSelector`。若未放置，角色使用 Pawn 类默认的移动方式；为确保验收一致，建议显式放置并指定 Chaos。

## 5. 发射状态衔接

进入弹弓袋时，角色关闭当前移动体的碰撞和 Chaos 模拟并传送至弹丸袋；发射时恢复 Chaos 碰撞、开启刚体模拟并写入初速度，同时切入 `BallisticFlight`。此状态不消费 WASD，只持续施加径向重力及弹道阻力。飞行结束、回收或回到弹弓时，会清除刚体线速度和角速度后恢复正常控制。

这使 M6 的撞击回调可直接读取发射前速度和碰撞法线，用于判断撞开、破坏与黑鸟爆炸，而无需为发射鸟另建一条物理链路。

## 6. 验收清单

1. PIE 启动日志包含：

```text
[ABTS][MovementMode] Active=ChaosRigidBody ... ChaosTick=1
[ABTS][ChaosMovement] DedicatedSphereBody=1 ... Root=ChaosPhysicsSphere
[ABTS][ChaosMovement] Enabled=1 Body=ChaosPhysicsSphere
```

2. 四只鸟在道路、坡面和地形交界处均可连续行走；松开按键后平稳减速，不出现旧路线的贴地漂移。
3. 任意鸟在 Tab 或 HUD 切为主控后，立即可移动和跳跃；不会因其他鸟或工作台贴近而被顶死。
4. 空格仅在接地后起跳，起跳方向始终为当前位置的径向外侧；从高处落下会与地表发生真实刚体碰撞而非穿过后再被高度查询拉回。
5. 鸟之间可穿过，树、岩石、建筑和地表不可穿过；高速测试时 CCD 不应出现明显穿透。
6. 在 Standalone 新进程重复以上测试，确认不是 PIE 旧状态残留。

## 7. 排错

| 现象 | 优先检查 |
| --- | --- |
| 小鸟掉进地表 | 地表 ProceduralMesh/静态地表是否真正启用 Physics 碰撞；检查 `ChaosPhysicsSphere` 是否为 `QueryAndPhysics` 且 `Simulate Physics` 已开启。 |
| 小鸟无碰撞或仍使用胶囊 | 查看 `DedicatedSphereBody=1` 日志；Chaos 模式下 Root 应为 `ChaosPhysicsSphere`，Capsule 应为 `NoCollision`。 |
| 靠近队友或工作台不能移动 | 检查 Pawn 通道：鸟群与静态交互站应为 `Ignore Pawn`，不要把它们设成 WorldStatic 对鸟阻挡。 |
| 在陡坡不能跳 | 检查命中法线与径向 Up 的夹角；适度提高 `CollisionGroundMaxAngleDegrees`，但不要高到把墙面当作地面。 |
| 角色空中旋转或模型朝向不对 | 刚体 Angular Damping 应保持较高；模型朝向由 `UpdateChaosVisualFrame` 根据径向 Up 和切向速度更新，而不应直接继承刚体滚转。 |
| HISM 可见却能穿过 | 对应 Static Mesh 缺少 Simple Collision，或 HISM 未开启 Collision；动态刚体不依赖材质 SDF。 |

## 8. 代码索引

| 文件 | 职责 |
| --- | --- |
| `Source/ABTSRuntime/Public/Movement/ABTSChaosBirdMovementComponent.h` | Chaos 运动参数、接地与弹道接口。 |
| `Source/ABTSRuntime/Private/Movement/ABTSChaosBirdMovementComponent.cpp` | 径向重力、速度伺服、碰撞接地、跳跃和弹道冲击回调。 |
| `Source/ABTSRuntime/Public/Player/ABTSM25BirdCharacter.h` | 三条可选移动路径和 Chaos 刚体声明。 |
| `Source/ABTSRuntime/Private/Player/ABTSM25BirdCharacter.cpp` | 刚体根组件配置、胶囊停用、输入分发、视觉朝向、弹弓状态衔接。 |
| `Source/ABTSRuntime/Public/Movement/ABTSMovementModeSelector.h` | 关卡级移动模式选择器。 |
