# M4：鸟群跟随、主控切换与常驻 HUD

> 状态：C++ 已实现，等待编辑器视觉与交互验收。
>
> Gameplay 规则来源：[BirdPartyFollowingGameplayDesign.md](BirdPartyFollowingGameplayDesign.md)。本稿只说明 M4 落地、编辑器配置和验收。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [Chaos 刚体移动](ChaosRigidBodyMovementDesign.md) · [球面 Orbit Camera](M4MultiCharacterOrbitCameraDesign.md) · [UI 系统](UISystemDesign.md) · [M5 背包与加工](M5InventoryCraftingImplementationDesign.md)

## 1. 本阶段范围

M4 在 M3 球面 TaskGraph 地图上生成四只可见鸟，并完成：

- 固定四鸟身份：绯翼、青翎、棱喙、玄爪。
- 以当前主控为根的无环队列链。
- 前导鸟 breadcrumb 路径历史跟随，而不是所有鸟直接追主控。
- 跟随启停距离迟滞、Arrival 减速、鸟间 Separation。
- 主控跳跃生成路径事件，后排到达跳点且跳跃高度确有需要时逐级跳跃。
- Tab 循环切换和 HUD 头像点击切换。
- 屏幕右侧固定红、蓝、黄、黑四头像位置；当前主控只高亮，不重排图标。
- 各鸟模型和头像纹理可由关卡 Settings Actor 配置。

本阶段不实现弹弓 Actor、槽位占用、拉弦、弹道或发射。每只鸟已经公开其弹弓能力资格：

| 鸟 | 预留资格 |
| --- | --- |
| 绯翼 | `Simple` |
| 青翎 | `TwigScout` |
| 棱喙 | `Simple` |
| 玄爪 | `Reinforced` |

后续弹弓只需查询鸟的 `SlingshotCapability` / `CanUseSlingshotCapability`，不修改队伍跟随规则。

## 2. 运行流程

```text
ABTSM4GameMode
-> 复用 M3 地图生成与道路起点放置
-> 创建 ABTSBirdParty
-> 把初始 Pawn 设为绯翼主控
-> 沿道路后方生成蓝、黄、黑三只跟随鸟
-> 每只鸟复用相同的 ForceSuspension / LegacySweep 选择
-> 记录各鸟 breadcrumb 和 Jump Event
-> HUD 读取固定 BirdId 顺序绘制四个圆形头像
```

队伍成员生成使用初始主控鸟的实际 Class，因此现有 `BP_ABTSM25BirdCharacter` 的通用外观、相机和移动参数可以继承。`ABTSBirdPartySettings` 中配置的单鸟 Mesh 会在生成后覆盖通用 Mesh；未配置时使用引擎 Sphere。

## 3. 编辑器操作

### 3.1 创建 M4 地图

1. 复制 `/Game/Maps/L_ABTS_M3` 为 `/Game/Maps/L_ABTS_M4`。
2. 打开 `L_ABTS_M4`。
3. 在 **World Settings > GameMode Override** 选择原生 `ABTSM4GameMode`。
4. 保留 `BP_ABTSM3Planet`、光照、PlayerStart 和 `ABTSMovementModeSelector`。
5. 建议 `ABTSMovementModeSelector` 继续选择 `Force + Radial Suspension`。

### 3.2 配置模型和头像

1. 在 **Place Actors** 搜索 `ABTSBirdPartySettings` 并放入关卡。
2. 选中该 Actor，在 **Details > ABTS > M4 > Birds** 展开四个固定条目。
3. 条目顺序和 `BirdId` 保持：Red、Blue、Yellow、Black。
4. 每个条目可设置：
   - `Bird Mesh`：对应鸟模型的 Static Mesh。
   - `Portrait Texture`：HUD 头像 `Texture2D`。
   - `Fallback Color`：头像缺失时的纯色。
   - `Display Name`：调试和未来 UI 名称。
   - `Slingshot Capability`：预留弹弓资格，建议保持默认。
5. 保存地图。

如果不放置 Settings Actor，M4 仍可运行：四鸟模型回退到当前默认球体，HUD 依次回退为红、蓝、黄、黑圆形。

头像贴图建议为正方形，主体留在中心圆形安全区；HUD 会将贴图裁成 48 边近似圆形。头像尺寸、间距和右边距也可在 Settings Actor 的 HUD 分类调整。

### 3.3 输入

- `WASD`：移动当前主控鸟。
- 鼠标：当前主控鸟相机。
- `Space`：当前主控鸟跳跃，并向其路径写入 Jump Event。
- `Tab`：按 Red → Blue → Yellow → Black 的固定身份顺序循环切换。
- 鼠标点击右侧头像：直接切换到对应鸟。

HUD 开启鼠标指针和 Game+UI 输入；图标位置始终固定，不因主控切换重排。

### 3.4 球面多角色 Orbit Camera

M4 使用独立 `ABTSM4PartyCamera` 作为 PlayerController 的固定 ViewTarget，不再直接使用每只鸟自带的 SpringArm 相机。相机持久保存玩家选择的球面切向 OrbitForward、Elevation 和 Distance；角色朝向变化不会重置这些状态。

`ABTSBirdPartySettings > Camera > Orbit/Follow/Input/Obstruction` 可调整：

- `Orbit Distance CM`：默认 `850`，范围默认 `550–1300`。
- `Default Elevation Degrees`：默认 `60°`；运行时可在 `-85°` 仰视到 `+85°` 俯视之间连续调节。
- `Camera Look At Height CM`：注视点相对主控的径向高度。
- `Orbit Yaw/Pitch Degrees Per Input`：RMB/右摇杆灵敏度。
- `Orbit Zoom Step CM`：滚轮缩放步长。
- `Camera Switch Blend Seconds`：默认 `0.48 s`。
- `Orbit Pivot/Rotation Follow Speed`：普通跟随平滑。
- `Camera Probe Radius`、最小遮挡距离、收缩/恢复速度：软遮挡参数。
- `Camera Field Of View Degrees`：默认 `52°`。

默认 Elevation 为 `60°`，保持明显俯视。WASD 使用 OrbitForward/Right 作为球面切向移动基准，角色只转向移动意图，不带动相机。按住 RMB 捕获并隐藏光标以调节 Yaw/Elevation，松开后恢复 HUD 光标；滚轮缩放，R 主动回正。切换主控时 ViewTarget 和 Orbit 状态不变，只用球面 Pivot Blend 平滑迁移目标。Camera Sphere Sweep 显式忽略所有鸟，并以快速收缩、慢速恢复避免遮挡弹跳。完整契约见 [M4MultiCharacterOrbitCameraDesign.md](M4MultiCharacterOrbitCameraDesign.md)。

## 4. 跟随规则落地

初始队列为：

```text
Red -> Blue -> Yellow -> Black
```

每只鸟只读取其直接前导鸟的路径历史，并寻找距前导当前位置约 `QueueSpacingCM` 的后方样本。距离超过 `FollowStartDistanceCM` 时施加移动输入，进入 `FollowStopDistanceCM` 后停止输入。停止输入并不锁定位置，鸟仍由自己的阻力和径向悬挂自然停稳。

切换主控后，以新主控为根，在剩余成员中按当前球面距离逐个选择最近鸟，生成稳定、无环的新链。HUD 仍保持 Red、Blue、Yellow、Black 原位置。

局部分离只在小于 `SeparationDistanceCM` 时加入排斥意图，不使用全队质心 Cohesion。排斥输入按进入分离半径的深度做平方渐强，在边界处连续归零；它与跟随 Arrival 分别计算，非跟随状态不会因为触发分离而额外获得朝队列槽位的拉力。合力接近抵消时不施力也不更新朝向，避免阈值附近的拉斥切换放大为旋转振荡。严重脱队且连续三秒没有进展时，鸟回收到其前导的安全 breadcrumb，而不是主控脚下。

鸟群成员的 Capsule 会互相忽略 Pawn 碰撞，避免两个鸟体接触后被移动 Sweep 互相顶死；ForceSuspension 的手动查询必须读取 Capsule 实例的 Collision Response，不能使用会恢复默认响应的 `SweepSingleByProfile`。这不会忽略 WorldStatic 的 Cube、建筑或地形障碍。过近时仍会施加 Separation 意图，让鸟主动拉开，而不是依赖碰撞把它们分开。

## 5. 跳跃传播

鸟从 Grounded 进入 Airborne 时记录 Jump Event，后续更新该次跳跃达到的最大径向高度。直接后继鸟满足以下条件才消费事件：

1. 自己已接地；
2. 到达前导的起跳点 `JumpTriggerDistanceCM` 内；
3. 前导实际跳跃高度达到 `JumpHeightTriggerCM`；
4. 事件属于当前队列路径版本且尚未消费。

后继鸟起跳后会生成自己的 Jump Event，继续传播给下一只鸟。这样事件沿队列逐级传播，不会让三只跟随鸟同时复制主控 Space。

当前实现的空中路径修正由每只鸟原有 ForceSuspension 的 `AirControlScale` 和持续 breadcrumb 目标共同承担；不会把跟随鸟直接吸向空中前导鸟的三维位置。

## 6. HUD 规则

右侧 HUD 固定顺序：

```text
Red
Blue
Yellow
Black
```

- 当前主控头像外侧绘制金黄色高亮环。
- 非主控头像保持原位和普通暗色边框。
- 头像 Texture 为空时绘制配置的纯色圆形。
- 点击区域与圆形外接方框一致，便于操作。
- HUD 为常驻绘制，不依赖 UMG 蓝图或头像资产才能显示。

## 7. 日志验收

启动 M4 后应包含：

```text
[ABTS][M4][Party] Initialized=1 Members=4 Controlled=Red ...
[ABTS][M4] Party entry ready=1 StartCell=...
[ABTS][M4][Controller] Orbit camera, HUD mouse input and party switching ready.
[ABTS][M4][OrbitCamera] ... RollError=0.000
```

切换时：

```text
[ABTS][M4][Switch] Controlled=... Queue=...,...,...,...
```

跟随鸟消费跳跃事件时：

```text
[ABTS][M4][JumpFollow] Bird=... From=... Event=... Height=...
```

正常移动不应频繁出现 Recovery；出现时日志为：

```text
[ABTS][M4][Recovery] Bird=... returned to safe breadcrumb.
```

## 8. 编辑器验收清单

### 8.1 四鸟和外观

1. 启动后道路起点附近同时存在四只鸟。
2. 未配置模型时四鸟均显示当前默认球体。
3. 配置任意一只 `Bird Mesh` 后只有该身份使用新模型。
4. 未配置头像时 HUD 固定显示红、蓝、黄、黑四个圆。
5. 配置头像后对应图标显示纹理且仍为圆形。

### 8.2 主控切换

1. 初始 Red 高亮并由相机控制。
2. 连续按 Tab，按固定身份顺序循环，HUD 图标不移动。
3. 点击任意头像，立即切到对应鸟并高亮该固定位置。
4. 原主控变为跟随，只有新主控响应玩家 WASD/Space。
5. 连续快速切换不会生成两个主控或丢失 Pawn。
6. 切换期间相机沿空间连续轨迹移动并持续注视新主控，不瞬移到新鸟 SpringArm。

### 8.3 相机

1. 默认画面能从高处俯视主控和附近队列，日志 `DownAngle` 约为 `25°`。
2. 主控靠近或穿过其他鸟时，相机距离和位置不突变。
3. 主控沿球面移动时，相机的“上方”使用当地径向，不依赖世界 Z。
4. 主控切换后相机平滑移动到新目标，整个过程中不存在一帧切回角色自带相机。

### 8.4 地面跟随

1. 主控直线移动，后排依次启动，不聚集到主控脚下。
2. 主控停止，后排进入舒适区后依次停止。
3. 主控做 S 形路线，后排沿 breadcrumb 转弯，不明显切角。
4. 主控切换后，队伍平滑重排且所有鸟仍归属于队伍。
5. 横穿坡面时四只鸟均使用同一移动模型和径向悬挂。

### 8.5 跳跃跟随

1. 主控完成明显跳跃后，后排到达同一跳点再依次起跳。
2. 小于高度阈值的短暂腾空不会强制后排跳跃。
3. 后排处于空中时继续受径向重力，不直接飞向主控。
4. 落地后重新进入正常队列，不永久脱队。

## 9. 已知边界

- M4 不验收弹弓资格是否与真实弹弓 Actor 正确连接，只保证查询接口和四鸟默认资格存在。
- HUD 采用原生 Canvas 以保证无资产回退和快速验收；后续若需要动画、冷却和库存数字，可迁移为 UMG，但固定 BirdId 顺序不能改变。
- 当前局部避障依赖各鸟移动组件对非地形障碍的 Capsule Sweep，尚未实现复杂道路绕障规划。
- Recovery 暂时采用安全 breadcrumb 传送，没有落羽特效；特效属于后续表现验收。
