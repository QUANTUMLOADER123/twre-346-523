#include "materialsystem.hpp"
#include "enginetrace.hpp"
#include <cmath>
#include <cstring>
#include <algorithm>

namespace {
struct Pix {
  int w, h; std::vector<Color> p;
  Pix(int W, int H) : w(W), h(H), p(W * H, Color{ 128, 128, 128, 255 }) {}
  Color& at(int x, int y) { x = ((x % w) + w) % w; y = ((y % h) + h) % h; return p[y * w + x]; }
};
inline uint32_t hash2(int x, int y, uint32_t s) { uint32_t h = (uint32_t)x * 374761393u + (uint32_t)y * 668265263u + s * 2246822519u; h = (h ^ (h >> 13)) * 1274126177u; return h ^ (h >> 16); }
inline float rnd2(int x, int y, uint32_t s) { return (hash2(x, y, s) & 0xFFFFFF) / 16777215.f; }
// tileable value noise with period p
float vnoise(float x, float y, int period, uint32_t seed) {
  int xi = (int)std::floor(x), yi = (int)std::floor(y); float fx = x - xi, fy = y - yi;
  auto r = [&](int a, int b) { return rnd2(((a % period) + period) % period, ((b % period) + period) % period, seed); };
  float sx = fx * fx * (3 - 2 * fx), sy = fy * fy * (3 - 2 * fy);
  float a = r(xi, yi), b = r(xi + 1, yi), c = r(xi, yi + 1), d = r(xi + 1, yi + 1);
  return a + (b - a) * sx + (c - a) * sy + (a - b - c + d) * sx * sy;
}
float fbm(float x, float y, int period, uint32_t seed, int oct = 4) {
  float s = 0, amp = 0.5f, tot = 0; int per = period;
  for (int i = 0; i < oct; i++) { s += vnoise(x, y, per, seed + i * 17) * amp; tot += amp; x *= 2; y *= 2; per *= 2; amp *= 0.5f; }
  return s / tot;
}
inline uint8_t c8(float v) { return (uint8_t)Clamp(v, 0.f, 255.f); }
inline Color mul(Color c, float k) { return { c8(c.r * k), c8(c.g * k), c8(c.b * k), 255 }; }
inline Color mix(Color a, Color b, float t) { return { c8(a.r + (b.r - a.r) * t), c8(a.g + (b.g - a.g) * t), c8(a.b + (b.b - a.b) * t), 255 }; }

// fill with base color modulated by fbm noise
void noiseFill(Pix& px, Color base, float amount, int period, uint32_t seed, float fine = 0.08f) {
  for (int y = 0; y < px.h; y++) for (int x = 0; x < px.w; x++) {
    float n = fbm(x * period / (float)px.w, y * period / (float)px.h, period, seed);
    float g = rnd2(x, y, seed + 99) - 0.5f;
    px.at(x, y) = mul(base, 1.f + (n - 0.5f) * amount + g * fine);
  }
}
// bricks / blocks pattern
void blocks(Pix& px, int bw, int bh, Color mortar, float mortarW, float jitter, uint32_t seed, float bevel = 0.f) {
  for (int y = 0; y < px.h; y++) for (int x = 0; x < px.w; x++) {
    int row = y / bh; int off = (row % 2) ? bw / 2 : 0;
    int bx = (x + off) / bw, lx = (x + off) % bw, ly = y % bh;
    float edge = (float)std::min(std::min(lx, bw - 1 - lx), std::min(ly, bh - 1 - ly));
    Color c = px.at(x, y);
    float k = 1.f + (rnd2(bx, row, seed) - 0.5f) * jitter;
    c = mul(c, k);
    if (bevel > 0 && edge < bevel) c = mul(c, 0.82f + 0.18f * edge / bevel);
    if (edge < mortarW) c = mix(c, mortar, 0.85f);
    px.at(x, y) = c;
  }
}
void planks(Pix& px, int pw, Color gap, uint32_t seed, bool vertical) {
  for (int y = 0; y < px.h; y++) for (int x = 0; x < px.w; x++) {
    int u = vertical ? x : y, v = vertical ? y : x;
    int idx = u / pw, lu = u % pw;
    Color c = px.at(x, y);
    float grain = std::sin((v * 0.15f + fbm(u * 0.05f, v * 0.02f, 8, seed) * 12.f)) * 0.06f;
    c = mul(c, 1.f + (rnd2(idx, 0, seed) - 0.5f) * 0.25f + grain);
    if (lu == 0 || lu == pw - 1) c = mix(c, gap, 0.8f);
    px.at(x, y) = c;
  }
}
Texture2D upload(Pix& px, bool mip = true) {
  Image im; im.data = px.p.data(); im.width = px.w; im.height = px.h; im.mipmaps = 1; im.format = PIXELFORMAT_UNCOMPRESSED_R8G8B8A8;
  Image copy = ImageCopy(im);
#ifndef CS_HEADLESS
  if (mip) ImageMipmaps(&copy);
#else
  (void)mip;
#endif
  Texture2D t = LoadTextureFromImage(copy);
  UnloadImage(copy);
#ifndef CS_HEADLESS
  if (mip && t.mipmaps > 1) SetTextureFilter(t, TEXTURE_FILTER_TRILINEAR); else SetTextureFilter(t, TEXTURE_FILTER_BILINEAR);
#endif
  SetTextureWrap(t, TEXTURE_WRAP_REPEAT);
  return t;
}
Color average(Pix& px) { long r = 0, g = 0, b = 0; for (auto& c : px.p) { r += c.r; g += c.g; b += c.b; } long n = (long)px.p.size(); return { (uint8_t)(r / n), (uint8_t)(g / n), (uint8_t)(b / n), 255 }; }
} // namespace

static const MaterialDef s_defs[MAT_COUNT] = {
  { "none", SURFACE_DEFAULT },
  { "sand_floor", SURFACE_SAND }, { "sand_packed", SURFACE_DIRT }, { "sandstone_wall", SURFACE_SANDSTONE }, { "sandstone_blocks", SURFACE_SANDSTONE },
  { "stone_trim", SURFACE_SANDSTONE }, { "plaster_white", SURFACE_PLASTER }, { "plaster_yellow", SURFACE_PLASTER }, { "plaster_blue", SURFACE_PLASTER },
  { "brick_red", SURFACE_BRICK }, { "concrete", SURFACE_CONCRETE }, { "concrete_dark", SURFACE_CONCRETE }, { "tile_floor", SURFACE_TILE },
  { "terracotta", SURFACE_TILE }, { "cobblestone", SURFACE_CONCRETE }, { "asphalt", SURFACE_CONCRETE }, { "planks", SURFACE_WOOD },
  { "door", SURFACE_WOOD }, { "crate", SURFACE_WOOD_CRATE }, { "crate_dark", SURFACE_WOOD_CRATE }, { "metal_plate", SURFACE_METAL },
  { "door_green", SURFACE_METAL }, { "rust", SURFACE_METAL }, { "roof_tiles", SURFACE_TILE }, { "dev_gray", SURFACE_CONCRETE },
  { "dev_orange", SURFACE_CONCRETE }, { "dev_blue", SURFACE_CONCRETE }, { "ice", SURFACE_GLASS }, { "snow", SURFACE_SAND },
  { "grass", SURFACE_DIRT }, { "barrel", SURFACE_METAL }, { "stone_dark", SURFACE_CONCRETE },
};
const MaterialDef& GetMaterialDef(int id) { return s_defs[(id >= 0 && id < MAT_COUNT) ? id : 0]; }
int MaterialFromName(const std::string& n) {
  for (int i = 1; i < MAT_COUNT; i++) if (n == s_defs[i].shortName) return i;
  return MAT_DEV_GRAY;
}

void CMaterialSystem::Init() {
  if (initialized) return;
  const int S = 128;
  auto make = [&](int id, const char* name, int surf, float worldSize, auto&& gen) {
    Pix px(S, S); gen(px);
    MaterialInfo& m = m_mats[id]; m.name = name; m.surfaceProp = surf; m.texWorldSize = worldSize; m.tex = upload(px); m.avg = average(px);
  };
  make(MAT_SAND_FLOOR, "de_dust/sand_floor", SURFACE_SAND, 128, [](Pix& p) {
    noiseFill(p, { 206, 172, 120, 255 }, 0.35f, 4, 11, 0.12f);
    for (int i = 0; i < 90; i++) { int x = (int)(rnd2(i, 1, 5) * 128), y = (int)(rnd2(i, 2, 5) * 128); p.at(x, y) = mul(p.at(x, y), 0.7f); p.at(x + 1, y) = mul(p.at(x + 1, y), 0.8f); }
  });
  make(MAT_SAND_FLOOR2, "de_dust/sand_packed", SURFACE_DIRT, 192, [](Pix& p) { noiseFill(p, { 190, 156, 106, 255 }, 0.45f, 3, 12, 0.10f); });
  make(MAT_SANDSTONE_WALL, "de_dust/sandstone_wall", SURFACE_SANDSTONE, 128, [](Pix& p) {
    noiseFill(p, { 214, 184, 134, 255 }, 0.3f, 4, 21, 0.07f);
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) { float n = fbm(x / 16.f, y / 16.f, 8, 23); if (n > 0.68f) p.at(x, y) = mul(p.at(x, y), 0.86f); }
  });
  make(MAT_SANDSTONE_BLOCKS, "de_dust/sandstone_blocks", SURFACE_SANDSTONE, 128, [](Pix& p) {
    noiseFill(p, { 205, 172, 122, 255 }, 0.25f, 4, 31, 0.08f); blocks(p, 64, 32, { 150, 120, 85, 255 }, 1.5f, 0.18f, 33, 4.f);
  });
  make(MAT_SANDSTONE_TRIM, "de_dust/stone_trim", SURFACE_SANDSTONE, 64, [](Pix& p) {
    noiseFill(p, { 186, 156, 112, 255 }, 0.2f, 4, 41, 0.06f); blocks(p, 128, 64, { 130, 105, 75, 255 }, 2.f, 0.1f, 42, 10.f);
  });
  make(MAT_PLASTER_WHITE, "de_mirage/plaster_white", SURFACE_PLASTER, 128, [](Pix& p) {
    noiseFill(p, { 228, 222, 206, 255 }, 0.18f, 4, 51, 0.05f);
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) { float n = fbm(x / 20.f, y / 20.f, 6, 53); if (n > 0.66f) p.at(x, y) = mix(p.at(x, y), { 180, 160, 130, 255 }, (n - 0.66f) * 2.5f); }
  });
  make(MAT_PLASTER_YELLOW, "de_mirage/plaster_yellow", SURFACE_PLASTER, 128, [](Pix& p) { noiseFill(p, { 226, 196, 132, 255 }, 0.22f, 4, 61, 0.05f); });
  make(MAT_PLASTER_BLUE, "de_mirage/plaster_blue", SURFACE_PLASTER, 128, [](Pix& p) { noiseFill(p, { 120, 160, 186, 255 }, 0.22f, 4, 62, 0.05f); });
  make(MAT_BRICK_RED, "de_inferno/brick_red", SURFACE_BRICK, 128, [](Pix& p) {
    noiseFill(p, { 162, 82, 62, 255 }, 0.25f, 4, 71, 0.1f); blocks(p, 32, 16, { 190, 180, 160, 255 }, 1.2f, 0.3f, 72, 0.f);
  });
  make(MAT_CONCRETE, "concrete/concrete_floor", SURFACE_CONCRETE, 192, [](Pix& p) {
    noiseFill(p, { 160, 158, 150, 255 }, 0.25f, 4, 81, 0.1f);
    for (int x = 0; x < 128; x++) { p.at(x, 0) = mul(p.at(x, 0), 0.75f); p.at(0, x) = mul(p.at(0, x), 0.75f); }
  });
  make(MAT_CONCRETE_DARK, "concrete/concrete_dark", SURFACE_CONCRETE, 192, [](Pix& p) { noiseFill(p, { 112, 110, 106, 255 }, 0.3f, 4, 82, 0.1f); });
  make(MAT_TILE_FLOOR, "tile/tile_floor", SURFACE_TILE, 128, [](Pix& p) {
    noiseFill(p, { 196, 190, 178, 255 }, 0.12f, 4, 91, 0.05f); blocks(p, 32, 32, { 120, 110, 100, 255 }, 1.f, 0.12f, 92, 0.f);
  });
  make(MAT_TILE_TERRACOTTA, "tile/terracotta", SURFACE_TILE, 128, [](Pix& p) {
    noiseFill(p, { 176, 104, 70, 255 }, 0.18f, 4, 93, 0.06f); blocks(p, 32, 32, { 140, 120, 100, 255 }, 1.f, 0.2f, 94, 2.f);
  });
  make(MAT_COBBLE, "stone/cobblestone", SURFACE_CONCRETE, 128, [](Pix& p) {
    noiseFill(p, { 150, 136, 118, 255 }, 0.2f, 4, 101, 0.1f); blocks(p, 24, 20, { 95, 85, 70, 255 }, 1.6f, 0.35f, 102, 5.f);
  });
  make(MAT_ASPHALT, "street/asphalt", SURFACE_CONCRETE, 256, [](Pix& p) { noiseFill(p, { 72, 72, 76, 255 }, 0.25f, 8, 111, 0.18f); });
  make(MAT_WOOD_PLANKS, "wood/planks", SURFACE_WOOD, 128, [](Pix& p) { noiseFill(p, { 150, 105, 62, 255 }, 0.15f, 4, 121, 0.06f); planks(p, 16, { 70, 45, 25, 255 }, 122, true); });
  make(MAT_WOOD_DOOR, "wood/door", SURFACE_WOOD, 128, [](Pix& p) {
    noiseFill(p, { 110, 70, 38, 255 }, 0.15f, 4, 123, 0.06f); planks(p, 21, { 50, 30, 18, 255 }, 124, true);
    for (int x = 0; x < 128; x++) for (int dy = -3; dy <= 3; dy++) { p.at(x, 30 + dy) = mul(p.at(x, 30 + dy), 0.6f); p.at(x, 98 + dy) = mul(p.at(x, 98 + dy), 0.6f); }
  });
  auto crate = [](Pix& p, Color base, uint32_t seed) {
    noiseFill(p, base, 0.12f, 4, seed, 0.05f); planks(p, 16, mul(base, 0.45f), seed + 1, false);
    Color frame = mul(base, 0.8f);
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
      bool border = x < 12 || x > 115 || y < 12 || y > 115;
      bool diag = std::abs(x - y) < 8 && !border;
      if (border || diag) { Color c = mul(frame, 1.f + (rnd2(x / 4, y / 4, seed) - 0.5f) * 0.1f); if (border && (x < 2 || x > 125 || y < 2 || y > 125 || (x > 10 && x < 13) || (x > 114 && x < 117) || (y > 10 && y < 13) || (y > 114 && y < 117))) c = mul(c, 0.55f); p.at(x, y) = c; }
      if ((x == 6 || x == 121) && (y == 6 || y == 121)) { p.at(x, y) = { 60, 60, 60, 255 }; }
    }
  };
  make(MAT_CRATE, "props/crate", SURFACE_WOOD_CRATE, 64, [&](Pix& p) { crate(p, { 176, 134, 78, 255 }, 131); });
  make(MAT_CRATE_DARK, "props/crate_dark", SURFACE_WOOD_CRATE, 64, [&](Pix& p) { crate(p, { 120, 92, 56, 255 }, 132); });
  make(MAT_METAL, "metal/metal_plate", SURFACE_METAL, 128, [](Pix& p) {
    noiseFill(p, { 118, 124, 130, 255 }, 0.15f, 4, 141, 0.05f); blocks(p, 64, 64, { 70, 74, 78, 255 }, 1.2f, 0.08f, 142, 3.f);
    for (int k = 0; k < 4; k++) for (int j = 0; j < 4; j++) { int x = 6 + k * 32, y = 6 + j * 32; if ((k + j) % 2 == 0) { p.at(x, y) = { 60, 60, 60, 255 }; p.at(x + 1, y) = { 160, 160, 160, 255 }; } }
  });
  make(MAT_METAL_DOOR, "metal/door_green", SURFACE_METAL, 128, [](Pix& p) {
    noiseFill(p, { 70, 104, 78, 255 }, 0.15f, 4, 151, 0.06f);
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) if (x % 32 < 2) p.at(x, y) = mul(p.at(x, y), 0.6f);
  });
  make(MAT_METAL_RUST, "metal/rust", SURFACE_METAL, 128, [](Pix& p) {
    noiseFill(p, { 120, 84, 60, 255 }, 0.35f, 4, 161, 0.1f);
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) { float n = fbm(x / 10.f, y / 10.f, 12, 162); if (n < 0.42f) p.at(x, y) = mix(p.at(x, y), { 100, 110, 115, 255 }, 0.6f); }
  });
  make(MAT_ROOF_TILES, "de_dust/roof_tiles", SURFACE_TILE, 128, [](Pix& p) {
    noiseFill(p, { 160, 88, 58, 255 }, 0.2f, 4, 171, 0.08f);
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) { float s = std::sin((x % 16) / 16.f * 3.14159f); p.at(x, y) = mul(p.at(x, y), 0.7f + 0.35f * s); if (y % 16 == 0) p.at(x, y) = mul(p.at(x, y), 0.55f); }
  });
  auto dev = [](Pix& p, Color base, Color line) {
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) {
      Color c = ((x / 64 + y / 64) % 2) ? base : mul(base, 0.92f);
      if (x % 64 == 0 || y % 64 == 0) c = line; else if (x % 16 == 0 || y % 16 == 0) c = mix(c, line, 0.35f);
      p.at(x, y) = c;
    }
  };
  make(MAT_DEV_GRAY, "dev/dev_measuregeneric01", SURFACE_CONCRETE, 128, [&](Pix& p) { dev(p, { 150, 150, 150, 255 }, { 210, 210, 210, 255 }); });
  make(MAT_DEV_ORANGE, "dev/dev_measurewall_orange", SURFACE_CONCRETE, 128, [&](Pix& p) { dev(p, { 214, 120, 40, 255 }, { 245, 190, 120, 255 }); });
  make(MAT_DEV_BLUE, "dev/dev_measurewall_blue", SURFACE_CONCRETE, 128, [&](Pix& p) { dev(p, { 60, 100, 170, 255 }, { 140, 180, 240, 255 }); });
  make(MAT_ICE, "fy_iceworld/ice", SURFACE_GLASS, 256, [](Pix& p) {
    noiseFill(p, { 170, 214, 236, 255 }, 0.2f, 4, 181, 0.04f);
    for (int i = 0; i < 6; i++) { float a = rnd2(i, 0, 182) * 6.28f; int x = (int)(rnd2(i, 1, 182) * 128), y = (int)(rnd2(i, 2, 182) * 128);
      for (int t = 0; t < 60; t++) { p.at(x + (int)(std::cos(a) * t), y + (int)(std::sin(a) * t)) = { 235, 250, 255, 255 }; a += (rnd2(i, t, 183) - 0.5f) * 0.3f; } }
  });
  make(MAT_SNOW, "fy_iceworld/snow", SURFACE_SAND, 192, [](Pix& p) { noiseFill(p, { 236, 240, 246, 255 }, 0.12f, 4, 191, 0.05f); });
  make(MAT_GRASS, "nature/grass", SURFACE_DIRT, 128, [](Pix& p) { noiseFill(p, { 90, 120, 60, 255 }, 0.4f, 4, 201, 0.25f); });
  make(MAT_BARREL, "props/barrel", SURFACE_METAL, 64, [](Pix& p) {
    noiseFill(p, { 60, 92, 140, 255 }, 0.2f, 4, 211, 0.06f);
    for (int y = 0; y < 128; y++) for (int x = 0; x < 128; x++) { if ((y > 20 && y < 26) || (y > 100 && y < 106)) p.at(x, y) = mul(p.at(x, y), 0.6f); }
  });
  make(MAT_STONE_DARK, "stone/stone_dark", SURFACE_CONCRETE, 128, [](Pix& p) {
    noiseFill(p, { 110, 100, 88, 255 }, 0.25f, 4, 221, 0.1f); blocks(p, 48, 24, { 70, 62, 54, 255 }, 1.5f, 0.25f, 222, 3.f);
  });

  // sprites ---------------------------------------------------------------
  {
    Pix px(4, 4); for (auto& c : px.p) c = WHITE; white = upload(px, false);
  }
  auto sprite = [&](int size, auto&& fn) {
    Pix px(size, size);
    for (int y = 0; y < size; y++) for (int x = 0; x < size; x++) { float u = (x + 0.5f) / size * 2 - 1, v = (y + 0.5f) / size * 2 - 1; px.p[y * size + x] = fn(u, v, x, y); }
    Texture2D t = upload(px, false); SetTextureWrap(t, TEXTURE_WRAP_CLAMP); return t;
  };
  smoke = sprite(64, [](float u, float v, int x, int y) {
    float r = std::sqrt(u * u + v * v); float n = fbm(x / 10.f, y / 10.f, 6, 300);
    float a = Clamp(1.f - r, 0.f, 1.f); a = a * a * (0.55f + n * 0.9f);
    return Color{ 255, 255, 255, c8(a * 255) };
  });
  spark = sprite(16, [](float u, float v, int, int) { float r = std::sqrt(u * u + v * v); float a = Clamp(1.f - r, 0.f, 1.f); return Color{ 255, 230, 160, c8(a * a * 255) }; });
  flare = sprite(64, [](float u, float v, int, int) {
    float r = std::sqrt(u * u + v * v); float a = Clamp(1.f - r, 0.f, 1.f);
    float star = std::max(0.f, 1.f - std::fabs(u) * 8) * std::max(0.f, 1.f - std::fabs(v)) + std::max(0.f, 1.f - std::fabs(v) * 8) * std::max(0.f, 1.f - std::fabs(u));
    float k = Clamp(a * a * a + star * 0.6f, 0.f, 1.f);
    return Color{ 255, (uint8_t)(200 + 55 * a), (uint8_t)(120 + 100 * a), c8(k * 255) };
  });
  decalHole = sprite(16, [](float u, float v, int, int) {
    float r = std::sqrt(u * u + v * v);
    if (r < 0.35f) return Color{ 15, 12, 10, 255 };
    if (r < 0.9f) return Color{ 40, 34, 28, c8((0.9f - r) / 0.55f * 200) };
    return Color{ 0, 0, 0, 0 };
  });
  glowDot = sprite(32, [](float u, float v, int, int) { float r = std::sqrt(u * u + v * v); float a = Clamp(1.f - r, 0.f, 1.f); return Color{ 255, 255, 255, c8(a * a * 255) }; });
  cloud = sprite(128, [](float u, float v, int x, int y) {
    float n = fbm(x / 128.f * 4, y / 128.f * 4, 4, 400, 5);
    float a = Clamp((n - 0.45f) * 3.f, 0.f, 1.f);
    return Color{ 255, 255, 255, c8(a * 255) };
  });
  SetTextureWrap(cloud, TEXTURE_WRAP_REPEAT);
  initialized = true;
}
void CMaterialSystem::Shutdown() {
  if (!initialized) return;
  for (auto& m : m_mats) if (m.tex.id) UnloadTexture(m.tex);
  for (Texture2D* t : { &white, &smoke, &spark, &flare, &decalHole, &glowDot, &cloud }) if (t->id) UnloadTexture(*t);
  initialized = false;
}
int CMaterialSystem::FindByName(const std::string& n) const {
  for (int i = 1; i < MAT_COUNT; i++) if (m_mats[i].name && n == m_mats[i].name) return i;
  return MAT_DEV_GRAY;
}
