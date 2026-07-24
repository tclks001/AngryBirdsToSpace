# M7：模块化建筑基础材料与装置

> 状态：基础材料 C++ 已实现；本阶段只验收材料库、参数化生成和破坏响应，不生成完整建筑。
>
> 前置：[M6SlingshotLaunchAndImpactDesign.md](M6SlingshotLaunchAndImpactDesign.md)。后续建筑拼装器只调用本稿公开的添加接口，不重复实现材质、碰撞或爆炸。
>
> 本阶段的碰撞爽感、持续损伤和结构破坏升级建议见 [PhysicsImpactDestructionResearch.md](PhysicsImpactDestructionResearch.md)。
>
> 手工搭建平面碰撞关卡、摆放四材质砖与四档完整弹弓的独立测试入口见 [M71PlanarPhysicsTestStageDesign.md](M71PlanarPhysicsTestStageDesign.md)。

## 1. 阶段边界

M7 首阶段提供四种砖块、两种悬挂装置和两种机关：

| 模块 | 表现与生成 | 本阶段物理语义 |
| --- | --- | --- |
| 木砖 | 共享 Cube HISM；长宽高可调；木质材质 | 较易撞开、较易破坏 |
| 石砖 | 共享 Cube HISM；长宽高可调；石质材质 | 更重、更难破坏 |
| 铁砖 | 共享 Cube HISM；长宽高可调；铁质材质 | 阈值最高 |
| 玻璃砖 | 共享 Cube HISM；长宽高可调；玻璃材质 | 很脆，低速即可破坏 |
| 绳子 | 参数化圆柱代理；长度、半径可调 | 木质阈值；为后续约束预留端点语义 |
| 铁链 | 参数化圆柱代理；长度、半径可调 | 铁质阈值；为后续约束预留端点语义 |
| 炸药桶 | 参数化圆柱；长度、直径可调 | 破坏时近圈摧毁、外圈施加径向冲击 |
| 弹簧活塞 | 参数化圆柱；长度、直径可调 | 破坏时沿自身轴线两端近处摧毁、远处冲击 |

`CellTopo` 仍是建筑生成点、合法性和最终拼装归属的逻辑源。本阶段测试展台只用于验证材料，并不写入 CellTopo，也不代表建筑布局算法。

## 2. 模块结构

`AABTSM7BuildingMaterialSystem` 是关卡内唯一材料服务：

```text
AABTSM7BuildingMaterialSystem
├─ WoodBrickHISM  ┐
├─ StoneBrickHISM │ 同一个 SharedBrickMesh
├─ IronBrickHISM  │ 每种材质独立 HISM
├─ GlassBrickHISM ┘
├─ AddBrick(Spec, Transform)
├─ SpawnSuspension(Spec, Transform)
├─ SpawnDevice(Spec, Transform)
└─ 统一处理命中、提升、爆炸、冻结
```

砖块平时保留为静态 HISM。每个 `FABTSM7BrickSpec` 独立指定最终世界尺寸 `DimensionsCM`，系统把它换算为共享 100 cm Cube 的非均匀缩放。达到撞开阈值时，仅将命中的实例移出 HISM，提升为 `AABTSM7BuildingModule` Chaos Actor；达到破坏阈值则直接移除。

绳、链、炸药桶和弹簧活塞是参数化 `AABTSM7BuildingModule`。当前用引擎 Cylinder 作无资产回退，Cylinder 局部 Z 是长度/装置轴线。后续替换美术 Mesh 时必须保持该轴约定。

## 3. 材料与鸟种阈值

`MaterialProfiles` 暴露每种材料的 `KnockSpeedCMPerSec`、`BreakSpeedCMPerSec` 与无资产回退颜色。默认顺序为玻璃、木、石、铁逐渐变难破坏。鸟种再乘阈值倍率：黄鸟和黑鸟更容易破坏，蓝鸟对这些结构略弱，红鸟为基准。

所有速度判断都使用碰撞法向入射速度，不用总速度，擦边飞过不能按全速算作正面破坏。提升后的模块受与鸟一致的径向重力，可继续撞击其他模块形成链式反应；发射结束时停止模拟，保留最终姿态和 `QueryAndPhysics` 静态阻挡，后续发射仍能重新撞动或破坏。

## 4. 装置效果

### 4.1 炸药桶

炸药桶被破坏时产生两个同心范围：

- `BarrelDestroyRadiusCM` 内直接破坏；
- 从破坏半径到 `BarrelImpulseRadiusCM` 的对象不直接消失，而是从 HISM 提升/从静态重新激活，并受到随距离线性衰减的 `BarrelImpulseSpeedCMPerSec` 径向冲量。

### 4.2 弹簧活塞

活塞沿 Cylinder 局部 Z 向正负两个方向生效：

- 轴向距离不超过 `PistonDestroyLengthCM` 且到轴线距离不超过 `PistonEffectRadiusCM` 的模块直接破坏；
- 延伸至 `PistonImpulseLengthCM` 的两端走廊施加方向相反、随轴向距离衰减的冲量。

### 4.3 黑鸟

M6 黑鸟同步改为双范围：`BlackExplosionRadiusCM` 是近处破坏半径；`BlackExplosionImpulseRadiusCM` 是远处冲击半径；`BlackExplosionImpulseSpeedCMPerSec` 控制外圈最大冲量。它同时作用于树石 HISM/代理和 M7 基础材料。

## 5. 编辑器步骤

1. 创建 `BP_ABTSM7GameMode`，父类选择 `ABTSM7GameMode`，并在目标地图 World Settings 中设为 GameMode Override。
2. 创建 `BP_ABTSM7BuildingMaterialSystem`，父类选择 `ABTSM7BuildingMaterialSystem`。
3. 在其 Class Defaults 的 `ABTS | M7 | Assets` 配置木、石、铁、玻璃、绳、链、炸药桶、弹簧材质。未配置时使用引擎网格和颜色回退；玻璃的正式半透明效果仍应由你配置的玻璃材质提供。
4. 在 `BP_ABTSM7GameMode` 的 `Building Material System Class` 指向该 Blueprint。
5. 首次验收可勾选 `ABTS | M7 | Testing > Spawn Building Material Test Set`，运行时会在 TaskGraph 出生点前方生成四种砖、绳、链、炸药桶和活塞样品。
6. 需要测试 M6 弹射时，可同时保留 M6 的 Debug Slingshots 开关。

因为系统由 GameMode 运行时生成，不要直接在关卡里另放第二个 MaterialSystem，否则 M6 可能只找到其中一个。

## 6. 参数接口

| 接口/参数 | 单位 | 用途 |
| --- | ---: | --- |
| `FABTSM7BrickSpec.DimensionsCM` | cm | 单块砖最终长宽高 |
| `FABTSM7SuspensionSpec.LengthCM/RadiusCM` | cm | 绳或链长度、半径 |
| `FABTSM7DeviceSpec.LengthCM/DiameterCM` | cm | 桶或活塞长度、直径 |
| `MaterialProfiles` | cm/s | 各材质撞开/破坏速度 |
| `BarrelDestroyRadiusCM/ImpulseRadiusCM` | cm | 炸药桶近破坏/远冲击范围 |
| `PistonDestroyLengthCM/ImpulseLengthCM` | cm | 活塞两端近破坏/远冲击长度 |
| `PistonEffectRadiusCM` | cm | 活塞轴线周围有效半径 |

## 7. 验收清单

1. 启动日志包含 `[ABTS][M7] Entry ready=1` 与 `MaterialSystem ready`。
2. 四种砖均来自同一 Cube Mesh，但尺寸和材质可分别变化；行走时均能阻挡鸟。
3. 中速发射命中砖块时，仅命中实例从 HISM 变为动态刚体；高速命中时实例消失。
4. 绳、链长度可调；桶与活塞长度、直径可调，碰撞形状随缩放一致变化。
5. 炸药桶破坏后，近处模块消失，外圈模块被推开而不是一并消失。
6. 活塞破坏后只沿自身轴线两端产生效果，不形成普通球形爆炸。
7. 黑鸟近处破坏、远处冲击；树石与 M7 材料均响应。
8. 鸟回归后，未破坏模块保留最终姿态、阻挡行走，下一次发射还能再次撞动或破坏。

## 8. 当前不做

- 不生成完整建筑，不建立梁柱、承重图或结构坍塌判定；
- 绳与链当前是基础模块，不创建 Physics Constraint、悬垂分段或断裂约束；
- 不生成碎片、材料掉落、爆炸 Niagara、音效或镜头震动；
- 不做模块对象池和大规模同时激活预算；
- 不保存被破坏或移动后的建筑状态。

## 9. M7.2：材质碰撞、累计损伤与反馈

`MaterialProfiles` 现除原有撞开/破坏速度外，增加以下可编辑参数：

| 参数 | 作用 | 建议 |
| --- | --- | --- |
| `DynamicFriction` / `StaticFriction` | Chaos 滑动与静止摩擦 | 木较高、石最高、铁较低、玻璃较低 |
| `Restitution` | 模块刚体的法向反弹 | 木低，石/铁略高，玻璃中低 |
| `DensityGPerCubicCM` | Chaos 根据碰撞体体积计算质量 | 木约 0.62、石约 2.55、铁约 7.85 |
| `DamageAtBreakSpeed` | 一次正面命中达到 `BreakSpeed` 时写入的损伤 | 默认约 100 |
| `BreakDamage` | 累积损伤上限 | 默认 100；提高可形成更多次中速撞击 |
| `PushVelocityTransfer` | 由鸟/爆炸速度转化为目标速度的比例 | 木高、石中、铁低 |

响应遵循：接触（仅碰撞）→ 推动（HISM 提升为 Chaos 模块）→ 损伤累计 → 破坏。静态 HISM 在未达到推动门槛时也会按稳定世界变换键保存损伤；提升后损伤复制给 `AABTSM7BuildingModule`，冻结后再次命中仍可重新激活或继续损伤。树/石 HISM 使用 M6 的同类 Profile，和建筑砖一样拥有摩擦、弹性、密度和累计损伤。

当前无 Niagara/音效资产时，物理反馈由偏心/速度传递、可读的姿态变化和运行日志承担。后续材质可读取每实例或模块损伤值制作裂纹、闪白与局部破损，且不改变本期物理判定。

### 9.1 验收补充

1. 同速撞木、石、铁动态模块：木应最容易位移，石更稳，铁最难推动；它们停止后的滑动距离和反弹不同。
2. 对同一木砖连续中速命中：首次推动，后续命中累计损伤，达到 `BreakDamage` 后才移除。
3. 擦边命中不应与正面命中一样快速破坏；重点观察法向速度。
4. 被撞歪并冻结的树、石或模块仍应阻挡行走，并在下一次发射中重新成为可损伤、可推动目标。
5. 输出日志中应出现 `[ABTS][M6][HISMDamageBreak]`、`[ABTS][M6][ProxyDamaged]` 或 `[ABTS][M7][DamageBreak]`，用于记录损伤破坏。

### 9.2 排错

| 症状 | 根因 | 处理 |
| --- | --- | --- |
| 中速首击仍直接消失 | `DamageAtBreakSpeed` 或 `BreakDamage` 过低 | 提高 `BreakDamage`，或降低 `DamageAtBreakSpeed` |
| 物体被撞开但滑得太远 | `DynamicFriction` 太低 | 提高动态摩擦并检查网格碰撞形状 |
| 物体几乎无法推动 | 密度过高或 `PushVelocityTransfer` 太低 | 先调整传递比例，再调整密度 |
| 冻结物体无法再次响应 | 模块未保留 `QueryAndPhysics` 或没有走重新激活分支 | 检查 `Freeze` 和命中日志 |

## 10. 发射阶段重力与支撑失效

建筑块不能长期保持静态 HISM，否则下方砖块被撞飞或破坏后，上方实例仍不会参与 Chaos，表现为悬浮。现在发射阶段采用以下生命周期：

```text
Inactive
  -> 点击弹弓进入 Ready/Pulling（建筑仍保持静态）
  -> 松开弹弓，鸟进入 Flying
  -> M7 四类砖 HISM 全部提升为 AABTSM7BuildingModule
  -> 已冻结的砖、绳、链、炸药桶、活塞重新开启 Chaos
  -> 平面测试场使用 -PlanarUp 重力；球面地图使用指向球心的径向重力
  -> 下层支撑移动/破坏，上层物块自然坠落并产生链式碰撞
  -> BeginReturn 时停止模拟，保留最终姿态和 QueryAndPhysics 阻挡
```

动态化后的 `LaunchContactDamageGraceSeconds`（默认 `0.20 s`）属于接触稳定窗口：Chaos 在解决初始贴合或轻微穿插时产生的接触不累计损伤。窗口结束后只把两个物体沿碰撞法线相互靠近的速度作为有效撞击；解穿和回弹产生的分离速度不再通过绝对值伪装成撞击。该保护不影响鸟的直接命中，且正常的后续高速模块连锁碰撞仍参与累计损伤。

树、石和 M7.1 手摆 HISM 不会在全地图范围全部 Actor 化，而只在弹弓周围 `LaunchGravityActivationRadiusCM` 内提升为临时 Chaos 代理。这样发射区域内所有可交互物体都会受重力，同时避免一次发射唤醒整颗星球的森林。

`AABTSM6SlingshotSystem > ABTS | M6 | Impact` 新增：

- `LaunchGravityActivationRadiusCM`：弹弓周围环境 HISM 的重力激活半径，默认 `6000 cm`。
- `LaunchObjectGravityAccelerationCMPerSec2`：发射阶段物体重力加速度，默认 `980 cm/s²`。

验收日志：进入发射模式时应出现 `[ABTS][M7][LaunchGravity]` 和 `[ABTS][M6][LaunchGravity]`，其中分别记录建筑提升数量、环境 HISM 提升数量、平面/球面模式与重力值。
