# M11：终局三重引力弹弓算法预演

> 状态：产品路线已确认；M11.0 已完成用户 PIE 验收；M11-A 纯数据求解器、Editor 编译与全新进程自动化已完成。M11-B C++、Development Editor 编译、冻结预设与全新进程自动认证均已完成，待用户 PIE；M11-C 实飞/HUD 和 M11-D 终局演出尚未开始。
>
> 父级：[AngryBirdsToSpace 游戏设计稿](AngryBirdsToSpaceGameDesign.md)。
>
> 导航：[项目工作流](ABTSProjectWorkflow.md) · [M11.0 终局前置收口](M110PreFinaleClosureDesign.md) · [M11-A 纯数据求解器](M11AGravityAssistSolverDesign.md) · [M11-B 局部布局与全输入域认证](M11BFinaleLayoutCertificationDesign.md) · [M6 弹弓发射与碰撞](M6SlingshotLaunchAndImpactDesign.md) · [M9 卫星与局部引力](M9SatelliteGravityDesign.md) · [M10.1-C 轨道全景图](M101COrbitalOverviewDiagramDesign.md) · [CuteBird 迁移与动画](CuteBirdMigrationAndAnimationDesign.md)

## 1. 预演结论

M11 应做成一段与程序生成地表连续、但轨道结果确定的终局谜题：

1. 玩家分别加工两根太空桩和一根太空弦，在 Task Graph 唯一 `LaunchSite` 的专用槽完成装配；
2. 一颗中心天体建立“直接发射必然受束缚”的能量底座；
3. Task Graph 世界生成完成后，以太空弹弓为原点应用一个经过离线认证的**终局局部布局预设**，生成三颗运行时不移动的助推行星和 UFO；
4. 玩家在完整 `Yaw × Pitch × Power` 输入域内调整发射方向和功率，只看**当前输入对应的完整预测结果**；
5. 第一颗行星通过距离形成近最大功率门槛，正确轨迹仍必须按 `① → ② → ③ → UFO` 的唯一拓扑顺序通过；
6. 系统用**前缀成功集稳定器**帮助玩家保住已经验证的 `F1/F2/F3`，但不吸向标准答案；
7. 四只彩色小鸟同时装入钢铁太空弹珠袋；发射后沿同一条预计算权威轨迹组成固定队列，命中后进入攻击和救援演出。

推荐算法不是纯真实 N-body，也不是样条吸附，而是：

> **恒星束缚 + 固定行星自然偏转 + 虚拟公转动量换算净能量 + 顺序 B-plane 容差走廊 + 预演/实飞同源固定步长求解器。**

这里的“唯一路径”指**只有一个连通的成功输入岛、一个助推顺序和一组飞越侧**，不指只有一个浮点角度。后者会把关卡变成像素狩猎，也无法抵抗输入量化和数值误差。

“四颗行星”在工程语义中指**既有地表主星 + 三颗新助推行星**，不是再生成四颗助推行星；UFO 是非引力目标。M9 练习卫星仍存在于同一 World，但不进入 M11 Body Specs，也不参与终局积分。

本稿确定 M11 的算法方向、数据契约、玩家反馈与正式验收方法。上游收口见 [M11.0 终局前置收口](M110PreFinaleClosureDesign.md)；已落地的唯一积分内核与自动化证据见 [M11-A 纯数据求解器](M11AGravityAssistSolverDesign.md)；当前布局搜索、认证预设、Actor 权威边界和 PIE 口径见 [M11-B 局部布局与全输入域认证](M11BFinaleLayoutCertificationDesign.md)。

## 2. 终局关卡与叙事边界

### 2.1 关卡流程

```text
完成前置采集与加工
  -> 找到 LaunchSite 唯一太空弹弓专用槽
  -> 安装两根太空桩并连接太空弦
  -> 短演出揭示被俘白色小鸟、UFO 和三颗助推行星
  -> 玩家进入终局轨迹模式
  -> 调整偏航、俯仰与功率，读取 ①/②/③ 的预测结果
  -> 前缀成功集稳定器依次保护 F1/F2/F3
  -> 发射并依次完成三次引力弹弓
  -> 命中 UFO
  -> 四鸟攻击与白色小鸟获救
```

`BP_Cute_Bird_0` 继续作为无颜色的基础 CuteBird 表现。被抓走的小鸟应是独立的剧情表现 Actor，不扩展现有四项 `EABTSBirdId`、不加入可切换 Party，也不成为第五种战斗能力。

交互阶段只求解一条权威编队中心轨迹。四只彩色小鸟在进入终局发射模式时同时放入钢铁太空弹珠袋，关闭各自的 Chaos 物理和阻挡碰撞，并以固定局部队形展示。发射后四鸟共享同一时间轴，以中心轨迹的时间偏移和小幅横向编队偏移形成队列；不能各自运行一套求解器，也不能临时拼接四个 Chaos 刚体。

### 2.2 Task Graph 后置的终局局部布局预设

M11 首版不使用手工独立地图，也不把绝对世界坐标烘焙在关卡里。Task Graph、地形、`LaunchSite` 平整面和太空弹弓专用槽全部生成并通过验证后，M11.0 先输出规范 `FABTSM110FinaleLocalFrame`：

```text
FrameOrigin = 左右 Space 槽世界位置中点
Up          = normalize(FrameOrigin - PrimaryCenter)
Right       = normalize(RightSlot - LeftSlot)，正向对准 SatelliteWindow 切向
Forward     = normalize(Right × Up)
PouchOrigin = Frame.TransformPosition(PouchLocalOffset)
```

离线搜索冻结的不是世界坐标，而是相对于该坐标系的局部偏移：

```text
WorldCenter_i = FrameOrigin
              + Forward * LocalOffset_i.X
              + Right   * LocalOffset_i.Y
              + Up      * LocalOffset_i.Z
```

预设至少记录 `LayoutVersion`、`GeneratorVersion`、主星尺度归一化基准、三颗行星/UFO 的局部偏移、视觉半径、玩法半径、作用圈、虚拟公转速度和验证 Hash。运行时不得按帧搜索或微调行星；若当前生成世界无法通过地平线、净空、作用圈分离和完整输入域验证，应拒绝该 PCG Attempt 或改用另一项**预先认证**的有限预设，不能静默拖动 Actor。

第一颗助推行星应放得足够远，使低功率轨迹在规定时间窗内无法进入其作用圈；但它必须在接近最大功率的一段连续区间内可达，而不是只接受浮点意义上的 `Power=1.0`。距离门控只负责建立功率下限，唯一路径族仍由第 6 节的全输入域扫描证明。

### 2.3 同一 World 的终局环境切换

M11 明确使用当前 Task Graph World，不执行 `OpenLevel`，也不加载独立终局地图。进入终局模式时：

- 将现有天空球切换为高清星空材质；
- 先淡出、再隐藏高度雾、体积云和需要关闭的行星大气表现；
- 同步调整 Skylight、曝光和方向光，避免只换天空纹理后地表仍维持白昼曝光；
- 星空表现跟随活动飞行镜头位置或使用等效无限远方案，不能让相机飞出天空球；
- 失败复位或退出终局模式时恢复进入前的环境状态。

“让体积云和高度雾随玩家在球面上的视角旋转”不属于 M11。它在 M11 后单列球面大气里程碑；不能在本阶段简单把雾/云 Actor 绑定到相机旋转，否则会产生游移和跳变。

```text
太空弹弓
   \
    \  ① 宽走廊：教会正确飞越侧
     \____
          \  ② 中走廊：继续增能并接入下一段
           \____
                \  ③ 窄走廊：最终定向
                 \____________ 合格终端拦截包络
                                \__ 预计算 coast __ 800 cm 几何 UFO

直接发射或漏过任意助推：仍处于中心天体束缚轨道，回落、超时或错过目标
```

用户草图中的两类长曲线可落实为：

- 内侧闭合/回落曲线：未获得足够助推、仍被中心天体束缚；
- 外侧延伸曲线：依次完成三次助推后才达到 UFO。

“太阳”首先是算法中的中心束缚源；最终美术可以把它表现为恒星或其他巨大中心天体，但其逻辑身份不能与三颗助推行星混在一起。

## 3. 调研结论与设计推导

### 3.1 静止行星为何不能提供真实净加速

NASA 对真实引力弹弓的解释很明确：相对飞越行星，航天器出入双曲线的远端速度大小相同、方向改变；相对太阳或其他中心天体的净能量变化来自**行星自身的轨道运动**。[NASA Gravity Assist Primer](https://science.nasa.gov/learn/basics-of-space-flight/primer/) · [NASA Cassini Gravity Assists](https://science.nasa.gov/mission/cassini/gravity-assists/) · [ESA：What are gravity assists?](https://www.esa.int/Enabling_Support/Operations/What_are_gravity_assists)

若中心天体和三颗行星都固定，且只使用保守引力：

\[
\dot{\mathbf r}=\mathbf v
\]

\[
\dot{\mathbf v}=
-\mu_0\frac{\mathbf r-\mathbf C_0}{\|\mathbf r-\mathbf C_0\|^3}
-\sum_i\mu_i\frac{\mathbf r-\mathbf C_i}{\|\mathbf r-\mathbf C_i\|^3}
\]

则无阻力时总比机械能守恒：

\[
E=\frac12\|\mathbf v\|^2
-\frac{\mu_0}{\|\mathbf r-\mathbf C_0\|}
-\sum_i\frac{\mu_i}{\|\mathbf r-\mathbf C_i\|}
\]

小鸟接近固定行星时获得的速度会在离开时还回去。固定行星能使轨迹偏转，却不能让原本被中心天体束缚的轨道永久获得逃逸能量。因此 M9 的“静止卫星 + 逆平方引力”可作为自然偏转基础，但不能单独承担 M11 的三次加速。

### 3.2 可借鉴的同类交互

| 来源 | 可观察做法 | M11 的设计推导 |
| --- | --- | --- |
| [Angry Birds 官方游戏档案](https://www.angrybirds.com/explore/hall-of-games/)与 [Rovio 的 Space 玩法说明](https://www.rovio.com/articles/angry-birds-2-space-arrives-october-24th/) | 把行星引力与 Gravity Zones 明确作为关卡机制 | M11 应进一步把作用区、飞越侧和当前轨迹画成可读反馈，不能把关键力藏在后台 |
| [KSP 1.7 官方更新说明](https://store.steampowered.com/news/posts/?appids=220200&enddate=1557151214) | 把精细机动节点编辑和轨道信息放入同一导航工具，服务行星际转移微调 | 系统展示当前输入的后果，但不显示标准答案角度或自动填入正确解 |
| [NASA Cassini 多次助推](https://science.nasa.gov/mission/cassini/gravity-assists/) | 一次飞越的出口条件服务于下一次飞越，多颗天体形成链式任务 | 三颗行星应是顺序状态机，不是三个可任意刷取的增速圈 |
| [NASA Lucy 引力助推可视化](https://svs.gsfc.nasa.gov/5044) | 同时提供全局俯视轨迹和近距离跟随两种尺度 | M10.1-C 的全景投影负责因果阅读，主镜头负责掠过演出，不让一个镜头包办两种尺度 |

上述 M11 取舍是基于公开资料的项目设计推导，不代表这些作品使用了与本稿相同的内部算法。

### 3.3 Patched-conic 的适用方式

NASA 的 patched-conic 方法把任务拆成中心天体巡航段和行星作用圈内的局部双曲线段，并在作用圈边界拼接状态；它适合初步设计和快速权衡。[NASA Patched Conic Trajectory Code](https://ntrs.nasa.gov/api/citations/20120006596/downloads/20120006596.pdf) · [NASA 轨迹设计课程](https://ntrs.nasa.gov/api/citations/20220000576/downloads/Interplanetary%20Trajectory%20Optimization%203%20-%20Designing%20an%20Interplanetary%20Trajectory.pptx.pdf)

M11 借用的是“分段作用圈、相对速度和能量交换”这套可解释结构，不声称模拟真实比例的太阳系。

## 4. 方案比较

| 方案 | 优点 | 主要问题 | 结论 |
| --- | --- | --- | --- |
| 运动行星 + restricted N-body | 物理最真实 | 出现发射窗口和相位变化；重试时唯一解会漂移，与固定布局要求冲突 | 不采用 |
| 固定行星 + 纯逆平方引力 | 连续、确定，可复用 M9 思路 | 只有偏转，没有永久净增能 | 只保留为自然偏转层 |
| 瞬时 patched-conic 状态映射 | 参数可解释、求解快、出口状态稳定 | 直接瞬时改方向会产生折点；固定行星仍需虚拟速度 | 用于计算目标能量，不直接硬切轨迹 |
| 人工切向力或速度倍率 | 最容易调关卡 | 容易退化成“进圈就加速”，飞越方向与近星点失去意义 | 仅作为受约束的平滑能量修正 |
| 样条吸附/检查点导轨 | 最容易保证命中 | 玩家瞄准不再决定结果，轨道预演失去意义 | 不作为主算法；首版也不做隐形横向吸附 |
| **推荐混合模型** | 固定布局、可解释、可重复、可形成能量阶梯 | 属于明确授权的游戏化物理，需要完整 HUD 解释 | **推荐** |

## 5. 推荐算法

### 5.1 场景数据与权威状态

统一使用厘米、秒和双精度数值。求解器只接收一次构建好的只读数据，不在每个子步扫描 World。

中心束缚源至少包含：

- 稳定 `BodyId`、中心 `Center`；
- `Mu`、软化半径或最小求值半径；
- 太阳碰撞/烧毁半径；
- 最大有效模拟半径。

三颗助推体各自包含：

- 稳定 `BodyId` 和严格顺序 `AssistIndex=1..3`；
- 静态中心、表现半径、球形碰撞半径、`InfluenceRadius` 与 `AssistReferenceRadius`；参考球必须位于 `InfluenceRadius - BlendWidth` 以内的完整引力区；
- 自然偏转参数 `Mu` 与边界平滑宽度；
- 不驱动 Transform 的 `VirtualOrbitalVelocity`；
- B-plane 目标中心、内外容差椭圆、允许飞越侧、参考法向与固定备用轴；
- 最大正/负能量修正和调试颜色。

M11-B v1 将目标拆成三层：F3 的 `TargetApproach`、只有连续三次高质量助推才可触发的合格终端拦截包络，以及位于更远端的 800 cm 实际 UFO 几何接触球。几何 UFO 有独立中心并提供演出朝向；静态网格只负责表现，不提供权威半径或复杂碰撞。该拆分避免用大命中球掩盖“关闭某次助推仍擦到真实 UFO”的旁路。

飞行状态为：

\[
X=(\mathbf r,\mathbf v,t,\text{ExpectedAssist},\text{Status})
\]

输出除完整点列外，还必须记录每次作用圈进入、最近掠过、离开、入/出速度、B-plane 偏差、能量变化和首个终止原因。

### 5.2 中心束缚与能量阶梯

中心天体始终施加：

\[
\mathbf a_0=-\mu_0
\frac{\mathbf r-\mathbf C_0}
{\max(\|\mathbf r-\mathbf C_0\|,r_{\min})^3}
\]

太空弹弓最大初速必须低于当地逃逸速度并留出调参余量：

\[
v_{\text{launch,max}}<
v_{\text{esc}}=\sqrt{\frac{2\mu_0}{r_0}}
\]

令 \(\boldsymbol\rho=\mathbf r-\mathbf C_0\)。在所有助推作用圈之外，把当前段近似为中心二体运动；其比轨道能量为，当 \(\epsilon<0\) 时，无助推椭圆段的远拱点可由下式得到：

\[
\epsilon=\frac{v^2}{2}-\frac{\mu_0}{r},
\qquad
h=\|\boldsymbol\rho\times\mathbf v\|,
\qquad
r=\|\boldsymbol\rho\|
\]

\[
a=-\frac{\mu_0}{2\epsilon},
\qquad
e=\sqrt{1+\frac{2\epsilon h^2}{\mu_0^2}},
\qquad
r_a=a(1+e)
\]

`v_launch,max < v_esc` 只建立“直接发射受束缚”的参数基线，不能单独证明有限距离处不存在旁路；最终不可达性仍以第 6 节的全输入域扫描和助推消融为准。关卡参数必须形成可验证的能量阶梯：

| 阶段 | 必须能到达 | 必须不能到达 |
| --- | --- | --- |
| 发射后、无助推 | 行星 ① | 行星 ② 与 UFO |
| 完成 ① | 行星 ② | 行星 ③ 与 UFO |
| 完成 ② | 行星 ③ | 合格终端拦截与几何 UFO |
| 完成 ③，但任一次终端质量不足 | TargetApproach | 合格终端拦截与几何 UFO |
| 完成 ③，且三次均达到终端资格 | 合格终端拦截包络 | — |

关闭任意一次玩法助推后都不得进入合格终端拦截，也不得接触独立几何 UFO；不能用“大目标球”或只统计 qualified hit 来掩盖能量阶梯失败。

### 5.3 固定行星负责自然偏转

只有当前期望行星、且仅在其作用半径内，才叠加平滑截断的径向引力：

\[
\mathbf a_i=
G_i(d_i)\mu_i
\frac{\mathbf C_i-\mathbf r}
{\max(d_i,r_{\min,i})^3}
\]

径向引力窗函数 \(G_i\) 在作用圈边缘平滑降为零；三个作用圈不得重叠。碰撞球位于奇点或软化区之前，高速运动以 swept sphere 判定，不能只在子步末端检查。

自然偏转层决定轨迹从行星哪一侧经过、偏转多少以及是否撞星。它不承担永久净增能，也不把小鸟拉向标准路线。

### 5.4 虚拟公转动量只换算净能量

每颗静止行星配置一个不可见的虚拟公转速度 \(\mathbf U_i\)。它不移动网格体，不改变关卡位置，也不作为一颗“实际上正在运动的行星”参与位置积分；它只是把当前**自然飞越已经产生的方向变化**换算成一个玩法做功预算。

穿入 `InfluenceRadius` 时先登记遭遇并保持当前行星为 `ExpectedAssist`；入站继续穿入完整引力区内的 `AssistReferenceRadius` 后，确定性自然飞越克隆必须在同一个参考球上取得。只擦过 Influence 外圈而未进入 Reference 的轨迹不构成一次完成的助推，不能递增前缀成功集：

- 入射与出射状态；
- 近星点 \(r_p\)；
- 入射/出射渐近方向 \(\hat{\mathbf S}^-\)、\(\hat{\mathbf S}^+\)；
- 自然出口比能量 \(\epsilon_{\text{natural,out}}\)；
- 实际自然偏转角 \(\delta_{\text{natural}}\)。

\(\epsilon_{\text{natural,out}}\) 与后续出口残差都必须在同一全局势能约定和同一参考球上求值；不能一处使用行星中心二体能量、另一处使用中心天体能量。下述 \(v_\infty\) 只作为局部双曲线诊断量，不替代完整自然克隆的全局出口状态。

参考球入口的局部速度也不能直接冒充双曲线渐近方向；渐近方向由该状态的双曲线轨道要素拟合。局部余速大小以固定行星中心参考系的参考球入口二体能量计算：

\[
v_\infty^2=
\|\mathbf v_{\text{entry}}\|^2
-\frac{2\mu_i}{d_{\text{entry}}}
\]

当 \(v_\infty^2\le v_{\infty,\min}^2\)、无法稳定拟合渐近方向或明确出现物理捕获时，本次遭遇是 `AssistInvalidHyperbola` 或 `PlanetCaptured`，不得继续套用助推公式。若只是自然克隆的固定步数/时域预算耗尽而尚未求得参考球出口，则必须报告 `AssistSolveFailed`，不能把“未算完”伪装成物理捕获。克隆预测到的未来碰撞、目标命中、捕获或请求时限只作为规划失败类别，不得直接把未来状态写入权威结果；正式轨迹仍逐步比较所有更早根，并且克隆在最近点前后及换能归一化校准中都不得越过本次请求时限。

定义与自然轨迹方向一致的渐近余速向量：

\[
\mathbf u_\infty^-=v_\infty\hat{\mathbf S}^-,
\qquad
\mathbf u_\infty^+=v_\infty\hat{\mathbf S}^+
\]

标准双曲线关系只用于调参与一致性校验：

\[
e_h=1+\frac{r_pv_\infty^2}{\mu_i},
\qquad
\delta_{\text{ideal}}=2\arcsin\left(\frac1{e_h}\right)
\]

\[
\delta_{\text{ideal}}=
2\arctan\left(\frac{\mu_i}{bv_\infty^2}\right)
\]

其中 \(b\) 是双曲线 impact parameter（瞄准参数）。第二式是同一双曲线几何的等价参数化；若 \(|\delta_{\text{natural}}-\delta_{\text{ideal}}|\) 超过批准容差，说明作用圈平滑、中心引力或步长已使 patched-conic 近似失真，场景配置应判为无效，而不是继续注入能量。

虚拟公转动量给出的原始比能量预算为：

\[
\Delta\epsilon_{i,\text{raw}}
=\mathbf U_i\cdot
(\mathbf u_\infty^+-\mathbf u_\infty^-)
\]

当 \(\mathbf U_i=0\) 时，净增能自然为零。该预算经 Body Spec 的正/负上限裁剪后，加到自然出口能量：

\[
\epsilon_{\text{full-assist,out}}
=\epsilon_{\text{natural,out}}
+\operatorname{clamp}
(\Delta\epsilon_{i,\text{raw}},
\Delta\epsilon_{i,\min},
\Delta\epsilon_{i,\max})
\]

\(\epsilon_{\text{full-assist,out}}\) 只是 \(q_i=1\) 时的满额助推调参值，不是所有走廊质量都必须达到的 shooting 目标。首版不把一个解析 \(\mathbf v^*\) 直接写入当前速度。出口方向仍由固定行星自然引力和玩家的入射几何决定；若自然偏转无法接入下一颗行星，应调整 `Mu`、作用半径、B-plane 目标或固定场景布局，而不是加入隐形横向吸附。近星点—余速—转角关系可参考 [NASA《Space Flight Handbooks, Vol. 3, Part 9》](https://ntrs.nasa.gov/api/citations/19700019224/downloads/19700019224.pdf)。

### 5.5 把能量修正平滑分配到近星段

穿入作用圈时求解器先登记遭遇；到达参考球入根后才克隆当前状态，模拟一次“中心引力 + 固定行星自然偏转”，取得自然近星点、B-plane 偏差、参考球出口状态和预计停留子步。`q_i` 在这次确定性规划中锁定，不随渲染帧或修正后的瞬时位置反复跳变。换能在参考球出根结算后，当前行星仍继续施加完整的出站淡出壳层引力；只有穿出 `InfluenceRadius` 才发出 `AssistExit` 并递增期望序号。

为了保证近星点和飞越侧确实来自自然引力，入站阶段不施加玩法做功；只有检测到 \( (\mathbf r-\mathbf C_i)\cdot\mathbf v \) 从负变正、近星点已锁定后，才在出站壳层平滑分配比能量预算：

\[
W_i=q_i^p
\operatorname{clamp}
(\Delta\epsilon_{i,\text{raw}},
\Delta\epsilon_{i,\min},
\Delta\epsilon_{i,\max})
\]

锁定 \(q_i\) 后，本次遭遇真正的出口 shooting 目标才定义为：

\[
\epsilon_{\text{target,out}}
=\epsilon_{\text{natural,out}}+W_i
\]

归一化核：

\[
\kappa(s)=30s^2(1-s)^2,
\qquad
\int_0^1\kappa(s)\,ds=1
\]

其中 \(s\in[0,1]\) 是“近星点到参考球出口”的归一化进度。遇到作用圈边界拆步或局部细分时，权重必须包含每个子步的真实时长：

\[
w_n=
\frac{\kappa(s_n)\Delta t_n}
{\sum_k\kappa(s_k)\Delta t_k}
\]

\[
z_n=v_n^2+2w_nW_i
\]

先检查 \(z_n\)：若 \(z_n<-\epsilon_{\text{root}}\)，立即返回 `AssistSolveFailed`；只有 \(-\epsilon_{\text{root}}\le z_n<0\) 的浮点负零才允许钳为零，然后计算

\[
v_n'=\sqrt{\max(0,z_n)}
\]

\[
\Delta\mathbf v_n=(v_n'-v_n)\hat{\mathbf v}_n
\]

- \(q_i\in[0,1]\) 是 B-plane 走廊质量；
- \(p\) 控制走廊边缘的**能量**衰减；
- 每个瞬时 kick 精确增加 \(w_nW_i\) 的比动能；
- 默认不存在横向位置修正。

完整修正轨迹仍需用同一求解器重算出口、碰撞和下一节点，不能把自然克隆的出口位置直接当作结果。若修正后无法退出作用圈、根号项为实质负值、出口能量残差超限或固定次数的局部 shooting 未收敛，结果必须是稳定的 `AssistSolveFailed`，不能静默采用最后一次迭代。错误飞越侧可由同一虚拟速度公式得到负能量预算，或只保留自然偏转；不得把错误侧也当作同额正助推。

### 5.6 B-plane 走廊

B-plane 是垂直于入射渐近速度的目标平面，JPL 也使用它描述行星遭遇的瞄准偏差。[JPL B-plane 说明](https://www.jpl.nasa.gov/news/mars-tugging-on-approaching-nasa-rover-curiosity/)

坐标轴不能从上一帧或上一条候选轨迹继承。每颗 Body Spec 保存作者给定的 `BPlaneReferenceNormal` 和 `BPlaneFallbackAxis`；以自然克隆得到的入射渐近单位方向 \(\hat{\mathbf S}^{-}\) 构造：

\[
\hat{\mathbf T}=
\operatorname{normalize}
\left(
\mathbf N_{\text{ref}}-
(\mathbf N_{\text{ref}}\cdot\hat{\mathbf S}^{-})
\hat{\mathbf S}^{-}
\right),
\qquad
\hat{\mathbf R}=
\operatorname{normalize}
(\hat{\mathbf S}^{-}\times\hat{\mathbf T})
\]

若参考法向投影长度低于固定阈值，则用 `BPlaneFallbackAxis` 重算；备用轴仍退化、入口不是有效双曲线或渐近线拟合失败时，稳定返回 `AssistInvalidBPlaneBasis`，不得任意选取世界轴或沿用历史轴。瞄准向量 \(\mathbf B\) 的坐标和飞越侧统一定义为：

\[
\mathbf h_i=
(\mathbf r-\mathbf C_i)\times\mathbf v,
\qquad
\mathbf B=
\frac{\hat{\mathbf S}^{-}\times\mathbf h_i}{v_\infty}
\]

这里 \(\mathbf h_i\) 使用与 \(v_\infty\) 和 \(\hat{\mathbf S}^{-}\) 相同的行星中心二体拟合状态；\(\mathbf B\) 的方向严格定义为“从行星中心指向入射渐近线最近点”，不得取反。随后才投影：

\[
B_T=\mathbf B\cdot\hat{\mathbf T},
\qquad
B_R=\mathbf B\cdot\hat{\mathbf R}
\]

`AllowedPassSide` 必须绑定 \(B_T\) 或 \(B_R\) 的明确符号，并冻结在场景数据中。

每颗行星配置目标 \((B_T^*,B_R^*)\) 和容差椭圆：

\[
\chi_i^2=
\left(\frac{B_T-B_T^*}{\sigma_T}\right)^2+
\left(\frac{B_R-B_R^*}{\sigma_R}\right)^2
\]

- \(\chi_i^2\le1\)：完整助推；
- \(1<\chi_i^2<\chi_{\text{outer}}^2\)：按平滑曲线衰减；
- 超出外椭圆、顺序错误或错误飞越侧：不获得正助推；
- 进入碰撞半径：`PlanetImpactN`，优先于走廊判定。

走廊只决定能量修正质量，不把小鸟横向吸向中心。成功输入必须形成一个窄但连续的容差岛。B-plane 椭圆与 Monte Carlo 走廊验证也用于真实任务的交付分析，可参考 [NASA OSIRIS-REx B-plane corridor](https://ntrs.nasa.gov/api/citations/20240000803/downloads/OREx_ER_Entry_Targeting_Strategy_and_Man_Perf_v3.pdf)。

### 5.7 唯一积分器

M11 不能继续使用“`0.075 s` 的 M6 预览 + 每帧 Chaos `AddForce` 的实际飞行”两条链路。三次近星飞越会放大微小误差，使玩家看到的路径与实际结果不一致。

推荐：

- 新建纯 C++、不依赖 World 扫描的固定步长求解器；
- 保守段使用 velocity-Verlet 或 leapfrog；人工能量修正作为独立 kick；
- 默认从 `1/120 s` 起做收敛测试，而不是直接把它当最终常量；
- 基础固定步长在确定性的最大细分深度内同时满足

\[
v_{\max}\Delta t\le(0.03\sim0.05)R_{\text{assist,min}}
\]

\[
v\Delta t\le c_vR_{\text{collision,min}},
\qquad
\Delta t\le c_g\sqrt{\frac{r^3}{\mu_{\text{active}}}},
\qquad
\frac12a\Delta t^2\le\epsilon_{\text{pos}}
\]

其中 \(c_v\)、\(c_g\)、\(\epsilon_{\text{pos}}\) 与最大细分深度属于冻结的求解器配置；`R_collision,min` 取当前可能事件中最小的非零解析碰撞尺度，不能只用更大的作用圈半径替代。

- 作用圈进入/退出、参考球进入/退出、解析球碰撞、\((\mathbf r-\mathbf C_i)\cdot\mathbf v=0\) 最近点、合格终端拦截与独立 UFO 几何接触均在线段内求交；临近根时使用固定最大深度的确定性细分和固定迭代二分，不依赖渲染帧时间；达到细分或总步数上限而仍未满足合同必须稳定失败，不能静默采用未批准的大步或误报物理超时；
- 使用双精度状态，太空段关闭空气阻力；
- 预演与实际飞行逐子步调用同一内核，渲染点另行抽稀；
- 实际飞行只重放/插值求解器的确定性状态。M11-B 额外冻结一条从原始 Pouch 状态开始、以 800 cm 几何 UFO 为终点的 nominal Physical Playback，证明完整演出路径无需从拦截球内部续算或位置瞬移；它不代表 F4 内任意输入都接触 800 cm 球。M11-C 必须单独冻结玩家 Release 与这条成功演出路径之间的位置/速度连续接管，不得隐藏吸向标准答案。接触几何 UFO 后才可切到局部 Chaos 演出。

**碰撞权威只有一处：**轨道段的作用圈、行星碰撞、中心天体碰撞、合格终端拦截和几何 UFO 接触全部由求解器对不可变 Body Specs 中的解析球完成。Body/UFO Actor 的 Static Mesh 不阻挡轨道鸟，`USphereComponent` 仅作编辑器可视化或 Query-only 调试；Actor 不得再执行会停止、滑动或推出鸟体的 UE 阻挡 Sweep。所有 Gameplay 回调由求解器事件发出。若后续需要加入其他太空障碍，必须先把同一份解析几何写入求解器输入，使预演与实飞共同消费，不能只在 World 碰撞里补一个阻挡体。

UE 官方也说明可变帧率会给物理步长带来稳定性问题，较小子步能提高稳定性但增加成本。[Unreal Engine Physics Sub-Stepping](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-sub-stepping-in-unreal-engine)

## 6. 离线反解与关卡定标

### 6.1 推荐玩家输入

首版开放偏航、俯仰和发射功率三个连续变量。玩家仍通过寻找“接近最大功率且飞向行星 ① 正确一侧”的组合进入解族，不把功率预先锁死：

- 行星 ① 的距离建立高功率门槛；离线定标建议让可达下界落在约 `0.88–0.95`，最终数值由完整搜索决定；
- 高功率成功带必须覆盖至少两个实际可输入档位或等效连续宽度，不能只接受 `Power=1.0`；
- M11 输入层应把 `Yaw/Pitch/Power` 明确解耦；改变功率不能像 M6 拉袋几何那样附带改变方向；
- 功率步长应在实现稿中细化到约 `0.02–0.04` 或等效模拟量精度；现有 M6 的 `0.08` 仅作上游现状，不直接继承为终局精调合同；
- 玩家在任何未稳定的区域都可自由增减功率；进入前缀稳定状态后只阻止破坏已验证前缀的输入，详见 8.1。

扩大为三个输入维度后，轨迹全景图、当前失败原因和前缀稳定器共同承担可读性，不能通过重新锁死功率规避搜索与验收。

### 6.2 两级搜索

离线工具同时搜索**终局局部布局预设**、发射输入和不可见玩法参数。搜索结果写成相对于 `Origin/Forward/Right/Up` 的局部偏移，不写绝对世界 Transform；运行时只做刚体坐标变换，不再优化或搬动行星。

第一级：

1. 对三颗行星/UFO 的受约束局部偏移与允许的 `Yaw × Pitch × Power` 做固定 Seed 的确定性网格、Sobol 或 Latin-hypercube 播种；
2. 以简化 patched-conic 迅速筛出能按 `①→②→③` 进入作用圈的候选；
3. 同时淘汰行星 ① 低功率可达、世界尺寸越界、视线穿主星、作用圈重叠或发射净空不足的布局；
4. 记录每段所需入射/出口速度、B-plane 中心和飞行时间。

第二级：

1. 用最终 runtime solver 重放候选；
2. 以 UFO 最近距离、三次 B-plane 偏差、碰撞、顺序、总时间和鲁棒性构造代价；
3. 用固定 Seed 的差分进化/CMA-ES 或粗网格，再以 Powell/Nelder–Mead 做局部精化；
4. 对候选周围做小扰动，并对完整 `Yaw × Pitch × Power` 输入域做 `F4` 发现与连通分量扫描；
5. 围绕已发现的 `F4` 族建立最终精化闭包，验证三个局部前缀成功内核的连通性、最小宽度及包含关系；
6. 冻结局部预设版本、场景版本、物理参数、标准输入、扫描合同和验证 Hash。

扫描报告必须同时冻结允许的 `Yaw × Pitch × Power` 全域、三轴网格步长、递归边界细化精度、最大飞行时长/圈数、输入到屏幕的映射、参考分辨率与 DPI。不能只在标准解附近抽样，也不能把功率固定后宣称全域唯一。有限采样不能数学证明连续域中“绝对唯一”；本项目 M11-B 的正式验收语义是：**在上述已声明的完整三维输入域和分辨精度下，只发现一个连通的 16k qualified-intercept `F4` 分量。** 独立 800 cm 物理 UFO 由冻结 nominal Physical Playback 和 M11-C 的连续演出交接负责，不是这一宽可玩族的命中半径。

目标函数可采用：

\[
J=
w_H\frac{d_{\text{UFO,min}}^2}{R_{\text{hit}}^2}
+w_B\sum_i\chi_i^2
+w_CP_{\text{collision}}
+w_SP_{\text{sequence}}
+w_T\frac{T}{T_{\max}}
+w_RJ_{\text{robust}}
\]

必须做助推消融：

- 关闭 ①、② 或 ③ 中任意一个：不得命中；
- 三次全关：保持束缚或无法到达；
- 任意错误飞越侧：减能或明显偏离下一节点；
- 在已声明的完整 `Yaw × Pitch × Power` 输入域、网格步长和递归边界精度下：只发现一个连通成功分量；
- 时域截断前不存在跳过行星、错误顺序、重复绕行或多圈后重新接入的旁路；
- 标准输入附近存在可玩的连续容差，不依赖单一浮点值。

### 6.3 前缀成功集与可玩宽度

为当前完整预测定义：

```text
F1 = 正确进入、掠过并离开行星 ①，出口仍可进入 ② 的输入集合
F2 = F1 且正确完成行星 ②，出口仍可进入 ③ 的输入集合
F3 = F2 且正确完成行星 ③，出口仍可进入 TargetApproach 的输入集合
F4 = F3 且满足三次终端资格并进入 16k qualified-intercept 包络的输入集合
```

离线验收必须在完整声明域内只发现一个连通 `F4` 成功族，并在围绕该族的最终精化闭包内证明 `F4 ⊂ F3 ⊂ F2 ⊂ F1`；每个 `Fn` 只发布与最终解相连的局部主分量，并具有足够的角度和功率内宽。这里的“成功岛”不是单独评估每颗行星的擦边命中：如果一次 ① 掠过无法继续到达 ②，它不属于 `F1`。该报告不宣称证明远离 `F4` 族的全域 `F1/F2` 微拓扑。

这里的 `F4` 是 M11-B 的可玩拦截合同，不等价于每个输入都穿过 800 cm 物理球；后者只对冻结 nominal 完整轨迹成立，并由 M11-C 显式验收可见连续接管。

精确的成功集可能非凸、带细颈或随功率变化。运行时不直接把光标投影到任意非凸集合边界，而是由离线报告为每个 `Fn` 输出一个完全包含在该集合内部的鲁棒信赖域，例如分功率切片的椭圆/凸多边形及其内外滞回边界。任何无法给出最小宽度的布局都应被搜索器拒绝，不能依靠输入锁定掩盖像素级解。

## 7. 工程职责建议

以下是实施阶段的建议边界，不代表本次已创建这些类：

| 建议职责 | 内容 |
| --- | --- |
| `FABTSM11GravityBodySpec` | 中心、玩法半径、作用半径、`Mu`、虚拟速度、顺序和 B-plane 参数；静态网格不进入求解 |
| `FABTSM11TrajectoryEvent` | `AssistEnter/ClosestApproach/AssistExit/BodyCollision/TargetHit/Timeout/SolarCaptured/WrongOrder` 及其时空、速度和偏差信息 |
| `FABTSM11TrajectoryResult` | 完整点列、事件列、终止原因、能量阶梯和验证 Hash；不硬塞进只描述主星落点的 `FABTSM6TrajectoryPreview` |
| `FABTSM11GravityAssistSolver` | 纯数据、固定步长、预演/实飞同源的唯一轨道内核 |
| `FABTSM11FinaleLayoutPreset` | 相对太空弹弓局部坐标系的三行星/UFO 偏移、玩法参数、版本和验证 Hash；不保存绝对世界坐标 |
| `FABTSM11PrefixTrustRegion` | `F1/F2/F3` 的鲁棒内核、捕获/释放滞回、允许角度域和功率下界 |
| `AABTSM11GravityBodyActor` | 普通 `AActor + UStaticMeshComponent`；由局部预设生成，可带不阻挡的 Query-only 调试球，表现与权威解析球分离 |
| `AABTSM11FinaleSystem` | 太空弹弓门控、局部预设实例化、预演缓存、确定性推进、前缀稳定、失败/重试和终局演出 |
| `AABTSM11FinaleEnvironmentController` | 同一 World 的星空切换、雾云淡出/恢复、光照曝光状态快照 |
| `FABTSM11AttemptSnapshot` | 点击太空弹珠袋前的 Party、镜头、输入、环境、轨迹残影和终局状态，用于黑屏内原位恢复 |
| `AABTSM11UFOActor` | UFO 与挂点表现；权威命中仍由求解器解析球及事件提供 |
| `AABTSM11CaptiveBirdActor` | 白色 CuteBird 剧情表现，不加入 Party |
| `AABTSM11GameMode` | 同一 Task Graph World 中从启动即生效的 M10 后继 GameMode；不使用独立 `L_ABTS_M11` |

关键兼容边界：

1. `EABTSSlingshotTier::Space`、钢铁弹弓视觉资源和 Space preset 继续复用；入口改为 `EABTSItemId::SpaceStake ×2 + EABTSItemId::SpaceCord ×1`，且只能安装在 `LaunchSite` 的 Space-only 槽。旧 `SpaceSlingshotPart` 只保留隐藏枚举值兼容历史序列化，不再出现在物品目录、配方目录或运行时装配链。完整前置契约见 [M11.0](M110PreFinaleClosureDesign.md)。
2. 三颗助推体**不能继承** `AABTSM2Planet` 或 `AABTSM9Satellite`。现有鸟移动会查找并缓存一个 `AABTSM2Planet` 作为脚下主星，多颗派生 Planet 会产生误绑定风险。
3. M9 卫星继续在同一 World 作为强化弹弓练习，但应由 `SatelliteWindow`/M11.0 的确定性空间合同放到远离 `LaunchSite` 的位置。M11 求解器构建 Body Specs 时采用白名单，只接收主星和三颗助推行星；不得查询或叠加 M9 卫星引力。
4. Space 档 Release 后由 M11 flight provider 接管，关闭该鸟的 Chaos 积分；普通 Twig/Simple/Reinforced 发射保持现有 M1–M10 链路不变。
5. M10.1-C 的 PCA 拟合、凸包取景、球体遮挡和圆裁剪应抽为屏幕无关投影工具。M10 与 M11 分别提供输入；M11 不依赖青翎侦察、`Reinforced` 档位或主星落点。
6. 求解器每步不得 `TActorIterator`。终局场景激活时按稳定 ID 构建一次只读 Body Specs。
7. 同一 World 直接沿用现有库存与 Party Actor；M11 不再保留独立地图分支。`FABTSM11AttemptSnapshot` 只服务单次发射失败后的原位恢复，不承担跨关卡序列化。

## 8. 轨道全景与玩家反馈

M11 延续 M10.1-C 的完整轨迹、拟合截面、自适应取景、球后虚线与球前实线语义，并增加终局解释层：

- 三颗行星分别用火星、木星、土星的极简平面线条绘制，UFO 使用椭圆船体、舱罩和光束等少量线段；这些是 HUD 图标，不从静态网格轮廓投影；
- HUD 图标大小可以按可读性夸张，不代表世界玩法半径、引力作用圈或真实天体比例；
- 三颗行星仍标记为 `① ② ③`，UFO 使用目标符号；
- 绘制每颗行星的作用圈、碰撞核心、进入点、最近掠过点和离开点；
- 显示极简链条 `太空弹弓 → ① → ② → ③ → UFO`；
- 节点状态只描述当前预测：未到达、有效、部分助推、撞击、错误侧、增能不足、错过下一节点；
- 助推段可使用对应行星颜色的外发光；虚线只保留“位于球后”这一种语义；
- 能量只显示 `受束缚 / 临界 / 可达目标` 三档，不给玩家伪精确的天体物理数字；
- 每颗行星周围显示轻量环向流动箭头，解释静止网格为什么存在增能侧和减能侧；
- 远端画面始终复用一套 SceneCapture/RenderTarget：预测轨迹有效接近当前最早未完成的目标行星时，切换到该行星的接近预览；完成 ③ 后切换到 UFO；
- 预览镜头以目标中心、预测入射方向和稳定 Up 建立，不读取静态网格相机 Socket；进入/离开判定带滞回，避免玩家微调时在两颗行星间闪烁；
- 如果当前预测没有进入目标作用圈，预览可显示最近接近失败构图，但不能伪造一条有效接近；
- 失败重试时保留一条低透明度上次轨迹，便于比较本次微调。
- 首次有效近掠可使用极短的表现性慢镜、尾迹增强和音高抬升；慢镜只改变渲染/镜头时间，不改变求解器固定时间轴。

系统不能显示金色标准路线、正确角度、自动吸附方向或“再调高 0.13°”这类答案。它只应说明玩家当前方案会在哪一节点、因何失败。

首版推荐在 Space 档瞄准时自动显示较大的终局轨迹模式，同时保留近端弹弓操作画面；圆形小图可作为收起状态。具体展开方式与按键属于 UI 实施稿，不在算法中硬编码。

### 8.1 前缀成功集稳定器

稳定器只保护玩家已经找到的前缀，不寻找或填入下一段答案。状态按当前完整预测评估：

```text
Free
  -> NearFn         // 接近 Fn 鲁棒内核，给出可见的精密模式提示
  -> StableFn       // 在内核中持续满足确认时间
  -> NearF(n+1)
```

建议交互合同：

1. `NearFn` 只平滑降低方向灵敏度到普通值的约 `0.35–0.5`，不立即 Clamp；
2. 输入在离线认证的 `Fn` 内核中稳定约 `0.2s` 后进入 `StableFn`，HUD 明示“① 走廊稳定 / 1 of 3”；
3. `StableFn` 只把 `Yaw/Pitch/Power` 限制在完全位于 `Fn` 内部的信赖域；不能把输入吸向标准解中心；
4. 功率下界取保持 `Fn` 成立的离线认证边界，而不是“当前功率只能升不能降”；在不破坏 `Fn` 的范围内仍可升降；
5. 捕获与释放使用不同边界和确认时间，防止在边缘抖动；
6. 玩家始终可以使用明确的取消/重置操作退出稳定状态并恢复完整输入范围；
7. ①、②、③ 的稳定状态逐级缩小可调域，但永远不显示标准轨迹、下一目标精确角度或自动移动光标。

默认玩法采用上述可见稳定器。如果后续确实需要把输入永久硬锁在成功岛内，应作为独立无障碍选项，而不是默认规则。

### 8.2 首个不可恢复失败

一次预测只报告第一个不可恢复事件：

| 内部结果 | 玩家反馈 |
| --- | --- |
| `MissAssist1` | 未进入第一引力区，高亮 ① |
| `PlanetImpactN` | 第 N 次掠过太近，标出撞击点 |
| `WeakAssistN` | 进入正确侧但偏转或增能不足 |
| `ReverseAssistN` | 从减速侧掠过，显示负能量脉冲 |
| `MissNextAssistN` | 第 N 次成功，但出口未接入下一颗行星 |
| `WrongOrder` | 先进入了非期望行星作用区 |
| `SunBound` | 总能量不足，回到束缚轨道 |
| `PlaneMiss` | 从目标轨道平面上/下方错过 |
| `MissUFO` | 三次助推完成，但未进入 UFO 命中球 |
| `TargetHit` | 命中，进入结局 |

连续多次因同一原因失败时，可依次提供“文字原因 → 高亮相关行星 → 显示宽泛正确侧弧段”的渐进提示，始终不显示标准轨迹。

## 9. 状态机与重试

```text
Locked
  -> Ready
  -> Aiming
  -> StableF1
  -> StableF2
  -> StableF3
  -> Launched
  -> Assist1
  -> Assist2
  -> Assist3
  -> FinalApproach
  -> TargetHit
  -> Rescue
  -> Complete

Launched / AssistN / FinalApproach
  -> Failed(首个不可恢复原因)
  -> Reset
  -> Aiming
```

- `Locked`：尚未获得部件、找到槽位或完成装配；
- `Aiming/StableFn`：只计算当前预演，不推进世界飞行；稳定器可以限制输入信赖域，但不移动鸟体或自动填入答案；
- `Launched`：冻结当前 `Yaw/Pitch/Power`、局部布局版本、场景版本与求解器版本，用高质量配置重新积分一次并缓存权威点列、速度、事件和 Hash；
- `AssistN`：每颗行星每次发射最多触发一次，只有有效按序通过才推进；
- `Failed`：包括撞星、错误顺序、太阳束缚、越界、超时与 UFO 未命中；
- `Reset`：在黑屏内恢复点击太空弹珠袋前的 `FABTSM11AttemptSnapshot`，保留上次轨迹残影和当前输入，允许快速重试；
- `TargetHit` 后才切换破坏、镜头和剧情演出。

实际飞行只按冻结结果的 \(\mathbf r(t),\mathbf v(t)\) 插值。推荐使用位置/速度 Hermite 插值，而不是仅对稀疏位置做线性插值；编队横向轴使用以弹珠袋坐标系为初始条件的平行移动标架，避免 Frenet 标架在低曲率段翻转。

玩家可以在尚未瞄准 UFO 时提前发射。失败演出播放到第一个不可恢复原因已清晰可见：撞击播到撞击，错过行星播到明显分离，长时间束缚轨道可时间加速；建议把常规失败反馈控制在约 `3–6s`，再用约 `0.5–0.8s` 淡出至黑。黑屏期间恢复 Party、弹弓、镜头、环境和输入状态，不能重新生成 World 或清空库存。第一次正确飞行播放全部近掠演出；重试可缩短非关键镜头。

## 10. 资产交接契约

用户后续提供三颗行星与 UFO 静态网格时，每个资产只需满足表现契约：

- Pivot 位于视觉球心；
- 提供明确的视觉半径/缩放基准；
- 环系统可独立设置朝向，但不影响玩法球；
- 关闭复杂网格作为权威飞行碰撞；
- 材质、LOD 和 Nanite 可独立调整，不改变 Body Spec；
- 资产替换不得移动 Actor 或自动改写玩法半径；
- UFO 提供白色小鸟/舱体挂点；其可见 Actor 对齐 Preset 的独立几何接触中心，而不是较早的合格终端拦截中心。

三颗行星的位置、玩法球、作用圈和虚拟公转速度存于版本化 `FABTSM11FinaleLayoutPreset` 或专用 DataAsset。位置必须是太空弹弓局部坐标，不得保存为关卡绝对世界 Transform。不能把这些数值烘焙进静态网格，也不能从网格 Bounds 每次自动推断。

## 11. 实施前验收矩阵

### 11.1 算法正确性

- [x] 同一初态、场景版本和求解器版本重复运行，点列、事件序列和结果 Hash 一致。
- [ ] 预演和实际飞行使用同一求解器；30/60/120 FPS 下助推事件、终止原因和 UFO 命中结果一致。
- [ ] 预演点与实际运动的最大偏差不超过最小碰撞半径的 `0.1%`，且不会随三次助推单调累积。
- [x] 作用圈、碰撞和命中使用 swept 检测，高速时不穿星或穿过 UFO。
- [x] 太空段无空气阻力；保守段做步长减半收敛测试，能量漂移处于批准阈值内。

### 11.2 关卡因果

- [x] 无助推时必然被中心天体束缚或无法到达合格终端拦截/几何 UFO。
- [x] 关闭 ①、②、③ 中任意一项都不能进入 F4，且独立几何接触旁路为 0。
- [x] 只有 `①→②→③` 的批准飞越侧和顺序能形成成功轨迹。
- [x] 错误侧不获得同额正助推，过近会撞星，过远会自然衰减。
- [x] 行星 ① 只有在接近最大功率的连续区间内可达；低功率域不存在进入 ① 后接通终局的旁路。
- [x] **正式门槛：**验收记录冻结完整 `Yaw × Pitch × Power` 输入域、三轴网格步长、递归边界精度和最大飞行时域；在该合同下只发现一个连通的 `F4` 成功分量。
- [x] 围绕唯一 `F4` 族的最终局部精化闭包满足 `F4 ⊂ F3 ⊂ F2 ⊂ F1`；每个局部前缀主分量都有批准的最小角度/功率内宽和可供稳定器使用的内接信赖域，不外推为全域 `F1/F2` 微拓扑证明。
- [x] 全域内不存在跳星、错序、多圈重入或重复收割同一助推的隐藏成功路径。
- [x] 先冻结最小角度/归一化输入容差和输入映射，再换算屏幕宽度；v1 F4 实心瞄准矩形为 `20×18 px`，覆盖 14 个连续 Power 切片。

以上勾选表示 M11-B 的有限离散认证已通过，不是对连续实数输入域的数学唯一性证明；玩家操作、HUD 与实飞仍由 M11-C 验收。

### 11.3 玩家可读性

- [ ] 玩家能从全景图指出当前轨迹在哪一颗行星、因何失败。
- [ ] 三次有效助推都产生可见的速度、尾迹、声音和能量档位变化。
- [ ] 虚线仍只表示球后遮挡，不被复用为“预测失败”或“弱助推”。
- [ ] HUD 不显示标准答案轨迹或精确修正角度。
- [ ] 前缀稳定器的降敏、捕获和释放都有明确视觉/声音反馈；不会移动准星到标准解，且玩家可主动退出。
- [ ] 每次预测有效接近 ①/②/③/UFO 时，远端预览按当前最早未完成目标稳定切换，无边缘闪烁。
- [ ] 失败后保留上次轨迹并快速回到相同瞄准输入。

### 11.4 叙事与资产

- [ ] 太空弹弓只有在两根 `SpaceStake`、一根 `SpaceCord`、唯一 Space-only 槽位和装配条件都满足后解锁。
- [ ] `BP_Cute_Bird_0` 白色小鸟只作被俘表现，不进入四鸟 Party。
- [ ] 命中 UFO 后四只彩色小鸟均参与攻击/救援演出。
- [ ] 更换任一静态网格不会改变确定性轨迹或成功输入岛。
- [ ] M9 练习卫星仍可存在于同一 World，但不出现在 M11 Body Specs、加速度查询或命中事件中；三颗助推行星只由终局局部布局预设生成。
- [ ] 同一 World 的星空/雾云切换不会重置库存、Space 装配、Party 或当前鸟；失败黑屏后能恢复进入终局发射模式前的快照。
- [ ] 四鸟同时从太空弹珠袋出发，深空段无 Chaos 漂移、互撞或队形翻转。

## 12. 建议实施拆分

| 子阶段 | 交付 | 明确不做 |
| --- | --- | --- |
| `M11.0` | LaunchSite 唯一 Space-only 槽、无玻璃建筑、Space 桩/弦配方、M9 练习卫星远置与 M11 引力隔离 | 三行星积分、终局 HUD、星空演出 |
| [`M11-A`](M11AGravityAssistSolverDesign.md) | 无 World/Actor 的纯数据求解器与测试夹具；中心束缚、单颗自然偏转、虚拟动量换能、步长收敛、确定性与助推消融自动化 | 地图、实飞接管、美术、终局演出 |
| [`M11-B`](M11BFinaleLayoutCertificationDesign.md) | 局部布局离线搜索、认证预设、三颗 Body Actor、合格终端拦截/独立几何 UFO、全 `Yaw × Pitch × Power` 的 F4 唯一性与局部前缀内核报告 | 完整 HUD、终端 coast 与剧情 |
| `M11-C` | 抽取并复用轨道投影，加入简笔行星/UFO、逐目标接近预览、前缀稳定器；Space 实飞同源接管 | 标准答案、隐形吸附、Chaos 深空飞行 |
| `M11-D` | 四鸟同袋/队列、星空环境切换、白鸟救援、失败黑屏复位与完整 PIE 验收 | 独立终局地图、运动行星、多人同步 |

M10.1-D 的通用道路外目标选择与走廊系统继续延期。M11 使用已知、固定的三行星和 UFO 场景，建立自己的顺序事件与走廊数据，不把尚未完成的通用目标系统设为前置依赖。

## 13. 已冻结的产品决策

1. **物理路线**：采用“固定视觉 + 虚拟公转动量 + 自然偏转 + 平滑切向换能”混合模型。
2. **场景路线**：沿用 Task Graph World；三颗助推行星和 UFO 由终局局部布局预设生成，不使用手工独立地图或绝对世界坐标。
3. **玩家输入**：开放完整 `Yaw × Pitch × Power`；行星 ① 的距离建立高功率门槛。
4. **输入辅助**：采用可见、可退出的前缀成功集稳定器，不采用隐藏的硬成功岛吸附。
5. **四鸟表现**：四鸟同时进入钢铁太空弹珠袋，深空段按一条预计算权威轨迹组成固定队列，不使用 Chaos。
6. **轨迹界面**：复用 M10.1-C 投影语义，以极简平面线条表现三颗行星和 UFO，并按当前目标切换远端接近预览。
7. **环境与重试**：同一 World 切换星空并关闭雾云；错误发射播放到失败可读后黑屏恢复到点击弹珠袋前。
8. **验收门槛**：完整 `Yaw × Pitch × Power` 输入域的 F4 唯一性、围绕该族的局部前缀集嵌套、助推消融和旁路排除均为阻断性验收项。

当前 [M11.0 前置收口](M110PreFinaleClosureDesign.md) 与 [M11-A 纯数据求解器](M11AGravityAssistSolverDesign.md) 已完成；[M11-B](M11BFinaleLayoutCertificationDesign.md) 的 C++、编译、冻结报告与全新进程自动认证也已完成。围绕全域发现出的唯一 `F4` 族，最终局部精化闭包得到 `F=(6244,1890,981,558)`、局部分量数 `(1,1,1,1)`、TargetHit `558`、几何接触旁路 `0`；F4 为 `20×18 px`、14 个连续 Power 切片。v1 通过增强行星③虚拟动量并要求终端 `Q>=0.95`，把宽前缀练习走廊和最终高质量拦截资格分开。完整 Hash 与扫描合同见 [M11-B 第 9.2 节](M11BFinaleLayoutCertificationDesign.md#92-v1-冻结认证报告)。

默认下一步是 M11-B PIE 验收；该项通过前不转入 M11-C。当前 `Content/Maps/Test.umap` 未由本阶段 C++ 实现修改，PIE 必须让实际地图/GameMode Blueprint 接入 M11 GameMode/Finale System 生命周期。

## 14. 资料来源

以下页面于 2026-07-27 访问：

1. [NASA：A Gravity Assist Primer](https://science.nasa.gov/learn/basics-of-space-flight/primer/)
2. [NASA：Cassini Gravity Assists](https://science.nasa.gov/mission/cassini/gravity-assists/)
3. [ESA：What are gravity assists?](https://www.esa.int/Enabling_Support/Operations/What_are_gravity_assists)
4. [NASA NTRS：Patched Conic Trajectory Code](https://ntrs.nasa.gov/api/citations/20120006596/downloads/20120006596.pdf)
5. [NASA NTRS：Designing an Interplanetary Trajectory](https://ntrs.nasa.gov/api/citations/20220000576/downloads/Interplanetary%20Trajectory%20Optimization%203%20-%20Designing%20an%20Interplanetary%20Trajectory.pptx.pdf)
6. [NASA NTRS：Space Flight Handbooks, Volume 3 — Planetary Flight Handbook, Part 9: Direct and Venus Swingby Trajectories to Mercury](https://ntrs.nasa.gov/citations/19700019224)
7. [JPL：B-plane targeting explanation](https://www.jpl.nasa.gov/news/mars-tugging-on-approaching-nasa-rover-curiosity/)
8. [NASA NTRS：OSIRIS-REx B-plane corridor and Monte Carlo](https://ntrs.nasa.gov/api/citations/20240000803/downloads/OREx_ER_Entry_Targeting_Strategy_and_Man_Perf_v3.pdf)
9. [Angry Birds：Hall of Games](https://www.angrybirds.com/explore/hall-of-games/)
10. [Rovio：Angry Birds 2 Space](https://www.rovio.com/articles/angry-birds-2-space-arrives-october-24th/)
11. [Kerbal Space Program 1.7：Room to Maneuver 官方更新说明](https://store.steampowered.com/news/posts/?appids=220200&enddate=1557151214)
12. [Epic Games：Physics Sub-Stepping](https://dev.epicgames.com/documentation/en-us/unreal-engine/physics-sub-stepping-in-unreal-engine)

返回父级：[AngryBirdsToSpace 游戏设计稿](AngryBirdsToSpaceGameDesign.md) · 前置子稿：[M11.0 终局前置收口](M110PreFinaleClosureDesign.md) · 求解器子稿：[M11-A 纯数据求解器](M11AGravityAssistSolverDesign.md) · 当前子稿：[M11-B 局部布局与全输入域认证](M11BFinaleLayoutCertificationDesign.md) · 返回交接入口：[ABTS 项目工作流](ABTSProjectWorkflow.md)
