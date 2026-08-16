# M7.3 E1 有序真实 Brick 目标集合绑定设计

## 1. Authority 与边界

- 上游 authority 是 M3/Integration 即将发布的 honest seal：公开 E1 descriptor 中全部真实 Brick OBB 的有序 union。
- M7 不生成另一套轨迹几何，也不选择单个 `BrickId` 代替 union。
- `Entry.Bricks` 的原始顺序就是绑定顺序；每项必须满足 `BrickId == DescriptorIndex`。
- OBB 使用 descriptor 的真实逐轴尺寸：`HalfExtentCM = DimensionsCM / 2`。禁止把最大轴复制到另外两轴形成 cube。
- `Caps`（包括 Crystal）和 `Devices` 不属于轨迹 first-hit union。
- 本文和配套源码只准备 M7 consumer；M3 新 API、authority hash 和 shared seal 仍由 M3/Integration owner 发布。

## 2. M7 只读目标集合

`AABTSM73StableBuildingActor::CopyJuryDemoE1DestructibleModuleTargetSet()` 输出：

- `ManifestEntryId / DescriptorHash / StaticGeometryHash`；
- 每个 descriptor Brick 的 `BrickId`；
- unit-scale `FrozenWorldTransform`（中心与 OBB 旋转）；
- 真实 `HalfExtentCM`；
- 对应的 live `AABTSM7BuildingModule`；
- 唯一 owning `AABTSM73StableBuildingActor`。

查询仅在 production promotion 已把所有 E1 HISM Brick 一对一变成真实 Actor 后成功。任一数量、顺序、尺寸、material、scale、collision、BrickId 或 ownership 不一致时返回 false，不返回部分集合。

`ComputeOrderedGeometryHash()` 是 M7 本地关联诊断，不替代 M3/Integration 发布的 authority hash。

## 3. 真实命中与 damage lifecycle

每个 promoted descriptor Brick 在生成时保存冻结 `BrickId`。实际碰撞命中该模块后，`HandleBirdImpact()` 使用碰撞组件所属的真实 Actor 进入材料伤害路径，并把同一个 `BrickId` 记录到 E1 lifecycle。

合法首击必须满足：

1. 模块属于唯一 E1 StableBuildingActor；
2. 模块属于该 Actor 使用的 MaterialSystem；
3. `DamageLifecycleBrickId != INDEX_NONE`；
4. 该 Brick 来自公开 descriptor target set；
5. 命中发生在真实模块碰撞组件上，而不是 hidden proxy。

Crystal 顶帽仍是损伤链终点，但不是轨迹 union 成员。命中 device、cap 或直接摧毁 Crystal 不会设置 `bRealModuleImpactObserved`。

## 4. M3 seal 合入后的绑定切换

新 master 提供 set-based M3 runtime API 后，M7 GameMode 才执行以下原子替换：

1. 有界重试等待六栋静态注册、E1 production promotion、唯一 ordered target set、唯一且 Ready 的 SatelliteRuntime。
2. 将完整 target set 一次性交给 M3 runtime；禁止抽取 `BrickId4`、首项或任一代表 Brick。
3. M3 以 frozen OBB union 做轨迹证书，以每项 live module/component 做真实 first-hit 身份。
4. 绑定成功后一次性清 timer；数量、Hash、顺序或 ownership 不一致立即 fail closed。
5. 移除旧 `BindProductionE1CrystalTarget()` 消费后，再单独清理 Crystal 命名兼容桥。

在 M3 honest seal 合入前，不修改 shared contract/M3 runtime，也不把当前单 Crystal API 宣称为新 authority。

## 5. 验证与日志计划

- 轻量纯状态：长砖 `144x18x18 cm` 必须保留 `72x9x9 cm` half extent；max-axis cube 必须产生不同 target identity。
- 轻量集合：缺项、重复/乱序 BrickId、非正 extent、非 unit-scale OBB、空 live ownership（生产模式）均拒绝。
- production source：查询数量精确等于 E1 descriptor Brick 数，逐项 BrickId/material/scale/collision/ownership 一致。
- fresh D3D11：轨迹 first-hit 可落在集合中任一真实 Brick，命中日志输出实际 `BrickId`，随后物理损伤链可摧毁 Crystal。
- 禁止使用 proxy 命中、单 Brick 代表证书或脚本直接删除 Crystal 作为通过证据。

计划日志字段：`DescriptorHash / StaticGeometryHash / TargetGeometryHash / TargetBrickCount / FirstHitBrickId / ModuleOwner / StructuralResponse / CrystalChain / Accepted`。
