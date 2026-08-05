# ABTS 三渲二与全局风格化渲染设计

> 状态：调研与方案冻结稿；2026-08-04 建立。T0 自动视觉/GPU 基线、T1 全局色调和 T2-A 主视图描边与共享语义契约均已通过验收；下一步为 T2-B 功能工作树只读语义适配与 Integration 串行接线，详见 [T2-A 设计](ABTSToonStylizedRenderingT2A.md)。
>
> 适用版本：Unreal Engine 5.8，项目唯一引擎路径为 `C:\Program Files\Epic Games\UE_5.8`。
>
> 相关文档：[T0 自动视觉基线](ABTSToonVisualCaptureT0.md) · [T1 全局色调](ABTSToonStylizedRenderingT1.md) · [T2-A 主视图描边与契约](ABTSToonStylizedRenderingT2A.md) · [主设计稿](AngryBirdsToSpaceGameDesign.md) · [低模资产工作流](LowPolyAssetProductionAndAIReportWorkflow.md) · [M3 地形表现](M3TaskGraphTerrainPresentationDesign.md) · [统一镜头视觉优化](ABTSCameraVisualOptimizationDesign.md) · [M11 v2 终局优化](M11V2FinaleOptimizationDesign.md)

## 1. 结论先行

本项目适合做三渲二，但短期目标应定义为**柔和的低模卡通渲染（soft cel shading）**，而不是立即分叉 UE 5.8、重写整套 Deferred Renderer 或追求严格二维动画观感。

适配原因很直接：

- 地表本来就是纯色或少量色块，SDF 负责形状与分区，不依赖写实纹理；
- 树、石、弹弓和建筑以低模轮廓及材质类别传达信息，适合减少高光、压缩明暗层级；
- 小鸟、弹弓、道路、建筑弱点和轨迹都要求远距离可读，选择性轮廓线与彩色阴影能强化这种可读性；
- 游戏不需要写实材质之间的细微粗糙度差异，当前 PBR 高光、反射、局部曝光更容易形成“塑料感”。

推荐路线是：

1. 保留 UE 5.8 的 Default Lit、Lumen、Virtual Shadow Maps、Substrate 与现有玩法代码；
2. 先用可一键关闭的全局后处理完成色调压缩、少量明暗色阶、彩色暗部和选择性描边；
3. 再按“地表、角色、建筑、道具、透明材质”五个材质族逐步统一 Base Color、Roughness、Specular 与特殊高光；
4. 只有当垂直切片证明后处理/材质混合方案无法达到目标时，才单独评估引擎源码分叉和自定义 Shading Model。

因此，本方案是一次**全局视觉语言重构**，但第一版不是“重写渲染器”。它不改变 CellTopo、PCG、物理、碰撞、轨迹求解、相机控制或世界生成权威。

## 2. 三渲二究竟由什么组成

“三渲二”不是一个开关，也不等于“把颜色减少到三档”。一套稳定的实时三渲二通常由以下层次共同组成。

### 2.1 色板与材质简化

资产先拥有受控的基础色板，材质减少写实纹理噪声，把木、石、金属、玻璃、角色皮肤等差异转化为清晰的大色块、粗糙度和少量特殊高光。若基础材质仍有强烈白色高光和复杂反射，后处理描边不会自动消除塑料感。

### 2.2 离散或受控的明暗

经典实时卡通渲染将连续光照值映射到一维色阶 Ramp，把亮部、半影和暗部分成少数区间。Lake 等人的早期实时 NPR 工作已把“量化光照 + 轮廓线”作为核心组合；X-Toon 又说明一维 Ramp 可以扩展为随深度、细节或视角变化的二维色调映射。[Lake et al. 2000](https://www.markmark.net/npar/npar2000_lake_et_al.pdf) · [X-Toon 2006](https://citeseerx.ist.psu.edu/document?doi=c57f12d530271a7e782384fe528b825ac47b7f41&repid=rep1&type=pdf)

本项目不宜把暗部压成纯黑。更合适的是暖亮部、冷暗部和一个窄的过渡带，让低模体块仍能读出方向。Gooch Shading 的核心经验正是用冷暖中间色保留形体，把最亮和最暗留给高光与轮廓。[Gooch et al. 1998](https://www.cs.princeton.edu/courses/archive/fall00/cs597b/papers/gooch98.pdf)

### 2.3 轮廓线与结构线

线条至少分三类：

- 外轮廓：物体与背景之间的深度差；
- 折痕/结构线：相邻像素法线变化；
- 玩法强调线：角色、可攻击建筑、关键弹弓或 UFO 的选择性轮廓。

基于屏幕空间深度与法线导数的特征线是成熟方案，但会受到分辨率、Temporal Jitter、透明材质和远景小物体影响。[Saito and Takahashi 1990](https://www.eecs.umich.edu/courses/eecs498-2/papers/saito90.pdf) UE 的 Post Process Material 可以读取 Scene Depth、World Normal 和 Custom Depth/Stencil，适合先做这一层；官方同时提醒后处理材质应谨慎使用，Temporal AA/TSR 与执行阶段会影响轮廓稳定性。[Post Process Materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-materials-in-unreal-engine) · [Temporal Super Resolution](https://dev.epicgames.com/documentation/unreal-engine/temporal-super-resolution-in-unreal-engine?lang=en-US)

### 2.4 受控高光、边缘光与阴影

金属套环、晶体、UFO 可以保留窄而明确的高光带；木、石、土壤和小鸟身体则应降低白色镜面高光。边缘光只用于角色、卫星背面目标和终局物体，不应给每棵树都加一圈发光描边。投影也需要艺术指导：真实而碎的软阴影未必适合卡通画面，但完全取消接触阴影又会让物体漂浮。[Shadows for Cel Animation](https://gfx.cs.princeton.edu/pubs/Petrovic_2000_SFC/index.php)

### 2.5 曝光、色彩分级与时间稳定性

若自动曝光或局部曝光持续改变场景亮度，同一个 Toon 阈值会在移动镜头时跨档跳变。因此三渲二不是只改材质，还要给曝光、Tonemapper、Color Grading 和各类 Scene Capture 规定一致的语义。UE 官方建议优先使用现代 Color Grading 控件，LUT 更适合作为补充而非唯一调色手段。[Post Process Effects](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-effects-in-unreal-engine) · [Color Grading LUT](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-lookup-tables-for-color-grading-in-unreal-engine)

## 3. 当前工程基线与“塑料感”根因

### 3.1 已启用的渲染能力

`Config/DefaultEngine.ini` 当前启用了动态光照、Lumen GI/Reflection、Virtual Shadow Maps、Mesh Distance Fields、Ray Tracing 和 Substrate；同时启用了扩展自动曝光范围与局部曝光。现有材质大多仍走 Default Lit。UE 的 Default Lit 会共同表现直接光、间接光和镜面反射，而 Unlit 只有 Emissive，不能在不另写光照的情况下保留标准投影与光照交互。[UE Shading Models](https://dev.epicgames.com/documentation/en-us/unreal-engine/shading-models-in-unreal-engine)

这意味着当前“塑料感”并不主要来自低多边形，而来自以下组合：

- 多类材质共享近似的宽白色高光，木、石、角色和金属缺少明确的高光规则；
- Lumen 间接光、反射与局部曝光让纯色表面出现连续、偏写实的亮度渐变；
- 天空、地面、树和角色之间缺少统一色板，画面虽低模但并未形成统一的卡通明暗语言；
- 没有全局结构线或重要对象轮廓线，几何只能依赖 PBR 明暗来解释体块。

### 3.2 现有材质不是一张母材质可以直接替换

当前项目至少包含以下不同契约：

- `M_ABTS_M3_SDFTerrain`：运行时由 `UABTSM3TerrainMaterialBridge` 注入 Cell、道路、边界等 LUT 与参数；必须保留参数名和 MID 注入链；
- CuteBird 身体与脸部：需要保留颜色、脸部表情、骨骼动画和透明/遮罩语义；
- M7 木、石、钢、玻璃：材质同时承担玩法材料识别，玻璃不能与不透明砖统一处理；
- 弹弓桩、弦、袋、槽：木、金属、绳索和晶体需要不同高光与轮廓强度；
- 树木/HISM：不宜为每个实例开启 Custom Depth，也不宜描出每个内部三角边；
- Niagara、轨迹点、HUD、侦察圆与二维轨迹图：已经是高度符号化的表现，不应被全局色阶和描边误伤。

因此，第一版以“现有材质继续生效 + 后处理叠加”为主；第二版才通过共享 Material Function 和材质实例参数逐族迁移，而不是替换所有资产引用。

### 3.3 主视图和画中画不是同一个输出链

目前 M10 地面预览和 M11 远端预览使用 `SCS_FinalColorLDR`；M10 月面预览为解决背光黑屏，明确切换为 `SCS_BaseColor`。玩家相机的全局后处理不会天然保证每个 Scene Capture 得到相同结果。

风格化系统必须显式定义三种视图：

| 视图 | 目标 | 默认策略 |
| --- | --- | --- |
| 玩家主视图 | 完整世界风格 | 应用 Ground/Satellite/Finale 对应 Profile。 |
| 落点画中画 | 精确辨认落点 | 地面与终局可使用简化的同风格后处理；轮廓更细，禁止强烈暗部。 |
| 月面背光画中画 | 导航工具而非电影镜头 | 保留 BaseColor 可读性；只叠加局部色板/标记，不强制复用主视图光照。 |

## 4. 技术路线比较

| 路线 | 原理 | 优点 | 主要限制 | 本项目结论 |
| --- | --- | --- | --- | --- |
| 后处理优先 | 对最终 Scene Color 做色阶、调色；用 Depth/Normal/Stencil 描边 | 全局生效、接入快、可一键回退、不改引擎 | 无法精确区分直接光与 GI；透明、天空、TSR、Scene Capture 需专门处理 | **短期首选** |
| 材质优先 | 共享 Material Function/MPC 统一色板、粗糙度、高光；必要时局部 Unlit 假光照 | 每类资产可艺术指导，稳定且可控 | Default Lit 材质内拿不到完整的直接光/投影结果；全量迁移工作大 | **中期配合方案** |
| 混合方案 | 后处理负责全局明暗与线条，材质负责颜色、高光和例外 | 成本与质量平衡最好 | 需要清晰的视图/材质族契约 | **正式推荐方案** |
| 自定义 Shading Model / Renderer | 修改 UE 源码，在 GBuffer/Lighting Pass 中实现 Toon BRDF、阴影 Ramp | 光照控制最完整 | 需要源码引擎分叉；需长期维护 Substrate、Lumen、VSM、编译与升级；与当前唯一安装版引擎基线冲突 | **当前不实施，仅留 R&D 门** |

不能把“Material Custom 节点写一段 HLSL”误认为自定义 Shading Model。Custom 节点只在当前材质阶段执行，不能直接取得并替换完整的 Deferred Lighting、阴影遮蔽、Lumen 与反射合成。若强行把所有材质改为 Unlit 并自己计算 `N·L`，虽然很快能看到两档色阶，却会丢失标准阴影、间接光、反射、雾和许多既有材质功能，不适合作为全项目正式路线。

## 5. 推荐的总体架构

### 5.1 运行时边界

后续实现建议建立一个由集成工作树拥有的共享表现层：

```text
UABTSStylizedRenderingSubsystem
  -> 选择 UABTSStylizedRenderProfile
  -> 写入 MPC_ABTS_StylizedRendering
  -> 控制主相机 Post Process Blendables
  -> 向 M10/M11 Scene Capture 提供只读 View Style 描述

UABTSStylizedRenderProfile (Data Asset)
  -> GroundDay
  -> SatelliteGuide
  -> FinaleSpace

Material Layer
  -> M_PP_ABTS_StylizedTone
  -> M_PP_ABTS_Outline
  -> MF_ABTS_StylizedSurface（中期）
```

Subsystem 只消费当前游戏阶段、视图类型、太阳方向和相机高度等只读信息，不拥有玩法状态。风格开关不得改变物理 Tick、碰撞通道、轨迹积分、PCG Seed、建筑 Candidate/Result Hash 或相机输入契约。

### 5.2 全局参数

建议通过 Material Parameter Collection 公开少量稳定参数：

- `StyleEnabled`、`ToneBandCount`、`ToneThreshold0/1`、`ToneSoftness`；
- `LightTint`、`MidTint`、`ShadowTint`；
- `OutlineDepthScale`、`OutlineNormalScale`、`OutlineWidthPx`；
- `RimStrength`、`RimWidth`；
- `PlanetCenter`、`CameraAltitudeNormalized`、`EnvironmentBlend`；
- `SunDirectionWS`，只供需要局部艺术化的材质使用。

不要在 MPC 中无限增加每个资产的局部参数。UE 官方说明 MPC 适合跨大量材质高效共享全局值，但改变参数数量会使引用材质重新编译。[Material Parameter Collections](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-material-parameter-collections-in-unreal-engine)

### 5.3 后处理顺序

首版做两个独立 Blendable：

1. `StylizedTone`：先做可控的亮度压缩、柔和 2–3 档色阶、冷暖着色和饱和度约束；
2. `StylizedOutline`：读取 Scene Depth 与 World Normal，计算外轮廓和大折痕；Custom Stencil 只增强重要对象。

应优先测试 Before Tonemapping 或 UE 5.8 提供的等价 Scene Color 阶段，因为官方文档指出在 Tonemapper 前读取深度/法线通常更利于解决 TAA/GBuffer 轮廓问题；同时必须在实际 TSR 配置下验证颜色空间、抖动和分辨率变化，不能照搬旧版本教程的 Blendable Location。[Post Process Materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-materials-in-unreal-engine)

### 5.4 描边策略

- 全局 Depth/Normal 线只画大轮廓和大折痕，默认 1–2 像素，不描三角网格边；
- 鸟、当前弹弓、可攻击建筑弱点和 UFO 可使用 Custom Stencil 获得稳定强调；
- HISM 树石默认不写 Custom Depth，避免额外绘制所有实例；
- 玻璃、粒子、弦和半透明特效使用独立规则，不能依赖不透明 GBuffer；
- 天空、云、星空、HUD、轨迹点和二维轨迹图默认排除；
- 遮挡透视轮廓是 Gameplay 功能，不与美术外轮廓共用 Stencil 值或开关。

UE 的 Custom Depth-Stencil 可按项目设置启用并写入 Stencil，但它会产生额外 pass；只应给玩法重要对象使用。[Rendering Project Settings](https://dev.epicgames.com/documentation/unreal-engine/rendering-settings-in-the-unreal-engine-project-settings?lang=en-US)

### 5.5 材质族迁移顺序

| 顺序 | 材质族 | 主要改造 | 不能破坏的契约 |
| ---: | --- | --- | --- |
| 1 | M3 SDF 地表 | 保留纯色和 LUT；压低镜面，统一暗部色 | `M3_*LUT`、道路/边界参数、MID 注入 |
| 2 | CuteBird | 身体柔和 2–3 档，脸部保持清晰；选择性 Rim | 身体/脸部材质槽、动画、颜色身份 |
| 3 | M7 建筑 | 木/石低镜面，钢窄高光，玻璃独立 | 材料可读性、破坏模块、透明语义 |
| 4 | 弹弓与交互道具 | 不同等级共享色板，晶体/金属保留受控高光 | 等级识别、弦/袋/槽资产绑定 |
| 5 | 树石与背景 | 大色块、轻轮廓、减少细碎阴影 | HISM 性能与实例语义 |
| 6 | 卫星、行星、UFO | Space Profile、边缘光和轮廓 | M9/M11 轨迹、预览与演出权威 |

材质实例应承担颜色、粗糙度和高光宽度的差异，共享母材质/函数承担算法。UE 的 Material Instance 可以在不重新编译父材质的情况下快速调整已暴露参数，适合进行 A/B 调色。[Material Instances](https://dev.epicgames.com/documentation/en-us/unreal-engine/creating-and-using-material-instances-in-unreal-engine)

## 6. 短时间内可以做到什么

下列估算以一名熟悉项目但首次实现三渲二的开发者为基准，是排期预算而非保证。

### 6.1 1–2 天：建立可比较的风格原型

- 建立全局开关和同镜头 A/B 截图规范；
- 固定或显著收窄曝光漂移，关闭与目标风格冲突的 Bloom/Lens Flare/过强局部曝光；
- 使用现有 Color Grading 做统一色板、对比度与冷暖暗部；
- 先不改任何玩法资产引用。

产出应是“低模卡通调色版”，还不是完整三渲二，但可立即判断方向是否适合。

### 6.2 2–4 天：柔和色阶与首版描边

- 完成参数化的 2/3 档柔和 Tone Ramp；
- 完成 Depth/Normal 外轮廓，加入距离与分辨率自适应；
- 为鸟、当前弹弓和一个 M7 建筑建立选择性 Stencil；
- 明确主视图、M10 画中画和 M11 画中画的不同 Profile。

这是最适合进行 Visible PIE 决策的首个垂直切片。

### 6.3 3–7 天：消除主要“塑料感”

- 先迁移地表、鸟、木/石/钢建筑和弹弓四个最常见材质族；
- 木、石、土和鸟体采用高 Roughness、低受控 Specular；
- 钢、晶体、UFO 使用窄高光/边缘光，而非所有表面统一发亮；
- 解决玻璃、树木、弦和 Niagara 的例外规则；
- 完成 1080p/1440p 性能与 TSR 稳定性验收。

这一阶段可以形成“可贯穿当前游戏的首版视觉风格”。

### 6.4 1–2 周以后：逐场景美术指导

- 为地表白天、卫星背面和终局星空分别建立 Profile；
- 逐步重做阴影色、间接光强度、金属/晶体表现和破坏碎块可读性；
- 为 M10/M11 Scene Capture 加入专用风格链；
- 结合真实内容密度修正轮廓、远景、粒子和 UI 对比。

### 6.5 当前不应承诺的效果

- 不在短期内实现 Guilty Gear/Arc System Works 级别的逐镜头角色法线、骨骼修形和手工阴影；
- 不做素描排线、水彩纸纹、逐帧线条抖动等第二风格层；实时排线本身就是独立 NPR 系统。[Real-Time Hatching](https://gfx.cs.princeton.edu/gfx/pubs/Praun_2001_RH/index.php)
- 不用引擎源码分叉作为第一个可见成果；
- 不把球面高度雾、体积云和高空星空塞进同一个 Toon 后处理材质解决。

## 7. 光照、球面雾云与高空星空的后续边界

### 7.1 光照系统

首版保留 Lumen 与 Virtual Shadow Maps，用风格层先约束输出。Lumen 提供动态 GI 和反射，适合程序生成世界，但其连续间接光会软化 Toon 色阶；正式风格确定后再 A/B 以下三档：

1. 保留 Lumen，仅压低反射与间接光对色阶的影响；
2. 保留 GI，按材质族弱化 Specular/Reflection；
3. 为终局或特殊 Profile 单独降低 GI，强化方向光和边缘光。

不要一开始关闭全部 GI/阴影。那会把“去塑料感”变成“物体漂浮”，也会丢失建筑、鸟和地表之间的重要接触关系。[Lumen GI and Reflections](https://dev.epicgames.com/documentation/en-us/unreal-engine/lumen-global-illumination-and-reflections-in-unreal-engine) · [Shadowing in UE](https://dev.epicgames.com/documentation/en-us/unreal-engine/shadowing-in-unreal-engine)

### 7.2 球面高度雾与体积云

当前问题的本质不是 Toon，而是地球式 Height Fog/Volumetric Cloud 使用全局高度/方向语义，与小行星任意侧面的局部径向 Up 不一致。后续应作为独立的“球面环境层”处理：

- 以 `distance(WorldPosition, PlanetCenter) - PlanetRadius` 作为海拔；
- 地表雾、云层和天空混合都消费同一 Planet Center/Radius 与视点高度；
- 近地阶段优先保留可读性，不让雾改变 Toon 分档；
- 体积云可先做跟随玩家径向 Up 的局部云壳/视觉代理，再评估真正的球壳体积方案。

仅仅旋转现有高度雾和体积云可以作为过渡方案，但无法同时让整颗球所有观察方向都正确。

### 7.3 高空星空

高空星空应由相机海拔和终局状态控制天空材质混合，而不是把白天地表整体压暗：

```text
AltitudeBlend = smoothstep(AtmosphereStart, SpaceStart, CameraAltitude)
DaySky/Atmosphere -> HighAltitudeHaze -> StarSky/FinaleSky
```

GroundDay Profile 保持明亮；Satellite Profile 可看到稀疏星空但仍保留月面导航；FinaleSpace Profile 才切换高清星空、关闭不相容的高度雾/体积云并强化行星/UFO 轮廓。该环境切换消费风格参数，但不属于三渲二首版验收。

## 8. 性能预算与技术风险

### 8.1 首版预算

以下是项目门槛，不是当前实测值：

- 目标平台：项目现有桌面 DX12/SM6 路线，1080p 60 FPS；
- Tone/Color 全屏 pass：目标不超过 `0.6 ms @ 1080p`；
- Depth/Normal 描边：目标不超过 `1.0 ms @ 1080p`；
- 风格层总 GPU 增量：目标不超过 `1.5 ms @ 1080p`，`2.5 ms @ 1440p`；
- Custom Depth 只用于少量重要对象，禁止默认覆盖全部 HISM；
- 风格关闭时不应保留显著的额外 Scene Capture、Custom Depth 或全屏 pass 成本。

### 8.2 主要风险

| 风险 | 现象 | 处理原则 |
| --- | --- | --- |
| 最终颜色分档无法区分直接光/GI | 阴影、天空和反射一起跳档 | 使用柔和阈值；中期将可控部分移到材质族，不盲目增加色阶。 |
| TSR/动态分辨率导致线条抖动 | 运动时轮廓闪烁、粗细变化 | 在正确 PP 阶段取样；按 View Size 算像素宽度；固定多种 Screen Percentage 回归。 |
| Substrate/GBuffer 兼容性 | 旧教程的私有 GBuffer 假设失效 | 只使用 UE 5.8 文档支持的 SceneTexture 输出；不复制旧版本引擎内部布局。 |
| 透明与粒子缺少法线/深度 | 玻璃、弦、Niagara 断线或黑边 | 独立材质族和显式排除；必要时只画外轮廓代理。 |
| 画中画与主视图不一致 | 主画面卡通，落点预览仍写实或过暗 | Scene Capture 显式选择 Profile/CaptureSource，不依赖继承玩家相机。 |
| Lumen/Distance Field 材质不一致 | WPO/Two-Sided 表现与软件追踪代理不同 | 迁移材质时检查 Lumen Scene；遵守官方限制与 Material Override 规则。[Lumen Technical Details](https://dev.epicgames.com/documentation/unreal-engine/lumen-technical-details-in-unreal-engine?lang=en-US) |
| 二进制资产多人冲突 | 多工作树同时改母材质、地图或 PP Volume | 风格核心由 Integration 独占；每个 `.uasset/.umap` 单写入者，功能树只提交自有适配。 |

## 9. 分阶段落地与所有权

### Phase T0：基线和垂直切片定义

- Integration 在唯一 `L_ABTS_M11` 世界中建立固定 Seed、四个语义相机点和 Style Off/On；M11 的 M10 继承链已经覆盖地表、鸟、树石、弹弓、M7 建筑、卫星/E5 与终局，不再维护第二张切片；
- 相机从 Start 路口、建筑合同/普通槽、卫星练习快照和终局局部帧实时解析，不保存绝对世界坐标；
- 截图与 GPU 基线复用相同 Pose Hash，但必须分进程/分帧运行；
- 每次运行记录 Seed、生成合同、卫星/终局身份、Profile、分辨率、镜头 Hash 和文件/命令证据；
- T0 具体运行方式、manifest 门槛与当前分层状态见 [T0 自动视觉基线](ABTSToonVisualCaptureT0.md)。

### Phase T1：全局色调原型

- Integration 独占 Post Process Material、MPC、Profile 与共享配置；
- 不改现有资产默认绑定；
- 通过开关在同一 PIE 运行中比较原图与风格图。
- 当前首版以项目级 Scene View Extension + Global Shader 落地同等的全屏后处理职责，避免在未获 Editor 资产写入授权时生成不可审阅的二进制材质；不修改引擎源码，T2 可在美术决策后迁移到 Material/MPC 资产链。

### Phase T2：描边和视图契约

- T2-A：Integration 冻结对象/视图语义、Stencil 保留区和主视图 Depth/Normal 描边；Scene Capture 明确跳过，不启用 Custom Depth producer；
- T2-B：M3/M7/M11 只提供稳定只读的对象类别适配，Integration 串行接入选择性 Stencil 与地面/月面/终局 Scene Capture；
- T2-C：补动态镜头、瞄准/PIP、破坏过程和 Screen Percentage 回归，冻结美术参数及性能门槛；
- M10/M11 的 Scene Capture 始终由 Integration 串行接线，禁止两工作树同时编辑同一相机或材质资产。详见 [T2-A 设计](ABTSToonStylizedRenderingT2A.md)。

### Phase T3：材质族迁移

- M3：只适配地表与自有 HISM 材质，保留 LUT/MID 契约；
- M7：只适配建筑木/石/钢/玻璃材质及破坏表现；
- M11：只适配助推行星、UFO 与终局专有表现；
- Integration：CuteBird、弹弓共享材质、PP/MPC、公共地图、默认绑定和最终 Profile。

### Phase T4：环境与光照

- 先冻结 T1–T3 的风格基线，再由 Integration 处理光照、球面雾云与高空星空；
- 环境改造不得与 M3 世界生成、M11 轨迹或镜头优化混为同一提交；
- 自定义 Shading Model 只有在 T3 验收明确失败、且项目允许改为源码引擎后才能立项。

## 10. 正式验收门槛

### 10.1 视觉门槛

- 同一 Seed、相机、时间和视图下提供 Style Off/On 对照图；
- 小鸟、道路、三档弹弓、木/石/钢/玻璃建筑、卫星、UFO 和轨迹 UI 均保持身份可读；
- 暗部有颜色而非大面积死黑，背光角色与月面落点仍可辨认；
- 轮廓不覆盖脸部细节、不描出所有低模三角边、不污染天空/UI；
- 相机平移、旋转、跳跃、发射与建筑坍塌时无明显线条闪烁和色阶泵动；
- 地面、月面画中画和终局预览遵守第 3.3 节的视图语义。

### 10.2 Gameplay 与确定性门槛

- Style Off/On 的 PCG Seed、Task Graph 结果、建筑 Candidate/Result Hash、弹弓预测、实飞碰撞和 M11 求解结果一致；
- 不改变碰撞、Custom Depth 以外的 Gameplay Trace、物理材质或 Tick 顺序；
- 不因风格化隐藏落点、弱点、槽位、资源、道路边界或轨迹偏转信息；
- 风格资源缺失时 fail soft：回退原渲染，不阻断 WorldReady 或玩法流程。

### 10.3 性能与工程门槛

- 在目标分辨率记录 `ProfileGPU` 或等价 GPU 证据，满足第 8.1 节预算；
- 风格开关与三个 Profile 的参数身份可记录，PIE 日志能辨认当前 Profile；
- 强制 Unity 编译、相关自动化与 Visible PIE 分层验收；纯 Markdown 设计阶段不要求编译；
- 资产迁移清单记录每个母材质、实例、地图和 Scene Capture 的唯一写入者。

## 11. 建议的第一次实现任务

第一轮不要创建几十个材质实例。按两个小阶段建立可随时删除的垂直切片：

1. T0 只在唯一 `L_ABTS_M11` 中建立固定 Seed、`GroundStart`、`SlingshotBuilding`、`SatelliteE5`、`FinaleLayout` 四个语义相机点，以及完全同姿态的 Style Off/On 截图和独立 GPU 证据；具体运行契约见 [T0 自动视觉基线](ABTSToonVisualCaptureT0.md)；
2. T0 不进入瞄准或发射状态，因此 HUD 绘制链会保留，但地面/月面 PIP 和轨迹显示不属于本轮已覆盖证据；
3. T1 让稳定 Style/Profile 接缝真实消费柔和三档色调与冷色暗部；
4. T2-A 增加最终主视图的 1–2 像素 Depth/Normal 轮廓，并冻结对象/视图语义及 Stencil 保留区，但不产生 Custom Depth；
5. T2-B 再给当前鸟、弹弓、建筑弱点和终局目标接入选择性 Stencil，并增加显式玩法状态截图，用冻结视图类别检查地面/月面/终局 PIP；
6. 达到稳定性与性能门槛后，再决定是否迁移第一批材质族。

这一步完成后应进行一次明确的美术决策：保留原渲染、采用柔和三渲二、或停止全局描边只保留色板/材质风格化。没有通过这道决策门，不进入引擎源码分叉。

## 12. 调研来源

### Unreal Engine 5.8 官方资料

- [Post Process Materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-materials-in-unreal-engine)：后处理材质、SceneTexture、Custom Depth/Stencil 与 TAA 注意事项。
- [Post Process Effects](https://dev.epicgames.com/documentation/en-us/unreal-engine/post-process-effects-in-unreal-engine)：曝光、Color Grading 与后处理体系。
- [Shading Models](https://dev.epicgames.com/documentation/en-us/unreal-engine/shading-models-in-unreal-engine)：Default Lit、Unlit 等模型边界。
- [Substrate Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/overview-of-substrate-materials-in-unreal-engine)：当前 Substrate 材质框架。
- [Material Instances](https://dev.epicgames.com/documentation/en-us/unreal-engine/creating-and-using-material-instances-in-unreal-engine) 与 [Material Parameter Collections](https://dev.epicgames.com/documentation/en-us/unreal-engine/using-material-parameter-collections-in-unreal-engine)：材质族参数化与全局参数。
- [Lumen Technical Details](https://dev.epicgames.com/documentation/unreal-engine/lumen-technical-details-in-unreal-engine?lang=en-US)：Lumen Scene、Distance Field 与材质限制。
- [Temporal Super Resolution](https://dev.epicgames.com/documentation/unreal-engine/temporal-super-resolution-in-unreal-engine?lang=en-US)：Temporal Upscaling 与后处理顺序。

### NPR / 三渲二基础论文

- [Stylized Rendering Techniques for Scalable Real-Time 3D Animation](https://www.markmark.net/npar/npar2000_lake_et_al.pdf)：实时量化光照与轮廓线。
- [Comprehensible Rendering of 3-D Shapes](https://www.eecs.umich.edu/courses/eecs498-2/papers/saito90.pdf)：基于深度/法线的屏幕空间结构线。
- [A Non-Photorealistic Lighting Model for Automatic Technical Illustration](https://www.cs.princeton.edu/courses/archive/fall00/cs597b/papers/gooch98.pdf)：冷暖色调与形体可读性。
- [X-Toon: An Extended Toon Shader](https://citeseerx.ist.psu.edu/document?doi=c57f12d530271a7e782384fe528b825ac47b7f41&repid=rep1&type=pdf)：从一维色阶到多维风格控制。
- [Real-Time Hatching](https://gfx.cs.princeton.edu/gfx/pubs/Praun_2001_RH/index.php)：实时排线，作为本项目明确延期的扩展方向。
