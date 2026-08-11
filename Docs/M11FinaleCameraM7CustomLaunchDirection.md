# M11 终局实飞镜头 M7：玩家自定义 F4 路线导演

## 1. 目标与边界

M7 让玩家在当前 Preset 的 `Yaw × Pitch × Power` 域内自行发射后，只要权威分类为 F4，现有单星 Lucy 穿越、双星桥接和 UFO 收束就消费该次真实路线，而不是只对 NominalInput 成立。

本阶段不修改求解器、F4 判定、候选 Rank、终端 800 cm 接触曲线、Stylized 渲染或生产默认开关。Rank 1–11 的录屏仍只能标为 `UNCERTIFIED`。

## 2. 根因与新合同

旧实现每帧从可变的 `LatestQualifiedResult` 解析镜头阶段，并把固定的表现时长强加给所有交接窗口。某些有效 F4 路线在行星轮廓清空到下一颗行星 Enter 之间不足以容纳完整固定预算，`ResolveStage` 会返回 `Unavailable`；交互层又把 `FlightCameraDirectorSampleRejected` 当作玩法失败，于是出现黑屏回退。

M7 建立三条单向权威链：

1. `FrozenReleaseInput → ReleasedTrajectoryResult → ReleasedPlaybackPlan` 决定玩法；
2. `ReleasedTrajectoryResult + ShotSettings → ReleasedCameraShotPlan` 只决定表现；
3. 表现失败只允许回退到轨迹切线 authority camera，不得修改玩法状态。

发射后，任何新预览 solve 都不能替换 Released Camera 的事件身份。镜头日程必须携带同一个 `ReleasedTrajectoryHash`，Hash 不符时拒绝消费。

## 3. 自适应跨星日程

每段交接首先以 `ForegroundTransitClearProgress` 计算上一颗行星前景轮廓清空时刻。窗口充足时继续使用 M3 已验收的 Outgoing、Bridge、Acquire、Track、Entry 时长和边界。窗口不足时，以最小 Outgoing、Bridge、Acquire、Track、Entry 的相同比例压缩全部阶段，保持：

```text
ForegroundClear < OutgoingEnd/RevealStart < BridgeEnd
                < AcquireEnd < EntryStart < IncomingEnter
```

压缩只改变阶段速度，不改变顺序、端点构图、鸟体 Scale 或轨迹。若连严格正序都无法建立，则整次飞行记 `CameraDirectorFallback` 并采用 authority follow；F4 播放与 UFO 接触仍继续。

## 4. 独立录屏入口

合同 v15 在既有 Rank、Stylized 和 M3 参数之外增加完整三元组：

```text
-ABTSM11CaptureYaw=<degrees>
-ABTSM11CapturePitch=<degrees>
-ABTSM11CapturePower=<0..1>
```

三项必须同时出现；缺项、非有限值或超出当前 Preset LaunchModel 时 fail closed。未提供时仍录制 NominalInput。Manifest 必须写 `launchInputMode`、三个输入值、Released/Playback Hash、是否使用自适应压缩及最终物理接触证据。

## 5. 阶段性验收里程碑

### M7-A：身份冻结与失败隔离

- Released Camera Result 的 Hash 精确等于 Released Playback Source Hash；
- 用不同 Hash 的 ShotPlan 必须拒绝消费；
- 导演 sample 不可用时输出一次降级日志，交互不得进入 Failed/Recovering。

### M7-B：自适应日程单元门

- 长窗口的阶段边界与 M3 基线数值一致；
- 短窗口仍包含严格有序的五个阶段，并标记 Adaptive Compression；
- ForegroundClear 以前不得提前拉远；各状态边界位置、旋转、FOV 连续。

### M7-C：两条随机 F4 录屏

- 从 Rank 11 当前可达域中以记录的随机种子选择两组不同输入；
- 两次均用 fresh UE 进程、`Rank=11`、`Stylized=1`、`DirectorM3=1`；
- Manifest 均为 `Complete/PhysicalContact`，最终鸟心到 UFO 为 `800 cm` 容差内；
- 两次输入和 Released Hash 都不同；无 Director fallback、无镜头状态 Unavailable、无失败黑屏；
- 逐段抽帧检查三次行星穿越、两次桥接和 UFO 收束。结果只证明这两条样本及实现结构，不等价于 Rank 11 全域认证。

## 6. 2026-08-11 实施证据

- UE 5.8 Development Editor `-ForceUnity -DisableAdaptiveUnity` 完整链接成功；
- fresh NullRHI `ABTS.M11C.Unit.FlightCameraAuthorityFrame`、`ABTS.M11C.CameraCapture.Config`、`ABTS.M11C.M7.RandomF4Witnesses` 均精确发现并通过 1/1；
- 随机种子 `0x4d3757a1`，从 Rank 11 nominal 邻域按确定性扰动搜索；只接受 `Classification.IsF(4)`、可构造 800 cm Candidate Presentation Contact 且可构造 M7 ShotPlan 的不同 Result Hash；
- 样本 A `M7CustomF4-A-Final-20260811-183000` 输入 `Yaw=-1.781091547476, Pitch=26.290847635878, Power=0.999426916514`，实际录制 Released Hash `0x668901FF090C69FE`、Plan Hash `0x525966177CEBF5FF`，1014 帧；
- 样本 B `M7CustomF4-B-Final-20260811-183500` 输入 `Yaw=-1.849907256661, Pitch=26.290295839910, Power=0.995681941009`，实际录制 Released Hash `0xCC31B4566FD9728F`、Plan Hash `0xAD16A25431EE8C3E`，995 帧；
- 两份 Manifest 均为 `Complete/TargetHit`、`m4PhysicalContactPassed=true`、`m4TerminalClosurePassed=true`，最终距离分别为 `800.000000000099/800.000000000039 cm`；CameraPlan Hash 各自精确等于 Released Hash，CSV 的 `Unavailable` 为 0，日志没有 `CameraDirectorFallback`；
- 两条窗口都足以保留标准 M3 节奏，故 `m7AdaptiveShotCompression=false`。短窗口压缩由合成单元 fixture 覆盖，本轮随机样本没有伪造压缩场景；
- 最终二进制重录的 AVI SHA-256 分别为 `94A7B37C161842C3B1B598C7FCD348237C1E30933361144C10110D36575AFFFA` 与 `FACD9EF8AB647120D475914952B0C43F2D36C99C57CD3B9CAC9ACEBDAD7709CA`。逐段抽帧确认三颗行星、双星远景、拖尾和 UFO 接触构图均进入像素通道。

注意：Automation 中随机数先以 float 生成再提升为 double，日志只打印 12 位；将打印值作为命令行重放会形成邻近但不同的权威 Hash。本次验收以录屏 Manifest 内的精确输入及实际 Released Hash 为最终身份，两条路径仍均由 fresh 进程重新求解并通过 F4 发布链。
