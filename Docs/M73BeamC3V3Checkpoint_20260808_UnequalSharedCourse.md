# M7.3 Beam-C3 V3 不等截面 Shared Course 检查点（2026-08-08）

## 1. 状态摘要

- 工作树：`feature/m7-buildings`；检查点 HEAD 为
  `e6d74a7852efb91a846d89d1d548efe9ef67d10d`；
- 用户要求：允许 shared course 连接不同粗细的接地芯体；以较细芯体 rail 为 donor 延至较粗芯体外端；
  同站位冲突时省略较粗芯体该层 rail；总长超过 720 cm 时分段，且唯一中段必须进入两个芯体；
- 新合同已写入 `Docs/M73BeamC3V3SkeletonFirstBuildingGenerationDesign.md` 第 21 节；
- C++ 已加入逻辑 `FSharedCourseLanePlan`、donor/receiver、物理 segment、唯一 cross-core segment、
  receiver 冲突省略和 canonical 身份；已将 SupportedSpan 对层级芯体的全局禁用移除；
- UE 5.8 ForceUnity Development Editor 完整链接两次成功；最后一次增量约 38 秒；
- 固定 `SeamRelease.E6` Stage 1 尚未通过。两次 fresh NullRHI 均稳定失败为
  `BeamC3V3SharedCourseCrossSegmentTooLong`，同一 span/course/lane 的最小跨芯段为 `936 cm`，
  超过不可放宽的 `720 cm`；
- 已按“两次同身份停止”规则冻结现场。未运行 Staged 宽过滤、5×6、BeamD15、Chaos、可见 PIE 或图形化 Editor；
  `PhysicalStability=NotEvaluated`；
- `Content/Maps/PlanarPhysicsTestMap.umap` 是用户本轮演示改动。本轮未读取、覆盖、还原、暂存或修改该文件。

当前不是 Stage 1 视觉验收入口：不等截面/分段数据结构已落地并可编译，但权威 E6 仍在 shared 发射前失败。

## 2. 本轮冻结的生成合同

1. span 端点从实际覆盖最高 shared course 及其上下夹层的接地芯体中选择，不再固定取 component 第一个 core；
2. 两端可不同轨数。沿 bridge 横向宽度、平面面积、core id 确定 donor/receiver，donor 的每根 rail 形成
   一条逻辑 shared lane；其 36 cm 横向实体必须完整进入 receiver；
3. lane 的要求区间覆盖两个端点芯体的最外物理端面。receiver 存在同 course/axis/station rail 时删除并由
   cross-core segment 替换；无同站位 rail 时保留，不删除最近 rail；
4. 完整区间不超过 720 cm 时为单段；超长时 tails 只留在两端芯体内，恰有一个中段进入两端各至少 36 cm；
   每段不超过 720 cm、相邻段只端面相接；
5. core 槽位引用 cross-core segment 只作为逻辑 lane 身份锚。承压校验按整条 lane 的 segment 并集验证，
   不能要求中段单独覆盖整座大芯体，也不能把无真实接触的 tail 算作 Bearing；
6. `SharedCourseCount` 现表示逻辑 lane；物理段、跨芯段、冲突省略分别独立计数并进入 Hash。

## 3. 已修改文件

- `Docs/M73BeamC3V3SkeletonFirstBuildingGenerationDesign.md`；
- `Source/ABTSRuntime/Private/Building/ABTSM73BeamC3V3SkeletonFirstTypes.h`；
- `Source/ABTSRuntime/Private/Building/ABTSM73BeamC3V3SkeletonFirstGenerator.cpp`；
- `Docs/M7WorktreeTroubleshooting.md`；
- 本检查点文件。

工作区中其它 Beam-C3/D0/D1/预览文件均为此前未提交改动；本轮没有覆盖或回退它们。

## 4. 编译证据

命令统一使用唯一允许的引擎：

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat' `
  AngryBirdsToSpaceEditor Win64 Development `
  -Project='<worktree>\AngryBirdsToSpace.uproject' `
  -WaitMutex -FromMsBuild -ForceUnity -DisableAdaptiveUnity -NoHotReloadFromIDE
```

结果：

- 首次全 Unity：19 actions，`Result: Succeeded`，约 98 秒；
- validator/端点约束后增量：4 actions，`Result: Succeeded`，约 40 秒；
- bridge-side child 可达性约束后增量：4 actions，`Result: Succeeded`，约 37 秒。

## 5. 两次固定失败证据

测试过滤器：

```text
ABTS.M73DAG.BeamC3V3.Staged.Stage1CoreAndSharedBoundary
```

两次均由 fresh `UnrealEditor-Cmd.exe -NullRHI` 启动，均精确发现 `1` 个测试，均以相同身份失败：

```text
BeamC3Stage1:
BeamC3V3SharedCourseCrossSegmentTooLong:
Volume=0:Course=44:Lane=0:Length=936.000:
Opening=-701.760..-408.000
```

唯一日志：

- `Saved/Logs/BeamC3V3-UnequalShared-Stage1Boundary-20260808-1.log`；
- `Saved/Logs/BeamC3V3-UnequalShared-Stage1Boundary-20260808-2.log`。

两次都在约 11 秒的测试体内失败；没有把该结果扩展成昂贵宽门。进程退出 `255` 与日志中的
`Test Completed. Result={Fail}` 一致，不能作为成功证据。

## 6. 当前失败的准确含义

分段器只能解决“两个芯体内部余量使整条 lane 超过 720 cm”的情形。合法中段至少需要覆盖：

```text
负端芯体内 36 cm + 两端芯体之间的实际间距 + 正端芯体内 36 cm
```

当前被选中的、能够到达 `course 44` 的两个高层子芯体，其实际内侧面间距使该下界达到 `936 cm`。
因此不存在一根不超过 720 cm、同时进入两个芯体的中段。把整条 lane 切成更多段也不能满足“中段必须跨两芯”合同；
把 936 cm 中段再切开则只会恢复“两个局部 rail 加 opening 短桥”的旧假共享结构。

加入 bridge-side child inset 约束后失败身份完全不变，说明当前相关上部分支没有产生一个既达到 shared 高度、
又靠近 bridge opening 的合法 child 候选；这不是分段长度算法、预算、Seed 或 Chaos 问题。

## 7. 已排除且不得重复的路线

| 路线 | 结论 |
| --- | --- |
| 放宽 720 cm | 违反硬合同；禁止 |
| 把 936 cm 中段再切开并称为跨两芯 | 中间没有任一 member 同时进入两芯，会恢复旧假 shared；禁止 |
| 只在 child 候选最终 tie-break 中偏向 opening | 已加入 bridge-side reach 下界后失败身份不变；不是末级排序问题 |
| 改 Seed、密度、预算、36 cm 截面、容差或 Chaos | 与缺少可达 endpoint core 无关；未执行且禁止 |
| 继续跑 Staged、5×6 或 BeamD15 | 最小 E6 未过，宽门只会重复失败并浪费时间；未执行 |

## 8. 下一次唯一合理的切入点

先建立毫秒级 `SharedEndpointReachability` 纯数据夹具，不运行完整 E6。夹具必须对每个 incident WFC branch
和每个 core candidate 输出：

- branch path/source volume；
- `TopCourse >= HighestShared+2`；
- opening-side inner face 与 opening 的 inset；
- 另一端最优候选下的最小 cross-core segment 长度；
- Body/Crown 逐 course 覆盖结果；
- composite lane 冲突结果；
- 被拒绝的第一个权威原因。

夹具应先回答二选一：

1. WFC 包络内存在合法的 bridge-facing grounded child，只是当前 branch seed/候选归属没有枚举到；则修复
   endpoint child 派生并证明 Plan/endpoint core id 改变；
2. WFC 包络内不存在该 child；则当前“单一中段跨两芯”合同与该 E6 轮廓数学不相容，必须回到设计层选择
   新的显式中间接地芯体/桥墩语义，或修改上游 WFC span/branch 关系，不能继续调 Beam-C3 参数。

只有夹具产生新的 endpoint core id 或证明新的中间权威实体后，才允许再运行一次固定 E6。否则保持本检查点。
