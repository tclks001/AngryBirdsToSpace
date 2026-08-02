# M7.3-Beam-A v2：分层积木、承托接触 IR 与编辑器预览

> 父级：[长条形积木建筑生成调研与演进方案](M73BeamBlockStructuralGenerationResearch.md)。
>
> 上游：[DAG5-B v2 复杂建筑轮廓预览](M73DAG5Bv2ComplexSilhouettePreviewDesign.md)。
>
> 总导航：[M7 建筑系统文档导航与执行路线](M7BuildingDevelopmentRoadmap.md)。
>
> 状态：v2.1 全局装配收口已完成并通过用户编辑器读形验收；10 项 Beam-A 专项自动化通过。

## 1. v2 修订原因

Beam-A v1 证明了语义 Volume 可以稳定转换成 Bay、Joint、Member 和 Assembly，但它把连接理解成
“多根中心线在同一个端点汇合”，并为每个 Bay 绘制完整方框。结果更像脚手架或钢结构线框，
没有长条积木上下搭放的重量感。

v2 将权威结构语义改成：

```text
固定方形截面、可变长度的 X/Y/Z 长条积木
  + 上下表面的 Bearing Contact
  + 明确的堆放高度和施工顺序
```

Member 的端点只描述自身长度；不同 Member 是否连通由真实上下表面接触决定，不再要求中心线
端点重合。

## 2. 阶段边界

Beam-A v2 实现：

- 消费 DAG5-B v2 的 Volume、Role、Primitive 和局部包围盒；
- 沿较长水平轴有界切分 Structural Bay；
- 所有积木只允许 X、Y、Z 三种方向；
- 所有积木使用同一个 `BlockCrossSectionCM × BlockCrossSectionCM` 截面，长度按 Bay/轮廓变化；
- 高体量采用“主梁 → 正交次梁 → 立柱 → 上主梁 → 正交压梁”的堆放顺序；
- 低矮体量自适应退化为 X/Y 交替的水平积木层，不因放不下完整门架而拒绝；
- Prism/Pyramid 屋顶使用逐层收分、X/Y 交替的水平积木层拟合；
- 从实际 AABB 上下表面重合和 XY 接触面积提取 `BearingContact`；
- 提供确定性 Hash、预算拒绝和 Editor-only 彩色实体长条预览。

Beam-A v2 暂不实现：

- 斜杆、斜屋架、旋转 Brick；
- Bay 内 Motif WFC 和多种结构家族选择；
- 最终离散长度目录、材质、碰撞、Chaos 或弱点；
- 从 Bearing Graph 提取权威 Load DAG；
- TaskGraph 生产切换。

这些目标分别属于 Beam-B、Beam-C 和 Beam-D。

## 3. 数据合同

### 3.1 Bay

`FABTSM73BeamABay` 保存来源 Volume、局部边界、首选主梁方向和 Bay 邻接。它限定结构求解范围，
不是一块最终积木。

### 3.2 Member

`FABTSM73BeamAMember` 保存两个自身端点、`X/Y/Z` 方向、角色和 `LengthCM`。截面来自全局
`BlockCrossSectionCM`，因此同一预览中只有长度变化，另外两条边保持不变。

允许的角色为：

- `PrimaryBeam`：一层中的下部主承梁；
- `SecondaryBeam`：搭在主梁上方的正交次梁；
- `Post`：底面落在次梁上、顶面承托上层梁的竖向积木；
- `RoofCourse`：逐层收分的水平屋顶积木。

### 3.3 Bearing Contact

`FABTSM73BeamABearingContact` 是 v2 新权威连接：

| 类型 | 语义 |
| --- | --- |
| `CrossBearing` | X 梁和 Y 梁上下交叉搭放 |
| `PostOnBeam` | Z 柱底面站在水平梁上 |
| `BeamOnPost` | 水平梁底面搭在柱头上 |
| `ParallelBearing` | 同向积木上下叠放 |

每项记录 Lower/Upper Member、接触中心和接触面积。Bearing Graph 是几何装配图；后续 Load DAG
仍需结合重力、支撑路径和累计荷载重新提取。

### 3.4 Joint 与 Assembly

Joint 退回“Member 自身端点和调试定位”职责，默认不显示。Assembly 将一个 Bay 内的积木组织为
`StackedFrameBay` 或 `LayeredRoofBay`；Member 之间的支撑关系不再由 Joint 冒充。

## 4. 生成算法

### 4.1 普通高体量

```text
Z0  两根 Primary（例如 X）
Z1  两根 Secondary（Y），搭在 Primary 上
Z2  四根 Post，落在 X/Y 交叉承托点
Z3  两根上层 Primary，搭在柱头
Z4  两根上层 Secondary，再次正交压住 Primary
```

每个相邻高度相差一个固定截面厚度，保证上下表面恰好接触而不穿透。

### 4.2 低矮体量

当高度不足以容纳完整上下门架时，生成 2～N 层 X/Y 交替的水平积木。该退化仍拥有
`CrossBearing`，而不是退回实心 Plate、方框或失败。

### 4.3 Prism/Pyramid 屋顶

屋顶不生成斜杆。每层使用 1、3 或 5 根同向水平积木，相邻层交替 X/Y；随高度增加：

- `Pyramid` 同时缩短 X、Y 可用范围；
- `TriangularPrismX` 只沿 X 收分；
- `TriangularPrismY` 只沿 Y 收分。

中心积木保证相邻正交层至少存在一个真实承托交点，外侧积木负责拟合阶梯状轮廓。

### 4.4 接触提取与预算

生成全部 Member 后，以固定截面构造 AABB，按量化 Z 平面匹配 Lower 顶面与 Upper 底面，再计算
XY 重叠面积。超过 `MaxBearingPairChecks` 或 `MaxBearingContactCount` 时原子拒绝，不返回部分图。

### 4.5 v2.1 全局装配收口

单个 Bay 的合法性不能保证多个 Volume/Bay 拼合后仍然合法，因此所有局部装配完成后必须再经过一次
全局几何闭合，最终 Member 图才可被接受：

1. Bridge 的结构范围裁切到相邻主体承托面，避免桥体与主体各自重复铺满同一空间；
2. 同轴且实体截面重叠的 Member 合并为一根，并合并其 Assembly 所有权；
3. X/Y 横条发生正体积相交时，将后生成的整组 course 抬升到下一合法堆叠层；
4. Z 柱穿过新增水平层时，在水平积木上下表面处分段，禁止柱体贯穿梁体；
5. 小于一根积木最短长度的承托缝隙通过抬升上层 course 扩为合法间距；
6. 对仍不可从地面到达的装配岛，优先连接最近的已承重水平层，找不到时补至地面；
7. 只有补柱后成员数和不可达数均不再改善时，才允许裁掉“所属 Assembly 已有其他接地成员”的孤立冗余片；
   整个 Assembly 不可达时不得裁剪绕过，必须 fail closed；
8. 最终重新提取 Bearing Graph，并以“无正体积穿插、所有 Member 均可沿 Bearing 有向边追溯到地面”为硬门槛。

`SupportedSpan` 是上述“不可达就补柱”规则的显式例外，但不是降低稳定门槛。Beam-A 会把跨度中部、从
地面到跨越体底面的空间写入 `ReservedSupportVoids`：全局收口不得在该范围内增加 Z 柱。跨越体只允许
在跨度两端、保留空间外侧生成与相邻承托体对齐的端部支柱；因此最终 Bearing 图仍必须到地，但门洞
中央保持为空。若两端承托不能形成闭合路径，则删除冗余候选或 fail closed，禁止退化为“一旁悬空，
再用一根超长地柱救活”的形态。

收口统计暴露为 `SplitPostMemberCount`、`MergedMemberCount`、`ShiftedCourseCount`、
`GlobalSupportMemberCount`、`PrunedUnsupportedMemberCount`、`RemainingPenetrationCount` 和
`UnsupportedMemberCount`。Accepted 结果的最后两项必须恒为 0。

主要拒绝原因包括：`BeamAHorizontalCourseSeparationFailed`、`BeamAGlobalSupportBudgetExceeded`、
`BeamAUnsupportedMembers`、`BeamAMemberPenetration` 和 `BeamAGlobalAssemblyPassBudgetExceeded`。
这些检查仍属于确定性几何装配，不使用 Chaos，也不替代 Beam-D 的动态稳定认证。

该收口通过内部复用入口 `ABTSM73BeamA::CloseGeneratedAssembly` 暴露给下游；逐层收分屋顶通过
`ABTSM73BeamA::BuildSemanticRoofMembers` 暴露为同源的预闭合语义构件。Beam-B 将 Box Motif
计划构件与非 Box 语义屋顶编译回同一套 `Joint / Member / Assembly` IR 后调用收口入口，因此 Beam-A 基线与 Beam-B
结构家族不存在两套不同的穿透或地面可达判定。

## 5. 编辑器预览

Actor：`M7.3 Beam-A Structural IR Preview`。

| 颜色 | 含义 |
| --- | --- |
| 红 | X 向积木 |
| 绿 | Y 向积木 |
| 蓝 | Z 向积木 |
| 白 | Member 端点 Joint；默认隐藏，仅供诊断 |

预览积木的截面直接使用 `BlockCrossSectionCM`，不再使用独立表现厚度。预览永久无碰撞、无
Overlap、无导航影响，并在 PIE/游戏中隐藏。

主要参数：

- `Silhouette.*`：上游轮廓、Seed、GrammarDepth 和 Archetype；
- `TargetBaySpanCM`：控制长体量的 Bay 数；
- `BlockCrossSectionCM`：全部长条积木的固定截面；
- `MaxRoofCourseCount`：屋顶最多堆放层数；
- `RoofBlocksPerCourse`：宽度允许时每层的奇数根水平积木；
- Bearing/Member/Joint/Bay 预算：有界生成硬门槛；
- `bShowJoints`：默认关闭，开启后只显示 Member 端点，不代表实体连接件。

## 6. 自动化验收合同

过滤器：`ABTS.M73DAG.BeamA.`，当前共 10 项。

- `Determinism`：同输入的 Bay、Member、Bearing、Assembly 和 Hash 完全相同；
- `ArchetypeCoverage`：四类轮廓均接受，拥有 X/Y/Z、Bearing，且 Diagonal 恒为 0；
- `ReferentialIntegrity`：全部端点、Member、Bearing、Assembly 引用有效；
- `StackedBlockSemantics`：必须同时出现 CrossBearing、PostOnBeam、BeamOnPost 和至少三种长度；
- `BudgetFailure`：预算不足稳定拒绝且不泄漏部分 Bearing 图；
- `InvalidSettings`：非法截面或生成参数 fail closed。
- `GlobalAssemblyClosure`：独立重算四类建筑的 Member AABB 与地面可达图，要求无正体积穿插且
  每一根 Member 都能沿 Bearing 链追溯到地面；
- `ParallelCourseSpacing`：平行积木满足数量、最小间隙和两根合一阈值；
- `AdjacentBayBoundarySpacing`：相邻 Bay 公共边界按实际 course 间距退让；
- `ParallelZSupportPlacement`：Z 柱直接消费水平积木位置，并覆盖 X-Y、X-X/Y-Y 对齐承托。
- 跨阶段 `ABTS.M73DAG.DAG5Bv2.SupportedSpanContract`：跨越体必须有两个不同模块族的对向承托体
  和非空净开口；Beam-B 的 `SupportedSpanVoid` 再验证保留空间内不得补入 Z 柱。

## 7. 用户编辑器验收

无需进入 PIE，在空白编辑器地图或平面测试场拖入 Preview Actor：

1. `bShowJoints=false` 时不应再出现白色脚手架接口；
2. 红色 X 积木与绿色 Y 积木应位于不同高度，清楚显示谁搭在谁上；
3. 蓝色柱底应落在水平梁上，柱头上方应承托另一层水平梁；
4. 所有积木只有 XYZ 三种方向，不应出现任何斜杆；
5. Prism/Pyramid 屋顶应呈阶梯式逐层收分，而不是三角斜杆；
6. 调整 `BlockCrossSectionCM` 时所有积木的两条短边同步变化，长度仍随轮廓独立变化；
7. Details 中 Accepted 为真、BearingContactCount 大于 0、DiagonalMemberCount 等于 0；
   `RemainingPenetrationCount` 与 `UnsupportedMemberCount` 均为 0；
8. `SupportedSpan` 下方应能读出完整门洞；两端与相邻主体相接，跨中不得出现补到地面的长柱；
9. 进入 PIE 后预览不可见，也不参与启动物理 Gate。

本阶段仍不要求预览在 Chaos 中站立。它验证的是“搭放拓扑和承托接触”，真实积木、摩擦、
沉降和破坏认证属于 Beam-D。

## 8. 自动化证据

### v1 历史基线

- `Saved/Logs/BeamA-20260731-175340-ForceUnity-Build.log`：ForceUnity 编译成功；
- `Saved/Logs/BeamA-20260731-175141-FreshAutomation.log`：v1 专项 5/5；
- `Saved/Logs/M7-20260731-175253-BeamA-FullRegression.log`：v1 完整 M7 77/77。

### v2 当前证据

- `Saved/Logs/BeamAv2-20260731-184128-ForceUnity-Build.log`：
  ForceUnity Development Editor 全链接，`Result: Succeeded`；
- `Saved/Logs/BeamAv2-20260731-183628-FreshAutomation.log`：精确找到 6 项，6/6 Success；
- `Saved/Logs/M7-20260731-183900-BeamAv2-FullRegression.log`：
  精确找到 78 项 `ABTS.M7` 测试，78/78 Success。

首轮 v2 专项曾发现低矮 Volume 放不下完整门架而被错误拒绝；最终逻辑改为合法的 X/Y 交替
水平层退化，再次专项验证通过。

### v2.1 全局装配收口证据

- 2026-08-01 Development Editor 完整链接：`Result: Succeeded`；构建时检测到的 Unreal Editor
  均属于其他工作树，使用 `-NoHotReload -NoHotReloadFromIDE`，未终止或复用其他工作树进程；
- `Saved/Logs/BeamA-GlobalClosure-Guarded-20260801-172128.log`：精确找到 10 项
  `ABTS.M73DAG.BeamA` 自动化，10/10 Success；
- `Saved/Logs/M7-GlobalClosure-Guarded-20260801-172227.log`：精确找到 82 项
  `ABTS.M7` 自动化，82/82 Success；
- `GlobalAssemblyClosure` 对四类轮廓独立重算 AABB 穿插和 Bearing 地面可达性，全部通过。
- 2026-08-01 用户编辑器读形验收确认：预览中已无明显悬空、横穿或大块组件重叠，Beam-A v2.1
  阶段关闭；后续结构家族差异转入 Beam-B。
