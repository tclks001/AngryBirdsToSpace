# 破碎动画与特效设计稿

> 状态：首版设计，尚未实现。  
> 目标：以最少的编辑器操作，为 UFO、HISM 树石、四类砖块、黑鸟与炸药桶提供统一、可扩展且不反写 Gameplay 的受击/破碎表现。  
> 关联：[开局与终局自动演出导演稿](OpeningAndFinaleCinematicDirection.md) · [M6 发射/碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M7 建筑](M7TaskGraphSphericalBuildingIntegrationDesign.md) · [Chaos 刚体移动](ChaosRigidBodyMovementDesign.md)

## 1. 首版结论

首版采用“**一份 Chaos UFO + 统一 Niagara 表现事件 + 少量预制碎块**”的混合方式。

- **只让 UFO 使用 Chaos Geometry Collection。**它只有一个、终局只破碎一次，值得使用 UE 的 Fracture Mode。
- **不把 HISM 树石和四种砖块批量转为 Geometry Collection。**它们的批量渲染价值高，逐实例转换会让内存、碰撞和 Chaos 体数量失控。
- **黑鸟与炸药桶是爆炸事件，不是网格破碎问题。**它们使用闪光、冲击波、烟尘、火花和现有径向冲量；炸药桶可额外生成少量预制桶片。
- **所有特效都是只读表现。**命中、伤害、HISM 移除、建筑断裂、黑鸟爆炸和 UFO 命中仍由已有 C++ Gameplay/Chaos 事件权威决定；VFX 不参与碰撞、伤害、回收和重力计算。

UE 的内置 Chaos Destruction 可以快速把一个 Static Mesh 制作为 Geometry Collection 并通过 Fracture Mode 分裂，但它不是“对任意 HISM 一键产生低成本、风格合适的破碎”的功能。Geometry Collection 要求资产尽量封闭、无交叠，并仍需为碎块数量、Cluster 和回收做一次调参。[Geometry Collection 指南](https://dev.epicgames.com/documentation/unreal-engine/geometry-collections-user-guide) [Fracture 指南](https://dev.epicgames.com/documentation/unreal-engine/fracturing-geometry-collections-user-guide)

## 2. 效果矩阵

| 表现事件 | 触发源（权威） | 首版画面 | 物理方案 | 生命周期 |
| --- | --- | --- | --- | --- |
| UFO 受击破碎 | M11 `TargetHit` | 0.12 s 白闪、定向火花、UFO 分块散开、烟尘 | 单个 Chaos Geometry Collection，一次定向 Field/Impulse | 2.5–3.5 s 后碎块淡出/回收 |
| UFO 悬停/抓取 | 开局演出时间轴 | 原地上下漂浮、微弱 Yaw/Roll；光束下白鸟上吸进入 UFO | 无物理；Transform 与 Socket 附着 | 演出结束销毁/隐藏 |
| 树 / 石受击 | M6 HISM 命中或代理碎裂 | 尘土/木屑或石屑、2–4 片短寿命碎块 | 保持现有 HISM 移除或动态代理 | 1.0–1.8 s |
| 木 / 石 / 铁 / 玻璃砖断裂 | M7 实例移除、模块断裂、爆炸范围 | 材质专属粉尘/火星/玻璃片；短促断裂声 | 保持 HISM 移除或 `PromoteBrick` 动态模块 | 0.7–1.5 s |
| 黑鸟爆破 | 既有 `DetonateBlackBird` | 黑白闪光、圆形冲击波、烟尘、火星；鸟消失 | 保持现有径向 Gameplay 冲量 | 1.5 s |
| 炸药桶爆破 | 既有 Barrel damage / radial blast | 橙色闪光、火焰球、浓烟、金属片 | 保持现有径向 Gameplay 冲量；可选 3–5 预制桶片 | 2.0–3.0 s |

## 3. 资产与目录约定

新增资源集中在以下目录，避免污染 Gameplay 资产目录：

```text
Content/ABTS/Destruction/
  GeometryCollections/GC_ABTS_UFO_Broken
  Blueprints/BP_ABTSUFOPresentation
  Blueprints/BP_ABTSBreakablePresentationLibrary   // 可选，仅用于编辑器预览
  Niagara/NS_ABTS_Impact_Dust
  Niagara/NS_ABTS_Impact_Wood
  Niagara/NS_ABTS_Impact_Stone
  Niagara/NS_ABTS_Impact_Metal
  Niagara/NS_ABTS_Impact_Glass
  Niagara/NS_ABTS_Explosion_BlackBird
  Niagara/NS_ABTS_Explosion_Barrel
  Niagara/NS_ABTS_UFO_Impact
  Niagara/NS_ABTS_UFO_CaptureBeam
  Meshes/SM_ABTS_Debris_Wood_01...
  Meshes/SM_ABTS_Debris_Stone_01...
  Meshes/SM_ABTS_Debris_Metal_01...
  Meshes/SM_ABTS_Debris_Glass_01...
```

所有 Niagara System 统一暴露少量 User Parameters；C++ 只需写这些参数，不依赖 Niagara 内部模块结构：

```text
User.ImpactNormal       (Vector)
User.ImpactDirection    (Vector)
User.Intensity          (Float, 0..1)
User.Tint               (LinearColor)
User.DebrisScale        (Float)
```

## 4. UFO：唯一的 Chaos 破碎对象

### 4.1 表现状态

`BP_ABTSUFOPresentation` 应包含以下组件，不需要制作复杂动画蓝图：

| 组件 | 默认状态 | 用途 |
| --- | --- | --- |
| `IntactVisual`（Static Mesh） | 可见、无碰撞 | 完整 UFO，悬停与抓取阶段使用 |
| `BrokenVisual`（Geometry Collection） | 隐藏、不模拟 | 终局命中后显示并受一次冲量 |
| `CaptureBeam`（Niagara） | 隐藏 | 开局白鸟抓取光束 |
| `WhiteBirdPrisonSocket` | 仅 Socket | 白鸟进入 UFO 后的附着点 |
| `WhiteBirdReleaseSocket` | 仅 Socket | UFO 破碎后的白鸟释放点 |
| `ImpactCore` | Scene Component | 必须与 M11 的权威命中球视觉中心对齐 |

悬停使用纯函数曲线，不需要 Sequencer：

```text
LocalZ   = sin(Time * 1.3) * 10 cm
LocalYaw = sin(Time * 0.7) * 3 deg
LocalRoll= sin(Time * 1.1) * 2 deg
```

抓取时，白鸟从其地表支撑点以 `0.75 s EaseInOut` 插值到 `WhiteBirdPrisonSocket`。光束只做 Niagara 表现；白鸟关闭碰撞、物理与 Party 注册，不能被该插值推离或拉动其他 Gameplay 物体。

### 4.2 编辑器一次性制作步骤

1. 导入或选中 UFO Static Mesh，确认它是封闭网格、没有明显相互穿插的子网格；必要时先在 DCC 工具修正。  
2. 在 Content Browser 复制一份为 `SM_ABTS_UFO_Intact`；进入 **Fracture Mode**，点击 **New** 创建 `GC_ABTS_UFO_Broken`。  
3. 对 Geometry Collection 使用一次 `Uniform Voronoi`：首版 `12–20` 块，不做二级细分。启用 Cluster，使其受击时先整体保持、再一次散开。  
4. 给内切面分配较暗的金属材质；设置碎块在约 `3 s` 后移除/淡出。  
5. 创建 `BP_ABTSUFOPresentation`，放入完整网格、GC、两个 Scene Component 和一个 Niagara Component；只填写本文指定名称，其他状态由 C++ 切换。  
6. 在该蓝图的 Class Defaults 中指向 `GC_ABTS_UFO_Broken` 与 `NS_ABTS_UFO_CaptureBeam`；不添加 Tick 图表、Damage 图表或自定义碰撞逻辑。

完成后只需把这个 Blueprint Class 配置给终局/开局演出数据资产。Geometry Collection 的 Cluster 用于限制初始碎块数量与分离层级，也有助于控制模拟开销。[Cluster Geometry Collections](https://dev.epicgames.com/documentation/unreal-engine/cluster-geometry-collections-user-guide-in-unreal-engine)

## 5. HISM 树石与砖块：不转 Geometry Collection

HISM 的一个组件由大量共享网格实例构成，单个实例只能独立维护有限的实例数据；将其逐个替换为 Geometry Collection 会失去实例化优势。[ISM 文档](https://dev.epicgames.com/documentation/en-us/unreal-engine/instanced-static-mesh-component-in-unreal-engine)

当前项目已经具备正确的 Gameplay 骨架：

- M6 能将被命中的树石 HISM 实例移除或提升为 `AABTSM6DestructibleProxy`；
- M7 能对木、石、铁、玻璃 HISM 进行 `RemoveInstance` 或 `PromoteBrick`，并对模块调用 `BreakModule`；
- 黑鸟与桶已有爆炸半径、冲量和范围处理。

首版只在这些“已经决定破坏”的路径上补发一个表现事件：

```text
HISM instance / dynamic module breaks
  -> 读取命中位置、法线、材质和强度
  -> C++ 发出 BreakPresentationEvent
  -> Niagara impact/explosion + 可选短寿命碎片 Actor
  -> Gameplay 继续原有的移除、回收、冲量或动态模块流程
```

### 5.1 四类砖块视觉规则

| 材质 | Niagara 基底 | 可选预制碎块 | 色调 |
| --- | --- | --- | --- |
| 木 | 木屑 + 浅棕尘土 | 2 根小木片 | 暖棕 |
| 石 | 粗粉尘 + 石屑 | 3 块低模石片 | 灰褐 |
| 铁 | 火花 + 极少烟 | 2 个金属小片 | 冷灰 / 橙色火花 |
| 玻璃 | 亮闪片 + 很少粉尘 | 4–6 透明片（无碰撞） | 青白 |

预制碎片均关闭碰撞和物理，以随机初速度做局部 Transform 动画；这样既表现出“飞散”，又不产生额外 Chaos 体。仅 UFO 的大块碎片使用真实 Chaos。

## 6. C++ 实施方案

### 6.1 新增表现服务

新增一个世界级的 `UABTSDestructionPresentationSubsystem`，只负责接收事件、生成 Niagara 和管理短寿命碎片。它不拥有伤害规则，也不查询 Gameplay 的成败。

```cpp
enum class EABTSBreakPresentationKind : uint8
{
    Tree,
    Rock,
    WoodBrick,
    StoneBrick,
    IronBrick,
    GlassBrick,
    BlackBirdExplosion,
    BarrelExplosion,
    UFOImpact
};

struct FABTSBreakPresentationEvent
{
    EABTSBreakPresentationKind Kind;
    FVector Location;
    FVector Normal;
    FVector Direction;
    float Intensity;       // 0..1
    FLinearColor Tint;
};
```

服务通过一个 `UABTSDestructionPresentationSettings` Data Asset 保存软引用：每种 `Kind` 对应的 Niagara、碎片 Mesh 列表、最大碎片数、寿命、缩放区间和颜色。找不到资源时只记一次 Warning 并静默跳过表现，不阻断破坏玩法。

### 6.2 现有代码接入点

| 现有位置 | 新增动作 | 不可改动的职责 |
| --- | --- | --- |
| `AABTSM6DestructibleProxy::Shatter()` | 发 `Tree/Rock` 表现事件 | HISM/代理本身的伤害与移除规则 |
| `AABTSM7BuildingMaterialSystem` 的 HISM 移除、`PromoteBrick`、`BreakModule` 路径 | 按材质发四类砖块事件 | `RemoveInstance`、模块提升、回收、径向冲量 |
| `AABTSM6SlingshotSystem::DetonateBlackBird()` | 先发黑鸟爆炸事件，再执行既有爆炸 | 黑鸟 Gameplay 爆炸半径和伤害 |
| M7 Barrel 的 `ApplyRadialBlast` 前 | 发桶爆炸事件 | 既有桶的范围冲量与销毁 |
| M11 `TargetHit` 处理处 | 调用 `BP_ABTSUFOPresentation::PlayImpact()` | UFO 命中判定与 M11 轨迹结果 |

`PlayImpact()` 的唯一职责为：隐藏完整网格、显示并激活 Geometry Collection、在 `ImpactCore` 施加一次沿 `Direction` 的脉冲、生成 `UFOImpact` Niagara，并通知导演稿中的白鸟释放轨道。它不得再调用 M11 伤害、重力或状态推进。

### 6.3 性能防线

- 同一帧的碎片表现最多 `24` 个，单次普通砖块最多 `6` 个；超过上限只保留 Niagara。
- 碎片 Actor 禁止碰撞、Nav、Tick 和阴影；Transform 用 Timeline 或 Niagara Mesh Renderer 更新，寿命到期自动回收。
- UFO Geometry Collection 同时只能有一个；命中后 `3.5 s` 内回收，不保留离线 Chaos 碎块。
- 距离玩家相机超过 `4500 cm` 的普通破坏只生成低成本粒子，不生成预制碎片。
- 所有 Niagara 使用 Scalability，低画质去掉 Mesh Debris，保留闪光/尘土主发射器。

## 7. 编辑器工作清单（最小化版本）

| 必做项 | 预计操作量 | 说明 |
| --- | --- | --- |
| 创建一个 `GC_ABTS_UFO_Broken` | 约 10 分钟 | 仅一次 Fracture、12–20 块、一次 Cluster 调参 |
| 创建一个 `BP_ABTSUFOPresentation` | 约 10 分钟 | 按第 4.1 节放置 3 个组件与 3 个挂点 |
| 导入/复制 9 个 Niagara System | 约 15 分钟 | 不改内部发射器，仅重命名并填入 Data Asset |
| 准备 4 组低模碎片 Mesh | 可选 | 若没有现成资源，首版可完全跳过，只有 Niagara |
| 配置 `UABTSDestructionPresentationSettings` | 约 10 分钟 | 每个事件下拉选择 Niagara；其余默认值由 C++ 提供 |

不要为每棵树、石头、砖块、桶或黑鸟各建一个 Blueprint；它们都复用事件类型和 Data Asset。不要在关卡摆放破碎 Actor，也不要在蓝图 Tick 中检测命中。

## 8. 特效资源清单

### 8.1 必需下载：无

首版 C++ 接口只要求 Niagara System 软引用。若暂时没有外部资源，可先用引擎 Niagara 的基础模板做一个烟尘、火花和冲击波版本；Niagara 是 UE 原生系统，需确认项目启用了 Niagara 插件。[Niagara Quick Start](https://dev.epicgames.com/documentation/unreal-engine/quick-start-for-niagara-effects-in-unreal-engine)

### 8.2 推荐只下载一个包

推荐优先获取免费的 [Niagara Examples Pack](https://www.fab.com/listings/0e188eca-4e54-4fb2-a9ed-d8b8a565e600)。它包含爆炸、命中、火花、火焰、烟雾、拖尾等 Niagara 示例，并包含可参考的性能与可扩展性做法；导入后只复制需要的 System 到 `Content/ABTS/Destruction/Niagara`，不要让第三方内容直接成为运行时硬路径。

如果希望 UFO/桶爆炸更写实，可替换为免费的 [Realistic Niagara Explosions Pack](https://www.fab.com/listings/a48b3fa2-2ebf-42c2-8892-fa20a1eff289)。它偏写实，可能和本项目低模卡通风格不完全一致，因此不作为默认依赖。下载前请在 Fab 页面确认目标 UE 版本和当前许可证。

### 8.3 不建议下载的资源

- 不下载“可破坏环境整包”来替代当前 HISM / M6 / M7 逻辑；通常会引入自己的 Actor、伤害和存档体系。
- 不下载黑鸟、桶、树、砖的整套 Chaos 破碎包；首版需要的是统一视觉反馈，不是每个对象的高成本实时断裂。
- 不下载强制依赖专用插件的特效包；本方案只依赖 Engine 自带 Niagara 和 Chaos。

## 9. 验收清单

- [ ] UFO 在开局只做悬停/抓取，白鸟进入内部时不触发碰撞、不改变 Party。
- [ ] UFO 终局只在 `TargetHit` 破碎一次；Geometry Collection 结束后已回收，无残留物理体。
- [ ] 树、石、四类砖块仍按原有命中、移除、回收和模块提升规则工作；新增特效失败时玩法照常继续。
- [ ] 黑鸟与桶的冲击半径、伤害和物理结果在接入特效前后完全一致。
- [ ] 近景有可辨认的材质差异；远景不生成额外碎片 Actor。
- [ ] PIE 与 Standalone 中，连续爆炸后没有持续增长的 Niagara、碎片 Actor 或 Chaos Body 数量。

## 10. 推荐实施顺序

1. 先实现表现服务、Data Asset 和树石/砖块的 Niagara 事件，不生成预制碎块；
2. 接入黑鸟、炸药桶爆炸，验证它们不会改变原有冲量；
3. 制作 `GC_ABTS_UFO_Broken` 和 `BP_ABTSUFOPresentation`，接入开局抓取与 M11 `TargetHit`；
4. 若首版画面仍不够丰富，再增加四组低模预制碎片和距离预算。

