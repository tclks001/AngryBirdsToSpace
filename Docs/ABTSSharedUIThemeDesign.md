# ABTS 共享 UI Theme 与视觉验收规范

> 状态：`Frozen Theme v1`，已于 2026-08-14 经用户 PIE 视觉验收后冻结。默认值在 C++ 中，运行时允许通过控制台变量临时调整；不使用 DataAsset，也不要求在 Unreal Editor 属性面板配置。

## 1. 目的与所有权

共享 Theme 由原始集成工作树维护，所有工作树只消费同一套语义 Token。功能工作树仍拥有自己范围内的 UI 行为与二进制资产；共享头文件、默认值、冻结版本、`Docs/UIReferences` 和未来的 `/Game/UI` 公共资产只能由集成工作树修改。

当前接入范围：

- M4 右侧小队肖像的选中环、底色和提示文字；
- M5 热栏、背包、配方、数量弹窗与状态反馈；
- M10 侦察地图、落点预览与轨道概览的公共框架、文字和轨道状态；
- M11 当前代码没有在本轮修改。后续由其唯一写入者消费共享 Theme，不允许复制一套私有色板。

Theme 是唯一机器可读真相；参考图负责视觉意图，本文负责布局、语义、可访问性与验收。任何工作树都不能只看一张图自行取色。

## 2. 当前场景证据

基线使用 UE 5.8、`L_ABTS_M11`、StyleOn、Seed `312503`、实现版本 `72`，以 `-RenderOffscreen` 获取四个语义视角：

| 视角 | 观察结果 |
| --- | --- |
| GroundStart | 粉蓝天空、薄荷绿色球面、暖琥珀木质机构、深色描边；灰色热栏与场景脱节。 |
| SlingshotBuilding | 深海军蓝天空与青绿色终止线；HUD 必须同时适配暗背景。 |
| SatelliteE5 | 黑色星空和高亮天体；信息 UI 适合青色轨道语汇，面板不能依赖纯黑背景。 |
| FinaleLayout | 大面积黑色负空间、小型绿蓝行星；终局界面应保持中心视野并控制覆盖率。 |

截图目录：

```text
Saved/ABTSVisualCaptures/UIThemeBaseline/20260814-160508/
  ToonT0_Screenshots_20260814T080533Z_18336/
```

终端证据：`Success=1`、`Records=4`、`Expected=4`、`Reason=None`。`Saved` 内容不进入版本库；可追踪的视觉目标位于 `Docs/UIReferences`。

## 3. 视觉语言

- 基底：深海军蓝、分层半透明、深色双层描边；必须在浅色行星和黑色太空上都可读。
- 形状：大轮廓、轻切角、低多边形倒角感。M5 与 M11 的现行 Canvas 组件使用真实截角多边形填充和双层轮廓；不能为追求圆角引入临时二进制资产，也不能在多边形后铺完整直角底板。
- 主操作/选中：暖琥珀；同一画面只允许一个主要视觉焦点。
- 信息/轨道/焦点：青色；不把整个界面做成高亮霓虹。
- 危险：红色；只用于失败、破坏性操作和短暂错误闪烁。
- 角色：红、蓝、黄、黑身份色保持不变；当前控制对象额外增加琥珀外环，不能只靠颜色区分。
- 文字：暖白为正文，蓝灰为次级帮助，暖黄为数量；保留文字、轮廓、图标或状态形状的冗余提示。
- 禁止：写实金属豪华 UI、全屏玻璃、密集装饰、过度渐变、无语义的霓虹、仅凭颜色表达关键状态。

## 4. C++ Theme 契约

入口：`FABTSUITheme::Get()`。每帧得到只读 `FABTSUIThemeSnapshot`；无 DataAsset、无 Config 对象、无 Editor 默认值依赖。

颜色输入采用 `RRGGBBAA` 或 `RRGGBB`，可带 `#`；非法字符串 fail closed 到对应 C++ 默认值。尺寸和透明度在读取时钳制，避免控制台输入破坏布局。

### 4.1 Frozen Theme v1 默认 Token

| 语义 | CVar | 默认值 |
| --- | --- | --- |
| 主面板 | `abts.UI.Theme.PanelPrimary` | `0B1830F2` |
| 次面板 | `abts.UI.Theme.PanelSecondary` | `162844EE` |
| 面板内描边 | `abts.UI.Theme.PanelBorder` | `293F61F5` |
| 槽位外描边 | `abts.UI.Theme.SlotBorder` | `06101FF8` |
| 普通槽位 | `abts.UI.Theme.SlotNormal` | `243752F2` |
| 手持槽位 | `abts.UI.Theme.SlotHeld` | `5E4725F6` |
| 选中槽位 | `abts.UI.Theme.SlotSelected` | `7B5D25F8` |
| 禁用 | `abts.UI.Theme.Disabled` | `253047D0` |
| 成功/可执行 | `abts.UI.Theme.Success` | `2E735EEB` |
| 警告 | `abts.UI.Theme.Warning` | `C78A27FA` |
| 危险 | `abts.UI.Theme.Danger` | `A83C42FA` |
| 错误闪烁 | `abts.UI.Theme.DangerFlash` | `E34C4FFA` |
| 主强调 | `abts.UI.Theme.AccentPrimary` | `F2BD4CFF` |
| 信息强调 | `abts.UI.Theme.AccentSecondary` | `64D7E8FF` |
| 主文字 | `abts.UI.Theme.TextPrimary` | `F4F0E5FF` |
| 次文字 | `abts.UI.Theme.TextMuted` | `ACBBD0FF` |
| 数量 | `abts.UI.Theme.CountAccent` | `FFDC70FF` |
| 肖像底 | `abts.UI.Theme.PortraitBacking` | `10192EF2` |

### 4.2 可调度量

| CVar | 默认值 | 运行时范围 |
| --- | ---: | ---: |
| `abts.UI.Theme.GlobalOpacity` | `1.0` | `0.2..1.0` |
| `abts.UI.Theme.BorderPx` | `3.0` | `1..12` |
| `abts.UI.Theme.CellInsetPx` | `3.0` | `1..12` |
| `abts.UI.Theme.HotbarSlotPx` | `78` | `52..128` |
| `abts.UI.Theme.InventoryRowPx` | `46` | `34..80` |
| `abts.UI.Theme.InventoryCellPx` | `42` | `30..76`，且不超过行高减 2 |
| `abts.UI.Theme.RecipeRowPx` | `66` | `48..104` |
| `abts.UI.Theme.TextScale` | `1.0` | `0.75..1.5` |
| `abts.UI.Theme.DebugOverlay` | `0` | `0/1`，PIE 实时 Token 调试板 |

## 5. 在 UE 中调整，不配置资产

关闭并重新打开 Editor 一次以加载包含 Theme 的最新 C++ DLL。此后在同一个 PIE 会话中不需要重启，推荐先输入：

```text
abts.UI.Theme.DebugOverlay 1
abts.UI.Theme.Set SlotNormal FF00FFFF
abts.UI.Theme.Set BorderPx 8
```

左上角会显示全部颜色、Hex 和度量；`Theme.Set` 会校验输入并在屏幕上显示 `live` 回执。洋红只是用于确认通路，确认变化后恢复并开始正式调参：

```text
abts.UI.Theme.Reset
abts.UI.Theme.DebugOverlay 1
abts.UI.Theme.Set SlotNormal 243752F2
abts.UI.Theme.Set SlotHeld 5E4725F6
abts.UI.Theme.Set PanelSecondary 162844EE
abts.UI.Theme.Set AccentPrimary F6C95AFF
abts.UI.Theme.Set GlobalOpacity 0.94
abts.UI.Theme.Set HotbarSlotPx 82
abts.UI.Theme.Dump
```

也可以继续直接输入 `abts.UI.Theme.SlotNormal FF00FFFF` 这类原生 CVar；调试板同样会在下一帧刷新。`Theme.Set` 更适合人工调试，因为它会拒绝错误 Hex、未知 Token 和非数字度量。`abts.UI.Theme.Help` 在 Output Log 打印全部 Token，`abts.UI.Theme.Reset` 恢复 C++ Frozen Theme v1 默认值。

常见可见区域与 Token 对应关系：

| 想调整的区域 | 应修改的 Token |
| --- | --- |
| 底部普通热栏格、背包普通物品行、普通小按钮 | `SlotNormal` |
| 右侧 HELD 格、当前手持物品行 | `SlotHeld` |
| 槽位最外层深线 | `SlotBorder` |
| 槽位第二层线 | `PanelBorder` |
| BAG、背包/配方内部面板 | `PanelSecondary` |
| 大背包最外层和数量弹窗底板 | `PanelPrimary` |
| 当前角色的选择外环 | `AccentPrimary` |
| 侦察图/轨道信息强调 | `AccentSecondary` |
| 数量数字 | `CountAccent` |
| 角色肖像深色底环 | `PortraitBacking` |

`PanelPrimary` 在常驻热栏上只露出很少或完全不出现；背包打开后又会被 `PanelSecondary` 内面板覆盖大部分。`AccentPrimary` 只影响当前角色的细外环。因此用相近颜色测试这两个 Token 时，很容易误判为没有生效；验证通路应优先使用 `SlotNormal FF00FFFF`。

启动时重放同一候选值：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  'C:\workspace\AngryBirdsToSpace\AngryBirdsToSpace.uproject' `
  '/Game/Maps/L_ABTS_M11' -game `
  '-ExecCmds=abts.UI.Theme.PanelPrimary 0B1830E8,abts.UI.Theme.AccentPrimary F6C95AFF,abts.UI.Theme.BorderPx 4,abts.UI.Theme.Dump'
```

`-ExecCmds` 内的多条命令必须用逗号分隔；分号会被字符串型颜色变量当成值的一部分。`abts.UI.Theme.Dump` 会打印版本、冻结状态、全部解析后的颜色和度量。视觉验收时保存这一行；不要只提交截图而丢失参数身份。

## 6. 参考图组

| 文件 | 用途 | 约束级别 |
| --- | --- | --- |
| `ABTS_UI_MasterStyleBoard_v001.png` | 全局色彩、层级、形状和轨道语汇 | 视觉总参考 |
| `ABTS_UI_ComponentStates_v001.png` | 槽位、按钮、肖像、提示、轨道和状态映射 | 组件参考 |
| `ABTS_UI_M5HUDTarget_v001.png` | 在真实 GroundStart 场景上的常驻 HUD 目标 | 布局/对比度目标 |
| `ABTS_UI_M11ConsoleTarget_v001.png` | 在真实 FinaleLayout 场景上的终局控制台目标 | 信息架构目标，不授权集成树修改 M11 文件 |

这些 PNG 是设计参考，不是可直接导入 UE 的最终纹理；其中的文字、图标、像素尺寸都不能绕过本文与 C++ Token 成为新契约。

## 7. 验收与冻结

冻结前候选阶段：

1. 使用同一个 Seed、Profile、StyleOn 实现版本和候选 CVar 集合；
2. 至少检查明亮地表、终止线、卫星区和深空终局四种背景；
3. 在 1920×1080 检查遮挡和信息层级，再用至少一个较窄视口检查钳制；
4. 检查普通、选中、手持、禁用、成功、危险和错误闪烁；
5. 执行 `abts.UI.Theme.Dump`，把完整输出与截图一起交给集成树。

用户确认后由集成树冻结：

- 用验收 Dump 替换 C++ 默认字面量；
- `ThemeVersion` 增加为正式版本，`bFrozen=true`；
- 保留 CVar 供诊断与未来提案，但任何默认改动必须增加 Theme 版本；
- 重跑 `ABTS.UI.Theme.FrozenContract`、相关 UI 自动化、完整链接与四视角截图；
- 在版本记录中保存 Seed、渲染实现版本、Theme 版本和截图 manifest。

冻结记录：用户于 2026-08-14 在 PIE 中确认当前默认参数满意；集成工作树未改变任何已验收颜色或度量字面量，将契约提升为 `ThemeVersion=1`、`bFrozen=true`。实时 CVar 继续用于诊断和未来提案，但不会持久化；任何默认值变更必须提升 Theme 版本并重新完成视觉验收。

## 8. 工作树一致性规则

- 新 UI 只能引用语义字段，禁止在功能 HUD 中新增成组硬编码色板。
- 某个功能需要新状态时，先向集成树提出 Token 需求；不得复用语义不符的颜色。
- M3/M7/M11 可以在自己的代码中消费共享头文件，但不能修改 Theme 默认值、设计规范或参考图。
- 同一 `.uasset`/`.umap` 仍遵守单写入者；本规范不会把共享 Theme 变成共享二进制写入许可。
- 合并顺序仍是功能分支吸收更新后的 `master`，功能工作树之间不直接合并。

## 9. Frozen Theme v1 验证证据

- UE 5.8 Development Editor 使用 `-ForceUnity -DisableAdaptiveUnity -NoHotReload` 完整链接成功。
- 冻结前 fresh NullRHI `ABTS.UI.Theme.CandidateContract` 精确发现 `1` 项并成功，日志：`Saved/Logs/UITheme-CandidateV0-FinalAutomation.log`（历史 Candidate v0 证据）。
- 非默认启动覆盖 `PanelPrimary=11223344`、`BorderPx=7` 被正确解析并由 `abts.UI.Theme.Dump` 原样打印；同轮测试成功，日志：`Saved/Logs/UITheme-CandidateV0-CVarSmoke.log`。
- fresh D3D12 `-RenderOffscreen` 四视角 `StyleOn` 精确 `4/4`，终端 `Success=1`、`Reason=None`；manifest：`Saved/ABTSVisualCaptures/ToonT0/ToonT0_Screenshots_20260814T082612Z_18996/manifest.json`。
- manifest 身份：UE `5.8.0`、1920×1080、Seed `312503`、Style 实现版本 `72`、Build Identity `UIThemeCandidateV0-6337871`。
- 人工复核：海军蓝双层槽位、暖黄数量、暖棕手持状态和琥珀肖像选中环在四类背景可读；最终切角、图标与排版精度仍以参考图为目标，尚未通过用户可见 PIE 冻结。
- PIE 实时修复验证：日志证明旧输入 `PanelPrimary=0B183000`、`AccentPrimary=F6C900FF` 已成功修改 CVar；无明显变化来自 Token 对应区域很少和新旧 Accent 相近，不是 CVar 未生效。新增 `Theme.Set`、`Theme.Reset`、`Theme.Help` 与 `DebugOverlay` 后，fresh 命令烟雾测试解析 `SlotNormal=FF00FFFF`、`BorderPx=8`、`DebugOverlay=1`，日志：`Saved/Logs/UITheme-LiveCommand-20260814-164839-Smoke.log`。
- fresh D3D12 离屏视觉验证精确 `1/1` 成功；调试板完整可见，普通热栏格同帧变为洋红且边框变为 8 px，截图：`Saved/ABTSVisualCaptures/ToonT0/ToonT0_Screenshots_20260814T085120Z_4040/01_GroundStart_StyleOn.png`，日志：`Saved/Logs/UITheme-LiveOverlay-20260814-165054-Offscreen.log`。
- 冻结前 live-setter 自动化 `ABTS.UI.Theme.CandidateContract` 精确 `1/1` 成功并带 `TEST COMPLETE. EXIT CODE: 0`，日志：`Saved/Logs/UITheme-LivePIE-20260814-164755-FreshAutomation.log`（历史 Candidate v0 证据）。
- Frozen Theme v1 使用 UE 5.8 `-ForceUnity -DisableAdaptiveUnity -NoHotReload` 完整链接成功；fresh NullRHI `ABTS.UI.Theme.FrozenContract` 精确 `1/1` 成功并带 `TEST COMPLETE. EXIT CODE: 0`，日志：`Saved/Logs/UITheme-FrozenV1-20260814-170828-FreshAutomation.log`。

## 10. M5 截角组件消费记录

- M5 热栏、背包、配方和数量弹窗已改为消费 Frozen Theme v1 的真实截角组件；默认 Token 和 `ThemeVersion=1` 均未改变。
- 16 类现行物品统一绑定 `/Game/Icons/Items` 的低多边形图标，产物、材料、库存和手持格消费同一 C++ 映射；缺图继续 fail soft 到 ASCII 名称。
- 1280×720 的 `hotbar/backpack/quantity` 三种 fresh D3D11 离屏截图已完成像素检查，详细布局、命令和冻结门见 [共享背包、合成与物品栏 HUD 视觉设计](SharedInventoryCraftingHUDVisualDesign.md)。
