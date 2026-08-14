# ABTS 共享首屏、暂停与系统设置设计

> 状态：集成候选 v1。纯 C++、无 Widget Blueprint、无 DataAsset、无地图手工绑定。
>
> 上游：[UI 系统设计](UISystemDesign.md) · [共享 UI Theme](ABTSSharedUIThemeDesign.md) · [音频设计](AudioDesign.md)

## 1. 目标与边界

本阶段补齐所有地图共同需要、且不应由 M3/M7/M11 各自重复实现的基础体验：

- 首次进入 Game/PIE 时显示统一首屏；
- 游戏中按 `Esc` 或手柄 Menu 键打开暂停菜单；
- 音频、视频与可访问性设置立即应用并写入 `GameUserSettings.ini`；
- 首屏、暂停和设置覆盖所有现有 GameMode，不要求修改地图或 Blueprint；
- `-unattended`、Commandlet 和自动化默认跳过首屏，避免暂停测试世界；
- 可用显式离屏捕获参数生成真实 GameViewport 像素证据。

本阶段不建立存档槽、关卡选择、联网大厅或输入重绑定。这些系统需要独立的数据和产品规则，不能用占位按钮伪装完成。

## 2. 运行时架构

`UABTSGameViewportClient` 由 `DefaultEngine.ini` 全局绑定，在 `Super::Draw()` 后使用最终 DebugCanvas 绘制覆盖层。因此它不依赖各地图的 HUD 类，也不会被阶段 HUD 或屏幕调试文字盖住。

`UABTSGameUserSettings` 是 `UGameUserSettings` 子类，保存：

- Master、Music、SFX、UI、Ambience 五路音量；
- Overall Quality、Resolution、Window Mode、VSync、Frame Cap、Dynamic Resolution；
- Menu Scale、Display Gamma、Subtitles、Mute When Unfocused、Reduce Motion、High Contrast Menu。

音频值不是另建一套声音播放通路。它们以 `Master × Category` 传入既有 `UABTSAudioWorldSubsystem::SetCategoryVolumes()`，继续消费 `/Game/Audio/Infrastructure` 的四路 SoundClass 与 Master SoundMix。新世界启动时主动读取持久设置，地图切换后不会回到默认音量。

## 3. 输入与暂停

| 输入 | 菜单关闭 | 菜单打开 |
| --- | --- | --- |
| `Esc` / Gamepad Menu | 打开暂停菜单 | 返回上一层或恢复游戏 |
| `W/S`、方向键、D-Pad | 原玩法输入 | 上下导航 |
| `A/D`、左右键、D-Pad | 原玩法输入 | 调整当前设置 |
| `Enter` / Space / Gamepad Confirm | 原玩法输入 | 确认 |
| `Q/E` / Shoulder | 原玩法输入 | 切换设置页签 |
| 鼠标 | 原玩法输入 | 点击真实 Canvas 热区 |

打开菜单时记录世界原暂停状态和鼠标显示状态，设置 `GameAndUI` 输入并暂停；关闭时只在本菜单原先造成暂停的情况下恢复。PIE 中不绘制 `Quit Game`，避免误关 Editor；Standalone/打包版本才提供退出。

## 4. 默认值与持久化

默认值硬编码在 `UABTSGameUserSettings::SetToDefaults()`：Epic、60 FPS、VSync Off、Dynamic Resolution Off、五路音量 100%、Menu Scale 100%、Gamma 2.2、Subtitles On、后台静音 On、Reduce Motion Off、High Contrast Off。

每次调整都会调用 `ApplyAndSave()`：

- Resolution/Window Mode 使用 `ApplySettings(false)`；
- 其他视频设置使用 `ApplyNonResolutionSettings()`；
- 音量即时更新当前世界 SoundMix；
- Gamma、字幕、后台音量和 Motion Blur 即时更新；
- 最终统一保存。

Resolution 与 Window Mode 应用后进入 12 秒独占确认层。玩家选择 `KEEP` 后才调用 `ConfirmVideoMode()`；选择 `REVERT`、按 Esc 或倒计时结束均调用 `RevertVideoMode()` 并重新应用上一份已确认显示设置。Reset Defaults 同样要求二次确认，默认焦点放在 `CANCEL`，避免误触。

Schema 变化必须提升 `SettingsSchemaVersion`。未知旧 Schema fail closed 到本版默认值。

## 5. 控制台与命令行

PIE 可实时使用：

```text
abts.Menu.Open
abts.Menu.Front
abts.Menu.Settings
abts.Menu.Close
abts.Settings.Dump
abts.Settings.Reset
```

自动化/专用运行参数：

| 参数 | 作用 |
| --- | --- |
| `-ABTSSkipFrontEnd` | 交互运行也跳过首次首屏 |
| `-ABTSMenuCapture=Front` | 强制首屏并在稳定 60 帧后截图 |
| `-ABTSMenuCapture=Pause` | 强制暂停页截图 |
| `-ABTSMenuCapture=SettingsAudio` | 强制音频设置页截图 |
| `-ABTSMenuCapture=SettingsVideo` | 强制视频设置页截图 |
| `-ABTSMenuCapture=SettingsAccessibility` | 强制可访问性设置页截图 |
| `-ABTSMenuCapture=SettingsVideoConfirm` | 强制显示视频设置确认层截图 |
| `-ABTSMenuCapture=SettingsResetConfirm` | 强制恢复默认值确认层截图 |
| `-ABTSMenuCaptureOutput=<abs.png>` | 指定唯一输出；缺省写入 `Saved/ABTSVisualCaptures/SystemMenu` |

捕获器拥有 45 秒硬超时；截图文件不存在时以非零状态退出并记录 `Success=0`，禁止只凭进程返回值宣称视觉通过。

## 6. 验收

自动化：`ABTS.UI.SystemMenu.SettingsContract` 必须精确 1/1 通过，验证默认值、钳制、诊断身份、分辨率回退和帧率标签。

视觉：至少在 1280×720 检查 Front 与 SettingsVideo；不得有文字裁切、直角底板穿出截角框、阶段 HUD 盖住菜单或点击箭头离开对应热区。1920×1080 用于最终人工验收。

手工 PIE：

1. 启动任一常规地图，首屏出现且世界暂停；
2. Begin 后世界恢复；
3. `Esc` 打开 Pause，Settings 三页均可键鼠操作；
4. 将 Music 调为 0，音乐静音但 SFX/UI 保持；
5. 修改分辨率/窗口模式后确认应用；
6. Stop PIE、重新 PIE，确认设置仍保留；
7. PIE 菜单不显示退出 Editor 的按钮。

## 7. 性能与排错

菜单关闭时只有 `GameViewportClient::Tick` 的一次布尔检查，不创建 Widget、不加载纹理、不增加场景采样器。菜单打开时每帧绘制约 20–80 个 Canvas primitive；截图通路只有显式参数才注册 delegate。

| 症状 | 根因 | 修复 |
| --- | --- | --- |
| 自动化世界一开始就暂停 | 首屏未排除 unattended | 保持 `FApp::IsUnattended()` 与 Commandlet 门；测试不要显式传 Capture 参数 |
| 音量调整后换地图恢复 | 新世界只用了 DeveloperSettings 默认值 | `OnWorldBeginPlay` 读取 `UABTSGameUserSettings` 后再建立音乐组件 |
| 旧屏幕调试文字盖住菜单 | 菜单画在 SceneCanvas，DebugCanvas 更晚 | `Super::Draw()` 后在 Viewport DebugCanvas 绘制系统覆盖层 |
| 离屏进程成功但没有图 | 只看进程码，没有核对截图 | 捕获 delegate 核对绝对路径文件大小并输出唯一 Complete 标记 |
| PIE 点击 Quit 关闭了 Editor | 未区分 WorldType | PIE 隐藏 Quit；只有 Game/Standalone 显示 |

## 8. 集成候选 v1 证据

- UE 5.8 Development Editor `-ForceUnity -DisableAdaptiveUnity -NoHotReload` 完整链接成功。
- fresh NullRHI `ABTS.UI.SystemMenu.SettingsContract` 精确发现 1 项、1/1 成功并以 `TEST COMPLETE. EXIT CODE: 0` 结束；确认保护最终日志：`Saved/Logs/SystemMenu-ConfirmFinalContract-20260815-055235.log`。
- shared UI 前缀回归精确发现并通过 3/3（Flight、SystemMenu、Theme），日志：`Saved/Logs/SystemMenu-SharedUIRegression-20260815-055704.log`；既有背包/HUD `ABTS.M5.UI.VisualLayout` 1/1 通过，日志：`Saved/Logs/SystemMenu-InventoryUIRegression-20260815-055749.log`。
- 既有 `ABTS.Audio.ReleaseAndMusicMapping` fresh NullRHI 1/1 成功；日志：`Saved/Logs/SystemSettings-AudioRegression-20260815-051131.log`。
- fresh DX11 `-RenderOffscreen` 捕获均记录 `Complete Success=1 Reason=None`：
  - Front：`Saved/ABTSVisualCaptures/SystemMenu/20260815-052440/Front.png`；
  - SettingsAudio：`Saved/ABTSVisualCaptures/SystemMenu/20260815-053045/SettingsAudio.png`；
  - SettingsVideo：`Saved/ABTSVisualCaptures/SystemMenu/20260815-053240/SettingsVideo.png`；
  - SettingsAccessibility：`Saved/ABTSVisualCaptures/SystemMenu/20260815-052951/SettingsAccessibility.png`；
  - Video Confirmation：`Saved/ABTSVisualCaptures/SystemMenu/20260815-054315/SettingsVideoConfirm.png`；
  - Reset Confirmation：`Saved/ABTSVisualCaptures/SystemMenu/20260815-054813/SettingsResetConfirm.png`。
- 1280×720 像素复核通过：标题和设置值无裁切，Footer 按钮不溢出，截角填充无直角底板，阶段 HUD 不穿过主面板，音量/Gamma 进度轨与对应数值一致。
- 离屏截图只证明布局和像素合成；首次进入、ESC 恢复、点击热区、实际可听音量及窗口模式切换仍需一次用户可见 PIE 验收。
