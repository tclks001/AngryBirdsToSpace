# M7.3 Building Freeze V3 交接

日期：2026-08-15

状态：M7 生成与冻结发布层完成；等待 Integration V3 DTO、M3 位置冻结及新布局 Chaos。

## 1. 本阶段完成项

- 新增 M7 自有 `FABTSM73BuildingFreezeV3` 发布层，不改动既有 Fixed-Six V2 DTO 和消费者。
- 固定 encounter 顺序为 `E2 / E3 / E4 / E5 / E1 / E6`；建筑编号、DifficultyTier 与 EncounterSlot 解耦，E1 保持 Tier0、位于 slot 4。
- 普通主体主材质为 `Wood / Wood / Stone / Iron / Stone / Iron`。材质策略同时进入 Stage 5 结构闭包自重与最终 Brick 编译；Connector、Device、Weakness candidate 与 Crystal cap 不被普通主体覆盖。
- 在 M7 输出端唯一执行 content-to-site 旋转：content `+Y` 映射到 site `+X`；发布旋转后的 site-local bricks/devices、OBB、PadBounds 与 EffectBounds。
- E1 发布且只发布一个 `72×72×72 cm` Crystal 顶帽。它不进入 Beam member 或 Load DAG，`bLoadBearing=false`、`bWeaknessCandidate=false`、`DeviceRole=None`，其上没有其它静态几何。
- Crystal 特殊砖可通过 caller-owned static module 显示、碰撞、破坏和回收，不参与全局 launch activation；Module break 幂等，回收事件精确一次。

## 2. 冻结身份

Source Manifest：version `1`，hash `2324068295`

BuildingFreezeV3 schema：`3`

Catalog hash：`8960617043786800590`

| Slot | Building | Tier | Seed | Primary | Bricks / Devices / Caps | Histogram W/S/I/G/C | Stage5 | DeviceAssembly | StaticGeometry | Production | Descriptor |
| ---: | --- | ---: | ---: | --- | --- | --- | ---: | ---: | ---: | ---: | ---: |
| 0 | E2 | 1 | 740000 | Wood | 257 / 1 / 0 | 257/0/0/0/0 | 17685577480875777327 | 17110365351347297356 | 9035518740462017661 | 2547697344996591725 | 2093905216809054552 |
| 1 | E3 | 2 | 750137 | Wood | 388 / 1 / 0 | 388/0/0/0/0 | 3783807544959526326 | 13499840356386341553 | 6056576068412876568 | 5980623437000438947 | 4766851746474182140 |
| 2 | E4 | 3 | 730000 | Stone | 904 / 1 / 0 | 0/904/0/0/0 | 8626866139811673118 | 4267868371875890409 | 12346635070808564758 | 10546537168470496360 | 4414623922721955589 |
| 3 | E5 | 4 | 720000 | Iron | 1903 / 1 / 0 | 0/0/1903/0/0 | 6515755032372742292 | 15204117308279581184 | 17932683668713717862 | 11772527566289753088 | 543918785024958331 |
| 4 | E1 | 0 | 710000 | Stone | 52 / 1 / 1 | 0/52/0/0/1 | 11654936042725289290 | 976568201920830387 | 261011352776326791 | 18375681187970766733 | 6197101184822124424 |
| 5 | E6 | 5 | 750000 | Iron | 2235 / 1 / 0 | 0/0/2235/0/0 | 2348159192872953385 | 198894657042108135 | 11440919070458269246 | 11323455661476895076 | 3187373410644525608 |

## 3. Site-local Bounds

格式为 `Min -> Max`，单位 cm。

| Slot | SiteLocalBounds | PadBounds | EffectBounds |
| ---: | --- | --- | --- |
| 0 | `(-450,-486,0) -> (450,774,1476)` | `(-486,-522,0) -> (486,810,1476)` | `(-1138,-58,-670) -> (382,1462,850)` |
| 1 | `(-414,-1026,0) -> (414,1026,1332)` | `(-450,-1062,0) -> (450,1062,1332)` | `(-522,774,-252) -> (-162,1134,468)` |
| 2 | `(-378,-846,0) -> (378,846,2376)` | `(-414,-882,0) -> (414,882,2376)` | `(-486,-342,-144) -> (-126,378,216)` |
| 3 | `(-630,-1350,0) -> (630,1350,2376)` | `(-666,-1386,0) -> (666,1386,2376)` | `(126,846,-144) -> (486,1566,216)` |
| 4 | `(-162,90,0) -> (162,432,720)` | `(-198,54,0) -> (198,468,720)` | `(-850,-418,-670) -> (670,1102,850)` |
| 5 | `(-486,-1062,0) -> (486,1062,3384)` | `(-522,-1098,0) -> (522,1098,3384)` | `(-594,558,-144) -> (-234,1278,216)` |

## 4. 验证证据

- 构建：唯一引擎 `C:\Program Files\Epic Games\UE_5.8`，`AngryBirdsToSpaceEditor Win64 Development -ForceUnity` 成功。
- fresh NullRHI filter：`ABTS.M73DAG.BuildingFreezeV3`，`2/2` 成功。
- V3 日志：`Saved/Logs/M7BuildingFreezeV3_FinalPass_20260815.log`。
- Crystal 基线与回收映射日志：`Saved/Logs/M7CrystalBaselineRecovery_Final_20260815.log`、`Saved/Logs/M7CrystalRecoveryMapping_Final_20260815.log`。
- 旧生产兼容回归：`Saved/Logs/M7Stage5Production_V3Compatibility_20260815.log`，`3/3` 成功。
- 自动化覆盖：encounter 顺序、E1 tier/slot 解耦、主材质、`+Y -> +X`、OBB/Pad/Effect、全局 Crystal=1、E1 cap 语义、全体静态 collision box 零正体积穿透、静态 cap 不参与 launch、碰撞破坏和精确一次回收。

## 5. 明确未完成/未越权项

- 未修改 Integration 共享 V3 DTO、M3 场地位置、任务图、共享 Physics 配置或任何地图。
- 未保存或覆盖 `/Game/StaticMesh/BrickMaterials/MI_Bricks_Crystal`。
- 未运行 GUI、Editor 可见预览或 PIE。Crystal 透明、自发光排序和月球场景可读性仍需用户可见验收。
- 当前 V2 固定六栋运行时继续保持原行为；Integration 发布 V3 DTO 后才接入 V3 runtime consumer。
- 新位置冻结前不恢复旧位置 Chaos 结论；位置完成后按计划重新执行 E1～E6 生产引力 Chaos。
