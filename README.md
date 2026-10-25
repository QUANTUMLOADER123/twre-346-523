# Counter-Strike: HvH

Фанатский ремейк Counter-Strike на **C++17 + [raylib 6.2](https://github.com/raysan5/raylib)**. В игре главное меню в стиле CS 1.6,
движение как в Source, боты, бомба, закупка, три карты и встроенное «HvH-читерское» меню по клавише **Insert**.
Код устроен как в Source SDK: `C_BaseEntity → C_BaseAnimating → C_BaseCombatCharacter → C_BasePlayer → CCSPlayer`,
`C_WeaponCSBase`, `CCSGameRules`, `CGameMovement`, `CLagCompensationManager`, `CUserCmd`, ConVar/ConCommand, игровые события.

> Проект не связан с Valve. Код, текстуры и звуки сделаны с нуля: текстуры и звуки генерируются процедурно.
> Карты сделаны «по мотивам» классических раскладок, это не копии. 3D-модели взяты из свободных CC0-паков, подробнее в [CREDITS.md](CREDITS.md).

## Запуск

1. Соберите игру (см. ниже). Для Windows собранный `cshvh.exe` можно положить в папку репозитория или в `bin/`.
2. Запустите `bin/cshvh.exe` (Windows) или `bin/cshvh` (Linux). Рядом должна лежать папка `assets/`: игра ищет её
   в текущей папке, рядом с exe и уровнем выше.

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

Карты описаны текстом (`assets/maps/*.map`) и генерируются скриптом `tools/mapgen.py`, его можно редактировать.
`python3 tools/mapgen.py --png` дополнительно рисует превью-схему каждой карты.

## Меню чита (Insert)

* **Rage:** aimbot (выбор хитбоксов, multipoint, hitchance, минимальный урон и его override), autowall,
  silent aim, autoscope, quick stop, backtrack, double tap, hide shots, knifebot, prefer/force baim, safe point, резольвер.
* **Anti-aim:** pitch, yaw base, yaw/jitter/spin, body yaw (desync), freestanding, LBY, edge yaw, manual AA (стрелки),
  fake lag, slow walk, fake duck.
* **Visuals:** ESP (бокс, HP, имя, флаги, оружие, патроны, скелет), стрелки вне экрана, glow, chams (видимые, за стеной,
  local, fake, backtrack), radar reveal, удаление флешки/дыма/скоупа/отдачи, ночной режим, третье лицо, трассеры,
  траектория гранат, spread circle, FOV и FOV viewmodel, hitmarker, урон, индикаторы, watermark, список биндов, спектаторы.
* **Misc:** bhop, autostrafe, edge jump, quick stop, hitsound, killsay, логи урона и покупок, buy bot.
* **Players:** белый список, приоритет, force baim, override резольвера для отдельных игроков.
* **Config:** сохранение и загрузка конфигов (`cfg/hvhsense_<имя>.cfg`), импорт и экспорт через буфер обмена.

Боты при `bot_hvh 1` тоже играют «HvH»: используют desync, jitter и fake lag.

## Сборка

Нужен компилятор C++17. raylib 6.2 скачивается автоматически.

**Windows (рекомендуется CMake + Visual Studio 2022 / MinGW / clang):**
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
                  процедурные материалы, рендер мира (запечённый свет), studio-модели (glTF, скелет, IK), host/тики
src/game/shared/  C_BaseEntity, CCSPlayer, C_WeaponCSBase + все пушки, gamemovement (Source-движение),
                  CCSGameRules (раунды, экономика, бомба), гранаты, lag compensation, анимстейт (desync/LBY), события
src/game/server/  навигационная сетка и боты (CCSBot)
src/game/client/  ввод/бинды, HUD (как в CS), VGUI-меню закупки и команд, viewmodel, эффекты, звук (процедурный синтез)
src/vgui/         ISurface, контролы (Frame/Button/Slider/ComboBox/…), главное меню CS 1.6, консоль
src/cheat/        hvhsense: меню, ragebot, antiaim, visuals, misc
tools/            mapgen.py, simtest (симуляция матча без графики), worldtest, modeltest
```

Мир работает с тикрейтом 64. Командование, предсказание, интерполяция и lag compensation устроены как в Source.
Сервер и клиент работают в одном процессе.
