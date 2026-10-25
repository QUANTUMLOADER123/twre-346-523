// cs_gamerules.hpp - CCSGameRules: rounds, economy, spawns, win conditions, buy/freeze time
#pragma once
#include "baseentity.hpp"
#include <string>

enum RoundEndReason_t {
  Invalid_Round_End_Reason = -1, Target_Bombed = 0, VIP_Escaped, VIP_Assassinated, Terrorists_Escaped, CTs_PreventEscape,
  Escaping_Terrorists_Neutralized, Bomb_Defused, CTs_Win, Terrorists_Win, Round_Draw, All_Hostages_Rescued, Target_Saved,
  Hostages_Not_Rescued, Terrorists_Not_Escaped, VIP_Not_Escaped, Game_Commencing, Terrorists_Surrender, CTs_Surrender,
};
enum WinStatus { WINNER_NONE = 0, WINNER_DRAW = 1, WINNER_TER = TEAM_TERRORIST, WINNER_CT = TEAM_CT };

class CCSGameRules {
public:
  // state (netvars of cs_gamerules_data)
  bool m_bFreezePeriod = true;
  bool m_bWarmupPeriod = false;
  float m_fWarmupPeriodEnd = 0;
  bool m_bMatchWaitingForResume = false;
  float m_fRoundStartTime = 0;      // when freeze time ends
  float m_flGameStartTime = 0;
  int m_iRoundTime = 115;           // seconds
  float m_flRestartRoundTime = 0;   // 0 = no restart pending
  bool m_bRoundInProgress = false;
  int m_iRoundWinStatus = WINNER_NONE;
  int m_eRoundWinReason = Invalid_Round_End_Reason;
  int m_iNumTerroristWins = 0, m_iNumCTWins = 0;
  int m_iTotalRoundsPlayed = 0;
  int m_iNumConsecutiveCTLoses = 0, m_iNumConsecutiveTerroristLoses = 0;
  int m_iLoserBonus = 1400;
  bool m_bBombPlanted = false, m_bBombDropped = false, m_bTargetBombed = false, m_bBombDefused = false;
  bool m_bMapHasBombTarget = true;
  bool m_bMatchOver = false;
  bool m_bFirstConnected = false;
  bool m_bCompleteReset = false;
  bool m_bGameRestart = false;
  int m_iSpawnPointCount_Terrorist = 0, m_iSpawnPointCount_CT = 0;
  int m_iMatchStats_PlayersAlive_T = 0, m_iMatchStats_PlayersAlive_CT = 0;
  std::string m_szWinMessage;
  float m_flWinMessageTime = -100;
  int m_iFirstKillThisRound = 0;

  void Init();
  void Think();                     // every tick
  void RestartRound();
  void RestartGame();
  void TerminateRound(float delay, int reason);
  void CheckWinConditions();
  void CheckFreezePeriodExpired();
  void CheckRoundTimeExpired();
  void PlayerSpawn(CCSPlayer* p);
  void PlayerKilled(CCSPlayer* victim, const CTakeDamageInfo& info);
  bool FPlayerCanTakeDamage(CCSPlayer* victim, C_BaseEntity* attacker) const;
  bool IsFreezePeriod() const { return m_bFreezePeriod; }
  bool IsWarmupPeriod() const { return m_bWarmupPeriod; }
  bool IsBuyTimeElapsed() const;
  bool IsRoundOver() const { return m_iRoundWinStatus != WINNER_NONE; }
  float GetRoundElapsedTime() const;
  float GetRoundRemainingTime() const;
  float GetFreezeTimeRemaining() const;
  void GiveC4();
  void AddAccountToTeam(int team, int amount, const char* reason);
  void BalanceTeams();
  bool IsDeathmatch() const;
  int GetHumanTeam() const;
  const char* GetRoundEndMessage(int reason) const;
  void OnBombPlanted(CCSPlayer* planter);
  void OnBombDefused(CCSPlayer* defuser);
  void OnBombExploded();
  CCSPlayer* PlayerJoinTeam(CCSPlayer* p, int team);
  bool SelectSpawnPoint(CCSPlayer* p, Vector& origin, float& yaw);
};
inline CCSGameRules g_GameRules;
inline CCSGameRules* CSGameRules() { return &g_GameRules; }
