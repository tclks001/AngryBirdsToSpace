# M7.3 Beam Stage 5 生产积木与承重 DAG 设计

> 状态：2026-08-15 固定演示六栋的 Stage 5 静态生产门已完成。Stage 4 曾按压实 DAG 证据窄范围重开一次并重新冻结。
> 本阶段只验证静态几何、真实接触 Bearing DAG 和 Load DAG；Chaos 明确为 `NotEvaluated`。

## 1. 目标与非目标

Stage 5 把 Stage 4 的程序化构件计划编译成真正可实例化的积木，并建立与这些积木一一对应的承重图。它不得重新生成建筑形态，也不得调用旧 `CompleteStaticDAG` 路径。若压实后暴露 Stage 4 前缀没有真实接地路径，只允许追加普通、可见、计入预算且进入真实接触 DAG 的生产闭合构件；禁止隐藏救援柱和声明式补边。

编辑器 `Generation Stop Stage = Stage 5`、PIE/runtime 初始化、自动化与离屏截图必须共享 `GenerateStage5()`。普通 PreviewActor 不得在 Stage 5 回退到旧 `Generate()`；生成拒绝时必须发布 `Stage5ProductionPreviewRejected`，成功时可见实例数必须等于生产 brick 数。

第一停点只覆盖冻结的六栋演示建筑，不运行 5×6 全矩阵、全 Seed 搜索或 Chaos。弱点、绳索、炸药桶和活塞装配不属于这一停点；所有生产积木的 weakness/device role 暂为 `None`。

## 2. 唯一生成顺序

1. 使用 Manifest 中固定的 Profile、Tier、Seed 重建已接受的 Stage 4。
2. 以 `bSuppressedByStage4FacadeToTop` 为唯一 suppression 权威，生成旧 Stage-4 member index 到 compact member id 的映射；被抑制项必须映射为 `INDEX_NONE`。
3. 只从活跃 `FPlannedMember` 重建 Joint、Member 和 Assembly 归属。compact `MemberId` 必须连续且从零开始。
4. 根据最终 36 cm 单位化 member AABB 重新计算 BearingContacts，不继承 suppression 前的接触边。
5. 对 compact assembly 运行 Beam-C 诊断；若存在由 suppression 后真实接触变化造成的不可达水平根，先在合法 36 cm 站位追加分段 Z 支撑，避开 `ReservedSupportVoid`，每段不超过 720 cm。
6. 对仍存在的承压/resultant 闭合缺口运行一次有界 `GenerateWithStructuralClosure`；新增构件必须是普通 Member，不得移动或删除 Stage 4 active 前缀，且最终数量不得超过 Profile 预算。
7. 从追加后的最终装配再次重建 Bearing 与 Load DAG。每个 production member 编译且只编译成一个 D1 brick；`BrickId == MemberId == LoadNode.MemberId`。
8. 分别计算 Stage 4 `ActiveGeometryHash` 与最终 `BearingDAG`、`LoadDAG`、`BrickGeometry`、`ProductionIdentity` Hash。Stage 4 前缀身份与最终生产身份不得混用。

## 3. Fail-closed 合同

- Stage 4 必须 accepted，Roof/Crown hash 非零，且计划 member 数与 Stage-4 assembly member 数一致。
- 活跃构件必须保留有效 Assembly owner；suppressed 构件不得出现在 compact assembly、brick 或 DAG 中。
- 截面必须为 `36×36 cm`，轴向长度必须为 `36n cm`，禁止 diagonal，禁止正体积穿透。
- `Stage4ActiveMemberCount == Frozen.ActiveMemberCount`，且 `ActiveGeometryHash` 必须继续匹配重新批准的 Stage 4.5 冻结值。
- `ProductionMemberCount == Stage4ActiveMemberCount + ReachabilitySupportPostCount + StructuralClosureMemberCount == BrickCount == LoadNodeCount`。
- `BearingContactCount == LoadEdgeCount`，真实接触复算必须一致。
- Load DAG 必须无环且每个节点可达至少一个接地节点。
- Stage 5 不得改变或重排 Stage 4 active 前缀；追加构件以独立数量和最终 Production Hash 发布。
- 任何失败只报告当前固定建筑与明确门禁原因，不换 Seed、不进入参数重试。

## 4. 验收节奏

1. 先运行最小 E1，验证 compact/mapping/brick/contact/DAG 身份链。
2. 再运行冻结六栋清单，逐栋对照 Stage 4.5 active member count 和 geometry hash。
3. ForceUnity Development Editor 全链接。
4. Spawn 一个 E6 PreviewActor 并选择 Stage 5，要求生产 hash 与直接 `GenerateStage5()` 一致、2433 个 production brick 全部成为可见实例。
5. 用显式离屏取证分别检查完整生产装配和仅新增闭合构件；E5/E6 不得出现轮廓外支撑林或跨越保留开口。
6. 两个独立 fresh 进程重放固定六栋，要求 Production Hash 稳定，再冻结 Stage 5。
7. 冻结后再进入 Chaos；Stage 5 静态通过不能替代物理稳定证据。

## 5. 长期不收敛防线

- 固定输入、单次派生、失败关闭，不因结构失败换 Seed。
- 第一处失败即停；先补可归因诊断，再决定修改合同或几何。
- 不以放宽 Bearing、可达性或一对一合同换取绿灯。
- 不在 Stage 5 调整 Stage 1～4 的形态和密度；若前缀本身制造假接地路径，回到对应阶段形成一次窄范围修复、离屏视觉复核和 Stage 4.5 重新冻结，随后立即返回 Stage 5。

## 6. 本轮完成值（固定演示六栋）

| 建筑 | Stage 4 active | Reachability | Structural closure | Production |
| --- | ---: | ---: | ---: | ---: |
| E1 | 52 | 0 | 0 | 52 |
| E2 | 238 | 0 | 25 | 263 |
| E3 | 373 | 2 | 37 | 412 |
| E4 | 890 | 0 | 40 | 930 |
| E5 | 1910 | 14 | 133 | 2057 |
| E6 | 2354 | 14 | 65 | 2433 |

六栋均满足 member/brick/node 一一对应、contact/edge 一一对应、零不可达、零环和 Profile 预算。E5/E6 已通过离屏完整装配与仅新增构件检查；Chaos 仍为 `NotEvaluated`。
