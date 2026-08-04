# ABTS：统一镜头视觉优化设计

> 状态：实施中；2026-07-31 建立，2026-08-03 补充直接操纵与遮挡联合方案，2026-08-04 补充主流第三人称镜头调研与源码差异分析，并落实地面直接操纵与遮挡安全层。绕月和统一模式仲裁仍待后续阶段。
>
> 父级入口：[项目工作流](ABTSProjectWorkflow.md) · [游戏主设计稿](AngryBirdsToSpaceGameDesign.md)
>
> 相关详稿：[M4 球面镜头](M4MultiCharacterOrbitCameraDesign.md) · [M6/M9 弹弓与卫星标定](M6M9SlingshotSatelliteCalibrationDesign.md) · [M11 v2 终局优化](M11V2FinaleOptimizationDesign.md) · [开场与终局演出](OpeningAndFinaleCinematicDirection.md)

## 1. 目标与延期边界

本阶段统一处理地面移动、弹射实飞、卫星引力绕行和终局演出之间的镜头可读性，不改变轨迹积分、Chaos 碰撞、成功岛、发射输入或任务判定。

进入本视觉优化阶段时确认的三项问题是：地面拖拽与遮挡已在第 10 节完成首版落实，绕月镜头仍待后续处理。

1. 鸟接近卫星后，镜头会按距离接管到卫星侧视构图。画面容易只读到卫星、读不到鸟，普通借力飞越也发生不必要的大幅转向。
2. 鸟群在地面移动时，一旦相机射线被地形、建筑或装饰物阻挡，镜头会在短时间内突然拉近；离开遮挡后的恢复速度与进入速度不对称，但边缘反复命中仍可能造成抽动。
3. 地面轨道镜头拖拽缺少直接操纵感：拖拽过程中镜头像带有质量和惯性，输入停止后画面仍继续运动，并容易把遮挡后的距离恢复或 Pivot 跟随误读为“镜头被拉回”。玩家期望的是拖到哪里就停在哪里，除非显式按下回正键或玩法状态切换要求接管。

月面着陆画中画已经独立完成验收，不属于本稿的待修问题。后续不得为了修主镜头而改变画中画的月面终点资格、固定俯角、径向 Up 或共享着陆相机距离。

## 2. 首版改造前实现与根因

### 2.1 地面遮挡

`AABTSM4PartyCamera::ResolveObstructedDistance` 当前每帧从 Pivot 到期望相机位置做一次 `ECC_Camera` 球形 Sweep，并按首个阻挡命中的 `Hit.Time` 计算目标距离。命中时使用较快的 `CameraObstructionPullInSpeed`，解除时使用较慢的 `CameraObstructionRestoreSpeed`，随后以 `FInterpTo` 更新距离。

这条链路能避免穿入地形，但仍有以下结构性问题：

- 单条中心 Sweep 的首个命中直接决定距离，没有遮挡进入迟滞、最短保持时间或边缘稳定器；
- 命中对象未按“主地形/建筑/小型装饰物”分级，小型 HISM、树木或凸出的碰撞体也可能触发完整收缩；
- 只沿当前视线缩短距离，不尝试有限的侧移、抬升或对可淡出的遮挡物做视觉处理；
- 安全距离可降至接近 Pivot。近距离阻挡虽然不会穿模，但视觉上可能表现为突然贴近角色；
- `DesiredDistance` 与碰撞解算后的 `EffectiveDistanceCM` 缺少显式状态和调试证据，难以从 PIE 中判断究竟是哪一个组件造成收缩。

### 2.2 绕月实飞

`AABTSM6SlingshotCamera::UpdateFollow` 当前优先调用 `UpdateSatelliteFollow`。只要标定场景中的鸟进入卫星约 `4.0R`，卫星镜头即可接管；进入约 `2.3R` 后切入轨道构图，退出阈值分别为 `4.8R` 与 `2.7R`。

当前侧视法线由卫星径向与鸟速度叉乘得到，焦点在鸟、卫星球心或 E5 之间插值，侧向距离至少为卫星半径的 `2.1` 倍。这能降低近月翻转概率，但存在：

- 接管依据主要是空间距离，没有区分“普通借力飞越”和“已预测会进入 E5 成功岛的演出路径”；
- 卫星球心参与焦点且距离下限随卫星半径放大，导致卫星占画面主体，鸟过小甚至离开玩家注意中心；
- 主星径向跟随突然切到卫星局部 frame，普通发射也会产生幅度过大的倾斜和构图变化；
- 当前状态机描述相机所处空间阶段，却没有表达镜头意图、构图约束和玩家是否需要继续读轨迹。

### 2.3 地面拖拽输入与“回弹感”

`AABTSM4PlayerController` 在按住右键时把 `MouseX/MouseY` 直接传给 `AddOrbitYawInput/AddOrbitPitchInput`，松开时只恢复光标位置，并没有调用 `RequestRecenter()`。因此当前体验不是由“松手自动回正”这一条显式逻辑造成，而是以下状态叠加后的感知结果：

- `DefaultInput.ini` 中鼠标轴灵敏度为 `0.07`，之后又乘 `OrbitYawDegreesPerInput=1.0` 和 `OrbitPitchDegreesPerInput=0.7`，拖拽目标本身偏慢；
- 输入立即修改 `OrbitForwardTangent/ElevationDegrees`，但 Actor 位置继续用 `VInterpTo(..., OrbitPivotFollowSpeed=7.5)`，旋转继续用 `QInterpTo(..., OrbitRotationFollowSpeed=12.0)` 追赶目标。松手后目标虽然停止，显示镜头仍会继续追赶，因此表现为惯性；
- Pivot 另有 `22 cm` 死区和位置插值，玩家移动或刚停止时，焦点与局部径向 Up 仍可能继续变化，进而带动相机位置与朝向；
- 遮挡把 `EffectiveDistanceCM` 收缩后，会以 `CameraObstructionRestoreSpeed=5.0` 自动恢复到持久的 `OrbitDistanceCM`。这项正确的安全恢复与旋转/位置追赶同时发生时，很像镜头被拉回旧构图；
- 鼠标和手柄目前共用同一逐帧增量接口。鼠标 Delta 不应乘帧时间，而手柄摇杆应解释为角速度并乘 `DeltaSeconds`；当前手柄路径会随帧率改变转速。

`TransportOrbitForward` 只负责在球面移动时平行运输玩家保存的切向朝向，不是自动回正；显式回正目前只应由 `R -> RequestRecenter()`、切鸟初始化或更高优先级玩法镜头触发。后续诊断必须区分“用户意图没变但显示仍在追赶”“Pivot 在移动”“遮挡距离恢复”和“真正的回正请求”。

## 3. 主流第三人称镜头调研与本项目差异

### 3.1 调研范围与共同架构

这里的“主流”指 Unreal、Unity/Cinemachine、Godot 官方第三人称方案中反复出现的工程结构，不表示所有商业游戏使用完全相同的参数或代码：

- Unreal 的 `USpringArmComponent` 保存未碰撞的目标臂长和位置，使用指定 Probe Channel/Size 做碰撞测试；发生碰撞时收回相机，无碰撞时恢复。它把目标旋转、位置/旋转 Lag、碰撞修正和最终 Socket 位置区分开，并提供未修正位置、碰撞修正状态、Lag 最大距离和 Lag 子步进等诊断/稳定接口。[Epic：USpringArmComponent](https://dev.epicgames.com/documentation/en-us/unreal-engine/API/Runtime/Engine/USpringArmComponent)
- Unity Cinemachine Third Person Follow 使用独立 Tracking Target 驱动水平 Pivot、肩部 Pivot 和 Camera Distance；玩家若要自由环绕，通常旋转一个可独立于角色的不可见 Tracking Target。目标跟随 Damping 按局部轴配置，碰撞层另有 Camera Radius、进入碰撞阻尼和离开碰撞阻尼。[Unity：Third Person Follow](https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineThirdPersonFollow.html)
- Cinemachine Deoccluder 进一步把遮挡当作独立约束求解：可选择拉近、尽量保持高度或尽量保持距离；提供 Smoothing Time、Minimum Occlusion Time、透明层、Ignore Tag、最大处理命中数以及进出遮挡的不同阻尼。官方文档认为多数环境中处理约四个命中已足够，体现“固定预算候选”而非无界搜索。[Unity：Cinemachine Deoccluder](https://docs.unity3d.com/Packages/com.unity.cinemachine@3.1/manual/CinemachineDeoccluder.html)
- Godot 的标准第三人称示例直接以鼠标相对位移旋转 Camera Pivot，再由 SpringArm3D 沿臂长做 Shape Motion Cast。相机近裁剪面代理或球体负责碰撞；官方明确指出单 Ray 不足以代表相机体积，球体则常用于平滑滑过边缘。[Godot：Third-person camera with spring arm](https://docs.godotengine.org/en/stable/tutorials/3d/spring_arm.html)
- UE 5.8 Gameplay Camera System 将 Camera Rig、Director、进入/退出 Transition 和 Camera Debugger分离。这套插件仍是 Experimental，本项目不应为了本阶段直接迁移过去，但其“模式选择与镜头解算分层”的结构可以作为参考。[Epic：Gameplay Camera System Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/gameplay-camera-system-overview)

这些方案的共同点可以概括为：

```text
Device-specific Input
  -> persistent orbit/control target
  -> desired follow rig (unfixed/unobstructed pose)
  -> collision/deocclusion constraint
  -> optional presentation damping and camera-mode transition
  -> final POV + per-layer diagnostics
```

玩家输入通常决定“想看哪里”，跟随阻尼主要处理角色/Pivot 的移动噪声，碰撞层只负责把不可达位姿改为安全位姿。三者使用不同状态和不同时间尺度，不应由同一个插值速度共同解释。

### 3.2 首版改造前实现与主流方案对照

| 维度 | 主流方案 | 改造前 ABTS | 直接后果 |
| --- | --- | --- | --- |
| 鼠标轨道输入 | 相对鼠标位移直接累计到持久 Pivot/控制目标；通常不对自由观察本身施加明显惯性 | Legacy Axis 灵敏度为 `0.07`，且 `bEnableMouseSmoothing=True`；输入目标之后还要经过 Actor 位置与旋转插值 | 拖拽起步慢，停止输入后显示镜头仍继续追赶 |
| 手柄输入 | 摇杆值经死区/响应曲线解释为角速度，再按时间积分 | 与鼠标共用 `AddOrbitYaw/PitchInput`，每帧直接累加 | 相同摇杆保持时间在不同帧率下可能得到不同旋转量 |
| 用户意图与目标跟随 | 用户角度持久保存；目标位置可按轴阻尼并限制最大滞后 | `OrbitForwardTangent/Elevation`、`SmoothedPivot` 和 Actor Transform 在一条 Tick 中连续更新 | 很难判断“镜头运动”来自输入、角色移动、球面 Up 运输还是显示 Lag |
| 显示平滑 | 对明确层级和模式使用可解释的时间常数，必要时子步进；直接操纵阶段可旁路或显著减小 Lag | `EffectiveDistance FInterpTo` 后又对 Actor Location 做 `VInterpTo`，Rotation 再做 `QInterpTo` | 两层位置滤波叠成更强的二阶拖尾，形成“实体质量感” |
| 基础碰撞 | 用带体积的 Probe/Shape Sweep 得到相机中心可安全到达的位置 | 单次 `ECC_Camera` Sphere Sweep，与基础 Spring Arm 同类 | 能避免大部分穿模，但只是最低层安全方案 |
| Sweep 距离 | Sweep 的 `Hit.Location/Distance` 已表示移动形状不穿透时的中心位置，再单独减很小且显式的安全边距 | 使用 `DesiredDistance * Hit.Time - ProbeRadius`；Shape Sweep 已考虑球半径后又减去完整半径 | 每次命中额外收缩约一个 Probe Radius，放大贴脸和跳变 |
| 初始穿透 | 单独识别、去穿透或选择备用位姿 | `Hit.Time=0` 会被直接 Clamp 到约 `1 cm` | Pivot 或 Probe 初始重叠时可能瞬间塌缩到角色脸上 |
| 遮挡稳定 | 进入延迟、最短遮挡时间、最近点保持、退出阻尼、候选粘滞 | 首个 Blocking Hit 当帧改变目标距离；只有快速拉近/慢速恢复两个速度 | 障碍边缘或碎片碰撞会反复拉近和恢复 |
| 遮挡策略 | 碰撞层/标签过滤，拉近、保高、保距、淡出等有限策略 | 所有阻挡对象等价，只能沿原视线缩短 | 树木、小石块、HISM 与主地形会造成同等级完整收缩 |
| 模式与调试 | Camera Rig/Mode、Transition、碰撞状态分别可见 | 只有切鸟日志，缺少输入状态、未修正位姿、命中组件和转换原因 | PIE 无法可靠区分自动回正、遮挡恢复或 Pivot 跟随 |

Unreal 的 `FHitResult::Location` 定义为 Sweep 形状在不穿透时可放置的位置，`Distance` 是起点到该位置的距离；`ImpactPoint` 才是形状与障碍物的实际接触点。因此当前再减完整 `ProbeRadius` 不是 Sphere Sweep 的必要组成部分，而是一个过大的隐式二次安全边距。[Epic：FHitResult](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FHitResult)

### 3.3 不跟手与跳跃收缩的联合根因

不跟手不是一个需要“增加灵敏度”就能完整解决的问题，遮挡跳跃也不是只要“降低 PullInSpeed”即可。两者共享以下根因：

1. **Camera 不知道直接操纵的生命周期。** `bOrbitInputHeld` 只存在于 PlayerController；Camera 只收到数值，不收到 Begin/Update/End 和设备来源，因此无法在拖拽期间旁路显示 Lag，也无法在松手时证明没有残余输入。
2. **输入先被平滑，输出又被多次平滑。** Legacy Mouse Smoothing、位置目标 Lag、旋转 Lag、碰撞距离 Lag 和最终 Actor Location Lag 级联，任一层结束后其他层仍可能继续移动。
3. **无遮挡目标与碰撞目标没有形成完整位姿快照。** 当前只分开 `OrbitDistanceCM/EffectiveDistanceCM`，没有同时保存 Desired/Safe/Rendered 的位置、旋转、命中来源和版本；遮挡恢复与用户拖拽会在同一个最终 Transform 上相互覆盖感知。
4. **碰撞修正过量且没有状态机。** Sphere Sweep 的安全中心距离被再次扣除完整半径；首个命中会立即产生大幅目标变化，初始穿透可塌缩至 `1 cm`，而解除后又持续慢速外拉。
5. **首个命中承担了全部构图决策。** 没有对象分类、命中持续时间、候选位姿或粘滞。同一树枝/建筑边缘在相邻帧进出 Probe 时会改变整个臂长。
6. **帧率和突发 Delta 缺少统一处理。** 手柄输入未按秒积分，Lag 没有子步进或最大滞后限制；帧时间波动会改变感知响应和一次性追赶幅度。

因此，“松手继续转”和“碰到物体突然贴脸”是同一架构问题的两个表现：用户意图、目标跟随、硬安全约束和视觉舒适度没有被建模为相互独立且可诊断的层。

### 3.4 采用路线

本项目不直接替换为标准 `USpringArmComponent`。主星径向 Up、球面平行运输、四鸟切换、卫星和终局镜头都需要自定义 Frame 与模式仲裁；直接迁移还会带来 Blueprint 默认子对象兼容风险。应保留当前自定义 Camera Actor，但按成熟 Spring Arm/Cinemachine 的职责重构：

- 输入层明确 Mouse Delta 与 Gamepad Angular Velocity；地面拖拽默认关闭额外 Mouse Smoothing，或通过独立 Enhanced Input Modifier 明确其用途，禁止隐式全局平滑；
- Camera 接收 `BeginDirectManipulation/ApplyMouseDelta/ApplyGamepadRate/EndDirectManipulation`，持久保存用户 Yaw/Pitch/Distance；
- Follow 层只平滑 Pivot/焦点，优先使用可解释的按轴时间常数、最大滞后和大 Delta 子步进；不得修改用户角度；
- Collision 层从 `DesiredPose` 扫描到期望相机中心，使用 `Hit.Distance` 或 `Hit.Location` 作为 Shape 中心安全距离，只再减独立且很小的 `CollisionSafetyMarginCM`；初始穿透走专用去穿透/备用位姿路径；
- 硬安全收缩必须保证最终相机不越过 Safe Pose；舒适阻尼只能在安全区间内运行。退出遮挡采用最短保持、迟滞和单调恢复；
- 固定评估“原位拉近、有限抬升、左移、右移”等少量候选，以构图代价、角变化和与上一帧候选的一致性评分；预算默认不超过 4 个有效命中/候选链；
- 地形和不可穿建筑保留硬碰撞，树木/石块/HISM 使用独立响应或投影面积门槛，可淡出物体使用视觉淡出；分类只通过稳定只读语义进入镜头；
- Camera Mode/Intent 决定什么时候允许 Blend；普通自由拖拽、遮挡修正和显式 Recenter 不复用同一插值状态。

## 4. 统一职责模型

后续新增统一的镜头仲裁与遮挡解算层。各玩法镜头只提交“期望构图”，最终位姿由同一层完成混合、Up 约束和碰撞安全：

```text
Mouse Direct Delta / Gamepad Angular Velocity
    -> Persistent User Orbit Intent (Yaw/Pitch/Distance; release does not rewrite)
PartyGround / Aim / PrimaryFlight / SatelliteAssist / SatelliteCinematic / Finale
    -> Desired Pose + Focus Set + Framing Constraints + Transition Intent
    -> Camera Arbiter
    -> Obstruction Resolver (Safe Pose; never rewrites User Intent)
    -> Rendered Pose
```

约束如下：

- Gameplay 轨迹、碰撞和命中结论保持权威；镜头不得反向修改鸟的位置、速度或发射输入。
- 每一时刻只有一个主镜头状态拥有最终构图，状态转换必须有来源、原因和退出条件。
- 球面地表继续采用主星径向 Up；近月演出采用稳定的轨道平面 frame，并通过连续运输避免 Up 翻转。
- 保留“期望距离”和“碰撞安全距离”两个独立状态，不能让一次遮挡永久改写玩家设置的轨道距离。
- 地面自由观察时，Yaw/Pitch/Distance 是持久用户意图。鼠标松开必须零残余角速度、零自动回正；碰撞、Pivot 跟随和显示平滑都不得反写该意图。
- 鼠标采用位移到角度的直接映射；手柄采用角速度到角度的时间积分。两种输入不得继续共用含糊的逐帧增量语义。

## 5. 地面遮挡改进

### 5.1 解算策略

1. 保留球形 Sweep 作为安全底线，同时记录阻挡 Actor、Component、对象类型、距离和持续时间。
2. 为进入与退出分别设置迟滞：危险穿透仍可立即收缩，普通边缘命中需连续成立后才进入遮挡状态。
3. 收缩使用有速度上限的临界阻尼；恢复更慢且单调，禁止在障碍边缘逐帧伸缩。
4. 在完整拉近前尝试有上限的抬升或左右候选位姿。候选数量固定，避免不可控的每帧 Trace 成本。
5. 将遮挡物分为：
   - 地形、不可穿建筑：必须保证碰撞安全；
   - 可淡出的大型前景：优先透明/抖动淡出，必要时再收缩；
   - 树木、石块、小型 HISM 等装饰：使用专用响应或忽略阈值，不能因单个小物体大幅拉近。
6. 接近 Pivot 的紧急收缩只用于防穿模，不作为普通舒适构图。

### 5.2 可调参数

- `ProbeRadiusCM`、`CollisionSafetyMarginCM`、`EmergencyDistanceCM`
- `ObstructionEnterDelaySeconds`、`ObstructionExitDelaySeconds`
- `PullInSpeed`、`RestoreSpeed`、`MaxPullInSpeedCMPerSecond`
- `MaxLateralEscapeCM`、`MaxVerticalEscapeCM`
- 可淡出对象类型、忽略对象类型与最小遮挡投影面积

### 5.3 直接操纵与遮挡的联合解算

不能只把 `OrbitRotationFollowSpeed` 调大：这会减轻拖拽延迟，却不能消除 Pivot 追赶、遮挡恢复和手柄帧率相关问题。地面镜头应明确保存四层状态：

1. `UserOrbitIntent`：持久 Yaw、Pitch、Distance，以及输入来源；只被玩家输入、显式回正或明确模式接管修改。
2. `DesiredPose`：根据当前 Pivot、球面 Up 和 `UserOrbitIntent` 每帧重建的无遮挡位姿。
3. `SafePose`：遮挡解算器在不修改用户意图的前提下选择的安全距离、有限抬升或侧移位姿。
4. `RenderedPose`：最终显示位姿。鼠标拖拽期间采用直接或最多 1–2 帧的紧跟响应；松手后不得继续消费虚构的角速度。只有 Pivot 移动、遮挡状态转换或显式模式 Blend 可以继续移动，并必须在调试信息中标明原因。

遮挡期间仍然完整积累用户拖拽意图。如果障碍使目标位置不可达，镜头只在 `SafePose` 层受限；障碍解除后沿当前用户角度单调恢复距离，不能旋回进入遮挡前的方向。边缘迟滞、最短保持时间和候选位姿粘滞同时用于防止“拖拽跟手”与“碰撞安全”互相打架。

建议新增参数：

- `MouseYawDegreesPerPixel`、`MousePitchDegreesPerPixel`；不乘 `DeltaSeconds`；
- `GamepadYawDegreesPerSecond`、`GamepadPitchDegreesPerSecond`、摇杆死区与响应曲线；必须乘 `DeltaSeconds`；
- `DirectManipulationMaxSettleSeconds`，默认目标为 0–2 帧，不提供松手惯性；
- `PivotFollowWhileOrbitingSpeed` 与 `PivotFollowAfterOrbitingSpeed`，但两者只影响焦点追踪，不得改变用户角度；
- 显式 `RecenterBlendSeconds`，只服务玩家按键或模式切换，不能复用普通拖拽的平滑参数。

## 6. 绕月镜头改进

### 6.1 镜头意图

发射时根据权威预测快照锁存本次镜头意图：

- `None`：预测不构成有效卫星接近，保持主星飞行镜头。
- `SubtleAssist`：会借助卫星引力但不进入 E5 成功岛。只允许轻微倾斜、适度拉远和焦点前移，不切成卫星全景。
- `CinematicE5`：预测进入 E5 成功岛或其容差带，允许采用类似深空任务引力助推演示的“卫星边缘 + 鸟 + 前方轨迹”组合构图。

实飞期间可以因轨迹偏差降级意图，但不得仅凭距离从 `None` 升级为完整演出。这样既保留玩家对普通发射方向的连续感，也让正确路径获得明确奖励。

### 6.2 构图

- 使用鸟、卫星可见边缘、短距离前视轨迹和 E5（若适用）的屏幕空间包围框求取相机距离，不再固定注视卫星球心。
- 卫星建议占画面短边约 `20%–45%`；鸟或鸟的高对比标记在近月主要阶段必须持续可见。
- 鸟本体过小时显示非侵入式标记或短尾迹，不放大碰撞体，也不改变 Actor Scale。
- `SubtleAssist` 的附加倾角设上限，建议默认不超过 `8°`。
- 使用预测近月点而非当前距离决定进入时机，并设置进入提前量、最短保持和退出混合时长。
- E5 命中保持与失败回收仍沿用现有 Gameplay 事件，不由相机自行猜测。

### 6.3 可调参数

- `SatelliteSubtleTiltMaxDegrees`
- `SatelliteCinematicEligibilityToleranceCM`
- `PeriapsisLeadSeconds`、`MinimumHoldSeconds`、`ExitBlendSeconds`
- `SatelliteMinScreenFraction`、`SatelliteMaxScreenFraction`
- `BirdMarkerMinPixels`、`TrajectoryLookAheadSeconds`

## 7. 实施顺序

1. 增加镜头调试显示与日志：输入来源、拖拽状态、用户意图角、Desired/Safe/Rendered 位姿、继续运动原因、阻挡对象、卫星意图与近月点时间。
2. 拆分鼠标 Delta 与手柄角速度，建立持久 `UserOrbitIntent`；先证明松手零惯性、无隐式回正，再调整手感参数。
3. 独立改造地面遮挡解算，使其只生成 `SafePose` 并完成地面回归，不同时改绕月状态机。
4. 为发射快照增加只读镜头意图，完成 `SubtleAssist` 与 `CinematicE5` 构图。
5. 最后接入 M11、开场和终局演出，统一状态优先级和转场规范。

前三步属于共享镜头热点，应由集成工作树实施；功能工作树只提供只读状态或事件，不直接修改统一仲裁器。

## 8. 正式验收门槛

### 8.1 地面

- 鼠标以多种速度拖拽后，画面在松手时停留于当前用户角度；不存在可感知的残余角速度，也不会自动朝角色朝向或默认角度回正。
- 30/60/120 FPS 下，相同鼠标物理位移产生近似相同角度；相同手柄保持时间产生近似相同角度，二者的帧率语义均正确。
- 遮挡中仍可连续拖拽；解除遮挡后只恢复安全距离，不改变玩家最后的 Yaw/Pitch，也不返回遮挡前构图。
- 在 30/60/120 FPS 下反复经过地形脊线、建筑墙角、树木和石块，均无单帧突跳、穿模或障碍边缘振荡。
- 解除遮挡后距离单调恢复，不出现一次拉远后立即再次拉近。
- 小型装饰物不会触发显著贴脸；被配置为淡出的对象不会持续遮住鸟群。
- 调试证据能唯一指出造成收缩的 Actor/Component、当前解算状态，以及松手后每一帧继续运动究竟来自 Pivot、遮挡恢复还是显式模式 Blend。

### 8.2 卫星

- 普通借力飞越不发生完整卫星镜头接管，附加倾角不超过配置上限。
- E5 正确路径近月阶段中，鸟或鸟标记至少在 `80%` 的阶段可见；卫星不占满画面，玩家同时能读到飞行方向。
- 进入、近月和退出均无切帧、Up 翻转或来回刷状态。
- 落回主星、撞卫星正面和不相关失败路径不得误触发 `CinematicE5`。
- 镜头优化前后的同输入预测终点、实飞碰撞结果、成功岛与回收结果一致。

## 9. 性能与回归

- 正常帧内使用固定上限的 Sweep/Trace，不允许每帧 `ActorIterator`、动态分配或无界候选搜索。
- 镜头解算的目标预算为 Game Thread `0.2 ms` 以内；超预算时优先退回单 Sweep 安全路径。
- 自动化覆盖状态转换、迟滞和确定性输入；可见 PIE 覆盖遮挡舒适度与近月可读性。两类证据缺一不可。
- 以可重复输入流分别在 30/60/120 FPS 重放鼠标 Delta 和手柄保持输入，验证最终 User Intent、松手残余角速度和时间积分一致性。
- 建立解析碰撞夹具：无障碍、平面墙、窄柱、边缘擦碰、初始穿透和短时装饰命中；必须证明 Sphere Sweep 安全中心距离不会再次扣除完整 Probe Radius。
- 自动化分别记录 Desired/Safe/Rendered Pose 与 Obstruction State；测试不能只断言最终 Actor Transform，否则无法发现层间状态被反写。

## 10. 2026-08-04 地面镜头首版落实

本次保持 `AABTSM4PlayerController` 关闭自动 ViewTarget 管理、单一持久 `AABTSM4PartyCamera` 和 `Possess(NewBird)` 切换契约不变，仅重构地面相机内部：

- 鼠标位移和手柄角速度使用独立入口；鼠标不乘帧时间，手柄经死区/指数曲线后乘 `DeltaSeconds`；关闭 Legacy Mouse Smoothing。
- `OrbitForwardTangent`、Elevation 和 Distance 继续作为持久用户意图；取消最终 Actor Location/Rotation 的二次追赶。普通拖拽松手不会继续消费显示 Lag，Pivot 跟随、显式回正和切鸟混合仍保留自己的状态。
- 遮挡层固定评估原位、抬升、左移、右移四个球形 Sweep 候选；只在替代候选具有明确距离收益时采用，并对候选偏移做有界混合。
- Sphere Sweep 直接使用 `FHitResult::Distance` 表示相机中心安全距离，只减独立 `CameraCollisionSafetyMarginCM`，不再重复扣除 Probe 半径；初始穿透 fail-safe 到紧急近距。
- 硬碰撞立即收缩，候选逃逸和解除遮挡采用按厘米/秒的帧率无关单调扩展；进入、退出具有独立状态与迟滞。
- 每帧快照分别记录 Desired、Safe、Rendered 位姿、遮挡阶段、候选、阻挡 Actor/Component 和直接操纵状态；状态或候选改变时输出 `[ABTS][M4][CameraObstruction]` 日志。
- 相机忽略列表直接读取 Party 成员，不再在每帧 Sweep 前扫描世界中的全部鸟 Actor。

自动化过滤器 `ABTS.Camera.GroundRig` 覆盖手柄 30/60/120 FPS 时间积分、Sweep 中心安全距离，以及硬收缩、退出迟滞、单调恢复和重新遮挡。可见 PIE 仍需按第 8.1 节检查手感、墙角/地形脊线和装饰碰撞。
