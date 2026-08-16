# M7.3 Beam-C3 V3 最高合法语义基座检查点（2026-08-09）

## 1. 目标与边界

- 分支：`feature/m7-buildings`。
- 目标：把耦合基座从固定半高提升到 Shape Grammar/WFC 中最高合法的形态分隔，并保持每个上部
  高层投影都有独立接地 `TowerChild`。
- 不修改 Seed、36 cm 网格、720 cm 上限、积木粗细、预算、容差或 Chaos 参数。
- 本轮只验证 WFC 语义体积、Stage 1 芯体/shared course 和静态 DAG；不运行 Stage 2+、5x6、
  BeamD15、Chaos、可见 PIE 或 Editor。
- 用户修改的 `Content/Maps/PlanarPhysicsTestMap.umap` 未读取、未修改、未还原、未暂存。

## 2. 旧规则与禁止重复路线

旧规则取最短接地分支高度的一半，再向下量化为 72 cm course 对。它虽可重复，却不是 WFC 形态
分隔，导致共同基座只形成很低的一层。不得通过继续调半高比例、扫描高度、换 Seed、扩大容差、缩小
子芯体或放宽 720 cm 来接近截图中的分隔；这些做法都没有证明基座与上层载荷的语义关系。

首次选择最高 seam 后，固定 E6 的 Component 0 全部 6300 个普通 child 候选被旧
`FitsIncidentSharedReach` 拒绝。该 reach 已由专用 `SharedEndpoint` 接管，继续保留只会让普通高层
子芯体错误地靠桥口聚集。移除后 East 第二高层分区仍分成 lane conflict/no coupling；根因是主芯体
只按面积和中心选址，把唯一可用轨道放在塔体边界。联合预留后已有 604 个候选通过全部几何检查，
但最佳 Y station 为合法坐标 `-1`，被旧 `INDEX_NONE` 哨兵误判为未选择。

## 3. 当前冻结算法

1. 旧半高只作为下界/fallback；从非 Crown Body 起点由高到低枚举语义 seam；
2. 候选不得越过 incident span 下表面、吞入 Crown，且每个语义根上方必须保留两个完整 Body course；
3. 顶面向下量化到偶数个 36 cm course；选择后真实删除/重切下部 volume，插入 `CoupledGround`，
   重建连续 Volume ID 并重映射 span support IDs；
4. 主芯体发射前按第 24 节规则提取全部上部独立投影；每个主芯体候选必须在每个投影内部同时预留
   至少一个严格内置 X/Y station，然后才按尺寸、方正度、面积和中心评分；
5. 普通 `TowerChild` 只处理逐投影连续接地、lane、双向正交承压和 source 覆盖，不再承担 shared reach；
6. 候选存在性使用显式布尔状态；`-1` 等合法网格坐标不得与 `INDEX_NONE` 混用。

## 4. 当前验证证据

UE 5.8 ForceUnity Development Editor 全链接成功。最终固定 `SeamRelease.E6`：

```text
Arcology/Core LegacyTop=504  SemanticTop=1152 cm
Arcology/East LegacyTop=648  SemanticTop=2448 cm
Arcology/West LegacyTop=576  SemanticTop=1368 cm

Volumes=25
Cores=11
Main=3
Children=6
HighRegions=6
BoundHigh=6
PairIntents=1
Shared=4
Members=1533
StaticDAG=Accepted
Physical=NotEvaluated
```

Fresh 自动化：

- `ABTS.M73DAG.DAG5Bv2`：Found=8，Passed=8，Failed=0；
- `ABTS.M73DAG.BeamC3V3.Staged`：Found=7，Passed=7，Failed=0；
- 固定 `Stage1CoreAndSharedBoundary`：Found=1，Passed=1，Failed=0。

日志：

- `Saved/Logs/BeamC3V3-SemanticPodium-DAG5Bv2-20260809-1.log`；
- `Saved/Logs/BeamC3V3-SemanticPodium-CoupledGround-20260809-2.log`；
- `Saved/Logs/BeamC3V3-SemanticPodium-SeamReleaseE6-20260809-6.log`；
- `Saved/Logs/BeamC3V3-SemanticPodium-Staged-20260809-1.log`。

## 5. 下一停止点

本检查点已完成，停止在 Stage 1。用户下一步在 Editor 中分别查看：

1. `WFC Semantic Envelope`：共同基座应到达更高的 setback/塔身分隔，不填死 bridge opening；
2. `Core Placement / Pairing Intent`：主芯体位于耦合基座内，各独立高层投影都有对应意图；
3. `Core + Shared Courses`：主芯体止于新基座顶，所有上层子芯体仍从真实地面连续生成。

视觉批准前不继续 Stage 2+、5x6、BeamD15 或 Chaos。
