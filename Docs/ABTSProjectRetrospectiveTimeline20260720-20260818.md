# Angry Birds To Space 项目时间轴与决策复盘

> 时间范围：2026-07-20 至 2026-08-18（UTC+08:00）。
>
> 文档性质：交付后取证时间轴。本文先按时间顺序记录事实、关键决策与结果，
> 再总结可迁移经验；不改变已经交付的产品状态、稳定合同或发行候选。
>
> 证据来源：仓库全部可达 Git refs、项目设计/发行/排错文档，以及
> `G:\Codex\Official\sessions` 中可定位的历史 Codex 会话。Git 提交时间用于确定
> 工程事实，历史会话用于还原当时的目标、判断和取舍；自然语言回忆不覆盖已经留存的
> 构建、自动化、PIE 或 packaged 证据。

## 1. 总览

项目从 `451ad8f`（2026-07-20 18:05，`Initial commit`）开始，到
`12b556e`（2026-08-18 19:45，最终集成候选 HEAD）结束，Git 时间跨度约
`697.7` 小时。全 refs 共留下约 `600` 个唯一提交，其中约 `93` 个 merge 提交。

最重要的三个阶段性事实是：

1. 四工作树不是临近交付才启用。稳定接口提交 `824bfa4` 于 7 月 28 日 22:12
   建立，约位于总 Git 时间的 `28.1%`；之后约 `71.9%` 的开发时间都处于并行状态。
2. 项目一直在做功能候选集成，但直到 8 月 16 日下午，集成对象才真正转为
   release scope、Cook、Development/Shipping package 和 packaged player path。
3. 最后四天（8 月 15 日至 18 日）产生约 `41%` 的全部提交，而且仍在改变地图、
   建筑、玩法链、终局轨迹、镜头和共享冻结身份，并非单纯打包扫尾。

### 1.1 提交密度

| 日期 | 全 refs 唯一提交数 | 主要含义 |
| --- | ---: | --- |
| 2026-07-29 | 32 | 四工作树启用后的首个高并发日 |
| 2026-08-05 | 36 | 三渲二共享表现与 M7/M11 并行推进 |
| 2026-08-15 | 77 | Fixed-Six、Building Freeze、Map Freeze 集中建立 |
| 2026-08-16 | 92 | 重冻、Chaos、发行收敛和 packaged 修复同时发生 |
| 2026-08-17 | 44 | Cook/Shipping 专属问题与多轮玩家包验收 |
| 2026-08-18 | 33 | 交付日仍在替换候选与修改核心生产链 |

### 1.2 三条高迭代线

- M3 路径约触达 `87` 个非 merge 提交，峰值在 8 月 16 日。
- M7 路径约触达 `160` 个非 merge 提交，其中约 `62.5%` 发生在 8 月 11 日以后。
- M11 路径约触达 `107` 个非 merge 提交；7 月 28 日至 8 月 3 日约有
  `49` 个，其中约 `28` 个直接与候选、Rank、搜索或认证有关。

这些数字衡量 Git churn，不等同于人时；跨模块提交和 cherry-pick 可能重复计入。

## 2. 时间轴

### 2.1 7 月 20 日：立项与玩法/PCG 总体方向

**工程事实**

- `451ad8f Initial commit` 建立项目。
- 初始设计目标是一个月内完成 PCG 与物理驱动的第三人称小行星 minigame，
  第一周形成可玩的纵向闭环。

**历史会话中的关键决策**

- 立项当天没有把地图理解为纯视觉随机地形，而是很快转向：
  `Gameplay Task Graph → CellTopo Spatial Graph → 连续球面表现`。
- 用户明确要求道路、河流、桥梁、资源、建筑占用和可达性都以 `CellTopo`
  为逻辑源，连续球面只负责渲染、碰撞和表现。
- 在阅读《饥荒》地图生成思路后，决定“先决定玩家经历什么，再把任务图嵌入地图”，
  而不是先随机生成地形后再塞玩法。

**评价**

这是项目最正确的早期决策之一。它让 M3 后来能够成为玩法路线生产者，而不仅是
地形装饰器，也为多工作树时的稳定只读合同提供了天然边界。

### 2.2 7 月 21 日至 24 日：单工作树高速建立纵向骨架

**关键提交**

- 7 月 21 日：`b40bc69 M1框架`、`05cef47 M2.5`、`befecda M3`
- 7 月 22 日：SDF 地表、法线、树木、径向移动、M4 跟随、相机、M5.2
- 7 月 23 日：M6 切换问题修复、M7 建筑材料、M7.1 平面物理测试场
- 7 月 24 日：物理效果、弹弓规范化与视觉优化

**关键决策**

- 采用“先打通一条纵向链，再逐层替换”的策略，而不是先建立完备基础设施。
- 修改 C++ 后要编译；不能把“代码看起来合理”当作 UE 可用证据。
- 建筑物理先在平面测试台隔离验证，再接球面世界。

**结果**

四天内形成了球面地表、移动、相机、鸟群、资源/交互、弹弓与建筑试验的基础。
这是后续并行开发能够成立的前提。

### 2.3 7 月 25 日至 28 日白天：M7/M11 技术基线建立

**M7 路线转向**

- `74b4ccc M7.3-B弱点和难度生成`
- `39cbfc9 M7.3-B建筑弱点。效果不好，下面将切换为M7.3-DAG`
- `1e39fff M7.3-DAG1 DAG生成`
- `026d30b M7.3-DAG2建筑生成`
- `ad19ee5 M7.3-DAG2.2 ... 没解决根本问题`
- `219ce60 M7.3-DAG2.3 楼板支撑算法`

M7 很早就表现出研究型项目特征：发现旧弱点方案效果不好后，迅速转向递归支撑 DAG，
并持续提高生成、支撑、可破坏和视觉复杂度。

**M11 基线**

- `3f8a794 M11前置工作`
- `36f9e63 M11-A`
- `7c7a781 M11-B 可行稳定解`

在多工作树建立前，M11 已有可行稳定解。这一点很重要：并行化不是为了让完全未知的
模块同时摸索，而是建立在三条工作线已经能被识别和分割的基础上。

### 2.4 7 月 28 日晚：建立一个集成工作树与三个功能工作树

**决策点**

- `824bfa4`（22:12）：建立 M3/M7/M11 稳定接口、自动化和多工作树规范。
- `5edda9b`（23:30）：补充编译、运行、所有权和冲突处理规则。
- 功能分支固定为：
  - `feature/m3-pcg-map`
  - `feature/m7-buildings`
  - `feature/m11-finale`

**历史会话动机**

M11-B 已完成，M3 地图改进、M7 建筑和 M11 终局都需要继续推进。用户主动询问同一
分支多会话是否会产生写入冲突，并决定：

- 原始仓库作为唯一集成工作树；
- 只创建三个功能工作树，不再创建第二个集成树；
- 先冻结稳定消费接口，再让三条线并行；
- 功能线不直接互相合并。

**评价**

启用时点约为项目总时间的 `28.1%`。它可以略早，但已经接近合理下限：

- 更早于 M3/M7/M11 边界成形时拆分，会得到三个共同修改 GameMode、地图、Config
  和共享类型的工作树，冲突可能比收益更大。
- 真正可以提前的是“接口草案和资产所有权”，而不是在第 1 天机械创建四个工作树。

### 2.5 7 月 29 日至 31 日：并行吞吐量爆发

**M3**

- R0 兼容基线、R1 观察门、R2 路线候选池、R3 Encounter 空间规划、R3.1 弹弓槽
- 7 月 30 日接入 M5.1/M6 消费端
- 7 月 31 日进入 R4 gameplay witness 与 R5 biome presentation

**M7**

- DAG3-A/B/C、DAG4、DAG5-A/B
- 随后从 DAG5-B 再转向 Beam-A 结构 IR

**M11**

- `f3af167` 可移植求解核心与 v2.1 候选搜索
- `730bb95` conditional particle beam search
- `ea5fafd` Rank 3 v2.2 预认证
- Rank 3 拓扑、目标偏移、末端修复与重映射连续推进

**集成**

大量 `integration/candidate-*` 分支证明项目并非“直到最后才做任何集成”。这一阶段
已经频繁把 M3、M7、M11 的精确提交拉入候选，执行编译、自动化或 PIE 验收。

**但缺少的是什么**

这些候选主要验证源码、模块合同、Preview、NullRHI 和 Editor/PIE；它们并不等价于：

- Cook 资产闭包；
- packaged Development；
- packaged Shipping；
- Shipping 默认值和宏分支；
- 从 exe 启动到首个可玩闭环的玩家路径。

项目后期真正的风险不是“没有合并”，而是“集成的证据层没有尽早到达最终制品”。

### 2.6 8 月 1 日至 4 日：候选筛选与跨模块表现继续扩张

**M11 候选高峰**

8 月 1 日在同一天连续出现 Candidate 353、Rank 7、Rank 8、Rank 9、Rank 10，
以及多次 early-stop、角向修复和终端映射搜索。8 月 2 日又增加放大尺度候选。

这段工作证明了求解器、搜索器、认证器和 UE 展示能够同源运行，但从最终产品价值看，
它的边际收益迅速下降：

- v1 已有可运行基线；
- 新候选大多保持 `Candidate / NOT CERTIFIED`；
- 最终 Shipping 仍需要可靠的终局播放和表现，而不是更多未认证 Rank；
- 后续 Rank 12 仍然 early-stop，没有替换生产 v1。

**M7 继续换代**

M7 从 Beam-A、Beam-B 推进到 Beam-C、Beam-D 和视觉难度梯度。每次升级都带来新的
设计、测试和数据身份，但生产主线仍未冻结。

**跨模块接缝**

- `a87cfda` 接通 M3R-5.2 与 M11 Preview 终局帧。
- 8 月 4 日合并三条线最新进展，建立三份功能排错账本并完善规范。

**评价**

并行能力在这一阶段被充分发挥；同时也开始出现“并发让所有研究路线都能持续推进，
却没有一个统一的产品价值停止条件”的副作用。

### 2.7 8 月 5 日至 10 日：三渲二与镜头成为新的共享主线

**关键工作**

- T0/T1/T2/T3 风格化渲染逐步进入共享材质、M3、鸟/弹弓和 M11。
- M11 加入多行星终局镜头导演。
- 球面天空、程序化星场、云和整体视觉统一被加入项目。

**做得好的地方**

- 低模、纯色和球面空间确实适合统一风格化，视觉收益明显。
- 使用只读语义适配器，而不是让共享渲染直接读取各模块私有状态。
- 多工作树允许视觉适配与核心功能并行。

**风险**

风格化是一个横切 M3/M7/M11/Integration 的新需求。它进一步增加了共享材质、
Cook 资产和表现合同的复杂度，而此时还没有稳定的 packaged release loop。

### 2.8 8 月 11 日至 14 日：M7 第二轮大重构，M11 再启认证

**M7**

- 8 月 11 日重新启动 skeleton-first Stage 1。
- 8 月 12 日冻结 Stage 1。
- 8 月 13 日连续推进 Stage 2、3、4。
- 8 月 14 日继续补 Stage 4 外墙、楼板和风格填充。

**M11**

- 完成多助推镜头、UFO terminal、四鸟编队等最终表现。
- 8 月 12 日启动 Rank 12 certification search。
- `86a3a14`（8 月 13 日）记录 Rank 12 refinement early stop。
- `e688437`（8 月 14 日）才建立 `PresentationAccepted` 稳定合同，并明确它不等于
  `StrictCertified`。

**共享系统**

UI、音频、物品表现、河道、桥梁、设置等仍在持续加入。

**评价**

距交付四天时，项目仍然同时进行：

- 建筑生成算法重构；
- 终局候选认证；
- 终局镜头收敛；
- 全局 UI/音频/表现扩张。

此时最缺的不是更多并发槽位，而是 release scope、止损点和固定 package 基线。

### 2.9 8 月 15 日：从功能开发突然切换到生产冻结

8 月 15 日产生 77 个提交。当天既有 UI、音频和 M11 工作，也集中建立了最终世界的
Fixed-Six 链：

1. 发布 JuryDemo Fixed-Six 合同；
2. 发布 Fixed-Six V2/V3 DTO；
3. 建立 M3 fixed-six adapter；
4. 密封六栋静态联合门；
5. `8a4892d` 冻结 M7 Building Freeze V3；
6. `b866c1e` 接受 Building Freeze V3；
7. `a6a3136` 发布 M3 Jury MapFreeze V3 handoff。

**关键问题**

真正面向最终演示世界的 Building Freeze、Map Freeze、Placement、Registration、
Crystal 生产语义和数值身份，都在交付前三天才集中建立。于是：

- 上游仍可能发现结构问题；
- 下游已开始依据“冻结”签地图和门禁；
- 任何小改动都会以最高成本传播。

### 2.10 8 月 16 日凌晨：E1 两根座梁触发全链路重冻

这是本项目最能说明架构问题的事件。

#### 原始状态

- `8a4892d`：M7 冻结 Building Freeze V3。
- `b866c1e`：Integration 接受旧 Catalog。
- M3 基于该身份开始 Map Freeze。

#### 局部变化

`b1e6fc6 M7: certify E1 crystal seat external loads` 为 E1 增加两根真实 Stone
Crystal 座梁：

- E1 Brick 从 52 增至 54；
- 全六栋静态模块从 5746 增至 5748；
- Catalog、Descriptor、Static/Production/Registration Hash 改变；
- 水平占地没有实质扩大，主要变化是内部结构和少量高度。

#### 自动化拒绝

旧 Map Freeze 与新 M7 身份不一致，正确触发：

```text
FrozenCatalogMismatch
MapFreezeV3 Ready=0
JuryFixedSix Exported=0
Authority=FailClosed
```

随后又发现 M3 测试通过 `BoundsSize.Y > BoundsSize.X` 推断建筑朝向；E1 合法变为
近方形后，测试也需修正。最终由 Integration 原子更新 Catalog、E1 Placement、
Layout、Registration 和模块数，M7 再合并新基线继续 Chaos。

#### 它暴露的不是“自动化太严格”

`FrozenCatalogMismatch` 阻止了旧地图身份与新建筑几何被拼成假生产结果，行为正确。
问题在于：

1. **构建期依赖成环。** 运行时名义方向是 `M3 → M7`，冻结方向却是
   `M7 几何 → Integration → M3 Placement → Integration → M7 Chaos`。
2. **公共合同吸收了过多内部实现。** 内部梁、精确砖数、完整 Catalog 与
   Registration 身份都进入了地图和启动门。
3. **消费者测试泄漏生产者知识。** M3 用 `Y > X` 推断 M7 朝向，是偶然形态，
   不是稳定语义。
4. **单一权威被复制为多份常量。** 同一身份散落在 M7 Catalog、M3 Map Freeze、
   Integration DTO、M6 Gate、自动化和文档。
5. **冻结时点与变更控制不一致。** 下游把它当已冻结，上游仍在补必要结构。

因此，本次架构可以概括为：

> 运行时代码模块化，但生产身份、冻结常量和验收门组成了一个分布式单体。

### 2.11 8 月 16 日下午至晚上：发行主线才正式出现

**关键节点**

- `b4ccc8c Use safe packaged bird locomotion`
- `926261c Fix lunar flight prediction and framing`，首次建立可审计的
  `ABTSReleaseScope20260818.md`
- `6b1ed99` RC9 collision/camera/startup gates
- `ca4f47f` opening cinematic handoff
- `7b99d15` finale cinematic physical hit binding
- `5f90774` Stabilize RC9 release presentation and Chaos

Release Scope 明确把大量历史研究延期：

- 不发布正式建筑弱点机制；
- M7.3-B/B2/DAG3/DAG4 只保留历史证据；
- 不要求 Crystal 精确首击；
- 不发布弱点专属材质、标记和引导；
- 最终必须验证 packaged Development/Shipping。

**评价**

这份范围冻结非常有效，但来得过晚。它如果在 8 月 8 日至 10 日形成，就可以更早停止
M11 Rank 搜索和 M7 的部分结构泛化，把时间投入生产玩法、包内资源、引导和 Shipping。

### 2.12 8 月 17 日：Shipping 第一次成为真实反馈系统

**Cook/资产问题**

- `a515743 fix(cook): include shared toon materials`
- `4bbdb55 fix(cook): include finale planets and ufo assets`
- HISM 材质缺 cooked usage permutation

**Shipping 默认值和调试泄漏**

- 隐藏卫星 debug overlay
- 硬关闭 debug inventory seeding
- 恢复 Shipping 中被冻结的 V3 presentation 与普通槽快照

**Shipping 启动死锁**

MoviePlayer 结束后，前台加载层过早暂停世界；而发布 `WorldReady` 的 M6 authority
还未获得 tick，造成 Shipping-only bootstrap deadlock。修复要求：

- `PostLoadMap` 不等于 `WorldReady`；
- authority 未发现前保持世界 tick；
- 只由运行时权威发布 100% 和进入前台。

**第一份明确记录的成功 Shipping 包**

`75d9e5b` 于 8 月 17 日 13:03 记录候选 `a2244b4`：

- Shipping BuildCookRun `ExitCode=0`
- 有归档制品和 packaged startup 证据

距 8 月 18 日晚交付只剩约一天半。

**M7 发行实验被拒绝**

`e41b6b7` 的 per-brick queue 方案在可见 Standalone 中出现约三秒 hitch、渐进坍塌、
浮空砖和爆炸后无支撑砖站立，被 `4c942e1` 明确拒绝，不得集成。

**教训**

Shipping 一天内暴露了 Editor/PIE 无法证明的多类问题：

- Cooker 资产闭包；
- 材质 usage；
- `UE_BUILD_SHIPPING` 分支；
- 序列化默认值；
- 启动时序与 tick；
- debug 功能硬关闭；
- 真实玩家资源/玩法链。

### 2.13 8 月 18 日：交付日仍在替换候选

**凌晨**

- `3a7e09d` 修复 M11 长时有效终端圆弧。
- `9f24bcd` 形成新的 Development/Shipping 候选。

**中午**

玩家发现 `9f24bcd` 缺少普通弹弓资源与正确开局引导，Development 放桩也不能连弦。
`a2534e2`、`7d0793a` 修复，`8b68b51` 明确废弃旧候选并打新双包。

**下午**

- `da5cfd3 M3: relocate E1 to operator landing cluster`
- `3a385f7 integration: refreeze relocated E1 site`
- M7 连续修 E1 Crystal 移动恢复、支持丢失激活、首次 Chaos hit 奖励和掉落砖易碎
- M10/M51 消费端同步修复

**晚上**

- 稳定终局镜头和 traversal pace
- 修复音乐在开场暂停时中断
- 修复 ForceUnity 局部符号冲突
- `12b556e` 19:45 修正终局 director anchor 与四鸟编队构图

**结果**

项目在当晚完成交付，但最终候选比 `master` 领先 59 个提交，发行线停留在
`integration/candidate-rc93-exact-bricks`。对于一次性交付，这可以接受；对于长期产品，
它表示主干、候选与制品身份尚未完成正式收口。

## 3. 做得好的地方

### 3.1 四工作树显著提高了吞吐量

这是最明确的成功。

- M3、M7、M11 在 7 月 29 日后能真正并行推进。
- 原始仓库保持 Integration 职责，避免另一个“集成的集成”。
- 功能分支只消费 master，不直接互相合并。
- 精确 SHA、候选分支和独立日志使失败可以取证。
- 二进制资产单写入者避免了不可恢复的 `.uasset/.umap` 合并。

没有四工作树，以当前范围不可能在 30 天内完成。

### 3.2 先建立稳定合同再并行，基本方向正确

M3 通过只读快照向 M7/M11 输出，而不是让消费者读取其原始数组和配置对象。
M9 与 M11 终局边界被明确分离。这个方向降低了运行时耦合，也使三个工作树能独立编译。

### 3.3 fail closed 防止了“看似能跑的假集成”

典型包括：

- `FrozenCatalogMismatch`
- Preview/Test 不得冒充 Production
- `PresentationAccepted` 不得冒充 `StrictCertified`
- 自动化零匹配不算通过
- `PostLoadMap` 不得冒充 `WorldReady`

这些规则在最后几天虽然昂贵，却避免了把旧地图、新建筑、假轨迹或预览结果拼成
无法复现的 Shipping。

### 3.4 证据分层意识很强

项目最终明确区分：

- 源码/静态检查
- Development Editor 编译
- ForceUnity
- 纯数据自动化
- NullRHI runtime
- 实时 Chaos
- 可见 PIE/Standalone
- packaged Development
- packaged Shipping
- 玩家可见验收

这是 UE 项目非常宝贵的积累。后期能快速判断“已有何种证据、还缺哪一层”，与这套
分层直接相关。

### 3.5 排错账本与交接文档非常完整

M3/M7/M11 原始账本、总排错文档、Release Scope 和账号交接 checkpoint，使项目在：

- 多会话；
- 多账号；
- 高提交密度；
- 候选频繁废弃；
- 最后 22 小时额度危机

的情况下仍能继续推进，没有完全依赖某一个聊天窗口的短期记忆。

### 3.6 用户在关键时刻做出了务实的降级

例如：

- 接受低积木 full-Chaos 作为发行保底；
- 正式延期建筑弱点；
- 不要求精确 Crystal 首击；
- 保留 v1 M11 生产基线，不让未认证 Rank 覆盖；
- 最后以可游玩、可进入、可完成主路径优先。

这些决策最终保住了交付。

## 4. 做得不足的地方

### 4.1 没有尽早建立“真实制品”反馈环

项目不是没有集成，而是长期停留在 Editor/Preview/NullRHI/PIE 证据。
第一份明确可审计的成功 Shipping BuildCookRun 直到 8 月 17 日才出现。

直接后果包括：

- 缺 cooked 材质和终局资产；
- HISM 材质 usage 不完整；
- Shipping-only 宏分支恢复旧世界；
- debug 功能泄漏；
- 启动 pause 死锁；
- 包内资源/弹弓链缺失；
- exe 玩家路径与 Editor 预期不一致。

**改进原则**

首个可玩闭环形成后就应打 Development package；之后按风险和节奏定期打 Shipping，
而不是等“功能基本完成”才开始。

### 4.2 产品范围冻结过晚

Release Scope 到 8 月 16 日才明确写出 Required/Deferred。此前 M7 弱点、DAG、
Beam、Stage，M11 Rank 搜索和视觉系统都能继续扩张，因为没有统一的产品退出条件。

**改进原则**

在项目 30% 左右形成第一版 release scope，在 60% 左右进入 feature freeze：

- 必须进入最终包的主路径；
- 明确延期的研究功能；
- 允许的 fallback；
- 不能再改变的世界/资产/玩法身份。

### 4.3 M11 候选筛选缺少价值门和时间盒

M11 搜索与认证技术上很出色，但投入与最终 Shipping 收益不匹配：

- v1 已有可运行生产基线；
- Rank 3 至 10 产生大量未认证候选；
- Rank 12 在交付前一周再次启动并 early-stop；
- 最终关键问题仍是轨迹连续播放、镜头、UI、包内资产和玩家引导。

**根因**

搜索停止条件主要由算法指标定义，没有同时绑定：

- 玩家体验是否明显提升；
- 是否能在剩余时间完成认证与生产绑定；
- 是否会进入最终 Shipping；
- 相比修包、引导、主路径 bug 的机会成本。

**改进原则**

研究候选使用“双门”：

1. 技术门：合法、可复现、达到指标；
2. 产品门：在限定试玩中显著优于基线，并能在剩余预算内进入 package。

任一不满足就回退已知基线。

### 4.4 M7 的多轮算法换代缺少“演示所需最小建筑”约束

M7 从弱点到 DAG2.3、DAG3/4/5、Beam、C3、Stage 1–5、Fixed-Six、per-brick Chaos，
积累了大量通用生成与认证能力，但最终发行明确延期了不少历史研究。

**改进原则**

短期 minigame 应先完成：

- 少量固定 Profile；
- 可稳定生成；
- 可见破坏；
- 可控制 body count；
- 一条玩家可理解的破坏反馈。

泛化搜索、复杂弱点、全结构语义和大规模候选库应放在主路径 package 稳定之后，
并设置强时间盒。

### 4.5 冻结合同过度传播内部实现身份

E1 两根座梁事件表明：

- 内部砖数、完整 Catalog 和 Registration Hash 传播到 M3/M6/Integration；
- 地图 Placement Hash 吸收了建筑内部实现；
- 消费者测试通过 Bounds 偶然形态推断方向；
- 同一事实在多处源码/文档中复制。

**改进原则**

把冻结拆成三层：

1. `Placement ABI`：ID、Pivot、Forward/Up、Oriented Bounds、Pad/Effect Envelope、
   Surface Requirement；
2. `Internal Implementation`：砖、梁、支撑 DAG、材料分布、Chaos 组织；
3. `Release Seal`：精确 Geometry/Catalog/Body Count/Result Hash 和包身份。

只要内部变化仍在已发布 Envelope 内，M3 不应重冻。精确 Hash 用于发行追踪，
不应全部成为每个消费者的编译期拒绝常量。

### 4.6 过度工程化与项目寿命不匹配

稳定合同、复杂自动化、全域认证、详细文档和多层 Hash 对长期项目很有价值，但本项目是
一个 30 天、交付后基本不维护的 minigame。部分流程的成本超过了它们避免的风险。

**但不能简单归结为“测试太多”**

真正需要精简的是：

- 合同内容；
- 文档粒度；
- 测试矩阵；
- 认证输入域；
- 冻结传播范围；
- 候选研究预算。

不应删除的是：

- 编译；
- 最小自动化；
- package smoke；
- Shipping 主路径；
- 二进制资产所有权；
- 精确制品身份；
- fail closed 的核心安全边界。

### 4.7 AI 放权提高了短期速度，但形成了严重知识债务

用户对项目理解主要来自 AI 的自然语言交接，没有系统阅读数十万行实现。这对一次性交付
非常高效，但对长期项目会造成：

- 人类无法独立定位启动链、资产绑定、状态机和跨模块根因；
- 审查主要验证“AI 是否说得完整”，而不是代码是否可维护；
- 架构事实存在于聊天和账本，缺少人类能够复述的简化模型；
- bus factor 实际上变成“仍能访问同一 AI 上下文和工具链”。

**改进原则**

按项目寿命决定 AI 自治级别：

- 一次性交付：AI 可高自治，但人类至少掌握启动、主循环、关键 authority、
  package、存档/配置和失败回退。
- 长期产品：架构、共享合同、持久化、网络、发布和关键玩法必须有人类 owner；
  需要代码审查、架构图、故障演练和无 AI 修复演习。

### 4.8 最后四天仍允许核心身份改变

E1 位置在交付日下午仍被移动，之后再次重冻。终局圆弧、镜头、音频、玩法资源和
Crystal 生命周期也继续变化。

**改进原则**

设置不可逾越的冻结层级：

- `Scope Frozen`
- `Placement Frozen`
- `Release Sealed`

进入 `Release Sealed` 后只允许 P0 修复；若必须改变核心身份，应明确重新打开 freeze，
废弃旧 RC，并重新执行受影响的 package/player-path 门，而不是边发包边继续设计。

## 5. 四工作树能否更早开启

**结论：可以小幅提前，但不应从第 1 天就机械拆成四棵树。**

合理的最早时点是满足以下条件时：

1. 已有一个能启动的共同基线；
2. M3/M7/M11 的职责与数据方向可画成图；
3. 共享地图、Config、GameMode、资产和合同有唯一 owner；
4. 至少有最小独立验收入口；
5. 功能工作树不需要频繁共同修改同一 `.umap/.uasset`。

本项目大约在 7 月 25–27 日已经基本满足这些条件，因此可以比 7 月 28 日提前
`1–3` 天，先建立：

- 所有权表；
- 二进制资产红区；
- 极小的只读 Placement/Finale 接口；
- 每条线的 smoke map。

也可以先启用 Integration + M3 + M7，等 M11.0/M11-A 边界成形后再加入 M11，
不必要求所有功能线同一天激活。不建议在 7 月 20–22 日就拆分，因为当时模块边界仍在
快速形成，过早拆分会把共享热点复制到多个分支。

更重要的是：即使提前三天，也不能解决 Shipping 晚和 scope freeze 晚的问题。

## 6. 下一次类似项目的建议流程

### 6.1 按项目模式选择工程强度

| 模式 | 典型周期 | 核心目标 | 推荐强度 |
| --- | --- | --- | --- |
| Prototype | 数天至数周 | 一条可玩、可打包的固定闭环 | 少合同、少文档、强 package smoke |
| Vertical Slice | 数周至数月 | 可代表最终品质的主路径 | 模块所有权、稳定边界、定期 Shipping |
| Product | 长期 | 可扩展、可迁移、可多人维护 | 正式版本化合同、CI、架构与人工 owner |

本项目应按 `Prototype / short vertical slice` 管理，而不是部分采用 Product 级全域认证。

### 6.2 建议里程碑

#### 0%–10%：目标与最小闭环

- 写一页 `Required / Deferred / Fallback`。
- 选固定 Seed、地图和白名单输入。
- 确定玩家从启动到成功/失败的唯一主路径。

#### 10%–20%：第一个 Editor 垂直切片

- 打通入口、核心交互和胜利条件。
- 建立关键 authority 和日志。
- 如果模块边界已清晰，开始建立功能工作树。

#### 20%–30%：第一个 packaged Development

- 不等待美术和所有功能完成。
- 从真实 exe 验证地图、资产、输入、存档/配置、启动与退出。

#### 30%–60%：并行开发与固定节奏集成

- 每 `2–3` 天或高风险改动后产出一个 Development package。
- 至少每周一个 Shipping package；DDL 少于两周时每 `2–3` 天一个 Shipping RC。
- 每个 package 绑定 commit、配置、日志和已知限制。

#### 60%：Scope Freeze

- 研究候选必须通过产品价值门，否则延期。
- 只保留确定会进入 Shipping 的功能。
- 明确 fallback package。

#### 70%–80%：Placement/Content Freeze

- 地图位置、关键资产路径、玩法资源链和启动流程冻结。
- 内部实现可在 Envelope 内修复，不再传播全局重签。

#### 80%–100%：Release Hardening

- 只做 P0/P1、Cook/Shipping、性能、引导、玩家路径和回归。
- 不再开启新算法路线，不再扩展全域认证。

### 6.3 自动化应按风险分层

短期项目的最小集合：

1. Development Editor 全链接；
2. 核心纯数据/合同测试；
3. 关键 NullRHI runtime；
4. 一次真实 RHI/物理主路径；
5. packaged Development smoke；
6. packaged Shipping 主路径；
7. 玩家可见验收。

测试只验证公共语义，不复制生产者内部结构。精确内容 Hash 放在 Release Manifest 中。

### 6.4 研究路线要有退出条件

每条研究线在开始前回答：

- 基线是什么？
- 可量化提升是什么？
- 最多投入多少天/候选/计算预算？
- 何时判定“不会进入本次 Shipping”？
- fallback 是什么？

到点未满足产品门，立即回退基线，不把“已经投入很多”当作继续投入的理由。

### 6.5 建立人类最小掌控面

无论是否长期维护，人类 owner 至少应能画出并解释：

- 启动到 `WorldReady`；
- 玩家主循环与胜利条件；
- 关键 authority；
- 资产/配置如何进入 Cook；
- package 如何生成和回退；
- 三个最常见 P0 的第一诊断点。

长期项目还应定期进行一次“不依赖 AI 的故障定位/小修复演练”。

## 7. 最终结论

本项目不是“流程失败后侥幸交付”，而是：

- 用四工作树和 AI 高自治获得了远超单人常规开发的吞吐量；
- 用合同、证据分层和 fail closed 保住了复杂系统的一致性；
- 但没有尽早把并行产能约束到一个稳定的 Shipping 产品上；
- 研究路线、全域认证、冻结身份和自动化粒度部分超过了短期 minigame 的需要；
- 因此最后四天不得不同时完成范围冻结、生产冻结、重冻、Cook、Shipping、
  玩家验收和核心功能修复。

下一次最应保留的是：

- 清晰模块所有权；
- 一个集成职责加多个功能职责；
- 二进制资产单写入者；
- 稳定的最小合同；
- 精确 SHA 与制品身份；
- 证据层不互相冒充；
- 排错经验持续沉淀。

下一次最应改变的是：

- 在第一个可玩闭环后立即打包；
- 在项目中期前开始周期性 Shipping；
- 早写 Release Scope 和 fallback；
- 给 M7/M11 类研究设置产品价值门与止损点；
- 将 Placement ABI、内部实现和 Release Seal 分层；
- 根据项目寿命决定 AI 自治、文档、自动化和架构强度。

## 8. 主要证据索引

### Git 决策点

- `451ad8f`：项目初始提交
- `39cbfc9`：M7 从弱点路线转向 DAG
- `7c7a781`：M11-B 可行稳定解
- `824bfa4`：多工作树稳定接口与规范
- `5edda9b`：多工作树编译/运行规则
- `f3af167`：M11 v2.1 候选搜索核心
- `ea5fafd`：M11 Rank 3 v2.2 预认证
- `86a3a14`：Rank 12 refinement early stop
- `e688437`：M11 PresentationAccepted 合同
- `8a4892d`：Building Freeze V3
- `b1e6fc6`：E1 Crystal 座梁外载认证
- `66de35f`：E1 座梁后的 V3 重冻
- `926261c`：8 月 18 日 Release Scope
- `75d9e5b`：首份明确成功 Shipping package 证据
- `4c942e1`：拒绝 M7 queued-brick 发行实验
- `8b68b51`：交付日替换失效 Shipping 候选
- `da5cfd3` / `3a385f7`：交付日下午移动并重冻 E1
- `12b556e`：最终候选 HEAD

### 项目文档

- `Docs/AngryBirdsToSpaceGameDesign.md`
- `Docs/ABTSProjectWorkflow.md`
- `Docs/ABTSMultiWorktreeDevelopmentGuide.md`
- `Docs/ABTSReleaseScope20260818.md`
- `Docs/ABTSAccountHandoffCheckpoint20260817.md`
- `Docs/BuildingGenerationAndPlacementFreezeV3Plan.md`
- `Docs/BuildingGenerationAndPlacementFreezeV3IntegrationReadiness.md`
- `Docs/M73BuildingFreezeV3Handoff.md`
- `Docs/M73ChaosMapFreezeV3ResearchCheckpoint_20260816.md`
- `Docs/M11BFinaleLayoutCertificationDesign.md`
- `Docs/M3WorktreeTroubleshootingLog.md`
- `Docs/M7WorktreeTroubleshooting.md`
- `Docs/M11WorktreeTroubleshooting.md`
- `Docs/DevelopmentTroubleshooting.md`

### 历史会话

- 2026-07-20 的项目设计会话：建立玩法 Task Graph、CellTopo 与 PCG 总方向。
- 2026-07-28 的主项目会话：在 M11-B 后决定建立 M3/M7/M11 三个功能工作树，
  冻结稳定接口和所有权规范。
- 2026-08-15 至 18 的 Integration/M3/M7/M11 会话：记录 Fixed-Six 冻结、
  E1 座梁重冻、发行候选、Shipping P0、玩家验收和账号交接。
