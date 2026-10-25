// visuals.cpp - player ESP, chams, world ESP, tracers / impacts, grenade trajectory, aimbot visualisation,
// indicators, manual AA arrows, watermark, hotkey list, spectator list, damage markers
#include "cheat_internal.hpp"
#include "../vgui/surface.hpp"
#include "../engine/enginetrace.hpp"
#include "../engine/host.hpp"
#include "../game/client/cl_main.hpp"
#include "../game/shared/cs_player.hpp"
#include "../game/shared/weapon_csbase.hpp"
#include "../game/shared/cs_projectiles.hpp"
#include "../game/shared/cs_gamerules.hpp"
#include "../game/shared/gameevents.hpp"
#include "../game/shared/mapinfo.hpp"
#include "rlgl.h"
#include <cmath>
#include <ctime>
#include <algorithm>
#include <cctype>
#include <cstdio>
#include <utility>

extern ConVar sv_maxusrcmdprocessticks;
extern ConVar sv_maxdtshift;

namespace cheat {
using vgui::surface;
struct NamedBind { const char* name; KeyBind* bind; };
extern std::vector<NamedBind> g_NamedBinds;

// ------------------------------------------------------------------------------------------------
struct Tracer { Vector a, b; float time; bool hit; };
struct Impact { Vector p; float time; bool server; };
struct DamageMarker { Vector p; int dmg; float time; bool head; };
struct ShotCapsules { HitboxSet hb; float time; int hitbox; };
static std::deque<Tracer> s_tracers;
static std::deque<Impact> s_impacts;
static std::deque<DamageMarker> s_dmg;
static std::deque<ShotCapsules> s_shotHb;
static Vector s_lastHitPos; static float s_lastHitTime = -100;

void Visuals_AddShotHitboxes(const HitboxSet& hb, int hitbox) {
  s_shotHb.push_back({ hb, (float)gpGlobals->realtime, hitbox });
  while (s_shotHb.size() > 4) s_shotHb.pop_front();
}
void Visuals_LevelInit() { s_tracers.clear(); s_impacts.clear(); s_dmg.clear(); s_shotHb.clear(); }

void Visuals_OnEvent(const IGameEvent& ev) {
  CCSPlayer* lp = g_pLocalPlayer; if (!lp) return;
  if (ev.IsEvent("bullet_impact") && ev.GetInt("userid") == lp->entindex()) {
    Vector p(ev.GetFloat("x"), ev.GetFloat("y"), ev.GetFloat("z"));
    s_impacts.push_back({ p, (float)gpGlobals->realtime, false });
    while (s_impacts.size() > 64) s_impacts.pop_front();
    if (!ev.GetBool("exit")) {
      Vector s(ev.GetFloat("sx"), ev.GetFloat("sy"), ev.GetFloat("sz"));
      if (s.IsZero()) s = lp->EyePosition();
      // one tracer per shot: the first segment starts at the eye
      if ((s - lp->EyePosition()).Length() < 40.f || s_tracers.empty() || gpGlobals->realtime - s_tracers.back().time > 0.01f)
        s_tracers.push_back({ s, p, (float)gpGlobals->realtime, ev.GetInt("player") != 0 });
      else s_tracers.back().b = p;
      while (s_tracers.size() > 24) s_tracers.pop_front();
    }
  } else if (ev.IsEvent("player_hurt") && ev.GetInt("attacker") == lp->entindex() && ev.GetInt("userid") != lp->entindex()) {
    CCSPlayer* victim = UTIL_PlayerByIndex(ev.GetInt("userid"));
    Vector p = victim ? victim->m_vecOrigin + Vector(0, 0, ev.GetInt("hitgroup") == HITGROUP_HEAD ? 66.f : 40.f) : Vector();
    if (!s_impacts.empty() && gpGlobals->realtime - s_impacts.back().time < 0.05f) p = s_impacts.back().p;
    s_dmg.push_back({ p, ev.GetInt("dmg_health"), (float)gpGlobals->realtime, ev.GetInt("hitgroup") == HITGROUP_HEAD });
    while (s_dmg.size() > 16) s_dmg.pop_front();
    s_lastHitPos = p; s_lastHitTime = (float)gpGlobals->realtime;
  }
}

// ------------------------------------------------------------------------------------------------
// chams
static int MaterialFlags() {
  switch (v.vis.chamsMaterial) { case 1: return STUDIO_FLAT_COLOR; case 2: return STUDIO_TINT | STUDIO_GLOW; case 3: return STUDIO_FLAT_COLOR | STUDIO_WIREFRAME; default: return STUDIO_TINT; }
}
static void DrawModelWithWeapon(const CStudioModel& mdl, const PlayerPoseOutput& pose, const CStudioModel* wpn, StudioDrawParams p) {
  p.pose = &pose.pose; p.modelToWorld = pose.modelToWorld;
  StudioRender_DrawModel(mdl, p);
  if (wpn && pose.hasWeapon) { StudioDrawParams q = p; q.pose = nullptr; q.modelToWorld = pose.weaponToWorld; StudioRender_DrawModel(*wpn, q); }
}

bool DrawPlayer(CCSPlayer* p, const CStudioModel& mdl, const PlayerPoseOutput& pose, const CStudioModel* wpn, const StudioLight& light, const CViewSetup& view) {
  CCSPlayer* lp = g_pLocalPlayer;
  auto& s = v.vis;
  StudioDrawParams base; base.light = light; base.eye = view.origin;
  if (p->IsLocalPlayer()) {
    // fake model (what other players see: client side feet yaw)
    if (s.chamsFake && v.aa.enabled && v.aa.bodyYaw) {
      float delta = AngleDiff(p->m_flFakeFootYaw, p->m_PlayerAnimState.m_flFootYaw);
      matrix3x4_t rot; AngleMatrix(QAngle(0, delta, 0), rot);
      Vector o = p->m_vecRenderOrigin;
      matrix3x4_t toOrigin, back; toOrigin.SetOrigin(o * -1.f); back.SetOrigin(o);
      matrix3x4_t t1, t2; ConcatTransforms(rot, toOrigin, t1); ConcatTransforms(back, t1, t2);
      PlayerPoseOutput fake = pose; ConcatTransforms(t2, pose.modelToWorld, fake.modelToWorld); ConcatTransforms(t2, pose.weaponToWorld, fake.weaponToWorld);
      StudioDrawParams fp = base; fp.flags = STUDIO_FLAT_COLOR; fp.colorOverride = s.chamsFakeCol;
      DrawModelWithWeapon(mdl, fake, nullptr, fp);
    }
    C_WeaponCSBase* w = p->GetActiveCSWeapon();
    bool scoped = w && w->IsZoomed();
    StudioDrawParams lpd = base;
    if (s.chamsLocal) { lpd.flags = MaterialFlags(); lpd.colorOverride = s.chamsLocalCol; }
    if (scoped && s.scopeBlend < 100) { lpd.flags |= STUDIO_TINT; if (!s.chamsLocal) lpd.colorOverride = MkCol(255, 255, 255); lpd.colorOverride.a = (unsigned char)(255 * s.scopeBlend / 100.f); }
    if (!s.chamsLocal && !(scoped && s.scopeBlend < 100)) return false;
    DrawModelWithWeapon(mdl, pose, wpn, lpd);
    return true;
  }
  bool enemy = lp && p->IsEnemy(lp);
  if (!enemy && !s.chamsTeam) return false;
  if (enemy && !s.chams && !s.chamsXqz && !s.glow && !s.chamsBacktrack) return false;
  // backtrack: oldest valid record
  if (enemy && s.chamsBacktrack && v.rage.backtrack && p->m_LagRecords.size() > 2) {
    const CNetPlayerState* old = nullptr;
    for (const CNetPlayerState& r : p->m_LagRecords) { if (r.simTime < ServerTime() - 0.19f) break; old = &r; }
    if (old && (old->origin - p->m_LagRecords.front().origin).Length() > 6.f) {
      PlayerPoseInput in; in.origin = old->origin; in.footYaw = p->m_bResolverActive ? p->m_flResolverFootYaw : p->m_flClientFootYaw; in.eyeAngles = old->eyeAngles;
      in.duckAmount = old->duckAmount; in.moveCycle = old->moveCycle; in.moveSpeed = old->moveSpeed; in.moveYaw = old->moveYaw; in.onGround = old->onGround; in.weaponId = 0;
      PlayerPoseOutput po; BuildPlayerPose(mdl, nullptr, in, po, false);
      StudioDrawParams bp = base; bp.flags = STUDIO_FLAT_COLOR; bp.colorOverride = s.chamsBacktrackCol;
      DrawModelWithWeapon(mdl, po, nullptr, bp);
    }
  }
  if (!enemy) {
    StudioDrawParams tp = base; tp.flags = MaterialFlags(); tp.colorOverride = s.chamsTeamCol;
    DrawModelWithWeapon(mdl, pose, wpn, tp);
    return true;
  }
  if (s.glow) { StudioDrawParams gp = base; gp.flags = STUDIO_FLAT_COLOR | STUDIO_NO_DEPTH | STUDIO_WIREFRAME; gp.colorOverride = s.glowCol; DrawModelWithWeapon(mdl, pose, nullptr, gp); }
  if (s.chamsXqz) { StudioDrawParams xp = base; xp.flags = MaterialFlags() | STUDIO_NO_DEPTH; if (xp.flags & STUDIO_TINT) xp.flags = (xp.flags & ~STUDIO_TINT) | STUDIO_FLAT_COLOR; xp.colorOverride = s.chamsXqzCol; DrawModelWithWeapon(mdl, pose, wpn, xp); }
  if (s.chams) { StudioDrawParams vp = base; vp.flags = MaterialFlags(); vp.colorOverride = s.chamsCol; DrawModelWithWeapon(mdl, pose, wpn, vp); }
  else DrawModelWithWeapon(mdl, pose, wpn, base);
  return true;
}

// ------------------------------------------------------------------------------------------------
// 3D
static void DrawCapsuleLines(const mstudiobbox_t& b, Color c) {
  DrawCapsuleWires({ b.a.x, b.a.y, b.a.z }, { b.b.x, b.b.y, b.b.z }, b.radius, 8, 3, c);
}

void Visuals_DrawWorld3D(const CViewSetup& view) {
  (void)view;
  float now = (float)gpGlobals->realtime;
  auto& s = v.vis;
  if (s.tracers) {
    for (auto it = s_tracers.begin(); it != s_tracers.end();) {
      float age = now - it->time; if (age > 2.5f) { it = s_tracers.erase(it); continue; }
      float a = age < 2.f ? 1.f : (2.5f - age) / 0.5f;
      Col c = s.tracerCol.Alpha((int)(s.tracerCol.a * a));
      Vector st = it->a + (it->b - it->a).Normalized() * 8.f;
      DrawLine3D({ st.x, st.y, st.z }, { it->b.x, it->b.y, it->b.z }, c);
      DrawLine3D({ st.x, st.y, st.z + 0.4f }, { it->b.x, it->b.y, it->b.z + 0.4f }, c.Alpha(c.a / 3));
      ++it;
    }
  }
  if (s.impacts) {
    for (auto& im : s_impacts) {
      float age = now - im.time; if (age > 4.f) continue;
      unsigned char a = (unsigned char)(age < 3.5f ? 160 : 160 * (4.f - age) / 0.5f);
      DrawCubeWires({ im.p.x, im.p.y, im.p.z }, 4, 4, 4, { 40, 90, 255, a });
    }
  }
  if (s.hitboxOnShot) {
    for (auto& sh : s_shotHb) {
      float age = now - sh.time; if (age > 3.f) continue;
      Col c = s.hitboxCol.Alpha((int)(s.hitboxCol.a * (age < 2.5f ? 1.f : (3.f - age) / 0.5f)));
      rlDrawRenderBatchActive(); rlDisableDepthTest();
      for (int i = 0; i < 19; i++) DrawCapsuleLines(sh.hb[i], i == sh.hitbox ? (Color)MkCol(255, 60, 60, c.a) : (Color)c);
      rlDrawRenderBatchActive(); rlEnableDepthTest();
    }
  }
  // grenade trajectory
  CCSPlayer* lp = g_pLocalPlayer;
  if (s.grenadePrediction && lp && lp->IsAlive()) {
    C_WeaponCSBase* w = lp->GetActiveCSWeapon();
    if (w && IsGrenade(w->GetCSWeaponID())) {
      CBaseCSGrenade* gr = (CBaseCSGrenade*)w;
      bool holding = gr->m_bPinPulled || IsMouseButtonDown(MOUSE_BUTTON_LEFT) || IsMouseButtonDown(MOUSE_BUTTON_RIGHT);
      if (holding) {
        float strength = IsMouseButtonDown(MOUSE_BUTTON_LEFT) && IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ? 0.5f : IsMouseButtonDown(MOUSE_BUTTON_RIGHT) ? 0.f : 1.f;
        if (gr->m_bPinPulled) strength = gr->m_flThrowStrength;
        Vector src, vel; GetGrenadeThrowParams(lp, cl.viewangles, strength, src, vel);
        GrenadePath path; PredictGrenadePath(w->GetCSWeaponID(), src, vel, path, lp);
        rlDrawRenderBatchActive(); rlDisableDepthTest();
        for (size_t i = 1; i < path.points.size(); i++) { const Vector &a = path.points[i - 1], &b = path.points[i]; DrawLine3D({ a.x, a.y, a.z }, { b.x, b.y, b.z }, s.grenadeCol); }
        for (const Vector& b : path.bounces) DrawCubeWires({ b.x, b.y, b.z }, 3, 3, 3, { 255, 255, 255, 200 });
        const Vector& d = path.detonate;
        float r = w->GetCSWeaponID() == WEAPON_HEGRENADE ? 350.f * 0.4f : w->GetCSWeaponID() == WEAPON_SMOKEGRENADE ? 144.f : 40.f;
        for (int i = 0; i < 32; i++) { float a0 = i * 2 * M_PI_F / 32, a1 = (i + 1) * 2 * M_PI_F / 32; DrawLine3D({ d.x + std::cos(a0) * r, d.y + std::sin(a0) * r, d.z + 2 }, { d.x + std::cos(a1) * r, d.y + std::sin(a1) * r, d.z + 2 }, Col(s.grenadeCol).Alpha(160)); }
        rlDrawRenderBatchActive(); rlEnableDepthTest();
      }
    }
  }
}

// ------------------------------------------------------------------------------------------------
// 2D helpers
static bool W2S(const Vector& p, Vector2D& out) { return cl.view.WorldToScreen(p, out); }
static void Txt(int font, const char* t, float x, float y, Col c) { surface()->DrawTextOutline(font, t, x, y, c, { 0, 0, 0, (unsigned char)(c.a * 0.8f) }); }
static void TxtC(int font, const char* t, float cx, float y, Col c) { Txt(font, t, cx - surface()->GetTextSize(font, t).x * 0.5f, y, c); }

static bool GetBox(CCSPlayer* p, float& x, float& y, float& w, float& h) {
  Vector o = p->m_bRenderPoseValid ? p->m_vecRenderOrigin : p->m_vecOrigin;
  float duck = p->m_LagRecords.empty() ? p->m_flDuckAmount : p->m_LagRecords.front().duckAmount;
  float top = 72.f - 18.f * duck + 4.f;
  float minx = 1e9f, miny = 1e9f, maxx = -1e9f, maxy = -1e9f;
  for (int i = 0; i < 8; i++) {
    Vector c = o + Vector((i & 1) ? 16.f : -16.f, (i & 2) ? 16.f : -16.f, (i & 4) ? top : 0.f);
    Vector2D s; if (!W2S(c, s)) return false;
    minx = std::min(minx, s.x); maxx = std::max(maxx, s.x); miny = std::min(miny, s.y); maxy = std::max(maxy, s.y);
  }
  // narrow the box like CS:GO cheats (height based)
  float hh = maxy - miny, ww = hh * 0.5f, cx = (minx + maxx) * 0.5f;
  x = std::floor(cx - ww * 0.5f); y = std::floor(miny); w = std::floor(ww); h = std::floor(hh);
  return w > 1 && h > 1;
}

static const int s_skel[][2] = { { HITBOX_HEAD, HITBOX_NECK }, { HITBOX_NECK, HITBOX_UPPER_CHEST }, { HITBOX_UPPER_CHEST, HITBOX_CHEST }, { HITBOX_CHEST, HITBOX_LOWER_CHEST },
  { HITBOX_LOWER_CHEST, HITBOX_STOMACH }, { HITBOX_STOMACH, HITBOX_PELVIS }, { HITBOX_PELVIS, HITBOX_LEFT_THIGH }, { HITBOX_LEFT_THIGH, HITBOX_LEFT_CALF }, { HITBOX_LEFT_CALF, HITBOX_LEFT_FOOT },
  { HITBOX_PELVIS, HITBOX_RIGHT_THIGH }, { HITBOX_RIGHT_THIGH, HITBOX_RIGHT_CALF }, { HITBOX_RIGHT_CALF, HITBOX_RIGHT_FOOT }, { HITBOX_UPPER_CHEST, HITBOX_LEFT_UPPER_ARM },
  { HITBOX_LEFT_UPPER_ARM, HITBOX_LEFT_FOREARM }, { HITBOX_LEFT_FOREARM, HITBOX_LEFT_HAND }, { HITBOX_UPPER_CHEST, HITBOX_RIGHT_UPPER_ARM }, { HITBOX_RIGHT_UPPER_ARM, HITBOX_RIGHT_FOREARM },
  { HITBOX_RIGHT_FOREARM, HITBOX_RIGHT_HAND } };

static bool BreakingLC(CCSPlayer* p) {
  if (p->m_LagRecords.size() < 2) return false;
  return (p->m_LagRecords[0].origin - p->m_LagRecords[1].origin).Length2DSqr() > 64.f * 64.f;
}

static void DrawPlayerESP(CCSPlayer* lp, CCSPlayer* p) {
  auto& s = v.vis;
  float x, y, w, h;
  bool onScreen = GetBox(p, x, y, w, h);
  int sw = GetScreenWidth(), sh = GetScreenHeight();
  if (onScreen && (x + w < 0 || x > sw || y + h < 0 || y > sh)) onScreen = false;
  if (!onScreen) {
    if (s.oof && p->IsEnemy(lp)) {
      QAngle a = CalcAngle(cl.view.origin, p->m_vecOrigin);
      float yaw = DEG2RADF(cl.view.angles.y - a.y - 90.f);
      float r = s.oofRadius;
      Vector2 c{ sw * 0.5f, sh * 0.5f };
      Vector2 tip{ c.x + std::cos(yaw) * r, c.y + std::sin(yaw) * r };
      Vector2 l{ c.x + std::cos(yaw - 0.08f) * (r - 18), c.y + std::sin(yaw - 0.08f) * (r - 18) };
      Vector2 rr{ c.x + std::cos(yaw + 0.08f) * (r - 18), c.y + std::sin(yaw + 0.08f) * (r - 18) };
      float pulse = 0.55f + 0.45f * std::sin((float)GetTime() * 5.f);
      Col oc = s.oofCol.Alpha((int)(s.oofCol.a * pulse));
      surface()->DrawTriangle(tip, rr, l, oc); surface()->DrawTriangle(tip, l, rr, oc);
    }
    return;
  }
  float alpha = 1.f;
  int a255 = (int)(255 * alpha);
  if (s.box) {
    surface()->DrawOutlinedRect(x - 1, y - 1, w + 2, h + 2, { 0, 0, 0, (unsigned char)(170 * alpha) });
    surface()->DrawOutlinedRect(x + 1, y + 1, w - 2, h - 2, { 0, 0, 0, (unsigned char)(170 * alpha) });
    surface()->DrawOutlinedRect(x, y, w, h, s.boxCol.Alpha((int)(s.boxCol.a * alpha)));
  }
  if (s.name) TxtC(FONT_ESP, p->GetPlayerName(), x + w * 0.5f, y - 13, s.nameCol.Alpha(a255));
  if (s.health) {
    float hp = Clamp(p->m_iHealth / 100.f, 0.f, 1.f);
    surface()->DrawFilledRect(x - 6, y - 1, 4, h + 2, { 0, 0, 0, (unsigned char)(150 * alpha) });
    Color hc = { (unsigned char)(255 * std::min(1.f, 2 * (1 - hp))), (unsigned char)(255 * std::min(1.f, 2 * hp)), 40, (unsigned char)a255 };
    surface()->DrawFilledRect(x - 5, y + h * (1 - hp), 2, h * hp, hc);
    if (p->m_iHealth < 100) Txt(FONT_ICONS_SMALL, TextFormat("%d", p->m_iHealth), x - 10, y + h * (1 - hp) - 5, MkCol(255, 255, 255, a255));
  }
  float by = y + h + 2;
  C_WeaponCSBase* wpn = p->GetActiveCSWeapon();
  if (s.ammo && wpn && wpn->IsGun() && wpn->GetCSWpnData().clipSize > 0) {
    float frac = Clamp((float)wpn->m_iClip1 / wpn->GetCSWpnData().clipSize, 0.f, 1.f);
    if (wpn->IsReloading()) frac = Clamp((ServerTime() - wpn->m_flReloadStartTime) / std::max(0.01f, wpn->m_flReloadFinishTime - wpn->m_flReloadStartTime), 0.f, 1.f);
    surface()->DrawFilledRect(x - 1, by, w + 2, 4, { 0, 0, 0, (unsigned char)(150 * alpha) });
    surface()->DrawFilledRect(x, by + 1, w * frac, 2, s.ammoCol.Alpha(a255));
    by += 5;
  }
  if (s.weapon && wpn) {
    std::string n = wpn->GetCSWpnData().printName; for (auto& ch : n) ch = (char)toupper(ch);
    TxtC(FONT_ICONS_SMALL, n.c_str(), x + w * 0.5f, by, MkCol(255, 255, 255, a255));
  }
  if (s.flags) {
    float fy = y - 1; auto flag = [&](const char* t, Col c) { Txt(FONT_ICONS_SMALL, t, x + w + 3, fy, c.Alpha(a255)); fy += 9; };
    if (p->m_ArmorValue > 0) flag(p->m_bHasHelmet ? "HK" : "K", MkCol(255, 255, 255));
    flag(TextFormat("$%d", p->m_iAccount), MkCol(130, 200, 60));
    if (p->m_bIsScoped) flag("ZOOM", MkCol(80, 160, 255));
    if (p->IsBlind()) flag("FLASHED", MkCol(255, 230, 80));
    if (p->m_LastCmd.bFakeDuck) flag("FD", MkCol(255, 255, 255));
    if (BreakingLC(p)) flag("LC", MkCol(255, 60, 60));
    if (p->m_bIsDefusing) flag("DEFUSE", MkCol(80, 160, 255));
    if (p->HasC4()) flag("BOMB", MkCol(255, 60, 60));
    if (p->IsEnemy(lp) && p->m_bResolverActive) flag(p->m_iResolverSide < 0 ? "L" : p->m_iResolverSide > 0 ? "R" : "C", MkCol(200, 200, 200));
    if (p->IsEnemy(lp) && p->m_iMissedShots > 0) flag(TextFormat("MISS %d", p->m_iMissedShots), MkCol(255, 120, 120));
    if (!p->m_LagRecords.empty() && p->m_LagRecords.front().chokedTicks > 3) flag(TextFormat("FL %d", p->m_LagRecords.front().chokedTicks), MkCol(200, 200, 200));
  }
  if (s.skeleton && p->m_bRenderPoseValid) {
    const HitboxSet& hb = p->m_RenderPose.hitboxes;
    for (auto& b : s_skel) {
      Vector a = (hb[b[0]].a + hb[b[0]].b) * 0.5f, c = (hb[b[1]].a + hb[b[1]].b) * 0.5f;
      Vector2D sa, sc; if (W2S(a, sa) && W2S(c, sc)) surface()->DrawLine(sa.x, sa.y, sc.x, sc.y, s.skeletonCol.Alpha(s.skeletonCol.a * alpha));
    }
  }
}

static void DrawWorldESP() {
  auto& s = v.vis;
  for (C_BaseEntity* e : g_EntityList.All()) {
    if (!e || e->m_bMarkedForDeletion) continue;
    Vector2D sp;
    if (e->m_iEntClass == ENTCLASS_WEAPON && s.droppedWeapons) {
      C_WeaponCSBase* w = (C_WeaponCSBase*)e;
      if (w->m_hOwner || !W2S(e->m_vecOrigin, sp)) continue;
      std::string n = w->GetCSWpnData().printName; for (auto& ch : n) ch = (char)toupper(ch);
      TxtC(FONT_ICONS_SMALL, n.c_str(), sp.x, sp.y, MkCol(255, 255, 255, 200));
    } else if (e->m_iEntClass == ENTCLASS_GRENADE_PROJECTILE && s.grenadeEsp) {
      CBaseCSGrenadeProjectile* gp = (CBaseCSGrenadeProjectile*)e;
      if (gp->m_bDetonated && gp->m_iWeaponID != WEAPON_SMOKEGRENADE) continue;
      if (!W2S(e->m_vecOrigin, sp)) continue;
      const char* n = gp->m_iWeaponID == WEAPON_HEGRENADE ? "HE" : gp->m_iWeaponID == WEAPON_FLASHBANG ? "FLASH" : "SMOKE";
      Col c = gp->m_iWeaponID == WEAPON_HEGRENADE ? MkCol(255, 80, 80) : gp->m_iWeaponID == WEAPON_FLASHBANG ? MkCol(255, 255, 120) : MkCol(180, 180, 180);
      TxtC(FONT_ICONS_SMALL, n, sp.x, sp.y - 4, c);
    } else if (e->m_iEntClass == ENTCLASS_PLANTED_C4 && s.bombEsp) {
      CPlantedC4* c4 = (CPlantedC4*)e;
      if (!c4->m_bBombTicking || !W2S(e->m_vecOrigin + Vector(0, 0, 8), sp)) continue;
      float left = std::max(0.f, c4->m_flC4Blow - ServerTime());
      TxtC(FONT_CHEAT_BOLD, TextFormat("BOMB %c  %.1fs", c4->m_nBombSite == 1 ? 'B' : 'A', left), sp.x, sp.y, left < 10 ? MkCol(255, 60, 60) : MkCol(255, 255, 255));
    }
  }
  // smoke timers
  if (s.grenadeEsp) for (const SmokeVolume& sv : GetActiveSmokes()) {
    Vector2D sp; if (!W2S(sv.center + Vector(0, 0, 40), sp)) continue;
    float left = sv.end - ServerTime(), tot = sv.end - sv.start;
    surface()->DrawFilledRect(sp.x - 20, sp.y, 40, 4, { 0, 0, 0, 150 });
    surface()->DrawFilledRect(sp.x - 19, sp.y + 1, 38 * Clamp(left / tot, 0.f, 1.f), 2, { 200, 200, 200, 220 });
  }
}

// ------------------------------------------------------------------------------------------------
static void DrawIndicators(CCSPlayer* lp) {
  float y = GetScreenHeight() * 0.62f, x = 12;
  auto ind = [&](const char* t, Col c) {
    Vector2 sz = surface()->GetTextSize(FONT_CHEAT_INDICATOR, t);
    surface()->DrawGradientH(x - 6, y - 3, sz.x * 0.6f + 6, sz.y + 6, { 0, 0, 0, 0 }, { 0, 0, 0, 90 });
    surface()->DrawGradientH(x + sz.x * 0.6f, y - 3, sz.x * 0.6f, sz.y + 6, { 0, 0, 0, 90 }, { 0, 0, 0, 0 });
    surface()->DrawTextShadow(FONT_CHEAT_INDICATOR, t, x, y, c, { 0, 0, 0, 200 });
    y += sz.y + 8;
  };
  Col green = MkCol(150, 200, 60), red = MkCol(255, 0, 0), white = MkCol(230, 230, 230);
  if (g.doubletapping) ind("DT", g.dtReady ? white : red);
  if (g.hideshots) ind("ONSHOT", white);
  if (g.fakeducking) ind("DUCK", white);
  if (g.freestandingActive || (v.aa.freestandKey.key && v.aa.freestandKey.Active())) ind("FS", white);
  if (v.rage.forceSafeKey.key && v.rage.forceSafeKey.Active()) ind("SAFE", white);
  if (v.rage.forceBaimKey.key && v.rage.forceBaimKey.Active()) ind("BAIM", white);
  if (v.rage.minDamageOverrideKey.key && v.rage.minDamageOverrideKey.Active()) ind(TextFormat("DMG: %d", (int)v.rage.minDamageOverride), white);
  if (v.aa.fakelag && lp->m_vecVelocity.Length2D() > 200 && !g.doubletapping) ind("LC", BreakingLC(lp) ? green : red);
  if (v.aa.enabled && v.aa.bodyYaw) {
    float desync = std::fabs(AngleDiff(lp->m_flFakeFootYaw, lp->m_PlayerAnimState.m_flFootYaw));
    float t = Clamp(desync / 58.f, 0.f, 1.f);
    ind("FAKE", MkCol((int)(255 - 105 * t), (int)(200 * t + 55 * (1 - t) * 0.2f), (int)(60 * t)));
  }
}

static void DrawArrows() {
  float cx = GetScreenWidth() * 0.5f, cy = GetScreenHeight() * 0.5f, d = 48;
  Col on = v.vis.arrowCol, off = MkCol(30, 30, 30, 150);
  surface()->DrawTriangle({ cx - d - 14, cy }, { cx - d, cy + 9 }, { cx - d, cy - 9 }, g.manualSide == -1 ? on : off);
  surface()->DrawTriangle({ cx + d + 14, cy }, { cx + d, cy - 9 }, { cx + d, cy + 9 }, g.manualSide == 1 ? on : off);
  if (g.manualSide == 2) surface()->DrawTriangle({ cx, cy + d + 14 }, { cx + 9, cy + d }, { cx - 9, cy + d }, on);
  // desync side bars
  surface()->DrawFilledRect(cx - d + 2, cy - 9, 2, 18, g.desyncSide < 0 ? on : off);
  surface()->DrawFilledRect(cx + d - 4, cy - 9, 2, 18, g.desyncSide > 0 ? on : off);
}

static void DrawWatermark(CCSPlayer* lp) {
  time_t tt = time(nullptr); struct tm* lt = localtime(&tt);
  char buf[256];
  snprintf(buf, sizeof(buf), "hvh\x01sense | %s | delay: %dms | %dtick | %02d:%02d:%02d", lp ? lp->GetPlayerName() : "user", lp ? lp->m_iPing : 0, (int)std::round(1.f / gpGlobals->interval_per_tick), lt->tm_hour, lt->tm_min, lt->tm_sec);
  std::string full = buf; size_t p = full.find('\x01');
  std::string a = full.substr(0, p), b = full.substr(p + 1);
  float wa = surface()->GetTextSize(FONT_CHEAT, a.c_str()).x, wb = surface()->GetTextSize(FONT_CHEAT, b.c_str()).x;
  float w = wa + wb + 16, h = 20, x = GetScreenWidth() - w - 10, y = 10;
  surface()->DrawFilledRect(x - 4, y - 4, w + 8, h + 8, { 12, 12, 12, 230 });
  surface()->DrawOutlinedRect(x - 4, y - 4, w + 8, h + 8, { 0, 0, 0, 255 }); surface()->DrawOutlinedRect(x - 3, y - 3, w + 6, h + 6, { 60, 60, 60, 255 });
  surface()->DrawGradientH(x, y - 2, w / 2, 1, { 55, 177, 218, 255 }, { 204, 70, 205, 255 });
  surface()->DrawGradientH(x + w / 2, y - 2, w / 2, 1, { 204, 70, 205, 255 }, { 204, 227, 53, 255 });
  surface()->DrawText(FONT_CHEAT, a.c_str(), x + 8, y + 4, { 230, 230, 230, 255 });
  surface()->DrawText(FONT_CHEAT, b.c_str(), x + 8 + wa, y + 4, v.misc.accent);
}

static void DrawListBox(const char* title, float x, float y, const std::vector<std::pair<std::string, std::string>>& rows) {
  float w = 180, h = 20 + rows.size() * 15.f;
  surface()->DrawFilledRect(x, y, w, h + 4, { 12, 12, 12, 200 });
  surface()->DrawGradientH(x, y, w / 2, 2, { 55, 177, 218, 255 }, { 204, 70, 205, 255 });
  surface()->DrawGradientH(x + w / 2, y, w / 2, 2, { 204, 70, 205, 255 }, { 204, 227, 53, 255 });
  surface()->DrawTextCentered(FONT_CHEAT_BOLD, title, x + w / 2, y + 5, { 230, 230, 230, 255 });
  float ry = y + 22;
  for (auto& r : rows) {
    surface()->DrawText(FONT_CHEAT, r.first.c_str(), x + 6, ry, { 220, 220, 220, 255 });
    surface()->DrawTextRight(FONT_CHEAT, r.second.c_str(), x + w - 6, ry, { 150, 150, 150, 255 });
    ry += 15;
  }
}

void Visuals_Paint() {
  CCSPlayer* lp = g_pLocalPlayer;
  auto& s = v.vis;
  if (lp && CL_IsInGame()) {
    for (CCSPlayer* p : UTIL_GetPlayers(true)) {
      if (p == lp || p->m_iTeamNum < TEAM_TERRORIST) continue;
      if (!s.teammates && !p->IsEnemy(lp)) continue;
      DrawPlayerESP(lp, p);
    }
    DrawWorldESP();
    float now = (float)gpGlobals->realtime;
    // damage markers
    if (s.damageMarker) for (auto& d : s_dmg) {
      float age = now - d.time; if (age > 1.6f) continue;
      Vector2D sp; if (!W2S(d.p + Vector(0, 0, age * 30.f), sp)) continue;
      Col c = d.head ? MkCol(255, 60, 60) : MkCol(255, 255, 255);
      TxtC(FONT_CHEAT_BOLD, TextFormat("-%d", d.dmg), sp.x, sp.y, c.Alpha((int)(255 * std::min(1.f, (1.6f - age) * 2))));
    }
    // 3D hit marker
    if (s.hitmarker3d && now - s_lastHitTime < 0.8f) {
      Vector2D sp; if (W2S(s_lastHitPos, sp)) {
        unsigned char a = (unsigned char)(255 * (0.8f - (now - s_lastHitTime)) / 0.8f);
        for (int i = 0; i < 4; i++) { float sx = (i & 1) ? 1.f : -1.f, sy = (i & 2) ? 1.f : -1.f; surface()->DrawLine(sp.x + sx * 3, sp.y + sy * 3, sp.x + sx * 8, sp.y + sy * 8, { 255, 255, 255, a }, 1.5f); }
      }
    }
    if (lp->IsAlive()) {
      C_WeaponCSBase* w = lp->GetActiveCSWeapon();
      float cx = GetScreenWidth() * 0.5f, cy = GetScreenHeight() * 0.5f;
      // scope lines instead of the overlay
      if (s.removeScope && w && w->IsZoomed() && IsSniper(w->GetCSWeaponID())) {
        surface()->DrawFilledRect(0, cy, (float)GetScreenWidth(), 1, { 0, 0, 0, 255 });
        surface()->DrawFilledRect(cx, 0, 1, (float)GetScreenHeight(), { 0, 0, 0, 255 });
      }
      // penetration reticle
      if (s.penCrosshair && w && w->IsGun()) {
        Vector eye = lp->EyePosition(), fwd; AngleVectors(cl.viewangles, &fwd);
        trace_t tr; UTIL_TraceLine(eye, eye + fwd * 4096.f, MASK_SHOT, lp, &tr);
        bool pen = false;
        if (tr.fraction < 1.f && !tr.m_pEnt) pen = CS_SimulateBulletDamage(lp, eye, tr.endpos + fwd * 40.f, w->GetCSWeaponID(), nullptr, HITGROUP_CHEST) > 1.f;
        surface()->DrawFilledRect(cx - 1, cy - 1, 3, 3, pen ? Color{ 80, 255, 80, 230 } : Color{ 255, 60, 60, 180 });
      }
      if (s.spreadCircle && w && w->IsGun()) {
        float sp = w->GetInaccuracy() + w->GetSpread();
        float r = sp * (GetScreenHeight() * 0.5f) / std::tan(DEG2RADF(cl.view.VerticalFov(cl.view.fov) * 0.5f));
        surface()->DrawFilledCircle(cx, cy, r, { 0, 0, 0, 40 }); surface()->DrawCircle(cx, cy, r, { 255, 255, 255, 60 }, 48);
      }
      if (s.indicators) DrawIndicators(lp);
      if (s.aaArrows && v.aa.enabled) DrawArrows();
    }
    // hotkey list
    float ly = GetScreenHeight() * 0.42f;
    if (s.keybindList) {
      std::vector<std::pair<std::string, std::string>> rows;
      for (auto& nb : g_NamedBinds) if (nb.bind->key && nb.bind->mode != KEYMODE_ALWAYS && nb.bind->Active()) rows.push_back({ nb.name, nb.bind->mode == KEYMODE_HOLD ? "[holding]" : nb.bind->mode == KEYMODE_TOGGLE ? "[toggled]" : "[off]" });
      if (!rows.empty() || g.menuOpen) { DrawListBox("keybinds", GetScreenWidth() - 200.f, ly, rows); ly += 30 + rows.size() * 15; }
    }
    if (s.spectatorList) {
      std::vector<std::pair<std::string, std::string>> rows;
      for (CCSPlayer* p : UTIL_GetPlayers()) if (!p->IsAlive() && p != lp && p->m_hObserverTarget == lp) rows.push_back({ p->GetPlayerName(), p->m_iObserverMode == OBS_MODE_IN_EYE ? "1st" : "3rd" });
      if (!rows.empty()) DrawListBox("spectators", GetScreenWidth() - 200.f, ly, rows);
    }
  }
  if (s.watermark) DrawWatermark(lp);
  DrawLogs();
}

} // namespace cheat
