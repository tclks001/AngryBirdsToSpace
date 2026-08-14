# ABTS M3/M7/M11 多工作树协作与集成规范

> 编码：UTF-8，简体中文。
> 状态：多工作树共同基线规范。
> 适用范围：`M3 PCG 地图`、`M7 建筑`、`M11 终局` 三条并行开发线，以及作为集成工作树的原始仓库。

## 1. 目标与拓扑

本项目采用“一个集成工作树 + 三个功能工作树”的固定职责拓扑。功能工作树的物理目录可以由 Codex 托管，目录名不参与职责判定：

```text
C:\workspace\AngryBirdsToSpace       原始仓库；唯一集成工作树
├─ master / integration-candidate    只做基线、合并、联合验收
│
├─ %CODEX_HOME%\worktrees\<opaque-id>\AngryBirdsToSpace   feature/m3-pcg-map
├─ %CODEX_HOME%\worktrees\<opaque-id>\AngryBirdsToSpace   feature/m7-buildings
└─ %CODEX_HOME%\worktrees\<opaque-id>\AngryBirdsToSpace   feature/m11-finale
```

Codex 默认把托管工作树放在 `%CODEX_HOME%\worktrees`；当前 Windows 安装对应
`C:\Users\mingyangwu\.codex-official\worktrees`。其中的 `<opaque-id>` 由 Codex 分配，不得根据目录 ID 猜测 M3/M7/M11 身份。身份只由 `git branch --show-current` 与上表中的专属分支决定。若以后在 Codex 设置中修改 Worktree root，本规范仍然有效。

只保留上述三个功能职责，不再新增第四个集成工作树。原始仓库始终承担集成职责。Codex 托管工作树可能随会话归档或清理而被回收；阶段成果必须及时形成分支提交并推送，不能把未提交工作长期只留在托管目录。长期会话应固定/保留，或改用 Codex permanent worktree。

工作树隔离的是工作目录、索引、当前分支以及各自的 `Binaries/`、`Intermediate/`、`Saved/`；它们仍共享同一个 Git 对象库和引用数据库。因此：

- 一个工作树创建的提交会立即对其他工作树可见，但不会自动进入其他工作树的文件或分支。
- 每个工作树必须绑定不同分支；不得让两个会话同时修改同一工作树。
- UE 生成文件位于各自工作树，不应提交，也不应跨工作树复制。
- “可以自动合并”仅意味着互不重叠的文本改动可由 Git 自动完成；它不意味着可以自动裁决 C++ 语义冲突或合并 `.uasset/.umap`。

本规范不改变 M3、M7、M11 已通过验收的玩法或数值逻辑，只固定并行开发边界、消费契约和集成程序。

## 2. 共同 base-commit

三个功能工作树必须从同一个、已通过构建和契约测试的 base-commit 创建。该提交包含：

- `Source/ABTSRuntime/Public/Contracts/ABTSWorldGenerationContracts.h`
- `Source/ABTSRuntime/Private/Contracts/ABTSWorldGenerationContracts.cpp`
- `Source/ABTSRuntime/Private/Contracts/ABTSWorldGenerationContractAutomationTests.cpp`
- `Source/ABTSRuntime/Public/Contracts/ABTSM11PresentationAcceptanceContract.h`
- `Source/ABTSRuntime/Private/Contracts/ABTSM11PresentationAcceptanceContract.cpp`
- `Source/ABTSRuntime/Private/Contracts/ABTSM11PresentationAcceptanceManifest.cpp`
- `Source/ABTSRuntime/Private/Contracts/ABTSM11PresentationAcceptanceContractAutomationTests.cpp`
- M3 的只读契约导出适配器
- M7/M11 的契约消费入口
- 本规范及项目工作流中的入口链接

该 base 至少必须通过 Development Editor 全链接、`ABTS.Contracts.WorldGeneration`、M7 Routing/DAG 回归、`ABTS.M110`、`ABTS.M11A`、`ABTS.M11B.Unit` 与 `ABTS.M11B.Runtime`。如果求解器、冻结布局或认证身份发生变化，还必须重跑 M11-B 两项慢速认证；若未变化，可引用此前已经批准且 Hash 未变的慢测证据。最终交接必须列出本次实际过滤器、成功测试数和唯一日志。

Git 提交无法在自身内容中记录自己的最终 SHA，因此 base-commit 的精确 SHA 由完成本任务时的交接消息给出。创建工作树时必须在 Codex UI 中选择包含该 SHA 的起始分支，并在创建后用 `git rev-parse HEAD` 核对；不要只凭会继续移动的分支名推定基线正确。

使用 Codex 的“在新工作树中继续”时，Codex 默认在 detached HEAD 上创建托管工作树。创建后，在对应会话使用“Create branch here”绑定尚不存在的专属分支；若分支已经存在，则不要重复创建，改在该工作树终端执行 `git switch <已有专属分支>`。Git 不允许同一分支同时被两个工作树检出。

当前三个职责与分支固定为：

| 职责 | 专属分支 |
| --- | --- |
| M3 | `feature/m3-pcg-map` |
| M7 | `feature/m7-buildings` |
| M11 | `feature/m11-finale` |

需要自行创建 permanent worktree 时，才在原始仓库的 PowerShell 中执行：

```powershell
$IntegrationRoot = 'C:\workspace\AngryBirdsToSpace'
$BaseCommit = '<交接消息给出的完整 base-commit SHA>'

git -C $IntegrationRoot status --short
git -C $IntegrationRoot rev-parse --verify "$BaseCommit^{commit}"
git -C $IntegrationRoot worktree list

git -C $IntegrationRoot worktree add `
  -b feature/m3-pcg-map `
  'C:\workspace\AngryBirdsToSpace-M3' `
  $BaseCommit

git -C $IntegrationRoot worktree add `
  -b feature/m7-buildings `
  'C:\workspace\AngryBirdsToSpace-M7' `
  $BaseCommit

git -C $IntegrationRoot worktree add `
  -b feature/m11-finale `
  'C:\workspace\AngryBirdsToSpace-M11' `
  $BaseCommit

git -C $IntegrationRoot worktree list
```

创建前，`git status --short` 必须没有输出。如果某个目标目录、分支或工作树已经存在，先停止并用 `git worktree list --porcelain` 查明归属。只有在绝对路径已经不存在且条目标记为 `prunable` 时，才允许由集成工作树执行 `git worktree prune --verbose` 清理失效元数据；不得删除仍存在的工作树目录、强制让同一分支被两个工作树检出或用 `--ignore-other-worktrees` 绕过保护。

## 3. 稳定消费契约

### 3.1 契约方向

跨阶段数据固定为单向流：

```text
M3 已接受的生成结果
  ├─ TryExportBuildingGenerationContract()
  │      └─ FABTSBuildingGenerationContract ──> M7
  └─ TryExportFinaleWorldContract()
         └─ FABTSFinaleWorldContract ─────────> M11
```

契约采用只读值快照，不向消费者暴露 M3 数组、配置对象或可变 UObject 引用。

| 契约 | 生产方 | 消费方 | 保证 |
| --- | --- | --- | --- |
| `FABTSGeneratedWorldIdentity` | M3 | M7/M11 | 契约版本、世界种子、生成器版本、Attempt、世界已接受 |
| `FABTSBuildingGenerationContract` | M3 | M7 | 有序建筑站点、稳定 ID、明确用途、确定性建筑种子、最终施工台与局部基底 |
| `FABTSFinaleWorldContract` | M3 | M11 | 主星半径与认证 Finale Local Frame；明确不包含 M9 |

### 3.2 不变量

- M3 只在 `PlanetReady && PCGSummary.bAccepted` 后导出契约。
- 已接受的当前世界必须导出非空建筑站点集合；空集合对 M7 fail closed。
- 快照版本未知、站点重复、坐标基底非法或 Finale Frame 非法时必须 fail closed。
- M7 必须保持 M3 导出的站点顺序，不按坐标、ID 或难度再次排序。
- `DeterministicSeed` 由 M3 适配器按基线公式一次性生成；M7 不再从 M3 公共字段重建种子。
- `FinaleLaunchReserved` 只保留终局施工台，M7 不在该站点生成普通建筑。
- M11 只消费主星和 Finale Local Frame；M9 练习卫星不得进入终局契约或四体求解。
- M7 仍可持有 `AABTSM3Planet*` 作为实时球面支撑/纬经度调试适配器。这是当前唯一保留的运行时几何例外，不得借此读取 TaskGraph、PCG 配置或建筑站点原始数组。
- M7 当前 Blueprint Profile 仍以历史 `EABTSM3TaskType` 作为序列化键；它是兼容缝，不是新的跨工作树数据通道。未经单独资产迁移，不得修改该 UPROPERTY 的类型或枚举值。
- `EABTSM3TaskType` 现有项的数值、顺序和语义已经冻结；不得删除、重命名、复用或在中间插入。新增项只能尾部追加，并必须完成 Blueprint 资产与契约回归。
- `Unsupported` 是合法但应由旧 M7 跳过的向前兼容用途；不得把未来 M3 站点静默解释成现有建筑类型。

### 3.3 契约变更程序

`Public/Contracts/**`、`Private/Contracts/**` 与 `Public/World/ABTSM110FinaleTypes.h` 为集成工作树所有。三个功能工作树不得直接修改。

以下符号即使声明位于某个功能工作树所有的文件中，也属于冻结契约面，不能由文件所有者单方面改签名、顺序语义或失败策略：

- `AABTSM3Planet::TryExportBuildingGenerationContract`
- `AABTSM3Planet::TryExportFinaleWorldContract`
- `AABTSM11FinaleSystem::InitializeFromWorldContract`
- M3 适配器中的 Purpose、Seed、SiteId、Encounter/Difficulty 投影规则

确实需要新增跨阶段字段时：

1. 功能工作树先提交需求说明：生产方、消费方、单位、有效域、默认值、失败策略、确定性/序列化要求。
2. 集成工作树优先以向后兼容方式加入 vNext：保留 v1 可用、追加新字段/类型和双版本自动化，使 `master` 始终为绿。
3. 三个功能分支合并该 `master`，分别在各自所有文件中实现生产或消费适配。
4. 集成候选把生产方和所有消费者原子合并，跑共同门禁后才切换默认版本。
5. 旧版清理另开集成提交，且只能在所有分支已迁移后进行。

如果无法提供同时支持新旧版本的过渡层，则在专门的 contract candidate 上协调生产方、消费者和测试的一次性原子集成；半套新版本不得进入 `master`。

禁止在某个功能分支中临时修改契约，然后要求其他两个功能分支“随后适配”。这会重新形成共享热点并使共同基线失效。

## 4. 职能与文件所有权

### 4.1 M3 工作树

职责：

- Task Graph/CellTopo、道路、水网、任务间距与地图引导。
- 地形类型分布、连续球面表现、HISM、建筑施工台和 Finale Frame 的生产。
- M3 独立地图与 PCG 验证。
- 只通过稳定契约向 M7/M11 输出数据。

专属文本范围：

- `Source/ABTSRuntime/Public/PCG/ABTSM3*`
- `Source/ABTSRuntime/Private/PCG/ABTSM3*`
- `Source/ABTSRuntime/Public/Terrain/ABTSM3*`
- `Source/ABTSRuntime/Private/Terrain/ABTSM3*`，但明确排除集成所有的 `ABTSM3WorldContractAdapter.cpp`
- `Source/ABTSRuntime/Public/Game/ABTSM3GameMode.h`
- `Source/ABTSRuntime/Private/Game/ABTSM3GameMode.cpp`
- `Docs/ABTSTaskGraphPCGDesign.md`
- `Docs/M3PCGMapImprovementPlan.md`
- `Docs/M3TaskGraphTerrainPresentationDesign.md`
- `Docs/M3WorktreeTroubleshootingLog.md`

专属二进制资产：

- `Content/Blueprints/BP_ABTSM3Planet.uasset`
- `Content/Maps/L_ABTS_M3.umap`
- `Content/Materials/M_ABTS_M3_SDFTerrain.uasset`
- 后续新建的 `Content/M3/**`

M3 不得修改 M7 建筑算法、M11 求解/终局逻辑或稳定契约。若 PCG 新增建筑语义，先通过契约变更程序提出，而不是让 M7 读取新的 M3 原始数组。

### 4.2 M7 工作树

职责：

- M7/M7.1/M7.3 建筑材料、DAG2.3 生成、结构稳定性、弱点、破坏与建筑门禁。
- 消费 `FABTSBuildingGenerationContract`，把站点用途映射到 M7 自有 Profile。
- M7 独立地图、平面测试台与实时 Chaos 验收。

专属文本范围：

- `Source/ABTSRuntime/Public/Building/ABTSM7*`
- `Source/ABTSRuntime/Public/Building/ABTSM73*`
- `Source/ABTSRuntime/Private/Building/ABTSM7*`
- `Source/ABTSRuntime/Private/Building/ABTSM73*`
- `Source/ABTSRuntime/Public/Game/ABTSM7GameMode.h`
- `Source/ABTSRuntime/Private/Game/ABTSM7GameMode.cpp`
- `Source/ABTSRuntime/Public/Game/ABTSM71PhysicsTestGameMode.h`
- `Source/ABTSRuntime/Private/Game/ABTSM71PhysicsTestGameMode.cpp`
- `Source/ABTSRuntime/Private/Game/ABTSM7TaskGraphDAG23AutomationTests.cpp`
- `Source/ABTSRuntime/Public/TestStage/ABTSM71*`
- `Source/ABTSRuntime/Private/TestStage/ABTSM71*`
- `Docs/M7*.md`
- `Docs/M71*.md`
- `Docs/M73*.md`

专属二进制资产：

- `Content/Blueprints/BP_ABTSM7GameMode.uasset`
- `Content/Blueprints/BP_ABTSM7BuildingMaterialSystem.uasset`
- `Content/Blueprints/BP_ABTSM71PlanarPhysicsTestGameMode.uasset`
- `Content/Maps/L_ABTS_M7.umap`
- `Content/Maps/PlanarPhysicsTestMap.umap`
- `Content/StaticMesh/BrickMaterials/**`
- 后续新建的 `Content/M7/**`

M7 不得修改 M3 站点生成规则来“配合建筑”，也不得从 `AABTSM3Planet` 重新读取 `WorldSeed`、`BuildingPadSettings` 或 `BuildingSpawnSites`。需要不同站点条件时走契约变更程序。

### 4.3 M11 工作树

职责：

- M11-A 纯数据引力求解、M11-B 认证布局，以及后续 M11 HUD、稳定器、权威轨迹播放和终局演出。
- 消费 `FABTSFinaleWorldContract`，在终局局部坐标中工作。
- 三颗表现行星、UFO 及 M11 独立地图/资产。

专属文本范围：

- `Source/ABTSRuntime/Public/Game/ABTSM11*`，排除 `ABTSM110*`
- `Source/ABTSRuntime/Private/Game/ABTSM11*`，排除 `ABTSM110*`
- `Source/ABTSRuntime/Public/World/ABTSM11*`，排除 `ABTSM110*`
- `Source/ABTSRuntime/Private/World/ABTSM11*`，排除 `ABTSM110*`
- 后续明确命名为 `ABTSM11*` 的 UI/Player/Slingshot 文件
- `Docs/M11AGravityAssistSolverDesign.md`
- `Docs/M11BFinaleLayoutCertificationDesign.md`
- `Docs/M11GravityAssistAlgorithmPrevisualization.md`
- `Docs/M11WorktreeTroubleshooting.md`
- 后续 `Docs/M11[A-Z]*` 阶段详稿；明确排除 `M110PreFinaleClosureDesign.md` 和共享总设计

专属二进制资产：

- `Content/Blueprints/BP_ABTSM11GameMode.uasset`
- `Content/Maps/L_ABTS_M11.umap`
- `Content/StaticMesh/UFO/**`
- `Content/Destruction/GeometryCollections/BP_UFOPresentation.uasset`
- `Content/Destruction/GeometryCollections/GC_UFO_Broken.uasset`
- 后续三颗助推行星及终局专属资产统一新建在 `Content/M11/**`

M11 不得修改 M3 世界坐标或生成规则来适配终局，不得扫描 M9 Actor，不得把 M9 引力接入终局。需要额外世界信息时走契约变更程序。

### 4.4 集成工作树

集成工作树专属：

- `Source/ABTSRuntime/Public/Contracts/**`
- `Source/ABTSRuntime/Private/Contracts/**`
- `Source/ABTSRuntime/Private/Terrain/ABTSM3WorldContractAdapter.cpp`
- `Source/ABTSRuntime/Public/World/ABTSM110FinaleTypes.h`
- `Source/ABTSRuntime/Private/World/ABTSM110FinaleTypes.cpp`
- `Source/ABTSRuntime/Private/World/ABTSM110AutomationTests.cpp`
- `Source/ABTSRuntime/Public/Contracts/ABTSM11PresentationAcceptanceContract.h`
- `Source/ABTSRuntime/Private/Contracts/ABTSM11PresentationAcceptanceContract.cpp`
- `Source/ABTSRuntime/Private/Contracts/ABTSM11PresentationAcceptanceManifest.cpp`
- `Source/ABTSRuntime/Private/Contracts/ABTSM11PresentationAcceptanceContractAutomationTests.cpp`
- `.uproject`、`Config/**`、`*.Build.cs`、`*.Target.cs`
- `AGENTS.md`
- `Docs/ABTSProjectWorkflow.md`
- `Docs/ABTSMultiWorktreeDevelopmentGuide.md`
- `Docs/AngryBirdsToSpaceGameDesign.md`
- `Docs/M110PreFinaleClosureDesign.md`
- `Docs/M110PresentationAcceptanceContract.md`
- `Docs/DevelopmentTroubleshooting.md`
- `Docs/OpeningAndFinaleCinematicDirection.md`
- `Content/Maps/Test.umap`
- 未在上文明确分配的共享 Blueprint、鸟、弹弓、物品、音频、Niagara 和材质资产

以下文件是已知的跨阶段热点，默认同样由集成工作树持有：

- `Source/ABTSRuntime/Public/Slingshot/ABTSM6SlingshotSystem.h`
- `Source/ABTSRuntime/Private/Slingshot/ABTSM6SlingshotSystem.cpp`
- `Source/ABTSRuntime/Private/Slingshot/ABTSM6StartupPhysicsWarmup.cpp`
- `Source/ABTSRuntime/Private/World/ABTSM51WorldActors.cpp`
- `Source/ABTSRuntime/Private/World/ABTSM51WorldSystem.cpp`
- `Source/ABTSRuntime/Private/World/ABTSM51WorldSlingshotSlots.cpp`
- `Source/ABTSRuntime/Private/Game/ABTSM51GameMode.cpp`
- `Source/ABTSRuntime/Public/World/ABTSM51PreviewFinaleFrame.h`
- `Source/ABTSRuntime/Private/World/ABTSM51PreviewFinaleFrame.cpp`
- `Source/ABTSRuntime/Private/World/ABTSM51PreviewFinaleFrameAutomationTests.cpp`
- Crafting/Inventory 中的共享物品枚举、目录与配方

`ABTSM110AutomationTests.cpp` 虽位于 `World/` 且包含 M3 的 103 Seed 测试，仍归集成工作树所有；M3/M11 都不能单方面修改。

M7 负责建筑 Actor 自身的注册、验证与状态输出；M6 StartupPhysics 的共同门禁和世界就绪时序仍是集成所有。修改后者必须走共享热点流程，不能以“M7 验收需要”为由越界。

### 4.5 未列出的文件

未列出的文件不是“谁先改谁拥有”。修改前先用下列顺序判定：

1. 文件名是否有唯一阶段前缀。
2. 是否包含两个或更多阶段的接口/验收。
3. 是否为地图、Blueprint、DataAsset 等不可合并资产。
4. 是否会改变另一工作树的编译或运行入口。

第 2–4 项任一为“是”，默认归集成工作树；需要在本规范中补充所有权后才能修改。

## 5. 二进制资产红区

当前仓库没有为 `.uasset/.umap` 配置 Git LFS 锁。Git 不能可靠合并 UE 二进制资产，因此执行单一写入者规则：

- 同一个二进制资产在一个集成周期内只能由一个工作树修改。
- 修改资产前先检查 `git status --short` 和所有权表。
- 专属阶段地图只在对应工作树中编辑；共同验收地图 `Content/Maps/Test.umap` 只在集成工作树编辑。
- 不用 `ours/theirs`、十六进制编辑、复制覆盖或 Blueprint 文本导出来“解决”二进制冲突。
- 遇到二进制冲突时中止合并，由资产所有者在最新 `master` 上重新打开 UE Editor，重放改动并重新提交。
- 保存 Blueprint 后检查是否意外连带修改父 Blueprint、地图、重定向器或默认对象；只提交任务所需资产。

## 6. 每个会话的启动检查

每个 Codex/人工会话开始时，必须先完整阅读根目录 `AGENTS.md` 和本规范，再执行：

```powershell
git rev-parse --show-toplevel
git branch --show-current
git status --short
git worktree list
```

然后在会话中明确：

- 当前工作树：M3、M7、M11 或 Integration。
- 本次允许修改的文件范围。
- 本次独立验收门槛。
- 是否触及稳定契约、共享热点或二进制红区。

如果顶层目录或分支与预期不符，立即停止。不要在工作树内用 `git switch` 切换到别的功能分支，也不要让会话“顺便”修改另一个工作树。

Codex 托管目录中的数字 ID 是透明实现细节；会话不得把
`C:\Users\mingyangwu\.codex-official\worktrees\<opaque-id>` 写入代码、配置、资产引用或交接契约。构建和测试脚本在运行时通过 `git rev-parse --show-toplevel` 获取当前工作树绝对路径。

正在运行的 Codex 会话不会因为另一个工作树提交了规范就自动重新阅读。共享规范更新合并后，用户或集成者必须在每个会话明确要求其重新读取 `AGENTS.md` 和本规范，然后才能继续工作。

### 6.1 电脑、Editor 与 PIE 操作授权边界

“实现功能”“编译验证”“给出 PIE 验收方案”不等于授权会话控制用户电脑或图形界面。除非用户在当前任务中明确要求 Codex 代为控制电脑、操作 Unreal Editor 或执行可见 PIE，否则：

- 不得调用 GUI/电脑控制工具打开、切换、点击或关闭 Unreal Editor、Visual Studio、Content Browser、Blueprint Editor、PIE 窗口或其他桌面应用；也不得用命令行绕过本条启动图形化 Editor。
- 可以在任务授权范围内读取文件、修改代码/文档、运行 Git、使用命令行 `Build.bat` 编译，以及运行明确要求的 unattended/NullRHI 自动化；这些操作不自动扩展为可见 Editor/PIE 权限。
- 需要资产、Blueprint、地图或视觉验收时，只提供严格的 Editor/PIE 操作步骤、地图/GameMode、启动参数或 CVar、日志标志、视觉标准和失败标准，并将状态写为“代码/自动化完成，待用户 PIE 验收”。
- 用户说“我来验收”“之后在 PIE 确认”或只询问“应该如何验收”时，明确表示验收由用户执行；不得自行启动 Editor。
- 只有“请控制电脑打开 Editor 并验收”“请代为执行 PIE”等当前任务中的明确指令才授予 GUI 操作权；历史任务中的一次授权不延续到后续任务。
- 即使已获 GUI 授权，也只能操作当前任务、当前工作树的应用和资产；不得关闭或干预其他工作树、其他项目或用户已有进程。

### 6.2 会话身份与共性误判防护

开始诊断前先区分“工作树/二进制身份、证据层、运行时权威”三个维度：

1. 工作树身份以 Git root、branch、status 和 `.uproject` 绝对路径为准；目录数字 ID、进程名和旧日志都不构成身份。
2. 编辑器预览、Preview/Test Candidate、生产消费、NullRHI 数据合同、实时 Chaos、SceneCapture 像素与可见 PIE 是不同证据层，不能互相替代。
3. 日志应保存 Seed、算法/版本、Profile、Candidate/Result Hash、Authority 和明确拒绝原因；先比较身份是否一致，再比较画面、性能或手感。
4. 沿完整链路寻找第一个缺失证据，不从最终截图直接断言上游没有执行。失败结果必须 fail closed，不得回退旧布局、发布半成品或保留隐形碰撞。

Windows 当前启用 `core.autocrlf=true` 和 `core.ignorecase=true`。因此：

- 禁止只改变文件名大小写；确需改名时由集成工作树执行两步显式重命名。
- 禁止无意义的全文件换行转换。
- 提交前必须运行 `git diff --check`。

## 7. 日常开发与提交

功能工作树的推荐节奏：

```powershell
git status --short
git diff --name-only master...HEAD
git diff --check

git add <明确列出的本工作树文件>
git diff --cached --stat
git diff --cached
git commit -m "<阶段>: <单一可验收结果>"
```

规则：

- 不使用 `git add .`，避免把 UE 自动保存、临时资产或其他会话文件带入提交。
- 一个提交只包含一个可回滚、可验收的逻辑单元。
- 本地 WIP 提交必须明确标识且不得交接；任何推送为候选或提供给集成者的 SHA 必须能编译，阶段交接 SHA 还必须通过该阶段独立自动化。
- 不把 `Binaries/`、`Intermediate/`、`Saved/`、Derived Data 或绝对日志路径提交。
- `.gitignore` 当前忽略 `Scripts/*`；未来需要纳入版本控制的集成工具放在 `Tools/`，并由集成工作树创建。
- 不使用仓库级共享 `git stash` 作为会话交接手段。未完成内容保留在当前功能工作树等待原会话继续；未通过编译/测试的提交可以存在，但不得作为交接 SHA。
- 已推送或已交接的功能提交不 rebase、不改写历史；后续用新提交修正。

### 排错记录闭环

三个功能工作树必须把排错文档作为开发产物持续维护，而不是等集成时依靠会话记忆补写：

| 工作树 | 持续追加的原始账本 |
| --- | --- |
| M3 | [M3WorktreeTroubleshootingLog.md](M3WorktreeTroubleshootingLog.md) |
| M7 | [M7WorktreeTroubleshooting.md](M7WorktreeTroubleshooting.md) |
| M11 | [M11WorktreeTroubleshooting.md](M11WorktreeTroubleshooting.md) |

- 新出现且可复现的故障、被证明错误的修复假设、跨工作树分诊边界、假绿灯和新的验收门，应在功能提交或紧邻的文档提交中记录；普通拼写错误和立即修正的局部编译错误不必单独立项。
- 每条至少包含“现象—根因—最终处理—防回归验证”，并标明状态、修复所有权、fresh 日志/自动化/PIE 等证据。仍是推断时必须标为开放，不得伪装成已确认根因。
- 功能交接必须列出本轮新增或更新的排错 ID；功能工作树只更新自己的账本，不直接修改共享 [DevelopmentTroubleshooting.md](DevelopmentTroubleshooting.md)。
- 集成工作树合并阶段成果时，必须比较总文档中记录的上次摘录基线与三份账本最新提交，去重后把稳定、跨阶段可复用的结论提炼到总文档，并更新摘录基线。
- 提炼完成后保留三份原始账本及其历史；不得以“已经汇总”为由删除、清空或停止维护。Markdown-only 摘录无需 UE 编译。

功能分支首次形成可恢复提交后，由该工作树推送自己的分支：

```powershell
git push -u origin feature/m3-pcg-map
# M7/M11 工作树分别替换为自己的分支名。
```

功能工作树不得推送 `master`。

### 同步 master

功能分支只合并 `master`，不直接互相合并：

```powershell
git fetch origin
git merge --no-edit master
```

若本地 `master` 尚未包含远端最新集成提交，应先由原始集成工作树更新 `master`。功能工作树不得替集成工作树移动 `master`。

### 同步共享规范与集成所有文件

`AGENTS.md`、本规范、稳定契约、共享默认绑定或共同资产只能在集成工作树修改。同步步骤固定为：

1. 相关功能会话完成当前命令、编译或测试，形成显式 checkpoint 提交；不使用共享 `stash`。
2. 集成工作树在干净 `master` 上形成职责单一的共享提交，并记录精确 SHA。
3. 功能工作树先用 `git log --oneline HEAD..master` 审阅将要进入本分支的全部集成提交，确认可接收后执行 `git merge --no-edit master`。若用 `git merge <共享提交 SHA>` 固定同步终点，仍会纳入该 SHA 的全部未合并祖先，不能把它理解为只复制一个补丁。
4. 合并后运行 `git merge-base --is-ancestor <共享提交 SHA> HEAD` 核实共享提交已经进入当前分支。
5. 当前 Codex 会话重新读取 `AGENTS.md` 和本规范，再继续原任务。

因为所有 worktree 共享同一个本地 Git 对象库与 `master` 引用，不使用 `git pull master` 从集成目录“拉取”。已推送或已交接的功能分支用 merge 接收共享提交，不通过 rebase 改写历史。仅修改 Markdown/`AGENTS.md` 时不要求关闭 UE 或重新编译；若共享提交包含 C++、Config、Blueprint、地图、默认资产绑定或资产路径变化，则按受影响门禁重新构建，并在 fresh Editor 中验证。

## 8. 独立验收

### 8.1 唯一引擎基线与通用编译

本项目唯一允许用于编译、UnrealHeaderTool、自动化、Standalone 和 Editor/PIE 的引擎安装为：

```text
C:\Program Files\Epic Games\UE_5.8
```

`.uproject` 中的 `EngineAssociation=5.8` 只表示版本族，不能证明某个 UE 5.8 安装具有相同 BuildId。禁止使用源码版 `C:\workspace\UnrealEngine-5.8.0-release`、其他磁盘上的 UE 5.8、PATH 中偶然命中的 `UnrealEditor.exe`、Visual Studio 当前选择的其他引擎或从别的工作树复制来的 DLL。命令必须从上述绝对 `$EngineRoot` 派生，并在执行前检查目标存在：

每个工作树使用自己的项目绝对路径：

```powershell
$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8'
$ProjectRoot = (git rev-parse --show-toplevel).Trim()
$BuildBat = "$EngineRoot\Engine\Build\BatchFiles\Build.bat"

if (-not (Test-Path -LiteralPath $BuildBat)) {
  throw "Required UE 5.8 installation is missing: $BuildBat"
}

& $BuildBat `
  AngryBirdsToSpaceEditor Win64 Development `
  "-Project=$ProjectRoot\AngryBirdsToSpace.uproject" `
  -WaitMutex -NoHotReload

if ($LASTEXITCODE -ne 0) {
  throw "Development Editor build failed: $LASTEXITCODE"
}
```

默认 Development Editor 成功不能覆盖 Unity 分桶问题。出现以下任一情况时，功能工作树交接前还必须使用同一 `$BuildBat` 追加一次 `-ForceUnity -DisableAdaptiveUnity` 全链接：新增/删除/重命名 `.cpp`、修改匿名命名空间或文件内辅助函数、改变模块源文件集合，或曾出现仅 Unity 才能复现的歧义/重定义。集成候选只要合并了 C++ 源码，就必须执行该 ForceUnity 门。匿名命名空间不会隔离被 Unity 合入同一翻译单元的同名函数，私有帮助函数应使用职责唯一名称。

自动化必须在全新 `UnrealEditor-Cmd` 进程运行，并使用当前工作树独有的绝对日志：

```powershell
$EditorCmd = "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Project = "$ProjectRoot\AngryBirdsToSpace.uproject"
$RunId = Get-Date -Format 'yyyyMMdd-HHmmss'
$Log = "$ProjectRoot\Saved\Logs\<阶段>-$RunId-FreshAutomation.log"

& $EditorCmd $Project `
  -unattended -nop4 -NullRHI -NoSound -NoMessaging `
  "-ExecCmds=Automation RunTests <Filter>;Quit" `
  "-TestExit=Automation Test Queue Empty" `
  "-AbsLog=$Log"

if ($LASTEXITCODE -ne 0) {
  throw "Automation process failed: $LASTEXITCODE"
}
if (-not (Select-String -LiteralPath $Log `
    -SimpleMatch '**** TEST COMPLETE. EXIT CODE: 0 ****')) {
  throw "Automation completion marker is missing: $Log"
}
$PassedCount = (Select-String -LiteralPath $Log `
  -SimpleMatch 'Test Completed. Result={Success}').Count
```

`$PassedCount` 必须等于该过滤器在本次提交中的预期测试数；零匹配、启动失败、复用旧日志或只有进程退出码为 0 都不能算通过。不得用当前已打开 Editor 的 Session Frontend 结果代替全新进程门禁。多个会话不要共用一个 `-AbsLog`。UBT 有全局资源竞争，重型构建和慢速认证默认串行排队，不通过结束其他会话的 Editor/编译进程来“解锁”。

全链接前关闭当前工作树自己的 UE Editor；`-NoHotReload` 不会解除该 Editor 对 DLL 的占用。若 `LNK1104` 只报告文件占用，确认进程归属后关闭本工作树进程，不得结束其他工作树或用户会话。

UE 5.8 的 Live Coding 互斥锁按共享的 `UnrealEditor.exe` 路径判断，另一个项目或另一个工作树的 Editor 也可能造成误拦截。只有在读取进程命令行并确认所有活动 Editor 都未加载“当前工作树”的项目/DLL 后，才可对当前构建追加 `-NoHotReloadFromIDE` 绕过这项过宽检查；若当前工作树自己的 Editor 正在运行，则禁止绕过，必须正常关闭它。交接中应记录使用过该例外。

### 8.2 多工作树编译与运行调度

每个工作树拥有独立的 `Binaries/`、`Intermediate/` 和 `Saved/`，因此使用正确的 `.uproject` 绝对路径时，项目 DLL、日志和自动化报告不会互相覆盖。但三个工作树仍共享同一套 UE 5.8 引擎及系统资源，包括 UBT/UBA 协调、Live Coding 状态、全局/Zen DDC、Shader 编译器、CPU、内存、GPU、音频设备和可能的消息/调试端口。

统一执行以下调度规则：

- 三个工作树一律关闭 Live Coding，不使用 Hot Reload。修改 C++ 后关闭当前工作树自己的 Editor，完整编译，再以 fresh Editor 启动。
- 编译可以由多个会话发起，但必须带 `-WaitMutex`；UBT/UBA 可能把全链接阶段排队。不得把“等待互斥量”当作失败，也不得通过结束其他会话的进程抢锁。
- 重型构建、Cook、全量 Shader 编译和 M11 慢速认证默认串行。
- 轻量 `-NullRHI` 自动化最多两个进程并行。每个进程必须使用自己的 `.uproject`、`Saved/Logs` 和带时间戳的 `-AbsLog`，并使用 `-NoSound -NoMessaging`；零匹配或仅进程返回 0 不算通过。
- 不在其他工作树构建或测试期间清空共享 DDC/Zen、Shader Cache 或引擎级缓存。共享缓存正常并发读取通常安全，但资源竞争导致的超时必须在低负载下复测，不能直接归类为产品缺陷。
- 三个图形化 Editor 可以同时打开各自工程，但不得同时保存同一共享资产。正式可见 PIE、D3D12、Chaos 沉降/hitch soak 与截图验收串行进行；验收时其他 Editor 至少不得处于 PIE、Shader 编译或重型资产加载状态。
- M7 建筑稳定性和定时门槛对帧时敏感。三个 PIE 同时运行所得的 timeout、沉降或稳定性失败只可作为诊断线索，不能作为正式拒收证据。
- 进程、DLL 或端口冲突时，先读取进程命令行和项目绝对路径确认归属；只处理当前任务启动且属于当前工作树的进程。
- 最终联合构建、完整自动化与可见 PIE 只在原始集成工作树的候选分支上串行执行。

推荐调度顺序：

```text
M3 / M7 / M11 各自编码并形成 checkpoint
  → 各自关闭本工作树 Editor
  → Development Editor 构建（允许同时请求，由 -WaitMutex 协调）
  → 轻量 NullRHI 自动化（最多两个并行）
  → M3 PIE → M7 PIE → M11 PIE（正式验收串行）
  → 集成候选联合构建、自动化与 PIE（串行）
```

### 8.3 分工作树门禁

| 工作树 | 自动化门禁 | 视觉/运行时门禁 |
| --- | --- | --- |
| M3 | `ABTS.Contracts.WorldGeneration`、`ABTS.M110.TaskGraphFinaleSeparation` | `L_ABTS_M3`：确定性重生成、道路/任务间距、施工台、M9/Finale 分离 |
| M7 | `ABTS.M7.TaskGraphDAG23ProfileRouting`、`ABTS.M73A`、`ABTS.M73B`、`ABTS.M73B2`、`ABTS.M73DAG` | `L_ABTS_M7` 与 `PlanarPhysicsTestMap`；正式门还需当前 canonical `L_ABTS_M10` 的实时 D3D12/PIE |
| M11 快速 | `ABTS.Contracts.WorldGeneration`、`ABTS.Contracts.M11PresentationAcceptance`、`ABTS.M110`、`ABTS.M11A`、`ABTS.M11B.Unit`、`ABTS.M11B.Runtime` | `L_ABTS_M11`：三行星/UFO、局部坐标、M9 排除、重复进入与 fail closed |
| M11 慢速 | `ABTS.M11B.ConstructiveSearch`、`ABTS.M11B.Certification.FullInputDomain` | 仅在冻结参数/求解器/认证身份变化或阶段交接时运行 |

M7 的 `-benchmark` 固定时间步只作为算法回归。正式建筑门禁仍要求在当前 canonical `L_ABTS_M10` 上进行不带 `-benchmark` 的 D3D12 fresh game 30/60/120 FPS 三档运行，以及一次可见 PIE/hitch soak。每档日志至少满足：

- `BuildingContractSealed Expected=3 Registered=3 SetupRejected=0`
- 三栋必需建筑各自 `IdleValidation ... Accepted=1`
- `WorldReady=1` 晚于全部建筑接受
- 不出现 `BuildingGateRejected`

“编译成功”或 NullRHI 自动化不能替代可见 PIE；可见 PIE 也不能替代确定性自动化。独立验收需要两者中与本阶段相关的部分。

### 8.4 共性故障的强制分诊

| 常见误判 | 强制处理 |
| --- | --- |
| “代码已改但画面没变” | 先核对工作树、项目绝对路径、最终 DLL 时间和 fresh 进程；再区分 Blueprint 序列化默认值、C++ CDO、Preview/Test 与生产消费链。不得直接重复改参数。 |
| “预览/候选正确，所以生产世界已完成” | 明确记录 Authority。Preview/Test 不得被晋升为生产结果；M3/M7/M11 跨消费者用同一 Source/Candidate/Result Hash 对齐。 |
| “生成日志成功但 Actor 消失” | 沿 Spawn/Generated → Idle/Runtime Validation → Gate → WorldReady 查找首次拒绝；事务回滚后的消失不是上游漏生成。 |
| “自动化绿灯，所以视觉/物理已通过” | NullRHI 不证明像素、光照、SceneCapture 或手感；`-benchmark` 不证明实时 Chaos；截图也不证明确定性、完整输入域或事件顺序。按证据层补门。 |
| “同一个 Seed 结果不同” | 同时比较版本、Profile、布局/候选 Hash、派生路径和输入域；禁止共享全局随机流依赖遍历次数，不能只比较 Seed 字面值。 |
| “单次性能门越线/通过” | 保留首次结果和 Oracle/Hash；停止并行重负载后用同一二进制、同一过滤器 fresh 隔离重跑。不得直接忽略首次失败，也不得用缓存热跑覆盖。 |
| “修一下穿透/位置后看起来能用” | 若运行时修复改变了权威几何或 Transform，必须重建下游接触、支撑、碰撞和 Hash；无法证明一致时 fail closed，不能以视觉可站立替代数据一致性。 |

## 9. 交接格式

功能工作树完成后向集成者提供：

```text
Stage:
Branch:
Exact commit SHA:
Base commit SHA:
Owned files changed:
Shared files changed: none
Binary assets changed:
Build command/result:
Fresh automation filters/result/log:
PIE map/result:
Troubleshooting IDs added/updated:
Known limitations:
Integration notes:
```

集成者按精确 SHA 合并，不按仍会移动的分支尖端猜测交付内容。若 `Shared files changed` 不是 `none`，默认拒绝合并，先走契约或共享热点流程。

## 10. 原始仓库中的联合集成

原始仓库不直接在 `master` 上试拼多个未验证分支。使用同一个原始工作树创建临时候选分支：

```powershell
$IntegrationRoot = 'C:\workspace\AngryBirdsToSpace'
$Candidate = 'integration/candidate-<日期或批次>'

git -C $IntegrationRoot switch master
git -C $IntegrationRoot status --short
git -C $IntegrationRoot switch -c $Candidate

git -C $IntegrationRoot merge --no-ff --no-edit <M3 精确提交 SHA>
git -C $IntegrationRoot merge --no-ff --no-edit <M7 精确提交 SHA>
git -C $IntegrationRoot merge --no-ff --no-edit <M11 精确提交 SHA>
```

默认合并顺序为 M3 → M7 → M11，因为联合运行时数据沿这个方向流动。稳定契约使三个文本变更通常可以自动合并，但每次 merge 后仍要检查：

```powershell
git -C $IntegrationRoot status --short
git -C $IntegrationRoot diff --check master...HEAD
```

全部联合门禁通过后：

```powershell
git -C $IntegrationRoot switch master
git -C $IntegrationRoot merge --ff-only $Candidate
git -C $IntegrationRoot push origin master
```

在候选分支验证失败时，`master` 保持不动。修复应回到对应功能分支形成新提交，再用新的唯一批次名重建候选；不要复用旧候选名，不要在候选分支里长期开发功能，也不要把只存在于候选分支的临时修复直接快进到 `master`。失败候选保留用于取证，只有集成者确认不再需要后才清理。

联合门禁至少包括：

1. Development Editor 全量构建。
2. `ABTS.Contracts.WorldGeneration`。
3. `ABTS.Contracts.M11PresentationAcceptance`；它只证明表现状态机安全，不得替代 M11-B StrictCertified。
4. M3 的 103 Seed 生成与 M9/Finale 分离。
5. M7 DAG2.3 全套自动化与生产建筑实时门禁。
6. M11-A、M11-B Unit/Runtime；阶段交接时包含两项慢速认证。
7. 当前 canonical `Content/Maps/L_ABTS_M10.umap` 的全新进程运行；本批次若修改 `Test.umap`，还必须额外验证它，不能二选一。
8. 一次可见 PIE 联合验收：在 `L_ABTS_M10` 验证地图引导 → 建筑 → 太空弹弓入口，并在 `L_ABTS_M11` 验证终局入口与 3+1 表现。
9. 比较 M3/M7/M11 排错账本自上次摘录基线以来的增量，将已稳定的共性结论提炼进 `DevelopmentTroubleshooting.md`；保留原账本并更新摘录基线。

## 11. 冲突处理

### 11.1 文本冲突

出现文本冲突时先判断所有权，不直接拼接两边代码：

- 同一专属文件冲突：该文件所有者负责在自己的功能分支合并最新 `master`、重做并验证。
- 功能分支修改了不属于自己的文件：中止本次集成，让该分支移除越界改动；不要在集成分支替其长期维护。
- 契约或共享热点冲突：中止功能合并，由集成工作树创建独立接口提交，再让相关分支合并新 `master`。

可在确认当前只是在未完成的候选合并中时使用：

```powershell
git merge --abort
```

禁止使用整文件 `ours/theirs` 掩盖语义冲突，禁止用 `reset --hard`、`checkout --` 或 `clean` 清理不明改动。

### 11.2 二进制冲突

`.uasset/.umap` 冲突没有文本合并流程：

1. 中止候选合并。
2. 确认资产的唯一所有者。
3. 明确以 `master` 中的资产版本为唯一基线；不得让 Git 尝试内容合并。
4. 由唯一所有者在 UE Editor 中对该基线重新执行功能改动并保存，形成替代提交。若需要在功能分支合并 `master`，二进制冲突也必须先保留 `master` 基线再手工重放，不能选择旧功能资产整文件覆盖。
5. 重跑资产所属阶段与联合 PIE。
6. 用新的替代提交重新集成。

### 11.3 编译/测试冲突但 Git 无冲突

这是接口或行为冲突，不是“偶发测试失败”。按最小复现定位：

1. 从 base-commit 分别验收三个精确提交。
2. 在候选分支按 M3、M7、M11 顺序逐个加入并运行相关测试。
3. 找到第一次失败的合并后，检查稳定契约、不变量、UObject 生命周期和 CDO/资产默认值。
4. 修复归属到引入行为变化的功能分支；若根因是共享接口，则归集成工作树。

## 12. 禁止操作

- 任何递归删除都必须先解析并核对绝对目标，只能位于当前工作树的明确生成目录，并确认没有该工作树进程占用；不得删除仓库根、其他工作树或来源不明的目录。
- 不使用 `git reset --hard`、`git clean -fdx`、强制 checkout 覆盖不明改动。
- 不从一个功能工作树直接合并另一个功能工作树。
- 不在已交接/推送分支上 rebase 或 force-push。
- 不删除仍被工作树使用的分支，不随意执行 `git worktree prune`、`git gc`。
- 不共用 `Saved/Logs` 文件，不把一次会话的构建产物复制给另一个工作树冒充验收。
- 不让两个 UE Editor 会话同时保存同一个资产。
- 不为减少冲突而复制第二套 M3→M7、M3→M11 数据通道。

## 13. 集成完成检查表

- [ ] 三个工作树来自交接给出的同一 base-commit。
- [ ] 各分支只修改其所有权范围，`Shared files changed: none`。
- [ ] 稳定契约版本与自动化通过。
- [ ] 每个分支独立编译和阶段自动化通过。
- [ ] 二进制资产具有唯一所有者且无冲突。
- [ ] 候选分支按 M3 → M7 → M11 合并精确 SHA。
- [ ] 联合构建、自动化、实时门禁和可见 PIE 通过。
- [ ] 各功能交接列出新增/更新的排错 ID；总排错文档已完成本批次提炼，三份原始账本仍保留。
- [ ] `master` 只以 `--ff-only` 接收已验证候选。
- [ ] 合并后记录精确提交、测试日志与未解决限制。

## 14. 相关入口

- 返回：[ABTS 项目工作流与开发入口](ABTSProjectWorkflow.md)
- M3：[Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [PCG 地图改进方案](M3PCGMapImprovementPlan.md)
- M7：[球面 DAG2.3 生产集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) · [DAG2.3 联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md)
- M11：[算法预演](M11GravityAssistAlgorithmPrevisualization.md) · [M11-A](M11AGravityAssistSolverDesign.md) · [M11-B](M11BFinaleLayoutCertificationDesign.md) · [PresentationAccepted 稳定合同](M110PresentationAcceptanceContract.md)
