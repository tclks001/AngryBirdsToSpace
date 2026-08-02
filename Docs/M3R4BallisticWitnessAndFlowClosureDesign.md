# M3R-4 弹道 Witness 与流程闭环设计

> 状态：M3LocalAccepted（FixtureAuthority，IntegrationPending）
> 日期：2026-07-30
> 父文档：[M3R PCG 地图生成改进方案](M3PCGMapImprovementPlan.md)、[Task Graph 阶段 G](ABTSTaskGraphPCGDesign.md#12-阶段-gset-piece攻击解与资源经济)

## 1. 目标与非目标

M3R-4 在 M3R-3 的道路外六 Encounter 空间候选和 M3R-3.1 的普通弹弓槽场候选之上，新增不可变的 `Encounter Gameplay Finalize` 层。它负责证明：

1. 每个 Encounter 至少存在一条由批准输入域产生的 Positive Ballistic Witness；
2. E4/E5 的上一档能力在完整批准输入域内不能完成目标效果；
3. E5 的成功解对 M9 卫星引力具有因果依赖，而不只是调用过查询；
4. Start 到 Finale Launch 的 Key、物品、配方、奖励、桥门和 Exit 顺序无软锁；
5. 只有六关 Witness 与流程全部通过的 R3/R3.1 候选才能进入 R4 Top 3，并按稳定规则选择候选。

本阶段不修改 R1、R2、R3 或 R3.1 的结构、枚举、默认值、排序与 Hash；不修改共享 M5.1/M6、M7、M9、世界生成合同或地图资产；不把脚本化测试夹具声明为生产弹道权威。

## 2. 冻结输入与精确连接

R4 不按数组下标连接候选，必须同时匹配：

- `SourceRouteCandidateId`；
- R3 `SpatialCandidateHash` 与 R3.1 `SourceSpatialCandidateHash`；
- R3.1 `CandidateHash`；
- R3.1 `SourceSpatialResultHash == R3.SpatialResultHash`。

任何缺失、重复或 Hash 不匹配都 fail closed。R4 不重排、不修改源候选，只在新结果中记录源身份。

R3 当前冻结的是 M3-local Profile fixture。R4 必须保留每关已有的 `ResolvedFixtureProfileId`、Bounds 和 `ProfileCatalogHash`，再由外部只读目录补充 AttackFace/TargetProxy。若目录不能证明与 R3 空间目录兼容，不得在 R4 临时换型。

## 3. 服务边界

R4 通过依赖注入的纯 C++ 服务接口读取四类不可变数据：

- 服务身份：Authority、Solver/Geometry/Gravity/Profile/Progression/Bird Catalog Hash；
- Profile 目录：精确 ProfileId、Bounds、AttackFace、目标效果与最小撞击速度；
- Encounter 几何：全部槽位的精确弦连接点、Target Transform、禁止体积和练习卫星体积；
- 玩法数据：每档可用 Bird、纯数据轨迹评估、配方/奖励快照和桥门证据。

正常运行时没有集成适配器时，R4 保持 `NotEvaluated` 或 `ProviderUnavailable`，兼容世界仍可 Ready，但不得生成伪 Witness。

自动化使用 `Fixture` Authority 的脚本服务验证算法。其结果必须满足：

```text
bGameplayFinalizeValid = true
bExternalInputsCertified = false
bMonthlyWorldAccepted = false
```

当前 R4 v1 明确只接受 `Fixture` Authority 且拒绝任何 `bCertified=true` 声明；合成流程快照不能借用 `Integration` 标签取得外部认证。因此本阶段 `bExternalInputsCertified` 恒为 false。真实 M5.1 配方目录、M6 轨迹服务、M7 Profile、M9 重力快照与桥门/奖励适配必须在 Integration 工作树形成新适配身份并完成联合门后才可放开认证；`bMonthlyWorldAccepted` 还要等待 R6 与整体集成验收。

## 4. 发射输入域与预算

R4 使用 M6 的真实输入形态，不另造 yaw/pitch/speed：

- 槽场内所有满足最大弦长的无序槽对 `A < B`；
- 两个发射侧；
- `PullAlpha` 量化采样；
- 弹弓平面内 `AimRight/AimUp` 方形网格裁剪到单位圆；
- 当前 Tier 的全部可用 Bird。

默认采样为 7 个 PullAlpha、3×3 Aim 网格裁成 5 点。每个 Encounter 的当前 Witness 与上一档证书合计最多进行 8192 次轨迹求值；每次调用前检查预算。达到预算时输出 `SearchBudgetExceeded`，不能把未完成搜索记为无解。

轨迹结果至少包含有序且首样本时间非负的 Time/Position/Velocity 样本、终止原因、落点、求解器身份回显及 M9 查询统计。R4 对样本段执行连续线段/扫掠球判定，不能只测试离散点。`TargetHit` 必须与几何接触一致，`None/Invalid/未知枚举` 不得参与 Witness 或无解证书；撞击位置与速度取线段首次进入目标球面的交点，不能使用线段最近球心点或段末速度。

## 5. Positive Witness 与能力门

Positive Witness 同时要求：

- 命中本 Encounter 冻结的 TargetProxy/AttackFace；
- Bird 满足该攻击面的目标效果；
- 撞击速度达到门槛；
- 轨迹不穿其他建筑、卫星或禁止体积；
- Provider 回显身份与请求一致；
- Witness 输入、样本、命中、净空和身份均进入确定性 Hash。

E1–E3 使用 Simple，E4–E6 使用 Reinforced。E4/E5 生成 Simple 的 `PriorTierInfeasibilityCertificate`。v1 Bird 域冻结为 `Simple={Red,Blue,Yellow}`、`Reinforced={Red,Blue,Yellow,Black}`，完整目录 Hash 同时进入服务身份与无解证书；缺鸟、多鸟、重复鸟或未知档位均 fail closed。当前 M6 的 Simple/Reinforced 不提供不同速度曲线，因此本阶段不能伪造“同 Bird 只有强化档才够远”；本地能力门由 Profile 的 Black 专属目标效果与批准 Bird 全集共同证明。以后若 M6 提供档位化发射曲线，应升级服务身份后替换数据，不得静默改变证书输入域。

证书必须枚举完整批准输入域，记录计划数、完成数、输入域 Hash、最近失败裕量与 Solver Hash。E1–E3/E6 明确为 `NotRequired`。

## 6. E5 卫星因果证据

全部 R4 轨迹使用同一重力快照。E5 额外要求：

1. 成功轨迹有非零 M9 加速度证据；
2. 用完全相同的发射输入关闭 M9 后重新求值；
3. 消融轨迹不得命中，且距成功代理的失败裕量不小于配置门槛；
4. 两次结果与重力快照 Hash 一并进入 Witness。

练习卫星碰撞体属于禁止体积。该数据不写入 M11 Final Contract；M11 仍只使用主星与三颗终局行星。

## 7. 资源、桥门与线性流程

R4 使用 `EABTSItemId` 记账，数量不得为负。本地夹具按下列抽象主线验证 Key、奖励、桥门与终局材料闭包：

```text
Initial
 -> Workbench recipe
 -> Simple slingshot recipe
 -> E1 -> E2 -> E3 rewards
 -> Bridge recipe / bridge gate opens
 -> Reinforced slingshot recipe
 -> E4
 -> E5 grants SatelliteShotSolved + CrystalCore
 -> E6 grants finale materials
 -> Space stake/cord recipe
 -> Finale Launch
```

`HaveWood`、`HaveCrystalCore` 必须由物品阈值派生，不能无来源授予。桥门证据至少包含候选稳定 ID、Cut/Barrier 身份、Gate/Pre/Post Cell、桥前阻断、桥后可达、无绕行。R4 记录精确 15 步中的 Required/Granted Keys、RequiredStation、排序后的有符号物品增减和逐步 Ledger Hash，并从零库存重放到最终库存。Workbench/Furnace 可用性进入 Progression 与 Flow Hash；太空桩对固定消耗 `MetalParts 6 + Wood 5`，太空弦固定消耗 `MetalParts 2 + CrystalCore 1`，二者都要求 Furnace。

Workbench、Simple、Bridge、Reinforced 在 Fixture 中仍是用于验证流程机的合成步骤，并不声称等同当前 M5 制作目录。真实 `WorkbenchKit/SimpleStake/SimpleCord/FurnaceKit/BridgeKit/ReinforcedStake/ReinforcedCord` 的资源与站点时序属于 IntegrationPending；这也是 R4 v1 禁止 Integration Authority 认证的原因。

本期默认主线无支线：`BranchCount=0`、`BranchUtility=NotRequired`。不得把旧 Scout/Satellite 分支复制到月度结果。

## 8. 候选选择与 Hash

R4 对全部精确连接的 R3/R3.1 候选求解。仅完整硬通过候选进入 Top 3，稳定排序为：

1. `GameplayScore` 降序；
2. `SpatialScore` 降序；
3. `RouteScore` 降序；
4. `SourceRouteCandidateId` 升序；
5. 无符号 Candidate Hash 升序。

`SelectedCandidateId` 是源稳定 ID，不是数组下标。R4 发布独立 `GameplayLayoutHash`，不覆盖兼容 `PCGSummary.LayoutHash`。Hash 包含源身份、服务身份、Profile/AttackFace、六关 Witness/证书、流程账本、桥门、支线状态和最终选择；不包含耗时、日志开关、指针或内存地址。

## 9. 自动化与集成门

M3-local 自动化至少覆盖：

- 默认配置、Hash 与日志开关排除；
- 六关正 Witness、精确候选连接与 Profile 冻结；
- 非首槽对成功，证明搜索全部无序槽对；
- 上一档完整输入域与 8192 硬预算；
- E5 M9 消融因果证据；
- 资源、配方、奖励、桥门、Exit 与零支线闭环；
- 相同输入完整结构与 Hash 重复一致；
- Provider、目录、Profile、连接、预算、Bird 全集、轨迹终止、首次接触速度、M9、制作站、流程、桥门篡改均 fail closed；
- 修改 ItemDelta 后重算 Flow/Candidate/Layout/Result 全层 Hash，仍会被精确流程形状与账本重放拒绝；
- 冻结 100 Seed fixture manifest；
- R3/R3.1 展示 Seed Hash 与兼容世界身份不变；
- fresh runtime 默认 R4 pending，且不会声称真实 Witness。

## 10. 2026-07-30 本地实现与验收结果

M3 所有权范围内已经落地：

- 新增独立、不可变的 `FABTSM3MonthlyWitnessBuilder` 与 `FABTSM3MonthlyWitnessResult`；按 R3 稳定 `EncounterId` 和独立 `EncounterOrder` 精确连接 R3/R3.1 候选，不改写父层结构、排序或 Hash；
- 通过 `IABTSM3MonthlyWitnessServices` 注入 Profile、几何、Bird、轨迹、M9 与流程快照。生产默认无真实适配器时保持 `NotEvaluated`，兼容世界可继续 Ready，但不会生成伪 Witness；v1 构建与深验证都只接受未认证的 Fixture Authority，合成快照冒充 Integration 会以 `ProviderIdentityMismatch` 失败；
- 搜索全部可达无序槽对、两侧、7 个 Pull、5 个圆形 Aim 样本和批准 Bird 域；E4/E5 的 Simple 证书覆盖 `21 × 2 × 7 × 5 × 3 = 4410` 个输入，当前 Witness 与证书共享 8192 硬预算；
- Fixture 中 E4/E5 使用 Black 专属目标效果；Simple 仅有 Red/Blue/Yellow，Reinforced 才加入 Black，BirdCatalogHash 与证书绑定，不伪造档位速度差；
- 轨迹对终止枚举、首样本时间、几何接触和 Provider 回显 fail closed；撞击速度按首次进入球面的交点插值，已覆盖“进入球后才加速到门槛”的负例；
- E5 保存同一发射输入关闭 M9 的完整消融轨迹；流程闭包使用真实 `EABTSItemId`，验证候选绑定桥门、制作站可用性、E1 目标、E5 卫星与晶核、E6 前置、太空桩/弦真实终局配方和零支线；其余 M5 配方仍明确待集成；
- 只有完整硬通过候选进入 Top 3；结果发布独立 `GameplayLayoutHash`，不覆盖兼容 `PCGSummary.LayoutHash`；
- 父级 R3 现在从 Integration 的两项 V0 冻结工厂构造并签名 `FABTSM3FrozenCalibrationBatch`，以档位射程包络先做保守空间粗筛；R4 只验证该批次已通过 R3/R3.1 的精确来源身份继续传入，仍由注入的轨迹服务产生 Fixture Witness。批次不是 `GravitySnapshotHash`，也没有把 Fixture Authority 晋升为生产 M6/M9 Authority；
- `M3R4AcceptanceManifest` 冻结 21 个本地、父级、运行时及待集成验收入口。冻结身份为：
  - `Manifest=735D1CEB18102607`
  - `SeedManifest=5610DCBA0A03D9CB`
  - `DisplayConfig=E7831808F41259DA`
  - `DisplayResult=3F148C763A8AB08E`
  - `DisplayCandidate=2C9798D1B1BE3B14`
  - `GameplayLayout=919D8B8777E98DC5`
  - `SweepOracle=73E737B64B33E3BF`

自动验收结果：

- Development Editor 使用 `-ForceUnity -DisableAdaptiveUnity` 完整编译成功；
- `ABTS.M3.Monthly.EncounterWitness.0` 精确 8/8 Success；100 Seed 对全部精确连接候选执行正式默认输入域，`Terminal=100, Accepted=100, P95MS=363.757, MaxMS=401.359`；
- `ABTS.M3.Monthly.EncounterWitnessFailure` 精确 8/8 Success，覆盖 Provider、Catalog、精确连接、Profile 冻结、预算、Bird 全集、首次接触速度、非法终止、M9、制作站、流程与桥门故障；
- 父级 fresh 回归全部通过：SlotField `7/7 + 2/2`、EncounterSpatial `8/8 + 2/2`、Route `7/7 + 1/1`、Schema `8/8`、WeekOne `2/2`、WorldGeneration `2/2`、M11.0 分离 `1/1`；
- fresh `L_ABTS_M3 -ABTSM3R4Smoke` 输出唯一
  `Terminal=1 Passed=1 Failed=0 M3LocalAccepted=1 FixtureAuthority=1 IntegrationPending=1`，同时确认
  `GameplayFinalizeValid=0 ExternalInputsCertified=0 MonthlyWorldAccepted=0`，没有严重日志。

本次证据：

- `Saved/Logs/M3R4-Final-ForceUnity-20260730.log`
- `Saved/Logs/M3R4-Final-Core8-20260730.log`
- `Saved/Logs/M3R4-Final-Failure8-20260730.log`
- `Saved/Logs/M3R4-Final-Parent-SlotField9-20260730.log`
- `Saved/Logs/M3R4-Final-Parent-EncounterSpatial10-20260730.log`
- `Saved/Logs/M3R4-Final-Parent-Route8-20260730.log`
- `Saved/Logs/M3R4-Final-Parent-Schema8-20260730.log`
- `Saved/Logs/M3R4-Final-Parent-WeekOne2-20260730.log`
- `Saved/Logs/M3R4-Final-Parent-Contracts2-20260730.log`
- `Saved/Logs/M3R4-Final-Parent-M110Separation1-20260730.log`
- `Saved/Logs/M3R4-Final-Runtime-20260730.log`

本阶段退出状态为 **M3LocalAccepted（FixtureAuthority，IntegrationPending）**，不是 `IntegrationAccepted`。未来必须由集成工作树以真实 M5.1 制作目录替换合成流程快照，完成 M6 轨迹、M7 Profile、M9 重力、候选桥门/奖励的同一身份适配；本工作树完成 R6 后，再通过共享自动化和 Visible PIE，才能进行整体集成验收并晋升。
