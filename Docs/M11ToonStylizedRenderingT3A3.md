# M11 三渲二 T3-A3：助推行星、UFO 与终局专属材质

> 状态：M11 代码、自动化与资产合同完成，Integration 只读消费已接入待验收分支；待可见 PIE/AVI。2026-08-07 接入。
>
> 基线：`803bb2512c0da68ac889ce25d98fa4f91cbe10b1`（T3-A0）。
>
> 上游：[三渲二总设计](ABTSToonStylizedRenderingDesign.md) · [T3-A0 共享材质契约](ABTSToonStylizedRenderingT3A0.md)

## 1. 目标与边界

T3-A3 只负责 M11 已提交的三颗 Gravity Assist 表现行星和物理 UFO 表面：

- 三颗行星发布 `FinalePlanet`；UFO 发布 `FinaleUFO`；
- 两族均由公共契约解析为 `M11 + ReversibleSlotOverride`；
- 只声明只读 `FABTSStylizedMaterialSlotBinding`，不拥有注册表、不直接切换材质；
- 不读取 Actor 名称、世界遍历顺序、网格 bounds 或槽位数量来推断对象身份；
- 不修改求解、认证布局、轨迹播放、前缀稳定器、导演状态机、PIP 选择或 AVI 时序；
- 不处理星空、雾、云、全局光照、公共鸟/弹弓、地形、建筑或 `TOON-T2A-002`。

`Style Off` 的精确恢复继续由 Integration 的唯一
`FABTSStylizedMaterialOverrideRegistry` 负责；M11 没有第二份注册表、CVar、SceneCapture 开关或 AVI 专用材质路径。

## 2. 只读绑定入口

文件：

- `Source/ABTSRuntime/Public/World/ABTSM11StylizedMaterialAdapter.h`
- `Source/ABTSRuntime/Private/World/ABTSM11StylizedMaterialAdapter.cpp`

Integration 待接入口：

```cpp
TArray<FABTSStylizedMaterialSlotBinding> M11Bindings;
FABTSM11StylizedMaterialAdapter::CollectBindings(
    FinaleSystem,
    M11Bindings);
```

默认入口由 M11 自己解析四个 M11-owned 软资产路径。另有可注入
`FABTSM11StylizedMaterialSet` 的重载，供自动化或 Integration 显式提供材质接口。
材质缺失只跳过对应 Actor，不产生含 null 材质的非法绑定。

绑定算法固定为：

1. 只读取 `AABTSM11FinaleSystem::GetGravityBodyActors()` 和 `GetUFOActor()`；
2. 用 `TryGetStylizedObjectClass()` 复核 Actor 属于当前 Ready System、Owner 正确、表现已配置且 Stable ID/Role 与冻结布局一致；
3. 从具体 Actor 的 `GetVisualMeshComponent()` 取得唯一表现网格；
4. 行星按 Certified Assist Stable ID 排序，UFO 排在行星之后；
5. 对该明确组件当前实际存在的合法槽位逐条发布 Component、SlotIndex、Material、Family；
6. 同一组件去重；任何未知、外来、未配置、无材质或无合法槽位对象都 fail soft。

因此隐藏辅助组件、碰撞代理、Spline/轨迹、HUD、二维轨迹图、SceneCapture 和 Niagara 不会进入候选集合。

## 3. 资产路径与公共参数

默认软路径：

| 语义 | 资产路径 | Family |
| --- | --- | --- |
| Assist1 / Mars | `/Game/M11/Toon/Planets/Mars/MI_Mars_FinalePlanet` | `FinalePlanet` |
| Assist2 / Jupiter | `/Game/M11/Toon/Planets/Jupiter/MI_Jupiter_FinalePlanet` | `FinalePlanet` |
| Assist3 / Saturn | `/Game/M11/Toon/Planets/Saturn/MI_Saturn_FinalePlanet` | `FinalePlanet` |
| UFO | `/Game/M11/Toon/UFO/MI_UFO_FinaleUFO` | `FinaleUFO` |

材质只使用 T3-A0 的八个风格参数，不建立 M11 同义参数：

```text
ABTS_StyleEnabled
ABTS_BaseColorTint
ABTS_RoughnessFloor
ABTS_RoughnessScale
ABTS_SpecularScale
ABTS_MetallicScale
ABTS_RimStrength
ABTS_RimPower
```

首版默认方向沿用 T3-A0：

- `FinalePlanet`：Roughness Floor `0.66`、Specular Scale `0.28`、Rim `0.20 / Power 4`；三颗实例分别保留颜色/纹理身份，不做统一 Emissive 球；
- `FinaleUFO`：Roughness Floor `0.30`、Specular Scale `0.64`、Metallic Scale `0.95`、Rim `0.22 / Power 7`；Rim 只轻微调制 Base Color/受光边缘，不以 Emissive 抹平体积。

UE 5.8 资产反射检查确认当前：

- Assist1/2/3 已按 Mars/Jupiter/Saturn 显式软路径接入；三颗主体的 authored reference radius 都是 `50 cm`；
- Mars/Jupiter bounds 原点为球心、单槽，完整包围球分别为 `52.249 cm`、`52.848 cm`；低模表面起伏不反向改变 authored reference radius；
- Saturn bounds 原点为球心、单槽；主体半径为 `50 cm`，环平面最大 extent 为 `71.891 cm`、完整包围球 `75.763 cm`；环不进入主体 reference radius、解析碰撞或引力参数；
- 三颗原接口分别为 `MI_Mars`、`MI_Jupiter`、`MI_Saturn`；
- `SM_UFO` 和 `SM_UFO_Intact` 都只有一个 `Material` 槽；
- 原接口均为 `/Game/StaticMesh/UFO/MI__UFO`；
- `MI__UFO` 的 BaseColor/Normal/Roughness 纹理分别来自现有 `T_UFO_*`；
- `M_M11_FinalePlanet` 与 `M_M11_FinaleUFO` 均为 Opaque / Default Lit；Planet 为 Two Sided，UFO 为单面，Emissive/Opacity 均未连接；
- 两个 Custom 都是 `CMOT Float3 + 3 x CMOT Float1`，14 个输入名称、顺序和连接来源与第 4.1 节一致；Planet/UFO HLSL 分别使用 `0.25/0.35` Rim Base Color 权重；
- 两个 Normal 输入都来自 `TextureSampleParameter2D` 的 `Normal` Sampler；八个公共参数的有效默认值与 T3-A0 合同一致；
- 三颗 Planet 实例分别解析自己的四张贴图；UFO 母材质与实例均解析现有 `/Game/StaticMesh/UFO/T_UFO_*` 四张贴图；
- 六个材质资产在 fresh UE 5.8 NullRHI 进程中重载/重编译为 `0 error(s), 0 warning(s)`；验证日志为 `Saved/Logs/M11-T3A3-MaterialAssetVerify-20260807.log`；
- T3-A3 不修改原行星/UFO 默认材质资产，Style Off 仍由注册表恢复同一接口指针。

## 4. Editor 资产步骤

资产搭建已由用户在唯一允许的 UE 5.8 Editor 中完成；以下记录最终落地结果。资产合同通过仍不能替代可见 PIE/AVI 视觉验收。

1. 从当前 M11 工作树的 `AngryBirdsToSpace.uproject` 打开 Editor，禁用 Live Coding/Hot Reload。
2. 已完成：Mars/Jupiter/Saturn 分别位于 `Content/M11/Toon/Planets/<Planet>/`，主体 unit-scale authored reference radius 均为 `50 cm`；Saturn 的环允许超出主体。
3. 已完成：三颗网格各有一个明确可见材质槽；M11 只把三个 authored reference radius 写为 `50 cm`，不从 bounds 猜测。
4. 已完成：在各自 Planet 文件夹建立 `MI_Mars_FinalePlanet`、`MI_Jupiter_FinalePlanet`、`MI_Saturn_FinalePlanet`；三颗分别复用已有 BaseColor/Normal/Roughness/Metallic 纹理身份。
5. 已完成：在 `Content/M11/Toon/UFO/` 建立 `MI_UFO_FinaleUFO`；复用现有 BaseColor/Normal/Roughness/Metallic 纹理，不修改网格、碰撞或原 `MI__UFO`。
6. 已完成：两个母材质只公开第 3 节八个公共风格参数；Rim 只调制 Base Color，没有 Emissive 或 Unlit 旁路。
7. 已完成：只保存 `Content/M11/Toon/**` 中本任务资产；没有保存 `L_ABTS_M11.umap`、Config、共享 Blueprint、公共材质或重定向器。
8. 已完成：关闭当前工作树 Editor 后执行 C++ 全链接检查和全部 fresh NullRHI 自动化。

低模网格默认软路径和三个 `50 cm` authored reference radius 已在 M11 自有 `AABTSM11FinaleSystem` 中显式绑定；它们只决定表现缩放，不改权威 `VisualRadiusCM`、CollisionRadius 或引力参数。

### 4.1 输入 → Custom → 输出的严格合同

建立两个 Opaque / Default Lit 母材质：

- `Content/M11/Toon/Planets/M_M11_FinalePlanet`：开启 Two Sided，以保证 Saturn 环背面可读；
- `Content/M11/Toon/UFO/M_M11_FinaleUFO`：保持单面；
- 两者均保持 Emissive、Opacity 未连接，不改 Blend Mode，也不切换为 Unlit。

两套材质采用同一组输入和输出，只有 Custom 内的 Rim Base Color 权重及材质实例参数不同。完整连线如下：

```text
TextureCoordinate[0]
  ├─ DiffuseColorMap RGB ──────────────────────────────┐
  ├─ ShininessMap R ───────────────────────────────────┤
  └─ MetallicMap R ────────────────────────────────────┤
Constant(0.5) ─────────────────────────────────────────┤
PixelNormalWS ─────────────────────────────────────────┤
CameraVectorWS ────────────────────────────────────────┤
八个 ABTS_* 公共参数 ──────────────────────────────────┤
                                                        ▼
                                                    [Custom]
                                                        ├─ Return Float3 ──> Base Color
                                                        ├─ OutRoughness ───> Roughness
                                                        ├─ OutSpecular ────> Specular
                                                        └─ OutMetallic ────> Metallic

NormalMap RGB ─────────────────────────────────────────────────────────────> Normal
```

Normal 不进入 Custom：使用标准 `Texture Sample Parameter2D` 的 Normal 解码路径直接连接材质 `Normal`，避免在 Custom 中声明纹理、Sampler 或重复解码切线空间法线。

#### 4.1.1 输入节点

两个母材质都建立以下贴图输入。它们承载现有内容贴图，不是新的风格参数，也不是 T3-A0 参数的 M11 私有同义词：

| Parameter Name | 节点与 Sampler Type | 输出接线 | 母材质默认纹理 |
| --- | --- | --- | --- |
| `DiffuseColorMap` | Texture Sample Parameter2D / `Color` | RGB → Custom `SourceBaseColor` | Planet 使用 Mars BaseColor；UFO 使用现有 `T_UFO_Basecolor` |
| `NormalMap` | Texture Sample Parameter2D / `Normal` | RGB → Material `Normal` | 对应资产的 Normal |
| `ShininessMap` | Texture Sample Parameter2D / `Linear Grayscale` | R → Custom `SourceRoughness` | 对应的 `T_*_Roughness`；名称沿用导入接口，但数值是 Roughness |
| `MetallicMap` | Texture Sample Parameter2D / `Linear Grayscale` | R → Custom `SourceMetallic` | 对应的 `T_*_Metallic` |

所有贴图 Sample 的 UV 都接 `TextureCoordinate[0]`。另建一个值为 `0.5` 的 Constant，连接 Custom `SourceSpecular`。

只建立下列八个公共参数，名称必须逐字符一致：

| Custom 输入名 | 参数节点 | Planet 默认值 | UFO 默认值 |
| --- | --- | --- | --- |
| `ABTS_StyleEnabled` | Scalar Parameter | `1.0` | `1.0` |
| `ABTS_BaseColorTint` | Vector Parameter | `(1,1,1,1)` | `(1,1,1,1)` |
| `ABTS_RoughnessFloor` | Scalar Parameter | `0.66` | `0.30` |
| `ABTS_RoughnessScale` | Scalar Parameter | `1.0` | `1.0` |
| `ABTS_SpecularScale` | Scalar Parameter | `0.28` | `0.64` |
| `ABTS_MetallicScale` | Scalar Parameter | `1.0` | `0.95` |
| `ABTS_RimStrength` | Scalar Parameter | `0.20` | `0.22` |
| `ABTS_RimPower` | Scalar Parameter | `4.0` | `7.0` |

另建 `PixelNormalWS` 和 `CameraVectorWS` 节点，分别连接 Custom 的同名输入。

#### 4.1.2 Custom 节点属性与输入顺序

两个 Custom 节点均设置：

- Output Type：`CMOT Float3`；
- Additional Outputs：`OutRoughness` / `CMOT Float1`、`OutSpecular` / `CMOT Float1`、`OutMetallic` / `CMOT Float1`；
- Planet Description：`M11 FinalePlanet Stylized Surface`；
- UFO Description：`M11 FinaleUFO Stylized Surface`。

按下表顺序添加 Custom Inputs；拼写和大小写必须一致：

| 顺序 | Input Name | 连接来源 |
| ---: | --- | --- |
| 1 | `SourceBaseColor` | `DiffuseColorMap` RGB |
| 2 | `SourceRoughness` | `ShininessMap` R |
| 3 | `SourceSpecular` | Constant `0.5` |
| 4 | `SourceMetallic` | `MetallicMap` R |
| 5 | `PixelNormalWS` | PixelNormalWS |
| 6 | `CameraVectorWS` | CameraVectorWS |
| 7 | `ABTS_StyleEnabled` | 同名 Scalar Parameter |
| 8 | `ABTS_BaseColorTint` | 同名 Vector Parameter |
| 9 | `ABTS_RoughnessFloor` | 同名 Scalar Parameter |
| 10 | `ABTS_RoughnessScale` | 同名 Scalar Parameter |
| 11 | `ABTS_SpecularScale` | 同名 Scalar Parameter |
| 12 | `ABTS_MetallicScale` | 同名 Scalar Parameter |
| 13 | `ABTS_RimStrength` | 同名 Scalar Parameter |
| 14 | `ABTS_RimPower` | 同名 Scalar Parameter |

#### 4.1.3 FinalePlanet 可直接粘贴的 Custom HLSL

将以下代码完整复制到 `M_M11_FinalePlanet` 的 Custom Code。代码中不声明顶层函数、不包含 `.ush`，也不在 Custom 内采样纹理：

```hlsl
float styleEnabled = saturate(ABTS_StyleEnabled);
float3 sourceBase = max(SourceBaseColor, float3(0.0, 0.0, 0.0));
float sourceRoughness = saturate(SourceRoughness);
float sourceSpecular = saturate(SourceSpecular);
float sourceMetallic = saturate(SourceMetallic);

float normalLengthSq = max(dot(PixelNormalWS, PixelNormalWS), 1.0e-8);
float viewLengthSq = max(dot(CameraVectorWS, CameraVectorWS), 1.0e-8);
float3 normalWS = PixelNormalWS * rsqrt(normalLengthSq);
float3 viewWS = CameraVectorWS * rsqrt(viewLengthSq);
float rimBase = 1.0 - saturate(dot(normalWS, viewWS));
float rimPower = clamp(ABTS_RimPower, 1.0, 32.0);
float rimStrength = saturate(ABTS_RimStrength);
float rim = pow(saturate(rimBase), rimPower) * rimStrength;

float3 tint = max(ABTS_BaseColorTint.rgb, float3(0.0, 0.0, 0.0));
float3 styledBase = saturate(sourceBase * tint * (1.0 + rim * 0.25));
float styledRoughness = max(
    saturate(ABTS_RoughnessFloor),
    saturate(sourceRoughness * clamp(ABTS_RoughnessScale, 0.0, 2.0)));
float styledSpecular =
    saturate(sourceSpecular * clamp(ABTS_SpecularScale, 0.0, 2.0));
float styledMetallic =
    saturate(sourceMetallic * saturate(ABTS_MetallicScale));

OutRoughness = lerp(sourceRoughness, styledRoughness, styleEnabled);
OutSpecular = lerp(sourceSpecular, styledSpecular, styleEnabled);
OutMetallic = lerp(sourceMetallic, styledMetallic, styleEnabled);
return lerp(sourceBase, styledBase, styleEnabled);
```

#### 4.1.4 FinaleUFO 可直接粘贴的 Custom HLSL

将以下代码完整复制到 `M_M11_FinaleUFO` 的 Custom Code。UFO 只把 Rim Base Color 权重提高到 `0.35`；没有 Emissive 补光路径：

```hlsl
float styleEnabled = saturate(ABTS_StyleEnabled);
float3 sourceBase = max(SourceBaseColor, float3(0.0, 0.0, 0.0));
float sourceRoughness = saturate(SourceRoughness);
float sourceSpecular = saturate(SourceSpecular);
float sourceMetallic = saturate(SourceMetallic);

float normalLengthSq = max(dot(PixelNormalWS, PixelNormalWS), 1.0e-8);
float viewLengthSq = max(dot(CameraVectorWS, CameraVectorWS), 1.0e-8);
float3 normalWS = PixelNormalWS * rsqrt(normalLengthSq);
float3 viewWS = CameraVectorWS * rsqrt(viewLengthSq);
float rimBase = 1.0 - saturate(dot(normalWS, viewWS));
float rimPower = clamp(ABTS_RimPower, 1.0, 32.0);
float rimStrength = saturate(ABTS_RimStrength);
float rim = pow(saturate(rimBase), rimPower) * rimStrength;

float3 tint = max(ABTS_BaseColorTint.rgb, float3(0.0, 0.0, 0.0));
float3 styledBase = saturate(sourceBase * tint * (1.0 + rim * 0.35));
float styledRoughness = max(
    saturate(ABTS_RoughnessFloor),
    saturate(sourceRoughness * clamp(ABTS_RoughnessScale, 0.0, 2.0)));
float styledSpecular =
    saturate(sourceSpecular * clamp(ABTS_SpecularScale, 0.0, 2.0));
float styledMetallic =
    saturate(sourceMetallic * saturate(ABTS_MetallicScale));

OutRoughness = lerp(sourceRoughness, styledRoughness, styleEnabled);
OutSpecular = lerp(sourceSpecular, styledSpecular, styleEnabled);
OutMetallic = lerp(sourceMetallic, styledMetallic, styleEnabled);
return lerp(sourceBase, styledBase, styleEnabled);
```

#### 4.1.5 Custom 输出与实例

| Custom 输出 | 材质输入 |
| --- | --- |
| 主输出（return Float3） | Base Color |
| `OutRoughness` | Roughness |
| `OutSpecular` | Specular |
| `OutMetallic` | Metallic |

`NormalMap` RGB 仍直接连接材质 `Normal`。Emissive、Opacity 保持未连接。

创建四个实例并设置：

| 实例 | 贴图 | 公共风格值 |
| --- | --- | --- |
| `Mars/MI_Mars_FinalePlanet` | `T_Mars_BaseColor/Normal/Roughness/Metallic` | Style `1`、Tint 白、Roughness `0.66/1.0`、Specular `0.28`、Metallic `1.0`、Rim `0.20/4` |
| `Jupiter/MI_Jupiter_FinalePlanet` | `T_Jupiter_*` | 同 `FinalePlanet` |
| `Saturn/MI_Saturn_FinalePlanet` | `T_Saturn_*` | 同 `FinalePlanet` |
| `UFO/MI_UFO_FinaleUFO` | 现有 `/Game/StaticMesh/UFO/T_UFO_*` | Style `1`、Tint 白、Roughness `0.30/1.0`、Specular `0.64`、Metallic `0.95`、Rim `0.22/7` |

保存前逐项检查：Custom Input 拼写/大小写和 Additional Output 名称完全一致；四张纹理均非空；Normal Sample 的 Sampler Type 为 `Normal`；Material Stats 无编译错误。Custom 内部的 `ABTS_StyleEnabled=0` 分支返回原贴图 PBR 输入，运行时 Style Off 的精确材质接口恢复仍由 Integration 注册表完成：原 `MI_Mars/MI_Jupiter/MI_Saturn/MI__UFO` 必须继续留在原网格槽上，不能把风格实例写回网格默认材质。

## 5. 自动化与不变量

独立过滤器 `ABTS.M11.StylizedMaterials` 包含：

| 测试 | 覆盖 |
| --- | --- |
| `Contract` | M11 所有权、ReversibleSlotOverride、默认参数有效及行星/UFO Roughness 关系 |
| `BindingAdapter` | 真实 3+1 Actor、确定性 Family、合法槽位、遍历顺序稳定、缺材质 fail soft、辅助/轨迹/碰撞代理排除 |
| `AuthorityParity` | 共享注册表 Style On/Off、精确原接口恢复、全部求解点/事件相等、Scenario/Preset/Certification/Bundle/Frame/Camera Pose Hash 不变、Actor Transform 不变 |

交接门：

```text
ABTS.M11.StylizedMaterials
ABTS.Rendering.Toon.T3A0
ABTS.M11B.Runtime.StylizedSemanticAdapter
ABTS.M11B.Runtime.ActorAuthority
ABTS.M11C.Runtime.CameraClassParity
ABTS.M11C.CameraCapture.Config
```

本阶段没有改求解器、冻结布局或认证身份，不重跑 FullInputDomain。任何权威 Hash 变化都属于越界回归，禁止更新冻结结果迁就。

2026-08-07 资产落地后的 fresh 证据（唯一引擎 `C:\Program Files\Epic Games\UE_5.8`）：

- `-ForceUnity -DisableAdaptiveUnity` Development Editor：目标已最新、`Result: Succeeded`；此前同一实现全编译为 4 actions / Succeeded。最终日志 `Saved/Logs/M11-T3A3-FinalAssets-ForceUnity-20260807-UBT.log`；
- 材质资产 fresh 重载、合同反射与重编译：六个资产全部加载，两个 Custom 各 14 个已连接输入，`0 error(s), 0 warning(s)`；日志 `Saved/Logs/M11-T3A3-MaterialAssetVerify-20260807.log`；
- `ABTS.M11.StylizedMaterials`：`3/3` Success、零失败、进程退出码 0，日志 `Saved/Logs/M11-T3A3-FinalAssets-StylizedMaterials-20260807-FreshAutomation.log`；
- `ABTS.Rendering.Toon.T3A0`：`2/2`，日志 `Saved/Logs/M11-T3A3-FinalAssets-T3A0-20260807-FreshAutomation.log`；
- `ABTS.M11B.Runtime.StylizedSemanticAdapter`：`1/1`，日志 `Saved/Logs/M11-T3A3-FinalAssets-StylizedSemanticAdapter-20260807-FreshAutomation.log`；
- `ABTS.M11B.Runtime.ActorAuthority`：`1/1`，日志 `Saved/Logs/M11-T3A3-FinalAssets-ActorAuthority-20260807-FreshAutomation.log`；
- `ABTS.M11C.Runtime.CameraClassParity`：`1/1`，日志 `Saved/Logs/M11-T3A3-FinalAssets-CameraClassParity-20260807-FreshAutomation.log`；
- `ABTS.M11C.CameraCapture.Config`：`1/1`，日志 `Saved/Logs/M11-T3A3-FinalAssets-CameraCaptureConfig-20260807-FreshAutomation.log`；
- 六个过滤器合计 `9/9` Success、零失败；未运行 FullInputDomain，未启动可见 Editor/PIE/AVI。

2026-08-07 Integration 待验收分支接入唯一共享注册表后，使用同一 UE 5.8 再次完成 ForceUnity；fresh NullRHI `ABTS.M11.StylizedMaterials` 为 3/3，`ABTS.Rendering.Toon.T3A` 为 3/3，分别记录于 `Saved/Logs/M11T3A3-Integration-StylizedMaterials-20260807.log` 与 `Saved/Logs/M11T3A3-Integration-ToonT3A-20260807.log`。这组证据证明集成编译、M11 绑定/权威不变量及共享注册表回归，不替代下一节 PIE/AVI 像素门。

## 6. 可见 PIE / AVI 验收

资产和 Integration 接线后，由用户/Integration 串行执行：

1. fresh 打开 `L_ABTS_M11`，固定同一 Seed、Candidate/Certified 身份、相机与时间；分别记录 `abts.Rendering.Stylized.Enabled 0/1`。
2. MainWorld：三颗行星远距离轮廓和颜色身份可读；不是同色发光球；UFO 有窄金属高光和略强 Rim，背光不死黑。
3. FinaleRemotePreview：AUTO/Probe 的目标选择、RenderTarget 矩形、背景刷新规则和权威局部轨迹完全不变；只比较目标表面风格。
4. FinaleCinematicCapture：使用既有 `FinaleCinematicCapture` ViewClass 和 v6 Runner；Style Off/On 的帧数、相机 Pose、Stage/Target/Reason 指纹、TargetHit 帧序完全相同。
5. Style Off 后四个组件恢复进入 T3 前的精确 `UMaterialInterface*`；若外部系统已改同槽，Integration 注册表必须记录冲突并保留外部接口。
6. 检查无碰撞、质量、引力参数、四体位置、轨迹、HUD、二维图、Niagara、雾云星空或地图资产变化。

## 7. 当前限制与 Integration 交接

- 当前状态是“代码、自动化、资产合同与 Integration 接线完成，待可见 PIE/AVI”，不是 T3-A3 视觉完成。
- Integration 已只枚举 Ready 的 `AABTSM11FinaleSystem`，调用默认 `CollectBindings()` 并把结果交给现有唯一 Registry；M11 仍不得创建第二份 Registry。
- 所有 View 继续使用既有 ViewClass 与全局 `abts.Rendering.Stylized.Enabled`。
- `Content/Maps/L_ABTS_M11.umap` 不在本阶段写入范围。
