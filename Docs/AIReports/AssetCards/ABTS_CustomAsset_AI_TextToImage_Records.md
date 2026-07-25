# ABTS 定制资产 AI 生成与低模收口记录

> 记录范围：本文件记录本项目目前采用的完整 Hunyuan 资产工作流，以及工作台、熔炉、桥、弹弓槽、弹弓桩、弹丸袋、弹弓弦、炸药桶和弹簧等定制资产。所有原画与对应低模位于 `C:\workspace\AngryBirdsToSpaceAIAsset`。
>
> **统一生产流程（当前实际采用）**：
>
> `HY Image 3.0 Instruct 文生图低模原画`
> `→ 选择/修正原画`
> `→ Hunyuan3D 图生 3D 高模`
> `→ Hunyuan3D「低模拓扑-三角面-指定面数」生成低模`
> `→ Hunyuan3D「纹理绘制-图生纹理」以原画为参考生成纹理`
> `→ UE 导入、材质收口、Pivot/Socket/碰撞/尺寸校正`
>
> 原画、FBX 低模、纹理和最终 UE 资产必须建立一一对应关系。AI 只负责视觉资产候选；CellTopo、建筑占用、道路/水网、骨骼命名、Socket、碰撞和 Gameplay 参数由人工确定。

## 0.1 本次工作流的工具与文件证据

| 阶段 | 工具/功能 | 产物 | 证据位置 |
| --- | --- | --- | --- |
| 概念设计 | HY Image 3.0 Instruct | 低模原画 PNG | `C:\workspace\AngryBirdsToSpaceAIAsset\*.png` |
| 高模生成 | Hunyuan3D 图生 3D | 原始高模（中间产物） | 按资产单独归档；不直接进 UE |
| 拓扑减面 | 低模拓扑-三角面-指定面数 | 指定面数低模 FBX | `C:\workspace\AngryBirdsToSpaceAIAsset\*低模.fbx` |
| 纹理生成 | 纹理绘制-图生纹理 | BaseColor/Normal/Roughness/Metallic 等纹理 | 对应 `*.fbm` 目录或 UE 导入纹理 |
| 人工收口 | UE 5.8、必要时 Blender | Pivot、Socket、材质实例、碰撞、命名 | UE 资产与本记录的补录字段 |

## 0.2 当前源资产清单与特殊拆分规则

| 资产 | 原画 | 低模 | 特殊处理 |
| --- | --- | --- | --- |
| 弹弓槽 | `弹弓槽.png` | `弹弓槽低模.fbx` | 使用新的 Hunyuan 工作流替代旧 `ABTS_SlingshotDirtHole_Road_v001` 记录；道路预生成无碰撞视觉资产。 |
| 弹弓桩 | `树枝桩.png`、`简易桩.png`、`强化桩.png`、`太空桩.png` | 对应四个 `*桩低模.fbx` | 四种桩共用同一低模拓扑；通过四张不同原画生成/应用不同纹理，不重复生成四套几何。 |
| 弹弓弦 | `弹弓弦.png` | `弹弓弦低模.fbx` | 只生成白膜低模；材质由人工在 UE 制作，运行时由 Spline/Cable 控制长度。 |
| 桥面 | `桥面.png` | `桥面低模.fbx` | 从桥原画拆出桥面部分，作为独立网格。 |
| 桥绳 | `桥绳.png` | `桥绳低模.fbx` | 从桥原画拆出桥绳部分，桥面与两根绳子在 UE 中拼装。 |
| 工作台/熔炉 | 对应 PNG | 对应低模 FBX | 视觉主体与交互挂点由人工收口。 |
| 炸药桶/弹簧 | 对应 PNG | 对应低模 FBX | 炸药桶爆炸、弹簧活塞弹出均由 Gameplay/动画驱动，模型不决定物理。 |

## 0.3 命名与归档规则

每个资产至少保存以下记录：

```text
AssetId
原画文件
高模生成日期与截图
低模拓扑目标三角面数
低模 FBX 文件
图生纹理输入原画
生成的纹理文件
UE 导入路径
人工修改内容
授权/比赛规则截图
```

推荐将中间文件按以下方式归档：

```text
C:\workspace\AngryBirdsToSpaceAIAsset\
├─ Concepts\        # HY Image 3.0 原画
├─ HighPoly\        # Hunyuan3D 图生 3D 中间高模
├─ LowPoly\         # 指定三角面数后的 FBX
└─ Textures\        # 图生纹理输出
```

## 0. 通用记录字段

- 文生图工具：Hunyuan（版本/套餐：待补填）
- 图生 3D 工具：Hunyuan3D（版本/套餐：待补填）
- 生成日期：待补填
- 商业/比赛授权核对日期：待补填
- 概念图目录：`C:\workspace\AngryBirdsToSpaceAIAsset\` 下对应的 PNG（当前以中文资产名保存）
- 低模 FBX 目录：`C:\workspace\AngryBirdsToSpaceAIAsset\` 下对应的 `*低模.fbx`
- Hunyuan3D 高模：中间产物，建议归档到 `C:\workspace\AngryBirdsToSpaceAIAsset\HighPoly\<AssetId>\`，不直接导入 UE
- 最终 UE 目录：`/Game/ABTS/Art/`
- 每条记录必须保存：HY Image 3.0 完整提示词、采用图、未采用图、Hunyuan3D 生成截图、原始高模、低模拓扑目标面数、低模 FBX、图生纹理输入/输出、人工编辑说明和许可证截图。

## 1. 工作台

### 1.1 资产卡

- AssetId：`ABTS_Workbench_Main_v001`
- 用途：玩家加工树枝、石料和弹弓组件的主工作台。
- 推荐 UE 类型：Static Mesh 或拆分为主体、台面、工具小件三个 Static Mesh；近景交互主体不使用 HISM。
- 风格约束：低矮、厚重、道路旁可读；木台面、石/木支腿、少量工具；不生成复杂可动机械。
- 建议预算：1,500–4,000 tris，2–3 个材质槽，简单 Box 碰撞由交互 Actor 提供。
- 逻辑边界：工作台占用和相邻联动由 CellTopo；模型不保存 CellId 或配方。

### 1.2 Hunyuan 文生图完整提示词

```text
original stylized low-poly crafting workbench for a small spherical planet exploration game,
compact sturdy wooden workbench beside a dirt road,
thick faceted timber tabletop, two chunky supports, a small stone vise,
simple hand tools, branch pieces, stone pieces and a shallow crafting tray,
warm muted wood and clay colors, broad polygon planes, clear readable silhouette,
game prop concept, three-quarter view, front view, isolated on a plain light background,
no character, no bird, no text, no logo, no modern machinery,
no realistic PBR noise, no excessive tiny props, no electrical cables
```

- 图生 3D 说明：只重建工作台主体与固定工具，删除漂浮工具和不可见内面；交互台面保持平整；不要把树枝、石料生成为不可分离的玩法库存。
- 必须人工确定：台面交互 Socket、CellTopo 建造占用、碰撞和材质槽。

## 2. 熔炉

### 2.1 资产卡

- AssetId：`ABTS_Furnace_Main_v001`
- 用途：加工木材、金属和强化弹弓组件的中期加工设施。
- 推荐 UE 类型：主体 Static Mesh + 烟雾/火焰 Niagara；炉门、晶体或把手可作为独立组件。
- 风格约束：低矮石砌炉体、明显炉口和少量暗红发光；不生成真实复杂火焰几何。
- 建议预算：2,000–5,000 tris，3 个材质槽；碰撞由主体 Actor 使用简单碰撞。
- 逻辑边界：燃烧/加工状态由 Gameplay；模型只表达状态材质和特效挂点。

### 2.2 Hunyuan 文生图完整提示词

```text
original stylized low-poly smelting furnace for a compact spherical planet game,
small stone and dark clay furnace built beside a road,
one clearly readable arched furnace opening, thick faceted masonry blocks,
simple side chimney, a small metal handle and a shallow glowing ember chamber,
charcoal gray stone, warm brown clay, subtle dark red inner glow,
broad polygon planes, clean game prop silhouette, three-quarter view,
isolated on a plain light background,
no character, no bird, no text, no logo, no factory pipes,
no complicated machinery, no realistic PBR noise, no giant chimney smoke mesh
```

- 图生 3D 说明：保留炉口、炉门、烟囱和稳定底座；删除复杂管道；火焰和烟雾在 UE 中单独实现。
- 必须人工确定：`S_FurnaceDoor`、`S_FurnaceGlow`、`S_Smoke` 等挂点以及加工状态材质参数。

## 3. 桥

### 3.1 资产卡

- AssetId：`ABTS_Bridge_FallenLog_v001`
- 用途：跨越 CellTopo 河网浅河/深河桥址，改变相邻边的 `CrossingType`。
- 推荐 UE 类型：桥面、两端支座、护栏可拆分；不把桥作为一体不可变的地形 Mesh。
- 风格约束：低模木桥，桥轴清晰，桥头两端可贴合球面坡面；暂不生成可自由拼装的无限桥板。
- 建议预算：2,000–5,000 tris，2–3 个材质槽；桥体简单碰撞由 Actor 提供。
- 逻辑边界：桥址、跨越方向、两岸 Cell 和可达性由 CellTopo；模型只读取结果。

### 3.2 Hunyuan 文生图完整提示词

```text
original stylized low-poly wooden footbridge for a small spherical planet game,
short sturdy bridge crossing a narrow river,
three or four thick faceted timber planks, two simple side supports,
rough rope handrails, chunky end posts, visible bridge axis from one bank to the other,
warm wood, muted rope, a few simple stone supports,
clean broad polygon planes, readable game prop silhouette, three-quarter view,
isolated on a plain light background,
no character, no bird, no text, no logo, no vehicles,
no long suspension bridge, no complex metal engineering,
no water plane, no full landscape, no realistic PBR noise
```

- 图生 3D 说明：只重建桥体，不重建水面、河岸和地形；桥的局部 `+X` 轴定义为 CellTopo Crossing Edge 方向。
- 必须人工确定：桥头接地 Pivot、两端支座、简单碰撞和桥生成时的球面姿态。

## 4. 通用弹弓桩

### 4.1 资产卡

- AssetId：`ABTS_SlingshotPost_Base_v001`
- 用途：简易/强化/钢铁弹弓的两根独立桩体视觉基型。
- 推荐 UE 类型：Skeletal Mesh；左右使用同一网格镜像实例。
- 骨架契约：`root → post_base → post_mid → post_tip`。
- 必需 Socket：`S_PostBase`、`S_StringAnchor`。
- 建议预算：700–2,000 tris，2–3 个材质槽；桩体不承担复杂碰撞。
- 逻辑边界：桩根由道路泥土洞和 CellTopo Anchor Pair 定位；桩体不能决定槽位。

### 4.2 Hunyuan 文生图完整提示词

```text
original stylized low-poly single slingshot post for a small spherical planet game,
one separate upright faceted wooden pole, rounded but chunky natural timber,
slightly tapered profile, clear flat bottom for insertion into a dirt planting hole,
small carved notch and rope anchor at the top,
subtle irregular grain implied by broad polygon planes, warm amber brown wood,
clean readable silhouette, isolated single prop, front and three-quarter view,
plain light background, no second post, no Y-shaped fork, no slingshot string,
no pouch, no stone base, no character, no bird, no text, no logo,
no realistic bark noise, no complex metal parts
```

- 图生 3D 说明：AI 只提供桩体外轮廓；Blender 必须重建底部 Pivot、四骨骨架、顶端 Socket 和左右镜像一致性。
- 桩体弯曲由 `post_mid/post_tip` 驱动，不使用布料或不可控物理模拟。

## 5. 弹弓弦：树枝、简易、强化、钢铁

> 四种弦都不是一整条静态弦网格。最终表现使用同一 `SM_ABTS_StringSegment` 截面，通过两条 Spline/Cable 段连接桩顶 `S_StringAnchor` 与弹丸袋左右 Socket。以下概念图只表现材质和截面语言，不生成整套弹弓。

### 5.1 树枝弦

- AssetId：`ABTS_SlingshotString_Twig_v001`
- 用途：青翎树枝槽的临时近射；视觉上是植物纤维/细枝束。
- 推荐输出：8 边低模纤维截面或细短枝束概念；运行时仍由 Spline 连接。

```text
original stylized low-poly twig slingshot string segment for a small planet game,
thin bundled plant fibers and flexible young twigs twisted into one readable cord,
natural green-brown fibers, simple cut ends, small knots at both ends,
faceted clean silhouette, game prop material concept, isolated single string segment,
straight relaxed presentation with visible braided direction, three-quarter view,
plain light background, no slingshot posts, no pouch, no full slingshot,
no character, no bird, no text, no logo, no realistic hair or cloth simulation,
no dense microfibers
```

### 5.2 简易弦

- AssetId：`ABTS_SlingshotString_Simple_v001`
- 用途：树枝和石料制作的简易弹弓。
- 推荐输出：单根粗麻绳/植物纤维绳，暖棕色；左右两段共用材质。

```text
original stylized low-poly simple slingshot string segment for a small spherical planet game,
one thick handmade hemp and plant-fiber cord, warm tan brown rope,
clear large braided bands, simple wrapped end knot, slightly uneven handmade shape,
clean faceted low-poly silhouette, isolated single string segment,
straight relaxed presentation, three-quarter view, plain light background,
no slingshot posts, no pouch, no full slingshot, no character, no bird,
no text, no logo, no realistic loose fibers, no metal wire, no dense texture noise
```

### 5.3 强化弦

- AssetId：`ABTS_SlingshotString_Reinforced_v001`
- 用途：黑鸟可使用的强化弹弓；比简易弦更直、更厚、更有工业包覆感。
- 推荐输出：深色编织芯 + 少量金属/皮革端部包覆；不生成真实复杂钢缆。

```text
original stylized low-poly reinforced slingshot string segment for a small spherical planet game,
thick dark braided cord with a compact protective leather and metal sleeve at each end,
strong tension-ready silhouette, charcoal fiber, muted steel bands,
large readable polygon planes, clean isolated single string segment,
straight relaxed presentation, three-quarter view, plain light background,
no slingshot posts, no pouch, no full slingshot, no character, no bird,
no text, no logo, no realistic cable bundle, no excessive rivets,
no dense PBR microdetail
```

### 5.4 钢铁弦

- AssetId：`ABTS_SlingshotString_Steel_v001`
- 用途：钢铁太空弹弓；视觉上是钢索/能量导线，不是普通绳子。
- 推荐输出：深钢色多股钢索与少量晶体/能量环；发光效果由 UE 材质和 Niagara 提供。

```text
original stylized low-poly steel slingshot string segment for a space-age launch device on a small spherical planet,
single thick dark steel cable with three readable bundled strands,
compact angular metal end sleeves, tiny muted teal energy bands,
industrial faceted silhouette, charcoal steel and subtle cyan accent,
isolated single string segment, straight relaxed presentation, three-quarter view,
plain light background, no slingshot posts, no pouch, no full slingshot,
no character, no bird, no text, no logo, no realistic wire fuzz,
no excessive glowing effects, no dense PBR microdetail
```

### 5.5 四种弦的统一编辑规则

- 四种弦必须共享同一长度方向、截面方向、端点语义和材质参数接口。
- `+X` 为弦段长度方向；导入后由 Spline 起点和终点决定实际长度。
- 弦不设置碰撞；鸟、桩和袋的交互由弹弓 Actor 处理。
- 生成的端部结、金属套或晶体环只能作为视觉参考，最终 Socket 位置由人工确定。
- 树枝/简易/强化/钢铁的差异只体现在色板、截面、端部包覆和材质参数，不改变运行时弦连接算法。

## 6. 统一人工收口与报告补录

每个概念图选定后，必须补录：

```markdown
- 采用概念图：
- 未采用概念图：
- Hunyuan 生成日期：
- Hunyuan 版本/套餐：
- Hunyuan3D 图生 3D 日期：
- Hunyuan3D 版本/套餐：
- 原始 3D 输出文件：
- Blender/UE 人工编辑：Pivot、材质槽、面数、骨骼、Socket、碰撞、删除的 AI 部分
- 最终 UE 资产路径：
- 授权/比赛规则截图：
```

AI 不得决定以下最终内容：CellTopo CellId、Anchor Pair、道路切线、球面径向姿态、建筑占用、物理碰撞、骨骼命名、Socket 变换、发射弹道和资源库存。
