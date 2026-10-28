// globals.hpp - CGlobalVarsBase (gpGlobals), CUserCmd, button bits
#pragma once
#include "mathlib.hpp"

class CGlobalVarsBase {
public:
  float realtime = 0;           // absolute time (wall clock since start)
  int framecount = 0;
  float absoluteframetime = 0;
  float curtime = 0;            // current game time (server tick based)
  float frametime = 0;
  int maxClients = 32;
  int tickcount = 0;
  float interval_per_tick = 1.f / 64.f;
  float interpolation_amount = 0;
  int simTicksThisFrame = 0;
  const char* mapname = "";
};
inline CGlobalVarsBase g_GlobalVars;
inline CGlobalVarsBase* gpGlobals = &g_GlobalVars;

#define TICK_INTERVAL (gpGlobals->interval_per_tick)
#define TIME_TO_TICKS(dt) ((int)(0.5f + (float)(dt) / TICK_INTERVAL))
#define TICKS_TO_TIME(t) (TICK_INTERVAL * (float)(t))
#define ROUND_TO_TICKS(t) (TICK_INTERVAL * TIME_TO_TICKS(t))
// real server time (gpGlobals->curtime is the player's tickbase time while a usercmd is being processed)
inline float ServerTime() { return TICKS_TO_TIME(gpGlobals->tickcount); }

// button bits (in_buttons.h)
enum {
  IN_ATTACK = 1 << 0, IN_JUMP = 1 << 1, IN_DUCK = 1 << 2, IN_FORWARD = 1 << 3, IN_BACK = 1 << 4,
  IN_USE = 1 << 5, IN_CANCEL = 1 << 6, IN_LEFT = 1 << 7, IN_RIGHT = 1 << 8, IN_MOVELEFT = 1 << 9,
  IN_MOVERIGHT = 1 << 10, IN_ATTACK2 = 1 << 11, IN_RUN = 1 << 12, IN_RELOAD = 1 << 13, IN_ALT1 = 1 << 14,
  IN_ALT2 = 1 << 15, IN_SCORE = 1 << 16, IN_SPEED = 1 << 17, IN_WALK = 1 << 18, IN_ZOOM = 1 << 19,
  IN_WEAPON1 = 1 << 20, IN_WEAPON2 = 1 << 21, IN_BULLRUSH = 1 << 22, IN_GRENADE1 = 1 << 23, IN_GRENADE2 = 1 << 24,
  IN_LOOKSPIN = 1 << 25,
};

class CUserCmd {
public:
  int command_number = 0;
  int tick_count = 0;
  QAngle viewangles;
  Vector aimdirection;
  float forwardmove = 0;
  float sidemove = 0;
  float upmove = 0;
  int buttons = 0;
  unsigned char impulse = 0;
  int weaponselect = 0;
  int weaponsubtype = 0;
  int random_seed = 0;
  short mousedx = 0, mousedy = 0;
  bool hasbeenpredicted = false;
  // engine side (not networked in real Source, kept here for the HvH simulation)
  bool bSendPacket = true;      // false => command is choked (held back) by the client
  int tickbaseShift = 0;        // doubletap / hideshots tickbase manipulation request
  bool bFakeDuck = false;
  void Reset() { *this = CUserCmd(); }
};

// max speeds (cs_shareddefs)
constexpr float CS_PLAYER_SPEED_RUN = 260.f;
constexpr float CS_PLAYER_SPEED_WALK_MODIFIER = 0.52f;
constexpr float CS_PLAYER_SPEED_DUCK_MODIFIER = 0.34f;
constexpr float CS_PLAYER_SPEED_STOPPED = 1.f;

// fFlags
enum {
  FL_ONGROUND = 1 << 0, FL_DUCKING = 1 << 1, FL_ANIMDUCKING = 1 << 2, FL_WATERJUMP = 1 << 3, FL_ONTRAIN = 1 << 4,
  FL_INRAIN = 1 << 5, FL_FROZEN = 1 << 6, FL_ATCONTROLS = 1 << 7, FL_CLIENT = 1 << 8, FL_FAKECLIENT = 1 << 9,
  FL_INWATER = 1 << 10, FL_FLY = 1 << 11, FL_GODMODE = 1 << 15, FL_NOTARGET = 1 << 16,
};
enum MoveType_t { MOVETYPE_NONE = 0, MOVETYPE_ISOMETRIC, MOVETYPE_WALK, MOVETYPE_STEP, MOVETYPE_FLY, MOVETYPE_FLYGRAVITY, MOVETYPE_VPHYSICS, MOVETYPE_PUSH, MOVETYPE_NOCLIP, MOVETYPE_LADDER, MOVETYPE_OBSERVER };
enum LifeState_t { LIFE_ALIVE = 0, LIFE_DYING = 1, LIFE_DEAD = 2, LIFE_RESPAWNABLE = 3 };
enum { TEAM_UNASSIGNED = 0, TEAM_SPECTATOR = 1, TEAM_TERRORIST = 2, TEAM_CT = 3 };
enum ObserverMode_t { OBS_MODE_NONE = 0, OBS_MODE_DEATHCAM, OBS_MODE_FREEZECAM, OBS_MODE_FIXED, OBS_MODE_IN_EYE, OBS_MODE_CHASE, OBS_MODE_ROAMING };

// hitgroups
enum { HITGROUP_GENERIC = 0, HITGROUP_HEAD = 1, HITGROUP_CHEST = 2, HITGROUP_STOMACH = 3, HITGROUP_LEFTARM = 4, HITGROUP_RIGHTARM = 5, HITGROUP_LEFTLEG = 6, HITGROUP_RIGHTLEG = 7, HITGROUP_GEAR = 10 };
inline const char* HitgroupName(int hg) {
  switch (hg) { case HITGROUP_HEAD: return "head"; case HITGROUP_CHEST: return "chest"; case HITGROUP_STOMACH: return "stomach";
    case HITGROUP_LEFTARM: return "left arm"; case HITGROUP_RIGHTARM: return "right arm"; case HITGROUP_LEFTLEG: return "left leg"; case HITGROUP_RIGHTLEG: return "right leg"; default: return "body"; }
}
// hitboxes (CS:GO hitbox set 0 order)
enum {
  HITBOX_HEAD = 0, HITBOX_NECK, HITBOX_PELVIS, HITBOX_STOMACH, HITBOX_LOWER_CHEST, HITBOX_CHEST, HITBOX_UPPER_CHEST,
  HITBOX_RIGHT_THIGH, HITBOX_LEFT_THIGH, HITBOX_RIGHT_CALF, HITBOX_LEFT_CALF, HITBOX_RIGHT_FOOT, HITBOX_LEFT_FOOT,
  HITBOX_RIGHT_HAND, HITBOX_LEFT_HAND, HITBOX_RIGHT_UPPER_ARM, HITBOX_RIGHT_FOREARM, HITBOX_LEFT_UPPER_ARM, HITBOX_LEFT_FOREARM,
  HITBOX_MAX
};
