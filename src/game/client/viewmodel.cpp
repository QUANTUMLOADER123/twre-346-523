// viewmodel.cpp - C_BaseViewModel: first person arms + weapon (posed from the character model, arms-only triangles)
#include "cl_main.hpp"
#include "../../engine/host.hpp"
#include "../../engine/worldrender.hpp"
#include "../../engine/studio.hpp"
#include "../shared/cs_player.hpp"
#include "../shared/weapon_csbase.hpp"
#include "rlgl.h"
#include <cmath>
#include <algorithm>
#include "../../engine/mdl.hpp"
#include "../../engine/hlfs.hpp"
#include <cstring>
#include "css_models.hpp"

ConVar viewmodel_fov("viewmodel_fov", "68", FCVAR_ARCHIVE, "Viewmodel field of view", true, 54, true, 90);
ConVar viewmodel_offset_x("viewmodel_offset_x", "0", FCVAR_ARCHIVE, "", true, -4, true, 4);
ConVar viewmodel_offset_y("viewmodel_offset_y", "0", FCVAR_ARCHIVE, "", true, -4, true, 4);
ConVar viewmodel_offset_z("viewmodel_offset_z", "0", FCVAR_ARCHIVE, "", true, -4, true, 4);
ConVar cl_bob("cl_bob", "0.01", FCVAR_ARCHIVE, "");
ConVar cl_bobcycle("cl_bobcycle", "0.8", FCVAR_ARCHIVE, "");
ConVar cl_bobup("cl_bobup", "0.5", FCVAR_ARCHIVE, "");
ConVar cl_drawviewmodel("r_drawviewmodel", "1", FCVAR_CHEAT, "Render view model");
ConVar cl_original_viewmodels("cl_original_viewmodels", "1", FCVAR_ARCHIVE, "Use the CS 1.6 view models (models/v_*.mdl) from your install when available");
ConVar cl_viewmodel_shift("cl_viewmodel_shift_left_amt", "1.5", FCVAR_ARCHIVE, "");

static struct {
  float bobTime = 0, bob = 0, bobSide = 0;
  QAngle lastAngles; QAngle sway;
  float kick = 0;
  int lastWeaponId = -1; C_WeaponCSBase* lastWeapon = nullptr;
  float deployStart = -100, deployLen = 0.5f;
  Vector muzzle;
  float landKick = 0; bool wasOnGround = true;
} vm;

void VM_Reset() { vm.lastWeapon = nullptr; vm.lastWeaponId = -1; vm.kick = 0; vm.sway = QAngle(); vm.deployStart = -100; }
void VM_OnFire() { vm.kick = 1.f; }
Vector VM_GetMuzzlePos() { return vm.muzzle; }

void VM_Update(float dt) {
  CCSPlayer* p = cl.viewEntity; if (!p || !p->IsAlive()) return;
  float speed = p->m_vecVelocity.Length2D();
  bool onGround = (p->m_fFlags & FL_ONGROUND) != 0;
  if (onGround) vm.bobTime += dt * (0.6f + 0.4f * std::min(1.f, speed / 250.f));
  float target = onGround ? std::min(1.f, speed / 250.f) : 0.f;
  vm.bob = Approach(target, vm.bob, dt * 4.f);
  vm.kick = std::max(0.f, vm.kick - dt * 9.f);
  if (onGround && !vm.wasOnGround) vm.landKick = 1.f;
  vm.wasOnGround = onGround;
  vm.landKick = std::max(0.f, vm.landKick - dt * 5.f);
  // sway: lag behind view rotation
  QAngle cur = cl.view.angles;
  QAngle d(AngleDiff(cur.x, vm.lastAngles.x), AngleDiff(cur.y, vm.lastAngles.y), 0);
  vm.lastAngles = cur;
  vm.sway.x = Clamp(vm.sway.x + d.x * 0.08f, -4.f, 4.f); vm.sway.y = Clamp(vm.sway.y + d.y * 0.08f, -4.f, 4.f);
  float k = std::exp(-8.f * dt); vm.sway.x *= k; vm.sway.y *= k;
  C_WeaponCSBase* w = p->GetActiveCSWeapon();
  if (w != vm.lastWeapon) {
    vm.lastWeapon = w; vm.deployStart = gpGlobals->realtime;
    vm.deployLen = w ? std::max(0.25f, std::min(1.f, w->GetCSWpnData().deployTime)) : 0.5f;
  }
}

// ------------------------------------------------------------------------------------------------
// original CS 1.6 view models
static const char* Cs16WeaponModel(int wid) {
  switch (wid) {
    case WEAPON_KNIFE: return "knife"; case WEAPON_GLOCK: return "glock18"; case WEAPON_USP_SILENCER: return "usp"; case WEAPON_P250: return "p228";
    case WEAPON_DEAGLE: case WEAPON_REVOLVER: return "deagle"; case WEAPON_MAC10: return "mac10"; case WEAPON_MP9: return "tmp"; case WEAPON_UMP45: return "ump45";
    case WEAPON_P90: return "p90"; case WEAPON_NOVA: return "m3"; case WEAPON_XM1014: return "xm1014"; case WEAPON_GALILAR: return "galil"; case WEAPON_FAMAS: return "famas";
    case WEAPON_AK47: return "ak47"; case WEAPON_M4A1: case WEAPON_M4A1_SILENCER: return "m4a1"; case WEAPON_SG556: return "sg552"; case WEAPON_AUG: return "aug";
    case WEAPON_SSG08: return "scout"; case WEAPON_AWP: return "awp"; case WEAPON_G3SG1: return "g3sg1"; case WEAPON_SCAR20: return "sg550";
    case WEAPON_HEGRENADE: return "hegrenade"; case WEAPON_FLASHBANG: return "flashbang"; case WEAPON_SMOKEGRENADE: return "smokegrenade"; case WEAPON_C4: return "c4";
    default: return nullptr;
  }
}

// sequence for a view activity: first name starting with one of the prefixes; `variant` picks among matches
static int PickSequence(const CGoldSrcModel& m, std::initializer_list<const char*> prefixes, bool wantUnsil, bool silModel, int variant, bool allowEmpty) {
  for (const char* pre : prefixes) {
    std::vector<int> hits; size_t pl = std::strlen(pre);
    for (int i = 0; i < (int)m.seqs.size(); i++) {
      const std::string& n = m.seqs[i].name;
      if (n.compare(0, pl, pre) != 0) continue;
      if (!allowEmpty && n.find("empty") != std::string::npos) continue;
      if (n.find("miss") != std::string::npos) continue;
      if (silModel && (n.find("unsil") != std::string::npos) != wantUnsil) continue;
      hits.push_back(i);
    }
    if (!hits.empty()) return hits[(size_t)std::max(0, variant) % hits.size()];
  }
  return -1;
}

static bool DrawOriginalViewModel(const CViewSetup& view, CCSPlayer* p, C_WeaponCSBase* w) {
  if (!cl_original_viewmodels.GetBool() || !hlfs::Available()) return false;
  int wid = w->GetCSWeaponID();
  const char* n = Cs16WeaponModel(wid); if (!n) return false;
  CGoldSrcModel* m = GoldSrc_GetModel(std::string("models/v_") + n + ".mdl");
  if (!m || m->seqs.empty()) return false;
  bool silModel = wid == WEAPON_USP_SILENCER || wid == WEAPON_M4A1 || wid == WEAPON_M4A1_SILENCER;
  bool unsil = wid == WEAPON_M4A1 || !w->m_bSilencerOn;
  int act = w->m_iViewActivity;
  float elapsed = std::max(0.f, gpGlobals->curtime - w->m_flViewActivityStart);
  int variant = w->m_iViewShotSide;
  bool empty = w->IsGun() && w->m_iClip1 == 0;
  int seq = -1; bool stretch = false;
  switch (act) {
    case ACT_VM_DRAW: seq = PickSequence(*m, { "draw", "deploy" }, unsil, silModel, 0, true); stretch = true; break;
    case ACT_VM_PRIMARYATTACK:
      if (wid == WEAPON_KNIFE) seq = PickSequence(*m, { "midslash", "slash", "attack1" }, unsil, silModel, variant, true);
      else seq = empty ? PickSequence(*m, { "shoot_empty", "shoot" }, unsil, silModel, 0, true) : PickSequence(*m, { "shoot", "fire" }, unsil, silModel, variant, false);
      break;
    case ACT_VM_SECONDARYATTACK: seq = PickSequence(*m, { "stab", "attack2" }, unsil, silModel, 0, true); break;
    case ACT_VM_RELOAD: seq = PickSequence(*m, { "reload", "start_reload", "insert" }, unsil, silModel, 0, true); stretch = true; break;
    case ACT_VM_PULLPIN: seq = PickSequence(*m, { "pullpin" }, unsil, silModel, 0, true); break;
    case ACT_VM_THROW: seq = PickSequence(*m, { "throw" }, unsil, silModel, 0, true); break;
    case ACT_VM_ATTACH_SILENCER: seq = PickSequence(*m, { "add_silencer", "attach_silencer" }, false, false, 0, true); stretch = true; break;
    case ACT_VM_DETACH_SILENCER: seq = PickSequence(*m, { "detach_silencer" }, false, false, 0, true); stretch = true; break;
    case ACT_VM_PLANT: seq = PickSequence(*m, { "pressbutton", "arm" }, unsil, silModel, 0, true); break;
    default: break;
  }
  if (IsGrenade(wid)) { auto* g = static_cast<CBaseCSGrenade*>(w); if (g->m_bPinPulled && act != ACT_VM_THROW) { seq = PickSequence(*m, { "pullpin" }, unsil, silModel, 0, true); elapsed = 99.f; } }
  float frame = 0;
  if (seq >= 0) {
    const auto& sq = m->seqs[seq];
    float dur = m->SequenceDuration(seq);
    float rate = stretch && w->m_flViewActivityDuration > 0.05f ? dur / w->m_flViewActivityDuration : 1.f;
    float t = elapsed * rate;
    if (t >= dur && !(sq.flags & 1) && act != ACT_VM_PULLPIN && !IsGrenade(wid)) { elapsed = (t - dur) / std::max(0.01f, rate); seq = -1; }   // finished: fall back to idle
    else frame = std::min(t * sq.fps, (float)(sq.numFrames - 1));
  }
  if (seq < 0) {
    seq = PickSequence(*m, { "idle" }, unsil, silModel, 0, true); if (seq < 0) seq = 0;
    const auto& sq = m->seqs[seq];
    frame = sq.numFrames > 1 ? std::fmod(elapsed * sq.fps, (float)(sq.numFrames - 1)) : 0.f;
  }
  thread_local std::vector<matrix3x4_t> bones;
  m->SetupBones(seq, frame, bones);

  // GoldSrc places the view model at the eye with the view angles; bob and sway on top
  float bt = vm.bobTime * 2.f * (float)M_PI / std::max(0.1f, cl_bobcycle.GetFloat());
  QAngle ang = view.angles; ang.x += vm.sway.x * 0.3f; ang.y += vm.sway.y * 0.3f;
  Vector org = view.origin + view.forward * (std::sin(bt) * 0.4f * vm.bob) - view.up * (vm.landKick * 1.f + std::fabs(std::sin(bt * 0.5f)) * 0.4f * vm.bob);
  org += view.right * viewmodel_offset_x.GetFloat() + view.forward * viewmodel_offset_y.GetFloat() + view.up * viewmodel_offset_z.GetFloat();
  matrix3x4_t xf; AngleMatrix(ang, org, xf);

  StudioLight L;
  if (g_WorldRender.built) { LightSample s = g_WorldRender.SampleLight(view.origin); L.ambient = s.ambient * 1.15f; L.direct = s.direct * 0.8f; L.dir = s.dir; }
  L.ambient = L.ambient * (0.35f + 0.65f * cl.worldBrightness); L.direct = L.direct * cl.worldBrightness;
  if (cl.nightvision) L.ambient = Vector(0.9f, 1.2f, 0.9f);

  // CS 1.6 draws view models with fov 90 (4:3), independent of the zoom
  CViewSetup vv = view;
  Matrix proj = BuildProjection(vv.VerticalFov(90.f), (float)view.width / std::max(1, view.height), 1.f, 600.f);
  Render_ClearDepth();
  Render_Begin3D(view.view, proj);
  m->Draw(bones, xf, L);
  Render_End3D();
  vm.muzzle = m->attachments.empty() ? view.origin + view.forward * 20.f - view.up * 4.f : m->AttachmentPos(0, bones, xf);
  return true;
}

void CL_DrawViewModel(const CViewSetup& view) {
  if (!cl_drawviewmodel.GetBool()) return;
  CCSPlayer* p = cl.viewEntity; if (!p) return;
  C_WeaponCSBase* w = p->GetActiveCSWeapon(); if (!w) return;
  if (w->GetCSWeaponID() != WEAPON_C4 || !p->m_bIsPlanting) { if (IsGrenade(w->GetCSWeaponID()) && static_cast<CBaseCSGrenade*>(w)->m_bRedraw) return; }
  if (CSS_DrawViewModel(view, p, w, vm.bobTime, vm.bob, vm.sway, vm.landKick, vm.muzzle)) return;
  if (DrawOriginalViewModel(view, p, w)) return;
  CStudioModel* mdl = p->GetModel(); if (!mdl) return;
  int wid = w->GetCSWeaponID();
  CStudioModel* wm = GetWeaponWorldModelById(wid);
  // hide grenade after throw / C4 after plant
  if (IsGrenade(wid)) { auto* g = static_cast<CBaseCSGrenade*>(w); if (g->m_bRedraw) return; }
  float now = ServerTime();
  float actCycle = w->GetViewActivityCycle();
  int act = w->m_iViewActivity;

  PlayerPoseInput in;
  in.origin = Vector(0, 0, 0); in.footYaw = view.angles.y; in.eyeAngles = QAngle(Clamp(cl.viewangles.x, -70.f, 70.f), view.angles.y, 0);
  in.weaponId = wid; in.onGround = true; in.duckAmount = 0;
  in.fireAmount = 0;
  in.reloadCycle = (w->m_bInReload && w->m_flReloadFinishTime > w->m_flReloadStartTime) ? Clamp((now - w->m_flReloadStartTime) / (w->m_flReloadFinishTime - w->m_flReloadStartTime), 0.f, 1.f) : -1.f;
  in.planting = p->m_bIsPlanting;
  in.viewmodel = true;
  PlayerPoseOutput out;
  BuildPlayerPose(*mdl, wm, in, out, false);

  // local offsets (camera space: x fwd, y left, z up) and rotations
  float hand = 1.f;
  Vector off(1.5f + viewmodel_offset_y.GetFloat(), -1.0f * hand - viewmodel_offset_x.GetFloat(), 1.2f + viewmodel_offset_z.GetFloat());
  QAngle rot(0, 0, 0);
  // bob (cl_bob style figure 8)
  float bt = vm.bobTime * 2.f * (float)M_PI / std::max(0.1f, cl_bobcycle.GetFloat());
  off.y += std::sin(bt * 0.5f) * 0.6f * vm.bob;
  off.z += -std::fabs(std::sin(bt * 0.5f)) * 0.5f * vm.bob - vm.bob * 0.3f;
  rot.z += std::sin(bt * 0.5f) * 0.8f * vm.bob;
  // sway
  rot.x += vm.sway.x * 0.6f; rot.y += vm.sway.y * 0.6f;
  off.y += vm.sway.y * 0.12f; off.z -= vm.sway.x * 0.08f;
  // landing
  off.z -= vm.landKick * 1.2f;
  // fire kick
  if (w->IsGun()) { float k = vm.kick * vm.kick; off.x -= k * (IsSniper(wid) || wid == WEAPON_DEAGLE || wid == WEAPON_NOVA || wid == WEAPON_XM1014 ? 3.f : 1.4f); rot.x -= k * 2.5f; }
  // deploy: comes up from below
  float dc = Clamp((gpGlobals->realtime - vm.deployStart) / vm.deployLen, 0.f, 1.f);
  float drop = (1.f - dc) * (1.f - dc);
  off.z -= drop * 12.f; rot.x += drop * 35.f;
  // reload: dip + roll
  if (in.reloadCycle >= 0) { float r = std::sin(in.reloadCycle * (float)M_PI); off.z -= r * 4.f; rot.z += r * 22.f; rot.x += r * 10.f; off.x -= r * 1.f; }
  // silencer attach
  if (act == ACT_VM_ATTACH_SILENCER || act == ACT_VM_DETACH_SILENCER) { float r = std::sin(actCycle * (float)M_PI); rot.y += r * 25.f; off.z -= r * 2.f; }
  // knife swings
  if (wid == WEAPON_KNIFE && (act == ACT_VM_PRIMARYATTACK || act == ACT_VM_SECONDARYATTACK)) {
    float c = actCycle; float s = std::sin(std::min(1.f, c * 1.6f) * (float)M_PI);
    if (act == ACT_VM_PRIMARYATTACK) { float side = (w->m_iViewShotSide & 1) ? 1.f : -1.f; rot.y += side * s * 40.f; rot.z += side * s * 30.f; off.x += s * 6.f; off.y += side * s * 3.f; }
    else { rot.x += s * 35.f; off.x += s * 9.f; off.z += s * 2.f; }
  }
  // grenades
  if (IsGrenade(wid)) {
    auto* g = static_cast<CBaseCSGrenade*>(w);
    if (g->m_bPinPulled) { off.z += 1.5f; off.x -= 2.f; rot.x -= 12.f; }
    if (act == ACT_VM_THROW) { float s = std::sin(std::min(1.f, actCycle * 1.4f) * (float)M_PI); off.x += s * 10.f; off.z += s * 4.f; rot.x += s * 40.f; }
  }
  if (wid == WEAPON_C4 && p->m_bIsPlanting) { off.z -= 6.f; rot.x += 30.f; off.x += 4.f; }

  // assemble: world = Cam * Local * Cam^-1 * shift * pose
  matrix3x4_t cam; AngleMatrix(view.angles, view.origin, cam);
  matrix3x4_t local; AngleMatrix(rot, off, local);
  matrix3x4_t camInv = RigidInvert(cam);
  Vector shift = view.origin - out.eyePos;
  matrix3x4_t sh; sh.SetOrigin(shift);
  matrix3x4_t xf = MatrixMul(cam, MatrixMul(local, MatrixMul(camInv, sh)));
  matrix3x4_t armsToWorld = MatrixMul(xf, out.modelToWorld);
  matrix3x4_t wpnToWorld = MatrixMul(xf, out.weaponToWorld);

  StudioLight L;
  if (g_WorldRender.built) { LightSample s = g_WorldRender.SampleLight(view.origin); L.ambient = s.ambient * 1.1f; L.direct = s.direct; L.dir = s.dir; }
  L.ambient = L.ambient * (0.35f + 0.65f * cl.worldBrightness); L.direct = L.direct * cl.worldBrightness;
  if (cl.nightvision) L.ambient = Vector(0.9f, 1.2f, 0.9f);

  // own projection (viewmodel fov), fresh depth
  float fov = cl.viewmodelFov > 0 ? cl.viewmodelFov : viewmodel_fov.GetFloat();
  CViewSetup vv = view; vv.zNear = 1.f; vv.zFar = 600.f;
  Matrix proj = BuildProjection(vv.VerticalFov(fov), (float)view.width / std::max(1, view.height), 1.f, 600.f);
  Render_ClearDepth();
  Render_Begin3D(view.view, proj);
  StudioDrawParams ap; ap.pose = &out.pose; ap.modelToWorld = armsToWorld; ap.light = L; ap.flags = STUDIO_ARMS_ONLY; ap.eye = view.origin;
  StudioRender_DrawModel(*mdl, ap);
  if (wm && out.hasWeapon) {
    StudioDrawParams wp; wp.modelToWorld = wpnToWorld; wp.light = L; wp.eye = view.origin;
    StudioRender_DrawModel(*wm, wp);
    vm.muzzle = VectorTransform(Vector(wm->maxs.x, 0, (wm->mins.z + wm->maxs.z) * 0.6f), wpnToWorld);
  } else vm.muzzle = view.origin + view.forward * 20.f - view.up * 4.f;
  Render_End3D();
}
