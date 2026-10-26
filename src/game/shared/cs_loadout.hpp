// cs_loadout.hpp - CS:GO style loadout: some buy menu slots hold one of two guns (M4A4 / M4A1-S, P2000 / USP-S, ...)
// The choice is made per team in the main menu (LOADOUT) and stored in archived convars (loadout_*).
#pragma once
#include <vector>

struct LoadoutSlot {
  const char* cvar;      // convar holding the chosen weapon alias
  const char* title;     // slot name shown in the loadout screen
  int team;              // TEAM_CT / TEAM_TERRORIST
  int category;          // buy menu category (see LOADOUT_CAT_*)
  int optionA, optionB;  // weapon ids, optionA = default
};
enum { LOADOUT_CAT_PISTOLS = 0, LOADOUT_CAT_HEAVY, LOADOUT_CAT_SMGS, LOADOUT_CAT_RIFLES, LOADOUT_CAT_GEAR, LOADOUT_CAT_GRENADES, LOADOUT_NUM_CATEGORIES };

const std::vector<LoadoutSlot>& Loadout_Slots();
int Loadout_Get(const LoadoutSlot& s);                 // chosen weapon id (falls back to optionA)
void Loadout_Set(const LoadoutSlot& s, int weaponId);
// weapons of a buy menu category for the team (max 6, CS:GO layout, loadout choices applied).
// Gear / grenades return an empty list (they are not weapons with a loadout).
std::vector<int> Loadout_CategoryWeapons(int category, int team);
int Loadout_StartPistol(int team, bool bot);           // spawn pistol (CT: P2000 / USP-S)
const char* Loadout_CategoryName(int category);
