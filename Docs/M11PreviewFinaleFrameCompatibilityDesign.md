# M11 Preview Finale Frame Compatibility

> 父级设计：[M11 v2 终局优化总设计](M11V2FinaleOptimizationDesign.md)
> 上游只读产物：[M3 PCG 地图改善计划](M3PCGMapImprovementPlan.md) 中的 M3R-5.2 Monthly Finale Anchor Preview
> 当前状态：**IntegrationAccepted；自动化、fresh `L_ABTS_M11` 与 Visible PIE 均已通过**
> 共享接缝：[M3R-5.2 → M5.1 → M11 Preview/Test 终局帧集成](M3R52M11PreviewFinaleIntegrationDesign.md)

## 1. 目标

M3R-5.2 已能为显式月度候选生成道路末端终局锚点预览。M11 的任务不是重新搜索轨迹，也不是把该预览提升为正式月度世界，而是证明并保留以下性质：

- Certified v1 与 Editor Candidate 都只保存终局局部坐标；
- 同一个局部布局可以刚性平移、旋转到任意合法主星表面帧；
- 求解请求不读取行星 Actor、UFO Actor 或槽 Actor 的世界坐标；
- 行星、UFO、轨迹演出和轨道图均消费同一个 `FABTSM110FinaleLocalFrame`；
- Rank 0 仍为 Production Certified v1，Rank 1～11 仍为 Editor Candidate / NOT CERTIFIED。

本阶段不修改 M3 道路、不选择月度候选、不修改 M5.1 太空槽、不覆盖 `Planet->FinaleLaunchFrame`，也不修改稳定世界生成契约或共享地图。

## 2. CandidateRank 单一选择权威

编辑器控制台变量仍为：

```text
abts.M11.CandidateRank
```

语义保持为：

- `0`：Production Certified v1；
- `1..11`：冻结的 Editor-only、UNCERTIFIED 候选；
- 必须在启动 PIE 前设置；修改后停止并重新启动 PIE。

控制台变量的注册与读取统一收口到 `FABTSM11CandidateExperienceCatalog::GetRequestedCandidateRank()`。M11 GameMode 与后续 Integration-owned M3 预览适配器必须读取这个入口，不得重复注册同名变量或建立第二套 Rank 配置。

## 3. 终局帧兼容规则

合法预览帧必须满足：

1. `LayoutVersion` 与布局预设兼容；
2. `WorldTransform` 为单位缩放刚性变换；
3. X=发射前方、Y=左槽到右槽、Z=主星径向上方；
4. 三轴单位化、正交且构成右手系；
5. 左右槽中点等于帧原点，槽对方向与 Y 轴一致；
6. M3 GeneratorVersion、主星半径与候选预设兼容。

不合法的缩放、版本、槽对/坐标轴关系必须 fail closed。帧的世界位置与方向不进入 Preset、Scenario、Request、Result 或 Certification Hash。

## 4. 诊断契约

M11 Ready 时输出：

```text
[ABTS][M11][FinaleFrame]
Authority=Production|PreviewTest
CandidateRank=<rank>
LayoutVersion=<version>
FrameHash=<diagnostic-only hash>
LaunchTask=<id> Anchor=<cell> Pair=<id>
LocalStart=<local pouch> WorldStart=<transformed pouch>
Origin/Forward/Right/Up=<frame basis>
```

`FrameHash` 只用于联合 PIE 排错，不属于求解或认证身份。集成日志还必须补充 M3 `SourceRouteCandidateId`、M3 Preview Hash 与 PreviewTest 权威，才能形成完整的跨阶段证据链。

## 5. 自动化验收

`ABTS.M11B.Runtime.PreviewFinaleFrameCompatibility` 使用同一个 Rank 11 局部候选和至少三组显著不同的合法主星表面帧，验证：

- Certified v1 对三帧均通过兼容门；
- 三帧拥有不同的诊断 Hash，但 Candidate Request/Result Hash 完全相同；
- 局部轨迹逐点位置、速度和时间完全一致；
- 世界轨迹逐点反变换回局部后的误差不超过 `0.001 cm`；
- 三颗行星 Actor 与 UFO Actor 反变换后回到预设局部位置；
- Playback Plan 与轨道图轨迹保持同一局部身份；
- 发射方向经帧变换、反变换后保持一致；
- 非单位缩放、版本不兼容和槽对/基底不一致均被拒绝。

同时，`ABTS.M11C.V2_1.InputParityAndLatestOnly` 验证控制台变量已经注册、默认值为 0，并且 Catalog 读取值与控制台变量一致。

## 6. 集成交接顺序

M11 合并回 master 后，由原始集成工作树完成：

1. 显式选择一个有效的 M3R-5.2 `FABTSM3MonthlyFinaleAnchorPreview`；
2. 构造独立的 Preview/Test 终局帧上下文，不覆盖正式 `FinaleLaunchFrame`；
3. 把同一个预览帧同时交给 M5.1 太空槽与 M11 初始化；
4. 在 M11 初始化前通过 `GetRequestedCandidateRank()` 读取玩家在 PIE 前设置的 Rank；
5. Rank 0 走现有 Certified v1 路径，正 Rank 走现有 `InitializeFromEditorCandidateRank`；
6. 记录 M3 Candidate/Preview Hash、M11 Frame Hash 与 CandidateRank；
7. 在 M3 地图执行 fresh Editor 自动化与可见 PIE，确认太空槽、行星、UFO、轨迹和交互没有帧分裂。

共享适配器、M5.1 和 `L_ABTS_M11` 已完成代码接入、自动化、fresh NullRHI 与 Visible PIE，当前标记为 `IntegrationAccepted`。该验收不授予月度正式布局权威。
