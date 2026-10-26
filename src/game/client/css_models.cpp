// css_models.cpp - Counter-Strike: Source player / weapon / view models (see css_models.hpp)
#include "css_models.hpp"
#include "cl_main.hpp"
#include "../../engine/srcmdl.hpp"
#include "../../engine/srcfs.hpp"
#include "../../engine/view.hpp"
#include "../../engine/worldrender.hpp"
#include "../../engine/host.hpp"
#include "../shared/cs_player.hpp"
#include "../shared/weapon_csbase.hpp"
#include <cmath>
#include <algorithm>
#include <unordered_map>
#include <string>

ConVar cl_css_models("cl_css_models", "1", FCVAR_ARCHIVE, "Use the Counter-Strike: Source player / weapon models when CS:S content is installed");
ConVar cl_css_viewmodels("cl_css_viewmodels", "1", FCVAR_ARCHIVE, "Use the Counter-Strike: Source view models (with hands) when CS:S content is installed");
ConVar cl_css_anim_layers("cl_css_anim_layers", "255", FCVAR_CHEAT, "debug: 1 jump 2 upper 4 reload 8 aim 16 shoot");
ConVar cl_r8_model("cl_r8_model", "1", FCVAR_ARCHIVE, "Use the CS:GO R8 Revolver model (downloaded with the game content) instead of the Desert Eagle");
ConVar cl_csgo_models("cl_csgo_models", "1", FCVAR_ARCHIVE, "Use the downloaded CS:GO models (SCAR-20, SSG 08, molotov, incendiary) instead of the CS:S stand-ins");
ConVar cl_csgo_viewmodel_fov("cl_csgo_viewmodel_fov", "56", FCVAR_ARCHIVE, "Field of view of the CS:GO SCAR-20 / SSG 08 / molotov view models", true, 40, true, 90);
ConVar cl_r8_viewmodel_fov("cl_r8_viewmodel_fov", "68", FCVAR_ARCHIVE, "Field of view of the CS:GO R8 view model (CS:GO viewmodel_fov)", true, 40, true, 90);
ConVar cl_css_viewmodel_fov("cl_css_viewmodel_fov", "54", FCVAR_ARCHIVE, "Field of view of the CS:S view models", true, 40, true, 90);

ConVar cl_agent_ct("cl_agent_ct", "0", FCVAR_ARCHIVE, "Your CT agent: 0 SEAL Team 6, 1 GSG-9, 2 SAS, 3 GIGN", true, 0, true, 3);
ConVar cl_agent_t("cl_agent_t", "0", FCVAR_ARCHIVE, "Your T agent: 0 Phoenix, 1 Elite Crew, 2 Arctic Avengers, 3 Guerilla Warfare", true, 0, true, 3);
const char* CSS_AgentName(int team, int idx) {
  static const char* ct[4] = { "SEAL Team 6", "GSG-9", "SAS", "GIGN" };
  static const char* t[4] = { "Phoenix Connexion", "Elite Crew", "Arctic Avengers", "Guerilla Warfare" };
  return team == TEAM_CT ? ct[idx & 3] : t[idx & 3];
}

bool CSS_ModelsActive() { return cl_css_models.GetBool() && srcfs::Available(); }

namespace {
struct WeaponModel { const char* model; const char* anim; };
WeaponModel WeaponInfo(int wid) {
  switch (wid) {
    case WEAPON_KNIFE: return { "knife_t", "KNIFE" };
    case WEAPON_GLOCK: return { "pist_glock18", "PISTOL" };
    case WEAPON_USP_SILENCER: return { "pist_usp", "PISTOL" };
    case WEAPON_P250: return { "pist_p228", "PISTOL" };
    case WEAPON_DEAGLE: case WEAPON_REVOLVER: return { "pist_deagle", "PISTOL" };
    case WEAPON_MAC10: return { "smg_mac10", "MAC10" };
    case WEAPON_MP9: return { "smg_tmp", "TMP" };
    case WEAPON_UMP45: return { "smg_ump45", "UMP45" };
    case WEAPON_P90: return { "smg_p90", "P90" };
    case WEAPON_NOVA: return { "shot_m3super90", "M3S90" };
    case WEAPON_XM1014: return { "shot_xm1014", "XM1014" };
    case WEAPON_GALILAR: return { "rif_galil", "GALIL" };
    case WEAPON_FAMAS: return { "rif_famas", "FAMAS" };
    case WEAPON_AK47: return { "rif_ak47", "AK" };
    case WEAPON_M4A1: case WEAPON_M4A1_SILENCER: return { "rif_m4a1", "M4" };
    case WEAPON_SG556: return { "rif_sg552", "SG552" };
    case WEAPON_AUG: return { "rif_aug", "AUG" };
    case WEAPON_SSG08: return { "snip_scout", "SCOUT" };
    case WEAPON_AWP: return { "snip_awp", "AWP" };
    case WEAPON_G3SG1: return { "snip_g3sg1", "G3" };
    case WEAPON_SCAR20: return { "snip_sg550", "SG550" };
    case WEAPON_HEGRENADE: return { "eq_fraggrenade", "GREN" };
    case WEAPON_FLASHBANG: return { "eq_flashbang", "GREN" };
    case WEAPON_SMOKEGRENADE: return { "eq_smokegrenade", "GREN" };
    case WEAPON_MOLOTOV: case WEAPON_INCGRENADE: return { "eq_fraggrenade", "GREN" };   // without the CS:GO extras
    case WEAPON_C4: return { "c4", "C4" };
    // CS:GO-only guns: the closest CS:S model
    case WEAPON_ELITE: return { "pist_elite", "ELITES" };
    case WEAPON_FIVESEVEN: return { "pist_fiveseven", "PISTOL" };
    case WEAPON_TEC9: return { "pist_glock18", "PISTOL" };
    case WEAPON_CZ75A: return { "pist_p228", "PISTOL" };
    case WEAPON_HKP2000: return { "pist_usp", "PISTOL" };
    case WEAPON_MP7: case WEAPON_MP5SD: return { "smg_mp5", "MP5" };
    case WEAPON_BIZON: return { "smg_ump45", "UMP45" };
    case WEAPON_MAG7: return { "shot_xm1014", "XM1014" };
    case WEAPON_SAWEDOFF: return { "shot_m3super90", "M3S90" };
    case WEAPON_M249: case WEAPON_NEGEV: return { "mach_m249para", "M249" };
    default: return { nullptr, "KNIFE" };
  }
}
bool SilencerModel(int wid) { return wid == WEAPON_USP_SILENCER || wid == WEAPON_M4A1_SILENCER; }

const char* const s_agentCT[4] = { "ct_urban", "ct_gsg9", "ct_sas", "ct_gign" };
const char* const s_agentT[4] = { "t_phoenix", "t_leet", "t_arctic", "t_guerilla" };
const CSrcModel* AgentModel(int team, int idx) {
  idx &= 3;
  return SrcMdl_Get(std::string("models/player/") + (team == TEAM_CT ? s_agentCT[idx] : s_agentT[idx]) + ".mdl");
}
const CSrcModel* PlayerModel(CCSPlayer* p) {
  int idx = (p->entindex() + p->m_iPlayerModel) & 3;
  if (p->IsLocalPlayer()) idx = (p->m_iTeamNum == TEAM_CT ? cl_agent_ct : cl_agent_t).GetInt();
  return AgentModel(p->m_iTeamNum, idx);
}
// CS:GO R8 Revolver (downloaded extra, see engine/download): Garry's Mod port, v48
const char* const kR8World = "models/weapons/w_models/csgo/w_pist_revolver.mdl";
const char* const kR8View = "models/weapons/v_models/csgo/v_pist_revolver.mdl";
const CSrcModel* LoadedOrNull(const CSrcModel* m) { return m && !m->meshes.empty() ? m : nullptr; }
// CS:GO models of the "csgo" extras pack (ARC9 GSR, a Garry's Mod port): c_ view models without hands (the agent's arms
// are merged on), the guns have no world models - the c_ model itself is drawn in the hand (`mirror`, ARC9 WorldModelOffset)
// mirror: the gun root bone of the c_ model is put on ValveBiped.weapon_bone of the CS:S stand-in (both frames have the
// barrel along +z and the top along +y), shifted by `mirrorOffset` (barrel height / grip, measured offline from the meshes)
struct CsgoModel { int wid; const char* view; const char* world; const char* mirrorRoot; Vector mirrorOffset; };
const CsgoModel kCsgoModels[] = {
  { WEAPON_SCAR20, "models/weapons/csgo/c_snip_scar20.mdl", nullptr, "v_weapon.SCAR_Parent", Vector(-1.08f, -5.48f, -3.54f) },
  { WEAPON_SSG08, "models/weapons/csgo/c_snip_ssg08.mdl", nullptr, "v_weapon.ssg08_parent", Vector(0.28f, -4.28f, -3.f) },
  { WEAPON_MOLOTOV, "models/weapons/csgo/v_eq_molotov.mdl", "models/weapons/csgo/w_eq_molotov.mdl", nullptr, {} },
  { WEAPON_INCGRENADE, "models/weapons/csgo/v_eq_incediary.mdl", "models/weapons/csgo/w_eq_incendiary.mdl", nullptr, {} },
};
// ARC9 GSR guns have no built-in optics: the scope is a separate model on the gun root bone (SWEP.Attachments "optics":
// Bone/Pos/Ang + the attachment's ModelOffset/Scale)
struct CsgoScope { const char* gun; const char* scope; const char* bone; Vector pos; Vector modelOffset; float scale; };
const CsgoScope kCsgoScopes[] = {
  { "c_snip_scar20.mdl", "models/weapons/csgo/atts/scopes/scope_scar.mdl", "v_weapon.SCAR_Parent", Vector(0, -6.4f, 3.4f), Vector(0, 0, -0.25f), 1.f },
  { "c_snip_ssg08.mdl", "models/weapons/csgo/atts/scopes/scope_ssg08.mdl", "v_weapon.ssg08_parent", Vector(0.2f, -3.5f, 9.5f), Vector(0, 0, 0), 1.1f },
};

const CsgoModel* CsgoModelFor(int wid) {
  if (!cl_csgo_models.GetBool()) return nullptr;
  for (const CsgoModel& c : kCsgoModels) if (c.wid == wid) return &c;
  return nullptr;
}
// gun drawn from its c_ view model in third person (nullptr: use the w_ model)
const CSrcModel* MirrorWeapon(int wid, const CsgoModel** info = nullptr) {
  const CsgoModel* c = CsgoModelFor(wid); if (!c || c->world) return nullptr;
  const CSrcModel* m = LoadedOrNull(SrcMdl_Get(c->view)); if (!m || m->seqs.empty()) return nullptr;
  if (info) *info = c;
  return m;
}

const CSrcModel* WorldWeapon(int wid, bool silencer = false, bool thrown = false, bool cssOnly = false) {
  if (!cssOnly && wid == WEAPON_REVOLVER && cl_r8_model.GetBool()) if (const CSrcModel* r8 = LoadedOrNull(SrcMdl_Get(kR8World))) return r8;
  if (!cssOnly) if (const CsgoModel* c = CsgoModelFor(wid)) if (c->world) if (const CSrcModel* m = LoadedOrNull(SrcMdl_Get(c->world))) return m;
  WeaponModel wi = WeaponInfo(wid); if (!wi.model) return nullptr;
  std::string n = std::string("models/weapons/w_") + wi.model;
  if (silencer && wid == WEAPON_USP_SILENCER) n += "_silencer";
  if (silencer && wid == WEAPON_M4A1_SILENCER) n += "_silencer";
  if (thrown && IsGrenade(wid)) n += "_thrown";
  return SrcMdl_Get(n + ".mdl");
}

// ------------------------------------------------------------------------------------------------
// third person animation - a pure function of PlayerPoseInput, so the server hitboxes, lag records and the
// rendered model are the same pose
void LayerFull(std::vector<Vector>& pos, std::vector<Quat>& q, const std::vector<Vector>& lp, const std::vector<Quat>& lq, const std::vector<float>& w, float amount) {
  for (size_t b = 0; b < pos.size(); b++) {
    float k = (b < w.size() ? w[b] : 1.f) * amount; if (k <= 0.001f) continue;
    q[b] = QuatSlerp(q[b], lq[b], std::min(1.f, k)); pos[b] = pos[b] + (lp[b] - pos[b]) * std::min(1.f, k);
  }
}
// Source SlerpBones for STUDIO_DELTA sequences: STUDIO_POST deltas (the CS:S aim matrices / shoot layers, flags 0x14) are
// applied in the bone's own frame (QuaternionMA: base * delta), plain deltas in the parent frame (QuaternionSM: delta * base)
constexpr int STUDIO_POST_FLAG = 0x10;
void LayerDelta(std::vector<Vector>& pos, std::vector<Quat>& q, const std::vector<Vector>& dp, const std::vector<Quat>& dq, const std::vector<float>& w, float amount, bool post) {
  for (size_t b = 0; b < pos.size(); b++) {
    float k = (b < w.size() ? w[b] : 1.f) * amount; if (k <= 0.001f) continue;
    Quat d = k >= 0.999f ? dq[b] : QuatSlerp(Quat(), dq[b], k);
    q[b] = post ? QuatMul(q[b], d) : QuatMul(d, q[b]); pos[b] = pos[b] + dp[b] * k;
  }
}
bool IsPost(const SrcSeqRef& r) { return r && (r.src->seqs[r.seq].flags & STUDIO_POST_FLAG); }

// sequence lookups are string based - cache them per model
struct SeqCache { std::unordered_map<std::string, SrcSeqRef> map; };
SrcSeqRef Seq(const CSrcModel& m, const std::string& name) {
  thread_local std::unordered_map<const CSrcModel*, SeqCache> cache;
  auto& c = cache[&m].map;
  auto it = c.find(name); if (it != c.end()) return it->second;
  SrcSeqRef r = m.FindSeq(name); c[name] = r; return r;
}

void BuildPlayerBones(const CSrcModel& m, const PlayerPoseInput& in, std::vector<matrix3x4_t>& bones) {
  const float now = in.animTime >= 0 ? in.animTime : (float)gpGlobals->curtime;
  thread_local std::vector<Vector> pos, lp; thread_local std::vector<Quat> q, lq; thread_local std::vector<float> w;
  SrcPoseValues pv;
  // death
  if (in.deathCycle >= 0) {
    SrcSeqRef d = Seq(m, "Death1");
    float dur = std::max(0.1f, m.SeqDuration(d));
    m.SampleSeq(d, Clamp(in.deathTime / dur, 0.f, 1.f), pv, pos, q);
    m.BuildBones(pos, q, bones);
    return;
  }
  const bool crouch = in.duckAmount > 0.5f;
  const float speed = in.moveSpeed * 250.f;
  const bool moving = speed > 12.f;
  const bool run = speed > 150.f;
  // movement direction on the 9-way blend square
  float yaw = DEG2RADF(in.moveYaw);
  float mx = std::cos(yaw), my = -std::sin(yaw);
  float mm = std::max(std::fabs(mx), std::fabs(my)); if (mm > 1e-3f) { mx /= mm; my /= mm; }
  float mag = moving ? Clamp(speed / (crouch ? 85.f : run ? 250.f : 130.f), 0.35f, 1.f) : 0.f;
  pv.move_x = mx * mag; pv.move_y = my * mag;
  pv.body_yaw = Clamp(AngleDiff(in.eyeAngles.y, in.footYaw), -90.f, 90.f);
  float pitch = in.eyeAngles.x; if (pitch > 180.f) pitch -= 360.f;
  pv.body_pitch = Clamp(pitch, -90.f, 90.f);
  // lower body: the move cycle advances with the distance walked (CCSGOPlayerAnimState, CS:S strides)
  const char* lower = crouch ? (moving ? "Crouch_walk_lower" : "Crouch_Idle_Lower") : (run ? "Run_lower" : moving ? "walk_lower" : "Idle_lower");
  SrcSeqRef base = Seq(m, lower);
  float cycle = moving ? in.moveCycle : std::fmod(now / std::max(0.1f, m.SeqDuration(base)), 1.f);
  m.SampleSeq(base, cycle, pv, pos, q);
  const int lm = cl_css_anim_layers.GetInt();
  // upper body (weapon hold), aim matrix, shoot, reload
  const std::string wpn = WeaponInfo(in.weaponId).anim;
  const std::string st = crouch ? (moving ? "Crouch_Walk_" : "Crouch_Idle_") : (run ? "Run_" : moving ? "Walk_" : "Idle_");
  const std::string aimSt = crouch ? "Crouch_" : (run ? "Run_" : moving ? "Walk_" : "Idle_");
  SrcSeqRef up = Seq(m, st + "Upper_" + wpn);
  if (!in.onGround && (lm & 1)) {
    // the jump pose only drives the bones the weapon-hold layer doesn't own (legs / pelvis): applied to the whole body
    // it bent the spine and arms, and with the aim deltas on top the gun pointed at the sky mid-jump
    SrcSeqRef j = Seq(m, "Jump");
    if (j) {
      thread_local std::vector<float> jw;
      jw.assign(pos.size(), 1.f);
      if (up) { m.SampleSeq(up, cycle, pv, lp, lq, &w); for (size_t b = 0; b < jw.size() && b < w.size(); b++) jw[b] = 1.f - Clamp(w[b], 0.f, 1.f); }
      m.SampleSeq(j, 0.45f, pv, lp, lq, &w);
      LayerFull(pos, q, lp, lq, jw, 1.f);
    }
  }
  if (up && (lm & 2)) { m.SampleSeq(up, cycle, pv, lp, lq, &w); LayerFull(pos, q, lp, lq, w, 1.f); }
  if (in.reloadCycle >= 0 && (lm & 4)) {
    SrcSeqRef r = Seq(m, st + "Reload_" + wpn);
    if (r) { m.SampleSeq(r, in.reloadCycle, pv, lp, lq, &w); LayerFull(pos, q, lp, lq, w, 1.f); }
  }
  if (in.planting && in.weaponId == WEAPON_C4) {
    SrcSeqRef r = Seq(m, st + "Shoot_C4");
    if (r) { m.SampleSeq(r, std::fmod(now / std::max(0.1f, m.SeqDuration(r)), 1.f), pv, lp, lq, &w); LayerFull(pos, q, lp, lq, w, 1.f); }
  }
  SrcSeqRef aim = Seq(m, aimSt + "Aim_" + wpn);
  if (aim && (lm & 8)) { m.SampleSeq(aim, 0, pv, lp, lq, &w); LayerDelta(pos, q, lp, lq, w, 1.f, IsPost(aim)); }
  if (in.fireAmount > 0 && in.weaponId != WEAPON_C4 && (lm & 16)) {
    SrcSeqRef s = Seq(m, st + "Shoot_" + wpn);
    if (!s) s = Seq(m, st + "Shoot_" + wpn + "_R");
    if (s) { m.SampleSeq(s, 1.f - in.fireAmount, pv, lp, lq, &w); LayerDelta(pos, q, lp, lq, w, 1.f, IsPost(s)); }
  }
  m.BuildBones(pos, q, bones);
}

// ------------------------------------------------------------------------------------------------
// hitboxes: the model's own hitbox set (19 oriented boxes) as the capsules of the CS:GO hitbox layout
struct HitboxMap { std::vector<std::pair<int, int>> slots; int spine2 = -1; };   // (slot, model hitbox)
const HitboxMap& GetHitboxMap(const CSrcModel& m) {
  thread_local std::unordered_map<const CSrcModel*, HitboxMap> cache;
  auto it = cache.find(&m); if (it != cache.end()) return it->second;
  HitboxMap hm;
  static const std::pair<const char*, int> names[] = {
    { "Head1", HITBOX_HEAD }, { "Neck1", HITBOX_NECK }, { "Pelvis", HITBOX_PELVIS }, { "Spine1", HITBOX_STOMACH },
    { "R_Thigh", HITBOX_RIGHT_THIGH }, { "L_Thigh", HITBOX_LEFT_THIGH }, { "R_Calf", HITBOX_RIGHT_CALF }, { "L_Calf", HITBOX_LEFT_CALF },
    { "R_Foot", HITBOX_RIGHT_FOOT }, { "L_Foot", HITBOX_LEFT_FOOT }, { "R_Hand", HITBOX_RIGHT_HAND }, { "L_Hand", HITBOX_LEFT_HAND },
    { "R_UpperArm", HITBOX_RIGHT_UPPER_ARM }, { "R_Forearm", HITBOX_RIGHT_FOREARM }, { "L_UpperArm", HITBOX_LEFT_UPPER_ARM }, { "L_Forearm", HITBOX_LEFT_FOREARM },
  };
  for (size_t i = 0; i < m.hitboxes.size(); i++) {
    int b = m.hitboxes[i].bone; if (b < 0 || b >= (int)m.bones.size()) continue;
    const std::string& bn = m.bones[b].name;
    if (bn.size() >= 6 && bn.compare(bn.size() - 6, 6, "Spine2") == 0) { hm.spine2 = (int)i; continue; }
    for (auto& n : names) {
      size_t L = strlen(n.first);
      if (bn.size() >= L && bn.compare(bn.size() - L, L, n.first) == 0 && bn[bn.size() - L - 1] == '_') { hm.slots.push_back({ n.second, (int)i }); break; }
    }
  }
  return cache[&m] = hm;
}
void BoxToCapsule(Vector mn, Vector mx, const matrix3x4_t& boneToWorld, mstudiobbox_t& out) {
  Vector c = (mn + mx) * 0.5f, h = (mx - mn) * 0.5f;
  int k = (h.x >= h.y && h.x >= h.z) ? 0 : (h.y >= h.z ? 1 : 2);
  float r = std::min(h[k], (h[(k + 1) % 3] + h[(k + 2) % 3]) * 0.5f);
  Vector a = c, b = c; a[k] -= h[k] - r; b[k] += h[k] - r;
  out.a = VectorTransform(a, boneToWorld); out.b = VectorTransform(b, boneToWorld); out.radius = r;
}
void ModelHitboxes(const CSrcModel& m, const std::vector<matrix3x4_t>& bones, const matrix3x4_t& toWorld, HitboxSet& out) {
  const HitboxMap& hm = GetHitboxMap(m);
  auto boneWorld = [&](int b) { matrix3x4_t w; ConcatTransforms(toWorld, bones[b], w); return w; };
  for (auto& sl : hm.slots) {
    const SrcMdlHitbox& h = m.hitboxes[sl.second];
    if (h.bone >= (int)bones.size()) continue;
    BoxToCapsule(h.mins, h.maxs, boneWorld(h.bone), out[sl.first]);
    out[sl.first].group = HitboxToHitgroup(sl.first);
  }
  if (hm.spine2 >= 0 && m.hitboxes[hm.spine2].bone < (int)bones.size()) {   // chest box -> lower / middle / upper chest
    const SrcMdlHitbox& h = m.hitboxes[hm.spine2];
    matrix3x4_t w = boneWorld(h.bone);
    const int slots[3] = { HITBOX_LOWER_CHEST, HITBOX_CHEST, HITBOX_UPPER_CHEST };
    for (int i = 0; i < 3; i++) {
      Vector mn = h.mins, mx = h.maxs;
      float x0 = h.mins.x + (h.maxs.x - h.mins.x) * i / 3.f, x1 = h.mins.x + (h.maxs.x - h.mins.x) * (i + 1) / 3.f;
      mn.x = x0; mx.x = x1;
      BoxToCapsule(mn, mx, w, out[slots[i]]); out[slots[i]].group = HitboxToHitgroup(slots[i]);
    }
  }
}
// no model loaded: a simple standing / crouching body so the game logic still works
void FallbackHitboxes(const PlayerPoseInput& in, HitboxSet& out) {
  float h = 1.f - 0.25f * Clamp(in.duckAmount, 0.f, 1.f);
  Vector f, r, u; AngleVectors(QAngle(0, in.footYaw, 0), &f, &r, &u);
  Vector o = in.origin; Vector up(0, 0, 1);
  auto cap = [&](int i, Vector a, Vector b, float rad) { out[i].a = a; out[i].b = b; out[i].radius = rad; out[i].group = HitboxToHitgroup(i); };
  cap(HITBOX_HEAD, o + up * (64 * h), o + up * (66 * h) + f * 1.5f, 4.2f);
  cap(HITBOX_NECK, o + up * (58 * h), o + up * (61 * h), 3.4f);
  cap(HITBOX_UPPER_CHEST, o + up * (53 * h) - r * 5, o + up * (53 * h) + r * 5, 6.f);
  cap(HITBOX_CHEST, o + up * (48 * h) - r * 5, o + up * (48 * h) + r * 5, 6.f);
  cap(HITBOX_LOWER_CHEST, o + up * (43 * h) - r * 5, o + up * (43 * h) + r * 5, 6.f);
  cap(HITBOX_STOMACH, o + up * (38 * h) - r * 4, o + up * (38 * h) + r * 4, 6.f);
  cap(HITBOX_PELVIS, o + up * (33 * h) - r * 4, o + up * (33 * h) + r * 4, 6.f);
  for (int s = 0; s < 2; s++) {
    float sd = s == 0 ? 1.f : -1.f; Vector side = r * (4.f * sd);
    cap(s == 0 ? HITBOX_RIGHT_THIGH : HITBOX_LEFT_THIGH, o + up * (31 * h) + side, o + up * (18 * h) + side, 4.f);
    cap(s == 0 ? HITBOX_RIGHT_CALF : HITBOX_LEFT_CALF, o + up * (17 * h) + side, o + up * 4 + side, 3.4f);
    cap(s == 0 ? HITBOX_RIGHT_FOOT : HITBOX_LEFT_FOOT, o + up * 2 + side, o + up * 2 + side + f * 6, 2.6f);
    Vector sh = o + up * (54 * h) + r * (8.f * sd);
    cap(s == 0 ? HITBOX_RIGHT_UPPER_ARM : HITBOX_LEFT_UPPER_ARM, sh, sh + f * 6 - up * 8, 2.6f);
    cap(s == 0 ? HITBOX_RIGHT_FOREARM : HITBOX_LEFT_FOREARM, sh + f * 6 - up * 8, sh + f * 16 - up * 6 - r * (3.f * sd), 2.3f);
    cap(s == 0 ? HITBOX_RIGHT_HAND : HITBOX_LEFT_HAND, sh + f * 16 - up * 6 - r * (3.f * sd), sh + f * 20 - up * 5 - r * (4.f * sd), 2.f);
  }
}

// weapon bone merge: bones with the same name as a player bone follow the player
void BoneMerge(const CSrcModel& w, const CSrcModel& parent, const std::vector<matrix3x4_t>& pb, std::vector<matrix3x4_t>& out) {
  out.resize(w.bones.size());
  for (size_t i = 0; i < w.bones.size(); i++) {
    int j = parent.FindBone(w.bones[i].name);
    if (j >= 0 && j < (int)pb.size()) { out[i] = pb[j]; continue; }
    matrix3x4_t local; QuaternionMatrix(w.bones[i].quat, w.bones[i].pos, Vector(1, 1, 1), local);
    int p = w.bones[i].parent;
    if (p >= 0 && p < (int)i) ConcatTransforms(out[p], local, out[i]); else out[i] = local;
  }
}
} // namespace

static void BuildPoseForModel(const CSrcModel* m, CCSPlayer* p, const PlayerPoseInput& in, PlayerPoseOutput& out, bool computeHitboxes);
void BuildPlayerPose(CCSPlayer* p, const PlayerPoseInput& in, PlayerPoseOutput& out, bool computeHitboxes) {
  BuildPoseForModel(p && srcfs::Available() ? PlayerModel(p) : nullptr, p, in, out, computeHitboxes);
}

bool CSS_DrawAgent(int team, int agent, const PlayerPoseInput& in, const StudioDrawParams& p) {
  if (!srcfs::Available()) return false;
  thread_local PlayerPoseOutput out;
  BuildPoseForModel(AgentModel(team, agent), nullptr, in, out, false);
  if (!out.css) return false;
  StudioDrawParams q = p; q.modelToWorld = out.cssToWorld;
  return CSS_DrawPose(out, q, true);
}

static void BuildPoseForModel(const CSrcModel* m, CCSPlayer* p, const PlayerPoseInput& in, PlayerPoseOutput& out, bool computeHitboxes) {
  out.css = nullptr; out.cssWeapon = nullptr; out.hasWeapon = false; out.hasMuzzle = false;
  AngleMatrix(QAngle(0, in.footYaw, 0), in.origin, out.cssToWorld);
  out.modelToWorld = out.cssToWorld; out.weaponToWorld = out.cssToWorld;
  out.eyePos = in.origin + Vector(0, 0, 64.f - 18.f * Clamp(in.duckAmount, 0.f, 1.f));
  if (!m || m->bones.empty() || m->includes.empty()) { if (computeHitboxes) FallbackHitboxes(in, out.hitboxes); return; }
  BuildPlayerBones(*m, in, out.cssBones);
  out.css = m;
  if (computeHitboxes) { FallbackHitboxes(in, out.hitboxes); ModelHitboxes(*m, out.cssBones, out.cssToWorld, out.hitboxes); }
  if (in.deathCycle < 0 && in.weaponId) {
    bool sil = false;
    if (p && SilencerModel(in.weaponId)) { C_WeaponCSBase* w = p->GetActiveCSWeapon(); sil = w && w->GetCSWeaponID() == in.weaponId && w->m_bSilencerOn; }
    const CsgoModel* mi = nullptr;
    const CSrcModel* mirror = MirrorWeapon(in.weaponId, &mi);
    const CSrcModel* wm = WorldWeapon(in.weaponId, sil, false, mirror != nullptr);
    if (mirror && wm && !wm->meshes.empty()) {
      // place the CS:S stand-in first (bone merge + hand fix below), then hang the c_ gun on its weapon_bone
      thread_local std::vector<matrix3x4_t> standIn;
      BoneMerge(*wm, *m, out.cssBones, standIn);
      int hand = m->FindBone("ValveBiped.Bip01_R_Hand"), grip = m->FindBone("ValveBiped.weapon_bone_RHand");
      if (hand >= 0 && grip >= 0 && hand < (int)out.cssBones.size() && grip < (int)out.cssBones.size()) {
        matrix3x4_t gripInv, fix; MatrixInvert(out.cssBones[grip], gripInv);
        ConcatTransforms(out.cssBones[hand], gripInv, fix);
        for (auto& b : standIn) { matrix3x4_t t; ConcatTransforms(fix, b, t); b = t; }
      }
      int wb = wm->FindBone("ValveBiped.weapon_bone"), root = mirror->FindBone(mi->mirrorRoot);
      if (wb >= 0 && root >= 0) {
        thread_local std::vector<Vector> cp; thread_local std::vector<Quat> cq; thread_local std::vector<matrix3x4_t> cb;
        SrcSeqRef idle = mirror->FindSeq("idle"); if (!idle) idle = { mirror, 0, -1 };
        mirror->SampleSeq(idle, 0.f, SrcPoseValues(), cp, cq); mirror->BuildBones(cp, cq, cb);
        matrix3x4_t off; AngleMatrix(QAngle(0, 0, 0), mi->mirrorOffset, off);
        matrix3x4_t rootInv, a1, toWorld; MatrixInvert(cb[root], rootInv);
        ConcatTransforms(standIn[wb], off, a1); ConcatTransforms(a1, rootInv, toWorld);
        out.cssWeaponBones.resize(cb.size());
        for (size_t i = 0; i < cb.size(); i++) ConcatTransforms(toWorld, cb[i], out.cssWeaponBones[i]);
        out.cssWeapon = mirror; out.hasWeapon = true;
        if (!mirror->attachments.empty()) { out.muzzle = mirror->AttachmentPos(0, out.cssWeaponBones, out.cssToWorld); out.hasMuzzle = true; }
        return;
      }
    }
    if (wm && !wm->meshes.empty()) {
      BoneMerge(*wm, *m, out.cssBones, out.cssWeaponBones);
      // the player animations move weapon_bone on their own (CS:S lets IK pull the hands onto the gun, we don't
      // run IK): put the gun into the right hand instead - weapon_bone_RHand is where the hand grips the weapon
      int hand = m->FindBone("ValveBiped.Bip01_R_Hand"), grip = m->FindBone("ValveBiped.weapon_bone_RHand");
      const bool handRooted = wm->FindBone("ValveBiped.weapon_bone") < 0 && wm->FindBone("ValveBiped.Bip01_R_Hand") >= 0;   // CS:GO w_eq_* ports
      if (!handRooted && hand >= 0 && grip >= 0 && hand < (int)out.cssBones.size() && grip < (int)out.cssBones.size()) {
        matrix3x4_t gripInv, fix; MatrixInvert(out.cssBones[grip], gripInv);
        ConcatTransforms(out.cssBones[hand], gripInv, fix);
        for (auto& b : out.cssWeaponBones) { matrix3x4_t t; ConcatTransforms(fix, b, t); b = t; }
      }
      out.cssWeapon = wm; out.hasWeapon = true;
      for (size_t i = 0; i < wm->attachments.size(); i++)
        if (wm->attachments[i].name == "muzzle_flash" || wm->attachments[i].name == "muzzle" || wm->attachments[i].name == "1") { out.muzzle = wm->AttachmentPos((int)i, out.cssWeaponBones, out.cssToWorld); out.hasMuzzle = true; break; }
    }
  }
}

// draws the optics of a CS:GO (ARC9) gun posed with `bones` (gun model space) under `xf`
static void DrawCsgoScope(const CSrcModel& gun, const std::vector<matrix3x4_t>& bones, const matrix3x4_t& xf, const StudioDrawParams& p) {
  for (const CsgoScope& sc : kCsgoScopes) {
    const size_t gl = strlen(sc.gun);
    if (gun.name.size() < gl || gun.name.compare(gun.name.size() - gl, gl, sc.gun) != 0) continue;
    const CSrcModel* s = LoadedOrNull(SrcMdl_Get(sc.scope)); if (!s) return;
    int b = gun.FindBone(sc.bone); if (b < 0 || b >= (int)bones.size()) return;
    // GMod bone angles: Forward = +x, Right = -y, Up = +z. Ang(90, 0, -90) turns the attachment to
    // Forward = bone +z (barrel), Right = bone -x, Up = bone +y (top of the gun); the scope model is drawn in that frame
    const Vector F(0, 0, 1), R(-1, 0, 0), U(0, 1, 0);
    Vector org = Vector(sc.pos.x, -sc.pos.y, sc.pos.z) + F * sc.modelOffset.x + R * sc.modelOffset.y + U * sc.modelOffset.z;
    matrix3x4_t local;
    const Vector cx = F, cy = R * -1.f, cz = U;   // model axes in the gun frame (checked: eyepiece at the back, objective bell forward)
    for (int i = 0; i < 3; i++) { local[i][0] = cx[i] * sc.scale; local[i][1] = cy[i] * sc.scale; local[i][2] = cz[i] * sc.scale; local[i][3] = org[i]; }
    matrix3x4_t att; ConcatTransforms(bones[b], local, att);
    thread_local std::vector<matrix3x4_t> sb; thread_local std::vector<Vector> pos; thread_local std::vector<Quat> q;
    s->SampleAnim(-1, 0, pos, q); s->BuildBones(pos, q, sb);
    for (auto& m : sb) { matrix3x4_t t; ConcatTransforms(att, m, t); m = t; }
    SrcMdl_Draw(*s, sb, xf, p);
    return;
  }
}

bool CSS_DrawPose(const PlayerPoseOutput& pose, const StudioDrawParams& p, bool drawWeapon) {
  if (!pose.css) return false;
  SrcMdl_Draw(*pose.css, pose.cssBones, pose.cssToWorld, p);
  if (drawWeapon && pose.cssWeapon) { SrcMdl_Draw(*pose.cssWeapon, pose.cssWeaponBones, pose.cssToWorld, p); DrawCsgoScope(*pose.cssWeapon, pose.cssWeaponBones, pose.cssToWorld, p); }
  return true;
}

bool CSS_DrawModelFile(const char* path, const matrix3x4_t& xf, const StudioDrawParams& p) {
  if (!srcfs::Available()) return false;
  const CSrcModel* m = SrcMdl_Get(path);
  if (!m || m->meshes.empty()) return false;
  thread_local std::vector<matrix3x4_t> bones; thread_local std::vector<Vector> pos; thread_local std::vector<Quat> q;
  m->SampleAnim(-1, 0, pos, q); m->BuildBones(pos, q, bones);
  SrcMdl_Draw(*m, bones, xf, p);
  return true;
}

bool CSS_DrawWorldWeapon(int weaponId, const matrix3x4_t& xf, const StudioDrawParams& p, bool thrown) {
  if (!CSS_ModelsActive()) return false;
  const CSrcModel* m = WorldWeapon(weaponId, false, thrown);
  if (!m || m->meshes.empty()) return false;
  thread_local std::vector<matrix3x4_t> bones; thread_local std::vector<Vector> pos; thread_local std::vector<Quat> q;
  m->SampleAnim(-1, 0, pos, q); m->BuildBones(pos, q, bones);
  SrcMdl_Draw(*m, bones, xf, p);
  return true;
}

// ------------------------------------------------------------------------------------------------
// view models
namespace {
// c_arms (Garry's Mod style): the CS:GO R8 view model has no hands, only ValveBiped.* arm bones - the arms of the
// player's own agent model are bone merged onto them and only the forearm / hand / finger triangles are drawn
void DrawViewModelArms(const CSrcModel& vm, const std::vector<matrix3x4_t>& vmBones, const CSrcModel* pm, const matrix3x4_t& xf, const StudioDrawParams& dp) {
  if (!pm || pm->meshes.empty()) return;
  thread_local std::vector<matrix3x4_t> armBones; thread_local std::vector<uint8_t> mask;
  BoneMerge(*pm, vm, vmBones, armBones);
  mask.assign(pm->bones.size(), 0);
  for (size_t i = 0; i < pm->bones.size(); i++) {
    const std::string& n = pm->bones[i].name;
    bool arm = n.find("Forearm") != std::string::npos || n.find("ForeTwist") != std::string::npos || n.find("_Hand") != std::string::npos ||
               n.find("Finger") != std::string::npos || n.find("Wrist") != std::string::npos || n.find("Ulna") != std::string::npos;
    if (!arm) continue;
    int par = pm->bones[i].parent;
    if (vm.FindBone(n) >= 0 || (par >= 0 && par < (int)i && mask[par])) mask[i] = 1;   // merged, or hangs off a merged arm bone
  }
  SrcMdl_Draw(*pm, armBones, xf, dp, 0, &mask);
}
SrcSeqRef PickVm(const CSrcModel& m, std::initializer_list<const char*> acts, bool silenced, int variant) {
  for (const char* a : acts) {
    std::vector<SrcSeqRef> v;
    if (silenced) v = m.FindSeqsByActivity(std::string(a) + "_SILENCED");
    if (v.empty()) v = m.FindSeqsByActivity(a);
    if (!v.empty()) return v[(size_t)std::max(0, variant) % v.size()];
  }
  return {};
}
}

bool CSS_DrawViewModel(const CViewSetup& view, CCSPlayer* p, C_WeaponCSBase* w, float bobTime, float bob, const QAngle& sway, float landKick, Vector& muzzleOut) {
  if (!cl_css_viewmodels.GetBool() || !srcfs::Available()) return false;
  const int wid = w->GetCSWeaponID();
  WeaponModel wi = WeaponInfo(wid); if (!wi.model) return false;
  const CSrcModel* m = nullptr;
  bool r8 = false;   // CS:GO model: right-handed, hands from the agent model, its own FOV
  if (wid == WEAPON_REVOLVER && cl_r8_model.GetBool()) { m = LoadedOrNull(SrcMdl_Get(kR8View)); r8 = m && !m->seqs.empty(); if (!r8) m = nullptr; }
  bool csgo = false;
  if (!m) if (const CsgoModel* c = CsgoModelFor(wid)) { m = LoadedOrNull(SrcMdl_Get(c->view)); csgo = m && !m->seqs.empty(); if (!csgo) m = nullptr; }
  if (!m) m = SrcMdl_Get(std::string("models/weapons/v_") + wi.model + ".mdl");
  if (!m || m->meshes.empty() || m->seqs.empty()) return false;
  const bool silenced = SilencerModel(wid) && w->m_bSilencerOn;
  const int act = w->m_iViewActivity;
  float elapsed = std::max(0.f, gpGlobals->curtime - w->m_flViewActivityStart);
  const int variant = w->m_iViewShotSide;
  const bool empty = w->IsGun() && w->m_iClip1 == 0;
  SrcSeqRef seq; bool stretch = false;
  switch (act) {
    case ACT_VM_DRAW: seq = PickVm(*m, { "ACT_VM_DRAW" }, silenced, 0); stretch = true; break;
    case ACT_VM_PRIMARYATTACK:
      if (wid == WEAPON_KNIFE) { seq = m->FindSeq((variant & 1) ? "midslash2" : "midslash1"); if (!seq) seq = PickVm(*m, { "ACT_VM_HITCENTER", "ACT_VM_MISSCENTER" }, false, variant); }
      else if (wid == WEAPON_C4) seq = PickVm(*m, { "ACT_VM_PRIMARYATTACK" }, false, 0);
      else seq = empty ? PickVm(*m, { "ACT_VM_DRYFIRE", "ACT_VM_PRIMARYATTACK" }, silenced, 0) : PickVm(*m, { "ACT_VM_PRIMARYATTACK" }, silenced, variant);
      break;
    case ACT_VM_SECONDARYATTACK: seq = wid == WEAPON_KNIFE ? m->FindSeq("stab") : PickVm(*m, { "ACT_VM_SECONDARYATTACK" }, silenced, 0); break;
    case ACT_VM_RELOAD:
      if (m->FindSeq("insert")) seq = m->FindSeq("insert");   // shotguns: shell after shell
      else { seq = PickVm(*m, { "ACT_VM_RELOAD" }, silenced, 0); stretch = true; }
      break;
    case ACT_VM_PULLPIN: seq = PickVm(*m, { "ACT_VM_PULLPIN" }, false, 0); break;
    case ACT_VM_THROW: seq = PickVm(*m, { "ACT_VM_THROW" }, false, 0); break;
    case ACT_VM_ATTACH_SILENCER: seq = PickVm(*m, { "ACT_VM_ATTACH_SILENCER" }, false, 0); stretch = true; break;
    case ACT_VM_DETACH_SILENCER: seq = PickVm(*m, { "ACT_VM_DETACH_SILENCER" }, false, 0); stretch = true; break;
    case ACT_VM_PLANT: seq = PickVm(*m, { "ACT_VM_PRIMARYATTACK" }, false, 0); stretch = true; break;
    case ACT_VM_HAULBACK: seq = PickVm(*m, { "ACT_VM_HAULBACK" }, false, 0); break;   // R8: pull the hammer back, hold it there
    default: break;
  }
  if (IsGrenade(wid)) { auto* g = static_cast<CBaseCSGrenade*>(w); if (g->m_bPinPulled && act != ACT_VM_THROW) { seq = PickVm(*m, { "ACT_VM_PULLPIN" }, false, 0); if (act != ACT_VM_PULLPIN) elapsed = 99.f; } }
  float cycle = 0;
  bool shotgunLoop = act == ACT_VM_RELOAD && m->FindSeq("insert");
  if (seq) {
    float dur = std::max(0.01f, m->SeqDuration(seq));
    float rate = stretch && w->m_flViewActivityDuration > 0.05f ? dur / w->m_flViewActivityDuration : 1.f;
    float t = elapsed * rate;
    const SrcMdlSeq& sq = seq.src->seqs[seq.seq];
    if (shotgunLoop && w->m_bInReload) cycle = std::fmod(t / dur, 1.f);
    else if (act == ACT_VM_HAULBACK) cycle = Clamp(t / dur, 0.f, 1.f);
    else if (t >= dur && !sq.looping && act != ACT_VM_PULLPIN && !IsGrenade(wid)) { elapsed = (t - dur) / std::max(0.01f, rate); seq = {}; }
    else cycle = Clamp(t / dur, 0.f, 1.f);
  }
  if (!seq) {
    seq = PickVm(*m, { "ACT_VM_IDLE" }, silenced, 0); if (!seq) seq = { m, 0, -1 };
    float dur = std::max(0.01f, m->SeqDuration(seq));
    cycle = std::fmod(elapsed / dur, 1.f);
  }
  thread_local std::vector<Vector> pos; thread_local std::vector<Quat> q; thread_local std::vector<matrix3x4_t> bones;
  m->SampleSeq(seq, cycle, SrcPoseValues(), pos, q);
  m->BuildBones(pos, q, bones);

  // Source: view model at the eye with the view angles; bob + sway on top
  float bt = bobTime * 2.f * (float)M_PI / 0.8f;
  QAngle ang = view.angles; ang.x += sway.x * 0.3f; ang.y += sway.y * 0.3f;
  Vector org = view.origin + view.forward * (std::sin(bt) * 0.4f * bob) - view.up * (landKick * 1.f + std::fabs(std::sin(bt * 0.5f)) * 0.4f * bob);
  matrix3x4_t xf; AngleMatrix(ang, org, xf);
  // CS:S v_ models are built left-handed (the game mirrors them for cl_righthand 1), the CS:GO R8 is right-handed
  static ConVar* righthand = FindConVar("cl_righthand");
  const bool wantRight = !righthand || righthand->GetBool();
  if (wantRight != (r8 || csgo)) for (int i = 0; i < 3; i++) xf[i][1] = -xf[i][1];   // mirror along the view's right axis

  StudioLight L;
  if (g_WorldRender.built) { LightSample s = g_WorldRender.SampleLight(view.origin); L.ambient = s.ambient * 1.15f; L.direct = s.direct * 0.8f; L.dir = s.dir; }
  L.ambient = L.ambient * (0.35f + 0.65f * cl.worldBrightness); L.direct = L.direct * cl.worldBrightness;
  if (cl.nightvision) L.ambient = Vector(0.9f, 1.2f, 0.9f);
  StudioDrawParams dp; dp.light = L; dp.eye = view.origin;

  Matrix proj = BuildProjection(view.VerticalFov((r8 ? cl_r8_viewmodel_fov : csgo ? cl_csgo_viewmodel_fov : cl_css_viewmodel_fov).GetFloat()), (float)view.width / std::max(1, view.height), 1.f, 600.f);
  Render_ClearDepth();
  Render_Begin3D(view.view, proj);
  SrcMdl_Draw(*m, bones, xf, dp);
  if (csgo) DrawCsgoScope(*m, bones, xf, dp);
  if (r8 || csgo) DrawViewModelArms(*m, bones, PlayerModel(p), xf, dp);
  Render_End3D();
  muzzleOut = m->attachments.empty() ? view.origin + view.forward * 20.f - view.up * 4.f : m->AttachmentPos(0, bones, xf);
  return true;
}
