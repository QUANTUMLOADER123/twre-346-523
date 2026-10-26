// weapon_parse.hpp - CCSWeaponInfo: weapon script data (values modelled after CS:GO weapon scripts)
#pragma once
#include <string>

enum CSWeaponID {
  WEAPON_NONE = 0,
  WEAPON_KNIFE, WEAPON_GLOCK, WEAPON_USP_SILENCER, WEAPON_P250, WEAPON_DEAGLE, WEAPON_REVOLVER,
  WEAPON_MAC10, WEAPON_MP9, WEAPON_UMP45, WEAPON_P90,
  WEAPON_NOVA, WEAPON_XM1014,
  WEAPON_GALILAR, WEAPON_FAMAS, WEAPON_AK47, WEAPON_M4A1, WEAPON_M4A1_SILENCER, WEAPON_SG556, WEAPON_AUG,
  WEAPON_SSG08, WEAPON_AWP, WEAPON_G3SG1, WEAPON_SCAR20,
  WEAPON_HEGRENADE, WEAPON_FLASHBANG, WEAPON_SMOKEGRENADE,
  WEAPON_C4,
  // CS:GO additions (kept after C4 so saved ids / tables stay stable)
  WEAPON_ELITE, WEAPON_FIVESEVEN, WEAPON_TEC9, WEAPON_CZ75A, WEAPON_HKP2000,
  WEAPON_MP7, WEAPON_MP5SD, WEAPON_BIZON,
  WEAPON_MAG7, WEAPON_SAWEDOFF, WEAPON_M249, WEAPON_NEGEV,
  WEAPON_MOLOTOV, WEAPON_INCGRENADE,
  WEAPON_MAX
};
enum CSWeaponType { WEAPONTYPE_KNIFE = 0, WEAPONTYPE_PISTOL, WEAPONTYPE_SUBMACHINEGUN, WEAPONTYPE_RIFLE, WEAPONTYPE_SHOTGUN, WEAPONTYPE_SNIPER_RIFLE, WEAPONTYPE_MACHINEGUN, WEAPONTYPE_C4, WEAPONTYPE_GRENADE };
enum { WEAPON_SLOT_RIFLE = 0, WEAPON_SLOT_PISTOL = 1, WEAPON_SLOT_KNIFE = 2, WEAPON_SLOT_GRENADES = 3, WEAPON_SLOT_C4 = 4 };
enum EquipmentID { EQUIP_KEVLAR = 100, EQUIP_ASSAULTSUIT, EQUIP_DEFUSER };

struct CCSWeaponInfo {
  CSWeaponID id;
  const char* className;     // weapon_ak47
  const char* printName;     // AK-47
  const char* modelPath;     // assets/models/weapons/w_ak47.glb (nullptr = procedural)
  CSWeaponType type;
  int slot;
  int team;                  // 0 both, 2 T only, 3 CT only
  int price;
  int killAward;
  int clipSize, reserveAmmo;
  int damage;
  float armorRatio;          // fraction of damage applied through armor
  float penetration;
  float range, rangeModifier;
  float cycleTime;           // seconds between shots
  float cycleTimeAlt;        // burst/secondary
  bool fullAuto;
  int bullets;               // shotgun pellets
  float maxSpeed, maxSpeedAlt;
  float headshotMultiplier;
  // accuracy (CS:GO units: radians * 1000)
  float spread, spreadAlt;
  float inaccuracyCrouch, inaccuracyStand, inaccuracyJump, inaccuracyLand, inaccuracyFire, inaccuracyMove;
  float inaccuracyCrouchAlt, inaccuracyStandAlt;
  float recoveryTimeCrouch, recoveryTimeStand;
  // recoil
  float recoilAngle, recoilAngleVariance, recoilMagnitude, recoilMagnitudeVariance;
  int zoomLevels; float zoomFov1, zoomFov2;
  float reloadTime, deployTime;
  bool silencer;              // has a silencer
  float modelLength;          // world model length in units
  float tracerFrequency;
  const char* killIcon;       // short name for killfeed
  float flinchVelocityModifierLarge;
};

const CCSWeaponInfo* GetWeaponInfo(int id);
const CCSWeaponInfo* GetWeaponInfoByName(const std::string& className); // accepts "ak47" or "weapon_ak47"
const char* WeaponIdToAlias(int id);
int AliasToWeaponID(const std::string& alias);
inline bool IsFireGrenade(int id) { return id == WEAPON_MOLOTOV || id == WEAPON_INCGRENADE; }
inline bool IsGrenade(int id) { return id == WEAPON_HEGRENADE || id == WEAPON_FLASHBANG || id == WEAPON_SMOKEGRENADE || IsFireGrenade(id); }
inline bool IsSniper(int id) { return id == WEAPON_AWP || id == WEAPON_SSG08 || id == WEAPON_G3SG1 || id == WEAPON_SCAR20; }
