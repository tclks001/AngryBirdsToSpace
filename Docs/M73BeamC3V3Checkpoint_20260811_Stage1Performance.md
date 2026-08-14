# M7.3 Beam-C3 V3 Stage 1 性能冻结检查点（2026-08-11）

## 1. 目标与边界

- 分支：`feature/m7-buildings`。
- 冻结基线：`83b1ebc621ec4303908b039a0b0a5f323973edbe`；六阶段计时门提交：`0cbe4d7`。
- 目标：在不改变 Stage 1 生成身份与几何合同的前提下，使每个 `CoreAndShared` 叶稳定低于 10 秒，并让 5 Profile × 6 Tier 可以用于高频算法验证。
- 不修改 Seed/Attempt 策略、36 cm 网格、720 cm 上限、轨数、间距、几何容差、WFC 包络、积木预算或物理参数。
- 不运行 Stage 2+、Beam-D1.5、Chaos、Editor 或可见 PIE。
- 用户修改的 `Content/Maps/PlanarPhysicsTestMap.umap` 未读取、未修改、未还原、未暂存。

## 2. 基线症状与根因

六阶段门首次将固定 `TipOver.E6/710000` 在 `10007.372 ms` 失败关闭：

```text
TerminalDemand       0.236 ms
ChildCandidate    1146.978 ms
PodiumMain        8860.000 ms
Joint/Emit/DAG       未开始
```

规模诊断随后证明 main 搜索枚举 582335 个矩形，产生 1611770 个不同长轨道查询和约 6477 万次缓存命中。所有候选都先完整扫描到裙房顶，之后才发现大多数无法进入每个 coverage/compatibility 桶的前 12 名。main 优化后，`SeamRelease.E6` 的瓶颈转移到生产级 shared endpoint：相同 footprint/source/course/rail 连续覆盖又被独立重复证明。

## 3. 当前冻结实现

1. 对每个 course/axis/cross-station 建立 36 cm 原子轨道覆盖前缀表，O(1) 回答任意合法长区间；原子固体无缝并集等于原长轨道固体。
2. 按半格中心、course 和 Body/Core selector 精确缓存 source；child/main/shared endpoint 的缓存作用域隔离。
3. PodiumMain 先计算 coverage、full-height compatibility 和排序键；已有 12 个候选均严格更优时才跳过全高验算，相等候选仍保留原流程。
4. 最终 child binding 复用 ChildCandidate 阶段已经证明的 source、top course、stations 和尺寸，不重跑同一 WFC 全高扫描。
5. shared endpoint 使用相同的精确 source/轨道缓存；评分和 bridge reach、720 cm、跨向 station、main coupling 合同不变。
6. 发布 `Stage1MainSearch`/`Stage1JointSearch` 规模诊断；10 秒门继续 fail closed，不因本次优化而放宽。

## 4. 验证证据

UE 5.8 ForceUnity Development Editor 全链接成功。

固定拓扑种子：

| Profile/Tier/Seed | Total | 身份 |
| --- | ---: | --- |
| TipOver E6 / 710000 | 1121.85 ms | Main 3、Children 8、Terminal 8/8、Members 1464、`Stage1Hash=6381458846136252022` |
| TipOver E6 / 730000 | 809.72 ms | Main 3、Children 7、Terminal 7/7 |
| TipOver E6 / 750000 | 840.95 ms | Main 3、Children 8、Terminal 8/8 |

fresh `ABTS.M73DAG.BeamC3V3.Staged.Stage1CoreAndSharedMatrix`：

```text
Found=30
Passed=30
Failed=0
Stage1 sum=32859.48 ms
Average=1095.32 ms
Median=956.73 ms
Maximum=3506.38 ms (SeamRelease E6)
Physical=NotEvaluated
```

日志：

- `Saved/Logs/BeamC3V3-Optimize8-Stage1-5x6-20260811.log`；
- `Saved/Logs/BeamC3V3-Optimize8-TipOverSeeds-20260811.log`；
- `Saved/Logs/BeamC3V3-Optimize8-SeamReleaseE6-EndpointPrefix-20260811.log`。

## 5. 停止点与下一步

性能步骤完成：当前 30 叶最大值低于 10 秒门约 6.49 秒，完整矩阵的 Stage 1 算法总计约 32.86 秒；Unreal 进程启动、资产加载和自动化调度另计，不能混入单建筑生成时间。

本检查点不宣告新的芯体形态已冻结。下一步回到设计稿第 36 节排期：建立 Body 支撑柱/Crown terminal 显式图和 support province，再实现局部裙房高度、均匀 PodiumMain 与“较粗主干 + 最多一次收缩”的 TowerChild，并在两个视觉停点分别验收。不得以本次性能绿灯替代后续视觉或 Chaos 证据。
