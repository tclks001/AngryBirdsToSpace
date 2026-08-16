# M7.3 Chaos 生产物理身份 checkpoint（等待重新冻结布局）

> 日期：2026-08-15
> 分支：`feature/m7-buildings`
> 基线：`master@9115bcb` 已通过合并提交 `950427b` 进入本工作树
> 状态：夹具身份改造完成；旧 Fixed-Six 位置的 Chaos 结果已失效，停止在 E3，不继续 E4～E6

## 1. 本 checkpoint 完成的基础设施

- M7 动态模块统一使用 `FABTSM7ChaosBodyProfile`：Position/Velocity solver 为
  `80/20`，线性/角阻尼为 `2/4`。该配置由生产 `ConfigureImpactPhysics` 和
  Chaos 夹具共同消费，不再只存在于测试代码。
- `FABTSM7ChaosWorldProfile` 只读捕获当前项目的 substep、async step、最大物理
  Delta、Chaos friction/shock CVar，并发布稳定 CRC。测试不得修改这些全局值。
- `BeginLaunchPhysics` 发布 Body/World Hash、重力模型、重力参考点和穿透统计。
- Stage-5 Chaos 夹具从 M3 的公开 `TryExportBuildingGenerationContract` 边界读取
  Fixed-Six 世界变换，在站点切平面生成当前 M7 Stage-5 刚体，并通过生产入口
  `BeginLaunchPhysics(false, PlanetCenter, 980)` 激活。
- 夹具外步为默认生产 `60 Hz`，重力为当前生产实现的“恒定 980 cm/s²、方向逐帧
  指向球心”；它不是距离平方反比的牛顿引力。
- 删除旧夹具专属的强制 `4×120 Hz` substep、Chaos 全局 CVar 覆盖、原生固定向下
  重力以及 Actor Tick 禁用。
- 穿透验证的相切过滤从世界轴 AABB 改为 StaticMesh OBB 的 15 轴 SAT，使其在
  M3 任意冻结旋转下保持不变；真实正体积穿透仍 fail closed。

## 2. 为什么必须停止旧位置验证

当前 M3 Fixed-Six V2 站点与当前 M7 Stage-5 生产几何已经存在身份漂移。例如：

- E1：合同 Production Hash `6524532268529485689`，当前 M7 Hash
  `3269906618432483684`；
- E2：合同 Production Hash `3864694895529971157`，当前 M7 Hash
  `17685577480875777327`；
- E3：合同 Production Hash `15118401498293854757`，当前 M7 Hash
  `12171233134421135637`。

因此夹具明确拆分记录：

- `PositionAuthority=M3FrozenV2`；
- `GeometryAuthority=M7CurrentProduction`；
- `ContractProductionEnvelopeMatches=0`。

用户进一步确认当前合同的空间布局本身也将重新冻结，尤其需要处理月球背面尚未
放置建筑的问题。自该决定起，旧站点变换不再是候选依据。正在进行的 E1 final
复验已中止；不得继续使用旧位置运行 E2～E6，也不得把下列旧位置结果写入新合同。

## 3. 仅作诊断保留的旧位置结果

这些结果只证明新夹具确实能区分 quiet 与空间姿态，不构成新布局验收：

| 建筑 | 结果 | 峰值漂移 / 沉降 / 转角 | 说明 |
| --- | --- | --- | --- |
| E1 | 旧位置通过 | `0.610 cm / 0.241 cm / 0.100°` | 52 Body，生产径向重力 |
| E2 | 旧位置通过 | `1.604 cm / 1.091 cm / 0.302°` | 257 Body，生产径向重力 |
| E3 | 旧位置失败 | `8.280 cm / 0.689 cm / 0.926°` | 已达到并结束 quiet window，但最终漂移 `7.941 cm`，证明 quiet 不等于姿态合格 |
| E4～E6 | 未运行 | — | 按逐栋顺序停在 E3；随后因布局将重冻而整体停止 |

旧位置诊断日志：

- `Saved/Logs/M7-Chaos-ProductionIdentity-E1-R3-20260815.log`
- `Saved/Logs/M7-Chaos-ProductionIdentity-E2-20260815.log`
- `Saved/Logs/M7-Chaos-ProductionIdentity-E3-FinalUnity-R2-20260815.log`

## 4. 已完成验证

- UE 5.8 Development Editor 普通完整链接成功；
- UE 5.8 `-ForceUnity -DisableAdaptiveUnity` 完整链接成功；
- ForceUnity 二进制上的 Stage5Production 为 `3/3`：
  `Saved/Logs/M7-Stage5Production-ProductionIdentity-FinalUnity-20260815.log`；
- 旧位置 E3 在普通与 ForceUnity 二进制上得到完全一致的空间失败指标；
- 未启动 GUI、可见 PIE 或 D3D12；未修改地图、M11 资产、共享合同或 Physics 配置。

## 5. 新冻结版本到达后的恢复顺序

1. 由原始集成工作树完成新的 M3/Fixed-Six 空间布局与共享合同冻结；M7 不在本工作树
   修改这些共享文件。
2. M7 合并更新后的 `master`，先核对 Contract Version、Layout Hash、六个站点变换、
   月球正/背面覆盖意图，以及逐项 Production/Device/Bounds 身份。
3. 若 M3 合同 Production Hash 与 M7 当前 producer 不一致，先回到 Integration/M3
   完成合法 V2 更新；禁止夹具绕过生产登记的 fail-closed 规则。
4. 在新位置重新生成 Candidate Hash，先跑 Stage5Production 与旋转后零穿透门。
5. 严格按 E1 → E2 → E3 → E4 → E5 → E6 串行；任一栋未解决，不提前为后续建筑
   记通过结论。
6. NullRHI 通过后，仍需真实地形、实时 30/60/120 FPS 与可见 PIE 独立验收；固定
   60 Hz 切平面夹具不能单独代表生产完成。
