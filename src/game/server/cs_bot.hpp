// cs_bot.hpp - CCSBot: server side bot AI (buy, navigate, fight, plant/defuse) + bot_hvh mode
#pragma once
#include "../shared/cs_player.hpp"
#include <vector>
#include <string>

enum BotRole { BOTROLE_DEFAULT = 0, BOTROLE_ENTRY, BOTROLE_LURK, BOTROLE_AWP, BOTROLE_ROTATOR };
enum BotBuy { BOTBUY_PISTOL = 0, BOTBUY_ECO, BOTBUY_FORCE, BOTBUY_HALF, BOTBUY_FULL };
enum BotPeek { PEEK_NONE = 0, PEEK_HOLD, PEEK_SWING, PEEK_JIGGLE, PEEK_FLASH };
const char* BotBuyName(int b);
enum BotState { BOT_IDLE = 0, BOT_BUY, BOT_MOVE_TO, BOT_ATTACK, BOT_PLANT, BOT_DEFUSE, BOT_HOLD, BOT_HUNT, BOT_FETCH_BOMB, BOT_INVESTIGATE, BOT_RETREAT };
const char* BotStateName(int s);

class CCSBot {
public:
  explicit CCSBot(CCSPlayer* p);
  void OnSpawn();
  void Upkeep(CUserCmd& cmd);          // build this tick's usercmd
  void OnEvent(const char* name, int userid, int attacker, const Vector& pos);

  CCSPlayer* me;
  int m_iDifficulty = 2;               // 0 easy .. 3 expert
  bool m_bHvH = false;
  int m_iState = BOT_IDLE;
  float m_flStateStart = 0;
  // navigation
  std::vector<Vector> m_Path; std::vector<int> m_PathFlags;
  size_t m_iPathIndex = 0;
  Vector m_vecGoal;
  float m_flRepathTime = 0;
  Vector m_vecLastPos; float m_flStuckTime = 0; float m_flLastProgress = 0;
  int m_iTargetSite = 0;
  Vector m_vecHoldSpot, m_vecHoldLook;
  float m_flHoldUntil = 0;
  // combat
  CCSPlayer* m_pEnemy = nullptr;
  float m_flEnemyFirstSeen = 0, m_flEnemyLastSeen = -100;
  Vector m_vecEnemyLastPos;
  float m_flReactionEnd = 0;
  QAngle m_angView;                    // smoothed aim (non hvh)
  QAngle m_angAimError;
  float m_flNextAimErrorTime = 0;
  float m_flStrafeChange = 0; int m_iStrafeDir = 1;
  bool m_bWantCrouch = false;
  float m_flLastShot = 0;
  int m_iBurst = 0;
  Vector m_vecNoise; float m_flNoiseTime = -100;
  // buying
  bool m_bBoughtThisRound = false;
  float m_flBuyTime = 0;
  float m_flLookAroundTime = 0; float m_flLookYaw = 0;
  bool m_bWantPlant = false;
  // hvh
  int m_nChoke = 0;
  bool m_bJitter = false;
  int m_iDesyncSide = 1;
  float m_flNextSideSwitch = 0;
  int m_iMissed = 0;
  float m_flUnstickUntil = 0;          // stuck detour in progress: goal changes are ignored until then
  // hvh cheat config (varies per bot: fakelag / doubletap / hide shots, different anti-aims)
  int m_iHvHProfile = 0;               // 0 fakelag, 1 doubletap, 2 hide shots, 3 mixed
  int m_iFakelag = 14;                 // choke limit while moving
  int m_iAAMode = 0;                   // 0 jitter, 1 static, 2 3-way, 3 sideways
  float m_flJitter = 12.f, m_flDesync = 120.f;
  float m_flLastHvHShot = -100.f;
  int m_iAAStep = 0;
  bool m_bJumpPressedLast = false;
  CUserCmd m_LastCmd;
  // personality (fixed per bot)
  float m_flAggression = 0.5f;         // 0 = passive angle holder .. 1 = entry fragger
  float m_flBhopSkill = 0.5f;          // chance to bunny hop on long straight runs
  // team play
  int m_iRole = 0;                     // BOTROLE_*
  Vector m_vecStageSpot;
  bool m_bRotating = false; int m_iSeenRotate = -1;
  bool m_bSaving = false;
  bool m_bDropped = false; bool m_bBuyWaited = false;
  int m_iPlanSerial = -1;
  // movement
  bool m_bWalk = false;                // shift-walk (no footsteps)
  bool m_bBhopping = false; int m_iAirSide = 1; int m_iAirSideTicks = 0;
  bool m_bRushing = false;
  // enemy lost: how to re-peek
  int m_iPeek = 0; float m_flPeekUntil = 0; float m_flPeekPhase = 0; int m_iJiggle = 0;
  bool m_bEnemyVisible = false;
  // fight cycle (strafe -> stop -> shoot)
  int m_iFightPhase = 0; float m_flPhaseEnd = 0; int m_iPhaseShots = 0; int m_iPhaseShotsMax = 0;
  bool m_bCrouchSpray = false;
  bool m_bHumanLike = false;  // human-like: smooth aim, crouch before peek, wait in corners
  // retreat to cover (reload / low hp)
  Vector m_vecCover; float m_flRetreatUntil = 0;
  // grenades
  int m_iThrowPhase = 0, m_iThrowNade = 0; QAngle m_angThrow; float m_flThrowPhaseTime = 0, m_flNextThrow = 0;
  bool m_bAfterThrowPeek = false;

private:
  void UpdatePerception();
  void Buy();
  void ThinkGoals();
  void MoveTowardsGoal(CUserCmd& cmd, QAngle& viewAngles);
  void Bhop(CUserCmd& cmd, QAngle& view, const Vector& target);
  void CounterStrafe(CUserCmd& cmd, const QAngle& view) const;
  bool PressFire(CUserCmd& cmd, C_WeaponCSBase* w);
  bool EnemyLost(CUserCmd& cmd, QAngle& view);
  bool StartThrow(int nadeId, const Vector& target, float maxError, bool requireHidden);
  bool RunThrow(CUserCmd& cmd, QAngle& view);
  void ThinkUtility();
  bool FindCover(const Vector& threat, Vector& out) const;
  Vector PickAngleSpot(const Vector& center, float radius, const Vector& lookAt, float minDist) const;
  bool ThinkPickup();
  void ThinkSave();
  void Fight(CUserCmd& cmd, QAngle& view);
  void HvHAim(CUserCmd& cmd, QAngle& view);
  bool HvHRevolver(CUserCmd& cmd);         // auto revolver: keeps the R8 cocked, true = a shot fires instantly
  void PickHvHProfile();
  void ApplyHvHAntiAim(CUserCmd& cmd, const QAngle& view, bool shooting);
  bool SetGoal(const Vector& goal, bool force = false);
  bool IsVisible(CCSPlayer* other, Vector* aimPoint) const;
  void SetState(int s);
  void EquipBestWeapon();
};

void Bot_AddBot(int team, const char* name = nullptr);
void Bot_KickAll();
void Bot_RunAll(); // build commands for every bot
void Bot_Init();
CCSBot* GetBot(CCSPlayer* p);
