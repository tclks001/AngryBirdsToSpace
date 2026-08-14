# M7.3 Beam-C3 V3 Stage-1 检查点（2026-08-07）

## 1. 状态摘要

- 工作树：`feature/m7-buildings`；检查点建立时 HEAD 为
  `e6d74a7852efb91a846d89d1d548efe9ef67d10d`，工作区包含本轮及此前未提交修改；
- 架构：由 WFC 语义包络先生成芯体/支座骨架，再向外生成四面外框、楼层、桥和屋顶；
- Stage-1：旧实现和 grounded rewrite 都曾取得静态全绿，但现在分别被两轮视觉反例降级。grounded
  rewrite 的 `G0 16/16`、`G1 4/4`、5×6 `30/30`、BeamD15 `32/32` 与 ForceUnity 成功只作为本轮
  修改前基线；当前三项新合同尚未取得新的编译/自动化证据；
- 视觉：第一轮拒收是顶部固定截面 RoofCourse 和主体无显式接地芯体；该根因已纠正。第二轮检查确认
  方向正确，但 shared rail 没有替换双方 core 槽位、多个 core 各自长成分离薄片而非共同外框、core
  crossing 只有半格搭接。当前仍为视觉拒收，不得合并或进入物理阶段；
- 物理：`PhysicalStability=NotEvaluated`，本轮没有运行 Chaos、可见 PIE、静置、扰动或攻击；
- 受保护资产：`Content/Maps/PlanarPhysicsTestMap.umap` 继续视为用户所有的脏二进制，本轮不得覆盖或暂存；
  用户本次编辑器检查后的当前 SHA-256 为
  `49A0DEE23E64CF5CDF8A14F9E36719224AEC13907777F8512540DE8ECF30FD5E`。

旧静态通过既不允许宣称真实摩擦稳定，也不再允许宣称 skeleton-first 几何已经完成；本检查点第 10 节
证据已经被第二轮视觉反例再次降级。当前恢复点是第 11 节冻结的三项合同及其最小反例，尚未恢复到
人工视觉验收入口，同样不证明物理稳定。

## 2. 实现文件

- 设计：`Docs/M73BeamC3V3SkeletonFirstBuildingGenerationDesign.md`；
- 类型：`Source/ABTSRuntime/Private/Building/ABTSM73BeamC3V3SkeletonFirstTypes.h`；
- 生成器：`Source/ABTSRuntime/Private/Building/ABTSM73BeamC3V3SkeletonFirstGenerator.h/.cpp`；
- 自动化：`Source/ABTSRuntime/Private/Building/ABTSM73BeamC3V3SkeletonFirstAutomationTests.cpp`；
- D1 路由：`Source/ABTSRuntime/Private/Building/ABTSM73BeamD1BrickCompiler.h/.cpp`；
- 公开结果：`Source/ABTSRuntime/Public/Building/ABTSM73BeamD1Types.h`。

旧 V1/V2 源码和取证仍保留，但不作为 V3 生产 fallback。

## 3. 冻结生产流程

```text
D0 Profile/Tier + finite WFC semantic selection
  -> sorted semantic envelope
  -> directed grounded components and vertical witnesses
  -> freeze SupportedSpan/shared phase, protected undercroft and endpoint required top
  -> explicit compact grounded core stations
  -> continuous ground-to-required-top alternating XY core
  -> replace both endpoint core slots with the same full-traversal shared members
  -> complete bridge course band and preserve protected undercroft
  -> one building-group common perimeter / core spokes / exterior posts
  -> group-level four-face grounded exterior-post certificate
  -> per-height primitive-tapered roof
  -> grounded-core / lineage / shared-endpoint / roof-core-separation validator
  -> exact Brick/IR-cap preflight
  -> one canonical Beam-A-compatible IR emission
  -> one RebuildBearingContacts
  -> exact predicted/actual Bearing pair-set equality
  -> Beam-C Generate (read-only)
  -> D1 one-Member:one-Brick compile
```

结构候选只执行一次。第一个语义有效 WFC 结果进入 V3 后，任何结构或编译失败都会立即返回；不得换
密度、补柱、运行 closure 或进入 Seed/Attempt 参数搜索。

## 4. 当前硬合同

1. 真实全局 Ground 为 `MinZ=0`，上层量体只能通过正 XY 重叠和上下表面接触的有向 witness 接地；
2. 每个接地 component 至少有一个显式 compact core；其基座来源仅限 Body，最低 CoreCourse 实体底面为
   ground，之后每 `36 cm` 严格交替 X/Y。默认止于 Body 顶部；仅当合法 shared course 位于更高层时，
   同一接地芯体才可在 `Body ∪ Crown` 实体覆盖内连续延到精确的 upper-sandwich course。Crown 不得新建
   独立、悬空或无 span 需求的 Core 身份；
3. local shell cell 的完整 `36 cm` 实体边必须由 Body 包络覆盖；building-group perimeter 只额外允许
   冻结的一个 `36 cm` facade halo/外挑，不得任意越出语义轮廓。through/facade/floor/exterior-post 必须
   记录可回溯到显式 core 的 inward chain；
4. 所有 Member 在创建时登记 ground 或 lower-seat；一般 Member 至少一座，首层屋顶和 span rail 使用
   两个分离座；
5. 同一候选的全部 component/core/span 归入一个 building group；四面 perimeter、through spokes 和 exterior
   posts 按 group 轮廓共同生成，四个面各有至少两个不同、沿 seat DAG 接地的 Z-post XY 站位。水平梁
   不计；每 component 独立四面 mask 不得代替 group 证书；
6. SupportedSpan/shared 的每条 rail 只生成一个 Member，并以同一 MemberId 替换两个 endpoint core 的同一
   `(course,rail)` 槽位；实体从负端 core 最外物理面贯穿到正端 core 最外物理面。X 桥固定在绝对偶数
   course、Y 桥固定在绝对奇数 course，两端 `c-1/c+1` 必须是正交真实 CoreCourse；桥内还须有承托于
   两条 shared rails 的正交 diaphragm，opening 下方登记 ProtectedVoid，禁止地柱或 rescue post；
7. 正体积穿透、ProtectedVoid、Member/Joint/Bearing 容量、720 cm、Brick 窗均在 Beam-C 前失败关闭；
8. `RebuildBearingContacts` 的实际 canonical pair 集必须与计划集合完全相等，重复或等数量替换失败；
9. Roof 只拥有 RoofCourse；Prism/Pyramid footprint 必须随对应高度的 Crown slice 退缩。Profile/
   Resolved/Grammar/WFC/envelope/member-kind/core-origin/inward/shared-endpoint/seat/void/component CRC 都进入
   identity；
10. Beam-C 只读，`ClosurePasses=0`、`ClosureAdded=0`、`Advisory=0`；
11. 每个 core X/Y crossing 的两个平面重叠维度都至少为 `36 cm`、Bearing 面积至少为 `1296 cm²`；core
    rail 在 transverse station 外延 `18 cm` 形成完整 cap，core-derived through member 再从 cap 外端面
    向共同外框外挑至少一个 `36 cm` 单元或连续到 facade，不得退后半格或正体积穿透。

Stage-1 当前只登记 SupportedSpan undercroft void；一般 MustVoid 尚没有上游权威输入，不得宣称已覆盖。

## 5. 固定密度 Recipe

基础 `horizontal/vertical`：

| E1 | E2 | E3 | E4 | E5 | E6 |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 18/18 | 12/14 | 11/10 | 8/8 | 9/7 | 8/5 |

显式覆盖：`TipOver.E2=10/14`、`TipOver.E4=8/7`、`ColumnBreak.E5=9/6`、
`TipOver.E5=7/6`、`TipOver.E6=8/4`。这些值是固定 Profile/Tier 合同，不读取 Seed 或 Attempt。

## 6. 已尝试、失败或不足的方案

| 方案 | 观察 | 结论/替代 |
| --- | --- | --- |
| 在重复 Member 反例中先持有数组引用再 `Add` | TArray 扩容使夹具引用失效并崩溃 | 先复制值再 Add；这是测试夹具错误，不是生产几何失败 |
| 用相邻 cell 最小 SourceId 给 Z 节点归属 | E6 节点可能声明一个不覆盖该节点/切片的 source | CorePost 按真实节点和 slice 选 source；外立 fallback 使用受控同 root/active-slice/halo 选择器 |
| 遇到非法 post 直接跳过 | 后续 lower course 缺 seat，DAG 仍失败 | 不把“少发一砖”当修复；先证明合法 source/seat，否则当前计划失败 |
| 把非法 CorePost 改名为 exterior post | 若仍使用 component union，错误 source 可继续假绿 | ShellFace/Post 只向声明 source 做受控投影，foreign/wrong-source 反例失败关闭 |
| 只检查 cell 中心 | 可让大部分实体边落到 WFC 外 | 检查四条实际发射边的完整 solid；中心只决定稳定 provenance |
| 要求整个 cell 由同一个 volume 覆盖 | Body/Crown 合法接缝被误拒绝 | 对排序后的 `Body ∪ Crown` 盒并集做 exact solid coverage |
| 以 0.5 cm 合并 coverage cuts | `0/0.4/0.8` 链可吞掉超过容差的窄漏洞 | cuts 只按机器 epsilon 去重；0/0.4/0.8 chained-sliver 反例固定 |
| 屋顶两个 seat 固定至少相距 72 cm | Slide/Tip 的窄 Crown 有合法相接 rail，却被判无座 | 最小中心距改为一个 36 cm 截面；允许相接，不允许正体积重叠 |
| Bearing 只比较 planned/actual 数量 | 丢失一个预测 pair 后由意外 pair 等量替换仍可假绿 | 比较完整 canonical pair set，并拒绝重复/非法 pair |
| SupportedSpan 只按 source 找包络 | owner、component、role 或 axis 被未来重构改坏时仍可能借同一包络通过 | 显式要求 owner=source、source 为 span、component 为空、role 为 BridgeRail、axis 匹配且恰有两个 endpoint seats；owner mutation 反例固定 |
| 四面证书只 OR 水平 shell FaceMask | 没有外部 Z post 也可能得到 `0x0f` | 每 component/face 统计 distinct grounded Z station，硬门为至少 2 |
| 在 30 格内继续邻域搜索密度 | 容易重新形成 Seed/参数路径依赖 | 每个失败只修一个有解释的几何/窗口假设，随后冻结离散 Recipe |

矩阵收敛记录为 21/30、23/30、27/30、29/30、30/30，对应日志：

- `Saved/Logs/BeamC3V3-Stage1-Matrix-20260807-014610.log`；
- `Saved/Logs/BeamC3V3-Stage1-Matrix-20260807-015147.log`；
- `Saved/Logs/BeamC3V3-Stage1-Matrix-20260807-015419.log`；
- `Saved/Logs/BeamC3V3-Stage1-Matrix-20260807-015549.log`；
- `Saved/Logs/BeamC3V3-Stage1-Matrix-Final-20260807-015758.log`。

这些轮次是有界的合同修正，不是 Seed、Attempt、cap、容差或连续密度扫描。最终终审又收紧 source、
full-solid、Bearing-set 和 exterior-post 证书，并在相同 30 格上重新取得 30/30。

## 7. 首轮历史证据（已被视觉反例降级）

### 7.1 编译

```text
C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat
  AngryBirdsToSpaceEditor Win64 Development
  -Project=<本工作树绝对 uproject>
  -WaitMutex -ForceUnity -DisableAdaptiveUnity
```

ForceUnity Development Editor 全链接成功；最终一次约 22.87 秒。

### 7.2 NullRHI 静态自动化

| Filter | 结果 | 日志 |
| --- | ---: | --- |
| `ABTS.M73DAG.BeamC3V3.Stage1.G0` | 15/15 | `Saved/Logs/BeamC3V3-Stage1-G0-FinalAudit-20260807.log` |
| `ABTS.M73DAG.BeamC3V3.Stage1.G1.Boundary` | 4/4 | `Saved/Logs/BeamC3V3-Stage1-G1-FinalAudit-20260807.log` |
| `ABTS.M73DAG.BeamC3V3.Stage1.Matrix` | 30/30 | `Saved/Logs/BeamC3V3-Stage1-Matrix-FinalAudit-20260807.log` |
| `ABTS.M73DAG.BeamC3V3.Stage1` | 49/49 | `Saved/Logs/BeamC3V3-Stage1-All-FinalAudit-20260807.log` |
| `ABTS.M73DAG.BeamD15` | 32/32 | `Saved/Logs/BeamD15-V3-Static-FinalAudit-20260807.log` |

每个 fresh 进程约 29～30 秒并包含 UE 启动。BeamD15 32 叶包含 30 个生产 Profile/Tier、
`ColumnHighTierClosure` 和 `LowTierRoofBearingContinuity`；旧约 20 分钟 closure 黑盒没有进入 V3 路径。

## 8. 首轮下一步与恢复点（随后被视觉拒绝）

下一步只有人工视觉验收：检查五种剪影、六档密度/高度、分层 XY 节奏、四面外框 Z 柱、退台、屋顶、
SeamRelease 双端共享 course、桥下净空、门洞，以及是否存在穿屏长梁/重叠假梁/漂浮立面。

若视觉不批准：只修改可解释的形体、固定 Recipe 或结构语言，先跑最小失败叶，再按 G0/G1/Matrix/
BeamD15 阶梯恢复；不得提前投入 Chaos。视觉批准后另立物理设计和实验卡，重新定义材料、质量、摩擦、
solver、实时静置、扰动与攻击门。本检查点不能作为真实稳定证据或 master 合并的视觉批准。

## 9. 视觉拒收补充（2026-08-07）

### 9.1 直接观察与代码归因

- 用户在物理测试地图中观察到高位井干塔，芯体没有从地面开始；
- 高位塔来自 Crown 阶段固定 footprint 的 `Roof/RoofCourse`，不是 D1/Preview 坐标偏移；
- Body 阶段只有整栋 sparse raster bands、外柱和稀疏 Z post，没有显式 compact horizontal core；
- `CoreCellCount`、`CorePlanHash` 和全局 ground reachability 都能在没有显式 core 时为真，解释了旧矩阵假绿。

### 9.2 当前恢复点

旧证据表全部降级为“缺失合同的历史反例”。本轮实际按以下顺序恢复：

```text
Grounded Body Core
  -> Frozen Shared Requirement / Endpoint Required Top
  -> Core-derived Four-face Shell
  -> Floor/Infill
  -> Tapered Roof
  -> Shared Course bound to both completed cores
```

先建立显式 core/member provenance 与 topology validator，并固定 core 抬高、删除 shell origin、shared
seat 换成非 core、非法 Crown core 等反例；随后依次完成 shared 绝对相位、精确连续 Crown extension、
一次 30 格计数调查、一次窗口重分区和 `DropTrigger.E4` 有限 roof solver，再扩大到 G0/G1/Matrix/
BeamD15。过程中没有扫描密度、Seed 或结构 Attempt，也没有运行 Chaos。用户视觉批准仍是进入物理研究
的人工门。

## 10. 接地重写首轮静态证据（已被第二轮视觉反例降级，2026-08-07）

### 10.1 实现结论

- 顶部“井干塔”已确认是固定 footprint 的 `RoofCourse`，不是 D1、Preview 或 Actor 坐标偏移；该路径已
  删除，屋顶改为按各高度 Crown slice 收分；
- Body 现在先建立独立 `FCoreCellPlan`，从真实 `Z=0` 逐层交替发射两根 X/Y CoreCourse，再由芯体向外
  派生 through/facade/floor/exterior-post，所有 shell 均带 core origin 与 inward lineage；
- shared requirement 在 shell 规划前冻结桥轴、绝对 course 奇偶相位、opening 和两端 required top；
  shared member 在两端 core 均可引用后加入完整计划，并与其余成员一次 canonical emission。E5 的
  `EndpointSeatUnavailable` 由此消失；E6 的剩余失败来自较矮端芯体只到 Body 顶部；
- 对高位合法 span，endpoint core 只连续上延到 `shared+1`，validator 从实际 endpoint 反推精确顶层并
  拒绝缺层、多层、断层和悬空 Crown core；
- roof 沿承托轴只扩到声明 lower seat，横向站位从包络边界与实际障碍物边界组成的有限集合确定性选择，
  解决 `DropTrigger.E4` 的 envelope 冲突和 2 cm roof/shell 穿透；没有连续坐标扫描。

### 10.2 有界失败与停止

一次性 5×6 计数调查得到 20/30，其余 10 项中 9 项只是旧 Brick 窗不覆盖真实接地结构，1 项是
`DropTrigger.E4` 屋顶几何冲突。调查没有在矩阵中改变 Seed、Attempt 或密度。此前预登记的
H8/H9/H10 诊断得到 1472/1348/1348，平台出现后停止 H11/H12；最终窗口根据固定候选的实际 D1
Brick 数一次性重分区，而不是用估算值追逐单一 Seed。

当前窗口：

| Tier | 通用 | `ColumnBreak` |
| --- | ---: | ---: |
| E1 | 20–149 | 20–149 |
| E2 | 150–349 | 150–349 |
| E3 | 350–799 | 350–799 |
| E4 | 800–2099 | 800–1599 |
| E5 | 2100–3399 | 1600–2199 |
| E6 | 3400–5499 | 2200–3499 |

### 10.3 Fresh 静态门

| Filter | 结果 | 日志 |
| --- | ---: | --- |
| grounded-core anti-fake | 通过 | `Saved/Logs/BeamC3V3-GroundedTopology-CrownExtension-20260807-113926.log` |
| determinism | 通过 | `Saved/Logs/BeamC3V3-Determinism-AfterGroundedRewrite-20260807-115108.log` |
| `ABTS.M73DAG.BeamC3V3.Stage1.G0` | 16/16 | `Saved/Logs/BeamC3V3-G0-AfterGroundedRewrite-20260807-115156.log` |
| `ABTS.M73DAG.BeamC3V3.Stage1.G1` | 4/4 | `Saved/Logs/BeamC3V3-G1-AfterGroundedRewrite-20260807-115431.log` |
| `ABTS.M73DAG.BeamC3V3.Stage1.Matrix` | 30/30 | `Saved/Logs/BeamC3V3-Matrix-Final-20260807-115526.log` |
| `ABTS.M73DAG.BeamD15` | 32/32 | `Saved/Logs/BeamD15-V3-GroundedRewrite-20260807-115751.log` |

关键叶：`ColumnBreak.E5=1951`、`ColumnBreak.E6=2515`、`SeamRelease.E6=4907`、
`DropTrigger.E4=1800`；全部满足
`GroundedCore==ExplicitCore`、`SuspendedCore=0`、`CoreDerivedShell==Shell`，E6 另有两个 shared course。
ForceUnity Development Editor 完整链接通过。

以上当时只把恢复点推进到“可供人工视觉验收”，现已由第二轮视觉反例证明仍缺 shared slot、building
group 和 full-face bearing 合同。本轮没有启动图形化 Editor、可见 PIE 或 Chaos，所有生产结果仍为
`Physical=NotEvaluated`；不得把 30/30 写成当前实现已完成、纯摩擦稳定或视觉已批准。

## 11. 第二轮视觉拒收恢复点（2026-08-07）

### 11.1 三项冻结合同

本轮只处理以下三个彼此可独立反证、但必须共同成立的合同；完整定义见
[V3 设计第 16 节](M73BeamC3V3SkeletonFirstBuildingGenerationDesign.md#16-第二轮视觉拒收与共同建筑组纠正合同2026-08-07)：

1. **真正 shared course。** 两端对齐 core 的同一 course/rail 槽位引用同一个 MemberId；原局部 rail 被
   删除/替换。该 Member 从一端芯体最外物理面贯穿另一端最外物理面，在每端取得两次 lower core
   cross-bearing；两条纵 rail 上还有至少一组正交 bridge diaphragm，形成完整 bridge band；
2. **一个 building group 的共同外框。** 多个 grounded components/core cells 保留独立接地证据，但只生成
   一个候选级 perimeter/band/post 系统，并由 core spokes 和 shared course 连成同一结构图。不得继续以
   “每 core 一片四面 shell”生成六栋相互分离的薄楼体；
3. **完整承压面和外挑。** 固定 `36 cm` 截面不变；每个 core X/Y crossing 的 XY overlap 至少
   `36 × 36 cm`。core rail 在交叉站外延 `18 cm` 形成 cap，through/facade 从 cap 外端面向共同外框继续
   至少一个 `36 cm` 单元，不得在连接处退后半格。

第二轮视觉反例的根因已经定位为 owner/slot、生成分组和端点几何合同缺失，不是密度、Seed、Attempt、
预算 cap、容差或 Chaos 参数。任何修改上述冻结数值的实验都不属于当前恢复路线。

### 11.2 单向验证状态机

```text
S0 文档/失败身份冻结
  -> S1 SharedSlot / BuildingGroup / FullFaceBearing 三个最小 anti-fake
  -> S2 固定 SeamRelease.E6 生产叶
  -> S3 固定无 Span 普通边界叶
  -> S4 受影响 G0 / G1
  -> S5 一次 Matrix 5×6，随后一次 BeamD15
  -> S6 用户编辑器视觉验收
```

- S1 必须证明同 member 双槽位、完整 traversal、bridge diaphragm、group-level 四面证书、全 core 连通和
  `1296 cm²` core bearing；删除/缩短/退半格反例必须分别在 Beam-C 前 fail closed；
- 前一状态未过不得运行后一状态。5×6 和 BeamD15 只作里程碑，不作调试器；
- Matrix 若只暴露共同外框带来的确定性数量窗变化，只允许一次精确 owner-kind 计数调查，然后回到预算
  设计层一次性决策；不得在 30 格中改变密度追窗；
- 同一 `FailureCode + PlanHash + GeometryHash + DAGHash` 两次不变、同一假设两轮专项仍失败、60 分钟无
  新增可排除结论或同路线 4 小时未越过当前最小门，以先到者停止并更新检查点；
- 禁止扫描 Seed、Attempt、horizontal/vertical units、min gap、merge gap、截面、预算 cap、720 cm、
  Bearing 容差、摩擦或 solver；禁止为 `SeamRelease.E6` 增加无法推广到合同的临时 owner/几何特判。

### 11.3 当前证据与保护项

- 新合同状态：**设计已冻结，代码/编译/专项/矩阵证据待本轮主实现完成后补录**；
- 当前允许的证据层：确定性几何、Bearing/Load DAG、Brick 预算、用户编辑器视觉；
- 当前禁止的证据层：Stage-0/完整建筑 Chaos、可见 PIE 静置、扰动和攻击；统一记录
  `PhysicalStability=NotEvaluated`；
- `Content/Maps/PlanarPhysicsTestMap.umap` 仍是用户所有的脏二进制，不修改、不覆盖、不暂存。当前登记
  SHA-256 继续为 `49A0DEE23E64CF5CDF8A14F9E36719224AEC13907777F8512540DE8ECF30FD5E`。

## 12. 共同外框承重闭合暂停检查点（2026-08-07 20:29 +08:00）

### 12.1 工作区与证据层

- 分支：`feature/m7-buildings`；检查点建立时 `HEAD=e6d74a7852efb91a846d89d1d548efe9ef67d10d`；
- 工作区包含本轮及此前未提交修改，**没有建立提交**；继续工作前必须先检查 `git diff`，不得回退或覆盖；
- 唯一允许的 UE `C:\Program Files\Epic Games\UE_5.8` 下，ForceUnity Development Editor 全链接成功；
  本轮最后一次构建约 35.05 秒；
- 当前只取得精确 `ColumnBreak.E6` 的负证据。G0、SeamRelease.E6、G1 分组、5×6 Matrix、BeamD15
  均未在当前最终源码上重跑，不得继承更早版本的绿灯；
- 没有启动图形化 Editor、可见 PIE 或 Chaos；`PhysicalStability=NotEvaluated`；用户视觉也尚未对当前源码
  重新验收；
- `Content/Maps/PlanarPhysicsTestMap.umap` 继续是用户所有的脏二进制，本轮未修改、覆盖、暂存或重放；
  SHA-256 仍为 `49A0DEE23E64CF5CDF8A14F9E36719224AEC13907777F8512540DE8ECF30FD5E`。

### 12.2 本轮已落地但尚未通过完整门的实现

1. common-frame gap 只在完整 `36 cm` cell face 上切分；删除原始浮点 `Lerp` 等分和 generic collapse 对
   Core/Shared 几何的任意吸收、拉长；ProtectedVoid 邻接的不足一格 interior remainder 省略，facade
   remainder 继续 fail closed；
2. shared course 使用同一 Member 替换两端 core 的同 course/rail 槽位，最长 `720 cm`，并保留
   `C/C+2` 两组长 rail 与 `C+1` 正交 diaphragm；所有 component/core/span 归入一个 building group，
   common perimeter 在 aggregate union 外生成；
3. core X/Y rail 在 transverse station 外延 `18 cm`，以取得完整 `36×36 cm` crossing；RoofCourse
   沿 lower fixed station 外延半截面并把横向站位收进完整承压 footprint；
4. support closure 固定为两遍，不增加 retry。第一遍允许仅用于几何成形的 self-centre fallback；第二遍
   已禁止该降级，并在 post pruning 后增加独立终态 support-hull fail-closed 审计；
5. 新增与 Beam-C 反力传播同构的 reaction-weighted load-resultant 计算：Member 自重按长度，水平梁按合力
   在相邻 support station 间分配反力，同站位按接触面积分配，Z Member 按总接触面积分配。第二遍闭合
   使用完整上部 DAG 合力，不再用“所有长梁都必须靠近两端落座”的过保守代理；
6. 已加入“把同线下级 carrier 向合力目标延长一个完整 cell，再放 transverse cap/post”的有界候选，
   但固定 E6 证明该精确延长进入 ProtectedVoid，当前不会落地；此候选仍是未获通过的诊断实现，不能称为
   完成方案。

### 12.3 本轮有界实验与结论

| 实验 | 精确观察 | 结论 | 日志 |
| --- | --- | --- | --- |
| 终态 support audit | 首个终态反例为 worklist 内的 CoreCourse；旧第二遍实际仍可保留 self-centre candidate | “第二遍已 fail closed”的旧注释不真实；必须封死候选选择阶段的降级，而不只封死空候选分支 | `Saved/Logs/BeamC3V3-G1-ColumnBreak-E6-FinalSupportAudit-20260807-200653.log` |
| 禁止 pass-1 self fallback | 失败前移到 Q52，过保守 end-zone 要求为 `-413.5..161.5`，屋顶/芯体占位使端部支点不可放置 | 所有长梁强制端部承座会误拒绝实际合力居中的构件，撤回；不得改 margin、gap 或容差恢复 | `Saved/Logs/BeamC3V3-G1-ColumnBreak-E6-Pass1NoSelfFallback-20260807-200908.log` |
| 只用直接上载荷范围 | Q52 仍要求 `-324..0`，而 Beam-C 旧运行并未拒绝该 Member | 未加权 min/max 仍过保守，不能替代完整 load DAG 合力 | `Saved/Logs/BeamC3V3-G1-ColumnBreak-E6-DirectLoadFinalClosure-20260807-201514.log` |
| reaction-weighted closure | 唯一当前闭合目标收敛到 Q44、X 向 648 cm ThroughCourse；计划合力 `X=-180.3`，现有右端 support 只到 `-198` | 与旧 Beam-C 最终合力 `X=-188.64` 指向同一缺失右支点；不是预算、spread、coverage 或容差问题 | `Saved/Logs/BeamC3V3-G1-ColumnBreak-E6-ReactionWeightedClosure-20260807-201952.log` |
| 一格 carrier 延长 | Q42 同线 carrier `-702..-198` 延到 `-162` 后自身支撑仍有效，新增长度恰为 `36 cm`，但候选被占位拒绝 | 需要区分实体碰撞与语义保留区，不得直接忽略 blocker | `Saved/Logs/BeamC3V3-G1-ColumnBreak-E6-CarrierExtensionDiagnostic-20260807-202645.log` |
| blocker identity | `Blocked=1, Void=1, Blocker=-1`；拒绝来自 ProtectedVoid，不是某根 Member 穿透 | 在桥下禁柱/禁实体区内补 carrier/cap/post 违反冻结合同；本路线到此停止 | `Saved/Logs/BeamC3V3-G1-ColumnBreak-E6-CarrierBlockerIdentity-20260807-202826.log` |

fresh 精确进程墙钟约 33～35 秒，其中 C3V3 阶段约 2.55～3.85 秒；LinuxArm64/VisionOS SDK 缺失是
UE 启动时的无关平台告警。自动化真实退出为 `255`，且日志含唯一 `Test Started`、`CandidateRejected` 和
`Test Completed: Fail`，因此这些都是有效负证据，不得按进程启动成功记为通过。

### 12.4 当前唯一阻塞

固定输入：`ColumnBreak / E6 / BaseSeed=710000 / CandidateSeed=710000 / Attempt=0`。

```text
Upper: pre-sort M1966, BuildingGroupShell / ThroughCourse / Q44 / Axis X
Geometry: X=-666..-18, Y=180, Length=648 cm
Reaction-weighted target: X=-180.3
Existing support intervals:
  -666..-630, -522..-486, -342..-306, -270..-234
Nearest generated candidate:
  X=-216, support=-234..-198       # 仍不足以包住合力与 0.5 cm margin
Required full-contact candidate:
  X=-180, support=-198..-162
Required lower carrier:
  Q42 M1906, -702..-198 -> -702..-162
Final blocker:
  ProtectedVoid intersection; no physical Member blocker
```

旧 Beam-C 最终编号 `M2311` 是 `RebuildPlannedSeatDAG` 按高度排序后的身份，不能与闭合阶段的临时
`M1966` 直接比较。稳定追踪键必须使用 Profile/Tier/Seed、Owner/Kind/Course/Axis/Role、端点和计划 Hash，
不能使用单独 MemberId。

这说明当前矛盾是：generic common ThroughCourse 的完整上部 DAG 合力要求一个落在桥下保留区投影内的
实体支点。ProtectedVoid 正确禁止该支点，所以不能再沿“加柱/延轨”方向继续。下一步必须先用只读 fixture
判定该 Q44 构件的语义身份：

- 若它占用了 SupportedSpan/shared band，本应由真正 SharedCourse/diaphragm 接管并从 generic common
  shell 中裁掉或别名替换；
- 若它是合法 perimeter/spoke，则必须在 ProtectedVoid 两侧重新分段或重新分配上部 load path，使合力
  留在合法 endpoint/core 支撑域；
- 只有先证明上述二者之一，才允许修改生成拓扑。不得放宽 ProtectedVoid、补地柱、改变截面、margin、
  Seed、密度或预算 cap。

### 12.5 禁止重复与恢复顺序

以下路线本检查点已证伪或不足，不再重复：

- 不恢复 pass-1 self-centre fallback；它会让计划假通过后在 Beam-C 重现 resultant-outside-hull；
- 不恢复“所有长梁端部都必须落座”；它会在 Roof/Core 占位处制造并不存在的 Q52 失败；
- 不再使用直接 load witness 的 min/max 代替质量加权合力；
- 不把 Q42 carrier 延入 ProtectedVoid，也不在 void 内补 cap、post 或 ground cell；
- 不用 `9.36 cm`/`9.86 cm` 缺口调整 resultant margin、Bearing 容差、格距或 Member 长度；
- 不运行 SeamRelease、G0/G1、5×6、BeamD15、视觉或 Chaos 来猜当前根因。

恢复时严格执行：

```text
R0 读取本节、设计第 16 节、M7-BC-045..048 和最新精确日志
  -> R1 建立 Q44 stable-signature / SupportedSpan / ProtectedVoid 原子只读 fixture
  -> R2 只实现“shared band 接管”或“void 两侧合法重分段”中的一个已证因果方案
  -> R3 fresh ColumnBreak.E6 单叶；同输入双次重放且计划/Beam-C 合力身份一致
  -> R4 fresh SeamRelease.E6
  -> R5 受影响 G0，再跑 G1 Boundary
  -> R6 一次 5×6 Matrix，再一次 BeamD15
  -> R7 用户编辑器视觉验收
```

前一状态未过不得晋级。R1 没有证明语义归属前不得继续改几何；相同 FailureCode、稳定几何签名与
ProtectedVoid blocker 再出现一次即停止，不进行第三轮同路线实验。

## 13. Stage 0/1 真实停点与视觉验收检查点（2026-08-07 21:40 +08:00）

### 13.1 路线切换

用户批准把实现步骤和生成步骤一一对应，并要求先只验收 WFC→芯体/shared。现行阶段定义已写入
[V3 设计第 17 节](M73BeamC3V3SkeletonFirstBuildingGenerationDesign.md#17-可停止可追溯的生产阶段合同2026-08-07)：

- Stage 0：仅 Shape Grammar/WFC 语义包络；
- Stage 1：仅接地分层芯体、shared 配对/slot 替换、长 shared rail 与 bridge diaphragm；
- Stage 2/3/4/5 依次为 coupling、共同外框、楼板/填充/屋顶、完整静态 DAG；当前未实现 Stage 2～4；
- 历史文档中把“完整建筑静态门”叫 Stage-1 的说法已降为历史日志标签，不再作为现行阶段名。

### 13.2 已落地代码

1. `AABTSM73BeamD1PreviewActor` 新增 `Generation Stop Stage` 和 `Stage 1 Diagnostic Layer` 下拉项；现有
   地图实例无需改二进制即可在重新编译后的 Details 面板看到原生属性；
2. 三层诊断分别显示真实 WFC Box/Prism/Pyramid/SupportedSpan 包络与 void、core footprint/预测体积和
   `FSharedCourseIntent` 配对、实际 CoreCourse/SharedCourse/BridgeDiaphragm；
3. Stage 0 在 WFC 后返回，V3 Plan 与 Beam-A Member 均为空；
4. Stage 1 在每个 component 发射完连续 XY core 后 early-continue，完全不进入旧 local shell/roof；全部
   endpoint core 完成后才冻结 shared intent、替换双方 `(course,lane)` slot、生成 diaphragm、重建 seat
   DAG，并在 `BuildCandidateCommonFrame` 之前返回；
5. Stage 1 门拒绝任何 Stage 2+ member、悬空 core、未双槽替换 shared、缺 diaphragm、非 Ground 可达、
   ProtectedVoid 穿入、正体积穿透或 planned/emitted 不一致。中间阶段不套用完整建筑 Brick 窗；
6. Stage 2～4 当前选择会返回稳定 `BeamC3StageNotImplemented`，禁止静默路由到旧完整生成器；Stage 1
   预览不允许生成 PIE runtime modules，继续保持 `Physical=NotEvaluated`。

### 13.3 本轮证据与停止点

- ForceUnity Development Editor 完整链接成功；中间增量成功构建约 12 秒和 10 秒，最终证据构建约 44 秒；
- fresh NullRHI `ABTS.M73DAG.BeamC3V3.Staged`：3/3 通过，日志
  `Saved/Logs/BeamC3V3-Staged-Stage0-Stage1-Final-20260807.log`；
- Stage 0 固定 `ColumnBreak.E1`：3 WFC volumes，0 V3/Beam-A members，未运行静态 DAG；
- Stage 1 固定 `SeamRelease.E6`：29 volumes、6 grounded cores、1 pairing intent、4 shared rails、
  775 Stage-1 members；stage-local Beam-C 静态 DAG accepted，`LoadDAGHash=966650309`；
- 未运行旧 G0/G1、5×6、BeamD15、Chaos、可见 PIE 或 Editor GUI；没有取得用户视觉批准；
- `Content/Maps/PlanarPhysicsTestMap.umap` 未修改、覆盖、暂存或重放；复核 SHA-256 仍为
  `49A0DEE23E64CF5CDF8A14F9E36719224AEC13907777F8512540DE8ECF30FD5E`。

本轮按约定停在 Stage 1 用户视觉验收点。下一步不是继续修第 12 节 Q44/common-frame closure，也不是实现
Stage 2；用户应先在 Editor 对同一 Actor 依次检查三层，确认 WFC→core 选址、core 体积、pair intent、
shared 长构件与 diaphragm 均符合预期。只有明确视觉批准后才设计 Stage 2 coupling course。

## 14. 相邻基座合并与多轨方芯体检查点（2026-08-07 22:55 +08:00）

用户批准把相邻 WFC component 的接地基座像屋顶语义一样合并，再从合并语义中选择更大的近方形
芯体。本轮只推进 Stage 1，未进入 Stage 2 或物理研究。

已完成：

- WFC 保持不可变；新增派生 `FCoreMergeRegionPlan`，记录来源原垂直 components、volume、精确接地
  片段、包络、轨数和独立 Hash；
- 只合并同 building、同 ground 且 XY 正面积重叠或整边相接的基座；角点相触、正宽间隙和
  SupportedSpan/ProtectedVoid 不作为相邻；
- core footprint 必须正面积覆盖 region 内全部原接地 components，并且每层每根实体由原 WFC
  `Body∪Crown` 并集完整覆盖；同等最小边长优先长宽差更小的候选；
- `RailCount=clamp(ceil(2*sqrt(N)),2,5)`；两来源使用 3 轨，上下正交 course 形成 9 个完整
  `36×36 cm` bearing patches，而非仅拉长两根空框外轨；
- Stage 1 下拉新增互斥的 `4 - Core Merge Regions`，只画精确来源基座片段；已有三层仍不串层；
- 公开摘要新增 region 数、合并来源数、最大轨数、每接口 bearing patch 数与 merge Hash。

首个定向运行准确暴露并修复了一个数组迁移错误：三轨站位已生成，但 `Core.LocalBounds.Max` 仍读取
旧 `[1]`，第三轨被包络截断并报 `BeamC3V3RebuiltSeatUnavailable`。修复为 `Last()` 后，同一固定
fixture 通过；没有修改 Seed、密度、预算、截面、720 cm、容差或物理参数。

最终证据：

- ForceUnity Development Editor 完整链接成功；
- fresh 定向 `CoreMergeRegionMultiRail` 1/1，通过日志
  `Saved/Logs/BeamC3V3-CoreMergeRegionMultiRail-20260807-Final.log`；
- fresh `ABTS.M73DAG.BeamC3V3.Staged` 5/5，通过日志
  `Saved/Logs/BeamC3V3-Staged-CoreMerge-20260807-Current.log`；
- `PlanarPhysicsTestMap.umap` 未覆盖，SHA-256 仍为
  `49A0DEE23E64CF5CDF8A14F9E36719224AEC13907777F8512540DE8ECF30FD5E`；
- 未运行 5×6、BeamD15、图形化 Editor、PIE 或 Chaos，所有稳定性结论仍为 `Physical=NotEvaluated`。

下一步停在用户 Editor 视觉验收：依次查看 WFC、Core Merge Regions、Core Placement/Pairing Intent、
Core + Shared Courses。当前一般化边界是：若一个 region 不能容纳覆盖全部来源且全高连续的多轨
footprint，会稳定 `ContinuousGroundedCoreUnavailable`；禁止用联合 AABB 填洞或参数搜索。视觉确认
后再决定是否需要实现“拆回多个 region/core”的显式回退，再进入 Stage 2。
