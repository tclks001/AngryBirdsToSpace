# ABTS 事件驱动引导系统设计

> 状态：P0 框架与地表纵向切片实现中。
>
> 所有权：跨阶段事件目录、统一调度、共享 HUD 表现和本文均由原始集成工作树维护。M3、M7、M11 只在各自所有文件中发布稳定语义事件，不保存引导步骤、不决定文案、不直接显示气泡。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [项目工作流](ABTSProjectWorkflow.md) · [共享 UI Theme](ABTSSharedUIThemeDesign.md) · [多工作树规范](ABTSMultiWorktreeDevelopmentGuide.md)

## 1. 目标与边界

引导系统解决四件事：

1. 用同一模板显示鸟头气泡、世界物体气泡和屏幕操作卡；
2. 由真实 Gameplay 事件推进，不用关卡时间轴或硬编码绝对坐标猜测玩家进度；
3. 同一事件可根据已知事实、主体、次数和优先级选择不同引导；
4. 子工作树只发布一个稳定事件，具体步骤、文案、排队、去重和表现均由集成工作树统一管理。

P0 不实现存档、完整本地化资产、正式纹理插画、语音、遥测后台、UMG 动画或 M7/M11 尚未冻结的生产玩法。当前 Canvas 表现使用统一槽位和代码绘制简笔图示验证结构、输入说明和世界锚点；正式图画可在 P1 通过软引用纹理替换，不改变事件合同。

## 2. 不变量

- 事件描述已经发生的事实，不描述“显示哪条教程”。禁止事件名包含具体 `GuideId`。
- 完成条件必须来自玩法提交后的真实结果；点击、按键或预览本身不能冒充安装、破坏、回收或命中成功。
- 引导系统只读消费事件，不修改库存、鸟种、CellTopo、弹弓状态、建筑物理、轨迹、Hash 或终局认证身份。
- PCG 锚点只通过 Actor、CellId、PairId、稳定任务/目标 ID 或本次事件位置解析；禁止保存绝对世界坐标和 Actor 名称。
- 事件发布是旁路观察。引导子系统缺失、关闭或拒绝事件时，原 Gameplay 返回值和提交顺序保持不变。
- 同一时刻最多一个主要引导。高优先级拒绝提示可排到当前步骤之后，但 P0 不抢占正在显示且尚未完成的步骤。
- 已满足完成事实的步骤不得补弹；玩家跳步、调试库存或旧存档进入时应自动越过。
- 飞行、Chaos Settling、M11 深空播放和失败黑屏期间只允许相关阶段提示，普通地表气泡必须抑制。

## 3. 架构

```text
M3 / M7 / M11 专属系统 ─┐
M4 / M5.1 / M6 / M8 ───┼─ FABTSGuideEventBus::Publish(...)
M10 / 共享库存/UI ──────┘
                              |
                              v
                    UABTSGuideWorldSubsystem
                    ├─ 事件计数与最近载荷
                    ├─ P0 规则目录
                    ├─ 完成去重与单活动队列
                    └─ 只读 Presentation Snapshot
                              |
                              v
                    AABTSM4PartyHUD::DrawHUD
                    ├─ Actor/鸟头世界投影气泡
                    └─ 无锚点时顶部屏幕操作卡
```

### 3.1 发布接口

子系统只需一行：

```cpp
FABTSGuideEventBus::Publish(
    this,
    FABTSGuideEventIds::BuildingTargetReady,
    TargetStableId,
    TargetActor,
    /* PrimaryValue */ DifficultyTier,
    /* SecondaryValue */ 0);
```

载荷字段的冻结语义：

| 字段 | 语义 |
| --- | --- |
| `EventId` | 稳定事件名，由公共目录定义 |
| `SubjectId` | 可选主体，如 `Branch`、`Twig`、`Blue` 或稳定目标 ID |
| `PrimaryValue` | 可选主整数，如数量、鸟枚举、前缀等级 |
| `SecondaryValue` | 可选次整数，如 PairId、难度或拒绝原因枚举 |
| `AnchorActor` | 可选弱语义锚点；调度器不得持有其强生命周期 |
| `WorldLocation` | 可选当帧位置；没有 Actor 时提供表现回退 |

事件名和通用载荷是稳定共享面；引导规则不是跨工作树契约。新增事件必须说明生产者、提交时点、载荷、重复语义和失败时是否发布。

### 3.2 规则模型

每条规则包含：

- `GuideId`：只在集成调度内使用；
- `RequiredFacts[]`：全部满足后才有资格显示；
- `CompletionFacts[]`：全部满足后完成，即使从未显示也跳过；
- `Priority`：候选排队顺序；
- `AnchorMode` 与 `AnchorEventId`：当前鸟、触发 Actor、事件位置或屏幕卡；
- `Title / Body / InputHint`：P0 C++ 回退文案；
- P1 的插画、冷却、升级提示和可重播策略。

事实键为 `EventId + SubjectId`，同时记录通用事件计数。因此：

```text
Slingshot.Assembled(Twig) + Party.ControlledBirdChanged(!Blue)
  -> Guide.SwitchToBlue

Slingshot.Assembled(Twig) + Party.ControlledBirdChanged(Blue)
  -> Guide.ClickPouch
```

同一事件不需要知道最终会触发哪条引导。

## 4. P0 事件目录

### 4.1 P0-A：本轮直接接入

| 事件 ID | 发布者 | 提交时点 | Subject / 数值 | 重复语义 |
| --- | --- | --- | --- | --- |
| `Guide.World.Ready` | M5.1 WorldSystem | 槽和拾取物全部生成成功后 | 无 | 每世界一次 |
| `Guide.Party.ControlledBirdChanged` | BirdParty | 初始四鸟 Ready；之后每次成功换鸟 | Subject=`Red/Blue/Yellow/Black`，Anchor=新主控鸟 | 初始一次，换鸟重复 |
| `Guide.Inventory.ItemAcquired` | InventoryComponent | `AddItem` 成功后 | Subject=ItemId，Primary=新增数量，Secondary=新总量 | 每次增加 |
| `Guide.Inventory.HeldItemChanged` | InventoryComponent | 成功设为手持或清空后 | Subject=ItemId；清空为 `None` | 每次变化 |
| `Guide.Slingshot.StakeInstalled` | M5.1 WorldSystem | Actor、槽占用和库存扣除均提交后 | Subject=Tier，Primary=CellId，Secondary=PairId，Anchor=桩 | 每根一次 |
| `Guide.Slingshot.CordEndpointSelected` | M5.1 WorldSystem | 第一根合法桩被记录后 | Subject=Tier，Anchor=桩 | 每次新首桩 |
| `Guide.Slingshot.Assembled` | M5.1 Assembly | Cord Actor、库存和两端 `HasCord` 原子提交后 | Subject=Tier，Secondary=PairId，Anchor=弦/袋 | 每套一次 |
| `Guide.Slingshot.EntryRejected` | M6 | 鸟种/档位资格拒绝后 | Subject=Tier，Primary=BirdId，Anchor=弦/袋 | 每次拒绝 |
| `Guide.Slingshot.Ready` | M6 | 鸟入袋且状态提交为 `Ready` 后 | Subject=Tier，Primary=BirdId，Anchor=弦/袋 | 每次进入 |
| `Guide.Slingshot.Pulling` | M6 | 状态成功进入 `Pulling` 后 | Subject=Tier，Anchor=弦/袋 | 每次进入 |
| `Guide.Slingshot.PowerChanged` | M6 | 合法滚轮输入实际改变 `PullAlpha` 后 | Subject=Tier，Primary=0..100 | 每次有效变化 |
| `Guide.Slingshot.Launched` | M6 | 状态和鸟初速度提交为 `Flying` 后 | Subject=Tier，Primary=BirdId，Secondary=SpeedCMPerSec | 每次发射 |
| `Guide.Slingshot.Completed` | M6 Settlement | 鸟已归队、输入/相机恢复且状态回到 `Inactive` 后 | Subject=Tier，Primary=BirdId，位置=最终落点 | 每次完整闭环 |
| `Guide.Scout.Revealed` | M10 ScoutMap | 新纹理、固定参考系和标记原子提交后 | Subject=`Blue`，位置=侦察中心 | 每次成功侦察 |
| `Guide.Building.MaterialRecovered` | M8 Recovery | M7 真正销毁模块且库存增加成功后 | Subject=ItemId，Primary=增加数量 | 每次回收 |
| `Guide.Bridge.Built` | M8 Bridge | 桥 Actor、Edge 状态、通路和库存扣除提交后 | Subject=`Flow/Barrier`，Primary/Secondary=CellA/B，Anchor=桥 | 每座一次 |

### 4.2 P0-B：ID 先冻结，等待专属工作树发布

| 事件 ID | 未来发布者 | 提交时点 | 本轮状态 |
| --- | --- | --- | --- |
| `Guide.Building.TargetReady` | M7 | 生产建筑完成登记、IdleValidation 接受且可攻击 | 公共 ID 已提供；M7 冻结后接入 |
| `Guide.Building.ImpactAccepted` | M7 | 鸟/爆炸对真实生产模块产生有效损伤或破坏 | 公共 ID已提供；M7 接入 |
| `Guide.Satellite.PracticeReady` | M3/M9 集成消费点 | 认证 SatelliteWindow、卫星和练习目标均 Ready | 公共 ID 已提供；等待生产入口稳定 |
| `Guide.Satellite.AssistPreviewed` | M10.1 | 当前同源预测首次形成有效卫星偏折证据 | 公共 ID 已提供；等待生产目标闭包 |
| `Guide.Finale.Ready` | M11 | Space 弹弓和认证终局交互通过 fail-closed 门 | 公共 ID 已提供；M11 接入 |
| `Guide.Finale.Aiming` | M11 | 终局进入 Aiming | 公共 ID 已提供；M11 接入 |
| `Guide.Finale.PrefixStable` | M11 | F1/F2/F3 稳定器真实进入 StableFn | Subject=`F1/F2/F3`，Primary=等级；M11 接入 |
| `Guide.Finale.Launched` | M11 | 冻结相同 revision 并提交 Playback Plan 后 | 公共 ID 已提供；M11 接入 |

M7/M11 发布这些事件前必须先合并包含公共目录的最新 `master`。不得在功能分支自行创建同名字符串副本或修改 P0 载荷语义。

## 5. P0 引导顺序

P0 首个纵向切片只覆盖：

```text
World Ready
  -> 收集树枝和植物纤维
  -> 安装两根 Twig 桩
  -> 依次选择两桩并完成 Twig 弦/袋
  -> 切换青翎
  -> 点击袋进入 Ready
  -> 拖动袋进入 Pulling
  -> 滚轮调整力度
  -> 松开发射
  -> 完整归队并揭示侦察图
```

建筑破坏、材料回收和桥梁事件本轮先接入总线并由自动化验证，但其正式引导规则要等 M7 冻结后的 `Building.TargetReady/ImpactAccepted` 一起启用，避免教程在生产建筑尚未 Ready 时提前推动玩家。

鸟与档位规则固定为：Twig 仅青翎；Simple 为青翎、绯翼、棱喙；Reinforced 为四鸟；Space 由 M11 四鸟编队接管。Twig 连接材料是植物纤维，不是第三根树枝。普通 M6 中滚轮向下增力、向上减力。

## 6. P0 表现

- 有合法 Actor 锚点时，以 Actor bounds 顶部作为尾线落点，沿 Actor 局部 Up 投影共享 Theme 气泡；投影失败时回退到顶部屏幕卡。
- 当前鸟引导以受控鸟为锚点；装配引导优先使用最近一次成功桩/弦 Actor。
- 卡片使用 `PanelPrimary / PanelBorder / AccentPrimary / AccentSecondary / TextPrimary / TextMuted`，不增加临时 UI Theme。
- 以 1280×720 为布局基线，按视口短边等比缩放并限制在 `0.80..1.45`；`abts.Guide.UIScale` 只作为人工验收倍率，不取代自动 DPI。
- P0 统一预留简笔图示槽位，并按步骤绘制收集、插桩、连弦、换鸟、点袋、拖袋、滚轮和发射图示；正式纹理图画、图集和动态分镜属于 P1。
- 卡片继续只保留步骤编号、标题、两行以内说明和输入短语；图示不得挤压正文或改变 Gameplay 事件语义。
- 当前步骤只有真实 CompletionFacts 全部满足后才消失。控制台命令可关闭、重置并输出状态，供 PIE 验收和排错。

## 7. 多工作树接入规则

功能工作树接入一个事件时：

1. 合并包含 `Guide/ABTSGuideEvents.h` 的最新 `master`；
2. 在自己所有的成功提交点调用一次 `FABTSGuideEventBus::Publish`；
3. 不 include 调度器、HUD 或具体 Guide Definition；
4. 不因发布失败改变原函数返回值，不增加 Gameplay 等待；
5. 为提交时点和载荷补本阶段自动化；
6. 交接列出新增事件 ID 和自动化证据。

跨阶段事件目录由集成工作树审查。若新事件会改变稳定 World Contract，不得借引导接口旁路传输生产数据；引导载荷只提供当帧表现和分类信息，不成为 M3→M7/M11 的第二条数据通道。

## 8. 验收

### 8.1 自动化

- 乱序完成事实不会补弹旧步骤；
- 同一事件按 Subject 独立计数；
- 两根 Twig 桩后才推进到连弦；
- 已组装 Twig 且当前为青翎时进入点击袋步骤；
- Ready → Pulling → PowerChanged → Launched → Completed → Scout.Revealed 顺序正确；
- 重复事件不重复完成一次性步骤；
- 未知事件只记录，不崩溃、不改变已有活动步骤；
- P0 公共事件 ID 唯一且非空。

### 8.2 用户 PIE

在 canonical `L_ABTS_M10` fresh PIE 中执行 P0 纵向切片：

1. 气泡/卡片不遮挡右侧鸟头像、底部热栏和左上侦察图；
2. 跳过某步直接完成 Gameplay 时，旧引导不补弹；
3. Twig 装配每次只推进一层，不因库存扣除事件重复跳步；
4. 不兼容鸟点击 Twig 袋时原 Gameplay 仍拒绝，引导给出青翎提示；
5. 发射、Settling 和 Returning 期间不显示无关地表卡；
6. 只有青翎完整归队后才显示侦察完成；
7. `abts.Guide.Dump` 的 Event/Guide 计数与画面一致；
8. `abts.Guide.Enabled 0` 只关闭表现和调度，不改变玩法。

### 8.3 性能预算

- 事件发布只做小数组/Map 更新，不扫描全世界；
- 无活动引导时 HUD 每帧只做一次子系统查询；
- 有世界锚点时每帧一次世界到屏幕投影；
- P0 简笔图示只增加少量 Canvas 线段/多边形，不创建 RenderTarget、SceneCapture、Tick Actor 或二进制 UI 资产；
- 目标 CPU 增量低于 `0.05 ms/frame`，额外常驻内存低于 `256 KB`（不含未来纹理）。

## 9. 排错

| 现象 | 首查 |
| --- | --- |
| Gameplay 成功但引导不推进 | 成功提交点之后是否发布；`SubjectId` 是否与目录一致；CompletionFacts 是否已记录 |
| 旧步骤突然补弹 | 完成事件是否晚于状态清理被漏发；规则是否把输入事件误当 Gameplay 完成 |
| 气泡跟错对象 | Payload 是否传稳定 Actor；Actor 已销毁时是否回退事件位置/屏幕卡 |
| 一个事件弹多张卡 | 调度器是否仍保持单活动步骤；Priority 是否只用于空闲选取 |
| 功能分支编译失败 | 是否先合并公共事件目录；不得复制本地同名声明 |
| 引导关闭后玩法异常 | 发布调用侵入了原返回值/事务；必须恢复为无副作用旁路 |

## 10. 后续阶段

- P1：DataAsset/软纹理图集、正式插画替换、升级提示、重播和输入设备图标；
- P2：完成/已看状态存档、设置菜单开关、键鼠/手柄适配与本地化；
- P3：M7 建筑弱点、卫星背面建筑、桥梁和 M11 前缀稳定器的全流程提示；
- P4：匿名本地诊断统计，用于发现长时间卡住和过度提示，不上传玩家内容。
