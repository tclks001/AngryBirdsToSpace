# M7.3-DAG5-B v2：复杂建筑轮廓生成与编辑器预览设计

> 阶段性质：Shape Grammar + WFC 的轮廓原型。
>
> 状态：C++、程序化网格与纯数据自动化已实现；待用户在 Editor 中完成视觉验收。
>
> 本阶段只生成可见轮廓，不生成完整建筑、承重 DAG、Brick、碰撞、弱点或 Chaos。
>
> 父级：[轮廓约束递归 DAG 建筑生成演进设计](M73EnvelopeConditionedRecursiveDAGGenerationEvolutionDesign.md)。
> 前代基线：[DAG5-A/B 候选搜索与语义轮廓](M73DAG5CandidateSearchSemanticEnvelopeAndProductionDesign.md)。

## 1. 目标与边界

DAG5-B v1 已证明固定的四类 `SemanticEnvelope` 可以进入真实 Brick 链，但宏观轮廓仍由
硬编码 Macro Graph 决定。v2 先独立实现更通用的轮廓前端：

```text
Archetype Root
  -> Recursive Shape Grammar
  -> semantic volume slots
  -> adjacency graph WFC
  -> Box / Triangular Prism / Pyramid
  -> editor ProceduralMesh preview
```

目标：

- 参数可以改变建筑总尺寸、递归深度、体量数量、分裂、退台、偏移和桥接概率；
- Shape Grammar 的递归规则真实增加体量，而不只是缩放固定模板；
- WFC 根据语义角色和相邻关系选择立方体、三角棱柱、棱锥；
- 至少提供四种不同的大型建筑起始拓扑；
- 同一 Seed 和完整参数得到完全一致的轮廓；
- 编辑器中修改参数后自动重建，不需要创建或修改二进制资产。

非目标：

- 不生成 DAG2.3 Plate、Column 或 `FABTSM73StructureData`；
- 不声明轮廓具有静力稳定性；
- 不运行 Failure Frontier、弱点规划或 DAG-4；
- 不生成碰撞，不参与 PIE 或 TaskGraph；
- 不替换当前生产建筑 Profile。

## 2. 独立预览 Actor

新增：

```text
AABTSM73DAG5BShapePreviewActor
DisplayName = M7.3 DAG5-B v2 Complex Silhouette Preview
```

Actor 只拥有：

- 一个 Root；
- 一个 `UProceduralMeshComponent`；
- `FABTSM73DAG5BV2PreviewSettings`；
- 最新生成摘要；
- 三类形体的预览颜色与动态材质。

预览组件：

- 无碰撞；
- 不生成 Overlap；
- 不影响导航；
- `HiddenInGame=true`；
- 仅作为 Editor 视觉原型。

采用独立 Actor，而不是向 `AABTSM73StableBuildingActor` 增加原生子对象，避免影响既有
Blueprint 和地图实例的 Native Default Subobject 序列化。

## 3. Shape Grammar

### 3.1 起始拓扑

`Archetype` 提供四类低频根拓扑：

| Archetype | 初始体量 |
|---|---|
| `TerracedCitadel` | 中央主楼、左右不同高度附楼 |
| `TwinTowerComplex` | 两座不同高度塔楼和中央桥 |
| `BridgedArcology` | 西塔、核心、东塔和两条不同高度桥 |
| `SpiredCampus` | 中央高塔、左右翼楼和两条连接体 |

`Auto` 根据 `BuildingSeed + GeneratorVersion` 确定性地选择其中一类。

这些 Archetype 只定义初始 Chunk/Scope，不定义最终体量数量。每个 Root 会继续执行同一套
通用递归规则。

### 3.2 递归规则

当前规则：

| 规则 | 行为 |
|---|---|
| `Stack` | 将 Scope 沿 Z 分成下部终端和继续递归的上部 |
| `Setback` | 沿 Z 分层，同时缩小并偏移上部 Scope |
| `SplitX` | 左右分区，中间留下空隙 |
| `SplitY` | 前后分区，中间留下空隙 |
| `Bridge` | 分区后按概率在中高层增加横跨体量 |
| `Terminal` | 停止展开并输出语义体量 |

规则选择由稳定路径 Seed 决定：

```text
StableSeed =
  Hash(GeneratorVersion, BuildingSeed, DerivationPath, StageSalt)
```

因此在一个分支中插入新的递归步骤，不会让其他独立路径全部漂移。

### 3.3 深度和预算

- `MinGrammarDepth`：达到该深度前不允许随机终止；
- `MaxGrammarDepth`：达到后必须终止；
- `MaxVolumeCount`：最终可见体量的硬预算；
- 每个递归子树获得独立 Leaf Budget；
- 分叉时预算确定性地分配给两个子树和可选桥梁；
- 预算不足时终止当前 Scope，不生成超过预算的半成品。

递归深度增加会真实增加：

- 楼层分段；
- 左右/前后分支；
- 退台层；
- 不同高度体量；
- 局部桥接；
- 顶部终端数量。

## 4. WFC 形体选择

### 4.1 WFC 节点

Shape Grammar 输出的每个终端体量成为一个 WFC 节点。节点保留：

```text
VolumeId
GrammarDepth
LocalBounds
Role
DerivationPath
PrimitiveDomain
```

`Role` 包括：

```text
Foundation / Body / Annex / Bridge / Crown
```

### 4.2 形体域

当前 WFC 可选形体：

| Primitive | 表现 |
|---|---|
| `Box` | 立方体或按 Scope 拉伸的长方体 |
| `TriangularPrismX` | X-Z 三角截面、沿 Y 延伸的棱柱 |
| `TriangularPrismY` | Y-Z 三角截面、沿 X 延伸的棱柱 |
| `Pyramid` | 四边底座和顶部单顶点棱锥 |

棱柱和棱锥都属于终端屋顶体量，上方不能再承载其他体量。垂直 WFC 相邻关系只允许
`Box` 作为下部节点；任何 `HasAbove` 体量的初始 Domain 都会被硬约束为 `Box`。
桥梁只有在自身没有上部体量时，才允许选择沿主跨度方向的 Prism。求解完成后还会再次
检查该不变量，并以 `DAG5BV2RoofPrimitiveHasUpperVolume` fail closed，避免未来修改
Domain 或传播规则时重新产生“棱柱上放棱锥”等明显不稳定的组合。

### 4.3 传播与回溯

WFC 在终端体量邻接图上运行：

1. 根据语义角色建立初始 Domain；
2. 根据垂直、X 水平和 Y 水平邻接传播相容性；
3. 选择熵最低的未决节点；
4. 根据 Box / Prism / Pyramid 权重确定性地排列候选；
5. 矛盾时回溯；
6. 超过传播或回溯预算时 fail closed。

`bRequirePrimitiveVariety=true` 时加入三类硬锚点：

- 一个 Foundation 使用 Box；
- 一个无遮挡顶部使用 Pyramid；
- 另一个顶部或 Bridge 使用 Prism。

最终必须同时存在 Box、Prism 和 Pyramid，否则拒绝候选。

## 5. 程序化网格

预览 Mesh 由三个 Section 组成：

```text
Section 0: Box
Section 1: Triangular Prism
Section 2: Pyramid
```

每个 Section 使用独立颜色。网格特点：

- 每个面使用独立顶点与 flat normal；
- 正反两个 winding 都生成，保证从编辑器不同视角可读；
- 无碰撞；
- 默认最多 96 个体量；
- 最坏情况下约两千余个可见三角形，适合编辑器原型。

本阶段不把程序化轮廓转换成 StaticMesh 资产，也不保存派生网格到 Content。

## 6. 编辑器参数

### Identity

| 参数 | 作用 |
|---|---|
| `BuildingSeed` | 改变规则选择和 WFC 形体组合 |
| `GeneratorVersion` | 轮廓身份版本 |

### Bounds

| 参数 | 作用 |
|---|---|
| `TargetWidthCM` | 建筑整体宽度 |
| `TargetDepthCM` | 建筑整体深度 |
| `TargetHeightCM` | 建筑整体高度 |

### Shape Grammar

| 参数 | 作用 |
|---|---|
| `Archetype` | 选择低频起始拓扑 |
| `MinGrammarDepth` / `MaxGrammarDepth` | 控制必须展开和最大展开层数 |
| `MaxVolumeCount` | 最终体量硬预算 |
| `MinVolumeSpanCM` | 允许继续分割的最小尺寸 |
| `StackWeight` | 垂直层叠倾向 |
| `HorizontalSplitWeight` | 左右/前后分区倾向 |
| `SetbackWeight` | 退台和上层偏移倾向 |
| `TerminalWeight` | 达到最小深度后的提前终止倾向 |
| `SplitGapRatio` | 分区之间的留空比例 |
| `SetbackRatio` | 上层收缩比例 |
| `MaxOffsetRatio` | 上层最大横向偏移 |
| `BridgeChance` | 分区后生成桥梁的概率 |
| `BridgeThicknessRatio` | 桥梁相对高度 |

### WFC

| 参数 | 作用 |
|---|---|
| `BoxWeight` | Box 形体权重 |
| `PrismWeight` | 两类 Prism 的合计权重 |
| `PyramidWeight` | Pyramid 权重 |
| `bRequirePrimitiveVariety` | 强制三种形体家族同时出现 |
| `MaxWFCPropagationOperations` | 传播硬预算 |
| `MaxWFCBacktrackSteps` | 回溯硬预算 |

## 7. Editor 验收步骤

1. 编译并重新启动本工作树的 Editor；
2. 在 Place Actors 中搜索
   `M7.3 DAG5-B v2 Complex Silhouette Preview`；
3. 把 C++ Actor 拖入空旷区域；无需创建 Blueprint；
4. 保持默认参数，确认出现蓝色 Box、绿色 Prism 和红色 Pyramid；
5. 依次固定四个 `Archetype`，观察整体拓扑明显不同；
6. 将 `MaxGrammarDepth` 从 `2` 调到 `4`，确认内部层级和体量数量增加；
7. 调高 `HorizontalSplitWeight` 与 `BridgeChance`，确认分区和横桥增加；
8. 调高 `SetbackWeight`、`SetbackRatio` 和 `MaxOffsetRatio`，确认轮廓逐层收缩和偏移；
9. 修改 `BuildingSeed`，确认产生新变体；再改回原值，确认轮廓完全复现；
10. 点击 `Regenerate Preview` 可手动重建；普通 Details 参数修改也会触发 Construction 重建。

建议首轮固定 Seed：

| Archetype | Seed |
|---|---:|
| `TerracedCitadel` | 820173 |
| `TwinTowerComplex` | 820346 |
| `BridgedArcology` | 820519 |
| `SpiredCampus` | 820692 |

共同建议：

```text
MinGrammarDepth = 2
MaxGrammarDepth = 4
MaxVolumeCount = 96
bRequirePrimitiveVariety = true
```

## 8. 预期结果

通过标准：

- 四个 Archetype 不是同一方塔的缩放版本；
- 每个轮廓同时出现 Box、Prism 和 Pyramid；
- 建筑存在多个高低不同的塔体、附楼、退台或桥接；
- 深度增加会增加真实体量，不只是提高一个固定网格的分辨率；
- 所有体量位于 Target Bounds 内；
- 编辑器修改参数后预览稳定重建；
- 进入 PIE 后该预览隐藏且无碰撞，不干扰物理测试场。

本阶段不验收：

- 柱网；
- 楼板牵拉；
- 初始 DAG 和 Expansion；
- 弱点；
- 建筑稳定性；
- TaskGraph 生产接入。

## 9. 日志与诊断

成功日志：

```text
[ABTS][M7.3-DAG5Bv2][PreviewGenerated]
Actor=...
Archetype=...
GrammarSteps=...
Volumes=...
Box=...
Prism=...
Pyramid=...
Propagation=...
Backtracks=...
GrammarHash=...
WFCHash=...
ResultHash=...
```

失败日志：

```text
[ABTS][M7.3-DAG5Bv2][PreviewRejected] Actor=... Reason=...
```

稳定拒绝原因包括：

```text
DAG5BV2InvalidSettings
DAG5BV2InitialPlanExceedsBudget
DAG5BV2VolumeBudgetExceeded
DAG5BV2PrimitiveVarietyUnreachable
DAG5BV2WFCPropagationBudgetExceeded
DAG5BV2WFCBacktrackBudgetExceeded
DAG5BV2WFCNoSolution
DAG5BV2PrimitiveVarietyNotRealized
```

## 10. 自动化证据

过滤器：

```text
ABTS.M73DAG.DAG5Bv2.
```

覆盖：

- `Determinism`；
- `DepthGrowth`；
- `ArchetypeCoverage`；
- `BoundsAndBudget`；
- `InvalidSettings`。

本次证据：

- `Saved/Logs/DAG5Bv2RoofRule-20260731-155608-ForceUnity-Build.log`：
  `-ForceUnity -DisableAdaptiveUnity` Development Editor，Succeeded；
- `Saved/Logs/DAG5Bv2RoofRule-20260731-155416-FreshAutomation.log`：
  fresh NullRHI，找到 6 项，6/6 Success；其中
  `RoofPrimitiveTerminal` 覆盖四种 Archetype、每种 8 个 Seed。
- `Saved/Logs/DAG5Bv2RoofRule-20260731-155524-M7Regression.log`：
  完整 `ABTS.M7`，找到 72 项，72/72 Success。

自动化只证明数据链、预算和确定性，不代替用户的 Editor 视觉验收。

## 11. 实现文件

```text
Public/Building/ABTSM73DAG5BShapePreviewTypes.h
Public/Building/ABTSM73DAG5BShapePreviewActor.h
Private/Building/ABTSM73DAG5BShapeGrammarV2.h
Private/Building/ABTSM73DAG5BShapeGrammarV2.cpp
Private/Building/ABTSM73DAG5BShapePreviewActor.cpp
Private/Building/ABTSM73DAG5BShapeGrammarV2AutomationTests.cpp
```

## 12. 后续接口

Editor 轮廓验收后，下一阶段不应直接把每个体量变成一块最终楼板。正确接续为：

```text
DAG5-B v2 semantic volumes
  -> SemanticEnvelope / Chunk ports
  -> Seed Macro DAG
  -> envelope-conditioned Expansion
  -> Plate footprint fitting
  -> support / weakness joint synthesis
```

其中 Prism 和 Pyramid 需要为后续结构阶段定义“外观包络”和“内部可承重正交 Scope”
两层表示；非长方体外形不应被直接声明为真实承重碰撞。
