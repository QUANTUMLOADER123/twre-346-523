// hostage.cpp - CHostage: spawn, carry, rescue, damage, render
#include "hostage.hpp"
#include "cs_player.hpp"
#include "cs_gamerules.hpp"
#include "mapinfo.hpp"
#include "gameevents.hpp"
#include "soundemitter.hpp"
#include "../client/css_models.hpp"
#include "../../engine/random.hpp"
#include "../../engine/srcfs.hpp"
#include "../../engine/srcmdl.hpp"
#include "../../engine/view.hpp"
#include <algorithm>

ConVar mp_hostages("mp_hostages", "1", FCVAR_REPLICATED | FCVAR_NOTIFY, "Spawn hostages for the CTs to rescue (0 = off)");
ConVar mp_hostage_count("mp_hostage_count", "4", FCVAR_REPLICATED | FCVAR_NOTIFY, "Number of hostages to spawn each round", true, 0, true, 8);
ConVar cl_hostage_debug_flat("cl_hostage_debug_flat", "0", FCVAR_CHEAT, "Debug: draw hostages as flat magenta");

CHostage* ToHostage(C_BaseEntity* e) { return (e && e->IsHostage()) ? static_cast<CHostage*>(e) : nullptr; }

const char* CHostage::ModelPath(int variant) {
  static const char* k[] = {
    "models/characters/hostage_01.mdl", "models/characters/hostage_02.mdl",
    "models/characters/hostage_03.mdl", "models/characters/hostage_04.mdl",
  };
  return k[std::max(0, std::min(3, variant))];
}

CHostage::CHostage() { m_iClassname = "hostage"; }

void CHostage::Spawn() {
  m_iClassname = "hostage";
  m_iEntClass = ENTCLASS_HOSTAGE;
  m_iTeamNum = TEAM_UNASSIGNED;
  m_iHealth = 100; m_iMaxHealth = 100;
  m_lifeState = LIFE_ALIVE;
  m_takedamage = 1;
  m_vecMins = Vector(-12, -12, 0);
  m_vecMaxs = Vector(12, 12, 64);
  m_vecOldOrigin = m_vecOrigin;
  m_flAnimTime = gpGlobals->curtime;
}

CHostage* CHostage::SpawnAt(const Vector& origin, float yaw, int variant) {
  CHostage* h = new CHostage();
  h->m_iVariant = std::max(0, std::min(3, variant));
  h->m_vecOrigin = origin;
  h->m_angRotation = QAngle(0, yaw, 0);
  h->Spawn();
  g_EntityList.Add(h);
  return h;
}

// a soft light from the map's sun / ambient so the hostage matches the world
static StudioLight HostageLight() {
  StudioLight l;
  l.ambient = g_MapInfo.ambient;
  l.direct = g_MapInfo.sunColor * 0.9f;
  l.dir = g_MapInfo.sunDir;
  return l;
}

void CHostage::DrawModel(const CViewSetup& view) {
  if (!srcfs::Available()) return;
  matrix3x4_t rot; AngleMatrix(QAngle(0, m_angRotation.y, 0), rot); rot.SetOrigin(m_vecOrigin);
  StudioDrawParams p; p.modelToWorld = rot; p.light = HostageLight(); p.eye = view.origin;
  if (cl_hostage_debug_flat.GetBool()) { p.flags |= STUDIO_FLAT_COLOR | STUDIO_NO_DEPTH | STUDIO_NO_CULL; p.colorOverride = { 255, 0, 255, 255 }; }
  bool ok = CSS_DrawModelFile(ModelPath(m_iVariant), rot, p);
}

void CHostage::Think() {
  if (m_bCarried) {
    CCSPlayer* c = m_hCarrier;
    if (!c || !c->IsAlive()) {
      // carrier died or left: the hostage is dropped where it was
      m_bCarried = false; m_hCarrier = nullptr;
    } else {
      Vector f, r, u; AngleVectors(QAngle(0, c->v_angle.y, 0), &f, &r, &u);
      // stand just behind the carrier, offset to their right
      Vector pos = c->m_vecOrigin - f * 21.f + r * 8.f;
      pos.z = c->m_vecOrigin.z;
      // keep the feet on the floor when the carrier walks up a step / ramp
      trace_t tr;
      UTIL_TraceHull(pos + Vector(0, 0, 48), pos - Vector(0, 0, 140), Vector(-12, -12, 0), Vector(12, 12, 72),
                     MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
      if (!tr.startsolid && tr.fraction < 1.f) pos.z = tr.endpos.z;
      m_vecOrigin = pos;
      m_angRotation = QAngle(0, c->v_angle.y, 0);
      m_flPhase += gpGlobals->interval_per_tick * 9.f;   // walk bob
    }
  } else {
    m_flPhase += gpGlobals->interval_per_tick;            // idle breath
  }
}

int CHostage::OnTakeDamage(const CTakeDamageInfo& info) {
  if (m_lifeState != LIFE_ALIVE) return 0;
  CCSPlayer* attacker = ToCSPlayer(info.m_hAttacker);
  // only terrorists (or the world) can hurt a hostage - CTs must not kill their own objective
  if (attacker && attacker->m_iTeamNum != TEAM_TERRORIST) return 0;
  m_iHealth = std::max(0, m_iHealth - (int)info.m_flDamage);
  if (m_iHealth <= 0) { m_lifeState = LIFE_DEAD; Event_Killed(info); }
  return (int)info.m_flDamage;
}

void CHostage::Event_Killed(const CTakeDamageInfo& info) {
  CCSPlayer* attacker = ToCSPlayer(info.m_hAttacker);
  if (attacker) attacker->m_iScore += 1;
  g_GameRules.m_iHostagesKilled++;
  if (m_hCarrier) { m_hCarrier->m_hCarriedHostage = nullptr; m_hCarrier->m_bIsGrabbingHostage = false; m_hCarrier = nullptr; }
  ConColorMsg({ 255, 130, 130, 255 }, "A hostage has been killed!\n");
  IGameEvent ev("hostage_killed");
  ev.SetInt("userid", info.m_hAttacker ? info.m_hAttacker->entindex() : 0);
  gameeventmanager->FireEvent(ev);
  Remove();
}

bool CHostage::IsSolidForTrace(unsigned mask) const {
  if (m_bCarried) return false;                 // a carried hostage does not block the carrier's shots
  return (mask & MASK_SHOT) != 0;               // standing hostages block bullets / knife
}

// drop the hostage when the carrier dies (called from the player's death path)
void CHostage::OnCarrierKilled(CCSPlayer* carrier) {
  if (carrier && carrier->m_hCarriedHostage) {
    carrier->m_hCarriedHostage->m_bCarried = false;
    carrier->m_hCarriedHostage->m_hCarrier = nullptr;
    carrier->m_hCarriedHostage = nullptr;
    carrier->m_bIsGrabbingHostage = false;
  }
}

CHostage* CHostage::FindNearest(CCSPlayer* p, float radius) {
  if (!p) return nullptr;
  CHostage* best = nullptr; float bestD = radius * radius;
  g_EntityList.ForEach([&](C_BaseEntity* e) {
    if (!e->IsHostage()) return;
    CHostage* h = static_cast<CHostage*>(e);
    if (!h->IsAlive() || h->m_bCarried) return;
    float d = (h->m_vecOrigin - p->m_vecOrigin).LengthSqr();
    if (d < bestD) { bestD = d; best = h; }
  });
  return best;
}

void CHostage::ResetRound() {
  // clear the previous round's hostages
  g_EntityList.ForEach([](C_BaseEntity* e) { if (e->IsHostage()) e->Remove(); });
  g_EntityList.CleanupDeleted();
  g_GameRules.m_iHostagesTotal = g_GameRules.m_iHostagesRescued = g_GameRules.m_iHostagesKilled = 0;
  if (!mp_hostages.GetBool()) return;

  // only hostage maps (cs_*) get hostages - bomb maps (de_*) do not
  if (g_MapInfo.name.compare(0, 3, "cs_") != 0) return;

  // classic hostage layout: the hostages sit in / near the T spawns, the CTs grab them and carry them
  // back to their own buy zone. Falls back to any spawn when the map has no T spawn.
  std::vector<Vector> spots;
  for (auto& s : g_MapInfo.spawns) if (s.team == TEAM_TERRORIST) spots.push_back(s.origin);
  if (spots.empty()) for (auto& s : g_MapInfo.spawns) spots.push_back(s.origin);
  if (spots.empty()) return;

  int count = std::max(0, std::min(8, mp_hostage_count.GetInt()));
  // face the nearest CT spawn so the hostage looks towards its rescuers
  Vector ctCenter; int nct = 0;
  for (auto& s : g_MapInfo.spawns) if (s.team == TEAM_CT) { ctCenter += s.origin; nct++; }
  if (nct) ctCenter /= (float)nct;

  for (int i = 0; i < count; i++) {
    const Vector& base = spots[i % spots.size()];
    Vector pos = base + Vector(RandomFloat(-14, 14), RandomFloat(-14, 14), 0);
    // settle onto the floor (the T spawns are already at feet level, this handles the random offset)
    trace_t tr;
    UTIL_TraceHull(pos + Vector(0, 0, 48), pos - Vector(0, 0, 150), Vector(-12, -12, 0), Vector(12, 12, 72),
                   MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
    if (!tr.startsolid && tr.fraction < 1.f) pos.z = tr.endpos.z;
    float yaw = 0;
    if (nct) yaw = RAD2DEGF(std::atan2(ctCenter.y - pos.y, ctCenter.x - pos.x));
    SpawnAt(pos, yaw, i % 4);
  }
  g_GameRules.m_iHostagesTotal = count;
  ConColorMsg({ 150, 200, 255, 255 }, "%d hostages are waiting to be rescued.\n", count);
}

// debug helpers ---------------------------------------------------------------------------------
static ConCommand hostage_debug("hostage_debug", [](const CCommand&) {
  int n = 0;
  g_EntityList.ForEach([&](C_BaseEntity* e) {
    if (!e->IsHostage()) return;
    CHostage* h = static_cast<CHostage*>(e);
    Msg("[hostage] %d pos %.0f %.0f %.0f yaw %.0f carried %d variant %d\n", n, h->m_vecOrigin.x, h->m_vecOrigin.y, h->m_vecOrigin.z, h->m_angRotation.y, h->m_bCarried, h->m_iVariant);
    n++;
  });
  Msg("[hostage] alive=%d total=%d rescued=%d killed=%d\n", n, g_GameRules.m_iHostagesTotal, g_GameRules.m_iHostagesRescued, g_GameRules.m_iHostagesKilled);
}, "Print the positions / state of every hostage");

static ConCommand hostage_goto("hostage_goto", [](const CCommand& c) {
  CCSPlayer* p = g_pLocalPlayer; if (!p) return;
  CHostage* h = nullptr; float best = 1e9f;
  g_EntityList.ForEach([&](C_BaseEntity* e) { if (!e->IsHostage()) return; CHostage* x = static_cast<CHostage*>(e); float d = (x->m_vecOrigin - p->m_vecOrigin).Length(); if (d < best) { best = d; h = x; } });
  if (!h) { Msg("[hostage] none found\n"); return; }
  // stand ~40 units from the hostage, dropped onto the floor, looking at it
  Vector want = h->m_vecOrigin + Vector(0, -42, 0);
  trace_t tr;
  UTIL_TraceHull(want + Vector(0, 0, 48), want - Vector(0, 0, 150), Vector(-16, -16, 0), Vector(16, 16, 72),
                 MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
  if (!tr.startsolid && tr.fraction < 1.f) want.z = tr.endpos.z;
  p->m_vecOrigin = want;
  p->m_vecOldOrigin = want;
  p->m_vecVelocity.Init();
  // debug: make sure the local player is alive so the camera uses eye height
  if (p->m_lifeState != LIFE_ALIVE) { p->m_lifeState = LIFE_ALIVE; p->m_iHealth = 100; p->m_iMaxHealth = 100; p->m_takedamage = 1; }
  Vector to = h->m_vecOrigin - p->m_vecOrigin; to.z = 0;
  if (to.Length() > 0.1f) to = to.Normalized();
  p->v_angle = QAngle(0, RAD2DEGF(std::atan2(to.y, to.x)), 0);
  { extern void CL_SetViewAngles(const QAngle&); CL_SetViewAngles(p->v_angle); }
  Msg("[hostage] goto %.0f %.0f %.0f (player %.0f %.0f %.0f yaw %.0f)\n", h->m_vecOrigin.x, h->m_vecOrigin.y, h->m_vecOrigin.z, p->m_vecOrigin.x, p->m_vecOrigin.y, p->m_vecOrigin.z, p->v_angle.y);
}, "Teleport the local player next to the nearest hostage (debug)");
