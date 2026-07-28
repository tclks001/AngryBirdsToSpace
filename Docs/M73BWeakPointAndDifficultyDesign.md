# M7.3-B：弱点与难度实现设计

> 状态：Legacy 图选点的 C++、预览和自动化已实现，现仅作历史对照；TaskGraph DAG2.3 不调用本 WeakPointPlanner。正式主体弱点需后续 DAG-3 实现，现行生产链见 [M7 球面集成](M7TaskGraphSphericalBuildingIntegrationDesign.md)。
> 父级：[M7.3 程序化模块化建筑总体算法](M73ProceduralModularBuildingGenerationResearch.md)。前置：[M73AStableBlockBuildingImplementationDesign.md](M73AStableBlockBuildingImplementationDesign.md)。
> B2 扩展：Legacy 顶部结构段、Contact Hull/COM、`TipMargin`、`ReseatRisk` 与防原位承接验收见 [M73B2StructuralWeaknessAndFailureValidationDesign.md](M73B2StructuralWeaknessAndFailureValidationDesign.md)。主体内部弱点和递归支撑 DAG 新路线见 [M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)。项目阶段索引见 [AngryBirdsToSpaceGameDesign.md](AngryBirdsToSpaceGameDesign.md)。

## 1. 阶段目标

M7.3-B 解决的不是“挑一块砖随机换成玻璃”，而是以下 Gameplay 问题：

- 玩家击中被提示的弱点时，建筑会失去一部分真实支撑路径；
- 击中其他位置仍有碰撞、推动和累计损伤，但不应和弱点一样高效；
- 弱点必须从弹弓主要来向可见，不能藏在完全遮挡的位置；
- 弱点不能一次无条件清空整栋，也不能击碎后毫无结构反馈；
- 难度参数必须最终落到现有 M7 材料强度，而不是独立的隐藏伤害倍率；
- M7.1 平面测试台和正式球面只改变承载面与世界变换，不改变同一建筑的支撑图、弱点 NodeId 和材料结果。

本阶段已经实现：

- 对支撑 DAG 中每个 Gameplay 砖执行一次节点移除反事实探针；
- 计算失去 Ground 路径的节点集合和质量比例；
- 以固定射线采样估计主攻击方向暴露度；
- 从真实 M7 Material Profile 推导材料破坏成本层级；
- 选择一个或多个互不重叠、相距足够远的弱点；
- 将弱点分配为目标材料层级，并对其他高影响节点进行有限强化；
- 预测弱点效果、普通攻击效果、效果优势和难度得分；
- 编辑器红色弱点覆盖预览、Details 结果摘要和运行时日志；
- 三种默认轮廓的确定性、难度窗口、材料改写和静态稳定性自动化测试。

本阶段暂不实现：

- 绳、锁链、炸药桶和弹簧活塞弱点，它们属于 M7.3-C；
- 批量候选搜索和 Novelty Archive，它们属于 M7.3-D；
- 在隐藏 World 中自动发射真实鸟刚体的 Chaos 攻击 rollout；
- 根据玩家历史表现动态调难度；
- 弱点 HUD、锁定提示或教程动画。

这里的“攻击探针”是确定性的结构图反事实探针。实际碰撞、倒塌方向和二次连锁仍需在 M7.1 用现有 M6/M7 物理链路验收；不能把图预测等同于最终 Chaos 结果。

B2 已补上本稿原始图探针没有覆盖的物理失效语义：弱点几何在支撑边建立前主动生成，完整时必须静稳，移除后必须具备正 `TipMargin` 且 `ReseatRisk` 不越界。默认开启 `bRequireAuthoredStructuralWeakness` 后，普通对齐楼板不再仅凭 Ground 失联比例被接受为最终弱点。

## 2. 模块边界

| 文件/类 | M7.3-B 职责 |
| --- | --- |
| `ABTSM73BuildingTypes.h` | `EABTSM73WeakPointRole`、`FABTSM73DifficultySettings` 和难度结果摘要 |
| `ABTSM73StructureData.h` | 节点弱点元数据、WeakPoint Record、强化节点和预测结果 |
| `FABTSM73WeakPointPlanner` | Ground 可达性、节点移除、暴露度、评分、选点、换料和难度验证 |
| `FABTSM7MaterialProfileLibrary` | M7 Runtime 与 M7.3 PCG 共用的默认材质 Profile 数据源 |
| `AABTSM7BuildingMaterialSystem::CopyMaterialProfiles` | 把编辑器中实际调整后的 Runtime Profile 复制给规划器 |
| `AABTSM73StableBuildingActor` | 攻击方向输入、编辑器高亮、运行时模块映射、Summary 和日志 |
| `ABTSM73AutomationTests.cpp` | 三种轮廓、材料层级、确定性、失败事务和旧预算回归 |

依赖关系：

```text
StructureBuilder
-> GroundAdapter
-> WeakPointPlanner（只改材料和弱点元数据，不改几何）
-> StabilityValidator
-> Editor HISM Preview / Runtime M7 Module
```

`FABTSM73WeakPointPlanner` 不持有 Actor、World 或 Component。失败时它不会把半成品写回 `FABTSM73StructureData`。

## 3. 输入数据契约

规划器只消费已经通过 M7.3-A 构建的 Gameplay 结构：

```text
Bricks
SupportEdges: LowerNodeId -> UpperNodeId
GroundNodeIds
Material
DimensionsCM
LocalCenter
```

以下对象不参加弱点候选和质量分母：

- `FoundationCap`；
- `FoundationFeet`；
- 球面连续地形；
- 编辑器预览 HISM；
- 空载验证专用对象。

因此不会把不可破坏施工台误选为弱点。若以后允许“打地基”，必须在 FoundationCap 上方生成独立、可破坏的 Gameplay 地基砖。

## 4. Ground Root 反事实探针

### 4.1 为什么不用普通无向割点

建筑有多个 Ground Node，也可能有两块楼板共同支撑一块桥面。把支撑图简单无向化会误判冗余支撑。

M7.3-B 逻辑上建立一个虚拟 `GroundRoot`，它连接所有 Ground Node，然后沿：

```text
LowerNode -> UpperNode
```

向上遍历。

### 4.2 单节点移除

对每个候选节点 `C`：

1. 临时忽略节点 `C`；
2. 忽略所有进入或离开 `C` 的支撑边；
3. 从除 `C` 外的所有 Ground Node 重新向上遍历；
4. 所有未被访问且不等于 `C` 的砖记为 `UnsupportedNodeIds`；
5. 计算这些节点的总质量占整栋建筑质量的比例。

```text
NodeMass = Dimensions.X * Dimensions.Y * Dimensions.Z * MaterialDensity

UnsupportedMassRatio =
    Sum(Mass of unreachable nodes after removal)
  / Sum(Mass of all gameplay bricks)
```

被直接击碎的候选自身不计入 `UnsupportedMassRatio`。这个指标衡量的是“一块局部损失额外带走多少失撑结构”，而不是用大体积砖自身伪造连锁效果。

复杂度为：

```text
O(C * (V + E))
```

默认单栋不超过 50 块，5×5 可见性采样下可直接在编辑器 Construction/Rebuild 使用。

## 5. 攻击方向暴露度

`AttackDirection` 箭头的前向代表“弹丸飞行方向”，不是从建筑看向弹弓的方向。

例如弹弓位于建筑本地 `-X`，鸟从 `-X` 飞向 `+X`，则箭头应指向 `+X`。

规划器在候选砖朝攻击方向的投影范围内生成固定 `5 × 5` 平行射线：

1. 射线从攻击方向反侧的远处发出；
2. 先求射线进入候选 AABB 的距离；
3. 若任何其他 Gameplay 砖在更早距离命中，则该采样被遮挡；
4. `Exposure = 可见采样数 / 实际命中候选的采样数`。

该实现支持任意局部攻击方向，不只支持轴对齐 +X。固定采样、稳定 NodeId 和固定排序保证同一 Seed 可复现。

当前只计算建筑自身遮挡。正式 TaskGraph 放置器以后还要检查地形、其他建筑和不可破坏装饰是否挡住射界；不能在球面放置后偷偷换成另一个 WeakPointId。

## 6. 材料破坏成本

### 6.1 单一 Profile 数据源

M7.3-B 新增 `FABTSM7MaterialProfileLibrary`，并让 `AABTSM7BuildingMaterialSystem` 的默认四材质配置从这里建立。这样 PCG 不再复制一份与 Runtime 脱节的密度或强度常量。

运行时生成时，`AABTSM7BuildingMaterialSystem::CopyMaterialProfiles` 会把实际编辑器调参传给规划器；编辑器无 MaterialSystem 的预览和自动化测试使用同一套共享默认 Profile。

### 6.2 相对破坏成本

材料成本使用：

```text
BreakEffort =
    BreakSpeedCMPerSec
  * BreakDamage
  / max(DamageAtBreakSpeed, 1)
```

四种材料按 `BreakEffort` 从小到大排序，得到 `HitTier = 1..4`。默认 Profile 的结果是：

```text
Glass < Wood < Stone < Iron
```

`TargetBirdHits` 在本阶段表示相对材料难度层级，不是承诺物理世界中严格命中 N 次必碎。真实次数还受鸟种、入射法向速度、累计损伤和二次碰撞影响。

`bAutoSelectWeakPointMaterial=true` 时：

```text
TargetBirdHits = 1 -> 当前最易碎 Profile
TargetBirdHits = 2 -> 第二易碎 Profile
TargetBirdHits = 3 -> 第三层级 Profile
TargetBirdHits = 4 -> 当前最难 Profile
```

如果设计师重排了 Profile 强度，规划器会跟随真实数据，而不是假设枚举顺序永远不变。

## 7. 候选角色与评分

当前砖块候选分为：

- `GroundSupport`：直接接地的柱；
- `VerticalSupport`：非接地竖向承重件；
- `LoadBearingDeck`：楼板或横梁；
- `BridgeConnector`：明显的长跨连接件。

评分为：

```text
WeakPointScore = clamp01(
    UnsupportedMassRatio
  * Exposure
  * Readability
  * (0.40 + 0.60 * TargetCollapseFit)
  / HitTier)
```

`Readability` 当前由结构角色和弱点材料与原材料的视觉反差近似；楼板/承重梁最高，普通竖柱略低。后续有正式模型和轮廓标签后，可把留白、材质明度差和装置图标加入同一分项。

候选必须同时满足：

```text
MinWeakCollapseRatio
<= UnsupportedMassRatio
<= MaxSingleWeakCollapseRatio

Exposure >= MinWeakPointExposure
```

多弱点还必须满足：

- 弱点中心距离不小于 `MinWeakPointSeparationCM`；
- 两个弱点的失撑子图重叠不高于 `MaxWeakPointAffectedOverlap`；
- 最终实际选中数量达到 `WeakPointCount`。

默认简单结构以一个弱点为目标。把 `WeakPointCount` 提高到 2 或 3 后收到 `InsufficientWeakPoints` 是合法的生成拒绝，不是渲染错误；说明当前轮廓没有足够独立的攻击解。

## 8. 非弱点强化与难度对照

选中弱点后：

1. 弱点改为自动或手工指定材料；
2. 其他 `UnsupportedMassRatio >= ReinforcementImpactThreshold` 的高影响节点按影响从高到低筛选；
3. 最多强化 `MaxReinforcedNodeCount` 块；
4. 强化材料使用 `ReinforcementMaterial`；
5. 材料改写后重新计算所有节点的质量比例和难度结果。

默认强化材料为 Stone，而不是 Iron。原因是大型铁质楼板的密度会让整栋质量分布剧烈改变，并可能让本来有效的弱点在质量比例上失去意义。需要更硬的关卡时仍可手动改为 Iron，但必须重新进行空载和实际击打验收。

普通攻击预测：

```text
NonWeakEffect(Node) = UnsupportedMassRatio(Node) / MaterialHitTier(Node)

PredictedNonWeakEffect = max(all non-weak nodes)
```

弱点预测：

```text
WeakEffect = PredictedWeakCollapseRatio / EstimatedWeakPointHits

WeakPointAdvantage =
    WeakEffect / max(PredictedNonWeakEffect, epsilon)
```

启用 `bRejectOutsideDifficultyWindow` 时，以下情况拒绝生成：

- 弱点换料后落出坍塌质量窗口；
- `PredictedNonWeakEffect > MaxNonWeakEffect`；
- `WeakPointAdvantage < MinWeakPointAdvantage`；
- 无足够数量、暴露度和独立性的候选。

默认要求弱点单位成本效果至少是普通攻击的 `1.5` 倍。

## 9. 默认参数

```text
bEnableWeakPointPlanning = true
WeakPointCount = 1
bAutoSelectWeakPointMaterial = true
TargetBirdHits = 1
WeakPointMaterial = Glass              # 仅手工选材时使用

MinWeakCollapseRatio = 0.02
TargetWeakCollapseRatio = 0.20
MaxSingleWeakCollapseRatio = 0.70
MinWeakPointExposure = 0.35
MaxNonWeakEffect = 0.25
MinWeakPointAdvantage = 1.50

MinWeakPointSeparationCM = 180
MaxWeakPointAffectedOverlap = 0.60

bReinforceNonWeakCriticalNodes = true
ReinforcementMaterial = Stone
ReinforcementImpactThreshold = 0.20
MaxReinforcedNodeCount = 4

bRejectOutsideDifficultyWindow = true
bShowWeakPointDebug = true
WeakPointDebugScale = 1.04
```

默认最小失效质量比为 2%，不是按砖数设置。B2 的 TwinTowerBridge authored load 在真实密度口径下接近 2%；若仍使用按块数直觉设置 10%–20% 下限，会错误地判定没有弱点。实际难度不能只靠提高该下限，应与 `TipMargin`、`ReseatRisk`、射界和弱点优势共同判断。

## 10. 编辑器操作：M7.1 平面实验台

### 10.1 更新与放置

1. 关闭正在运行的 Editor，完成 C++ 编译后重新打开工程。
2. 打开 M7.1 平面物理测试地图。
3. 选中已有 `ABTSM73StableBuildingActor`，或从 All Classes 放置 `M7.3 Stable Building Generator`。
4. Actor Scale 保持 `(1,1,1)`；尺寸只通过 `GenerationSettings` 调整。
5. 保持 `GroundMode=Auto`，并按 M7.3-A 的方法放到测试台。
6. 旋转整个 Actor，使 `AttackDirection` 箭头与测试弹弓发出的鸟飞行方向一致。
7. 展开 `ABTS|M7.3-B|Difficulty -> DifficultySettings`。
8. 首轮保持第 9 节默认值。

### 10.2 预览

预览应出现：

- 四材质 HISM 中真实的弱点材料；
- 弱点外侧一层略放大的红色 Debug Cube；
- `GenerationSummary.WeakPointCount = 1`；
- `PrimaryWeakPointNodeId` 为有效 NodeId；
- `PredictedWeakCollapseRatio >= 0.02`；
- `PredictedNonWeakEffect <= 0.25`；
- `RejectReason` 为空。

不需要红色覆盖层时关闭 `bShowWeakPointDebug`。它没有碰撞，只用于 Editor 识别；运行时生成真实砖 Actor 后会随其他预览 HISM 一起清空。

若引擎基础材质未能显示预期红色，可创建一个普通不透明、红/橙色高亮材质并赋给 `WeakPointDebugMaterial`，无需修改算法。

### 10.3 难度调节顺序

建议按以下顺序调参，避免多个参数互相抵消：

1. 用 `Min/Target/MaxWeakCollapseRatio` 确定希望掉落多少结构；
2. 用 Actor 朝向和 `MinWeakPointExposure` 确定射界；
3. 用 `TargetBirdHits` 选择弱点材料层级；
4. 用 `ReinforcementImpactThreshold` 和强化材料限制其他高影响位置；
5. 最后提高 `MinWeakPointAdvantage`；
6. 每次换材料后重新做空载和击打验收。

不要通过把所有非弱点都改成 Iron 获得难度。那会增加质量失衡、碰撞冲量和“怎么打都不倒”的风险。

### 10.4 PIE 实际击打

1. PIE 后等待 M7.3-A 空载验证完成。
2. 确认建筑没有初始抖动、穿透或自发倾倒。
3. 用同一鸟、尽量相近的拉力先撞一次普通柱/墙面，记录局部移动和破坏。
4. 重新开始 PIE，以同一鸟和相近拉力攻击红色预览对应的真实弱点位置。
5. 弱点击碎后，B2 authored weakness 的 `AffectedNodeIds` 对应 Carrier/载荷应自然进入 Chaos 倾覆或下落；普通旧图候选仍使用 `UnsupportedNodeIds`。
6. 普通攻击可以推动、累计损伤或造成局部反馈，但其结构损失应明显低于弱点攻击。
7. 检查倒塌没有在鸟抵达前发生，FoundationCap/Feet 仍保持静态。

图探针只能证明 Ground 路径差异，不能保证最终倒塌方向。若物理结果与图预测相反，应优先检查碰撞形状、未建模接触、质量和实际命中速度。

## 11. 正式球面兼容

同一 `BuildingSeed`、轮廓、尺寸、材质 Profile 和 DifficultySettings 在平面与球面必须输出相同：

- Brick NodeId；
- Support Edge；
- WeakPoint NodeId；
- 弱点与强化材料；
- 失撑子图；
- 预测难度摘要。

球面只通过 `FABTSM73GroundContext.AnchorTransform` 把建筑局部数据变换到 Anchor：

- Local `+Z` 对齐 Anchor 径向；
- 攻击箭头变换到同一局部框架后参与射线计算；
- FoundationCap/Feet 处理曲率和地形高度；
- 运行时砖继续走 M7 径向重力。

如果 TaskGraph 决定了弹弓与建筑位置，正式 Spawner 应用“弹弓到建筑”的切平面方向设置建筑 Yaw，再执行弱点规划。地形或其他建筑挡住射界时应拒绝该 Anchor/朝向，不能为了通过而随机改弱点。

## 12. 运行时装配

通过规划的数据仍使用：

```text
AABTSM7BuildingMaterialSystem::SpawnBrickModule
-> AABTSM7BuildingModule
```

每个 Node 的最终 `Material` 写入 `FABTSM7BrickSpec`，所以弱点真实使用 Glass/Wood 等较低成本 Profile，强化节点真实使用 Stone/Iron Profile；没有额外的弱点伤害倍率。

`AABTSM73StableBuildingActor` 保存 `NodeId -> Runtime Module` 的弱引用映射，仅用于日志和未来 Gameplay 查询。通用 `AABTSM7BuildingModule` 不反向依赖 M7.3。

预览 HISM 始终 `NoCollision`，运行时真实模块生成后全部清空，避免同一砖两套碰撞造成初始穿透弹飞。

## 13. Summary 与日志

Details 中新增：

```text
WeakPointCount
ReinforcedNodeCount
PrimaryWeakPointNodeId
BestWeakPointScore
PredictedWeakCollapseRatio
PredictedNonWeakEffect
EstimatedWeakPointHits
DifficultyScore
```

运行时日志：

```text
[ABTS][M7.3-B][WeakPoint]
Actor=... Node=... Role=... Module=...
UnsupportedMass=... Exposure=... Hits=... Score=...

[ABTS][M7.3-B][Difficulty]
Actor=... WeakPoints=... Reinforced=...
WeakCollapse=... NonWeakEffect=... Hits=... Score=...
```

常见拒绝原因：

```text
InsufficientWeakPoints:Found:Requested
WeakPointOutsideWindow:NodeId:Ratio
NonWeakTooFragile:Actual:Limit
WeakPointAdvantageTooLow:Actual:Required
IncompleteMaterialProfiles
InvalidOrDuplicateNodeId
InvalidSupportEdge
InvalidGroundNode
```

## 14. 自动化测试

新增：

```text
ABTS.M73B.WeakPointPlanner
```

覆盖：

- 默认 Profile 的实际成本顺序为 `Glass < Wood < Stone < Iron`；
- SingleTower、Gatehouse、TwinTowerBridge 均能选出默认弱点；
- 弱点数量、材料、暴露度和失撑子图正确；
- 弱点效果达到非弱点效果优势；
- 换料后仍通过 M7.3-A 静态稳定校验；
- 相同 Seed 重建后的 NodeId、材料和弱点标记一致；
- 不可能的难度窗口稳定拒绝，且不留下半完成材料改写。

原有测试继续保留：

```text
ABTS.M73A.DefaultStructuresAreStaticallyStable
```

它继续保护三种基础结构和五层 `51:50` / `53:50` 砖块预算诊断。

## 15. 验收清单

### 功能

- 默认三种轮廓均产生一个弱点；
- 改 Seed 后结果确定且可复现；
- 弱点不是 FoundationCap/Feet；
- 弱点材料进入真实 M7 Damage/Chaos 链路；
- 强化节点没有额外隐藏无敌逻辑；
- 关闭弱点规划后恢复 M7.3-A 原始材料。

### Gameplay

- 弱点从主发射方向能直接命中；
- 弱点击碎后至少有一组上层块失去支撑；
- 普通攻击仍有碰撞反馈，但不应和弱点同样高效；
- 默认不会一击无条件清空整栋；
- 更换鸟种或 Profile 后重新评估 `TargetBirdHits`，不把相对层级当绝对次数。

### 稳定性

- 弱点换成 Glass 后建筑空载仍不自行破坏；
- 强化材料不造成初始重叠、弹飞或明显质量不稳；
- 空载判定把施工平面漂移与重力轴接触沉降分开：默认分别不超过 `4cm` 和 `6cm`，避免把小幅竖向落座误报成倾倒；
- 发射开始前所有砖保持静态；
- 发射开始后真实模块统一进入 Chaos；
- 平面和球面使用各自正确的恒向/径向重力。

### 视觉

- Editor 红色覆盖与真实弱点砖位置一致；
- Debug Overlay 无碰撞且不进入发射；
- 关闭 Overlay 后仍能从材料反差辨认弱点；
- TwinTowerBridge 弱点不应被铁桥面自身完全遮挡。

### 性能

- 默认单栋不超过 50 个候选；
- 图探针不生成临时 Actor 或 Chaos Body；
- Editor 属性修改无明显长阻塞；
- 正式批量 PCG 仍应只对最终少量候选做实际 Chaos rollout。

## 16. 排错

| 症状 | 根因 | 处理 |
| --- | --- | --- |
| 建筑预览全部消失，`InsufficientWeakPoints` | 难度窗口、暴露度、弱点数量或子图重叠要求过严 | 先恢复默认值；`WeakPointCount=1`；降低 Min Ratio/Exposure；确认攻击箭头方向 |
| TwinTowerBridge 找不到弱点 | 铁桥面和 B2 顶部载荷按真实密度计入总质量，仍按块数直觉设了过高 Min Ratio | 使用质量口径；从当前默认 `MinWeakCollapseRatio=0.02` 起调，并同时检查 B2 Tip/Reseat 指标 |
| 弱点红框在建筑背面 | AttackDirection 箭头被理解成“指向弹弓” | 箭头应与鸟的飞行方向同向；旋转 Actor 后 Rebuild |
| 弱点红框不显示 | Debug 关闭、规划被拒绝或材质父项不能设置 Color | 检查 Summary；打开 Debug；给 `WeakPointDebugMaterial` 指定红色材质 |
| 弱点击碎但上层不掉 | 实际 Mesh 有未建模接触，或碰撞体尺寸与生成 AABB 不一致 | 检查 Simple Collision、Pivot、最终 Scale；B2 比较 `AffectedNodeIds`，旧图候选比较 `UnsupportedNodeIds` |
| 打普通位置同样整栋倒 | 普通高影响梁未强化、MaxNonWeakEffect 太宽或现有材料 Profile 太脆 | 降低 MaxNonWeakEffect/强化阈值；用 Stone 强化；重新跑击打对照 |
| 强化后整栋很重、二次碰撞异常 | 大楼板被改为 Iron，密度远高于木 | 恢复默认 Stone；减少强化数量；重做空载和击打验收 |
| 换料后 `MaxMove` 略超 4cm，但建筑没有倾斜 | 旧空载验证把法向接触沉降与沿施工平面的失稳漂移混为一个总位移 | 当前实现分别记录 `MaxDrift` 与 `MaxSettlement`；默认漂移上限 4cm、沉降上限 6cm，真正掉落一层仍会远超沉降上限 |
| `WeakPointAdvantageTooLow` | 弱点换料后效果/成本仍没有显著超过普通攻击 | 选择更易碎弱点材料、强化最高影响普通节点或调整轮廓 |
| `WeakPointOutsideWindow` | 换料/强化改变总质量后，最终比例越界 | 调整比例窗口或降低强化材料密度；不要只看换料前候选分数 |
| 同 Seed 在 Editor 与 PIE 弱点不同 | Runtime MaterialSystem Profile 被改过，而 Editor 无该 Actor 时使用共享默认 Profile | 让 Profile 保持一致；根据运行时日志确认实际 NodeId；后续可把 ProfileSet 资产化 |
| 提高 WeakPointCount 后拒绝 | 简单轮廓没有足够远且失撑子图不重叠的多个解 | 使用 1 个弱点，或在后续结构语法中增加独立翼/塔/桥段 |

## 17. 下一阶段接口

> B2 已将真实失效载荷写入 `WeakPointRecord.AffectedNodeIds`；模板、Carrier 约束与验证门槛见 [M73B2StructuralWeaknessAndFailureValidationDesign.md](M73B2StructuralWeaknessAndFailureValidationDesign.md)。

M7.3-C 可直接基于当前 `WeakPointRecord.AffectedNodeIds` 和 `WeakPointRole` 添加：

- 覆盖关键节点的炸药桶；
- 将上层推出支撑区的弹簧活塞；
- 只承拉的绳/锁链；
- 装置触发目标和预期方向；
- 装置弱点与纯材料弱点的组合预算。

装置不能绕过当前流程。加入任何 Device 后仍需重新构建关系图、执行空载稳定验证，并比较弱点与非弱点攻击结果。
