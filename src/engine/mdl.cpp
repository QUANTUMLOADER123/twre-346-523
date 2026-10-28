#include "mdl.hpp"
#include "hlfs.hpp"
#include "studio.hpp"
#include "convar.hpp"
#include "rlgl.h"
#include <cstring>
#include <cmath>
#include <map>
#include <algorithm>

namespace {
template <class T> T Rd(const std::vector<uint8_t>& d, size_t off) { T v{}; if (off + sizeof(T) <= d.size()) std::memcpy(&v, &d[off], sizeof(T)); return v; }
int RdI(const std::vector<uint8_t>& d, size_t off) { return Rd<int32_t>(d, off); }
float RdF(const std::vector<uint8_t>& d, size_t off) { return Rd<float>(d, off); }
Vector RdV(const std::vector<uint8_t>& d, size_t off) { return { RdF(d, off), RdF(d, off + 4), RdF(d, off + 8) }; }
std::string RdS(const std::vector<uint8_t>& d, size_t off, size_t n) { std::string s; for (size_t i = 0; i < n && off + i < d.size() && d[off + i]; i++) s += (char)d[off + i]; return s; }
bool ReadFile(const std::string& p, std::vector<uint8_t>& out) {
  FILE* f = fopen(p.c_str(), "rb"); if (!f) return false;
  fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
  out.resize(n > 0 ? n : 0); size_t r = n > 0 ? fread(out.data(), 1, n, f) : 0; fclose(f);
  return (long)r == n;
}
// GoldSrc AngleQuaternion (radians, x = roll, y = pitch, z = yaw)
Quat EulerQuat(const float a[3]) {
  float sy = std::sin(a[2] * 0.5f), cy = std::cos(a[2] * 0.5f), sp = std::sin(a[1] * 0.5f), cp = std::cos(a[1] * 0.5f), sr = std::sin(a[0] * 0.5f), cr = std::cos(a[0] * 0.5f);
  return { sr * cp * cy - cr * sp * sy, cr * sp * cy + sr * cp * sy, cr * cp * sy - sr * sp * cy, cr * cp * cy + sr * sp * sy };
}
// mstudioanimvalue_t stream decode for one channel at an integer frame pair
void AnimValue(const uint8_t* base, const uint8_t* end, int frame, float s, float& out) {
  auto num = [&](const uint8_t* p, uint8_t& valid, uint8_t& total) { valid = p[0]; total = p[1]; };
  auto val = [&](const uint8_t* p) { int16_t v; std::memcpy(&v, p, 2); return (float)v; };
  const uint8_t* pv = base; int k = frame;
  uint8_t valid, total;
  for (int guard = 0; guard < 4096; guard++) {
    if (pv + 2 > end) { out = 0; return; }
    num(pv, valid, total);
    if (total == 0) { out = 0; return; }
    if (total > k) break;
    k -= total; pv += (valid + 1) * 2;
  }
  float v1, v2;
  if (valid > k) {
    v1 = val(pv + (k + 1) * 2);
    if (valid > k + 1) v2 = val(pv + (k + 2) * 2);
    else if (total > k + 1) v2 = v1;
    else v2 = (pv + (valid + 2) * 2 + 2 <= end) ? val(pv + (valid + 2) * 2) : v1;
  } else {
    v1 = val(pv + valid * 2);
    if (total > k + 1) v2 = v1;
    else v2 = (pv + (valid + 2) * 2 + 2 <= end) ? val(pv + (valid + 2) * 2) : v1;
  }
  out = v1 * (1.f - s) + v2 * s;
}
} // namespace

void CGoldSrcModel::Unload() {
  for (auto& t : textures) if (t.tex.id) UnloadTexture(t.tex);
  *this = CGoldSrcModel();
}

bool CGoldSrcModel::Load(const std::string& path) {
  Unload();
  std::vector<uint8_t>& d = m_data;
  if (!ReadFile(path, d) || d.size() < 244) return false;
  if (std::memcmp(d.data(), "IDST", 4) != 0 || RdI(d, 4) != 10) { Warning("%s: not a GoldSrc MDL v10\n", path.c_str()); return false; }
  name = RdS(d, 8, 64);
  // bones
  int nb = RdI(d, 140), bi = RdI(d, 144);
  if (nb <= 0 || nb > 256) return false;
  bones.resize(nb);
  for (int i = 0; i < nb; i++) {
    size_t o = bi + (size_t)i * 112; Bone& b = bones[i];
    b.name = RdS(d, o, 32); b.parent = RdI(d, o + 32);
    for (int j = 0; j < 6; j++) { b.value[j] = RdF(d, o + 64 + j * 4); b.scale[j] = RdF(d, o + 88 + j * 4); }
  }
  // sequences
  int ns = RdI(d, 164), si = RdI(d, 168);
  for (int i = 0; i < ns && i < 512; i++) {
    size_t o = si + (size_t)i * 176; Sequence s;
    s.name = hlfs::Lower(RdS(d, o, 32)); s.fps = RdF(d, o + 32); s.flags = RdI(d, o + 36); s.activity = RdI(d, o + 40);
    s.numFrames = std::max(1, RdI(d, o + 56)); s.numBlends = std::max(1, RdI(d, o + 120)); s.animOffset = RdI(d, o + 124); s.group = RdI(d, o + 156);
    seqs.push_back(s);
  }
  // external sequence groups: <name>01.mdl ...
  int ng = RdI(d, 172);
  m_groups.resize(std::max(1, ng));
  std::string stem = path.substr(0, path.size() - 4);
  for (int g = 1; g < ng && g < 32; g++) {
    char suf[8]; snprintf(suf, sizeof(suf), "%02d.mdl", g);
    std::string gp = hlfs::ResolveCase(stem + suf);
    if (!gp.empty()) ReadFile(gp, m_groups[g]);
  }
  // textures (main file or <name>T.mdl)
  const std::vector<uint8_t>* td = &d;
  std::vector<uint8_t> tfile;
  int nt = RdI(d, 180);
  if (nt == 0) { std::string tp = hlfs::ResolveCase(stem + "T.mdl"); if (!tp.empty() && ReadFile(tp, tfile)) td = &tfile; nt = RdI(*td, 180); }
  int ti = RdI(*td, 184);
  for (int i = 0; i < nt && i < 256; i++) {
    size_t o = ti + (size_t)i * 80; Texture t;
    t.name = RdS(*td, o, 64); t.flags = RdI(*td, o + 64); t.w = RdI(*td, o + 68); t.h = RdI(*td, o + 72);
    int idx = RdI(*td, o + 76);
    if (t.w > 0 && t.h > 0 && t.w <= 2048 && t.h <= 2048 && idx > 0 && (size_t)idx + (size_t)t.w * t.h + 768 <= td->size()) {
      const uint8_t* px = &(*td)[idx]; const uint8_t* pal = px + t.w * t.h;
      std::vector<uint8_t> rgba((size_t)t.w * t.h * 4);
      bool masked = (t.flags & 0x40) != 0;
      for (int p = 0; p < t.w * t.h; p++) {
        uint8_t c = px[p]; rgba[p * 4] = pal[c * 3]; rgba[p * 4 + 1] = pal[c * 3 + 1]; rgba[p * 4 + 2] = pal[c * 3 + 2];
        rgba[p * 4 + 3] = (masked && c == 255) ? 0 : 255;
      }
      Image im{ rgba.data(), t.w, t.h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
      t.tex = LoadTextureFromImage(im);
#ifndef CS_HEADLESS
      SetTextureFilter(t.tex, TEXTURE_FILTER_BILINEAR);
#endif
      SetTextureWrap(t.tex, TEXTURE_WRAP_REPEAT);
    }
    textures.push_back(t);
  }
  numSkinRef = RdI(*td, 192);
  int nf = RdI(*td, 196), ski = RdI(*td, 200);
  for (int i = 0; i < numSkinRef * nf && i < 4096; i++) skins.push_back(Rd<int16_t>(*td, ski + (size_t)i * 2));
  // body parts
  int nbp = RdI(d, 204), bpi = RdI(d, 208);
  for (int i = 0; i < nbp && i < 32; i++) {
    size_t o = bpi + (size_t)i * 76; BodyPart bp;
    bp.name = RdS(d, o, 64); int nm = RdI(d, o + 64); bp.base = std::max(1, RdI(d, o + 68)); int mi = RdI(d, o + 72);
    for (int m = 0; m < nm && m < 32; m++) {
      size_t mo = mi + (size_t)m * 112; SubModel sm;
      int nmesh = RdI(d, mo + 72), meshi = RdI(d, mo + 76), nv = RdI(d, mo + 80), vii = RdI(d, mo + 84), vi = RdI(d, mo + 88);
      int nn = RdI(d, mo + 92), nii = RdI(d, mo + 96), ni = RdI(d, mo + 100);
      if (nv < 0 || nv > 65536 || nn < 0 || nn > 65536) continue;
      for (int v = 0; v < nv; v++) { sm.verts.push_back(RdV(d, vi + (size_t)v * 12)); sm.vertBone.push_back(Rd<uint8_t>(d, vii + v)); }
      for (int v = 0; v < nn; v++) { sm.norms.push_back(RdV(d, ni + (size_t)v * 12)); sm.normBone.push_back(Rd<uint8_t>(d, nii + v)); }
      for (int me = 0; me < nmesh && me < 256; me++) {
        size_t meo = meshi + (size_t)me * 20; Mesh mesh; mesh.skinref = RdI(d, meo + 8);
        size_t p = RdI(d, meo + 4);
        auto cmd = [&]() { int16_t v = Rd<int16_t>(d, p); p += 2; return (int)v; };
        for (int guard = 0; guard < 100000; guard++) {
          int n = cmd(); if (n == 0 || p >= d.size()) break;
          bool fan = n < 0; n = std::abs(n);
          std::vector<Tri> run;
          for (int k = 0; k < n; k++) { Tri t; t.vert = cmd(); t.norm = cmd(); t.s = (float)cmd(); t.t = (float)cmd(); run.push_back(t); }
          for (int k = 2; k < n; k++) {
            if (fan) { mesh.tris.push_back(run[0]); mesh.tris.push_back(run[k - 1]); mesh.tris.push_back(run[k]); }
            else if (k & 1) { mesh.tris.push_back(run[k - 1]); mesh.tris.push_back(run[k - 2]); mesh.tris.push_back(run[k]); }
            else { mesh.tris.push_back(run[k - 2]); mesh.tris.push_back(run[k - 1]); mesh.tris.push_back(run[k]); }
          }
        }
        sm.meshes.push_back(std::move(mesh));
      }
      bp.models.push_back(std::move(sm));
    }
    bodyparts.push_back(std::move(bp));
  }
  // attachments
  int na = RdI(d, 212), ai = RdI(d, 216);
  for (int i = 0; i < na && i < 16; i++) { size_t o = ai + (size_t)i * 88; Attachment a; a.bone = RdI(d, o + 36); a.org = RdV(d, o + 40); attachments.push_back(a); }
  loaded = true;
  return true;
}

int CGoldSrcModel::FindSequence(const char* n) const {
  std::string l = hlfs::Lower(n);
  for (size_t i = 0; i < seqs.size(); i++) if (seqs[i].name == l) return (int)i;
  return -1;
}

void CGoldSrcModel::CalcBone(const uint8_t* animBase, int b, float frame, int numFrames, Vector& pos, Quat& q) const {
  const Bone& bone = bones[b];
  const uint8_t* panim = animBase + (size_t)b * 12;
  int f = (int)std::floor(frame); float s = frame - f;
  f = Clamp(f, 0, std::max(0, numFrames - 1));
  if (f >= numFrames - 1) s = 0;
  const uint8_t* end = nullptr;
  // the end of the buffer that holds animBase
  for (const auto* buf : { &m_data }) if (animBase >= buf->data() && animBase < buf->data() + buf->size()) end = buf->data() + buf->size();
  if (!end) for (const auto& g : m_groups) if (!g.empty() && animBase >= g.data() && animBase < g.data() + g.size()) end = g.data() + g.size();
  if (!end || panim + 12 > end) { pos = Vector(bone.value[0], bone.value[1], bone.value[2]); q = EulerQuat(bone.value + 3); return; }
  uint16_t off[6]; std::memcpy(off, panim, 12);
  float p[3], a1[3], a2[3];
  for (int j = 0; j < 3; j++) {
    float v = 0; if (off[j]) AnimValue(panim + off[j], end, f, s, v);
    p[j] = bone.value[j] + v * bone.scale[j];
  }
  // rotations: interpolate in quaternion space like GoldSrc
  for (int j = 0; j < 3; j++) {
    float v1 = 0, v2 = 0;
    if (off[j + 3]) { AnimValue(panim + off[j + 3], end, f, 0.f, v1); AnimValue(panim + off[j + 3], end, f, 1.f, v2); }
    a1[j] = bone.value[j + 3] + v1 * bone.scale[j + 3];
    a2[j] = bone.value[j + 3] + v2 * bone.scale[j + 3];
  }
  Quat q1 = EulerQuat(a1), q2 = EulerQuat(a2);
  q = s > 0.001f ? QuatSlerp(q1, q2, s) : q1;
  pos = Vector(p[0], p[1], p[2]);
}

void CGoldSrcModel::SetupBones(int seq, float frame, std::vector<matrix3x4_t>& out) const {
  out.resize(bones.size());
  const uint8_t* animBase = nullptr; int numFrames = 1;
  if (seq >= 0 && seq < (int)seqs.size()) {
    const Sequence& sq = seqs[seq]; numFrames = sq.numFrames;
    if (sq.group == 0) { if (sq.animOffset > 0 && (size_t)sq.animOffset < m_data.size()) animBase = m_data.data() + sq.animOffset; }
    else if (sq.group < (int)m_groups.size() && (size_t)sq.animOffset < m_groups[sq.group].size()) animBase = m_groups[sq.group].data() + sq.animOffset;
  }
  for (size_t b = 0; b < bones.size(); b++) {
    Vector pos; Quat q;
    if (animBase) CalcBone(animBase, (int)b, frame, numFrames, pos, q);
    else { pos = Vector(bones[b].value[0], bones[b].value[1], bones[b].value[2]); q = EulerQuat(bones[b].value + 3); }
    matrix3x4_t local; QuaternionMatrix(q, pos, Vector(1, 1, 1), local);
    int par = bones[b].parent;
    out[b] = (par >= 0 && par < (int)b) ? MatrixMul(out[par], local) : local;
  }
}

Vector CGoldSrcModel::AttachmentPos(int idx, const std::vector<matrix3x4_t>& bm, const matrix3x4_t& modelToWorld) const {
  if (idx < 0 || idx >= (int)attachments.size() || attachments[idx].bone >= (int)bm.size()) return modelToWorld.GetOrigin();
  return VectorTransform(VectorTransform(attachments[idx].org, bm[attachments[idx].bone]), modelToWorld);
}

void CGoldSrcModel::Draw(const std::vector<matrix3x4_t>& bm, const matrix3x4_t& modelToWorld, const StudioLight& light, int body, int skin) const {
  if (!loaded || bm.size() < bones.size()) return;
  thread_local std::vector<matrix3x4_t> xf;
  xf.resize(bones.size());
  for (size_t b = 0; b < bones.size(); b++) xf[b] = MatrixMul(modelToWorld, bm[b]);
  Vector L = light.dir.Normalized();
  rlDrawRenderBatchActive();
  rlDisableBackfaceCulling();
  thread_local std::vector<Vector> wv, wn;
  for (const BodyPart& bp : bodyparts) {
    if (bp.models.empty()) continue;
    int mi = (body / bp.base) % (int)bp.models.size();
    const SubModel& sm = bp.models[mi];
    wv.resize(sm.verts.size()); wn.resize(sm.norms.size());
    for (size_t v = 0; v < sm.verts.size(); v++) wv[v] = VectorTransform(sm.verts[v], xf[std::min<size_t>(sm.vertBone[v], xf.size() - 1)]);
    for (size_t v = 0; v < sm.norms.size(); v++) wn[v] = VectorRotate(sm.norms[v], xf[std::min<size_t>(sm.normBone[v], xf.size() - 1)]).Normalized();
    for (const Mesh& me : sm.meshes) {
      int texIdx = me.skinref;
      if (numSkinRef > 0) { size_t k = (size_t)skin * numSkinRef + me.skinref; if (k < skins.size()) texIdx = skins[k]; }
      const Texture* t = (texIdx >= 0 && texIdx < (int)textures.size()) ? &textures[texIdx] : nullptr;
      float iw = t && t->w ? 1.f / t->w : 1.f, ih = t && t->h ? 1.f / t->h : 1.f;
      bool additive = t && (t->flags & 0x20), chrome = t && (t->flags & 0x02), flat = t && (t->flags & 0x01);
      if (additive) { rlDrawRenderBatchActive(); rlSetBlendMode(RL_BLEND_ADDITIVE); }
      rlSetTexture(t && t->tex.id ? t->tex.id : rlGetTextureIdDefault());
      rlBegin(RL_TRIANGLES);
      for (const Tri& tr : me.tris) {
        if (tr.vert < 0 || tr.vert >= (int)wv.size()) continue;
        Vector n = (tr.norm >= 0 && tr.norm < (int)wn.size()) ? wn[tr.norm] : Vector(0, 0, 1);
        // GoldSrc studio lighting: ambient + lambert, softened
        float ndl = flat ? 0.6f : std::max(0.f, n.Dot(L));
        Vector li = light.ambient * (0.8f + 0.2f * n.z) + light.direct * ndl;
        Color c = { (unsigned char)Clamp(li.x * 255.f, 0.f, 255.f), (unsigned char)Clamp(li.y * 255.f, 0.f, 255.f), (unsigned char)Clamp(li.z * 255.f, 0.f, 255.f), 255 };
        float u = tr.s * iw, v = tr.t * ih;
        if (chrome) { u = 0.5f + n.y * 0.5f; v = 0.5f - n.z * 0.5f; }
        rlColor4ub(c.r, c.g, c.b, c.a); rlTexCoord2f(u, v);
        const Vector& p = wv[tr.vert]; rlVertex3f(p.x, p.y, p.z);
      }
      rlEnd();
      if (additive) { rlDrawRenderBatchActive(); rlSetBlendMode(RL_BLEND_ALPHA); }
    }
  }
  rlSetTexture(0);
  rlDrawRenderBatchActive();
  rlEnableBackfaceCulling();
}

// ------------------------------------------------------------------------------------------------
static std::map<std::string, std::unique_ptr<CGoldSrcModel>> s_models;
CGoldSrcModel* GoldSrc_GetModel(const std::string& rel) {
  std::string key = hlfs::Lower(rel);
  auto it = s_models.find(key);
  if (it != s_models.end()) return it->second.get();
  std::unique_ptr<CGoldSrcModel> m;
  std::string p = hlfs::Find(rel);
  if (!p.empty()) {
    m = std::make_unique<CGoldSrcModel>();
    if (!m->Load(p)) m.reset();
    else Msg("Loaded CS 1.6 model %s: %d bones, %d sequences, %d textures\n", rel.c_str(), (int)m->bones.size(), (int)m->seqs.size(), (int)m->textures.size());
  }
  CGoldSrcModel* r = m.get();
  s_models[key] = std::move(m);
  return r;
}
void GoldSrc_FlushModels() { for (auto& [k, m] : s_models) if (m) m->Unload(); s_models.clear(); }
