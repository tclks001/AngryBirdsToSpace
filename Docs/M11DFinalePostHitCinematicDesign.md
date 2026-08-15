# M11-D 撞击后终局动画编排设计

## 1. 范围与权威边界

本阶段落实鸟物理撞击 UFO 后的 18 秒终局表演，但只提供 M11 自有的确定性编排、独立预览 Actor、命令行入口和离屏录制器。当前实现不修改共同地图、稳定契约、生产交互状态或共享音频系统，也不把 Editor Candidate 冒充正式通关。

- 证据层：`PreviewTest`。
- Gameplay Mutation：`0`。
- Map Binding：`0`。
- Production Binding：`0`。
- 输入：由接入方确认的非 Candidate、StrictCertified、Physical UFO Contact。
- 输出：五鸟/UFO/碎片/相机姿态、阶段、淡出参数和一次性语义音效提示。

编排是世界无关的纯时间求值器，不读取求解器、Released Result、轨迹或碰撞状态。因此它不能改变 Candidate/Result Hash，也不能把离屏预览当成实时 Chaos 或正式可见 PIE 证据。

## 2. 18 秒时间轴

| 时间 | 阶段 | 表演 | 相机与音效 |
| --- | --- | --- | --- |
| `0.0–1.2 s` | Impact | 四只攻击鸟从撞击点震开；白鸟困在 UFO 中并播放受伤姿态；`0.18 s` UFO 完整体切换破损体，确定性碎片爆开 | 近景后坐；`0.18 s` 金属撞击＋小爆炸，不播放 UI Confirm |
| `1.2–5.5 s` | Rescue | 破损 UFO 渐退；白鸟脱离并飞向四鸟，五只鸟进入会合站位 | 镜头从撞击点推向白鸟；`1.45 s` 暂用中性 Select 作为释放占位音 |
| `5.5–7.0 s` | Reformation | 五鸟从会合站位平滑组成等相位环形队形 | 镜头过渡到五鸟全景；`6.0 s` 四种已有鸟叫依次叠入，白鸟专属救援音留给集成工作树 |
| `7.0–14.0 s` | FiveBirdOrbit | 五鸟围绕终局中心持续飞行，每只鸟保持独立相位和朝向 | 50° 环绕全景，镜头轻微绕行 |
| `14.0–18.0 s` | Ending | 五鸟继续环绕，画面逐渐拉远；`16.5–18.0 s` 输出黑场 Alpha | `14.2 s` 才播放 UI Confirm，即用户此前听到的 ding；最终 UI/字幕/返回入口由集成工作树消费淡出参数 |

所有阶段边界与音效时刻都由 `FABTSM11FinalePostHitCinematicEvaluator` 唯一定义；一次 Tick 跨过多个时刻时，语义音效位掩码会补齐所有跨越事件，不依赖帧率。

## 3. 预览表现

独立预览 Actor 在当前游戏世界临时生成五只只读消费现有 Mesh/材质/动画的鸟、UFO 表现代理和专属相机。它运行时加载 `/Game/Destruction/GeometryCollections/BP_UFOPresentation`，按组件 Tag/名称解析 `IntactVisual` 与 `BrokenVisual`；离屏录像要求 `BrokenVisual` 必须引用 `/Game/Destruction/GeometryCollections/GC_UFO_Broken`，不再生成椭球占位碎片，也不在真实碎片不可用时伪装成功。

撞击后先对真实 Geometry Collection 解除锚定并执行 `CrumbleActiveClusters`，等待两个固定录制帧后施加径向与定向速度脉冲；关闭重力、碰撞和碎片移除，以便碎片在深空镜头内清晰展开。该过程使用固定 30 fps 的本地 Chaos 作为 `PreviewTest` 表演层，不得反向影响终局认证、轨迹身份或生产物理解算。

### 3.1 终局电影灯光

终局不再依赖共同地图单向太阳光，也不通过提高全局 `FinaleSpace` 曝光洗亮整个星空。预览 Actor 自有一组相机相对三点灯：暖色 Key 提供主体体积，冷色 Fill 托起背光面，冷色 Rim 保证黑鸟、白鸟和 UFO 与星空分离；三盏灯不投阴影，作用半径只覆盖远离地图主体的临时预览舞台。

灯位每帧从同一相机 LookAt 基底求值，五鸟绕行时不会因为朝向变化突然进入纯黑。Rescue 逐步抬升 Fill，Reunion 在 `6.0 s` 附近短暂增强 Fill/Rim，Ending 拉远时保留不少于初始 70% 的 Rim，最终明暗退出仍由 Integration 消费黑场 Alpha。

电影相机冻结手动曝光，`ExposureBias=+0.45`；SceneCapture 继续复制电影相机 PostProcess，保证交互预览和离屏录像使用同一曝光。现有共享鸟材质保持只读，本轮 `MaterialOverride=0`：关键帧已经通过灯光恢复完整颜色与面部可读性，暂不以 Emissive 抹平体积。

交互预览命令：

```text
ABTS.M11Finale.PostHitPreview [TimeScale]
ABTS.M11Finale.PostHitPreview.Stop
```

离屏录像命令：

```text
ABTS.M11Finale.PostHitCapture <AbsoluteOutputDirectory> [FrameRate] [Width] [Height] [JpegQuality]
```

`GC_UFO_Broken` 的真实叶片启用了 Nanite，因此启动 Editor-Cmd 时必须使用 `-dx12 -RenderOffscreen`；DX11 只能显示 Root Proxy，破碎后叶片不可见。录制器会检查当前 RHI，非 D3D12 时 fail closed，并在 manifest 记录 `renderingRHI`、`naniteRequired`、`realGeometryCollectionDebris` 和 `liveChaos`。录制器使用独立 `SceneCapture2D + TextureRenderTarget2D`，预热后逐帧保存 JPG，并在同一进程封装为 MJPEG AVI 和 JSON manifest。录像本身不含音轨；日志与 manifest 记录语义音效时间，交互预览会通过现有 Audio Subsystem 实际播放。录像期间注册 `FinaleCinematicCapture` 风格视图，结束后恢复全局状态并自动退出。

## 4. 集成工作树接线清单

集成工作树负责以下生产接线，本 M11 提交不代做：

1. 仅在非 Candidate、StrictCertified 且真实 `PhysicalTargetHit` 后启动导演；未认证接触继续 fail closed。
2. 把真实四鸟、白鸟表现 Actor 与 `BP_UFOPresentation` 绑定到求值结果；禁止修改 Released Result、Playback Plan 或物理轨迹 Hash。
   同时把 `EvaluateLighting()` 的相机相对 Key/Fill/Rim 和 `+0.45` 手动曝光绑定到生产电影相机；不得直接改共享 `FinaleSpace` Profile。
3. 移除/抑制 `AABTSM11FinaleInteractionSystem::CertifiedTargetHit` 当前同帧播放的 Confirm，将撞击、小爆炸、会合鸟叫和结束 Confirm 按本时间轴调度，避免撞击帧只响 ding。
4. 为白鸟救援/会合补充共享层可接受的专属 cue；M11 当前不越权新增共享音频资产或默认绑定。
5. 在 `16.5–18.0 s` 消费 `FadeToBlackAlpha`，并于完成后显示正式终局 UI、字幕或返回入口。离屏 SceneCapture 不合成这层 Integration-owned UI。
6. 生产中断、地图退出和失败恢复必须销毁导演并恢复玩家相机/音频上下文；同一命中只能启动一次。

## 5. 验收门

- 纯时间轴自动化冻结五个阶段、四个音效跨越点、UFO 完整/破损切换、白鸟释放、五鸟环形半径、碎片确定性与结束镜头。
- UE 5.8 Development Editor 普通链接与 `-ForceUnity -DisableAdaptiveUnity -NoHotReloadFromIDE` 完整链接成功。
- fresh NullRHI 运行 M11-D 新测试及既有 M11 快速门；它只证明确定性合同和生命周期，不代替画面验收。
- fresh DX12 `-game -RenderOffscreen` 生成连续 JPG、MJPEG AVI 与 manifest；manifest 必须为 D3D12/Nanite/真实 GC/实时 Chaos，抽检 Impact、真实碎片展开、Rescue、Reformation、Orbit、Ending 关键帧。
- 正式接线完成后仍需集成工作树执行可听 PIE、实时 Chaos 与生产 Authority 验收。

## 6. 本轮证据

- ForceUnity：`Saved/Logs/M11-PostHit-20260815-024529-ForceUnity-UBT.log`，完整链接 `Succeeded`。
- fresh NullRHI：`ABTS.M11D.PostHit` 1/1、`ABTS.M11C.Unit` 13/13、`ABTS.M11C.Runtime` 2/2、`ABTS.Contracts.M11PresentationAcceptance` 3/3，均无失败标记。
- fresh DX11 RenderOffscreen：`Saved/ABTSVisualCaptures/M11PostHitFinale/M11PostHitFinale-20260815-025314`，540 帧、30 fps、1280×720、18 秒，manifest `success=true`。
- AVI：`M11PostHitFinale.avi`，99,239,754 bytes；`avih.dwTotalFrames=540`、视频流 `strh.dwLength=540`、`idx1=540`，SHA-256 `2B18F17215ED5202B2737E0ED5E11CA6765534302BDA28700B2854E7B2E4986B`。
- 抽检第 0、6、45、180、300、426、539 帧：撞击、破损、救援、重组、五鸟环绕与结束拉远均可见，五鸟无阶段切换丢失。
- 录制日志明确记录 `ImpactBreak UIConfirm=0`、`Completion UIConfirm=1`；ding 不再属于撞击帧的本地编排。

曾尝试以总前缀 `ABTS.M11` 运行回归，但该前缀包含与本轮无关的 `M11B.Certification.FullInputDomain` 正式慢认证；在已完成 22 项且零失败后，经核对 PID 与当前工作树命令行，只停止该 M11 命令行进程并保留不完整日志。上述相关快速门随后分别 fresh 完整通过，不把该不完整总前缀运行计为绿灯。

### 6.1 电影灯光修订证据

- ForceUnity：`Saved/Logs/M11-PostHitLighting-20260815-031109-ForceUnity-UBT.log`，完整链接 `Succeeded`。
- fresh NullRHI：`ABTS.M11D.PostHit` 1/1、`ABTS.M11C.Unit` 13/13、`ABTS.Rendering.Toon.T2B1.SceneCaptureRegistry` 1/1，均无失败标记。
- 正式 DX11 RenderOffscreen：`Saved/ABTSVisualCaptures/M11PostHitFinaleLighting/M11PostHitFinaleLighting-Final-20260815-031253`，manifest 明确记录 `CameraRelativeThreePoint`、`fixedExposureBias=0.45`、`materialOverride=false`、`productionBinding=false`。
- AVI：540 帧、30 fps、1280×720、18 秒、100,104,450 bytes；`avih.dwTotalFrames=540`、视频流 `strh.dwLength=540`、`idx1=540`，SHA-256 `B9F4D7F5B1A979EEF7967E9BC6E5529FD1E6EBA060DB4B41B52DDA6E383514CB`。
- 同帧 A/B 抽检第 45、180、300、426、539 帧：初版只剩亮色高光条的鸟体已经恢复完整颜色、脸部和翅膀；黑鸟保留冷色轮廓与明暗体积，星空没有因曝光托起而洗白。第 539 帧仍保持主体可读，最终黑场继续由 Integration 合成。

### 6.2 真实 UFO 碎片修订证据

- 资产读取确认 `BP_UFOPresentation.BrokenVisual` 是 `/Script/GeometryCollectionEngine.GeometryCollectionComponent`，唯一引用 `/Game/Destruction/GeometryCollections/GC_UFO_Broken`；GC 有 18 个 Transform、使用 `MI__UFO`，并启用 Nanite。
- 预演已删除 14 个椭球占位碎片，改为真实 GC 的解除锚定、Crumble、两帧等待及径向/定向速度脉冲；重力、碰撞和碎片自动移除保持关闭。
- ForceUnity 全链接 `Succeeded`；fresh `ABTS.M11D.PostHit` 精确 1/1 成功，日志 `Saved/Logs/M11-PostHitRealGC-20260815-042917-Automation.log`。
- DX11 负向门正确以 `RealGeometryCollectionDebrisRequiresD3D12Nanite-ActualRHI=D3D11`、0 帧和 `success=false` 退出，不再把真实叶片不可见误报为成功。
- 正式 DX12/Nanite RenderOffscreen：`Saved/ABTSVisualCaptures/M11PostHitFinaleRealGC/M11PostHitFinaleRealGC-Final-20260815-043044`，540 帧、30 fps、1280×720、18 秒；manifest 为 schema 4，明确记录 `renderingRHI=D3D12`、`naniteRequired=true`、`realGeometryCollectionDebris=true`、`liveChaos=true`、`productionBinding=false`。
- AVI 为 100,575,952 bytes，SHA-256 `A9A4BB0BF529DA1DF8D1B625FB482E35903BCF405004C8B10167279EC01F2053`。抽检第 4、9、18、27、45 帧确认完整 UFO 切换为带真实裂缝、材质和青色灯带的外壳块并逐步散开；没有黑色椭球占位物。
