# M7.3-Beam-A：结构 Bay、梁装配 IR 与编辑器预览

> 父级：[长条形积木建筑生成调研与演进方案](M73BeamBlockStructuralGenerationResearch.md)。
>
> 上游：[DAG5-B v2 复杂建筑轮廓预览](M73DAG5Bv2ComplexSilhouettePreviewDesign.md)。
>
> 总导航：[M7 建筑系统文档导航与执行路线](M7BuildingDevelopmentRoadmap.md)。
>
> 状态：代码、强制 Unity 编译和自动化已完成；等待用户编辑器读形验收。

## 1. 阶段目标

Beam-A 建立从“语义轮廓”到“结构数据”的第一条确定性桥梁：

```text
DAG5-B v2 semantic Volume
  -> bounded Bay decomposition
  -> Joint + Member + Assembly
  -> editor-only colored beam preview
```

本阶段回答三个问题：

1. 复杂 Volume 是否可以拆成尺寸受控、带邻接关系的 Structural Bay；
2. 是否可以用统一 `Joint / Member / Assembly` 表示 X/Y/Z 三向梁、立柱和屋顶斜杆；
3. 同 Seed、同参数是否得到完全相同的结构图，预算不足时是否原子拒绝。

## 2. 明确边界

Beam-A 实现：

- 消费已接受的 DAG5-B v2 Volume、Role、Primitive 和局部包围盒；
- 沿较长水平轴按 `TargetBaySpanCM` 有界切分 Bay；
- 建立 Bay 邻接图；
- 为盒体 Bay 生成柱—梁框架，为 Prism/Pyramid 屋顶生成檩边、屋脊和斜椽；
- 将 Bridge Role 保持为桥式框架，不把桥体误当成屋顶；
- 合并同位置 Joint、去重同端点 Member；
- 生成 `BayGraphHash` 和 `BeamGraphHash`；
- 提供无碰撞、无物理、PIE 隐藏的编辑器程序化网格预览。

Beam-A 不实现：

- Bay 内 Motif WFC 或“哪一种门架更合适”的结构选择；
- 梁图递归扩展、跨度优化、冗余选择和弱点设计；
- Load DAG、累计荷载、重心、支撑域或真实接触图；
- 最终离散长度木条、真实 Brick Actor、碰撞或 Chaos；
- TaskGraph 生产默认切换。

这些边界分别由 Beam-B、Beam-C 和 Beam-D 接管。

## 3. 数据合同

### 3.1 Bay

`FABTSM73BeamABay` 保存 `BayId`、`SourceVolumeId`、局部包围盒、首选主梁轴和相邻 Bay。
Bay 是结构求解的有限域，不是最终积木。

### 3.2 Joint

`FABTSM73BeamAJoint` 保存稳定 ID、局部坐标和语义角色。位置在合并容差内相同的节点必须共享
Joint ID，以便后续 Assembly 之间真正连通。

### 3.3 Member

`FABTSM73BeamAMember` 只引用两个 Joint，并记录 X/Y/Z/Diagonal 轴向和 Post、PrimaryBeam、
SecondaryBeam、RoofRafter、RoofRidge 语义。Member 仍是结构候选，不等于一块最终物理木条。

### 3.4 Assembly

`FABTSM73BeamAAssembly` 把一个 Bay 内的 Joint/Member 组织成 Post-and-Lintel、CrossBeam、
RoofFrame 或 BridgeFrame。Beam-B 将在此边界内扩展结构 Motif，而不是在无限体素空间运行 WFC。

## 4. 生成算法

1. 先运行 `FABTSM73DAG5BShapeGrammarV2`；轮廓未接受则不创建任何 Beam 数据。
2. 每个 Volume 选择 X/Y 中较长轴，以 `ceil(span / TargetBaySpanCM)` 切成 1～`MaxBaysPerVolume` 个 Bay。
3. 通过面接触和正交方向重叠建立无向 Bay 邻接。
4. Box/Bridge Bay 建立四角柱和顶部 X/Y 梁；屋顶 Bay 建立底框及 Pyramid 斜椽或 Prism 屋脊。
5. 按位置量化键合并 Joint，按无向端点对去重 Member。
6. 超出 Bay/Joint/Member 任一预算时清空所有中间结果并返回稳定拒绝原因。
7. 对规范序列计算 CRC 身份，供确定性回归测试和后续候选档案使用。

## 5. 编辑器预览

Actor：`M7.3 Beam-A Structural IR Preview`。

颜色约定：

| 颜色 | 含义 |
| --- | --- |
| 红 | X 向构件 |
| 绿 | Y 向构件 |
| 蓝 | Z 向立柱 |
| 金 | 屋顶、斜向构件 |
| 白 | Joint，可用 `bShowJoints` 隐藏 |

预览 Actor 永久关闭碰撞、Overlap 和导航影响，并在 PIE/游戏中隐藏。它只用于确认结构数据是否
覆盖轮廓以及三维梁向是否可读，不得把预览网格当作物理建筑。

可调参数：

- `Silhouette.*`：直接控制上游 v2 轮廓；
- `TargetBaySpanCM`：越小，单个 Volume 被拆成越多 Bay；
- `MaxBaysPerVolume / MaxBayCount / MaxJointCount / MaxMemberCount`：硬预算；
- `JointMergeToleranceCM`：共享接点的几何合并容差；
- `MemberThicknessCM / JointSizeCM`：只影响预览外观，不改变 IR。

## 6. 自动化验收合同

自动化过滤器：`ABTS.M73DAG.BeamA.`。

必须覆盖：

- `Determinism`：同输入所有 IR 记录和图 Hash 相同，Seed 变化改变身份；
- `ArchetypeCoverage`：四类 v2 Archetype 均能生成 Bay/Assembly，且包含 X/Y/Z/Diagonal；
- `ReferentialIntegrity`：所有 ID、端点、Assembly 引用有效，Bay 邻接对称，Member 非零长；
- `BudgetFailure`：预算不足稳定拒绝且不泄漏部分图；
- `InvalidSettings`：非法参数 fail closed。

## 7. 人工验收

在平面物理测试场或任意空白编辑器地图拖入预览 Actor；不必进入 PIE。

预期：

1. 不再显示实心体量，而是三维彩色梁骨架；
2. X/Y/Z 三向结构可从颜色区分，屋顶有金色斜杆；
3. 调小 `TargetBaySpanCM` 时长体量会出现更多连续 Bay，而建筑总轮廓保持不变；
4. 改变 `Archetype / GrammarDepth / Seed` 时，轮廓复杂度变化会传递到 Bay 数和骨架拓扑；
5. Details 中 `Last Preview Summary` 为 Accepted，图 Hash 非零；
6. 进入 PIE 后该预览不可见，且不会产生碰撞、重力或启动物理 Gate 记录。

这项验收不要求骨架当前能够站立，也不要求出现多种弱点；那是 Beam-B～D 的正式目标。

## 8. 自动化证据（2026-07-31）

- `Saved/Logs/BeamA-20260731-175340-ForceUnity-Build.log`：
  `Development Editor -ForceUnity -DisableAdaptiveUnity -NoHotReload`，`Result: Succeeded`；
- `Saved/Logs/BeamA-20260731-175141-FreshAutomation.log`：
  fresh NullRHI 找到 5 项 Beam-A 测试，5/5 Success；
- `Saved/Logs/M7-20260731-175253-BeamA-FullRegression.log`：
  fresh NullRHI 精确找到 77 项 `ABTS.M7` 测试，77/77 Success。

首次专项运行曾由 `ArchetypeCoverage` 发现屋顶斜椽按最大分量被误分类成 X/Y；最终实现已改为
“同时跨越两个以上坐标轴即为 Diagonal”，修正后专项与完整回归均通过。
