# M3R-5.2 → M5.1 → M11 Preview/Test 终局帧集成

> 上游：[M3R 月度地图改进](M3PCGMapImprovementPlan.md) · [M11 Preview Finale Frame Compatibility](M11PreviewFinaleFrameCompatibilityDesign.md)
> 验收地图：`/Game/Maps/L_ABTS_M11`
> 当前状态：**代码、自动化与 fresh NullRHI 已通过；Visible PIE Pending**

## 1. 目标与边界

本接缝只用于显式 Preview/Test：把 M3R-5.2 候选道路末端锚点同时交给 M5.1 太空槽和 M11 终局局部布局。它不覆盖 `AABTSM3Planet::FinaleLaunchFrame`，不发布 `MonthlyAccepted`，不重搜 M11 轨迹，也不改变生产默认路径。

`BP_ABTSM11GameMode` 当前显式启用 `SourceRouteCandidateId=4`；原生 C++ 默认仍关闭。GameMode 在主星 `BeginPlay/RebuildPlanet` 前注入候选，使道路表现、普通槽净空和终局锚点来自同一 M3 预览。

## 2. 单一数据链

```text
M3 FABTSM3MonthlyFinaleAnchorPreview
  → FABTSM51PreviewFinaleFrameAdapter
  → FABTSM51PreviewFinaleFrameContext (PreviewTest, MonthlyAccepted=0)
       ├─ AABTSM51WorldSystem：普通槽 + 一对 FinaleSpace 槽
       └─ AABTSM11GameMode：替换测试初始化副本中的 LaunchFrame
            → AABTSM11FinaleSystem：3 颗行星 + UFO + 同源轨迹
```

兼容 TaskGraph 的 `FinaleLaunchFrame` 只提供 `LayoutVersion/LaunchTaskId/SlotPairId` 兼容身份；世界原点、三轴、AnchorCell 和左右槽位置来自 M3R-5.2。任何候选、Hash、坐标轴或配置时序不一致均 fail closed，不回退到旧 Cell 7683。

## 3. 连续地表兼容

M3 会把左右槽分别贴合连续起伏地表，因此槽对连线可以含有限的径向高度差。`FABTSM110FinaleLocalFrame::IsUsable()` 现要求：

- 槽对中点仍严格等于帧原点；
- 槽对在切平面上的投影严格沿局部 Y；
- 径向倾斜不超过 45°；
- 帧本身仍为单位缩放正交右手刚性变换。

这是一项向后兼容放宽：旧水平槽对继续通过，沿 X 错位、纯径向或过度倾斜的槽对仍被拒绝。

## 4. 自动化与运行证据

- `ABTS.Integration.PreviewFinaleFrame`：2/2；适配确定性、正式帧不变、MonthlyAccepted 拒绝、帧/槽分裂拒绝和 WorldSystem fail-closed。
- `ABTS.M110`：4/4；含独立地表贴合槽对和过度倾斜拒绝。
- `ABTS.M51`：4/4。
- `ABTS.Contracts.WorldGeneration`：2/2。
- `ABTS.M3.Monthly.FinaleAnchor`：3/3。
- `ABTS.M11B.Runtime`：5/5。
- `ABTS.M11A`：15/15；`ABTS.M11B.Unit`：8/8。
- `ABTS.M11C.Unit`：8/8；`ABTS.M11C.Runtime`：2/2。
- `AngryBirdsToSpaceEditor Win64 Development -ForceUnity -DisableAdaptiveUnity`：通过。
- fresh `L_ABTS_M11 -game -NullRHI -ABTSM3R5Smoke`：Candidate 4、Anchor 847、Finale 槽 2、Assists 3、UFO 1；M5.1 与 M11 输出同一 Preview/Context/Frame 身份，M3R-5 runtime certification 通过。

## 5. Visible PIE 验收

1. 打开 `L_ABTS_M11`，使用其默认 `BP_ABTSM11GameMode` 开始 PIE。
2. 确认月度道路候选 4 可见，太空槽唯一一对且位于道路末端，不再位于旧 Cell 7683。
3. 检查两槽均贴合地表，没有明显悬空、埋入或左右翻转；普通槽不进入终局净空。
4. 确认 M11 三颗行星、UFO、轨迹和交互均相对该槽对生成。
5. 日志必须同时出现 `Authority=PreviewTest Candidate=4`、`Finale=2 AnchorCell=847`、`Assists=3 UFO=1`，且不出现 `PreviewFinaleFrame Rejected`。
6. 如需体验未认证布局，PIE 前设置 `abts.M11.CandidateRank 1..11`；Rank 0 仍使用 Certified v1 局部布局。无论 Rank，世界帧均为本接缝的 Preview/Test 帧。

Visible PIE 通过前，本接缝不得标记为 `IntegrationAccepted`，也不得把候选 4 晋升为正式月度世界。
