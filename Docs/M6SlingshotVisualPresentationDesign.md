# M6 弹弓视觉表现设计

> 状态：M7.1/M6 共享的弹弓视觉协议与实现说明。本文只描述弹弓桩、双弹弓弦和弹珠袋的表现，不改变 M6 的发射判定、弹道或破坏规则。

## 1. 目标

弹弓在待机、瞄准、拉伸和发射前后都保持完整可读的构型：两个桩固定，左右两根弦分别连接桩顶与弹珠袋两侧，只有袋中心在拉伸时移动。运行时由 `AABTSM51SlingshotCord` 持有两根弦和袋体视觉；旧的横向交互网格仅保留为鼠标点击/可见性查询碰撞，不再渲染。

## 2. 坐标和连接协议

弹弓 Actor 的局部约定为：`+X` 发射方向，`+Y` 为两桩分布轴（A 在负 Y，B 在正 Y），`+Z` 为向上。所有连接参数都是 Actor 局部厘米向量，并在球面上由桩的平均地表法线重建视觉旋转，因此不会把球面径向误当作世界 Z。

`FABTSSlingshotConnectionLayout` 参数位于 `ABTS | M7.1 | Slingshot | Connections`：

- `StakeAConnectionOffsetCM`、`StakeBConnectionOffsetCM`：从自动计算的桩顶向量偏移。
- `RestPouchOffsetCM`：两桩连接点中点到待机袋中心的偏移；默认零向量，即袋位于两桩顶部中间。
- `PouchAConnectionOffsetCM`、`PouchBConnectionOffsetCM`：袋局部左右连接点，默认 `(0,-18,0)` 与 `(0,18,0)`。

## 3. 视觉状态

- 待机：两弦连接桩顶和袋两侧，袋位于中点。
- Ready/Pulling：桩不动，袋按 M6 的鼠标投影位置移动，两根弦每帧按两端点距离重定位和拉伸。
- Flying/Returning：袋和弦恢复为待机构型或按发射状态隐藏，鸟由 M6 控制；发射资格和碰撞逻辑不受视觉组件影响。

## 4. 编辑器参数

在 `AABTSM71PlaceableSlingshotActor` 中配置：

- `Stake`：`Mesh/Material/LocalOffsetCM/LocalRotation/LocalScale`。
- `Cord`：同样的模型槽；缺省使用细圆柱。
- `Pouch`：同样的模型槽；缺省使用非均匀缩放的球体，视觉上为扁平椭圆片。
- `StakeHeightCM`、`StakeDiameterCM`、`CordThicknessCM`：几何基准尺寸。

沿测试台弹弓 Actor 的 Y 轴缩放只改变桩间距；X/Z 缩放不改变间距规则。构造预览和 PIE 生成使用同一套连接计算。

## 5. 模型协议

- 弹弓桩：原点在底部中心，模型向上轴为本地 Z；网格不应包含额外的地面偏移。
- 弹弓弦：原点在长度中心，长度轴为本地 Z，横截面位于 XY；模型默认长度按 100 cm 基准缩放。
- 弹珠袋：原点在袋中心；本地 Y 表示左右连接方向，本地 X 朝发射方向，本地 Z 为袋厚度法线。连接点由 `PouchA/BConnectionOffsetCM` 提供，不依赖模型顶点位置。

替换资产时优先调整对应 Slot 的局部偏移、旋转和缩放，不修改 Actor 的布局参数。只要遵守原点和轴向协议，无模型回退状态下调好的手感可以直接迁移到正式资产。

## 6. 验收

1. 在 M7.1 测试台拖入任一四档弹弓，未配置模型时可看到粗圆柱桩、两根细圆柱弦和扁平袋体。
2. 沿 Y 缩放 Actor，两个桩和两根弦的连接保持正确，袋仍位于顶部中间。
3. 点击弦进入 M6，拖动鼠标时只有袋移动，两根弦端点连续跟随且不出现横向旧弦。
4. 松开鼠标后视觉不阻挡鸟的碰撞和轨迹；返回待机后双弦和袋重新显示。

## 7. 排错

袋偏离中点：检查 `RestPouchOffsetCM` 和 Actor 局部轴；弦接反：交换 `PouchA/BConnectionOffsetCM`；桩插地：确认桩模型原点在底部；弦扭转：确认弦长度轴为本地 Z，并只用 Cord Slot 修正旋转。
