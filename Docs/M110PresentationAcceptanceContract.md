# M11 PresentationAccepted 稳定验收合同

> 编码：UTF-8，简体中文。
>
> 所有权：Integration。状态：v1 合同实现完成；候选证据与生产绑定未完成。

## 1. 目标与边界

本合同把“严格玩法/物理拓扑认证”和“演示表现状态机可安全播放”拆成两个互不替代的结论：

- `PresentationAccepted`：完整输入域中的每个输入都进入合同支持的成功或失败恢复通道，身份、计划、播放和帧率结果确定；
- `StrictCertified`：继续由 M11-B 证明唯一成功族、助推质量、最小可玩宽度、Trust Region、桥区闭包和消融。

因此允许以下组合：

```text
PresentationAccepted + StrictUncertified
```

该组合是紧急演示/交付安全标记，不得改写为 `M11-B Certified`。本提交没有把 Rank12 绑定到生产；`GetFrozenProductionBindingV1()` 返回 `Unbound`，现有 StrictCertified v1 默认保持不变。

## 2. v1 路由语义

### 2.1 StrictF4Success

必须同时满足：

1. 三次助推完成，权威事件有限且顺序有效；
2. PlaybackPlan 已建立且与 Trajectory Hash 一致；
3. ShotPlan 已建立且与同一轨迹一致；
4. 终端表现和可见终端转移已建立；
5. 全时域没有 `Unavailable` 或 ordinary-flight fallback；
6. 最终进入 `TargetHit`，EndpointAuthority 为 `CandidateQualified` 或 `PhysicalContact`。

### 2.2 EarlyPhysicalContactSuccess

Rank12 精化中 `CompletedAssistCount=3 + physical TargetHit + 非 F4` 的输入归入独立的表现成功通道。它必须完成三次助推、完整 ShotPlan、终端表现和 `TargetHit`，EndpointAuthority 只能是 `PhysicalContact`。

该通道只承认现有状态机已经发生的物理接触表现；它不授予 `CandidateQualified`，不提升 F4，不产生 `CertificationHash/CertifiedBundleHash`，也不证明严格助推质量。少于三次助推的早碰撞仍拒绝。

### 2.3 DirectedFailureRecovery

三次助推事件足以建立 ShotPlan、但没有成功 EndpointAuthority/TargetHit 的输入，可以走导演可用的失败通道。必须完整观察：

```text
Failed -> Recovering -> Ready
```

不得出现 `Unavailable` 或 fallback。

### 2.4 OrdinaryFlightFallbackRecovery

少于三次完整助推事件时，M3 导演无法建立完整 ShotPlan。此时 `Unavailable` 只有在同时触发普通飞行 fallback、EndpointAuthority 为 `None`，且 `Failed -> Recovering -> Ready` 闭合时才是合法失败通道。

ShotPlan 已建立后发生的意外 fallback、任何失败通道获得成功 Authority、恢复未回到 Ready，均拒绝。

## 3. 全局拒绝条件

任一路由出现以下情况，整个候选 Manifest fail closed：

- NaN/Inf、事件乱序或权威数据非法；
- PlaybackPlan/Trajectory、ShotPlan/Trajectory 或 Evidence Hash 不匹配；
- 成功通道出现 `Unavailable`/fallback；
- 终端表现、终端转移或状态机结果不符合所属路由；
- 输入顺序不连续，候选/Policy 身份不匹配；
- 30/60/120 Hz 结果 Hash 任一为零或彼此不同；
- 完整域没有至少一个 `StrictF4Success`。

## 4. 身份、Manifest 与生产绑定

公共入口为：

```text
Source/ABTSRuntime/Public/Contracts/
  ABTSM11PresentationAcceptanceContract.h
Source/ABTSRuntime/Private/Contracts/
  ABTSM11PresentationAcceptanceContract.cpp
  ABTSM11PresentationAcceptanceManifest.cpp
```

冻结身份包含：

- Rank、GlobalWorkIndex、CandidateSource/Request/Result/Score Hash；
- Policy Hash、InputDomain Hash、PresentationImplementation Hash；
- 每个输入的 Launch/Trajectory/Playback/Shot/Terminal/Outcome/Evidence Hash；
- 30/60/120 Hz Replay Identity；
- 各路由计数、EvidenceAggregateHash 与最终 ManifestHash；
- 独立的 `StrictCertificationStatus`。

生产消费还必须匹配 Integration 冻结的 `FABTSM11PresentationProductionBinding`。候选自己生成的 `PresentationAccepted` Manifest 不能自授权生产。只有 M11 全域证据合并、Integration 联合构建与最终可见 PIE 都通过后，Integration 才能以单独提交冻结非零的 Rank/Policy/Candidate/Manifest/Binding Hash。

## 5. M11 工作树适配要求

M11 合并本合同后负责：

1. 从 `FABTSM11CandidateExperienceIdentity` 显式复制六个冻结字段，不修改候选 Catalog 或 Certified Bundle 字段；
2. 对完整规范输入域按固定 InputOrdinal 生成 route evidence；
3. 至少详细重放既有 `9,225 F4 + 305 EarlyPhysicalContact`，成功通道逐帧验证 ShotPlan、Basis、终端表现和 EndpointAuthority；
4. 对其他输入证明 `DirectedFailureRecovery` 或 `OrdinaryFlightFallbackRecovery` 闭合；
5. 以同一二进制和身份生成 30/60/120 Hz 结果 Hash；
6. 调用 `BuildManifest`，记录 Policy/Candidate/InputDomain/Implementation/Replay/Evidence/Manifest Hash；
7. 保持 Rank12 为 `StrictUncertified`，不得写入 `CertificationHash`、`CertifiedBundleHash` 或默认 CVar/生产资产；
8. 更新 M11 设计稿与 `M11WorktreeTroubleshooting.md`，交接精确提交和 fresh 日志。

## 6. 验证层级

Integration 本合同提交门：

- UE 5.8 Development Editor ForceUnity 全链接；
- fresh NullRHI `ABTS.Contracts.M11PresentationAcceptance`，预期 `3/3`；
- 既有 `ABTS.Contracts.WorldGeneration` 与 M11 快速合同回归。

本合同是纯数据层，NullRHI 不证明镜头画面。Rank12 最终放行仍需 M11 全域扫描、Integration 联合构建，以及用户授权后在 `L_ABTS_M11` 执行可见 PIE，确认成功终端和失败黑场恢复均与 Manifest 身份一致。

2026-08-14 的 Integration 合同提交已完成：

- UE 5.8 `-ForceUnity -DisableAdaptiveUnity` Development Editor 全链接：`Succeeded`；
- `ABTS.Contracts.M11PresentationAcceptance`：fresh NullRHI `3/3`；
- `ABTS.Contracts.WorldGeneration`：fresh NullRHI `2/2`；
- `ABTS.M110`：fresh NullRHI `4/4`；
- `ABTS.M11A`：fresh NullRHI `15/15`；
- `ABTS.M11B.Unit`：fresh NullRHI `9/9`；
- `ABTS.M11B.Runtime`：fresh NullRHI `6/6`。

本提交没有改变求解器、冻结布局或严格认证身份，因此不重跑 `ConstructiveSearch` 与 `Certification.FullInputDomain` 两项慢测；它们不能替代后续专门的 Rank12 Presentation 全域重放。

返回：[项目工作流](ABTSProjectWorkflow.md) · [多工作树规范](ABTSMultiWorktreeDevelopmentGuide.md) · [M11-B 严格认证](M11BFinaleLayoutCertificationDesign.md) · [M11-C 交互与实飞](M11CFinaleInteractionAndPlaybackDesign.md)
