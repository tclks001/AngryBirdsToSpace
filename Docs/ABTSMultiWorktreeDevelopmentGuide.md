# ABTS M3/M7/M11 多工作树协作与集成规范

> 编码：UTF-8，简体中文。
> 状态：多工作树共同基线规范。
> 适用范围：`M3 PCG 地图`、`M7 建筑`、`M11 终局` 三条并行开发线，以及作为集成工作树的原始仓库。

## 1. 目标与拓扑

本项目采用“一个集成工作树 + 三个功能工作树”的固定拓扑：

```text
C:\workspace\AngryBirdsToSpace       原始仓库；唯一集成工作树
├─ master / integration-candidate    只做基线、合并、联合验收
│
├─ C:\workspace\AngryBirdsToSpace-M3   feature/m3-pcg-map
├─ C:\workspace\AngryBirdsToSpace-M7   feature/m7-buildings
└─ C:\workspace\AngryBirdsToSpace-M11  feature/m11-finale
```

只新增上述三个功能工作树，不再新增第四个集成工作树。原始仓库始终承担集成职责。

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
- M3 的只读契约导出适配器
- M7/M11 的契约消费入口
- 本规范及项目工作流中的入口链接

该 base 至少必须通过 Development Editor 全链接、`ABTS.Contracts.WorldGeneration`、M7 Routing/DAG 回归、`ABTS.M110`、`ABTS.M11A`、`ABTS.M11B.Unit` 与 `ABTS.M11B.Runtime`。如果求解器、冻结布局或认证身份发生变化，还必须重跑 M11-B 两项慢速认证；若未变化，可引用此前已经批准且 Hash 未变的慢测证据。最终交接必须列出本次实际过滤器、成功测试数和唯一日志。

Git 提交无法在自身内容中记录自己的最终 SHA，因此 base-commit 的精确 SHA 由完成本任务时的交接消息给出。创建工作树前必须把该 SHA 填入 `$BaseCommit`，不要用会继续移动的 `HEAD`、`master` 或远端分支名代替。

在原始仓库的 PowerShell 中执行：

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

创建前，`git status --short` 必须没有输出。如果某个目标目录、分支或工作树已经存在，先停止并查明归属，不得用删除目录、`worktree prune` 或强制重建来绕过。

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
- `.uproject`、`Config/**`、`*.Build.cs`、`*.Target.cs`
- `Docs/ABTSProjectWorkflow.md`
- `Docs/ABTSMultiWorktreeDevelopmentGuide.md`
- `Docs/AngryBirdsToSpaceGameDesign.md`
- `Docs/M110PreFinaleClosureDesign.md`
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

每个 Codex/人工会话开始时先执行：

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

## 8. 独立验收

### 8.1 通用编译

每个工作树使用自己的项目绝对路径：

```powershell
$EngineRoot = 'C:\Program Files\Epic Games\UE_5.8'
$ProjectRoot = '<当前工作树绝对路径>'

& "$EngineRoot\Engine\Build\BatchFiles\Build.bat" `
  AngryBirdsToSpaceEditor Win64 Development `
  "-Project=$ProjectRoot\AngryBirdsToSpace.uproject" `
  -WaitMutex -NoHotReload

if ($LASTEXITCODE -ne 0) {
  throw "Development Editor build failed: $LASTEXITCODE"
}
```

自动化必须在全新 `UnrealEditor-Cmd` 进程运行，并使用当前工作树独有的绝对日志：

```powershell
$EditorCmd = "$EngineRoot\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
$Project = "$ProjectRoot\AngryBirdsToSpace.uproject"
$RunId = Get-Date -Format 'yyyyMMdd-HHmmss'
$Log = "$ProjectRoot\Saved\Logs\<阶段>-$RunId-FreshAutomation.log"

& $EditorCmd $Project `
  -unattended -nop4 -NullRHI `
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

### 8.2 分工作树门禁

| 工作树 | 自动化门禁 | 视觉/运行时门禁 |
| --- | --- | --- |
| M3 | `ABTS.Contracts.WorldGeneration`、`ABTS.M110.TaskGraphFinaleSeparation` | `L_ABTS_M3`：确定性重生成、道路/任务间距、施工台、M9/Finale 分离 |
| M7 | `ABTS.M7.TaskGraphDAG23ProfileRouting`、`ABTS.M73A`、`ABTS.M73B`、`ABTS.M73B2`、`ABTS.M73DAG` | `L_ABTS_M7` 与 `PlanarPhysicsTestMap`；正式门还需当前 canonical `L_ABTS_M10` 的实时 D3D12/PIE |
| M11 快速 | `ABTS.Contracts.WorldGeneration`、`ABTS.M110`、`ABTS.M11A`、`ABTS.M11B.Unit`、`ABTS.M11B.Runtime` | `L_ABTS_M11`：三行星/UFO、局部坐标、M9 排除、重复进入与 fail closed |
| M11 慢速 | `ABTS.M11B.ConstructiveSearch`、`ABTS.M11B.Certification.FullInputDomain` | 仅在冻结参数/求解器/认证身份变化或阶段交接时运行 |

M7 的 `-benchmark` 固定时间步只作为算法回归。正式建筑门禁仍要求在当前 canonical `L_ABTS_M10` 上进行不带 `-benchmark` 的 D3D12 fresh game 30/60/120 FPS 三档运行，以及一次可见 PIE/hitch soak。每档日志至少满足：

- `BuildingContractSealed Expected=3 Registered=3 SetupRejected=0`
- 三栋必需建筑各自 `IdleValidation ... Accepted=1`
- `WorldReady=1` 晚于全部建筑接受
- 不出现 `BuildingGateRejected`

“编译成功”或 NullRHI 自动化不能替代可见 PIE；可见 PIE 也不能替代确定性自动化。独立验收需要两者中与本阶段相关的部分。

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
3. M3 的 103 Seed 生成与 M9/Finale 分离。
4. M7 DAG2.3 全套自动化与生产建筑实时门禁。
5. M11-A、M11-B Unit/Runtime；阶段交接时包含两项慢速认证。
6. 当前 canonical `Content/Maps/L_ABTS_M10.umap` 的全新进程运行；本批次若修改 `Test.umap`，还必须额外验证它，不能二选一。
7. 一次可见 PIE 联合验收：在 `L_ABTS_M10` 验证地图引导 → 建筑 → 太空弹弓入口，并在 `L_ABTS_M11` 验证终局入口与 3+1 表现。

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
- [ ] `master` 只以 `--ff-only` 接收已验证候选。
- [ ] 合并后记录精确提交、测试日志与未解决限制。

## 14. 相关入口

- 返回：[ABTS 项目工作流与开发入口](ABTSProjectWorkflow.md)
- M3：[Task Graph 球面 PCG](ABTSTaskGraphPCGDesign.md) · [PCG 地图改进方案](M3PCGMapImprovementPlan.md)
- M7：[球面 DAG2.3 生产集成](M7TaskGraphSphericalBuildingIntegrationDesign.md) · [DAG2.3 联合支撑](M73DAG23CumulativeLoadAndJointSupportDesign.md)
- M11：[算法预演](M11GravityAssistAlgorithmPrevisualization.md) · [M11-A](M11AGravityAssistSolverDesign.md) · [M11-B](M11BFinaleLayoutCertificationDesign.md)
