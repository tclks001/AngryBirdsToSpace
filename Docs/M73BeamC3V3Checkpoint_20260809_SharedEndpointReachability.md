# M7.3 Beam-C3 V3 SharedEndpoint 可达性检查点（2026-08-09）

## 1. 结论

- 分支：`feature/m7-buildings`；起始 HEAD：`e6d74a7852efb91a846d89d1d548efe9ef67d10d`。
- 固定 `SeamRelease.E6` 的 `936 cm` 最小跨芯中段问题已经解决。
- 720 cm 硬门没有放宽；Seed、36 cm 截面、密度、预算、容差和 WFC 均未修改。
- Stage 1 现在生成两个按 span/side 明确归属的连续接地 `SharedEndpoint`，最终 shared pairing 不再消费普通 `TowerChild`。
- 固定 E6 Stage 1 静态 DAG 已通过；物理稳定性仍未评估。
- 用户修改的 `Content/Maps/PlanarPhysicsTestMap.umap` 未读取、未修改、未还原、未暂存。

## 2. 最小证伪结果

新增纯数据测试：

```text
ABTS.M73DAG.BeamC3V3.Staged.SharedEndpointReachability
```

它只读取固定 Shape Grammar/WFC 结果，不生成 core member、Beam-A IR、DAG 或 Chaos。结果：

```text
Span=0
Candidates=1264
NegativeBestInset=72.240 cm
PositiveBestInset=30.000 cm
Opening=-701.760..-408.000 cm
MinimumCrossSegment=468.000 cm
Reachable=1
ElapsedMs=82.957 / 104.917
```

因此旧 `936 cm` 不是 WFC 数学无解，而是 endpoint core 选址/身份错误，不需要回退 Shape Grammar。

## 3. 实现顺序

1. 在 WFC 上枚举 36 cm 最小接地 endpoint cell，逐 course 检查 Body/Crown rail 全实体覆盖；
2. `PodiumMain` 生成后、普通 `TowerChild` 之前，按 span/side 预留桥端 cell；
3. 普通 child 的 composite lane 选择必须避让预留 endpoint；
4. endpoint 自身独立接地，不强制与 PodiumMain 投影重叠；
5. 发射 endpoint 后记录 `SharedEndpointSpanVolumeId` 与负/正侧；
6. 最终 selector 精确优先该身份，再生成不超过 720 cm 的唯一 cross-core segment。

中间暴露并排除的失败身份：

- `SharedEndpointCompositeLaneUnavailable`：桥端在普通 child 之后才加入，生成顺序错误；
- `SharedEndpointReservationUnavailable`：误把“与 PodiumMain 投影耦合”当成桥端接地的前置条件；
- `SharedCourseCrossLaneUnavailable`：专用 endpoint 已生成，但最终 selector 仍选了普通 child。

这些都不是参数问题，之后不得以 Seed/密度/预算扫描重复试验。

## 4. 验证证据

ForceUnity Development Editor 最终链接成功：

```text
C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat
AngryBirdsToSpaceEditor Win64 Development
-ForceUnity -DisableAdaptiveUnity -NoHotReloadFromIDE
Result: Succeeded
```

fresh NullRHI：

- `SharedEndpointReachability`：精确发现 1，1/1 通过；
- `Stage1CoreAndSharedBoundary`：精确发现 1，1/1 通过；
- `ABTS.M73DAG.BeamC3V3.Staged`：精确发现 7，7/7 通过。

固定 E6 权威摘要：

```text
Profile=SeamRelease Tier=5 Seed=720000
GrammarHash=108351416
WFCHash=774350686
EnvelopeHash=1820105585257978029
Stage1Hash=-1034190928148718326
Volumes=32 Cores=8 PairIntents=1 Shared=4 Members=793
StaticDAG=Accepted LoadDAGHash=2068933772
Physical=NotEvaluated
```

日志：

- `Saved/Logs/BeamC3V3-SharedEndpointReachability-20260809-1.log`
- `Saved/Logs/BeamC3V3-SharedEndpoint-Stage1Boundary-20260809-5.log`
- `Saved/Logs/BeamC3V3-SharedEndpoint-Staged-20260809.log`

## 5. 停止点

本检查点只证明 Stage 1 的 WFC 可达性、专用桥端身份、shared course、静态 Bearing/Load DAG 和 720 cm 合同。
没有运行 Stage 2+、5×6、BeamD15、可见 PIE 或 Chaos。下一步是用户在 Editor 中视觉检查 Stage 1；视觉批准前不扩大验证范围。
