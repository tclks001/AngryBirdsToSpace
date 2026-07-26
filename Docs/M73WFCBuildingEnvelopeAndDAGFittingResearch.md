# M7.3 调研：3D WFC 建筑外观体块与承载 DAG 拟合

> 状态：调研结论，尚未实现。
>
> 父级：[M7.3-DAG 递归承载图总体设计](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) · [M7.3 总体算法](M73ProceduralModularBuildingGenerationResearch.md)。上游：[M7.3-DAG2.3 累计荷载与联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md)。
> 下游建议：M7.3-DAG2.4 不规则体块包络与积木排布；M7.3-WFC-1 语义体块蓝图。

## 1. 结论

可行，并且适合解决当前单塔、拱门、双塔桥无论怎样递归都趋向规整木架的问题；但不建议采用“WFC 直接生成最终砖块，之后再拟合 DAG”的强拟合方案。

推荐采用三阶段、双向约束的架构：

```text
玩法与场地约束
  -> 小尺度 3D 语义 WFC（外观/功能蓝图）
  -> 体块约束图 Envelope + Ports + Forbidden Volumes
  -> 支撑 DAG 合成与累计荷载验证
  -> 现有 DAG2.3 柱组、Brick、Contact DAG、Chaos
```

WFC 负责回答“哪里看起来像屋顶、墙、窗、门洞、梁柱、悬挑和空洞”；DAG 负责回答“哪些体块必须承载、哪些连接可以传力、哪里需要真实楼板/柱、打掉哪里会坍塌”。WFC 不能取代 DAG，也不应决定最终碰撞承载边。

## 2. 为什么当前递归 DAG 外观会规整

当前 DAG-1 的 `Series / Parallel` 先表达承载关系，再由 DAG-2 分配轴对齐 Scope；每个终端 Macro Node 主要编译为一块 Plate，再由 DAG2.3 补柱。即使递归树很深，生成词汇仍集中于：

```text
整板、水平分割、竖柱、规则平行分支
```

而建筑外观通常需要的是：

```text
退台、偏置、门洞、窗洞、屋顶冠部、局部悬挑、左右不对称、空腔、梁框
```

这些首先是空间语义与外轮廓问题，不是单纯多加几条 Support Edge 能解决的问题。

## 3. WFC 的适用性与边界

WFC 可视为约束满足，不是量子模拟，也不是保证全局可玩性的万能随机算法。它从局部邻接规则或样本中得到候选状态集合，选择熵最低的未定单元、传播约束；矛盾时需要回溯或失败回退。

WFC / Model Synthesis 的公开资料明确支持生成复杂 2D/3D 形状、局部相邻规则和非规则网格变体；但它们不保证建筑静力、可破坏弱点、Chaos 接触稳定或游戏镜头可读性。因此本项目只能把 WFC 用在尺寸受控的“建筑语义蓝图”，不能把它用于全图、无上限体素空间或最终物理构件。

### 3.1 可采用的规模

建议每栋建筑仅使用低分辨率局部格：

```text
X: 5~9 格
Y: 3~5 格
Z: 4~8 格
```

典型最大单元数约 `9 × 5 × 8 = 360`。实际只对建筑包络内有效格求解，并设置最大传播/回溯预算。它与地表 PCG 禁止全局 WFC 的原则一致：WFC 必须局部化、确定性、可终止、可回退。

### 3.2 不推荐的两条路线

| 路线 | 问题 |
|---|---|
| WFC 直接输出所有 Brick 和柱子 | 局部美术邻接不等于载荷连续；很容易产生浮空、意外接触旁路和不可验证弱点。 |
| WFC 后再把任意体素结果强行“反拟合”成当前 Series/Parallel 树 | 一般三维占据体存在环、悬挑、多路径和非层状拓扑；无损反推成二叉 Series/Parallel 树并不稳定，还会丢失 WFC 的主要外观信息。 |

## 4. 推荐表示：语义体块蓝图而非最终方块

每个 WFC 格输出一种 `EABTSBuildingSemanticCell`。它表达建筑意图，不直接对应一个 Brick Actor：

| 语义格 | 可见外观 | 对 DAG 的约束 |
|---|---|---|
| `Void` | 空气、门洞、窗洞、庭院 | 禁止楼板、柱和实体 Brick 占据。 |
| `Foundation` | 地基、底座 | 必须有 Ground / Foundation Foot；允许向上支撑。 |
| `FloorCarrier` | 楼板、平台、横梁段 | 必须有至少一个下方支撑组；可承接上方荷载。 |
| `ColumnZone` | 柱、墙垛、塔肢 | 允许垂直柱链；相邻层间优先创建 Support Port。 |
| `WallInfill` | 墙面、装饰填充 | 可生成短砖，但不作为主承重路径，除非被提升为 `ColumnZone`。 |
| `WindowVoid` | 窗孔 | 禁止实体填充；要求周边存在框梁/墙垛，避免整面墙无端悬空。 |
| `DoorVoid` | 门洞/攻击孔 | 从地面或可达平台连通；两侧与顶部要求框架。 |
| `BeamZone` | 横向梁、桥面 | 允许横向 Deck / Carrier；端点必须连接到可承载 Zone。 |
| `Roof` | 塔顶、屋檐、冠部 | 仅允许位于实体或梁柱之上；通常不作为上部主承重来源。 |
| `Cantilever` | 可读悬挑 | 需要回锚长度、最大悬挑比例；不允许作为无条件主支撑。 |
| `WeaknessSocket` | 弱点或装置槽 | 必须位于 DAG 的主承重切面附近，不能只是视觉贴花。 |

WFC 同时输出每格的端口掩码：

```text
TopLoadPort / BottomSupportPort
LeftBeamPort / RightBeamPort
FrontFacadePort / BackFacadePort
NoSolidVolume / RequiredVoid
```

这份输出称为 `FABTSM73SemanticEnvelope`。它是 WFC 和 DAG 之间稳定的中间接口。

## 5. 两阶段算法

### 阶段 A：受玩法约束的 3D 语义 WFC

先放置硬约束，再做 WFC：

```text
地面层：Foundation 或 DoorVoid
建筑外上表面：Roof / Parapet / OpenTop
指定攻击面：DoorVoid、WindowVoid、WeaknessSocket
预留弹道空域：Void
边界外：Outside / Void
```

之后传播局部规则。例子：

```text
Roof 只能位于 FloorCarrier / ColumnZone / BeamZone 上方
DoorVoid 必须与外部或平台连通，左右至少一侧为 ColumnZone
WindowVoid 上方必须是 BeamZone 或 FloorCarrier
Cantilever 后方必须连续连接 ColumnZone / FloorCarrier
Foundation 下方只能是 Ground
Void 不得携带 BottomSupportPort
```

规则来源可混合：

- 人工定义的小型建筑语义邻接表；
- 从 10~30 个手工搭建的低模积木建筑样本提取相邻模式；
- Preset / Biome / 难度对状态权重的偏置。

建议第一版使用人工规则表，不采用单一截图自动学习。因为项目目标是可读、可破坏的弹弓建筑，而不是复刻某张样本图的局部纹理。

### 阶段 B：从语义蓝图合成受限 Support DAG

不能“从 WFC 结果反向猜唯一 DAG”；正确做法是从语义端口构造候选图，再在约束下求一张稀疏承载 DAG：

```text
FloorCarrier / BeamZone 的 BottomSupportPort
  -> 向下查找同 XY 或允许偏移范围内的
     ColumnZone / Foundation / FloorCarrier TopLoadPort
  -> 形成候选 Support Edge
  -> DAG2.3 按累计荷载与联合支撑凸包选择实际边
```

语义蓝图还提供三个空间掩码：

```text
MustOccupy：必须生成 Deck / Column / Foundation 的区域
MayOccupy：可被 Brick 填充或留空的区域
MustVoid：严禁 Plate / Column 占据的门窗、弹道和庭院区域
```

因此不再是“用 DAG 拟合一个任意 WFC 体素模型”，而是：

```text
WFC 规定外观与不可违反的空间边界
DAG 在边界内寻找可承载、可破坏的最小结构骨架
```

### 阶段 C：Brick 编译与审计

现有 DAG2.3 后续链保留：

```text
Selected Supports
  -> Deck / Column Brick
  -> Contact DAG 重建
  -> 静态稳定、旁路与 Chaos 验证
```

但 Module Compiler 需从 `SemanticEnvelope` 获取局部构件词汇：完整板、分段板、偏置板、门窗框、短墙、悬挑段。它们仍全部由当前基础长方体 HISM / Actor Brick 组成，不需要棱柱、圆柱或棱锥。

## 6. 为什么它能生成不规则外观

WFC 输出的并非每层都完整覆盖的矩形楼板，而是离散但受约束的体块组合。例如：

```text
Z=5:       Roof Roof
Z=4:  Wall Beam Beam Void
Z=3:  Column Window Wall Column
Z=2:  Column Door   Void Column
Z=1:  Foundation Foundation Foundation
```

从这里可自然得到：一侧高塔、另一侧低墙、偏置桥面、门洞、窗洞、局部悬挑、退台和不对称屋顶。它们都来自方块占据与空洞的组合，最终构件仍是立方体/长方体。

更重要的是，`WeaknessSocket`、`DoorVoid`、桥面和悬挑不是后期装饰；它们会影响 `MustVoid`、可用支撑端口、累计荷载和 DAG 的主承重切面。

## 7. 与当前 DAG2.3 的接口建议

| 当前对象 | WFC 阶段后的变化 |
|---|---|
| `FABTSM73DAGGenerationSettings.Preset` | 从唯一轮廓定义降为 WFC 风格族、主题与强制锚点；保留旧 Preset 作回退。 |
| `FABTSM73DAGGenerationResult` | 增加来自 Semantic Envelope 的 Macro Node、允许端口、MustVoid 约束和节点语义。 |
| `FABTSM73DAGMacroLayout.AllowedScope` | 不再只是递归均分矩形；由相连语义格的并集、局部偏移和占据掩码导出。 |
| `FABTSM73DAGLoadSupportSolver` | 候选 Support Edge 必须同时满足 Port 对齐、可放柱区域和 MustVoid 排除；累计荷载/联合凸包逻辑保持。 |
| `FABTSM73DAGModuleCompiler` | 依据语义将一个 Macro 编译为 1~N 块长方体 Deck、Column、Frame Brick。 |
| `FABTSM73DAGContactGraphBuilder` | 保持为唯一物理真相；新增 MustVoid 违规检查和非预期旁路审计。 |

## 8. WFC 规则不应单独保证的内容

| 需求 | 正确责任层 |
|---|---|
| 屋顶邻接墙、窗不贴地、门洞连外界 | WFC / 语义约束图 |
| 每层是否能承载、柱脚是否覆盖累计合力点 | DAG2.3 荷载支撑求解 |
| 砖块是否意外接触并绕过弱点 | Contact DAG 审计 |
| 受弹弓冲击后是否产生预期坍塌 | Chaos / 反事实破坏验证 |
| 一击弱点比攻击任意墙更有效 | Weakness Planner + 多次发射仿真 |

## 9. 风险与控制

| 风险 | 原因 | 控制 |
|---|---|---|
| WFC 无解或回溯过久 | 3D 局部规则、门窗和承载端口互相矛盾 | 低分辨率局部格、最大传播/回溯步数、确定性 Seed、超限时回退到当前 DAG Preset。 |
| 外观好但没有物理解 | WFC 的局部规则不了解质量与接触凸包 | WFC 后必须执行 DAG 合成；无可行 DAG 的 WFC 输出丢弃或局部修复。 |
| 物理好但又变成长方体 | DAG Compiler 忽略语义占据/空洞 | MustOccupy / MustVoid 成为编译硬约束；Plate 支持分段与偏置。 |
| 门窗过多破坏稳定性 | 空洞切断传力路径 | 门窗周边强制 Frame / BeamZone；限制同层空洞比例。 |
| 高 Budget 出现旁路接触 | Brick 太密、柱太粗、相邻 Plate 重叠 | 把 Contact DAG 预测加入候选筛选；WFC 阶段保留最小净空。 |
| 结果像随机建筑而非可攻击关卡 | 仅按样式权重求解 | 强制攻击面、弱点 Socket、目标/炸药位置和弹道净空。 |

## 10. 推荐落地顺序

不建议直接开始通用 3D WFC。建议按以下里程碑推进：

1. **DAG2.4：不规则 Brick 编译。** 先使现有 DAG 支持分段 Deck、偏置、退台、门窗框和 `MustVoid`；验证只有方块也能产生非规则轮廓。
2. **WFC-0：手工语义蓝图。** 用编辑器可视化体素/格子手工布置 `Foundation / Floor / Column / Void / Door / Roof`，测试“语义蓝图 -> DAG2.3 -> Brick”的接口。
3. **WFC-1：小格局部 WFC。** 只在 `5~9 × 3~5 × 4~8` 范围内生成语义蓝图，提供建筑主题与硬锚点。
4. **WFC-2：可控样本规则。** 从少量人工验收通过的蓝图提取/维护邻接表，增加风格权重和确定性变体。
5. **WFC-3：玩法评估。** 联合弱点规划、弹道净空和 Chaos 反事实模拟，对多个 WFC 候选筛选而非只接受第一次解。

## 11. 验收建议

- 同一 Seed 可重复得到同一语义蓝图、Support DAG 和 Brick 结果。
- 至少生成明显不同的退台塔、偏置桥、门洞墙、单侧高塔四种轮廓，且均只用长方体 Brick。
- `MustVoid` 中无 Brick；`MustOccupy` 至少有对应 Deck / Column / Foundation。
- 所有实际承重关系通过 DAG2.3、Contact DAG 与静态验证；不将 WFC 邻接当成承重事实。
- 对每个 WFC 成功候选执行有限 Chaos 验证；失败候选可解释地回退或重抽样。

## 12. 公开资料与访问时间

访问日期：2026-07-26。

- Maxim Gumin, [WaveFunctionCollapse](https://github.com/mxgmn/WaveFunctionCollapse)：README 将 WFC 定义为局部图样约束与传播过程；输出只保证局部相似性，矛盾可导致失败。
- Paul Merrell, [Model Synthesis](https://paulmerrell.org/model-synthesis/)：说明 Model Synthesis 可生成复杂 2D/3D 形状；该站明确指出 WFC 重新实现了其 Model Synthesis 思路，二者结果相近；3D 模型是其主要关注方向之一。
- BorisTheBrave, [Wave Function Collapse Explained](https://www.boristhebrave.com/2020/04/13/wave-function-collapse-explained/)：将 WFC 解释为约束编程，适合作为本项目使用硬约束、传播、回溯上限和失败回退的工程依据。
- Pascal Müller et al., *Procedural Modeling of Buildings*, ACM TOG 2006：CGA Shape Grammar 的 Scope、Split、Repeat 证明“先有体块包络与语义划分、再细化构件”是成熟的程序化建筑范式；本项目只借鉴其分层思想，不把它当作物理稳定性保证。
- 当前项目 [ABTSTaskGraphPCGDesign.md](ABTSTaskGraphPCGDesign.md)：已规定 WFC 必须局部化、有限回溯且不能决定全局逻辑图；该约束同样适用于建筑 WFC。
