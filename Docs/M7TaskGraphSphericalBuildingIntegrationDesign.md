# M7 收口：TaskGraph 球面建筑集成

> 状态：TaskGraph 球面建筑生产路径已迁移至 M7.3-DAG2.3。Furnace 的 Tripod 接触质心偏置已修复，并增加方柱净空与 6% 生产接触面积底线；UE 5.8 Editor 编译、`ABTS.M7` 14 项、`ABTS.M73` 13 项及最终二进制三次 fresh D3D12 实时 60 FPS 均通过。待在可见 PIE 中补做外观、实际击打与回收验收。
>
> 上游：[Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [M7 材料与装置](M7BuildingMaterialsAndDevicesDesign.md)。生成器父级：[M7.3-DAG-2 空间布局与模块编译](M73DAG2SpatialLayoutAndModuleCompilationDesign.md) · [M7.3-DAG2.3 累计荷载与联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md)。
>
> 下游修订：[M11.0 终局前置收口](M110PreFinaleClosureDesign.md)已将 `LaunchSite` 保留为无建筑的终局施工台和 Space-only 槽位区。
>
> 历史对照：[M7.3-A](M73AStableBlockBuildingImplementationDesign.md) · [M7.3-B](M73BWeakPointAndDifficultyDesign.md) · [M7.3-B2](M73B2StructuralWeaknessAndFailureValidationDesign.md)。这些 Legacy 生成稿不再是 TaskGraph 生产路线。
>
> 边界：本稿负责 DAG2.3 的球面生产接入；不实现 DAG-3 内部弱点、WFC、Beam/Span、新建筑装置语法或任务完成逻辑，也不继续维护 `LegacyLayeredAB2` 的 TaskGraph 生成。

## 1. 目标

每次 M3 TaskGraph 世界生成后，`bBuildingAnchor` 仍由 `CellTopo` 决定。水网、道路完成后，PCG 会在每个建筑 Task 内寻找一个完整邻域均为 `bBuildable && !bWater` 的 Anchor，而不是只验证任务 Seed Cell。`BuildingPadClearanceRingCells` 控制这个逻辑施工范围；找不到范围时整次 PCG 尝试会拒绝并重试。M7 在玩家出生、共享 `AABTSM7BuildingMaterialSystem` 就绪后读取这些 Anchor，为每个已配置 Task 生成一栋 M7.3 建筑。

```text
CellTopo Task / bBuildingAnchor
  -> FABTSM3BuildingSpawnSite
  -> 局部切平面 Building Pad
  -> AABTSM7GameMode DAG Task Profile
  -> RecursiveSupportDAG / DAG-1
  -> DAG2.1 + DAG2.2 + DAG2.3
  -> FABTSM73StructureData
  -> GroundAdapter / IdleValidation
  -> M7 Brick Actor / M6 Chaos 破坏链
```

TaskGraph 只表达任务、道路、水网、可建造格和 Anchor；它不读取建筑网格、碰撞或 Chaos 结果。M3 仍是 Anchor/Pad 的权威上游，但不再选择或调用建筑生成算法。材质、DAG Preset、物理尺寸和预算由 M7 GameMode 的任务 Profile 决定。

## 2. 球面施工台

`AABTSM3Planet.BuildingPadSettings` 是唯一的球面施工台参数：

| 参数 | 含义 |
| --- | --- |
| `bEnableTerrainFlattening` | 是否将 Anchor 周围连续表面变为施工台；关闭时仍可生成建筑，退回 M7.3 的 FoundationCap/Feet 适配。 |
| `HalfExtentCM` | 内部水平矩形的半宽/半深，必须覆盖该 Profile 建筑占地及 `FoundationMarginCM`。 |
| `EdgeBlendWidthCM` | 内部施工台到原始高度场的平滑过渡宽度。 |
| `PCGConfig.BuildingPadClearanceRingCells` | CellTopo 逻辑认证半径；默认 2 环，必须覆盖最大建筑占地和地基边距。 |

施工台不是把整片球面压成平面，而是将从球心出发的每条射线与 Anchor 处局部切平面相交：

```text
Rpad(D) = Ranchor / dot(AnchorRadialUp, D)
Rfinal  = lerp(Rterrain, Rpad, PadBlend)
```

内部表面因此与 Anchor 的径向 `Up` 正交；边缘用 SDF 矩形距离平滑衔接。连续网格、地表法线、角色/物体地形查询和 HISM 摆放都读取同一个 `FABTSM3TerrainVisualField`，不会出现“渲染地面平了但碰撞仍是旧坡面”的双源问题。

`CellTopo` 仍是唯一逻辑源：Anchor、可建造性、水域排除和任务归属均来自 Cell；施工台只是基于 Anchor 的连续表面/物理表现。施工台范围内自动排除树石 HISM，避免装饰与建筑地基重叠。

这层逻辑施工范围不能由 `GroundAdapter` 在运行时放宽：否则建筑会看似落在平地上，实际却跨越水域或不可建 Cell。运行时 `FootprintCellNotBuildable` 现在应只表示 Profile 占地大于 PCG 的 `BuildingPadClearanceRingCells` 认证范围，需同步提高该 PCG 参数或缩小 Profile。

## 3. Task 到建筑的映射

在 `AABTSM7GameMode.TaskGraphBuildingProfiles` 中，每一项仍以 `TaskType` 为键，但生产合同已经收窄为：

- `bSpawnBuilding`：该 Task 是否生成建筑；
- `GenerationSettings.PrimaryMaterial`：该任务的 M7 材料；
- `DAGGenerationSettings`：Preset、确定性 Seed 与扩展预算；
- `DAGLayoutSettings`：根 Scope、Plate/Column 和联合支撑参数；
- `GenerationAlgorithm` 在 TaskGraph 边界必须为 `RecursiveSupportDAG`。DAG Reject 必须显式失败，不能回退 Legacy。

`FABTSM7TaskGraphDAG23ProfileResolver` 同时承担两件事：

- 新建 C++/Blueprint 默认 Profile 直接使用 DAG2.3；
- 已保存的 M7/M9/M10 Blueprint CDO 若仍序列化 `Algorithm=0`，在生成边界升级为该 Task 的安全 DAG Profile，并记录 `MigratedLegacy=1`；不会重新调用旧生成器。

显式编写的 `Algorithm=1` Profile 仍可修改 DAG Preset/Layout；Resolver 关闭尚未实现的 DAG-3 弱点预算，并只对铁质 Furnace 施加 `MinSupportContactAreaRatio>=0.06` 的生产安全底线。这个边界也覆盖旧 Blueprint CDO 已序列化的显式 DAG Profile，避免旧 4% 值绕过新原生默认值；除此以外不覆盖合法 DAG 参数。默认生产表为：

| Task | 材料 | DAG Preset | Budget / MaxDepth | Layout `W×D×H cm` | 最小接触比 | `MaxBrickCount` |
| --- | --- | --- | --- | --- | ---: | ---: |
| `Workshop` | 木 | `SingleTower` | `0 / 0` | `360×260×480` | 4% | 20 |
| `TargetBuilding` | 石 | `TwinTowerBridge` | `0 / 0` | `460×300×520` | 4% | 24 |
| `FurnaceRuins` | 铁 | `SingleTower` | `0 / 0` | `400×280×480` | 6% | 20 |

首版使用 Budget=0 是有意的稳定迁移：仍完整经过 DAG2.3 累计荷载、联合支撑、模块编译与真实接触审计，但物理刚体数不随 Cell Seed 改变。当前分别编译为 13、17、13 个模块。Target 的 `TwinTowerBridge` 含 Parallel 和四条联合支撑，确保 DAG2.3 真正进入生产覆盖。递归 Budget=1 必须先补充 Seed sweep 和物理砖预算回归，不能直接作为球面默认。

每栋建筑的 Seed 仍由 `WorldSeed + TaskId + CellId` 确定，并注入 DAG Seed。相同世界 Seed 的 Task、Preset、Brick/Support 数与 `DAGHash` 必须复现。`MaxTaskGraphBuildingAngularSpanDegrees=7°` 继续作为硬上限；当前三套 Scope 加地基边距均落在 `(650,450)cm` 施工台和该角跨度内。

M7 在创建建筑前向 M6 开启“必需建筑集合合同”：当前生产世界登记 `Expected=3`，每个成功创建的 Actor 按引用注册，全部尝试结束后才封口。合同激活后门禁只统计这组注册引用，不受无关测试 Actor 干扰，并要求每个引用都显式进入 `Accepted`；`NotRequired` 不能作为必需建筑的验收替代。MaterialSystem、Profile、StableBuildingClass、Actor Spawn 任一失败，封口时 `Registered != Expected`，或 `Accepted != Expected`，都会成为 Setup/Validation Reject；因此“本应有三栋但现场为零栋”不能再被 Actor 枚举误判成合法空关。历史 `bSpawnStableBuildingAtFirstAnchor` 只允许在关闭 TaskGraph 建筑时作为独立测试使用，不能成为 DAG 失败后的 Legacy fallback。

### 3.1 当前玩法边界

DAG2.3 会复用 M7 Runtime Module、材料损伤、激活、击碎、二次碰撞和 M8 回收，所以建筑仍可被鸟击打并发生物理坍塌。但 DAG-3 Failure Frontier 尚未实现：

- `WeaknessPlanner=0`、`WeakPoints=0` 是当前 DAG 生产建筑的预期结果；
- 旧 B/B2 顶冠弱材质、唯一弱点与难度分数不再属于生产合同；
- 首版应把可读的主承重柱作为攻击提示，不能宣称已有“唯一弱点”；
- 若后续恢复严格的弱点玩法，必须实现 DAG-3，不能把 TaskGraph 切回 Legacy。

### 3.2 M11.0 的 LaunchSite 硬边界

旧版本曾把 `LaunchSite` 映射为玻璃 `TwinTowerBridge`。该映射现已退役：

- C++ 默认 Profile 不再添加 `LaunchSite`；
- 建筑生成循环遇到 `Site.TaskType == LaunchSite` 时必须无条件跳过；
- 第二条是旧 Blueprint CDO/历史关卡仍序列化旧 Profile 时的兼容防线，不能删除；
- 跳过建筑后仍保留施工台平整、净空和 HISM 排除，供唯一 Space-only 槽使用。

现行终局槽和建筑退役验收见 [M11.0 第 4 节](M110PreFinaleClosureDesign.md#4-launchsite-建筑退役)。

## 4. 编辑器配置与验收

1. 在正式球面地图使用 `AABTSM7GameMode` 或其 Blueprint 子类。
2. 在 GameMode 的 `ABTS | M7 | TaskGraph Buildings` 保持 `Spawn Task Graph Buildings=true`；`Max Task Graph Buildings` 默认 8。
3. 展开 `Task Graph Building Profiles`，只调整普通任务的材料、DAG Preset/Budget 和 Layout；`Silhouette/Levels` 是 Legacy 兼容字段，不决定生产 DAG。不要重新添加 `LaunchSite`。
4. 在 Planet 上保持 `Enable Terrain Flattening=true`，将 `Half Extent CM` 设为大于建筑实际占地半尺寸加 `Foundation Margin CM`；首次建议 `(650, 450)`，`Edge Blend Width CM=180`。
5. 若增大 Layout，同步检查施工台、`BuildingPadClearanceRingCells`、`MaxBrickCount` 与 `7°` 上限；不得以放宽 Idle 阈值掩盖不稳定结构。
6. PIE 后检查日志：

```text
[ABTS][M7][TaskGraphBuilding] ... Algorithm=1 DAGPreset=... DAGBudget=... DAGMinContact=... MigratedLegacy=...
[ABTS][M11.0][LaunchSite] Certified pad retained; M7 building suppressed Task=... Cell=...
[ABTS][StartupPhysics] BuildingContractSealed Expected=3 Registered=3 SetupRejected=0
[ABTS][M7.3-A][Generated] ... Algorithm=1 DAGMacro>0 DAGSparse>0 DAGHash!=0 ... Accepted=1
[ABTS][M7.3-A][IdleValidation] ... Accepted=1
[ABTS][StartupPhysics] Complete ... BuildingAccepted=3 BuildingRejected=0 BuildingExpected=3 BuildingRegistered=3
```

7. 每栋建筑必须先完成零穿透预检，再完成 IdleValidation；`StartupPhysics Complete` 不能替代逐 Actor 验收，且必须晚于所有 Actor 的 terminal 日志。任一 `Accepted=0`、`Algorithm=0`、`DAGNoJointSupportHull`、`DAGMissingRequiredContact` 或 `DAGUnexpectedBypass` 都是阻断项。Furnace 还必须记录 `DAGMinContact=0.060`。
8. `Rejected` 不是可放行的 terminal：它必须阻止 `WorldReady=1` 和发射模式。该门禁独立于可关闭的 HISM Startup Warmup，因此关闭暖机也不能绕过 Pending/Running/Rejected 建筑；必需集合合同未封口、Setup Reject、`Registered != Expected`、`Accepted != Expected` 或任一必需 Actor 为 `NotRequired` 同样阻断。
9. 观察每栋普通建筑周边：施工台内应水平、边缘没有突然台阶、没有树石 HISM 穿入地基；实际发射后模块能受力、损伤、击碎并回收。另检查 `LaunchSite` 施工台平整且无建筑/地基，只保留 Space-only 槽。

### 4.1 当前自动与冷启动证据（2026-07-28）

- `AngryBirdsToSpaceEditor Win64 Development` 编译成功；
- `ABTS.M7`：14/14 Success；`ABTS.M73`：13/13 Success；其中 `ABTS.M7.TaskGraphDAG23ProfileRouting` 覆盖旧 CDO 升级、三套生产 Profile、13/17/13 模块 golden 拓扑与 Hash、LaunchSite 拒绝，以及合同 open/count mismatch/setup reject/exact match/`NotRequired`/Accepted 数量世界门禁；
- Tripod 宽向/深向接触质心自动化、Furnace 6% 实际接触比/柱细长比及显式旧 DAG CDO 运行时升级均通过；13/17/13 模块数和抽象 `DAGHash` 保持不变；
- 历史算法基线的两次独立 `UnrealEditor -game -NullRHI` 均为相同 Seed/Brick/Support/Hash、`Repairs=0 LargeErrors=0 RemainingSmall=0`、三栋 `IdleValidation Accepted=1`；两次都先封口 `Expected=3 Registered=3 SetupRejected=0`，`WorldReady=1` 最终报告 `BuildingAccepted=3 BuildingRejected=0 BuildingExpected=3 BuildingRegistered=3`，且没有 TaskGraph `Algorithm=0`；
- 最终二进制另有三次不带 `-benchmark` 的 fresh D3D12 实时 60 FPS：三栋均 `TimedOut=0 Accepted=1`，Furnace `MaxRotation=0.08°/0.09°/0.08°`、`DAGMinContact=0.060`，门禁均为 `Accepted/Rejected/Expected/Registered=3/0/3/3`，且 `WorldReady=1`、无 `LogABTSRuntime: Error` 或 `WorldReadyBlocked`；
- `-benchmark` 会令 UE 使用固定时间步，只能证明算法确定性，不能单独代替实时 PIE Chaos 验收；正式稳定性回归必须包含不带 `-benchmark` 的实时 30/60/120 FPS、新进程 D3D12 以及可见 PIE/hitch soak。

### 4.2 PIE 定位调试

`AABTSM7GameMode` 在 `ABTS | M7 | Debug` 默认开启 `Show Task Graph Position Debug`。PIE 时屏幕左上会持续显示玩家和已生成 TaskGraph 建筑的经纬度（纬度为 `-90°~+90°`，经度为 `-180°~+180°`），建筑条目同时带有 Task/Cell 标识。场景中每栋建筑上方还会显示同一组 `B序号 / Task / Lat / Lon` 标签，可将屏幕列表和现场目标对应。

如需录制画面或观察建筑本身，可关闭 `Draw Task Graph Building World Labels`；不需要定位辅助时关闭 `Show Task Graph Position Debug` 即可。经纬度一律由 `(WorldPosition - PlanetCenter).GetSafeNormal()` 计算，仅用于调试导航，不参与 CellTopo 的任何逻辑判定。

## 5. 排错

| 现象 | 原因与处理 |
| --- | --- |
| 没有建筑 | 检查 GameMode 是否为 M7、`Spawn Task Graph Buildings`、对应 `TaskType` Profile 的 `bSpawnBuilding`，以及 `[TaskGraphBuilding]` 日志。 |
| 施工台仍有坡 | 检查 Planet `Enable Terrain Flattening`；确认建筑 Anchor 为当前重建世界生成的 Anchor，而不是旧 Actor 位置。 |
| 建筑被 `FootprintCellNotBuildable` 拒绝 | Profile 占地超出施工台或覆盖水域/不可建 Cell；缩小建筑/施工台，或调整 TaskGraph Anchor 策略。 |
| 施工台上仍有树石 | 重新生成 Planet；HISM 在重建时才依据 Pad 排除范围重新摆放。 |
| 日志仍出现 `Algorithm=0` | 活动 GameMode 仍在绕过 TaskGraph Resolver，或运行的是旧 DLL。重新编译并确认 `[TaskGraphBuilding]` 带 `Algorithm=1`；不允许用 Legacy 结果继续验收。 |
| 三种任务都变成 SingleTower | 只翻了算法枚举，却没有处理旧 CDO 中默认 DAG Preset。必须经 Resolver 将 Target 映射到 `TwinTowerBridge`。 |
| `[Generated] Accepted=1`，稍后 Idle `Accepted=0` | 前者只表示静态生成通过。按 Drift/Settlement/Rotation 找到失稳模块；不能放宽门槛，也不能以 `StartupPhysics Complete` 放行。 |
| Furnace/B2 标签处为空，日志却先有 13 模块 `Generated Accepted=1`，随后 Node 6/9 在 Idle 超时并约 `2.02°` 拒绝 | 这不是漏生成；Reject 会事务删除主体和地基。旧 Tripod 的接触质心偏离合力中心，且铁结构旧 4% 面积余量不足。必须使用共享居中 Tripod 几何，并确认该任务 `DAGMinContact=0.060`；不要放宽 2° 门槛。 |
| Idle 日志统一为速度 0/Awake 0，且晚于 WorldReady | M6 启动预热提前冻结了 M7.3 模块。现行状态机要求 M7.3 独占验证/冻结；M6 等待 Pending/Running 归零，并仅在 `Rejected=0` 时放行。 |
| 提高递归 Budget 后突然超出刚体预算 | 当前首版只批准 Budget=0。启用 Budget=1 前先补编译后物理砖数硬门槛和 TaskGraph Seed sweep。 |

## 6. 后续

TaskGraph 生产生成器已经在 DAG2.3 收口。后续按顺序补 DAG-3 内部 Failure Frontier、Budget=1 Seed sweep/物理预算，再由 WFC 提供语义包络；这些阶段都复用 `CellTopo Anchor -> Pad -> DAG Profile -> Runtime Building`，不得恢复 Legacy fallback。
