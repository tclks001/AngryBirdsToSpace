# Building Generation and Placement Freeze V3 实现与冻结计划

> 状态：集成前置 Crystal 基线已实现；提交精确 SHA 由本次交接消息给出。
>
> 日期：2026-08-15
> 目标：以最小跨工作树改动冻结建筑生成规则、5+1 地图位置和后续 Chaos 证据链。

集成执行详稿、V3 字段预备语义、fail-closed 矩阵与门禁清单见
[Building Generation and Placement Freeze V3 集成准备与门禁](BuildingGenerationAndPlacementFreezeV3IntegrationReadiness.md)。

## 1. 本轮最终规则

| 遭遇槽 | 建筑 | 支撑表面 | 主材料 | 特殊规则 |
| ---: | --- | --- | --- | --- |
| 0 | E2 | 主星 | Wood | 普通地面建筑 |
| 1 | E3 | 主星 | Wood | 普通地面建筑 |
| 2 | E4 | 主星 | Stone | 普通地面建筑 |
| 3 | E5 | 主星 | Iron | 普通地面建筑 |
| 4 | E1 | 月球背面 | Stone | 顶端固定唯一 Crystal 小砖 |
| 5 | E6 | 主星 | Iron | 普通地面建筑 |

建筑编号、难度层级和遭遇槽必须解耦。E1 即使位于遭遇槽 4，仍保持最小、最简单的 Tier 0 建筑；已有 SatelliteWindow 继续占用槽 4，从而避免重写任务图和 CrystalCore 到 SpaceCord 的时间顺序。

## 2. 坐标与朝向最小变更规则

- 建筑内容坐标明确以本地 `+Y` 为正面。
- M3 场地坐标继续保持 `+X Forward / +Y Right / +Z Up`，不迁移道路、地形、Pad 和径向重力算法。
- M7 在生成出口执行一次固定的 content-to-site 旋转，并生成旋转后的 site-local OBB、PadBounds 和 EffectBounds。
- 自动化必须验证 `BuildingLocal +Y` 经转换后与 Site `+X Forward` 同向。
- M3 只消费 site-local 结果，不重新推断建筑宽深或建筑原始正面。

## 3. 集成前置：Crystal 稳定基线

本节是 M7 开始 `Building Freeze V3` 前必须合入的共享前置提交。

### 3.1 本提交包含

- 在 `EABTSM7BuildingMaterial` 尾部追加 `Crystal=4`，保留 Wood/Stone/Iron/Glass 的既有序列化值。
- 注册 Crystal 专用 HISM、材质引用、fallback 和物理材质。
- Crystal 第一版复用 Glass 的碰撞/破坏 Profile，避免在几何冻结前开启新的 Chaos 调参支线。
- Crystal 撞击表现暂时复用 Glass impact profile。
- 建筑材料回收映射固定为 `Crystal -> CrystalCore`，未知材料 fail closed，不再隐式回退为 Wood。
- 加入材料 Profile 和回收入库纯映射自动化。

### 3.2 已有二进制资产

资产：`/Game/StaticMesh/BrickMaterials/MI_Bricks_Crystal`

UE 5.8 只读反射审计结果：

- 类型：`MaterialInstanceConstant`
- Parent：`/Game/StaticMesh/BrickMaterials/M_Bricks_Glass`
- `EdgeGlowStrength=2.0`
- `GlassOpacityCenter=0.06`
- `GlassOpacityEdge=0.0`
- `GlassRoughness=0.0`
- `GlassTint=(0.70, 0.72, 0.90, 1.0)`

该资产本轮由集成工作树作为唯一基线写入并随本提交冻结。M7 合入后只读消费，不在同一集成周期内另存或覆盖该 `.uasset`；若视觉参数必须调整，退回集成资产基线重放并形成新的共享提交。

命令行资产加载只证明路径、父材质和参数可解析，不证明最终像素正确。自发光亮度、透明排序和月球场景可读性仍需后续可见 PIE 验收。

### 3.3 本提交明确不包含

- E1 Crystal 顶砖的生成、尺寸、Transform 或结构语义。
- E1–E6 主材料覆盖。
- 建筑 `+Y` 正面的 content-to-site 转换。
- 新的 Building/Layout/Chaos Hash。
- 地图位置或 `.umap` 修改。

这些内容必须在 M7/M3 的后续冻结阶段按顺序完成。

## 4. 实现与冻结顺序

### 阶段 0：各工作树形成可恢复 checkpoint

1. 记录 M3、M7、M11 当前精确 SHA 和 `git status --short`。
2. M7 先处理自身 dirty 状态：M7 所有的 `PlanarPhysicsTestMap.umap` 单独确认；误入 M7 的 M11 UI 二进制资产不得随 M7 提交。
3. 不使用共享 stash，不跨工作树复制 `.uasset/.umap`。
4. M11 继续独立推进，不让本轮建筑冻结等待 M11 同步。

### 阶段 1：M7 合入 Crystal 基线

1. M7 审阅 `git log --oneline HEAD..master`。
2. M7 在干净 checkpoint 上执行 `git merge --no-edit master`，不直接合并其他功能分支。
3. 用 `git merge-base --is-ancestor <Crystal基线SHA> HEAD` 确认共享提交已进入 M7。
4. 重新完整阅读 `AGENTS.md` 和多工作树规范。
5. 重点检查 `ABTSM7BuildingMaterialSystem` 与 M7 当前 Chaos identity 改动的语义合并；两者必须同时保留。
6. 关闭 M7 自己的 Editor，使用唯一 UE 5.8 完整编译并运行 Crystal/Profile/Recovery 相关 fresh 自动化。
7. 在 fresh Editor 中检查 `BP_ABTSM7BuildingMaterialSystem` 新增的 `CrystalBrickHISM`，不得出现 stale subobject、模板对象或 Details 递归错误。

阶段门：M7 能生成一块独立 Crystal 测试砖，材质路径解析成功，破坏事件只发送一次 `Crystal` recovery；此时尚不生成 E1 顶砖。

### 阶段 2：M7 执行 Building Freeze V3

1. 增加按 Manifest Entry 的主材料覆盖：
   - E1/E4：Stone
   - E2/E3：Wood
   - E5/E6：Iron
2. 主材料覆盖只作用于普通主体结构砖，不覆盖 connector、device、weak candidate 和 Crystal cap。
3. 实现建筑本地 `+Y` 正面到 site frame 的唯一转换，并同时转换 OBB/Pad/Effect bounds。
4. 在 E1 顶端生成且只生成一块 Crystal cap：
   - 无上方结构；
   - 不承重；
   - 不进入弱点自动选择和坍塌目标；
   - 保留碰撞、受击、破坏和 recovery；
   - M7 在本阶段冻结精确尺寸，目标值为 `72 x 72 x 72 cm`，若网格约束不允许则在首次 Hash 前一次性改为最近合法 voxel 尺寸。
5. 重新产出 E1–E6 的 generator-local/site-local bounds、材料直方图、StaticGeometryHash、DescriptorHash、ProductionHash 和 EffectBounds。
6. 自动化固定：全局 Crystal 数量为 1、仅属于 E1、材质分配正确、朝向断言正确、无初始穿插。

`BuildingFreezeV3` 发布后，任何几何、pivot、主材料、Crystal 尺寸/位置或 site-local bounds 变化都必须重开本阶段。只改变 solver/body tuning 不重开 Building Freeze。

### 阶段 3：集成工作树加入 Fixed-Six V3 接口

1. 先合入 M7 `BuildingFreezeV3` 的精确 SHA。
2. 以向后兼容方式增加 V3 DTO；V1/V2 继续可读，V3 未完整时不切换默认生产版本。
3. V3 追加 `SurfaceKind`、支撑球心、半径、重力身份和 site-local bounds 身份。
4. 不在跨模块 DTO 中保存 Satellite UObject。
5. Crystal recovery 继续沿本基线的 `Crystal -> CrystalCore` 映射，不额外发放遭遇完成奖励。

### 阶段 4：M3 执行 Map Freeze V3

1. M3 合入包含 `BuildingFreezeV3` 的最新 master。
2. 建立遭遇槽到建筑的固定映射 `[E2, E3, E4, E5, E1, E6]`。
3. 主星只预留五个建筑 Pad；不得为 E1 保留第六个主星 Pad。
4. 遭遇槽 4 使用既有 SatelliteWindow/月球背面逻辑放置 E1，并使用 E1 的最终 site-local bounds 计算 surface pivot。
5. 冻结五个主星 Transform、一个月球 Transform、SurfaceKind、支撑球数据、Pad/EffectEnvelope、PlacementHash 和 LayoutHash。
6. 同一 Seed/Candidate 连续生成两次，结果与 Hash 必须完全一致。
7. 错误轴向、错误 SurfaceKind、错误 E1 槽位、错误 bounds 必须 fail closed。

优先用运行时确定性生成冻结位置；若没有必要，不修改共享 `.umap`。

### 阶段 5：集成工作树发布 Map Freeze V3

1. 合入 M3 `MapFreezeV3` 精确 SHA。
2. 填写最终 V3 LayoutHash，并只对批准的 Seed/Candidate 原子启用 V3。
3. 增加资源链测试：
   - Crystal cap 未破坏：无 CrystalCore，SpaceCord 不可制作；
   - cap 首次破坏：CrystalCore 恰好增加 1；
   - 制作 SpaceCord：消耗该 CrystalCore；
   - 重复 damage/remove/replay：不得再次发放。

该阶段完成即表示地图位置被冻结，M3 不再等待 Chaos 数值结果。

### 阶段 6：M7 执行 Chaos Freeze V3

1. M7 合入最终 V3 master；旧布局上的 Chaos 证据全部失效。
2. 按 E1 到 E6 从头验证：E1 使用卫星球心/径向重力，E2–E6 使用主星球心/径向重力。
3. 先验证生产几何、OBB penetration、支撑与 Crystal recovery，再跑 fixed-step 算法回归。
4. 实时 30/60/120 FPS、可见 PIE 和 hitch soak 串行执行。
5. 只改变 Chaos body/world/solver 参数且仍处于冻结 bounds 内时，仅更新 Chaos Hash；几何或 bounds 变化退回阶段 2。

### 阶段 7：最终集成候选

1. 按精确 SHA 合入 M3、M7；M11 单独评估，不与本次冻结强绑。
2. 执行 ForceUnity Development Editor 构建、共同合同、M3 Fixed-Six、M7 静态/Chaos 和 CrystalCore 供应链自动化。
3. fresh runtime 验证正式路线：`E2 -> E3 -> E4 -> E5 -> 月球 E1 -> E6`。
4. 可见 PIE 只在用户明确授权后执行；NullRHI、资产加载和可见像素是不同证据层。

## 5. 并行工作安排

| 泳道 | 可立即开始 | 等待点 |
| --- | --- | --- |
| Integration | 提交 Crystal 基线；准备 V3 DTO/资源链测试骨架 | 最终 DTO 值等待 M7 bounds，LayoutHash 等待 M3 |
| M7 | 合并 Crystal 基线；实现材料覆盖、+Y 转换、E1 cap | 最终发布后等待 M3 Map Freeze，再跑正式 Chaos |
| M3 | 预做 5+1 resolver、Satellite surface 与负向测试 | 最终位置等待 M7 site-local bounds |
| M11 | 保持独立开发与 checkpoint | 不进入本轮建筑关键路径 |

重型构建、正式实时 Chaos 和可见 PIE 串行；轻量 NullRHI 自动化最多两个并行进程，使用各工作树独立项目路径和唯一日志。

## 6. 三个冻结门

| 冻结门 | 冻结内容 | 允许后续改变 |
| --- | --- | --- |
| `BuildingFreezeV3` | 几何、主材料、+Y 正面转换、Crystal cap、site-local bounds、生成身份 | 仅不影响几何/bounds 的 Chaos tuning |
| `MapFreezeV3` | 5+1 Transform、SurfaceKind、Pad/Envelope、Placement/Layout Hash | 仅不改变位置输入的消费者实现 |
| `ChaosFreezeV3` | Body/World/Solver/Candidate/Result Hash 与实时证据 | 进入最终集成后只接受显式重开冻结 |

任何阶段发现上游冻结身份不一致都必须 fail closed，不回退旧布局，也不把 Preview/Test、NullRHI 或截图提升为生产/实时 Chaos 证据。
