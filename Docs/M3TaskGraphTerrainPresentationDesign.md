# M3：TaskGraph 地形表现与 HISM 摆放设计

> 状态：C++ 已实现；需要按本文在编辑器创建 M3 地图、M3 Planet Blueprint 和材质资产。
>
> 逻辑 PCG 上游：[`ABTSTaskGraphPCGDesign.md`](ABTSTaskGraphPCGDesign.md)。本文不定义玩法锁、可达性、河流最低点、道路寻路或桥梁状态；它们只由 TaskGraph/CellTopo 生成并通过接口提供给表现层。

## 1. M3 目标与边界

M3 将 TaskGraph 的区域结果映射为连续球面地形：Plain、Forest、Highland、Mountain 与 TaskGraph 指定的 Water 区域形成低频径向高度；不叠加 fBm、侵蚀噪声、体素噪声或独立高度图。连续表面使用 SDF 纯色材质平滑交界，树与岩石使用无碰撞 HISM，建筑只输出施工台位置，不生成模块化建筑。

```text
TaskGraph / CellTopo（逻辑唯一来源）
    -> FABTSM3CellState：TaskId、TerrainType、Road/Water、BuildingAnchor
    -> FABTSM3TerrainVisualField：低频高度、法线、边界线段 SDF
    -> ProceduralMesh：径向顶点推拉、四通道 UV 材质上下文
    -> M3 SDF Material：纯色与平滑边界
    -> HISM：树、岩石；仅视觉
    -> BuildingSpawnSites：供 M4 建筑生成消费
```

`CellTopo` 和 `FABTSM3CellState` 才是道路、水体、建筑位和地形类别的逻辑来源。材质像素、连续网格三角形、`SurfaceQuery`、HISM InstanceId 与碰撞结果都不得反写 Gameplay。

水体尤其如此：M3 的 `bWater` 来自 TaskGraph 对 BridgeGate/任务走廊的逻辑安排，并以蓝色低频区域表现；它不根据连续地形的最低点、网格顶点高度或材质颜色反推河网。

## 2. C++ 模块与接口

| 模块 | 职责 | 禁止承担的职责 |
| --- | --- | --- |
| `FABTSM3TaskGraphGenerator` | 固定主线路径、Task 区域分配、道路距离、水体/施工台逻辑标签 | 网格顶点、材质、HISM 归属判定 |
| `FABTSM3TerrainVisualField` | 从逻辑标签产生低频半径、连续法线、边界线段距离 | 可达性、资源、河流最低点搜索 |
| `AABTSM3Planet` | 组装逻辑结果、重建 PMC、输出 `QuerySurface`、填充 HISM/施工位 | 建筑模块和物理破坏 |
| `UABTSM3TerrainMaterialBridge` | 创建并注入临时 LUT/MID | 存储 Gameplay 状态 |
| `ForestHISM` / `RockHISM` | 稳定装饰实例 | 碰撞、点击、地面高度、建筑逻辑 |

### 2.1 对 TaskGraph 的输入约定

M3 只读取以下逻辑结果：

| 字段 | M3 用途 |
| --- | --- |
| `TaskId` / `TerrainType` | 低频高度类别、纯色、HISM 密度 |
| `bRoad` / `RoadDistance` | 清空路旁装饰并预留道路视觉通道 |
| `bWater` | TaskGraph 指定的水体颜色与轻微视觉下凹 |
| `bBuildingAnchor` | 生成 `FABTSM3BuildingSpawnSite`，不生成建筑 |
| `UnitCenter` / `NeighborCellIds` | 构造地形区域边界线段和稳定散布方向 |

M3 不需要也不会读取“最低连续顶点”“材质水色”“HISM 命中”来决定水体或道路。

### 2.2 `QuerySurface` 接口

`AABTSM3Planet::QuerySurface(UnitDirection)` 输出世界位置、高度感知法线、表面半径及最近 `CellId`。M2.5 径向移动已改为通过 `GetSurfaceRadiusAtDirection` 接地，因此角色仍沿球心径向保持重力方向，同时脚底遵循 M3 低频表面。

相机轨道与极区姿态继续使用标准球面的径向 Up，不使用坡面法线重写相机控制。

## 3. 低频高度与边界距离 SDF

每个 Cell 的低频高度来自 TaskGraph 的地形解释；Plain 接近零、Forest 为低丘、Highland/Mountain 更高、TaskGraph 指定 Water 轻微下凹。没有任何 fBm 或随机高频位移。

对相邻且地形类型不同的 Cell 边 `(A,B)`，使用两侧共同邻居构成的两个 dual corner 得到边界端点 `E0`、`E1`。对表面像素方向 `P`，距离使用线段投影：

```text
t = clamp(dot(P - E0, E1 - E0) / |E1 - E0|², 0, 1)
Q = E0 + t * (E1 - E0)
d = |P - Q| * PlanetRadiusCM
```

`t` 的 clamp 是本阶段的关键：垂足位于端点外时，`Q` 自动成为端点，所以距离自动退化为点到点距离；垂足位于边段内部时，才是点到直线的垂距。SDF 边缘使用 `smoothstep(0, BlendWidthCM, d)` 在两侧地形颜色和低频高度之间过渡。

这与“只算像素到最近 Cell 中心距离”不同：同一地形的相邻 Cell 不再产生六边形/五边形棋盘接缝；真正参与 SDF 的是**不同地形区域围成的线段边界**。

## 4. 编辑器：创建 M3 地图与 Planet

1. 关闭 PIE，编译 `AngryBirdsToSpaceEditor Win64 Development`，重新打开 Editor。
2. 复制 `/Game/Maps/L_ABTS_M2_5` 为 `/Game/Maps/L_ABTS_M3`。
3. 删除或替换原有 `BP_ABTSM2Planet`：创建 Blueprint `BP_ABTSM3Planet`，父类选 `ABTSM3Planet`，拖入场景并放在 `(0,0,0)`。
4. 保持 `LogicalSubdivision=5`、`SurfaceSubdivision=7`、`PlanetRadiusCM=10000`。首轮保持 `MacroHeightScaleCM=900`、`TaskWaterDepthCM=80`、`TerrainBlendWidthCM=240`、`WorldSeed=312503`。
5. 在 **World Settings > GameMode Override** 选择原生 `ABTSM3GameMode`。它继续使用 M2.5 径向角色与跳跃。
6. 先不要给 `TerrainMaterial` 赋值，运行一次 PIE 确认 Output Log 出现 `[ABTS][M3] Ready=1`。此时可先用 Vertex Color 调试材质查看 C++ 计算的纯色结果。
7. 在 `BP_ABTSM3Planet` 的 `ForestHISM` 与 `RockHISM` 上分别指定低面数树/岩石或临时 Engine Basic Shape；务必保持 **Collision Enabled = No Collision**。也可通过 `Forest Instance Mesh`、`Rock Instance Mesh` 指定同一资产。

建筑尚未生成。运行时 `GetBuildingSpawnSites()` 输出工作台、目标建筑、熔炉与发射场的预留 Transform，M4 模块化建筑只能从这些接口消费位置。

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
float3 baseColor = CellVisualLUT.SampleLevel(CellVisualLUTSampler, chosenUV, 0).rgb;
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
    float3 otherColor = CellVisualLUT.SampleLevel(CellVisualLUTSampler, otherUV, 0).rgb;
    float insideWeight = smoothstep(0.0, BlendWidthCM, nearestDistance);
    baseColor = lerp(0.5 * (baseColor + otherColor), baseColor, insideWeight);
}

return baseColor;
```

最后将 Custom 输出接 `Base Color`，保留 `Roughness=0.82`、`Metallic=0`，不要连接像素 Normal；M3 已在 C++ 端写入高度感知顶点法线。保存并回到 `BP_ABTSM3Planet`，重新 PIE。

## 6. HISM 规则

`ForestHISM` 仅在 Forest Cell 内放树，`RockHISM` 仅在 Mountain Cell 内放岩石。每个实例由 `Hash(WorldSeed, CellId, Slot)` 得到稳定方向、旋转和缩放；其位置通过同一 `TerrainVisualField` 查询表面半径与法线。道路、水体、施工台 Cell 不放装饰；所有 HISM 为 `NoCollision`。

M3 不生成建筑 Actor，也不把 HISM 当施工台。`BuildingSpawnSites` 是唯一建筑预留接口，包含 `CellId`、TaskType、WorldTransform 与坡度；M4 才在这些位置生成模块与刚体。

## 7. 验收

1. 固定 `WorldSeed` 重复运行，`[ABTS][M3]` 的 Task、道路、水体、施工位与 HISM 数量一致。
2. 改变 `WorldSeed` 后，Task 方向与区域形状改变，但所有逻辑标签仍可追溯到 CellId。
3. 山地/高地连续隆起，Water 仅在 TaskGraph 指定区域下凹；完全没有 fBm 颗粒、高频波纹或由最低点自动生成的河网。
4. 纯色材质在同类地形 Cell 之间没有六边形棋盘缝；不同地形交界沿区域线段平滑混色。把视角移到边界端点外，确认混色按端点距离圆滑收束。
5. 放置带 `BlockAll` 的测试物体，M2.5 角色仍能跳跃并沿 M3 表面径向接地；角色 Down 始终朝球心。
6. HISM 开关不改变 `QuerySurface`、CellId、道路、水体或建筑施工位；关闭 HISM 后连续地表仍完整。
7. `GetBuildingSpawnSites()` 返回施工位，但场景中没有模块化建筑或建筑刚体。

## 8. 排错

| 现象 | 原因与处理 |
| --- | --- |
| 材质仍是默认灰色 | 先做 5.1 的 Vertex Color 对照；确认 `TerrainMaterial` 指向该材质，且不是直接覆写 `ContinuousSurface` 未受代码管理的材质槽。 |
| Custom 节点报采样器/类型错误 | 三个 LUT 必须为 Texture Object Parameter、Sampler Type=`Linear Color`，节点默认纹理不可为空。 |
| 边缘仍是六边形格 | 检查 Custom 使用的是 `M3_BoundarySegmentLUT`，并且每条线段的投影 `t` 使用 `saturate`；不要改回只比较候选 Cell 中心距离。 |
| 山体光照像光滑球或有三角分块 | 不要在材质中用世界 Z 重写 Normal。确认 `GetSurfaceNormalAtDirection` 写入了 PMC 顶点法线；重新 `RebuildPlanet`。 |
| 树石悬空或穿入太深 | 确认 HISM 只从 `TerrainVisualField` 查询半径/法线，且保持默认 `-8cm` 埋入偏移；不要用 `CellCenter * PlanetRadius`。 |
| 水体随高度最低点迁移 | 这是错误实现。检查逻辑端只写 TaskGraph `bWater`，不要从网格、材质或 `QuerySurface` 回读水体。 |

## 9. M3 性能预算

- `SurfaceSubdivision=7` 固定为 `327,680` 个三角形。为了给每个三角形保存三候选 CellId，材质属性顶点展开为约 `983,040` 个；几何位置语义仍连续，但显存开销高于 M2 共享顶点版本。
- 三张浮点 LUT 在 `10,242` Cells 下约占：Direction `0.16MB`、Visual `0.16MB`、Boundary `1.97MB`，合计约 `2.3MB`（不含 RHI 对齐）。
- 当前 Custom 节点每像素最多采样 3 次方向 LUT、1 次自身颜色 LUT、12 次边界端点 LUT和 1 次邻区颜色 LUT。M3 以正确性为先；进入性能优化阶段后可按可见区域、边界 Cell 压缩或预计算最近边段减少采样。
- Fresh Commandlet 中，`Sub=7` 的逻辑生成、低频网格、碰撞与无材质资源重建约在 8 秒内完成，进程峰值物理内存约 `2.25GB`（含完整 Editor-Cmd 启动成本）。比赛版本不应在正常游玩中频繁全量重建。
- HISM 每种装饰默认不超过 `InstancesPerCell * 匹配地形 Cell 数`，并统一 `NoCollision`。首轮 GPU 验收目标为地表材质增量小于约 `2ms @ 1080p`；若超过预算，先降低边界采样次数或用较低 `SurfaceSubdivision` 做材质调试，不能删除 CellTopo 逻辑。
