# ABTS：统一镜头视觉优化设计

> 状态：视觉优化阶段规划稿；2026-07-31 建立，2026-08-03 补充直接操纵与遮挡联合方案。当前只记录问题、职责边界与验收门槛，不修改运行时镜头。
>
> 父级入口：[项目工作流](ABTSProjectWorkflow.md) · [游戏主设计稿](AngryBirdsToSpaceGameDesign.md)
>
> 相关详稿：[M4 球面镜头](M4MultiCharacterOrbitCameraDesign.md) · [M6/M9 弹弓与卫星标定](M6M9SlingshotSatelliteCalibrationDesign.md) · [M11 v2 终局优化](M11V2FinaleOptimizationDesign.md) · [开场与终局演出](OpeningAndFinaleCinematicDirection.md)

## 1. 目标与延期边界

本阶段统一处理地面移动、弹射实飞、卫星引力绕行和终局演出之间的镜头可读性，不改变轨迹积分、Chaos 碰撞、成功岛、发射输入或任务判定。

当前已确认但暂不修复的三项问题是：

1. 鸟接近卫星后，镜头会按距离接管到卫星侧视构图。画面容易只读到卫星、读不到鸟，普通借力飞越也发生不必要的大幅转向。
2. 鸟群在地面移动时，一旦相机射线被地形、建筑或装饰物阻挡，镜头会在短时间内突然拉近；离开遮挡后的恢复速度与进入速度不对称，但边缘反复命中仍可能造成抽动。
3. 地面轨道镜头拖拽缺少直接操纵感：拖拽过程中镜头像带有质量和惯性，输入停止后画面仍继续运动，并容易把遮挡后的距离恢复或 Pivot 跟随误读为“镜头被拉回”。玩家期望的是拖到哪里就停在哪里，除非显式按下回正键或玩法状态切换要求接管。

月面着陆画中画已经独立完成验收，不属于本稿的待修问题。后续不得为了修主镜头而改变画中画的月面终点资格、固定俯角、径向 Up 或共享着陆相机距离。

## 2. 当前实现与根因

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

## 3. 统一职责模型

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

## 4. 地面遮挡改进

### 4.1 解算策略

1. 保留球形 Sweep 作为安全底线，同时记录阻挡 Actor、Component、对象类型、距离和持续时间。
2. 为进入与退出分别设置迟滞：危险穿透仍可立即收缩，普通边缘命中需连续成立后才进入遮挡状态。
3. 收缩使用有速度上限的临界阻尼；恢复更慢且单调，禁止在障碍边缘逐帧伸缩。
4. 在完整拉近前尝试有上限的抬升或左右候选位姿。候选数量固定，避免不可控的每帧 Trace 成本。
5. 将遮挡物分为：
   - 地形、不可穿建筑：必须保证碰撞安全；
   - 可淡出的大型前景：优先透明/抖动淡出，必要时再收缩；
   - 树木、石块、小型 HISM 等装饰：使用专用响应或忽略阈值，不能因单个小物体大幅拉近。
6. 接近 Pivot 的紧急收缩只用于防穿模，不作为普通舒适构图。

### 4.2 可调参数

- `ProbeRadiusCM`、`CollisionSafetyMarginCM`、`EmergencyDistanceCM`
- `ObstructionEnterDelaySeconds`、`ObstructionExitDelaySeconds`
- `PullInSpeed`、`RestoreSpeed`、`MaxPullInSpeedCMPerSecond`
- `MaxLateralEscapeCM`、`MaxVerticalEscapeCM`
- 可淡出对象类型、忽略对象类型与最小遮挡投影面积

### 4.3 直接操纵与遮挡的联合解算

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

## 5. 绕月镜头改进

### 5.1 镜头意图

发射时根据权威预测快照锁存本次镜头意图：

- `None`：预测不构成有效卫星接近，保持主星飞行镜头。
- `SubtleAssist`：会借助卫星引力但不进入 E5 成功岛。只允许轻微倾斜、适度拉远和焦点前移，不切成卫星全景。
- `CinematicE5`：预测进入 E5 成功岛或其容差带，允许采用类似深空任务引力助推演示的“卫星边缘 + 鸟 + 前方轨迹”组合构图。

实飞期间可以因轨迹偏差降级意图，但不得仅凭距离从 `None` 升级为完整演出。这样既保留玩家对普通发射方向的连续感，也让正确路径获得明确奖励。

### 5.2 构图

- 使用鸟、卫星可见边缘、短距离前视轨迹和 E5（若适用）的屏幕空间包围框求取相机距离，不再固定注视卫星球心。
- 卫星建议占画面短边约 `20%–45%`；鸟或鸟的高对比标记在近月主要阶段必须持续可见。
- 鸟本体过小时显示非侵入式标记或短尾迹，不放大碰撞体，也不改变 Actor Scale。
- `SubtleAssist` 的附加倾角设上限，建议默认不超过 `8°`。
- 使用预测近月点而非当前距离决定进入时机，并设置进入提前量、最短保持和退出混合时长。
- E5 命中保持与失败回收仍沿用现有 Gameplay 事件，不由相机自行猜测。

### 5.3 可调参数

- `SatelliteSubtleTiltMaxDegrees`
- `SatelliteCinematicEligibilityToleranceCM`
- `PeriapsisLeadSeconds`、`MinimumHoldSeconds`、`ExitBlendSeconds`
- `SatelliteMinScreenFraction`、`SatelliteMaxScreenFraction`
- `BirdMarkerMinPixels`、`TrajectoryLookAheadSeconds`

## 6. 实施顺序

1. 增加镜头调试显示与日志：输入来源、拖拽状态、用户意图角、Desired/Safe/Rendered 位姿、继续运动原因、阻挡对象、卫星意图与近月点时间。
2. 拆分鼠标 Delta 与手柄角速度，建立持久 `UserOrbitIntent`；先证明松手零惯性、无隐式回正，再调整手感参数。
3. 独立改造地面遮挡解算，使其只生成 `SafePose` 并完成地面回归，不同时改绕月状态机。
4. 为发射快照增加只读镜头意图，完成 `SubtleAssist` 与 `CinematicE5` 构图。
5. 最后接入 M11、开场和终局演出，统一状态优先级和转场规范。

前三步属于共享镜头热点，应由集成工作树实施；功能工作树只提供只读状态或事件，不直接修改统一仲裁器。

## 7. 正式验收门槛

### 7.1 地面

- 鼠标以多种速度拖拽后，画面在松手时停留于当前用户角度；不存在可感知的残余角速度，也不会自动朝角色朝向或默认角度回正。
- 30/60/120 FPS 下，相同鼠标物理位移产生近似相同角度；相同手柄保持时间产生近似相同角度，二者的帧率语义均正确。
- 遮挡中仍可连续拖拽；解除遮挡后只恢复安全距离，不改变玩家最后的 Yaw/Pitch，也不返回遮挡前构图。
- 在 30/60/120 FPS 下反复经过地形脊线、建筑墙角、树木和石块，均无单帧突跳、穿模或障碍边缘振荡。
- 解除遮挡后距离单调恢复，不出现一次拉远后立即再次拉近。
- 小型装饰物不会触发显著贴脸；被配置为淡出的对象不会持续遮住鸟群。
- 调试证据能唯一指出造成收缩的 Actor/Component、当前解算状态，以及松手后每一帧继续运动究竟来自 Pivot、遮挡恢复还是显式模式 Blend。

### 7.2 卫星

- 普通借力飞越不发生完整卫星镜头接管，附加倾角不超过配置上限。
- E5 正确路径近月阶段中，鸟或鸟标记至少在 `80%` 的阶段可见；卫星不占满画面，玩家同时能读到飞行方向。
- 进入、近月和退出均无切帧、Up 翻转或来回刷状态。
- 落回主星、撞卫星正面和不相关失败路径不得误触发 `CinematicE5`。
- 镜头优化前后的同输入预测终点、实飞碰撞结果、成功岛与回收结果一致。

## 8. 性能与回归

- 正常帧内使用固定上限的 Sweep/Trace，不允许每帧 `ActorIterator`、动态分配或无界候选搜索。
- 镜头解算的目标预算为 Game Thread `0.2 ms` 以内；超预算时优先退回单 Sweep 安全路径。
- 自动化覆盖状态转换、迟滞和确定性输入；可见 PIE 覆盖遮挡舒适度与近月可读性。两类证据缺一不可。
