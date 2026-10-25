// gameui.cpp - CBasePanel / CGameMenu (Counter-Strike 1.6 Steam main menu) and the GameUI dialogs
#include "gameui.hpp"
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
static Frame s_newGame("Create Server", 560, 470);
static Frame s_servers("Servers", 820, 520, true);
static Frame s_options("Options", 600, 520);
static Frame s_console("Console", 700, 460, true);
static Frame s_quit("Quit game", 360, 150);
static Frame s_connect("Connecting...", 380, 150);
static Frame s_about("About Counter-Strike: HvH", 480, 300);

bool IsMenuActive() { return s_menuVisible || !CL_IsInGame(); }
void Show() { s_menuVisible = true; EnableCursor(); }
void Hide() { if (CL_IsInGame()) s_menuVisible = false; }
bool IsConsoleVisible() { return s_console.visible; }
void ToggleConsole() {
  if (s_console.visible && s_console.IsActive()) { s_console.visible = false; g_KeyboardFocus = 0; return; }
  if (!s_console.x || s_console.x == 100) { s_console.x = 60; s_console.y = 50; }
  s_console.Activate(); g_KeyboardFocus = 9001;
}
bool WantsKeyboard() { return g_KeyboardFocus != 0 || s_menuVisible; }
bool WantsMouse() { return IsMenuActive() || s_console.visible; }

static ConCommand cmd_toggleconsole("toggleconsole", [](const CCommand&) { ToggleConsole(); }, "Show/hide the console.");
static ConCommand cmd_showconsole("showconsole", [](const CCommand&) { if (!s_console.visible) ToggleConsole(); }, "Show the console.");
static ConCommand cmd_gameui_activate("gameui_activate", [](const CCommand&) { Show(); }, "Shows the game UI");
static ConCommand cmd_gameui_hide("gameui_hide", [](const CCommand&) { Hide(); }, "Hides the game UI");

// ------------------------------------------------------------------------------------------------
// Create Server (New Game)
struct NewGameSettings {
  int map = 0; std::string serverName = "Counter-Strike: HvH Server"; std::string password;
  int mode = 0; // 0 classic competitive, 1 deathmatch, 2 HvH competitive
  int botCount = 9; int botDifficulty = 2; int botTeam = 0; bool hvhBots = false;
  float roundTime = 1.92f, freezeTime = 6, maxRounds = 30, startMoney = 800, buyTime = 20;
  bool friendlyFire = false, cheats = true, autoBalance = true, halftime = true;
  int tab = 0;
};
static NewGameSettings s_ng;
static std::vector<MapListEntry> s_maps;

static void StartGame(const std::string& map) {
  Cbuf_AddText(TextFormat("hostname \"%s\"\n", s_ng.serverName.c_str()));
  int gm = s_ng.mode == 1 ? 1 : 0;
  Cbuf_AddText(TextFormat("mp_gamemode %d\nmp_roundtime %.2f\nmp_freezetime %d\nmp_maxrounds %d\nmp_startmoney %d\nmp_buytime %d\nmp_friendlyfire %d\nsv_cheats %d\nmp_autoteambalance %d\nmp_halftime %d\n",
    gm, s_ng.roundTime, (int)s_ng.freezeTime, (int)s_ng.maxRounds, (int)s_ng.startMoney, (int)s_ng.buyTime, s_ng.friendlyFire ? 1 : 0, s_ng.cheats ? 1 : 0, s_ng.autoBalance ? 1 : 0, s_ng.halftime ? 1 : 0));
  Cbuf_AddText(TextFormat("bot_quota %d\nbot_difficulty %d\nbot_join_team %s\nbot_hvh %d\n", s_ng.botCount, s_ng.botDifficulty, s_ng.botTeam == 1 ? "T" : s_ng.botTeam == 2 ? "CT" : "any", (s_ng.hvhBots || s_ng.mode == 2) ? 1 : 0));
  Cbuf_AddText("map " + map + "\n");
  s_menuVisible = false; s_newGame.visible = false; s_servers.visible = false; s_connect.visible = false;
}

void OpenNewGame() {
  s_maps = ListMaps();
  if (!s_newGame.visible) s_newGame.Center();
  s_newGame.Activate();
}

static void PaintNewGame() {
  if (!s_newGame.Begin()) return;
  Frame& f = s_newGame;
  float x = f.cx, y = f.cy;
  PropertySheet({ "Server", "Game", "Bots" }, &s_ng.tab, x, y, f.cw, f.ch - 40);
  float px = x + 16, py = y + 40;
  if (s_ng.tab == 0) {
    std::vector<std::string> names; for (auto& m : s_maps) names.push_back(m.name);
    names.insert(names.begin(), "< Random Map >");
    Label("Map", px, py + 4); ComboBox(101, names, &s_ng.map, px + 140, py, 220);
    if (s_ng.map > 0 && s_ng.map - 1 < (int)s_maps.size()) {
      auto& m = s_maps[s_ng.map - 1];
      GroupBox("Map information", px, py + 36, f.cw - 32, 110);
      Label(m.title.c_str(), px + 14, py + 60, TextSelected, FONT_DEFAULT_BOLD);
      Label(TextFormat("Mode: %s", m.mode == "bomb" ? "Bomb defusal" : m.mode.c_str()), px + 14, py + 80, TextDim);
      Label(m.desc.c_str(), px + 14, py + 100, Text);
    }
    py += 160;
    Label("Server name", px, py + 4); TextEntry(102, s_ng.serverName, px + 140, py, 300);
    py += 32; Label("Password", px, py + 4); TextEntry(103, s_ng.password, px + 140, py, 300, 22, true);
    py += 32; Label("Game mode", px, py + 4); ComboBox(104, { "Classic Competitive", "Deathmatch", "HvH Competitive (cheating bots)" }, &s_ng.mode, px + 140, py, 300);
    py += 36; CheckButton("Allow cheats (sv_cheats 1) - required for the HvH menu", &s_ng.cheats, px, py);
  } else if (s_ng.tab == 1) {
    float w = f.cw - 40;
    Slider("Round time (minutes)", &s_ng.roundTime, 1, 5, px, py, w, "%.2f"); py += 44;
    Slider("Freeze time (seconds)", &s_ng.freezeTime, 0, 15, px, py, w, "%.0f"); py += 44;
    Slider("Max rounds", &s_ng.maxRounds, 2, 30, px, py, w, "%.0f"); py += 44;
    Slider("Start money", &s_ng.startMoney, 800, 16000, px, py, w, "$%.0f"); py += 44;
    Slider("Buy time (seconds)", &s_ng.buyTime, 10, 90, px, py, w, "%.0f"); py += 48;
    CheckButton("Friendly fire", &s_ng.friendlyFire, px, py); CheckButton("Auto team balance", &s_ng.autoBalance, px + 250, py); py += 26;
    CheckButton("Switch sides at halftime", &s_ng.halftime, px, py);
  } else {
    float w = f.cw - 40;
    float bc = (float)s_ng.botCount;
    if (Slider("Number of bots", &bc, 0, 19, px, py, w, "%.0f")) s_ng.botCount = (int)std::round(bc); py += 48;
    Label("Bot difficulty", px, py + 4); ComboBox(110, { "Easy", "Normal", "Hard", "Expert" }, &s_ng.botDifficulty, px + 140, py, 200); py += 34;
    Label("Bots join team", px, py + 4); ComboBox(111, { "Any", "Terrorist", "Counter-Terrorist" }, &s_ng.botTeam, px + 140, py, 200); py += 40;
    CheckButton("HvH bots (bots use rage aimbot, anti-aim, fakelag, resolver)", &s_ng.hvhBots, px, py); py += 30;
    Label("HvH bots buy auto-snipers / heavy pistols and play like", px, py, TextDim); py += 18;
    Label("cheaters on community HvH servers.", px, py, TextDim);
  }
  float by = f.y + f.h - 40;
  if (Button("Start", f.x + f.w - 200, by, 90, 26, true, true)) {
    std::string map;
    if (s_ng.map == 0 && !s_maps.empty()) map = s_maps[RandomInt(0, (int)s_maps.size() - 1)].name;
    else if (s_ng.map - 1 < (int)s_maps.size()) map = s_maps[s_ng.map - 1].name;
    if (!map.empty()) StartGame(map);
  }
  if (Button("Cancel", f.x + f.w - 100, by, 90, 26)) f.Close();
  f.End();
}

// ------------------------------------------------------------------------------------------------
// Find Servers (fake HvH server browser)
struct FakeServer { std::string name, map; int players, maxPlayers, ping; bool password, hvh, dm, secure; };
static std::vector<FakeServer> s_serverList;
static ListPanelState s_serverState;
static int s_serverTab = 0;
static float s_refreshTime = 0; static size_t s_refreshShown = 0;
static FakeServer s_connectTo; static float s_connectStart = 0;

static void BuildServerList() {
  auto maps = ListMaps();
  if (maps.empty()) return;
  const char* names[] = {
    "[RU] HvH.SU | 64 tick | Nospread | Public #1", "gamesense.pub HvH | high fps", "neverlose.cc official HvH | EU", "[EU] onetap.su HvH | 16k money",
    "<<< FATALITY.WIN HVH >>> 1v1 arena", "HvH | aimware.net | DM FFA", "[RU] ТОЛЬКО ЧИТЕРЫ | HvH 5x5", "skeet.cc mm hvh | 13+ choke allowed",
    "Moscow HvH Community | dust2 only", "legendware HvH | noob friendly", "pandora.cc HvH | rage only", "[UA] Kyiv HvH | public",
    "HVH.LV | Latvian HvH | 100 fps", "nixware HvH arena | free cfgs", "weave.su HvH server | DE", "[RU] Rostov HvH | ростов на дону",
    "CS 1.6 classic | NO CHEATS (lol)", "Pub | dust2 24/7 | friendly", "HvH | 2v2 wingman style", "[PL] polska HvH | bez zasad",
    "fatal.club headshot only HvH", "HvH tournaments | qualifiers", "evolve hvh | auto snipers allowed", "dm.hvh.gg | deathmatch hvh",
  };
  s_serverList.clear();
  CUniformRandomStream rs; rs.SetSeed((int)(GetTime() * 1000) & 0xffff);
  for (const char* n : names) {
    FakeServer s; s.name = n; s.map = maps[rs.RandomInt(0, (int)maps.size() - 1)].name;
    s.hvh = strstr(n, "HvH") || strstr(n, "HVH") || strstr(n, "hvh") || strstr(n, "ЧИТЕРЫ");
    s.dm = strstr(n, "DM") || strstr(n, "deathmatch") || strstr(n, "dm.");
    if (strstr(n, "dust2")) s.map = "de_dust2";
    s.maxPlayers = rs.RandomInt(0, 3) == 0 ? 32 : (rs.RandomInt(0, 1) ? 20 : 10);
    s.players = rs.RandomInt(0, s.maxPlayers);
    s.ping = rs.RandomInt(8, 160); if (strstr(n, "[RU]") || strstr(n, "Rostov") || strstr(n, "Moscow")) s.ping = rs.RandomInt(8, 40);
    s.password = rs.RandomInt(0, 9) == 0; s.secure = rs.RandomInt(0, 3) != 0;
    s_serverList.push_back(s);
  }
  s_refreshTime = (float)GetTime(); s_refreshShown = 0;
}

static void ConnectTo(const FakeServer& s) {
  s_connectTo = s; s_connectStart = (float)GetTime();
  s_connect.Center(); s_connect.Activate();
}

static void PaintServers() {
  if (!s_servers.Begin()) return;
  Frame& f = s_servers;
  if (s_serverList.empty()) BuildServerList();
  PropertySheet({ "Internet", "Favorites", "History", "Spectate", "Lan", "Friends" }, &s_serverTab, f.cx, f.cy, f.cw, f.ch - 70);
  // progressive "refresh" like the real browser
  float since = (float)GetTime() - s_refreshTime;
  s_refreshShown = std::min(s_serverList.size(), (size_t)(since * 14));
  std::vector<int> idx;
  for (size_t i = 0; i < s_refreshShown; i++) {
    const FakeServer& s = s_serverList[i];
    if (s_serverTab == 1 && !(i % 5 == 0)) continue;
    if (s_serverTab == 2 && !(i % 7 == 1)) continue;
    if (s_serverTab == 3 || s_serverTab == 5) continue;
    if (s_serverTab == 4 && i != 0) continue;
    idx.push_back((int)i);
  }
  int sc = s_serverState.sortColumn; bool asc = s_serverState.sortAsc;
  std::sort(idx.begin(), idx.end(), [&](int a, int b) {
    const FakeServer &A = s_serverList[a], &B = s_serverList[b]; bool r;
    switch (sc) { case 1: r = A.name < B.name; break; case 3: r = A.players < B.players; break; case 4: r = A.map < B.map; break; case 5: r = A.ping < B.ping; break; default: return a < b; }
    return asc ? r : !r;
  });
  std::vector<std::vector<std::string>> rows;
  for (int i : idx) { const FakeServer& s = s_serverList[i]; rows.push_back({ s.password ? "L" : "", s.name, "Counter-Strike: HvH", TextFormat("%d / %d", s.players, s.maxPlayers), s.map, TextFormat("%d", s_serverTab == 4 ? 1 : s.ping) }); }
  std::vector<ListColumn> cols = { { "", 22 }, { "Servers", f.cw - 22 - 150 - 80 - 110 - 70 }, { "Game", 150 }, { "Players", 80 }, { "Map", 110 }, { "Latency", 70 } };
  int dbl = ListPanel(s_serverState, cols, rows, f.cx + 8, f.cy + 34, f.cw - 16, f.ch - 112, [&](int r, int c) {
    const FakeServer& s = s_serverList[idx[r]]; if (c == 5) return s.ping < 50 ? Color{ 150, 220, 120, 255 } : s.ping < 100 ? Text : Color{ 220, 140, 100, 255 }; return s.players == s.maxPlayers ? TextDim : Text; });
  if (dbl >= 0 && dbl < (int)idx.size()) ConnectTo(s_serverList[idx[dbl]]);
  float by = f.y + f.h - 36;
  Label(s_refreshShown < s_serverList.size() ? TextFormat("Refreshing server list... (%d servers)", (int)s_refreshShown) : TextFormat("%d servers", (int)idx.size()), f.cx + 4, by - 26, TextDim);
  if (Button("Change filters", f.cx, by, 110, 26)) {}
  if (Button("Quick refresh", f.x + f.w - 330, by, 100, 26)) { s_refreshTime = (float)GetTime(); s_refreshShown = 0; }
  if (Button("Refresh all", f.x + f.w - 220, by, 100, 26)) BuildServerList();
  bool canConnect = s_serverState.selected >= 0 && s_serverState.selected < (int)idx.size();
  if (Button("Connect", f.x + f.w - 110, by, 100, 26, canConnect, true) && canConnect) ConnectTo(s_serverList[idx[s_serverState.selected]]);
  f.End();
}

static void PaintConnect() {
  if (!s_connect.Begin()) return;
  Frame& f = s_connect;
  float t = (float)GetTime() - s_connectStart;
  const char* stage = t < 0.6f ? "Connecting to server..." : t < 1.2f ? "Retrieving server info..." : t < 1.8f ? "Sending client info..." : "Loading map...";
  Label(TextFormat("%s", s_connectTo.name.c_str()), f.cx, f.cy + 4, TextSelected, FONT_DEFAULT_BOLD);
  Label(stage, f.cx, f.cy + 26);
  float pw = f.cw, frac = Clamp(t / 2.2f, 0.f, 1.f);
  surface()->DrawFilledRect(f.cx, f.cy + 50, pw, 14, BgDark); surface()->DrawBevel(f.cx, f.cy + 50, pw, 14, BorderLight, Border, true);
  for (int i = 0; i < (int)(frac * 30); i++) surface()->DrawFilledRect(f.cx + 2 + i * (pw - 4) / 30, f.cy + 52, (pw - 4) / 30 - 2, 10, TextSelected);
  if (Button("Cancel", f.x + f.w - 100, f.y + f.h - 36, 90, 26)) f.Close();
  if (t > 2.3f && f.visible) {
    s_ng.serverName = s_connectTo.name; s_ng.hvhBots = s_connectTo.hvh; s_ng.mode = s_connectTo.dm ? 1 : (s_connectTo.hvh ? 2 : 0);
    s_ng.botCount = std::max(3, std::min(19, s_connectTo.players > 1 ? s_connectTo.players - 1 : 9));
    s_ng.botDifficulty = 3; s_ng.cheats = true;
    StartGame(s_connectTo.map);
  }
  f.End();
}

// ------------------------------------------------------------------------------------------------
// Options
static int s_optTab = 0;
struct OptState {
  std::string name; float sens = 2.5f; bool invert = false, raw = true, filter = false;
  float volume = 0.6f; bool footsteps = true; int hitsound = 0;
  int crossColor = 0; float crossSize = 5; bool dynCross = true; float hudScale = 1.f;
  int resolution = 1; int displayMode = 0; bool vsync = false; float fpsMax = 300; float fov = 90; float vmFov = 68;
  bool loaded = false;
};
static OptState s_opt;
static ListPanelState s_keyState;
static int s_waitingForKey = -1;
static const int s_resW[] = { 1024, 1280, 1280, 1366, 1600, 1920, 2560 }, s_resH[] = { 768, 720, 1024, 768, 900, 1080, 1440 };
struct KeyAction { const char* cmd; const char* name; };
static const KeyAction s_actions[] = {
  { "+forward", "Move forward" }, { "+back", "Move back" }, { "+moveleft", "Move left (strafe)" }, { "+moveright", "Move right (strafe)" },
  { "+jump", "Jump" }, { "+duck", "Duck" }, { "+speed", "Walk" }, { "+use", "Use items" }, { "+attack", "Fire" }, { "+attack2", "Weapon special function" },
  { "+reload", "Reload weapon" }, { "drop", "Drop current weapon" }, { "buymenu", "Buy menu" }, { "chooseteam", "Select team" },
  { "slot1", "Primary weapon" }, { "slot2", "Secondary weapon" }, { "slot3", "Knife" }, { "slot4", "Grenades" }, { "slot5", "C4 bomb" }, { "lastinv", "Last used weapon" },
  { "+showscores", "Display scores" }, { "messagemode", "Chat message" }, { "messagemode2", "Team message" }, { "autobuy", "Autobuy" }, { "rebuy", "Rebuy" },
  { "toggleconsole", "Toggle console" }, { "screenshot", "Take screenshot" }, { "thirdperson", "Third person camera" },
};
static const char* s_crossColors[] = { "Green", "Red", "Blue", "Yellow", "Cyan", "White" };
static const int s_crossRGB[][3] = { { 50, 250, 50 }, { 250, 50, 50 }, { 50, 50, 250 }, { 250, 250, 50 }, { 50, 250, 250 }, { 255, 255, 255 } };

static void LoadOptions() {
  s_opt.name = FindConVar("name")->GetString(); s_opt.sens = FindConVar("sensitivity")->GetFloat();
  s_opt.invert = FindConVar("m_invert")->GetBool(); s_opt.raw = FindConVar("m_rawinput")->GetBool(); s_opt.filter = FindConVar("m_filter")->GetBool();
  s_opt.volume = FindConVar("volume")->GetFloat(); s_opt.footsteps = FindConVar("snd_footsteps")->GetBool(); s_opt.hitsound = FindConVar("snd_hitsound")->GetInt();
  s_opt.crossSize = FindConVar("cl_crosshairsize")->GetFloat(); s_opt.dynCross = FindConVar("cl_dynamiccrosshair")->GetBool(); s_opt.hudScale = FindConVar("hud_scaling")->GetFloat();
  int r = FindConVar("cl_crosshaircolor_r")->GetInt(), g = FindConVar("cl_crosshaircolor_g")->GetInt(), b = FindConVar("cl_crosshaircolor_b")->GetInt();
  for (int i = 0; i < 6; i++) if (s_crossRGB[i][0] == r && s_crossRGB[i][1] == g && s_crossRGB[i][2] == b) s_opt.crossColor = i;
  s_opt.vsync = mat_vsync.GetBool(); s_opt.fpsMax = fps_max.GetFloat(); s_opt.fov = FindConVar("fov_desired")->GetFloat(); s_opt.vmFov = FindConVar("viewmodel_fov")->GetFloat();
  for (int i = 0; i < 7; i++) if (s_resW[i] == GetScreenWidth() && s_resH[i] == GetScreenHeight()) s_opt.resolution = i;
  s_opt.displayMode = IsWindowFullscreen() ? 1 : 0;
  s_opt.loaded = true;
}
static void ApplyOptions() {
  Cbuf_AddText(TextFormat("name \"%s\"\nsensitivity %.3f\nm_invert %d\nm_rawinput %d\nm_filter %d\nvolume %.2f\nsnd_footsteps %d\nsnd_hitsound %d\n", s_opt.name.c_str(), s_opt.sens, s_opt.invert, s_opt.raw, s_opt.filter, s_opt.volume, s_opt.footsteps, s_opt.hitsound));
  Cbuf_AddText(TextFormat("cl_crosshaircolor_r %d\ncl_crosshaircolor_g %d\ncl_crosshaircolor_b %d\ncl_crosshairsize %.0f\ncl_dynamiccrosshair %d\nhud_scaling %.2f\n", s_crossRGB[s_opt.crossColor][0], s_crossRGB[s_opt.crossColor][1], s_crossRGB[s_opt.crossColor][2], s_opt.crossSize, s_opt.dynCross, s_opt.hudScale));
  Cbuf_AddText(TextFormat("mat_vsync %d\nfps_max %.0f\nfov_desired %.0f\nviewmodel_fov %.0f\n", s_opt.vsync, s_opt.fpsMax, s_opt.fov, s_opt.vmFov));
  if (g_pLocalPlayer) g_pLocalPlayer->m_szPlayerName = s_opt.name;
#ifndef CS_HEADLESS
  bool fs = s_opt.displayMode == 1;
  if (fs != IsWindowFullscreen()) { if (fs) { SetWindowSize(GetMonitorWidth(GetCurrentMonitor()), GetMonitorHeight(GetCurrentMonitor())); } ToggleFullscreen(); }
  if (!fs && (GetScreenWidth() != s_resW[s_opt.resolution] || GetScreenHeight() != s_resH[s_opt.resolution])) SetWindowSize(s_resW[s_opt.resolution], s_resH[s_opt.resolution]);
#endif
  Cbuf_AddText("host_writeconfig\n");
}

static void PaintOptions() {
  if (!s_options.Begin()) return;
  Frame& f = s_options;
  if (!s_opt.loaded) LoadOptions();
  PropertySheet({ "Multiplayer", "Keyboard", "Mouse", "Audio", "Video" }, &s_optTab, f.cx, f.cy, f.cw, f.ch - 40);
  float px = f.cx + 16, py = f.cy + 40, w = f.cw - 32;
  switch (s_optTab) {
    case 0:
      Label("Player name", px, py + 4); TextEntry(201, s_opt.name, px + 120, py, 260, 22, false, 31); py += 40;
      GroupBox("Crosshair", px, py, w, 150);
      Label("Color", px + 14, py + 26); ComboBox(202, { s_crossColors, s_crossColors + 6 }, &s_opt.crossColor, px + 120, py + 22, 160);
      Slider("Size", &s_opt.crossSize, 1, 10, px + 14, py + 56, w - 200, "%.0f");
      CheckButton("Dynamic crosshair", &s_opt.dynCross, px + 14, py + 104);
      { // preview
        float cxp = px + w - 70, cyp = py + 80; surface()->DrawFilledRect(cxp - 50, cyp - 50, 100, 100, { 90, 80, 60, 255 });
        Color c = { (unsigned char)s_crossRGB[s_opt.crossColor][0], (unsigned char)s_crossRGB[s_opt.crossColor][1], (unsigned char)s_crossRGB[s_opt.crossColor][2], 255 };
        float l = s_opt.crossSize * 2, g = 3;
        surface()->DrawFilledRect(cxp, cyp - g - l, 1, l, c); surface()->DrawFilledRect(cxp, cyp + g, 1, l, c); surface()->DrawFilledRect(cxp - g - l, cyp, l, 1, c); surface()->DrawFilledRect(cxp + g, cyp, l, 1, c);
      }
      py += 166;
      Slider("HUD scale", &s_opt.hudScale, 0.5f, 1.5f, px, py, w, "%.2f"); py += 48;
      Slider("Field of view", &s_opt.fov, 75, 110, px, py, w, "%.0f"); py += 48;
      Slider("Viewmodel field of view", &s_opt.vmFov, 54, 90, px, py, w, "%.0f");
      break;
    case 1: {
      std::vector<std::vector<std::string>> rows;
      for (auto& a : s_actions) { int k = Key_FindBinding(a.cmd); rows.push_back({ a.name, k >= 0 ? Key_KeynumToString(k) : "" }); }
      if (s_waitingForKey >= 0 && s_waitingForKey < (int)rows.size()) rows[s_waitingForKey][1] = "< press a key >";
      int dbl = ListPanel(s_keyState, { { "Action", w - 160 }, { "Key / button", 160 } }, rows, px, py, w, f.ch - 140);
      if (dbl >= 0) s_waitingForKey = dbl;
      float by = f.cy + f.ch - 86;
      if (Button("Edit key", px, by, 100, 24, s_keyState.selected >= 0)) s_waitingForKey = s_keyState.selected;
      if (Button("Clear key", px + 110, by, 100, 24, s_keyState.selected >= 0)) { int k = Key_FindBinding(s_actions[s_keyState.selected].cmd); if (k >= 0) Key_SetBinding(k, ""); }
      if (Button("Use defaults", px + w - 110, by, 110, 24)) { Cbuf_AddText("unbindall\n"); IN_Init(); }
      if (s_waitingForKey >= 0) {
        int key = 0;
        for (int k : Input().keys) { key = k; break; }
        if (Input().mousePressed && !WM_MouseInCurrent()) key = 0;
        static const int mb[5] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_SIDE, MOUSE_BUTTON_EXTRA };
        static bool armed = false; if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) armed = true;
        for (int i = 0; i < 5; i++) if (IsMouseButtonPressed(mb[i]) && (i != 0 || armed)) key = K_MOUSE1 + i;
        if (Input().wheel > 0) key = K_MWHEELUP; if (Input().wheel < 0) key = K_MWHEELDOWN;
        if (key == KEY_ESCAPE) { s_waitingForKey = -1; ConsumeKey(KEY_ESCAPE); armed = false; }
        else if (key) {
          int old = Key_FindBinding(s_actions[s_waitingForKey].cmd); if (old >= 0) Key_SetBinding(old, "");
          Key_SetBinding(key, s_actions[s_waitingForKey].cmd); s_waitingForKey = -1; armed = false; ConsumeKey(key);
        }
      }
      break;
    }
    case 2:
      CheckButton("Reverse mouse", &s_opt.invert, px, py); py += 26;
      CheckButton("Mouse filter", &s_opt.filter, px, py); py += 26;
      CheckButton("Raw input", &s_opt.raw, px, py); py += 36;
      Slider("Mouse sensitivity", &s_opt.sens, 0.1f, 10.f, px, py, w, "%.2f"); py += 50;
      Label(TextFormat("Effective: %.1f cm / 360 at 800 dpi", 360.f / (s_opt.sens * 0.022f) / 800.f * 2.54f), px, py, TextDim);
      break;
    case 3:
      Slider("Game volume", &s_opt.volume, 0, 1, px, py, w, "%.2f"); py += 50;
      CheckButton("Footstep sounds", &s_opt.footsteps, px, py); py += 30;
      Label("Hit sound", px, py + 4); ComboBox(240, { "Off", "Bell (skeet)", "Click" }, &s_opt.hitsound, px + 120, py, 200); py += 40;
      Label("All sounds are synthesized procedurally in real time.", px, py, TextDim);
      break;
    case 4: {
      std::vector<std::string> res; for (int i = 0; i < 7; i++) res.push_back(TextFormat("%d x %d", s_resW[i], s_resH[i]));
      Label("Resolution", px, py + 4); ComboBox(250, res, &s_opt.resolution, px + 140, py, 200); py += 34;
      Label("Display mode", px, py + 4); ComboBox(251, { "Windowed", "Fullscreen" }, &s_opt.displayMode, px + 140, py, 200); py += 40;
      CheckButton("Wait for vertical sync", &s_opt.vsync, px, py); py += 34;
      Slider("Max FPS (0 = unlimited)", &s_opt.fpsMax, 0, 1000, px, py, w, "%.0f"); py += 50;
      Label(TextFormat("Renderer: raylib %s  |  %d fps", RAYLIB_VERSION, GetFPS()), px, py, TextDim);
      break;
    }
  }
  float by = f.y + f.h - 40;
  if (Button("OK", f.x + f.w - 300, by, 90, 26, true, true)) { ApplyOptions(); f.Close(); s_opt.loaded = false; }
  if (Button("Cancel", f.x + f.w - 200, by, 90, 26)) { f.Close(); s_opt.loaded = false; }
  if (Button("Apply", f.x + f.w - 100, by, 90, 26)) ApplyOptions();
  f.End();
}

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

// ------------------------------------------------------------------------------------------------
static void PaintQuit() {
  if (!s_quit.Begin()) return;
  Frame& f = s_quit;
  Label("Are you sure you want to quit?", f.cx + 10, f.cy + 14);
  if (Button("Quit game", f.x + f.w - 200, f.y + f.h - 40, 90, 26, true, true)) g_Host.wantQuit = true;
  if (Button("Cancel", f.x + f.w - 100, f.y + f.h - 40, 90, 26)) f.Close();
  f.End();
}
static void PaintAbout() {
  if (!s_about.Begin()) return;
  Frame& f = s_about; float y = f.cy + 8;
  const char* lines[] = { "Counter-Strike: HvH", "", "A from-scratch C++17 remake of the Counter-Strike gameplay loop", "with a built-in HvH cheat (press INSERT in game).", "",
    "Engine: raylib 6.2 (zlib license)", "Models: Quaternius (CC0)", "Fonts: DejaVu", "Sounds: synthesized in real time", "", "Not affiliated with Valve Corporation." };
  for (int i = 0; i < 11; i++) { Label(lines[i], f.cx + 6, y, i == 0 ? TextSelected : Text, i == 0 ? FONT_DEFAULT_BOLD : FONT_UI); y += 18; }
  if (Button("Close", f.x + f.w - 100, f.y + f.h - 40, 90, 26)) f.Close();
  f.End();
}

// ------------------------------------------------------------------------------------------------
// main menu
static void MainMenuItem(const char* text, float x, float& y, std::function<void()> fn, bool enabled = true) {
  ISurface* s = surface();
  Vector2 sz = s->GetTextSize(FONT_MENU_TITLE, text);
  bool hv = enabled && !WM_AnyFrameUnderMouse() && Input().mouse.x >= x - 6 && Input().mouse.x < x + sz.x + 6 && Input().mouse.y >= y - 2 && Input().mouse.y < y + sz.y + 2;
  static std::string lastHover;
  if (hv && lastHover != text) { EmitSound2D("UI.Rollover"); lastHover = text; }
  if (!hv && lastHover == text) lastHover.clear();
  Color c = !enabled ? Color{ 120, 120, 110, 200 } : hv ? Color{ 255, 255, 255, 255 } : Color{ 210, 210, 200, 235 };
  s->DrawTextShadow(FONT_MENU_TITLE, text, x, y, c, { 0, 0, 0, 180 });
  if (hv && Input().mouseReleased) { PlayClick(); fn(); }
  y += sz.y + 12;
}

static void PaintMainMenu() {
  ISurface* s = surface();
  int W = s->ScreenWide(), H = s->ScreenTall();
  bool ingame = CL_IsInGame();
  // darken / tint (olive gradient like the 1.6 backgrounds)
  if (ingame) s->DrawFilledRect(0, 0, (float)W, (float)H, { 0, 0, 0, 120 });
  else { s->DrawGradientV(0, 0, (float)W, (float)H, { 20, 24, 16, 120 }, { 10, 12, 8, 220 }); s->DrawGradientH(0, 0, W * 0.55f, (float)H, { 0, 0, 0, 170 }, { 0, 0, 0, 0 }); }
  // logo
  float lx = 64, ly = H * 0.16f;
  const char* logo = "COUNTER-STRIKE";
  s->DrawTextShadow(FONT_LOGO, logo, lx, ly, { 235, 235, 225, 255 }, { 0, 0, 0, 200 });
  Vector2 ls = s->GetTextSize(FONT_LOGO, logo);
  s->DrawTextShadow(FONT_LOGO, "HvH", lx + ls.x + 16, ly, { 196, 181, 80, 255 }, { 0, 0, 0, 200 });
  s->DrawFilledRect(lx, ly + ls.y + 4, ls.x + 140, 2, { 196, 181, 80, 200 });
  s->DrawText(FONT_UI, "hack vs hack edition", lx + 2, ly + ls.y + 10, { 200, 200, 190, 200 });
  // menu
  float x = 72, y = H * 0.5f;
  if (ingame) {
    MainMenuItem("Resume Game", x, y, [] { Hide(); });
    MainMenuItem("Disconnect", x, y, [] { Cbuf_AddText("disconnect\n"); });
    y += 10;
  }
  MainMenuItem("New Game", x, y, [] { OpenNewGame(); });
  MainMenuItem("Find Servers", x, y, [] { if (!s_servers.visible) { s_servers.Center(); BuildServerList(); } s_servers.Activate(); });
  MainMenuItem("Options", x, y, [] { if (!s_options.visible) { s_options.Center(); s_opt.loaded = false; } s_options.Activate(); });
  MainMenuItem("Console", x, y, [] { ToggleConsole(); });
  MainMenuItem("About", x, y, [] { s_about.Center(); s_about.Activate(); });
  y += 10;
  MainMenuItem("Quit", x, y, [] { s_quit.Center(); s_quit.Activate(); });
  // footer
  s->DrawTextRight(FONT_UI, "Counter-Strike: HvH  1.0.0  |  raylib " RAYLIB_VERSION, W - 12.f, H - 22.f, { 200, 200, 190, 160 });
  if (ingame) s->DrawTextRight(FONT_UI, TextFormat("%s  -  %s", FindConVar("hostname")->GetString(), g_MapInfo.name.c_str()), W - 12.f, 12.f, { 220, 220, 210, 200 }, true);
}

void Init() { s_maps = ListMaps(); }
void Shutdown() {}

void OnEscape() {
  // close the topmost dialog first
  std::vector<Frame*> order; WM_DrawOrder(order);
  for (auto it = order.rbegin(); it != order.rend(); ++it) if ((*it)->visible && *it != &s_console) { (*it)->Close(); return; }
  if (s_console.visible && s_console.IsActive()) { s_console.Close(); g_KeyboardFocus = 0; return; }
  if (CL_IsInGame()) { if (s_menuVisible) Hide(); else Show(); }
}

void Paint() {
  WM_BeginFrame();
  WM_SetNoFrame();
  if (IsMenuActive()) PaintMainMenu();
  // dialogs in z-order
  std::vector<Frame*> order; WM_DrawOrder(order);
  bool drawn[8] = {};
  for (Frame* f : order) {
    if (!f->visible) continue;
    if (f == &s_newGame) { PaintNewGame(); drawn[0] = true; }
    else if (f == &s_servers) { PaintServers(); drawn[1] = true; }
    else if (f == &s_options) { PaintOptions(); drawn[2] = true; }
    else if (f == &s_console) { PaintConsole(); drawn[3] = true; }
    else if (f == &s_quit) { PaintQuit(); drawn[4] = true; }
    else if (f == &s_connect) { PaintConnect(); drawn[5] = true; }
    else if (f == &s_about) { PaintAbout(); drawn[6] = true; }
  }
  WM_SetNoFrame();
  DrawPopups();
  if (!s_console.visible && g_KeyboardFocus == 9001) g_KeyboardFocus = 0;
  // dialogs only show while the menu is up (except the console)
  if (!IsMenuActive()) { s_newGame.visible = s_servers.visible = s_options.visible = s_quit.visible = s_about.visible = false; }
}
} // namespace gameui
