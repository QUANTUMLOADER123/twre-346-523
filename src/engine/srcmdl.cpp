// srcmdl.cpp - Source studio model loader
#include <cctype>
#include "srcmdl.hpp"
#include "srcfs.hpp"
#include "srcmaterial.hpp"
#include "convar.hpp"
#include <cstring>
#include <cmath>
#include <unordered_map>
#include <algorithm>

namespace {
template <class T> T Rd(const std::vector<uint8_t>& d, size_t o) { T v{}; if (o + sizeof(T) <= d.size()) std::memcpy(&v, &d[o], sizeof(T)); return v; }
std::string Str(const std::vector<uint8_t>& d, size_t o) {
  std::string s; while (o < d.size() && d[o]) s += (char)d[o++]; return s;
}
float Half(uint16_t h) {
  uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
  if (e == 0) return (s ? -1.f : 1.f) * std::ldexp((float)m, -24);
  if (e == 31) return s ? -65504.f : 65504.f;
  return (s ? -1.f : 1.f) * std::ldexp((float)(m | 1024), (int)e - 25);
}
Quat EulerQuat(const Vector& a) {   // RadianEuler -> quaternion (Source AngleQuaternion)
  float sr = std::sin(a.x * 0.5f), cr = std::cos(a.x * 0.5f), sp = std::sin(a.y * 0.5f), cp = std::cos(a.y * 0.5f), sy = std::sin(a.z * 0.5f), cy = std::cos(a.z * 0.5f);
  Quat q; q.x = sr * cp * cy - cr * sp * sy; q.y = cr * sp * cy + sr * cp * sy; q.z = cr * cp * sy - sr * sp * cy; q.w = cr * cp * cy + sr * sp * sy; return q;
}
// RLE animation value at frame k (Source ExtractAnimValue, without interpolation)
float AnimValue(const uint8_t* base, const uint8_t* end, int k, float scale) {
  const uint8_t* p = base;
  for (int guard = 0; guard < 4096; guard++) {
    if (p + 2 > end) return 0;
    int valid = p[0], total = p[1];
    if (total == 0) return 0;
    if (total <= k) { k -= total; p += (valid + 1) * 2; continue; }
    int idx = valid > k ? k + 1 : valid;
    const uint8_t* v = p + idx * 2; if (v + 2 > end) return 0;
    int16_t s; std::memcpy(&s, v, 2); return s * scale;
  }
  return 0;
}
} // namespace

void CSrcModel::Unload() {
  for (auto& m : meshes) if (m.uploaded) { UnloadMesh(m.gpu); }
  *this = CSrcModel();
}

bool CSrcModel::Load(const std::string& path) {
  Unload();
  name = srcfs::Normalize(path);
  std::string base = name.size() > 4 ? name.substr(0, name.size() - 4) : name;
  if (!srcfs::Read(name, data) || data.size() < 408 || std::memcmp(data.data(), "IDST", 4) != 0) return false;
  const auto& d = data;
  version = Rd<int>(d, 4);
  if (version < 44 || version > 49) { Warning("%s: unsupported MDL version %d\n", name.c_str(), version); return false; }
  eyePos = Rd<Vector>(d, 80); hullMin = Rd<Vector>(d, 104); hullMax = Rd<Vector>(d, 116);
  // ---- bones
  int numBones = Rd<int>(d, 156), boneIdx = Rd<int>(d, 160);
  for (int i = 0; i < numBones && i < 256; i++) {
    size_t b = (size_t)boneIdx + (size_t)i * 216; if (b + 216 > d.size()) break;
    SrcMdlBone bn; bn.name = Str(d, b + Rd<int>(d, b)); bn.parent = Rd<int>(d, b + 4);
    bn.pos = Rd<Vector>(d, b + 32);
    bn.quat.x = Rd<float>(d, b + 44); bn.quat.y = Rd<float>(d, b + 48); bn.quat.z = Rd<float>(d, b + 52); bn.quat.w = Rd<float>(d, b + 56);
    bn.rot = Rd<Vector>(d, b + 60); bn.posScale = Rd<Vector>(d, b + 72); bn.rotScale = Rd<Vector>(d, b + 84);
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) bn.poseToBone.m[r][c] = Rd<float>(d, b + 96 + (r * 4 + c) * 4);
    bones.push_back(bn);
  }
  // ---- textures / materials
  int numTex = Rd<int>(d, 204), texIdx = Rd<int>(d, 208), numCd = Rd<int>(d, 212), cdIdx = Rd<int>(d, 216);
  for (int i = 0; i < numTex; i++) { size_t t = (size_t)texIdx + i * 64; textures.push_back(srcfs::Normalize(Str(d, t + Rd<int>(d, t)))); }
  for (int i = 0; i < numCd; i++) { std::string s = srcfs::Normalize(Str(d, Rd<int>(d, cdIdx + i * 4))); while (!s.empty() && s.back() == '/') s.pop_back(); cdmaterials.push_back(s); }
  int numSkinRef = Rd<int>(d, 220), numFam = Rd<int>(d, 224), skinIdx = Rd<int>(d, 228);
  for (int f = 0; f < numFam; f++) { std::vector<int16_t> row; for (int r = 0; r < numSkinRef; r++) row.push_back(Rd<int16_t>(d, skinIdx + (f * numSkinRef + r) * 2)); skins.push_back(row); }
  // ---- attachments / hitboxes
  int numAtt = Rd<int>(d, 240), attIdx = Rd<int>(d, 244);
  for (int i = 0; i < numAtt; i++) {
    size_t a = (size_t)attIdx + i * 92; SrcMdlAttachment at; at.name = Str(d, a + Rd<int>(d, a)); at.bone = Rd<int>(d, a + 8);
    for (int r = 0; r < 3; r++) for (int c = 0; c < 4; c++) at.local.m[r][c] = Rd<float>(d, a + 12 + (r * 4 + c) * 4);
    attachments.push_back(at);
  }
  if (Rd<int>(d, 172) > 0) {
    size_t hs = Rd<int>(d, 176); int nh = Rd<int>(d, hs + 4); size_t hb = hs + Rd<int>(d, hs + 8);
    for (int i = 0; i < nh; i++) { size_t h = hb + i * 68; SrcMdlHitbox x; x.bone = Rd<int>(d, h); x.group = Rd<int>(d, h + 4); x.mins = Rd<Vector>(d, h + 8); x.maxs = Rd<Vector>(d, h + 20); hitboxes.push_back(x); }
  }
  // ---- pose parameters / include models
  int numPose = Rd<int>(d, 300), poseIdx = Rd<int>(d, 304);
  for (int i = 0; i < numPose && i < 64; i++) {
    size_t pp = (size_t)poseIdx + i * 20; SrcMdlPoseParam q; q.name = Str(d, pp + Rd<int>(d, pp));
    q.flags = Rd<int>(d, pp + 4); q.start = Rd<float>(d, pp + 8); q.end = Rd<float>(d, pp + 12); q.loop = Rd<float>(d, pp + 16);
    poseParams.push_back(q);
  }
  int numInc = Rd<int>(d, 336), incIdx = Rd<int>(d, 340);
  for (int i = 0; i < numInc && i < 32; i++) { size_t g = (size_t)incIdx + i * 8; includeModels.push_back(srcfs::Normalize(Str(d, g + Rd<int>(d, g + 4)))); }
  // ---- animations / sequences
  int numAnim = Rd<int>(d, 180), animIdx = Rd<int>(d, 184);
  for (int i = 0; i < numAnim; i++) {
    size_t a = (size_t)animIdx + i * 100; SrcMdlAnim an;
    an.name = Str(d, a + Rd<int>(d, a + 4)); an.fps = Rd<float>(d, a + 8); an.flags = Rd<int>(d, a + 12); an.numFrames = std::max(1, Rd<int>(d, a + 16));
    int block = Rd<int>(d, a + 52), idx = Rd<int>(d, a + 56);
    an.sectionOffset = Rd<int>(d, a + 80) ? (int)a + Rd<int>(d, a + 80) : 0; an.sectionFrames = Rd<int>(d, a + 84);
    an.animOffset = (int)a + idx; an.descOffset = (int)a; an.valid = block == 0;
    anims.push_back(an);
  }
  int numSeq = Rd<int>(d, 188), seqIdx = Rd<int>(d, 192);
  for (int i = 0; i < numSeq; i++) {
    size_t s = (size_t)seqIdx + i * 212; SrcMdlSeq sq;
    sq.label = Str(d, s + Rd<int>(d, s + 4)); sq.activity = Str(d, s + Rd<int>(d, s + 8)); sq.flags = Rd<int>(d, s + 12); sq.actWeight = Rd<int>(d, s + 20);
    sq.groupSize[0] = std::max(1, Rd<int>(d, s + 68)); sq.groupSize[1] = std::max(1, Rd<int>(d, s + 72));
    int ai = Rd<int>(d, s + 60);
    for (int k = 0; k < sq.groupSize[0] * sq.groupSize[1]; k++) sq.anims.push_back(Rd<int16_t>(d, s + ai + k * 2));
    sq.fadeIn = Rd<float>(d, s + 104); sq.fadeOut = Rd<float>(d, s + 108); sq.looping = (sq.flags & 1) != 0; sq.delta = (sq.flags & 4) != 0;
    for (int k = 0; k < 2; k++) { sq.paramIndex[k] = Rd<int>(d, s + 76 + k * 4); sq.paramStart[k] = Rd<float>(d, s + 84 + k * 4); sq.paramEnd[k] = Rd<float>(d, s + 92 + k * 4); }
    int wl = Rd<int>(d, s + 156);
    if (wl) for (int b = 0; b < numBones; b++) sq.weights.push_back(Rd<float>(d, s + wl + b * 4));
    seqs.push_back(sq);
  }

  // ---- geometry (VVD + VTX)
  std::vector<uint8_t> vvd, vtx;
  if (!srcfs::Read(base + ".vvd", vvd) || vvd.size() < 64 || std::memcmp(vvd.data(), "IDSV", 4) != 0) { loaded = true; return true; }   // animation-only model
  if (!srcfs::Read(base + ".dx90.vtx", vtx) && !srcfs::Read(base + ".dx80.vtx", vtx) && !srcfs::Read(base + ".sw.vtx", vtx)) { loaded = true; return true; }
  int numFixups = Rd<int>(vvd, 48), fixupStart = Rd<int>(vvd, 52), vertStart = Rd<int>(vvd, 56);
  int numLod0 = Rd<int>(vvd, 16);
  struct V { float w[3]; uint8_t b[3]; uint8_t nb; Vector p, n; float u, v; };
  std::vector<V> verts;
  auto vertAt = [&](int i) { V v{}; size_t o = (size_t)vertStart + (size_t)i * 48; if (o + 48 <= vvd.size()) { std::memcpy(v.w, &vvd[o], 12); std::memcpy(v.b, &vvd[o + 12], 3); v.nb = vvd[o + 15]; v.p = Rd<Vector>(vvd, o + 16); v.n = Rd<Vector>(vvd, o + 28); v.u = Rd<float>(vvd, o + 40); v.v = Rd<float>(vvd, o + 44); } return v; };
  if (numFixups > 0) {
    for (int f = 0; f < numFixups; f++) {
      size_t fo = (size_t)fixupStart + f * 12; int lod = Rd<int>(vvd, fo), src = Rd<int>(vvd, fo + 4), n = Rd<int>(vvd, fo + 8);
      if (lod < 0) continue;
      for (int k = 0; k < n; k++) verts.push_back(vertAt(src + k));
    }
  } else for (int i = 0; i < numLod0; i++) verts.push_back(vertAt(i));

  // body group choices that differ from 0 (the CS:GO view models from ARC9 carry attachment variants as body groups)
  std::vector<int> bodygroups;
  {
    std::string base = path.substr(path.find_last_of("/\\") + 1);
    for (auto& ch : base) ch = (char)std::tolower((unsigned char)ch);
    if (base == "c_snip_ssg08.mdl") bodygroups = { 0, 0, 0, 0, 0, 2 };   // CS:GO SSG 08: no bipod
    if (base == "c_snip_scar20.mdl") bodygroups = { 0, 2, 0, 0, 0, 1 };  // CS:GO SCAR-20: scope mount (ARC9 element "mount")
  }
  int vtxBodyParts = Rd<int>(vtx, 28), vtxBpOff = Rd<int>(vtx, 32);
  int numBp = Rd<int>(d, 232), bpIdx = Rd<int>(d, 236);
  for (int bp = 0; bp < numBp && bp < vtxBodyParts; bp++) {
    size_t mbp = (size_t)bpIdx + bp * 16; int nModels = Rd<int>(d, mbp + 4); size_t mModels = mbp + Rd<int>(d, mbp + 12);
    { size_t nmOff = mbp + (size_t)Rd<int>(d, mbp); bodyPartNames.push_back(nmOff < d.size() ? Str(d, nmOff) : std::string()); }
    size_t vbp = (size_t)vtxBpOff + bp * 8; size_t vModels = vbp + Rd<int>(vtx, vbp + 4);
    if (nModels <= 0) continue;
    int mi = bp < (int)bodygroups.size() ? std::min(bodygroups[bp], nModels - 1) : 0;
    size_t mm = mModels + (size_t)mi * 148;
    int nMeshes = Rd<int>(d, mm + 72); size_t mMeshes = mm + Rd<int>(d, mm + 76); int modelVertIndex = Rd<int>(d, mm + 84) / 48;
    size_t vm = vModels + (size_t)mi * 8; size_t vLods = vm + Rd<int>(vtx, vm + 4);
    size_t lod0 = vLods; int vNumMeshes = Rd<int>(vtx, lod0); size_t vMeshes = lod0 + Rd<int>(vtx, lod0 + 4);
    for (int me = 0; me < nMeshes && me < vNumMeshes; me++) {
      size_t mmesh = mMeshes + (size_t)me * 116; int material = Rd<int>(d, mmesh); int meshVertOff = Rd<int>(d, mmesh + 12);
      size_t vmesh = vMeshes + (size_t)me * 9; int nSg = Rd<int>(vtx, vmesh); size_t sgOff = vmesh + Rd<int>(vtx, vmesh + 4);
      SrcMdlMesh out; out.skinRef = material; out.bodyPart = bp;
      std::unordered_map<int, uint16_t> remap;
      for (int sg = 0; sg < nSg; sg++) {
        size_t g = sgOff + (size_t)sg * (version >= 49 ? 33 : 25);
        int nv = Rd<int>(vtx, g), vOff = Rd<int>(vtx, g + 4), ni = Rd<int>(vtx, g + 8), iOff = Rd<int>(vtx, g + 12), ns = Rd<int>(vtx, g + 16), sOff = Rd<int>(vtx, g + 20);
        auto emitVert = [&](int sgVert) -> int {
          if (sgVert < 0 || sgVert >= nv) return -1;
          size_t vv = g + vOff + (size_t)sgVert * 9;
          int orig = Rd<uint16_t>(vtx, vv + 4);
          int vi = modelVertIndex + meshVertOff + orig;
          if (vi < 0 || vi >= (int)verts.size()) return -1;
          auto it = remap.find(vi); if (it != remap.end()) return it->second;
          if (out.pos.size() / 3 >= 65535) return -1;
          const V& v = verts[vi];
          uint16_t id = (uint16_t)(out.pos.size() / 3);
          out.pos.insert(out.pos.end(), { v.p.x, v.p.y, v.p.z }); out.normal.insert(out.normal.end(), { v.n.x, v.n.y, v.n.z });
          out.uv.insert(out.uv.end(), { v.u, v.v });
          for (int k = 0; k < 3; k++) { out.bone.push_back(k < v.nb ? v.b[k] : 0); out.weight.push_back(k < v.nb ? v.w[k] : 0.f); }
          remap[vi] = id; return id;
        };
        auto tri = [&](int a, int b, int c) {
          int A = emitVert(a), B = emitVert(b), C = emitVert(c); if (A < 0 || B < 0 || C < 0) return;
          // Source triangles are clockwise -> reverse for counter-clockwise front faces
          out.idx.push_back((uint16_t)A); out.idx.push_back((uint16_t)C); out.idx.push_back((uint16_t)B);
        };
        for (int s = 0; s < ns; s++) {
          size_t st = g + sOff + (size_t)s * 27;
          int sni = Rd<int>(vtx, st), sio = Rd<int>(vtx, st + 4); uint8_t flags = vtx.size() > st + 18 ? vtx[st + 18] : 1;
          auto index = [&](int k) { return (int)Rd<uint16_t>(vtx, g + iOff + (size_t)(sio + k) * 2); };
          if (flags & 2) { for (int k = 0; k + 2 < sni; k++) { if (k & 1) tri(index(k + 1), index(k), index(k + 2)); else tri(index(k), index(k + 1), index(k + 2)); } }
          else for (int k = 0; k + 2 < sni; k += 3) tri(index(k), index(k + 1), index(k + 2));
        }
        if (ns == 0) for (int k = 0; k + 2 < ni; k += 3) tri(Rd<uint16_t>(vtx, g + iOff + k * 2), Rd<uint16_t>(vtx, g + iOff + (k + 1) * 2), Rd<uint16_t>(vtx, g + iOff + (k + 2) * 2));
      }
      if (!out.idx.empty()) {
        if (!out.pos.empty()) {
          out.mn = out.mx = Vector(out.pos[0], out.pos[1], out.pos[2]);
          for (size_t q = 3; q + 2 < out.pos.size(); q += 3) {
            Vector p(out.pos[q], out.pos[q + 1], out.pos[q + 2]);
            out.mn = Vector(std::min(out.mn.x, p.x), std::min(out.mn.y, p.y), std::min(out.mn.z, p.z));
            out.mx = Vector(std::max(out.mx.x, p.x), std::max(out.mx.y, p.y), std::max(out.mx.z, p.z));
          }
        }
        meshes.push_back(std::move(out));
      }
    }
  }
  // Y-up detection: a standing skeleton spans ~72 units along its up axis, so the axis with by far the
  loaded = true;
  return true;
}

Texture2D CSrcModel::MeshTexture(const SrcMdlMesh& m, int skin, bool* translucent, bool* alphatest) const {
  int fam = (skin >= 0 && skin < (int)skins.size()) ? skin : 0;
  int tex = m.skinRef;
  if (fam < (int)skins.size() && m.skinRef < (int)skins[fam].size()) tex = skins[fam][m.skinRef];
  if (tex < 0 || tex >= (int)textures.size()) { Texture2D t{}; return t; }
  // the material lookup builds and normalizes path strings - do it once per texture slot, not per mesh per frame
  if (m_matGen != SrcMat_Generation() || m_matCache.size() != textures.size()) { m_matCache.assign(textures.size(), MatCache{}); m_matGen = SrcMat_Generation(); }
  MatCache& c = m_matCache[tex];
  if (!c.valid) {
    const SrcMaterial& mat = SrcMat_FindInDirs(textures[tex], cdmaterials);
    c.tex = mat.tex; c.translucent = mat.translucent || mat.additive; c.alphatest = mat.alphatest; c.valid = true;
  }
  if (translucent) *translucent = c.translucent;
  if (alphatest) *alphatest = c.alphatest;
  return c.tex;
}

// ARC9 render-target scope glass (needs a second view render) - not drawn
bool CSrcModel::MeshHidden(const SrcMdlMesh& m, int skin) const {
  int fam = (skin >= 0 && skin < (int)skins.size()) ? skin : 0;
  int tex = m.skinRef;
  if (fam < (int)skins.size() && m.skinRef < (int)skins[fam].size()) tex = skins[fam][m.skinRef];
  if (tex < 0 || tex >= (int)textures.size()) return false;
  const std::string& t = textures[tex];
  return t.size() >= 7 && srcfs::Normalize(t.substr(t.size() - 7)) == "rtglass";
}

int CSrcModel::FindNamed(const std::string& q, bool bone) const {
  auto& cache = bone ? m_boneIndex : m_seqIndex;
  auto it = cache.find(q);
  if (it != cache.end()) return it->second;
  std::string l = srcfs::Normalize(q);
  int r = -1;
  if (bone) { for (int i = 0; i < (int)bones.size(); i++) if (srcfs::Normalize(bones[i].name) == l) { r = i; break; } }
  else { for (int i = 0; i < (int)seqs.size(); i++) if (srcfs::Normalize(seqs[i].label) == l) { r = i; break; } }
  if (cache.size() > 4096) cache.clear();
  cache.emplace(q, r);
  return r;
}
int CSrcModel::FindSequence(const std::string& label) const { return FindNamed(label, false); }
int CSrcModel::FindBone(const std::string& n) const { return FindNamed(n, true); }

void CSrcModel::ExtractAnimBone(const uint8_t* p, int bone, int frame, float, Vector& pos, Quat& q, int animFlags) const {
  const uint8_t* end = data.data() + data.size();
  const SrcMdlBone& b = bones[bone];
  uint8_t flags = p[1];
  const uint8_t* dat = p + 4;
  bool delta = (flags & 0x10) || (animFlags & 0x4);
  // rotation
  if (flags & 0x02) {          // Quaternion48
    uint16_t x, y, zw; std::memcpy(&x, dat, 2); std::memcpy(&y, dat + 2, 2); std::memcpy(&zw, dat + 4, 2);
    q.x = ((int)x - 32768) * (1.f / 32768.f); q.y = ((int)y - 32768) * (1.f / 32768.f); q.z = ((int)(zw & 0x7fff) - 16384) * (1.f / 16384.f);
    q.w = std::sqrt(std::max(0.f, 1.f - q.x * q.x - q.y * q.y - q.z * q.z)); if (zw & 0x8000) q.w = -q.w;
  } else if (flags & 0x20) {   // Quaternion64
    uint64_t v; std::memcpy(&v, dat, 8);
    q.x = ((int)(v & 0x1fffff) - 1048576) * (1.f / 1048576.5f); q.y = ((int)((v >> 21) & 0x1fffff) - 1048576) * (1.f / 1048576.5f);
    q.z = ((int)((v >> 42) & 0x1fffff) - 1048576) * (1.f / 1048576.5f);
    q.w = std::sqrt(std::max(0.f, 1.f - q.x * q.x - q.y * q.y - q.z * q.z)); if (v >> 63) q.w = -q.w;
  } else if (flags & 0x08) {   // animated euler
    const uint8_t* vp = dat; Vector e;
    for (int k = 0; k < 3; k++) { int16_t off; std::memcpy(&off, vp + k * 2, 2); e[k] = off ? AnimValue(vp + off, end, frame, b.rotScale[k]) : 0.f; }
    if (!delta) e = e + b.rot;
    q = EulerQuat(e);
  } else q = delta ? Quat() : b.quat;
  // position
  const uint8_t* pp = dat + ((flags & 0x02) ? 6 : 0) + ((flags & 0x20) ? 8 : 0);
  if (flags & 0x01) {
    uint16_t h[3]; std::memcpy(h, pp, 6); pos = Vector(Half(h[0]), Half(h[1]), Half(h[2]));
  } else if (flags & 0x04) {
    const uint8_t* vp = dat + ((flags & 0x08) ? 6 : 0);
    Vector e; for (int k = 0; k < 3; k++) { int16_t off; std::memcpy(&off, vp + k * 2, 2); e[k] = off ? AnimValue(vp + off, end, frame, b.posScale[k]) : 0.f; }
    pos = delta ? e : e + b.pos;
  } else pos = delta ? Vector() : b.pos;
}

void CSrcModel::SampleAnim(int anim, float frame, std::vector<Vector>& pos, std::vector<Quat>& q) const {
  size_t nb = bones.size();
  pos.resize(nb); q.resize(nb);
  const bool deltaAnim = anim >= 0 && anim < (int)anims.size() && (anims[anim].flags & 0x4);
  for (size_t i = 0; i < nb; i++) { pos[i] = deltaAnim ? Vector() : bones[i].pos; q[i] = deltaAnim ? Quat() : bones[i].quat; }
  if (anim < 0 || anim >= (int)anims.size() || !anims[anim].valid) return;
  const SrcMdlAnim& a = anims[anim];
  frame = Clamp(frame, 0.f, (float)(a.numFrames - 1));
  int f0 = (int)frame, f1 = std::min(f0 + 1, a.numFrames - 1); float t = frame - f0;
  auto sample = [&](int f, std::vector<Vector>& P, std::vector<Quat>& Q) {
    P.resize(nb); Q.resize(nb);
    for (size_t i = 0; i < nb; i++) { P[i] = deltaAnim ? Vector() : bones[i].pos; Q[i] = deltaAnim ? Quat() : bones[i].quat; }
    int off = a.animOffset, lf = f;
    if (a.sectionFrames > 0 && a.sectionOffset) {
      int sec = f / a.sectionFrames;
      int secAnimIdx = Rd<int>(data, (size_t)a.sectionOffset + sec * 8 + 4);
      int descBase = a.descOffset;
      off = descBase + secAnimIdx; lf = f - sec * a.sectionFrames;
    }
    for (int guard = 0; guard < 512 && off > 0 && (size_t)off + 4 <= data.size(); guard++) {
      const uint8_t* p = data.data() + off;
      int bone = p[0];
      if (bone < (int)nb) ExtractAnimBone(p, bone, lf, 0, P[bone], Q[bone], a.flags);
      int16_t next; std::memcpy(&next, p + 2, 2);
      if (!next) break;
      off += next;
    }
  };
  std::vector<Vector> P1; std::vector<Quat> Q1;
  sample(f0, pos, q);
  if (t > 0.001f && f1 != f0) {
    sample(f1, P1, Q1);
    for (size_t i = 0; i < nb; i++) { pos[i] = pos[i] + (P1[i] - pos[i]) * t; q[i] = QuatSlerp(q[i], Q1[i], t); }
  }
}

void CSrcModel::BuildBones(const std::vector<Vector>& pos, const std::vector<Quat>& q, std::vector<matrix3x4_t>& out) const {
  out.resize(bones.size());
  // a sequence of an $includemodel only covers that library's bones (the CS:GO animsets have two fewer than the
  // agent itself): the bones it does not touch keep their reference transform
  for (size_t i = 0; i < bones.size(); i++) {
    matrix3x4_t local;
    QuaternionMatrix(i < q.size() ? q[i] : bones[i].quat, i < pos.size() ? pos[i] : bones[i].pos, Vector(1, 1, 1), local);
    int p = bones[i].parent;
    if (p >= 0 && p < (int)i) ConcatTransforms(out[p], local, out[i]); else out[i] = local;
  }
}

void CSrcModel::UploadStatic() {
  for (auto& m : meshes) {
    if (m.uploaded || m.idx.empty()) continue;
    Mesh g{}; g.vertexCount = (int)(m.pos.size() / 3); g.triangleCount = (int)(m.idx.size() / 3);
    g.vertices = (float*)MemAlloc((unsigned)(m.pos.size() * sizeof(float))); std::memcpy(g.vertices, m.pos.data(), m.pos.size() * sizeof(float));
    g.normals = (float*)MemAlloc((unsigned)(m.normal.size() * sizeof(float))); std::memcpy(g.normals, m.normal.data(), m.normal.size() * sizeof(float));
    g.texcoords = (float*)MemAlloc((unsigned)(m.uv.size() * sizeof(float))); std::memcpy(g.texcoords, m.uv.data(), m.uv.size() * sizeof(float));
    g.indices = (unsigned short*)MemAlloc((unsigned)(m.idx.size() * sizeof(uint16_t))); std::memcpy(g.indices, m.idx.data(), m.idx.size() * sizeof(uint16_t));
    UploadMesh(&g, false);
    m.gpu = g; m.uploaded = true;
  }
}

namespace {
std::unordered_map<std::string, std::unique_ptr<CSrcModel>> s_models;
}
CSrcModel* SrcMdl_Get(const std::string& path) {
  std::string key = srcfs::Normalize(path);
  auto it = s_models.find(key);
  if (it != s_models.end()) return it->second && it->second->loaded ? it->second.get() : nullptr;
  auto m = std::make_unique<CSrcModel>();
  if (!m->Load(key)) { s_models[key] = nullptr; return nullptr; }
  CSrcModel* r = m.get(); s_models[key] = std::move(m);
  // $includemodel animation libraries (cs_player_shared.mdl ...): map bones by name
  for (const std::string& inc : r->includeModels) {
    if (inc == key) continue;
    CSrcModel* im = SrcMdl_Get(inc); if (!im) continue;
    std::vector<int> map(r->bones.size(), -1);
    for (size_t b = 0; b < r->bones.size(); b++) map[b] = im->FindBone(r->bones[b].name);
    r->includes.push_back(im); r->includeBoneMap.push_back(std::move(map));
  }
  return r;
}
void SrcMdl_Clear() { for (auto& [k, m] : s_models) if (m) m->Unload(); s_models.clear(); }

// console: which bones each mesh is skinned to (debug for view model / hand placement)
static ConCommand mdl_meshes("mdl_meshes", [](const CCommand& c) {
  if (c.ArgC() < 2) { Msg("usage: mdl_meshes models/weapons/v_pist_deagle.mdl\n"); return; }
  CSrcModel* m = SrcMdl_Get(c.Arg(1));
  if (!m) { Msg("can't load %s\n", c.Arg(1)); return; }
  for (size_t i = 0; i < m->meshes.size(); i++) {
    const SrcMdlMesh& me = m->meshes[i];
    std::vector<int> bones;
    for (size_t v = 0; v < me.pos.size() / 3; v++) for (int k = 0; k < 3; k++) if (me.weight[v * 3 + k] > 0) bones.push_back(me.bone[v * 3 + k]);
    std::sort(bones.begin(), bones.end()); bones.erase(std::unique(bones.begin(), bones.end()), bones.end());
    Msg("mesh %d bp %d skin %d verts %d tris %d bones %d\n", (int)i, me.bodyPart, me.skinRef, (int)me.pos.size() / 3, (int)me.idx.size() / 3, (int)bones.size());
    if (me.pos.empty()) continue;
    Vector mn(me.pos[0], me.pos[1], me.pos[2]), mx = mn;
    for (size_t v = 1; v < me.pos.size() / 3; v++) {
      Vector p(me.pos[v * 3], me.pos[v * 3 + 1], me.pos[v * 3 + 2]);
      mn = Vector(std::min(mn.x, p.x), std::min(mn.y, p.y), std::min(mn.z, p.z));
      mx = Vector(std::max(mx.x, p.x), std::max(mx.y, p.y), std::max(mx.z, p.z));
    }
    Msg("     bounds %.2f %.2f %.2f .. %.2f %.2f %.2f  size %.2f %.2f %.2f\n", mn.x, mn.y, mn.z, mx.x, mx.y, mx.z, mx.x - mn.x, mx.y - mn.y, mx.z - mn.z);
    for (int b : bones) Msg("   %3d %s\n", b, b >= 0 && b < (int)m->bones.size() ? m->bones[b].name.c_str() : "?");
  }
}, "Print the bone indices of every mesh of a Source model");

// console: world positions of the bones of a model in the reference pose (debug)
static ConCommand mdl_bones("mdl_bones", [](const CCommand& c) {
  if (c.ArgC() < 2) { Msg("usage: mdl_bones models/weapons/v_pist_deagle.mdl\n"); return; }
  CSrcModel* m = SrcMdl_Get(c.Arg(1));
  if (!m) { Msg("can't load %s\n", c.Arg(1)); return; }
  std::vector<Vector> pos; std::vector<Quat> q;
  m->SampleAnim(-1, 0, pos, q);
  std::vector<matrix3x4_t> bm; m->BuildBones(pos, q, bm);
  Msg("%s: %d bones\n", m->name.c_str(), (int)m->bones.size());
  for (size_t i = 0; i < bm.size(); i++)
    Msg("  %3d %-38s origin %8.2f %8.2f %8.2f  col0 %6.2f %6.2f %6.2f col1 %6.2f %6.2f %6.2f col2 %6.2f %6.2f %6.2f\n",
        (int)i, m->bones[i].name.c_str(), bm[i][0][3], bm[i][1][3], bm[i][2][3],
        bm[i][0][0], bm[i][1][0], bm[i][2][0], bm[i][0][1], bm[i][1][1], bm[i][2][1], bm[i][0][2], bm[i][1][2], bm[i][2][2]);
}, "Print the world origin of every bone of a Source model in the reference pose");

// console: dump a model (sequences / activities / pose parameters / attachments)
static ConCommand mdl_info("mdl_info", [](const CCommand& c) {
  if (c.ArgC() < 2) { Msg("usage: mdl_info models/player/ct_urban.mdl\n"); return; }
  CSrcModel* m = SrcMdl_Get(c.Arg(1));
  if (!m) { Msg("can't load %s\n", c.Arg(1)); return; }
  Msg("%s v%d: %d bones %d meshes %d anims %d seqs %d attachments %d textures\n", m->name.c_str(), m->version, (int)m->bones.size(), (int)m->meshes.size(), (int)m->anims.size(), (int)m->seqs.size(), (int)m->attachments.size(), (int)m->textures.size());
  for (auto& i : m->includeModels) Msg("  include %s\n", i.c_str());
  for (auto& p : m->poseParams) Msg("  pose %s [%g..%g] loop %g\n", p.name.c_str(), p.start, p.end, p.loop);
  for (auto& a : m->attachments) Msg("  attachment %s bone %d\n", a.name.c_str(), a.bone);
  for (auto& t : m->textures) Msg("  texture %s\n", t.c_str());
  for (size_t i = 0; i < m->meshes.size(); i++) Msg("  mesh %d skinref %d verts %d tris %d\n", (int)i, m->meshes[i].skinRef, (int)m->meshes[i].pos.size() / 3, (int)m->meshes[i].idx.size() / 3);
  if (c.ArgC() > 2) for (size_t i = 0; i < m->bones.size(); i++) Msg("  bone %d %s parent %d pos %.1f %.1f %.1f\n", (int)i, m->bones[i].name.c_str(), m->bones[i].parent, m->bones[i].pos.x, m->bones[i].pos.y, m->bones[i].pos.z);
  for (size_t i = 0; i < m->seqs.size(); i++) {
    auto& s = m->seqs[i]; const SrcMdlAnim* a = s.anims.empty() || s.anims[0] < 0 || s.anims[0] >= (int)m->anims.size() ? nullptr : &m->anims[s.anims[0]];
    Msg("  seq %3d %-28s %-30s flags %x grid %dx%d frames %d fps %g%s\n", (int)i, s.label.c_str(), s.activity.c_str(), s.flags, s.groupSize[0], s.groupSize[1], a ? a->numFrames : 0, a ? a->fps : 0, a && !a->valid ? " (animblock)" : "");
  }
}, "Print the sequences / pose parameters / attachments of a Source model");

// ------------------------------------------------------------------------------------------------
// sequences across $includemodel libraries, blending
float SrcPoseValues::Get(const std::string& n) const {
  std::string l = srcfs::Normalize(n);
  if (l == "move_x") return move_x; if (l == "move_y") return move_y; if (l == "move_yaw") return move_yaw;
  if (l == "body_yaw" || l == "aim_yaw") return body_yaw; if (l == "body_pitch" || l == "aim_pitch") return body_pitch;
  if (l == "stand") return stand; if (l == "speed") return speed;
  for (auto& [k, v] : extra) if (l == k) return v;
  return 0;
}

SrcSeqRef CSrcModel::FindSeq(const std::string& label) const {
  int s = FindSequence(label); if (s >= 0) return { this, s, -1 };
  for (size_t i = 0; i < includes.size(); i++) { int k = includes[i]->FindSequence(label); if (k >= 0) return { includes[i], k, (int)i }; }
  return {};
}
std::vector<SrcSeqRef> CSrcModel::FindSeqsByActivity(const std::string& act) const {
  std::vector<SrcSeqRef> out; std::string a = srcfs::Normalize(act);
  for (int i = 0; i < (int)seqs.size(); i++) if (srcfs::Normalize(seqs[i].activity) == a) out.push_back({ this, i, -1 });
  return out;
}
static const SrcMdlAnim* FirstAnim(const SrcSeqRef& r) {
  if (!r) return nullptr; const SrcMdlSeq& s = r.src->seqs[r.seq];
  if (s.anims.empty() || s.anims[0] < 0 || s.anims[0] >= (int)r.src->anims.size()) return nullptr;
  return &r.src->anims[s.anims[0]];
}
float CSrcModel::SeqFps(const SrcSeqRef& r) const { const SrcMdlAnim* a = FirstAnim(r); return a ? a->fps : 30.f; }
int CSrcModel::SeqFrames(const SrcSeqRef& r) const { const SrcMdlAnim* a = FirstAnim(r); return a ? a->numFrames : 1; }

void CSrcModel::SampleSeq(const SrcSeqRef& r, float cycle, const SrcPoseValues& pv, std::vector<Vector>& pos, std::vector<Quat>& q, std::vector<float>* weights) const {
  const size_t nb = bones.size();
  pos.resize(nb); q.resize(nb);
  if (!r) { for (size_t i = 0; i < nb; i++) { pos[i] = bones[i].pos; q[i] = bones[i].quat; } if (weights) weights->assign(nb, 0.f); return; }
  const CSrcModel& m = *r.src; const SrcMdlSeq& s = m.seqs[r.seq];
  // blend grid position
  float t[2] = { 0, 0 }; int g[2] = { s.groupSize[0], s.groupSize[1] };
  for (int k = 0; k < 2; k++) {
    if (g[k] <= 1 || s.paramIndex[k] < 0 || s.paramIndex[k] >= (int)m.poseParams.size()) continue;
    float v = pv.Get(m.poseParams[s.paramIndex[k]].name);
    float a = s.paramStart[k], b = s.paramEnd[k];
    t[k] = std::fabs(b - a) > 1e-4f ? Clamp((v - a) / (b - a), 0.f, 1.f) * (g[k] - 1) : 0.f;
  }
  int x0 = std::min((int)t[0], std::max(0, g[0] - 2)), y0 = std::min((int)t[1], std::max(0, g[1] - 2));
  float fx = g[0] > 1 ? t[0] - x0 : 0.f, fy = g[1] > 1 ? t[1] - y0 : 0.f;
  auto animAt = [&](int x, int y) { x = std::min(x, g[0] - 1); y = std::min(y, g[1] - 1); size_t i = (size_t)y * g[0] + x; return i < s.anims.size() ? (int)s.anims[i] : -1; };
  auto frameOf = [&](int anim) { int nf = anim >= 0 && anim < (int)m.anims.size() ? m.anims[anim].numFrames : 1; return Clamp(cycle, 0.f, 1.f) * (nf - 1); };
  thread_local std::vector<Vector> P[4]; thread_local std::vector<Quat> Q[4];
  struct C { int x, y; float w; } corners[4] = { { x0, y0, (1 - fx) * (1 - fy) }, { x0 + 1, y0, fx * (1 - fy) }, { x0, y0 + 1, (1 - fx) * fy }, { x0 + 1, y0 + 1, fx * fy } };
  std::vector<Vector> SP; std::vector<Quat> SQ; bool first = true; float acc = 0;
  for (int c = 0; c < 4; c++) {
    if (corners[c].w < 1e-4f) continue;
    int an = animAt(corners[c].x, corners[c].y);
    m.SampleAnim(an, frameOf(an), P[c], Q[c]);
    if (first) { SP = P[c]; SQ = Q[c]; acc = corners[c].w; first = false; continue; }
    acc += corners[c].w; float k = corners[c].w / acc;
    for (size_t i = 0; i < SP.size(); i++) { SP[i] = SP[i] + (P[c][i] - SP[i]) * k; SQ[i] = QuatSlerp(SQ[i], Q[c][i], k); }
  }
  if (first) m.SampleAnim(-1, 0, SP, SQ);
  // map into our skeleton
  const std::vector<int>* map = r.include >= 0 && r.include < (int)includeBoneMap.size() ? &includeBoneMap[r.include] : nullptr;
  if (weights) weights->assign(nb, s.weights.empty() ? 1.f : 0.f);
  for (size_t i = 0; i < nb; i++) {
    int j = map ? (*map)[i] : (int)i;
    if (j >= 0 && j < (int)SP.size()) { pos[i] = SP[j]; q[i] = SQ[j]; if (weights && !s.weights.empty() && j < (int)s.weights.size()) (*weights)[i] = s.weights[j]; }
    else { pos[i] = s.delta ? Vector() : bones[i].pos; q[i] = s.delta ? Quat() : bones[i].quat; if (weights) (*weights)[i] = 0; }
  }
}

void CSrcModel::SkinMatrices(const std::vector<matrix3x4_t>& bm, const matrix3x4_t& modelToWorld, std::vector<matrix3x4_t>& out) const {
  out.resize(bones.size());
  for (size_t b = 0; b < bones.size(); b++) {
    matrix3x4_t t; ConcatTransforms(b < bm.size() ? bm[b] : matrix3x4_t(), bones[b].poseToBone, t);
    ConcatTransforms(modelToWorld, t, out[b]);
  }
}
Vector CSrcModel::AttachmentPos(int idx, const std::vector<matrix3x4_t>& bm, const matrix3x4_t& modelToWorld) const {
  if (idx < 0 || idx >= (int)attachments.size()) return VectorTransform(Vector(), modelToWorld);
  const SrcMdlAttachment& a = attachments[idx];
  matrix3x4_t bw; ConcatTransforms(modelToWorld, a.bone < (int)bm.size() ? bm[a.bone] : matrix3x4_t(), bw);
  return VectorTransform(Vector(a.local.m[0][3], a.local.m[1][3], a.local.m[2][3]), bw);
}
