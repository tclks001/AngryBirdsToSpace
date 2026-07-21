# M2：CellTopo 与连续球面实现设计

> 状态：已实现 C++；已修复球面三角形绕序，等待对 `L_ABTS_M2` 执行本阶段 PIE / Standalone 验收。
>
> 主设计见 [AngryBirdsToSpaceGameDesign.md](AngryBirdsToSpaceGameDesign.md)，PCG 上游见 [ABTSTaskGraphPCGDesign.md](ABTSTaskGraphPCGDesign.md)。

## 1. 目标与边界

M2 建立 ABTS 的球面地基：`CellTopo Sub=5` 是唯一的逻辑拓扑，连续 `Sub=7` icosphere 是由它独立表现的可见、可碰撞表面。M2 同时提供角色的径向姿态、球面切线移动与极点稳定的相机参考系；M2 不生成高度、地块、道路、水网、建筑、资源、真实重力、刚体径向力或 PCG 内容。

`AABTSM2Planet` 在 BeginPlay 同步构建：

```text
CellTopo Sub=5 -> 10,242 个逻辑 Cell、12 个五边形、Cell 邻接表
Continuous Surface Sub=7 -> 163,842 顶点、327,680 三角形、复杂碰撞
```

连续表面不保存 CellId、道路、水域或玩法占用；后续 M3+ 只能在 `LogicalCells` 的 CellId/邻接边上写入逻辑数据，并将结果投射为表现。

## 2. C++ 职责

| 类 | 职责 |
| --- | --- |
| `AABTSM2Planet` | 构建自包含的 icosphere CellTopo、生成 ProceduralMesh 连续球面和碰撞、提供表面/出生点查询，并验证全部三角形朝外。 |
| `FABTSM2Cell` | 逻辑 Cell 中心、邻居 CellId、五边形标记；不包含任何渲染状态。 |
| `AABTSM2GameMode` | M2 专属入口，装配 M2 球面角色，不把星球逻辑塞进 Character。 |
| `AABTSM2BirdCharacter` | M2 角色入口：将输入交给切线移动与球面相机，不负责 Planet 查询细节。 |
| `UABTSM2SphericalSurfaceComponent` | 查找 Planet、每帧投影到球壳、保持 Actor 局部 +Z 朝径向外侧、产生切线移动和相机基向量。 |

`ProceduralMeshComponent` 插件已在项目与 `ABTSRuntime` 模块启用。为避免 Editor 反复 Construction 造成大网格和碰撞重建，M2 只在 BeginPlay 或显式 `RebuildPlanet()` 调用时构建。

## 3. 编辑器设置

1. 编译 `AngryBirdsToSpaceEditor` 后启动编辑器。
2. 使用现有 `/Game/Maps/L_ABTS_M2`。若需要重建地图，复制 `L_ABTS_M1` 为该路径，删除平面 Floor，但保留 Sky、Directional Light、Sky Light 和 Player Start。
3. 放置一个 `ABTSM2Planet`，Actor 位置为 `(0,0,0)`；保持默认 `LogicalSubdivision=5`、`SurfaceSubdivision=7`、`PlanetRadiusCM=10000`。
4. 将 Player Start 放在 `(0,0,10140)`，旋转为 `(0,0,0)`，使 M2 Pawn 在北极附近初始化。`AABTSM2BirdCharacter` 会在 Planet 就绪后将自身投影到球壳，并保持角色局部 +Z 指向球外（所以 Down 始终指向球心）。
5. 在 World Settings 把 GameMode Override 改为 `ABTSM2GameMode`，保存地图。
6. 验收移动时，可从北极附近一路移动到侧面和南半球。此阶段使用 Flying CharacterMovement + 每帧表面投影，目的是验证球面坐标系，不应将其解释为真实重力、跳跃或物理碰撞方案。

不要把 `AABTSM2Planet` 的 Construction Script 设为自动重建，也不要把 `LogicalCells` 作为蓝图可写状态。

## 4. 验收

1. PIE 日志出现：

```text
[ABTS][M2] Planet rebuilt. CellTopoSub=5 Cells=10242 SurfaceSub=7 Triangles=327680 InwardTriangles=0 Ready=1
[ABTS][M2] Dedicated planet entry ready. CellTopo is logic; continuous surface is collision/presentation only.
```

2. `InwardTriangles=0`。该值验证的是 **ProceduralMeshComponent 实际使用的正面绕序**；本项目必须以 PMC 的可见正面为准，不能仅凭代数叉积方向推断。顶点法线仍显式使用 `+UnitCenter` 朝外；不得依赖渲染输出时逐三角形交换顶点的临时修正。
3. 无论角色位于北极、赤道附近或南半球，角色局部 +Z 都径向背离球心，故其 Down 方向始终朝向球心；鼠标/移动不会在两极发生固定坐标系导致的翻转。
4. 在 Details 或蓝图读取 `GetLogicalCellCount()` 返回 `10242`，`GetSurfaceTriangleCount()` 返回 `327680`，`GetInwardSurfaceTriangleCount()` 返回 `0`，`IsPlanetReady()` 为 true。
5. Standalone 新进程重复 1–4；不得引用 TerraCivilization 源码/模块。

## 5. 排错

| 现象 | 检查 |
| --- | --- |
| 网格不可见 | 确认场景没有平面遮挡，Actor 未缩放为 0，且本次 PIE 有 M2 rebuild 日志。 |
| `InwardTriangles` 非 0 或 Lit 面看似反向 | 以 PMC 的实际正面为准：ABTS 初始 20 面使用 Terra `PrimalTris` 的 `(A,B,C)` 顺序。其代数叉积朝内是正常的；PMC 以该顺序识别球外可见正面，显式顶点法线仍使用 `+UnitCenter` 朝外。不要用“叉积必须朝外”的统计替代实际 PMC 绕序验证，也不要在 `BuildContinuousSurface` 中逐面交换索引。 |
| M2 地图仍生成 M1 Pawn | 旧 `BP_ABTSM2GameMode` 可能保存了 M1 DefaultPawnClass 覆盖。当前 C++ GameMode 会在 BeginPlay 检测并替换该旧 Pawn；编辑器中仍建议把 Blueprint 的 Default Pawn Class 更新为 `ABTSM2BirdCharacter`。 |
| Bird 穿透表面 | 检查 `ContinuousSurface` 的 Collision Enabled 和 `BlockAll` Profile；等待首次复杂碰撞 cook 完成后再落下。 |
| `Ready=0` | 不要把逻辑 Sub 改到 6 以上；检查输出的 Cell/三角形数量。 |
| 编辑器卡顿 | Sub=7 约 32 万三角形且同步创建碰撞，属于 M2 一次性生成成本；不要在 Construction 中重建。 |
| 极点附近移动/镜头翻转 | 检查角色是否是 `AABTSM2BirdCharacter`，以及 `SphericalSurface` 是否存在；M2 必须将移动与镜头向量投影到当前径向 Up 的切平面，而不是用固定世界 XY。 |
| 角色向球内倒伏 | 检查 `UABTSM2SphericalSurfaceComponent::ApplyActorFrame` 使用 `MakeFromXZ(Forward, RadialUp)`；`RadialUp` 为 `normalize(CharacterLocation - PlanetCenter)`。 |
| 角色贴地但仍可横向飞离 | 这是 M2 的已知边界：目前是 Flying Movement 后每帧投影，不是物理重力。M4/物理阶段再替换为真正的径向重力与碰撞响应。 |
