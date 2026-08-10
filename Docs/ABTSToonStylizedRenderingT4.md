# ABTS 三渲二 T4：球面环境、光照、云雾与程序化星空

> 状态：2026-08-09 更新。T4-A0 已完成用户截图验收。T4-A1 的球心 Sky Atmosphere、可逆全局 Z 高度雾关闭、固定曝光、确定性程序化 HDR 星场和 `ToonT4A1` 捕获合同已落地；天空/太阳/星场与物体 Tone 已按 SceneDepth 分离，主视图与 SceneCapture 共用暗部无增益保护，GroundDay 使用原相机位置沿球面正北（世界 +Z 的切向投影）观察云际。正式 Style On 的 GroundDay v18 已隔离原生低频天空颜色并消除明暗两处斜向色阶；v19 进一步用观察者太阳高度和视线朝阳程度共同近似晨昏光程，修复“夜侧边缘看向太阳反而比昼侧边缘背向太阳更暗”。`ToonT4A1` 现含十个固定点、20 张 Style Off/On 捕获；四个精确天空点的 float32 高频残差均无成组斜向轮廓，朝阳/背阳天空平均亮度关系为 `0.5881 > 0.2797`。UE 5.8 ForceUnity、fresh NullRHI `ABTS.Rendering.Toon` 18/18 与用户可见 PIE 均已通过；新版 GPU 基线仍需在 T4 联合冻结前刷新。体积云正式留给 T4-A2，A2–A3/B 尚未开始。
>
> 唯一验收地图：`/Game/Maps/L_ABTS_M11`。唯一引擎：`C:\Program Files\Epic Games\UE_5.8`。
>
> 上游：[三渲二总设计](ABTSToonStylizedRenderingDesign.md) · [T0 自动视觉基线](ABTSToonVisualCaptureT0.md) · [T2-A 描边](ABTSToonStylizedRenderingT2A.md) · [T3-A0 材质合同](ABTSToonStylizedRenderingT3A0.md)

## 1. 目标与边界

T4 把当前依赖全局 Z 高度的雾、云与天空，改造成共同消费球面环境合同的表现层，并建立从昼面地表、晨昏、夜面、高空到终局太空的连续视觉语言。首版继续使用安装版 UE 5.8 的 Sky Atmosphere、现有 Lumen/VSM 和项目渲染扩展，不分叉引擎、不实现自定义 Shading Model。

T4 不改变：

- M3 CellTopo、Task Graph、地形几何和世界接受权威；
- M7 建筑算法、Chaos、材料破坏与门禁；
- M9 卫星引力、M11 四体积分、轨迹和演出位置；
- 角色/弹弓相机输入和玩法状态；
- T1–T3 已验收的材质槽、Profile ID 和 Style Off 精确恢复合同。

环境表现失败时必须 fail soft 到当前渲染，不能阻断 `WorldReady`。环境身份歧义则对 T4 诊断和正式 Profile fail closed，不能静默选择任意主星或太阳。

## 2. 冻结实施顺序

| 阶段 | 内容 | 明确不做 | 退出门 |
| --- | --- | --- | --- |
| **T4-A0 球面环境合同与诊断** | 主星中心/半径、太阳方向、Profile 的只读快照；五个固定环境截图点；Tone/Outline/Shadow 隔离矩阵 | 不改生产光照、雾云、曝光和最终参数 | ForceUnity；`ABTS.Rendering.Toon.T4A0`；30 张同姿态矩阵可生成；`TOON-T2A-002` 保持开放直至人工判读 |
| **T4-A1 Sky Atmosphere 与程序化 HDR 星场** | 关闭生产全局 Z 高度雾；球心大气；唯一 Atmosphere Sun；固定曝光；昼夜/高度星空显隐 | 不做体积云，不回调 T3 最终材质 | 昼面→晨昏→夜面→高空→太空连续；主视图/PIP/AVI 身份一致；GPU 证据 |
| **T4-A2 球面云原型** | 原生 Volumetric Cloud 径向密度垂直切片；失败则切换双层风格化云壳 | 不先开云影，不把云写进 Toon 后处理 | GPU、噪点、时域稳定性、SceneCapture 四门；路线决策被记录 |
| **T4-A3 环境 Profile** | `GroundDay`、`SatelliteGuide`、`FinaleSpace` 环境装配；M11 快照与失败恢复 | 不改变 M11 求解/轨迹 | 主视图、地面/月面/终局 PIP、Rank11 AVI 与退出终局恢复一致 |
| **T4-B T3/T4 联合校色** | 回开并调整 Roughness、Specular、Rim、Tint；解决地形褶皱；形成非 M7 联合基线 | M7 未完成时不宣称全项目冻结 | `TOON-T2A-002` 关闭证据；T3/T4 视觉和 GPU 联合基线；M7 后补建筑材质/特效 |

阶段必须按 A0→A1→A2→A3→B 前进。允许在 A2 证明原生体积云不适合当前小星球尺度后切换云壳，但不允许跳过 A0 身份与 A1 大气/星空基础直接调云。

## 3. T4-A0：只读球面环境合同

`FABTSToonEnvironmentSnapshot` 是 Integration 所有的值快照，当前版本为 1：

```text
Version
PlanetCenterWorld
PlanetRadiusCM
SunDirectionToSunWorld       // 主星指向太阳的单位向量
Profile                      // GroundDay / SatelliteGuide / FinaleSpace
WorldSeed / GeneratorVersion / GenerationAttempt
bSourceWorldAccepted
IdentityHash
```

唯一运行时解析规则：

1. 世界中必须恰有一个 `AABTSM3Planet`，且其 M3 表现和已接受 `FABTSFinaleWorldContract` 可用；
2. Actor 半径必须与合同 `PrimaryRadiusCM` 一致；
3. 必须恰有一个可见、启用 `Atmosphere Sun Light`、且 `AtmosphereSunLightIndex == 0` 的 `UDirectionalLightComponent`；
4. UE Directional Light 的 `GetDirection()` 是光线传播方向，合同保存其反向量，即主星指向太阳；
5. Profile、Seed、版本、Attempt 或接受状态非法时 fail closed；
6. Hash 对中心/半径按 0.1 cm、太阳单位方向按 `1e-6` 量化，Profile 和世界身份均进入 Hash。

`UABTSStylizedRenderingWorldSubsystem` 只发布最新有效快照并输出：

```text
[ABTS][Rendering][T4-A0][Environment] Ready=1 ... SnapshotHash=...
```

快照不能反向修改 M3 Actor、Directional Light 或游戏 Profile。后续大气、雾、云、星空和 M11 环境切换都只能读取它。

## 4. 五个固定环境截图点

T4-A0 复用 T0 的 transient camera、Pose Hash、PNG 解码/MD5 和原子 manifest，不新建第二套地图或绝对坐标表。五点都从当次快照和语义对象解析：

| ID | 解析 | Profile | 观察目标 |
| --- | --- | --- | --- |
| `GroundDay` | 局部 Up = 主星指向太阳 | `GroundDay` | 正午地表、地形色阶和天际线 |
| `GroundDawn` | 初始道路径向 Up 正交投影到晨昏圈；退化时确定性选轴 | `GroundDay` | 掠射光、长阴影与远端褶皱高风险区 |
| `GroundNight` | 局部 Up = 主星背向太阳 | `GroundDay` | 夜面可读性、星空显隐和曝光 |
| `HighAltitude` | 太阳方向和晨昏方向组成的固定高空视点 | `SatelliteGuide` | 球面轮廓、大气薄层、高空星空与云壳 |
| `FinaleSpace` | 现有 M11 终局局部帧与 3+1 包络 | `FinaleSpace` | 终局星空、行星/UFO 与空间 Profile |

同一点的所有变体必须拥有相同 `semanticIdentityHash`、`cameraPoseHash`、`effectiveCameraPoseHash`、`environmentSnapshotHash`、FOV 和分辨率。截图探针暂停世界、每次 Camera Cut 后执行相同 warmup；它不移动玩法 Actor、不进入发射状态。

## 5. `TOON-T2A-002` 隔离矩阵

T4-A0 给后处理加入默认值为 `ToneAndOutline` 的诊断掩码。它只决定 Tone 和 Outline pass 是否注册；Style 开关仍决定 T3 材质族是否启用。捕获结束无论成功、失败或中断，都恢复原 Profile、Style、掩码、`r.ShadowQuality`、暂停和 ViewTarget。

| 变体 | T3 材质 | Tone | Outline | 阴影 | 用途 |
| --- | ---: | ---: | ---: | ---: | --- |
| `StyleOff` | 否 | 否 | 否 | 是 | T1–T3 前的完整视觉基线 |
| `ToneOnly` | 是 | 是 | 否 | 是 | 判断量化明暗自身是否形成粗褶皱 |
| `OutlineOnly` | 是 | 否 | 是 | 是 | 判断法线/深度轮廓自身贡献 |
| `ToneOutline` | 是 | 是 | 是 | 是 | 当前正式组合 |
| `ShadowOff` | 是 | 否 | 否 | 否 | 无投影的材质/基础光照控制组 |
| `LightingOnly` | 是 | 否 | 否 | 是 | 与 `ShadowOff` 对比得到阴影贡献 |

`ToneOutline - ToneOnly` 观察轮廓增量，`ToneOutline - OutlineOnly` 观察 Tone 增量，`LightingOnly - ShadowOff` 观察阴影增量。只有矩阵人工判读证明根因后，T4-B 才能修改轮廓或阴影参数；A0 不关闭 `TOON-T2A-002`。

运行命令沿用 T0 的世界/Seed/分辨率身份参数，只把 Suite 改成 T4-A0：

```powershell
$Editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$Project = 'C:\workspace\AngryBirdsToSpace\AngryBirdsToSpace.uproject'
$BuildId = (git -C 'C:\workspace\AngryBirdsToSpace' rev-parse HEAD).Trim()

& $Editor $Project /Game/Maps/L_ABTS_M11 `
  -game -dx12 -windowed -ForceRes -ResX=1920 -ResY=1080 `
  -ABTSM3R5Preview -ABTSM3R5PreviewCandidate=4 `
  -ABTSM3R31SlotPreviewCandidate=4 `
  -ABTSVisualCaptureSuite=ToonT4A0 `
  -ABTSToonT0Mode=Screenshots "-ABTSToonT0BuildId=$BuildId" `
  -ABTSToonT0ExpectedSeed=312503 -ABTSToonT0WarmupFrames=8 `
  -ABTSToonT0TimeoutSeconds=600 -ABTSToonT0ExitWhenDone
```

成功证据为 `Saved/ABTSVisualCaptures/ToonT4A0/<run>/manifest.json`：`status=Succeeded`、5 点 × 6 变体 = 30 条记录、两个 Catalogue Hash 非零、每条 PNG 可解码且 MD5 非空、日志唯一 `[ABTS][ToonT4A0][Terminal] Success=1`。A0 不接受 GPU 模式；真正 GPU 基线从 A1 的大气/星空像素存在后开始。

## 6. T4-A1：Sky Atmosphere 与确定性程序化 HDR 星场

### 6.0 已落地边界

- 普通世界仍只使用地图中唯一的 `SkyAtmosphere`，不生成或保存 `.uasset/.umap`；Style On 时运行时切换为 `PlanetCenterAtComponentTransform`，中心、半径和大气高度来自当次环境快照；Style Off、子系统退出或失败路径恢复 Actor 位置、Transform Mode 与全部被改参数；
- 所有 `ExponentialHeightFogComponent` 只在 Style On 期间隐藏，原可见性被逐组件保存并恢复；找不到唯一 Sky Atmosphere 或环境快照无效时清空渲染线程参数并 fail soft，不阻断玩法 `WorldReady`；
- 主视图与已登记的地面/月面/终局 SceneCapture 在 View Setup 阶段使用同一个手动曝光策略；未知 SceneCapture 继续被拒绝；
- 星场由 `ABTSStylizedStarMainPS` 在 HDR 后处理阶段按世界观察方向生成。固定 `StarSeed`、六面方向网格、每像素固定 3×3 相邻单元、整数 Hash 和 `fwidth` 软圆盘保证成本不随可见星数线性增加；
- `ToonT4A1` 保留 A0 五点并增加 `TerminatorSky`、`BrightSkyBanding`、`TerminatorSunwardSky`、`TerminatorAntiSunwardSky` 与 `BacklitBirdParty`，只保留 Style Off/On 两个变体，允许 Screenshots 与 GPUProfile；GroundDay 不移动既有相机位置，只把观察方向冻结为世界 +Z 在当地切平面上的正北方向，以长期比较白天天空色相和云际；四个天空点分别冻结用户在暗处色阶、亮处色阶、晨昏朝阳和晨昏背阳时的精确 Camera Transform/FOV；manifest schema 4 为每条记录写入大气高度、StarSeed、网格、密度、角半径、HDR 强度与曝光偏置。

### 6.1 大气与曝光

- 生产环境关闭现有全局 Z `ExponentialHeightFog` 贡献，不用旋转 Actor 冒充球面高度；
- `SkyAtmosphere` 使用 `Planet Center at Component Transform`，中心、Ground Radius 和 Atmosphere Height 从环境合同换算；
- 只有合同选中的 index 0 Directional Light 可以作为太阳；存在零个或多个时环境 Profile fail closed；
- 固定或严格限制曝光，防止昼夜切换、背光面和 HDR 星点引起曝光泵动；
- ABTS 运行时主星会触及 UE Sky Atmosphere 的 0.1 km 原生半径下限；在该尺度强制逐像素天空追踪会暴露规则的摄像机空间积分块。Style On 期间以引用计数、当前 SetBy 优先级原位替换并保存原值的方式启用 `r.SkyAtmosphere.FastSkyLUT=1`，把 SkyView LUT 提升到 384×208，并令 LUT 追踪在 0.01 km 后稳定使用 16–32 样本；不透明物体继续关闭 Fast Aerial 近似。其余 LUT 使用 32 位精度、32 个透射样本和高质量多重散射，组件 `TraceSampleCountScale` 为 2。最后一个世界释放、Style Off、失败或退出时逐项恢复全部 CVar 和组件原值；
- GroundDay 的天空底色由 Tone pass 按世界视线、径向 Up、太阳方向、观察者 `SunHeight`、地平线项和朝阳项连续解析生成；晨昏附近的昼光权重使用 `ObserverDay` 与 `RayDay(SunHeight + ViewToSun × 0.42)` 连续混合，深昼面/深夜面仍由观察者位置主导。不得用亮度/饱和度掩码把原生 SceneColor 整色回填，因为亮处诊断证明这会重新引入大尺度等值平台。太阳盘/光晕和确定性 HDR 星点在解析底色之后独立重建；本阶段不合成原生体积云，云的球面形态、时域与 SceneCapture 路线统一留给 T4-A2。Satellite/Finale 背景不走 GroundDay 替换；
- 当前 SDR 合同显式使用原生 8-bit RGBA BackBuffer。解析天空在最终写回前进行屏幕稳定、逐通道独立的 8-bit 随机舍入，只改变最后一个量化步长内的取整方向，不使用强噪声掩盖低频轮廓；
- 自定义天空材质使用 `Opaque + Unlit + Is Sky`，并显式组合 SkyAtmosphere 表达式；不能让星空覆盖大气散射。

### 6.2 确定性程序化 HDR 星场

星空不使用高清 EXR，也不把数万颗星保存为数组后逐像素遍历。正式路线是：

```text
归一化世界观察方向
  -> cubemap face / 等面积方向单元
  -> 固定 StarSeed 的整数 Hash
  -> 当前单元和少量相邻单元
  -> 星点存在概率、单元内偏移、角半径、亮度、色温
  -> 解析软圆盘 + fwidth 亚像素覆盖
  -> HDR Emissive × 大气透射/昼夜/高度可见度
```

冻结约束：

- 不读取 `Time`，星点方向恒定；首版不闪烁；
- 星数只改变单元命中概率，不增加逐像素循环次数；禁止“屏幕像素 × 星表数量”的实现；
- 不使用经纬 UV 直接撒点，避免极区密度挤压和接缝；优先六面方向网格，必要时评估八面体映射；
- 使用 `fwidth/ddx/ddy` 或等价解析足迹，给亚像素星点保持能量，防止 TSR、高速相机、PIP 和 Screen Percentage 下闪烁；
- 约 90% 暗星、9% 中星、少于 1% 亮星；只有稀疏亮星进入 Bloom；不生成 Point Light；
- 可追加 50–200 颗美术主星/星座作为小型目录或单网格层，但背景微星仍由程序材质生成；
- `StarSeed`、密度层、角半径、HDR 亮度、冷暖比例、Bloom 门、昼夜和高度阈值均进入 Profile/manifest 身份。

星空可见度统一使用：

```text
AltitudeCM = length(CameraWS - PlanetCenter) - PlanetRadius
RadialUp   = normalize(CameraWS - PlanetCenter)
SunHeight  = dot(RadialUp, SunDirectionToSunWorld)

StarVisibility = NightFactor(SunHeight)
               * AltitudeFactor(AltitudeCM)
               * AtmosphereTransmittance
               * HorizonAirMassAttenuation
```

昼面地表由散射自然压住星点；夜面逐渐显露；地平线因长光程衰减；高空与 `FinaleSpace` 完全显现。主视图、SceneCapture 和 M11 AVI 都以各自 View Direction 计算同一星场，不依赖主相机附着的 Niagara。

### 6.3 捕获命令与当前证据

```powershell
$Editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$Project = 'C:\workspace\AngryBirdsToSpace\AngryBirdsToSpace.uproject'
$BuildId = 'T4A1-Visual-' + (Get-Date -Format 'yyyyMMdd-HHmmss')

& $Editor $Project /Game/Maps/L_ABTS_M11 `
  -game -dx12 -windowed -ForceRes -ResX=1920 -ResY=1080 `
  -ABTSM3R5Preview -ABTSM3R5PreviewCandidate=4 `
  -ABTSM3R31SlotPreviewCandidate=4 `
  -ABTSVisualCaptureSuite=ToonT4A1 -ABTSToonT0Mode=Screenshots `
  "-ABTSToonT0BuildId=$BuildId" `
  -ABTSToonT0ExpectedSeed=312503 -ABTSToonT0WarmupFrames=12 `
  -ABTSToonT0TimeoutSeconds=300 -ABTSToonT0ExitWhenDone
```

`TerminatorSky` 已再次冻结为用户在 PIE 中找到的高敏感度斜向分层点：Location `(7330.839531, -8195.326369, -2132.005920)`，Details Rotation `X/Roll=-87.010167°、Y/Pitch=41.294936°、Z/Yaw=59.050687°`，FOV `52°`。该点长期用于观察暗蓝天空的斜向等值线，不再以旧点或推导点冒充同一诊断身份。

`BrightSkyBanding` 冻结为用户补充的亮处高敏感度点：Location `(5055.549427, -9511.377996, -4376.699690)`，Details Rotation `X/Roll=35.775644°、Y/Pitch=82.960042°、Z/Yaw=151.889451°`，FOV `52°`。该点与 `TerminatorSky` 必须同时通过，禁止只修暗处后宣称天空连续。

`TerminatorSunwardSky` 冻结为晨昏线偏夜侧、几乎正对太阳的点：Location `(8174.509529, -7018.140929, 4234.994413)`，Details Rotation `X/Roll=23.100288°、Y/Pitch=39.625340°、Z/Yaw=161.729072°`，FOV `52°`；其 `Forward·SunDirection=+0.9803`、`SunHeight=-0.2432`。

`TerminatorAntiSunwardSky` 冻结为晨昏线偏昼侧、几乎背对太阳的点：Location `(4378.023910, -6442.873313, 8680.170513)`，Details Rotation `X/Roll=-41.056320°、Y/Pitch=-57.560810°、Z/Yaw=5.318501°`，FOV `52°`；其 `Forward·SunDirection=-0.9773`、`SunHeight=+0.2621`。固定曝光下，前者天空平均亮度必须严格大于后者；这两个点不允许用不同曝光补偿作弊。

2026-08-08 在该精确姿态上完成了同一 Seed、1920×1080、D3D12、Style On 的根因矩阵；每组完整 `ToonT4A1` manifest 均为 `Succeeded`，有效 CVar 由日志回读：

| 实验 | 唯一改动 | 结果 |
| --- | --- | --- |
| `Control` | FastSky LUT 384×208，16–32 样本 | 斜向等值带存在 |
| `HighResolution` | FastSky LUT 提升到 768×416 | 等值带仍存在，未随 LUT texel 尺寸消失 |
| `HighSamples` | FastSky 样本提升到 64–128 | 等值带仍存在，不是瑞利/米氏积分样本数不足 |
| `PerPixel` | `FastSkyLUT=0`，逐像素 64–128 样本 | 等值带仍存在，排除 FastSky LUT 为主因 |
| `ControlNoCloud` | 关闭 `r.VolumetricCloud` | 等值带仍完整存在；云只会叠加低频明暗并提高可见度 |
| `FloatPostNoCloud` | 额外启用 `r.PostProcessing.PropagateAlpha=1`，强制 FloatRGBA 后处理链 | 不消除等值带；相邻完全同色比例反而约为 65.9%，排除默认 R11G11B10 后处理链为主因 |
| `StrongDitherNoCloud` | Tonemapper 按 6-bit 目标施加强诊断抖动 | 相邻完全同色比例由约 47.5% 降至 6.1%，等值带被噪声打散；仅用于证明，不可作为生产值 |
| `Dither8NoCloud` | Tonemapper 按 8-bit 目标施加抖动 | 相邻完全同色比例下降，但修正后的浮点残差仍显示成组斜向等值线；拒绝把统计下降当成视觉通过 |
| `SkyStochasticV13` | 仅天空做稳定随机 8-bit 舍入 | 减弱台阶边界，但原生低频底色中的相干轮廓仍完整存在 |
| `SkyReconstructV14/V15` | 8/32 像素边缘保持低频重建后再量化 | 模糊轮廓而没有消除轮廓，且平坦区域指标恶化；已撤销 |
| `NativeSDR8V16` | BackBuffer 明确改为 8-bit RGBA，不做额外天空重建 | 排除 10-bit→8-bit 身份不一致后，原生暗蓝天空仍有斜向色阶，说明输出位深只是放大因素而非唯一根因 |
| `AnalyticGroundSkyV17` | 连续解析 GroundDay 低频天空；按高亮/低饱和掩码回填原生云/太阳/星点；稳定逐通道 SDR 舍入 | 暗处点改善，但新增亮处点仍出现与原生天空一致的斜向轮廓；掩码回填范围过宽，未触及完整根因 |
| `IsolatedGroundSkyV18` | 解析底色完全隔离原生 SceneColor；太阳与确定性星点独立重建；体积云延后至 A2 | 暗处与亮处原图均连续；两张 float32 高频残差只剩稳定细粒度舍入、地平线与 UI 响应，不再出现重复平行斜带 |
| `ViewAwareTerminatorV19` | 晨昏附近把位置昼夜因子与视线有效太阳高度连续混合；深昼/深夜仍由位置主导 | 朝阳天空均值由 `0.3159` 升至 `0.5881`，背阳由 `0.6167` 降至 `0.2797`；四个天空残差点均无斜带回归 |

根因不是单纯“瑞利/米氏采样数太少”，也不能只归因于 10-bit→8-bit 输出身份。高采样、逐像素与高分辨率 LUT 均未改变轮廓族；把 BackBuffer 明确改为 8-bit 后轮廓仍存在。这说明 UE 原生 Sky Atmosphere 在当前极小主星尺度、暗蓝低梯度和固定曝光组合下已经产生可见的低频等值平台，末端量化只会进一步放大它。体积云改变局部对比度，却不是无云诊断点上色阶的生成源。

正式 v19 仍只替换 `GroundDay` 的天空背景，不改变几何 Tone，也不改变 Satellite/Finale。受控云开/关对照证明 v17 亮处斜带在 `r.VolumetricCloud=0` 时仍保持相同位置和形状；纯解析实验则同时消除了亮、暗两点的斜带，故色阶直接根因是原生整色回填而非云。v18 隔离整色后，两个晨昏方向点又暴露了位置权重压倒视线权重的问题；v19 仅在晨昏带引入视线有效太阳高度，避免反转深昼与深夜。当前 SDR 输出身份在 `DefaultEngine.ini` 固定为 8-bit RGBA；解析天空最后采用逐通道、屏幕稳定的随机舍入。正式截图证据位于 `Saved/ABTSVisualCaptures/ToonT4A1TerminatorDirection/V19ViewAware-20260808/ToonT4A1_Screenshots_20260808T080204Z_18920/`，manifest 为 `Succeeded`、20/20、实现版本 19。

`Tools/Rendering/AnalyzeSkyBandingResidual.py` 必须在 `float32` 亮度上生成高斯低频参考，再只把最终残差可视化量化为 PNG；禁止先把亮度降为 8-bit 后再模糊，因为那会把分析工具自身的台阶误当成天空色阶。正式视觉门是：精确 `TerminatorSky` 和 `BrightSkyBanding` 原图中均没有可见斜向色阶，且 `AnalysisFloat/03_TerminatorSky_02_StyleOn_HighFrequencyResidual.png`、`AnalysisFloat/04_BrightSkyBanding_02_StyleOn_HighFrequencyResidual.png` 中均没有重复、平行、跨大范围延伸的相干轮廓族。相邻同色率和唯一颜色数仅作辅助统计，不能替代该残差目视门。

原五点 GPU 证据位于 `Saved/ABTSVisualCaptures/ToonT4A1/ToonT4A1_GPUProfile_20260807T103856Z_44824/manifest.json`：schema 4、D3D12、RTX 2080、10/10 记录均接受 `ProfileGPU`，每个 Off/On 变体完成 3 个样本。由于 A1 目录现为十点，冻结前须刷新为 20/20；旧证据不冒充新目录的完整 GPU 门槛。

## 7. T4-A2：球面云路线决策

第一候选是原生 Volumetric Cloud 的径向密度材料：

```text
CloudAltitude = length(WorldPosition - PlanetCenter) - PlanetRadius
CloudBand = density between CloudBase and CloudTop
```

只做一层低频主形和一层轻细节，先关闭云影。必须记录 1080p/1440p GPU、TSR 噪点、快速相机稳定性、地面/月面/终局 SceneCapture 和高空球面形状。若 UE 云系统的公里尺度、追踪步长或世界坐标假设在当前小星球上产生不可接受成本/噪点，则正式切换为双层风格化云壳：低频不透明/遮罩云片负责形状，第二壳负责柔边和视差。路线切换必须保留同一中心/半径/高度合同。

## 8. T4-A3 与 T4-B

三套环境 Profile 只读消费同一合同：

- `GroundDay`：完整大气，地表昼夜连续，星空受大气/太阳高度抑制；
- `SatelliteGuide`：弱大气背景、稀疏星场，月面导航和画中画优先可读；
- `FinaleSpace`：完整星场，不使用不相容的地表高度雾/云，行星/UFO 保持轮廓和受控边缘光。

M11 进入/退出、失败黑屏和重置必须保存并恢复环境 Profile，不能只切天空材质。A3 通过后进入 T4-B，重新看 T3 的 Roughness、Specular、Rim、Tint，并用 A0 矩阵关闭 `TOON-T2A-002`。M7 未完成时可以形成“非 M7 T3/T4 联合基线”，但不得宣布完整视觉冻结。

## 9. 验收与性能门

- 数据：快照版本、Profile、Seed、中心、半径、太阳方向和 Hash 合法；多主星/多 index-0 太阳 fail closed；
- 图像：五点连续，夜面非死黑，高空不是裸岩球，星点无明显分辨率纹理感；
- 稳定：50%/75%/100% Screen Percentage、主视图、三类 PIP、Rank11 AVI、高速 Camera Cut 不抖动或丢星；
- Gameplay：Style/环境 Off/On 不改变世界、建筑、卫星、终局和轨迹 Hash；
- GPU：A1 起分别记录 Atmosphere、Star、Cloud 增量；程序星空必须为固定邻域成本，不能随“视觉星数”线性增长；
- 恢复：捕获或终局成功、失败、中断后恢复 Profile、Style、pass mask、shadow quality、暂停和 ViewTarget。

## 10. 排错表

| 现象 | 优先根因 | 处理 |
| --- | --- | --- |
| 环境日志 `Ready=0` | 主星未接受；index-0 Atmosphere Sun 为零或多个 | 核对 Actor 数、合同身份和 Directional Light 标志；禁止选第一个凑数 |
| 同一点矩阵构图变化 | CameraManager cache、Camera Cut 或 Profile 改变了姿态 | 比较 requested/effective Pose Hash；任何差异整组拒收 |
| 关闭阴影后恢复失败 | `r.ShadowQuality` set-by 未保存 | 检查 manifest/终端恢复路径；不得靠重启掩盖 |
| 夜面仍然亮、星星忽明忽暗 | 自动/局部曝光泵动 | A1 固定曝光后重跑，不先提高星点 HDR |
| 星点旋转或接缝 | 使用相机 UV、经纬 UV 或时间噪声 | 改用世界观察方向和固定 Seed 的方向单元 Hash |
| 星点运动闪烁 | 硬阈值单像素点，缺少导数足迹 | 使用解析软圆盘与亚像素能量保持，回归三档 Screen Percentage |
| 高空像裸岩球 | 只有暗天空，没有大气薄层/云/空气透视 | 先完成 A1/A2，不用更高清 EXR 掩盖 |
| 地形粗褶皱仍在 | Tone、Outline、Shadow 尚未隔离或叠加 | 读取六变体矩阵，只在 T4-B 修改被证明的贡献层 |
| 所有网格旁出现同方向浅色复制剪影 | Tone pass 用 SceneColor 输入 UV 直接采 SceneDepth，导致 ViewRect 映射执行两次，几何/天空遮罩在屏幕上错位 | SceneColor 继续用输入 UV；SceneDepth 必须从 `ScreenPosToViewportUV(UVAndScreenPos.zw)` 重建 Viewport UV；主视图和 SceneCapture 共用此路径 |
| 暗蓝天空出现斜向连续等值带，增加 LUT/样本后仍存在 | 10-bit BackBuffer 身份令 Tonemapper 只施加约 `1/1023` 抖动，但 PNG 与常见 SDR 显示链最终按 8-bit 量化；低梯度暗蓝在末端转换时形成等值线。逐像素积分、768×416 LUT、64–128 样本、禁用云和 FloatRGBA 后处理均不能消除，排除瑞利/米氏采样、FastSky LUT、云和 R11G11B10 为主因 | 使用精确 `TerminatorSky` Transform/FOV 重跑；Style On 通过可逆引用计数覆盖使用 `r.BackbufferQuantizationDitheringOverride=8`，最后释放时恢复原值；10-bit/HDR 单独保持自身身份。不得用 6-bit 强抖动作为生产值。无云自动截图已通过，仍需可见 PIE 检查运动稳定性 |

## 11. 所有权与交接

环境合同、捕获探针、共享渲染控制、Config、共同地图和共享天空/大气资产由 Integration 唯一修改。M3 只继续提供主星几何/已接受世界；M11 只发布终局阶段和自有 SceneCapture 语义；M7 不承担 A0–A3。任何需要 M3/M11 新字段的需求先走稳定快照/消费接口，不允许环境系统读取功能工作树私有数组。
