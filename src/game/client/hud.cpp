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
// icons (drawn procedurally like the 1.6 sprites)
static void IconCross(float x, float y, float s, Color c) { surface()->DrawFilledRect(x + s * 0.35f, y, s * 0.3f, s, c); surface()->DrawFilledRect(x, y + s * 0.35f, s, s * 0.3f, c); }
static void IconShield(float x, float y, float s, Color c, bool helmet) {
  Vector2 a{ x, y }, b{ x + s, y }, d{ x + s, y + s * 0.55f }, e{ x + s * 0.5f, y + s }, f{ x, y + s * 0.55f };
  surface()->DrawTriangle(a, b, d, c); surface()->DrawTriangle(a, d, f, c); surface()->DrawTriangle(f, d, e, c);
  if (helmet) { Color k = { 0, 0, 0, 160 }; surface()->DrawFilledRect(x + s * 0.3f, y + s * 0.15f, s * 0.4f, s * 0.12f, k); }
}
static void IconClock(float x, float y, float s, Color c) {
  float r = s * 0.5f; Vector2 ctr{ x + r, y + r };
  DrawRing(ctr, r * 0.72f, r, 0, 360, 24, c);
  surface()->DrawLine(ctr.x, ctr.y, ctr.x, ctr.y - r * 0.6f, c, 2.f * s / 24); surface()->DrawLine(ctr.x, ctr.y, ctr.x + r * 0.45f, ctr.y, c, 2.f * s / 24);
}
static void IconBullet(float x, float y, float s, Color c) {
  surface()->DrawFilledRect(x + s * 0.3f, y + s * 0.3f, s * 0.4f, s * 0.7f, c);
  surface()->DrawTriangle({ x + s * 0.3f, y + s * 0.3f }, { x + s * 0.7f, y + s * 0.3f }, { x + s * 0.5f, y }, c);
}
static void IconC4(float x, float y, float s, Color c) {
  surface()->DrawFilledRect(x, y + s * 0.2f, s * 1.3f, s * 0.6f, c);
  surface()->DrawFilledRect(x + s * 0.2f, y + s * 0.05f, s * 0.12f, s * 0.2f, c); surface()->DrawFilledRect(x + s * 0.5f, y + s * 0.05f, s * 0.12f, s * 0.2f, c);
}
static void IconCart(float x, float y, float s, Color c) {
  surface()->DrawText(FONT_HUD_NUMBERS, "$", x, y - s * 0.25f, c);
}
static void IconDefuser(float x, float y, float s, Color c) { surface()->DrawFilledRect(x, y + s * 0.3f, s * 0.9f, s * 0.4f, c); surface()->DrawFilledRect(x + s * 0.6f, y, s * 0.15f, s * 0.35f, c); }

static void HudNumber(int font, const char* t, float x, float y, Color c) {
  BeginBlendMode(BLEND_ADDITIVE); surface()->DrawText(font, t, x, y, c); EndBlendMode();
}

// ------------------------------------------------------------------------------------------------
static void DrawHealthArmor(CCSPlayer* p, float sc) {
  // CS:GO style: HP/Armor at bottom RIGHT
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  float y = H - 52 * sc, isz = 26 * sc;
  int hp = std::max(0, p->m_iHealth);
  
  // Calculate width
  char hpStr[32], armorStr[32];
  snprintf(hpStr, sizeof(hpStr), "%d", hp);
  snprintf(armorStr, sizeof(armorStr), "%d", p->m_ArmorValue);
  float hpW = surface()->GetTextSize(FONT_HUD_NUMBERS, hpStr).x;
  float armorW = surface()->GetTextSize(FONT_HUD_NUMBERS, armorStr).x;
  float totalW = isz + 10 * sc + hpW + 30 * sc + isz + 10 * sc + armorW;
  float startX = W - 20 * sc - totalW;
  
  // Background
  surface()->DrawFilledRect(startX - 8 * sc, y - 4 * sc, totalW + 16 * sc, isz + 16 * sc, { 0, 0, 0, 160 });
  
  // HP icon + number
  Color c = HUD; if (hp <= 25) { c = scheme::HudRed; if (std::fmod(gpGlobals->realtime, 0.6f) < 0.3f) c.a = 140; }
  c.a = (unsigned char)(c.a * 0.86f);
  BeginBlendMode(BLEND_ADDITIVE); IconCross(startX, y + 6 * sc, isz, c); EndBlendMode();
  HudNumber(FONT_HUD_NUMBERS, hpStr, startX + isz + 10 * sc, y, c);
  
  // Armor icon + number
  float ax = startX + isz + 10 * sc + hpW + 30 * sc;
  Color ac = HUD; ac.a = 220;
  BeginBlendMode(BLEND_ADDITIVE); IconShield(ax, y + 5 * sc, isz, ac, p->m_bHasHelmet); EndBlendMode();
  HudNumber(FONT_HUD_NUMBERS, armorStr, ax + isz + 10 * sc, y, ac);
}

static void DrawTimer(float sc) {
  CCSGameRules* gr = CSGameRules();
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  
  // CS:GO style: score at top center (CT | TIMER | T)
  int ctScore = 0, tScore = 0;
  for (CCSPlayer* p : UTIL_GetPlayers()) {
    if (!p) continue;
    if (p->m_iTeamNum == TEAM_CT) ctScore += p->m_iKills;
    else if (p->m_iTeamNum == TEAM_TERRORIST) tScore += p->m_iKills;
  }
  
  float t;
  if (gr->m_bBombPlanted) {
    // Bomb planted: flashing C4 icon at top
    if (std::fmod(gpGlobals->realtime, 1.f) < 0.6f) { BeginBlendMode(BLEND_ADDITIVE); IconC4(W * 0.5f - 18 * sc, 10 * sc, 28 * sc, scheme::HudRed); EndBlendMode(); }
    return;
  }
  t = gr->IsFreezePeriod() ? gr->GetFreezeTimeRemaining() : gr->GetRoundRemainingTime();
  if (gr->IsDeathmatch()) t = gr->GetRoundRemainingTime();
  t = std::max(0.f, t);
  int m = (int)t / 60, s = (int)t % 60;
  Color c = HUD; c.a = 220; if (t < 10 && !gr->IsFreezePeriod()) c = scheme::HudRed;
  const char* timerTxt = TextFormat("%d:%02d", m, s);
  float timerW = surface()->GetTextSize(FONT_HUD_NUMBERS, timerTxt).x;
  
  // Draw score at top: CT [score] | TIMER | [score] T
  float topY = 10 * sc;
  const char* ctTxt = TextFormat("CT %d", ctScore);
  const char* tTxt = TextFormat("%d T", tScore);
  float ctW = surface()->GetTextSize(FONT_HUD_NUMBERS, ctTxt).x;
  float tW = surface()->GetTextSize(FONT_HUD_NUMBERS, tTxt).x;
  float totalW = ctW + 20 * sc + timerW + 34 * sc + 20 * sc + tW;
  float startX = W * 0.5f - totalW * 0.5f;
  
  // Background panel
  surface()->DrawFilledRect(startX - 10 * sc, topY, totalW + 20 * sc, 40 * sc, { 0, 0, 0, 180 });
  
  // CT score
  Color ctColor = {100, 150, 255, 255};
  HudNumber(FONT_HUD_NUMBERS, ctTxt, startX, topY + 6 * sc, ctColor);
  
  // Timer with clock icon
  float timerX = startX + ctW + 20 * sc;
  BeginBlendMode(BLEND_ADDITIVE); IconClock(timerX, topY + 10 * sc, 20 * sc, c); EndBlendMode();
  HudNumber(FONT_HUD_NUMBERS, timerTxt, timerX + 28 * sc, topY + 6 * sc, c);
  
  // T score
  Color tColor = {255, 200, 80, 255};
  HudNumber(FONT_HUD_NUMBERS, tTxt, timerX + timerW + 34 * sc + 20 * sc, topY + 6 * sc, tColor);
}

static void DrawAmmoMoney(CCSPlayer* p, float sc) {
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  C_WeaponCSBase* w = p->GetActiveCSWeapon();
  Color c = HUD; c.a = 220;
  float y = H - 52 * sc;
  if (w && (w->IsGun() || w->GetWeaponType() == WEAPONTYPE_GRENADE)) {
    std::string txt;
    if (w->GetWeaponType() == WEAPONTYPE_GRENADE) txt = TextFormat("%d", std::max(1, w->m_iClip1));
    else txt = TextFormat("%d | %d", std::max(0, w->m_iClip1), w->m_iPrimaryReserveAmmoCount);
    float tw = surface()->GetTextSize(FONT_HUD_NUMBERS, txt.c_str()).x;
    float x = W - 30 * sc - tw - 30 * sc;
    Color cc = c; if (w->IsGun() && w->m_iClip1 == 0) cc = scheme::HudRed;
    HudNumber(FONT_HUD_NUMBERS, txt.c_str(), x, y, cc);
    BeginBlendMode(BLEND_ADDITIVE); IconBullet(W - 40 * sc, y + 6 * sc, 26 * sc, cc); EndBlendMode();
    // silhouette of the weapon left of the counter
    if (const Texture2D* ic = VGUI_WeaponIcon(w->m_iWeaponID)) {
      float ih = 26 * sc, iw = std::min(ih * ic->width / (float)std::max(1, ic->height), 110 * sc);
      Color tc = c; tc.a = 170;
      DrawTexturePro(*ic, { 0, 0, (float)ic->width, (float)ic->height }, { x - iw - 16 * sc, y + 8 * sc, iw, ih }, { 0, 0 }, 0, tc);
    }
  }
  // money
  if (s_lastMoney >= 0 && p->m_iAccount != s_lastMoney) { s_moneyDelta = p->m_iAccount - s_lastMoney; s_moneyDeltaTime = gpGlobals->realtime; }
  s_lastMoney = p->m_iAccount;
  const char* mt = TextFormat("%d", p->m_iAccount);
  float mw = surface()->GetTextSize(FONT_HUD_NUMBERS, mt).x;
  float my = H - 104 * sc; float mx = W - 30 * sc - mw;
  Color mc = c; if (gpGlobals->realtime - s_moneyDeltaTime < 0.5f && s_moneyDelta < 0) mc = scheme::HudRed;
  HudNumber(FONT_HUD_NUMBERS, mt, mx, my, mc);
  BeginBlendMode(BLEND_ADDITIVE); surface()->DrawText(FONT_HUD_NUMBERS, "$", mx - 32 * sc, my, mc); EndBlendMode();
  float since = gpGlobals->realtime - s_moneyDeltaTime;
  if (since < 3.f && s_moneyDelta != 0) {
    Color dc = s_moneyDelta > 0 ? Color{ 80, 230, 80, 255 } : scheme::HudRed; dc.a = (unsigned char)(255 * Clamp(3.f - since, 0.f, 1.f));
    const char* dt = TextFormat("%s%d", s_moneyDelta > 0 ? "+" : "-", std::abs(s_moneyDelta));
    surface()->DrawTextRight(FONT_HUD_SMALL, dt, W - 30 * sc, my - 22 * sc, dc, true);
  }
  // status icons (left side, 1.6: buy zone, c4, defuser)
  float ix = 20 * sc, iy = H * 0.5f;
  if (p->m_bInBuyZone && !CSGameRules()->IsBuyTimeElapsed()) { BeginBlendMode(BLEND_ADDITIVE); surface()->DrawText(FONT_HUD_NUMBERS, "$", ix, iy, { 60, 220, 60, 220 }); EndBlendMode(); iy += 46 * sc; }
  if (p->HasC4()) { Color cc = p->m_bInBombZone ? (std::fmod(gpGlobals->realtime, 0.5f) < 0.25f ? Color{ 60, 255, 60, 255 } : Color{ 30, 120, 30, 255 }) : Color{ 60, 200, 60, 200 }; BeginBlendMode(BLEND_ADDITIVE); IconC4(ix, iy, 28 * sc, cc); EndBlendMode(); iy += 40 * sc; }
  if (p->m_bHasDefuser) { BeginBlendMode(BLEND_ADDITIVE); IconDefuser(ix, iy, 28 * sc, { 60, 200, 60, 200 }); EndBlendMode(); }
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
  float w = 300 * sc, h = 12 * sc, x = W * 0.5f - w / 2, y = H * 0.66f;
  surface()->DrawFilledRect(x - 2, y - 2, w + 4, h + 4, { 0, 0, 0, 160 });
  surface()->DrawOutlinedRect(x - 2, y - 2, w + 4, h + 4, HUD);
  surface()->DrawFilledRect(x, y, w * frac, h, { 255, 176, 0, 210 });
  surface()->DrawTextCentered(FONT_HUD_SMALL, label, W * 0.5f, y - 22 * sc, HUD, true);
}

// CS:GO-style round radar: the map is a triangle fan whose texture coordinates come from the inverse of the
// world -> radar transform (both affine), so the circle is exact without a stencil
static void DrawRadar(CCSPlayer* lp, float sc) {
  if (!cl_radar.GetBool() || !s_radarTex.id) return;
  const CMapInfo& m = g_MapInfo;
  const float size = 190 * sc, R = size * 0.5f, cx = 14 * sc + R, cy = 14 * sc + R;
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
    surface()->DrawTextCentered(FONT_MENU_TITLE, gr->m_szWinMessage.c_str(), W * 0.5f, H * 0.28f, { 255, 255, 255, a }, true);
  }
  if (gpGlobals->realtime < s_centerEnd && !s_center.empty())
    surface()->DrawTextCentered(FONT_MENU_TITLE, s_center.c_str(), W * 0.5f, H * 0.34f, { 255, 255, 255, 255 }, true);
  if (gpGlobals->realtime < s_hintEnd && !s_hint.empty()) {
    // 1.6 style hint box (orange text in a translucent box)
    std::vector<std::string> lines; size_t st = 0; while (true) { size_t e = s_hint.find('\n', st); lines.push_back(s_hint.substr(st, e == std::string::npos ? std::string::npos : e - st)); if (e == std::string::npos) break; st = e + 1; }
    float w = 0; for (auto& l : lines) w = std::max(w, surface()->GetTextSize(FONT_UI, l.c_str()).x);
    float x = W * 0.5f - w / 2 - 10, y = H * 0.12f;
    surface()->DrawFilledRect(x, y, w + 20, lines.size() * 18 + 12, { 0, 0, 0, 140 });
    surface()->DrawOutlinedRect(x, y, w + 20, lines.size() * 18 + 12, { 255, 176, 0, 160 });
    for (size_t i = 0; i < lines.size(); i++) surface()->DrawTextCentered(FONT_UI, lines[i].c_str(), W * 0.5f, y + 6 + i * 18, HUD);
  }
  // freeze time / warmup
  if (gr->IsFreezePeriod() && lp->m_iTeamNum >= TEAM_TERRORIST && !gr->IsDeathmatch()) {
    surface()->DrawTextCentered(FONT_HUD_SMALL, TextFormat("Round starts in %d", (int)std::ceil(gr->GetFreezeTimeRemaining())), W * 0.5f, H * 0.2f, { 255, 255, 255, 220 }, true);
  }
  // dead: spectator info (1.6 black bars)
  if (!lp->IsAlive() && lp->m_iTeamNum != TEAM_UNASSIGNED) {
    float bh = 56 * sc;
    surface()->DrawFilledRect(0, 0, (float)W, bh, { 0, 0, 0, 200 });
    surface()->DrawFilledRect(0, H - bh, (float)W, bh, { 0, 0, 0, 200 });
    const char* mode = lp->m_iObserverMode == OBS_MODE_IN_EYE ? "First Person" : lp->m_iObserverMode == OBS_MODE_CHASE ? "Chase Camera" : lp->m_iObserverMode == OBS_MODE_ROAMING ? "Free Look" : "Death Cam";
    surface()->DrawText(FONT_HUD_SMALL, mode, 20 * sc, H - bh + 8 * sc, HUD);
    if (cl.viewEntity && cl.viewEntity != lp) {
      CCSPlayer* t = cl.viewEntity;
      surface()->DrawTextCentered(FONT_MENU_TITLE, TextFormat("%s  (%d hp)", t->GetPlayerName(), t->m_iHealth), W * 0.5f, H - bh + 10 * sc, TeamColor(t->m_iTeamNum), true);
    }
    surface()->DrawTextRight(FONT_UI, "MOUSE1/MOUSE2 - next/prev player   SPACE - change mode", W - 20 * sc, H - 22 * sc, { 200, 200, 200, 200 });
    surface()->DrawTextRight(FONT_HUD_SMALL, TextFormat("Terrorists : %d    CT : %d", gr->m_iNumTerroristWins, gr->m_iNumCTWins), W - 20 * sc, 10 * sc, HUD);
    if (gr->m_flRestartRoundTime <= 0 && lp->m_iTeamNum >= TEAM_TERRORIST && gr->IsDeathmatch()) surface()->DrawTextCentered(FONT_HUD_SMALL, "Respawning...", W * 0.5f, 18 * sc, HUD);
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
    DrawHealthArmor(lp, sc);
    DrawAmmoMoney(lp, sc);
    DrawDamageIndicator(lp, sc);
    DrawProgressBar(lp, sc);
    DrawHitmarker(sc);
  } else if (vp != lp && vp->IsAlive()) {
    DrawHealthArmor(vp, sc);
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
