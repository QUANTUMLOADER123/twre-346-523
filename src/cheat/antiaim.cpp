// antiaim.cpp - pitch / yaw / jitter / body yaw (desync) / freestanding / manual AA, fake lag, fake duck, slow walk
#include "cheat_internal.hpp"
#include "../engine/random.hpp"
#include "../game/client/cl_main.hpp"
#include "../game/shared/cs_player.hpp"
#include "../game/shared/weapon_csbase.hpp"
#include "../game/shared/cs_gamerules.hpp"
#include <cmath>
#include <algorithm>

extern ConVar sv_maxusrcmdprocessticks;

namespace cheat {

static CCSPlayer* ClosestEnemyToCrosshair(CCSPlayer* lp, const QAngle& view) {
  CCSPlayer* best = nullptr; float bf = 1e9f;
  for (CCSPlayer* o : UTIL_GetPlayers(true)) {
    if (!lp->IsEnemy(o)) continue;
    QAngle a = CalcAngle(lp->EyePosition(), o->m_vecOrigin + Vector(0, 0, 50));
    float f = std::fabs(AngleDiff(a.y, view.y));
    if (f < bf) { bf = f; best = o; }
  }
  return best;
}

// which side has a wall close to our head: -1 left, 1 right, 0 none
static int WallSide(CCSPlayer* lp, float yaw) {
  Vector head = lp->EyePosition();
  float best[2];
  for (int i = 0; i < 2; i++) {
    Vector dir; AngleVectors(QAngle(0, yaw + (i == 0 ? 90.f : -90.f), 0), &dir);
    trace_t tr; UTIL_TraceLine(head, head + dir * 48.f, MASK_PLAYERSOLID_BRUSHONLY, lp, &tr);
    best[i] = tr.fraction;
  }
  if (std::fabs(best[0] - best[1]) < 0.1f) return 0;
  return best[0] < best[1] ? -1 : 1;
}

// edge: a wall right in front of us -> face it
static bool EdgeYaw(CCSPlayer* lp, float& outYaw) {
  Vector head = lp->EyePosition();
  float bestFrac = 1; float bestYaw = 0;
  for (int a = 0; a < 360; a += 30) {
    Vector dir; AngleVectors(QAngle(0, (float)a, 0), &dir);
    trace_t tr; UTIL_TraceLine(head, head + dir * 36.f, MASK_PLAYERSOLID_BRUSHONLY, lp, &tr);
    if (tr.fraction < bestFrac) { bestFrac = tr.fraction; bestYaw = (float)a; }
  }
  if (bestFrac < 1.f) { outYaw = bestYaw; return true; }
  return false;
}

static void UpdateManual() {
  static bool lastL = false, lastR = false, lastB = false;
  auto& a = v.aa;
  bool l = a.manualLeft.toggled, r = a.manualRight.toggled, b = a.manualBack.toggled;
  if (l != lastL) { g.manualSide = g.manualSide == -1 ? 0 : -1; }
  if (r != lastR) { g.manualSide = g.manualSide == 1 ? 0 : 1; }
  if (b != lastB) { g.manualSide = g.manualSide == 2 ? 0 : 2; }
  lastL = l; lastR = r; lastB = b;
}

bool LocalBodyYawActive() { CCSPlayer* lp = g_pLocalPlayer; return lp && lp->IsAlive() && v.aa.enabled && v.aa.bodyYaw != 0; }

void AA_FakeLag(CUserCmd& cmd, CCSPlayer* lp, bool shooting) {
  int maxChoke = std::max(1, sv_maxusrcmdprocessticks.GetInt() - 2);
  auto& a = v.aa;
  int limit = 0;
  bool desync = a.enabled && a.bodyYaw != 0 && lp->IsAlive();
  if (desync) limit = 1;
  if (a.fakelag && lp->IsAlive()) {
    int L = (int)a.fakelagLimit;
    switch (a.fakelagMode) {
      case 0: { // dynamic: choke just enough to break lag compensation (> 64 units per packet)
        float spd = lp->m_vecVelocity.Length2D();
        int need = spd > 1 ? (int)std::ceil(64.f / (spd * gpGlobals->interval_per_tick)) + 1 : L;
        limit = std::min(L, std::max(need, 2)); break;
      }
      case 1: limit = L; break;
      case 2: { static int cyc = 0; cyc++; limit = (cyc / 16) % 2 ? L : std::max(1, (int)(L * (1 - a.fakelagVariance / 100.f))); break; }
    }
    if (a.fakelagMode != 2 && a.fakelagVariance > 0) limit = std::max(1, limit - RandomInt(0, (int)(limit * a.fakelagVariance / 100.f)));
    if (lp->m_vecVelocity.Length2D() < 5 && a.fakelagMode == 0) limit = std::max(desync ? 1 : 0, std::min(limit, 2));
  }
  if (g.doubletapping) limit = std::min(limit, 1);      // dt can't fakelag
  if (g.fakeducking) limit = maxChoke;
  limit = std::min(limit, maxChoke);
  bool send = g.choked >= limit;
  if (shooting && !a.fakelagOnShot && !g.fakeducking) send = true;
  if (shooting && g.hideshots) send = false;             // hide the shot angles from the server snapshot
  if (g.choked >= maxChoke) send = true;
  cmd.bSendPacket = send;
  g.sendPacket = send;
  g.choked = send ? 0 : g.choked + 1;
}

void AA_CreateMove(CUserCmd& cmd, CCSPlayer* lp, const QAngle& viewAngles, bool shooting) {
  auto& a = v.aa;
  UpdateManual();
  g.fakeducking = a.fakeDuckKey.key && a.fakeDuckKey.Active() && (lp->m_fFlags & FL_ONGROUND);
  g.slowwalking = a.slowWalkKey.key && a.slowWalkKey.Active() && (lp->m_fFlags & FL_ONGROUND);
  // slow walk: limit the movement speed (accurate peeking)
  if (g.slowwalking) {
    C_WeaponCSBase* w = lp->GetActiveCSWeapon();
    float maxSpd = (w ? w->GetMaxSpeed() : 250.f) * (a.slowWalkSpeed / 100.f);
    float m = std::hypot(cmd.forwardmove, cmd.sidemove);
    if (m > maxSpd) { cmd.forwardmove *= maxSpd / m; cmd.sidemove *= maxSpd / m; }
    if (lp->m_vecVelocity.Length2D() > maxSpd + 5.f) { cmd.forwardmove = cmd.sidemove = 0; }
    cmd.buttons &= ~IN_SPEED;
  }
  // fake duck: duck while choking, stand up for the sent tick -> others see us unducked
  if (g.fakeducking) {
    cmd.bFakeDuck = true;
    int maxChoke = std::max(1, sv_maxusrcmdprocessticks.GetInt() - 2);
    if (g.choked < maxChoke / 2) cmd.buttons &= ~IN_DUCK; else cmd.buttons |= IN_DUCK;
  }
  if (!a.enabled || !lp->IsAlive()) return;
  if (lp->m_MoveType == MOVETYPE_LADDER || lp->m_MoveType == MOVETYPE_NOCLIP) return;
  if (CSGameRules()->IsFreezePeriod()) return;
  if (cmd.buttons & IN_USE) return;
  C_WeaponCSBase* w = lp->GetActiveCSWeapon();
  if (w && IsGrenade(w->GetCSWeaponID())) { CBaseCSGrenade* gr = (CBaseCSGrenade*)w; if (gr->m_fThrowTime > 0) return; }
  if (shooting && !(g.hideshots && !cmd.bSendPacket) && !a.onShotAA) return;
  if (w && w->GetWeaponType() == WEAPONTYPE_KNIFE && (cmd.buttons & (IN_ATTACK | IN_ATTACK2))) return;

  float now = gpGlobals->curtime;
  // pitch
  switch (a.pitch) {
    case 1: cmd.viewangles.x = 89.f; break;
    case 2: cmd.viewangles.x = -89.f; break;
    case 3: cmd.viewangles.x = 0.f; break;
    case 4: cmd.viewangles.x = RandomFloat(-89.f, 89.f); break;
    case 5: cmd.viewangles.x = 70.f + std::sin(now * 8.f) * 15.f; break;
  }
  // yaw base
  float base = viewAngles.y;
  if (a.yawBase == 1) if (CCSPlayer* t = ClosestEnemyToCrosshair(lp, viewAngles)) base = CalcAngle(lp->m_vecOrigin, t->m_vecOrigin).y;
  float yaw = base;
  switch (a.yaw) {
    case 1: yaw += 180.f; break;
    case 2: yaw += std::fmod(now * a.spinSpeed * 36.f, 360.f); break;
    case 3: break;
    case 4: yaw += 180.f + std::sin(now * 6.f) * 30.f; break;
  }
  yaw += a.yawAdd;
  // jitter (applied on sent ticks so the networked angle flips)
  static bool jit = false;
  if (cmd.bSendPacket) jit = !jit;
  switch (a.jitter) {
    case 1: if (jit) yaw += a.jitterRange; break;
    case 2: yaw += jit ? a.jitterRange * 0.5f : -a.jitterRange * 0.5f; break;
    case 3: yaw += RandomFloat(-a.jitterRange, a.jitterRange) * 0.5f; break;
  }
  // manual / freestanding / edge
  g.freestandingActive = false;
  if (g.manualSide == -1) yaw = viewAngles.y + 90.f;
  else if (g.manualSide == 1) yaw = viewAngles.y - 90.f;
  else if (g.manualSide == 2) yaw = viewAngles.y + 180.f;
  else if (a.freestandKey.key && a.freestandKey.Active()) {
    int ws = WallSide(lp, base);
    if (ws != 0) { yaw = base + (ws == -1 ? 90.f : -90.f); g.freestandingActive = true; }
  } else if (a.edgeYaw) { float ey; if (EdgeYaw(lp, ey)) yaw = ey; }
  // body yaw side
  int side = a.inverter.toggled ? -1 : 1;
  if (a.freestandBody) { int ws = WallSide(lp, yaw); if (ws != 0) side = -ws; }
  static int jitterSide = 1;
  if (a.bodyYaw == 2 && cmd.bSendPacket) jitterSide = -jitterSide;
  if (a.bodyYaw == 2) side = jitterSide;
  if (a.bodyYaw == 3) side = -side;
  g.desyncSide = side;
  float desyncAmount = a.fakeLimit / 58.f * 120.f;
  // lby: flick on the tick the server updates the lby so the real stays desynced while standing
  bool standing = lp->m_vecVelocity.Length2D() < 3.f && (lp->m_fFlags & FL_ONGROUND);
  bool lbyFlick = a.lbyMode != 0 && standing && now >= lp->m_PlayerAnimState.m_flNextLbyUpdateTime - gpGlobals->interval_per_tick && !cmd.bSendPacket;
  if (a.bodyYaw != 0 && !cmd.bSendPacket) yaw += side * desyncAmount;
  if (lbyFlick) yaw += (a.lbyMode == 1 ? 120.f : 60.f + std::sin(now * 3.f) * 50.f) * side;
  // micro movement keeps the lby from updating while standing still
  if (a.bodyYaw != 0 && standing && std::fabs(cmd.forwardmove) < 1 && std::fabs(cmd.sidemove) < 1 && !g.fakeducking)
    cmd.sidemove = (cmd.command_number & 1) ? 1.1f : -1.1f * ((cmd.buttons & IN_DUCK) ? 3.f : 1.f);
  cmd.viewangles.y = AngleNormalize(yaw);
  cmd.viewangles.z = 0;
}

} // namespace cheat
