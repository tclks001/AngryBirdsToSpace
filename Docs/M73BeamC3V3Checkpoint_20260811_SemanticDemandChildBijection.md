# M7.3 Beam-C3 V3 Semantic Demand / Child 双射检查点（2026-08-11）

## 1. 恢复身份

- 工作树：`feature/m7-buildings`；起点 HEAD：`d9caff5b3ad51f27df837a06c63785f28f7fe4e5`；
- 阶段：Stage 1，以 semantic terminal Body 为权威修复 TowerChild 生成；
- 用户资产：`Content/Maps/PlanarPhysicsTestMap.umap` 保持工作区原状，未覆盖、暂存或还原；
- 证据层：UE 5.8 Development Editor 与 fresh NullRHI；未启动 GUI、可见 PIE 或 Chaos，
  `Physical=NotEvaluated`。

## 2. 根因与替换合同

旧 `HighProjectionRegion` 来自逐 course 几何 slice。两个独立 Body 在高处汇入共同 Crown 时，slice 血缘可以合并；旧生产
路径因此能把两个 semantic demand 指向同一个 TowerChild，同时保留一个没有 semantic owner 的孤儿 child。旧的
`High==Bound==Children` 计数和各 child 独立接地仍会通过，无法证明实际 Body 支撑完整。

新合同如下：

1. 每个 `SemanticTerminalDemand` 独立生成一个 required child demand；Crown merge 不得减少该数量；
2. child 固定 footprint 必须逐 course 连续容纳到 required top，并完整位于该 Body 的
   `ContinuousCoreFitBounds`；
3. semantic demand 在 main 枚举前完整存在，joint selection 必须为全部 demand 找到兼容 child；
4. `SemanticDemandId` 直接贯穿 region、full-height candidate 与 TowerChild；
5. 最终严格要求 Demand→Region→Child 一一对应。unmapped、ambiguous、outside、reused、orphan、top mismatch 或
   identity mismatch 均拒绝；
6. child→main 的真实 bearing 仍只诊断，因为 Stage 2 的耦合路径尚未冻结。

旧 terminal slice 仅提供 terminal course/component 的诊断可追溯性，不再通过 source overlap 猜测最终 child owner。

## 3. 代表种子结果

```text
TipOver E6 710000: Main=3 Child=8 Demand=8 Total=1424.48 ms
TipOver E6 730000: Main=3 Child=7 Demand=7 Total=1304.69 ms
TipOver E6 750000: Main=4 Child=8 Demand=8 Total=1644.62 ms
All: Unmapped=0 Ambiguous=0 Outside=0 Reused=0 Orphan=0
```

`750000` 中原先复用同一 child 的两个需求现在分别为：

- Demand 4 / BodySource 19 → Region 4 / Child 8 / Main 1；
- Demand 6 / BodySource 27 → Region 6 / Child 10 / Main 3。

两者均与自己的 Body 正面积重叠、中心在 Body 内且完整位于 continuous fit。主芯体数从 3 增为 4，是完整 semantic
demand 约束触发的联合重选结果。

## 4. 自动化证据

- UE 5.8 ForceUnity Development Editor 全链接：成功；
- `ABTS.M73DAG.BeamC3V3.Staged.SemanticSupportMergedRoofDemand`：1/1 Success；
- `ABTS.M73DAG.BeamC3V3.Staged.TipOverE6OptimizationSeeds`：1/1 Success，覆盖 710000/730000/750000；
- 受影响回归 5/5 Success：`GroundedPodiumCoreHierarchy`、`MultiPodiumMainCoverage`、
  `TerminalBranchDemandSplit`、`TipOverE6Seed710000TerminalCoverage`、`SemanticSupportMergedRoofDemand`；
- 日志：`Saved/Logs/BeamC3-SemanticBijection-Fixture-20260811-203903.log`、
  `Saved/Logs/BeamC3-SemanticBijection-TipOverSeeds-20260811-204326.log`、
  `Saved/Logs/BeamC3-SemanticBijection-Impacted-Final-20260811.log`。

日志启动阶段仍有目标测试入队前的既有 UnifiedError `Condition failed` 噪声；目标测试均明确发布
`Result={Success}`。没有运行 5×6：本轮先停在编辑器视觉验收，再决定是否进入完整矩阵，避免把昂贵矩阵放在可见
几何批准之前。

## 5. 下一停点

在 Editor 使用 TipOver E6 750000 对照诊断层 3、9、10：确认右侧两个 Body 各有独立 TowerChild，且新增第 4 个
PodiumMain 的位置合理；再抽查 710000 与 730000。视觉批准后运行 Stage 1-only 5×6。Stage 2、Beam-D1.5、Chaos
与物理参数不在本检查点范围。
