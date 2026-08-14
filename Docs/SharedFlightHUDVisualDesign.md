# 共享发射期 HUD 视觉设计

> 状态：Theme v1 消费实现，2026-08-15 已完成离屏候选验收；不改变发射、预测、侦察或相机权威。
>
> 上游：[共享背包/合成 HUD 视觉设计](SharedInventoryCraftingHUDVisualDesign.md) · [M10 侦察小地图](M10ScoutMinimapDesign.md) · [M10.1 超视距发射界面](M101BeyondHorizonLaunchInterfaceDesign.md)

## 1. 范围与所有权

本轮由原始集成工作树串行修改共享 UI 消费端：

- `AABTSM4PartyHUD`：右侧四鸟队列及鸟头像硬绑定；
- `AABTSM10ScoutMapHUD`：侦察小地图、落点画中画、二维轨道仪表和小地图轨迹；
- `AABTSM6SlingshotSystem`：主世界预测轨迹的表现层；
- `FABTSCanvasUI`：非 Slate Canvas HUD 共用的截角面板、角标和等比例纹理绘制词汇。

这些改动只读取现有 Gameplay 快照，不改变 M6 积分器、M10 侦察资格、PIP 捕获频率、轨道投影或实际发射结果。M11 专属 HUD 和本轮之前提交的 M11 图标资产不在修改范围内。

## 2. 视觉语言

全部控件实时读取冻结的共享 `FABTSUITheme`：深海军蓝是承载层，青色表示信息、轨迹和信号有效，琥珀表示当前控制鸟及预测终点，红色仍保留给真正危险/失败语义。

- 画中画从“图片上直接压文字的矩形框”改为独立标题轨、截角外框、内层图像边框和四角取景标；RenderTarget 仍保持 `BLEND_Opaque`，避免桌面 Tonemapper 的零 Alpha 丢掉有效 RGB。Capture 内的轨迹实例改为 Theme 青色，并用第二个仅对该 Capture 可见的无碰撞球表现琥珀终点。
- 小地图保留其球面圆盘语义，但外部增加截角仪表卡片和底部状态标签；环境查询和圆盘投影不变。
- 二维轨道图保留圆形裁剪与遮挡虚线，在外部增加同族仪表卡片。轨迹使用深色衬底加青色主体，起点/终点使用琥珀焦点语义。
- 主世界预测点同样改为深色大点＋青色小点，最后一个可见预测点使用琥珀色；最多仍只消费 128 个预测采样，隔点显示后每帧最多 128 次 `DrawDebugPoint`，没有新增预测计算或 SceneCapture。

## 3. 鸟头像资产合同

四张已导入资产由 C++ 构造阶段固定解析并以 `UPROPERTY` 强引用保存，无需在 Editor 中配置 DataAsset：

| 鸟 | 固定路径 |
| --- | --- |
| Red | `/Game/Icons/Birds/T_Icon_Bird_Red` |
| Blue | `/Game/Icons/Birds/T_Icon_Bird_Blue` |
| Yellow | `/Game/Icons/Birds/T_Icon_Bird_Yellow` |
| Black | `/Game/Icons/Birds/T_Icon_Bird_Black` |

右侧队列与小地图标记共用同一绑定。纹理总是按原始宽高等比例放入可用区域，不使用圆形 UV 裁剪；资源缺失时才 fail closed 到既有鸟色占位。

`Tools/UI/InspectBirdPortraitAssets.py` 是只读资产核验工具：检查类型、尺寸、sRGB、流送/LOD/压缩设置，并把源像素导出到 `Saved/Diagnostics/M4BirdPortraitAssets/` 供人工查看。当前四张均为 128×128、sRGB，加载与 PNG 导出成功。

## 4. PIE 实时调参与命令行

以下 CVar 可直接在 PIE 控制台输入，下一帧生效；也可放入启动参数 `-ExecCmds="..."`：

| CVar | 默认值 | 作用 |
| --- | ---: | --- |
| `abts.UI.Flight.PortraitCardPx` | `78` | 右侧头像卡片尺寸 |
| `abts.UI.Flight.PortraitGapPx` | `10` | 头像卡片间距 |
| `abts.UI.Flight.PortraitRightPx` | `30` | 头像队列右边距 |
| `abts.UI.Flight.PortraitInsetPx` | `8` | 头像图像内缩 |
| `abts.UI.Flight.CutPx` | `11` | 头像/提示条截角 |
| `abts.UI.Flight.PanelCutPx` | `13` | 小地图/PIP 面板截角 |
| `abts.UI.Flight.PanelPaddingPx` | `8` | PIP 图像边距 |
| `abts.UI.Flight.HeaderPx` | `27` | PIP 标题轨高度 |
| `abts.UI.Flight.TrajectoryGlowPx` | `2` | Canvas 轨迹深色衬底增量 |
| `abts.UI.Flight.PIP.TrajectoryPointScale` | `1.0` | PIP 内青色轨迹点倍率 |
| `abts.UI.Flight.PIP.EndpointScale` | `1.45` | PIP 内琥珀终点相对倍率 |
| `abts.UI.Flight.WorldTrajectory.CoreScale` | `0.62` | 世界轨迹青色内点倍率 |
| `abts.UI.Flight.WorldTrajectory.UnderlayScale` | `1.24` | 世界轨迹深色外点倍率 |
| `abts.UI.Flight.WorldTrajectory.EndpointScale` | `1.35` | 世界轨迹琥珀终点倍率 |

运行 `abts.UI.Flight.Dump`、`abts.UI.Flight.M10.Dump` 和 `abts.UI.Flight.PIP.Dump` 可把当前解析值写入 Output Log。颜色继续通过 `abts.UI.Theme.*` 调整，不建立第二份调色板。

## 5. 自动证据与边界

构建与合同门：

- UE 5.8 Development Editor，`-ForceUnity -DisableAdaptiveUnity` 完整链接成功；
- fresh NullRHI `ABTS.UI.Flight.Contract` 为 1/1，验证四张头像路径完整且唯一、非法鸟 ID fail closed、共享截角几何稳定；
- 只读 UE Python 检查为 4/4，零错误零警告。

离屏候选截图：

- `Saved/FlightUI/FlightUI_Minimap_v001.png`：真实 M10 地图、真实鸟队伍与 M5 热栏组合；
- `Saved/FlightUI/FlightUI_Instruments_v001.png`：真实 M10 HUD 上叠加明确标记为 Preview/Test 的 PIP/轨道像素夹具，用于检查尚未进入合法拉弓态时的框架、轨迹和终点配色。

捕获命令使用 `/Game/Maps/L_ABTS_M10 -game -dx11 -RenderOffscreen`，参数分别为 `-ABTSFlightUICapture=minimap` 或 `instruments`，输出由 `-ABTSFlightUICaptureOutput=<绝对 PNG 路径>` 指定。捕获夹具只在显式参数存在时揭示一次本地侦察图并自动退出；普通 PIE/Standalone 零初始化、零截图开销。

这些截图是 Preview/Test 视觉证据，不替代用户在可见 PIE 中验证真实拉弓状态、PIP 激活/隐藏时序、鼠标可读性和不同分辨率体验。
