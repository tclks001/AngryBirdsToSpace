# ABTS 技术演示数值冻结设计

> 状态：Phase 1 `TechnicalFrozen` 已设计并由集中 Manifest 锁定；完整流程仍为
> `IntegrationPending`，不得把本稿当作 E1→E6→M11 已拉通的证据。
>
> 目标：本项目以 PCG、物理破坏、球面移动和引力弹弓的技术展示为主。数值只需
> 清晰、确定、可复现并保证主线无明显软锁，不追求复杂的经济、Build 或长期留存平衡。

## 1. 冻结原则

数值状态只使用以下三级：

| 状态 | 含义 | 变更门槛 |
| --- | --- | --- |
| `TechnicalFrozen` | 单位、公式语义、确定性目录、版本和 Hash 已锁定；用于给其他系统提供稳定标尺 | 修改时提升版本、更新集中 Manifest，并重跑受影响自动化 |
| `PlaytestCandidate` | 当前可运行默认值，但尚未由完整流程证明节奏与供需合理 | 可以调整；不得在文档或日志中称为生产冻结 |
| `ProductionFrozen` | 完整生产消费、fresh 自动化和对应可见 PIE 都已通过 | 只能按版本化重冻流程修改 |

计算能够证明确定性、供需下界和“不会软锁”，不能证明数值“好玩”。本项目不建立
复杂数值策划流程：拉通后只修正明显过难、过慢、资源溢出和软锁，不为追求精细平衡
反复移动已经冻结的技术标尺。

## 2. 集中冻结入口

第一阶段的跨系统身份由
`FABTSTechnicalDemoNumericFreeze::ValidateCurrentProject` 集中校验，自动化过滤器为：

```text
ABTS.Contracts.TechnicalDemoNumericFreeze
```

Manifest v1 的冻结身份为：

```text
ManifestVersion=1
ManifestHash=0x3B5CD2611E423715
```

Manifest 只聚合各系统的权威冻结工厂或发布目录，不复制第二套运行时实现。任一来源
版本、Hash、条目、顺序语义或成员数量漂移时，自动化必须 fail closed；有意修改则
提升 Manifest 版本并重新认证，不直接改期望 Hash 掩盖漂移。

## 3. Phase 1 TechnicalFrozen 清单

### 3.1 M6 常规弹弓 Launch Profile V0

共同参数：

| 参数 | 冻结值 |
| --- | ---: |
| Catalog Version | 1 |
| Flight Air Drag | 0.08 /s |
| Aim Camera Distance | 1500 cm |
| Aim Camera Pitch | -3 deg |
| Aim Target Forward / Height | 900 / 245 cm |
| Pull Distance | 120–430 cm |
| Initial Pull | 0.55 |
| Comfortable Pull | 0.60–0.85 |
| Maximum Aim Plane Offset | 260 cm |
| `LaunchProfileHash` | `0xC2B94139752AD846` |

| Tier | Min / Max Speed | Exponent | Wheel Step |
| --- | ---: | ---: | ---: |
| Twig | 700 / 1700 cm/s | 1.15 | 0.04 |
| Simple | 900 / 2300 cm/s | 1.08 | 0.02 |
| Reinforced | 1050 / 3300 cm/s | 1.00 | 0.01 |

冻结公式：

```text
Speed = lerp(MinSpeed, MaxSpeed, clamp(PullAlpha, 0, 1) ^ Exponent)
```

M11 Space 发射继续使用其独立、已认证的 M11 v1 Launch Model，不并入本表。

### 3.2 M3 JuryDemo Fixed-Six V2

| 字段 | 冻结值 |
| --- | --- |
| Contract Version | 2 |
| World Seed / Candidate | `312503 / 4` |
| Demo Manifest | `1 / 2324068295` |
| Placement Schema | 1 |
| Placement Catalog Hash | `0x9F9DA4381EEF7CA8` |
| Layout Hash | `0x7029074579FDC52E` |
| Ordered Sites | E1 → E2 → E3 → E4 → E5 → E6 |

该冻结只说明 Demo 固定六栋的放置身份稳定，不说明通用随机地图已冻结，也不说明六栋
已经 `ChaosReady`。

### 3.3 M7 固定六栋静态目录

| Encounter | Stable ID | Profile | Tier | Seed | Pad Half Extent | Active Members |
| --- | --- | --- | ---: | ---: | ---: | ---: |
| E1 | DemoE1ColumnBreak | ColumnBreak | 0 | 710000 | 450 × 198 cm | 52 |
| E2 | DemoE2DropTrigger | DropTrigger | 1 | 740000 | 810 × 486 cm | 235 |
| E3 | DemoE3SlideRelease | SlideRelease | 2 | 750137 | 1062 × 450 cm | 364 |
| E4 | DemoE4TipOver | TipOver | 3 | 730000 | 882 × 414 cm | 872 |
| E5 | DemoE5SeamRelease | SeamRelease | 4 | 720000 | 1386 × 666 cm | 1807 |
| E6 | DemoE6TipOver | TipOver | 5 | 750000 | 1098 × 522 cm | 2174 |

冻结目录 Hash 为 `0x9F9DA4381EEF7CA8`，总活动成员数为 `5504`。集中 Manifest
同时绑定六条 Descriptor Hash 和成员数；Stage 4.5 自有自动化继续负责从真实静态几何
重建并核对这些描述。

这里冻结的是静态几何、Profile 身份和放置尺度。弱点、破坏阈值、Chaos 崩塌比例、
奖励数量和目标击打次数都不在 Phase 1 冻结范围内。

### 3.4 M11 production v1

| 身份 | 冻结值 |
| --- | --- |
| Preset Source / Preset Hash | `0x7DBF1BA71F67768E` |
| Canonical Scenario Hash | `0x62D86D29` |
| Scan Contract Hash | `0x8A6D71CF21E552C9` |
| Certification Hash | `0x941684A72E11B27D` |
| Nominal Trajectory Hash | `0x185D3B673C1D52AF` |
| Physical Playback Contract | 1 |
| Physical Playback Trajectory Hash | `0xCAC902C4183084AF` |
| Certified Bundle Hash | `0xA219D69CF3F92AF0` |

Editor-only v2/v3 Candidate 的 Certification/Bundle Hash 为零，不得通过更新本 Manifest
绕过 M11 的完整认证并替换 production v1。

### 3.5 合成拓扑

Phase 1 只冻结配方 ID、产物、单次产量和工作站依赖；材料种类与数量暂时保持
`PlaytestCandidate`。

| Recipe ID | Output | Qty | Station |
| --- | --- | ---: | --- |
| WorkbenchKit | WorkbenchKit | 1 | None |
| SimpleStake | SimpleStake | 1 | Workbench |
| SimpleCord | SimpleCord | 1 | Workbench |
| FurnaceKit | FurnaceKit | 1 | Workbench |
| BridgeKit | BridgeKit | 1 | Workbench |
| ReinforcedStake | ReinforcedStake | 1 | Furnace |
| ReinforcedCord | ReinforcedCord | 1 | Furnace |
| SpaceStakePair | SpaceStake | 2 | Furnace |
| SpaceCord | SpaceCord | 1 | Furnace |

`RecipeTopologyHash=0x3C17FB587178014B`。旧 `SpaceSlingshotPart` 不得重新进入配方。

## 4. 冻结的计算语义

### 4.1 合成需求

令 `R[r,i]` 为配方 `i` 对资源 `r` 的单次需求，`x[i,k]` 为到里程碑 `k` 前必须
制作的次数：

```text
Demand[r,k] = sum(R[r,i] * x[i,k])
```

材料数量变化不改变公式语义，但必须重算全部里程碑，不允许只检查终局总量。

### 4.2 无软锁供需门

对每个强制里程碑和每种资源都必须满足：

```text
GuaranteedSupply[r,k] + PriorInventory[r,k]
  - MandatorySpend[r,k]
  >= NextMandatoryDemand[r,k] + SafetyBuffer[r,k]
```

技术演示版采用最简单的硬保证：

- 强制资源按固定奖励包或固定地面保底发放，不把平均随机掉落当作保证；
- 关键奖励只计算玩家在正常路线中必然可取得的数量；
- `SafetyBuffer` 至少为下一最小强制配方的一份，避免一次误放导致永久软锁；
- 强制奖励漏发、重复发放、阶段顺序错误或库存不足必须记录明确原因，不能静默继续；
- 随机地图恢复后，强制资源使用所有接受 Seed 的严格最小值；P01/P05 只能做体验观察。

### 4.3 撞击破坏

当前公式语义冻结为：

```text
DamagePerHit = A * (NormalImpactSpeed / (BreakSpeed * BirdMaterialScale)) ^ 2
```

冻结的是单位、法向撞击速度口径和累计伤害语义，不冻结 `A`、`BreakSpeed` 或
`BirdMaterialScale` 的当前数值。目标击打次数 `H` 确定后可反解候选阈值：

```text
BreakSpeed * BirdMaterialScale
  = ReferenceNormalImpactSpeed * sqrt(H * A / BreakDamage)
```

最终门仍需真实 M6 发射遥测和 Chaos 崩塌验证；初始发射速度不能替代实际法向撞击速度。

## 5. 当前软锁审计

Phase 1 完成后仍有两个明确阻断项，因此整局状态保持 `IntegrationPending`：

1. `SpaceCord` 需要 `CrystalCore ×1`，当前生产运行时尚未形成已证明的 CrystalCore
   奖励交付。M3 Witness 中的模拟账本不是玩家库存发放证据。
2. 当前地面资源日志记录的是 Actor 数量，而每个 Actor 实际可含 1–2 单位；在加入
   “逐里程碑资源单位”日志前，无法对真实库存做严格供需证明。

另外，M8 当前按实际毁坏砖逐块回收；M7 固定六栋共有 5504 个活动成员，而配方需求
只有几十单位。`每砖回收 1` 不进入冻结，Phase 2 应改为逐 Encounter 固定奖励预算或
带上限的指定货物/核心奖励，避免建筑复杂度直接放大经济产出。

## 6. 最小拉通方案（Phase 2）

不做复杂数值策划，直接采用一条固定制作路线：

```text
Workbench ×1
→ Simple Slingshot ×1
→ Furnace ×1
→ Reinforced Slingshot ×1
→ Space Slingshot ×1
```

Bridge 只有在地图主线确实强制过河时才计入一份强制需求，否则保持可选。普通与强化
弹弓组件默认可在后续槽位复用；若运行时仍坚持安装即永久消耗，则必须按实际消耗套数
重算供需并为每套增加确定性保底。

Phase 2 只需交付：

1. 明确组件复用/消耗次数；
2. 生成逐里程碑需求表；
3. 为 E1–E6 定义有上限的固定奖励表，并在需要 SpaceCord 前固定发放一个 CrystalCore；
4. 记录实际资源单位、消费、奖励和里程碑库存；
5. 自动化证明所有强制里程碑满足供需不变量；
6. 在完整 E1→E6→M11 可见 PIE 中确认路径可理解且没有交互软锁。

## 7. 变更与验收

Phase 1 任一冻结项改变时：

1. 修改该系统自己的权威工厂或目录；
2. 运行该系统原有派生/确定性/认证门；
3. 在集成工作树提升集中 Manifest 版本并更新身份；
4. 运行 Development Editor 全链接与
   `ABTS.Contracts.TechnicalDemoNumericFreeze` fresh NullRHI 自动化；
5. 若改变发射、建筑、地图或终局实际行为，再执行对应实时 Chaos/可见 PIE，不能用
   Manifest 测试替代体验和运行时证据。

Phase 1 自身不需要修改 Blueprint 或地图资产。
