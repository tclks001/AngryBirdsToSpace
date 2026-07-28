# M7.3-B2：结构弱点与失效验证实现设计

> 状态：Legacy 顶部冠段代码与独立自动化已通过，现仅作历史对照；TaskGraph 生产已由 [M7.3-DAG2.3](M73DAG23CumulativeLoadAndJointSupportDesign.md) 取代，不再以本稿作为球面验收或回退路线。
>
> 父级：[M7.3 程序化模块化建筑总体算法](M73ProceduralModularBuildingGenerationResearch.md)。前置：[M7.3-B 弱点与难度](M73BWeakPointAndDifficultyDesign.md)。导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M7.3-A 稳定建筑](M73AStableBlockBuildingImplementationDesign.md) · [M7.3-DAG 递归主体建筑新路线](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md)

> 定位更新：本稿实现的是加在现有主体最高处的局部冠段失效，自动化也只要求 `Carrier + 两个 Payload` 三个节点受影响；它不证明整栋主体会坍塌。该实现与测试保留为 Legacy 对照。主体中下部承载瓶颈、递归图语法和主体级联的新设计见 [M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)。

## 1. 为什么需要 B2

M7.3-B 已能从支撑 DAG 中找出高影响节点、计算失撑质量、选择弱点材料并控制难度，但旧基础轮廓仍有两个物理问题：

- 四角柱上下完全对齐。破坏其中一角后，其余三角往往仍能让楼体保持站立；
- 破坏整层楼板后，上方体量容易沿竖直方向下落，并被下方同构柱列重新承接。

因此，“节点从 Ground 路径断开”不等于“真实刚体会发生可见倾覆”。B2 不再从普通四柱建筑中事后挑一块砖换色，而是在支撑边最终构建前，主动增加一段具有明确失效意图的顶部结构，再验证：

1. 弱点未破坏时，载荷重心位于完整接触支撑域内；
2. 弱支撑移除后，载荷重心越过剩余支撑域边界；
3. 上部体量不会高概率原地垂直落座并恢复原结构；
4. 弱点仍满足 M7.3-B 的射界、材料成本和难度窗口。

本阶段的 Gameplay 目标是：玩家第一次正确攻击必须获得可见的结构进展，表现为上部体量倾斜、滑移、下落或引发后续碰撞，而不是只让一块砖消失。

## 2. 范围与边界

B2 已实现：

- 三种不依赖装置的结构弱点模板；
- Seed 决定的塔位、弱侧和倾覆方向；
- 一个 Weak Support Candidate、一个 Carrier 和两个石质 Payload；
- 真实材质密度驱动的载荷质量与重心计算；
- 完整/失效后的接触凸包与 `TipMargin`；
- 垂直重新落座风险 `ReseatRisk`；
- 材料改写前后各执行一次失效探针；
- 将结构类型、预测倒塌类型、受影响节点和失效指标写入弱点记录与结果摘要；
- M7.1 平面测试台和正式球面共用同一局部结构结果。

B2 不实现：

- 绳、锁链、炸药桶、弹簧活塞参与的弱点；它们属于 M7.3-C；
- 斜柱、骨骼、焊接或隐藏 Physics Constraint；
- 多个 Carrier 自动归并为一个刚体组；
- 隐藏 World 中的真实鸟刚体批量攻击；
- 多 Seed 搜索、变异、Novelty Archive 和 TaskGraph 难度分配；它们属于 M7.3-D；
- 用图探针替代最终 Chaos 击打验收。

## 3. 生成链路

```text
基础轮廓 StructureBuilder
-> 检查基础 BrickBudget
-> WeaknessStructureBuilder 添加顶部弱点结构段
-> 检查带弱点后的 BrickBudget
-> FinalizeBoundsAndSupports 统一构建支撑边
-> GroundAdapter / Foundation
-> PostFailureValidator 验证初始稳定与移除后失效
-> WeakPointPlanner 结合射界、材料成本和难度窗口选点/换料
-> 换料后重新执行 PostFailureValidator
-> StabilityValidator / Editor Preview / Runtime Module
```

关键实现职责：

| 类型 | 职责 |
| --- | --- |
| `FABTSM73WeaknessStructureBuilder` | 解析模板、选择 Bay、生成支撑、Carrier 和 Payload，并写入结构意图 |
| `FABTSM73StructuralWeaknessIntent` | 保存 Candidate、Carrier、直接支撑、Payload、倾覆方向和预期失效类型 |
| `FABTSM73PostFailureValidator` | 计算载荷闭包、质量/COM、接触凸包、`TipMargin` 与 `ReseatRisk` |
| `FABTSM73WeakPointPlanner` | 把合格的 authored Candidate 纳入 B 阶段射界、材料、评分、强化和难度流程 |
| `AABTSM73StableBuildingActor` | 暴露参数、预览、结果摘要、运行时 Node→Module 映射与日志 |

基础结构预算先于 B2 扩展检查，目的是保留 M7.3-A 已建立的预算回归语义。基础轮廓本身超限仍输出：

```text
BrickBudgetExceeded:Actual:Budget
```

只有增加 B2 段后才超限时输出：

```text
BrickBudgetExceededWithWeakness:Actual:Budget
```

这样 Gatehouse/TwinTowerBridge 五层基础轮廓的旧 `51:50`、`53:50` 诊断不会被 B2 改写或掩盖。

## 4. 三种无装置结构弱点

当 `StructuralWeaknessPattern=Auto` 时，当前轮廓映射固定为：

| 基础轮廓 | B2 模板 | 直接支撑数 | 预期失效 |
| --- | --- | ---: | --- |
| `SingleTower` | `AsymmetricDualSupport` | 2 | `Tip` |
| `Gatehouse` | `CriticalCorner` | 4 | `Tip` |
| `TwinTowerBridge` | `OffsetSeam` | 3 | `SlideAndTip` |

SingleTower 与 TwinTowerBridge 模板添加在选中 Bay 的最高 Deck 之上；Gatehouse 的 `CriticalCorner` 使用两塔共用的顶层铁质门梁作为强承台。后者避免了“绕开玻璃弱柱，直接打掉下面普通木楼板反而更有效”的旁路弱点，也不会让新增支撑与门梁处在同一高度相互穿透。`StructuralWeaknessBayIndex=-1` 时，多塔轮廓仍由 `BuildingSeed` 确定弱点元数据所属 Bay；单塔始终使用 Bay 0。Seed 先产生局部四象限 `Corner`；对于多塔轮廓，如果弱侧朝向两塔中央，生成器会把它镜像到所选塔的外侧，避免弱点被中心 Connector 遮挡或为了避让 Connector 而失去承台支撑。因此不能把多塔同一轮廓理解为“每个 Pattern 都保留四个最终局部象限”。

### 4.1 `AsymmetricDualSupport`：双支撑择一

- 两根竖向支撑形成一条平行于建筑局部 Y 轴的支撑线；
- Seed 的 `Corner.X` 决定整条支撑线位于局部 `+X` 或 `-X` 一侧，`Corner.Y` 决定两根支撑中的哪一根是 `WeakSupport`；
- `TipDirection` 使用完整的 `Corner` 对角方向，Carrier 与石质 Payload 的组合重心同时向所选 X/Y 弱象限偏移；
- 完整双支撑时 COM 仍在接触域内；
- 弱侧移除后，剩余单侧支撑不足以覆盖 COM，预期向对应弱角象限倾覆。

它解决单塔“四角缺一仍能站立”的问题，并提供清楚的一强一弱视觉语言。当前四个矩阵 Seed 会让 SingleTower 的 `AsymmetricDualSupport` 分别覆盖 `(+X,+Y)`、`(+X,-Y)`、`(-X,+Y)`、`(-X,-Y)` 四个倾覆象限。

### 4.2 `CriticalCorner`：关键角柱

- Carrier 下方有四个角支撑；
- Seed 选择一个角作为 `WeakSupport`；
- Carrier 与 Payload 向该角对角线方向偏置；
- 四角同时存在时保持正支撑裕量；
- 移除关键角后，剩余三角接触凸包不再覆盖载荷 COM，预期向缺角方向倾倒。

这不是在普通对齐四柱中随意换一根玻璃柱。B2 的 Carrier 尺寸、支撑跨距和载荷偏置共同形成经过验证的关键角。

Gatehouse 中四根支撑落在已有铁质门梁顶部。门梁是可碰撞、可受损的真实建筑材料，但其破坏成本高于玻璃弱柱，因此不会成为首击收益更高的旁路目标。

### 4.3 `OffsetSeam`：偏置接缝

- Carrier 下方使用三个不对称支撑点；
- Seed 选择其中一个外角支撑为 `WeakSupport`；
- 另外两个支撑形成偏置接缝；
- 移除弱支撑后，载荷 COM 越过剩余接触域，并具有横向滑移后倾覆的趋势；
- 预测失效类型为 `SlideAndTip`。

该模板用于双塔桥，避免把弱点再次做成“上层沿原柱列竖直下落、下层原位承接”的同构结构。

### 4.4 Payload 的作用

每个 B2 段在 Carrier 上方生成两个石质 Payload：

- 使用真实石材密度参与质量和 COM；
- 强化上部体量的惯性与二次碰撞反馈；
- 让倾覆方向更容易从轮廓中读出；
- 不作为 Weak Point Candidate。

Payload 不是装饰网格；运行时会像其他砖一样进入现有 M7 Chaos、碰撞和损伤链路。

Payload 本身保持轴对齐盒体。沿对角倾覆方向布置时，生成器会按局部 X/Y 最大投影反算最小中心间距，并额外保留 4 cm 间隙，避免“欧氏距离足够但两个轴对齐 AABB 仍穿透”的初始弹飞问题。

## 5. Carrier 必须保持单刚体

当前 B2 每段只能有一个 `CarrierNodeId`，并要求 Carrier 在运行时对应一个完整的 `AABTSM7BuildingModule` 刚体。不得为了外观把它静默拆成多个相互独立的砖 Actor。

原因是失效验证以 Carrier 为结构边界：

- 只收集 `UpperNodeId == CarrierNodeId` 的直接支撑接触；
- 从 Carrier 沿支撑边收集全部上层载荷闭包；
- 以同一个刚体的底面与支撑顶面交集构建 Contact Hull；
- 移除 Candidate 后预测这一整体载荷的倾覆。

若把 Carrier 拆开而不增加“刚体组”语义，可能出现：

- 每块 Carrier 只看到一部分接触域；
- 上层闭包被拆断或遗漏；
- 一块下落、另一块仍由原柱承接；
- 图上 `TipMargin` 合格，Chaos 中却只是局部落座。

未来确需多块外观时，应增加显式 `RigidGroupId` 或复合碰撞体，把多块作为一个质量、一个 COM、一个接触域和一个运行时刚体处理；不能只靠视觉拼接冒充单 Carrier。

## 6. Contact Hull、COM 与 TipMargin

### 6.1 载荷闭包和质量

从 Carrier 开始沿 `LowerNodeId -> UpperNodeId` 支撑边收集后代，得到 `AffectedNodeIds`。每个节点使用真实尺寸和 M7 Material Profile 密度估算质量：

```text
NodeMass = Dimensions.X * Dimensions.Y * Dimensions.Z * Density
LoadCOM  = Σ(NodeCenter * NodeMass) / Σ(NodeMass)
```

这里使用最终碰撞尺寸和材质密度，不按砖块数量投票。弱点换料或非弱点强化会改变质量，因此换料完成后必须重新计算。

### 6.2 接触凸包

对每个“直接支撑 → Carrier”支撑边：

1. 取下层支撑 AABB 与 Carrier AABB 在局部 XY 平面的真实交集矩形；
2. 收集交集矩形的四个角；
3. 对全部角点构建二维 Convex Hull。

得到两套支撑域：

- `FullContactHull`：包含 Candidate 的完整支撑；
- `RemainingContactHull`：移除 Candidate 后的剩余支撑。

`InsideMargin(COM, Hull)` 使用到凸包边界的最近距离并带符号：凸包内为正，凸包外为负。

### 6.3 初始支撑裕量

```text
InitialSupportMarginCM = InsideMargin(LoadCOM, FullContactHull)
```

必须满足：

```text
InitialSupportMarginCM >= MinInitialSupportMarginCM
```

它防止 B2 为了制造弱点而直接生成空载就会倾倒的建筑。

### 6.4 倾覆裕量

```text
TipMarginCM = -InsideMargin(LoadCOM, RemainingContactHull)
```

- `TipMarginCM > 0`：移除弱支撑后 COM 已位于剩余接触域外；
- 数值越大：COM 越过支撑边界越远，倾覆趋势越明确；
- `TipMarginCM <= 0`：COM 仍被剩余支撑覆盖，旧的“打掉一块仍能站住”风险仍在。

默认要求：

```text
TipMarginCM >= MinTipMarginCM
```

`TipMargin` 是确定性几何裕量，不是 Chaos 倒塌时间，也不包含碰撞反弹、摩擦卡住和其他未建模接触。

完整或剩余 Contact Hull 少于三个有效凸包点时会直接拒绝，不允许用 `BIG_NUMBER` 形式的伪大 `TipMargin` 假通过。探针还会比较“剩余支撑域中心 → 载荷 COM”的实际越界方向与 Seed 声明方向，点积低于 `0.25` 时以方向不匹配拒绝。

## 7. ReseatRisk：防止上层原位承接

仅让 COM 离开当前接触域仍不够。上部体量下落后，可能被更低一层同位置构件重新接住。当前 `EstimateVerticalReseatRisk` 执行以下近似：

1. 找出 `AffectedNodeIds` 中最低的一组刚体；
2. 排除被移除 Candidate 和整个 Falling Set；
3. 在其下方寻找最高的可接触落座层；
4. 用真实 XY 接触交集重建 Landing Hull；
5. 检查 Falling Set 的整体 COM 是否仍落在 Landing Hull 内。

当前 `AlignmentRisk` 是确定性二值代理：能原位承接为 1，否则为 0。再用倾覆裕量降低风险：

```text
TipFeasibility = clamp(
    TipMarginCM / max(1, 2 * MinTipMarginCM),
    0,
    1)

ReseatRisk = AlignmentRisk * (1 - TipFeasibility)
```

必须满足：

```text
ReseatRisk <= MaxReseatRisk
```

这不是统计概率。它是 B2 的快速筛选指标：对齐落座且倾覆裕量小的结构风险高；没有下层承接面，或倾覆裕量足够大的结构风险低。真实摩擦、旋转、碰撞和二次撞击仍由 M7.1 Chaos 验收，后续 M7.3-D 才可用多次 rollout 形成统计风险。

## 8. 与 M7.3-B 选点和难度的关系

B2 不替代 B，而是给 B 提供物理含义更强的 authored Candidate。

默认 `bRequireAuthoredStructuralWeakness=true` 时：

- 只允许通过 B2 失效探针的 Candidate 进入最终弱点选择；
- 旧对齐楼板即使图上失撑质量很高，也不能冒充已验证的结构弱点；
- `AffectedMassRatio` 取代 authored Candidate 的普通 Ground 失联比例；
- `TipMargin` 与 `(1 - ReseatRisk)` 参与候选得分；
- 最终弱点记录保存 `AffectedNodeIds`、模板、倒塌类型和三项失效指标。

弱点材料仍由 M7.3-B 的真实 Profile 层级决定：

- `bAutoSelectWeakPointMaterial=true` 时按 `TargetBirdHits` 选择成本层级；
- 默认 Profile 和 `TargetBirdHits=1` 通常选中 Glass；
- 关闭自动选择后使用 `WeakPointMaterial`；
- 不增加隐藏弱点伤害倍率。

选点和非弱点强化完成后必须再运行一次 B2 Probe。若换料导致 COM、初始裕量、倾覆裕量或重新落座风险越界，整个规划事务拒绝，不保留半完成的材料改写。

## 9. 编辑器参数

选中 `M7.3 Stable Building Generator`，主要参数位于 `GenerationSettings` 与 `DifficultySettings`。

### 9.1 `GenerationSettings -> Weakness Geometry`

| 参数 | 当前默认 | 作用 |
| --- | ---: | --- |
| `bGenerateStructuralWeakness` | `true` | 是否在基础轮廓上增加 B2 结构段 |
| `StructuralWeaknessPattern` | `Auto` | 自动按轮廓映射，或强制指定三种模板之一 |
| `StructuralWeaknessBayIndex` | `-1` | 多塔轮廓的目标塔；`-1` 由 Seed 确定 |
| `WeaknessFootprintRatio` | `0.62` | Carrier 相对所选顶部 Deck/强门梁承台的 XY 尺寸比例 |
| `WeaknessSupportHeightCM` | `110` | B2 支撑柱高度，单位 cm |
| `WeaknessBiasRatio` | `0.72` | Carrier/载荷 COM 朝弱侧的偏置比例 |
| `WeaknessTipReserveCM` | `5.0` | 比例偏置之后沿预测倾覆方向追加的绝对 COM 安全余量，单位 cm |
| `WeaknessPayloadHeightCM` | `130` | 两个石质 Payload 的高度，单位 cm |

调整建议：

- 先使用 `Auto` 验收三种默认模板，不要一开始交叉组合所有轮廓和模板；
- `WeaknessBiasRatio` 越大，通常 `TipMargin` 越大，但 `InitialSupportMargin` 会减小；
- `WeaknessTipReserveCM` 用于吸收木/石/铁 Carrier 密度改变造成的 COM 差异，不应代替比例偏置；修改后仍需同时检查完整支撑裕量；
- 缩小 `WeaknessFootprintRatio` 会改变接触域、支撑跨距和顶部轮廓，必须重新检查穿透与静稳；
- 增大 Payload 会提高上部质量和二次碰撞，但也会改变整栋失效质量比；
- B2 会额外消耗砖预算：双支撑模板 5 块、偏置接缝 6 块、关键角柱 7 块。

### 9.2 `DifficultySettings -> B2 Failure Validation`

| 参数 | 当前默认 | 作用 |
| --- | ---: | --- |
| `bRequireAuthoredStructuralWeakness` | `true` | 拒绝只通过旧图失联、没有 B2 几何意图的弱点 |
| `MinInitialSupportMarginCM` | `2` | 完整支撑时 COM 位于接触凸包内的最小距离 |
| `MinTipMarginCM` | `8` | 移除 Candidate 后 COM 越过剩余支撑域的最小距离 |
| `MaxReseatRisk` | `0.35` | 允许的最大垂直重新落座代理风险 |

这些是硬门槛，不是单纯评分权重。生成被拒绝时应先查具体指标和几何，不要直接把所有阈值降为 0。

### 9.3 仍由 M7.3-B 控制的参数

- `WeakPointCount`：当前每个 B2 段只有一个 authored Candidate，首轮保持 1；
- `MinWeakCollapseRatio` / `MaxSingleWeakCollapseRatio`：受影响真实质量比例窗口；
- `MinWeakPointExposure`：从 `AttackDirection` 的可命中程度；
- `TargetBirdHits` / `WeakPointMaterial`：材料成本；
- `MaxNonWeakEffect` / `MinWeakPointAdvantage`：弱点与普通攻击差异；
- `bReinforceNonWeakCriticalNodes`：非弱点关键节点的有限强化；
- `bShowWeakPointDebug` / `WeakPointDebugScale`：红色无碰撞预览。

`AttackDirection` 箭头必须与鸟从弹弓飞向建筑的方向同向，而不是从建筑指回弹弓。

### 9.4 最终默认基线与参数矩阵

使用 `BuildingSeed=7301`、默认尺寸、默认 M7 Material Profile、默认难度门槛和 `StructuralWeaknessPattern=Auto` 时，最终换料后 Failure Probe 的实测基线为：

| 轮廓 | Auto 模板 | `InitialSupportMarginCM` | `TipMarginCM` | `ReseatRisk` |
| --- | --- | ---: | ---: | ---: |
| `SingleTower` | `AsymmetricDualSupport` | `24.428` | `56.174` | `0.000` |
| `Gatehouse` | `CriticalCorner` | `39.734` | `15.755` | `0.000` |
| `TwinTowerBridge` | `OffsetSeam` | `32.876` | `14.118` | `0.000` |

以上数值是当前默认实现的防回归基线，不是建议把验证写成精确浮点相等；正式硬门槛仍为 `InitialSupportMarginCM>=2`、`TipMarginCM>=8`、`ReseatRisk<=0.35`。

自动化测试 `ABTS.M73B2.ParameterMatrix` 已扩展并通过 `192` 组组合：

```text
4 Seeds（7301 / 7302 / 7303 / 7310）
x 3 Silhouettes
x 4 PrimaryMaterials（Wood / Stone / Iron / Glass）
x 4 Levels（1–4）
= 192 cases
```

四个 Seed 明确覆盖 SingleTower / `AsymmetricDualSupport` 的四个弱角象限。Gatehouse 与 TwinTowerBridge 还会把朝向中央的弱侧镜像到所选塔外侧，所以它们验证的是“多 Seed、多 Bay 选择下始终外向、可攻击且有承台支撑”，而不是要求同一多塔轮廓保留四个最终局部象限。每组均要求 Builder、Planner、Static Stability Validator 和最终 Failure Probe 成功，并重新检查三个 B2 硬门槛。该矩阵是纯数据确定性回归，不包含 Chaos、实际 Static Mesh Collision、Pivot 或鸟的真实冲量，因此不能替代下节的 M7.1 空载与击打验收。

## 10. M7.1 实际击打步骤

### 10.1 场景准备

1. 完成编译后打开 M7.1 平面物理测试地图。
2. 确认 World Settings 使用 M7.1 Physics Test GameMode，场景中已有 Test Stage、Bird Player Start 和可用弹弓。
3. 放置或选中 `M7.3 Stable Building Generator`。
4. 保持 Actor Scale 为 `(1,1,1)`；尺寸只通过 `GenerationSettings` 调整。
5. `GroundMode=Auto`，默认关闭 `bSnapPlanarAnchorToTestStage` 以便自由放置；需要自动贴地时再打开。
6. 旋转 Actor，使 `AttackDirection` 箭头与弹丸飞行方向一致。
7. 首轮保持 B2 默认参数，依次测试 `SingleTower`、`Gatehouse` 和 `TwinTowerBridge`。

### 10.2 编辑器预览

每种轮廓都应满足：

- 顶部出现与模板一致的弱点段，而不是普通四柱无限向上重复；
- 红色弱点覆盖落在 `WeakSupport` 上，不在 Carrier、Payload 或 Foundation 上；
- `GenerationSummary.bAccepted=true`，`RejectReason` 为空；
- `PrimaryWeakPointNodeId` 有效；
- B2 Pattern、Collapse Mode、`TipMargin` 和 `ReseatRisk` 与当前模板一致；
- 建筑、FoundationCap 和 FoundationFeet 可整体沿 XYZ 移动，不被强制拉回测试台；
- 砖块之间无穿透，空载时不倾倒。

### 10.3 对照击打

为避免用不同初速得出错误结论，采用“同 Seed、同鸟、同拉力、重启 PIE”的 A/B 对照：

1. PIE 后等待空载 Chaos 验证完成，确认建筑未自发移动。
2. 第一轮攻击普通非弱点柱/墙，记录移动、损伤和最终结构状态。
3. 退出并重新 PIE，保持同一 Seed、鸟种和近似拉力。
4. 第二轮攻击红色覆盖对应的真实弱支撑。
5. 弱支撑破坏后，`AffectedNodeIds` 对应的 Carrier 与 Payload 应产生明显倾斜、滑移或下落。
6. 上部不得只是垂直下沉一层后恢复原来的稳定四柱结构。
7. 普通攻击仍可推动和累积损伤，但第一次正确弱点攻击应产生明显更大的结构进展。
8. FoundationCap/Feet 始终静态；发射前不应自发倒塌；发射后未破坏砖继续走现有 M7 Chaos 链路。

三种模板的视觉重点：

- `AsymmetricDualSupport`：向弱侧倾覆；
- `CriticalCorner`：从缺角方向失稳；
- `OffsetSeam`：先横向错动，再倾覆或撞击相邻结构。

若实际方向与预测不同，但仍产生明显、可玩的结构进展，记录实际轨迹供 M7.3-D rollout 评分；不要为了强行对齐预测方向加入隐藏焊接。

## 11. 日志与结果摘要

应重点检查以下三层信息：

1. `GenerationSummary`：是否接受、Weak Node、Pattern、Collapse Mode、`PrimaryTipMarginCM`、`PrimaryReseatRisk`；
2. M7.3-B 弱点/难度日志：受影响质量、射界、材料层级、得分和非弱点效果；
3. M7.3-A 空载日志：穿透、漂移、沉降、旋转和最终 Accepted。

为兼容既有排错检索，运行时仍使用 `[ABTS][M7.3-B][WeakPoint]` 标签；B2 由新增字段识别：

```text
[ABTS][M7.3-B][WeakPoint]
... Pattern=... Collapse=...
InitialMargin=... TipMargin=... Reseat=... Affected=...
```

不要只凭标签中没有 `B2` 就判断新链路未运行。

B2 常见拒绝原因：

```text
B2NoDeckBay
B2NoCarrierBaseDeck:Bay
B2WeakSupportSelectionFailed
B2IntentNodeMissing
B2AffectedMassInvalid
B2FullSupportHullDegenerate
B2RemainingSupportHullDegenerate
B2InitialSupportMarginTooSmall:Actual:Required
B2TipMarginTooSmall:Actual:Required
B2TipDirectionMismatch:ActualDot:RequiredDot
B2ReseatRiskTooHigh:Actual:Required
B2FailureProbeRejected:Node:Reason
B2NoValidAuthoredWeakness
B2FinalFailureProbeRejected:Node:Reason
InsufficientWeakPoints:...:B2Ratio=...:Exposure=...:Tip=...:Reseat=...:Score=...
BrickBudgetExceededWithWeakness:Actual:Budget
```

日志判读原则：

- `InitialSupportMargin` 失败：结构完整时就不稳，优先减小偏置或增大完整支撑域；
- `TipMargin` 失败：移除弱点后仍被剩余支撑覆盖，优先增加合理偏置或调整支撑位置；
- `ReseatRisk` 失败：下方存在同位置承接面，优先改变上下层错位关系，而不是只把弱点换得更脆；
- `Exposure` 失败：先检查 Actor 朝向和 `AttackDirection`，不要先改结构强度；
- `BrickBudgetExceededWithWeakness`：提高预算或降低基础 Levels，不要删除失效验证。

## 12. 验收清单

### 12.1 确定性与数据

- 独立自动化测试 `ABTS.M73B2.StructuralWeaknessFailure` 与覆盖 192 组的 `ABTS.M73B2.ParameterMatrix` 通过；完整建筑回归可运行 `ABTS.M73`；
- `Auto` 对三种轮廓的模板映射正确；
- 相同 Seed 的 Bay、Candidate、Carrier、倾覆方向、尺寸和材料结果一致；
- Structural Weakness Intent 恰好包含有效 Candidate、Carrier、直接支撑和 Payload；
- Candidate 的语义是 `WeakSupport`，不是 Foundation 或普通楼板；
- `AffectedNodeIds` 非空，并包含 Carrier 及其上层载荷；
- 默认弱点使用真实 M7 低成本材质 Profile，不使用隐藏倍率；
- 关闭 `bGenerateStructuralWeakness` 且要求 authored weakness 时，旧对齐结构稳定拒绝。

### 12.2 几何与静稳

- 完整状态 `InitialSupportMarginCM >= MinInitialSupportMarginCM`；
- 移除弱支撑后 `TipMarginCM >= MinTipMarginCM`；
- `ReseatRisk <= MaxReseatRisk`；
- 三种模板无砖块穿透；
- 空载 Chaos 验证不出现自发弹起、滑移或倾倒；
- Carrier 在运行时保持单刚体语义。

### 12.3 Gameplay

- 正确攻击弱点的第一次命中产生可见结构进展；
- 非弱点攻击仍有物理反馈，但效果明显较低；
- 上部不会稳定地竖直落座回原结构；
- 坍塌过程保留推动、倾斜和二次碰撞，不是整栋同时消失；
- 平面与球面同 Seed 保持相同局部结构和弱点结果。

### 12.4 回归

- M7.3-A 三种基础轮廓静态校验继续通过；
- Gatehouse/TwinTowerBridge 五层基础预算诊断仍为 `51:50` / `53:50`；
- 不可能的难度窗口事务式失败，不留下材料和弱点标记；
- Editor Preview 无碰撞，Runtime 只保留真实 Module 碰撞；
- M7.1 手工砖、弹弓、炸药桶和活塞原测试功能不受影响。

## 13. 排错表

| 症状 | 可能根因 | 处理 |
| --- | --- | --- |
| 三种轮廓都没有弱点段 | `bGenerateStructuralWeakness=false`，或基础结构在进入 B2 前已超预算 | 打开开关；检查 `RejectReason` 区分基础预算和 B2 预算 |
| 建筑完整时就向一侧倒 | 偏置过大、完整接触域过小，或 Payload 过重 | 恢复默认参数；检查 `InitialSupportMargin`；再检查实际碰撞尺寸 |
| 弱支撑破坏后仍站立 | `TipMargin<=0`、存在未建模旁路接触，或 Mesh Collision 比生成 AABB 大 | 检查 Probe；核对 Pivot、Simple Collision 和 Runtime Scale；不要只看预览网格 |
| 上层下沉后重新站稳 | 下方 Landing Hull 仍覆盖 Falling COM | 检查 `ReseatRisk`；采用错位接缝/调整支撑关系，不要只降低弱点耐久 |
| 红框正确但击中 Carrier | 发射方向、模型 Pivot 或碰撞体与预览尺寸不一致 | 校准 `AttackDirection`、Mesh Pivot、Simple Collision 和实际 Node→Module 位置 |
| Gatehouse 顶部与门梁穿透，或木楼板比弱点更值得攻击 | `CriticalCorner` 错落在塔顶木 Deck，导致支撑与门梁同高，或形成可绕过弱柱的低成本承台 | 保持 Gatehouse 使用顶层铁质门梁作为 B2 强承台；检查门梁顶部与弱支撑底部只接触、不穿透 |
| 两个石质 Payload 在对角方向互相顶开 | 只按沿垂向轴的欧氏间距放置，没有考虑轴对齐 AABB 在 X/Y 上的投影 | 保留按最大轴投影反算的 Payload 间距；若修改尺寸，重新运行穿透与静稳测试 |
| TwinTowerBridge 只差很小距离被拒绝 | `TipMargin` 位于硬阈值边缘，Seed 尺寸扰动改变了接触域 | 不用浮点容差强行放行；小幅调整 Bias/Footprint 后重跑三种轮廓与初始裕量回归 |
| `InsufficientWeakPoints` 且 B2 指标合格 | 射界、质量比例、弱点数量或间距仍被 M7.3-B 拒绝 | 按日志分别检查 Exposure、Ratio、RequestedCount；首轮保持 `WeakPointCount=1` |
| 预览通过、Chaos 中只碎局部 Carrier | Carrier 被美术/运行时拆成多个独立刚体 | 恢复单 Carrier；未来用显式 RigidGroup，而非视觉拼块 |
| 同 Seed 平面与球面指标不同 | 将世界坐标/径向混入局部结构计算，或材质 Profile 来源不同 | B2 只用建筑局部坐标；保证两端使用同一 Profile 集合 |

## 14. 与 M7.3-C / D 的边界

### M7.3-C：装置连锁

C 可以在 B2 已验证的结构段上增加：

- 绳/锁链拉索弱点；
- 炸药桶覆盖多个关键支撑的近毁远推；
- 弹簧活塞把 Carrier 或配重推出支撑域；
- 纯结构弱点与装置弱点的组合预算。

但 C 不能绕过 B2：装置加入后仍要重建接触/约束关系，重新执行空载验证，并验证触发后 COM 确实离开有效支撑域。炸药桶或活塞不能用来掩盖一个本身没有失效路径的结构。

### M7.3-D：多样性搜索与 PCG 集成

D 负责：

- 多 Seed、多尺寸、多模板候选搜索；
- 真实 Chaos 弱点/非弱点攻击 rollout 与统计失败率；
- 将 `TipMargin`、`ReseatRisk`、实际倒塌方向、Settling Time 和二次碰撞纳入多目标评分；
- Novelty Archive 和固定 Seed 回归库；
- TaskGraph 的 Anchor、弹弓射界、地形遮挡、道路难度和坍塌安全区集成。

B2 的几何指标是 D 的廉价前置过滤器，不是最终搜索评分的全部。D 只对通过 B2 的少量候选运行昂贵 Chaos 验证。

## 15. 上下游文档

- 主项目阶段状态与总体约束：[AngryBirdsToSpaceGameDesign.md](AngryBirdsToSpaceGameDesign.md)
- 建筑生成总体研究、结构语法和 C/D 路线：[M73ProceduralModularBuildingGenerationResearch.md](M73ProceduralModularBuildingGenerationResearch.md)
- Ground Adapter、施工台、地基脚和稳定建筑：[M73AStableBlockBuildingImplementationDesign.md](M73AStableBlockBuildingImplementationDesign.md)
- 射界、材料成本、强化和难度窗口：[M73BWeakPointAndDifficultyDesign.md](M73BWeakPointAndDifficultyDesign.md)
- 材料 Profile、损伤、爆炸和活塞：[M7BuildingMaterialsAndDevicesDesign.md](M7BuildingMaterialsAndDevicesDesign.md)
- 平面放置、发射与实际击打入口：[M71PlanarPhysicsTestStageDesign.md](M71PlanarPhysicsTestStageDesign.md)
- 推动、累计损伤和结构破坏手感依据：[PhysicsImpactDestructionResearch.md](PhysicsImpactDestructionResearch.md)
- 工程问题沉淀：[DevelopmentTroubleshooting.md](DevelopmentTroubleshooting.md)
