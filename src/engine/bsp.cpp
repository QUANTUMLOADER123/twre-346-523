#include "bsp.hpp"
#include "hlfs.hpp"
#include "convar.hpp"
#include "studio.hpp"
#include <cstring>
#include <cmath>
#include <fstream>
#include <sstream>
#include <algorithm>
#include <unordered_map>

static constexpr float DIST_EPSILON = 0.03125f;

namespace {
enum { LUMP_ENTITIES = 0, LUMP_PLANES, LUMP_TEXTURES, LUMP_VERTEXES, LUMP_VISIBILITY, LUMP_NODES, LUMP_TEXINFO, LUMP_FACES,
       LUMP_LIGHTING, LUMP_CLIPNODES, LUMP_LEAFS, LUMP_MARKSURFACES, LUMP_EDGES, LUMP_SURFEDGES, LUMP_MODELS, HEADER_LUMPS };
#pragma pack(push, 1)
struct dlump_t { int32_t ofs, len; };
struct dheader_t { int32_t version; dlump_t lumps[HEADER_LUMPS]; };
struct dplane_t { float normal[3]; float dist; int32_t type; };
struct dnode_t { int32_t planenum; int16_t children[2]; int16_t mins[3], maxs[3]; uint16_t firstface, numfaces; };
struct dclipnode_t { int32_t planenum; int16_t children[2]; };
struct dleaf_t { int32_t contents; int32_t visofs; int16_t mins[3], maxs[3]; uint16_t firstmark, nummark; uint8_t ambient[4]; };
struct dtexinfo_t { float vecs[2][4]; int32_t miptex; int32_t flags; };
struct dface_t { int16_t planenum; int16_t side; int32_t firstedge; int16_t numedges; int16_t texinfo; uint8_t styles[4]; int32_t lightofs; };
struct dedge_t { uint16_t v[2]; };
struct dmodel_t { float mins[3], maxs[3], origin[3]; int32_t headnode[4]; int32_t visleafs; int32_t firstface, numfaces; };
struct miptex_t { char name[16]; uint32_t width, height; uint32_t offsets[4]; };
struct wadinfo_t { char id[4]; int32_t numlumps; int32_t infotableofs; };
struct lumpinfo_t { int32_t filepos, disksize, size; char type, compression, pad1, pad2; char name[16]; };
#pragma pack(pop)

std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
std::string Upper(std::string s) { for (char& c : s) c = (char)std::toupper((unsigned char)c); return s; }

template <class T> bool LumpArray(const std::vector<uint8_t>& d, const dheader_t& h, int lump, std::vector<T>& out) {
  const dlump_t& l = h.lumps[lump];
  if (l.ofs < 0 || l.len < 0 || (size_t)l.ofs + (size_t)l.len > d.size() || l.len % sizeof(T)) return false;
  out.resize(l.len / sizeof(T));
  if (l.len) std::memcpy(out.data(), d.data() + l.ofs, l.len);
  return true;
}

// decode one mip texture (embedded or from a WAD lump); `base` points at the miptex_t header
bool DecodeMip(const uint8_t* base, size_t len, BspTexture& t) {
  if (len < sizeof(miptex_t)) return false;
  miptex_t mt; std::memcpy(&mt, base, sizeof(mt));
  if (mt.offsets[0] == 0 || mt.width == 0 || mt.height == 0 || mt.width > 4096 || mt.height > 4096) return false;
  size_t npix = (size_t)mt.width * mt.height;
  size_t palOfs = (size_t)mt.offsets[3] + (size_t)(mt.width / 8) * (mt.height / 8) + 2;
  if ((size_t)mt.offsets[0] + npix > len || palOfs + 768 > len) return false;
  const uint8_t* pix = base + mt.offsets[0];
  const uint8_t* pal = base + palOfs;
  t.w = (int)mt.width; t.h = (int)mt.height;
  t.rgba.resize(npix * 4);
  double ar = 0, ag = 0, ab = 0; size_t na = 0;
  for (size_t i = 0; i < npix; i++) {
    int idx = pix[i];
    uint8_t* o = &t.rgba[i * 4];
    o[0] = pal[idx * 3]; o[1] = pal[idx * 3 + 1]; o[2] = pal[idx * 3 + 2]; o[3] = 255;
    if (t.alphaTest && idx == 255) { o[0] = o[1] = o[2] = 0; o[3] = 0; continue; }
    ar += o[0]; ag += o[1]; ab += o[2]; na++;
  }
  if (t.alphaTest) { // bleed colours into the transparent texels so filtering doesn't show dark fringes
    for (int pass = 0; pass < 2; pass++)
      for (int y = 0; y < t.h; y++) for (int x = 0; x < t.w; x++) {
        uint8_t* o = &t.rgba[((size_t)y * t.w + x) * 4]; if (o[3]) continue;
        int r = 0, g = 0, b = 0, n = 0;
        for (int k = 0; k < 4; k++) {
          int xx = x + (k == 0) - (k == 1), yy = y + (k == 2) - (k == 3);
          if (xx < 0 || yy < 0 || xx >= t.w || yy >= t.h) continue;
          const uint8_t* q = &t.rgba[((size_t)yy * t.w + xx) * 4];
          if (q[3] || (q[0] | q[1] | q[2])) { r += q[0]; g += q[1]; b += q[2]; n++; }
        }
        if (n) { o[0] = (uint8_t)(r / n); o[1] = (uint8_t)(g / n); o[2] = (uint8_t)(b / n); }
      }
  }
  if (na) t.avgColor = Vector((float)(ar / na / 255.0), (float)(ag / na / 255.0), (float)(ab / na / 255.0));
  return true;
}

struct WadFile {
  std::string path;
  std::unordered_map<std::string, lumpinfo_t> lumps;   // lowercase name
  bool Open(const std::string& p) {
    path = p;
    std::ifstream f(p, std::ios::binary); if (!f) return false;
    wadinfo_t h; f.read((char*)&h, sizeof(h)); if (!f || std::memcmp(h.id, "WAD3", 4) != 0 || h.numlumps <= 0 || h.numlumps > 100000) return false;
    std::vector<lumpinfo_t> info(h.numlumps);
    f.seekg(h.infotableofs); f.read((char*)info.data(), sizeof(lumpinfo_t) * h.numlumps); if (!f) return false;
    for (auto& l : info) { char n[17] = {}; std::memcpy(n, l.name, 16); lumps[Lower(n)] = l; }
    return true;
  }
  bool Load(const std::string& name, BspTexture& t) const {
    auto it = lumps.find(Lower(name)); if (it == lumps.end() || it->second.compression) return false;
    std::ifstream f(path, std::ios::binary); if (!f) return false;
    std::vector<uint8_t> buf(it->second.disksize);
    f.seekg(it->second.filepos); f.read((char*)buf.data(), buf.size()); if (!f) return false;
    return DecodeMip(buf.data(), buf.size(), t);
  }
};
} // namespace

// ------------------------------------------------------------------------------------------------
float BspEntity::GetFloat(const char* k, float def) const { auto it = kv.find(k); return it == kv.end() ? def : (float)atof(it->second.c_str()); }
Vector BspEntity::GetVector(const char* k, const Vector& def) const {
  auto it = kv.find(k); if (it == kv.end()) return def;
  Vector v; if (sscanf(it->second.c_str(), "%f %f %f", &v.x, &v.y, &v.z) == 3) return v; return def;
}
int BspEntity::ModelIndex() const { const char* m = Get("model"); return m[0] == '*' ? atoi(m + 1) : -1; }

const Vector& CBspWorld::HullMins(int h) {
  static const Vector m[4] = { { 0, 0, 0 }, { -16, -16, -36 }, { -32, -32, -32 }, { -16, -16, -18 } }; return m[h & 3];
}
const Vector& CBspWorld::HullMaxs(int h) {
  static const Vector m[4] = { { 0, 0, 0 }, { 16, 16, 36 }, { 32, 32, 32 }, { 16, 16, 18 } }; return m[h & 3];
}

void CBspWorld::Unload() { *this = CBspWorld(); }

bool CBspWorld::Load(const std::string& path, const std::string& mapName) {
  Unload();
  std::vector<uint8_t> d;
  {
    std::ifstream f(path, std::ios::binary); if (!f) { Warning("BSP: can't open %s\n", path.c_str()); return false; }
    f.seekg(0, std::ios::end); d.resize((size_t)f.tellg()); f.seekg(0); f.read((char*)d.data(), d.size());
  }
  if (d.size() < sizeof(dheader_t)) return false;
  dheader_t h; std::memcpy(&h, d.data(), sizeof(h));
  if (h.version != 30) { Warning("BSP: %s is version %d (only GoldSrc v30 maps are supported)\n", path.c_str(), h.version); return false; }
  name = mapName;
  std::vector<dplane_t> dpl; std::vector<dnode_t> dno; std::vector<dclipnode_t> dcl; std::vector<dleaf_t> dle; std::vector<dtexinfo_t> dti;
  std::vector<dface_t> dfa; std::vector<dedge_t> ded; std::vector<int32_t> dse; std::vector<dmodel_t> dmo; std::vector<Vector> dve;
  if (!LumpArray(d, h, LUMP_PLANES, dpl) || !LumpArray(d, h, LUMP_NODES, dno) || !LumpArray(d, h, LUMP_CLIPNODES, dcl) || !LumpArray(d, h, LUMP_LEAFS, dle) ||
      !LumpArray(d, h, LUMP_TEXINFO, dti) || !LumpArray(d, h, LUMP_FACES, dfa) || !LumpArray(d, h, LUMP_EDGES, ded) || !LumpArray(d, h, LUMP_SURFEDGES, dse) ||
      !LumpArray(d, h, LUMP_MODELS, dmo) || !LumpArray(d, h, LUMP_VERTEXES, dve) || !LumpArray(d, h, LUMP_MARKSURFACES, markSurfaces) || !LumpArray(d, h, LUMP_LIGHTING, lighting)) {
    Warning("BSP: %s is damaged\n", path.c_str()); return false;
  }
  // entities
  {
    const dlump_t& l = h.lumps[LUMP_ENTITIES];
    std::string txt((const char*)d.data() + l.ofs, (size_t)l.len);
    size_t i = 0; BspEntity cur; bool in = false;
    auto readQuoted = [&](std::string& out) { size_t a = txt.find('"', i); if (a == std::string::npos) return false; size_t b = txt.find('"', a + 1); if (b == std::string::npos) return false; out = txt.substr(a + 1, b - a - 1); i = b + 1; return true; };
    while (i < txt.size()) {
      char c = txt[i];
      if (c == '{') { cur = BspEntity(); in = true; i++; }
      else if (c == '}') { if (in) entities.push_back(cur); in = false; i++; }
      else if (c == '"' && in) { std::string k, v; if (!readQuoted(k) || !readQuoted(v)) break; cur.kv[k] = v; }
      else i++;
    }
  }
  planes.resize(dpl.size());
  for (size_t i = 0; i < dpl.size(); i++) { planes[i].normal = Vector(dpl[i].normal[0], dpl[i].normal[1], dpl[i].normal[2]); planes[i].dist = dpl[i].dist; planes[i].type = dpl[i].type; }
  leaves.resize(dle.size());
  for (size_t i = 0; i < dle.size(); i++) {
    leaves[i].contents = dle[i].contents; leaves[i].visofs = dle[i].visofs; leaves[i].firstMark = dle[i].firstmark; leaves[i].numMarks = dle[i].nummark;
    leaves[i].mins = Vector(dle[i].mins[0], dle[i].mins[1], dle[i].mins[2]); leaves[i].maxs = Vector(dle[i].maxs[0], dle[i].maxs[1], dle[i].maxs[2]);
  }
  nodes.resize(dno.size()); hull0.resize(dno.size());
  for (size_t i = 0; i < dno.size(); i++) {
    BspNode& n = nodes[i]; n.plane = dno[i].planenum; n.firstFace = dno[i].firstface; n.numFaces = dno[i].numfaces;
    n.mins = Vector(dno[i].mins[0], dno[i].mins[1], dno[i].mins[2]); n.maxs = Vector(dno[i].maxs[0], dno[i].maxs[1], dno[i].maxs[2]);
    hull0[i].plane = n.plane;
    for (int c = 0; c < 2; c++) {
      int ch = dno[i].children[c]; n.children[c] = ch;
      if (ch >= 0) hull0[i].children[c] = ch;
      else { int leaf = -1 - ch; hull0[i].children[c] = (leaf >= 0 && leaf < (int)leaves.size()) ? leaves[leaf].contents : BSP_CONTENTS_SOLID; }
    }
  }
  clipnodes.resize(dcl.size());
  for (size_t i = 0; i < dcl.size(); i++) { clipnodes[i].plane = dcl[i].planenum; clipnodes[i].children[0] = dcl[i].children[0]; clipnodes[i].children[1] = dcl[i].children[1]; }
  texinfo.resize(dti.size());
  for (size_t i = 0; i < dti.size(); i++) { std::memcpy(texinfo[i].vecs, dti[i].vecs, sizeof(texinfo[i].vecs)); texinfo[i].miptex = dti[i].miptex; texinfo[i].flags = dti[i].flags; }
  models.resize(dmo.size());
  for (size_t i = 0; i < dmo.size(); i++) {
    BspModel& m = models[i];
    m.mins = Vector(dmo[i].mins[0], dmo[i].mins[1], dmo[i].mins[2]); m.maxs = Vector(dmo[i].maxs[0], dmo[i].maxs[1], dmo[i].maxs[2]);
    m.origin = Vector(dmo[i].origin[0], dmo[i].origin[1], dmo[i].origin[2]);
    for (int k = 0; k < 4; k++) m.headnode[k] = dmo[i].headnode[k];
    m.firstFace = dmo[i].firstface; m.numFaces = dmo[i].numfaces;
  }
  if (models.empty()) return false;
  worldMins = models[0].mins; worldMaxs = models[0].maxs;
  // faces: polygons + lightmap extents
  faces.resize(dfa.size());
  int badLm = 0;
  for (size_t i = 0; i < dfa.size(); i++) {
    const dface_t& df = dfa[i]; BspFace& f = faces[i];
    f.plane = df.planenum; f.side = df.side != 0; f.texinfo = df.texinfo; std::memcpy(f.styles, df.styles, 4); f.lightofs = df.lightofs;
    f.normal = planes[f.plane].normal * (f.side ? -1.f : 1.f);
    f.firstVert = (int)faceVerts.size();
    for (int e = 0; e < df.numedges; e++) {
      int se = dse[df.firstedge + e];
      int vi = se >= 0 ? ded[se].v[0] : ded[-se].v[1];
      faceVerts.push_back(dve[vi]);
    }
    f.numVerts = (int)faceVerts.size() - f.firstVert;
    const BspTexInfo& ti = texinfo[f.texinfo];
    double mn[2] = { 1e30, 1e30 }, mx[2] = { -1e30, -1e30 };
    for (int v = 0; v < f.numVerts; v++) {
      const Vector& p = faceVerts[f.firstVert + v];
      for (int j = 0; j < 2; j++) {
        double val = (double)p.x * ti.vecs[j][0] + (double)p.y * ti.vecs[j][1] + (double)p.z * ti.vecs[j][2] + ti.vecs[j][3];
        mn[j] = std::min(mn[j], val); mx[j] = std::max(mx[j], val);
      }
    }
    for (int j = 0; j < 2; j++) {
      int bmin = (int)std::floor(mn[j] / 16.0), bmax = (int)std::ceil(mx[j] / 16.0);
      f.texMins[j] = bmin * 16; f.extents[j] = (bmax - bmin) * 16;
    }
    f.lmW = f.extents[0] / 16 + 1; f.lmH = f.extents[1] / 16 + 1;
    if (f.lightofs >= 0) {
      int ns = 0; for (int s = 0; s < 4; s++) if (f.styles[s] != 255) ns++;
      if ((size_t)f.lightofs + (size_t)f.lmW * f.lmH * 3 * std::max(1, ns) > lighting.size()) { badLm++; f.lightofs = -1; }
    }
  }
  for (size_t m = 0; m < models.size(); m++)
    for (int i = 0; i < models[m].numFaces; i++) { int fi = models[m].firstFace + i; if (fi >= 0 && fi < (int)faces.size()) faces[fi].model = (int)m; }
  if (badLm) Warning("BSP: %d faces with broken lightmaps\n", badLm);
  // textures
  { const dlump_t& l = h.lumps[LUMP_TEXTURES]; if (l.len > 4) LoadTextures(d.data() + l.ofs, (size_t)l.len); }
  LoadMaterials();
  // worldspawn / light_environment
  for (auto& e : entities) {
    std::string c = e.Class();
    if (c == "worldspawn") skyName = e.Get("skyname", "desert");
    if (c == "light_environment") {
      float yaw = e.GetFloat("angle", 0); Vector ang = e.GetVector("angles"); if (e.Has("angles") && !e.Has("angle")) yaw = ang.y;
      float pitch = e.Has("pitch") ? e.GetFloat("pitch") : ang.x;
      // the light shines along (pitch, yaw); the direction *towards* the sun is the opposite
      float cp = std::cos(DEG2RADF(pitch)), sp = std::sin(DEG2RADF(pitch));
      Vector shine(std::cos(DEG2RADF(yaw)) * cp, std::sin(DEG2RADF(yaw)) * cp, sp);
      sunDir = (shine * -1.f).Normalized(); if (sunDir.z < 0.1f) sunDir.z = 0.1f; sunDir = sunDir.Normalized();
      Vector lc = e.GetVector("_light", Vector(255, 255, 200)); sunColor = Vector(lc.x / 255.f, lc.y / 255.f, lc.z / 255.f);
    }
  }
  SetupBrushEntities();
  loaded = true;
  Msg("BSP %s: %d faces, %d textures (%d missing), %d entities, %d brush entities, lightmaps %d KB\n", name.c_str(), (int)faces.size(), (int)textures.size(),
      texturesMissing, (int)entities.size(), (int)brushEnts.size(), (int)(lighting.size() / 1024));
  return true;
}

void CBspWorld::LoadTextures(const uint8_t* lump, size_t len) {
  int32_t n; std::memcpy(&n, lump, 4);
  if (n < 0 || 4 + (size_t)n * 4 > len) return;
  textures.resize(n);
  std::vector<int> needWad;
  for (int i = 0; i < n; i++) {
    int32_t ofs; std::memcpy(&ofs, lump + 4 + i * 4, 4);
    BspTexture& t = textures[i];
    if (ofs < 0 || (size_t)ofs + sizeof(miptex_t) > len) { t.name = "missing"; continue; }
    miptex_t mt; std::memcpy(&mt, lump + ofs, sizeof(mt));
    char nm[17] = {}; std::memcpy(nm, mt.name, 16); t.name = nm;
    std::string ln = Lower(t.name);
    t.alphaTest = !ln.empty() && ln[0] == '{';
    t.sky = ln == "sky";
    t.water = !ln.empty() && (ln[0] == '!' || ln[0] == '*' || ln.rfind("water", 0) == 0);
    t.nodraw = ln == "aaatrigger" || ln == "clip" || ln == "origin" || ln == "null" || ln == "hint" || ln == "skip" || ln == "bevel" || ln == "nodraw";
    t.w = (int)mt.width; t.h = (int)mt.height;
    if (mt.offsets[0] != 0) { if (!DecodeMip(lump + ofs, len - ofs, t)) needWad.push_back(i); }
    else needWad.push_back(i);
  }
  if (needWad.empty()) return;
  // WADs: the ones listed in worldspawn first, then every .wad of the install
  std::vector<WadFile> wads; std::vector<std::string> opened;
  auto openWad = [&](const std::string& rel) {
    std::string p = hlfs::Find(rel); if (p.empty()) return;
    for (auto& o : opened) if (o == p) return;
    opened.push_back(p);
    WadFile w; if (w.Open(p)) wads.push_back(std::move(w));
  };
  for (auto& e : entities) if (e.Class() == "worldspawn") {
    std::string list = e.Get("wad");
    std::stringstream ss(list); std::string item;
    while (std::getline(ss, item, ';')) {
      for (char& c : item) if (c == '\\') c = '/';
      size_t sl = item.find_last_of('/'); std::string base = sl == std::string::npos ? item : item.substr(sl + 1);
      if (!base.empty()) openWad(base);
    }
  }
  for (const char* w : { "cstrike.wad", "halflife.wad", "cs_dust.wad", "itsitaly.wad", "torntextures.wad", "de_piranesi.wad", "cs_assault.wad", "cs_office.wad",
                         "de_aztec.wad", "de_vertigo.wad", "cs_bdog.wad", "de_storm.wad", "n0th1ng.wad", "decals.wad", "liquids.wad", "xeno.wad" }) openWad(w);
  for (auto& d : hlfs::GameDirs()) for (auto& w : hlfs::List("", ".wad")) { (void)d; openWad(w + ".wad"); }
  for (int i : needWad) {
    BspTexture& t = textures[i]; bool ok = false;
    for (auto& w : wads) if (w.Load(t.name, t)) { ok = true; break; }
    if (!ok && !t.nodraw && !t.sky) texturesMissing++;
  }
}

void CBspWorld::LoadMaterials() {
  m_materials.clear();
  std::vector<uint8_t> buf;
  if (!hlfs::Read("sound/materials.txt", buf)) return;
  std::stringstream ss(std::string(buf.begin(), buf.end())); std::string line;
  while (std::getline(ss, line)) {
    size_t a = line.find_first_not_of(" \t\r"); if (a == std::string::npos || line[a] == '/') continue;
    char c = (char)std::toupper((unsigned char)line[a]);
    size_t b = line.find_first_not_of(" \t", a + 1); if (b == std::string::npos) continue;
    size_t e = line.find_first_of(" \t\r", b);
    std::string tex = Upper(line.substr(b, e == std::string::npos ? std::string::npos : e - b));
    if (tex.size() > 12) tex.resize(12);
    m_materials[tex] = c;
  }
}

char CBspWorld::TextureMaterial(const std::string& texIn) const {
  const char* p = texIn.c_str();
  if (*p == '-' || *p == '+') { p++; if (*p) p++; }
  if (*p == '{' || *p == '!' || *p == '~' || *p == ' ') p++;
  std::string t = Upper(p); if (t.size() > 12) t.resize(12);
  auto it = m_materials.find(t);
  return it == m_materials.end() ? 'C' : it->second;
}

const BspTexture* CBspWorld::FaceTexture(int face) const {
  if (face < 0 || face >= (int)faces.size()) return nullptr;
  int mt = texinfo[faces[face].texinfo].miptex;
  return (mt >= 0 && mt < (int)textures.size()) ? &textures[mt] : nullptr;
}

// ------------------------------------------------------------------------------------------------
void CBspWorld::SetupBrushEntities() {
  modelToBrushEnt.assign(models.size(), -1);
  static const char* invisible[] = { "func_buyzone", "func_bomb_target", "func_hostage_rescue", "func_escapezone", "func_vip_safetyzone", "func_ladder",
                                      "func_monsterclip", "game_zone_player", "func_mortar_field", "env_bubbles", "func_clip", "func_traincontrols", nullptr };
  static const char* nonSolid[] = { "func_illusionary", "func_water", "func_door", "func_door_rotating", "momentary_door", "func_friction", nullptr };
  for (int ei = 0; ei < (int)entities.size(); ei++) {
    const BspEntity& e = entities[ei];
    int mi = e.ModelIndex(); if (mi <= 0 || mi >= (int)models.size()) continue;
    std::string c = e.Class();
    BspBrushEntity be; be.model = mi; be.entity = ei; be.classname = c;
    be.visible = c.rfind("trigger_", 0) != 0;
    for (const char** p = invisible; *p; p++) if (c == *p) be.visible = false;
    be.solid = be.visible;
    for (const char** p = nonSolid; *p; p++) if (c == *p) be.solid = false;
    be.rendermode = e.GetInt("rendermode", 0); be.renderamt = e.GetFloat("renderamt", 255);
    be.rendercolor = e.GetVector("rendercolor", Vector(255, 255, 255));
    if (be.rendermode != 0 && be.rendermode != 4 && be.renderamt <= 0) be.visible = false;
    be.origin = e.GetVector("origin");
    matrix3x4_t xf; xf.SetOrigin(be.origin);
    const BspModel& m = models[mi];
    if (c == "func_door" || c == "momentary_door") {
      // drawn in the open position (no door logic here; doors don't block)
      Vector ang = e.GetVector("angles"); float a = e.Has("angle") ? e.GetFloat("angle") : ang.y;
      Vector dir = a == -1 ? Vector(0, 0, 1) : a == -2 ? Vector(0, 0, -1) : Vector(std::cos(DEG2RADF(a)), std::sin(DEG2RADF(a)), 0);
      Vector size = m.maxs - m.mins;
      float dist = std::fabs(dir.x * (size.x - 2)) + std::fabs(dir.y * (size.y - 2)) + std::fabs(dir.z * (size.z - 2)) - e.GetFloat("lip", 0);
      if (!(e.GetInt("spawnflags") & 1)) xf.SetOrigin(be.origin + dir * dist);
    } else if (c == "func_door_rotating") {
      int sf = e.GetInt("spawnflags"); float dist = e.GetFloat("distance", 90); if (sf & 2) dist = -dist;
      Vector axis = (sf & 64) ? Vector(1, 0, 0) : (sf & 128) ? Vector(0, 1, 0) : Vector(0, 0, 1);
      if (!(sf & 1)) { xf = AxisAngleMatrix(axis, dist); xf.SetOrigin(be.origin); }
    }
    be.xform = xf;
    // world bounds of the (possibly rotated) model
    Vector mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
    for (int k = 0; k < 8; k++) { Vector p((k & 1) ? m.maxs.x : m.mins.x, (k & 2) ? m.maxs.y : m.mins.y, (k & 4) ? m.maxs.z : m.mins.z); p = VectorTransform(p, xf); mn = VecMin(mn, p); mx = VecMax(mx, p); }
    be.absMins = mn; be.absMaxs = mx;
    if (c == "func_breakable") {
      be.health = e.GetFloat("health", 0); be.material = e.GetInt("material", 0);
      be.breakable = !(e.GetInt("spawnflags") & 1) && be.health > 0 && be.material != 7;
    }
    modelToBrushEnt[mi] = (int)brushEnts.size();
    brushEnts.push_back(be);
  }
}

void CBspWorld::BreakEntity(int i) {
  if (i < 0 || i >= (int)brushEnts.size()) return;
  brushEnts[i].broken = true; brushEnts[i].solid = false; brushEnts[i].visible = false;
}

// ------------------------------------------------------------------------------------------------
// collision (Quake / GoldSrc SV_RecursiveHullCheck)
int CBspWorld::HullPointContents(const BspClipNode* hn, int num, const Vector& p) const {
  while (num >= 0) {
    const BspClipNode& n = hn[num]; const BspPlane& pl = planes[n.plane];
    float d = pl.type < 3 ? p[pl.type] - pl.dist : pl.normal.Dot(p) - pl.dist;
    num = d < 0 ? n.children[1] : n.children[0];
  }
  return num;
}

int CBspWorld::PointContents(const Vector& p, int hull, int model) const {
  if (!loaded || model < 0 || model >= (int)models.size()) return BSP_CONTENTS_EMPTY;
  const BspClipNode* hn = hull == 0 ? hull0.data() : clipnodes.data();
  int head = models[model].headnode[hull & 3];
  if (hull == 0 ? (head < 0 || head >= (int)hull0.size()) : (head < 0 || head >= (int)clipnodes.size())) return BSP_CONTENTS_EMPTY;
  return HullPointContents(hn, head, p);
}

bool CBspWorld::RecursiveHullCheck(const BspClipNode* hn, int head, int num, float p1f, float p2f, const Vector& p1, const Vector& p2, BspTrace& tr, bool skySolid) const {
  if (num < 0) {
    bool solid = num == BSP_CONTENTS_SOLID || (skySolid && num == BSP_CONTENTS_SKY);
    if (!solid) { tr.allsolid = false; if (num == BSP_CONTENTS_EMPTY) tr.inopen = true; else tr.inwater = true; }
    else tr.startsolid = true;
    return true;
  }
  const BspClipNode& node = hn[num]; const BspPlane& pl = planes[node.plane];
  float t1, t2;
  if (pl.type < 3) { t1 = p1[pl.type] - pl.dist; t2 = p2[pl.type] - pl.dist; }
  else { t1 = pl.normal.Dot(p1) - pl.dist; t2 = pl.normal.Dot(p2) - pl.dist; }
  if (t1 >= 0 && t2 >= 0) return RecursiveHullCheck(hn, head, node.children[0], p1f, p2f, p1, p2, tr, skySolid);
  if (t1 < 0 && t2 < 0) return RecursiveHullCheck(hn, head, node.children[1], p1f, p2f, p1, p2, tr, skySolid);
  float frac = t1 < 0 ? (t1 + DIST_EPSILON) / (t1 - t2) : (t1 - DIST_EPSILON) / (t1 - t2);
  frac = Clamp(frac, 0.f, 1.f);
  float midf = p1f + (p2f - p1f) * frac;
  Vector mid = p1 + (p2 - p1) * frac;
  int side = t1 < 0 ? 1 : 0;
  if (!RecursiveHullCheck(hn, head, node.children[side], p1f, midf, p1, mid, tr, skySolid)) return false;
  auto isSolid = [&](int c) { return c == BSP_CONTENTS_SOLID || (skySolid && c == BSP_CONTENTS_SKY); };
  int other = HullPointContents(hn, node.children[side ^ 1], mid);
  if (!isSolid(other)) return RecursiveHullCheck(hn, head, node.children[side ^ 1], midf, p2f, mid, p2, tr, skySolid);
  if (tr.allsolid) return false;
  if (!side) { tr.normal = pl.normal; tr.dist = pl.dist; } else { tr.normal = pl.normal * -1.f; tr.dist = -pl.dist; }
  while (isSolid(HullPointContents(hn, head, mid))) {   // shouldn't really happen, but does with bad epsilons
    frac -= 0.1f;
    if (frac < 0) { tr.fraction = midf; tr.endpos = mid; tr.hitNode = num; tr.hitContents = other; return false; }
    midf = p1f + (p2f - p1f) * frac; mid = p1 + (p2 - p1) * frac;
  }
  tr.fraction = midf; tr.endpos = mid; tr.hitNode = num; tr.hitContents = other;
  return false;
}

void CBspWorld::TraceModel(int hull, int model, const Vector& start, const Vector& end, BspTrace& tr, bool skySolid) const {
  tr = BspTrace(); tr.endpos = end;
  if (!loaded || model < 0 || model >= (int)models.size()) { tr.allsolid = false; return; }
  hull &= 3;
  const BspClipNode* hn = hull == 0 ? hull0.data() : clipnodes.data();
  int head = models[model].headnode[hull];
  if (hull == 0 ? (head < 0 || head >= (int)hull0.size()) : (head < 0 || head >= (int)clipnodes.size())) { tr.allsolid = false; return; }
  RecursiveHullCheck(hn, head, head, 0.f, 1.f, start, end, tr, skySolid && hull == 0);
  if (tr.allsolid) { tr.startsolid = true; tr.fraction = 0; tr.endpos = start; }
}

void CBspWorld::Trace(int hull, const Vector& start, const Vector& end, BspTrace& tr, bool skySolid) const {
  TraceModel(hull, 0, start, end, tr, skySolid);
  if (tr.allsolid) return;
  const Vector hmn = HullMins(hull), hmx = HullMaxs(hull);
  Vector smn = VecMin(start, end) + hmn - Vector(1, 1, 1), smx = VecMax(start, end) + hmx + Vector(1, 1, 1);
  for (int i = 0; i < (int)brushEnts.size(); i++) {
    const BspBrushEntity& be = brushEnts[i];
    if (!be.solid) continue;
    if (be.absMins.x > smx.x || be.absMaxs.x < smn.x || be.absMins.y > smx.y || be.absMaxs.y < smn.y || be.absMins.z > smx.z || be.absMaxs.z < smn.z) continue;
    BspTrace t; TraceModel(hull, be.model, start - be.origin, end - be.origin, t, skySolid);
    if (t.allsolid || t.startsolid || t.fraction < tr.fraction) {
      t.endpos += be.origin; t.brushEntity = i;
      bool wasStart = tr.startsolid;
      tr = t; if (wasStart) tr.startsolid = true;
    } else if (t.startsolid) tr.startsolid = true;
  }
}

static bool PointInFace(const CBspWorld& w, const BspFace& f, const Vector& p, float eps) {
  const Vector n = w.planes[f.plane].normal;
  for (int i = 0; i < f.numVerts; i++) {
    const Vector& a = w.faceVerts[f.firstVert + i]; const Vector& b = w.faceVerts[f.firstVert + (i + 1) % f.numVerts];
    Vector edge = b - a; Vector inward = n.Cross(edge);   // GoldSrc windings are clockwise seen from the front
    float len = inward.Length(); if (len < 1e-4f) continue;
    float d = (p - a).Dot(inward) / len;
    if (f.side ? d < -eps : d > eps) return false;
  }
  return true;
}

int CBspWorld::FaceAtImpact(const BspTrace& tr) const {
  if (tr.hitNode < 0 || tr.hitNode >= (int)nodes.size()) return -1;
  const BspNode& n = nodes[tr.hitNode];
  // tr.endpos is in the hit model's space only for the world; brush entities are translated back
  Vector p = tr.endpos;
  if (tr.brushEntity >= 0) p -= brushEnts[tr.brushEntity].origin;
  int best = -1;
  for (int eps = 0; eps < 2 && best < 0; eps++)
    for (int i = 0; i < n.numFaces; i++) {
      int fi = n.firstFace + i; if (fi < 0 || fi >= (int)faces.size()) continue;
      if (PointInFace(*this, faces[fi], p, eps ? 4.f : 0.25f)) { best = fi; break; }
    }
  return best;
}

int CBspWorld::FindFace(const Vector& p, const Vector& dir, float maxDist) const {
  BspTrace tr; Trace(0, p - dir * 1.f, p + dir * maxDist, tr, true);
  if (tr.fraction >= 1.f || tr.allsolid) return -1;
  return FaceAtImpact(tr);
}

Vector CBspWorld::SampleFaceLight(int fi, const Vector& p) const {
  if (fi < 0 || fi >= (int)faces.size()) return Vector(1, 1, 1);
  const BspFace& f = faces[fi];
  if (f.lightofs < 0) return Vector(1, 1, 1);
  const BspTexInfo& ti = texinfo[f.texinfo];
  float s = (p.x * ti.vecs[0][0] + p.y * ti.vecs[0][1] + p.z * ti.vecs[0][2] + ti.vecs[0][3] - f.texMins[0]) / 16.f;
  float t = (p.x * ti.vecs[1][0] + p.y * ti.vecs[1][1] + p.z * ti.vecs[1][2] + ti.vecs[1][3] - f.texMins[1]) / 16.f;
  s = Clamp(s, 0.f, (float)(f.lmW - 1)); t = Clamp(t, 0.f, (float)(f.lmH - 1));
  int x0 = (int)s, y0 = (int)t, x1 = std::min(x0 + 1, f.lmW - 1), y1 = std::min(y0 + 1, f.lmH - 1);
  float fx = s - x0, fy = t - y0;
  auto L = [&](int x, int y) { const uint8_t* q = &lighting[f.lightofs + (y * f.lmW + x) * 3]; return Vector(q[0], q[1], q[2]); };
  Vector c = (L(x0, y0) * (1 - fx) + L(x1, y0) * fx) * (1 - fy) + (L(x0, y1) * (1 - fx) + L(x1, y1) * fx) * fy;
  return c / 255.f;
}

bool CBspWorld::LightPoint(const Vector& p, Vector& color) const {
  BspTrace tr; Trace(0, p, p - Vector(0, 0, 2048), tr, true);
  if (tr.fraction >= 1.f || tr.allsolid) return false;
  int fi = FaceAtImpact(tr); if (fi < 0) return false;
  color = SampleFaceLight(fi, tr.endpos - (tr.brushEntity >= 0 ? brushEnts[tr.brushEntity].origin : Vector()));
  return true;
}
