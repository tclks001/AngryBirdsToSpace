# ABTS：Low Poly 资产生产与 AI 使用报告工作流

> 状态：资产生产规范。本文只规定小鸟之外的低模资产如何取得、生成、清理、导入和记录；不实现 M6 弹弓代码，不替代现有球面 PCG、CellTopo 或建筑玩法设计。
>
> 已知前提：四只小鸟的模型与全套动画使用已购买的 `CuteBird` 资产包；其余资产遵循本文。比赛允许 AI 资产，但赛后必须提交全部 AI 使用行为报告。

## 1. 目标与不可突破的约束

目标是在比赛周期内建立一套统一、可复用、可追溯的低模资产库。画面追求清晰的几何轮廓、少量纯色分区、可读的受力结构，而不是高保真 PBR。

所有资产必须遵守：

1. `CellTopo` 仍是道路、水网、弹弓槽、建筑占位和可达性的唯一逻辑源；网格只是附着在逻辑位置上的表现。
2. 每个会被 HISM 大量摆放的资产必须是独立 Static Mesh、统一 Pivot、无 Skeletal Mesh、无运行时碰撞；视觉变化优先用实例旋转、缩放和 Material Instance。
3. 每个与交互、破坏、发射或建造有关的资产，必须预先定义 Pivot、Socket、碰撞和命名，不能等玩法代码完成后再反推。
4. AI 生成物不是直接交付物。它只能提供概念、轮廓或基础网格，必须经 Blender 清理并经 UE 5.8 验收后才进入 `Content/ABTS/`。
5. 不得以任何现有游戏、角色、品牌、商标或可识别作品名称作为提示词；提示词只描述原创世界观、形状、材料、颜色和功能。

建议目录：

```text
Content/ABTS/
  Art/
    Environment/       # 通用树、岩石、植被、桥、地表装饰
    Structures/        # 建筑模块、破坏件、道路设施
    Slingshot/         # 槽、桩、弦、弹丸袋、发射台
    Materials/         # 低模共享母材质与 Material Instance
  Characters/CuteBird/ # 购买的小鸟资产；保持与环境资产隔离
Docs/AIReports/        # 赛后 AI 使用报告与来源凭证
SourceArt/
  Purchased/           # 原始购买/下载包，只读备份
  AI/                  # 原始概念图、AI 导出 FBX/GLB、提示词和截图
  Blender/             # .blend 源文件
```

`SourceArt/` 不进入 UE Content Browser；只把已清理、命名和验收过的 FBX/贴图导入 `Content/ABTS/`。

## 2. 资产来源分流：先买/下载，再为玩法定制

### 2.1 可直接购买或下载：不需要 AI 生成

以下资产只要求与整体风格接近、可批量摆放或可替换；先从 Fab、Kenney、Quaternius、Poly Haven、官方 Starter Content 或许可清楚的 CC0 来源取得。下载后可以做尺寸、Pivot、材质色彩和面数清理，但不应为它们浪费 AI 生成轮次。

| 分类 | 建议资产 | 用途 | 导入后必做的收口 |
| --- | --- | --- | --- |
| 自然环境 | 树、灌木、草簇、岩石、蘑菇、漂流木 | M3 HISM 森林、河岸、道路外景观 | 统一接地点 Pivot；关闭碰撞；改为 1–3 个共享纯色材质；做 2–4 个缩放变体。 |
| 一般地表道具 | 石料堆、树枝堆、木箱、桶、矿石、路牌、栅栏 | 资源可见物、主路引导、探索点缀 | 拆成可拾取单件与仅装饰 HISM 两种；互动件另做简单碰撞。 |
| 通用道路/水域表现 | 桥板、护栏、浅滩石、路边桩、普通河石 | 已有 CellTopo 道路、水网和桥址的视觉层 | 以道路/河道切线摆放；不要在网格中编码路线逻辑。 |
| 无玩法差异的建筑填充 | 普通屋顶、窗、门、烟囱、木梁、货箱、背景废墟 | 建筑原型的非关键外观 | 保持为可替换的静态表现，不承担弱点、破坏或连锁逻辑。 |
| UI/图标参考 | 纯色圆形、基础资源符号、临时按钮图 | 原型 HUD | 可先用程序/矢量或原生 Canvas；不为原型阶段生成 3D 图标。 |

筛选时优先选择：低多边形、可商用、原始 FBX/GLB 可得、材质槽不超过 3 个、无强绑定特定世界观的资产。示例地图、蓝图控制器、后处理、音效和复杂动画状态机默认不迁移。

### 2.2 必须定制：允许 AI 辅助生成并人工编辑

以下资产的形状直接表达本项目玩法，几乎不可能从商店找到“既合适又可被弹弓/CellTopo/建筑逻辑使用”的成品。它们才进入 AI 生成管线。

| 定制资产族 | 为什么必须定制 | 推荐生产方式 | 是否需要骨骼 |
| --- | --- | --- | --- |
| 弹弓桩组 | 必须匹配一对 CellTopo 弹弓槽、桩距、发射方向与等级 | AI 概念/装饰轮廓 + Blender 手工低模、统一骨架 | 是，桩体弯曲用 3 骨骼。 |
| 弹弓弦与弹丸袋 | 必须精确连接两桩顶端和发射中心，并实时随拉力变形 | 手工 Spline/Cable 表现 + 手工低模袋；AI 仅可辅助材质/装饰 | 不做传统整条骨骼；运行时由连接点驱动。 |
| 道路弹弓泥土洞 | 必须与 CellTopo Anchor Pair、道路切线、桩根位置和坡度契合；它是预先存在的道路地貌，不是装配后出现的底座 | AI 生成低模泥土洞/夯土边缘候选 + Blender 收口；导入为无碰撞 Static Mesh | 否。 |
| 可破坏目标建筑“弱点模块” | 必须明确柱、梁、货仓、炸药桶、晶体核心和连锁关系 | 模块化手工/Geometry Nodes；AI 生成少量独特外壳或徽记 | 默认否；独立刚体/约束优先。 |
| 升级弹弓的等级语言 | 木、金属、晶体、太空终局需要同一结构逐级演化 | 先固定共用比例与 Socket，再为每级生成/编辑独特装饰层 | 桩体共用骨架。 |
| 卫星遗迹/引力走廊标志物 | 是强化弹道的视觉锚点，必须指向玩法走廊 | AI 图生 3D 取轮廓 + Blender 重拓扑、手工发光组件 | 否；旋转可由 Actor 驱动。 |
| 关键任务 Set Piece | 工作台、熔炉、桥机关、发射遗址 | 参数化主体 + AI 生成独特剪影部件 | 仅有开关门/活塞时才局部骨骼或独立组件。 |

## 3. 工具选择与调研结论

### 3.1 推荐工具职责

| 工具 | 最适合做什么 | 本项目用途 | 不应承担什么 |
| --- | --- | --- | --- |
| Sloyd | 可控模板、低模、尺寸/类别变体 | 低模岩石、遗迹小件、路牌、木质装饰、非关键建筑附属物的候选 | 需要精确 Socket、骨骼、动态弦连接的核心弹弓。 |
| Meshy | 文字/图片快速出独特体块和贴图候选 | 卫星遗迹、晶体装置、木桩雕刻、奇异货仓外壳的第一版形状 | 直接作为终稿；其自动拓扑/Pivot/材质不可直接信任。 |
| Tripo | 图生 3D 的快速多方案探索 | 先由概念图固定风格，再批量得到 3–6 个独特轮廓候选 | 统一项目尺度、碰撞、骨骼或 UE 蓝图逻辑。 |
| Blender | 最终模型控制中心 | 清网格、降面、重建 Pivot、UV、材质槽、骨骼、Socket Marker、FBX 导出 | 不需要把每块自然石头精雕。 |
| Blender Geometry Nodes | 规则化低模变体 | 砖块、木板、石墙、屋顶瓦、破损边缘、碎石簇 | 动态弹弦或真实玩法装配。 |
| UE 5.8 | 最终验证和运行时表现 | Static/Skeletal Mesh、Socket、Physics Asset、HISM、材质实例、Spline 弦、Niagara | 作为 AI 模型修复器；网格问题应回 Blender 修。 |

调研依据：Sloyd 提供文字、图片和模板驱动的可控 AI 3D 创建，并把游戏开发列为目标场景；其付费计划声明提供商业许可和插件访问。[Sloyd](https://www.sloyd.ai/) / [Pricing](https://www.sloyd.ai/pricing)。Meshy 提供文字/图像到 3D，声明可导出 FBX、GLB、OBJ 等并面向 Blender、Unity、Unreal 工作流。[Text to 3D](https://www.meshy.ai/features/text-to-3d) / [Image to 3D](https://www.meshy.ai/features/image-to-3d)。Tripo 提供文字与图像生成 3D 的快速探索路径。[Tripo](https://www.tripo3d.ai/)。实际比赛提交前必须再次核对使用当天套餐的商业授权条款，并截图保存。

### 3.2 本项目的最终决策

采用以下顺序，不采用“输入一句提示词后直接导入 Unreal”的路线：

```text
玩法规格 / CellTopo 接口
  -> 纯色概念板（可由 AI 图像或手绘产生）
  -> AI 3D 只生成 3–6 个轮廓候选
  -> Blender 选择一个、重建为低模资产
  -> 统一 Pivot / Socket / 骨骼 / 材质 / LOD
  -> UE 5.8 导入、挂到真实槽位或 HISM
  -> 记录 AI 报告证据
```

核心发射资产必须先写规格后生成；AI 不能决定桩距、弦端、鸟挂点、拉力方向或骨骼命名。

## 4. 全局低模风格与预算

### 4.1 风格契约

- 轮廓优先：从相机默认 `850 cm` 距离仍能看出木桩、金属套环、弦和弹丸袋。
- 每个资产默认 1–3 个材质槽；颜色优先用纯色、顶底渐变、顶点色或少量 Ramp，不依赖写实木纹/砖纹。
- 自然资产可 `Shade Flat`；角色与弹丸袋需要控制平滑。不要让 AI 自动法线决定项目风格。
- 颜色按资产族统一：简易弹弓为暖木色/麻绳色；强化弹弓为冷灰金属/深色强化带；太空弹弓为钢蓝、暗金属与晶体发光色。每一族中的桩、弦、袋、槽共享同一套色板。
- 只有可交互的弱点、弹弦、晶体核心和弹道提示使用明显发光或高对比色；背景资产保持低对比。

### 4.2 面数与纹理预算

| 资产类型 | 目标三角面 | 材质槽 | 碰撞 |
| --- | ---: | ---: | --- |
| HISM 草/碎石/小枝 | 40–250 | 1 | 无。 |
| HISM 树/普通岩石 | 200–1,200 | 1–2 | 无。 |
| 通用静态道具 | 300–1,500 | 1–2 | 简单 Box/Capsule 或无。 |
| 关键 Set Piece | 1,500–5,000 | 2–3 | 独立简单碰撞。 |
| 单根弹弓桩渲染网格 | 700–2,000 | 2–3 | 由角色/发射逻辑承担交互判定；桩网格不承担复杂碰撞。 |
| 弹丸袋 | 250–800 | 1–2 | 不用复杂碰撞。 |
| 弹弦单段表现 | 8–12 边截面，按 Spline 分段 | 1 | 无。 |
| 道路弹弓泥土洞 | 250–900 / 单洞 | 1–2 | **无碰撞**；道路连续地表承担地面碰撞。 |

如 AI 候选超过预算，优先手工保留外轮廓、孔洞和可读受力件，其余全部删去；不要仅依靠 UE 的自动 LOD 掩盖高面数源模型。

## 5. 弹弓视觉与资产接口：先对齐结构

### 5.1 弹弓是三件式、两桩装配物

每套弹弓必须由以下三部分清晰组成：

```text
道路左泥土洞 -- 左弹弓桩 -- 左弦半段 --\
                                      弹丸袋
道路右泥土洞 -- 右弹弓桩 -- 右弦半段 --/
```

- **两个弹弓桩**：每根是独立的竖直棍状受力件；底端插进道路上预先存在的各自泥土洞，顶端是弦端锚点。左右桩是同一资产族的镜像实例，而不是一个 Y 字叉。
- **弹弓弦**：视觉上是两段，从左右桩顶端分别连接到弹丸袋左右耳；逻辑上它们一起表达张力。不可把弦画成穿过桩顶的直线贴图。
- **弹丸袋**：位于两段弦的中央，是鸟准备发射时的承托物和拉拽控制点。它必须独立于弦和桩，方便放入鸟、后拉和释放。
- **道路弹弓泥土洞**：不是桩的一部分，也不是装配完成后替换出来的石基座。PCG 在道路上的指定 `CellTopo Anchor Pair` 预先生成两个低模夯土/泥土洞，负责给玩家提供可读的“可插桩位置”。逻辑仍由 Anchor Pair 决定桩底位置、槽轴、左右序、桩距及发射朝向；洞模型只表现洞口和受压土壤，不保存 PairId 或碰撞逻辑。

插入、拔出或升级弹弓桩时，泥土洞的网格、材质和地表状态保持不变；只出现/隐藏或替换桩、弦和弹丸袋。洞口周围可以有固定的压实土、少量碎石和草根，但不能在“空槽/装配完成”之间切换成不同样式的基座。

同一等级的两桩、两段弦和袋必须共用：主色、辅助色、边角处理、金属/纤维语言和磨损程度。升级必须首先读成“同一套装更强”，而不是四个互不相干的装饰物。

### 5.2 三个首版等级的视觉差异

| 套装 | 桩体 | 弦/袋 | 槽位 | 关键轮廓 |
| --- | --- | --- | --- | --- |
| `Simple` 简易弹弓 | 略弯的粗木桩，低边数切面，压实泥土直接抱紧桩根 | 浅麻绳或植物纤维；暖棕皮革/布质袋 | 预生成道路泥土洞，不因插桩改变 | 自然、临时、可读木纤维受力。 |
| `Reinforced` 强化弹弓 | 木芯外加金属箍、侧向加强片或铆钉 | 深色编织强化弦；金属边或深布袋 | 复用同一泥土洞；金属加固只属于桩根附属件，不能替换洞口 | 结构更直、更厚、可承受黑鸟。 |
| `Space` 钢铁太空弹弓 | 钢制分段桩、环形支撑、少量晶体导能片 | 钢索/能量纤维；晶体核心袋 | 只在终局发射遗址使用独立的道路/遗址泥土洞变体；装配前后洞口不变 | 清晰的工业弧线与能量焦点。 |

首版只需完成 `Simple` 一套并让其可拉动；后两套先复用同一接口和骨骼，在视觉上替换网格/材质即可。

### 5.3 资产命名、Pivot、Socket 与骨骼契约

| 对象 | 推荐资产名 | 根 Pivot / 原点 | 必需骨骼或 Socket | 说明 |
| --- | --- | --- | --- | --- |
| 左/右桩 Skeletal Mesh | `SK_ABTS_SlingshotPost_Simple` | 桩底中心，局部 Z 向上 | Bones：`root`、`post_base`、`post_mid`、`post_tip`；Socket：`S_PostBase`、`S_StringAnchor` | 同一网格镜像实例；`S_StringAnchor` 位于桩顶弦端。 |
| 桩体骨架 | `SKEL_ABTS_SlingshotPost` | `root` 在 `(0,0,0)` | 3 段链 | `post_base` 几乎固定，`post_mid`/`post_tip` 沿发射反方向弯曲。 |
| 弹丸袋 Static Mesh | `SM_ABTS_SlingshotPouch_Simple` | 袋中心/拉拽点 | Socket：`S_BirdSeat`、`S_LeftString`、`S_RightString`、`S_PullHandle` | 可作为 Actor Root；鸟挂在 `S_BirdSeat`，玩家拖拽用 `S_PullHandle`。 |
| 左右弦表现 | `SM_ABTS_StringSegment` 或 Spline Mesh | 本地 X 为长度方向 | 两端由运行时 `S_StringAnchor` 与袋 Socket 驱动 | 使用同一截面和材质；不直接把整条弦烘焙在桩网格中。 |
| 道路泥土洞 | `SM_ABTS_SlingshotDirtHole_Road` | 单个 CellTopo Anchor 中心 | Marker：`S_PostInsert`（可选，仅作编辑器预览） | 由 AI 候选 + Blender 收口产生的无碰撞 Static Mesh；预先随道路生成，插桩不更换网格。逻辑 PairId 不保存在网格中。 |

Blender 中的骨骼只需 4 根骨，权重应干净且渐变：底部 65–80% 受 `post_base` 影响，中段平滑过渡给 `post_mid`，顶端给 `post_tip`。不能把木桩做成布料，也不需要复杂 Physics Asset。桩体弯曲是一个由拉力归一化数值驱动的短动画或 Control Rig 目标，不是物理模拟。

UE 的 Skeleton Socket 可以作为相对骨骼的专用挂点，适合将弦、袋和后续发射特效精确附着。[Skeletal Mesh Sockets](https://dev.epicgames.com/documentation/en-us/unreal-engine/skeletal-mesh-sockets-in-unreal-engine)。

### 5.4 拉弓时的视觉状态

```text
Idle        : 两桩直立；袋位于桩距中点；两段弦轻微下垂。
LoadBird    : 鸟挂到 S_BirdSeat；袋向前/中央稳定。
Pull        : 袋沿槽轴反向移动；两段弦实时连向桩顶；两桩顶端向袋方向轻微弯曲。
Hold        : 维持最大拉伸；弦高亮/震颤可选；袋和鸟稳定。
Release     : 袋沿发射方向快速回弹；桩由最大弯曲回到直立并轻微过冲；弦短暂振动。
Recover     : 袋回到中点，桩和弦消除余振。
```

首版表现实现建议：两个 `USkeletalMeshComponent`（左右桩）+ 一个袋 Mesh Component + 两条 Spline Mesh 或 Cable Component。根据桩顶 Socket 和袋左右 Socket 每帧更新两条弦；将拉力 `0..1` 送入桩动画 Blueprint/Control Rig 的 `post_mid`、`post_tip` 旋转。这样桩体和弦可以独立替换，鸟发射时也不会把整套装配物绑死在一个骨骼资产上。

## 6. AI 辅助资产的逐件生产流程

### 6.1 先写“资产卡”，再生成

每个定制资产创建一张资产卡，存于 `Docs/AIReports/AssetCards/`：

```text
AssetId: ABTS_SlingshotPost_Simple_v001
GameplayOwner: M6 Slingshot Socket
CellTopoInterface: AnchorPair 左/右桩位；道路上已生成泥土洞；局部 +X 为发射方向；+Z 为径向 Up
Scale: 桩高 360 cm；底部直径 38 cm；顶端锚点高度 342 cm
RequiredSockets: S_PostBase, S_StringAnchor
RequiredRig: root > post_base > post_mid > post_tip
MaterialSlots: M_ABTS_Wood, M_ABTS_RopeMetal
Budget: <= 1,500 tris，<= 2 材质槽
AIAllowedRole: 概念图与木桩表面/轮廓候选；不允许决定骨架、Socket、Pivot
Acceptance: 左右镜像装入槽位；最大拉力时弦端不漂移；Default 相机下轮廓清晰
```

### 6.2 概念到候选

1. 用文字或二维图像生成工具制作**正交倾向**的概念板：正面、侧面、三分之四视角；背景纯色；不要让鸟、人物、文字、Logo 或已有 IP 出现在图中。
2. 每个资产族只选择一张主概念板，定义色板和轮廓；同组桩、弦、袋、槽均以其为准。
3. 用 Meshy/Tripo 做 3–6 个图生 3D 候选；Sloyd 用于规则的低模次要件和可变体资产。
4. 只选择一个候选作为 Blender 参考，保留其余候选与生成记录，不导入 UE。

示例提示词（可直接改颜色和等级，不含外部 IP）：

```text
original stylized low-poly slingshot post for a small spherical planet game,
single upright hardwood pole, subtly bent by tension, faceted silhouette,
warm amber wood, stone wedge at the base, rope anchor notch at the top,
simple clean geometry, no character, no text, no logo, isolated on plain background
```

```text
original stylized low-poly modular ruin device, crystal-powered launch-site marker,
large readable silhouette, charcoal steel, muted teal crystal accents,
few broad faceted planes, game prop, no character, no text, no logo, isolated background
```

### 6.3 Blender 收口：所有 AI 候选都必须经过

1. **隔离轮廓**：保留外轮廓、槽口、明显金属箍/晶体等玩法可读部分；删除浮动碎片、内部面、隐蔽背面、伪文字和无意义微细节。
2. **重新低模化**：必要时用手工重建、Decimate 后手工修边或 Quad Remesh 再简化；最终按第 4.2 节预算控制三角面。
3. **统一尺度与朝向**：Blender 使用厘米或按导出单位换算；`+Z` 是桩向上方向，预生成道路泥土洞局部 `+X` 是发射方向；`Ctrl+A` 应用 Rotation/Scale。
4. **建立 Pivot**：静态物体的 Origin 放在接地点/插槽点；袋 Origin 放在可拉拽中心；不得放在 AI 网格几何中心。
5. **材质收敛**：清除 AI 自动生成的大量 PBR 贴图与材质槽；重建为项目共享的木、石、金属、纤维、晶体 Material Instance。需要纹理时，优先单张低分辨率 BaseColor/Mask，不引入无意义 4K 贴图。
6. **UV/顶点色**：保留一个干净 UV0；如需烘焙/遮罩，保留 UV1。低模色差可用顶点色或 Material Instance 参数。
7. **碰撞命名**：静态 Mesh 如需自定义碰撞，使用 `UCX_RenderMeshName_00` 等 UE FBX 约定；纯 HISM 装饰不输出碰撞。
8. **骨骼与标记**：弹弓桩按第 5.3 节建骨，袋和槽添加 Empty/Marker 作为 Socket 参考；弦不烘焙成长网格。
9. **导出**：导出 FBX 2020.2；UE 的当前 FBX 导入管线使用该版本。[Static Mesh FBX Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-static-mesh-pipeline-in-unreal-engine) / [Skeletal Mesh FBX Pipeline](https://dev.epicgames.com/documentation/en-us/unreal-engine/fbx-skeletal-mesh-pipeline-in-unreal-engine)。

### 6.4 UE 5.8 导入、验证与版本控制

1. 先导入到 `Content/ABTS/Art/_Staging/`，不要直接替换正在被地图/HISM 引用的正式资产。
2. Static Mesh：检查单位、Forward Axis、Pivot、材质槽、UV、法线；HISM 候选设为 `Collision Enabled = No Collision`。
3. Skeletal Mesh：创建/复用 `SKEL_ABTS_SlingshotPost`，检查 4 根骨、权重、`S_StringAnchor`；不自动生成复杂 Physics Asset。
4. 在临时 `BP_ABTS_ArtValidation` 中摆放道路切线、两个预生成泥土洞、两个桩、一个袋和两条 Spline 弦，依次测试空洞、插桩、半拉、满拉、释放；确认插桩前后洞口模型没有被替换。
5. 通过后再移动/重命名到正式目录并在 UE 中 `Fix Up Redirectors`；不要在 Windows 文件管理器中移动已导入 `.uasset`。
6. 记录最终 UE 资产路径、源 `.blend`、AI 输入/输出文件的哈希或相对路径。

## 7. 各资产族的最小交付清单

### 7.1 M6 必需：简易弹弓一套

| 交付物 | 数量 | 是否 AI | 备注 |
| --- | ---: | --- | --- |
| 简易桩 Skeletal Mesh | 1 个，左右镜像复用 | AI 辅助候选 + 手工重建 | 必须有 4 骨和桩顶 Socket。 |
| 简易弹丸袋 Static Mesh | 1 | 手工为主 | 需要 4 个逻辑 Socket/Marker。 |
| 弦截面 Static Mesh / Spline 材质 | 1 | 手工 | 运行时生成左右两段。 |
| 道路弹弓泥土洞 Static Mesh | 1 个，左右由 Anchor Pair 放置 | **AI 候选 + Blender 收口** | 无碰撞；预先在道路生成；与 PCG Anchor Pair 对齐，插桩不改变洞口。 |
| 木/麻绳/石材 Material Instance | 3–4 | 无需 AI | 共享色板。 |
| 拉弓测试动画或 Control Rig | 1 | 无需 AI | 只驱动桩体弯曲。 |

### 7.2 建筑破坏纵向切片

- `SM_ABTS_Brick_Base`：完整砖；`SM_ABTS_Brick_Chipped`：缺角砖；`SM_ABTS_Brick_Half`：半砖。用 Geometry Nodes/手工生成，不用 AI。
- `SM_ABTS_WoodBeam`、`SM_ABTS_WoodPost`、`SM_ABTS_RoofPiece`：可买/下载后收口。
- `SM_ABTS_TargetWeakPoint_Crate`、`SM_ABTS_TargetWeakPoint_Crystal`、`SM_ABTS_ExplosiveBarrel`：玩法定制，AI 只用于独特外轮廓，最终需要简单碰撞、可替换破坏件和高对比弱点色。
- `SM_ABTS_RuinFacade_*`：可用 AI 候选做 2–3 个独特天际线，但主体必须拆成可重排模块，不能把整座 AI 建筑作为不可破坏单 Mesh。

### 7.3 后续 Set Piece

| 玩法 | 必需定制表现 | 生产重点 |
| --- | --- | --- |
| 工作台/熔炉 | 制作接口、材料槽、等级光效 | 基础可买；关键交互面与晶体插槽定制。 |
| 桥机关 | 锁链、卷扬、桥板支座 | 可买桥板；机关外壳和连接点定制。 |
| 侦察树枝槽 | 小型临时发射架 | 简易桩/弦复用缩小版，青翎专属色带。 |
| 强化弹弓 | 金属桩、强化弦、轨道槽 | 复用简易弹弓骨架和运行时接口，换网格/材质。 |
| 卫星/引力走廊 | 卫星遗迹、轨迹门、晶体信标 | AI 生成独特轮廓后强制低模化；逻辑仍由 PCG/轨迹系统决定。 |
| 终局发射遗址 | 钢铁太空弹弓、四鸟就位座、能量核心 | 优先做强轮廓、少量可动结构；不用大规模 AI 场景。 |

## 8. 赛后 AI 使用报告：必须与资产同步记录

### 8.1 每次 AI 使用都写一条记录

创建 `Docs/AIReports/ABTS_AI_Usage_Report.md`，每个生成任务一条记录。即使最终没有采用生成结果，也应记录“未采用”。

```markdown
## AI-2026-###

- 日期：YYYY-MM-DD
- 负责人：
- 用途：概念图 / 3D 候选 / 纹理候选 / 文案 / 代码辅助（可多选）
- 工具与版本/套餐：
- 商业授权核对日期与页面截图：
- 输入：文字提示词、用户原创草图、自己拍摄照片；列出文件相对路径。
- 完整提示词：
- 输入图来源与权利说明：
- 输出文件：`SourceArt/AI/...`
- 人工编辑：例如“在 Blender 删除 62% 面、重建骨骼、重画 2 个材质槽、重置 Pivot”。
- 最终使用状态：采用 / 部分采用 / 未采用。
- 最终 UE 资产路径：
- 是否含第三方品牌、角色、受限参考：否。
```

同时保存：生成界面截图、下载日期、原始 GLB/FBX/图片、购买/订阅凭证与最终 `.blend`。不要只在聊天记录里保留提示词。

### 8.2 报告范围

- 使用 AI 图像生成作为 3D 参考，也要报告。
- 使用 AI 3D 工具产生但最终完全重拓扑的模型，也要报告。
- 购买/下载的非 AI 资产记录来源、许可证和修改内容，但不应冒充 AI 生成。
- 使用本助手生成设计、代码、提示词或文档，也应按比赛规则纳入 AI 使用行为说明。

## 9. 验收清单与常见失败

### 9.1 资产验收

- 默认 Orbit 相机下，弹弓三件式结构能一眼读出“两桩、两段弦、中央袋”，且道路上能提前读出两个可插桩的泥土洞。
- 左右桩装入对应泥土洞后，桩根不悬浮、弦端不穿出桩顶、袋在两桩中点；插桩前后泥土洞网格与材质不被替换。
- 最大拉伸时袋可沿发射轴后拉，左右弦持续连接，桩顶轻微同向弯曲；释放后无残留歪曲。
- 资产镜像后法线、材质和 Socket 方向正确。
- HISM 资产不产生碰撞阻塞、不过度消耗材质槽。
- 关键建筑弱点、晶体核心和弹弦在道路外/俯视相机下仍有足够对比度。
- 所有定制资产均能在 AI 报告中追溯至输入、输出、人工修改与许可证。

### 9.2 排错表

| 症状 | 根因 | 修复 |
| --- | --- | --- |
| AI 模型看起来精细但放入关卡像异物 | 自动 PBR 贴图、密集噪点和圆滑法线破坏低模风格 | 回 Blender 删除微细节，减少材质槽，改用共享纯色母材质并控制平滑。 |
| 拉弓时弦与桩顶脱节 | 弦被烘焙进静态网格，或缺少稳定端点 Socket | 弦改为两条由桩顶 Socket 与袋 Socket 驱动的 Spline/Cable。 |
| 两桩拉伸时方向不一致 | 左右镜像未统一局部发射轴，或骨骼旋转符号不同 | 规定槽位局部 `+X` 为发射方向；左右实例使用相同骨架曲线，仅以镜像/符号处理外形。 |
| 桩体弯曲像橡皮 | 骨骼过多、权重过软或使用布料/物理模拟 | 保持 4 骨、三段渐变权重；只由拉力参数驱动小角度旋转。 |
| AI 导出 FBX 在 UE 中比例/朝向错 | 未应用 Blender Transform，或导出单位不一致 | Blender `Ctrl+A` 应用 Rotation/Scale；按 UE FBX 2020.2 重新导出并在 Staging 验证。 |
| 模型在球面上插入/悬浮 | Pivot 在 AI 网格中心而非接地点 | Origin 重置到接地点/插槽点；由 CellTopo 的表现变换放置。 |
| 插桩后道路洞口变成另一套石基座或丢失 | 将装配状态错误做成替换槽位 Mesh，而不是独立显示桩/弦/袋 | 道路泥土洞只在 PCG 表现创建时生成一次；装配状态只切换桩、弦、袋组件。 |
| 泥土洞阻挡鸟或造成相机/地形碰撞抖动 | AI 洞口网格启用了复杂碰撞，和连续地表发生重复碰撞 | `SM_ABTS_SlingshotDirtHole_Road` 固定 `Collision Enabled = No Collision`；地面碰撞仍由 Continuous Surface 处理。 |
| 赛后无法说明 AI 使用情况 | 只保留最终 uasset，丢失提示词和原始输出 | 每次生成立即填写报告并保存原始文件、截图和 `.blend`。 |

## 10. 本周优先顺序

1. 建立 `SourceArt/` 与 `Docs/AIReports/`，从第一张概念图开始记录。
2. 完成 `Simple` 弹弓资产卡、二维概念板、两个桩/两弦/一袋/两个预生成道路泥土洞的验证蓝图。
3. 只让简易弹弓通过“满拉—释放”视觉验收，再开始 M6 发射代码。
4. 同时下载一套通用低模树、岩石、木箱、桥板和建筑填充件，接入现有 HISM。
5. 用 Geometry Nodes 做砖、梁、碎片和破坏件变体；只为晶体弱点、卫星遗迹和终局发射台使用 AI 3D 候选。
6. 强化/太空弹弓必须复用简易弹弓的槽位、Socket 和骨骼接口，禁止另起一套不兼容结构。
