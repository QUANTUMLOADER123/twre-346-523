// sv_transmit.cpp - dormancy: which enemies the local client receives
//
// A mild version of Source's PVS based transmit:
//   - teammates, players seen by a living teammate (radar spotting) and everything while free-roaming as a
//     spectator are always sent
//   - closer than sv_dormant_near: always sent (the PVS right around you is practically everything)
//   - outside the PVS of the viewer's cluster (the map's vvis data): dormant
//   - farther than sv_dormant_los_dist: only while some part of the (padded) player is in line of sight,
//     so ESP doesn't show players far away behind walls
//   - sv_dormant_hold seconds of hysteresis so peeking players don't flicker
#include "sv_transmit.hpp"
#include "../shared/cs_player.hpp"
#include "../shared/gameevents.hpp"
#include "../shared/weapon_csbase.hpp"
#include "../../engine/srcbsp.hpp"
#include "../../engine/random.hpp"
#include "nav_mesh.hpp"
#include <cmath>
#include <cstring>

ConVar sv_dormant("sv_dormant", "1", FCVAR_REPLICATED | FCVAR_NOTIFY, "Enemies outside the client's PVS or far away behind walls are not sent to it (dormant)");
ConVar sv_dormant_near("sv_dormant_near", "800", FCVAR_REPLICATED, "Enemies closer than this are always sent", true, 0, true, 16384);
ConVar sv_dormant_los_dist("sv_dormant_los_dist", "1600", FCVAR_REPLICATED, "Beyond this distance an enemy in the PVS is only sent while in line of sight", true, 0, true, 32768);
ConVar sv_dormant_hold("sv_dormant_hold", "0.5", FCVAR_REPLICATED, "Seconds a player stays sent after he stopped passing the check", true, 0, true, 5);

namespace {

// the point the client looks from: our eyes, or the eyes of the player we spectate; false = sees everything
bool GetViewer(CCSPlayer* lp, Vector& eye, int& team) {
  if (!lp || lp->m_iTeamNum < TEAM_TERRORIST) return false;
  team = lp->m_iTeamNum;
  if (lp->IsAlive()) { eye = lp->EyePosition(); return true; }
  C_BaseEntity* t = lp->m_hObserverTarget;
  if (t && t->IsPlayer() && t != lp) {
    CCSPlayer* tp = static_cast<CCSPlayer*>(t);
    if (tp->IsAlive() && (lp->m_iObserverMode == OBS_MODE_IN_EYE || lp->m_iObserverMode == OBS_MODE_CHASE)) { eye = tp->EyePosition(); team = tp->m_iTeamNum; return true; }
  }
  return false;
}

bool LineOfSight(const Vector& eye, const CCSPlayer* p) {
  const Vector o = p->m_vecOrigin;
  Vector side(o.y - eye.y, eye.x - o.x, 0); float l = side.Length2D(); side = l > 1.f ? side * (30.f / l) : Vector(30, 0, 0);
  const Vector pts[] = { o + Vector(0, 0, 66), o + Vector(0, 0, 40), o + Vector(0, 0, 8), o + side + Vector(0, 0, 40), o - side + Vector(0, 0, 40),
                         o + side + Vector(0, 0, 66), o - side + Vector(0, 0, 66) };
  for (const Vector& pt : pts) {
    trace_t tr; Ray_t ray; ray.Init(eye, pt); CTraceFilterWorldOnly f;
    g_EngineTrace.TraceRay(ray, MASK_VISIBLE, &f, &tr);
    if (tr.fraction >= 0.98f) return true;
  }
  return false;
}

bool InPVS(int viewerCluster, const CCSPlayer* p) {
  if (!g_SrcBsp.loaded || !g_SrcBsp.HasVis() || viewerCluster < 0) return true;
  // like Source, the entity is in the PVS when any leaf it touches is: test feet, center and head
  for (float z : { 4.f, 36.f, 68.f }) {
    int c = g_SrcBsp.PointCluster(p->m_vecOrigin + Vector(0, 0, z));
    if (c < 0 || g_SrcBsp.ClusterVisible(viewerCluster, c)) return true;
  }
  return false;
}

void RememberState(CCSPlayer* p, float now) {
  p->m_vecLastKnownOrigin = p->m_net.valid ? p->m_net.origin : p->m_vecOrigin;
  p->m_flLastKnownTime = now;
  p->m_iLastKnownHealth = p->m_iHealth;
  C_WeaponCSBase* w = p->GetActiveCSWeapon(); p->m_iLastKnownWeapon = w ? w->GetCSWeaponID() : 0;
  p->m_flLastKnownDuck = p->m_flDuckAmount;
}

// sounds of dormant players still reach the client (that's what "sound ESP" uses)
void InstallListener() {
  static bool installed = false;
  if (installed) return; installed = true;
  gameeventmanager->AddListener(nullptr, [](const IGameEvent& ev) {
    const char* n = ev.GetName();
    const bool fire = !strcmp(n, "weapon_fire"), step = !strcmp(n, "player_footstep"), hurt = !strcmp(n, "player_hurt");
    if (!strcmp(n, "player_spawn") || !strcmp(n, "round_start")) {
      for (CCSPlayer* p : UTIL_GetPlayers()) if (!strcmp(n, "round_start") || p->entindex() == ev.GetInt("userid")) {
        p->m_flLastKnownTime = p->m_flSoundTime = p->m_flLastTransmitTime = -100; p->m_bDormant = false;
      }
      return;
    }
    if (!fire && !step && !hurt) return;
    CCSPlayer* p = UTIL_PlayerByIndex(ev.GetInt("userid"));
    if (!p || !p->IsAlive() || !p->IsDormant()) return;
    Vector eye; int team = 0;
    if (!GetViewer(g_pLocalPlayer, eye, team)) return;
    float range = fire ? (ev.GetBool("silenced") ? 900.f : 3000.f) : step ? 1100.f : 1e9f;
    if (hurt) { CCSPlayer* a = UTIL_PlayerByIndex(ev.GetInt("attacker")); if (a != g_pLocalPlayer) return; p->m_iLastKnownHealth = p->m_iHealth; }
    if (eye.DistTo(p->m_vecOrigin) > range) return;
    p->m_vecSoundOrigin = p->m_vecOrigin; p->m_flSoundTime = ServerTime();
  });
}

} // namespace

void SV_UpdateTransmit() {
  InstallListener();
  CCSPlayer* lp = g_pLocalPlayer;
  const float now = ServerTime();
  Vector eye; int team = 0;
  const bool limited = sv_dormant.GetBool() && GetViewer(lp, eye, team);
  const int viewerCluster = limited && g_SrcBsp.loaded ? g_SrcBsp.PointCluster(eye) : -1;
  const float nearDist = sv_dormant_near.GetFloat(), losDist = sv_dormant_los_dist.GetFloat(), hold = sv_dormant_hold.GetFloat();
  for (CCSPlayer* p : UTIL_GetPlayers()) {
    if (p == lp) { p->m_bDormant = false; continue; }
    bool send = true;
    if (limited && p->IsAlive() && p->m_iTeamNum != team && p->m_iTeamNum >= TEAM_TERRORIST) {
      const float dist = eye.DistTo(p->m_vecOrigin + Vector(0, 0, 36));
      if (dist < nearDist || gpGlobals->realtime - p->m_flTeamSpottedTime < 0.3f) send = true;
      else if (!InPVS(viewerCluster, p)) send = false;
      else if (dist < losDist) send = true;
      else if ((gpGlobals->tickcount + p->entindex()) & 1) send = now - p->m_flLastTransmitTime < 0.05f;  // traces every 2nd tick
      else send = LineOfSight(eye, p);
    }
    if (send) p->m_flLastTransmitTime = now;
    const bool dormant = p->IsAlive() && now - p->m_flLastTransmitTime > (send ? 0.f : hold);
    if (!dormant) {
      if (p->m_bDormant) p->m_netPrev = p->m_net;     // back in the PVS: snap, don't interpolate from the old spot
      RememberState(p, now);
    }
    p->m_bDormant = dormant;
  }
}

static ConCommand cmd_dormant_status("dormant_status", [](const CCommand&) {
  CCSPlayer* lp = g_pLocalPlayer;
  Vector eye; int team = 0; bool limited = GetViewer(lp, eye, team);
  Msg("PVS: %s, viewer cluster %d%s\n", g_SrcBsp.HasVis() ? "yes" : "no", limited ? g_SrcBsp.PointCluster(eye) : -1, limited ? "" : " (sees everything)");
  for (CCSPlayer* p : UTIL_GetPlayers(true)) {
    if (p == lp) continue;
    float d = limited ? eye.DistTo(p->m_vecOrigin) : 0;
    Msg("  %-16s team %d dist %5.0f  %s  pvs %d los %d\n", p->GetPlayerName(), p->m_iTeamNum, d, p->IsDormant() ? "DORMANT" : "sent   ",
        limited ? (int)InPVS(g_SrcBsp.PointCluster(eye), p) : 1, limited ? (int)LineOfSight(eye, p) : 1);
  }
}, "Print which players are dormant for the local client");

// dev: random pairs of walkable spots - whenever there is a clear line of sight the PVS must say "visible"
static ConCommand cmd_pvs_selftest("pvs_selftest", [](const CCommand& a) {
  if (!g_SrcBsp.HasVis() || !g_NavMesh.IsBuilt() || g_NavMesh.NumAreas() < 2) { Msg("pvs_selftest: no vis data / nav mesh\n"); return; }
  int n = a.ArgC() > 1 ? atoi(a.Arg(1)) : 20000, los = 0, bad = 0, culled = 0;
  for (int i = 0; i < n; i++) {
    const CNavArea* A = g_NavMesh.GetArea(RandomInt(0, g_NavMesh.NumAreas() - 1));
    const CNavArea* B = g_NavMesh.GetArea(RandomInt(0, g_NavMesh.NumAreas() - 1));
    if (!A || !B || !A->walkable || !B->walkable) continue;
    Vector e = A->center + Vector(0, 0, 64), t = B->center + Vector(0, 0, RandomFloat(4, 68));
    bool vis = g_SrcBsp.ClusterVisible(g_SrcBsp.PointCluster(e), g_SrcBsp.PointCluster(t));
    if (!vis) culled++;
    trace_t tr; Ray_t ray; ray.Init(e, t); CTraceFilterWorldOnly f; g_EngineTrace.TraceRay(ray, MASK_VISIBLE, &f, &tr);
    if (tr.fraction < 1.f) continue;
    los++;
    if (!vis) { bad++; if (bad <= 5) Msg("  mismatch %.0f %.0f %.0f -> %.0f %.0f %.0f\n", e.x, e.y, e.z, t.x, t.y, t.z); }
  }
  Msg("pvs_selftest: %d pairs, %d culled by PVS, %d in line of sight, %d of those culled (must be 0)\n", n, culled, los, bad);
}, "Check the PVS against line-of-sight traces between random walkable spots");
