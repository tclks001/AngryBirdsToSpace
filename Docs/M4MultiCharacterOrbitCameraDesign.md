# M4：球面多角色 Orbit Camera 设计

> 状态：C++ 已按本方案实现，等待编辑器手感与遮挡视觉验收。
>
> 本稿规定相机体验、球面数学、输入映射、主控切换和遮挡策略。它不修改鸟群跟随、移动力模型或 HUD 固定头像顺序。

## 1. 当前问题与根因

当前 M4 相机虽然已经独立于每只鸟的 SpringArm，但期望位置仍使用主控鸟的 `ActorForward`：

```text
CameraLocation = BirdLocation + RadialUp * Height - BirdForward * BackDistance
```

角色朝向又会随 WASD 输入立即变化。因此：

- 按 W、A、S、D 会得到四个不同的 `BirdForward`，相机被迫绕主控切换到四个方向。
- A/D 横移实际上变成“角色先转向，镜头再追到角色背后”，无法保持玩家原来的观察方向。
- 相机朝向来自角色而不是玩家，游戏内没有独立的环绕视角状态。
- 主控切换后，新鸟的 Forward 不同，即使位置平滑，期望相机点也可能跳到球面的另一侧。

根因不是 Lag 参数太小，而是**相机方位的所有权错误**：相机把角色朝向当成自己的轨道方向。

## 2. 市面主流方案比较

| 方案 | 常见使用场景 | 优点 | 对本项目的问题 |
| --- | --- | --- | --- |
| 角色背后锁定相机 | 车辆、严格前进式动作 | 永远看角色前方，结构简单 | A/D 和掉头会强制旋转视角；不适合可自由环绕和四角色切换。 |
| 固定俯视/等距相机 | ARPG、战术游戏 | 稳定、不会晕、队伍容易入镜 | 缺少玩家调整，球面地形和前方观察能力弱。 |
| 队伍包围盒动态构图 | 本地合作、RTS 小队 | 能看到全部成员 | 任一跟随鸟落后都会拖动/缩放镜头，削弱当前主控的直接操控感。 |
| 玩家持久 Orbit Camera | 主流第三人称动作、开放世界、可切换角色游戏 | 视角由玩家控制；移动可相机相对；切换角色时可以保留观察方向 | 需要明确球面 Up、极点运输、鼠标与 HUD 光标冲突处理。 |
| Lock-on/目标锁定 | 战斗模式 | 始终看到主控与敌人 | 当前 M4 没有战斗目标，不应作为默认探索相机。 |

主流第三人称动作游戏通常采用第四种作为默认状态，并按需叠加 Lock-on、剧情镜头或动态构图。可切换角色游戏的关键也不是为每个角色保存一台独立相机，而是让同一玩家相机在更换 Follow Target 后保留轨道状态。

## 3. 最终选择

本项目采用：

> **Player-owned Persistent Radial Orbit Camera**

其原则是：

1. 相机由 PlayerController/CameraManager 持有，全局只有一套玩家轨道状态。
2. 当前主控鸟只提供跟踪锚点和当地径向 Up，不提供相机 Yaw。
3. WASD 只改变鸟的移动与朝向，不改变相机轨道角。
4. 鼠标/右摇杆显式改变 Orbit Yaw、俯视角和距离。
5. 主控切换只替换跟踪目标，并平滑迁移锚点；Yaw、Elevation、Distance 全部保留。
6. 所有球面计算使用主控位置的径向 Up，不使用世界 Z。

不采用“自动始终回到鸟背后”作为默认行为。否则只是把当前问题延迟几秒重新出现。

## 4. 球面轨道坐标

相机保存三个与角色朝向无关的持久状态：

```text
OrbitForwardTangent  // 玩家选择的水平观察方向
ElevationDegrees     // 有符号俯仰角：正值俯视，负值仰视
OrbitDistanceCM      // 相机到注视锚点的距离
```

每帧根据主控鸟位置计算：

```text
Up = normalize(BirdLocation - PlanetCenter)
Forward = normalize(ProjectOnPlane(OrbitForwardTangent, Up))
Right = normalize(cross(Up, Forward))

Pivot = BirdLocation + Up * LookAtHeightCM

CameraLocation = Pivot
               + Up * sin(Elevation) * Distance
               - Forward * cos(Elevation) * Distance

CameraRotation = LookAt(CameraLocation, Pivot)
```

`LookAt` 不能只用 Forward 构造。相机屏幕 Up 必须显式锁定为 `RadialUp` 在相机图像平面上的投影：

```text
LookDirection = normalize(Pivot - CameraLocation)
ScreenUp = normalize(ProjectOnPlane(RadialUp, LookDirection))
CameraBasis = MakeFromXZ(LookDirection, ScreenUp)
```

旋转平滑使用最短弧四元数插值；插值结果每帧再用当前 `RadialUp` 重建一次 X/Z 正交基，以消除四元数中间姿态可能出现的 Roll。禁止只调用 `MakeFromX`，也禁止对 Pitch/Yaw/Roll 欧拉角直接插值。

这样 `Elevation=+85°` 接近正上方俯视，`Elevation=0°` 是当地地平线，`Elevation=-85°` 接近从地表向上仰视。运行时固定限制为 `[-85°, +85°]`；不允许到达正负 `90°`，因为此时 LookDirection 与 RadialUp 平行，屏幕 Up 投影会数值退化。推荐默认 `58°–65°`，既能看到前方道路，也保持清楚的俯视关系，但玩家可以连续穿过水平视角进入仰视。

### 4.1 球面移动时的方向运输

主控沿球面移动后，旧 `OrbitForwardTangent` 不再严格垂直于新 Up。不能用世界 Yaw 或经纬度重新生成，否则接近极点会翻转。

每帧采用：

1. 求旧 Up 到新 Up 的最短弧旋转。
2. 用该旋转将旧 OrbitForward 平行运输到新切平面。
3. 再投影到新 Up 的切平面并归一化。
4. 只有数值退化时才使用上一帧 Camera Right 构造回退方向。

该规则使镜头绕过极点时仍保留玩家的观察方位，不依赖世界 XY 或经纬度。

## 5. 输入方案

当前 HUD 要求鼠标左键点击头像，而第三人称相机又需要鼠标环绕。推荐采用 PC 游戏中常见的“自由光标 + 右键拖拽相机”：

| 输入 | 行为 |
| --- | --- |
| WASD | 按相机切向基准移动主控，不改变相机 Orbit。 |
| 按住 RMB + Mouse X | 绕当地 Up 旋转 Orbit Yaw。 |
| 按住 RMB + Mouse Y | 调整 Elevation。 |
| Mouse Wheel | 调整 Orbit Distance。 |
| 松开 RMB | 恢复可见光标，可点击右侧头像。 |
| LMB 点击头像 | 切换主控，不改变 Orbit Yaw/Elevation/Distance。 |
| Tab | 固定 BirdId 顺序循环切换。 |
| 可选 `R` | 主动将镜头缓慢重置到当前鸟后方；不是自动行为。 |
| Gamepad Right Stick | 始终控制 Orbit Yaw/Elevation。 |

RMB 按下时临时捕获并隐藏鼠标；松开时恢复光标和 Game+UI 输入。这样 HUD 点击与自由视角不会争用同一组 Mouse X/Y。

### 5.1 相机相对移动

WASD 不能继续使用角色 Forward。移动基准直接来自相机轨道：

```text
MoveForward = OrbitForwardTangent
MoveRight   = cross(RadialUp, MoveForward)

MoveIntent = W/S * MoveForward + A/D * MoveRight
```

角色将自身朝向转向 `MoveIntent`，但相机不跟随角色旋转。因此：

- W 永远朝当前画面上方/前方移动。
- S 向画面后方移动。
- A/D 是稳定的相机相对横移。
- 连续快速按不同方向不会让镜头绕角色跳转。

即使接近正俯视，移动也使用持久的 OrbitForward，而不是投影几乎竖直的 Camera Look Vector，因此不会退化为零向量。

## 6. 主控切换

切换 Red/Blue/Yellow/Black 时：

1. ViewTarget 始终保持同一台 Orbit Camera。
2. 不调用 `SetControlRotation(NewBird->GetActorRotation())` 重置镜头。
3. 保存当前 OrbitForward、Elevation 和 Distance。
4. 记录旧 Pivot 和新 Pivot。
5. 在 `SwitchBlendSeconds` 内用 Cubic Ease 平滑迁移 Pivot。
6. Pivot 的球面方向使用最短弧 Slerp，半径/高度独立插值，避免远距离切换时直线穿入球体。
7. OrbitForward 从旧 Up 平行运输到插值 Up，保证画面方位连续。
8. Blend 完成后继续普通跟随 Lag。

建议切换时间 `0.40–0.55 s`。切换距离较大时可临时增加最多 `10%–15%` 的 Orbit Distance，让旧鸟和新鸟短暂同时入镜；初版可不做该效果。

相机不读取新主控的 ActorForward，因此切到朝向相反的鸟也不会突然绕到它背后。

## 7. 普通跟随与构图

### 7.1 跟踪对象

默认只以当前主控为构图锚点。三只跟随鸟不能参与持续的队伍包围盒构图，否则任一脱队、跳跃或 Recovery 都会拖动镜头。

跟随鸟只在以下表现中影响镜头：

- 主控切换的短暂过渡；
- 未来终局四鸟集体发射演出；
- 未来明确进入 Party Overview 模式。

### 7.2 屏幕位置

主控不必严格位于屏幕中心。建议略低于中心，使画面上方保留更多道路、建筑和跳跃落点空间：

```text
TargetScreenY ≈ 0.56–0.62
```

初版可通过 `LookAtHeightCM` 和相机几何近似实现；后续再增加真正的 Screen-space Composer。

### 7.3 Lag 与 Dead Zone

成熟相机通常不会对目标每一厘米位移立即响应。建议：

- 切向 Pivot Dead Zone：`15–30 cm`。
- 普通位置跟随速度：`6–9`。
- 旋转跟随速度：`10–14`。
- 跳跃径向跟随速度略低于地面切向速度，减轻起跳时的镜头上下抽动。
- 不启用位置 Lookahead；Unity Cinemachine 文档明确提醒，Lookahead 会放大噪声并造成抖动。

## 8. 相机遮挡与防突变

完全禁用相机碰撞会穿进建筑；直接使用无平滑 SpringArm 又会在碰撞体边缘突然收缩。推荐自定义软遮挡距离：

1. 从 Pivot 向期望 CameraLocation 做 Camera Sphere Sweep。
2. 明确忽略四只鸟、BirdVisual、HUD/队伍 Actor 和纯装饰 HISM。
3. 只响应地形、建筑和需要遮挡镜头的 WorldStatic/WorldDynamic。
4. 命中时快速缩短有效距离，避免穿模。
5. 障碍消失时慢速恢复距离，避免“弹簧弹出”。
6. 使用进入/退出不同速度和 `10–25 cm` 迟滞。
7. 通常构图距离不应过近；但碰撞安全优先于舒适距离。进入负 Elevation 后轨道射线会靠近地表，命中时允许相机收缩到地面之前，禁止为了维持旧的最小臂长而把相机推入地形。

建议：

```text
ObstructionPullInSpeed  = 18–25
ObstructionRestoreSpeed = 4–7
ProbeRadiusCM           = 18–28
```

附近鸟永远不能触发相机遮挡；这条规则应同时由查询 Ignore Actor 和鸟 Capsule 的 Camera Response 保证。

## 9. 相机状态

初版只需要三个状态：

| 状态 | 进入方式 | 行为 |
| --- | --- | --- |
| `FreeOrbit` | 默认 | 玩家持久控制 Yaw/Elevation/Zoom，WASD 不旋转镜头。 |
| `SwitchBlend` | Tab/头像切换 | 保留轨道状态，平滑迁移 Pivot。 |
| `ManualRecenter` | 玩家按重置键 | 用短时插值将 OrbitForward 对齐鸟前向。 |

未来可增加 `LockOn`、`SlingshotAim`、`BirdInFlight`、`PartyOverview` 和 `Cinematic`，但每个模式结束后都应恢复玩家先前的 FreeOrbit 状态，而不是重置为角色 Forward。

## 10. 推荐默认参数

| 参数 | 默认值 | 范围 |
| --- | ---: | ---: |
| `OrbitDistanceCM` | 850 | 550–1300 |
| `DefaultElevationDegrees` | 60° | -85°–+85° |
| 运行时 Pitch 范围 | -85°–+85° | 固定安全范围 |
| `LookAtHeightCM` | 35 | 0–120 |
| `FieldOfViewDegrees` | 52° | 45°–70° |
| `TargetSwitchBlendSeconds` | 0.48 s | 0.30–0.75 s |
| `PivotFollowSpeed` | 7.5 | 4–12 |
| `RotationFollowSpeed` | 12 | 7–20 |
| `ZoomStepCM` | 80 | 40–150 |
| `ProbeRadiusCM` | 24 | 12–40 |
| `ObstructionPullInSpeed` | 22 | 12–35 |
| `ObstructionRestoreSpeed` | 5 | 2–10 |
| `AutoRecenterEnabled` | false | 初版关闭 |

当前 `CameraHeightCM + CameraBackDistanceCM` 两参数表达应迁移为 `OrbitDistance + Elevation`，避免高度和后退距离组合出不一致的 Pitch。

## 11. 编辑器与输入配置要求

未来实现时，`ABTSBirdPartySettings > Camera` 应暴露：

- Orbit 默认距离、最小/最大距离；
- 默认/最小/最大 Elevation；
- LookAt 高度和 FOV；
- 普通跟随与切换 Blend 参数；
- 鼠标/手柄灵敏度和反转 Y；
- 遮挡 Probe、最小距离、收缩/恢复速度；
- 可选手动 Recenter 参数。

输入增加：

- `ABTS_CameraOrbitHold`：RMB；
- `ABTS_CameraZoom`：Mouse Wheel Axis；
- `ABTS_CameraRecenter`：可选 R；
- Gamepad Right X/Y。

现有 `ABTS_Turn` / `ABTS_LookUp` 不应再由角色 Surface Component 直接改变相机方位；它们改为在 OrbitHold 或手柄视角输入有效时写入 Player Camera 状态。

## 12. 验收场景

### 12.1 WASD 稳定性

1. 镜头保持静止，依次按 W、A、S、D；鸟按画面相对方向移动，镜头不绕到鸟的新背后。
2. 快速交替 A/D，角色朝向改变但相机 Orbit Yaw 不变。
3. 同时按 W+D，斜向速度归一，镜头不变化。

### 12.2 手动视角

1. 按住 RMB 左右拖动，可绕主控连续旋转。
2. RMB 上下拖动可从 `+85°` 俯视连续经过 `0°` 水平视角，到达 `-85°` 仰视；两个方向都只在 85° 截断。
3. 滚轮缩放不改变俯视方向。
4. 松开 RMB 后光标恢复，能够点击四个固定 HUD 头像。
5. 不操作相机时不会自动回到角色背后。

### 12.3 主控切换

1. 让相机观察道路侧面，再切到朝向相反的鸟；相机保留原观察方位。
2. Tab 和头像切换都在约 `0.48 s` 内平滑迁移目标。
3. 切换过程无一帧切回鸟自带 SpringArm。
4. 连续快速切换时，以当前插值位置作为下一次 Blend 起点，不瞬移。

### 12.4 球面与极点

1. 沿球面长距离移动，画面 Up 连续变化但不产生 Roll 抖动。
2. 穿过极点后 WASD 仍是相机相对方向。
3. OrbitForward 不因世界 XY 投影退化而翻转。

### 12.5 遮挡

1. 主控穿过其他鸟之间，相机不收缩、不跳动。
2. 靠近建筑时相机平滑拉近；离开后较慢恢复。
3. 相机不会进入地形内部，也不会因单帧边缘接触在远近位置间振荡。

## 13. 调研依据

### 13.1 Unreal Engine

- Epic 的 Spring Arm 文档将其描述为保持相机距离并在碰撞时收缩的 camera boom。它适合单目标基础第三人称相机，但动态目标和邻近角色未过滤时会产生本项目已经遇到的突然收缩。
- `APlayerCameraManager` 和 PlayerController ViewTarget 模型支持把相机所有权放在玩家侧，而不是每个 Pawn 内；多角色切换应保持同一相机状态，只更换被跟踪对象。
- `SetViewTargetWithBlend` 提供视图目标过渡，但本项目更适合同一 Camera Actor 内部 Blend Pivot，因为这样不会在角色相机之间切换，也能保留 Orbit 状态。
- Lyra Sample 展示了基于 Camera Mode/PlayerCameraManager 的玩家相机分层思路，说明瞄准、普通跟随和其他模式应是同一玩家相机的状态，而非散落在不同 Pawn 上。

### 13.2 Unity Cinemachine

- Cinemachine Orbital Follow 把水平轴、垂直轴和半径作为相机相对 Tracking Target 的独立状态，并允许玩家输入驱动。这与本项目的 OrbitForward、Elevation、Distance 一致。
- Orbital Follow 的 Binding Mode 明确区分“跟随 Target 旋转”和“只在 Assign 时锁定/保持世界方位”。本项目选择保留轨道方位，不能使用随 Target Rotation 的绑定。
- Position Composer 提供目标屏幕位置、Dead Zone、Damping 和 Lookahead；本项目采用 Screen Offset、Dead Zone 与 Damping，但初版禁用 Lookahead，因为其文档明确提示运动噪声会被放大为相机抖动。

### 13.3 其他引擎与游戏惯例

- Godot 的第三人称 SpringArm 文档同样使用从目标到相机的碰撞探测，说明遮挡收缩是通用方案；工程质量差异主要在碰撞过滤、迟滞与恢复平滑。
- 开放世界/动作游戏常用“自由 Orbit + 相机相对移动 + 可选 Lock-on”；可切换队员的 RPG 则保留相机朝向并 Blend 新角色锚点。固定等距和全队包围盒相机更适合 ARPG/战术或本地合作，不适合本项目当前单主控的球面探索。

### 在线资料

- [Using Spring Arm Components in Unreal Engine — Epic Games](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-spring-arm-components-in-unreal-engine)
- [APlayerCameraManager API — Epic Games](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/Camera/APlayerCameraManager)
- [SetViewTargetWithBlend API — Epic Games](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/GameFramework/APlayerController/SetViewTargetWithBlend)
- [Lyra Sample Game — Epic Games](https://dev.epicgames.com/documentation/en-us/unreal-engine/lyra-sample-game-in-unreal-engine)
- [Orbital Follow — Unity Cinemachine 3.1](https://docs.unity.cn/Packages/com.unity.cinemachine@3.1/manual/CinemachineOrbitalFollow.html)
- [Position Composer — Unity Cinemachine 3.1](https://docs.unity.cn/Packages/com.unity.cinemachine@3.1/manual/CinemachinePositionComposer.html)
- [Third-person camera with spring arm — Godot](https://docs.godotengine.org/en/stable/tutorials/3d/spring_arm.html)

## 14. 最终决策

M4 下一版相机不再根据 `Bird ActorForward` 计算位置，改为玩家持有的球面持久 Orbit：

```text
玩家 Orbit 状态
    + 当前主控的 Pivot / RadialUp
    -> 球面相机期望位置
    -> 主控切换 Pivot Blend
    -> 遮挡软距离
    -> 最终 Camera Transform
```

WASD 改为相机相对移动；RMB 控制 Orbit；滚轮控制距离；LMB 保留 HUD 点击。主控切换保持玩家观察方向，只平滑更换注视锚点。这一方案同时解决当前“按方向键镜头换方向”“无法游戏内调视角”和“切换角色镜头失去方位连续性”三个问题。
