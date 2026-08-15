# M7 JuryDemo 静态封口与 Fixed-Six V2 交接设计

> 状态：2026-08-15，M7 静态封口已通过；等待 Integration 发布 Fixed-Six V2，Chaos 尚未开始。
>
> 上游：[M7 评委演示六栋垂直切片冲刺设计](M7JuryDemoVerticalSliceSprintPlan.md)
>
> 共享合同：[JuryDemo Fixed-Six 世界生成合同](JuryDemoFixedSixWorldGenerationContract.md)

## 1. 目标与边界

本阶段在开始完整建筑 Chaos 研究前，固定六栋演示建筑的跨工作树推进顺序，并用真实 Stage 5/5.5
生产结果验证最终静态积木、装置和效果走廊相对 Stage 4.5 `LocalBounds`、Pad 与 36 cm 安全边的关系。

本工作树只发布 M7 自有事实和 V2 需求，不修改 `Public/Contracts/**`、M3 Fixture、M3 Adapter、
M6 StartupPhysics、共同 WorldReady 门或 canonical `L_ABTS_M10`。本阶段不运行 Chaos，也不把静态封口
解释为物理稳定、爆炸/活塞效果或 `IntegrationAccepted`。

## 2. 固定工作顺序与工作树所有权

| 顺序 | 工作树 | 职责与停止点 |
| ---: | --- | --- |
| 1 | M7 | 计算六栋 Stage 5/5.5 最终静态物理 Bounds、Pad 余量和动态效果走廊；提交精确 Hash 与 V2 需求 |
| 2 | Integration | 以加法式 V2 扩展稳定合同；保留 V1，加入双版本验证，不在原版本下静默换 Hash |
| 3 | M3 | 合并 V2 基线，更新六栋 Fixture/Catalog，重新生成 PlacementHash、LayoutHash 与 Pad 预留 |
| 4 | Integration | 用 M3 最终 Hash 固定 V2 Adapter/合同常量，运行合同与 M3 Adapter 失败注入后发布 `master` |
| 5 | M7 | 合并 V2 `master`，实现精确 Manifest 消费、六栋静态生成、原子注册、回滚与 WorldReady 前置状态 |
| 6 | Integration | 合并 M3/M7 精确提交，处理 M6 共同门禁和 canonical `L_ABTS_M10` 静态联合验收 |
| 7 | M7 | 只在冻结包络内研究 Stage 5/5.5、材料、Solver、动态激活和装置效果；不改 Stage 4 放置前缀 |
| 8 | Integration | 串行完成 J5 E1～E6、单栋动态激活/离场、D3D12 与可见 PIE，决定 `IntegrationAccepted` |

若 Chaos 迫使修改 Stage 4 active 前缀、Pivot、Bounds 或 Pad，M7 必须停止动态研究并提交重新开冻需求；
不得在 M7 分支修改共享合同或让 M3 读取第二条数据通道。

## 3. 当前身份差异

已发布 Fixed-Six V1 仍引用旧 Stage 4.5 Catalog `13889440156022460967`。M7 在 Stage 5.5 前统一三轴
36 cm 边界格后，当前 Catalog 为 `11501529584318250152`，六栋 `StaticGeometryHash` 与
`DescriptorHash` 已随之改变。Bounds、Pad、Manifest、Tier 和 Seed 保持不变，但 M3 PlacementHash 和
LayoutHash 会消费 Catalog/Descriptor，因此 Integration 必须发布新版本，M7 不能把当前值伪装成 V1。

## 4. 静态封口判定

过滤器 `ABTS.M73DAG.BeamC3V3.Demo.J4StaticSeal.BoundsAndPad` 对 Manifest 顺序中的六栋逐项：

1. 解析当前 Stage 4.5 冻结 Descriptor；
2. 运行真实 `GenerateStage55DeviceAssembly()`，同时取得 Stage 5 Production 与 Stage 5.5 Device Hash；
3. 合并每块生产 Brick 和每个 Device 的已验证 `LocalBounds`，形成 `PhysicalBounds`；
4. 要求 `PhysicalBounds` 位于冻结 `LocalBounds` 内，且相对 Pad 四边仍至少保留 36 cm；
5. 合并 Device `EffectCorridorLocalBounds`，记录它是否位于当前 Pad；动态走廊超界只形成 V2 需求，
   不冒充静态积木超界，也不在本门中运行物理；
6. 输出 Entry、Tier、Seed、Catalog、Descriptor、Production、Device、Physical Bounds、PadMargin、
   EffectBounds 和 `DynamicEnvelopeRequired`，供 Integration/M3 按精确身份接收。

判定矩阵：

| 结果 | 所有权与后续 |
| --- | --- |
| Physical 在 LocalBounds 内且 PadMargin ≥ 36 cm | 静态世界包络可保持；只更新 V2 Catalog/Descriptor/Layout 身份 |
| Physical 超出 LocalBounds、仍在 Pad 内 | M7 提交新静态 Bounds；Integration/M3 重新冻结 V2 Bounds/Descriptor/Layout |
| Physical 超出 Pad 或安全边不足 | M7 提交更大 Pad 需求；Integration/M3 重新布局，未完成前不得进入 J4 |
| 仅 EffectBounds 超出 Pad | 静态 J4 可继续；若 M3 必须为爆炸/活塞避让道路或邻楼，V2 需新增独立动态安全包络 |

### 4.1 2026-08-15 实测结果

当前交接身份为 Manifest `Version=1 / Hash=2324068295`、Placement Schema `1`、Catalog
`11501529584318250152`。两个独立全新 NullRHI 进程对六栋逐项运行真实 Stage 5/5.5 producer，七条
Entry/Summary 证据逐字符一致；结果全部 `StaticAccepted=1`，每栋 X/Y PadMargin 都精确为 `(36, 36) cm`，
因此静态 `LocalBounds` 与 Pad 无需扩大。

| Entry | Descriptor | Production | Device | Physical Min / Max (cm) | Effect Min / Max (cm) | DynamicEnvelopeRequired |
| --- | ---: | ---: | ---: | --- | --- | ---: |
| E1 ColumnBreak | `10113758205408230493` | `6524532268529485689` | `12560907909080588493` | `(-414,-162,0) / (-90,162,648)` | `(-1102,-850,-670) / (418,670,850)` | 1 |
| E2 DropTrigger | `1108134973396587699` | `3864694895529971157` | `1033929311817437759` | `(-774,-450,0) / (486,450,1476)` | `(-1462,-1138,-670) / (58,382,850)` | 1 |
| E3 SlideRelease | `17683520519518435068` | `15118401498293854757` | `6073774060920401162` | `(-1026,-414,0) / (1026,414,1332)` | `(-1134,-522,-252) / (-774,-162,468)` | 1 |
| E4 TipOver | `11089610541129920709` | `3596567542130940914` | `3035395675580472088` | `(-846,-378,0) / (846,378,2376)` | `(-378,-486,-144) / (342,-126,216)` | 1 |
| E5 SeamRelease | `7322844578368466709` | `12062404675177644267` | `9042370151666144586` | `(-1350,-630,0) / (1350,630,2376)` | `(-1566,126,-144) / (-846,486,216)` | 1 |
| E6 TipOver | `3963542007450344969` | `10510335516369342439` | `1309116746468502251` | `(-1062,-486,0) / (1062,486,3384)` | `(-1278,-594,-144) / (-558,-234,216)` | 1 |

六栋效果走廊都至少在一个水平轴上超出现有 Pad，所以 V2 应把 `EffectBounds` 作为独立动态安全包络交给
M3/Integration 判断道路、邻楼和离场空间，不应把它并入静态建筑 Pad。当前证据只批准工作顺序第 1 步；
在 V2 和静态联合门完成前，M7 不开始 Chaos。

证据日志：`Saved/Logs/M7-J4StaticSeal-20260815-151735-FreshAutomation.log`、
`Saved/Logs/M7-J4StaticSeal-20260815-151935-FreshAutomation-Rerun.log`；两个进程均只发现并成功执行 1 项测试，
进程退出码与 Automation 完成码均为 0。

## 5. Chaos 期间的冻结纪律

- 材料、质量、Solver、激活时间、Stage 5/5.5 内部积木调整只更新 M7 `ProductionIdentityHash`、
  `DeviceAssemblyHash` 与 Chaos Candidate/Result Hash；只要不越过冻结包络，不触发 M3 重排。
- Stage 4 active 几何、Pivot、LocalBounds、Pad、Manifest 映射属于跨工作树放置前缀；修改任一项都必须
  显式重新开冻并走 Integration → M3 → Integration → M7。
- Chaos 研究期间的 Candidate Hash 可以变化，只有逐栋实时证据通过后才冻结最终 Chaos 身份；不得把
  NullRHI 静态门、编辑器预览或一次可见结果提前晋升为 `ChaosReady`。

## 6. 交接清单

M7 静态封口交给 Integration 时必须包含：

- 精确 M7 提交 SHA 与合入的 `master` SHA；
- Manifest Version/Hash、Placement Schema/Catalog；
- 六栋 Descriptor、Production、Device Assembly Hash；
- 六栋 PhysicalBounds、PadMargin、EffectBounds 与动态包络需求；
- ForceUnity 结果、fresh 自动化过滤器、成功数和唯一日志；
- 新增/更新的 M7 排错 ID；
- `Shared files changed: none`、`Chaos: NotEvaluated`。
