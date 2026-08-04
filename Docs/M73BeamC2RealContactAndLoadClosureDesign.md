# M7.3-Beam-C2：真实接触检测与承重收口

> 父文档：[Beam-C Load DAG 与静态传力代理](M73BeamCLoadDAGAndStaticProxyDesign.md)
>
> 下游：[Beam-D0 Profile Catalog](M73BeamD0GameplayProfileCatalogDesign.md) →
> [Beam-D1 真实 Brick](M73BeamD1RealBrickAndMaterialRolesDesign.md) →
> [Beam-D1.5 视觉复杂度阶梯](M73BeamD15VisualComplexityLadderDesign.md)
>
> 状态：C++、专项自动化和 30 组 `Profile × Tier` 生产矩阵已完成；Chaos 动态认证仍属于 Beam-D2。

## 1. 问题与目标

Beam-C 首版信任 Beam-A/B 写入的 `BearingContact`。该数据能描述设计意图，但不能证明最终
Brick AABB 在几何上确实接触，也不能阻止宽水平构件仅由一根细柱支承。Beam-D1 因此可能把
“声明为稳定、实际无支撑或支点过窄”的结构编译成真实 Brick。

Beam-C2 增加两道生产门槛：

1. **真实接触一致性**：以最终闭合 Member 的实际 Brick AABB 重新计算承压面，不接受只存在于
   上游声明中的接触；
2. **承重收口**：当水平构件的合力或支点展宽不满足静态代理时，在保留门洞/桥洞语义的前提下，
   有界、确定性地补充局部 Z 柱，再重新执行 Beam-A 全局装配闭合与 Beam-C 检查。

## 2. 权威流程

```text
Beam-B closed assembly
  -> rebuild final BearingContact from member geometry
  -> Beam-C exact AABB face-contact audit
  -> Load DAG / reactions / longitudinal resultant / support spread
  -> if blocking support violation:
       add deterministic local Z support posts
       respect ReservedSupportVoids
       rerun Beam-A assembly closure
       repeat within pass/member budgets
  -> accepted closed assembly + authoritative Load DAG
  -> Beam-D1 one Member : one Brick compile
```

Beam-D1 只能消费该收口结果，不能再使用收口前的 Beam-B Member 数量、Hash 或接触图。

## 3. 真实接触合同

- 每条 `BearingContact` 必须对应一对最终 Member 的上下表面接触；
- Z 面差必须落入 `RealContactToleranceCM`；X/Y 投影必须产生正面积交集；
- 实际接触中心、Patch Min/Max 和面积由几何重算，随后才进入 Load DAG；
- 声明接触与实际接触不一致时以 `BeamCRealContactMismatch` 拒绝；
- 接触面积仍须通过原有最小承压面积门槛。

这不是 Chaos settled contact。它是编译期、无 World、确定性的 Brick AABB 面接触真值，用来在
运行时实例化前消除明显的悬空与伪接触。

## 4. 支撑结果与展宽合同

对水平构件，把每个真实接触 Patch 投影到构件主轴，合并为支撑区间：

- 合力投影必须位于真实支撑区间内，并保留 `SupportResultantMarginCM`；
- 单一 Z 柱支撑宽水平构件时，连续支撑覆盖率不得低于
  `MinimumSingleSupportCoverageRatio`；
- 多个 Z 柱支撑时，最外支点间距不得低于
  `MinimumSeparatedSupportSpanRatio`；
- 水平积木交叠承重允许使用真实接触区间，只要合力落在区间内；这保留 Beam-A 的积木堆放语义，
  不把所有交叉搭接误判为单柱机制。

截图中 SeamRelease Tier 0 的“宽楼层只落在一根细柱上”会触发展宽拒绝，而不是因存在一条
声明边就被接受。

## 5. 有界结构修复

- 修复只新增 Z 向支撑柱，不移动、不删除既有语义构件；
- 支撑柱从下方最近可承重构件或 Ground 接到目标水平构件的真实底面；
- 若目标站位落入 `ReservedSupportVoids`，确定性移动到保留洞口边界，不封死门洞；
- 每一轮修复后调用 Beam-A 权威闭合，重建接触并重新执行完整 Beam-C；
- 由 `MaximumStructuralClosurePasses` 与 `MaximumStructuralSupportPosts` 限制工作量；
- 无可行修复、无进展或预算耗尽均 fail closed，不返回半闭合建筑。

当结构已经具有足够的真实接触展宽，但一维纵向代理仍报告微小合力偏差时，只有在至少完成一轮
实际修复后才允许把该残余记为 `SupportResultantAdvisory`。它仍进入 Summary 和 Hash，并留给
Beam-D2 的三维 Chaos/侧翻认证；真实接触不一致和支撑展宽不足永远是阻断项。

## 6. 输出诊断

Beam-C/D1 Summary 新增：

- `RealContactMismatchCount`；
- `SupportResultantViolationCount` 与 `SupportResultantAdvisoryCount`；
- `SupportSpreadViolationCount`；
- `StructuralClosurePassCount` 与 `AddedStructuralSupportPostCount`；
- 每个节点的真实支撑区间数、覆盖率、支点跨度及有效性标记；
- 每条边的真实接触 Patch Min/Max。

生产接受条件为真实接触不一致、阻断型合力违规和支撑展宽违规均为零。Advisory 不等于 Chaos
验收通过，只表示静态一维代理已经完成其职责边界。

## 7. 自动化验收

1. 声明接触但 AABB 留有缝隙时稳定拒绝；
2. 宽水平梁仅落在细 Z 柱上时稳定拒绝；
3. 合法双支点偏载反力继续满足合力与一阶矩；
4. Beam-B 四类轮廓经结构收口后真实接触不一致与阻断支撑违规为零；
5. 同输入新增柱、收口轮次、Load DAG 与 Hash 完全确定；
6. 五个 Profile 的 E5/E6 以及完整 30 组 `Profile × Tier` 均通过 D1 生产编译；
7. 强制 Unity、Beam-C、Beam-D0/D1/D1.5 与 M7 全量回归通过。

## 8. 阶段边界

Beam-C2 不模拟摩擦、刚接、扭转刚度、动态侧翻或破坏后的载荷重分配，也不决定弱点是否可玩。
这些由 Beam-D2 使用真实 Module、settled contact、Failure Frontier 和 Chaos trial 认证。
Beam-E 完成前，球面 TaskGraph 的 DAG2.3 生产绑定保持不变。

## 9. 2026-08-03 实现证据

- `AngryBirdsToSpaceEditor Win64 Development -ForceUnity -DisableAdaptiveUnity` 完整编译通过；
- fresh NullRHI `ABTS.M73DAG.BeamC.`：11/11；
- fresh NullRHI `ABTS.M73DAG.BeamD0.`：6/6；`ABTS.M73DAG.BeamD1.`：5/5；
- fresh NullRHI `ABTS.M73DAG.BeamD15.`：2/2，其中包含 30 组 `Profile × Tier` 和
  `ColumnBreak` E5/E6 承重闭合专项；
- fresh NullRHI `ABTS.M7`：119/119，旧 DAG2.3 生产基线与 Beam 路线共同回归通过；
- 未启动 Editor/PIE，未修改共享合同、默认生产绑定，也未纳入物理测试场地图的既有用户改动。
