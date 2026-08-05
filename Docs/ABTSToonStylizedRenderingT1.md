# ABTS 三渲二 T1 全局色调原型

> 状态：Integration 候选实现。
>
> 上游：[三渲二与全局风格化渲染设计](ABTSToonStylizedRenderingDesign.md) · [T0 自动视觉基线](ABTSToonVisualCaptureT0.md)

## 1. 本阶段范围

T1 只建立可回退的全局色调垂直切片：Style On 对最终主视图应用柔和三档亮度、冷色暗部和 Profile 调色；Style Off 不注册全屏 pass。T1 不修改现有材质、地图、Blueprint 或资产默认绑定，也不实现 Depth/Normal 描边、Custom Stencil、PIP 风格链、材质族迁移、球面雾云或星空切换；这些分别留给 T2–T4。

本轮使用项目级 Scene View Extension 与 Global Shader 承担后处理职责，而不是生成二进制 Post Process Material。原因是它可以在纯文本提交中完整审阅、自动编译和快速回退，同时仍保留安装版 UE 5.8、Default Lit、Lumen、VSM、Substrate 与现有玩法权威。若 T1 美术决策通过，T2 可按调参工作流迁移到 Material/MPC 资产，不改变 Style/Profile 契约。

## 2. 运行时链路

```text
abts.Rendering.Stylized.Enabled / Profile
  -> FABTSStylizedRenderingControl
  -> FABTSStylizedToneSceneViewExtension
  -> Tonemap 后回调
  -> ABTSStylizedTone.usf 全屏 pass
```

- `Enabled=0`：不订阅 Tonemap 回调，没有 T1 全屏 pass。
- `Enabled=1`：读取当前 `GroundDay`、`SatelliteGuide` 或 `FinaleSpace` 冻结 Profile。
- Shader 将 Tonemap 后颜色转换到线性空间，按两个柔和阈值混合三档目标亮度，再应用冷暖 Tint 和饱和度，最后输出回显示色域。
- `StyleImplementationVersion=1` 表示 Style On 已真实改变像素。
- `ABTSRender` 是唯一 `PostConfigInit` 的轻量模块，只负责及时注册 Global Shader；它不得依赖 `ABTSRuntime` 或任何 Gameplay UObject。`ABTSRuntime` 保持 `Default` 加载，并公开依赖 `ABTSRender` 的控制接口。
- View Extension 不在早加载模块的 `StartupModule` 中立即创建，而是在 `FCoreDelegates::GetOnPostEngineInit()` 后注册，避免 `GEngine` 尚未建立。Style 默认关闭时不订阅全屏 pass；但 Global Shader 属于启动期资源，缺失或编译错误必须由真实 RHI 冷启动 fail closed 暴露，不能声称可静默回退。

T1 后处理发生在 UI 合成前，因此不改变 HUD、背包、轨迹 UI 的颜色。天空仍属于 Scene Color，会暂时参与全局色调；环境排除与主视图/PIP 差异属于后续阶段。

## 3. 冻结 Profile

| Profile | 视觉意图 | 阴影阈值 / 高光阈值 | 强度 |
| --- | --- | --- | --- |
| `GroundDay` | 明亮地表、暖亮部、浅蓝暗部 | `0.22 / 0.62` | `0.72` |
| `SatelliteGuide` | 保留导航亮度、加强冷暗部 | `0.18 / 0.56` | `0.68` |
| `FinaleSpace` | 更深冷暗部、略高饱和度 | `0.15 / 0.48` | `0.74` |

这些是首轮美术候选，不是永久资产参数。修改时必须同步更新 Profile 自动化、T0 A/B 截图和 GPU 对比，不能只凭单张截图调数值。

## 4. 使用与验收

PIE 或 Standalone 中可使用：

```text
abts.Rendering.Stylized.Enabled 0
abts.Rendering.Stylized.Enabled 1
abts.Rendering.Stylized.Profile 0   // GroundDay
abts.Rendering.Stylized.Profile 1   // SatelliteGuide
abts.Rendering.Stylized.Profile 2   // FinaleSpace
```

自动化过滤器仍为 `ABTS.Rendering.Toon.T0`，但 T1 后其 Style seam 测试应报告实现版本 `1`，并验证三份 Profile 合法且互不退化。真实 RHI 必须重新运行 T0 截图与 GPU 脚本，并满足：

- 8 张图与 8 个 GPU records 均成功；同一截图点的 Off/On Pose Hash 相同；
- Style On 与 Off 产生非空像素差异，暗部有冷色但不死黑，HUD 不被调色；
- Style Off 的 GPU 图中没有 `ABTS Stylized Tone` pass；Style On 可辨认该 pass；
- T1 全屏 pass 目标 `<= 0.6 ms @ 1080p`，全局风格总增量在 T1 仅包含该 pass；
- Seed、世界合同、建筑、卫星和终局 Hash 与 T0 对齐。

可见 PIE 的美术决策不是自动化替代项。用户需在 `L_ABTS_M11` 中检查地面、建筑、卫星和终局的色阶可读性，并决定保留当前柔和色调、回调 Profile，或停止进入 T2。

## 5. 排错

| 现象 | 首查 |
| --- | --- |
| Style On/Off 无像素差异 | manifest 的实现版本是否为 `1`；CVar 是否在当前进程；shader 是否成功编译；ProfileGPU 是否出现 `ABTS Stylized Tone`。 |
| 全屏黑色或棋盘格 | `/Project/Private/ABTSStylizedTone.usf` 映射、shader 编译日志、SceneColor 输入与输出格式。 |
| HUD 也被调色 | pass 是否仍位于 Tonemap 后、Slate/UI 合成前；不得改为对最终窗口抓屏再次处理。 |
| 暗部死黑或天空严重断层 | Profile 亮度、阈值和 softness；T1 不通过增加描边掩盖色调问题。 |
| Off 仍有 GPU 成本 | View Extension 只能在 Enabled 时订阅回调；检查 RDG 事件中是否仍出现 T1 pass。 |
