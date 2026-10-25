# Counter-Strike: HvH

Фанатский ремейк Counter-Strike на **C++17 + [raylib 6.2](https://github.com/raysan5/raylib)**. В игре главное меню в стиле CS 1.6,
движение как в Source, боты, бомба, закупка, три карты и встроенное «HvH-читерское» меню по клавише **Insert**.
Код устроен как в Source SDK: `C_BaseEntity → C_BaseAnimating → C_BaseCombatCharacter → C_BasePlayer → CCSPlayer`,
`C_WeaponCSBase`, `CCSGameRules`, `CGameMovement`, `CLagCompensationManager`, `CUserCmd`, ConVar/ConCommand, игровые события.

> Проект не связан с Valve. Код, текстуры и звуки сделаны с нуля: текстуры и звуки генерируются процедурно.
> **Если у тебя установлена CS 1.6, игра запускает её оригинальные карты (BSP) и звуки** прямо из папки игры,
> см. раздел [«Оригинальный контент CS 1.6»](#оригинальный-контент-cs-16). В репозитории файлов Valve нет.
> Встроенные карты сделаны «по мотивам» классических раскладок. 3D-модели взяты из свободных CC0-паков, подробнее в [CREDITS.md](CREDITS.md).

## Запуск

**Windows, без сборки:** скачайте репозиторий (Code → Download ZIP), распакуйте и запустите **`bin\cshvh.exe`**.
Это готовая сборка x64 (OpenGL 3.3). Папка `assets/` должна лежать рядом с `bin/`: игра ищет её в текущей папке,
рядом с exe и уровнем выше.

**Linux или своя сборка:** см. раздел «Сборка» ниже.

Параметры запуска: `-w 1920 -h 1080`, `-fullscreen`, `-windowed`, `+map de_mirage` (сразу загрузить карту),
`+любая_консольная_команда`, `-game <папка>`.

## Управление (как в CS)

| Клавиша | Действие |
|---|---|
| WASD, Space, Ctrl, Shift | движение, прыжок, присед, шаг |
| ЛКМ / ПКМ | огонь / прицел, второй режим |
| R, G, E | перезарядка, выбросить, использовать (разминирование) |
| 1–5, Q, колесо | оружие |
| B, M | меню закупки, выбор команды |
| Tab | таблица счёта |
| Y / U | чат / командный чат |
| F1 / F2 | autobuy / rebuy |
| F5 | скриншот (`screenshots/`) |
| **`~` (тильда)** | консоль |
| **Insert** | меню чита |
| Esc | главное меню |

Полезные консольные команды: `map de_dust2|de_mirage|de_inferno`, `maps`, `bot_add`, `bot_kick`, `bot_quota 9`,
`bot_hvh 1`, `bot_difficulty 0-3`, `mp_gamemode 1` (deathmatch), `mp_freezetime`, `mp_roundtime`, `mp_startmoney`,
`sv_cheats 1` → `noclip`, `god`, `give weapon_awp`, `impulse 101`, `setpos`, `endround`; `thirdperson`, `fov_desired`,
`sensitivity`, `bind`, `cl_crosshair*`, `viewmodel_offset_x/y/z`, `fps_max`, `mat_vsync`, `host_writeconfig`, `quit`.
Настройки сохраняются в `cfg/config.cfg`, свои команды можно писать в `cfg/autoexec.cfg`.

## Карты

* **de_dust2:** Long A, Catwalk, Mid, Tunnels, B.
* **de_mirage:** A ramp, Palace, Jungle/Connector, Mid, B Apartments, Catwalk.
* **de_inferno:** Banana, B site с фонтаном, Arch, Library, Apartments, A site.

Если найдена CS 1.6, в списке карт появляются **все её карты** (`cstrike/maps/*.bsp`: de_dust2, de_inferno, de_nuke,
cs_office, de_aztec и любые скачанные). Оригинал заменяет встроенную карту с тем же именем. `hl_content 0` возвращает встроенные.

Встроенные карты описаны текстом (`assets/maps/*.map`) и генерируются скриптом `tools/mapgen.py`, его можно редактировать.
`python3 tools/mapgen.py --png` дополнительно рисует превью-схему каждой карты.

## Меню чита (Insert)

Меню в стиле Aimware: красная рамка, чёрная панель вкладок, светлые группы. Перетаскивается за красную рамку,
цвет рамки меняется в **Misc → Settings → Menu color** (или во вкладке **Colors**). Legitbot нет, только HvH.

* **Ragebot:** aimbot (выбор хитбоксов, multipoint, hitchance, минимальный урон и его override), autowall,
  silent aim, autoscope, quick stop, backtrack, double tap, hide shots, knifebot, prefer/force baim, safe point, резольвер.
* **Anti-aim:** pitch, yaw base, yaw/jitter/spin, body yaw (desync), freestanding, LBY, edge yaw, manual AA (стрелки),
  fake lag, slow walk, fake duck.
* **Visuals:** ESP (бокс, HP, имя, флаги, оружие, патроны, скелет), стрелки вне экрана, glow, chams (видимые, за стеной,
  local, fake, backtrack), radar reveal, удаление флешки/дыма/скоупа/отдачи, ночной режим, третье лицо, трассеры,
  траектория гранат, дым (можно убрать), spread circle, FOV и FOV viewmodel, hitmarker, урон, индикаторы, watermark, список биндов, спектаторы.
* **Misc:** bhop, autostrafe, edge jump, quick stop, hitsound, killsay, логи урона и покупок, buy bot.
* **Colors:** все цвета ESP, chams, трассеров, индикаторов и самого меню на одной вкладке.
* **Players:** белый список, приоритет, force baim, override резольвера для отдельных игроков.
* **Settings:** сохранение и загрузка конфигов (`cfg/aimwhere_<имя>.cfg`), импорт и экспорт через буфер обмена.
  Из консоли: `aw_menu`, `aw_save <имя>`, `aw_load <имя>`.

Watermark, список биндов, индикаторы и спектаторы оформлены в стиле меню: тёмная плашка, красная полоса сверху.

**Double tap** работает как в HvH-читах. Когда DT включается (или после выстрела), игрок «заряжается»: примерно 15 тиков
стоит на месте, даже в воздухе. На выстреле или при отпускании бинда накопленные тики проигрываются разом, это
телепорт вперёд на 15 тиков движения. Предел задаёт `sv_maxdtshift` (по умолчанию 15).

Боты при `bot_hvh 1` тоже играют «HvH»: используют desync, jitter и fake lag.

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

**Звуки.** Используются `weapons/*.wav` (выстрелы, перезарядка, нож, гранаты, C4), `player/pl_*.wav` (шаги), `radio/*.wav`,
звуки попаданий и интерфейса. У оружия из CS:GO, которого нет в 1.6, берётся ближайший аналог: R8 → deagle, P250 → p228,
SSG08 → scout, SCAR-20 → sg550, SG553 → sg552, MP9 → tmp, Nova → m3. Если какого-то файла нет, для него остаётся синтез.
`snd_original 0` выключает оригинальные звуки, `snd_restart` перечитывает их, `play Weapon_ak47.Single` проигрывает звук
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

`./build.sh headless` собирает версию с программным рендером и без окна. На ней проводятся автотесты, например:
`bin/cshvh_headless -frames 300 -shot shot.png +map de_mirage +jointeam 3`.

## Структура

```
src/engine/       ConVar/консоль, математика (Vector/QAngle/matrix3x4_t), трассировка лучей и хуллов по брашам,
                  процедурные материалы, рендер мира (запечённый свет), studio-модели (glTF, скелет, IK), host/тики,
                  hlfs (файлы установленной CS 1.6), bsp/bsprender (карты GoldSrc: хуллы, лайтмапы, WAD, небо)
src/game/shared/  C_BaseEntity, CCSPlayer, C_WeaponCSBase + все пушки, gamemovement (Source-движение),
                  CCSGameRules (раунды, экономика, бомба), гранаты, lag compensation, анимстейт (desync/LBY), события
src/game/server/  навигационная сетка и боты (CCSBot)
src/game/client/  ввод/бинды, HUD (как в CS), VGUI-меню закупки и команд, viewmodel, эффекты, звук (процедурный синтез или WAV из CS 1.6)
src/vgui/         ISurface, контролы (Frame/Button/Slider/ComboBox/…), главное меню CS 1.6, консоль
src/cheat/        aimwhere: меню, ragebot, antiaim, visuals, misc
tools/            mapgen.py, gen_vs.py (решение Visual Studio), simtest (симуляция матча без графики), worldtest, modeltest
msvc/             проекты Visual Studio (cshvh.vcxproj, raylib.vcxproj)
third_party/      raylib 6.2 (исходники, zlib-лицензия)
```

Мир работает с тикрейтом 64. Командование, предсказание, интерполяция и lag compensation устроены как в Source.
Сервер и клиент работают в одном процессе.
