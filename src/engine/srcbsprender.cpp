// srcbsprender.cpp - renderer for Counter-Strike: Source maps (CSrcBsp)
//
// Same pass structure as the GoldSrc renderer: textures -> displacement blend layer ($basetexture2) ->
// lightmaps (modulate 2x) -> static props -> alpha tested -> translucent (sorted). Lightmaps are Source
// ColorRGBExp32 luxels converted from linear to gamma space.
#include "worldrender.hpp"
#include "srcbsp.hpp"
#include "srcmaterial.hpp"
#include "srcmdl.hpp"
#include "srcfs.hpp"
#include "convar.hpp"
#include "../game/shared/mapinfo.hpp"
#include "rlgl.h"
#include "raymath.h"
#include <map>
#include <tuple>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>

extern ConVar r_drawworld;
extern ConVar mat_fullbright;
ConVar r_src_lightscale("r_src_lightscale", "1.15", FCVAR_ARCHIVE, "CS:S lightmap brightness (applied on map load)", true, 0.25f, true, 4.f);
ConVar r_drawprops("r_drawprops", "1", 0, "Draw static props on CS:S maps");
ConVar r_propfade("r_propfade", "1", FCVAR_ARCHIVE, "Fade static props at the distance set by the mapper");
ConVar r_propscreencull("r_propscreencull", "2", FCVAR_ARCHIVE, "Skip static props smaller than this many pixels on screen (0 = off)");

#ifdef CS_HEADLESS
extern "C" void swBlendFunc(int sfactor, int dfactor);
#endif
namespace {
enum BlendKind { BK_ALPHA, BK_ADDITIVE, BK_MODULATE2X };
void SetBlend(BlendKind k) {
  rlDrawRenderBatchActive();
#ifdef CS_HEADLESS
  if (k == BK_MODULATE2X) swBlendFunc(0x0306, 0x0300);
  else if (k == BK_ADDITIVE) swBlendFunc(0x0302, 1);
  else swBlendFunc(0x0302, 0x0303);
#else
  if (k == BK_MODULATE2X) { rlSetBlendFactors(RL_DST_COLOR, RL_SRC_COLOR, RL_FUNC_ADD); rlSetBlendMode(RL_BLEND_CUSTOM); }
  else rlSetBlendMode(k == BK_ADDITIVE ? RL_BLEND_ADDITIVE : RL_BLEND_ALPHA);
#endif
}
float Gamma(float lin) { return std::pow(std::max(0.f, lin * r_src_lightscale.GetFloat()), 1.f / 2.2f); }
Vector Gamma(const Vector& c) { return { Gamma(c.x), Gamma(c.y), Gamma(c.z) }; }
unsigned char U8(float f) { return (unsigned char)Clamp(f * 255.f, 0.f, 255.f); }

struct Builder {
  std::vector<float> v, uv; std::vector<unsigned char> col;
  Vector mins{ 1e9f, 1e9f, 1e9f }, maxs{ -1e9f, -1e9f, -1e9f };
  void Add(const Vector& p, float u, float t, Color c, bool withColor) {
    v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); uv.push_back(u); uv.push_back(t);
    if (withColor) { col.push_back(c.r); col.push_back(c.g); col.push_back(c.b); col.push_back(c.a); }
    mins = VecMin(mins, p); maxs = VecMax(maxs, p);
  }
};
Matrix ToRl(const matrix3x4_t& m) {
  Matrix r{};
  r.m0 = m.m[0][0]; r.m4 = m.m[0][1]; r.m8 = m.m[0][2]; r.m12 = m.m[0][3];
  r.m1 = m.m[1][0]; r.m5 = m.m[1][1]; r.m9 = m.m[1][2]; r.m13 = m.m[1][3];
  r.m2 = m.m[2][0]; r.m6 = m.m[2][1]; r.m10 = m.m[2][2]; r.m14 = m.m[2][3];
  r.m15 = 1; return r;
}
Material& PropMaterial() { static Material m{}; static bool init = false; if (!init) { m = LoadMaterialDefault(); init = true; } return m; }
} // namespace

void CWorldRenderer::UnloadSrc() {
  m_srcProps.clear();
  for (auto& t : m_srcSky) t = Texture2D{};   // owned by the material cache
  src = false;
}

void CWorldRenderer::BuildSrc() {
  auto t0 = std::chrono::steady_clock::now();
  const CSrcBsp& W = g_SrcBsp;
  bool fullbright = mat_fullbright.GetBool();
  // ---- materials
  std::vector<const SrcMaterial*> mats(W.materials.size());
  int matOk = 0, matFallback = 0;
  for (size_t i = 0; i < W.materials.size(); i++) {
    mats[i] = &SrcMat_Find(W.materials[i]);
    if (mats[i]->ok) matOk++; else matFallback++;
  }
  // ---- lightmap atlas
  struct Slot { int x = -1, y = -1; };
  std::vector<Slot> slot(W.faces.size());
  std::vector<int> order; long long area = 0;
  for (int i = 0; i < (int)W.faces.size(); i++) if (W.faces[i].lightofs >= 0) { order.push_back(i); area += (long long)(W.faces[i].lmSize[0] + 3) * (W.faces[i].lmSize[1] + 3); }
  std::sort(order.begin(), order.end(), [&](int a, int b) { return W.faces[a].lmSize[1] > W.faces[b].lmSize[1]; });
  int AW = 256; while ((long long)AW * AW < area * 5 / 4 + 64 && AW < 4096) AW *= 2;
  int x = 3, y = 0, rowH = 3;
  for (int fi : order) {
    const SrcFace& f = W.faces[fi]; int w = f.lmSize[0] + 3, h = f.lmSize[1] + 3;
    if (w > AW) continue;
    if (x + w > AW) { x = 0; y += rowH; rowH = 0; }
    if (y + h > 4096) break;
    slot[fi] = { x, y }; x += w; rowH = std::max(rowH, h);
  }
  int AH = 16; while (AH < y + rowH) AH *= 2;
  std::vector<uint8_t> atlas((size_t)AW * AH * 4, 0);
  auto put = [&](int px, int py, const Vector& c) {   // c = gamma-space light (1 = texture as is)
    if (px < 0 || py < 0 || px >= AW || py >= AH) return;
    uint8_t* o = &atlas[((size_t)py * AW + px) * 4];
#ifdef CS_HEADLESS
    // the software rasterizer wraps instead of saturating in the 2x modulate blend: keep 2*src*dst <= 1
    const float cap = 0.495f;
#else
    const float cap = 1.f;
#endif
    o[0] = U8(std::min(c.x * 0.5f, cap)); o[1] = U8(std::min(c.y * 0.5f, cap)); o[2] = U8(std::min(c.z * 0.5f, cap)); o[3] = 255;
  };
  for (int yy = 0; yy < 3; yy++) for (int xx = 0; xx < 3; xx++) put(xx, yy, Vector(1, 1, 1));
  auto luxel = [&](const SrcFace& f, int sx, int sy) {
    int w = f.lmSize[0] + 1, h = f.lmSize[1] + 1; sx = Clamp(sx, 0, w - 1); sy = Clamp(sy, 0, h - 1);
    size_t off = (size_t)f.lightofs + ((size_t)sy * w + sx) * 4;
    if (off + 4 > W.lighting.size()) return Vector(1, 1, 1);
    return Gamma(CSrcBsp::DecodeLuxel(&W.lighting[off]));
  };
  for (int fi : order) {
    const SrcFace& f = W.faces[fi]; if (slot[fi].x < 0) continue;
    int w = f.lmSize[0] + 1, h = f.lmSize[1] + 1;
    for (int ty = -1; ty <= h; ty++) for (int tx = -1; tx <= w; tx++)
      put(slot[fi].x + 1 + tx, slot[fi].y + 1 + ty, fullbright ? Vector(1, 1, 1) : luxel(f, tx, ty));
  }
  m_lightmap = LoadTextureFromImage(Image{ atlas.data(), AW, AH, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 });
#ifndef CS_HEADLESS
  SetTextureFilter(m_lightmap, TEXTURE_FILTER_BILINEAR);
#endif
  SetTextureWrap(m_lightmap, TEXTURE_WRAP_CLAMP);

  // ---- geometry
  std::map<std::tuple<int, int, int, int>, Builder> builders;   // (pass, material, cellx, celly)
  auto cellOf = [](const Vector& p) { return std::make_pair((int)std::floor(p.x / 1024.f), (int)std::floor(p.y / 1024.f)); };
  int tris = 0;
  for (int fi = 0; fi < (int)W.faces.size(); fi++) {
    const SrcFace& f = W.faces[fi];
    if (f.material < 0 || f.verts.empty()) continue;
    const SrcMaterial& m = *mats[f.material];
    if (m.nodraw || m.sky || m.tool) continue;
    int pass = BSPPASS_OPAQUE; unsigned char alpha = 255;
    const SrcBrushEnt* be = nullptr;
    if (f.model) for (auto& b : W.brushEnts) if (b.model == f.model) { be = &b; break; }
    if (m.water) { pass = BSPPASS_TRANSLUCENT; alpha = U8(m.alpha < 1.f ? m.alpha : 0.75f); }
    else if (m.additive) pass = BSPPASS_ADDITIVE;
    else if (m.translucent) { pass = BSPPASS_TRANSLUCENT; alpha = U8(m.alpha); }
    else if (m.alphatest) pass = BSPPASS_ALPHATEST;
    if (be && (be->rendermode == 1 || be->rendermode == 2) && be->renderamt < 250) { pass = BSPPASS_TRANSLUCENT; alpha = (unsigned char)Clamp(be->renderamt, 0.f, 255.f); }
    if (be && be->rendermode == 5) pass = BSPPASS_ADDITIVE;
    Vector center; for (auto& v : f.verts) center += v.pos; center /= (float)f.verts.size();
    auto cell = cellOf(center);
    Builder& B = builders[{ pass, f.material, cell.first, cell.second }];
    Builder* B2 = (pass == BSPPASS_OPAQUE && f.disp && m.tex2.id) ? &builders[{ BSPPASS_BLEND2, f.material, cell.first, cell.second }] : nullptr;
    Builder* L = pass == BSPPASS_OPAQUE ? &builders[{ BSPPASS_LIGHTMAP, -1, cell.first, cell.second }] : nullptr;
    bool lit = f.lightofs >= 0 && slot[fi].x >= 0;
    auto emit = [&](const SrcFaceVert& v) {
      Color c{ 255, 255, 255, alpha };
      if (pass != BSPPASS_OPAQUE) {
        Vector l = (lit && !fullbright && !m.unlit) ? luxel(f, (int)(v.lu + 0.5f), (int)(v.lv + 0.5f)) : Vector(1, 1, 1);
        if (pass == BSPPASS_ADDITIVE) l = Vector(1, 1, 1);
        c = { U8(l.x * m.color.x), U8(l.y * m.color.y), U8(l.z * m.color.z), alpha };
      }
      B.Add(v.pos, v.u, v.v, c, pass != BSPPASS_OPAQUE);
      if (B2) B2->Add(v.pos, v.u, v.v, Color{ 255, 255, 255, U8(v.alpha) }, true);
      if (L) {
        float lu = 1.5f / AW, lv = 1.5f / AH;
        if (lit) { lu = (slot[fi].x + 1.5f + v.lu) / AW; lv = (slot[fi].y + 1.5f + v.lv) / AH; }
        L->Add(v.pos, lu, lv, Color{ 255, 255, 255, 255 }, false);
      }
    };
    // stored clockwise (front) -> emit counter-clockwise
    for (size_t k = 0; k + 2 < f.verts.size(); k += 3) { emit(f.verts[k]); emit(f.verts[k + 2]); emit(f.verts[k + 1]); tris++; }
  }
  for (auto& [key, B] : builders) {
    if (B.v.empty()) continue;
    BspChunk c; c.pass = std::get<0>(key); int mi = std::get<1>(key);
    Mesh mesh{}; mesh.vertexCount = (int)B.v.size() / 3; mesh.triangleCount = mesh.vertexCount / 3;
    mesh.vertices = (float*)MemAlloc((unsigned)(B.v.size() * sizeof(float))); std::memcpy(mesh.vertices, B.v.data(), B.v.size() * sizeof(float));
    mesh.texcoords = (float*)MemAlloc((unsigned)(B.uv.size() * sizeof(float))); std::memcpy(mesh.texcoords, B.uv.data(), B.uv.size() * sizeof(float));
    if (!B.col.empty()) { mesh.colors = (unsigned char*)MemAlloc((unsigned)B.col.size()); std::memcpy(mesh.colors, B.col.data(), B.col.size()); }
    UploadMesh(&mesh, false);
    c.mesh = mesh; c.material = LoadMaterialDefault();
    Texture2D tex = m_lightmap;
    if (mi >= 0) tex = c.pass == BSPPASS_BLEND2 ? mats[mi]->tex2 : mats[mi]->tex;
    c.material.maps[MATERIAL_MAP_DIFFUSE].texture = tex;
    if (mi >= 0 && c.pass == BSPPASS_OPAQUE) { const Vector& mc = mats[mi]->color; c.material.maps[MATERIAL_MAP_DIFFUSE].color = Color{ U8(mc.x), U8(mc.y), U8(mc.z), 255 }; }
    c.mins = B.mins; c.maxs = B.maxs;
    m_bspChunks.push_back(c);
  }
  // ---- static props
  int propTris = 0, missing = 0;
  for (const SrcStaticProp& sp : W.props) {
    CSrcModel* mdl = SrcMdl_Get(sp.model);
    if (!mdl || mdl->meshes.empty()) { missing++; continue; }
    mdl->UploadStatic();
    SrcPropInst pi; pi.model = mdl; pi.skin = sp.skin; pi.fadeMax = sp.fadeMax;
    matrix3x4_t xf; AngleMatrix(sp.angles, sp.origin, xf); pi.xform = ToRl(xf);
    Vector mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
    for (int k = 0; k < 8; k++) {
      Vector c((k & 1) ? mdl->hullMax.x : mdl->hullMin.x, (k & 2) ? mdl->hullMax.y : mdl->hullMin.y, (k & 4) ? mdl->hullMax.z : mdl->hullMin.z);
      Vector w = VectorTransform(c, xf); mn = VecMin(mn, w); mx = VecMax(mx, w);
    }
    pi.mins = mn; pi.maxs = mx;
    // light: the explicit lighting origin, else the surface right below the prop's top (sampling from the bbox
    // center often lands inside / under the prop itself and comes out black)
    Vector lin; Vector col(0.8f, 0.8f, 0.8f);
    bool got = false;
    if (sp.useLightingOrigin) got = W.LightPoint(sp.lightingOrigin + Vector(0, 0, 8), lin);
    if (!got) got = W.LightPoint(Vector((mn.x + mx.x) * 0.5f, (mn.y + mx.y) * 0.5f, mx.z + 4.f), lin);
    if (got) col = Gamma(lin) * 0.85f + Gamma(W.ambientColor * 0.25f) * 0.15f;
    col = VecMax(col, Vector(0.3f, 0.3f, 0.3f));
    if (fullbright) col = Vector(1, 1, 1);
    pi.tint = Color{ U8(col.x), U8(col.y), U8(col.z), 255 };
    for (auto& me : mdl->meshes) propTris += (int)me.idx.size() / 3;
    m_srcProps.push_back(pi);
  }
  // ---- sky
  static const char* suf[6] = { "rt", "bk", "lf", "ft", "up", "dn" };
  int skyOk = 0;
  for (int i = 0; i < 6; i++) {
    const SrcMaterial& sm = SrcMat_Find("skybox/" + W.skyName + suf[i]);
    if (sm.ok && sm.tex.id) { m_srcSky[i] = sm.tex; SetTextureWrap(m_srcSky[i], TEXTURE_WRAP_CLAMP); skyOk++; }
  }
  m_hasSky = skyOk == 6;
  numTris = tris; numVerts = tris * 3; numChunks = (int)m_bspChunks.size();
  bakeSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
  src = true;
  Msg("World (Source): %d tris, %d batches, lightmap %dx%d, %d props (%d tris, %d missing models), sky '%s' %s, materials %d/%d resolved, built in %.2fs\n",
      numTris, numChunks, AW, AH, (int)m_srcProps.size(), propTris, missing, W.skyName.c_str(), m_hasSky ? "ok" : "missing", matOk, matOk + matFallback, bakeSeconds);
}

void CWorldRenderer::DrawSrcSky(const CViewSetup& v) {
  static const int st_to_vec[6][3] = { { 3, -1, 2 }, { -3, 1, 2 }, { 1, 3, 2 }, { -1, -3, 2 }, { -2, -1, 3 }, { 2, -1, -3 } };
  static const int skytexorder[6] = { 0, 2, 1, 3, 4, 5 };
  rlDrawRenderBatchActive();
  rlDisableDepthMask(); rlDisableDepthTest(); rlDisableBackfaceCulling();
  const float R = 2000.f;
  for (int side = 0; side < 6; side++) {
    rlSetTexture(m_srcSky[skytexorder[side]].id);
    rlBegin(RL_QUADS);
    rlColor4ub(255, 255, 255, 255);
    static const float cs[4][2] = { { -1, -1 }, { -1, 1 }, { 1, 1 }, { 1, -1 } };
    for (int k = 0; k < 4; k++) {
      float s = cs[k][0], t = cs[k][1];
      float b[3] = { s * R, t * R, R }, vv[3];
      for (int j = 0; j < 3; j++) { int a = st_to_vec[side][j]; vv[j] = a < 0 ? -b[-a - 1] : b[a - 1]; }
      float u = (s + 1) * 0.5f, w = 1.f - (t + 1) * 0.5f;
      u = Clamp(u, 1.f / 512, 511.f / 512); w = Clamp(w, 1.f / 512, 511.f / 512);
      rlTexCoord2f(u, w); rlVertex3f(v.origin.x + vv[0], v.origin.y + vv[1], v.origin.z + vv[2]);
    }
    rlEnd();
  }
  rlSetTexture(0);
  rlDrawRenderBatchActive();
  rlEnableDepthMask(); rlEnableDepthTest(); rlEnableBackfaceCulling();
}

void CWorldRenderer::DrawSrc(const CViewSetup& v) {
  drawnChunks = 0;
  if (!r_drawworld.GetBool()) return;
  Frustum fr; fr.FromMatrix(v.viewProj);
  // 1. base textures
  for (auto& c : m_bspChunks) if (c.pass == BSPPASS_OPAQUE && !fr.CullBox(c.mins, c.maxs)) { DrawMesh(c.mesh, c.material, MatrixIdentity()); drawnChunks++; }
  // 2. displacement blend layer
  rlDrawRenderBatchActive();
  SetBlend(BK_ALPHA); rlDisableDepthMask();
  for (auto& c : m_bspChunks) if (c.pass == BSPPASS_BLEND2 && !fr.CullBox(c.mins, c.maxs)) { DrawMesh(c.mesh, c.material, MatrixIdentity()); drawnChunks++; }
  rlDrawRenderBatchActive(); rlEnableDepthMask();
  // 3. lightmaps
  SetBlend(BK_MODULATE2X); rlDisableDepthMask();
  for (auto& c : m_bspChunks) if (c.pass == BSPPASS_LIGHTMAP && !fr.CullBox(c.mins, c.maxs)) { DrawMesh(c.mesh, c.material, MatrixIdentity()); drawnChunks++; }
  SetBlend(BK_ALPHA); rlEnableDepthMask();
  // 4. static props
  struct Deferred { const SrcPropInst* p; const SrcMdlMesh* m; Texture2D t; float d; };
  std::vector<Deferred> transProps;
  if (r_drawprops.GetBool()) {
    Material& mat = PropMaterial();
    // projected size: radius / distance * (screen height / 2) / tan(vfov / 2)
    const float pxCull = r_propscreencull.GetFloat();
    const float pxScale = GetScreenHeight() * 0.5f / std::tan(DEG2RADF(v.VerticalFov(v.fov) * 0.5f)), pxScale2 = pxScale * pxScale;
    rlDisableBackfaceCulling();
    for (const SrcPropInst& p : m_srcProps) {
      if (fr.CullBox(p.mins, p.maxs)) continue;
      float d2 = (((p.mins + p.maxs) * 0.5f) - v.origin).LengthSqr();
      if (r_propfade.GetBool() && p.fadeMax > 0 && d2 > p.fadeMax * p.fadeMax) continue;
      if (pxCull > 0) { float rad = (p.maxs - p.mins).Length() * 0.5f; if (rad * rad * pxScale2 < pxCull * pxCull * d2) continue; }   // too small to see
      for (const SrcMdlMesh& m : p.model->meshes) {
        if (!m.uploaded) continue;
        bool trans = false, at = false;
        Texture2D t = p.model->MeshTexture(m, p.skin, &trans, &at);
        if (trans) { transProps.push_back({ &p, &m, t, d2 }); continue; }
        mat.maps[MATERIAL_MAP_DIFFUSE].texture = t; mat.maps[MATERIAL_MAP_DIFFUSE].color = p.tint;
        DrawMesh(m.gpu, mat, p.xform);
      }
      drawnChunks++;
    }
    rlEnableBackfaceCulling();
  }
  // 5. alpha tested brushes
  for (auto& c : m_bspChunks) if (c.pass == BSPPASS_ALPHATEST && !fr.CullBox(c.mins, c.maxs)) { DrawMesh(c.mesh, c.material, MatrixIdentity()); drawnChunks++; }
  // 6. translucent, back to front
  std::vector<BspChunk*> trans;
  for (auto& c : m_bspChunks) if ((c.pass == BSPPASS_TRANSLUCENT || c.pass == BSPPASS_ADDITIVE) && !fr.CullBox(c.mins, c.maxs)) { c.dist = (((c.mins + c.maxs) * 0.5f) - v.origin).LengthSqr(); trans.push_back(&c); }
  if (!trans.empty() || !transProps.empty()) {
    std::sort(trans.begin(), trans.end(), [](BspChunk* a, BspChunk* b) { return a->dist > b->dist; });
    rlDrawRenderBatchActive();
    rlDisableDepthMask(); rlDisableBackfaceCulling();
    Material& mat = PropMaterial();
    std::sort(transProps.begin(), transProps.end(), [](const Deferred& a, const Deferred& b) { return a.d > b.d; });
    for (auto& d : transProps) { mat.maps[MATERIAL_MAP_DIFFUSE].texture = d.t; mat.maps[MATERIAL_MAP_DIFFUSE].color = d.p->tint; DrawMesh(d.m->gpu, mat, d.p->xform); }
    for (BspChunk* c : trans) {
      SetBlend(c->pass == BSPPASS_ADDITIVE ? BK_ADDITIVE : BK_ALPHA);
      DrawMesh(c->mesh, c->material, MatrixIdentity()); drawnChunks++;
      SetBlend(BK_ALPHA);
    }
    rlEnableDepthMask(); rlEnableBackfaceCulling();
  }
}

LightSample CWorldRenderer::SampleLightSrc(const Vector& pos) {
  long long key = ((long long)std::floor(pos.x / 16.f) + 100000) * 400000000LL + ((long long)std::floor(pos.y / 16.f) + 100000) * 2000LL + ((long long)std::floor(pos.z / 16.f) + 1000);
  auto it = m_probeCache.find(key); if (it != m_probeCache.end()) return it->second;
  if (m_probeCache.size() > 200000) m_probeCache.clear();
  Vector c(0.6f, 0.6f, 0.6f), lin;
  if (g_SrcBsp.LightPoint(pos + Vector(0, 0, 4), lin)) c = Gamma(lin);
  if (mat_fullbright.GetBool()) c = Vector(1, 1, 1);
  LightSample s;
  s.ambient = c * 0.62f + Vector(0.05f, 0.05f, 0.05f);
  s.direct = c * 0.5f;
  s.dir = g_SrcBsp.sunDir;
  m_probeCache[key] = s;
  return s;
}
