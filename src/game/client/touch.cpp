// touch.cpp - on-screen controls for phones / tablets (see touch.hpp)
#include "touch.hpp"
#include "cl_main.hpp"
#include "../shared/cs_player.hpp"
#include "../../engine/convar.hpp"
#include "../../vgui/surface.hpp"
#include "../../vgui/controls.hpp"
#include "../../vgui/gameui.hpp"
#include "../../cheat/cheat.hpp"
#include "raylib.h"
#include <cmath>
#include <string>
#include <vector>
#include <algorithm>

#ifdef PLATFORM_ANDROID
#define TOUCH_DEFAULT "1"
#else
#define TOUCH_DEFAULT "0"
#endif
ConVar touch_enable("touch_enable", TOUCH_DEFAULT, FCVAR_ARCHIVE, "On-screen touch controls (stick, look, buttons)");
ConVar touch_sensitivity("touch_sensitivity", "1.0", FCVAR_ARCHIVE, "Touch look speed", true, 0.05f, true, 10.f);
ConVar touch_opacity("touch_opacity", "0.55", FCVAR_ARCHIVE, "Touch buttons opacity", true, 0.05f, true, 1.f);
ConVar touch_scale("touch_scale", "1.0", FCVAR_ARCHIVE, "Touch buttons size", true, 0.5f, true, 2.f);
ConVar touch_invert("touch_invert", "0", FCVAR_ARCHIVE, "Invert touch look pitch");
ConVar touch_mouse("touch_mouse", "0", FCVAR_NONE, "Desktop testing: the left mouse button acts as a finger");

namespace {
enum Action { ACT_HOLD, ACT_PRESS, ACT_ESCAPE, ACT_CHEATMENU };
struct Button {
  const char* label; const char* cmd; Action act; bool aim;   // aim: dragging the finger also turns the view
  bool round; float x, y, r, w, h;                              // computed every frame (pixels)
  int finger = -1; bool down = false;
};
std::vector<Button> s_buttons = {
  { "FIRE",   "+attack",     ACT_HOLD, true,  true },
  { "FIRE",   "+attack",     ACT_HOLD, true,  true },   // second fire button on the left (thumb on the stick side)
  { "JUMP",   "+jump",       ACT_HOLD, false, true },
  { "DUCK",   "+duck",       ACT_HOLD, false, true },
  { "SCOPE",  "+attack2",    ACT_HOLD, false, true },
  { "R",      "+reload",     ACT_HOLD, false, true },
  { "USE",    "+use",        ACT_HOLD, false, true },
  { "NEXT",   "invnext",     ACT_PRESS, false, true },
  { "DROP",   "drop",        ACT_PRESS, false, true },
  { "WALK",   "+speed",      ACT_HOLD, false, true },
  { "MENU",   "",            ACT_ESCAPE, false, false },
  { "BUY",    "buymenu",     ACT_PRESS, false, false },
  { "TEAM",   "chooseteam",  ACT_PRESS, false, false },
  { "SCORE",  "+showscores", ACT_HOLD, false, false },
  { "AW",     "",            ACT_CHEATMENU, false, false },
  { "1", "slot1", ACT_PRESS, false, false }, { "2", "slot2", ACT_PRESS, false, false }, { "3", "slot3", ACT_PRESS, false, false },
  { "4", "slot4", ACT_PRESS, false, false }, { "5", "slot5", ACT_PRESS, false, false },
};
enum { B_FIRE, B_FIRE2, B_JUMP, B_DUCK, B_SCOPE, B_RELOAD, B_USE, B_NEXT, B_DROP, B_WALK, B_MENU, B_BUY, B_TEAM, B_SCORE, B_AW, B_SLOT1 };

struct Finger { int id; Vector2 pos, last; int role; };   // role: -1 stick, -2 look, >= 0 button index
std::vector<Finger> s_fingers;
int s_stickFinger = -1; Vector2 s_stickBase{}, s_stickPos{};
float s_moveF = 0, s_moveS = 0;
bool s_active = false;

float Scale() { return GetScreenHeight() / 1080.f * touch_scale.GetFloat(); }

void Layout() {
  const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight(), s = Scale();
  auto circ = [&](int i, float x, float y, float r) { Button& b = s_buttons[i]; b.round = true; b.x = x; b.y = y; b.r = r * s; };
  auto rect = [&](int i, float x, float y, float w, float h) { Button& b = s_buttons[i]; b.round = false; b.x = x; b.y = y; b.w = w * s; b.h = h * s; };
  circ(B_FIRE, W - 235 * s, H - 330 * s, 108);
  circ(B_FIRE2, 150 * s, H * 0.40f, 72);
  circ(B_JUMP, W - 105 * s, H - 565 * s, 80);
  circ(B_DUCK, W - 495 * s, H - 112 * s, 76);
  circ(B_SCOPE, W - 485 * s, H - 330 * s, 64);
  circ(B_RELOAD, W - 300 * s, H - 610 * s, 60);
  circ(B_USE, W - 690 * s, H - 420 * s, 52);
  circ(B_NEXT, W - 690 * s, H - 112 * s, 58);
  circ(B_DROP, W - 690 * s, H - 270 * s, 48);
  circ(B_WALK, W - 110 * s, H - 790 * s, 50);
  // top bar (centered) and weapon slots (top right)
  const float bw = 128, bh = 62, gap = 10;
  float x0 = W * 0.5f - (5 * bw + 4 * gap) * s * 0.5f;
  for (int i = 0; i < 5; i++) rect(B_MENU + i, x0 + i * (bw + gap) * s, 12 * s, bw, bh);
  for (int i = 0; i < 5; i++) rect(B_SLOT1 + i, W - (5 - i) * 86 * s - 14 * s, 64 * s, 76, 76);
}

bool Hit(const Button& b, Vector2 p, float slack = 1.15f) {
  if (b.round) { float dx = p.x - b.x, dy = p.y - b.y; return dx * dx + dy * dy <= b.r * b.r * slack * slack; }
  return p.x >= b.x && p.x <= b.x + b.w && p.y >= b.y && p.y <= b.y + b.h;
}

void Press(int i) {
  Button& b = s_buttons[i]; if (b.down) return; b.down = true;
  switch (b.act) {
    case ACT_HOLD: Cbuf_AddText(std::string(b.cmd) + " " + std::to_string(2000 + i) + "\n"); break;
    case ACT_PRESS: Cbuf_AddText(std::string(b.cmd) + "\n"); break;
    case ACT_ESCAPE: vgui::InjectKey(KEY_ESCAPE); break;
    case ACT_CHEATMENU: cheat::ToggleMenu(); break;
  }
}
void Release(int i) {
  Button& b = s_buttons[i]; if (!b.down) return; b.down = false; b.finger = -1;
  if (b.act == ACT_HOLD) { std::string c = b.cmd; c[0] = '-'; Cbuf_AddText(c + " " + std::to_string(2000 + i) + "\n"); }
}

void Look(Vector2 d) {
  float s = Scale(); if (s <= 0) return;
  float k = 0.13f * touch_sensitivity.GetFloat() / s;     // degrees per reference pixel
  CCSPlayer* lp = g_pLocalPlayer;
  if (lp && lp->m_iFOV != 0 && lp->m_iFOV < 90) k *= lp->m_iFOV / 90.f;
  cl.viewangles.y -= d.x * k;
  cl.viewangles.x += d.y * k * (touch_invert.GetBool() ? -1.f : 1.f);
  cl.mouseDx += d.x; cl.mouseDy += d.y;
}

bool WantControls() {
  return touch_enable.GetBool() && CL_IsInGame() && cl.keyDest == KEYDEST_GAME && !cl.buyMenuOpen && !cl.teamMenuOpen &&
         !cheat::IsMenuOpen() && !gameui::IsMenuActive() && !gameui::IsConsoleVisible();
}
} // namespace

void Touch_ReleaseAll() {
  for (int i = 0; i < (int)s_buttons.size(); i++) Release(i);
  s_fingers.clear(); s_stickFinger = -1; s_moveF = s_moveS = 0;
}

bool Touch_Active() { return s_active; }
void Touch_GetMove(float& f, float& s) { f = s_active ? s_moveF : 0; s = s_active ? s_moveS : 0; }

void Touch_Frame(float) {
  bool want = WantControls();
  if (!want) { if (s_active) Touch_ReleaseAll(); s_active = false; return; }
  s_active = true;
  Layout();
  const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight(), s = Scale();
  // current touches
  struct T { int id; Vector2 p; };
  std::vector<T> touches;
  for (int i = 0, n = GetTouchPointCount(); i < n; i++) touches.push_back({ GetTouchPointId(i), GetTouchPosition(i) });
  if (touches.empty() && touch_mouse.GetBool() && IsMouseButtonDown(MOUSE_BUTTON_LEFT)) touches.push_back({ 99, GetMousePosition() });
  // ended fingers
  for (size_t k = 0; k < s_fingers.size();) {
    Finger& f = s_fingers[k];
    bool alive = std::any_of(touches.begin(), touches.end(), [&](const T& t) { return t.id == f.id; });
    if (alive) { k++; continue; }
    if (f.role >= 0) Release(f.role);
    if (f.role == -1) { s_stickFinger = -1; s_moveF = s_moveS = 0; }
    s_fingers.erase(s_fingers.begin() + k);
  }
  // new and moved fingers
  for (const T& t : touches) {
    auto it = std::find_if(s_fingers.begin(), s_fingers.end(), [&](const Finger& f) { return f.id == t.id; });
    if (it == s_fingers.end()) {
      Finger f{ t.id, t.p, t.p, -2 };
      int hit = -1;
      for (int i = 0; i < (int)s_buttons.size() && hit < 0; i++) if (!s_buttons[i].down && Hit(s_buttons[i], t.p)) hit = i;
      if (hit >= 0) { f.role = hit; s_buttons[hit].finger = t.id; Press(hit); }
      else if (t.p.x < W * 0.42f && t.p.y > H * 0.2f && s_stickFinger < 0) { f.role = -1; s_stickFinger = t.id; s_stickBase = s_stickPos = t.p; }
      else f.role = -2;
      s_fingers.push_back(f);
      continue;
    }
    Finger& f = *it;
    Vector2 d{ t.p.x - f.last.x, t.p.y - f.last.y };
    f.last = f.pos = t.p;
    if (f.role == -2 || (f.role >= 0 && s_buttons[f.role].aim)) Look(d);
    if (f.role == -1) {
      const float R = 120.f * s;
      float dx = t.p.x - s_stickBase.x, dy = t.p.y - s_stickBase.y, l = std::sqrt(dx * dx + dy * dy);
      if (l > R) { // drag the base along so the stick never gets "stuck" at the edge
        s_stickBase.x = t.p.x - dx / l * R; s_stickBase.y = t.p.y - dy / l * R; dx = dx / l * R; dy = dy / l * R; l = R;
      }
      s_stickPos = t.p;
      float m = l / R; if (m < 0.12f) m = 0; else if (m > 0.85f) m = 1.f;   // dead zone / full speed zone
      s_moveF = l > 0 ? -dy / l * m : 0; s_moveS = l > 0 ? dx / l * m : 0;
    }
  }
}

void Touch_Draw() {
  if (!s_active) return;
  const float a = touch_opacity.GetFloat(), s = Scale();
  auto& sf = *vgui::surface();
  auto col = [&](int r, int g, int b, float alpha) { return Color{ (unsigned char)r, (unsigned char)g, (unsigned char)b, (unsigned char)std::clamp(alpha * 255.f, 0.f, 255.f) }; };
  for (const Button& b : s_buttons) {
    Color fill = b.down ? col(255, 255, 255, a * 0.45f) : col(0, 0, 0, a * 0.45f);
    Color edge = col(255, 255, 255, a * (b.down ? 1.f : 0.7f));
    int font = b.round && b.r > 70 * s ? FONT_DEFAULT_BOLD : FONT_UI;
    float tall = (float)sf.GetFontTall(font);
    if (b.round) {
      DrawCircleV({ b.x, b.y }, b.r, fill);
      DrawRing({ b.x, b.y }, b.r - 3 * s, b.r, 0, 360, 48, edge);
      sf.DrawTextCentered(font, b.label, b.x, b.y - tall * 0.5f, col(255, 255, 255, std::min(1.f, a + 0.3f)), true);
    } else {
      DrawRectangleRounded({ b.x, b.y, b.w, b.h }, 0.25f, 6, fill);
      DrawRectangleRoundedLinesEx({ b.x, b.y, b.w, b.h }, 0.25f, 6, 2.f * s, edge);
      sf.DrawTextCentered(font, b.label, b.x + b.w * 0.5f, b.y + b.h * 0.5f - tall * 0.5f, col(255, 255, 255, std::min(1.f, a + 0.3f)), true);
    }
  }
  // movement stick: resting position hint, or the live stick
  const float R = 120.f * s;
  Vector2 base = s_stickFinger >= 0 ? s_stickBase : Vector2{ 260 * s, GetScreenHeight() - 260 * s };
  Vector2 knob = s_stickFinger >= 0 ? s_stickPos : base;
  DrawCircleV(base, R, col(0, 0, 0, a * 0.3f));
  DrawRing(base, R - 3 * s, R, 0, 360, 64, col(255, 255, 255, a * 0.6f));
  DrawCircleV(knob, 52 * s, col(255, 255, 255, a * (s_stickFinger >= 0 ? 0.7f : 0.35f)));
}
