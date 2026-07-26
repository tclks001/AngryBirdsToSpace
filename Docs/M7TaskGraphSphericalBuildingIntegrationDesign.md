# M7 收口：TaskGraph 球面建筑集成

> 状态：已实现，待 PIE 验收。
>
> 上游：[Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [M7 材料与装置](M7BuildingMaterialsAndDevicesDesign.md) · [M7.3-A 稳定积木建筑](M73AStableBlockBuildingImplementationDesign.md)。
>
> 边界：本稿只将已有 M7.3 建筑接入球面任务地图；不实现 WFC、DAG2.4、不新增建筑装置语法或任务完成逻辑。

## 1. 目标

每次 M3 TaskGraph 世界生成后，`bBuildingAnchor` 仍由 `CellTopo` 决定。水网、道路完成后，PCG 会在每个建筑 Task 内寻找一个完整邻域均为 `bBuildable && !bWater` 的 Anchor，而不是只验证任务 Seed Cell。`BuildingPadClearanceRingCells` 控制这个逻辑施工范围；找不到范围时整次 PCG 尝试会拒绝并重试。M7 在玩家出生、共享 `AABTSM7BuildingMaterialSystem` 就绪后读取这些 Anchor，为每个已配置 Task 生成一栋 M7.3 建筑。

```text
CellTopo Task / bBuildingAnchor
  -> FABTSM3BuildingSpawnSite
  -> 局部切平面 Building Pad
  -> AABTSM7GameMode Task Profile
  -> AABTSM73StableBuildingActor
  -> M7 Brick Actor / M6 Chaos 破坏链
```

TaskGraph 只表达任务、道路、水网、可建造格和 Anchor；它不读取建筑网格、碰撞或 Chaos 结果。建筑材质、规模、轮廓与难度由 M7 GameMode 的任务 Profile 决定。

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

在 `AABTSM7GameMode.TaskGraphBuildingProfiles` 中，每一项以 `TaskType` 为键，包含：

- `bSpawnBuilding`：该 Task 是否生成建筑；
- `GenerationSettings`：材质、Legacy/DAG 算法、轮廓、层数和占地；
- `DAGGenerationSettings` / `DAGLayoutSettings`：若该 Profile 选 DAG 的生成参数；
- `DifficultySettings`：弱点与结构难度参数。

`MaxTaskGraphBuildingAngularSpanDegrees` 是球面 TaskGraph 的额外硬上限，默认 `7°`。它只在启用连续 Terrain Pad 时把 Profile 的旧 `MaxSinglePlatformAngularSpanDegrees` 下限提升到该值：Gatehouse/TwinTower 的默认占地约为 `5.1°~5.8°`，可以落在当前 `(650,450)cm` Pad 内；超过 `7°` 的建筑仍须缩小 Profile 或扩大施工台后另行验证，不能无上限放宽。

默认配置覆盖当前会产生 Building Anchor 的四种任务：

| Task | 默认材料 | 默认轮廓 | 层数 |
| --- | --- | --- | ---: |
| `Workshop` | 木 | SingleTower | 2 |
| `TargetBuilding` | 石 | Gatehouse | 3 |
| `FurnaceRuins` | 铁 | SingleTower | 2 |
| `LaunchSite` | 玻璃 | TwinTowerBridge | 2 |

这是可编辑的初始难度表，不是硬编码的最终关卡平衡。每栋建筑的 Seed 由 `WorldSeed + TaskId + CellId` 确定，因此同一世界 Seed 可复现，且不同任务不会复制出同一结构。

## 4. 编辑器配置与验收

1. 在正式球面地图使用 `AABTSM7GameMode` 或其 Blueprint 子类。
2. 在 GameMode 的 `ABTS | M7 | TaskGraph Buildings` 保持 `Spawn Task Graph Buildings=true`；`Max Task Graph Buildings` 默认 8。
3. 展开 `Task Graph Building Profiles`，为各 Task 修改 `Primary Material`、`Levels`、轮廓/DAG 与难度。若 Profile 占地变大，同时在 `ABTSM3Planet > ABTS | M7 | Spherical Buildings` 增大 `Half Extent CM`，并按需提高 GameMode 的 `Max Task Graph Building Angular Span Degrees`（默认 `7°`）。
4. 在 Planet 上保持 `Enable Terrain Flattening=true`，将 `Half Extent CM` 设为大于建筑实际占地半尺寸加 `Foundation Margin CM`；首次建议 `(650, 450)`，`Edge Blend Width CM=180`。
5. PIE 后检查日志：

```text
[ABTS][M7][TaskGraphBuilding] Task=... Cell=... Material=... Pad=1
[ABTS][M7.3-A][Generated] ... Planar=0 ... Accepted=1
[ABTS][M7] Entry ... TaskGraphBuildings=...
```

6. 观察每栋建筑周边：施工台内应水平、边缘没有突然台阶、没有树石 HISM 穿入地基；发射阶段建筑仍进入既有 Chaos 重力与破坏链路。

### 4.1 PIE 定位调试

`AABTSM7GameMode` 在 `ABTS | M7 | Debug` 默认开启 `Show Task Graph Position Debug`。PIE 时屏幕左上会持续显示玩家和已生成 TaskGraph 建筑的经纬度（纬度为 `-90°~+90°`，经度为 `-180°~+180°`），建筑条目同时带有 Task/Cell 标识。场景中每栋建筑上方还会显示同一组 `B序号 / Task / Lat / Lon` 标签，可将屏幕列表和现场目标对应。

如需录制画面或观察建筑本身，可关闭 `Draw Task Graph Building World Labels`；不需要定位辅助时关闭 `Show Task Graph Position Debug` 即可。经纬度一律由 `(WorldPosition - PlanetCenter).GetSafeNormal()` 计算，仅用于调试导航，不参与 CellTopo 的任何逻辑判定。

## 5. 排错

| 现象 | 原因与处理 |
| --- | --- |
| 没有建筑 | 检查 GameMode 是否为 M7、`Spawn Task Graph Buildings`、对应 `TaskType` Profile 的 `bSpawnBuilding`，以及 `[TaskGraphBuilding]` 日志。 |
| 施工台仍有坡 | 检查 Planet `Enable Terrain Flattening`；确认建筑 Anchor 为当前重建世界生成的 Anchor，而不是旧 Actor 位置。 |
| 建筑被 `FootprintCellNotBuildable` 拒绝 | Profile 占地超出施工台或覆盖水域/不可建 Cell；缩小建筑/施工台，或调整 TaskGraph Anchor 策略。 |
| 施工台上仍有树石 | 重新生成 Planet；HISM 在重建时才依据 Pad 排除范围重新摆放。 |
| 高层双塔生成失败 | 当前 M7 收口默认使用已验证的低层 Legacy Profile；提高层数前先同步提高 `MaxBrickCount`，并使用 M7.1 平面测试台验证。 |

## 6. 后续

M7 暂时在此收口。未来的 DAG2.4/WFC 仅需替换 Task Profile 的生成算法或提供新的 Profile 数据，不改变 `CellTopo Anchor -> Pad -> Runtime Building` 的职责边界。
