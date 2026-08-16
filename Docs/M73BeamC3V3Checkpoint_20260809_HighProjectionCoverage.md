# M7.3 Beam-C3 V3 高层投影逐区子芯体检查点（2026-08-09）

## 1. 目标与边界

- 分支：`feature/m7-buildings`。
- 目标：使耦合基座上方的每个独立高层投影分区都绑定一个接地 `TowerChild`；
  `PodiumMain` 仍只负责耦合基座，不随某一高塔向上延伸。
- 不修改 Shape Grammar/WFC 输入、Seed、36 cm 网格、720 cm 上限、砖块密度、预算或容差。
- 本轮只验证 Stage 1 几何身份和静态 DAG；不运行 Stage 2+、5x6、BeamD15、Chaos、可见 PIE 或 Editor。
- 用户修改的 `Content/Maps/PlanarPhysicsTestMap.umap` 未读取、未修改、未还原、未暂存。

## 2. 根因与禁止重复路线

旧实现对上层 Body seed 使用 `RootPath(DerivationPath)` 去重。不同 XY 位置的可见塔体只要共享同一
语义根，就可能只保留一个普通子芯体。已有静态验证从“已发射 core”向外检查自洽性，却没有从
“全部高层投影”反向检查 core 覆盖，因此 WFC 轮廓中心为空仍能假绿。

不得再次通过换 Seed、调密度、扩大合并 AABB、提升主芯体高度或修改 RootPath 字符串来回避。
这些做法都没有建立逐投影覆盖关系；其中提升主芯体还会破坏“主芯体只属于耦合基座”的合同。

## 3. 当前冻结算法

1. 在每个耦合基座顶面收集至少延续两个 36 cm course 的非 `CoupledGround` Body 投影；
2. 按 XY 正面积重叠或共享正长度完整边建立确定性连通分量，每个分量生成稳定
   `FHighProjectionRegionPlan`；
3. region 冻结 component、`PodiumTopCourse`、精确 source volume 集合、联合局部边界和绑定 core id；
4. 每个 region 独立生成一个接地 `TowerChild`，其基座顶以上首两个 course 必须完全由该 region
   自己的 source Body 覆盖；
5. `PodiumMain.TopCourseIndex` 必须等于 `PodiumTopCourse`；shared endpoint 仍是独立角色，
   不冒充普通高层分区；
6. region 数、已绑定 region 数和普通 `TowerChild` 数必须一致，region/core 双向引用、source
   唯一性和 component 归属全部失败关闭；canonical hash 包含完整 region 与绑定身份。

## 4. 当前验证证据

UE 5.8 ForceUnity Development Editor 全链接两轮成功：首次 8/8 actions，约 49 秒；合同收紧后
6/6 actions，约 42 秒。

共享同一旧 RootPath 的双塔 fixture：

```text
ABTS.M73DAG.BeamC3V3.Staged.GroundedPodiumCoreHierarchy
Found=1 Passed=1 Failed=0
ExpectedHighProjectionRegions=2
ExpectedTowerChildren=2
```

日志：`Saved/Logs/BeamC3V3-HighProjection-GroundedFixture-20260809-1.log`。

固定 `SeamRelease.E6` 与最终 fresh staged 宽过滤：

```text
ABTS.M73DAG.BeamC3V3.Staged
Found=7 Passed=7 Failed=0
Profile=SeamRelease Tier=5
Volumes=32
Cores=11
Main=3
Children=6
HighRegions=6
BoundHigh=6
PairIntents=1
Shared=4
Members=1165
StaticDAG=Accepted
Physical=NotEvaluated
```

日志：

- `Saved/Logs/BeamC3V3-HighProjection-SeamReleaseE6-20260809-1.log`；
- `Saved/Logs/BeamC3V3-HighProjection-Staged-20260809-1.log`。

固定 E6 的 2 个 `SharedEndpoint` 继续为 `108×108×1728 cm`，不计入 6 个普通高层分区；
总 core 计数为 3 个 `PodiumMain` + 6 个 `TowerChild` + 2 个 `SharedEndpoint` = 11。

## 5. 下一停止点

本检查点已完成。停止在 Stage 1，交给用户在 Editor 中对照 `WFC Semantic Envelope` 与
`Core + Shared Courses`，重点确认此前为空的每个独立高层轮廓中心现在都有接地子芯体，且主芯体
只覆盖耦合基座。视觉批准前不继续 Stage 2+、5x6、BeamD15 或 Chaos。
