# ABTS 共享 UI 参考图 AI 使用记录

生成日期：2026-08-14。工具：Codex 内置图像生成（GPT Image 2 路径）。用途：内部 UI 设计参考；当前没有把生成 PNG 直接导入 Unreal，也没有让 AI 决定 Gameplay、点击热区、稳定契约或工作树所有权。

真实输入为 UE 5.8 离屏 StyleOn 截图：`GroundStart`、`SlingshotBuilding`、`SatelliteE5`、`FinaleLayout`。输出均保存到 `Docs/UIReferences`，原始生成输出保留在 Codex 生成目录。

## UITHEME-IMG-001 主风格板

- 输出：`Docs/UIReferences/ABTS_UI_MasterStyleBoard_v001.png`
- 输入：四张真实 StyleOn 截图，仅作为世界风格参考。
- 采用原因：同时给出明暗背景可读的海军蓝基底、琥珀主强调、青色信息强调、低多边形切角和角色身份环。
- 完整提示词：

```text
Create a polished 16:9 MASTER UI STYLE BOARD for an original low-poly space adventure game. The four input screenshots are visual-world references only, not edit targets. Derive the UI language from their soft cel-shaded geometry, thick deep-navy outlines, powder-blue and mint planetary colors, warm cream/amber wooden mechanisms, cyan/teal orbital instruments, and black starfield.

Show an organized production design system on a clean deep-navy presentation background: a compact color palette made of unlabeled swatches; layered translucent navy panels with softly faceted corners and restrained heavy outlines; normal, hover/focus, selected, held, disabled, success, warning, and danger states; inventory/hotbar slots, count badge, small button, large primary action button; circular character portrait frames that preserve red/blue/yellow/black character color semantics; cyan orbit-line and telemetry motifs; warm amber selection and primary-action glow; subtle low-poly bevel/highlight shapes that feel carved or molded, not glossy sci-fi glass.

The UI must remain readable over both bright pastel planets and deep black space. Prefer robust game-production shapes suitable for Unreal Canvas/Slate implementation. Avoid photorealism, metallic luxury UI, excessive neon, dense decorations, gradients that reduce readability, existing game logos, copyrighted characters, watermarks, and all readable text. Use abstract icon marks and purely visual samples. Crisp, coherent, high-end game UI concept art.
```

## UITHEME-IMG-002 组件与状态板

- 输出：`Docs/UIReferences/ABTS_UI_ComponentStates_v001.png`
- 输入：主风格板、GroundStart、FinaleLayout。
- 采用原因：把共享视觉语言拆成可映射到 C++/Canvas/Slate 的状态族，并验证亮暗背景方向。
- 完整提示词：

```text
Create a polished 16:9 GAME UI COMPONENT AND STATE REFERENCE SHEET for the same original low-poly space adventure UI shown in input 1. Inputs 2 and 3 establish the bright-planet and deep-space readability conditions.

On a clean deep-navy presentation surface, arrange isolated, evenly spaced component families with no readable text: hotbar/inventory square slots in normal, focus, selected, held, disabled, success, warning, and danger states; small icon buttons in normal, hover, pressed, disabled, destructive states; a large warm-amber primary action button and secondary cyan action button; circular character portrait frames in red, blue, yellow, and black identity colors, each with a clear warm-amber selected ring variant; count badges and notification pips; tooltip, compact panel, modal panel, title divider, scrollbar, progress meter; orbit-line, dotted trajectory, crosshair, location marker, signal and telemetry motifs.

Use the exact visual language of the master board: layered translucent navy, deep outline, softly faceted low-poly corners, warm amber selection, cyan information accent, cream text-placeholder bars, restrained bevel highlights. Components must look implementable with Unreal Canvas, Slate brushes, 9-slice textures, and simple materials. Keep generous gutters, consistent scale, crisp silhouettes, no scene illustration, no real words, no logos, no watermark, no copyrighted characters.
```

## UITHEME-IMG-003 M5 HUD 目标

- 输出：`Docs/UIReferences/ABTS_UI_M5HUDTarget_v001.png`
- 输入：GroundStart 实景、主风格板、组件状态板。
- 采用原因：在不改变 3D 场景的前提下，直接证明共享风格可替换灰色热栏与简陋肖像环。
- 完整提示词：

```text
Edit input 1 into a TARGET IN-GAME HUD MOCKUP for the same original low-poly space adventure. Input 1 is the edit target; inputs 2 and 3 are strict UI style references.

Preserve the entire 3D scene, camera, sky, planet, slingshots, structures, birds, lighting, and composition from input 1. Change only the UI overlay: redesign the bottom inventory hotbar as a compact layered deep-navy strip with softly faceted slots, dark double outlines, subtle cyan focus edges, and one warm-amber held/selected state; keep the same approximate bottom-center footprint and do not obscure gameplay; redesign the four right-side circular character selectors using the red, blue, yellow, and black identity frames from the component sheet, with only the controlled red character receiving a warm-amber outer selection ring; use cream primary text, muted blue-grey helper text, and warm yellow count badges; include actual inventory item icon silhouettes where useful, but keep the information hierarchy equivalent to the original HUD; ensure strong readability over the bright pastel planet without becoming glossy or neon.

This is a production UI reference image, not a marketing illustration. No new menus, no dialogue, no logos, no watermark, no copyrighted characters beyond what is already present in the edit target. Avoid changing any world pixel except where the replacement UI overlay covers the original UI. Keep any text minimal and legible; do not invent long labels.
```

## UITHEME-IMG-004 M11 终局控制台目标

- 输出：`Docs/UIReferences/ABTS_UI_M11ConsoleTarget_v001.png`
- 输入：FinaleLayout 实景、主风格板、组件状态板。
- 采用原因：把共享 Theme 扩展到深空、轨道图、参数控制和主行动层级；仅作 M11 唯一写入者的参考。
- 完整提示词：

```text
Edit input 1 into a TARGET FINALE FLIGHT-CONSOLE HUD MOCKUP for the same original low-poly space adventure. Input 1 is the edit target; inputs 2 and 3 are strict shared-UI references.

Preserve the black starfield, distant green-blue planet, stars, camera, lighting, and all visible world content from input 1. Add only a restrained cockpit-like UI overlay that leaves the central planet and most of space unobstructed: left lower quadrant: a compact circular orbital overview with cyan orbit lines, launch marker, dotted predicted trajectory, and a warm-amber selected waypoint; right lower quadrant: a deep-navy telemetry/preview panel with a small picture-in-picture target viewport and three compact status/parameter rows; bottom center: three faceted adjustment controls or knob-like selectors plus one prominent warm-amber launch/confirm action; top edge: a very thin mission-status strip using cream text-placeholder bars and cyan telemetry pips; use layered translucent navy, dark double outlines, softly faceted low-poly corners, cyan information accents, amber selection/action, red only for danger; maintain legibility against deep space and keep the overall overlay playful and handcrafted, not military, not glossy sci-fi.

This is a UI architecture and visual target reference, not a marketing image. Use no readable words, no logos, no watermark, no copyrighted characters. Do not alter the world except where the new transparent UI covers it.
```

## 人工收口与未采用内容

- 采用：色板关系、面板层级、状态语义、肖像环、轨道/遥测图形和总体覆盖率。
- 未直接采用：AI 图中的具体文字、随机图标、精确像素、角色细节、按钮功能与所有 Gameplay 数值。
- 人工实现：C++ Token、输入钳制、错误回退、Canvas 绘制顺序、热区、自动化与冻结流程。
- 若未来把某个生成元素制成正式贴图，必须新增独立资产卡并记录透明处理、人工编辑、最终 UE 路径和授权复核。
