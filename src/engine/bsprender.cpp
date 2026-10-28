// bsprender.cpp - renderer for original GoldSrc maps (CWorldRenderer, BSP part)
//
// Faces are batched per texture and 1024 unit cell. Lighting uses the map's own lightmaps (packed into one
// atlas) in a second multiplicative pass (dst * lightmap * 2), which works with the default raylib shader on real
// OpenGL and on the software renderer alike. '{' textures are alpha tested and lit per vertex, water and
// translucent brush entities (rendermode texture / additive) are drawn last, sorted back to front.
// The sky is the map's 6-sided skybox from gfx/env/<skyname>{rt,bk,lf,ft,up,dn}.tga.
#include "worldrender.hpp"
#include "bsp.hpp"
#include "hlfs.hpp"
#include "convar.hpp"
#include "../game/shared/mapinfo.hpp"
#include "rlgl.h"
#include "raymath.h"
#include <map>
#include <cstring>
#include <cmath>
#include <algorithm>
#include <chrono>
#include <tuple>

ConVar r_lightmap_gamma("r_lightmap_gamma", "0.62", FCVAR_ARCHIVE, "GoldSrc lightmap gamma (lower = brighter shadows), applied at map load", true, 0.2f, true, 1.5f);
ConVar r_lightmap_scale("r_lightmap_scale", "1.1", FCVAR_ARCHIVE, "GoldSrc lightmap brightness, applied at map load", true, 0.25f, true, 2.f);
ConVar r_bsp_lightmaps("r_bsp_lightmaps", "1", 0, "Draw the lightmap pass on original maps");
extern ConVar r_drawworld;
extern ConVar r_drawsky;
extern ConVar mat_fullbright;

#ifdef CS_HEADLESS
// software renderer: rlgl's GL 1.1 path ignores blend modes, talk to rlsw directly
extern "C" void swBlendFunc(int sfactor, int dfactor);
#endif
namespace {
enum BlendKind { BK_ALPHA, BK_ADDITIVE, BK_MODULATE2X };
void SetBlend(BlendKind k) {
  rlDrawRenderBatchActive();
#ifdef CS_HEADLESS
  if (k == BK_MODULATE2X) swBlendFunc(0x0306 /*DST_COLOR*/, 0x0300 /*SRC_COLOR*/);
  else if (k == BK_ADDITIVE) swBlendFunc(0x0302 /*SRC_ALPHA*/, 1 /*ONE*/);
  else swBlendFunc(0x0302 /*SRC_ALPHA*/, 0x0303 /*ONE_MINUS_SRC_ALPHA*/);
#else
  if (k == BK_MODULATE2X) { rlSetBlendFactors(RL_DST_COLOR, RL_SRC_COLOR, RL_FUNC_ADD); rlSetBlendMode(RL_BLEND_CUSTOM); }
  else rlSetBlendMode(k == BK_ADDITIVE ? RL_BLEND_ADDITIVE : RL_BLEND_ALPHA);
#endif
}
float LightCurve(float v) { return std::pow(std::max(0.f, v), r_lightmap_gamma.GetFloat()) * r_lightmap_scale.GetFloat(); }
Vector LightCurve(const Vector& c) { return { LightCurve(c.x), LightCurve(c.y), LightCurve(c.z) }; }

// minimal TGA reader (uncompressed / RLE, 24 or 32 bit, any origin) -> RGBA8 image
bool LoadTGA(const std::string& path, Image& out) {
  std::vector<uint8_t> d;
  { FILE* f = fopen(path.c_str(), "rb"); if (!f) return false; fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); d.resize(n); size_t r = fread(d.data(), 1, n, f); fclose(f); if ((long)r != n) return false; }
  if (d.size() < 18) return false;
  int idLen = d[0], cmapType = d[1], type = d[2];
  int w = d[12] | (d[13] << 8), h = d[14] | (d[15] << 8), bpp = d[16], desc = d[17];
  if (cmapType != 0 || (type != 2 && type != 10) || (bpp != 24 && bpp != 32) || w <= 0 || h <= 0) return false;
  size_t p = 18 + idLen; int bytes = bpp / 8;
  std::vector<uint8_t> px((size_t)w * h * 4);
  size_t total = (size_t)w * h, i = 0;
  auto put = [&](const uint8_t* s) { uint8_t* o = &px[i * 4]; o[0] = s[2]; o[1] = s[1]; o[2] = s[0]; o[3] = 255; i++; };   // sky TGAs often carry an all-zero alpha channel
  while (i < total) {
    if (type == 2) { if (p + bytes > d.size()) return false; put(&d[p]); p += bytes; continue; }
    if (p >= d.size()) return false;
    int c = d[p++]; int n = (c & 0x7f) + 1;
    if (c & 0x80) { if (p + bytes > d.size()) return false; for (int k = 0; k < n && i < total; k++) put(&d[p]); p += bytes; }
    else for (int k = 0; k < n && i < total; k++) { if (p + bytes > d.size()) return false; put(&d[p]); p += bytes; }
  }
  if (!(desc & 0x20)) { // bottom-up -> flip
    for (int y = 0; y < h / 2; y++) for (int x = 0; x < w * 4; x++) std::swap(px[(size_t)y * w * 4 + x], px[(size_t)(h - 1 - y) * w * 4 + x]);
  }
  out.width = w; out.height = h; out.mipmaps = 1; out.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  out.data = MemAlloc((unsigned)px.size()); std::memcpy(out.data, px.data(), px.size());
  return true;
}

Texture2D UploadRGBA(const uint8_t* rgba, int w, int h, bool mip, bool repeat) {
  Image im; im.data = (void*)rgba; im.width = w; im.height = h; im.mipmaps = 1; im.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  Image copy = ImageCopy(im);
#ifndef CS_HEADLESS
  if (mip) ImageMipmaps(&copy);
#endif
  Texture2D t = LoadTextureFromImage(copy);
  UnloadImage(copy);
#ifndef CS_HEADLESS
  SetTextureFilter(t, (mip && t.mipmaps > 1) ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR);
#else
  (void)mip;
#endif
  SetTextureWrap(t, repeat ? TEXTURE_WRAP_REPEAT : TEXTURE_WRAP_CLAMP);
  return t;
}

struct Builder {
  std::vector<float> v, uv; std::vector<unsigned char> col;
  Vector mins{ 1e9f, 1e9f, 1e9f }, maxs{ -1e9f, -1e9f, -1e9f };
  void Add(const Vector& p, float u, float t, Color c, bool withColor) {
    v.push_back(p.x); v.push_back(p.y); v.push_back(p.z); uv.push_back(u); uv.push_back(t);
    if (withColor) { col.push_back(c.r); col.push_back(c.g); col.push_back(c.b); col.push_back(c.a); }
    mins = VecMin(mins, p); maxs = VecMax(maxs, p);
  }
};
} // namespace

void CWorldRenderer::UnloadBsp() {
  for (auto& c : m_bspChunks) { UnloadMesh(c.mesh); MemFree(c.material.maps); }
  m_bspChunks.clear();
  for (auto& t : m_bspTextures) if (t.id) UnloadTexture(t);
  m_bspTextures.clear();
  if (m_lightmap.id) UnloadTexture(m_lightmap);
  m_lightmap = Texture2D{};
  for (auto& t : m_sky) { if (t.id) UnloadTexture(t); t = Texture2D{}; }
  m_hasSky = false; bsp = false;
}

void CWorldRenderer::BuildBsp() {
  auto t0 = std::chrono::steady_clock::now();
  const CBspWorld& W = g_Bsp;
  // ---- textures
  m_bspTextures.resize(W.textures.size());
  for (size_t i = 0; i < W.textures.size(); i++) {
    const BspTexture& t = W.textures[i];
    if (!t.rgba.empty()) m_bspTextures[i] = UploadRGBA(t.rgba.data(), t.w, t.h, true, true);
    else { // missing texture: grey checkerboard
      std::vector<uint8_t> px(64 * 64 * 4);
      for (int y = 0; y < 64; y++) for (int x = 0; x < 64; x++) { uint8_t c = ((x / 16 + y / 16) & 1) ? 150 : 105; uint8_t* o = &px[(y * 64 + x) * 4]; o[0] = o[1] = o[2] = c; o[3] = 255; }
      m_bspTextures[i] = UploadRGBA(px.data(), 64, 64, true, true);
    }
  }
  // ---- lightmap atlas (1 luxel border around every face so filtering never bleeds)
  struct Slot { int x = 0, y = 0; };
  std::vector<Slot> slot(W.faces.size());
  std::vector<int> order;
  long long area = 0;
  for (int i = 0; i < (int)W.faces.size(); i++) if (W.faces[i].lightofs >= 0) { order.push_back(i); area += (long long)(W.faces[i].lmW + 2) * (W.faces[i].lmH + 2); }
  std::sort(order.begin(), order.end(), [&](int a, int b) { return W.faces[a].lmH > W.faces[b].lmH; });
  int AW = 256; while ((long long)AW * AW < area * 5 / 4 + 64 && AW < 4096) AW *= 2;
  int x = 3, y = 0, rowH = 3;   // the first 3x3 texels are a neutral "fullbright" block
  for (int fi : order) {
    const BspFace& f = W.faces[fi]; int w = f.lmW + 2, h = f.lmH + 2;
    if (x + w > AW) { x = 0; y += rowH; rowH = 0; }
    slot[fi] = { x, y }; x += w; rowH = std::max(rowH, h);
  }
  int AH = 16; while (AH < y + rowH) AH *= 2;
  std::vector<uint8_t> atlas((size_t)AW * AH * 4, 0);
  auto putLux = [&](int px, int py, const Vector& c) {
    if (px < 0 || py < 0 || px >= AW || py >= AH) return;
    uint8_t* o = &atlas[((size_t)py * AW + px) * 4];
    // stored at half intensity: the multiply pass doubles it (overbright up to 2x)
    o[0] = (uint8_t)Clamp(c.x * 127.5f, 0.f, 255.f); o[1] = (uint8_t)Clamp(c.y * 127.5f, 0.f, 255.f); o[2] = (uint8_t)Clamp(c.z * 127.5f, 0.f, 255.f); o[3] = 255;
  };
  bool fullbright = mat_fullbright.GetBool();
  for (int yy = 0; yy < 3; yy++) for (int xx = 0; xx < 3; xx++) putLux(xx, yy, Vector(1, 1, 1));
  for (int fi : order) {
    const BspFace& f = W.faces[fi];
    int ns = 0; for (int s = 0; s < 4; s++) if (f.styles[s] != 255) ns++;
    ns = std::max(ns, 1);
    for (int ty = -1; ty <= f.lmH; ty++) for (int tx = -1; tx <= f.lmW; tx++) {
      int sx = Clamp(tx, 0, f.lmW - 1), sy = Clamp(ty, 0, f.lmH - 1);
      Vector c;
      for (int s = 0; s < ns; s++) { const uint8_t* q = &W.lighting[f.lightofs + ((size_t)s * f.lmW * f.lmH + sy * f.lmW + sx) * 3]; c += Vector(q[0], q[1], q[2]); }
      c = fullbright ? Vector(1, 1, 1) : LightCurve(c / 255.f);
      putLux(slot[fi].x + 1 + tx, slot[fi].y + 1 + ty, c);
    }
  }
  m_lightmap = UploadRGBA(atlas.data(), AW, AH, false, false);

  // ---- geometry
  std::map<std::tuple<int, int, int, int>, Builder> builders;   // (pass, texture, cellx, celly)
  auto cellOf = [](const Vector& p) { return std::make_pair((int)std::floor(p.x / 1024.f), (int)std::floor(p.y / 1024.f)); };
  int numTrisOut = 0;
  for (int fi = 0; fi < (int)W.faces.size(); fi++) {
    const BspFace& f = W.faces[fi];
    if (f.numVerts < 3) continue;
    const BspTexInfo& ti = W.texinfo[f.texinfo];
    int texIdx = ti.miptex; if (texIdx < 0 || texIdx >= (int)W.textures.size()) continue;
    const BspTexture& tex = W.textures[texIdx];
    if (tex.sky || tex.nodraw) continue;
    matrix3x4_t xf; const BspBrushEntity* be = nullptr;
    if (f.model > 0) {
      int bi = f.model < (int)W.modelToBrushEnt.size() ? W.modelToBrushEnt[f.model] : -1;
      if (bi < 0) continue;
      be = &W.brushEnts[bi]; if (!be->visible) continue;
      xf = be->xform;
    }
    int pass = BSPPASS_OPAQUE; unsigned char alpha = 255;
    if (be && (be->rendermode == 1 || be->rendermode == 2)) { pass = BSPPASS_TRANSLUCENT; alpha = (unsigned char)Clamp(be->renderamt, 0.f, 255.f); }
    else if (be && (be->rendermode == 3 || be->rendermode == 5)) { pass = BSPPASS_ADDITIVE; alpha = (unsigned char)Clamp(be->renderamt, 0.f, 255.f); }
    else if (tex.water) { pass = BSPPASS_TRANSLUCENT; alpha = 170; }
    else if (tex.alphaTest || (be && be->rendermode == 4)) pass = BSPPASS_ALPHATEST;
    if (pass == BSPPASS_TRANSLUCENT && alpha >= 250 && !tex.water) pass = tex.alphaTest ? BSPPASS_ALPHATEST : BSPPASS_OPAQUE;
    Vector center; for (int v = 0; v < f.numVerts; v++) center += VectorTransform(W.faceVerts[f.firstVert + v], xf); center /= (float)f.numVerts;
    auto cell = cellOf(center);
    Builder& B = builders[{ pass, texIdx, cell.first, cell.second }];
    Builder* L = pass == BSPPASS_OPAQUE ? &builders[{ BSPPASS_LIGHTMAP, -1, cell.first, cell.second }] : nullptr;
    float tw = (float)std::max(1, tex.w), th = (float)std::max(1, tex.h);
    auto emit = [&](int vi) {
      const Vector& lp = W.faceVerts[f.firstVert + vi];
      Vector wp = VectorTransform(lp, xf);
      float s = lp.x * ti.vecs[0][0] + lp.y * ti.vecs[0][1] + lp.z * ti.vecs[0][2] + ti.vecs[0][3];
      float t = lp.x * ti.vecs[1][0] + lp.y * ti.vecs[1][1] + lp.z * ti.vecs[1][2] + ti.vecs[1][3];
      Color c{ 255, 255, 255, alpha };
      if (pass == BSPPASS_ALPHATEST) {
        Vector l = f.lightofs >= 0 && !fullbright ? LightCurve(W.SampleFaceLight(fi, lp)) : Vector(1, 1, 1);
        c = { (unsigned char)Clamp(l.x * 255.f, 0.f, 255.f), (unsigned char)Clamp(l.y * 255.f, 0.f, 255.f), (unsigned char)Clamp(l.z * 255.f, 0.f, 255.f), 255 };
      } else if (be && be->rendermode == 1) c = { (unsigned char)be->rendercolor.x, (unsigned char)be->rendercolor.y, (unsigned char)be->rendercolor.z, alpha };
      B.Add(wp, s / tw, t / th, c, pass != BSPPASS_OPAQUE);
      if (L) {
        float lu, lv;
        if (f.lightofs >= 0) { lu = (slot[fi].x + 1.5f + (s - f.texMins[0]) / 16.f) / AW; lv = (slot[fi].y + 1.5f + (t - f.texMins[1]) / 16.f) / AH; }
        else { lu = 1.5f / AW; lv = 1.5f / AH; }
        L->Add(wp, lu, lv, Color{ 255, 255, 255, 255 }, false);
      }
    };
    // GoldSrc windings are clockwise seen from the front -> reverse for counter-clockwise front faces
    for (int k = 1; k + 1 < f.numVerts; k++) { emit(0); emit(k + 1); emit(k); numTrisOut++; }
  }
  for (auto& [key, B] : builders) {
    if (B.v.empty()) continue;
    BspChunk c; c.pass = std::get<0>(key);
    int texIdx = std::get<1>(key);
    Mesh m{}; m.vertexCount = (int)B.v.size() / 3; m.triangleCount = m.vertexCount / 3;
    m.vertices = (float*)MemAlloc((unsigned)(B.v.size() * sizeof(float))); std::memcpy(m.vertices, B.v.data(), B.v.size() * sizeof(float));
    m.texcoords = (float*)MemAlloc((unsigned)(B.uv.size() * sizeof(float))); std::memcpy(m.texcoords, B.uv.data(), B.uv.size() * sizeof(float));
    if (!B.col.empty()) { m.colors = (unsigned char*)MemAlloc((unsigned)B.col.size()); std::memcpy(m.colors, B.col.data(), B.col.size()); }
    UploadMesh(&m, false);
    c.mesh = m; c.material = LoadMaterialDefault();
    c.material.maps[MATERIAL_MAP_DIFFUSE].texture = texIdx >= 0 ? m_bspTextures[texIdx] : m_lightmap;
    c.mins = B.mins; c.maxs = B.maxs;
    m_bspChunks.push_back(c);
  }
  // ---- sky box
  static const char* suf[6] = { "rt", "bk", "lf", "ft", "up", "dn" };
  int skyOk = 0;
  for (int i = 0; i < 6; i++) {
    std::string p = hlfs::Find("gfx/env/" + W.skyName + suf[i] + ".tga");
    Image im{};
    if (!p.empty() && LoadTGA(p, im)) { m_sky[i] = UploadRGBA((const uint8_t*)im.data, im.width, im.height, false, false); UnloadImage(im); skyOk++; }
  }
  m_hasSky = skyOk == 6;
  numTris = numTrisOut; numVerts = numTrisOut * 3; numChunks = (int)m_bspChunks.size();
  bakeSeconds = std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count();
  bsp = true;
  Msg("World (BSP): %d tris, %d batches, lightmap atlas %dx%d, sky '%s' %s, built in %.2fs\n", numTris, numChunks, AW, AH, W.skyName.c_str(), m_hasSky ? "ok" : "missing", bakeSeconds);
}

void CWorldRenderer::DrawBspSky(const CViewSetup& v) {
  // Quake 2 / GoldSrc skybox layout: side -> (s, t, depth) axes, image order rt bk lf ft up dn
  static const int st_to_vec[6][3] = { { 3, -1, 2 }, { -3, 1, 2 }, { 1, 3, 2 }, { -1, -3, 2 }, { -2, -1, 3 }, { 2, -1, -3 } };
  static const int skytexorder[6] = { 0, 2, 1, 3, 4, 5 };
  rlDrawRenderBatchActive();
  rlDisableDepthMask(); rlDisableDepthTest(); rlDisableBackfaceCulling();
  const float R = 2000.f;
  for (int side = 0; side < 6; side++) {
    rlSetTexture(m_sky[skytexorder[side]].id);
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

void CWorldRenderer::DrawBsp(const CViewSetup& v) {
  drawnChunks = 0;
  if (!r_drawworld.GetBool()) return;
  Frustum fr; fr.FromMatrix(v.viewProj);
  std::vector<BspChunk*> trans;
  // 1. textures
  for (auto& c : m_bspChunks) if (c.pass == BSPPASS_OPAQUE && !fr.CullBox(c.mins, c.maxs)) { DrawMesh(c.mesh, c.material, MatrixIdentity()); drawnChunks++; }
  // 2. lightmaps: dst = dst * lightmap * 2
  rlDrawRenderBatchActive();
  if (r_bsp_lightmaps.GetBool()) {
  SetBlend(BK_MODULATE2X);
  rlDisableDepthMask();
  for (auto& c : m_bspChunks) if (c.pass == BSPPASS_LIGHTMAP && !fr.CullBox(c.mins, c.maxs)) { DrawMesh(c.mesh, c.material, MatrixIdentity()); drawnChunks++; }
  SetBlend(BK_ALPHA);
  rlEnableDepthMask();
  }
  // 3. alpha tested (fences, foliage) lit per vertex
  for (auto& c : m_bspChunks) if (c.pass == BSPPASS_ALPHATEST && !fr.CullBox(c.mins, c.maxs)) { DrawMesh(c.mesh, c.material, MatrixIdentity()); drawnChunks++; }
  // 4. water / translucent / additive brush entities, back to front
  for (auto& c : m_bspChunks) if ((c.pass == BSPPASS_TRANSLUCENT || c.pass == BSPPASS_ADDITIVE) && !fr.CullBox(c.mins, c.maxs)) { c.dist = (((c.mins + c.maxs) * 0.5f) - v.origin).LengthSqr(); trans.push_back(&c); }
  if (!trans.empty()) {
    std::sort(trans.begin(), trans.end(), [](BspChunk* a, BspChunk* b) { return a->dist > b->dist; });
    rlDrawRenderBatchActive();
    rlDisableDepthMask(); rlDisableBackfaceCulling();
    for (BspChunk* c : trans) {
      SetBlend(c->pass == BSPPASS_ADDITIVE ? BK_ADDITIVE : BK_ALPHA);
      DrawMesh(c->mesh, c->material, MatrixIdentity()); drawnChunks++;
      SetBlend(BK_ALPHA);
    }
    rlEnableDepthMask(); rlEnableBackfaceCulling();
  }
}

LightSample CWorldRenderer::SampleLightBsp(const Vector& pos) {
  long long key = ((long long)std::floor(pos.x / 16.f) + 100000) * 400000000LL + ((long long)std::floor(pos.y / 16.f) + 100000) * 2000LL + ((long long)std::floor(pos.z / 16.f) + 1000);
  auto it = m_probeCache.find(key); if (it != m_probeCache.end()) return it->second;
  if (m_probeCache.size() > 200000) m_probeCache.clear();
  Vector c(0.55f, 0.55f, 0.55f);
  if (g_Bsp.LightPoint(pos + Vector(0, 0, 4), c)) c = LightCurve(c);
  if (mat_fullbright.GetBool()) c = Vector(1, 1, 1);
  LightSample s;
  s.ambient = c * 0.62f + Vector(0.05f, 0.05f, 0.05f);
  s.direct = c * 0.5f;
  s.dir = g_Bsp.sunDir;
  m_probeCache[key] = s;
  return s;
}
