// cs_projectiles.cpp - grenade projectiles, smoke volumes, radius damage, C4
#include "cs_projectiles.hpp"
#include "cs_player.hpp"
#include "cs_gamerules.hpp"
#include "gameevents.hpp"
#include "soundemitter.hpp"
#include "mapinfo.hpp"
#include "../../engine/random.hpp"
#include <cmath>
#include <algorithm>

extern ConVar mp_c4timer;
ConVar sv_grenade_trajectory("sv_grenade_trajectory", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "Shows grenade trajectory visualization in-game.");
ConVar ammo_grenade_limit_total("ammo_grenade_limit_total", "4", FCVAR_REPLICATED, "");

ConVar sv_hegrenade_fuse("sv_hegrenade_fuse", "2.0", FCVAR_REPLICATED, "Seconds from the throw until an HE grenade explodes", true, 0.5f, true, 10.f);
ConVar sv_flashbang_fuse("sv_flashbang_fuse", "2.5", FCVAR_REPLICATED | FCVAR_ARCHIVE, "Seconds from the throw until a flashbang pops", true, 0.5f, true, 10.f);
ConVar sv_smoke_duration("sv_smoke_duration", "25", FCVAR_REPLICATED | FCVAR_ARCHIVE, "How long a smoke grenade cloud lasts (seconds)", true, 3.f, true, 90.f);
ConVar molotov_throw_detonate_time("molotov_throw_detonate_time", "2.0", FCVAR_REPLICATED | FCVAR_ARCHIVE, "Molotov / incendiary: seconds of flight before it bursts in the air", true, 0.3f, true, 10.f);
ConVar weapon_molotov_maxdetonateslope("weapon_molotov_maxdetonateslope", "30", FCVAR_REPLICATED, "Maximum slope (degrees) a molotov bursts on - steeper surfaces bounce it", true, 0.f, true, 90.f);
ConVar inferno_flame_lifetime("inferno_flame_lifetime", "7", FCVAR_REPLICATED | FCVAR_ARCHIVE, "How long a molotov / incendiary fire burns (seconds)", true, 1.f, true, 30.f);
ConVar inferno_damage("inferno_damage", "40", FCVAR_REPLICATED, "Fire damage per second");
ConVar inferno_max_range("inferno_max_range", "150", FCVAR_REPLICATED, "How far the fire spreads from the impact point");
ConVar inferno_max_flames("inferno_max_flames", "16", FCVAR_REPLICATED, "Maximum flames of one fire");

static constexpr float GRENADE_GRAVITY = 0.4f, GRENADE_ELASTICITY = 0.45f, GRENADE_FRICTION = 0.2f;
static constexpr float SMOKE_RADIUS = 144.f;
static constexpr float FLAME_RADIUS = 30.f;   // one flame covers ~60 units
float SmokeDuration() { return sv_smoke_duration.GetFloat(); }
float GrenadeFuseTime(int weaponId) {
  switch (weaponId) {
    case WEAPON_HEGRENADE: return sv_hegrenade_fuse.GetFloat();
    case WEAPON_FLASHBANG: return sv_flashbang_fuse.GetFloat();
    case WEAPON_MOLOTOV: case WEAPON_INCGRENADE: return molotov_throw_detonate_time.GetFloat();
    default: return 1e9f;   // smoke: when it stops
  }
}
static bool IsDetonateFloor(const Vector& n) { return n.z >= std::cos(DEG2RADF(weapon_molotov_maxdetonateslope.GetFloat())) - 1e-3f; }

static std::vector<SmokeVolume> s_smokes;
std::vector<SmokeVolume>& GetActiveSmokes() { return s_smokes; }

bool LineGoesThroughSmoke(const Vector& a, const Vector& b) {
  float now = ServerTime();
  for (const SmokeVolume& s : s_smokes) {
    if (now < s.start || now > s.end) continue;
    float age = now - s.start, left = s.end - now;
    float r = SMOKE_RADIUS * Clamp(age / 0.75f, 0.f, 1.f) * Clamp(left / 1.5f, 0.f, 1.f);
    if (r < 20.f) continue;
    Vector d = b - a; float len2 = d.LengthSqr(); if (len2 < 1) continue;
    Vector c = s.center + Vector(0, 0, 40);
    float t = Clamp((c - a).Dot(d) / len2, 0.f, 1.f);
    Vector p = a + d * t;
    float dist = p.DistTo(c);
    if (dist >= r) continue;
    float chord = 2.f * std::sqrt(r * r - dist * dist);
    // endpoints inside the smoke also count
    if (chord > 48.f || a.DistTo(c) < r || b.DistTo(c) < r) return true;
  }
  return false;
}

void GetGrenadeThrowParams(CCSPlayer* player, const QAngle& eyeAngles, float strength, Vector& outSrc, Vector& outVel) {
  QAngle ang = eyeAngles;
  if (ang.x > 90.f) ang.x -= 360.f; else if (ang.x < -90.f) ang.x += 360.f;
  ang.x -= (90.f - std::fabs(ang.x)) * 10.f / 90.f;
  float flVel = Clamp(750.f * 0.9f, 15.f, 750.f);
  strength = Clamp(strength, 0.f, 1.f);
  flVel *= 0.3f + 0.7f * strength;
  Vector fwd; AngleVectors(ang, &fwd);
  Vector src = player->EyePosition(); src.z += strength * 12.f - 12.f;
  trace_t tr; UTIL_TraceHull(src, src + fwd * 22.f, Vector(-2, -2, -2), Vector(2, 2, 2), MASK_SOLID & ~CONTENTS_MONSTER, player, &tr);
  outSrc = tr.endpos - fwd * 6.f;
  outVel = fwd * flVel + player->m_vecVelocity * 1.25f;
}

// one physics step shared by the real projectile and the prediction
struct GrenadeSim { Vector pos, vel; bool stopped = false; int bounces = 0; };
static bool StepGrenade(GrenadeSim& g, float dt, const IHandleEntity* skip, Vector* bouncePos, bool hitPlayers, Vector* bounceNormal = nullptr) {
  if (g.stopped) return false;
  g.vel.z -= 800.f * GRENADE_GRAVITY * dt;
  float timeLeft = dt; bool bounced = false;
  for (int i = 0; i < 3 && timeLeft > 0; i++) {
    Vector end = g.pos + g.vel * timeLeft;
    trace_t tr; Ray_t ray; ray.Init(g.pos, end, Vector(-2, -2, -2), Vector(2, 2, 2));
    unsigned mask = MASK_SOLID & ~(hitPlayers ? 0u : (unsigned)CONTENTS_MONSTER);
    if (hitPlayers) { CTraceFilter f(skip); g_EngineTrace.TraceRay(ray, mask, &f, &tr); }
    else { CTraceFilterWorldOnly f; g_EngineTrace.TraceRay(ray, mask, &f, &tr); }
    if (tr.startsolid) { g.vel.Init(); g.stopped = true; return bounced; }
    g.pos = tr.endpos;
    if (tr.fraction >= 1.f) break;
    timeLeft *= 1.f - tr.fraction;
    Vector n = tr.plane.normal;
    if (bounceNormal) *bounceNormal = n;
    float backoff = g.vel.Dot(n) * 2.f;
    g.vel -= n * backoff;
    float elasticity = GRENADE_ELASTICITY * (tr.m_pEnt ? 0.3f : 1.f);
    g.vel *= elasticity;
    if (n.z > 0.7f) {
      float speed2 = g.vel.LengthSqr();
      if (speed2 > 96000.f) { float l = g.vel.Dot(n.Normalized()); if (l > 0) g.vel -= n * l * 0.5f; }
      if (speed2 < 400.f) { g.vel.Init(); g.stopped = true; }
      else { g.vel.x *= 1.f - GRENADE_FRICTION; g.vel.y *= 1.f - GRENADE_FRICTION; }
    }
    g.bounces++; bounced = true;
    if (bouncePos) *bouncePos = g.pos;
    if (g.stopped) break;
  }
  return bounced;
}

void PredictGrenadePath(int weaponId, const Vector& src, const Vector& vel, GrenadePath& out, const IHandleEntity* skip) {
  out.points.clear(); out.bounces.clear();
  GrenadeSim g; g.pos = src; g.vel = vel;
  float dt = gpGlobals->interval_per_tick, t = 0;
  const bool fire = IsFireGrenade(weaponId);
  float fuse = weaponId == WEAPON_SMOKEGRENADE ? 10.f : GrenadeFuseTime(weaponId);
  if (fire) fuse = std::max(fuse, 6.f);   // it keeps flying to the floor (see CMolotovProjectile::PhysicsSimulate)
  out.points.push_back(src);
  float stoppedFor = 0;
  while (t < fuse) {
    Vector bp, bn;
    bool bounced = StepGrenade(g, dt, skip, &bp, false, &bn);
    t += dt;
    if (bounced && fire && IsDetonateFloor(bn)) { out.points.push_back(g.pos); break; }   // bursts on the floor
    if (bounced) out.bounces.push_back(bp);
    if ((int)(t / dt) % 2 == 0 || g.stopped) out.points.push_back(g.pos);
    if (g.stopped) { stoppedFor += dt; if (weaponId == WEAPON_SMOKEGRENADE || fire || stoppedFor > 0.2f) break; }
  }
  out.detonate = g.pos; out.time = t;
}

// ------------------------------------------------------------------------------------------------
CBaseCSGrenadeProjectile::CBaseCSGrenadeProjectile(int weaponId, CCSPlayer* thrower, const Vector& pos, const Vector& vel) {
  m_iEntClass = ENTCLASS_GRENADE_PROJECTILE;
  m_iWeaponID = weaponId; m_hThrower = thrower; m_hOwnerEntity = thrower;
  m_vecOrigin = m_vecOldOrigin = pos; m_vecVelocity = m_vecInitialVelocity = vel;
  m_flSpawnTime = ServerTime();
  m_flDetonateTime = m_flSpawnTime + GrenadeFuseTime(weaponId);
  m_angSpin = QAngle(RandomFloat(-600, 600), RandomFloat(-600, 600), 0);
  m_iTeamNum = thrower ? thrower->m_iTeamNum : 0;
  m_vecMins = { -2, -2, -2 }; m_vecMaxs = { 2, 2, 2 };
  const char* cls[] = { "hegrenade_projectile", "flashbang_projectile", "smokegrenade_projectile" };
  m_iClassname = IsFireGrenade(weaponId) ? "molotov_projectile" : cls[std::max(0, std::min(2, weaponId - WEAPON_HEGRENADE))];
}

void CBaseCSGrenadeProjectile::PhysicsSimulate() {
  if (m_bDetonated) return;
  float dt = gpGlobals->interval_per_tick;
  m_vecOldOrigin = m_vecOrigin;
  m_angOldRotation = m_angRotation;
  GrenadeSim g; g.pos = m_vecOrigin; g.vel = m_vecVelocity; g.stopped = m_bStopped; g.bounces = m_nBounces;
  float speedBefore = g.vel.Length();
  m_bBouncedThisTick = StepGrenade(g, dt, m_hThrower, nullptr, true, &m_vecLastBounceNormal);
  if (m_bBouncedThisTick) {
    if (speedBefore > 60.f) {
      const char* snd = m_iWeaponID == WEAPON_HEGRENADE ? "HEGrenade.Bounce" : m_iWeaponID == WEAPON_FLASHBANG ? "Flashbang.Bounce" :
                        IsFireGrenade(m_iWeaponID) ? "Molotov.Bounce" : "SmokeGrenade.Bounce";
      EmitSound(snd, g.pos, entindex(), Clamp(speedBefore / 500.f, 0.2f, 1.f));
    }
  }
  if (g.stopped && !m_bStopped) m_flStoppedTime = ServerTime();
  m_vecOrigin = g.pos; m_vecVelocity = g.vel; m_bStopped = g.stopped; m_nBounces = g.bounces;
  if (!m_bStopped) { m_angRotation.x += m_angSpin.x * dt; m_angRotation.y += m_angSpin.y * dt; }
  if (m_Trail.empty() || m_Trail.back().DistTo(m_vecOrigin) > 8.f) { m_Trail.push_back(m_vecOrigin); if (m_Trail.size() > 64) m_Trail.erase(m_Trail.begin()); }
  if (ServerTime() >= m_flDetonateTime) Detonate();
}

void CBaseCSGrenadeProjectile::Detonate() { m_bDetonated = true; Remove(); }

void RadiusDamage(const CTakeDamageInfo& info, const Vector& src, float radius, C_BaseEntity* ignore) {
  float sigma = radius / 3.f;
  for (CCSPlayer* p : UTIL_GetPlayers(true)) {
    if (p == ignore) continue;
    Vector target = p->m_vecOrigin + Vector(0, 0, 36);
    float dist = src.DistTo(target);
    if (dist > radius) continue;
    // need line of sight to the body (eyes or center)
    trace_t tr; UTIL_TraceLine(src, target, MASK_SOLID & ~CONTENTS_MONSTER, nullptr, &tr);
    if (tr.fraction < 1.f) { UTIL_TraceLine(src, p->EyePosition(), MASK_SOLID & ~CONTENTS_MONSTER, nullptr, &tr); if (tr.fraction < 1.f) continue; }
    float dmg = info.m_flDamage * std::exp(-(dist * dist) / (2.f * sigma * sigma));
    if (dmg < 1.f) continue;
    CTakeDamageInfo d = info; d.m_flDamage = dmg; d.m_vecDamagePosition = target; d.m_iHitGroup = HITGROUP_GENERIC;
    Vector force = target - src; force.NormalizeInPlace(); d.m_vecDamageForce = force * dmg * 20.f;
    p->OnTakeDamage(d);
  }
}

void CHEGrenadeProjectile::Detonate() {
  if (m_bDetonated) return;
  CTakeDamageInfo info; info.m_hAttacker = m_hThrower; info.m_hInflictor = this; info.m_iWeaponID = WEAPON_HEGRENADE;
  info.m_flDamage = 99.f; info.m_bitsDamageType = DMG_BLAST;
  RadiusDamage(info, m_vecOrigin + Vector(0, 0, 8), 350.f, nullptr);
  EmitSound("BaseGrenade.Explode", m_vecOrigin, entindex(), 1.f);
  IGameEvent ev("hegrenade_detonate"); ev.SetInt("userid", m_hThrower ? m_hThrower->entindex() : 0).SetFloat("x", m_vecOrigin.x).SetFloat("y", m_vecOrigin.y).SetFloat("z", m_vecOrigin.z);
  gameeventmanager->FireEvent(ev);
  CBaseCSGrenadeProjectile::Detonate();
}

void CFlashbangProjectile::Detonate() {
  if (m_bDetonated) return;
  Vector src = m_vecOrigin + Vector(0, 0, 4);
  for (CCSPlayer* p : UTIL_GetPlayers(true)) {
    Vector eye = p->EyePosition();
    float dist = src.DistTo(eye);
    if (dist > 2500.f) continue;
    trace_t tr; UTIL_TraceLine(src, eye, MASK_OPAQUE, nullptr, &tr);
    if (tr.fraction < 0.99f) continue;
    float smoke = LineGoesThroughSmoke(src, eye) ? 0.4f : 1.f;
    Vector fwd; AngleVectors(p->v_angle, &fwd);
    Vector to = src - eye; to.NormalizeInPlace();
    float dot = fwd.Dot(to);
    float angleFactor = dot >= 0.6f ? 1.f : dot >= 0.3f ? 0.85f : dot >= -0.2f ? 0.55f : 0.3f;
    float distFactor = Clamp(1.f - (dist - 300.f) / 1500.f, 0.f, 1.f);
    float strength = angleFactor * distFactor * smoke;
    if (strength <= 0.05f) continue;
    float hold = 1.9f * strength * (dot >= 0.6f ? 1.f : 0.6f);
    float fade = 3.0f * strength;
    p->Blind(hold, fade, strength > 0.5f ? 255.f : 200.f * strength + 50.f);
  }
  EmitSound("Flashbang.Explode", m_vecOrigin, entindex(), 1.f);
  IGameEvent ev("flashbang_detonate"); ev.SetInt("userid", m_hThrower ? m_hThrower->entindex() : 0).SetFloat("x", m_vecOrigin.x).SetFloat("y", m_vecOrigin.y).SetFloat("z", m_vecOrigin.z);
  gameeventmanager->FireEvent(ev);
  CBaseCSGrenadeProjectile::Detonate();
}

void CSmokeGrenadeProjectile::PhysicsSimulate() {
  if (m_bDidSmokeEffect) {
    if (ServerTime() > m_flSmokeStart + SmokeDuration()) { m_bDetonated = true; Remove(); }
    return;
  }
  CBaseCSGrenadeProjectile::PhysicsSimulate();
  float now = ServerTime();
  if (!m_bDidSmokeEffect && ((m_bStopped && now > m_flStoppedTime + 0.1f) || now > m_flSpawnTime + 6.f) && now > m_flSpawnTime + 1.0f) Detonate();
}
void CSmokeGrenadeProjectile::Detonate() {
  if (m_bDidSmokeEffect) return;
  m_bDidSmokeEffect = true; m_flSmokeStart = ServerTime();
  m_vecSmokeCenter = m_vecOrigin; m_vecVelocity.Init(); m_bStopped = true;
  s_smokes.push_back({ m_vecOrigin, m_flSmokeStart, m_flSmokeStart + SmokeDuration() });
  EmitSound("BaseSmokeEffect.Sound", m_vecOrigin, entindex(), 1.f);
  IGameEvent ev("smokegrenade_detonate"); ev.SetInt("userid", m_hThrower ? m_hThrower->entindex() : 0).SetFloat("x", m_vecOrigin.x).SetFloat("y", m_vecOrigin.y).SetFloat("z", m_vecOrigin.z).SetFloat("duration", SmokeDuration());
  gameeventmanager->FireEvent(ev);
}
float CSmokeGrenadeProjectile::GetSmokeRadius() const {
  if (!m_bDidSmokeEffect) return 0;
  float age = ServerTime() - m_flSmokeStart, left = SmokeDuration() - age;
  return SMOKE_RADIUS * Clamp(age / 0.75f, 0.f, 1.f) * Clamp(left / 1.5f, 0.f, 1.f);
}

// ------------------------------------------------------------------------------------------------
// molotov / incendiary
static bool GroundBelow(const Vector& p, float drop, Vector& out);   // defined below (inferno placement)
void CMolotovProjectile::PhysicsSimulate() {
  if (m_bDetonated) return;
  // a bottle that runs out of fuse a little short of the floor keeps flying instead of popping in mid-air: if there
  // is ground right under it, the fuse is extended (bounded, so a molotov lobbed high into the sky still bursts)
  const float dt = gpGlobals->interval_per_tick;
  if (!m_bStopped && ServerTime() + dt >= m_flDetonateTime && ServerTime() - m_flSpawnTime < 6.f) {
    Vector ground;
    if (GroundBelow(m_vecOrigin, 160.f, ground)) m_flDetonateTime = ServerTime() + 0.6f;
  }
  CBaseCSGrenadeProjectile::PhysicsSimulate();            // air burst after the fuse -> Detonate()
  if (m_bDetonated || m_bMarkedForDeletion) return;
  if ((m_bBouncedThisTick && IsDetonateFloor(m_vecLastBounceNormal)) || m_bStopped) DetonateOnGround();
}
void CMolotovProjectile::Detonate() {
  if (m_bDetonated) return;
  // the fuse ran out in the air: if there is a floor under the bottle the fire still lands there (from a real
  // height the fire would only start on the ground anyway), otherwise the bottle just pops
  Vector ground;
  if (GroundBelow(m_vecOrigin, 200.f, ground)) { m_vecOrigin = ground; DetonateOnGround(); return; }
  EmitSound(m_iWeaponID == WEAPON_MOLOTOV ? "Molotov.Detonate" : "IncGrenade.Detonate", m_vecOrigin, entindex(), 0.8f);
  IGameEvent ev("molotov_detonate"); ev.SetInt("userid", m_hThrower ? m_hThrower->entindex() : 0).SetFloat("x", m_vecOrigin.x).SetFloat("y", m_vecOrigin.y).SetFloat("z", m_vecOrigin.z).SetBool("air", true);
  gameeventmanager->FireEvent(ev);
  CBaseCSGrenadeProjectile::Detonate();
}
void CMolotovProjectile::DetonateOnGround() {
  if (m_bDetonated) return;
  EmitSound(m_iWeaponID == WEAPON_MOLOTOV ? "Molotov.Detonate" : "IncGrenade.Detonate", m_vecOrigin, entindex(), 1.f);
  IGameEvent ev("molotov_detonate"); ev.SetInt("userid", m_hThrower ? m_hThrower->entindex() : 0).SetFloat("x", m_vecOrigin.x).SetFloat("y", m_vecOrigin.y).SetFloat("z", m_vecOrigin.z).SetBool("air", false);
  gameeventmanager->FireEvent(ev);
  g_EntityList.Add(new CInferno(m_iWeaponID, m_hThrower, m_vecOrigin));
  CBaseCSGrenadeProjectile::Detonate();
}

static std::vector<CInferno*> s_infernos;
std::vector<CInferno*>& GetActiveInfernos() { return s_infernos; }
bool PointInFire(const Vector& p, float pad) { for (CInferno* f : s_infernos) if (f->IsPointBurning(p, pad)) return true; return false; }
bool LineCrossesFire(const Vector& a, const Vector& b, float pad) {
  float len = (b - a).Length(); int n = std::max(1, (int)(len / 16.f));
  for (int i = 0; i <= n; i++) if (PointInFire(a + (b - a) * ((float)i / n), pad)) return true;
  return false;
}

// ground point under p (nullopt: nothing within `drop`)
static bool GroundBelow(const Vector& p, float drop, Vector& out) {
  trace_t tr; UTIL_TraceLine(p + Vector(0, 0, 8), p - Vector(0, 0, drop), MASK_SOLID & ~CONTENTS_MONSTER, nullptr, &tr);
  if (tr.startsolid || tr.fraction >= 1.f || tr.plane.normal.z < 0.6f) return false;
  out = tr.endpos + Vector(0, 0, 1); return true;
}

CInferno::CInferno(int weaponId, CCSPlayer* thrower, const Vector& pos) {
  m_iClassname = "inferno";
  m_iWeaponID = weaponId; m_iThrower = thrower ? thrower->entindex() : 0;
  m_iTeamNum = thrower ? thrower->m_iTeamNum : 0;
  Vector g = pos; GroundBelow(pos, 160.f, g);
  m_vecCenter = m_vecOrigin = m_vecOldOrigin = g;
  m_flStart = ServerTime(); m_flEnd = m_flStart + inferno_flame_lifetime.GetFloat();
  m_flNextSpread = m_flStart + 0.08f; m_flNextSound = m_flStart + 0.4f;
  m_Flames.push_back({ g, m_flStart, 1.15f, RandomFloat(0, 6.28f) });
  s_infernos.push_back(this);
}
CInferno::~CInferno() { s_infernos.erase(std::remove(s_infernos.begin(), s_infernos.end(), this), s_infernos.end()); }

float CInferno::Fade() const {
  float now = ServerTime();
  if (now >= m_flEnd) return 0.f;
  return Clamp((m_flEnd - now) / 0.8f, 0.f, 1.f);
}
bool CInferno::IsPointBurning(const Vector& p, float pad) const {
  if (m_bExtinguished || Fade() < 0.3f) return false;
  float r = FLAME_RADIUS + pad;
  for (const InfernoFlame& f : m_Flames) {
    float dz = p.z - f.pos.z; if (dz < -24.f || dz > 56.f) continue;
    float dx = p.x - f.pos.x, dy = p.y - f.pos.y; if (dx * dx + dy * dy < r * r * f.size * f.size) return true;
  }
  return false;
}
void CInferno::Extinguish() {
  if (m_bExtinguished) return;
  m_bExtinguished = true; m_flEnd = std::min(m_flEnd, ServerTime() + 0.5f);
  EmitSound("Inferno.Extinguish", m_vecCenter, entindex(), 1.f);
  IGameEvent ev("inferno_extinguish"); ev.SetInt("userid", m_iThrower).SetFloat("x", m_vecCenter.x).SetFloat("y", m_vecCenter.y).SetFloat("z", m_vecCenter.z);
  gameeventmanager->FireEvent(ev);
}

void CInferno::PhysicsSimulate() {
  const float now = ServerTime(), dt = gpGlobals->interval_per_tick;
  if (now >= m_flEnd) {
    IGameEvent ev("inferno_expire"); ev.SetFloat("x", m_vecCenter.x).SetFloat("y", m_vecCenter.y).SetFloat("z", m_vecCenter.z); gameeventmanager->FireEvent(ev);
    Remove(); return;
  }
  // a smoke over the fire puts it out (CS:GO)
  if (!m_bExtinguished) for (const SmokeVolume& sv : s_smokes) {
    if (now < sv.start || now > sv.end) continue;
    for (const InfernoFlame& f : m_Flames) if (f.pos.DistTo(sv.center) < SMOKE_RADIUS) { Extinguish(); break; }
    if (m_bExtinguished) break;
  }
  // spread: new flames hop out from existing ones over the first ~2 seconds, never through walls, flowing down ledges
  int maxFlames = std::max(1, inferno_max_flames.GetInt());
  while (!m_bExtinguished && now >= m_flNextSpread && now < m_flStart + 2.2f && (int)m_Flames.size() < maxFlames) {
    m_flNextSpread += 0.11f;
    for (int attempt = 0; attempt < 6; attempt++) {
      const InfernoFlame& par = m_Flames[RandomInt(0, (int)m_Flames.size() - 1)];
      float a = RandomFloat(0, 6.2832f), d = RandomFloat(34.f, 52.f);
      Vector cand = par.pos + Vector(std::cos(a) * d, std::sin(a) * d, 0);
      Vector off = cand - m_vecCenter; off.z = 0;
      if (off.Length() > inferno_max_range.GetFloat()) continue;
      trace_t tr; UTIL_TraceHull(par.pos + Vector(0, 0, 12), cand + Vector(0, 0, 12), Vector(-4, -4, -4), Vector(4, 4, 4), MASK_SOLID & ~CONTENTS_MONSTER, nullptr, &tr);
      if (tr.startsolid || tr.fraction < 1.f) continue;
      Vector g; if (!GroundBelow(cand + Vector(0, 0, 12), 140.f, g)) continue;
      bool close = false; for (const InfernoFlame& f : m_Flames) if ((f.pos - g).Length() < 26.f) { close = true; break; }
      if (close) continue;
      m_Flames.push_back({ g, now, RandomFloat(0.8f, 1.1f), RandomFloat(0, 6.28f) });
      break;
    }
  }
  // fire sound (no looping channels: re-trigger the CS:GO loop sample)
  if (!m_bExtinguished && now >= m_flNextSound) { EmitSound("Inferno.Loop", m_vecCenter, entindex(), 0.7f); m_flNextSound = now + 4.3f; }
  // damage: inferno_damage per second, in 0.25 s steps, to everybody standing in it (the thrower too; teammates follow friendly fire)
  if (m_bExtinguished) return;
  CCSPlayer* thrower = m_iThrower ? UTIL_PlayerByIndex(m_iThrower) : nullptr;
  for (CCSPlayer* p : UTIL_GetPlayers(true)) {
    int i = p->entindex(); if (i <= 0 || i >= 65) continue;
    if (!IsPointBurning(p->m_vecOrigin)) continue;
    if (now < m_flNextDamage[i]) continue;
    m_flNextDamage[i] = now + 0.25f;
    CTakeDamageInfo info; info.m_hAttacker = thrower; info.m_hInflictor = this; info.m_iWeaponID = m_iWeaponID;
    info.m_flDamage = inferno_damage.GetFloat() * 0.25f; info.m_bitsDamageType = DMG_BURN;
    info.m_vecDamagePosition = p->m_vecOrigin + Vector(0, 0, 8); info.m_iHitGroup = HITGROUP_GENERIC;
    p->OnTakeDamage(info);
  }
  (void)dt;
}

// ------------------------------------------------------------------------------------------------
CPlantedC4::CPlantedC4(CCSPlayer* planter, const Vector& pos, float yaw) {
  m_iEntClass = ENTCLASS_PLANTED_C4; m_iClassname = "planted_c4";
  m_hPlanter = planter; m_vecOrigin = m_vecOldOrigin = pos; m_flYaw = yaw;
  m_flTimerLength = mp_c4timer.GetFloat();
  m_flC4Blow = ServerTime() + m_flTimerLength;
  m_flNextBeep = ServerTime() + 1.f;
  m_nBombSite = g_MapInfo.InZone(pos, ZONE_BOMBSITE_B) ? 1 : 0;
  m_iTeamNum = TEAM_TERRORIST;
  g_pPlantedC4 = this;
}
float CPlantedC4::GetTimeRemaining() const { return std::max(0.f, m_flC4Blow - ServerTime()); }
void CPlantedC4::PhysicsSimulate() {}
void CPlantedC4::StartDefuse(CCSPlayer* p) {
  if (m_hBombDefuser || m_bBombDefused || m_bExploded) return;
  m_hBombDefuser = p; p->m_bIsDefusing = true;
  m_flDefuseLength = p->m_bHasDefuser ? 5.f : 10.f;
  m_flDefuseCountDown = ServerTime() + m_flDefuseLength;
  p->SetProgressBarTime((int)m_flDefuseLength);
  EmitSound("c4.disarmstart", m_vecOrigin, entindex(), 0.7f);
  IGameEvent ev("bomb_begindefuse"); ev.SetInt("userid", p->entindex()).SetBool("haskit", p->m_bHasDefuser); gameeventmanager->FireEvent(ev);
}
void CPlantedC4::AbortDefuse() {
  if (m_hBombDefuser) { m_hBombDefuser->m_bIsDefusing = false; m_hBombDefuser->SetProgressBarTime(0); IGameEvent ev("bomb_abortdefuse"); ev.SetInt("userid", m_hBombDefuser->entindex()); gameeventmanager->FireEvent(ev); }
  m_hBombDefuser = nullptr;
}
void CPlantedC4::Think() {
  if (m_bBombDefused || m_bExploded) return;
  float now = ServerTime();
  float left = m_flC4Blow - now;
  if (now >= m_flNextBeep) {
    float interval = Clamp(left / m_flTimerLength, 0.f, 1.f) * 1.4f + 0.1f;
    if (left < 1.f) interval = 0.08f;
    m_flNextBeep = now + interval;
    EmitSound("c4.click", m_vecOrigin, entindex(), 0.8f);
    m_flNextGlow = now + 0.1f;
  }
  if (m_hBombDefuser) {
    CCSPlayer* d = m_hBombDefuser;
    if (!d->IsAlive() || !(d->m_nButtons & IN_USE) || d->EyePosition().DistTo(m_vecOrigin) > 80.f) AbortDefuse();
    else if (now >= m_flDefuseCountDown && left > 0) {
      m_bBombDefused = true; m_bBombTicking = false;
      d->m_bIsDefusing = false; d->SetProgressBarTime(0);
      EmitSound("c4.disarmfinish", m_vecOrigin, entindex(), 1.f);
      CSGameRules()->OnBombDefused(d);
      m_hBombDefuser = nullptr;
      return;
    }
  }
  if (left <= 0) Explode();
}
void CPlantedC4::Explode() {
  if (m_bExploded) return;
  m_bExploded = true; m_bBombTicking = false;
  if (m_hBombDefuser) AbortDefuse();
  CTakeDamageInfo info; info.m_hAttacker = m_hPlanter; info.m_hInflictor = this; info.m_iWeaponID = WEAPON_C4; info.m_flDamage = 500.f; info.m_bitsDamageType = DMG_BLAST;
  RadiusDamage(info, m_vecOrigin + Vector(0, 0, 8), 1750.f, nullptr);
  EmitSound("c4.explode", m_vecOrigin, entindex(), 1.f);
  IGameEvent ev("bomb_exploded"); ev.SetInt("userid", m_hPlanter ? m_hPlanter->entindex() : 0).SetInt("site", m_nBombSite).SetFloat("x", m_vecOrigin.x).SetFloat("y", m_vecOrigin.y).SetFloat("z", m_vecOrigin.z);
  gameeventmanager->FireEvent(ev);
  CSGameRules()->OnBombExploded();
}
