# CuteBird：UE 5.1 迁移、四鸟外观与动画接入设计

> 状态：资产迁移验收已完成；首版共享 Presentation 动画框架、四鸟模型/材质/动画默认绑定已实现，并在 `integration/candidate-bird-presentation-v1-20260731` 通过 `AngryBirdsToSpaceEditor Win64 Development` 完整编译。本阶段按集成计划不执行 PIE，视觉验收留待最终集成。目标项目为 UE 5.8 的 `AngryBirdsToSpace`。
>
> 本文规定 CuteBird 资产如何安全迁移、四只鸟的外观映射、哪些动画进入本项目、每种动画服务哪个 Gameplay 状态，以及后续 Skeletal Mesh 表现层的接入边界。它不修改现有球面移动、Chaos、鸟群跟随、弹弓弹道或 CellTopo 逻辑。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M4 鸟群工程落地](M4BirdPartyImplementationDesign.md) · [Chaos 刚体移动](ChaosRigidBodyMovementDesign.md) · [Low Poly/AI 资产工作流](LowPolyAssetProductionAndAIReportWorkflow.md)

## 1. 结论与边界

CuteBird 是 M4 之后四鸟的**纯表现资产包**。角色的真实位置、碰撞、径向重力、队列跟随、主控切换和弹弓资格仍由 `AABTSM25BirdCharacter`、其 Capsule/Chaos 组件和 `AABTSBirdParty` 决定；CuteBird 网格永远不作为角色的物理根，也不参与碰撞。

`BirdVisual` 已升级为 `USkeletalMeshComponent`。首版由 C++ 构造函数中的默认资源路径直接绑定模型、材质和动画；不创建 Animation Blueprint、不要求在编辑器为四只鸟逐项配置。

1. **资产迁移层**：已验收通过。
2. **表现接入层（首版已实现）**：由代码端默认值接入 Skeletal Mesh 与动画；不改角色移动/碰撞根组件。

不要将 `BP_Cute_Bird_*` 设为 GameMode 默认 Pawn，也不要直接 Possess 这些示例 Blueprint。它们只用于迁移外观依赖并作为颜色配置参考。

## 2. 已确定的四鸟映射

| ABTS 身份 | 中文名 | CuteBird Blueprint | 角色职责 | 建议外观定位 |
| --- | --- | --- | --- | --- |
| Red | 绯翼 | `BP_Cute_Bird_12` | 默认主控、通用撞击、制作/建造执行者 | 红色主控鸟。 |
| Blue | 青翎 | `BP_Cute_Bird_3` | 树枝近射、侦察、拍照标记 | 蓝色轻快侦察鸟。 |
| Yellow | 棱喙 | `BP_Cute_Bird_10` | 高速穿透、木结构伤害加成 | 黄色高速鸟。 |
| Black | 玄爪 | `BP_Cute_Bird_16` | 爆破冲击、范围推力 | 黑色重击/爆破鸟。 |

这些 Blueprint 编号是**外观预设编号**，不是 Gameplay 身份编号。运行时鸟身份继续使用 `EABTSBirdId::Red/Blue/Yellow/Black`，不能把 `12/3/10/16` 写进任何玩法判定、存档、TaskGraph 或弹弓能力逻辑。

## 3. 源资产清单与迁移选择

源根目录：`C:\workspace\UE51Test\Content\CuteBird`。

### 3.1 必迁移：四鸟外观闭包

在 UE 5.1 的 Content Browser 中选择以下资产，并使用一次性 `Asset Actions → Migrate`。迁移报告是最终依赖清单的唯一权威；不要在 Windows 文件管理器中复制 `.uasset`。

| 源目录 | 选择的资产 | 原因 |
| --- | --- | --- |
| `Blueprints` | `BP_Cute_Bird_12`、`BP_Cute_Bird_3`、`BP_Cute_Bird_10`、`BP_Cute_Bird_16` | 作为四种颜色/脸部外观预设，并让 UE 自动收集它们的材质与纹理依赖。 |
| `Meshes` | `SM_Cute_Bird` | 共享鸟网格。名称虽以 `SM_` 开头，但必须在 UE 5.1 中确认其 Asset Type；只有它是 Skeletal Mesh 时才能播放动画。不要根据命名猜测类型。 |
| `Meshes` | `SK_Cute_Bird_Skeleton` | 共享骨架/动画兼容关系。 |
| `Meshes` | `Cute_Bird_PhysicsAsset` | 迁移骨架网格可能引用它；目标项目中仅供预览，不作为 ABTS 角色碰撞。 |
| `Materials/CuteBirdColor_Materials`、`Materials/Face_Materials` | **不手选全部**；由四个 Blueprint 的迁移依赖报告自动带入实际使用的 `M_CuteBird_*`、`M_Dino_face_*`。 | 避免无意义迁移 18 套颜色和全部脸部素材。 |
| `Textures/CuteBirdColor_Textures`、`Textures/Face_Textures` | **不手选全部**；同样由依赖报告自动带入。 | 四种鸟的颜色和脸部材质需要的纹理会自动被选中。 |

### 3.2 必迁移：共享 Gameplay 动画

以下动画全部使用同一 Skeleton，迁移一次即可给四只鸟共用：

| 资产 | 是否首版必需 | ABTS 用途 | 接入方式 |
| --- | --- | --- | --- |
| `Cutebird_IdleA` | 待补迁 | 常驻静止、队列停止、默认待机 | 当前目标目录未验收此资产；补迁前按第 6.3 节的静止回退策略处理。 |
| `Cutebird_IdleB` | 待补迁（可选） | 长时间未输入时的第二待机变化 | 当前目标目录未验收此资产；不阻塞首版。 |
| `Cutebird_Move` | 是 | 地面行走、队列跟随、青翎回归队伍 | Grounded Move 循环，按切向速度混合。 |
| `Cutebird_Jump` | 是 | 玩家空格跳跃、跟随鸟按需跳跃 | Jump 起跳 One-shot；结束后转 Fly。 |
| `Cutebird_Fly` | 是 | 普通空中、弹弓发射后的飞行、自由落体视觉 | Airborne/Launched 循环；不施加 Root Motion。 |
| `Cutebird_Attack` | 推荐 | 撞击建筑、爆破触发前后的短促动作候选 | 先在预览中确认语义；可作为 Impact One-shot，不用于实际物理驱动。 |
| `Cutebird_Damage` | 推荐 | 受到强碰撞、落水/失败前的短反馈 | 纯表现蒙太奇；不会减少鸟生命或终止 Gameplay。 |

### 3.3 第二优先级：只在对应玩法到来时迁移

| 动画 | 未来用途 | 当前不应强行使用的原因 |
| --- | --- | --- |
| `Cutebird_Fly_L`、`Cutebird_Fly_R` | 弹弓高速飞行中的左右倾斜、卫星轨迹转弯 | 需要先有稳定的速度/角速度视觉参数；首版 `Fly` 已足够。 |
| `Cutebird_Move_L`、`Cutebird_Move_R` | 未来真正的侧移或队列转弯 Lean | 当前移动会让鸟转向移动意图，通常没有持续侧移。 |
| `Cutebird_Happy`、`Cutebird_Yes` | 制作成功、材料回收、任务完成、终局前庆祝 | 适合 M5/M8/M11 的情绪反馈。 |
| `Cutebird_Hello` | 开场/首次集合演出 | 不影响核心闭环。 |
| `Cutebird_No` | 配方不足、不能建造、错误弹弓资格提示 | 应只作为短 UI/角色反馈。 |
| `Cutebird_Eat` | 自动回收材料的近景表演 | 资源库存自动写入，不应逐个拾取都播放。 |
| `Cutebird_Sick` | 落水、无效区域或卫星超时的反馈 | 鸟会回收而非永久受伤。 |
| `Cutebird_DieA`、`Cutebird_DieB` | 原包通用动画 | **不迁移首版**。ABTS 鸟不会死亡；失败后回收至安全位置。 |

若要一次性减少后续迁移操作，可以把第二优先级也一起迁移；但 `DieA/DieB` 仍建议不迁移，避免误用并降低审核资产范围。

### 3.4 明确不迁移

```text
/Game/CuteBird/Maps/CuteBird_Map
/Game/CuteBird/Maps/CuteBird_Map_BuiltData
/Game/CuteBird/Materials/M_Floor_Pink
/Game/CuteBird/Textures/T_floor_pink
未选中的 BP_Cute_Bird_0...17
未被四个选择 Blueprint 引用的颜色/脸部材质与纹理
```

示例地图、地板和其 BuiltData 对 ABTS 没有价值；迁移它们只会污染目标项目并增加打开地图时的版本/依赖风险。

## 4. UE 5.1 → UE 5.8 迁移操作：逐步执行

### 4.1 源项目准备

1. 关闭 `AngryBirdsToSpace` 的 UE 5.8 Editor，避免目标 Content 在迁移中被占用或 Asset Registry 不刷新。
2. 打开 UE 5.1 项目：`C:\workspace\UE51Test\UE51Test.uproject`。
3. 在 Content Browser 打开 `/Game/CuteBird`，不要打开示例 `CuteBird_Map`。
4. 逐个打开 `BP_Cute_Bird_12`、`3`、`10`、`16`：选中其 Mesh/Skeletal Mesh 组件，记录 Details 中的 Mesh、Material Slot、`M_CuteBird_*` 和 `M_Dino_face_*` 实际引用。将四张 Details 截图存到：

```text
C:\workspace\AngryBirdsToSpace\SourceArt\Purchased\CuteBird\ReferenceScreenshots\
```

这一步是颜色/脸部映射的人工证据；不要假设 Blueprint 编号与材质编号必然相同。

5. 右键 `/Game/CuteBird` 文件夹，执行 `Fix Up Redirectors in Folder`。只在 UE 5.1 源项目中执行，不对目标项目的其他目录做批量修复。

### 4.2 第一批：外观依赖迁移

1. 按住 `Ctrl`，在 Content Browser 中选中：

```text
BP_Cute_Bird_12
BP_Cute_Bird_3
BP_Cute_Bird_10
BP_Cute_Bird_16
SM_Cute_Bird
SK_Cute_Bird_Skeleton
Cute_Bird_PhysicsAsset
```

2. 右键任意选中资产：

```text
Asset Actions → Migrate
```

3. 阅读 `Asset Report`：

   - 应包含选中的 Blueprint、共享网格、Skeleton、PhysicsAsset；
   - 应包含四种外观实际引用的颜色/脸部 Material 与 Texture；
   - 不应包含 `CuteBird_Map`、`CuteBird_Map_BuiltData`、`M_Floor_Pink` 或 `T_floor_pink`。

4. 若报告包含地图或地板，点击 `Cancel`，改为只从四个 Blueprint/网格开始选择；不要用文件复制绕开依赖报告。
5. 点击 `OK` 后，在文件夹选择器中选择**目标 Content 根目录**：

```text
C:\workspace\AngryBirdsToSpace\Content
```

不要选择 `Content\CuteBird`。迁移会保留源 Package Path，最终应形成：

```text
C:\workspace\AngryBirdsToSpace\Content\CuteBird\...
```

### 4.3 第二批：首版动画迁移

1. 在 `/Game/CuteBird/Animations` 选中：

```text
Cutebird_IdleA
Cutebird_IdleB
Cutebird_Move
Cutebird_Jump
Cutebird_Fly
Cutebird_Attack
Cutebird_Damage
```

2. 重复 `Asset Actions → Migrate`，目标仍为：

```text
C:\workspace\AngryBirdsToSpace\Content
```

3. `Asset Report` 中 Skeleton 应是已有依赖；如果提示同名覆盖，说明第一批已迁移成功，允许使用同一依赖，不要手工复制同名 `.uasset`。

### 4.4 UE 5.8 首次打开与转换

1. 用 UE 5.8 打开 `C:\workspace\AngryBirdsToSpace\AngryBirdsToSpace.uproject`。
2. 等待资源注册和版本转换完成；首次加载时不要中途关闭 Editor。
3. 在 Content Browser 搜索：

```text
/Game/CuteBird
```

4. 对四个 Blueprint、共享 Mesh、Skeleton、Physics Asset 和七条首版动画依次双击打开、保存。
5. 只在 `/Game/CuteBird` 文件夹执行 `Fix Up Redirectors in Folder`。
6. 在 fresh Editor 重开项目，确认没有 `Failed to load`、材质丢失、Skeleton 不兼容或 `Could not find template object` 警告。

### 4.5 迁移层验收

| 检查 | 通过标准 |
| --- | --- |
| 四种外观 | 四个源 Blueprint 均可打开，Mesh 与颜色/脸部材质显示正确。 |
| 动画 | 七条首版动画均可在同一 Skeleton 上预览，无骨骼不兼容提示。 |
| 示例内容 | 目标项目中没有 CuteBird 示例地图和粉色地板。 |
| 目录 | 资源仍位于 `/Game/CuteBird/...`，初次迁移后不重命名、不用资源管理器移动。 |
| 报错 | fresh Editor Output Log 无 `Failed to load`、`Could not find template object`、缺失材质/纹理错误。 |

## 5. 动画如何服务 ABTS Gameplay

### 5.1 统一代码动画表，而不是每只鸟一套 Animation Blueprint

四只鸟共用 `SK_Cute_Bird_Skeleton`，因此首版只维护一份 C++ 动画表和一套状态选择规则；不创建 `ABP_ABTS_CuteBird`。颜色、脸部材质和 BirdId 由 `SetBirdIdentity` 的代码预设决定；动画选择不得根据 `BP_Cute_Bird_12/3/10/16` 复制四份逻辑。

建议的代码状态选择：

```text
Grounded
  ├─ IdleA <-> Move
  └─ Jump (One-shot)
        └─ AirborneFly

LaunchedFly        # 弹弓发射时覆盖普通 AirborneFly
ImpactFeedback     # 短 One-shot，结束回 Airborne 或 Grounded
EmoteSlot          # Happy/Yes/No 等可选蒙太奇，不改变移动状态
```

| ABTS 运行时条件 | 动画 | 规则 |
| --- | --- | --- |
| 接地且切向速度低 | `IdleA` | 默认循环；可每 8–20 秒低概率切一次 `IdleB`。 |
| 接地且有移动意图/切向速度 | `Move` | 按归一化速度调播放速率；角色旋转由现有球面/Chaos 视觉帧决定。 |
| Grounded → Airborne | `Jump` | 只播放一次；动画结束或离地时间超过短阈值后进入 `Fly`。 |
| 普通空中/落下 | `Fly` | 循环；不由动画位移推动角色。 |
| 弹弓 `Pull/Hold` | `IdleA` 或专用待机姿势 | 小鸟挂在弹丸袋 Socket，位置由弹弓表现逻辑决定。首版不强制购买包外的拉弓动画。 |
| 弹弓 `Release/Launched` | `Fly` | 以物理速度、球面重力、碰撞为真相；`Fly` 只表现飞行。未来可混合 `Fly_L/R` 表现转弯。 |
| 建筑撞击/黑鸟爆破 | `Attack` 或 `Damage` 候选 | 必须先手工预览语义；若画面不合适，先只做粒子/镜头反馈，不强行播放。 |
| 制作成功/回收成功 | `Happy` 或 `Yes` | 可选短 Emote，不打断玩家移动。 |
| 配方不足/不可建造 | `No` | 可选表现，UI 提示仍是主反馈。 |
| 落水/回收/卫星超时 | `Sick` 或 `Damage` | 只在回收前短暂播放；不得播放死亡动画。 |

### 5.2 必须关闭 Root Motion

ABTS 的位置由 C++ Force/Chaos/球面径向约束、弹弓初速度和碰撞决定。CuteBird 动画只能改变骨骼姿态，**不能**驱动 Actor 位移。

在每条首版动画的 Asset Details 中确认：

```text
EnableRootMotion = false
Root Motion Root Lock = Ref Pose 或默认非驱动设置
```

如果源动画带有明显前移，保持原动画作为参考，创建 In-place 副本或在 Animation Blueprint 中只使用其姿态；禁止启用 Root Motion 来“修正”角色移动，否则会和 Chaos/弹弓物理双重驱动并产生漂移。

### 5.3 动画事件与 Gameplay 事件的单向关系

```text
C++ / Physics / Party 的真实状态
        ↓ 只写只读表现参数
AnimationSingleNode、材质参数
        ↓
Skeletal Mesh 可见表现
```

动画 Notify 可以播放脚步尘土、轻微羽毛粒子或声音，但不得改变：速度、跳跃高度、是否接地、发射初速度、建筑伤害、资源库存、队列位置或 CellTopo 状态。

## 6. 当前 C++ 与未来 Skeletal 表现层的接入契约

### 6.1 当前状态

`AABTSM1BirdCharacter` 创建同名 `USkeletalMeshComponent BirdVisual`，并在 CDO 中硬引用共享 `SM_Cute_Bird`。`AABTSM25BirdCharacter` 保留四鸟两槽材质默认绑定，并在 `BeginPlay` 创建不参与 Blueprint 序列化的运行时 `UABTSBirdAnimationPresentationComponent`。该组件在自身 CDO 中硬引用 IdleA、Move、Jump、Fly、Attack、Damage，只消费角色每帧提供的接地、切向速度与弹弓飞行快照。M4 的旧 `BirdMesh` Settings 字段不再参与运行时外观选择。

数据方向固定为：

```text
Gameplay / Force / Chaos（真相源）
    -> FABTSBirdAnimationSnapshot（只读快照）
    -> UABTSBirdAnimationPresentationComponent
    -> BirdVisual 的 AnimationSingleNode 姿态
```

`RequestBirdPresentationAction(Impact/Damage)` 只请求一次性姿态覆盖；组件没有速度、位置、碰撞、伤害、库存或鸟群状态的写入接口。运行时创建组件而不新增序列化 Native Default Subobject，是为了避免三个并行工作树中的派生 Bird Blueprint 发生无意义的二进制迁移。

### 6.2 首版已落实的范围

首版 `M4.x CuteBird Skeletal Presentation` 已按以下边界实现：

1. 保留 Capsule、`ChaosPhysicsSphere`、Force/Chaos 移动组件、球面朝向计算和 Party 碰撞隔离原样。
2. 以同名 Native Default Subobject 将 `BirdVisual` 的类型替换为 `USkeletalMeshComponent`，仍挂接到现有 Capsule。它固定 `Collision Enabled = No Collision`、`Generate Overlap Events = false`、`Simulate Physics = false`，不得创建 Physics Body 或参与查询/物理碰撞。Chaos 模式下模型脚底枢轴由代码锚定到碰撞球沿当前 Up 的支撑点；`Bird Visual Relative Location` 只是在该支撑点之上的表现微调，不改变刚体中心或半径。
3. 首版采用 `AnimationSingleNode`（`UAnimSingleNodeInstance`）而非 `Animation Blueprint`：C++ 根据现有移动/弹弓状态切换 Sequence，并控制循环与单次播放。这样模型、动画、材质均为 CDO 默认值，不需要编辑器中的 AnimGraph、State Machine 或每鸟 Blueprint 配置。
4. 在 `ABTSBirdPartySettings`/`FABTSBirdPresentationConfig` 中移除本阶段对 `BirdMesh` 的运行时外观决定权：`AABTSM25BirdCharacter::SetBirdIdentity` 成为唯一的 BirdId→材质预设选择点。四鸟共享同一个 Skeletal Mesh 与动画表；仅根据 `EABTSBirdId` 应用代码内建的 Material Slot 覆盖。保留静态球体作为资源加载失败的受控回退，不把关卡 Settings 或派生 Blueprint 作为正常路径。
5. 迁移后的 `BP_Cute_Bird_*` 只作为“该 BirdId 采用哪套 Material/Texture”的编辑器参考，不嵌套为 Character 的子 Pawn，也不继承其事件图。
6. 不要求创建或编辑 ABTS Bird Blueprint；直接在 fresh Editor/Standalone 用现有 M4 地图验证。由于 `BirdVisual` 的原生子对象类型发生变化，必须保持其名称不变，并把既有蓝图序列化兼容性作为专项验收项。

### 6.3 代码端默认资源表与切换规则

实施时，以下资源必须使用 C++ 构造函数中的 `ConstructorHelpers::FObjectFinder`（或等效的仅 CDO 加载方式）绑定；不得在 `BeginPlay` 用字符串反复同步加载。所有路径以已迁入的 `/Game/CuteBird/...` Package Path 为准：

| 用途 | 默认资源路径 | 代码端规则 |
| --- | --- | --- |
| 共享模型 | `/Game/CuteBird/Meshes/SM_Cute_Bird.SM_Cute_Bird` | 绑定给唯一的 `BirdVisual` Skeletal Mesh Component；四鸟共用。实施前以资源类型确认该 `SM_` 命名资产确为 Skeletal Mesh，不能仅凭前缀判断。 |
| 行走 | `/Game/CuteBird/Animations/Cutebird_Move.Cutebird_Move` | 接地且切向速度超过阈值时循环。 |
| 起跳 | `/Game/CuteBird/Animations/Cutebird_Jump.Cutebird_Jump` | 接地→离地沿只播放一次；结束或离地超时立即转飞行。 |
| 飞行 | `/Game/CuteBird/Animations/Cutebird_Fly.Cutebird_Fly` | 非接地、弹弓发射或 Chaos 弹道中循环；不启用 Root Motion。 |
| 撞击候选 | `/Game/CuteBird/Animations/Cutebird_Attack.Cutebird_Attack` | 仅在人工预览确认语义后，作为可选短暂表现，不驱动伤害或速度。 |
| 受击/回收候选 | `/Game/CuteBird/Animations/Cutebird_Damage.Cutebird_Damage` | 仅作可选短暂表现，不改变回收、生命或物理状态。 |

当前 `Content/CuteBird/Animations` 已验收到 `IdleA`、`IdleB`、`Move`、`Jump`、`Fly`、`Attack`、`Damage`、`No`、`Yes`、`DieA`、`DieB`。首版组件已硬引用 `IdleA`、`Move`、`Jump`、`Fly`、`Attack`、`Damage`；其中 Attack/Damage 只通过显式表现请求播放，尚不自动接入 Gameplay 命中链路。`IdleB`、`No`、`Yes` 保留为后续演出扩展候选，`DieA/DieB` 不参与 ABTS 表现状态。

四鸟材质预设同样为代码常量：Red→`BP_Cute_Bird_12`、Blue→`BP_Cute_Bird_3`、Yellow→`BP_Cute_Bird_10`、Black→`BP_Cute_Bird_16`。每个预设应列出 Skeletal Mesh 的所有 Material Slot 及其对应的已迁入 `M_CuteBird_*`/`M_Dino_face_*` 路径；这些精确 Slot 对照必须从已验收 Blueprint 的 Mesh Component 读取后写入代码常量，不能根据资产编号猜测脸部材质。材质解析失败时记录 BirdId、Slot 和路径，并回退为共享模型的原始材质；不得回退到碰撞组件。

### 6.4 每只鸟的外观配置目标

| BirdId | Skeleton Mesh | 动画方式 | 材质来源 | 回退 |
| --- | --- | --- | --- | --- |
| Red | CuteBird 共享 Mesh | C++ `AnimationSingleNode` | 代码常量：`BP_Cute_Bird_12` 已验收的 Slot 对照 | 资源加载失败时红色球形。 |
| Blue | 同上 | 同上 | 代码常量：`BP_Cute_Bird_3` 已验收的 Slot 对照 | 资源加载失败时蓝色球形。 |
| Yellow | 同上 | 同上 | 代码常量：`BP_Cute_Bird_10` 已验收的 Slot 对照 | 资源加载失败时黄色球形。 |
| Black | 同上 | 同上 | 代码常量：`BP_Cute_Bird_16` 已验收的 Slot 对照 | 资源加载失败时黑色球形。 |

## 7. 编辑器验收顺序

### 7.1 已完成的资产迁移验收

1. 已在 UE 5.8 中打开四个 `BP_Cute_Bird_*`，确认颜色和脸部对应身份。
2. 打开共享 Mesh，确认引用的 Skeleton 与 `SK_Cute_Bird_Skeleton` 一致。
3. 已对实际迁入的 `IdleA/IdleB`、`Move`、`Jump`、`Fly` 等动画完成预览。
4. 打开 `Attack`、`Damage`，决定它们是否语义适合撞击反馈；不适合则仅记录为未采用候选。
5. 保存全部已经转换的资产，关闭并 fresh 重开 UE 5.8。

### 7.2 最终集成时的 Skeletal 表现验收

1. 在 `L_ABTS_M4` 中四鸟都显示正确颜色，不再显示占位球。
2. 不按键时遵循第 6.3 节已确定的 Idle 回退策略；行走时播放 Move；跳跃时播放 Jump 后转 Fly；落地后回 Idle/Move。
3. Tab/HUD 切换主控不会重置动画、丢材质、改变相机 Orbit 或破坏队列逻辑。
4. 球面跨越极点、斜坡、平面 M7.1/Chaos 测试台时，骨骼视觉的本地 Up 仍服从角色现有径向/平面视觉框架。
5. 飞行/发射中动画不改变物理轨迹；关闭动画或触发资源加载回退球体不会改变弹道、碰撞或伤害结果。
6. 不编辑关卡 Settings、Character 派生 Blueprint 或 Animation Blueprint 的前提下，fresh Editor 与 Standalone 中四鸟均能由代码加载正确模型、身份材质和动画；输出日志中无默认资源路径加载失败。
7. Chaos 平面和球面接地时，模型脚底贴合碰撞球支撑点；修改视觉 Scale 不重新引入固定离地间隙，修改 `Bird Visual Relative Location` 只调整模型表现位置。

## 8. 排错表

| 症状 | 根因 | 修复 |
| --- | --- | --- |
| UE 5.8 中材质、脸部或纹理丢失 | 直接复制 `.uasset`、未通过 Migrate 收集依赖，或移动了 Package Path | 回 UE 5.1 从四个 BP 重新 Migrate；以 Asset Report 为准；初次迁移不重命名目录。 |
| Blueprint 可打开但颜色不对 | 只迁移 Mesh，没有迁移 BP 依赖的颜色/脸部 Material | 迁移对应 BP；在源 BP Details 记录实际 Material Slot。 |
| 动画提示 Skeleton 不兼容 | 使用了错误 Mesh/Skeleton，或源资源未完整迁移 | 从 `SK_Cute_Bird_Skeleton` 重新确认 Animation 的 Skeleton；迁移 Skeleton 与动画的完整闭包。 |
| 鸟走路时位置抖动、速度翻倍或脱离球面 | 动画启用了 Root Motion，与 C++/Chaos/球面移动同时驱动 | 关闭 Root Motion；所有位置只由现有移动组件和物理决定。 |
| 鸟模型相互卡住 | Skeletal Mesh 启用了 Pawn/Physics 碰撞，覆盖了已验证的 Capsule 碰撞隔离 | `BirdSkeletalVisual` 固定 No Collision、Simulate Physics=false；保留 Capsule/ChaosPhysicsSphere 为唯一物理体。 |
| 模型脚底固定悬空，且间距不随 Scale 改变 | 脚底枢轴被放在 Chaos 碰撞球心，球心始终高出接触面一个球半径 | 以 `SphereCenter - Up * SphereRadius` 计算视觉支撑点，再叠加纯表现 Location；不要移动或缩放碰撞体来迁就模型。 |
| 默认路径加载失败或模型回退为球体 | C++ 常量路径、资产类型或 Cook 引用错误 | 核对第 6.3 节 Package Path 与资源类型；使用 CDO 硬引用确保 Cook 收集，并记录 BirdId/资源路径。 |
| 静止时无动画或报找不到 Idle | CDO 默认资源路径、Cook 引用或 Skeleton 兼容关系错误 | 核对 `Cutebird_IdleA` 的 Package Path 与 Skeleton；确认 CDO 硬引用存在且输出日志无加载失败。 |
| 切换鸟后相机或移动逻辑异常 | 将示例 `BP_Cute_Bird_*` 当作 Pawn/Controller 使用 | 只读取其 Mesh/Material 外观；继续 Possess ABTS C++ Character。 |
| 旧 M4 地图/Blueprint 打开报模板错误 | 改名/删除 Native 表现组件，旧 Blueprint 保留序列化引用 | 保留兼容回退，创建全新派生 Blueprint 验证后再逐项迁移地图。 |

## 9. 后续代码接入任务边界

1. 以第 6.3 节的路径表和材质 Slot 对照为唯一默认资源来源，实现 `BirdVisual` 的 Skeletal 表现与 `AnimationSingleNode` 状态切换。
2. 保持 Capsule/`ChaosPhysicsSphere`、移动组件、球面/平面视觉框架、鸟群跟随和弹弓物理不变；模型只做表演，永不参与碰撞。
3. 首版只使用 `IdleA`、`Move`、`Jump`、`Fly`；`IdleB`、Attack/Damage/No/Yes 与死亡动画暂不接入状态切换。
4. 不创建 Anim Blueprint，不在编辑器为四鸟逐项挂模型/动画，也不把 `BP_Cute_Bird_*` 用作 Pawn。
