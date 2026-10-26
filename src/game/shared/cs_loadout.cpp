// cs_loadout.cpp - see cs_loadout.hpp
#include "cs_loadout.hpp"
#include "weapon_parse.hpp"
#include "../../engine/globals.hpp"
#include "../../engine/convar.hpp"
#include "../../engine/random.hpp"
#include <cstring>

static ConVar loadout_ct_pistol("loadout_ct_pistol", "hkp2000", FCVAR_ARCHIVE, "CT loadout: starting pistol (hkp2000 / usp_silencer)");
static ConVar loadout_ct_fiveseven("loadout_ct_fiveseven", "fiveseven", FCVAR_ARCHIVE, "CT loadout: pistol slot 4 (fiveseven / cz75a)");
static ConVar loadout_t_tec9("loadout_t_tec9", "tec9", FCVAR_ARCHIVE, "T loadout: pistol slot 4 (tec9 / cz75a)");
static ConVar loadout_ct_deagle("loadout_ct_deagle", "deagle", FCVAR_ARCHIVE, "CT loadout: pistol slot 5 (deagle / revolver)");
static ConVar loadout_t_deagle("loadout_t_deagle", "deagle", FCVAR_ARCHIVE, "T loadout: pistol slot 5 (deagle / revolver)");
static ConVar loadout_ct_mag7("loadout_ct_mag7", "mag7", FCVAR_ARCHIVE, "CT loadout: heavy slot 3 (mag7 / sawedoff, the other team's gun needs mp_buy_anyteam)");
static ConVar loadout_t_sawedoff("loadout_t_sawedoff", "sawedoff", FCVAR_ARCHIVE, "T loadout: heavy slot 3 (sawedoff / mag7, the other team's gun needs mp_buy_anyteam)");
static ConVar loadout_ct_mp9("loadout_ct_mp9", "mp9", FCVAR_ARCHIVE, "CT loadout: SMG slot 1 (mp9 / mac10, the other team's gun needs mp_buy_anyteam)");
static ConVar loadout_t_mac10("loadout_t_mac10", "mac10", FCVAR_ARCHIVE, "T loadout: SMG slot 1 (mac10 / mp9, the other team's gun needs mp_buy_anyteam)");
static ConVar loadout_ct_mp7("loadout_ct_mp7", "mp7", FCVAR_ARCHIVE, "CT loadout: SMG slot 2 (mp7 / mp5sd)");
static ConVar loadout_t_mp7("loadout_t_mp7", "mp7", FCVAR_ARCHIVE, "T loadout: SMG slot 2 (mp7 / mp5sd)");
static ConVar loadout_ct_m4("loadout_ct_m4", "m4a1", FCVAR_ARCHIVE, "CT loadout: rifle slot 2 (m4a1 = M4A4 / m4a1_silencer)");

const std::vector<LoadoutSlot>& Loadout_Slots() {
  static const std::vector<LoadoutSlot> s = {
    { "loadout_ct_pistol", "Starting pistol", TEAM_CT, LOADOUT_CAT_PISTOLS, WEAPON_HKP2000, WEAPON_USP_SILENCER },
    { "loadout_ct_fiveseven", "Pistol", TEAM_CT, LOADOUT_CAT_PISTOLS, WEAPON_FIVESEVEN, WEAPON_CZ75A },
    { "loadout_ct_deagle", "Pistol", TEAM_CT, LOADOUT_CAT_PISTOLS, WEAPON_DEAGLE, WEAPON_REVOLVER },
    { "loadout_ct_mag7", "Shotgun", TEAM_CT, LOADOUT_CAT_HEAVY, WEAPON_MAG7, WEAPON_SAWEDOFF },
    { "loadout_ct_mp9", "SMG", TEAM_CT, LOADOUT_CAT_SMGS, WEAPON_MP9, WEAPON_MAC10 },
    { "loadout_ct_mp7", "SMG", TEAM_CT, LOADOUT_CAT_SMGS, WEAPON_MP7, WEAPON_MP5SD },
    { "loadout_ct_m4", "Rifle", TEAM_CT, LOADOUT_CAT_RIFLES, WEAPON_M4A1, WEAPON_M4A1_SILENCER },
    { "loadout_t_tec9", "Pistol", TEAM_TERRORIST, LOADOUT_CAT_PISTOLS, WEAPON_TEC9, WEAPON_CZ75A },
    { "loadout_t_deagle", "Pistol", TEAM_TERRORIST, LOADOUT_CAT_PISTOLS, WEAPON_DEAGLE, WEAPON_REVOLVER },
    { "loadout_t_sawedoff", "Shotgun", TEAM_TERRORIST, LOADOUT_CAT_HEAVY, WEAPON_SAWEDOFF, WEAPON_MAG7 },
    { "loadout_t_mac10", "SMG", TEAM_TERRORIST, LOADOUT_CAT_SMGS, WEAPON_MAC10, WEAPON_MP9 },
    { "loadout_t_mp7", "SMG", TEAM_TERRORIST, LOADOUT_CAT_SMGS, WEAPON_MP7, WEAPON_MP5SD },
  };
  return s;
}

int Loadout_Get(const LoadoutSlot& s) {
  ConVar* cv = FindConVar(s.cvar);
  if (cv) {
    const CCSWeaponInfo* wi = GetWeaponInfoByName(cv->GetString());
    if (wi && (wi->id == s.optionA || wi->id == s.optionB)) {
      // the other team's gun only when the server lets everyone buy everything
      if (wi->team && wi->team != s.team) { ConVar* any = FindConVar("mp_buy_anyteam"); if (!any || !any->GetBool()) return s.optionA; }
      return wi->id;
    }
  }
  return s.optionA;
}
void Loadout_Set(const LoadoutSlot& s, int weaponId) {
  if (ConVar* cv = FindConVar(s.cvar)) cv->SetValue(WeaponIdToAlias(weaponId));
}

static int Slot(const char* cvar) {
  for (const LoadoutSlot& s : Loadout_Slots()) if (!strcmp(s.cvar, cvar)) return Loadout_Get(s);
  return WEAPON_NONE;
}

std::vector<int> Loadout_CategoryWeapons(int category, int team) {
  const bool ct = team == TEAM_CT;
  switch (category) {
    case LOADOUT_CAT_PISTOLS:
      return { ct ? Slot("loadout_ct_pistol") : WEAPON_GLOCK, WEAPON_ELITE, WEAPON_P250, ct ? Slot("loadout_ct_fiveseven") : Slot("loadout_t_tec9"), ct ? Slot("loadout_ct_deagle") : Slot("loadout_t_deagle") };
    case LOADOUT_CAT_HEAVY:
      return { WEAPON_NOVA, WEAPON_XM1014, ct ? Slot("loadout_ct_mag7") : Slot("loadout_t_sawedoff"), WEAPON_M249, WEAPON_NEGEV };
    case LOADOUT_CAT_SMGS:
      return { ct ? Slot("loadout_ct_mp9") : Slot("loadout_t_mac10"), ct ? Slot("loadout_ct_mp7") : Slot("loadout_t_mp7"), WEAPON_UMP45, WEAPON_P90, WEAPON_BIZON };
    case LOADOUT_CAT_RIFLES:
      if (ct) return { WEAPON_FAMAS, Slot("loadout_ct_m4"), WEAPON_SSG08, WEAPON_AUG, WEAPON_AWP, WEAPON_SCAR20 };
      return { WEAPON_GALILAR, WEAPON_AK47, WEAPON_SSG08, WEAPON_SG556, WEAPON_AWP, WEAPON_G3SG1 };
    default: return {};
  }
}

int Loadout_StartPistol(int team, bool bot) {
  if (team != TEAM_CT) return WEAPON_GLOCK;
  if (bot) return RandomInt(0, 1) ? WEAPON_USP_SILENCER : WEAPON_HKP2000;
  return Slot("loadout_ct_pistol");
}

const char* Loadout_CategoryName(int category) {
  static const char* n[LOADOUT_NUM_CATEGORIES] = { "PISTOLS", "HEAVY", "SMGs", "RIFLES", "GEAR", "GRENADES" };
  return category >= 0 && category < LOADOUT_NUM_CATEGORIES ? n[category] : "";
}
