# M7.3 Beam-C3 V3 扩大 SharedEndpoint 检查点（2026-08-09）

## 1. 目标与边界

- 分支：`feature/m7-buildings`；起始 HEAD：`e6d74a7852efb91a846d89d1d548efe9ef67d10d`。
- 目标：保留 `1x1` WFC 可达性见证，但让固定 `SeamRelease.E6` 的桥两端生产芯体尽可能大。
- 不修改 WFC、Seed、36 cm 截面、密度、预算、容差或 720 cm；不运行 Stage 2+、5x6、BeamD15、Chaos 或可见 PIE。
- 用户修改的 `Content/Maps/PlanarPhysicsTestMap.umap` 未读取、未修改、未还原、未暂存。

## 2. 已失败且不得重复的路线

第一次实现把两端各自在 WFC 内扩大到近方形最大足迹，并允许既有 720 cm 分段器生成 tail。固定 E6 两次稳定出现：

```text
InvalidSupportedSpanContract: Course=44 Seats=1
```

第 21 节已经冻结“按同一 logical lane 的 segment 并集验证两端全部交点”，因此逐 segment 强制两个下承座是遗留验证器假设。改为 tail 至少一个真实座、唯一 cross-core segment 至少两个真实座后，包络合同通过，但静态 DAG 继续给出：

```text
BeamCSupportResultantOutsideHull
```

诊断固定出 8 个 252 cm tail。负端 tail 的合力约 `-1045.636/-1011.273 cm`，唯一支座区间为 `-1098..-1062 cm`；正端约 `-142.364/-176.727 cm`，唯一支座区间为 `-126..-90 cm`。因此它们是真实单支点悬臂，不是 DAG 漏边，不能靠放宽验证器通过。

日志：

- `Saved/Logs/BeamC3V3-ExpandedSharedEndpoint-Stage1-20260809-180641.log`；
- `Saved/Logs/BeamC3V3-ExpandedSharedEndpoint-Stage1-20260809-181246.log`；
- `Saved/Logs/BeamC3V3-ExpandedSharedEndpoint-ResultantDiag-20260809-182106.log`。

## 3. 当前生产合同

1. `SharedEndpointReachability` 继续只枚举 `1x1` 最小 cell，作为毫秒级 WFC 可行性证据；
2. 生产端点另行在 36 cm 网格内枚举矩形足迹，单轴最多 18 格，并逐 course 检查两条交替 X/Y rail 的全实体覆盖和接地 source；
3. 两端联合选择而非独立贪心：横向 stations 必须完全相同，且另一端必须存在达到同等共同短边的候选；
4. 排序为共同短边最大、长宽差最小、面积最大、opening inset 最短、横向居中、接触/tie-break；
5. 普通 `TowerChild` 在生产足迹冻结后规划，必须避让完整扩大足迹；
6. 当前还要求完整 shared lane 从一端最外物理端面到另一端最外物理端面不超过 720 cm，以单一 member 通过真实静态合力门。

通用超长 tail 数据结构没有删除，但在新增真实内侧承座或端部传力模型以前，不能由扩大桥端触发。不得用 logical lane 元数据把单支点 tail 伪装成稳定。

## 4. 当前验证证据

UE 5.8 ForceUnity Development Editor 增量链接成功。最终固定 E6 fresh NullRHI：

```text
ABTS.M73DAG.BeamC3V3.Staged.Stage1CoreAndSharedBoundary
Found=1 Passed=1 Failed=0
ProcessElapsed≈44 s, TestBody≈17 s
```

日志：

- `Saved/Logs/BeamC3V3-MaxSharedEndpoint-Stage1-20260809-182657.log`。

最终 ForceUnity Development Editor 全链接再次成功（14/14 actions，约 55 秒）。随后 fresh staged 宽过滤：

```text
ABTS.M73DAG.BeamC3V3.Staged
Found=7 Passed=7 Failed=0
Stage1SharedEndpointSize:Span=0:
  Negative=X=108.000 Y=108.000 Z=1728.000
  Positive=X=108.000 Y=108.000 Z=1728.000
Stage1: Cores=8 PairIntents=1 Shared=4 Members=793 StaticDAG=Accepted
Physical=NotEvaluated
```

固定 E6 的两端生产芯体均为 `3x3` 个 36 cm 网格，即 `108x108 cm` 方形足迹；不再把 `1x1` 最小可达性见证当成生产芯体。每条固定 E6 shared lane 也由一根不超过 720 cm 的实体 member 贯通两端，未产生单支点 tail。

日志：

- `Saved/Logs/BeamC3V3-MaxSharedEndpoint-Staged-20260809-183700.log`。

物理稳定性仍为 `NotEvaluated`。

## 5. 下一停止点

本检查点已经完成。立即停止并交给用户在 Editor 的 `Core + Shared Courses` 诊断层做视觉验收；不扩大到 Stage 2+、5x6、BeamD15、Chaos 或可见 PIE。
