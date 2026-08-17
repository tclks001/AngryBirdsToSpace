# M11-D 终局发射 HUD 视觉设计与 UE 落地方案

## 1. 目标与边界

M11-D 把既有的轨道概览、三旋钮发射控制、目标 PIP、轨迹探针和候选身份提示，整理为一套可直接在终局玩法中使用的统一 HUD。设计参考：

- `Docs/UIReferences/ABTS_UI_M11ConsoleTarget_v001.png`
- `Docs/UIReferences/ABTS_UI_MasterStyleBoard_v001.png`
- `Docs/UIReferences/ABTS_UI_ComponentStates_v001.png`
- `Docs/ABTSSharedUIThemeDesign.md` 中已冻结的 Theme v1

本阶段不改变轨迹求解、F1-F4 判定、释放门、PIP 捕获、相机或候选认证语义，也不新增共享 Token、UMG 蓝图、纹理或字体资产。所有视觉实现均位于 M11 专属 C++ Canvas HUD 与纯数据布局代码中。

## 2. 信息层级

玩家每一帧首先需要回答三个问题：

1. 当前通过了几段引力走廊，是否已达到 F4；
2. 下一步应调哪个轴、向哪个方向调；
3. 当前目标接近画面和发射按钮在哪里。

因此生产 HUD 只常显任务链、稳定器状态、旋钮值/引导、目标 PIP、轨道概览和发射状态。求解耗时、延迟、丢弃数、Revision、Capture Count 与候选哈希属于工程诊断，只在 `abts.UI.Theme.DebugOverlay 1` 时显示。

编辑器候选模式必须始终显示 `EDITOR CANDIDATE / NOT CERTIFIED`，防止候选结果被误认为正式认证；详细身份哈希仍受调试开关控制。

## 3. 四区布局

HUD 使用安全边距和连续缩放计算，禁止以 1280x720 的绝对坐标作为唯一真值。

| 区域 | 位置 | 内容 | 行为 |
|---|---|---|---|
| Mission strip | 顶部安全区 | 终局标题、F1-F4 链、稳定器/发布状态 | 常显，单行优先 |
| Orbit panel | 左下 | 轨道概览、Select/Move、Reset/Rebase/Auto | 保留既有命中半径与探针语义 |
| Control deck | 底部中央 | Yaw/Pitch/Power、精度档、F4 引导、Launch | 所有可点击形状与命中盒同源 |
| Target monitor | 右下 | 自动目标或冻结探针 PIP、轨迹、最近点 | 保持原始 RenderTarget 宽高比 |

四区之间保留至少 12 px 间距；中央上半部保持为游戏世界净空。窄视口先收紧面板间距、字体和辅助说明，再缩小轨道盘与旋钮，但不缩小到既有最小交互半径以下。

## 4. Theme v1 映射

M11-D 每帧只读取 `FABTSUITheme::Get()`，不维护私有调色板。

| UI 语义 | Theme v1 Token |
|---|---|
| 面板底色/内层底色 | `PanelPrimary` / `PanelSecondary` |
| 外框/刻线 | `PanelBorder` / `SlotBorder` |
| 主操作与 F4 引导 | `AccentPrimary` / `Success` |
| 空间信息与轨迹 | `AccentSecondary` |
| 正文/次要说明 | `TextPrimary` / `TextMuted` |
| 候选提醒/失败 | `Warning` / `Danger` / `DangerFlash` |
| 不可用状态 | `Disabled` |

面板采用切角轮廓、双层边框、短角标和轻度透明填充，延续主风格板的机械舱体语言；不使用发光纹理堆叠。不同星体仍可通过轮廓形状、编号与当前选择强调区分，避免扩展共享颜色契约。

## 5. 状态表现

- 普通按钮：`SlotNormal + PanelBorder`。
- 选中/按下：`SlotSelected` 或语义 Accent，边框增粗。
- F4 严格命中：`Success` 外环和 `READY TO LAUNCH`。
- Launch：常态使用 `AccentPrimary`，按下使用 `Warning`，不可发射使用 `Disabled`。
- 求解中：保留上一次已发布结果，并以 `Warning` 文本提示刷新；不得把未发布候选显示成最新结果。
- 失败黑场：继续作为继承 HUD 之后的最终合成层，语义不变。
- 屏幕边缘目标楔标：生产 HUD 已退役；方向调整由三仪表/F4 指示承担，目标空间关系由轨道概览与 PIP 承担，不再在游戏世界净空区重复显示 `PLANET 1–3` 或 `UFO` 楔形标签。
- 轨道全览 MOVE：滚轮上滚放大、下滚缩小；左键拖拽平移、右键拖拽旋转。释放拖拽时不得调用 `SetMouseLocation` 或恢复按下点，系统光标保持在玩家实际释放的位置。
- F4 轨道全览：玩家权威求解前缀使用白色实线；只有通过 `PlaybackPlan` 身份与物理接触门的表现终端延长段才使用统一的琥珀色连续实线，并一直绘制到 UFO 的物理接触面。该延长线不可被 Select/Probe 命中，颜色仍须与白色玩家权威前缀明确区分。

## 6. UE 实现方法

1. 在 `ABTSM11FinaleHUDData` 增加纯数据的四区布局构建器；宽屏与紧凑视口使用同一算法。
2. `AABTSM11FinaleHUD` 使用布局结果同时更新绘制位置和命中盒，杜绝视觉/交互漂移。
3. 增加 M11 本地切角面板绘制原语；填充与描边共享同一组八个截角顶点，禁止先画完整矩形再用斜边覆盖；轨道、按钮、旋钮、任务条和 PIP 框全部消费 Theme v1。
4. 将生产状态与调试诊断分层；`Theme.bDebugOverlay` 是唯一诊断显示门。
5. 使用 M11 专属 `-game -RenderOffscreen` 截图流程进入 Aiming 状态并捕获真实 GameViewport HUD；截图不得代替 NullRHI 自动化、实时 Chaos 或正式可见 PIE 证据。

## 7. 验收矩阵

- 1280x720、1600x900、1920x1080：四区不相交，中央视野净空，PIP 保持宽高比。
- 1024x768 紧凑视口：按钮和旋钮仍可命中，辅助说明允许缩短，面板不得越界。
- Theme 默认与实时覆盖：所有 M11 面板、文本和状态色同步变化。
- Aiming：Orbit、PIP、Controls、Mission strip 同时可见。
- Release/Flight：PIP 与发射控制按既有语义隐藏，轨道和任务状态继续显示。
- Candidate：`NOT CERTIFIED` 常显；哈希只在调试开关开启时显示。
- 自动化：纯布局、输入映射、独占捕获和 PIP 边缘提示全部通过。
- 像素边界：四区面板的四个截角外侧只允许显示游戏场景，不得露出矩形填充或额外色块。
- PIP 对齐：保持 RenderTarget 宽高比后，在 Target Monitor 标题线下的有效内容区水平、垂直居中；宽高比产生的剩余空间不得只堆积到左侧或顶部。
- 轨道全览滚轮：在 MOVE 模式且指针位于轨道盘内时，上滚后 `Zoom` 严格增大，下滚后严格减小；三仪表既有微调方向不随本项改变。
- 轨道全览拖拽：右键释放只结束 M11 旋转捕获，不触发继承镜头的光标恢复；退出终局后普通 Party Camera 的原始右键释放绑定必须恢复。
- 轨道全览终端延长：F4 且存在有效 `VisibleTerminalTransfer` 时，白色实线终点到 UFO 接触面之间出现单一琥珀色实线；`VisibleTerminalTransfer` 与 `CertifiedNominalTail` 不再以虚线或双色接缝分隔。显示几何只允许投影与真实飞行相同的 `PlaybackPlan::Sample()` 位置样本，再以“只删点、不生成新位置”的保形算法去除冗余点；禁止在筛选后的非相邻样本之间再次执行屏幕空间 Hermite。运行时按最大 `4×` 缩放与 `512 px` 轨道盘半径保守换算三维弦误差，使最终屏幕偏差不超过 `0.35 px`；点预算不足时 fail closed，不得用低精度折线代替。不得直接连接稀疏缓存位置，也不得让长直尾段稀释弯曲转移段。计划 Hash 不匹配或终点未落在物理接触球时不得显示延长线。
- Candidate F4 终端权威：不得把合格包络终点与某条固定 UFO 尾段用单一五次曲线强接。实现从 Released 轨迹末端向前搜索最晚可行交接状态，以该点位置和速度方向构造“与白色轨迹相切且经过 UFO 中心”的唯一三维圆；圆弧在第一次进入 800 cm 物理接触球时结束。交接后的短段用五次曲线匹配 Released 的位置、速度、加速度，并在进入定半径圆弧时匹配圆弧的位置、速度、向心加速度。组合路径必须通过目标距离单调下降、转向符号不反转、单步航向、最小速度、加速度、jerk 与天体净空门，失败则 fail closed。
- HUD 与飞行共用边界：提前交接会替换 Released 结果中交接点之后的白色后缀；轨道全览必须在同一 `TransferStartTimeSeconds` 截断白线并插入精确的 PlayerAuthoritative 锚点，再绘制琥珀色复合段。HUD 不得继续保留实际不会飞行的合格包络后缀，也不得自行推导另一条圆弧。Candidate 实际飞行和 HUD 均消费同一个 `FABTSM11PlaybackPlan`；生产认证路径及其 `PhysicalTrajectoryHash` 语义本轮保持不变。

## 8. 上下游与回退

- 上游：冻结 Theme v1、M11-C 交互/求解/PIP 数据、M11 相机与候选身份。
- 下游：M11 可见 PIE 验收、正式终局美术资产替换、集成工作树的共享 UI 复核。
- 回退：本阶段没有二进制资产迁移；回退 M11 HUD/Data 源码和本文档即可恢复旧布局，不影响共享 Theme、关卡和稳定契约。
