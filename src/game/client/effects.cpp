// effects.cpp - client effects: tracers, impacts/decals, muzzle flashes, explosions, blood, smoke clouds
#include "cl_main.hpp"
#include "../../engine/materialsystem.hpp"
#include "../../engine/random.hpp"
#include "../../engine/enginetrace.hpp"
#include "../shared/cs_player.hpp"
#include "rlgl.h"
#include <vector>
#include <deque>
#include <cmath>
#include <algorithm>

ConVar r_decals("r_decals", "256", FCVAR_ARCHIVE, "Maximum number of bullet hole decals");
ConVar cl_tracers("cl_tracers", "1", FCVAR_ARCHIVE, "Draw bullet tracers");
ConVar r_drawparticles("r_drawparticles", "1", FCVAR_CHEAT, "Enable/disable particle rendering");
ConVar cl_showimpacts("sv_showimpacts", "0", FCVAR_REPLICATED, "Shows client (red) and server (blue) bullet impact point (1=both, 2=client-only, 3=server-only)");

struct Particle {
  Vector pos, vel;
  float size, sizeVel;
  float life, maxLife;
  Color color;
  float gravity = 0, drag = 0;
  int tex = 0;          // 0 smoke 1 spark/flare 2 glow 3 cloud
  bool additive = false;
  float rot = 0, rotVel = 0;
  bool stretch = false; // sparks: stretched along velocity
};
struct Tracer { Vector a, b; float life, maxLife; Color c; float width; bool beam; float speed; };
struct Decal { Vector pos, normal; float size; float time; bool player; };
struct DebugBox { Vector mins, maxs; Color c; float life; };
struct Capsules { HitboxSet hb; Color c; float life, maxLife; };

static std::vector<Particle> s_particles;
static std::vector<Tracer> s_tracers;
static std::deque<Decal> s_decals;
static std::vector<DebugBox> s_boxes;
static std::vector<Capsules> s_capsules;
struct SmokeCloud { Vector center; float start, duration; std::vector<Vector> puffs; std::vector<float> sizes; std::vector<float> shade; };
static std::vector<SmokeCloud> s_smokes;

void FX_Init() {}
void FX_Clear() { s_particles.clear(); s_tracers.clear(); s_decals.clear(); s_boxes.clear(); s_capsules.clear(); s_smokes.clear(); }

static void AddParticle(const Particle& p) { if (s_particles.size() < 4000) s_particles.push_back(p); }

void FX_Tracer(const Vector& start, const Vector& end, Color c, float width, float life) {
  if (!cl_tracers.GetBool()) return;
  Tracer t{ start, end, life, life, c, width, false, 6000.f };
  s_tracers.push_back(t);
}
void FX_BeamTracer(const Vector& start, const Vector& end, Color c, float life) { s_tracers.push_back({ start, end, life, life, c, 1.6f, true, 0 }); }

void FX_Impact(const Vector& pos, const Vector& normal, int surfaceProp, bool player) {
  if (player) { FX_Blood(pos, normal); return; }
  // decal
  s_decals.push_back({ pos + normal * 0.15f, normal, RandomFloat(1.6f, 2.4f), gpGlobals->realtime, false });
  while ((int)s_decals.size() > r_decals.GetInt()) s_decals.pop_front();
  if (!r_drawparticles.GetBool()) return;
  const surfacedata_t* sd = GetSurfaceData(surfaceProp);
  char gm = sd ? sd->gameMaterial : CHAR_TEX_CONCRETE;
  bool metal = gm == CHAR_TEX_METAL || gm == CHAR_TEX_GRATE || gm == CHAR_TEX_VENT;
  Color dust = { 200, 185, 160, 200 };
  if (gm == CHAR_TEX_SAND || gm == CHAR_TEX_DIRT) dust = { 210, 180, 130, 210 };
  else if (gm == CHAR_TEX_WOOD) dust = { 150, 110, 70, 210 };
  else if (gm == CHAR_TEX_CONCRETE || gm == CHAR_TEX_TILE) dust = { 170, 170, 165, 210 };
  for (int i = 0; i < 4; i++) {
    Particle p; p.pos = pos + normal * 2; p.vel = normal * RandomFloat(30, 90) + Vector(RandomFloat(-25, 25), RandomFloat(-25, 25), RandomFloat(0, 30));
    p.size = RandomFloat(3, 6); p.sizeVel = RandomFloat(10, 18); p.life = p.maxLife = RandomFloat(0.5f, 1.0f); p.color = dust; p.drag = 2.5f; p.gravity = 0.1f;
    p.tex = 0; p.rot = RandomFloat(0, 360); p.rotVel = RandomFloat(-60, 60);
    AddParticle(p);
  }
  if (metal || RandomInt(0, 3) == 0) FX_Spark(pos + normal, metal ? 8 : 3);
}

void FX_Spark(const Vector& pos, int count) {
  for (int i = 0; i < count; i++) {
    Particle p; p.pos = pos; p.vel = Vector(RandomFloat(-1, 1), RandomFloat(-1, 1), RandomFloat(-0.2f, 1.2f)).Normalized() * RandomFloat(120, 320);
    p.size = RandomFloat(0.6f, 1.2f); p.sizeVel = 0; p.life = p.maxLife = RandomFloat(0.15f, 0.4f); p.color = { 255, 210, 120, 255 };
    p.gravity = 1.f; p.drag = 1.f; p.tex = 1; p.additive = true; p.stretch = true;
    AddParticle(p);
  }
}

void FX_MuzzleFlash(const Vector& pos, const Vector& dir, float scale) {
  if (!r_drawparticles.GetBool()) return;
  for (int i = 0; i < 3; i++) {
    Particle p; p.pos = pos + dir * (i * 2.5f * scale); p.vel = dir * 40.f; p.size = (5.f - i) * scale; p.sizeVel = 30 * scale;
    p.life = p.maxLife = 0.05f; p.color = { 255, (unsigned char)(200 - i * 30), 120, 255 }; p.tex = 2; p.additive = true; p.rot = RandomFloat(0, 360);
    AddParticle(p);
  }
  Particle s; s.pos = pos + dir * 3; s.vel = dir * 30 + Vector(0, 0, 12); s.size = 2 * scale; s.sizeVel = 12; s.life = s.maxLife = 0.5f; s.color = { 200, 200, 200, 70 }; s.drag = 1.5f; s.tex = 0;
  AddParticle(s);
}

void FX_Explosion(const Vector& pos) {
  for (int i = 0; i < 26; i++) {
    Particle p; p.pos = pos + Vector(0, 0, 16); p.vel = Vector(RandomFloat(-1, 1), RandomFloat(-1, 1), RandomFloat(0, 1.2f)).Normalized() * RandomFloat(80, 380);
    p.size = RandomFloat(20, 40); p.sizeVel = 70; p.life = p.maxLife = RandomFloat(0.3f, 0.6f); p.color = { 255, (unsigned char)RandomInt(120, 200), 40, 255 };
    p.drag = 4.f; p.tex = 2; p.additive = true; p.rot = RandomFloat(0, 360); p.rotVel = RandomFloat(-90, 90);
    AddParticle(p);
  }
  for (int i = 0; i < 20; i++) {
    Particle p; p.pos = pos + Vector(RandomFloat(-30, 30), RandomFloat(-30, 30), RandomFloat(10, 50)); p.vel = Vector(RandomFloat(-1, 1), RandomFloat(-1, 1), RandomFloat(0.2f, 1.f)).Normalized() * RandomFloat(40, 160);
    p.size = RandomFloat(30, 50); p.sizeVel = 40; p.life = p.maxLife = RandomFloat(2.f, 3.5f); unsigned char g = (unsigned char)RandomInt(40, 80); p.color = { g, g, g, 200 };
    p.drag = 1.5f; p.gravity = -0.05f; p.tex = 0; p.rot = RandomFloat(0, 360); p.rotVel = RandomFloat(-30, 30);
    AddParticle(p);
  }
  FX_Spark(pos + Vector(0, 0, 10), 30);
  s_decals.push_back({ pos + Vector(0, 0, 0.3f), Vector(0, 0, 1), 60.f, gpGlobals->realtime, false });
}

void FX_Blood(const Vector& pos, const Vector& dir) {
  if (!r_drawparticles.GetBool()) return;
  for (int i = 0; i < 6; i++) {
    Particle p; p.pos = pos; p.vel = (dir * RandomFloat(20, 80)) + Vector(RandomFloat(-30, 30), RandomFloat(-30, 30), RandomFloat(-10, 40));
    p.size = RandomFloat(2, 4.5f); p.sizeVel = RandomFloat(4, 10); p.life = p.maxLife = RandomFloat(0.3f, 0.6f); p.color = { 140, 10, 10, 230 }; p.gravity = 0.6f; p.drag = 1.f; p.tex = 0;
    AddParticle(p);
  }
}

void FX_SmokeCloud(const Vector& center, float start, float duration) {
  SmokeCloud c; c.center = center; c.start = start; c.duration = duration;
  uint32_t seed = (uint32_t)(center.x * 13 + center.y * 7);
  CUniformRandomStream rs; rs.SetSeed((int)seed);
  for (int i = 0; i < 46; i++) {
    // fill a flattened sphere, push puffs out of walls
    Vector off(rs.RandomFloat(-1, 1), rs.RandomFloat(-1, 1), rs.RandomFloat(-0.2f, 0.9f));
    if (off.Length() > 1) off = off.Normalized();
    Vector p = center + Vector(0, 0, 40) + Vector(off.x * 130, off.y * 130, off.z * 80);
    trace_t tr; Ray_t ray; ray.Init(center + Vector(0, 0, 40), p); g_EngineTrace.TraceRayWorld(ray, MASK_PLAYERSOLID_BRUSHONLY, &tr);
    p = tr.endpos - (p - center).Normalized() * 20.f;
    if (p.z < center.z + 20) p.z = center.z + 20;
    c.puffs.push_back(p); c.sizes.push_back(rs.RandomFloat(70, 105)); c.shade.push_back(rs.RandomFloat(0.82f, 1.f));
  }
  s_smokes.push_back(std::move(c));
}

void FX_DebugBox(const Vector& mins, const Vector& maxs, Color c, float life) { s_boxes.push_back({ mins, maxs, c, life }); }
void FX_HitboxCapsules(CCSPlayer*, const void* hb, Color c, float life) { if (hb) s_capsules.push_back({ *(const HitboxSet*)hb, c, life, life }); }

void FX_Update(float dt) {
  for (auto& p : s_particles) {
    p.life -= dt;
    p.vel.z -= p.gravity * 800.f * dt;
    p.vel *= std::max(0.f, 1.f - p.drag * dt);
    p.pos += p.vel * dt;
    p.size += p.sizeVel * dt;
    p.rot += p.rotVel * dt;
  }
  s_particles.erase(std::remove_if(s_particles.begin(), s_particles.end(), [](const Particle& p) { return p.life <= 0; }), s_particles.end());
  for (auto& t : s_tracers) t.life -= dt;
  s_tracers.erase(std::remove_if(s_tracers.begin(), s_tracers.end(), [](const Tracer& t) { return t.life <= 0; }), s_tracers.end());
  for (auto& b : s_boxes) b.life -= dt;
  s_boxes.erase(std::remove_if(s_boxes.begin(), s_boxes.end(), [](const DebugBox& b) { return b.life <= 0; }), s_boxes.end());
  for (auto& c : s_capsules) c.life -= dt;
  s_capsules.erase(std::remove_if(s_capsules.begin(), s_capsules.end(), [](const Capsules& c) { return c.life <= 0; }), s_capsules.end());
  float now = ServerTime();
  s_smokes.erase(std::remove_if(s_smokes.begin(), s_smokes.end(), [&](const SmokeCloud& c) { return now > c.start + c.duration + 0.1f || now < c.start - 1.f; }), s_smokes.end());
}

// ------------------------------------------------------------------------------------------------
static void Billboard(const CViewSetup& v, const Vector& p, float size, float rotDeg, Color c) {
  float cr = std::cos(DEG2RADF(rotDeg)), sr = std::sin(DEG2RADF(rotDeg));
  Vector r = (v.right * cr + v.up * sr) * size, u = (v.up * cr - v.right * sr) * size;
  rlColor4ub(c.r, c.g, c.b, c.a);
  rlTexCoord2f(0, 1); rlVertex3f(p.x - r.x - u.x, p.y - r.y - u.y, p.z - r.z - u.z);
  rlTexCoord2f(1, 1); rlVertex3f(p.x + r.x - u.x, p.y + r.y - u.y, p.z + r.z - u.z);
  rlTexCoord2f(1, 0); rlVertex3f(p.x + r.x + u.x, p.y + r.y + u.y, p.z + r.z + u.z);
  rlTexCoord2f(0, 0); rlVertex3f(p.x - r.x + u.x, p.y - r.y + u.y, p.z - r.z + u.z);
}
static void Beam(const CViewSetup& v, const Vector& a, const Vector& b, float w, Color ca, Color cb) {
  Vector dir = b - a; Vector toEye = v.origin - (a + b) * 0.5f;
  Vector side = dir.Cross(toEye); float l = side.Length(); if (l < 1e-4f) return; side = side * (w * 0.5f / l);
  rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlTexCoord2f(0, 0); rlVertex3f(a.x - side.x, a.y - side.y, a.z - side.z);
  rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlTexCoord2f(1, 0); rlVertex3f(b.x - side.x, b.y - side.y, b.z - side.z);
  rlColor4ub(cb.r, cb.g, cb.b, cb.a); rlTexCoord2f(1, 1); rlVertex3f(b.x + side.x, b.y + side.y, b.z + side.z);
  rlColor4ub(ca.r, ca.g, ca.b, ca.a); rlTexCoord2f(0, 1); rlVertex3f(a.x + side.x, a.y + side.y, a.z + side.z);
}
static unsigned TexFor(int t) {
  const CMaterialSystem& m = g_MaterialSystem;
  switch (t) { case 0: return m.smoke.id; case 1: return m.spark.id; case 2: return m.flare.id; case 3: return m.cloud.id; default: return m.white.id; }
}

void FX_DrawOpaque(const CViewSetup& v) {
  if (!g_MaterialSystem.initialized) return;
  // decals: dark quads on the surface
  rlDrawRenderBatchActive();
  rlDisableDepthMask();
  rlSetTexture(g_MaterialSystem.decalHole.id);
  rlBegin(RL_QUADS);
  for (auto& d : s_decals) {
    if ((d.pos - v.origin).LengthSqr() > 2500.f * 2500.f) continue;
    Vector n = d.normal; Vector t = std::fabs(n.z) < 0.9f ? Vector(0, 0, 1).Cross(n).Normalized() : Vector(1, 0, 0).Cross(n).Normalized(); Vector b = n.Cross(t);
    t *= d.size; b *= d.size;
    Color c = d.size > 10 ? Color{ 20, 18, 15, 170 } : Color{ 255, 255, 255, 235 };
    rlColor4ub(c.r, c.g, c.b, c.a);
    rlTexCoord2f(0, 0); rlVertex3f(d.pos.x - t.x - b.x, d.pos.y - t.y - b.y, d.pos.z - t.z - b.z);
    rlTexCoord2f(0, 1); rlVertex3f(d.pos.x - t.x + b.x, d.pos.y - t.y + b.y, d.pos.z - t.z + b.z);
    rlTexCoord2f(1, 1); rlVertex3f(d.pos.x + t.x + b.x, d.pos.y + t.y + b.y, d.pos.z + t.z + b.z);
    rlTexCoord2f(1, 0); rlVertex3f(d.pos.x + t.x - b.x, d.pos.y + t.y - b.y, d.pos.z + t.z - b.z);
  }
  rlEnd();
  rlSetTexture(0);
  rlDrawRenderBatchActive();
  rlEnableDepthMask();
  // debug boxes / capsules (lines)
  if (!s_boxes.empty() || !s_capsules.empty()) {
    rlBegin(RL_LINES);
    for (auto& bx : s_boxes) {
      Vector c[8]; for (int i = 0; i < 8; i++) c[i] = Vector(i & 1 ? bx.maxs.x : bx.mins.x, i & 2 ? bx.maxs.y : bx.mins.y, i & 4 ? bx.maxs.z : bx.mins.z);
      static const int E[12][2] = { { 0, 1 }, { 2, 3 }, { 4, 5 }, { 6, 7 }, { 0, 2 }, { 1, 3 }, { 4, 6 }, { 5, 7 }, { 0, 4 }, { 1, 5 }, { 2, 6 }, { 3, 7 } };
      rlColor4ub(bx.c.r, bx.c.g, bx.c.b, bx.c.a);
      for (auto& e : E) { rlVertex3f(c[e[0]].x, c[e[0]].y, c[e[0]].z); rlVertex3f(c[e[1]].x, c[e[1]].y, c[e[1]].z); }
    }
    for (auto& cap : s_capsules) {
      float a = cap.life / cap.maxLife; rlColor4ub(cap.c.r, cap.c.g, cap.c.b, (unsigned char)(cap.c.a * std::min(1.f, a * 2)));
      for (auto& h : cap.hb) {
        Vector ax = h.b - h.a; Vector d = ax.Length() > 0.01f ? ax.Normalized() : Vector(0, 0, 1);
        Vector t = std::fabs(d.z) < 0.9f ? d.Cross(Vector(0, 0, 1)).Normalized() : d.Cross(Vector(1, 0, 0)).Normalized(); Vector bb = d.Cross(t);
        const int S = 10;
        for (int i = 0; i < S; i++) {
          float a0 = i * 2 * (float)M_PI / S, a1 = (i + 1) * 2 * (float)M_PI / S;
          Vector o0 = (t * std::cos(a0) + bb * std::sin(a0)) * h.radius, o1 = (t * std::cos(a1) + bb * std::sin(a1)) * h.radius;
          for (const Vector& base : { h.a, h.b }) { Vector p0 = base + o0, p1 = base + o1; rlVertex3f(p0.x, p0.y, p0.z); rlVertex3f(p1.x, p1.y, p1.z); }
          if (i % 3 == 0) { Vector p0 = h.a + o0, p1 = h.b + o0; rlVertex3f(p0.x, p0.y, p0.z); rlVertex3f(p1.x, p1.y, p1.z); }
        }
        // end caps
        for (int i = 0; i < S; i++) {
          float a0 = i * (float)M_PI / S, a1 = (i + 1) * (float)M_PI / S;
          Vector p0 = h.a + (t * std::cos(a0) - d * std::sin(a0)) * h.radius, p1 = h.a + (t * std::cos(a1) - d * std::sin(a1)) * h.radius;
          rlVertex3f(p0.x, p0.y, p0.z); rlVertex3f(p1.x, p1.y, p1.z);
          p0 = h.b + (t * std::cos(a0) + d * std::sin(a0)) * h.radius; p1 = h.b + (t * std::cos(a1) + d * std::sin(a1)) * h.radius;
          rlVertex3f(p0.x, p0.y, p0.z); rlVertex3f(p1.x, p1.y, p1.z);
        }
      }
    }
    rlEnd();
    rlDrawRenderBatchActive();
  }
}

void FX_DrawTranslucent(const CViewSetup& v) {
  if (!g_MaterialSystem.initialized) return;
  rlDrawRenderBatchActive();
  rlDisableDepthMask();
  rlDisableBackfaceCulling();
  float now = ServerTime();
  // smoke grenade clouds (sorted far->near)
  if (!cl.removeSmoke) {
    struct P { float d; Vector p; float s; float a; float shade; };
    std::vector<P> puffs;
    for (auto& c : s_smokes) {
      float t = now - c.start;
      float grow = Clamp(t / 0.9f, 0.f, 1.f), fade = Clamp((c.start + c.duration - now) / 2.f, 0.f, 1.f);
      for (size_t i = 0; i < c.puffs.size(); i++) {
        Vector p = VectorLerp(c.center + Vector(0, 0, 16), c.puffs[i], 0.3f + 0.7f * grow);
        p.z += std::sin(now * 0.4f + i) * 3.f;
        puffs.push_back({ (p - v.origin).LengthSqr(), p, c.sizes[i] * (0.4f + 0.6f * grow), fade * 0.92f, c.shade[i] });
      }
    }
    std::sort(puffs.begin(), puffs.end(), [](const P& a, const P& b) { return a.d > b.d; });
    rlSetTexture(g_MaterialSystem.cloud.id);
    rlBegin(RL_QUADS);
    for (auto& p : puffs) { unsigned char g = (unsigned char)(185 * p.shade * cl.worldBrightness); Billboard(v, p.p, p.s, p.d * 0.0001f, { g, g, (unsigned char)(g * 0.98f), (unsigned char)(255 * p.a) }); }
    rlEnd();
    rlDrawRenderBatchActive();
  }
  // normal particles
  std::vector<int> order(s_particles.size());
  for (size_t i = 0; i < order.size(); i++) order[i] = (int)i;
  std::sort(order.begin(), order.end(), [&](int a, int b) { return (s_particles[a].pos - v.origin).LengthSqr() > (s_particles[b].pos - v.origin).LengthSqr(); });
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1) BeginBlendMode(BLEND_ADDITIVE);
    for (int tex = 0; tex < 4; tex++) {
      bool any = false;
      for (int i : order) { const Particle& p = s_particles[i]; if (p.additive != (pass == 1) || p.tex != tex) continue; any = true; break; }
      if (!any) continue;
      rlSetTexture(TexFor(tex));
      rlBegin(RL_QUADS);
      for (int i : order) {
        const Particle& p = s_particles[i]; if (p.additive != (pass == 1) || p.tex != tex) continue;
        float a = Clamp(p.life / p.maxLife, 0.f, 1.f); if (!p.additive) a = std::min(1.f, a * 1.5f);
        Color c = p.color; c.a = (unsigned char)(c.a * a);
        if (p.stretch) { Vector tail = p.pos - p.vel * 0.03f; Beam(v, tail, p.pos, p.size, { c.r, c.g, c.b, 0 }, c); }
        else Billboard(v, p.pos, p.size, p.rot, c);
      }
      rlEnd();
      rlDrawRenderBatchActive();
    }
    if (pass == 1) EndBlendMode();
  }
  // tracers
  if (!s_tracers.empty()) {
    BeginBlendMode(BLEND_ADDITIVE);
    rlSetTexture(g_MaterialSystem.white.id);
    rlBegin(RL_QUADS);
    for (auto& t : s_tracers) {
      float a = Clamp(t.life / t.maxLife, 0.f, 1.f);
      if (t.beam) { Color c = t.c; c.a = (unsigned char)(c.a * a); Beam(v, t.a, t.b, t.width, c, c); continue; }
      // moving streak along the path
      float len = (t.b - t.a).Length(); if (len < 1) continue;
      float traveled = (t.maxLife - t.life) * t.speed; float streak = 280.f;
      float s0 = std::max(0.f, traveled - streak), s1 = std::min(len, traveled);
      if (s1 <= s0) continue;
      Vector d = (t.b - t.a) / len;
      Color c0 = t.c; c0.a = 0; Color c1 = t.c; c1.a = (unsigned char)(t.c.a * std::min(1.f, a * 3));
      Beam(v, t.a + d * s0, t.a + d * s1, t.width, c0, c1);
    }
    rlEnd(); rlSetTexture(0); rlDrawRenderBatchActive();
    EndBlendMode();
  }
  rlSetTexture(0);
  rlEnableDepthMask();
  rlEnableBackfaceCulling();
}

void FX_DrawWorldText(const CViewSetup&) {}

// smoke particle volume used by the grenade entity (CSmokeGrenadeProjectile::DrawTranslucent registers via event)
