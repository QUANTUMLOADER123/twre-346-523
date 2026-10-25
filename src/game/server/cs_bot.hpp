// cs_bot.hpp - CCSBot: server side bot AI (buy, navigate, fight, plant/defuse) + bot_hvh mode
#pragma once
#include "../shared/cs_player.hpp"
#include <vector>
#include <string>

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
  bool m_bJumpPressedLast = false;
  CUserCmd m_LastCmd;

private:
  void UpdatePerception();
  void Buy();
  void ThinkGoals();
  void MoveTowardsGoal(CUserCmd& cmd, const QAngle& viewAngles);
  void Fight(CUserCmd& cmd, QAngle& view);
  void HvHAim(CUserCmd& cmd, QAngle& view);
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
