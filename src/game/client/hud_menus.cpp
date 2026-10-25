// hud_menus.cpp - CS 1.6 style VGUI in-game menus (CCSTeamMenu, CCSBuyMenu, CClientScoreBoardDialog)
#include "cl_main.hpp"
#include "hud_menus.hpp"
#include "../../engine/host.hpp"
#include "../../vgui/surface.hpp"
#include "../shared/cs_player.hpp"
#include "../shared/weapon_csbase.hpp"
#include "../shared/cs_gamerules.hpp"
#include "../shared/soundemitter.hpp"
#include "../shared/mapinfo.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>

using namespace vgui;
extern Color TeamColor(int team);
extern float HudScale();

static constexpr Color MENU_ORANGE = { 255, 176, 0, 255 };
static constexpr Color ORANGE_DIM = { 160, 110, 0, 255 };
static constexpr Color PANEL = { 0, 0, 0, 175 };

static int s_buyCategory = -1;      // -1 = main menu
static int s_hoverItem = -1;
static bool s_mouseWasDown = false;

bool HUD_MenusWantInput() { return cl.teamMenuOpen || cl.buyMenuOpen || cl.chatOpen; }

// ------------------------------------------------------------------------------------------------
// shared widgets
static bool MenuButton(float x, float y, float w, float h, const char* hotkey, const char* text, bool enabled, const char* right = nullptr) {
  Vector2 m = GetMousePosition();
  bool hover = enabled && m.x >= x && m.x < x + w && m.y >= y && m.y < y + h;
  bool clicked = hover && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
  if (hover) { surface()->DrawFilledRect(x, y, w, h, { 255, 176, 0, 40 }); }
  surface()->DrawOutlinedRect(x, y, w, h, hover ? MENU_ORANGE : Color{ 255, 176, 0, 90 });
  Color tc = enabled ? (hover ? Color{ 255, 230, 150, 255 } : MENU_ORANGE) : Color{ 110, 90, 50, 255 };
  float ty = y + (h - surface()->GetFontTall(FONT_DEFAULT_BOLD)) / 2;
  surface()->DrawText(FONT_DEFAULT_BOLD, hotkey, x + 8, ty, tc);
  surface()->DrawText(FONT_DEFAULT_BOLD, text, x + 30, ty, tc);
  if (right) surface()->DrawTextRight(FONT_DEFAULT_BOLD, right, x + w - 8, ty, tc);
  if (clicked) EmitSound2D("UI.Click");
  return clicked;
}
static void MenuFrame(float x, float y, float w, float h, const char* title) {
  surface()->DrawFilledRect(x, y, w, h, PANEL);
  surface()->DrawOutlinedRect(x, y, w, h, { 255, 176, 0, 140 });
  surface()->DrawText(FONT_MENU_TITLE, title, x + 18, y + 14, MENU_ORANGE);
  surface()->DrawFilledRect(x + 16, y + 44, w - 32, 1, { 255, 176, 0, 110 });
}

// ------------------------------------------------------------------------------------------------
// team menu
static void JoinTeam(int team) { Cbuf_AddText(TextFormat("jointeam %d\n", team)); cl.teamMenuOpen = false; }

void HUD_DrawTeamMenu() {
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  float w = 640, h = 400, x = (W - w) / 2, y = (H - h) / 2;
  MenuFrame(x, y, w, h, "SELECT TEAM");
  surface()->DrawText(FONT_UI, TextFormat("Map: %s  -  %s", g_MapInfo.name.c_str(), g_MapInfo.title.c_str()), x + 18, y + 54, { 220, 200, 150, 255 });
  float by = y + 84, bw = 250, bh = 32;
  int t = 0, ct = 0; for (CCSPlayer* p : UTIL_GetPlayers()) { if (p->m_iTeamNum == TEAM_TERRORIST) t++; else if (p->m_iTeamNum == TEAM_CT) ct++; }
  static int hover = 0;
  Vector2 m = GetMousePosition();
  struct Item { const char* key; const char* name; int team; std::string count; };
  Item items[4] = { { "1", "TERRORIST FORCES", TEAM_TERRORIST, std::to_string(t) }, { "2", "CT FORCES", TEAM_CT, std::to_string(ct) }, { "5", "AUTO-SELECT", 0, "" }, { "6", "SPECTATE", TEAM_SPECTATOR, "" } };
  for (int i = 0; i < 4; i++) {
    float yy = by + i * (bh + 10) + (i >= 2 ? 24 : 0);
    if (m.x >= x + 18 && m.x < x + 18 + bw && m.y >= yy && m.y < yy + bh) hover = i;
    if (MenuButton(x + 18, yy, bw, bh, items[i].key, items[i].name, true, items[i].count.c_str())) JoinTeam(items[i].team);
  }
  // description panel (1.6 shows the team picture + text)
  float px = x + 290, py = y + 84, pw = w - 308, ph = 290;
  surface()->DrawOutlinedRect(px, py, pw, ph, { 255, 176, 0, 90 });
  const char* desc[4] = {
    "TERRORIST FORCES\n\nPlant the bomb at bomb site A or B\nor eliminate the Counter-Terrorists.\n\nStarting weapons: Glock-18, knife.\nOne terrorist carries the C4.",
    "COUNTER-TERRORIST FORCES\n\nPrevent the bomb from being planted,\ndefuse it, or eliminate all Terrorists.\n\nStarting weapons: USP-S, knife.\nBuy a defuse kit to defuse in 5s.",
    "AUTO-SELECT\n\nJoin the team with fewer players.",
    "SPECTATE\n\nWatch the game in free look\nor follow players.",
  };
  std::string d = desc[hover]; float ty = py + 14; size_t st = 0; int ln = 0;
  while (st <= d.size()) { size_t e = d.find('\n', st); std::string l = d.substr(st, e == std::string::npos ? std::string::npos : e - st);
    surface()->DrawText(ln == 0 ? FONT_DEFAULT_BOLD : FONT_UI, l.c_str(), px + 14, ty, ln == 0 ? TeamColor(hover == 0 ? TEAM_TERRORIST : hover == 1 ? TEAM_CT : 0) : Color{ 230, 220, 190, 255 }); ty += 18; ln++;
    if (e == std::string::npos) break; st = e + 1; }
  // team silhouettes: little stripe color
  if (hover < 2) surface()->DrawFilledRect(px + 14, py + ph - 26, pw - 28, 10, hover == 0 ? Color{ 160, 40, 30, 200 } : Color{ 40, 80, 160, 200 });
  CCSPlayer* lp = g_pLocalPlayer;
  if (lp && lp->m_iTeamNum != TEAM_UNASSIGNED) surface()->DrawText(FONT_UI, "ESC - cancel", x + 18, y + h - 26, ORANGE_DIM);
}

// ------------------------------------------------------------------------------------------------
// buy menu
struct BuyItem { const char* alias; int weaponId; const char* label; int price; int team; };
static const char* s_categories[] = { "PISTOLS", "SHOTGUNS", "SUB-MACHINE GUNS", "RIFLES", "MACHINE GUNS", "PRIMARY AMMO", "SECONDARY AMMO", "EQUIPMENT" };
static std::vector<BuyItem> GetCategory(int cat) {
  std::vector<BuyItem> out;
  auto addType = [&](int type) {
    for (int id = 1; id < WEAPON_MAX; id++) { const CCSWeaponInfo* wi = GetWeaponInfo(id); if (!wi || wi->type != type || wi->price <= 0) continue; out.push_back({ WeaponIdToAlias(id), id, wi->printName, wi->price, wi->team }); }
  };
  switch (cat) {
    case 0: addType(WEAPONTYPE_PISTOL); break;
    case 1: addType(WEAPONTYPE_SHOTGUN); break;
    case 2: addType(WEAPONTYPE_SUBMACHINEGUN); break;
    case 3: addType(WEAPONTYPE_RIFLE); addType(WEAPONTYPE_SNIPER_RIFLE); break;
    case 4: addType(WEAPONTYPE_MACHINEGUN); break;
    case 7:
      out.push_back({ "vest", 0, "Kevlar Vest", 650, 0 }); out.push_back({ "vesthelm", 0, "Kevlar + Helmet", 1000, 0 });
      out.push_back({ "flashbang", WEAPON_FLASHBANG, "Flashbang", GetWeaponInfo(WEAPON_FLASHBANG)->price, 0 });
      out.push_back({ "hegrenade", WEAPON_HEGRENADE, "HE Grenade", GetWeaponInfo(WEAPON_HEGRENADE)->price, 0 });
      out.push_back({ "smokegrenade", WEAPON_SMOKEGRENADE, "Smoke Grenade", GetWeaponInfo(WEAPON_SMOKEGRENADE)->price, 0 });
      out.push_back({ "defuser", 0, "Defusal Kit", 400, TEAM_CT });
      break;
  }
  return out;
}

static void Buy(const char* alias) { Cbuf_AddText(std::string("buy ") + alias + "\n"); }

void HUD_DrawBuyMenu() {
  CCSPlayer* lp = g_pLocalPlayer; if (!lp) return;
  if (!lp->IsAlive() || !lp->m_bInBuyZone || CSGameRules()->IsBuyTimeElapsed()) { cl.buyMenuOpen = false; s_buyCategory = -1; return; }
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  float w = 700, h = 460, x = (W - w) / 2, y = (H - h) / 2;
  float bw = 300, bh = 30;
  if (s_buyCategory < 0) {
    MenuFrame(x, y, w, h, "BUY MENU");
    for (int i = 0; i < 8; i++) {
      float yy = y + 64 + i * (bh + 6);
      if (MenuButton(x + 18, yy, bw, bh, TextFormat("%d", i + 1), s_categories[i], true)) {
        if (i == 5) Buy("primammo"); else if (i == 6) Buy("secammo"); else s_buyCategory = i;
      }
    }
    if (MenuButton(x + 18, y + 64 + 8 * (bh + 6) + 10, bw, bh, "0", "CANCEL", true)) cl.buyMenuOpen = false;
    float px = x + 340, py = y + 64;
    surface()->DrawText(FONT_DEFAULT_BOLD, TextFormat("$%d", lp->m_iAccount), px, py, MENU_ORANGE);
    surface()->DrawText(FONT_UI, "Select a category with the number keys\nor the mouse. F1 = autobuy, F2 = rebuy.", px, py + 24, { 220, 200, 150, 255 });
    float bt = CSGameRules()->m_fRoundStartTime + FindConVar("mp_buytime")->GetFloat() - ServerTime();
    if (!CSGameRules()->IsFreezePeriod() && !CSGameRules()->IsDeathmatch()) surface()->DrawText(FONT_UI, TextFormat("Buy time left: %ds", (int)std::max(0.f, bt)), px, py + 70, { 220, 200, 150, 255 });
    return;
  }
  auto items = GetCategory(s_buyCategory);
  MenuFrame(x, y, w, h, s_categories[s_buyCategory]);
  int hover = -1; Vector2 m = GetMousePosition();
  for (size_t i = 0; i < items.size(); i++) {
    const BuyItem& it = items[i];
    float yy = y + 64 + i * (bh + 6);
    bool teamOk = it.team == 0 || it.team == lp->m_iTeamNum;
    bool afford = lp->m_iAccount >= it.price;
    if (m.x >= x + 18 && m.x < x + 18 + bw && m.y >= yy && m.y < yy + bh) hover = (int)i;
    std::string label = it.label; if (!teamOk) label += it.team == TEAM_CT ? " (CT)" : " (T)";
    if (MenuButton(x + 18, yy, bw, bh, TextFormat("%d", (int)i + 1), label.c_str(), teamOk && afford, TextFormat("$%d", it.price))) { Buy(it.alias); cl.buyMenuOpen = false; s_buyCategory = -1; }
  }
  if (MenuButton(x + 18, y + h - 46, bw, bh, "0", "BACK", true)) s_buyCategory = -1;
  // info panel
  if (hover < 0) hover = s_hoverItem; else s_hoverItem = hover;
  if (hover >= 0 && hover < (int)items.size()) {
    const BuyItem& it = items[hover];
    float px = x + 340, py = y + 64, pw = w - 358;
    surface()->DrawOutlinedRect(px, py, pw, 330, { 255, 176, 0, 90 });
    surface()->DrawText(FONT_MENU_TITLE, it.label, px + 12, py + 10, MENU_ORANGE);
    surface()->DrawText(FONT_DEFAULT_BOLD, TextFormat("PRICE: $%d", it.price), px + 12, py + 42, { 230, 220, 190, 255 });
    if (const CCSWeaponInfo* wi = it.weaponId ? GetWeaponInfo(it.weaponId) : nullptr) {
      float ly = py + 70;
      auto stat = [&](const char* name, const char* val, float frac) {
        surface()->DrawText(FONT_UI, name, px + 12, ly, { 220, 200, 150, 255 }); surface()->DrawTextRight(FONT_UI, val, px + pw - 12, ly, { 230, 220, 190, 255 });
        surface()->DrawFilledRect(px + 12, ly + 16, pw - 24, 4, { 255, 176, 0, 40 }); surface()->DrawFilledRect(px + 12, ly + 16, (pw - 24) * Clamp(frac, 0.f, 1.f), 4, { 255, 176, 0, 200 });
        ly += 30;
      };
      if (wi->type != WEAPONTYPE_GRENADE) {
        stat("Damage", TextFormat("%d", wi->damage * std::max(1, wi->bullets)), wi->damage * std::max(1, wi->bullets) / 120.f);
        stat("Fire rate", TextFormat("%d RPM", (int)(60.f / std::max(0.01f, wi->cycleTime))), (60.f / wi->cycleTime) / 1000.f);
        stat("Armor penetration", TextFormat("%d%%", (int)(wi->armorRatio * 200)), wi->armorRatio * 2.f);
        stat("Magazine", TextFormat("%d / %d", wi->clipSize, wi->reserveAmmo), wi->clipSize / 100.f);
        stat("Mobility", TextFormat("%d", (int)wi->maxSpeed), wi->maxSpeed / 260.f);
        stat("Kill award", TextFormat("$%d", wi->killAward), wi->killAward / 1500.f);
      }
      surface()->DrawText(FONT_UI, wi->team == TEAM_TERRORIST ? "Terrorists only" : wi->team == TEAM_CT ? "Counter-Terrorists only" : "Both teams", px + 12, py + 300, ORANGE_DIM);
    }
  }
  surface()->DrawTextRight(FONT_DEFAULT_BOLD, TextFormat("$%d", lp->m_iAccount), x + w - 18, y + 16, MENU_ORANGE);
}

// ------------------------------------------------------------------------------------------------
// scoreboard
void HUD_DrawScoreboard() {
  int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  float w = std::min(900.f, W - 40.f), h = std::min(620.f, H - 80.f), x = (W - w) / 2, y = (H - h) / 2;
  surface()->DrawFilledRect(x, y, w, h, { 0, 0, 0, 185 });
  surface()->DrawOutlinedRect(x, y, w, h, { 255, 176, 0, 110 });
  CCSGameRules* gr = CSGameRules();
  surface()->DrawText(FONT_MENU_TITLE, "Counter-Strike: HvH", x + 16, y + 10, MENU_ORANGE);
  surface()->DrawTextRight(FONT_UI, TextFormat("%s   |   Round %d / %d", g_MapInfo.name.c_str(), gr->m_iTotalRoundsPlayed + 1, FindConVar("mp_maxrounds")->GetInt()), x + w - 16, y + 16, { 220, 200, 150, 255 });
  float colScore = x + w - 250, colDeaths = x + w - 180, colAssist = x + w - 120, colPing = x + w - 60;
  float ty = y + 48;
  CCSPlayer* lp = g_pLocalPlayer;
  for (int team : { TEAM_TERRORIST, TEAM_CT }) {
    std::vector<CCSPlayer*> ps;
    for (CCSPlayer* p : UTIL_GetPlayers()) if (p->m_iTeamNum == team) ps.push_back(p);
    std::sort(ps.begin(), ps.end(), [](CCSPlayer* a, CCSPlayer* b) { return a->m_iKills != b->m_iKills ? a->m_iKills > b->m_iKills : a->m_iDeaths < b->m_iDeaths; });
    Color tc = TeamColor(team);
    int alive = 0; for (auto* p : ps) alive += p->IsAlive();
    surface()->DrawText(FONT_DEFAULT_BOLD, TextFormat("%s   -   %d players (%d alive)", team == TEAM_TERRORIST ? "TERRORISTS" : "COUNTER-TERRORISTS", (int)ps.size(), alive), x + 16, ty, tc);
    surface()->DrawTextRight(FONT_MENU_TITLE, TextFormat("%d", team == TEAM_TERRORIST ? gr->m_iNumTerroristWins : gr->m_iNumCTWins), colScore - 40, ty - 4, tc);
    surface()->DrawTextRight(FONT_UI, "Score", colScore, ty, tc); surface()->DrawTextRight(FONT_UI, "Deaths", colDeaths, ty, tc);
    surface()->DrawTextRight(FONT_UI, "Assists", colAssist, ty, tc); surface()->DrawTextRight(FONT_UI, "Latency", colPing, ty, tc);
    ty += 20; surface()->DrawFilledRect(x + 12, ty, w - 24, 1, tc); ty += 4;
    for (CCSPlayer* p : ps) {
      if (p == lp) surface()->DrawFilledRect(x + 12, ty - 1, w - 24, 18, { 255, 255, 255, 30 });
      Color c = tc; if (!p->IsAlive()) c.a = 120;
      surface()->DrawText(FONT_UI, p->GetPlayerName(), x + 20, ty, c);
      float sx = x + 20 + surface()->GetTextWide(FONT_UI, p->GetPlayerName()) + 12;
      if (!p->IsAlive()) { surface()->DrawText(FONT_UI, "Dead", sx, ty, { 200, 200, 200, 160 }); sx += 40; }
      if (p->HasC4() && lp && lp->m_iTeamNum == TEAM_TERRORIST) { surface()->DrawText(FONT_UI, "Bomb", sx, ty, { 255, 120, 120, 220 }); sx += 40; }
      if (p->m_bHasDefuser && lp && lp->m_iTeamNum == TEAM_CT) { surface()->DrawText(FONT_UI, "Defuser", sx, ty, { 120, 200, 255, 220 }); sx += 50; }
      if (p->m_iMVPs > 0) surface()->DrawText(FONT_UI, TextFormat("*%d", p->m_iMVPs), sx, ty, { 255, 220, 80, 220 });
      surface()->DrawTextRight(FONT_UI, TextFormat("%d", p->m_iKills), colScore, ty, c);
      surface()->DrawTextRight(FONT_UI, TextFormat("%d", p->m_iDeaths), colDeaths, ty, c);
      surface()->DrawTextRight(FONT_UI, TextFormat("%d", p->m_iAssists), colAssist, ty, c);
      surface()->DrawTextRight(FONT_UI, p->IsBot() ? "BOT" : TextFormat("%d", p->m_iPing), colPing, ty, c);
      ty += 18;
    }
    ty += 18;
  }
  // spectators
  std::string specs; for (CCSPlayer* p : UTIL_GetPlayers()) if (p->m_iTeamNum <= TEAM_SPECTATOR) { if (!specs.empty()) specs += ", "; specs += p->GetPlayerName(); }
  if (!specs.empty()) surface()->DrawText(FONT_UI, ("Spectators: " + specs).c_str(), x + 16, y + h - 24, { 200, 200, 200, 200 });
}

// ------------------------------------------------------------------------------------------------
bool HUD_MenuKeyPressed(int key) {
  if (cl.chatOpen) {
    if (key == KEY_ESCAPE) { cl.chatOpen = false; return true; }
    if (key == KEY_ENTER || key == KEY_KP_ENTER) {
      if (!cl.chatBuffer.empty()) Cbuf_AddText(std::string(cl.chatTeam ? "say_team " : "say ") + cl.chatBuffer + "\n");
      cl.chatOpen = false; return true;
    }
    if (key == KEY_BACKSPACE) { while (!cl.chatBuffer.empty() && (cl.chatBuffer.back() & 0xC0) == 0x80) cl.chatBuffer.pop_back(); if (!cl.chatBuffer.empty()) cl.chatBuffer.pop_back(); return true; }
    return true;
  }
  if (cl.teamMenuOpen) {
    if (key == KEY_ONE || key == KEY_KP_1) { JoinTeam(TEAM_TERRORIST); return true; }
    if (key == KEY_TWO || key == KEY_KP_2) { JoinTeam(TEAM_CT); return true; }
    if (key == KEY_FIVE || key == KEY_KP_5) { JoinTeam(0); return true; }
    if (key == KEY_SIX || key == KEY_KP_6) { JoinTeam(TEAM_SPECTATOR); return true; }
    if (key == KEY_ESCAPE || key == KEY_M) { if (g_pLocalPlayer && g_pLocalPlayer->m_iTeamNum != TEAM_UNASSIGNED) cl.teamMenuOpen = false; return true; }
    return true;
  }
  if (cl.buyMenuOpen) {
    int n = -1;
    if (key >= KEY_ZERO && key <= KEY_NINE) n = key - KEY_ZERO; else if (key >= KEY_KP_0 && key <= KEY_KP_9) n = key - KEY_KP_0;
    if (key == KEY_ESCAPE || key == KEY_B) { if (s_buyCategory >= 0 && key == KEY_ESCAPE) s_buyCategory = -1; else { cl.buyMenuOpen = false; s_buyCategory = -1; } return true; }
    if (n == 0) { if (s_buyCategory >= 0) s_buyCategory = -1; else cl.buyMenuOpen = false; return true; }
    if (n > 0) {
      if (s_buyCategory < 0) { if (n == 6) Buy("primammo"); else if (n == 7) Buy("secammo"); else if (n <= 8) s_buyCategory = n - 1; }
      else { auto items = GetCategory(s_buyCategory); if (n <= (int)items.size()) { Buy(items[n - 1].alias); cl.buyMenuOpen = false; s_buyCategory = -1; } }
      EmitSound2D("UI.Click");
      return true;
    }
    if (key == KEY_F1 || key == KEY_F2) return false;
    return true;
  }
  return false;
}

void HUD_ChatChar(int cp) {
  if (!cl.chatOpen || cp < 32 || cl.chatBuffer.size() > 120) return;
  int n = 0; const char* u = CodepointToUTF8(cp, &n); cl.chatBuffer.append(u, n);
}
