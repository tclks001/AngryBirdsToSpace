# M2.5：径向物理引力、碰撞与跳跃

> 状态：已实现 C++，等待创建 M2.5 地图入口并完成 PIE / Standalone 验收。
>
> 前置：[M2PlanetSurfaceDesign.md](M2PlanetSurfaceDesign.md)。本阶段只替换角色运动，不修改 `CellTopo`、连续球面拓扑、PCG 或资源玩法。

## 1. 目标与边界

M2.5 将 M2 的“每帧强制贴回球面”替换为带速度的径向运动：角色受球心方向的持续加速度，能在基础球壳上接地、沿切平面行走、碰到阻挡体时滑动，并可按空格跳离表面再落回。

本期的“物理”是可控的 kinematic Character Capsule 运动，不是将角色改成 Chaos 模拟刚体。这样能稳定支持第三人称控制、未来建筑/树木碰撞和弹射玩法，同时仍保持 M2 的球面局部坐标系。

本期不做：坡度滑落、摩擦材质差异、可破坏刚体对角色的冲量、游泳、飞行、斜坡台阶、网络预测、发射鸟的刚体轨迹。发射鸟的真实刚体与径向重力由后续 `ABTSPhysicsInteractionComponent` 负责。

## 2. 运动模型

```text
Up          = normalize(CharacterLocation - PlanetCenter)
GravityDir  = -Up
Velocity   += GravityDir * GravityAcceleration * DeltaTime

TangentInput = ProjectOnPlane(CameraForward / CameraRight, Up)
Velocity    += TangentInput * GroundAcceleration * ControlScale * DeltaTime
Location    += Velocity * DeltaTime   (Capsule Sweep)
```

每个 Tick 结束后，根据球壳半径与 Capsule Half Height 处理基础地面接触：

```text
DesiredCenterRadius = PlanetRadius + CapsuleHalfHeight
if CurrentRadius <= DesiredCenterRadius + GroundSnapTolerance
    Snap to DesiredCenterRadius
    remove all radial velocity
    Grounded = outward radial speed <= UngroundSpeed
```

接地并不要求径向速度严格小于等于零。角色沿曲面移动时，上一帧的切线速度相对新位置的 `Up` 会天然出现极小的向外分量；若以该分量直接清除 `Grounded`，角色离开初始点后会被误判为空中状态。因此，仅当向外速度超过 `UngroundSpeedCMPerSec`（默认 `120`）时才真正离地；跳跃初速度 `620` 足以越过该阈值。

每帧消费跳跃输入前必须先按当前位置刷新一次几何接地，不能只依赖上一帧缓存的 `Grounded`。空格输入保留 `JumpBufferSeconds`（默认 `0.15` 秒），避免输入发生在接触状态切换帧时被直接丢弃。地面移动不再持续累加加速度，而是以 `GroundAccelerationCMPerSec2` 或 `GroundBrakingCMPerSec2` 向目标切线速度收敛，因此反向和松键不会残留异常侧向惯性。

Capsule Sweep 首次命中阻挡物后，未消耗位移会投影到碰撞面切平面并再次 Sweep。这样角色保留沿球壳或 Cube 表面的可行移动，不会因为每帧丢失剩余切线位移而出现滞涩。

跳跃规则：只在 `Grounded=true` 时接受 `SpaceBar`；向当前 `Up` 注入 `JumpSpeedCMPerSec`，随后重力将其拉回。空中只保留较低的 `AirControlScale`。

同一帧的 W/S 与 A/D 输入先累计，再投影/限制到当前切平面单位圆，避免斜向移动被最后一个 Axis 回调覆盖。

## 3. C++ 责任划分

| 类 | 职责 |
| --- | --- |
| `UABTSM25RadialMovementComponent` | 速度积分、径向引力、Capsule Sweep、阻挡投影、球壳接地、跳跃状态。 |
| `AABTSM25BirdCharacter` | 输入转发、启用 M2.5 Movement、禁用旧 CharacterMovement、保持 M2 相机/姿态组件。 |
| `AABTSM25GameMode` | M2.5 入口装配；只指定 Pawn/Controller/HUD。 |
| `UABTSM2SphericalSurfaceComponent` | 继续提供径向 Up、切线相机/朝向；M2.5 关闭其“强制贴球”功能，避免覆盖跳跃高度。 |

`AABTSM2Planet` 仍只拥有球面、碰撞与逻辑拓扑，不持有角色速度、接地或跳跃状态。

## 4. 编辑器操作

1. 关闭正在运行的 PIE，编译 `AngryBirdsToSpaceEditor Win64 Development`，再重新打开编辑器。
2. 复制 `/Game/Maps/L_ABTS_M2` 为 `/Game/Maps/L_ABTS_M25`；保留 `BP_ABTSM2Planet`、天空、Directional Light、Sky Light 和 Player Start。
3. 保持 Planet 位于 `(0,0,0)`，半径为 `10000`；Player Start 保持在 `(0,0,10140)`。
4. 在 **World Settings > GameMode Override** 设置为原生 `ABTSM25GameMode`。本阶段不要使用旧的 `BP_ABTSM2GameMode`，它可能保存 M1 Pawn 覆盖。
5. 保存地图。若希望本阶段成为默认入口，在 **Project Settings > Maps & Modes** 将 Game Default Map 与 Editor Startup Map 改为 `L_ABTS_M25`。
6. 在 PIE 中使用 WASD 移动；按 **Space** 跳跃。可在 `AABTSM25BirdCharacter` 的 `RadialMovement` Details 中调整引力、跳跃速度、空中控制和最大移动速度。

建议首轮保持默认参数：`GravityAcceleration=980`、`JumpSpeed=620`、`MaxGroundSpeed=680`。这会产生约 0.8 秒的完整跳跃往返，便于观察。

## 5. 验收

1. 角色静止于北极、赤道和南半球任一点时，不悬浮、不穿入球面；Down 始终指向球心。
2. WASD 在任意球面位置沿局部切平面移动；接近两极时不会因世界 XY/经纬度退化而失去方向或翻转。
3. 按空格时角色沿当地径向外侧离开球面，达到最高点后沿球心方向回落，且只能在落地后再次跳跃。
4. 放置一个带 `BlockAll` 碰撞的 Cube/StaticMesh 在球面附近。角色撞到它不会穿透，径向运动组件移除朝向碰撞面的速度分量，表现为停止或沿面滑动。
5. 本次 PIE/Standalone Output Log 包含 M2 的 `Ready=1`；M2.5 不应再有每帧强制把角色投射到球壳从而取消跳跃的行为。
6. Standalone 新进程重复 1–5。

## 6. 排错

| 现象 | 原因与修复 |
| --- | --- |
| 跳起后立刻被拉回地面 | 确认 `SphericalSurface.bProjectToBaseSurface=false`；M2.5 必须保留姿态更新，禁用强制位置投影。 |
| 空格无反应 | 检查 `DefaultInput.ini` 的 `ABTS_Jump -> SpaceBar`，以及地图 GameMode 是 `ABTSM25GameMode`。 |
| 仅在初始点能跳，或变成任何位置都不能跳 | 除了曲面移动产生微小径向速度外，跳跃还可能读取上一帧的接地缓存并在当帧立即丢弃输入。消费空格前刷新几何接地，并用 `JumpBufferSeconds` 短暂保留输入；确认 `UngroundSpeedCMPerSec` 默认 `120`。 |
| 松开输入后惯性滑动或转向漂移异常 | 持续向当前速度叠加输入加速度会保留旧方向速度。接地时应向 `TangentInput * MaxGroundSpeed` 目标速度收敛，松键时向零速度收敛。 |
| 一直无法二连跳 | 这是预期：只有 Grounded 才能 Jump。若角色悬空无法落地，检查 Planet 已 `Ready=1`、半径和 Player Start。 |
| 角色穿过 Cube | Cube 必须启用 Collision、Profile 为 `BlockAll`；确认 M2.5 Pawn 使用 Capsule 而不是关闭碰撞的 BirdVisual。 |
| 角色落到球体内部 | 检查 `DesiredCenterRadius = PlanetRadius + CapsuleHalfHeight`；不要将 Capsule 半高误写为半径。 |
| 角色跳跃时朝向错乱 | 姿态仍由 M2 `MakeFromXZ(TangentForward, RadialUp)` 维护；不要用世界 Z 重写 Actor Rotation。 |
| 跳跃仍无效果，无法从画面判断断点 | 使用 Output Log 搜索 `[ABTS][M2.5][Jump]` 与 `[ABTS][M2.5][Ground]`。依次应出现 `Space input reached`、`Input queued`、`Accepted`；若缓冲超时，日志会明确说明未接地；接地状态变化同时输出实际半径、目标半径、径向速度和离地判定。 |
