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
}
