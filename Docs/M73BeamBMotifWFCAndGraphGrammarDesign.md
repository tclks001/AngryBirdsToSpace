# M7.3-Beam-B：Bay Motif WFC 与结构图语法

> 父级：[长条形积木建筑生成调研与演进方案](M73BeamBlockStructuralGenerationResearch.md)。
>
> 上游：[M7.3-Beam-A v2](M73BeamAStructuralIRPreviewDesign.md)。
>
> 总导航：[M7 建筑系统文档导航与执行路线](M7BuildingDevelopmentRoadmap.md)。
>
> 状态：Beam-B 全局装配收口与自动化已完成，等待用户编辑器读形验收；不接管 TaskGraph 生产建筑。

## 1. 目标与边界

Beam-A 已解决“语义 Volume 如何变成可搭放的 XYZ 长条积木和 Bearing Contact”。Beam-B 解决另一维问题：
同一个轮廓内部不能永远使用同一种分层方框，而应由有限结构母题形成不同的承托读形。

本阶段实现：

- 在 Beam-A Bay 图上运行第二级、确定性的 Motif WFC；
- 当前母题包括 `PostAndLintel`、`PortalFrame`、`CrossBeam`、`TwoLayerCrib`、
  `TransferFrame`、`CantileverBay`、`BridgeBay`；`BracedBay` 保留为枚举兼容项，但不进入 WFC 生成域；
- 用 Port 约束相邻 Bay 的 X/Y 连续性，Bridge 只允许出现在上游 Bridge Volume；
- 用图语法深度增加横梁、交替木垛、转换支点或悬挑根部等拓扑；
- 所有计划构件保持在来源 Bay/Volume 允许域内，跨 Bay 只通过相容 Port 表达；
- 将 Motif 计划构件编译为 Beam-A `Joint / Member / Assembly` IR，并复用 Beam-A 的全局装配收口；
- 最终预览只绘制收口后的闭合成员集合，要求无正体积穿透、无悬空承重分量、无斜撑；
- 提供无碰撞、PIE 隐藏的 Editor-only 彩色母题预览和确定性摘要。

本阶段不实现：

- Bearing Graph 到权威 Load DAG 的反力、环和跨距验证；
- 离散长度目录、真实 `AABTSM7BuildingModule`、材质、碰撞或 Chaos；
- Failure Frontier、弱点、TaskGraph 默认切换或六栋生产候选。

上述职责分别属于 Beam-C、Beam-D 和 Beam-E。

## 2. 数据流

```text
DAG5-B v2 semantic volumes
  -> Beam-A Bay decomposition and accepted stacked-block baseline
  -> Beam-B domain construction
  -> deterministic Motif WFC collapse + port propagation
  -> bounded graph grammar expansion
  -> planned structural members
  -> compile to Beam-A Joint / Member / Assembly IR
  -> shared Beam-A global assembly closure
  -> closed structural members for editor preview
  -> Beam-C Load DAG/static proxy (next)
```

Beam-B 的 `PlannedMember` 是结构意图，不是物理 Brick。它保留 Bay、Motif、规则来源和 XYZ
方向。长度小于积木截面且无法实体化的局部装饰短段在编译前被省略；其余构件进入 Beam-A IR，
由同一套全局收口执行共线合并、立柱切分、水平层分离、支承修补、孤立片段裁剪、Bearing
重建和最终穿透检查。Beam-C 只消费闭合结果，不再重新猜测 Beam-B 的空间装配关系。

## 3. Motif 与 Port

| Motif | 结构读形 | 主要使用区域 |
| --- | --- | --- |
| PostAndLintel | 两柱一梁的最小结构词 | Annex、Crown、窄 Bay |
| PortalFrame | 有底梁、双柱和顶梁的门架 | Body、Foundation |
| CrossBeam | 两向梁层与四角支柱 | 宽 Body/Foundation |
| TwoLayerCrib | X/Y 交替堆放的木垛层 | Roof、Crown、矮 Bay |
| TransferFrame | 下部少支点转为上部多支点 | 高 Body |
| CantileverBay | 有明确根部和配重侧的悬挑 | Annex、外缘 Bay |
| BracedBay | 保留枚举；当前不进入生成域 | 等显式斜撑座与连接契约完成后再启用 |
| BridgeBay | 双主梁、端横梁和跨 Chunk Port | Bridge Volume，强制 |

Port 使用 `X- / X+ / Y- / Y+ / Lower / Upper` 位掩码。WFC 只负责局部邻接相容；它不把
Port 相容误当成结构已经稳定。相邻 Bay 的共享边界必须由两侧同时提供相向 Port。

## 4. 图语法

首版规则为：

- `BeamToGrillage`：在跨度允许时增加均匀平行梁；
- `AlternateCribLayer`：增加一层与前层正交的木垛；
- `AddTransferTier`：把少数下支点展开为更多上支点；
- `AddCantileverRoot`：为悬挑增加根部和配重联系；
- `TriangulateBay`：保留规则枚举但当前停用，不产生任何斜撑构件；
- `RefinePortal`：为高门架增加中间联系层。

`GrammarDepth` 增加时必须增加 `GrammarStepCount` 或 `PlannedMemberCount`，不能只把同一个外盒
切成更细但拓扑等价的片段。所有规则受 `MaxGrammarStepCount` 和 `MaxPlannedMemberCount` 硬预算控制，
失败时不返回半张图。

## 5. 编辑器预览

Actor：`M7.3 Beam-B Motif WFC Preview`。

- 每种 Motif 使用独立颜色，颜色按闭合成员所属 Assembly/Bay 回溯；
- Preview 永久无碰撞、无 Overlap、无导航影响，并在 PIE/游戏中隐藏；
- `GrammarDepth` 用于观察结构复杂度增长；
- `bRequireMotifVariety` 要求足够 Bay 时至少出现多种结构家族；
- Last Result 显示 Motif、WFC、规则、计划构件、闭合成员、Bearing、收口修补和 Hash 统计。

## 6. 自动化验收

过滤器：`ABTS.M73DAG.BeamB.`。

- `Determinism`：同输入 Motif、Port、规则、构件和 Hash 完全一致；
- `MotifCoverage`：固定种子矩阵覆盖首版结构家族，Bridge Volume 只能选择 BridgeBay；
- `PortCompatibility`：所有相邻 Bay 的相向 Port 相容；
- `GrammarDepthAddsTopology`：提高深度会增加规则或计划构件，不改变上游轮廓身份；
- `BoundsAndBudget`：计划构件不越出所属 Bay，预算不足原子拒绝；
- `InvalidSettings`：非法深度、预算或截面稳定 fail closed；
- `GlobalAssemblyClosure`：多组 Archetype 的闭合成员独立 AABB 检查无正体积穿透，且每个成员均可沿 Bearing 链到达地面；
- `NoDiagonalMembers`：即使旧资产把 `bAllowBracedBay` 设为真，也不得选择 BracedBay 或生成计划/闭合斜撑。

## 7. 用户编辑器验收

在空白地图或 `PlanarPhysicsTestMap` 放置新的 Beam-B Preview Actor，无需 PIE：

1. 同一上游轮廓内应能看到至少两种颜色/结构母题，而不是全楼重复同一种方框；
2. `GrammarDepth` 从 1 提高到 3～4 时，门架联系层、木垛层或平行梁数量应增加；
3. Bridge 体量必须显示双长梁/横向端梁，不得坍缩为普通塔楼母题；
4. Cantilever 只应出现在外缘/Annex 候选；当前任何设置下都不应出现斜杆；
5. 不应再看到整组悬空、相互横穿或大块正体积重叠；允许端面接触与上下承托接触；
6. Details 中 Accepted 为真，PortViolationCount、OutOfBoundsMemberCount、
   RemainingPenetrationCount、UnsupportedMemberCount、DiagonalMemberCount 均为 0，
   ClosedMemberCount 与 ClosedBearingContactCount 大于 0；
7. 进入 PIE 后预览不可见且不参与物理 Gate。

本阶段验收“结构复杂度、局部拼接语义以及静态全局装配闭合”。真实 Brick/Chaos 动态稳定性、
可玩弱点和权威 Load DAG 仍不在 Beam-B 宣称范围内。

## 8. 自动化证据

- 2026-08-01 Development Editor 构建成功；当前工作树没有活动 Editor，使用
  `-NoHotReload -NoHotReloadFromIDE` 完整链接；
- `Saved/Logs/BeamB-Closure-20260801-201643-FreshAutomation.log`：精确找到 8 项
  `ABTS.M73DAG.BeamB`，8/8 Success；
- `Saved/Logs/BeamB-Closure-20260801-201749-M7FreshAutomation.log`：精确找到 90 项
  `ABTS.M7`，90/90 Success；
- 种子/Archetype 矩阵覆盖至少六类 Motif，Bridge Volume 强制映射为 BridgeBay；
- 深度 1 与深度 4 保持相同 Motif WFC Hash，同时增加规则步骤和计划构件数量；
- Beam-B 与 Beam-A 调用同一个 `CloseGeneratedAssembly`，不存在第二套收口判定；当前生成域中斜撑数量恒为 0。
