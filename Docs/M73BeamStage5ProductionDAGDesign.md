# M7.3 Beam Stage 5 生产积木与承重 DAG 设计

> 状态：2026-08-15 开始实现第一停点。输入基线为已冻结的 Stage 4 与 Stage 4.5 六栋演示建筑。
> 本阶段只验证静态几何、真实接触 Bearing DAG 和 Load DAG；Chaos 明确为 `NotEvaluated`。

## 1. 目标与非目标

Stage 5 把 Stage 4 的程序化构件计划编译成真正可实例化的积木，并建立与这些积木一一对应的承重图。它不得重新生成建筑形态，不得调用旧 `CompleteStaticDAG` 路径，也不得插入修复柱改变已视觉批准的 Stage 4 几何。

第一停点只覆盖冻结的六栋演示建筑，不运行 5×6 全矩阵、全 Seed 搜索或 Chaos。弱点、绳索、炸药桶和活塞装配不属于这一停点；所有生产积木的 weakness/device role 暂为 `None`。

## 2. 唯一生成顺序

1. 使用 Manifest 中固定的 Profile、Tier、Seed 重建已接受的 Stage 4。
2. 以 `bSuppressedByStage4FacadeToTop` 为唯一 suppression 权威，生成旧 Stage-4 member index 到 compact member id 的映射；被抑制项必须映射为 `INDEX_NONE`。
3. 只从活跃 `FPlannedMember` 重建 Joint、Member 和 Assembly 归属。compact `MemberId` 必须连续且从零开始。
4. 根据最终 36 cm 单位化 member AABB 重新计算 BearingContacts，不继承 suppression 前的接触边。
5. 对 compact assembly 运行只读 Beam-C：不得调用 `GenerateWithStructuralClosure`，不得增加、拆分、移动或删除构件。
6. 每个 compact member 编译且只编译成一个 D1 brick；`BrickId == MemberId == LoadNode.MemberId`。
7. 计算 ActiveGeometry、BearingDAG、LoadDAG、BrickGeometry 和 ProductionIdentity Hash。

## 3. Fail-closed 合同

- Stage 4 必须 accepted，Roof/Crown hash 非零，且计划 member 数与 Stage-4 assembly member 数一致。
- 活跃构件必须保留有效 Assembly owner；suppressed 构件不得出现在 compact assembly、brick 或 DAG 中。
- 截面必须为 `36×36 cm`，轴向长度必须为 `36n cm`，禁止 diagonal，禁止正体积穿透。
- `ActiveMemberCount == CompactMemberCount == BrickCount == LoadNodeCount`。
- `BearingContactCount == LoadEdgeCount`，真实接触复算必须一致。
- Load DAG 必须无环且每个节点可达至少一个接地节点。
- Stage 5 不得改变 Stage 4.5 的活跃几何 Hash。
- 任何失败只报告当前固定建筑与明确门禁原因，不换 Seed、不进入参数重试。

## 4. 验收节奏

1. 先运行最小 E1，验证 compact/mapping/brick/contact/DAG 身份链。
2. 再运行冻结六栋清单，逐栋对照 Stage 4.5 active member count 和 geometry hash。
3. ForceUnity Development Editor 全链接。
4. 用户视觉检查 Stage 5 积木与 Bearing/Load DAG 诊断层后，才冻结 Stage 5。
5. 冻结后再进入 Chaos；Stage 5 静态通过不能替代物理稳定证据。

## 5. 长期不收敛防线

- 固定输入、单次派生、失败关闭，不因结构失败换 Seed。
- 第一处失败即停；先补可归因诊断，再决定修改合同或几何。
- 不以放宽 Bearing、可达性或一对一合同换取绿灯。
- 不在 Stage 5 调整 Stage 1～4 的形态和密度；若几何本身无法闭合，回到对应阶段形成独立修复与视觉复核。
