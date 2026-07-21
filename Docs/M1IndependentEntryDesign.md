# M1：独立入口实现设计

> 状态：已实现 C++，等待编辑器创建独立地图并进行视觉验收。主设计见 [AngryBirdsToSpaceGameDesign.md](AngryBirdsToSpaceGameDesign.md)。

## 1. 目标与边界

M1 提供一个完全独立的 ABTS 游戏入口：第三人称绯翼占位角色、可移动相机、基础 HUD、专属 `GameMode` 与 `PlayerController`。它不依赖、也没有接入回合、阵营、棋子、Cell 点击移动、NPC、资源、建造、PCG 或球面地形逻辑。

当前工程未包含这些旧系统的源码，因此独立性通过模块边界与入口装配保证：后续 M2/M3/M4 分别增加 Planet、PCG、Party 等模块或组件，而不将它们揉入 M1 类。

## 2. 模块与类职责

```text
AngryBirdsToSpace（项目入口模块）
    -> ABTSRuntime（可复用的游戏 Runtime）
         -> Game / AABTSM1GameMode
         -> Player / AABTSM1PlayerController
         -> Player / AABTSM1BirdCharacter
         -> UI / AABTSM1HUD
```

| 位置 | 职责 |
| --- | --- |
| `ABTSRuntime` | 未来 ABTS 玩法的独立 Runtime 模块，当前不依赖旧项目模块。 |
| `AABTSM1GameMode` | 装配 M1 Pawn、Controller 和 HUD；不放置玩法规则。 |
| `AABTSM1PlayerController` | 本地玩家启动时切到纯游戏输入模式。 |
| `AABTSM1BirdCharacter` | 临时球形绯翼、Character 移动、弹簧臂与第三人称相机。 |
| `AABTSM1HUD` | 无蓝图依赖的状态标题、控制提示和中心准星。 |

每个源文件只包含对应的单一职责，均远小于 600 行。

## 3. 编辑器操作

### 3.1 编译并加载原生类

1. 关闭 Unreal Editor。
2. 在 Visual Studio 中选择 `Development Editor | Win64`，编译 `AngryBirdsToSpaceEditor`；或使用项目根目录的生成文件执行同一目标。
3. 从 `.uproject` 启动编辑器。若编辑器提示模块版本不匹配，选择重新编译。

### 3.2 创建 M1 地图

1. 在 Content Browser 创建 `/Game/Maps`。
2. 选择 **File > New Level > Basic**，保存为 `/Game/Maps/L_ABTS_M1`。
3. Basic 模板已有可行走地面、天空和光源；保留它们。M1 不生成临时地面。
4. 放置一个 `Player Start`，位置设为 `(0, 0, 150)`，旋转为 `(0, 0, 0)`。
5. 打开 **World Settings**：将 **GameMode Override** 设置为 `ABTSM1GameMode`。
6. 打开 **Project Settings > Maps & Modes**：确认 Default GameMode 为 `ABTSM1GameMode`；将 **Game Default Map** 和 **Editor Startup Map** 都设置为 `L_ABTS_M1`。
7. 保存地图。

### 3.3 可选：替换占位球为绯翼资产

1. 创建 `BP_ABTSM1BirdCharacter`，父类选择 `ABTSM1BirdCharacter`。
2. 仅替换 `BirdVisual` 的 Static Mesh 与材质；可调整其相对位置/缩放以贴合 Capsule。
3. 禁止删除、重命名或重新附着 `CapsuleComponent`、`BirdVisual`、`CameraBoom`、`FollowCamera`。这些是 C++ Default Subobject，修改会造成 Blueprint 序列化兼容风险。
4. 若需要使用该 Blueprint，创建 `BP_ABTSM1GameMode`（父类 `ABTSM1GameMode`），仅设置 **Default Pawn Class** 为 `BP_ABTSM1BirdCharacter`，再将地图 Override 指向此 GameMode Blueprint。

## 4. 验收标准

1. 在 `L_ABTS_M1` 使用 PIE 启动后，玩家以第三人称控制球形绯翼占位角色；WASD 行走，鼠标转动镜头。
2. 左上显示 `ANGRY BIRDS TO SPACE | M1 Independent Entry` 和控制提示，画面中央有准星。
3. Output Log 在本次 PIE/Standalone 进程出现以下日志：

```text
[ABTS][M1] Independent entry ready. No turn, faction, Cell-click, or NPC gameplay is loaded.
[ABTS][M1] Player controller initialized.
```

4. Standalone 启动新进程后仍满足前 3 项。
5. 在 Class Viewer 中可见 `ABTSRuntime` 下的 M1 类；它们不引用 `TerraCivilization` 或任何旧 Gameplay 模块。

## 5. 排错

| 现象 | 检查和修复 |
| --- | --- |
| 进入默认 OpenWorld 或错误地图 | 在 Maps & Modes 将 Game Default Map 改为 `L_ABTS_M1`。 |
| 出现默认 Pawn 或没有 HUD | 检查 World Settings 的 GameMode Override 是否为 `ABTSM1GameMode` 或其指定 Blueprint。 |
| WASD/鼠标无效 | 查看 Project Settings > Input 的 `ABTS_MoveForward`、`ABTS_MoveRight`、`ABTS_Turn`、`ABTS_LookUp` 映射；重启编辑器后再测。 |
| 角色生成在空中或地面下 | 校正 Player Start 的 Z；Basic 地面通常位于 Z=0。 |
| 更换 Blueprint 后相机失效 | 恢复原生默认组件名称、父子关系与 CameraBoom/FollowCamera 附着关系。 |

## 6. 后续接口

M2 将新增独立 Planet/Surface 模块和球面出生定位服务；M1 Character 只消费一个最终 Spawn Transform，不在自身内部计算 `CellTopo`、球面高度或重力。M4 的鸟群状态将放入单独 `PartyComponent`，不扩充 M1 HUD 或 PlayerController 为队伍管理器。
