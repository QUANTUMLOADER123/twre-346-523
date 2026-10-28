// cs_player_shared.cpp - FX_FireBullets / FireBullet with CS:GO style penetration, ScaleDamage
#include "cs_player.hpp"
#include "weapon_csbase.hpp"
#include "cs_gamerules.hpp"
#include "cs_projectiles.hpp"
#include "hostage.hpp"
#include "lagcompensation.hpp"
#include "gameevents.hpp"
#include "soundemitter.hpp"
#include "../../engine/random.hpp"
#include <cmath>

ConVar weapon_accuracy_nospread("weapon_accuracy_nospread", "0", FCVAR_REPLICATED, "Disable weapon inaccuracy spread");
ConVar mp_damage_headshot_only("mp_damage_headshot_only", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "Only headshots deal damage");
ConVar mp_damage_scale_head("mp_damage_scale_head", "1.0", FCVAR_REPLICATED, "Scales headshot damage");
ConVar mp_damage_scale_body("mp_damage_scale_body", "1.0", FCVAR_REPLICATED, "Scales body damage");
ConVar sv_showimpacts("sv_showimpacts", "0", FCVAR_REPLICATED, "Shows client (red) and server (blue) bullet impact point (1=both, 2=client-only, 3=server-only)");
ConVar sv_penetration_type("sv_penetration_type", "1", FCVAR_REPLICATED, "What type of penetration is currently being used. 0 = old CS, 1 = new penetration");

// server-side impact log for sv_showimpacts and the cheat's bullet tracers
struct BulletImpactRecord { Vector start, end; float time; bool server; int shooter; bool hitPlayer; };
std::vector<BulletImpactRecord> g_BulletImpacts;

static float ScaleDamage(int hitgroup, float damage, const CCSWeaponInfo& wi) {
  switch (hitgroup) {
    case HITGROUP_HEAD: return damage * wi.headshotMultiplier * mp_damage_scale_head.GetFloat();
    case HITGROUP_STOMACH: return damage * 1.25f * mp_damage_scale_body.GetFloat();
    case HITGROUP_LEFTLEG: case HITGROUP_RIGHTLEG: return damage * 0.75f * mp_damage_scale_body.GetFloat();
    default: return damage * mp_damage_scale_body.GetFloat();
  }
}

// step through solid until we leave it; returns exit trace
static bool TraceToExit(const Vector& start, const Vector& dir, Vector& end, const trace_t& enterTrace, trace_t& exitTrace, float stepSize = 4.f, float maxDistance = 90.f) {
  float dist = 0;
  int firstContents = 0;
  // every probe lies on this segment: fetch the candidate brushes once instead of per probe
  thread_local std::vector<int> cand;
  const Vector segEnd = start + dir * (maxDistance + stepSize);
  const bool fast = g_EngineTrace.GatherBrushes(VecMin(start, segEnd), VecMax(start, segEnd), MASK_SHOT_HULL, cand);
  while (dist <= maxDistance) {
    dist += stepSize;
    end = start + dir * dist;
    int contents = (fast ? g_EngineTrace.PointContentsIn(cand, end) : g_EngineTrace.GetPointContents(end, MASK_SHOT_HULL)) & MASK_SHOT_HULL;
    if (firstContents == 0) firstContents = contents;
    if (!(contents & MASK_SHOT_HULL)) {
      Vector back = end - dir * stepSize;
      Ray_t ray; ray.Init(end, back);
      g_EngineTrace.TraceRayWorld(ray, MASK_SHOT_HULL, &exitTrace);
      if (exitTrace.DidHit() && !exitTrace.startsolid) {
        if (exitTrace.plane.normal.Dot(dir) <= 1.f) { end = exitTrace.endpos; return true; }
      } else if (!exitTrace.DidHit()) {
        // thin geometry: we are outside now
        exitTrace.endpos = end; exitTrace.surface = enterTrace.surface; exitTrace.plane.normal = dir;
        return true;
      }
    }
  }
  return false;
}

// returns true if the bullet stops
static bool HandleBulletPenetration(float flPenetration, int& nPenetrationCount, trace_t& tr, const Vector& vecDir, Vector& vecSrc, float& fCurrentDamage, bool& hitGrate, bool fireEvent = true) {
  const surfacedata_t* enter = GetSurfaceData(tr.surface.surfaceProps);
  int iEnterMaterial = enter->gameMaterial;
  bool bIsNodraw = (tr.surface.flags & SURF_NODRAW) != 0;
  bool bFailedPenetrate = false;
  hitGrate = (tr.contents & CONTENTS_GRATE) != 0;
  if (nPenetrationCount == 0 && !hitGrate && !bIsNodraw && iEnterMaterial != CHAR_TEX_GLASS && iEnterMaterial != CHAR_TEX_GRATE) bFailedPenetrate = true;
  if (flPenetration <= 0 || nPenetrationCount <= 0) bFailedPenetrate = true;
  Vector penetrationEnd; trace_t exitTr;
  if (!TraceToExit(tr.endpos, vecDir, penetrationEnd, tr, exitTr)) {
    if (!(g_EngineTrace.GetPointContents(tr.endpos + vecDir * 1.f, MASK_SHOT_HULL) & CONTENTS_GRATE)) bFailedPenetrate = true;
  }
  if (bFailedPenetrate) return true;
  const surfacedata_t* exitSurf = GetSurfaceData(exitTr.surface.surfaceProps);
  int iExitMaterial = exitSurf->gameMaterial;
  float flCombinedPenetrationModifier, flDamLostPercent = 0.16f;
  if (hitGrate || bIsNodraw || iEnterMaterial == CHAR_TEX_GLASS || iEnterMaterial == CHAR_TEX_GRATE) {
    if (iEnterMaterial == CHAR_TEX_GLASS || iEnterMaterial == CHAR_TEX_GRATE) { flCombinedPenetrationModifier = 3.0f; flDamLostPercent = 0.05f; }
    else flCombinedPenetrationModifier = 1.0f;
  } else {
    flCombinedPenetrationModifier = (enter->penetrationModifier + exitSurf->penetrationModifier) * 0.5f;
  }
  if (iEnterMaterial == iExitMaterial) {
    if (iExitMaterial == CHAR_TEX_WOOD || iExitMaterial == CHAR_TEX_CARDBOARD) flCombinedPenetrationModifier = 3.f;
    else if (iExitMaterial == CHAR_TEX_PLASTIC) flCombinedPenetrationModifier = 2.f;
  }
  float flTraceDistance = (exitTr.endpos - tr.endpos).Length();
  float flPenMod = std::max(0.f, 1.f / std::max(0.001f, flCombinedPenetrationModifier));
  float flPercentDamageChunk = fCurrentDamage * flDamLostPercent;
  float flPenWepMod = flPercentDamageChunk + std::max(0.f, (3.f / std::max(0.01f, flPenetration)) * 1.25f) * (flPenMod * 3.0f);
  float flLostDamageObject = (flPenMod * (flTraceDistance * flTraceDistance)) / 24.f;
  float flTotalLostDamage = flPenWepMod + flLostDamageObject;
  fCurrentDamage -= std::max(flTotalLostDamage, 0.f);
  if (fCurrentDamage < 1.f) return true;
  vecSrc = exitTr.endpos + vecDir * 0.1f;
  nPenetrationCount--;
  if (!fireEvent) return false;
  // exit impact decal
  IGameEvent ev("bullet_impact"); ev.SetInt("userid", 0).SetFloat("x", exitTr.endpos.x).SetFloat("y", exitTr.endpos.y).SetFloat("z", exitTr.endpos.z).SetInt("exit", 1).SetInt("surf", exitTr.surface.surfaceProps)
    .SetFloat("nx", -vecDir.x).SetFloat("ny", -vecDir.y).SetFloat("nz", -vecDir.z);
  gameeventmanager->FireEvent(ev);
  return false;
}

void CCSPlayer::FireBullet(const Vector& vecSrcIn, const QAngle& shootAngles, float flDistance, float flPenetration, int nPenetrationCount, int iBulletType,
                           int iDamage, float flRangeModifier, C_WeaponCSBase* weapon, int weaponId, bool isTracer, const CUserCmd* cmd) {
  (void)iBulletType; (void)cmd;
  const CCSWeaponInfo& wi = *GetWeaponInfo(weaponId);
  float fCurrentDamage = (float)iDamage;
  float flCurrentDistance = 0;
  Vector vecSrc = vecSrcIn;
  Vector vecDir; AngleVectors(shootAngles, &vecDir);
  C_BaseEntity* lastPlayerHit = nullptr;
  bool firstSegment = true;
  Vector tracerStart = vecSrc;
  bool hitAny = false;
  int penetrated = 0;
  while (fCurrentDamage > 0) {
    Vector vecEnd = vecSrc + vecDir * (flDistance - flCurrentDistance);
    trace_t tr;
    Ray_t ray; ray.Init(vecSrc, vecEnd);
    CTraceFilterSkipTwoEntities filter(this, lastPlayerHit);
    g_EngineTrace.TraceRay(ray, MASK_SHOT | CONTENTS_GRATE, &filter, &tr);
    if (tr.fraction == 1.f) { if (firstSegment || true) g_BulletImpacts.push_back({ tracerStart, vecEnd, gpGlobals->curtime, true, entindex(), false }); break; }
    flCurrentDistance += tr.fraction * (flDistance - flCurrentDistance);
    fCurrentDamage *= std::pow(flRangeModifier, flCurrentDistance / 500.f);
    if (flCurrentDistance > 3000.f && flPenetration > 0) nPenetrationCount = 0;
    // impact event (decals, tracers, cheat impact boxes)
    {
      IGameEvent ev("bullet_impact");
      ev.SetInt("userid", entindex()).SetFloat("x", tr.endpos.x).SetFloat("y", tr.endpos.y).SetFloat("z", tr.endpos.z).SetInt("surf", tr.surface.surfaceProps)
        .SetFloat("nx", tr.plane.normal.x).SetFloat("ny", tr.plane.normal.y).SetFloat("nz", tr.plane.normal.z).SetInt("player", tr.m_pEnt ? tr.m_pEnt->entindex() : 0)
        .SetFloat("sx", tracerStart.x).SetFloat("sy", tracerStart.y).SetFloat("sz", tracerStart.z).SetInt("tracer", isTracer && firstSegment ? 1 : 0);
      gameeventmanager->FireEvent(ev);
    }
    g_BulletImpacts.push_back({ tracerStart, tr.endpos, gpGlobals->curtime, true, entindex(), tr.m_pEnt != nullptr });
    firstSegment = false; tracerStart = tr.endpos;
    if (CCSPlayer* victim = ToCSPlayer(tr.m_pEnt)) {
      lastPlayerHit = victim; hitAny = true;
      bool canDamage = !mp_damage_headshot_only.GetBool() || tr.hitgroup == HITGROUP_HEAD;
      if (canDamage && victim->IsAlive()) {
        CTakeDamageInfo info;
        info.m_hAttacker = this; info.m_hInflictor = weapon ? (C_BaseEntity*)weapon : this; info.m_iWeaponID = weaponId;
        info.m_flDamage = ScaleDamage(tr.hitgroup, fCurrentDamage, wi);
        info.m_bitsDamageType = DMG_BULLET | (tr.hitgroup == HITGROUP_HEAD ? DMG_HEADSHOT : 0);
        info.m_iHitGroup = tr.hitgroup; info.m_iHitbox = tr.hitbox;
        info.m_vecDamagePosition = tr.endpos; info.m_vecDamageForce = vecDir * fCurrentDamage * 10.f;
        info.m_iPenetrationCount = penetrated; info.m_bWallbang = penetrated > 0;
        info.m_bNoScope = IsSniper(weaponId) && !m_bIsScoped;
        info.m_bThroughSmoke = LineGoesThroughSmoke(vecSrcIn, tr.endpos);
        info.m_bAttackerBlind = IsBlind();
        victim->OnTakeDamage(info);
      }
      // bullets pass through players (losing damage like CS:GO flesh penetration)
      const surfacedata_t* flesh = GetSurfaceData(SURFACE_FLESH);
      fCurrentDamage *= 0.75f * flesh->damageModifier + 0.25f;
      if (nPenetrationCount <= 0) break;
      nPenetrationCount--;
      vecSrc = tr.endpos + vecDir * 2.f;
      penetrated++;
      continue;
    }
    if (CHostage* h = ToHostage(tr.m_pEnt)) {
      if (h->IsAlive()) {
        CTakeDamageInfo info;
        info.m_hAttacker = this; info.m_hInflictor = weapon ? (C_BaseEntity*)weapon : this; info.m_iWeaponID = weaponId;
        info.m_flDamage = fCurrentDamage;
        info.m_bitsDamageType = DMG_BULLET;
        info.m_iHitGroup = HITGROUP_GENERIC;
        info.m_vecDamagePosition = tr.endpos; info.m_vecDamageForce = vecDir * fCurrentDamage * 10.f;
        h->OnTakeDamage(info);
      }
      break;   // bullets do not pass through a hostage
    }
    bool hitGrate = false;
    if (sv_penetration_type.GetInt() == 0 && nPenetrationCount <= 0) break;
    if (HandleBulletPenetration(flPenetration, nPenetrationCount, tr, vecDir, vecSrc, fCurrentDamage, hitGrate)) break;
    penetrated++;
  }
  (void)hitAny;
  while (g_BulletImpacts.size() > 256) g_BulletImpacts.erase(g_BulletImpacts.begin());
}

void CCSPlayer::FX_FireBullets(int weaponId, const Vector& origin, const QAngle& angles, int seed, float inaccuracy, float spread, const CUserCmd* cmd) {
  const CCSWeaponInfo* wi = GetWeaponInfo(weaponId);
  if (!wi) return;
  C_WeaponCSBase* weapon = GetActiveCSWeapon();
  RandomSeed(seed + 1);
  if (weapon_accuracy_nospread.GetBool()) { inaccuracy = 0; spread = 0; }
  float fRand1 = RandomFloat(0.f, 1.f), fRandPi1 = RandomFloat(0.f, 2.f * M_PI_F);
  float fRand2 = RandomFloat(0.f, 1.f), fRandPi2 = RandomFloat(0.f, 2.f * M_PI_F);
  if (weaponId == WEAPON_REVOLVER && cmd && (cmd->buttons & IN_ATTACK2)) { fRand1 = 1.f - fRand1 * fRand1; fRand2 = 1.f - fRand2 * fRand2; }
  float fInaccuracy = fRand1 * inaccuracy;
  Vector forward, right, up;
  AngleVectors(angles, &forward, &right, &up);
  // lag compensation for the hit test (rewind every other player to what the shooter saw)
  bool lagcomp = cmd != nullptr;
  if (lagcomp) g_LagCompensation.StartLagCompensation(this, cmd);
  int tracerFreq = (int)wi->tracerFrequency;
  for (int iBullet = 0; iBullet < wi->bullets; iBullet++) {
    float fSpreadRand = fRand2, fSpreadPi = fRandPi2;
    if (iBullet > 0) { fSpreadRand = RandomFloat(0.f, 1.f); fSpreadPi = RandomFloat(0.f, 2.f * M_PI_F); }
    float fSpread = fSpreadRand * spread;
    float x = std::cos(fRandPi1) * fInaccuracy + std::cos(fSpreadPi) * fSpread;
    float y = std::sin(fRandPi1) * fInaccuracy + std::sin(fSpreadPi) * fSpread;
    Vector dir = forward + right * x + up * y; dir.NormalizeInPlace();
    QAngle a; VectorAngles(dir, a);
    bool tracer = tracerFreq > 0 && (m_iShotsFired % tracerFreq) == 0;
    FireBullet(origin, a, wi->range, wi->penetration, 4, 0, wi->damage, wi->rangeModifier, weapon, weaponId, tracer, cmd);
  }
  if (lagcomp) g_LagCompensation.FinishLagCompensation(this);
}

// Autowall: simulate a bullet from src to a point on the target through world geometry only (no events, no damage).
// Returns the damage the target would take at hitgroup (after armor), 0 if the bullet stops. outWalls = surfaces penetrated.
float CS_SimulateBulletDamage(const CCSPlayer* shooter, const Vector& src, const Vector& end, int weaponId, const CCSPlayer* target, int hitgroup, int* outWalls) {
  (void)shooter;
  const CCSWeaponInfo* wi = GetWeaponInfo(weaponId);
  if (!wi) return 0;
  Vector dir = end - src; float total = dir.Length(); if (total < 1) return 0; dir = dir * (1.f / total);
  float dmg = (float)wi->damage, travelled = 0;
  int pen = 4, walls = 0;
  Vector cur = src;
  for (int iter = 0; iter < 8 && dmg > 0; iter++) {
    Vector segEnd = src + dir * total;
    trace_t tr; Ray_t ray; ray.Init(cur, segEnd);
    g_EngineTrace.TraceRayWorld(ray, MASK_SHOT | CONTENTS_GRATE, &tr);
    if (tr.fraction >= 1.f) { travelled = total; break; }
    travelled = (tr.endpos - src).Length();
    float rangeDmg = dmg * std::pow(wi->rangeModifier, travelled / 500.f);
    float tmp = rangeDmg; bool grate = false;
    if (HandleBulletPenetration(wi->penetration, pen, tr, dir, cur, tmp, grate, false)) { if (outWalls) *outWalls = walls; return 0; }
    // keep "dmg" unscaled by range so the final range falloff is applied once
    dmg *= tmp / std::max(0.001f, rangeDmg);
    walls++;
    if ((cur - src).Dot(dir) >= total) break;
  }
  if (outWalls) *outWalls = walls;
  float d = dmg * std::pow(wi->rangeModifier, total / 500.f);
  d = ScaleDamage(hitgroup, d, *wi);
  if (target && target->m_ArmorValue > 0 && (hitgroup != HITGROUP_HEAD || target->m_bHasHelmet) && hitgroup != HITGROUP_LEFTLEG && hitgroup != HITGROUP_RIGHTLEG)
    d *= wi->armorRatio * 0.5f;
  return d;
}
