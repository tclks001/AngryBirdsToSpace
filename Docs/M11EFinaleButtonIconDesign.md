# M11-E 终局按钮图标设计与 UE 落地

## 1. 目标与边界

M11-E 用 Image2 生成的机械象形图替换 M11-D HUD 中九个以文字为主体的按钮，同时保持既有四区布局、点击盒、输入语义、Theme v1 状态色和轨迹/PIP 合同不变。

参考图：

- `Docs/UIReferences/ABTS_UI_M11ConsoleTarget_v001.png`
- `Docs/UIReferences/ABTS_UI_ComponentStates_v001.png`
- `Saved/M11D/HudCapture_v005/M11D_Hud_1280x720_v005.png`

本阶段只新增 `/Game/M11/UI/Buttons` 资产并修改 `AABTSM11FinaleHUD`。不修改共享 Theme、Build.cs、Config、地图、UMG 或其他工作树资产。

## 2. 图标语义

| 按钮 | 图标 | 主要辨识线索 |
| --- | --- | --- |
| Select | 轨道节点靶心＋指针 | 选取轨迹点 |
| Move | 四向箭头＋轨道中心 | 平移/环绕观察 |
| Reset View | 回转箭头＋取景框 | 恢复默认视图 |
| Rebase | 三轴基准＋确认刻度 | 将冻结 PIP 基线对齐到当前点 |
| Auto PIP | 显示器＋追踪靶心 | 恢复自动目标跟随 |
| Coarse | 三枚右向箭头 | 最大调整步长 |
| Fine | 双箭头＋精度点 | 中等调整步长 |
| Ultra Fine | 单箭头进入靶心 | 最小精调步长 |
| Launch | 弹弓＋弹道箭头 | 唯一发射动作，使用琥珀强调 |

图标不含文字、字母或数字；失载时 C++ 保留原文字作为 fail-soft 回退，但正常资源路径只显示图标。

## 3. Image2 生成与源资产

- 模式：内置 Image2 / `stylized-concept`。
- 生成物：3×3 图标表，深海军蓝钢制倒角、青色交互光、象牙色高光，Launch 单独使用琥珀色。
- 背景：统一 `#00ff00` 色键；本地软遮罩、去绿边后输出 Alpha PNG。
- 生成原图：`Content/M11/UI/SourceArt/M11_ButtonIconSheet_Image2_v001.png`。
- Alpha 图表：`Content/M11/UI/SourceArt/M11_ButtonIconSheet_Alpha_v001.png`。
- 九张 256×256 源 PNG：`Content/M11/UI/SourceArt/Buttons/T_M11_Button_*.png`。

最终提示词要求九枚图标按 Select、Move、Reset View、Rebase、Auto PIP、Coarse、Fine、Ultra Fine、Launch 排列；必须是无文字、无按钮底板、彼此隔离、粗轮廓、可在 24–40 px 读取的机械象形图，并禁止主体使用色键绿。

## 4. UE 资产设置

九张纹理导入到 `Content/M11/UI/Buttons`，对应 `/Game/M11/UI/Buttons/T_M11_Button_*`。统一设置：

- 256×256、sRGB；
- `TC_EditorIcon`；
- `TEXTUREGROUP_UI`；
- `TMGS_NoMipmaps`；
- `TF_Bilinear`；
- `NeverStream=true`。

`AABTSM11FinaleHUD` 构造函数建立硬资产引用，保证 Cook 可追踪。按钮仍由 Canvas 绘制 Theme 填充和边框，再把图标按按钮高度居中合成；禁用态降低饱和和透明度，但保留轮廓可读性。

## 5. 视觉迭代结论

首轮离屏截图 `Saved/M11E/HudCapture_v001/M11E_Hud_1280x720_v001.png` 证明语义映射成立，但 29 px 高的左侧按钮中，透明留白和深色描边令非激活图标偏小、偏暗。

第二轮将图标占比从“按钮高度减 4 px”提高到完整按钮高度，启用双线性 UI 采样，并提高禁用态轮廓亮度。最终截图：

- `Saved/M11E/HudCapture_v002/M11E_Hud_1280x720_v002.png`
- 1280×720；
- Camera Capture Contract v18；
- `State=Aiming`；
- `Status=Complete`；
- `Authority=UNCERTIFIED`，仅作为 HUD 视觉诊断。

最终图中九个按钮均不再依赖文字标签；活动态仍由青色填充/粗边框表达，发射保持琥珀色唯一关键动作，禁用态可见但不抢焦点。

## 6. 验收与后续

- UE 5.8 Development Editor 完整链接成功，后续视觉调整增量链接成功。
- `ABTS.M11C.HUD.Unit` 9/9。
- `ABTS.M11D.HUD.Unit` 2/2。
- 九个 Texture2D 属性已通过 UE Python Commandlet 逐项读取并记录在 `Saved/M11E/import_results.json`。
- 正式可见 PIE 仍需人工确认鼠标悬停/按下手感与连续档位切换；离屏截图不替代该证据层。

回退时同时移除 HUD 硬引用、`Content/M11/UI/Buttons` 与对应 SourceArt；不得只删 `.uasset`，否则会触发文字回退并掩盖资源缺失。

## 7. 最终 Image2 提示词

```text
Use case: stylized-concept
Asset type: 3 by 3 game UI pictogram sheet for Unreal Engine 5 M11 finale console buttons.
Primary request: Create nine isolated, immediately readable mechanical control glyphs that match the supplied Angry Birds To Space sci-fi console references. The glyphs will replace text labels inside the existing HUD buttons.
Input images: Image 1 is the target M11 console style and composition reference; Image 2 is the authoritative component/button state style board; Image 3 is the current in-engine HUD whose text buttons will receive these icons.
Scene/backdrop: perfectly flat solid #00ff00 chroma-key background for background removal. One uniform color only, with no shadows, gradients, texture, reflections, floor plane, or lighting variation.
Subject: nine separate pictogram glyphs in this exact row-major order: Select target reticle; Move four-direction navigation; Reset View circular return; Rebase anchor/coordinate axis; Auto PIP monitor and tracking reticle; Coarse three chevrons; Fine two chevrons and precision dot; Ultra Fine one chevron entering a precision crosshair; Launch slingshot releasing a projectile along an upward trajectory arc.
Style/medium: polished 2D game UI icon art, chunky low-poly/beveled sci-fi machinery, crisp silhouette, subtle internal facets, dark navy steel edging, cyan emissive accents, warm ivory highlights; the launch icon alone uses strong amber/gold emphasis.
Composition/framing: exact 3x3 grid, equal square cells, centered icon in each cell, consistent apparent scale and stroke weight, generous separation and padding, no icon may touch another.
Constraints: no words, letters, numerals, captions, button rectangles, background plates, characters, logos or watermark. Every icon must be opaque, isolated, crisp-edged and readable at 24 to 40 pixels. Do not use #00ff00 in the glyph subjects.
Avoid: photorealism, thin hairlines, excessive detail, soft blur, bloom spilling into the green background, inconsistent cell sizing, gradients or shadows in the background.
```
