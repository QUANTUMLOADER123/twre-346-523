// dds.cpp - DirectDraw Surface reader. Only what the Source overview radars use:
// DXT1 (BC1), DXT3 (BC2), DXT5 (BC3) and plain 24/32 bit RGB(A).
#include "dds.hpp"
#include "srcfs.hpp"
#include <cstring>
#include <algorithm>

namespace dds {
namespace {
inline uint16_t Rd16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }
inline uint32_t Rd32(const uint8_t* p) { return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24); }

void Unpack565(uint16_t c, uint8_t* out) {
  int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
  out[0] = (uint8_t)((r << 3) | (r >> 2));
  out[1] = (uint8_t)((g << 2) | (g >> 4));
  out[2] = (uint8_t)((b << 3) | (b >> 2));
}

// one 4x4 block -> 16 RGBA pixels (DXT1 with optional 1 bit alpha, DXT2/3 explicit alpha, DXT4/5 interpolated)
void DecodeBlock(const uint8_t* src, int bw, int bh, uint8_t* dst, int dstStride, int blockBytes, int mode) {
  uint8_t pal[4][4];
  uint8_t& a0 = pal[3][3]; (void)a0;
  uint8_t alpha[16]; std::memset(alpha, 255, sizeof(alpha));
  const uint8_t* colors = src;
  if (mode == 3) {   // DXT3: 8 bytes of 4 bit alpha, then the colour block
    for (int i = 0; i < 16; i++) {
      uint8_t v = src[i >> 1];
      int a = (i & 1) ? (v >> 4) : (v & 15);
      alpha[i] = (uint8_t)(a * 17);
    }
    colors = src + 8;
  } else if (mode == 5) {   // DXT5: two alpha endpoints + 3 bit indices
    uint8_t al[8] = { src[0], src[1] };
    if (al[0] > al[1]) { for (int i = 1; i <= 6; i++) al[1 + i] = (uint8_t)(((7 - i) * al[0] + i * al[1]) / 7); }
    else { for (int i = 1; i <= 4; i++) al[1 + i] = (uint8_t)(((5 - i) * al[0] + i * al[1]) / 5); }
    al[6] = 0; al[7] = 255;
    uint64_t bits = 0;
    for (int i = 0; i < 6; i++) bits |= (uint64_t)src[2 + i] << (8 * i);
    for (int i = 0; i < 16; i++) alpha[i] = al[(bits >> (3 * i)) & 7];
    colors = src + 8;
  }
  uint16_t c0 = Rd16(colors), c1 = Rd16(colors + 2);
  bool hasAlpha = mode == 1 && c0 <= c1;
  Unpack565(c0, pal[0]); Unpack565(c1, pal[1]);
  if (!hasAlpha) {
    for (int k = 0; k < 3; k++) pal[2][k] = (uint8_t)((2 * pal[0][k] + pal[1][k]) / 3);
    for (int k = 0; k < 3; k++) pal[3][k] = (uint8_t)((pal[0][k] + 2 * pal[1][k]) / 3);
    pal[2][3] = pal[3][3] = 255;
  } else {
    for (int k = 0; k < 3; k++) pal[2][k] = (uint8_t)((pal[0][k] + pal[1][k]) / 2);
    pal[3][0] = pal[3][1] = pal[3][2] = 0; pal[3][3] = 0;
    pal[2][3] = 255;
  }
  uint32_t idx = Rd32(colors + 4);
  for (int y = 0; y < 4; y++) for (int x = 0; x < 4; x++) {
    if (x >= bw || y >= bh) continue;
    int i = y * 4 + x, p = (idx >> (2 * i)) & 3;
    uint8_t* o = dst + (size_t)y * dstStride + (size_t)x * 4;
    o[0] = pal[p][0]; o[1] = pal[p][1]; o[2] = pal[p][2];
    o[3] = (uint8_t)std::min<int>(pal[p][3], alpha[i]);
  }
  (void)blockBytes;
}
} // namespace

bool Decode(const uint8_t* d, size_t size, std::vector<uint8_t>& rgba, int& w, int& h, bool blackIsTransparent) {
  rgba.clear(); w = h = 0;
  if (size < 128 || std::memcmp(d, "DDS ", 4) != 0) return false;
  uint32_t headerSize = Rd32(d + 4), height = Rd32(d + 12), width = Rd32(d + 16);
  uint32_t pfSize = Rd32(d + 76), fourCC = Rd32(d + 84), bitCount = Rd32(d + 88),
           rm = Rd32(d + 92), gm = Rd32(d + 96), bm = Rd32(d + 100), am = Rd32(d + 104);
  if (headerSize != 124 || width == 0 || height == 0 || width > 8192 || height > 8192) return false;
  size_t off = 4 + headerSize;
  if (fourCC == 0x30315844 /*DX10*/) off += 20;
  if (off >= size) return false;
  const uint8_t* src = d + off; size_t srcLen = size - off;
  w = (int)width; h = (int)height;
  rgba.resize((size_t)w * h * 4);

  // bit masks -> channel shift/width (uncompressed formats)
  auto chan = [](uint32_t mask, int& shift, int& bits) { shift = 0; bits = 0; if (!mask) return; while (!(mask & 1)) { mask >>= 1; shift++; } while (mask & 1) { mask >>= 1; bits++; } };
  int rs, rb, gs, gb, bs, bb, as, ab; chan(rm, rs, rb); chan(gm, gs, gb); chan(bm, bs, bb); chan(am, as, ab);

  int mode = 0;   // 0 = raw, 1/3/5 = DXT1/3/5
  if (fourCC == 0x31545844 /*DXT1*/) mode = 1;
  else if (fourCC == 0x33545844 /*DXT3*/) mode = 3;
  else if (fourCC == 0x35545844 /*DXT5*/) mode = 5;
  else if (fourCC != 0) return false;   // ATI2/BC7 etc. - not used by overviews

  if (mode) {
    int bpp = mode == 1 ? 8 : 16;
    size_t need = (size_t)((w + 3) / 4) * ((h + 3) / 4) * bpp;
    if (srcLen < need) { rgba.clear(); return false; }
    const uint8_t* p = src;
    for (int by = 0; by < h; by += 4) for (int bx = 0; bx < w; bx += 4) {
      int bw = std::min(4, w - bx), bh = std::min(4, h - by);
      DecodeBlock(p, bw, bh, rgba.data() + ((size_t)by * w + bx) * 4, w * 4, bpp, mode);
      p += bpp;
    }
  } else {
    if (pfSize != 32 || bitCount < 24) { rgba.clear(); return false; }
    size_t stride = (size_t)bitCount / 8;
    if (srcLen < stride * (size_t)w * h) { rgba.clear(); return false; }
    auto pick = [](uint32_t v, int shift, int bits) -> uint8_t {
      if (!bits) return 255;
      uint32_t m = (bits >= 32) ? 0xffffffffu : ((1u << bits) - 1);
      uint32_t c = (v >> shift) & m;
      if (bits >= 8) return (uint8_t)(c >> (bits - 8));
      return (uint8_t)((c * 255) / m);
    };
    for (int y = 0; y < h; y++) {
      const uint8_t* row = src + (size_t)y * w * stride;
      for (int x = 0; x < w; x++) {
        uint32_t v = 0; for (size_t k = 0; k < stride; k++) v |= (uint32_t)row[x * stride + k] << (8 * k);
        uint8_t* o = &rgba[((size_t)y * w + x) * 4];
        o[0] = pick(v, rs, rb); o[1] = pick(v, gs, gb); o[2] = pick(v, bs, bb); o[3] = am ? pick(v, as, ab) : 255;
      }
    }
  }
  if (blackIsTransparent) {
    for (size_t i = 0; i < rgba.size(); i += 4)
      if (!rgba[i] && !rgba[i + 1] && !rgba[i + 2]) rgba[i + 3] = 0;
  }
  return true;
}

bool Load(const std::string& rel, std::vector<uint8_t>& rgba, int& w, int& h, bool blackIsTransparent) {
  std::vector<uint8_t> file;
  if (!srcfs::Read(rel, file) || file.empty()) return false;
  return Decode(file.data(), file.size(), rgba, w, h, blackIsTransparent);
}
} // namespace dds
