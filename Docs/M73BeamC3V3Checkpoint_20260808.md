# M7.3 Beam-C3 V3 接地芯体层级检查点（2026-08-08）

## 1. 状态摘要

- 工作树：`feature/m7-buildings`；检查点建立时 HEAD 为
  `e6d74a7852efb91a846d89d1d548efe9ef67d10d`，工作区包含本轮和此前未提交的 Beam-C3 修改；
- 本轮目标：用 WFC 耦合裙房容纳一个大而短的接地主芯体，为每个上部分支建立较小但较高的独立接地子芯体；
- 设计合同已写入 `Docs/M73BeamC3V3SkeletonFirstBuildingGenerationDesign.md` 第 19 节；
- 纯数据层级夹具已通过：1 个 `PodiumMain` + 2 个 `TowerChild`，三者都从真实 ground plane 开始，主芯体短、子芯体高，成员计数与各自 `TopCourseIndex * RailCount` 一致；
- DAG5Bv2 轮廓门 8/8 通过；Stage 1 宽过滤为 5/6；
- 当前唯一稳定失败：Seam Release E6 `Stage1CoreAndSharedBoundary`，
  `BeamC3V3EnvelopeViolation:Reason=InvalidCoreCourseContract:Member=421:Owner=0:Axis=0:Source=22`；
- 同一失败身份在独立 fresh NullRHI 中连续两次不变，已按防长期不收敛规则停止；
- 本轮未运行 5x6、BeamD15、Chaos、可见 PIE 或图形化 Editor。`PhysicalStability=NotEvaluated`；
- 受保护资产 `Content/Maps/PlanarPhysicsTestMap.umap` 未被本轮写入；当前 SHA-256 仍为
  `49A0DEE23E64CF5CDF8A14F9E36719224AEC13907777F8512540DE8ECF30FD5E`。

当前不是人工视觉验收入口：新层级夹具已成立，但 Seam Release E6 静态边界未恢复，不得宣称 Stage 1 全绿或进入 Stage 2。

## 2. 本轮冻结合同

1. 裙房顶高度不使用固定 `1000/300 cm`；由最矮原接地分支高度的 1/2 派生，量化为 72 cm 双 course，并在上下各保留完整 XY 对；
2. `PodiumMain` 只贯穿裙房；`TowerChild` 贯穿对应上部语义分支；两者都真实接地，不创建悬空细芯体；
3. 主芯体最大站距为 18 格，实体 member 最长 684 cm，低于 720 cm 硬门；非层级连续芯体和 shared 端点保留 12 格旧上限；
4. SupportedSpan 端点若需要贯穿到裙房以上，暂不启用分层芯体，保留原连续接地芯体和单一长 shared-course 槽位替换；
5. `Core Placement / Pairing Intent` 中主/连续芯体与子芯体使用不同诊断实例层；`Core Merge Regions` 显示完整裙房高度，不再只显示半层薄片。

## 3. 已实现文件

- `Docs/M73BeamC3V3SkeletonFirstBuildingGenerationDesign.md`；
- `Source/ABTSRuntime/Private/Building/ABTSM73DAG5BShapeGrammarV2.cpp`；
- `Source/ABTSRuntime/Private/Building/ABTSM73BeamC3V3SkeletonFirstTypes.h`；
- `Source/ABTSRuntime/Private/Building/ABTSM73BeamC3V3SkeletonFirstGenerator.cpp`；
- `Source/ABTSRuntime/Private/Building/ABTSM73BeamC3V3SkeletonFirstAutomationTests.cpp`；
- `Source/ABTSRuntime/Public/Building/ABTSM73BeamD1PreviewActor.h`；
- `Source/ABTSRuntime/Private/Building/ABTSM73BeamD1PreviewActor.cpp`。

## 4. 已通过证据

1. UE 5.8 ForceUnity Development Editor 全链接成功，最后一次耗时约 29 秒；
2. `ABTS.M73DAG.BeamC3V3.Staged.GroundedPodiumCoreHierarchy` 1/1 通过：
   `Saved/Logs/BeamC3V3-GroundedPodiumHierarchy-20260808.log`；
3. `ABTS.M73DAG.DAG5Bv2` 8/8 通过：
   `Saved/Logs/BeamC3V3-HierarchicalPodium-DAG5Bv2-20260808.log`；
4. `ABTS.M73DAG.BeamC3V3.Staged` 中前 5 项通过：`CoreMergeRegionMultiRail`、
   `FutureStageFailsClosed`、`GroundedPodiumCoreHierarchy`、`PreviewDiagnosticContracts`、
   `Stage0StopsBeforeMembers`。

## 5. 稳定失败的精确含义

`Member=421` 是 `CoreCourse`，`OwnerKind=CoreCell`，水平 X 轴，声明来源 `Volume=22`。
`Volume=22` 被当前 root 标记为 Crown，但成员 course 低于当前验证器用“整个 component 的 Body 最高点”计算的 `BodyCourseCount`，因此触发 `InvalidCoreCourseContract`。

这个失败不证明 member 越出 WFC 实体；它暴露的是新层级与旧 source-role 验证基准不同步：某个子芯体所属分支已从 Body 进入 Crown，但同 component 中另一分支的 Body 仍更高，使全局 `BodyCourseCount` 错把前者的合法 Crown 当成过早 Crown。

## 6. 已排除且不得重复的方案

| 试验 | 结果 | 结论 |
| --- | --- | --- |
| 把所有芯体的最大站距从 12 格放宽到 18 格 | Seam E6 失败为同一 `Member=421 Source=22` | 非原因；已将 18 格限定为 `PodiumMain`，不得再用宽度调参试验此失败 |
| 在收窄 shared 端点上启用短主芯体+子芯体 | 未执行 | 合同明确禁止；会破坏已验收的 shared 端点连续槽位 |
| 改 Seed、密度、预算、36 cm 截面或 Chaos 参数 | 未执行 | 与 source-role 验证失配无关，禁止尝试 |

## 7. 下一次唯一合理的修复切入点

不得放宽或删除 Crown 规则。应先为每个 `CoreCellPlan` 冻结它自己的 source-role 转换证据，例如 `BodyTopCourseIndex` 或逐 course `SourceVolumeId/Role`；然后将 `InvalidCoreCourseContract` 从“component 全局 Body 最高点”改为“该 core 对应分支在该 course 的真实 Body/Crown 归属”。

最小反例必须包含：两个上部分支共用裙房，其中一个较矮 Body 早进入 Crown，另一个 Body 继续升高。正例要求前者 Crown course 在实体并集覆盖下通过；反例要求在真实分支 Body 尚未结束时偷用 Crown source 仍失败。

修复后验证顺序只能是：新 source-role 夹具 -> 单独 `Stage1CoreAndSharedBoundary` -> `Staged` 宽过滤。三者全通过后再提供 Editor Stage 1 视觉验收；不提前运行 5x6 或 Chaos。

## 8. 证据日志

- 层级夹具：`Saved/Logs/BeamC3V3-GroundedPodiumHierarchy-20260808.log`；
- DAG5Bv2 8/8：`Saved/Logs/BeamC3V3-HierarchicalPodium-DAG5Bv2-20260808.log`；
- Stage 1 5/6：`Saved/Logs/BeamC3V3-HierarchicalPodium-Staged-20260808.log`；
- Seam E6 同身份二次复现：`Saved/Logs/BeamC3V3-HierarchicalPodium-SeamE6-20260808.log`。
