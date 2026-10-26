// menu.cpp - gamesense-style cheat menu (INSERT): tabs, group boxes, checkbox/slider/combo/multicombo,
// key binds with modes, colour pickers, config system
#include "cheat_internal.hpp"
#include "../vgui/surface.hpp"
#include "../vgui/controls.hpp"
#include "../game/client/cl_main.hpp"
#include "../game/shared/cs_player.hpp"
#include "../game/shared/soundemitter.hpp"
#include "../game/client/touch.hpp"
#include "../engine/convar.hpp"
#include <functional>
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <cstdarg>
#include <algorithm>
#include <cctype>
#include <cstdio>

namespace cheat {
Vars v;
PlayerSettings g_PlayerList[65];
RuntimeState g;

using vgui::surface;

// ------------------------------------------------------------------------------------------------
// key binds
constexpr int K_TOUCHBIND = 3000;   // virtual keys of the on-screen bind circles (3000 + index in g_NamedBinds)
static bool TouchUI() { static ConVar* t = FindConVar("touch_enable"); return t && t->GetBool(); }
static bool RawKeyDown(int key) {
  if (key <= 0) return false;
  if (key >= K_TOUCHBIND && key < K_TOUCHBIND + 64) return Touch_BindDown(key - K_TOUCHBIND);
  if (key >= K_MOUSE1 && key <= K_MOUSE5) { static const int mb[5] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_SIDE, MOUSE_BUTTON_EXTRA }; return IsMouseButtonDown(mb[key - K_MOUSE1]); }
  if (key >= 1000) return false;
  return IsKeyDown(key);
}
static bool RawKeyPressed(int key) {
  if (key <= 0) return false;
  if (key >= K_TOUCHBIND && key < K_TOUCHBIND + 64) return Touch_BindPressed(key - K_TOUCHBIND);
  if (key >= K_MOUSE1 && key <= K_MOUSE5) { static const int mb[5] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_SIDE, MOUSE_BUTTON_EXTRA }; return IsMouseButtonPressed(mb[key - K_MOUSE1]); }
  if (key >= 1000) return false;
  return IsKeyPressed(key);
}
bool KeyBind::Active() const {
  switch (mode) {
    case KEYMODE_ALWAYS: return true;
    case KEYMODE_HOLD: return key && InGameInput() && RawKeyDown(key);
    case KEYMODE_TOGGLE: return toggled;
    case KEYMODE_OFFHOTKEY: return !(key && InGameInput() && RawKeyDown(key));
  }
  return false;
}
static std::vector<KeyBind*> s_allBinds;
static void UpdateToggles() {
  if (!InGameInput() || Menu_IsCapturingKey()) return;
  for (KeyBind* b : s_allBinds) if (b->mode == KEYMODE_TOGGLE && b->key && RawKeyPressed(b->key)) {
    // mouse binds only toggle when the cursor isn't over the menu
    if (b->key >= K_MOUSE1 && g.menuOpen) continue;
    b->toggled = !b->toggled;
  }
}

// ------------------------------------------------------------------------------------------------
// registry (config + keybind list)
enum VarType { VT_BOOL, VT_INT, VT_FLOAT, VT_COL, VT_KEY };
struct VarEntry { std::string name; VarType type; void* ptr; };
static std::vector<VarEntry> s_registry;
static void R(const char* n, bool& x) { s_registry.push_back({ n, VT_BOOL, &x }); }
static void R(const char* n, int& x) { s_registry.push_back({ n, VT_INT, &x }); }
static void R(const char* n, float& x) { s_registry.push_back({ n, VT_FLOAT, &x }); }
static void R(const char* n, Col& x) { s_registry.push_back({ n, VT_COL, &x }); }
static void R(const char* n, KeyBind& x) { s_registry.push_back({ n, VT_KEY, &x }); s_allBinds.push_back(&x); }
// binds shown in the keybind list
struct NamedBind { const char* name; KeyBind* bind; };
std::vector<NamedBind> g_NamedBinds;
static const char* s_touchLabels[] = { "RAGE", "DMG", "BAIM", "SAFE", "DT", "HS", "INV", "FS", "LEFT", "RIGHT", "BACK", "FD", "SLOW", "3RD", "EJ" };
void GetTouchBinds(std::vector<TouchBind>& out) {
  out.clear();
  for (size_t i = 0; i < g_NamedBinds.size(); i++) {
    const KeyBind& kb = *g_NamedBinds[i].bind;
    if (kb.key != K_TOUCHBIND + (int)i) continue;
    out.push_back({ (int)i, i < sizeof(s_touchLabels) / sizeof(*s_touchLabels) ? s_touchLabels[i] : "BIND", kb.Active() });
  }
}

static void RegisterVars() {
  auto& r = v.rage; auto& a = v.aa; auto& s = v.vis; auto& m = v.misc;
  R("rage.enabled", r.enabled); R("rage.key", r.key); R("rage.target", r.target); R("rage.hitboxes", r.hitboxes); R("rage.multipoint", r.multipoint);
  R("rage.head_scale", r.headScale); R("rage.body_scale", r.bodyScale); R("rage.autofire", r.autoFire); R("rage.silent", r.silent); R("rage.autowall", r.autowall);
  R("rage.autoscope", r.autoscope); R("rage.quickstop", r.quickStop); R("rage.quickstop_between", r.quickStopBetweenShots); R("rage.hitchance", r.hitchance);
  R("rage.mindmg", r.minDamage); R("rage.mindmg_override_key", r.minDamageOverrideKey); R("rage.mindmg_override", r.minDamageOverride); R("rage.prefer_baim", r.preferBaim);
  R("rage.force_baim_key", r.forceBaimKey); R("rage.safepoint", r.safePoint); R("rage.force_safe_key", r.forceSafeKey); R("rage.fov", r.fov); R("rage.norecoil", r.removeRecoil);
  R("rage.nospread", r.compensateSpread); R("rage.backtrack", r.backtrack); R("rage.delayshot", r.delayShot); R("rage.dt_key", r.doubletapKey); R("rage.dt_mode", r.doubletapMode);
  R("rage.hs_key", r.hideshotsKey); R("rage.dt", r.doubletap); R("rage.hs", r.hideshots); R("rage.logmisses", r.logMisses); R("rage.knifebot", r.knifebot); R("rage.resolver", r.resolver);
  R("aa.enabled", a.enabled); R("aa.pitch", a.pitch); R("aa.yawbase", a.yawBase); R("aa.yaw", a.yaw); R("aa.yawadd", a.yawAdd); R("aa.jitter", a.jitter); R("aa.jitter_range", a.jitterRange);
  R("aa.spin", a.spinSpeed); R("aa.bodyyaw", a.bodyYaw); R("aa.inverter", a.inverter); R("aa.fakelimit", a.fakeLimit); R("aa.freestand_body", a.freestandBody); R("aa.freestand_key", a.freestandKey);
  R("aa.edge", a.edgeYaw); R("aa.manual_left", a.manualLeft); R("aa.manual_right", a.manualRight); R("aa.manual_back", a.manualBack); R("aa.lby", a.lbyMode);
  R("aa.fakelag", a.fakelag); R("aa.fakelag_mode", a.fakelagMode); R("aa.fakelag_limit", a.fakelagLimit); R("aa.fakelag_var", a.fakelagVariance); R("aa.fakelag_shot", a.fakelagOnShot);
  R("aa.fakeduck_key", a.fakeDuckKey); R("aa.slowwalk_key", a.slowWalkKey); R("aa.slowwalk_speed", a.slowWalkSpeed); R("aa.legs", a.legMovement); R("aa.onshot", a.onShotAA);
  R("vis.team", s.teammates); R("vis.box", s.box); R("vis.box_col", s.boxCol); R("vis.name", s.name); R("vis.name_col", s.nameCol); R("vis.health", s.health); R("vis.weapon", s.weapon);
  R("vis.ammo", s.ammo); R("vis.ammo_col", s.ammoCol); R("vis.flags", s.flags); R("vis.skeleton", s.skeleton); R("vis.skeleton_col", s.skeletonCol); R("vis.oof", s.oof); R("vis.oof_col", s.oofCol);
  R("vis.oof_radius", s.oofRadius); R("vis.shot_hitbox", s.hitboxOnShot); R("vis.shot_hitbox_col", s.hitboxCol); R("vis.glow", s.glow); R("vis.glow_col", s.glowCol); R("vis.radar", s.radarReveal);
  R("vis.chams", s.chams); R("vis.chams_col", s.chamsCol); R("vis.xqz", s.chamsXqz); R("vis.xqz_col", s.chamsXqzCol); R("vis.chams_mat", s.chamsMaterial); R("vis.chams_team", s.chamsTeam);
  R("vis.chams_team_col", s.chamsTeamCol); R("vis.chams_bt", s.chamsBacktrack); R("vis.chams_bt_col", s.chamsBacktrackCol); R("vis.chams_local", s.chamsLocal); R("vis.chams_local_col", s.chamsLocalCol);
  R("vis.chams_fake", s.chamsFake); R("vis.chams_fake_col", s.chamsFakeCol); R("vis.scope_blend", s.scopeBlend); R("vis.noflash", s.removeFlash); R("vis.nosmoke", s.removeSmoke);
  R("vis.noscope", s.removeScope); R("vis.novisrecoil", s.removeRecoil); R("vis.nightmode", s.nightmode); R("vis.tp_key", s.thirdpersonKey); R("vis.tp_dist", s.thirdpersonDist);
  R("vis.tp_dead", s.thirdpersonDead); R("vis.force_xhair", s.forceCrosshair); R("vis.pen_xhair", s.penCrosshair); R("vis.tracers", s.tracers); R("vis.tracer_col", s.tracerCol);
  R("vis.impacts", s.impacts); R("vis.nade_pred", s.grenadePrediction); R("vis.nade_col", s.grenadeCol); R("vis.spread", s.spreadCircle); R("vis.fov", s.fov); R("vis.zoomfov", s.zoomFov);
  R("vis.vmfov", s.viewmodelFov); R("vis.hitmarker3d", s.hitmarker3d); R("vis.dmgmarker", s.damageMarker); R("vis.indicators", s.indicators); R("vis.arrows", s.aaArrows);
  R("vis.arrow_col", s.arrowCol); R("vis.watermark", s.watermark); R("vis.keybinds", s.keybindList); R("vis.spectators", s.spectatorList); R("vis.weapons", s.droppedWeapons);
  R("vis.bomb", s.bombEsp); R("vis.nades", s.grenadeEsp);
  R("misc.bhop", m.bhop); R("misc.autostrafe", m.autostrafe); R("misc.edgejump_key", m.edgeJumpKey); R("misc.faststop", m.fastStop); R("misc.buy_primary", m.buyPrimary);
  R("misc.buy_secondary", m.buySecondary); R("misc.buy_armor", m.buyArmor); R("misc.buy_nades", m.buyNades); R("misc.buy_kit", m.buyKit); R("misc.hitsound", m.hitsound);
  R("misc.hitsound_type", m.hitsoundType); R("misc.killsay", m.killsay); R("misc.log_hits", m.logHits); R("misc.log_hurt", m.logHurt); R("misc.log_buy", m.logPurchases);
  R("misc.clantag", m.clantag); R("misc.accent", m.accent); R("misc.menu_col", m.menuCol); R("misc.menu_key", m.menuKey); R("misc.dpi", m.dpi);
  g_NamedBinds = { { "Ragebot", &r.key }, { "Minimum damage override", &r.minDamageOverrideKey }, { "Force body aim", &r.forceBaimKey }, { "Force safe point", &r.forceSafeKey },
    { "Double tap", &r.doubletapKey }, { "Hide shots", &r.hideshotsKey }, { "Anti-aim inverter", &a.inverter }, { "Freestanding", &a.freestandKey },
    { "Manual left", &a.manualLeft }, { "Manual right", &a.manualRight }, { "Manual back", &a.manualBack }, { "Duck peek assist", &a.fakeDuckKey },
    { "Slow motion", &a.slowWalkKey }, { "Thirdperson", &s.thirdpersonKey }, { "Edge jump", &m.edgeJumpKey } };
}

static std::string ConfigPath(const std::string& n) { return "cfg/aimwhere_" + n + ".cfg"; }
// configs from before the rename (cfg/hvhsense_*.cfg) are renamed once
static void MigrateOldConfigs() {
  static bool done = false; if (done) return; done = true;
  std::error_code ec;
  for (auto& de : std::filesystem::directory_iterator("cfg", ec)) {
    std::string fn = de.path().filename().string();
    if (fn.rfind("hvhsense_", 0) != 0) continue;
    std::filesystem::path to = de.path().parent_path() / ("aimwhere_" + fn.substr(9));
    if (!std::filesystem::exists(to, ec)) std::filesystem::rename(de.path(), to, ec);
  }
}
void Config_Save(const std::string& name) {
  std::error_code ec; std::filesystem::create_directories("cfg", ec);
  std::ofstream f(ConfigPath(name));
  for (auto& e : s_registry) {
    f << e.name << " ";
    switch (e.type) {
      case VT_BOOL: f << (*(bool*)e.ptr ? 1 : 0); break;
      case VT_INT: f << *(int*)e.ptr; break;
      case VT_FLOAT: f << *(float*)e.ptr; break;
      case VT_COL: { Col c = *(Col*)e.ptr; f << (int)c.r << " " << (int)c.g << " " << (int)c.b << " " << (int)c.a; break; }
      case VT_KEY: { KeyBind k = *(KeyBind*)e.ptr; f << k.key << " " << k.mode << " " << (k.toggled ? 1 : 0); break; }
    }
    f << "\n";
  }
  Log(v.misc.accent, "Saved config \"%s\"", name.c_str());
}
bool Config_Load(const std::string& name) {
  MigrateOldConfigs();
  std::ifstream f(ConfigPath(name));
  if (!f) return false;
  std::map<std::string, VarEntry*> idx; for (auto& e : s_registry) idx[e.name] = &e;
  std::string line;
  while (std::getline(f, line)) {
    std::istringstream ss(line); std::string k; ss >> k;
    auto it = idx.find(k); if (it == idx.end()) continue;
    VarEntry& e = *it->second;
    switch (e.type) {
      case VT_BOOL: { int x; if (ss >> x) *(bool*)e.ptr = x != 0; break; }
      case VT_INT: ss >> *(int*)e.ptr; break;
      case VT_FLOAT: ss >> *(float*)e.ptr; break;
      case VT_COL: { int r, gg, b, a; if (ss >> r >> gg >> b >> a) *(Col*)e.ptr = MkCol(r, gg, b, a); break; }
      case VT_KEY: { KeyBind& kb = *(KeyBind*)e.ptr; int t = 0; ss >> kb.key >> kb.mode >> t; kb.toggled = t != 0; break; }
    }
  }
  return true;
}
std::vector<std::string> Config_List() {
  MigrateOldConfigs();
  std::vector<std::string> out = { "default", "rage", "hvh 1", "hvh 2", "secret" };
  std::error_code ec;
  for (auto& de : std::filesystem::directory_iterator("cfg", ec)) {
    std::string fn = de.path().filename().string();
    if (fn.rfind("aimwhere_", 0) == 0 && fn.size() > 13 && fn.substr(fn.size() - 4) == ".cfg") {
      std::string n = fn.substr(9, fn.size() - 13);
      if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
  }
  return out;
}

// ------------------------------------------------------------------------------------------------
// event log (top-left)
struct LogLine { std::string text; Col col; float time; };
static std::deque<LogLine> s_logs;
void Log(Col c, const char* fmt, ...) {
  char buf[512]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof(buf), fmt, ap); va_end(ap);
  s_logs.push_back({ buf, c, (float)GetTime() });
  while (s_logs.size() > 10) s_logs.pop_front();
  ConColorMsg(Color32{ c.r, c.g, c.b, 255 }, "[aimwhere] ");
  ConColorMsg(Color32{ 220, 220, 220, 255 }, "%s\n", buf);
}
void DrawLogs() {
  float y = 5, now = (float)GetTime();
  for (auto it = s_logs.begin(); it != s_logs.end();) {
    float age = now - it->time;
    if (age > 6.f) { it = s_logs.erase(it); continue; }
    float a = age > 5.5f ? (6.f - age) / 0.5f : 1.f;
    float x = 6 - (age > 5.5f ? (1 - a) * 30 : 0);
    surface()->DrawTextOutline(FONT_CHEAT, "[aimwhere]", x, y, it->col.Alpha((int)(255 * a)), { 0, 0, 0, (unsigned char)(200 * a) });
    surface()->DrawTextOutline(FONT_CHEAT, it->text.c_str(), x + surface()->GetTextSize(FONT_CHEAT, "[aimwhere] ").x, y, { 230, 230, 230, (unsigned char)(255 * a) }, { 0, 0, 0, (unsigned char)(200 * a) });
    y += 13; ++it;
  }
}

// ------------------------------------------------------------------------------------------------
// immediate mode gui - aimware style (light grey window, white group boxes, labels left / controls right, red accents)
namespace gui {
static float S = 1.f;                    // dpi scale
static Vector2 mouse; static bool mDown, mPressed, mReleased, mRPressed; static float wheel;
static bool inputBlocked = false;        // a popup is open: widgets below don't get input
static int activeId = 0;                 // slider being dragged
static int openPopup = 0;                // combo / keybind / colour picker popup id
static int captureKeyId = 0;             // keybind waiting for a key
static std::vector<std::function<void()>> deferred;  // popups drawn last
// group cursor
static float gx, gy, gw, gh, cy, gTop, gContent; static int gId; static std::map<int, float> scrolls;
static int idSeed = 0;

// palette
static Col BgC() { return MkCol(236, 236, 236); }
static Col GroupC() { return MkCol(250, 250, 250); }
static Col BorderC() { return MkCol(168, 168, 168); }
static Col FieldBorder() { return MkCol(140, 140, 140); }
static Col TextC() { return MkCol(28, 28, 28); }
static Col TextDim() { return MkCol(115, 115, 115); }
static Col Accent() { return v.misc.menuCol; }
static Col AccentDark() { Col a = v.misc.menuCol; return MkCol(a.r * 6 / 10, a.g * 6 / 10, a.b * 6 / 10); }

static bool Hover(float x, float y, float w, float h) { return mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h; }
static bool HoverClipped(float x, float y, float w, float h) { return !inputBlocked && Hover(x, y, w, h) && mouse.y >= gTop && mouse.y < gTop + gh - 4; }
static int MakeId(const char* label) { unsigned h = 2166136261u + idSeed * 16777619u; for (const char* p = label; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; } return (int)(h & 0x7fffffff) | 1; }
static void Rect(float x, float y, float w, float h, Col c) { surface()->DrawFilledRect(x, y, w, h, c); }
static void Outline(float x, float y, float w, float h, Col c) { surface()->DrawOutlinedRect(x, y, w, h, c); }
static void GradV(float x, float y, float w, float h, Col a, Col b) { surface()->DrawGradientV(x, y, w, h, a, b); }
static void Text(float x, float y, const char* t, Col c = TextC(), int font = FONT_CHEAT) { surface()->DrawText(font, t, x, y, c); }
static float TextW(const char* t, int font = FONT_CHEAT) { return surface()->GetTextSize(font, t).x; }
// text shortened with "..." to fit maxW
static std::string Fit(const char* t, float maxW, int font = FONT_CHEAT) {
  std::string s = t; if (TextW(s.c_str(), font) <= maxW) return s;
  while (s.size() > 1 && TextW((s + "...").c_str(), font) > maxW) s.pop_back();
  return s + "...";
}

static void BeginGroup(const char* title, float x, float y, float w, float h) {
  gId = MakeId(title); gx = x; gy = y; gw = w; gh = h;
  Rect(x, y, w, h, GroupC());
  Outline(x, y, w, h, BorderC());
  // title cut into the top border
  float tw = TextW(title, FONT_CHEAT_BOLD);
  Rect(x + 10 * S, y - 1, tw + 8 * S, 3, GroupC());
  Text(x + 14 * S, y - 7 * S, title, MkCol(20, 20, 20), FONT_CHEAT_BOLD);
  float& sc = scrolls[gId];
  gTop = y + 4;
  surface()->PushClip((int)x + 2, (int)y + 4, (int)w - 4, (int)h - 6);
  cy = y + 16 * S - sc;
  gContent = cy;
}
static void EndGroup() {
  float& sc = scrolls[gId];
  float content = cy + sc - (gy + 16 * S) + 8;
  float view = gh - 20;
  if (!inputBlocked && Hover(gx, gy, gw, gh) && wheel != 0) sc -= wheel * 30 * S;
  sc = Clamp(sc, 0.f, std::max(0.f, content - view));
  surface()->PopClip();
  if (content > view) { // scrollbar
    float bh = std::max(20.f, view * view / content), by = gy + 6 + (view - bh) * (sc / std::max(1.f, content - view));
    Rect(gx + gw - 7, gy + 6, 4, view, MkCol(228, 228, 228));
    Rect(gx + gw - 7, by, 4, bh, MkCol(170, 170, 170));
  }
}
// two column layout inside a group: label at LX, control at CX (width CW)
static float LX() { return gx + 14 * S; }
static float CX() { return gx + std::max(118 * S, gw * 0.47f); }
static float CW() { return gx + gw - 16 * S - CX(); }
static float WX() { return LX(); }
static float WW() { return gw - 30 * S; }
static float s_lastLineY = 0;   // y of the last widget line (for attached keybind/colour)
static bool s_lastLineHasControl = false;
static float s_lastLineRight = 0; // free space right edge on the last line (attached widgets stack leftwards)

static void RowLabel(const char* label, float y, bool enabled = true) {
  std::string t = Fit(label, CX() - LX() - 6 * S, FONT_CHEAT_BOLD);
  Text(LX(), y, t.c_str(), enabled ? TextC() : TextDim(), FONT_CHEAT_BOLD);
}
static void Field(float x, float y, float w, float h, bool hv, bool focus = false) {
  Rect(x, y, w, h, hv ? MkCol(255, 255, 255) : MkCol(252, 252, 252));
  Outline(x, y, w, h, focus ? Accent() : (hv ? MkCol(110, 110, 110) : FieldBorder()));
}

static bool Checkbox(const char* label, bool& val, bool enabled = true) {
  float y = cy, bs = 12 * S, x = CX();
  bool hv = enabled && HoverClipped(LX(), y - 2, gw - 28 * S, 18 * S);
  if (hv && mPressed) { val = !val; EmitSound2D("UI.Click", 0.3f); }
  RowLabel(label, y, enabled);
  Rect(x, y, bs, bs, MkCol(255, 255, 255));
  Outline(x, y, bs, bs, hv ? MkCol(90, 90, 90) : FieldBorder());
  if (val) { Rect(x + 2, y + 2, bs - 4, bs - 4, Accent()); Outline(x + 2, y + 2, bs - 4, bs - 4, AccentDark()); }
  s_lastLineY = y; s_lastLineHasControl = true; s_lastLineRight = gx + gw - 16 * S; cy += 24 * S;
  return val;
}
static bool SliderF(const char* label, float& val, float mn, float mx, const char* fmt = "%.0f", float step = 0, const char* zeroText = nullptr) {
  int id = MakeId(label[0] ? label : TextFormat("slider%p", (void*)&val));
  float y = cy;
  if (label[0]) RowLabel(label, y);
  float x = CX(), w = CW(), ty = y + 3 * S, th = 5 * S;
  bool hv = HoverClipped(x - 4, y - 2, w + 8, 16 * S);
  if (hv && mPressed) activeId = id;
  bool changed = false;
  if (activeId == id) {
    if (!mDown) activeId = 0;
    else { float t = Clamp((mouse.x - x) / w, 0.f, 1.f); float nv = mn + (mx - mn) * t; if (step > 0) nv = std::round(nv / step) * step; if (nv != val) { val = nv; changed = true; } }
  }
  if (hv && !mDown) {
    if (IsKeyPressed(KEY_LEFT)) { val = std::max(mn, val - (step > 0 ? step : 1)); changed = true; }
    if (IsKeyPressed(KEY_RIGHT)) { val = std::min(mx, val + (step > 0 ? step : 1)); changed = true; }
  }
  GradV(x, ty, w, th, MkCol(214, 214, 214), MkCol(236, 236, 236));
  Outline(x, ty, w, th, MkCol(150, 150, 150));
  float t = Clamp((val - mn) / std::max(0.0001f, mx - mn), 0.f, 1.f);
  float hx = x + w * t - 2.5f * S;
  Rect(hx, y, 5 * S, 11 * S, Accent()); Outline(hx, y, 5 * S, 11 * S, AccentDark());
  char buf[64];
  if (zeroText && val <= mn + 0.0001f) snprintf(buf, sizeof(buf), "%s", zeroText); else snprintf(buf, sizeof(buf), fmt, val);
  float tw = TextW(buf, FONT_CHEAT_BOLD);
  Text(x + (w - tw) * 0.5f, y + 12 * S, buf, MkCol(20, 20, 20), FONT_CHEAT_BOLD);
  s_lastLineY = y; s_lastLineHasControl = true; s_lastLineRight = gx + gw - 16 * S; cy += 30 * S;
  return changed;
}
static void ComboArrow(float x, float y, float w, bool open) {
  Col c = open ? Accent() : MkCol(70, 70, 70);
  surface()->DrawTriangle({ x + w - 12 * S, y + 7 * S }, { x + w - 5 * S, y + 7 * S }, { x + w - 8.5f * S, y + 11 * S }, c);
}
static bool Combo(const char* label, int& sel, const std::vector<const char*>& items) {
  int id = MakeId(label);
  float y = cy;
  RowLabel(label, y);
  float x = CX(), w = CW(), h = 17 * S, fy = y - 2 * S;
  bool hv = HoverClipped(x, fy, w, h);
  if (hv && mPressed) { openPopup = openPopup == id ? 0 : id; EmitSound2D("UI.Click", 0.3f); }
  Field(x, fy, w, h, hv, openPopup == id);
  sel = Clamp(sel, 0, (int)items.size() - 1);
  Text(x + 5 * S, fy + 2 * S, Fit(items[sel], w - 22 * S).c_str(), TextC());
  ComboArrow(x, fy, w, openPopup == id);
  if (openPopup == id) {
    float py = fy + h; int* psel = &sel; std::vector<const char*> its = items;
    deferred.push_back([=]() {
      float ih = 18 * S, ph = ih * its.size();
      Rect(x, py, w, ph, MkCol(255, 255, 255)); Outline(x, py, w, ph, MkCol(110, 110, 110));
      for (size_t i = 0; i < its.size(); i++) {
        bool ih_ = Hover(x, py + i * ih, w, ih);
        if (ih_) Rect(x + 1, py + i * ih, w - 2, ih, MkCol(232, 232, 232));
        Text(x + 5 * S, py + i * ih + 2 * S, its[i], (int)i == *psel ? Accent() : TextC(), (int)i == *psel ? FONT_CHEAT_BOLD : FONT_CHEAT);
        if (ih_ && mPressed) { *psel = (int)i; openPopup = 0; }
      }
      if (mPressed && !Hover(x, py - h - 2, w, ph + h + 2)) openPopup = 0;
    });
  }
  s_lastLineY = y; s_lastLineHasControl = true; s_lastLineRight = gx + gw - 16 * S; cy += 24 * S;
  return false;
}
static void MultiCombo(const char* label, int& mask, const std::vector<const char*>& items) {
  int id = MakeId(label);
  float y = cy;
  RowLabel(label, y);
  float x = CX(), w = CW(), h = 17 * S, fy = y - 2 * S;
  bool hv = HoverClipped(x, fy, w, h);
  if (hv && mPressed) openPopup = openPopup == id ? 0 : id;
  Field(x, fy, w, h, hv, openPopup == id);
  std::string txt;
  for (size_t i = 0; i < items.size(); i++) if (mask & (1 << i)) { if (!txt.empty()) txt += ", "; txt += items[i]; }
  if (txt.empty()) txt = "-";
  Text(x + 5 * S, fy + 2 * S, Fit(txt.c_str(), w - 22 * S).c_str());
  ComboArrow(x, fy, w, openPopup == id);
  if (openPopup == id) {
    float py = fy + h; int* pm = &mask; std::vector<const char*> its = items;
    deferred.push_back([=]() {
      float ih = 18 * S, ph = ih * its.size();
      Rect(x, py, w, ph, MkCol(255, 255, 255)); Outline(x, py, w, ph, MkCol(110, 110, 110));
      for (size_t i = 0; i < its.size(); i++) {
        bool on = (*pm >> i) & 1, ih_ = Hover(x, py + i * ih, w, ih);
        if (ih_) Rect(x + 1, py + i * ih, w - 2, ih, MkCol(232, 232, 232));
        // small checkbox per item
        float bx = x + 5 * S, by = py + i * ih + 3 * S, bs = 11 * S;
        Rect(bx, by, bs, bs, MkCol(255, 255, 255)); Outline(bx, by, bs, bs, FieldBorder());
        if (on) { Rect(bx + 2, by + 2, bs - 4, bs - 4, Accent()); }
        Text(bx + bs + 6 * S, py + i * ih + 2 * S, its[i], on ? Accent() : TextC(), on ? FONT_CHEAT_BOLD : FONT_CHEAT);
        if (ih_ && mPressed) *pm ^= (1 << i);
      }
      if (mPressed && !Hover(x, py - h - 2, w, ph + h + 2)) openPopup = 0;
    });
  }
  s_lastLineY = y; s_lastLineHasControl = true; s_lastLineRight = gx + gw - 16 * S; cy += 24 * S;
}
static bool ButtonW(const char* label, float wOverride = 0) {
  float x = LX(), y = cy, w = wOverride > 0 ? wOverride : WW(), h = 22 * S;
  bool hv = HoverClipped(x, y, w, h);
  GradV(x, y, w, h, hv ? (mDown ? MkCol(205, 205, 205) : MkCol(252, 252, 252)) : MkCol(244, 244, 244), hv && mDown ? MkCol(225, 225, 225) : MkCol(212, 212, 212));
  Outline(x, y, w, h, hv ? MkCol(100, 100, 100) : MkCol(140, 140, 140));
  float tw = TextW(label, FONT_CHEAT_BOLD);
  Text(x + (w - tw) * 0.5f, y + 4 * S, label, TextC(), FONT_CHEAT_BOLD);
  s_lastLineHasControl = true;
  cy += h + 7 * S;
  bool clicked = hv && mPressed;
  if (clicked) EmitSound2D("UI.Click", 0.4f);
  return clicked;
}
static void LabelW(const char* t, Col c = TextC()) {
  std::string s = Fit(t, gw - 30 * S, FONT_CHEAT_BOLD);
  Text(LX(), cy, s.c_str(), c, FONT_CHEAT_BOLD);
  s_lastLineY = cy; s_lastLineHasControl = false; s_lastLineRight = gx + gw - 16 * S; cy += 24 * S;
}

// key bind attached to the previous line: a text field in the control column for label rows (like aimware's "On Key"),
// a small field at the right end of rows that already have a control
static void KeyBindW(const char* idLabel, KeyBind& kb) {
  int id = MakeId(idLabel);
  char buf[48];
  if (captureKeyId == id) snprintf(buf, sizeof(buf), "press a key...");
  else if (kb.mode == KEYMODE_ALWAYS) snprintf(buf, sizeof(buf), "always");
  else if (!kb.key) snprintf(buf, sizeof(buf), TouchUI() ? "+ circle" : "-");
  else if (kb.key >= K_TOUCHBIND) snprintf(buf, sizeof(buf), kb.mode == KEYMODE_TOGGLE ? "circle: toggle" : kb.mode == KEYMODE_HOLD ? "circle: hold" : "circle");
  else { std::string k = Key_KeynumToString(kb.key); for (auto& ch : k) ch = (char)tolower(ch); snprintf(buf, sizeof(buf), "%s", k.c_str()); }
  float x, w, y = s_lastLineY - 2 * S, h = 17 * S;
  if (!s_lastLineHasControl) { x = CX(); w = CW(); }
  else { w = 58 * S; x = s_lastLineRight - w; s_lastLineRight = x - 4 * S; }
  bool hv = HoverClipped(x, y, w, h);
  Field(x, y, w, h, hv, captureKeyId == id);
  Text(x + 5 * S, y + 2 * S, Fit(buf, w - 8 * S).c_str(), captureKeyId == id ? Accent() : TextC());
  // phones: no keyboard -> tapping the field puts the bind on screen as a circle (off -> toggle circle -> hold circle -> off)
  int touchSlot = -1;
  for (size_t i = 0; i < g_NamedBinds.size(); i++) if (g_NamedBinds[i].bind == &kb) touchSlot = (int)i;
  if (TouchUI() && touchSlot >= 0) {
    if (hv && mPressed) {
      if (kb.key != K_TOUCHBIND + touchSlot || kb.mode == KEYMODE_ALWAYS) { kb.key = K_TOUCHBIND + touchSlot; kb.mode = KEYMODE_TOGGLE; kb.toggled = false; }
      else if (kb.mode == KEYMODE_TOGGLE) kb.mode = KEYMODE_HOLD;
      else { kb.key = 0; kb.mode = KEYMODE_HOLD; kb.toggled = false; }
    }
    if (hv && mRPressed) openPopup = id + 1;
  } else
  if (hv && mPressed && captureKeyId != id) { captureKeyId = id; return; }
  if (hv && mRPressed) openPopup = id + 1;
  if (captureKeyId == id) {
    int key = vgui::Input().keys.empty() ? 0 : vgui::Input().keys.front();
    if (key) vgui::ConsumeKey(key);
    static const int mb[5] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_SIDE, MOUSE_BUTTON_EXTRA };
    for (int i = 0; i < 5; i++) if (IsMouseButtonPressed(mb[i]) && !(i == 0 && hv)) key = K_MOUSE1 + i;
    if (key == KEY_ESCAPE) { kb.key = 0; captureKeyId = 0; }
    else if (key) { kb.key = key; if (kb.mode == KEYMODE_ALWAYS) kb.mode = KEYMODE_HOLD; captureKeyId = 0; }
  }
  if (openPopup == id + 1) {
    KeyBind* pk = &kb; float px = x, py = y + h;
    deferred.push_back([=]() {
      const char* modes[4] = { "Always on", "On hotkey", "Toggle", "Off hotkey" };
      float ih = 18 * S, w2 = 96 * S;
      Rect(px, py, w2, ih * 4, MkCol(255, 255, 255)); Outline(px, py, w2, ih * 4, MkCol(110, 110, 110));
      for (int i = 0; i < 4; i++) {
        bool hh = Hover(px, py + i * ih, w2, ih);
        if (hh) Rect(px + 1, py + i * ih, w2 - 2, ih, MkCol(232, 232, 232));
        Text(px + 6, py + i * ih + 2 * S, modes[i], pk->mode == i ? Accent() : TextC(), pk->mode == i ? FONT_CHEAT_BOLD : FONT_CHEAT);
        if (hh && mPressed) { pk->mode = i; openPopup = 0; }
      }
      if (mPressed && !Hover(px, py, w2, ih * 4)) openPopup = 0;
    });
  }
}

// colour swatch attached to the previous line (opens an HSV picker)
static void ColorToHSV(Col c, float& h, float& s, float& vv) { Vector3 hsv = ColorToHSV(Color{ c.r, c.g, c.b, 255 }); h = hsv.x; s = hsv.y; vv = hsv.z; }
static void ColorPickerW(const char* idLabel, Col& col, float rightOffset = 0) {
  int id = MakeId(idLabel);
  float w = 22 * S, h = 12 * S, x = s_lastLineRight - w - rightOffset, y = s_lastLineY;
  s_lastLineRight = x - 4 * S;
  bool hv = HoverClipped(x - 2, y - 2, w + 4, h + 4);
  // checker behind (shows alpha)
  Rect(x, y, w, h, MkCol(255, 255, 255)); Rect(x, y, w / 2, h / 2, MkCol(200, 200, 200)); Rect(x + w / 2, y + h / 2, w / 2, h / 2, MkCol(200, 200, 200));
  Rect(x, y, w, h, col);
  Outline(x, y, w, h, hv ? MkCol(60, 60, 60) : FieldBorder());
  if (hv && mPressed) openPopup = openPopup == id ? 0 : id;
  if (openPopup == id) {
    Col* pc = &col; float px = x + w + 6, py = y;
    deferred.push_back([=]() {
      static float hue = 0, sat = 0, val = 0; static int lastId = 0;
      if (lastId != id) { ColorToHSV(*pc, hue, sat, val); lastId = id; }
      float sz = 150 * S, pw = sz + 36 * S, ph = sz + 24 * S;
      float qx = std::min(px, (float)GetScreenWidth() - pw - 4);
      Rect(qx, py, pw, ph, MkCol(248, 248, 248)); Outline(qx, py, pw, ph, MkCol(110, 110, 110));
      float sx = qx + 6, sy = py + 6;
      Color hc = ColorFromHSV(hue, 1, 1);
      surface()->DrawGradientH(sx, sy, sz, sz, { 255, 255, 255, 255 }, hc);
      surface()->DrawGradientV(sx, sy, sz, sz, { 0, 0, 0, 0 }, { 0, 0, 0, 255 });
      Outline(sx - 1, sy - 1, sz + 2, sz + 2, MkCol(110, 110, 110));
      static int drag = 0;
      if (mPressed && Hover(sx, sy, sz, sz)) drag = 1;
      if (mPressed && Hover(sx + sz + 6, sy, 14 * S, sz)) drag = 2;
      if (mPressed && Hover(sx, sy + sz + 5, sz, 9 * S)) drag = 3;
      if (!mDown) drag = 0;
      if (drag == 1) { sat = Clamp((mouse.x - sx) / sz, 0.f, 1.f); val = 1 - Clamp((mouse.y - sy) / sz, 0.f, 1.f); }
      if (drag == 2) hue = Clamp((mouse.y - sy) / sz, 0.f, 0.999f) * 360.f;
      if (drag == 3) pc->a = (unsigned char)(Clamp((mouse.x - sx) / sz, 0.f, 1.f) * 255);
      if (drag) { Color c = ColorFromHSV(hue, sat, val); pc->r = c.r; pc->g = c.g; pc->b = c.b; }
      surface()->DrawOutlinedRect(sx + sat * sz - 2, sy + (1 - val) * sz - 2, 4, 4, { 255, 255, 255, 255 });
      float hx = sx + sz + 6;
      for (int i = 0; i < 6; i++) surface()->DrawGradientV(hx, sy + i * sz / 6, 14 * S, sz / 6 + 1, ColorFromHSV(i * 60.f, 1, 1), ColorFromHSV((i + 1) * 60.f, 1, 1));
      Rect(hx - 1, sy + hue / 360.f * sz - 1, 14 * S + 2, 3, MkCol(20, 20, 20));
      surface()->DrawGradientH(sx, sy + sz + 5, sz, 9 * S, { pc->r, pc->g, pc->b, 0 }, { pc->r, pc->g, pc->b, 255 });
      Outline(sx - 1, sy + sz + 4, sz + 2, 9 * S + 2, MkCol(110, 110, 110));
      Rect(sx + pc->a / 255.f * sz - 1, sy + sz + 4, 3, 9 * S + 2, MkCol(20, 20, 20));
      if (mPressed && !Hover(qx, py, pw, ph) && !drag) { openPopup = 0; lastId = 0; }
    });
  }
}

static bool ListBox(const char* idLabel, const std::vector<std::string>& items, int& sel, float h) {
  float x = LX(), y = cy, w = WW();
  Rect(x, y, w, h, MkCol(255, 255, 255)); Outline(x, y, w, h, FieldBorder());
  bool changed = false; float ih = 19 * S;
  surface()->PushClip((int)x + 1, (int)std::max(y + 1, gTop), (int)w - 2, (int)h - 2);
  for (size_t i = 0; i < items.size(); i++) {
    float iy = y + 1 + i * ih; if (iy > y + h) break;
    bool hv = HoverClipped(x, iy, w, ih) && Hover(x, y, w, h);
    if ((int)i == sel) Rect(x + 1, iy, w - 2, ih, Accent());
    else if (hv) Rect(x + 1, iy, w - 2, ih, MkCol(234, 234, 234));
    Text(x + 6 * S, iy + 2 * S, items[i].c_str(), (int)i == sel ? MkCol(255, 255, 255) : TextC(), (int)i == sel ? FONT_CHEAT_BOLD : FONT_CHEAT);
    if (hv && mPressed) { sel = (int)i; changed = true; }
  }
  surface()->PopClip();
  (void)idLabel;
  cy += h + 8 * S;
  return changed;
}
static bool g_cheatTextFocus = false;
static bool TextBox(const char* idLabel, std::string& txt) {
  int id = MakeId(idLabel);
  static int focus = 0;
  float x = LX(), y = cy, w = WW(), h = 19 * S;
  bool hv = HoverClipped(x, y, w, h);
  if (mPressed) focus = hv ? id : (focus == id ? 0 : focus);
  Field(x, y, w, h, hv, focus == id);
  bool enter = false;
  if (focus == id) {
    for (int c : vgui::Input().chars) if (c >= 32 && c < 127 && txt.size() < 24) txt += (char)c;
    vgui::ConsumeChars();
    if (vgui::Input().KeyPressed(KEY_BACKSPACE) && !txt.empty()) txt.pop_back();
    if (vgui::Input().KeyPressed(KEY_ENTER)) { enter = true; focus = 0; }
    vgui::ConsumeKey(KEY_BACKSPACE); vgui::ConsumeKey(KEY_ENTER);
    g_cheatTextFocus = true;
  }
  std::string shown = txt + ((focus == id && std::fmod(GetTime(), 1.0) < 0.5) ? "_" : "");
  Text(x + 5 * S, y + 3 * S, shown.c_str());
  cy += h + 8 * S;
  return enter;
}
} // namespace gui

static bool s_capturingAny() { return gui::captureKeyId != 0 || gui::g_cheatTextFocus; }
bool Menu_IsCapturingKey() { return s_capturingAny(); }

// ------------------------------------------------------------------------------------------------
static float s_mx = 120, s_my = 90;
static int s_tab = 0;
static bool s_drag = false; static Vector2 s_dragOff;
static int s_cfgSel = 0; static std::string s_cfgName = "my config";
static int s_playerSel = 0;
static float s_openAnim = 0;

static void PaintRage(float x, float y, float w, float h) {
  using namespace gui;
  float cw = (w - 30 * S) / 2;
  auto& r = v.rage;
  BeginGroup("Aimbot", x + 10 * S, y, cw, h);
  Checkbox("Enabled", r.enabled); KeyBindW("rage.key", r.key);
  Combo("Target selection", r.target, { "Cycle", "Near crosshair", "Lowest distance", "Lowest health" });
  MultiCombo("Target hitbox", r.hitboxes, { "Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs", "Feet" });
  MultiCombo("Multi-point", r.multipoint, { "Head", "Neck", "Chest", "Stomach", "Pelvis", "Arms", "Legs", "Feet" });
  SliderF("Head point scale", r.headScale, 0, 100, "%.0f%%");
  SliderF("Body point scale", r.bodyScale, 0, 100, "%.0f%%");
  Checkbox("Prefer safe point", r.safePoint);
  LabelW("Force safe point"); KeyBindW("rage.safekey", r.forceSafeKey);
  Checkbox("Automatic fire", r.autoFire);
  Checkbox("Automatic penetration", r.autowall);
  Checkbox("Silent aim", r.silent);
  SliderF("Minimum hit chance", r.hitchance, 0, 100, "%.0f%%", 1, "Off");
  SliderF("Minimum damage", r.minDamage, 0, 126, "%.0f", 1, "Auto");
  if (r.minDamage > 100) { char b[32]; snprintf(b, sizeof(b), "    HP + %d", (int)r.minDamage - 100); LabelW(b, MkCol(140, 140, 140)); }
  LabelW("Minimum damage override"); KeyBindW("rage.mdokey", r.minDamageOverrideKey);
  SliderF("", r.minDamageOverride, 0, 126, "%.0f", 1);
  Checkbox("Automatic scope", r.autoscope);
  SliderF("Maximum FOV", r.fov, 1, 180, "%.0f°");
  EndGroup();
  BeginGroup("Other", x + 20 * S + cw, y, cw, h);
  Checkbox("Remove recoil", r.removeRecoil);
  Checkbox("Compensate spread (nospread)", r.compensateSpread);
  Combo("Accuracy boost", r.quickStop, { "Off", "Quick stop", "Slide stop" });
  Checkbox("Quick stop between shots", r.quickStopBetweenShots);
  Checkbox("Delay shot on unduck", r.delayShot);
  Checkbox("Backtrack (lag compensation)", r.backtrack);
  Combo("Prefer body aim", r.preferBaim, { "Off", "Lethal", "Always", "After 2 misses" });
  LabelW("Force body aim"); KeyBindW("rage.baimkey", r.forceBaimKey);
  Combo("Anti-aim correction", r.resolver, { "Off", "Default", "Bruteforce" });
  Checkbox("Double tap", r.doubletap); KeyBindW("rage.dtkey", r.doubletapKey);
  Combo("Double tap mode", r.doubletapMode, { "Offensive", "Defensive" });
  Checkbox("On shot anti-aim", r.hideshots); KeyBindW("rage.hskey", r.hideshotsKey);
  Checkbox("Knifebot", r.knifebot);
  Checkbox("Log misses due to spread", r.logMisses);
  EndGroup();
}

static void PaintAA(float x, float y, float w, float h) {
  using namespace gui;
  float cw = (w - 30 * S) / 2;
  auto& a = v.aa;
  BeginGroup("Anti-aimbot angles", x + 10 * S, y, cw, h);
  Checkbox("Enabled", a.enabled);
  Combo("Pitch", a.pitch, { "Off", "Down", "Up", "Zero", "Random", "Minimal" });
  Combo("Yaw base", a.yawBase, { "Local view", "At targets" });
  Combo("Yaw", a.yaw, { "Off", "180", "Spin", "Static", "180 Z" });
  if (a.yaw == 2) SliderF("Spin speed", a.spinSpeed, 1, 60, "%.0f");
  SliderF("Yaw add", a.yawAdd, -180, 180, "%.0f°", 1);
  Combo("Yaw jitter", a.jitter, { "Off", "Offset", "Center", "Random" });
  if (a.jitter) SliderF("Jitter range", a.jitterRange, 0, 90, "%.0f°", 1);
  Combo("Body yaw", a.bodyYaw, { "Off", "Static", "Jitter", "Opposite" });
  LabelW("Body yaw inverter"); KeyBindW("aa.inv", a.inverter);
  SliderF("Fake yaw limit", a.fakeLimit, 0, 58, "%.0f°", 1);
  Checkbox("Freestanding body yaw", a.freestandBody);
  Combo("Lower body yaw target", a.lbyMode, { "Off", "Opposite", "Sway" });
  Checkbox("Edge yaw", a.edgeYaw);
  LabelW("Freestanding"); KeyBindW("aa.fs", a.freestandKey);
  LabelW("Manual left"); KeyBindW("aa.ml", a.manualLeft);
  LabelW("Manual right"); KeyBindW("aa.mr", a.manualRight);
  LabelW("Manual back"); KeyBindW("aa.mb", a.manualBack);
  EndGroup();
  float h1 = h * 0.55f;
  BeginGroup("Fake lag", x + 20 * S + cw, y, cw, h1);
  Checkbox("Enabled", a.fakelag);
  Combo("Amount", a.fakelagMode, { "Dynamic", "Maximum", "Fluctuate" });
  SliderF("Variance", a.fakelagVariance, 0, 100, "%.0f%%", 1);
  SliderF("Limit", a.fakelagLimit, 1, 14, "%.0f", 1);
  Checkbox("Fake lag while shooting", a.fakelagOnShot);
  EndGroup();
  BeginGroup("Other", x + 20 * S + cw, y + h1 + 16 * S, cw, h - h1 - 16 * S);
  LabelW("Slow motion"); KeyBindW("aa.sw", a.slowWalkKey);
  SliderF("Slow motion speed", a.slowWalkSpeed, 5, 100, "%.0f%%", 1);
  LabelW("Duck peek assist"); KeyBindW("aa.fd", a.fakeDuckKey);
  Checkbox("On shot anti-aim", a.onShotAA);
  EndGroup();
}

static void PaintVisuals(float x, float y, float w, float h) {
  using namespace gui;
  float cw = (w - 30 * S) / 2;
  auto& s = v.vis;
  float h1 = h * 0.5f;
  BeginGroup("Player ESP", x + 10 * S, y, cw, h1);
  Checkbox("Teammates", s.teammates);
  Checkbox("Bounding box", s.box); ColorPickerW("box", s.boxCol);
  Checkbox("Health bar", s.health);
  Checkbox("Name", s.name); ColorPickerW("name", s.nameCol);
  Checkbox("Flags", s.flags);
  Checkbox("Weapon text", s.weapon);
  Checkbox("Ammo", s.ammo); ColorPickerW("ammo", s.ammoCol);
  Checkbox("Skeleton", s.skeleton); ColorPickerW("skel", s.skeletonCol);
  Checkbox("Out of FOV arrow", s.oof); ColorPickerW("oof", s.oofCol);
  if (s.oof) SliderF("Arrow distance", s.oofRadius, 60, 400, "%.0fpx", 1);
  Checkbox("Glow", s.glow); ColorPickerW("glow", s.glowCol);
  Checkbox("Visualize aimbot", s.hitboxOnShot); ColorPickerW("hbshot", s.hitboxCol);
  Checkbox("Radar", s.radarReveal);
  Checkbox("Dropped weapons", s.droppedWeapons);
  Checkbox("Grenades", s.grenadeEsp);
  Checkbox("Bomb", s.bombEsp);
  EndGroup();
  BeginGroup("Colored models", x + 10 * S, y + h1 + 16 * S, cw, h - h1 - 16 * S);
  Checkbox("Player", s.chams); ColorPickerW("chams", s.chamsCol);
  Checkbox("Player behind wall", s.chamsXqz); ColorPickerW("xqz", s.chamsXqzCol);
  Combo("Material", s.chamsMaterial, { "Default", "Solid", "Glow", "Wireframe" });
  Checkbox("Teammate", s.chamsTeam); ColorPickerW("team", s.chamsTeamCol);
  Checkbox("Backtrack", s.chamsBacktrack); ColorPickerW("bt", s.chamsBacktrackCol);
  Checkbox("Local player", s.chamsLocal); ColorPickerW("local", s.chamsLocalCol);
  Checkbox("Local player fake", s.chamsFake); ColorPickerW("fake", s.chamsFakeCol);
  SliderF("Transparency in scope", s.scopeBlend, 0, 100, "%.0f%%", 1);
  EndGroup();
  BeginGroup("Effects", x + 20 * S + cw, y, cw, h);
  Checkbox("Remove flashbang effects", s.removeFlash);
  Checkbox("Remove smoke grenades", s.removeSmoke);
  Checkbox("Remove scope overlay", s.removeScope);
  Checkbox("Visual recoil adjustment", s.removeRecoil);
  SliderF("Brightness adjustment (night mode)", s.nightmode, 0, 100, "%.0f%%", 1, "Off");
  LabelW("Force third person"); KeyBindW("tp", s.thirdpersonKey);
  SliderF("Third person distance", s.thirdpersonDist, 30, 300, "%.0f", 1);
  Checkbox("Third person when dead", s.thirdpersonDead);
  Checkbox("Force crosshair", s.forceCrosshair);
  Checkbox("Penetration reticle", s.penCrosshair);
  Checkbox("Bullet tracers", s.tracers); ColorPickerW("tracer", s.tracerCol);
  Checkbox("Bullet impacts", s.impacts);
  Checkbox("Grenade trajectory", s.grenadePrediction); ColorPickerW("nade", s.grenadeCol);
  Checkbox("Spread circle", s.spreadCircle);
  SliderF("Override FOV", s.fov, 0, 130, "%.0f°", 1, "Off");
  SliderF("Override zoom FOV", s.zoomFov, 0, 100, "%.0f%%", 1, "Off");
  SliderF("Viewmodel FOV", s.viewmodelFov, 54, 120, "%.0f", 1);
  Checkbox("Hit marker", s.hitmarker3d);
  Checkbox("Damage marker", s.damageMarker);
  Checkbox("Indicators", s.indicators);
  Checkbox("Manual anti-aim arrows", s.aaArrows); ColorPickerW("arrows", s.arrowCol);
  Checkbox("Watermark", s.watermark);
  Checkbox("Hotkey list", s.keybindList);
  Checkbox("Spectator list", s.spectatorList);
  EndGroup();
}

static void PaintMisc(float x, float y, float w, float h) {
  using namespace gui;
  float cw = (w - 30 * S) / 2;
  auto& m = v.misc;
  BeginGroup("Movement", x + 10 * S, y, cw, h * 0.42f);
  Checkbox("Bunny hop", m.bhop);
  Combo("Air strafe", m.autostrafe, { "Off", "Legit", "Directional", "Rage" });
  LabelW("Edge jump"); KeyBindW("ej", m.edgeJumpKey);
  Checkbox("Quick stop", m.fastStop);
  EndGroup();
  BeginGroup("Miscellaneous", x + 10 * S, y + h * 0.42f + 16 * S, cw, h * 0.58f - 16 * S);
  Checkbox("Hit sound", m.hitsound);
  if (m.hitsound) Combo("Hit sound type", m.hitsoundType, { "Bell (skeet)", "Click" });
  Checkbox("Kill say", m.killsay);
  Checkbox("Log damage dealt", m.logHits);
  Checkbox("Log damage received", m.logHurt);
  Checkbox("Log weapon purchases", m.logPurchases);
  EndGroup();
  BeginGroup("Buy bot", x + 20 * S + cw, y, cw, h * 0.5f);
  Combo("Primary", m.buyPrimary, { "Off", "Auto sniper", "Scout", "AWP", "Rifle" });
  Combo("Secondary", m.buySecondary, { "Off", "Deagle / R8", "Dual Berettas", "P250" });
  Checkbox("Armor", m.buyArmor);
  Checkbox("Grenades", m.buyNades);
  Checkbox("Defuse kit", m.buyKit);
  EndGroup();
  BeginGroup("Settings", x + 20 * S + cw, y + h * 0.5f + 16 * S, cw, h * 0.5f - 16 * S);
  LabelW("Menu key"); KeyBindW("menukey", m.menuKey);
  LabelW("Menu color"); ColorPickerW("menucol", m.menuCol);
  LabelW("Indicator / log color"); ColorPickerW("accent", m.accent);
  Combo("DPI scale", m.dpi, { "100%", "125%", "150%" });
  if (ButtonW("Unload")) { g.menuOpen = false; v.rage.enabled = false; v.aa.enabled = false; Log(MkCol(255, 80, 80), "unloaded (not really)"); }
  EndGroup();
}

static void PaintColors(float x, float y, float w, float h) {
  using namespace gui;
  float cw = (w - 40 * S) / 3;
  auto& s = v.vis; auto& m = v.misc;
  auto row = [](const char* label, Col& c, const char* id) { LabelW(label); ColorPickerW(id, c); };
  BeginGroup("ESP", x + 10 * S, y, cw, h);
  row("Bounding box", s.boxCol, "c.box");
  row("Name", s.nameCol, "c.name");
  row("Ammo bar", s.ammoCol, "c.ammo");
  row("Skeleton", s.skeletonCol, "c.skel");
  row("Out of FOV arrow", s.oofCol, "c.oof");
  row("Glow", s.glowCol, "c.glow");
  row("Aimbot hitboxes", s.hitboxCol, "c.hb");
  EndGroup();
  BeginGroup("Colored models", x + 20 * S + cw, y, cw, h);
  row("Player", s.chamsCol, "c.chams");
  row("Player behind wall", s.chamsXqzCol, "c.xqz");
  row("Teammate", s.chamsTeamCol, "c.team");
  row("Backtrack", s.chamsBacktrackCol, "c.bt");
  row("Local player", s.chamsLocalCol, "c.local");
  row("Local player fake", s.chamsFakeCol, "c.fake");
  EndGroup();
  BeginGroup("Other", x + 30 * S + cw * 2, y, cw, h);
  row("Bullet tracers", s.tracerCol, "c.tracer");
  row("Grenade trajectory", s.grenadeCol, "c.nade");
  row("Anti-aim arrows", s.arrowCol, "c.arrows");
  row("Indicators / logs", m.accent, "c.accent");
  row("Menu", m.menuCol, "c.menu");
  EndGroup();
}

static void PaintPlayers(float x, float y, float w, float h) {
  using namespace gui;
  float cw = (w - 30 * S) / 2;
  std::vector<std::string> names; std::vector<int> idx;
  for (CCSPlayer* p : UTIL_GetPlayers()) if (!p->IsLocalPlayer()) { names.push_back(TextFormat("%s%s", p->GetPlayerName(), p->IsEnemy(g_pLocalPlayer) ? "" : " (team)")); idx.push_back(p->entindex()); }
  BeginGroup("Players", x + 10 * S, y, cw, h);
  if (names.empty()) LabelW("No players", TextDim());
  else ListBox("plist", names, s_playerSel, h - 40 * S);
  EndGroup();
  BeginGroup("Adjustments", x + 20 * S + cw, y, cw, h);
  if (s_playerSel >= 0 && s_playerSel < (int)idx.size()) {
    int i = idx[s_playerSel]; CCSPlayer* p = UTIL_PlayerByIndex(i);
    PlayerSettings& ps = g_PlayerList[i];
    if (p) {
      LabelW(p->GetPlayerName(), Accent());
      LabelW(TextFormat("Missed shots: %d", p->m_iMissedShots), TextDim());
      LabelW(TextFormat("Resolver side: %s", p->m_iResolverSide < 0 ? "left" : p->m_iResolverSide > 0 ? "right" : "-"), TextDim());
      LabelW(TextFormat("Choked ticks: %d", p->m_LagRecords.empty() ? 0 : p->m_LagRecords.front().chokedTicks), TextDim());
    }
    Checkbox("Whitelist", ps.whitelist);
    Checkbox("High priority", ps.highPriority);
    Checkbox("Force body aim", ps.forceBaim);
    Combo("Override resolver", ps.resolverOverride, { "Off", "Force left", "Force right", "Force center", "Disable" });
    if (ButtonW("Reset missed shots") && p) p->m_iMissedShots = 0;
  } else LabelW("Select a player", TextDim());
  EndGroup();
}

static void PaintConfig(float x, float y, float w, float h) {
  using namespace gui;
  float cw = (w - 30 * S) / 2;
  static std::vector<std::string> list; static float lastList = -10;
  if (GetTime() - lastList > 1.0) { list = Config_List(); lastList = (float)GetTime(); }
  BeginGroup("Presets", x + 10 * S, y, cw, h);
  if (ListBox("cfgs", list, s_cfgSel, h - 160 * S) && s_cfgSel < (int)list.size()) s_cfgName = list[s_cfgSel];
  TextBox("cfgname", s_cfgName);
  EndGroup();
  BeginGroup("Actions", x + 20 * S + cw, y, cw, h);
  if (ButtonW("Load")) { if (Config_Load(s_cfgName)) Log(v.misc.accent, "Loaded config \"%s\"", s_cfgName.c_str()); else Log(MkCol(255, 80, 80), "Config \"%s\" not found", s_cfgName.c_str()); }
  if (ButtonW("Save")) { Config_Save(s_cfgName); lastList = -10; }
  if (ButtonW("Delete")) { std::error_code ec; std::filesystem::remove(ConfigPath(s_cfgName), ec); lastList = -10; Log(v.misc.accent, "Deleted config \"%s\"", s_cfgName.c_str()); }
  if (ButtonW("Reset")) { Vars def; v = def; Log(v.misc.accent, "Reset to default settings"); }
  if (ButtonW("Import from clipboard")) {
    const char* clip = GetClipboardText();
    if (clip) { std::error_code ec; std::filesystem::create_directories("cfg", ec); std::ofstream("cfg/aimwhere__clip.cfg") << clip; Config_Load("_clip"); Log(v.misc.accent, "Imported config from clipboard"); }
  }
  if (ButtonW("Export to clipboard")) { Config_Save("_clip"); std::ifstream f("cfg/aimwhere__clip.cfg"); std::stringstream ss; ss << f.rdbuf(); SetClipboardText(ss.str().c_str()); }
  LabelW("Configs are stored in cfg/aimwhere_<name>.cfg", TextDim());
  EndGroup();
}

void Menu_Init() { if (s_registry.empty()) RegisterVars(); Config_Load("default"); }

void Menu_Paint() {
  using namespace gui;
  bool typing = g_cheatTextFocus; g_cheatTextFocus = false;
  UpdateToggles();
  // menu key
  if (!typing && captureKeyId == 0 && RawKeyPressed(v.misc.menuKey.key ? v.misc.menuKey.key : KEY_INSERT)) { g.menuOpen = !g.menuOpen; openPopup = 0; }
  s_openAnim = Approach(g.menuOpen ? 1.f : 0.f, s_openAnim, GetFrameTime() * 8.f);
  if (s_openAnim <= 0.001f) { captureKeyId = 0; return; }
  S = v.misc.dpi == 2 ? 1.5f : v.misc.dpi == 1 ? 1.25f : 1.f;
  mouse = GetMousePosition(); mDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT); mPressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
  mReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT); mRPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT); wheel = GetMouseWheelMove();
  if (!g.menuOpen) { mPressed = mRPressed = false; mDown = false; wheel = 0; }
  float W = 780 * S, H = 600 * S;
  W = std::min(W, (float)GetScreenWidth()); H = std::min(H, (float)GetScreenHeight());
  s_mx = Clamp(s_mx, 0.f, std::max(0.f, GetScreenWidth() - W)); s_my = Clamp(s_my, 0.f, std::max(0.f, GetScreenHeight() - H));
  float x = s_mx, y = s_my;
  const float titleH = 26 * S, tabH = 26 * S, border = 3 * S;
  // close button (phones have no Insert key)
  const float cbw = 30 * S, cbx = x + W - cbw - 2 * S;
  const bool closeHv = Hover(cbx, y + 1, cbw, titleH - 2);
  if (mPressed && closeHv && openPopup == 0) { g.menuOpen = false; mPressed = false; captureKeyId = 0; }
  // dragging by the red title bar / frame
  bool overFrame = Hover(x, y, W, H);
  if (mPressed && openPopup == 0 && overFrame && (mouse.y < y + titleH || mouse.x < x + border || mouse.x > x + W - border || mouse.y > y + H - border)) { s_drag = true; s_dragOff = { mouse.x - x, mouse.y - y }; }
  if (!mDown) s_drag = false;
  if (s_drag) { s_mx = mouse.x - s_dragOff.x; s_my = mouse.y - s_dragOff.y; x = s_mx; y = s_my; }
  const Col red = v.misc.menuCol;
  auto shade = [](Col c, float k) { return MkCol((int)Clamp(c.r * k, 0.f, 255.f), (int)Clamp(c.g * k, 0.f, 255.f), (int)Clamp(c.b * k, 0.f, 255.f)); };
  // drop shadow + red frame
  Rect(x + 4, y + 4, W, H, MkCol(0, 0, 0, 70));
  GradV(x, y, W, H, shade(red, 0.95f), shade(red, 0.55f));
  Outline(x, y, W, H, shade(red, 0.35f));
  // title bar: white name field on the left (aimware), gradient red bar
  GradV(x + 1, y + 1, W - 2, titleH - 1, shade(red, 1.15f), shade(red, 0.7f));
  float fw = 250 * S;
  Rect(x + 1, y + 2 * S, fw, titleH - 5 * S, MkCol(255, 255, 255));
  Text(x + 8 * S, y + 6 * S, "AIM", MkCol(20, 20, 20), FONT_CHEAT_BOLD);
  Text(x + 8 * S + TextW("AIM", FONT_CHEAT_BOLD), y + 6 * S, "WHERE", red, FONT_CHEAT_BOLD);
  Text(x + 8 * S + TextW("AIMWHERE", FONT_CHEAT_BOLD), y + 6 * S, ".net", MkCol(130, 130, 130), FONT_CHEAT_BOLD);
  Text(x + 8 * S + TextW("AIMWHERE.net   ", FONT_CHEAT_BOLD), y + 6 * S, "counter-strike: hvh", MkCol(150, 150, 150));
  {
    const char* r = TextFormat("%s | %s", g_pLocalPlayer ? g_pLocalPlayer->GetPlayerName() : "user", "v4");
    Text(cbx - 10 * S - TextW(r), y + 6 * S, r, MkCol(255, 230, 230));
    if (closeHv) Rect(cbx, y + 2 * S, cbw, titleH - 5 * S, MkCol(255, 255, 255, 60));
    float cx = cbx + cbw * 0.5f, cy = y + titleH * 0.5f - 1 * S, k = 5.5f * S;
    surface()->DrawLine(cx - k, cy - k, cx + k, cy + k, MkCol(255, 255, 255), 2.f * S);
    surface()->DrawLine(cx - k, cy + k, cx + k, cy - k, MkCol(255, 255, 255), 2.f * S);
  }
  // black tab bar
  float tbx = x + border, tby = y + titleH, tbw = W - border * 2;
  GradV(tbx, tby, tbw, tabH, MkCol(48, 48, 48), MkCol(6, 6, 6));
  static const char* tabs[] = { "Ragebot", "Anti-aim", "Visuals", "Misc", "Colors", "Players", "Settings" };
  const int NT = 7;
  float tw = tbw / NT;
  for (int t = 0; t < NT; t++) {
    float xx = tbx + t * tw;
    bool sel = s_tab == t, hv = openPopup == 0 && !s_drag && Hover(xx, tby, tw, tabH);
    if (sel) { GradV(xx, tby, tw, tabH, MkCol(122, 122, 122), MkCol(66, 66, 66)); Rect(xx, tby, tw, 1, MkCol(160, 160, 160)); }
    else if (hv) GradV(xx, tby, tw, tabH, MkCol(70, 70, 70), MkCol(24, 24, 24));
    float lw = TextW(tabs[t], FONT_CHEAT_BOLD);
    Text(xx + (tw - lw) * 0.5f, tby + (tabH - 12 * S) * 0.5f, tabs[t], sel || hv ? MkCol(255, 255, 255) : MkCol(215, 215, 215), FONT_CHEAT_BOLD);
    if (hv && mPressed) { s_tab = t; openPopup = 0; EmitSound2D("UI.Click", 0.4f); }
  }
  // light grey content area
  float ax = x + border, ay = tby + tabH, aw = W - border * 2, ah = y + H - border - ay;
  Rect(ax, ay, aw, ah, BgC());
  float cx = ax + 4 * S, cyy = ay + 16 * S, cw = aw - 8 * S, ch = ah - 26 * S;
  inputBlocked = openPopup != 0 || s_drag;
  deferred.clear();
  idSeed = s_tab;
  switch (s_tab) {
    case 0: PaintRage(cx, cyy, cw, ch); break;
    case 1: PaintAA(cx, cyy, cw, ch); break;
    case 2: PaintVisuals(cx, cyy, cw, ch); break;
    case 3: PaintMisc(cx, cyy, cw, ch); break;
    case 4: PaintColors(cx, cyy, cw, ch); break;
    case 5: PaintPlayers(cx, cyy, cw, ch); break;
    case 6: PaintConfig(cx, cyy, cw, ch); break;
  }
  // popups on top (they receive input even though widgets are blocked)
  inputBlocked = false; gTop = 0; gh = 1e9f;
  auto dl = deferred; deferred.clear();
  for (auto& f : dl) f();
  // fade-in overlay
  if (s_openAnim < 1.f) Rect(x, y, W, H, MkCol(0, 0, 0, (int)(255 * (1 - s_openAnim))));
}

bool MenuWantsMouse() { return g.menuOpen; }
} // namespace cheat
