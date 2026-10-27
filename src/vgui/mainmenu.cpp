// mainmenu.cpp - Counter-Strike: Global Offensive (Panorama) style main menu, see mainmenu.hpp
#include "mainmenu.hpp"
#include <filesystem>
#include "gameui.hpp"
#include "controls.hpp"
#include "csgo_style.hpp"
#include "vgui_images.hpp"
#include "../engine/host.hpp"
#include "../engine/convar.hpp"
#include "../engine/random.hpp"
#include "../engine/srcfs.hpp"
#include "../engine/srcmaterial.hpp"
#include "../engine/download.hpp"
#include "../engine/mapoverview.hpp"
#include "../game/client/cl_main.hpp"
#include "../game/client/css_models.hpp"
#include "../game/shared/mapinfo.hpp"
#include "../game/shared/soundemitter.hpp"
#include "../game/shared/cs_player.hpp"
#include "../game/shared/cs_loadout.hpp"
#include "../game/shared/weapon_parse.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>
#include <map>
#include <string>
#include <vector>

using namespace vgui;

namespace mainmenu {
namespace {
// ------------------------------------------------------------------------------------------------
// state
enum Panel { P_HOME, P_PLAY, P_AGENTS, P_LOADOUT, P_SERVERS, P_SETTINGS };
Panel s_panel = P_HOME;
float s_panelAnim = 0;           // 0..1 fade of the open panel
const float TB = 56;             // top bar height

struct MatchSettings {
  std::string map = "de_dust2";
  int mode = 0;                  // 0 competitive, 1 deathmatch, 2 HvH
  float bots = 9; int botDifficulty = 2; int botTeam = 0; bool hvhBots = false;
  int roundTime = 2;             // index into s_roundTimes
  float freezeTime = 6; int maxRounds = 3; int startMoney = 0; float buyTime = 20;
  bool friendlyFire = false, cheats = true, autoBalance = true, halftime = true;
} s_ms;
const float s_roundTimes[] = { 1.f, 1.5f, 1.92f, 2.25f, 3.f, 5.f };
const int s_maxRounds[] = { 8, 16, 24, 30, 0 };
const int s_startMoney[] = { 800, 1000, 4000, 10000, 16000 };

// input helpers (mouse == first finger on phones)
Vector2 s_pressPos{}; bool s_dragScroll = false; bool s_clearDrag = false; const void* s_activeSlider = nullptr;
Vector2 s_lastMouse{};
std::string s_lastHover;
int s_buttonIndex = 0;           // FlatButton call order this frame -> stable hover ids even while panels animate
const void* s_captureKey = nullptr; std::string s_captureCmd;   // keyboard settings

InputState& In() { return Input(); }
ISurface* S() { return surface(); }
bool Over(float x, float y, float w, float h) { Vector2 m = In().mouse; return m.x >= x && m.y >= y && m.x < x + w && m.y < y + h; }
bool PressedIn(float x, float y, float w, float h) { return s_pressPos.x >= x && s_pressPos.y >= y && s_pressPos.x < x + w && s_pressPos.y < y + h; }
// a click: released over the widget it was pressed on, and the finger did not scroll a list
bool Clicked(float x, float y, float w, float h) { return In().mouseReleased && !s_dragScroll && !s_activeSlider && Over(x, y, w, h) && PressedIn(x, y, w, h); }
void HoverSound(const char* id, bool hv) {
  if (hv && (s_lastHover.empty() || s_lastHover != id)) { EmitSound2D("UI.Rollover"); s_lastHover = id; }
  else if (!hv && s_lastHover == id) s_lastHover.clear();
}
Color Mix(Color a, Color b, float t) { return { (unsigned char)(a.r + (b.r - a.r) * t), (unsigned char)(a.g + (b.g - a.g) * t), (unsigned char)(a.b + (b.b - a.b) * t), (unsigned char)(a.a + (b.a - a.a) * t) }; }

// ------------------------------------------------------------------------------------------------
// icons (vector, no textures needed)
void IconPlay(float cx, float cy, float r, Color c) { DrawTriangle({ cx - r * 0.55f, cy - r * 0.8f }, { cx - r * 0.55f, cy + r * 0.8f }, { cx + r * 0.85f, cy }, c); }
void IconGear(float cx, float cy, float r, Color c) {
  for (int i = 0; i < 8; i++) {
    float a = i * PI / 4;
    DrawLineEx({ cx + std::cos(a) * r * 0.55f, cy + std::sin(a) * r * 0.55f }, { cx + std::cos(a) * r * 1.0f, cy + std::sin(a) * r * 1.0f }, r * 0.38f, c);
  }
  DrawCircleV({ cx, cy }, r * 0.72f, c);
  DrawCircleV({ cx, cy }, r * 0.30f, csgo::TopBar);
}
void IconPower(float cx, float cy, float r, Color c) {
  DrawRing({ cx, cy }, r * 0.72f, r * 0.92f, -50, 230, 32, c);   // raylib angles: 0 = right, clockwise (y down)
  DrawLineEx({ cx, cy - r * 1.0f }, { cx, cy - r * 0.15f }, r * 0.2f, c);
}
void IconAgent(float cx, float cy, float r, Color c) {
  DrawCircleV({ cx, cy - r * 0.42f }, r * 0.36f, c);
  DrawCircleSector({ cx, cy + r * 0.85f }, r * 0.8f, 180, 360, 24, c);
}
void IconGlobe(float cx, float cy, float r, Color c) {
  DrawRing({ cx, cy }, r * 0.8f, r * 0.95f, 0, 360, 32, c);
  DrawLineEx({ cx - r * 0.9f, cy }, { cx + r * 0.9f, cy }, r * 0.13f, c);
  DrawEllipseLines((int)cx, (int)cy, r * 0.38f, r * 0.88f, c);
  DrawLineEx({ cx, cy - r * 0.9f }, { cx, cy + r * 0.9f }, r * 0.13f, c);
}
void IconConsole(float cx, float cy, float r, Color c) {
  DrawLineEx({ cx - r * 0.8f, cy - r * 0.5f }, { cx - r * 0.25f, cy }, r * 0.2f, c);
  DrawLineEx({ cx - r * 0.25f, cy }, { cx - r * 0.8f, cy + r * 0.5f }, r * 0.2f, c);
  DrawLineEx({ cx, cy + r * 0.55f }, { cx + r * 0.8f, cy + r * 0.55f }, r * 0.2f, c);
}
void IconGun(float cx, float cy, float r, Color c) {
  DrawRectangleRec({ cx - r, cy - r * 0.45f, r * 2.f, r * 0.42f }, c);                  // slide
  DrawRectangleRec({ cx - r * 0.95f, cy - r * 0.1f, r * 0.55f, r * 0.95f }, c);         // grip
  DrawRectangleRec({ cx - r * 0.3f, cy - r * 0.05f, r * 0.45f, r * 0.14f }, c);         // trigger guard
}
void IconBack(float cx, float cy, float r, Color c) {
  DrawLineEx({ cx + r * 0.3f, cy - r * 0.7f }, { cx - r * 0.4f, cy }, r * 0.24f, c);
  DrawLineEx({ cx - r * 0.4f, cy }, { cx + r * 0.3f, cy + r * 0.7f }, r * 0.24f, c);
}

// ------------------------------------------------------------------------------------------------
// widgets
bool FlatButton(const char* label, float x, float y, float w, float h, Color base, Color hover, Color text = csgo::Text, int font = FONT_GO_BOLD) {
  bool hv = Over(x, y, w, h);
  HoverSound(TextFormat("btn%d:%s", s_buttonIndex++, label), hv);   // unique per widget (two "PLAY" buttons used to fight)
  DrawRectangleRec({ x, y, w, h }, hv ? hover : base);
  float th = (float)S()->GetFontTall(font);
  S()->DrawTextCentered(font, label, x + w * 0.5f, y + (h - th) * 0.5f, text);
  bool c = Clicked(x, y, w, h);
  if (c) PlayClick();
  return c;
}

bool Toggle(bool& v, float x, float y, float w, float h) {
  // "OFF | ON" segmented switch like the Panorama settings
  float hw = w * 0.5f;
  DrawRectangleRec({ x, y, w, h }, csgo::PanelLight);
  DrawRectangleRec({ v ? x + hw : x, y, hw, h }, v ? csgo::Accent : csgo::PanelHover);
  float th = (float)S()->GetFontTall(FONT_GO_BOLD);
  S()->DrawTextCentered(FONT_GO_BOLD, "OFF", x + hw * 0.5f, y + (h - th) * 0.5f, v ? csgo::TextDim : csgo::Text);
  S()->DrawTextCentered(FONT_GO_BOLD, "ON", x + hw * 1.5f, y + (h - th) * 0.5f, v ? csgo::Text : csgo::TextDim);
  if (Clicked(x, y, w, h)) { v = !v; PlayClick(); return true; }
  return false;
}

bool Choice(int& idx, const std::vector<std::string>& opts, float x, float y, float w, float h) {
  if (opts.empty()) return false;
  int n = (int)opts.size(); idx = std::clamp(idx, 0, n - 1);
  DrawRectangleRec({ x, y, w, h }, csgo::PanelLight);
  bool hl = Over(x, y, h, h), hr = Over(x + w - h, y, h, h);
  DrawRectangleRec({ x, y, h, h }, hl ? csgo::PanelHover : csgo::Panel);
  DrawRectangleRec({ x + w - h, y, h, h }, hr ? csgo::PanelHover : csgo::Panel);
  IconBack(x + h * 0.5f, y + h * 0.5f, h * 0.28f, csgo::Text);
  float rx = x + w - h * 0.5f, ry = y + h * 0.5f, r = h * 0.28f;
  DrawLineEx({ rx - r * 0.3f, ry - r * 0.7f }, { rx + r * 0.4f, ry }, r * 0.24f, csgo::Text);
  DrawLineEx({ rx + r * 0.4f, ry }, { rx - r * 0.3f, ry + r * 0.7f }, r * 0.24f, csgo::Text);
  float th = (float)S()->GetFontTall(FONT_GO);
  S()->DrawTextCentered(FONT_GO, opts[idx].c_str(), x + w * 0.5f, y + (h - th) * 0.5f, csgo::Text);
  if (Clicked(x, y, w * 0.5f, h)) { idx = (idx + n - 1) % n; PlayClick(); return true; }
  if (Clicked(x + w * 0.5f, y, w * 0.5f, h)) { idx = (idx + 1) % n; PlayClick(); return true; }
  return false;
}

bool Slider(const void* id, float& v, float mn, float mx, float step, const char* fmt, float x, float y, float w, float h) {
  const float vw = 64;   // value box
  float tx = x, tw = w - vw - 10, ty = y + h * 0.5f;
  bool changed = false;
  if (In().mousePressed && Over(tx - 6, y, tw + 12, h)) s_activeSlider = id;
  if (s_activeSlider == id) {
    float t = std::clamp((In().mouse.x - tx) / tw, 0.f, 1.f);
    float nv = mn + t * (mx - mn);
    if (step > 0) nv = std::round(nv / step) * step;
    nv = std::clamp(nv, mn, mx);
    if (nv != v) { v = nv; changed = true; }
  }
  float t = mx > mn ? std::clamp((v - mn) / (mx - mn), 0.f, 1.f) : 0;
  DrawRectangleRec({ tx, ty - 2, tw, 4 }, csgo::Line);
  DrawRectangleRec({ tx, ty - 2, tw * t, 4 }, csgo::Accent);
  DrawCircleV({ tx + tw * t, ty }, s_activeSlider == id ? 9.f : 7.f, s_activeSlider == id || Over(tx - 6, y, tw + 12, h) ? WHITE : csgo::Text);
  DrawRectangleRec({ x + w - vw, y + 3, vw, h - 6 }, csgo::PanelLight);
  float th = (float)S()->GetFontTall(FONT_GO);
  S()->DrawTextCentered(FONT_GO, TextFormat(fmt, v), x + w - vw * 0.5f, y + (h - th) * 0.5f, csgo::Text);
  return changed;
}

// scrollable region with wheel + finger drag
struct Scroll { float y = 0, target = 0, content = 0; };
void ScrollRegion(Scroll& s, float x, float y, float w, float h) {
  if (Over(x, y, w, h)) s.target -= In().wheel * 70.f;
  if (In().mouseDown && !s_activeSlider && PressedIn(x, y, w, h)) {
    float dy = In().mouse.y - s_lastMouse.y;
    if (!s_dragScroll && std::fabs(In().mouse.y - s_pressPos.y) > 10.f) s_dragScroll = true;
    if (s_dragScroll) s.target -= dy;
  }
  float maxY = std::max(0.f, s.content - h);
  s.target = std::clamp(s.target, 0.f, maxY);
  s.y += (s.target - s.y) * std::min(1.f, GetFrameTime() * 16.f);
  if (std::fabs(s.target - s.y) < 0.5f) s.y = s.target;
}
void ScrollBar(const Scroll& s, float x, float y, float w, float h) {   // after the content
  float maxY = std::max(0.f, s.content - h);
  if (maxY <= 0) return;
  float bh = std::max(30.f, h * h / s.content), by = y + (h - bh) * (s.y / maxY);
  DrawRectangleRec({ x + w + 2, by, 3, bh }, csgo::Alpha(csgo::Text, 0.4f));
}

// ------------------------------------------------------------------------------------------------
// maps
const char* MapTitle(const std::string& n) {
  static std::map<std::string, std::string> cache;
  auto it = cache.find(n);
  if (it == cache.end()) it = cache.emplace(n, PrettyMapName(n)).first;
  return it->second.c_str();
}
std::vector<MapListEntry> s_maps; float s_mapsTime = -100;
const std::vector<MapListEntry>& Maps() {
  if (GetTime() - s_mapsTime > 5.0 || s_maps.empty()) {
    s_maps = ListMaps(); s_mapsTime = (float)GetTime();
    // bomb defusal maps first (like the CS:GO map picker)
    std::stable_sort(s_maps.begin(), s_maps.end(), [](const MapListEntry& a, const MapListEntry& b) { return (a.name.rfind("de_", 0) == 0) > (b.name.rfind("de_", 0) == 0); });
  }
  return s_maps;
}
const Texture2D* Thumb(const std::string& map) {
  const Texture2D* t = VGUI_Image("materials/vgui/menu_thumb_" + map + ".vtf");
  return t ? t : VGUI_Image("materials/vgui/maps/menu_thumb_" + map + ".vtf");
}

// The real overview radar of the map (resource/overviews/<map>_radar.dds inside the map itself) - exactly the
// image CS:GO's map picker uses. Decoded once per map and cached, cropped to the part the map covers.
struct MapPreview { Texture2D tex{}; };
std::map<std::string, MapPreview> s_previews;
const MapPreview* Preview(const std::string& map) {
  auto it = s_previews.find(map);
  if (it != s_previews.end()) return it->second.tex.id ? &it->second : nullptr;
  MapPreview& p = s_previews[map];
  std::vector<uint8_t> rgba; int w = 0, h = 0; mapoverview::Info ov;
  // the picker shows the level itself: a top-down render of the map's own geometry, cached on disk after the first
  // build (a radar image says nothing about how the map actually looks)
  std::string cache = "content/cache/preview_" + map + ".png";
  bool have = false;
  if (FileExists(cache.c_str())) {
    Image img = LoadImage(cache.c_str());
    if (img.data) {
      ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
      w = img.width; h = img.height;
      rgba.assign((uint8_t*)img.data, (uint8_t*)img.data + (size_t)w * h * 4);
      UnloadImage(img);
      have = w > 0 && h > 0;
    }
  }
  if (!have) {
    have = mapoverview::RenderTopDown(map, rgba, w, h, 640);
    if (have) {
      std::error_code ec; std::filesystem::create_directories("content/cache", ec);
      Image img{ rgba.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
      ExportImage(img, cache.c_str());
    }
  }
  // no geometry (the map is not downloaded yet): the map's own overview radar is the only thing left
  if (!have && (!mapoverview::Load(map, rgba, w, h, ov) || w <= 0 || h <= 0)) return nullptr;
  int x0 = w, y0 = h, x1 = -1, y1 = -1;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    if (!rgba[((size_t)y * w + x) * 4 + 3]) continue;
    x0 = std::min(x0, x); x1 = std::max(x1, x); y0 = std::min(y0, y); y1 = std::max(y1, y);
  }
  if (x1 < x0 || y1 < y0) { x0 = y0 = 0; x1 = w - 1; y1 = h - 1; }
  int cw = x1 - x0 + 1, ch = y1 - y0 + 1;
  std::vector<uint8_t> crop((size_t)cw * ch * 4);
  for (int y = 0; y < ch; y++) std::memcpy(&crop[(size_t)y * cw * 4], &rgba[((size_t)(y + y0) * w + x0) * 4], (size_t)cw * 4);
  Image img{ crop.data(), cw, ch, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
  if (cw > 1024 || ch > 1024) ImageResize(&img, cw > ch ? 1024 : cw * 1024 / ch, ch >= cw ? 1024 : ch * 1024 / cw);
  p.tex = LoadTextureFromImage(img);
  SetTextureFilter(p.tex, TEXTURE_FILTER_BILINEAR);
  return p.tex.id ? &p : nullptr;
}
// draws the preview into dst, letterboxed on the map picker tile colour; false when the map has no overview
bool PaintPreview(const std::string& map, Rectangle dst, bool bright) {
  const MapPreview* p = Preview(map);
  if (!p) return false;
  float a = p->tex.width / (float)p->tex.height, box = dst.width / dst.height;
  float w = box > a ? dst.height * a : dst.width, h = box > a ? dst.height : dst.width / a;
  float x = dst.x + (dst.width - w) * 0.5f, y = dst.y + (dst.height - h) * 0.5f;
  DrawRectangleRec(dst, { 24, 24, 27, 255 });   // the radar itself is transparent outside the map
  DrawTexturePro(p->tex, { 0, 0, (float)p->tex.width, (float)p->tex.height }, { x, y, w, h }, { 0, 0 }, 0,
                 bright ? Color{ 255, 255, 255, 255 } : Color{ 205, 205, 205, 255 });
  return true;
}
const char* MapModeTag(const std::string& n) { return n.rfind("de_", 0) == 0 ? "BOMB DEFUSAL" : n.rfind("cs_", 0) == 0 ? "HOSTAGE RESCUE" : "CUSTOM"; }

void StartGame(const std::string& map, const std::string& hostname = "Counter-Strike: HvH Server") {
  const MatchSettings& m = s_ms;
  Cbuf_AddText(TextFormat("hostname \"%s\"\n", hostname.c_str()));
  Cbuf_AddText(TextFormat("mp_gamemode %d\nmp_roundtime %.2f\nmp_freezetime %d\nmp_maxrounds %d\nmp_startmoney %d\nmp_buytime %d\nmp_friendlyfire %d\nsv_cheats %d\nmp_autoteambalance %d\nmp_halftime %d\n",
    m.mode == 1 ? 1 : 0, s_roundTimes[m.roundTime], (int)m.freezeTime, s_maxRounds[m.maxRounds], s_startMoney[m.startMoney], (int)m.buyTime, m.friendlyFire, m.cheats, m.autoBalance, m.halftime));
  Cbuf_AddText(TextFormat("bot_quota %d\nbot_difficulty %d\nbot_join_team %s\nbot_hvh %d\n", (int)m.bots, m.botDifficulty, m.botTeam == 1 ? "T" : m.botTeam == 2 ? "CT" : "any", (m.hvhBots || m.mode == 2) ? 1 : 0));
  Cbuf_AddText("map " + map + "\n");
  s_panel = P_HOME;
  gameui::ForceHide();
}

// ------------------------------------------------------------------------------------------------
// fake community servers (offline game: "connecting" starts a local server with bots)
struct FakeServer { std::string name, map; int players, maxPlayers, ping; bool password, hvh, dm; };
std::vector<FakeServer> s_servers; float s_refreshTime = 0; int s_serverSel = -1; Scroll s_serverScroll;
int s_connecting = -1; float s_connectStart = 0;
void BuildServerList() {
  const auto& maps = Maps(); if (maps.empty()) return;
  const char* names[] = {
    "[RU] HvH.SU | 64 tick | Nospread | Public #1", "gamesense.pub HvH | high fps", "neverlose.cc official HvH | EU", "[EU] onetap.su HvH | 16k money",
    "<<< FATALITY.WIN HVH >>> 1v1 arena", "HvH | aimware.net | DM FFA", "[RU] ТОЛЬКО ЧИТЕРЫ | HvH 5x5", "skeet.cc mm hvh | 13+ choke allowed",
    "Moscow HvH Community | dust2 only", "legendware HvH | noob friendly", "pandora.cc HvH | rage only", "[UA] Kyiv HvH | public",
    "HVH.LV | Latvian HvH | 100 fps", "nixware HvH arena | free cfgs", "weave.su HvH server | DE", "[RU] Rostov HvH | ростов на дону",
    "Pub | dust2 24/7 | friendly", "HvH | 2v2 wingman style", "[PL] polska HvH | bez zasad", "fatal.club headshot only HvH",
    "HvH tournaments | qualifiers", "evolve hvh | auto snipers allowed", "dm.hvh.gg | deathmatch hvh", "Office 24/7 | hostages | fun",
  };
  s_servers.clear();
  CUniformRandomStream rs; rs.SetSeed((int)(GetTime() * 1000) & 0xffff);
  for (const char* n : names) {
    FakeServer s; s.name = n; s.map = maps[rs.RandomInt(0, (int)maps.size() - 1)].name;
    s.hvh = strstr(n, "HvH") || strstr(n, "HVH") || strstr(n, "hvh") || strstr(n, "ЧИТЕРЫ");
    s.dm = strstr(n, "DM") || strstr(n, "deathmatch") || strstr(n, "dm.");
    if (strstr(n, "dust2")) s.map = "de_dust2";
    if (strstr(n, "Office")) s.map = "cs_office";
    s.maxPlayers = rs.RandomInt(0, 3) == 0 ? 32 : (rs.RandomInt(0, 1) ? 20 : 10);
    s.players = rs.RandomInt(0, s.maxPlayers);
    s.ping = rs.RandomInt(8, 160); if (strstr(n, "[RU]") || strstr(n, "Rostov") || strstr(n, "Moscow")) s.ping = rs.RandomInt(8, 40);
    s.password = rs.RandomInt(0, 9) == 0;
    s_servers.push_back(s);
  }
  s_refreshTime = (float)GetTime(); s_serverSel = -1;
}

// ------------------------------------------------------------------------------------------------
// top bar
struct NavItem { const char* label; Panel panel; void (*icon)(float, float, float, Color); };
void PaintTopBar(bool ingame) {
  const float W = (float)GetScreenWidth();
  DrawRectangleRec({ 0, 0, W, TB }, csgo::TopBar);
  DrawRectangleRec({ 0, TB - 1, W, 1 }, csgo::Alpha(csgo::Line, 0.6f));
  float x = 0;
  // quit + settings icons
  auto iconBtn = [&](const char* id, void (*icon)(float, float, float, Color), bool active) {
    bool hv = Over(x, 0, TB, TB); HoverSound(id, hv);
    if (hv || active) DrawRectangleRec({ x, 0, TB, TB }, csgo::Alpha(csgo::PanelHover, hv ? 1.f : 0.7f));
    icon(x + TB * 0.5f, TB * 0.5f, TB * 0.24f, hv || active ? WHITE : csgo::TextDim);
    bool c = Clicked(x, 0, TB, TB); if (c) PlayClick();
    x += TB; return c;
  };
  static bool s_quitAsk = false;
  if (iconBtn("quit", IconPower, false)) s_quitAsk = true;
  if (iconBtn("settings", IconGear, s_panel == P_SETTINGS)) s_panel = s_panel == P_SETTINGS ? P_HOME : P_SETTINGS;
  DrawRectangleRec({ x + 6, 12, 1, TB - 24 }, csgo::Line); x += 14;
  static const NavItem nav[] = { { "PLAY", P_PLAY, IconPlay }, { "LOADOUT", P_LOADOUT, IconGun }, { "SERVERS", P_SERVERS, IconGlobe } };
  for (const NavItem& n : nav) {
    float tw = (float)S()->GetTextWide(FONT_GO_BOLD, n.label), w = tw + 56;
    bool active = s_panel == n.panel, hv = Over(x, 0, w, TB);
    HoverSound(TextFormat("nav:%s", n.label), hv);
    if (hv) DrawRectangleRec({ x, 0, w, TB }, csgo::Alpha(csgo::PanelHover, 0.8f));
    Color c = active || hv ? WHITE : csgo::TextDim;
    n.icon(x + 20, TB * 0.5f, 9, c);
    S()->DrawText(FONT_GO_BOLD, n.label, x + 36, TB * 0.5f - S()->GetFontTall(FONT_GO_BOLD) * 0.5f, c);
    if (active) DrawRectangleRec({ x + 8, TB - 4, w - 16, 3 }, csgo::Accent);
    if (Clicked(x, 0, w, TB)) { PlayClick(); s_panel = active ? P_HOME : n.panel; if (s_panel == P_SERVERS && s_servers.empty()) BuildServerList(); }
    x += w;
  }
  // console
  if (iconBtn("console", IconConsole, gameui::IsConsoleVisible())) gameui::ToggleConsole();
  // right side: in-game buttons + profile
  float rx = W - 12;
  const char* name = FindConVar("name")->GetString();
  float nw = (float)std::max(S()->GetTextWide(FONT_GO_BOLD, name), S()->GetTextWide(FONT_GO, "aimwhere"));
  rx -= 36; DrawRectangleRec({ rx, TB * 0.5f - 18, 36, 36 }, csgo::Accent);
  char ini[2] = { (char)toupper(name[0] ? name[0] : 'P'), 0 };
  S()->DrawTextCentered(FONT_GO_TITLE, ini, rx + 18, TB * 0.5f - S()->GetFontTall(FONT_GO_TITLE) * 0.5f, WHITE);
  rx -= nw + 10; S()->DrawText(FONT_GO_BOLD, name, rx, TB * 0.5f - 15, csgo::Text);
  S()->DrawText(FONT_GO, "aimwhere", rx, TB * 0.5f + 2, csgo::Gold);
  rx -= 16;
  if (ingame) {
    rx -= 130; if (FlatButton("DISCONNECT", rx, 10, 130, TB - 20, csgo::Alpha(csgo::Red, 0.75f), csgo::Red)) { Cbuf_AddText("disconnect\n"); s_panel = P_HOME; }
    rx -= 120; if (FlatButton("RESUME", rx, 10, 112, TB - 20, csgo::Alpha(csgo::Green, 0.85f), csgo::GreenHover)) { s_panel = P_HOME; gameui::Hide(); }
  }
  // quit confirmation
  if (s_quitAsk) {
    const float H = (float)GetScreenHeight();
    DrawRectangleRec({ 0, 0, W, H }, { 0, 0, 0, 160 });
    float w = 420, h = 170, px = (W - w) * 0.5f, py = (H - h) * 0.5f;
    DrawRectangleRec({ px, py, w, h }, csgo::Panel); DrawRectangleRec({ px, py, w, 3 }, csgo::Red);
    S()->DrawText(FONT_GO_TITLE, "QUIT", px + 22, py + 20, csgo::Text);
    S()->DrawText(FONT_GO, "Are you sure you want to quit Counter-Strike: HvH?", px + 22, py + 60, csgo::TextDim);
    if (FlatButton("QUIT", px + w - 250, py + h - 56, 110, 38, csgo::Alpha(csgo::Red, 0.85f), csgo::Red)) g_Host.wantQuit = true;
    if (FlatButton("CANCEL", px + w - 130, py + h - 56, 110, 38, csgo::PanelLight, csgo::PanelHover)) s_quitAsk = false;
    if (In().KeyPressed(KEY_ESCAPE)) { s_quitAsk = false; ConsumeKey(KEY_ESCAPE); }
  }
}

// ------------------------------------------------------------------------------------------------
// home: logo, news, agent switcher, PLAY
void PaintHome(bool ingame) {
  const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
  // left shade for readability
  DrawRectangleGradientH(0, (int)TB, (int)(W * 0.5f), (int)(H - TB), { 0, 0, 0, 150 }, { 0, 0, 0, 0 });
  DrawRectangleGradientV(0, (int)(H - 180), (int)W, 180, { 0, 0, 0, 0 }, { 0, 0, 0, 170 });
  float x = 40, y = TB + 34;
  S()->DrawTextShadow(FONT_GO_HUGE, "COUNTER-STRIKE", x, y, csgo::Text, { 0, 0, 0, 160 });
  float lw = (float)S()->GetTextWide(FONT_GO_HUGE, "COUNTER-STRIKE");
  S()->DrawTextShadow(FONT_GO_HUGE, "HvH", x + lw + 14, y, csgo::Gold, { 0, 0, 0, 160 });
  y += S()->GetFontTall(FONT_GO_HUGE) + 6;
  S()->DrawText(FONT_GO_BOLD, "HACK VS HACK  -  POWERED BY AIMWHERE", x + 2, y, csgo::TextDim);
  y += 40;
  // news card
  if (H > 520) {
    float cw = std::min(400.f, W * 0.36f), chh = std::min(230.f, H - y - 120);
    if (chh > 120) {
      DrawRectangleRec({ x, y, cw, chh }, csgo::Alpha(csgo::Panel, 0.92f));
      DrawRectangleRec({ x, y, 3, chh }, csgo::Accent);
      S()->DrawText(FONT_GO_BOLD, "WHAT'S NEW", x + 16, y + 14, csgo::Accent);
      const char* lines[] = { "- New vertigo / inferno / mirage with their own props and materials", "- Hitboxes from the CS:S player models", "- Radial buy menu, CS:GO style menus",
                              "- Phones: content download, bind circles", "- Press INSERT (or AW) in game for aimwhere" };
      float ly = y + 42;
      for (const char* l : lines) { if (ly > y + chh - 20) break; S()->DrawText(FONT_GO, l, x + 16, ly, csgo::Text); ly += 24; }
    }
  }
  // agent switcher removed - now only in LOADOUT screen
  // big PLAY / RESUME
  float bw = 230, bh = 62, bx = W - bw - 34, by = H - bh - 34;
  if (ingame) { if (FlatButton("RESUME", bx, by, bw, bh, csgo::Green, csgo::GreenHover, WHITE, FONT_GO_TITLE)) gameui::Hide(); }
  else if (FlatButton("PLAY", bx, by, bw, bh, csgo::Green, csgo::GreenHover, WHITE, FONT_GO_TITLE)) s_panel = P_PLAY;
  S()->DrawText(FONT_GO, "Counter-Strike: HvH 1.2", 14, H - 24, csgo::TextFaint);
}

// ------------------------------------------------------------------------------------------------
// panel frame
void PanelBg(float x, float y, float w, float h, const char* title) {
  DrawRectangleRec({ x, y, w, h }, { 16, 18, 22, 236 });
  S()->DrawText(FONT_GO_TITLE, title, x + 26, y + 18, csgo::Text);
  DrawRectangleRec({ x + 26, y + 50, w - 52, 1 }, csgo::Line);
}

// settings-style row (label left, control right)
struct RowCtx { float x, y, w, ctrlX, ctrlW, top, bottom; int n; } s_row;
const float ROW_H = 40;
bool RowVisible() { return s_row.y >= s_row.top - 1 && s_row.y + ROW_H <= s_row.bottom + 1; }   // whole rows only (no scissor)
bool RowBegin(const char* label) {
  bool vis = RowVisible();
  if (vis) {
    if (s_row.n & 1) DrawRectangleRec({ s_row.x, s_row.y, s_row.w, ROW_H }, { 255, 255, 255, 8 });
    if (Over(s_row.x, s_row.y, s_row.w, ROW_H) && In().mouse.y > s_row.top && In().mouse.y < s_row.bottom) DrawRectangleRec({ s_row.x, s_row.y, s_row.w, ROW_H }, { 255, 255, 255, 14 });
    S()->DrawText(FONT_GO, label, s_row.x + 14, s_row.y + (ROW_H - S()->GetFontTall(FONT_GO)) * 0.5f, csgo::Text);
  }
  s_row.n++;
  return vis;
}
void RowEnd() { s_row.y += ROW_H; }
void Header(const char* t) {
  s_row.y += 10;
  if (s_row.y >= s_row.top - 1 && s_row.y + 30 <= s_row.bottom + 1) {
    S()->DrawText(FONT_GO_BOLD, t, s_row.x + 4, s_row.y + 6, csgo::Accent);
    DrawRectangleRec({ s_row.x, s_row.y + 28, s_row.w, 1 }, csgo::Alpha(csgo::Line, 0.7f));
  }
  s_row.y += 34; s_row.n = 0;
}
// value rows
void RowToggle(const char* label, bool& v) { if (RowBegin(label)) Toggle(v, s_row.ctrlX + s_row.ctrlW - 150, s_row.y + 6, 150, ROW_H - 12); RowEnd(); }
void RowChoice(const char* label, int& v, const std::vector<std::string>& o) { if (RowBegin(label)) Choice(v, o, s_row.ctrlX, s_row.y + 5, s_row.ctrlW, ROW_H - 10); RowEnd(); }
void RowSlider(const char* label, float& v, float mn, float mx, float step, const char* fmt) { if (RowBegin(label)) Slider(&v, v, mn, mx, step, fmt, s_row.ctrlX, s_row.y + 4, s_row.ctrlW, ROW_H - 8); RowEnd(); }
// cvar rows
void CvToggle(const char* label, const char* cv) {
  ConVar* c = FindConVar(cv); if (!c) return;
  bool v = c->GetBool();
  if (RowBegin(label) && Toggle(v, s_row.ctrlX + s_row.ctrlW - 150, s_row.y + 6, 150, ROW_H - 12)) c->SetValue(v ? 1 : 0);
  RowEnd();
}
void CvSlider(const char* label, const char* cv, float mn, float mx, float step, const char* fmt) {
  ConVar* c = FindConVar(cv); if (!c) return;
  float v = c->GetFloat();
  if (RowBegin(label) && Slider(c, v, mn, mx, step, fmt, s_row.ctrlX, s_row.y + 4, s_row.ctrlW, ROW_H - 8)) c->SetValue(v);
  RowEnd();
}
void CvChoice(const char* label, const char* cv, const std::vector<std::pair<const char*, const char*>>& opts) {
  ConVar* c = FindConVar(cv); if (!c) return;
  int idx = 0; float best = 1e30f;
  for (size_t i = 0; i < opts.size(); i++) {
    float d = std::fabs((float)atof(opts[i].second) - c->GetFloat());
    if (!strcmp(opts[i].second, c->GetString())) { idx = (int)i; break; }
    if (d < best) { best = d; idx = (int)i; }
  }
  std::vector<std::string> o; for (auto& p : opts) o.push_back(p.first);
  if (RowBegin(label) && Choice(idx, o, s_row.ctrlX, s_row.y + 5, s_row.ctrlW, ROW_H - 10)) c->SetValue(opts[idx].second);
  RowEnd();
}
void RowButton(const char* label, const char* btn, const std::function<void()>& fn) {
  if (RowBegin(label) && FlatButton(btn, s_row.ctrlX + s_row.ctrlW - 200, s_row.y + 5, 200, ROW_H - 10, csgo::PanelLight, csgo::PanelHover)) fn();
  RowEnd();
}
void RowInfo(const char* label, const char* value) {
  if (RowBegin(label)) S()->DrawTextRight(FONT_GO, value, s_row.ctrlX + s_row.ctrlW, s_row.y + (ROW_H - S()->GetFontTall(FONT_GO)) * 0.5f, csgo::TextDim);
  RowEnd();
}
void RowKey(const char* label, const char* cmd) {
  if (RowBegin(label)) {
    int k = Key_FindBinding(cmd);
    bool cap = s_captureKey == cmd;
    float bx = s_row.ctrlX + s_row.ctrlW - 200, by = s_row.y + 5, bw = 200, bh = ROW_H - 10;
    bool hv = Over(bx, by, bw, bh);
    DrawRectangleRec({ bx, by, bw, bh }, cap ? csgo::Accent : hv ? csgo::PanelHover : csgo::PanelLight);
    std::string t = cap ? "PRESS A KEY..." : k >= 0 ? Key_KeynumToString(k) : "-";
    for (auto& ch : t) ch = (char)toupper(ch);
    S()->DrawTextCentered(FONT_GO_BOLD, t.c_str(), bx + bw * 0.5f, by + (bh - S()->GetFontTall(FONT_GO_BOLD)) * 0.5f, csgo::Text);
    if (Clicked(bx, by, bw, bh) && !cap) { s_captureKey = cmd; s_captureCmd = cmd; PlayClick(); }
  }
  RowEnd();
}

// ------------------------------------------------------------------------------------------------
// PLAY
Scroll s_mapScroll, s_matchScroll;
void PaintPlay() {
  const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
  float px = 0, py = TB, pw = W, ph = H - TB;
  PanelBg(px, py, pw, ph, "PLAY  -  OFFLINE WITH BOTS");
  // game mode pills
  static const char* modes[] = { "COMPETITIVE", "DEATHMATCH", "HVH" };
  float mx = px + pw - 26;
  for (int i = 2; i >= 0; i--) {
    float w = (float)S()->GetTextWide(FONT_GO_BOLD, modes[i]) + 34; mx -= w + 6;
    if (FlatButton(modes[i], mx, py + 12, w, 32, s_ms.mode == i ? csgo::Alpha(csgo::Accent, 0.95f) : csgo::PanelLight, csgo::PanelHover)) {
      s_ms.mode = i;
      if (i == 1) { s_ms.freezeTime = 0; s_ms.roundTime = 5; }
      else { s_ms.freezeTime = 6; s_ms.roundTime = 2; }
      s_ms.hvhBots = i == 2;
    }
  }
  const float sideW = std::min(380.f, pw * 0.38f);
  // map tiles
  float gx = px + 26, gy = py + 64, gw = pw - sideW - 26 * 2 - 16, gh = ph - 64 - 16;
  const auto& maps = Maps();
  int cols = std::max(1, (int)((gw + 12) / (210 + 12)));
  float tw = (gw - (cols - 1) * 12) / cols, th = tw * 0.62f + 30;
  int rows = ((int)maps.size() + cols - 1) / cols;
  s_mapScroll.content = rows * (th + 12);
  ScrollRegion(s_mapScroll, gx, gy, gw, gh);
  for (size_t i = 0; i < maps.size(); i++) {
    int r = (int)i / cols, c = (int)i % cols;
    float x = gx + c * (tw + 12), y = gy + r * (th + 12) - s_mapScroll.y;
    if (y + th < gy || y > gy + gh) continue;
    // clip by hand at the region edges (no scissor: it is offset on some Android surfaces)
    const float vy0 = std::max(y, gy), vy1 = std::min(y + th, gy + gh);
    const bool full = vy0 <= y + 0.5f && vy1 >= y + th - 0.5f;
    const std::string& n = maps[i].name;
    bool sel = n == s_ms.map, hv = Over(x, vy0, tw, vy1 - vy0) && Over(gx, gy, gw, gh);
    DrawRectangleRec({ x, vy0, tw, vy1 - vy0 }, hv ? csgo::PanelHover : csgo::Panel);
    float ih = th - 30;
    { // picture, cropped to the visible part
      float iy0 = std::max(y, vy0), iy1 = std::min(y + ih, vy1);
      if (iy1 > iy0 && !(full && PaintPreview(n, { x, y, tw, ih }, hv || sel))) {
        if (const Texture2D* t = Thumb(n)) {
          float sh = t->height * 0.75f, u0 = (iy0 - y) / ih, u1 = (iy1 - y) / ih;
          DrawTexturePro(*t, { 0, sh * u0, (float)t->width, sh * (u1 - u0) }, { x, iy0, tw, iy1 - iy0 }, { 0, 0 }, 0, hv || sel ? WHITE : Color{ 200, 200, 200, 255 });
        } else DrawRectangleRec({ x, iy0, tw, iy1 - iy0 }, { 52, 48, 40, 255 });
      }
    }
    if (y + ih + 26 <= vy1 && y + ih >= vy0) {
      S()->DrawText(FONT_GO_BOLD, MapTitle(n), x + 10, y + ih + 7, csgo::Text);
      S()->DrawTextRight(FONT_GO, MapModeTag(n), x + tw - 8, y + ih + 7, csgo::TextFaint);
    }
    if (!Thumb(n) && !s_previews.count(n) && y + ih * 0.5f > vy0 && y + ih * 0.5f + 20 < vy1) S()->DrawTextCentered(FONT_GO_TITLE, MapTitle(n), x + tw * 0.5f, y + ih * 0.5f - 12, csgo::Alpha(csgo::Text, 0.6f));
    if (sel && full) { DrawRectangleLinesEx({ x, y, tw, th }, 3, csgo::Accent); DrawRectangleRec({ x + tw - 30, y + 6, 24, 24 }, csgo::Accent); DrawLineEx({ x + tw - 25, y + 18 }, { x + tw - 20, y + 24 }, 3, WHITE); DrawLineEx({ x + tw - 20, y + 24 }, { x + tw - 11, y + 11 }, 3, WHITE); }
    if (Clicked(x, vy0, tw, vy1 - vy0) && Over(gx, gy, gw, gh)) { s_ms.map = n; PlayClick(); }
  }
  ScrollBar(s_mapScroll, gx, gy, gw, gh);
  if (maps.empty()) S()->DrawText(FONT_GO, "No maps found. Settings > Content: download the Counter-Strike: Source content.", gx, gy + 10, csgo::TextDim);
  // match settings column
  float sx = px + pw - sideW - 26, sy = py + 64, sh = ph - 64 - 90;
  DrawRectangleRec({ sx, sy, sideW, sh }, csgo::Alpha(csgo::Panel, 0.85f));
  s_row = { sx + 4, sy + 4 - s_matchScroll.y, sideW - 12, sx + sideW * 0.46f, sideW * 0.54f - 14, sy, sy + sh, 0 };
  ScrollRegion(s_matchScroll, sx, sy, sideW, sh);
  float startY = s_row.y;
  Header("BOTS");
  RowSlider("Bots", s_ms.bots, 0, 31, 1, "%.0f");
  RowChoice("Difficulty", s_ms.botDifficulty, { "Easy", "Normal", "Hard", "Expert" });
  RowChoice("Bots join", s_ms.botTeam, { "Both teams", "Terrorists", "Counter-T" });
  RowToggle("HvH bots", s_ms.hvhBots);
  Header("MATCH");
  RowChoice("Round time", s_ms.roundTime, { "1:00", "1:30", "1:55", "2:15", "3:00", "5:00" });
  RowSlider("Freeze time", s_ms.freezeTime, 0, 15, 1, "%.0f s");
  RowChoice("Max rounds", s_ms.maxRounds, { "8", "16", "24", "30", "No limit" });
  RowChoice("Start money", s_ms.startMoney, { "$800", "$1000", "$4000", "$10000", "$16000" });
  RowSlider("Buy time", s_ms.buyTime, 5, 90, 5, "%.0f s");
  RowToggle("Friendly fire", s_ms.friendlyFire);
  RowToggle("Team auto-balance", s_ms.autoBalance);
  RowToggle("Halftime", s_ms.halftime);
  RowToggle("Cheats (sv_cheats)", s_ms.cheats);
  s_matchScroll.content = s_row.y - startY + 10;
  ScrollBar(s_matchScroll, sx, sy, sideW - 6, sh);
  // GO
  float bw = sideW, bh = 60, bx = sx, by = py + ph - bh - 16;
  S()->DrawText(FONT_GO, TextFormat("%s  -  %s", MapTitle(s_ms.map), modes[s_ms.mode]), bx, by - 22, csgo::TextDim);
  if (FlatButton("GO", bx, by, bw, bh, csgo::Green, csgo::GreenHover, WHITE, FONT_GO_TITLE) && !maps.empty()) {
    bool found = false; for (auto& m : maps) found |= m.name == s_ms.map;
    StartGame(found ? s_ms.map : maps[0].name);
  }
}

// ------------------------------------------------------------------------------------------------
// AGENTS (narrow panel on the left: the agent stays visible on the right)
void PaintAgents() {
  const float H = (float)GetScreenHeight();
  float pw = std::min(440.f, GetScreenWidth() * 0.45f), px = 0, py = TB, ph = H - TB;
  PanelBg(px, py, pw, ph, "AGENTS");
  ConVar* teamCv = FindConVar("cl_menu_team");
  int team = teamCv->GetInt();
  float tx = px + 26, ty = py + 64, hw = (pw - 52 - 8) * 0.5f;
  if (FlatButton("COUNTER-TERRORISTS", tx, ty, hw, 36, team == TEAM_CT ? csgo::Alpha(csgo::Accent, 0.95f) : csgo::PanelLight, csgo::PanelHover)) teamCv->SetValue(TEAM_CT);
  if (FlatButton("TERRORISTS", tx + hw + 8, ty, hw, 36, team == TEAM_TERRORIST ? csgo::Alpha(csgo::Gold, 0.95f) : csgo::PanelLight, csgo::PanelHover)) teamCv->SetValue(TEAM_TERRORIST);
  team = teamCv->GetInt();
  ConVar* agent = FindConVar(team == TEAM_CT ? "cl_agent_ct" : "cl_agent_t");
  static const char* descCT[4] = { "Naval Special Warfare (USA)", "Grenzschutzgruppe 9 (Germany)", "Special Air Service (UK)", "Gendarmerie (France)" };
  static const char* descT[4] = { "Phoenix Connexion", "Elite Crew", "Arctic Avengers", "Guerilla Warfare" };
  float cy = ty + 52, ch = std::min(74.f, (ph - (cy - py) - 70) / 4 - 8);
  for (int i = 0; i < 4; i++) {
    float y = cy + i * (ch + 8);
    bool sel = agent->GetInt() == i, hv = Over(tx, y, pw - 52, ch);
    DrawRectangleRec({ tx, y, pw - 52, ch }, sel ? csgo::Alpha(csgo::Accent, 0.35f) : hv ? csgo::PanelHover : csgo::Panel);
    if (sel) DrawRectangleRec({ tx, y, 4, ch }, csgo::Accent);
    // rarity stripe like the CS:GO agent cards
    static const Color rar[4] = { { 136, 71, 255, 255 }, { 75, 105, 255, 255 }, { 211, 44, 230, 255 }, { 235, 75, 75, 255 } };
    DrawRectangleRec({ tx + pw - 52 - 6, y, 6, ch }, rar[i]);
    static const char* imgCT[4] = { "urban", "gsg9", "sas", "gign" };
    static const char* imgT[4] = { "terror", "leet", "arctic", "guerilla" };
    float textX = tx + 18;
    if (const Texture2D* img = VGUI_ItemImage(team == TEAM_CT ? imgCT[i] : imgT[i])) { VGUI_DrawImageFit(*img, tx + 10, y + 4, ch * 1.3f, ch - 8); textX = tx + 22 + ch * 1.3f; }
    S()->DrawText(FONT_GO_BOLD, CSS_AgentName(team, i), textX, y + ch * 0.5f - 18, csgo::Text);
    S()->DrawText(FONT_GO, team == TEAM_CT ? descCT[i] : descT[i], textX, y + ch * 0.5f + 2, csgo::TextDim);
    if (Clicked(tx, y, pw - 52, ch)) { agent->SetValue(i); PlayClick(); }
  }
  S()->DrawText(FONT_GO, "Your agent is used for your player model in game.", tx, py + ph - 34, csgo::TextFaint);
}

// ------------------------------------------------------------------------------------------------
// SERVERS
// LOADOUT (CS2-style): loadout left, agent right
static int s_dragWeapon = 0;  // weapon being dragged (0 = none)
static float s_dragStartX = 0, s_dragStartY = 0;

void PaintLoadout() {
  const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
  float pw = W * 0.45f, px = 10.f, py = TB, ph = H - TB;  // loadout panel: 45% width, left-aligned
  float agentW = W - pw - 20.f;
  float agentX = px + pw + 10.f;
  
  PanelBg(px, py, pw, ph, "LOADOUT");
  
  ConVar* teamCv = FindConVar("cl_menu_team");
  int team = teamCv->GetInt() == TEAM_TERRORIST ? TEAM_TERRORIST : TEAM_CT;
  
  const float startY = py + 50.f;
  const float bottomY = py + ph * 0.65f;  // AVAILABLE line higher (65% down)
  const float slotH = 75.f;  // taller slots
  const float colW = (pw - 40.f) / 3.f;
  
  // 3 columns: PISTOLS / MID-TIER / RIFLES
  for (int c = 0; c < 3; c++) {
    float x = px + 20.f + c * (colW + 10.f);
    S()->DrawText(FONT_GO_BOLD, Loadout_CategoryName(c), x, startY, csgo::TextDim);
    
    std::vector<LoadoutSlot2> slots = Loadout_GetSlots2(team);
    std::vector<int> slotWeapons;
    for (const auto& s : slots) {
      if (s.category == c) slotWeapons.push_back(s.weaponId);
    }
    
    for (int i = 0; i < 5; i++) {
      float y = startY + 28.f + i * (slotH + 4.f);
      
      int weaponId = (i < (int)slotWeapons.size()) ? slotWeapons[i] : 0;
      const CCSWeaponInfo* wi = weaponId ? GetWeaponInfo(weaponId) : nullptr;
      bool isLocked = (c == 0 && i == 0);
      bool hv = Over(x, y, colW, slotH) && !isLocked && s_dragWeapon == 0;
      
      std::string hid = "lo_" + std::to_string(c) + "_" + std::to_string(i);
      HoverSound(hid.c_str(), hv);
      
      // Highlight if dragging over this slot
      bool dragOver = s_dragWeapon != 0 && Over(x, y, colW, slotH);
      DrawRectangleRec({ x, y, colW, slotH }, dragOver ? csgo::Alpha(csgo::Accent, 0.5f) : hv ? csgo::PanelHover : isLocked ? csgo::Alpha(csgo::Panel, 0.6f) : csgo::Panel);
      
      // For locked slot, show default pistol (Glock for T, USP for CT)
      int displayWeaponId = isLocked ? (team == TEAM_TERRORIST ? WEAPON_GLOCK : WEAPON_USP_SILENCER) : weaponId;
      const CCSWeaponInfo* displayWi = displayWeaponId ? GetWeaponInfo(displayWeaponId) : nullptr;
      
      if (displayWi) {
        float imgH = slotH - 30.f;
        Color tint = isLocked ? csgo::TextDim : WHITE;
        if (const Texture2D* img = VGUI_WeaponImage(displayWeaponId))
          VGUI_DrawImageFit(*img, x + 10.f, y + 6.f, colW - 20.f, imgH, tint);
        S()->DrawText(FONT_GO_BOLD, displayWi->printName, x + 10.f, y + slotH - 20.f, isLocked ? csgo::TextDim : csgo::Text);
        if (isLocked) {
          S()->DrawText(FONT_GO, "DEFAULT", x + colW - 80.f, y + 10.f, csgo::TextDim);
        }
      }
      
      // Drop here when released over slot
      if (s_dragWeapon != 0 && !isLocked && dragOver && !In().mouseDown) {
        PlayClick();
        Loadout_SetWeapon2(team, c, i, s_dragWeapon);
        s_dragWeapon = 0;
      }
      
      // Cancel drag if released anywhere
      if (s_dragWeapon != 0 && !In().mouseDown && !dragOver) {
        s_dragWeapon = 0;
      }
    }
  }
  
  // AVAILABLE line
  DrawRectangleRec({ px + 10.f, bottomY, pw - 20.f, 2.f }, csgo::Line);
  S()->DrawText(FONT_GO_BOLD, "AVAILABLE — drag to slot", px + 20.f, bottomY + 10.f, csgo::TextDim);
  
  // Available weapons (5 per row, scrollable)
  std::vector<int> allWeapons;
  for (int c = 0; c < 3; c++) {
    std::vector<int> catWeapons = Loadout_CategoryWeapons2(c, team);
    for (int w : catWeapons) {
      if (!Loadout_IsEquipped2(team, w) && std::find(allWeapons.begin(), allWeapons.end(), w) == allWeapons.end()) {
        allWeapons.push_back(w);
      }
    }
  }
  
  float aW = (pw - 50.f) / 5.f;  // 5 items per row
  float aRowH = 50.f;
  float ax = px + 25.f, ay = bottomY + 28.f;
  static int s_availScroll = 0;
  int maxRows = (int)((ph - (bottomY - py) - 40.f) / (aRowH + 4.f));
  int startIdx = s_availScroll * 5;
  
  for (int idx = startIdx; idx < (int)allWeapons.size() && idx < startIdx + maxRows * 5; idx++) {
    int w = allWeapons[idx];
    const CCSWeaponInfo* wi = GetWeaponInfo(w);
    if (!wi) continue;
    
    if (ax + aW > px + pw - 10.f) { ax = px + 25.f; ay += aRowH + 4.f; }
    if (ay + aRowH > py + ph - 10.f) break;
    
    bool hv = Over(ax, ay, aW, aRowH);
    std::string hid = "avail_" + std::to_string(w);
    HoverSound(hid.c_str(), hv);
    bool dragging = s_dragWeapon == w;
    DrawRectangleRec({ ax, ay, aW, aRowH }, dragging ? csgo::Alpha(csgo::Accent, 0.3f) : hv ? csgo::PanelHover : csgo::Alpha(csgo::Panel, 0.6f));
    if (const Texture2D* img = VGUI_WeaponImage(w))
      VGUI_DrawImageFit(*img, ax + 4.f, ay + 4.f, 42.f, aRowH - 8.f, dragging ? csgo::Alpha(WHITE, 0.5f) : WHITE);
    S()->DrawText(FONT_GO, wi->printName, ax + 48.f, ay + aRowH * 0.5f - 8.f, dragging ? csgo::TextDim : csgo::Text);
    
    // Start drag on hold
    if (In().mouseDown && Over(ax, ay, aW, aRowH) && s_dragWeapon == 0) {
      s_dragWeapon = w;
      PlayClick();
    }
    ax += aW + 4.f;
  }
  
  // Draw dragged weapon following cursor
  if (s_dragWeapon != 0) {
    const CCSWeaponInfo* wi = GetWeaponInfo(s_dragWeapon);
    if (wi) {
      float dw = 80.f, dh = 50.f;
      float dx = In().mouse.x - dw * 0.5f, dy = In().mouse.y - dh * 0.5f;
      DrawRectangleRec({ dx, dy, dw, dh }, csgo::Alpha(csgo::Accent, 0.8f));
      if (const Texture2D* img = VGUI_WeaponImage(s_dragWeapon))
        VGUI_DrawImageFit(*img, dx + 4.f, dy + 4.f, dw - 8.f, dh - 8.f, WHITE);
    }
  }
  
  // Scroll buttons
  if (startIdx > 0) {
    if (FlatButton("▲", px + pw - 60.f, bottomY + 8.f, 40.f, 20.f, csgo::Panel, csgo::PanelHover)) s_availScroll = std::max(0, s_availScroll - 1);
  }
  if (startIdx + maxRows * 5 < (int)allWeapons.size()) {
    if (FlatButton("▼", px + pw - 60.f, py + ph - 30.f, 40.f, 20.f, csgo::Panel, csgo::PanelHover)) s_availScroll++;
  }
  
  // RIGHT: Agent preview + team selector (no title, just render)
  // Team buttons
  float bw = 180.f, bx = agentX, by = py + 40.f;
  if (FlatButton("COUNTER-TERRORISTS", bx, by, bw, 34, team == TEAM_CT ? csgo::Alpha(csgo::Accent, 0.95f) : csgo::PanelLight, csgo::PanelHover)) teamCv->SetValue(TEAM_CT);
  if (FlatButton("TERRORISTS", bx + bw + 8.f, by, bw, 34, team == TEAM_TERRORIST ? csgo::Alpha(csgo::Gold, 0.95f) : csgo::PanelLight, csgo::PanelHover)) teamCv->SetValue(TEAM_TERRORIST);
  team = teamCv->GetInt() == TEAM_TERRORIST ? TEAM_TERRORIST : TEAM_CT;
  
  // Agent model preview
  ConVar* agentCv = FindConVar("cl_menu_agent");
  int agent = agentCv ? agentCv->GetInt() : 0;
  
  // Agent selector arrows (draw before preview so they're on top)
  if (FlatButton("<", agentX + 20.f, py + ph - 80.f, 40.f, 40.f, csgo::Panel, csgo::PanelHover)) {
    if (agentCv) {
      int newAgent = (agent + 3) % 4;
      agentCv->SetValue(newAgent);
      agent = newAgent;  // Update local variable immediately
    }
  }
  S()->DrawText(FONT_GO_BOLD, CSS_AgentName(team, agent), agentX + 70.f, py + ph - 70.f, csgo::Text);
  if (FlatButton(">", agentX + agentW - 60.f, py + ph - 80.f, 40.f, 40.f, csgo::Panel, csgo::PanelHover)) {
    if (agentCv) {
      int newAgent = (agent + 1) % 4;
      agentCv->SetValue(newAgent);
      agent = newAgent;  // Update local variable immediately
    }
  }
  
  PlayerPoseInput in;
  in.origin = Vector(agentX + agentW * 0.5f, py + ph * 0.5f, 0);
  in.footYaw = 180.f;
  in.eyeAngles = QAngle(0, 180.f, 0);
  in.duckAmount = 0;
  in.moveCycle = 0;
  in.moveSpeed = 0;
  in.moveYaw = 0;
  in.onGround = true;
  in.weaponId = 0;
  CSS_DrawAgent(team, agent, in, {});
}


void PaintServers() {
  const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
  float px = 0, py = TB, pw = W, ph = H - TB;
  PanelBg(px, py, pw, ph, "COMMUNITY SERVER BROWSER");
  if (s_servers.empty()) BuildServerList();
  float lx = px + 26, ly = py + 64, lw = pw - 52, lh = ph - 64 - 80;
  // columns
  float cMap = lw - 330, cPl = lw - 190, cPing = lw - 80;
  if (lw < 700) { cMap = lw - 250; cPl = lw - 140; cPing = lw - 60; }
  S()->DrawText(FONT_GO_BOLD, "SERVER", lx + 12, ly, csgo::TextDim);
  S()->DrawText(FONT_GO_BOLD, "MAP", lx + cMap, ly, csgo::TextDim);
  S()->DrawText(FONT_GO_BOLD, "PLAYERS", lx + cPl, ly, csgo::TextDim);
  S()->DrawText(FONT_GO_BOLD, "PING", lx + cPing, ly, csgo::TextDim);
  ly += 26; lh -= 26;
  size_t shown = std::min(s_servers.size(), (size_t)((GetTime() - s_refreshTime) * 16));
  const float rh = 36;
  s_serverScroll.content = shown * rh;
  ScrollRegion(s_serverScroll, lx, ly, lw, lh);
  for (size_t i = 0; i < shown; i++) {
    float y = ly + i * rh - s_serverScroll.y;
    if (y < ly - 0.5f || y + rh > ly + lh + 0.5f) continue;   // whole rows only
    const FakeServer& s = s_servers[i];
    bool sel = s_serverSel == (int)i, hv = Over(lx, y, lw, rh);
    DrawRectangleRec({ lx, y, lw, rh - 2 }, sel ? csgo::Alpha(csgo::Accent, 0.4f) : hv ? csgo::PanelHover : (i & 1) ? csgo::Alpha(csgo::Panel, 0.7f) : csgo::Alpha(csgo::Panel, 0.95f));
    float ty = y + (rh - 2 - S()->GetFontTall(FONT_GO)) * 0.5f;
    std::string name = s.name; float maxW = cMap - 30;
    while (name.size() > 4 && S()->GetTextWide(FONT_GO, name.c_str()) > maxW) { name.pop_back(); while (!name.empty() && ((unsigned char)name.back() & 0xC0) == 0x80) name.pop_back(); }
    if (name.size() != s.name.size()) name += "...";
    S()->DrawText(FONT_GO, name.c_str(), lx + 12, ty, s.players == s.maxPlayers ? csgo::TextDim : csgo::Text);
    S()->DrawText(FONT_GO, s.map.c_str(), lx + cMap, ty, csgo::Text);
    S()->DrawText(FONT_GO, TextFormat("%d / %d", s.players, s.maxPlayers), lx + cPl, ty, csgo::Text);
    S()->DrawText(FONT_GO, TextFormat("%d", s.ping), lx + cPing, ty, s.ping < 50 ? csgo::Green : s.ping < 100 ? csgo::Text : csgo::Red);
    if (Clicked(lx, y, lw, rh)) { if (s_serverSel == (int)i && In().doubleClick) { s_connecting = (int)i; s_connectStart = (float)GetTime(); } s_serverSel = (int)i; PlayClick(); }
  }
  ScrollBar(s_serverScroll, lx, ly, lw, lh);
  float by = py + ph - 60;
  S()->DrawText(FONT_GO, shown < s_servers.size() ? TextFormat("Refreshing... %d servers", (int)shown) : TextFormat("%d servers", (int)s_servers.size()), lx, by + 14, csgo::TextDim);
  if (FlatButton("REFRESH", lx + lw - 360, by, 160, 44, csgo::PanelLight, csgo::PanelHover)) BuildServerList();
  bool can = s_serverSel >= 0 && s_serverSel < (int)s_servers.size();
  if (FlatButton("CONNECT", lx + lw - 190, by, 190, 44, can ? csgo::Green : csgo::PanelLight, can ? csgo::GreenHover : csgo::PanelLight) && can) { s_connecting = s_serverSel; s_connectStart = (float)GetTime(); }
}

void PaintConnecting() {
  if (s_connecting < 0 || s_connecting >= (int)s_servers.size()) { s_connecting = -1; return; }
  const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
  const FakeServer& s = s_servers[s_connecting];
  DrawRectangleRec({ 0, 0, W, H }, { 0, 0, 0, 170 });
  float w = std::min(520.f, W - 40), h = 170, x = (W - w) * 0.5f, y = (H - h) * 0.5f;
  DrawRectangleRec({ x, y, w, h }, csgo::Panel); DrawRectangleRec({ x, y, w, 3 }, csgo::Accent);
  float t = (float)GetTime() - s_connectStart;
  const char* stage = t < 0.6f ? "Connecting to server..." : t < 1.2f ? "Retrieving server info..." : t < 1.8f ? "Sending client info..." : "Loading map...";
  S()->DrawText(FONT_GO_BOLD, s.name.c_str(), x + 22, y + 20, csgo::Text);
  S()->DrawText(FONT_GO, stage, x + 22, y + 50, csgo::TextDim);
  DrawRectangleRec({ x + 22, y + 84, w - 44, 6 }, csgo::Line);
  DrawRectangleRec({ x + 22, y + 84, (w - 44) * std::clamp(t / 2.2f, 0.f, 1.f), 6 }, csgo::Accent);
  if (FlatButton("CANCEL", x + w - 140, y + h - 54, 118, 38, csgo::PanelLight, csgo::PanelHover)) { s_connecting = -1; return; }
  if (t > 2.3f) {
    s_ms.mode = s.dm ? 1 : (s.hvh ? 2 : 0); s_ms.hvhBots = s.hvh;
    s_ms.bots = (float)std::clamp(s.players > 1 ? s.players - 1 : 9, 3, 19); s_ms.botDifficulty = 3; s_ms.cheats = true;
    s_ms.freezeTime = s.dm ? 0 : 6; s_ms.roundTime = s.dm ? 5 : 2;
    std::string host = s.name, map = s.map;
    s_connecting = -1;
    StartGame(map, host);
  }
}

// ------------------------------------------------------------------------------------------------
// SETTINGS
int s_setTab = 0; Scroll s_setScroll;
const int s_resW[] = { 960, 1024, 1280, 1280, 1366, 1600, 1920, 2560 }, s_resH[] = { 600, 768, 720, 1024, 768, 900, 1080, 1440 };
struct XColor { const char* name; int r, g, b; };
const XColor s_xcolors[] = { { "Green", 50, 250, 50 }, { "Yellow", 250, 250, 50 }, { "Blue", 50, 50, 250 }, { "Cyan", 50, 250, 250 }, { "Red", 250, 50, 50 }, { "White", 255, 255, 255 }, { "Pink", 255, 105, 180 } };

void SettingsTab(int tab) {
  switch (tab) {
  case 0: // GAME
    Header("PROFILE");
    if (RowBegin("Player name")) {
      static std::string nameBuf; static bool init = false;
      if (!init || g_KeyboardFocus != 7101) { nameBuf = FindConVar("name")->GetString(); init = true; }
      if (TextEntry(7101, nameBuf, s_row.ctrlX, s_row.y + 8, s_row.ctrlW, ROW_H - 16, false, 31) || (g_KeyboardFocus == 7101 && nameBuf != FindConVar("name")->GetString())) {
        if (!nameBuf.empty()) { FindConVar("name")->SetValue(nameBuf.c_str()); if (g_pLocalPlayer) g_pLocalPlayer->m_szPlayerName = nameBuf; }
      }
    }
    RowEnd();
    Header("VIEW");
    CvSlider("Field of view", "fov_desired", 75, 110, 1, "%.0f");
    CvSlider("Viewmodel FOV", "cl_real_viewmodel_fov", 40, 90, 1, "%.0f");
    CvSlider("Viewmodel FOV (other models)", "viewmodel_fov", 54, 90, 1, "%.0f");
    CvSlider("Viewmodel offset X", "viewmodel_offset_x", -2.5f, 2.5f, 0.1f, "%.1f");
    CvSlider("Viewmodel offset Y", "viewmodel_offset_y", -2.5f, 2.5f, 0.1f, "%.1f");
    CvSlider("Viewmodel offset Z", "viewmodel_offset_z", -2.5f, 2.5f, 0.1f, "%.1f");
    CvChoice("Viewmodel hand", "cl_righthand", { { "Left", "0" }, { "Right", "1" } });
    CvSlider("View bob", "cl_bob", 0, 0.03f, 0.001f, "%.3f");
    CvSlider("Third person distance", "cam_idealdist", 40, 250, 5, "%.0f");
    Header("GAMEPLAY");
    CvToggle("Fast weapon switch", "hud_fastswitch");
    CvToggle("Hit markers", "cl_hitmarker");
    CvToggle("Bullet tracers", "cl_tracers");
    CvToggle("Player shadows", "r_shadows");
    CvSlider("Bullet hole decals", "r_decals", 0, 2048, 64, "%.0f");
    CvToggle("Interpolate entities", "cl_interpolate");
    break;
  case 1: { // CROSSHAIR
    Header("CROSSHAIR");
    CvToggle("Show crosshair", "crosshair");
    // colour presets (three cvars)
    {
      ConVar *r = FindConVar("cl_crosshaircolor_r"), *g = FindConVar("cl_crosshaircolor_g"), *b = FindConVar("cl_crosshaircolor_b");
      int idx = 0; int n = (int)(sizeof(s_xcolors) / sizeof(*s_xcolors));
      for (int i = 0; i < n; i++) if (s_xcolors[i].r == r->GetInt() && s_xcolors[i].g == g->GetInt() && s_xcolors[i].b == b->GetInt()) idx = i;
      std::vector<std::string> o; for (int i = 0; i < n; i++) o.push_back(s_xcolors[i].name);
      if (RowBegin("Color") && Choice(idx, o, s_row.ctrlX, s_row.y + 5, s_row.ctrlW, ROW_H - 10)) { r->SetValue(s_xcolors[idx].r); g->SetValue(s_xcolors[idx].g); b->SetValue(s_xcolors[idx].b); }
      RowEnd();
    }
    CvSlider("Size", "cl_crosshairsize", 1, 15, 0.5f, "%.1f");
    CvSlider("Gap", "cl_crosshairgap", -3, 10, 0.5f, "%.1f");
    CvSlider("Thickness", "cl_crosshairthickness", 0.5f, 4, 0.5f, "%.1f");
    CvToggle("Center dot", "cl_crosshairdot");
    CvToggle("Dynamic (spread)", "cl_dynamiccrosshair");
    // preview
    s_row.y += 8;
    if (s_row.y + 120 > s_row.top && s_row.y < s_row.bottom) {
      float cx = s_row.x + s_row.w * 0.5f, cy = s_row.y + 60;
      DrawRectangleRec({ cx - 120, s_row.y, 240, 120 }, { 92, 84, 66, 255 });
      Color c = { (unsigned char)FindConVar("cl_crosshaircolor_r")->GetInt(), (unsigned char)FindConVar("cl_crosshaircolor_g")->GetInt(), (unsigned char)FindConVar("cl_crosshaircolor_b")->GetInt(), 255 };
      float len = FindConVar("cl_crosshairsize")->GetFloat() * 2, gap = FindConVar("cl_crosshairgap")->GetFloat() + 2, th = std::max(1.f, FindConVar("cl_crosshairthickness")->GetFloat() * 2);
      if (FindConVar("crosshair")->GetBool()) {
        DrawRectangleRec({ cx - th * 0.5f, cy - gap - len, th, len }, c); DrawRectangleRec({ cx - th * 0.5f, cy + gap, th, len }, c);
        DrawRectangleRec({ cx - gap - len, cy - th * 0.5f, len, th }, c); DrawRectangleRec({ cx + gap, cy - th * 0.5f, len, th }, c);
        if (FindConVar("cl_crosshairdot")->GetBool()) DrawRectangleRec({ cx - th * 0.5f, cy - th * 0.5f, th, th }, c);
      }
    }
    s_row.y += 128;
    break;
  }
  case 2: // HUD
    Header("HUD");
    CvSlider("HUD scale", "hud_scaling", 0.5f, 2.f, 0.05f, "%.2f");
    CvToggle("Radar", "cl_radar");
    CvSlider("Radar zoom", "cl_radar_scale", 0.2f, 1.5f, 0.05f, "%.2f");
    CvToggle("Rotate radar", "cl_radar_rotate");
    CvSlider("Kill feed time", "hud_deathnotice_time", 2, 15, 1, "%.0f s");
    CvSlider("Console notify time", "con_notifytime", 0, 15, 1, "%.0f s");
    Header("DEBUG");
    CvChoice("Show FPS", "cl_showfps", { { "Off", "0" }, { "FPS", "1" }, { "Smooth", "2" } });
    CvToggle("Net graph", "net_graph");
    CvToggle("Show position", "cl_showpos");
    break;
  case 3: // AUDIO
    Header("AUDIO");
    CvSlider("Master volume", "volume", 0, 1, 0.01f, "%.2f");
    CvSlider("Music volume", "snd_musicvolume", 0, 1, 0.01f, "%.2f");
    CvToggle("Footsteps", "snd_footsteps");
    CvChoice("Hit sound", "snd_hitsound", { { "Off", "0" }, { "Bell", "1" }, { "Click", "2" } });
    CvToggle("Counter-Strike: Source sounds", "snd_css");
    break;
  case 4: { // VIDEO
    Header("DISPLAY");
#if !defined(PLATFORM_ANDROID) && !defined(CS_HEADLESS)
    {
      int n = (int)(sizeof(s_resW) / sizeof(*s_resW)), idx = 0;
      std::vector<std::string> o; for (int i = 0; i < n; i++) { o.push_back(TextFormat("%dx%d", s_resW[i], s_resH[i])); if (s_resW[i] == GetScreenWidth() && s_resH[i] == GetScreenHeight()) idx = i; }
      if (RowBegin("Resolution") && Choice(idx, o, s_row.ctrlX, s_row.y + 5, s_row.ctrlW, ROW_H - 10) && !IsWindowFullscreen()) SetWindowSize(s_resW[idx], s_resH[idx]);
      RowEnd();
      bool fs = IsWindowFullscreen();
      if (RowBegin("Fullscreen") && Toggle(fs, s_row.ctrlX + s_row.ctrlW - 150, s_row.y + 6, 150, ROW_H - 12)) {
        if (fs) SetWindowSize(GetMonitorWidth(GetCurrentMonitor()), GetMonitorHeight(GetCurrentMonitor()));
        ToggleFullscreen();
      }
      RowEnd();
    }
#endif
    CvToggle("Vertical sync", "mat_vsync");
    CvChoice("FPS limit", "fps_max", { { "30", "30" }, { "60", "60" }, { "90", "90" }, { "120", "120" }, { "144", "144" }, { "240", "240" }, { "300", "300" }, { "Unlimited", "0" } });
    Header("QUALITY");
    CvSlider("Brightness", "r_src_lightscale", 0.6f, 2.f, 0.05f, "%.2f");
    CvToggle("Fade distant props", "r_propfade");
    CvChoice("Skip tiny props", "r_propscreencull", { { "Off", "0" }, { "< 2 px", "2" }, { "< 4 px (faster)", "4" }, { "< 8 px (fastest)", "8" } });
    CvToggle("Player blob shadows", "r_shadows");
    CvChoice("Bullet decals", "r_decals", { { "Off", "0" }, { "64", "64" }, { "128", "128" }, { "256", "256" }, { "512", "512" } });
    CvToggle("Real player models", "cl_real_models");
    CvToggle("Real view models", "cl_real_viewmodels");
    {
      std::vector<std::pair<const char*, const char*>> o;
      static std::vector<std::string> names; names.clear();
      for (auto& m : Maps()) names.push_back(m.name);
      for (auto& n : names) o.push_back({ n.c_str(), n.c_str() });
      if (!o.empty()) CvChoice("Main menu map (restart)", "menu_background_map", o);
    }
    break;
  }
  case 5: // MOUSE
    Header("MOUSE");
    CvSlider("Sensitivity", "sensitivity", 0.1f, 8.f, 0.05f, "%.2f");
    CvSlider("Zoom sensitivity", "zoom_sensitivity_ratio_mouse", 0.2f, 2.f, 0.05f, "%.2f");
    CvToggle("Invert mouse", "m_invert");
    CvToggle("Raw input", "m_rawinput");
    CvToggle("Mouse filter", "m_filter");
    CvSlider("Yaw factor (m_yaw)", "m_yaw", 0.01f, 0.05f, 0.001f, "%.3f");
    CvSlider("Pitch factor (m_pitch)", "m_pitch", 0.01f, 0.05f, 0.001f, "%.3f");
    if (ConVar* sn = FindConVar("sensitivity")) RowInfo("cm / 360 at 800 dpi", TextFormat("%.1f", 360.f / (sn->GetFloat() * 0.022f) / 800.f * 2.54f));
    break;
  case 6: { // KEYBOARD
    static const std::pair<const char*, const char*> actions[] = {
      { "Move forward", "+forward" }, { "Move back", "+back" }, { "Move left", "+moveleft" }, { "Move right", "+moveright" }, { "Jump", "+jump" }, { "Duck", "+duck" },
      { "Walk", "+speed" }, { "Use", "+use" }, { "Fire", "+attack" }, { "Secondary fire / scope", "+attack2" }, { "Reload", "+reload" }, { "Drop weapon", "drop" },
      { "Primary weapon", "slot1" }, { "Secondary weapon", "slot2" }, { "Knife", "slot3" }, { "Grenades", "slot4" }, { "Bomb", "slot5" }, { "Last weapon", "lastinv" },
      { "Next weapon", "invnext" }, { "Buy menu", "buymenu" }, { "Select team", "chooseteam" }, { "Scoreboard", "+showscores" }, { "Chat", "messagemode" },
      { "Team chat", "messagemode2" }, { "Autobuy", "autobuy" }, { "Rebuy", "rebuy" }, { "Third person", "thirdperson" }, { "Console", "toggleconsole" }, { "Screenshot", "screenshot" },
    };
    Header("MOVEMENT & WEAPONS");
    for (auto& a : actions) RowKey(a.first, a.second);
    RowButton("Reset all keys", "USE DEFAULTS", [] { Cbuf_AddText("unbindall\n"); IN_Init(); });
    break;
  }
  case 7: // TOUCH
    Header("TOUCH CONTROLS");
    CvToggle("On-screen controls", "touch_enable");
    CvSlider("Look sensitivity", "touch_sensitivity", 0.1f, 4.f, 0.05f, "%.2f");
    CvSlider("Button opacity", "touch_opacity", 0.1f, 1.f, 0.05f, "%.2f");
    CvSlider("Button size", "touch_scale", 0.6f, 1.6f, 0.05f, "%.2f");
    CvToggle("Invert look", "touch_invert");
    RowInfo("Cheat binds", "aimwhere menu: tap a bind -> circle");
    break;
  case 8: { // CONTENT
    Header("CS:GO CONTENT");
    RowInfo("CS:GO", srcfs::CsgoAvailable() ? "installed" : "missing");
    std::string go = srcfs::CsgoRoot(); if (go.size() > 44) go = "..." + go.substr(go.size() - 41);
    RowInfo("Folder", go.empty() ? "-  (not found)" : go.c_str());
    RowInfo("Maps found", TextFormat("%d", (int)Maps().size()));
    download::Progress pr = download::Get();
    if (download::Busy()) {
      int pc = 0; const download::Pack* packs = download::Packs(pc);
      RowInfo("Downloading", TextFormat("%.0f / %.0f MB  (%.1f MB/s)  %.0f%%", pr.totalMB, pr.totalExpectedMB, pr.speedMBs,
                                         pr.totalExpectedMB > 0 ? 100.0 * pr.totalMB / pr.totalExpectedMB : 0.0));
      RowButton("", "CANCEL", [] { download::Cancel(); });
    } else {
      if (pr.state == download::FAILED) RowInfo("Last download", pr.error.c_str());
      if (pr.state == download::DONE) RowInfo("Last download", "finished - restart the game");
      RowButton("Download / repair content", "DOWNLOAD", [] { download::Start(); });
    }
    RowButton("Download real CS:GO content (~2 GB)", "CSGO_DOWNLOAD", [] { Cbuf_AddText("csgo_download\n"); });
    RowInfo("Source", "public CS:GO map mirrors on GitHub + your own CS:GO / CS:GO Dedicated Server");
    break;
  }
  }
}

void PaintSettings() {
  const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight();
  float px = 0, py = TB, pw = W, ph = H - TB;
  DrawRectangleRec({ px, py, pw, ph }, { 14, 16, 20, 245 });
  static const char* tabs[] = { "GAME", "CROSSHAIR", "HUD", "AUDIO", "VIDEO", "MOUSE", "KEYBOARD", "TOUCH", "CONTENT" };
  const int NT = 9;
  float sw = std::min(220.f, pw * 0.22f);
  DrawRectangleRec({ px, py, sw, ph }, { 22, 25, 30, 250 });
  S()->DrawText(FONT_GO_TITLE, "SETTINGS", px + 20, py + 18, csgo::Text);
  float ty = py + 64, tabH = std::min(42.f, (ph - 70) / NT);
  for (int i = 0; i < NT; i++) {
    bool sel = s_setTab == i, hv = Over(px, ty, sw, tabH);
    if (sel || hv) DrawRectangleRec({ px, ty, sw, tabH }, sel ? csgo::Alpha(csgo::Accent, 0.25f) : csgo::Alpha(csgo::PanelHover, 0.6f));
    if (sel) DrawRectangleRec({ px, ty, 4, tabH }, csgo::Accent);
    S()->DrawText(FONT_GO_BOLD, tabs[i], px + 22, ty + (tabH - S()->GetFontTall(FONT_GO_BOLD)) * 0.5f, sel || hv ? WHITE : csgo::TextDim);
    if (Clicked(px, ty, sw, tabH)) { s_setTab = i; s_setScroll = Scroll(); s_captureKey = nullptr; PlayClick(); }
    ty += tabH;
  }
  // rows
  float cx = px + sw + 24, cy = py + 16, cw = std::min(pw - sw - 48, 860.f), chh = ph - 32;
  float ctrlW = std::min(340.f, cw * 0.5f);
  s_row = { cx, cy - s_setScroll.y, cw, cx + cw - ctrlW - 10, ctrlW, cy, cy + chh, 0 };
  ScrollRegion(s_setScroll, cx, cy, cw + 10, chh);
  float startY = s_row.y;
  SettingsTab(s_setTab);
  s_setScroll.content = s_row.y - startY + 20;
  ScrollBar(s_setScroll, cx, cy, cw, chh);
  // key capture
  if (s_captureKey) {
    int key = 0;
    for (int k : In().keys) { key = k; break; }
    static const int mb[5] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_SIDE, MOUSE_BUTTON_EXTRA };
    static bool armed = false; if (!IsMouseButtonDown(MOUSE_BUTTON_LEFT)) armed = true;
    for (int i = 0; i < 5; i++) if (IsMouseButtonPressed(mb[i]) && (i != 0 || armed)) key = K_MOUSE1 + i;
    if (In().wheel > 0) key = K_MWHEELUP; if (In().wheel < 0) key = K_MWHEELDOWN;
    if (key == KEY_ESCAPE) { s_captureKey = nullptr; ConsumeKey(KEY_ESCAPE); armed = false; }
    else if (key) {
      int old = Key_FindBinding(s_captureCmd.c_str()); if (old >= 0) Key_SetBinding(old, "");
      Key_SetBinding(key, s_captureCmd.c_str()); s_captureKey = nullptr; armed = false; ConsumeKey(key);
    }
  }
}
} // namespace

static ConCommand cmd_gameui_panel("gameui_panel", [](const CCommand& a) {
  std::string n = a.Arg(1);
  s_panel = n == "play" ? P_PLAY : n == "agents" ? P_AGENTS : n == "loadout" ? P_LOADOUT : n == "servers" ? P_SERVERS : n == "settings" ? P_SETTINGS : P_HOME;
  if (a.ArgC() > 2) s_setTab = std::clamp(atoi(a.Arg(2)), 0, 8);
  gameui::Show();
}, "Open a main menu panel: play, agents, servers, settings [tab 0-8], home");

// ------------------------------------------------------------------------------------------------
bool CapturingKey() { return s_captureKey != nullptr; }
void OpenSettings() { s_panel = P_SETTINGS; }

bool OnEscape() {
  if (s_captureKey) { s_captureKey = nullptr; return true; }
  if (s_connecting >= 0) { s_connecting = -1; return true; }
  if (s_panel != P_HOME) {
    if (s_panel == P_SETTINGS || s_panel == P_LOADOUT) Cbuf_AddText("host_writeconfig\n");
    s_panel = P_HOME; return true;
  }
  return false;
}

void Paint() {
  s_buttonIndex = 0;
  // input bookkeeping
  if (s_clearDrag) { s_dragScroll = false; s_activeSlider = nullptr; s_clearDrag = false; }
  if (In().mousePressed) { s_pressPos = In().mouse; s_dragScroll = false; s_activeSlider = nullptr; }
  if (In().mouseReleased) s_clearDrag = true;
  const bool ingame = CL_IsInGame();
  static Panel last = P_HOME;
  if ((last == P_SETTINGS || last == P_LOADOUT) && s_panel != last) Cbuf_AddText("host_writeconfig\n");
  if (last != s_panel) s_panelAnim = 0;
  last = s_panel;
  s_panelAnim = std::min(1.f, s_panelAnim + GetFrameTime() * 7.f);
  if (ingame) DrawRectangle(0, 0, GetScreenWidth(), GetScreenHeight(), { 0, 0, 0, 90 });
  switch (s_panel) {
    case P_HOME: PaintHome(ingame); break;
    case P_PLAY: PaintPlay(); break;
    case P_AGENTS: PaintAgents(); break;
    case P_LOADOUT: PaintLoadout(); break;
    case P_SERVERS: PaintServers(); break;
    case P_SETTINGS: PaintSettings(); break;
  }
  PaintTopBar(ingame);
  PaintConnecting();
  s_lastMouse = In().mouse;
}
} // namespace mainmenu
