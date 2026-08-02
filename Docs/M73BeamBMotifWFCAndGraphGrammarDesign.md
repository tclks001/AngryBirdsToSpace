# M7.3-Beam-B：Bay Motif WFC 与结构图语法

> 父级：[长条形积木建筑生成调研与演进方案](M73BeamBlockStructuralGenerationResearch.md)。
>
> 上游：[M7.3-Beam-A v2](M73BeamAStructuralIRPreviewDesign.md)。
>
> 总导航：[M7 建筑系统文档导航与执行路线](M7BuildingDevelopmentRoadmap.md)。
>
> 状态：Beam-B 全局装配收口、Beam-A 语义轮廓拟合与自动化已完成，等待用户编辑器读形验收；不接管 TaskGraph 生产建筑。

## 1. 目标与边界

Beam-A 已解决“语义 Volume 如何变成可搭放的 XYZ 长条积木和 Bearing Contact”。Beam-B 解决另一维问题：
同一个轮廓内部不能永远使用同一种分层方框，而应由有限结构母题形成不同的承托读形。

本阶段实现：

- 在 Beam-A Bay 图上运行第二级、确定性的 Motif WFC；
- 当前生成母题包括 `PostAndLintel`、`PortalFrame`、`CrossBeam`、`TwoLayerCrib`、
  `TransferFrame`、`BridgeBay`；`CantileverBay` 与 `BracedBay` 仅保留枚举/旧资产兼容，不进入 WFC 生成域；
- 用 Port 约束相邻 Bay 的 X/Y 连续性；上游 `SupportedSpan` 强制选择 `BridgeBay`；
- `SupportedSpan` 的终端主梁延伸到权威净开口边界，并在两端生成归属指定承托模块的横向 `BridgeSeat`；
- 用图语法深度增加横梁、交替木垛或转换支点等拓扑；
- Box Bay 使用 Beam-B Motif；Prism/Pyramid Bay 直接复用 Beam-A 的逐层收分屋顶编译器，
  不再用矩形 TwoLayerCrib 覆盖上游 Shape Grammar 语义；
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
  -> Box Bay motif members + Beam-A semantic roof courses
  -> supported-span endpoint rail extension + support-module bridge seats
  -> compile to Beam-A Joint / Member / Assembly IR
  -> shared Beam-A global assembly closure
  -> closed structural members for editor preview
  -> Beam-C Load DAG/static proxy (next)
```

Beam-B 的 `PlannedMember` 是结构意图，不是物理 Brick。它保留 Bay、Motif、规则来源和 XYZ
方向。长度小于积木截面且无法实体化的局部装饰短段在编译前被省略；其余构件进入 Beam-A IR，
由同一套全局收口执行共线合并、立柱切分、水平层分离、支承修补、孤立片段裁剪、Bearing
重建和最终穿透检查。Beam-C 只消费闭合结果，不再重新猜测 Beam-B 的空间装配关系。

非 Box Bay 使用共享的 `ABTSM73BeamA::BuildSemanticRoofMembers` 生成规划层 `RoofCourse`。
闭合编译不复制一套近似屋顶，而是从已验收的 Beam-A Assembly 中保留该语义体及其沿 Bearing
向下的完整支撑祖先，再与 Box Bay 的 Beam-B Motif 构件一起交给
`ABTSM73BeamA::CloseGeneratedAssembly`。这样轮廓来源和结构闭合各有唯一权威实现。

## 3. Motif 与 Port

| Motif | 结构读形 | 主要使用区域 |
| --- | --- | --- |
| PostAndLintel | 两柱一梁的最小结构词 | Annex、Crown、窄 Bay |
| PortalFrame | 有底梁、双柱和顶梁的门架 | Body、Foundation |
| CrossBeam | 两向梁层与四角支柱 | 宽 Body/Foundation |
| TwoLayerCrib | X/Y 交替堆放的木垛层 | Roof、Crown、矮 Bay |
| TransferFrame | 下部少支点转为上部多支点 | 高 Body |
| CantileverBay | 保留枚举；当前不进入生成域 | 单边悬挑已停用，不得靠补地长柱救活 |
| BracedBay | 保留枚举；当前不进入生成域 | 等显式斜撑座与连接契约完成后再启用 |
| BridgeBay | 双主梁、端横梁、跨 Chunk Port；终端主梁延伸到权威开口边界并落在桥托上 | SupportedSpan，强制 |

Port 使用 `X- / X+ / Y- / Y+ / Lower / Upper` 位掩码。WFC 只负责局部邻接相容；它不把
Port 相容误当成结构已经稳定。相邻 Bay 的共享边界必须由两侧同时提供相向 Port。

## 4. 图语法

首版规则为：

- `BeamToGrillage`：在跨度允许时增加均匀平行梁；
- `AlternateCribLayer`：增加一层与前层正交的木垛；
- `AddTransferTier`：把少数下支点展开为更多上支点；
- `AddCantileverRoot`：保留规则枚举；当前没有可进入该规则的 CantileverBay；
- `TriangulateBay`：保留规则枚举但当前停用，不产生任何斜撑构件；
- `RefinePortal`：为高门架增加中间联系层。
- `AddBridgeSeat`：在跨越体两端记录指定承托模块的横向桥托；全局收口可将桥托吸收到同模块既有承托梁，但不得丢失模块到桥体的局部 Bearing 路径。

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
- `MotifCoverage`：固定种子矩阵覆盖当前结构家族，SupportedSpan 只能选择 BridgeBay，且生成域不含 CantileverBay；
- `PortCompatibility`：所有相邻 Bay 的相向 Port 相容；
- `GrammarDepthAddsTopology`：提高深度会增加规则或计划构件，不改变上游轮廓身份；
- `BoundsAndBudget`：计划构件不越出所属 Bay，预算不足原子拒绝；
- `InvalidSettings`：非法深度、预算或截面稳定 fail closed；
- `GlobalAssemblyClosure`：多组 Archetype 的闭合成员独立 AABB 检查无正体积穿透，且每个成员均可沿 Bearing 链到达地面；
- `NoDiagonalMembers`：即使旧资产把 `bAllowBracedBay`、`bAllowCantilever` 设为真，也不得选择 BracedBay、CantileverBay 或生成计划/闭合斜撑。
- `SupportedSpanVoid`：有意架空跨越必须留下非空保留空间，闭合 Assembly 的 Z 柱不得进入其跨中下方。
- `BridgeEndpointBearing`：每个 SupportedSpan 必须有两份桥托账本；桥托留在上游指定的语义模块内，闭合后两端均存在“指定模块 -> 桥体”的局部 Bearing 路径，且桥体 Assembly 不得靠落地长柱救援。
- `SemanticRoofFitting`：Prism/Pyramid 的规划屋顶必须落在 Beam-A 权威逐层包络内，最高层相对最低层
  在对应轴上明显收分，同时最终闭合结构仍须无悬空、无穿透。

## 7. 用户编辑器验收

在空白地图或 `PlanarPhysicsTestMap` 放置新的 Beam-B Preview Actor，无需 PIE：

1. 同一上游轮廓内应能看到至少两种颜色/结构母题，而不是全楼重复同一种方框；
2. `GrammarDepth` 从 1 提高到 3～4 时，门架联系层、木垛层或平行梁数量应增加；
3. SupportedSpan 必须显示双长梁/横向端梁；终端主梁应延伸至净开口边界并分别落在两端横向桥托上，不得留下可见端缝，也不得坍缩为普通塔楼母题；
4. 不得生成单边 Cantilever；旧 `bAllowCantilever` 开关不再恢复该形态，当前任何设置下也不应出现斜杆；
5. 上游 Prism 应由逐层缩短一个水平轴的梁层读出坡屋顶；Pyramid 应同时沿 X/Y 收分，
   不得再显示为等宽矩形木垛或平顶框；
6. 不应再看到整组悬空、相互横穿或大块正体积重叠；允许端面接触与上下承托接触；
7. Details 中 Accepted 为真，PortViolationCount、OutOfBoundsMemberCount、
   RemainingPenetrationCount、UnsupportedMemberCount、DiagonalMemberCount 均为 0，
   SemanticEnvelopeViolationCount 为 0，ClosedMemberCount 与 ClosedBearingContactCount 大于 0；
8. SupportedSpan 下方必须保持为空，不得生成跨中落地长柱；两端桥托应由上游指定的承托模块承担，允许收口后与该模块既有承托梁合并，但不允许桥体自身补到地面；
9. 进入 PIE 后预览不可见且不参与物理 Gate。

本阶段验收“结构复杂度、局部拼接语义以及静态全局装配闭合”。真实 Brick/Chaos 动态稳定性、
可玩弱点和权威 Load DAG 仍不在 Beam-B 宣称范围内。

## 8. 自动化证据

- 2026-08-01 Development Editor 构建成功；当前工作树没有活动 Editor，使用
  `-NoHotReload -NoHotReloadFromIDE` 完整链接；
- `Saved/Logs/BeamB-Closure-20260801-201643-FreshAutomation.log`：精确找到 8 项
  `ABTS.M73DAG.BeamB`，8/8 Success；
- `Saved/Logs/BeamB-Closure-20260801-201749-M7FreshAutomation.log`：精确找到 90 项
  `ABTS.M7`，90/90 Success；
- `Saved/Logs/BeamB-Semantic-Full-20260801-2210.log`：精确找到 9 项
  `ABTS.M73DAG.BeamB`，9/9 Success，包含语义屋顶包络/收分矩阵；
- `Saved/Logs/BeamB-Semantic-M7-20260801-2215.log`：精确找到 91 项
  `ABTS.M7`，91/91 Success；
- 种子/Archetype 矩阵覆盖至少六类 Motif，Bridge Volume 强制映射为 BridgeBay；
- 深度 1 与深度 4 保持相同 Motif WFC Hash，同时增加规则步骤和计划构件数量；
- Beam-B 与 Beam-A 调用同一个 `CloseGeneratedAssembly`，不存在第二套收口判定；当前生成域中斜撑数量恒为 0。
- `Saved/Logs/SupportedSpan-BeamB-20260802-1835.log`：精确找到 10 项
  `ABTS.M73DAG.BeamB`，10/10 Success；新增 `SupportedSpanVoid` 覆盖双端支承、显式跨度轴和跨中禁柱；
- `Saved/Logs/SupportedSpan-M7-20260802-1840.log`：精确找到 93 项 `ABTS.M7`，93/93 Success；
- `Saved/Logs/SupportedSpan-ModuleFamily-Build-20260802-1830.log`：使用
  `-ForceUnity -DisableAdaptiveUnity` 完整链接，`Result: Succeeded`。
- `Saved/Logs/BridgeSeat-BeamB-20260802-185754-FreshAutomation.log`：精确找到 11 项
  `ABTS.M73DAG.BeamB`，11/11 Success；新增 `BridgeEndpointBearing` 覆盖桥托模块归属、终端主梁到权威开口边界的实体连接、局部 Bearing 链和桥体禁用落地救援柱。
- `Saved/Logs/BridgeSeat-ForceUnity-20260802-190049.log`：使用 `-ForceUnity -DisableAdaptiveUnity`
  完整链接，`Result: Succeeded`；检测到的可见 Editor 属于其他工作树，未终止、复用或控制。
- `Saved/Logs/BridgeSeat-M7-20260802-190230-FullRegression.log`：精确找到 94 项
  `ABTS.M7`，94/94 Success。
