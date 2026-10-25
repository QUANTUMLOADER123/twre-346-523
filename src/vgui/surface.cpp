// surface.cpp - ISurface implementation on raylib
#include "surface.hpp"
#include "rlgl.h"
#include <vector>
#include <cmath>
#include <algorithm>

namespace vgui {
static ISurface g_Surface;
ISurface* surface() { return &g_Surface; }

static Font LoadUIFont(const char* path, int size) {
  // ASCII + Latin-1 + Cyrillic + a few symbols (arrows, bullets)
  std::vector<int> cps;
  for (int c = 32; c < 127; c++) cps.push_back(c);
  for (int c = 0xA0; c <= 0xFF; c++) cps.push_back(c);
  for (int c = 0x400; c <= 0x45F; c++) cps.push_back(c);
  for (int c : { 0x2022, 0x2190, 0x2191, 0x2192, 0x2193, 0x25B2, 0x25BC, 0x25C0, 0x25B6, 0x2713, 0x2715, 0x2605, 0x2014, 0x2026 }) cps.push_back(c);
  Font f = LoadFontEx(path, size, cps.data(), (int)cps.size());
  if (f.texture.id == 0) f = GetFontDefault();
  else SetTextureFilter(f.texture, TEXTURE_FILTER_BILINEAR);
  return f;
}

void ISurface::Init() {
  const char* sans = "assets/fonts/DejaVuSans.ttf";
  const char* bold = "assets/fonts/DejaVuSans-Bold.ttf";
  const char* mono = "assets/fonts/DejaVuSansMono.ttf";
  struct Def { int id; const char* path; int size; float spacing; };
  Def defs[] = {
    { FONT_UI, sans, 13, 0 }, { FONT_DEFAULT_BOLD, bold, 13, 0 }, { FONT_MENU_TITLE, sans, 20, 0 }, { FONT_LOGO, bold, 64, 2 },
    { FONT_HUD_NUMBERS, bold, 40, 1 }, { FONT_HUD_SMALL, bold, 15, 0 }, { FONT_MONO, mono, 13, 0 }, { FONT_CHEAT, sans, 12, 0 },
    { FONT_CHEAT_BOLD, bold, 12, 0 }, { FONT_ESP, sans, 11, 0 }, { FONT_ICONS_SMALL, bold, 10, 0 }, { FONT_CHEAT_INDICATOR, bold, 24, 0 },
  };
  for (auto& d : defs) { m_fonts[d.id].font = LoadUIFont(d.path, d.size); m_fonts[d.id].size = (float)d.size; m_fonts[d.id].spacing = d.spacing; }
}
void ISurface::Shutdown() { for (auto& f : m_fonts) if (f.font.texture.id && f.font.texture.id != GetFontDefault().texture.id) UnloadFont(f.font); }

Vector2 ISurface::GetTextSize(int font, const char* text) const { const HFont& f = m_fonts[font]; return MeasureTextEx(f.font, text, f.size, f.spacing); }
void ISurface::DrawText(int font, const char* text, float x, float y, Color c) const { const HFont& f = m_fonts[font]; DrawTextEx(f.font, text, { std::floor(x), std::floor(y) }, f.size, f.spacing, c); }
void ISurface::DrawTextShadow(int font, const char* text, float x, float y, Color c, Color s) const { s.a = (unsigned char)(s.a * c.a / 255); DrawText(font, text, x + 1, y + 1, s); DrawText(font, text, x, y, c); }
void ISurface::DrawTextOutline(int font, const char* text, float x, float y, Color c, Color o) const {
  o.a = (unsigned char)(o.a * c.a / 255);
  for (int dx = -1; dx <= 1; dx++) for (int dy = -1; dy <= 1; dy++) if (dx || dy) DrawText(font, text, x + dx, y + dy, o);
  DrawText(font, text, x, y, c);
}
void ISurface::DrawTextCentered(int font, const char* text, float cx, float y, Color c, bool shadow) const {
  float w = GetTextSize(font, text).x; if (shadow) DrawTextShadow(font, text, cx - w / 2, y, c); else DrawText(font, text, cx - w / 2, y, c);
}
void ISurface::DrawTextRight(int font, const char* text, float rx, float y, Color c, bool shadow) const {
  float w = GetTextSize(font, text).x; if (shadow) DrawTextShadow(font, text, rx - w, y, c); else DrawText(font, text, rx - w, y, c);
}
void ISurface::DrawFilledRect(float x, float y, float w, float h, Color c) const { if (w > 0 && h > 0) DrawRectangleRec({ x, y, w, h }, c); }
void ISurface::DrawOutlinedRect(float x, float y, float w, float h, Color c) const {
  DrawFilledRect(x, y, w, 1, c); DrawFilledRect(x, y + h - 1, w, 1, c); DrawFilledRect(x, y + 1, 1, h - 2, c); DrawFilledRect(x + w - 1, y + 1, 1, h - 2, c);
}
void ISurface::DrawLine(float x0, float y0, float x1, float y1, Color c, float t) const { if (t <= 1.f) ::DrawLineV({ x0, y0 }, { x1, y1 }, c); else DrawLineEx({ x0, y0 }, { x1, y1 }, t, c); }
void ISurface::DrawGradientV(float x, float y, float w, float h, Color t, Color b) const { DrawRectangleGradientV((int)x, (int)y, (int)w, (int)h, t, b); }
void ISurface::DrawGradientH(float x, float y, float w, float h, Color l, Color r) const { DrawRectangleGradientH((int)x, (int)y, (int)w, (int)h, l, r); }
void ISurface::DrawCircle(float x, float y, float r, Color c, int segs) const { DrawCircleLinesV({ x, y }, r, c); (void)segs; }
void ISurface::DrawFilledCircle(float x, float y, float r, Color c) const { DrawCircleV({ x, y }, r, c); }
void ISurface::DrawTriangle(Vector2 a, Vector2 b, Vector2 c, Color col) const {
  // raylib wants counter-clockwise; draw both windings
  float cross = (b.x - a.x) * (c.y - a.y) - (b.y - a.y) * (c.x - a.x);
  if (cross < 0) ::DrawTriangle(a, b, c, col); else ::DrawTriangle(a, c, b, col);
}
void ISurface::DrawBevel(float x, float y, float w, float h, Color light, Color dark, bool sunken) const {
  Color tl = sunken ? dark : light, br = sunken ? light : dark;
  DrawFilledRect(x, y, w, 1, tl); DrawFilledRect(x, y, 1, h, tl);
  DrawFilledRect(x, y + h - 1, w, 1, br); DrawFilledRect(x + w - 1, y, 1, h, br);
}
void ISurface::PushClip(int x, int y, int w, int h) {
  if (m_clipDepth > 0) { // intersect
    int* p = m_clipStack[m_clipDepth - 1];
    int x1 = std::max(x, p[0]), y1 = std::max(y, p[1]), x2 = std::min(x + w, p[0] + p[2]), y2 = std::min(y + h, p[1] + p[3]);
    x = x1; y = y1; w = std::max(0, x2 - x1); h = std::max(0, y2 - y1);
  }
  if (m_clipDepth < 16) { int* c = m_clipStack[m_clipDepth++]; c[0] = x; c[1] = y; c[2] = w; c[3] = h; }
  BeginScissorMode(x, y, w, h);
}
void ISurface::PopClip() {
  if (m_clipDepth > 0) m_clipDepth--;
  EndScissorMode();
  if (m_clipDepth > 0) { int* c = m_clipStack[m_clipDepth - 1]; BeginScissorMode(c[0], c[1], c[2], c[3]); }
}
float ISurface::Scale() const { return std::max(0.75f, GetScreenHeight() / 768.f); }
} // namespace vgui
