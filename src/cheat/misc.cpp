// misc.cpp - bunny hop, air strafe, edge jump, quick stop, buy bot, event logs, hit sound, kill say
#include "cheat_internal.hpp"
#include "../engine/random.hpp"
#include "../game/client/cl_main.hpp"
#include "../game/shared/cs_player.hpp"
#include "../game/shared/weapon_csbase.hpp"
#include "../game/shared/cs_gamerules.hpp"
#include "../game/shared/gameevents.hpp"
#include "../game/shared/soundemitter.hpp"
#include <cmath>

namespace cheat {

static void AirStrafe(CUserCmd& cmd, CCSPlayer* lp, const QAngle& view) {
  if (v.misc.autostrafe == 0 || (lp->m_fFlags & FL_ONGROUND) || lp->m_MoveType != MOVETYPE_WALK) return;
  Vector vel = lp->m_vecVelocity; float speed = vel.Length2D();
  if (v.misc.autostrafe == 1) { // legit: follow the mouse
    if (cmd.mousedx < 0) cmd.sidemove = -450.f; else if (cmd.mousedx > 0) cmd.sidemove = 450.f;
    return;
  }
  // wish direction from WASD (directional) or always forward (rage)
  float offset = 0; bool keys = true;
  bool f = cmd.buttons & IN_FORWARD, b = cmd.buttons & IN_BACK, l = cmd.buttons & IN_MOVELEFT, r = cmd.buttons & IN_MOVERIGHT;
  if (v.misc.autostrafe == 2) {
    if (f && l) offset = 45; else if (f && r) offset = -45; else if (b && l) offset = 135; else if (b && r) offset = -135;
    else if (l) offset = 90; else if (r) offset = -90; else if (b) offset = 180; else if (f) offset = 0; else keys = false;
  }
  if (!keys) return;
  // Strict directional strafer (no smoothing, no speed build-up):
  //  every tick we solve for the exact air-acceleration vector that rotates the velocity onto the wished
  //  direction (view yaw + WASD offset) as fast as physics allows while keeping |v| constant.
  //  - it never accelerates: above the weapon's ground speed it brakes down to it, below it simply keeps the speed
  //  - once aligned there is no input at all -> perfectly straight lines / exact 45 degree diagonals
  C_WeaponCSBase* w = lp->GetActiveCSWeapon();
  float cap = w ? w->GetMaxSpeed() : 250.f;
  if (lp->m_bIsScoped && w) cap *= 0.75f;
  const float wishYaw = view.y + offset;
  cmd.buttons &= ~(IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT);
  cmd.forwardmove = cmd.sidemove = 0;
  ConVar* aaVar = FindConVar("sv_airaccelerate"); ConVar* awVar = FindConVar("sv_air_max_wishspeed");
  const float airAccel = aaVar ? aaVar->GetFloat() : 12.f;
  const float wishCap = awVar ? awVar->GetFloat() : 30.f;
  const float ft = gpGlobals->interval_per_tick;
  const float accMax = airAccel * cap * ft;                       // accelspeed with a full-length wish vector
  auto press = [&](float moveYaw, float wishspeed) {               // wishspeed in units/s (move vector length)
    float rel = DEG2RADF(AngleDiff(moveYaw, view.y));
    cmd.forwardmove = std::cos(rel) * wishspeed;
    cmd.sidemove = -std::sin(rel) * wishspeed;
  };
  if (speed < 1.f) { press(wishYaw, cap); return; }
  const float velYaw = RAD2DEGF(std::atan2(vel.y, vel.x));
  const float delta = AngleDiff(wishYaw, velYaw);
  const float targetSpeed = speed > cap ? std::max(cap, speed - wishCap) : speed;
  if (std::fabs(delta) < 0.05f && std::fabs(targetSpeed - speed) < 0.05f) return;   // aligned: coast straight
  // feasibility of reaching v* = rotate(v, delta*t) scaled to lerp(speed, targetSpeed, t) in one tick
  struct Sol { bool ok; float yaw, ws; };
  auto solve = [&](float t) -> Sol {
    float yaw = DEG2RADF(velYaw + delta * t);
    float m = speed + (targetSpeed - speed) * t;
    float dx = std::cos(yaw) * m - vel.x, dy = std::sin(yaw) * m - vel.y;
    float a = std::sqrt(dx * dx + dy * dy);
    if (a < 1e-4f) return { true, velYaw, 0.f };
    float ux = dx / a, uy = dy / a;
    float addspeed = wishCap - (vel.x * ux + vel.y * uy);           // what AirAccelerate allows along this dir
    if (a > accMax + 1e-3f || a > addspeed + 1e-3f) return { false, 0, 0 };
    return { true, RAD2DEGF(std::atan2(uy, ux)), a / (airAccel * ft) };
  };
  Sol best = solve(1.f);
  if (!best.ok) {
    float lo = 0.f, hi = 1.f;
    best = solve(0.f);
    for (int i = 0; i < 24; i++) { float mid = (lo + hi) * 0.5f; Sol sm = solve(mid); if (sm.ok) { lo = mid; best = sm; } else hi = mid; }
  }
  if (best.ok && best.ws > 0.01f) press(best.yaw, std::min(best.ws, cap));
}

static void EdgeJump(CUserCmd& cmd, CCSPlayer* lp) {
  if (!v.misc.edgeJumpKey.key || !v.misc.edgeJumpKey.Active() || !(lp->m_fFlags & FL_ONGROUND)) return;
  Vector next = lp->m_vecOrigin + lp->m_vecVelocity * (gpGlobals->interval_per_tick * 2.f);
  trace_t tr; UTIL_TraceHull(next + Vector(0, 0, 2), next - Vector(0, 0, 18), Vector(-16, -16, 0), Vector(16, 16, 2), MASK_PLAYERSOLID, lp, &tr);
  if (tr.fraction >= 1.f) cmd.buttons |= IN_JUMP;
}

static void FastStop(CUserCmd& cmd, CCSPlayer* lp, const QAngle& view) {
  if (!v.misc.fastStop || !(lp->m_fFlags & FL_ONGROUND) || (cmd.buttons & IN_JUMP)) return;
  if (cmd.buttons & (IN_FORWARD | IN_BACK | IN_MOVELEFT | IN_MOVERIGHT)) return;
  Vector vel = lp->m_vecVelocity; vel.z = 0; float speed = vel.Length();
  if (speed < 15.f) return;
  float rel = DEG2RADF(AngleDiff(RAD2DEGF(std::atan2(-vel.y, -vel.x)), view.y));
  cmd.forwardmove = std::cos(rel) * 450.f; cmd.sidemove = -std::sin(rel) * 450.f;
}

static bool s_buyPending = false;
static void DoAutoBuy();
void Misc_CreateMove(CUserCmd& cmd, CCSPlayer* lp, const QAngle& view) {
  if (s_buyPending && lp->IsAlive() && lp->CanPlayerBuy(false)) { s_buyPending = false; DoAutoBuy(); }
  if (!lp->IsAlive()) return;
  if (v.misc.bhop && (cmd.buttons & IN_JUMP) && !(lp->m_fFlags & FL_ONGROUND) && lp->m_MoveType == MOVETYPE_WALK) cmd.buttons &= ~IN_JUMP;
  AirStrafe(cmd, lp, view);
  EdgeJump(cmd, lp);
  FastStop(cmd, lp, view);
}

void Misc_AutoBuy() { s_buyPending = true; }   // executed from CreateMove once we can actually buy
static void DoAutoBuy() {
  CCSPlayer* lp = g_pLocalPlayer; if (!lp || !lp->IsAlive()) return;
  bool ct = lp->m_iTeamNum == TEAM_CT;
  std::string s;
  switch (v.misc.buyPrimary) {
    case 1: s += ct ? "buy scar20; " : "buy g3sg1; "; break;
    case 2: s += "buy ssg08; "; break;
    case 3: s += "buy awp; "; break;
    case 4: s += ct ? "buy m4a1; " : "buy ak47; "; break;
  }
  switch (v.misc.buySecondary) { case 1: s += "buy deagle; "; break; case 2: s += "buy revolver; "; break; case 3: s += "buy p250; "; break; }
  if (v.misc.buyArmor) s += "buy vesthelm; ";
  if (v.misc.buyNades) s += "buy hegrenade; buy smokegrenade; buy flashbang; ";
  if (v.misc.buyKit && ct) s += "buy defuser; ";
  // buy silently (no "insufficient funds" spam when we can't afford the full setup)
  size_t pos = 0;
  while ((pos = s.find("buy ", pos)) != std::string::npos) {
    size_t end = s.find(';', pos); std::string alias = s.substr(pos + 4, end - pos - 4);
    lp->BuyItem(alias.c_str(), true); pos = end;
  }
}

static const char* s_killsay[] = { "1", "sit", "nice resolver", "uid issue", "aimwhere > all", "hs", "by aimwhere.net", "owned by $kitty", "ez", "what's your cfg?", "refund", "ты где ресольвер потерял?" };

void Misc_OnEvent(const IGameEvent& ev) {
  CCSPlayer* lp = g_pLocalPlayer; if (!lp) return;
  int me = lp->entindex();
  if (ev.IsEvent("player_hurt")) {
    CCSPlayer* victim = UTIL_PlayerByIndex(ev.GetInt("userid")); CCSPlayer* att = UTIL_PlayerByIndex(ev.GetInt("attacker"));
    if (att == lp && victim && victim != lp) {
      if (v.misc.logHits) Log(v.misc.accent, "Hit %s in the %s for %d damage (%d health remaining)", victim->GetPlayerName(), HitgroupName(ev.GetInt("hitgroup")), ev.GetInt("dmg_health"), ev.GetInt("health"));
      ConVar& snd_hitsound = *FindConVar("snd_hitsound");
      if (v.misc.hitsound && snd_hitsound.GetInt() == 0) EmitSound2D(v.misc.hitsoundType == 0 ? "Cheat.Hitsound" : "Cheat.Hitsound2");
    } else if (victim == lp && att && att != lp && v.misc.logHurt) {
      Log(MkCol(255, 140, 60), "Harmed by %s in the %s for %d damage (%d health remaining)", att->GetPlayerName(), HitgroupName(ev.GetInt("hitgroup")), ev.GetInt("dmg_health"), ev.GetInt("health"));
    }
  } else if (ev.IsEvent("player_death")) {
    if (ev.GetInt("attacker") == me && ev.GetInt("userid") != me && v.misc.killsay)
      Cbuf_AddText(std::string("say \"") + s_killsay[RandomInt(0, (int)(sizeof(s_killsay) / sizeof(*s_killsay)) - 1)] + "\"\n");
  } else if (ev.IsEvent("item_purchase") && v.misc.logPurchases) {
    CCSPlayer* p = UTIL_PlayerByIndex(ev.GetInt("userid"));
    if (p && p != lp && p->IsEnemy(lp)) Log(MkCol(200, 200, 200), "%s bought %s", p->GetPlayerName(), ev.GetString("weapon"));
  } else if (ev.IsEvent("player_spawn") && ev.GetInt("userid") == me) {
    Misc_AutoBuy();
  }
}

} // namespace cheat
