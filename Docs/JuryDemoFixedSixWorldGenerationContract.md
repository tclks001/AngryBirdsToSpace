# JuryDemo Fixed-Six V1/V2 世界生成合同

> 状态：2026-08-15，V1 J3 保持兼容；M3 V2、Integration Adapter、M7 六栋静态消费与共享 M6 联合门均已通过。canonical `L_ABTS_M10` 两次 fresh NullRHI 均以六栋 Accepted 发布 WorldReady；下一步回到 M7 做逐关动态 Chaos。
>
> 发布身份：`JuryDemoFixedSixV2 / StaticJointAccepted / ChaosNotEvaluated`；兼容身份：`JuryDemoFixedSixV1`。

## 1. 目标与非目标

本合同把 M3 已接受的固定评审地图放置结果，以只读值快照交给 M7。它只服务一个冻结的六关评审路径，不恢复旧版 Profile 搜索、AttackFace、Weakness、Failure Frontier、任意 Seed 可行性或全目录认证。

本期固定身份：

- M3 World Seed：`312503`
- M3 Candidate：`4`
- M3 V1 / V2 Layout Hash：`0x8AB8D7E4F094072D / 0x7029074579FDC52E`
- M7 Placement Schema：`1`
- M7 Manifest Version / Hash：`1 / 2324068295`
- M7 V1 / V2 Placement Catalog Hash：`13889440156022460967 / 11501529584318250152`
- M7 静态 Registration Result Hash：`3948236352584381910`
- 六栋静态模块：`5736` 块积木实例 + `6` 个静态装置 = `5742`
- 有序 Encounter：`E1`～`E6`，恰好六条

## 2. 兼容策略

现有 `FABTSBuildingGenerationContract::Identity` 与 `Sites` 保持 v1 语义不变。新增的 `JuryDemoFixedSix` 是可选、加法式快照：

- `ContractVersion == 0` 且其他字段为空表示普通 v1 世界没有发布 Fixed-Six；旧消费方可继续只读 `Sites`。
- 固定评审 Seed 一旦出现，就必须发布完整 `JuryDemoFixedSixV2`；不存在“部分填充后继续使用旧站点”的回退。V1 常量与校验继续保留给既有纯值读者和回归测试。
- M7 J4 必须显式要求 `JuryDemoFixedSix.IsUsable()`，不得在缺失或拒绝时改读旧 Profile/Seed 搜索路径。

该做法不新增 M7→M3 运行时通道，不暴露 M3 TaskGraph、Candidate 数组或可变 UObject，也不要求修改 M3 所有的 Planet 头文件。

### 2.1 V2 加法式迁移

V2 不修改 V1 常量；M3 Adapter 只在源结果明确声明 V2 且全部身份匹配时切换发布版本：

- `CurrentContractVersion` 继续为 `1`；当前已接受的 V1 Producer/Consumer 与 Layout `0x8AB8D7E4F094072D` 保持可用；
- `SupportedV2ContractVersion=2`，Placement Catalog 固定为 `11501529584318250152`；
- 每条站点追加 `V2Envelope`，包含 `StaticGeometryHash`、`ProductionIdentityHash`、`DeviceAssemblyHash`、`PhysicalBounds`、`EffectBounds` 与 `bDynamicEnvelopeRequired`；
- V1 必须保持 `V2Envelope` 为空；V2 必须携带 M7 J4 精确身份，静态 `PhysicalBounds` 必须等于冻结 `LocalBounds`，相对 Pad 的 X/Y 安全边均不得小于 36 cm；
- `EffectBounds` 是独立动态安全包络，不能并入静态 Pad。当前六栋均要求 `bDynamicEnvelopeRequired=true`；
- V2 Layout 固定为 M3 基于新 Catalog/Descriptor 与动态包络预留生成的 `0x7029074579FDC52E`，不得接受任意非零 Hash 或复用 V1 Layout。

合同校验按版本 fail closed：未知版本、V1 混入 V2 状态、V2 复用旧 Catalog/Layout、任一 Descriptor/Stage 5/Stage 5.5 Hash 漂移、静态 Bounds 漂移或动态走廊标志不一致都会拒绝整个快照。

## 3. DTO

合同级 `FABTSJuryDemoFixedSixContract` 包含：

- `ContractVersion`
- `PlacementSchemaVersion`
- `DemoManifestVersion / DemoManifestHash`
- `PlacementCatalogHash`
- `WorldSeed`
- `CandidateId`
- `LayoutHash`
- 六条有序 `Sites`

站点级 `FABTSJuryDemoFixedSixBuildingSite` 包含：

- `ManifestEntryId`
- `EncounterIndex`
- `WorldTransform`
- `PadHalfExtentCM`
- `LocalBounds`
- `DifficultyTier`
- `DeterministicSeed`
- `DescriptorHash`
- 可选 `V2Envelope`；只在 `ContractVersion == 2` 时必须完整存在

`FABTSJuryDemoFixedSixV2Envelope` 包含：

- `StaticGeometryHash`
- `ProductionIdentityHash`
- `DeviceAssemblyHash`
- `PhysicalBounds`
- `EffectBounds`
- `bDynamicEnvelopeRequired`

`WorldTransform` 的局部 `+X/+Y/+Z` 分别是 M3 已冻结的 Forward/Right/Up；Pivot 保持 M7 Stage 4.5 的生成器原点，不移动到 Bounds 中心。

## 4. M3 Adapter 原子导出

`AABTSM3Planet::TryExportBuildingGenerationContract()` 先在局部候选对象中构造旧 v1 站点，再对固定 Seed 执行以下门：

1. M3 `PlacementReady`、RejectReason、Schema、World Seed、Candidate、R3 Result/Candidate Hash 全部匹配；
2. M7 Manifest、Placement Schema 与 Catalog Hash 全部匹配；
3. M3 Layout Hash 等于冻结值，且由当前六条 Placement 重新计算仍一致；
4. 六条 Placement 按 Encounter `0..5` 排列，Entry 唯一；
5. 每条 Entry、StableId、Tier、Seed、Pad 和 Descriptor Hash 与 M3 已版本化的 Stage 4.5 Fixture 一致；
6. 每条 Placement Hash 可重算，Transform、Bounds 与 Pad 通过合同验证；
7. 完整 building contract 最终再次通过 `IsUsable()`。

任一步失败都返回 `false`，输出重置为空，并记录 `Authority=FailClosed`。不递增 Seed、不扫描其他 Candidate、不替换 Manifest Entry，也不返回已构造的旧站点作为半份成功结果。

## 5. 验收与证据层

J3 自动门：

- `ABTS.Contracts.WorldGeneration.Validation`：普通 v1 快照继续可用；Fixed-Six 的错误 Manifest Hash、缺条目、乱序、重复 Entry、错误 Layout Hash 与父子 World Seed 不一致均拒绝。
- `ABTS.Contracts.WorldGeneration.M3Adapter`：同一固定世界连续导出两次得到相同 V2 六条 Entry、Transform、三类身份 Hash、Bounds 与 Layout Hash；源版本、Manifest/Catalog、缺条目、乱序、重复 Entry、Layout、三类身份 Hash、两类 Bounds、动态标志及动态预留注入均原子拒绝。
- UE 5.8 Development Editor `-ForceUnity -DisableAdaptiveUnity` 全链接。

这些门只证明数据合同和导出生命周期，不证明六栋已经生成、静态注册、动态 Chaos 稳定或画面无重叠。

2026-08-15 候选证据：

- ForceUnity：17/17 actions，`Result: Succeeded`；
- `ABTS.Contracts.WorldGeneration`：2/2，日志 `Saved/Logs/J3-WorldGeneration-20260815-132439-934-FreshAutomation.log`；实际导出得到 `Seed=312503 / Candidate=4 / Buildings=6 / ReservedPadCells=52 / Manifest=1:2324068295 / Catalog=0xC0C1343B84C3D227 / LayoutHash=0x8AB8D7E4F094072D`，五类失败注入均输出 `Authority=FailClosed`；
- `ABTS.M3.Monthly.JuryFixedSix`：2/2，日志 `Saved/Logs/J3-M3FixedSix-20260815-132521-130-FreshAutomation.log`；
- `ABTS.M110.TaskGraphFinaleSeparation`：1/1，日志 `Saved/Logs/J3-FinaleSeparation-20260815-132600-091-FreshAutomation.log`；
- `ABTS.M73DAG.BeamC3V3.Demo.Stage45PlacementFreeze`：1/1，六条 Descriptor 与 Catalog Hash `13889440156022460967` 一致，日志 `Saved/Logs/J3-M7Stage45-20260815-132645-612-FreshAutomation.log`；
- 四份日志均有唯一 `EXIT CODE: 0` 完成标记，且 `LogAutomationController Error`、`LogABTSRuntime Error`、Fatal、ensure 与失败测试计数均为零。

2026-08-15 V2 Adapter 发布证据：

- M3 交接提交：`370e50e85c9977b0bf8e0f9ab81909becc12171f`；固定结果为 `Buildings=6 / ReservedPadCells=52 / ReservedDynamicEnvelopeCells=40 / LayoutHash=0x7029074579FDC52E`；
- UE 5.8 Development Editor 自适应非 Unity 与 `-ForceUnity -DisableAdaptiveUnity` 均 `Result: Succeeded`；非 Unity 首次暴露 Adapter 日志类别依赖 Unity 偶然带入，补充显式 `ABTSRuntime.h` 后两种编译均独立通过；
- `ABTS.Contracts.WorldGeneration`：2/2，包含 V2 合同常量门和 M3 Adapter 全部失败注入，最终源码日志 `Saved/Logs/Integration-M3V2-Contracts-20260815-R3-FreshAutomation.log`；
- `ABTS.M3.Monthly.JuryFixedSix`：2/2，日志 `Saved/Logs/Integration-M3V2-FixedSix-20260815-R2-FreshAutomation.log`；
- `ABTS.M110.TaskGraphFinaleSeparation`：1/1，日志 `Saved/Logs/Integration-M3V2-Finale-20260815-FreshAutomation.log`；
- `ABTS.M73DAG.BeamC3V3.Demo.Stage45PlacementFreeze`：1/1，日志 `Saved/Logs/Integration-M3V2-M7Stage45-20260815-FreshAutomation.log`；
- 四份 fresh NullRHI 日志均有唯一 `TEST COMPLETE. EXIT CODE: 0`，失败测试、项目 Error、Fatal 与 ensure 计数均为零。前两份初始 `-log` 预检运行没有测试完成标记，已明确作废且未计入证据。

2026-08-15 六栋静态联合门证据：

- M7 交接提交：`5b4755719b9d82d87303a6861bc35640c6184e3d`；只修改 M7 所有权文本文件，无共享文件和二进制资产进入提交；
- UE 5.8 Development Editor 自适应非 Unity 与 `-ForceUnity -DisableAdaptiveUnity` 均 `Result: Succeeded`；
- `ABTS.Integration.JuryDemoFixedSix.StaticJointGate`：1/1；真实执行 M3 V2 导出 → M7 六栋生产/静态注册 → M6 required-building 封口，并验证顺序漂移及自洽但错误的 Registration Hash 均失败关闭，日志 `Saved/Logs/Integration-M7V2-StaticJointGate-20260815-R1-FreshAutomation.log`；
- M7 `J4V2Consumer`：2/2，日志 `Saved/Logs/Integration-M7V2-J4Consumer-20260815-FreshAutomation.log`；J4 静态封口：1/1，日志 `Saved/Logs/Integration-M7V2-J4StaticSeal-20260815-FreshAutomation.log`；
- 世界合同：2/2，日志 `Saved/Logs/Integration-M7V2-WorldContracts-20260815-FreshAutomation.log`；M6 回归：4/4，日志 `Saved/Logs/Integration-M7V2-M6-20260815-FreshAutomation.log`；上述五份自动化日志均有唯一正常完成标记和零失败测试；
- canonical `L_ABTS_M10` 两个独立 fresh NullRHI 进程均得到 `Buildings=6 / Modules=5742 / Layout=0x7029074579FDC52E / ResultHash=3948236352584381910`、`BuildingContractSealed Expected=6 Registered=6 SetupRejected=0` 与 `WorldReady=1 BuildingAccepted=6 BuildingRejected=0`，日志 `Saved/Logs/Integration-M7V2-L_ABTS_M10-StaticJoint-20260815-FreshRuntime.log`、`Saved/Logs/Integration-M7V2-L_ABTS_M10-StaticJoint-Repeat-20260815-FreshRuntime.log`；
- 两次运行的 M3 地貌 HISM 预热候选数分别为 `0 / 3502`，但扫描总数均为 `8909`，且 Fixed-Six 六栋身份、计数、共享封口和 WorldReady 完全一致；该预热调度差异不属于静态 Registration 身份，也不作为 Chaos 证据。

该 DTO 固定六个纯值站点，只在导出时复制并验证，无 Tick、渲染、GPU、纹理、Sampler 或资产内存增量；J4 的 Actor/Chaos/帧时预算必须由 M7 和 J5 另行测量。

## 6. M7 J4 消费与 Integration 静态联合门

M7 已按以下边界完成 V2 静态消费；Integration 随后把同一批次接入共享 M6 门：

1. 只按 `ManifestEntryId` 解析 Stage 4.5 冻结目录；
2. 逐条比较 Encounter、Tier、Seed、Descriptor Hash，并比较合同级 Manifest/Catalog/Layout 身份；
3. 六条完成静态构建与空间注册后才允许 Fixed-Six WorldReady；
4. 仅当前 Encounter 切换为动态 Chaos，离场后按明确生命周期处理；
5. 任一不一致 fail closed，禁止换 Profile、递增 Seed、重新运行 WFC 搜索或回退旧 DAG2.3 三建筑合同。

M7 排错账本新增 `M7-BC-118`；共享 M6 门要求 E1→E6 顺序、共同 Registration Hash、`5742` 静态模块与六个 Accepted Actor 同时匹配，之后才允许封口。

## 6.1 V2 当前冻结事实

V2 的 Manifest 仍为 `1 / 2324068295`，Schema 仍为 `1`，Pad、Tier、Seed、Pivot 与静态 Bounds 不变。新的 Catalog、逐栋 Descriptor/Production/Device Hash 及 EffectBounds 以 [M7 JuryDemo 静态封口与 Fixed-Six V2 交接设计](M7JuryDemoStaticSealAndContractV2Handoff.md) 为唯一人读表；运行时校验中的精确常量由集成工作树所有的合同实现维护。

M3、Integration 与 M7 已输出并验证 V2 数据面及六栋静态注册，当前身份提升为 `StaticJointAccepted`。这仍不代表 `ChaosReady` 或最终 `IntegrationAccepted`；动态激活、离场清理、D3D12 与可见 PIE 必须另行取证。

## 7. J5 联合验收

静态联合门完成后继续串行执行：

1. ~~fresh NullRHI：六栋静态注册、重复加载身份不漂移、错误合同失败注入；~~ 已完成；
2. fresh 可见 PIE：`L_ABTS_M10` 完整 E1→E6，核对道路、河流、水体、建筑、出生区与终局区无重叠；
3. 每关只激活当前建筑的动态 Chaos，流程可继续，失败不遗留隐形碰撞或半注册 Actor；
4. 日志同时记录 World Seed、Candidate、Manifest/Catalog/Layout Hash、当前 Encounter、静态注册数与动态 Authority。

当前状态最多为 `StaticJointAccepted / ChaosPending`。只有逐栋动态门与上述剩余 J5 共同通过，才可提升为 `IntegrationAccepted`；`ChaosReady` 仍是逐栋独立证据。
