// srcbsp.cpp - VBSP loader
#include "srcbsp.hpp"
#include "srcfs.hpp"
#include "srcmaterial.hpp"
#include "srcmdl.hpp"
#include "enginetrace.hpp"
#include "convar.hpp"
#include <cstring>
#include <cmath>
#include <algorithm>
#include <functional>

namespace {
enum {
  LUMP_ENTITIES = 0, LUMP_PLANES = 1, LUMP_TEXDATA = 2, LUMP_VERTEXES = 3, LUMP_NODES = 5, LUMP_TEXINFO = 6, LUMP_FACES = 7,
  LUMP_LIGHTING = 8, LUMP_LEAFS = 10, LUMP_EDGES = 12, LUMP_SURFEDGES = 13, LUMP_MODELS = 14, LUMP_LEAFFACES = 16,
  LUMP_LEAFBRUSHES = 17, LUMP_BRUSHES = 18, LUMP_BRUSHSIDES = 19, LUMP_DISPINFO = 26, LUMP_DISP_VERTS = 33, LUMP_GAME_LUMP = 35,
  LUMP_PAKFILE = 40, LUMP_TEXDATA_STRING_DATA = 43, LUMP_TEXDATA_STRING_TABLE = 44, LUMP_LIGHTING_HDR = 53,
};
enum { SURF_SKY2D_ = 0x2, SURF_SKY_ = 0x4, SURF_NODRAW_ = 0x80, SURF_HINT_ = 0x100, SURF_SKIP_ = 0x200, SURF_NOLIGHT_ = 0x400, SURF_BUMPLIGHT_ = 0x800, SURF_TRIGGER_ = 0x40 };
// Source contents bits we care about (same values as our enginetrace)
constexpr int SRC_CONTENTS_SOLID = 0x1, SRC_CONTENTS_WINDOW = 0x2, SRC_CONTENTS_GRATE = 0x8, SRC_CONTENTS_WATER = 0x20, SRC_CONTENTS_SLIME = 0x10,
              SRC_CONTENTS_MOVEABLE = 0x4000, SRC_CONTENTS_PLAYERCLIP = 0x10000, SRC_CONTENTS_MONSTERCLIP = 0x20000;

struct Lump { int ofs, len, ver; };
template <class T> T Rd(const uint8_t* p) { T v; std::memcpy(&v, p, sizeof(T)); return v; }

void ParseEntities(const char* s, size_t n, std::vector<BspEntity>& out) {
  size_t i = 0;
  auto skipWs = [&] { while (i < n && (unsigned char)s[i] <= ' ') i++; };
  auto readQ = [&](std::string& o) { o.clear(); if (i >= n || s[i] != '"') return false; i++; while (i < n && s[i] != '"') o += s[i++]; if (i < n) i++; return true; };
  while (i < n) {
    skipWs(); if (i >= n) break;
    if (s[i] != '{') { i++; continue; }
    i++;
    BspEntity e; std::string k, v;
    for (;;) {
      skipWs(); if (i >= n) break;
      if (s[i] == '}') { i++; break; }
      if (!readQ(k)) { i++; continue; }
      skipWs(); if (!readQ(v)) break;
      if (!e.kv.count(k)) e.kv[k] = v;    // first wins (Source outputs repeat keys like "OnTrigger")
    }
    out.push_back(std::move(e));
  }
}
} // namespace

Vector CSrcBsp::DecodeLuxel(const uint8_t* c) {
  int e = (int8_t)c[3];
  float s = std::ldexp(1.f, e) / 255.f;
  return { c[0] * s, c[1] * s, c[2] * s };
}

// convex hull (point cloud of a convex solid) -> brush with face planes + axial bevels
bool CSrcBsp::AddConvexHull(const std::vector<Vector>& pts, int surf) {
  if (pts.size() < 4) return false;
  Vector mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f), ctr;
  for (auto& p : pts) { mn = VecMin(mn, p); mx = VecMax(mx, p); ctr += p; }
  ctr /= (float)pts.size();
  if (mx.x - mn.x < 0.5f || mx.y - mn.y < 0.5f || mx.z - mn.z < 0.5f) return false;
  // garbage from a misparsed .phy ledge (huge / non-finite coordinates): such a "hull" would get an absurd bounding box
  for (int a = 0; a < 3; a++) if (!std::isfinite(mn[a]) || !std::isfinite(mx[a]) || std::fabs(mn[a]) > 32768.f || std::fabs(mx[a]) > 32768.f || mx[a] - mn[a] > 8192.f) return false;
  std::vector<cplane_t> pl;
  auto addPlane = [&](Vector n, float d) {
    for (auto& q : pl) if (q.normal.Dot(n) > 0.999f && std::fabs(q.dist - d) < 0.5f) return;
    cplane_t c; c.normal = n; c.dist = d; pl.push_back(c);
  };
  // brute force hull faces: every triple whose plane has all points behind it (point counts per ledge are small)
  const size_t n = std::min<size_t>(pts.size(), 64);
  for (size_t i = 0; i < n; i++) for (size_t j = i + 1; j < n; j++) for (size_t k = j + 1; k < n; k++) {
    Vector nn = (pts[j] - pts[i]).Cross(pts[k] - pts[i]); float l = nn.Length(); if (l < 1e-3f) continue; nn /= l;
    float d = nn.Dot(pts[i]);
    if (nn.Dot(ctr) > d) { nn = nn * -1.f; d = -d; }
    bool ok = true; for (size_t q = 0; q < n && ok; q++) if (nn.Dot(pts[q]) - d > 0.1f) ok = false;
    if (ok) addPlane(nn, d);
  }
  for (int a = 0; a < 3; a++) { Vector e(0, 0, 0); e[a] = 1; addPlane(e, mx[a]); addPlane(e * -1.f, -mn[a]); }
  if (pl.size() < 6) return false;
  g_EngineTrace.AddBrush(pl, mn, mx, CONTENTS_SOLID, surf, 0, false);
  return true;
}

// .phy (IVP compact surfaces): every ledge is a convex piece
void CSrcBsp::LoadPhyHulls(const std::string& model, std::vector<std::vector<Vector>>& out) {
  std::string base = model.size() > 4 ? model.substr(0, model.size() - 4) : model;
  std::vector<uint8_t> d; if (!srcfs::Read(base + ".phy", d) || d.size() < 16) return;
  auto I = [&](size_t o) { int v = 0; if (o + 4 <= d.size()) std::memcpy(&v, &d[o], 4); return v; };
  int hdr = I(0), solids = I(8);
  size_t p = (size_t)hdr;
  for (int s = 0; s < solids && p + 4 <= d.size(); s++) {
    int solidSize = I(p); size_t solidStart = p + 4, solidEnd = solidStart + (size_t)solidSize;
    if (solidEnd > d.size()) break;
    size_t surf = solidStart;
    if (I(solidStart) == (('Y' << 24) | ('H' << 16) | ('P' << 8) | 'V')) surf = solidStart + 28;   // "VPHY" header
    int ledgeTreeRoot = I(surf + 32);
    size_t lp = surf + 48, lend = ledgeTreeRoot > 0 ? surf + ledgeTreeRoot : solidEnd;
    for (int guard = 0; lp + 16 <= lend && lp + 16 <= solidEnd && guard < 4096; guard++) {
      int pointOff = I(lp); uint32_t sizeFlags = (uint32_t)I(lp + 8); int16_t ntri; std::memcpy(&ntri, &d[lp + 12], 2);
      size_t size = (size_t)(sizeFlags >> 8) * 16;
      if (size < 16 || ntri <= 0) break;
      std::vector<int> idx;
      for (int t = 0; t < ntri; t++) for (int e = 0; e < 3; e++) {
        size_t eo = lp + 16 + (size_t)t * 16 + 4 + e * 4; if (eo + 4 > d.size()) break;
        int pi = I(eo) & 0xffff; if (std::find(idx.begin(), idx.end(), pi) == idx.end()) idx.push_back(pi);
      }
      std::vector<Vector> hull;
      for (int pi : idx) {
        size_t po = lp + pointOff + (size_t)pi * 16; if (po + 12 > d.size()) continue;
        float x, y, z; std::memcpy(&x, &d[po], 4); std::memcpy(&y, &d[po + 4], 4); std::memcpy(&z, &d[po + 8], 4);
        const float k = 39.3700787f;
        hull.push_back(Vector(x * k, z * k, -y * k));
      }
      if (hull.size() >= 4) out.push_back(hull);
      lp += size;
    }
    p = solidEnd;
  }
}

void CSrcBsp::Unload() {
  *this = CSrcBsp();
  srcfs::UnmountPak();
}

bool CSrcBsp::Load(const std::string& mapName) {
  Unload();
  std::vector<uint8_t> file;
  if (!srcfs::Read("maps/" + mapName + ".bsp", file) || file.size() < 1036) return false;
  const uint8_t* d = file.data();
  if (std::memcmp(d, "VBSP", 4) != 0) { Warning("%s: not a Source BSP\n", mapName.c_str()); return false; }
  version = Rd<int>(d + 4);
  if (version < 19 || version > 21) { Warning("%s: unsupported VBSP version %d\n", mapName.c_str(), version); return false; }
  Lump L[64];
  for (int i = 0; i < 64; i++) {
    const uint8_t* h = d + 8 + i * 16;
    L[i] = { Rd<int>(h), Rd<int>(h + 4), Rd<int>(h + 8) };
    if (L[i].ofs < 0 || L[i].len < 0 || (size_t)L[i].ofs + L[i].len > file.size()) L[i] = { 0, 0, 0 };
  }
  if (version == 21) {
    // v21 (L4D2-era) swaps the lump header field order to version, ofs, len - detect by sanity
    bool bad = false; for (int i = 0; i < 64; i++) if (L[i].len && L[i].ofs < 1036) bad = true;
    if (bad) for (int i = 0; i < 64; i++) { const uint8_t* h = d + 8 + i * 16; L[i] = { Rd<int>(h + 4), Rd<int>(h + 8), Rd<int>(h) }; }
  }
  name = mapName;
  auto lump = [&](int i) { return d + L[i].ofs; };
  auto count = [&](int i, int sz) { return L[i].len / sz; };

  // pakfile first: materials referenced below may live inside the map
  if (L[LUMP_PAKFILE].len > 0) srcfs::MountPak(std::vector<uint8_t>(lump(LUMP_PAKFILE), lump(LUMP_PAKFILE) + L[LUMP_PAKFILE].len));

  ParseEntities((const char*)lump(LUMP_ENTITIES), L[LUMP_ENTITIES].len, entities);
  for (auto& e : entities) if (e.Class() == "worldspawn") skyName = e.Get("skyname", "sky_dust");

  // planes
  for (int i = 0, n = count(LUMP_PLANES, 20); i < n; i++) { const uint8_t* p = lump(LUMP_PLANES) + i * 20; m_planes.push_back({ Rd<Vector>(p), Rd<float>(p + 12) }); }
  // texdata names
  std::vector<int> strTable; for (int i = 0, n = count(LUMP_TEXDATA_STRING_TABLE, 4); i < n; i++) strTable.push_back(Rd<int>(lump(LUMP_TEXDATA_STRING_TABLE) + i * 4));
  const char* strData = (const char*)lump(LUMP_TEXDATA_STRING_DATA); int strLen = L[LUMP_TEXDATA_STRING_DATA].len;
  for (int i = 0, n = count(LUMP_TEXDATA, 32); i < n; i++) {
    const uint8_t* p = lump(LUMP_TEXDATA) + i * 32;
    int sid = Rd<int>(p + 12);
    std::string nm;
    if (sid >= 0 && sid < (int)strTable.size() && strTable[sid] >= 0 && strTable[sid] < strLen) nm = std::string(strData + strTable[sid], strnlen(strData + strTable[sid], strLen - strTable[sid]));
    materials.push_back(srcfs::Normalize(nm)); matWidth.push_back(Rd<int>(p + 16)); matHeight.push_back(Rd<int>(p + 20));
  }
  for (int i = 0, n = count(LUMP_VERTEXES, 12); i < n; i++) m_verts.push_back(Rd<Vector>(lump(LUMP_VERTEXES) + i * 12));
  for (int i = 0, n = count(LUMP_TEXINFO, 72); i < n; i++) {
    const uint8_t* p = lump(LUMP_TEXINFO) + i * 72; TexInfo t;
    std::memcpy(t.tv, p, 32); std::memcpy(t.lv, p + 32, 32); t.flags = Rd<int>(p + 64); t.texdata = Rd<int>(p + 68);
    m_texinfo.push_back(t);
  }
  for (int i = 0, n = count(LUMP_NODES, 32); i < n; i++) {
    const uint8_t* p = lump(LUMP_NODES) + i * 32;
    m_nodes.push_back({ Rd<int>(p), { Rd<int>(p + 4), Rd<int>(p + 8) }, Rd<uint16_t>(p + 24), Rd<uint16_t>(p + 26) });
  }
  std::vector<uint16_t> edges; for (int i = 0, n = count(LUMP_EDGES, 4); i < n; i++) { edges.push_back(Rd<uint16_t>(lump(LUMP_EDGES) + i * 4)); edges.push_back(Rd<uint16_t>(lump(LUMP_EDGES) + i * 4 + 2)); }
  std::vector<int> surfedges; for (int i = 0, n = count(LUMP_SURFEDGES, 4); i < n; i++) surfedges.push_back(Rd<int>(lump(LUMP_SURFEDGES) + i * 4));
  // lighting (LDR preferred, HDR as fallback)
  int lightLump = L[LUMP_LIGHTING].len > 0 ? LUMP_LIGHTING : LUMP_LIGHTING_HDR;
  lighting.assign(lump(lightLump), lump(lightLump) + L[lightLump].len);
  // faces
  for (int i = 0, n = count(LUMP_FACES, 56); i < n; i++) {
    const uint8_t* p = lump(LUMP_FACES) + i * 56; FaceRaw f;
    f.plane = Rd<uint16_t>(p); f.side = p[2]; f.firstEdge = Rd<int>(p + 4); f.numEdges = Rd<int16_t>(p + 8);
    f.texinfo = Rd<int16_t>(p + 10); f.dispinfo = Rd<int16_t>(p + 12); std::memcpy(f.styles, p + 16, 4); f.lightofs = Rd<int>(p + 20);
    f.lmMins[0] = Rd<int>(p + 28); f.lmMins[1] = Rd<int>(p + 32); f.lmSize[0] = Rd<int>(p + 36); f.lmSize[1] = Rd<int>(p + 40);
    m_rawFaces.push_back(f);
  }
  // models
  struct Model { Vector mins, maxs, origin; int headnode, firstFace, numFaces; };
  std::vector<Model> models;
  for (int i = 0, n = count(LUMP_MODELS, 48); i < n; i++) {
    const uint8_t* p = lump(LUMP_MODELS) + i * 48;
    models.push_back({ Rd<Vector>(p), Rd<Vector>(p + 12), Rd<Vector>(p + 24), Rd<int>(p + 36), Rd<int>(p + 40), Rd<int>(p + 44) });
  }
  if (models.empty()) return false;
  for (auto& m : models) modelBounds.push_back({ m.mins, m.maxs });
  worldMins = models[0].mins; worldMaxs = models[0].maxs;

  // ---- brush entities
  std::vector<int> modelEnt(models.size(), -1);
  for (int ei = 0; ei < (int)entities.size(); ei++) {
    int mi = entities[ei].ModelIndex(); if (mi <= 0 || mi >= (int)models.size()) continue;
    modelEnt[mi] = ei;
    const BspEntity& e = entities[ei];
    SrcBrushEnt be; be.model = mi; be.entity = ei; be.classname = e.Class();
    be.origin = e.GetVector("origin"); be.angles = QAngle(0, 0, 0);
    if (e.Has("angles")) { Vector a = e.GetVector("angles"); be.angles = QAngle(a.x, a.y, a.z); }
    be.rendermode = e.GetInt("rendermode", 0); be.renderamt = e.GetFloat("renderamt", 255);
    const std::string& c = be.classname;
    bool trigger = c.rfind("trigger_", 0) == 0 || c == "func_buyzone" || c == "func_bomb_target" || c == "func_hostage_rescue" || c.rfind("func_areaportal", 0) == 0 ||
                   c == "func_occluder" || c == "func_precipitation" || c == "func_ladder" || c == "func_clip_vphysics" || c == "func_dustmotes" ||
                   c == "func_dustcloud" || c == "func_smokevolume" || c == "func_nobuild" || c == "func_no_defuse" || c == "func_viscluster" || c == "func_lod";
    be.visible = !trigger && be.rendermode != 10 && !(e.GetInt("spawnflags") & 0 /*start invisible flags vary*/);
    be.solid = !trigger && c != "func_illusionary" && !(c == "func_brush" && e.GetInt("Solidity", 0) == 1) && c != "func_door" && c != "func_door_rotating" &&
               c != "func_water_analog" && c != "func_movelinear" && c != "func_breakable_surf";
    // doors: drawn where they are, but never block (we have no door logic) - same as our GoldSrc maps
    if (c == "func_breakable_surf") be.solid = false;     // windows break instantly in CS - shoot/walk through
    if (c == "func_breakable") be.solid = true;
    brushEnts.push_back(be);
  }
  auto entOfModel = [&](int mi) -> const SrcBrushEnt* { for (auto& b : brushEnts) if (b.model == mi) return &b; return nullptr; };

  // ---- displacement info
  struct DispInfo { Vector start; int vertStart, power; int mapFace; };
  std::vector<DispInfo> disps;
  for (int i = 0, n = count(LUMP_DISPINFO, 176); i < n; i++) {
    const uint8_t* p = lump(LUMP_DISPINFO) + i * 176;
    disps.push_back({ Rd<Vector>(p), Rd<int>(p + 12), Rd<int>(p + 20), Rd<uint16_t>(p + 36) });
  }
  struct DispVert { Vector vec; float dist, alpha; };
  std::vector<DispVert> dverts;
  for (int i = 0, n = count(LUMP_DISP_VERTS, 20); i < n; i++) { const uint8_t* p = lump(LUMP_DISP_VERTS) + i * 20; dverts.push_back({ Rd<Vector>(p), Rd<float>(p + 12), Rd<float>(p + 16) }); }

  // ---- render faces
  std::vector<Vector> dispTriPos;   // for collision (triples)
  auto faceVerts = [&](const FaceRaw& f, std::vector<Vector>& out) {
    out.clear();
    for (int k = 0; k < f.numEdges; k++) {
      int se = f.firstEdge + k; if (se < 0 || se >= (int)surfedges.size()) break;
      int e = surfedges[se]; int vi = e >= 0 ? edges[e * 2] : edges[-e * 2 + 1];
      if (vi < (int)m_verts.size()) out.push_back(m_verts[vi]);
    }
  };
  std::vector<Vector> w;
  for (int mi = 0; mi < (int)models.size(); mi++) {
    const SrcBrushEnt* be = mi ? entOfModel(mi) : nullptr;
    if (mi && (!be || !be->visible)) continue;
    matrix3x4_t xf; if (be) AngleMatrix(be->angles, be->origin, xf);
    for (int fi = models[mi].firstFace; fi < models[mi].firstFace + models[mi].numFaces && fi < (int)m_rawFaces.size(); fi++) {
      const FaceRaw& f = m_rawFaces[fi];
      if (f.texinfo < 0 || f.texinfo >= (int)m_texinfo.size()) continue;
      const TexInfo& ti = m_texinfo[f.texinfo];
      if (ti.flags & (SURF_SKY_ | SURF_SKY2D_ | SURF_NODRAW_ | SURF_HINT_ | SURF_SKIP_ | SURF_TRIGGER_)) continue;
      if (ti.texdata < 0 || ti.texdata >= (int)materials.size()) continue;
      const std::string& mat = materials[ti.texdata];
      if (mat.rfind("tools/", 0) == 0) continue;
      faceVerts(f, w); if (w.size() < 3) continue;
      SrcFace sf; sf.material = ti.texdata; sf.model = mi; sf.texinfo = f.texinfo; sf.lightofs = (ti.flags & SURF_NOLIGHT_) ? -1 : f.lightofs;
      sf.lmMins[0] = f.lmMins[0]; sf.lmMins[1] = f.lmMins[1]; sf.lmSize[0] = f.lmSize[0]; sf.lmSize[1] = f.lmSize[1];
      std::memcpy(sf.styles, f.styles, 4); sf.bump = (ti.flags & SURF_BUMPLIGHT_) != 0;
      Vector n = m_planes[f.plane].n; if (f.side) n = n * -1.f;
      sf.normal = be ? VectorRotate(n, xf) : n;
      float tw = (float)std::max(1, matWidth[ti.texdata]), th = (float)std::max(1, matHeight[ti.texdata]);
      auto mk = [&](const Vector& texPos, const Vector& worldPos, float alpha) {
        SrcFaceVert v; v.pos = be ? VectorTransform(worldPos, xf) : worldPos; v.normal = sf.normal; v.alpha = alpha;
        v.u = (texPos.Dot(Vector(ti.tv[0][0], ti.tv[0][1], ti.tv[0][2])) + ti.tv[0][3]) / tw;
        v.v = (texPos.Dot(Vector(ti.tv[1][0], ti.tv[1][1], ti.tv[1][2])) + ti.tv[1][3]) / th;
        v.lu = texPos.Dot(Vector(ti.lv[0][0], ti.lv[0][1], ti.lv[0][2])) + ti.lv[0][3] - f.lmMins[0];
        v.lv = texPos.Dot(Vector(ti.lv[1][0], ti.lv[1][1], ti.lv[1][2])) + ti.lv[1][3] - f.lmMins[1];
        return v;
      };
      if (f.dispinfo >= 0 && f.dispinfo < (int)disps.size() && w.size() == 4) {
        const DispInfo& di = disps[f.dispinfo];
        sf.disp = true;
        int start = 0; float best = 1e30f;
        for (int k = 0; k < 4; k++) { float dd = (w[k] - di.start).LengthSqr(); if (dd < best) { best = dd; start = k; } }
        Vector c[4]; for (int k = 0; k < 4; k++) c[k] = w[(start + k) % 4];
        int N = (1 << di.power) + 1;
        // lightmap: vbsp gives displacements their own luxel rectangle (0..lmSize) spanning the 4 corners; the
        // texinfo projection only tells the orientation, so map the corner projections onto the full rectangle
        float cu[4], cv[4];
        for (int k = 0; k < 4; k++) { SrcFaceVert t = mk(c[k], c[k], 1); cu[k] = t.lu; cv[k] = t.lv; }
        {
          float u0 = std::min({ cu[0], cu[1], cu[2], cu[3] }), u1 = std::max({ cu[0], cu[1], cu[2], cu[3] });
          float v0 = std::min({ cv[0], cv[1], cv[2], cv[3] }), v1 = std::max({ cv[0], cv[1], cv[2], cv[3] });
          for (int k = 0; k < 4; k++) {
            cu[k] = u1 - u0 > 0.5f ? std::round((cu[k] - u0) / (u1 - u0)) * f.lmSize[0] : 0.f;
            cv[k] = v1 - v0 > 0.5f ? std::round((cv[k] - v0) / (v1 - v0)) * f.lmSize[1] : 0.f;
          }
        }
        std::vector<SrcFaceVert> grid((size_t)N * N);
        std::vector<Vector> gpos((size_t)N * N);
        for (int i = 0; i < N; i++) {
          float fi2 = (float)i / (N - 1);
          Vector e0 = c[0] + (c[1] - c[0]) * fi2, e1 = c[3] + (c[2] - c[3]) * fi2;
          for (int j = 0; j < N; j++) {
            float fj = (float)j / (N - 1);
            Vector flat = e0 + (e1 - e0) * fj;
            int vi = di.vertStart + i * N + j;
            Vector pos = flat; float a = 0;
            if (vi >= 0 && vi < (int)dverts.size()) { pos = flat + dverts[vi].vec * dverts[vi].dist; a = dverts[vi].alpha / 255.f; }
            grid[i * N + j] = mk(flat, pos, Clamp(a, 0.f, 1.f));   // alpha = amount of $basetexture2
            {
              float eu0 = cu[0] + (cu[1] - cu[0]) * fi2, eu1 = cu[3] + (cu[2] - cu[3]) * fi2;
              float ev0 = cv[0] + (cv[1] - cv[0]) * fi2, ev1 = cv[3] + (cv[2] - cv[3]) * fi2;
              grid[i * N + j].lu = eu0 + (eu1 - eu0) * fj; grid[i * N + j].lv = ev0 + (ev1 - ev0) * fj;
            }
            gpos[i * N + j] = grid[i * N + j].pos;
          }
        }
        // smooth normals from the grid
        for (int i = 0; i < N; i++) for (int j = 0; j < N; j++) {
          Vector dx = gpos[i * N + std::min(j + 1, N - 1)] - gpos[i * N + std::max(j - 1, 0)];
          Vector dy = gpos[std::min(i + 1, N - 1) * N + j] - gpos[std::max(i - 1, 0) * N + j];
          Vector nn = dy.Cross(dx); float l = nn.Length();
          if (l > 1e-4f) { nn /= l; if (nn.Dot(sf.normal) < 0) nn = nn * -1.f; grid[i * N + j].normal = nn; }
        }
        for (int i = 0; i + 1 < N; i++) for (int j = 0; j + 1 < N; j++) {
          int a = i * N + j, b = i * N + j + 1, cc = (i + 1) * N + j + 1, dd = (i + 1) * N + j;
          int t[6];
          if (((i * N + j) & 1) == 0) { int tt[6] = { a, b, cc, a, cc, dd }; std::memcpy(t, tt, sizeof t); }
          else { int tt[6] = { a, b, dd, b, cc, dd }; std::memcpy(t, tt, sizeof t); }
          for (int q = 0; q < 6; q += 3) {
            Vector p0 = gpos[t[q]], p1 = gpos[t[q + 1]], p2 = gpos[t[q + 2]];
            // output clockwise seen from the front (same convention as brush faces)
            Vector nn = (p1 - p0).Cross(p2 - p0);
            if (nn.Dot(sf.normal) > 0) std::swap(t[q + 1], t[q + 2]);
            sf.verts.push_back(grid[t[q]]); sf.verts.push_back(grid[t[q + 1]]); sf.verts.push_back(grid[t[q + 2]]);
            if (mi == 0) { dispTriPos.push_back(gpos[t[q]]); dispTriPos.push_back(gpos[t[q + 1]]); dispTriPos.push_back(gpos[t[q + 2]]); }
          }
        }
      } else {
        // fan (Source windings are clockwise seen from the front)
        for (size_t k = 1; k + 1 < w.size(); k++) { sf.verts.push_back(mk(w[0], w[0], 1)); sf.verts.push_back(mk(w[k], w[k], 1)); sf.verts.push_back(mk(w[k + 1], w[k + 1], 1)); }
      }
      faces.push_back(std::move(sf));
    }
  }

  // ---- collision: brushes of the world and of solid brush entities
  struct BrushRaw { int firstSide, numSides, contents; };
  std::vector<BrushRaw> brushes;
  for (int i = 0, n = count(LUMP_BRUSHES, 12); i < n; i++) { const uint8_t* p = lump(LUMP_BRUSHES) + i * 12; brushes.push_back({ Rd<int>(p), Rd<int>(p + 4), Rd<int>(p + 8) }); }
  struct SideRaw { int plane, texinfo, dispinfo, bevel; };
  std::vector<SideRaw> sides;
  for (int i = 0, n = count(LUMP_BRUSHSIDES, 8); i < n; i++) { const uint8_t* p = lump(LUMP_BRUSHSIDES) + i * 8; sides.push_back({ Rd<uint16_t>(p), Rd<int16_t>(p + 2), Rd<int16_t>(p + 4), Rd<int16_t>(p + 6) }); }
  std::vector<uint16_t> leafBrushes; for (int i = 0, n = count(LUMP_LEAFBRUSHES, 2); i < n; i++) leafBrushes.push_back(Rd<uint16_t>(lump(LUMP_LEAFBRUSHES) + i * 2));
  int leafSize = L[LUMP_LEAFS].ver == 0 ? 56 : 32;
  struct LeafRaw { int firstBrush, numBrushes; };
  std::vector<LeafRaw> leaves;
  for (int i = 0, n = count(LUMP_LEAFS, leafSize); i < n; i++) { const uint8_t* p = lump(LUMP_LEAFS) + i * leafSize; leaves.push_back({ Rd<uint16_t>(p + 24), Rd<uint16_t>(p + 26) }); }
  std::vector<int> brushOwner(brushes.size(), -2);   // -2 unknown, -1 world... model index
  std::function<void(int, int)> walk = [&](int node, int model) {
    if (node < 0) {
      int li = -1 - node; if (li >= (int)leaves.size()) return;
      for (int k = 0; k < leaves[li].numBrushes; k++) {
        int bi = leaves[li].firstBrush + k; if (bi >= (int)leafBrushes.size()) break;
        int b = leafBrushes[bi]; if (b < (int)brushOwner.size() && brushOwner[b] == -2) brushOwner[b] = model;
      }
      return;
    }
    if (node >= (int)m_nodes.size()) return;
    walk(m_nodes[node].children[0], model); walk(m_nodes[node].children[1], model);
  };
  for (int mi = 0; mi < (int)models.size(); mi++) walk(models[mi].headnode, mi);
  CEngineTrace& et = g_EngineTrace;
  std::vector<cplane_t> pl;
  for (int bi = 0; bi < (int)brushes.size(); bi++) {
    const BrushRaw& b = brushes[bi];
    int owner = brushOwner[bi]; if (owner < 0) continue;
    const SrcBrushEnt* be = owner ? entOfModel(owner) : nullptr;
    if (owner && (!be || !be->solid)) continue;
    int contents = 0;
    if (b.contents & SRC_CONTENTS_SOLID) contents |= CONTENTS_SOLID;
    if (b.contents & SRC_CONTENTS_WINDOW) contents |= CONTENTS_WINDOW;
    if (b.contents & SRC_CONTENTS_GRATE) contents |= CONTENTS_GRATE;
    if (b.contents & (SRC_CONTENTS_WATER | SRC_CONTENTS_SLIME)) contents |= CONTENTS_WATER;
    if (b.contents & SRC_CONTENTS_MOVEABLE) contents |= CONTENTS_MOVEABLE;
    if (b.contents & SRC_CONTENTS_PLAYERCLIP) contents |= CONTENTS_PLAYERCLIP;
    if (b.contents & SRC_CONTENTS_MONSTERCLIP) contents |= CONTENTS_MONSTERCLIP;
    if (!contents) continue;
    pl.clear(); Vector mn(-1e9f, -1e9f, -1e9f), mx(1e9f, 1e9f, 1e9f);
    bool sky = false, hasDisp = false; std::string surfMat;
    matrix3x4_t xf; if (be) AngleMatrix(be->angles, be->origin, xf);
    for (int s = b.firstSide; s < b.firstSide + b.numSides && s < (int)sides.size(); s++) {
      const SideRaw& sd = sides[s]; if (sd.plane >= (int)m_planes.size()) continue;
      Vector n = m_planes[sd.plane].n; float dist = m_planes[sd.plane].d;
      if (be) { Vector p0 = n * dist; n = VectorRotate(n, xf); dist = VectorTransform(p0, xf).Dot(n); }
      cplane_t cp; cp.normal = n; cp.dist = dist; pl.push_back(cp);
      for (int a = 0; a < 3; a++) { if (n[a] > 0.9999f) mx[a] = std::min(mx[a], dist); if (n[a] < -0.9999f) mn[a] = std::max(mn[a], -dist); }
      if (sd.dispinfo > 0) hasDisp = true;
      if (sd.texinfo >= 0 && sd.texinfo < (int)m_texinfo.size()) {
        const TexInfo& ti = m_texinfo[sd.texinfo];
        if (ti.flags & SURF_SKY_) sky = true;
        if (!sd.bevel && surfMat.empty() && ti.texdata >= 0 && ti.texdata < (int)materials.size() && materials[ti.texdata].rfind("tools/", 0) != 0) surfMat = materials[ti.texdata];
      }
    }
    (void)hasDisp;
    if (pl.size() < 4 || mn.x > mx.x || mn.y > mx.y || mn.z > mx.z || mx.x - mn.x > 1e8f) continue;
    int surf = surfMat.empty() ? SURFACE_CONCRETE : SrcMat_SurfaceFromProp(SrcMat_Find(surfMat).surfaceprop, surfMat);
    int idx = et.AddBrush(pl, mn, mx, contents, surf, 0, false);
    if (sky) et.brushes[idx].surfFlags |= SURF_SKY | SURF_NODRAW;
    numCollisionBrushes++;
  }
  // displacements: thin prisms under every triangle
  for (size_t t = 0; t + 2 < dispTriPos.size(); t += 3) {
    Vector a = dispTriPos[t], b = dispTriPos[t + 1], c = dispTriPos[t + 2];
    Vector n = (c - a).Cross(b - a); float len = n.Length(); if (len < 1e-3f) continue; n /= len;   // clockwise -> up normal
    if (n.z < 0 && std::fabs(n.z) > 0.01f) { n = n * -1.f; std::swap(b, c); }
    const float thick = 4.f;
    pl.clear();
    cplane_t top; top.normal = n; top.dist = n.Dot(a); pl.push_back(top);
    cplane_t bot; bot.normal = n * -1.f; bot.dist = -(n.Dot(a) - thick); pl.push_back(bot);
    Vector P[3] = { a, b, c };
    Vector ctr = (a + b + c) / 3.f;
    for (int e = 0; e < 3; e++) {
      Vector ed = P[(e + 1) % 3] - P[e];
      Vector sn = ed.Cross(n); float sl = sn.Length(); if (sl < 1e-4f) continue; sn /= sl;
      if (sn.Dot(ctr - P[e]) > 0) sn = sn * -1.f;
      cplane_t sp; sp.normal = sn; sp.dist = sn.Dot(P[e]); pl.push_back(sp);
    }
    Vector mn = VecMin(VecMin(a, b), c), mx = VecMax(VecMax(a, b), c);
    mn = VecMin(mn, VecMin(VecMin(a, b), c) - n * thick); mx = VecMax(mx, VecMax(VecMax(a - n * thick, b - n * thick), c - n * thick));
    mn = VecMin(mn, VecMin(VecMin(a - n * thick, b - n * thick), c - n * thick));
    // axial bevels (needed for box sweeps)
    for (int ax = 0; ax < 3; ax++) {
      cplane_t p1; p1.normal = Vector(0, 0, 0); p1.normal[ax] = 1; p1.dist = mx[ax]; pl.push_back(p1);
      cplane_t p2; p2.normal = Vector(0, 0, 0); p2.normal[ax] = -1; p2.dist = -mn[ax]; pl.push_back(p2);
    }
    et.AddBrush(pl, mn, mx, CONTENTS_SOLID, SURFACE_DIRT, 0, false);
    numDispTris++;
  }

  // ---- static props (game lump "sprp")
  if (L[LUMP_GAME_LUMP].len >= 4) {
    const uint8_t* g = lump(LUMP_GAME_LUMP); int ng = Rd<int>(g);
    for (int i = 0; i < ng && 4 + (i + 1) * 16 <= L[LUMP_GAME_LUMP].len; i++) {
      const uint8_t* h = g + 4 + i * 16;
      int id = Rd<int>(h); uint16_t ver = Rd<uint16_t>(h + 6); int ofs = Rd<int>(h + 8), len = Rd<int>(h + 12);
      if (id != (('s' << 24) | ('p' << 16) | ('r' << 8) | 'p') || ofs <= 0 || (size_t)ofs + len > file.size()) continue;
      const uint8_t* p = d + ofs; const uint8_t* end = p + len;
      int nd = Rd<int>(p); p += 4;
      std::vector<std::string> dict;
      for (int k = 0; k < nd && p + 128 <= end; k++, p += 128) dict.push_back(srcfs::Normalize(std::string((const char*)p, strnlen((const char*)p, 128))));
      int nl = Rd<int>(p); p += 4 + nl * 2;
      if (p + 4 > end) break;
      int np = Rd<int>(p); p += 4;
      if (np <= 0) break;
      int sz = (int)((end - p) / np);
      if (sz < 56) break;
      (void)ver;
      for (int k = 0; k < np; k++, p += sz) {
        SrcStaticProp sp;
        sp.origin = Rd<Vector>(p); Vector a = Rd<Vector>(p + 12); sp.angles = QAngle(a.x, a.y, a.z);
        int type = Rd<uint16_t>(p + 24); sp.solid = p[30]; sp.flags = p[31]; sp.skin = Rd<int>(p + 32);
        sp.fadeMin = Rd<float>(p + 36); sp.fadeMax = Rd<float>(p + 40); sp.lightingOrigin = Rd<Vector>(p + 44);
        sp.useLightingOrigin = (sp.flags & 0x2) != 0;
        if (type < 0 || type >= (int)dict.size()) continue;
        sp.model = dict[type];
        props.push_back(sp);
      }
    }
  }

  // ---- static prop collision (.phy convex hulls, bbox fallback)
  for (const SrcStaticProp& sp : props) {
    if (sp.solid == 0) continue;
    matrix3x4_t xf; AngleMatrix(sp.angles, sp.origin, xf);
    int surf = SrcMat_SurfaceFromProp("", sp.model);
    std::vector<std::vector<Vector>> hulls;
    LoadPhyHulls(sp.model, hulls);
    if (hulls.empty()) {
      CSrcModel* mdl = SrcMdl_Get(sp.model); if (!mdl) continue;
      Vector a = mdl->hullMin, b = mdl->hullMax;
      if ((b - a).Length() < 4.f) continue;
      std::vector<Vector> box; for (int k = 0; k < 8; k++) box.push_back(Vector((k & 1) ? b.x : a.x, (k & 2) ? b.y : a.y, (k & 4) ? b.z : a.z));
      hulls.push_back(box);
    }
    for (auto& h : hulls) { for (auto& v : h) v = VectorTransform(v, xf); if (AddConvexHull(h, surf)) numPropHulls++; }
  }

  // ---- sun / ambient
  for (auto& e : entities) if (e.Class() == "light_environment") {
    float pitch = e.GetFloat("pitch", -60); Vector ang = e.GetVector("angles");
    if (e.Has("pitch")) ang.x = pitch;
    Vector fwd; AngleVectors(QAngle(ang.x, ang.y, 0), &fwd);
    sunDir = fwd * -1.f;
    float r = 255, gg = 255, bb = 255, br = 200;
    if (sscanf(e.Get("_light"), "%f %f %f %f", &r, &gg, &bb, &br) >= 3) sunColor = Vector(r, gg, bb) / 255.f;
    if (sscanf(e.Get("_ambient"), "%f %f %f %f", &r, &gg, &bb, &br) >= 3) ambientColor = Vector(r, gg, bb) / 255.f;
  }
  loaded = true;
  Msg("Source BSP v%d %s: %d faces, %d materials, %d collision brushes, %d displacement tris, %d static props, %d brush entities\n",
      version, name.c_str(), (int)faces.size(), (int)materials.size(), numCollisionBrushes, numDispTris, (int)props.size(), (int)brushEnts.size());
  return true;
}

// light under a point: walk down the node tree (Quake style R_LightPoint)
bool CSrcBsp::RecursiveLight(int node, const Vector& a, const Vector& b, Vector& color) const {
  if (node < 0 || node >= (int)m_nodes.size()) return false;
  const Node& n = m_nodes[node]; const Plane& p = m_planes[n.plane];
  float fa = a.Dot(p.n) - p.d, fb = b.Dot(p.n) - p.d;
  int side = fa < 0;
  if ((fb < 0) == side) return RecursiveLight(n.children[side], a, b, color);
  float frac = fa / (fa - fb); Vector mid = a + (b - a) * frac;
  if (RecursiveLight(n.children[side], a, mid, color)) return true;
  for (int i = 0; i < n.numFaces; i++) {
    int fi = n.firstFace + i; if (fi < 0 || fi >= (int)m_rawFaces.size()) continue;
    const FaceRaw& f = m_rawFaces[fi];
    if (f.lightofs < 0 || f.texinfo < 0 || f.texinfo >= (int)m_texinfo.size()) continue;
    const TexInfo& ti = m_texinfo[f.texinfo]; if (ti.flags & (SURF_SKY_ | SURF_NODRAW_ | SURF_NOLIGHT_)) continue;
    float s = mid.Dot(Vector(ti.lv[0][0], ti.lv[0][1], ti.lv[0][2])) + ti.lv[0][3] - f.lmMins[0];
    float t = mid.Dot(Vector(ti.lv[1][0], ti.lv[1][1], ti.lv[1][2])) + ti.lv[1][3] - f.lmMins[1];
    if (s < 0 || t < 0 || s > f.lmSize[0] || t > f.lmSize[1]) continue;
    int w = f.lmSize[0] + 1;
    int si = (int)(s + 0.5f), tj = (int)(t + 0.5f);
    size_t off = (size_t)f.lightofs + ((size_t)tj * w + si) * 4;
    if (off + 4 > lighting.size()) return false;
    color = DecodeLuxel(&lighting[off]);
    return true;
  }
  return RecursiveLight(n.children[!side], mid, b, color);
}
bool CSrcBsp::LightPoint(const Vector& p, Vector& color) const {
  if (!loaded || m_nodes.empty()) return false;
  return RecursiveLight(0, p, p - Vector(0, 0, 2048), color);
}
