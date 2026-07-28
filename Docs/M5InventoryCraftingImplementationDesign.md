# M5：共享物品栏、背包与加工界面

> 状态：M5 已由 M5.1 接续；测试库存和出生点自动工作台/熔炉已移除，真实拾取与放置规则见 [M51WorldItemsPlacementSlingshotDesign.md](M51WorldItemsPlacementSlingshotDesign.md)。
>
> 本阶段只实现共享库存、配方规则、附近站点检测、红鸟加工权限和 UI。地图物品刷新、拾取、工作台/熔炉放置、弹弓槽/桩/弦表现均属于 M5.1。
>
> 导航：[主设计稿](AngryBirdsToSpaceGameDesign.md) · [UI 系统](UISystemDesign.md) · [M4 小队头像 HUD](M4BirdPartyImplementationDesign.md) · [M5.1 世界物品与装配](M51WorldItemsPlacementSlingshotDesign.md) · [M11.0 终局前置收口](M110PreFinaleClosureDesign.md)

## 1. 阶段验收目标

1. 底部显示 8 格快捷栏，按首次获得顺序填充空位。
2. 点击快捷栏或按 K 打开背包/加工界面。
3. 左侧显示全部共享物品，切换鸟不丢失库存。
4. 右侧按可制作、缺材料、缺站点排序配方。
5. 只有红鸟能执行制作。
6. 工作台/熔炉配方只在对应站点范围内解锁。
7. 点击可制作配方弹出数量面板，`--/-/+/++` 正确 Clamp。
8. 点击不可制作配方产生红色加强反馈，Hover 显示缺失原因。

## 2. 编辑器搭建步骤

### 2.1 创建 M5 关卡

1. 在 Content Browser 复制 `/Game/Maps/L_ABTS_M4`。
2. 将副本命名为 `/Game/Maps/L_ABTS_M5`。
3. 打开 `L_ABTS_M5`。
4. 打开 `World Settings`。
5. 将 `GameMode Override` 设置为 `ABTSM5GameMode`。如果列表中有旧的 M4 Blueprint GameMode，不要继续使用它，因为它会覆盖新的 Controller/HUD Class。
6. 保存关卡，关闭并重新打开编辑器一次，确保 CDO 和输入配置为 fresh 状态。

### 2.2 已退役的测试工作台与熔炉

M5 曾在动态出生点附近自动生成工作台和熔炉占位方块；M5.1 已取消该路径。当前规则为：

1. 开局不存在工作台或熔炉。
2. 玩家通过世界拾取取得资源，制作工作台/熔炉组件。
3. 从快捷栏手持组件后，点击合法平缓 Cell 中心完成放置。
4. 放置后的站点仍只阻挡鼠标 `Visibility` 查询，不阻挡 Pawn 移动。
5. 完整规则见 [M51WorldItemsPlacementSlingshotDesign.md](M51WorldItemsPlacementSlingshotDesign.md)。

## 3. 已退役的临时测试库存

M5 初期曾由 `ABTSCraftingSystem` 注入以下测试库存；M5.1 已取消这一路径，日志现在为 `PrototypeSeed=0`，资源全部来自世界拾取：

| 物品 | 数量 |
| --- | ---: |
| 树枝 | 20 |
| 石料 | 15 |
| 植物纤维 | 6 |
| 木材 | 2 |
| 金属部件 | 1 |

该旧版本日志曾包含 `PrototypeSeed=1`；M5.1 现已输出 `PrototypeSeed=0`，避免双重资源来源。

## 4. 当前配方

| 配方 | 站点 | 材料 |
| --- | --- | --- |
| 工作台组件 | 徒手 | 树枝 4、石料 3 |
| 简易弹弓桩 | 工作台 | 树枝 3、石料 2 |
| 简易弹弓弦 | 工作台 | 树枝 2、植物纤维 3 |
| 熔炉组件 | 工作台 | 石料 8、木材 4 |
| 强化弹弓桩 | 熔炉 | 金属部件 4、石料 3 |
| 强化弹弓弦 | 熔炉 | 金属部件 2、植物纤维 4 |
| 太空弹弓桩（一次产出 2 根） | 熔炉 | 金属部件 6、木材 5 |
| 太空弹弓弦 | 熔炉 | 金属部件 2、晶体核心 1 |

配方为当前纵向切片测试值，后续可数据化调参；简易弹弓仍遵守核心约束，以树枝/石料体系为主，不要求先从目标建筑取得木材。`SpaceStakePair + SpaceCord` 合计仍消耗金属部件 8、木材 5、晶体核心 1，与退役的一体式太空弹弓部件总预算相同。

旧 `EABTSItemId::SpaceSlingshotPart` 只保留隐藏枚举值以兼容历史序列化，不再进入默认物品目录和配方目录，`FindRecipe/Craft` 也不得继续接受旧配方。现行枚举、配方 ID 和世界装配合同见 [M11.0](M110PreFinaleClosureDesign.md#5-太空桩太空弦和配方迁移)。

## 5. 操作与视觉验收

1. 启动 PIE，日志应出现：

```text
[ABTS][M5][Inventory] Ready Stacks=0 Hotbar=8 Recipes=8 PrototypeSeed=0
[ABTS][M5] Entry ready=1 StartCell=...
```

2. 底部快捷栏前五格依次显示树枝、石料、植物纤维、木材、金属部件，其余为空。
3. 点击快捷栏或按 K，界面打开，鸟和相机不再响应移动/Orbit 输入。
4. 红鸟主控且靠近工作台时，工作台组件、简易桩、简易弦应排在可制作区域。
5. 熔炉组件因木材不足进入缺材料区域。
6. 开局工作台和熔炉都在范围内；强化组件、太空桩和太空弦应只因材料不足而进入缺材料区域。M5.1 的正式站点放置后可通过远离熔炉验收缺站点区域。
7. Hover 熔炉组件应提示缺少木材；点击后该行短暂闪红。
8. 点击简易弹弓桩，数量初始为 1；`++` 不得超过最大制作量，`--` 不得低于 0。
9. 数量大于 0 时点击 CRAFT，材料减少、产物加入背包；若快捷栏有空位，产物填入最左侧空位。
10. 切换为蓝/黄/黑鸟后打开界面，顶部显示红鸟要求；配方仍按材料/站点排序，但全部不可执行。
11. 点击世界中的工作台或熔炉方块也能打开界面；M5.1 的正式站点距离不足时不会误解锁配方。

## 6. 已知边界

- 当前 UI 使用 Canvas 绘制，无正式纹理或手柄焦点导航。
- 左侧背包已支持鼠标滚轮逐行滚动，并绘制轻量滚动条；背包条目可直接设为手持物品。正式 UMG 阶段需要保持这两个交互语义。
- M5.1 站点的建造合法性和中心位置由 CellTopo 决定；使用范围仍是局部玩家交互距离。
- M5 占位站点不能阻挡角色。`BlockAllDynamic` 会参与 ForceSuspension 的 Pawn Sweep，令角色在方块接触面反复移除速度；当前站点只响应 `Visibility`，以保证左键点击仍然可用。
- 世界点击依赖站点 Actor 的碰撞和 PlayerController Click Events；真实模型接入后须检查模型碰撞不会吞掉点击。

## 7. M5.1 接口边界

M5.1 只需要：

- 拾取成功调用共享库存 `AddItem`；
- 放置/拆除工作台或熔炉生成/销毁站点 Actor；
- 站点 Transform、CellId、邻接关系由 CellTopo 决定；
- 弹弓桩、弹弓弦和槽位读取制作产物，但不修改库存排序与配方判定；
- 关闭 `PrototypeSeed` 测试资源。
