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

Tiny Hammer on Stone — Shamewap
https://freesound.org/s/389692/
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
| `Tiny Hammer on Stone.wav` | `Tiny Hammer on Stone.txt` | Shamewap；<https://freesound.org/s/389692/> | `Creative Commons 0` | 可商用；不要求署名，仍在鸣谢中记录。 |
| `kenney_digital-audio`（63 个音频） | `kenney_digital-audio\License.txt` | Kenney Vleugels；<https://kenney.nl/> | `Creative Commons Zero, CC0` | 可商用；署名可选。 |
| `kenney_impact-sounds`（130 个音频） | `kenney_impact-sounds\License.txt` | Kenney；<https://www.kenney.nl/assets/impact-sounds> | `Creative Commons Zero, CC0` | 可商用；署名可选。 |
| `kenney_interface-sounds`（100 个音频） | `kenney_interface-sounds\License.txt` | Kenney；<https://www.kenney.nl/assets/interface-sounds> | `Creative Commons Zero, CC0` | 可商用；署名可选。 |
| `kenney_sci-fi-sounds`（73 个音频） | `kenney_sci-fi-sounds\License.txt` | Kenney；<https://kenney.nl/assets/sci-fi-sounds> | `Creative Commons Zero, CC0` | 可商用；署名可选。 |
| `kenney_ui-audio`（52 个音频） | `kenney_ui-audio\License.txt` | Kenney Vleugels；<https://kenney.nl/assets/ui-audio> | `Creative Commons Zero, CC0` | 可商用；署名可选。 |
| `Owlish Media Sound Effects`（161 个音频） | `Owlish Media Sound Effects\License.txt` | OwlishMedia；<https://opengameart.org/content/sound-effects-pack> | `CC0` | 可商用；署名可选。 |

## 当前导入清单与用途

截至本次核对，以下 52 个 SoundWave 已存在于 `Content\SoundEffects\`；`Tiny Hammer on Stone.wav` 已有来源文件和许可证，但仍待通过 Content Browser 导入。其余下载的文件当前均不纳入游戏。

| UE 资产 / 待导入源文件 | 来源文件 | 本游戏中的唯一用途 | 播放规则 |
| --- | --- | --- | --- |
| `Looped_Rubber-y_Stretch` | `Looped Rubber-y Stretch.wav` | 弹弓拉伸循环。 | `PullAlpha` 发生变化时启用同一个循环组件；以拉力调整音高/滤波，释放或取消时 150 ms 淡出；**不使用 tick 音**。CC BY 4.0。 |
| `Elastic_band_c_note` | `Elastic band c note.wav` | 弹弓释放的基础音调/共鸣。 | 在 `ReleaseLaunch()` 同帧播放；以释放时的原始拉伸长度设定音高。 |
| `pluck_001` | `kenney_interface-sounds\Audio\pluck_001.ogg` | 弹弓释放的短促 Snap 瞬态。 | 与 `Elastic_band_c_note` 同帧叠加，不循环。 |
| `footstep_grass_000–002` | `kenney_impact-sounds\Audio\footstep_grass_000–002.ogg` | 草地、软土地表移动。 | 主控鸟落脚时随机选一个；跟随鸟不重复播放。 |
| `footstep_wood_000–002` | `kenney_impact-sounds\Audio\footstep_wood_000–002.ogg` | 木桥、木质建筑/工作台地表移动。 | 主控鸟落脚时随机选一个。 |
| `impactWood_light_000–002` | `kenney_impact-sounds\Audio\impactWood_light_000–002.ogg` | 轻质木结构的撞击。 | 这里的 `light` 表示**被撞物体较轻**，不是撞击力度；由物体材质/质量选择。 |
| `impactWood_medium_000–002` | `kenney_impact-sounds\Audio\impactWood_medium_000–002.ogg` | 中等质量木结构的撞击。 | `medium` 表示被撞物体中等质量、音色更低沉。 |
| `impactWood_heavy_000–002` | `kenney_impact-sounds\Audio\impactWood_heavy_000–002.ogg` | 重型木结构的撞击和倒塌。 | `heavy` 表示被撞物体较重、音色更低沉；仍以法向速度控制音量，不把档位当作力度档位。 |
| `impactPlank_medium_000–002` | `kenney_impact-sounds\Audio\impactPlank_medium_000–002.ogg` | **石质物体的撞击**。 | 石材统一复用本组；随机变体，音量按法向撞击速度缩放。 |
| `impactGlass_light_000–002`、`medium_000–002`、`heavy_000–002` | `kenney_impact-sounds\Audio\impactGlass_*.ogg` | 轻/中/重玻璃物体的撞击。 | 后缀表示物体质量与音色低沉程度；玻璃破坏连锁限声。 |
| `impactMetal_light_000–002`、`medium_000–002`、`heavy_000–002` | `kenney_impact-sounds\Audio\impactMetal_*.ogg` | 轻/中/重金属物体、活塞与强化组件的撞击。 | 后缀表示物体质量与音色低沉程度；法向速度仅控制响度/是否触发。 |
| `explosionCrunch_000` | `kenney_sci-fi-sounds\Audio\explosionCrunch_000.ogg` | 黑鸟、爆炸桶和大型结构连锁的主体爆炸。 | 3D 衰减；同帧多次爆炸限声。 |
| `lowFrequency_explosion_000` | `kenney_sci-fi-sounds\Audio\lowFrequency_explosion_000.ogg` | 大爆炸的低频尾部。 | 仅主镜头附近叠加到主体爆炸。 |
| `open_001` / `close_001` | `kenney_interface-sounds\Audio\open_001.ogg` / `close_001.ogg` | 打开/关闭背包与制作界面。 | 2D UI 单发。 |
| `select_002` | `kenney_interface-sounds\Audio\select_002.ogg` | 所有普通 UI 选择。 | 固定使用此单一选择音：物品格、配方、鸟头像和一般按钮。 |
| `confirmation_001` | `kenney_interface-sounds\Audio\confirmation_001.ogg` | 制作成功、关键拾取成功。 | 与普通选择声不重复叠放。 |
| `error_003` | `kenney_interface-sounds\Audio\error_003.ogg` | 材料不足、非法放置、不可安装组件。 | 2D UI，保持短促克制。 |
| `tick_001` | `kenney_interface-sounds\Audio\tick_001.ogg` | 背包滚动。 | 仅用于背包/列表的滚动步进；**不用于弹弓蓄力**。 |
| `switch7` | `kenney_ui-audio\Audio\switch7.ogg` | 物品栏切换。 | 每次有效快捷栏/手持物切换播放一次。 |
| `rollover1` | `kenney_ui-audio\Audio\rollover1.ogg` | 弹弓桩、弹弓弦放置成功。 | 仅在安装事务成功提交后播放一次；不用于悬停。 |
| `spaceEngineLow_001` | `kenney_sci-fi-sounds\Audio\spaceEngineLow_001.ogg` | 卫星附近的低频空间环境循环。 | 3D loop，按与卫星距离淡入淡出；先确认循环接缝。 |
| `forceField_001` | `kenney_sci-fi-sounds\Audio\forceField_001.ogg` | 空间弹道预测或实际发射时受到引力偏转。 | 仅在进入/显著改变引力偏转阶段时触发，不能每帧重复。 |
| `thrusterFire_000` | `kenney_sci-fi-sounds\Audio\thrusterFire_000.ogg` | 终局太空发射；熔炉环境持续声。 | 终局为 3D 发射声；熔炉为低音量循环，二者分开配置衰减/音量。 |
| **待导入：`Tiny Hammer on Stone.wav`** | `Tiny Hammer on Stone.wav` | 工作台环境持续声。 | 导入后命名 `AMB_Workbench_TinyHammerOnStone`；低音量循环，仅在工作台附近或制作界面活动时播放。CC0。 |

## 当前不使用的已下载文件

除上表资产及待导入的 `Tiny Hammer on Stone.wav` 外，`C:\workspace\SoundEffects` 内的其余下载音频暂不导入、不绑定玩法，也不列为首版候选。`Preview.ogg`、`.url`、`desktop.ini`、`.pkf` 和所有 `License.txt` 同样不导入游戏，但须继续保留在原始下载归档中。

## 导入前验收

- [ ] `Tiny Hammer on Stone.wav` 已导入 `Content\SoundEffects\` 并创建工作台环境 SoundWave / loop 配置。
- [ ] 每个循环（拉伸、卫星、熔炉、工作台）在 Unreal 中试听并确认无明显爆音、过长静音或循环接缝。
- [ ] 木、石、玻璃和金属的 `light/medium/heavy` 均按被撞物体质量选择；撞击速度只控制响度/触发阈值。
- [ ] `Looped Rubber-y Stretch.wav` 在 Credits 中保留 CC BY 4.0 署名文本；如果不愿承担署名义务，则不要导入它，应改用 CC0 拉伸声或自制版本。
- [ ] `Elastic band c note.wav` 作为基础音高层导入，最终音高由释放时 `PullAlpha` / 拉伸长度驱动。
- [ ] `select_002`、`switch7`、`rollover1` 和 `tick_001` 只在表内指定的 UI/安装事件触发。
- [ ] 通过 Music、SFX、UI、Ambience 四个独立音量滑杆测试静音与混音。
