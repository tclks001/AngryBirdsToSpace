# M11-A：纯数据引力弹弓求解器实施与验收

> 状态：C++ 实现、Editor 编译与 `ABTS.M11A` 全新进程自动化已完成；本阶段不需要 PIE 视觉验收。
>
> 父级：[M11 终局三重引力弹弓算法预演](M11GravityAssistAlgorithmPrevisualization.md)。
>
> 上游：[M11.0 终局前置收口](M110PreFinaleClosureDesign.md)。
>
> 下游：M11-B 终局局部布局搜索、认证预设与完整 `Yaw × Pitch × Power` 输入域验证。
>
> 交接入口：[ABTS 项目工作流](ABTSProjectWorkflow.md)。

## 1. 本阶段交付边界

M11-A 交付一个不依赖 `UWorld`、`AActor`、Chaos、渲染帧率或随机数的双精度轨道内核。它负责：

1. 主星始终生效的中心逆平方引力；
2. 只对当前期望助推行星启用的平滑截断自然引力；
3. 作用圈完整生命周期、参考球自然克隆、最近点、B-plane 走廊和固定飞越侧；
4. 由虚拟公转速度换算的出站切向能量修正；
5. 作用圈、解析球碰撞、UFO、错序和最近点的确定性事件；
6. 固定顺序规范序列化的 64 位验证 Hash；
7. 中心束缚、自然偏转、正负换能、助推消融、稳定失败、步长收敛和高速 swept 检测自动化。

本阶段明确不生成三颗行星/UFO Actor，不搜索正式局部布局，不接管 Space 实飞，不接 HUD/全景图，不实现前缀稳定器、四鸟演出、星空切换或失败复位。以上分别属于 M11-B/C/D。

## 2. 纯数据职责

| 类型 | 权威职责 |
| --- | --- |
| `FABTSM11GravityBodySpec` | 固定角色、解析中心/半径、`Mu`、参考球、边缘平滑、虚拟速度、B-plane 走廊和能量上下限 |
| `FABTSM11GravityScenario` | 固定 `Primary + Assist1 + Assist2 + Assist3` 与非引力 UFO；验证稳定 ID、角色顺序、作用圈不重叠和模拟域边界 |
| `FABTSM11SolverConfig` | 版本、基础步长、固定最大细分、根求解、双曲线/B-plane/能量残差容差和三位助推消融 Mask |
| `FABTSM11TrajectoryRequest` | 不可变场景、配置、双精度初始位置/速度/时间和期望助推序号 |
| `FABTSM11TrajectoryPoint` | 每个权威子步的时间、位置、速度和主星比轨道能 |
| `FABTSM11TrajectoryEvent` | 有序事件及入/出速度、最近点、B-plane、走廊质量、自然/理想转角和能量变化 |
| `FABTSM11TrajectoryResult` | 完整点列、事件列、首个终止原因、已完成助推数、诊断和验证 Hash |
| `FABTSM11GravityAssistSolver` | 唯一公开 `Solve` 入口、主星比能诊断和解析球 swept 求交 |

M11.0 的 `EABTSM110FinaleGravityRole` 继续作为固定角色白名单。M9 卫星没有枚举入口，因此不能因 World 中存在、隐藏、移动或改参数而进入求解。

## 3. SolverVersion 1 数值合同

### 3.1 保守积分与确定性细分

保守段冻结为 velocity-Verlet：

\[
\mathbf r_1=\mathbf r_0+\mathbf v_0\Delta t+
\frac12\mathbf a_0\Delta t^2
\]

\[
\mathbf v_1=\mathbf v_0+
\frac12(\mathbf a_0+\mathbf a_1)\Delta t
\]

基础步长默认 `1/120 s`。若速度—作用圈尺度、速度—最小碰撞球、局部引力时间尺度或位置误差条件不满足，只允许按 `BaseDt / 2^Depth` 细分，最大深度固定；不读取渲染 `DeltaSeconds`。达到最大细分后仍不满足约束必须稳定返回 `AssistSolveFailed / SubdivisionLimitExceeded`，达到最大步数但尚未到模拟时限则返回 `AssistSolveFailed / StepBudgetExceeded`，不能降级为一个未经批准的大步，也不能误报物理 `Timeout`。

主星始终施力。只有 `ExpectedAssist` 在其 `InfluenceRadius` 内施力，外缘使用五次 smootherstep 从 1 平滑降至 0；非期望行星不施力，但其作用圈与碰撞球仍参与错序/碰撞扫掠。`AssistReferenceRadius` 必须位于完整引力区，即不大于 `InfluenceRadius - InfluenceBlendWidth`；初态不得位于任何助推作用圈内，也不得位于主星 `MaximumSimulationRadius` 之外。飞越侧枚举、根容差及版本号都在积分前验证，`RootAlphaTolerance` 是 `[0,1]` 内的无量纲步内比例，不得直接当秒数使用。

### 3.2 自然遭遇规划

每次遭遇按以下五个确定性阶段推进：

1. 穿入当前行星 `InfluenceRadius`，发出 `AssistEnter`，但不做规划或换能；
2. 入站穿入完整引力区内的 `AssistReferenceRadius`，从该解析交点克隆一条不带玩法 kick 的同源 Verlet 轨迹；
3. 在实际轨迹上锁定最近点，随后才允许出站换能；
4. 出站穿出参考球，在同一根位置、时间和速度上结清预算并检查能量残差；
5. 继续保留同一颗行星的自然引力，直到穿出 `InfluenceRadius` 后才发出 `AssistExit`、递增 `ExpectedAssist`。

因此淡出外壳的入站/出站引力严格成对，`U=0` 不会因参考球出口提前停用行星而凭空留下净能量。若轨迹只擦过 Influence 外圈而没有进入参考球，则稳定返回 `AssistSolveFailed / ReferenceSphereMissed`，不得递增 `CompletedAssistCount` 或 `ExpectedAssist`。自然克隆必须：

- 入站径向速度为负；
- `vInfinity²` 高于配置下限；
- 不进入解析碰撞球；
- 找到径向速度由负转正的最近点；
- 再次由内向外穿过同一参考球；
- 自然转角与理想双曲线转角差不超过冻结容差。

入/出渐近方向由参考球状态的行星中心双曲线轨道要素拟合，不把有限参考半径处的局部速度直接冒充渐近方向。明确的出站径向反转可终止为 `PlanetCaptured`；自然克隆的步数/时域预算耗尽必须是 `AssistSolveFailed`，不能伪装成物理捕获。其他无效双曲线与数值失败分别稳定终止为 `AssistInvalidHyperbola` 或 `AssistSolveFailed`，不采用最后一个数值状态继续飞行。

自然克隆只负责冻结遭遇参数，不能预先提交未来的物理终止。它预测到的 `BodyCollision / TargetHit / PlanetCaptured / SolarCaptured / Timeout` 只把遭遇切换为“等待自然终止”；权威求解器仍逐步推进并按真实时间比较 swept 根、径向根和请求时限。因此较早的 UFO 根或请求时限可以覆盖克隆中更晚的行星碰撞/捕获。克隆在最近点前后及三次能量归一化校准中都受同一个请求时限约束；只有数值合同本身失败才立即返回求解失败。

### 3.3 B-plane 与走廊

`T` 轴只由当前入射方向和 Body Spec 的 `BPlaneReferenceNormal` 构造。投影退化时只允许使用固定 `BPlaneFallbackAxis`；二者都退化则终止为 `AssistInvalidBPlaneBasis`，不能继承上一帧或临时选择世界轴。

规范 B 向量方向仍是“行星中心指向入射渐近线最近点”。内椭圆给 `q=1`，内外椭圆之间使用同一个五次平滑函数降至 0。错误飞越侧不得取得正助推，走廊不产生横向吸附。

### 3.4 虚拟动量换能

自然克隆先计算：

\[
\Delta\epsilon_\text{raw}
=\mathbf U\cdot(\mathbf u_\infty^+-\mathbf u_\infty^-)
\]

再经过 Body Spec 正负上限、飞越侧、消融 Mask 和 `q^p` 得到本次预算。入站不 kick；过近星点后，使用自然克隆冻结的

\[
\kappa(s)=30s^2(1-s)^2
\]

按每个真实子步的 `κ(s)Δt` 归一化分配。每次只改当前速度大小：

\[
v'^2=v^2+2\Delta\epsilon
\]

方向不变，也不改位置。SolverVersion 1 冻结 `EnergyShootingIterationCount=3`：每次都从自然最近点开始，用修正后轨迹的真实自适应子步重新标定 `Σκ(s)Δt`；正式轨迹按同一核消费预算。若修正轨迹提前到达参考球出口，剩余预算在同一解析根上结清；随后以自然出口的同一主星势能约定检查残差。负根、非有限状态、无法退出或残差超限稳定返回 `AssistSolveFailed`。改变 shooting 次数必须提高 SolverVersion，并重新认证布局和全输入域报告。

### 3.5 解析事件与优先级

行星/主星碰撞和 UFO 命中使用线段—球二次根；即使一个子步两端都在球外也不能穿透。最近点使用固定次数二分求径向速度零点。版本 1 的同子步裁决为：

1. 最早解析根优先；
2. 同一根上 `BodyCollision > TargetHit`；
3. 随后处理当前阶段唯一合法的 `WrongOrder / InfluenceEnter / ReferenceEnter / ClosestApproach / ReferenceExit / InfluenceExit`；
4. 最后处理越界与模拟时限；
5. 时限处主星比能为负映射为 `SolarCaptured`，否则为 `Timeout`。

输出事件基线为 `AssistEnter / ClosestApproach / AssistExit / BodyCollision / TargetHit / WrongOrder / OutOfBounds / SolarCaptured / Timeout`，并保留三类稳定求解失败。

### 3.6 HashSchemaVersion 1

Hash 使用固定字节序 FNV-1a 64 位折叠，不对结构体内存、Padding 或指针做 CRC。规范序列依次包含：

- Hash/Solver 版本和全部数值配置；
- 场景版本、ScenarioHash、所有玩法相关 Body/Target 字段；
- 初态与期望助推序号；
- 终止原因、完成助推数；
- 按顺序的全部点和事件字段。

`-0.0` 先规范为 `+0.0`；所有非有限输入在求解前拒绝。当前只接受明确实现的 `SolverVersion=1 / HashSchemaVersion=1`，未知版本不能按 v1 静默求解。同一有效输入在同一 solver/hash schema 下必须得到完全一致的点列、事件列、终止原因和非零 Hash。续算请求的 `InitialExpectedAssistIndex=N` 表示前 `N-1` 颗已完成，结果的 `CompletedAssistCount` 从该前缀继续累计。

## 4. 自动化验收

全新 `UnrealEditor-Cmd -NullRHI` 运行 `ABTS.M11A`，共 7 项：

| 测试 | 阻断性断言 |
| --- | --- |
| `DataContract` | 固定四体/三助推；未知版本/飞越侧、重叠作用圈、参考球进入淡出壳层、非法根容差及作用圈/主星仿真域非法初态均被拒绝 |
| `DeterminismAndEvents` | 两次点列/事件/终止/Hash 位级一致；冻结 golden Hash、ScenarioHash 敏感性和 `±0` 规范化；`Enter < Closest < Exit`；续算前缀计数一致 |
| `CentralBindingAndConvergence` | 亚逃逸圆轨道为 `SolarCaptured`；`dt/2` 保持事件拓扑并降低位置/能量误差 |
| `NaturalDeflection` | `U=0` 时有明显自然转角、不撞星、拟合渐近线转角贴合理想双曲线、完整 Influence 外到外自然能量守恒、玩法能量严格为 0 |
| `VirtualMomentumAndAblation` | 非饱和正/负换能、`B=h/v∞`、错误侧归零、部分/外部走廊、最近点前轨迹不变且能量核在 25%/50%/75% 出站进度逐级分配、校准时限优先、①/②/③ 三个 Mask 位独立消融 |
| `SweptAnalyticEvents` | 大步跨过小 UFO/行星仍命中；验证较早根及同根 `Body > Target > WrongOrder`；请求时限在最近点前后均可压过更晚的克隆终止 |
| `StableFailure` | 退化 B-plane、低 `vInfinity`、细分/自然克隆预算耗尽和只擦 Influence 未进 Reference 均稳定失败，重复结果与 Hash 一致 |

批准夹具结果：

- 中心圆轨道 `dt=0.5/0.25 s` 端点误差约 `0.00826/0.00207 cm`；
- 相对能量漂移约 `1.56e-12/9.68e-14`；
- HashSchema 1 冻结夹具为 `0xd78e8f7153cca7f1`；
- 单星自然偏转约 `0.274399183 rad`，与理想值误差约 `4.25e-9 rad`，最近点约 `1082.83 cm`，完整 Influence 外壳相对能量残差约 `8.19e-13`；
- 非饱和正/反虚拟动量分别得到 `+3272.972/-3272.972 cm²/s²`，消融与错误侧为严格 `0`；
- 正助推出站核在 25%/50%/75% 进度的动能差约为 `403.890/1710.125/2926.424 cm²/s²`，证明预算不是集中在出口一次结算；
- 高速目标与行星扫掠根约为 `0.45 s`。

这些是算法夹具，不是正式三行星布局参数。M11-B 不得把测试坐标、巨大 B-plane 宽度或能量裁剪值复制为生产预设。

## 5. M11-B 交接

M11-B 应直接消费本阶段公开 Request/Result，不复制积分公式。它需要：

1. 在 `FABTSM110FinaleLocalFrame` 中搜索三颗行星和 UFO 的局部坐标；
2. 为每颗行星冻结真实 B-plane 目标、容差、虚拟速度、作用圈和玩法半径；
3. 生成版本化认证预设及 ScenarioHash；
4. 对完整声明的 `Yaw × Pitch × Power` 输入域做唯一连通 `F4`、前缀嵌套、错序/旁路和三颗任一助推消融验证；
5. 只把通过同一 SolverVersion/HashSchemaVersion 的预设交给 M11-C。

返回父级：[M11 算法预演](M11GravityAssistAlgorithmPrevisualization.md) · 返回入口：[ABTS 项目工作流](ABTSProjectWorkflow.md)。
