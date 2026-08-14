# Beam-C3 Stage 5 Checkpoint：Active DAG 压实

日期：2026-08-15（Asia/Hong_Kong）

## 1. 本轮目标与边界

Stage 5 的首个停止点只验证冻结 Stage 4 几何能否压实为生产级积木与真实承重 DAG：

- 只消费 `FloorInfillRoof` 的 active member；
- 移除 `bSuppressedByStage4FacadeToTop` 临时构件并重排稠密 ID；
- 一个 active member 对应一个 brick 和一个 load node；
- 从最终 AABB 重建真实 Bearing Contact，禁止声明式补边；
- 不生成弱点、炸药桶或活塞，不运行 Chaos；
- 本轮只验证固定演示六栋，不扩展到 5×6 或 Seed 扫描。

## 2. 已完成实现

- 新增 `FABTSM73BeamD1Stage5Result` 与 `GenerateStage5`。
- 发布 Stage4→Compact member 映射、suppressed 数量、active geometry hash、bearing DAG hash 与 production identity hash。
- 从 active assembly 重建 joints 和 Bearing Contact，再调用只读 Beam-C load DAG。
- Stage 5 brick 编译严格保持 `Member == Brick == LoadNode`，并验证 36 cm 单位化、720 cm 上限与零正体积穿透。
- 新增 E1 与固定演示六栋自动化；失败时发布 unreachable component 的 Stage 4 ID、kind、role、owner、course、bounds 与 required-lower 证据。

## 3. 已通过证据

- UE 5.8 ForceUnity Development Editor 全链接通过。
- fresh NullRHI `Stage5ProductionDAG.E1`：1/1。
- E1：Stage4=52、Suppressed=0、Active=52、Brick=52、Contact/Edge=96/96、Ground=6、Unreachable=0、Cycle=0。
- E1 `ActiveGeometryHash=16780849829317489644`，与冻结 Stage 4.5 几何身份一致；`PhysicalStability=NotEvaluated`。
- 日志：`Saved/Logs/M7-Stage5-E1-20260815.log`。

## 4. 固定六栋暴露的阻塞（历史证据，已由第 6 节关闭）

固定六栋测试为 1/6：E1 通过，E2～E6 均以 `BeamCGroundUnreachable` fail closed。

| 建筑 | Active | Suppressed | Unreachable |
| --- | ---: | ---: | ---: |
| E2 | 230 | 4 | 10 |
| E3 | 387 | 12 | 22 |
| E4 | 877 | 38 | 12 |
| E5 | 1894 | 41 | 40 |
| E6 | 2316 | 53 | 40 |

不可达分量的根通常是 Stage 4 `FloorCourse`，本身没有真实 lower bearing；后续 `FacadeToTopSeat/Post` 只把该悬空根继续向上传递。冻结 Stage 4 的局部门在未压实 Assembly 上重建接触，已标记 suppressed 的 Stage 3 临时外柱仍然参与接触，因此形成假接地路径。Stage 4.5 虽然从可见几何和 Hash 中排除了这些临时柱，却没有在压实后的 active assembly 上重新运行完整 ground-reachability。

日志：`Saved/Logs/M7-Stage5-SixBuildings-Diagnostic-20260815.log`。

## 5. 停止决定与下一最小步骤

这不是 Stage 5 映射丢边，也不是预算、Seed、密度或容差问题。继续在 Stage 5 添加隐藏支柱会重新制造声明式假支撑，因此禁止。

下一步必须窄范围重开冻结 Stage 4 的 Facade-to-Top 闭合：

1. suppression 后构建 active assembly 与连通分量根账本；
2. 每个悬空 `FloorCourse` 根必须复用真实 active lower member，或生成 36 cm 单位化、可追溯到 TopSurface/芯体的垂直闭合；
3. Stage 4 在 suppression 后的 active compact assembly 上验证完整 ground reachability；
4. 更新 Stage 4/4.5 六栋几何 Hash 并重新进行必要的视觉验收；
5. 随后重跑 Stage 5 E1 与固定六栋，要求 6/6、零 unreachable、零 cycle。

该修复会改变已经批准的 Stage 4 几何身份，必须由用户明确同意重开冻结范围后再实施。测试地图继续作为用户改动排除。

## 6. 重开结果与根因关闭

用户授权继续推进 Stage 5 后，压实 DAG、逐成员诊断和离屏截图共同确认了两层问题：

1. Stage 4 抑制了横跨 TopSurface 的整根 Stage 3 临时柱，连同 TopSurface 以下仍应保留的真实承托段一起删除；deferred junction 又在同一体素发射水平替代块，导致生产压实后出现浮空根。
2. 即使修正该 seam，完整全建筑荷载仍会产生少量不可达根和支座 resultant 缺口；Stage-local DAG 不能代表最终压实后的全建筑 DAG。

Stage 4 窄修复只保留临时柱的 TopSurface 以下部分，取消同体素水平替代块，并为 TopSurface frame 选择最近的真实柱站及必要的下座。Stage 4.5 随后从真实 active geometry 重新发布六项冻结描述，Catalog Hash 更新为 `2538906766243156379`。

Stage 5 不再添加隐藏构件，而是把闭合项作为普通 production member 追加：先补不可达水平根的 36 cm 单位化 Z 路径，再运行预算内的有界结构闭合。所有新增项都编译为可见 brick、进入 Bearing/Load DAG，并计入最终 Production Hash。

## 7. 最终证据

- ForceUnity Development Editor 全链接通过。
- fresh Stage 4.5 Placement Freeze：1/1。
- fresh 固定六栋 Stage 5：6/6；E1～E6 Production 分别为 `52/263/412/930/2057/2433`。
- 六栋均为零 unreachable、零 cycle，且 member=brick=node、contact=edge。
- E5/E6 离屏取证包含 Stage 1～4 总览、完整 Stage 5、仅新增构件的等轴与侧视图；新增项位于轮廓内部的真实承托链，没有轮廓外支撑林或跨越保留开口。
- 日志：`M7-Stage45-Refreeze-Final-20260815.log`、`M7-Stage5-SixBuildings-Final-20260815.log`、`M7-Stage5-Offscreen-E5-ExplicitClosure-20260815.log`、`M7-Stage5-Offscreen-E6-ExplicitClosure-20260815.log`。
- 截图：`Saved/ABTSVisualCaptures/M73Stage5/E5-ExplicitClosure`、`E6-ExplicitClosure`。
- Chaos、爆炸物、活塞、破坏行为和可见 PIE 尚未评估；测试地图继续排除。
