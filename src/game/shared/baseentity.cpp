#include "baseentity.hpp"
#include "cs_player.hpp"
#include <algorithm>

C_BaseEntity::C_BaseEntity() { m_flCreateTime = gpGlobals->curtime; }

bool C_BaseEntity::IsSolidForTrace(unsigned) const { return false; }

void CBaseEntityList::Clear() {
  for (auto* e : m_ents) delete e;
  m_ents.clear(); traceList.clear();
}
int CBaseEntityList::Add(C_BaseEntity* e, int forceIndex) {
  if (forceIndex >= 0) {
    if ((int)m_ents.size() <= forceIndex) m_ents.resize(forceIndex + 1, nullptr);
    m_ents[forceIndex] = e; e->m_iIndex = forceIndex; return forceIndex;
  }
  // players occupy 1..MAX_PLAYERS, everything else after
  int start = e->IsPlayer() ? 1 : MAX_PLAYERS + 1;
  int end = e->IsPlayer() ? MAX_PLAYERS + 1 : 1 << 20;
  for (int i = start; i < end; i++) {
    if (i >= (int)m_ents.size()) m_ents.resize(i + 1, nullptr);
    if (!m_ents[i]) { m_ents[i] = e; e->m_iIndex = i; return i; }
  }
  return -1;
}
CCSPlayer* CBaseEntityList::GetPlayer(int idx) const {
  C_BaseEntity* e = Get(idx);
  return (e && e->IsPlayer()) ? static_cast<CCSPlayer*>(e) : nullptr;
}
void CBaseEntityList::CleanupDeleted() {
  for (size_t i = 0; i < m_ents.size(); i++) {
    C_BaseEntity* e = m_ents[i];
    if (e && e->m_bMarkedForDeletion) {
      // clear dangling handles
      for (auto* o : m_ents) if (o && o != e) {
        if (o->m_hOwnerEntity == e) o->m_hOwnerEntity = nullptr;
        if (o->m_hGroundEntity == e) o->m_hGroundEntity = nullptr;
        if (o->IsPlayer()) {
          auto* p = static_cast<CCSPlayer*>(o);
          for (auto*& w : p->m_hMyWeapons) if ((C_BaseEntity*)w == e) w = nullptr;
          if ((C_BaseEntity*)p->m_hActiveWeapon == e) p->m_hActiveWeapon = nullptr;
          if ((C_BaseEntity*)p->m_hLastWeapon == e) p->m_hLastWeapon = nullptr;
          if (p->m_hObserverTarget == e) p->m_hObserverTarget = nullptr;
          if (p->m_hLastAttacker == e) p->m_hLastAttacker = nullptr;
        }
      }
      delete e; m_ents[i] = nullptr;
    }
  }
  RebuildTraceList();
}
void CBaseEntityList::RebuildTraceList() {
  traceList.clear();
  for (auto* e : m_ents) if (e && !e->m_bMarkedForDeletion && (e->IsPlayer())) traceList.push_back(e);
}

CCSPlayer* UTIL_PlayerByIndex(int idx) { return g_EntityList.GetPlayer(idx); }
std::vector<CCSPlayer*> UTIL_GetPlayers(bool aliveOnly) {
  std::vector<CCSPlayer*> out;
  for (int i = 1; i <= CBaseEntityList::MAX_PLAYERS; i++) {
    CCSPlayer* p = g_EntityList.GetPlayer(i);
    if (p && !p->m_bMarkedForDeletion && (!aliveOnly || p->IsAlive())) out.push_back(p);
  }
  return out;
}
void UTIL_TraceLine(const Vector& a, const Vector& b, unsigned mask, const IHandleEntity* ignore, trace_t* tr) { g_EngineTrace.TraceLine(a, b, mask, ignore, tr); }
void UTIL_TraceHull(const Vector& a, const Vector& b, const Vector& mn, const Vector& mx, unsigned mask, const IHandleEntity* ignore, trace_t* tr) { g_EngineTrace.TraceHull(a, b, mn, mx, mask, ignore, tr); }
