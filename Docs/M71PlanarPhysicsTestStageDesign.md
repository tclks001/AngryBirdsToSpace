# M7.1：平面物理测试台与手工关卡搭建设计

> 状态：C++ 已实现。M7.1 是与正式球面 TaskGraph 地图并行的实验关卡，不改变正式地图的 `CellTopo` 逻辑源约束。
> M7 文档关系与当前执行路线见 [M7 建筑系统文档导航](M7BuildingDevelopmentRoadmap.md)。
>
> 父级：[M7 模块化建筑基础材料与装置](M7BuildingMaterialsAndDevicesDesign.md)。前置：[M6SlingshotLaunchAndImpactDesign.md](M6SlingshotLaunchAndImpactDesign.md)。
>
> M7.3-A 已提供可直接拖入本测试台、生成并击打的 `M7.3-A Stable Building Generator`；其参数、空载 Chaos 验证和验收步骤见 [M73AStableBlockBuildingImplementationDesign.md](M73AStableBlockBuildingImplementationDesign.md)。原 `M7.1 Modular Building Anchor` 仅保留为旧布局标记。
>
> 弱点测试下游：[M73BWeakPointAndDifficultyDesign.md](M73BWeakPointAndDifficultyDesign.md) 负责选点/难度，[M73B2StructuralWeaknessAndFailureValidationDesign.md](M73B2StructuralWeaknessAndFailureValidationDesign.md) 负责三种结构弱点及实际击打对照。项目总览见 [AngryBirdsToSpaceGameDesign.md](AngryBirdsToSpaceGameDesign.md)。

## 1. 阶段目标与边界

M7.1 提供一个可重复、可手工摆放的平面物理测试环境，用于快速调整弹弓方向、物体尺度、碰撞组合以及后续模块化建筑。它不生成 TaskGraph、不创建球面 Planet、不运行 M3 PCG，也不把测试摆放写入 CellTopo。

本阶段验收内容：

- 在编辑器中直接拖入并变换 Floor、树 HISM、石头 HISM、四材质砖、炸药桶、弹簧活塞和四档完整弹弓；
- 移动、旋转、缩放后，编辑器视口立即显示最终形态；
- 四档弹弓进入 PIE 后均生成两根可用桩和一条可点击弹弓弦；
- 可放置专用玩家起点，PIE 后四鸟队伍从该位置生成；
- 平面关卡使用 Chaos 刚体、恒定方向重力和平面相机，不依赖球心或地表半径查询；
- 预留模块化建筑生成锚点，但不在 M7.1 生成完整建筑或计算质量。

## 2. 运行结构

```text
AABTSM71PhysicsTestGameMode
├─ AABTSM71PhysicsTestStage       平面、碰撞与局部 Z 上方向
├─ AABTSM71PlayerStart            初始位置；局部 X 为朝向
├─ AABTSBirdParty                 平面四鸟队伍
├─ AABTSM6SlingshotSystem         恒向重力弹道与发射闭环
├─ AABTSM7BuildingMaterialSystem  保留 M7 材料服务
└─ 手工摆放 Actor
   ├─ Tree/Rock HISM
   ├─ Wood/Stone/Iron/Glass Brick
   ├─ Explosive Barrel / Spring Piston
   ├─ Twig/Simple/Reinforced/Space Slingshot
   └─ Modular Building Anchor
```

正式球面模式与测试台模式使用显式开关分流：测试台 GameMode 才会调用 `InitializePlanarParty`、`EnablePlanarChaosMovement` 和 `ConfigurePlanarTestMode`。原 M3—M7 GameMode 仍走 Planet、径向重力和球面距离，不会自动进入平面模式。

## 3. 可摆放 Actor

### 3.1 测试台与出生点

`AABTSM71PhysicsTestStage` 的 Actor 原点表示可行走平面，Floor 网格从该平面向局部 `-Z` 延伸。`Floor Size CM` 和 `Floor Thickness CM` 控制基础尺寸；Actor 自身仍可平移、旋转和缩放。局部 `+Z` 是整张测试台的重力反方向。

`AABTSM71PlayerStart` 的局部 `+X` 是初始面向。起点可以直接放在 Floor 表面；运行时若起点高度低于鸟的碰撞球半径，GameMode 会仅沿测试台 Up 抬高到安全高度，避免开局穿模。

### 3.2 树、石头与砖

下列原生类可直接从 Place Actors 面板拖入：

| 搜索名 | C++ 类 | 缩放规则 |
| --- | --- | --- |
| `M7.1 Tree HISM` | `AABTSM71TreeHISMActor` | XYZ 任意非均匀缩放 |
| `M7.1 Rock HISM` | `AABTSM71RockHISMActor` | XYZ 任意非均匀缩放 |
| `M7.1 Wood Brick` | `AABTSM71WoodBrickActor` | XYZ 分别对应砖长宽高 |
| `M7.1 Stone Brick` | `AABTSM71StoneBrickActor` | 同上 |
| `M7.1 Iron Brick` | `AABTSM71IronBrickActor` | 同上 |
| `M7.1 Glass Brick` | `AABTSM71GlassBrickActor` | 同上 |
| `M7.1 Explosive Barrel` | `AABTSM71ExplosiveBarrelActor` | XYZ 任意缩放；局部 Z 为桶轴 |
| `M7.1 Spring Piston` | `AABTSM71SpringPistonActor` | XYZ 任意缩放；局部 Z 为双向冲击轴 |

每个对象内部只有一个 identity HISM 实例，Actor Transform 是唯一编辑器变换源。替换 `Instance Mesh` 或 `Instance Material`、移动、旋转或缩放 Actor 时，`OnConstruction` 会立即重建预览。四种砖共享引擎 Cube 回退网格；未配置正式材质时使用颜色回退。

M7.1 暂不根据缩放计算质量，但保留完整 Actor Scale。后续质量公式只能读取最终世界尺度和材质密度，不应把编辑器缩放烘焙后丢失。

### 3.3 炸药桶与弹簧活塞

`M7.1 Explosive Barrel` 与 `M7.1 Spring Piston` 是可直接拖入测试关卡的动态装置 Actor。编辑器中显示无碰撞预览圆柱；PIE 时由 Actor 的 `Length CM`、`Diameter CM`、位置、旋转、缩放和可选网格/材质生成真正的 `AABTSM7BuildingModule`，因此复用 M7 的 Chaos、累计损伤、发射阶段重力和破坏规则。

- 局部 `+Z` 是圆柱轴；炸药桶爆炸和弹簧活塞冲击均以此方向为依据。
- `Length CM`、`Diameter CM` 定义基础尺寸，Actor 的 XYZ Scale 保留到运行时模块。
- 炸药桶受足够损伤后执行近处破坏、远处径向冲击；弹簧活塞执行沿局部 `±Z` 的近端破坏和远端冲击。
- Preview 进入 PIE 后隐藏，运行时模块是唯一的碰撞实体，避免重复碰撞。

### 3.4 四档完整弹弓

| 搜索名 | Tier | 默认资格 |
| --- | --- | --- |
| `M7.1 Twig Slingshot` | Twig | 仅青翎（TwigScout） |
| `M7.1 Simple Slingshot` | Simple | 红、蓝、黄 |
| `M7.1 Reinforced Slingshot` | Reinforced | 四鸟 |
| `M7.1 Space Slingshot` | Space | 四鸟；后续可追加终局参数 |

弹弓坐标约定固定为：

- 两根桩默认沿局部 Y 轴对称分布；
- 局部 `+X` 是发射方向，编辑器绿色箭头显示该方向；
- 局部 `+Z` 是弹弓上方向；
- Actor 的 Y Scale 乘到 `Base Stake Spacing CM`，控制两桩间距；
- Actor 的 X/Z Scale 被预览与运行时生成逻辑主动忽略，不改变弹弓位置、桩尺寸、弦高度或发射框架；
- `Stake Height CM`、`Stake Diameter CM`、`Cord Thickness CM` 通过独立参数调整，不借用 X/Z Scale。

在 `ABTS | M7.1 | Slingshot | Stake`、`Cord`、`Pouch` 下，三类部件均有独立的 `Mesh`、`Material`、`Local Offset CM`、`Local Rotation`、`Local Scale`。这些局部校正用于适配不同资产 Pivot 和轴向：Stake/Cord/Pouch 同时用于编辑器 Preview 和 PIE 的实际交互 Actor。待机时两根弦连接桩顶和位于中间的袋体；M6 瞄准/拉伸时袋体随弹丸袋运动、双弦实时拉伸，松开后恢复待机构型。

编辑器中显示的是无碰撞 Preview Component。进入 PIE 后，摆放 Actor 按同一世界变换生成两个 `AABTSM51SlingshotStake` 和一个 `AABTSM51SlingshotCord`；预览自动隐藏，因此不会产生双重碰撞。Cord 保存独立 `EABTSSlingshotTier`，不再只依靠 Simple/Reinforced 物品枚举猜测发射资格。

### 3.5 模块化建筑预留

`AABTSM71ModularBuildingAnchor` 仅提供位置、旋转、缩放和局部 `+X` 箭头。后续建筑生成器应从该锚点读取世界变换并生成模块，不应让 M7.1 锚点承担承重图、布局搜索或质量计算。

## 4. 平面物理适配

测试台模式下：

- `UABTSChaosBirdMovementComponent` 使用固定 `PlanarUp`，重力恒为 `-PlanarUp * 980 cm/s²`；
- WASD 目标速度投影到与 `PlanarUp` 垂直的平面；
- 接地仍来自 Chaos 的真实阻挡碰撞法线，不使用地表半径吸附；
- 跳跃沿 `PlanarUp` 写入速度；
- 鸟群距离改为欧氏距离，出生、跟随分离、跳跃高度和脱队恢复均使用同一平面 Up；
- M4 相机使用固定 Up 进行无滚转环绕和切换插值；
- M6 预览弹道和飞行使用恒向重力，等待鸟、发射后返回点均投影到测试平面；
- 被撞出的 HISM 临时代理使用同一恒向重力。

Floor 的复杂碰撞由其 Static Mesh 碰撞设置决定。默认 Engine Cube 使用简单盒碰撞，足够用于 M7.1；如替换 Floor Mesh，应确保新网格拥有可用的 Simple Collision 或按项目测试需求启用 Complex as Simple。

## 5. 编辑器搭建步骤

### 5.1 创建关卡

1. 在 Content Browser 新建空白 Level，例如 `L_M71_PhysicsTestStage`。
2. 删除模板关卡中不需要的地形；保留或添加 `DirectionalLight`、`SkyLight`、`SkyAtmosphere` 和 `ExponentialHeightFog` 以便观察材质。
3. 打开 `World Settings`，将 `GameMode Override` 设置为原生 `M7.1 Planar Physics Test GameMode`；也可以先创建其 Blueprint 子类 `BP_ABTSM71PhysicsTestGameMode` 再选用。
4. 确认关卡中没有 `AABTSM2Planet`、`AABTSM3Planet`、TaskGraph Planet 或旧的自动生成测试系统。

### 5.2 放置测试台与玩家

1. 在 Place Actors 搜索 `M7.1 Physics Test Stage` 并拖入，建议初始 Location=`(0,0,0)`、Rotation=`(0,0,0)`、Scale=`(1,1,1)`。
2. 在 Details 的 `ABTS | M7.1 | Floor` 设置 `Floor Size CM`，默认 `12000 × 12000`；按需配置 Floor Material。
3. 搜索 `M7.1 Bird Player Start`，将其放在 Floor 表面附近。
4. 旋转 PlayerStart 的 Z/Yaw，使红鸟初始朝向希望的测试区域；不要用 Roll/Pitch 倾斜出生方向。

### 5.3 放置测试物体

1. 分别拖入 Tree HISM、Rock HISM、Wood/Stone/Iron/Glass Brick，以及 Explosive Barrel、Spring Piston。
2. 使用普通 Transform 工具进行平移、旋转和 XYZ 非均匀缩放；确认视口中的实例同步变化。
3. 正式美术资产到位后，在各 Actor 的 `ABTS | M7.1 | Assets` 设置 `Instance Mesh` 和 `Instance Material`。
4. 将 Mesh Pivot 放在物体落地基准面；若资产 Pivot 不在底部，应先在 Static Mesh 编辑流程中修正，避免靠 Actor 偏移掩盖问题。
5. 选择装置，在 `ABTS | M7.1 | Device` 调整 `Length CM`、`Diameter CM`；通过常规 Transform 工具摆放、旋转和 XYZ 缩放。
6. 拖入 `M7.1 Modular Building Anchor`，作为未来自动建筑生成位置标识。

### 5.4 放置弹弓

1. 分别拖入 Twig、Simple、Reinforced、Space 四种 Slingshot。
2. 保持默认旋转时，两桩沿世界 Y，绿色箭头朝世界 +X。
3. 只修改 Actor 的 Y Scale 测试桩距，例如 `Y=0.7/1.0/1.5`；即使误改 X/Z Scale，弹弓视觉与运行时尺寸也应保持不变。
4. 旋转 Actor 的 Yaw 以覆盖多个发射方向；若测试台本身被旋转，弹弓仍以自身局部 Z 为上，建议使弹弓局部 Z 与测试台 Up 对齐。
5. 如需不同桩尺寸或弦高度，直接调整 `Base Stake Spacing CM`、`Stake Height CM`、`Stake Diameter CM`、`Cord Thickness CM`。
6. 在 Stake/Cord/Pouch 三个折叠栏绑定模型；模型 Pivot 或局部轴不匹配时，只调该部件的 Local Offset/Rotation/Scale，不要用弹弓 Actor 的 X/Z Scale 修正。

## 6. PIE 验收

1. 启动 PIE，日志应包含：

   ```text
   [ABTS][M7.1] Planar test stage ready Party=1 Slingshot=1 Materials=1
   [ABTS][M7.1][Party] Planar=1 Members=4
   [ABTS][M7.1][Slingshot] Spawned Tier=...
   ```

2. 四鸟在 PlayerStart 附近排队生成，红鸟位于起点且不会穿过 Floor。
3. WASD 可在平面移动，Space 沿测试台 Up 跳跃，Tab/右侧头像可切换主控，相机保持平面世界无滚转。
4. 鸟、Floor、树、石头和砖之间存在真实 Chaos 阻挡；树石 HISM 和砖在行走时保持静态。
5. 点击四种弹弓弦均可尝试进入 M6；不满足 Tier 的鸟应被拒绝，满足者进入弹丸袋。
6. 弹道呈恒定重力抛物线；发射、飞行、落地静默和返回链路不查询 Planet。
7. 编辑器退出 PIE 后，所有手工摆放 Actor 保持原 Transform，不产生运行时 Stake/Cord/Device 残留资产。
8. 进入发射模式后，桶和活塞成为受恒向重力影响的 Chaos 刚体；撞击桶应触发径向近破坏/远冲击，撞击活塞应触发沿局部 `±Z` 的近破坏/远冲击，日志应包含 `[ABTS][M7.1][Device] Spawned`。

## 7. 当前不做

- 不提供自动生成 `.umap`；关卡按本稿在编辑器中手工搭建和保存；
- 不按缩放、材质密度或体积计算质量；
- 不生成完整模块化建筑，不计算连接、约束、承重图或结构坍塌；
- 不把平面摆放转换为 CellTopo 数据，也不替代正式球面 PCG 验收；
- 不在 M7.1 中恢复 M5.1 的随机拾取、弹弓槽和工作台自动生成。
