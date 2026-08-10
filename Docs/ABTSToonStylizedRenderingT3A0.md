# ABTS 三渲二 T3-A0：共享材质族契约

> 状态：代码与自动化完成；2026-08-06 建立。无二进制资产或可见画面变化，不要求本切片单独进行可见 PIE。
>
> 上游：[三渲二总设计](ABTSToonStylizedRenderingDesign.md) · [T2-B1 选择性语义与画中画](ABTSToonStylizedRenderingT2B1.md) · [T2-C1 无 M7 动态回归](ABTSToonStylizedRenderingT2C1.md)
>
> 下游：T3-A1 M3 地形/自然物、[T3-A2 共享鸟/弹弓](ABTSToonStylizedRenderingT3A2.md)、T3-A3 M11 行星/UFO、T3-B M7 建筑、T3-C 全量冻结。

## 1. 目标与边界

T3-A0 在任何工作树创建或绑定风格材质前，冻结一条 Integration 拥有的公共消费契约：

- 每个材质族只有一个稳定 ID 和唯一所有者；
- 参数名、单位、有效域和 Style Off 语义一致；
- 普通材质槽由一个可逆注册表接管，关闭风格或退出世界时恢复精确的原 `UMaterialInterface*`；
- M3 SDF 地形保留代码生成 MID 与 `M3_*` LUT 注入，以材质内部参数切换原始/风格分支；
- 材质资源缺失、绑定非法或槽位被外部系统接管时 fail soft，不改变玩法与 WorldReady。

本切片不创建 `.uasset/.umap`，不修改 M3/M7/M11 专属资产，不让画面发生预期视觉变化，也不解决 `TOON-T2A-002` 光照/阴影问题。

## 2. 分阶段排期

| 切片 | 所有者 | 产出 | 能否在 M7 Beam-C3 未完成时验收 |
| --- | --- | --- | --- |
| T3-A0 | Integration | 本契约、恢复注册表、诊断、自动化 | 是 |
| T3-A1 | M3 | 地表、树、石适配 | 是 |
| T3-A2 | Integration | 鸟和弹弓共享材质 | 是 |
| T3-A3 | M11 | 行星、UFO 与终局专属材质 | 是 |
| T3-B | M7 | 建筑静态/破坏材质 | 否；等待 M7 干净检查点 |
| T3-C | Integration | 全量绑定、参数与 GPU 冻结 | 否；必须包含 M7 |

T3-A1/A2/A3 的独立通过只证明各自消费链，不能替代 T2-B2/T2-C2、T3-B 或 T3-C。

## 3. 公共类型契约

公共定义位于 `ABTSRender`：

- `EABTSStylizedMaterialFamily`：材质族身份；
- `EABTSStylizedMaterialOwner`：Integration/M3/M7/M11 唯一所有者；
- `EABTSStylizedMaterialAdoptionMode`：采用可逆槽覆盖还是原位参数；
- `FABTSStylizedSurfaceParameters`：共享参数语义和合法范围；
- `FABTSStylizedMaterialContract`：版本、Hash、所有权、默认参数和稳定参数名。

`MaterialContractVersion=1` 的枚举只允许尾部追加。修改既有枚举含义、参数名、单位或 Style Off 行为必须提升版本并走集成契约迁移，功能工作树不能单方面改写。

### 3.1 稳定参数名

| 名称 | 类型 | 单位/范围 | Style Off 语义 |
| --- | --- | --- | --- |
| `ABTS_StyleEnabled` | Scalar | `0` 或 `1` | `0` 必须复现进入 T3 前的表面分支 |
| `ABTS_BaseColorTint` | Vector | 线性 RGB；默认白色 | 不改变原 BaseColor |
| `ABTS_RoughnessFloor` | Scalar | `[0,1]` | 不参与原始分支 |
| `ABTS_RoughnessScale` | Scalar | `[0,2]` | 不参与原始分支 |
| `ABTS_SpecularScale` | Scalar | `[0,2]` | 不参与原始分支 |
| `ABTS_MetallicScale` | Scalar | `[0,1]` | 不参与原始分支 |
| `ABTS_RimStrength` | Scalar | `[0,1]` | `0` |
| `ABTS_RimPower` | Scalar | `[1,32]` | 不参与原始分支 |

这些参数只负责材质族的表面响应，不能携带 PCG、碰撞、物理材质、轨迹或相机状态。T3-A1/A2/A3 可以调整各族默认美术值，但不得创建同义的本地参数名。

### 3.2 家族与所有权

| 材质族 | 所有者 | 采用方式 | 特殊不变量 |
| --- | --- | --- | --- |
| `M3Surface` | M3 | `InPlaceStyleParameter` | 保留地形 MID 和全部 `M3_*` 注入 |
| `M3BackgroundProp` | M3 | `ReversibleSlotOverride` | 保留 HISM 实例/批次语义 |
| `CuteBirdBody` / `CuteBirdFace` | Integration | `ReversibleSlotOverride` | 保留身体/脸部槽位与颜色身份 |
| `SlingshotOrganic` / `SlingshotMetal` | Integration | `ReversibleSlotOverride` | 不改 M6 标定、碰撞或物理材质 |
| `M7Wood/Stone/Steel/Glass` | M7 | `ReversibleSlotOverride` | T3-B 前仅保留 ID；玻璃保持 Opacity 语义 |
| `FinalePlanet` / `FinaleUFO` | M11 | `ReversibleSlotOverride` | 不改四体求解、权威路径或演出时序 |

## 4. 可逆材质槽注册表

`FABTSStylizedMaterialOverrideRegistry` 只接受 `FABTSStylizedMaterialSlotBinding`：组件、槽位、风格材质和稳定材质族。处理规则：

1. Style On 首次接管时保存原材质接口，再应用风格材质；
2. 同一槽的相同声明幂等；互相矛盾的重复声明记录冲突并拒绝接管；
3. Style Off、对象离开期望集合或世界退出时，仅当槽位仍等于注册表应用的接口才恢复原材质；
4. 若外部系统已经改变该槽，注册表失去所有权，保留外部材质并记录冲突；
5. `M3Surface` 等原位参数族进入槽注册表会被拒绝；
6. 注册表持有原/风格材质的 GC 引用，避免覆盖期间原材质被回收。

首版世界子系统已创建该注册表并将版本、Hash、接管数、冲突数和拒绝数写入 `[ABTS][Rendering][T3-A0]` 摘要。T3-A0 的期望值是 `MaterialSlots=0 MaterialConflicts=0 MaterialRejected=0`；后续切片接入后才允许槽数增加。

## 5. 下游消费规则

### 5.1 M3 / T3-A1

- 只修改 M3 自有代码和 `Content/M3/Toon/**`；
- SDF 地形继续由 `UABTSM3TerrainMaterialBridge` 创建 MID 并写入既有 LUT；在同一 MID 写 `ABTS_StyleEnabled`，值为零时必须走原始表面分支；
- 树/石适配器只发布只读 `FABTSStylizedMaterialSlotBinding`，不直接修改共享注册表。

### 5.2 Integration / T3-A2

- 身体和脸部分别声明，不以槽位数字猜测语义；
- Twig/Simple/Reinforced/Steel 的桩、弦、袋分别归类为 Organic 或 Metal；
- 视觉材质与冻结的弹弓 Profile、物理材质完全分离。

### 5.3 M11 / T3-A3

- 只创建 M11 自有材质和只读绑定适配器；
- 终局 Actor 仍以求解/演出系统为位置权威，材质缺失只回退原渲染；
- Scene Capture 和 AVI 继续消费既有 `ViewClass`，不建立第二套材质开关。

### 5.4 M7 / T3-B

M7 当前只保留公共 family ID，不要求修改 Beam-C3 脏工作区。待 Beam-C3 交付后由 M7 在最新 `master` 上实现建筑主体、弱点和破坏后碎片的绑定；其他工作树不得修改 `Content/StaticMesh/BrickMaterials/**` 代做。

## 6. 自动验收

T3-A0 是无资产、无视觉差异切片，因此编译和自动化是正式门，可见 PIE 不作为本切片退出条件：

```text
Build.bat AngryBirdsToSpaceEditor Win64 Development \
  <uproject> -WaitMutex -NoHotReloadFromIDE -ForceUnity -DisableAdaptiveUnity

UnrealEditor-Cmd.exe <uproject> \
  -ExecCmds="Automation RunTests ABTS.Rendering.Toon.T3A0;Quit" \
  -unattended -nop4 -nosplash -NullRHI -log
```

必须通过：

- `ABTS.Rendering.Toon.T3A0.MaterialContract`：所有家族、所有权、采用方式、参数名和范围稳定；
- `ABTS.Rendering.Toon.T3A0.MaterialOverrideRegistry`：Style On 应用、Style Off 精确恢复、外部接管 fail closed、原位参数族拒绝进入槽注册表；
- 现有 `ABTS.Rendering.Toon` 回归无倒退；
- 强制 Unity Development Editor 编译通过。

2026-08-06 当前候选证据：

- 唯一允许引擎 `C:\Program Files\Epic Games\UE_5.8`，`-ForceUnity -DisableAdaptiveUnity` Development Editor 全链接成功且新代码无编译警告；
- fresh NullRHI `ABTS.Rendering.Toon.T3A0` 精确 `2/2` Success、项目 Error 为 0，完成标记 `EXIT CODE: 0`，日志 `Saved/Logs/ToonT3A0-Approved-T3A0-20260806-202526-FreshAutomation.log`；
- fresh NullRHI `ABTS.Rendering.Toon` 精确 `11/11` Success、项目 Error 为 0，完成标记 `EXIT CODE: 0`，日志 `Saved/Logs/ToonT3A0-Approved-FullToon-20260806-202554-FreshAutomation.log`；
- 未启动可见 Editor、PIE 或资产写入流程。

## 7. T3-A0 退出与 T3-C 正式门

T3-A0 退出只意味着下游可以基于版本 1 开发。完整 T3 仍需在 T3-C 验证：

- 同一 Seed/相机/时间的 Style Off/On；
- M3、鸟/弹弓、M7、M11 全部族接入；
- 地面/月面/终局画中画、终局 AVI 与建筑破坏；
- Style Off 后无材质槽、Stencil 或 Scene Capture 遗留；
- PCG、弹弓、Chaos 和 M11 权威 Hash 不变；
- 1080p/1440p GPU 门通过。
