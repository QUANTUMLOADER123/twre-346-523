#include "worldrender.hpp"
#include "bsp.hpp"
#include "srcbsp.hpp"
#include "enginetrace.hpp"
#include "materialsystem.hpp"
#include "convar.hpp"
#include "random.hpp"
#include "../game/shared/mapinfo.hpp"
#include "rlgl.h"
#include "raymath.h"
#include <thread>
#include <atomic>
#include <chrono>
#include <map>
#include <cstring>
#include <utility>

ConVar r_drawworld("r_drawworld", "1", FCVAR_CHEAT, "Draw world geometry");
ConVar r_drawsky("r_drawsky", "1", 0, "Draw sky");
ConVar r_lightmap_ao("r_lightmap_ao", "10", FCVAR_ARCHIVE, "Ambient occlusion rays per vertex when baking");
ConVar r_lightmap_sun("r_lightmap_sun", "4", FCVAR_ARCHIVE, "Sun shadow rays per vertex when baking");
ConVar r_subdivide("r_subdivide", "32", FCVAR_ARCHIVE, "World face subdivision size for vertex lighting", true, 16, true, 128);
ConVar mat_fullbright("mat_fullbright", "0", FCVAR_CHEAT, "Fullbright world");

namespace {
using Winding = std::vector<Vector>;
Winding BaseWinding(const Vector& n, float d) {
  int axis = 0; float mx = -1;
  for (int i = 0; i < 3; i++) if (std::fabs(n[i]) > mx) { mx = std::fabs(n[i]); axis = i; }
  Vector vup = axis == 2 ? Vector(1, 0, 0) : Vector(0, 0, 1);
  vup = (vup - n * vup.Dot(n)).Normalized();
  Vector org = n * d, vr = vup.Cross(n);
  const float B = 65536.f; vup *= B; vr *= B;
  return { org - vr + vup, org + vr + vup, org + vr - vup, org - vr - vup };
}
// keep the part behind the plane (n.p - d <= 0)
void ClipWinding(Winding& w, const Vector& n, float d, float eps = 0.01f) {
  if (w.empty()) return;
  Winding out; out.reserve(w.size() + 4);
  size_t c = w.size();
  for (size_t i = 0; i < c; i++) {
    const Vector& a = w[i]; const Vector& b = w[(i + 1) % c];
    float da = a.Dot(n) - d, db = b.Dot(n) - d;
    if (da <= eps) out.push_back(a);
    if ((da < -eps && db > eps) || (da > eps && db < -eps)) { float t = da / (da - db); out.push_back(a + (b - a) * t); }
  }
  w.swap(out);
}
float WindingArea(const Winding& w) {
  Vector s; for (size_t i = 2; i < w.size(); i++) s += (w[i - 1] - w[0]).Cross(w[i] - w[0]);
  return s.Length() * 0.5f;
}
Vector WindingCenter(const Winding& w) { Vector c; for (auto& v : w) c += v; return c / (float)std::max<size_t>(1, w.size()); }
inline long long QKey(const Vector& p, const Vector& n) {
  long long x = (long long)std::lround(p.x * 2) + 200000, y = (long long)std::lround(p.y * 2) + 200000, z = (long long)std::lround(p.z * 2) + 20000;
  long long ni = (long long)(std::lround(n.x * 3) + 3) * 49 + (std::lround(n.y * 3) + 3) * 7 + (std::lround(n.z * 3) + 3);
  return ((x * 400000 + y) * 40000 + z) * 343 + ni;
}
struct BakeVert { Vector p, n; Vector light; };
struct Tri { int v[3]; float uv[3][2]; int mat; };
} // namespace

void Frustum::FromMatrix(const Matrix& m) {
  // rows of the combined matrix (column-vector convention)
  float r0[4] = { m.m0, m.m4, m.m8, m.m12 }, r1[4] = { m.m1, m.m5, m.m9, m.m13 }, r2[4] = { m.m2, m.m6, m.m10, m.m14 }, r3[4] = { m.m3, m.m7, m.m11, m.m15 };
  for (int i = 0; i < 4; i++) {
    planes[0][i] = r3[i] + r0[i]; planes[1][i] = r3[i] - r0[i];
    planes[2][i] = r3[i] + r1[i]; planes[3][i] = r3[i] - r1[i];
    planes[4][i] = r3[i] + r2[i]; planes[5][i] = r3[i] - r2[i];
  }
}
bool Frustum::CullBox(const Vector& mn, const Vector& mx) const {
  for (int i = 0; i < 6; i++) {
    const float* p = planes[i];
    Vector v{ p[0] >= 0 ? mx.x : mn.x, p[1] >= 0 ? mx.y : mn.y, p[2] >= 0 ? mx.z : mn.z };
    if (p[0] * v.x + p[1] * v.y + p[2] * v.z + p[3] < 0) return true;
  }
  return false;
}

Vector CWorldRenderer::ComputeLightAt(const Vector& pIn, const Vector& n, int aoRays, int sunRays) {
  const CMapInfo& M = *map;
  CEngineTrace& et = g_EngineTrace;
  // find a sample position outside of solids
  Vector p = pIn + n * 2.f;
  if (et.GetBrushAtPoint(p, CONTENTS_SOLID) >= 0) {
    static const Vector nud[6] = { { 2, 0, 0 }, { -2, 0, 0 }, { 0, 2, 0 }, { 0, -2, 0 }, { 0, 0, 2 }, { 0, 0, -2 } };
    for (auto& d : nud) { if (std::fabs(d.Dot(n)) > 0.5f) continue; Vector q = p + d; if (et.GetBrushAtPoint(q, CONTENTS_SOLID) < 0) { p = q; break; } }
  }
  Vector L = M.sunDir;
  Vector light;
  // sun
  float ndl = n.Dot(L);
  if (ndl > 0 && sunRays > 0) {
    int vis = 0;
    // build a tangent frame around the sun direction for soft shadows
    Vector t1 = std::fabs(L.z) < 0.9f ? L.Cross(Vector(0, 0, 1)).Normalized() : L.Cross(Vector(1, 0, 0)).Normalized(), t2 = L.Cross(t1);
    float len = std::max(64.f, (m_maxTop + 96.f - p.z) / std::max(0.1f, L.z));
    for (int i = 0; i < sunRays; i++) {
      float a = (i + 0.5f) / sunRays * 6.2831f, r = sunRays > 1 ? 0.022f : 0.f;
      Vector d = (L + t1 * (std::cos(a) * r) + t2 * (std::sin(a) * r)).Normalized();
      Ray_t ray; ray.Init(p, p + d * len); trace_t tr; et.TraceRayWorld(ray, MASK_OPAQUE, &tr);
      if (tr.fraction >= 1.f && !tr.allsolid) vis++;
    }
    light += M.sunColor * (ndl * (float)vis / sunRays);
  }
  // sky ambient with occlusion (cosine weighted hemisphere)
  if (aoRays > 0) {
    Vector t1 = std::fabs(n.z) < 0.9f ? n.Cross(Vector(0, 0, 1)).Normalized() : n.Cross(Vector(1, 0, 0)).Normalized(), t2 = n.Cross(t1);
    float open = 0;
    for (int i = 0; i < aoRays; i++) {
      // golden-angle spiral
      float u = (i + 0.5f) / aoRays, phi = i * 2.39996f;
      float r = std::sqrt(u), z = std::sqrt(1 - u);
      Vector d = (t1 * (r * std::cos(phi)) + t2 * (r * std::sin(phi)) + n * z).Normalized();
      Ray_t ray; ray.Init(p, p + d * 220.f); trace_t tr; et.TraceRayWorld(ray, MASK_OPAQUE, &tr);
      open += tr.allsolid ? 0.f : (0.25f + 0.75f * tr.fraction * tr.fraction);
    }
    float ao = open / aoRays;
    // sky is brighter from above, ground bounce from below
    float up = n.z * 0.5f + 0.5f;
    Vector amb = M.ambient * (0.75f + 0.35f * up);
    Vector bounce = M.sunColor * 0.10f * (1.f - up) * Vector(1.0f, 0.9f, 0.75f);
    light += (amb + bounce) * (0.25f + 0.75f * ao);
  } else light += M.ambient;
  // point lights
  for (auto& pl : M.lights) {
    Vector d = pl.pos - p; float dist = d.Length(); if (dist > pl.radius) continue;
    d /= std::max(dist, 0.01f);
    float k = std::max(0.f, n.Dot(d)) * 0.7f + 0.3f;
    float att = 1.f - dist / pl.radius; att *= att;
    Ray_t ray; ray.Init(p, pl.pos); trace_t tr; et.TraceRayWorld(ray, MASK_OPAQUE, &tr);
    if (tr.fraction < 0.98f) continue;
    light += pl.color * (att * k * 1.1f);
  }
  return light;
}

void CWorldRenderer::Build(const CMapInfo& M) {
  Unload();
  map = &M;
  if (M.isBsp && g_SrcBsp.loaded) { BuildSrc(); built = true; return; }
  if (M.isBsp && g_Bsp.loaded) { BuildBsp(); built = true; return; }
  auto t0 = std::chrono::steady_clock::now();
  CEngineTrace& et = g_EngineTrace;
  m_maxTop = M.wallHeight;
  for (auto& b : et.brushes) if (b.contents & CONTENTS_SOLID) m_maxTop = std::max(m_maxTop, b.maxs.z);
  const float S = r_subdivide.GetFloat();
  const float gx0 = M.gridOrigin.x, gx1 = M.gridOrigin.x + M.gw * M.cell, gy1 = M.gridOrigin.y, gy0 = M.gridOrigin.y - M.gh * M.cell;
  float bottom = 1e9f; for (auto& b : et.brushes) if (b.contents & CONTENTS_SOLID) bottom = std::min(bottom, b.mins.z);

  std::vector<BakeVert> verts; std::unordered_map<long long, int> vmap; std::vector<Tri> tris;
  auto vid = [&](const Vector& p, const Vector& n) {
    long long k = QKey(p, n); auto it = vmap.find(k); if (it != vmap.end()) return it->second;
    int id = (int)verts.size(); verts.push_back({ p, n, {} }); vmap[k] = id; return id;
  };
  for (int bi = 0; bi < (int)et.brushes.size(); bi++) {
    const cbrush_t& b = et.brushes[bi];
    if (!b.render || !(b.contents & CONTENTS_SOLID) || b.material <= 0) continue;
    const MaterialInfo& mi = g_MaterialSystem.Get(b.material);
    bool boxMap = b.material == MAT_CRATE || b.material == MAT_CRATE_DARK || b.material == MAT_BARREL;
    for (int pi = 0; pi < b.numPlanes; pi++) {
      const cplane_t& pl = et.planes[b.firstPlane + pi];
      Winding w = BaseWinding(pl.normal, pl.dist);
      for (int pj = 0; pj < b.numPlanes && w.size() >= 3; pj++) if (pj != pi) { const cplane_t& o = et.planes[b.firstPlane + pj]; ClipWinding(w, o.normal, o.dist); }
      if (w.size() < 3 || WindingArea(w) < 0.5f) continue;
      const Vector& n = pl.normal;
      if (n.z < -0.99f && w[0].z <= bottom + 1.f) continue; // never visible
      int ax = 0; float mx = -1; for (int i = 0; i < 3; i++) if (std::fabs(n[i]) > mx) { mx = std::fabs(n[i]); ax = i; }
      int ua = ax == 0 ? 1 : 0, va = ax == 2 ? 1 : 2;
      Vector wmn(1e9f, 1e9f, 1e9f), wmx(-1e9f, -1e9f, -1e9f); for (auto& v : w) { wmn = VecMin(wmn, v); wmx = VecMax(wmx, v); }
      int iu0 = (int)std::floor(wmn[ua] / S + 0.001f), iu1 = (int)std::ceil(wmx[ua] / S - 0.001f);
      int iv0 = (int)std::floor(wmn[va] / S + 0.001f), iv1 = (int)std::ceil(wmx[va] / S - 0.001f);
      for (int iu = iu0; iu < iu1; iu++) for (int iv = iv0; iv < iv1; iv++) {
        Winding c = w;
        Vector e; e.Init(); e[ua] = 1; ClipWinding(c, e, (iu + 1) * S); ClipWinding(c, -e, -(iu * S));
        e.Init(); e[va] = 1; ClipWinding(c, e, (iv + 1) * S); ClipWinding(c, -e, -(iv * S));
        if (c.size() < 3 || WindingArea(c) < 0.05f) continue;
        Vector ctr = WindingCenter(c) + n * 1.0f;
        if (ctr.x < gx0 - 1 || ctr.x > gx1 + 1 || ctr.y < gy0 - 1 || ctr.y > gy1 + 1) continue; // outside the playable map
        // hidden inside another solid brush?
        auto covered = [&](const Vector& q) { int hb = et.GetBrushAtPoint(q, CONTENTS_SOLID); return hb >= 0 && hb != bi; };
        bool hidden = covered(ctr);
        if (hidden) for (auto& cv : c) { Vector q = ctr + ((cv + n * 1.0f) - ctr) * 0.8f; if (!covered(q)) { hidden = false; break; } }
        if (hidden) continue;
        // triangulate (fan) with CCW order seen from the front
        std::vector<int> ids; for (auto& v : c) ids.push_back(vid(v, n));
        for (size_t k = 2; k < c.size(); k++) {
          Tri t; t.mat = b.material;
          int i0 = 0, i1 = (int)k - 1, i2 = (int)k;
          if ((c[i1] - c[i0]).Cross(c[i2] - c[i0]).Dot(n) < 0) std::swap(i1, i2);
          int idx[3] = { i0, i1, i2 };
          for (int q = 0; q < 3; q++) {
            const Vector& v = c[idx[q]]; t.v[q] = ids[idx[q]];
            if (boxMap) {
              float su = std::max(1.f, b.maxs[ua] - b.mins[ua]), sv = std::max(1.f, b.maxs[va] - b.mins[va]);
              t.uv[q][0] = (v[ua] - b.mins[ua]) / su; t.uv[q][1] = 1.f - (v[va] - b.mins[va]) / sv;
            } else {
              float ts = mi.texWorldSize;
              t.uv[q][0] = v[ua] / ts; t.uv[q][1] = -v[va] / ts;
            }
          }
          tris.push_back(t);
        }
      }
    }
  }
  // bake lighting in parallel
  int aoRays = r_lightmap_ao.GetInt(), sunRays = r_lightmap_sun.GetInt();
  std::atomic<int> next{ 0 };
  unsigned nth = std::max(1u, std::min(16u, std::thread::hardware_concurrency()));
  auto worker = [&]() { int i; while ((i = next.fetch_add(64)) < (int)verts.size()) { int e = std::min((int)verts.size(), i + 64); for (int k = i; k < e; k++) verts[k].light = mat_fullbright.GetBool() ? Vector(1, 1, 1) : ComputeLightAt(verts[k].p, verts[k].n, aoRays, sunRays); } };
  std::vector<std::thread> pool; for (unsigned t = 0; t < nth; t++) pool.emplace_back(worker);
  for (auto& t : pool) t.join();

  // build chunked meshes (1024u x 1024u x material)
  const float CH = 1024.f;
  std::map<long long, std::vector<int>> groups;
  for (int i = 0; i < (int)tris.size(); i++) {
    Vector c = (verts[tris[i].v[0]].p + verts[tris[i].v[1]].p + verts[tris[i].v[2]].p) / 3.f;
    long long cx = (long long)std::floor(c.x / CH) + 512, cy = (long long)std::floor(c.y / CH) + 512;
    groups[(cx * 1024 + cy) * 256 + tris[i].mat].push_back(i);
  }
  for (auto& [key, list] : groups) {
    int mat = (int)(key % 256);
    for (size_t start = 0; start < list.size(); start += 20000) {
      size_t cnt = std::min<size_t>(20000, list.size() - start);
      Chunk ch; ch.mat = mat; ch.mins = Vector(1e9f, 1e9f, 1e9f); ch.maxs = Vector(-1e9f, -1e9f, -1e9f);
      Mesh& m = ch.mesh; m.vertexCount = (int)cnt * 3; m.triangleCount = (int)cnt;
      m.vertices = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
      m.texcoords = (float*)MemAlloc(m.vertexCount * 2 * sizeof(float));
      m.colors = (unsigned char*)MemAlloc(m.vertexCount * 4);
      m.normals = (float*)MemAlloc(m.vertexCount * 3 * sizeof(float));
      for (size_t t = 0; t < cnt; t++) {
        const Tri& tr = tris[list[start + t]];
        for (int q = 0; q < 3; q++) {
          int vi = (int)t * 3 + q; const BakeVert& bv = verts[tr.v[q]];
          m.vertices[vi * 3 + 0] = bv.p.x; m.vertices[vi * 3 + 1] = bv.p.y; m.vertices[vi * 3 + 2] = bv.p.z;
          m.normals[vi * 3 + 0] = bv.n.x; m.normals[vi * 3 + 1] = bv.n.y; m.normals[vi * 3 + 2] = bv.n.z;
          m.texcoords[vi * 2 + 0] = tr.uv[q][0]; m.texcoords[vi * 2 + 1] = tr.uv[q][1];
          m.colors[vi * 4 + 0] = (unsigned char)Clamp(bv.light.x * 255.f, 0.f, 255.f);
          m.colors[vi * 4 + 1] = (unsigned char)Clamp(bv.light.y * 255.f, 0.f, 255.f);
          m.colors[vi * 4 + 2] = (unsigned char)Clamp(bv.light.z * 255.f, 0.f, 255.f);
          m.colors[vi * 4 + 3] = 255;
          ch.mins = VecMin(ch.mins, bv.p); ch.maxs = VecMax(ch.maxs, bv.p);
        }
      }
      UploadMesh(&m, false);
      ch.material = LoadMaterialDefault();
      ch.material.maps[MATERIAL_MAP_DIFFUSE].texture = g_MaterialSystem.Get(mat).tex;
      m_chunks.push_back(ch);
    }
  }
  numTris = (int)tris.size(); numVerts = (int)verts.size(); numChunks = (int)m_chunks.size();
  bakeSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
  m_probeCache.clear();
  built = true;
  Msg("World: %d tris, %d light samples, %d chunks, baked in %.2fs (%u threads)\n", numTris, numVerts, numChunks, bakeSeconds, nth);
}

void CWorldRenderer::Unload() {
  for (auto& c : m_chunks) { UnloadMesh(c.mesh); MemFree(c.material.maps); }
  m_chunks.clear(); m_probeCache.clear(); built = false;
  UnloadBsp();
  UnloadSrc();
}

void CWorldRenderer::DrawSky(const CViewSetup& v, float time) {
  if (!map || !r_drawsky.GetBool()) return;
  if (src && m_hasSky) { DrawSrcSky(v); return; }
  if (bsp && m_hasSky) { DrawBspSky(v); return; }
  rlDisableDepthMask(); rlDisableDepthTest(); rlDisableBackfaceCulling();
  const Vector& o = v.origin; const float R = 6000.f;
  auto col = [&](float elev) { // elev -1..1
    Vector top = map->skyTop, hor = map->skyHorizon;
    Vector c = elev > 0 ? VectorLerp(hor, top, std::pow(Clamp(elev, 0.f, 1.f), 0.6f)) : hor * 0.85f;
    return Color{ (unsigned char)Clamp(c.x * 255, 0.f, 255.f), (unsigned char)Clamp(c.y * 255, 0.f, 255.f), (unsigned char)Clamp(c.z * 255, 0.f, 255.f), 255 };
  };
  const int SEG = 24, RING = 12;
  rlSetTexture(g_MaterialSystem.white.id);
  rlBegin(RL_TRIANGLES);
  for (int r = 0; r < RING; r++) {
    float e0 = -1.f + (r / (float)RING) * 2.f, e1 = -1.f + ((r + 1) / (float)RING) * 2.f;
    float a0 = std::asin(Clamp(e0, -1.f, 1.f)), a1 = std::asin(Clamp(e1, -1.f, 1.f));
    for (int s = 0; s < SEG; s++) {
      float y0 = s / (float)SEG * 6.2832f, y1 = (s + 1) / (float)SEG * 6.2832f;
      auto P = [&](float yaw, float el) { return Vector(o.x + std::cos(yaw) * std::cos(el) * R, o.y + std::sin(yaw) * std::cos(el) * R, o.z + std::sin(el) * R); };
      Vector p00 = P(y0, a0), p10 = P(y1, a0), p01 = P(y0, a1), p11 = P(y1, a1);
      Color c0 = col(e0), c1 = col(e1);
      auto V = [&](const Vector& p, Color c) { rlColor4ub(c.r, c.g, c.b, 255); rlTexCoord2f(0.5f, 0.5f); rlVertex3f(p.x, p.y, p.z); };
      V(p00, c0); V(p10, c0); V(p11, c1);
      V(p00, c0); V(p11, c1); V(p01, c1);
    }
  }
  rlEnd();
  // clouds layer
  if (!map->snow || true) {
    rlSetTexture(g_MaterialSystem.cloud.id);
    rlBegin(RL_QUADS);
    float h = 2400.f, ext = 9000.f, sc = 1.f / 6000.f, sh = time * 0.004f;
    Vector top = map->skyTop;
    unsigned char cr = (unsigned char)Clamp(200 + top.x * 55, 0.f, 255.f), cg = (unsigned char)Clamp(200 + top.y * 55, 0.f, 255.f), cb = (unsigned char)Clamp(205 + top.z * 50, 0.f, 255.f);
    rlColor4ub(cr, cg, cb, 170);
    float x0 = o.x - ext, x1 = o.x + ext, y0 = o.y - ext, y1 = o.y + ext, z = o.z + h;
    rlTexCoord2f(x0 * sc + sh, y0 * sc); rlVertex3f(x0, y0, z);
    rlTexCoord2f(x0 * sc + sh, y1 * sc); rlVertex3f(x0, y1, z);
    rlTexCoord2f(x1 * sc + sh, y1 * sc); rlVertex3f(x1, y1, z);
    rlTexCoord2f(x1 * sc + sh, y0 * sc); rlVertex3f(x1, y0, z);
    rlEnd();
  }
  // sun glow
  {
    Vector sp = o + map->sunDir * 5000.f;
    Vector r = v.right, u = v.up; float s = 520.f;
    rlSetTexture(g_MaterialSystem.glowDot.id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 246, 220, 230);
    Vector a = sp - r * s - u * s, b = sp + r * s - u * s, c = sp + r * s + u * s, d = sp - r * s + u * s;
    rlTexCoord2f(0, 1); rlVertex3f(a.x, a.y, a.z); rlTexCoord2f(1, 1); rlVertex3f(b.x, b.y, b.z);
    rlTexCoord2f(1, 0); rlVertex3f(c.x, c.y, c.z); rlTexCoord2f(0, 0); rlVertex3f(d.x, d.y, d.z);
    rlEnd();
  }
  rlSetTexture(0);
  rlDrawRenderBatchActive();
  rlEnableDepthMask(); rlEnableDepthTest(); rlEnableBackfaceCulling();
}

void CWorldRenderer::DrawWorld(const CViewSetup& v) {
  drawnChunks = 0;
  if (!built || !r_drawworld.GetBool()) return;
  if (src) { DrawSrc(v); return; }
  if (bsp) { DrawBsp(v); return; }
  Frustum fr; fr.FromMatrix(v.viewProj);
  for (auto& c : m_chunks) {
    if (fr.CullBox(c.mins, c.maxs)) continue;
    DrawMesh(c.mesh, c.material, MatrixIdentity());
    drawnChunks++;
  }
}

LightSample CWorldRenderer::SampleLight(const Vector& pos) {
  if (src) return SampleLightSrc(pos);
  if (bsp) return SampleLightBsp(pos);
  LightSample s; if (!map) { s.ambient = { 0.6f, 0.6f, 0.6f }; s.direct = { 0.5f, 0.5f, 0.5f }; s.dir = { 0, 0, 1 }; return s; }
  long long key = QKey(Vector(std::floor(pos.x / 24.f) * 24.f, std::floor(pos.y / 24.f) * 24.f, std::floor(pos.z / 24.f) * 24.f), Vector(0, 0, 1));
  auto it = m_probeCache.find(key); if (it != m_probeCache.end()) return it->second;
  if (m_probeCache.size() > 200000) m_probeCache.clear();
  Vector p = pos;
  // ambient: average of up / horizontal directions without sun
  Vector amb; const Vector dirs[5] = { { 0, 0, 1 }, { 1, 0, 0 }, { -1, 0, 0 }, { 0, 1, 0 }, { 0, -1, 0 } };
  for (auto& d : dirs) amb += ComputeLightAt(p, d, 4, 0);
  amb /= 5.f;
  // direct sun visibility
  Ray_t ray; ray.Init(p, p + map->sunDir * 4000.f); trace_t tr; g_EngineTrace.TraceRayWorld(ray, MASK_OPAQUE, &tr);
  s.ambient = amb; s.direct = (tr.fraction >= 1.f) ? map->sunColor : Vector(); s.dir = map->sunDir;
  m_probeCache[key] = s;
  return s;
}
