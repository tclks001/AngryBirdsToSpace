# M6 弹弓视觉表现设计

> 状态：M7.1/M6 共享的弹弓视觉协议与实现说明。本文只描述弹弓桩、双弹弓弦和弹珠袋的表现，不改变 M6 的发射判定、弹道或破坏规则。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [M6 发射与碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M7.1 平面测试台](M71PlanarPhysicsTestStageDesign.md) · [Low Poly/AI 资产工作流](LowPolyAssetProductionAndAIReportWorkflow.md)

## 1. 目标与统一原则

弹弓由两个固定桩、两段可伸缩弹弓弦和一个可移动弹珠袋组成。待机、瞄准与拉伸阶段只移动弹珠袋；桩始终固定，弦每帧根据两个连接端点重建长度和姿态。

所有正式静态网格体都遵守同一原则：

- UE 单位为厘米，导入 `Uniform Scale` 使用 `1.0`，DCC 导出前应用物体缩放。
- `Mesh` 的尺寸由导入后的 Static Mesh Bounds 判断，代码自动缩放到编辑器中的目标厘米尺寸，不再假设源模型一定长 100 cm。
- `LocalScale` 默认必须为 `(1,1,1)`，只用于最后的美术微调，不能再承担厘米换算。
- `LocalOffsetCM` 默认必须为 `(0,0,0)`，表示适配后的厘米偏移，不会再随模型缩放倍增。
- 符合协议的模型必须使用零 `LocalRotation`；只有旧资产轴向错误时才临时用它修正。

## 2. 弹弓局部坐标

完整弹弓 Actor 的局部坐标严格定义为：

| 轴 | 含义 |
|---|---|
| `+X` | 发射方向、弹珠袋向后拉动时的反方向参考轴 |
| `+Y` | 两个桩的排列方向；A 位于负 Y，B 位于正 Y |
| `+Z` | 弹弓向上方向；球面地图中由局部径向/地表朝向决定 |

沿测试台弹弓 Actor 的 Y 轴缩放只修改桩间距；X/Z Actor Scale 不参与弹弓几何尺寸。桩高、弦粗和袋尺寸必须通过专属厘米参数调整。

## 3. 静态网格体规格

### 3.1 弹弓桩 `StakeVisual`

| 项目 | 严格约定 |
|---|---|
| 枢轴点 | 桩底截面的几何中心，即本地 `(0,0,0)` |
| 向上轴 | 本地 `+Z` |
| 横截面 | 位于本地 XY 平面 |
| 推荐源 Bounds | `20 × 20 × 100 cm`（X × Y × Z） |
| 默认运行目标 Bounds | `28 × 28 × 220 cm`，分别来自 `StakeDiameterCM` 与 `StakeHeightCM` |
| 默认 Slot 修正 | Offset `0,0,0`；Rotation `0,0,0`；Scale `1,1,1` |

桩模型不得包含地面、空节点造成的额外 Bounds，也不得有顶点低于底面。代码以 Bounds 底面中心对齐桩基准点，并按实际导入 Bounds 适配目标直径和高度。因此 UE 的中心枢轴回退圆柱体与底部枢轴正式模型都能落在同一底面；正式资产仍必须采用底部枢轴，便于单独拖入关卡时正确吸附地面。

### 3.2 弹弓弦 `CordVisual`

| 项目 | 严格约定 |
|---|---|
| 枢轴点 | 弦段 Bounds 的长度中心 |
| 长度轴 | 本地 `+Z`，端点位于 `-Z/+Z` |
| 横截面 | 位于本地 XY 平面，X/Y 尺寸相等 |
| 推荐源 Bounds | `4 × 4 × 100 cm`（X × Y × Z） |
| 运行目标 Bounds | `CordThicknessCM × CordThicknessCM × 当前端点距离` |
| 默认 Slot 修正 | Offset `0,0,0`；Rotation `0,0,0`；Scale `1,1,1` |

弦的长度完全由两端连接点决定。不要在模型中预留松弛弧线、额外尾巴或不可见辅助几何，否则这些内容也会进入 Bounds 并造成视觉压缩。若需要弯曲弦，应另行升级为样条或骨骼表现，不能混入当前直线网格协议。

### 3.3 弹珠袋 `PouchVisual`

| 项目 | 严格约定 |
|---|---|
| 枢轴点 | 袋体 Bounds 几何中心 |
| 本地 X | 由本地 Y/Z 按右手坐标系派生；不承载发射方向 |
| 本地 Y | 左右宽度、两根弦的连接方向 |
| 本地 Z | 待机时朝局部上方；进入 Ready/Pulling 后朝即时发射方向，可指向前方或后方 |
| 推荐源 Bounds | `42 × 60 × 12 cm`（X × Y × Z） |
| 默认运行目标 Bounds | `PouchSizeCM=(42,60,12)` |
| 默认 Slot 修正 | Offset `0,0,0`；Rotation `0,0,0`；Scale `1,1,1` |

袋体弦连接点不从模型顶点或 Socket 自动推导，而由 `PouchAConnectionOffsetCM` 和 `PouchBConnectionOffsetCM` 指定。默认分别为 `(0,-18,0)` 与 `(0,18,0)`，必须落在袋体左右范围内。两个偏移会乘以 `PouchVisual.LocalScale` 的各轴绝对值：例如将 `PouchVisual.LocalScale.Y` 调为 `1.5` 时，默认弦端会从 Y=`±18 cm` 同步扩展到 `±27 cm`，始终保持在袋体边缘的相对位置。拉伸时袋体局部 Y 始终投影到固定的桩间方向；两根弦还会按总长度最短的方式选择对应袋边，杜绝反向瞄准时形成交叉 X。

## 4. 目标尺寸适配和枢轴处理

`ABTSMakeSlingshotVisualTransform` 是编辑器预览和 PIE 运行时共用的唯一视觉适配入口：

1. 读取当前 Static Mesh 的导入后 Bounds。
2. 用目标 X/Y/Z 厘米尺寸分别除以源 Bounds 尺寸，得到 Fit Scale。
3. 乘以 Slot `LocalScale` 美术微调值。
4. 桩使用 `BoundsBottomCenter` 对齐桩底；弦和袋使用 `BoundsCenter` 对齐。
5. 最后施加未参与缩放的 `LocalOffsetCM`。

这条规则修复了旧实现的两个问题：旧代码把所有模型都当成 `100 × 100 × 100 cm` 的 UE 基础几何体；同时把桩 Actor 放在半高位置后直接把自定义模型枢轴放到该位置。底部枢轴模型因此从腰部开始生长，自定义弦也会被再次按 100 cm 基准拉伸。现在模型自身的 Bounds 和锚点均被显式纳入计算。

## 5. 连接参数

`FABTSSlingshotConnectionLayout` 位于 `ABTS | M7.1 | Slingshot | Connections`：

- `StakeAConnectionOffsetCM`、`StakeBConnectionOffsetCM`：从自动计算的桩顶位置继续偏移，用于把弦端移动到桩顶内侧。
- `RestPouchOffsetCM`：两桩弦端中点到待机袋中心的偏移；默认零向量。
- `PouchAConnectionOffsetCM`、`PouchBConnectionOffsetCM`：袋局部左右弦端，默认 `(0,-18,0)` 与 `(0,18,0)`。
- `BirdInPouchOffsetCM`：位于 `AABTSM6SlingshotSystem / ABTS|M6|Visual`，默认 `20 cm`；表示鸟 Actor 相对袋中心沿袋体局部 `+Z` 的偏移，四只鸟共享该参数。

连接参数不随网格源 Bounds 变化；但会随 `PouchVisual.LocalScale` 成比例变化，使弦端与袋体视觉缩放同步。更换模型时，先保持布局参数不变并确认三个模型协议，再只微调连接点。

## 6. 编辑器配置步骤

1. 在 Static Mesh Editor 中打开资产，检查 `Approx Size` 和 Bounds，确认轴向、尺寸与枢轴协议。
2. 在 M7.1 测试场选中 `AABTSM71PlaceableSlingshotActor`。
3. 设置 `StakeVisual.Mesh`、`CordVisual.Mesh`、`PouchVisual.Mesh`，先把三个 Slot 的 Offset/Rotation 归零、Scale 设为一。
4. 用 `StakeHeightCM`、`StakeDiameterCM`、`CordThicknessCM` 和 `PouchSizeCM` 调整最终厘米尺寸。
5. 确认总体尺寸正确后，再用连接参数调整弦端；最后才允许使用 Slot 微调。
6. 不要用 Actor X/Z Scale 修正模型大小，也不要用很小的 `LocalScale` 抵消错误的导入单位。

推荐初始值：

```text
BaseStakeSpacingCM = 210
StakeHeightCM = 220
StakeDiameterCM = 28
CordThicknessCM = 3.5
PouchSizeCM = (42, 60, 12)
PouchAConnectionOffsetCM = (0, -18, 0)
PouchBConnectionOffsetCM = (0, 18, 0)
BirdInPouchOffsetCM = 20
```

## 7. 无模型回退协议

- 桩：UE Engine Cylinder，原始中心枢轴；代码以其 Bounds 底面重新对齐，因此不会要求正式底部枢轴模型迁就回退模型。
- 弦：UE Engine Cylinder，按 Bounds 中心和本地 Z 轴拉伸。
- 袋：UE Engine Sphere，自动适配 `PouchSizeCM` 成为扁平椭圆片。

回退模型的枢轴差异只存在于资产内部，最终三者都经过相同的 Bounds 锚点适配。不得再把回退圆柱体的中心枢轴假设传播给正式桩模型。

## 8. 验收

1. 未配置模型时，桩底位于 Actor 地面，桩顶高度等于 `StakeHeightCM`。
2. 换成符合协议的底部枢轴桩后，底面和桩顶位置与回退模型一致，不会从半高处开始。
3. 更换任意实际源长度的弦模型后，最终弦粗仍等于 `CordThicknessCM`，长度恰好连接两端。
4. 编辑 `PouchSizeCM` 时袋体按 X/Y/Z 目标 Bounds 变化，弦连接点不被模型缩放重复放大。
5. 沿 Actor Y 缩放时仅改变桩间距；预览和 PIE 中连接关系一致。
6. 调整 `PouchVisual.LocalScale.Y` 时，两根弦的袋端沿 Y 等比例移动；预览和 PIE 中一致。
7. 向任意方向拉伸时，袋体本地 `+Z` 对齐即时发射方向，本地 `+Y` 保持桩间侧向；两根弦不得交叉连接。
8. `BirdInPouchOffsetCM=20` 时，鸟体中心始终位于袋中心沿当前袋体局部 `+Z` 的 20 cm 位置；改变发射方向后偏移随袋体旋转。
9. M6 拉动期间只有袋移动，两根弦持续连接四个端点，松手与回归后恢复待机状态。

## 9. 排错

| 现象 | 检查项 |
|---|---|
| 桩仍从腰部开始 | 确认使用的是本次编译后的类；将 `StakeVisual.LocalOffsetCM` 归零，并检查模型 Bounds 是否含隐藏地面/辅助几何 |
| 桩太粗或太细 | 调整 `StakeDiameterCM`；不要先改 Slot Scale |
| 弦太长或越过连接点 | 检查弦长度轴是否为本地 Z，模型是否包含额外尾部几何，`CordVisual.LocalScale.Z` 是否为 1 |
| 弦太细 | 调整 `CordThicknessCM`；检查源 Bounds 是否含远离主体的顶点 |
| 袋体巨大或极薄 | 将旧实例的 `PouchVisual.LocalScale` 重置为 `(1,1,1)`，再使用 `PouchSizeCM` |
| 全部模型统一偏移 | 检查 Slot Offset；它现在是未缩放的真实厘米值，旧补偿值通常应清零 |
| 预览正常、PIE 异常 | 确认没有旧派生 Blueprint 覆盖 Slot 默认值，并对实例执行 Reset to Default 后重新保存 |
