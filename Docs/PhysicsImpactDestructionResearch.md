# 物理碰撞破坏爽感调研与 M6/M7 指导

> 用途：为 AngryBirdsToSpace 的弹射、碰撞、链式破坏和建筑材料参数提供设计依据。
>
> 结论先行：当前“高速直接碎、低速完全不动”的问题，不应通过单纯调整两个速度阈值解决。应把碰撞结果拆成四个连续层级：**接触/偏转、推动、损伤累积、结构破坏**。速度只影响冲量和损伤增长率，不能直接决定唯一结果。

## 1. 调研范围与可核查资料

本稿参考了公开产品页、技术文档和开发者文章。产品宣传页用于确认游戏的核心交互承诺，技术资料用于提取可迁移的实现原则。

| 案例 | 可核查来源 | 对本项目的启示 |
| --- | --- | --- |
| 《Angry Birds》系列 | [Rovio Angry Birds 官网](https://www.angrybirds.com/)、[Game Developer：Rovio Angry Birds postmortem](https://www.gamedeveloper.com/design/postmortem-rovio-s-i-angry-birds-i-) | 弹道是可读的；不同鸟和材料有明确克制关系；玩家爽感来自一次命中后连续倒塌，而不是每个物体都立即碎掉。 |
| 《Boom Blox》 | [EA 产品资料](https://www.ea.com/games/boom-blox)、[Wikipedia 条目](https://en.wikipedia.org/wiki/Boom_Blox) | 物体会被推动、倾倒和连锁触发；“没有完全破坏也能产生进展”非常重要。 |
| 《Red Faction: Guerrilla》 | [Game Developer 技术栏目](https://www.gamedeveloper.com/programming/the-technology-of-i-red-faction-guerrilla-i-)、[GDC Vault 检索](https://www.gdcvault.com/search.php#&keyword=Red+Faction+Guerrilla) | 破坏不是单个物体的二元开关，还与承重、连接和局部损伤有关；局部断裂会诱发整体坍塌。 |
| 《Teardown》 | [Steam 产品页](https://store.steampowered.com/app/1167630/Teardown/)、[80.lv 环境破坏拆解](https://80.lv/articles/teardown-breaking-down-the-fully-destructible-environments/)、[Game Developer 技术栏目](https://www.gamedeveloper.com/programming/how-teardown-made-a-fully-destructible-environment) | 远处物体不必全部即时碎裂；局部切除、碎片/大块转刚体和可利用的缺口共同形成反馈。 |
| 《Besiege》 | [Steam 产品页](https://store.steampowered.com/app/346010/Besiege/)、[Besiege 官网](https://besiege.en.softonic.com/) | 连接件、机械部件和整体姿态共同决定破坏；先断连接再让结构失稳，比所有部件同时消失更有过程感。 |
| UE Chaos Destruction | [Chaos Destruction](https://dev.epicgames.com/documentation/en-us/unreal-engine/chaos-destruction)、[Fields Overview](https://dev.epicgames.com/documentation/en-us/unreal-engine/fields-overview) | Chaos 适合做刚体、Geometry Collection、Field 和应力/损伤传播，但必须由 gameplay 层控制何时激活和何时破坏。 |
| UE 官方物理 | [Physics-Based Character Movement](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-based-character-movement)、[Physics Materials](https://dev.epicgames.com/documentation/en-us/unreal-engine/physical-materials-reference) | 碰撞响应、摩擦、恢复系数和冲量应分开调；不能用一个 Break 阈值同时承担所有手感。 |

注：部分厂商内部实现没有完整公开；本稿只把公开可验证的产品目标和通用技术原则迁移到本项目，不声称复刻其私有源码。

## 2. 为什么当前方案会产生“悖论”

当前 M6/M7 的核心判断近似为：

```text
NormalSpeed < KnockThreshold  -> 不处理
KnockThreshold <= NormalSpeed < BreakThreshold -> 推动/转动态代理
NormalSpeed >= BreakThreshold -> 直接破坏
```

这个模型有三个问题：

1. 速度同时承担“是否接触有效”“推动多少”“损伤多少”“是否碎裂”四个职责，阈值稍微改变就会在“完全不动”和“直接消失”之间跳变。
2. 建筑没有记忆。一次 500 cm/s 的轻微命中和十次 500 cm/s 的连续命中在系统中几乎等价，玩家无法通过连续小撞击制造策略性破坏。
3. 物体没有结构状态。被撞歪的梁、已经松动的玻璃砖、受力很大的连接件和完整物体使用同一套阈值，导致链式反应缺少中间过程。

真正需要的是连续函数：

```text
ImpactEnergy = 0.5 * EffectiveMass * NormalSpeed²
Impulse = NormalDirection * EffectiveMass * NormalSpeed * ContactTransfer
DamageGain = ImpactEnergy * MaterialVulnerability * AngleFactor * ExposureFactor
```

其中 `DamageGain` 写入物体的持久损伤；只有损伤达到 `BreakDamage`，或结构约束失效，才执行破坏。

## 3. 各游戏值得迁移的共同规律

### 3.1 《Angry Birds》：可读的材料克制与“先动后碎”

原作的爽感不是每一击都追求最大破坏，而是让玩家看到：

- 鸟先压弯、推倒、滚动或反弹；
- 材料在达到破坏条件后才碎；
- 第一处破坏改变后续落点和支撑关系；
- 木、石、玻璃之间存在明确的鸟种克制。

对本项目的直接建议：M6 的鸟撞砖时，哪怕没有达到破坏条件，也至少应产生可见位移/倾角/轻微弹性反馈，除非是极低速擦碰。

### 3.2 《Boom Blox》：推动本身就是奖励

这类积木游戏把“倒下但没碎”的状态当作成功反馈。物体先被推离原位，接着碰撞邻居，邻居再把动量传下去；玩家不需要每次都获得破坏数字。

对本项目的直接建议：砖块应允许三个结果：

```text
轻触 -> 位移/旋转很小，不激活级联
有效撞击 -> 推动或倾倒，产生后续接触
高损伤/结构失稳 -> 破坏
```

### 3.3 《Red Faction: Guerrilla》：承重和损伤记忆

结构破坏最有价值的地方不是“单块砖被打掉”，而是失去关键支撑后整体响应。这个原则适合 M7 后续建筑拼装：连接图和承重点应独立于材料本身。

对本项目的直接建议：

- M7 基础材料先记录 `Damage`、`Velocity`、`Tilt` 和 `LastImpactTime`；
- M7 后续拼装层再加入 `SupportCount`、`ConstraintStrength`、`Load`；
- 破坏可以由两种条件触发：材料损伤超标，或支持图失效。

### 3.4 《Teardown》：局部破坏、大块动态化和远近分层

完全可破坏世界并不意味着所有东西一开始都必须是动态刚体。实用方案是：静态几何保持便宜；命中区域才转为动态代理或 Geometry Collection；远处效果用冲量、尘土、声音和延迟响应表达。

对本项目的直接建议：当前 HISM → 临时 Actor 的方向是正确的，但需要让“被撞歪的代理”保留碰撞、损伤和结构身份，而不是只拥有一次性动态状态。

### 3.5 《Besiege》：连接件失效优先于整体消失

机械装置的爽感来自连接关系变化：绳、链、活塞或约束先承担载荷，达到极限后断开，剩余部件因姿态和重力继续运动。

对本项目的直接建议：绳/铁链不要只当装饰圆柱。M7 后续拼装时应给它们独立的拉伸/剪切强度；连接件断开可作为低成本的第一层破坏反馈。

## 4. 推荐的碰撞结果模型

### 4.1 四层结果

| 层级 | 触发依据 | 视觉/物理效果 | 是否永久改变 |
| --- | --- | --- | --- |
| 接触 | 法向速度 > `ContactSpeed` 或有效重叠 | 撞击音、轻微抖动、低反弹 | 否 |
| 推动 | 冲量 > `PushImpulse` | 产生平移/旋转，HISM 提升为动态代理 | 是当前位置/姿态 |
| 损伤 | `DamageGain` 累积 | 裂纹、材质变暗、连接件变形、局部松动 | 是损伤值 |
| 破坏 | `Damage >= BreakDamage` 或结构失稳 | 碎裂/移除/爆炸 | 是模块状态 |

其中“推动”和“损伤”可以同时发生，但不能把推动直接等同于破坏。

### 4.2 建议的连续响应曲线

使用两个平滑区间，而不是一个硬分界：

```text
ContactWeight = SmoothStep(ContactSpeed, PushSpeed, NormalSpeed)
BreakWeight   = SmoothStep(BreakStartSpeed, BreakFullSpeed, EffectiveDamage)
PushImpulse   = NormalSpeed * Mass * ContactWeight * TransferCoefficient
DamageGain    = ImpactEnergy * DamageCoefficient * ContactWeight
```

建议初始区间：

```text
ContactSpeed     = 60–120 cm/s
PushSpeed        = 180–300 cm/s
BreakStartSpeed  = 650–900 cm/s（材料/鸟种独立）
BreakFullSpeed   = 1100–1600 cm/s
```

这些不是最终数值，而是用来避免“低于 520 完全不动、高于 1050 立即消失”的窄窗口。必须用测试关卡按质量和尺寸重新标定。

### 4.3 碰撞法向与掠射角

不能只使用鸟的总速度。建议使用：

```text
NormalSpeed = max(0, -dot(BirdVelocity - TargetVelocity, HitNormal))
ImpactAngle = abs(dot(-BirdVelocity.normalized, HitNormal))
AngleFactor = saturate((ImpactAngle - 0.25) / 0.75)
```

正面撞击应高效传递冲量；擦边撞击应主要产生切向滑动和旋转，损伤降低但不应完全没有反馈。

## 5. 本项目推荐的 M6/M7 物理架构

### 5.1 鸟撞目标

```text
Chaos Bird Hit
  -> Contact Event（始终触发）
  -> Compute NormalSpeed / ImpactEnergy / AngleFactor
  -> Apply Bird Rebound（速度保留 + 恢复系数）
  -> Apply Push Impulse（按质量和接触权重）
  -> Add Damage（按材料易损系数）
  -> Evaluate Constraint / Break
```

当前代码的 `KnockSpeedCMPerSec` 和 `BreakSpeedCMPerSec` 应保留为兼容参数，但建议逐步改名/扩展为 `PushImpulseThreshold`、`DamageCoefficient`、`BreakDamage`。这样调低 `KnockSpeed` 不会意外让所有物体直接破碎。

### 5.2 HISM 与动态代理

- 静态 HISM：只承载未受击实例；
- 第一次有效推动：从 HISM 移除，生成动态代理，并复制尺寸、材质、模块 ID、当前损伤；
- 再次命中：代理继续接受冲量和损伤；
- 发射回归后：代理停止模拟但保留 `QueryAndPhysics`，成为静态阻挡；
- 后续命中：冻结代理可重新激活，或按损伤直接破坏；
- 远处低强度冲击：优先只写损伤/轻微位移，避免一次爆炸唤醒全森林。

### 5.3 建筑结构层

M7 当前只提供材料基础层。后续拼装器应在材料 Actor 之上增加：

```text
BuildingGraph
├─ ModuleId / CellTopoCellId
├─ SupportLinks
├─ ConstraintStrength
├─ CurrentDamage
├─ Load / Stress
└─ BreakPropagationQueue
```

计算频率建议为事件驱动，不要每帧遍历全建筑：命中、绳链断裂、炸药桶爆炸和活塞冲击才触发局部图更新。

## 6. 对现有 M6/M7 参数的具体建议

### 6.1 立即可调、不改代码

当前可以先通过编辑器建立可玩窗口：

| 参数 | 当前作用 | 调参建议 |
| --- | --- | --- |
| M6 `KnockSpeedCMPerSec` | 鸟种基础撞开速度 | 不要直接大幅降低；先提高目标质量/代理冲量转移 |
| M6 `BreakSpeedCMPerSec` | 鸟种基础破坏速度 | 适当提高，避免首击全碎；用连续损伤替代单次依赖 |
| M6 `RetainedTangentSpeed` | 撞后鸟保留切向速度 | 提高可让鸟继续撞下一层；过高会穿透密集结构 |
| M6 `Restitution` | 鸟的法向反弹 | 木/玻璃低反弹，石/铁略高；反弹方向要稳定 |
| M7 `KnockSpeedCMPerSec` | 材料基础撞开阈值 | 作为推动门槛，不应作为破坏门槛 |
| M7 `BreakSpeedCMPerSec` | 材料基础破坏阈值 | 作为兼容后备值，最终由累积损伤决定 |
| 爆炸近圈/远圈 | 近处破坏、外圈冲击 | 近圈小而确定，外圈大而衰减；外圈不要直接删除 |

### 6.2 最值得优先实现的代码升级

1. 给 `AABTSM6DestructibleProxy` 和 `AABTSM7BuildingModule` 增加 `CurrentDamage`、`BreakDamage`、`DamageResistance`、`LastImpactTime`。
2. 将 HISM 命中改为“先计算损伤，再决定推动/破坏”；同一对象多次命中应累积。
3. 给代理复制一个稳定的 `ModuleId`；HISM 实例索引不能作为永久身份，因为 `RemoveInstance` 会改变后续索引。
4. 对炸药桶、活塞和黑鸟统一使用 `BlastEvent {Origin, Axis, DestroyRadius, ImpulseRadius, Impulse}`，由 M7 材料系统消费。
5. 对链式碰撞加入每次事件的传播预算、最小有效速度和冷却时间，防止密集森林碰撞风暴。
6. 将 `Freeze`、`Reactivate`、`Break` 做成明确状态迁移，禁止 QueryOnly 这种“不可见地退出物理世界”的状态。

## 7. 爽感反馈设计

物理正确不等于有爽感。每次有效命中至少要给玩家一个可读反馈：

- 轻撞：短促撞击声、模型轻微位移；
- 推动：明显旋转、尘土或材质擦痕；
- 损伤：裂纹/闪白/局部破损、音调升高；
- 破坏：碎片、爆裂声、短暂镜头冲击、附近物体被带动；
- 连锁：按时间顺序播放，而不是所有物体同帧消失；
- 远处冲击：较低音量、更长延迟和更小视觉幅度，帮助玩家理解因果距离。

建议加入一个很短的命中事件节奏：`0–80 ms` 接触反馈，`80–250 ms` 目标位移，`250–700 ms` 邻居响应，随后才进入结构破坏。这是感知层的“预备—释放—余震”，不是增加输入延迟。

## 8. 建议的验证矩阵

建立一块不依赖 PCG 的 M6/M7 物理测试台：

| 测试 | 目标 |
| --- | --- |
| 低速正撞木砖 | 有接触反馈，但不应穿过或完全静止 |
| 中速正撞木砖 | 砖被推动/倾倒，鸟仍有可控余速 |
| 高速正撞木砖 | 累积损伤达到破坏，后方目标继续响应 |
| 同速擦边撞石砖 | 主要滑动/旋转，损伤明显低于正撞 |
| 连续三次中速撞同一砖 | 第三次应比第一次更接近破坏 |
| 黑鸟近圈/外圈 | 近处破坏，远处冲击，不应整圈同时消失 |
| 炸药桶/活塞 | 径向与轴向传播方向可辨识 |
| 冻结后再撞 | 仍阻挡行走，能重新激活或破坏 |

所有测试记录：`NormalSpeed`、`ImpactEnergy`、`AngleFactor`、`DamageBefore/After`、`ImpulseApplied`、`StateBefore/After` 和 `ChainDepth`。只有这些数据齐全，才能分辨“物体没动”是阈值问题、质量问题、碰撞问题还是结构状态问题。

## 9. 对本项目的阶段结论

当前 M6/M7 的 HISM→代理路线、Chaos 球形鸟、近处破坏/远处冲击方向是正确的，但仍处在“阈值驱动原型”阶段。为了形成真正有爽感的连锁碰撞，下一版应优先加入**持续损伤和推动权重**，其次加入**连接/承重结构**，最后再加入碎片特效和音效。不要先靠把 `BreakSpeed` 降到很低来制造“更容易碎”，那会重新回到本次悖论。

最终希望玩家感受到的不是“我撞到一个阈值”，而是：

```text
我撞到了它
-> 它先被推动
-> 它撞到旁边的东西
-> 结构开始松动
-> 关键连接断开
-> 局部坍塌
-> 余波继续改变下一步弹道
```

