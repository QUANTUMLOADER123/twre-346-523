#include "lagcompensation.hpp"
#include <cmath>

ConVar sv_unlag("sv_unlag", "1", FCVAR_REPLICATED, "Enables player lag compensation");
ConVar sv_maxunlag("sv_maxunlag", "0.2", FCVAR_REPLICATED, "Maximum lag compensation in seconds", true, 0, true, 1);
ConVar sv_lagcompensation_teleport_dist("sv_lagcompensation_teleport_dist", "64", FCVAR_REPLICATED, "How far a player got moved by game code before we can't lag compensate their position back");
ConVar cl_interp("cl_interp", "0.015625", FCVAR_ARCHIVE, "Sets the interpolation amount (bounded on low side by server interp ratio settings)", true, 0, true, 0.5f);
ConVar sv_showlagcompensation("sv_showlagcompensation", "0", FCVAR_CHEAT, "Show lag compensated hitboxes whenever a player is lag compensated");

float CLagCompensationManager::GetLerpTime() { return cl_interp.GetFloat(); }

bool CLagCompensationManager::IsTickValid(int tickCount) {
  float delta = ServerTime() - TICKS_TO_TIME(tickCount);
  return delta >= -TICK_INTERVAL && delta <= sv_maxunlag.GetFloat();
}

void CLagCompensationManager::FrameUpdatePostEntityThink() {
  float dead = ServerTime() - 1.0f;
  for (CCSPlayer* p : UTIL_GetPlayers()) {
    while (!p->m_LagRecords.empty() && p->m_LagRecords.back().simTime < dead) p->m_LagRecords.pop_back();
  }
}

const CLagRecord* CLagCompensationManager::FindRecord(CCSPlayer* target, int tickCount) const {
  if (!target || target->m_LagRecords.empty()) return nullptr;
  float targetTime = TICKS_TO_TIME(tickCount);
  if (!IsTickValid(tickCount)) return &target->m_LagRecords.front(); // the server ignores bogus tick counts
  const float teleport2 = sv_lagcompensation_teleport_dist.GetFloat() * sv_lagcompensation_teleport_dist.GetFloat();
  const CLagRecord* prev = nullptr;
  for (const CLagRecord& r : target->m_LagRecords) {
    if (prev && (prev->origin - r.origin).Length2DSqr() > teleport2) {
      // lag compensation broken by a too large step (fakelag / teleport): can't rewind past it
      return &target->m_LagRecords.front();
    }
    if (r.simTime <= targetTime + 0.0001f) return &r;
    if (r.simTime < ServerTime() - sv_maxunlag.GetFloat()) break;
    prev = &r;
  }
  return prev ? prev : &target->m_LagRecords.front();
}

void CLagCompensationManager::StartLagCompensation(CCSPlayer* player, const CUserCmd* cmd) {
  if (!sv_unlag.GetBool() || !cmd || m_bActive) return;
  m_bActive = true; m_pCurrentPlayer = player; m_nLastRestoredCount = 0;
  for (CCSPlayer* t : UTIL_GetPlayers(true)) {
    if (t == player) continue;
    const CLagRecord* r = FindRecord(t, cmd->tick_count);
    if (r && r->valid) { t->m_pActiveHitboxes = &r->hitboxes; if (r != &t->m_LagRecords.front()) m_nLastRestoredCount++; }
    else if (!t->m_LagRecords.empty()) { t->m_pActiveHitboxes = &t->m_LagRecords.front().hitboxes; m_nLastBrokenLC++; }
  }
}

void CLagCompensationManager::FinishLagCompensation(CCSPlayer* player) {
  (void)player;
  if (!m_bActive) return;
  for (CCSPlayer* t : UTIL_GetPlayers()) t->m_pActiveHitboxes = nullptr;
  m_bActive = false; m_pCurrentPlayer = nullptr;
}
