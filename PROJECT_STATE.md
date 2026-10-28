# PROJECT_STATE.md — состояние проекта Counter-Strike: HvH

## Текущий build
- Версия: 1.2.0 (CSHVH_VERSION)
- Бранч: `arena/3d7361b5-twre-346-523`
- BIN: `bin/cshvh_headless` (headless), `bin/cshvh` (normal)

## Состояние хостагов (заложников)
- **ГОТОВО.** `CHostage` (`src/game/shared/hostage.cpp/hpp`) — спавн, рендер, damage, поднятие.
- **Хостауги спавнятся ТОЛЬКО на картах хостейджей (cs_*).** Проверка в `ResetRound()`: `if (g_MapInfo.name.compare(0, 3, "cs_") != 0) return;`
- Спавнится 4 хостага на T-spawnах (`mp_hostages`=1, `mp_hostage_count`=4).
- Рендер проверен: хостага видно на экране (normal-texture работает).
- Урон: `FireBullet` (DMG_BULLET) и нож `SwingOrStab` (DMG_SLASH) — через `CHostage::OnTakeDamage`.
- Конвары: `mp_hostages`, `mp_hostage_count`, `hostage_debug`, `hostage_goto`, `cl_hostage_debug_flat`.

## Хостажные карты (скачаны)
- `content/css/cstrike/maps/cs_office.bsp` (9.8 MB)
- `content/css/cstrike/maps/cs_havana.bsp` (9.9 MB)
- `content/css/cstrike/maps/cs_italy.bsp` (8.7 MB)
- `content/css/cstrike/maps/cs_militia.bsp` (25.2 MB)
- `content/css/cstrike/maps/cs_assault.bsp` (10.8 MB)
- `content/css/cstrike/maps/cs_compound.bsp` (7.3 MB)
- Source: `api.github.com/repos/razenxc/css_content/contents/maps/`

## Состояние читов (имба)
- **Hostage ESP** — green box + label ("HOSTAGE"/"CARRY"/"RESCUED") для каждого заложника.
- **Rescue Zone** — blue marker на CT-spawn (зона доставки хостагов).
- **Sound ESP** — звуковой пинг (Cheat.SoundEsp) когда виден враг.
- Варианты в меню: Visuals → Hostages, Rescue zone, Sound ESP.
- Файлы: `src/cheat/visuals.cpp` (рендеринг), `src/cheat/menu.cpp` (UI), `src/cheat/cheat_internal.hpp` (struct).

## Состояние оружия
- R8 Revolver: CS:GO viewmodel (`v_pist_revolver.mdl`),不在手旁边.
- SCAR-20: CS:S `v_snip_sg550` viewmodel (CS:GO `c_` модель — dead end).

## Состояние контента
- `content/css/cstrike/` — CS:S модели/материалы/звук (из bouletmarc/css_content).
- `content/css/cstrike/maps/` — 6 хостажные карты + de_dust2.
- `content/extras/all` — R8 + SCAR-20 (ожидается авто-скачивание при первом запуске).

## Скрипты
- `bash build.sh headless` — компиляция.
- `bash run headless -frames 300 +map de_dust2` — запуск.
- `bash run -frames 300 -at 255 "hostage_goto" -shot screenshots/x.png` — скриншот.

## Важные ограничения
- Ответы на русском.
- Не создавать теги вручную.
- После каждой сессии — ссылка на новый релиз + релиз-ноты через `gh release edit vX.Y.XX --notes-file /tmp/release.txt`.
- Имзен: `cl_hostage_debug_flat` — magenta-flat рендер хостага (только debug).
