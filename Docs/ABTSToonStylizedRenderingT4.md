# ABTS 三渲二 T4：球面环境、光照、云雾与程序化星空

> 状态：2026-08-07 建立。T4 总路线与 A0 合同已落地；UE 5.8 ForceUnity、`ABTS.Rendering.Toon.T4A0` 3/3、T0 回归 3/3、T2-A 回归 2/2 已通过。A0 尚待用户可见的 5 点 × 6 变体截图矩阵验收；A1–A3/B 尚未开始。
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

### 6.1 大气与曝光

- 生产环境关闭现有全局 Z `ExponentialHeightFog` 贡献，不用旋转 Actor 冒充球面高度；
- `SkyAtmosphere` 使用 `Planet Center at Component Transform`，中心、Ground Radius 和 Atmosphere Height 从环境合同换算；
- 只有合同选中的 index 0 Directional Light 可以作为太阳；存在零个或多个时环境 Profile fail closed；
- 固定或严格限制曝光，防止昼夜切换、背光面和 HDR 星点引起曝光泵动；
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

## 11. 所有权与交接

环境合同、捕获探针、共享渲染控制、Config、共同地图和共享天空/大气资产由 Integration 唯一修改。M3 只继续提供主星几何/已接受世界；M11 只发布终局阶段和自有 SceneCapture 语义；M7 不承担 A0–A3。任何需要 M3/M11 新字段的需求先走稳定快照/消费接口，不允许环境系统读取功能工作树私有数组。
