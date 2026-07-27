# M10 青翎侦察小地图设计与实现

> 文档定位：本稿记录 M10 的玩法闭环、固定球面投影、地图数据来源、编辑器配置、验收与排错。M10 不改变 TaskGraph、CellTopo、M3 地形生成或 M6 破坏规则，只把已有逻辑结果转译为一次侦察所得的固定圆形地图。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [UI 系统设计](UISystemDesign.md) · [M5.1 世界物品与弹弓放置](M51WorldItemsPlacementSlingshotDesign.md) · [M6 弹弓发射与碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M3 地形视觉表现](M3TaskGraphTerrainPresentationDesign.md) · [M10.1 超视距目标与引力走廊](M101BeyondHorizonLaunchInterfaceDesign.md)

## 1. 阶段目标

M10 完成以下最小闭环：

1. 玩家取得树枝与植物纤维；
2. 在一对弹弓槽中分别插入树枝，形成两根 Twig 弹弓桩；
3. 手持植物纤维，依次点击两根相邻树枝桩，连接 Twig 弹弓弦与弹珠袋；
4. 切换主控为青翎，点击弹珠袋进入 M6 发射模式；
5. 青翎落地、物理结算并完整归队后，屏幕左上角揭示一张以真实落点为中心的圆形小地图；
6. 地图范围与朝向固定不再跟随角色，但环境物体的 Chaos 位移、破坏状态和四只鸟的位置继续更新。

侦察地图的作用是把道路外目标、资源密度、河流阻断和建筑位置转化为玩家可规划的信息，不是全局上帝视角，也不是可点击传送界面。

## 2. 玩法规则

### 2.1 Twig 弹弓装配

- `Branch / 树枝` 同时是基础资源与 Twig 弹弓桩物品。手持树枝点击未占用弹弓槽，消耗 1 个树枝并在该槽的 CellTopo Cell 上生成一根 Twig 桩。
- 一套可用弹弓需要两根桩。两根桩必须同为树枝桩，且球面夹角不超过 M5.1 的 `MaxStakeArcRadians`；默认值为 `0.12 rad`。
- `PlantFiber / 植物纤维` 是 Twig 弹弓弦。手持植物纤维第一次点击树枝桩只记录第一根桩；点击第二根合法树枝桩后才消耗 1 个植物纤维并完成弹弓。
- 点击同一根桩会取消当前首桩选择；点击类型不匹配、已有弦或距离过远的桩不会完成连接。
- M9 的“任意放置弹弓”调试开关仍可让树枝桩绕过弹弓槽放在任意未占用 CellTopo Cell，但弦的同类型与球面弧度限制不变。正式玩法仍以弹弓槽为入口。

所有桩位、槽位与相邻判断以 CellTopo 为逻辑源；连续球面只提供最终地表位置、朝向和点击碰撞。

### 2.2 青翎专属发射

- Twig 弹弓只接受 `TwigScout` 能力，即当前版本的青翎。
- 其他鸟点击 Twig 弹珠袋时拒绝进入发射模式，不消耗弹弓或物品。
- 青翎进入弹弓后的拉拽、力度、轨迹、飞行、碰撞、物理结算和自动归队均复用 M6，不建立第二套发射逻辑。
- 世界全局 Chaos 预结算尚未完成时，任何弹弓发射都会被 M6 阻止；M10 不绕过 `WorldReady` 门。

### 2.3 揭示时机与重复侦察

M6 在青翎开始回归前记录已经稳定的真实落点，但只在以下状态全部恢复后广播发射完成：

- 发射鸟完成自动回归；
- 小队退出弹弓状态；
- 行走输入和小队相机恢复；
- M6 `LaunchState` 回到 `Inactive`。

M10 只响应 `BirdId == Blue` 的完成事件。其他鸟发射不会创建或移动侦察地图。

当前 M10 同时只保留一张有效侦察圆盘。青翎再次完成侦察后，新落点会替换旧中心、旧朝向和旧地形纹理；本阶段不做多个圆盘拼接、已探索区域并集或永久战争迷雾存档。

## 3. 固定球面到平面圆盘投影

### 3.1 固定参考系

设主星球心为 `O`，主星半径为 `R`，青翎落点为 `L`。揭示中心单位向量为：

```text
C = normalize(L - O)
```

小地图北向优先使用世界 `+Z` 在 `C` 的切平面上的投影：

```text
N0 = normalize(WorldUp - dot(WorldUp, C) * C)
```

若落点接近世界极点导致 `N0` 退化，则依次使用世界 `+X`、`+Y` 的切平面投影。之后构造正交基：

```text
E = normalize(cross(N, C))
N = normalize(cross(C, E))
```

`C`、`E`、`N` 只在揭示时计算一次。角色移动、相机旋转和主控切换都不会改变该参考系，因此地图不会旋转或追随玩家。

### 3.2 世界点投影到小地图

对任意世界点 `Pworld`：

```text
P = normalize(Pworld - O)
d = clamp(dot(C, P), -1, 1)
theta = acos(d)
s = R * theta
```

其中 `theta` 是球心夹角，`s` 是球面大圆弧长。设侦察半径为 `Smax`，若 `s > Smax`，该点不显示。否则：

```text
T = normalize(P - d * C)
q = s / Smax
x =  dot(T, E) * q
y = -dot(T, N) * q
```

`(x,y)` 是范围 `[-1,1]` 内的归一化圆盘坐标；`+X` 向东，屏幕 `+Y` 向下，因此北向使用负号。`x²+y² > 1` 的点不绘制。

这是方位等距近似映射：从中心出发的方向正确，距中心的球面弧长按比例映射到圆盘半径。它不保持远端面积和形状，但默认半径只有 `R/4`，形变足以满足路线与目标识别。

### 3.3 像素反向采样

生成地形纹理时，对圆盘像素 `(x,y)` 令：

```text
q = sqrt(x*x + y*y)
T = normalize(E*x - N*y)
theta = q * Smax / R
D = normalize(C*cos(theta) + T*sin(theta))
```

`D` 是该像素对应的球面单位方向。只向 M3 查询 `D`，不读取地形高度，因此地图表达的是径向俯视的地形语义，而不是带透视和山体遮挡的航拍图。

侦察半径最终限制在：

```text
10 cm <= Smax <= R * (PI - 0.01)
```

这样避免单张方位圆盘碰到对跖点歧义。

## 4. 地形 SDF 映射

### 4.1 数据来源

小地图不截图、不做 SceneCapture，也不从 GPU 材质回读颜色。它调用 M3 的 CPU 表现查询，仍由 CellTopo 和 TaskGraph 派生：

1. 先查询 line-feature Voronoi 的陆地底色；
2. 以 `TerrainBlendWidthCM` 查询道路中心线 SDF 权重；
3. 以同一宽度查询河流中心线 SDF 权重；
4. 按“陆地 → 道路 → 河流”的顺序混色。

颜色合成规则为：

```text
Color0 = GetDebugLandColor(D)
Color1 = lerp(Color0, RoadColor,  saturate(RoadWeight))
Color2 = lerp(Color1, RiverColor, saturate(RiverWeight))
```

河流最后覆盖，保证道路与河流交叉时水域仍有明确视觉优先级。

### 4.2 必须保持的约束

- 不允许把最近 Cell 的完整六边形直接涂成地形、道路或水体。
- `bWater` 只是河流相关 Gameplay 缓存，不能把整个 Cell 重新画成蓝色六边形。
- 地形交界使用现有线特征距离场；道路与河流使用连续中心线距离场。
- 地图忽略宏观高度和建筑施工台高度，但保留地形类别、道路和河流的颜色语义。
- 修改 M3 材质宽度和颜色后，小地图下一次侦察生成的新底图应读取同一组参数。

## 5. 标记来源与更新规则

### 5.1 环境标记

| 标记 | 权威来源 | 位置规则 | 消失规则 |
| --- | --- | --- | --- |
| 建筑 | `AABTSM73StableBuildingActor` | 所有仍存活运行时模块的世界位置质心 | 建筑生成未通过，或存活模块数为 0 |
| 树 | `ForestHISM` 与 Wood `AABTSM6DestructibleProxy` | HISM 世界变换或 Proxy 当前网格位置 | HISM 被移除且无代理，或代理被摧毁 |
| 石头 | `RockHISM` 与 Stone `AABTSM6DestructibleProxy` | HISM 世界变换或 Proxy 当前网格位置 | HISM 被移除且无代理，或代理被摧毁 |

建筑优先占用标记预算，其次是已经进入 Chaos 的 Proxy，最后才是剩余 Rock/Forest HISM。这样即使森林密集，关键建筑和正在运动的物体也不会先被裁掉。

启动 Chaos 只把检测到重叠的树石按批临时提升为 Proxy，并在每批结束后把最终 Transform 回写原 HISM，因此加载完成时绝大部分乃至全部树石仍由 HISM 合批。正式发射期间，被撞飞的单个实例才会长期成为 Proxy。M10 仍同时查询 HISM 与 M6 的有效 Proxy 注册表，以覆盖静态与动态两种状态。

HISM 的 `RemoveInstance` 会改变实例索引，M10 不跨刷新缓存 InstanceIndex。环境标记默认每 `0.20 s` 重新收集和投影；Proxy 移动、模块坍塌和物体销毁会在下一个刷新周期反映到地图中。采集 HISM 时先以侦察球冠对应的世界空间球体做粗筛，而不是每次读取全星球全部实例。

建筑标记使用存活模块质心，而不是永远固定的生成锚点。只采集 `ConfiguredPlanet` 等于当前主星的球面 TaskGraph 建筑，平面测试台或手摆的无主星建筑不会误投影进地图。这样整栋建筑被推移时图标会移动，模块全部销毁后图标会消失。单个碎块散开时仍只显示一个建筑图标，本阶段不为每块砖生成独立标记。

### 5.2 四鸟标记

- 四鸟位置在每次 HUD 绘制时从小队成员当前世界位置投影，不受环境刷新间隔影响。
- 配置了 `PortraitTexture` 时使用头像纹理；未配置时沿用 M4 的红、蓝、黄、黑纯色圆形回退。
- 鸟离开侦察圆盘范围时不贴边、不显示方向箭头，直接隐藏。
- 地图中心是历史落点，不是青翎回归后的当前位置；青翎归队后可能与其他鸟一起出现在弹弓附近，也可能已经位于地图范围外。

## 6. HUD 表现与层级

- 小地图位于屏幕左上角，外圈使用浅色描边，内部是带透明圆外区域的地形纹理。
- 地图底部显示 `SCOUT MAP`，明确其信息类型。
- 树、石头和建筑图标有纹理时绘制纹理；纹理缺失时分别回退为绿色、灰色、橙色圆点。
- 鸟标记使用黑色外圈增强地形上的可读性。
- 小地图没有点击热区，不拦截世界左键，也不承担放置、导航或传送功能。
- M10 HUD 先绘制小地图，再复用 M5 HUD。正常游戏时快捷栏与右侧头像位于其上层；打开背包/加工模态界面时，模态面板自然覆盖小地图，避免遮挡配方与库存信息。
- 发射开始、飞行、结算和回归期间不提前显示新地图；只有完整发射完成事件到达后才替换地图。

### 6.1 M10.1 强化弹弓预测叠加

M10 地图揭示完成后，可作为 [M10.1 超视距目标与引力走廊](M101BeyondHorizonLaunchInterfaceDesign.md) 的固定情报底图。本期新增的只读叠加规则为：

- 仅在强化弹弓处于 M6 `Pulling` 时读取当前同源弹道预测快照；
- 若预测器找到主星连续地表首次落点，且该落点位于当前侦察圆内，则把预测采样点投影为白色虚线，并以红色 `X` 标注落点；
- 落点不在侦察圆时，轨迹和 `X` 一起隐藏，不能贴边显示或暴露圆外情报；
- 轨迹只连接圆内的连续采样段，越界后断线，重新入界时另起一段；
- 松开左键、切换离开 `Pulling` 或失去有效预测时立即移除叠加；底图、环境图标和四鸟位置保持不变；
- M10 HUD 只投影 M6 的预测快照，不自行积分主星/卫星引力。

M10.1-A 已验收。M10.1-B 已完成 C++：屏幕中上部远端落点 `SceneCapture2D + RenderTarget` 画中画与小地图并列显示，HUD 会在视口允许时避开左上角侦察圆并在窄屏时缩小画框，待 PIE 视觉验收；它属于 [M10.1 详稿](M101BeyondHorizonLaunchInterfaceDesign.md) 的独立发射界面，不改变小地图的 CPU SDF 底图、固定投影和环境标记职责；超长轨迹二维轨道截面图仍属后续阶段。

## 7. 工程接口与职责边界

| 模块 | 职责 |
| --- | --- |
| `AABTSM10GameMode` | 继承 M9；选择 M10 HUD；向运行时地图系统传递编辑器配置 |
| `AABTSM10ScoutMapSystem` | 监听 M6 完成事件；固化球面参考系；生成底图；定时刷新环境标记 |
| `AABTSM10ScoutMapHUD` | 继承 M5 HUD；绘制圆盘、环境图标和四鸟头像 |
| `AABTSM6SlingshotSystem` | 记录回归前真实落点；回到 Inactive 后广播鸟种与落点；提供有效 Proxy 快照 |
| `AABTSM3Planet` | 提供与视觉材质同源的 CPU 地形/道路/河流颜色查询 |
| `AABTSM73StableBuildingActor` | 在自身私有模块归属内计算存活模块质心 |
| `AABTSM51WorldSystem` | 处理树枝桩、植物纤维弦、CellTopo 相邻限制和库存消耗 |

M10 只读取这些系统的公开结果。它不能反向修改 Cell 地形类型、道路、水网、建筑结构、HISM 归属或物理状态。

## 8. 编辑器参数

在 `AABTSM10GameMode` 的 `ABTS | M10 | Scout Map` 中配置：

| 参数 | 默认值 | 作用 |
| --- | ---: | --- |
| `Scout Radius Primary Ratio` | `0.25` | 未设置绝对半径时，侦察弧长为主星半径的该比例 |
| `Scout Radius Override CM` | `0` | 大于 0 时覆盖比例半径，单位 cm |
| `Terrain Texture Resolution` | `192` | 一次性地形纹理分辨率，限制 `64..512` |
| `Environment Refresh Interval Seconds` | `0.20` | 树石、Proxy、建筑标记刷新周期 |
| `Maximum Environment Marker Count` | `1024` | 环境标记总预算；建筑和动态 Proxy 优先 |
| `Environment Broadphase Padding CM` | `1400` | 球冠 HISM/Proxy 粗筛为地形高度和物体尺寸增加的余量 |
| `Map Diameter Px` | `310` | 屏幕圆盘直径 |
| `Top Left Margin Px` | `28` | 距屏幕左边和上边的边距 |
| `Tree Icon Texture` | `T_Icon_Tree` | 树标记纹理，可覆盖 |
| `Tree Icon Size Px` | `11` | 树图标像素尺寸 |
| `Stone Icon Texture` | `T_Icon_Stone` | 石头标记纹理，可覆盖 |
| `Stone Icon Size Px` | `12` | 石头图标像素尺寸 |
| `Building Icon Texture` | `T_Icon_Building` | 建筑标记纹理，可覆盖 |
| `Building Icon Size Px` | `25` | 建筑图标像素尺寸 |
| `Bird Icon Size Px` | `28` | 四鸟头像直径 |
| `Show Reinforced Trajectory Preview` | `true` | 强化弹弓 Pulling 时显示 M10.1 预测叠加 |
| `Trajectory Dash Length Px` | `6` | 白色虚线段长 |
| `Trajectory Gap Length Px` | `5` | 白色虚线间距 |
| `Trajectory Line Thickness Px` | `1.8` | 白色虚线线宽 |
| `Predicted Landing Cross Size Px` | `14` | 红色 `X` 尺寸 |
| `Predicted Landing Cross Thickness Px` | `2.5` | 红色 `X` 线宽 |
| `Show Reinforced Landing Preview` | `true` | M10.1-B 远端落点画中画总开关；只影响强化弹弓 Pulling 的独立 Capture，不影响小地图本体 |
| `Landing View Screen Width Px` | `420` | 远端落点画中画 HUD 宽度 |
| `Landing View Screen Height Px` | `236` | 远端落点画中画 HUD 高度 |
| `Landing View Top Margin Px` | `24` | 远端落点画中画距离屏幕上边缘的边距；水平方向居中 |
| `Render Target Width` | `512` | 远端落点 RenderTarget 宽度 |
| `Render Target Height` | `288` | 远端落点 RenderTarget 高度 |
| `Capture Hz` | `20` | 合法 Pulling 中 CaptureScene 更新频率上限 |
| `Camera Distance CM` | `1200` | 相机至预测落点的固定距离，沿落点前接触速度反向延长线放置 |
| `Field Of View Degrees` | `46` | 远端落点摄像机 FOV |
| `Trajectory Point Size CM` | `8` | 远端轨迹末段表现点尺寸 |
| `Trajectory Point Stride` | `2` | 远端轨迹末段表现点的预测采样步长 |
| `Landing View Trajectory Point Count` | `48` | 远端画中画保留的末段预测点上限 |
| `Landing View Trajectory Color` | 浅蓝白 | 远端轨迹表现点颜色 |

C++ 默认纹理路径为：

```text
/Game/Icons/Decorations/T_Icon_Tree
/Game/Icons/Decorations/T_Icon_Stone
/Game/Icons/Decorations/T_Icon_Building
```

若实际资产改名或未放在这些对象路径，默认引用会失败，但纯色回退仍可验收；在 GameMode Blueprint 中手工指定正确 Texture2D 即可。

`ScoutMapSystemClass` 位于 `ABTS | M10`，通常保持原生 `AABTSM10ScoutMapSystem`。只有需要 Blueprint 表现扩展时才替换子类。

## 9. 编辑器配置步骤

### 9.1 创建 M10 GameMode 配置

1. 在 Content Browser 中新建 Blueprint Class，父类选择 `AABTSM10GameMode`，建议命名 `BP_ABTSM10GameMode`。
2. 打开 Blueprint 的 Class Defaults，展开 `ABTS | M10 | Scout Map`。
3. 首轮保留 `Scout Radius Primary Ratio=0.25`、`Scout Radius Override CM=0`、`Terrain Texture Resolution=192`。
4. 为 Tree、Stone、Building 三个纹理槽指定 `Content/Icons/Decorations` 下的 Texture2D；若 C++ 默认路径已经正确解析，可以保留默认值。
5. 根据图标透明留白调整三个 `Icon Size Px`。不要通过放大地形纹理来补偿图标本身的大面积透明边缘，应先修正纹理裁切。
6. 保持 `Scout Map System Class` 为原生类。
7. 打开测试地图的 World Settings，把 `GameMode Override` 改为 `BP_ABTSM10GameMode`。
8. 检查该 Blueprint 没有把 `HUD Class` 序列化回旧的 M5 HUD；应继承或明确选择 `AABTSM10ScoutMapHUD`。

若已有基于 M9 的关卡 GameMode Blueprint，可将 Parent Class 重设为 M10 后重新检查全部继承参数；更稳妥的首轮验收方式是新建 M10 子类，避免旧 Blueprint 保存的 HUDClass 覆盖新默认值。

### 9.2 快速准备物品

可选择以下任一方法：

- 正常拾取出生区域生成的树枝和植物纤维；
- 在继承的 `ABTS | M8 | Debug` 中开启 `Seed Debug Maximum Inventory`，并保留默认最大数量，快速取得所有测试物品。

M9 的 `Allow Developer Any Cell Slingshot Stake Placement` 只用于排错任意 Cell 放置，不是 Twig 正常验收的必需条件。

### 9.3 PIE 装配与发射

1. 启动 PIE，等待日志出现 `[ABTS][StartupPhysics] Complete WorldReady=1`。
2. 在快捷栏或背包点击树枝，使其成为 HELD 物品。
3. 分别点击一对未占用弹弓槽，确认生成两根 Twig 桩且库存减少 2。
4. 手持植物纤维，先点击第一根 Twig 桩，再点击第二根 Twig 桩；确认出现两段弦和中间弹珠袋，库存减少 1。
5. 先用非青翎主控点击弹珠袋，确认被拒绝。
6. 切换到青翎，点击可见弹珠袋进入发射模式，完成拉动、发射、落地、结算和自动回归。
7. 等待小队相机恢复，确认左上角出现小地图。
8. 移动、切鸟和旋转相机，确认地图中心及北向不变，四鸟头像按实际位置移动。

### 9.4 M10.1-B 远端落点画中画验收准备

1. 沿用 9.3 的流程，先让青翎完成一次侦察，使左上角已存在有效小地图。
2. 切换至可使用强化弹弓的鸟，点击已连接的强化弹弓进入 `Ready`；在 GameMode Defaults 中确认 `Show Reinforced Landing Preview=true`。
3. 按住弹珠袋进入 `Pulling`，调整拉力，使 M6 预测到主星地表落点且落点位于当前青翎侦察圆内。
4. 确认屏幕中上部出现远端画中画：画面从轨迹接近方向观察预定落点，且能看到末段轨迹指向落点。
5. 连续微调鼠标和滚轮，确认主视图不被切走；远端画中画的地平径向 Up 稳定、不滚转、不反向。
6. 令预测落点离开侦察圆，或松开左键发射，确认画中画当帧消失；左上角侦察地图仍保留。

## 10. 运行日志

### 10.1 正常链路

```text
[ABTS][M10] Entry ready=1 StartCell=... RadiusRatio=0.250 OverrideCM=0.0 Icons(Tree=1 Stone=1 Building=1)
[ABTS][M5.1][Stake] Installed=Branch Cell=...
[ABTS][M5.1][Cord] FirstStake Cell=...
[ABTS][M5.1][Cord] Complete ArcRadians=... Item=Plant Fiber
[ABTS][M6][Return] Complete StaticProxies=...
[ABTS][M10][Reveal] Bird=Blue Landing=(...) Radius=2500.0 Ratio=0.250 Resolution=192 Markers=...
[ABTS][M10.1][LandingPreview] Camera spawned=ABTSM101LandingPreviewCamera_...
[ABTS][M10.1][LandingPreview] Activated Distance=1200.0 FOV=46.0 CaptureHz=20.0
[ABTS][M10.1][LandingPreview] Hidden
```

主星半径默认 `10000 cm` 时，比例 `0.25` 对应 `Radius=2500 cm`。

### 10.2 关键异常日志

```text
[ABTS][StartupPhysics] Launch blocked: world Chaos settling is still in progress.
[ABTS][M6][Enter] Rejected Bird=... Stake=Branch
[ABTS][M5.1][Place] Rejected Reason=StakeTypeOrOccupied
[ABTS][M5.1][Place] Rejected Reason=StakeArcOrType
[ABTS][M10][Reveal] Failed: primary planet or terrain texture was unavailable.
```

`Entry ready=1` 只说明 M10 系统已生成；真正地图创建成功必须看到 `[ABTS][M10][Reveal]`。

`[ABTS][M10.1][LandingPreview] Activated` 只会在强化弹弓 `Pulling`、预测落点在当前侦察圆内时出现；在松开、发射或落点离圈后应紧随一条 `Hidden`，而不会继续出现新的 `CaptureScene` 更新。

## 11. 验收清单

### 11.1 玩法闭环

- [ ] 树枝能在两个槽位分别生成 Twig 桩，每根消耗 1 个树枝。
- [ ] 植物纤维只能连接两根相邻且未占用的树枝桩，成功后消耗 1 个。
- [ ] 非青翎无法使用 Twig 弹弓；青翎可以进入完整 M6 发射流程。
- [ ] 小地图不会在飞行、落地震荡或自动回归中提前出现。
- [ ] 青翎完整归队后出现地图，中心对应回归前记录的真实稳定落点。
- [ ] 再次使用青翎侦察会以新落点替换旧地图。

### 11.2 投影与地形

- [ ] 默认覆盖弧长为主星半径的 `1/4`。
- [ ] 移动角色、旋转相机和切换主控不会平移或旋转小地图。
- [ ] 地形交界平滑，没有六边形拼接回归。
- [ ] 道路显示连续带状中心线，河流显示连续带状中心线，河流交叉处具有最终颜色优先级。
- [ ] 地图忽略地表高度，不因山峰透视或角色海拔改变形状。
- [ ] 圆盘外像素透明，边框完整，无方形黑底。

### 11.3 动态标记

- [ ] 世界加载后的树石继续从 HISM 显示标记；发射中提升为 Proxy 后标记不中断。
- [ ] 发射过程中被撞飞的树石图标在刷新周期内移动；被摧毁后消失。
- [ ] 建筑图标位于存活模块质心；整体位移后跟随，全部模块摧毁后消失。
- [ ] 密集森林达到预算时，建筑和动态 Proxy 仍优先保留。
- [ ] 四鸟头像逐帧反映当前位置；没有头像纹理时显示红蓝黄黑回退圆形。

### 11.4 UI 层级

- [ ] 地图位于左上角，不遮挡右侧小队头像和底部快捷栏。
- [ ] 打开背包/加工界面后，模态面板覆盖地图。
- [ ] 点击地图区域不会阻止世界点击，也不会触发传送或放置。

### 11.5 M10.1-A 预测叠加（已验收）

- [x] 强化弹弓 Pulling 且预测落点位于侦察圆内时，白色虚线与红色 `X` 同时出现。
- [x] 落点不在侦察圆、未找到地表落点、松开左键或使用其他档位时，两者均不显示。
- [x] 虚线在圆盘边界正确断开，不出现跨盘长线；红色 `X` 不贴边伪造圆外落点。
- [x] 预测叠加位于地形和环境标记之上，红色落点不会被建筑图标遮住；四鸟头像仍在最上层。

### 11.6 M10.1-B 远端落点画中画（C++ 已完成，待 PIE 验收）

- [x] 仅在强化弹弓 `Pulling`、已有有效侦察图、预测存在首次主星地表落点且该落点属于侦察圆时显示。
- [x] 相机位置由落点前接触速度的反向延长线和 `Camera Distance CM` 唯一决定，相机始终看向落点。
- [x] 画面 Up 从落点径向向量投影得到，连续调弓时无 Roll、无翻转。
- [x] 远端 RenderTarget 显示同源预测轨迹末段，且末端落点可见。
- [x] 松开、发射、返回、预测无落点或落点离开侦察圆时，当帧隐藏并停止捕获；M10 小地图不受影响。
- [x] 同时最多一台复用 SceneCapture2D，捕获频率不超过 `Capture Hz`。

## 12. 性能预算

- 地形底图只在青翎侦察完成时同步生成一次。默认 `192²` 共查询 `36,864` 个像素；`512²` 约为其 7.1 倍，只用于高质量截图，不建议作为默认比赛配置。
- M3 为小地图提供不计算高度和地表法线的颜色专用 SDF 查询；同一像素行把前一个 CellId 作为下一像素的邻近搜索提示，避免从 Cell 0 重走整条球面拓扑。
- 底图是 `PF_B8G8R8A8` 瞬态纹理，开启 sRGB、双线性过滤、Clamp 与 NeverStream。圆盘外像素 Alpha 为 0。
- 环境标记默认 5 Hz 更新，四鸟标记逐帧更新。不要把环境刷新周期设成 HUD 帧率。
- 默认环境标记上限为 1024。建筑先加入、动态 Proxy 次之、静态 Rock/Forest HISM 最后加入。
- HISM 先用侦察圆对应的球冠包围球调用实例空间索引，只对候选实例读取世界 Transform 和执行球面投影；默认半径下不再以 5 Hz 全扫约 8500 个全星球实例。
- 再次侦察会创建新的瞬态纹理并替换旧引用；旧纹理由 UObject 生命周期回收，不持久化到 Content。
- 当前 CPU 查询从球面方向重建 SDF，不产生 GPU Readback、SceneCapture 或第二套高细分地形网格。
- M10.1 预测叠加只投影 M6 已缓存的采样点并绘制 Canvas 线段，不重新积分弹道、不重建底图，也不提高环境标记刷新频率。
- M10.1-B 仅在合法 Pulling 时以 `Capture Hz` 更新一台可复用的远端 `SceneCapture2D`；HUD 在两次捕获间复用最近 RenderTarget，资格失效当帧隐藏而不是等待下一帧 Capture。

若首次揭示出现可感知卡顿，按以下顺序优化：先将分辨率降至 `192` 或 `128`，再考虑分帧生成纹理；不得退回最近 Cell 六边形着色。

## 13. 排错表

| 现象 | 原因与处理 |
| --- | --- |
| PIE 中完全没有 M10 日志 | 地图仍使用 M9/M8 GameMode；将 World Settings 的 GameMode Override 改为 M10 子类 |
| `Entry ready=1` 但发射后没有地图 | 只有青翎完成事件会触发；必须等待自动归队和 M6 Inactive，而不是刚落地就检查 |
| 点击 Twig 弹珠袋被拒绝 | 当前主控不是青翎，或该弹弓不是由 Branch 桩形成的 Twig Tier；查看 `[ABTS][M6][Enter] Rejected` |
| 第一次点植物纤维后没有弦 | 正常行为；第一次只记录首桩，必须再点第二根合法树枝桩 |
| 第二根桩点击后仍失败 | 两桩类型不同、已有弦、超过 `MaxStakeArcRadians`，或重复点击同一根；查看 `StakeArcOrType` / `StakeTypeOrOccupied` |
| 树石图标全部缺失 | 检查 Forest/Rock HISM InstanceCount、M6 有效 Proxy 与 `Environment Broadphase Padding CM`；默认启动预结算结束后应回写 HISM，日志应有 `BatchRestored ... DynamicProxies=0` |
| 被撞飞的树图标停在原位 | 检查环境刷新周期；动态图标必须来自 M6 有效 Proxy，而非缓存旧 HISM InstanceIndex |
| 建筑坍塌但图标不动 | 建筑标记必须调用 StableBuilding 的存活模块质心查询，不能直接使用固定 Actor 锚点 |
| 建筑完全摧毁后仍有图标 | 确认建筑的所有 RuntimeModule 已实际 Destroy；Foundation 不计入存活模块 |
| 森林有图标但建筑缺失 | 建筑生成可能未通过 `GenerationSummary.bAccepted`，或模块已全部销毁；建筑应先于森林占用预算 |
| 地图重新出现六边形水块 | 使用了 Cell `bWater` 或最近 Cell 颜色；必须走 `QueryScoutMapTerrainColor` 的陆地 line-SDF + 河流中心线混色 |
| 道路/河流宽度与地表不同 | 小地图必须使用 `TerrainBlendWidthCM`，不能误用 `SurfacePhysicsBlendWidthCM` |
| 地图南北颠倒 | 屏幕 Y 轴向下，公式必须使用 `y=-dot(T,N)`；地图朝向与相机无关 |
| 地图显示方形黑底 | 圆盘外像素 Alpha 应为 0，HUD 纹理混合需保留 Alpha；检查是否用不透明材质替换了瞬态纹理 |
| 图标资产未自动加载 | 资产对象名或路径与默认硬引用不一致；在 M10 GameMode 的三个 Texture 槽中手工指定，或接受纯色回退 |
| 打开背包后看不到小地图 | 这是既定模态层级：背包/加工面板覆盖地图，关闭后恢复 |
| 地图标记忽多忽少 | 达到 `MaximumEnvironmentMarkerCount`；建筑和 Proxy 优先，适当提高预算或减小覆盖半径 |
| 强化弹弓拉弓时没有预测轨迹 | 必须已经有有效青翎地图、弹弓 Tier 为 Reinforced、M6 状态为 Pulling，且预测器找到的首次主星地表落点位于侦察圆内 |
| 只有一小段轨迹但没有红色 X | 不允许出现此状态；落点资格失败时整条预测叠加都应隐藏，检查 HUD 是否先验证落点投影再绘制采样点 |
| 虚线横跨整个圆盘 | 两个圆内采样点之间夹有越界点或投影跳变；越界必须断段，异常大的相邻屏幕距离也应拒绝连接 |
| 远端画中画从未出现 | 检查强化弹弓、M6 `Pulling`、有效侦察图、首个主星落点和落点投影资格；任一不成立均应隐藏 |
| 远端画面上下颠倒或滚转 | 相机 Up 未从落点径向向量投影，或投影退化没有受控回退；参照 M10.1-B 的 `ScreenUp` 规则 |
| 远端画中画在松开后仍停留 | Capture 的资格检查只放在限频分支；应每帧先隐藏，再按频率决定是否 CaptureScene |
| 拉弓后明显掉帧 | 重复创建 SceneCapture/RenderTarget，或 Capture Hz 未限速；对象应复用，默认 20 Hz |
| 首次揭示明显卡顿 | 将 `TerrainTextureResolution` 从默认 192 降到 128；不得改回最近 Cell 六边形着色 |
| 切换 M10 后尚未侦察就持续极卡 | 这不是小地图底图；检查 `[StartupPhysics] Begin`。不得出现 `PromotedHISM=8526 DynamicBodies=8594` 的旧全量路径，应看到 `Candidates`、`BatchLimit`、`BatchRestored` 和最终 `WorldReady=1` |

## 14. 本阶段不实现

- 多次侦察区域的并集合并、战争迷雾保存和读档；
- 小地图点击、路径规划、导航箭头、缩放与拖拽；
- M10 小地图自身的 SceneCapture 航拍、山体遮挡、高度阴影或建筑轮廓栅格化；M10.1-B 已实现的远端落点摄像机是独立画中画系统，不改变本条边界；
- 每块建筑砖的独立图标；
- 卫星表面小地图与主星/卫星跨天体投影；
- 地形运行时改造后的底图局部重绘；当前地形底图在下一次青翎侦察时整体重建。
