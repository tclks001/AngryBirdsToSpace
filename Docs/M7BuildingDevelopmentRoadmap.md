# M7 建筑系统文档导航、模块清单与执行路线

> 文档性质：M7 功能工作树的导航与阶段状态权威页。具体算法、参数和验收合同仍以链接的子设计稿为准。
>
> 当前路线：以已验收的 DAG5-B v2 语义轮廓为上游，转入长条梁式结构编译；
> [M7.3-Beam-B](M73BeamBMotifWFCAndGraphGrammarDesign.md) 已完成全局装配收口、Beam-A 语义轮廓拟合与自动化，
> 等待用户编辑器读形验收，后续阶段为 Beam-C。
>
> 生产现状：球面 TaskGraph 普通建筑仍使用已稳定的 DAG2.3 路径。Beam 路线在完成
> Beam-D 真实 Brick/Load DAG/Chaos 闭环前不得替换生产默认值。

## 1. 文档之间的关系

```text
M7 材料、碰撞与破坏服务
├─ M7.1 平面物理测试场
├─ Legacy M7.3-A/B/B2（历史工程基线）
└─ M7.3-DAG 正式生成路线
   ├─ DAG1 → DAG2 → DAG2.1 → DAG2.2 → DAG2.3（当前生产路径）
   ├─ DAG3-A/B/C → DAG4 → DAG5-A/B v1（已实现、默认关闭）
   ├─ DAG5-B v2 Shape Grammar + WFC 语义轮廓（已验收的编辑器原型）
   └─ Beam 演进
      ├─ Beam-A：Bay / Joint / Member / Assembly IR 与线框预览
      ├─ Beam-B：Bay 内 Motif WFC、Beam-A 语义屋顶与全局装配收口（待读形验收）
      ├─ Beam-C：受限扩展、构件选择与 Load DAG 提取（下一阶段）
      └─ Beam-D：真实 Brick、弱点、Chaos 与生产候选认证
```

### 1.1 基础与现行生产文档

| 文档 | 关系 | 当前状态 |
| --- | --- | --- |
| [M7 材料与装置](M7BuildingMaterialsAndDevicesDesign.md) | 所有建筑生成器共享的材质、Brick、伤害与装置服务 | 已实现，继续复用 |
| [M7.1 平面物理测试台](M71PlanarPhysicsTestStageDesign.md) | 手工摆放、编辑器读形及 Chaos 验收场 | 已实现，继续作为人工验收入口 |
| [M7 TaskGraph 球面集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) | M3 Anchor/Pad 到 M7 建筑 Actor 的生产接线 | DAG2.3 当前生产基线 |
| [M7.3-DAG 总路线](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md) | DAG1～5 和 Beam 演进的算法父文档 | 持续维护 |

### 1.2 已实现的 DAG 子阶段

| 阶段 | 文档 | 状态与用途 |
| --- | --- | --- |
| DAG1 | [递归语法](M73DAG1RecursiveGrammarImplementationDesign.md) | 已实现；确定性 Macro DAG 与预算边界 |
| DAG2 | [空间布局与模块编译](M73DAG2SpatialLayoutAndModuleCompilationDesign.md) | 已实现；Scope、模块几何与生产编译 |
| DAG2.1 | [支撑模式](M73DAG21SupportPatternsDesign.md) | 已实现；支撑拓扑模式 |
| DAG2.2 | [自适应几何](M73DAG22AdaptiveGeometryDesign.md) | 已实现；尺寸、地形与几何适配 |
| DAG2.3 | [累计荷载与联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md) | 已实现并用于生产；Beam-C/D 将复用其荷载规则 |
| DAG3 | [内部 Failure Frontier](M73DAG3InternalFailureFrontierDesign.md) | 已实现、默认关闭；结构改写与静态弱点候选 |
| DAG3-C | [攻击可达与生产路由](M73DAG3CAttackReachabilityAndProductionRoutingDesign.md) | 已实现、默认关闭；攻击方向和候选路由 |
| DAG4 | [settled Contact 与攻击对照](M73DAG4SettledContactAndAttackRolloutDesign.md) | 已实现、默认关闭；静态/真实接触/Chaos 反事实认证 |
| DAG5-A/B v1 | [候选搜索、语义轮廓与生产认证](M73DAG5CandidateSearchSemanticEnvelopeAndProductionDesign.md) | 已实现、默认关闭；保留为有界搜索与真实 Brick 接入基线 |
| DAG5-B v2 | [复杂轮廓预览](M73DAG5Bv2ComplexSilhouettePreviewDesign.md) | 已实现并完成人工读形；作为 Beam 上游 |

### 1.3 历史、研究和暂不执行的方案

| 文档 | 当前定位 |
| --- | --- |
| [原程序化模块化建筑调研](M73ProceduralModularBuildingGenerationResearch.md) | **历史参考**。保留早期生成原则，不再作为实现入口 |
| [M7.3-A 稳定积木建筑](M73AStableBlockBuildingImplementationDesign.md) | **Legacy**。Ground/IdleValidation 等公共经验继续复用，旧轮廓不再扩展 |
| [M7.3-B 弱点与难度](M73BWeakPointAndDifficultyDesign.md) | **Legacy**。图分析思路保留，旧 WeakPointPlanner 不接入现行生产 |
| [M7.3-B2 结构弱点](M73B2StructuralWeaknessAndFailureValidationDesign.md) | **Legacy**。顶部冠段只作失败模式对照 |
| [3D WFC 与 DAG 拟合调研](M73WFCBuildingEnvelopeAndDAGFittingResearch.md) | **已吸收，不独立实施**。语义 WFC 已进入 DAG5-B/v2，结构 WFC 由 Beam-B 接管 |
| [轮廓约束递归 DAG 演进设计](M73EnvelopeConditionedRecursiveDAGGenerationEvolutionDesign.md) | **部分延期**。其中 Shape/WFC 前端继续复用；Plate 拉伸和整板楼层实现暂不进行，改由 Beam 路线验证 |
| DAG5-C～E | **暂不进行**。其生产认证目标迁移到 Beam-C/D，待梁式结构闭环后重新映射 |

## 2. 当前可复用模块

| 能力 | 已实现模块 | Beam 路线中的复用方式 |
| --- | --- | --- |
| 语义轮廓 | `FABTSM73DAG5BShapeGrammarV2`、`AABTSM73DAG5BShapePreviewActor` | Beam-A 直接消费确定性 Volume/Role/Primitive；轮廓复杂度保持独立 |
| 有界候选搜索 | `FABTSM73DAG5CandidateSearch` | Beam-C/D 搜索结构候选时复用预算、回溯和稳定拒绝语义 |
| 语义包络 | `FABTSM73DAG5BSemanticEnvelope` | 作为 Port、MustVoid、结构区域定义的旧基线，不直接决定最终梁 |
| 递归图 | `FABTSM73DAGGrammarExpander` | Beam-C 对 Assembly/Member 做受限展开时复用确定性推导原则 |
| 布局与几何 | `FABTSM73DAGLayoutSolver`、`FABTSM73DAGSupportGeometry` | Beam-C/D 复用边界、跨度、落脚点和适配规则 |
| 荷载 | `FABTSM73DAGLoadSupportSolver` | Beam-C 从 Beam Assembly Graph 提取 Load DAG 后复用累计荷载合同 |
| 真实模块编译 | `FABTSM73DAGModuleCompiler`、`FABTSM73DAGBuildingPipeline` | Beam-D 把选中的 Member 编译为真实 Brick/Actor |
| 地面与施工面 | `FABTSM73GroundAdapter` | Beam-D 的平面/球面共同落地层 |
| 静态稳定 | `FABTSM73DAGValidator`、`FABTSM73StabilityValidator`、`FABTSM7PenetrationValidator` | Beam-C/D 的快速拒绝与无穿透检查 |
| 弱点与可玩性 | `FABTSM73DAGFailureFrontierAnalyzer`、`FABTSM73DAGFailurePatternRewriter`、`FABTSM73DAGFailurePlayabilityPlanner` | Beam-C/D 基于新 Load DAG 复用，不复用旧固定楼板轮廓 |
| 真实接触与动态认证 | `FABTSM73DAGContactGraphBuilder`、DAG4 settled/response/trial/runtime validators | Beam-D 最终验收；Beam-A/B 不提前调用 Chaos |
| 材料与运行时砖 | `AABTSM7BuildingMaterialSystem`、`AABTSM7BuildingModule`、`AABTSM73StableBuildingActor` | Beam-D 的唯一真实物理输出层 |

复用的原则是“复用合同和验证器，不把旧 Plate 几何偷偷带回 Beam IR”。Beam Assembly
Graph 是无向/混合方向的几何装配图；Load DAG 是之后按重力和接触提取的有向承载图，两者不得混为一张图。

## 3. Beam 路线执行情况

| 阶段 | 目标 | 当前状态 |
| --- | --- | --- |
| Beam 调研 | 明确轮廓复杂度与结构复杂度分层，以及 Assembly Graph / Load DAG 双图 | [调研完成](M73BeamBlockStructuralGenerationResearch.md) |
| Beam-A v2 | 将 Volume 编译为 Bay，再生成固定截面、可变长度的 XYZ 积木及 Bearing Contact | [已完成并通过用户编辑器读形验收](M73BeamAStructuralIRPreviewDesign.md) |
| Beam-B | Box Bay 用 Motif WFC 选择结构家族，Prism/Pyramid Bay 复用 Beam-A 逐层收分语义屋顶，并统一装配收口 | [9 项专项与 91 项 M7 自动化完成，待编辑器读形](M73BeamBMotifWFCAndGraphGrammarDesign.md) |
| Beam-C | 对图做预算内展开，选择离散构件，并提取/验证 Load DAG | 未开始 |
| Beam-D | 编译真实 Brick，联合弱点、真实接触、Chaos、TaskGraph 与六栋建筑候选 | 未开始 |

Beam-A v2 的完成不表示建筑已经物理可站立。它证明复杂语义轮廓能够稳定转换为有明确上下
顺序的 XYZ 长条积木与 Bearing Contact，并且不再依赖方框中心线端点拼接。

## 4. 当前执行链

```text
DAG5-B v2 Shape Grammar + graph WFC
  -> semantic Volumes (implemented)
  -> Beam-A Bay decomposition (implemented)
  -> Joint / Member / Assembly structural IR (implemented)
  -> editor-only stacked-block preview (implemented)
  -> Beam-B Box motifs + Beam-A semantic roof fitting + global closure (implemented)
  -> Beam-C bounded expansion + Load DAG (next)
  -> Beam-D Brick + weak point + Chaos + production routing
```

在 Beam-D 完成前，现行球面生产链保持：

```text
TaskGraph Anchor / Pad
  -> DAG1
  -> DAG2 / 2.1 / 2.2 / 2.3
  -> real modules
  -> GroundAdapter / IdleValidation
```
