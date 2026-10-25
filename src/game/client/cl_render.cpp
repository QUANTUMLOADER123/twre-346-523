// cl_render.cpp - view setup (CViewRender), entity rendering (DrawModel), client event effects
#include "cl_main.hpp"
#include "../../engine/host.hpp"
#include "../../engine/worldrender.hpp"
#include "../../engine/materialsystem.hpp"
#include "../../engine/studio.hpp"
#include "../../engine/random.hpp"
#include "../shared/cs_player.hpp"
#include "../shared/weapon_csbase.hpp"
#include "../shared/cs_projectiles.hpp"
#include "../shared/cs_gamerules.hpp"
#include "../shared/gameevents.hpp"
#include "../shared/mapinfo.hpp"
#include "../shared/soundemitter.hpp"
#include "../../cheat/cheat.hpp"
#include "rlgl.h"
#include <cmath>
#include <algorithm>

ConVar fov_desired("fov_desired", "90", FCVAR_ARCHIVE, "Sets the base field-of-view.", true, 75, true, 110);
extern ConVar cl_interp;
ConVar cl_interpolate("cl_interpolate", "1", FCVAR_ARCHIVE, "Interpolate entities on the client.");
ConVar cam_idealdist("cam_idealdist", "120", FCVAR_ARCHIVE, "Thirdperson camera distance");
ConVar view_recoil_tracking("view_recoil_tracking", "0.45", FCVAR_CHEAT | FCVAR_REPLICATED, "How closely the view tracks with the aim punch from weapon recoil");
ConVar r_drawothermodels("r_drawothermodels", "1", FCVAR_CHEAT, "0=Off, 1=Normal, 2=Wireframe");
ConVar r_drawentities("r_drawentities", "1", FCVAR_CHEAT, "");
ConVar cl_righthand("cl_righthand", "1", FCVAR_ARCHIVE, "Use right-handed view models.");
ConVar cl_showpos("cl_showpos", "0", FCVAR_ARCHIVE, "Draw current position at top of screen");
ConVar cl_showfps("cl_showfps", "0", FCVAR_ARCHIVE, "Draw fps meter at top of screen (1 = fps, 2 = smooth fps)");
ConVar net_graph("net_graph", "0", FCVAR_ARCHIVE, "Draw the network usage graph");
extern ConVar mat_fullbright;
ConVar r_shadows("r_shadows", "1", FCVAR_ARCHIVE, "Draw blob shadows under players");
extern ConVar weapon_recoil_scale;

std::function<void()> g_CheatPaintHook;
static float s_shakeAmount = 0, s_shakeEnd = 0;

bool CL_IsInGame() { return g_Host.active && g_pLocalPlayer; }
CCSPlayer* CL_GetViewPlayer() { return cl.viewEntity; }

// ------------------------------------------------------------------------------------------------
// interpolation
float CL_GetRenderTime() {
  float interp = cl_interpolate.GetBool() ? std::max(cl_interp.GetFloat(), gpGlobals->interval_per_tick) : 0.f;
  return ServerTime() + g_Host.frameAlpha * gpGlobals->interval_per_tick - interp;
}

bool CL_GetInterpolatedState(CCSPlayer* p, InterpState& out) {
  out.valid = false;
  if (p->m_LagRecords.empty()) return false;
  float rt = CL_GetRenderTime();
  const CLagRecord* a = nullptr; const CLagRecord* b = nullptr;
  auto& recs = p->m_LagRecords; // front = newest
  if (rt >= recs.front().simTime || recs.size() == 1) { a = b = &recs.front(); }
  else {
    for (size_t i = 0; i + 1 < recs.size(); i++) {
      if (recs[i + 1].simTime <= rt && rt <= recs[i].simTime) { a = &recs[i + 1]; b = &recs[i]; break; }
    }
    if (!a) a = b = &recs.back();
  }
  float t = (b->simTime > a->simTime) ? Clamp((rt - a->simTime) / (b->simTime - a->simTime), 0.f, 1.f) : 1.f;
  if ((b->origin - a->origin).Length2D() > 64.f) t = 1.f; // teleport / lag compensation broken -> snap
  out.origin = VectorLerp(a->origin, b->origin, t);
  out.eyeAngles = QAngle(FLerp(a->eyeAngles.x, b->eyeAngles.x, t), LerpAngle(a->eyeAngles.y, b->eyeAngles.y, t), 0);
  out.duck = FLerp(a->duckAmount, b->duckAmount, t);
  out.moveCycle = b->moveCycle; out.moveSpeed = FLerp(a->moveSpeed, b->moveSpeed, t); out.moveYaw = b->moveYaw;
  out.onGround = b->onGround; out.weaponId = b->weaponId; out.fireAmount = b->fireAmount; out.reloadCycle = b->reloadCycle; out.land = b->landAmount;
  // feet yaw: what this client's animstate computed (desync!) or the resolver's guess
  out.footYaw = p->m_bResolverActive ? p->m_flResolverFootYaw : p->m_flClientFootYaw;
  out.valid = true;
  return true;
}

// ------------------------------------------------------------------------------------------------
// view
void CL_ComputeView(CViewSetup& view) {
  CCSPlayer* lp = g_pLocalPlayer;
  float alpha = g_Host.frameAlpha;
  view.width = GetScreenWidth(); view.height = GetScreenHeight();
  view.fov = fov_desired.GetFloat();
  cl.firstPerson = true;
  cl.viewEntity = nullptr;
  if (!lp) return;
  QAngle ang = cl.viewangles;
  Vector org;
  if (lp->IsAlive()) {
    cl.viewEntity = lp;
    org = VectorLerp(lp->m_vecOldOrigin, lp->m_vecOrigin, alpha) + lp->m_vecViewOffset;
    if (!cl.removeVisualRecoil) {
      ang += lp->m_viewPunchAngle;
      ang += lp->m_aimPunchAngle * (weapon_recoil_scale.GetFloat() * view_recoil_tracking.GetFloat());
    }
    // zoom
    C_WeaponCSBase* w = lp->GetActiveCSWeapon();
    int zoomFov = (w && w->m_zoomLevel > 0) ? w->GetZoomFOV(w->m_zoomLevel) : 0;
    if (zoomFov > 0) view.fov = (float)zoomFov;
    else if (cl.overrideFov > 0) view.fov = cl.overrideFov;
    lp->m_iFOV = zoomFov > 0 ? zoomFov : 90;
  } else {
    // observer
    float sinceDeath = ServerTime() - lp->m_flDeathTime;
    if (lp->m_iObserverMode == OBS_MODE_DEATHCAM && lp->m_iTeamNum >= TEAM_TERRORIST) {
      if (sinceDeath > 3.f) lp->m_iObserverMode = OBS_MODE_IN_EYE;
      // look at the killer / own corpse from above
      Vector corpse = lp->m_vecOrigin;
      CCSPlayer* killer = ToCSPlayer(lp->m_hLastAttacker);
      Vector look = (killer && killer->IsAlive() && killer != lp) ? killer->EyePosition() : corpse;
      float k = Clamp(sinceDeath / 1.f, 0.f, 1.f);
      org = corpse + Vector(0, 0, 64 + 48 * k);
      Vector back; AngleVectors(QAngle(0, lp->m_flDeathYaw, 0), &back);
      org -= back * (60 * k);
      trace_t tr; g_EngineTrace.TraceHull(corpse + Vector(0, 0, 50), org, Vector(-6, -6, -6), Vector(6, 6, 6), MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
      org = tr.endpos;
      ang = VectorAngles(look - org);
      cl.firstPerson = false;
    } else if (lp->m_iObserverMode == OBS_MODE_IN_EYE || lp->m_iObserverMode == OBS_MODE_CHASE) {
      std::vector<CCSPlayer*> cands;
      for (CCSPlayer* p : UTIL_GetPlayers(true)) if (p != lp && (lp->m_iTeamNum < TEAM_TERRORIST || p->m_iTeamNum == lp->m_iTeamNum)) cands.push_back(p);
      if (cands.empty()) for (CCSPlayer* p : UTIL_GetPlayers(true)) if (p != lp) cands.push_back(p);
      if (cands.empty()) { org = lp->m_vecOrigin + Vector(0, 0, 64); cl.firstPerson = false; }
      else {
        int n = (int)cands.size(); int idx = ((cl.specTarget % n) + n) % n;
        CCSPlayer* t = cands[idx];
        cl.viewEntity = t;
        if (lp->m_iObserverMode == OBS_MODE_IN_EYE) {
          org = VectorLerp(t->m_vecOldOrigin, t->m_vecOrigin, alpha) + t->m_vecViewOffset;
          ang = t->m_net.eyeAngles; if (t->m_bIsBot && t->m_LastCmd.command_number) ang = t->v_angle;
        } else {
          cl.firstPerson = false;
          Vector fwd; AngleVectors(cl.viewangles, &fwd);
          Vector eye = VectorLerp(t->m_vecOldOrigin, t->m_vecOrigin, alpha) + Vector(0, 0, 56);
          trace_t tr; g_EngineTrace.TraceHull(eye, eye - fwd * 110.f, Vector(-6, -6, -6), Vector(6, 6, 6), MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
          org = tr.endpos; ang = cl.viewangles;
        }
      }
    } else {
      // roaming
      org = VectorLerp(lp->m_vecOldOrigin, lp->m_vecOrigin, alpha);
      cl.firstPerson = false;
    }
  }
  // screen shake (explosions)
  if (gpGlobals->realtime < s_shakeEnd) {
    float a = s_shakeAmount * (s_shakeEnd - gpGlobals->realtime);
    ang.x += RandomFloat(-a, a) * 0.6f; ang.y += RandomFloat(-a, a) * 0.6f; org.z += RandomFloat(-a, a) * 0.3f;
  }
  view.origin = org; view.angles = ang;
  // thirdperson for the alive local player
  if (lp->IsAlive() && (cl.thirdperson || cheat::ThirdpersonActive())) {
    Vector fwd; AngleVectors(cl.viewangles, &fwd);
    float dist = cl.thirdpersonDist > 0 ? cl.thirdpersonDist : cam_idealdist.GetFloat();
    trace_t tr; g_EngineTrace.TraceHull(org, org - fwd * dist, Vector(-8, -8, -8), Vector(8, 8, 8), MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
    view.origin = tr.endpos; view.angles = cl.viewangles;
    cl.firstPerson = false;
  }
  cheat::OverrideView(view);
  view.Compute();
  cl.viewOrigin = view.origin; cl.viewAngles = view.angles; cl.fov = view.fov;
}

// ------------------------------------------------------------------------------------------------
// entity rendering
static StudioLight LightAt(const Vector& p) {
  StudioLight L;
  if (g_WorldRender.built) {
    LightSample s = g_WorldRender.SampleLight(p);
    L.ambient = s.ambient; L.direct = s.direct; L.dir = s.dir;
  }
  float b = cl.worldBrightness;
  L.ambient = L.ambient * (0.35f + 0.65f * b); L.direct = L.direct * b;
  if (mat_fullbright.GetBool()) { L.ambient = Vector(1, 1, 1); L.direct = Vector(0, 0, 0); }
  if (cl.nightvision) { L.ambient = Vector(0.9f, 1.2f, 0.9f); }
  return L;
}

static void DrawBlobShadow(const Vector& origin, float radius, float alpha) {
  if (!r_shadows.GetBool()) return;
  trace_t tr; g_EngineTrace.TraceLine(origin + Vector(0, 0, 8), origin - Vector(0, 0, 96), MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
  if (tr.fraction >= 1.f || tr.plane.normal.z < 0.7f) return;
  float a = alpha * (1.f - tr.fraction);
  Vector c = tr.endpos + Vector(0, 0, 0.4f);
  rlSetTexture(g_MaterialSystem.glowDot.id);
  rlBegin(RL_QUADS);
  rlColor4ub(0, 0, 0, (unsigned char)(150 * a));
  rlTexCoord2f(0, 0); rlVertex3f(c.x - radius, c.y - radius, c.z);
  rlTexCoord2f(0, 1); rlVertex3f(c.x - radius, c.y + radius, c.z);
  rlTexCoord2f(1, 1); rlVertex3f(c.x + radius, c.y + radius, c.z);
  rlTexCoord2f(1, 0); rlVertex3f(c.x + radius, c.y - radius, c.z);
  rlEnd(); rlSetTexture(0);
}

void CCSPlayer::BuildRenderPose(float alpha, bool useNet) {
  CStudioModel* mdl = GetModel(); if (!mdl) return;
  PlayerPoseInput in;
  if (!IsAlive()) {
    in.origin = m_vecOrigin; in.footYaw = m_net.footYaw; in.eyeAngles = m_net.eyeAngles;
    in.deathCycle = Clamp((ServerTime() - m_flDeathTime) / 0.9f, 0.f, 1.f); in.deathYaw = m_flDeathYaw - m_net.footYaw;
    in.weaponId = 0;
  } else if (useNet) {
    InterpState st; if (!CL_GetInterpolatedState(this, st)) return;
    in.origin = st.origin; in.footYaw = st.footYaw; in.eyeAngles = st.eyeAngles; in.duckAmount = st.duck;
    in.moveCycle = st.moveCycle; in.moveSpeed = st.moveSpeed; in.moveYaw = st.moveYaw; in.onGround = st.onGround; in.landAmount = st.land;
    in.weaponId = st.weaponId; in.fireAmount = st.fireAmount; in.reloadCycle = st.reloadCycle;
    in.planting = m_bIsPlanting; in.defusing = m_bIsDefusing;
  } else {
    const CCSGOPlayerAnimState& as = m_PlayerAnimState;
    in.origin = VectorLerp(m_vecOldOrigin, m_vecOrigin, alpha); in.footYaw = as.m_flFootYaw; in.eyeAngles = v_angle;
    in.duckAmount = m_flDuckAmount; in.moveCycle = as.m_flMoveCycle; in.moveSpeed = as.m_flSpeedAsPortionOfRunTopSpeed; in.moveYaw = as.m_flMoveYaw;
    in.onGround = (m_fFlags & FL_ONGROUND) != 0; in.landAmount = as.m_flLandAmount;
    C_WeaponCSBase* w = GetActiveCSWeapon(); in.weaponId = w ? w->GetCSWeaponID() : 0;
    in.fireAmount = Clamp(1.f - (gpGlobals->curtime - m_flLastFireTime) / 0.15f, 0.f, 1.f);
    in.reloadCycle = (w && w->m_bInReload && w->m_flReloadFinishTime > w->m_flReloadStartTime) ? Clamp((ServerTime() - w->m_flReloadStartTime) / (w->m_flReloadFinishTime - w->m_flReloadStartTime), 0.f, 1.f) : -1.f;
    in.planting = m_bIsPlanting; in.defusing = m_bIsDefusing;
    if (m_LastCmd.bFakeDuck && (m_fFlags & FL_ONGROUND)) in.duckAmount = m_net.duckAmount;
  }
  m_vecRenderOrigin = in.origin;
  BuildPlayerPose(*mdl, GetWeaponWorldModel(in.weaponId), in, m_RenderPose, true);
  m_bRenderPoseValid = true;
  if (useNet) { m_ClientPose = m_RenderPose; m_bClientPoseValid = true; }
}

void CCSPlayer::DrawModel(const CViewSetup& view) {
  if (!r_drawothermodels.GetBool() && !IsLocalPlayer()) return;
  if (m_iTeamNum < TEAM_TERRORIST) return;
  if (!IsAlive() && (m_iObserverMode == OBS_MODE_ROAMING || ServerTime() - m_flDeathTime > 30.f)) return;
  bool isViewEnt = (cl.viewEntity == this) && cl.firstPerson;
  if (isViewEnt) return;
  if (!IsAlive() && m_flDeathTime <= 0) return;
  bool useNet = IsAlive() && !IsLocalPlayer();
  BuildRenderPose(g_Host.frameAlpha, useNet);
  if (!m_bRenderPoseValid) return;
  CStudioModel* mdl = GetModel(); if (!mdl) return;
  StudioLight light = LightAt(m_vecRenderOrigin + Vector(0, 0, 40));
  const CStudioModel* wm = m_RenderPose.hasWeapon ? GetWeaponWorldModel(IsAlive() ? (useNet ? m_LagRecords.front().weaponId : (GetActiveCSWeapon() ? GetActiveCSWeapon()->GetCSWeaponID() : 0)) : 0) : nullptr;
  DrawBlobShadow(m_vecRenderOrigin, 22.f, IsAlive() ? 1.f : 0.6f);
  if (IsAlive() && cheat::DrawPlayer(this, *mdl, m_RenderPose, wm, light, view)) return;
  StudioDrawParams p; p.pose = &m_RenderPose.pose; p.modelToWorld = m_RenderPose.modelToWorld; p.light = light; p.eye = view.origin;
  if (r_drawothermodels.GetInt() == 2) p.flags |= STUDIO_WIREFRAME;
  StudioRender_DrawModel(*mdl, p);
  if (wm && m_RenderPose.hasWeapon) { StudioDrawParams g = p; g.pose = nullptr; g.modelToWorld = m_RenderPose.weaponToWorld; StudioRender_DrawModel(*wm, g); }
}
void CCSPlayer::DrawTranslucent(const CViewSetup&) {}

void C_WeaponCSBase::DrawModel(const CViewSetup&) {
  if (m_hOwner) return; // carried weapons are drawn by the player
  CStudioModel* mdl = GetWeaponWorldModelById(m_iWeaponID); if (!mdl) return;
  Vector o = GetRenderOrigin(g_Host.frameAlpha);
  matrix3x4_t rot; AngleMatrix(QAngle(0, m_angRotation.y, 90), rot);
  if (m_iWeaponID == WEAPON_C4 || IsGrenade(m_iWeaponID)) AngleMatrix(QAngle(0, m_angRotation.y, 0), rot);
  rot.SetOrigin(o + Vector(0, 0, IsGrenade(m_iWeaponID) || m_iWeaponID == WEAPON_C4 ? 0.5f : 1.2f));
  StudioDrawParams p; p.modelToWorld = rot; p.light = LightAt(o + Vector(0, 0, 8));
  // dropped C4 blinks
  if (m_iWeaponID == WEAPON_C4 && std::fmod(gpGlobals->realtime, 1.f) < 0.15f) { p.flags |= STUDIO_TINT; p.colorOverride = { 255, 120, 120, 255 }; }
  StudioRender_DrawModel(*mdl, p);
}

void CBaseCSGrenadeProjectile::DrawModel(const CViewSetup&) {
  if (m_bDetonated && m_iWeaponID != WEAPON_SMOKEGRENADE) return;
  CStudioModel* mdl = GetWeaponWorldModelById(m_iWeaponID); if (!mdl) return;
  Vector o = GetRenderOrigin(g_Host.frameAlpha);
  matrix3x4_t rot; AngleMatrix(m_angRotation, rot); rot.SetOrigin(o);
  StudioDrawParams p; p.modelToWorld = rot; p.light = LightAt(o);
  StudioRender_DrawModel(*mdl, p);
}
void CBaseCSGrenadeProjectile::DrawTranslucent(const CViewSetup&) {}
void CSmokeGrenadeProjectile::DrawTranslucent(const CViewSetup&) {}

void CPlantedC4::DrawModel(const CViewSetup&) {
  CStudioModel* mdl = GetWeaponWorldModelById(WEAPON_C4); if (!mdl) return;
  matrix3x4_t rot; AngleMatrix(QAngle(0, m_flYaw, 0), rot); rot.SetOrigin(m_vecOrigin + Vector(0, 0, 0.5f));
  StudioDrawParams p; p.modelToWorld = rot; p.light = LightAt(m_vecOrigin + Vector(0, 0, 8));
  StudioRender_DrawModel(*mdl, p);
  // blinking LED
  if (!m_bExploded && !m_bBombDefused) {
    float rem = GetTimeRemaining(); float period = rem > 10 ? 1.f : rem > 5 ? 0.5f : 0.25f;
    if (std::fmod(gpGlobals->realtime, period) < 0.12f) {
      Vector led = m_vecOrigin + Vector(0, 0, 6);
      rlDrawRenderBatchActive(); BeginBlendMode(BLEND_ADDITIVE); rlDisableDepthMask();
      rlSetTexture(g_MaterialSystem.flare.id); rlBegin(RL_QUADS); rlColor4ub(255, 40, 30, 255);
      const CViewSetup& v = g_CurrentView; Vector r = v.right * 5.f, u = v.up * 5.f;
      rlTexCoord2f(0, 1); rlVertex3f(led.x - r.x - u.x, led.y - r.y - u.y, led.z - r.z - u.z);
      rlTexCoord2f(1, 1); rlVertex3f(led.x + r.x - u.x, led.y + r.y - u.y, led.z + r.z - u.z);
      rlTexCoord2f(1, 0); rlVertex3f(led.x + r.x + u.x, led.y + r.y + u.y, led.z + r.z + u.z);
      rlTexCoord2f(0, 0); rlVertex3f(led.x - r.x + u.x, led.y - r.y + u.y, led.z - r.z + u.z);
      rlEnd(); rlSetTexture(0); rlDrawRenderBatchActive(); rlEnableDepthMask(); EndBlendMode();
    }
  }
}

void CL_DrawEntities(const CViewSetup& view) {
  if (!r_drawentities.GetBool()) return;
  for (C_BaseEntity* e : g_EntityList.All()) {
    if (!e || e->m_bMarkedForDeletion) continue;
    // cull far / behind
    Vector c = e->WorldSpaceCenter();
    if ((c - view.origin).Dot(view.forward) < -80.f) continue;
    e->DrawModel(view);
  }
}

// ------------------------------------------------------------------------------------------------
// level / events
static int s_eventListener = 0;
static Vector MuzzlePos(CCSPlayer* p) {
  if (p->m_bRenderPoseValid && p->m_RenderPose.hasWeapon) {
    const matrix3x4_t& w = p->m_RenderPose.weaponToWorld;
    CStudioModel* wm = p->GetWeaponWorldModel(p->m_LagRecords.empty() ? 0 : p->m_LagRecords.front().weaponId);
    if (wm) return VectorTransform(Vector(wm->maxs.x, 0, (wm->mins.z + wm->maxs.z) * 0.6f), w);
  }
  Vector f, r, u; AngleVectors(p->m_net.eyeAngles, &f, &r, &u);
  return p->m_net.origin + Vector(0, 0, 58) + f * 30 + r * 4;
}
extern Vector VM_GetMuzzlePos();
extern void VM_OnFire();

static void OnGameEvent(const IGameEvent& e) {
  CCSPlayer* lp = g_pLocalPlayer;
  if (e.IsEvent("weapon_fire")) {
    CCSPlayer* p = UTIL_PlayerByIndex(e.GetInt("userid")); if (!p) return;
    if (!strcmp(e.GetString("weapon"), "knife") || IsGrenade(AliasToWeaponID(e.GetString("weapon")))) return;
    bool silenced = e.GetBool("silenced");
    if (p == cl.viewEntity && cl.firstPerson) { VM_OnFire(); if (!silenced) FX_MuzzleFlash(VM_GetMuzzlePos(), cl.view.forward, 0.6f); }
    else if (!silenced) { Vector f; AngleVectors(p->m_net.eyeAngles, &f); FX_MuzzleFlash(MuzzlePos(p), f, 1.f); }
  } else if (e.IsEvent("bullet_impact")) {
    Vector pos(e.GetFloat("x"), e.GetFloat("y"), e.GetFloat("z")), n(e.GetFloat("nx"), e.GetFloat("ny"), e.GetFloat("nz"));
    int victim = e.GetInt("player");
    FX_Impact(pos, n, e.GetInt("surf"), victim > 0 && UTIL_PlayerByIndex(victim) != nullptr);
    if (e.GetInt("tracer")) {
      CCSPlayer* p = UTIL_PlayerByIndex(e.GetInt("userid"));
      Vector start(e.GetFloat("sx"), e.GetFloat("sy"), e.GetFloat("sz"));
      if (p && p == cl.viewEntity && cl.firstPerson) start = VM_GetMuzzlePos();
      else if (p) start = MuzzlePos(p);
      FX_Tracer(start, pos, { 255, 220, 150, 200 }, 1.2f, std::max(0.05f, (pos - start).Length() / 6000.f + 0.05f));
    }
  } else if (e.IsEvent("hegrenade_detonate")) {
    Vector pos(e.GetFloat("x"), e.GetFloat("y"), e.GetFloat("z"));
    FX_Explosion(pos);
    float d = (pos - cl.viewOrigin).Length();
    if (d < 1000) { s_shakeAmount = 3.f * (1.f - d / 1000.f); s_shakeEnd = gpGlobals->realtime + 1.f; }
  } else if (e.IsEvent("bomb_exploded")) {
    Vector pos = g_pPlantedC4 ? g_pPlantedC4->m_vecOrigin : cl.viewOrigin;
    for (int i = 0; i < 4; i++) FX_Explosion(pos + Vector(RandomFloat(-80, 80), RandomFloat(-80, 80), RandomFloat(0, 120)));
    s_shakeAmount = 6.f; s_shakeEnd = gpGlobals->realtime + 2.f;
  } else if (e.IsEvent("smokegrenade_detonate")) {
    FX_SmokeCloud(Vector(e.GetFloat("x"), e.GetFloat("y"), e.GetFloat("z")), ServerTime(), 18.f);
  } else if (e.IsEvent("flashbang_detonate")) {
    Vector pos(e.GetFloat("x"), e.GetFloat("y"), e.GetFloat("z")); FX_Spark(pos, 25);
  } else if (e.IsEvent("player_hurt")) {
    CCSPlayer* v = UTIL_PlayerByIndex(e.GetInt("userid")); CCSPlayer* a = UTIL_PlayerByIndex(e.GetInt("attacker"));
    if (lp && a == lp && v != lp) {
      cl.hitmarkerTime = gpGlobals->realtime; cl.hitmarkerDmg = e.GetInt("dmg_health"); cl.hitmarkerHead = e.GetInt("hitgroup") == HITGROUP_HEAD;
      extern ConVar snd_hitsound; if (snd_hitsound.GetInt() == 1) EmitSound2D("Cheat.Hitsound"); else if (snd_hitsound.GetInt() == 2) EmitSound2D("Cheat.Hitsound2");
    }
    if (lp && v == lp) {
      cl.lastDamageTakenTime = gpGlobals->realtime;
      if (a && a != lp) cl.lastDamageDir = a->m_vecOrigin - lp->m_vecOrigin; else cl.lastDamageDir = Vector(0, 0, 0);
    }
  } else if (e.IsEvent("player_blind")) {
    if (lp && e.GetInt("userid") == lp->entindex() && e.GetFloat("blind_duration") > 1.5f) EmitSound2D("Player.FlashRing", std::min(1.f, e.GetFloat("blind_duration") / 4.f));
  } else if (e.IsEvent("round_start")) {
    FX_Clear(); cl.roundStartRealtime = gpGlobals->realtime;
    cl.buyMenuOpen = false;
  } else if (e.IsEvent("weapon_reload")) {
    CCSPlayer* p = UTIL_PlayerByIndex(e.GetInt("userid")); if (p) EmitSound("Weapon.Reload", p->m_vecOrigin, p->entindex(), 0.6f);
  } else if (e.IsEvent("player_spawn")) {
    if (lp && e.GetInt("userid") == lp->entindex()) { cl.viewangles = lp->v_angle; VM_Reset(); cl.teamMenuOpen = false; }
  } else if (e.IsEvent("player_death")) {
    CCSPlayer* v = UTIL_PlayerByIndex(e.GetInt("userid"));
    if (v) EmitSound("Player.Death", v->m_vecOrigin, v->entindex(), 0.8f);
    if (lp && v == lp) { cl.buyMenuOpen = false; }
  }
}

void CL_LevelInit() {
  g_WorldRender.Unload();
  if (g_MaterialSystem.initialized) g_WorldRender.Build(g_MapInfo);
  FX_Clear(); VM_Reset();
  HUD_LevelInit();
  cheat::LevelInit();
  cl.viewangles = QAngle(0, 0, 0);
  if (!s_eventListener) s_eventListener = gameeventmanager->AddListener(nullptr, OnGameEvent);
  cl.teamMenuOpen = true;   // MOTD -> team select like CS
  cl.buyMenuOpen = false; cl.showScores = false;
}
void CL_LevelShutdown() { FX_Clear(); S_StopAll(); }
void CL_OnTickEnd() {}

// ------------------------------------------------------------------------------------------------
// frame
// radar spotting: an enemy shows up on the radar while any living teammate can see him
static void CL_UpdateSpotting() {
  static double s_next = 0;
  if (gpGlobals->realtime < s_next) return;
  s_next = gpGlobals->realtime + 0.1;
  CCSPlayer* lp = g_pLocalPlayer; if (!lp) return;
  int myTeam = lp->m_iTeamNum;
  std::vector<CCSPlayer*> alive = UTIL_GetPlayers(true);
  for (CCSPlayer* e : alive) {
    if (myTeam < TEAM_TERRORIST || e->m_iTeamNum == myTeam) { e->m_flSpottedTime = (float)gpGlobals->realtime; continue; } // teammates always / spectators see all
    Vector targets[2] = { e->EyePosition(), e->m_vecOrigin + Vector(0, 0, 36) };
    for (CCSPlayer* o : alive) {
      if (o->m_iTeamNum != myTeam) continue;
      Vector eye = o->EyePosition(), fwd; AngleVectors(o->v_angle, &fwd);
      bool seen = false;
      for (const Vector& t : targets) {
        Vector d = t - eye; float len = d.Length(); if (len < 1.f) { seen = true; break; }
        if (d.Dot(fwd) / len < 0.5f) continue;                       // outside a 120 degree view cone
        trace_t tr; UTIL_TraceLine(eye, t, MASK_VISIBLE, o, &tr);
        if (tr.fraction >= 0.99f && !LineGoesThroughSmoke(eye, t)) { seen = true; break; }
      }
      if (seen) { e->m_flSpottedTime = (float)gpGlobals->realtime + 0.2f; break; }
    }
  }
}

void CL_Frame(float dt) {
  CL_UpdateSpotting();
  FX_Update(dt);
  VM_Update(dt);
  cl.removeScope = cheat::RemoveScope(); cl.removeVisualRecoil = cheat::RemoveVisualRecoil(); cl.removeFlash = cheat::RemoveFlash(); cl.removeSmoke = cheat::RemoveSmoke();
  cl.worldBrightness = 1.f - 0.75f * cheat::NightmodeAmount();
  cl.viewmodelFov = cheat::ViewmodelFov();
}

void CL_RenderFrame() {
  CViewSetup& view = cl.view;
  CL_ComputeView(view);
  g_CurrentView = view;
  S_Update(view.origin, view.angles);
  cheat::FrameStageNotify();
  ClearBackground(BLACK);
  Render_Begin3D(view.view, view.proj);
  g_WorldRender.DrawSky(view, gpGlobals->realtime);
  g_WorldRender.DrawWorld(view);
  FX_DrawOpaque(view);
  Render_End3D();
  // nightmode: darken the world before models are drawn (models get darker light instead)
  if (cl.worldBrightness < 0.999f) {
    unsigned char g = (unsigned char)(255 * cl.worldBrightness);
    BeginBlendMode(BLEND_MULTIPLIED); DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), { g, g, (unsigned char)std::min(255, g + 20), 255 }); EndBlendMode();
  }
  Render_Begin3D(view.view, view.proj);
  CL_DrawEntities(view);
  cheat::DrawWorld3D(view);
  FX_DrawTranslucent(view);
  Render_End3D();
  // view model
  if (cl.firstPerson && cl.viewEntity && cl.viewEntity->IsAlive()) {
    C_WeaponCSBase* w = cl.viewEntity->GetActiveCSWeapon();
    bool scoped = w && w->m_zoomLevel > 0 && IsSniper(w->GetCSWeaponID());
    if (!scoped || cl.removeScope) CL_DrawViewModel(view);
  }
  if (cl.nightvision) { BeginBlendMode(BLEND_ADDITIVE); DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), { 0, 60, 0, 255 }); EndBlendMode(); }
  // 2D
  HUD_Paint();
  cheat::Paint();
}

void CL_RenderMenuBackground(float time) {
  // slow fly-by over the background map (like the CS:GO / Source animated menu backgrounds)
  CViewSetup v; v.width = GetScreenWidth(); v.height = GetScreenHeight(); v.fov = 75;
  float a = time * 0.035f;
  Vector c = (g_MapInfo.mins + g_MapInfo.maxs) * 0.5f;
  float r = std::max(600.f, (g_MapInfo.maxs - g_MapInfo.mins).Length2D() * 0.32f);
  v.origin = c + Vector(std::cos(a) * r, std::sin(a) * r, 0); v.origin.z = g_MapInfo.maxs.z + 380.f;
  Vector look = c + Vector(std::cos(a + 1.3f) * r * 0.3f, std::sin(a + 1.3f) * r * 0.3f, 0); look.z = g_MapInfo.mins.z + 60.f;
  v.angles = VectorAngles(look - v.origin);
  v.Compute(); g_CurrentView = v;
  ClearBackground(BLACK);
  Render_Begin3D(v.view, v.proj);
  g_WorldRender.DrawSky(v, time);
  if (g_WorldRender.built) g_WorldRender.DrawWorld(v);
  Render_End3D();
}

void CL_Init() { FX_Init(); HUD_Init(); IN_Init(); }
void CL_Shutdown() { S_Shutdown(); }
