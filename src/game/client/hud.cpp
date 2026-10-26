// hud.cpp - CHud: Counter-Strike 1.6 style HUD elements
#include "cl_main.hpp"
#include "hud_menus.hpp"
#include "../../engine/host.hpp"
#include "../../engine/materialsystem.hpp"
#include "../../vgui/surface.hpp"
#include "../shared/cs_player.hpp"
#include "../shared/weapon_csbase.hpp"
#include "../../vgui/vgui_images.hpp"
#include "../shared/cs_gamerules.hpp"
#include "../shared/cs_projectiles.hpp"
#include "../shared/gameevents.hpp"
#include "../shared/mapinfo.hpp"
#include "../shared/soundemitter.hpp"
#include "../server/nav_mesh.hpp"
#include "../../engine/hlfs.hpp"
#include "../../engine/srcfs.hpp"
#include "../../engine/srcmaterial.hpp"
#include "../../engine/keyvalues.hpp"
#include "rlgl.h"
#include <deque>
#include <sstream>
#include <cmath>
#include <algorithm>

using namespace vgui;

ConVar cl_drawhud("cl_drawhud", "1", FCVAR_CHEAT, "Enable the rendering of the hud");
ConVar hud_scaling("hud_scaling", "1.0", FCVAR_ARCHIVE, "Scales hud elements", true, 0.5f, true, 1.5f);
ConVar cl_crosshaircolor_r("cl_crosshaircolor_r", "50", FCVAR_ARCHIVE, "");
ConVar cl_crosshaircolor_g("cl_crosshaircolor_g", "250", FCVAR_ARCHIVE, "");
ConVar cl_crosshaircolor_b("cl_crosshaircolor_b", "50", FCVAR_ARCHIVE, "");
ConVar cl_crosshairsize("cl_crosshairsize", "5", FCVAR_ARCHIVE, "");
ConVar cl_crosshairgap("cl_crosshairgap", "3", FCVAR_ARCHIVE, "");
ConVar cl_crosshairthickness("cl_crosshairthickness", "1", FCVAR_ARCHIVE, "");
ConVar cl_crosshairdot("cl_crosshairdot", "0", FCVAR_ARCHIVE, "");
ConVar cl_dynamiccrosshair("cl_dynamiccrosshair", "1", FCVAR_ARCHIVE, "");
ConVar crosshair("crosshair", "1", FCVAR_ARCHIVE, "");
ConVar cl_radar("cl_radar", "1", FCVAR_ARCHIVE, "Draw the radar");
ConVar cl_radar_scale("cl_radar_scale", "0.6", FCVAR_ARCHIVE, "", true, 0.25f, true, 1.5f);
ConVar cl_radar_rotate("cl_radar_rotate", "1", FCVAR_ARCHIVE, "");
ConVar hud_deathnotice_time("hud_deathnotice_time", "6", FCVAR_ARCHIVE, "");

static constexpr Color HUD = { 255, 176, 0, 255 };
static constexpr Color COL_T = { 255, 64, 64, 255 };
static constexpr Color COL_CT = { 153, 204, 255, 255 };
Color TeamColor(int team) { return team == TEAM_TERRORIST ? COL_T : team == TEAM_CT ? COL_CT : Color{ 200, 200, 200, 255 }; }

struct DeathNotice { std::string killer, victim, assister, weapon; int kteam, vteam; bool hs, wallbang, smoke, noscope, blind, localInvolved; float time; };
struct ChatLine { std::string who, text; int team; bool teamOnly, dead; float time; };
static std::deque<DeathNotice> s_deaths;
static std::deque<ChatLine> s_chat;
static std::string s_center; static float s_centerEnd = 0;
static std::string s_hint; static float s_hintEnd = 0;
static int s_lastMoney = -1; static int s_moneyDelta = 0; static float s_moneyDeltaTime = -100;
static Texture2D s_radarTex{}; static Texture2D s_scopeTex{};
static int s_listener = 0;

float HudScale() { return surface()->Scale() * hud_scaling.GetFloat(); }
bool HUD_ShouldDraw() { return cl_drawhud.GetBool() && CL_IsInGame(); }

void HUD_AddChat(const std::string& who, const std::string& text, int team, bool teamOnly, bool dead) {
  s_chat.push_back({ who, text, team, teamOnly, dead, gpGlobals->realtime });
  while (s_chat.size() > 8) s_chat.pop_front();
  Msg("%s%s%s : %s\n", dead ? "*DEAD* " : "", teamOnly ? (team == TEAM_CT ? "(Counter-Terrorist) " : "(Terrorist) ") : "", who.c_str(), text.c_str());
  EmitSound2D("UI.Click", 0.5f);
}
void HUD_CenterPrint(const std::string& text, float time) { s_center = text; s_centerEnd = gpGlobals->realtime + time; }
void HUD_HintText(const std::string& text) { s_hint = text; s_hintEnd = gpGlobals->realtime + 4.f; EmitSound2D("Hud.Hint"); }

// dev: fills the kill feed with examples of every icon (UI testing / screenshots)
static ConCommand cmd_hud_testkillfeed("hud_testkillfeed", [](const CCommand&) {
  struct T { const char* k; const char* v; const char* w; int kt, vt; bool hs, wb, smk, ns, bl, local; };
  static const T t[] = { { "Player", "Neil", "ak47", 3, 2, true, false, false, false, false, true }, { "Graham", "Clarence", "awp", 2, 3, false, true, false, true, false, false },
                         { "Ernie", "Player", "knife", 2, 3, false, false, false, false, false, true }, { "Kevin", "Arnold", "m4a1", 3, 2, true, true, true, false, false, false },
                         { "Brett", "Dan", "hegrenade", 3, 2, false, false, false, false, true, false }, { "", "Steve", "world", 0, 2, false, false, false, false, false, false } };
  s_deaths.clear();
  for (const T& x : t) { DeathNotice d; d.killer = x.k; d.victim = x.v; d.weapon = x.w; d.kteam = x.kt; d.vteam = x.vt; d.hs = x.hs; d.wallbang = x.wb; d.smoke = x.smk; d.noscope = x.ns; d.blind = x.bl; d.localInvolved = x.local; d.time = gpGlobals->realtime; s_deaths.push_back(d); }
}, "Fill the kill feed with sample entries");

std::vector<std::pair<int, int>> g_HudRoundHistory;   // (winner team, RoundEndReason_t) per played round - scoreboard
static void OnEvent(const IGameEvent& e) {
  CCSPlayer* lp = g_pLocalPlayer;
  if (e.IsEvent("round_end")) {
    int w = e.GetInt("winner"), r = e.GetInt("reason");
    if ((w == TEAM_TERRORIST || w == TEAM_CT) && r != Game_Commencing) g_HudRoundHistory.push_back({ w, r });
  } else if (e.IsEvent("round_start")) {
    if (CCSGameRules* gr = CSGameRules()) if ((int)g_HudRoundHistory.size() > gr->m_iTotalRoundsPlayed) g_HudRoundHistory.resize(gr->m_iTotalRoundsPlayed);
  }
  if (e.IsEvent("player_death")) {
    CCSPlayer* v = UTIL_PlayerByIndex(e.GetInt("userid")); CCSPlayer* a = UTIL_PlayerByIndex(e.GetInt("attacker")); CCSPlayer* as = UTIL_PlayerByIndex(e.GetInt("assister"));
    DeathNotice d;
    d.killer = (a && a != v) ? a->GetPlayerName() : ""; d.kteam = a ? a->m_iTeamNum : 0;
    d.victim = v ? v->GetPlayerName() : "?"; d.vteam = v ? v->m_iTeamNum : 0;
    d.assister = as ? as->GetPlayerName() : "";
    d.weapon = e.GetString("weapon"); if (d.weapon == "inferno") d.weapon = "weapon_molotov"; d.hs = e.GetBool("headshot"); d.wallbang = e.GetInt("penetrated") > 0; d.smoke = e.GetBool("thrusmoke"); d.noscope = e.GetBool("noscope"); d.blind = e.GetBool("attackerblind");
    d.localInvolved = lp && (a == lp || v == lp);
    d.time = gpGlobals->realtime;
    s_deaths.push_back(d); while (s_deaths.size() > 6) s_deaths.pop_front();
  } else if (e.IsEvent("bomb_planted")) {
    HUD_CenterPrint("The bomb has been planted.", 3.f);
  } else if (e.IsEvent("bomb_pickup")) {
    if (lp && e.GetInt("userid") == lp->entindex()) HUD_HintText("You picked up the bomb.\nPlant it at a bomb site (marked on your radar).");
  } else if (e.IsEvent("round_freeze_end")) {
    if (lp && lp->IsAlive() && lp->HasC4()) HUD_HintText("You have the bomb! Find the target zone or DROP the bomb for another Terrorist.");
  } else if (e.IsEvent("player_team")) {
  }
}

// radar image <-> world: world = s_radarW0 + u * s_radarDU + v * s_radarDV  (u, v in texture pixels)
static Vector s_radarW0, s_radarDU, s_radarDV;

// CS 1.6 overview (overviews/<map>.txt + .bmp from the player's install), HL1 spectator overview layout
// CS:S: resource/overviews/<map>.txt (pos_x, pos_y, scale) + materials/overviews/<map>.vtf
static bool LoadSrcOverviewRadar(const CMapInfo& m) {
  float px = 0, py = 0, scale = 0;
  std::vector<uint8_t> txt;
  if (srcfs::Read("resource/overviews/" + m.name + ".txt", txt)) {
    auto kv = KeyValues::Parse((const char*)txt.data(), txt.size());
    const KeyValues* root = kv && !kv->children.empty() ? kv->children[0].get() : nullptr;
    if (root) { px = root->GetFloat("pos_x"); py = root->GetFloat("pos_y"); scale = root->GetFloat("scale"); }
  }
  if (scale <= 0) {   // stock maps (the overview scripts are not part of every content pack)
    struct O { const char* n; float x, y, s; };
    static const O known[] = { { "de_dust2", -2476, 3239, 4.4f }, { "de_inferno", -2087, 3870, 4.9f }, { "de_nuke", -3453, 2887, 7.f } };
    for (auto& o : known) if (m.name == o.n) { px = o.x; py = o.y; scale = o.s; }
  }
  if (scale <= 0) return false;
  std::vector<uint8_t> rgba; int w = 0, h = 0;
  if (!VTF_Load("materials/overviews/" + m.name + ".vtf", rgba, w, h, 1024) && !VTF_Load("materials/overviews/" + m.name + "_radar.vtf", rgba, w, h, 1024)) return false;
  Image img{ rgba.data(), w, h, 1, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8 };
  float upp = scale * 1024.f / (float)w;
  s_radarW0 = Vector(px, py, 0); s_radarDU = Vector(upp, 0, 0); s_radarDV = Vector(0, -upp, 0);
  s_radarTex = LoadTextureFromImage(img); SetTextureFilter(s_radarTex, TEXTURE_FILTER_BILINEAR);
  Msg("Radar: materials/overviews/%s.vtf (scale %.2f)\n", m.name.c_str(), scale);
  return true;
}

static bool LoadOverviewRadar(const CMapInfo& m) {
  if (m.isSrc) return LoadSrcOverviewRadar(m);
  std::vector<uint8_t> txt; if (!hlfs::Read("overviews/" + m.name + ".txt", txt)) return false;
  std::string t(txt.begin(), txt.end());
  float zoom = 1.f, ox = 0, oy = 0; int rotated = 0; std::string image = "overviews/" + m.name + ".bmp";
  std::istringstream in(t); std::string tok;
  while (in >> tok) {
    std::string lt = hlfs::Lower(tok);
    if (lt == "zoom") in >> zoom;
    else if (lt == "origin") { float oz; in >> ox >> oy >> oz; }
    else if (lt == "rotated") in >> rotated;
    else if (lt == "image") { in >> image; if (image.size() >= 2 && image.front() == '"') image = image.substr(1, image.size() - 2); }
  }
  std::string path = hlfs::Find(image); if (path.empty()) return false;
  Image img = LoadImage(path.c_str()); if (!img.data) return false;
  ImageFormat(&img, PIXELFORMAT_UNCOMPRESSED_R8G8B8A8);
  // background colour (corner pixel) becomes transparent
  Color* px = (Color*)img.data; Color bg = px[0];
  for (int i = 0; i < img.width * img.height; i++) {
    Color& c = px[i];
    if (std::abs(c.r - bg.r) + std::abs(c.g - bg.g) + std::abs(c.b - bg.b) < 24) c = { 0, 0, 0, 0 }; else c.a = 235;
  }
  if (zoom <= 0) zoom = 1;
  float upp = 8192.f / zoom / (float)(rotated ? img.width : img.width);   // world units per image pixel (1024 px = 8192 / zoom)
  float halfW = img.width * 0.5f * upp, halfH = img.height * 0.5f * upp;
  if (rotated) { s_radarW0 = Vector(ox - halfW, oy + halfH, 0); s_radarDU = Vector(upp, 0, 0); s_radarDV = Vector(0, -upp, 0); }
  else { s_radarW0 = Vector(ox + halfH, oy + halfW, 0); s_radarDU = Vector(0, -upp, 0); s_radarDV = Vector(-upp, 0, 0); }
  s_radarTex = LoadTextureFromImage(img); SetTextureFilter(s_radarTex, TEXTURE_FILTER_BILINEAR);
  UnloadImage(img);
  Msg("Radar: %s (zoom %.2f%s)\n", image.c_str(), zoom, rotated ? ", rotated" : "");
  return true;
}

// original maps without an overview: top-down image of the nav columns (height shaded)
static void BuildNavRadar() {
  float x0, y0, cell; int w, h; g_NavMesh.GetGrid(x0, y0, cell, w, h);
  if (w <= 0 || h <= 0) return;
  const int S = 2;
  Image img = GenImageColor(w * S, h * S, { 0, 0, 0, 0 });
  float zmin = 1e9f, zmax = -1e9f;
  for (int i = 0; i < g_NavMesh.NumAreas(); i++) { const CNavArea* a = g_NavMesh.GetArea(i); if (a->reachable) { zmin = std::min(zmin, a->z); zmax = std::max(zmax, a->z); } }
  for (int cy = 0; cy < h; cy++) for (int cx = 0; cx < w; cx++) {
    float top = -1e9f;
    for (int i : g_NavMesh.Column(cy * w + cx)) { const CNavArea* a = g_NavMesh.GetArea(i); if (a->reachable) top = std::max(top, a->z); }
    if (top < -1e8f) continue;
    float k = zmax > zmin ? (top - zmin) / (zmax - zmin) : 0.5f;
    unsigned char v = (unsigned char)(110 + 110 * k);
    ImageDrawRectangle(&img, cx * S, (h - 1 - cy) * S, S, S, { v, (unsigned char)(v * 0.95f), (unsigned char)(v * 0.82f), 230 });
  }
  s_radarW0 = Vector(x0, y0 + h * cell, 0); s_radarDU = Vector(cell / S, 0, 0); s_radarDV = Vector(0, -cell / S, 0);
  s_radarTex = LoadTextureFromImage(img); SetTextureFilter(s_radarTex, TEXTURE_FILTER_BILINEAR);
  UnloadImage(img);
}

static void BuildRadarTexture() {
  if (s_radarTex.id) { UnloadTexture(s_radarTex); s_radarTex = {}; }
  const CMapInfo& m = g_MapInfo;
  if (m.isBsp) { if (!LoadOverviewRadar(m)) BuildNavRadar(); return; }
  if (m.gw <= 0) return;
  const int S = 4; // pixels per cell
  s_radarW0 = Vector(m.gridOrigin.x, m.gridOrigin.y, 0); s_radarDU = Vector(m.cell / S, 0, 0); s_radarDV = Vector(0, -m.cell / S, 0);
  Image img = GenImageColor(m.gw * S, m.gh * S, { 0, 0, 0, 0 });
  float zmin = 1e9f, zmax = -1e9f;
  for (int i = 0; i < m.gw * m.gh; i++) if (!std::isnan(m.floorZ[i]) && (m.cellType[i] == 1 || m.cellType[i] == 3)) { zmin = std::min(zmin, m.floorZ[i]); zmax = std::max(zmax, m.floorZ[i]); }
  for (int y = 0; y < m.gh; y++) for (int x = 0; x < m.gw; x++) {
    int i = y * m.gw + x; uint8_t t = m.cellType[i];
    Color c = { 0, 0, 0, 0 };
    if (t == 1 || t == 3) { float h = zmax > zmin ? (m.floorZ[i] - zmin) / (zmax - zmin) : 0.5f; unsigned char v = (unsigned char)(120 + 100 * h); c = { v, (unsigned char)(v * 0.95f), (unsigned char)(v * 0.82f), 235 }; }
    else if (t == 4) c = { 95, 85, 70, 235 };
    else if (t == 2) {
      // walls next to floor get an outline color, inner walls transparent
      bool edge = false;
      for (int dy = -1; dy <= 1 && !edge; dy++) for (int dx = -1; dx <= 1; dx++) { int xx = x + dx, yy = y + dy; if (xx < 0 || yy < 0 || xx >= m.gw || yy >= m.gh) continue; uint8_t tt = m.cellType[yy * m.gw + xx]; if (tt == 1 || tt == 3 || tt == 4) { edge = true; break; } }
      if (edge) c = { 30, 30, 26, 235 };
    }
    ImageDrawRectangle(&img, x * S, y * S, S, S, c);
  }
  // bomb sites
  for (auto& z : m.zones) {
    if (z.type != ZONE_BOMBSITE_A && z.type != ZONE_BOMBSITE_B) continue;
    int x0 = (int)((z.mins.x - m.gridOrigin.x) / m.cell * S), x1 = (int)((z.maxs.x - m.gridOrigin.x) / m.cell * S);
    int y0 = (int)((m.gridOrigin.y - z.maxs.y) / m.cell * S), y1 = (int)((m.gridOrigin.y - z.mins.y) / m.cell * S);
    for (int y = y0; y < y1; y++) for (int x = x0; x < x1; x++) if (x >= 0 && y >= 0 && x < img.width && y < img.height) {
      Color c = GetImageColor(img, x, y); if (c.a == 0) continue;
      c.r = (unsigned char)std::min(255, c.r + 50); c.g = (unsigned char)(c.g * 0.75f); c.b = (unsigned char)(c.b * 0.7f);
      ImageDrawPixel(&img, x, y, c);
    }
  }
  s_radarTex = LoadTextureFromImage(img); SetTextureFilter(s_radarTex, TEXTURE_FILTER_BILINEAR);
  UnloadImage(img);
}

static void BuildScopeTexture() {
  const int S = 512;
  Image img = GenImageColor(S, S, { 0, 0, 0, 255 });
  for (int y = 0; y < S; y++) for (int x = 0; x < S; x++) {
    float dx = (x + 0.5f) / S * 2 - 1, dy = (y + 0.5f) / S * 2 - 1; float r = std::sqrt(dx * dx + dy * dy);
    float a = Clamp((r - 0.985f) / 0.015f, 0.f, 1.f);
    // inner vignette
    float vig = Clamp((r - 0.6f) / 0.4f, 0.f, 1.f) * 0.35f;
    ImageDrawPixel(&img, x, y, { 0, 0, 0, (unsigned char)(255 * std::max(a, vig)) });
  }
  s_scopeTex = LoadTextureFromImage(img); SetTextureFilter(s_scopeTex, TEXTURE_FILTER_BILINEAR);
  UnloadImage(img);
}

void HUD_Init() {
  if (!s_listener) s_listener = gameeventmanager->AddListener(nullptr, OnEvent);
}
void HUD_LevelInit() {
  s_deaths.clear(); s_chat.clear(); s_lastMoney = -1; g_HudRoundHistory.clear();
  if (IsWindowReady()) { BuildRadarTexture(); if (!s_scopeTex.id) BuildScopeTexture(); }
}

// ------------------------------------------------------------------------------------------------
// CS:GO / CS2 HUD look: translucent dark plates, thin bars, white bold typography, no 1.6 orange boxes
namespace hudgo {
constexpr Color Text      = { 240, 242, 245, 255 };
constexpr Color TextDim   = { 182, 187, 193, 235 };
constexpr Color TextFaint = { 133, 138, 144, 210 };
constexpr Color Panel     = { 10, 12, 15, 120 };
constexpr Color BarTrack  = { 0, 0, 0, 120 };
constexpr Color CT        = { 100, 158, 235, 255 };
constexpr Color T         = { 232, 172, 60, 255 };
constexpr Color Danger    = { 235, 62, 48, 255 };
constexpr Color Gain      = { 118, 224, 118, 255 };
constexpr Color ArmorCol  = { 160, 200, 240, 255 };
} // namespace hudgo

// vertical space taken by the spectator black bars (CS:GO draws them when dead)
static float HudBottomInset(float sc) {
  CCSPlayer* lp = g_pLocalPlayer;
  bool dead = lp && !lp->IsAlive() && lp->m_iTeamNum != TEAM_UNASSIGNED;
  return dead ? 56 * sc : 0.f;
}
static Color Alpha(Color c, float k) { c.a = (unsigned char)Clamp(c.a * k, 0.f, 255.f); return c; }
static float HudTextW(int font, const char* t, float size) { return (t && *t) ? surface()->GetTextSizeScaled(font, t, size).x : 0.f; }
static void HudText(int font, const char* t, float x, float y, float size, Color c, bool shadow = true) {
  if (!t || !*t) return;
  if (shadow) surface()->DrawTextScaledShadow(font, t, x, y, size, c); else surface()->DrawTextScaled(font, t, x, y, size, c);
}
static void HudTextRight(int font, const char* t, float rx, float y, float size, Color c, bool shadow = true) {
  if (!t || !*t) return; HudText(font, t, rx - HudTextW(font, t, size), y, size, c, shadow);
}
static void HudTextCentered(int font, const char* t, float cx, float y, float size, Color c, bool shadow = true) {
  if (!t || !*t) return; HudText(font, t, cx - HudTextW(font, t, size) * 0.5f, y, size, c, shadow);
}
// thin progress bar (CS:GO style: dark track + solid fill)
static void HudBar(float x, float y, float w, float h, float frac, Color fill) {
  frac = Clamp(frac, 0.f, 1.f);
  surface()->DrawFilledRect(x, y, w, h, hudgo::BarTrack);
  if (frac > 0) surface()->DrawFilledRect(x, y, w * frac, h, fill);
}

// ------------------------------------------------------------------------------------------------
// icons (procedural, always available - no content files needed)
static void IconCross(float x, float y, float s, Color c) {
  float t = s * 0.32f;
  DrawRectangleRounded({ x + (s - t) * 0.5f, y, t, s }, 0.5f, 4, c);
  DrawRectangleRounded({ x, y + (s - t) * 0.5f, s, t }, 0.5f, 4, c);
}
static void IconShield(float x, float y, float s, Color c, bool helmet) {
  float top = y + (helmet ? s * 0.16f : 0.f), h = s - (top - y);
  DrawRectangleRounded({ x, top, s, h * 0.60f }, 0.30f, 4, c);
  surface()->DrawTriangle({ x, top + h * 0.52f }, { x + s, top + h * 0.52f }, { x + s * 0.5f, top + h }, c);
  if (helmet) DrawRectangleRounded({ x + s * 0.16f, y, s * 0.68f, s * 0.24f }, 0.8f, 4, c);
}
static void IconC4(float x, float y, float s, Color c) {
  // bomb pack: body with a strap, a small keypad window and the antenna
  DrawRectangleRounded({ x, y + s * 0.30f, s, s * 0.66f }, 0.20f, 4, c);
  Color dark = { 0, 0, 0, 150 };
  surface()->DrawFilledRect(x + s * 0.16f, y + s * 0.46f, s * 0.30f, s * 0.26f, dark);
  surface()->DrawFilledRect(x + s * 0.58f, y + s * 0.46f, s * 0.26f, s * 0.06f, dark);
  surface()->DrawFilledRect(x + s * 0.58f, y + s * 0.60f, s * 0.26f, s * 0.06f, dark);
  surface()->DrawLine(x + s * 0.72f, y + s * 0.32f, x + s * 0.72f, y + s * 0.04f, c, std::max(1.f, s * 0.10f));
}
static void IconDefuser(float x, float y, float s, Color c) {
  surface()->DrawFilledRect(x, y + s * 0.36f, s * 0.86f, s * 0.28f, c);
  surface()->DrawFilledRect(x + s * 0.62f, y + s * 0.06f, s * 0.17f, s * 0.32f, c);
}


// fallback weapon silhouettes (used when the CS:S content / CS:GO icons are not installed)
static void DrawWeaponGlyph(int wtype, float x, float y, float w, float h, Color c) {
  const float t = std::max(1.f, h * 0.14f);
  auto R = [&](float rx, float ry, float rw, float rh) { surface()->DrawFilledRect(x + w * rx, y + h * ry, w * rw, h * rh, c); };
  switch (wtype) {
    case WEAPONTYPE_PISTOL:
      R(0.02f, 0.30f, 0.92f, 0.20f);            // slide
      R(0.10f, 0.48f, 0.26f, 0.40f);            // grip
      R(0.62f, 0.46f, 0.10f, 0.14f);            // trigger guard
      break;
    case WEAPONTYPE_SNIPER_RIFLE:
      R(0.00f, 0.40f, 1.00f, 0.16f);            // barrel + body
      R(0.28f, 0.22f, 0.30f, 0.14f);            // scope
      R(0.30f, 0.52f, 0.12f, 0.34f);            // grip
      R(0.74f, 0.50f, 0.22f, 0.22f);            // stock
      break;
    case WEAPONTYPE_SHOTGUN:
      R(0.00f, 0.34f, 0.86f, 0.24f);
      R(0.18f, 0.58f, 0.16f, 0.30f);
      R(0.60f, 0.56f, 0.18f, 0.22f);
      break;
    case WEAPONTYPE_SUBMACHINEGUN:
      R(0.02f, 0.38f, 0.90f, 0.18f);
      R(0.24f, 0.56f, 0.10f, 0.34f);            // magazine
      R(0.70f, 0.52f, 0.20f, 0.16f);
      break;
    case WEAPONTYPE_GRENADE:
      DrawRectangleRounded({ x + w * 0.18f, y + h * 0.26f, w * 0.64f, h * 0.70f }, 0.30f, 5, c);
      R(0.40f, 0.02f, 0.20f, 0.26f);            // fuse
      R(0.60f, 0.10f, 0.26f, 0.10f);            // lever
      break;
    case WEAPONTYPE_KNIFE:
      R(0.02f, 0.46f, 0.30f, 0.18f);
      surface()->DrawTriangle({ x + w * 0.32f, y + h * 0.34f }, { x + w * 0.32f, y + h * 0.74f }, { x + w, y + h * 0.50f }, c);
      break;
    default:                                    // rifle / machine gun
      R(0.00f, 0.38f, 1.00f, 0.18f);
      R(0.22f, 0.16f, 0.40f, 0.14f);            // top rail / carry handle
      R(0.28f, 0.54f, 0.12f, 0.36f);            // magazine
      R(0.56f, 0.54f, 0.10f, 0.28f);            // grip
      R(0.76f, 0.48f, 0.22f, 0.20f);            // stock
      break;
  }
  (void)t;
}

// ------------------------------------------------------------------------------------------------
// bottom left: money, buy-zone / bomb / defuse icons
static void DrawMoney(CCSPlayer* p, float sc) {
  int H = surface()->ScreenTall();
  const float x = 26 * sc, size = 19 * sc, y = H - HudBottomInset(sc) - 96 * sc;
  if (s_lastMoney >= 0 && p->m_iAccount != s_lastMoney) { s_moneyDelta = p->m_iAccount - s_lastMoney; s_moneyDeltaTime = gpGlobals->realtime; }
  s_lastMoney = p->m_iAccount;
  float since = gpGlobals->realtime - s_moneyDeltaTime;
  Color c = hudgo::Text;
  if (since < 0.6f && s_moneyDelta < 0) c = hudgo::Danger;
  else if (s_moneyDelta > 0 && since < 0.9f) c = { 205, 255, 205, 255 };
  const char* mt = TextFormat("$%d", p->m_iAccount);
  HudText(FONT_GO_HUGE, mt, x, y, size, c);
  float w = HudTextW(FONT_GO_HUGE, mt, size);
  if (since < 3.f && s_moneyDelta != 0) {
    Color dc = s_moneyDelta > 0 ? hudgo::Gain : hudgo::Danger;
    dc.a = (unsigned char)(255 * Clamp(3.f - since, 0.f, 1.f));
    HudText(FONT_GO_BOLD, TextFormat("%s%d", s_moneyDelta > 0 ? "+" : "-", std::abs(s_moneyDelta)), x + w + 7 * sc, y + 2 * sc, size * 0.78f, dc);
  }
  // status icons above the money
  float ix = x, iy = y - 19 * sc, isz = 13 * sc;
  if (p->m_bInBuyZone && CSGameRules() && !CSGameRules()->IsBuyTimeElapsed()) {
    Color bc = { 120, 225, 130, 230 };
    HudText(FONT_GO_HUGE, "$", ix, iy - 3 * sc, isz * 1.25f, bc);
    ix += isz + 8 * sc;
  }
  if (p->HasC4()) {
    Color cc = p->m_bInBombZone ? (std::fmod(gpGlobals->realtime, 0.5f) < 0.25f ? Color{ 90, 255, 100, 255 } : Color{ 40, 140, 45, 235 }) : Color{ 100, 210, 105, 220 };
    IconC4(ix, iy, isz, cc); ix += isz + 8 * sc;
  }
  if (p->m_bHasDefuser) { IconDefuser(ix, iy + 2 * sc, isz, { 110, 205, 225, 220 }); ix += isz + 8 * sc; }
}

// bottom left: health + armor with thin bars
static void DrawVitals(CCSPlayer* p, float sc) {
  int H = surface()->ScreenTall();
  const float x = 26 * sc;
  const int hp = std::max(0, p->m_iHealth), armor = std::max(0, p->m_ArmorValue);
  const bool crit = hp <= 25;
  const float pulse = 0.5f + 0.5f * std::sin(gpGlobals->realtime * 10.f);
  Color hpC = crit ? Color{ 240, 66, 52, (unsigned char)(190 + 65 * pulse) } : hudgo::Text;

  // health row
  {
    const float numSize = 27 * sc, ic = 14 * sc, barW = 84 * sc, barH = 3 * sc;
    float rowY = H - HudBottomInset(sc) - 62 * sc;
    IconCross(x, rowY + numSize * 0.42f - ic * 0.5f, ic, hpC);
    const char* t = TextFormat("%d", hp);
    HudText(FONT_GO_HUGE, t, x + ic + 9 * sc, rowY, numSize, hpC);
    float bx = x + ic + 9 * sc + HudTextW(FONT_GO_HUGE, t, numSize) + 10 * sc;
    HudBar(bx, rowY + numSize * 0.42f - barH * 0.5f, barW, barH, hp / 100.f, crit ? hpC : Alpha(hudgo::Text, 0.88f));
  }
  // armor row
  {
    const float numSize = 20 * sc, ic = 13 * sc, barW = 84 * sc, barH = 3 * sc;
    float rowY = H - HudBottomInset(sc) - 32 * sc;
    Color ac = armor > 0 ? hudgo::Text : Alpha(hudgo::TextFaint, 0.85f);
    IconShield(x, rowY + numSize * 0.42f - ic * 0.62f, ic, ac, p->m_bHasHelmet);
    const char* t = TextFormat("%d", armor);
    HudText(FONT_GO_HUGE, t, x + ic + 9 * sc, rowY, numSize, ac);
    float bx = x + ic + 9 * sc + HudTextW(FONT_GO_HUGE, t, numSize) + 10 * sc;
    HudBar(bx, rowY + numSize * 0.42f - barH * 0.5f, barW, barH, armor / 100.f, Alpha(hudgo::ArmorCol, 0.85f));
  }
}

// ------------------------------------------------------------------------------------------------
// top centre: CT score | round timer | T score, with alive pips and round-win pips (CS:GO score bar)
static void DrawTimer(float sc) {
  CCSGameRules* gr = CSGameRules();
  if (!gr) return;
  int W = surface()->ScreenWide();
  const int ctWin = gr->m_iNumCTWins, tWin = gr->m_iNumTerroristWins;
  const int ctAlive = gr->m_iMatchStats_PlayersAlive_CT, tAlive = gr->m_iMatchStats_PlayersAlive_T;
  const bool planted = gr->m_bBombPlanted && g_pPlantedC4 && !g_pPlantedC4->m_bBombDefused && !g_pPlantedC4->m_bExploded;
  const bool freeze = !planted && gr->IsFreezePeriod() && !gr->IsDeathmatch();

  float secs = planted ? (g_pPlantedC4->m_flC4Blow - ServerTime())
             : freeze ? gr->GetFreezeTimeRemaining() : gr->GetRoundRemainingTime();
  secs = std::max(0.f, secs);
  const char* timerTxt = TextFormat("%d:%02d", (int)secs / 60, (int)secs % 60);
  const char* ctTxt = TextFormat("%d", ctWin);
  const char* tTxt = TextFormat("%d", tWin);

  const float sScore = 20 * sc, pad = 13 * sc, gap = 13 * sc, dotS = 7 * sc, dotGap = 3 * sc;
  const float ctW = HudTextW(FONT_GO_HUGE, ctTxt, sScore), tW = HudTextW(FONT_GO_HUGE, tTxt, sScore);
  const float timerW = HudTextW(FONT_GO_HUGE, timerTxt, sScore);
  const float c4W = planted ? 14 * sc + 6 * sc : 0.f;
  const float ctDotsW = ctAlive > 0 ? ctAlive * dotS + (ctAlive - 1) * dotGap : 0.f;
  const float tDotsW = tAlive > 0 ? tAlive * dotS + (tAlive - 1) * dotGap : 0.f;

  const float ph = 35 * sc, py = 8 * sc;
  const float pw = pad * 2 + ctDotsW + (ctDotsW > 0 ? gap : 0) + ctW + gap + c4W + timerW + gap + tW + (tDotsW > 0 ? gap : 0) + tDotsW;
  const float px = W * 0.5f - pw * 0.5f;
  DrawRectangleRounded({ px, py, pw, ph }, 0.16f, 6, hudgo::Panel);

  const float textY = py + 4 * sc;
  float cx = px + pad;
  // CT alive pips
  for (int i = 0; i < ctAlive; i++) {
    DrawRectangleRounded({ cx + i * (dotS + dotGap), py + ph * 0.5f - dotS * 0.5f - 1 * sc, dotS, dotS }, 0.35f, 4, Alpha(hudgo::CT, 0.92f));
  }
  cx += ctDotsW + (ctDotsW > 0 ? gap : 0);
  HudText(FONT_GO_HUGE, ctTxt, cx, textY, sScore, hudgo::CT);
  float ctX = cx;
  cx += ctW + gap;
  // round timer (red and blinking when it matters)
  Color tc = hudgo::Text;
  if (planted) { tc = { 245, 235, 235, 255 }; if (secs < 10.f && std::fmod(gpGlobals->realtime, 0.5f) < 0.25f) tc = hudgo::Danger; }
  else if (freeze) tc = Alpha(hudgo::TextDim, 0.9f);
  else if (secs < 10.f) { tc = hudgo::Danger; tc.a = (unsigned char)(200 + 55 * (0.5f + 0.5f * std::sin(gpGlobals->realtime * 8.f))); }
  if (planted) { IconC4(cx, textY + 2 * sc, 14 * sc, secs < 10.f ? hudgo::Danger : Color{ 245, 190, 70, 245 }); cx += c4W; }
  HudText(FONT_GO_HUGE, timerTxt, cx, textY, sScore, tc);
  cx += timerW + gap;
  float tX = cx;
  HudText(FONT_GO_HUGE, tTxt, cx, textY, sScore, hudgo::T);
  cx += tW + (tDotsW > 0 ? gap : 0);
  for (int i = 0; i < tAlive; i++) {
    DrawRectangleRounded({ cx + i * (dotS + dotGap), py + ph * 0.5f - dotS * 0.5f - 1 * sc, dotS, dotS }, 0.35f, 4, Alpha(hudgo::T, 0.92f));
  }

  // round-win pips under the scores (CS:GO "match points" strip)
  const float pipH = 2.5f * sc, pipW = 4 * sc, pipGap = 2 * sc, pipY = py + ph - 6 * sc;
  for (int i = 0; i < std::min(ctWin, 15); i++)
    surface()->DrawFilledRect(ctX + ctW - (i + 1) * (pipW + pipGap) + pipGap, pipY, pipW, pipH, Alpha(hudgo::CT, 0.85f));
  for (int i = 0; i < std::min(tWin, 15); i++)
    surface()->DrawFilledRect(tX + i * (pipW + pipGap), pipY, pipW, pipH, Alpha(hudgo::T, 0.85f));
}

// ------------------------------------------------------------------------------------------------
// bottom right: grenades, weapon name + silhouette, "clip | reserve"
static void DrawAmmo(CCSPlayer* p, float sc) {
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  C_WeaponCSBase* w = p->GetActiveCSWeapon();
  const float right = W - 26 * sc, sClip = 32 * sc, sRes = 17 * sc;
  const float baseY = H - HudBottomInset(sc) - 26 * sc - sClip;

  // weapon name + silhouette (CS:GO: small dim name, white silhouette above the ammo numbers)
  const float nameY = baseY - 19 * sc;
  float iconLeft = right;
  if (w && w->m_pWeaponInfo) {
    HudTextRight(FONT_GO_BOLD, w->m_pWeaponInfo->printName, right, nameY, 13 * sc, hudgo::TextDim);
    const float ih = 17 * sc, iy = nameY - ih - 5 * sc;
    if (const Texture2D* ic = VGUI_WeaponIcon(w->GetCSWeaponID())) {
      float iw = std::min(ih * ic->width / (float)std::max(1, ic->height), 44 * sc);
      DrawTexturePro(*ic, { 0, 0, (float)ic->width, (float)ic->height }, { right - iw, iy, iw, ih }, { 0, 0 }, 0, Alpha(hudgo::Text, 0.9f));
      iconLeft = right - iw;
    } else {
      DrawWeaponGlyph(w->GetWeaponType(), right - 40 * sc, iy, 40 * sc, ih, Alpha(hudgo::Text, 0.75f));
      iconLeft = right - 40 * sc;
    }
  }

  // grenade icons: a row to the left of the weapon silhouette (CS:GO inventory strip)
  int gcount[WEAPON_MAX] = {};
  for (int i = 0; i < CCSPlayer::MAX_WEAPONS; i++) {
    C_WeaponCSBase* ww = p->m_hMyWeapons[i];
    if (!ww || !ww->m_pWeaponInfo || ww->GetWeaponType() != WEAPONTYPE_GRENADE) continue;
    int id = ww->GetCSWeaponID(); if (id > 0 && id < WEAPON_MAX) gcount[id] += std::max(1, ww->m_iClip1);   // grenades stack in the clip
  }
  static const int gorder[] = { WEAPON_HEGRENADE, WEAPON_FLASHBANG, WEAPON_SMOKEGRENADE, WEAPON_MOLOTOV, WEAPON_INCGRENADE };
  std::vector<int> grenades;
  for (int id : gorder) for (int k = 0; k < gcount[id]; k++) grenades.push_back(id);
  const bool nadeInHand = w && w->GetWeaponType() == WEAPONTYPE_GRENADE;
  if (nadeInHand) {   // the grenade in hand already has its own icon -> drop one copy from the strip
    auto it = std::find(grenades.begin(), grenades.end(), w->GetCSWeaponID());
    if (it != grenades.end()) grenades.erase(it);
  }
  if (!grenades.empty() && w) {
    const float ih = 13 * sc, gy = nameY - ih - 3 * sc;
    float gx = iconLeft - 8 * sc;
    for (int i = (int)grenades.size() - 1; i >= 0; i--) {
      // highlight the grenade that is currently in hand
      Color gc = Alpha(hudgo::Text, 0.68f);
      const Texture2D* ic = VGUI_WeaponIcon(grenades[i]);
      float iw;
      if (ic) {
        iw = std::min(ih * ic->width / (float)std::max(1, ic->height), 30 * sc);
        DrawTexturePro(*ic, { 0, 0, (float)ic->width, (float)ic->height }, { gx - iw, gy, iw, ih }, { 0, 0 }, 0, gc);
      } else {
        iw = 12 * sc;
        DrawWeaponGlyph(WEAPONTYPE_GRENADE, gx - iw, gy, iw, ih, gc);
      }
      gx -= iw + 5 * sc;
    }
  }

  if (!w) return;
  const int wtype = w->GetWeaponType();
  const bool nade = wtype == WEAPONTYPE_GRENADE;
  if (!nade && !w->IsGun()) return;   // knife / c4: no ammo counter

  const int clip = std::max(0, w->m_iClip1), res = std::max(0, w->m_iPrimaryReserveAmmoCount);
  const char* clipTxt = TextFormat("%d", nade ? std::max(1, clip) : clip);
  Color cc = hudgo::Text;
  if (!nade) {
    if (clip == 0) { cc = hudgo::Danger; cc.a = (unsigned char)(170 + 85 * (0.5f + 0.5f * std::sin(gpGlobals->realtime * 8.f))); }
    else if (w->m_pWeaponInfo && w->m_pWeaponInfo->clipSize > 0 && clip * 4 <= w->m_pWeaponInfo->clipSize) cc = { 245, 205, 120, 250 };
    if (w->m_bInReload) cc = Alpha(cc, 0.55f);
  }
  if (nade) { HudTextRight(FONT_GO_HUGE, clipTxt, right, baseY, sClip, cc); return; }

  const char* resTxt = TextFormat("%d", res);
  const float resW = HudTextW(FONT_GO_HUGE, resTxt, sRes);
  const float clipW = HudTextW(FONT_GO_HUGE, clipTxt, sClip);
  const float sepX = right - resW - 9 * sc;
  HudText(FONT_GO_HUGE, resTxt, right - resW, baseY + (sClip - sRes) * 0.9f, sRes, Alpha(hudgo::TextDim, 0.95f));
  surface()->DrawFilledRect(sepX - 1, baseY + sClip * 0.32f, std::max(1.f, 1.4f * sc), sClip * 0.42f, Alpha(hudgo::TextFaint, 0.7f));
  HudText(FONT_GO_HUGE, clipTxt, sepX - 9 * sc - clipW, baseY, sClip, cc);
}

// red vignette when the player is close to death (CS:GO low-hp pulse)
static void DrawLowHpVignette(CCSPlayer* p) {
  if (!p->IsAlive() || p->m_iHealth > 40) return;
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  float k = Clamp((40.f - p->m_iHealth) / 40.f, 0.f, 1.f);
  float pulse = 0.75f + 0.25f * std::sin(gpGlobals->realtime * 5.f);
  unsigned char a = (unsigned char)(70 * k * pulse);
  if (!a) return;
  Color red = { 130, 8, 6, 0 };
  float th = H * 0.22f;
  red.a = a;        surface()->DrawGradientV(0, 0, (float)W, th, red, { 130, 8, 6, 0 });
  red.a = a;        surface()->DrawGradientV(0, H - th, (float)W, th, { 130, 8, 6, 0 }, red);
  red.a = (unsigned char)(a * 0.8f);
  surface()->DrawGradientH(0, 0, W * 0.22f, (float)H, red, { 130, 8, 6, 0 });
  surface()->DrawGradientH(W - W * 0.22f, 0, W * 0.22f, (float)H, { 130, 8, 6, 0 }, red);
}

static void DrawCrosshair(CCSPlayer* p, float sc) {
  if (!crosshair.GetBool()) return;
  C_WeaponCSBase* w = p->GetActiveCSWeapon(); if (!w) return;
  if (IsSniper(w->GetCSWeaponID()) && w->m_zoomLevel == 0 && !cl.forceCrosshair) return; // snipers have no crosshair
  if (w->m_zoomLevel > 0 && IsSniper(w->GetCSWeaponID())) return;
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  float cx = W * 0.5f, cy = H * 0.5f;
  // where the shots actually go (aim punch)
  if (!cl.removeVisualRecoil && w->IsGun()) {
    extern ConVar weapon_recoil_scale; extern ConVar view_recoil_tracking;
    float k = weapon_recoil_scale.GetFloat() * (1.f - view_recoil_tracking.GetFloat());
    float pxPerDeg = H / (2.f * std::tan(DEG2RADF(cl.view.VerticalFov(cl.fov)) * 0.5f)) * DEG2RADF(1.f);
    cx -= p->m_aimPunchAngle.y * k * pxPerDeg; cy += p->m_aimPunchAngle.x * k * pxPerDeg;
  }
  float gap = cl_crosshairgap.GetFloat() * sc;
  if (cl_dynamiccrosshair.GetBool() && w->IsGun()) gap += std::min(60.f, w->GetInaccuracy() * 300.f) * sc;
  float len = cl_crosshairsize.GetFloat() * 2 * sc, th = std::max(1.f, cl_crosshairthickness.GetFloat() * sc);
  Color c = { (unsigned char)cl_crosshaircolor_r.GetInt(), (unsigned char)cl_crosshaircolor_g.GetInt(), (unsigned char)cl_crosshaircolor_b.GetInt(), 230 };
  auto bar = [&](float x, float y, float w2, float h2) { surface()->DrawFilledRect(std::floor(x), std::floor(y), w2, h2, c); };
  bar(cx - th / 2, cy - gap - len, th, len); bar(cx - th / 2, cy + gap, th, len);
  bar(cx - gap - len, cy - th / 2, len, th); bar(cx + gap, cy - th / 2, len, th);
  if (cl_crosshairdot.GetBool()) bar(cx - th / 2, cy - th / 2, th, th);
}

static void DrawScope(CCSPlayer* p) {
  C_WeaponCSBase* w = p->GetActiveCSWeapon();
  if (!w || w->m_zoomLevel == 0 || !IsSniper(w->GetCSWeaponID())) return;
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  float cx = W * 0.5f, cy = H * 0.5f;
  if (cl.removeScope) { // cheat: simple lines across the screen
    surface()->DrawFilledRect(0, cy, (float)W, 1, { 0, 0, 0, 255 }); surface()->DrawFilledRect(cx, 0, 1, (float)H, { 0, 0, 0, 255 });
    return;
  }
  float s = (float)H;
  if (s_scopeTex.id) DrawTexturePro(s_scopeTex, { 0, 0, (float)s_scopeTex.width, (float)s_scopeTex.height }, { cx - s / 2, 0, s, s }, { 0, 0 }, 0, WHITE);
  surface()->DrawFilledRect(0, 0, cx - s / 2 + 1, (float)H, BLACK); surface()->DrawFilledRect(cx + s / 2 - 1, 0, W - (cx + s / 2) + 2, (float)H, BLACK);
  // crosshair: thin center lines + thicker outer
  surface()->DrawFilledRect(cx - s / 2, cy, s, 1, BLACK); surface()->DrawFilledRect(cx, 0, 1, (float)H, BLACK);
  surface()->DrawFilledRect(cx - s / 2, cy - 1, s * 0.3f, 3, BLACK); surface()->DrawFilledRect(cx + s * 0.2f, cy - 1, s * 0.3f, 3, BLACK);
  surface()->DrawFilledRect(cx - 1, cy + s * 0.2f, 3, s * 0.3f, BLACK);
  // inaccuracy blur ring
  float inacc = w->GetInaccuracy();
  if (inacc > 0.004f) { float r = std::min(s * 0.45f, inacc * s * 3.5f); DrawRing({ cx, cy }, r, r + 2, 0, 360, 48, { 0, 0, 0, 90 }); }
}

static void DrawFlash(CCSPlayer* p) {
  if (cl.removeFlash) return;
  float a = p->GetFlashAlpha();
  if (a <= 0) return;
  surface()->DrawFilledRect(0, 0, (float)surface()->ScreenWide(), (float)surface()->ScreenTall(), { 255, 255, 255, (unsigned char)Clamp(a, 0.f, 255.f) });
}

static void DrawDamageIndicator(CCSPlayer* p, float sc) {
  float since = gpGlobals->realtime - cl.lastDamageTakenTime;
  if (since > 1.2f || cl.lastDamageDir.LengthSqr() < 1) return;
  float a = 1.f - since / 1.2f;
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  float yaw = RAD2DEGF(std::atan2(cl.lastDamageDir.y, cl.lastDamageDir.x)) - cl.view.angles.y;
  float r = 90 * sc; float ang = DEG2RADF(-yaw - 90);
  Vector2 c{ W * 0.5f + std::cos(ang) * r, H * 0.5f + std::sin(ang) * r };
  float perp = ang + (float)M_PI / 2;
  Vector2 a1{ c.x + std::cos(perp) * 22 * sc, c.y + std::sin(perp) * 22 * sc }, a2{ c.x - std::cos(perp) * 22 * sc, c.y - std::sin(perp) * 22 * sc };
  Vector2 tip{ c.x + std::cos(ang) * 14 * sc, c.y + std::sin(ang) * 14 * sc };
  surface()->DrawTriangle(a1, a2, tip, { 255, 30, 20, (unsigned char)(200 * a) });
  (void)p;
}

static void DrawProgressBar(CCSPlayer* p, float sc) {
  float frac = -1; const char* label = nullptr;
  if (p->m_bIsPlanting) { C_WeaponCSBase* w = p->GetActiveCSWeapon(); if (w && w->GetCSWeaponID() == WEAPON_C4) { auto* c4 = static_cast<CC4*>(w); if (c4->m_bStartedArming) { frac = Clamp(1.f - (c4->m_fArmedTime - gpGlobals->curtime) / 3.f, 0.f, 1.f); label = "PLANTING BOMB"; } } }
  if (p->m_bIsDefusing && g_pPlantedC4) { frac = Clamp(1.f - (g_pPlantedC4->m_flDefuseCountDown - ServerTime()) / g_pPlantedC4->m_flDefuseLength, 0.f, 1.f); label = "DEFUSING BOMB"; }
  if (frac < 0) return;
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  // CS:GO/CS2 action bar: thin track + solid fill, label in small caps above
  float w = 260 * sc, h = 4 * sc, x = W * 0.5f - w / 2, y = H * 0.70f;
  const bool defuse = p->m_bIsDefusing;
  Color fill = defuse ? Color{ 120, 190, 255, 245 } : Color{ 235, 238, 242, 245 };
  surface()->DrawFilledRect(x, y, w, h, { 0, 0, 0, 150 });
  surface()->DrawFilledRect(x, y, w * frac, h, fill);
  HudTextCentered(FONT_GO_BOLD, label, W * 0.5f, y - 26 * sc, 13 * sc, { 235, 238, 242, 240 });
}

// CS:GO-style round radar: the map is a triangle fan whose texture coordinates come from the inverse of the
// world -> radar transform (both affine), so the circle is exact without a stencil
static void DrawRadar(CCSPlayer* lp, float sc) {
  if (!cl_radar.GetBool() || !s_radarTex.id) return;
  const CMapInfo& m = g_MapInfo;
  const float size = 218 * sc, R = size * 0.5f, cx = 12 * sc + R, cy = 12 * sc + R;
  Vector center = cl.viewEntity ? cl.viewEntity->m_vecOrigin : cl.viewOrigin;
  float yaw = cl_radar_rotate.GetBool() ? cl.view.angles.y : 90.f;
  float unitsPerPx = 1.f / (cl_radar_scale.GetFloat() * 0.14f) * (1.f / sc);   // ~ world units per screen px
  const float a = DEG2RADF(yaw - 90.f), ca = std::cos(a), sa = std::sin(a);
  auto toRadar = [&](const Vector& p, Vector2& out) {
    Vector d = p - center;
    float rx = d.x * ca + d.y * sa, ry = -d.x * sa + d.y * ca;
    out = { cx + rx / unitsPerPx, cy - ry / unitsPerPx };
  };
  // radar px -> world xy -> texture uv (solve world = W0 + DU*u + DV*v in the xy plane)
  const float W = (float)s_radarTex.width, H = (float)s_radarTex.height;
  const float det = s_radarDU.x * s_radarDV.y - s_radarDU.y * s_radarDV.x;
  auto toUV = [&](float sx, float sy, float& u, float& v) {
    float rx = (sx - cx) * unitsPerPx, ry = -(sy - cy) * unitsPerPx;
    float wx = center.x + rx * ca - ry * sa - s_radarW0.x, wy = center.y + rx * sa + ry * ca - s_radarW0.y;
    float pu = (wx * s_radarDV.y - wy * s_radarDV.x) / det, pv = (s_radarDU.x * wy - s_radarDU.y * wx) / det;
    u = pu / W; v = pv / H;
  };
  const int N = 64;
  // backdrop disc + map
  for (int pass = 0; pass < 2; pass++) {
    if (pass == 1 && std::fabs(det) < 1e-6f) break;
    if (pass == 1) rlSetTexture(s_radarTex.id);
    rlBegin(RL_TRIANGLES);
    for (int i = 0; i < N; i++) {
      float a0 = i * 2.f * (float)M_PI / N, a1 = (i + 1) * 2.f * (float)M_PI / N;
      float px[3] = { cx, cx + R * std::cos(a1), cx + R * std::cos(a0) }, py[3] = { cy, cy + R * std::sin(a1), cy + R * std::sin(a0) };
      for (int k = 0; k < 3; k++) {
        if (pass == 0) rlColor4ub(10, 12, 14, 170);
        else { float u, v; toUV(px[k], py[k], u, v); rlColor4ub(255, 255, 255, 225); rlTexCoord2f(u, v); }
        rlVertex2f(px[k], py[k]);
      }
    }
    rlEnd();
    if (pass == 1) rlSetTexture(0);
  }
  auto inside = [&](const Vector2& s, float margin) { float dx = s.x - cx, dy = s.y - cy; return dx * dx + dy * dy <= (R - margin) * (R - margin); };
  // bombsite letters
  for (auto& z : m.zones) { if (z.type > ZONE_BOMBSITE_B) continue; Vector2 s; toRadar(z.Center(), s); if (inside(s, 8 * sc)) surface()->DrawTextCentered(FONT_GO_BOLD, z.type == ZONE_BOMBSITE_A ? "A" : "B", s.x, s.y - 8 * sc, { 255, 220, 120, 210 }, true); }
  // players: teammates always (clamped to the rim like CS:GO), enemies while spotted
  for (CCSPlayer* p : UTIL_GetPlayers(true)) {
    if (p == cl.viewEntity) continue;
    bool friendly = lp->m_iTeamNum < TEAM_TERRORIST || p->m_iTeamNum == lp->m_iTeamNum;
    bool spotted = gpGlobals->realtime - p->m_flSpottedTime < 0.3f;
    if (!friendly && !spotted) continue;
    Vector2 s; toRadar(p->m_LagRecords.empty() ? p->m_vecOrigin : p->m_LagRecords.front().origin, s);
    if (!inside(s, 4 * sc)) {
      if (!friendly) continue;
      float dx = s.x - cx, dy = s.y - cy, l = std::sqrt(dx * dx + dy * dy); s = { cx + dx / l * (R - 4 * sc), cy + dy / l * (R - 4 * sc) };
    }
    Color c = friendly ? (p->m_iTeamNum == TEAM_CT ? Color{ 93, 152, 230, 255 } : Color{ 230, 168, 60, 255 }) : Color{ 240, 50, 50, 255 };
    if (p->HasC4() && (lp->m_iTeamNum == TEAM_TERRORIST || spotted)) c = { 255, 60, 200, 255 };
    // dot with a facing notch
    float yawp = DEG2RADF(p->m_angEyeAngles.y - yaw);
    Vector2 f = { -std::sin(yawp), -std::cos(yawp) };
    surface()->DrawFilledCircle(s.x, s.y, 3.6f * sc, BLACK);
    surface()->DrawFilledCircle(s.x, s.y, 2.8f * sc, c);
    surface()->DrawLine(s.x, s.y, s.x + f.x * 6 * sc, s.y + f.y * 6 * sc, c);
  }
  // bomb
  Vector bombPos; bool bomb = false;
  if (g_pPlantedC4) { bombPos = g_pPlantedC4->m_vecOrigin; bomb = true; }
  else g_EntityList.ForEach([&](C_BaseEntity* e) { if (e->IsWeapon()) { auto* w = static_cast<C_WeaponCSBase*>(e); if (w->GetCSWeaponID() == WEAPON_C4 && !w->m_hOwner) { bombPos = w->m_vecOrigin; bomb = true; } } });
  if (bomb && (lp->m_iTeamNum == TEAM_TERRORIST || g_pPlantedC4)) {
    Vector2 s; toRadar(bombPos, s);
    if (inside(s, 4 * sc)) {
      Color bc = g_pPlantedC4 && std::fmod(gpGlobals->realtime, 0.6f) < 0.3f ? Color{ 255, 40, 40, 255 } : Color{ 255, 200, 0, 255 };
      if (const Texture2D* ic = VGUI_SvgIcon("weapon_c4")) { float ih = 11 * sc, iw = ih * ic->width / (float)std::max(1, ic->height); DrawTexturePro(*ic, { 0, 0, (float)ic->width, (float)ic->height }, { s.x - iw / 2, s.y - ih / 2, iw, ih }, { 0, 0 }, 0, bc); }
      else surface()->DrawFilledRect(s.x - 3 * sc, s.y - 2 * sc, 6 * sc, 4 * sc, bc);
    }
  }
  // local player: white arrow in the centre
  surface()->DrawTriangle({ cx, cy - 7 * sc }, { cx - 4.5f * sc, cy + 5 * sc }, { cx + 4.5f * sc, cy + 5 * sc }, { 255, 255, 255, 245 });
  // rim
  for (int i = 0; i < N; i++) {
    float a0 = i * 2.f * (float)M_PI / N, a1 = (i + 1) * 2.f * (float)M_PI / N;
    surface()->DrawLine(cx + R * std::cos(a0), cy + R * std::sin(a0), cx + R * std::cos(a1), cy + R * std::sin(a1), { 200, 205, 215, 150 });
  }
  // place name under the radar
  const char* place = g_MapInfo.GetPlaceName(center);
  if (place && *place) surface()->DrawTextCentered(FONT_GO_BOLD, place, cx, cy + R + 6 * sc, { 235, 235, 235, 220 }, true);
}

// CS:GO overheads: teammate weapon icon, name and a down arrow above the head (half transparent, close range only)
ConVar cl_teamid_overhead("cl_teamid_overhead", "1", FCVAR_ARCHIVE, "Show teammate names / weapons above their heads");
ConVar cl_teamid_overhead_maxdist("cl_teamid_overhead_maxdist", "700", FCVAR_ARCHIVE, "Overheads are hidden beyond this distance");
ConVar cl_teamid_overhead_alpha("cl_teamid_overhead_alpha", "0.5", FCVAR_ARCHIVE, "Overhead opacity", true, 0.1f, true, 1.f);
static void DrawOverheads(CCSPlayer* lp, float sc) {
  if (!cl_teamid_overhead.GetBool() || lp->m_iTeamNum < TEAM_TERRORIST) return;
  const float maxd = cl_teamid_overhead_maxdist.GetFloat(), fade = maxd * 0.8f, base = cl_teamid_overhead_alpha.GetFloat();
  // far ones first so the closest teammate's overhead ends up on top; a bit smaller with distance
  std::vector<std::pair<float, CCSPlayer*>> list;
  for (CCSPlayer* p : UTIL_GetPlayers(true)) {
    if (p == lp || p == cl.viewEntity || p->m_iTeamNum != lp->m_iTeamNum) continue;
    float d = (p->m_bRenderPoseValid ? p->m_vecRenderOrigin : p->m_vecOrigin).DistTo(cl.view.origin);
    if (d <= maxd) list.push_back({ d, p });
  }
  std::sort(list.begin(), list.end(), [](const auto& a, const auto& b) { return a.first > b.first; });
  const float sc0 = sc;
  for (auto& [dist, p] : list) {
    Vector o = p->m_bRenderPoseValid ? p->m_vecRenderOrigin : p->m_vecOrigin;
    const float sc = sc0 * Clamp(1.15f - dist / 1200.f, 0.7f, 1.f);
    float al = base * (dist > fade ? 1.f - (dist - fade) / (maxd - fade) : 1.f);
    Vector head = o + Vector(0, 0, 64.f - 18.f * Clamp(p->m_flDuckAmount, 0.f, 1.f) + 14.f);
    Vector2D s; if (!cl.view.WorldToScreen(head, s)) continue;
    Color tc = p->m_iTeamNum == TEAM_CT ? Color{ 110, 170, 245, 255 } : Color{ 240, 180, 70, 255 };
    auto A = [&](Color c, float k = 1.f) { c.a = (unsigned char)Clamp(c.a * al * k, 0.f, 255.f); return c; };
    // arrow (bottom)
    float ay = s.y - 4 * sc;
    surface()->DrawTriangle({ s.x - 7 * sc, ay - 8 * sc }, { s.x, ay }, { s.x + 7 * sc, ay - 8 * sc }, A(tc));   // counter-clockwise on screen
    // name
    const char* name = p->GetPlayerName();
    float ny = ay - 26 * sc;
    surface()->DrawTextCentered(FONT_GO_BOLD, name, s.x, ny, A({ 255, 255, 255, 255 }), true);
    // weapon icon (top)
    if (C_WeaponCSBase* w = p->GetActiveCSWeapon())
      if (const Texture2D* ic = VGUI_WeaponIcon(w->GetCSWeaponID())) {
        float ih = 16 * sc, iw = std::min(ih * ic->width / (float)std::max(1, ic->height), 70 * sc);
        DrawTexturePro(*ic, { 0, 0, (float)ic->width, (float)ic->height }, { s.x - iw / 2, ny - ih - 3 * sc, iw, ih }, { 0, 0 }, 0, A({ 255, 255, 255, 255 }));
      }
  }
}

// CS:GO-style kill feed icons: weapon silhouettes from the CS:S buy menu pictures, small vector glyphs for the flags
enum { KF_HS, KF_WB, KF_SMOKE, KF_NOSCOPE, KF_BLIND };
static float KillIconWide(int kind, float h) { (void)kind; return h * 1.05f; }
static void DrawKillIcon(int kind, float x, float y, float h, Color c) {
  float cx = x + h * 0.52f, cy = y + h * 0.5f, r = h * 0.36f;
  switch (kind) {
    case KF_HS:       // head with a crosshair
      DrawCircleV({ cx, cy - r * 0.1f }, r, c);
      DrawCircleV({ cx, cy - r * 0.1f }, r * 0.55f, { 20, 20, 20, c.a });
      DrawCircleV({ cx, cy - r * 0.1f }, r * 0.22f, c);
      DrawLineEx({ cx - r * 1.35f, cy - r * 0.1f }, { cx - r * 0.7f, cy - r * 0.1f }, 1.5f, c);
      DrawLineEx({ cx + r * 0.7f, cy - r * 0.1f }, { cx + r * 1.35f, cy - r * 0.1f }, 1.5f, c);
      DrawLineEx({ cx, cy - r * 1.4f }, { cx, cy - r * 0.8f }, 1.5f, c);
      DrawLineEx({ cx, cy + r * 0.6f }, { cx, cy + r * 1.25f }, 1.5f, c);
      break;
    case KF_WB:       // bullet through a wall
      for (int k = 0; k < 3; k++)   // brick wall
        DrawRectangleRec({ cx - r * 0.45f, cy - r * 1.3f + k * r * 0.9f, r * 0.9f, r * 0.75f }, { c.r, c.g, c.b, (unsigned char)(c.a * 0.55f) });
      DrawLineEx({ cx - r * 1.5f, cy }, { cx + r * 0.9f, cy }, 2.5f, c);
      DrawTriangle({ cx + r * 1.5f, cy }, { cx + r * 0.8f, cy - r * 0.6f }, { cx + r * 0.8f, cy + r * 0.6f }, c);
      break;
    case KF_SMOKE:    // cloud
      DrawCircleV({ cx - r * 0.6f, cy + r * 0.25f }, r * 0.62f, c);
      DrawCircleV({ cx + r * 0.6f, cy + r * 0.25f }, r * 0.62f, c);
      DrawCircleV({ cx, cy - r * 0.3f }, r * 0.75f, c);
      break;
    case KF_NOSCOPE:  // crossed-out scope
      DrawRing({ cx, cy }, r * 0.85f, r * 1.1f, 0, 360, 24, c);
      DrawLineEx({ cx - r, cy }, { cx + r, cy }, 1.f, c);
      DrawLineEx({ cx, cy - r }, { cx, cy + r }, 1.f, c);
      DrawLineEx({ cx - r * 1.2f, cy + r * 1.2f }, { cx + r * 1.2f, cy - r * 1.2f }, 2.f, { 255, 80, 60, c.a });
      break;
    case KF_BLIND:    // eye
      DrawEllipse((int)cx, (int)cy, r * 1.3f, r * 0.7f, c);
      DrawCircleV({ cx, cy }, r * 0.45f, { 20, 20, 20, c.a });
      break;
  }
}
static void DrawKnifeGlyph(float x, float y, float w, float h, Color c) {
  // handle + guard + blade, pointing right (towards the victim) like the gun pictures
  float m = y + h * 0.5f;
  DrawRectangleRounded({ x, m - h * 0.22f, w * 0.33f, h * 0.3f }, 0.5f, 4, c);
  DrawRectangleRec({ x + w * 0.33f, m - h * 0.4f, w * 0.05f, h * 0.62f }, c);
  DrawTriangle({ x + w * 0.38f, m - h * 0.32f }, { x + w * 0.38f, m + h * 0.12f }, { x + w, m - h * 0.05f }, c);
}

static void DrawDeathNotices(float sc) {
  int W = surface()->ScreenWide();
  float y = 60 * sc;
  const float rowH = std::max(22.f, 24 * sc), iconH = rowH - 6;
  float lifetime = hud_deathnotice_time.GetFloat();
  while (!s_deaths.empty() && gpGlobals->realtime - s_deaths.front().time > lifetime + (s_deaths.front().localInvolved ? 4 : 0)) s_deaths.pop_front();
  for (auto& d : s_deaths) {
    const int wid = AliasToWeaponID(d.weapon);
    const Texture2D* icon = wid > 0 ? VGUI_WeaponIcon(wid) : nullptr;
    const bool knife = d.weapon == "knife";
    std::string wpnText = d.weapon == "world" ? "fell" : d.weapon;
    float iconW = icon ? iconH * icon->width / (float)std::max(1, icon->height) : knife ? iconH * 2.2f : surface()->GetTextSize(FONT_DEFAULT_BOLD, wpnText.c_str()).x;
    iconW = std::min(iconW, iconH * 4.2f);
    std::vector<int> flags;
    if (d.blind) flags.push_back(KF_BLIND);
    if (d.noscope) flags.push_back(KF_NOSCOPE);
    if (d.smoke) flags.push_back(KF_SMOKE);
    if (d.wallbang) flags.push_back(KF_WB);
    if (d.hs) flags.push_back(KF_HS);
    float flagsW = 0; for (int f : flags) flagsW += KillIconWide(f, iconH) + 3;
    std::string killer = d.killer; if (!d.assister.empty() && !killer.empty()) killer += " + " + d.assister;
    float kw = killer.empty() ? 0 : surface()->GetTextSize(FONT_UI, killer.c_str()).x;
    float vw = surface()->GetTextSize(FONT_UI, d.victim.c_str()).x;
    float total = (kw > 0 ? kw + 8 : 0) + iconW + (flagsW > 0 ? 6 + flagsW : 0) + 8 + vw;
    float x = W - 12 * sc - total - 8;
    Color bg = d.localInvolved ? Color{ 40, 10, 10, 190 } : Color{ 0, 0, 0, 140 };
    DrawRectangleRounded({ x - 8, y, total + 16, rowH }, 0.25f, 4, bg);
    if (d.localInvolved) DrawRectangleRoundedLinesEx({ x - 8, y, total + 16, rowH }, 0.25f, 4, 1.5f, { 220, 40, 40, 230 });
    float ty = y + (rowH - 15) * 0.5f;
    if (!killer.empty()) { surface()->DrawText(FONT_UI, killer.c_str(), x, ty, TeamColor(d.kteam)); x += kw + 8; }
    Color ic = { 235, 235, 235, 255 };
    if (icon) DrawTexturePro(*icon, { 0, 0, (float)icon->width, (float)icon->height }, { x, y + 3, iconW, iconH }, { 0, 0 }, 0, ic);
    else if (knife) DrawKnifeGlyph(x, y + 3, iconW, iconH, ic);
    else surface()->DrawText(FONT_DEFAULT_BOLD, wpnText.c_str(), x, ty, HUD);
    x += iconW;
    if (!flags.empty()) { x += 6; for (int f : flags) { DrawKillIcon(f, x, y + 3, iconH, ic); x += KillIconWide(f, iconH) + 3; } }
    x += 8;
    surface()->DrawText(FONT_UI, d.victim.c_str(), x, ty, TeamColor(d.vteam));
    y += rowH + 4;
  }
}

static void DrawChat(float sc) {
  int H = surface()->ScreenTall();
  float x = 20 * sc, y = H - 120 * sc - s_chat.size() * 18;
  for (auto& c : s_chat) {
    float age = gpGlobals->realtime - c.time;
    if (age > 12.f && !cl.chatOpen) { y += 18; continue; }
    unsigned char a = (unsigned char)(255 * Clamp(12.f - age, 0.f, 1.f)); if (cl.chatOpen) a = 255;
    float xx = x;
    if (c.dead) { surface()->DrawTextShadow(FONT_UI, "*DEAD* ", xx, y, { 230, 230, 230, a }); xx += surface()->GetTextWide(FONT_UI, "*DEAD* "); }
    if (c.teamOnly) { const char* t = c.team == TEAM_CT ? "(Counter-Terrorist) " : "(Terrorist) "; surface()->DrawTextShadow(FONT_UI, t, xx, y, { 230, 230, 230, a }); xx += surface()->GetTextWide(FONT_UI, t); }
    Color tc = TeamColor(c.team); tc.a = a;
    std::string name = c.who + " : ";
    surface()->DrawTextShadow(FONT_DEFAULT_BOLD, name.c_str(), xx, y, tc); xx += surface()->GetTextWide(FONT_DEFAULT_BOLD, name.c_str());
    surface()->DrawTextShadow(FONT_UI, c.text.c_str(), xx, y, { 255, 230, 160, a });
    y += 18;
  }
  if (cl.chatOpen) {
    std::string line = std::string(cl.chatTeam ? "say_team : " : "say : ") + cl.chatBuffer + ((int)(gpGlobals->realtime * 2) % 2 ? "_" : "");
    surface()->DrawFilledRect(x - 4, H - 118 * sc + 4, 500 * sc, 20, { 0, 0, 0, 120 });
    surface()->DrawTextShadow(FONT_UI, line.c_str(), x, H - 118 * sc + 6, { 255, 230, 160, 255 });
  }
}

static void DrawCenterTexts(CCSPlayer* lp, float sc) {
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  CCSGameRules* gr = CSGameRules();
  float since = ServerTime() - gr->m_flWinMessageTime;
  if (since >= 0 && since < 5.f && !gr->m_szWinMessage.empty()) {
    unsigned char a = (unsigned char)(255 * Clamp(5.f - since, 0.f, 1.f));
    HudTextCentered(FONT_GO_HUGE, gr->m_szWinMessage.c_str(), W * 0.5f, H * 0.26f, 26 * sc, { 255, 255, 255, a });
  }
  if (gpGlobals->realtime < s_centerEnd && !s_center.empty())
    HudTextCentered(FONT_GO_HUGE, s_center.c_str(), W * 0.5f, H * 0.33f, 24 * sc, { 255, 255, 255, 255 });
  if (gpGlobals->realtime < s_hintEnd && !s_hint.empty()) {
    // CS:GO hint: plain white lines under the top bar (no 1.6 orange box)
    std::vector<std::string> lines; size_t st = 0; while (true) { size_t e = s_hint.find('\n', st); lines.push_back(s_hint.substr(st, e == std::string::npos ? std::string::npos : e - st)); if (e == std::string::npos) break; st = e + 1; }
    float y = 60 * sc;
    for (size_t i = 0; i < lines.size(); i++)
      HudTextCentered(FONT_GO_BOLD, lines[i].c_str(), W * 0.5f, y + (float)i * 18 * sc, 13 * sc, { 240, 242, 245, 245 });
  }
  // freeze time countdown: right under the top score bar, like CS:GO
  if (gr->IsFreezePeriod() && lp->m_iTeamNum >= TEAM_TERRORIST && !gr->IsDeathmatch()) {
    HudTextCentered(FONT_GO_BOLD, TextFormat("Round starts in %d", (int)std::ceil(gr->GetFreezeTimeRemaining())), W * 0.5f, 50 * sc, 12 * sc, { 236, 238, 240, 225 });
  }
  // dead: spectator bars (CS:GO style - flat black plates, plain white text)
  if (!lp->IsAlive() && lp->m_iTeamNum != TEAM_UNASSIGNED) {
    float bh = 46 * sc;
    surface()->DrawFilledRect(0, 0, (float)W, bh, { 6, 7, 9, 215 });
    surface()->DrawFilledRect(0, H - bh, (float)W, bh, { 6, 7, 9, 215 });
    const char* mode = lp->m_iObserverMode == OBS_MODE_IN_EYE ? "FIRST PERSON" : lp->m_iObserverMode == OBS_MODE_CHASE ? "CHASE CAMERA" : lp->m_iObserverMode == OBS_MODE_ROAMING ? "FREE LOOK" : "DEATH CAM";
    HudText(FONT_GO_BOLD, mode, 22 * sc, H - bh + 15 * sc, 12 * sc, Alpha(hudgo::TextDim, 0.95f));
    if (cl.viewEntity && cl.viewEntity != lp) {
      CCSPlayer* t = cl.viewEntity;
      HudTextCentered(FONT_GO_HUGE, TextFormat("%s  (%d HP)", t->GetPlayerName(), t->m_iHealth), W * 0.5f, H - bh + 10 * sc, 17 * sc, { 240, 242, 245, 245 });
    }
    HudTextRight(FONT_GO_BOLD, "MOUSE1 / MOUSE2 - next / previous player      SPACE - change mode", W - 22 * sc, H - bh - 20 * sc, 11 * sc, Alpha(hudgo::TextFaint, 0.9f));
    HudTextRight(FONT_GO_BOLD, TextFormat("TERRORISTS %d      CT %d", gr->m_iNumTerroristWins, gr->m_iNumCTWins), W - 22 * sc, 16 * sc, 12 * sc, Alpha(hudgo::TextDim, 0.95f));
    if (gr->m_flRestartRoundTime <= 0 && lp->m_iTeamNum >= TEAM_TERRORIST && gr->IsDeathmatch())
      HudTextCentered(FONT_GO_BOLD, "RESPAWNING...", W * 0.5f, 18 * sc, 13 * sc, Alpha(hudgo::Text, 0.9f));
  }
}

static void DrawHitmarker(float sc) {
  float since = gpGlobals->realtime - cl.hitmarkerTime;
  extern ConVar cl_hitmarker;
  if (!cl_hitmarker.GetBool() || since > 0.35f) return;
  int W = surface()->ScreenWide(), H = surface()->ScreenTall(); float cx = W * 0.5f, cy = H * 0.5f;
  unsigned char a = (unsigned char)(255 * (1.f - since / 0.35f));
  Color c = cl.hitmarkerHead ? Color{ 255, 60, 60, a } : Color{ 255, 255, 255, a };
  float g = 4 * sc, l = 10 * sc;
  for (int sx = -1; sx <= 1; sx += 2) for (int sy = -1; sy <= 1; sy += 2) surface()->DrawLine(cx + sx * g, cy + sy * g, cx + sx * (g + l), cy + sy * (g + l), c, 2.f);
}
ConVar cl_hitmarker("cl_hitmarker", "1", FCVAR_ARCHIVE, "Show hit markers");

static void DrawDebugTexts() {
  extern ConVar cl_showpos, cl_showfps;
  int W = surface()->ScreenWide();
  float y = 4;
  if (cl_showfps.GetBool()) { surface()->DrawTextRight(FONT_MONO, TextFormat("%d fps", (int)cl.fps), W - 8, y, { 255, 255, 255, 255 }, true); y += 15; }
  if (cl_showpos.GetBool() && g_pLocalPlayer) {
    CCSPlayer* p = g_pLocalPlayer;
    surface()->DrawTextRight(FONT_MONO, TextFormat("pos: %.2f %.2f %.2f", p->m_vecOrigin.x, p->m_vecOrigin.y, p->m_vecOrigin.z), W - 8, y, WHITE, true); y += 15;
    surface()->DrawTextRight(FONT_MONO, TextFormat("ang: %.2f %.2f", cl.viewangles.x, cl.viewangles.y), W - 8, y, WHITE, true); y += 15;
    surface()->DrawTextRight(FONT_MONO, TextFormat("vel: %.2f", p->m_vecVelocity.Length2D()), W - 8, y, WHITE, true);
  }
}

static void DrawNotify() {
  // developer console notify lines (top-left)
  auto& n = ConsoleNotify();
  float y = 4; extern ConVar con_notifytime_ref;
  for (auto it = n.begin(); it != n.end();) { if (gpGlobals->realtime - it->time > 6.0) it = n.erase(it); else ++it; }
  size_t start = n.size() > 5 ? n.size() - 5 : 0;
  for (size_t i = start; i < n.size(); i++) { surface()->DrawTextShadow(FONT_MONO, n[i].text.c_str(), 200, y, { n[i].color.r, n[i].color.g, n[i].color.b, 255 }); y += 14; }
}
ConVar con_notifytime_ref("con_drawnotify", "1", 0, "Disables drawing of notification area (for taking screenshots).");

void HUD_Paint() {
  CCSPlayer* lp = g_pLocalPlayer;
  if (!lp || !cl_drawhud.GetBool()) return;
  float sc = HudScale();
  CCSPlayer* vp = cl.viewEntity ? cl.viewEntity : lp;
  if (vp->IsAlive()) DrawScope(vp);
  if (lp->IsAlive()) DrawFlash(lp);
  DrawCenterTexts(lp, sc);
  if (vp->IsAlive() && cl.firstPerson) DrawCrosshair(vp, sc);
  if (lp->IsAlive()) {
    DrawLowHpVignette(lp);
    DrawMoney(lp, sc);
    DrawVitals(lp, sc);
    DrawAmmo(lp, sc);
    DrawDamageIndicator(lp, sc);
    DrawProgressBar(lp, sc);
    DrawHitmarker(sc);
  } else if (vp != lp && vp->IsAlive()) {
    // spectator: show the watched player's health / armor / ammo
    DrawVitals(vp, sc);
    DrawAmmo(vp, sc);
  }
  if (!cl.showScores) DrawOverheads(lp, sc);
  if (lp->m_iTeamNum != TEAM_UNASSIGNED || lp->IsAlive()) DrawTimer(sc);
  DrawRadar(lp, sc);
  DrawDeathNotices(sc);
  DrawChat(sc);
  if (con_notifytime_ref.GetBool()) DrawNotify();
  DrawDebugTexts();
  // in-game menus
  if (cl.showScores) HUD_DrawScoreboard();
  if (cl.teamMenuOpen) HUD_DrawTeamMenu();
  else if (cl.buyMenuOpen) HUD_DrawBuyMenu();
}
