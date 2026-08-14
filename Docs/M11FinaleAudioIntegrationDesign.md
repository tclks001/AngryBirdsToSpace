# M11 终局专属音效接入设计

## 1. 目标与边界

本阶段只在 M11 所有权范围内，把终局交互的稳定语义事件接入共享
`UABTSAudioWorldSubsystem`。不修改共享 Audio 子系统、`Config/**`、
`Content/Audio/**`、`Content/SoundEffects/**` 或共同地图，也不新增第二套音量、
Sound Class、Sound Mix 或资产目录权威。

首版“专属”指 M11 独占的事件编排和证据语义；声音素材继续复用已经进入
共享音频目录的弹弓、金属撞击、轻爆炸、鸟叫和 UI 反馈。后续若制作新的 UFO、
救援或终局音乐素材，应先由集成工作树扩展共享音频目录/公共入口，再由 M11
替换本文件中的语义映射。

## 2. 事件映射

| M11 事件 | 共享音频调用 | 约束 |
| --- | --- | --- |
| 成功进入终局瞄准 | `SetMusicState(Finale)` + `PlayUIEvent(Open)` | 只有 `TryEnterFinale()` 完成后触发 |
| 发射计划正式提交 | `PlaySlingshotRelease()`，随后重新声明 `Finale` | 位置、静止弦长和 Power 取冻结发射输入；避免共享 Release 的 `Destruction` 音乐态覆盖终局配乐 |
| 生产侧物理接触 UFO | 金属撞击 + 小型爆炸 + 当前鸟短叫 + `Confirm` | 必须是非 Candidate 模式且 `bPhysicalTargetHit=true` |
| Editor Candidate 到达候选终点 | `PlayUIEvent(Select)` | 只给中性预览反馈，不播放爆炸、鸟叫或成功确认 |
| 发射失败/运行时失败 | `PlayUIEvent(Error)` | 进入统一失败时间线时触发一次 |
| 取消、退出或失败恢复完成 | `SetMusicState(Explore)` + `PlayUIEvent(Close)` | 世界恢复到 `Ready` 后触发 |

终端结果通过纯函数 `ABTSM11ResolveFinaleCompletionAudioCue()` 解析。Production
只有物理 UFO 接触能得到 `CertifiedTargetHit`；Candidate-only、缺失证据或非法
身份全部 fail closed。

## 3. 运行时证据

每次语义触发输出：

```text
[ABTS][M11-C][Audio] Cue=<...> Authority=<...> Played=<0|1> State=<...> Plan=0x<...>
```

正式成功必须同时看到：

- `Cue=CertifiedTargetHit`
- `Authority=StrictCertifiedPhysical`
- 同一发射计划的非零 `Plan` Hash
- 既有 `[ABTS][M11-C][Playback] PhysicalTargetHit` 日志

`Authority=EditorCandidatePreviewOnly` 只能作为候选体验证据，不得晋升为生产救援。
`Played=0` 表示当前 World 没有可用共享 Audio Subsystem；状态机继续保持权威，
不得因音频缺失改变求解、播放或成功判定。

## 4. 自动化与 PIE 验收

自动化：

1. `ABTS.M11C.Unit.AudioCueAuthority` 覆盖 Production 物理命中、Production
   candidate-only、Editor Candidate、Candidate 身份不完整和无命中证据。
2. M11 快速门继续覆盖 `ABTS.M11C.Unit`、`ABTS.M11B.Runtime` 及共享音频
   `ABTS.Audio.ReleaseAndMusicMapping`。

可听 PIE 必须由用户在 `L_ABTS_M11` 执行：

1. 进入太空弹弓：终局四轨淡入且只出现一次打开提示。
2. 点击 Launch：袋口位置发出弹弓释放声，音乐保持 Finale，不短暂停留在普通
   Destruction 状态。
3. Production 物理命中 UFO：在可见接触帧听到金属撞击、小爆炸、鸟叫和确认；
   不能在 Qualified Endpoint 或终端转移开始时提前触发。
4. Editor Candidate：只听到中性 Select，不得出现爆炸、鸟叫或成功确认。
5. 分别制造 Miss、BodyCollision 和超时：每次失败只出现一次 Error；全黑恢复后
   返回 Explore 音乐。
6. 退出/取消后重新进入两次：每轮进入、发射、失败/成功和退出均只触发一次，
   不残留 Release/Finale Audio Component。

NullRHI 只验证映射、资产加载和生命周期日志，不能替代上述听感与空间定位验收。

## 5. 本轮验证证据（2026-08-15）

- Development Editor：`Result: Succeeded`，日志
  `Saved/Logs/M11-Audio-20260815-014851-UBT.log`。
- `-ForceUnity -DisableAdaptiveUnity`：`Result: Succeeded`，日志
  `Saved/Logs/M11-Audio-ForceUnity-20260815-015030-UBT.log`。
- `ABTS.Audio.ReleaseAndMusicMapping`：1/1。
- `ABTS.M11C.Unit`：13/13，包含 `AudioCueAuthority`。
- `ABTS.Contracts.WorldGeneration`：2/2；fresh 日志明确枚举
  `M3Adapter` 与 `Validation`。
- `ABTS.Contracts.M11PresentationAcceptance`：3/3。
- `ABTS.M110`：4/4。
- `ABTS.M11A`：15/15。
- `ABTS.M11B.Unit`：9/9。
- `ABTS.M11B.Runtime`：6/6。

每条 fresh 日志均包含 `**** TEST COMPLETE. EXIT CODE: 0 ****`，且未发现
Automation Fail、Ensure 或 Fatal。求解器、冻结布局和认证身份未变化，因此本轮不
运行 `ABTS.M11B.ConstructiveSearch` 与
`ABTS.M11B.Certification.FullInputDomain`。可听 PIE 仍待用户执行。
