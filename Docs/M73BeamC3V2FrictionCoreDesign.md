# M7.3 Beam-C3 V2：纯摩擦分层芯体

> 状态：Stage 0 独立原型已实现并通过静态合同、E1 Brick 下界证明、固定 30 Hz
> Chaos 六秒静置与顶层向外 8 cm/s 扰动筛查。Stage 1 已选定“36 cm 四面接地耦合外框”作为
> 生产接入路线：先完成 DAG 与视觉门，再研究完整建筑 Chaos；当前尚未执行可见 PIE、视觉批准、
> 完整建筑物理承载或 5 Profile × 6 Tier 最终验收。
>
> 旧版四柱闭环设计保留为历史证据：
> [M73BeamC3CribCoreStabilityDesign.md](M73BeamC3CribCoreStabilityDesign.md)。新版不得继续沿用
> donor、Host、Portal、post-C2 repair 或 Catalog/Seed/Attempt 调参路线。

## 1. 设计问题重新定义

Beam-C3 V2 不再问“怎样让四根 Z 柱满足更多静态闭合条件”，而是先回答三个更基础的问题：

1. 只依赖轴对齐长方体、UE 几何/密度生成质量、碰撞和摩擦时，芯体自身能否静置并从固定小扰动中重新静置；
2. 某个几何在 Profile/Tier Brick 窗口内是否数学可行；
3. 增加 XY rail 或内部 Z 压杆时，究竟改善竖向承压、局部冗余、抗滑还是抗倾覆，不能把
   “接触点更多”统称为“更稳定”。

Stage-0 已用独立原型回答“该类摩擦堆叠并非立即散架”的最低可证伪前提；Stage 1 改为先验证
统一 36 cm 生产几何、DAG 和视觉语言。只有人工视觉通过后才为完整建筑建立 Chaos 门，因此这一
阶段不得把独立原型结果外推为成品建筑物理稳定。

## 2. 第一版冻结路线

以下路线不再作为 V2 的实现入口：

- 四根长/分段 Z 柱加水平 Belt 的直立矩形框；
- 在旧 C3 内继续调整 Host 数、Portal 覆盖、donor、Reserve、Bounds、MaxBays、Attempt、
  容差或特殊 Seed；
- 先运行 D1.5/5×6，再从二十分钟后的末项错误反推局部几何；
- 只在 D1 把芯体 Brick 放大，却让 Beam-A/C 的 AABB、Bearing、穿透和 Hash 继续按全局
  `36 cm` 计算；
- 用 `3×3` 个 `36 cm` 刚体束冒充一根 `108×108 cm` 真 Brick；二者对象数、接触和摩擦
  拓扑不同；
- 放宽 49/199/1499、720 cm、真实 Bearing、穿透或 Chaos 位移/转角门槛。

同一 `FailureSignature + GeometryCrc32 + FixtureCrc32 + candidate commit/binary identity` 在两个
fresh 进程中不变即淘汰该拓扑，不进行第三次同路线试验。两个 CRC32 是快速防误配身份，不是无碰撞
权威 Hash；共享 Cube BodySetup、全局 Chaos 配置和代码差异仍由提交/二进制身份兜底。

## 3. 文献证据、适用边界与项目推论

### 3.1 多 rail 的真实收益

美国矿务局的全尺度木 crib 试验比较了每层多根、正交交替的构型。所有正交 rail 完整交叉且保持
受压时，每层 `N` 根形成 `N²` 个几何承压区/理论 bearing column；2/3/4/5 rail 分别为
4/9/16/25。这不是 Chaos manifold contact 数。矿井上下压紧、可压木材的试验中，增加 rail 能
提高竖向承压刚度、分担局部缺陷并增加冗余。在相同接触面积的压缩试件对照中，更大的平面宽度/
惯性矩能降低屈曲，最外 rail 对惯性矩贡献最大；它对自由体抗倾覆的意义是本项目推论，仍须
Chaos 验证。
[RI 9341](https://stacks.cdc.gov/view/cdc/10471)

这不意味着接触数可直接换算为自由站立稳定性：

- 理想 Coulomb 模型中，在同摩擦系数、同总法向力且无开缝、冲击或材料咬合时，整层滑移上限
  仍为 `Σ μN_i = μW`；这是项目力学推导，多接触主要改善载荷分布。新增 rail 同时增重时，
  `W` 也会改变；
- 中央 rail 不扩大支撑多边形，抗倾覆边际小于外轨；
- 增加高度会降低承载和稳定；为增加接触面积而形成平行四边形，在早期可能略增承载，随后更
  不稳定。[RI 9168](https://stacks.cdc.gov/view/cdc/10486)

因此高 Tier 的顺序必须是“先由可用轮廓和目标高宽比确定外轨宽度，再决定内部 rail 数”，
不能在固定窄芯体中央不断塞砖。

### 3.2 内部 Z 只能是受围束压杆，不是钢筋

矿务局测试过在水平 crib 内放置全长竖柱的 `crib-post support`：内部柱可提高初始刚度和竖向
承载，外圈水平 crib 用于约束其屈曲；试件包含实心 cap，但报告没有无 cap 对照。竖柱屈曲、外圈
失去围束后承载下降。报告中的预制 mat 还显示，完全对齐的 block-on-block 峰后更易形成不连续柱
并屈曲/卸载，与水平 slab 交叠的短块能维持更大的位移范围。预制、受压夹紧的 mat 不能直接证明
本项目无连接散块同样成立。[RI 9494](https://stacks.cdc.gov/view/cdc/10088)

它不能称为“钢筋”。真实钢筋通过肋纹机械咬合、承压、锚固长度和围束把拉力传入混凝土；无胶、
无连接的 Z Brick 不能跨越张开的接缝传拉力，只能在压紧接触存在时传递法向压力和不超过
`μN` 的切向摩擦。
[FHWA 钢筋粘结研究](https://www.fhwa.dot.gov/publications/research/infrastructure/structures/bridge/14090/003.cfm)

矿井试验还利用了木材顺纹/横纹各向异性，而当前 Chaos 材料是各向同性 Profile，不能直接继承
其承载倍率。V2 的 Z 候选必须逐帧记录底/顶承压、侧向接触、开缝、位移、倾角和 cap 失接；
底/顶承压路径失效、柱位移/倾角超门或越过预登记 cage clearance，且结果不优于纯 XY 对照时
淘汰。外笼可以在柱偏移后才接触，不要求侧面逐帧贴合。

### 3.3 自由站立不能直接外推矿井预压结果

矿井 crib 被顶板与底板夹紧；Beam-C3 主要只有自重预压。作为另一种不同工况，USACE 海岸防护
timber crib bulkhead 使用石料填充，并允许角部螺纹拉杆；这些增强都不在本项目允许范围内，不能
把其稳定性外推到空芯自由体。
[USACE EM 1110-2-1614](https://www.publications.usace.army.mil/Portals/76/Publications/EngineerManuals/EM_1110-2-1614.pdf)

FEMA 的救援 crib 指南把“稳定将被移动的载荷”限制为高度不超过宽度 2 倍，支撑坍塌结构时可到
3 倍；这只能作为保守工程参照，不是本项目自由站立动力塔的定量门槛。
[FEMA US&R Module 4](https://www.fema.gov/pdf/emergency/usr/module4.pdf)

“宽度”还必须明确：Stage-0 原型全 Brick AABB 为 `432 cm`，接触面外缘包络为 `396 cm`，外轨
接触中心距只有 `288 cm`；对应 `H/W` 分别为 `3.00 / 3.27 / 4.50`。矿务局的压缩屈曲定义更接近
接触几何，不能拿 AABB 的 3.00 与其试件直接对比。当前端部 overhang 只有 `18 cm`；RI 9341
说明木材 overhang 经压缩后可互锁并限制局部滑出，但本项目近刚体 Brick 不具备该材料机制。
这些都是后续实时 PIE/完整荷载前必须保留的风险，不得直接声明为生产稳定芯体。

## 4. E1 的预算定理

设身体高度为 `H`、水平日志竖向厚度为 `t`。最小双轨合同要求每个 X course 两根、每个 Y
course 两根，并以完整 XY 层对结束：

```text
PairCount = ceil(H / (2t))
CoreBrickCount(2 rails) = 4 * PairCount
Interlayer contacts = (2 * PairCount - 1) * 4
```

五个 E1 的身体高度为目标高度减去 `8×36 cm` 单主屋顶：

| Profile | H（cm） | 36 cm 双轨芯体 | 芯体 + 最低 8 屋顶 | 108 cm 双轨芯体 | + 屋顶 + 12 外壳 |
| --- | ---: | ---: | ---: | ---: | ---: |
| ColumnBreak | 962 | 56 | 64 | 20 | 40 |
| SeamRelease | 912 | 52 | 60 | 20 | 40 |
| TipOver | 1137 | 64 | 72 | 24 | 44 |
| DropTrigger | 804 | 48 | 56 | 16 | 36 |
| SlideRelease | 837 | 48 | 56 | 16 | 36 |

结论：

- 该 `36 cm` 双轨完整 XY-pair 合同有 3/5 Profile 单独就超过 49，另两个只余 1 Brick；加入必需的 8 屋顶后
  全部超过 49，无需候选搜索即可判定不可行；
- `72 cm` 加最低外壳时 TipOver 仍超过 49；
- `108 cm` 是以 36 cm 为量化单位、同时给所有 E1 留 12 Brick 外壳的首个**未被 Brick 数
  下界排除**的截面，不是生产可行解；完整 XY pair 向上量化后，五个 E1 的实际身体高度依次为
  `1080/1080/1296/864/864 cm`，比现有身体窗口高 `118/168/159/60/27 cm`。若既有屋顶仍从
  原身体顶开始就会穿透，因此逐 Member 尺寸、屋顶起点与总高 seam 尚未解决；
- E1 使用 108 cm 三轨时，ColumnBreak、SeamRelease、TipOver 又会超过 49，因此多 rail 首先是
  高 Tier 候选，不是 E1 默认值；
- 通过 Brick 数不等于通过质量和玩法。108 cm 日志截面积是 36 cm 的 9 倍，芯体总材料和质量
  明显增加，必须在完整建筑/D2 单独评估。

## 5. 有限拓扑候选树

候选不是可连续调节的参数网格。每种只有一个预先登记的几何身份和最多两次 fresh 运行。

| 候选 | 适用入口 | 主要增强 | 固定证伪点 |
| --- | --- | --- | --- |
| A2：双轨纯 XY | E1 Brick 下界/独立物理基线 | 最少 Brick；在完整承压面与预登记 overhang 约束下扩大有效支撑宽度 | 静置/扰动不收敛，或 H/W 与可用轮廓无法形成稳定余量 |
| A3/A4/A5：多轨纯 XY | 预算充足的高 Tier | 承压、载荷分配、局部冗余 | 同外宽下最坏漂移/倾角无显著改善，则被更少 rail Pareto 支配 |
| B：XY 外笼 + 全长受围束 Z 压杆 + 实心 cap | 高竖向荷载候选 | 初始竖向刚度、压缩荷载路径 | 底/顶承压路径失效、位移/倾角越门、超出 cage clearance，或不优于相同预算纯 XY |
| C：XY slab + 错位短粗 Z block | 全长柱屈曲后的独立候选 | 分段承压、局部填芯 | 形成不连续对齐柱、开缝或引入新的细长矩形框机制 |

不得在同一轮同时改变 rail 数、外宽、截面、Z 形态、材料或摩擦。多轨比较采用固定 `N=3/4/5`；
如果需要扩大外宽，它必须先由轮廓给出的外宽下界/允许高宽比上限一次性推导并写入新的实验卡。

## 6. Stage 0：A2 最小原型

### 6.1 固定几何

- 真 Brick 截面：`108×108 cm`；长度：`432 cm`；
- 每 course 两根平行日志，中心位于另一横轴 `±144 cm`；
- X/Y course 严格交替；无 Z Brick；
- 最坏 E1 量化高度：6 个完整 XY pair，12 course，24 Brick，`1296 cm` 高；
- 相邻 course 恰有 4 个 `108×108 cm` 承压面，总计 44 个；
- 交替层合并后的整塔 AABB 为 `432×432 cm`；首层实际落地承压包络只有 `432×396 cm`，最坏
  `H/W=1296/396=3.27`。它不是 432 cm 方形地面 footprint；Geometry CRC32 为 `3576735518`；
- Wood-on-Wood、平面重力 `980 cm/s²`、Chaos `32/8`、固定 `30 Hz`；Brick 的 UE 自动质量为
  `417.871 kg`，由 `0.62 g/cm³`、实际碰撞体及 UE 默认 `RaiseMassToPower=0.75` 生成；它不是按
  `体积×密度` 得到的字面 `3124.086 kg` 木材质量；
- 无 Attach、无 PhysicsConstraint、无穿透修复、无质量覆盖、无 Freeze；为隔离纯稳定性，
  `DamageGrace` 覆盖整个观察窗，因此这是 no-damage settling/perturbation screen，不是破坏认证。

### 6.2 实验卡

| 项目 | 冻结内容 |
| --- | --- |
| 唯一假设 | 用厚水平日志把竖向矩形框改成多层正交摩擦面，可让芯体自身在小扰动后重新静置 |
| 主变量 | 拓扑从旧四柱框改为 A2；本轮不比较其它尺寸或 rail 数 |
| 最小 Filter | `StaticContract` → `Tier0BudgetProof` → `ChaosIdle` → `ChaosPerturbation` |
| 成功 | 几何/预算硬门通过；每阶段固定观察 6 秒，在 1.25 s 后曾达到且结束时仍保持连续 0.45 s quiet；全窗峰值和最终漂移 ≤4 cm、沉降 ≤6 cm、转角 ≤2° |
| 失败 | 同 Geometry/Fixture CRC 与 binary identity 的两个 fresh 进程失败即淘汰 A2；不改摩擦、阈值、Seed、Attempt 或尺寸 |
| 回滚点 | 本阶段文件完全隔离；删除 V2 prototype/tests 即恢复，不触碰旧 C3、Catalog 或地图 |
| 时间预算 | 单叶应为一分钟级；不运行 Beam-C3 全套、D1.5 或 5×6 |

### 6.3 当前证据

| 层级 | 结果 | 关键数据 |
| --- | --- | --- |
| Development Editor build | Pass | 安装版 UE 5.8，普通构建与 `-ForceUnity -DisableAdaptiveUnity` 全链接成功 |
| `StaticContract` | 2/2 fresh Pass | 24 Brick、12 course、44 contacts、整塔 AABB 432×432、首层承压包络 432×396、Geometry CRC32 `3576735518`、失败事务不发布 Brick |
| `Tier0BudgetProof` | 2/2 fresh Pass | 36 cm E1 数学不可行；108 cm 仅未被 Brick 数下界排除，五项均超出既有身体窗口；E1 三轨并非统一预算解 |
| `ChaosIdle` | 2/2 fresh Pass | Fixture CRC32 `3056556652`；首次 quiet 1.70 s，完整 6.00 s 后仍 quiet；最终漂移 0.864、沉降 1.544 cm、转角 0.061°；全窗峰值 1.379/1.730 cm/0.083° |
| `ChaosPerturbation` | 2/2 fresh Pass | Fixture CRC32 `1572860114`；course 11 顶层自由 rail 向外请求 8 cm/s，首物理帧向外 0.093 cm/2.078 cm/s，证明扰动非 no-op；完整 6.00 s 后仍 quiet；最终漂移 0.059、沉降 0.006 cm、转角 0.004°；全窗峰值 0.179/0.026 cm/0.031° |

日志：

- `Saved/Logs/BeamC3V2-Full4-Hardened-20260805-Run3-ForceUnity.log`
- `Saved/Logs/BeamC3V2-Full4-Hardened-20260805-Run4-Fresh.log`

两份日志各发现恰好 4 个测试、4 个 scoped `Result={Success}`，进程 `EXIT CODE: 0`，物理指标逐项
相同。命令固定为：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor-Cmd.exe' `
  '<WORKTREE>\AngryBirdsToSpace.uproject' -Unattended -NoSplash -NoSound -NoP4 `
  -NullRHI -stdout -FullStdOutLogOutput `
  '-ExecCmds=Automation RunTests ABTS.M73DAG.BeamC3V2.MassiveXYCrib;Quit' `
  '-TestExit=Automation Test Queue Empty' '-log=<UNIQUE_LOG>.log'
```

运行身份：基线 `e6d74a7852efb91a846d89d1d548efe9ef67d10d` 加本页所述未提交 Stage-0
工作树；ForceUnity `UnrealEditor-ABTSRuntime.dll` SHA-256 为
`7C90827A462B85B39B382537151AEF86469318785410194E022446080FAE8E66`。`Saved/*` 被
`.gitignore` 排除，以上 raw log 路径只是本工作树本地证据；提交 Stage-0 后必须以交付 SHA 重新运行，
不能把本段二进制身份冒充未来提交证据。

NullRHI 测试创建了真实 Physics Scene 和真实独立 Brick，但仍不能替代实时 30/60/120 FPS、可见
PIE、完整建筑荷载、画面检查或攻击认证。当前任务没有 GUI 授权，也没有修改
`PlanarPhysicsTestMap.umap`。

## 7. 生产接入前的强制阶段

> **历史边界。** 本节记录的是把 `108 cm` Massive-XY Stage-0 原型直接迁入生产所需的前置条件，
> 不是第 8 节 Stage 1 的当前执行清单。Stage 1 已选择统一 `36 cm` 截面和“结构 facade 替换普通
> body 表达”的空间 seam，因此不再等待逐 Member 尺寸迁移，也不把 Stage-0 的 Wood-on-Wood
> Chaos 证据作为生产前置门。第 8.5 节的 G0→G4→人工视觉→Chaos 顺序是当前权威节奏；本节仅作为
> 被停止路线的取证保留，防止以后无意中重新接回 `108 cm` 原型。

1. **物理候选阶段。** A2 先完成 fresh 重放；按高 Tier 需求只运行已登记的 A3/A4/A5/B/C 固定
   对照。记录层间开缝、相对滑移、支撑多边形 COM margin、Z 接触和每新增 Brick 收益。
2. **逐 Member 尺寸 seam。** 给 Beam IR 增加权威单件尺寸；新 USTRUCT 字段只能尾部追加并有
   legacy `36 cm` fallback。统一迁移 Beam-A/B/C/C3/D1 的 bounds、Bearing、穿透、split/merge、
   identity 和 Hash；Beam-A 现有 endpoint-only MemberKey/去重必须纳入解析后的横截面尺寸，或对
   同端点异尺寸 fail closed，不能让 36 cm Member 吞掉 108 cm Member。D0 catalog/hash/version 与
   schema 原子升级，旧 enum/USTRUCT 字段不得删除或重排；旧 `36 cm` 输入保持行为与身份等价。
3. **隔离 V2 generator/certifier。** 只生成明确的层/rail/cap/Z 压杆语义；从最终真实 Member 与
   Bearing 重建认证，不复用旧 Host/Tie/donor/repair。C3→C2 路由必须显式传
   `bAllowDeferredCoreBracing=false`，最终 cert 还要从真实 Bearing 证明 core envelope 内无 Z，
   防止旧 C2 路径重新生成满高 `CorePost`。
4. **原子生产切换。** 芯体先生成并保留硬预算，外壳后消费剩余预算；C2 不得重新生成旧
   `CorePost` deferred bracing；先跑最坏 E1 单叶，再 5×2、E2、高 Tier，最后保留的 30-leaf
   D1.5/5×6。
5. **动态正式门。** fresh 可见 PIE 的实时 30/60/120 FPS 静置、完整建筑承载和一次固定扰动；
   之后才进入 D2 攻击局部性。

生产 seam 还有两个未决硬合同，未解决前不得把 A2 接入：

- **材料权威。** Stage-0 只证明 Wood core 在 Wood floor 上的独立响应；当前生产 palette 会让
  SeamRelease/SlideRelease 等 CoreCourse 变成 Stone，TipOver 变成 Iron。接入前必须二选一：在 IR
  显式冻结 V2 core material=Wood 并纳入 Hash，或为各 Profile 的实际 core/base material 组合建立
  独立 fixture。不能把 Wood-on-Wood 结果外推到 Stone/Iron；
- **空间权威。** E1 depth 依次为 `500/575/450/504/500 cm`，而 432 cm core 若两侧各放一层
  36 cm shell 至少需要 `504 cm`，TipOver 明确不可放，ColumnBreak/SlideRelease 各差 4 cm，
  DropTrigger 也没有装配余量。再加上第 4 节的量化超高，预算余量不是 placement 证明。必须固定
  “core 替代边界 shell / 扩大 silhouette / exact placement+clearance fail-closed”之一，先给出无穿透
  几何，再进入生产候选。

任一阶段失败都返回上一层合同，不通过扩大候选次数进入下一层。

## 8. Stage 1：四面接地耦合外框

### 8.1 本阶段目标与边界

Stage 1 把用户提出的“芯体向外延伸、夹住外立面横梁、再由外立面横梁分段承接 Z 柱”落实为一套
可生成合同。生产 Brick 继续使用 Beam-A 当前统一的 `36 cm` 截面；Stage-0 的 `108 cm` 日志只保留为
历史物理对照，不进入本阶段生产几何。

本阶段的最终验收目标是：

- 5 个 Profile × 6 个 Tier 均能在各自有界候选上限内确定性生成；
- 最终 Member、真实 Bearing、DAG、预算、跨度和穿透门通过；
- 四面结构、接地外柱、主屋顶与风格/Tier 视觉里程碑可供人工验收。

这些条目是必须逐级取得的目标，不是设计稿本身的完成声明。截至 2026-08-06，仅 G0、
`DropTrigger.E3` 和 `TipOver.E1` 已取得生产链正证据；`SeamRelease.E6` 仍阻断在 G2，因此不能声明
30 个组合已完成，也不能进入 G3/G4、人工视觉或完整建筑 Chaos。

本阶段明确不声明 Chaos 静置稳定、攻击局部性或材料摩擦认证。所有日志必须记录
`PhysicalStability=NotEvaluated`，不得把 DAG 通过改写成物理稳定完成。

### 8.2 单 cell 六层宏带合同

设统一截面为 `s=36 cm`，芯体总 course 数 `C` 为不小于 8 的偶数，平行 rail 数为 `n`。普通芯体
course 按 X/Y 严格交替；一个耦合宏带固定占用六个 course：

```text
a+0  X through：芯体 X rail 直接延长到东西向外框
a+1  Y facade：x=±Fx 的两根外立面 rail，被上下 X through 夹持
a+2  X through
a+3  Y through：芯体 Y rail 直接延长到南北向外框
a+4  X facade：y=±Fy 的两根外立面 rail，被上下 Y through 夹持
a+5  Y through
```

`through` 是同一根芯体 rail 的加长形态，不允许再叠加一根同轴 Brick。每根 facade rail 必须从最终
真实 Bearing 证明：下侧和上侧分别至少有 `n` 个承压区；不能以空间接近或旧 MemberId 代替。

外柱只位于 facade rail 与 through rail 的交点，并在相邻宏带之间分段：最低段直接落地，后续段的
底面和顶面分别接触相邻耦合层。所有新结构 Brick 的轴向长度均不得超过 `720 cm`；这比旧合同只审计
Z 站位更严格。最终合同要求：若一个语义主体在 X/Y 任一方向超过单 cell 可用跨度，应按语义主体拆成
多个各自接地的 cell，不用端对端长梁冒充一根连续承重构件，也不跨 `ReservedSupportVoid`，更不能
借用未进入 root witness 的其它 Source。当前 Stage 1 尚未实现一般超宽主体拆分；除合法
`SupportedSpan` 的桥端双 cell 尝试外，D1 只选择第一个确定性根体，超宽情形必须 fail closed，不能把
本段目标写成已有能力。

对四面对称、每向均为 `n` rail 的单 cell，结构 Brick 下界为：

```text
Bcore      = n * C
Bstructure = n * C + 4 * B * (n + 1)
```

其中 `B` 为宏带数；第二项包含每带 4 根 facade rail 与 `4n` 根分段外柱。一般非对称式为：

```text
C * (nx + ny) / 2 + B * (4 + 2 * (nx + ny))
```

共享面第一版不抵扣预算，避免把尚未由最终去重证明的收益提前计入。

宏带起点只取预登记的偶数 course。相邻起点间隔限制为 6～22 course，使所有首段和后续外柱段均不
超过 720 cm。course、lane、cell 和 facade 坐标全部先在 `s/2` 或 course 整数量子上求解，最后才转为
浮点位置，避免候选搜索和累计误差。

同一 course 的 `n` 个 lane 不固定使用 72 cm 窄间距；它们在该 cell 已解析的 core span 内确定性
铺开：最外 lane 中心贴近 core 两侧承压边界，中间 lane 在 `s/2=18 cm` 整数量子上均分。这样
E1 的两条外轨不会挤在长 through 梁中央，高 Tier 增加到 3/4/5 rail 时则在相同芯体宽度内逐步
加密。该几何必须通过 Beam-C 生产默认的 `MinimumSeparatedSupportSpanRatio=0.20`，不得只在测试
中把门槛降到 0.10 来制造 G0 通过。

低 Tier 短边采用紧凑但不穿透的解析下界：普通 core 包络与 facade/post 站之间每侧保留一整个
`s=36 cm`，所以 `MinimumCellSpan=(n+2)s`；E1 双轨下界为 144 cm。构件面可以在这条边界相接，
但正体积不得重叠。最终 cell 仍取语义体内的 18 cm 量化可用跨度，并继续接受默认承压展开比审计；
这不是改变 Brick 粗细或放宽 DAG 门。

### 8.3 5 Profile × 6 Tier 的共同算法

30 个组合只使用一套结构算法。风格由既有语义主体、Reserved Void、屋顶、材料和弱点意图表达；Tier
只从离散表解析 rail 数、course 数、cell 上限、耦合节奏和预算：

```text
RailCount(E1..E6) = 2, 2, 3, 3, 4, 5
```

Stage 1 首轮实现把主拓扑也冻结为离散表；这些值是可复现身份，不是生成器内搜索范围：

| Tier | 主 course 数 | rail 数 | cell 上限 | 宏带上限 |
| --- | ---: | ---: | ---: | ---: |
| E1 | 8 | 2 | 1 | 1 |
| E2 | 16 | 2 | 1 | 1 |
| E3 | 30 | 3 | 2 | 2 |
| E4 | 44 | 3 | 2 | 2 |
| E5 | 60 | 4 | 3 | 3 |
| E6 | 76 | 5 | 4 | 4 |

`cell 上限` 只是轮廓、语义和预算都允许时可消费的确定性上限，不是必须凑满的数量。把
`MaximumCellCount` 当作目标数量会让 E3 从单 cell 的 711 Brick 退化为 6/6 候选失败，其中最接近
候选为 778/799 且 Beam-C 仍需 22 个支撑、只剩 21 个容量；因此“高 Tier 自动填满 cell”已经被
证伪并禁止。

当前多 cell 只服务一个明确结构原因：精确 `SeamRelease.E6` 的 `BridgedArcology` 必须把两个接地
cell 分别对齐同一桥跨的负、正端。两端必须来自完整的同一 `SpanVolumeId`；每个 cell
的局部根系证据必须包含相应 `SupportBayId`（否则只允许回退到该支撑的 `SourceVolumeId`）；端点侧
facade 平面必须贴合 `BearingPlaneCM`，垂直方向包络必须覆盖全部 `RailStationsCM ± 18 cm`；两个根
Source 必须不同，且两个 cell 正体积不得重叠。任一条件不成立时以
`EndpointPairUnavailable` fail closed，不退回单 cell，也不枚举任意第二、第三或第四 cell。

生成器对每个请求只规划一次。一般 Profile 只有一个强制 cell；E6 的两个 cell 与共享 course 是原子
计划，只有在精确几何不冲突，且
`Existing - RemovedUnion + Planned <= HardFinalMemberCount - ClosureReserve` 时才整体接纳，其中
`ClosureReserve=4 * RailCount * MaximumMacroBandCount`。结果显式记录 Requested/Selected、
BudgetLimited、GeometryLimited 和 ClosureReserve；不在同一生产候选内尝试 1、2、3、4 个 cell
组合，也不以 Seed 搜索替代结构原因。

宏带数只由实际 course 数与 720 cm 合同推导，不能因 Tier 较低而放宽。最终合同的低 Tier 有限降级
顺序固定为：

```text
降低 body course / 总高
  -> 减少非承重装饰与 cell
  -> 在仍满足柱段 720 cm 的前提下增大宏带间距
  -> 使用 D0 明示的结构预算下限
  -> fail closed
```

不得在 C3 内静默抬预算、删除主屋顶、删除一面外框或回退旧四柱 C3。D0 当前已把
`MaximumFallbackLevel`、course/cell reduction 纳入校验和 Resolved Hash，但 D1 尚未消费这些字段；
因此当前生产候选只有一个主拓扑，失败即 fail closed，不能声称已经执行至多两个预登记降级。以后接入
降级时，每级必须成为显式身份并受同一 rejection fingerprint 去重，不能在 C3 内隐式搜索。

### 8.4 生产链和最终认证

```text
D0 resolve Profile/Tier + V2 discrete recipe
  -> Shape / Beam-A / Beam-B 形成语义轮廓和受保护区域
  -> V2 以结构 facade 替换冲突的普通 body 表达
  -> Beam-A authoritative closure + final Bearing rebuild
  -> Beam-C/C2，显式 bAllowDeferredCoreBracing=false
  -> V2 final certificate（只读最终几何，不做 post-C2 repair）
  -> D1 one Member : one 36 cm Brick
```

V2 不调用旧 C3 的 Host、Portal、donor、Catalog 特例或 post-C2 repair。C2 之后只允许一次最终认证；若
C2 改坏结构，当前候选直接失败，不能在 C3 内继续修补。以后只有 D1 真正消费了预登记降级身份后，
上游才可选择另一个已登记配置；当前实现没有这条生产 fallback。

cell 根体不是“某个 `MinZ=0` Bay 足够高”的近似。D1 在 Beam-A 的 `AdjacentBayIds` 图上建立局部
竖向权威链：起点 Bay 必须接地且满足 XY 放置；向上一步必须同时满足上下表面接触和正 XY 投影重叠；
只有图上 `HighestReachZ >= RequiredZ` 的根体才可候选。链可以穿过确实相接的 stacked semantic
volume，其全部 BayId/SourceVolumeId 都进入 witness；不能借用未进入该链的 Source。确定性 witness
生成 `RootAuthorityCrc32` 并进入 Plan 身份。`ReservedSupportVoid` 仍按全局几何审计，不能因为局部
图可达就穿越其它语义体的保护空间。

PlanSet canonical 当前为 v6，并纳入 root authority、桥跨/端点、Requested/Selected/skipped、闭合
预留、硬预算和最终几何身份；E6 原子双 cell 另外绑定 Beam-B 权威 rail Z/station、共享 course 层位
与覆盖范围。可替换 `BridgeRail` 集合由 plan-set 输入确定性派生，但当前不直接写入 canonical；实际
替换仍须由最终几何证据补强。Plan 不引用闭合后易变的 MemberId。这样 optional cell 被跳过、端点
对齐变化、共享 rail 变化或根系变化都会改变可解释身份，而不是只在日志中显示“进入过分支”。

最终证书至少检查：

- 四面位掩码完整，对向 facade 成对存在；
- 每面至少两个接地外柱站，最低段 `MinZ=0`；
- core/through/facade/post 计划成员能从最终几何按 Role、Axis、量化中心和长度重建；
- facade rail 上下夹持 Bearing、外柱段上下 Bearing、所有 V2 Member 到 Ground 的 DAG 路径完整；
- 新结构轴长与最大 Z 段均不超过 720 cm；
- 无正体积穿透、悬空、Reserved Void 侵入、跨 Source 借根或 Brick 预算越界；
- Plan Hash 不含闭合后易变的 MemberId；Geometry Hash 按计划顺序记录最终匹配构件的
  Kind/Axis/Role、量化 AABB Min/Max 与长度。

### 8.5 分层执行节奏与停止条件

本阶段固定按以下门禁推进，前一层失败时不得运行后一层：

1. `G0`：毫秒级纯数据 `TopologyAndDimensions`、`DAGContract`、`FailClosed`；
2. `G1`：`DropTrigger.E3 / Seed 740000` 单一中 Tier 完整生产链；
3. `G2`：`TipOver.E1 / Seed 730000` 最窄低 Tier 与
   `SeamRelease.E6 / Seed 720000` 最大上游容量边界；
4. `G3`：五个 Profile 各一个 E3，确认风格身份、材质、弱点和结果 Hash 不坍缩；
5. `G4`：复用现有 30 个独立 D1.5 Complex leaf，按 Profile 分片运行；
6. 人工视觉验收通过后冻结结构语言，再建立完整建筑 Chaos 矩阵。

调试不得运行会同时命中 Stage-0 Chaos 的宽 `BeamC3V2.MassiveXYCrib` 前缀，也不得用旧
`ColumnHighTierClosure` 或循环十格的旧 C3 测试作内环。每个 leaf 使用唯一日志和建议 90 秒外部
watchdog；宽矩阵只做里程碑，不作调参器。

开始编码前的实验卡固定为：唯一主变量是四面接地耦合外框；生产截面 36 cm；拓扑变体数 1；成功必须
改变最终 Geometry/DAG 身份并通过 G0/G1；相同最终拒绝身份两次、单假设两轮、连续 60 分钟无新增可排除
结论或同路线累计 4 小时未越过最小门，以先到者立即停止并建立 checkpoint。

当前门禁状态：历史基础 G0 3/3、E6 shared G0 1/1、G1 `DropTrigger.E3`、G2
`TipOver.E1` 已有通过证据；Catalog v11 的 40 cm 平行 Brick 最小净空已通过 D0 6/6，且最终 E6
Member 已降到 4358/4999。G2 `SeamRelease.E6` 仍未通过：最终只剩一个真实 Bearing 闭合缺口。
依照前置门失败和单假设两轮即停止的规则，本轮不运行 G3、G4、人工视觉或完整建筑 Chaos，也不继续
改变间距、预算、cell 数、course、rail、宏带数或 DAG 阈值。

### 8.6 2026-08-06 Stage 1 实现与证据

本轮在基线 `e6d74a7852efb91a846d89d1d548efe9ef67d10d` 的未提交工作区完成 D0 离散 Recipe、
四面外框生成器、D1 生产 seam、最终证书和 G0 测试。Development Editor 使用安装版 UE 5.8、
`-ForceUnity -NoHotReloadFromIDE` 完整链接成功；权威无后缀模块
`UnrealEditor-ABTSRuntime.dll` SHA-256 为
`8BC644461FA9C585D6216AA926A470628BA5E3E1B53FD3F9CB1BA2E287C134D8`（9,418,240 bytes）。由于源码尚未提交，
该二进制身份只能证明当前工作区，不能冒充未来交付 SHA。

正证据：

- D0 `ABTS.M73DAG.BeamD0` 6/6：Catalog validation/hash、5×6 Recipe 矩阵解析、determinism、
  fail-closed 和 hard-gate isolation；这只验证 Recipe/身份，不等于 30 个生产建筑已生成；日志
  `Saved/Logs/M73-BeamC3V2-D0-Compliant-20260806.log`；
- G0 `GroundedCoupledFrame` 3/3：`TopologyAndDimensions`、`DAGContract`、`FailClosed`；双 cell
  fixture 在上限 64 时接纳两个手工 FBox 的独立 28-Member cell，在上限 56 时事务性保留一个并记录
  一个 `BudgetLimited`，证明生成器的可选 cell 精确预算接纳；它尚未覆盖 D1 的 endpoint/root 派生，
  不能作为桥端对齐正证据；日志
  `Saved/Logs/M73-BeamC3V2-G0-Compliant-20260806.log`；
- G1 `DropTrigger.E3 / 740000` 首候选通过：711 Brick，1/1 cell，Plan
  `1757119523`，Geometry `1454963307`，DAG `3490125525`，最大构件/柱段
  `720/684 cm`，总测试 222.92 ms；日志
  `Saved/Logs/M73-BeamC3V2-G1-DropTrigger-E3-Compliant-20260806.log`；
- G2 `TipOver.E1 / 730000` 首候选通过：84 Brick，1/1 cell，Plan
  `2768066177`，Geometry `1195178146`，DAG `677265849`，最大构件/柱段
  `288/180 cm`，总测试 4.79 ms；日志
  `Saved/Logs/M73-BeamC3V2-G2-TipOver-E1-Compliant-20260806.log`。

以上三份正证据和冻结后的 Stage-0 49 窗历史夹具均使用 `-NoSound -NoMessaging`；历史夹具 1/1
通过，日志 `Saved/Logs/M73-BeamC3V2-HistoricalTier0Budget-Compliant-20260806.log`。旧夹具不再
Resolve 当前 Catalog，避免把退役的 E1=49 布尔结论强加给 V2 的 E1=99 权威窗口。

低 Tier 预算先做了下界诊断：旧 E1 上限 49 在 V2 接入后最少需 55，临时上限 59 仍不可行；只用于
测量的高上限样本落在 63～84。当前权威窗口因此重分配为 E1 `20–99`、E2 `100–299`，保持 E3
从 300 起、六档不重叠，也没有改变 36 cm Brick 截面。E1 的 84 Brick 正证据说明新窗口不是任意
放宽，但 5 个 Profile 的 E1/E2 尚未全部运行，不能外推为 10/10 完成。

负证据与停止结论：

- 单 cell `SeamRelease.E6` 在正式 4999 上限下运行约 92.98 s 仍失败，最佳预算型候选只差一个
  Beam-C 支撑；把诊断上限临时提高到 8191 后运行约 86.09 s 仍全部失败，出现
  `ClosureStalled`，说明“只抬预算”不是充分修复；上限已经恢复为 4999；
- 把 cell 数自动填到 `MaximumCellCount` 后，原本通过的 `DropTrigger.E3` 退化为 6/6 失败，最终
  778/799 且 Required 22、Capacity 21；该路线已经撤回；
- 只对合法高 Tier `SupportedSpan` 请求桥端双 cell 的精确 E6 运行 12 次、71.27085 s。8 个通过
  Beam-B 的候选全部报告 `EndpointPairUnavailable RequiredSpans=1 Span=0 Endpoints=2
  EligibleRoots=2`，因此实际仍为单 cell；失败类别仍为结构闭合、最终预算和支撑容量，最后为
  `ClosureStalled Resultant=6 Spread=4 Members=4849`。日志
  `Saved/Logs/M73-BeamC3V2-G2-SeamRelease-E6-EndpointPair-Retry-20260806.log`。同名前一份非 Retry
  日志没有启动任何测试，明确排除在证据之外。该负日志缺少 `-NoSound -NoMessaging`，只作为本轮
  诊断和停止证据；E6 已失败，不为补命令格式重复运行。

上述日志是本轮共享-course 实验前的历史停止点：当时只能证明端点完整、存在两个合格根体，但严格 pair
尚未派生。随后完成的纯数据派生和生产专项已越过 `EndpointPairUnavailable`；新的阻断已收敛为
Beam-C 最终预算，详见下一节和
[M73BeamC3V2Checkpoint_20260806.md](M73BeamC3V2Checkpoint_20260806.md)。

### 8.7 E6-only 双芯体共享 course 合同与停止结论

endpoint 派生问题解决后，本轮按用户给出的桥接关系只为 `SeamRelease.E6` 增加一个原子合同；外层
Building Seed 为 `720000`，生产专项固定 Candidate Seed 为 `972217611`。该合同不推广为一般
多 cell：恰好两个 endpoint cell 共用 Beam-B 已存在的两条外轨。D1 把每个
endpoint 的 `RailCenterZCM` 和排序后的 `RailStationsCM` 传入 V2 请求；两端身份、rail Z 和 station
必须一致，否则 fail closed。生成器选择最接近 Beam-B rail 且相位差在
`2 * JointMergeTolerance` 内的真实 C3 lattice course；生产样本为 course 44、中心高 `1602 cm`。

每条 authority rail 的 embedded shared span 必须自身不超过 720 cm，否则 fail closed。生产几何用
一个 SharedCourse 吸收一侧 core 延伸，并保留另一侧一个 far stub，形成最少两件覆盖。共享 rail 上下
相邻的交叉 course 只延长到两条 authority station，使两端 crib
sandwich 与共享 rail 建立真实面接触，同时避免穿透 facade Z 柱。受保护 `BridgeRail` 只能在同轴、
同 station、Z 相位相容且共享计划沿跨度完整覆盖时被替换；其余保护成员仍拒绝。R4 的四条 core rail
继续只表达芯体内部密度，不被误作四条桥轨，也不从其间距虚构内桥轨。身份为 `PlanSet:v6`、
`SharedCourse:v2`。

G0 专项通过正向、逆序重放、超长和预算 fail-closed。生产几何也确实形成 2 cell、2 shared rail、
52 local courses、R4 core、540 planned members；这说明实现已越过此前的 `EndpointPairUnavailable`，
不是“进入分支但最终几何不变”。但是最终 Beam-C 门仍失败：

```text
旧独立 course：4706 + 235 + 464 - 181 = 5224
新共享 course：4671 + 241 + 491 - 186 = 5217 > 4999
```

235/241 是按最终闭合恒等式反推的 Beam-C 提交几何量，日志没有单列 proposal 计数。共享 topology
只降低 7 个最终 Member；反推量增加 6，reclose 的净 split 成本增加 22。结论是该关系可作为可读的
双芯体桥接几何，但它不是 E6 闭合预算的主导解。该历史停止点要求下一次实验先证明至少 218 个最终
Member 的可解释节省；8.8 节的 40 cm 减密随后满足了这项前置。共享-course 本身仍不得再用
R3/R2、Seed、cap、容差、额外 rail 或一般多 cell 重试，G3/G4/视觉/Chaos 仍未开放。

当前共享 rail 由 C3 sandwich 承接，装配所有权仍归负端 C3 cell；它没有重新归属到 Beam-B
`SupportedSpan`。因此本阶段只声明几何/DAG 实验结论，不声明物理稳定性，也不把该语义所有权留项
隐藏在“共享”一词中。

此外，当前 shared-pair certificate 只证明计划内两条 SharedCourse 与四侧 sandwich contact；它没有
逐 authority station 证明既有 Beam-B `BridgeRail` 确实被消费，G0 也使用空 Assembly。因此以后若
重启这条架构，必须先补实际替换证据、目标 span owner、共享所有权，以及原 `BridgeSeat` 保留或退役
合同，再讨论预算修复；当前结果不能命名为 Beam-B rail 替换证书。

### 8.8 平行 Brick 减密与 E6 有界闭合实验

用户提出的主变量被落实为 `MinimumParallelBlockGapCM`，而不是改变 36 cm Brick 截面或
`TwoBlockMergeGapCM=4 cm` 的两根并一根门。Catalog v11 把最小平行净空写死为 40 cm，并把
`BlockCrossSectionCM`、`MinimumParallelBlockGapCM`、`TwoBlockMergeGapCM` 纳入 Resolved Hash；否则
同一 Profile/Tier 在改变实际几何后仍可能沿用旧结果身份。5×6 解析矩阵显式断言 40/4 cm。

实验只允许两个离散原型，随后停止扫 gap：

| 原型 | 预算结果 | 结构结果 | 结论 |
| --- | --- | --- | --- |
| 12 cm 历史基线 | 5217/4999 | 尚未进入最终 DAG | 超 218，不能生产 |
| 72 cm | 3625/4999 | 4 个 Beam-C 节点停滞 | 预算充足，但三轨区域被压成两轨，过疏 |
| 40 cm | 4552/4999 | 4 个 Beam-C 节点停滞 | 预算已解决且保留三轨形态，固定为当前值 |

72 cm 下 D0 6/6、Beam-A 11/11；40 cm 下 D0 6/6。由于 40 cm 已留出 447 Member，后续失败不得
再解释为需要第三个 gap，也不得提高 4999。生产 V2 原先把 Beam-C 的 deferred rooted grillage 关闭；
恢复该已有的显式 `BridgeSeat + 2 BridgePost` 路径后，立即以 V2 最终几何、完整 DAG 和所有最终 Z
段跨度重新认证，仍不运行 Chaos。新增 fail-closed 夹具证明任一 756 cm 最终 Z 段会被
`BeamC3V2FinalAllZSpanExceeded` 拒绝并清除 DAG certificate。

第一轮转承几何使用与失败 Upper 平行的 25/75 cap。它把最终数降到 4363/4999，并把缺口收敛为两个；
但交替层中的正交 course 会迫使 Beam-A 把一根新 cap 抬高一层，使它脱离双根柱，另一根 cap 仍未覆盖
真实荷载合力。第二轮改为只在相同失败 DAG 已证明 quarter-root 无效后，生成与 Upper 正交、以真实
LoadResultant 为中心的三截面 cross-bearing，再以两根分离落地柱承托。该事务消除了 6 个待修节点中的
5 个，包括原先差约 4.18 cm 的 resultant 缺口；最终为 4358/4999、641 Member 余量，只剩
Member 1879（Axis X、SecondaryBeam，枚举 Role=2、268.62 cm）仍只有旧右端支承。新 cross-bearing 在权威 reclose
后没有成为该 Upper 的 Bearing candidate，原因尚未由最小纯数据夹具定位。

这一结果是 DAG/分段代理，不是 Chaos 稳定证据。两轮结构实验已经用满；依 M7-BC-023 不再直接运行
第三个 E6 生产变体。恢复条件是先用毫秒级夹具输出 Member 1879 目标 seat 在 reclose 前后的
Member/Role/AABB/owner/Bearing 差异，证明它究竟被 merge、重建、迁移还是未形成接触，再提出一项原子
几何修复。不得用新 gap、比例、容差、Seed、闭合 pass 或预算代替该证据。

本节权威日志：

- `Saved/Logs/M73-BeamC3V2-ParallelGap72-BeamD0-20260806-205544.log`；
- `Saved/Logs/M73-BeamC3V2-ParallelGap72-BeamA-20260806-205641.log`；
- `Saved/Logs/M73-BeamC3V2-SeamRelease-E6-Gap72-Production-20260806-205741.log`；
- `Saved/Logs/M73-BeamC3V2-ParallelGap40-BeamD0-20260806-210059.log`；
- `Saved/Logs/M73-BeamC3V2-SeamRelease-E6-Gap40-Production-20260806-210154.log`；
- `Saved/Logs/M73-BeamC3V2-FinalAllZSpan-20260806-211749.log`；
- `Saved/Logs/M73-BeamC3V2-SeamRelease-E6-Gap40-TransferGrillage-20260806-211841.log`；
- `Saved/Logs/M73-BeamC3V2-SeamRelease-E6-Gap40-ResultantCrossBearing-20260806-2129.log`。
