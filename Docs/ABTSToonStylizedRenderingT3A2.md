# ABTS 三渲二 T3-A2：共享鸟与弹弓材质族

> 状态：代码、共享资产、自动化与可见 PIE 验收均已完成；2026-08-06 建立，2026-08-07 验收通过。
>
> 上游：[三渲二总设计](ABTSToonStylizedRenderingDesign.md) · [T3-A0 共享材质族契约](ABTSToonStylizedRenderingT3A0.md)
>
> 下游：T3-C 全量材质、GPU 与可见 PIE 冻结。

## 1. 目标与边界

T3-A2 只迁移 Integration 所有的共享 CuteBird 与弹弓视觉表面：

- CuteBird 身体与脸部保持两个独立语义；红、蓝、黄、黑四只玩法鸟以及开场白鸟均保留原贴图身份；
- Twig、Simple、Reinforced、Steel 四档弹弓的桩、弦、袋分别具有明确候选材质；
- Twig/Simple/Reinforced 归 `SlingshotOrganic`，Steel 归 `SlingshotMetal`；Reinforced 的单一贴图槽以木质主体归类，不虚构网格中不存在的金属子槽；
- 不改变鸟的骨骼网格、动画、碰撞和角色切换；不改变 M6 标定 Profile、发射功率、弦长、物理材质或交互组件；
- 不修改任何既有鸟/弹弓 `.uasset`。所有新增二进制均位于 `Content/Toon/Shared/**`，由 Integration 唯一写入。

## 2. 资产与参数

资产由 `Tools/Rendering/GenerateT3A2SharedMaterials.py` 在固定 UE 5.8 的无界面命令行中按固定目录生成。完整资产集存在时脚本只验证并不重写，避免无意义的二进制变更；只有显式设置进程环境变量 `ABTS_T3A2_REBUILD=1` 才重建四个自有母材质和固定实例集：

| 目录 | 内容 | 数量 |
| --- | --- | ---: |
| `Content/Toon/Shared/Masters` | 鸟身体、遮罩脸部、弹弓贴图表面、弹弓纯色弦母材质 | 4 |
| `Content/Toon/Shared/Birds` | 5 个身体实例 + 5 个脸部实例 | 10 |
| `Content/Toon/Shared/Slingshot` | 4 档 × 桩/弦/袋 | 12 |

母材质全部暴露 T3-A0 冻结的八个公共参数。身体使用较高粗糙度和轻 Rim；脸部保留 `BLEND_Masked` 与贴图 Alpha；有贴图的桩/袋保留 BaseColor、Normal、Roughness 输入；弦保留原 BaseColor、Roughness、Metallic。Steel 只保留受控窄高光，其他档压低塑料感。

`ABTS_StyleEnabled=0` 仍可在母材质内部回到源表面参数，但生产开关以可逆槽注册表为权威：Style Off 会恢复组件进入 T3-A2 前的精确 `UMaterialInterface*`，而不是用一份近似的“旧材质实例”替代。

## 3. 运行时消费

`FABTSSharedStylizedMaterialAdapter` 使用 22 条显式的“已接受源材质 → 风格候选 → Family”记录，不用槽位数字猜测语义：

1. 世界子系统只扫描鸟 Party、开场预演鸟和 M6 已公开的活动弹弓视觉组件；
2. 只有源材质路径命中目录时才发布 `FABTSStylizedMaterialSlotBinding`；未知 Blueprint 覆盖保持原样；
3. 风格候选自身也可再次解析，因此 0.1 秒刷新不会在原材质和候选材质之间振荡；
4. 候选缺失、重复槽冲突或外部系统改写已接管槽时 fail closed；
5. 日志 `[ABTS][Rendering][T3-A2]` 输出鸟/弹弓槽数、22 项目录 Hash、实际接管、冲突、拒绝和 Style 身份。

开场白鸟使用与玩法鸟相同的显式资产目录，不通过修改开场时序或相机实现风格化。

## 4. 自动化门

过滤器：

```text
ABTS.Rendering.Toon.T3A2
```

`SharedMaterialAdapter` 验证：

- 22 项目录及稳定 Hash；
- 身体/脸部语义由源材质决定，即使交换槽位仍不误判；
- 脸部候选仍为 Masked；
- 两个鸟母材质均持久化 `Used with Skeletal Mesh`，运行时不得触发 Default Material 回退；
- 十个鸟实例的 `ABTS_SourceColorTexture` 均非空、匹配冻结身份，且不同身份不得塌缩为同一贴图；
- 公共参数存在；
- 已应用候选可重复发布，刷新不振荡；
- Style Off 恢复精确原材质；
- Reinforced 为 Organic、Steel 为 Metal。

源文件集合变化后必须通过 UE 5.8 `-ForceUnity -DisableAdaptiveUnity` 全链接。

## 5. 可见 PIE 验收

用户在 `L_ABTS_M11` 或当前共同验收地图中执行；本阶段不要求 Codex 自行控制 Editor：

1. 进入 PIE，控制台设置 `abts.Rendering.Stylized.Enabled 1`；地面使用 Profile 0，卫星引导可用 1，终局使用 2。
2. 近看四只玩法鸟：颜色、脸部表情、遮罩边缘、动画和角色切换不变；身体高光较原材质柔和，不能发灰、透明或丢脸。
3. fresh PIE 启动后先确认日志为 `[T3-A2][Preload] Catalog=22 Loaded=22 Complete=22 Failed=0 Ready=1`；第一次进入实际弹弓模式时，当前档的两桩、两段弦和袋必须立即被风格材质覆盖，不得出现默认材质窗口或点击阶段预热卡顿；点击、拉弦、功率步进、轨迹与发射手感不得变化。
4. 控制台执行 `ABTS.OpeningPreview 4`：红、蓝、黄、黑、白五只预演鸟均消费身体/脸部候选，开场时序不变。
5. 在运行中切换 `abts.Rendering.Stylized.Enabled 0`：下一次子系统刷新后鸟和弹弓恢复原材质；日志必须为 `Conflicts=0 Rejected=0`，且再次开启不闪烁、不发生 0.1 秒材质往返。

拒收条件包括：脸部变成不透明矩形、颜色身份改变、弦或袋遗漏、Steel 完全失去金属可读性、Style Off 不能恢复、运行时材质振荡，或任何 M6 标定/碰撞/手感变化。

四档弹弓全部实例的存在与参数由自动化覆盖；若当前地图只能实际生成一档，其他档的最终可见比较可并入 T3-C，不以临时修改冻结标定或地图生成来凑齐画面。

本阶段首次可见 PIE 曾暴露“所有鸟变成同一蓝色且脸部消失”。源贴图实例回读正常，实际日志为鸟身体/脸部候选缺少 `SkeletalMesh` Usage，UE 因而按设计替换为 Default Material。生成器现已把 Usage 作为母材质固有属性，并把 Usage 与实例贴图回读加入无重写验证；验证失败时 fail closed，只有显式 `ABTS_T3A2_REBUILD=1` 才允许重写。资产存在、目录 Hash 或 NullRHI 加载成功均不能单独替代这两项门。

修复 Usage 后首次 PIE 又显示蓝鸟由青蓝变成黄绿色。最初将其归因于身体族粗糙度/高光响应，并加入身份参数保护，但复验仍为绿色，证明该假设错误。资产级回读随后找到蓝鸟独有的历史输入契约：`T_Cutebird_03` 为 `TC_Normalmap / sRGB=false`，原 `M_CuteBird_3` 也以 `Normal` sampler 读取并把解码结果作为 BaseColor；其余四只鸟均为 `TC_Default / sRGB=true / Color` sampler。共享 T3 主材质把五张纹理统一按 Color 读取，因而只有蓝鸟发生通道解码错误。最终方案是不修改源纹理、不强制染色，也不保留无效的表面参数特例，而是让蓝鸟候选单独继承 `M_ABTS_Toon_BirdBody_LegacyNormalColor`，完整保留原 Normal-sampler 语义；其他鸟仍共用标准 Color-sampler 主材质。

普通弹弓在非发射状态使用原材质；进入 M6 发射状态后，世界子系统才为当前两桩、两段弦和袋应用 T3 候选。可见 PIE 曾在此刻让桩和袋回退默认材质。fresh PIE 日志同时给出两个资产合同缺口：`M_ABTS_Toon_SlingshotTextured` 的 Normal 参数以 Color 默认纹理编译，SamplerType 不匹配导致母材质整体编译失败；Textured/Solid 两个弹弓母材质均未持久化 Nanite Usage，三个部件实例首次用于 Nanite 网格时均报告 `missing usage flag Nanite`。生成器现为 Normal 参数固定真实 Normalmap 默认资源，并为两个弹弓母材质固定 StaticMesh 与 Nanite Usage；生成后和无重写启动都回读这些条件。发射模式仅是首次消费候选的触发点，不修改 M6 视觉槽、标定或发射状态机。

修复上述资产合同后，首次点击仍可能出现约 1–2 秒默认材质窗口。其根因是 22 项候选目录此前只在活动弹弓组件第一次发布槽绑定时通过软路径加载；母材质及其着色器排列因此仍把“首次消费”当成预热时机。世界子系统现于 `OnWorldBeginPlay` 同步加载完整候选目录，在 Editor 中对每个候选执行 `EnsureIsComplete()`，并以世界生命周期强引用保持全部候选及其依赖。只有 `Catalog=Loaded=Complete=22` 且 `Failed=0` 时才允许共享鸟/弹弓槽覆盖；否则保留精确原材质并 fail closed。启动日志 `[ABTS][Rendering][T3-A2][Preload]` 记录目录数、加载数、完成数、失败数、Ready 和耗时。该方案允许初次进入地图时一次性承担预热成本，但禁止把成本和默认材质闪烁推迟到玩家第一次点击弹弓。

## 6. 已知边界

- 本阶段未接入 M3 T3-A1、M11 T3-A3 或 M7 T3-B 的只读绑定；它们由后续 Integration 串行汇合。
- `TOON-T2A-002` 地形远端粗褶皱仍等待 T4 光照/阴影隔离；T3-A2 不以材质参数掩盖或关闭该问题。
- NullRHI 自动化证明资产/契约/恢复链，不替代 D3D12 像素和美术判断。
