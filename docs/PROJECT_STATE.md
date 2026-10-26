# Полная сводка для продолжения работы

> Снимок состояния проекта на момент сохранения (2026-10-26).
> Ветка, указанная ниже в тексте (`arena/01a0f92c-twre-346-523`), — историческая;
> текущая рабочая ветка: `arena/fb0ed122-twre-346-523`.

## Проект
**Counter-Strike HvH** — мод на базе Source SDK для CS:S, портативная версия для Windows/Android.

**Репозиторий:** `/home/user/twre-346-523`
**Ветка:** `arena/01a0f92c-twre-346-523` (всегда работать только на ней!)
**Коммит:** последний на ветке

## Сборка и релизы

### CI/CD
- GitHub Actions автоматически собирает при push в ветку
- 3 jobs: `windows` (cshvh.exe + CounterStrikeHvH-windows.zip), `android` (cshvh.apk), `android-test` (emulator smoke test)
- После успешной сборки автоматически создаётся релиз через `softprops/action-gh-release`
- **НЕ нужно вручную создавать теги и релизы** — CI делает это сам при push

### Как пушить
```bash
git add -A
git commit -m "описание"
git push origin arena/01a0f92c-twre-346-523
```
После push ждать ~5-6 минут CI, потом релиз готов.

### Описание релизов
Автоматически берётся из `.github/release_notes.md` через скрипт `tools/extract_notes.py`. Но пользователь просил **писать описание вручную** через `gh release edit v1.2.XX --notes-file /tmp/release.txt` после создания релиза.

### Текущая версия
v1.2.40 (последний релиз)

## Что сделано в этой сессии

### Loadout (src/vgui/mainmenu.cpp)
- Ширина панели: 45% экрана (было 65%)
- Слоты: 75px высота, 3 колонки × 5 слотов
- Slot 0 Pistols: показывает базовый пистолет (Glock/USP-S) с пометкой "DEFAULT", нельзя менять
- Drag-and-drop: зажал предмет в AVAILABLE → следует за курсором → отпустил над слотом = экипировался
- Уникальные ID для hover sound (не жужжит)
- Агент: стрелочки переключают (0-3), агент обновляется сразу

### Buy Menu (src/game/client/hud_menus.cpp)
- Радиальное (круговое) меню
- Тёмная тема (серые цвета, без синего)
- 5 категорий: PISTOLS, MID-TIER, RIFLES, EQUIPMENT, GRENADES
- **GetCategory показывает только оружия из Loadout** (через Loadout_GetSlots2), не все доступные
- HEAVY и SMGS обе маппятся в MidTier (P90 иконка)

### HUD (src/game/client/hud.cpp) — переделан под CS:GO (сессия 2026-10-26)
- Типографика: `surface()->DrawTextScaled*` / `HudText*` / `HudTextRight` / `HudTextCentered` (свободный размер шрифта)
- Палитра: `namespace hudgo` (Text/TextDim/TextFaint/Panel/BarTrack/CT/T/Danger/Gain/ArmorCol)
- Счёт сверху по центру плашкой: точки живых (CT/T), счёт, таймер; таймер краснеет на <10с,
  при заложенной бомбе — таймер C4 + иконка; под счётом насечки выигранных раундов
- **Счёт = выигранные раунды** (m_iNumCTWins/m_iNumTerroristWins), не киллы
- Слева снизу: деньги ($) + дельта, иконки buy-zone/бомбы/дефузера
- Слева снизу: HP и броня белыми цифрами + тонкие полосы (HudBar), HP<=25 краснеет
- Справа снизу: название оружия, силуэт (VGUI_WeaponIcon, иначе процедурный `DrawWeaponGlyph`),
  патроны `clip | reserve`; стрип гранат над оружием
- Вигнетка красным при низком HP (`DrawLowHpVignette`)
- Полоса закладки/дефуза — тонкий трек + заливка
- Экран смерти: чёрные плашки сверху/снизу, крупный ник/HP; деньги/HP/патроны поднимаются
  через `HudBottomInset(sc)`
- Радар увеличен (218*sc), круглый, A/B, точки игроков, стрелка
- Важно: `-at <frame> <cmd>` в CLI требует команду в одном argv, т.е. кавычить: `-at 20 "give ak47"`

### Иконки (src/vgui/vgui_images.cpp)
- WEAPON_REVOLVER использует deserteagle (SVG revolver нет)

## Что НЕ сделано / проблемы

1. ~~HUD не как в CS:GO~~ — сделано: HUD переписан в стиле CS:GO/CS2 (см. раздел HUD выше).

2. **Пользователь недоволен Buy Menu** — хочет только 3 раздела оружий (как в Loadout), Equipment и Grenades. Сейчас 5 категорий — возможно нужно убрать Equipment/Grenades из радиального меню или оставить только 3 оружия.

3. ~~Пользователь недоволен HUD~~ — закрыто этой сессией.

## Ключевые структуры

### LoadoutSlot2 (src/game/shared/cs_loadout.hpp)
```cpp
struct LoadoutSlot2 {
  int category;     // LOADOUT_PISTOLS / LOADOUT_MIDTIER / LOADOUT_RIFLES
  int slotIndex;    // 0..4
  int weaponId;     // currently equipped (0 = empty)
  int defaultWeapon; // fallback
  bool locked;      // slot 0 in PISTOLS is always Glock/USP
};
```

### CCSGameRules счёт раундов (src/game/shared/cs_gamerules.hpp)
```cpp
int m_iNumTerroristWins = 0;
int m_iNumCTWins = 0;
int m_iTotalRoundsPlayed = 0;
```

### LOADOUT категории
```cpp
enum { 
  LOADOUT_CAT_PISTOLS = 0, 
  LOADOUT_CAT_HEAVY,      // → MidTier
  LOADOUT_CAT_SMGS,       // → MidTier
  LOADOUT_CAT_RIFLES, 
  LOADOUT_CAT_GEAR,       // Equipment
  LOADOUT_CAT_GRENADES, 
  LOADOUT_NUM_CATEGORIES 
};
```

## Важные детали

- **Всегда работать на ветке `arena/01a0f92c-twre-346-523`**, никогда не переключаться
- **Отвечать на русском языке**
- **Всегда давать ссылки на скачивание релиза** в конце ответа
- **Писать описание релизов вручную** через `gh release edit`
- **Не создавать теги вручную** — CI делает это сам
- **DrawFilledRect** принимает (x, y, w, h, color) — 5 параметров
- **UTIL_GetPlayers()** для получения списка игроков (не Player::GetByIndex)
- **m_iKills** для киллов, **m_iNumCTWins/m_iNumTerroristWins** для раундов
- **In().mouseDown** для проверки зажата ли мышь (не MouseHeld)

## Следующий шаг
Пользователь просит сделать "нормальный HUD как в CS:GO". Нужно посмотреть scaleform-hud-cs2 и реализовать похожий HUD. Возможно также нужно уточнить про Buy Menu — сколько категорий оружий должно быть (3 или 5).
