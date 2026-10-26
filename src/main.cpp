// main.cpp - Counter-Strike: HvH launcher (hl2.exe / engine.dll main loop)
//   cshvh [-w 1280] [-h 720] [-fullscreen] [-novid] [+map de_dust2] [+any_console_command args]
//   test helpers: -frames N (quit after N frames)  -shot file.png (save the last frame)  -dt 0.016 (fixed frame time)  -at N cmd (run cmd at frame N)
#include "engine/convar.hpp"
#include "engine/host.hpp"
#include "engine/platform.hpp"
#include "engine/download.hpp"
#include "vgui/contentscreen.hpp"
#include "engine/materialsystem.hpp"
#include "engine/srcfs.hpp"
#include "engine/worldrender.hpp"
#include "game/client/cl_main.hpp"
#include "game/client/hud_menus.hpp"
#include "game/shared/mapinfo.hpp"
#include "game/shared/cs_player.hpp"
#include "vgui/surface.hpp"
#include "vgui/controls.hpp"
#include "vgui/gameui.hpp"
#include "cheat/cheat.hpp"
#include "game/client/touch.hpp"
#include "raylib.h"
#include <string>
#include <vector>
#include <cstring>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <utility>

extern ConVar fps_max;
extern ConVar mat_vsync;

static bool s_wantScreenshot = false;
static ConCommand cmd_screenshot("screenshot", [](const CCommand&) { s_wantScreenshot = true; }, "Take a screenshot.");
static ConVar sv_menu_background("menu_background_map", "de_dust2", FCVAR_ARCHIVE, "Map rendered behind the main menu");

static void SaveScreen(const std::string& path) {
  Image img = LoadImageFromScreen();
#ifdef CS_HEADLESS
  // software framebuffer comes back upside down and BGR
  ImageFlipVertical(&img);
  unsigned char* p = (unsigned char*)img.data;
  if (img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) for (int i = 0; i < img.width * img.height; i++) std::swap(p[i * 4], p[i * 4 + 2]);
#endif
  ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8);
  ExportImage(img, path.c_str());
  UnloadImage(img);
  Msg("Wrote %s\n", path.c_str());
}

#ifndef CSHVH_VERSION
#define CSHVH_VERSION "1.2.0"
#endif

// background world for the main menu (rendered while no game is running)
static void LoadMenuBackground() {
  if (g_Host.active) return;
  std::string map = sv_menu_background.GetString();
  if (!srcfs::Exists("maps/" + map + ".bsp")) {
    // the configured map is not there (a fresh install, or an old map name): use the nicest map that is
    static const char* kPreferred[] = { "de_dust2_old", "de_mirage_ce", "de_inferno_old", "de_nuke_ve", "cs_office", "fy_pool_day" };
    std::string pick;
    for (const char* c : kPreferred) if (srcfs::Exists(std::string("maps/") + c + ".bsp")) { pick = c; break; }
    if (pick.empty()) {
      std::vector<std::string> maps = srcfs::List("maps/", ".bsp");
      if (!maps.empty()) pick = maps[0];
    }
    if (!pick.empty()) map = pick;
  }
  if (g_MapInfo.Load(ResolveMapFile(map))) { g_WorldRender.Unload(); g_WorldRender.Build(g_MapInfo); Msg("Menu background: %s\n", map.c_str()); }
  else Msg("Menu background: no map yet - the content is still downloading\n");
}

// keyboard routing: console > GameUI dialogs > cheat menu > HUD menus (team/buy/chat) > game binds
static void RouteInput() {
  vgui::InputState& in = vgui::Input();
  // ` / ~ toggles the console everywhere
  if (in.KeyPressed(KEY_GRAVE)) { gameui::ToggleConsole(); vgui::ConsumeKey(KEY_GRAVE); vgui::ConsumeChars(); }
  bool gameuiActive = gameui::IsMenuActive();
  bool consoleFocus = gameui::IsConsoleVisible() && vgui::g_KeyboardFocus == 9001;
  // escape
  if (in.KeyPressed(KEY_ESCAPE)) {
    if (!gameuiActive && !consoleFocus && HUD_MenusWantInput()) HUD_MenuKeyPressed(KEY_ESCAPE);
    else if (cheat::IsMenuOpen() && !gameuiActive) cheat::ToggleMenu();
    else gameui::OnEscape();
    vgui::ConsumeKey(KEY_ESCAPE);
  }
  gameuiActive = gameui::IsMenuActive();
  consoleFocus = gameui::IsConsoleVisible() && vgui::g_KeyboardFocus != 0;
  // in-game VGUI menus get the number keys / chat text
  static bool chatWasOpen = false;
  if (!gameuiActive && !consoleFocus && HUD_MenusWantInput()) {
    for (int k : std::vector<int>(in.keys)) if (HUD_MenuKeyPressed(k)) vgui::ConsumeKey(k);
    if (cl.chatOpen && chatWasOpen) for (int c : in.chars) HUD_ChatChar(c);
    vgui::ConsumeChars();
  }
  chatWasOpen = cl.chatOpen;
  if (consoleFocus) cl.keyDest = KEYDEST_CONSOLE;
  else if (gameuiActive) cl.keyDest = KEYDEST_MENU;
  else if (HUD_MenusWantInput()) cl.keyDest = KEYDEST_MESSAGE;
  else cl.keyDest = KEYDEST_GAME;
}

static void UpdateCursor() {
#ifndef CS_HEADLESS
  bool wantCursor = cl.keyDest != KEYDEST_GAME || cheat::WantsMouse() || !CL_IsInGame() || gameui::IsConsoleVisible();
  if (wantCursor && cl.mouseCaptured) { EnableCursor(); cl.mouseCaptured = false; }
  else if (!wantCursor && !cl.mouseCaptured && IsWindowFocused()) { DisableCursor(); cl.mouseCaptured = true; GetMouseDelta(); }
#else
  cl.mouseCaptured = cl.keyDest == KEYDEST_GAME && !cheat::WantsMouse();
#endif
}

int main(int argc, char** argv) {
  int width = 1280, height = 720, frames = -1;
  bool fullscreen = false;
  float fixedDt = 0;
  std::string shotPath;
  std::vector<std::string> plusCmds;
  std::vector<std::pair<int, std::string>> timedCmds; // -at <frame> "<command>"
  for (int i = 1; i < argc; i++) {
    std::string a = argv[i];
    auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
    if (a == "-w" || a == "-width") width = std::atoi(next().c_str());
    else if (a == "-h" || a == "-height") height = std::atoi(next().c_str());
    else if (a == "-fullscreen") fullscreen = true;
    else if (a == "-windowed") fullscreen = false;
    else if (a == "-at") { int f = std::atoi(next().c_str()); timedCmds.push_back({ f, next() }); }
    else if (a == "-frames") frames = std::atoi(next().c_str());
    else if (a == "-shot") shotPath = next();
    else if (a == "-dt") fixedDt = (float)std::atof(next().c_str());
    else if (a == "-game") { std::error_code ec; std::filesystem::current_path(next(), ec); }
    else if (a[0] == '+') {
      std::string cmd = a.substr(1);
      while (i + 1 < argc && argv[i + 1][0] != '+' && argv[i + 1][0] != '-') cmd += std::string(" ") + argv[++i];
      plusCmds.push_back(cmd);
    }
  }
  Platform_PreInit();
  // run from the game directory (assets/, cfg/)
  if (!std::filesystem::exists("assets") && argc > 0) {
    std::error_code ec; auto dir = std::filesystem::path(argv[0]).parent_path();
    if (!dir.empty() && std::filesystem::exists(dir / "assets")) std::filesystem::current_path(dir, ec);
    else if (!dir.empty() && std::filesystem::exists(dir / ".." / "assets")) std::filesystem::current_path(dir / "..", ec);
  }

  SetTraceLogLevel(LOG_WARNING);
  unsigned flags = FLAG_WINDOW_RESIZABLE;
  if (mat_vsync.GetBool()) flags |= FLAG_VSYNC_HINT;
  SetConfigFlags(flags);
  Platform_WindowSize(width, height);
  InitWindow(width, height, "Counter-Strike: HvH");
  SetExitKey(KEY_NULL);
  SetWindowMinSize(800, 600);
  Platform_PostInit();
#if !defined(PLATFORM_ANDROID) && !defined(CS_HEADLESS)
  if (FileExists("assets/icon.png")) { Image icon = LoadImage("assets/icon.png"); ImageFormat(&icon, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8); SetWindowIcon(icon); UnloadImage(icon); }
#endif
  if (fullscreen) ToggleFullscreen();
  ConColorMsg(Color32{ 196, 181, 80, 255 }, "Counter-Strike: HvH %s (raylib %s)\n", CSHVH_VERSION, RAYLIB_VERSION);

  g_MaterialSystem.Init();
  vgui::surface()->Init();
  S_Init();
  CL_Init();
  Host_Init();          // execs cfg/config.cfg
  Cbuf_Execute();
  cheat::Init();
  gameui::Init();
  if (plusCmds.empty()) ContentScreen_Run();   // first start: download the CS:S content (not for scripted runs)
  LoadMenuBackground();
  Host_ExecAutoexec();
  for (auto& c : plusCmds) Cbuf_AddText(c + "\n");
  if (!plusCmds.empty()) gameui::Hide();

  int frame = 0;
  int lastFpsMax = -1;
  bool wasInGame = false;
  while (!g_Host.wantQuit) {
#ifndef CS_HEADLESS
    if (WindowShouldClose()) break;
#endif
    for (auto& tc : timedCmds) if (tc.first == frame) Cbuf_AddText(tc.second + "\n");
    if (fps_max.GetInt() != lastFpsMax) { lastFpsMax = fps_max.GetInt(); SetTargetFPS(lastFpsMax); }
    float dt = fixedDt > 0 ? fixedDt : std::min(GetFrameTime(), 0.1f);
    if (frame == 0 && fixedDt <= 0) dt = 1.f / 60.f;

    vgui::BeginInput();
    RouteInput();
    if (cheat::IsMenuOpen() && cl.keyDest == KEYDEST_GAME) { /* gamesense keeps keyboard movement while the menu is open */ }
    UpdateCursor();
    IN_Frame(dt);
    Host_Frame(dt);
    bool inGame = CL_IsInGame();
    if (inGame != wasInGame) {
      if (inGame) gameui::Hide(); else { gameui::Show(); LoadMenuBackground(); }
      wasInGame = inGame;
    }
    CL_Frame(dt);
    cl.fps = dt > 0 ? (cl.fps * 0.9f + 0.1f / dt) : cl.fps; cl.frameMs = dt * 1000.f;

    BeginDrawing();
    if (inGame) { CL_RenderFrame(); Touch_Draw(); }
    else CL_RenderMenuBackground((float)GetTime());
    gameui::Paint();
    cheat::PaintMenu();
    EndDrawing();

    if (s_wantScreenshot) {
      s_wantScreenshot = false;
      std::error_code ec; std::filesystem::create_directories("screenshots", ec);
      time_t t = time(nullptr); char name[64]; strftime(name, sizeof(name), "screenshots/cshvh_%Y%m%d_%H%M%S.png", localtime(&t));
      SaveScreen(name);
    }
    frame++;
    if (frames > 0 && frame >= frames) {
      if (!shotPath.empty()) SaveScreen(shotPath);
      break;
    }
  }
  cheat::Shutdown();
  CL_WriteConfig();
  Host_Disconnect();
  CL_Shutdown();
  download::Shutdown();   // never leave the content thread running while the process goes away (std::terminate)
  CloseWindow();
  return 0;
}
// trigger ci
// trigger
