# M6：弹弓发射、弹道与碰撞破坏

> 状态：首版 C++ 发射闭环已实现；M5.1→M6 的厘米长度、三维障碍与失败原子装配门已通过 fresh 自动化和兼容世界 PIE。月度六关槽场仍等待 R4/R6 唯一 Candidate 后单独验收。
>
> 物理碰撞爽感、阈值悖论和后续损伤/结构升级建议见 [PhysicsImpactDestructionResearch.md](PhysicsImpactDestructionResearch.md)。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M5.1 弹弓装配](M51WorldItemsPlacementSlingshotDesign.md) · [M5.2 碰撞](M52CollisionAndMovementDesign.md) · [M6 视觉协议](M6SlingshotVisualPresentationDesign.md) · [M7 材料与装置](M7BuildingMaterialsAndDevicesDesign.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md) · [M10.1 超视距目标与引力走廊](M101BeyondHorizonLaunchInterfaceDesign.md)

## 1. 阶段目标

M6 验收从“已连接弹弓弦”开始，覆盖进入发射、弹丸袋拖拽、拉力、球面万有引力弹道预览、发射飞行、HISM 动态化/破坏、黑鸟爆炸、落地静默判断、物体静态化和队伍回归。暂不产生材料掉落，也不自动回收已冻结的动态代理。

CellTopo 继续负责地形与 Gameplay 语义；连续球面负责碰撞表现。M6 不把全图树石 Actor 化，只将实际被撞开的 HISM 实例提升为动态代理 Actor。

## 2. 状态机

```text
Inactive
  -- 点击可用弹丸袋 --> Ready
  -- 左键按中弹丸袋 --> Pulling
  -- 松开左键 -------> Flying
  -- 发射鸟落地 -----> Settling
  -- 全部动态刚体持续稳定，或达到最大等待时间 --> Returning
  -- 回归完成 -------> Inactive
```

进入 Ready 后：当前主控鸟进入弹丸袋、Force Movement 暂停，鸟群跟随暂停，其余鸟移动到弹弓后方左右两侧；WSAD、Tab 和普通相机输入被控制器阻断。退出后恢复鸟群跟随、普通移动与 M4 Party Camera。

## 3. 弹弓适配性

- SimpleStake 弹弓不能发射 `Reinforced` 能力的黑鸟；
- ReinforcedStake 弹弓可以发射四只鸟；
- 青鸟 TwigScout、红鸟和黄鸟可使用简易弹弓；
- `Twig` 树枝弹弓只接受 `TwigScout` 能力（默认且正式配置为青翎），完整发射结束事件会保留回飞前的最终落点供 [M10 侦察小地图](M10ScoutMinimapDesign.md) 使用；
- 当前验证由 `AABTSM51SlingshotCord` 保存两根桩引用与端点，不依赖渲染 Mesh 猜测类型。

### 3.1 进入 M6 前的装配几何门

M6 只接受已由 M5.1 完整提交的 Cord Actor。普通 Twig/Simple/Reinforced 弦在提交前使用
`FABTSM6CordConnectionRules` 对两桩顶部端点执行纯数据三维验证：

- 世界空间长度不超过当前兼容值或已接受月度槽快照中的 `MaxCordLengthCM`；
- 与除两端外所有已插桩中心线保持“桩半径 + 候选弦半径 + Clearance”以上净空；
- 与所有既有弦端点线段保持“候选弦半径 + 既有弦半径 + Clearance”以上净空；
- 非有限值、退化段、交叉、接触和净空边界均 fail closed；计算使用显式线段距离，不读取碰撞或渲染结果。

普通槽的 Field、Encounter 和槽组身份不构成配对权限。Space 档仍额外要求 M11.0 唯一 Finale Pair 的同
`PairId` 左右两侧；普通月度参数不能改变该 Pair 的身份。只有几何、Actor 生成与材料扣除均成功后才提交两端
`HasCord`，失败不得给 M6 留下半成品弹弓。

## 4. 瞄准、拉力与弹道

发射相机位于弹弓后方、略抬高并略仰视，屏幕 Up 由弹弓位置的球面径向约束，不允许 Roll。左键按住弹丸袋后，鼠标射线与当前拉伸平面相交，弹丸袋中心使用该交点，因此拖拽时中心跟随鼠标。

鼠标滚轮：

- 向下滚：`PullAlpha` 增大，袋子沿反发射方向后移，速度增大；
- 向上滚：`PullAlpha` 减小；
- 默认速度范围 `900~2300 cm/s`，拉距 `120~430 cm`。

预览使用与飞行一致的径向反平方重力：

```text
mu = SurfaceGravity * PlanetRadius²
Acceleration = normalize(Center - Position) * mu / Radius²
```

并加入与速度反向的线性空气阻力。浅蓝色隔点 Debug Point 构成虚线；预览不查询碰撞、不计算反弹。

### 4.1 固定发射视角

进入发射模式时，系统以弹弓弦在当地球面切平面内的垂线建立固定发射轴，并选择当前主控鸟所在一侧作为正向。该轴在 `Ready` 与 `Pulling` 整个阶段保持不变：鼠标拖拽仅改变弹丸袋偏移和实际发射速度/方向，不再更新瞄准相机的中心、朝向或滚转。

相机固定在发射轴后方，以略仰视角看向该轴前方；屏幕 Up 始终由弹弓中心的球面径向 Up 投影得到。`AABTSM6SlingshotCamera` 的 `ABTS | M6 | Aim` 分类提供以下可调参数：`AimDistanceCM`、`AimPitchDegrees`、`AimTargetForwardDistanceCM`、`AimTargetHeightCM` 与 `AimCameraBlendSpeed`，分别控制镜头距离、仰视角、前视构图距离、目标高度及进入发射模式时的平滑速度。

## 5. 飞行与碰撞

发射后 Force Movement 进入 BallisticFlight：

- 不读取 WASD 输入；
- 使用反平方径向引力；
- 使用独立 `FlightAirDragPerSecond`；
- 胶囊继续 Sweep 地形、HISM 与建筑；
- 每次 Blocking Hit 输出入射法向速度。

撞击响应同时查鸟与材料两张配置：鸟配置给出基础撞开/破坏速度，材料配置提供阈值倍率、鸟剩余切向速度和反弹比例。默认木材较易撞开/撞碎，石材阈值高且对鸟反弹更明显。

HISM 实例达到撞开阈值后：

1. 保存实例世界 Transform 与 Static Mesh；
2. 从原 HISM 移除该实例；
3. 若达到破坏阈值，实例直接消失；
4. 否则生成 `AABTSM6DestructibleProxy`，启用 Chaos 刚体并施加径向重力和冲量。

动态代理撞击其他 HISM 时使用连锁阈值继续提升或破坏；自身高速碰撞也会碎裂。M6 按需求暂不处理密集森林中代理同时激活产生的拥挤问题。

发射结束后，未碎裂的动态代理会停止模拟并转为 `WorldStatic + QueryAndPhysics`：它们保留被撞歪后的姿态，并持续阻挡 Chaos 行走和后续发射，不能降级为 `QueryOnly`。后续发射再次命中已冻结代理时，达到撞开阈值会重新启用其刚体并施加冲量；达到破坏阈值会将其碎裂移除。

## 6. 黑鸟

黑鸟飞行时左键点击黑鸟 Actor 可立即手动引爆。第一次显著碰撞会启动默认 `2.2s` 自动引信。爆炸使用世界空间球形范围查询，无视遮挡和碰撞：`BlackExplosionRadiusCM` 近圈内树石与 M7 建筑材料直接破坏；延伸至 `BlackExplosionImpulseRadiusCM` 的外圈对象提升或重新激活为动态刚体，并受到按距离衰减的径向冲量。当前不生成粒子、声效或掉落。

## 7. 结束与回归

发射鸟在飞行至少 1 秒并接地后进入 `Settling`。该阶段仍维持 Chaos、径向/平面重力、黑鸟引信和连锁碰撞，不立即冻结场景。`FABTSM6PhysicsSettleMonitor` 每隔 `SettleSampleIntervalSeconds` 汇总 M6 动态代理与 M7 动态模块：只有全部刚体的线速度低于 `SettleLinearSpeedThresholdCMPerSec`、角速度低于 `SettleAngularSpeedThresholdDegPerSec`，并连续保持 `SettleStableHoldSeconds`，同时距离最后一次显著冲击、爆炸、活塞释放或动态提升超过 `SettleMinimumPostActivitySeconds`，才进入 Returning。

黑鸟爆炸、炸药桶爆炸、弹簧活塞释放、发射鸟显著碰撞、代理显著碰撞以及 M7 模块重新激活均重置活动时间。发射鸟在 Settling 中重新离地也会重置稳定窗口。由于径向重力通过逐帧 `AddForce` 实现，Chaos Awake 只用于诊断，最终判据以线速度、角速度和连续稳定时间为准，避免在抛物线最高点或低位移高速旋转时误判静止。

若场景因微抖或长时间滚动在 `SettleMaximumWaitSeconds` 内仍未稳定，则输出 `[ABTS][M6][Settle] ForcedTimeout` 并强制进入 Returning，防止玩法闭环永久阻塞。正常稳定输出 `Settled`，采样期间每秒输出一次动态刚体数量、移动数量、最大线速度、最大角速度和稳定累计时间。进入 Returning 后：

- 全部未碎裂动态代理关闭 Simulate Physics，保留当前位置和旋转，变为静态查询/碰撞物；
- 鸟关闭碰撞与移动 Tick，沿球面弧线飞回弹弓后方；
- 回归完成后恢复 Capsule、Force Movement、Party 跟随和 Party Camera。

## 8. 编辑器步骤

1. 关闭 PIE 并编译；建议复制 M5.1 地图为 `L_ABTS_M6`。
2. World Settings → GameMode Override 选择 `ABTSM6GameMode`，或创建 `BP_ABTSM6GameMode` 子类后选择它。
3. 若要调参数，创建 `BP_ABTSM6SlingshotSystem`，在 GameMode 的 `SlingshotSystemClass` 中引用它。
   - `ABTS | M6 | Return | Settlement` 暴露线速度阈值、角速度阈值、持续稳定时间、冲击后最短等待、最大等待和采样间隔；默认分别为 `20 cm/s`、`10 deg/s`、`2.0s`、`2.5s`、`15s`、`0.1s`。
4. 树与石 Static Mesh 必须配置 Simple Collision。HISM 在 M6 为 `QueryAndPhysics + WorldStatic + SimulatePhysics=false`。
5. 进入游戏，收集/加工并在两个 DirtHole 安装同类桩，再连接对应弦。

### 8.1 快速测试弹弓

`ABTSM6GameMode` 提供 `ABTS | M6 | Testing > Spawn Debug Slingshots At Start`。由于 GameMode 是运行时生成对象，应创建 `BP_ABTSM6GameMode` 子类后在 **Class Defaults** 勾选该项，并在目标地图的 **World Settings > GameMode Override** 选择该 Blueprint。

启用后，每次新游戏在 TaskGraph 出生 Cell 周围直接生成 8 组完整弹弓：

- 简易弹弓 4 组：前、后、左、右；
- 强化弹弓 4 组：四条对角方向；
- 每组都包含两根已经插入的同类桩和一根已连接弦；无需材料、背包或手动装配。

这些对象仅用于 M6 交互/弹道测试，运行时不写入 CellTopo 占用表，也不会在游戏结束后保存。完成测试后取消勾选，恢复正常 M5.1 装配流程。

## 9. 验收清单

1. 点击已连接弹弓的可见弹丸袋进入发射模式；不兼容鸟得到拒绝日志且不进入。
2. 当前鸟进入袋子，其他三鸟分列后方两侧，不遮挡发射方向。
3. 只有点击袋子附近才开始拖拽；滚轮方向与拉力规则一致。
4. 浅蓝虚线随鼠标方向与滚轮拉力实时变化，没有碰撞反弹预览。
5. 松开左键发射；WASD 与 Tab 不影响飞行，相机跟随鸟且不滚转。
6. 低速撞树石只改变鸟速度；中速将实例撞开；高速将实例撞碎。
7. 动态树石能撞击其他 HISM 并触发连锁响应。
8. 黑鸟可点击提前引爆，也能在碰撞后自动引爆。
9. 鸟落地后进入 Settling；爆炸弹飞的物体必须完成下落和后续碰撞，全部动态物持续低速后才保持最终位置并冻结。
10. 日志包含 `[ABTS][M6][Enter]`、`[Launch]`、`[Impact]`、`[Settle] Begin`、`[Settle] Settled` 和 `[Return]`；正常测试不出现 `ForcedTimeout`、Fatal、assert 或 ensure。

## 10. 已知首版边界

- 虚线使用 Debug Draw，正式版本应替换为 Niagara Ribbon/Sprite；
- 建筑专属模块尚未进入 M7，当前非地形/HISM 阻挡物统一按 Building Profile 响应，不拆建筑部件；
- M6 不生成掉落，不回收到代理池；
- 不处理密集森林的同时唤醒预算、代理互穿或碰撞风暴；
- 黑鸟爆炸没有表现资产、音效和镜头震动。

## 11. M11 Space 档下游边界

M6 的 Twig/Simple/Reinforced 预演、Chaos 实飞和碰撞链路保持不变。M11 的三重近星飞越会放大当前粗预览与逐帧实飞之间的误差，因此 Space 档若获批准，应在 Release 后交由一个预演/实飞同源的固定步长 provider 接管，而不是继续扩展 `FABTSM6TrajectoryPreview` 或在 HUD 内建立第二套积分器。完整取舍见 [M11 三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md)。
