// srcmaterial.cpp - VTF decoding, VMT parsing, texture/material caches
#include "srcmaterial.hpp"
#include "srcfs.hpp"
#include "keyvalues.hpp"
#include "convar.hpp"
#include "materialsystem.hpp"
#include "enginetrace.hpp"
#include "rlgl.h"
#include <unordered_map>
#include <map>
#include <cstring>
#include <cmath>
#include <algorithm>

#ifdef CS_HEADLESS
ConVar mat_texture_limit("mat_texture_limit", "256", FCVAR_ARCHIVE, "Max texture size for CS:S materials (applied on map load)", true, 16, true, 4096);
#else
ConVar mat_texture_limit("mat_texture_limit", "2048", FCVAR_ARCHIVE, "Max texture size for CS:S materials (applied on map load)", true, 16, true, 4096);
#endif
int SrcMat_TextureLimit() { return mat_texture_limit.GetInt(); }

// ------------------------------------------------------------------------------------------------
// VTF
namespace {
enum {
  IMG_RGBA8888 = 0, IMG_ABGR8888, IMG_RGB888, IMG_BGR888, IMG_RGB565, IMG_I8, IMG_IA88, IMG_P8, IMG_A8, IMG_RGB888_BLUESCREEN,
  IMG_BGR888_BLUESCREEN, IMG_ARGB8888, IMG_BGRA8888, IMG_DXT1, IMG_DXT3, IMG_DXT5, IMG_BGRX8888, IMG_BGR565, IMG_BGRX5551,
  IMG_BGRA4444, IMG_DXT1_ONEBITALPHA, IMG_BGRA5551, IMG_UV88, IMG_UVWQ8888, IMG_RGBA16161616F, IMG_RGBA16161616, IMG_UVLX8888,
};
size_t ImageSize(int fmt, int w, int h) {
  switch (fmt) {
    case IMG_DXT1: case IMG_DXT1_ONEBITALPHA: return (size_t)std::max(1, (w + 3) / 4) * std::max(1, (h + 3) / 4) * 8;
    case IMG_DXT3: case IMG_DXT5: return (size_t)std::max(1, (w + 3) / 4) * std::max(1, (h + 3) / 4) * 16;
    case IMG_RGBA8888: case IMG_ABGR8888: case IMG_ARGB8888: case IMG_BGRA8888: case IMG_BGRX8888: case IMG_UVWQ8888: case IMG_UVLX8888: return (size_t)w * h * 4;
    case IMG_RGB888: case IMG_BGR888: case IMG_RGB888_BLUESCREEN: case IMG_BGR888_BLUESCREEN: return (size_t)w * h * 3;
    case IMG_RGB565: case IMG_BGR565: case IMG_BGRX5551: case IMG_BGRA4444: case IMG_BGRA5551: case IMG_IA88: case IMG_UV88: return (size_t)w * h * 2;
    case IMG_I8: case IMG_A8: case IMG_P8: return (size_t)w * h;
    case IMG_RGBA16161616F: case IMG_RGBA16161616: return (size_t)w * h * 8;
    default: return 0;
  }
}
inline void C565(uint16_t c, uint8_t* o) {
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  o[0] = (uint8_t)((r << 3) | (r >> 2)); o[1] = (uint8_t)((g << 2) | (g >> 4)); o[2] = (uint8_t)((b << 3) | (b >> 2));
}
void DecodeColorBlock(const uint8_t* b, uint8_t pal[4][4], bool dxt1) {
  uint16_t c0 = (uint16_t)(b[0] | (b[1] << 8)), c1 = (uint16_t)(b[2] | (b[3] << 8));
  C565(c0, pal[0]); C565(c1, pal[1]); pal[0][3] = pal[1][3] = 255;
  if (c0 > c1 || !dxt1) {
    for (int k = 0; k < 3; k++) { pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3); pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3); }
    pal[2][3] = pal[3][3] = 255;
  } else {
    for (int k = 0; k < 3; k++) { pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2); pal[3][k] = 0; }
    pal[2][3] = 255; pal[3][3] = 0;
  }
}
void DecodeDXT(const uint8_t* src, int w, int h, int fmt, uint8_t* out) {
  int bw = std::max(1, (w + 3) / 4), bh = std::max(1, (h + 3) / 4);
  bool dxt1 = fmt == IMG_DXT1 || fmt == IMG_DXT1_ONEBITALPHA;
  for (int by = 0; by < bh; by++) for (int bx = 0; bx < bw; bx++) {
    const uint8_t* b = src + ((size_t)by * bw + bx) * (dxt1 ? 8 : 16);
    uint8_t alpha[16]; for (auto& a : alpha) a = 255;
    if (fmt == IMG_DXT3) { for (int i = 0; i < 16; i++) { int v = (b[i / 2] >> ((i & 1) * 4)) & 15; alpha[i] = (uint8_t)(v * 17); } b += 8; }
    else if (fmt == IMG_DXT5) {
      uint8_t a0 = b[0], a1 = b[1]; uint8_t ap[8]; ap[0] = a0; ap[1] = a1;
      if (a0 > a1) for (int i = 1; i < 7; i++) ap[i + 1] = (uint8_t)(((7 - i) * a0 + i * a1) / 7);
      else { for (int i = 1; i < 5; i++) ap[i + 1] = (uint8_t)(((5 - i) * a0 + i * a1) / 5); ap[6] = 0; ap[7] = 255; }
      uint64_t bits = 0; for (int i = 0; i < 6; i++) bits |= (uint64_t)b[2 + i] << (8 * i);
      for (int i = 0; i < 16; i++) alpha[i] = ap[(bits >> (3 * i)) & 7];
      b += 8;
    }
    uint8_t pal[4][4]; DecodeColorBlock(b, pal, dxt1);
    uint32_t idx = (uint32_t)(b[4] | (b[5] << 8) | (b[6] << 16) | ((uint32_t)b[7] << 24));
    for (int py = 0; py < 4; py++) for (int px = 0; px < 4; px++) {
      int x = bx * 4 + px, y = by * 4 + py; if (x >= w || y >= h) continue;
      int i = py * 4 + px; const uint8_t* c = pal[(idx >> (2 * i)) & 3];
      uint8_t* o = out + ((size_t)y * w + x) * 4;
      o[0] = c[0]; o[1] = c[1]; o[2] = c[2]; o[3] = dxt1 ? c[3] : alpha[i];
    }
  }
}
inline float HalfToFloat(uint16_t h) {
  uint32_t s = (h >> 15) & 1, e = (h >> 10) & 31, m = h & 1023;
  if (e == 0) return (s ? -1.f : 1.f) * std::ldexp((float)m, -24);
  if (e == 31) return s ? -65504.f : 65504.f;
  return (s ? -1.f : 1.f) * std::ldexp((float)(m | 1024), (int)e - 25);
}
bool DecodePixels(const uint8_t* s, int w, int h, int fmt, uint8_t* o) {
  size_t n = (size_t)w * h;
  switch (fmt) {
    case IMG_DXT1: case IMG_DXT1_ONEBITALPHA: case IMG_DXT3: case IMG_DXT5: DecodeDXT(s, w, h, fmt, o); return true;
    case IMG_RGBA8888: std::memcpy(o, s, n * 4); return true;
    case IMG_ABGR8888: for (size_t i = 0; i < n; i++) { o[i*4] = s[i*4+3]; o[i*4+1] = s[i*4+2]; o[i*4+2] = s[i*4+1]; o[i*4+3] = s[i*4]; } return true;
    case IMG_ARGB8888: for (size_t i = 0; i < n; i++) { o[i*4] = s[i*4+1]; o[i*4+1] = s[i*4+2]; o[i*4+2] = s[i*4+3]; o[i*4+3] = s[i*4]; } return true;
    case IMG_BGRA8888: for (size_t i = 0; i < n; i++) { o[i*4] = s[i*4+2]; o[i*4+1] = s[i*4+1]; o[i*4+2] = s[i*4]; o[i*4+3] = s[i*4+3]; } return true;
    case IMG_BGRX8888: for (size_t i = 0; i < n; i++) { o[i*4] = s[i*4+2]; o[i*4+1] = s[i*4+1]; o[i*4+2] = s[i*4]; o[i*4+3] = 255; } return true;
    case IMG_UVWQ8888: case IMG_UVLX8888: for (size_t i = 0; i < n; i++) { o[i*4] = s[i*4]; o[i*4+1] = s[i*4+1]; o[i*4+2] = s[i*4+2]; o[i*4+3] = 255; } return true;
    case IMG_RGB888: case IMG_RGB888_BLUESCREEN:
      for (size_t i = 0; i < n; i++) { o[i*4] = s[i*3]; o[i*4+1] = s[i*3+1]; o[i*4+2] = s[i*3+2]; o[i*4+3] = (fmt == IMG_RGB888_BLUESCREEN && s[i*3] == 0 && s[i*3+1] == 0 && s[i*3+2] == 255) ? 0 : 255; } return true;
    case IMG_BGR888: case IMG_BGR888_BLUESCREEN:
      for (size_t i = 0; i < n; i++) { o[i*4] = s[i*3+2]; o[i*4+1] = s[i*3+1]; o[i*4+2] = s[i*3]; o[i*4+3] = (fmt == IMG_BGR888_BLUESCREEN && s[i*3] == 255 && s[i*3+1] == 0 && s[i*3+2] == 0) ? 0 : 255; } return true;
    case IMG_RGB565: for (size_t i = 0; i < n; i++) { uint16_t c = (uint16_t)(s[i*2] | (s[i*2+1] << 8)); C565(c, o + i*4); o[i*4+3] = 255; } return true;
    case IMG_BGR565: for (size_t i = 0; i < n; i++) { uint16_t c = (uint16_t)(s[i*2] | (s[i*2+1] << 8)); uint8_t t[3]; C565(c, t); o[i*4] = t[2]; o[i*4+1] = t[1]; o[i*4+2] = t[0]; o[i*4+3] = 255; } return true;
    case IMG_BGRA4444: for (size_t i = 0; i < n; i++) { uint16_t c = (uint16_t)(s[i*2] | (s[i*2+1] << 8)); o[i*4] = (uint8_t)(((c >> 8) & 15) * 17); o[i*4+1] = (uint8_t)(((c >> 4) & 15) * 17); o[i*4+2] = (uint8_t)((c & 15) * 17); o[i*4+3] = (uint8_t)(((c >> 12) & 15) * 17); } return true;
    case IMG_BGRX5551: case IMG_BGRA5551: for (size_t i = 0; i < n; i++) { uint16_t c = (uint16_t)(s[i*2] | (s[i*2+1] << 8)); o[i*4] = (uint8_t)(((c >> 10) & 31) * 255 / 31); o[i*4+1] = (uint8_t)(((c >> 5) & 31) * 255 / 31); o[i*4+2] = (uint8_t)((c & 31) * 255 / 31); o[i*4+3] = fmt == IMG_BGRA5551 ? ((c >> 15) ? 255 : 0) : 255; } return true;
    case IMG_I8: for (size_t i = 0; i < n; i++) { o[i*4] = o[i*4+1] = o[i*4+2] = s[i]; o[i*4+3] = 255; } return true;
    case IMG_IA88: for (size_t i = 0; i < n; i++) { o[i*4] = o[i*4+1] = o[i*4+2] = s[i*2]; o[i*4+3] = s[i*2+1]; } return true;
    case IMG_A8: for (size_t i = 0; i < n; i++) { o[i*4] = o[i*4+1] = o[i*4+2] = 255; o[i*4+3] = s[i]; } return true;
    case IMG_UV88: for (size_t i = 0; i < n; i++) { o[i*4] = s[i*2]; o[i*4+1] = s[i*2+1]; o[i*4+2] = 255; o[i*4+3] = 255; } return true;
    case IMG_RGBA16161616F: for (size_t i = 0; i < n; i++) for (int k = 0; k < 4; k++) {
      float f = HalfToFloat((uint16_t)(s[i*8+k*2] | (s[i*8+k*2+1] << 8)));
      if (k < 3) f = std::pow(std::max(0.f, f), 1.f / 2.2f);
      o[i*4+k] = (uint8_t)std::clamp(f * 255.f, 0.f, 255.f);
    } return true;
    case IMG_RGBA16161616: for (size_t i = 0; i < n; i++) for (int k = 0; k < 4; k++) o[i*4+k] = s[i*8+k*2+1]; return true;
    default: return false;
  }
}
} // namespace

bool VTF_Decode(const std::vector<uint8_t>& d, std::vector<uint8_t>& rgba, int& w, int& h, int maxDim, int* fullW, int* fullH) {
  if (d.size() < 64 || std::memcmp(d.data(), "VTF\0", 4) != 0) return false;
  uint32_t vmaj, vmin, hdrSize; std::memcpy(&vmaj, &d[4], 4); std::memcpy(&vmin, &d[8], 4); std::memcpy(&hdrSize, &d[12], 4);
  uint16_t W, H, frames, firstFrame; uint32_t flags; std::memcpy(&W, &d[16], 2); std::memcpy(&H, &d[18], 2); std::memcpy(&flags, &d[20], 4);
  std::memcpy(&frames, &d[24], 2); std::memcpy(&firstFrame, &d[26], 2);
  int32_t fmt, lowFmt; std::memcpy(&fmt, &d[52], 4); uint8_t mips = d[56]; std::memcpy(&lowFmt, &d[57], 4);
  uint8_t lowW = d[61], lowH = d[62];
  uint16_t depth = 1; if (vmaj == 7 && vmin >= 2 && d.size() >= 65) { std::memcpy(&depth, &d[63], 2); if (depth == 0) depth = 1; }
  if (fullW) *fullW = W; if (fullH) *fullH = H;
  if (W == 0 || H == 0 || mips == 0 || frames == 0) return false;
  int faces = (flags & 0x4000) ? ((vmaj == 7 && vmin < 5 && firstFrame == 0xffff) ? 7 : 6) : 1;
  size_t hiOff = 0;
  if (vmaj == 7 && vmin >= 3) {
    uint32_t numRes = 0; if (d.size() >= 72) std::memcpy(&numRes, &d[68], 4);
    bool found = false;
    for (uint32_t i = 0; i < numRes && 80 + i * 8 + 8 <= d.size(); i++) {
      const uint8_t* r = &d[80 + i * 8];
      if (r[0] == 0x30 && r[1] == 0 && r[2] == 0) { uint32_t off; std::memcpy(&off, r + 4, 4); hiOff = off; found = true; break; }
    }
    if (!found) return false;
  } else {
    hiOff = hdrSize + (lowFmt >= 0 ? ImageSize(lowFmt, lowW, lowH) : 0);
  }
  int pick = 0; while (pick + 1 < mips && (std::max(W >> pick, 1) > maxDim || std::max(H >> pick, 1) > maxDim)) pick++;
  size_t off = hiOff;
  for (int m = mips - 1; m > pick; m--) off += ImageSize(fmt, std::max(W >> m, 1), std::max(H >> m, 1)) * frames * faces * depth;
  w = std::max(W >> pick, 1); h = std::max(H >> pick, 1);
  size_t sz = ImageSize(fmt, w, h);
  if (!sz || off + sz > d.size()) return false;
  rgba.resize((size_t)w * h * 4);
  return DecodePixels(&d[off], w, h, fmt, rgba.data());
}

bool VTF_Load(const std::string& rel, std::vector<uint8_t>& rgba, int& w, int& h, int maxDim) {
  std::vector<uint8_t> d; if (!srcfs::Read(rel, d)) return false;
  return VTF_Decode(d, rgba, w, h, maxDim);
}

// ------------------------------------------------------------------------------------------------
// textures / materials
namespace {
struct TexEntry { Texture2D tex{}; int fullW = 0, fullH = 0; bool ok = false; bool hasAlpha = false; };
std::unordered_map<std::string, TexEntry> s_tex;
std::unordered_map<std::string, SrcMaterial> s_mats;

Texture2D Upload(const std::vector<uint8_t>& rgba, int w, int h) {
  Image im; im.data = (void*)rgba.data(); im.width = w; im.height = h; im.mipmaps = 1; im.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  Image copy = ImageCopy(im);
#ifndef CS_HEADLESS
  if ((w & (w - 1)) == 0 && (h & (h - 1)) == 0) ImageMipmaps(&copy);
#endif
  Texture2D t = LoadTextureFromImage(copy);
  UnloadImage(copy);
#ifndef CS_HEADLESS
  SetTextureFilter(t, t.mipmaps > 1 ? TEXTURE_FILTER_TRILINEAR : TEXTURE_FILTER_BILINEAR);
#endif
  SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
  return t;
}
const TexEntry& LoadTex(const std::string& name) {
  std::string key = srcfs::Normalize(name);
  if (key.size() > 4 && key.compare(key.size() - 4, 4, ".vtf") == 0) key.resize(key.size() - 4);
  auto it = s_tex.find(key); if (it != s_tex.end()) return it->second;
  TexEntry e;
  std::vector<uint8_t> file, rgba; int w = 0, h = 0, fw = 0, fh = 0;
  if (srcfs::Read("materials/" + key + ".vtf", file) && VTF_Decode(file, rgba, w, h, SrcMat_TextureLimit(), &fw, &fh)) {
    e.tex = Upload(rgba, w, h); e.fullW = fw; e.fullH = fh; e.ok = true;
    for (size_t i = 3; i < rgba.size(); i += 4) if (rgba[i] < 250) { e.hasAlpha = true; break; }
  }
  return s_tex[key] = e;
}
// A missing vmt / vtf (a CS:GO map without its material library) falls back to a plausible procedural surface
// picked from the material path - never a checkerboard in game. Real textures always win when they exist.
int FallbackMaterialId(const std::string& key) {
  std::string k = key;
  auto has = [&](const char* t) { return k.find(t) != std::string::npos; };
  if (has("water") || has("lava") || has("slime")) return MAT_DEV_BLUE;
  if (has("glass") || has("window")) return MAT_DEV_BLUE;
  if (has("tile") || has("ceramic") || has("porcelain") || has("bathroom")) return MAT_TILE_FLOOR;
  if (has("brick")) return MAT_BRICK_RED;
  if (has("wood") || has("crate") || has("plank") || has("door_frame")) return has("crate") ? MAT_CRATE : MAT_WOOD_PLANKS;
  if (has("metal") || has("steel") || has("iron") || has("girder") || has("beam") || has("grate") || has("rust")) return MAT_METAL;
  if (has("sand") || has("dust") || has("desert")) return MAT_SANDSTONE_WALL;
  if (has("grass") || has("foliage") || has("leaf") || has("leaves") || has("moss")) return MAT_GRASS;
  if (has("roof") || has("shingle") || has("tile_roof")) return MAT_ROOF_TILES;
  if (has("stone") || has("rock") || has("cliff") || has("marble") || has("granite")) return MAT_STONE_DARK;
  if (has("cobble") || has("pavement") || has("asphalt") || has("street")) return MAT_COBBLE;
  if (has("plaster") || has("stucco") || has("wallpaper")) return MAT_PLASTER_WHITE;
  if (has("floor") || has("ground") || has("concrete") || has("cement") || has("trim")) return MAT_CONCRETE;
  return MAT_CONCRETE;
}
Texture2D Checker();
Texture2D FallbackTex(const std::string& key) {
  if (materials->initialized) {
    MaterialInfo& mi = materials->Get(FallbackMaterialId(key));
    if (mi.tex.id) return mi.tex;
  }
  return Checker();
}

Texture2D Checker() {
  static Texture2D t{}; if (t.id) return t;
  std::vector<uint8_t> px(32 * 32 * 4);
  for (int y = 0; y < 32; y++) for (int x = 0; x < 32; x++) { uint8_t c = ((x / 8 + y / 8) & 1) ? 140 : 100; uint8_t* o = &px[(y * 32 + x) * 4]; o[0] = o[1] = o[2] = c; o[3] = 255; }
  t = Upload(px, 32, 32); return t;
}

// returns the (merged) shader block of a vmt, following "patch" includes
std::unique_ptr<KeyValues> LoadVmt(const std::string& rel, std::string& shader, int depth = 0) {
  std::vector<uint8_t> d; if (depth > 4 || !srcfs::Read(rel, d)) return nullptr;
  auto root = KeyValues::Parse((const char*)d.data(), d.size());
  if (root->children.empty()) return nullptr;
  std::unique_ptr<KeyValues> blk = std::move(root->children[0]);
  shader = KV_Lower(blk->key);
  if (shader == "patch") {
    std::string inc = blk->GetString("include");
    std::string incShader;
    auto base = LoadVmt(inc, incShader, depth + 1);
    if (!base) return nullptr;
    if (const KeyValues* ins = blk->Find("insert")) base->MergeFrom(*ins, false);
    if (const KeyValues* rep = blk->Find("replace")) base->MergeFrom(*rep, true);
    shader = incShader; return base;
  }
  // shader fallback blocks ("LightmappedGeneric_DX9", ">=dx90") override the base keys
  std::vector<const KeyValues*> extra;
  for (auto& c : blk->children) if (c->isBlock) {
    std::string k = KV_Lower(c->key);
    if (k.find("dx9") != std::string::npos || k.find(">=dx90") != std::string::npos || k.find("hdr") != std::string::npos) extra.push_back(c.get());
  }
  for (const KeyValues* e : extra) { KeyValues tmp; tmp.MergeFrom(*e, true); blk->MergeFrom(tmp, true); }
  return blk;
}
Vector ParseColor(const std::string& s) {
  float r = 1, g = 1, b = 1; std::string t = s;
  for (char& c : t) if (c == '[' || c == ']' || c == '{' || c == '}') c = ' ';
  bool ints = s.find('{') != std::string::npos;
  if (sscanf(t.c_str(), "%f %f %f", &r, &g, &b) == 3) { if (ints) { r /= 255.f; g /= 255.f; b /= 255.f; } return { r, g, b }; }
  return { 1, 1, 1 };
}
} // namespace

static SrcMaterial BuildMaterial(const std::string& key) {
  SrcMaterial m; m.name = key;
  std::string shader;
  auto kv = LoadVmt("materials/" + key + ".vmt", shader);
  if (key.rfind("tools/", 0) == 0) m.tool = true;
  if (key.rfind("tools/toolsnodraw", 0) == 0 || key.rfind("tools/toolsskip", 0) == 0 || key.rfind("tools/toolshint", 0) == 0) m.nodraw = true;
  if (key.rfind("tools/toolsskybox", 0) == 0) m.sky = true;
  if (!kv) { m.tex = FallbackTex(key); m.width = m.height = 128; return m; }
  m.ok = true; m.shader = shader;
  auto B = [&](const char* k) { return kv->GetInt(k, 0) != 0; };
  m.translucent = B("$translucent"); m.alphatest = B("$alphatest"); m.additive = B("$additive"); m.nocull = B("$nocull");
  m.vertexalpha = B("$vertexalpha");
  m.alpha = kv->GetFloat("$alpha", 1.f); if (m.alpha < 0.999f) m.translucent = true;
  m.alphaRef = kv->GetFloat("$alphatestreference", 0.5f);
  if (const KeyValues* c = kv->Find("$color")) m.color = ParseColor(c->value);
  m.surfaceprop = KV_Lower(kv->GetString("$surfaceprop"));
  if (B("$no_draw") || B("$nodraw")) m.nodraw = true;
  if (shader == "water") { m.water = true; m.translucent = true; }
  if (shader.find("sky") != std::string::npos) m.sky = true;
  if (shader == "unlitgeneric" || shader == "sprite" || shader == "spritecard" || shader == "unlittwotexture") m.unlit = true;
  std::string bt = kv->GetString("$basetexture");
  if (m.water && bt.empty()) { m.color = Vector(0.25f, 0.33f, 0.3f); m.alpha = 0.7f; }
  if (!bt.empty()) {
    const TexEntry& t = LoadTex(bt);
    if (t.ok) { m.tex = t.tex; m.width = t.fullW; m.height = t.fullH; }
    else { m.tex = FallbackTex(bt); m.width = m.height = 128; }
    if (!m.translucent && !m.alphatest && t.ok && t.hasAlpha && m.additive) {}
  } else { m.tex = FallbackTex(key); m.width = m.height = 128; }
  std::string bt2 = kv->GetString("$basetexture2");
  if (!bt2.empty()) { const TexEntry& t2 = LoadTex(bt2); if (t2.ok) m.tex2 = t2.tex; }
  return m;
}

const SrcMaterial& SrcMat_Find(const std::string& name, bool) {
  std::string key = srcfs::Normalize(name);
  if (key.rfind("materials/", 0) == 0) key.erase(0, 10);
  if (key.size() > 4 && key.compare(key.size() - 4, 4, ".vmt") == 0) key.resize(key.size() - 4);
  auto it = s_mats.find(key); if (it != s_mats.end()) return it->second;
  return s_mats[key] = BuildMaterial(key);
}

const SrcMaterial& SrcMat_FindInDirs(const std::string& name, const std::vector<std::string>& dirs) {
  for (const std::string& d : dirs) {
    std::string p = srcfs::Normalize(d + "/" + name);
    if (s_mats.count(p) && s_mats[p].ok) return s_mats[p];
    if (srcfs::Exists("materials/" + p + ".vmt")) return SrcMat_Find(p);
  }
  return SrcMat_Find(dirs.empty() ? name : srcfs::Normalize(dirs[0] + "/" + name));
}

static int s_matGeneration = 1;
int SrcMat_Generation() { return s_matGeneration; }
void SrcMat_Clear() {
  for (auto& [k, t] : s_tex) if (t.ok && t.tex.id) UnloadTexture(t.tex);
  s_tex.clear(); s_mats.clear();
  s_matGeneration++;
}

int SrcMat_SurfaceFromProp(const std::string& prop, const std::string& matName) {
  std::string p = prop.empty() ? matName : prop;
  auto has = [&](const char* s) { return p.find(s) != std::string::npos; };
  if (has("chainlink") || has("grate") || has("metalgrate")) return SURFACE_METALGRATE;
  if (has("metal") || has("vent") || has("canister") || has("combine")) return SURFACE_METAL;
  if (has("crate")) return SURFACE_WOOD_CRATE;
  if (has("wood")) return SURFACE_WOOD;
  if (has("glass") || has("window")) return SURFACE_GLASS;
  if (has("tile") || has("ceramic") || has("porcelain")) return SURFACE_TILE;
  if (has("sand")) return SURFACE_SAND;
  if (has("dirt") || has("grass") || has("mud") || has("gravel") || has("foliage")) return SURFACE_DIRT;
  if (has("plaster") || has("drywall")) return SURFACE_PLASTER;
  if (has("brick")) return SURFACE_BRICK;
  if (has("cardboard") || has("paper")) return SURFACE_CARDBOARD;
  if (has("flesh")) return SURFACE_FLESH;
  if (has("stone") || has("rock")) return SURFACE_SANDSTONE;
  return SURFACE_CONCRETE;
}
