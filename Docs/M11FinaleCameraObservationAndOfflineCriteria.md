# M11 终局镜头导演观测与离线判据（M1）

> 编码：UTF-8，简体中文。
>
> 状态：已实现并完成旧镜头四象限基线。
> 所有权：`feature/m11-finale`。本阶段只观测既有镜头，不修改相机、轨迹、候选、三渲二参数或共享地图。

## 1. 目标与非目标

M1 把 M0 的“可以稳定录完整段视频”推进为“可以逐帧解释旧镜头为什么失败”。每一个落盘 JPG 都有一行同编号观测；离线工具只读 CSV，量化鸟离框、当前叙事目标丢失与镜头跃变，并比较同一 Rank 在 Stylized 0/1 下的导演身份。

M1 不实现镜头导演，不改变 `AABTSM11FinaleFlightCamera` 的位置、旋转、FOV、Lag、ViewTarget 或切换逻辑。观测器也不读像素，因此其“几何可见”结论不等同于颜色、轮廓、雾化或三渲二像素可读性。

## 2. 数据来源与阶段判定

### 2.1 唯一目标身份

当前目标只从冻结布局和权威播放事件解析，禁止按 Actor 名称、离鸟最近物体、屏幕中最大球体或当前 PIP 猜测：

- `Assist1/2/3`：使用布局的 canonical center 与 `VisualRadiusCM`，经当前 Finale frame 变换到世界空间；
- `UFO`：使用终局目标的几何接触中心和半径；
- 主控鸟：读取本次 Attempt Bird 的视觉 Skeletal Mesh 包围球；Actor bounds 只作视觉组件缺失时的保守回退。

### 2.2 诊断阶段

阶段只由 `AssistEnter / ClosestApproach / AssistExit / TargetHit` 的权威时间与当前 playback time 决定：

| 时间区间 | 阶段 | 当前目标 | 切换原因 |
| --- | --- | --- | --- |
| 发射前 | `PreLaunch` | Assist1 | `AwaitingLaunch` |
| 发射至 Assist1 Enter | `CruiseToBody` | Assist1 | `LaunchToAssist1` |
| Assist N Enter 至 Closest | `Approach` | Assist N | `AssistNEnter` |
| Assist N Closest 至 Exit | `Periapsis` | Assist N | `AssistNClosestApproach` |
| Assist N Exit 至下一 Assist Enter | `Handoff` | Assist N+1 | `AssistNExitToAssistN+1` |
| Assist3 Exit 至命中 | `FinalApproach` | UFO | `Assist3Exit` |
| `TargetHit` 后终帧停留 | `Terminal` | UFO | `TargetHit` |

发射后若权威事件不完整，观测器 fail closed；不会回退到按距离选目标。

## 3. 逐帧 CSV 合同

M1 基线录制合同版本为 4。成功录制除 JPG、AVI、Manifest 外，必须写出：

```text
<MovieFolder>/<MovieName>.camera-observations.csv
```

CSV schema 版本为 1，共 36 列：

- 身份与时间：`schemaVersion`、`frameIndex`、`captureSeconds`、`playbackSeconds`、`interactionState`；
- 导演语义：`stage`、`currentTarget`、`stageReason`；
- 鸟：世界坐标、屏幕坐标、相机深度、像素半径、可见比例；
- 目标：世界坐标、屏幕坐标、相机深度、像素半径、可见比例；
- 相机：世界坐标、Pitch/Yaw/Roll、到鸟/目标距离、FOV；
- 连续性：相邻捕获帧的位置、旋转和 FOV 变化量。

CSV 行数必须严格等于 JPG/AVI 帧数，`frameIndex` 必须为 `0..N-1`。写 CSV 失败或行数不一致会使整个录制失败，不允许只留下视频并报告成功。

### 3.1 M2 向后兼容扩展

M2 把录制合同升级到 5、CSV Schema 升级到 2，并在原 36 列基础上增加 `directorMode`、`directorM2FrozenEnabled`、`directorBlendAlpha`。Manifest 同步记录请求模式、实际冻结模式、混合帧数、非 Assist1 越界帧数和最大混合权重。离线工具同时接受 Schema 1/2；旧 M1 四象限 CSV 不需要重录。

M2 A/B 使用：

```powershell
python Tools/M11Camera/analyze_camera_observations.py `
  <legacy.camera-observations.csv> `
  <directed.camera-observations.csv> `
  --comparison-mode director-ab `
  --require-pass `
  --output <director-ab.camera-report.json>
```

该门要求两条阶段决策指纹相等、旧镜头混合帧为 0、导演镜头只在 Assist1 Cruise/Approach/Periapsis 混合、鸟不丢失、当前目标丢失显著改善且相机无跳变。报告额外记录 Cruise 混合帧、Cruise 目标丢失帧、首次 Cruise 混合帧和首次 Cruise 目标可见帧，用于防止“Approach 出现太晚且首次出现已经过大”的假通过。

### 3.2 投影与可见比例

观测器用录制帧相同的最终相机 View、水平 FOV 和分辨率做解析投影。视觉包围球投影为屏幕圆，其可见比例定义为“圆的轴对齐包围方形与画幅相交面积 / 包围方形面积”，并限制在 `[0,1]`。深度不为正时像素半径和可见比例为 0。

这个比例是稳定、便宜、与渲染风格无关的构图代理，不是遮挡查询，也不证明目标实际有颜色或轮廓。尤其在近掠时，大球体可能超框但仍保留边缘；M2 起可在同一 CSV 上增加更严格的地平线/遮挡判据，而不修改 M1 原始数据。

## 4. 离线判据

工具入口：

```powershell
python Tools/M11Camera/analyze_camera_observations.py `
  <run-a.camera-observations.csv> `
  <run-b.camera-observations.csv> `
  --output <comparison.camera-report.json>
```

默认只要输入结构有效就退出 0，因为“旧镜头未通过构图判据”正是 M1 所需证据；以后需要作为导演质量门时显式加入 `--require-pass`，不通过返回 2。输入/Schema/数值非法返回 1。

默认逐帧门限只作用于 `Launched` 和 `TargetHit`：

| 判据 | 默认阈值 | 失败语义 |
| --- | --- | --- |
| 鸟可见 | `birdVisibleRatio >= 0.5` | 鸟离开安全构图代理 |
| 当前目标可见 | `targetVisibleRatio > 0.01` 且 `targetPixelRadius >= 4` | 当前叙事目标离框、在相机后或小到不可读 |
| 相机位置连续 | 单帧变化 `<= 5000 cm` | 位置跃变 |
| 相机旋转连续 | 单帧变化 `<= 15°` | 朝向跃变 |
| FOV 连续 | 单帧变化 `<= 2°` | 焦段跃变 |

报告同时给出失败帧数、最长连续失败、最大跃变量，以及每个连续 Stage/Target 窗口的帧范围和极值。阈值是 M1 的离线诊断基线，不是最终 M2–M5 镜头质量目标。

## 5. 渲染正交身份

fresh 进程可能因渲染预热在发射前多等待一帧；呈现 Actor 在同一 playback time 的小量浮点位置也可能受帧调度影响。把绝对 `frameIndex` 或全部原始浮点数混入“阶段决策 Hash”会把调度噪声误报成导演分支变化。因此报告明确分为三类指纹：

1. `decisionFingerprintSha256`：只对有效飞行帧的 playback time、Interaction State、Stage、CurrentTarget 和 StageReason 哈希；
2. `criteriaFingerprintSha256`：对同一飞行序列逐帧的鸟丢失、目标丢失、位置/旋转/FOV 跃变布尔判据哈希；
3. `observationFingerprintSha256`：对飞行段原始世界位置和相机数值哈希，只用于追踪呈现浮点差异，不作为 M1 正交放行门。

同一 Rank 的 Stylized 0/1 必须同时满足前两类指纹相等，才有 `allIdentityFingerprintsEqual=true`。发射前等待帧不同不会改变结论；阶段或任一阈值分类不同则必须失败。M5 在导演实现稳定后再收紧到轨迹、事件、阶段与相机数值 Hash 全等。

## 6. 2026-08-05 旧镜头四象限证据

四次均为 fresh `UnrealEditor.exe -game -dx11 -RenderOffscreen`、1280×720、30 fps、旧 `AABTSM11FinaleFlightCamera`、合同版本 4；均到达 `TargetHit`，CSV 与影像帧数一致：

| Rank / 渲染 | 总帧 | 飞行帧 | 鸟丢失 | 当前目标丢失 | 空构图 | 位置/旋转/FOV 跃变 | 阶段窗口 |
| --- | ---: | ---: | ---: | ---: | ---: | --- | ---: |
| Rank11 / Stylized 0 | 949 | 946 | 0 | 946 | 0 | 0 / 0 / 0 | 11 |
| Rank11 / Stylized 1 | 949 | 946 | 0 | 946 | 0 | 0 / 0 / 0 | 11 |
| Rank0 / Stylized 0 | 1146 | 1140 | 0 | 391 | 0 | 0 / 0 / 0 | 11 |
| Rank0 / Stylized 1 | 1147 | 1140 | 0 | 391 | 0 | 0 / 0 / 0 | 11 |

结论：

- Rank11 两种渲染的导演决策、阈值判据和原始观测指纹均相等；
- Rank0 两种渲染的导演决策与阈值判据指纹相等，`allIdentityFingerprintsEqual=true`；Stylized 1 只在发射前多 1 帧，原始呈现浮点指纹不同，不影响阶段和判据身份；
- 旧镜头没有单帧位置、旋转或 FOV 跃变，主控鸟在本解析包围体判据下也没有丢失；主要失败是“当前权威叙事目标”长期离框。Rank11 全部 946 个有效飞行帧均失败，Rank0 为 391/1140；
- `target lost` 指当前应该参与构图的 Assist/UFO 不可见，不等价于画面中完全没有任何行星；这解释了肉眼仍可能看到另一颗球体轮廓而判据继续失败的情况；
- Rank11 仍为 `UNCERTIFIED`。本次只是 Nominal 路线的镜头诊断，不能替代全操作域认证，也不评价大气散射或三渲二颜色质量。

报告产物：

- `Saved/VideoCaptures/M11CameraCapture/M1-Rank11-Orthogonality-20260805-234749.json`
- `Saved/VideoCaptures/M11CameraCapture/M1-Rank0-Orthogonality-20260805-234749.json`

## 7. M1 完成门

- [x] 每个影像帧都有同编号、有限数值的观测行；
- [x] 当前目标和阶段只来自权威布局/事件，缺失时 fail closed；
- [x] 离线工具能量化旧镜头失败，并允许旧基线以诊断结果正常出报告；
- [x] Rank0、Rank11 × Stylized 0、1 完成 fresh 四象限录制；
- [x] 同一 Rank 的阶段决策与阈值判据身份跨渲染一致；
- [x] 未修改旧相机、候选、权威轨迹、三渲二参数、地图或资产。

M2 已在 Assist1 落地双目标 Cruise Lead-in / Approach / Periapsis 镜头，并通过复用本 CSV 与离线工具完成 A/B；结果见 [M11 终局镜头 M2 设计](M11FinaleCameraM2SingleAssistDirection.md)。
