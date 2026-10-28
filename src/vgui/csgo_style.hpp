// csgo_style.hpp - colours / helpers of the CS:GO (Panorama) style UI: main menu, settings, buy wheel, loading screen
#pragma once
#include "surface.hpp"

namespace csgo {
constexpr Color Bg = { 18, 20, 24, 255 };
constexpr Color Panel = { 28, 31, 37, 236 };
constexpr Color PanelLight = { 44, 48, 56, 240 };
constexpr Color PanelHover = { 58, 64, 74, 245 };
constexpr Color Line = { 70, 76, 88, 255 };
constexpr Color Text = { 232, 234, 238, 255 };
constexpr Color TextDim = { 150, 156, 166, 255 };
constexpr Color TextFaint = { 100, 106, 116, 255 };
constexpr Color Accent = { 94, 152, 217, 255 };      // blue highlight
constexpr Color Green = { 98, 170, 73, 255 };        // "GO" buttons
constexpr Color GreenHover = { 118, 196, 90, 255 };
constexpr Color Red = { 214, 72, 66, 255 };
constexpr Color Gold = { 222, 182, 84, 255 };
constexpr Color TopBar = { 12, 14, 17, 235 };
inline Color Alpha(Color c, float a) { c.a = (unsigned char)(c.a * (a < 0 ? 0 : a > 1 ? 1 : a)); return c; }
inline bool Hover(float x, float y, float w, float h) { Vector2 m = GetMousePosition(); return m.x >= x && m.y >= y && m.x < x + w && m.y < y + h; }
}
