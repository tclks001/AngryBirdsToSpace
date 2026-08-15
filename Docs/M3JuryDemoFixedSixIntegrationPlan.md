# M3 Jury Demo Fixed-Six 集成计划

> 状态：2026-08-15，M7 静态生产封装与 V2 放置合同已进入 M3；六条 Fixture、动态 EffectBounds 预留及固定地图 V2 Layout 已达到 `M3LocalAccepted`，Integration V2 Adapter 已发布。ChaosReady 不作为 M3 放置验收条件。

## 1. 目标与边界

本阶段只为 DDL 评审路径冻结一张地图中的六个 Encounter，并把它们按顺序绑定到 M7 Stage 4.5 的六条建筑放置描述。M3 不再选择 Gameplay Profile、Weakness、AttackFace 或建筑 Seed，也不证明任意世界 Seed 都能容纳任意 M7 建筑。

固定身份：

- M3 World Seed：`312503`
- R3/R5 Source Candidate：`4`
- R3 Spatial Result Hash：`0x16A44AF72C58261E`
- R3 Spatial Candidate Hash：`0x645E131BE34A5B3E`
- M7 Placement Schema：`1`
- M7 Source Manifest Version / Hash：`1 / 2324068295`
- Fixed-Six Contract Version：`2`
- M7 Placement Catalog Hash：`11501529584318250152`（`0x9F9DA4381EEF7CA8`）

该身份是 `JuryDemoFixedSixV2`。运行时不得扫描其他 Candidate、递增 Building Seed 或替换 Manifest Entry。

## 2. 六条绑定

| Encounter | Manifest Entry | Stable Id | Tier | Seed | Required Pad Half Extent (cm) |
|---:|---|---|---:|---:|---:|
| E1 | `E1ColumnBreak` | `DemoE1ColumnBreak` | 0 | 710000 | `450 × 198` |
| E2 | `E2DropTrigger` | `DemoE2DropTrigger` | 1 | 740000 | `810 × 486` |
| E3 | `E3SlideRelease` | `DemoE3SlideRelease` | 2 | 750137 | `1062 × 450` |
| E4 | `E4TipOver` | `DemoE4TipOver` | 3 | 730000 | `882 × 414` |
| E5 | `E5SeamRelease` | `DemoE5SeamRelease` | 4 | 720000 | `1386 × 666` |
| E6 | `E6TipOver` | `DemoE6TipOver` | 5 | 750000 | `1098 × 522` |

Bounds、Static Geometry Hash、Descriptor Hash、Production Identity Hash、Device Assembly Hash、PhysicalBounds、EffectBounds 与 `bDynamicEnvelopeRequired` 同样保存在 M3 的固定 Fixture 中。六栋 PhysicalBounds 均等于 LocalBounds，静态 Pad 在 X/Y 两轴各保留精确 `36 cm`；六栋 EffectBounds 均至少在一个水平轴超出静态 Pad，因此动态包络不能合并进静态 Pad。

Fixture 是 M3 对已经合入 `master` 的 M7 V2 不可变事实的版本化副本，不形成 M7→M3 运行时反向依赖；真正导出到 M7 的 DTO 仍由集成工作树的稳定契约与 Adapter 提供。

## 3. M3 放置算法

`FABTSM3JuryFixedSixLayoutBuilder` 只接受上述固定 R3 结果和 Candidate：

1. 按 Encounter 顺序绑定六条 Fixture；
2. Pad 中心优先使用 Target Anchor；若真实 Pad 冲突，则按距 Target Anchor 从近到远、Cell Id 从小到大的稳定顺序检查本关既有 `TargetNoRoadCellIds` / `TargetFootprintCellIds`，不跨 Encounter 搜索；
3. 使用解析后 Pad Center 的球面法线作为 Up，并以该中心指向本关 Slingshot Anchor 的切平面方向作为建筑 Forward，不读取旧 AttackFace；
4. 用 Forward/Right 旋转真实 `RequiredPadHalfExtentCM`；
5. 在 Pad 的 `3 × 3` 固定采样点上寻找最近 Cell，形成排序去重的 `ReservedPadCellIds`；每个 Cell 必须非水域且不属于该 Candidate 的最终道路，旧 `TargetNoRoadCellIds` 只是已有保留区的子集；
6. 对独立的 EffectBounds 同样做旋转后的 `3 × 3` 水平采样，生成排序去重的 `ReservedDynamicEnvelopeCellIds`；动态样本必须非水域、非最终道路，且不能用增大静态 Pad 来代替；
7. 六个放置中心的球面距离必须大于静态 Pad 外接半径和动态 EffectBounds 水平外接半径中的较大者之和，再加固定安全边；
8. 对 Entry、Seed、Cell、基底、静态 Pad、动态包络预留和全部 V2 身份 Hash 计算 Placement Hash，再计算整个 Layout Hash。

任一身份或空间条件不满足均 fail closed，不回退旧 Fixture、其他 Candidate 或其他 Seed。
解析后的 `PadCenterCellId` 与 Jury 层派生的保留 Cell 一并纳入 Hash，但不回写旧 R3 Candidate、Spatial Hash 或 100 Seed 认证结果；集成工作树在消费放置 DTO 时必须让道路和装饰避开这些 Cell。

## 4. 工作树职责

### M3

- 生成并验证 `FABTSM3JuryFixedSixLayoutResult`；
- 保持道路、水体、Pad、路线顺序、揭示距离与终局分离；
- 提交共享契约需求，但不修改 `Public/Contracts/**` 或世界契约 Adapter。

### Integration

- 已以加法方式发布向后兼容的 Fixed-Six V2 字段，保留 V1 读取路径；
- 已按 M3 最终身份冻结 V2 Catalog/Layout Hash，并由 Adapter 精确导出 Contract V2；
- 维护默认地图、WorldReady 时序和联合验收。

### M7

- 按 `ManifestEntryId` 精确解析，校验 Tier/Seed/Hash；
- 禁止重新选 Profile、换 Seed 或 WFC 扫描；
- 五栋保持静态表示，当前 Encounter 的一栋按需动态化；
- ChaosReady 独立推进，不反向阻断已冻结的 M3 放置。

## 5. 验收

M3 独立门：

- `ABTS.M3.Monthly.JuryFixedSix` 精确 `3/3 Success`；第三项必须分别覆盖动态包络独占道路/水体冲突和建筑间动态包络过近；
- 同一输入重复构建得到相同六条 Placement Hash 与 Layout Hash；
- 错误 World Seed、Candidate Hash、缺失 Pocket、静态 Pad/动态包络命中最终道路或水域、动态包络过近均 fail closed；
- `-ForceUnity -DisableAdaptiveUnity` 与普通 Adaptive Non-Unity Development Editor 均完整链接；中间暴露的稳定 Adapter 显式 include 缺失已由 `master 3991723` 修复，详见 `M3-WT-004`；
- `ABTS.Contracts.WorldGeneration` 保持 `2/2 Success`，其中 `M3Adapter` 精确验证 V2 六站点导出、重复导出确定性和全部原子失败注入；
- `ABTS.M110.TaskGraphFinaleSeparation` 保持既有边界，最终由集成候选回归。

联合门由集成工作树执行：固定六条静态注册、逐栋按需 Chaos 激活、一次完整 E1→E6 fresh 可见 PIE。旧版全量 `ABTS.M7` 当前明确豁免，因为 JuryDemo 不再消费该泛化路径；Stage 4.5 和当前固定 Stage 自动化不能被豁免。

M3 本地固定地图 V2 证据：`Seed=312503`、`Candidate=4` 得到 `Buildings=6`、`ReservedPadCells=52`、`ReservedDynamicEnvelopeCells=40`、`LayoutHash=7029074579FDC52E`。两次 fresh NullRHI 重建得到完全相同的六条 Placement Hash 与 Layout Hash；E3 为满足动态包络从旧 V1 的 Pad Center Cell `702` 稳定解析到 `703`，其余结果可见日志 `M3Jury-V2-FixedMap-20260815-160534-710-FreshRuntime.log` 与 `M3Jury-V2-FixedMap-Repeat-20260815-160617-483-FreshRuntime.log`。Diagnostics 扩展后的 `3/3` 自动化证据为 `M3Jury-V2-Diagnostics-20260815-163413-442-FreshAutomation.log`；带真实地图精确身份门和 F7 计数的 fresh runtime 证据为 `M3Jury-V2-Diagnostics-FixedMap-20260815-163458-671-FreshRuntime.log`；稳定合同与终局边界 fresh 回归分别为 `M3Jury-V2-Diagnostics-Contract-20260815-163814-618-FreshAutomation.log`、`M3Jury-V2-Diagnostics-Finale-20260815-163850-384-FreshAutomation.log`，均为 `1/1 Success`。合入 V2 Adapter 后的最终合同与 M3 门分别见 `M3Jury-V2-Diagnostics-PostMerge-Contracts-20260815-164309-601-FreshAutomation.log`（`2/2`）和 `M3Jury-V2-Diagnostics-PostMerge-FixedSix-20260815-164354-852-FreshAutomation.log`（`3/3`）。

F7 Editor-only 诊断叠层使用 Cyan 显示静态 Pad 与静态预留 Cell、Green 显示 PhysicalBounds、Magenta 显示 EffectBounds 与动态预留 Cell、Red/White 显示 Target Anchor/最终 Pad Center。它只帮助联合 PIE 定位空间冲突，不进入 Placement/Layout Hash，也不改变 Preview/Test 与生产权威边界。

该结果身份是 `M3LocalAccepted / AdapterPublished`；稳定 Adapter 已发布合同 V2 并冻结 `LayoutHash=0x7029074579FDC52E`。在 M7 六栋注册与联合可见 PIE 完成前，不得提升为 `IntegrationAccepted` 或 `ChaosReady`。

## 6. 延后项

- Weakness、AttackFace、Failure Frontier；
- Profile × Tier × Seed 泛化目录；
- Prior-tier infeasibility 与完整 Ballistic Witness；
- M7 全种子可行性、六栋同时动态 Chaos；
- M3 1000 Seed / 20 Runtime Seed / 3 随机 PIE Seed 正式认证。

这些内容保留为长期设计历史，不构成 `JuryDemoFixedSixV2` 发布门。
