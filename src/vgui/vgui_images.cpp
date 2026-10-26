// vgui_images.cpp - see vgui_images.hpp
#include "vgui_images.hpp"
#include "../engine/srcfs.hpp"
#include "../engine/srcmaterial.hpp"
#include "../game/shared/weapon_parse.hpp"
#include <algorithm>
#include <cstring>
#include <map>
#include <vector>

static std::map<std::string, Texture2D> s_images;

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

const Texture2D* VGUI_ItemImage(const char* name) { return VGUI_Image(std::string("materials/vgui/gfx/vgui/") + name + ".vtf"); }

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
    default: return nullptr;
  }
  return n;
}

const Texture2D* VGUI_WeaponImage(int id) {
  const char* n = WeaponImageName(id);
  return n ? LoadImageEx(std::string("materials/vgui/gfx/vgui/") + n + ".vtf", 1) : nullptr;
}

const Texture2D* VGUI_WeaponIcon(int id) {
  const char* n = WeaponImageName(id);
  return n ? LoadImageEx(std::string("materials/vgui/gfx/vgui/") + n + ".vtf", 3) : nullptr;
}

void VGUI_DrawImageFit(const Texture2D& t, float x, float y, float w, float h, Color tint) {
  float s = std::min(w / t.width, h / t.height);
  float dw = t.width * s, dh = t.height * s;
  DrawTexturePro(t, { 0, 0, (float)t.width, (float)t.height }, { x + (w - dw) * 0.5f, y + (h - dh) * 0.5f, dw, dh }, { 0, 0 }, 0, tint);
}

void VGUI_ClearImages() { for (auto& kv : s_images) if (kv.second.id) UnloadTexture(kv.second); s_images.clear(); }
