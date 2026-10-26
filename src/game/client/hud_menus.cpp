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
      add("primammo", 0, "Primary Ammo", 0, 0, "bullet"); add("secammo", 0, "Secondary Ammo", 0, 0, "cartridge");
      break;
    case LOADOUT_CAT_GRENADES:
      add("flashbang", WEAPON_FLASHBANG, "Flashbang", GetWeaponInfo(WEAPON_FLASHBANG)->price, 0, nullptr);
      add("hegrenade", WEAPON_HEGRENADE, "HE Grenade", GetWeaponInfo(WEAPON_HEGRENADE)->price, 0, nullptr);
      add("smokegrenade", WEAPON_SMOKEGRENADE, "Smoke Grenade", GetWeaponInfo(WEAPON_SMOKEGRENADE)->price, 0, nullptr);
      break;
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
  if (n >= 1 && n <= (int)items.size()) { Buy(items[n - 1].alias); CloseBuyMenu(); return true; }
  return false;
}

void HUD_DrawBuyMenu() {
  CCSPlayer* lp = g_pLocalPlayer; if (!lp) return;
  if (!lp->IsAlive() || !lp->m_bInBuyZone || CSGameRules()->IsBuyTimeElapsed()) { CloseBuyMenu(); return; }
  const float W = (float)surface()->ScreenWide(), H = (float)surface()->ScreenTall();
  const float cx = W * 0.5f, cy = H * 0.5f;
  const float R = std::min(H * 0.44f, 330.f), r0 = R * 0.34f;
  const int team = lp->m_iTeamNum;
  DrawRectangle(0, 0, (int)W, (int)H, { 0, 0, 0, 110 });
  // entries of the current ring
  struct Entry { std::string label, price; const Texture2D* img; bool enabled; int hotkey; };
  std::vector<Entry> entries;
  std::vector<BuyItem> items;
  if (s_buyCategory < 0) {
    for (int i = 0; i < kNumCategories; i++) entries.push_back({ CategoryName(i), "", CategoryImage(i, team), true, i + 1 });
  } else {
    items = GetCategory(s_buyCategory, team);
    for (size_t i = 0; i < items.size(); i++) {
      const BuyItem& it = items[i];
      entries.push_back({ it.label, it.price > 0 ? TextFormat("$%d", it.price) : "", ItemImage(it), lp->m_iAccount >= it.price, (int)i + 1 });
    }
  }
  const int n = std::max(1, (int)entries.size());
  const float span = 360.f / n;
  // hover by direction (outside the center button)
  Vector2 m = GetMousePosition();
  float dx = m.x - cx, dy = m.y - cy, dist = std::sqrt(dx * dx + dy * dy);
  int hover = -1;
  if (dist > r0 && dist < R * 1.25f) {
    float ang = std::atan2(dy, dx) * RAD2DEG;          // screen space: 0 = right, clockwise
    float rel = ang + 90.f + span * 0.5f; while (rel < 0) rel += 360.f; while (rel >= 360.f) rel -= 360.f;
    hover = std::min(n - 1, (int)(rel / span));
  }
  if (hover != s_hoverItem && hover >= 0) EmitSound2D("UI.Rollover");
  s_hoverItem = hover;
  // ring
  for (int i = 0; i < (int)entries.size(); i++) {
    float a0 = -90.f + i * span - span * 0.5f + 0.8f, a1 = a0 + span - 1.6f;
    bool hv = i == hover;
    const Entry& e = entries[i];
    Color fill = hv ? Color{ 64, 72, 86, 235 } : Color{ 24, 27, 33, 225 };
    DrawRing({ cx, cy }, r0 + 4, R, a0, a1, 40, fill);
    DrawRing({ cx, cy }, R - 4, R, a0, a1, 40, hv ? csgo::Accent : Color{ 70, 76, 88, 255 });
    float mid = (-90.f + i * span) * DEG2RAD, rc = (r0 + R) * 0.5f + 4;
    float ex = cx + std::cos(mid) * rc, ey = cy + std::sin(mid) * rc;
    float iw = std::min((R - r0) * 0.86f, 2.f * rc * std::sin(span * 0.5f * DEG2RAD) * 0.8f), ih = iw * 0.45f;
    Color tint = e.enabled ? (hv ? WHITE : Color{ 215, 215, 215, 255 }) : Color{ 110, 110, 110, 200 };
    if (e.img) VGUI_DrawImageFit(*e.img, ex - iw * 0.5f, ey - ih * 0.5f - 8, iw, ih, tint);
    surface()->DrawTextCentered(FONT_GO_BOLD, e.label.c_str(), ex, ey + ih * 0.5f - 4, e.enabled ? csgo::Text : csgo::TextFaint, true);
    if (!e.price.empty()) surface()->DrawTextCentered(FONT_GO, e.price.c_str(), ex, ey + ih * 0.5f + 14, e.enabled ? csgo::Green : csgo::Red, true);
    // hotkey number near the outer edge
    float hx = cx + std::cos(mid) * (R - 16), hy = cy + std::sin(mid) * (R - 16);
    if (e.hotkey <= 9) surface()->DrawTextCentered(FONT_GO, TextFormat("%d", e.hotkey), hx, hy - 8, csgo::TextFaint);
  }
  // center button: money + (category: close / items: back)
  bool centerHv = dist <= r0;
  DrawCircleV({ cx, cy }, r0, centerHv ? Color{ 52, 58, 70, 240 } : Color{ 16, 18, 22, 240 });
  DrawRing({ cx, cy }, r0 - 3, r0, 0, 360, 48, Color{ 70, 76, 88, 255 });
  surface()->DrawTextCentered(FONT_GO_TITLE, TextFormat("$%d", lp->m_iAccount), cx, cy - 30, csgo::Green, true);
  const char* title = s_buyCategory < 0 ? "BUY MENU" : CategoryName(s_buyCategory);
  surface()->DrawTextCentered(FONT_GO_BOLD, title, cx, cy - 2, csgo::Text, true);
  surface()->DrawTextCentered(FONT_GO, s_buyCategory < 0 ? "[0] CLOSE" : "[0] BACK", cx, cy + 20, centerHv ? WHITE : csgo::TextDim, true);
  if (!CSGameRules()->IsFreezePeriod() && !CSGameRules()->IsDeathmatch()) {
    float bt = CSGameRules()->m_fRoundStartTime + FindConVar("mp_buytime")->GetFloat() - ServerTime();
    surface()->DrawTextCentered(FONT_GO, TextFormat("%ds", (int)std::max(0.f, bt)), cx, cy + 40, csgo::Gold, true);
  }
  // item info (stats) on the right for weapons
  if (s_buyCategory >= 0 && hover >= 0 && hover < (int)items.size()) {
    const BuyItem& it = items[hover];
    if (const CCSWeaponInfo* wi = it.weaponId ? GetWeaponInfo(it.weaponId) : nullptr) if (wi->type != WEAPONTYPE_GRENADE && cx + R + 260 < W) {
      float px = cx + R + 30, py = cy - 110, pw = 230;
      DrawRectangleRec({ px, py, pw, 220 }, { 16, 18, 22, 225 });
      DrawRectangleRec({ px, py, pw, 3 }, csgo::Accent);
      surface()->DrawText(FONT_GO_BOLD, it.label, px + 12, py + 12, csgo::Text);
      float ly = py + 42;
      auto stat = [&](const char* name, const char* val, float frac) {
        surface()->DrawText(FONT_GO, name, px + 12, ly, csgo::TextDim); surface()->DrawTextRight(FONT_GO, val, px + pw - 12, ly, csgo::Text);
        DrawRectangleRec({ px + 12, ly + 19, pw - 24, 4 }, csgo::Line); DrawRectangleRec({ px + 12, ly + 19, (pw - 24) * Clamp(frac, 0.f, 1.f), 4 }, csgo::Accent);
        ly += 34;
      };
      stat("Damage", TextFormat("%d", wi->damage * std::max(1, wi->bullets)), wi->damage * std::max(1, wi->bullets) / 120.f);
      stat("Fire rate", TextFormat("%d RPM", (int)(60.f / std::max(0.01f, wi->cycleTime))), (60.f / wi->cycleTime) / 1000.f);
      stat("Armor penetration", TextFormat("%d%%", (int)(wi->armorRatio * 50)), wi->armorRatio * 0.5f);
      stat("Magazine", TextFormat("%d / %d", wi->clipSize, wi->reserveAmmo), wi->clipSize / 100.f);
      stat("Mobility", TextFormat("%d", (int)wi->maxSpeed), wi->maxSpeed / 260.f);
    }
  }
  // clicks / taps (released on the entry it was pressed on is not required: the wheel selects by direction)
  if (IsMouseButtonReleased(MOUSE_BUTTON_LEFT)) {
    if (centerHv) { if (s_buyCategory >= 0) s_buyCategory = -1; else CloseBuyMenu(); EmitSound2D("UI.Click"); }
    else if (hover >= 0 && hover < (int)entries.size() && entries[hover].enabled) { EmitSound2D("UI.Click"); BuySelect(hover + 1); }
  }
  if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) { if (s_buyCategory >= 0) s_buyCategory = -1; else CloseBuyMenu(); }
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
