# M7.3 Beam Stage 5.5 炸药桶/活塞装配设计

> 状态：2026-08-15 固定演示六栋首版静态装配完成，并已通过用户编辑器视觉验收；Chaos 触发效果尚未验证。

## 1. 目标与边界

Stage 5.5 在已冻结的 Stage 5 生产建筑上派生炸药桶或活塞槽位，不重新生成 WFC、芯体、外框、楼面、
屋顶或 Beam-C 闭合构件。失败时只拒绝装置层，禁止换 Seed、扫描候选建筑或改写 Stage 5 Hash。

本阶段证明：装置具有确定性位置、合法体积、显式静态支撑路径、可见编辑器资产和运行时刚体入口。
它不证明建筑或装置进入 Chaos 后稳定，也不证明爆炸/活塞效果适合作为最终关卡弱点。

## 2. 统一 36 cm 占位

- 设备分析与建筑共用唯一 36 cm 三轴格，不存在 18 cm 兼容层。
- 建筑 X/Y AABB 边界为 `18 + 36n cm`，Z 边界为 `GroundZ + 36n cm`；每个 Stage 5 brick 必须能被
  精确展开成整数 cell，任一错相位构件立即以 `BeamD1Stage55NonVoxelBrick` 失败。
- 炸药桶逻辑占位为 `72×72×180 cm`，即 `2×2×5` cell。
- 活塞逻辑占位为 `216×72×72 cm`，随 X/Y/Z 轴旋转为 `6×2×2`、`2×6×2` 或 `2×2×6` cell。
- 真实 Dynamite/Spring mesh 仅作表现，按逻辑包围盒缩放；Chaos 权威使用同尺寸的简单碰撞代理。表现
  mesh 无碰撞，不能扩大 DAG 占位或制造第二刚体。

## 3. 确定性候选与支撑 DAG

候选按底部 course、边界距离、XYZ 格点和轴向稳定排序。每个候选必须同时满足：

1. 全部 cell 为空，和 Stage 5 brick 无正体积重叠；
2. 每个 cell 中心仍属于最终 WFC 语义包络；
3. 不进入任何 `ReservedSupportVoid`；
4. 竖直装置底面四角均有支撑，水平活塞两端均有支撑；
5. 支座是地面时登记 `DirectGroundSupport`，否则登记实际 Stage 5 member id，且这些节点全部向地可达。

装置保持一个刚体和一个 Device Load 节点，不把占据的 cell 伪装成多根积木。静态质量只用于装置账本，
不反写冻结的 Stage 5 Load DAG。分别计算 `DeviceSlotHash`、`DeviceLoadDAGHash` 和
`DeviceAssemblyHash`，便于后续 Chaos/玩法层精确绑定。

## 4. 首版演示策略

`DeviceIntent` 优先决定类型；无明确设备意图时，TipOver 使用活塞，其余使用炸药桶。每栋首版只放一个
装置，自动布局不追求最佳破坏位置。当前固定六栋均选择了语义包络内的接地空 cell，因此设备是显式地面
根，不虚构积木支座；后续若需把装置抬到楼面，只能选择真实、向地可达的 Stage 5 member。

编辑器选择 `Generation Stop Stage = Stage 5.5 - Barrel / Piston Assembly` 时显示完整 Stage 5 建筑及
真实 Dynamite/Spring 资产。PIE/runtime 将每个设备生成成一个碰撞代理 Actor 加一个无碰撞表现子网格；
碰撞代理关闭自身渲染 pass，而不能隐藏根组件并连带隐藏表现网格。

## 5. 验收与未完成项

- `ABTS.M73DAG.BeamC3V3.Demo.Stage45PlacementFreeze`：统一网格后的六栋目录一致。
- `ABTS.M73DAG.BeamC3V3.Demo.Stage5Production.SixBuildings`：六栋静态生产 DAG 通过。
- `ABTS.M73DAG.BeamC3V3.Demo.Stage55DeviceAssembly.SixBuildings`：每栋恰好一个装置、无穿透、有明确支撑，
  且固定清单同时包含炸药桶和活塞。
- `ABTS.M73DAG.BeamC3V3.Demo.Stage55DeviceAssembly.EditorPreviewE6`：真实 PreviewActor 路由显示全部生产
  brick 和一个装置资产，装配 Hash 与直接 producer 一致。

用户已在编辑器中验收固定六栋的资产比例、朝向、装配位置和可见性。尚未验证：装置和积木同时进入 Chaos 后的
接触稳定性、鸟撞触发、爆炸/活塞冲量与破坏半径、关卡玩法效果。不得用上述静态门或编辑器视觉验收替代这些
动态证据。
