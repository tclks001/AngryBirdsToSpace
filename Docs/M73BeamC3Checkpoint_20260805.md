# M7.3-Beam-C3 暂停检查点（2026-08-05，历史冻结）

> 建立检查点时状态：**暂停继续修复，保留现场。** 当时最新复合回归中 E5（`ColumnBreak / Tier 4`）仍失败，
> E6（`ColumnBreak / Tier 5`）已通过。除下文列出的 1–3 个假设外，不再继续扩展修复路径。
>
> 工作树：`feature/m7-buildings`；建立检查点时的基线 HEAD：`c571817`。
>
> 当前静态收敛结论与剩余证据层见[第 10 节](#10-恢复后的续处理结论2026-08-05-追加)。

本文件冻结 [M73BeamC3CribCoreStabilityDesign.md](M73BeamC3CribCoreStabilityDesign.md) 建立检查点时的实现、
测试证据、失败现场和恢复边界。它不是 Beam-C3 完成声明；实时 Chaos 静置与攻击 PIE 仍未验收。

## 1. 目标、不变量与预算限制

Beam-C3 的目标是在不改变当前 Shape Grammar / WFC 主轮廓的前提下，把普通梁柱预算中的一部分
改写为可见的四柱井干式芯体，使低 Tier 和高 Tier 都不再依赖过长、无侧向约束的平行 Z 柱。
芯体、横隔、Portal 拉结和外围承重路径必须由最终 Brick 的真实 AABB 接触及 `BearingContacts`
证明，不得由语义标签、距离接近或虚构边补足。

冻结的不变量：

1. 不使用隐式铰链、锁定、胶合、不可见约束或斜撑；碰撞仍为真实积木碰撞。
2. 四柱井干芯体必须有闭合的 X/Y 横隔；缺柱、缺横隔、缺角部承接或单锚悬臂均失败关闭。
3. 普通楼层网只有在同一 `SourceVolumeId` 内形成至少两个独立根、同时包含 X/Y 两向且每一步
   都有真实承接面时，才可截断外围 Z 柱的无侧向支撑跨度。
4. Source-aware Portal 可跨 `BayId`，但不得跨 `SourceVolumeId` 借用承重进展。
5. `MaximumUnbracedCorePostSpanCM = 720 cm` 是最终装配中**全部原始 Z 构件/连续 Z 站位**的硬门；
   不因 Tier、视觉效果或候选难度放宽。
6. Tier 0 最终上限仍为 49 Brick，C3 累计净增上限仍为 12；Tier 1 最终上限仍为 199，
   C3 累计净增上限仍为 33。
7. 高 Tier 仍受各自 D1 最终 Brick 窗约束；本次 E5 上限为 1499，E6 候选可在其 Catalog 窗内闭合。
8. 所有修复和 donor 回收都必须事务化；失败不得把半成品写回最终 Assembly。

## 2. 已完成的源码与测试改动

建立检查点时的未提交源码已经实现：

- Catalog v8 四柱井干芯体、交错 X/Y course、四角 CorePost 与分层 Belt；
- 低 Tier 复用优先、普通 frame/roof donor 与 49/199、12/33 双重预算收口；
- 最终 all-Z 720 cm 审计，不再只审计带 `CorePost` 语义的构件；
- 普通楼层网的双根、双轴、同 Source 根系认证及证据 Hash；
- 同 Source 跨 Bay 的定向 Portal 拉结，目标端和锚端均要求实体承接；
- C2 后有界修复、最终 Assembly 再认证、失败事务回滚；
- Z 区间仅在重叠/接触，或存在 `Z -> 水平 course -> Z` 的真实 BearingContact 路径时合并；
- 初始 Host 事务化选择；donor 改为逐组事务化，并保护从 CoreCourse/CorePost 向地面回溯的
  support cone；
- D1 compiler、Beam-A/B/C、D0 Profile/Settings/Hash 与 C3 certificate 的生产链接入。

建立检查点时自动化测试文件定义 17 项。原有 14 项覆盖低 Tier 生产矩阵、确定性、四柱闭环、根系拉结、
fail-closed、C2 后修复、最终 all-Z 审计及事务回滚；当时新增但尚未正式执行的 3 项为：

- `SameSourceCrossBayTargetedTie`；
- `StructuralSourceProgressIsolation`；
- `RootedBiaxialFloorDiaphragmContract`。

## 3. 已保存日志与已通过门禁

完整日志已归档为：

- [M73BeamC3Checkpoint-20260805-FullLogs.zip](AIReports/M73BeamC3Checkpoint-20260805-FullLogs.zip)，
  SHA-256 `48388ACC19331D8CE61549CCEC6C05BD6B2219129A210BC405382BF7CEEC43C7`；包含旧 14 项
  套件、DropTrigger Tier 4 专项，以及 Biaxial、PhysicalBridge、SafeDonor、SameSourceDonor、
  LoadConeDonor、PartialSafeDonor 六轮 E5/E6 失败演进日志。
- [M73BeamC3Checkpoint-20260805-Logs.zip](AIReports/M73BeamC3Checkpoint-20260805-Logs.zip)，
  SHA-256 `5D432CA16C763212D7EF9C16D7C38E14E51849B17EA035DA4852E3C52435FEF5`；保留三个核心
  证据日志的便携子集。

已通过但必须按源码时点解释的门禁：

| 门禁 | 结果 | 唯一日志 | 适用边界 |
|---|---:|---|---|
| `ABTS.M73DAG.BeamC3` 原 14 项 | 14/14 Success | `BeamC3-Biaxial-20260805-130937.log` | 日志早于最后 donor/承接改动；不能代替当前源码 17/17 |
| `ABTS.M73DAG.BeamD1.ReportedAxisBalanceRegression` | Success | `BeamC3-HighTier-StrictRooted-Final.log` | `DropTrigger / Tier 4`，2297 Brick，`MaxAllZ=713.04` |
| `ColumnBreak / Tier 5`（E6） | 候选通过 | `BeamC3-ColumnHighTier-PartialSafeDonor-20260805-134205.log` | 复合测试整体因 E5 失败；E6 自身已有 `Certified` |

建立检查点时的 `UnrealEditor-ABTSRuntime.dll` 包含新增 3 个测试名，说明当时模块曾成功编译链接；但没有保存
与最新源码一一对应的正式 `-ForceUnity -DisableAdaptiveUnity` 构建日志，因此它不列为最终正式构建门。
建立检查点时源码的 fresh 17/17、完整生产矩阵和实时 Chaos PIE 均尚未执行。

## 4. 建立检查点时最新的 E5 / E6 结果与失败签名

当时最新命令：

```text
C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe
AngryBirdsToSpace.uproject -unattended -nop4 -NullRHI -NoSound -NoMessaging
-ExecCmds="Automation RunTests ABTS.M73DAG.BeamD15.ColumnHighTierClosure;Quit"
-TestExit="Automation Test Queue Empty"
```

该测试已经正常结束，没有 UnrealEditor 或 UnrealBuildTool 进程仍在运行；最终结果为
`Test Completed. Result={Fail}`，不是超时或中断。

### E5：`ColumnBreak / Tier 4`

E5 未认证。一个候选在 **C3 生成点** 已满足预算和跨度：

```text
Before=1369 After=1487 Net=118 Hosts=8 Belts=8 Rooted=109
Reused=74 Inserted=85 Donors=49
MaxBefore=1351.33 MaxAfter=713.42
Hash=960089118 RootedHash=1598040481
```

但该候选没有对应的 `[Certified]`。后续 C2/全局闭合先把装配降到 1135 个 Member，再重组到
1450+；随后 C3 修复与候选搜索继续。10 次候选最终失败签名为：

```text
BeamD15NoCandidateInBrickWindow:Attempts=10:
Last=BeamC3:BeamC3CoreBudgetInsufficient:1803>1499:Bricks=0
```

临近终态还有：

```text
BudgetDonorRejected Assembly=8 Members=22
Error=BeamC3BudgetDonorSpanExceeded:1871.68>720.00
```

因此本轮 **E5 的终态主阻塞是 1803 > 1499 的预算缺口**；`AnchorVerticalPathMissing` 和
`MissingPostBearing` 仍是大量候选级拒绝原因，但不能误写为本轮最后一条失败签名。

### E6：`ColumnBreak / Tier 5`

E6 已通过：

```text
[Generated] Before=2057 After=2304 Net=247 Hosts=11 Belts=17 Rooted=220
Reused=174 Inserted=53 Donors=0 MaxBefore=1860.27 MaxAfter=667.03
Hash=3180370346 RootedHash=3782347597

[Certified] Profile=ColumnBreak Tier=5 BaseSeed=710000 Attempt=5
ResolvedHash=1579154186 PlanHash=3180370346 EvidenceHash=3782347597
Bricks=2325 Rooted=220 MaxAllZ=667.03
```

## 5. 已尝试且证明不足的方案

| 方案 | 得到的结果 | 为什么不足/无效 |
|---|---|---|
| 只按距离把相邻 Z 区间合并 | 可降低报告跨度 | 会跨越真实空隙，把“接近”误当成承接；已改为 BearingContact 路径合并 |
| 仅恢复 `CorePost` 语义 | 部分候选重新被识别 | 不能重建已被 donor/C2 删除的实体下端点、承接面和向地根路径 |
| 批量删除普通 frame donor | 快速回收数百 Member | 删除了芯体的普通承重祖先，曾造成 `Unsupported=557` 与真实 `MissingPostBearing` |
| 同 Source / 跨 Bay Portal 与双根双轴楼面证据 | 解决部分外围柱缺横向约束 | 这是必要条件，但不能替代 anchor 端连续向地承接，也不能单独解决 E5 的 304 块预算缺口 |
| Full-section face、PhysicalBridge、Biaxial 逐项放宽候选覆盖 | `MissingPostBearing` 定位更精确 | 仍未保证闭合后的 member id、承接面和 support cone 同时保留 |
| support cone 保护 + 逐组事务 donor | E5 首个候选从 1836 降到 1487，且 `MaxAllZ=713.42` | C3 初始点成功，但 C2/全局闭合后没有最终认证；全搜索终态仍为 `1803>1499` |

不能采用的“修复”包括：放宽 720 cm、扩大 49/199 或 E5 1499 上限、把距离近似写成承接边、
跨 Source 借根、无实体 Bearing 的语义拉结，以及为通过预算而删除未证明可重路由的承重依赖闭包。

## 6. 当前最新假设：真实承接面 / Anchor vertical path 缺在哪里

需要区分两个层次：

1. **建立检查点时的终态阻塞是预算。** PartialSafeDonor 只把 E5 末端需求从 1836 降到 1803；剩余 304 个
   Member 没有可证明安全的 donor，不能以破坏 720 cm 或承接链换取。
2. **候选级高频阻塞仍是实体锚路径。** 典型拒绝为：

   ```text
   AnchorVerticalPathMissing:Tie=7 ... Evidence=11 Progress=0 Portal=0
   Station=-203.4,-250.4 Axis=0 Interval=489.4..1504.2
   ```

   这里水平候选和部分楼面证据已经存在，但 anchor 端没有一条由最终 `BearingContacts` 逐跳连接到
   Ground/CorePost 的连续竖向路径。最新假设是：C2/GlobalClosure 的 split、merge、prune 或 donor
   重写后，真实下承构件被删除，或者 member id / `HostCourseMemberIds` 仍引用闭合前身份；结果是
   `HasVerticalRootPathAtAnchor` 在几何可接近的站点得到 `Progress=0`。必须用逐跳 member/face 日志
   证明是哪一种，不能继续靠扩大搜索范围猜测。

## 7. 恢复后只允许验证的三个假设

1. **固定站点逐跳承接审计。** 对一个固定失败站点（优先
   `Station=-347.4,250.4 Axis=0 Interval=522.7..1504.2`）记录 anchor 到地面的每个 member id、Role、
   上下 Z face、BearingContact 与终止原因；先证明几何链究竟是否存在。
2. **闭合前后身份映射。** 若几何链存在，对比 GlobalClosure split/merge/remap 前后的 anchor member、
   HostCourseMemberIds 和 rooted evidence id，验证是否为旧 ID/角色集合导致假阴性。
3. **donor 依赖闭包与预算可行性。** 对每个候选 donor 同时计算上下承接面、祖先/后继 load cone 和
   删除后的 720 cm 结果；若不存在至少 304 个可安全回收 Member，则应直接得出“当前 E5 Catalog
   轮廓与 1499 窗不可行”，把问题返回 Profile/轮廓预算设计，而不是继续补启发式。

除上述验证外，暂停新增 Portal 搜索、容差、虚拟承接、特殊 Seed 或更多 donor 启发式。

## 8. 建立检查点前的全部未提交文件及来源

### 8.1 本次 Beam-C3 及其生产链接入

```text
Docs/M73BeamC3CribCoreStabilityDesign.md                         (new)
Docs/M73BeamCLoadDAGAndStaticProxyDesign.md
Docs/M73BeamD0GameplayProfileCatalogDesign.md
Docs/M73BeamD15VisualComplexityLadderDesign.md
Docs/M73BeamD1RealBrickAndMaterialRolesDesign.md
Docs/M7BuildingDevelopmentRoadmap.md
Docs/M7WorktreeTroubleshooting.md                               (mixed: C3 + prior D1 entries)
Source/ABTSRuntime/Public/Building/ABTSM73BeamAPreviewTypes.h
Source/ABTSRuntime/Public/Building/ABTSM73BeamD1Types.h
Source/ABTSRuntime/Private/Building/ABTSM73BeamAGenerator.cpp
Source/ABTSRuntime/Private/Building/ABTSM73BeamBGenerator.cpp
Source/ABTSRuntime/Private/Building/ABTSM73BeamCAutomationTests.cpp
Source/ABTSRuntime/Private/Building/ABTSM73BeamCGenerator.cpp
Source/ABTSRuntime/Private/Building/ABTSM73BeamCGenerator.h
Source/ABTSRuntime/Private/Building/ABTSM73BeamD0AutomationTests.cpp
Source/ABTSRuntime/Private/Building/ABTSM73BeamD0ProfileCatalog.cpp
Source/ABTSRuntime/Private/Building/ABTSM73BeamD0ProfileCatalog.h
Source/ABTSRuntime/Private/Building/ABTSM73BeamD1BrickCompiler.cpp
Source/ABTSRuntime/Private/Building/ABTSM73BeamC3AutomationTests.cpp       (new)
Source/ABTSRuntime/Private/Building/ABTSM73BeamC3CribCoreGenerator.cpp    (new)
Source/ABTSRuntime/Private/Building/ABTSM73BeamC3CribCoreGenerator.h      (new)
Source/ABTSRuntime/Private/Building/ABTSM73BeamC3CribCoreTypes.h          (new)
```

### 8.2 前序用户请求形成的 M7 改动

这些改动早于 Beam-C3，应与 C3 分开保存：

```text
Source/ABTSRuntime/Private/Building/ABTSM73BeamD1PreviewActor.cpp
Source/ABTSRuntime/Public/Building/ABTSM73BeamD1PreviewActor.h
Source/ABTSRuntime/Private/Building/ABTSM73StableBuildingActor.cpp
Source/ABTSRuntime/Public/Building/ABTSM73StableBuildingActor.h
Source/ABTSRuntime/Private/Building/ABTSM73BeamD1AutomationTests.cpp
Docs/M7WorktreeTroubleshooting.md                               (D1-008 / D1-009 entries)
```

它们分别对应 Beam-D1 Preview 在 PIE 中延迟依赖出现后的重试，以及 StableBuildingActor 是否参与
PIE runtime / 弹弓 validation gate 的两个开关。

### 8.3 明确属于用户的二进制资产

```text
Content/Maps/PlanarPhysicsTestMap.umap
```

该地图由用户在编辑器中修改，不属于本次 C3 checkpoint，不得与源码提交混入。Git 无法对二进制
内部内容做 hunk 归因；在用户明确要求前保持未暂存。

### 8.4 为本检查点新增的证据文件

```text
Docs/M73BeamC3Checkpoint_20260805.md
Docs/AIReports/M73BeamC3Checkpoint-20260805-Logs.zip
Docs/AIReports/M73BeamC3Checkpoint-20260805-FullLogs.zip
```

建立检查点时未发现其它未知来源文件、构建产物或日志进入 Git 工作区。

## 9. 恢复条件

恢复 Beam-C3 前先以本文件和归档日志复现 E5/E6；只执行第 7 节的 1–3 个证伪实验。若第三项证明
E5 在 1499 窗内没有安全 donor，下一步应调整 `ColumnBreak / Tier 4` 的轮廓/语义生成预算，而不是
继续在 C3 内增加特殊分支。完成 fresh 17/17、E5/E6、生产矩阵和 Chaos PIE 之前，不得把 Beam-C3
标记为完成。

## 10. 恢复后的续处理结论（2026-08-05 追加）

本节只追加恢复后的证伪和结果；第 1～9 节仍是当时的冻结现场，不回写为“从未失败”。其中 E5/E6
静态阻塞已解除，但完整 D1/D1.5、5 × 6 与 Chaos PIE 仍未完成，所以本文件仍不是 Beam-C3 动态完成声明。

### 10.1 恢复后先证伪且已撤回的路径

| 路径 | 结果 | 结论 |
|---|---|---|
| 候选 Gate、屋顶回滚、Host 深度 5→4、固定站位、多 Host 身份、按需求精确 Reserve、Bounds 1.20/1.21、MaxBays 5/4、复用 Tie 柱、Attempt 10/11、post-C2 临时事务 | 可改变局部数量或搜索顺序，但没有同时保住最终 DAG、真实 Bearing、720 cm 与 Brick 窗 | 均已撤回，不再以参数组合重复试验 |
| `TargetBaySpanCM=440`，以及只把 E5 调到 473 而不改变 Beam-C 实体闭合 | 473 能留下预算，但 Node 606 仍回到相同失败 DAG | 473 是最终方案的容量条件，不是单独的结构修复 |
| 相邻 Void cross-seat 上移两个 Brick 层 | 没有形成可认证 anchor/progress | 不再扩大 Portal/高度启发式 |
| 外侧单边 cross-seat | Beam-C 局部通过，但最终 `MaxAllZ` 退化到约 1585/808 cm | 不能用局部 DAG 通过替代最终全部 Z 站位审计 |
| 在 Attempt 0/2 批量给候选加平行 cap | 能改变部分 bearing，但没有先证明两条根系 lane | 证据不足的 bulk cap 会制造假承托，保持撤回 |

### 10.2 最终根因与有界修复

E5 最终失败集中在 Node 606：两条 25/75 全高 Z 根柱虽被尝试加入，却在 Beam-A 的 merge/split 后折入
既有分段柱，Member/Bearing/DAG Hash 不变。旧循环把 `Added=2` 当成进展，既无法到达缺失的水平 cap，
也可能无限重试同一结构身份。

最终修复由四个互相约束的部分组成：

1. Catalog v9 的唯一配置参数变化是把 `ColumnBreak / Tier 4` 的 `TargetBaySpanCM` 固定为 473 cm；
   Tier 5 保持 420 cm；CatalogVersion 升级仍会使全局 Catalog/Resolved 身份 Hash 改变，
   E5 因而能为最终实体 C2 cap 留出恰好 1499 Brick 的容量；
2. Beam-C 先按失败上梁的真实 footprint 尝试并证明精确 25/75 双 lane；该证据只跨一次权威 Beam-A
   重闭合存在。只有同一几何再次出现时，才允许在上梁下一截面生成平行 cap；短 course 还必须满足
   两 lane 的真实截面间距；
3. 无 void 的相邻重复 Hash 只有在上一轮留下精确 twin-lane 证据后才可生成平行 cap；`BestVoid` 不需要
   该证据，但两类路径都只在 `BridgeSeat + 2 BridgePost` 全部实际追加后消费 token，并在 Beam-A 重闭合前
   登记失败 DAG。任何已登记 Hash 都在下一次修改前以 `BeamCStructuralClosureNoProgress` 拒绝；
   `H1→H2→H1` 等非相邻循环也直接 fail closed，不能再用累计 `Added` 伪造进展；
4. C3 屋顶 donor 改为事务：删除、重闭合或重新认证任一失败即恢复 Assembly/Evidence/插入账本，随后
   才尝试 frame donor。D1 的所有候选拒绝统一记录 Profile/Tier/BaseSeed/Attempt/CandidateSeed/Gate/
   Reason 及实际 Member/Brick，认证日志同时记录 closure pass/add ledger。

### 10.3 当前静态证据

| 目标 | Attempt | Resolved / Plan / Evidence Hash | Brick / Rooted / MaxAllZ | Closure ledger |
|---|---:|---|---|---|
| ColumnBreak E5 / Tier 4 | 5 | `1404937059 / 1954804974 / 2654780606` | `1499 / 159 / 712.19` | `6 Pass / 33 Added` |
| ColumnBreak E6 / Tier 5 | 5 | `3234425960 / 3180370346 / 3782347597` | `2325 / 220 / 667.03` | `1 Pass / 15 Added` |

E5 与 E6 均在同一测试中重放，Resolved/Upstream/Core/Rooted/Brick Geometry Hash、Attempt、Brick 数、
Rooted、最大柱跨和 closure ledger 全部一致。
证据日志为：

- `BeamC3-ColumnHighTier-V9-PhysicalCommitFinal-20260805-1822.log`：`ColumnHighTierClosure` 1/1，E5/E6 均双次重放，并记录实体 grillage 的即时 DAG 提交；
- `BeamC3-Full17-V9-PhysicalCommitFinal-20260805-1821.log`：Beam-C3 17/17，并含 Catalog v9 低 Tier 5 × 2；
- `BeamC3-BeamC13-V9-PhysicalCommitFinal-20260805-1820.log`：精确 Beam-C 前缀 13/13；
- `BeamC3-D0-6-V9-PhysicalCommitFinal-20260805-1825.log`：D0 6/6。

相关 Unity 对象已直接重新编译并链接；随后安装版 UE 5.8 的
`Build.bat AngryBirdsToSpaceEditor Win64 Development -ForceUnity -DisableAdaptiveUnity` 全链接成功。
这不是清理全部 Intermediate 后的 clean rebuild，不应扩大为其它工作树或共享热点的 fresh 编译证明。

### 10.4 仍未完成与资产边界

- 未执行完整 D1、D1.5 与 5 Profile × 6 Tier 生产矩阵；
- 当前任务没有 GUI 授权，未启动可见 Editor/PIE，也未执行实时 Chaos 静置或攻击验收；
- `Content/Maps/PlanarPhysicsTestMap.umap` 仍是用户已有二进制改动，本轮未保存或覆盖、未暂存；
- 在上述证据补齐前，只能声明 E5/E6 静态收敛，不能声明 Beam-C3 动态完成。
