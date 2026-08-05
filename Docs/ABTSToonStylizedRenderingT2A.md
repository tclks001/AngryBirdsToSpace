# ABTS 三渲二 T2-A 主视图描边与共享语义契约

> 状态：Integration 候选实现；自动化与真实 RHI 证据待本分支冻结后补齐。
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
  -> Tonemap 后 Scene View Extension 回调
  -> 同一个 ABTS Stylized ToneAndOutline 全屏 pass
       1. T1 柔和三档色调
       2. Scene Depth 四邻域外轮廓
       3. GBufferA World Normal 四邻域大折痕
  -> UI / HUD 后续合成
```

轮廓宽度以最终 Viewport 像素定义，再转换为 Scene Buffer UV，因此 100% 与非 100% Screen Percentage 不会因直接使用内部 Buffer 像素而成倍变粗。深度使用相对差异，避免同一个斜面仅因距离较远就被整体描线；法线只强化明显折角。天空没有有效 Scene Depth，中心天空像素不参与轮廓，物体邻接天空时只从物体侧形成外轮廓。颜色使用深蓝灰而非纯黑，避免低模资产变成硬质剪影。

Tone 与 Outline 合并为一个全屏 pass，避免额外 Scene Color 中间纹理和第二次全屏读写。`StyleImplementationVersion=2` 是 T2-A 的运行身份；`Enabled=0` 时不订阅回调。

## 4. 冻结候选参数

| Profile | 宽度 px | 深度阈值 / 软区 | 法线阈值 / 软区 | 强度 |
| --- | ---: | --- | --- | ---: |
| `GroundDay` | 1.25 | 0.012 / 0.018 | 0.16 / 0.18 | 0.78 |
| `SatelliteGuide` | 1.20 | 0.010 / 0.016 | 0.14 / 0.17 | 0.82 |
| `FinaleSpace` | 1.40 | 0.009 / 0.015 | 0.13 / 0.16 | 0.86 |

这些仍是美术候选，不是 Blueprint 默认值。调整任何数值时必须重跑四点 A/B 和 GPU 证据；不得只根据单张静态图把阈值降到会描出所有低模三角面的程度。

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
4. fresh DX12 ProfileGPU，Style Off 不出现风格 pass，Style On 出现 `ABTS Stylized ToneAndOutline`；
5. `ToneAndOutline <= 1.5 ms @ 1920x1080`，其中轮廓设计预算仍以 `<= 1.0 ms` 为目标；
6. 用户在 `L_ABTS_M11` 可见 PIE 检查动态运动与身份可读性。

可见 PIE 重点不是再次证明编译，而是检查：鸟脸不被粗线覆盖；树石不出现全部低模三角边；建筑外轮廓和大折角可读；天空和 HUD 没有描边；相机旋转、鸟跳跃及建筑动态过程没有不可接受的闪烁。PIP 在 T2-A 必须保持原样，这一点也是验收项。

## 6. T2-B 交接

T2-B 才允许三个功能工作树分别提供只读适配器：M3 提供地表/背景物与卫星目标类别，M7 提供建筑主体/弱点类别，M11 提供助推行星/UFO 类别。Integration 在功能提交都基于同一 `master` 后串行接入 Custom Stencil 与三个 Scene Capture；功能树不得互相合并，也不得编辑共享 PP、相机资产或项目渲染设置。

T2-B 的正式门槛包括主视图选择性强化、地面/月面/终局预览的显式 Profile、Style Off 的 Custom Depth 成本核验，以及新增瞄准状态自动截图。未满足这些条件前，T2-A 的 raw Stencil 分区只是保留合同，不代表选择性描边已经实现。
