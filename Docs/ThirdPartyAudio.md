# 第三方音频与关联素材清单及许可

> 状态：2026-08-14，首批 53 个音效、4 个音乐 SoundWave 与“也字工厂小石头”字体已导入 Unreal。
> 音效源素材暂存根目录：`C:\workspace\SoundEffects`；音乐许可证位于 `C:\workspace\Media\License.txt`，字体声明位于 `C:\workspace\也字工厂小石头字体使用声明.txt`。本文件是导入筛选表与发布许可记录，原始许可证/声明仍须保留在工作区外归档中。

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

16 RPG-like procedural generated music tracks — messersm
https://opengameart.org/content/16-rpg-like-procedural-generated-music-tracks
CC0 / Public Domain dedication

字体：也字工厂小石头
可免费商用
https://www.yefont.com/fontDetails/172
```

## 音乐与字体许可证原文登记

`C:\workspace\Media\License.txt` 内容：

```text
https://opengameart.org/content/16-rpg-like-procedural-generated-music-tracks

Author: messersm
Saturday, April 18, 2015 - 02:49
Art Type: Music
Tags: RPG procedural
License(s): CC0
```

对应已导入资产为 `/Game/Audio/Music/Bass`、`Harmony`、`Melody`、`Percussion`。四轨是用户基于该 CC0 来源所做的 MIDI 重编曲/混音；发布鸣谢建议同时保留原作者、来源和用户重编曲说明。

`C:\workspace\也字工厂小石头字体使用声明.txt` 内容：

```text
字体：也字工厂小石头
可免费商用
网址：https://www.yefont.com/fontDetails/172
```

对应已导入资产为 `/Game/Font/YeZiGongChangXiaoShiTou` 与 `/Game/Font/YeZiGongChangXiaoShiTou_Font`。该声明只明确“可免费商用”，未给出标准许可证名称，也未在本地声明中明确修改、再分发或单独转授权条款；项目只按游戏内嵌字体用途登记，不把声明扩展解释为其他权利，原始声明须随发布证据归档保留。

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
| `16 RPG-like procedural generated music tracks` | `C:\workspace\Media\License.txt` | messersm；<https://opengameart.org/content/16-rpg-like-procedural-generated-music-tracks> | `CC0` | 可商用；署名可选；项目仍保留来源与重编曲说明。 |
| `也字工厂小石头` | `C:\workspace\也字工厂小石头字体使用声明.txt` | 也字工厂；<https://www.yefont.com/fontDetails/172> | `可免费商用` | 仅按声明记录游戏内嵌使用；保留原始声明，不推定未写明的修改/再分发权。 |

## 当前导入清单与用途

截至本次核对，以下 53 个 SoundWave 已存在于 `Content\SoundEffects\`，其中包括 `Tiny_Hammer_on_Stone`。四轨音乐位于 `Content\Audio\Music\`。其余下载文件当前均不纳入游戏。

| UE 资产 / 待导入源文件 | 来源文件 | 本游戏中的唯一用途 | 播放规则 |
| --- | --- | --- | --- |
| `Looped_Rubber-y_Stretch` | `Looped Rubber-y Stretch.wav` | 弹弓拉伸循环。 | 拉弓时启用同一个循环组件；音高只由弹弓未拉动时的固有弦长决定且拉动中保持不变，拉得越用力音量越高；释放或取消时 150 ms 淡出；**不使用 tick 音**。CC BY 4.0。 |
| `Elastic_band_c_note` | `Elastic band c note.wav` | 弹弓释放的基础音调/共鸣。 | 在 `ReleaseLaunch()` 同帧播放；按弹弓未拉动时的固有弦长使用“短弦高、长弦低”的固定音高，本次拉力只控制响度。 |
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
| `Tiny_Hammer_on_Stone` | `Tiny Hammer on Stone.wav` | 工作台环境持续声。 | 低音量循环，仅在工作台附近或制作界面活动时播放；正式接线前确认循环接缝。CC0。 |

## 当前不使用的已下载文件

除上表资产外，`C:\workspace\SoundEffects` 内的其余下载音频暂不导入、不绑定玩法，也不列为首版候选。`Preview.ogg`、`.url`、`desktop.ini`、`.pkf` 和所有 `License.txt` 同样不导入游戏，但须继续保留在原始下载归档中。

## 导入前验收

- [x] `Tiny Hammer on Stone.wav` 已导入 `Content\SoundEffects\`；工作台环境 loop 属性与玩法接线仍待配置。
- [ ] 每个循环（拉伸、卫星、熔炉、工作台）在 Unreal 中试听并确认无明显爆音、过长静音或循环接缝。
- [ ] 木、石、玻璃和金属的 `light/medium/heavy` 均按被撞物体质量选择；撞击速度只控制响度/触发阈值。
- [ ] `Looped Rubber-y Stretch.wav` 在 Credits 中保留 CC BY 4.0 署名文本；如果不愿承担署名义务，则不要导入它，应改用 CC0 拉伸声或自制版本。
- [x] `Elastic band c note.wav` 已作为基础音高层导入，最终音高由弹弓固有弦长驱动，释放 `PullAlpha` 只控制响度。
- [ ] `select_002`、`switch7`、`rollover1` 和 `tick_001` 只在表内指定的 UI/安装事件触发。
- [ ] 通过 Music、SFX、UI、Ambience 四个独立音量滑杆测试静音与混音。
