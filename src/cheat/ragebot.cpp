// ragebot.cpp - resolver, hitscan + multipoint, autowall, hitchance / spread compensation, backtrack,
// quick stop, autoscope, double tap, knifebot and the shot / miss log
#include "cheat_internal.hpp"
#include "../engine/random.hpp"
#include "../engine/enginetrace.hpp"
#include "../game/client/cl_main.hpp"
#include "../game/shared/cs_player.hpp"
#include "../game/shared/weapon_csbase.hpp"
#include "../game/shared/cs_gamerules.hpp"
#include "../game/shared/gameevents.hpp"
#include "../game/shared/lagcompensation.hpp"
#include <algorithm>
#include <cmath>

extern ConVar weapon_recoil_scale;
extern ConVar weapon_accuracy_nospread;
extern ConVar sv_maxusrcmdprocessticks;
extern ConVar sv_maxdtshift;
extern ConVar sv_maxunlag;

namespace cheat {

// menu hitbox groups -> hitboxes
static const std::vector<int> s_groups[8] = {
  { HITBOX_HEAD }, { HITBOX_NECK }, { HITBOX_UPPER_CHEST, HITBOX_CHEST, HITBOX_LOWER_CHEST }, { HITBOX_STOMACH }, { HITBOX_PELVIS },
  { HITBOX_RIGHT_UPPER_ARM, HITBOX_LEFT_UPPER_ARM, HITBOX_RIGHT_FOREARM, HITBOX_LEFT_FOREARM },
  { HITBOX_RIGHT_THIGH, HITBOX_LEFT_THIGH, HITBOX_RIGHT_CALF, HITBOX_LEFT_CALF }, { HITBOX_RIGHT_FOOT, HITBOX_LEFT_FOOT } };
static int GroupOfHitbox(int hb) { for (int g_ = 0; g_ < 8; g_++) for (int h : s_groups[g_]) if (h == hb) return g_; return 2; }
static bool IsBodyHitbox(int hb) { return hb == HITBOX_PELVIS || hb == HITBOX_STOMACH || hb == HITBOX_LOWER_CHEST || hb == HITBOX_CHEST || hb == HITBOX_UPPER_CHEST; }

// ------------------------------------------------------------------------------------------------
// resolver
struct ResolverInfo { float lastLby = 0; float lbyUpdateTime = -100; int freestandSide = 0; float lastFreestandCheck = -100; int lastMissed = 0; };
static ResolverInfo s_res[65];

static int FreestandSide(CCSPlayer* p, const CNetPlayerState& rec) {
  CCSPlayer* lp = g_pLocalPlayer;
  if (!lp) return 1;
  Vector eye = lp->EyePosition();
  Vector head = rec.headPos.IsZero() ? rec.origin + Vector(0, 0, 64) : rec.headPos;
  float yaw = CalcAngle(eye, head).y;
  Vector right; AngleVectors(QAngle(0, yaw - 90, 0), &right);
  float frac[2];
  for (int i = 0; i < 2; i++) {
    Vector pt = head + right * (i == 0 ? 28.f : -28.f);
    trace_t tr; UTIL_TraceLine(eye, pt, MASK_SHOT, lp, &tr);
    frac[i] = tr.fraction + (tr.m_pEnt == p ? 1.f : 0.f);
  }
  (void)p;
  // the real head is hidden behind the wall: choose the side that is more occluded
  if (std::fabs(frac[0] - frac[1]) < 0.05f) return 0;
  return frac[0] < frac[1] ? 1 : -1;
}

float Resolver_GetYaw(CCSPlayer* p, const CNetPlayerState& rec, int* sideOut) {
  int idx = p->entindex();
  ResolverInfo& ri = s_res[idx & 63];
  float eye = rec.eyeAngles.y, des = rec.maxDesync;
  int side = 0; float yaw = p->m_flClientFootYaw;
  const PlayerSettings& ps = g_PlayerList[idx & 63];
  if (ps.resolverOverride == 4 || v.rage.resolver == 0) { if (sideOut) *sideOut = 0; return p->m_flClientFootYaw; }
  if (ps.resolverOverride >= 1 && ps.resolverOverride <= 3) {
    side = ps.resolverOverride == 1 ? -1 : ps.resolverOverride == 2 ? 1 : 0;
    if (sideOut) *sideOut = side;
    return AngleNormalize(eye + side * des);
  }
  if (rec.lbyUpdated) { ri.lastLby = rec.lby; ri.lbyUpdateTime = rec.simTime; }
  bool moving = rec.velocity.Length2D() > 110.f && rec.onGround;
  if (v.rage.resolver == 1 && rec.lbyUpdated && !moving) { if (sideOut) *sideOut = 0; return rec.lby; }
  if (v.rage.resolver == 1 && moving && rec.chokedTicks <= 2) { if (sideOut) *sideOut = 0; return p->m_flClientFootYaw; }
  // freestanding guess for the first shot, then brute force by misses
  if (gpGlobals->realtime - ri.lastFreestandCheck > 0.25f) { ri.freestandSide = FreestandSide(p, rec); ri.lastFreestandCheck = gpGlobals->realtime; }
  int first = ri.freestandSide != 0 && v.rage.resolver == 1 ? ri.freestandSide : 1;
  static const float steps[5] = { 1.f, -1.f, 0.5f, -0.5f, 0.f };
  float k = steps[p->m_iMissedShots % 5] * first;
  side = k > 0 ? 1 : k < 0 ? -1 : 0;
  yaw = AngleNormalize(eye + k * des);
  if (sideOut) *sideOut = side;
  return yaw;
}

void Resolver_Update(CCSPlayer* p) {
  if (!p->IsAlive() || p->m_LagRecords.empty() || !g_pLocalPlayer || !p->IsEnemy(g_pLocalPlayer)) { p->m_bResolverActive = false; return; }
  if (v.rage.resolver == 0 || !v.rage.enabled) { p->m_bResolverActive = false; return; }
  int side = 0;
  p->m_flResolverFootYaw = Resolver_GetYaw(p, p->m_LagRecords.front(), &side);
  p->m_iResolverSide = side;
  p->m_bResolverActive = true;
}

void Rage_FrameStage() { for (CCSPlayer* p : UTIL_GetPlayers()) if (!p->IsLocalPlayer()) Resolver_Update(p); }

// ------------------------------------------------------------------------------------------------
static bool RecordValid(CCSPlayer* t, const CNetPlayerState& r) {
  if (!r.valid) return false;
  if (r.simTime < ServerTime() - sv_maxunlag.GetFloat() + gpGlobals->interval_per_tick) return false;
  // teleport check between front and this record
  const CNetPlayerState* prev = nullptr;
  for (const CNetPlayerState& x : t->m_LagRecords) {
    if (prev && (prev->origin - x.origin).Length2DSqr() > 64.f * 64.f) return false;
    if (&x == &r) return true;
    prev = &x;
  }
  return false;
}

static void BuildRecordHitboxes(CCSPlayer* t, const CNetPlayerState& rec, float footYaw, HitboxSet& out) {
  PlayerPoseInput in; in.origin = rec.origin; in.footYaw = footYaw; in.eyeAngles = rec.eyeAngles; in.duckAmount = rec.duckAmount;
  in.moveCycle = rec.moveCycle; in.moveSpeed = rec.moveSpeed; in.moveYaw = rec.moveYaw; in.onGround = rec.onGround; in.landAmount = rec.landAmount;
  in.weaponId = rec.weaponId; in.fireAmount = rec.fireAmount; in.reloadCycle = rec.reloadCycle;
  PlayerPoseOutput po; BuildPlayerPose(t, in, po, true);
  out = po.hitboxes;
}

static bool PointInCapsule(const mstudiobbox_t& b, const Vector& p, float slack = 0.f) {
  Vector ab = b.b - b.a; float l2 = ab.LengthSqr();
  float t = l2 > 0 ? Clamp((p - b.a).Dot(ab) / l2, 0.f, 1.f) : 0.f;
  return (b.a + ab * t - p).Length() <= b.radius + slack;
}

static void GetPoints(const HitboxSet& hb, int h, const Vector& eye, std::vector<Vector>& pts) {
  const mstudiobbox_t& b = hb[h];
  Vector c = (b.a + b.b) * 0.5f;
  pts.push_back(c);
  int grp = GroupOfHitbox(h);
  if (!((v.rage.multipoint >> grp) & 1)) return;
  float scale = (h == HITBOX_HEAD ? v.rage.headScale : v.rage.bodyScale) / 100.f;
  if (scale <= 0.01f) return;
  float r = b.radius * scale * 0.95f;
  Vector toEye = eye - c; toEye.z = 0; toEye.NormalizeInPlace();
  Vector side(-toEye.y, toEye.x, 0);
  if (h == HITBOX_HEAD) {
    Vector top = (b.a.z > b.b.z ? b.a : b.b);
    pts.push_back(top + Vector(0, 0, r * 0.9f));
    pts.push_back(c + side * r); pts.push_back(c - side * r);
    pts.push_back(c + side * r * 0.7f + Vector(0, 0, r * 0.6f)); pts.push_back(c - side * r * 0.7f + Vector(0, 0, r * 0.6f));
  } else if (IsBodyHitbox(h)) {
    pts.push_back(c + side * r); pts.push_back(c - side * r);
    if (h == HITBOX_PELVIS || h == HITBOX_STOMACH) pts.push_back(c - toEye * r * 0.4f + Vector(0, 0, -r * 0.3f));
  }
}

struct AimCandidate { CCSPlayer* target = nullptr; const CNetPlayerState* rec = nullptr; HitboxSet hb; int hitbox = -1; Vector point; float damage = 0; int walls = 0; bool safe = false; float resolvedYaw = 0; int side = 0; int btTicks = 0; float score = -1; };

static float MinDamageFor(CCSPlayer* t) {
  float md = v.rage.minDamageOverrideKey.key && v.rage.minDamageOverrideKey.Active() ? v.rage.minDamageOverride : v.rage.minDamage;
  int hp = std::max(1, t->m_iHealth);
  if (md <= 0) return (float)std::min(hp, 30);
  if (md > 100) return (float)(hp + (int)md - 100);
  return std::min(md, (float)hp);
}

static bool ScanRecord(CCSPlayer* lp, C_WeaponCSBase* w, const Vector& eye, CCSPlayer* t, const CNetPlayerState& rec, AimCandidate& best) {
  int side = 0;
  float yaw = Resolver_GetYaw(t, rec, &side);
  HitboxSet hb; BuildRecordHitboxes(t, rec, yaw, hb);
  bool forceBaim = (v.rage.forceBaimKey.key && v.rage.forceBaimKey.Active()) || g_PlayerList[t->entindex() & 63].forceBaim || (v.rage.preferBaim == 3 && t->m_iMissedShots >= 2) || v.rage.preferBaim == 2;
  bool forceSafe = v.rage.forceSafeKey.key && v.rage.forceSafeKey.Active();
  float minDmg = MinDamageFor(t);
  HitboxSet hbL, hbR; bool haveSafe = false;
  auto ensureSafeSets = [&]() { if (haveSafe) return; BuildRecordHitboxes(t, rec, AngleNormalize(rec.eyeAngles.y - rec.maxDesync), hbL); BuildRecordHitboxes(t, rec, AngleNormalize(rec.eyeAngles.y + rec.maxDesync), hbR); haveSafe = true; };
  int wid = w->GetCSWeaponID();
  bool found = false;
  AimCandidate local; local.score = -1;
  AimCandidate bestBody; bestBody.score = -1;
  std::vector<Vector> pts;
  for (int grp = 0; grp < 8; grp++) {
    if (!((v.rage.hitboxes >> grp) & 1)) continue;
    for (int h : s_groups[grp]) {
      bool isHead = h == HITBOX_HEAD || h == HITBOX_NECK;
      if (isHead && forceBaim) continue;
      pts.clear(); GetPoints(hb, h, eye, pts);
      for (const Vector& pt : pts) {
        int walls = 0;
        float dmg = CS_SimulateBulletDamage(lp, eye, pt, wid, t, HitboxToHitgroup(h), &walls);
        if (walls > 0 && !v.rage.autowall) continue;
        if (dmg < minDmg && dmg < t->m_iHealth) continue;
        bool safe = false;
        if (v.rage.safePoint || forceSafe) { ensureSafeSets(); safe = PointInCapsule(hbL[h], pt, -0.5f) && PointInCapsule(hbR[h], pt, -0.5f); if (forceSafe && !safe) continue; }
        float score = dmg + (safe ? 15.f : 0.f) - walls * 3.f + (pt == pts[0] ? 4.f : 0.f);
        if (isHead) score += 10.f;
        if (g_PlayerList[t->entindex() & 63].highPriority) score += 50.f;
        if (score > local.score) { local.score = score; local.hitbox = h; local.point = pt; local.damage = dmg; local.walls = walls; local.safe = safe; }
        if (IsBodyHitbox(h) && score > bestBody.score) { bestBody = local; bestBody.score = score; bestBody.hitbox = h; bestBody.point = pt; bestBody.damage = dmg; bestBody.walls = walls; bestBody.safe = safe; }
        found = true;
        if (pt == pts[0] && dmg >= t->m_iHealth && IsBodyHitbox(h) && v.rage.preferBaim == 1) break; // lethal body point found, good enough
      }
    }
  }
  if (!found) return false;
  // prefer body when it kills
  if (v.rage.preferBaim == 1 && bestBody.score > 0 && bestBody.damage >= t->m_iHealth) local = bestBody, local.score = bestBody.score + 30.f;
  if (local.score > best.score) {
    best = local; best.target = t; best.rec = &rec; best.hb = hb; best.resolvedYaw = yaw; best.side = side;
    best.btTicks = TIME_TO_TICKS(t->m_LagRecords.front().simTime - rec.simTime);
  }
  return true;
}

// spread sample matching CCSPlayer::FX_FireBullets
static Vector SpreadDir(const QAngle& ang, int seed, float inacc, float spread, int bullet = 0) {
  CUniformRandomStream rs; rs.SetSeed(seed + 1);
  float r1 = rs.RandomFloat(0, 1), p1 = rs.RandomFloat(0, 2 * M_PI_F), r2 = rs.RandomFloat(0, 1), p2 = rs.RandomFloat(0, 2 * M_PI_F);
  for (int i = 0; i < bullet; i++) { r2 = rs.RandomFloat(0, 1); p2 = rs.RandomFloat(0, 2 * M_PI_F); }
  float x = std::cos(p1) * r1 * inacc + std::cos(p2) * r2 * spread;
  float y = std::sin(p1) * r1 * inacc + std::sin(p2) * r2 * spread;
  Vector f, r, u; AngleVectors(ang, &f, &r, &u);
  Vector d = f + r * x + u * y; d.NormalizeInPlace();
  return d;
}

static int HitChance(CCSPlayer* lp, C_WeaponCSBase* w, const Vector& eye, const QAngle& ang, const AimCandidate& c, float inaccOverride = -1.f) {
  (void)lp;
  if (weapon_accuracy_nospread.GetBool()) return 100;
  float inacc = inaccOverride >= 0 ? inaccOverride : w->GetInaccuracy(), spread = w->GetSpread();
  if (inacc + spread < 0.0005f) return 100;
  int hits = 0, total = 0;
  float range = w->GetCSWpnData().range;
  for (int seed = 0; seed < 256; seed += 2) {
    Vector d = SpreadDir(ang, seed, inacc, spread);
    float t; int hb = IntersectHitboxes(c.hb, eye, d * range, t);
    total++;
    if (hb >= 0) hits++;
  }
  return total ? hits * 100 / total : 0;
}

static QAngle CompensateSpread(const QAngle& aim, int seed, float inacc, float spread) {
  QAngle a = aim;
  Vector want; AngleVectors(aim, &want);
  for (int it = 0; it < 6; it++) {
    Vector d = SpreadDir(a, seed, inacc, spread);
    QAngle got = VectorAngles(d);
    a.x -= AngleDiff(got.x, aim.x); a.y -= AngleDiff(got.y, aim.y);
  }
  return a;
}

void FixMovement(CUserCmd& cmd, const QAngle& wish) {
  float delta = DEG2RADF(AngleDiff(cmd.viewangles.y, wish.y));
  float f = cmd.forwardmove, s = cmd.sidemove;
  cmd.forwardmove = std::cos(delta) * f - std::sin(delta) * s;
  cmd.sidemove = std::sin(delta) * f + std::cos(delta) * s;
  // upside-down pitch (> 90) inverts forward
  if (std::fabs(cmd.viewangles.x) > 90.f) cmd.forwardmove = -cmd.forwardmove;
}

static void QuickStop(CUserCmd& cmd, CCSPlayer* lp, const QAngle& viewAngles, bool slide) {
  if (!(lp->m_fFlags & FL_ONGROUND)) return;
  Vector vel = lp->m_vecVelocity; vel.z = 0;
  float speed = vel.Length();
  C_WeaponCSBase* w = lp->GetActiveCSWeapon();
  float maxSpd = w ? w->GetMaxSpeed() * (lp->m_bIsScoped ? 0.75f : 1.f) : 250.f;
  if (slide) { // walk at 1/3 speed (accurate in CS:GO)
    float target = maxSpd * 0.33f;
    if (speed <= target) { cmd.buttons &= ~IN_SPEED; return; }
  }
  if (speed < 6.f) { cmd.forwardmove = cmd.sidemove = 0; return; }
  QAngle dir = VectorAngles(vel * -1.f);
  dir.y = viewAngles.y - dir.y;
  Vector fwd; AngleVectors(QAngle(0, dir.y, 0), &fwd);
  cmd.forwardmove = fwd.x * 450.f; cmd.sidemove = fwd.y * 450.f;    // sidemove positive = right
  cmd.buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT | IN_JUMP);
}

float LocalAttackTime(CCSPlayer* lp) { return TICKS_TO_TIME(lp->m_nTickBase); }

// ------------------------------------------------------------------------------------------------
// auto revolver (CS:GO R8 semantics, see C_WeaponCSBase::ItemPostFrame):
//  1. hold attack: the hammer is pulled back, the shot is armed 15 ticks later
//  2. on the last tick before the shot would go off, swap attack for attack2: with m_flNextSecondaryAttack still in
//     the future (every primary tick pushes it 0.25s ahead) attack2 does nothing and the hammer stays back
//  3. while cocked, the ragebot presses attack -> the shot leaves on that very tick
//  4. the attack2 window is over -> let go of everything for a tick (hammer drops) and start again
// returns true while the revolver is cocked (an attack this tick fires instantly)
static bool AutoRevolver(CUserCmd& cmd, CCSPlayer* lp, C_WeaponCSBase* w, bool userAttack) {
  if (!w || !w->IsRevolver()) return false;
  const float now = LocalAttackTime(lp), tick = gpGlobals->interval_per_tick;
  const bool usable = !w->IsReloading() && w->m_iClip1 > 0 && !CSGameRules()->IsFreezePeriod() && !lp->m_bIsDefusing && !lp->m_bIsPlanting &&
                      now >= lp->m_flNextAttack;
  if (!usable || userAttack) return usable && w->RevolverCocked(now);
  cmd.buttons &= ~(IN_ATTACK | IN_ATTACK2);
  if (now < w->m_flNextPrimaryAttack) return false;                      // between shots: nothing to pull yet
  if (!w->m_bRevolverHaulback) { cmd.buttons |= IN_ATTACK; return false; }
  if (!(w->m_flPostponeFireReadyTime < now)) { cmd.buttons |= IN_ATTACK; return false; }   // still pulling: attack can't fire yet
  if (now + tick * 0.5f < w->m_flNextSecondaryAttack) { cmd.buttons |= IN_ATTACK2; return true; }   // cocked, held by attack2
  return true;   // window over: release (the hammer drops after this tick) - still cocked now, a shot this tick is instant
}
bool Rage_RevolverCocked(CCSPlayer* lp, C_WeaponCSBase* w) { return w && w->IsRevolver() && w->RevolverCocked(LocalAttackTime(lp)); }

static bool CanShoot(CCSPlayer* lp, C_WeaponCSBase* w) {
  if (!w || w->IsReloading() || w->m_iClip1 <= 0) return false;
  float now = LocalAttackTime(lp);   // the weapon fires on tickbase time, not on the server clock
  if (now < lp->m_flNextAttack || now < w->m_flNextPrimaryAttack) return false;
  if (CSGameRules()->IsFreezePeriod()) return false;
  if (lp->m_bIsDefusing || lp->m_bIsPlanting) return false;
  return true;
}

// ------------------------------------------------------------------------------------------------
// shot tracking / miss reasons
static ShotRecord* s_pendingShot = nullptr;
static Vector s_lastImpact; static bool s_haveImpact = false; static int s_impactTick = -1;
static int s_hurtTick = -1; static int s_hurtVictim = -1;

static void ProcessPendingShot() {
  if (g.shots.empty()) return;
  ShotRecord& s = g.shots.back();
  if (s.acked) return;
  if (gpGlobals->tickcount <= s.tick) return;
  s.acked = true;
  if (!s.fired) { g.shots.pop_back(); return; }     // the weapon never fired (no weapon_fire) -> not a shot, no log
  CCSPlayer* t = UTIL_PlayerByIndex(s.target);
  if (s_hurtTick >= s.tick && s_hurtVictim == s.target) { s.hit = true; return; }
  if (!t) return;
  if (!t->IsAlive()) return; // killed by someone else
  const char* reason = "?";
  CCSPlayer* lp = g_pLocalPlayer;
  if (s_haveImpact && s_impactTick >= s.tick && lp) {
    Vector eye = lp->EyePosition();
    float dImpact = (s_lastImpact - eye).Length(), dTarget = (s.aimPoint - eye).Length();
    if (dImpact + 8.f < dTarget) reason = "occlusion";
    else {
      // would the bullet have hit the hitboxes we aimed at?
      HitboxSet hb;
      if (!t->m_LagRecords.empty()) {
        BuildRecordHitboxes(t, t->m_LagRecords.front(), s.resolvedYaw, hb);
        Vector dir = (s_lastImpact - eye); float tt;
        int h = IntersectHitboxes(hb, eye, dir.Normalized() * (dTarget + 64.f), tt);
        reason = h >= 0 ? "resolver" : "spread";
      }
    }
  } else reason = "spread";
  bool resolverMiss = !strcmp(reason, "resolver");
  if (resolverMiss || !strcmp(reason, "?")) t->m_iMissedShots++;
  if (v.rage.logMisses) Log(MkCol(255, 90, 90), "Missed shot due to %s (%s, hc %d%%, bt %dt%s)", reason, t->GetPlayerName(), s.hitchance, s.backtrackTicks, s.safe ? ", safe" : "");
}

void Rage_OnEvent(const IGameEvent& ev) {
  CCSPlayer* lp = g_pLocalPlayer; if (!lp) return;
  if (ev.IsEvent("weapon_fire") && ev.GetInt("userid") == lp->entindex()) {
    if (!g.shots.empty() && !g.shots.back().acked && g.shots.back().tick == gpGlobals->tickcount) g.shots.back().fired = true;
  } else if (ev.IsEvent("bullet_impact") && ev.GetInt("userid") == lp->entindex() && !ev.GetBool("exit")) {
    s_lastImpact = Vector(ev.GetFloat("x"), ev.GetFloat("y"), ev.GetFloat("z")); s_haveImpact = true; s_impactTick = gpGlobals->tickcount;
  } else if (ev.IsEvent("player_hurt") && ev.GetInt("attacker") == lp->entindex()) {
    s_hurtTick = gpGlobals->tickcount; s_hurtVictim = ev.GetInt("userid");
  } else if (ev.IsEvent("round_start")) {
    for (CCSPlayer* p : UTIL_GetPlayers()) p->m_iMissedShots = 0;
  } else if (ev.IsEvent("player_death") && ev.GetInt("userid") != lp->entindex()) {
    if (CCSPlayer* p = UTIL_PlayerByIndex(ev.GetInt("userid"))) p->m_iMissedShots = 0;
  }
}
void Rage_LevelInit() { g.shots.clear(); for (auto& r : s_res) r = ResolverInfo(); }

// ------------------------------------------------------------------------------------------------
static void Knifebot(CUserCmd& cmd, CCSPlayer* lp, C_WeaponCSBase* w) {
  if (ServerTime() < w->m_flNextPrimaryAttack || ServerTime() < lp->m_flNextAttack) return;
  Vector eye = lp->EyePosition();
  for (CCSPlayer* t : UTIL_GetPlayers(true)) {
    if (!lp->IsEnemy(t) || t->m_LagRecords.empty() || g_PlayerList[t->entindex() & 63].whitelist) continue;
    const CNetPlayerState& r = t->m_LagRecords.front();
    Vector chest = (r.hitboxes[HITBOX_CHEST].a + r.hitboxes[HITBOX_CHEST].b) * 0.5f;
    float d = (chest - eye).Length();
    if (d > 70.f) continue;
    QAngle a = CalcAngle(eye, chest);
    Vector fwdT; AngleVectors(QAngle(0, r.eyeAngles.y, 0), &fwdT);
    Vector toT = (r.origin - lp->m_vecOrigin); toT.z = 0; toT.NormalizeInPlace();
    bool back = fwdT.Dot(toT) > 0.475f;
    bool stab = d < 56.f && (t->m_iHealth <= (back ? 180 : 65));
    cmd.viewangles = a; cmd.buttons |= stab ? IN_ATTACK2 : IN_ATTACK;
    cmd.tick_count = r.tick;
    g.lastShotTime = ServerTime();
    return;
  }
}

void Rage_CreateMove(CUserCmd& cmd, CCSPlayer* lp, const QAngle& viewAngles) {
  g.targetValid = false; g.target = nullptr;
  ProcessPendingShot();
  C_WeaponCSBase* w = lp->GetActiveCSWeapon();
  // double tap charge management
  int maxShift = std::min(sv_maxdtshift.GetInt(), sv_maxusrcmdprocessticks.GetInt() - 1);
  bool dtOn = v.rage.doubletap && v.rage.doubletapKey.Active() && !g.fakeducking;
  g.doubletapping = dtOn;
  g.hideshots = v.rage.hideshots && v.rage.hideshotsKey.Active() && !dtOn;
  g.dtCharge = lp->m_nTickbaseShift; g.dtReady = g.dtCharge >= maxShift;
  if (!v.rage.enabled || !v.rage.key.Active() || !w) return;
  if (w->GetWeaponType() == WEAPONTYPE_KNIFE) { if (v.rage.knifebot) Knifebot(cmd, lp, w); return; }
  if (!w->IsGun()) return;
  const bool userAttack = (cmd.buttons & IN_ATTACK) != 0;
  const bool revolver = w->IsRevolver();
  bool revolverCocked = revolver && w->RevolverCocked(LocalAttackTime(lp));
  if (revolver && v.rage.autoRevolver) revolverCocked = AutoRevolver(cmd, lp, w, userAttack);
  bool canShoot = CanShoot(lp, w);
  Vector eye = lp->EyePosition() + lp->m_vecVelocity * gpGlobals->interval_per_tick;
  // gather targets
  std::vector<CCSPlayer*> targets;
  for (CCSPlayer* t : UTIL_GetPlayers(true)) {
    if (t == lp || !lp->IsEnemy(t) || t->m_LagRecords.empty() || t->m_bGunGameImmunity || g_PlayerList[t->entindex() & 63].whitelist) continue;
    QAngle a = CalcAngle(eye, t->m_LagRecords.front().origin + Vector(0, 0, 40));
    float fov = std::hypot(AngleDiff(a.x, viewAngles.x), AngleDiff(a.y, viewAngles.y));
    if (fov > v.rage.fov) continue;
    targets.push_back(t);
  }
  auto key = [&](CCSPlayer* t) -> float {
    const Vector& o = t->m_LagRecords.front().origin;
    switch (v.rage.target) {
      case 1: { QAngle a = CalcAngle(eye, o + Vector(0, 0, 40)); return std::hypot(AngleDiff(a.x, viewAngles.x), AngleDiff(a.y, viewAngles.y)); }
      case 2: return (o - eye).Length();
      case 3: return (float)t->m_iHealth;
      default: return (float)((t->entindex() + gpGlobals->tickcount / 16) % 64);
    }
  };
  std::sort(targets.begin(), targets.end(), [&](CCSPlayer* a, CCSPlayer* b) { return key(a) < key(b); });
  if (targets.size() > 4) targets.resize(4);  // performance: scan the 4 best
  AimCandidate best;
  for (CCSPlayer* t : targets) {
    const CNetPlayerState& front = t->m_LagRecords.front();
    ScanRecord(lp, w, eye, t, front, best);
    if (v.rage.backtrack && t->m_LagRecords.size() > 1) {
      // oldest valid record (people peeking forward leave their old position exposed)
      const CNetPlayerState* oldest = nullptr;
      for (const CNetPlayerState& r : t->m_LagRecords) { if (&r == &front) continue; if (!RecordValid(t, r)) break; oldest = &r; }
      if (oldest) { AimCandidate bt; if (ScanRecord(lp, w, eye, t, *oldest, bt) && bt.score > best.score + 5.f) best = bt; }
      // record where the lby flicked (real yaw known)
      for (const CNetPlayerState& r : t->m_LagRecords) { if (&r == &front || !RecordValid(t, r)) break; if (r.lbyUpdated) { AimCandidate lb; if (ScanRecord(lp, w, eye, t, r, lb) && lb.score + 10.f > best.score) best = lb; break; } }
    }
  }
  if (!best.target) return;
  g.target = best.target; g.targetHitbox = best.hitbox; g.targetPoint = best.point; g.targetValid = true;
  // with spread compensation (or a nospread server) every bullet goes exactly where we aim:
  // no autoscope, no quick stop, no hitchance, no unduck delay
  bool perfect = v.rage.compensateSpread || weapon_accuracy_nospread.GetBool();
  bool sniper = IsSniper(w->GetCSWeaponID());
  bool onGround = (lp->m_fFlags & FL_ONGROUND) != 0;
  // auto stop only right before a real shot:
  //  - we actually want to shoot (auto fire on, or the attack button is held)
  //  - the weapon is ready now, or (between shots) becomes ready within a few ticks
  //  - never while reloading / empty / in freeze time / planting (CanShoot covers this), never in the air
  bool wantShoot = v.rage.autoFire || userAttack;
  float readyIn = std::max(w->m_flNextPrimaryAttack, lp->m_flNextAttack) - LocalAttackTime(lp);
  bool soonReady = !w->IsReloading() && w->m_iClip1 > 0 && !CSGameRules()->IsFreezePeriod() && !lp->m_bIsDefusing && !lp->m_bIsPlanting &&
                   readyIn <= TICKS_TO_TIME(3);
  bool stopAllowed = v.rage.quickStop && onGround && wantShoot && (canShoot || (v.rage.quickStopBetweenShots && soonReady));
  if (!perfect && v.rage.autoscope && sniper && !w->IsZoomed() && onGround) {
    if (LocalAttackTime(lp) >= w->m_flNextSecondaryAttack && wantShoot) cmd.buttons |= IN_ATTACK2;
    if (stopAllowed) QuickStop(cmd, lp, viewAngles, v.rage.quickStop == 2);
    return;
  }
  if (!perfect && v.rage.delayShot && lp->m_flDuckAmount > 0.05f && lp->m_flDuckAmount < 0.95f) return;
  // aim
  QAngle aim = CalcAngle(eye, best.point);
  if (v.rage.removeRecoil) { aim.x -= lp->m_aimPunchAngle.x * weapon_recoil_scale.GetFloat(); aim.y -= lp->m_aimPunchAngle.y * weapon_recoil_scale.GetFloat(); }
  int hc = 100;
  if (perfect) {
    if (!canShoot) return;
    if (v.rage.compensateSpread) aim = CompensateSpread(aim, cmd.random_seed & 255, w->GetInaccuracy(), w->GetSpread());
  } else {
    int need = (int)v.rage.hitchance;
    hc = HitChance(lp, w, eye, aim, best);
    if (need > 0 && hc < need) {
      // quick stop only when standing still would actually reach the hitchance (otherwise keep moving / anti-aiming)
      if (stopAllowed) {
        const CCSWeaponInfo& wi = w->GetCSWpnData();
        bool ducked = lp->m_flDuckAmount > 0.9f;
        float still = (ducked ? (w->IsZoomed() ? wi.inaccuracyCrouchAlt : wi.inaccuracyCrouch) : (w->IsZoomed() ? wi.inaccuracyStandAlt : wi.inaccuracyStand)) / 1000.f;
        if (HitChance(lp, w, eye, aim, best, still) >= need) QuickStop(cmd, lp, viewAngles, v.rage.quickStop == 2);
      }
      return;
    }
    if (stopAllowed && !canShoot) QuickStop(cmd, lp, viewAngles, v.rage.quickStop == 2);
    if (!canShoot) return;
  }
  aim.x = Clamp(aim.x, -89.f, 89.f); aim.y = AngleNormalize(aim.y); aim.z = 0;
  if (!v.rage.autoFire && !userAttack) return;
  if (revolver && !revolverCocked) {
    // the hammer is not back yet: hold attack to pull it (aim already, but this tick is not a shot)
    cmd.buttons &= ~IN_ATTACK2; cmd.buttons |= IN_ATTACK;
    if (!v.rage.silent) CL_SetViewAngles(aim);
    return;
  }
  cmd.viewangles = aim;
  cmd.buttons &= ~IN_ATTACK2;
  cmd.buttons |= IN_ATTACK;
  cmd.tick_count = best.rec->tick;
  if (!v.rage.silent) CL_SetViewAngles(aim);
  // double tap: shift the tickbase right after the shot so the next attack is available immediately
  if (dtOn && g.dtReady) cmd.tickbaseShift = maxShift;
  g.lastShotTime = ServerTime();
  ShotRecord s{}; s.target = best.target->entindex(); s.hitbox = best.hitbox; s.aimPoint = best.point; s.time = ServerTime(); s.tick = gpGlobals->tickcount;
  s.resolved = true; s.resolvedYaw = best.resolvedYaw; s.side = best.side; s.damage = best.damage; s.hitchance = hc; s.backtrackTicks = best.btTicks; s.safe = best.safe; s.fired = false;
  g.shots.push_back(s); while (g.shots.size() > 32) g.shots.pop_front();
  s_haveImpact = false;
}

} // namespace cheat
