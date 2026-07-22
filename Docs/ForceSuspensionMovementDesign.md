# 力模型与径向悬挂移动设计

> 状态：C++ 已实现。默认启用 `ForceSuspension`，旧 `LegacySweep` 完整保留用于对照。
>
> 上游约束：`CellTopo` 仍是地形唯一逻辑源；移动系统只读取 Planet 提供的 CellTopo 派生表面高度与法线，不把连续渲染网格或碰撞结果反写到 PCG。

## 1. 修复目标

旧移动使用 Capsule Sweep 与 M3 的 `Complex As Simple` 程序化三角网格接触。渲染顶点法线虽然已经平滑，但 Sweep 的 `ImpactNormal` 仍是逐三角面法线；跨越三角边时会反复改变滑动方向。此外，旧模式还需要径向位置修正。两者叠加后会产生可见的爬坡卡顿。

新模式不再把地形三角网格当作玩家的物理支撑面：

- 移动力、阻力、径向重力全部先作为力累加，再除以虚拟质量积分速度。
- 地表高度通过 `GetSurfaceRadiusAtDirection` 查询。
- 地表法线通过 `GetSurfaceNormalAtDirection` 查询。
- 径向弹簧和阻尼维持胶囊底部到连续地表的距离。
- 玩家 Capsule 的障碍物 Sweep 只忽略 Planet 的 `ContinuousSurface` 组件，但仍阻挡建筑、岩石和其他 Planet 子组件。
- 本地固定子步默认按 `1/120 s` 积分，降低帧率变化对弹簧的影响。

## 2. 模块划分

| 类 | 职责 |
| --- | --- |
| `UABTSRadialForceMovementComponent` | 输入力、切向阻力、径向重力、速度积分、子步、跳跃、非地形障碍 Sweep。 |
| `UABTSRadialSurfaceSuspensionComponent` | 查询地表、计算径向弹簧/阻尼、接地迟滞、跳跃后的悬挂禁用窗口。它不直接移动 Actor。 |
| `UABTSM25RadialMovementComponent` | 保留旧 Kinematic Sweep 实现，只在 `LegacySweep` 模式 Tick。 |
| `AABTSM25BirdCharacter` | 根据模式将输入和跳跃转发给唯一活动的移动组件。 |
| `AABTSMovementModeSelector` | 可选的关卡设置 Actor，用于在编辑器中覆盖 Pawn 默认模式。无 Tick。 |

两个移动组件不会同时 Tick。出生点传送会同时清空两套组件的速度、输入和接地缓存，避免切换或重生时继承旧状态。

## 3. 力模型

```text
Up = normalize(CharacterLocation - PlanetCenter)
N  = queried smooth surface normal

FGravity = -Up * Mass * GravityAcceleration
FMove    = SurfaceMoveDirection * Mass * MoveAcceleration * InputMagnitude
FDrag    = -SurfaceTangentVelocity * Mass * DragPerSecond
FSupport = Up * Mass * SupportAcceleration

Acceleration = (FGravity + FMove + FDrag + FSupport) / Mass
Velocity    += Acceleration * SubstepSeconds
```

默认地面移动加速度为 `3600 cm/s²`、线性阻力为 `5.3 s⁻¹`，因此满输入的理论终端速度约为：

```text
TerminalSpeed = 3600 / 5.3 ≈ 679 cm/s
```

超过 `DesignMaxGroundSpeedCMPerSec` 后会增加一项超速阻力，但不会每帧硬截断速度。空中阻力只作用于切向速度，不会直接吃掉径向跳跃速度。

## 4. 径向悬挂

```text
TargetRadius = SurfaceRadius + CapsuleHalfHeight + GroundClearance
HeightAboveTarget = CurrentRadius - TargetRadius
RadialSpeed = dot(Velocity, Up)

Omega = 2 * PI * SpringFrequency
ASpring = -Omega² * HeightAboveTarget
ADamping = -2 * DampingRatio * Omega * RadialSpeed
ASupport = GravityAcceleration + ASpring + ADamping
```

`GravityAcceleration` 前馈用于消除静止时的弹簧下沉。`SupportCaptureDistanceCM` 限制悬挂只能在地表附近捕获角色；跳跃后 `JumpSupportDisableSeconds` 暂停悬挂，防止跳跃冲量同帧被抵消。

接地判定只使用连续地表高度误差并带迟滞：进入使用 `GroundedEnterDistanceCM`，保持接地使用 `GroundedExitDistanceCM`。普通爬坡产生的径向速度不再导致 Grounded 抖动。

## 5. 编辑器切换步骤

### 推荐：关卡 Selector

1. 编译并重新打开 UE 编辑器。
2. 打开 `/Game/Maps/L_ABTS_M3`。
3. 在 **Place Actors** 中搜索 `ABTSMovementModeSelector`，放入关卡任意位置；其 Transform 没有玩法意义。
4. 选中该 Actor，在 **Details > ABTS > Movement > Movement Mode** 中选择：
   - `Force + Radial Suspension (Recommended)`：新模式。
   - `Legacy Kinematic Sweep`：旧模式。
5. 保存地图并启动 PIE。
6. Output Log 应出现且只出现一个活动模式摘要：

```text
[ABTS][MovementMode] Level selector found: ...
[ABTS][MovementMode] Active=ForceSuspension LegacyTick=0 ForceTick=1
```

切换旧模式后第二行应变为：

```text
[ABTS][MovementMode] Active=LegacySweep LegacyTick=1 ForceTick=0
```

关卡中没有 Selector 时，使用 `AABTSM25BirdCharacter` 的类默认值；当前 C++ 默认值为 `ForceSuspension`。同一关卡只放置一个 Selector。

### Blueprint 类默认值

也可以创建 `AABTSM25BirdCharacter` 的 Blueprint 子类，在 **Class Defaults > ABTS > Movement** 设置模式，并将 GameMode 的 Default Pawn Class 改为该 Blueprint。关卡 Selector 的优先级高于 Pawn 类默认值。

## 6. 参数入口

选择运行时 Pawn 的 Components 面板：

- `ForceMovement`
  - **Force**：虚拟质量、重力、移动加速度、空中控制。
  - **Drag**：地面和空中切向阻力。
  - **Speed**：设计最大速度和超速阻力。
  - **Jump**：跳跃速度和输入缓冲。
  - **Simulation**：最大子步时长和最大子步数。
- `SurfaceSuspension`
  - **Ground**：离地高度、捕获距离、接地迟滞。
  - **Spring**：弹簧频率、阻尼比、最大支撑加速度。
  - **Jump**：跳跃后暂停悬挂的时间。

首轮验收保持默认值。若出现弹跳，先降低 `SpringFrequencyHz` 或提高 `SpringDampingRatio`，不要提高地形网格细分来掩盖控制器问题。

## 7. 验收

### 功能与视觉

1. `ForceSuspension` 下连续爬坡，角色没有逐三角形的周期性停顿。
2. 下坡和横穿坡面时速度连续；松开输入后由阻力平滑减速。
3. 角色 Down 始终朝球心，极点附近相机与方向不退化。
4. 坡面按 Space 能沿径向跳起，悬挂不会立即将角色吸回。
5. 放置一个 `BlockAll` Cube：角色仍被 Cube 阻挡并沿其表面滑动；Planet 下未来新增的非地形碰撞组件也不会被一并忽略。
6. Planet 地形不会通过三角碰撞阻挡玩家；角色也不能明显沉入或悬浮于查询地表。

### 日志

```text
[ABTS][ForceSuspension] Movement ready. ...
[ABTS][MovementMode] Active=ForceSuspension LegacyTick=0 ForceTick=1
[ABTS][ForceSuspension][Ground] Grounded ...
[ABTS][ForceSuspension][Jump] Accepted ...
```

普通爬坡期间不应连续交替输出 `Grounded/Airborne`。

### 回归

切换到 `LegacySweep` 后，原移动、跳跃和旧 `[ABTS][M2.5]` 日志仍可运行，便于在同一地图、同一种子和同一路线上 A/B 对照。

## 8. 当前边界

- 新方案是确定性的“虚拟力积分”，不是把玩家 Capsule 交给 Chaos 刚体模拟。
- 地形支撑来自 CellTopo 派生查询；非地形障碍仍由 Capsule Sweep 处理。
- 暂不实现坡面自然滑落、道路阻力差异、水体浮力、网络预测和移动平台。
- 后续道路、水域玩法可直接修改移动力、阻力和悬挂参数，不需要改地形网格碰撞。
