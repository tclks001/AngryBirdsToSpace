# M11-B：终局局部布局搜索与全输入域认证

> 状态：M11.0 与 M11-A 已完成；M11-B C++、Development Editor 编译、全新进程 Unit/Runtime/ConstructiveSearch/FullInputDomain 自动认证和用户 PIE 均已完成，v1 预设与认证 Hash 已冻结；现已正式交接至 [M11-C](M11CFinaleInteractionAndPlaybackDesign.md)。
>
> v2 说明：本文以下数值和 Hash 仍是已验收的 v1 权威；M11-B v2.1 已生成 4 个未认证候选并完成快速同源重放，详见 [M11-B v2.1 候选搜索子稿](M11B21CandidateSearchDesign.md)。强助推、非共线、60 秒节奏和三维域重认证的总边界见 [M11 v2 优化总设计](M11V2FinaleOptimizationDesign.md)；完整输入域认证仍留到体验冻结后的 M11-B v2.2。
>
> 父级：[M11 终局三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md)。
>
> 总设计：[AngryBirdsToSpace 游戏设计稿](AngryBirdsToSpaceGameDesign.md)。
>
> 上游：[M11.0 终局前置收口](M110PreFinaleClosureDesign.md) · [M11-A 纯数据引力弹弓求解器](M11AGravityAssistSolverDesign.md)。
>
> 下游：[M11-C 终局轨道交互、全景 HUD 与确定性实飞](M11CFinaleInteractionAndPlaybackDesign.md)；本稿的自动认证和用户 PIE 已通过，交接门已解除。
>
> 交接入口：[ABTS 项目工作流](ABTSProjectWorkflow.md)。

## 1. 一句话目标

M11-B 使用 M11.0 输出的 `FABTSM110FinaleLocalFrame` 和 M11-A 的唯一积分器，离线搜索并冻结一套“主星 + 三颗静止助推行星 + 非引力 UFO”的终局局部布局；随后在完整、已声明的 `Yaw × Pitch × Power` 输入域内证明：

- 只有一个连通的最终成功输入岛；
- 成功只能按 `① → ② → ③ → UFO` 的顺序发生；
- 三颗助推缺一不可；
- 围绕该唯一 `F4` 成功族，`F1/F2/F3` 具有可玩的局部前缀内核与严格嵌套关系；
- M11-C 可以只消费认证预设和内接信赖域，不需要运行时重新搜索。

本阶段的“唯一”只限于冻结的 base + half-cell 发现合同与最终精度自适应闭包：在该有限采样合同下只有一个连通 `F4` 分量；不声称对连续实数域作数学上的绝对唯一证明，也不排除小于发现分辨率、未被该合同覆盖的数学微岛。`F1/F2/F3` 的分量数、差集、内宽和信赖域结论只针对围绕已发现 `F4` 族建立的最终精化闭包，不宣称证明远离该族的全域前缀微拓扑。

## 2. 本阶段交付与明确不做

### 2.1 必须交付

1. 一个无运行时随机回退的确定性两级离线搜索器；
2. 一套通过正式门槛的版本化 `FABTSM11FinaleLayoutPreset`；
3. `Yaw/Pitch/Power` 到 M11-A 初态的唯一 `FABTSM11FinaleLaunchModel`；
4. 完整输入域、网格、递归精度、连通规则和最大时域均冻结的扫描合同；
5. 全域 `F4` 分类/唯一性，以及围绕该族的 `F1/F2/F3` 局部分量、最小内宽、内接信赖域和助推消融报告；
6. 三颗只负责表现的助推行星 Actor，以及位于独立物理接触中心、只负责表现的 UFO Actor；
7. 预设编译、搜索确定性、完整域唯一性、前缀集合、旁路、消融和 Actor 权威边界自动化；
8. 同一 Task Graph World 中的局部布局实例化与 PIE 放置验收。
9. 一条从原始 Pouch 状态开始、以独立 800 cm UFO 为终点的冻结 nominal Physical Playback 轨迹及结果 Hash，供后续成功演出无瞬移重放。

### 2.2 本阶段明确不做

- 不实现完整轨道 HUD、简笔行星/UFO、逐目标远端预览；
- 不实现运行时 `NearFn/StableFn` 状态机、降敏、Clamp 或取消操作；
- 不接管 Space 弹弓瞄准、Release 或深空实飞；
- 不实现四鸟同袋、固定队列、白色小鸟救援和 UFO 攻击演出；
- 不切换星空、雾云、曝光，也不实现失败黑屏复位；
- 不使用独立终局地图，不保存绝对世界坐标；
- 不移动助推行星，不模拟真实公转或发射窗口；
- 不使用 Chaos 作为轨道权威，不从 Static Mesh Bounds 推导玩法半径；
- 不依赖延期中的 M10.1-D 通用道路外目标/走廊系统；
- 不在运行时搜索、优化或静默移动布局；
- 不显示或向 M11-C 暴露可直接呈现给玩家的标准答案轨迹。

## 3. 权威数据流

```text
Task Graph 已接受的 World
  -> FABTSM110FinaleLocalFrame
  -> 已认证 FABTSM11FinaleLayoutPreset
  -> PresetCompiler(Frame, Primary)
  -> FABTSM11GravityScenario
  -> FABTSM11FinaleLaunchModel(Yaw, Pitch, Power)
  -> FABTSM11TrajectoryRequest
  -> FABTSM11GravityAssistSolver::Solve
  -> FABTSM11TrajectoryResult
  -> PrefixClassifier(F1/F2/F3/F4)
  -> Connectivity / Width / Ablation / Bypass Report

同一 Preset + NominalInput
  -> BuildPhysicalPlaybackRequest
  -> 同一 M11-A Solver（同一初态、Bodies、步长和助推资格）
  -> 派生终端 = 独立 800 cm 几何 UFO
  -> Frozen PhysicalPlaybackContractVersion + TrajectoryHash
```

只有 `FABTSM11GravityAssistSolver::Solve` 可以推进轨道。搜索器、认证器、Actor、PIE 调试表现和后续 HUD 均不得复制中心引力、自然偏转、B-plane 或换能公式。

M11.0 的 `FABTSM110FinaleGravityScenario` 只保留前置四体角色与隔离合同。M11-B 的正式轨道编译目标是 M11-A 已落地的 `FABTSM11GravityScenario`；不得让两种 Scenario 各自维护一套不同的生产参数。

## 4. 局部坐标和发射模型

### 4.1 冻结坐标语义

沿用 `FABTSM110FinaleLocalFrame`：

| 局部轴 | 语义 |
| --- | --- |
| `+X / Forward` | 太空弹弓规范零角度发射方向 |
| `+Y / Right` | 左 Space 槽指向右 Space 槽，并对准卫星切向正向 |
| `+Z / Up` | 主星中心指向槽中点的径向外侧 |

本阶段新增并冻结：

- 正 `Yaw` 从 `+X` 朝 `+Y`；
- 正 `Pitch` 从局部水平面朝 `+Z`；
- 方向语义为先应用 Yaw，再应用 Pitch；
- 实现不得依赖 `FRotator` 的隐含欧拉顺序，而应使用下列显式公式。

令 Yaw 为 \(y\)，Pitch 为 \(p\)，均先转换为弧度。规范局部发射方向为：

\[
\mathbf d_\text{local}(y,p)=
\operatorname{normalize}
\begin{bmatrix}
\cos p\cos y\\
\cos p\sin y\\
\sin p
\end{bmatrix}
\]

因此：

```text
Yaw=0, Pitch=0     -> +X
Yaw>0, Pitch=0     -> 朝 +Y
Yaw=0, Pitch>0     -> 朝 +Z
```

世界方向只由 Finale Frame 的无缩放旋转变换得到，不受 Actor 当前朝向、相机朝向或 Static Mesh Pivot 影响。

### 4.2 `FABTSM11FinaleLaunchModel`

建议的数据合同：

| 字段 | 权威语义 |
| --- | --- |
| `LaunchModelVersion` | 发射映射版本，大于零 |
| `PouchLocalPositionCM` | 相对 Finale Frame 原点的弹珠袋中心；不改写 Frame Origin |
| `MinimumYawDegrees / MaximumYawDegrees` | 完整可输入 Yaw 域 |
| `MinimumPitchDegrees / MaximumPitchDegrees` | 完整可输入 Pitch 域 |
| `MinimumPower / MaximumPower` | 完整规范化 Power 域 |
| `MinimumLaunchSpeedCMPerSec / MaximumLaunchSpeedCMPerSec` | Power 端点对应的初速 |
| `MaximumSimulationTimeSeconds` | 正式扫描和发射请求共享的最大时域 |

Power 到初速冻结为线性映射：

\[
\alpha=
\operatorname{clamp}
\left(
\frac{P-P_{\min}}{P_{\max}-P_{\min}},
0,1
\right)
\]

\[
v(P)=v_{\min}+\alpha(v_{\max}-v_{\min})
\]

\[
\mathbf r_0=\operatorname{FramePosition}
(\mathbf r_{\text{pouch,local}})
\]

\[
\mathbf v_0=
\operatorname{FrameDirection}
(\mathbf d_\text{local}(y,p))\,v(P)
\]

请求固定：

```text
InitialTimeSeconds = 0
InitialExpectedAssistIndex = 1
SolverVersion / HashSchemaVersion = 认证合同冻结值
```

改变 Power 只改变速度大小，不改变方向；改变 Yaw/Pitch 只改变方向，不改变速度大小。M6 拉袋几何不得反向修改这份映射。

### 4.3 v1 冻结数值

以下值来自 `FABTSM11FinaleLayoutPreset::MakeCertifiedV1()` 与同版本 Scan Contract；测试夹具中的临时值不属于生产默认值。

| 项目 | v1 冻结值 |
| --- | --- |
| `PouchLocalPositionCM` | `(0, 0, 180) cm` |
| 完整输入域 | `Yaw=[-18°, 18°]`，`Pitch=[0°, 60°]`，`Power=[0, 1]`，闭区间且包含端点 |
| Power 到初速 | 线性映射 `400–1050 cm/s` |
| 认证标准输入 | `Yaw=0°`，`Pitch=30°`，`Power=0.975` |
| 最大飞行时域 | `700 s` |
| Solver 固定步长/位移预算 | `1/120 s`，单步位移预算 `200 cm` |
| base 发现步长 | `Yaw=1.5°`，`Pitch=2°`，`Power=0.025` |
| half-cell 发现 | 在 base 三轴各偏移半格；仍覆盖同一完整输入域的内部格 |
| 最终精度 | `Yaw=0.1875°`，`Pitch=0.25°`，`Power=0.003125` |
| 边界细化 | 深度 `3`；发现策略版本 `1`；coarse halo `1` 格；最多 `3` 轮、`250000` 个细化样本 |
| 连通与绕行 | 三维规则网格 `6-neighbor`；最多 `1` 个完整主星绕行 |
| `F4` 最小三轴内宽 | `Yaw=0.375°`，`Pitch=0.5°`，`Power=0.04` |
| 屏幕信赖域基准 | `1920×1080`、DPI `1.0`、最小宽度 `8 px` |
| 主星参考值 | 半径 `10000 cm`，`Mu=5.665×10^9 cm³/s²` |
| 合格终端拦截门 | `Target.CenterCM` 周围 `HitRadius=16000 cm`；只有连续三次助推都达到 `CorridorQuality >= 0.95`、正能量和允许侧门槛时才可产生 `TargetHit` |
| F3 接近门 | 与终端拦截中心同源的 `TargetApproachRadius=24000 cm` |
| UFO 几何接触 | 独立 `GeometricContactCenterCM`，`GeometricContactRadius=800 cm`；只用于真实 UFO 表现位置和旁路/消融检测，不把 Mesh Bounds 当作权威 |
| 行星③虚拟动量 | v1 使用增强后的 `VirtualOrbitalVelocity=(-1353.280261, -2220.052372, 0) cm/s`，使缺失第三次换能的轨迹在远端与 UFO 几何接触球明确分离 |

改变其中任一玩法或扫描值都必须提升相应版本、改变 Hash，并重跑本稿第 8 节的完整认证。

### 4.4 v1 局部布局摘要

以下均为 Finale Local Frame 坐标，不能直接当作地图世界坐标手工摆 Actor。完整双精度值和全部 B-plane/半径/`Mu` 参数以 `MakeCertifiedV1()` 为权威：

| 对象 | 局部中心 cm | 虚拟速度 cm/s | 备注 |
| --- | --- | --- | --- |
| Primary | `(0, 0, -10000)` | — | 半径 `10000` |
| ① | `(97219.225601, -5700, -14094.375995)` | `(0, -650, 0)` | `Mu=8.0e7`，Influence `15000` |
| ② | `(138324.925973, -26497.024518, -37845.625614)` | `(-333.298028, -513.198820, -219.178913)` | `Mu=1.0e8`，Influence `22000` |
| ③ | `(190659.219286, -61253.272726, -64968.511900)` | `(-1353.280261, -2220.052372, 0)` | `Mu=1.3e8`，Influence `30000`；v1 增强动量 |
| 合格终端拦截中心 | `(233103.200250, -78974.321891, -87227.804625)` | — | `TargetApproach=24000`，`HitRadius=16000` |
| 几何 UFO 中心 | `(278058.940003, -112576.146690, -114647.405394)` | — | `GeometricContactRadius=800`；UFO Actor 放在这里 |

局部布局经过任意批准的刚体 Finale Frame 变换后保持认证等价。严禁把本表的小数截断值重新写回预设；它们只用于人读排查和 PIE 构图核对，Hash 仍绑定代码中的完整 IEEE-754 值。

## 5. 版本化局部布局预设

### 5.1 `FABTSM11FinaleLayoutPreset`

预设至少包含：

| 分类 | 必须字段 |
| --- | --- |
| 版本 | `PresetVersion`、兼容的 `GeneratorVersion`、`FrameLayoutVersion`、`LaunchModelVersion`、`SolverVersion`、`HashSchemaVersion`、`ScanContractVersion` |
| 主星兼容 | `ReferencePrimaryRadiusCM`、`ReferencePrimaryMuCM3PerSec2`、批准容差 |
| 发射 | 完整 `FABTSM11FinaleLaunchModel` |
| 三颗助推行星 | 稳定 BodyId/Role、局部中心、视觉/碰撞/作用/参考/淡出半径、`Mu`、虚拟速度、B-plane 参考轴/备用轴/目标/内外椭圆/允许侧、能量上下限、调试颜色 |
| UFO/终端目标 | 稳定 TargetId、合格终端拦截中心与 HitRadius、TargetApproachRadius、独立几何接触中心与半径、连续合格助推门槛、局部表现 Forward |
| 前缀门槛 | 每颗助推最小认证走廊质量、最小认证正能量增量 |
| 认证 | 标准输入、Preset Source/Preset Hash、Canonical ScenarioHash、ScanContractHash、CertificationHash、NominalTrajectoryHash、PhysicalPlaybackContractVersion/TrajectoryHash、CertifiedBundleHash、`F1/F2/F3` compact trust regions 与各自 RegionHash |

`FrameLayoutVersion` 明确对应现有 `FABTSM110FinaleLocalFrame::LayoutVersion`；`PresetVersion` 表示本预设自身的数据语义。二者不得共用一个模糊的“LayoutVersion”字段。

### 5.2 主星尺度策略

v1 不对认证布局做自动尺度缩放。运行时主星半径和 `Mu` 必须在预设批准容差内匹配参考值，否则拒绝实例化。

原因是只缩放局部位置和半径会改变轨道动力学；若未来希望支持不同主星尺度，必须同时定义长度、速度、时间和 `Mu` 的相似变换，提升 Preset/ScanContract 版本并重新执行完整域认证。

### 5.3 从局部预设编译 M11-A Scenario

编译规则：

- 行星/UFO 局部中心使用 Frame 的刚体位置变换；
- `VirtualOrbitalVelocity` 使用 Frame 旋转，不叠加 Frame 平移；
- B-plane 参考法向、备用轴和 UFO PresentationForward 只旋转；
- 所有半径、`Mu`、能量限额和稳定 ID 保持预设值；
- Primary 中心、半径和 `Mu` 来自已验证的主星权威数据；
- M9 卫星、HISM、SDF、建筑、天空和雾云没有任何编译入口；
- 编译后必须调用 `FABTSM11GravityScenario::IsValid`；
- 运行时 `ScenarioHash` 与预设/Frame/Primary 兼容身份绑定，不从 Actor 名称或 Mesh 资产路径推导。

Canonical 离线认证可在标准局部 Frame 中完成。运行时只要 Frame 是无缩放正交刚体变换、主星参数兼容，逆变换后的轨迹与 Canonical 结果应在批准数值容差内一致；不在每个 PCG Attempt 内重新跑完整三维扫描。

### 5.4 运行时拒绝条件

出现以下任一情况必须 fail closed，不得生成半套终局 Actor：

- Finale Frame 无效、有缩放、非正交或版本不兼容；
- Preset、LaunchModel、Solver、Hash 或 ScanContract 版本不兼容；
- Primary 半径/`Mu` 超出批准容差；
- 三颗作用圈相交；
- 任一天体超出 Primary 最大模拟域；
- Pouch 位于碰撞球、作用圈或主星模拟域之外；
- UFO 的 `TargetApproachRadius <= HitRadius`；
- 稳定 ID 重复、Role 顺序不是 Primary/①/②/③；
- Preset Source/Preset/Scenario/Scan/Certification/Nominal/Certified Bundle 或 Trust Region Hash 不匹配；
- 从 Pouch 到任一助推视觉球/UFO 视觉球的认证视线被主星视觉球及安全边距遮挡；
- 当前 World 的发射净空或终局空间合同不满足预设要求。

## 6. TargetApproach 与严格前缀成功集

### 6.1 三层目标几何：接近、合格拦截、实际接触

v1 不再把一个大球同时当作“成功门”和“真实 UFO”。目标数据分为三层：

1. `TargetApproachRadiusCM=24000 cm`：F3 的解析接近门；
2. `Target.CenterCM + HitRadiusCM=16000 cm`：求解器的**合格终端拦截包络**；
3. `GeometricContactCenterCM + GeometricContactRadiusCM=800 cm`：位于更远端的实际 UFO 几何接触球，也是 UFO Actor 的表现位置。

前两层共用终端拦截中心，并满足：

```text
TargetApproachRadiusCM > Target.HitRadiusCM
```

`TargetApproachRadiusCM` 是解析接近门，不产生引力，也不生成阻挡碰撞。它通过 M11-A 已公开的 `SweptSphereFirstHit` 在 `AssistExit3` 之后的权威点列上判定；不得用抽稀后的渲染折线或子步端点距离代替 swept 判定。

`TargetHit` 只表示轨迹已进入合格终端拦截包络，且求解器已经观察到三次**连续合格**助推。v1 的目标资格门为每次 `CorridorQuality >= 0.95`、正能量增量至少 `20000 cm²/s²`、允许飞越侧正确；这是比前缀训练走廊更严格的终端资格，不应把 `MinimumCertifiedCorridorQuality[i]=0.05` 的宽前缀门误写成最终命中门。若结果已经按正确顺序 `TargetHit`，则必然同时满足 TargetApproach。

独立几何接触由 `TargetContact` 事件记录，即使助推资格不足也不会被合格拦截包络掩盖。认证把“有几何接触但未达到 F4”计为 `BypassTargetHit`；正式全域结果为 `GeometricContact=0`、`Bypass=0`。

合格拦截 Result 会在较早的 16000 cm 包络处终止，因此不能把该点列末端直接延长或从中间续算。M11-B 另外提供 `BuildPhysicalPlaybackRequest`：它仍从原始 Pouch 状态和 nominal 发射输入开始，复用完全相同的 M11-A Bodies、固定步长、助推资格和积分核，只把终端球派生到独立几何中心/800 cm 半径，并用独立 Scenario domain、`PhysicalPlaybackContractVersion` 与 `PhysicalPlaybackTrajectoryHash` 冻结完整路径。该 nominal 播放轨迹用于证明“从发射到几何 UFO 存在一条无位置瞬移的确定性演出路径”；它不把 800 cm 球重新定义为本稿的可玩 F4 宽度门槛。

M11-C 不得据此假设 F4 内任意玩家输入都会穿过 800 cm 球，也不得静默把玩家输入改成标准答案。成功演出究竟从玩家输入保持连续，还是在玩家已取得合格拦截后采用一条明确、可见连续的演出接管策略，必须在 M11-C 单独冻结并通过预演/Release/速度连续性验收；无论采用哪种方式，都不能从拦截中心瞬移到 UFO 或改用 Chaos 猜测路径。

### 6.2 `ValidAssist(i)`

第 \(i\) 颗行星只有同时满足以下条件才是认证意义上的有效助推：

1. 有同一 BodyId/AssistIndex 的严格事件顺序：

   ```text
   AssistEnter_i < ClosestApproach_i < AssistExit_i
   ```

2. 在这些事件之前没有 `WrongOrder`、`BodyCollision` 或其他终止事件；
3. B-plane 符号符合该 Body Spec 的 `AllowedPassSide`；
4. `CorridorQuality >= MinimumCertifiedCorridorQuality[i]`；
5. `AppliedEnergyChange >= MinimumCertifiedEnergyGain[i] > 0`；
6. 该助推位在 `EnabledAssistMask` 中启用；
7. 事件数据、最近点和出口状态均有限且通过 M11-A 自身数值门。

只穿出 Influence、错误侧以 `q=0` 离开、获得负能量、仅擦过外圈或未达到认证质量，都不算 `ValidAssist`。

### 6.3 F1–F4 的冻结定义

对一个固定 `(Yaw, Pitch, Power)` 的完整权威结果定义：

```text
F1 =
    ValidAssist(1)
    且 AssistEnter_2 发生在 AssistExit_1 之后

F2 =
    F1
    且 ValidAssist(2)
    且 AssistEnter_3 发生在 AssistExit_2 之后

F3 =
    F2
    且 ValidAssist(3)
    且 AssistExit_3 之后的权威轨迹进入 TargetApproachRadius

F4 =
    F3
    且满足三次连续 Q>=0.95/正能量/允许侧资格
    且合格终端拦截 TargetHit 发生在 AssistExit_3 之后
```

每个样本保存它满足的最高 Prefix Level：`0..4`。扫描集合使用 superlevel 语义：

```text
SetFn = { Input | HighestPrefixLevel >= n }
```

由构造保证：

```text
F4 ⊆ F3 ⊆ F2 ⊆ F1
```

v1 的正式可玩性还要求在包围已发现 `F4` 族的最终精化闭包内存在非空差集：

```text
F1 \ F2 != ∅
F2 \ F3 != ∅
F3 \ F4 != ∅
```

因此玩家可以先稳定前缀，再继续细调，而不是三个稳定状态在同一采样点瞬间一起成立。

任何独立 `TargetContact` 若不同时满足 `F3`、正确三助推顺序和终端资格，必须记录为 `BypassTargetHit` 硬失败。不得让较大的合格终端拦截包络遮蔽较小的几何接触，也不能因为发生了 `TargetHit` 或 `TargetContact` 就直接计入 `F4`。

### 6.4 前缀主分量与信赖域

- `F4` 在完整域内必须恰有一个连通分量；
- `F1/F2/F3` 只发布最终精化闭包内包含该 `F4` 分量的局部主分量；
- 最终精化闭包内与最终解无关的次级前缀分量必须全部报告；
- 任一被该局部精化覆盖的次级分量若达到批准的最小可玩宽度，布局应拒绝，避免 M11-C 把玩家稳定到死路；
- 信赖域必须完全内接于对应主分量，不得用外接包围盒；
- 推荐按 Power 切片输出内接凸多边形或椭圆；
- 捕获域和释放域使用不同边界，但 Stable Clamp 的全部允许点仍必须位于对应 `Fn`；
- compact trust regions 必须满足：

  ```text
  TrustF3 ⊆ TrustF2 ⊆ TrustF1
  ```

- compact box 允许因保守内接而出现相邻层相等；严格差集由最终局部精化闭包内的 `F1/F2/F3/F4` 样本集证明，不要求三个矩形 box 都严格缩小；
- 信赖域只限制输入，不保存“朝标准答案移动”的向量，也不包含可直接显示的金色标准轨迹。

这里没有“全域 `F1/F2` 只有一个分量”的结论：远端只完成一两次助推、却无法接入最终 `F4` 的前缀支路不属于本阶段要交给稳定器的可玩内核。M11-C 只能消费上述局部 trust regions，不能把它们外推成完整输入域的前缀地图。

## 7. 两级确定性搜索

### 7.1 搜索与认证必须分离

优化器用于寻找候选，不负责证明候选可用。候选参数一旦进入正式认证即全部冻结；认证失败后只能返回搜索阶段生成新候选，不能在扫描过程中偷偷放宽走廊、命中球或能量上限。

所有搜索参数都必须有版本化上下界。尤其不能通过以下方式制造虚假的唯一解：

- 把合格终端拦截 HitRadius 放大到吞掉大量错误轨迹，或让它遮蔽独立 UFO 几何接触；
- 把 B-plane 宽度压到像素级；
- 把虚拟能量限额提高到任意擦边都能越过下一节点；
- 把完整 Yaw/Pitch/Power 域裁成只包住标准解的小盒；
- 把最大飞行时域缩短到尚未暴露旁路；
- 把 Power 固定为 1.0。

### 7.2 第一级：快速布局播种

使用固定 Seed 的确定性网格、Sobol 或 Latin-hypercube：

1. 在 Finale Local Frame 中播种三颗行星和 UFO 的受约束局部偏移；
2. 在冻结范围内播种 `Mu`、Influence/Reference 半径、虚拟速度、B-plane 中心/宽度、允许侧和能量限额；
3. 同时播种允许域内的标准 `Yaw/Pitch/Power`；
4. 使用简化 patched-conic 或低成本几何门筛选能按 `①→②→③→UFO` 接通的候选；
5. 淘汰：
   - 作用圈重叠；
   - 行星碰撞/参考球合同非法；
   - 低功率过早可达行星①；
   - 视线穿主星或超出模拟域；
   - Pouch/发射净空非法；
   - 错误顺序比批准顺序更容易接通；
   - 标准输入只有单点、没有初步扰动宽度；
6. 输出候选及每段预计飞行时间、入/出口状态、B-plane 中心和代价，不输出正式认证结论。

快速层不得复制一套“近似 runtime solver”作为最终权威；它只负责减少交给 M11-A 的候选数量。

### 7.3 第二级：M11-A 精化

对每个候选：

1. 编译为正式 `FABTSM11TrajectoryRequest`；
2. 只调用 `FABTSM11GravityAssistSolver::Solve`；
3. 用 UFO 最近距离、三次 B-plane 偏差、事件顺序、碰撞、总时间、走廊质量、能量阶梯和小扰动鲁棒性构造代价；
4. 使用固定 Seed/固定迭代预算的差分进化、CMA-ES、网格或等价确定性批处理，再用 Powell/Nelder–Mead 等固定规则局部精化；
5. 标准输入必须得到：

   ```text
   Enter1 < Closest1 < Exit1
   < Enter2 < Closest2 < Exit2
   < Enter3 < Closest3 < Exit3
   < TargetHit
   ```

6. 行星①的可达功率下界应落在约 `0.88–0.95`，最终值由 v1 完整扫描回填；
7. 高功率成功带至少覆盖两个实际 Power 输入档位或等效连续宽度；
8. 对候选周围作冻结的小扰动批量测试；
9. 波束保留前固定重放 nominal 与六个 face-neighbor；邻点步长必须使用 Scan Contract 的最终精度 `0.1875° / 0.25° / 0.003125`，不能误用粗发现步长。nominal 必须存活，且七点中至少 `4` 点（nominal + 至少三个邻点）通过当前前缀的走廊、正能量和允许侧门；
10. 将最优候选完全冻结后才进入全域认证。

`BuildConstructiveSeed` 的正式自动化证明固定搜索合同能确定性地产生一个三助推 F4 候选，并在重复运行时给出相同 SearchOutput/NominalTrajectory Hash；它不是运行时搜索，也不要求其未经后续离线精化的中间 Preset Hash 等于最终 `MakeCertifiedV1()`。只有完成后续精化和第 8 节认证的候选才可硬编码为生产 v1。

批准运行中的两次固定合同重放各执行 `2213` 次求解，并各淘汰 `383` 个几何非法候选和 `84` 个事件/走廊非法候选；两次都得到搜索阶段 `PresetHash=0x1d0d519420ef9bf4`、`TrajectoryHash=0x3cfb9fc14900b8ab`。这两个 Hash 只证明构造搜索输出可复现，不是第 9.2 节冻结的生产 Preset/nominal 身份。

### 7.4 v2.1 标准 C++ 候选库

v2.1 没有复用本节 v1 的生产预设或认证 Hash，而是共同编译 M11-A v2.1 标准 C++ Core，由标准 C++ `M11Search` 构造、精确求值、分类和排名；Python 只调度分片进程。一次冻结的 `256` 工作项搜索得到 `4` 个状态为 `Candidate` 的布局，其中首选 `Work=166` 的总时长为 `36.117 s`、最长 coast 为 `6.841 s`、三次实际偏转为 `0.606 / 0.500 / 0.532 rad`，并已通过 UE/CLI 逐字段快速同源重放。

该候选库只供 M11-C v2.1 Editor-only 手感比较；其 `CertificationHash` 与 `CertifiedBundleHash` 均为零，不得覆盖本文 v1 生产默认值。搜索合同、四候选身份、断点恢复和自动验收证据见 [M11-B v2.1 标准 C++ 候选布局搜索与快速同源重放](M11B21CandidateSearchDesign.md)。体验批准并冻结唯一候选后，仍须按第 8 节语义执行 M11-B v2.2 完整三维域、连通性、旁路与消融认证。

### 7.5 Rank 3 的 v2.2 预认证结果（2026-07-30）

用户批准 `abts.M11.CandidateRank 3` 的 PIE 手感后，v2.2 将
`CandidateSourceHash=0xed74ffaf0de8028f` 冻结为本轮唯一认证输入。标准 C++
`ABTSM11V22CertificationCLI` 直接编译生产 `M11Core + M11Search`，支持规范化全局
索引、分片归属、检查点、断点恢复、确定性合并、覆盖去重、样本聚合 Hash、前缀嵌套
检查和三维六邻域连通域统计；它不依赖 Unreal Editor。

稀疏闭区间三维预认证使用
`YawStep=2° / PitchStep=3° / PowerStep=0.025`，共 `16,359` 点，得到
`F1/F2/F3/F4 = 279/73/8/4`、六邻域分量 `2/2/2/2`，
`AggregateSampleHash=0xbb9a814baf62975c`。由于粗网格可能切断斜向窄桥，随即执行
半步复核：`1° / 1.5° / 0.0125`，共 `122,877` 点，得到
`2067/553/72/27`、分量 `2/3/9/9`，
`AggregateSampleHash=0x4c3a8c30abd283bb`。两轮前缀嵌套违规均为 `0`；
半步 F4 的 Power 索引范围为 `71..80/80`，没有低功率旁路，但存在九个离散
六邻域 F4 分量。

因此本轮在“唯一六邻域 F4”预认证门失败并按合同早停：没有运行正式全域边界递归、
消融、错序、重复助推、多圈、晚到旁路、Trust Region、UE parity 或 Certified
Bundle 冻结。Rank 3 仍是 `Candidate / NOT CERTIFIED`，不能进入 M11-C v2.2
生产绑定。随后已按诊断要求先做局部递归检查，不直接启动其他正式慢认证。

局部闭包取半步 F4 包围盒外扩一格：
`Yaw=[-4°,2°] / Pitch=[19.5°,34.5°] / Power=[0.875,1]`。第一层采用
`0.5° / 0.75° / 0.00625`，共 `5,733` 点，得到
`F=3622/2478/529/209`、分量 `1/1/9/22`，
`AggregateSampleHash=0xf5156dc972ec7ccb`；第二层采用
`0.25° / 0.375° / 0.003125`，共 `42,025` 点，得到
`F=26836/18886/4205/1664`、分量 `1/1/4/40`，
`AggregateSampleHash=0x535f8ecf5e638633`。第二层最大 F4 分量包含
`1522/1664` 点并包含 nominal；半步 27 个 F4 锚点中有 25 个落入该主分量，
其余两个仍在小分量。

为直接检查两个离群锚点到主分量之间的桥，第三层只扫描
`Yaw=[-3.25°,-0.75°] / Pitch=[20.625°,24.375°] /
Power=[0.875,0.95]`，步长为
`0.125° / 0.1875° / 0.0015625`，共 `21,609` 点。结果
`F=18243/18053/4355/1565`、分量 `1/1/1/32`，
`AggregateSampleHash=0x610409f7401fb916`。其中
`(-2°,22.5°,0.925)` 已与主分量连接，但
`(-3°,21°,0.9125)` 仍处于独立小分量；最大 F4 分量为 `1421` 点，其余
31 个分量均不超过 7 点。

这证明当前问题不是 F1/F2/F3 走廊整体断裂，而是第三次助推后“合格命中 UFO”的
F4 终端集合包含一个主成功岛和许多真实的窄碎片。继续提高同一布局的采样精度不能
把它声明为唯一成功族。下一步必须回到 B v2.1 调整目标终端几何、资格边界或重新
搜索布局，使这些碎片消失或并入主岛；不得改用 18/26 邻域绕过门槛。

### 7.6 UFO 位置微调实验（2026-07-31）

为验证是否可以保留三颗行星和玩家认可的 Rank 3 手感、只移动 UFO 来消除碎片，
认证 CLI 增加了不覆盖冻结候选的审计位移参数。每次运行先验证基础
`CandidateSourceHash=0xed74ffaf0de8028f`，随后同时平移 Target Hit Center 与
Geometric Contact Center，输出独立 `VariantSourceHash`；位移不改变引力、三次
助推或 F1/F2/F3 分类。Python 仅并发调度标准 C++ CLI。

搜索共精确评估 `108` 次、`105` 个不重复位置：

1. `X/Y/Z ∈ {-2000,0,2000} cm` 的 27 点全方向扫描；
2. 沿改善方向 `X/Y=2000..4000 cm / Z=-4000..-2000 cm` 的 1000 cm 细化；
3. `X/Y=-1000..3000 cm / Z=-8000..-4000 cm` 的延伸扫描；
4. 当前最优附近 `X=2000..3000 / Y=0..1000 / Z=-9000..-8000 cm`
   的 500 cm 精扫。

所有位置都保留 nominal F4，但没有一个达到唯一 F4。原位置在第一层局部网格中为
`F4=209 / Components=22 / Largest=55 / FragmentPoints=154`；最佳审计位置为
`Offset=(2500,1000,-9000) cm`，
`VariantSourceHash=0x6d9c2656f1b49ec1`，结果为
`F4=325 / Components=15 / Largest=266 / FragmentPoints=59`。这说明移动 UFO
能显著扩大主岛并减少碎片，但在已批准的“微调”范围内不能消除碎片。由于粗层仍
失败，没有必要为该位置运行更昂贵的第二/第三层递归，也不得生成派生 Candidate
或进入 PIE/正式认证。

后续修复不再继续扩大位移；应在 B v2.1 中联合优化 UFO HitRadius/终端到达方向门，
或重新搜索第三颗行星至 UFO 的末段映射。任何资格语义修改都必须提升 Search/
Manifest 合同并重新执行候选搜索、PIE 与 v2.2 认证。

### 7.7 Rank 3 终端拓扑联合修复实验（2026-07-31）

在 7.6 的最优 UFO 位移基础上，标准 C++ 认证 CLI 新增了仅用于诊断的
`TargetHitRadius`、到达速度锥、命中面锥和行星③位置偏移参数。所有分类仍由同一
积分器逐点执行；Python 只负责分发独立 CLI 进程和汇总 JSON，不参与数值判断。

实验依次得到：

- 将 HitRadius 从 `12000 cm` 缩小到 `4500..10500 cm` 会同时损失名义路径或增加
  碎片比例，`12000 cm` 仍是该布局的最好值；
- 速度到达锥收紧至 `3°..30°`、命中面锥收紧至 `5°..60°` 都只能裁掉成功点，
  最窄命中面锥仍为 `F4=16 / Components=12`，不能把碎片接回主岛；
- 固定 UFO 最优位移 `(2500,1000,-9000) cm` 后，以 `2000 cm` 粗步长和
  `1000 cm` 细步长微调行星③。最好粗层组合为行星③偏移
  `(1000,-3000,-1000) cm`，`F4=131 / Components=12 / Largest=112`，
  碎片点由 UFO-only 最优的 59 降为 19；继续在其附近微调 UFO 未再降低分量数；
- 对该最好联合组合执行半步 `25×41×41=42025` 点复核，结果为
  `F1/F2/F3/F4=26836/18886/2073/1083`、分量数 `1/1/38/32`、
  F4 主分量 `1000`、碎片 `83`。提高分辨率后碎片仍存在，排除粗网格对角连接假象。

因此，Rank 3 原始布局、UFO-only 变体和本轮行星③+UFO 联合微调变体均保持
`Candidate / NOT CERTIFIED`。本轮没有修改冻结 Catalog、生产默认绑定、Search/
Manifest 语义或 Certified Bundle。若继续保留 Rank 3 的前两段手感，下一步必须
重新构造行星③的 B-plane/虚拟动量到 UFO 的末段映射，而不是继续扩大 UFO、加入
终端方向裁剪，或改用 18/26 邻域掩盖真实碎片。

### 7.8 Rank 3 行星③至 UFO 末段重映射搜索（2026-07-31）

本轮保持 Rank 3 的 LaunchModel、主星、行星①②、积分器和输入域不变，只联合搜索：

- 行星③局部中心偏移；
- 行星③ B-plane 的 T/R 中心及 Sigma 比例；
- 行星③虚拟动量三轴增量；
- UFO 合格拦截中心与 HitRadius；
- 诊断性的终端最低走廊质量。

`m11_v22_terminal_mapping_search.py` 使用固定 Halton 序列产生参数，Python 只调度
标准 C++ CLI。每轮先以 462 点三维网格筛选，再把固定排序的前列候选提升到
5733 点；最佳研究候选继续执行 42025 点半步复核。共完成 1408 个主参数样本，
另有各轮精筛。搜索轨迹为：

| 轮次 | 最好 5733 点结果 | 说明 |
| --- | --- | --- |
| 全域 384 组 | `F4=210 / Components=11 / Largest=188 / Fragments=22` | 首次联合重映射 |
| 局部 256 组 | `130 / 5 / 124 / 6` | 围绕全域候选 207 |
| 半尺度 256 组 | `146 / 3 / 143 / 3` | 围绕局部候选 81 |
| 四分之一尺度 256 组 | `172 / 3 / 169 / 3` | 候选 131，进入拓扑平台 |
| 八分之一尺度 512 组 | `174 / 3 / 171 / 3` | 未越过平台 |

候选 131 的可复现诊断参数为：

```text
VariantSourceHash = 0xc6c5dca2ee75fb28
TargetOffsetCM = (2045.340, 2022.718, -8885.799)
Assist3OffsetCM = (1864.062, -1883.951, -345.280)
Assist3BPlaneDeltaCM = (731.050, 1622.652)
Assist3BPlaneSigmaScale = 1.193424
Assist3VelocityDeltaCMPerSec = (-955.286, 1384.235, -1302.886)
TargetHitRadiusCM = 12000
```

其 42025 点半步结果为 `F1/F2/F3/F4=26836/18886/2263/1373`，分量数
`1/1/15/12`，F4 主分量 `1352`、碎片 `21`。它相对 7.7 最佳联合微调的 83 个
半步碎片减少约 75%，但仍不合格。把终端最低走廊质量从 `0.05` 提高到 `0.95`
不改变任何 F4 点，说明碎片并非低质量擦边助推；HitRadius 缩小和沿第三次助推
方向前后移动 UFO 均使主岛比例变差。

因此候选 131 仅作为未认证研究基线保存，不加入 Frozen Catalog、不提供 PIE Rank、
不生成 Trust Region 或 Bundle。继续要求“原始全域 F4 严格单连通”时，需要改变
行星③之前的上游映射或引入新的、可证明连续的末段控制变量，而不是在当前局部
参数盆地继续缩小步长。

### 7.9 行星②上游映射搜索与单岛候选 353（2026-08-01）

在 7.8 末段候选 131 的行星③/UFO 参数保持不变时，本轮进一步开放行星②的
局部中心、B-plane T/R、Sigma 和虚拟动量共 9 个维度。固定 Halton 全域 384 组
搜索产生三个 5733 点 F4 单岛候选；围绕其中最宽的候选 80 再搜索 512 组，产生
四个新的 5733 点单岛候选。

跨尺度复核结果如下：

| 候选 | 5733 点 | 42025 点 F4 | 半步结论 |
| --- | --- | --- | --- |
| 全域 80 | `203 / Components=1` | `1601 / Components=8 / Fragments=8` | 粗层假单岛 |
| 全域 86 | `188 / 1` | `1543 / 4 / 11` | 粗层假单岛 |
| 全域 142 | `102 / 1` | `782 / 3 / 8` | 粗层假单岛 |
| 局部 39 | `173 / 1` | `1327 / 4 / 4` | 改善但未通过 |
| **局部 353** | `129 / 1` | **`1004 / 1 / 0`** | **跨尺度单岛** |

候选 353 的半步扫描还满足：`F1/F2/F3/F4=27713/20976/1491/1004`、
F1/F2 单连通、名义输入在 F4、NestingViolations=0。F3 全域有 6 个分量，但其中
只有一个分量进入唯一 F4；正式认证仍需在主成功族周围完成前缀信赖域闭包，并审计
其余 F3 分量不能通过边界精化重新进入第二个 F4 岛。

同一 42025 点扫描分别以 12 线程和 6 线程、独立输出目录运行，两次均
`Passed=true`，AggregateSampleHash 均为 `0x0baef62a673e8e55`。候选身份为：

```text
BaseCandidateSourceHash = 0xed74ffaf0de8028f
VariantSourceHash = 0xb3e0f00ca35d499a
Assist2OffsetCM = (-1344.726, 1739.712, -1105.200)
Assist2BPlaneDeltaCM = (-48.730, -1327.573)
Assist2BPlaneSigmaScale = 0.857348
Assist2VelocityDeltaCMPerSec = (80.096, -225.197, 77.338)
```

完整下游参数和证据冻结在
`Tools/M11Core/Candidates/Rank3UpstreamCandidate353.json`。该文件明确保持
`candidate_not_certified`，CertificationHash/CertifiedBundleHash 为零。为进行最终
PIE 手感复核，它以 Editor-only `abts.M11.CandidateRank 7` 追加到控制台列表末尾，
但不会进入生产绑定。下一步应暂停继续搜索，将候选 353
提升为唯一 B v2.2 完整认证输入，执行 base/half-cell、边界递归精化、消融/旁路、
信赖域和 UE parity。

接入 UE Frozen Catalog 后的 parity 进一步发现：候选 353 名义轨迹的
`TargetHit` 发生在 `Assist3 Exit` 之前。前述单岛来自 Core 预认证阶段临时使用的
“完成三次助推且曾命中”分类，而正式 UE 前缀语义要求
`Assist3 Exit → TargetApproach → TargetHit`。因此 Rank 7 只用于观察轨迹和手感，
不得宣称 runtime-qualified F4，也不得直接成为完整认证输入；后续搜索必须先把
终端事件顺序加入 Core F4 权威判定，再寻找或修复候选。

### 候选 353 的 F3 扩大微调（Candidate 21）

研究清单位于 `Tools/M11Core/Candidates/Rank3F3ExpansionCandidate21.json`。本轮认证
CLI 将 F4 收紧为 `Assist3 Exit` 之后发生 `TargetHit`，避免把第三行星作用区内的
提前接触误计为终局成功。以候选 353 为上游基线，局部调整 Assist3 出口映射，
并将 UFO 命中半径从 12000 cm 收紧为 6000 cm 后，half-cell 结果为
`27713 → 20976 → 1538 → 480`，其中 `F3/F2=7.33%`，高于候选 353 的
`1491/20976=7.11%`；F4 的 480 个样本构成唯一六邻域分量，名义输入也满足严格
F4。F3 仍含一个 1536 点主分量和两个边界样本，因此该结果仍是研究候选，不能
写入 Certified Bundle，也不替换 Rank 0。

该研究候选以 Editor-only `abts.M11.CandidateRank 8` 追加到列表末尾。Rank 7
继续保留候选 353 原始体验，便于同一 PIE 环境直接比较；Rank 8 的冻结身份为
`Source=0x617687274ed0c29a`、`NominalResult=0xaac8ba98079011fd`。它仍显示
`Candidate / NOT CERTIFIED`，不得被运行时默认选择。

## 8. 完整输入域认证

### 8.1 `FABTSM11LayoutScanContract`

正式 Scan Contract 至少冻结：

| 分类 | 内容 |
| --- | --- |
| 版本 | ScanContract、Preset、LaunchModel、Solver、Hash 版本 |
| 输入域 | Yaw/Pitch/Power 的闭区间边界 |
| 基础网格 | 三轴步长与端点包含规则 |
| 边界细化 | 最大递归深度、最终角度/Power 精度、固定访问顺序 |
| 连通性 | v1 使用三维规则网格的 6-neighbor；不得在不同运行中改成 18/26-neighbor |
| 时域 | 最大模拟秒数、最大步数、最大允许完整圈数/重入次数 |
| 可玩宽度 | `F4` 的最小 Yaw/Pitch/Power 内宽，以及局部前缀主分量的安全腐蚀 Margin |
| 目标门 | TargetApproachRadius、合格终端拦截中心/HitRadius、独立几何接触中心/半径，以及连续合格助推资格 |
| 屏幕映射 | 输入曲线、参考分辨率、DPI 与约 `8–12 px` 目标的换算结果 |
| 数值权威 | 完整 `FABTSM11SolverConfig` 和配置 Hash |

边界与步长的 v1 数值已经按第 4.3 节冻结；任何变化均使本次认证失效。

### 8.2 扫描算法

1. 按固定索引顺序遍历完整基础三维网格；
2. 每个样本从同一 Preset 和 LaunchModel 构造独立请求；
3. 保存终止原因、事件摘要、最高 Prefix Level、最小目标距离、三次助推质量和结果 Hash；
4. 从完整 base + half-cell 发现结果中确定全部 `F4` 种子，围绕其边界、Prefix Level 变化、终止类别变化或接近批准 Margin 的单元按固定顺序建立并递归扩展精化闭包；
5. 达到最终精度后，在该闭包内对 `F1..F4` 分别进行 6-neighbor flood fill；只有 `F4` 的“唯一成功族”结论回指完整声明域；
6. 记录局部闭包内所有前缀分量、体素数、输入包围范围、轴向内宽、与标准输入/F4 的关系；
7. 从最终解相关主分量执行确定性安全腐蚀，构造并逐边重放 compact trust regions；
8. 报告全部扫描点和细化点的稳定 Hash。

有限采样可能遗漏小于声明精度的极小岛。因此正式表述必须始终带上 Scan Contract；同时，批准的最小可玩宽度必须显著大于最终扫描精度，不能把“扫描没看到”当作接受像素级解的理由。

### 8.3 正式阻断门槛

- `F4` 非空且连通分量数严格等于 1；
- 标准输入位于该唯一 `F4` 分量内部，不在边界单元；
- 在最终局部精化闭包内，`F4 ⊂ F3 ⊂ F2 ⊂ F1` 且三个差集均非空；
- 该闭包内与 `F4` 相连的每个前缀主分量达到批准的最小 Yaw/Pitch/Power 内宽；
- `TrustF1/F2/F3` 完全内接、逐级嵌套，边界重放不逃出对应集合；
- 行星①只有接近最大功率的连续区间可达，低功率域不存在终局旁路；
- 全域只有正确顺序和批准飞越侧可进入 `F4`；
- 不存在跳星、错序、重复收割同一助推、多圈后重入，或提前进入合格拦截/接触几何 UFO 的成功路径；
- 过近撞星、过远衰减、错误侧不取得等额正助推；
- 目标 HitRadius 和三颗走廊宽度均处于冻结的玩法上/下限内；
- 搜索重跑和认证重跑生成相同预设、组件摘要和 Hash。

### 8.4 助推消融与能量阶梯

对完整输入域的 base + half-cell 发现网格至少重放：

```text
EnabledAssistMask = 0b111  // 正式
EnabledAssistMask = 0b110  // 关闭①
EnabledAssistMask = 0b101  // 关闭②
EnabledAssistMask = 0b011  // 关闭③
EnabledAssistMask = 0b000  // 全关
```

每个关闭方案的 `F4` 样本数都必须为 0。Mask 只关闭玩法能量交换，不删除行星解析碰撞和自然偏转；这正是“缺少该次净助推仍不可命中”的正式测试。消融的高精度复核复用完整掩码发现出的 `F4` 精化闭包，检查该已知成功族在缺失助推后不会残留命中或几何旁路；它不宣称对每个消融掩码重新发现并证明全域细尺度微拓扑。

能量阶梯同时要求：

| 状态 | 可以到达 | 不得到达 |
| --- | --- | --- |
| 无有效助推 | 行星① | 行星②、③、UFO |
| 仅完成① | 行星② | 行星③、UFO |
| 完成①② | 行星③ | 合格终端拦截包络与 UFO 几何接触球 |
| 完成①②③且资格不足 | TargetApproach | 合格终端拦截与 UFO 几何接触 |
| 完成①②③且三次均达 Q/能量/侧门 | TargetApproach 与唯一合格终端拦截成功岛 | — |

## 9. 认证报告与 Hash

### 9.1 `FABTSM11LayoutCertificationReport`

报告至少包含：

- 全部版本和 Preset/Scenario/ScanContract Hash；
- 搜索 Seed、候选数量、精化迭代预算和最终标准输入；
- 输入域边界、基础网格尺寸、细化点数量和最终精度；
- 终止原因及首次失败类别直方图；
- 最终 `F4` 精化闭包内 `F1/F2/F3/F4` 的样本数、分量数、主分量范围和最小内宽；
- 该局部闭包内三个差集的非空证据；
- compact trust regions 及逐边重放结果；
- 标准输入事件序列、飞行时间、三次最近点/B-plane/走廊质量/能量增量；
- 正式、三项单独消融和全关消融摘要；
- 低功率、错误侧、错序、跳星、重复助推、多圈重入、独立 GeometricContact 和 BypassTargetHit 计数；
- Canonical Frame 与至少两个刚体变换 Frame 的逆变换等价结果；
- 认证开始/结束时间、工具构建版本和最终 `CertificationHash`。

成功与失败报告都必须生成非零审计 Hash；Hash 覆盖 `bPassed/Failure`、closure、全部 base/half/refined 子报告及消融体，Suite 不能只信任可能已过期的缓存 `ReportHash`。大体积扫描明细可以输出为 JSON/CSV 与机器可读二进制；运行时 Preset 只保存 compact trust regions、必要摘要和报告 Hash。人类可读 Markdown 报告负责验收，不作为运行时轨道权威。

### 9.2 v1 冻结认证报告

2026-07-28 的批准运行在全新 `UnrealEditor-Cmd -NullRHI` 进程中实际重算，而不是读取旧报告。`CertifiedBundleHash` 绑定 Source/Preset、Scenario、Scan、Certification、Nominal、Physical Playback 合同版本/轨迹和三层 Trust Region；运行时即使把篡改后的字段重新签成自洽的局部 Hash，也必须因与冻结 manifest 不符而 fail closed。

| 身份 | 冻结值 |
| --- | --- |
| `PresetSourceHash / PresetHash` | `0x7dbf1ba71f67768e` |
| Canonical `ScenarioHash` | `0x62d86d29` |
| `ScanContractHash` | `0x8a6d71cf21e552c9` |
| `CertificationHash / SuiteHash` | `0x941684a72e11b27d` |
| Refined report Hash | `0xd41d668e50ba72c7` |
| `NominalTrajectoryHash` | `0x185d3b673c1d52af` |
| `PhysicalPlaybackContractVersion` | `1` |
| `PhysicalPlaybackTrajectoryHash` | `0xcac902c4183084af` |
| `CertifiedBundleHash` | `0xa219d69cf3f92af0` |

扫描与闭包结果：

| Pass | 覆盖/求解 | 关键结果 |
| --- | --- | --- |
| base 发现 | `31775` 个完整域格点，`3100` 次候选轨迹求解 | `F4=3` 个发现样本 |
| half-cell 发现 | `28800` 个半格偏移格点，`2880` 次候选轨迹求解 | `F4=2` 个发现样本 |
| 发现覆盖合计 | `60575` 个 base + half-cell 样本 | 完整声明域与半格偏移发现合同均覆盖 |
| 最终精度局部闭包 | 围绕完整掩码发现的 `F4` 族；网格 `Yaw=[-2.25°,3°]`、`Pitch=[27°,34°]`、`Power=[0.925,1]`，`21025` 个样本 | 局部 `F=(6244,1890,981,558)`；局部分量数 `(1,1,1,1)`；TargetHit `558`；GeometricContact `0`；Bypass `0` |
| Suite 总计 | `135025` 次轨迹求解，`1` 轮细化 | `Coverage=1`，`Closure=1`，最终 `F4` 恰有一个连通分量 |

base/half-cell 的粗粒度 `F4` 样本只是全声明域的发现种子；唯一成功族、宽度与局部信赖域门槛以最终精度闭包报告为准。该闭包中的 `(1,1,1,1)` 不外推为全域 `F1/F2/F3` 微拓扑结论。第三颗行星的虚拟动量采用增强后的 v1 值，使关闭③后的远端轨迹与 800 cm UFO 几何接触球明显分离；同时以 `MinimumQualifyingCorridorQuality=0.95` 排除强助推产生的远端次级 F4。四项消融均执行完整 base + half-cell 发现复核；表中的精化结果只是在完整掩码已发现 `F4` 族周围的局部复核：

| `EnabledAssistMask` | base Hit/Contact/Bypass | half-cell Hit/Contact/Bypass | 已发现族周围的局部精化 Hit/Contact/Bypass |
| --- | --- | --- | --- |
| `0x06`（关闭①） | `0 / 0 / 0` | `0 / 0 / 0` | `0 / 0 / 0` |
| `0x05`（关闭②） | `0 / 0 / 0` | `0 / 0 / 0` | `0 / 0 / 0` |
| `0x03`（关闭③） | `0 / 0 / 0` | `0 / 0 / 0` | `0 / 0 / 0` |
| `0x00`（全关） | `0 / 0 / 0` | `0 / 0 / 0` | `0 / 0 / 0` |

冻结的 compact trust regions 为：

| 前缀 | Yaw | Pitch | Power | `RegionHash` |
| --- | --- | --- | --- | --- |
| `TrustF1` | `[-1.125°, 0°]` | `[29°, 30.5°]` | `[0.9656250000000001, 0.984375]` | `0xe3eab799d8de550f` |
| `TrustF2` | `[-0.1875°, 0°]` | `[29.5°, 30°]` | `[0.9750000000000001, 0.978125]` | `0xd8ecbab103ab4f16` |
| `TrustF3` | `[-0.1875°, 0°]` | `[29.5°, 30°]` | `[0.9750000000000001, 0.978125]` | `0x10d7ed9bc9bd706f` |

标准输入产生 `66988` 个权威轨迹点，并在 `558.152725 s` 进入合格终端拦截包络；完整事件序列严格为 `①→②→③→TargetHit`。`TrustF2` 与 `TrustF3` 的几何 box 相同，但 PrefixLevel 和 RegionHash 不同，并分别逐边重放在对应局部集合内部；最终精化闭包中的 `F2 \ F3` 非空。F4 的最大实心 Yaw×Pitch 矩形为 `20×18 px`（参考 `1920×1080`、DPI 1.0），覆盖 `14` 个连续 Power 切片，范围 `[0.953125, 0.99375]`。

独立 Physical Playback 从同一原始 Pouch、时间零和 Assist1 期待态出发，按相同四体、固定步长与资格合同完成三次助推，只把终端换成 800 cm 几何 UFO；结果含 `80221` 个权威轨迹点，严格以三组 `Enter→ClosestApproach→Exit` 加最终唯一 `TargetHit` 共 `10` 个事件结束，几何接触次数为 `1`，终点距 UFO 中心 `800.000000000 cm`，最大相邻点距离 `8.613634456 cm`。轨迹状态有限，时间以固定步长或更细粒度严格递增。该路径的冻结身份是 `0xcac902c4183084af`；它只认证 nominal 成功演出路径，不扩大上方 F4 的可玩命中定义。

因此本报告批准的是：**在冻结的 base + half-cell 发现合同以及最终精度自适应闭包下，恰有一个可玩的 `F4` 分量。** 它不是连续实数输入域上的数学唯一性证明。

### 9.3 认证失效条件

以下任一变化都必须使旧报告失效并重新认证：

- Preset、LaunchModel、ScanContract、Solver 或 Hash 版本变化；
- Physical Playback 合同版本、派生 Scenario domain 或冻结播放轨迹变化；
- 任一局部位置、半径、`Mu`、虚拟速度、B-plane 或能量参数变化；
- TargetApproach、合格终端拦截中心/HitRadius、独立几何接触中心/半径或终端资格门变化；
- 主星参考半径或 `Mu` 变化；
- 输入域、步长、递归精度、最大时域或连通规则变化；
- 前缀质量/能量门槛或最小可玩宽度变化。

Static Mesh、材质、LOD、Nanite、环系统朝向和非权威视觉缩放变化不得改变 Preset 内容 Hash、Scenario 或成功输入岛。

## 10. Actor 与碰撞权威边界

### 10.1 `AABTSM11GravityBodyActor`

本阶段的三颗助推行星 Actor 是不可蓝图派生的原生 `final` 表现类型：

```text
UCLASS(NotBlueprintable) final AActor
  -> SceneRoot
  -> StaticMeshComponent（可空，仅表现）
```

硬约束：

- 不继承 `AABTSM2Planet` 或 `AABTSM9Satellite`；
- 不允许 Blueprint 子类或关卡蓝图覆盖其组件合同；
- 不 Tick、不模拟物理、不移动；
- Actor/Component 每次配置后都会重新强制关闭 Tick、Collision、Overlap、RigidBody Notify、Gravity、Damage 与 Navigation 影响；
- Static Mesh 不阻挡轨道鸟，不产生重叠 Gameplay 事件；
- Actor 只保存稳定 Role/BodyId、Preset 身份和表现资产；
- Actor Transform 只由 `Preset LocalCenter + Finale Frame` 构造；
- 不从 Mesh Bounds、Pivot 偏差或 Component Scale 回写 Body Spec；
- 用户后续替换火星/木星/土星网格时，轨迹和认证 Hash 不变。

### 10.2 `AABTSM11UFOActor`

UFO Actor 同样是 `UCLASS(NotBlueprintable)` 的原生 `final` 表现类型，只负责表现：

- 使用独立 `GeometricContactCenterCM` 和 PresentationForward；不得把较早的合格终端拦截中心当作可见 UFO 位置；
- 可提供后续白鸟/舱体挂点，但 M11-B 不播放剧情；
- `FABTSM11TargetSpec::HitRadiusCM` 是合格终端拦截权威，`GeometricContactRadiusCM` 是独立物理接触/旁路权威；
- TargetApproach 只由认证器/HUD 下游读取，不产生 World 阻挡；
- UFO 不产生引力，不进入四体 Bodies。

### 10.3 实例化边界

M11-B 可以提供显式的：

```text
InstantiateCertifiedLayout(Preset, FinaleFrame, Primary)
```

但它只负责：

1. 验证全部版本、Frame、Primary 和 Hash；
2. 一次性编译只读 Scenario；
3. 恰好生成三颗 Body Actor 和一个 UFO Actor；
4. 注册稳定 ID 到显式数组；
5. 失败时不保留半生成 Actor。

终局模式、瞄准、预览缓存、Space Release 和状态机由 M11-C 接管。本阶段实例化路径不得通过 `TActorIterator` 自动搜集重力体，M9 卫星即使可见也没有注册入口。

### 10.4 Editor PIE 诊断线框

为完成本阶段布局验收，`AABTSM11FinaleSystem` 在 Editor PIE 初始化成功时默认一次性绘制 persistent 诊断线框：

- 三颗助推行星各自的 Influence sphere；
- `TargetApproach`；
- `16000 cm` qualified-intercept sphere；
- 独立 `800 cm` physical-UFO sphere。

线框中心和半径只能由冻结 Preset 经 Finale Frame 单向变换得到，并以不同颜色和文字标签区分。它不是额外 Actor 或碰撞组件，不参与 overlap、navigation、Chaos、重力查询、命中分类或 Hash；不得从线框/可见 Actor Transform 反写求解器数据。该开关只在 Editor 默认值中用于 PIE 验收，Commandlet 和 packaged build 均不绘制该诊断层。

## 11. 自动化测试与执行结果

阻断断言统一使用 `ABTS.M11B` 前缀，具体映射为：

| 测试 | 阻断性断言 |
| --- | --- |
| `Unit.LaunchModelContract` | 正 Yaw 朝 +Y、正 Pitch 朝 +Z、Yaw 后 Pitch；Power 线性且不改方向；Frame 刚体变换正确；非法边界拒绝 |
| `Unit.ScanAndSearchContracts` | v2 强制 base + half-cell 发现；闭区间网格必须被步长整除并精确采样最大端点；搜索 Seed、预算合法，且使用 final-precision 邻点并要求 nominal+至少三个 face-neighbor 存活 |
| `Unit.CertifiedBundleIdentity` | Source/Preset/Scenario/Scan/Certification/Nominal/Physical Playback version+trajectory/Trust/Bundle 完整身份一致；源字段、播放合同、Trust 和 Bundle 篡改均 fail closed |
| `Unit.Connectivity6` | 冻结 6-neighbor 连通规则，拒绝只以角或棱相接的伪连通 |
| `Unit.CertificationHashCoverage` | Report/Suite Hash 覆盖 pass/failure、closure、全部消融子报告及缓存子 Hash；早期拒绝也产生非零审计身份与稳定原因 |
| `Unit.PrimaryOrbitLimit` | 主星完整绕行上限稳定阻断多圈旁路 |
| `Unit.NominalSequence` | 标准输入严格得到三次 `Enter<Closest<Exit` 后的合格 TargetHit，三次均满足终端质量/正能量/允许侧门 |
| `Unit.PhysicalPlayback` | 从原始 Pouch/时间零/Assist1 期待态开始，复用同一四体与资格合同；事件精确为三组 `Enter→Closest→Exit` 加最终 `TargetHit` 共 10 项；一次 800 cm 接触、冻结 Hash、有限状态、固定步长时间单调且无大于 500 cm 的位置跳变 |
| `ConstructiveSearch` | 固定 Seed/预算两次重跑均生成三助推 F4 候选，SearchOutputHash 与 NominalTrajectoryHash 完全一致；final-precision 六邻点满足 nominal+至少三个邻点的鲁棒门 |
| `Certification.FullInputDomain` | 完整 v1 base + half-cell 发现重算；F4 恰有一个成功族；围绕该族的局部精化闭包中三个差集、最小宽度和 compact trust regions 全通过；同时执行助推消融、几何接触/旁路和循环排除 |
| `Runtime.NativePresentationIsolation` | Body/UFO 均为不可蓝图派生的精确原生类型；即使测试先注入 Tick/Collision/Overlap/Navigation 等非法状态，重新配置也必须恢复纯表现合同 |
| `Runtime.CompatibilityBoundary` | Frame/Primary/manifest 不兼容时 fail closed；重新签名的篡改预设也不能越过冻结 manifest |
| `Runtime.ActorAuthority` | 恰好 3+1、Tick/Physics/Blocking 关闭；UFO 位于独立几何中心；换 Mesh/Scale 不改 Scenario/轨迹；尤其覆盖三颗 assist 已生成后 UFO 配置失败时 pending Actor 全部回滚 |

2026-07-28 的冻结版本已在全新 `UnrealEditor-Cmd -NullRHI` 进程中完成：

| 自动化入口 | 结果 | 证据 |
| --- | --- | --- |
| `ABTS.M11B.Unit` | `8/8` 通过：Launch/Scan/Search 合同、Certified Bundle、6-neighbor、Hash 覆盖、主星圈数门、标准序列与 Physical Playback | `Saved/Logs/M11B_Unit_Final.log` |
| `ABTS.M11B.Runtime` | `3/3` 通过：原生表现隔离、兼容/fail-closed 边界与 Actor 原子权威边界 | `Saved/Logs/M11B_Runtime_Final.log` |
| `ABTS.M11B.ConstructiveSearch` | `1/1` 通过：固定合同双跑及鲁棒候选一致性；每次 `2213` solves，`GeometryReject=383`、`EncounterReject=84` | `Saved/Logs/M11B_ConstructiveSearch_Final.log` |
| `ABTS.M11B.Certification.FullInputDomain` | `1/1` 通过；现场重算 `135025` 次轨迹并匹配冻结 Suite/Refined Hash | `Saved/Logs/M11B_FullInputDomain_Final.log` |
| `ABTS.M11A` 回归 | `8/8` 通过，包含 qualified-target 与独立 `TargetContact` 合同 | `Saved/Logs/M11A_Regression_AfterM11B.log` |
| `ABTS.M110` 回归 | `4/4` 通过 | `Saved/Logs/M110_Regression_AfterM11B.log` |

`AngryBirdsToSpaceEditor Win64 Development` 编译也已通过。主工作区有已打开 Editor 时使用不链接的编译检查；同源 scratch 工程完成全链接和上述全新进程运行，避免把热加载或旧模块误当作通过。

正式完整域测试可以单列为较慢的认证组，但阶段验收时必须在全新 `UnrealEditor-Cmd -NullRHI` 进程实际执行，不能只加载旧 JSON 宣称通过。快速 CI 可以验证冻结报告 Hash 和抽样，发布/阶段收口仍需重跑 Full Certification。

并行扫描如被采用，只能并行计算样本；结果必须按规范三维索引排序后再连通和 Hash，线程调度不得改变输出。

## 12. PIE 验收

M11-B 的 PIE 只验收布局实例化和权威边界，不提前验收 M11-C 的瞄准/HUD/实飞。

本次 C++ 实现**没有修改或迁移**生产地图/GameMode 资产；工作区中的 `Content/Maps/Test.umap` 不属于本次 M11-B 代码交付。开始 PIE 前，必须在实际验收地图的 World Settings 或对应 GameMode Blueprint 中接入 `AABTSM11GameMode`（或以它为父类的生产蓝图）及其 `AABTSM11FinaleSystem` 生命周期。若仍使用 M10/M11.0 GameMode，看不到三颗行星和 UFO 不是认证布局失败，而是资产入口尚未接线。

### 12.1 操作顺序

1. 使用已通过 M11.0 的 Task Graph World 进入 PIE；
2. 等待 `WorldReady=1` 和唯一 Space 弹弓槽/Finale Frame 有效；
3. 触发或自动执行认证 Preset 实例化；
4. 保持 Editor 默认的 `bDrawCertificationDebugInPIE=true`，观察三颗行星、位于独立几何中心的 UFO，以及一次性 persistent 绘制的三颗 Influence、24k TargetApproach、16k qualified-intercept 和 800 cm physical-UFO 线框/标签；
5. 记录 Frame/Preset/Scenario/Certification Hash；
6. 停止并重新进入 PIE，确认身份和局部位置复现；
7. 保持 M9 卫星存在，确认它不出现在 M11 Body 列表和调试积分中；
8. 若有替代测试网格，替换其中一颗表现网格，确认 ScenarioHash 和标准输入结果不变。

### 12.2 视觉与运行时门槛

- 同一 World 中恰好出现三颗助推行星和一个 UFO；
- 三颗行星运行时位置不移动；
- 从 Pouch 观察时，认证目标的视觉球不被主星及安全边距遮挡；
- 行星之间的作用圈不重叠，UFO 没有引力作用圈；
- Editor PIE 诊断线框分别与 Preset 的接近门、合格拦截包络和独立几何接触中心/半径一致；
- 轨道鸟或测试 Probe 不被 Static Mesh 阻挡、推出或停止；诊断线框没有碰撞实体；
- M9 卫星可见但 Body 列表仍严格为 Primary+①+②+③；
- 重进 PIE 不重复累加 Actor；
- 版本、Primary 或 Hash 故意不匹配时明确拒绝，且不留下半套 Actor；
- 不加载独立地图，不修改库存、Party、Space 装配或 Task Graph 结果。

建议日志：

```text
[ABTS][M11-B][FinaleSystem] Ready Generator=... FrameLayout=... Pair=... PresetHash=... ScenarioHash=... BundleHash=... Assists=3 UFO=1
[ABTS][M11-B][Debug] PIE overlay Assists=3 Approach=24000.0 Intercept=16000.0 Physical=800.0
[ABTS][M11-B][GameMode] Entry Ready=1 StartCell=...
[ABTS][M11-B][FinaleSystem] Rejected Reason=...
```

## 13. 实施顺序

1. 落地 LaunchModel、Preset、ScanContract、Prefix/Report 数据结构及严格校验；
2. 实现 `Preset + Frame + Primary -> M11-A Scenario/Request` 的唯一编译器；
3. 实现 PrefixClassifier、TargetApproach swept 判定和事件顺序检查；
4. 实现一级快速播种与二级 M11-A 精化；
5. 冻结候选，执行完整域扫描、6-neighbor 连通分析和边界细化；
6. 实现助推消融、旁路/多圈检查、最小宽度和 trust region 构造；
7. 生成机器可读报告、人类可读报告和认证 Hash；
8. 实现三颗被动 Body Actor、UFO Actor 及 fail-closed 一次性实例化；
9. 编译 Editor，运行 `ABTS.M11B` 全新进程自动化；
10. 回填第 4.3 节全部 v1 数值与批准报告；
11. 完成 PIE 放置/碰撞边界验收后，再交给 M11-C。

## 14. 阶段验收清单

### 14.1 数据与搜索

- [x] LaunchModel 的轴、符号、Yaw 后 Pitch 和 Power 线性映射自动化通过。
- [x] v1 的 Pouch、输入域、速度、网格、递归精度、宽度和时域已从代码认证预设回填。
- [x] 搜索固定 Seed/预算重跑产生相同 Preset 和 Hash。
- [x] 正式 Preset 不含绝对世界坐标或测试夹具参数。
- [x] Primary 尺度不匹配会拒绝，不执行未认证自动缩放。

### 14.2 轨迹因果与唯一性

- [x] 标准输入严格按 `①→②→③→UFO` 命中。
- [x] `F4` 在声明完整域内恰有一个连通分量。
- [x] 最终局部精化闭包内 `F4 ⊂ F3 ⊂ F2 ⊂ F1` 且三个差集非空；不宣称全域 `F1/F2` 微拓扑唯一。
- [x] 该闭包内每个最终解相关前缀主分量达到批准的三轴最小内宽。
- [x] `TrustF3 ⊆ TrustF2 ⊆ TrustF1` 且全部边界重放仍在对应集合内部。
- [x] 行星①可达下界处于批准的近最大功率连续区间，至少覆盖两个实际输入档。
- [x] 单独关闭①/②/③以及全关后，base 与 half-cell 完整域均无命中和 Bypass；围绕完整掩码已发现族的局部精化复核也为零。
- [x] 低功率、错误侧、错序、跳星、重复助推、多圈重入和 BypassTargetHit 均无成功路径。
- [x] 合格终端拦截包络与 800 cm 实际 UFO 几何接触分离；几何接触独立检测没有被较大 HitRadius 遮蔽。
- [x] 行星③增强虚拟动量和终端 `Q>=0.95` 门已进入 Source/Scenario/Bundle 身份；关闭③后不能到达独立几何 UFO。

### 14.3 Actor 与 World

- [x] Runtime 自动化中恰好实例化 3 个 Body Actor 和 1 个 UFO Actor。
- [x] Body Actor 不继承 M2 Planet/M9 Satellite，所有表现组件不阻挡、不模拟物理。
- [x] 换 Mesh、材质、LOD 或视觉缩放不改变 Scenario、轨迹或成功岛。
- [x] M9 卫星没有编译/注册入口，存在、隐藏、移动或改引力参数均不改变 M11-B 认证结果。
- [x] 失败实例化不留下半套 Actor，重复激活不重复累加。
- [x] 不使用独立终局地图，不在运行时搜索或静默移动天体。

### 14.4 自动化与 PIE

- [x] `AngryBirdsToSpaceEditor Win64 Development` 编译成功。
- [x] 全新 `UnrealEditor-Cmd -NullRHI` 的 M11-B Unit、Runtime、ConstructiveSearch 与 FullInputDomain 认证全部通过。
- [x] Full Certification 在本次运行中实际重算并生成匹配的报告 Hash。
- [x] M11-A 相关自动化回归通过。
- [x] M11.0 相关自动化回归通过。
- [x] PIE 中完成位置、可见性、作用圈、非阻挡、M9 排除和重入确定性验收。

用户已完成 M11-B PIE 验收。本稿全部阶段门槛均已关闭；当前下游实现、自动化和 PIE 口径见 [M11-C](M11CFinaleInteractionAndPlaybackDesign.md)。

## 15. M11-C 交接清单

M11-B 的冻结认证报告与用户 PIE 均已完成；M11-C 依照以下接口边界正式开发：

1. 只消费 M11-B 的已认证 `AABTSM11FinaleSystem`、`FABTSM11FinaleLayoutPreset` 和由其构造的 M11-A Request/Result；不在运行时重新搜索布局，也不扫描 World 猜测重力体。
2. 进入终局瞄准前必须校验 Frame、Generator、Layout、Solver、HashSchema、ScanContract、Preset Source/Preset、Scenario、Certification、Nominal、Physical Playback 合同版本/轨迹、Certified Bundle 和三层 Trust Region 身份；任一未知版本、零 Hash 或不匹配均 fail-closed，不降级到未认证预设。
3. `Yaw/Pitch/Power` 的输入轴、正方向、角度顺序、功率曲线和边界必须唯一复用本稿 LaunchModel，HUD、鼠标输入和 Release 不得各写一套映射。
4. 当前预测的 `F0/F1/F2/F3/F4`、TargetApproach、合格 `TargetHit`、独立 `TargetContact`、有效助推、错误侧、旁路和第一个不可恢复失败只由同一认证分类器解释，不从 Actor 距离或屏幕图形反推。
5. 前缀成功集稳定器只消费认证输出的 `TrustF1/TrustF2/TrustF3` 及其滞回边界；它可以降敏和限制到已找到的前缀内核，但不得吸向标准输入、替玩家寻找下一段或永久锁死取消操作。
6. 终局轨道 HUD 复用 [M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md) 的拟合平面、正交投影、自适应构图、主星经纬网和球后虚线语义；数据源改为当前 M11-A Result，不复制 M6/M9 轨迹积分。
7. 远端预览始终选择当前预测里最早未完成的行星；完成 ③ 后才切到 UFO。UFO 构图中心使用独立 `GeometricContactCenterCM`，但 F3/F4 判断仍使用 TargetApproach/合格终端拦截中心；未进入目标作用圈时只能显示真实最近接近失败，不能伪造成功接近。
8. Space Release 冻结当前输入和全部身份后，只用同一求解器求解并缓存权威位置、速度、事件和 Hash；深空移动按缓存插值，不使用 Chaos，也不查询 M9 卫星。M11-B 已提供从原始 Pouch 状态出发的 nominal `BuildPhysicalPlaybackRequest` 与冻结结果 Hash，证明可无位置瞬移抵达独立 800 cm UFO；它不是“从 16000 cm 拦截球内部续算”，也不证明 F4 中每个输入都会接触 800 cm 球。M11-C 必须显式设计并验收玩家 Release 与 nominal 成功演出的交接，保持预演可解释、位置/速度视觉连续且不隐藏吸向标准输入；不得简单在两个中心之间瞬移。
9. 三颗 `AABTSM11GravityBodyActor` 与 `AABTSM11UFOActor` 始终只是表现层；Mesh、材质、LOD、Pivot、组件 Scale 或调试可视化不得回写 Scenario、轨迹、命中或前缀状态。
10. 生产 PIE 前必须让实际地图/GameMode Blueprint 使用 M11-B 提供的 M11 GameMode/Finale System 生命周期；不得因当前编辑器资产未迁移而在关卡蓝图中临时复制实例化逻辑。
11. M11-C 自动化至少回归：同输入预演/Release Hash 一致、不同渲染帧率轨迹一致、Trust Region 边界重放、稳定器取消/重置、逐目标预览无抖动，以及 M9 存在/隐藏/移动/改参数均不影响终局结果。
12. HUD 和调试模式都不得向玩家显示标准轨迹、标准 `Yaw/Pitch/Power`、精确修正量或不可见答案吸附；认证标准输入只用于离线 golden/自动化。

正式交接顺序已完成：冻结认证预设与报告 → 全新进程自动认证及 M11-A/M11.0 回归通过 → 完成本稿第 12 节用户 PIE → 集成工作流状态更新 → 开始 M11-C。M11 专属工作树中的当前落实见 [M11-C 设计稿](M11CFinaleInteractionAndPlaybackDesign.md)；共享工作流由集成所有者更新。

## 16. 排错表

| 症状 | 根因优先级 | 修复 |
| --- | --- | --- |
| `F3` 与 `F4` 样本完全相同 | TargetApproach 不大于合格拦截 HitRadius，或扫描过粗 | 修正 TargetApproach 合同并提高认证精度；不得伪造 F3 |
| 出现多个 `F4` 分量 | 布局/走廊存在第二解族，或连通规则不稳定 | 返回搜索阶段；不得只删除报告中的次级分量 |
| F4 唯一但宽度只有一个样本 | B-plane/目标/输入精度形成像素级解 | 拒绝布局，扩大物理鲁棒宽度后重新完整认证 |
| 关闭某颗助推仍命中 | 能量阶梯失败、目标过大或存在自然旁路 | 调整布局和能量预算；不能只改成功分类器 |
| 关闭③后仍“擦到 UFO”但报告 Bypass=0 | 较大的合格拦截球吞掉了真实接触，或只统计 TargetHit | 独立 swept 检测 800 cm `TargetContact`；把未达 F4 的几何接触计为 Bypass |
| F4 出现远端次级分量 | 第三次虚拟换能形成另一族弱质量路径 | 保持 v1 的增强动量，用终端 `Q>=0.95` 资格排除低质量支路；任何调整都重跑完整认证 |
| PIE 没有三行星/UFO，但自动化全绿 | 当前地图仍使用旧 GameMode，或 Finale System 生命周期未接入 | 不改认证参数；让实际地图/GameMode Blueprint 继承/使用 M11 GameMode，并重新冷启动 PIE |
| Canonical 通过、旋转 Frame 失败 | 向量误用了位置变换，或轴/欧拉顺序不一致 | 使用显式方向公式和纯旋转变换 |
| 换 Mesh 后轨迹改变 | 从 Bounds/Component Transform 回写玩法数据 | 切断视觉到 Preset/Scenario 的反向写入 |
| PIE 鸟撞停在行星网格 | Actor 启用了 Blocking/Chaos | 关闭阻挡和物理；只消费 solver 解析事件 |
| M9 卫星改变终局结果 | 使用 World 扫描或 M9 引力查询 | 只从 Preset 编译固定四体数组 |
| 相同 Seed 报告 Hash 不同 | 并行归并顺序、浮点环境或未规范序列化不稳定 | 按规范网格索引排序、固定迭代与 HashSchema |
| 运行时反复执行全域扫描 | 把离线认证误接进 PCG/PIE 激活 | 运行时只做版本、Frame、Primary、净空和 Hash 兼容检查 |

返回总设计：[AngryBirdsToSpace 游戏设计稿](AngryBirdsToSpaceGameDesign.md) · 返回父级：[M11 终局三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md) · 上游收口：[M11.0 终局前置收口](M110PreFinaleClosureDesign.md) · 上游实现：[M11-A 纯数据求解器](M11AGravityAssistSolverDesign.md) · 下游实现：[M11-C 终局轨道交互、全景 HUD 与确定性实飞](M11CFinaleInteractionAndPlaybackDesign.md) · HUD 投影上游：[M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md) · 返回入口：[ABTS 项目工作流](ABTSProjectWorkflow.md)。
