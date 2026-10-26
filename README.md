# Counter-Strike: HvH

Фанатский ремейк Counter-Strike на **C++17 + [raylib 6.2](https://github.com/raysan5/raylib)**: главное меню в стиле CS:GO
(агент, Play, Settings, браузер серверов), круговое меню закупки, движение как в Source, боты, бомба, экономика,
карты/модели/звуки Counter-Strike: Source и встроенное HvH-меню **aimwhere** по клавише **Insert**.
Код устроен как в Source SDK: `C_BaseEntity → C_BaseAnimating → C_BaseCombatCharacter → C_BasePlayer → CCSPlayer`,
`C_WeaponCSBase`, `CCSGameRules`, `CGameMovement`, `CLagCompensationManager`, `CUserCmd`, ConVar/ConCommand, игровые события.

> Проект не связан с Valve. В репозитории файлов Valve нет.
> **Карты, модели, текстуры и звуки CS:S игра скачивает сама при первом запуске** (экран загрузки, ~1.6 ГБ с GitHub),
> см. [«Контент при первом запуске»](#контент-при-первом-запуске). Встроенных «самодельных» карт и моделей больше нет.

## Запуск

**Готовые сборки — во вкладке [Releases](../../releases/latest):**
* `CounterStrikeHvH-windows.zip` — Windows (распаковать, запустить `cshvh.exe`);
* `cshvh.apk` — **Android** (Android 7.0+, arm64/armv7, OpenGL ES 3), управление с экрана, см. [«Android»](#android).

Релизы собирает GitHub Actions (`.github/workflows/release.yml`) при каждом пуше.

**Windows, без сборки:** скачайте репозиторий (Code → Download ZIP), распакуйте и запустите **`bin\cshvh.exe`**.
Это готовая сборка x64 (OpenGL 3.3). Папка `assets/` (шрифты) должна лежать рядом с `bin/`: игра ищет её в текущей папке,
рядом с exe и уровнем выше.

**Linux или своя сборка:** см. раздел «Сборка» ниже.

Параметры запуска: `-w 1920 -h 1080`, `-fullscreen`, `-windowed`, `+map de_dust2` (сразу загрузить карту),
`+любая_консольная_команда`, `-game <папка>`.

## Управление (как в CS)

| Клавиша | Действие |
|---|---|
| WASD, Space, Ctrl, Shift | движение, прыжок, присед, шаг |
| ЛКМ / ПКМ | огонь / прицел, второй режим |
| R, G, E | перезарядка, выбросить, использовать (разминирование) |
| 1–5, Q, колесо | оружие |
| B, M | круговое меню закупки (мышь или 1–9, ПКМ/0 — назад), выбор команды |
| Tab | таблица счёта |
| Y / U | чат / командный чат |
| F1 / F2 | autobuy / rebuy |
| F5 | скриншот (`screenshots/`) |
| **`~` (тильда)** | консоль |
| **Insert** | меню чита |
| Esc | главное меню |

Полезные консольные команды: `map de_dust2|de_inferno|cs_office…`, `maps`, `bot_add`, `bot_kick`, `bot_quota 9`,
`bot_hvh 1`, `bot_difficulty 0-3`, `mp_gamemode 1` (deathmatch), `mp_freezetime`, `mp_roundtime`, `mp_startmoney`,
`sv_cheats 1` → `noclip`, `god`, `give weapon_awp`, `impulse 101`, `setpos`, `endround`; `thirdperson`, `fov_desired`,
`sensitivity`, `bind`, `cl_crosshair*`, `viewmodel_offset_x/y/z`, `fps_max`, `mat_vsync`, `host_writeconfig`, `quit`.
Настройки сохраняются в `cfg/config.cfg`, свои команды можно писать в `cfg/autoexec.cfg`.

## Карты

Все стандартные карты CS:S: de_dust2, de_dust, de_inferno, de_nuke, de_train, de_aztec, de_cbble, de_chateau, de_piranesi,
de_port, de_prodigy, de_tides, cs_office, cs_italy, cs_assault, cs_compound, cs_havana, cs_militia и любые другие `.bsp`
из папки `maps/`. Если установлена CS 1.6 (`hl_content 1`), появляются и её карты (BSP v30).

## Главное меню

Как в CS:GO: сверху вкладки **PLAY / AGENTS / LOADOUT / SERVERS / консоль**, слева — выход и **настройки**, справа профиль.
* **Home** — агент (модель CS:S на фоне карты), кнопка **PLAY**, быстрые режимы.
* **Play** — плитки карт с превью, режим (Competitive / Deathmatch / HvH), все параметры матча и ботов, кнопка **GO**.
* **Agents** — выбор модели для CT (SEAL/GSG-9/SAS/GIGN) и T (Phoenix/Leet/Arctic/Guerilla) с портретами.
* **Loadout** (Снаряжение) — для CT и T отдельно выбирается, какой ствол стоит в «двойных» слотах закупки: M4A4 / M4A1-S,
  P2000 / USP-S (стартовый пистолет), Five-SeveN / CZ75, Tec-9 / CZ75, Desert Eagle / R8, MP7 / MP5-SD, MP9 / MAC-10,
  MAG-7 / Sawed-Off. Купить можно только выбранный. Хранится в `loadout_*` (например `loadout_ct_m4 m4a1_silencer`).
* **Servers** — браузер серверов.
* **Settings** — Game, Crosshair (с превью), HUD, Audio, Video, Mouse, Keyboard (переназначение всех клавиш), Touch, Content.
  Любой параметр также доступен в консоли. Из консоли можно открыть панель: `gameui_panel play|agents|loadout|servers|settings [вкладка]`.

## HUD и иконки

* Килфид как в CS:GO: силуэт оружия (из картинок CS:S), значки headshot, wallbang, сквозь дым, noscope, ослеплён;
  свои убийства/смерти обведены красным. `hud_testkillfeed` показывает пример.
* Рядом с патронами силуэт текущего оружия, у exe и окна есть иконка.
* Производительность: Settings → Video → «Skip tiny props» (`r_propscreencull`), «Fade distant props», decals, тени.
  `trace_selftest` проверяет ускоренную трассировку против полного перебора (должно быть 0 mismatches).

## Оружие

* Круговое меню закупки (B) как в CS:GO: шесть категорий (Pistols, Heavy, SMGs, Rifles, Gear, Grenades), в каждой не
  больше 6 позиций, отдельной категории снайперок нет (SSG 08, AWP и автоматы — в Rifles):
  * CT Rifles: FAMAS, M4A4/M4A1-S, SSG 08, AUG, AWP, SCAR-20; T Rifles: Galil AR, AK-47, SSG 08, SG 553, AWP, G3SG1;
  * Heavy: Nova, XM1014, MAG-7/Sawed-Off, M249, Negev; SMGs: MP9/MAC-10, MP7/MP5-SD, UMP-45, P90, PP-Bizon;
  * Pistols: P2000/USP-S (у T Glock-18), Dual Berettas, P250, Five-SeveN/CZ75 (у T Tec-9/CZ75), Desert Eagle/R8.
  Какой ствол из пары стоит в слоте, задаётся в главном меню → **Loadout**. У стволов, которых нет в CS:S, модель,
  звук и иконка берутся от ближайшего аналога.
* `mp_buy_anyteam 1` (по умолчанию): defuse kit покупают обе команды, а в Loadout можно поставить ствол другой команды
  (MP9/MAC-10, MAG-7/Sawed-Off). `0` — ограничения как в CS:GO.
* **R8 Revolver** работает как в CS:GO: ЛКМ взводит курок (~0.23 с) и стреляет, ПКМ — быстрая стрельба с бедра.
  Модель, анимации и звуки R8 из CS:GO скачиваются отдельным маленьким пакетом (~9 МБ, см. ниже). Руки на
  view-модели берутся от модели вашего агента. `cl_r8_model 0` возвращает модель Desert Eagle,
  `cl_r8_viewmodel_fov` (68) задаёт FOV view-модели R8.
* Модели игроков: наклон взгляда (pitch) двигает торс и голову, а не всю модель. Ноги стоят на земле.
  Оружие от третьего лица лежит в правой руке (крепится к кисти, а не к анимированной кости оружия).
* View-модели в правой руке. Модели CS:S сделаны под левую руку, поэтому они зеркалятся, как в оригинальной игре.
  `cl_righthand 0` (Settings → Viewmodel hand) переносит оружие в левую руку.

## Меню чита (Insert)

Меню в стиле Aimware: красная рамка, чёрная панель вкладок, светлые группы. Перетаскивается за красную рамку,
цвет рамки меняется в **Misc → Settings → Menu color** (или во вкладке **Colors**). Legitbot нет, только HvH.

* **Ragebot:** aimbot (выбор хитбоксов, multipoint, hitchance, минимальный урон и его override), autowall,
  silent aim, autoscope, quick stop, backtrack, double tap, hide shots, knifebot, prefer/force baim, safe point, резольвер,
  **auto revolver**: курок R8 всё время взведён (до предела, отпускается в последний тик), поэтому выстрел мгновенный.
* **Anti-aim:** pitch, yaw base, yaw/jitter/spin, body yaw (desync), freestanding, LBY, edge yaw, manual AA (стрелки),
  fake lag, slow walk, fake duck.
* **Visuals:** ESP (бокс, HP, имя, флаги, оружие, патроны, скелет), стрелки вне экрана, glow, chams (видимые, за стеной,
  local, fake, backtrack), radar reveal, удаление флешки/дыма/скоупа/отдачи, ночной режим, третье лицо, трассеры,
  траектория гранат, дым (можно убрать), spread circle, FOV и FOV viewmodel, hitmarker, урон, индикаторы, watermark, список биндов, спектаторы.
* **Misc:** bhop, autostrafe, edge jump, quick stop, hitsound, killsay, логи урона и покупок, buy bot.
  Autostrafe (Directional / Rage) разгоняется без ограничения скорости в воздухе: каждый тик максимальный толчок под 90°
  к скорости, курс держится точно по WASD, в том числе по диагонали. При касании земли скорость сбрасывается до
  скорости бега (`sv_bhop_reset 1`), и разгон начинается заново со следующего прыжка.
* **Dormancy:** враги, которых сервер не передаёт (далеко и вне PVS, за стенами), становятся «dormant». ESP рисует
  их серой полупрозрачной рамкой с последним известным оружием, ragebot по ним не стреляет. Близких врагов сервер
  передаёт всегда, поэтому эффект мягкий.
* **Colors:** все цвета ESP, chams, трассеров, индикаторов и самого меню на одной вкладке.
* **Players:** белый список, приоритет, force baim, override резольвера для отдельных игроков.
* **Settings:** сохранение и загрузка конфигов (`cfg/aimwhere_<имя>.cfg`), импорт и экспорт через буфер обмена.
  Из консоли: `aw_menu`, `aw_save <имя>`, `aw_load <имя>`.

Watermark, список биндов, индикаторы и спектаторы оформлены в стиле меню: тёмная плашка, красная полоса сверху.

**Double tap** работает как в HvH-читах. Когда DT включается (или после выстрела), игрок «заряжается»: примерно 15 тиков
стоит на месте, даже в воздухе. На выстреле или при отпускании бинда накопленные тики проигрываются разом, это
телепорт вперёд на 15 тиков движения. Предел задаёт `sv_maxdtshift` (по умолчанию 15).

Боты при `bot_hvh 1` тоже играют «HvH», и конфиги у них разные: одни на fake lag (12–14 тиков), другие с **double tap**
(заряжаются, на выстреле делают второй мгновенный выстрел), третьи с **hide shots** или со смешанным fake lag.
Anti-aim тоже разный: jitter, static, 3-way, sideways, разная сила desync. У ботов с R8 работает auto revolver.
Посмотреть конфиги: `bot_status`.

### Боты

* Командный план на раунд: экономика (eco / force / full buy, сброс оружия товарищу), роли (entry, lurker, AWP, rotator),
  выполнение на сайт с гранатами, ротации CT по информации.
* Прыгают (bhop со стрейфом на длинных участках), пикают углы (swing, jiggle, пик под свою флешку, AWP-анпик), отходят после контакта,
  сейвятся, если по реальному пути до бомбы не успевают разминировать.
* Навигация строится из BSP при загрузке карты, включая узкие проходы и двери не по сетке. Застрявший бот прыгает,
  приседает, уходит в сторону и обходит препятствие.
* Имена у ботов обычные человеческие, не названия читов.

## Контент при первом запуске

Если CS:S не найдена, при запуске открывается **экран загрузки**: игра скачивает zip-архивы с GitHub
(`codeload.github.com`, зеркала [bouletmarc/css_maps_content](https://github.com/bouletmarc/css_maps_content) — карты,
[bouletmarc/css_content](https://github.com/bouletmarc/css_content) — модели, материалы, звуки) и распаковывает их «на лету»
в `content/css/cstrike/` (на Android — `Android/data/com.cshvh.game/files/content/css/`). Всего ~1.6 ГБ, ~18 000 файлов.
* Видно прогресс по каждому архиву, скорость и текущий файл; загрузку можно отменить и продолжить позже
  (уже скачанные архивы не качаются заново).
* Повторить или докачать: консоль `content_download` или **Settings → Content**.
* Адреса архивов меняются переменными `cl_content_url_maps` / `cl_content_url_main` (любые zip с той же структурой).
* **Дополнительный пакет R8** (модели, текстуры и звуки R8 из CS:GO, 19 файлов, ~9 МБ) берётся из TTT-аддона
  [EntranceJew/gmod-addons](https://github.com/EntranceJew/gmod-addons) (закреплённый коммит) и кладётся в
  `content/extras/csgo_r8/`. Если raw.githubusercontent.com недоступен, файлы качаются через api.github.com.
  Этот пакет докачивается, даже если CS:S уже установлена. Если загрузка не удалась, игра всё равно запускается
  (кнопка SKIP), а у R8 остаётся модель Desert Eagle. Адреса: `cl_content_url_r8` / `cl_content_url_r8_api`.
* HTTP: на Windows — WinHTTP, на Android — `HttpURLConnection` через JNI, на Linux — `curl`.

## Контент Counter-Strike: Source

Альтернатива загрузке: Valve бесплатно раздаёт **Counter-Strike: Source Dedicated Server** (Steam app 232330,
около 2 ГБ) через SteamCMD с анонимным входом. В нём есть все карты, материалы, модели и звуки CS:S.
* в игре: консоль → **`css_download`**, по окончании `css_rescan` или перезапуск;
* или **`tools\get_css_content.bat`** (Windows) / `tools/get_css_content.sh` (Linux).

Контент ставится в `content/css/` (папка в `.gitignore`). Если CS:S уже установлена через Steam, она находится сама,
другую папку можно указать через `css_gamedir "D:/Games/Counter-Strike Source"`. `css_content 0` выключает CS:S.

**Что берётся из CS:S:**
* **карты** (VBSP v19–21: de_dust2, de_inferno, de_nuke, de_train, cs_office…): геометрия, дисплейсменты со смешиванием
  двух текстур, родные лайтмапы, небо, прозрачные и альфа-тестовые материалы, брашевые сущности;
* **коллизия** по брашам карты с плоскостями-бевелами, дисплейсменты, статические пропы по выпуклым оболочкам из `.phy`;
* **статические пропы** (ящики, бочки, машины, купола…) из `.mdl/.vvd/.vtx` с освещением по лайтмапе;
* спавны, зоны закупки и бомбы из сущностей, **радар** из `materials/overviews/<карта>.vtf`;
* **модели игроков** (ct_urban/gsg9/sas/gign, t_phoenix/leet/arctic/guerilla) с родными анимациями CS:S из
  `cs_player_shared.mdl`: 9-сторонний бег/ходьба/присед, хват под каждое оружие, наклон корпуса и прицел (body_yaw/body_pitch),
  выстрел, перезарядка, прыжок, смерть; оружие в руках — модели `w_*.mdl`; выброшенное оружие и гранаты тоже из CS:S;
* **модели от первого лица** `v_*.mdl` с руками CS:S и родными анимациями (достать, выстрел, перезарядка, нож, гранаты,
  глушитель USP/M4A1, C4); FOV — `cl_css_viewmodel_fov 54`;
* **звуки** через родные скрипты `scripts/game_sounds*.txt`: выстрелы, перезарядка, нож, гранаты, C4, шаги по материалам,
  попадания, радио, интерфейс. Для оружия из CS:GO берётся ближайший аналог (R8 → Deagle, P250 → P228, SSG08 → Scout…).

Файлы читаются прямо из VPK-архивов (`cstrike_pak_dir.vpk`), распаковывать ничего не нужно. **Синтезированных «бипов» больше нет:**
если нет ни CS:S, ни CS 1.6, звук просто не играет. Модели CS:S выключаются `cl_css_models 0` / `cl_css_viewmodels 0`
(тогда игроки рисуются упрощённо). Хитбоксы — родные хитбоксы CS:S (`r_drawhitboxes 1` показывает их).

## Android

`cshvh.apk` из [Releases](../../releases/latest): скачать на телефон, открыть, разрешить установку из неизвестных источников.

**Управление с экрана:**
* левая половина — **джойстик ходьбы** (появляется под пальцем, у края — бег);
* правая половина — **свайп для обзора**;
* **FIRE** (две кнопки: справа и слева; если вести пальцем по кнопке — одновременно прицеливаешься), **JUMP**, **DUCK**,
  **SCOPE** (прицел / второй режим), **R** (перезарядка), **USE**, **NEXT** (следующее оружие), **DROP**, **WALK**, слоты **1–5**;
* сверху: **MENU** (= Esc, также системная кнопка «Назад»), **BUY**, **TEAM**, **SCORE**, **AW** (меню aimwhere);
* в меню нажатия работают как мышь; у меню aimwhere есть кнопка закрытия ✕;
* **бинды на телефоне — кружки на экране:** в меню aimwhere тапните поле бинда (DT, hide shots, fake duck, manual AA…):
  `+ circle` → `circle: toggle` → `circle: hold` → выкл. На экране появится круг с подписью: тап включает/выключает
  (toggle) или работает, пока палец держит его (hold). Активный круг подсвечивается.

Настройки (консоль или `cfg/config.cfg`): `touch_sensitivity 1.0`, `touch_opacity 0.55`, `touch_scale 1.0`, `touch_invert 0`,
`touch_enable 0/1` (на ПК можно включить для проверки, `touch_mouse 1` — мышь вместо пальца).

**Контент CS:S на телефоне** скачивается сам при первом запуске (лучше по Wi-Fi, ~1.6 ГБ). Можно и вручную скопировать папку
`content/css` с ПК по USB в `Android/data/com.cshvh.game/files/content/css/`.
При первом запуске игра распаковывает свои файлы в `Android/data/com.cshvh.game/files/`, там же лежит `cfg/`.

## Оригинальный контент CS 1.6

Файлы Valve защищены авторским правом, поэтому в репозитории их нет и игра их не скачивает. Но если у тебя есть
Counter-Strike 1.6 (Steam или любая копия), игра берёт из неё **карты, текстуры, небо, радар и звуки**:

1. **Автоматически.** Проверяются стандартные папки Steam: `C:/Program Files (x86)/Steam/steamapps/common/Half-Life`,
   `C:/Program Files/...`, `D:/SteamLibrary/...`, на Linux `~/.steam/steam/...` и `~/.local/share/Steam/...`.
2. **Своя папка.** Укажи в консоли папку, где лежат `cstrike/` и `valve/` (значение сохраняется в конфиг):
   ```
   hl_gamedir "E:/Games/Half-Life"
   map de_dust2
   ```
   Подойдёт и путь прямо к `cstrike`. Проверить поиск можно командами `hl_path` и `hl_find maps/de_dust2.bsp`.
3. **Копия рядом с игрой.** Положи папки `cstrike/` (и по желанию `valve/`) рядом с `cshvh.exe`. Они в `.gitignore`.

**Карты (BSP v30).** Карта загружается целиком, как в GoldSrc:
* геометрия и текстуры из WAD-файлов, указанных в карте (`cs_dust.wad`, `halflife.wad`…), и встроенные текстуры;
* родные лайтмапы карты и небо `gfx/env/<sky>*.tga`;
* коллизия по клип-хуллам карты (как в CS 1.6, с clip-брашами), материалы поверхностей для шагов и попаданий берутся из `sound/materials.txt`;
* спавны, зоны закупки и бомбы берутся из сущностей карты;
* радар использует `overviews/<карта>.bmp`, а если его нет, рисуется по навигации;
* навигация для ботов строится автоматически при загрузке, `.nav` не нужен.

Двери рисуются открытыми и не мешают проходу. Стёкла (`func_breakable`) пока не разбиваются.
`r_lightmap_gamma` и `r_lightmap_scale` задают яркость лайтмапов, применяются при загрузке карты.

**Модели оружия от первого лица.** Берутся оригинальные `models/v_*.mdl` (MDL v10) вместе с руками и анимациями:
draw, idle, выстрелы, перезарядка, удары ножом, чека и бросок гранаты, надевание глушителя на M4A1 и USP.
Для оружия из CS:GO берётся аналог из 1.6, как и для звуков. Выключаются командой `cl_original_viewmodels 0`.

**Звуки.** Используются `weapons/*.wav` (выстрелы, перезарядка, нож, гранаты, C4), `player/pl_*.wav` (шаги), `radio/*.wav`,
звуки попаданий и интерфейса. У оружия из CS:GO, которого нет в 1.6, берётся ближайший аналог: R8 → deagle, P250 → p228,
SSG08 → scout, SCAR-20 → sg550, SG553 → sg552, MP9 → tmp, Nova → m3. Если какого-то файла нет, звук не играет (синтеза больше нет).
`snd_original 0` выключает звуки 1.6, `snd_css 0` звуки CS:S, `snd_restart` перечитывает их, `play Weapon_ak47.Single` проигрывает звук
для проверки. Можно и просто скопировать `cstrike/sound` в `./sound/` рядом с игрой.

## Сборка

Нужен компилятор C++17. raylib 6.2 скачивается автоматически.

**Visual Studio (проще всего):**
1. Установите Visual Studio 2022 или 2026 с нагрузкой **«Разработка классических приложений на C++»**.
2. Откройте **`CounterStrikeHvH.slnx`** (VS 2022 17.13+ / VS 2026) или **`CounterStrikeHvH.sln`** (любая VS 2019+).
3. Вверху выберите **Release | x64** и нажмите **Ctrl+Shift+B** (собрать) или **F5** (собрать и запустить).

Скачивать ничего не нужно: исходники raylib лежат в `third_party/raylib` и собираются как отдельный проект решения.
Готовый файл появляется в `bin\cshvh.exe` (в Debug — `bin\cshvh_debug.exe`, с консолью для логов).
Если добавили новые `.cpp`/`.hpp`, обновите проекты командой `python tools/gen_vs.py`.

**Windows через CMake (Visual Studio / MinGW / clang):**
```
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
bin\cshvh.exe
```

**Windows exe из Linux/macOS (zig):**
```
pip install ziglang      # или установите zig
./build.sh windows       # -> bin/cshvh.exe
```

**Linux:**
```
sudo apt install build-essential libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev
./build.sh               # -> bin/cshvh
```

**Android APK** (нужны Android SDK + NDK, переменные `ANDROID_SDK_ROOT` и `ANDROID_NDK_ROOT`):
```
android/build_apk.sh     # -> bin/cshvh.apk (NativeActivity, без Java)
```

`./build.sh headless` собирает версию с программным рендером и без окна. На ней проводятся автотесты, например:
`bin/cshvh_headless -frames 300 -shot shot.png +map de_dust2 +jointeam 3`.

## Структура

```
src/engine/       ConVar/консоль, математика (Vector/QAngle/matrix3x4_t), трассировка лучей и хуллов по брашам,
                  рендер мира, host/тики, download (загрузка контента с GitHub),
                  hlfs (файлы установленной CS 1.6), bsp/bsprender (карты GoldSrc: хуллы, лайтмапы, WAD, небо), mdl (модели MDL v10),
                  srcfs (CS:S: VPK, папки, pakfile), keyvalues, srcmaterial (VMT/VTF, DXT), srcbsp/srcbsprender (карты VBSP),
                  srcmdl (модели Source MDL v44–49 + VVD/VTX, кости, анимации)
src/game/shared/  C_BaseEntity, CCSPlayer, C_WeaponCSBase + все пушки, gamemovement (Source-движение),
                  CCSGameRules (раунды, экономика, бомба), гранаты, lag compensation, анимстейт (desync/LBY), события
src/game/server/  навигационная сетка и боты (CCSBot)
src/game/client/  ввод/бинды, HUD (как в CS), VGUI-меню закупки и команд, viewmodel, эффекты, звук (WAV из CS:S или CS 1.6)
src/vgui/         ISurface, контролы (Frame/Button/Slider/ComboBox/…), главное меню в стиле CS:GO (mainmenu), экран загрузки контента, консоль
src/cheat/        aimwhere: меню, ragebot, antiaim, visuals, misc
tools/            gen_vs.py (решение Visual Studio), simtest (симуляция матча без графики), get_css_content (SteamCMD)
msvc/             проекты Visual Studio (cshvh.vcxproj, raylib.vcxproj)
third_party/      raylib 6.2 (исходники, zlib-лицензия)
```

Мир работает с тикрейтом 64. Командование, предсказание, интерполяция и lag compensation устроены как в Source.
Сервер и клиент работают в одном процессе.
