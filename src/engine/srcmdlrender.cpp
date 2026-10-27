// srcmdlrender.cpp - CPU skinned drawing of Source studio models (CS:S player models, view models, weapons)
// Same lighting / flags (chams, glow, wireframe, no-depth) as StudioRender_DrawModel so the cheat visuals work on them.
#include "srcmdl.hpp"
#include "studio.hpp"
#include "rlgl.h"
#include <cmath>
#include <algorithm>

void SrcMdl_Draw(const CSrcModel& m, const std::vector<matrix3x4_t>& bones, const matrix3x4_t& modelToWorld, const StudioDrawParams& p, int skin, const std::vector<uint8_t>* boneMask, bool strictMask, const std::vector<uint8_t>* skipBones) {
  if (!m.loaded || m.meshes.empty()) return;
  thread_local std::vector<matrix3x4_t> sk;
  m.SkinMatrices(bones, modelToWorld, sk);
  if (sk.empty()) sk.push_back(modelToWorld);
  const Vector L = p.light.dir.Normalized();
  const Vector amb = p.light.ambient, dir = p.light.direct;
  const Color co = p.colorOverride;
  const bool flat = p.flags & STUDIO_FLAT_COLOR, tint = p.flags & STUDIO_TINT, glow = p.flags & STUDIO_GLOW, wire = p.flags & STUDIO_WIREFRAME;
  rlDrawRenderBatchActive();
  if (p.flags & STUDIO_NO_DEPTH) rlDisableDepthTest();
  if (p.flags & STUDIO_NO_CULL) rlDisableBackfaceCulling();
  // mirrored transform (right-handed view models): the winding flips, so cull the other side
  const matrix3x4_t& X = modelToWorld;
  const float det = X[0][0] * (X[1][1] * X[2][2] - X[1][2] * X[2][1]) - X[0][1] * (X[1][0] * X[2][2] - X[1][2] * X[2][0]) + X[0][2] * (X[1][0] * X[2][1] - X[1][1] * X[2][0]);
  const bool mirrored = det < 0.f;
  if (mirrored) rlSetCullFace(RL_CULL_FACE_FRONT);
  thread_local std::vector<Vector> wp; thread_local std::vector<Color> wc; thread_local std::vector<uint8_t> vok, sok;
  // opaque meshes first, translucent ones after
  for (int pass = 0; pass < 2; pass++) {
    for (const SrcMdlMesh& me : m.meshes) {
      if (m.MeshHidden(me, skin)) continue;
      bool translucent = false, alphatest = false;
      Texture2D tex = m.MeshTexture(me, skin, &translucent, &alphatest);
      if ((pass == 1) != translucent) continue;
      const size_t vc = me.pos.size() / 3;
      wp.resize(vc); wc.resize(vc);
      // vertices weighted only to the skipped bones (the bind-pose hands some ARC9 view models carry) are dropped: a
      // triangle that has no other vertex is not drawn at all
      if (skipBones) {
        sok.assign(vc, 1);
        for (size_t v = 0; v < vc; v++) {
          bool onlySkipped = true, any = false;
          for (int k = 0; k < 3; k++) {
            int b = me.bone[v * 3 + k]; if (me.weight[v * 3 + k] <= 0) continue;
            any = true;
            if (b < 0 || b >= (int)skipBones->size() || !(*skipBones)[b]) { onlySkipped = false; break; }
          }
          if (any && onlySkipped) sok[v] = 0;
        }
      }
      if (boneMask) {
        // a vertex belongs to the masked part if any of its influences is in the mask (arm vertices blend into the
        // shoulder, so requiring all three would cut the whole arm away)
        vok.assign(vc, 0);
        for (size_t v = 0; v < vc; v++)
          for (int k = 0; k < 3; k++) { int b = me.bone[v * 3 + k]; if (me.weight[v * 3 + k] > 0 && b >= 0 && b < (int)boneMask->size() && (*boneMask)[b]) { vok[v] = 1; break; } }
        if (strictMask)
          for (size_t v = 0; v < vc; v++)
            for (int k = 0; k < 3; k++) { int b = me.bone[v * 3 + k]; if (me.weight[v * 3 + k] > 0 && (b < 0 || b >= (int)boneMask->size() || !(*boneMask)[b])) { vok[v] = 0; break; } }
      }
      for (size_t v = 0; v < vc; v++) {
        Vector P(me.pos[v * 3], me.pos[v * 3 + 1], me.pos[v * 3 + 2]), N(me.normal[v * 3], me.normal[v * 3 + 1], me.normal[v * 3 + 2]);
        Vector pos, nrm; float wsum = 0;
        for (int k = 0; k < 3; k++) {
          float w = me.weight[v * 3 + k]; if (w <= 0) continue;
          const matrix3x4_t& b = sk[std::min<size_t>(me.bone[v * 3 + k], sk.size() - 1)];
          pos += VectorTransform(P, b) * w; nrm += VectorRotate(N, b) * w; wsum += w;
        }
        if (wsum <= 0) { pos = VectorTransform(P, sk[0]); nrm = VectorRotate(N, sk[0]); }
        else if (std::fabs(wsum - 1.f) > 1e-3f) { pos = pos * (1.f / wsum); }
        nrm.NormalizeInPlace();
        wp[v] = pos;
        Color c;
        if (flat) {
          float shade = 0.75f + 0.25f * std::max(0.f, nrm.Dot(L));
          c = { (uint8_t)(co.r * shade), (uint8_t)(co.g * shade), (uint8_t)(co.b * shade), co.a };
        } else {
          float ndl = std::max(0.f, nrm.Dot(L));
          Vector li = amb * (0.85f + 0.15f * nrm.z) + dir * ndl;
          Vector col = li * 255.f;
          if (tint) col = col * Vector(co.r / 255.f, co.g / 255.f, co.b / 255.f);
          if (glow) { Vector vd = (p.eye - pos).Normalized(); float rim = 1.f - std::fabs(vd.Dot(nrm)); col += Vector(co.r, co.g, co.b) * rim * rim * 1.2f; }
          c = { (uint8_t)Clamp(col.x, 0.f, 255.f), (uint8_t)Clamp(col.y, 0.f, 255.f), (uint8_t)Clamp(col.z, 0.f, 255.f), tint ? co.a : (uint8_t)255 };
        }
        wc[v] = c;
      }
      const size_t tc = me.idx.size() / 3;
      if (wire) {
        rlSetTexture(rlGetTextureIdDefault());
        for (size_t t0 = 0; t0 < tc; t0 += 256) {
          size_t t1 = std::min(tc, t0 + 256);
          rlCheckRenderBatchLimit((int)(t1 - t0) * 6);
          rlBegin(RL_LINES);
          for (size_t t = t0; t < t1; t++)
            for (int q = 0; q < 3; q++) {
              uint16_t a = me.idx[t * 3 + q], b = me.idx[t * 3 + (q + 1) % 3];
              rlColor4ub(wc[a].r, wc[a].g, wc[a].b, wc[a].a); rlVertex3f(wp[a].x, wp[a].y, wp[a].z);
              rlColor4ub(wc[b].r, wc[b].g, wc[b].b, wc[b].a); rlVertex3f(wp[b].x, wp[b].y, wp[b].z);
            }
          rlEnd();
        }
        continue;
      }
      // opaque materials: the texture alpha is a mask for phong / envmap ($basemapalphaphongmask, $basealphaenvmapmask -
      // CS:GO models) and must not make the mesh see-through
      const bool noBlend = !translucent && !alphatest && !tint && !glow;
      if (noBlend) { rlDrawRenderBatchActive(); rlDisableColorBlend(); }
      rlSetTexture(!flat && tex.id ? tex.id : rlGetTextureIdDefault());
      for (size_t t0 = 0; t0 < tc; t0 += 512) {
        size_t t1 = std::min(tc, t0 + 512);
        rlCheckRenderBatchLimit((int)(t1 - t0) * 3);
        rlBegin(RL_TRIANGLES);
        for (size_t t = t0; t < t1; t++) {
          if (boneMask && !(vok[me.idx[t * 3]] && vok[me.idx[t * 3 + 1]] && vok[me.idx[t * 3 + 2]])) continue;
          if (skipBones && !(sok[me.idx[t * 3]] || sok[me.idx[t * 3 + 1]] || sok[me.idx[t * 3 + 2]])) continue;
          for (int q = 0; q < 3; q++) {
            uint16_t vi = me.idx[t * 3 + q]; const Vector& v = wp[vi]; const Color& c = wc[vi];
            rlColor4ub(c.r, c.g, c.b, c.a); rlTexCoord2f(me.uv[vi * 2], me.uv[vi * 2 + 1]); rlVertex3f(v.x, v.y, v.z);
          }
        }
        rlEnd();
      }
      if (noBlend) { rlDrawRenderBatchActive(); rlEnableColorBlend(); }
    }
  }
  rlSetTexture(0);
  rlDrawRenderBatchActive();
  if (p.flags & (STUDIO_NO_DEPTH | STUDIO_NO_CULL)) { rlEnableDepthTest(); rlEnableBackfaceCulling(); }
  if (mirrored) rlSetCullFace(RL_CULL_FACE_BACK);
}
