# ABTS 三渲二 T2-B1 选择性语义与画中画接线

> 状态：2026-08-05 Integration 候选实现；M3、M11 及共享鸟/当前弹弓已接入，自动化与真实 RHI 候选烟测通过，等待用户可见 PIE 验收。M7 因 `Beam-C3` 长任务仍在独立工作树中，本阶段明确保持 fail closed，建筑暂时只消费 T2-A 的全局 Depth/Normal 描边。
>
> 上游：[三渲二总设计](ABTSToonStylizedRenderingDesign.md) · [T2-A 主视图描边与契约](ABTSToonStylizedRenderingT2A.md) · [T0 自动视觉基线](ABTSToonVisualCaptureT0.md)

## 1. 本阶段交付

T2-B1 在不修改 M7、不编辑地图或 Blueprint 资产的前提下，完成两条 Integration 消费链：

1. 显式对象语义写入 Custom Depth/Stencil，强化鸟、当前弹弓、卫星/E5 和终局三行星/UFO 的轮廓；
2. 显式 Scene Capture 视图注册，让地面落点、月面落点和终局远端预览各自消费冻结 Profile，而不是从地图名、相机位置或 Actor 名称推断。

本阶段不改变生成、引力、轨迹、碰撞、终局积分、候选 Hash 或玩法 Authority。M3/M11 适配器仍只发布只读语义；Stencil 数字、状态保存/恢复和视图像素策略全部由 Integration 持有。

## 2. 对象语义消费

`UABTSStylizedRenderingWorldSubsystem` 只在 Game/PIE 世界创建，并按显式权威刷新：

```text
M3 Planet / M9 卫星练习运行时 ─┐
BirdParty 当前四鸟 ─────────────┼─> 语义枚举 -> Integration Stencil 注册表
M6 当前活动弹弓 ────────────────┤                  │
M11 已提交终局表现 Actor ───────┘                  └─> T2-A TSR 前轮廓 pass
M7 ── 无适配器，fail closed
```

选择性类别继续使用 T2-A 冻结的 `1..7`：`PlayerBird`、`Slingshot`、`BuildingBody`、`BuildingWeakPoint`、`SatelliteTarget`、`FinalePlanet`、`FinaleUFO`。大面积地表、道路、树石 HISM 和未知对象不写 Stencil，防止轮廓成本与视觉噪声扩散。

注册表在第一次接管组件时保存 `bRenderCustomDepth`、Stencil 值和写入 Mask；对象离开显式集合、Style Off、世界退出或子系统销毁时恢复原状态。若组件已经被其他玩法/调试系统以不同值占用，则记录冲突并拒绝接管，不覆盖外部状态。Style Off 日志必须出现 `SelectiveProducers=0`。

当前弹弓只在 M6 发射模式活动时发布弦、两个桩和可见运行时弹袋；普通行走状态的静态槽不会被误标为“当前弹弓”。

## 3. 画中画视图接线

每个已知 `USceneCaptureComponent2D` 都注册一个只属于该组件的 `ISceneViewExtension`：

| 场景 | 显式类别 | 固定 Profile | 像素策略 |
| --- | --- | --- | --- |
| 主星落点 | `GroundLandingPreview` | `GroundDay` | Tone + Outline + 选择性 Stencil |
| 月面落点 | `SatelliteLandingPreview` | `SatelliteGuide` | Outline + 选择性 Stencil；保留既有 `SCS_BaseColor`，不重复色调量化 |
| 终局远端预览 | `FinaleRemotePreview` | `FinaleSpace` | Tone + Outline + 选择性 Stencil |

主视图的全局扩展仍拒绝所有 Scene Capture；上述效果只来自对应捕获组件的直接注册。未知捕获、反射捕获和 Planar Reflection 不会被风格系统自动接管。月面 PIP 继续使用此前为背光可读性确定的 BaseColor 路径，因此本阶段只叠加轮廓，不把它强制改回最终光照颜色。

落点相机在切换 Preview Subject 时立即注册/注销视图，避免“首帧先捕获、下一 Tick 才接线”的闪帧；M11 在主动 `CaptureScene` 前同样确保远端视图已注册。

## 4. M7 延后边界

T2-B1 不按 Actor 名、类名片段、材质、模型、位置或当前建筑状态猜测 `BuildingBody`/`BuildingWeakPoint`。日志固定输出 `M7AdapterReady=0`。因此：

- M7 建筑仍有 T2-A 的场景深度/法线外轮廓；
- 本阶段没有建筑主体和弱点的选择性加粗；
- 不修改 M7 工作树正在重构的砖块、Beam-C3、DAG、弱点或物理代码；
- M7 完成后以 T2-B2 只读适配器补入，同一渲染注册表直接消费，无需重写 Shader 或画中画链。

这是一项可观测的临时降级，不属于静默缺失。T2-B1 不得以现有建筑“看起来已经有线”冒充 M7 语义接入完成。

## 5. 自动化和诊断

快速测试过滤器：

```text
ABTS.Rendering.Toon
```

T2-B1 新增：

- `T2B1.SceneCaptureRegistry`：显式视图注册、替换、注销和未知类别 fail closed；
- `T2B1.PrimitiveRegistry`：状态接管、Style Off/离场恢复及外部 Stencil 冲突拒绝。

运行时状态只在摘要发生变化时记录：

```text
[ABTS][Rendering][T2-B1] M3Semantics=... M7AdapterReady=0 M11Semantics=...
Birds=... SlingshotPrimitives=... SelectiveProducers=... PreviewViews=...
Conflicts=... Style=...
```

正式自动证据必须使用 `C:\Program Files\Epic Games\UE_5.8`，依次通过 ForceUnity、fresh NullRHI、fresh D3D12 8/8 A/B 截图和独立 ProfileGPU。`Saved/` 证据不进入 Git。被动 T0 四点不会自动进入瞄准状态，因此 `SlingshotPrimitives=0` 在这组静态证据中是预期值，不能代替下面的可见 PIE 弹弓验收。

## 6. 可见 PIE 验收

唯一地图为 `/Game/Maps/L_ABTS_M11`，使用该地图既有 `ABTSM11GameMode`。开始前确认：

```text
abts.Rendering.Stylized.Enabled 1
```

按以下顺序检查：

1. 地面普通视图：鸟、卫星目标和可见终局对象的外轮廓比树石/道路更清楚；树石 HISM 没有选择性加粗；M7 砖块只有默认全局轮廓。
2. 进入强化弹弓瞄准：当前两根桩、弦和弹袋获得选择性轮廓，日志变为 `SlingshotPrimitives>0`；退出瞄准后恢复，不残留 Custom Depth。
3. 主星落点 PIP：画面不是黑屏，使用 `GroundDay`，目标区域与主视图风格协调。
4. 月面落点 PIP：只有落点确属月面时出现，维持现有 45 度落点构图与 BaseColor 背光可读性；月球不被错误重做成全貌预览。
5. 终局远端 PIP：进入终局发射模式后，三行星/UFO 可读，采用 `FinaleSpace`；不改变积分路径或成功集行为。
6. 控制台切到 `abts.Rendering.Stylized.Enabled 0`：下一条摘要必须为 `SelectiveProducers=0`；主视图和三个 PIP 都不再运行风格 pass，原 Custom Depth/Stencil 状态得到恢复。重新设为 `1` 后恢复。
7. 全程确认鸟切换、轨迹、月球引力、E5、终局布局/Result Hash 与发射结果没有变化。

若画中画的 Tone/Outline 在 Editor 视口正常、PIE 黑屏，或 Style Off 仍有选择性 producer，均判定本阶段未通过。可见 PIE 只负责动态观感和玩法状态；不得用它代替自动化、真实 RHI Shader 冷启动或 GPU 门。

## 7. 后续编排

- **T2-B2（等待 M7）**：M7 发布已认证建筑主体/弱点只读语义，Integration 接入现有注册表并补破坏前后恢复测试。
- **T2-C**：增加确定性的瞄准、地面/月面/终局 PIP 和建筑破坏截图状态，覆盖 Screen Percentage 与动态运动，最终冻结线宽、强度和 GPU 预算。
- 只有 T2-B2/T2-C 均完成且美术门通过，才进入第一批材质族迁移；T2-B1 本身不授权修改 M7 材质或 Blueprint 默认绑定。

## 8. 当前自动证据

2026-08-05 在干净源码提交 `8a225e843a71a7781718e63c77181bde615b6644` 上完成：

- UE 5.8 `-ForceUnity -DisableAdaptiveUnity`：`Result: Succeeded`；
- fresh NullRHI `ABTS.Rendering.Toon`：`7/7`，终止标记 `TEST COMPLETE. EXIT CODE: 0`，日志 `Saved/Logs/ToonT2B1-8a225e8-FreshAutomation.log`；
- M3 回归：`ABTS.M3.StylizedSemantics` 为 `1/1`，`ABTS.M3.Monthly.SatellitePreview` 为 `3/3`；
- M11 回归：`ABTS.M11B.Runtime` 为 `6/6`，`ABTS.M110` 为 `4/4`；
- fresh D3D12 截图：`8/8`，manifest 为 `Saved/ABTSVisualCaptures/ToonT0/Screenshots_20260805T093823Z_45872/manifest.json`，日志 `Saved/Logs/ToonT2B1-8a225e8-Formal-Screenshots.log`；
- fresh D3D12 GPU：`8/8`、每项 3 个样本，manifest 为 `Saved/ABTSVisualCaptures/ToonT0/GPUProfile_20260805T094014Z_47100/manifest.json`，日志 `Saved/Logs/ToonT2B1-8a225e8-Formal-GPU.log`；
- 两份 manifest 均绑定完整提交号、Seed `312503`、实现版本 4 与 `1920x1080`；四组 Off/On 的 requested/effective Pose Hash 分别一致；
- Style Off 没有风格 pass，且日志反复确认 `SelectiveProducers=0`；Style On 为 `M3Semantics=6`、`M11Semantics=4`、`Birds=4`、`SelectiveProducers=32`、`M7AdapterReady=0`、`Conflicts=0`；
- Style On 的 12 个正式 GPU 样本中，`OutlinePreTSR` 为 `0.072–0.133 ms`、平均 `0.1111 ms`，`Tone` 为 `0.041–0.047 ms`、平均 `0.0430 ms`，远低于 T2-A 的 `1.5 ms @ 1080p` 合计门槛；真实 RHI 无 Shader Error、Fatal 或 Assertion。

T0 的被动点没有进入弹弓瞄准，故正式日志中的 `SlingshotPrimitives=0` 是预期结果；地面/月面/终局 PIP 也不属于四张被动主视图截图。当前自动证据不能替代第 6 节的可见 PIE，候选仍等待用户验收后才能合并 `master`。
