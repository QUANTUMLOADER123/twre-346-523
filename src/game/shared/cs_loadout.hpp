// cs_loadout.hpp - CS2-style loadout: agent left, 3 columns × 5 slots, unequipped right
#pragma once
#include <vector>

// Legacy compatibility - old category constants still used by hud_menus.cpp
enum { LOADOUT_CAT_PISTOLS = 0, LOADOUT_CAT_HEAVY, LOADOUT_CAT_SMGS, LOADOUT_CAT_RIFLES, LOADOUT_CAT_GEAR, LOADOUT_CAT_GRENADES, LOADOUT_NUM_CATEGORIES };

// Categories for the new loadout
enum { LOADOUT_PISTOLS = 0, LOADOUT_MIDTIER, LOADOUT_RIFLES, LOADOUT_NUM_NEW };

struct LoadoutSlot2 {
  int category;     // LOADOUT_PISTOLS / MIDTIER / RIFLES
  int slotIndex;    // 0..4
  int weaponId;     // currently equipped (0 = empty)
  int defaultWeapon; // fallback
  bool locked;      // slot 0 in PISTOLS is always Glock/USP
};

// Get all 15 slots (3 cats × 5) for a team
std::vector<LoadoutSlot2> Loadout_GetSlots2(int team);

// Set weapon for a slot
void Loadout_SetWeapon2(int team, int category, int slotIndex, int weaponId);

// Get all weapons available for a category (for "unequipped" list)
std::vector<int> Loadout_CategoryWeapons2(int category, int team);

// Check if weapon is equipped
bool Loadout_IsEquipped2(int team, int weaponId);

// Legacy compatibility
const char* Loadout_CategoryName(int category);
int Loadout_StartPistol(int team, bool bot);
std::vector<int> Loadout_CategoryWeapons(int category, int team);
