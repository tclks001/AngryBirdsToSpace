# M7.3 Beam-C3 V3 Demand/Core/Coupling 诊断检查点（2026-08-11）

## 1. 恢复身份

- 工作树：`feature/m7-buildings`；起点 HEAD：`35b0ff3099ef77698e46e8ced6319a9f449799bb`；
- 阶段：Stage 1，只补 semantic demand→TowerChild→PodiumMain 对应诊断，不修几何；
- 用户资产：`Content/Maps/PlanarPhysicsTestMap.umap` 保持工作区原状，未读取、覆盖、暂存或还原；
- 证据层：UE 5.8 ForceUnity Development Editor 与 fresh NullRHI；未启动 GUI、可见 PIE 或 Chaos，
  `Physical=NotEvaluated`。

## 2. 新增诊断合同

`FSemanticDemandCoreBindingDiagnostic` 对每个 `SemanticTerminalDemand` 记录：

1. Demand/Component/Province/TerminalBodySource；
2. source lineage 或 XY overlap 可见的 HighRegion/child 候选数；
3. 确定性选中的 HighRegion、TowerChild、PodiumMain；
4. Body-child XY overlap、child center 是否在 Body、child 是否在 continuous fit；
5. child 与 main 是否存在最终 member DAG 重建得到的实际跨芯 bearing；
6. child 被多少 demand 复用、映射理由及 Demand/Child/Main bounds。

Summary 发布行数、unmapped、ambiguous、outside-body、no-direct-main、reused-child、orphan-child 和独立 Hash。
这些字段不参与任何几何 Hash，也不改变当前静态接受。

Editor 增加互斥层 `10 - Demand / Child / Main Ledger`。玻璃/石材/钢材薄板依次表示 demand Body、actual child、
assigned main；细线依次显示 demand→child→main。无直接 main bearing 的后一段加粗；完全无 child 候选的 demand
显示竖直错误标记。

## 3. TipOver E6 根因证据

三固定 Seed 专项通过。`750000` 保持旧几何身份：

```text
SemanticDemands=8 DemandCoreRows=8
Unmapped=0 Ambiguous=0 OutsideBody=1 NoDirectMain=3
ReusedChildren=1 OrphanChildren=1
FinalGeometryHash=-5115885695694694918
```

逐行证据显示：

- Demand 4（BodySource 19）和 Demand 6（BodySource 27）都沿旧 HighRegion source lineage 指向 Region 1 / Child 4；
- Demand 4 与 Child 4 的 XY overlap 为 0，child center 不在 Body，且不在 continuous fit；
- Child 4 的 demand multiplicity 为 2，因此另有一个已生成 TowerChild 没有任何 semantic demand owner；
- 3 个 demand 的 child 与 assigned main 没有最终跨芯 bearing，只是 Stage 1 最近-main 逻辑归属。

所以旧测试通过并不证明截图无误：它只证明 8 个旧 HighRegion 分别绑定 8 个独立接地 child；没有验证 8 个 semantic
demand 与 8 个 child 的空间双射，也没有要求 child 直接接触 main。

## 4. 验证

- ForceUnity Development Editor：成功；
- `ABTS.M73DAG.BeamC3V3.Staged.PreviewDiagnosticContracts`：1/1 Success；
- `ABTS.M73DAG.BeamC3V3.Staged.TipOverE6OptimizationSeeds`：710000/730000/750000 全部 Success；
- 日志：`Saved/Logs/BeamC3-DemandCore-Preview-20260811.log`、
  `Saved/Logs/BeamC3-DemandCore-TipOverSeeds-Final-20260811.log`。

没有重跑 5×6：本轮只增加只读 ledger，三固定种子的 FinalGeometryHash 保持不变；完整矩阵留到下一次实际修改
semantic-demand/child 选择后再运行，避免用昂贵矩阵替代针对性根因验证。

## 5. 下一停点

先在 Editor 选择第 10 层视觉核对 750000 的复用线、孤儿 child 和三条无直接 main 接触线。批准后，下一原子修改是
以 semantic demand 为权威建立 child 空间唯一绑定，再联合重选 main；不得先调 Seed、Attempt、36 cm、720 cm、
轨距、预算或容差。是否允许无直接 main 接触应等 Stage 2 耦合路径明确后另行冻结。
