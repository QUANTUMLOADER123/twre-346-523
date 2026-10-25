#include "studio.hpp"
#include "convar.hpp"
#include "rlgl.h"
#include "external/cgltf.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <utility>

ConVar r_drawmodels("r_drawmodels", "1", FCVAR_CHEAT, "Draw studio models");

matrix3x4_t MatrixMul(const matrix3x4_t& a, const matrix3x4_t& b) { matrix3x4_t r; ConcatTransforms(a, b, r); return r; }
matrix3x4_t RigidInvert(const matrix3x4_t& m) { matrix3x4_t r; MatrixInvert(m, r); return r; }
matrix3x4_t AxisAngleMatrix(const Vector& axis, float deg) { matrix3x4_t m; QuaternionMatrix(AxisAngleQuat(axis, deg), Vector(), Vector(1, 1, 1), m); return m; }
matrix3x4_t RotationBetween(const Vector& f, const Vector& t) {
  Vector a = f.Normalized(), b = t.Normalized();
  float d = Clamp(a.Dot(b), -1.f, 1.f);
  Vector ax = a.Cross(b);
  if (ax.Length() < 1e-6f) {
    if (d > 0) return matrix3x4_t();
    Vector perp = std::fabs(a.x) < 0.9f ? a.Cross(Vector(1, 0, 0)) : a.Cross(Vector(0, 1, 0));
    return AxisAngleMatrix(perp, 180.f);
  }
  return AxisAngleMatrix(ax, RAD2DEGF(std::acos(d)));
}
static inline uint8_t LinToSrgb(float c, float k) { c = std::pow(Clamp(c, 0.f, 1.f), 1.f / 2.2f) * k; return (uint8_t)Clamp(c * 255.f, 0.f, 255.f); }
static matrix3x4_t FromGltf(const float* m) { // column-major 4x4
  matrix3x4_t r;
  for (int row = 0; row < 3; row++) for (int col = 0; col < 4; col++) r[row][col] = m[col * 4 + row];
  return r;
}
static matrix3x4_t Normalized3x4(const matrix3x4_t& m) {
  matrix3x4_t r = m;
  for (int c = 0; c < 3; c++) { Vector v = m.Column(c).Normalized(); r.SetColumn(c, v); }
  // re-orthogonalize (gram-schmidt) to stay rigid
  Vector x = r.Column(0), y = r.Column(1); y = (y - x * x.Dot(y)).Normalized(); Vector z = x.Cross(y);
  r.SetColumn(1, y); r.SetColumn(2, z);
  return r;
}

bool CStudioModel::Load(const std::string& path, int orient, float targetSize, float brighten) {
  name = path;
  cgltf_options opt{}; cgltf_data* data = nullptr;
  if (cgltf_parse_file(&opt, path.c_str(), &data) != cgltf_result_success) { Warning("Model %s: parse failed\n", path.c_str()); return false; }
  if (cgltf_load_buffers(&opt, data, path.c_str()) != cgltf_result_success) { Warning("Model %s: buffers failed\n", path.c_str()); cgltf_free(data); return false; }
  matrix3x4_t R0; // gltf (Y up) -> Source model space (Z up, X forward)
  std::memset(R0.m, 0, sizeof(R0.m));
  if (orient == STUDIO_ORIENT_CHARACTER) { R0[0][2] = 1; R0[1][0] = 1; R0[2][1] = 1; } // x=gz y=gx z=gy
  else { R0[0][0] = 1; R0[1][2] = -1; R0[2][1] = 1; }                                // x=gx y=-gz z=gy
  size_t nn = data->nodes_count;
  nodes.resize(nn);
  std::vector<matrix3x4_t> graw(nn);
  for (size_t i = 0; i < nn; i++) {
    cgltf_node* n = &data->nodes[i];
    nodes[i].name = n->name ? n->name : ("node" + std::to_string(i));
    nodes[i].parent = n->parent ? (int)(n->parent - data->nodes) : -1;
    float w[16]; cgltf_node_transform_world(n, w);
    graw[i] = MatrixMul(R0, FromGltf(w));
    m_nameToNode[nodes[i].name] = (int)i;
  }
  // raw vertices
  struct RawPrim { mstudioprim_t p; };
  std::vector<mstudioprim_t> raw;
  for (size_t ni = 0; ni < nn; ni++) {
    cgltf_node* node = &data->nodes[ni];
    if (!node->mesh) continue;
    cgltf_skin* skin = node->skin;
    std::vector<matrix3x4_t> jointMats;
    std::vector<int> jointNode;
    if (skin) {
      for (size_t j = 0; j < skin->joints_count; j++) {
        float ibm[16]; cgltf_accessor_read_float(skin->inverse_bind_matrices, j, ibm, 16);
        int jn = (int)(skin->joints[j] - data->nodes);
        jointNode.push_back(jn);
        jointMats.push_back(MatrixMul(graw[jn], FromGltf(ibm)));
      }
    }
    for (size_t pi = 0; pi < node->mesh->primitives_count; pi++) {
      cgltf_primitive* pr = &node->mesh->primitives[pi];
      if (pr->type != cgltf_primitive_type_triangles) continue;
      mstudioprim_t P;
      cgltf_accessor *apos = nullptr, *anrm = nullptr, *ajnt = nullptr, *awgt = nullptr;
      for (size_t a = 0; a < pr->attributes_count; a++) {
        auto& at = pr->attributes[a];
        if (at.type == cgltf_attribute_type_position) apos = at.data;
        else if (at.type == cgltf_attribute_type_normal) anrm = at.data;
        else if (at.type == cgltf_attribute_type_joints && at.index == 0) ajnt = at.data;
        else if (at.type == cgltf_attribute_type_weights && at.index == 0) awgt = at.data;
      }
      if (!apos) continue;
      size_t vc = apos->count;
      P.skinned = skin && ajnt && awgt;
      P.node = (int)ni;
      P.pos.resize(vc); P.nrm.resize(vc);
      if (P.skinned) { P.joints.resize(vc); P.weights.resize(vc); }
      for (size_t v = 0; v < vc; v++) {
        float p[3] = { 0, 0, 0 }, n[3] = { 0, 0, 1 };
        cgltf_accessor_read_float(apos, v, p, 3);
        if (anrm) cgltf_accessor_read_float(anrm, v, n, 3);
        Vector vp(p[0], p[1], p[2]), vn(n[0], n[1], n[2]);
        if (P.skinned) {
          cgltf_uint j[4] = { 0, 0, 0, 0 }; float w[4] = { 1, 0, 0, 0 };
          cgltf_accessor_read_uint(ajnt, v, j, 4); cgltf_accessor_read_float(awgt, v, w, 4);
          float ws = w[0] + w[1] + w[2] + w[3]; if (ws <= 0) { w[0] = 1; ws = 1; }
          Vector op, on;
          for (int k = 0; k < 4; k++) {
            w[k] /= ws;
            int jj = (int)std::min<cgltf_uint>(j[k], (cgltf_uint)jointMats.size() - 1);
            P.joints[v][k] = (uint16_t)jointNode[jj]; P.weights[v][k] = w[k];
            if (w[k] > 0) { op += VectorTransform(vp, jointMats[jj]) * w[k]; on += VectorRotate(vn, jointMats[jj]) * w[k]; }
          }
          P.pos[v] = op; P.nrm[v] = on.Normalized();
        } else {
          P.pos[v] = VectorTransform(vp, graw[ni]); P.nrm[v] = VectorRotate(vn, graw[ni]).Normalized();
        }
      }
      if (pr->indices) { P.indices.resize(pr->indices->count); for (size_t k = 0; k < pr->indices->count; k++) P.indices[k] = (uint32_t)cgltf_accessor_read_index(pr->indices, k); }
      else { P.indices.resize(vc); for (size_t k = 0; k < vc; k++) P.indices[k] = (uint32_t)k; }
      float col[4] = { 0.8f, 0.8f, 0.8f, 1 };
      if (pr->material) {
        P.matName = pr->material->name ? pr->material->name : "";
        if (pr->material->has_pbr_metallic_roughness) std::memcpy(col, pr->material->pbr_metallic_roughness.base_color_factor, sizeof(col));
      }
      float k = brighten;
      P.color = { LinToSrgb(col[0], k), LinToSrgb(col[1], k), LinToSrgb(col[2], k), 255 };
      raw.push_back(std::move(P));
    }
  }
  cgltf_free(data);
  if (raw.empty()) { Warning("Model %s has no meshes\n", path.c_str()); return false; }
  // bounds & normalization
  Vector mn(1e9f, 1e9f, 1e9f), mx(-1e9f, -1e9f, -1e9f);
  for (auto& p : raw) for (auto& v : p.pos) { mn = VecMin(mn, v); mx = VecMax(mx, v); }
  Vector ext = mx - mn;
  float s = 1.f; Vector off;
  if (orient == STUDIO_ORIENT_CHARACTER) { s = targetSize / std::max(1e-6f, ext.z); off = Vector(-(mn.x + mx.x) * 0.5f, -(mn.y + mx.y) * 0.5f, -mn.z); }
  else { s = targetSize / std::max(1e-6f, ext.x); off = (mn + mx) * -0.5f; }
  auto C = [&](const Vector& v) { return (v + off) * s; };
  mins = Vector(1e9f, 1e9f, 1e9f); maxs = Vector(-1e9f, -1e9f, -1e9f);
  numVerts = 0; numTris = 0;
  for (auto& p : raw) { for (auto& v : p.pos) { v = C(v); mins = VecMin(mins, v); maxs = VecMax(maxs, v); } numVerts += (int)p.pos.size(); numTris += (int)p.indices.size() / 3; if (p.skinned) skinned = true; }
  for (size_t i = 0; i < nn; i++) {
    matrix3x4_t f = Normalized3x4(graw[i]);
    f.SetOrigin(C(graw[i].GetOrigin()));
    nodes[i].bindToModel = f;
  }
  for (size_t i = 0; i < nn; i++) {
    int par = nodes[i].parent;
    nodes[i].relToParent = par >= 0 ? MatrixMul(RigidInvert(nodes[par].bindToModel), nodes[i].bindToModel) : nodes[i].bindToModel;
  }
  // topological order
  order.clear(); std::vector<char> done(nn, 0);
  while (order.size() < nn) {
    size_t before = order.size();
    for (size_t i = 0; i < nn; i++) if (!done[i] && (nodes[i].parent < 0 || done[nodes[i].parent])) { done[i] = 1; order.push_back((int)i); }
    if (order.size() == before) break;
  }
  // rigid prims: positions are already in model space; store node so they follow posed nodes
  // arm triangles (dominant joint is a lower arm / hand / finger bone)
  auto isArm = [&](int node) {
    const std::string& n = nodes[node].name;
    return n.rfind("LowerArm", 0) == 0 || n.rfind("Hand", 0) == 0 || n.rfind("Index", 0) == 0 || n.rfind("Middle", 0) == 0 || n.rfind("Ring", 0) == 0 || n.rfind("Pinky", 0) == 0 || n.rfind("Thumb", 0) == 0;
  };
  for (auto& p : raw) {
    if (!p.skinned) continue;
    for (size_t t = 0; t + 2 < p.indices.size(); t += 3) {
      bool all = true;
      for (int q = 0; q < 3; q++) {
        uint32_t vi = p.indices[t + q]; int best = 0;
        for (int k = 1; k < 4; k++) if (p.weights[vi][k] > p.weights[vi][best]) best = k;
        if (!isArm(p.joints[vi][best])) { all = false; break; }
      }
      if (all) { p.armIndices.push_back(p.indices[t]); p.armIndices.push_back(p.indices[t + 1]); p.armIndices.push_back(p.indices[t + 2]); }
    }
  }
  prims = std::move(raw);
  loaded = true;
  DevMsg("Loaded model %s: %d verts %d tris %d nodes, size %.1f %.1f %.1f\n", path.c_str(), numVerts, numTris, (int)nn, maxs.x - mins.x, maxs.y - mins.y, maxs.z - mins.z);
  return true;
}

void CStudioModel::BuildProcedural(const std::string& n) {
  name = n; loaded = true; skinned = false;
  mins = Vector(1e9f, 1e9f, 1e9f); maxs = Vector(-1e9f, -1e9f, -1e9f);
}
static void PushQuad(mstudioprim_t& p, const Vector& a, const Vector& b, const Vector& c, const Vector& d) {
  Vector n = (b - a).Cross(c - a).Normalized();
  uint32_t base = (uint32_t)p.pos.size();
  p.pos.insert(p.pos.end(), { a, b, c, d }); p.nrm.insert(p.nrm.end(), { n, n, n, n });
  p.indices.insert(p.indices.end(), { base, base + 1, base + 2, base, base + 2, base + 3 });
}
void CStudioModel::AddBox(const Vector& a, const Vector& b, Color c) {
  mstudioprim_t p; p.color = c;
  Vector v[8] = { { a.x, a.y, a.z }, { b.x, a.y, a.z }, { b.x, b.y, a.z }, { a.x, b.y, a.z }, { a.x, a.y, b.z }, { b.x, a.y, b.z }, { b.x, b.y, b.z }, { a.x, b.y, b.z } };
  PushQuad(p, v[0], v[3], v[2], v[1]); // bottom
  PushQuad(p, v[4], v[5], v[6], v[7]); // top
  PushQuad(p, v[0], v[1], v[5], v[4]); // -y
  PushQuad(p, v[2], v[3], v[7], v[6]); // +y
  PushQuad(p, v[1], v[2], v[6], v[5]); // +x
  PushQuad(p, v[3], v[0], v[4], v[7]); // -x
  for (auto& q : p.pos) { mins = VecMin(mins, q); maxs = VecMax(maxs, q); }
  numVerts += 24; numTris += 12;
  prims.push_back(std::move(p));
}
// flat quad a,b,c,d (CCW seen from +side) extruded by thick along its normal (both sides)
void CStudioModel::AddWedge(const Vector& a, const Vector& b, const Vector& c, const Vector& d, float thick, Color col) {
  mstudioprim_t p; p.color = col;
  Vector n = (b - a).Cross(c - a).Normalized() * (thick * 0.5f);
  Vector A0 = a - n, B0 = b - n, C0 = c - n, D0 = d - n, A1 = a + n, B1 = b + n, C1 = c + n, D1 = d + n;
  PushQuad(p, A1, B1, C1, D1); PushQuad(p, D0, C0, B0, A0);
  PushQuad(p, A0, B0, B1, A1); PushQuad(p, B0, C0, C1, B1); PushQuad(p, C0, D0, D1, C1); PushQuad(p, D0, A0, A1, D1);
  for (auto& q : p.pos) { mins = VecMin(mins, q); maxs = VecMax(maxs, q); }
  numVerts += 24; numTris += 12;
  prims.push_back(std::move(p));
}

void CStudioModel::BindPose(CStudioPose& out) const {
  out.nodes.resize(nodes.size());
  for (size_t i = 0; i < nodes.size(); i++) out.nodes[i] = nodes[i].bindToModel;
}
void CStudioModel::BuildPose(const std::vector<BoneDelta>& d, CStudioPose& out) const {
  out.nodes.resize(nodes.size());
  for (int i : order) {
    const mstudionode_t& n = nodes[i];
    matrix3x4_t m = n.parent >= 0 ? MatrixMul(out.nodes[n.parent], n.relToParent) : n.bindToModel;
    if (i < (int)d.size() && d[i].stretch != 1.f && n.parent >= 0) { Vector po = out.nodes[n.parent].GetOrigin(); m.SetOrigin(po + (m.GetOrigin() - po) * d[i].stretch); }
    if (i < (int)d.size() && d[i].active) {
      Vector o = m.GetOrigin();
      m = MatrixMul(d[i].rot, m);
      m.SetOrigin(o + d[i].translate);
    }
    out.nodes[i] = m;
  }
}
void CStudioModel::SolveTwoBoneIK(std::vector<BoneDelta>& d, int up, int lo, int end, const Vector& T, const Vector& pole) const {
  if (up < 0 || lo < 0 || end < 0) return;
  if (d.size() < nodes.size()) d.resize(nodes.size());
  CStudioPose pose; BuildPose(d, pose);
  Vector A = pose.nodes[up].GetOrigin(), B = pose.nodes[lo].GetOrigin(), Cc = pose.nodes[end].GetOrigin();
  float l1 = nodes[lo].bindPos().DistTo(nodes[up].bindPos()) * d[lo].stretch, l2 = nodes[end].bindPos().DistTo(nodes[lo].bindPos()) * d[end].stretch;
  Vector toT = T - A; float dist = Clamp(toT.Length(), std::fabs(l1 - l2) + 0.01f, (l1 + l2) * 0.999f);
  Vector dir = toT.Normalized();
  float a = (l1 * l1 - l2 * l2 + dist * dist) / (2 * dist), h = std::sqrt(std::max(0.f, l1 * l1 - a * a));
  Vector perp = pole - dir * pole.Dot(dir); if (perp.Length() < 1e-4f) perp = Vector(0, 0, -1); perp.NormalizeInPlace();
  Vector E = A + dir * a + perp * h;
  matrix3x4_t rUp = RotationBetween(B - A, E - A);
  Vector C1 = A + VectorRotate(Cc - A, rUp);
  Vector Tc = A + dir * dist;
  matrix3x4_t rLo = RotationBetween(C1 - E, Tc - E);
  auto compose = [&](int i, const matrix3x4_t& r) { if (d[i].active) d[i].rot = MatrixMul(r, d[i].rot); else { d[i].active = true; d[i].rot = r; d[i].translate = Vector(); } };
  compose(up, rUp); compose(lo, rLo);
}

// ------------------------------------------------------------------------------------------------
void StudioRender_DrawModel(const CStudioModel& mdl, const StudioDrawParams& p) {
  if (!mdl.loaded || !r_drawmodels.GetBool()) return;
  thread_local std::vector<matrix3x4_t> skin;
  size_t nn = mdl.nodes.size();
  skin.resize(std::max<size_t>(nn, 1));
  for (size_t i = 0; i < nn; i++) {
    if (p.pose && i < p.pose->nodes.size()) skin[i] = MatrixMul(p.modelToWorld, MatrixMul(p.pose->nodes[i], RigidInvert(mdl.nodes[i].bindToModel)));
    else skin[i] = p.modelToWorld;
  }
  if (p.flags & STUDIO_NO_DEPTH) { rlDrawRenderBatchActive(); rlDisableDepthTest(); }
  if (p.flags & STUDIO_NO_CULL) { rlDrawRenderBatchActive(); rlDisableBackfaceCulling(); }
  Vector L = p.light.dir.Normalized();
  Vector amb = p.light.ambient, dir = p.light.direct;
  const Color co = p.colorOverride;
  thread_local std::vector<Vector> wp; thread_local std::vector<Color> wc;
  rlSetTexture(rlGetTextureIdDefault());
  for (const auto& pr : mdl.prims) {
    const std::vector<uint32_t>& idx = (p.flags & STUDIO_ARMS_ONLY) ? pr.armIndices : pr.indices;
    if (idx.empty()) continue;
    size_t vc = pr.pos.size(); wp.resize(vc); wc.resize(vc);
    for (size_t v = 0; v < vc; v++) {
      Vector pos, nrm;
      if (pr.skinned) {
        for (int k = 0; k < 4; k++) { float w = pr.weights[v][k]; if (w <= 0) continue; const matrix3x4_t& m = skin[pr.joints[v][k]]; pos += VectorTransform(pr.pos[v], m) * w; nrm += VectorRotate(pr.nrm[v], m) * w; }
      } else {
        const matrix3x4_t& m = (pr.node >= 0 && p.pose) ? skin[pr.node] : p.modelToWorld;
        pos = VectorTransform(pr.pos[v], m); nrm = VectorRotate(pr.nrm[v], m);
      }
      nrm.NormalizeInPlace();
      wp[v] = pos;
      Color c;
      if (p.flags & STUDIO_FLAT_COLOR) {
        float shade = 0.75f + 0.25f * std::max(0.f, nrm.Dot(L));
        c = { (uint8_t)(co.r * shade), (uint8_t)(co.g * shade), (uint8_t)(co.b * shade), co.a };
      } else {
        float ndl = std::max(0.f, nrm.Dot(L));
        float hemi = 0.85f + 0.15f * nrm.z;
        Vector li = amb * hemi + dir * ndl;
        Vector base(pr.color.r, pr.color.g, pr.color.b);
        if (p.flags & STUDIO_TINT) base = base * Vector(co.r / 255.f, co.g / 255.f, co.b / 255.f);
        Vector col = base * li;
        if (p.flags & STUDIO_GLOW) { Vector vd = (p.eye - pos).Normalized(); float rim = 1.f - std::fabs(vd.Dot(nrm)); rim = rim * rim; col += Vector(co.r, co.g, co.b) * rim * 1.2f; }
        c = { (uint8_t)Clamp(col.x, 0.f, 255.f), (uint8_t)Clamp(col.y, 0.f, 255.f), (uint8_t)Clamp(col.z, 0.f, 255.f), (p.flags & STUDIO_TINT) ? co.a : (uint8_t)255 };
      }
      wc[v] = c;
    }
    size_t tc = idx.size() / 3;
    if (p.flags & STUDIO_WIREFRAME) {
      for (size_t t0 = 0; t0 < tc; t0 += 256) {
        size_t t1 = std::min(tc, t0 + 256);
        rlCheckRenderBatchLimit((int)(t1 - t0) * 6);
        rlBegin(RL_LINES);
        for (size_t t = t0; t < t1; t++)
          for (int q = 0; q < 3; q++) {
            uint32_t a = idx[t * 3 + q], b = idx[t * 3 + (q + 1) % 3];
            rlColor4ub(wc[a].r, wc[a].g, wc[a].b, wc[a].a); rlVertex3f(wp[a].x, wp[a].y, wp[a].z);
            rlColor4ub(wc[b].r, wc[b].g, wc[b].b, wc[b].a); rlVertex3f(wp[b].x, wp[b].y, wp[b].z);
          }
        rlEnd();
      }
      continue;
    }
    for (size_t t0 = 0; t0 < tc; t0 += 512) {
      size_t t1 = std::min(tc, t0 + 512);
      rlCheckRenderBatchLimit((int)(t1 - t0) * 3);
      rlBegin(RL_TRIANGLES);
      for (size_t t = t0; t < t1; t++) {
        for (int q = 0; q < 3; q++) {
          uint32_t vi = idx[t * 3 + q]; const Vector& v = wp[vi]; const Color& c = wc[vi];
          rlColor4ub(c.r, c.g, c.b, c.a); rlTexCoord2f(0.5f, 0.5f); rlVertex3f(v.x, v.y, v.z);
        }
      }
      rlEnd();
    }
  }
  if (p.flags & (STUDIO_NO_DEPTH | STUDIO_NO_CULL)) { rlDrawRenderBatchActive(); rlEnableDepthTest(); rlEnableBackfaceCulling(); }
}

CStudioModel* CModelCache::Get(const std::string& path, int orient, float size, float brighten) {
  auto it = m_models.find(path); if (it != m_models.end()) return it->second;
  CStudioModel* m = new CStudioModel();
  if (!m->Load(path, orient, size, brighten)) { m->loaded = false; }
  m_models[path] = m;
  return m;
}
CStudioModel* CModelCache::GetProcedural(const std::string& n) {
  auto it = m_models.find("*" + n); if (it != m_models.end()) return it->second;
  CStudioModel* m = new CStudioModel(); m->BuildProcedural(n);
  m_models["*" + n] = m;
  return m;
}
