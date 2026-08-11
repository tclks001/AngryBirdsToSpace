# M7.3 Beam-C3 V3 支撑省份约束主芯体检查点（2026-08-11）

## 1. 恢复身份

- 工作树：`feature/m7-buildings`；本轮起点 HEAD：`2f2afc26d965bc2f46677b69d89c1a023ca5feb2`；
- 阶段：Stage 1 第一个视觉停点——支撑省份已经约束 PodiumMain 选择与实际绑定；
- 受保护资产：`Content/Maps/PlanarPhysicsTestMap.umap` 是用户在 Editor 中修改的二进制资产，本检查点未读取、
  覆盖、暂存或还原；
- 证据层：ForceUnity Development Editor 编译与 fresh NullRHI 静态自动化；没有启动图形化 Editor、可见 PIE 或
  Chaos，所有生产结果均为 `Physical=NotEvaluated`。

## 2. 本轮完成的合同

1. semantic demand DAG 与 support province 在 PodiumMain 枚举前完成；每个省份从真实 course-0 占用格选择一个
   确定性 anchor，负坐标由显式存在位保护；
2. PodiumMain 候选发布 province coverage mask；联合选择必须同时覆盖全部省份、全部 terminal region 和旧中心
   anchor，并继续满足全高 child、main-main lane、sibling 与 shared endpoint 合同；
3. 发射后每个省份必须绑定实际接地 core。层级路径只能绑定覆盖其 anchor 的真实 PodiumMain；计划与实际不闭合
   立即失败；
4. Summary/日志发布 `BoundSupportProvinceCount`、`DistinctProvinceGroundCoreCount`、
   `SupportProvinceMainBindingHash` 及逐省份绑定记录；5×6 测试逐项核验实际 core 身份和 anchor 覆盖；
5. Editor 增加互斥诊断层 `9 - Province / Main Assignment`：省份 course-0/候选顶面、选中 main 薄板及
   anchor→actual main 归属线不得与其他诊断层混画。

## 3. 搜索收敛方法

`DropTrigger.E6` 的首个实现曾在 10 秒门失败：为所有 retained main 预计算约 1897 万条 main-child 关系，并在
找到最少可行解后继续枚举 121246 个等数量组合。最终方案没有提高门限或调 Seed、Attempt、36 cm、720 cm、轨距、
候选桶容量：

- 单 main 的 full-height/shared 必要条件先剪枝；
- 精确 main-child 位集按需缓存；
- 确定性贪心只产生满足完整 child/sibling/shared 合同的可行上界；
- 对唯一的 `region+province+中心 anchor` 覆盖签名做有限 BFS，得到忽略几何冲突后的严格数量下界；
- 只有上下界相等才跳过其余等价几何枚举；否则继续精确搜索并保留 10 秒 fail-closed；
- 搜索状态用完整整数数组做相等比较，Hash 不承担生成正确性。

## 4. 静态证据

UE 5.8 ForceUnity Development Editor 全链接成功。代表叶：

| 证据 | 结果 | 算法时间 |
| --- | --- | ---: |
| `DropTrigger.E6/740000` | 8 省份→5 main；上下界均为 5；Static DAG Accepted | `2009.92 ms` |
| `ColumnBreak.E6/710000` | 4 省份→2 main；shared 保留；Static DAG Accepted | `2488.63 ms` |
| `TipOver.E6/710000` | 8 省份→3 main；Static DAG Accepted | `898.73 ms` |
| `TipOver.E6/730000` | 7 省份→3 main；Static DAG Accepted | `792.81 ms` |
| `TipOver.E6/750000` | 8 省份→3 main；Static DAG Accepted | `879.99 ms` |

fresh Stage 1-only 5×6 为 30/30：最小 `6.65 ms`、平均 `919.24 ms`、中位 `729.00 ms`、最大
`3536.32 ms`；最慢叶为 `SeamRelease.E6`，没有叶触发 `10000 ms` 门。省份数范围 `1..8`，每叶全部绑定。

关键日志：

- `Saved/Logs/BeamC3V3-ProvinceMain-CoverageBound-DropTriggerE6-20260811-183206.log`；
- `Saved/Logs/BeamC3V3-ProvinceMain-ColumnBreakE6-20260811103703-0.log`；
- `Saved/Logs/BeamC3V3-ProvinceMain-MergedRoof-20260811103703-1.log`；
- `Saved/Logs/BeamC3V3-ProvinceMain-PreviewContracts-20260811104035-0.log`；
- `Saved/Logs/BeamC3V3-ProvinceMain-TipOverSeeds-20260811104035-1.log`；
- `Saved/Logs/BeamC3V3-ProvinceMain-Stage1-5x6-20260811-184252.log`。

## 5. 人工视觉停点

在物理测试地图的 `ABTSM73BeamD1PreviewActor` 上选择 Stage 1，并将 `Stage 1 Diagnostic Layer` 设为
`9 - Province / Main Assignment`。验收：

1. 每个彩色省份恰有一条归属线；
2. 每条线落到包含该省份 anchor 的真实 main 薄板内；
3. 相邻省份可以汇入同一较大 main，但相隔较远的省份不能因共同 DAG 根全部汇入一侧；
4. 该层不混画 WFC 包络、Body Box、旧 core intent 或完整 member；
5. 上层彩色面只是 `ProposedPodiumTopCourse`，不得误读为已经生产化的局部裙房。

## 6. 明确未完成与下一步

本检查点尚未取得上述可见视觉批准，因此不宣称 Stage 1 第三步已冻结。批准后才进入逐省份局部裙房高度：逐个
验证 local seam 对 SupportedSpan、Crown、ProtectedVoid 的合法性，再把获准高度输入 PodiumMain；不得直接把
诊断 proposal 写入 WFC。之后另设视觉停点，再进入“较粗主干 + 最多一次收缩”的 TowerChild。Stage 2、
Beam-D1.5、Chaos 和物理参数均未开始。
