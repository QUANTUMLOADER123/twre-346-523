// surface.hpp - ISurface: fonts and 2D drawing primitives (vgui::surface())
#pragma once
#include "raylib.h"
#include <string>

enum FontId {
  FONT_UI = 0,     // Tahoma-ish UI font (VGUI "DefaultSmall")
  FONT_DEFAULT_BOLD,
  FONT_MENU_TITLE,      // main menu items
  FONT_LOGO,            // big title
  FONT_HUD_NUMBERS,     // big HUD digits
  FONT_HUD_SMALL,
  FONT_MONO,            // console
  FONT_CHEAT,           // cheat menu (small verdana-like)
  FONT_CHEAT_BOLD,
  FONT_ESP,             // esp names
  FONT_ICONS_SMALL,
  FONT_CHEAT_INDICATOR, // big bold indicators (DT / DUCK / FS)
  FONT_COUNT
};

namespace vgui {
struct HFont { Font font; float size; float spacing; };

class ISurface {
public:
  void Init();
  void Shutdown();
  const HFont& GetFont(int id) const { return m_fonts[id]; }
  // text
  Vector2 GetTextSize(int font, const char* text) const;
  int GetTextWide(int font, const char* text) const { return (int)GetTextSize(font, text).x; }
  int GetFontTall(int font) const { return (int)m_fonts[font].size; }
  void DrawText(int font, const char* text, float x, float y, Color c) const;
  void DrawTextShadow(int font, const char* text, float x, float y, Color c, Color shadow = { 0, 0, 0, 200 }) const;
  void DrawTextOutline(int font, const char* text, float x, float y, Color c, Color outline = { 0, 0, 0, 220 }) const;
  void DrawTextCentered(int font, const char* text, float cx, float y, Color c, bool shadow = false) const;
  void DrawTextRight(int font, const char* text, float rx, float y, Color c, bool shadow = false) const;
  // shapes
  void DrawFilledRect(float x, float y, float w, float h, Color c) const;
  void DrawOutlinedRect(float x, float y, float w, float h, Color c) const;
  void DrawLine(float x0, float y0, float x1, float y1, Color c, float thick = 1.f) const;
  void DrawGradientV(float x, float y, float w, float h, Color top, Color bottom) const;
  void DrawGradientH(float x, float y, float w, float h, Color left, Color right) const;
  void DrawCircle(float x, float y, float r, Color c, int segs = 32) const;
  void DrawFilledCircle(float x, float y, float r, Color c) const;
  void DrawTriangle(Vector2 a, Vector2 b, Vector2 c, Color col) const;
  // 3D-ish bevel (VGUI2 steam style) - raised or sunken border
  void DrawBevel(float x, float y, float w, float h, Color light, Color dark, bool sunken = false) const;
  // scissor
  void PushClip(int x, int y, int w, int h);
  void PopClip();
  int ScreenWide() const { return GetScreenWidth(); }
  int ScreenTall() const { return GetScreenHeight(); }
  float Scale() const; // hud scale relative to 768p
private:
  HFont m_fonts[FONT_COUNT];
  int m_clipStack[16][4]; int m_clipDepth = 0;
};
ISurface* surface();
} // namespace vgui

// colors used by the steam olive VGUI2 scheme (resource/SourceScheme.res of CS 1.6 / Steam 2004)
namespace scheme {
constexpr Color Bg = { 76, 88, 68, 255 };           // 4C5844 window background
constexpr Color BgDark = { 62, 70, 55, 255 };        // 3E4637 list / text entry background
constexpr Color Border = { 40, 46, 34, 255 };        // 282E22
constexpr Color BorderLight = { 136, 145, 128, 255 };// 889180
constexpr Color BorderDark = { 40, 46, 34, 255 };
constexpr Color Text = { 222, 225, 214, 255 };       // DEE1D6? (white-ish)
constexpr Color TextDim = { 160, 170, 149, 255 };    // A0AA95
constexpr Color TextSelected = { 196, 181, 80, 255 }; // C4B550 selected / highlight
constexpr Color ButtonBg = { 76, 88, 68, 255 };
constexpr Color ButtonBgArmed = { 90, 106, 80, 255 };
constexpr Color ButtonBgDepressed = { 60, 70, 54, 255 };
constexpr Color Selection = { 149, 136, 49, 255 };   // 958831 list selection
constexpr Color TitleBar = { 76, 88, 68, 255 };
constexpr Color TitleBarInactive = { 70, 80, 63, 255 };
constexpr Color Disabled = { 117, 128, 111, 255 };
constexpr Color HudOrange = { 255, 176, 0, 255 };    // CS 1.6 HUD
constexpr Color HudRed = { 255, 40, 20, 255 };
}
