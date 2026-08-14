# ABTS 共享背包、合成与物品栏 HUD 视觉设计

> 状态：`Visual Candidate v1`。共享 Frozen Theme v1 的默认 Token 未改变；候选组件已通过 UE 5.8 离屏像素检查，仍等待玩家在可见 PIE 中确认交互手感后冻结组件版本。

## 1. 目标与所有权

本设计由原始集成工作树维护，覆盖 `AABTSM5InventoryHUD` 及其 M10 派生 HUD。目标是在不改库存、手持和合成玩法语义的前提下，把旧文本方框替换为与 M11 控制台一致的截角矩形、双层轮廓和青色/琥珀状态语言。

颜色与度量只消费 `FABTSUITheme::Get()`；默认值继续硬编码在 C++，运行时使用现有 `abts.UI.Theme.*` 命令调整，不创建 DataAsset，也不要求在 Editor 属性面板配置。

## 2. 参考图与场景适配

设计共同消费以下参考图：

| 参考图 | 本页面消费内容 |
| --- | --- |
| `Docs/UIReferences/ABTS_UI_MasterStyleBoard_v001.png` | 深海军蓝底、双层轮廓、青色信息、琥珀焦点 |
| `Docs/UIReferences/ABTS_UI_ComponentStates_v001.png` | 普通、手持、选中、成功、警告和危险状态映射 |
| `Docs/UIReferences/ABTS_UI_M5HUDTarget_v001.png` | 底部居中热栏、轻量常驻信息和场景对比度 |
| `Docs/UIReferences/ABTS_UI_M11ConsoleTarget_v001.png` | 截角控制台、分区标题、细状态轨和模态层级 |

像素验收使用 `/Game/Maps/L_ABTS_M10`、1280×720、`GroundDay` 风格化场景。UI 在浅绿地表、暖色道路和角色阴影上均保持深色轮廓，不依赖纯黑太空背景才能可读。

## 3. 图标资产合同

现有 16 枚图标均为 64×64、sRGB、低多边形手绘风格，语义清楚且同源，因此本轮不生成 AI 替代图。`FABTSM5InventoryHUDData::GetItemIconAssetPath` 是唯一映射入口；HUD 构造阶段把它们解析为 `UPROPERTY` 硬引用，运行时只读取缓存对象。

| 物品组 | 资产 |
| --- | --- |
| 原料 | `Branch`、`Stone`、`Wood`、`PlantFiber`、`MetalParts`、`CrystalCore` |
| 基础加工 | `WorkbenchKit`、`SimpleStake`、`SimpleCord`、`FurnaceKit` |
| 强化加工 | `ReinforcedStake`、`ReinforcedCord`、`Glass` |
| 太空加工 | `BridgeKit`、`SpaceStake`、`SpaceCord` |

退休的 `SpaceSlingshotPart` 明确返回空路径；任何缺图都必须回退到 ASCII 名称，不得显示随机纹理或静默复用其他物品图。

## 4. 布局与状态语言

### 4.1 常驻物品栏

- 底部安全区内居中放置单个截角外壳，青色顶线承担信息层。
- 左侧 `BAG` 是独立入口，中间固定八格，右侧 `HELD` 是独立手持格。
- 图标是第一识别层；物品名为小号辅助文本，右下数量徽标使用暖黄文字与琥珀轮廓。
- 当前手持槽同时使用琥珀底、琥珀边与 `HELD` 文本，不只靠颜色表达。

### 4.2 背包与配方

- 全屏压暗后绘制一个截角主外壳；标题、权限徽标和关闭按钮保持固定。
- 左侧使用图标卡片网格，数量不够时滚动，禁止继续缩小字体塞入全部物品。
- 右侧每个配方行包含产物图、名称、站点、材料图和 `已有/所需` 数量；细状态轨与状态文本共同表达结果。
- 720p 时九条现行配方必须全部可见；行高可在 Theme 上限内按可用高度收缩，但不低于 54 px。

### 4.3 数量确认

- 居中弹窗再次压暗底层，琥珀外轮廓建立唯一焦点。
- 产物图、名称、当前数量和最大数量位于上半部；`--/-/+ /++` 保持等距。
- `ASSEMBLE` 是唯一琥珀实心主按钮，`CANCEL` 保持中性。

## 5. 确定性布局与自动化

`FABTSM5InventoryHUDData::ResolveLayout` 只接收视口尺寸和 Theme 快照，统一产出绘制与命中盒坐标。自动化覆盖 1024×600、1280×720 和 1920×1080，验证热栏/模态框不越界、左右面板不相交、八个热栏格稳定，以及 16 个现行图标路径非空且唯一。

截角框使用三角扇直接填充八边形，再逐层绘制轮廓；不得先绘制完整矩形填充，否则四角仍会留下矩形底色，使“截角”只剩装饰线。

## 6. 离屏截图命令

三个模式分别为 `hotbar`、`backpack` 和 `quantity`：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe' `
  'C:\workspace\AngryBirdsToSpace\AngryBirdsToSpace.uproject' `
  '/Game/Maps/L_ABTS_M10' -game -dx11 -RenderOffscreen -Unattended -NoSplash `
  -ForceRes -ResX=1280 -ResY=720 `
  '-ABTSM5UICapture=backpack' `
  '-ABTSM5UICaptureOutput=C:\workspace\AngryBirdsToSpace\Saved\M5UI\Backpack.png' `
  '-ExecCmds=abts.Rendering.Stylized.Enabled 1,abts.Rendering.Stylized.Profile 0'
```

捕获入口会只在截图诊断模式中注入全部 16 类物品、选定手持物和配方状态，预热 36 帧后截图并退出；它不修改存档或正常 PIE 默认库存。

## 7. Candidate v1 证据

- UE 5.8 Development Editor `ForceUnity` 完整链接成功，同时 HUD 源文件在 Adaptive Build 中独立编译成功。
- fresh NullRHI：`ABTS.M5.UI.VisualLayout` 与 `ABTS.UI.Theme.FrozenContract` 精确 `2/2` 成功。
- fresh D3D11 `-RenderOffscreen` 三种模式均成功生成 1280×720 PNG：`Hotbar.png`、`Backpack.png`、`Quantity.png`。
- 像素复核：图标无缺失，热栏不遮挡主要角色，背包 12 个当前可见卡片与滚动条成立，九条配方全部可见，数量弹窗正确压暗并突出唯一主操作。
- Frozen Theme v1 Token 未修改；本候选只改变组件形状、信息密度、图标绑定和确定性布局。

## 8. 冻结门

用户在可见 PIE 中至少确认：热栏点击命中与图标一致、K/BAG 开关、背包滚轮、手持选择、配方状态、四档数量按钮、取消/确认，以及 1024×600 与 1920×1080 下的可读性。通过后由集成树把 `Visual Candidate v1` 改为 Frozen 组件版本；若调整 Theme 默认 Token，必须提升 Theme 版本并重跑其冻结合同。
