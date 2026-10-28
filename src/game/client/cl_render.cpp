// cl_render.cpp - view setup (CViewRender), entity rendering (DrawModel), client event effects
#include "cl_main.hpp"
#include "css_models.hpp"
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
ConVar cam_idealyaw("cam_idealyaw", "0", FCVAR_CHEAT, "Thirdperson camera yaw offset (orbit around the player)");
ConVar cam_idealpitch("cam_idealpitch", "0", FCVAR_CHEAT, "Thirdperson camera pitch offset");
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
    QAngle camAng = cl.viewangles; camAng.x = Clamp(camAng.x + cam_idealpitch.GetFloat(), -89.f, 89.f); camAng.y += cam_idealyaw.GetFloat();
    Vector fwd; AngleVectors(camAng, &fwd);
    float dist = cl.thirdpersonDist > 0 ? cl.thirdpersonDist : cam_idealdist.GetFloat();
    trace_t tr; g_EngineTrace.TraceHull(org, org - fwd * dist, Vector(-8, -8, -8), Vector(8, 8, 8), MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
    view.origin = tr.endpos; view.angles = camAng;
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

ConVar r_drawhitboxes("r_drawhitboxes", "0", FCVAR_CHEAT, "Draw the players' hitbox capsules");
static void DrawHitboxesDebug(const HitboxSet& hb) {
    rlDrawRenderBatchActive(); rlDisableDepthTest();
    for (int i = 0; i < 19; i++) {
      const mstudiobbox_t& b = hb[i];
      Color c = i == HITBOX_HEAD ? Color{ 255, 70, 70, 255 } : i <= HITBOX_UPPER_CHEST ? Color{ 255, 220, 60, 255 } : Color{ 80, 200, 255, 255 };
      DrawCapsuleWires({ b.a.x, b.a.y, b.a.z }, { b.b.x, b.b.y, b.b.z }, b.radius, 8, 3, c);
    }
    rlDrawRenderBatchActive(); rlEnableDepthTest();
}

void CCSPlayer::BuildRenderPose(float alpha, bool useNet) {
  PlayerPoseInput in;
  if (!IsAlive()) {
    in.origin = m_vecOrigin; in.footYaw = m_net.footYaw; in.eyeAngles = m_net.eyeAngles;
    in.deathCycle = Clamp((ServerTime() - m_flDeathTime) / 0.9f, 0.f, 1.f); in.deathYaw = m_flDeathYaw - m_net.footYaw; in.deathTime = std::max(0.f, ServerTime() - m_flDeathTime);
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
    // body yaw (desync): the visible model keeps the anti-aim yaw that was actually sent and the feet other
    // clients compute from it - the desynced server body is drawn by the cheat as the 'fake' ghost
    if (IsLocalPlayer() && m_net.valid && cheat::LocalBodyYawActive()) { in.eyeAngles = m_angLastSentAngles; in.footYaw = m_flFakeFootYaw; }
  }
  m_vecRenderOrigin = in.origin;
  BuildPlayerPose(this, in, m_RenderPose, true);
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
  if (IsAlive() && IsDormant()) return;          // not sent to us this tick (out of our PVS)
  bool useNet = IsAlive() && !IsLocalPlayer();
  BuildRenderPose(g_Host.frameAlpha, useNet);
  if (!m_bRenderPoseValid) return;
  StudioLight light = LightAt(m_vecRenderOrigin + Vector(0, 0, 40));
  DrawBlobShadow(m_vecRenderOrigin, 22.f, IsAlive() ? 1.f : 0.6f);
  const bool hb = r_drawhitboxes.GetBool() && IsAlive();
  if (IsAlive() && cheat::DrawPlayer(this, m_RenderPose, light, view)) { if (hb) DrawHitboxesDebug(m_RenderPose.hitboxes); return; }
  StudioDrawParams p; p.modelToWorld = m_RenderPose.modelToWorld; p.light = light; p.eye = view.origin;
  if (r_drawothermodels.GetInt() == 2) p.flags |= STUDIO_WIREFRAME;
  CSS_DrawPose(m_RenderPose, p, true);
  if (hb) DrawHitboxesDebug(m_RenderPose.hitboxes);
}
void CCSPlayer::DrawTranslucent(const CViewSetup&) {}

void C_WeaponCSBase::DrawModel(const CViewSetup&) {
  if (m_hOwner) return; // carried weapons are drawn by the player
  Vector o = GetRenderOrigin(g_Host.frameAlpha);
  matrix3x4_t rot; AngleMatrix(QAngle(0, m_angRotation.y, 90), rot);
  if (m_iWeaponID == WEAPON_C4 || IsGrenade(m_iWeaponID)) AngleMatrix(QAngle(0, m_angRotation.y, 0), rot);
  rot.SetOrigin(o + Vector(0, 0, IsGrenade(m_iWeaponID) || m_iWeaponID == WEAPON_C4 ? 0.5f : 1.2f));
  StudioDrawParams p; p.modelToWorld = rot; p.light = LightAt(o + Vector(0, 0, 8));
  // dropped C4 blinks
  if (m_iWeaponID == WEAPON_C4 && std::fmod(gpGlobals->realtime, 1.f) < 0.15f) { p.flags |= STUDIO_TINT; p.colorOverride = { 255, 120, 120, 255 }; }
  CSS_DrawWorldWeapon(m_iWeaponID, rot, p, false);
}

void CBaseCSGrenadeProjectile::DrawModel(const CViewSetup&) {
  if (m_bDetonated && m_iWeaponID != WEAPON_SMOKEGRENADE) return;
  Vector o = GetRenderOrigin(g_Host.frameAlpha);
  matrix3x4_t rot; AngleMatrix(m_angRotation, rot); rot.SetOrigin(o);
  StudioDrawParams p; p.modelToWorld = rot; p.light = LightAt(o);
  CSS_DrawWorldWeapon(m_iWeaponID, rot, p, true);
}
void CBaseCSGrenadeProjectile::DrawTranslucent(const CViewSetup&) {}
void CSmokeGrenadeProjectile::DrawTranslucent(const CViewSetup&) {}

// fire: camera-facing vertical tongues per flame (additive), a soft glow on the ground, rising smoke and embers
void CInferno::DrawTranslucent(const CViewSetup& v) {
  if (!g_MaterialSystem.initialized || m_Flames.empty()) return;
  const float now = ServerTime(), fade = Fade();
  if (fade <= 0.f) return;
  if (gpGlobals->realtime >= m_flNextFx && !m_bExtinguished) {
    m_flNextFx = gpGlobals->realtime + 0.05f;
    const InfernoFlame& f = m_Flames[RandomInt(0, (int)m_Flames.size() - 1)];
    FX_Ember(f.pos + Vector(RandomFloat(-12, 12), RandomFloat(-12, 12), 10));
    if (RandomInt(0, 2) == 0) FX_FireSmoke(f.pos + Vector(RandomFloat(-10, 10), RandomFloat(-10, 10), 40), 1.f + 0.4f * fade);
  }
  rlDrawRenderBatchActive(); BeginBlendMode(BLEND_ADDITIVE); rlDisableDepthMask(); rlDisableBackfaceCulling();
  // ground glow
  rlSetTexture(g_MaterialSystem.glowDot.id); rlBegin(RL_QUADS);
  for (const InfernoFlame& f : m_Flames) {
    float grow = Clamp((now - f.born) / 0.35f, 0.f, 1.f); float r = 46.f * f.size * grow;
    float flick = 0.8f + 0.2f * std::sin(now * 11.f + f.phase * 3.f);
    rlColor4ub(255, 120, 30, (uint8_t)(110 * fade * flick)); Vector c = f.pos + Vector(0, 0, 1.5f);
    rlTexCoord2f(0, 0); rlVertex3f(c.x - r, c.y - r, c.z); rlTexCoord2f(1, 0); rlVertex3f(c.x + r, c.y - r, c.z);
    rlTexCoord2f(1, 1); rlVertex3f(c.x + r, c.y + r, c.z); rlTexCoord2f(0, 1); rlVertex3f(c.x - r, c.y + r, c.z);
  }
  rlEnd();
  // tongues: vertical quads turned towards the camera around the z axis
  rlSetTexture(g_MaterialSystem.flame.id); rlBegin(RL_QUADS);
  for (const InfernoFlame& f : m_Flames) {
    float grow = Clamp((now - f.born) / 0.35f, 0.f, 1.f);
    float die = m_bExtinguished ? fade : Clamp(fade * 1.4f, 0.f, 1.f);
    for (int k = 0; k < 5; k++) {
      float ph = f.phase + k * 1.257f;
      float rr = k == 0 ? 0.f : 13.f;
      Vector base = f.pos + Vector(std::cos(ph) * rr * f.size, std::sin(ph) * rr * f.size, -1.f);
      float h = (k == 0 ? 44.f : 30.f) * f.size * grow * (0.4f + 0.6f * die) * (0.8f + 0.2f * std::sin(now * 9.f + ph * 1.7f));
      float w = h * 0.62f;
      Vector to = v.origin - base; to.z = 0; float l = to.Length(); if (l < 1.f) continue; to = to / l;
      Vector right(-to.y, to.x, 0); right = right * w;
      Vector top = base + Vector(std::sin(now * 5.f + ph) * 3.f, std::cos(now * 4.f + ph) * 3.f, h);   // the tip sways
      uint8_t a = (uint8_t)(235 * die);
      rlColor4ub(255, 255, 255, a);
      rlTexCoord2f(0, 1); rlVertex3f(base.x - right.x, base.y - right.y, base.z);
      rlTexCoord2f(1, 1); rlVertex3f(base.x + right.x, base.y + right.y, base.z);
      rlTexCoord2f(1, 0); rlVertex3f(top.x + right.x, top.y + right.y, top.z);
      rlTexCoord2f(0, 0); rlVertex3f(top.x - right.x, top.y - right.y, top.z);
    }
  }
  rlEnd();
  // hot glow in the middle of every flame
  rlSetTexture(g_MaterialSystem.glowDot.id); rlBegin(RL_QUADS);
  for (const InfernoFlame& f : m_Flames) {
    float grow = Clamp((now - f.born) / 0.35f, 0.f, 1.f), s = 22.f * f.size * grow;
    Vector c = f.pos + Vector(0, 0, 14.f * f.size);
    rlColor4ub(255, 140, 40, (uint8_t)(120 * fade * (0.85f + 0.15f * std::sin(now * 13.f + f.phase))));
    Vector r = v.right * s, u = v.up * s;
    rlTexCoord2f(0, 1); rlVertex3f(c.x - r.x - u.x, c.y - r.y - u.y, c.z - r.z - u.z);
    rlTexCoord2f(1, 1); rlVertex3f(c.x + r.x - u.x, c.y + r.y - u.y, c.z + r.z - u.z);
    rlTexCoord2f(1, 0); rlVertex3f(c.x + r.x + u.x, c.y + r.y + u.y, c.z + r.z + u.z);
    rlTexCoord2f(0, 0); rlVertex3f(c.x - r.x + u.x, c.y - r.y + u.y, c.z - r.z + u.z);
  }
  rlEnd(); rlSetTexture(0); rlDrawRenderBatchActive(); rlEnableDepthMask(); rlEnableBackfaceCulling(); EndBlendMode();
}

void CPlantedC4::DrawModel(const CViewSetup&) {
  matrix3x4_t rot; AngleMatrix(QAngle(0, m_flYaw, 0), rot); rot.SetOrigin(m_vecOrigin + Vector(0, 0, 0.5f));
  StudioDrawParams p; p.modelToWorld = rot; p.light = LightAt(m_vecOrigin + Vector(0, 0, 8));
  CSS_DrawModelFile("models/weapons/w_c4_planted.mdl", rot, p);
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
  // translucent pass (fire): after every opaque model, before the particles / smoke
  for (CInferno* f : GetActiveInfernos()) if (f && !f->m_bMarkedForDeletion) f->DrawTranslucent(view);
}

// ------------------------------------------------------------------------------------------------
// level / events
static int s_eventListener = 0;
static Vector MuzzlePos(CCSPlayer* p) {
  if (p->m_bRenderPoseValid && p->m_RenderPose.hasMuzzle) return p->m_RenderPose.muzzle;
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
    FX_SmokeCloud(Vector(e.GetFloat("x"), e.GetFloat("y"), e.GetFloat("z")), ServerTime(), e.GetFloat("duration") > 0 ? e.GetFloat("duration") : SmokeDuration());
  } else if (e.IsEvent("molotov_detonate")) {
    FX_MolotovBurst(Vector(e.GetFloat("x"), e.GetFloat("y"), e.GetFloat("z")), e.GetBool("air"));
  } else if (e.IsEvent("inferno_extinguish")) {
    Vector pos(e.GetFloat("x"), e.GetFloat("y"), e.GetFloat("z")); FX_SteamPuff(pos);
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
void CL_LevelShutdown() {
  FX_Clear(); S_StopAll();
  cl.viewEntity = nullptr;           // the entities are about to be deleted
  VM_Reset();
  cheat::LevelInit();
}
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
      if (seen) { e->m_flSpottedTime = (float)gpGlobals->realtime + 0.2f; e->m_flTeamSpottedTime = (float)gpGlobals->realtime + 0.2f; break; }
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

std::string g_VMShotPath;
std::string g_HostageShotModel, g_HostageShotPath;
#ifdef CS_HEADLESS
extern "C" void swClear(uint32_t bitmask);
#endif
static ConCommand cmd_vm_shot("vm_shot", [](const CCommand& a) {
  if (a.ArgC() >= 1) { g_VMShotPath = a.Arg(1); Msg("[vm_shot] %s\n", g_VMShotPath.c_str()); }
  }, "Debug: render only the view model on a flat background into a png.");
static void SaveImageFile(const std::string& path) {
  Image img = LoadImageFromScreen();
  ImageFlipVertical(&img);
  ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
  ExportImage(img, path.c_str());
  UnloadImage(img);
}
// Debug aid: the view model alone on a flat background, from a fixed camera, so the gun can be measured from the png
// (no way to look at pictures here - the numbers have to come out of the file).
static void CL_RenderViewModelOnly(const CViewSetup& view, const std::string& path) {
  Msg("[vm_shot] render start %s\n", path.c_str());
  ClearBackground(Color{ 16, 84, 196, 255 });
#ifdef CS_HEADLESS
  swClear(0x00004000);
#endif
  Matrix proj = BuildProjection(view.VerticalFov(75.f), (float)view.width / std::max(1, view.height), 1.f, 600.f);
  Render_Begin3D(view.view, proj);
  CL_DrawViewModel(view);
  Render_End3D();
  SaveImageFile(path);
  Msg("[vm_shot] render done\n");
}

// Debug: render a model alone on a flat background with a fixed camera,
// so we can tell whether the model itself draws or the world-draw is dropping it.
static void CL_RenderModelOnly(const std::string& path, const std::string& outPath) {
  Msg("[hostage_shot] render start %s -> %s\n", path.c_str(), outPath.c_str());
  ClearBackground(Color{ 16, 84, 196, 255 });
#ifdef CS_HEADLESS
  swClear(0x00004000);
  Render_ClearDepth();
#endif
  CViewSetup v; v.width = GetScreenWidth(); v.height = GetScreenHeight(); v.fov = 75;
  v.origin = Vector(0, 0, 0); v.angles = QAngle(0, 0, 0); v.Compute();
  Matrix proj = BuildProjection(v.VerticalFov(75.f), (float)v.width / std::max(1, v.height), 1.f, 600.f);
  Render_Begin3D(v.view, proj);
  matrix3x4_t rot; AngleMatrix(QAngle(0, 0, 0), rot); rot.SetOrigin(Vector(200, 0, 0));
  StudioDrawParams p; p.modelToWorld = rot; p.flags |= STUDIO_FLAT_COLOR; p.colorOverride = { 255, 0, 255, 255 };
  bool ok = CSS_DrawModelFile(path.c_str(), rot, p);
  Msg("[hostage_shot] css draw ok=%d\n", ok);
  // reference triangle 100 units ahead (camera looks +x at angles 0,0,0)
  rlSetTexture(rlGetTextureIdDefault());
  rlBegin(RL_TRIANGLES);
  rlColor4ub(255, 0, 255, 255); rlVertex3f(100, 0, 0); rlVertex3f(100, -30, 0); rlVertex3f(100, 0, 30);
  rlEnd();
  Render_End3D();
  SaveImageFile(outPath);
  Msg("[hostage_shot] render done\n");
}
static ConCommand cmd_hostage_shot("hostage_shot", [](const CCommand& a) {
  if (a.ArgC() >= 2) { g_HostageShotModel = a.Arg(1); g_HostageShotPath = a.Arg(2); }
  else if (a.ArgC() >= 1) { g_HostageShotModel = "models/characters/hostage_01.mdl"; g_HostageShotPath = a.Arg(1); }
  }, "Debug: render a model on a flat background. usage: hostage_shot <model.mdl> <out.png>");

void CL_RenderFrame() {
  CViewSetup& view = cl.view;
  CL_ComputeView(view);
  g_CurrentView = view;
  if (!g_VMShotPath.empty()) { Msg("[vm_shot] path taken\n"); CL_RenderViewModelOnly(view, g_VMShotPath); g_VMShotPath.clear(); return; }
  if (!g_HostageShotPath.empty()) { CL_RenderModelOnly(g_HostageShotModel, g_HostageShotPath); g_HostageShotPath.clear(); return; }
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
  if (!cl.noHud) HUD_Paint();
  cheat::Paint();
}

// ------------------------------------------------------------------------------------------------
// main menu scene (CS:GO style): your agent standing on the background map, holding a rifle
ConVar cl_menu_team("cl_menu_team", "3", FCVAR_ARCHIVE, "Agent shown in the main menu: 2 = Terrorist, 3 = Counter-Terrorist", true, 2, true, 3);
static struct MenuScene { std::string map; bool valid = false; Vector agent; float yaw = 0; Vector cam; QAngle camAng; } s_menuScene;

static void SetupMenuScene() {
  MenuScene& sc = s_menuScene; sc = MenuScene(); sc.map = g_MapInfo.name;
  const int team = cl_menu_team.GetInt();
  std::vector<const MapSpawn*> cand;
  for (auto& sp : g_MapInfo.spawns) if (sp.team == team) cand.push_back(&sp);
  if (cand.empty()) for (auto& sp : g_MapInfo.spawns) cand.push_back(&sp);
  if (cand.empty()) return;
  // pick the spawn / camera direction with the most open view (camera space in front, deep background behind)
  float bestScore = -1;
  for (size_t ci = 0; ci < cand.size() && ci < 6; ci++) {
    Vector o = cand[ci]->origin;
    for (int k = 0; k < 16; k++) {
      float yaw = cand[ci]->yaw + 180.f + k * 22.5f;
      Vector dir(std::cos(DEG2RADF(yaw)), std::sin(DEG2RADF(yaw)), 0);
      trace_t f, bk;
      UTIL_TraceLine(o + Vector(0, 0, 48), o + Vector(0, 0, 48) + dir * 170.f, MASK_SOLID, nullptr, &f);
      UTIL_TraceLine(o + Vector(0, 0, 48), o + Vector(0, 0, 48) - dir * 1600.f, MASK_SOLID, nullptr, &bk);
      float score = std::min(f.fraction * 170.f, 150.f) * 4.f + bk.fraction * 1600.f * 0.25f - (k == 0 ? 0.f : 6.f);
      if (score > bestScore) { bestScore = score; sc.agent = o; sc.yaw = yaw; }
    }
  }
  Vector dir(std::cos(DEG2RADF(sc.yaw)), std::sin(DEG2RADF(sc.yaw)), 0), right(dir.y, -dir.x, 0);
  trace_t f; UTIL_TraceLine(sc.agent + Vector(0, 0, 48), sc.agent + Vector(0, 0, 48) + dir * 170.f, MASK_SOLID, nullptr, &f);
  float dist = std::max(70.f, f.fraction * 170.f - 12.f);
  sc.cam = sc.agent + dir * dist + Vector(0, 0, 50);
  // look slightly to the agent's left so it stands right of the menu panels
  Vector look = sc.agent + Vector(0, 0, 38) + right * (dist * 0.30f);
  sc.camAng = VectorAngles(look - sc.cam);
  sc.valid = true;
}

void CL_RenderMenuBackground(float time) {
  CViewSetup v; v.width = GetScreenWidth(); v.height = GetScreenHeight(); v.fov = 62;
  if (s_menuScene.map != g_MapInfo.name || (s_menuScene.valid && !g_MapInfo.spawns.empty() && cl_menu_team.GetInt() != (int)s_menuScene.camAng.z)) { SetupMenuScene(); s_menuScene.camAng.z = (float)cl_menu_team.GetInt(); }
  const MenuScene& sc = s_menuScene;
  if (sc.valid) {
    // subtle parallax with the mouse
    Vector2 m = GetMousePosition(); float mx = m.x / std::max(1, v.width) - 0.5f, my = m.y / std::max(1, v.height) - 0.5f;
    v.origin = sc.cam + Vector(0, 0, std::sin(time * 0.4f) * 0.6f);
    v.angles = QAngle(sc.camAng.x + my * 1.5f, sc.camAng.y - mx * 2.f, 0);
  } else {
    float a = time * 0.035f;
    Vector c = (g_MapInfo.mins + g_MapInfo.maxs) * 0.5f;
    float r = std::max(600.f, (g_MapInfo.maxs - g_MapInfo.mins).Length2D() * 0.32f);
    v.origin = c + Vector(std::cos(a) * r, std::sin(a) * r, 0); v.origin.z = g_MapInfo.maxs.z + 380.f;
    Vector look = c + Vector(std::cos(a + 1.3f) * r * 0.3f, std::sin(a + 1.3f) * r * 0.3f, 0); look.z = g_MapInfo.mins.z + 60.f;
    v.angles = VectorAngles(look - v.origin); v.fov = 75;
  }
  v.Compute(); g_CurrentView = v;
  ClearBackground(BLACK);
  Render_Begin3D(v.view, v.proj);
  g_WorldRender.DrawSky(v, time);
  if (g_WorldRender.built) g_WorldRender.DrawWorld(v);
  if (sc.valid) {
    const int team = cl_menu_team.GetInt();
    PlayerPoseInput in; in.origin = sc.agent; in.footYaw = sc.yaw; in.animTime = time;
    Vector2 m = GetMousePosition();
    float mx = m.x / std::max(1, v.width) - 0.62f, my = m.y / std::max(1, v.height) - 0.45f;
    in.eyeAngles = QAngle(Clamp(my * 30.f, -12.f, 15.f), sc.yaw - Clamp(mx * 50.f, -25.f, 25.f), 0);
    in.weaponId = team == TEAM_CT ? WEAPON_M4A1 : WEAPON_AK47;
    StudioDrawParams p; p.light = LightAt(sc.agent + Vector(0, 0, 40)); p.eye = v.origin;
    DrawBlobShadow(sc.agent, 20.f, 1.f);
    CSS_DrawAgent(team, (team == TEAM_CT ? FindConVar("cl_agent_ct") : FindConVar("cl_agent_t"))->GetInt(), in, p);
  }
  Render_End3D();
}

void CL_Init() { FX_Init(); HUD_Init(); IN_Init(); }
void CL_Shutdown() { S_Shutdown(); }
