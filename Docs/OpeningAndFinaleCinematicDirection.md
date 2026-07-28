# 开局与终局自动演出导演稿

> 状态：导演与编排设计，尚未实现。  
> 目标：把“白鸟被掳—四鸟启程—三重引力弹弓—救回白鸟”的叙事转换为可由 UE 运行时自动编排的镜头、角色与事件数据；不把程序生成世界绑定到手工绝对世界坐标。  
> 上游： [主设计稿](AngryBirdsToSpaceGameDesign.md) · [M4 多角色 Orbit Camera](M4MultiCharacterOrbitCameraDesign.md) · [CuteBird 迁移与动画](CuteBirdMigrationAndAnimationDesign.md) · [M11.0 终局前置收口](M110PreFinaleClosureDesign.md) · [M11 三重引力弹弓预演](M11GravityAssistAlgorithmPrevisualization.md)

## 1. 导演目标与不可破坏的玩法边界

开局让玩家先看见五只鸟作为一个快乐、可辨认的小群体，再以一次明确的掳走事件建立终局目标；镜头停在四鸟面向世界的背影上后才交还控制权。终局则先让玩家读懂“四鸟一起出发”和“三颗行星连续助推”，最后以撞击、破碎、解救和零重力团聚收束。

演出是表现层的状态机，不得改变下列权威数据：

- M4 的四鸟 Party 仍只有红、蓝、黄、黑四名 Gameplay 成员；白鸟仅是表现 Actor，永不进入 Party、库存、可切换主控或普通碰撞链路。
- 开局的奔跑、UFO 抓取、终局的编队和团聚均由时间轴写入表现 Transform/动画；不向 Chaos、移动组件、CellTopo 或任务图回写位移与速度。
- 终局实际飞行继续使用 M11 冻结后的权威轨迹点列；镜头和鸟的表现偏移只读取该点列，不能改变 `Yaw × Pitch × Power`、重力求解、命中或失败结果。
- 三颗助推行星与 UFO 仍只由 LaunchSite 的局部布局预设生成，不建立独立地图，也不保存随机世界的绝对 Transform。

## 2. 可编排坐标契约

### 2.1 开局：`OpeningFrame`

世界生成完成、M4 Party 初始化完成后，以初始主控鸟的出生点建立一次性的开局局部坐标系：

```text
Origin  = 初始主控鸟接地位置
Up      = normalize(Origin - PlanetCenter)
Forward = 出生点可见主路的切向前进方向；不可用时采用 Party 初始朝向投影到切平面
Right   = cross(Up, Forward)
```

所有开局位置使用 `(Forward, Right, Up)` 的厘米偏移。`Up` 偏移均以角色碰撞球支撑点为基准，模型仍由既有 Bird Visual 仅承担动画表演。若出生点附近不能容纳半径 `420 cm` 的平整舞台，演出管理器在同一出生 Cell 的已验证平整区域内平移 `Origin`；失败则跳过转圈段，直接执行“UFO 抵达—抓取—交还控制”的短版，不阻塞进入游戏。

### 2.2 终局：复用 `FABTSM110FinaleLocalFrame`

终局严格使用 `AABTSM3Planet::GetFinaleLaunchFrame()`：

```text
Origin = 两个 Finale Space Slot 的中点
X      = LaunchForward
Y      = 从 LeftSlot 指向 RightSlot 的 Right
Z      = 主行星径向 Up
```

导演稿中所有终局地表位置均为 `(X, Y, Z)` 本地厘米坐标；深空物体位置继续由 M11 局部布局预设和权威轨迹给出。相机的 `LookAt` 使用同一局部坐标或轨迹采样点，绝不从静态网格 Bounds 推导。

## 3. 角色、资产与表现职责

| 代号 | 现有来源 | 演出职责 | 默认动画 |
| --- | --- | --- | --- |
| 红 / 绯翼 | M4 Party 的 Red | 开局主控、受惊领队、终局左侧攻击编队 | `IdleA`、`Move`、`Fly` |
| 蓝 / 青翎 | M4 Party 的 Blue | 开局活跃外圈、终局侦察感的右内侧编队 | `IdleA`、`Move`、`Fly` |
| 黄 / 棱喙 | M4 Party 的 Yellow | 开局追逐白鸟、终局右侧高速编队 | `IdleA`、`Move`、`Fly` |
| 黑 / 玄爪 | M4 Party 的 Black | 开局慢半拍、终局左内侧重击编队 | `IdleA`、`Move`、`Fly` |
| 白鸟 / 被掳者 | `/Game/CuteBird/Blueprints/BP_Cute_Bird_0` | 开局被抓、终局从 UFO 逃出、团聚 | `IdleA`、`Move`、`Fly` |
| UFO | 待接入表现资产 | 抓取、终局命中体、可破碎外壳 | 悬停 / 抓取 / 受击 / 破碎状态 |

白鸟实例必须关闭 Gameplay 碰撞、导航、物理模拟和 Party 注册。UFO 必须提供如下表现 Socket/组件名；若美术命名不同，由数据资产映射，而非硬编码模型骨骼名：

```text
WhiteBirdPrisonSocket   // 开局抓取与终局囚笼中的白鸟
WhiteBirdReleaseSocket  // UFO 破碎后白鸟的第一帧释放点
CaptureBeamOrigin       // 抓取光束起点
ImpactCore              // M11 UFO 权威命中球的视觉中心
```

## 4. 开局演出：约 42 秒

### 4.1 时间轴与画面

| 时间 | 镜头 | 角色位置与表演 | 事件 / 切换条件 |
| --- | --- | --- | --- |
| 0.0–4.0 s | `OC_01 建立镜头`：相机 `(-820, -980, +620)`，看向 `(0,0,+45)`，FOV 50°。从矮树、石头之间缓慢推近。 | 五鸟位于半径 `300 cm` 的圆环：红 `(0,-300,0)`、蓝 `(285,-93,0)`、黄 `(176,+243,0)`、黑 `(-176,+243,0)`、白 `(-285,-93,0)`；都朝顺时针切线。 | HUD、输入与 Party 跟随关闭；角色使用演出代理移动，四鸟原 Gameplay 体冻结在相同支撑点。 |
| 4.0–12.0 s | `OC_02 环绕玩耍`：相机半径 `920 cm`，仰角 30°，以圆心为 LookAt，沿反方向缓慢环绕 35°。 | 五鸟以 `r=300 cm`、`1.05 rad/s` 顺时针跑两圈；白鸟始终比红鸟快 8%，形成会被追赶的视觉节奏。循环 `Move`。 | 画面优先保证全体五鸟至少 1.5 s 同框。 |
| 12.0–16.0 s | `OC_03 白鸟特写`：相机 `(+260,-510,+220)`，看向白鸟当前位置 `+ Up*45`，FOV 42°。 | 白鸟减速、朝镜头一侧回头，播 `IdleA`；其他鸟从画面后方跑过。 | 远处先出现一帧掠过的 UFO 阴影和低频音。 |
| 16.0–21.0 s | `OC_04 威胁揭示`：相机从白鸟肩后抬至 `(-180,-760,+560)`，看向 UFO 与白鸟之间的中点，FOV 48°。 | UFO 从 `(+1100,+350,+900)` 沿三次缓入曲线抵达白鸟正上方 `(+0,+0,+540)`；其水平投影先落在圆环外缘。其他四鸟停止、转向白鸟。 | UFO 以 `CaptureBeamOrigin` 投射光束，不使用真实牵引物理。 |
| 21.0–27.0 s | `OC_05 抓取`：低机位相机 `(-480,-660,+150)`，看向白鸟至 UFO 的上升线，FOV 55°。 | 白鸟从圆环位置以 `EaseInOut` 升至 `WhiteBirdPrisonSocket`，全程播 `Fly`；四鸟各自向白鸟原位置跑 `120–220 cm` 后急停，红鸟位于前景。 | 白鸟到达 Socket 时隐藏光束、切换为 UFO 附着；不产生任何碰撞或可拾取物。 |
| 27.0–35.0 s | `OC_06 追视离场`：相机升到 `(−1200,−800,+880)`，沿 UFO 离开方向摇摄。 | UFO 沿 `OpeningFrame.Forward` 飞离，先穿过 `(+900,+0,+1300)` 再淡出至 `(+5000,+0,+5000)`；白鸟可在囚笼内短暂可见。四鸟跑两步后停止。 | 远处飞行方向是叙事上的“终局方位”，但不要求它等于终局的实际空间坐标。 |
| 35.0–42.0 s | `OC_07 交还控制`：相机落回 M4 Party Camera 的期望 Orbit 状态；先取 `(−720,−850,+520)` 看向红鸟，再在 1.0 s 内 Blend。 | 红、蓝、黄、黑在白鸟离场点附近形成菱形：红前、蓝左、黄右、黑后，均面向主路。红 `IdleA`，其余短暂 `IdleA` 后恢复 M4 跟随。 | 恢复 HUD、玩家输入、Party 跟随、Chaos；只在所有恢复完成后显示首个 Gameplay 目标。 |

### 4.2 开局转圈的确定性公式

为便于以后直接写入 Sequencer 或 C++ 轨道，圆环位置由下式生成，而非五组独立手工关键帧：

```text
PhaseBird(t) = Phase0Bird + AngularSpeedBird * t
PositionBird(t) = Origin + r * (cos(PhaseBird) * Forward + sin(PhaseBird) * Right)
FacingBird(t)   = normalize(-sin(PhaseBird) * Forward + cos(PhaseBird) * Right)
```

`Phase0` 依次为 `-90°、-18°、54°、126°、198°`，鸟间最小间距不小于 `180 cm`。地表有轻微起伏时，只替换每个切向位置的 `SurfacePosition/SurfaceNormal`；圆环相位、镜头节奏和白鸟被抓的时刻不改变。

## 5. 终局演出：约 78 秒

终局从玩家成功发射、M11 返回 `TargetHit` 的同一权威事件开始。前三颗行星的近掠仍是可玩的实际飞行；下表只规定成功路径上的镜头接管、表现编队和收束。失败路径沿用 M11 的“第一不可恢复失败 → 黑屏快照恢复”，不播放救援段。

### 5.1 地表起飞与三重助推

| 时间 / 状态 | 镜头 | 四鸟与环境编排 | 事件 |
| --- | --- | --- | --- |
| `FinaleAim → Launch`，0.0–3.5 s | `FC_01 发射台总览`：相机 `(-1100,-980,+760)` 看向 `(0,0,+80)`，FOV 46°。镜头沿 `+X` 缓推。 | 四鸟按袋体横向编队进入太空弹弓袋前的展示位置：红 `(-90,-75,+25)`、黑 `(-90,-25,+25)`、蓝 `(-90,+25,+25)`、黄 `(-90,+75,+25)`。两侧 Space Slot 与钢铁弹弓完整入画。 | 玩家完成发射确认后锁输入；镜头不更改瞄准结果。 |
| `Launched`，3.5–8.0 s | `FC_02 袋体近景`：相机 `(−280,−420,+260)` 看向 `(0,0,+45)`，FOV 55°；释放瞬间后拉。 | 四鸟在表现上合入同一个袋体中心；真实深空段切换到固定编队偏移。鸟均播 `Fly`，不再模拟 Chaos。 | 释放点与 M11 轨迹 `t=0` 完全重合。 |
| `Assist1` | `FC_03 行星① 掠过`：镜头位于权威轨迹点后方 `-Tangent*900 + Up*380`，LookAt 为当前队形中心前方 `Tangent*500`，FOV 58°。 | 队形横向偏移为红 `Y=-66`、黑 `Y=-22`、蓝 `Y=+22`、黄 `Y=+66 cm`，沿由发射袋坐标系平行运输的稳定横轴排列。行星①从画面右侧掠过。 | 助推事件时只做 0.22 s 视觉慢镜、尾迹增强和镜头轻微推近；固定步长求解器时间不变。 |
| `Assist2` | `FC_04 行星② 侧追`：镜头由上一镜头 Match Cut 到队形侧前方 `+Right*980 + Up*260`，看向行星②最近掠过点，FOV 52°。 | 四鸟保持同一队形，仅将整体 Roll 逐步对齐轨迹切线，禁止 Frenet 翻转。 | 用行星②颜色的边缘光和第二段更长尾迹读出增能。 |
| `Assist3` | `FC_05 行星③ 全景`：先 `0.7 s` 广角，相机位于轨迹上方 `+Up*1800`，同时纳入行星③、队形和远端 UFO；随后切回后追。 | 四鸟队形在大远景中占屏幕高度约 8–12%，防止被误读为单一弹丸。 | `Assist3` 成功后停止慢镜，音乐推入撞击节拍。 |

### 5.2 UFO 撞击、救援与团聚

| 时间 / 状态 | 镜头 | 角色位置与表演 | 事件 |
| --- | --- | --- | --- |
| `FinalApproach`，约 0–3 s | `FC_06 目标对切`：先从 UFO `ImpactCore` 的反向看四鸟来向，镜头位于 `ImpactCore + IncomingDirection*1500 + Up*250`，FOV 60°；最后 0.3 s 切为侧面全景。 | 四鸟保持横向队形，收拢到 `±42/±14 cm`，使命中读作集体冲击。白鸟在 `WhiteBirdPrisonSocket` 内清晰可见。 | UFO 的权威命中球仍是唯一命中判定；视觉外壳可比命中球大。 |
| `TargetHit`，0–1.2 s | `FC_07 撞击定格`：相机在撞击平面侧方，距离 `1300 cm`，FOV 62°。 | 撞击帧四鸟进入 `ImpactCore` 前 `80 cm`，随后按预设表现轨迹分离；播 `Attack` 或首版以 `Fly` + squash/stretch 替代。 | 触发 UFO 破碎、闪光、碎片；不把碎片纳入 M11 重力或碰撞。 |
| `Rescue`，1.2–5.5 s | `FC_08 白鸟释放`：相机从破碎缺口推向 `WhiteBirdReleaseSocket`，FOV 45°。 | 白鸟从释放 Socket 沿镜头侧前方飞出 `650 cm` 后减速，播 `Fly`；四鸟在其后方扇形减速：红左前、黄右前、蓝左后、黑右后。 | 白鸟与任何碎片无碰撞。UFO 残骸在 2.5 s 内淡出或进入纯装饰漂浮。 |
| `Complete`，5.5–14.0 s | `FC_09 五鸟团聚`：相机先退至五鸟中心 `(-1050,-1250,+800)` 的等效深空观察位，FOV 50°，再缓慢 25° 环绕。 | 以白鸟为圆心，五鸟半径 `260 cm`、`0.55 rad/s` 绕圈：白、红、蓝、黄、黑按顺时针等分；朝圆心略偏前，均播 `Fly`。背景是主行星与远处行星，不再显示 UFO。 | 播放“救回”确认；四鸟仍不回写到 Party 的地表位置。 |
| `Complete`，14.0–18.0 s | `FC_10 结尾`：相机逐渐拉远，五鸟缩至屏幕高度 12%，FOV 42°；最后淡出。 | 五鸟继续同相位绕行；白鸟完成一次更小半径的向内/向外摆动，作为它终于自由的区别。 | 显示结束 UI。若首版没有结局菜单，停留 3 s 后回到主菜单，不回到可操控地表。 |

## 6. 镜头实现规则

1. Gameplay 段继续由 `AABTSM4PartyCamera` 保持玩家拥有的持久 Orbit 状态。演出开始时保存其 Orbit 参数、ViewTarget、HUD/input 状态；演出结束后以 `0.8–1.0 s` Blend 恢复，而不是重新按角色 Forward 构造相机。
2. 演出镜头使用独立 `CinematicCameraActor` / Camera Rig 或等价的 C++ 相机轨道。所有相机 Transform 由 `OpeningFrame`、`FinaleLocalFrame`、轨迹采样点和指定的 LookAt 计算；禁止在关卡里手摆固定世界坐标。
3. 地表镜头的屏幕 Up 必须是当地 `RadialUp` 在视线平面上的投影，沿球面移动时遵守 M4 的并行运输规则；深空镜头以权威轨迹切线和稳定队形 Up 构造，不使用欧拉角线性插值。
4. 镜头遮挡只对地形、建筑和必要 WorldStatic 生效；忽略五鸟、白鸟、UFO 光束、纯表现碎片和装饰 HISM。因遮挡缩短距离时，优先保留白鸟 / UFO / 四鸟的叙事关系，而非死守原始焦距。
5. 任何慢镜仅影响相机、粒子、动画和音频播放率；权威的 M11 固定步长积分和事件时刻不受影响。

## 7. 自动演出状态机与交接

```text
WorldReady
  -> OpeningSetup -> OpeningPlay -> WhiteBirdCaptured -> OpeningHandoff -> Gameplay

Gameplay + M11 TargetHit
  -> FinaleLaunch -> Assist1 -> Assist2 -> Assist3 -> FinalApproach
  -> UFOImpact -> WhiteBirdRescue -> FiveBirdOrbit -> Complete

M11 Failed
  -> 现有失败可读反馈 -> SnapshotReset -> FinaleAim
```

| 交接点 | 必须冻结 / 隐藏 | 必须恢复 / 继续 | 验证信号 |
| --- | --- | --- | --- |
| `OpeningSetup` | 四鸟移动输入、Party 跟随、HUD 交互 | 地表渲染、白鸟表现 Actor | 五鸟都能在合法地表支撑点出现。 |
| `OpeningHandoff` | UFO、白鸟抓取光束、开局代理轨道 | 四鸟 Chaos、Party 跟随、M4 Party Camera、HUD、输入 | 当前主控仍为红鸟，四鸟没有位置跳变或额外冲量。 |
| `FinaleLaunch` | Party 跟随、角色 Chaos、常规 Orbit 输入 | M11 权威飞行、终局相机、终局 HUD | 轨迹 Hash 与发射前预演一致。 |
| `UFOImpact` | UFO 命中前完整外壳、终局瞄准输入 | 破碎表现、白鸟表现 Actor | 命中只触发一次，白鸟只释放一次。 |
| `Complete` | 终局飞行控制、残骸交互 | 结束 UI / 主菜单流程 | 不生成第二个 Party 成员或可控制白鸟。 |

## 8. 首版数据资产建议

首版不要求立刻制作 Level Sequence。建立一个可版本化的 `UDataAsset`（建议名 `ABTSCinematicDirectionPreset`）即可承载下列数据，并让 C++ 演出管理器解释它：

- `OpeningFrame` 的圆环半径、角速度、五鸟初相位、UFO 入口/离场关键点、各镜头本地偏移/FOV/时长；
- `FinaleFrame` 的地表起飞镜头偏移、四鸟袋体和深空编队偏移、每个 M11 事件对应镜头策略；
- 白鸟 Blueprint Class、UFO Class、Socket 名映射、动画序列覆盖；
- 每段的输入/HUD/Party/Chaos 控制策略，以及“找不到平整开局区域时的短版开场”开关；
- 导演版本号。存档或回放只记录版本号和随机世界/终局布局版本，不保存任何绝对世界 Transform。

## 9. 首版验收清单

- [ ] 任意可验收 Seed 中，开局五鸟都在出生点附近、脚底贴地；白鸟不是 Party 成员，四鸟仍可正常切换和跟随。
- [ ] 开局 UFO 抓取只改变白鸟表现位置；抓取前后四鸟的 Chaos 速度、半径和碰撞不被演出修改。
- [ ] 开局结束恢复到保存的 M4 Orbit 状态，玩家不会突然被相机绕到角色背后；HUD 与输入只恢复一次。
- [ ] 终局所有地表镜头、Space Slot、三颗行星和 UFO 都能随 `FinaleLocalFrame` 移动；无任何依赖绝对世界坐标的关键帧。
- [ ] 成功轨迹的四鸟始终按 M11 权威点列飞行，助推与 UFO 命中事件在 30/60/120 FPS 下顺序一致。
- [ ] 失败发射不会播放救援或结局；快照恢复后 Party、库存、Space 弹弓和世界生成结果保持不变。
- [ ] UFO 破碎后白鸟恰好释放一次，结尾画面中正好五鸟，没有残留可碰撞碎片或新增可控制 Pawn。

## 10. 当前缺口与后续实现顺序

当前工程已有四鸟 Party、玩家 Orbit Camera、终局局部坐标帧、Space Slot Pair 和 M11 数据侧重力/事件基础；尚缺白鸟表现生命周期、UFO 表现/破碎 Actor、演出相机管理器、演出状态机及终局实体布局。因此建议实施顺序为：

1. 先建立 `OpeningFrame`、`FinaleLocalFrame` 驱动的演出数据与纯表现 Actor 约束；
2. 实现开局短版，再扩展为五鸟转圈和镜头组；
3. 接入 M11 的 `Launched / Assist1–3 / TargetHit` 权威事件，只做镜头和表现队形；
4. 最后接入 UFO 破碎、白鸟释放和五鸟团聚，并完成 PIE / Standalone 的完整交接验证。

