# M7.3 Beam-C3 V3 局部差异裙房高度计划检查点（2026-08-11）

## 1. 恢复身份

- 工作树/分支：`feature/m7-buildings`；本轮未移动 `master`。
- 基线提交：`e309713`。
- 阶段：Stage 1，只读 `LocalPodiumHeightPlan` 视觉停点；生产芯体/积木未改。
- 用户资产：`Content/Maps/PlanarPhysicsTestMap.umap` 保持用户工作区状态，未覆盖、暂存或还原。
- 证据层：UE 5.8 Development Editor 编译与 fresh NullRHI；未启动 GUI、可见 PIE、5×6 或 Chaos，
  `Physical=NotEvaluated`。

## 2. 根因与失败试验

第一轮以每个 support province 自己的 merge event 为候选，并要求同 course footprint 直接面接触，结果是 710000 只到
course 51、730000 全部停在 baseline 40、750000 只到 course 33。该方案失败有两个原因：

1. `BoundGroundCoreCellId` 是接地覆盖身份，不是 demand 的结构父 main；
2. 同一结构 main 的兄弟省份事件层可能错开，36 cm 离散轮廓还会留下 2–3 格量化缝。完全相同事件与零缝接触把
   视觉上连续的共同裙房误判为断开。

本轮没有通过改 Seed、固定魔法容差、积木尺寸、36 cm 网格或 720 cm 上限追逐三个样例。

## 3. 最终只读计划合同

1. 从每个省份 demand 的 `SemanticDemandId -> TowerChild -> PodiumMainCoreCellId` 解析唯一
   `StructuralPodiumMainCoreCellId`；缺失或同省份不一致 fail closed。
2. 同一结构 main 家族共享全部 semantic contact event，候选同时保留 `OwnBoundary` 与 `SharedEvent` 身份。
3. 每个候选 footprint 必须从地面连续存在、覆盖全部 demand seed、给最矮 child 留至少两个 course，并避开
   `ReservedSupportVoid`。
4. 每省份候选必须保留其第一层合法 raised footprint 至少 50%，避免裙房抬到已经收缩成塔颈的截面。
5. 最近端点间的 X-then-Y 或 Y-then-X L 形 seam bridge 不得超过 720 cm，且至少一条必须 protected-void clear。
6. 同一结构 main 内优先选择覆盖省份/支撑需求更多的组，再在同覆盖数下选择更高分隔面；未入组省份保持 baseline。
7. 计划 Hash 和生产 GeometryHash 分离；本停点不写回生产 WFC/PodiumMain。

## 4. 编辑器验收入口

在物理测试地图的 `ABTSM73BeamD1PreviewActor` 上：

- `Generation Stop Stage = Stage 1 - Core + Shared Courses`；
- `Stage 1 Diagnostic Layer = 11 - Local Podium Height Plan`。

彩色薄板是选中顶面；钢材薄板是 raised region 的真实结构 main baseline；竖直细柱显示抬升量；相邻彩色薄板间的
水平细线是计划中的 seam bridge；铁色小方块是拒绝候选。该层不代表桥积木已经生成。

目标中部结构 main 应为：

- `TipOver / E6 / 710000`：actual 32，`StructuralMain=1`，provinces `0,1,2`，selected `92`；
- `TipOver / E6 / 730000`：actual 40，`StructuralMain=0`，provinces `0,1`，selected `92`；
- `TipOver / E6 / 750000`：actual 32，`StructuralMain=0`，provinces `0,1`，selected `89`。

其他结构 main 可产生自己的局部台阶，但不允许跨父 main 合组。

## 5. 自动化证据

- UE 5.8 Development Editor 全链接成功。
- fresh `PreviewDiagnosticContracts`：Found 1，Success 1，EXIT CODE 0。
- fresh `TipOverE6OptimizationSeeds`：Found 1，Success 1，EXIT CODE 0。
- fresh `Stage1CoreAndSharedMatrix.TipOver.E6`：Found 1，Success 1，EXIT CODE 0。
- 三种子生产几何 Hash 保持
  `8070591144232803120 / 3595832047213963210 / 7430148173544257172`；Static DAG Accepted。
- 三种子单叶总计约 `1105.03 / 1029.49 / 1201.39 ms`；Physical NotEvaluated。

日志：

- `Saved/Logs/BeamC3-LocalPodium-CoverageFirst-Final-TipOverSeeds-20260812.log`
- `Saved/Logs/BeamC3-LocalPodium-CoverageFirst-Final-Preview-20260812.log`
- `Saved/Logs/BeamC3-LocalPodium-CoverageFirst-Final-TipOverE6-MatrixLeaf-20260812.log`

## 6. 下一停点

等待用户视觉批准第 11 层。批准前不把 plan 写回 WFC/PodiumMain，不跑 5×6、Stage 2、Beam-D1.5 或 Chaos。
批准后让生产局部 CoupledGround/PodiumMain 消费逐区域高度，并逐区重验 SupportedSpan、Crown、ProtectedVoid、全高
TowerChild、720 cm 与预算合同；任何一项不成立都必须失败关闭。
