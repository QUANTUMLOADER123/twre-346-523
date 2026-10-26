// gameui.cpp - GameUI: CS:GO style main menu (mainmenu.cpp) + the developer console
#include "gameui.hpp"
#include "mainmenu.hpp"
#include "controls.hpp"
#include "../engine/host.hpp"
#include "../engine/convar.hpp"
#include "../engine/random.hpp"
#include "../game/client/cl_main.hpp"
#include "../game/shared/mapinfo.hpp"
#include "../game/shared/soundemitter.hpp"
#include "../game/shared/cs_player.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace vgui;
using namespace scheme;

ConVar hostname("hostname", "Counter-Strike: HvH Server", FCVAR_NOTIFY, "Hostname for server.");
ConVar fps_max("fps_max", "300", FCVAR_ARCHIVE, "Frame rate limiter", true, 0, true, 1000);
ConVar mat_vsync("mat_vsync", "0", FCVAR_ARCHIVE, "Force sync to vertical retrace");

namespace gameui {
static bool s_menuVisible = true;
static Frame s_console("Console", 700, 460, true);

bool IsMenuActive() { return s_menuVisible || !CL_IsInGame(); }
void Show() { s_menuVisible = true; EnableCursor(); }
void Hide() { if (CL_IsInGame()) s_menuVisible = false; }
void ForceHide() { s_menuVisible = false; }
bool IsConsoleVisible() { return s_console.visible; }
void ToggleConsole() {
  if (s_console.visible && s_console.IsActive()) { s_console.visible = false; g_KeyboardFocus = 0; return; }
  if (!s_console.x || s_console.x == 100) { s_console.x = 60; s_console.y = 50; }
  s_console.Activate(); g_KeyboardFocus = 9001;
}
bool WantsKeyboard() { return g_KeyboardFocus != 0 || s_menuVisible || mainmenu::CapturingKey(); }
bool WantsMouse() { return IsMenuActive() || s_console.visible; }

static ConCommand cmd_toggleconsole("toggleconsole", [](const CCommand&) { ToggleConsole(); }, "Show/hide the console.");
static ConCommand cmd_showconsole("showconsole", [](const CCommand&) { if (!s_console.visible) ToggleConsole(); }, "Show the console.");
static ConCommand cmd_gameui_activate("gameui_activate", [](const CCommand&) { Show(); }, "Shows the game UI");
static ConCommand cmd_gameui_hide("gameui_hide", [](const CCommand&) { Hide(); }, "Hides the game UI");

// ------------------------------------------------------------------------------------------------
// Console
static std::string s_conInput;
static std::vector<std::string> s_conHistory; static int s_conHistPos = -1;
static float s_conScroll = 0; static size_t s_conLastCount = 0;
static std::vector<std::string> s_completions; static int s_compSel = -1;

static void PaintConsole() {
  if (!s_console.Begin()) return;
  Frame& f = s_console;
  ISurface* s = surface();
  float ox = f.cx, oy = f.cy, ow = f.cw, oh = f.ch - 34;
  s->DrawFilledRect(ox, oy, ow, oh, BgDark);
  s->DrawBevel(ox, oy, ow, oh, BorderLight, Border, true);
  auto& lines = ConsoleHistory();
  float lh = 14; float content = lines.size() * lh + 6;
  bool atBottom = s_conScroll >= content - oh - lh * 2 || s_conLastCount == 0;
  if (lines.size() != s_conLastCount && atBottom) s_conScroll = std::max(0.f, content - oh);
  s_conLastCount = lines.size();
  if (WM_MouseInCurrent() && Input().wheel != 0 && Input().mouse.y < oy + oh) s_conScroll = Clamp(s_conScroll - Input().wheel * lh * 3, 0.f, std::max(0.f, content - oh));
  s->PushClip((int)ox + 2, (int)oy + 2, (int)ow - 20, (int)oh - 4);
  int first = (int)(s_conScroll / lh);
  for (int i = std::max(0, first); i < (int)lines.size(); i++) {
    float y = oy + 3 + i * lh - s_conScroll; if (y > oy + oh) break;
    const auto& l = lines[i];
    s->DrawText(FONT_MONO, l.text.c_str(), ox + 5, y, { l.color.r, l.color.g, l.color.b, 255 });
  }
  s->PopClip();
  ScrollBar(&s_conScroll, content, oh - 4, ox + ow - 17, oy + 2, oh - 4);
  float iy = f.y + f.h - 36;
  if (g_KeyboardFocus == 0 && f.IsActive()) g_KeyboardFocus = 9001;
  std::string before = s_conInput;
  // history / completion keys
  if (g_KeyboardFocus == 9001) {
    if (Input().KeyPressed(KEY_UP)) { if (!s_completions.empty()) s_compSel = (s_compSel <= 0 ? (int)s_completions.size() : s_compSel) - 1; else if (!s_conHistory.empty()) { s_conHistPos = s_conHistPos < 0 ? (int)s_conHistory.size() - 1 : std::max(0, s_conHistPos - 1); s_conInput = s_conHistory[s_conHistPos]; } ConsumeKey(KEY_UP); }
    if (Input().KeyPressed(KEY_DOWN)) { if (!s_completions.empty()) s_compSel = (s_compSel + 1) % (int)s_completions.size(); else if (s_conHistPos >= 0) { s_conHistPos++; if (s_conHistPos >= (int)s_conHistory.size()) { s_conHistPos = -1; s_conInput.clear(); } else s_conInput = s_conHistory[s_conHistPos]; } ConsumeKey(KEY_DOWN); }
    if (Input().KeyPressed(KEY_TAB)) { if (!s_completions.empty()) { s_conInput = s_completions[s_compSel < 0 ? 0 : s_compSel] + " "; s_completions.clear(); } ConsumeKey(KEY_TAB); }
    if (Input().KeyPressed(KEY_GRAVE)) ConsumeKey(KEY_GRAVE);
    // swallow the backtick character
    auto& ch = Input().chars; ch.erase(std::remove_if(ch.begin(), ch.end(), [](int c) { return c == '`' || c == '~' || c == 0x451 || c == 0x401; }), ch.end());
  }
  bool submit = TextEntry(9001, s_conInput, f.cx, iy, f.cw - 90, 24);
  if (Button("Submit", f.x + f.w - 90, iy, 80, 24)) submit = true;
  if (submit && !s_conInput.empty()) {
    ConColorMsg({ 180, 180, 180, 255 }, "] %s\n", s_conInput.c_str());
    Cbuf_AddText(s_conInput + "\n");
    s_conHistory.push_back(s_conInput); s_conHistPos = -1; s_conInput.clear(); s_completions.clear();
    s_conScroll = 1e9f;
  }
  if (s_conInput != before) {
    s_completions.clear(); s_compSel = -1;
    if (!s_conInput.empty() && s_conInput.find(' ') == std::string::npos) s_completions = CCvar::Get().Complete(s_conInput, 12);
  }
  f.End();
  // completion popup
  if (!s_completions.empty() && g_KeyboardFocus == 9001) {
    float px = f.cx, py = iy + 26, pw = 320, ph = s_completions.size() * 16 + 4;
    if (py + ph > GetScreenHeight()) py = iy - ph - 2;
    s->DrawFilledRect(px, py, pw, ph, BgDark); s->DrawOutlinedRect(px, py, pw, ph, Border);
    for (size_t i = 0; i < s_completions.size(); i++) {
      if ((int)i == s_compSel) s->DrawFilledRect(px + 1, py + 2 + i * 16, pw - 2, 16, Selection);
      std::string t = s_completions[i];
      if (ConVar* v = CCvar::Get().FindVar(t.c_str())) t += TextFormat(" %s", v->GetString());
      s->DrawText(FONT_MONO, t.c_str(), px + 5, py + 3 + i * 16, Text);
    }
  }
}

void Init() {}
void Shutdown() {}

void OnEscape() {
  if (s_console.visible && s_console.IsActive()) { s_console.Close(); g_KeyboardFocus = 0; return; }
  if (IsMenuActive() && mainmenu::OnEscape()) return;
  if (CL_IsInGame()) { if (s_menuVisible) Hide(); else Show(); }
}

void Paint() {
  WM_BeginFrame();
  WM_SetNoFrame();
  if (IsMenuActive()) mainmenu::Paint();
  if (s_console.visible) PaintConsole();
  WM_SetNoFrame();
  DrawPopups();
  if (!s_console.visible && g_KeyboardFocus == 9001) g_KeyboardFocus = 0;
}
} // namespace gameui
