#include "weapon_parse.hpp"
#include <cstring>

#define M(x) "assets/models/weapons/" x ".glb"
// id, class, name, model, type, slot, team, price, award, clip, reserve, dmg, armorRatio, pen, range, rangeMod, cycle, cycleAlt, auto, bullets,
// speed, speedAlt, hsMult, spread, spreadAlt, inCrouch, inStand, inJump, inLand, inFire, inMove, inCrouchAlt, inStandAlt, recCrouch, recStand,
// recoilAngle, recoilAngleVar, recoilMag, recoilMagVar, zoomLevels, zoom1, zoom2, reload, deploy, silencer, length, tracerFreq, icon, flinch
static const CCSWeaponInfo s_weapons[WEAPON_MAX] = {
  { WEAPON_NONE, "weapon_none", "None", nullptr, WEAPONTYPE_KNIFE, 2, 0, 0, 0, -1, 0, 0, 1, 0, 0, 1, 1, 1, false, 1, 250, 250, 4, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 90, 90, 1, 1, false, 10, 0, "", 1 },
  { WEAPON_KNIFE, "weapon_knife", "Knife", nullptr, WEAPONTYPE_KNIFE, WEAPON_SLOT_KNIFE, 0, 0, 1500, -1, 0, 40, 0.85f, 0, 48, 1, 0.4f, 1.0f, true, 1, 250, 250, 1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 90, 90, 1, 1.0f, false, 12, 0, "knife", 1 },
  { WEAPON_GLOCK, "weapon_glock", "Glock-18", M("w_glock"), WEAPONTYPE_PISTOL, WEAPON_SLOT_PISTOL, 2, 200, 300, 20, 120, 30, 0.47f, 1, 4096, 0.85f, 0.15f, 0.5f, false, 1, 240, 240, 4,
    2.0f, 2.0f, 4.2f, 5.6f, 340, 70, 56, 13, 4.2f, 5.6f, 0.23f, 0.28f, 0, 20, 18, 0, 0, 90, 90, 2.27f, 1.0f, false, 9, 1, "glock", 0.5f },
  { WEAPON_USP_SILENCER, "weapon_usp_silencer", "USP-S", M("w_usp"), WEAPONTYPE_PISTOL, WEAPON_SLOT_PISTOL, 3, 200, 300, 12, 24, 35, 0.505f, 1, 4096, 0.91f, 0.17f, 0.17f, false, 1, 240, 240, 4,
    2.5f, 2.5f, 3.7f, 4.9f, 340, 70, 52, 13, 3.7f, 4.9f, 0.30f, 0.37f, 0, 20, 23, 0, 0, 90, 90, 2.2f, 1.0f, true, 11, 1, "usp_silencer", 0.5f },
  { WEAPON_P250, "weapon_p250", "P250", M("w_p250"), WEAPONTYPE_PISTOL, WEAPON_SLOT_PISTOL, 0, 300, 300, 13, 26, 38, 0.64f, 1, 4096, 0.90f, 0.15f, 0.15f, false, 1, 240, 240, 4,
    2.0f, 2.0f, 5.0f, 6.6f, 340, 70, 52, 13, 5.0f, 6.6f, 0.25f, 0.30f, 0, 20, 26, 0, 0, 90, 90, 2.2f, 1.0f, false, 9, 1, "p250", 0.5f },
  { WEAPON_DEAGLE, "weapon_deagle", "Desert Eagle", M("w_deagle"), WEAPONTYPE_PISTOL, WEAPON_SLOT_PISTOL, 0, 700, 300, 7, 35, 63, 0.932f, 2, 4096, 0.81f, 0.225f, 0.225f, false, 1, 230, 230, 4,
    2.0f, 2.0f, 6.2f, 8.8f, 400, 90, 55, 15, 6.2f, 8.8f, 0.55f, 0.81f, 0, 20, 39, 0, 0, 90, 90, 2.2f, 1.0f, false, 11, 1, "deagle", 0.5f },
  { WEAPON_REVOLVER, "weapon_revolver", "R8 Revolver", M("w_revolver"), WEAPONTYPE_PISTOL, WEAPON_SLOT_PISTOL, 0, 600, 300, 8, 8, 86, 0.932f, 2, 4096, 0.94f, 0.5f, 0.4f, false, 1, 220, 220, 4,
    0.5f, 2.0f, 2.3f, 3.0f, 400, 90, 60, 15, 30.f, 40.f, 0.6f, 0.8f, 0, 20, 45, 0, 0, 90, 90, 2.3f, 1.0f, false, 11, 1, "revolver", 0.5f },
  { WEAPON_MAC10, "weapon_mac10", "MAC-10", M("w_mac10"), WEAPONTYPE_SUBMACHINEGUN, WEAPON_SLOT_RIFLE, 2, 1050, 600, 30, 100, 29, 0.575f, 1, 4096, 0.80f, 0.075f, 0.075f, true, 1, 240, 240, 4,
    0.6f, 0.6f, 8.5f, 11.4f, 120, 45, 7, 30, 8.5f, 11.4f, 0.30f, 0.40f, 0, 70, 18, 1, 0, 90, 90, 2.6f, 1.0f, false, 12, 3, "mac10", 0.5f },
  { WEAPON_MP9, "weapon_mp9", "MP9", M("w_mp9"), WEAPONTYPE_SUBMACHINEGUN, WEAPON_SLOT_RIFLE, 3, 1250, 600, 30, 120, 26, 0.6f, 1, 4096, 0.87f, 0.07f, 0.07f, true, 1, 240, 240, 4,
    0.6f, 0.6f, 8.2f, 11.0f, 120, 45, 7, 30, 8.2f, 11.0f, 0.30f, 0.40f, 0, 70, 19, 1, 0, 90, 90, 2.1f, 1.0f, false, 13, 3, "mp9", 0.5f },
  { WEAPON_UMP45, "weapon_ump45", "UMP-45", M("w_ump45"), WEAPONTYPE_SUBMACHINEGUN, WEAPON_SLOT_RIFLE, 0, 1200, 600, 25, 100, 35, 0.65f, 1, 4096, 0.75f, 0.09f, 0.09f, true, 1, 230, 230, 4,
    0.6f, 0.6f, 11.0f, 15.0f, 120, 45, 8, 30, 11.0f, 15.0f, 0.30f, 0.40f, 0, 70, 21, 1, 0, 90, 90, 3.5f, 1.0f, false, 16, 3, "ump45", 0.5f },
  { WEAPON_P90, "weapon_p90", "P90", M("w_p90"), WEAPONTYPE_SUBMACHINEGUN, WEAPON_SLOT_RIFLE, 0, 2350, 300, 50, 100, 26, 0.69f, 1, 4096, 0.86f, 0.07f, 0.07f, true, 1, 230, 230, 4,
    0.6f, 0.6f, 8.0f, 12.0f, 120, 45, 6, 30, 8.0f, 12.0f, 0.30f, 0.40f, 0, 70, 16, 1, 0, 90, 90, 3.3f, 1.0f, false, 18, 3, "p90", 0.5f },
  { WEAPON_NOVA, "weapon_nova", "Nova", M("w_nova"), WEAPONTYPE_SHOTGUN, WEAPON_SLOT_RIFLE, 0, 1050, 900, 8, 32, 26, 0.5f, 1, 3000, 0.70f, 0.88f, 0.88f, false, 9, 220, 220, 4,
    40.0f, 40.0f, 6.0f, 8.0f, 300, 60, 25, 30, 6.0f, 8.0f, 0.35f, 0.45f, 0, 30, 143, 22, 0, 90, 90, 0.5f, 1.0f, false, 24, 1, "nova", 0.5f },
  { WEAPON_XM1014, "weapon_xm1014", "XM1014", M("w_xm1014"), WEAPONTYPE_SHOTGUN, WEAPON_SLOT_RIFLE, 0, 2000, 900, 7, 32, 20, 0.8f, 1, 3000, 0.70f, 0.35f, 0.35f, true, 6, 215, 215, 4,
    38.0f, 38.0f, 6.0f, 8.0f, 300, 60, 25, 30, 6.0f, 8.0f, 0.35f, 0.45f, 0, 30, 72, 20, 0, 90, 90, 0.45f, 1.0f, false, 26, 1, "xm1014", 0.5f },
  { WEAPON_GALILAR, "weapon_galilar", "Galil AR", M("w_galilar"), WEAPONTYPE_RIFLE, WEAPON_SLOT_RIFLE, 2, 1800, 300, 35, 90, 30, 0.775f, 2, 8192, 0.98f, 0.09f, 0.09f, true, 1, 215, 215, 4,
    0.6f, 0.6f, 5.0f, 7.0f, 400, 80, 9, 130, 5.0f, 7.0f, 0.30f, 0.42f, 0, 30, 21, 1, 0, 90, 90, 3.0f, 1.0f, false, 30, 3, "galilar", 0.5f },
  { WEAPON_FAMAS, "weapon_famas", "FAMAS", M("w_famas"), WEAPONTYPE_RIFLE, WEAPON_SLOT_RIFLE, 3, 2050, 300, 25, 90, 30, 0.7f, 2, 8192, 0.96f, 0.09f, 0.09f, true, 1, 220, 220, 4,
    0.6f, 0.6f, 4.8f, 6.4f, 400, 80, 9, 125, 4.8f, 6.4f, 0.30f, 0.42f, 0, 30, 20, 1, 0, 90, 90, 3.3f, 1.0f, false, 28, 3, "famas", 0.5f },
  { WEAPON_AK47, "weapon_ak47", "AK-47", M("w_ak47"), WEAPONTYPE_RIFLE, WEAPON_SLOT_RIFLE, 2, 2700, 300, 30, 90, 36, 0.775f, 2, 8192, 0.98f, 0.10f, 0.10f, true, 1, 215, 215, 4,
    0.6f, 0.6f, 4.81f, 6.41f, 400, 80, 7.8f, 140, 4.81f, 6.41f, 0.31f, 0.43f, 0, 30, 30, 1, 0, 90, 90, 2.43f, 1.0f, false, 32, 3, "ak47", 0.5f },
  { WEAPON_M4A1, "weapon_m4a1", "M4A4", M("w_m4a4"), WEAPONTYPE_RIFLE, WEAPON_SLOT_RIFLE, 3, 3100, 300, 30, 90, 33, 0.7f, 2, 8192, 0.97f, 0.09f, 0.09f, true, 1, 225, 225, 4,
    0.6f, 0.6f, 4.3f, 5.7f, 400, 80, 7.0f, 130, 4.3f, 5.7f, 0.30f, 0.40f, 0, 30, 23, 1, 0, 90, 90, 3.07f, 1.0f, false, 32, 3, "m4a1", 0.5f },
  { WEAPON_M4A1_SILENCER, "weapon_m4a1_silencer", "M4A1-S", M("w_m4a1s"), WEAPONTYPE_RIFLE, WEAPON_SLOT_RIFLE, 3, 2900, 300, 20, 80, 38, 0.7f, 2, 8192, 0.94f, 0.10f, 0.10f, true, 1, 225, 225, 4,
    0.6f, 0.6f, 3.7f, 4.9f, 400, 80, 6.5f, 125, 3.7f, 4.9f, 0.30f, 0.38f, 0, 30, 21, 1, 0, 90, 90, 3.07f, 1.0f, true, 34, 3, "m4a1_silencer", 0.5f },
  { WEAPON_SG556, "weapon_sg556", "SG 553", M("w_sg553"), WEAPONTYPE_RIFLE, WEAPON_SLOT_RIFLE, 2, 3000, 300, 30, 90, 30, 1.0f, 2, 8192, 0.98f, 0.09f, 0.135f, true, 1, 210, 150, 4,
    0.6f, 0.3f, 4.5f, 6.0f, 400, 80, 8.0f, 140, 2.0f, 2.8f, 0.32f, 0.44f, 0, 30, 28, 1, 1, 45, 45, 2.8f, 1.0f, false, 32, 3, "sg556", 0.5f },
  { WEAPON_AUG, "weapon_aug", "AUG", M("w_aug"), WEAPONTYPE_RIFLE, WEAPON_SLOT_RIFLE, 3, 3300, 300, 30, 90, 28, 0.9f, 2, 8192, 0.98f, 0.09f, 0.135f, true, 1, 220, 150, 4,
    0.6f, 0.3f, 4.3f, 5.7f, 400, 80, 7.5f, 130, 2.0f, 2.8f, 0.32f, 0.44f, 0, 30, 26, 1, 1, 45, 45, 3.8f, 1.0f, false, 30, 3, "aug", 0.5f },
  { WEAPON_SSG08, "weapon_ssg08", "SSG 08", M("w_ssg08"), WEAPONTYPE_SNIPER_RIFLE, WEAPON_SLOT_RIFLE, 0, 1700, 300, 10, 90, 88, 0.85f, 2.5f, 8192, 0.98f, 1.25f, 1.25f, false, 1, 230, 230, 4,
    0.3f, 0.3f, 23.f, 32.f, 30, 30, 35, 70, 2.2f, 3.0f, 0.06f, 0.08f, 0, 10, 33, 0, 2, 40, 15, 3.7f, 1.25f, false, 44, 1, "ssg08", 0.5f },
  { WEAPON_AWP, "weapon_awp", "AWP", M("w_awp"), WEAPONTYPE_SNIPER_RIFLE, WEAPON_SLOT_RIFLE, 0, 4750, 100, 10, 30, 115, 0.975f, 2.5f, 8192, 0.99f, 1.455f, 1.455f, false, 1, 200, 100, 4,
    0.2f, 0.2f, 90.f, 120.f, 400, 120, 120, 170, 1.5f, 2.0f, 0.25f, 0.35f, 0, 10, 78, 0, 2, 40, 10, 3.67f, 1.5f, false, 46, 1, "awp", 0.35f },
  { WEAPON_G3SG1, "weapon_g3sg1", "G3SG1", M("w_g3sg1"), WEAPONTYPE_SNIPER_RIFLE, WEAPON_SLOT_RIFLE, 2, 5000, 300, 20, 90, 80, 0.825f, 2.5f, 8192, 0.98f, 0.25f, 0.25f, false, 1, 215, 120, 4,
    0.3f, 0.3f, 25.f, 33.f, 400, 90, 30, 100, 1.8f, 2.4f, 0.25f, 0.32f, 0, 20, 30, 2, 2, 40, 15, 4.7f, 1.25f, false, 44, 1, "g3sg1", 0.5f },
  { WEAPON_SCAR20, "weapon_scar20", "SCAR-20", M("w_scar20"), WEAPONTYPE_SNIPER_RIFLE, WEAPON_SLOT_RIFLE, 3, 5000, 300, 20, 90, 80, 0.825f, 2.5f, 8192, 0.98f, 0.25f, 0.25f, false, 1, 215, 120, 4,
    0.3f, 0.3f, 25.f, 33.f, 400, 90, 30, 100, 1.8f, 2.4f, 0.25f, 0.32f, 0, 20, 30, 2, 2, 40, 15, 3.1f, 1.25f, false, 44, 1, "scar20", 0.5f },
  { WEAPON_HEGRENADE, "weapon_hegrenade", "HE Grenade", nullptr, WEAPONTYPE_GRENADE, WEAPON_SLOT_GRENADES, 0, 300, 300, -1, 1, 98, 0.57f, 0, 350, 1, 1.0f, 1.0f, false, 1, 245, 245, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 90, 90, 1, 1.0f, false, 6, 0, "hegrenade", 1 },
  { WEAPON_FLASHBANG, "weapon_flashbang", "Flashbang", nullptr, WEAPONTYPE_GRENADE, WEAPON_SLOT_GRENADES, 0, 200, 300, -1, 2, 1, 1, 0, 0, 1, 1.0f, 1.0f, false, 1, 245, 245, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 90, 90, 1, 1.0f, false, 6, 0, "flashbang", 1 },
  { WEAPON_SMOKEGRENADE, "weapon_smokegrenade", "Smoke Grenade", nullptr, WEAPONTYPE_GRENADE, WEAPON_SLOT_GRENADES, 0, 300, 300, -1, 1, 0, 1, 0, 0, 1, 1.0f, 1.0f, false, 1, 245, 245, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 90, 90, 1, 1.0f, false, 6, 0, "smokegrenade", 1 },
  { WEAPON_C4, "weapon_c4", "C4 Explosive", nullptr, WEAPONTYPE_C4, WEAPON_SLOT_C4, 2, 0, 0, -1, 1, 0, 1, 0, 0, 1, 1.0f, 1.0f, false, 1, 250, 250, 1,
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 0, 0, 0, 0, 90, 90, 1, 1.0f, false, 10, 0, "c4", 1 },
};
#undef M

const CCSWeaponInfo* GetWeaponInfo(int id) { return (id > 0 && id < WEAPON_MAX) ? &s_weapons[id] : &s_weapons[0]; }
const char* WeaponIdToAlias(int id) { const char* n = GetWeaponInfo(id)->className; return std::strncmp(n, "weapon_", 7) == 0 ? n + 7 : n; }
int AliasToWeaponID(const std::string& a) {
  std::string s = a.rfind("weapon_", 0) == 0 ? a.substr(7) : a;
  if (s == "m4a4") s = "m4a1"; if (s == "usp" || s == "usps") s = "usp_silencer"; if (s == "m4a1s") s = "m4a1_silencer"; if (s == "galil") s = "galilar";
  if (s == "sg553") s = "sg556"; if (s == "scout") s = "ssg08"; if (s == "he") s = "hegrenade"; if (s == "flash") s = "flashbang"; if (s == "smoke") s = "smokegrenade";
  if (s == "r8") s = "revolver"; if (s == "auto") s = "scar20";
  for (int i = 1; i < WEAPON_MAX; i++) if (s == WeaponIdToAlias(i)) return i;
  return WEAPON_NONE;
}
const CCSWeaponInfo* GetWeaponInfoByName(const std::string& n) { int id = AliasToWeaponID(n); return id ? &s_weapons[id] : nullptr; }
