// cheat.cpp - hook entry points (CreateMove / OverrideView / FrameStageNotify / PaintTraverse) of the HvH cheat
#include "cheat_internal.hpp"
#include "../game/client/cl_main.hpp"
#include "../game/shared/cs_player.hpp"
#include "../game/shared/weapon_csbase.hpp"
#include "../game/shared/cs_gamerules.hpp"
#include "../game/shared/gameevents.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>

extern ConVar sv_maxusrcmdprocessticks;
extern ConVar sv_maxdtshift;

namespace cheat {
void Visuals_AddShotHitboxes(const HitboxSet& hb, int hitbox);

static int s_listener = 0;

bool InGameInput() { return cl.keyDest == KEYDEST_GAME && !cl.chatOpen; }

void Init() {
  Menu_Init();
  if (!s_listener) s_listener = gameeventmanager->AddListener(nullptr, [](const IGameEvent& ev) {
    Rage_OnEvent(ev); Misc_OnEvent(ev); Visuals_OnEvent(ev);
  });
  Log(v.misc.accent, "welcome back, %s. press INSERT to open the menu", "user");
}
void Shutdown() { Config_Save("default"); }
void LevelInit() { Rage_LevelInit(); Visuals_LevelInit(); g.choked = 0; g.shots.clear(); g.target = nullptr; g.targetValid = false; }

static bool IsShooting(const CUserCmd& cmd, CCSPlayer* lp) {
  C_WeaponCSBase* w = lp->GetActiveCSWeapon();
  if (!w) return false;
  float now = LocalAttackTime(lp);
  if (IsGrenade(w->GetCSWeaponID())) {
    CBaseCSGrenade* gr = (CBaseCSGrenade*)w;
    return gr->m_bPinPulled && !(cmd.buttons & (IN_ATTACK | IN_ATTACK2));   // released -> throw this tick
  }
  if (w->GetWeaponType() == WEAPONTYPE_KNIFE) return (cmd.buttons & (IN_ATTACK | IN_ATTACK2)) && now >= w->m_flNextPrimaryAttack;
  if (w->GetWeaponType() == WEAPONTYPE_C4) return false;
  if (w->IsRevolver() && !w->RevolverCocked(now)) return false;   // pulling the hammer back is not a shot
  return (cmd.buttons & IN_ATTACK) && now >= w->m_flNextPrimaryAttack && now >= lp->m_flNextAttack && w->m_iClip1 > 0 && !w->IsReloading();
}

void CreateMove(CUserCmd& cmd) {
  CCSPlayer* lp = g_pLocalPlayer;
  if (!lp || !lp->IsAlive() || lp->m_iTeamNum < TEAM_TERRORIST) { g.choked = 0; cmd.bSendPacket = true; return; }
  const QAngle view = cmd.viewangles;
  Misc_CreateMove(cmd, lp, view);
  size_t shotsBefore = g.shots.size();
  Rage_CreateMove(cmd, lp, view);
  if (g.shots.size() != shotsBefore && g.targetValid && g.target && v.vis.hitboxOnShot) {
    // remember the hitboxes we aimed at
    if (!g.target->m_LagRecords.empty()) {
      const ShotRecord& s = g.shots.back();
      const CNetPlayerState* rec = &g.target->m_LagRecords.front();
      for (const CNetPlayerState& r : g.target->m_LagRecords) if (r.tick == cmd.tick_count) { rec = &r; break; }
      PlayerPoseInput in; in.origin = rec->origin; in.footYaw = s.resolvedYaw; in.eyeAngles = rec->eyeAngles; in.duckAmount = rec->duckAmount;
      in.moveCycle = rec->moveCycle; in.moveSpeed = rec->moveSpeed; in.moveYaw = rec->moveYaw; in.onGround = rec->onGround; in.weaponId = rec->weaponId;
      PlayerPoseOutput po; BuildPlayerPose(g.target, in, po, true);
      Visuals_AddShotHitboxes(po.hitboxes, s.hitbox);
    }
  }
  bool shooting = IsShooting(cmd, lp);
  AA_FakeLag(cmd, lp, shooting);
  AA_CreateMove(cmd, lp, view, shooting);
  // double tap:
  //  - charge: as soon as DT is on we hold back our commands for maxShift ticks -> we freeze in place (also in the air)
  //  - shot / key released: every held tick is simulated at once -> we teleport that many ticks ahead
  //    (and the tickbase jump makes the weapon ready again, which is the "double" shot)
  int maxShift = std::min(sv_maxdtshift.GetInt(), sv_maxusrcmdprocessticks.GetInt() - 1);
  int charge = lp->m_nTickbaseShift;
  // only real gun shots use the charge: knife slashes / grenades / c4 never teleport or trigger a recharge
  C_WeaponCSBase* aw = lp->GetActiveCSWeapon();
  bool gunShot = shooting && aw && aw->IsGun();
  if (!gunShot && cmd.tickbaseShift > 0 && g.doubletapping) cmd.tickbaseShift = 0;
  if (gunShot) g.lastShotTime = ServerTime();
  if (g.doubletapping && gunShot && charge > 0) cmd.tickbaseShift = std::max(cmd.tickbaseShift, charge);
  else if (!g.doubletapping && charge > 0) cmd.tickbaseShift = charge;   // released (key / menu / fake duck) -> teleport
  else if (g.doubletapping && !shooting && cmd.tickbaseShift == 0 && charge < maxShift && ServerTime() - g.lastShotTime > 0.25f &&
           !(cmd.buttons & (IN_ATTACK | IN_ATTACK2)))
    cmd.tickbaseShift = -1;   // recharge: the whole cmd is held back -> clean hang in place (see host.cpp)
  // defensive dt: one shift when an enemy first becomes hittable while we peek (not periodically -> no stop&go)
  static bool s_defUsed = false;
  if (!g.targetValid) s_defUsed = false;
  if (g.doubletapping && v.rage.doubletapMode == 1 && !shooting && g.dtReady && g.targetValid && !s_defUsed && lp->m_vecVelocity.Length2D() > 100.f) {
    cmd.tickbaseShift = maxShift; s_defUsed = true;
  }
  FixMovement(cmd, view);
  cmd.viewangles.x = Clamp(cmd.viewangles.x, -89.f, 89.f);
  cmd.viewangles.y = AngleNormalize(cmd.viewangles.y);
  cmd.viewangles.z = 0;
  if (cmd.bSendPacket) g.fakeAngles = cmd.viewangles; else g.realAngles = cmd.viewangles;
  g.viewAngles = view;
  g.realFootYaw = lp->m_PlayerAnimState.m_flFootYaw;
  g.fakeFootYaw = lp->m_flFakeFootYaw;
}

void OverrideView(CViewSetup& view) {
  CCSPlayer* lp = g_pLocalPlayer;
  if (!lp || !lp->IsAlive()) return;
  C_WeaponCSBase* w = lp->GetActiveCSWeapon();
  bool zoomed = w && w->IsZoomed();
  float base = v.vis.fov > 0 ? v.vis.fov : FindConVar("fov_desired")->GetFloat();
  cl.unzoomedFov = base;
  if (!zoomed) { if (v.vis.fov > 0) view.fov = v.vis.fov; }
  else if (v.vis.zoomFov > 0) view.fov = view.fov + (base - view.fov) * (v.vis.zoomFov / 100.f);
}

void FrameStageNotify() {
  Rage_FrameStage();
  cl.forceCrosshair = v.vis.forceCrosshair;
  cl.thirdpersonDist = v.vis.thirdpersonDist;
  CCSPlayer* lp = g_pLocalPlayer;
  if (lp && v.vis.radarReveal) for (CCSPlayer* p : UTIL_GetPlayers(true)) if (p->IsEnemy(lp) && !p->IsDormant()) p->m_flSpottedTime = (float)gpGlobals->realtime;
}

void DrawWorld3D(const CViewSetup& view) { Visuals_DrawWorld3D(view); }
void Paint() { Visuals_Paint(); }
void PaintMenu() { Menu_Paint(); }
bool IsMenuOpen() { return g.menuOpen; }
void ToggleMenu() { g.menuOpen = !g.menuOpen; }
bool WantsMouse() { return g.menuOpen; }
bool IsKeyBindActive(const char* name) {
  if (!strcmp(name, "thirdperson")) return v.vis.thirdpersonKey.key && v.vis.thirdpersonKey.Active();
  if (!strcmp(name, "doubletap")) return g.doubletapping;
  if (!strcmp(name, "fakeduck")) return g.fakeducking;
  return false;
}
bool RemoveScope() { return v.vis.removeScope; }
bool RemoveVisualRecoil() { return v.vis.removeRecoil; }
bool RemoveFlash() { return v.vis.removeFlash; }
bool RemoveSmoke() { return v.vis.removeSmoke; }
float NightmodeAmount() { return v.vis.nightmode / 100.f; }
float ViewmodelFov() { return v.vis.viewmodelFov; }
bool ThirdpersonActive() { CCSPlayer* lp = g_pLocalPlayer; return lp && lp->IsAlive() && v.vis.thirdpersonKey.key && v.vis.thirdpersonKey.Active(); }
bool ShouldDrawLocalInThirdperson() { return true; }
float GetRealYaw() { return g_pLocalPlayer ? g_pLocalPlayer->m_PlayerAnimState.m_flFootYaw : 0.f; }

static ConCommand cmd_cheat_menu("gs_menu", [](const CCommand&) { ToggleMenu(); }, "Toggle the cheat menu");
static ConCommand cmd_cheat_save("gs_save", [](const CCommand& c) { Config_Save(c.ArgC() > 1 ? c[1] : "default"); }, "Save cheat config");
static ConCommand cmd_aw_menu("aw_menu", [](const CCommand&) { ToggleMenu(); }, "Toggle the aimwhere menu");
static ConCommand cmd_aw_save("aw_save", [](const CCommand& c) { Config_Save(c.ArgC() > 1 ? c[1] : "default"); }, "Save aimwhere config");
static ConCommand cmd_aw_load("aw_load", [](const CCommand& c) { Config_Load(c.ArgC() > 1 ? c[1] : "default"); }, "Load aimwhere config");
static ConCommand cmd_cheat_load("gs_load", [](const CCommand& c) { Config_Load(c.ArgC() > 1 ? c[1] : "default"); }, "Load cheat config");
} // namespace cheat
