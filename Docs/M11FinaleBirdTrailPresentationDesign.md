# M11 终局连续飞行拖尾演出设计

> 编码：UTF-8，简体中文。
> 状态：阶段 T0/T1/T2 已完成；T2-P 历史点粒子方案由 T2-Q 弧长蓝噪声彗尾替代；T2-R 已用纯 C++ 三层软 Sprite 替换默认 `DrawPoint`，并完成亮面清晰核心增强、编译、fresh 数学自动化与 Rank11 三渲二命令行录屏；2026-08-10 用户主观验收通过，镜头状态重构留给后续 T3。
> 所有权：`feature/m11-finale`。本文只涉及 M11 终局主控鸟的演出拖尾，不修改共享 Niagara、材质、地图、三渲二实现或候选/轨迹合同。

## 1. 本阶段目标与冻结项

本阶段只解决“主控鸟在完整终局实飞中持续拥有可读拖尾”，并同时撤销跨行星桥接对鸟体视觉模型的临时放大。拖尾必须独立于候选 Rank、镜头状态和 Stylized 开关；Rank 0–11、原渲染与三渲二都消费同一份已结算鸟位置。

下列镜头问题只记录、不在本阶段修复：

- `IncomingEntryMatch → Authority/Approach` 释放跟随误差造成倒退；
- `OutgoingHold → DualBodyBridge` 从局部构图跳到三主体拟合；
- `DualBodyBridge → IncomingTrack` 更换世界空间基底并丢失上一行星；
- `IncomingTrack/IncomingEntryMatch → Authority` 没有真正匹配后续 Lucy 构图；
- 同一路径在火星→木星、木星→土星重复产生相机位移、旋转和主体像素跳变。

这些问题统一由排错项 `M11-CAM-M3-007` 跟踪。拖尾验收完成后，再以“共享相机姿态、共享屏幕锚点、入口/出口导数连续”的状态重构关闭它；禁止用拖尾掩盖镜头硬切。

## 2. 结构

```text
ReleasedPlaybackPlan（只读确定性轨迹）
        |
        v
InteractionSystem::UpdatePlayback
        |  已结算 WorldPosition；不回写权威层
        +---------------------------> Bird Actor（原始恒定视觉尺寸）
        |
        v
M11 FinaleBirdTrailComponent
		|  两帧世界位置之间按弧长采样；跨帧保留距离余数
		|  64 cm 名义间距＋一维分层蓝噪声位置抖动
		|  法平面 2D 正态横移；速度相关寿命；最多 384 粒子
		v
Depth-tested screen-space soft-sprite proxy
        +----> 玩家主视图
        +----> M11 命令行 SceneCapture / JPG / AVI
```

实现是 M11 自有的纯 C++ `UPrimitiveComponent`，无需 Niagara System、项目 Material Instance、Blueprint 或地图引用。它只接收最终鸟世界坐标，并把上一帧到当前帧的世界运动线段按弧长连续消费。采样器保存“距离下一个粒子还差多少厘米”，因此同一条运动既可以一帧走完，也可以拆成任意多个帧，发射位置、年龄、横向速度和寿命都保持一致。

名义采样间距为 `64 cm`。第 `n` 个粒子只允许在自己的名义弧长分层内做确定性位置抖动，相邻间距由两个分层偏移之差得到，默认范围为 `0.60–1.40 × 64 cm`。这种一维分层蓝噪声保持长期平均密度和硬最小间距，同时打破严格等距点列与录屏帧率的周期锁相。每个采样点只生成一个粒子，不再把同帧粒子堆成一团。

粒子的横向速度严格位于出生时轨迹切线的法平面。两个法平面分量由粒子序号经过固定哈希和 Box–Muller 变换生成二维正态分布；径向 `3σ`/硬上限为出生时鸟速的 `1/5`。大量粒子停留在慢速密集核心，少量粒子向外脱离。每粒子的寿命从慢速核心的 `0.40 s` 线性降至速度上限处的 `0.18 s`，所以外层扩散越快、消散越快；同一帧线段内的粒子还按插值时刻直接获得 `(1-t)Δt` 的初始年龄和对应横向位移，不会整批从满亮度同时出生。

默认渲染路径使用 UE 5.8 `PDI->DrawSprite`，以同一张运行时生成的 `64×64` 径向软纹理绘制三层屏幕 Sprite：先绘制加性暖橙 Halo，再以 `SE_BLEND_AlphaComposite` 绘制深色 Contrast Shell，最后以同一混合方式绘制琥珀 Core。纹理在 `BeginTrail` 时由纯 C++ `UTexture2D::CreateTransient` 创建，RGB 与 Alpha 都编码径向衰减；RGB 也写入衰减是因为 Halo 的 `SE_BLEND_Additive` 为 One/One 加法混合，不能只依赖 Alpha 隐去矩形四角。Shell/Core 的顶点 RGB 按自身 Alpha 预乘，既在亮色行星表面压出局部对比，也避免暗背景上出现不透明黑珠。纹理使用双线性过滤、Clamp、关闭 sRGB/Streaming，不创建或保存任何 `.uasset`。代理按每个 View 的 clip W、画面高度和投影 Y 把目标像素直径换算为世界半径，因此近景与远景都保持稳定光点尺度。Halo 与 Shell/Core 使用不同的年龄衰减指数，并随年龄轻微扩散；`SDPG_World`、零 DepthBias 保持行星深度遮挡。

`abts.M11.FinaleBirdTrail.RenderMode=1` 是上述 Sprite 默认路径；`RenderMode=0` 只保留旧 `DrawPoint` 作为命令行 A/B 和故障回退，不再代表目标外观。若运行时软纹理创建失败，组件 fail closed 并停止拖尾，不以白色方块或无纹理 Sprite 冒充成功。该路径仍不依赖项目材质、Niagara、Blueprint 或地图资产。

由于共享 `ABTSRuntime.Build.cs` 归集成工作树所有，本阶段不增加 Niagara 或 `RenderCore` 依赖。组件每帧以 UE 原生 `MarkRenderStateDirty` 更新最多 384 粒子的代理；每粒子默认提交 Halo/Shell/Core 三个 Sprite，但三层复用同一张 64×64 纹理，没有新增纹理采样资源或项目资产。既有 Rank11 30 fps 证据的鸟速中位数约 `21905 cm/s`、99 分位约 `40964 cm/s`，按 64 cm 间距和速度相关寿命估算常态约 100–220 个活跃粒子，即每视图每帧约 300–660 次 PDI Sprite 提交，理论硬上限为 1152 次。每帧最多允许 96 次发射；超过说明发生异常大位移或极低更新率，组件清空历史并从当前点重新建尾，禁止跨跳变补出巨量粒子。T2-R 像素复验必须同时记录录制耗时。若以后性能证据表明代理重建超预算，应由集成工作树批准模块依赖后改成 Render Thread 动态数据更新，不能由 M11 越权修改 Build.cs。

## 3. 不变量

### 3.1 鸟体尺寸

- 发射前记录 BirdVisual 原始相对 Scale；实飞所有镜头状态均保持该值。
- `OutgoingHold`、`DualBodyBridge`、`IncomingTrack` 不再执行 `1×→10×→1×`。
- 重置、退出或失败恢复时显式恢复原始 Scale，防止中途失败泄漏演出状态。
- 拖尾可提高运动可读性，但不纳入 `BirdRadiusCM`，不改变镜头求解器的主体包围体。

### 3.2 权威与渲染正交

- 不改变 `ReleasedTrajectoryHash`、`PlaybackPlanHash`、事件、命中分类、碰撞或 Actor Transform 的来源。
- 拖尾不读取 Rank，不按行星或 ShotPhase 开关；发射开始即建立，直到播放终点保持连续。
- `abts.Rendering.Stylized.Enabled` 只改变集成渲染路径，不改变拖尾采样点、年龄或长度。
- `abts.M11.FinaleBirdTrail.Enabled=0|1` 仅供像素 A/B；默认 `1`。切换它不得影响鸟、镜头或轨迹。
- `abts.M11.FinaleBirdTrail.RenderMode=0|1` 选择旧 `DrawPoint`/新软 Sprite；默认 `1`，两种模式共用同一粒子历史。
- 目标预览 PIP 的 ShowOnly 列表不包含拖尾；终局主视图与正式录制 SceneCapture 包含拖尾。

### 3.3 时间、长度与遮挡

- 慢速核心寿命为真实演出时间 `0.40 s`，而不是轨迹模拟秒数；横向速度越高，寿命越短，硬上限粒子寿命约 `0.18 s`。T2 首录的 `0.90 s` 在线带方案的 UFO 终段形成跨整屏直线，故本阶段不重新延长核心寿命。
- 采样器消费世界弧长并跨帧保存距离余数；默认名义间距 `64 cm`，分层位置抖动比例 `0.20`，故相邻间距保持 `38.4–89.6 cm` 且长期密度不漂移。
- 每个弧长站点只生成一个粒子。出生横移速度是轨迹法平面的二维正态样本，径向 `3σ` 和硬上限均为鸟速的 `0.20`；粒子出生后保持该世界横向匀速，不再重抽方向或速度。
- 粒子按出生时间稳定存放；不同寿命导致中间粒子可先过期，因此使用显式稳定压缩，禁止 `RemoveAllSwap`。硬上限为 384。
- 每粒子基础年龄仍按自身年龄/寿命独立推进；Sprite Core/Shell 与 Halo 分别使用可调衰减指数，直径还可随年龄轻微扩散；基础衰减低于 4% 后不再提交，随后独立消失。
- 使用 `SDPG_World`、零 DepthBias；不透视行星，不切到前景覆盖层。

## 4. 生命周期

| 时机 | 行为 |
| --- | --- |
| 进入终局/新尝试 | 清空旧历史，缓存鸟原始视觉 Scale。 |
| `Launched` 第一帧 | 记录第一个已结算鸟位置，不凭空构造没有速度方向的粒子。 |
| `Launched` 后续帧 | 先按真实 `DeltaSeconds` 老化和横移既有粒子，再沿上一位置→当前位置的世界线段消费弧长余数；按亚帧时刻赋初始年龄/位移并稳定裁剪各自过期的粒子。 |
| `TargetHit`/失败可读停留 | 保留最后尾迹，不再改鸟或相机；随终帧一起作为演出证据。 |
| 重置、退出、恢复世界、EndPlay | 清空尾迹并恢复鸟原始视觉 Scale。 |

组件跟随 InteractionSystem Actor，但内部保存组件局部坐标；Actor 若发生整体移动，轨迹与其边界一起变换。输入含 NaN/Inf 时拒绝该帧，绝不污染渲染代理。

## 5. 阶段性验收里程碑

### T0：设计与问题冻结（完成）

- 独立设计稿明确拖尾与镜头状态重构的边界；
- `M11-CAM-M3-007` 完整记录现有结构性跳变和帧证据；
- M3 原设计删除“桥接期放大鸟体”作为当前方案的表述。

### T1：运行时结构与无资产接线（完成）

- M11 自有 C++ 轨迹组件由 InteractionSystem 默认子对象创建；
- 发射全程采样，不依赖 ShotPhase、Rank 或 Stylized；
- 鸟体跨镜头状态保持原始 Scale，世界恢复时显式复原；
- 自动化验证开始/弧长发射/分帧不变性/蓝噪声间距/亚帧年龄/法平面速度/寿命相关/稳定过期裁剪/384 粒子硬上限/清空；
- Development Editor 编译通过；无需共享 Build.cs、Config、Niagara、材质、Blueprint 或地图变更。

### T2：fresh-process 像素验收（完成）

用同一 Rank 11、Stylized 1、关闭原生体积云的命令行录制工作流做 A/B：

1. 默认拖尾 `Enabled=1` 录一版完整 AVI；必要时再用 `-ExecCmds="abts.M11.FinaleBirdTrail.Enabled 0"` 生成身份相同的无拖尾对照。
2. Manifest 必须 `Complete/TargetHit`，Rank 11 明确 `UNCERTIFIED`，JPG 连续，AVI 可解码。
3. 抽查发射后、三次 Approach/Periapsis、两次双星桥、UFO 终段：尾迹连续、接在鸟后、不会只在桥接状态突然出现。
4. 抽查盘前/盘后：盘前尾迹可见，盘后受深度遮挡；禁止穿透行星。
5. 逐帧或图像测量确认鸟体包围像素不再因 ShotPhase 的视觉 Scale 曲线发生 `1×→10×→1×`；远近变化只能来自相机透视/FOV。
6. 对照 Manifest/CSV 确认轨迹、计划和阶段身份不因拖尾开关变化。镜头跳变仍应如实保留并标记为 `M11-CAM-M3-007`，不能因此判本阶段失败或宣称 M3 已完成。

2026-08-10 最终 fresh 证据为 `Saved/M11CameraCaptures/M3TrailT2Final-20260810-150000/Stylized1/`：Rank11、Stylized 1、RenderVersion 19、M3 导演、`r.VolumetricCloud=0`，Manifest 为合同 v10、`Complete/TargetHit`、Authority `UNCERTIFIED`；JPG/CSV/AVI 均为 949 帧。AVI 171463186 bytes，SHA-256 `2A4AC1A918C4B9BA18C53FF6E7A23FDD861422CE0A2B044308DEFC1B88FC7FB5`；ReleasedTrajectory `0x505F3312AC8AE07F`、PlaybackPlan `0x76B24AB41B6E8B63`，与本轮前既有 Rank11 M3 证据一致。

抽查帧 40/230/330/360/400、650/670/730/770、880/898/906/913/923，尾迹从普通接近、三颗盘前穿越、两次双星远景持续进入 UFO 终段；桥接鸟体半径仅约 `0.34–0.55 px`，尾迹仍可读。状态机不再修改 BirdVisual Scale；帧 329→356 的鸟像素变化只来自相机/FOV，桥接起点不再出现历史 10 倍模型。当前 Lucy 路线把鸟编排在行星前方，没有可用的盘后像素样本；组件已使用 `SDPG_World` 零 DepthBias，若未来轨迹产生盘后段，仍须补一条遮挡像素回归，不能把结构设置写成既有像素证据。

本次 Manifest 仍如实报告鸟丢失 8 帧、位置跳变 7 帧、旋转跳变 10 帧、FOV 跳变 19 帧，最大位置/旋转单帧变化约 `273923 cm / 178.569°`。这些正是 `M11-CAM-M3-007` 和 M4 终段的开放镜头问题；T2 只放行拖尾结构、恒定鸟体尺寸与命令行录制接线，不放行 M3 镜头连续性。

### T2-P：按帧点状粒子替换（历史实现与像素基线完成，当前由 T2-Q 替代）

线带方案的逐帧复核发现两类耦合故障：采样点约每 12 帧成批过期，且 `RemoveAllSwap` 破坏历史顺序后，线段会连接到错误邻点；新段补入时整条相邻线段同时变亮，造成明显分节和周期闪烁。T2-P 不改鸟、轨迹或镜头，只把历史解释从“相邻点连线”改成“独立点粒子”。

- 自动化冻结：每次发射 4 粒子、全部位于 14 cm 扰动球内、至少存在离轴扰动、重置后扰动可复现、年龄顺序稳定、过期仅留下最新一簇、总数不超过 192；
- Development Editor 与 `-ForceUnity -DisableAdaptiveUnity` 都必须通过；
- fresh NullRHI 只证明粒子池、扰动、寿命、顺序与上限，不能代替像素验收；
- Rank11、Stylized 1 命令行录屏需逐帧检查：鸟与最近点之间不再出现固定 12 帧缺口，尾部亮度不再随簇补充整体脉冲，近景粒子形成轻微厚度而非一条单像素曲线；同时记录总帧数、耗时、AVI 可解码性与轨迹/计划 Hash。

2026-08-10 fresh 像素接线证据为 `Saved/M11CameraCaptures/M3PointTrailT2P-V4-20260810-162000/Stylized1/`：Rank11、Stylized 1、原生体积云关闭，Manifest 为 `Complete/TargetHit`，JPG/CSV/AVI 均为 949 帧，实进程录制约 22.55 秒；AVI 170362402 bytes，SHA-256 `797C67E691C5D5E893FE0ED7201658CFE0B9CDA95E115AE89C6073C9AB2B1733`。终段帧 906–918 可见沿鸟运动留下的独立点列，各点随年龄分别变暗、缩小和退出，没有恢复成连续线带；近景三维轻扰动由自动化冻结，最终视觉密度仍留给用户验收。ReleasedTrajectory `0x505F3312AC8AE07F`、PlaybackPlan `0x76B24AB41B6E8B63` 与 T2 基线一致；镜头位置/旋转/FOV 跳变仍为 7/10/19 帧，继续由 `M11-CAM-M3-007` 跟踪。

实现探索中的 `V1/V2` 曾尝试 `DefaultBokeh + DrawSprite`，但 UE 5.8 的这类动态 Primitive 没有把 Translucent/Masked sprite 纳入正式 SceneCapture 主通道，像素与无拖尾版本一致；`V3` 改用 `DrawPoint` 后可见，但 8 px 外点过大。最终 V4 使用 3 px/1.25 px。该探索证据只用于排除“粒子池未运行”，不得作为最终效果版本。

### T2-Q：弧长蓝噪声彗尾（代码/自动化/首轮 fresh 录屏完成，待主观调参）

T2-P 的逐帧主观复核发现新的时间混叠：每帧 4 个粒子共享中心和出生时刻；当相机跟随使拖尾每帧屏幕位移接近相邻簇间距时，旧粒子会被外观相同的新粒子替换到近似相同的屏幕位置，形成“打点计时器”和原地扭动的粒子团。严格等距弧长采样仍可能与输出帧率锁相，因此 T2-Q 同时重构纵向采样、亚帧时间和横向动力学。

- 世界轨迹按弧长重采样，跨帧保存到下一站点的剩余距离；一帧走完整段与拆成两帧的所有粒子位置、速度、年龄和寿命一致；
- 站点使用确定性一维分层蓝噪声：每个 64 cm 名义分层只放一个抖动点，默认相邻间距为 38.4–89.6 cm，打破周期点列但保留最小距离和长期密度；
- 每站点只出生一个粒子，并按线段比例直接获得亚帧年龄 `(1-t)Δt`，同帧新粒子不再同步满亮；
- 横移在轨迹法平面上服从二维正态分布，径向 `3σ`/硬上限为鸟速 `1/5`；横向速度越高，寿命从 0.40 s 越快降至 0.18 s，形成内层密实、外层稀薄的彗尾；
- 保留当前 3 px/1.25 px 双层 `DrawPoint`、世界深度遮挡和无资产接线；本阶段不评价透明软粒子、Niagara 或材质表现；
- 调参无需 Editor：可在录制启动命令中设置 `abts.M11.FinaleBirdTrail.SampleSpacingCM`、`BlueNoiseJitterFraction`、`LifetimeSeconds`、`LateralSpeedFraction`、`OuterLifetimeScale`；`Begin` 日志会打印实际生效参数。

自动化 `ABTS.M11C.FinaleBirdTrail.History` 冻结：跨帧距离余数、帧细分不变性、蓝噪声间距上下界与非等距性、亚帧年龄顺序、速度与轨迹切线正交、`1/5` 上限、慢核/少量外逸分布、速度—寿命负相关、重置可复现、不同寿命稳定删除、384 容量上限和清空。2026-08-10 Development Editor 构建成功，`-ForceUnity -DisableAdaptiveUnity` 6 actions 全链成功；随后 fresh NullRHI 精确 1/1 成功，日志为 `Saved/Logs/M11-ArcTrail-Final-20260810-172912-FreshAutomation.log`。

同日首轮 fresh 像素证据为 `Saved/M11CameraCaptures/M3ArcTrailT2Q-20260810-173359/Stylized1/`：Rank11、Stylized 1、M3、原生体积云关闭，Manifest 为 `Complete/TargetHit`，JPG/CSV/AVI 均为 949 帧；录制进程从启动到自动退出 50.67 秒，Manifest 采集区间 20.382 秒。AVI 为 172631216 bytes，SHA-256 `7230C4C20FE3471A1C59B042B0727EF52456298712A03C68680C532361D8FF1A`；ReleasedTrajectory `0x505F3312AC8AE07F`、PlaybackPlan `0x76B24AB41B6E8B63` 与基线一致。日志确认实际参数为 64 cm / 0.20 抖动 / 0.40 s 核心寿命 / 0.20 横速上限比例 / 0.45 外层寿命比例，未触发单次发射保护。

逐帧检查 38→39、50→51 与近景 329→334：粒子集合逐帧推进，未复现旧版约 12 帧整批补段，也未见同一屏幕位置的整团粒子周期扭动或同步闪亮；蓝噪声纵向间距和横向外逸已可见。远景桥接帧 430/700 能读成连续收束的彗尾；近景 245/329–334 仍明显读成稀疏橙色方点列，亮度层次偏弱。这说明 T2-Q 已解决主要采样/时序混叠，但当前 `DrawPoint` 外观与 64 cm 密度尚未主观定型；下一步只允许经命令行 CVar 做密度、寿命与横移幅度 A/B，不以本次首录宣称最终美术验收。

### T2-R：纯 C++ 三层软 Sprite（亮面清晰核心增强已验收）

T2-Q 的轨迹采样、出生时序和横向动力学保持不变，本轮只替换表现层。组件在开始拖尾时生成一张 `64×64` 径向软纹理；Scene Proxy 对每个粒子按 Halo → Contrast Shell → Core 的顺序提交三个 Sprite，并由 View 投影把命令行指定的像素直径换算成世界尺寸。Halo 保留加性柔光；Shell/Core 使用预乘 RGB 的 `SE_BLEND_AlphaComposite`，先在过亮背景上建立窄对比区，再写入有色核心。默认表现参数如下：

| CVar | 默认值 | 作用 |
| --- | ---: | --- |
| `abts.M11.FinaleBirdTrail.RenderMode` | `1` | `0=DrawPoint` 历史对照，`1=SoftSprite` 默认实现。 |
| `abts.M11.FinaleBirdTrail.HaloDiameterPixels` | `5.5` | 外层柔光直径，屏幕像素。 |
| `abts.M11.FinaleBirdTrail.ContrastShellDiameterPixels` | `4.4` | 亮面可读性的深色对比壳直径，必须位于 Halo 与 Core 之间。 |
| `abts.M11.FinaleBirdTrail.CoreDiameterPixels` | `3.0` | 内层琥珀核心直径，屏幕像素。 |
| `abts.M11.FinaleBirdTrail.HaloIntensity` | `0.30` | 外层加法亮度倍率。 |
| `abts.M11.FinaleBirdTrail.ContrastShellOpacity` | `0.45` | 深色对比壳的 AlphaComposite 不透明度。 |
| `abts.M11.FinaleBirdTrail.CoreIntensity` | `0.95` | 琥珀核心的预乘 RGB 辐亮度倍率。 |
| `abts.M11.FinaleBirdTrail.CoreCompositeOpacity` | `0.80` | 琥珀核心的 AlphaComposite 不透明度。 |
| `abts.M11.FinaleBirdTrail.Softness` | `0.68` | 运行时纹理径向软化程度；开始新一次拖尾时重建纹理。 |
| `abts.M11.FinaleBirdTrail.ExpansionFraction` | `0.15` | 从出生到消失的相对扩散量。 |
| `abts.M11.FinaleBirdTrail.CoreFadeExponent` | `0.60` | 亮核年龄衰减指数；较小值使核心保持更久。 |
| `abts.M11.FinaleBirdTrail.HaloFadeExponent` | `1.50` | Halo 年龄衰减指数；较大值使外层更早淡出。 |

这些参数都可通过现有录制命令的 `-ExecCmds` 设置，无需打开 Editor。例如：

```text
-ExecCmds="abts.M11.FinaleBirdTrail.RenderMode 1,abts.M11.FinaleBirdTrail.HaloDiameterPixels 5.5,abts.M11.FinaleBirdTrail.ContrastShellDiameterPixels 4.4,abts.M11.FinaleBirdTrail.CoreDiameterPixels 3.0,abts.M11.FinaleBirdTrail.HaloIntensity 0.30,abts.M11.FinaleBirdTrail.ContrastShellOpacity 0.45,abts.M11.FinaleBirdTrail.CoreIntensity 0.95,abts.M11.FinaleBirdTrail.CoreCompositeOpacity 0.80,abts.M11.FinaleBirdTrail.Softness 0.68,abts.M11.FinaleBirdTrail.ExpansionFraction 0.15,abts.M11.FinaleBirdTrail.CoreFadeExponent 0.60,abts.M11.FinaleBirdTrail.HaloFadeExponent 1.50"
```

自动化 `ABTS.M11C.FinaleBirdTrail.History` 冻结默认模式、运行时纹理成功/尺寸、`Halo > Shell > Core` 尺寸关系、Shell/Core 合成不透明度范围、Core/Halo 强度关系、Softness/Expansion/Fade 指数边界，以及十二个外观 CVar 已注册。最终三层版本以 `-ForceUnity` 完成 4 actions 全链，日志为 `Saved/Logs/M11-HybridContrast-v2-ForceUnity-20260810-UBT.log`；随后 fresh NullRHI 精确发现 1 项并通过 1/1，`Begin` 日志确认 `5.50/4.40/3.00 px`、`0.300` Halo、`0.450` Shell、`0.950/0.800` Core 参数实际生效，证据为 `Saved/Logs/M11-HybridContrast-v2-20260810-FreshAutomation.log`。

同日 fresh 像素证据为 `Saved/M11CameraCaptures/M3SoftSpriteT2R-20260810-182551/Stylized1/`：Rank11、Stylized 1、M3、原生体积云关闭，Manifest 为合同 v10、`Complete/TargetHit`，JPG/CSV/AVI 均为 949 帧，采集区间 21.852 秒。AVI 为 171761404 bytes，SHA-256 `6F46637CC422CC13064D523EF2FD9584888E207270A327A413A713CCFE062BA5`；ReleasedTrajectory `0x505F3312AC8AE07F`、PlaybackPlan `0x76B24AB41B6E8B63` 与 T2-Q 基线一致。日志确认 `RenderMode=1`、软纹理 `64×64` 且未发生创建失败。

逐帧复核近景 38/39、245、329–331 和远景 430/700：矩形边缘和橙色方块轮廓已经消失，单粒子读成带柔边的圆形暖金光点；远景仍形成连续、收束的彗尾。末段 906/912/918/924/948 显示粒子分别衰减和退出，没有整批闪灭。首版全加性 Sprite 的亮面问题由三层版本继续处理：最终 fresh 像素证据为 `Saved/M11CameraCaptures/M3HybridContrastV2-20260810-195545/Stylized1/`，Rank11、Stylized 1、M3、原生体积云关闭，Manifest 为合同 v10、`Complete/TargetHit`，JPG/CSV/AVI 均为 949 帧，采集区间 20.298 秒。AVI 为 171620610 bytes，SHA-256 `905A37B67687416C55B0B489D324A628AFD9575D44877568260CE5713298B452`；ReleasedTrajectory `0x505F3312AC8AE07F`、PlaybackPlan `0x76B24AB41B6E8B63` 未变。

火星 316–322、木星 554–560、土星 809–815 的旧/新双行序列均已生成到该录屏的 `ComparisonCrops/`。相较 `M3SoftSpriteT2R-20260810-182551`，亮面点由接近背景的白色高光变为带暖色核心和轻微暗边的光点；暗面仍保留加性外辉，七帧序列没有黑珠串、同步闪亮或逐帧跳变。该增强刻意保持克制，不以大面积暗描边替代光点；最终审美强度仍由用户验收。本轮不修改三渲二、材质或镜头状态。

2026-08-10 用户完成主观验收并确认通过；上述三层参数由此冻结为 T2-R 验收基线。后续镜头状态迭代不得隐式改变粒子采样、混合层次或默认外观参数，如需调整必须重新执行同等 fresh 录屏与亮/暗背景逐帧对比。

2026-08-11 的 PIE A/B 进一步发现：同一路线中 `RenderMode=0` 可见而 `RenderMode=1` 不可见。该证据排除了粒子历史、组件包围盒和主视图收集链，定位到运行时软纹理的资源生命周期。UE 5.8 的带数据 `CreateTransient` 已隐式初始化资源，旧实现随后再次 `UpdateResource`，可能让 SceneProxy 持有被异步替换的旧 `FTexture*`。当前实现改为先创建未初始化 CPU 纹理、设置属性并写入 mip，最后仅执行一次资源初始化；Begin 日志增加 `ResourceReady`。该修复不改变已验收的三层外观、采样、寿命或命令行参数。fresh NullRHI 与离屏 D3D 自动化均已通过，离屏 D3D 明确记录 `ResourceReady=1`；随后可见 PIE 已确认 Soft Sprite 资源正常出现。

同日第二轮 A/B 进一步区分了“资源不可见”和“运动中变淡”：Soft Sprite 资源现已可见，但运动时明显变淡、鸟停止后历史粒子变清晰。`r.MotionBlurQuality 0` 无效，`r.AntiAliasingMethod 0` 则立即关闭问题，证明根因是 TSR/TAA 对没有可靠运动矢量的细小半透明 PDI Sprite 做时域历史抑制。粒子实现没有出生后缓慢亮起的曲线；出生时亮度最高，随后只按年龄衰减。最终处理不修改已验收的 Sprite 外观或寿命，而由 M11 Flight Camera 在 Authority Follow 生命周期内作用域式选择 FXAA，并在 Reset/EndPlay 恢复先前 AA 方法。这样 PIE 和命令行录屏自动一致，也不会污染瞄准与其他玩法镜头。

2026-08-11 用户使用默认发射流程完成最终可见 PIE 验收：M11 终局相机接管后尾迹持续清晰，无需手动关闭抗锯齿；由此关闭资源生命周期与运动中时域淡化两项 PIE 问题。

### T3：拖尾定型后回到镜头状态（后续，不在本阶段）

- 先冻结 T2 可读性参数，再重构跨状态相机连续性；
- 共享桥接末姿态与 IncomingTrack 初姿态，EntryMatch 真正匹配 Authority 的位置、旋转、FOV 与屏幕锚点；
- 重新录 Rank 0/11 × Stylized 0/1，关闭 `M11-CAM-M3-007`。

## 6. 编辑器操作

本阶段没有必须在 Unreal Editor 中执行的步骤。无需打开 Editor、创建纹理/材质/Niagara、指定资产、修改 Blueprint、放置 Actor 或保存地图；软纹理在运行时由 C++ 生成，编译、自动化和 T2-R 录制都从命令行 fresh process 完成。

若未来决定把当前资产无关的运行时软 Sprite 升级为 Niagara Sprite/Ribbon 或美术材质，该工作会触及集成工作树拥有的共享 Niagara/材质与模块依赖，必须单独交给集成工作树设计和实施，不能把它作为本阶段的隐藏手工步骤。
