// cs_loadout.cpp - CS2-style loadout implementation
#include "cs_loadout.hpp"
#include "weapon_parse.hpp"
#include "../../engine/globals.hpp"
#include "../../engine/convar.hpp"
#include "../../engine/random.hpp"
#include <cstring>
#include <cstdio>

// Convars for each slot: loadout2_<team>_<cat>_<slot>
// Team: ct / t
// Cat: p (pistols) / m (midtier) / r (rifles)
// Slot: 1-5

static ConVar ct_p2("loadout2_ct_p2", "elite", FCVAR_ARCHIVE);
static ConVar ct_p3("loadout2_ct_p3", "fiveseven", FCVAR_ARCHIVE);
static ConVar ct_p4("loadout2_ct_p4", "deagle", FCVAR_ARCHIVE);
static ConVar ct_p5("loadout2_ct_p5", "p250", FCVAR_ARCHIVE);
static ConVar ct_m1("loadout2_ct_m1", "mp9", FCVAR_ARCHIVE);
static ConVar ct_m2("loadout2_ct_m2", "mp7", FCVAR_ARCHIVE);
static ConVar ct_m3("loadout2_ct_m3", "ump45", FCVAR_ARCHIVE);
static ConVar ct_m4("loadout2_ct_m4", "p90", FCVAR_ARCHIVE);
static ConVar ct_m5("loadout2_ct_m5", "bizon", FCVAR_ARCHIVE);
static ConVar ct_r1("loadout2_ct_r1", "famas", FCVAR_ARCHIVE);
static ConVar ct_r2("loadout2_ct_r2", "m4a1", FCVAR_ARCHIVE);
static ConVar ct_r3("loadout2_ct_r3", "aug", FCVAR_ARCHIVE);
static ConVar ct_r4("loadout2_ct_r4", "ssg08", FCVAR_ARCHIVE);
static ConVar ct_r5("loadout2_ct_r5", "awp", FCVAR_ARCHIVE);

static ConVar t_p2("loadout2_t_p2", "elite", FCVAR_ARCHIVE);
static ConVar t_p3("loadout2_t_p3", "cz75a", FCVAR_ARCHIVE);
static ConVar t_p4("loadout2_t_p4", "deagle", FCVAR_ARCHIVE);
static ConVar t_p5("loadout2_t_p5", "p250", FCVAR_ARCHIVE);
static ConVar t_m1("loadout2_t_m1", "mac10", FCVAR_ARCHIVE);
static ConVar t_m2("loadout2_t_m2", "mp7", FCVAR_ARCHIVE);
static ConVar t_m3("loadout2_t_m3", "ump45", FCVAR_ARCHIVE);
static ConVar t_m4("loadout2_t_m4", "p90", FCVAR_ARCHIVE);
static ConVar t_m5("loadout2_t_m5", "mag7", FCVAR_ARCHIVE);
static ConVar t_r1("loadout2_t_r1", "galilar", FCVAR_ARCHIVE);
static ConVar t_r2("loadout2_t_r2", "ak47", FCVAR_ARCHIVE);
static ConVar t_r3("loadout2_t_r3", "sg556", FCVAR_ARCHIVE);
static ConVar t_r4("loadout2_t_r4", "ssg08", FCVAR_ARCHIVE);
static ConVar t_r5("loadout2_t_r5", "awp", FCVAR_ARCHIVE);

static int GetCvarWeapon(ConVar* cv, int defaultId) {
  if (!cv) return defaultId;
  const CCSWeaponInfo* wi = GetWeaponInfoByName(cv->GetString());
  return wi ? wi->id : defaultId;
}

static ConVar* FindSlotCvar(int team, int cat, int slot) {
  const bool ct = (team == 3); // TEAM_CT = 3
  const char prefix = (cat == 0) ? 'p' : (cat == 1) ? 'm' : 'r';
  char name[32];
  snprintf(name, sizeof(name), "loadout2_%s_%c%d", ct ? "ct" : "t", prefix, slot + 1);
  return FindConVar(name);
}

std::vector<LoadoutSlot2> Loadout_GetSlots2(int team) {
  std::vector<LoadoutSlot2> slots;
  const bool ct = (team == 3);

  // PISTOLS (slot 0 = Glock/USP locked)
  slots.push_back({0, 0, ct ? WEAPON_HKP2000 : WEAPON_GLOCK, ct ? WEAPON_HKP2000 : WEAPON_GLOCK, true});
  slots.push_back({0, 1, GetCvarWeapon(ct ? &ct_p2 : &t_p2, WEAPON_ELITE), WEAPON_ELITE, false});
  slots.push_back({0, 2, GetCvarWeapon(ct ? &ct_p3 : &t_p3, ct ? WEAPON_FIVESEVEN : WEAPON_CZ75A), ct ? WEAPON_FIVESEVEN : WEAPON_CZ75A, false});
  slots.push_back({0, 3, GetCvarWeapon(ct ? &ct_p4 : &t_p4, WEAPON_DEAGLE), WEAPON_DEAGLE, false});
  slots.push_back({0, 4, GetCvarWeapon(ct ? &ct_p5 : &t_p5, WEAPON_P250), WEAPON_P250, false});

  // MID-TIER (SMGs + Shotguns)
  slots.push_back({1, 0, GetCvarWeapon(ct ? &ct_m1 : &t_m1, ct ? WEAPON_MP9 : WEAPON_MAC10), ct ? WEAPON_MP9 : WEAPON_MAC10, false});
  slots.push_back({1, 1, GetCvarWeapon(ct ? &ct_m2 : &t_m2, WEAPON_MP7), WEAPON_MP7, false});
  slots.push_back({1, 2, GetCvarWeapon(ct ? &ct_m3 : &t_m3, WEAPON_UMP45), WEAPON_UMP45, false});
  slots.push_back({1, 3, GetCvarWeapon(ct ? &ct_m4 : &t_m4, WEAPON_P90), WEAPON_P90, false});
  slots.push_back({1, 4, GetCvarWeapon(ct ? &ct_m5 : &t_m5, ct ? WEAPON_BIZON : WEAPON_MAG7), ct ? WEAPON_BIZON : WEAPON_MAG7, false});

  // RIFLES
  slots.push_back({2, 0, GetCvarWeapon(ct ? &ct_r1 : &t_r1, ct ? WEAPON_FAMAS : WEAPON_GALILAR), ct ? WEAPON_FAMAS : WEAPON_GALILAR, false});
  slots.push_back({2, 1, GetCvarWeapon(ct ? &ct_r2 : &t_r2, ct ? WEAPON_M4A1 : WEAPON_AK47), ct ? WEAPON_M4A1 : WEAPON_AK47, false});
  slots.push_back({2, 2, GetCvarWeapon(ct ? &ct_r3 : &t_r3, ct ? WEAPON_AUG : WEAPON_SG556), ct ? WEAPON_AUG : WEAPON_SG556, false});
  slots.push_back({2, 3, GetCvarWeapon(ct ? &ct_r4 : &t_r4, WEAPON_SSG08), WEAPON_SSG08, false});
  slots.push_back({2, 4, GetCvarWeapon(ct ? &ct_r5 : &t_r5, WEAPON_AWP), WEAPON_AWP, false});

  return slots;
}

void Loadout_SetWeapon2(int team, int category, int slotIndex, int weaponId) {
  ConVar* cv = FindSlotCvar(team, category, slotIndex);
  if (cv) {
    const char* alias = WeaponIdToAlias(weaponId);
    if (alias) cv->SetValue(alias);
  }
}

std::vector<int> Loadout_CategoryWeapons2(int category, int team) {
  const bool ct = (team == 3);
  switch (category) {
    case 0: // PISTOLS
      return { ct ? WEAPON_HKP2000 : WEAPON_GLOCK, WEAPON_ELITE, WEAPON_P250,
               ct ? WEAPON_FIVESEVEN : WEAPON_TEC9, WEAPON_CZ75A, WEAPON_DEAGLE, WEAPON_REVOLVER };
    case 1: // MID-TIER
      return { ct ? WEAPON_MP9 : WEAPON_MAC10, WEAPON_MP7, WEAPON_MP5SD, WEAPON_UMP45,
               WEAPON_P90, WEAPON_BIZON, WEAPON_NOVA, WEAPON_XM1014, ct ? WEAPON_MAG7 : WEAPON_SAWEDOFF,
               WEAPON_M249, WEAPON_NEGEV };
    case 2: // RIFLES
      if (ct) return { WEAPON_FAMAS, WEAPON_M4A1, WEAPON_M4A1_SILENCER, WEAPON_AUG, WEAPON_SSG08, WEAPON_AWP, WEAPON_SCAR20 };
      return { WEAPON_GALILAR, WEAPON_AK47, WEAPON_SG556, WEAPON_SSG08, WEAPON_AWP, WEAPON_G3SG1, WEAPON_SCAR20 };
    default: return {};
  }
}

bool Loadout_IsEquipped2(int team, int weaponId) {
  for (const auto& s : Loadout_GetSlots2(team)) {
    if (s.weaponId == weaponId) return true;
  }
  return false;
}

const char* Loadout_CategoryName(int category) {
  static const char* names[] = { "PISTOLS", "MID-TIER", "RIFLES" };
  return (category >= 0 && category < 3) ? names[category] : "";
}

int Loadout_StartPistol(int team, bool bot) {
  if (team != 3) return WEAPON_GLOCK;
  if (bot) return RandomInt(0, 1) ? WEAPON_USP_SILENCER : WEAPON_HKP2000;
  return WEAPON_HKP2000;
}
