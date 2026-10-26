// hud_menus.cpp - in-game menus: team menu, CS:GO radial buy menu, scoreboard, chat
#include "cl_main.hpp"
#include "hud_menus.hpp"
#include "../../engine/host.hpp"
#include "../../vgui/surface.hpp"
#include "../shared/cs_player.hpp"
#include "../shared/weapon_csbase.hpp"
#include "../shared/cs_gamerules.hpp"
#include "../shared/soundemitter.hpp"
#include "../shared/mapinfo.hpp"
#include "../shared/cs_loadout.hpp"
#include "../../vgui/csgo_style.hpp"
#include "../../vgui/vgui_images.hpp"
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

bool HUD_MenusWantInput() { return cl.teamMenuOpen || cl.buyMenuOpen || cl.chatOpen; }

// ------------------------------------------------------------------------------------------------
// team menu
static void JoinTeam(int team) { Cbuf_AddText(TextFormat("jointeam %d\n", team)); cl.teamMenuOpen = false; }

void HUD_DrawTeamMenu() {
  const float W = (float)surface()->ScreenWide(), H = (float)surface()->ScreenTall();
  DrawRectangle(0, 0, (int)W, (int)H, { 0, 0, 0, 150 });
  int t = 0, ct = 0; for (CCSPlayer* p : UTIL_GetPlayers()) { if (p->m_iTeamNum == TEAM_TERRORIST) t++; else if (p->m_iTeamNum == TEAM_CT) ct++; }
  surface()->DrawTextCentered(FONT_GO_TITLE, "CHOOSE YOUR SIDE", W * 0.5f, H * 0.08f, csgo::Text, true);
  surface()->DrawTextCentered(FONT_GO, TextFormat("%s", g_MapInfo.name.c_str()), W * 0.5f, H * 0.08f + 30, csgo::TextDim, true);
  const float cw = std::min(330.f, W * 0.36f), chh = std::min(H * 0.56f, 400.f), gap = 26;
  const float y = H * 0.08f + 64;
  Vector2 m = GetMousePosition();
  struct Card { int team; const char* title; const char* sub; const char* img; Color col; int count; const char* key; };
  Card cards[2] = { { TEAM_TERRORIST, "TERRORISTS", "Plant the bomb or eliminate the CTs", "terror", csgo::Gold, t, "1" },
                    { TEAM_CT, "COUNTER-TERRORISTS", "Defend the sites, defuse the bomb", "urban", csgo::Accent, ct, "2" } };
  for (int i = 0; i < 2; i++) {
    const Card& c = cards[i];
    float x = W * 0.5f + (i == 0 ? -cw - gap * 0.5f : gap * 0.5f);
    bool hv = m.x >= x && m.x < x + cw && m.y >= y && m.y < y + chh;
    DrawRectangleRec({ x, y, cw, chh }, hv ? Color{ 44, 50, 60, 240 } : Color{ 22, 25, 31, 235 });
    DrawRectangleRec({ x, y, cw, 4 }, c.col);
    float ih = chh - 110;
    if (const Texture2D* img = VGUI_ItemImage(c.img)) VGUI_DrawImageFit(*img, x + 14, y + 18, cw - 28, ih - 10, hv ? WHITE : Color{ 200, 200, 200, 255 });
    else DrawRectangleGradientV((int)x + 14, (int)y + 18, (int)cw - 28, (int)ih - 10, csgo::Alpha(c.col, 0.35f), { 0, 0, 0, 0 });
    surface()->DrawTextCentered(FONT_GO_TITLE, c.title, x + cw * 0.5f, y + ih + 14, c.col, true);
    surface()->DrawTextCentered(FONT_GO, c.sub, x + cw * 0.5f, y + ih + 46, csgo::TextDim, true);
    surface()->DrawTextCentered(FONT_GO_BOLD, TextFormat("[%s]   %d PLAYERS", c.key, c.count), x + cw * 0.5f, y + ih + 72, csgo::Text, true);
    if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { EmitSound2D("UI.Click"); JoinTeam(c.team); }
  }
  // auto / spectate
  float by = y + chh + 18, bw = 200, bh = 40;
  const char* labels[2] = { "[5] AUTO-SELECT", "[6] SPECTATE" }; int teams[2] = { 0, TEAM_SPECTATOR };
  for (int i = 0; i < 2; i++) {
    float x = W * 0.5f + (i == 0 ? -bw - 6 : 6);
    bool hv = m.x >= x && m.x < x + bw && m.y >= by && m.y < by + bh;
    DrawRectangleRec({ x, by, bw, bh }, hv ? csgo::PanelHover : csgo::PanelLight);
    surface()->DrawTextCentered(FONT_GO_BOLD, labels[i], x + bw * 0.5f, by + (bh - surface()->GetFontTall(FONT_GO_BOLD)) * 0.5f, csgo::Text);
    if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) { EmitSound2D("UI.Click"); JoinTeam(teams[i]); }
  }
  CCSPlayer* lp = g_pLocalPlayer;
  if (lp && lp->m_iTeamNum != TEAM_UNASSIGNED) surface()->DrawTextCentered(FONT_GO, "ESC - cancel", W * 0.5f, by + bh + 12, csgo::TextFaint, true);
}

// ------------------------------------------------------------------------------------------------
// buy menu
struct BuyItem { const char* alias; int weaponId; const char* label; int price; int team; const char* image; };
// CS:GO buy wheel: 6 categories around the money, then the items of the category around a BACK button
// CS:GO layout: 6 categories, at most 6 items each, no sniper category (scouts / AWPs / autos are rifles);
// slots with two guns (M4A4 / M4A1-S, ...) show the one chosen in the main menu LOADOUT screen
static constexpr int kNumCategories = LOADOUT_NUM_CATEGORIES;
static std::vector<BuyItem> GetCategory(int cat, int team) {
  std::vector<BuyItem> out;
  auto add = [&](const char* alias, int id, const char* label, int price, int t, const char* img) { if (t && team && t != team) return; out.push_back({ alias, id, label, price, t, img }); };
  switch (cat) {
    case LOADOUT_CAT_GEAR:
      add("vest", 0, "Kevlar Vest", 650, 0, "kevlar"); add("vesthelm", 0, "Kevlar + Helmet", 1000, 0, "kevlar_helmet");
      add("defuser", 0, "Defuse Kit", 400, FindConVar("mp_buy_anyteam")->GetBool() ? 0 : TEAM_CT, "defuser");
      break;
    case LOADOUT_CAT_GRENADES: {
      const bool any = FindConVar("mp_buy_anyteam")->GetBool();
      add("molotov", WEAPON_MOLOTOV, "Molotov", GetWeaponInfo(WEAPON_MOLOTOV)->price, any ? 0 : TEAM_TERRORIST, nullptr);
      add("incgrenade", WEAPON_INCGRENADE, "Incendiary Grenade", GetWeaponInfo(WEAPON_INCGRENADE)->price, any ? 0 : TEAM_CT, nullptr);
      add("flashbang", WEAPON_FLASHBANG, "Flashbang", GetWeaponInfo(WEAPON_FLASHBANG)->price, 0, nullptr);
      add("hegrenade", WEAPON_HEGRENADE, "HE Grenade", GetWeaponInfo(WEAPON_HEGRENADE)->price, 0, nullptr);
      add("smokegrenade", WEAPON_SMOKEGRENADE, "Smoke Grenade", GetWeaponInfo(WEAPON_SMOKEGRENADE)->price, 0, nullptr);
      break;
    }
    default:
      for (int id : Loadout_CategoryWeapons(cat, team)) {
        const CCSWeaponInfo* wi = GetWeaponInfo(id); if (!wi) continue;
        out.push_back({ WeaponIdToAlias(id), id, wi->printName, wi->price, wi->team, nullptr });
      }
      break;
  }
  if (out.size() > 6) out.resize(6);
  return out;
}
static const char* CategoryName(int cat) { return Loadout_CategoryName(cat); }

void HUD_BuyMenuSetCategory(int cat) { s_buyCategory = cat >= 0 && cat < kNumCategories ? cat : -1; s_hoverItem = -1; }
static void Buy(const char* alias) { Cbuf_AddText(std::string("buy ") + alias + "\n"); }
static void CloseBuyMenu() { cl.buyMenuOpen = false; s_buyCategory = -1; }

static const Texture2D* ItemImage(const BuyItem& it) { return it.image ? VGUI_ItemImage(it.image) : VGUI_WeaponImage(it.weaponId); }
static const Texture2D* CategoryImage(int cat, int team) {
  switch (cat) {
    case LOADOUT_CAT_PISTOLS: return VGUI_WeaponImage(WEAPON_DEAGLE);
    case LOADOUT_CAT_HEAVY: return VGUI_WeaponImage(WEAPON_XM1014);
    case LOADOUT_CAT_SMGS: return VGUI_WeaponImage(WEAPON_P90);
    case LOADOUT_CAT_RIFLES: return VGUI_WeaponImage(team == TEAM_CT ? WEAPON_M4A1 : WEAPON_AK47);
    case LOADOUT_CAT_GEAR: return VGUI_ItemImage("kevlar_helmet");
    default: return VGUI_WeaponImage(WEAPON_HEGRENADE);
  }
}

// select by number (1..n) at the current level; returns true when handled
static bool BuySelect(int n) {
  CCSPlayer* lp = g_pLocalPlayer; if (!lp) return false;
  if (s_buyCategory < 0) { if (n >= 1 && n <= kNumCategories) { s_buyCategory = n - 1; s_hoverItem = -1; return true; } return false; }
  auto items = GetCategory(s_buyCategory, lp->m_iTeamNum);
  if (n >= 1 && n <= (int)items.size()) { Buy(items[n - 1].alias); return true; }   // stays open (ESC / B / right click closes)
  return false;
}

void HUD_DrawBuyMenu() {
  CCSPlayer* lp = g_pLocalPlayer; if (!lp) return;
  if (!lp->IsAlive() || !lp->m_bInBuyZone || CSGameRules()->IsBuyTimeElapsed()) { CloseBuyMenu(); return; }
  const float W = (float)surface()->ScreenWide(), H = (float)surface()->ScreenTall();
  
  // Dark theme: black background
  DrawRectangle(0, 0, (int)W, (int)H, { 0, 0, 0, 200 });
  
  // Menu panel: centered, dark
  const float menuW = std::min(800.f, W * 0.7f), menuH = std::min(600.f, H * 0.8f);
  const float mx = W * 0.5f - menuW * 0.5f, my = H * 0.5f - menuH * 0.5f;
  
  // Background
  DrawRectangleRec({ mx, my, menuW, menuH }, { 20, 22, 26, 245 });
  DrawRectangleRec({ mx, my, menuW, 3 }, { 60, 65, 75, 255 }); // top line
  
  Vector2 m = GetMousePosition();
  
  if (s_buyCategory < 0) {
    // Main menu: 6 categories (5 active + 1 empty)
    const float colW = (menuW - 40) / 3.f, rowH = 90.f;
    const char* catNames[] = { "PISTOLS", "MID-TIER", "RIFLES", "EQUIPMENT", "GRENADES", "" };
    
    surface()->DrawTextCentered(FONT_GO_TITLE, "BUY MENU", mx + menuW * 0.5f, my + 30, csgo::Text, true);
    surface()->DrawTextCentered(FONT_GO_TITLE, TextFormat("$%d", lp->m_iAccount), mx + menuW * 0.5f, my + 60, csgo::Green, true);
    
    for (int i = 0; i < 6; i++) {
      int row = i / 3, col = i % 3;
      float x = mx + 20 + col * (colW + 10), y = my + 100 + row * (rowH + 10);
      
      bool isEmpty = (i == 5); // 6th slot empty
      bool hv = !isEmpty && m.x >= x && m.x < x + colW && m.y >= y && m.y < y + rowH;
      
      // Dark panel, no blue
      Color bg = isEmpty ? Color{ 15, 16, 18, 200 } : hv ? Color{ 45, 50, 60, 240 } : Color{ 30, 33, 38, 230 };
      DrawRectangleRec({ x, y, colW, rowH }, bg);
      
      if (!isEmpty) {
        // Category icon
        if (const Texture2D* img = CategoryImage(i, lp->m_iTeamNum)) {
          float iw = 60, ih = 40;
          Color tint = hv ? WHITE : Color{ 180, 180, 180, 220 };
          VGUI_DrawImageFit(*img, x + 10, y + 10, iw, ih, tint);
        }
        surface()->DrawText(FONT_GO_BOLD, catNames[i], x + 80, y + 20, csgo::Text);
        surface()->DrawText(FONT_GO, TextFormat("[%d]", i + 1), x + colW - 40, y + 20, csgo::TextDim);
        
        if (hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
          EmitSound2D("UI.Click");
          s_buyCategory = i;
        }
      }
    }
    
    surface()->DrawTextCentered(FONT_GO, "[0] CLOSE", mx + menuW * 0.5f, my + menuH - 40, csgo::TextDim, true);
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || (IsKeyPressed(KEY_ESCAPE))) CloseBuyMenu();
  } else {
    // Category view: items list
    auto items = GetCategory(s_buyCategory, lp->m_iTeamNum);
    const float itemH = 60.f;
    
    // Header
    surface()->DrawText(FONT_GO_TITLE, CategoryName(s_buyCategory), mx + 20, my + 20, csgo::Text);
    surface()->DrawText(FONT_GO_TITLE, TextFormat("$%d", lp->m_iAccount), mx + menuW - 150, my + 20, csgo::Green);
    DrawRectangleRec({ mx, my + 50, menuW, 2 }, { 50, 55, 65, 255 });
    
    // Items list
    for (size_t i = 0; i < items.size(); i++) {
      const BuyItem& it = items[i];
      float y = my + 70 + i * (itemH + 8);
      if (y + itemH > my + menuH - 60) break;
      
      bool canAfford = lp->m_iAccount >= it.price;
      bool hv = m.x >= mx + 20 && m.x < mx + menuW - 20 && m.y >= y && m.y < y + itemH;
      
      Color bg = hv && canAfford ? Color{ 45, 50, 60, 240 } : Color{ 30, 33, 38, 230 };
      DrawRectangleRec({ mx + 20, y, menuW - 40, itemH }, bg);
      
      // Item icon
      if (const Texture2D* img = ItemImage(it)) {
        float iw = 50, ih = 35;
        Color tint = canAfford ? (hv ? WHITE : Color{ 200, 200, 200, 220 }) : Color{ 100, 100, 100, 180 };
        VGUI_DrawImageFit(*img, mx + 30, y + 12, iw, ih, tint);
      }
      
      // Item name
      Color nameCol = canAfford ? csgo::Text : csgo::TextDim;
      surface()->DrawText(FONT_GO_BOLD, it.label, mx + 90, y + 15, nameCol);
      
      // Price
      Color priceCol = canAfford ? csgo::Green : csgo::Red;
      surface()->DrawText(FONT_GO, TextFormat("$%d", it.price), mx + menuW - 120, y + 15, priceCol);
      
      // Hotkey
      surface()->DrawText(FONT_GO, TextFormat("[%d]", i + 1), mx + menuW - 60, y + 15, csgo::TextDim);
      
      if (hv && canAfford && IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
        EmitSound2D("UI.Click");
        Buy(it.alias);
        CloseBuyMenu();
      }
    }
    
    // Back button
    float backY = my + menuH - 50;
    bool backHv = m.x >= mx + 20 && m.x < mx + 120 && m.y >= backY && m.y < backY + 36;
    DrawRectangleRec({ mx + 20, backY, 100, 36 }, backHv ? Color{ 50, 55, 65, 240 } : Color{ 35, 38, 45, 230 });
    surface()->DrawTextCentered(FONT_GO_BOLD, "[0] BACK", mx + 70, backY + 18, csgo::Text, true);
    
    if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT) && backHv) {
      EmitSound2D("UI.Click");
      s_buyCategory = -1;
    }
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT) || IsKeyPressed(KEY_ESCAPE)) {
      if (s_buyCategory >= 0) s_buyCategory = -1; else CloseBuyMenu();
    }
  }
  
  // Buy time indicator
  if (!CSGameRules()->IsFreezePeriod() && !CSGameRules()->IsDeathmatch()) {
    float bt = CSGameRules()->m_fRoundStartTime + FindConVar("mp_buytime")->GetFloat() - ServerTime();
    surface()->DrawTextCentered(FONT_GO, TextFormat("Buy time: %ds", (int)std::max(0.f, bt)), mx + menuW * 0.5f, my + menuH - 15, csgo::Gold, true);
  }
}


// ------------------------------------------------------------------------------------------------
// scoreboard
extern std::vector<std::pair<int, int>> g_HudRoundHistory;
namespace {
constexpr Color kSbCT = { 93, 121, 174, 255 }, kSbT = { 222, 155, 53, 255 };   // CS:GO scoreboard team colours
Color SbTeam(int team) { return team == TEAM_CT ? kSbCT : kSbT; }
void SbIcon(const char* svg, float x, float y, float h, Color c) {
  if (const Texture2D* ic = VGUI_SvgIcon(svg)) { float w = h * ic->width / (float)std::max(1, ic->height); DrawTexturePro(*ic, { 0, 0, (float)ic->width, (float)ic->height }, { x - w / 2, y - h / 2, w, h }, { 0, 0 }, 0, c); }
}
void SbRoundGlyph(int reason, float x, float y, float s, Color c) {
  if (reason == Target_Bombed) { if (VGUI_SvgIcon("weapon_c4")) SbIcon("weapon_c4", x, y, s, c); else surface()->DrawFilledRect(x - s * 0.4f, y - s * 0.3f, s * 0.8f, s * 0.6f, c); }
  else if (reason == Bomb_Defused) { if (VGUI_SvgIcon("item_defuser")) SbIcon("item_defuser", x, y, s, c); else surface()->DrawFilledRect(x - s * 0.4f, y - s * 0.15f, s * 0.8f, s * 0.3f, c); }
  else if (reason == Target_Saved) {   // clock
    surface()->DrawFilledCircle(x, y, s * 0.42f, c); surface()->DrawFilledCircle(x, y, s * 0.30f, { 20, 22, 26, 255 });
    surface()->DrawLine(x, y, x, y - s * 0.24f, c, 1.5f); surface()->DrawLine(x, y, x + s * 0.18f, y, c, 1.5f);
  } else {   // elimination: skull
    surface()->DrawFilledCircle(x, y - s * 0.06f, s * 0.36f, c); surface()->DrawFilledRect(x - s * 0.2f, y + s * 0.12f, s * 0.4f, s * 0.26f, c);
    Color e = { 20, 22, 26, 255 }; surface()->DrawFilledCircle(x - s * 0.14f, y - s * 0.04f, s * 0.09f, e); surface()->DrawFilledCircle(x + s * 0.14f, y - s * 0.04f, s * 0.09f, e);
  }
}
}

// CS:GO scoreboard: header with the match score, CT and T panels (ping, name, money, K / A / D, MVPs, HS %, score)
// and the round history strip between them
void HUD_DrawScoreboard() {
  using namespace csgo;
  const int W = surface()->ScreenWide(), H = surface()->ScreenTall();
  CCSGameRules* gr = CSGameRules(); CCSPlayer* lp = g_pLocalPlayer;
  const float sc = std::clamp(H / 900.f, 0.7f, 1.4f);
  const float w = std::min(1040.f * sc, W - 30.f), x = (W - w) / 2;
  const float rowH = 26 * sc, headH = 54 * sc, histH = 34 * sc;
  std::vector<CCSPlayer*> teams[2];
  for (CCSPlayer* p : UTIL_GetPlayers()) { if (p->m_iTeamNum == TEAM_CT) teams[0].push_back(p); else if (p->m_iTeamNum == TEAM_TERRORIST) teams[1].push_back(p); }
  for (auto& t : teams) std::sort(t.begin(), t.end(), [](CCSPlayer* a, CCSPlayer* b) { return a->m_iScore != b->m_iScore ? a->m_iScore > b->m_iScore : a->m_iKills != b->m_iKills ? a->m_iKills > b->m_iKills : a->m_iDeaths < b->m_iDeaths; });
  const int rowsCT = std::max<int>(5, (int)teams[0].size()), rowsT = std::max<int>(5, (int)teams[1].size());
  const float colHeadH = 20 * sc;
  float totalH = headH + 8 * sc + (colHeadH + rowsCT * rowH) + histH + (colHeadH + rowsT * rowH) + 30 * sc;
  float y = std::max(10.f, (H - totalH) / 2);
  // header: map, score, round
  surface()->DrawFilledRect(x, y, w, headH, { 12, 14, 17, 225 });
  const int ctWins = gr ? gr->m_iNumCTWins : 0, tWins = gr ? gr->m_iNumTerroristWins : 0;
  const float mx = x + w / 2;
  surface()->DrawFilledRect(mx - 150 * sc, y, 120 * sc, headH, Alpha(kSbCT, 0.85f));
  surface()->DrawFilledRect(mx + 30 * sc, y, 120 * sc, headH, Alpha(kSbT, 0.85f));
  surface()->DrawTextCentered(FONT_GO_HUGE, TextFormat("%d", ctWins), mx - 90 * sc, y + headH / 2 - 24 * sc, Text, true);
  surface()->DrawTextCentered(FONT_GO_HUGE, TextFormat("%d", tWins), mx + 90 * sc, y + headH / 2 - 24 * sc, Text, true);
    surface()->DrawTextCentered(FONT_GO_TITLE, ":", mx, y + headH / 2 - 12 * sc, TextDim);
  surface()->DrawText(FONT_GO_TITLE, g_MapInfo.name.c_str(), x + 16 * sc, y + 10 * sc, Text);
  surface()->DrawText(FONT_GO, "Competitive  |  Counter-Strike: HvH", x + 16 * sc, y + 32 * sc, TextDim);
  if (gr) surface()->DrawTextRight(FONT_GO_BOLD, TextFormat("ROUND %d / %d", gr->m_iTotalRoundsPlayed + 1, FindConVar("mp_maxrounds")->GetInt()), x + w - 16 * sc, y + 19 * sc, Text);
  y += headH + 8 * sc;

  // columns (right aligned, from the right edge)
  const float cScore = x + w - 14 * sc, cHs = cScore - 78 * sc, cMvp = cHs - 62 * sc, cD = cMvp - 56 * sc, cA = cD - 44 * sc, cK = cA - 44 * sc, cMoney = cK - 76 * sc;
  const float cPing = x + 100 * sc, cName = x + 146 * sc;
  auto teamPanel = [&](int ti, int team, int rows) {
    const Color tc = SbTeam(team);
    const bool showMoney = lp && lp->m_iTeamNum == team;
    // column header
    surface()->DrawFilledRect(x, y, w, colHeadH, { 12, 14, 17, 215 });
    surface()->DrawFilledRect(x, y, 4 * sc, colHeadH + rows * rowH, tc);
    const float hy = y + 3 * sc; const Color hc = TextDim;
    surface()->DrawText(FONT_GO_BOLD, team == TEAM_CT ? "COUNTER-TERRORISTS" : "TERRORISTS", cName, hy, tc);
    surface()->DrawTextRight(FONT_GO, "PING", cPing, hy, hc);
    if (showMoney) surface()->DrawTextRight(FONT_GO, "MONEY", cMoney, hy, hc);
    surface()->DrawTextRight(FONT_GO, "K", cK, hy, hc); surface()->DrawTextRight(FONT_GO, "A", cA, hy, hc); surface()->DrawTextRight(FONT_GO, "D", cD, hy, hc);
    surface()->DrawTextRight(FONT_GO, "MVP", cMvp, hy, hc); surface()->DrawTextRight(FONT_GO, "HS%", cHs, hy, hc); surface()->DrawTextRight(FONT_GO, "SCORE", cScore, hy, hc);
    y += colHeadH;
    // big team score block on the left
    surface()->DrawFilledRect(x + 4 * sc, y, 50 * sc, rows * rowH, Alpha(tc, 0.28f));
    surface()->DrawTextCentered(FONT_GO_HUGE, TextFormat("%d", team == TEAM_CT ? ctWins : tWins), x + 29 * sc, y + rows * rowH / 2 - 16 * sc, tc, true);
    for (int r = 0; r < rows; r++) {
      const float ry = y + r * rowH;
      Color bg = (r & 1) ? Color{ 34, 37, 43, 215 } : Color{ 26, 29, 34, 215 };
      CCSPlayer* p = r < (int)teams[ti].size() ? teams[ti][r] : nullptr;
      if (p && p == lp) bg = { 64, 70, 82, 235 };
      surface()->DrawFilledRect(x + 54 * sc, ry, w - 54 * sc, rowH - 1, bg);
      if (!p) continue;
      const bool dead = !p->IsAlive();
      Color c = dead ? Color{ 130, 134, 142, 255 } : Text;
      const float ty = ry + (rowH - 15 * sc) / 2;
      surface()->DrawTextRight(FONT_GO, p->IsBot() ? "BOT" : TextFormat("%d", p->m_iPing), cPing, ty, TextDim);
      // avatar square in team colour
      surface()->DrawFilledRect(cName - 30 * sc, ry + 3 * sc, rowH - 6 * sc, rowH - 6 * sc, Alpha(tc, dead ? 0.35f : 0.9f));
      if (dead) SbRoundGlyph(CTs_Win, cName - 30 * sc + (rowH - 6 * sc) / 2, ry + rowH / 2, rowH * 0.55f, { 230, 230, 230, 200 });
      surface()->DrawText(FONT_GO_BOLD, p->GetPlayerName(), cName, ty, c);
      float sx = cName + surface()->GetTextWide(FONT_GO_BOLD, p->GetPlayerName()) + 10 * sc;
      if (p->HasC4() && lp && lp->m_iTeamNum == TEAM_TERRORIST) { SbIcon("weapon_c4", sx + 9 * sc, ry + rowH / 2, 14 * sc, { 255, 120, 120, 240 }); sx += 24 * sc; }
      if (p->m_bHasDefuser && lp && lp->m_iTeamNum == TEAM_CT) { SbIcon("item_defuser", sx + 9 * sc, ry + rowH / 2, 14 * sc, { 140, 200, 255, 240 }); sx += 24 * sc; }
      if (showMoney) surface()->DrawTextRight(FONT_GO, TextFormat("$%d", p->m_iAccount), cMoney, ty, { 140, 210, 120, 255 });
      surface()->DrawTextRight(FONT_GO_BOLD, TextFormat("%d", p->m_iKills), cK, ty, c);
      surface()->DrawTextRight(FONT_GO, TextFormat("%d", p->m_iAssists), cA, ty, c);
      surface()->DrawTextRight(FONT_GO, TextFormat("%d", p->m_iDeaths), cD, ty, c);
      if (p->m_iMVPs > 0) surface()->DrawTextRight(FONT_GO, TextFormat("\xE2\x98\x85%d", p->m_iMVPs), cMvp, ty, Gold);
      surface()->DrawTextRight(FONT_GO, p->m_iKills ? TextFormat("%d%%", (int)std::lround(100.0 * p->m_iHeadshots / p->m_iKills)) : "0%", cHs, ty, c);
      surface()->DrawTextRight(FONT_GO_BOLD, TextFormat("%d", p->m_iScore), cScore, ty, c);
    }
    y += rows * rowH;
  };
  teamPanel(0, TEAM_CT, rowsCT);
  // round history: one cell per round of the half/match, coloured by the winner with the win reason glyph
  {
    surface()->DrawFilledRect(x, y, w, histH, { 12, 14, 17, 200 });
    const int maxRounds = std::max(1, FindConVar("mp_maxrounds")->GetInt());
    const float cell = std::min(26.f * sc, (w - 120 * sc) / maxRounds), hx = x + 110 * sc;
    surface()->DrawText(FONT_GO, "ROUNDS", x + 16 * sc, y + histH / 2 - 8 * sc, TextDim);
    for (int i = 0; i < maxRounds; i++) {
      const float cx = hx + i * cell + cell / 2, cy = y + histH / 2;
      if (i == maxRounds / 2 && maxRounds > 1) surface()->DrawFilledRect(hx + i * cell - 1, y + 4 * sc, 2, histH - 8 * sc, TextFaint);
      if (i < (int)g_HudRoundHistory.size()) {
        const auto& h = g_HudRoundHistory[i];
        const Color wc = SbTeam(h.first);
        surface()->DrawFilledRect(cx - cell * 0.42f, cy + (h.first == TEAM_CT ? -cell * 0.5f : 0.f), cell * 0.84f, cell * 0.5f, Alpha(wc, 0.25f));
        SbRoundGlyph(h.second, cx, cy + (h.first == TEAM_CT ? -cell * 0.25f : cell * 0.25f), cell * 0.5f, wc);
      } else surface()->DrawFilledCircle(cx, cy, 1.6f * sc, TextFaint);
    }
    y += histH;
  }
  teamPanel(1, TEAM_TERRORIST, rowsT);
  // spectators
  std::string specs; for (CCSPlayer* p : UTIL_GetPlayers()) if (p->m_iTeamNum <= TEAM_SPECTATOR) { if (!specs.empty()) specs += ", "; specs += p->GetPlayerName(); }
  if (!specs.empty()) surface()->DrawText(FONT_GO, ("Spectators: " + specs).c_str(), x + 16 * sc, y + 8 * sc, TextDim);
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
    if (key == KEY_ESCAPE || key == KEY_B) { if (s_buyCategory >= 0 && key == KEY_ESCAPE) s_buyCategory = -1; else CloseBuyMenu(); return true; }
    if (n == 0) { if (s_buyCategory >= 0) s_buyCategory = -1; else CloseBuyMenu(); return true; }
    if (n > 0) { if (BuySelect(n)) EmitSound2D("UI.Click"); return true; }
    if (key == KEY_F1 || key == KEY_F2) return false;
    return true;
  }
  return false;
}

void HUD_ChatChar(int cp) {
  if (!cl.chatOpen || cp < 32 || cl.chatBuffer.size() > 120) return;
  int n = 0; const char* u = CodepointToUTF8(cp, &n); cl.chatBuffer.append(u, n);
}
