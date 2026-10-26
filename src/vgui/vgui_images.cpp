// vgui_images.cpp - see vgui_images.hpp
#include "vgui_images.hpp"
#include "../engine/srcfs.hpp"
#include "../engine/srcmaterial.hpp"
#include "../game/shared/weapon_parse.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>
#include <cstdio>
#include <cmath>
#include "../engine/convar.hpp"
#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#include "../../third_party/nanosvg/nanosvg.h"
#include "../../third_party/nanosvg/nanosvgrast.h"

static std::map<std::string, Texture2D> s_images;
ConVar cl_csgo_icons("cl_csgo_icons", "1", FCVAR_ARCHIVE, "Use the downloaded CS:GO weapon icons (SVG) in the buy menu, HUD and kill feed");

// CS:GO icon (downloaded extras, content/extras/csgo_icons/svg_normal/<name>.svg): rasterized with nanosvg at `heightPx`,
// cropped to the drawn part, white with the coverage as alpha (tinted when drawn)
static const Texture2D* LoadSvgIcon(const std::string& name, int heightPx = 160) {
  if (!cl_csgo_icons.GetBool()) return nullptr;
  std::string key = "svg:" + name + "#" + std::to_string(heightPx);
  auto it = s_images.find(key);
  if (it != s_images.end()) return it->second.id ? &it->second : nullptr;
  Texture2D tex{};
  std::vector<uint8_t> file;
  if (srcfs::Read("svg_normal/" + name + ".svg", file) && !file.empty()) {
    file.push_back(0);
    if (NSVGimage* img = nsvgParse(reinterpret_cast<char*>(file.data()), "px", 96.f)) {
      if (img->width > 0 && img->height > 0) {
        float scale = heightPx / img->height;
        int w = std::max(1, (int)std::ceil(img->width * scale)), h = std::max(1, (int)std::ceil(img->height * scale));
        std::vector<uint8_t> rgba((size_t)w * h * 4, 0);
        if (NSVGrasterizer* r = nsvgCreateRasterizer()) { nsvgRasterize(r, img, 0, 0, scale, rgba.data(), w, h, w * 4); nsvgDeleteRasterizer(r); }
        int x0 = w, y0 = h, x1 = -1, y1 = -1;
        for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) if (rgba[((size_t)y * w + x) * 4 + 3] > 8) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
        if (x1 >= x0) {
          x0 = std::max(0, x0 - 2); y0 = std::max(0, y0 - 2); x1 = std::min(w - 1, x1 + 2); y1 = std::min(h - 1, y1 + 2);
          int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
          std::vector<uint8_t> out((size_t)cw * ch * 4);
          for (int y = 0; y < ch; y++) for (int x = 0; x < cw; x++) {
            const uint8_t* s = &rgba[((size_t)(y + y0) * w + (x + x0)) * 4]; uint8_t* d = &out[((size_t)y * cw + x) * 4];
            d[0] = d[1] = d[2] = 255; d[3] = s[3];
          }
          Image im{ out.data(), cw, ch, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
          tex = LoadTextureFromImage(im);
          GenTextureMipmaps(&tex);
          SetTextureFilter(tex, TEXTURE_FILTER_TRILINEAR);
        }
      }
      nsvgDelete(img);
    }
  }
  it = s_images.emplace(key, tex).first;
  return it->second.id ? &it->second : nullptr;
}
static std::string SvgWeaponName(int id) {
  switch (id) {
    case WEAPON_MP5SD: return "weapon_mp7";           // not in the icon set
    case WEAPON_KNIFE: return "weapon_knife";
    default: { const char* a = WeaponIdToAlias(id); return a && *a ? std::string("weapon_") + a : std::string(); }
  }
}
const Texture2D* VGUI_SvgIcon(const char* name) { return LoadSvgIcon(name); }

// flags: 1 = crop to the opaque part, 2 = white silhouette (kill feed / HUD icons, tinted when drawn)
static const Texture2D* LoadImageEx(const std::string& vtf, int flags) {
  std::string key = vtf + "#" + std::to_string(flags);
  auto it = s_images.find(key);
  if (it == s_images.end()) {
    Texture2D tex{};
    std::vector<uint8_t> rgba; int w = 0, h = 0;
    if (srcfs::Available() && VTF_Load(vtf, rgba, w, h, 512)) {
      int x0 = 0, y0 = 0, x1 = w - 1, y1 = h - 1;
      if (flags & 1) {
        x0 = w; y0 = h; x1 = -1; y1 = -1;
        for (int y = 0; y < h; y++)
          for (int x = 0; x < w; x++)
            if (rgba[(y * w + x) * 4 + 3] > 24) { x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y); }
        if (x1 < x0) { x0 = 0; y0 = 0; x1 = w - 1; y1 = h - 1; }
        x0 = std::max(0, x0 - 1); y0 = std::max(0, y0 - 1); x1 = std::min(w - 1, x1 + 1); y1 = std::min(h - 1, y1 + 1);
      }
      int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
      std::vector<uint8_t> out((size_t)cw * ch * 4);
      for (int y = 0; y < ch; y++)
        for (int x = 0; x < cw; x++) {
          const uint8_t* src = &rgba[((y + y0) * w + (x + x0)) * 4];
          uint8_t* d = &out[(y * cw + x) * 4];
          if (flags & 2) { d[0] = d[1] = d[2] = 255; d[3] = src[3] > 24 ? src[3] : 0; }
          else memcpy(d, src, 4);
        }
      Image img{ out.data(), cw, ch, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
      tex = LoadTextureFromImage(img);
      SetTextureFilter(tex, TEXTURE_FILTER_BILINEAR);
    }
    it = s_images.emplace(key, tex).first;
  }
  return it->second.id ? &it->second : nullptr;
}

const Texture2D* VGUI_Image(const std::string& vtf) { return LoadImageEx(vtf, 0); }

const Texture2D* VGUI_ItemImage(const char* name) {
  const char* svg = !strcmp(name, "kevlar") ? "item_kevlar" : !strcmp(name, "kevlar_helmet") ? "item_assaultsuit" : !strcmp(name, "defuser") ? "item_defuser" : nullptr;
  if (svg) if (const Texture2D* t = LoadSvgIcon(svg)) return t;
  return VGUI_Image(std::string("materials/vgui/gfx/vgui/") + name + ".vtf");
}

static const char* WeaponImageName(int id) {
  const char* n = nullptr;
  switch (id) {
    case WEAPON_GLOCK: n = "glock18"; break;
    case WEAPON_USP_SILENCER: n = "usp45"; break;
    case WEAPON_P250: n = "p228"; break;
    case WEAPON_DEAGLE: case WEAPON_REVOLVER: n = "deserteagle"; break;
    case WEAPON_MAC10: n = "mac10"; break;
    case WEAPON_MP9: n = "tmp"; break;
    case WEAPON_UMP45: n = "ump45"; break;
    case WEAPON_P90: n = "p90"; break;
    case WEAPON_NOVA: n = "m3"; break;
    case WEAPON_XM1014: n = "xm1014"; break;
    case WEAPON_GALILAR: n = "galil"; break;
    case WEAPON_FAMAS: n = "famas"; break;
    case WEAPON_AK47: n = "ak47"; break;
    case WEAPON_M4A1: case WEAPON_M4A1_SILENCER: n = "m4a1"; break;
    case WEAPON_SG556: n = "sg552"; break;
    case WEAPON_AUG: n = "aug"; break;
    case WEAPON_SSG08: n = "scout"; break;
    case WEAPON_AWP: n = "awp"; break;
    case WEAPON_G3SG1: n = "g3sg1"; break;
    case WEAPON_SCAR20: n = "sg550"; break;
    case WEAPON_HEGRENADE: n = "hegrenade"; break;
    case WEAPON_FLASHBANG: n = "flashbang"; break;
    case WEAPON_SMOKEGRENADE: n = "smokegrenade"; break;
    case WEAPON_MOLOTOV: case WEAPON_INCGRENADE: n = "hegrenade"; break;
    case WEAPON_ELITE: n = "elites"; break;
    case WEAPON_FIVESEVEN: n = "fiveseven"; break;
    case WEAPON_TEC9: n = "glock18"; break;
    case WEAPON_CZ75A: n = "p228"; break;
    case WEAPON_HKP2000: n = "usp45"; break;
    case WEAPON_MP7: case WEAPON_MP5SD: n = "mp5"; break;
    case WEAPON_BIZON: n = "ump45"; break;
    case WEAPON_MAG7: n = "xm1014"; break;
    case WEAPON_SAWEDOFF: n = "m3"; break;
    case WEAPON_M249: case WEAPON_NEGEV: n = "m249"; break;
    default: return nullptr;
  }
  return n;
}

const Texture2D* VGUI_WeaponImage(int id) {
  if (const Texture2D* t = LoadSvgIcon(SvgWeaponName(id))) return t;
  const char* n = WeaponImageName(id);
  return n ? LoadImageEx(std::string("materials/vgui/gfx/vgui/") + n + ".vtf", 1) : nullptr;
}

const Texture2D* VGUI_WeaponIcon(int id) {
  if (const Texture2D* t = LoadSvgIcon(SvgWeaponName(id))) return t;
  const char* n = WeaponImageName(id);
  return n ? LoadImageEx(std::string("materials/vgui/gfx/vgui/") + n + ".vtf", 3) : nullptr;
}

void VGUI_DrawImageFit(const Texture2D& t, float x, float y, float w, float h, Color tint) {
  float s = std::min(w / t.width, h / t.height);
  float dw = t.width * s, dh = t.height * s;
  DrawTexturePro(t, { 0, 0, (float)t.width, (float)t.height }, { x + (w - dw) * 0.5f, y + (h - dh) * 0.5f, dw, dh }, { 0, 0 }, 0, tint);
}

void VGUI_ClearImages() { for (auto& kv : s_images) if (kv.second.id) UnloadTexture(kv.second); s_images.clear(); }
