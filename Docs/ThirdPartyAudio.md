# 第三方音频素材清单与许可

> 状态：已下载、尚未导入 Unreal。  
> 素材暂存根目录：`C:\workspace\SoundEffects`。此文件是导入筛选表与发布时的音频许可记录；只有标为“建议导入”的文件才应进入 `Content/SoundEffects/`，未列入的下载文件保留在原始归档中，不默认进入游戏包。

## 使用与归档规则

- 每次从暂存目录导入时，在 Unreal 中保留此处的源文件名，并以 `SFX_` / `UI_` / `AMB_` 前缀建立清楚的资产名；不要覆盖原始文件。
- 每个第三方包的 `License.txt` 连同下载包一起保留在工作区外的原始归档中；本文件记录来源、作者、许可和实际用途。
- 所有 CC0 素材均可用于商业发行，署名不是强制条件；项目仍在鸣谢中保留来源，便于追溯。
- `Looped Rubber-y Stretch.wav` 是本清单唯一的 CC BY 4.0 素材。它可商业使用和修改，但发行版本的 Credits / 第三方许可页必须保留其作者、标题、来源 URL、`CC BY 4.0` 和许可证 URL。
- 导入前在 Unreal 的 SoundWave 预听并确认用途。这里的文件名匹配是首选候选集，不表示未试听即强制使用全部文件。

## 发布时必须包含的鸣谢

```text
Looped Rubber-y Stretch — walllable
https://freesound.org/s/631739/
Licensed under Creative Commons Attribution 4.0 International (CC BY 4.0)
https://creativecommons.org/licenses/by/4.0/

Elastic band c note — mudflea2
https://freesound.org/s/708182/
CC0 / Public Domain dedication

Kenney audio assets — Kenney Vleugels, https://kenney.nl/
CC0 / Public Domain dedication

Owlish Media Sound Effects — OwlishMedia
https://opengameart.org/content/sound-effects-pack
CC0 / Public Domain dedication
```

## 素材来源与许可证登记

| 来源包 / 文件 | 本地许可证证据 | 作者 / 来源 | 许可证字段 | 许可处理 |
| --- | --- | --- | --- | --- |
| `Elastic band c note.wav` | `Elastic band c note.txt` | mudflea2；<https://freesound.org/s/708182/> | `Creative Commons 0` | 可商用；不要求署名，仍在鸣谢中记录。 |
| `Looped Rubber-y Stretch.wav` | `Looped Rubber-y Stretch.txt` | walllable；<https://freesound.org/s/631739/> | `Attribution 4.0` | 可商用；**必须署名**，按上方固定文案保留。 |
| `kenney_digital-audio`（63 个音频） | `kenney_digital-audio\License.txt` | Kenney Vleugels；<https://kenney.nl/> | `Creative Commons Zero, CC0` | 可商用；署名可选。 |
| `kenney_impact-sounds`（130 个音频） | `kenney_impact-sounds\License.txt` | Kenney；<https://www.kenney.nl/assets/impact-sounds> | `Creative Commons Zero, CC0` | 可商用；署名可选。 |
| `kenney_interface-sounds`（100 个音频） | `kenney_interface-sounds\License.txt` | Kenney；<https://www.kenney.nl/assets/interface-sounds> | `Creative Commons Zero, CC0` | 可商用；署名可选。 |
| `kenney_sci-fi-sounds`（73 个音频） | `kenney_sci-fi-sounds\License.txt` | Kenney；<https://kenney.nl/assets/sci-fi-sounds> | `Creative Commons Zero, CC0` | 可商用；署名可选。 |
| `kenney_ui-audio`（52 个音频） | `kenney_ui-audio\License.txt` | Kenney Vleugels；<https://kenney.nl/assets/ui-audio> | `Creative Commons Zero, CC0` | 可商用；署名可选。 |
| `Owlish Media Sound Effects`（161 个音频） | `Owlish Media Sound Effects\License.txt` | OwlishMedia；<https://opengameart.org/content/sound-effects-pack> | `CC0` | 可商用；署名可选。 |

## 建议导入清单与游戏用途

### 弹弓、飞行与爆炸

| 建议导入的原始文件 | 游戏用途 | Unreal 资产 / 播放建议 |
| --- | --- | --- |
| `Looped Rubber-y Stretch.wav` | 拉住弓兜且 `PullAlpha` 持续变化时的低音量橡胶拉伸循环。 | `SFX_Slingshot_PullLoop`；仅创建一个 Audio Component，按拉力更新音高/滤波，松手后 150 ms 淡出。CC BY 4.0。 |
| `Elastic band c note.wav` | 释放弹弓的有音高基础共鸣层。 | `SFX_Slingshot_Release_Resonance_C`；在释放帧以拉伸长度设置 pitch，搭配独立短瞬态。CC0。 |
| `kenney_interface-sounds\Audio\pluck_001.ogg`、`pluck_002.ogg` | 释放瞬态（Snap）的试听候选。 | 不做循环；与基础共鸣同帧触发，较共鸣更短、更干。 |
| `kenney_sci-fi-sounds\Audio\explosionCrunch_000–004.ogg` | 黑鸟爆炸、爆炸桶和大型结构连锁。 | 按距离衰减；对同一帧多次爆炸限声。 |
| `kenney_sci-fi-sounds\Audio\lowFrequency_explosion_000–001.ogg` | 黑鸟爆炸的低频尾部，可选层。 | 仅近距离或主镜头时叠加，避免小型撞击播放。 |
| `kenney_sci-fi-sounds\Audio\thrusterFire_000–004.ogg` | 终局太空发射和短暂高速飞掠。 | 仅 Space/Finale 档使用；不要替代常规鸟的风切声。 |

### 撞击、建筑材质与地表移动

| 建议导入的原始文件 | 游戏用途 | Unreal 资产 / 播放建议 |
| --- | --- | --- |
| `kenney_impact-sounds\Audio\footstep_grass_000–004.ogg` | 行星草地/软土移动。 | `SFX_Footstep_Grass` Random；当前主控鸟且着地时播放。 |
| `kenney_impact-sounds\Audio\footstep_wood_000–004.ogg` | 桥面、木质建筑和工作台附近移动。 | `SFX_Footstep_Wood` Random。 |
| `kenney_impact-sounds\Audio\impactWood_light/medium/heavy_000–004.ogg` | 木材受击、黄鸟穿透、木结构断裂。 | 按 M6 法向撞击速度选择 light / medium / heavy。 |
| `kenney_impact-sounds\Audio\impactGlass_light/medium/heavy_000–004.ogg` | 玻璃建筑的受击与碎裂。 | 重击时可叠加一个碎裂尾声；避免对每片碎块重复播放。 |
| `kenney_impact-sounds\Audio\impactMetal_light/medium/heavy_000–004.ogg` | 铁质建筑、活塞和强化弹弓周边受击。 | 按撞击速度选择；heavy 给予较低优先级以防轰鸣堆积。 |
| `kenney_impact-sounds\Audio\impactMining_000–004.ogg` | 石料拾取、石质建筑重击的试听候选。 | 当前包没有显式 stone 名称；先试听这组，不合适则使用 `Owlish…\Impacts\clamour*.wav` 作为备选。 |
| `kenney_impact-sounds\Audio\impactPlank_medium_000–004.ogg` | 桥梁建成、木板落位。 | 一次建桥只触发一次落位声。 |
| `Owlish Media Sound Effects\Footsteps\grass_footsteps.wav`、`grassy-footstep2–4.wav` | 更自然、非卡通的草地脚步备选。 | 与 Kenney 草地脚步 A/B 试听后仅保留一套风格。CC0。 |

### UI、库存、制作与桥梁

| 建议导入的原始文件 | 游戏用途 | Unreal 资产 / 播放建议 |
| --- | --- | --- |
| `kenney_interface-sounds\Audio\open_001–004.ogg`、`close_001–004.ogg` | 打开/关闭背包、制作界面和侦察界面。 | 2D UI 声音。 |
| `kenney_interface-sounds\Audio\select_001–008.ogg`、`click_001–005.ogg` | 物品格、鸟头像、配方和选项的普通选择。 | 每次确定点击只播放一个随机变体。 |
| `kenney_interface-sounds\Audio\confirmation_001–004.ogg` | 制作成功、桥梁成功放置、拾取关键资源。 | 成功后单发；不要与 UI click 重叠。 |
| `kenney_interface-sounds\Audio\error_001–008.ogg` | 材料不足、非法桥位、不可安装的弹弓部件。 | 与成功音色明显区分，音量保持克制。 |
| `kenney_interface-sounds\Audio\tick_001–002.ogg` | 背包滚动、弹弓蓄力分档 tick 的试听候选。 | 弹弓仅在滚轮改变蓄力档时触发，鼠标持续拖动不触发。 |
| `kenney_ui-audio\Audio\rollover1–6.ogg` | HUD 悬停。 | 只在控件首次进入时播放；禁止每帧重复。 |
| `kenney_ui-audio\Audio\switch1–38.ogg` | 物品栏切换、鸟切换的试听候选。 | 先试听选出 2–4 个统一风格变体。 |

### 卫星、侦察与终局空间感

| 建议导入的原始文件 | 游戏用途 | Unreal 资产 / 播放建议 |
| --- | --- | --- |
| `kenney_sci-fi-sounds\Audio\spaceEngineLow_000–004.ogg` | 卫星附近低频空间环境层。 | 3D 循环候选；必须先确认音频可无缝循环，否则只作短提示。 |
| `kenney_sci-fi-sounds\Audio\forceField_000–004.ogg` | 卫星引力走廊、预测弹道或空间弹弓能量提示。 | 短提示，避免误作真实碰撞音。 |
| `kenney_sci-fi-sounds\Audio\computerNoise_000–003.ogg` | 青鸟侦察扫描、小地图刷新和科技设施环境。 | 扫描开始/结束提示；不持续覆盖音乐。 |
| `kenney_digital-audio\Audio\phaseJump1–5.ogg` | 侦察发现、卫星练习的轨道变化提示。 | 用于非战斗科技反馈，需试听后确认不会像传送。 |
| `kenney_digital-audio\Audio\powerUp1–12.ogg` | 强化弹弓解锁、关键制作完成、终局蓄能。 | 只在里程碑使用；选 1–2 个建立一致的“强化”语汇。 |
| `kenney_digital-audio\Audio\highUp.ogg`、`lowDown.ogg` | 菜单升降、成功/撤销、HUD 轻提示。 | 2D UI；不要与主要 UI 包混用过多。 |
| `Owlish Media Sound Effects\Scifi\blackhole*.wav`、`robotics*.wav` | 卫星/终局气氛的 CC0 试听备选。 | 首版不要把它们直接设为循环；优先用于稀疏环境点缀。 |

## 暂不建议导入

- `laser*`、`laserLarge*`、`laserSmall*`、`laserRetro*`：本游戏当前没有激光武器，导入会扩大素材库但无直接玩法归属。
- Kenney 的 carpet、snow、concrete 脚步：当前玩法的球面草地、木桥和建筑已经有对应候选。
- Owlish 的真人语音、咳嗽、尖叫、电话、铃声、煮水、纸张等：与可爱鸟类、无对话和小行星题材的风格不稳定。
- 所有 `Preview.ogg`、`.url`、`desktop.ini`、`.pkf` 和每个包的 `License.txt`：这些不是游戏内音频资产；保留在原始下载目录即可。

## 导入前验收

- [ ] 每个拟导入文件在 Unreal 中试听并确认无明显爆音、过长静音或循环接缝。
- [ ] 每个随机组至少保留 3 个可分辨变体，且音量统一；不要把整个下载包无差别导入。
- [ ] `Looped Rubber-y Stretch.wav` 在 Credits 中保留 CC BY 4.0 署名文本；如果不愿承担署名义务，则不要导入它，应改用 CC0 拉伸声或自制版本。
- [ ] `Elastic band c note.wav` 作为基础音高层导入，最终音高由释放时 `PullAlpha` / 拉伸长度驱动。
- [ ] 通过 Music、SFX、UI、Ambience 四个独立音量滑杆测试静音与混音。
