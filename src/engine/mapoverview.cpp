// mapoverview.cpp - CS:GO / CS:S overview radars straight out of the map file.
#include "mapoverview.hpp"
#include "dds.hpp"
#include "keyvalues.hpp"
#include "srcfs.hpp"
#include "srcbsp.hpp"
#include "srcmaterial.hpp"
#include "enginetrace.hpp"
#include "convar.hpp"
#include "raylib.h"
#include <cstring>
#include <cmath>
#include <algorithm>

namespace mapoverview {
namespace {
// every place a map's inner file can be: the mounted pakfile (map loaded) or the map's bsp zip on disk
bool ReadInner(const std::string& map, const std::string& entry, std::vector<uint8_t>& out) {
  if (srcfs::Read(entry, out)) return true;
  std::string bsp = srcfs::LoosePath("maps/" + map + ".bsp");
  if (bsp.empty()) return false;
  return srcfs::ReadZipEntryFile(bsp, entry, out);
}

bool DecodeData(const std::vector<uint8_t>& d, std::vector<uint8_t>& rgba, int& w, int& h) {
  if (d.size() >= 128 && d[0] == 'D' && d[1] == 'D' && d[2] == 'S' && d[3] == ' ') return dds::Decode(d.data(), d.size(), rgba, w, h, true);
  // fallback: an uncompressed BMP/tga radar shipped by a few custom maps (raylib handles those)
  return false;
}
} // namespace

bool Load(const std::string& map, std::vector<uint8_t>& rgba, int& w, int& h, Info& info) {
  info = Info();
  std::vector<uint8_t> d;
  if (!ReadInner(map, "resource/overviews/" + map + "_radar.dds", d) || !DecodeData(d, rgba, w, h)) return false;
  std::vector<uint8_t> txt;
  if (ReadInner(map, "resource/overviews/" + map + ".txt", txt) && !txt.empty()) {
    auto kv = KeyValues::Parse((const char*)txt.data(), txt.size());
    const KeyValues* root = kv && !kv->children.empty() ? kv->children[0].get() : nullptr;
    if (root) {
      info.posX = root->GetFloat("pos_x"); info.posY = root->GetFloat("pos_y"); info.scale = root->GetFloat("scale");
      info.rotated = root->GetInt("rotate") != 0;
      info.scriptFound = info.scale > 0;
    }
  }
  return true;
}

bool Has(const std::string& map) {
  std::vector<uint8_t> d;
  return ReadInner(map, "resource/overviews/" + map + "_radar.dds", d);
}

// ------------------------------------------------------------------------------------------------
// top-down render of the level itself (the map picker preview)
namespace {
// a muted colour per surface type: concrete greys, wooden floors brown, sand, grass, metal...
void SurfaceColor(const std::string& matName, float& r, float& g, float& b) {
  const int s = SrcMat_SurfaceFromProp(std::string(), matName);
  switch (s) {
    case SURFACE_WOOD: case SURFACE_WOOD_CRATE: r = 0.62f; g = 0.45f; b = 0.28f; break;
    case SURFACE_SAND: case SURFACE_SANDSTONE: case SURFACE_DIRT: r = 0.72f; g = 0.64f; b = 0.46f; break;
    case SURFACE_METAL: case SURFACE_METALGRATE: r = 0.52f; g = 0.55f; b = 0.60f; break;
    case SURFACE_GLASS: r = 0.55f; g = 0.72f; b = 0.78f; break;
    case SURFACE_TILE: r = 0.68f; g = 0.70f; b = 0.70f; break;
    case SURFACE_BRICK: r = 0.60f; g = 0.40f; b = 0.34f; break;
    default: {
      // everything else (concrete, plaster, stone) gets a light-grey shade that differs a little per material, so
      // walls and floors of a blocky map still read as separate surfaces
      uint32_t h = 2166136261u;
      for (char c : matName) { h ^= (uint8_t)c; h *= 16777619u; }
      const float v = 0.58f + (float)(h % 100) / 100.f * 0.22f;
      r = v; g = v * 0.99f; b = v * 0.96f;
      break;
    }
  }
}
} // namespace

bool RenderTopDown(const std::string& map, std::vector<uint8_t>& rgba, int& w, int& h, int size) {
  CSrcBsp bsp;
  if (!bsp.Load(map)) return false;
  if (bsp.faces.empty()) return false;
  float mnx = 1e9f, mny = 1e9f, mxx = -1e9f, mxy = -1e9f, mnz = 1e9f, mxz = -1e9f;
  auto drawable = [&](const SrcFace& f) {
    if (f.model != 0 || f.material < 0 || f.verts.size() < 3) return false;
    const std::string& n = f.material < (int)bsp.materials.size() ? bsp.materials[f.material] : std::string();
    if (n.rfind("tools/", 0) == 0) return false;            // nodraw / clip / hint / skybox hull
    if (n.find("skybox") != std::string::npos) return false;
    return true;
  };
  // a single stray brush parked far outside the level (a ceiling, a decal half a mile away) would shrink the whole
  // map into one corner, so the frame comes from the vertex distribution: the middle 99% of the positions
  std::vector<float> xs, ys;
  for (const SrcFace& f : bsp.faces) {
    if (!drawable(f)) continue;
    for (const SrcFaceVert& v : f.verts) { xs.push_back(v.pos.x); ys.push_back(v.pos.y); }
  }
  if (xs.size() < 3) return false;
  auto quant = [](std::vector<float>& v, float q) { std::sort(v.begin(), v.end()); return v[std::min(v.size() - 1, (size_t)(q * (float)(v.size() - 1)))]; };
  mnx = quant(xs, 0.005f); mxx = quant(xs, 0.995f);
  mny = quant(ys, 0.005f); mxy = quant(ys, 0.995f);
  mnz = 0; mxz = 0;
  if (mxx <= mnx || mxy <= mny) return false;
  const float span = std::max(mxx - mnx, mxy - mny);
  const int s = std::max(64, size);
  w = std::max(1, (int)std::lround(s * (mxx - mnx) / span));
  h = std::max(1, (int)std::lround(s * (mxy - mny) / span));
  const float scale = s / span;
  Msg("preview: bounds %.0f..%.0f x %.0f..%.0f, scale %.4f, %dx%d\n", mnx, mxx, mny, mxy, scale, w, h);
  rgba.assign((size_t)w * h * 4, 0);
  std::vector<float> depth((size_t)w * h, -1e9f);
  // one pass per triangle, painter style: a brush that is higher up covers what is under it
  for (const SrcFace& f : bsp.faces) {
    if (!drawable(f)) continue;
    const std::string& name = f.material < (int)bsp.materials.size() ? bsp.materials[f.material] : std::string();
    float cr = 0.6f, cg = 0.6f, cb = 0.6f;
    SurfaceColor(name, cr, cg, cb);
    const bool vertical = std::fabs(f.verts[0].normal.z) < 0.5f;
    if (vertical) { cr *= 0.72f; cg *= 0.72f; cb *= 0.78f; }   // walls darker, so floors stand out
    for (size_t k = 0; k + 2 < f.verts.size(); k += 3) {
      const SrcFaceVert* t[3] = { &f.verts[k], &f.verts[k + 1], &f.verts[k + 2] };
      float px[3], py[3], pz[3];
      for (int i = 0; i < 3; i++) { px[i] = (t[i]->pos.x - mnx) * scale; py[i] = (mxy - t[i]->pos.y) * scale; pz[i] = t[i]->pos.z; }
      if (std::max(px[0], std::max(px[1], px[2])) < 0 || std::min(px[0], std::min(px[1], px[2])) > w) continue;
      if (std::max(py[0], std::max(py[1], py[2])) < 0 || std::min(py[0], std::min(py[1], py[2])) > h) continue;
      const int minX = std::max(0, (int)std::floor(std::min(px[0], std::min(px[1], px[2]))));
      const int maxX = std::min(w - 1, (int)std::ceil(std::max(px[0], std::max(px[1], px[2]))));
      const int minY = std::max(0, (int)std::floor(std::min(py[0], std::min(py[1], py[2]))));
      const int maxY = std::min(h - 1, (int)std::ceil(std::max(py[0], std::max(py[1], py[2]))));
      const float d = (px[1] - px[0]) * (py[2] - py[0]) - (px[2] - px[0]) * (py[1] - py[0]);
      if (std::fabs(d) < 1e-6f) continue;
      for (int y = minY; y <= maxY; y++) for (int x = minX; x <= maxX; x++) {
        const float cx = x + 0.5f, cy = y + 0.5f;
        float w0 = ((px[1] - cx) * (py[2] - cy) - (px[2] - cx) * (py[1] - cy)) / d;
        float w1 = ((px[2] - cx) * (py[0] - cy) - (px[0] - cx) * (py[2] - cy)) / d;
        float w2 = 1.f - w0 - w1;
        if (w0 < -0.001f || w1 < -0.001f || w2 < -0.001f) continue;
        const float z = w0 * pz[0] + w1 * pz[1] + w2 * pz[2];
        const size_t o = (size_t)y * w + x;
        if (z < depth[o]) continue;
        depth[o] = z;
        uint8_t* p = &rgba[o * 4];
        p[0] = (uint8_t)std::clamp(cr * 255.f, 0.f, 255.f);
        p[1] = (uint8_t)std::clamp(cg * 255.f, 0.f, 255.f);
        p[2] = (uint8_t)std::clamp(cb * 255.f, 0.f, 255.f);
        p[3] = 255;
      }
    }
  }
  // fade the background to transparent so the picker tile colour shows through
  for (size_t i = 0; i < (size_t)w * h; i++) if (depth[i] < -1e8f) rgba[i * 4 + 3] = 0;
  return true;
}
// console: build the top-down preview of a map and write it to a png (debug / content check)
static ConCommand preview_dump("preview_dump", [](const CCommand& c) {
  if (c.ArgC() < 2) { Msg("usage: preview_dump <map> [out.png]\n"); return; }
  std::vector<uint8_t> rgba; int w = 0, h = 0;
  if (!RenderTopDown(c.Arg(1), rgba, w, h, 640)) { Msg("preview_dump: %s: no geometry\n", c.Arg(1)); return; }
  const char* out = c.ArgC() > 2 ? c.Arg(2) : "preview.png";
  Image im{ rgba.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
  Msg(ExportImage(im, out) ? "wrote %s (%dx%d)\n" : "can't write %s\n", out, w, h);
}, "Write the generated top-down map preview to a png");

} // namespace mapoverview
