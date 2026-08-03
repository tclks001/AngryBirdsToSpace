# M7.3-Beam-C：Load DAG 与静态传力代理

> 父文档：[M7 建筑生成演进路线](M7BuildingDevelopmentRoadmap.md)
> 上游：[M7.3-Beam-B Motif WFC 与结构图语法](M73BeamBMotifWFCAndGraphGrammarDesign.md)
> 理论依据：[长条积木结构生成调研](M73BeamBlockStructuralGenerationResearch.md)
> 状态：首版 C++、编辑器预览和自动化已完成，等待用户编辑器读形验收；不接管 TaskGraph 生产建筑。

## 1. 阶段目标

Beam-C 消费 Beam-B 已经通过全局装配收口的 `ClosedAssembly`，把显式
`BearingContact` 转换为有方向的 Load DAG，并在真实 Chaos 生成前完成廉价、确定性的
静态拒绝。它回答三个问题：

1. 每根积木的荷载是否沿设计接触最终传到 Ground；
2. 多支点水平梁的反力是否为非负并满足合力/一阶矩合同；
3. 跨度、悬臂、柱长细比和基础侧向约束是否落在可调预算内。

Beam Assembly Graph 仍是几何装配图；Load DAG 的边固定为
`UpperMember -> LowerMemberOrGround`。两者不得合并为一张图，也不得从偶然 AABB 接触
推测 Load DAG 边。

## 2. 本阶段边界

Beam-C 首版包含：

- 从 Beam-B 闭合构件与显式承重点生成一节点一构件的 Load DAG；
- 环检测、Ground 可达、承压面积、反力守恒和有界预算检查；
- 水平梁多支点反力、有效跨度/悬臂代理与 Z 柱长细比代理；
- 只依赖 XYZ 构件族的保守侧向机制检查；
- 确定性结果哈希、纯数据自动化和编辑器颜色预览。

首版不包含：

- 真实材料截面、弹性模量、弯矩图、有限元或精确挠度；
- 互承环/SCC 压缩；检测到任何 Load DAG 环都稳定拒绝；
- 弱点选择、Failure Frontier、真实 Brick、约束或 Chaos 落稳；
- TaskGraph 生产切换。上述内容留给 Beam-D/E。

## 3. 数据流与权威关系

```text
Shape Grammar + WFC silhouette
  -> Beam-A block topology / explicit BearingContact
  -> Beam-B motif grammar / global assembly closure
  -> Beam-C Load DAG extraction
       -> topological order
       -> cumulative load + first moment
       -> non-negative reactions
       -> span / cantilever / slenderness / lateral proxies
  -> Beam-D real bricks + Chaos + weakness closure
```

Beam-C 不再移动、增删或重新猜测 Beam-B 构件。若输入缺少支撑，Beam-C 返回稳定拒绝，
修复必须回到 Beam-A/B 的装配规则。

## 4. Load DAG 数据模型

### 4.1 节点

每个闭合 `Member` 对应一个 `LoadNode`，记录：

- `MemberId`、轴向与是否 Ground；
- 自重、累计荷载、累计一阶矩与合力作用点；
- 支点数量、有效跨度、悬臂比、跨度利用率、柱长细比；
- Ground 可达与校验状态。

自重首版使用 `LengthCM * MemberLinearDensityKGPerCM`。这只是离散构件选择前的相对荷载，
不是现实木材密度。

### 4.2 边

每个 Beam-A `BearingContact` 对应一条 Load Edge：

```text
UpperMemberId -> LowerMemberId
```

边保留 `BearingContactId`、接触中心、接触面积、反力份额和反力大小。任何无效成员引用、
自环或低于最小承压面积比例的边均拒绝。

## 5. 静态传力合同

节点按 Load DAG 的上到下拓扑序处理。节点初始累计量为自身荷载与中点一阶矩；收到上层
反力后累加。水平梁把支点投影到自身主轴：

- 合力位于相邻两个支点之间时，使用线性插值求两组非负反力；
- 同一支点上的多个接触按接触面积分摊；
- 合力超出支点包络时，把荷载交给最近支点，并以悬臂比和未平衡力矩代理判定；
- Z 柱和其他退化情形按接触面积分配。

所有非 Ground 节点必须满足 `sum(LoadShare)=1`，且反力总和等于累计荷载。所有 Ground
节点累计反力之和必须与全部构件自重在容差内守恒。

## 6. 快速静态代理

- **跨度利用率**：使用 `LoadRatio * SpanRatio^2 / StiffnessScale`。它保留均布梁
  `wL^2` 随跨度增长的方向性，用于玩法筛选而非工程设计。
- **悬臂比**：最远端到最近外侧支点的距离除以构件全长。
- **柱长细比**：Z 构件长度除以固定方形截面边长。
- **侧向机制**：存在 Z 构件的结构必须同时具备 X、Y 水平构件族；不模拟刚接或斜撑。

所有阈值均作为 `UPROPERTY` 暴露。默认值需保证 Beam-B 四类标准轮廓通过，测试可收紧
单项阈值验证稳定拒绝语义。

## 7. 正式拒绝语义

- `BeamCUpstreamRejected`
- `BeamCNodeBudgetExceeded` / `BeamCEdgeBudgetExceeded`
- `BeamCInvalidBearingContact`
- `BeamCBearingAreaInsufficient`
- `BeamCLoadDAGCycle`
- `BeamCGroundUnreachable`
- `BeamCReactionBalanceFailed`
- `BeamCSpanLimitExceeded`
- `BeamCCantileverLimitExceeded`
- `BeamCColumnSlendernessExceeded`
- `BeamCLateralMechanism`

拒绝原因必须只依赖输入和参数，不依赖容器迭代顺序或帧时序。

## 8. 编辑器预览与验收

新增 `M7.3 Beam-C Load DAG Preview` Actor，继续生成完整上游链路，但只绘制 Beam-B 的
闭合积木。构件按最大静态利用率分档着色：低载为蓝/青，中载为黄，高载为橙/红；Ground
构件单独显示。预览组件无碰撞、无重力且 `HiddenInGame`，只用于编辑器读图。

首版自动化门槛：

1. 同输入 Load DAG、反力与哈希完全确定；
2. 双支点偏载反力满足合力和一阶矩；
3. 环、失地、承压面积不足、跨度超限、长细比超限分别稳定拒绝；
4. Terraced/TwinTower/Bridged/Spired 四类完整 Beam-B 结果均能提取无环、Ground 可达、
   荷载守恒的 Load DAG；
5. 强制 Unity 编译、Beam-C 专项和 M7 回归通过。

人工编辑器验收只确认颜色和参数反馈可读，不宣称 Chaos 动态稳定。Beam-D 完成前生产建筑
不得切换到 Beam 链路。

## 9. 2026-08-03 首版实现证据

- 强制 Unity / 禁用 Adaptive Unity 的 `AngryBirdsToSpaceEditor Win64 Development` 编译通过；
- fresh NullRHI `ABTS.M73DAG.BeamC.` 专项 9/9，通过确定性、双支点反力、四类轮廓矩阵、
  环、失地、承压面积、跨度、长细比与侧向机制；
- fresh NullRHI `ABTS.M7` 全量回归 104/104；
- 新 Preview Actor 无碰撞、运行时隐藏，不修改共享地图或生产默认绑定。
