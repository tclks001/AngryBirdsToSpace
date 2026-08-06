# ABTS 三渲二 T2-A 主视图描边与共享语义契约

> 状态：已于 2026-08-05 通过自动门槛与用户可见 PIE 验收并合入 `master`。初版 `b37d792f7835da107d2bdd50f7e533e32e79ee5a` 的 Tonemap 后硬四邻域描边曾出现锯齿和时域抖动；版本 3 的源码提交 `130be2ab9ab50e462af0515f6cacd29dcba58647` 使用 TSR 前连续覆盖描边与 Tonemap 后色调两个通道。实现版本 7 进一步把背景天际线、几何遮挡和法线褶皱分层，但 2026-08-06 用户可见 PIE 证明远端地形粗褶皱仍然存在；该问题以 `TOON-T2A-002` 保持开放，暂停继续修改轮廓参数，等待 T4 光照模型调整后联合复查。
>
> 上游：[三渲二与全局风格化渲染设计](ABTSToonStylizedRenderingDesign.md) · [T1 全局色调原型](ABTSToonStylizedRenderingT1.md) · [T0 自动视觉基线](ABTSToonVisualCaptureT0.md)

## 1. 阶段结论与边界

T2-A 在已经通过美术验收的 T1 全局色调上增加最终主视图的屏幕空间轮廓，并冻结 M3、M7、M11 后续可消费的对象类别和视图类别。它不改 Gameplay、地图、材质、Blueprint、Scene Capture、Custom Depth 项目设置或功能工作树源码。

本轮刻意不把主视图回调自动套到画中画。`FSceneView::bIsSceneCapture`、反射捕获和 Planar Reflection 全部跳过；地面落点、月面落点和终局远端预览必须在 T2-B 由 Integration 依据显式视图类别逐个接线。这样可以防止 T1 那种“全局回调也无条件处理隐藏捕获”的隐式继承，也不会在尚未验证 Capture Source、曝光和深度缓冲时改变现有 PIP。

T2-A 也只预留 Stencil 语义及数字分区，不让任何 Actor 写入 Custom Depth/Stencil。选择性对象注册、项目级 `r.CustomDepth` 成本与弱点强化留给 T2-B；Style Off 因而继续没有额外全屏 pass、Custom Depth producer 或 Scene Capture 成本。

## 2. 共享消费契约

功能工作树只能发布 `EABTSStylizedObjectClass`，不得保存、复制或直接设置 Stencil 数字：

| 类别 | 语义 | T2-A 分配 |
| --- | --- | --- |
| `None` | 未参与风格语义 | 不写 Stencil |
| `WorldSurface` | 主星地表、道路、水域等大面积表面 | 不写 Stencil |
| `BackgroundProp` | 普通树石和非关键背景物 | 不写 Stencil |
| `PlayerBird` | 当前或编队小鸟 | Integration 保留值 1 |
| `Slingshot` | 当前普通/强化/太空弹弓 | Integration 保留值 2 |
| `BuildingBody` | M7 建筑主体 | Integration 保留值 3 |
| `BuildingWeakPoint` | M7 已认证弱点 | Integration 保留值 4 |
| `SatelliteTarget` | M9 卫星及练习目标 | Integration 保留值 5 |
| `FinalePlanet` | M11 三颗助推行星 | Integration 保留值 6 |
| `FinaleUFO` | M11 终局 UFO | Integration 保留值 7 |

`1..31` 是 Integration 的风格渲染保留区。T2-A 的 `ResolveStencilValueForRenderer()` 只供渲染器与自动化核验；M3/M7/M11 不得把返回值写入自己的合同、Snapshot、Hash 或资产默认值。将来即使数字重排，功能合同仍只比较语义枚举。

视图类别为：

| 类别 | Profile | T2-A 像素消费 |
| --- | --- | --- |
| `MainWorld` | 使用运行时当前 Profile | 已实现 Tone + Outline |
| `GroundLandingPreview` | 固定 `GroundDay` | T2-B 接线 |
| `SatelliteLandingPreview` | 固定 `SatelliteGuide` | T2-B 接线 |
| `FinaleRemotePreview` | 固定 `FinaleSpace` | T2-B 接线 |

`FABTSStylizedViewPolicy` 是只读解析结果，记录色调、轮廓和选择性 Stencil 的意图。Scene Capture 必须显式声明类别；不能根据地图名、相机位置、Actor 类名或当前玩家 Profile 猜测。

## 3. 主视图像素链

```text
abts.Rendering.Stylized.Enabled / Profile
  -> FABTSStylizedRenderingContract::ResolveViewPolicy(MainWorld)
  -> AfterDOF（TSR/TAA 之前）ABTS Stylized OutlinePreTSR
       1. Scene Depth 八邻域外轮廓
       2. GBufferA World Normal 八邻域大折痕
       3. 邻域响应累积为连续覆盖率，不再用单点 max 形成硬台阶
  -> TSR/TAA 时域重建与抗锯齿
  -> Tonemap 后 ABTS Stylized Tone
       1. T1 柔和三档色调
  -> UI / HUD 后续合成
```

初版把轮廓放在 Tonemap 后，已经晚于 UE 5.8 的 TSR/TAA：Temporal Jitter 每帧改变 Depth/Normal 边缘落在哪个最终像素，而硬四邻域 `max` 又把很小的采样变化放大为 0/1 跳变，因此静态截图虽能显示轮廓，运动时仍会锯齿和抖动。版本 3 把只依赖 HDR Scene Color、Scene Depth 和 GBuffer Normal 的轮廓移到 `AfterDOF` 扩展点，让后续 TSR/TAA 共同稳定几何与线条；色调仍留在 Tonemap 后，避免改变 T1 已验收的显示空间观感。

轮廓宽度从最终 Viewport 像素换算为当前内部渲染像素，并在低 Screen Percentage 下保留 `0.75` 内部像素下限，避免线条完全丢失或随分辨率成倍变粗。深度使用相对差异，避免同一个斜面仅因距离较远就被整体描线；法线只强化明显折角。天空没有有效 Scene Depth，中心天空像素不参与轮廓，物体邻接天空时只从物体侧形成外轮廓。颜色使用深蓝灰乘入当前 HDR Scene Color，而非在 TSR 前直接写固定 LDR 黑色，避免低模资产变成硬质剪影。

稳定性优先于省掉一次全屏读写，因此版本 3 明确拆分 Tone 与 Outline。`StyleImplementationVersion=3` 是修复后的运行身份；`Enabled=0` 时两个通道都不订阅。Outline pass 必须显式绑定 `FViewUniformShaderParameters`，因为 UE 的 `ViewportUVToBufferUV` 在真实 RHI Shader 中消费该 uniform；NullRHI 不绘制，不能替代这项验证。

### 3.1 版本 7：天际线、遮挡与褶皱分层

版本 6 以前，每个邻域样本把深度边缘和法线边缘直接取 `max`，再共用一套强度。低模主星在掠射角下的相邻三角面法线差因此可能得到与地形—天空边界接近的权重，表现为远端坡面上过强的深色折痕。

版本 7 保持原有八方向采样和 TSR 前执行位置，但把覆盖率拆为三层：

1. 邻域没有有效 Scene Depth：背景天际线，使用最强权重；
2. 两侧都有几何且相对深度超过阈值：遮挡轮廓，使用中等权重；
3. 深度连续而法线超过阈值：坡面或低模折角，使用最低权重。

选择性 Stencil 仍独立取最大值，鸟、弹弓、建筑、卫星和终局目标的玩法轮廓不依赖地形褶皱权重。`WorldSurface` 继续不写 Custom Depth，避免为了地形分类额外重绘整颗程序化主星。版本 7 不新增 Scene Texture 采样，只增加少量覆盖率分类运算。

2026-08-06 可见 PIE 的结论是：上述分层是仍可保留的轮廓结构基础，但不是粗褶皱问题的完整修复。新截图中深色线条仍沿远端地形起伏成组出现，候选根因改为“量化后的光照/阴影明暗边界与屏幕空间轮廓在相近位置叠加”，而不是单独的法线 Coverage 过强。该判断目前只是视觉证据支持的假设，尚未通过 Tone-only、Outline-only、阴影关闭和 Lighting-only 对照矩阵确认。T2 阶段不再继续降低褶皱强度或调整阈值，以免在光照贡献尚未隔离时破坏坡度提示和其他对象轮廓；正式诊断与最终参数冻结转交 Phase T4。

## 4. 冻结候选参数

| Profile | 宽度 px | 深度阈值 / 软区 | 法线阈值 / 软区 | 天际线 / 遮挡 / 褶皱强度 |
| --- | ---: | --- | --- | --- |
| `GroundDay` | 1.25 | 0.012 / 0.018 | 0.16 / 0.18 | 0.92 / 0.64 / 0.22 |
| `SatelliteGuide` | 1.20 | 0.010 / 0.016 | 0.14 / 0.17 | 0.82 / 0.68 / 0.28 |
| `FinaleSpace` | 1.40 | 0.009 / 0.015 | 0.13 / 0.16 | 0.86 / 0.72 / 0.30 |

这些仍是美术候选，不是 Blueprint 默认值。三层强度必须保持“天际线 > 遮挡 > 褶皱”；调整任何数值时必须重跑四点 A/B 和 GPU 证据，不得只根据单张静态图把阈值降到会描出所有低模三角面的程度。

## 5. 验证分层

自动化过滤器为：

```text
ABTS.Rendering.Toon
```

除 T0 原有 3 项外，T2-A 新增：

- `T2A.SharedRenderingContract`：枚举合法性、七个选择类的唯一值、Integration 保留区和未知类别 fail closed；
- `T2A.ViewPolicy`：四类视图的冻结 Profile、主视图运行时 Profile、T2-A 仅实现主视图。

正式候选必须依次通过：

1. UE 5.8 `-ForceUnity -DisableAdaptiveUnity`；
2. fresh NullRHI `5/5`，终止标记 `TEST COMPLETE. EXIT CODE: 0`；
3. fresh DX12 四点 Style Off/On 截图，8/8 成功，On/Off Pose Hash 完全相同；
4. fresh DX12 ProfileGPU，Style Off 不出现风格 pass，Style On 同时出现 `ABTS Stylized OutlinePreTSR` 与 `ABTS Stylized Tone`；
5. 两个 pass 合计 `<= 1.5 ms @ 1920x1080`，其中 `OutlinePreTSR <= 1.0 ms`；
6. 用户在 `L_ABTS_M11` 可见 PIE 检查动态运动与身份可读性。

可见 PIE 重点不是再次证明编译，而是检查：鸟脸不被粗线覆盖；树石不出现全部低模三角边；建筑外轮廓和大折角可读；天空和 HUD 没有描边；相机旋转、鸟跳跃及建筑动态过程没有不可接受的闪烁。PIP 在 T2-A 必须保持原样，这一点也是验收项。

## 6. T2-B 交接

T2-B 才允许三个功能工作树分别提供只读适配器：M3 提供地表/背景物与卫星目标类别，M7 提供建筑主体/弱点类别，M11 提供助推行星/UFO 类别。Integration 在功能提交都基于同一 `master` 后串行接入 Custom Stencil 与三个 Scene Capture；功能树不得互相合并，也不得编辑共享 PP、相机资产或项目渲染设置。

T2-B 的正式门槛包括主视图选择性强化、地面/月面/终局预览的显式 Profile、Style Off 的 Custom Depth 成本核验，以及新增瞄准状态自动截图。未满足这些条件前，T2-A 的 raw Stencil 分区只是保留合同，不代表选择性描边已经实现。

## 7. 当前自动证据

### 7.1 初版版本 2（历史证据）

2026-08-05 在干净源码提交 `b37d792f7835da107d2bdd50f7e533e32e79ee5a` 上完成：

- UE 5.8 `-ForceUnity -DisableAdaptiveUnity`：`Result: Succeeded`；
- fresh NullRHI：`ABTS.Rendering.Toon` 共 `5/5` 成功，日志 `Saved/Logs/ToonT2A-b37d792-FreshAutomation.log`；
- fresh DX12 离屏截图：`8/8`，manifest 为 `Saved/ABTSVisualCaptures/ToonT0/Screenshots_20260805T070211Z_48956/manifest.json`；
- fresh DX12 GPU：`8/8`，manifest 为 `Saved/ABTSVisualCaptures/ToonT0/GPUProfile_20260805T070309Z_67776/manifest.json`，日志 `Saved/Logs/ToonT2A-b37d792-Formal-GPU.log`；
- 两个 manifest 的 Build、Seed `312503`、Catalogue Hash 及全部 requested/effective Pose Hash 对齐，`StyleImplementationVersion=2`；
- Style On 的 `ABTS Stylized ToneAndOutline` 共 12 个样本，`0.059–0.129 ms`，平均 `0.0952 ms @ 1920x1080`；Style Off 未出现该 pass。

这些文件位于本地 `Saved/`，不进入 Git。它们证明确定性、shader 冷启动与性能门槛，但不能代替动态可见 PIE 的线条闪烁、脸部可读性和主观美术判断。

### 7.2 稳定性修复版本 3

2026-08-05 在干净源码提交 `130be2ab9ab50e462af0515f6cacd29dcba58647` 上完成：

- UE 5.8 `-ForceUnity -DisableAdaptiveUnity`：`Result: Succeeded`；
- fresh NullRHI：`ABTS.Rendering.Toon` 共 `5/5` 成功并以 `TEST COMPLETE. EXIT CODE: 0` 终止，日志 `Saved/Logs/ToonT2A-130be2a-FreshAutomation.log`；
- fresh D3D12 离屏截图：`8/8`，manifest 为 `Saved/ABTSVisualCaptures/ToonT0/Screenshots_20260805T074124Z_68668/manifest.json`，日志 `Saved/Logs/ToonT2A-130be2a-Formal-Screenshots.log`；
- fresh D3D12 GPU：`8/8`，manifest 为 `Saved/ABTSVisualCaptures/ToonT0/GPUProfile_20260805T074219Z_3504/manifest.json`，日志 `Saved/Logs/ToonT2A-130be2a-Formal-GPU.log`；
- 两个 manifest 均绑定完整提交号、Seed `312503`、Catalogue Hash、实现版本 3 与 `1920x1080`；四组 Style Off/On requested/effective Pose Hash 全部一致；
- Style On 共 12 组：`OutlinePreTSR 0.039–0.106 ms`，平均 `0.075 ms`；`Tone 0.041–0.044 ms`，平均 `0.042 ms`；两者合计 `0.082–0.150 ms`，平均 `0.118 ms`；Style Off 不出现这两个 pass；
- 真实 RHI Shader 冷启动无未绑定 `View`、`Missing uniform buffer`、Fatal 或 Assertion。

自动证据证明实现位置、静态边缘、身份与预算；相机旋转、鸟移动和建筑运动时的主观抖动改善仍由可见 PIE 最终验收。
