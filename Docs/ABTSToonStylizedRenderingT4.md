# ABTS 三渲二 T4：球面环境、光照、云雾与程序化星空

> 状态：2026-08-12 更新。T4-A0/A1 与 T4-A2.1～A2.4 均为 `IntegrationAccepted`，A2 已冻结；历史版本、云场参数和证据保留在第 7 节。**T4-A3.1 高空连续星空过渡**实现版本 64 已通过自动化、真实 D3D12 捕获和实际飞行 PIE。当前进入 **T4-A3.2 三套环境 Profile 正式装配**：实现版本 65 将表面 Tone/Outline Profile 与环境背景 Profile 分离，普通主世界正式消费 `GroundDay`，月面画中画以 `GroundDay` 表面视觉消费 `SatelliteGuide` 深空背景，M11 终局活动期间主世界自动消费 `FinaleSpace` 并在活动结束后回到配置基线。A3.2 代码与自动化完成后仍需可见 PIE；A3.3 M11 环境快照/异常中断恢复和 T4-B 尚未开始。
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
| **T4-A2 球面云** | **A2.1 云岛形态与表面基线** → **A2.2 全球云场与融合语义** → **A2.3 有界穿云表现** → **A2.4 消费端与性能冻结** | 不先开真实云影；不保留原生体积云与风格云双重消费；不以自动化绿灯覆盖可见 PIE 拒绝 | A2.1～A2.3 已通过；A2.4 完成 PIP/AVI/时域/GPU 与最终内容密度门后，T4-A2 才能冻结 |
| **T4-A3 环境 Profile** | **A3.1 高空连续星空过渡** → **A3.2 三套环境 Profile 正式装配** → **A3.3 M11 环境快照与异常恢复** | 不改变 M11 求解/轨迹；A3.1 不以高度硬切 Profile；A3.2 不把月面表面改成独立补光 | 五高度连续；普通世界/月面 PIP/终局主视图与 AVI 消费正确 Profile；退出、失败、中断后恢复一致 |
| **T4-B T3/T4 联合校色** | 回开并调整 Roughness、Specular、Rim、Tint；解决地形褶皱；形成非 M7 联合基线 | M7 未完成时不宣称全项目冻结 | `TOON-T2A-002` 关闭证据；T3/T4 视觉和 GPU 联合基线；M7 后补建筑材质/特效 |

阶段必须按 A0→A1→A2→A3→B 前进。A2 的正式编号只使用 **A2.1→A2.2→A2.3→A2.4**，四项现已全部验收并冻结；`R0/R1-A/R1-C2-A4/B3B6/v44` 等只用于说明 A2.1 的技术演进、资产合同和日志身份。A3 的正式编号为 **A3.1→A3.2→A3.3**，当前推进 A3.2。不得用任一中间阶段静态截图或自动化绿灯替代后续门槛。

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
- 星场由 `ABTSStylizedStarMainPS` 在 HDR 后处理阶段按世界观察方向生成。固定 `StarSeed`、六面方向网格、每像素固定 3×3 相邻单元、整数 Hash 和 `fwidth` 软圆盘保证成本不随可见星数线性增加；v67 将基础角半径由 `0.055` 提高到 `0.120`，并让稀疏中星/亮星获得 `1.50/2.50` 倍解析足迹，GroundDay/SatelliteGuide/FinaleSpace 的 HDR 强度分别冻结为 `2.6/3.0/3.6`，以保证 1080p 高空主视图经 TSR 与 SDR 输出后仍可读，同时不改变 Seed 或方向分布；v68 让 GroundDay 晨昏星场复用连续天空的视线有效太阳高度，v69 同时放宽天空地平线的连续可见带：朝阳视线抑制星点、水平背阳视线显示星点，地表以下仍由 SceneDepth 与消隐带共同拒绝；深昼/深夜以及高空和 FinaleSpace 的完整星场不受该方向门控削弱；
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

## 7. T4-A2：低模球面云排期与路线决策

### 7.1 已退役路线

原生 `VolumetricCloud` 的地球尺度材质在当前约 100 m 主星上没有可用参数窗口：公开 ViewState 云纹理和透射率读取无误，但密度较低时完全透明，按薄壳光程补偿后立即饱和为均匀灰幕。v24 的 `StylizedDualRadialShell` 虽能在地面得到局部云形，却仍是包裹整颗主星的全屏背景算法；云上视角会读成连续壳层，不能提供有限云团的侧面、顶部与穿越体积，也无法支持本游戏高频弹弓飞越云层。两条路线均保留为历史证据，不再作为生产候选。

### 7.2 统一编号与冻结排期

排期编号只表达可独立验收的交付物；实现路线和版本号只表达内部技术身份：

- 排期：`T4-A2.1`～`T4-A2.4`；用于工作流、任务交接和验收结论；
- 技术历史：`R0/R1-A/R1-C2-A4/B3B6`；只在本详稿的演进记录和日志中保留；
- 实现版本：`v25`～`v47`；只用于 manifest、自动化与回归定位。

不得再把内部试验名拼接成 `B3-B6-C` 一类新排期。旧文档中的 `B3-C` 内容统一归入 A2.2，`R1-D` 统一归入 A2.3，`B3-D/R2` 统一归入 A2.4。

| 子阶段 | 实现范围 | 明确不做 | 退出门 |
| --- | --- | --- | --- |
| **T4-A2.1 云岛形态与表面基线** | 三座确定性球面云岛；252 个 HISM 云滴、18 个 Seed 驱动宏簇；专用低模网格/Unlit 材质；内部描边抑制；视角无关共享体场、向光/薄层变白和连续云底 | 不做多云团分组、相机入云效果、最终消费端/GPU 冻结 | **`IntegrationAccepted`**：ForceUnity、fresh Toon 24/24、资产只读验证、D3D12 17 点/34 条及用户 PIE 均通过；当前实现路线 `InstancedCloudletsR1C2B3B6`、版本 44 |
| **T4-A2.2 全球云场与融合语义** | 保留 24 朵太阳无关全球背景逻辑云；追加 7 朵太阳相对、横跨晨昏线且联合角包络 27–33° 的连通超大云簇；所有云共享单一 `CloudComposite` stencil；按每像素局部太阳高度连续门控夜面直射/薄层增白 | 不给每朵云分配独立 stencil；不保留云—云内部描边；不以云编号切换光照，也不回退 A2.1 单云体场 | **`IntegrationAccepted`**：实现版本 47、材质宏合同 8；ForceUnity、fresh Toon 25/25、资产只读验证、D3D12 22 点/44 条、manifest schema 7 及用户可见 PIE 均已通过；夜面亮度、晨昏超大云簇融合与运动稳定性已验收 |
| **T4-A2.3 有界穿云表现** | A2.3 建立镜头球、逐鸟可见网格球和镜头—鸟群有限走廊；A2.3.1 在硬保护核心外围使用云岛局部二维噪声；全屏薄云雾幕已永久删除；GroundDay 云场禁用 Motion Blur 保持高速轮廓清晰 | 不恢复全屏球壳或全屏雾幕；不让相机参数进入云光照；不把全部云改为 Translucent；不让云改变轨迹、碰撞或 WorldReady | **`IntegrationAccepted`**：实现版本 54、材质宏合同 11、manifest schema 12；ForceUnity、资产验证、fresh Toon 26/26、真实 D3D12 26 点/52 条和用户连续运动 PIE 均已通过；逐鸟可见、无雾幕闪烁、无夜云动态亮边 |
| **T4-A2.4 消费端与性能冻结** | 最终内容密度调参（提高超大型云簇占比并冻结数量/尺度分布）；主视图、地面/月面/终局 PIP、Rank11 AVI；50/75/100% SP、1080p/1440p、快速相机、时域稳定性和 GPU 增量 | 不回调 T3 材质和最终光照；不以单一截图代替消费端矩阵；不在缺少性能证据时直接复制当前诊断云簇 | 调参后全球分布、云簇融合、视觉、时域、SceneCapture/AVI 与 GPU 门全部通过；A2.1～A2.3 无回归后才宣布 T4-A2 冻结 |

### 7.3 A2.1 技术演进：R0 有限云岛（版本 25）

R0 继续消费 A0 的 `PlanetCenterWorld`、`PlanetRadiusCM`、`SunDirectionToSunWorld`、`CloudBaseAltitudeCM` 与 `CloudLayerHeightCM`。纯 C++ 布局器在昼面固定三个径向中心并建立局部切平面；云岛 Actor 以球心为原点生成 transient `UProceduralMeshComponent`，不写地图或资产。三个岛的位置、尺度、Seed、布局 Hash 与几何 Hash 均由合同确定；同一环境快照重复进入不得漂移。

每个云岛由五个相交椭球叶瓣组成。每个叶瓣使用低段数三角网格，逻辑边必须恰好被两个三角形消费；渲染网格为保持硬面而拆分顶点。法线以局部径向 Up 为主、面法线为辅，避免单一 Directional Light 把封闭云底压成近黑岩石，同时保留轻微低模明暗层级。R0 使用 Engine 不透明基材，只是几何和观察方向原型；专用云母材质及穿云内部雾必须在 R1 完成，不能把 R0 外观称为最终卡通云。

仅 `GroundDay && Style On && CloudsEnabled` 创建云岛。地图中唯一原生 `VolumetricCloud` 在此期间只被可逆隐藏；Style Off、Satellite、Finale、失败或子系统退出时销毁 transient Actor，并恢复原生组件可见性和原字段。云岛关闭 Collision、Overlap、Navigation、Static/Dynamic Shadow，不改变 Gameplay、WorldReady 或物理 Hash。

`ToonT4A2` 保留 A1 十个精确姿态，并增加：

| ID | 观察目的 |
| --- | --- |
| `CloudR0Ground` | 从地表确认云岛不是全屏灰幕，蓝天空隙仍存在 |
| `CloudR0Side` | 从侧向确认有限体积、低模轮廓与相邻叶瓣关系 |
| `CloudR0SideOrthogonal` | 与 `CloudR0Side` 相差 90°，防止单一长轴云带通过固定侧视验收 |
| `CloudR0Above` | 从云上确认不会看到包裹主星的连续云壳 |
| `CloudR0FlyThrough` | 固定入云方向与构图身份；R0 只看几何入口，R1 才验内部雾和时间连续性 |
| `CloudR0GroundObliqueUp` | 以地面高度从侧下方仰视完整云块，优先判断云底、主体、云顶是否仍读作同一均匀色块 |
| `CloudR0GroundZenith` | 以地面高度正上仰视云底，优先判断云底连续层次、接缝与视角相关整体跳亮 |
| `CloudFieldGlobal` | 从太空读取当前半球的全球覆盖，确认云场不是三个孤立样例或规则点阵 |
| `CloudFieldFusion` | 沿最近邻逻辑云对的切向轴观察投影融合，确认二者读作一个连续天气簇且不出现编号接缝 |
| `CloudFieldVariety` | 同屏读取邻近但面积差异最大的云对，确认大小、厚度与轮廓具有可见变化 |
| `CloudFieldNight` | 读取深夜面背景云，确认局部太阳高度连续压低直射与薄层增白，云仍保留冷灰蓝层次而不是漂浮白块 |
| `CloudFieldTerminatorMega` | 读取横跨晨昏线的连通超大云簇，确认约 30° 联合包络、云—云融合和同一云簇内部连续昼夜过渡 |

当前目录为 22 个点，分别捕获 Style Off/On，共 44 条记录。manifest schema 7 在 schema 6 的逻辑云字段上追加全球背景云数、晨昏超大云簇云数、联合角包络/连通性、局部太阳高度光照、夜面增白门控、夜面亮度与昼夜混合区间。R0–A2.1 历史证据仍保留各自当时的点数与 schema；实现版本 46 的 schema 6、20 点/40 条证据也继续作为 A2.2 前一轮基线，不回写历史结果。命令入口：

```powershell
$BuildId = 'T4A2-Visual-' + (Get-Date -Format 'yyyyMMdd-HHmmss')
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  'C:\workspace\AngryBirdsToSpace\AngryBirdsToSpace.uproject' `
  '/Game/Maps/L_ABTS_M11' -game -windowed -ResX=1920 -ResY=1080 `
  -ForceRes -NoSplash -NoLoadingScreen -NoSound -NoMessaging `
  -ABTSVisualCaptureSuite=ToonT4A2 -ABTSToonT0Mode=Screenshots `
  "-ABTSToonT0BuildId=$BuildId" -ABTSToonT0ExitWhenDone
```

R0 当前证据为 UE 5.8 ForceUnity 构建、fresh `ABTS.Rendering.Toon` 20/20，以及命令行 DX12 的 28/28 manifest。最新截图目录：`Saved/ABTSVisualCaptures/ToonT4A2R0/T4A2R0-Final-R2-20260809-203210/ToonT4A2_Screenshots_20260809T123235Z_19368`；当前径向法线偏置已消除云底近黑，但不透明基础材质仍只代表 R0 几何原型。该证据只证明有限几何路线成立；地面 PIP、SceneCapture、时域、TSR、GPU 和可见 PIE 后来分别进入 A2.1 演进与 A2.4 正式门，R0 不据此关闭 A2。

### 7.4 A2.1 技术演进：R1-A 实例层（版本 26）

R1 采用“许多共享低模云滴组成有限云团”的路线。参考方案以 GPU 实例、小球矩阵、顶点 Noise 和伪阴影构造可控云形；本项目吸收其表示思想，但不照搬“数千个 CPU 每帧更新的球”。UE 实现优先使用 `UHierarchicalInstancedStaticMeshComponent`：每座云岛一个组件，共享同一网格与材质；静态实例矩阵只在环境快照或布局 Hash 改变时重建，整座云的运动以后只改根 Actor。首版固定 96/72/84，共 252 个实例，先验证表示和生命周期，再以 GPU 数据决定是否扩到 600–1200。参考说明：[程序化 GPU 实例云方案](https://www.bilibili.com/video/BV1xNjt6hEMw/)。

R1-A 继续使用 R0 的三座球面径向云岛和 Seed。每座岛通过低差异角序列、椭圆范围与确定性边界扰动生成 2.5D 云滴：下部相对平、中心较高且较大、边缘较小。运行时暂用 Engine 共享 Sphere 只是实例合同占位，R1-B 必须替换为专用低面数、不规则云滴网格和材质。每实例冻结四路数据：`Seed01`、`NormalizedHeight`、`FakeOcclusion`、`SizeTier`；这四路现在只建立数据通道，不允许基础材质假装已消费。

R1-A 退出门只包含：三岛/252 实例数量、重复生成 Hash、四路数据范围、无 Collision/Navigation/Shadow、Style/Profile 恢复、ForceUnity 与 fresh `ABTS.Rendering.Toon`。截图只检查云岛仍为有限体积且上下视角没有全局壳；规则球缝、缺少噪声、缺少自阴影属于 R1-B/C 的已知开放项，不作为 R1-A 失败或完成最终美术的依据。

R1-A 当前代码证据已形成：UE 5.8 ForceUnity 成功；fresh `ABTS.Rendering.Toon` 为 21/21，日志 `Saved/Logs/T4A2R1A-Toon-20260809-210124-FreshAutomation.log`；隐藏 D3D12 `ToonT4A2` manifest 为 `Succeeded`、28/28、版本 26、`cloudRoute=InstancedCloudletsR1A`，目录 `Saved/ABTSVisualCaptures/ToonT4A2R1A/ToonT4A2_Screenshots_20260809T130439Z_66116`。运行日志冻结 `Islands=3 / Cloudlets=252 / CustomDataFloats=4 / LayoutHash=12476466087894278273`。四个云诊断点确认云团是有界实例集合且没有回归全局壳；同时清楚暴露规则球、硬明暗半球、内部描边和上下视角空隙，这些是 R1-B/C 的输入证据，不是调 R1-A 实例数来掩盖的问题。R1-A 尚待用户决定是否需要单独 PIE 验收；当前自动证据不替代最终云视觉验收。

### 7.5 A2.1 技术演进：R1-B 云滴材质（版本 27）

R1-B 不改变 R1-A 的三岛、252 实例、四路实例数据或生命周期，只替换视觉消费端。Integration 以 `Tools/Rendering/GenerateT4A2CloudAssets.py` 在 UE 5.8 命令行中确定性生成 `/Game/Toon/Environment/Cloud/M_ABTS_Toon_Cloudlet` 与 `SM_ABTS_Toon_Cloudlet`；无 `ABTS_T4A2_REBUILD=1` 时脚本只读验证且不得重写资产。网格是项目自有的共享云滴资产，正负 WPO Bounds 各扩展 30 cm；材质固定 Opaque/Unlit、StaticMesh/HISM Usage，并消费 `Seed01 / NormalizedHeight / FakeOcclusion / SizeTier` 四路实例数据。顶点阶段以世界位置、Seed 和 SizeTier 驱动连续 Noise 沿世界法线扰动；像素阶段用世界法线、权威太阳方向、高度抬升和伪遮蔽在冷色亮/暗两档间连续插值。云不再接收 UE 方向光硬阴影，避免受光球体的黑白半球，但太阳方向仍随环境快照更新。

R1-B 的退出门是“专用资产真实消费且没有退回默认材质”，不是最终云团美术冻结：资产生成/验证幂等；缺网格、缺材质或 MID 创建失败时整套 transient 云 fail closed；ForceUnity 通过；fresh `ABTS.Rendering.Toon` 含 R1-B 资产合同；真实 D3D12 四个云点中硬黑半球消失，近景外轮廓可见 Noise 扰动。全局描边仍会逐个勾出相交云滴，因此上视图像圆石堆、内部线条过密是 R1-C 的正式输入，不允许在 R1-B 通过增加实例数或把轮廓整体关掉掩盖。

当前证据：资产重建日志 `Saved/Logs/T4A2R1B-GenerateAssets-20260809-212229.log`；普通无重写验证日志 `Saved/Logs/T4A2R1B-ValidateAssets-20260809-212703.log`；ForceUnity 日志 `Saved/Logs/T4A2R1B-ForceUnity-20260809-212743.log`；fresh `ABTS.Rendering.Toon` 为 22/22，日志 `Saved/Logs/T4A2R1B-Toon-20260809-212816-FreshAutomation.log`。隐藏 D3D12 manifest 为 `Succeeded`、28/28、实现版本 27、全部记录 `cloudRoute=InstancedCloudletsR1B`，目录 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260809T133015Z_66060`。四图确认有限云岛和蓝天空隙保持，硬明暗半球已消失，近景轮廓已被 WPO 打散；内部云滴描边与俯视圆石感继续开放给 R1-C。R1-B 自动证据不替代后续可见 PIE、PIP、时域和 GPU 门。

### 7.6 A2.1 技术演进：R1-C 内部轮廓语义（版本 28）

R1-C 不合并几何，也不关闭云的全局描边。三座 transient 云岛在写入 Custom Depth 时统一使用 Integration 保留值 `8`；现有鸟、建筑、地形和交互对象的选择性语义仍只使用 `1..7`。Tone/Outline pass 先以 SceneDepth 与 CustomDepth 一致性取得当前可见表面的 stencil，只有中心像素和邻域样本的可见 stencil **同时为 8** 时，才把这条深度/法线边视为同一云团内部接缝并抑制。云对天空、地面、主星、鸟或其他非云对象的边界仍沿用普通轮廓，因此不会把云整体抹成无边界白块，也不会改变玩法对象的选择性描边。

该规则只解决“相交云滴被逐个套黑线”的合成问题。俯视时若两个云滴之间真实露出地面或天空，它们各自仍拥有合法外轮廓；这些空隙属于实例布局和后续美术形态问题，不应通过扩大 stencil 深度容差或无条件关闭云轮廓掩盖。R1-C 也不添加相机内雾、半透明壳或全屏灰幕，穿云内部表现现统一由 T4-A2.3 负责。

当前证据：ForceUnity 日志 `Saved/Logs/T4A2R1C-ForceUnity-20260810-103012.log`；fresh `ABTS.Rendering.Toon` 为 23/23，日志 `Saved/Logs/T4A2R1C-Toon-20260810-103220.log`，其中 `CloudCompositeStencilContract` 验证值 8 不与 `1..7` 相撞、只抑制 `8/8` 边。隐藏 D3D12 manifest 为 `Succeeded`、28/28、实现版本 28、全部记录 `cloudRoute=InstancedCloudletsR1C`、`cloudCompositeStencilValue=8`、`cloudInternalOutlineSuppression=true`，目录 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260810T023343Z_33084`。侧视和穿云点确认相交云滴之间的黑色石堆线已消失，云团对蓝天的轮廓仍存在；俯视点保留真实分离云滴的外边界。该自动像素证据仍不替代用户可见 PIE、A2.3 穿云与 A2.4 消费端/性能门。

### 7.7 A2.1 技术演进：R1-C2 形态与云面聚合

R1-C2-A 保留 R1-C 的 stencil 8、R1-B 资产、四路 Custom Data、三岛和 252 总预算，并建立 Body/Crown/Edge 三层数据角色。它的 ForceUnity、fresh Toon 24/24 和隐藏 D3D12 28/28 均通过，但验收目标错误地要求 Body 覆盖完整归一化椭圆核心 98%。用户可见 PIE 因而读到整块实心凸云板；同时所有实例共用一个切平面，低空侧视形成云墙，规则椭球方向产生平行肋纹，环带 Edge 又留下孤立小球。故 R1-C2-A 只保留为数据预演，**视觉状态为 Rejected**，不得直接进入 B。

R1-C2-A2 修正覆盖对象而不提高实例数：每岛由 5 个确定性椭圆宏簇组成，宏簇彼此连接但联合边界非凸，并在原包围椭圆中保留明确负空间。Body 只需覆盖各宏簇半径 0.76 的核心 `>=98%`；Crown 在簇内按高度偏置形成不等高隆起；Edge 中心落在簇内轮廓带且必须与同簇 Body 解析相交，禁止卫星式孤立云滴。每个实例的切向偏移换算为球面圆弧，位置、局部 Z 与旋转轴均消费该点径向 Up。三岛水平尺度缩小、云底抬高、厚度降低，以避免百米主星上的低空巨大墙体。宏簇身份、球面位置和层身份全部进入 Hash。

R1-C2-A2 当前自动证据已形成：实现版本 30、路线 `InstancedCloudletsR1C2A2`；UE 5.8 ForceUnity 日志为 `Saved/Logs/T4A2R1C2A2-ForceUnity-20260810-113238.log`；fresh `ABTS.Rendering.Toon` 为 24/24，日志 `Saved/Logs/T4A2R1C2A2-Toon-20260810-113256.log`。隐藏 D3D12 manifest 为 `Succeeded`、28/28，目录 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260810T033427Z_34208`；记录 15 宏簇、球面贴合开启、Detached Edge 最大值 0，运行 Hash 为 `5747631784136716594`。四个 Style On 云点确认卫星式小球消失、侧视云体抬离地表、俯视云面连续且轮廓保留凹凸。近距离 `CloudR0FlyThrough` 仍可读到 Body 层和逐云滴明暗，前者需在可见 PIE 判断是否仍像云板，后者正式留给 R1-C2-B；当前自动截图不替代用户 PIE。

R1-C2-A2 的后续正交方位 PIE 显示：云岛包络 X/Y、宏簇中心链、簇椭圆和 Edge 切向拉伸共享同一主轴，方向性被逐层放大；宽侧面可读，端面却成为狭长叶片。这不是材质或相机问题，而是布局合同缺少方位不变量。R1-C2-A3 因而保持 A2 的非凸、球面、附着和预算合同，只把水平包络改为近等尺度，将宏簇改为中心加四方位的不规则组合，并按每岛 Seed 整体旋转；自动化直接扫描实际云滴椭圆联合体的 24 个水平投影，不再以作者 Extents 冒充最终形态。版本 31 路线为 `InstancedCloudletsR1C2A3`。UE 5.8 ForceUnity 已通过，日志 `Saved/Logs/T4A2R1C2A3-ForceUnity-20260810-120255.log`；fresh `ABTS.Rendering.Toon` 为 24/24，日志 `Saved/Logs/T4A2R1C2A3-Toon-20260810-120340-FreshAutomation.log`。A3 消除了叶片端面，却由近等半径的中心加四象限固定结构产生了方形/十字轮廓，说明“方位均衡”不能等同于“人工对称”。

R1-C2-A4 保留 A3 的包络、球面贴合、实例预算和方位门，将每岛拓扑改为一个轻微偏心核心加五个 Seed 驱动外瓣。五瓣只用等角骨架防止全部随机落在同一侧，随后分别随机角度扰动、离心距离、等效半径、长宽比、局部朝向和高度；外瓣半径下限及离心距离上限受控，以免随机不定形重新退化为窄云带。实现版本 32，路线 `InstancedCloudletsR1C2A4`，三岛共 18 宏簇。UE 5.8 ForceUnity 日志为 `Saved/Logs/T4A2R1C2A4-ForceUnity-20260810-123219.log`；定向合同日志为 `Saved/Logs/T4A2R1C2A4-Targeted-20260810-123219.log`；fresh `ABTS.Rendering.Toon` 为 24/24，日志 `Saved/Logs/T4A2R1C2A4-Toon-20260810-123322-FreshAutomation.log`。后续用户可见 PIE 已确认随机不定形轮廓不再呈正方形、十字或规则多边形；该成果已经并入 A2.1 最终基线，不再作为独立待验收阶段。

R1-C2-B 在 A4 可见形态通过后建立云岛级连续着色坐标。每座 HISM 的 MID 消费中心、径向 Up、两个切向轴、范围和六个宏簇；第五路实例数据只提供 Body/Crown/Edge 层身份。像素先把六个宏簇解析为连续云顶高度场并对高度场求导，得到跨云滴共享的宏法线；宏簇高度、主导度和交叠谷继续生成低频明暗与连续遮蔽。局部 `VertexNormalWS` 只按 Body/Crown/Edge 权重 `0.18/0.30/0.36` 混入，宏法线强度为 `0.84`，逐实例伪遮蔽仅保留 `0.045` 的轻微变化。这样明显的球面接缝不再承担层次表达，云顶、主体和边缘仍可由共享场读出；后续 A2.3 不得用雾效遮盖法线问题。

实现版本为 33，路线 `InstancedCloudletsR1C2B`，三岛仍为 18 宏簇、252 云滴和 stencil 8，布局 Hash 保持 `3043688260911877677`。资产必须由 `Tools/Rendering/GenerateT4A2CloudAssets.py` 在 `ABTS_T4A2_REBUILD=1` 时重建；普通运行只读验证。当前证据：资产重建日志 `Saved/Logs/T4A2R1C2B-GenerateAssets-20260810-134357-Rebuild.log`；ForceUnity 日志 `Saved/Logs/T4A2R1C2B-ForceUnity-20260810-133600.log`；fresh `ABTS.Rendering.Toon` 为 24/24，日志 `Saved/Logs/T4A2R1C2B-Toon-20260810-135001-FinalAutomation.log`。真实 D3D12 manifest 为 `Succeeded`、30/30，目录 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260810T054507Z_68612`；日志无云材质 shader 编译错误。`CloudR0Side`、`CloudR0SideOrthogonal`、`CloudR0Above`、`CloudR0FlyThrough` 显示逐云滴明暗边界已收敛为连续浅蓝低频起伏，同时保留云团外轮廓。可见 PIE 仍须绕云横向移动，确认接缝不会随遮挡顺序重新跳变；该门通过前不得进入视觉冻结。

用户可见 PIE 随后否定了版本 33 的“已无接缝”假设：截图左侧仍能看到沿云滴轮廓碎裂的浅蓝边界。根因不是共享宏场本身，而是像素端继续消费 `VertexNormalWS`、Body/Crown/Edge 层权重、实例高度与 `FakeOcclusion`；不透明云滴切换最前表面时，这些值不可连续。R1-C2-B2 因而不是继续降低权重，而是把所有逐实例量完全移出像素亮度。五路 Custom Data 继续服务 WPO、布局身份和未来合同，但颜色只使用六宏簇共享二维密度、解析云顶梯度、宏簇高度/主导度/交叠谷与权威太阳方向；采样坐标取相机射线在云岛中心深度的公共点，因此同一像素即使切换最前云滴也读取同一个宏场坐标。版本 34 路线为 `InstancedCloudletsR1C2B2`，材质版本为 2；manifest 必须明确记录 `cloudPixelLocalNormalWeight=0` 与 `cloudPixelInstanceVariation=0`。验收仍要求外轮廓完整、云顶大尺度层次可读，并在侧视、正交侧视、俯视、穿越和绕云 PIE 中不再出现跟随单个云滴轮廓的碎裂亮暗边界。

R1-C2-B2 当前代码与资产门已完成：最终资产重建日志为 `Saved/Logs/T4A2R1C2B2-GenerateAssets-20260810-141651-FinalRebuild.log`，确认 `ContinuousPlanarLighting=1 / PixelLocalNormalWeight=0 / PixelInstanceVariation=0`；UE 5.8 ForceUnity 日志为 `Saved/Logs/T4A2R1C2B2-ForceUnity-20260810-141345.log`；最终 fresh `ABTS.Rendering.Toon` 为 24/24，日志 `Saved/Logs/T4A2R1C2B2-Toon-20260810-141732-FinalAutomation.log`。用户 PIE 随后确认碎裂边界消失，但整体退化为均匀白块，因此 B2 作为“接缝归零”诊断成立，视觉不接受。

### 7.8 A2.1 技术演进：连续云体着色与最终基线

R1-C2-B3 不把逐实例法线、Layer、高度或 FakeOcclusion 放回像素端。HISM 云滴只负责不规则外轮廓、深度覆盖和 WPO；每岛六个确定性宏簇同时定义一个共享三维体场，所有相交云滴在同一相机射线上必须读取同一虚拟表面、体场梯度和光学厚度，从结构上兼容“无碎裂接缝”和“可读云体层次”。

下表仅记录 A2.1 内部被验证或否决的着色方案，不是后续排期：

| 历史路线 | 内容 | 结论 |
| --- | --- | --- |
| **B3-A 体场与表面** | 对六个定向椭球做解析射线相交，以软联合得到统一虚拟表面；三维高斯联合梯度生成共享法线，射线弦长形成连续光学厚度 | **Rejected**：实现版本 35 虽去除碎裂接缝，但相机射线参与光照权威，绕云时出现整团 brightness pumping |
| **B3-B 调色及后续修正** | 改用固定岛坐标的视角无关体场，增加向光/薄层变白，并以连续云底场消除六瓣放射纹 | v37～v44 最终收敛为 A2.1 当前基线；实现版本 44 已通过用户 PIE，不再拆成新的排期子阶段 |

B3-A 使用六个宏椭球的解析二次方程，不做固定步数 Ray March；每像素成本与六宏簇数线性相关。软联合只混合邻近的命中深度，避免宏簇切换形成新的硬缝。体场法线来自联合密度梯度，连续低频细节也只使用岛局部坐标；像素端不得采样 PerInstanceCustomData。多云团身份当时未进入本轮，现统一由 T4-A2.2 单独验收。

B3-A 当前代码、资产和数据门已完成：材质重建日志为 `Saved/Logs/T4A2R1C2B3-GenerateAssets-20260810-145832.log`；UE 5.8 ForceUnity 独立证据为 `Saved/Logs/T4A2R1C2B3-ForceUnity-20260810-150352.log`；fresh `ABTS.Rendering.Toon` 为 24/24，日志 `Saved/Logs/T4A2R1C2B3-Toon-20260810-150203-FreshAutomation.log`。当前实现版本 35、材质版本 3，manifest 路线为 `InstancedCloudletsR1C2B3`。NullRHI 不编译最终像素 shader，也不能判断三段色是否真正可读；真实 RHI shader 扫描和用户侧视/正交侧视/俯视/近距 PIE 是 B3-A 尚未完成的视觉门。

用户可见 PIE 拒绝了 B3-A：云体仍接近整体均匀融合，且绕云移动时整团明暗随观察方向变化。根因是“统一虚拟表面”本身由 `CameraPos + ViewRay` 与六椭球解析命中构造；相机一动，软联合命中深度、体法线和弦长会同时改变。它虽然不读取单颗云滴法线，却仍把观察者写进了光照权威，因此不是稳定的世界空间云体。

**B3-A2 视角无关修正**保留三岛形态、六宏簇、252 实例、WPO 和 stencil 8，只替换像素着色。版本 36 首轮虽然删除了 `CameraPos`、View Ray、解析命中、视角 Rim 和射线光学厚度，但二维平面高度项仍把侧视压成水平色带，通用物体 Tone 又把云材质的窄亮度范围归并为单一高光档，因此真实截图仍接近均匀白块。

当前版本 37、材质版本 5 改为在固定云岛坐标中计算六个三维宏簇的连续密度梯度：`WorldPos` 只决定世界空间采样点，体梯度、宏簇高度、交叠谷和低频细节共同生成云顶/主体/云底；云 composite stencil `8` 在 Tone pass 中保留材质已经生成的连续卡通层次，不再二次套用通用物体亮度量化，Outline pass 仍保留云—天空外轮廓。像素颜色继续禁止逐实例法线、Layer、高度和 FakeOcclusion，因此不会为了恢复层次重新引入碎裂接缝；颜色与光照路径也不包含任何相机输入。

最终代码证据为 UE 5.8 ForceUnity `Saved/Logs/T4A2R1C2B3A2V37-ForceUnity-R3-20260810.log`，fresh `ABTS.Rendering.Toon` 24/24 为 `Saved/Logs/T4A2R1C2B3A2V37-Toon-FreshAutomation-R3-20260810.log`，真实 D3D12 为 `Saved/Logs/T4A2R1C2B3A2V37-Capture-R3-20260810.log`。截图与 manifest 位于 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260810T084323Z_68892`，共 17 点/34 条，`cloudCameraDependentLighting=false`、`cloudViewInvariantVolumeGradient=true`、`cloudBypassGenericObjectTone=true`，且日志无云材质编译失败或 fallback。静态证据确认两个正交侧视、俯视、穿越、地面侧上仰视和正上仰视均已恢复连续低频层次；最终仍需用户在 PIE 中绕云移动，确认不存在整团 brightness pumping。

用户 PIE 已确认 v37 的整团忽明忽暗消失，但中部色带仍偏均匀灰蓝，向光面与薄层和主体的明度差不足。B3-B v38 因而不整体提高 Emissive：`SunWhite` 由共享宏体法线与太阳方向生成，`ThinWhite` 由共享三维密度的低密度端生成，二者再独立混入近中性的 `LightColor`。暗面薄层只保留较弱透亮，主体与阴影继续使用冷灰蓝色带。该路径禁止 `CameraPos`、View Ray、Fresnel、逐实例法线、高度与 FakeOcclusion，避免调色重新引入 brightness pumping 或碎裂接缝。材质版本升为 6，实现版本升为 38；manifest 固定 `cloudSunwardWhitening=true`、`cloudThinDensityWhitening=true` 与 `cloudViewIndependentWhitening=true`。

v38 代码与静态视觉门已完成：资产重建日志为 `Saved/Logs/T4A2R1C2B3BV38-GenerateAssets-20260810-174137.log`，无重写验证为 `Saved/Logs/T4A2R1C2B3BV38-Validate-NoRewrite-20260810-174853.log`，UE 5.8 ForceUnity 为 `Saved/Logs/T4A2R1C2B3BV38-ForceUnity-20260810-174216.log`，fresh `ABTS.Rendering.Toon` 为 24/24，日志 `Saved/Logs/T4A2R1C2B3BV38-Toon-FreshAutomation-20260810-174254.log`。真实 D3D12 17 点/34 条 manifest 为 `Succeeded`，目录 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260810T094420Z_48316`，云材质无编译/fallback。相对 v37，地面侧上仰视的云像素 5%–95% 明度跨度由 `0.157` 增至 `0.224`，正上仰视由 `0.053` 增至 `0.132`，云上仍保留 `0.176`；两个远侧视主要读取受光面，整体趋白且跨度收窄。最终仍由用户绕云 PIE 判断远侧是否过白、向光/薄层对比和运动稳定性。

用户在 v38 的地面正上仰视图中发现了从云底中心向外发散的六瓣放射纹。v39–v43 依次验证了梯度置信度、宏簇交界门、核心体场闭合以及二维核心权重；这些实验只能改变放射纹强弱，无法改变其拓扑，说明问题不来自色阶精度，也不是普通法线接缝。根因是六个宏簇的三维深度场仍在云滴不透明前表面切换处被重复投影：正下方观察时，多个云滴的局部 `P.z` 和密度响应共同把六簇作者结构显露成中心辐射图案。

最终 v44 路线 `InstancedCloudletsR1C2B3B6` 为云底引入**视角无关的连续底面场**：只按固定云岛坐标中的局部高度生成 `UndersideBlend`，在云底逐渐压低宏簇交界项，并把密度、垂直层和直射光收敛到连续的大尺度响应；云侧、云顶继续消费原有共享体梯度、向光变白和薄层变白。该修复不读取相机位置、View Ray、Fresnel 或逐实例像素量，因此不会恢复 brightness pumping 或碎裂接缝。实现版本为 44，manifest 固定 `cloudUndersideField=true`。

v44 证据：资产重建 `Saved/Logs/T4A2R1C2B3B6V44-GenerateAssets-20260810.log`；UE 5.8 ForceUnity `Saved/Logs/T4A2R1C2B3B6V44-ForceUnity-20260810.log`；资产无重写验证 `Saved/Logs/T4A2R1C2B3B6V44-Validate-NoRewrite-20260810.log`；fresh `ABTS.Rendering.Toon` 24/24 为 `Saved/Logs/T4A2R1C2B3B6V44-Toon-FreshAutomation-20260810.log`。真实 D3D12 17 点/34 条位于 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260810T105146Z_13992`，云材质无编译错误或 shader fallback；日志中的四条 M11 Finale Nanite usage 警告是既有独立项，不属于本次云修复。`CloudR0GroundZenith` 原图及高频残差均不再出现中心放射纹；`CloudR0GroundObliqueUp` 仍保留较暗云底与受光边，两个正交侧视保留亮云体/暗底分层，`CloudR0Above` 保留云顶低频层次。用户已于 2026-08-10 完成 R1-C2-B 可见 PIE 验收，确认放射纹消失且正常云体层次保留；该阶段状态更新为 `IntegrationAccepted`。云底色带的后续冷暖微调不得重新引入按实例或按视角的亮度源。

### 7.9 T4-A2.2 全球云场与融合语义（已验收）

A2.2 将“逻辑云身份”和“描边身份”拆开。每朵云继续发布稳定 `LogicalCloudIndex`、Seed 与 `LogicalCloudIdentityHash`，供确定性生成、布局 Hash、未来 LOD/裁剪和诊断使用；渲染端则把所有云统一写入 composite stencil `8`。Outline 对任意 `8/8` 邻域抑制内部深度/法线边，对云—天空、云—地面和云—玩法物体边界仍保留外轮廓。这样相邻或相交云朵会被读作连续云场，不会因人为云编号重新出现接缝，也不再受 `8..15` 最多八组的限制。

版本 47 保留版本 46 已建立的太阳无关球面确定性背景布局：12 个 Fibonacci 分布的天气簇覆盖全球，每簇包含两朵邻近逻辑云，共 24 朵；云岛尺寸、厚度、高度、形态 Seed 均有变化。背景云的逻辑身份、布局 Hash 与位置不随太阳方向改变。在此基础上追加 7 朵太阳相对的晨昏测试云：一朵中心云、四朵约 5° 的内桥接云和两朵约 9.5° 的对称外缘云形成连通超大云簇，簇中心跟随晨昏圈，目标联合可见角包络为 27–33°。连接与跨度均按云滴实际占据的可见支撑估算，不再用偏大的作者包围盒制造“数值相连、画面分离”的假阳性。它用于验证大片相交云朵在光暗交界处的融合，不替代或重新排布全球背景云。

每朵云继续保留 A2.1 已验收的 84 云滴质量预算；当前共 31 朵逻辑云、2604 个 HISM 实例与 186 个宏簇。生成合同同时验证八个球面象限覆盖、背景云太阳无关、晨昏超大云簇太阳相对、7 朵连通、联合角包络 27–33°、逻辑身份唯一、大小差异与重复生成 Hash。性能及 LOD 冻结仍属于 A2.4，不在 A2.2 通过降低单云质量提前优化。

云材质不按逻辑云编号或整座云岛切换昼夜。每个像素以 `normalize(WorldPosition - PlanetCenter) · SunDirectionToSun` 求局部太阳高度，并在冻结混合区间内连续产生昼光权重；该权重门控直射变白与薄层变白，夜面则连续混入冷灰蓝基色。夜面亮度乘数为 `0.42`。这样同一朵横跨晨昏线的云可以在空间上连续过渡，多个逻辑云重叠时也不会因 ID 形成亮度接缝；材质宏合同提升为 8，仍禁止相机位置、View Ray 或逐实例像素属性重新进入光照权威。

捕获目录在 A2.2 版本 46 的 `CloudFieldGlobal`、`CloudFieldFusion`、`CloudFieldVariety` 后追加 `CloudFieldNight` 与 `CloudFieldTerminatorMega`，总数变为 22 点/44 条。前者读取深夜面云朵明度与层次，后者对准约 30° 晨昏超大云簇检查连通融合和簇内昼夜连续性。manifest schema 7 记录 24 朵全球背景云、7 朵晨昏超大云、联合角包络与连通性、局部太阳高度光照、夜面增白门控和夜面亮度；逻辑云仍共享单一 `cloudCompositeStencilValue`，不记录每云 stencil。

版本 46 的既有自动证据继续保留：路线 `InstancedCloudletsA2_2GlobalField`；UE 5.8 ForceUnity 已通过；fresh `ABTS.Rendering.Toon` 为 25/25，日志 `Saved/Logs/T4A22Global-ToonAutomation-Final-20260810.log`。真实 D3D12 manifest 为 `Succeeded`、20 点/40 条、版本 46、24 朵逻辑云和 14 个邻近融合对，目录 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260810T140249Z_21868`。这些证据只证明前一轮全球背景云与融合语义，不冒充版本 47 的夜面和晨昏超大云簇验收。

当前版本 47、材质宏合同 8 已完成自动证据：资产重建日志 `Saved/Logs/T4A22NightMega-RebuildAssets-VisibleFusion-20260810.log`，UE 5.8 ForceUnity 日志 `Saved/Logs/T4A22NightMega-ForceUnity-BridgedMega-20260810.log`，fresh `ABTS.Rendering.Toon` 25/25 日志 `Saved/Logs/T4A22NightMega-ToonAutomation-BridgedMega-20260810.log`，资产无重写验证 `Saved/Logs/T4A22NightMega-ValidateAssets-BridgedMega-20260810.log`。真实 D3D12 manifest 为 `Succeeded`、22 点/44 条、schema 7，目录 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260810T155155Z_22128`；记录 24 朵背景云、7 朵晨昏簇成员、`28.8789°` 可见联合包络、连通成立和夜面亮度乘数 `0.42`。静态截图确认晨昏主云体连续成片、簇内日夜颜色连续，深夜云由近白降为可读的冷蓝灰。用户已于 2026-08-11 完成可见 PIE，确认全球背景分布、约 30° 晨昏超大云簇的持续连通与无编号接缝、深夜面层次、簇内昼夜连续过渡、云—天空外轮廓和绕云运动稳定性均符合预期，A2.1 单云层次亦无回退；A2.2 因而晋升为 `IntegrationAccepted`。

当前 1 组晨昏超大云簇是 A2.2 的最低可验收合同，不是最终内容密度。最终画面应提高这类超大型连通云簇在全球云场中的占比，使远景和高空视角更多读取成片的立体天气结构，而不是以孤立单云为主。数量、球面分布、联合角尺度和大小变化暂不在 A2.2 内调整；统一延后到 A2.4、最终合并验收前调参并冻结。该轮必须同时重跑全球覆盖、云—云融合、晨昏连续性、快速相机时域稳定性、PIP/AVI 和 GPU 预算，不能只增加云簇后沿用本轮性能与视觉证据。此调参待办不撤销 A2.2 已通过的生成、融合与夜面光照合同，但未完成前不得宣布 T4-A2 最终冻结。

### 7.10 T4-A2.3 有界穿云表现

A2.3 采用“关系判定 + 局部可见走廊”，而不是重新引入全屏雾壳。CPU 以每朵逻辑云实际可见椭球包络判断四种关系：鸟群进入云内、镜头进入云内、云位于镜头与鸟群之间、镜头与鸟群同时位于云内。任一关系成立时，仅对对应云 MID 连续开启一个由镜头球、鸟群球和镜头—鸟群胶囊走廊组成的有界清除域；其他云和域外云体继续正常绘制。鸟群包络只吸收受控鸟附近的编队成员，避免离队成员把走廊无限拉长。进入和离开使用不同速度平滑，Camera Cut 或诊断传送则立即刷新，防止上一镜头的局部清除残留。

云材质由 `Opaque/Unlit` 升级为 `Masked/Unlit`；局部清除只写 `Opacity Mask`，不改变 A2.1/A2.2 已验收的颜色、宏法线、薄层增白、昼夜过渡或 composite stencil。云光照继续禁止 `CameraPos`、View Ray 和 Fresnel，因而穿云可见性不会重新引入随观察方向发生的整体明暗跳变。Style Off、非 `GroundDay` Profile、Actor 销毁或无法取得有效镜头/鸟群时均 fail closed 到 `TraversalActive=0`；系统不写鸟的轨迹、碰撞、世界生成或 `WorldReady`。

实现版本 48、材质宏合同 9 是 A2.3 的硬清除基线。其纯数据自动化 `ABTS.Rendering.Toon.T4A2_3.BoundedTraversalRelation` 分别验证上述四类遮挡关系与明确无关云；四个捕获点为 `CloudTraversalBirdInside`、`CloudTraversalCameraInside`、`CloudTraversalBetween`、`CloudTraversalBothInside`。基线证据仍保留在 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260811T041012Z_68332`，但不冒充 A2.3.1 的半透明感结果。

A2.3.1 的时域修正版将实现版本提升为 50、材质宏合同保持 10、manifest schema 提升为 10。它不是把云切换为真正透明：鸟体、镜头与视线中心仍为完全清除的安全核心；外围以云岛局部切平面上的稳定二维噪声保留 `0.82` 云覆盖，尺度为 `0.012 cm⁻¹`，使清除边界保持云状且不会形成像素网格。Tone Pass 的最大 `0.20` 雾幕不再消费二值“相机在云内”开关，而是消费椭球包络末端 `22%` 的连续穿入深度并经过小死区和平滑响应；运行时只把 `bGameCameraCutThisFrame` 或显式强制刷新视为切镜，禁止再用单帧位移阈值把高速发射误判为 Camera Cut。方向稳定噪声改为更高角频率、近固定均值的小幅调制，防止镜头转向改变整屏平均遮罩亮度。该雾幕不写深度、不参与云光照，也不增加每朵云的透明 HISM overdraw。

A2.3.1 当前自动证据：资产重建日志 `Saved/Logs/T4A231-RebuildAssets-DenseMask-20260811.log`；UE 5.8 ForceUnity 日志 `Saved/Logs/T4A231-ForceUnity-HybridVeil-Final-20260811.log`；fresh `ABTS.Rendering.Toon` 为 26/26，日志 `Saved/Logs/T4A231-ToonAutomation-HybridVeil-Final-20260811.log`；资产只读验证且 SHA-256 未变化，日志 `Saved/Logs/T4A231-ValidateAssets-HybridVeil-Final-20260811.log`。真实 D3D12 捕获为 `Succeeded`、26 点/52 条，日志 `Saved/Logs/T4A231-D3D12Capture-HybridVeilDenseMask-Final-20260811.log`，目录 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260811T055358Z_29520`。四个 Style On 点均保持完整鸟群；`CloudTraversalBetween` 同时保留连续大云主体与明确视线核心。自动证据不替代连续运动：用户仍须在 PIE 中验证入云、穿越、离云、快速折返和 Camera Cut 时无硬跳、无噪声闪烁、无雾幕残留，完成后才可把本阶段晋升为 `IntegrationAccepted`。

版本 50 的时域修正证据：UE 5.8 ForceUnity 为 `Saved/Logs/T4A231-FlickerFix-ForceUnity-Final-20260811.log`；fresh `ABTS.Rendering.Toon` 26/26 为 `Saved/Logs/T4A231-FlickerFix-ToonAutomation-20260811.log`，边界用例明确验证分数深度而非二值开关。真实 D3D12 manifest 为 `Succeeded`、schema 10、26 点/52 条，日志 `Saved/Logs/T4A231-FlickerFix-D3D12Capture-20260811.log`，目录 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260811T074349Z_70144`；四个穿云 Style On 点均保持鸟群完整可见，manifest 记录 `cloudTraversalContinuousEnvelopeWeight=true`、`cloudTraversalExplicitCameraCutOnly=true` 和 `cloudTraversalLowMeanDriftVeilNoise=true`。静态截图与纯数据合同不能替代运动验收，用户仍须在 PIE 中复测“镜头在云内、鸟在云外”的持续飞行与边界折返。

版本 51 为定位持续闪烁而增加雾幕隔离开关：Tone Pass 的 `CloudTraversalVeilStrength` 与 `CloudTraversalVeilOpacity` 在运行时均固定为 0，manifest schema 11 明确记录 `cloudTraversalCameraInsideVeil=false` 和 `cloudTraversalVeilOpacity=0`；云材质局部镜头球、鸟群球、镜头—鸟群走廊及二维噪声清除仍照常工作。用户在同一路径 PIE 中确认闪烁完全消失，因此根因归属于全屏薄云雾幕，该实验结论已成立；最终路线不再重加此雾幕。

版本 51 自动证据：UE 5.8 ForceUnity 日志 `Saved/Logs/T4A231-NoVeil-ForceUnity-Final-20260811.log`，fresh `ABTS.Rendering.Toon` 26/26 日志 `Saved/Logs/T4A231-NoVeil-ToonAutomation-Final-20260811.log`；`BoundedTraversalRelation` 同时验证雾幕强度/不透明度为零，以及鸟内、镜头内、云在两者之间、两者均在云内和无关云五种局部几何关系。该自动证据只证明合同与回归，不替代 PIE 连续运动的闪烁判断。

版本 52 把该实验结论落实为正式合同，并修复两项只能在运动中暴露的问题。第一，旧实现把受控鸟附近编队压缩为一个最多 `420 cm` 的 Actor/组件包围球；四鸟展开或 Chaos 视觉网格偏离 Actor 中心时，端部鸟会落在清除圈外。新实现按最多四个 `GetBirdVisual()->Bounds` 建立独立球体，加入 `70 cm` 安全边与最多 `260 cm` 的两帧速度前视；关系判定仍使用完整编队联合包络，但材质硬清除逐鸟计算，并以独立 `TraversalProtectionActive` 在进入首帧立即生效，外围噪声走廊才继续平滑。第二，Masked Opacity 与 WPO 会产生几何速度无法完整描述的像素动画；旧材质未声明这一点，TSR 在夜面高速镜头中复用陈旧边缘历史，形成亮边拖影。云母材质现固定 `Has Pixel Animation=true`，让 UE 5.8 为该 Masked 材质写入相应速度/历史拒绝身份，不全局关闭 TSR，也不改变云光照。

版本 52 的自动门包括：UE 5.8 `-ForceUnity -DisableAdaptiveUnity` 编译成功；命令行重建云材质后再次无写入验证成功；fresh `ABTS.Rendering.Toon` 26/26 通过。资产合同验证宏版本 11、四个逐鸟球参数、即时硬保护参数和 `HasPixelAnimation()`；关系测试额外用超过旧 `840 cm` 直径的四鸟展开队形证明旧单球无法覆盖端部而逐鸟球均可覆盖。上述数据门只能防止实现合同回退，不能替代动态像素验收：最终 PIE 必须连续执行“镜头在云内而鸟在云外”“云位于镜头和四鸟之间”“俯视展开队形高速穿云”“夜面高速横移/旋转”四条路径，并逐帧确认四鸟始终可见、无明暗闪烁、无夜云亮边拖影；单张截图不得作为本门通过证据。

版本 53 修复了用户 PIE 发现的云材质回退。根因不是运行时绑定丢失，而是 v52 开启 `Has Pixel Animation` 后，真实 PCD3D_SM6 首次编译额外的 Velocity/Depth 材质排列：Custom 表达式的 `TraversalBirdSphere0..3` 由 Vector Parameter 的 RGB 输出推断为 `float3`，旧 HLSL 却读取 `.w` 半径，产生 `vector swizzle 'w' is out of bounds`，UE 因而按设计回退默认材质。v53 将每个鸟球拆成明确的 RGB 中心输入与连接同一 Vector Parameter A 通道的标量半径输入，运行时仍只写原有四个 `FLinearColor(Center, Radius)` 参数，不扩大参数同步面；资产验证同时禁止再次出现 `.w` 读取。UE 5.8 ForceUnity、资产重建与 SHA 不变的只读复验均通过；fresh `ABTS.Rendering.Toon` 为 26/26，日志 `Saved/Logs/T4A231-MaterialFallbackFix-ToonAutomation-v53-20260811.log`。真实 D3D12 离屏捕获为 `Succeeded`、26 点/52 条，日志 `Saved/Logs/T4A231-MaterialFallbackFix-D3D12-v53-20260811.log`，目录 `Saved/ToonT4A2MaterialFallbackFixV53/ToonT4A2_Screenshots_20260811T093424Z_38448`；日志中不再存在云材质编译错误、越界 swizzle 或云默认材质回退。此事故也固定了一条验证边界：NullRHI 数据/资产合同不能证明最终 SM6 材质排列可编译，凡修改 Custom HLSL、材质 Usage 或像素动画标志，必须追加真实 D3D12 shader 编译与捕获门。该静态门只关闭材质回退，不替代 v52 已列出的四条连续运动 PIE 验收。

版本 54 继续处理用户在材质恢复后观察到的夜面移动亮边。根因是 GroundDay PIE 主相机仍消费默认 Motion Blur，而自动捕获相机一直显式使用 `MotionBlurAmount=0 / MotionBlurMax=0`，因此此前静态证据无法复现高速移动时亮天空被卷入暗云轮廓的青白拖边。`Has Pixel Animation` 在 UE 5.8 中会强制速度写入并改变 TSR 的部分反闪烁/历史策略，但不等价于禁用 Motion Blur；v54 因而把“清晰低模云轮廓”落实为显式 Profile 合同，只在风格化 `GroundDay && CloudsEnabled` 时覆盖 Motion Blur 为 0，`GroundDay` 无云、`SatelliteGuide` 与 `FinaleSpace` 均保持原镜头策略。UE 5.8 ForceUnity 日志为 `Saved/Logs/T4A231-V54-ForceUnity-20260811.log`；fresh `ABTS.Rendering.Toon` 为 26/26，日志 `Saved/Logs/T4A231-V54-Toon-Fresh-20260811.log`。真实 D3D12 manifest 为 `Succeeded`、实现版本 54、26 点/52 条，目录 `Saved/ABTSVisualCaptures/ToonT4A2/ToonT4A2_Screenshots_20260811T100733Z_50824`，日志 `Saved/Logs/T4A231-V54-D3D12-T4A231-V54-Static-20260811-180502.log`；云材质 shader 错误和 fallback 均为 0。该静态门只证明无回归；最终必须由用户在夜面连续高速横移、旋转与折返的 PIE 中确认亮边消失，并同时确认鸟体清除保持稳定。

用户于 2026-08-11 完成最终连续运动 PIE：镜头入云、鸟在云外、云位于镜头与展开鸟群之间、镜头与鸟群同时入云、夜面高速横移/旋转/折返均保持鸟体可见；全屏雾幕闪烁未恢复，夜云青白动态亮边已经消失。至此 A2.3 的数据、资产、真实 RHI、静态像素和动态 PIE 证据层全部闭合，阶段晋升为 `IntegrationAccepted`，允许进入 A2.4；后续性能或消费端调整不得重新开启全屏雾幕、GroundDay Motion Blur 或改变逐鸟硬保护合同。

正式门为云下、云内、云上连续；穿越期间鸟体始终可读且不整屏灰；快速进入/离开与 Camera Cut 后无残留；Style Off、Profile 切换和子系统退出可完整恢复。

### 7.11 T4-A2.4 消费端与性能冻结

A2.4 汇总原 `B3-D/R2`：验证主视图、地面/月面/终局 PIP、Rank11 AVI、快速相机、50%/75%/100% Screen Percentage、1080p/1440p、时域稳定性和 GPU 增量。该阶段不再修改 T3 材质，也不以某一个截图点或 NullRHI 绿灯替代消费端矩阵。

#### 7.11.1 云簇分布冻结与 PIE 复核入口（实现版本 58）

A2.4 的分布调参已完成。生产默认值冻结为 `ClusterCount=24`、`CloudsPerClusterMean=10`、`CloudsPerClusterVariance=64`；这里的 24 表示天气云簇数量，而不是背景逻辑云总数。全球背景继续采用“显式云簇数 × 每簇采样成员数”，因此方差会形成大小差异明显的连通云簇；7 个晨昏诊断云仍作为独立验收簇追加，不参与背景分布采样。每个逻辑云继续固定 84 个 cloudlet：

- `ClusterCount`：全球背景天气簇数量，范围 `1..64`，不再从均值或云朵大小推导；
- `CloudsPerClusterMean`：每簇逻辑云数量均值，范围 `1..64`；
- `CloudsPerClusterVariance`：成员数截断高斯采样的**方差**，范围 `0..1024`，不是标准差；
- `Seed`：非零 `uint32`，同时驱动簇成员分配、球面位置、云岛尺寸与形态，用于重复生成和多 Seed 观察；
- 每簇实际成员数截断到 `1..64`；同一云簇数、均值、方差和 Seed 的成员数组、逻辑布局 Hash 必须完全相同；
- 为容纳冻结分布在生产 Seed 下的确定性采样，背景逻辑云硬上限提升为 `384`，对应追加晨昏簇后的最多 `391` 朵逻辑云与 `32,844` 个 cloudlet；A2.4 生产渲染仍只创建一个共享 HISM 和一个材质批次；超过预算时拒绝本次输入并恢复上一个有效云场；
- 同一天气簇不再使用黄金角盘式独立散点。第一朵云作为中心，后续成员沿确定性三叉生长树连接到既有父云；父子中心间距由双方实际可见角半径计算，并保留足以覆盖不定形轮廓侵蚀的宽重叠。同簇成员共享高度基线，仅允许小幅径向扰动；因此云簇被读取为一片云体，而不是一圈彼此分离的小云；
- 运行时按每簇逻辑 ID、成员索引、角向可见支撑与径向厚度建立重叠图。任意多成员云簇不是单一连通分量时，本次重建 fail closed；冻结的 `24 / 10 / 64` 默认值同样消费该连通合同；
- 当前固定星场派生的生产云 Seed 为 `0xC1A5466C`，在冻结分布下确定性生成 `277` 朵背景逻辑云；追加 7 朵晨昏诊断云后共 `284` 朵逻辑云与 `23,856` 个 cloudlet；这些逻辑身份全部写入同一个 HISM 的逐实例自定义数据，不再一云一组件；
- PIE 命令仍可临时覆盖这组生产默认值，但停止 PIE 后不会写入 Config、Blueprint 或地图。`ClearDistribution` 会立刻恢复冻结的 `24 / 10 / 64` 生产分布及生产 Seed；恢复后的成员位置继续消费版本 58 的连通生长合同。

PIE 控制台命令：

```text
ABTS.Toon.CloudField.Overview
ABTS.Toon.CloudField.SetDistribution 24 10.0 64.0 12345
ABTS.Toon.CloudField.SetClusterCount 24
ABTS.Toon.CloudField.SetMean 10.0
ABTS.Toon.CloudField.SetVariance 64.0
ABTS.Toon.CloudField.SetSeed 67890
ABTS.Toon.CloudField.Status
ABTS.Toon.CloudField.ClearDistribution
ABTS.Toon.CloudField.RestoreView
```

`Overview` 在当前 PIE 世界生成 transient 诊断相机，以主星 `+Z` 径向北极为观察方向，按实际主星半径、云层高度、视口宽高比和 `52°` FOV 自动计算距离，使主星及云场完整位于画面中央；它不移动玩家、鸟群或生产 Party Camera。`RestoreView` 恢复进入总览前保存的 ViewTarget 并销毁诊断相机。每次成功调参都会立即重建云场，并输出 `ClusterCount/Mean/Variance/Seed/BackgroundLogicalClouds/TotalLogicalClouds/Cloudlets/Members/LayoutHash`；无效范围、超出安全预算、Seed 为零、非 PIE 世界或重建未通过现有云合同都会拒绝并恢复上一个有效布局。

分布调参子阶段已通过用户 PIE 验收并冻结；上述入口保留为多 Seed 回归和后续视觉复核工具，不再作为生产默认值来源。PIP/AVI、静态多分辨率/多 SP、GPU 门以及快速相机、夜面、穿云可见 PIE 均已完成，A2.4 与整个 T4-A2 已冻结。

实现版本 58 的冻结代码门使用 UE 5.8 `-ForceUnity -DisableAdaptiveUnity` 与 fresh `ABTS.Rendering.Toon` 回归；最终日志分别记录在 `Saved/Logs/T4A24-FrozenDistribution-V58-ForceUnity-20260811.log` 和 `Saved/Logs/T4A24-FrozenDistribution-V58-Toon-Fresh-20260811.log`。自动化固定验证 `24 / 10 / 64` 三项生产默认值、生产 Seed 下 `277 + 7 = 284` 个逻辑云和 `23,856` 个 cloudlet、每个背景天气簇均为单一可见包络连通分量、显式云簇量与成员均值相互独立、超出 384 朵背景预算时 fail closed，以及 A2.1～A2.3 全回归。分布视觉已经用户 PIE 验收；其消费端与 GPU 后续由实现版本 60 的合同闭合。

#### 7.11.2 单 HISM 合批、消费端与 GPU 合同（实现版本 60）

A2.4 不改变已经验收的逻辑云分布、外形、局部太阳高度照明或穿云清除结果，只重构消费方式：

- `284` 朵逻辑云不再创建 `284` 个 HISM 和 MID，而是由一个 HISM、一个 MID 承载全部 `23,856` 个实例；HISM 仍按内部空间簇执行视锥裁剪，因此“一个材质批次”不等于强制绘制整个星球的所有实例；
- 每个实例的 `63` 个 Custom Data Float 保存基础扰动、云岛中心/局部轴/尺度、6 组宏场和颜色变体。材质直接读取这些数据，逻辑云 ID 不进入 stencil，故合批前后的形态、照明、云间融合和统一外轮廓语义保持一致；
- 穿云关系仍由 CPU 对所有逻辑云逐帧求值，但只把最大穿云强度、硬保护状态、镜头球、四鸟球和镜头—鸟群走廊写入共享 MID。清除区域在 shader 中按世界空间局部化，不因共享材质而清空无关云；
- manifest 只有在运行时云 Actor、单 HISM/单材质批次、`63` 浮点合同和布局身份均成立时才报告成功；关闭云的性能对照也必须真实销毁该消费路线，不能只改可见性描述字段；
- 捕获脚本 `Scripts/ToonT4A24.ps1` 固定 6 个视觉点、1080p/1440p、50/75/100% SP，以及全球俯瞰、夜面和穿云三个云开/关 GPU 对照点。每个 GPU 点和开关状态使用独立 fresh UE 进程，避免连续 `stat GPU` 采样历史污染后续中位数。

当前自动证据：

- UE 5.8 `-ForceUnity -DisableAdaptiveUnity` 编译成功；fresh NullRHI `ABTS.Rendering.Toon` 为 26/26，日志 `Saved/Logs/T4A24-V60-BatchedHISM-Toon-20260811.log`；
- 36 张 D3D12 主视图矩阵位于 `Saved/ABTSVisualCaptures/ToonT4A24/T4A24-20260811-230742`；夜面、全球俯瞰和穿云构图均消费相同云资产，无默认材质回退；
- 地面/月面预览的 SP50/75/100 证据分别位于 `Saved/ABTSVisualCaptures/ToonT2C1/T2C1-LandingPreviews-On-SP50-20260811-231355`、`...SP75-20260811-231430`、`...SP100-20260811-231505`；Rank11 远端预览及 1021 帧 AVI 位于 `Saved/ABTSVisualCaptures/ToonT2C1/T2C1-FinaleRemotePreview-On-SP100-20260811-231541`；
- 隔离式 GPU 报告为 `Saved/ABTSVisualCaptures/ToonT4A24/T4A24-20260811-233224/gpu-increment-summary.json`。全球俯瞰为 `25.40 ms`、云增量 `11.64 ms`，通过开发诊断构图的 30 Hz 门；夜面玩法视角为 `10.29 ms`、云增量 `2.42 ms`，穿云玩法视角为 `13.41 ms`、云增量 `4.62 ms`，均通过 60 Hz 与各自增量门；
- 全球俯瞰是刻意让整颗星球及全部云场入镜的开发诊断点，不代表正常玩法相机。不得为了让它达到 60 Hz 而破坏生产云量；A2.4 的正常玩法门由夜面和穿云局部视角承担。

实现版本 61 在 A2.4 可见 PIE 前补齐三项消费端回归。地面落点画中画先以“光照无关的导航仪器”关闭 SceneCapture 直射光，避免夜面地表坠入黑场；月面落点画中画不再复用全局地面环境快照，而是按 `SatelliteGuide` 派生曝光与星场，并在空深度像素上用深空底色替换蓝色大气背景；主视图地平线轮廓则在天空侧增加对称的八邻域连续覆盖，吞掉高速运动时由时域重建暴露的亮色子像素锯齿。UE 5.8 ForceUnity 和 fresh `ABTS.Rendering.Toon` 26/26 已通过；真实 DX11 两类生产画中画捕获为 `Saved/ABTSVisualCaptures/ToonT2C1/T2C1-LandingPreviews-On-SP100-20260812-105133`。用户已在 PIE 确认地平线描边稳定，随后要求画中画恢复与主视图一致的晨昏光照，因此 v61 的光照无关 PIP 只保留为问题隔离基线，不作为最终 PIP 视觉合同。

实现版本 62 将两种 Landing Preview 改为“同源世界光照 + 导航暗部托底”。SceneCapture 重新开启 Lighting，直接消费当前世界的太阳方向、阴影和材质响应，因而地面与月面在向光面、晨昏线和背光面的方向关系与主视图一致；AfterDOF 的导航分支只处理有深度且低于可靠亮度的几何像素，从 GBuffer BaseColor 保留色相并连续混入冷色最低可读亮度，亮度超过阈值后迅速归零，不改变向光面，也不使用自动曝光、相机方向或额外补光灯。地面继续使用 `GroundDay` 环境背景，月面继续以 `SatelliteGuide` 深空星场替换空深度的蓝色大气。UE 5.8 ForceUnity 与 fresh `ABTS.Rendering.Toon` 26/26 已通过；真实 DX11 两类生产捕获为 `Saved/ABTSVisualCaptures/ToonT2C1/T2C1-LandingPreviews-On-SP100-20260812-112256`，地面夹具重新出现定向阴影，月面夹具出现受光梯度且星空未回退。静态夹具不能覆盖落点连续跨晨昏线，用户仍须在 PIE 分别验证地面/月面向光、晨昏和背光落点，确认夜面可读、方向一致且无亮度跳变；因此阶段保持 `ImplementationComplete（VisibleValidationPending）`。

实现版本 63 进一步把“方向一致”收紧为“对应表面视觉一致”。v62 仍有三个非等价条件：捕获组件不持久化 ViewState、每次 20 Hz 手动刷新都强制 `bCameraCutThisFrame`，并只给 PIP 施加主视图没有的暗部托底；因此主视图已积累的阴影、间接光和时域结果无法在 PIP 首帧成立。v63 删除画中画专用阴影抬升，地面和月面表面都使用与普通主视图相同的世界 Lighting、`GroundDay` 固定曝光、Tone 和 Outline；月面只在空深度像素替换深空星场，背景差异不再改变实体光照。SceneCapture 改为持久 ViewState；同一落点附近的小幅瞄准连续积累历史，只有位置超过 `max(400 cm, 0.5 × CameraDistance)`、旋转超过 `15°` 或地面/月面语义切换才 Camera Cut。真实跳变后先向隐藏的同尺寸 RT 捕获两帧，第三帧才发布到 HUD；同模式跳变期间保留上一张稳定画面，首次打开和语义切换则等新帧稳定后再显示，避免首帧黑场冒充正式 PIP。

v63 的 UE 5.8 ForceUnity 与 fresh `ABTS.Rendering.Toon` 26/26 已通过；真实 DX11 两类生产捕获位于 `Saved/ABTSVisualCaptures/ToonT2C1/T2C1-LandingPreviews-On-SP100-20260812-115955`，manifest 为 `Succeeded`、实现版本 63、两条 512×288 记录。地面夹具阴影已恢复为稳定的冷绿色主视图色带；月面材质消费相同的 GroundDay 表面光照与色调，Tone Pass 同时保留 AfterDOF 已生成的确定性深空背景，不再回退为蓝天。该静态夹具仍不能证明玩家抵达同一落点后的逐像素对照；最终 PIE 应在地面向光/晨昏/背光和月面相同三档各保存一组“PIP—抵达后实地”画面，允许因观察角度造成镜面响应不同，但同一材质的阴影档、曝光和色相必须一致，且调整瞄准时不得闪回未预热首帧。

A2.1～A2.4 已满足真实 RHI、SceneCapture/AVI、动态 PIE 与 GPU 门并冻结；后续 A3/T4-B 回归若发现云问题，应重新打开对应门，不再把本句当作待办。

## 8. T4-A3 与 T4-B

### 8.1 T4-A3.1：高空连续星空过渡

A3.1 先关闭“飞到卫星高度仍是蓝色天幕”的表示缺口。权威输入是每个渲染视图的 `CameraFromPlanetCenterWorld`，而不是鸟体位置、全局计时器或 Gameplay Profile：相机高度低于 `0.22R` 时保持完整地表大气；`0.22R～0.52R` 使用同一条 `smoothstep` 连续降低低频蓝色大气、提高程序 HDR 星场并把太阳 Halo 从 `4°` 收窄到 `1.45°`；到 `0.52R` 完整进入深空背景，冻结卫星高度 `0.55R` 因而不会再停留在地面蓝天。完全进入太空后仅在主星解析切线附近保留窄蓝色大气 Limb，避免退化为无大气裸岩球。

不得在跨越阈值时切到 `SatelliteGuide`：主视图、PIP、AVI 可以处于不同高度，同一帧必须按各自相机独立计算；若用全局 Profile 或鸟体高度，会使仍在地表的 PIP/其他视图同步跳色。首版不增加时间平滑，宽空间过渡带已经连续且确定；若玩家 PIE 仍感觉过渡过快，只调整起止比例，不能引入随帧历史或 Camera Cut 依赖。

正式自动视觉合同为 `ToonT4A3` / manifest schema 15，共五个固定 GroundDay 点：`AltitudeGround`、`AltitudeCloudTop`、`AltitudeTransitionMid`、`AltitudeSatellite`、`AltitudeSpace`；每点都有可逆 StyleOff/StyleOn，共 10 条记录。开发捕获脚本为本地 `Scripts/ToonT4A31.ps1`（`Scripts/*` 按仓库策略不纳入版本控制；正式命令参数由本段和 manifest 冻结）。自动证据必须证明高度与 `highAltitudeSpaceBlend` 单调、卫星/外大气为 1、五点始终仍为 GroundDay，并检查地面蓝天不回退、过渡中段无硬色带、卫星高度是星空、外大气只保留窄 Limb。UE 5.8 ForceUnity 已通过；fresh `ABTS.Rendering.Toon` 为 27/27，日志 `Saved/Logs/T4A31-Toon-20260812-123423.log`。真实 D3D12 manifest 为 `Succeeded`、schema 15、实现版本 64，目录 `Saved/ABTSVisualCaptures/ToonT4A3/T4A31-20260812-123752/ToonT4A3_Screenshots_20260812T043832Z_52848`，日志 `Saved/Logs/T4A31-20260812-123752.log`；10 条记录依次为 `0.04R/0`、`0.215R/0`、`0.37R/0.5`、`0.55R/1`、`0.72R/1`，真实 shader 无编译错误。Style On 图确认地面/云上保持蓝天，中段连续变为深蓝灰，卫星及外大气转为带星点的深空并只在行星切线附近保留窄蓝色 Limb。用户已于 2026-08-12 完成实际飞行 PIE，确认从地表上升至卫星/月球高度期间天空连续过渡、没有高度硬切，星场和切线大气边缘表现符合预期；A3.1 因而晋升为 `IntegrationAccepted`。

v67 星场可读性复核继续使用完全相同的五点相机构图与 `StarSeed`。UE 5.8 ForceUnity 和 fresh `ABTS.Rendering.Toon` 28/28 已通过，日志 `Saved/Logs/T4A3-StarReadability-V67-Toon-Fresh-20260812.log`；真实 D3D12 目录为 `Saved/ABTSVisualCaptures/ToonT4A3/T4A31-20260812-145622/ToonT4A3_Screenshots_20260812T065649Z_65500`，日志 `Saved/Logs/T4A31-20260812-145622.log`。在 `AltitudeSatellite` 固定空域裁剪中，可读星点像素由 v64 的 `5` 增至 `52`，亮星像素由 `0` 增至 `12`，峰值由 `29/255` 增至 `68/255`，而背景平均亮度仅由 `3.077` 增至 `3.092`；因此提升来自星点足迹/等级而不是抬灰深空背景。该组截图为自动视觉证据，仍保留真实 PIE 对星点密度、运动稳定性和主观层级的最终判读。

v68/v69 修复晨昏线星点方向反转：GroundDay 星场不再只读相机位置的 `SunHeight`，而在晨昏带内复用天空的 `SunHeight + ViewToSun×0.42` 连续视线模型；v69 将接近水平的天空射线可见度从旧窄带提高到可读范围，避免正确的背阳方向权重再次被地平线项压没。朝阳天空压制星点，背阳天空显示星点，深昼/深夜和高空/终局仍保持原权威；地表几何继续由 SceneDepth 拒绝，不会透出星点。v69 的 UE 5.8 ForceUnity 与 fresh `ABTS.Rendering.Toon` 28/28 已通过，最终合同日志为 `Saved/Logs/T4A3-TerminatorStars-V69-Horizontal-Toon-Fresh-20260812.log`；真实 D3D12 `ToonT4A1` 20/20 位于 `Saved/ABTSVisualCaptures/ToonT4A1TerminatorStars/T4A1-TerminatorStars-V69-20260812-153234/ToonT4A1_Screenshots_20260812T073323Z_13520`，日志 `Saved/Logs/T4A1-TerminatorStars-V69-20260812-153234.log`，且无 shader 编译错误或材质 fallback。该真实捕获证明 shader 路线有效；两组历史固定诊断相机均向地表俯视，不能替代本轮用户所给“同一晨昏位置水平转身”的最终 PIE 门，故不再以其高频残差冒充水平星点验收。

### 8.2 T4-A3.2：三套环境 Profile 正式装配

实现版本 65 把此前“存在参数但主要靠全局 CVar/捕获脚本选择”的三套 Profile 提升为正式消费合同：

- `FABTSStylizedEnvironmentProfilePolicy` 冻结 Actor 表现：`GroundDay` 保留球心 `SkyAtmosphere` 和低模云场并继续关闭不相容的全局 Z 高度雾；`SatelliteGuide` 与 `FinaleSpace` 均隐藏地表大气、全局雾和地表云，背景由确定性 HDR 星场负责；Style Off、子系统退出和失败路径恢复原 Actor 可见性与原始参数；
- `FABTSStylizedViewPolicy` 将 `Profile`（实体表面的 Tone、Outline、曝光）与 `EnvironmentProfile`（大气/星场背景）分开。月面 PIP 因而继续使用已验收的 `GroundDay` 表面视觉和世界光照，只把空背景交给 `SatelliteGuide`；地面 PIP 两者均为 `GroundDay`，终局 PIP/AVI 两者均为 `FinaleSpace`；
- Integration 世界子系统每 `0.1 s` 只读查询 M11 公开的 `IsFinaleActive()`：普通世界选择配置基线（生产默认 `GroundDay`），终局活动优先选择 `FinaleSpace`；退出终局或失败时间线结束后，解析器重新得到配置基线。该装配不写 M11 私有状态，也不修改积分器、轨迹、镜头或演出；
- 主视图后处理不再直接相信诊断 CVar，而优先消费世界子系统已经发布的环境快照 Profile，避免世界 Actor 已切终局而 Tone/Outline 仍留在地面档。诊断/捕获仍可在没有世界快照时使用显式 CVar。

三套环境 Profile 只读消费同一球面快照：

- `GroundDay`：完整大气，地表昼夜连续，星空受大气/太阳高度抑制；
- `SatelliteGuide`：无地表云和蓝色天幕的稀疏深空星场；月面实体仍由 `GroundDay` 表面光照保证与实地一致；
- `FinaleSpace`：完整星场，不使用不相容的地表高度雾/云，行星/UFO 保持轮廓和受控边缘光。

A3.2 自动化必须冻结三套 Actor 策略、月面“表面/背景”双 Profile、终局优先级和结束后的确定性回切。可见 PIE 至少检查普通世界云/大气、月面 PIP 星空、进入终局后无地表云/蓝天、主动退出与失败恢复后的 GroundDay 回切，以及 Rank11 AVI。A3.3 再处理进程中断、Actor 销毁、地图切换等显式快照/恢复所有权；在 A3.3 完成前不得把 A3.2 的轮询回切写成完整异常恢复。

A3.2 当前状态为 `ImplementationComplete（VisibleValidationPending）`。UE 5.8 `-ForceUnity -DisableAdaptiveUnity` 编译已成功；fresh NullRHI `ABTS.Rendering.Toon` 为 28/28，新增 `ABTS.Rendering.Toon.T4A3_2.EnvironmentProfileAssemblyContract` 已通过，日志为 `Saved/Logs/T4A32-V65-Toon-Fresh-20260812-R2.log`。这些证据证明数据路由与旧阶段回归，不替代终局进入/退出、失败恢复、月面 PIP 和 Rank11 AVI 的真实 RHI/PIE 判读。

A3 全部通过后进入 T4-B，重新看 T3 的 Roughness、Specular、Rim、Tint，并用 A0 矩阵关闭 `TOON-T2A-002`。M7 未完成时可以形成“非 M7 T3/T4 联合基线”，但不得宣布完整视觉冻结。

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
| 夜面云在高速移动时出现青白色外缘，静态 D3D12 截图却正常 | GroundDay PIE 主相机仍使用默认 Motion Blur；暗云与亮天空的高反差轮廓在运动模糊中跨边混合。`Has Pixel Animation` 只标记像素动画、强制速度写入并改变 TSR 的部分历史策略，不能关闭运动模糊，也不能单独保证轮廓邻域无拖边 | v54 将后处理策略限定为 `GroundDay && CloudsEnabled` 时把 `MotionBlurAmount/Max` 同时覆盖为 0；SatelliteGuide 与 FinaleSpace 不变。自动截图原本就是零 Motion Blur，所以静态图只承担无回归证据；最终必须用夜面高速横移、旋转和折返 PIE 验证动态边缘 |

## 11. 所有权与交接

环境合同、捕获探针、共享渲染控制、Config、共同地图和共享天空/大气资产由 Integration 唯一修改。M3 只继续提供主星几何/已接受世界；M11 只发布终局阶段和自有 SceneCapture 语义；M7 不承担 A0–A3。任何需要 M3/M11 新字段的需求先走稳定快照/消费接口，不允许环境系统读取功能工作树私有数组。
