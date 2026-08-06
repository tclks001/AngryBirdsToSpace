# ABTS 三渲二 T2-C1 无 M7 动态与画中画视觉回归

> 状态：2026-08-06 实现与自动门完成；ForceUnity、NullRHI、M3/M11 语义回归及 DX11 Off/On 像素/录制门均通过，等待用户视觉验收。M7 仍按约定留给 T2-B2/T2-C2。
>
> 上游：[三渲二总设计](ABTSToonStylizedRenderingDesign.md) · [T0 自动视觉基线](ABTSToonVisualCaptureT0.md) · [T2-B1 选择性语义与画中画](ABTSToonStylizedRenderingT2B1.md)

## 1. 目标与边界

T2-C1 在 M7 `Beam-C3` 尚未完成时，把非建筑部分从“可见 PIE 手工检查”升级为可重复的真实 RHI 像素证据：

- 地面落点与月面 E5 落点使用生产 `AABTSM101LandingPreviewCamera`、真实 CaptureSource、RenderTarget 和风格化 ViewClass；
- 终局远端预览只读取 M11 正常瞄准链已经刷新过的 `FinaleRemotePreview` RenderTarget；
- M11 动态飞行继续使用独立命令行录制器，Style Off/On 的 Rank、轨迹与 Playback Hash 必须一致；
- 主视图 Screen Percentage 继续复用 T0 四点，不复制第二套世界/相机目录；
- 所有输出固定标记 `authority=PreviewTest`、`m7AdapterReady=false`，不等待 M7 IdleValidation，不发布或修改玩法结果。

本阶段不捕获建筑破坏，不给砖块猜测主体/弱点语义，不冻结包含完整六建筑的最终 GPU 预算。上述内容留给 T2-B2/T2-C2。

## 2. 架构

```text
L_ABTS_M11 / 固定 Seed 312503
  ├─ LandingPreviews slice
  │    ├─ M3 初始道路表面 -> 确定性强化弹弓终端夹具
  │    ├─ M3 月度卫星/E5快照 -> 确定性月面终端夹具
  │    └─ 生产 LandingPreviewCamera -> Ground/Satellite RT PNG
  │
  ├─ FinaleRemotePreview slice
  │    └─ M11 CameraCapture 正常瞄准 -> 已刷新 RemotePreview RT PNG
  │
  └─ M11 CameraCapture
       └─ Rank11 全程 JPG/MJPEG AVI -> 动态轮廓与构图证据
```

`UABTSToonT2C1CaptureSubsystem` 只有显式 `-ABTSToonT2C1Capture` 或 `-ABTSVisualCaptureSuite=ToonT2C1` 时才创建。Landing slice 自己拥有瞬态预览相机和临时 Style/Profile，并在终止时恢复；Finale slice 不调用 `CaptureScene`、不改变 M11 输入，也不拥有、设置或恢复全局 Style/Profile，只观察 M11 录制器已经建立的 `FinaleSpace` 状态与 `TargetCaptureCount>0` 后的实际 RenderTarget。普通游戏零 Tick、零对象、零读回。

Finale 的 Style 生命周期唯一所有者是 `AABTSM11FinaleCameraCaptureRunner`。录制器每帧验证 `Enabled` 与 `FinaleSpace` Profile；任何中途漂移都以 `StylizedRuntimeStateDrift` fail closed，禁止把“初始化时启用、实际中途关闭”的 AVI 写成成功证据。

## 3. 确定性夹具

地面/月面夹具不是认证轨迹，也不进入 M6 发射：它们只构造 `FABTSM6TrajectoryPreview` 的表现字段，调用生产预览相机。输入全部来自已提交语义数据：

- Ground：从 `GetInitialRoadSpawnTransform` 沿道路 Forward 前推 1600 cm，再以 `QuerySurface` 回落到真实连续地表；这样保持 M3 确定性且避开出生点鸟群遮挡，Up 使用查询法线，入射切向仍来自道路 Forward；
- Satellite：月度 Runtime Snapshot 的卫星中心/半径、E5 Transform/半尺寸和冻结发射帧 Forward；夹具终点采用 E5 径向外表面而不是 Actor 中心，以匹配扫掠碰撞点语义并避免镜头进入大型代理立方体；
- 两种夹具都量化到 0.1 cm 后计算 64 位 Fixture Hash；Style Off/On 必须相同；
- 退化 Up、切向方向、半径或 NaN 一律 fail closed。

这条链验证的是“相同落点和相机下的 PIP 像素”，不冒充“玩家通过鼠标瞄准得到同一轨迹”。Gameplay 轨迹、碰撞和引力仍由既有 M6/M9 回归负责。

## 4. 命令行入口

推荐使用集成脚本：

```powershell
& 'C:\workspace\AngryBirdsToSpace\Scripts\ToonT2C1.ps1' `
  -Slice LandingPreviews -Style On -ScreenPercentage 100

& 'C:\workspace\AngryBirdsToSpace\Scripts\ToonT2C1.ps1' `
  -Slice FinaleRemotePreview -Style On -ScreenPercentage 100 -Rank 11
```

脚本固定使用 `C:\Program Files\Epic Games\UE_5.8`、`L_ABTS_M11`、DX11 RenderOffscreen、Seed 312503、唯一输出/日志，并要求干净工作树。Landing slice 由 T2-C1 退出进程；Finale slice 把进程退出权留给 M11 录制器，避免两个 Runner 争抢生命周期。

底层参数：

| 参数 | 含义 |
| --- | --- |
| `-ABTSToonT2C1Slice=LandingPreviews\|FinaleRemotePreview` | 本进程唯一切片 |
| `-ABTSToonT2C1Stylized=0\|1` | Style Off/On |
| `-ABTSToonT2C1ExpectedSeed=312503` | 只校验，不改写世界 Seed |
| `-ABTSToonT2C1ScreenPercentage=50\|75\|100` | 显式设置并记录主视图 Screen Percentage；PIP 仍使用自身固定 ViewRect |
| `-ABTSToonT2C1WarmupFrames=<n>` | CaptureScene 或 M11 RT 刷新后的等待帧 |
| `-ABTSToonT2C1Output=<absolute>` | PNG 与 `manifest.json` 输出目录 |
| `-ABTSToonT2C1BuildId=<HEAD>` | 源码/二进制调用者声明 |
| `-ABTSToonT2C1ExitWhenDone` | 只允许 Landing slice 使用 |

## 5. Manifest 与自动门

每份 Manifest 至少满足：

- `contractVersion=2`、`status=Succeeded`；
- `buildIdentity` 等于本次干净编译 HEAD；
- `expectedWorldSeed=actualWorldSeed=312503`；
- `screenPercentage` 与运行请求相等，且 `screenPercentageCVar` 已实际生效；
- `authority=PreviewTest`、`m7AdapterReady=false`；
- Landing 恰好两条记录，Finale 恰好一条；
- 每条记录包含显式 ViewClass、PNG 路径、可解码尺寸、MD5、Fixture/Revision；
- 同一切片 Off/On 的 Fixture Hash、分辨率和世界身份相同，MD5 允许不同；
- 日志只有一个 `[ABTS][Rendering][T2-C1][Terminal] Success=1`。
- Finale 还必须验证 M11 recorder manifest 合同至少为 v4，且为 `Complete/TargetHit`、Rank/Style 一致、`stylizedRuntimeStateMaintained=true`、`stylizedRuntimeStateFailureFrame=-1`、`CaptureRevision>0`、Playback Hash 与 T2-C1 Fixture Hash 一致、AVI 存在且非空。

快速自动化过滤器：

```text
ABTS.Rendering.Toon.T2C1
```

它验证命令行 fail closed、进程退出所有权和两种夹具的确定性；NullRHI 不替代 PNG/AVI 像素。

## 6. 正式 T2-C1 证据矩阵

| 证据 | Style | Screen Percentage | 结果门 |
| --- | --- | --- | --- |
| T0 四个主视图 | Off/On | 50、75、100 | Pose/语义 Hash 一致；低比例轮廓不消失或倍增 |
| Ground PIP | Off/On | 100 | On 有协调 Tone/Outline，暗部无青黑散点 |
| Satellite E5 PIP | Off/On | 100 | 关闭捕获视图 Lighting 后背光可读；On 只增加稳定轮廓，Off/On PNG 不得像素完全相同 |
| Finale Remote PIP | Off/On | 100 | 实际 M11 CaptureRevision>0，目标轮廓可读 |
| Rank11 AVI | Off/On | 100 | Result/Playback Hash 一致；鸟/行星动态轮廓可读 |
| ProfileGPU | Off/On | 100 | 非建筑场景保持 T2-A 合计 `<=1.5 ms @1080p` |

Screen Percentage 50/75/100 只冻结主视图行为；SceneCapture 有自己的固定 ViewRect，不能把主视图 CVar 字面值误写成 PIP 内部缩放证据。

## 7. 人工视觉门

自动 PNG/AVI 生成后仍需人工检查：

1. Ground On 不得复发高密度青黑噪点；轨迹点和地形边缘清楚但不过粗。
2. Satellite On 保持月背落点局部构图，不能退回月球全貌；E5 与轨迹点可辨。
3. Finale Remote On 的当前目标不是空白或纯天空，Style Off/On 构图一致。
4. Rank11 动态帧中鸟和行星轮廓无明显锯齿/抖动；UFO 可读性仍按 M11 镜头导演独立判定。
5. 日志始终为 `M7AdapterReady=0`；建筑只有 T2-A 默认外轮廓，不得作为 T2-C1 成功项。

首版录制曾在 T2-C1 远端 PIP 提前完成后恢复为 Style Off，使中后段远景对象丢失轮廓、看似只剩天空；该生命周期问题已由合同 v2/v4 修复。修复后的 Rank 11 中后段仍采用既有 Flight Camera 远景构图，鸟、行星和 UFO 占屏较小，但轮廓全程可辨。进一步放大主体或调整导演节奏仍归属 M11 镜头导演后续优化，不再与风格状态生命周期混为一谈。

## 8. 后续 T2-C2

M7 合入 master 并发布稳定 `BuildingBody/BuildingWeakPoint` 只读语义后：

- 先完成 T2-B2 注册与恢复合同；
- 再新增建筑静态、弱点、破坏前后和运动序列；
- 重新测包含六建筑的完整 GPU 预算；
- T2-C1 + T2-B2 + T2-C2 全部通过后，才把总 T2-C 标为完成。
