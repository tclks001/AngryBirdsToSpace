# ABTS 三渲二 T3-A1：M3 地形与自然物材质族适配

> 状态：M3 代码、自有资产、Integration 只读消费、自动化与可见 PIE 均已完成；2026-08-06 建立，2026-08-07 接入并验收通过。
>
> Base：`803bb2512c0da68ac889ce25d98fa4f91cbe10b1`（T3-A0）。
>
> 上游：[三渲二总设计](ABTSToonStylizedRenderingDesign.md) · [T3-A0 共享材质契约](ABTSToonStylizedRenderingT3A0.md)

## 1. 目标与边界

本切片只迁移 M3 拥有的 SDF 地表、树木 HISM 和岩石 HISM：

- 地表继续由 `UABTSM3TerrainMaterialBridge` 创建唯一 `TerrainMID`，使用 `M3Surface` / `InPlaceStyleParameter`；
- 树石只发布 `M3BackgroundProp` / `ReversibleSlotOverride` 的只读槽绑定，由 Integration 的共享注册表统一应用和恢复；
- 保留 CellTopo、TaskGraph、道路、河流、边界、地貌色板、碰撞、Transform、实例数量、Cull Distance、批次和 Hash；
- 风格缺失只回退原表现，不阻断 PlanetReady/WorldReady；
- 不修改 T3-A0 契约、共享注册表、WorldSubsystem、M7/M11、Config、Lumen、Tone、Outline、光源或阴影；不处理 `TOON-T2A-002`。

## 2. SDF 地表实现

### 2.1 MID 与原参数契约

`UABTSM3TerrainMaterialBridge::Initialize()` 仍创建一个 `TerrainMID`，并在同一个 MID 注入：

- Texture：`M3_CellDirectionLUT`、`M3_CellVisualLUT`、`M3_BoundarySegmentLUT`、`M3_RoadSegmentLUT`、`M3_RiverSegmentLUT`；
- Vector：`M3_PlanetCenter`、`M3_RoadColor`、`M3_RiverColor`；
- Scalar：`M3_CellCount`、`M3_BoundarySlots`、`M3_RoadSegmentCount`、`M3_RiverSegmentCount`、`M3_PlanetRadiusCM`、`M3_BlendWidthCM`。

地貌、道路与河流颜色仍先由原 SDF Custom 节点和 LUT 得到，风格分支只对最终 BaseColor 乘 `ABTS_BaseColorTint`。默认 Tint 为白色，因此不硬编码或替换道路、河流与 Biome 色板。

### 2.2 Style Off/On

`M_ABTS_M3_SDFTerrain` 的 `ABTS_StyleEnabled` 默认值为 `0`：

- `0`：BaseColor 直接走原 Custom 输出；Roughness/Specular/Metallic 分别恢复原未接线默认值 `0.5 / 0.5 / 0.0`，Emissive 为 `0`；
- `1`：BaseColor 允许公共 Tint，Roughness 使用 `max(0.5 * ABTS_RoughnessScale, ABTS_RoughnessFloor)`，Specular 使用 `0.5 * ABTS_SpecularScale`，Metallic 使用 `0.0 * ABTS_MetallicScale`；Rim 由公共 Strength/Power 控制，首版 Strength 为零。

M3Surface 默认值来自 T3-A0 公共契约：

```text
ABTS_BaseColorTint = (1,1,1,1)
ABTS_RoughnessFloor = 0.82
ABTS_RoughnessScale = 1.00
ABTS_SpecularScale = 0.18
ABTS_MetallicScale = 1.00
ABTS_RimStrength = 0.00
ABTS_RimPower = 4.00
```

`ApplyStylizedSurfaceParameters(bool)` 可对已存在的 MID 重复写入 0/1，不重建地形或替换材质。`AABTSM3Planet::ApplyStylizedSurfaceStyle(bool)` 是 Integration 的运行时入口；`TryGetStylizedSurfaceStyleEnabled()` 用于诊断。材质缺少任何公共参数时，桥记录 `SurfaceStyleUnavailable`、拒绝启用风格，但继续保留原地形 MID 和生成流程。

## 3. 树石 HISM 实现

未修改共享原材质：

- `/Game/StaticMesh/Tree/M_PineTree`
- `/Game/StaticMesh/Stone/M_Stone`

新增 M3 唯一所有资产：

- `/Game/M3/Toon/Trees/M_ABTS_M3_ToonPine`
- `/Game/M3/Toon/Stones/M_ABTS_M3_ToonStone`

两者从原材质复制 BaseColor/纹理语义，保留 `Used with Instanced Static Meshes`，只增加冻结的八个公共参数。M3BackgroundProp 默认 `RoughnessFloor=0.76`、`SpecularScale=0.22`、`RimStrength=0`，以高粗糙度、低白色镜面为首版目标。

只读函数：

```cpp
FABTSM3StylizedMaterialAdapter::GatherBackgroundPropMaterialBindings(
    const AABTSM3Planet& Planet,
    TArray<FABTSStylizedMaterialSlotBinding>& OutBindings);
```

输出顺序固定为 ForestHISM slot 0、RockHISM slot 0。每条明确包含 Component、MaterialSlotIndex、StylizedMaterial 和 `M3BackgroundProp`。任一组件、槽或风格资产缺失时跳过该项；两项均缺失时返回空数组，不发布非法绑定。适配器不实例化注册表、不调用 `SetMaterial()`，因此不改变实例、Transform、Cull Distance、碰撞或批次。

## 4. Integration 接入

Integration 合并本提交后，在 `UABTSStylizedRenderingWorldSubsystem::RefreshNow()` 中消费，且必须放在 Style On 条件之外，使 1→0 也能写回地形 MID：

```cpp
for (TActorIterator<AABTSM3Planet> It(World); It; ++It)
{
    It->ApplyStylizedSurfaceStyle(
        FABTSStylizedRenderingControl::IsEnabled());

    TArray<FABTSStylizedMaterialSlotBinding> M3Bindings;
    FABTSM3StylizedMaterialAdapter::
        GatherBackgroundPropMaterialBindings(**It, M3Bindings);
    DesiredMaterialBindings.Append(M3Bindings);
}
```

随后继续由现有共享 `MaterialRegistry->Apply(DesiredMaterialBindings, StyleEnabled)` 应用/恢复树石槽。不得把 `M3Surface` 加入槽注册表，也不得在 M3 复制第二个注册表。

## 5. 自动化与证据

唯一引擎：`C:\Program Files\Epic Games\UE_5.8`。

- `-ForceUnity -DisableAdaptiveUnity` Development Editor：Succeeded；因另一个 M11 工作树 Editor 正在运行且已核对不属于当前项目，按规范增加 `-NoHotReloadFromIDE`，未结束任何进程；
- `ABTS.M3.StylizedMaterials`：`2/2 Success`，覆盖 M3Surface 所有权/采用方式、完整参数、同一 MID 的原 `M3_*` 注入、运行时 0/1、缺失入口 fail soft、树石绑定、确定性顺序/身份，以及 Style 切换前后的 PlanetReady、TaskGraph、实例数、Layout/Result Hash；
- `ABTS.M3.StylizedSemantics`：`1/1 Success`；
- `ABTS.M3.Monthly.SatellitePreview`：`4/4 Success`；
- `ABTS.Rendering.Toon.T3A0`：`2/2 Success`。

fresh 日志：

- `Saved/Logs/M3T3A1-ForceUnityBuild-20260806-212714.log`
- `Saved/Logs/M3T3A1-StylizedMaterials-20260806-212800-FreshAutomation.log`
- `Saved/Logs/M3T3A1-StylizedSemantics-20260806-211943-FreshAutomation.log`
- `Saved/Logs/M3T3A1-MonthlySatellitePreview-20260806-212029-FreshAutomation.log`
- `Saved/Logs/M3T3A1-ToonT3A0-20260806-211943-FreshAutomation.log`

四个过滤器合计 `9/9 Success`，均有 `EXIT CODE: 0` 完成标记。NullRHI 不替代材质像素、Lumen Scene、TSR 或可见 PIE。

## 6. 可见 PIE 验收步骤

以下步骤由用户或 Integration 在合并只读消费后执行；本任务未获 GUI 授权，因此未自行打开 Editor：

1. 关闭当前 M3 Editor，使用唯一 UE 5.8 完整编译 Integration 候选；禁用 Live Coding/Hot Reload。
2. fresh 打开 `L_ABTS_M3`，固定 Seed、GroundDay Profile、相机位置、太阳时间和分辨率，进入 PIE。
3. 控制台执行 `abts.Rendering.Stylized.Enabled 0`。记录地表 MID、PCG LayoutHash、月度 ResultHash、Forest/Rock 实例数与截图；道路、河流、边界和 Biome 色板必须与 T3 前一致。
4. 不退出 PIE、不重建 Planet，执行 `abts.Rendering.Stylized.Enabled 1`。日志必须出现 `SurfaceStyle=1 Family=M3Surface Adoption=InPlaceStyleParameter`；地表白色高光明显收敛，树石减少塑料感，但颜色身份不漂移。
5. 核对树石仍各为一个 HISM 组件批次，实例数量、Transform、Cull Distance、碰撞和可见范围不变；不得为每个实例创建 MID、Actor 或 Custom Depth producer。
6. 同一 PIE 中切回 `0`。日志必须出现 `SurfaceStyle=0`，树石精确恢复原材质槽，地表走原分支；不得残留风格材质或改变 Hash/WorldReady。
7. 用相同相机保存 Style Off/On 对照，并检查近景/远景道路、河流、地貌交界、森林和岩石。`TOON-T2A-002` 远端粗褶皱仍按 T4 独立处理，不以本轮 Roughness 改善关闭。

### 6.1 2026-08-07 验收结果

用户已在 Integration 待验收分支完成本节可见 PIE 验收并确认通过。结合 UE 5.8 ForceUnity 成功、`ABTS.M3.Stylized` 3/3 与 `ABTS.Rendering.Toon.T3A` 3/3，本 T3-A1 独立切片验收门已关闭。该结论只覆盖 M3 SDF 地表、树木和岩石材质族，不替代仍待 M7/T3-B 接入后的 T3-C 全量视觉、GPU 与回归冻结。

## 7. 当前限制

- M3 已发布运行时地表入口和只读 HISM 绑定，共享 WorldSubsystem 已由 Integration 接入并通过可见 PIE；后续修改共享材质注册表、M3 TerrainMID 或树石资产时仍须重跑本阶段 0→1→0 回归。
- 本轮没有 GPU 像素/耗时证据；T3-C 仍需完整 M7、Scene Capture、1080p/1440p GPU 和可见 PIE 门。
- 首版 RimStrength 为零，未引入额外高光；后续美术调参仍只能使用冻结公共参数名。
