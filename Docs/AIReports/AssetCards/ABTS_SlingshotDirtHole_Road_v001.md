# ABTS_SlingshotDirtHole_Road_v001

- 记录状态：已完成概念图与 Hunyuan3D 图生 3D；最终模型按当前版本保留，待填写实际生成日期、模型文件名和授权截图。
- AI 流程：Hunyuan 文生图 → 选定概念图 `SlingshotDirtHole.png` → Hunyuan3D 图生 3D → UE 5.8 导入验收。
- 概念图文件：`Docs/AIReports/AssetCards/SlingshotDirtHole.png`
- 文生图工具：Hunyuan（具体版本/账号套餐：待补填）
- 图生 3D 工具：Hunyuan3D（具体版本/账号套餐：待补填）
- 生成日期：待补填
- 商业/比赛授权核对日期：待补填
- 原始 3D 输出：`SourceArt/AI/SlingshotDirtHole/`（待补填实际 FBX/GLB 文件名）
- 最终 UE 资产：`/Game/ABTS/Art/Slingshot/SM_ABTS_SlingshotDirtHole_Road_A`
- 最终模型处理：保留 Hunyuan3D 当前轮廓；如需优化，只做可控减面、Pivot/尺度整理和材质替换，不重新改变洞口造型。
- AI 使用范围：AI 决定概念轮廓和初始 3D 外形；AI 不决定 CellTopo Anchor、发射轴、Pivot、碰撞或插桩状态。
- 人工编辑范围：确认单洞尺寸、局部坐标、Pivot、材质槽、无碰撞设置、道路摆放和插桩前后表现不变。
- 文生图完整提示词：

```text
original stylized low-poly dirt planting hole on a compact road,
a shallow oval hole cut into packed warm brown soil,
slightly raised compressed-earth rim,
a clear narrow vertical insertion opening for a wooden pole,
a few small stones and exposed grass roots around the rim,
faceted broad planes, clean readable silhouette,
game environment prop, isolated on a plain light background,
front orthographic view and three-quarter view,
no slingshot post, no rope, no pouch,
no stone base, no metal socket, no character, no text, no logo,
no realistic PBR noise, no deep cave, no water
```

- 图生 3D 输入：使用 `SlingshotDirtHole.png`；图生 3D 额外说明（如工具提供文本输入）：

```text
reconstruct only the shallow low-poly packed-earth hole from the reference image;
keep the broad faceted outer mound, raised compacted soil rim and recessed flat center;
make it a single readable game prop with no slingshot parts;
preserve a clean low-poly silhouette and remove floating fragments, text, logos and background
```

- 负面约束：不要生成弹弓桩、弹弓弦、弹丸袋、石质底座、金属套筒、复杂洞穴、角色、文字或 Logo。

- AssetId: ABTS_SlingshotDirtHole_Road_v001
- 用途：道路上预生成的弹弓桩插入泥土洞
- 逻辑来源：CellTopo Anchor Pair
- 资产类型：无碰撞 Static Mesh
- 单洞建议尺寸：长 85 cm，宽 55 cm，深 18 cm
- 默认局部坐标：
  - +X：弹弓发射方向
  - +Y：道路横向
  - +Z：球面径向向上
- Pivot：洞口几何中心，放在最终地表接触高度
- 必须表现：
  - 压实泥土边缘
  - 明确的插桩孔
  - 低模、宽面、清晰轮廓
- 禁止表现：
  - 石质底座
  - 金属套筒
  - 弹弓桩
  - 弹弓弦
  - 复杂洞穴
  - 文字、Logo、角色
- 三角面预算：250–900
- 材质槽：1–2
- 碰撞：Collision Enabled = No Collision
- AI 允许用途：概念图与泥土洞轮廓候选
- AI 不允许决定：Pivot、CellTopo 位置、桩距、发射方向、碰撞设置
- 验收：
  - 插桩前后洞口模型不改变
  - 左右洞可由 Anchor Pair 镜像摆放
  - 默认 Orbit 相机下可清楚识别
  - 不阻挡角色或相机
