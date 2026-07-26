# M7.3-A：稳定积木建筑实现设计

> 状态：已完成编辑器、PIE 与物理验收；球面 TaskGraph 批量接入与连续地形施工台见后续 [M7TaskGraphSphericalBuildingIntegrationDesign.md](M7TaskGraphSphericalBuildingIntegrationDesign.md)。
> 父级：[M7.3 程序化模块化建筑总体算法](M73ProceduralModularBuildingGenerationResearch.md)。
> 平面测试场：[M71PlanarPhysicsTestStageDesign.md](M71PlanarPhysicsTestStageDesign.md)。
> 下游：[M73BWeakPointAndDifficultyDesign.md](M73BWeakPointAndDifficultyDesign.md) 负责图选点与难度；[M73B2StructuralWeaknessAndFailureValidationDesign.md](M73B2StructuralWeaknessAndFailureValidationDesign.md) 负责 Legacy 顶部局部结构弱点；主体 Macro DAG 递归、内部 Failure Frontier 与真实接触图新路线见 [M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md](M73RecursiveSupportDAGProceduralBuildingGenerationResearch.md)；项目阶段索引见 [AngryBirdsToSpaceGameDesign.md](AngryBirdsToSpaceGameDesign.md)。

## 1. 阶段目标与边界

M7.3-A 只完成“能够可靠站立并进入现有 M6/M7 破坏链路”的积木建筑基础，不提前实现弱点搜索、绳/链、炸药桶、弹簧活塞或多目标难度搜索。

本阶段实现：

- `SingleTower`、`Gatehouse`、`TwinTowerBridge` 三种结构轮廓；
- 同一套局部建筑数据同时适配 M7.1 平面实验台和 M3 球面；
- 占地范围采样、局部平坦施工台、自适应地基脚；
- 砖块接触关系形成的支撑 DAG；
- 初始穿透、Ground 路径、接触面积和重心投影静态校验；
- 生成完成后的短时隐藏 Chaos 空载稳定性验证；
- 通过现有 `AABTSM7BuildingMaterialSystem` 生成可参与发射阶段重力、碰撞、累计损伤和坍塌的砖块 Actor；
- M7.1 中可直接拖入生成器、调整参数并使用现有弹弓击打；
- M7 正式球面入口可选择在第一个 TaskGraph 建筑 Anchor 生成测试建筑。

本阶段暂不实现：

- 图分析驱动的显式弱点；
- 多平台球面建筑；
- 绳、锁链、炸药桶和活塞结构语法；
- 搜索式 PCG、攻击探针和难度评分；
- 将 M3 连续地形本身切削成施工台（已由后续 M7 收口阶段独立完成，见 [M7TaskGraphSphericalBuildingIntegrationDesign.md](M7TaskGraphSphericalBuildingIntegrationDesign.md)）；
- 自动材料掉落、建筑库存和任务完成条件。

## 2. 实现决策

### 2.1 为什么建筑砖使用 Actor

M7.3-A 的单栋建筑只有有限数量的 Gameplay 砖。为执行空载 Chaos 验证并保留逐块碰撞/损伤，建筑主体通过：

```text
AABTSM7BuildingMaterialSystem::SpawnBrickModule
-> AABTSM7BuildingModule
```

生成。

它们在普通状态下是静态碰撞 Actor；空载验证时短暂启用 Chaos，达到连续静稳窗口后在已经落座的位置再次冻结；进入 M6 发射阶段后由现有 `BeginLaunchPhysics` 统一激活。因此不会出现编辑器预览 HISM 与运行时碰撞体重复，也不会把预沉降结果强行恢复到尚未解算接触的初始 Transform。

### 2.2 施工台的 M7.3-A 表示

M7.3-A 为优先兼容 M7.1，原始实现采用非破坏式表示：

```text
原地面
-> FoundationFeet
-> 平坦 FoundationCap（本阶段的 BuildingPad 实体）
-> Gameplay 建筑主体
```

`FoundationCap` 是真正参与碰撞的平坦施工面，位于 Footprint 内最高地面之上；地基脚填补其与地表之间的高度。后续 M7 收口已增加 `AABTSM3Planet.BuildingPadSettings`：连续表面在每个 CellTopo Anchor 的局部切平面内变平，并与原始高度场平滑相接。`GroundAdapter` 仍保持不变，因为它本来就通过 `QuerySurface` 消费最终地表；FoundationCap/Feet 继续保留，用于碰撞稳定和关闭 Terrain Pad 时的兼容回退。

### 2.3 地形适配层不作为 Gameplay 弱点

`FoundationFeet` 和 `FoundationCap` 属于承载面适配层，保持静态且不会被 M6 提升为动态模块。建筑的第一层柱、梁才属于可破坏 Gameplay 结构。

这样避免向下缩放普通砖导致：

- 质量和转动惯量异常增大；
- 地基变成无法推动的巨大锚点；
- 受击面积与破坏难度随地形高度随机变化；
- 平面与球面测试结果不可比较。

## 3. 模块结构

| 文件/类 | 职责 |
| --- | --- |
| `ABTSM73BuildingTypes.h` | Ground Mode、轮廓类型、编辑器 Settings 和结果 Summary |
| `ABTSM73StructureData.h` | 生成期砖块、支撑边、地面采样、地基脚和 Ground Context |
| `FABTSM73StructureBuilder` | 确定性生成三种积木结构 |
| `FABTSM73GroundAdapter` | 统一 M7.1 Floor 与 M3 球面查询，执行 Footprint/地基分析 |
| `FABTSM73StabilityValidator` | 穿透、Ground 路径、接触面积和重心校验 |
| `AABTSM73StableBuildingActor` | 编辑器预览、运行时装配、空载 Chaos 验证和日志 |
| `AABTSM7BuildingMaterialSystem::SpawnBrickModule` | 按现有 M7 Profile 生成静态砖块模块并注册到发射链路 |
| `AABTSM71PhysicsTestGameMode` | MaterialSystem 创建后初始化场景中的所有 M7.3-A 生成器 |
| `AABTSM7GameMode` | 可选在第一个球面 Building Spawn Site 生成一栋 M7.3-A 建筑 |

依赖方向：

```text
StructureBuilder -----> StructureData
GroundAdapter --------> StructureData + M7.1 Stage / M3 Planet 查询
StabilityValidator ---> StructureData
StableBuildingActor --> 三个纯 C++ 服务 + M7 MaterialSystem
```

Builder 和 Validator 不持有 UObject，不读取 World，不产生 Actor。

## 4. 平面与球面统一契约

`FABTSM73GroundContext` 输出：

```text
bPlanar
AnchorTransform
GravityUp
PlaneOrigin
Planet/TestStage
AnchorCellId
```

### 4.1 M7.1 平面模式

- 自动寻找 `AABTSM71PhysicsTestStage`；
- Floor Actor 位置就是承载平面；
- `GravityUp = Stage Actor +Z`；
- 默认 `bSnapPlanarAnchorToTestStage=false`，生成器 Actor 的完整 XYZ 位置就是局部施工平面，建筑主体、FoundationCap 和 FoundationFeet 会随 Actor 整体自由移动；
- 打开 `bSnapPlanarAnchorToTestStage` 时，才把生成器位置沿 Up 投影到 Floor，提供一键贴地的旧行为；
- 生成器本地 `+X` 投影到 Floor，作为攻击方向；
- 理想 Floor 的所有地面采样高度均为 0；
- `CurvatureDropCM = 0`、`TerrainDeltaCM = 0`、`MaxSlopeDegrees = 0`。

### 4.2 球面模式

- Anchor 必须是有效 `CellTopo` CellId，或从 Actor 所在径向查询最近 Cell；
- `GravityUp` 固定为 Anchor Cell 径向，不使用局部地表法线倾斜整栋建筑；
- Footprint 点从 Anchor 切平面投影为球心方向，再调用 `AABTSM3Planet::QuerySurface`；
- 每个采样 Cell 必须 `bBuildable && !bWater`；
- 记录曲率高差、地形高差、最大法线倾角和地基深度；
- 圆心角跨度、高差、坡度或地基深度超限时拒绝，而非无限拉长底砖。

## 5. 三层防曲面算法

### 5.1 占地范围校验

采样点包括：

- Footprint 规则网格；
- 中心、四角和边界点；
- 所有 Ground Support 节点下方；
- 地基脚最终位置。

硬拒绝条件：

```text
Surface Query 失败
球面覆盖 Cell 不可建造或为水域
TerrainDelta > MaxTerrainDeltaCM
MaxSlope > MaxBuildingPadSlopeDegrees
AngularSpan > MaxSinglePlatformAngularSpanDegrees
FoundationDepth > MaxFoundationDepthCM
```

球面曲率诊断使用：

```text
CurvatureDropCM = R - sqrt(R² - ρ²)
```

### 5.2 局部平坦施工台

本阶段的施工台是静态 `FoundationCap`：

```text
CapBottom = MaxSampledGroundHeight + FoundationTopClearanceCM
CapTop = CapBottom + FoundationCapThicknessCM
```

建筑 Builder 始终以 `Z=0` 为主体底面；Spawner 将所有主体砖整体上移 `CapTop`。因此建筑语法永远在同一平面内运算，不随球面逐砖弯曲。

### 5.3 自适应地基脚

每个承重点的地基脚长度：

```text
FootBottom = SampledGroundHeight - FoundationEmbedDepthCM
FootTop = CapBottom
FootLength = FootTop - FootBottom
```

地基脚至少覆盖四角和主体 Ground Support。平面上各脚长度应完全一致；球面上长度随曲率和地形变化，但不得超过上限。

## 6. 三种结构语法

### 6.1 SingleTower

- 每层四根角柱；
- 柱顶放置完整楼板/梁；
- 上层轮廓轻微收分；
- 每层柱由下层楼板支撑；
- 适合作为最小稳定性与参数回归样本。

### 6.2 Gatehouse

- 左右各一座塔体；
- 两塔各自具有独立接地路径；
- 最顶部新增一层连接梁，不能与塔顶梁同层重叠；
- 可验证多 Ground Component 汇合为一个上层结构。

### 6.3 TwinTowerBridge

- 两个独立塔体；
- 在约半高楼层生成真正的中部连廊，而不是在两塔顶部再放一根门梁；
- 连廊由一块铁质桥面和沿攻击方向两侧的护栏组成；
- 桥面沿 Y 跨越两塔，并在两端伸入塔楼板形成竖向支撑；
- 桥面沿 X 保持较窄，位于下一层前后角柱之间的中央通道，因此不会与角柱穿透；
- 护栏由桥面支撑，使 TwinTowerBridge 在轮廓和支撑图上都与 Gatehouse 明确不同。

Seed 只在安全范围内扰动宽度、深度和层高，保证同一 Seed 可复现且不会用完全随机旋转破坏稳定性。

## 7. 支撑 DAG 与静态校验

### 7.1 支撑边

若下层砖顶面与上层砖底面在容差内相接，且 XY 接触面积大于零，则建立：

```text
LowerNode -> UpperNode
ContactAreaCM2
```

底面位于主体 `Z=0` 的节点标记为 Ground Node；它们实际由 FoundationCap 支撑。

### 7.2 穿透

所有砖使用生成尺寸形成局部 AABB。三轴均产生超过 `0.25cm` 的正重叠时立即拒绝。相互接触不算穿透。

### 7.3 Ground 路径

从每个非 Ground Node 沿支撑边向下递归，必须能到达至少一个 Ground Node。缓存结果并检测环路。

### 7.4 接触面积与重心

- 合并所有直接支撑者提供的接触面积；
- `ContactArea / UpperBottomArea` 必须达到 `MinContactAreaRatio`；
- 上层块中心投影必须落在所有有效接触斑块的包围区域中；
- 桥梁允许由左右两个分离支点共同支撑。

这是进入 Chaos 前的廉价硬门槛，不替代运行时物理验证。

## 8. Chaos 空载稳定性验证

运行时砖块生成后：

1. 收集本建筑的全部 Runtime Module，并在启用 Chaos 前复用 M7 `PenetrationValidator` 检查初始穿透；
2. 只要结果包含任意 `Repairs>0`、`LargeErrors>0` 或 `RemainingSmall>0`，立即以 `IdlePenetrationInvalid` 拒绝，不进入隐藏 Chaos。共享 Validator 可能为了给出完整诊断而执行误差内微调，但 M7.3-A 不再把“修过以后能模拟”视为通过，防止运行时几何偏离 Builder 支撑图、Contact Hull 和 B2 `TipMargin`；仅零穿透、零修复时才记录原始 Transform 作为 Idle 基线；
3. 隐藏模块渲染但保留真实碰撞，并设置覆盖最长验证时间的接触损伤 Grace，防止初始接触被当成破坏；
4. 平面模式施加恒定 `-GravityUp`，球面模式施加朝向 Planet Center 的径向重力；
5. 至少模拟 `IdleValidationSeconds=1.25s`。达到最短观察时间后，只有所有刚体同时满足线速度不超过 `4cm/s`、角速度不超过 `1.5deg/s`，才累计静稳时间；任一刚体重新越界就把静稳计时归零；
6. 连续静稳达到 `IdleStableHoldSeconds=0.45s` 后通过收敛检查；若到 `IdleValidationMaxSeconds=6s` 仍未满足，则以 `TimedOut=1` 拒绝；
7. 将相对预校验基线的位移分解为施工平面内漂移和沿重力轴的接触沉降，同时记录总位移、最大旋转、结束速度和 Awake 数量；
8. 平面漂移与 `MaxIdleDisplacementCM` 比较，法向沉降与 `MaxIdleSettlementCM` 比较，旋转与 `MaxIdleRotationDegrees` 比较；速度静稳、未超时和三个几何门槛必须同时满足；
9. 在最终落座 Transform 上执行 `Freeze()` 并重新显示，不把模块传送回验证前位置；
10. 不稳定时输出 Error 和带 `TimedOut` 的 `RejectReason`，随后统一调用 `RejectRuntimeStructure`；
11. `RejectRuntimeStructure` 销毁全部 Runtime Module，清空模块数组、Node→Module 映射和 Idle Transform 缓存，停止 Tick，并隐藏、禁用 `FoundationCap` 与 `FoundationFeet`。被拒绝的半成品不会留下隐形碰撞体，也不能继续参与 M6/M7 Gameplay；
12. 后续显式重新初始化/重试时，正常装配路径会再次调用 `UpdateFoundationComponents`，恢复 Foundation 可见性、实例和 `QueryAndPhysics` 碰撞，然后再生成新模块并重新执行完整验证。

日志：

```text
[ABTS][M7.3-A][Generated]
[ABTS][M7.3-A][Reject]
[ABTS][M7.3-A][IdlePenetrationValidation]
[ABTS][M7.3-A][IdleValidation]
```

`IdlePenetrationValidation` 应先报告模块数、穿透对、修复数和最大深度；`Repairs` 也必须为 0，不能只检查 `LargeErrors`。预检拒绝时直接输出 `PenetrationRejected=1`，不会继续出现正常的 quiet-window 结果。预检通过后，`IdleValidation` 会额外报告 `Stable`、`TimedOut`、`MaxLinearSpeed`、`MaxAngularSpeed` 与 `Awake`。`Awake` 仅用于诊断，当前收敛依据是实际线/角速度连续低于门槛，而不是要求 Chaos 已把所有刚体标记为 Sleep。

无论失败发生在穿透预检还是 quiet-window 结束阶段，最终语义都不是“保留建筑但标记红灯”，而是彻底撤销本次 Runtime Structure。`GenerationSummary.bAccepted=false` 后，模块与 Foundation 都不应再阻挡鸟、弹丸或其他物体。

## 9. 编辑器操作：M7.1 平面实验台

### 9.1 放置生成器

1. 打开已有 M7.1 平面物理测试地图。
2. 确认地图使用 `AABTSM71PhysicsTestGameMode` 或其 Blueprint 子类。
3. 确认场景已有：
   - `M7.1 Physics Test Stage`；
   - `M7.1 Bird Player Start`；
   - 至少一个完整 M7.1 弹弓。
4. 在 Place Actors 的 All Classes 中搜索 `M7.3-A Stable Building Generator`。
5. 把 Actor 拖到 Floor 上。
6. Actor 本地 `+X` 箭头指向预期来弹方向；旋转 Z/Yaw 调整受击朝向。
7. 保持 Actor Scale 为 `(1,1,1)`；建筑尺寸使用 Settings 调整。
8. 默认关闭 `Snap Planar Anchor To Test Stage`，可自由沿 XYZ 移动整栋建筑；需要一键贴到测试台时再打开。

### 9.2 建议首轮参数

```text
GroundMode = Auto
bSnapPlanarAnchorToTestStage = false
BuildingSeed = 7301
Silhouette = SingleTower
PrimaryMaterial = Wood
Levels = 3
MaxBrickCount = 50
BayWidthCM = 360
BuildingDepthCM = 260
LevelHeightCM = 190
ColumnWidthCM = 74
BeamHeightCM = 58
bRunIdleChaosValidation = true
IdleValidationSeconds = 1.25
IdleStableHoldSeconds = 0.45
IdleValidationMaxSeconds = 6
IdleLinearSpeedThresholdCMPerSec = 4
IdleAngularSpeedThresholdDegPerSec = 1.5
MaxIdleDisplacementCM = 4
MaxIdleSettlementCM = 6
MaxIdleRotationDegrees = 2
```

### 9.3 预览验收

- 编辑器移动或旋转生成器时，建筑预览实时更新；
- 地基顶板保持水平；
- 四角和承重柱下有地基脚；
- Details 中 `GenerationSummary.bAccepted = true`；
- 平面模式 Terrain Delta、Curvature 和 Slope 接近 0；
- 调整 Seed 后尺寸发生小幅可复现变化；
- 切换三种 Silhouette 后无砖块互相穿透。

### 9.4 PIE 与击打

1. PIE。
2. 等待 `[IdleValidation]` 日志；默认最早约 `1.70s` 完成，仍在运动时会继续观察，但不会超过约 `6s`。
3. Output Log 应先出现 `[Generated] Accepted=1`，再出现 `[IdleValidation] ... Accepted=1`。
4. 建筑在验证过程中不应可见抖动或弹飞。
5. 使用场景弹弓进入发射模式并击打建筑。
6. 发射开始后，所有主体砖应成为 Chaos 刚体并受平面重力。
7. 下层柱被撞开后，上层楼板和柱体应自然下落。
8. FoundationCap 和 FoundationFeet 保持静态，不随建筑一起飞走。

## 10. 编辑器操作：正式球面

当前提供两种入口：

### 手工放置

- 在球面 M7 地图放置 `M7.3-A Stable Building Generator`；
- `GroundMode = SphericalCellTopo`；
- `AnchorCellId` 指定合法建筑 Cell；
- Actor 位置只用于初始方向和编辑器识别，最终位置由 Anchor Cell Surface Query 决定。

### GameMode 自动测试

1. 使用 `AABTSM7GameMode` 的 Blueprint 子类。
2. 打开 `ABTS|M7.3-A -> Spawn Stable Building At First Anchor`。
3. 可通过 `StableBuildingClass` 指定生成器 Blueprint 子类。
4. 运行后系统读取 `AABTSM3Planet::GetBuildingSpawnSites()[0]`。
5. 在该 CellTopo Anchor 创建一栋球面建筑并接入共享 MaterialSystem。

球面日志应满足：

```text
Planar=0
TerrainDelta <= MaxTerrainDeltaCM
MaxSlope <= MaxBuildingPadSlopeDegrees
FoundationDepth <= MaxFoundationDepthCM
Accepted=1
```

## 11. 参数接口

### Generation

- `GroundMode`
- `AnchorCellId`
- `bSnapPlanarAnchorToTestStage`
- `BuildingSeed`
- `Silhouette`
- `PrimaryMaterial`
- `Levels`
- `MaxBrickCount`

### Dimensions

- `BayWidthCM`
- `BuildingDepthCM`
- `LevelHeightCM`
- `ColumnWidthCM`
- `BeamHeightCM`

### Ground

- `FoundationMarginCM`
- `FoundationCapThicknessCM`
- `FoundationFootSizeCM`
- `FootprintSampleSpacingCM`
- `MaxBuildingPadSlopeDegrees`
- `MaxTerrainDeltaCM`
- `MaxFoundationDepthCM`
- `FoundationEmbedDepthCM`
- `FoundationTopClearanceCM`
- `MaxSinglePlatformAngularSpanDegrees`

### Validation

- `MinContactAreaRatio`
- `bRunIdleChaosValidation`
- `IdleValidationSeconds`（最短观察时间）
- `IdleStableHoldSeconds`
- `IdleValidationMaxSeconds`
- `IdleLinearSpeedThresholdCMPerSec`
- `IdleAngularSpeedThresholdDegPerSec`
- `MaxIdleDisplacementCM`
- `MaxIdleSettlementCM`
- `MaxIdleRotationDegrees`
- `ValidationGravityCMPerSec2`

## 12. 验收清单

源码内置自动化测试：

```text
ABTS.M73A.DefaultStructuresAreStaticallyStable
```

它会对三种默认轮廓分别验证生成成功、砖块预算、Ground Node、Support Edge 和静态稳定性，并固定检查五层 Gatehouse/TwinTowerBridge 分别产生 `BrickBudgetExceeded:51:50` 与 `53:50`，防止该现象以后被误判为渲染丢失。该测试不能替代地图中的实际 Chaos、模型碰撞和弹弓视觉验收。

### 功能

- 三种轮廓均能生成；
- 相同 Seed/Preset 在重复 PIE 中砖块局部拓扑一致；
- M7.1 与球面共用 Builder、Validator 和主体尺寸；
- 球面只改变承载面适配和世界 Transform；
- 主体进入 M6/M7 发射与破坏链路；
- Foundation 层不被错误激活。
- Idle 验证拒绝后不残留 Runtime Module、Node 映射或 Foundation 碰撞；修正参数并重新初始化后 Foundation 和整栋结构能够重新创建。

### 稳定性

- 平面空载验证通过；
- 球面合法 Anchor 空载验证通过；
- 无明显初始弹起、自旋或整体倾倒；
- 不允许有砖块初始大穿透；
- 每个非 Ground Node 都有 Ground 路径；
- 所有地基脚深度在上限内。

### 视觉

- 建筑主体保持统一径向 Up，不随每块地面的法线分别倾斜；
- 球面边缘空隙由地基脚填补；
- 地基顶板不穿入地面；
- Gatehouse 的连接梁位于塔顶上方；TwinTowerBridge 的桥面和护栏位于中层，两种轮廓必须一眼可区分。

### 日志

- `[Generated]` 计数与所选轮廓相符；
- `[Reject]` 带明确原因；
- `[IdlePenetrationValidation]` 输出穿透对、修复数、大误差与最大深度；
- 只有 `Repairs=0`、`LargeErrors=0`、`RemainingSmall=0` 才允许进入 quiet-window；任何自动微调都必须使本次生成拒绝；
- `[IdleValidation]` 输出总时间、连续静稳时间、是否超时、总位移、平面漂移、法向沉降、最大旋转、结束线/角速度、Awake 数量和 Accepted；
- M7 入口输出 `M73A=1`。

## 13. 排错

| 症状 | 根因 | 处理 |
| --- | --- | --- |
| 编辑器看不到建筑 | `bShowEditorPreview` 关闭或静态校验拒绝 | 查看 GenerationSummary.RejectReason |
| Gatehouse/TwinTowerBridge 在 `Levels >= 5` 时全部消失，`RejectReason=BrickBudgetExceeded:51:50` 或 `53:50` | 不是渲染错误：每座塔每层为 `4 柱 + 1 楼板=5` 块，双塔每层共 10 块；Gatehouse 再加 1 根顶部门梁，因此 5 层为 51 块；新版 TwinTowerBridge 再加桥面和两道护栏，因此 5 层为 53 块。默认 `MaxBrickCount=50` 会执行硬拒绝，预览先被清空，因此看起来像全部消失。 | 测试高层建筑时提高 `GenerationSettings.MaxBrickCount`，或把 Levels 降到 4；正式作品应优先合并构件/分级激活，不要无预算地提高全局刚体上限。 |
| 沿 Z 拖动后建筑被拉回地面，基座不随 Actor 移动 | 旧 Ground Adapter 无条件把 Anchor 投影到 TestStage，且 Foundation 组件错误标为 Static | 默认关闭 `bSnapPlanarAnchorToTestStage`；FoundationCap/Feet 使用 Movable Mobility，但保持非模拟碰撞 |
| 退出 PIE 报 `FoundationCap 的移动性必须为可移动` | Static 组件在 OnConstruction/运行时被写入 WorldTransform | 使用当前 C++ 的 Movable Foundation 组件；重新编译并重开 Editor，避免仍加载旧 DLL |
| PIE 中出现两套砖 | Preview 未清空或 Runtime 初始化重复 | 检查 `[Generated]` 是否重复；`bRuntimeSpawned` 应阻止二次生成 |
| 平面也出现不同长度地基脚 | Stage Up/Origin、Actor Scale 或 Ground Context 错误 | Scale 设为 1；确认生成器与正确 TestStage 同场景 |
| `PlanarStageMissing` | M7.1 场景没有 TestStage | 放置 `M7.1 Physics Test Stage` 或改 Auto/球面模式 |
| `FootprintCellNotBuildable` | 球面 Footprint 覆盖水域或不可建 Cell | 换 Anchor、缩小建筑或减少层数/占地 |
| `AngularSpanTooLarge` | 建筑相对星球半径过宽 | 缩小占地；多平台拆分留待后续 |
| `BrickPenetration` | 参数使柱、梁或连接梁占据同一体积 | 恢复默认尺寸；查看节点编号 |
| `IdlePenetrationInvalid:Repairs>0` | 运行时真实 Collision 与生成尺寸存在小穿透；虽然共享 Validator 能微调，但继续模拟会让支撑图和 B2 COM/Contact Hull 失真 | 不放宽为“修复后继续”；校准 Mesh Pivot、Simple Collision、Node 尺寸或间隙，直到预检原始结果为零修复 |
| Idle `Accepted=0` 后整栋建筑和 Foundation 消失 | 这是 `RejectRuntimeStructure` 的预期失败清理，不是渲染丢失：失败结构已销毁模块、清空映射并关闭 Foundation 碰撞，禁止半成品继续参与 Gameplay | 先按 `RejectReason` 修复几何或静稳问题，再走正式初始化/重试；`UpdateFoundationComponents` 会恢复 Foundation 可见性、实例与碰撞，然后重新生成模块 |
| `NoGroundPath` | 某层没有实际接触支撑 | 检查层高、梁高和柱高关系 |
| 空载验证整体散开 | 碰撞面、质量差或生成接触仍不稳定 | 检查 Simple Collision、Sub-Stepping 和 IdleValidation 日志 |
| `TimedOut=1`，但位移/旋转尚未越界 | 至少一个刚体始终未连续 `0.45s` 低于线/角速度阈值，不能仅凭某一帧位移很小判为稳定 | 查看 `MaxLinearSpeed`、`MaxAngularSpeed` 和最大移动模块；修复持续滑动/震荡，或在有证据时单独校准速度门槛，不要只延长最大时长 |
| 发射后砖不动 | 未通过共享 MaterialSystem 生成或未进入 M6 发射状态 | 检查 `[Generated]` 与 M6 `BeginLaunchPhysics` 日志 |
| Foundation 一起飞走 | Foundation 被错误加入 M7 Modules | Foundation 只能由生成器静态组件持有 |

## 14. 性能预算

- 单栋目标约 15–45 个砖 Actor，取决于轮廓和层数；
- 默认仅当前测试建筑运行一次隐藏空载验证；最早约 `1.70s` 收敛，持续运动时最多 `6s`；
- Editor Preview 使用 4 个 HISM + Foundation HISM，不为每块预览生成 Actor；
- 正式地图仍遵守同时活跃刚体不超过约 50 的主设计预算；
- 批量 PCG 时不能让所有候选都进入 Chaos，本阶段 Actor 验证用于最终候选；
- 后续搜索式生成应先用当前 Static Validator 淘汰绝大多数候选。

## 15. 下一阶段接口

> M7.3-B 已基于以下接口实现，详见 [M73BWeakPointAndDifficultyDesign.md](M73BWeakPointAndDifficultyDesign.md)。
> B2 在同一接口上增加 authored weakness geometry、Carrier 载荷闭包和失效探针，详见 [M73B2StructuralWeaknessAndFailureValidationDesign.md](M73B2StructuralWeaknessAndFailureValidationDesign.md)。

M7.3-B 可直接消费：

- `Bricks`；
- `SupportEdges`；
- `GroundNodeIds`；
- Material 和尺寸；
- Ground Context 与攻击方向；
- 空载验证结果。

在此基础上增加割点/支配节点、弱点评分、弱点与非弱点攻击探针，无需重写承载面适配或建筑运行时装配。
