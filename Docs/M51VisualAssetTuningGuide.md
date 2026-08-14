# M5.1 摆放物与掉落物视觉调参指南

本指南用于在 PIE 中临时调整工作台、熔炉、桥、弹弓槽和四种掉落物的模型大小与贴地高度。弹弓主体沿用现有定值，不在本轮命令范围内。

所有命令只修改模型视觉组件，不移动 Actor 的逻辑锚点，也不修改桥的碰撞盒。所有已挂载的美术模型都使用 XYZ 等比基础缩放；桥只按跨度轴计算一个统一适配比例，不会为贴合碰撞盒而压扁模型。数值对当前 PIE 中已经生成的对象立即生效，之后新生成的同类对象也会使用同一数值。退出 Editor 进程后临时数值会恢复到下表所列的冻结默认值。

## 参数约定

每条视觉命令都使用两个参数：

```text
<ScaleMultiplier> <LocalZOffsetCM>
```

- `ScaleMultiplier`：相对于当前代码基础尺寸的统一缩放倍数，允许范围 `0.01` 到 `20`。
- `LocalZOffsetCM`：模型沿物体局部 Z 轴的位移，单位厘米，允许范围 `-1000` 到 `1000`。在球形地表上，正值通常表示离开地面，负值通常表示沉入地面。
- 示例：`ABTS.M51.Visual.Workbench 1.5 -8` 表示工作台放大到当前基础尺寸的 1.5 倍，并向地面方向下移 8 cm。

## 四种掉落物展示

进入正确关卡的 PIE，打开控制台后执行：

```text
ABTS.M51.Pickup.SpawnShowcase 450
```

该命令以当前玩家为中心，在地面扇形区域一次性生成树枝、石头、木材和植物纤维各一个。`450` 是期望距离，单位厘米；省略时同样默认为 `450`。代码还会强制使用不小于自动拾取半径加 250 cm 的安全距离，并在四个安全地表格都确定后才生成，避免刚出现就被拾取以及只生成一部分。

## 摆放物视觉命令

```text
ABTS.M51.Visual.Workbench 3.0 -30
ABTS.M51.Visual.Furnace 3.0 -25
ABTS.M8.Visual.Bridge 1.5 40
ABTS.M51.Visual.StandardSlot 2.0 0
ABTS.M51.Visual.FinaleSlot 3.0 0
```

- `StandardSlot` 是普通弹弓槽。
- `FinaleSlot` 是太空/终局弹弓槽。
- 桥命令只调整桥模型，桥的权威尺寸和碰撞盒保持不变。

## 掉落物视觉命令

```text
ABTS.M51.Visual.Pickup.Branch 3.0 -25
ABTS.M51.Visual.Pickup.Stone 3.0 -10
ABTS.M51.Visual.Pickup.Wood 3.0 0
ABTS.M51.Visual.Pickup.PlantFiber 4.0 -20
```

`Stone` 对应项目中的 `Gravel` 石头模型资产；命令仍使用游戏物品 ID 的名字 `Stone`。

## 推荐验收流程

1. 启动一个全新的 Editor 进程并进入目标关卡 PIE。
2. 执行 `ABTS.M51.Pickup.SpawnShowcase 450`，确认四种模型都出现且没有立即被拾取。
3. 逐条修改目标命令的缩放与局部 Z 值。命令会立即刷新场景中的同类对象。
4. 对工作台、熔炉、桥、普通槽、太空槽和四种掉落物分别检查比例、贴地情况、遮挡与可辨识度。
5. 执行 `ABTS.Visual.Status`。日志会为九个目标分别打印一条带 `FreezeCommand=` 的可复制结果。
6. 若要形成下一轮默认值，把这九条 `FreezeCommand` 原样发回即可再次冻结。

需要撤销本次 PIE 中全部临时调节、恢复上述冻结默认值时执行：

```text
ABTS.Visual.ResetAll
```

验收时如果某条命令没有匹配到已生成对象，日志中的 `Refreshed=0` 会明确显示；该值仍会应用到之后生成的同类对象。
