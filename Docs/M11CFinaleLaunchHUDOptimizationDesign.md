# M11-C/HUD-1：终局发射控制台、轨迹探针与联动画中画

> 状态：HUD-1A 纯数据原型与 HUD-1B 运行时接线已实现并通过自动化；有渲染 PIE 手感验收与 HUD-1C 调优尚未开始。
>
> 父级：[M11-C 终局轨道交互、全景 HUD 与确定性实飞](M11CFinaleInteractionAndPlaybackDesign.md)。
>
> 算法与布局边界：[M11 v2 优化总设计](M11V2FinaleOptimizationDesign.md) · [M11-A 纯数据引力弹弓求解器](M11AGravityAssistSolverDesign.md) · [M11-B 局部布局与全输入域认证](M11BFinaleLayoutCertificationDesign.md)。
>
> 轨道图语义上游：[M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md)。
>
> 项目入口：[ABTS 项目工作流](ABTSProjectWorkflow.md) · [多工作树协作与集成规范](ABTSMultiWorktreeDevelopmentGuide.md)。

## 1. 一句话目标

把终局瞄准从“拖动弹弓袋”改成三个连续旋钮组成的轨道解算控制台，并把轨道全览扩展为互斥的 `Select / Rotate` 两种模式；玩家可在轨迹上放置一个具有稳定语义的“轨迹探针”，画中画在冻结的局部参考系中持续显示同一段轨迹相对附近天体的变化。

核心体验不是替玩家给出答案，而是让玩家看懂：

1. 调整 `Yaw / Pitch / Power` 后，哪一段轨迹发生了多大移动；
2. 这段移动让轨迹更靠近还是更远离当前天体；
3. 轨迹是在天体哪一侧进入、从哪一侧离开，以及偏转方向如何变化；
4. 轨迹已经无法到达原阶段时，失败发生在前缀链的哪一段。

## 2. 已确认的产品决定

### 2.1 终局不再追求普通弹弓同手感

终局成功岛明显小于普通弹弓的有效落点范围，因此不再要求使用可见光标直接拖动弹弓袋。普通 M6 弹弓保持原交互；Space 弹弓单独使用终局控制台。

终局输入仍然是同一组连续参数：

```text
LaunchInput = Yaw × Pitch × Power
```

HUD 只改变输入方式，不改变 M11-A 的积分语义、M11-B 的输入域或 Release 后的权威轨迹。

### 2.2 三旋钮与多档调速

三个旋钮分别控制：

| 旋钮 | 数据 | 粗调用途 | 精调用途 |
|---|---|---|---|
| `YAW` | 水平偏转 | 搜索通过行星①的一侧 | 调整后续助推的侧向偏差 |
| `PITCH` | 俯仰 | 搜索完整轨迹族 | 调整行星接近高度与偏转平面 |
| `POWER` | 发射力度 | 找到行星①的功率门槛 | 调整各段到达时序和最近接距离 |

调速倍率由独立选择器控制，首版建议 `1× / 0.1× / 0.01×`。倍率只缩放“屏幕拖动量或滚轮刻度 → 参数增量”的转换，不量化内部值，也不缩小参数合法范围。

推荐输入：

- 按住旋钮后上下拖动：连续增减；
- 鼠标滚轮悬停旋钮：逐格微调；
- 点击倍率：全局切换三个旋钮的调速档；
- 双击旋钮：恢复本次 Attempt 的进入值，不恢复隐藏 nominal 答案；
- 独立 `LAUNCH` 按钮或专用按键：冻结精确输入并进入 `ReleasePending`。

旋钮松开、轨迹图松开、焦点丢失都不得触发发射。

### 2.3 轨道全览的两个互斥模式

| 模式 | 鼠标行为 | 是否改变发射输入 | 是否改变观察相机 |
|---|---|---:|---:|
| `Select` | 悬停/点击/沿轨迹拖动探针 | 否 | 否 |
| `Rotate` | 虚拟轨迹球旋转，滚轮缩放 | 否 | 是 |

模式按钮必须显式高亮。任何时刻只有一个轨道图输入捕获者。

### 2.4 天体固定、轨迹移动

进入终局瞄准后，轨道全览冻结一次 Attempt 级观察状态：

```text
OverviewViewState
  ViewRotation
  ProjectionScale
  ProjectionCenter
  FinaleLocalCoordinateBasis
```

玩家调节旋钮、求解器发布新轨迹时：

- 主星、三颗行星、UFO、经纬网和作用圈的屏幕位置不变；
- 只有当前轨迹线和沿线事件标记发生变化；
- 不重新执行 PCA 自动转正；
- 不重新自适应缩放掩盖轨迹偏移；
- 越出圆形视口的轨迹被裁剪，并用边缘方向标记提示去向。

只有 `Rotate` 模式允许整体视角旋转，此时天体、轨迹、经纬网和探针一起重投影。`RESET VIEW` 恢复进入 Attempt 时由冻结布局和基准轨迹建立的默认视图。

这条约束是轨迹变化可读性的前提。如果每次新解都重新居中或缩放，玩家的输入效果会被相机抵消。

## 3. 外部项目调研与可借鉴模式

### 3.1 Kerbal Space Program：轨迹分段和事件标记

KSP 的地图界面把未来路径按轨道 patch 和引力作用域变化分段，使用不同颜色/线型表达当前轨迹、未来轨迹、SOI 切换和机动节点；接近目标时还使用最近接标记表达两条轨迹在对应时刻的位置关系。官方 KSPedia 手册明确展示了 Future Path、Maneuver Node 和 Sphere of Influence Change 的分段语义。

对本项目的启示：轨迹不能只被视为一条匿名折线。点击位置需要先归属到 `发射→行星① / 行星①接近 / 行星①→行星② ...` 这样的语义阶段，后续新解才能重新找到“同一段”。

来源：[Kerbal Space Program KSPedia 手册](https://www.kerbalspaceprogram.com/files/KSPedia-XB1.pdf)。

### 3.2 NASA GMAT OrbitView：参考点、观察方向和 Up 必须分别定义

GMAT 的 OrbitView 将相机拆成 `ViewPointReference`、`ViewPointVector`、`ViewDirection`、`ViewUpCoordinateSystem / ViewUpAxis`，并允许用户用鼠标旋转后保留该视图。它强调，仅有“看向哪里”不足以确定稳定画面，还必须冻结参考坐标系和 Up。

对本项目的启示：画中画不能每次仅执行“LookAt 当前轨迹点”。必须保存明确的参考中心、观察法线、Up 和尺度；否则玩家轻微调参时会出现滚转、镜像翻转和重新居中。

来源：[GMAT OrbitView 官方文档](https://documentation.help/gmat/OrbitView.html)。

### 3.3 STK：From/To、参考系、固定 Up 与 Trackball 分工

STK 的 3D Graphics 把 `From Position`、`To Position/Direction`、`Reference Frame` 和 `Constrained Up` 分开，并提供对象约束视角与 Untethered/Trackball 自由旋转两类模式。选中的参考点可以成为当前视图的 To Object，而自由旋转不会改写仿真对象本身。

对本项目的启示：

- `Select` 负责建立一个局部观察参考；
- `Rotate` 只操作全览相机；
- PIP 使用点选时冻结的局部 From/To/Up 合同；
- 两者不能共享一套“鼠标拖动既选点又转相机”的隐式状态。

来源：[STK View Position and Direction](https://help.agi.com/stk/Content/vo/3DViewPosition.htm) · [STK Camera Control Toolbar](https://help.agi.com/stk/12.7.0/Content/vo/cameratoolbar.htm)。

### 3.4 NASA Open MCT：用共同的时间域同步多个视图

Open MCT 的 Time Conductor 使用共同时间系统同步多个数据视图。视图消费同一个时间身份，而不是保存某次渲染得到的像素或数组索引。

对本项目的启示：轨迹探针必须保存求解器域中的稳定身份。全览中的二维点击只负责反解出该身份，PIP、标签和当前轨迹都消费同一个 Probe，而不能互相保存屏幕坐标。

来源：[NASA Open MCT API：Time Conductor](https://github.com/nasa/openmct/blob/master/API.md)。

### 3.5 SpaceEngine 与 Universe Sandbox：选择、聚焦和绕转是不同动作

SpaceEngine 把选中对象、飞向对象、绑定相机和绕对象旋转分开；Universe Sandbox 也区分选择天体、聚焦天体、围绕焦点旋转和自由观察。

对本项目的启示：轨迹点选只建立“观察什么”，不应自动改变全览的观察角；全览旋转也不应替换当前轨迹探针。

来源：[SpaceEngine 用户手册](https://spaceengine.org/manual/user-manual-0980/) · [Universe Sandbox Controls](https://universesandbox.com/support/controls/)。

## 4. 核心方案：语义轨迹探针

### 4.1 为什么不能保存点数组下标

求解器的轨迹点数量会因终止时间、事件、抽稀和新输入而变化。保存 `PointIndex=317` 会导致下一份结果中的 317 指向完全不同的位置。保存旧世界坐标同样不可靠：玩家正是要让轨迹离开该位置。

因此点击后建立：

```text
TrajectoryProbe
  ReferenceResultHash
  SemanticLeg
  PhaseWithinLeg
  ContextBody
  ReferenceSolverTime
  ReferenceLocalPosition
  ReferenceTangent
  FrozenPipView
```

`ReferenceResultHash` 用于显示参考轨迹身份，不要求新结果 Hash 相同。

### 4.2 SemanticLeg

轨迹按照求解器事件划分为稳定语义段：

```text
LaunchToAssist1
Assist1Encounter
Assist1ToAssist2
Assist2Encounter
Assist2ToAssist3
Assist3Encounter
Assist3ToTarget
TargetApproach
```

每段用事件边界而不是绘制点索引定义。行星接近段以 `InfluenceEntry / ClosestApproach / InfluenceExit` 为主要锚点；没有正式 Entry/Exit 的 miss 轨迹仍可对同一行星计算 closest approach，形成诊断窗口。

### 4.3 PhaseWithinLeg

`PhaseWithinLeg ∈ [0,1]`：

- Coast 段按该段累计弧长归一化；
- Encounter 段使用事件对齐的分段参数：Entry→Closest 映射到 `[0,0.5]`，Closest→Exit 映射到 `[0.5,1]`；
- 点击恰好位于事件 glyph 时直接吸附到 `0 / 0.5 / 1`；
- 保存 `ReferenceSolverTime` 仅用于标签、回归和诊断，不作为唯一重定位依据。

这种身份既能容忍积分采样点变化，又能在接近时序略有变化后继续观察同一次助推的相同阶段。

### 4.4 从屏幕点击生成 Probe

Select 模式下：

1. 在圆形裁剪内寻找离鼠标最近的可见或虚线轨迹段；
2. 使用屏幕空间点到线段距离，默认命中半径建议 `10 px`，高 DPI 后按 UI Scale 修正；
3. 在线段上插值得到源轨迹弧长和 solver time；
4. 根据前后事件确定 `SemanticLeg + PhaseWithinLeg`；
5. 选择 ContextBody；
6. 冻结参考轨迹局部窗口与 PIP 相机；
7. 在全览中绘制十字探针，并把对应局部窗口发送到 PIP。

若多条投影轨迹在屏幕交叉，优先级为：

1. 鼠标最近的线段；
2. 实线优先于球后虚线；
3. 当前高亮的语义段优先；
4. 仍相同则选择 solver time 更早者，并允许滚轮在重叠候选间切换。

悬停只高亮，不创建 Probe。按住并沿轨迹拖动可以 scrub；松开时才冻结新的参考 PIP，避免拖动期间频繁创建 Scene Capture。

### 4.5 ContextBody 选择

ContextBody 不简单等于欧氏距离最近的天体，而使用归一化影响距离：

```text
BodyScore = Distance(ProbePoint, BodyCenter)
          / max(InfluenceRadius, VisualRadius)
```

- Encounter 段固定使用该段所属行星；
- TargetApproach 固定使用 UFO；
- Coast 段取分数最低的前后两个任务天体中的一个；
- ContextBody 一旦由点击冻结，不因旋钮微调自动跳到另一颗天体；
- 玩家重新点选或点击 `REBASE PIP` 时才允许更换。

PIP 标签明确显示 `ASSIST 2 / OUTBOUND`、`COAST 2→3` 等语义，不能只显示匿名的 `POINT 417`。

## 5. 画中画联动设计

### 5.1 冻结参考系，而不是追着当前轨迹重新构图

点击轨迹时，从参考结果构建一次 `FrozenPipView`：

```text
FrozenPipView
  ContextBodyCenter
  ViewCenter
  ViewForward
  ViewUp
  ViewRight
  OrthoWidth / CameraDistance / FOV
  ReferenceWindowBounds
```

行星接近段推荐参考系：

```text
Radial    = normalize(ReferencePoint - BodyCenter)
Tangent   = normalize(ReferenceVelocity relative to body)
PlaneN    = normalize(Radial × Tangent)
ViewForward = sign-stabilized PlaneN
ViewUp      = projected FinaleLocal +Z
```

若退化，则依次回退到参考轨迹拟合平面法线、全览视图法线和 Finale Local 固定轴。法线符号必须与创建 Probe 时的全览朝向对齐，禁止下一帧翻面。

ViewCenter 和尺度一次性包住：

- ContextBody 的视觉圆；
- 参考 Probe 点；
- Probe 前后固定弧长窗口；
- 必要时显示作用圈的一部分。

创建后不再因当前轨迹移动而自动转向、居中或缩放。

### 5.2 PIP 中显示什么

推荐画面层级：

1. 静态 Scene Capture：ContextBody 低模、必要的邻近天体和星空背景；
2. ContextBody 作用圈的低对比度轮廓；
3. 参考轨迹局部线：细灰色或灰色虚线；
4. 当前轨迹局部线：高亮青色实线；
5. 参考 Probe：白色十字；
6. 当前重新定位后的 Probe：实心菱形；
7. 当前速度切线短箭头；
8. 最近接点、距离和 `ENTER / CA / EXIT` 标签；
9. 当前轨迹已离开 PIP 时的边缘箭头和距离文字。

参考轨迹不是标准答案，而是“玩家点击那一刻自己的轨迹”。它只用于前后对比，不泄露 nominal 解。

### 5.3 旋钮变化后的联动

每当 latest-only 求解器发布新结果：

1. 保持 `FrozenPipView` 不变；
2. 在新结果中解析同一 `SemanticLeg`；
3. 用 `PhaseWithinLeg` 得到当前 Probe；
4. 抽取该 Probe 前后固定物理弧长的当前局部轨迹；
5. 投影到冻结 PIP；
6. 更新当前线、菱形、最近接距离与前缀状态；
7. 不重新捕获静态天体画面。

因此玩家转动旋钮时会直接看到亮色轨迹相对灰色参考轨迹和静止行星移动。

### 5.4 当前新解没有原语义段

这是重要反馈，不能静默跳到别处：

- 若原来选中 `Assist2Encounter`，但新轨迹不再进入行星②作用圈，PIP 仍以行星②和冻结视角显示该新轨迹对行星②的 closest-miss 局部窗口；
- 若新轨迹在到达该阶段前已经碰撞或终止，保留灰色参考线，当前线画到边缘，并显示 `TRAJECTORY ENDS BEFORE ASSIST 2`；
- 不得自动把 PIP 改为行星①或新的最近天体；
- 玩家可点击 `FOLLOW CURRENT TARGET` 回到现有的自动“最早未完成助推”预览。

### 5.5 REBASE PIP

当玩家已经接受当前变化，希望继续观察更小修正，可以点击 `REBASE PIP`：

- 当前轨迹成为新的灰色参考；
- 当前 Probe 位置成为新十字；
- 重新计算并冻结局部构图；
- 不改变任何发射参数。

这相当于测量工具的重新置零，不能自动触发。

## 6. 轨道全览 Rotate 模式

Rotate 模式采用虚拟轨迹球：

- 左键拖动：绕全览中心旋转；
- 滚轮：缩放；
- 双击或 `RESET VIEW`：恢复 Attempt 默认视角；
- 默认约束 Finale Local `+Z` 为稳定 Up；可提供一个高级“自由滚转”选项，但首版不需要；
- 旋转只修改 `OverviewViewState`，不递增 AimRevision，不启动求解器。

若已经存在 Probe：

- 全览中的 Probe glyph 随整体视图重投影；
- PIP 的 FrozenPipView 保持不变；
- Rotate 不重新选择轨迹点，也不改变 ContextBody。

这种分离允许玩家转动全览理解三维形态，同时让局部对比画面保持一个稳定测量基准。

## 7. 输入状态机

```text
FinaleHudCapture
  None
  AdjustYaw
  AdjustPitch
  AdjustPower
  ScrubTrajectoryProbe
  RotateOverview
  AdjustOverviewZoom
```

规则：

1. 同一时刻最多一个 Capture；
2. Capture 开始后暂时关闭 Actor click/mouse-over 透传；
3. 鼠标离开控件后仍由原 Capture 接收，直至松开或焦点丢失；
4. 焦点丢失只取消操作，不回滚已生效输入，也不发射；
5. `LAUNCH` 只在 `None` 或其自身按钮按下时生效；
6. `RotateOverview` 和 Probe 操作永不调用 `ApplyAbsoluteCursorAim()`；
7. Release 后全部控制禁用，权威飞行相机接管。

## 8. 数据与代码边界建议

### 8.1 新纯数据类型

建议增加但不纳入 Solver/Certification Hash：

```text
FABTSM11FinaleControlPanelState
FABTSM11OverviewViewState
FABTSM11TrajectorySemanticMap
FABTSM11TrajectoryProbe
FABTSM11FrozenPipView
FABTSM11ProbeProjection
FABTSM11FinaleHudCaptureState
```

这些类型属于 M11-C Presentation/Interaction，不得写回 `FABTSM11FinaleLayoutPreset`。

### 8.2 现有类型的演进

当前 `FABTSM11OrbitalDiagramSnapshot` 已经是投影后的二维快照，无法支持无损 Rotate 和重新命中。需要在它之前保留一个 Attempt 级三维表现快照：

```text
FABTSM11OrbitalSceneSnapshot
  3D trajectory samples + solver time + arc length
  semantic event boundaries
  body centers/radii/glyph identities
  world latitude/longitude basis
  source trajectory hash
```

数据流改为：

```text
Latest Trajectory Result
  -> OrbitalSceneSnapshot (3D, immutable per result)
  -> OverviewViewState
  -> OrbitalDiagramSnapshot (2D draw + hit proxies)
  -> Select hit
  -> TrajectoryProbe
  -> FrozenPipView
  -> ProbeProjection for every latest result
```

### 8.3 PIP 复用现有架构

当前 M11-C 已经采用“Scene Capture 只在目标切换时更新，当前轨迹由 HUD 纯数据叠加”的方式。本方案延续这一架构：

- Probe 创建、ContextBody 变化或显式 Rebase 时更新一次 Capture；
- 旋钮变化只重建当前局部轨迹投影；
- Renderer 工作始终留在 Game Thread；
- 后台求解 future 只返回纯数据；
- 旧 revision 不得更新 ProbeProjection。

## 9. 性能预算

| 工作 | 触发频率 | 目标预算 |
|---|---:|---:|
| 三旋钮状态更新 | 输入帧 | `<0.1 ms` |
| 最新轨迹异步求解 | 参数变化后 latest-only | 保持现有同帧/近同帧目标 |
| 3D→2D 全览重投影 | 新结果或 Rotate | `<0.4 ms / 900点` |
| 轨迹命中测试 | Select 鼠标移动 | `<0.1 ms` |
| Probe 语义重定位与局部抽取 | 每份新结果 | `<0.2 ms` |
| Scene Capture | 新 Probe/Context/Rebase | 单次，不随旋钮连续触发 |

Rotate 不允许触发 M11-A Solve。选择拖动过程中可实时移动 Probe，但 Scene Capture 只在 ContextBody 改变或松开确认后刷新。

## 10. 自动化门槛

### 10.1 纯数据单元测试

建议新增：

| 过滤器 | 阻断内容 |
|---|---|
| `ABTS.M11C.HUD.Unit.ControlKnobs` | 三旋钮范围、倍率、连续值、不越界、双击恢复和焦点丢失 |
| `ABTS.M11C.HUD.Unit.OverviewViewInvariance` | 只改 Aim 时所有天体投影不动；Rotate 时天体和轨迹使用同一变换 |
| `ABTS.M11C.HUD.Unit.TrajectoryHitTest` | 最近线段、交叉歧义、实/虚线优先级、DPI 命中半径 |
| `ABTS.M11C.HUD.Unit.SemanticProbe` | 点索引变化后仍解析同一 Leg/Phase；Entry/CA/Exit 吸附 |
| `ABTS.M11C.HUD.Unit.FrozenPipView` | 新结果不改变 ViewForward/Up/Scale；退化法线不翻转 |
| `ABTS.M11C.HUD.Unit.ProbeRemap` | pass→miss、提前终止、跨 prefix 回退和 Rebase |
| `ABTS.M11C.HUD.Unit.InputCapture` | 各 Capture 互斥、松开不发射、Launch 唯一入口 |

### 10.2 Runtime 自动化

至少验证：

1. 进入终局后不再调用 M11 的绝对鼠标拖袋瞄准；
2. 旋钮输入和求解请求精确一致；
3. 同一输入的 Preview/Release Hash 仍一致；
4. Rotate 100 帧不增加 AimRevision 和 SolveCount；
5. Probe 持续跨越至少 20 份新轨迹结果，不读取 stale revision；
6. Scene Capture 次数只随 Probe/Context/Rebase 变化；
7. 普通 M6 弹弓输入与 Release 手势不受影响。

### 10.3 PIE 视觉与手感验收

- [ ] `1×` 可以在数秒内扫过完整合法输入域；
- [ ] `0.1× / 0.01×` 能稳定进入并留在小型前缀成功集；
- [ ] 调参时全览天体像素位置无肉眼漂移；
- [ ] 当前轨迹相对参考轨迹的移动方向清晰；
- [ ] Select 点击目标段与 PIP 显示段一致；
- [ ] 轨迹失去某颗行星时，PIP 保持该行星并明确展示 miss；
- [ ] PIP 不因微小调参滚转、镜像翻转、缩放或乱晃；
- [ ] Rotate 可从侧面验证偏转平面，且不改变发射参数；
- [ ] 交叉轨迹附近仍可选择预期的前/后段；
- [ ] PIP 离屏箭头、距离和阶段标签不遮挡主体；
- [ ] 松开任意控件不发射，只有 `LAUNCH` 发射；
- [ ] Release 后隐藏控制台并沿权威轨迹正常跟随。

## 11. 失败模式与排错

| 症状 | 根因 | 修复原则 |
|---|---|---|
| 调旋钮后行星跟着漂移 | 每份结果重新 PCA/AutoFit | Attempt 级冻结 OverviewViewState |
| PIP 中轨迹始终居中，看不出变化 | 以当前线重新构图 | 冻结点选时的相机与尺度 |
| PIP 突然跳到另一颗星 | ContextBody 每帧按最近距离重选 | ContextBody 属于 Probe，只能显式替换 |
| 点选后下一解显示错误轨迹段 | 保存了数组下标 | 保存 SemanticLeg + PhaseWithinLeg |
| 行星背面轨迹难以点中 | 虚线和实线命中无规则 | 使用明确优先级并允许候选切换 |
| 轨迹微调造成 PIP 翻面 | `Radial × Tangent` 符号不稳定 | 与冻结参考法线点积定向并固定 Up |
| Rotate 时不断产生求解任务 | 相机输入混入 Aim 输入 | Capture 状态隔离；Rotate 不增 AimRevision |
| 旋钮松开误发射 | 复用了旧拖袋 Release | 独立 Launch 门，其他 MouseUp 永不 Release |
| 新轨迹到不了选中阶段，PIP 空白 | 只支持成功事件段 | 对同一天体生成 closest-miss 或提前终止诊断 |

## 12. 实施顺序与阶段边界

### HUD-1A：纯数据交互原型

- 三旋钮连续映射和倍率；
- OverviewViewState 与天体不动合同；
- 3D SceneSnapshot、2D 投影和命中代理；
- SemanticProbe、ContextBody、FrozenPipView；
- 全部纯数据单元测试。

实现落点：

- 公共纯数据接口：`Source/ABTSRuntime/Public/UI/ABTSM11FinaleHUDData.h`；
- 实现：`Source/ABTSRuntime/Private/UI/ABTSM11FinaleHUDData.cpp`；
- 自动化：`Source/ABTSRuntime/Private/UI/ABTSM11FinaleHUDDataAutomationTests.cpp`；
- 已实现 `Coarse / Fine / UltraFine = 1 / 0.1 / 0.01` 三档连续控制；
- 轨道全览的 Attempt 级视图与每份求解结果的三维场景快照相互分离，调节 Aim 只替换轨迹，不重拟合天体投影；
- 点击结果保存为 `SemanticLeg + PhaseWithinLeg + ContextBody`，新结果优先精确重映射，缺失行星遭遇时回退为 closest miss，尚未到达未来段时回退到轨迹终点；
- PIP 的方向、Up、中心和尺度在创建 Probe 时冻结，只有显式 Rebase 才重建；
- 七项 `ABTS.M11C.HUD.Unit.*` 测试已在新进程 NullRHI 下通过，Development Editor 普通全链接和 Forced Unity 全量重建均通过。

### HUD-1B：M11-C Runtime 接线

- PlayerController 输入 Capture；
- HUD 绘制与模式按钮；
- latest-only 结果驱动 Probe remap；
- PIP 静态 Capture + 当前/参考轨迹叠加；
- 独立 Launch；
- fresh Runtime 自动化。

实现状态：

- `AABTSM11PlayerController` 已停止把太空弹弓入口点击/任意鼠标松开解释为 Release；入口仅开启控制台，只有 `LAUNCH` 捕获在按钮内松开时调用 `RequestRelease()`；
- `AABTSM11FinaleHUD` 已接入三旋钮、三档倍率、Select/Rotate、Reset View、Rebase 和 Auto PIP，并以独占 `FinaleHudCapture` 路由按下、持续拖动、松开与焦点丢失；
- 旋钮拖动和滚轮微调经 `FABTSM11FinaleControlPanelState` 映射后进入现有 Stabilizer，继续复用 latest-only 异步求解、Preview/Release 同输入 Hash 和前缀稳定器；
- `AABTSM11FinaleInteractionSystem` 在首份发布结果上冻结 Attempt 级 `OverviewViewState`，后续 Aim 变化只替换轨迹场景并重投影；Rotate/Zoom 不递增 `AimRevision`、不启动 Solve；
- Select 松开后创建 `TrajectoryProbe`，Scrub 过程中不反复 Scene Capture；后续新结果仅执行语义重映射和 HUD 轨迹叠加；
- Probe PIP 使用冻结正交视框，灰色虚线显示点选时参考轨迹，青色实线显示 latest-only 当前轨迹；closest miss 和提前终止状态显式显示；
- 静态 Scene Capture 只在首次自动目标、自动目标切换、新 Probe、Rebase 或返回 Auto PIP 时刷新；Aim-only 更新不刷新；
- 全览天体、主星绝对经纬网、作用圈、UFO 和轨迹使用同一冻结三维投影；只有 Rotate 模式允许整体重投影；
- 新增 `TargetCaptureCount / HudOverviewRevision / HudProbeRevision` 运行时诊断计数，供 HUD-1C PIE 性能与行为验收使用。

### HUD-1C：有渲染 PIE 调优

- 旋钮尺寸、拖动增益和倍率；
- 命中半径、交叉段选择；
- PIP 构图、参考线对比、标签和离屏提示；
- 轨迹球速度、Zoom 与 Up 约束；
- 性能和输入焦点回归。

### M11-D 保留内容

M11-D 只负责最终旋钮美术、材质、动画、音效、触觉反馈和剧情层布局润色；不得重新定义 Probe、输入、求解、Release 或 PIP 参考系。

只要本阶段不改变 M11-A 数值、M11-B 布局、输入合法域和分类规则，就不需要重跑 M11-B 完整输入域认证。仍必须重跑 M11-C Preview/Release、一致性和终端接管闭包。

## 13. 结论

轨迹点选与画中画联动的关键不是“让相机追着点跑”，而是建立一个可跨求解结果重定位的语义探针：

```text
屏幕点击
  -> SemanticLeg + Phase
  -> 冻结 ContextBody / Camera / Scale
  -> 新轨迹解析同一语义段
  -> 在冻结 PIP 中显示亮色当前线相对灰色参考线的位移
```

这样既能保证画面稳定，又能直接显示旋钮调节带来的因果变化；当轨迹不再经过某颗行星时，玩家看到的是一次清楚的 miss，而不是 PIP 自动换目标或重新居中后看似“什么都没发生”。
