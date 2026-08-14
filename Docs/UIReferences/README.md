# ABTS UI Reference Set

本目录保存可追踪的共享 UI 视觉目标，不保存运行时 `.uasset`。机器可读默认值和控制台变量见 `Source/ABTSRuntime/Public/UI/ABTSUITheme.h` 与 `Source/ABTSRuntime/Private/UI/ABTSUITheme.cpp`，完整规则见 `Docs/ABTSSharedUIThemeDesign.md`。

| 文件 | 内容 |
| --- | --- |
| `ABTS_UI_MasterStyleBoard_v001.png` | 主风格板：色板、面板、槽位、肖像环与轨道信息语言。 |
| `ABTS_UI_ComponentStates_v001.png` | 组件状态板：normal/focus/selected/held/disabled/success/warning/danger。 |
| `ABTS_UI_M5HUDTarget_v001.png` | GroundStart 实景上的常驻 HUD 目标。 |
| `ABTS_UI_M11ConsoleTarget_v001.png` | FinaleLayout 实景上的终局控制台信息架构目标。 |

边界：

- 前两张定义视觉语言，后两张验证它在明亮与深色背景上的适配。
- 图中 AI 生成的小字、具体图标和像素不能视为稳定契约。
- 任何正式运行时贴图必须另建资产卡、明确唯一写入者、导入路径、透明边缘处理与许可证记录。
- 当前 v001 对应 Frozen Theme v1；未来若视觉发生实质变化，提升 Theme 版本并新增参考图版本，不覆盖历史图。
