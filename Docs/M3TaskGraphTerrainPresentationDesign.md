# M3：TaskGraph 地形表现与 HISM 摆放设计

> 状态：C++ 与生产资产已实现，并已接入 M3 首周长路线/道路外建筑结果、M3R-1 只读月度 Schema、M3R-2 路线候选池、M3R-3 六 Encounter 空间候选、M3R-5 候选绑定表现层及 R-5.1 卫星/E5 预览叠层。R-5/R-5.1 当前为 M3LocalAccepted（IntegrationPending），只在显式预览中消费候选，`MonthlyAccepted` 仍为 false。M3 当前只产出地形、TaskGraph、RoadPortal、建筑 Anchor/施工台；球面普通建筑由下游 M7 DAG2.3 消费。第 4 节保留为历史独立 M3 验证场景搭建说明，不是现行生产地图入口。
>
> 逻辑 PCG 上游：[`ABTSTaskGraphPCGDesign.md`](ABTSTaskGraphPCGDesign.md)。本文不定义玩法锁、可达性、河流最低点、道路寻路或桥梁状态；它们只由 TaskGraph/CellTopo 生成并通过接口提供给表现层。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M3R 首周/月度地图改进](M3PCGMapImprovementPlan.md) · [M2 球面基础](M2PlanetSurfaceDesign.md) · [M7 TaskGraph DAG2.3 建筑集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) · [M5.2 碰撞与 CPU SDF 物理采样](M52CollisionAndMovementDesign.md) · [M11.0 终局前置收口](M110PreFinaleClosureDesign.md) · [开发排错](DevelopmentTroubleshooting.md)

## 1. M3 目标与边界

M3 将 TaskGraph 的区域结果映射为连续球面地形：Plain、Forest、Highland、Mountain 与 TaskGraph 指定的 Water 区域形成低频径向高度；不叠加 fBm、侵蚀噪声、体素噪声或独立高度图。连续表面使用 SDF 纯色材质平滑交界；树与岩石使用不模拟刚体、但参与静态查询/物理阻挡的 HISM；建筑只输出施工台位置，不生成模块化建筑。

```text
TaskGraph / CellTopo（逻辑唯一来源）
    -> FABTSM3CellState：TaskId、TerrainType、Road/Water、FlowS、BuildingAnchor/NoRoad
    -> FABTSM3TerrainVisualField：低频高度、法线、边界线段 SDF
    -> ProceduralMesh：径向顶点推拉、四通道 UV 材质上下文
    -> M3 SDF Material：纯色与平滑边界
    -> HISM：树、岩石；表现与静态阻挡，不是 Gameplay 身份源
    -> BuildingSpawnSites：供 M7 TaskGraph DAG2.3 建筑生成消费
    -> FABTSM110FinaleLocalFrame：供终局局部预设消费
```

`CellTopo` 和 `FABTSM3CellState` 才是道路、水体、建筑位和地形类别的逻辑来源。材质像素、连续网格三角形、`SurfaceQuery`、HISM InstanceId 与碰撞结果都不得反写 Gameplay。

水体尤其如此：M3 的 `bWater` 来自 TaskGraph 对 BridgeGate/任务走廊的逻辑安排，并以蓝色低频区域表现；它不根据连续地形的最低点、网格顶点高度或材质颜色反推河网。

## 2. C++ 模块与接口

| 模块 | 职责 | 禁止承担的职责 |
| --- | --- | --- |
| `FABTSM3TaskGraphGenerator` | 首周可调主线节奏、Task 区域分配、道路/纵向进度、水体/施工台逻辑标签 | 网格顶点、材质、HISM 归属判定 |
| `FABTSM3TerrainVisualField` | 从逻辑标签产生低频半径、连续法线、边界线段距离 | 可达性、资源、河流最低点搜索 |
| `AABTSM3Planet` | 组装逻辑结果、重建 PMC、输出 `QuerySurface`、填充 HISM/施工位 | 建筑模块和物理破坏 |
| `UABTSM3TerrainMaterialBridge` | 创建并注入临时 LUT/MID | 存储 Gameplay 状态 |
| `ForestHISM` / `RockHISM` | 稳定装饰实例与静态阻挡；`QueryAndPhysics`、`ABTSDeveloperObstacleChannel`、不模拟 | Gameplay 身份、地面高度、任务/道路/PVS/Witness 决策 |

### 2.1 对 TaskGraph 的输入约定

M3 只读取以下逻辑结果：

| 字段 | M3 用途 |
| --- | --- |
| `TaskId` / `TerrainType` | 低频高度类别、纯色、HISM 密度 |
| `bRoad` / `RoadDistance` / `MainRoadDistance` | 清空路旁装饰、预留道路视觉通道并记录施工台到主路的拓扑距离 |
| `ProgressDistanceCM` / `FlowS` | 只读主线纵向节奏与调试信息；不从表现反推 |
| `bWater` | TaskGraph 指定的水体颜色与轻微视觉下凹 |
| `bBuildingAnchor` / `bBuildingRoadExclusion` | 生成 `FABTSM3BuildingSpawnSite`；NoRoad 只参与逻辑验收，不生成独立网格；最终还按实际平台/道路宽度认证连续几何净空 |
| `UnitCenter` / `NeighborCellIds` | 构造地形区域边界线段和稳定散布方向 |
| `LaunchSite` / `SatelliteWindow` | M11.0 构造唯一终局槽对的表面位置、稳定切线轴和局部坐标系；不在表现层重选 Task |

M3 不需要也不会读取“最低连续顶点”“材质水色”“HISM 命中”来决定水体或道路。

### 2.2 M3R-1 Schema 观测层

`AABTSM3Planet` 在接受旧 Gen3/Policy1 结果后，额外暴露只读 `MonthlyWorldSchema` 和 Editor-only `MonthlySchemaDebugData`，供检查 Route/Encounter/Biome/Quality 的身份、引用和覆盖统计。R-1 表现层不消费这些数据来生成或修改 PMC、SDF、HISM、道路、河流或 `BuildingSpawnSites`；关闭 `bBuildObservation` 只会得到空观测 Schema，不会改变旧 TaskGraph 和共享导出。实现边界、版本身份及验收见 [M3R-1 月度 Schema 与观测面](M3PCGMapImprovementPlan.md#144-m3r-1建立月度-schema版本身份与观测面)。

### 2.3 M3R-2 路线候选观测层

`AABTSM3Planet` 在 R-1 Schema 成功后，以相同 `WorldSeed` 和只读 `LogicalCells` 构建独立 `MonthlyRoutePool`。当前传入中性 `MonthlyRoadContext`，因此它只证明候选骨架、Corridor、状态化 Road Solver 和路线几何门；R-3 才会注入正式 Terrain/Water/NoRoad/Encounter 状态并重算实际道路。

R-2 Pool 不替换首周 `GeneratedCellStates`、`GeneratedEdgeStates`、`PCGSummary` 或地表材质中的道路。`bMonthlyWorldAccepted` 在本阶段恒为 false，`MonthlyRouteFallback` 也只是继续交给 R-3/R-4 的中间骨架。Editor-only `bDrawMonthlyRouteDebugOverlay` 默认关闭；开启后以青线显示最佳路线、黄点显示控制点，30 秒后自动消失，不创建正式组件或资产。完整算法、冻结 Hash 与验收见 [M3R-2 多候选球面路线](M3PCGMapImprovementPlan.md#145-m3r-2实现多候选球面路线与道路求解)。

### 2.4 M3R-5 候选绑定表现层

`AABTSM3Planet` 在 R-3 完整验证通过后，为每个保留候选生成独立 `MonthlyPresentation` 快照。该层完整保存 R-3 Source Route/Spatial 身份、每个 Cell 的全部 Envelope 成员关系、ActiveRole 与冻结 `BiomeDistrictId`；它不选最终候选，也不改写 R-3 数据。`DisplayBiomeArchetype` 是单独的显示字段：一 Cell 逻辑 District 孤岛可以确定性并入邻区显示主题，但源 DistrictId、统计与 Hash 不变。

生产默认不消费任一月度表现候选。只有显式预览开关或命令行 `-ABTSM3R5PreviewCandidate=<SourceCandidateId>` 才把对应快照复制给材质桥和 HISM；固定展示运行时因此明确报告 `PreviewAuthority=1`、`MonthlyAccepted=0`。关闭预览后继续显示兼容世界，权威 `GeneratedCellStates/GeneratedEdgeStates/TerrainVisualField` 始终不变，所以 `QuerySurface`、旧 TaskGraph、PVS、Witness 与稳定合同不受表现开关影响。

树石散布既在源 Cell 上检查保护标记，也在偏移后的最终落点重新解析 Cell 并复查。道路、ActiveRole、Target Footprint、NoRoad、攻击走廊和水体禁止装饰侵入。完整实现状态、100 Seed 证据与待集成门禁见 [M3R-5](M3PCGMapImprovementPlan.md#148-m3r-5实现-biomedistrict-与-playable-envelope-表现)。

显式预览时，材质桥按 `VisualBeatId/AccentVariantId/ThemeVariantId` 对 Cell LUT 做低幅明暗调制；HISM 用相同字段调整树石疏密、随机序列和尺度。Runtime 必须证明全部材质 Cell、全部实际树石实例均经过 Beat 消费路径，因而 20–45 m 节拍不是只存在于数据快照。Editor Debug Overlay 可分别显示 Biome 点、Envelope 边界、Visual Beat 边界以及 ActiveRole/DeepWild 覆盖。PIE 中按 `F7` 可独立切换逻辑区域快捷叠层：Target Footprint 使用红色球点，Attack Corridor 使用橙色点线；叠层只读取显式选择的预览 Candidate，不存在精确 Candidate 时 fail closed 并显示启动参数提示。命令行 `-ABTSM3R5LogicRegions` 可让该叠层随 PIE 启动。

### 2.5 M3R-5.1 卫星与 E5 预览叠层

R-5.1 为每个精确 R-3/R-3.1 候选保存冻结参数生成的卫星中心、半径、E5 背面目标 Transform 和一个只用于建立局部坐标的参考桩对。所有数据都通过实际 `QuerySurface` 投影到当前地形；没有绝对世界坐标预设，也不把原主星 E5 Target Anchor 当作最终建筑位置。

F7 叠层增加蓝色卫星线框球、洋红 E5 目标盒、黄色参考桩对、绿色鸟袋点和青色发射点—卫星关系线。为避免误读，F7 隐藏原主星 E5 的红色 Target Footprint；其余五关目标与全部攻击走廊不变。叠层只画瞬时 Debug Primitive，不创建卫星/建筑 Actor、不添加碰撞、不进入材质/HISM 或布局 Hash。没有精确 Candidate 或冻结校准 Hash 不匹配时 fail closed。完整数据身份与集成交接见 [M3R-5.1 设计](M3R51SatellitePreviewDesign.md)。

### 2.6 `QuerySurface` 接口

`AABTSM3Planet::QuerySurface(UnitDirection)` 输出世界位置、高度感知法线、表面半径及最近 `CellId`。M2.5 径向移动已改为通过 `GetSurfaceRadiusAtDirection` 接地，因此角色仍沿球心径向保持重力方向，同时脚底遵循 M3 低频表面。

相机轨道与极区姿态继续使用标准球面的径向 Up，不使用坡面法线重写相机控制。

M11.0 起，`AABTSM3Planet::GetFinaleLaunchFrame()` 输出 `FABTSM110FinaleLocalFrame`。其中世界位置由 `QuerySurface` 提供，Task/Cell/槽对身份仍来自 PCG；表现层不能依据网格顶点或屏幕方向重新选择槽轴。完整合同见 [M11.0 第 6 节](M110PreFinaleClosureDesign.md#6-终局局部坐标系)。

## 3. 低频高度与边界距离 SDF

每个 Cell 的低频高度来自 TaskGraph 的地形解释；Plain 接近零、Forest 为低丘、Highland/Mountain 更高、TaskGraph 指定 Water 轻微下凹。没有任何 fBm 或随机高频位移。`HeightBlendWidthCM` 只控制这些几何高度在地形边界的平滑半径；`TerrainBlendWidthCM`（材质参数名 `M3_BlendWidthCM`）只控制颜色 SDF 的平滑半径。两者故意独立，调整道路/地形颜色过渡不会改变角色接地高度或坡度。

M11.0/V3 起，逻辑高度场对每个建筑/施工任务主动压平 `BuildingPadClearanceRingCells + 1` 圈：配置的前 `N` 圈用于最终施工面认证，额外一圈只保护边界 Cell 的邻接坡度计算。水文、`bBuildable`、唯一 Anchor 和最终可达性仍由后续 Planner/Validator 决定，表现层不得因看见平面而自行放行。

对相邻且地形类型不同的 Cell 边 `(A,B)`，使用两侧共同邻居构成的两个 dual corner 得到边界端点 `E0`、`E1`。对表面像素方向 `P`，距离使用线段投影：

```text
t = clamp(dot(P - E0, E1 - E0) / |E1 - E0|², 0, 1)
Q = E0 + t * (E1 - E0)
d = |P - Q| * PlanetRadiusCM
```

`t` 的 clamp 是本阶段的关键：垂足位于端点外时，`Q` 自动成为端点，所以距离自动退化为点到点距离；垂足位于边段内部时，才是点到直线的垂距。颜色 SDF 使用 `smoothstep(0, M3_BlendWidthCM, d)`，高度场使用独立 `HeightBlendWidthCM`。

道路也是 CellTopo 的 `bRoad` 标签，不是额外绘制的样条或网格。道路状态不同的相邻 Cell 同样写入边界线段 LUT；材质把 `CellVisualLUT.a` 解释为道路 mask，并用 `M3_RoadColor` 做同一套线段 SDF 平滑混色。因此道路可以跨越 Plain/Forest/Highland，且道路颜色宽度只受 `M3_BlendWidthCM` 影响，不会改变低频地形高度。实现中道路专属边界标记为“仅颜色”，`GetSurfaceRadius` 会跳过它们；只有地形类别变化的边界才进入 `HeightBlendWidthCM`。

这与“只算像素到最近 Cell 中心距离”不同：同一地形的相邻 Cell 不再产生六边形/五边形棋盘接缝；真正参与 SDF 的是**不同地形区域围成的线段边界**。

## 4. 历史独立验证：创建 M3 地图与 Planet

以下步骤只用于从 M2.5 复现早期 M3 地形验证场景。当前生产入口沿用项目工作流指定的地图与 M7+ GameMode；不要用本节复制地图或切回 `ABTSM3GameMode` 来替换生产入口。

1. 关闭 PIE，编译 `AngryBirdsToSpaceEditor Win64 Development`，重新打开 Editor。
2. 复制 `/Game/Maps/L_ABTS_M2_5` 为 `/Game/Maps/L_ABTS_M3`。
3. 删除或替换原有 `BP_ABTSM2Planet`：创建 Blueprint `BP_ABTSM3Planet`，父类选 `ABTSM3Planet`，拖入场景并放在 `(0,0,0)`。
4. 保持 `LogicalSubdivision=5`、`SurfaceSubdivision=7`、`PlanetRadiusCM=10000`。首轮保持 `MacroHeightScaleCM=900`、`TaskWaterDepthCM=80`、`HeightBlendWidthCM=160`、`TerrainBlendWidthCM=240`、`RoadColor=(0.22,0.12,0.045,1)`、`WorldSeed=312503`。
5. 在 **World Settings > GameMode Override** 选择原生 `ABTSM3GameMode`。它继续使用 M2.5 径向角色与跳跃。
6. 先不要给 `TerrainMaterial` 赋值，运行一次 PIE 确认 Output Log 出现 `[ABTS][M3] Ready=1`。此时可先用 Vertex Color 调试材质查看 C++ 计算的纯色结果。
7. 在 `BP_ABTSM3Planet` 的 `ForestHISM` 与 `RockHISM` 上分别指定低面数树/岩石。不要把组件改回历史 `NoCollision`；重建代码会统一重申 **Collision Enabled = Query And Physics**、Object Type=`ABTSDeveloperObstacleChannel`、`SimulatePhysics=false`。也可通过 Actor 的 `Forest Instance Mesh`、`Rock Instance Mesh` 指定同一资产，Actor 字段优先于组件字段。两处均未配置时，代码自动以 Engine Basic Shape 的 Cone/Cube 作为可见验收占位，不会生成“有组件但无网格”的空 HISM。

建筑尚未生成。运行时 `GetBuildingSpawnSites()` 输出工作台、目标建筑、熔炉与发射场的预留 Transform；M7 只能从这些接口消费位置，并由自己的 DAG2.3 Profile Resolver 决定结构。M3 不调用或维护任何建筑生成器。

### 4.1 玩家初始道路出生点

`ABTSM3GameMode` 不把地图里的 `PlayerStart` 当作最终位置。游戏开始后，它等待 `AABTSM3Planet` 完成 TaskGraph/地表重建，再查询 `GetInitialRoadSpawnTransform`：

- 位置为 `Start` Task 的 `SeedCellId`；若未来模板不把 Seed 放在道路上，则退化为 Start 区域内第一个 `bRoad=true` 的 Cell。
- 高度为该方向的 M3 低频表面半径加角色 Capsule Half Height，返回值是角色中心而不是脚底点。
- 朝向为 Start 道路通往下一个主线 Task 的第一条相邻道路边切线；角色本地 `+Z` 仍使用球面径向 Up。
- 传送前清零 M2.5 径向速度、跳跃缓冲和接地缓存，防止 PlayerStart 阶段积累的速度被带到出生点。
- GameMode 最多每 `0.1s` 重试一次、共 `30` 次，以处理 Pawn 与 Planet 的 BeginPlay 顺序；成功日志为 `[ABTS][M3][Spawn] Player placed at Start road`。

地图仍需保留一个合法 `PlayerStart`，用于 UE 创建并 Possess Pawn；它只承担临时生成入口，不决定 M3 最终出生位置。

## 5. 编辑器：M3 SDF 纯色材质

### 5.1 先做零风险 Vertex Color 对照

1. 新建 Material：`M_ABTS_M3_SDFTerrain`。
2. **Blend Mode** 设 `Opaque`，**Shading Model** 设 `Default Lit`，**Two Sided** 关闭。
3. 放置 `Vertex Color` 节点，`RGB -> Base Color`；添加常量 `0.82 -> Roughness`，常量 `0 -> Metallic`。
4. 把该材质赋给 `BP_ABTSM3Planet > Terrain Material`，PIE。应看到无纹理的 Plain/Forest/Highland/Mountain/Water 纯色大区，并且边缘已经是 C++ SDF 的平滑过渡。

此步用于排除材质参数名、MID 注入、绕序、法线与光照问题；通过后再进入 Custom 节点。

### 5.2 创建参数节点

在同一材质创建以下节点，**参数名必须完全一致**：

| 节点 | 参数名 | 类型 |
| --- | --- | --- |
| Texture Object Parameter | `M3_CellDirectionLUT` | Texture2D，默认可临时用任意线性颜色纹理 |
| Texture Object Parameter | `M3_CellVisualLUT` | Texture2D，默认可临时用任意线性颜色纹理 |
| Texture Object Parameter | `M3_BoundarySegmentLUT` | Texture2D，默认可临时用任意线性颜色纹理 |
| Scalar Parameter | `M3_CellCount` | 默认 `10242` |
| Scalar Parameter | `M3_BoundarySlots` | 默认 `6` |
| Scalar Parameter | `M3_PlanetRadiusCM` | 默认 `10000` |
| Scalar Parameter | `M3_BlendWidthCM` | 默认 `240` |
| Vector Parameter | `M3_PlanetCenter` | 默认 `(0,0,0,1)` |
| Vector Parameter | `M3_RoadColor` | 默认 `(0.22,0.12,0.045,1)` |
| Texture Object Parameter | `M3_RoadSegmentLUT` | 道路局部中心线槽位，Linear Color |
| Scalar Parameter | `M3_RoadSegmentCount` | 默认 `16`，运行时由 C++ 覆盖 |

三个 Texture Object Parameter 的 **Sampler Type** 均选 `Linear Color`。节点默认纹理不能留空；运行时会由 `UABTSM3TerrainMaterialBridge` 覆盖为 `PF_A32B32G32R32F` LUT。

再放置：

- 三个 `Texture Coordinate`，Coordinate Index 分别为 `0`、`1`、`2`；它们分别是每个渲染三角形固定的候选 Cell A/B/C ID。
- 一个 `Absolute World Position`。
- 一个 `Custom` 节点，**Output Type = CMOT Float3**，Description 填 `M3 segment SDF terrain color`。

### 5.3 Custom 节点输入

按下表顺序创建输入并连接：

| Input 名称 | 连接 |
| --- | --- |
| `CandidateA` | TexCoord0 |
| `CandidateB` | TexCoord1 |
| `CandidateC` | TexCoord2 |
| `WorldPos` | Absolute World Position |
| `PlanetCenter` | `M3_PlanetCenter` |
| `CellDirectionLUT` | 同名 Texture Object Parameter |
| `CellVisualLUT` | 同名 Texture Object Parameter |
| `BoundarySegmentLUT` | 同名 Texture Object Parameter |
| `CellCount` | `M3_CellCount` |
| `PlanetRadiusCM` | `M3_PlanetRadiusCM` |
| `BlendWidthCM` | `M3_BlendWidthCM` |
| `RoadColor` | `M3_RoadColor` |
| `RoadSegmentLUT` | `M3_RoadSegmentLUT` |
| `RoadSegmentCount` | `M3_RoadSegmentCount` |

粘贴以下**可直接使用的完整 Code**。它不定义顶层函数；`saturate(t)` 即是线段垂足越界时退化为端点距离的实现。

```hlsl
int c0 = (int)round(CandidateA.x) * 256 + (int)round(CandidateA.y);
int c1 = (int)round(CandidateB.x) * 256 + (int)round(CandidateB.y);
int c2 = (int)round(CandidateC.x) * 256 + (int)round(CandidateC.y);

float3 p = normalize(WorldPos - PlanetCenter);
float2 uv0 = float2((c0 + 0.5) / CellCount, 0.5);
float2 uv1 = float2((c1 + 0.5) / CellCount, 0.5);
float2 uv2 = float2((c2 + 0.5) / CellCount, 0.5);
float4 d0 = CellDirectionLUT.SampleLevel(CellDirectionLUTSampler, uv0, 0);
float4 d1 = CellDirectionLUT.SampleLevel(CellDirectionLUTSampler, uv1, 0);
float4 d2 = CellDirectionLUT.SampleLevel(CellDirectionLUTSampler, uv2, 0);
float dot0 = dot(p, d0.xyz);
float dot1 = dot(p, d1.xyz);
float dot2 = dot(p, d2.xyz);
int chosen = dot0 >= dot1 && dot0 >= dot2 ? c0 : (dot1 >= dot2 ? c1 : c2);

float2 chosenUV = float2((chosen + 0.5) / CellCount, 0.5);
float4 chosenVisual = CellVisualLUT.SampleLevel(CellVisualLUTSampler, chosenUV, 0);
float3 baseTerrainColor = chosenVisual.rgb;
float roadMask = chosenVisual.a;
float nearestDistance = 1e20;
int otherCell = -1;

[unroll]
for (int slot = 0; slot < 6; ++slot)
{
    float2 edgeUV0 = float2((slot * 2 + 0.5) / 12.0, (chosen + 0.5) / CellCount);
    float2 edgeUV1 = float2((slot * 2 + 1.5) / 12.0, (chosen + 0.5) / CellCount);
    float4 edge0 = BoundarySegmentLUT.SampleLevel(BoundarySegmentLUTSampler, edgeUV0, 0);
    float4 edge1 = BoundarySegmentLUT.SampleLevel(BoundarySegmentLUTSampler, edgeUV1, 0);
    if (edge0.w > 0.5 && edge1.w >= 0.0)
    {
        float3 segmentVector = edge1.xyz - edge0.xyz;
        float segmentLengthSq = max(dot(segmentVector, segmentVector), 1e-8);
        float t = saturate(dot(p - edge0.xyz, segmentVector) / segmentLengthSq);
        float3 closest = edge0.xyz + t * segmentVector;
        float distanceCM = length(p - closest) * PlanetRadiusCM;
        if (distanceCM < nearestDistance)
        {
            nearestDistance = distanceCM;
            otherCell = (int)round(edge1.w);
        }
    }
}

if (otherCell >= 0 && nearestDistance < BlendWidthCM)
{
    float2 otherUV = float2((otherCell + 0.5) / CellCount, 0.5);
    float4 otherVisual = CellVisualLUT.SampleLevel(CellVisualLUTSampler, otherUV, 0);
    float insideWeight = smoothstep(0.0, BlendWidthCM, nearestDistance);
    baseTerrainColor = lerp(0.5 * (baseTerrainColor + otherVisual.rgb), baseTerrainColor, insideWeight);
    roadMask = lerp(0.5 * (roadMask + otherVisual.a), roadMask, insideWeight);
}

return lerp(baseTerrainColor, RoadColor.rgb, saturate(roadMask));
```

最后将 Custom 输出接 `Base Color`，保留 `Roughness=0.82`、`Metallic=0`，不要连接像素 Normal；M3 已在 C++ 端写入高度感知顶点法线。保存并回到 `BP_ABTSM3Planet`，重新 PIE。

## 6. HISM 规则

`ForestHISM` 仅在 Forest Cell 内放树，`RockHISM` 仅在 Mountain Cell 内放岩石。每个实例由 `Hash(WorldSeed, CellId, Slot)` 得到稳定方向、旋转和缩放；其位置通过同一 `TerrainVisualField` 查询表面半径与法线。道路、水体、施工台 Cell 不放装饰；R-5 显式预览还保护 ActiveRole、Target Footprint、NoRoad 和攻击走廊，并在散点偏移后的最终 Cell 再验证一次，防止实例越界侵入保护区。道路本身由 SDF 纯色显示，不新增道路 HISM。

每次重建都对两类 HISM 重新应用生产碰撞合同：`QueryAndPhysics + ABTSDeveloperObstacleChannel + SimulatePhysics=false`。Character Sweep、Visibility Query 和 M6 动态代理应能撞到静态实例，M9 开发者穿行按专用 Object Channel 识别；实例本身不成为 Chaos 刚体，也不反向参与 Mission、道路、PVS、Witness、地面高度或布局 Hash。这里的“静态装饰”指不模拟、不自主移动，不等于把 Object Type 改成 `WorldStatic`。

网格解析顺序为：Actor 的 `Forest/Rock Instance Mesh` → 对应 HISM 组件的 `Static Mesh` → Engine Cone/Cube 验收占位。重建不得用空 Actor 字段覆盖组件已经配置的网格。日志 `[ABTS][M3][HISM]` 会同时报告最终网格、符合摆放条件的 Cell 数和实际实例数；若 Cell 数大于 0 而实例数为 0，优先检查网格解析与 `InstancesPerCell`。

HISM 使用的每个材质还必须在材质 Details 的 **Usage** 中启用 **Used with Instanced Static Meshes**，否则普通 StaticMesh 预览正常，HISM 却会使用默认材质或缺失对应渲染结果。当前 `M_PineTree` 与 `M_Stone` 已启用该 Usage；代码会把验证结果输出为 `ForestMaterialsValid` / `RockMaterialsValid`。

树木的局部 `+Z` 不能完全跟随连续地表法线，否则在宏观高度交界或陡坡上会出现大幅侧倒。树木使用 `normalize(lerp(RadialUp, SurfaceNormal, ForestSurfaceNormalBlend))`，默认权重 `0.2`，即以球心径向为主、只吸收 20% 的坡面倾斜；岩石继续完整贴合地表法线。Pivot 必须位于树干底部中心且模型局部 `+Z` 指向树梢。日志 `[ABTS][M3][HISM]` 同时输出 `MaxSurfaceTilt` 与实际应用后的 `MaxAppliedTilt`，后者应显著更小。

地表几何高度不能直接使用最近 Cell 的常量高度，否则 Cell 边界会形成六边形台阶，极小距离的法线差分会把台阶解释成近乎竖直的坡面。`FABTSM3TerrainVisualField` 在包含查询方向的 CellTopo 三角形内对三个逻辑高度做重心插值，使半径场跨三角形连续；数值边界采用一环逆距离插值兜底。顶点法线用 `SurfaceNormalSmoothingDistanceCM` 指定的世界空间中心差分半径，默认 `160cm`，并平均正交与对角两组梯度，消除方向偏置和六边形阴影斑纹。日志 `[ABTS][M3][SurfaceNormals]` 中 `ExtremeOver80` 应为 0 或接近 0。

M3 不生成建筑 Actor，也不把 HISM 当施工台。`BuildingSpawnSites` 是唯一建筑预留接口，包含 `CellId`、TaskType、WorldTransform 与坡度；M7 TaskGraph/DAG2.3 才在这些位置生成模块与刚体。普通建筑的 `WorldTransform.+X` 由 RoadPortal 指向 BuildingAnchor，使道路外移位后仍把 M7 的攻击正面朝向玩家抵达方向；LaunchSite 继续使用 SatelliteWindow 定义终局局部轴。

## 7. 验收

1. 固定 `WorldSeed` 重复运行，`[ABTS][M3]` 的 Task、道路、水体、施工位与 HISM 数量一致。
2. 改变 `WorldSeed` 后，Task 方向与区域形状改变，但所有逻辑标签仍可追溯到 CellId。
3. 山地/高地连续隆起，Water 仅在 TaskGraph 指定区域下凹；完全没有 fBm 颗粒、高频波纹或由最低点自动生成的河网。
4. 纯色材质在同类地形 Cell 之间没有六边形棋盘缝；不同地形交界沿区域线段平滑混色。把视角移到边界端点外，确认混色按端点距离圆滑收束。
5. 主线 `bRoad` Cell 显示 `M3_RoadColor`；道路与非道路交界沿同一线段 SDF 平滑过渡。调大 `TerrainBlendWidthCM` 时只改变颜色带宽，调大 `HeightBlendWidthCM` 时只改变坡面/接地过渡。
6. 放置带 `BlockAll` 的测试物体，M2.5 角色仍能跳跃并沿 M3 表面径向接地；角色 Down 始终朝球心。
7. HISM 开关不改变 `QuerySurface`、CellId、道路、水体或建筑施工位；关闭 HISM 后连续地表仍完整。
8. `GetBuildingSpawnSites()` 返回施工位，但场景中没有模块化建筑或建筑刚体。
9. 每次进入 PIE，玩家最终位于 Start Task 的道路 Cell，朝向下一个主线 Task；移动前 Output Log 出现一次 `[ABTS][M3][Spawn] Player placed at Start road`。改变 `WorldSeed` 后出生点随 TaskGraph 改变，而不是停留在地图 `PlayerStart`。
10. M11.0 后 `LaunchSite` 仍可返回平整施工位，但 M7 不在其上生成建筑；`GetFinaleLaunchFrame()` 必须返回正交、右手、槽中点为原点的唯一终局局部坐标系。
11. 首周展示 Seed 的普通三栋施工 Anchor 均不在道路上，`bBuildingRoadExclusion && bRoad` 的 Cell 数为 0，且日志均为 `GeometricRoadClearance=1`；该认证以实际平台、混合带、道路半宽和动态几何预筛为准。LaunchSite 仍位于道路端点，其高度压平护环必须完整归属于 LaunchSite Task。
12. `[ABTS][PCG][Accepted]` 的 `Visibility` 为 `11/00/00`，表示 B1 在默认/最大 OrbitDistance 均可见，B2/B3 均隐藏；该值来自逻辑高度包络，不依赖 HISM 遮挡。
13. R-5 固定展示运行时必须报告 `PreviewAuthority=1/MonthlyAccepted=0`；切换或关闭预览后 R-3 Route/Spatial 身份、`QuerySurface`、PVS、Witness 和兼容世界 Hash 均不改变。
14. R-5 100 Seed 必须 `100/100` 接受全部 300 个候选表现计划；当前冻结证据为 ActiveRole 覆盖下限 `786‰`、DeepWild 上限 `0‰`、主题下限 `4`、逻辑 singleton `2` 与后续小碎片 `2 Cell` 仅视觉合并、最终显示 singleton `0`、最小显示连通块 `3 Cell`、全局显示邻接边界率上限 `21‰`，Oracle=`6751B93DA5E4C778`、Manifest=`9E5A2FE0E563A7C4`。
15. 树石实例不得落入道路、ActiveRole、Target Footprint、NoRoad、攻击走廊或水体；组件必须保持 `QueryAndPhysics + ABTSDeveloperObstacleChannel + SimulatePhysics=false`。真实 M6/M9/Character/Visibility 联合碰撞仍在 IntegrationPending 清单中。
16. 使用精确预览 Candidate 进入 PIE 后，`F7` 必须能在不重建地图的情况下显示/隐藏逻辑区域；红色 Target Footprint 与橙色 Attack Corridor 的 Cell 集合必须逐项等于 R-3 源标记，关闭后叠层在一个刷新周期内消失。

## 8. 排错

| 现象 | 原因与处理 |
| --- | --- |
| 材质仍是默认灰色 | 先做 5.1 的 Vertex Color 对照；确认 `TerrainMaterial` 指向该材质，且不是直接覆写 `ContinuousSurface` 未受代码管理的材质槽。 |
| Custom 节点报采样器/类型错误 | 三个 LUT 必须为 Texture Object Parameter、Sampler Type=`Linear Color`，节点默认纹理不可为空。 |
| 边缘仍是六边形格 | 检查 Custom 使用的是 `M3_BoundarySegmentLUT`，并且每条线段的投影 `t` 使用 `saturate`；不要改回只比较候选 Cell 中心距离。 |
| 山体光照像光滑球或有三角分块 | 不要在材质中用世界 Z 重写 Normal。确认 `GetSurfaceNormalAtDirection` 写入了 PMC 顶点法线；重新 `RebuildPlanet`。 |
| 树石悬空或穿入太深 | 确认 HISM 只从 `TerrainVisualField` 查询半径/法线，且保持默认 `-8cm` 埋入偏移；不要用 `CellCenter * PlanetRadius`。 |
| 水体随高度最低点迁移 | 这是错误实现。检查逻辑端只写 TaskGraph `bWater`，不要从网格、材质或 `QuerySurface` 回读水体。 |
| 道路没有颜色或只显示硬方块 | 确认 Custom 节点新增了 `RoadColor` 输入并连接 `M3_RoadColor`，且使用 `CellVisualLUT.a` 作为道路 mask。道路/非道路状态不同的 Cell 会自动写入边界线段；不要用 HISM 或样条代替该逻辑标签。 |
| 调整颜色带宽却让角色坡度改变 | `TerrainBlendWidthCM` 只应进入 `M3_BlendWidthCM`；检查 `HeightBlendWidthCM` 是否仍单独传给 `TerrainVisualField`。 |
| 玩家仍停在地图 PlayerStart | 确认地图使用 `ABTSM3GameMode`，场景内 Actor 是 `ABTSM3Planet` 而非 M2 Planet；查看 `[ABTS][M3][Spawn]`。连续 30 次失败日志会分别显示 Planet/Pawn 是否就绪。 |
| 玩家出生后下沉、弹飞或方向错误 | Spawn 查询必须传 Capsule Half Height，且传送前调用 `RadialMovement.ResetMotionState()`；旋转用 `MakeFromXZ(RoadForward, SpawnDirection)`，不能使用世界 Z。 |

## 9. M3 性能预算

- `SurfaceSubdivision=7` 固定为 `327,680` 个三角形。为了给每个三角形保存三候选 CellId，材质属性顶点展开为约 `983,040` 个；几何位置语义仍连续，但显存开销高于 M2 共享顶点版本。
- 三张浮点 LUT 在 `10,242` Cells 下约占：Direction `0.16MB`、Visual `0.16MB`、Boundary `1.97MB`，合计约 `2.3MB`（不含 RHI 对齐）。
- 当前 Custom 节点每像素最多采样 3 次方向 LUT、1 次自身颜色 LUT、12 次边界端点 LUT和 1 次邻区颜色 LUT。M3 以正确性为先；进入性能优化阶段后可按可见区域、边界 Cell 压缩或预计算最近边段减少采样。
- R-5 候选表现构造的 100 Seed 最终基线为 `P95=127.860 ms`、`Max=139.359 ms`，满足冻结的 `250/1000 ms` 门。连续表面改为按唯一 icosphere 顶点缓存高度、法线与颜色采样，再把结果复制到三角形局部 UV 顶点；几何、材质和碰撞结果不变。`SurfaceSubdivision=7` 的 MaterialReady + HISM 显式预览 fresh runtime 实测 `RebuildMS=6057.156`、`PeakPhysicalMB=2221.3`，正式通过 `<=8 s` 与 `2.25GB * 115%` 门。
- HISM 每种装饰默认不超过 `InstancesPerCell * 匹配地形 Cell 数`，并统一 `QueryAndPhysics + ABTSDeveloperObstacleChannel + SimulatePhysics=false`。首轮 GPU 验收目标为地表材质增量小于约 `2ms @ 1080p`；若超过预算，先降低边界采样次数或用较低 `SurfaceSubdivision` 做材质调试，不能删除 CellTopo 逻辑、禁用生产碰撞合同或让实例侵入保护走廊。
### 河流边线 SDF（修复六边形拼接）

水体的逻辑标记仍来自 `FABTSM3CellEdgeState::Water`，但表现层不得把 `bWater` 当作整块 Cell 的填充区域。`UABTSM3TerrainMaterialBridge` 为每条水边生成 `M3_RiverSegmentLUT`，每个局部槽的第一像素存起点方向与半宽，第二像素存终点方向与水体类型。

线段端点必须根据 Edge 的语义决定：具有 `DownstreamCellId` 且不承担阻断职责的自然水文边，中心线连接 `CellA.UnitCenter -> CellB.UnitCenter`，相邻下游边因此共享 Cell 中心并形成连续水道；`bBlocksOnFoot` 的 Gameplay 割集边则用 A/B 两侧共同邻居构造的两个 dual corner，因为这些公共边首尾相接后才形成连续的阻断水带。不可把自然水文边也转换成 dual edge——dual edge 与 A→B 流向近似垂直，会让连续河网变成一排横向短条纹。

材质 Custom 节点查询当前 Cell 的固定 24 个局部河段槽位，使用 `saturate(dot(P-A,B-A)/|B-A|²)` 将垂足限制在线段内，计算像素到线段的最近距离；距离小于半宽时混合 `M3_RiverColor`。C++ 按“河段半宽 + 颜色 BlendWidth + Cell 外接半径”计算每个 Cell 必须持有的河段，并按距离保留最近 24 条；运行日志中的 `DroppedLocalRefs` 必须为 0。这样不会在 LUT Cell 行切换处把宽河硬裁成一截一截，同时也避免每像素遍历全图河网。`M3_BoundarySegmentLUT` 仍只负责地形/道路边界，不能承担河流主体。

几何高度同样按到最近河流线段的距离局部下凹，`GetCellHeightCM()` 不再依据 `bWater` 对整个 Cell 降低半径，避免产生六边形凹陷。

### 道路中心线与地形轮廓 SDF

道路不得再使用 `FABTSM3CellState::bRoad` 填满整个 Cell。表现层从 `FABTSM3CellEdgeState::Transport` 读取道路边，使用 `CellA.UnitCenter -> CellB.UnitCenter` 作为连续中心线：`TrailVisualHalfWidthCM` 控制小径半宽，`MainRoadVisualHalfWidthCM` 控制主路半宽。道路写入独立的 `M3_RoadSegmentLUT`，材质与河流相同地计算像素到线段的最近距离，再混合 `M3_RoadColor`。`bRoad` 只保留为 Gameplay/快速查询缓存。

仅简化不同地形之间的 dual-edge 外轮廓仍然不够：像素的基础归属若继续取最近 Cell，六边形 Voronoi 分区仍会在轮廓之外残留。最终方案由 `FABTSM3TerrainFeatureVisualBuilder` 把每对“相邻且陆地类型相同”的 Cell 中心连接成线段；没有同类邻居的孤立 Cell 退化为点特征。材质在当前 Cell 的三环邻域中查询这些线段，分别求 Plain/Forest/Highland/Mountain 四类线网的最近距离，再按最近与次近距离差连续混色。这是 line-feature Voronoi，而不是 Cell-center Voronoi，因此地形归属边界不再继承六边形 Cell 外形。

每个 Cell 从三环候选中强制保留每一种出现地形的最近特征，再按距离补足至 32 槽；`PrunedTerrainRefs` 表示被安全裁掉的远端冗余线段，不是视觉数据丢失。CPU 高度场消费同一组最近/次近地形特征，所以颜色交界和坡面交界使用同一连续场。该场仅从 CellTopo 派生，不回写 TaskGraph、可建造性或道路可达性。
