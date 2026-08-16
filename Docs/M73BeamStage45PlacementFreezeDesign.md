# M7.3 Beam Stage 4.5 建筑放置冻结设计

> 状态：2026-08-15 已按统一三轴 36 cm 边界格重新冻结；WFC 与六栋 Bounds 未改变。
>
> 范围：固定演示六栋的静态放置描述。本文不声明 Stage 5、Chaos、破坏、弱点、六栋动态并发或完整 PIE 已完成。

## 1. 目的与边界

Stage 4.5 让集成工作树和 M3 在不等待 M7 完成生产积木承重 DAG、破坏及 Chaos 的前提下，先消费一个
不会随未完成动态阶段漂移的建筑放置目录。该目录不是手写尺寸表：每项都由对应 Manifest 输入重新运行
真实 Stage 4 `FloorInfillRoof` 静态生成，从未被 Stage 4 suppression 的 member AABB 中提取，并由 fresh
自动化逐项反验提交值。

冻结目录只发布 M7 自有数据，不新建 M7→M3 运行时反向通道，也不修改集成工作树所有的共享契约。
集成工作树可在合并本提交后，把只读目录适配到 M3 的布局选择；M7 运行时仍只消费 M3 已接受的
`FABTSBuildingGenerationContract`。

## 2. 冻结身份

- 放置描述 Schema：`1`
- 源 Beam Demo Manifest 版本：`1`
- 源 Manifest Hash：`2324068295`
- 冻结条目数：`6`
- 放置目录 Hash：`11501529584318250152`
- 目录入口：`FABTSM73BeamStage45PlacementFreeze::GetFrozenDescriptors()`
- 单项入口：`FABTSM73BeamStage45PlacementFreeze::ResolveFrozen()`
- 权威头文件：`Source/ABTSRuntime/Public/Building/ABTSM73BeamStage45PlacementFreeze.h`

六个 `ManifestEntryId`、StableId、Profile、Tier、Seed 沿用 Beam Demo Manifest，不允许在放置目录中另设
一套选择逻辑。Profile Catalog、Resolved Settings、Grammar、WFC 和 Stage4 Plan Hash 同时冻结，避免只有
Seed 字面相同但输入身份已经变化。

## 3. 坐标、Pivot、地面与 Pad 约定

所有数值单位均为厘米，描述位于 Stage 4 生成器局部空间：

- `PlacementPivotLocalCM = (0,0,0)`，即生成器原点；
- 局部 `+X = Forward`、`+Y = Right`、`+Z = Up`，三轴为右手正交基；
- 六栋的 `GroundPlaneZCM = 0`，`PivotToGroundOffsetCM = 0`；
- `LocalBounds` 是所有 active 静态 member 实体 AABB 的并集；
- `FootprintMinCM/FootprintMaxCM` 是该并集在局部 XY 平面的保守矩形投影；
- `RequiredPadHalfExtentCM` 按 Pivot 分别取 `max(abs(Min), abs(Max)) + 36 cm`；最后一个 36 cm 是一整块
  积木的冻结安全边，不包含 M3 自己的道路、坡度或站点间距余量；
- M3/集成适配器必须随站点 Transform 一起旋转 Forward/Right/Up 和 Pad，不能把局部 XY 半尺寸当世界轴对齐
  尺寸，也不能偷偷把 Pivot 改为 Bounds 中心。

## 4. 静态几何提取与拒绝门

`DeriveAndValidate()` 对每一项执行以下不可跳过的过程：

1. 由冻结 Manifest 解析 Profile/Tier/Seed，运行真实 Stage 4 `GenerateStagePreview(...FloorInfillRoof)`；
2. 要求 Stage 4 accepted、各分阶段 Hash 非零，TopSurface 未解析数、各绑定/冲突计数和 unsupported Roof
   member 数全部为零；
3. 排除 `bSuppressedByStage4FacadeToTop` 的 Stage 3 临时柱，只把实际可见/可放置的 active member 纳入冻结；
4. 从 member 中线和 36 cm 截面重建实体 AABB，要求有限、不得低于统一 `Z=0` 地面；所有
   `bRequiresGroundSeat` member 的底面必须精确落在地面，整栋 Bounds 最低面也必须等于地面；
5. 对 active member 全体执行两两严格正体积相交检查；共面接触允许，任一 XYZ 三轴均有正厚度的交叠立即
   fail closed；
6. 将排序后的 AABB 行计算 `StaticGeometryHash`，将 AABB 加 Axis/Role/GroundSeat 计算
   `StaticStructureHash`；排序使 Hash 不依赖 member 数组索引或未来的安全压实顺序；
7. 对完整身份、Bounds、Footprint、Pivot、地面、方向、Pad 和几何 Hash 计算 `DescriptorHash`，再按
   Manifest 顺序计算目录 Hash。

因此，后续若 Stage 5 仅删除已经 suppression 的临时记录或安全压实 active 数组，冻结 Hash 不会误漂；若真实
静态几何、占地、角色或落地状态改变，则自动化会明确拒绝，必须重新评审并发布新的冻结值。

## 5. 六栋冻结放置表

| ManifestEntryId | StableId | Profile / Tier / Seed | Local Bounds Min → Max | Required Pad Half Extent | Active Members | Static Geometry Hash | Descriptor Hash |
| --- | --- | --- | --- | --- | ---: | ---: | ---: |
| `E1ColumnBreak` | `DemoE1ColumnBreak` | `ColumnBreak / 0 / 710000` | `(-414,-162,0) → (-90,162,648)` | `(450,198)` | 52 | `10276011350224018878` | `10113758205408230493` |
| `E2DropTrigger` | `DemoE2DropTrigger` | `DropTrigger / 1 / 740000` | `(-774,-450,0) → (486,450,1476)` | `(810,486)` | 235 | `1243337162086650128` | `1108134973396587699` |
| `E3SlideRelease` | `DemoE3SlideRelease` | `SlideRelease / 2 / 750137` | `(-1026,-414,0) → (1026,414,1332)` | `(1062,450)` | 364 | `3075258440093988143` | `17683520519518435068` |
| `E4TipOver` | `DemoE4TipOver` | `TipOver / 3 / 730000` | `(-846,-378,0) → (846,378,2376)` | `(882,414)` | 872 | `4328116049969586954` | `11089610541129920709` |
| `E5SeamRelease` | `DemoE5SeamRelease` | `SeamRelease / 4 / 720000` | `(-1350,-630,0) → (1350,630,2376)` | `(1386,666)` | 1807 | `461929562625370845` | `7322844578368466709` |
| `E6TipOver` | `DemoE6TipOver` | `TipOver / 5 / 750000` | `(-1062,-486,0) → (1062,486,3384)` | `(1098,522)` | 2174 | `6610608065286482828` | `3963542007450344969` |

表中 Bounds、Pad 与 Hash 均来自同一自动化提取结果；源码中的冻结目录还保存每项 Profile Catalog、Resolved
Settings、Grammar、WFC、Stage4 Plan 和 Static Structure Hash，表格仅省略这些长字段。

## 6. 验证与集成交接

自动化过滤器 `ABTS.M73DAG.BeamC3V3.Demo.Stage45PlacementFreeze` 会重新生成六栋并逐字段比较冻结目录，
同时重新执行落地和无正体积重叠门。最终证据要求 UE 5.8 Development Editor ForceUnity 全链接成功，并在
两个互相独立的 fresh `UnrealEditor-Cmd -NullRHI` 进程中得到相同 Manifest、Catalog、Structure、Geometry
和 Descriptor Hash。

Stage 4.5 通过后，M3 可以不再等待以下 M7 项目：Chaos 倒塌、破坏效果、弱点/攻击面、六栋同时动态运行、
完整 PIE。它仍不得把本冻结解释为这些门已经通过；集成候选负责在共享契约/地图中完成实际消费适配和联合门。
