# M7.3 Beam-C3 V3 Terminal Load Branch Demand 检查点（2026-08-11）

## 1. 恢复身份

- 工作树/分支：`feature/m7-buildings`；本轮未移动 `master`。
- 阶段：Stage 1，仅修复 semantic support demand 到 TowerChild 的需求全集。
- 用户资产：`Content/Maps/PlanarPhysicsTestMap.umap` 保持用户工作区状态，未覆盖、暂存或还原。
- 证据层：UE 5.8 Development Editor 编译与 fresh NullRHI 静态自动化；未启动 GUI、可见 PIE 或 Chaos，
  `Physical=NotEvaluated`。

## 2. 根因

`TipOver.E6/750000` 的左侧 `BodySource=14` 是一个 terminal Body，但它在 Crown 层分成两条互不连通的终端荷载支路：

- South：node/source `15→16`；
- North：node/source `17→18`。

旧版“一 terminal Body 一 semantic demand”把这两条支路压成一个 child。Stage 7 画完整 DAG，所以显示三个左侧终端；
Stage 10 与实体只消费 demand，所以只显示两个 child。自动化只验证 Body demand 数与 child 数自洽，没有独立统计同一 Body
可达的 terminal load leaves，因而通过。

## 3. 修复合同

- demand 身份改为 `(TerminalBodyNodeId, TerminalLoadNodeId)`；同一 Body 的每个合格荷载叶必须有独立 demand、region、child。
- lineage 只包含 Body 到选定 load leaf 的那条分支，不能把 sibling Crown 合并成大包络。
- 只有高出 coupled podium 至少两个 36 cm course 的叶需要 TowerChild；其余图叶标记为 `MainCarried`，允许低 Tier 出现
  0 demand / 0 support province，而不是伪造 child。
- 汇总新增 `SemanticTerminalLoadBranchCount`、`MultiBranchTerminalBodyCount`、
  `UnrepresentedSemanticTerminalLoadBranchCount`；独立遍历得到的叶键集合必须与 demand 键集合严格相等。
- Stage 7 增加 terminal Body/load-leaf/demand 标记；Stage 10 以 load bounds 定位 demand plate。

## 4. 验证结果

- UE 5.8 Development Editor 普通编译成功；`-ForceUnity -DisableAdaptiveUnity` 全链接成功。
- fresh `ABTS.M73DAG.BeamC3V3.Staged.TipOverE6OptimizationSeeds`：1/1 Success。
- fresh `ABTS.M73DAG.BeamC3V3.Staged`：Found 44，Success 44，Fail 0，完成标记
  `**** TEST COMPLETE. EXIT CODE: 0 ****`；其中 5 Profile × 6 Tier 为 30/30。
- 750000：Main `4`，Children/Required/Bound/LoadBranches/SemanticDemands/BindingRows 均为 `9`；
  `MultiBranchBodies=1`，`Unrepresented=Unmapped=Ambiguous=Outside=Reused=Orphan=0`；算法总计 `1329.90 ms`。
- `BodySource=14` 的两个 demand 分别绑定 `LoadSource=16` 与 `LoadSource=18`，并生成独立 child。

日志：

- `Saved/Logs/BeamC3-EndpointClassification-Probe-20260811.log`
- `Saved/Logs/BeamC3-TerminalLoadBranches-TipOverSeeds-20260811-214022.log`
- `Saved/Logs/BeamC3-TerminalLoadBranches-StagedRegression-Acceptance-20260811-220048.log`

启动阶段仍会出现项目既有 UnifiedError 自测的 `Condition failed` 噪声；它发生在目标测试入队前。本轮以精确 Found、
每项 `Result={Success}` 和最终 EXIT CODE 0 三重闭合，不把启动噪声误报为目标失败。

## 5. 下一停点

在编辑器中用 `TipOver / E6 / 750000` 复核：

1. `Semantic Support Demand DAG` 显示 `BodySource=14` 上方 South/North 两个独立 load leaf；
2. `Demand / Child / Main Ledger` 显示两个不同 demand plate 与两条独立 demand→child 线；
3. `Core + Shared Rails` 中左侧原缺失位置出现第三个 child，右侧四个 child 保持不变；
4. 低 Tier 不应因合法的 0 TowerChild demand 出现伪造细芯体。

视觉批准前不进入 Stage 2 或 Chaos。
