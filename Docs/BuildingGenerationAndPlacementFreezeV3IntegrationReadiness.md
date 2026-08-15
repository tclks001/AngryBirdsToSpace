# Building Generation and Placement Freeze V3 集成准备与门禁

> 状态：`IntegrationScaffoldVerified / BuildingFreezeV3HandoffUnderReview`
>
> 日期：2026-08-15
>
> 基线：`master@36400f498cbfbbd6a18e3d64c45b5c6291d82915`

## 1. 目标与边界

本文是 [V3 实现与冻结计划](BuildingGenerationAndPlacementFreezeV3Plan.md) 的集成执行详稿，提前固定合同语义、负向矩阵、交接预检和最终证据层。它不发布 V3 DTO，不填写 M7/M3 最终 Hash，不切换生产版本，也不修改共同地图或默认绑定。

当前只允许：

- 审计三个功能分支的精确提交、所有权和二进制红区；
- 准备 V3 字段语义、fail-closed 测试矩阵和门禁清单；
- 验证已存在的 `Crystal -> CrystalCore -> SpaceCord` 纯事务链；
- 等待 M7 `BuildingFreezeV3` 精确 SHA 后再进入合同阶段。

## 2. 当前工作树快照

| 职责 | 当前精确 HEAD | 相对 `master` | 当前状态 | V3 结论 |
| --- | --- | --- | --- | --- |
| Integration | `36400f498cbfbbd6a18e3d64c45b5c6291d82915` | 基线 | 干净 | Crystal 共享基线已发布 |
| M7 | `8a4892d3928233721b47e7410fc9dddbfc63e08e` | 已含 Crystal 基线 | `BuildingFreezeV3` 已提交；`PlanarPhysicsTestMap.umap` 仍为工作树未提交修改 | 精确 SHA 已进入 Integration 候选审计，尚未提升为已验收基线 |
| M3 | `a6c2607e83b56fbacf251fb1489dcdd73eb0d934` | 未含当前 V3 基线 | 干净 | 正确等待 M7 bounds；不得先发布 V3 地图身份 |
| M11 | `56caafd1ffb74fc19aa9381251e6d18f54405428` | 独立推进 | 干净 | 不进入本轮建筑关键路径 |

该表只是 2026-08-15 的只读快照。每次集成必须重新运行：

```powershell
& .\Tools\Invoke-ABTSV3IntegrationPreflight.ps1
```

提供精确交接 SHA 时追加 `-M7Commit`、`-M3Commit` 或 `-M11Commit`。工具只读 Git、Manifest 和引擎路径，不切分支、不合并、不写资产。

## 3. V3 DTO 预备语义

### 3.1 版本策略

- V1/V2 的字段、冻结值和当前校验行为保持不变。
- `BuildingFreezeV3` 到达前不在稳定合同中声明可用 V3。
- V3 必须是加法式新版本；任何 V3 字段缺失都拒绝整份 V3 快照，不能回退 V2。
- `MapFreezeV3` 发布前 `activationAllowed=false`，生产合同仍是 V2。
- 最终默认版本只在 M7 producer、M3 placement、M7 consumer 和共同自动化可原子通过时切换。

### 3.2 提议字段

下列名称是集成设计名，最终 C++ 签名在 M7 交接后按真实类型落地：

| 字段 | 生产方 | 单位/域 | 失败策略 | Hash/序列化要求 |
| --- | --- | --- | --- | --- |
| `SurfaceKind` | M3 | `Unknown / PrimaryPlanet / Satellite` | `Unknown` 或与槽位不符即拒绝 | 枚举只尾部追加，数值进入 V3 身份 |
| `SupportCenterWorldCM` | M3 | 有限世界坐标，cm | 非有限或与 Transform 径向关系不符即拒绝 | 采用冻结量化规则进入 Placement Hash |
| `SupportRadiusCM` | M3 | 有限正数，cm | `<=0`、表面 Pivot 不一致即拒绝 | 与支撑球身份共同进入 Hash |
| `GravityAuthorityId` | M3/Integration | 非空稳定名称 | 未知权威或扫描 UObject 取得权威即拒绝 | 按稳定字符串内容 Hash，不使用进程内 `FName` 索引 |
| `GravityIdentityHash` | M3 | 非零 `uint64` | 与球心、半径、模型版本不一致即拒绝 | 消费者只比对，不从 UObject 重建 |
| `SiteLocalBounds` | M7 | content-to-site 转换后的有限 `FBox`，cm | 非法、退化或与冻结身份不符即拒绝 | 不允许 M3 从原始建筑宽深重新推断 |
| `SiteLocalBoundsHash` | M7 | 非零 `uint64` | 与 M7 handoff 不同即拒绝 | 由 M7 唯一生成，M3 只携带 |
| `PlacementHash` | M3 | 非零 `uint64` | Transform、Surface、支撑或 bounds 任一漂移即拒绝 | 每站点独立 Hash |

DTO 不保存 Satellite、Planet 或其他 UObject 引用。世界对象只负责在生产边界生成不可变值快照。

### 3.3 已发现的 V1/V2 兼容陷阱

当前 `FABTSJuryDemoFixedSixBuildingSite::IsUsable()` 强制 `DifficultyTier == EncounterIndex`，且 V2 冻结身份按 `EncounterIndex` 直接索引 E1→E6。V3 顺序固定为：

```text
Encounter 0  1  2  3  4  5
Building  E2 E3 E4 E5 E1 E6
Tier      1  2  3  4  0  5
Surface   P  P  P  P  S  P
```

因此阶段 3 必须：

1. 保留 V1/V2 的旧校验，不修改历史冻结语义；
2. 把共同基础合法性与版本专属身份校验拆开；
3. V3 按批准的槽位→Manifest 表解析，不用槽位猜建筑编号；
4. 难度从 Manifest 身份校验，不再要求等于遭遇槽；
5. E1 必须是 `Tier 0 + Encounter 4 + Satellite`，任一项漂移即拒绝；
6. `BuildingLocal +Y` 到 `Site +X Forward` 的转换只由 M7 producer 做一次。

### 3.4 Hash 输入顺序

最终算法由 handoff 提供，至少必须冻结以下顺序，且不能依赖 TMap/TSet 遍历或进程内 `FName` 索引：

```text
ContractVersion
PlacementSchemaVersion
WorldSeed / CandidateId
EncounterIndex
ManifestEntryId / DifficultyTier
Descriptor / StaticGeometry / Production identity
SiteLocalBounds / SiteLocalBoundsHash
WorldTransform
SurfaceKind
SupportCenterWorldCM / SupportRadiusCM
GravityAuthorityId / GravityIdentityHash
PadBounds / EffectBounds
PlacementHash
```

`LayoutHash` 按遭遇槽 0→5 顺序吸收六个 `PlacementHash`。任何未冻结量化方式的 float/double 都不能提前进入合同常量。

## 4. Fail-closed 测试矩阵

| 类别 | 注入 | 预期 |
| --- | --- | --- |
| 版本 | 未知版本、半填 V3、V3 字段全默认 | 整份 V3 拒绝；不回退 V2 |
| 顺序 | 缺项、重复项、交换槽 0/1 | 原子拒绝，无部分注册 |
| 身份 | Descriptor、Geometry、Production、Bounds Hash 任一 `+1` | 拒绝且日志指出首个漂移身份 |
| 映射 | E1 仍在槽 0、槽 4 不是 E1、Tier 跟随槽位 | 拒绝 |
| 表面 | E1=Primary、E2–E6 任一=Satellite、Unknown | 拒绝 |
| 支撑球 | 非有限球心、半径 `<=0`、Pivot 不在冻结球面 | 拒绝 |
| 重力 | 空 Authority、Hash 为 0、E1 使用主星重力身份 | 拒绝 |
| 朝向 | content `+Y` 未对齐 site `+X`、非正交/非右手基 | 拒绝 |
| Bounds | 非有限、退化、与 M7 Hash 不同、M3 重算不同 | 拒绝 |
| Pad/Effect | 静态 Bounds 越 Pad、Effect Envelope 未预留 | 拒绝 |
| 确定性 | 同 Seed/Candidate 连续两次结果不同 | 两次都不得发布生产身份 |
| 回收 | 未破坏先获得 CrystalCore、一次破坏发放不等于 1 | 拒绝/不发放 |
| 重放 | 同 cap damage/remove/replay 二次发放 | 必须被 cap 稳定身份账本拒绝 |

## 5. CrystalCore 资源链准备状态

当前已经有三层可运行合同：

1. `ABTS.M73A.CrystalMaterialBaseline`：Crystal Profile 和材质路径；
2. `ABTS.M8.Recovery.BuildingMaterialMapping`：`Crystal -> CrystalCore`，未知材料 fail closed；
3. `ABTS.M8.Recovery.CrystalCoreToSpaceCordTransaction`：无 CrystalCore 不可制作；一次回收允许一次制作并消耗核心；没有新核心不能再次制作；
4. `ABTS.M110.SpaceSlingshotItemContract`：SpaceCord 配方为 `MetalParts ×2 + CrystalCore ×1`。

第 3 项只证明共享库存/配方事务，不证明 E1 cap 的破坏事件已经 exactly-once。以下测试必须等待 M7 交付稳定 `CrystalCapIdentity` 后补齐：

- cap 未破坏时绝不发放；
- 首次权威破坏恰好发放一次；
- damage/remove/replay 使用同一 cap identity 时不重复发放；
- Preview/Test cap 不得写入生产库存；
- 遭遇完成奖励不得额外发放 CrystalCore。

## 6. 联合门禁清单

机器可读基线位于 `Tools/ABTSV3IntegrationGateManifest.json`。其中最终三个冻结身份保持 `null`，这是阻止占位 Hash 被误认成批准值的显式门。

只列出当前门禁而不启动 UE：

```powershell
& .\Tools\Invoke-ABTSV3CurrentContractGates.ps1 -ListOnly
```

在重型构建结束且没有其他正式门占用资源时，顺序运行所有 current gates：

```powershell
& .\Tools\Invoke-ABTSV3CurrentContractGates.ps1
```

每个过滤器使用新的 `UnrealEditor-Cmd` 进程和唯一日志。脚本要求退出码为 0、完成标记恰好一次、成功数等于 Manifest，任一不符立即停止。

### 6.1 构建与合同

- UE 只能来自 `C:\Program Files\Epic Games\UE_5.8`；
- 合入任意 C++ 后运行 Development Editor `-ForceUnity -DisableAdaptiveUnity`；
- fresh NullRHI 每个过滤器使用独立 `-AbsLog`；
- 同时验证进程退出码、完成标记和精确成功数；
- 当前可运行过滤器及计数以 Gate Manifest 为准。

### 6.2 冻结证据

| 冻结门 | 数据/自动化 | 实时/视觉 | 不得替代 |
| --- | --- | --- | --- |
| BuildingFreezeV3 | 六栋生成身份、材料、Crystal 唯一性、OBB、bounds | Crystal 像素、初始穿插检查 | 资产加载不等于像素；NullRHI 不等于 Chaos |
| MapFreezeV3 | 两次确定性生成、5+1 Surface/球心/Hash | 正式路线位置与月球背面 E1 | Preview/Test 不等于生产消费 |
| ChaosFreezeV3 | fixed-step Candidate/Result Hash | 30/60/120 FPS、hitch soak、可见 PIE | fixed-step 不等于实时 Chaos |
| Final Candidate | ForceUnity、共同合同、完整资源链 | `E2→E3→E4→E5→月球E1→E6` | 截图不等于事件顺序/确定性 |

可见 PIE 只有用户在当前任务明确授权时才能由 Codex 执行；否则交接必须标为 `VisibleValidationPending`。

## 7. 交接审计

### 7.1 M7 BuildingFreezeV3 必须提供

- 精确 SHA、base SHA、`Shared files changed: none`；
- 处理 `PlanarPhysicsTestMap.umap` 的明确决定，不能含混带入；
- 六条按 Manifest 的主材料、Tier、content-to-site 旋转；
- E1 唯一 Crystal cap 的尺寸、Transform、身份和结构排除项；
- 六套 Local/Physical/Effect Bounds 与全部 Hash；
- 两个 fresh 进程的相同结果；
- 新增/更新排错 ID；
- 明确 `Chaos=NotEvaluated`，不得把 Building Freeze 写成 Chaos Freeze。

### 7.2 M3 MapFreezeV3 必须提供

- 已合入包含 M7 freeze 和 V3 合同的 `master`；
- `[E2,E3,E4,E5,E1,E6]` 精确顺序；
- 五个主星 Transform 和一个月球 Transform；
- 六条 Surface、支撑球、Gravity、Pad/Effect/Placement Hash；
- 同 Seed/Candidate 两次独立确定性证据；
- 错轴、错 Surface、错槽位和错 bounds 失败注入；
- 不修改共享 `.umap`，除非另行取得唯一写入者并补共同地图门。

## 8. 排错账本增量候选

本节只做集成预审，不更新 `DevelopmentTroubleshooting.md` 的摘录基线。只有对应精确 SHA 合入候选后才允许上收。

| 工作树 | 自上次摘录基线后的重点候选 | 当前处理 |
| --- | --- | --- |
| M3 | `M3-WT-004`、`M3-JURY-004`、`M3-HISM-001` | 前两项涉及共享编译/合同身份；HISM 项涉及生产初始穿插。等待 M3 handoff 后判定是否提炼 |
| M7 | `M7-BC-119`～`125` | `124` 的旋转 OBB/SAT 分诊和 `125` 的证据身份降级最具跨阶段价值；旧位置 Chaos 结果不得进入 V3 |
| M11 | `M11-UI-010`～`013`、`M11-AUDIO-001`、`M11-CINE-001`～`004` | 与本轮建筑冻结解耦；待独立 M11 集成候选再摘录 |

当前 V3 最重要的共性结论是：一旦位置、支撑球或 Authority 改变，旧 Chaos 结果必须降级为诊断；不能用“同一几何”恢复旧实时证据。

## 9. 集成停止条件

出现以下任一项时停止进入下一冻结门：

- 功能交接修改共享合同、Config、默认绑定或共同地图；
- 精确 SHA 不在对应功能分支，或工作树含未解释二进制修改；
- M7 bounds/Hash 不完整但要求 Integration 填占位值；
- M3 重新推断建筑宽深、材料或 content 正面；
- V3 失败后回退 V2 继续生产；
- 只提供 Preview、旧日志、NullRHI 或截图，却声称实时 Chaos/生产/可见 PIE 已通过；
- 任何 Hash、Seed、Profile、Authority 或 Candidate/Result 身份不一致。

## 10. 本次集成准备验证

2026-08-15 在 Integration `master@36400f4` 完成：

- UE 5.8 Development Editor `-ForceUnity -DisableAdaptiveUnity`：`Result: Succeeded`；
- `ABTS.M73A.CrystalMaterialBaseline`：1/1；
- `ABTS.M8.Recovery.BuildingMaterialMapping`：1/1；
- `ABTS.M8.Recovery.CrystalCoreToSpaceCordTransaction`：1/1；
- `ABTS.M110.SpaceSlingshotItemContract`：1/1；
- `ABTS.Contracts.WorldGeneration`：2/2；
- Gate Manifest JSON 解析成功，两个 PowerShell 工具 AST 解析和 `-ListOnly` 自测通过；
- 交接预检以 M3/M7/M11 当前精确 SHA 运行：所有权错误 0，警告仅为当前 Integration 改动和 M7 正在进行的 V3/测试地图改动。

fresh 日志：

```text
Saved/Logs/V3-Prepared-ABTS.M73A.CrystalMaterialBaseline-20260815-215920-107-FreshAutomation.log
Saved/Logs/V3-Prepared-ABTS.M8.Recovery.BuildingMaterialMapping-20260815-215954-597-FreshAutomation.log
Saved/Logs/V3-Prepared-ABTS.M8.Recovery.CrystalCoreToSpaceCordTransaction-20260815-220021-958-FreshAutomation.log
Saved/Logs/V3-Prepared-ABTS.M110.SpaceSlingshotItemContract-20260815-220055-318-FreshAutomation.log
Saved/Logs/V3-Prepared-ABTS.Contracts.WorldGeneration-20260815-220151-952-FreshAutomation.log
```

这些结果只把当前合同/事务脚手架标为已验证；Building、Map、Chaos 三个 V3 Freeze 仍全部等待功能交接，且没有可见 PIE 授权或证据。
