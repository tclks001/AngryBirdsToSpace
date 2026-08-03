# M7.3-Beam-D1：真实 Brick 与材料角色编译

> 上游：[Beam-D0 Profile Catalog](M73BeamD0GameplayProfileCatalogDesign.md) · [Beam-C Load DAG](M73BeamCLoadDAGAndStaticProxyDesign.md)
>
> 总路线：[M7 建筑开发路线](M7BuildingDevelopmentRoadmap.md)
>
> 下游：Beam-D2 弱点、Chaos 与 `Profile × Tier` 认证；Beam-E Catalog 冻结和 M3 生产接入
>
> 状态：首版 C++、独立编辑器预览、真实 Module 测试入口与自动化已完成；待用户编辑器读形验收

## 1. 阶段目标

Beam-D1 把 Beam-B 已闭合、Beam-C 已通过静态代理检查的每一个结构 Member 编译为
现有 M7 材料系统可直接生成的 `FABTSM7BrickSpec + LocalTransform`，并为真实 Brick
保留材料、结构、弱点候选和装置候选语义。

本阶段解决的是“结构数据是否已经成为真实 M7 Brick”的问题，不证明某个弱点一定按预期
倒塌。弱点伤害改写、Failure Frontier、Chaos 稳定与倒塌模式认证属于 Beam-D2。

## 2. 不变边界

- 不修改当前 DAG2.3 球面生产绑定；Beam-D1 仍是独立预览和测试链。
- 不修改 M3 共享合同、TaskGraph 输入或默认绑定。
- `GameplayProfileId + DifficultyTier` 仍是唯一外部玩法输入；Seed 只改变同一身份下的实例。
- 缺资产、非法 Member 或编译失败时必须拒绝当前 Profile，不得静默换成另一 Profile。
- Beam-B 的闭合装配是几何权威：D1 不重排、不补柱、不再次执行 WFC。

## 3. 数据链

```text
GameplayProfileId + DifficultyTier + Seed
  -> Beam-D0 ResolvedProfile
  -> Shape Grammar/WFC silhouette
  -> Beam-A structural IR
  -> Beam-B closed assembly
  -> Beam-C Load DAG/static proxy
  -> Beam-D1 member-role selection
  -> one Member : one Brick binding
  -> FABTSM7BrickSpec + LocalTransform
  -> editor material preview / AABTSM7BuildingModule runtime instance
```

一对一绑定是 D1 的硬合同。以后若需要把超长梁离散为多个物理 Brick，必须在新版本中引入
显式 `Member -> Brick[]` 分段合同，不能在当前链路中隐式拆分。

## 4. Brick 几何编译

当前 Beam 只接受 X/Y/Z 三种轴向 Member。以 `BlockCrossSectionCM` 为固定截面：

- X Member：`Dimensions = (Length, Section, Section)`；
- Y Member：`Dimensions = (Section, Length, Section)`；
- Z Member：`Dimensions = (Section, Section, Length)`；
- Brick 中心为两个 Joint 的中点，局部旋转为单位旋转；
- `LocalBounds` 从最终真实尺寸计算，不沿用 Shape 体积或 Bay 包围盒。

编译完成后必须满足：有效 Joint 引用、轴向和端点一致、正长度和正截面、完整 Member 引用、
真实 Brick AABB 总包围盒有效、严格穿透计数为零。

## 5. 材料与角色

每个 Brick 同时拥有两组正交信息：

1. `EABTSM7BuildingMaterial`：Wood / Stone / Iron / Glass，直接消费现有网格、材质和物理参数；
2. D1 语义角色：PrimaryFrame、SecondaryFrame、Connector、WeaknessCandidate、
   DeviceAnchor、DevicePayload。

首版 Palette 映射：

| MaterialPalette | 普通框架 | 连接/桥托 | 弱点候选 | 装置候选 |
|---|---|---|---|---|
| LightFrameFragileJoint | Wood | Iron | Glass | Wood |
| MasonryWithWoodSeam | Stone | Iron | Wood | Wood |
| IronFrameGlassTrigger | Iron | Iron | Glass | Glass |
| SuspendedStonePod | Wood | Iron | Wood | Stone |

候选选择只使用已存在的 Member/Load DAG，且完全确定：ColumnBreak 选承载 Z 柱，
Seam/Slide 优先桥梁与连接构件，TipOver 选外侧承载 Z 柱，DropTrigger 选高处承载构件。
选择结果必须至少覆盖一个真实 Brick；但 D1 不降低 BreakDamage，也不生成隐式约束。

## 6. 编辑器与运行时

- `AABTSM73BeamD1PreviewActor` 在编辑器以四组真实 M7 材质 HISM 显示编译结果；这些组件
  无碰撞且游戏中隐藏，不参与物理。
- Actor 提供显式运行时实例化入口；只有开启 D1 PIE 运行时开关时，才通过现有
  `AABTSM7BuildingMaterialSystem::SpawnBrickModule` 生成真正的
  `AABTSM7BuildingModule`。
- 运行时模块使用 D1 已编译的 Profile 身份、BrickSpec 和变换，不重新选 Profile。

## 7. 输出与诊断

Summary 至少包含：Profile 身份、Tier、Member/Brick/引用数、四种材料数量、弱点/装置角色
数量、严格穿透数、真实局部 AABB、上游 Hash、D1 几何/角色 Hash 和稳定拒绝原因。

## 8. 自动化验收

1. 五个 Profile 在代表 Tier 下均可编译，且至少产出一个真实 Brick Spec；
2. `MemberCount == BrickCount == CompleteReferenceCount`；
3. 同输入 Hash、角色和材料完全确定；换 Seed 不改变 Profile 身份；
4. 四种 Palette 的实际材料和角色符合表格；
5. 所有 Brick 位于报告的真实局部 AABB 内，严格穿透为零；
6. 在测试 World 中，通过真实 MaterialSystem 至少生成一个
   `AABTSM7BuildingModule`，其材质枚举、尺寸和引用可回查；
7. 强制 Unity 编译以及 `ABTS.M73DAG.BeamD1.*`、`ABTS.M7.*` 回归通过。

## 9. 可见验收

将 D1 Preview Actor 放入 `PlanarPhysicsTestMap`，依次选择五个 Profile：

- 建筑轮廓、桥和屋顶仍与 Beam-B/C 一致；
- 不出现新悬空、重叠或 Member 丢失；
- 不同 Palette 能看到真实 Wood/Stone/Iron/Glass 材质分工；
- PIE 运行时开关关闭时只看编辑器预览；开启时预览隐藏并生成真实 Brick Module。

通过这些检查只表示 Beam-D1 完成，不能宣称建筑已经通过可玩弱点和 Chaos 倒塌认证。

## 10. 2026-08-03 首版实现证据

- `FABTSM73BeamD1BrickCompiler` 完成 D0→Shape→Beam-A/B/C→D1 的单次、失败关闭链；
- `AABTSM73BeamD1PreviewActor` 提供四种真实 M7 材质预览，并以显式开关接入
  `AABTSM7BuildingMaterialSystem::SpawnBrickModule`；
- 五个固定 Profile 的代表性确定种子均完成一对一 Member/Brick 编译；这些种子只是测试夹具，
  生产编译器不会在失败后自动换 Seed 或换 Profile；
- `ABTS.M73DAG.BeamD1.*` 5/5 通过，覆盖确定性、失败关闭、五 Profile、材料角色和真实 Module；
- `ABTS.M73DAG.Beam` 路线回归 42/42、`ABTS.M7` 全量回归 115/115 通过；
- 强制 Unity、禁用 Adaptive Unity 的 Development Editor 编译通过；
- 未修改共享合同、配置、Build.cs、地图资产或 DAG2.3 生产绑定。
