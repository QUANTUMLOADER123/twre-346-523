// cs_gamerules.cpp - CCSGameRules
#include "cs_gamerules.hpp"
#include "cs_player.hpp"
#include "weapon_csbase.hpp"
#include "cs_projectiles.hpp"
#include "gameevents.hpp"
#include "soundemitter.hpp"
#include "mapinfo.hpp"
#include "../../engine/random.hpp"
#include <algorithm>
#include <cmath>
#include <utility>

ConVar mp_startmoney("mp_startmoney", "800", FCVAR_REPLICATED | FCVAR_NOTIFY, "Amount of money each player gets when they reset", true, 0, true, 65535);
ConVar mp_freezetime("mp_freezetime", "6", FCVAR_REPLICATED | FCVAR_NOTIFY, "How many seconds to keep players frozen when the round starts", true, 0, true, 60);
ConVar mp_roundtime("mp_roundtime", "1.92", FCVAR_REPLICATED | FCVAR_NOTIFY, "How many minutes each round takes.", true, 0.25f, true, 60);
ConVar mp_round_restart_delay("mp_round_restart_delay", "5", FCVAR_REPLICATED, "Number of seconds to delay before restarting a round after a win", true, 0, true, 14);
ConVar mp_maxrounds("mp_maxrounds", "30", FCVAR_REPLICATED | FCVAR_NOTIFY, "max number of rounds to play before server changes maps", true, 0, false, 0);
ConVar mp_halftime("mp_halftime", "1", FCVAR_REPLICATED | FCVAR_NOTIFY, "Determines whether the match switches sides in a halftime event.");
ConVar mp_gamemode("mp_gamemode", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "0 = competitive (bomb defusal), 1 = deathmatch");
ConVar mp_respawn_on_death_ct("mp_respawn_on_death_ct", "0", FCVAR_REPLICATED, "When set to 1, counter-terrorists will respawn after dying.");
ConVar mp_respawn_on_death_t("mp_respawn_on_death_t", "0", FCVAR_REPLICATED, "When set to 1, terrorists will respawn after dying.");
ConVar mp_respawnwavetime("mp_respawnwavetime", "2", FCVAR_REPLICATED, "Time between respawn waves.");
ConVar mp_ignore_round_win_conditions("mp_ignore_round_win_conditions", "0", FCVAR_REPLICATED, "Ignore conditions which would end the current round");
ConVar mp_give_player_c4("mp_give_player_c4", "1", FCVAR_REPLICATED, "Whether this map should spawn a c4 bomb for a player or not.");
ConVar mp_restartgame("mp_restartgame", "0", FCVAR_GAMEDLL, "If non-zero, game will restart in the specified number of seconds",
  true, 0, true, 60, [](ConVar* v, const char*) { if (v->GetInt() > 0) { CSGameRules()->m_flRestartRoundTime = ServerTime() + v->GetFloat(); CSGameRules()->m_bCompleteReset = true; Msg("Game will restart in %d seconds\n", v->GetInt()); } });
ConVar mp_death_drop_gun("mp_death_drop_gun", "1", FCVAR_REPLICATED, "Drop gun on death");
ConVar mp_autoteambalance("mp_autoteambalance", "1", FCVAR_REPLICATED, "");

int mp_maxmoney_value();
static constexpr int ROUND_WIN_ELIMINATION = 3250, ROUND_WIN_BOMB = 3500, ROUND_WIN_TIME = 3250, BOMB_PLANT_BONUS = 800, PLANTER_BONUS = 300, DEFUSER_BONUS = 300;

bool CCSGameRules::IsDeathmatch() const { return mp_gamemode.GetInt() == 1; }

void CCSGameRules::Init() {
  *this = CCSGameRules();
  m_flGameStartTime = ServerTime();
  m_bMapHasBombTarget = g_MapInfo.FindZone(ZONE_BOMBSITE_A) != nullptr;
  m_iSpawnPointCount_CT = m_iSpawnPointCount_Terrorist = 0;
  for (auto& s : g_MapInfo.spawns) (s.team == TEAM_CT ? m_iSpawnPointCount_CT : m_iSpawnPointCount_Terrorist)++;
  m_bCompleteReset = true;
  m_flRestartRoundTime = ServerTime() + 1.6f; // let the bots connect first
  m_bFreezePeriod = true;
  m_iRoundWinStatus = WINNER_DRAW; // until the first restart
}

bool CCSGameRules::SelectSpawnPoint(CCSPlayer* p, Vector& origin, float& yaw) {
  std::vector<const MapSpawn*> list;
  for (auto& s : g_MapInfo.spawns) if (IsDeathmatch() || s.team == p->m_iTeamNum) list.push_back(&s);
  if (list.empty()) for (auto& s : g_MapInfo.spawns) list.push_back(&s);
  if (list.empty()) { origin = Vector(0, 0, 64); yaw = 0; return false; }
  int start = IsDeathmatch() ? RandomInt(0, (int)list.size() - 1) : 0;
  const MapSpawn* best = nullptr; float bestScore = -1e9f;
  for (size_t k = 0; k < list.size(); k++) {
    const MapSpawn* s = list[(start + k) % list.size()];
    float minDist = 1e9f, minEnemy = 1e9f;
    for (CCSPlayer* o : UTIL_GetPlayers(true)) {
      if (o == p) continue;
      float d = o->m_vecOrigin.DistTo(s->origin);
      minDist = std::min(minDist, d);
      if (o->m_iTeamNum != p->m_iTeamNum) minEnemy = std::min(minEnemy, d);
    }
    if (minDist < 40.f) continue;
    float score = IsDeathmatch() ? std::min(minEnemy, 1500.f) + RandomFloat(0, 400) : -(float)k;
    if (score > bestScore) { bestScore = score; best = s; }
  }
  if (!best) best = list[RandomInt(0, (int)list.size() - 1)];
  origin = best->origin; yaw = best->yaw;
  return true;
}

void CCSGameRules::PlayerSpawn(CCSPlayer* p) {
  if (IsDeathmatch()) { p->m_iAccount = mp_maxmoney_value(); }
}
int mp_maxmoney_value() { ConVar* v = FindConVar("mp_maxmoney"); return v ? v->GetInt() : 16000; }

CCSPlayer* CCSGameRules::PlayerJoinTeam(CCSPlayer* p, int team) {
  if (team == p->m_iTeamNum) return p;
  if (p->IsAlive()) { CTakeDamageInfo info; info.m_flDamage = 1000; p->m_lifeState = LIFE_ALIVE; p->Event_Killed(info); p->m_iDeaths--; }
  p->RemoveAllItems(true);
  p->m_iTeamNum = team;
  p->m_iPlayerModel = RandomInt(0, 1);
  IGameEvent ev("player_team"); ev.SetInt("userid", p->entindex()).SetInt("team", team); gameeventmanager->FireEvent(ev);
  ConColorMsg({ 230, 230, 230, 255 }, "%s is joining the %s\n", p->GetPlayerName(), team == TEAM_CT ? "Counter-Terrorist force" : team == TEAM_TERRORIST ? "Terrorist force" : "Spectators");
  // during freeze time / deathmatch you spawn right away
  if (team >= TEAM_TERRORIST && (m_bFreezePeriod || IsDeathmatch() || GetRoundElapsedTime() < 10.f)) p->Spawn();
  return p;
}

float CCSGameRules::GetRoundElapsedTime() const { return ServerTime() - m_fRoundStartTime; }
float CCSGameRules::GetRoundRemainingTime() const { return (float)m_iRoundTime - GetRoundElapsedTime(); }
float CCSGameRules::GetFreezeTimeRemaining() const { return std::max(0.f, m_fRoundStartTime - ServerTime()); }
bool CCSGameRules::IsBuyTimeElapsed() const {
  if (IsDeathmatch()) return false;
  ConVar* bt = FindConVar("mp_buytime");
  return GetRoundElapsedTime() > (bt ? bt->GetFloat() : 20.f);
}
bool CCSGameRules::FPlayerCanTakeDamage(CCSPlayer* victim, C_BaseEntity* attacker) const {
  CCSPlayer* a = ToCSPlayer(attacker);
  if (!a || a == victim) return true;
  if (a->m_iTeamNum != victim->m_iTeamNum) return true;
  ConVar* ff = FindConVar("mp_friendlyfire");
  return ff && ff->GetBool();
}

void CCSGameRules::AddAccountToTeam(int team, int amount, const char* reason) {
  int maxm = mp_maxmoney_value();
  for (CCSPlayer* p : UTIL_GetPlayers()) if (p->m_iTeamNum == team) p->m_iAccount = std::min(maxm, p->m_iAccount + amount);
  (void)reason;
}

void CCSGameRules::GiveC4() {
  if (!m_bMapHasBombTarget || !mp_give_player_c4.GetBool() || IsDeathmatch()) return;
  std::vector<CCSPlayer*> ts;
  for (CCSPlayer* p : UTIL_GetPlayers(true)) if (p->m_iTeamNum == TEAM_TERRORIST) ts.push_back(p);
  if (ts.empty()) return;
  // prefer bots over the human so the human doesn't always have to carry it
  CCSPlayer* pick = ts[RandomInt(0, (int)ts.size() - 1)];
  pick->GiveWeapon(WEAPON_C4);
}

void CCSGameRules::BalanceTeams() {
  if (!mp_autoteambalance.GetBool()) return;
  std::vector<CCSPlayer*> t, ct;
  for (CCSPlayer* p : UTIL_GetPlayers()) { if (p->m_iTeamNum == TEAM_TERRORIST) t.push_back(p); else if (p->m_iTeamNum == TEAM_CT) ct.push_back(p); }
  while (t.size() > ct.size() + 1) { CCSPlayer* b = nullptr; for (auto* p : t) if (p->IsBot()) b = p; if (!b) break; t.erase(std::find(t.begin(), t.end(), b)); b->m_iTeamNum = TEAM_CT; b->RemoveAllItems(true); ct.push_back(b); }
  while (ct.size() > t.size() + 1) { CCSPlayer* b = nullptr; for (auto* p : ct) if (p->IsBot()) b = p; if (!b) break; ct.erase(std::find(ct.begin(), ct.end(), b)); b->m_iTeamNum = TEAM_TERRORIST; b->RemoveAllItems(true); t.push_back(b); }
}

void CCSGameRules::RestartGame() {
  m_iNumTerroristWins = m_iNumCTWins = 0; m_iTotalRoundsPlayed = 0;
  m_iNumConsecutiveCTLoses = m_iNumConsecutiveTerroristLoses = 0;
  m_bMatchOver = false;
  for (CCSPlayer* p : UTIL_GetPlayers()) p->Reset(false);
}

void CCSGameRules::RestartRound() {
  if (m_bCompleteReset) { RestartGame(); m_bCompleteReset = false; }
  // halftime: swap teams
  int maxr = mp_maxrounds.GetInt();
  if (mp_halftime.GetBool() && maxr > 0 && m_iTotalRoundsPlayed == maxr / 2 && m_iTotalRoundsPlayed > 0 && !m_bGameRestart) {
    std::swap(m_iNumCTWins, m_iNumTerroristWins);
    for (CCSPlayer* p : UTIL_GetPlayers()) {
      if (p->m_iTeamNum == TEAM_CT) p->m_iTeamNum = TEAM_TERRORIST; else if (p->m_iTeamNum == TEAM_TERRORIST) p->m_iTeamNum = TEAM_CT;
      p->RemoveAllItems(true); p->m_iAccount = mp_startmoney.GetInt();
    }
    m_iNumConsecutiveCTLoses = m_iNumConsecutiveTerroristLoses = 0;
    ConColorMsg({ 255, 220, 120, 255 }, "*** Halftime: teams switched sides ***\n");
    IGameEvent ev("announce_phase_end"); gameeventmanager->FireEvent(ev);
  }
  BalanceTeams();
  // remove world entities (dropped weapons, projectiles, bomb)
  g_EntityList.ForEach([](C_BaseEntity* e) {
    if (e->IsPlayer()) return;
    if (e->IsWeapon()) { auto* w = static_cast<C_WeaponCSBase*>(e); if (w->m_iState != WEAPON_NOT_CARRIED) return; }
    e->Remove();
  });
  g_pPlantedC4 = nullptr;
  GetActiveSmokes().clear();
  for (CCSPlayer* p : UTIL_GetPlayers()) { if (C_WeaponCSBase* c4 = p->Weapon_OwnsThisType(WEAPON_C4)) { for (auto*& s : p->m_hMyWeapons) if (s == c4) s = nullptr; if (p->m_hActiveWeapon == c4) p->m_hActiveWeapon = nullptr; c4->Remove(); } }
  g_EntityList.CleanupDeleted();
  m_bBombPlanted = m_bBombDropped = m_bTargetBombed = m_bBombDefused = false;
  m_iRoundWinStatus = WINNER_NONE; m_eRoundWinReason = Invalid_Round_End_Reason;
  m_iRoundTime = (int)(mp_roundtime.GetFloat() * 60.f);
  m_bFreezePeriod = !IsDeathmatch() && mp_freezetime.GetFloat() > 0;
  m_fRoundStartTime = ServerTime() + (m_bFreezePeriod ? mp_freezetime.GetFloat() : 0.f);
  m_flRestartRoundTime = 0;
  m_bRoundInProgress = true;
  m_iFirstKillThisRound = 0;
  m_bGameRestart = false;
  // spawn everyone on a team
  std::vector<CCSPlayer*> players = UTIL_GetPlayers();
  // CT first then T so spawn selection is stable
  for (CCSPlayer* p : players) if (p->m_iTeamNum >= TEAM_TERRORIST) {
    if (!p->IsAlive()) p->RemoveAllItems(false); // dead players lose their gear
    p->Spawn();
  }
  GiveC4();
  IGameEvent ev("round_start"); ev.SetInt("timelimit", m_iRoundTime).SetInt("fraglimit", 0); gameeventmanager->FireEvent(ev);
  if (!m_bFreezePeriod) { IGameEvent fe("round_freeze_end"); gameeventmanager->FireEvent(fe); }
  ConColorMsg({ 180, 220, 255, 255 }, "--- Round %d ---\n", m_iTotalRoundsPlayed + 1);
}

const char* CCSGameRules::GetRoundEndMessage(int reason) const {
  switch (reason) {
    case Target_Bombed: return "Target Successfully Bombed!";
    case Bomb_Defused: return "The bomb has been defused!";
    case CTs_Win: return "Counter-Terrorists Win!";
    case Terrorists_Win: return "Terrorists Win!";
    case Round_Draw: return "Round Draw!";
    case Target_Saved: return "Target has been saved!";
    case Game_Commencing: return "Game Commencing!";
    default: return "";
  }
}

void CCSGameRules::TerminateRound(float delay, int reason) {
  if (IsRoundOver()) return;
  int winner = WINNER_DRAW;
  switch (reason) {
    case Target_Bombed: case Terrorists_Win: winner = WINNER_TER; break;
    case Bomb_Defused: case CTs_Win: case Target_Saved: winner = WINNER_CT; break;
    default: winner = WINNER_DRAW; break;
  }
  m_iRoundWinStatus = winner; m_eRoundWinReason = reason;
  m_flRestartRoundTime = ServerTime() + delay;
  m_szWinMessage = GetRoundEndMessage(reason); m_flWinMessageTime = ServerTime();
  m_bRoundInProgress = false;
  if (reason != Game_Commencing && reason != Round_Draw) {
    m_iTotalRoundsPlayed++;
    int reward = (reason == Target_Bombed || reason == Bomb_Defused) ? ROUND_WIN_BOMB : (reason == Target_Saved ? ROUND_WIN_TIME : ROUND_WIN_ELIMINATION);
    int loserTeam = winner == WINNER_CT ? TEAM_TERRORIST : TEAM_CT;
    int& lossStreak = loserTeam == TEAM_CT ? m_iNumConsecutiveCTLoses : m_iNumConsecutiveTerroristLoses;
    int& winStreak = loserTeam == TEAM_CT ? m_iNumConsecutiveTerroristLoses : m_iNumConsecutiveCTLoses;
    winStreak = std::max(0, winStreak - 1);
    int lossBonus = std::min(3400, 1400 + 500 * lossStreak);
    lossStreak = std::min(lossStreak + 1, 4);
    if (winner == WINNER_CT) m_iNumCTWins++; else m_iNumTerroristWins++;
    AddAccountToTeam(winner, reward, "round win");
    // Ts who survive a time-out loss get nothing
    if (!(reason == Target_Saved && loserTeam == TEAM_TERRORIST)) AddAccountToTeam(loserTeam, lossBonus + (loserTeam == TEAM_TERRORIST && m_bBombPlanted ? BOMB_PLANT_BONUS : 0), "loss bonus");
    else for (CCSPlayer* p : UTIL_GetPlayers()) if (p->m_iTeamNum == TEAM_TERRORIST && !p->IsAlive()) p->m_iAccount = std::min(mp_maxmoney_value(), p->m_iAccount + lossBonus);
    // MVP: most kills on the winning team
    CCSPlayer* mvp = nullptr;
    for (CCSPlayer* p : UTIL_GetPlayers()) if (p->m_iTeamNum == winner && (!mvp || p->m_iNumRoundKills > mvp->m_iNumRoundKills)) mvp = p;
    if (mvp && mvp->m_iNumRoundKills > 0) mvp->m_iMVPs++;
    int maxr = mp_maxrounds.GetInt();
    if (maxr > 0 && (m_iNumCTWins > maxr / 2 || m_iNumTerroristWins > maxr / 2 || m_iTotalRoundsPlayed >= maxr)) {
      m_bMatchOver = true; m_flRestartRoundTime = ServerTime() + 10.f; m_bCompleteReset = true;
      m_szWinMessage = m_iNumCTWins > m_iNumTerroristWins ? "Counter-Terrorists win the match!" : m_iNumCTWins < m_iNumTerroristWins ? "Terrorists win the match!" : "The match ended in a draw";
    }
  }
  const char* snd = winner == WINNER_CT ? "Radio.CTWin" : winner == WINNER_TER ? "Radio.TerWin" : "Radio.RoundDraw";
  if (reason == Bomb_Defused) snd = "Radio.BombDefused"; else if (reason == Target_Bombed) snd = "Radio.TargetBombed";
  EmitSound2D(snd);
  IGameEvent ev("round_end"); ev.SetInt("winner", winner).SetInt("reason", reason).SetString("message", m_szWinMessage); gameeventmanager->FireEvent(ev);
  ConColorMsg({ 255, 230, 140, 255 }, "%s  (CT %d : %d T)\n", m_szWinMessage.c_str(), m_iNumCTWins, m_iNumTerroristWins);
}

void CCSGameRules::CheckFreezePeriodExpired() {
  if (!m_bFreezePeriod) return;
  if (ServerTime() < m_fRoundStartTime) return;
  m_bFreezePeriod = false;
  // make sure somebody carries the bomb (players may have joined after the round restart)
  if (m_bMapHasBombTarget && !m_bBombPlanted && !IsDeathmatch()) {
    bool have = false;
    g_EntityList.ForEach([&](C_BaseEntity* e) { if (e->IsWeapon() && static_cast<C_WeaponCSBase*>(e)->GetCSWeaponID() == WEAPON_C4) have = true; });
    if (!have) GiveC4();
  }
  IGameEvent ev("round_freeze_end"); gameeventmanager->FireEvent(ev);
  EmitSound2D("Radio.LetsGo");
}
void CCSGameRules::CheckRoundTimeExpired() {
  if (IsDeathmatch() || IsRoundOver() || m_bFreezePeriod || m_bBombPlanted) return;
  if (GetRoundRemainingTime() > 0) return;
  TerminateRound(mp_round_restart_delay.GetFloat(), m_bMapHasBombTarget ? Target_Saved : Round_Draw);
}
void CCSGameRules::CheckWinConditions() {
  if (IsRoundOver() || IsDeathmatch() || mp_ignore_round_win_conditions.GetBool()) return;
  int tAlive = 0, ctAlive = 0, tTotal = 0, ctTotal = 0;
  for (CCSPlayer* p : UTIL_GetPlayers()) {
    if (p->m_iTeamNum == TEAM_TERRORIST) { tTotal++; if (p->IsAlive()) tAlive++; }
    else if (p->m_iTeamNum == TEAM_CT) { ctTotal++; if (p->IsAlive()) ctAlive++; }
  }
  m_iMatchStats_PlayersAlive_T = tAlive; m_iMatchStats_PlayersAlive_CT = ctAlive;
  if (tTotal == 0 || ctTotal == 0) return; // need both teams
  if (ctAlive == 0 && tTotal > 0) { TerminateRound(mp_round_restart_delay.GetFloat(), Terrorists_Win); return; }
  if (tAlive == 0 && !m_bBombPlanted) { TerminateRound(mp_round_restart_delay.GetFloat(), CTs_Win); return; }
}

void CCSGameRules::OnBombPlanted(CCSPlayer* planter) {
  m_bBombPlanted = true;
  planter->m_iAccount = std::min(mp_maxmoney_value(), planter->m_iAccount + PLANTER_BONUS);
  planter->m_iScore += 2;
  EmitSound2D("Radio.BombPlanted");
  IGameEvent ev("bomb_planted"); ev.SetInt("userid", planter->entindex()).SetInt("site", g_pPlantedC4 ? g_pPlantedC4->m_nBombSite : 0); gameeventmanager->FireEvent(ev);
  ConColorMsg({ 255, 120, 80, 255 }, "The bomb has been planted.\n");
}
void CCSGameRules::OnBombDefused(CCSPlayer* defuser) {
  m_bBombDefused = true;
  defuser->m_iAccount = std::min(mp_maxmoney_value(), defuser->m_iAccount + DEFUSER_BONUS);
  defuser->m_iScore += 2;
  IGameEvent ev("bomb_defused"); ev.SetInt("userid", defuser->entindex()); gameeventmanager->FireEvent(ev);
  TerminateRound(mp_round_restart_delay.GetFloat(), Bomb_Defused);
}
void CCSGameRules::OnBombExploded() {
  m_bTargetBombed = true;
  // the round ends even if the round was already decided? (CS: if CTs all died before, T already won)
  if (!IsRoundOver()) TerminateRound(mp_round_restart_delay.GetFloat(), Target_Bombed);
}

void CCSGameRules::PlayerKilled(CCSPlayer* victim, const CTakeDamageInfo& info) {
  CCSPlayer* attacker = ToCSPlayer(info.m_hAttacker);
  int weaponId = info.m_iWeaponID;
  bool headshot = info.m_iHitGroup == HITGROUP_HEAD;
  if (attacker && attacker != victim) {
    if (attacker->m_iTeamNum == victim->m_iTeamNum) {
      attacker->m_iKills--; attacker->m_iScore -= 1;
      attacker->m_iAccount = std::max(0, attacker->m_iAccount - 300);
    } else {
      attacker->m_iKills++; attacker->m_iScore += 2; attacker->m_iNumRoundKills++;
      if (headshot) { attacker->m_iHeadshots++; attacker->m_iNumRoundKillsHeadshots++; }
      const CCSWeaponInfo* wi = GetWeaponInfo(weaponId);
      int award = wi ? wi->killAward : 300;
      if (weaponId == WEAPON_HEGRENADE || weaponId == WEAPON_MOLOTOV || weaponId == WEAPON_INCGRENADE) award = 300;
      attacker->m_iAccount = std::min(mp_maxmoney_value(), attacker->m_iAccount + (IsDeathmatch() ? 0 : award));
    }
  } else if (!attacker || attacker == victim) {
    victim->m_iScore -= 1; victim->m_iKills -= (info.m_bitsDamageType & DMG_FALL) ? 0 : 0;
  }
  // assist: another enemy dealt >= 41 damage
  CCSPlayer* assister = nullptr;
  for (CCSPlayer* p : UTIL_GetPlayers()) {
    if (p == attacker || p == victim || p->m_iTeamNum == victim->m_iTeamNum) continue;
    if (p->m_iDamageDealt[victim->entindex()] >= 41) { assister = p; break; }
  }
  if (assister) { assister->m_iAssists++; assister->m_iScore += 1; }
  for (CCSPlayer* p : UTIL_GetPlayers()) p->m_iDamageDealt[victim->entindex()] = 0;
  if (!m_iFirstKillThisRound) m_iFirstKillThisRound = victim->entindex();
  IGameEvent ev("player_death");
  ev.SetInt("userid", victim->entindex()).SetInt("attacker", attacker ? attacker->entindex() : 0).SetInt("assister", assister ? assister->entindex() : 0)
    .SetString("weapon", info.m_bitsDamageType & DMG_FALL ? "world" : WeaponIdToAlias(weaponId)).SetBool("headshot", headshot)
    .SetInt("penetrated", info.m_iPenetrationCount).SetBool("noscope", info.m_bNoScope).SetBool("thrusmoke", info.m_bThroughSmoke).SetBool("attackerblind", info.m_bAttackerBlind)
    .SetInt("hitbox", info.m_iHitbox);
  gameeventmanager->FireEvent(ev);
  CheckWinConditions();
}

void CCSGameRules::Think() {
  float now = ServerTime();
  if (m_flRestartRoundTime > 0 && now >= m_flRestartRoundTime) { RestartRound(); return; }
  CheckFreezePeriodExpired();
  CheckRoundTimeExpired();
  CheckWinConditions();
  // deathmatch / respawn modes
  bool dm = IsDeathmatch();
  for (CCSPlayer* p : UTIL_GetPlayers()) {
    if (p->IsAlive() || p->m_iTeamNum < TEAM_TERRORIST) continue;
    bool respawn = dm || (p->m_iTeamNum == TEAM_CT && mp_respawn_on_death_ct.GetBool()) || (p->m_iTeamNum == TEAM_TERRORIST && mp_respawn_on_death_t.GetBool());
    if (!respawn) continue;
    if (p->m_flDeathTime > 0 && now > p->m_flDeathTime + mp_respawnwavetime.GetFloat()) p->Spawn();
    else if (p->m_flDeathTime <= 0) p->Spawn();
  }
  // nobody on a team: keep the round "commencing"
  if (m_iRoundWinStatus == WINNER_NONE && !dm) {
    bool anyT = false, anyCT = false;
    for (CCSPlayer* p : UTIL_GetPlayers()) { anyT |= p->m_iTeamNum == TEAM_TERRORIST; anyCT |= p->m_iTeamNum == TEAM_CT; }
    (void)anyT; (void)anyCT;
  }
}
