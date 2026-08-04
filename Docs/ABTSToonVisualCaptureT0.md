# ABTS 三渲二 T0 自动视觉基线

> 状态：**T0 源码与纯数据自动化已完成；真实 RHI 截图、GPU 数据与美术批准基线待显式运行验收**
> 唯一验收地图：`/Game/Maps/L_ABTS_M11`
> 唯一引擎：`C:\Program Files\Epic Games\UE_5.8`

关联：[三渲二与全局风格化渲染设计](ABTSToonStylizedRenderingDesign.md) · [多工作树协作与集成规范](ABTSMultiWorktreeDevelopmentGuide.md)

## 1. T0 交付了什么

T0 新增一个 Integration 所有的显式运行探针。它不编辑地图、GameMode、材质或 Blueprint，也不移动玩家和玩法 Actor。只有进程带有 `-ABTSToonT0Capture` 或 `-ABTSVisualCaptureSuite=ToonT0` 时，`UABTSToonVisualCaptureSubsystem` 才会创建并执行；普通游戏没有 Tick 或截图成本。

探针会：

1. 只接受 `L_ABTS_M11`，等待 M3 表现、四鸟队伍、M6 `WorldReady`、M7 建筑 IdleValidation、M3 卫星/E5 练习运行时和 M11 三行星/UFO 全部就绪；
2. 校验实际生成 Seed 和 M7/M11 世界合同身份，不在生成后改写 Seed；
3. 由只读语义数据解析四个镜头，生成相机姿态 Hash；
4. 暂停世界、抑制临时屏幕调试文字，同时保留真实 HUD 绘制链；这四个点不主动进入瞄准或发射状态，因此不把 PIP/轨迹当作 T0 已覆盖证据；
5. 在完全相同的世界时间与镜头姿态下依次运行 Style Off/On；
6. 截图模式写出八张 PNG，经完整解码后记录实际尺寸及 MD5；GPU 模式默认对八个变体各采 3 个互不重叠的 `ProfileGPU` 单帧样本；
7. 成功、失败或中断都以临时文件 + 替换方式更新 `manifest.json`，并恢复原 ViewTarget、暂停状态、屏幕消息和风格开关。

T0 的 `StyleImplementationVersion=0` 是刻意保留的真实身份：Style On 已经走稳定开关和 Profile 接缝，但 T1 材质/后处理尚未消费它，所以此时 Off/On 不应被宣称为已有三渲二像素差异。

## 2. 为什么只使用 `L_ABTS_M11`

`ABTSM11GameMode` 继承 M10 的完整运行链，而且当前 `L_ABTS_M11` 同时具备地面生成世界、M7 建筑、M3 卫星/E5 练习和 M11 终局表现。T0 因而使用一张世界、一个 Seed、一次 WorldReady，在其中设置四个语义检查点，而不是把地面与终局拆成两张地图。

这样能避免两次生成的 Seed、建筑沉降、天空/光照、配置或资产加载状态不同，却被误当成同一组 A/B。将来只有当某个渲染契约确实无法在 M11 继承链中出现时，才新增独立切片；不能仅为了分类整齐复制一张地图。

## 3. 四个语义截图点

| ID | 只读来源 | Profile | 画面意图 |
| --- | --- | --- | --- |
| `GroundStart` | M3 `GetInitialRoadSpawnTransform` 与已接受世界身份 | `GroundDay` | 地表、道路、鸟群、树石和近景总体基线 |
| `SlingshotBuilding` | M7 建筑合同中路线中段的 `DestructibleTarget`、对应 Accepted Actor、最近两个普通槽 | `GroundDay` | 弹弓槽与可破坏建筑的同框可读性 |
| `SatelliteE5` | `AABTSM3MonthlySatellitePracticeRuntime` 的已认证快照 | `SatelliteGuide` | 卫星球体、背面 E5 与空间尺度关系 |
| `FinaleLayout` | M11 终局局部帧、三颗助推行星 Actor 和 UFO | `FinaleSpace` | 3+1 终局整体构图与星空对象可读性 |

镜头不保存绝对坐标。每次运行都从上述语义对象重新求解；建筑构图使用 M7 合同的地形帧与确定性视觉包络，活动 Chaos 模块只用于确认 Accepted Actor 仍真实存在且位于合同包络允许的最大匹配距离内，不参与相机定位。缺失、多实例、身份不一致、建筑拒绝、错误地图、错误分辨率或超时都会 fail closed。`manifest.json` 同时记录 `semanticIdentityHash`、请求的 `cameraPoseHash` 和 PlayerCameraManager 最终输出的 `effectiveCameraPoseHash`，因此“文件名相同但实际不是同一世界/镜头”不能冒充回归证据。

## 4. 运行方式

### 4.1 截图基线

关闭其他正在进行的正式 PIE、Shader 编译和重型 GPU 工作后，在 PowerShell 中运行：

```powershell
$Editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$Project = 'C:\workspace\AngryBirdsToSpace\AngryBirdsToSpace.uproject'
$Repo = 'C:\workspace\AngryBirdsToSpace'
$Dirty = @(git -C $Repo status --porcelain)
if ($Dirty.Count -ne 0) { throw 'T0 正式证据要求干净工作树。' }
$BuildId = (git -C $Repo rev-parse HEAD).Trim()

& $Editor $Project /Game/Maps/L_ABTS_M11 `
  -game -dx12 -windowed -ForceRes -ResX=1920 -ResY=1080 `
  -ABTSM3R5Preview -ABTSM3R5PreviewCandidate=4 `
  -ABTSM3R31SlotPreviewCandidate=4 `
  -ABTSToonT0Capture -ABTSToonT0Mode=Screenshots `
  "-ABTSToonT0BuildId=$BuildId" `
  -ABTSToonT0ExpectedSeed=312503 -ABTSToonT0WarmupFrames=8 `
  -ABTSToonT0TimeoutSeconds=180 -ABTSToonT0ExitWhenDone
```

不需要操作角色或手调镜头。默认输出为：

```text
Saved/ABTSVisualCaptures/ToonT0/
  Screenshots_<UTC>_<PID>/
    01_GroundStart_StyleOff.png
    01_GroundStart_StyleOn.png
    ...
    04_FinaleLayout_StyleOff.png
    04_FinaleLayout_StyleOn.png
    manifest.json
```

`-ABTSToonT0BuildId` 是必填的调用者声明：运行器只检查它非空并写入 manifest，无法自行证明磁盘 DLL 就来自该提交。正式运行必须先保持工作树干净、用唯一 UE 5.8 强制 Unity 编译该完整 HEAD，再运行上面的命令；不能拿旧二进制配新提交号。`-ABTSToonT0Output=<目录>` 可改变输出根目录；相对路径以 `Saved/` 为基准。`-ABTSToonT0AllowAnyResolution` 只允许临时预览，带该参数的结果不能成为 1080p 正式基线。`-ABTSToonT0KeepWorldRunning` 同样只用于排错，因为它放弃了 Off/On 的同一世界时间保证。

### 4.2 GPU 基线

GPU 证据必须另起 fresh 真实 RHI 进程，不能和截图同帧，也不能使用 `-NullRHI`：

```powershell
$Editor = 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe'
$Project = 'C:\workspace\AngryBirdsToSpace\AngryBirdsToSpace.uproject'
$Log = 'C:\workspace\AngryBirdsToSpace\Saved\Logs\ToonT0-GPU.log'
$Repo = 'C:\workspace\AngryBirdsToSpace'
$Dirty = @(git -C $Repo status --porcelain)
if ($Dirty.Count -ne 0) { throw 'T0 正式证据要求干净工作树。' }
$BuildId = (git -C $Repo rev-parse HEAD).Trim()

& $Editor $Project /Game/Maps/L_ABTS_M11 `
  -game -dx12 -windowed -ForceRes -ResX=1920 -ResY=1080 `
  -ABTSM3R5Preview -ABTSM3R5PreviewCandidate=4 `
  -ABTSM3R31SlotPreviewCandidate=4 `
  -ABTSToonT0Capture -ABTSToonT0Mode=GPU `
  "-ABTSToonT0BuildId=$BuildId" -ABTSToonT0GPUSamples=3 `
  -ABTSToonT0ExpectedSeed=312503 -ABTSToonT0WarmupFrames=8 `
  -ABTSToonT0TimeoutSeconds=600 -ABTSToonT0ExitWhenDone `
  "-AbsLog=$Log"
```

运行器会关闭 `r.ProfileGPU.ShowUI`，等待当前 profile 报告 `IsProfiling=false` 后再经过至少 30 个 Tick 且 1 秒的稳定期、刷新线程日志，才承认样本完成并发下一个；结束时恢复原 CVar 值与 set-by 优先级。日志以 `[ABTS][ToonT0][GPUProfileMarker]` 标出点位、Style、请求/实际 Pose Hash 和样本序号；紧随其后的 `ProfileGPU` 是对应的单帧层级。默认 3 次采样只是可重复诊断基线，不是统计意义上的稳定帧时测试；manifest 中的 `gpuProfileCommandAccepted=true` 也只证明命令被真实 RHI 接受并经过上述完成稳定门，不等于某个毫秒预算已经通过。正式报告仍须归档各样本的 ProfileGPU 层级或等价 GPU 工具输出，并比较 Style On 相对 Off 的增量。

## 5. Manifest 最小证据

正式截图/GPU 证据至少满足：

- `status == "Succeeded"`；
- `buildIdentity` 与实际编译源码 HEAD 一致；
- `actualSeed == expectedSeed == 312503` 且 `sourceWorldAccepted == true`；
- M3 表现、卫星和终局帧的 `SourceCandidateId` 相同，普通槽与终局帧分别明确记录 `PreviewTest`，且 `finaleFrameMonthlyWorldAccepted == false`；
- `records` 恰好为 8；
- 同一个 `pointId` 的 Off/On 拥有相同 `semanticIdentityHash`、`cameraPoseHash`、`effectiveCameraPoseHash`、FOV 和分辨率；
- 截图模式每项 PNG 存在、可完整解码、MD5 非空且 PNG 实际尺寸与 viewport/目标分辨率相同；GPU 模式每项命令被接受且 `gpuProfileSampleCount == 3`；
- GPU、RHI、驱动、Shader Platform、Scalability、Screen Percentage 和曝光策略身份已写入 `environment`；
- 日志只有一个 `[ABTS][ToonT0][Terminal] Success=1`；
- `StyleImplementationVersion=0` 时不把 On/Off 近似相同判为 T0 失败；进入 T1 后必须提高实现版本，并由批准基线开始做像素比较。

第一次真实 RHI 运行产生的是“候选基线”，不能由程序自动批准。应先人工检查构图、遮挡、HUD 和四类对象是否可读；批准后再冻结保存位置和变更流程。T0 的被动四点不进入瞄准/发射，因此不能证明地面/月面 PIP 或轨迹线正确；这些需要 T2 增加显式玩法状态截图。缺少已批准图时，不得把 UE 截图比较工具的“新图”警告当作正式通过。

截图与 GPU 来自两个 fresh 进程，必须额外做跨 manifest 对齐：`buildIdentity`、全部 world/candidate/result identity、`captureCatalogueHash`、四点 requested/effective pose hash、分辨率，以及 `dynamicRHI`、`shaderPlatform`、Scalability、`screenPercentage`、`dynamicResolutionOperationMode`、`vSync` 必须逐项一致，才能称为同一镜头的图像/GPU 基线。与已批准历史基线做 GPU 对比也遵守相同环境门；字段仅存在但值不同不算可比。

T0 不强制覆盖关卡现有自动曝光。每个变体都会独立触发同规则 Camera Cut 重置并等待相同 warmup，但 T1 改变画面亮度后仍可能收敛到不同目标曝光，跨运行也不承诺实际 EV 相同。`environment.exposurePolicy=SceneConfiguredAutoExposure_CameraCut_PerVariantWarmup` 只是运行器策略标签，不是 PP Volume、曝光范围/速度和实际 EV 的完整快照；若后续重复运行仍有曝光漂移，再新增独立的固定曝光 Profile，而不是静默修改当前基线。

## 6. 自动化与当前验收状态

T0 的快速测试过滤器为：

```text
ABTS.Rendering.Toon.T0
```

它包含 3 项：命令行 fail-closed 契约、四点目录/相机数学、Style/Profile 接缝。2026-08-04 已使用 UE 5.8 fresh `UnrealEditor-Cmd -NullRHI` 验证 `3/3`，完成标记为 `TEST COMPLETE. EXIT CODE: 0`，日志为 `Saved/Logs/ToonT0-20260804-Final-FreshAutomation.log`；同时已通过 `-ForceUnity -DisableAdaptiveUnity` 全链接。

这三项不渲染像素，也不测 GPU。当前分层状态为：

| 证据层 | 状态 |
| --- | --- |
| UHT / ForceUnity / 链接 | 已通过 |
| fresh NullRHI 纯数据自动化 | 已通过，3/3 |
| `L_ABTS_M11` 真实 RHI 自动截图 | 待用户显式运行与美术检查 |
| 独立 ProfileGPU 基线 | 待用户显式运行与数据归档 |
| T1 Style On 像素差异 | 尚未实现，不能提前验收 |
