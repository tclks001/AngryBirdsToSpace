# ABTS：统一镜头视觉优化设计

> 状态：视觉优化阶段规划稿；2026-07-31 建立。当前只记录问题、职责边界与验收门槛，不修改运行时镜头。
>
> 父级入口：[项目工作流](ABTSProjectWorkflow.md) · [游戏主设计稿](AngryBirdsToSpaceGameDesign.md)
>
> 相关详稿：[M4 球面镜头](M4MultiCharacterOrbitCameraDesign.md) · [M6/M9 弹弓与卫星标定](M6M9SlingshotSatelliteCalibrationDesign.md) · [M11 v2 终局优化](M11V2FinaleOptimizationDesign.md) · [开场与终局演出](OpeningAndFinaleCinematicDirection.md)

## 1. 目标与延期边界

本阶段统一处理地面移动、弹射实飞、卫星引力绕行和终局演出之间的镜头可读性，不改变轨迹积分、Chaos 碰撞、成功岛、发射输入或任务判定。

当前已确认但暂不修复的两项问题是：

1. 鸟接近卫星后，镜头会按距离接管到卫星侧视构图。画面容易只读到卫星、读不到鸟，普通借力飞越也发生不必要的大幅转向。
2. 鸟群在地面移动时，一旦相机射线被地形、建筑或装饰物阻挡，镜头会在短时间内突然拉近；离开遮挡后的恢复速度与进入速度不对称，但边缘反复命中仍可能造成抽动。

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

## 3. 统一职责模型

后续新增统一的镜头仲裁与遮挡解算层。各玩法镜头只提交“期望构图”，最终位姿由同一层完成混合、Up 约束和碰撞安全：

```text
PartyGround / Aim / PrimaryFlight / SatelliteAssist / SatelliteCinematic / Finale
    -> Desired Pose + Focus Set + Framing Constraints + Transition Intent
    -> Camera Arbiter
    -> Obstruction Resolver
    -> Final View
```

约束如下：

- Gameplay 轨迹、碰撞和命中结论保持权威；镜头不得反向修改鸟的位置、速度或发射输入。
- 每一时刻只有一个主镜头状态拥有最终构图，状态转换必须有来源、原因和退出条件。
- 球面地表继续采用主星径向 Up；近月演出采用稳定的轨道平面 frame，并通过连续运输避免 Up 翻转。
- 保留“期望距离”和“碰撞安全距离”两个独立状态，不能让一次遮挡永久改写玩家设置的轨道距离。

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

1. 增加镜头调试显示与日志：当前模式、转换原因、期望/实际距离、阻挡对象、卫星意图与近月点时间。
2. 独立改造地面遮挡解算并完成地面回归，不同时改绕月状态机。
3. 为发射快照增加只读镜头意图，完成 `SubtleAssist` 与 `CinematicE5` 构图。
4. 最后接入 M11、开场和终局演出，统一状态优先级和转场规范。

前两步属于共享镜头热点，应由集成工作树实施；功能工作树只提供只读状态或事件，不直接修改统一仲裁器。

## 7. 正式验收门槛

### 7.1 地面

- 在 30/60/120 FPS 下反复经过地形脊线、建筑墙角、树木和石块，均无单帧突跳、穿模或障碍边缘振荡。
- 解除遮挡后距离单调恢复，不出现一次拉远后立即再次拉近。
- 小型装饰物不会触发显著贴脸；被配置为淡出的对象不会持续遮住鸟群。
- 调试证据能唯一指出造成收缩的 Actor/Component 和当前解算状态。

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
