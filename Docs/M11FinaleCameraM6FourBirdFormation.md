# M11 M6 四鸟终局编队设计与实施合同

> 状态：M11-owned implementation baseline（2026-08-11）。本稿只定义和实现
> M11 工作树拥有的终局表现逻辑；Space 弹弓共享视觉、Party/Character 公共动画
> API、共享资产与生产地图仍由 Integration 唯一写入。

## 1. 目标与非目标

M6 把现有“单只主控鸟消费一条冻结 `FABTSM11PlaybackPlan`”扩展为四鸟终局
表现，同时保持以下权威不变：

- Released Trajectory、Playback Plan、Plan Hash、Rank、F4 分类和 UFO 800 cm
  接触语义全部仍只由主控鸟与原计划决定；
- 其余三鸟只属于表现层，不运行第二套积分器，不参与行星/UFO 命中；
- 镜头的 Lucy 式屏幕锚、左到右穿越和 UFO 终端叙事仍由主控鸟决定；
- 四鸟不得通过 Visual Scale 补偿远景可读性；
- 首版继续只渲染一条主控鸟编队尾迹，避免四套同轨迹粒子叠亮。

M6 不在 M11 工作树修改普通 M6 弹弓、共享 Party/Character、Space 袋 Mesh、
弦连接点、桩间距、材质、Niagara、Blueprint 或地图。

## 2. 冻结编队合同

### 2.1 成员顺序

进入终局时一次性冻结：

1. Slot 0：当前 `ControlledBird`，也是唯一轨迹/镜头/接触权威；
2. Slot 1–3：其余成员按 `BirdId` 升序。

系统要求成员数恰好为 4、Actor 全部有效、BirdId 唯一。任一条件不满足即
fail closed；飞行期间不重排，也不响应主控切换。

### 2.2 袋内布局

袋内使用紧凑 2×2 逻辑槽位。局部 Z 沿发射方向，X/Y 位于袋平面；默认中心
间距为 110×100 cm，四个槽位共用现有 `BirdInPouchOffsetCM=20`，并增加
Space 专用 `M6PouchForwardClearanceCM=25`，使四鸟中心沿发射轴距袋中心
`45 cm`，避免放大后的 Steel 袋与鸟体穿模。每帧瞄准只
根据当前袋 Transform 重算槽位，不对 Character 建父子 Attach，因此不会污染
根组件、Chaos 或恢复层级。

袋 Transform 只负责槽位位置；四只鸟的 Actor 旋转统一由当前发射方向与终局
局部上方向重建，使 Actor `+X` 朝前。不得继续复制袋体“局部 +Z 朝发射方向”的
旋转，也不得保留鸟进入终局前的随机行走朝向。终局四鸟还必须在每次 Actor
挂袋重定位后恢复各自 CDO 冻结的 `BirdVisual` 相对位置和导入轴修正：Chaos
表现 Tick 可能先以进入终局前的物理朝向写 Mesh 世界旋转；只统一 Actor 而不恢复
组件相对帧，会让四只鸟继续显示不同的旧朝向。普通单鸟 M6 不经过这条四鸟
Chaos 编队更新链，因此无需增加该恢复步骤。

这组数值是 M11 代码验收包络，不代表共享 Space 袋资产已经定型。Integration
应按“最大鸟视觉直径 + 槽位中心距 + 弦/轮廓余量”重新定型 Space Tier。

### 2.3 飞行单列

释放时对不可变 Playback Plan 的 Hermite 曲线做每原始段 4 次致密采样，建立
`time ↔ world-trajectory arc length` 查找表。冻结间距：

```text
D = max(260 cm, 2 × MaxCollisionRadius + 80 cm)
S0 = current authority arc
S1 = max(0, S0 - D)
S2 = max(0, S0 - 2D)
S3 = max(0, S0 - 3D)
```

后三鸟按弧长采样同一条曲线，因而近掠弯道也不会用 `Primary - Forward*D`
切入未经验证的行星区域。每只跟随鸟在其历史弧长可用前保持袋内槽位，并在
默认 `0.5D` 距离窗内用零端斜率 SmoothStep 进入轨迹；这会形成按顺序出袋，
而不是四鸟在路径起点重叠。

## 3. 生命周期与恢复事务

进入终局时保存四只鸟各自的 Actor Transform、BirdVisual Relative Scale、引用
和槽位状态，再逐只调用现有 `EnterSlingshotPouch`。该 API 关闭运动和碰撞；
M11 之后只写表现 Transform。

失败、取消、退出或重置时逐只执行：

1. 恢复原 Visual Scale；
2. `BeginSlingshotReturn()` 保持碰撞关闭；
3. Teleport 到该鸟自己的进入前 Transform；
4. `FinishSlingshotReturn()` 恢复原运动/碰撞；
5. 最后恢复 Party 跟随、袋 Rest 并清空 M6 路径/间距状态。

Reset-but-stay-in-finale 会先完整恢复四鸟，再将同一冻结顺序重新送入当前袋槽。

## 4. 镜头安全合同

导演先照旧求解单主控鸟 Transform/FOV。四鸟安全层随后逐球检查固定 16:9
安全框：水平 NDC 0.88、垂直 NDC 0.84。若任一成员越界，只允许相机沿“相机
到主控鸟”的射线后退，二分求最小退距（上限 30000 cm）。这种移动保持主控鸟
NDC 锚不变，不改变镜头朝向、不改 FOV、不转向四鸟质心，也不缩放鸟体。
超过最大退距仍无法容纳则 fail closed。

## 5. 离线观测与判据

Capture Contract 升为 16，CSV Schema 升为 8。Schema 8 在 Schema 7 后追加：

- 四个固定 Slot 的 BirdId、Actor、World、Screen、Depth、PixelRadius、VisibleRatio；
- 三个相邻弧长间距、期望间距；
- 顺序稳定、主控锚正确、编队完全展开标记。

Manifest 与 Python 离线分析共同统计：四鸟丢失帧、顺序/主控错配帧、完全展开
帧、间距不足次数和最小相邻间距。Schema 1–7 保持可读；Schema 8 缺任一 M6
列或数值非法会 fail closed。

## 6. 阶段里程碑

| 阶段 | 内容 | 状态 / 所有者 |
|---|---|---|
| M6-0 | 顺序、2×2 槽位、弧长间距、恢复合同冻结 | 已落实 / M11 |
| M6-1 | 四鸟逻辑装袋与瞄准跟随 | 已落实 / M11 |
| M6-2 | 同一 Playback Plan 的单列弧长播放 | 已落实 / M11 |
| M6-3 | 四鸟事务恢复；首版单编队尾迹 | 已落实 / M11 |
| M6-4 | 主控锚不变的四球镜头安全框 | 已落实 / M11 |
| M6-5 | Schema 8、Manifest 与 Python 离线判据 | 已落实并完成 fresh 双路线证据 / M11 |
| M6-6 | Space 袋、弦、桩、点击体积与轮廓资产定型 | `IntegrationAccepted`（2026-08-13） |
| M6-7 | 合并后生产 PIE/Standalone 联合验收 | `IntegrationAccepted`（2026-08-13） |

## 7. Integration 唯一写入交接

Integration 必须处理：

1. 只放大 Space Tier 的袋、桩距、两端弦连接偏移、弦长/粗细和点击碰撞；普通
   Twig/Simple/Reinforced 不变；
2. 如需强制 Fly 表现，新增 Party/Character 的“presentation lease / force-flight
   snapshot”公共 API。现有公开动作只有 Impact/Damage；禁止用
   `LaunchFromSlingshot` 假装动画，因为它会重新开启物理和碰撞；
3. 共享 Mesh/材质/纹理/动画/Niagara/Stencil/声音资产及生产地图绑定；
4. 合并后的完整 Editor、生产 PIE、Standalone 与普通 M6 三档弹弓回归。

### 7.1 M6-6 Integration 验收结果

共享视觉合同 v2 已只对 Space 档装配四鸟框架：桩距 `320 cm`、桩高
`285 cm`、桩径 `36 cm`、弦粗 `5.5 cm`、待机袋下移 `45 cm`，袋端连接点
为局部 `Y=±52 cm`。Space 袋原生目标 Bounds 为 `(84,120,24) cm`，严格是普通
三档 `(42,60,12) cm` 的 2 倍；三档普通弹弓参数和资产不变。既有 M11.0 地图中
恰好为 `210 cm` 的旧默认桩距会兼容迁移至 `320 cm`，非旧默认的编辑器调参不会
被覆盖。

Space 桩、弦和袋分别消费既有 Steel Mesh/Material；发射模式继续复用 M5.1 弦
Actor 的同一 `PouchVisual`，没有第二个延迟加载代理。该可见袋本身保持
`QueryOnly + Visibility Block`，两段弦保持 `NoCollision`，所以放大后的几何同时
就是放大后的点击体积。共享风格化适配器仍以 `Slingshot` stencil 和 Steel 资产
映射提供轮廓。用户已于 2026-08-13 在生产 PIE 中确认四鸟容纳、统一朝前、
`25 cm` 附加前向净空、弦端连接、点击、材质和轮廓，M6-6 因此关闭为
`IntegrationAccepted`。fresh Standalone 与普通三档联合回归仍单独属于 M6-7，
不得用本次可见验收替代。

2026-08-12 集成候选已使用唯一批准的 UE 5.8 完成
`-ForceUnity -DisableAdaptiveUnity` Development Editor 链接；fresh NullRHI 精确
通过 `ABTS.M6.SlingshotVisual.SpaceFourBirdFrame` 1/1、
`ABTS.M51.SlingshotAssembly` 2/2、`ABTS.M110.TaskGraphFinaleSeparation` 1/1、
`ABTS.M3.Monthly.FinaleAnchor` 3/3、
`ABTS.Rendering.Toon.T3A2.SharedMaterialAdapter` 1/1 和
`ABTS.Integration.PreviewFinaleFrame` 2/2。以上只证明原生尺寸、旧默认迁移、
装配、Steel 资产、可见袋点击体积和共享材质合同，不替代待执行的生产 PIE。

### 7.2 M6-7 Integration 联合验收

2026-08-13，基于已通过生产 PIE 的 M6-6 提交，Integration 使用唯一批准的
UE 5.8 完成 `-ForceUnity -DisableAdaptiveUnity` Development Editor 全链接；
fresh NullRHI 精确通过：

- `ABTS.M6.SlingshotVisual.SpaceFourBirdFrame`：1/1；
- `ABTS.M51.SlingshotAssembly`：2/2；
- `ABTS.M11C.V2_1.InputParityAndLatestOnly`：1/1；
- `ABTS.Rendering.Toon.T3A2.SharedMaterialAdapter`：1/1。

普通三档随后在隔离 M6/M9 Standalone 中同时生成
`Twig=1 Simple=1 Reinforced=1`，唯一运行终态为
`Terminal=1 Passed=1 Failed=0`，且完整原因包含
`Slingshots=3 Targets=7 Envelopes=3 Sweep=1 SimpleHits=0 OutsidePullHits=0 Gravity=1 ScoutMap=1 Buildings=0`。

生产 `L_ABTS_M11` 再以 fresh D3D12、RenderOffscreen、60 FPS 运行：M5.1 报告
`OrdinaryConfigured=1 FinaleConfigured=1`，M11 Finale Frame 为
`Authority=Production CandidateRank=0`，三栋建筑均 `IdleValidation Accepted=1`，
最终 `WorldReady=1 / BuildingAccepted=3 / BuildingRejected=0 / Expected=3 /
Registered=3`，且零 `LogABTSRuntime: Error`。用户此前已在同一候选完成生产 PIE
的 Space 四鸟容纳、朝向、净空、材质、轮廓和普通弹弓回归验收，因此 M6-7 正式
关闭为 `IntegrationAccepted`。

一次附加 NullRHI 全世界诊断曾因 B0 建筑 IdleValidation 超时触发既有 M7
fail-closed；该结果被保留且没有当成成功。相同提交在正式要求的 D3D12 60 FPS
实时路径下三栋建筑全部接受，证明该诊断不属于 M6 回归，也不能用来放宽 M7 门禁。

## 8. 验收门

M11 侧完成条件：Development Editor 与 ForceUnity；fresh NullRHI M6 弧长测试及
既有 M11 相机/交互测试；Rank11 Stylized1 两条不同 F4 可达自定义路线录制；两份
Schema 8 报告都必须四鸟顺序稳定、主控一致、完全展开、间距合格且四鸟无丢失。

资产定型前，旧 Space 袋中四鸟可能视觉拥挤；这不应被误报为 M11 代码验收通过，
也不授权 M11 修改共享资产。

## 9. 2026-08-11 M11-owned 实施证据

- UE 5.8 Development Editor 与 `-ForceUnity -DisableAdaptiveUnity` 完整链接成功；
- fresh NullRHI `ABTS.M11C.Unit` 精确通过 10/10，其中
  `ABTS.M11C.Unit.M6FormationArcLength` 1/1；Capture Config 1/1；Python
  Schema 1–8 回归 7/7；
- Rank11 Stylized1 自定义 F4 A：`M6FormationF4A-R2-20260811`，Released
  `0x668901FF090C69FE`、Plan `0x525966177CEBF5FF`、1014 帧、最终接触
  `800.0000000000989 cm`、AVI SHA-256
  `3EBFDD103AA309BE422B97294CCEB79EC8379DBCC32984EB228D51E182E09556`；
- Rank11 Stylized1 自定义 F4 B：`M6FormationF4B-Final-20260811`，Released
  `0xCC31B4566FD9728F`、Plan `0xAD16A25431EE8C3E`、995 帧、最终接触
  `800.0000000000393 cm`、AVI SHA-256
  `AB5F5DF59C4207AC2A7609CEB0DF669E1923B619395170E76AE3EE61A9438A21`；
- 两份 Manifest/Schema 8 均为 `m6FormationPassed=true`、四鸟丢失/顺序错配/
  主控错配/间距不足全为 0，完全展开帧 1010/991，最小相邻弧长均为 260 cm；
- 双星超远景四鸟最小像素半径约 0.284 px，但四个投影圆可见率全程为 1.0。
  这按“安全框不裁切且不缩放鸟体”记通过；像素半径保留诊断，不强行破坏双星构图；
- 抽检发射初段、火星前景与终端帧确认四只鸟均进入像素通道，主控在队首，后三鸟
  沿同一路径排成单列；首版尾迹仍只有主控一条。

这些证据只关闭 M11-owned M6-0～M6-5，不关闭 Integration 的 M6-6/M6-7。

## 10. 飞行姿态合同

四鸟进入飞行表现后，Actor `+X` 必须逐帧对齐各自在冻结 Playback Plan 上采样的
世界速度。Actor `+Z` 对齐“本帧导演相机 Up 在速度法平面上的投影”，从而在满足
速度前向的同时，让鸟体上方稳定朝向当前屏幕上方。导演必须先完成本帧相机求解，
再写入鸟体旋转；不得继续使用 Finale Local Frame Up，也不得读取 Movement Component
或 Actor 位移差分作为姿态权威。

`BirdVisualRelativeRotation=(0,-90,0)` 是导入模型的既有局部轴修正：可见模型原始
`+Y` 经该修正映射到 Actor `+X`。M11 从每个鸟类 CDO 冻结 authored Mesh 轴修正，
禁止把这项 `-90°` 再乘入 Actor 世界旋转造成双重修正。Interaction System Tick
显式依赖四只鸟 Tick；导演完成本帧相机求解后，最终写入
`FlightActorRotation × AuthoredVisualAxisCorrection`。因此即使 Chaos 鸟自身先通过
`UpdateChaosVisualFrame` 重写 Mesh 世界姿态，最终结果也不依赖未声明的 Tick 顺序。
相机 Up 与速度近乎平行时，依次使用上一帧 Actor Up 和相机 Right 建立退化回退；
任何非有限或零速度输入均 fail closed。

2026-08-11 验证：Development Editor 完整链接成功；fresh NullRHI
`ABTS.M11C.Unit` 11/11，通过新增 `M6FormationViewRotation` 的速度前向、视角 Up、
默认 `Yaw=-90°` 可见模型轴与奇异角回退断言。Rank11 Stylized1 自定义 F4
`M6ViewRotationR4-20260811` 为 1014 帧 `Complete/TargetHit`，Released/Plan Hash 仍为
`0x668901FF090C69FE / 0x525966177CEBF5FF`，M6 丢失/顺序/间距失败均为 0，最终
UFO 距离 `800.0000000000989 cm`；AVI SHA-256
`C31C1E03F962ADF42F073C1793344FCBF86632539F93EF1B6B4876F29E93F9A9`。抽检
319/557/812 帧确认四鸟可见模型的前方和上方在三次行星近景中保持一致。
