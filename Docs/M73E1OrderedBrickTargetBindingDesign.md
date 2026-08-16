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

## 4. M3 honest seal 下的绑定切换

`master@82bb769` 的 M3 runtime 公开入口仍以 cap pose/half extent 恢复 frozen SiteTransform，但其内部会定位唯一 E1 StableBuildingActor，并逐材质 HISM 按 descriptor 有序 exact 审核完整 54-Brick union。因此 M7 不把 cap 当目标，只生成一次性、无碰撞、调用后立即销毁的 site-recovery adapter；真正的 gameplay target 始终是 E1 Actor，first-hit authority 始终是 54 个真实 Brick OBB。

1. 有界重试等待六栋静态注册、唯一 pre-promotion ordered HISM union 和唯一且 Ready 的 SatelliteRuntime。
2. M7 先 exact 审核 54 项 descriptor/HISM 顺序、逐轴 OBB、collision 与 owner，再把 unit-scale cap descriptor pose 仅作为 site-recovery adapter 调用 M3。
3. M3 再独立 exact 审核同一真实 E1 Actor 的 54-Brick HISM union，成功后销毁 stand-in，并把 gameplay target 切换为真实 E1 Actor。
4. M7 只在 M3 成功后 promotion；promotion 后每个 descriptor Brick 一对一绑定真实 damage module，Caps/Devices/Crystal 仍不进入 first-hit union。
5. 绑定成功后一次性清 timer；数量、Hash、顺序、scale、collision 或 ownership 任一不一致立即 fail closed。

旧 `CopyJuryDemoE1CrystalTarget()` 仅保留 ABI/历史测试兼容，production GameMode 不再消费它。

## 5. 验证与日志计划

- 轻量纯状态：长砖 `144x18x18 cm` 必须保留 `72x9x9 cm` half extent；max-axis cube 必须产生不同 target identity。
- 轻量集合：缺项、重复/乱序 BrickId、非正 extent、非 unit-scale OBB、空 live ownership（生产模式）均拒绝。
- production source：查询数量精确等于 E1 descriptor Brick 数，逐项 BrickId/material/scale/collision/ownership 一致。
- fresh D3D11：轨迹 first-hit 可落在集合中任一真实 Brick，命中日志输出实际 `BrickId`，随后物理损伤链可摧毁 Crystal。
- 禁止使用 proxy 命中、单 Brick 代表证书或脚本直接删除 Crystal 作为通过证据。

计划日志字段：`DescriptorHash / StaticGeometryHash / TargetGeometryHash / TargetBrickCount / FirstHitBrickId / ModuleOwner / StructuralResponse / CrystalChain / Accepted`。

## 6. 与生产 Chaos 的同物理身份

- exact union 审核发生在 promotion 前；HISM 被替换为真实模块后，damage lifecycle 使用相同 `BrickId` 和 frozen OBB 顺序。
- E1 位于连续卫星球面，但其 BuildingFreeze 几何是 frozen SiteTransform 的局部切平面。生产必须显式物化与夹具相同的 `PadHalfExtentCM × 100 cm` WorldStatic tangent support；该支撑不进入目标 union，也不改变共享 Static/Placement/Layout 身份。
- canonical 首跑进一步证明，E2–E6 的 M3 pad 数学查询虽为切平面，实际刚体接触仍来自 tessellated ProceduralMesh；M3 自身允许最高 `35 cm` collision residual，不能与 Stage5 精确 box floor 互相替代。因此六栋都物化同一冻结尺寸/厚度的精确 tangent support，M3 继续拥有站点位置、地表和 grade skirt。
- 六栋观察共同使用 60 Hz fixed step、post-activation frame barrier、SiteUniformTangentGravity 和 enhanced deterministic Chaos solver；Candidate 必须编码这些 consumer policy。
- solver/fixed-step 是有界作用域：成功、拒绝、Actor 丢失及 EndPlay 都恢复进入前状态。Prepare 若发现 support 或 solver identity 漂移则 fail closed。

## 7. Candidate v8 最终发行阻断

- E1 exact 54-Brick union 已在 canonical 中绑定真实 `AABTSM73StableBuildingActor`，`Bricks=54`、`Geometry=3519185746`、`CapsDevicesExcluded=1`、`StandInRetired=1`；该 target authority 没有回退。
- 六栋批次改为同一 fixed-step/determinism 作用域内按 E1→E6 逐栋跨帧激活，排除了大批刚体并发导致 E5 不稳的假设。
- E5 单栋 frozen-pad fixture 全睡眠且最终空间门通过，真实地图串行 E5 仍有 1216 awake 与 9.594 cm 最终漂移。源码上 M3 continuous surface 和 M7 tangent pad 都是 Static BlockAll，故真实地图存在夹具没有的第二支撑碰撞 authority。
- 在 Integration/M3 明确 pad 区域唯一 collision authority 前，M7 必须保持 `BuildingValidation=Rejected`；不得通过放宽 final/quiet/awake、强制 sleep、回写 transform 或关闭整个主星 collision 伪造通过。

## 8. Candidate v9 碰撞分层实机结论

- M7 GameMode 已把碰撞 override 做成代次绑定的有界资源：六个 exact pad 全部通过严格身份校验后、任何 building activation 前，保存主星与卫星 ContinuousSurface 的原 DeveloperObstacle 响应并切为 Ignore；成功保持到 EndPlay，setup/canonical failure 与 generation retry 恢复。
- override 只操作 `ABTSDeveloperObstacleChannel`。每块 ContinuousSurface 的 Pawn、WorldStatic、PhysicsBody 响应在切换前后逐项恒等，六个 tangent pad 对 DeveloperObstacle 继续 Block。
- 唯一 fresh canonical 证明 override 与恢复日志成立，却没有改变 E5：最终/峰值和 awake 数与 v8 相同。源码审计确认生产 module 激活时 `SetCollisionProfileName("PhysicsActor")` 把 ObjectType 重设为 `PhysicsBody`；因此被 terrain Ignore 的 DeveloperObstacle 并不是实际动态 brick 的当前 ObjectType。
- Candidate v9 由此 fail closed。下一次验证前必须先在 M7-owned 动态激活边界恢复 DeveloperObstacle ObjectType，同时保留 PhysicsActor 的响应容器、模拟状态和玩家/鸟交互；未经新的 fresh canonical 不得提交为发行修复。

## 9. Candidate v10 动态 shape-filter 实机结论

- `ActivateDynamic()` 在应用 `PhysicsActor` profile 后立即恢复 `ABTSDeveloperObstacleChannel`；profile 的 QueryAndPhysics、Physics simulation、通知与响应容器仍保持，不改玩家/鸟/普通物理交互。
- 每个 SiteUniform module 激活后、观察开始前，M7 读取 component ObjectType、physics actor 与所有 live `FPhysicsShapeHandle` 的 `FShapeFilterData`。任一 shape 非 DeveloperObstacle、没有 shape 或没有 actor 均 fail closed。
- 唯一 fresh canonical 没有触发 `CollisionIdentityInvalid`，且 E5 指标由 v9 的 `9.594/2.670/1.497`、`10.138/4.275/2.887` 改变为 `9.245/2.794/2.510`、`10.834/4.412/3.388`，这证明过滤身份修复已作用于真实 Chaos。
- E5 仍未达到 final `4/6/2`、quiet/end-quiet 和 `FinalAwake=0`，所以 v10 继续 fail closed；E6 未运行。下一步必须针对 E5 的实际结构/接触动力学提出新的最小修复授权，不能把当前修复或静止时间视作通过。

## 10. Candidate v11 首击延迟 Chaos

- 启动期仍精确物化 descriptor、frozen tangent pad 与所有真实模块，并执行质量、支撑、穿透、装配与 54-Brick OBB union 预检；仅不把无玩家命中的建筑提前推进到动态观察窗。
- 每栋独立记录 `ChaosDeferredUntilFirstHit=1`。任何真实 module damage 在原伤害继续之前先原子激活该栋完整 module/device/cap 集合，保持 `PhysicsActor` response、`ABTSDeveloperObstacle` object/shape filter、对应 pad 与 `SiteUniformTangentGravity`；直接 Crystal、脚本删除与 blast 均不能替代 `ModuleContact -> Crystal` 因果链。
- fresh canonical `M7-BC-142-CandidateV11-DeferredStartup-L_ABTS_M10-20260816-FreshOffscreenD3D11.log` 以 E1 exact 54 union 和 stand-in retired 完成 WorldReady；这是启动就绪证据，不重命名为六栋 startup Chaos 稳定认证。
