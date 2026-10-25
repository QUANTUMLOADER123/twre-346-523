// menu.cpp - gamesense-style cheat menu (INSERT): tabs, group boxes, checkbox/slider/combo/multicombo,
// key binds with modes, colour pickers, config system
#include "cheat_internal.hpp"
#include "../vgui/surface.hpp"
#include "../vgui/controls.hpp"
#include "../game/client/cl_main.hpp"
#include "../game/shared/cs_player.hpp"
#include "../game/shared/soundemitter.hpp"
#include <functional>
#include <map>
#include <fstream>
#include <sstream>
#include <filesystem>
#include <cstring>
#include <cstdarg>
#include <algorithm>

namespace cheat {
Vars v;
PlayerSettings g_PlayerList[65];
RuntimeState g;

using vgui::surface;

// ------------------------------------------------------------------------------------------------
// key binds
static bool RawKeyDown(int key) {
  if (key <= 0) return false;
  if (key >= K_MOUSE1 && key <= K_MOUSE5) { static const int mb[5] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_SIDE, MOUSE_BUTTON_EXTRA }; return IsMouseButtonDown(mb[key - K_MOUSE1]); }
  if (key >= 1000) return false;
  return IsKeyDown(key);
}
static bool RawKeyPressed(int key) {
  if (key <= 0) return false;
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
  R("misc.clantag", m.clantag); R("misc.accent", m.accent); R("misc.menu_key", m.menuKey); R("misc.dpi", m.dpi);
  g_NamedBinds = { { "Ragebot", &r.key }, { "Minimum damage override", &r.minDamageOverrideKey }, { "Force body aim", &r.forceBaimKey }, { "Force safe point", &r.forceSafeKey },
    { "Double tap", &r.doubletapKey }, { "Hide shots", &r.hideshotsKey }, { "Anti-aim inverter", &a.inverter }, { "Freestanding", &a.freestandKey },
    { "Manual left", &a.manualLeft }, { "Manual right", &a.manualRight }, { "Manual back", &a.manualBack }, { "Duck peek assist", &a.fakeDuckKey },
    { "Slow motion", &a.slowWalkKey }, { "Thirdperson", &s.thirdpersonKey }, { "Edge jump", &m.edgeJumpKey } };
}

static std::string ConfigPath(const std::string& n) { return "cfg/hvhsense_" + n + ".cfg"; }
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
  std::vector<std::string> out = { "default", "rage", "hvh 1", "hvh 2", "legit", "secret" };
  std::error_code ec;
  for (auto& de : std::filesystem::directory_iterator("cfg", ec)) {
    std::string fn = de.path().filename().string();
    if (fn.rfind("hvhsense_", 0) == 0 && fn.size() > 13 && fn.substr(fn.size() - 4) == ".cfg") {
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
  ConColorMsg(Color32{ c.r, c.g, c.b, 255 }, "[hvhsense] ");
  ConColorMsg(Color32{ 220, 220, 220, 255 }, "%s\n", buf);
}
void DrawLogs() {
  float y = 5, now = (float)GetTime();
  for (auto it = s_logs.begin(); it != s_logs.end();) {
    float age = now - it->time;
    if (age > 6.f) { it = s_logs.erase(it); continue; }
    float a = age > 5.5f ? (6.f - age) / 0.5f : 1.f;
    float x = 6 - (age > 5.5f ? (1 - a) * 30 : 0);
    surface()->DrawTextOutline(FONT_CHEAT, "[hvhsense]", x, y, it->col.Alpha((int)(255 * a)), { 0, 0, 0, (unsigned char)(200 * a) });
    surface()->DrawTextOutline(FONT_CHEAT, it->text.c_str(), x + surface()->GetTextSize(FONT_CHEAT, "[hvhsense] ").x, y, { 230, 230, 230, (unsigned char)(255 * a) }, { 0, 0, 0, (unsigned char)(200 * a) });
    y += 13; ++it;
  }
}

// ------------------------------------------------------------------------------------------------
// immediate mode gui
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

static Col TextC() { return MkCol(205, 205, 205); }
static Col TextDim() { return MkCol(140, 140, 140); }
static bool Hover(float x, float y, float w, float h) { return mouse.x >= x && mouse.x < x + w && mouse.y >= y && mouse.y < y + h; }
static bool HoverClipped(float x, float y, float w, float h) { return !inputBlocked && Hover(x, y, w, h) && mouse.y >= gTop && mouse.y < gTop + gh - 4; }
static int MakeId(const char* label) { unsigned h = 2166136261u + idSeed * 16777619u; for (const char* p = label; *p; p++) { h ^= (unsigned char)*p; h *= 16777619u; } return (int)(h & 0x7fffffff) | 1; }
static void Rect(float x, float y, float w, float h, Col c) { surface()->DrawFilledRect(x, y, w, h, c); }
static void Outline(float x, float y, float w, float h, Col c) { surface()->DrawOutlinedRect(x, y, w, h, c); }
static void GradV(float x, float y, float w, float h, Col a, Col b) { surface()->DrawGradientV(x, y, w, h, a, b); }
static void Text(float x, float y, const char* t, Col c = TextC(), int font = FONT_CHEAT) { surface()->DrawText(font, t, x, y, c); }
static float TextW(const char* t, int font = FONT_CHEAT) { return surface()->GetTextSize(font, t).x; }
static Col Accent() { return v.misc.accent; }

static void BeginGroup(const char* title, float x, float y, float w, float h) {
  gId = MakeId(title); gx = x; gy = y; gw = w; gh = h;
  Rect(x, y, w, h, MkCol(17, 17, 17));
  Outline(x, y, w, h, MkCol(0, 0, 0)); Outline(x + 1, y + 1, w - 2, h - 2, MkCol(40, 40, 40));
  // title cut into the top border
  float tw = TextW(title, FONT_CHEAT_BOLD);
  Rect(x + 10, y - 1, tw + 8, 3, MkCol(17, 17, 17));
  Text(x + 14, y - 6, title, MkCol(210, 210, 210), FONT_CHEAT_BOLD);
  float& sc = scrolls[gId];
  gTop = y + 4;
  surface()->PushClip((int)x + 2, (int)y + 4, (int)w - 4, (int)h - 6);
  cy = y + 14 - sc;
  gContent = cy;
}
static void EndGroup() {
  float& sc = scrolls[gId];
  float content = cy + sc - (gy + 14) + 8;
  float view = gh - 18;
  if (!inputBlocked && Hover(gx, gy, gw, gh) && wheel != 0) sc -= wheel * 30 * S;
  sc = Clamp(sc, 0.f, std::max(0.f, content - view));
  surface()->PopClip();
  if (content > view) { // scrollbar
    float bh = std::max(20.f, view * view / content), by = gy + 6 + (view - bh) * (sc / std::max(1.f, content - view));
    Rect(gx + gw - 6, by, 3, bh, MkCol(60, 60, 60));
  }
}
static float WX() { return gx + 20 * S; }
static float WW() { return gw - 60 * S; }
static float s_lastLineY = 0;   // y of the last widget line (for attached keybind/colour)

static bool Checkbox(const char* label, bool& val, bool enabled = true) {
  int id = MakeId(label);
  float x = gx + 10 * S, y = cy;
  float bs = 8 * S;
  bool hv = enabled && HoverClipped(x, y - 1, TextW(label) + 20 * S, 14 * S);
  if (hv && mPressed) { val = !val; EmitSound2D("UI.Click", 0.3f); }
  if (val) GradV(x, y + 2, bs, bs, Accent(), Accent().Alpha(170));
  else GradV(x, y + 2, bs, bs, hv ? MkCol(70, 70, 70) : MkCol(60, 60, 60), MkCol(40, 40, 40));
  Outline(x - 1, y + 1, bs + 2, bs + 2, MkCol(0, 0, 0));
  Text(x + 18 * S, y, label, enabled ? TextC() : TextDim());
  (void)id;
  s_lastLineY = y; cy += 19 * S;
  return val;
}
static bool SliderF(const char* label, float& val, float mn, float mx, const char* fmt = "%.0f", float step = 0, const char* zeroText = nullptr) {
  int id = MakeId(label);
  if (label[0]) { Text(WX(), cy - 2, label); cy += 13 * S; }
  float x = WX(), y = cy, w = WW(), h = 7 * S;
  bool hv = HoverClipped(x, y - 2, w, h + 4);
  if (hv && mPressed) activeId = id;
  bool changed = false;
  if (activeId == id) {
    if (!mDown) activeId = 0;
    else { float t = Clamp((mouse.x - x) / w, 0.f, 1.f); float nv = mn + (mx - mn) * t; if (step > 0) nv = std::round(nv / step) * step; if (nv != val) { val = nv; changed = true; } }
  }
  if (hv && !mDown) { // arrow keys / wheel-free fine tuning with left/right click on the ends
    if (IsKeyPressed(KEY_LEFT)) { val = std::max(mn, val - (step > 0 ? step : 1)); changed = true; }
    if (IsKeyPressed(KEY_RIGHT)) { val = std::min(mx, val + (step > 0 ? step : 1)); changed = true; }
  }
  GradV(x, y, w, h, MkCol(50, 50, 50), MkCol(36, 36, 36));
  float t = (val - mn) / std::max(0.0001f, mx - mn);
  GradV(x, y, w * t, h, Accent(), Accent().Alpha(160));
  Outline(x - 1, y - 1, w + 2, h + 2, MkCol(0, 0, 0));
  char buf[64];
  if (zeroText && val <= mn + 0.0001f) snprintf(buf, sizeof(buf), "%s", zeroText); else snprintf(buf, sizeof(buf), fmt, val);
  float tw = TextW(buf, FONT_CHEAT_BOLD);
  surface()->DrawTextOutline(FONT_CHEAT_BOLD, buf, Clamp(x + w * t - tw * 0.5f, x - tw * 0.5f, x + w - tw * 0.5f), y - 2, MkCol(230, 230, 230));
  s_lastLineY = y - 13 * S; cy += 20 * S;
  return changed;
}
static bool Combo(const char* label, int& sel, const std::vector<const char*>& items) {
  int id = MakeId(label);
  if (label[0]) { Text(WX(), cy - 2, label); cy += 13 * S; }
  float x = WX(), y = cy, w = WW(), h = 19 * S;
  bool hv = HoverClipped(x, y, w, h);
  if (hv && mPressed) { openPopup = openPopup == id ? 0 : id; EmitSound2D("UI.Click", 0.3f); }
  GradV(x, y, w, h, hv ? MkCol(41, 41, 41) : MkCol(35, 35, 35), MkCol(30, 30, 30));
  Outline(x - 1, y - 1, w + 2, h + 2, MkCol(0, 0, 0));
  sel = Clamp(sel, 0, (int)items.size() - 1);
  Text(x + 8, y + 3 * S, items[sel], TextC());
  surface()->DrawTriangle({ x + w - 12, y + 8 * S }, { x + w - 5, y + 8 * S }, { x + w - 8.5f, y + 12 * S }, openPopup == id ? (Color)Accent() : Color{ 150, 150, 150, 255 });
  bool changed = false;
  if (openPopup == id) {
    float py = y + h + 1;
    int* psel = &sel; std::vector<const char*> its = items;
    deferred.push_back([=]() {
      float ih = 19 * S, ph = ih * its.size();
      Rect(x, py, w, ph, MkCol(35, 35, 35)); Outline(x - 1, py - 1, w + 2, ph + 2, MkCol(0, 0, 0));
      for (size_t i = 0; i < its.size(); i++) {
        bool ih_ = Hover(x, py + i * ih, w, ih);
        if (ih_) Rect(x, py + i * ih, w, ih, MkCol(28, 28, 28));
        Text(x + 8, py + i * ih + 3 * S, its[i], (int)i == *psel ? Accent() : (ih_ ? MkCol(255, 255, 255) : TextC()), (int)i == *psel ? FONT_CHEAT_BOLD : FONT_CHEAT);
        if (ih_ && mPressed) { *psel = (int)i; openPopup = 0; }
      }
      if (mPressed && !Hover(x, py - h - 2, w, ph + h + 2)) openPopup = 0;
    });
  }
  cy += h + 8 * S;
  return changed;
}
static void MultiCombo(const char* label, int& mask, const std::vector<const char*>& items) {
  int id = MakeId(label);
  if (label[0]) { Text(WX(), cy - 2, label); cy += 13 * S; }
  float x = WX(), y = cy, w = WW(), h = 19 * S;
  bool hv = HoverClipped(x, y, w, h);
  if (hv && mPressed) openPopup = openPopup == id ? 0 : id;
  GradV(x, y, w, h, hv ? MkCol(41, 41, 41) : MkCol(35, 35, 35), MkCol(30, 30, 30));
  Outline(x - 1, y - 1, w + 2, h + 2, MkCol(0, 0, 0));
  std::string txt;
  for (size_t i = 0; i < items.size(); i++) if (mask & (1 << i)) { if (!txt.empty()) txt += ", "; txt += items[i]; }
  if (txt.empty()) txt = "-";
  while (txt.size() > 3 && TextW(txt.c_str()) > w - 26) { txt.pop_back(); }
  if (TextW(txt.c_str()) > w - 26) txt += "...";
  Text(x + 8, y + 3 * S, txt.c_str());
  surface()->DrawTriangle({ x + w - 12, y + 8 * S }, { x + w - 5, y + 8 * S }, { x + w - 8.5f, y + 12 * S }, Color{ 150, 150, 150, 255 });
  if (openPopup == id) {
    float py = y + h + 1; int* pm = &mask; std::vector<const char*> its = items;
    deferred.push_back([=]() {
      float ih = 19 * S, ph = ih * its.size();
      Rect(x, py, w, ph, MkCol(35, 35, 35)); Outline(x - 1, py - 1, w + 2, ph + 2, MkCol(0, 0, 0));
      for (size_t i = 0; i < its.size(); i++) {
        bool on = (*pm >> i) & 1, ih_ = Hover(x, py + i * ih, w, ih);
        if (ih_) Rect(x, py + i * ih, w, ih, MkCol(28, 28, 28));
        Text(x + 8, py + i * ih + 3 * S, its[i], on ? Accent() : (ih_ ? MkCol(255, 255, 255) : TextC()), on ? FONT_CHEAT_BOLD : FONT_CHEAT);
        if (ih_ && mPressed) *pm ^= (1 << i);
      }
      if (mPressed && !Hover(x, py - h - 2, w, ph + h + 2)) openPopup = 0;
    });
  }
  cy += h + 8 * S;
}
static bool ButtonW(const char* label, float wOverride = 0) {
  float x = WX(), y = cy, w = wOverride > 0 ? wOverride : WW(), h = 24 * S;
  bool hv = HoverClipped(x, y, w, h);
  GradV(x, y, w, h, hv ? (mDown ? MkCol(28, 28, 28) : MkCol(42, 42, 42)) : MkCol(35, 35, 35), MkCol(26, 26, 26));
  Outline(x - 1, y - 1, w + 2, h + 2, MkCol(0, 0, 0)); Outline(x, y, w, h, MkCol(50, 50, 50));
  float tw = TextW(label, FONT_CHEAT_BOLD);
  Text(x + (w - tw) * 0.5f, y + 6 * S, label, TextC(), FONT_CHEAT_BOLD);
  cy += h + 8 * S;
  bool clicked = hv && mPressed;
  if (clicked) EmitSound2D("UI.Click", 0.4f);
  return clicked;
}
static void LabelW(const char* t, Col c = TextC()) { Text(gx + 10 * S + 18 * S, cy, t, c); s_lastLineY = cy; cy += 17 * S; }

// key bind attached to the previous line (right aligned)
static void KeyBindW(const char* idLabel, KeyBind& kb) {
  int id = MakeId(idLabel);
  char buf[48];
  if (captureKeyId == id) snprintf(buf, sizeof(buf), "[...]");
  else if (kb.mode == KEYMODE_ALWAYS) snprintf(buf, sizeof(buf), "[on]");
  else if (!kb.key) snprintf(buf, sizeof(buf), "[-]");
  else { std::string k = Key_KeynumToString(kb.key); for (auto& ch : k) ch = (char)toupper(ch); if (k.size() > 6) k = k.substr(0, 6); snprintf(buf, sizeof(buf), "[%s]", k.c_str()); }
  float tw = TextW(buf, FONT_ICONS_SMALL);
  float x = gx + gw - 16 * S - tw, y = s_lastLineY + 1;
  bool hv = HoverClipped(x - 4, y - 2, tw + 8, 14 * S);
  Text(x, y, buf, captureKeyId == id ? MkCol(255, 40, 40) : (hv ? MkCol(200, 200, 200) : MkCol(115, 115, 115)), FONT_ICONS_SMALL);
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
    KeyBind* pk = &kb; float px = x + tw + 6, py = y;
    deferred.push_back([=]() {
      const char* modes[4] = { "Always on", "On hotkey", "Toggle", "Off hotkey" };
      float ih = 18 * S, w = 90 * S;
      Rect(px, py, w, ih * 4, MkCol(35, 35, 35)); Outline(px - 1, py - 1, w + 2, ih * 4 + 2, MkCol(0, 0, 0));
      for (int i = 0; i < 4; i++) {
        bool h = Hover(px, py + i * ih, w, ih);
        if (h) Rect(px, py + i * ih, w, ih, MkCol(28, 28, 28));
        Text(px + 7, py + i * ih + 3, modes[i], pk->mode == i ? Accent() : TextC(), pk->mode == i ? FONT_CHEAT_BOLD : FONT_CHEAT);
        if (h && mPressed) { pk->mode = i; openPopup = 0; }
      }
      if (mPressed && !Hover(px, py, w, ih * 4)) openPopup = 0;
    });
  }
}

// HSV colour picker attached to the previous line
static void ColorToHSV(Col c, float& h, float& s, float& vv) { Vector3 hsv = ColorToHSV(Color{ c.r, c.g, c.b, 255 }); h = hsv.x; s = hsv.y; vv = hsv.z; }
static void ColorPickerW(const char* idLabel, Col& col, float rightOffset = 0) {
  int id = MakeId(idLabel);
  float w = 16 * S, h = 8 * S, x = gx + gw - 18 * S - w - rightOffset, y = s_lastLineY + 3;
  bool hv = HoverClipped(x - 2, y - 2, w + 4, h + 4);
  GradV(x, y, w, h, Col(col).Alpha(255), MkCol(col.r * 3 / 4, col.g * 3 / 4, col.b * 3 / 4));
  Outline(x - 1, y - 1, w + 2, h + 2, MkCol(0, 0, 0));
  if (hv && mPressed) openPopup = openPopup == id ? 0 : id;
  if (openPopup == id) {
    Col* pc = &col; float px = x + w + 6, py = y;
    deferred.push_back([=]() {
      static float hue = 0, sat = 0, val = 0; static int lastId = 0;
      if (lastId != id) { ColorToHSV(*pc, hue, sat, val); lastId = id; }
      float sz = 150 * S, pw = sz + 36 * S, ph = sz + 24 * S;
      float qx = std::min(px, (float)GetScreenWidth() - pw - 4);
      Rect(qx, py, pw, ph, MkCol(30, 30, 30)); Outline(qx - 1, py - 1, pw + 2, ph + 2, MkCol(0, 0, 0));
      float sx = qx + 6, sy = py + 6;
      // saturation/value square
      Color hc = ColorFromHSV(hue, 1, 1);
      surface()->DrawGradientH(sx, sy, sz, sz, { 255, 255, 255, 255 }, hc);
      surface()->DrawGradientV(sx, sy, sz, sz, { 0, 0, 0, 0 }, { 0, 0, 0, 255 });
      Outline(sx - 1, sy - 1, sz + 2, sz + 2, MkCol(0, 0, 0));
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
      // hue bar
      float hx = sx + sz + 6;
      for (int i = 0; i < 6; i++) surface()->DrawGradientV(hx, sy + i * sz / 6, 14 * S, sz / 6 + 1, ColorFromHSV(i * 60.f, 1, 1), ColorFromHSV((i + 1) * 60.f, 1, 1));
      Rect(hx - 1, sy + hue / 360.f * sz - 1, 14 * S + 2, 3, MkCol(255, 255, 255));
      // alpha bar
      surface()->DrawGradientH(sx, sy + sz + 5, sz, 9 * S, { pc->r, pc->g, pc->b, 0 }, { pc->r, pc->g, pc->b, 255 });
      Outline(sx - 1, sy + sz + 4, sz + 2, 9 * S + 2, MkCol(0, 0, 0));
      Rect(sx + pc->a / 255.f * sz - 1, sy + sz + 4, 3, 9 * S + 2, MkCol(255, 255, 255));
      if (mPressed && !Hover(qx, py, pw, ph) && !drag) { openPopup = 0; lastId = 0; }
    });
  }
}

static bool ListBox(const char* idLabel, const std::vector<std::string>& items, int& sel, float h) {
  float x = WX(), y = cy, w = WW();
  Rect(x, y, w, h, MkCol(25, 25, 25)); Outline(x - 1, y - 1, w + 2, h + 2, MkCol(0, 0, 0));
  bool changed = false; float ih = 20 * S;
  surface()->PushClip((int)x, (int)std::max(y, gTop), (int)w, (int)h);
  for (size_t i = 0; i < items.size(); i++) {
    float iy = y + i * ih; if (iy > y + h) break;
    bool hv = HoverClipped(x, iy, w, ih) && Hover(x, y, w, h);
    if ((int)i == sel) Rect(x, iy, w, ih, MkCol(35, 35, 35));
    Text(x + 8, iy + 4 * S, items[i].c_str(), (int)i == sel ? Accent() : (hv ? MkCol(255, 255, 255) : TextC()), (int)i == sel ? FONT_CHEAT_BOLD : FONT_CHEAT);
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
  float x = WX(), y = cy, w = WW(), h = 20 * S;
  bool hv = HoverClipped(x, y, w, h);
  if (mPressed) focus = hv ? id : (focus == id ? 0 : focus);
  Rect(x, y, w, h, MkCol(25, 25, 25)); Outline(x - 1, y - 1, w + 2, h + 2, focus == id ? Accent() : MkCol(0, 0, 0));
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
  Text(x + 6, y + 4 * S, shown.c_str());
  cy += h + 8 * S;
  return enter;
}
} // namespace gui

static bool s_capturingAny() { return gui::captureKeyId != 0 || gui::g_cheatTextFocus; }
bool Menu_IsCapturingKey() { return s_capturingAny(); }

// ------------------------------------------------------------------------------------------------
// tab icons (procedural, gamesense style)
static void DrawTabIcon(int tab, float cx, float cy, Col c, float S) {
  auto* s = surface();
  switch (tab) {
    case 0: // rage: crosshair
      s->DrawCircle(cx, cy, 13 * S, c, 32); s->DrawCircle(cx, cy, 12 * S, c, 32);
      s->DrawLine(cx - 19 * S, cy, cx - 6 * S, cy, c, 2); s->DrawLine(cx + 6 * S, cy, cx + 19 * S, cy, c, 2);
      s->DrawLine(cx, cy - 19 * S, cx, cy - 6 * S, c, 2); s->DrawLine(cx, cy + 6 * S, cx, cy + 19 * S, c, 2);
      s->DrawFilledCircle(cx, cy, 2 * S, c); break;
    case 1: { // anti-aim: rotating arrows
      for (int i = 0; i < 3; i++) { float a0 = i * 2.094f + 0.3f; for (int k = 0; k < 8; k++) { float a = a0 + k * 0.17f, b = a + 0.17f; s->DrawLine(cx + std::cos(a) * 14 * S, cy + std::sin(a) * 14 * S, cx + std::cos(b) * 14 * S, cy + std::sin(b) * 14 * S, c, 2.5f); }
        float e = a0 + 1.36f; Vector2 tip{ cx + std::cos(e + 0.25f) * 14 * S, cy + std::sin(e + 0.25f) * 14 * S };
        s->DrawTriangle({ cx + std::cos(e) * 9 * S, cy + std::sin(e) * 9 * S }, tip, { cx + std::cos(e) * 19 * S, cy + std::sin(e) * 19 * S }, c); }
      s->DrawFilledCircle(cx, cy, 3 * S, c); break; }
    case 2: { // visuals: eye
      for (int k = 0; k < 16; k++) { float t0 = -1 + k / 8.f, t1 = -1 + (k + 1) / 8.f; float y0 = (1 - t0 * t0) * 9 * S, y1 = (1 - t1 * t1) * 9 * S;
        s->DrawLine(cx + t0 * 18 * S, cy - y0, cx + t1 * 18 * S, cy - y1, c, 2); s->DrawLine(cx + t0 * 18 * S, cy + y0, cx + t1 * 18 * S, cy + y1, c, 2); }
      s->DrawFilledCircle(cx, cy, 6 * S, c); s->DrawFilledCircle(cx, cy, 2.5f * S, { 12, 12, 12, 255 }); break; }
    case 3: { // misc: gear
      for (int i = 0; i < 8; i++) { float a = i * 0.785f; s->DrawLine(cx + std::cos(a) * 9 * S, cy + std::sin(a) * 9 * S, cx + std::cos(a) * 17 * S, cy + std::sin(a) * 17 * S, c, 5 * S); }
      s->DrawFilledCircle(cx, cy, 12 * S, c); s->DrawFilledCircle(cx, cy, 5 * S, { 12, 12, 12, 255 }); break; }
    case 4: // players: two heads
      s->DrawFilledCircle(cx - 7 * S, cy - 6 * S, 5 * S, c); s->DrawFilledCircle(cx + 7 * S, cy - 6 * S, 5 * S, c);
      s->DrawFilledRect(cx - 14 * S, cy + 1 * S, 13 * S, 12 * S, c); s->DrawFilledRect(cx + 1 * S, cy + 1 * S, 13 * S, 12 * S, c); break;
    case 5: // config: floppy disk
      s->DrawFilledRect(cx - 14 * S, cy - 14 * S, 28 * S, 28 * S, c); s->DrawFilledRect(cx - 8 * S, cy - 14 * S, 14 * S, 9 * S, { 12, 12, 12, 255 });
      s->DrawFilledRect(cx + 1 * S, cy - 12 * S, 3 * S, 5 * S, c); s->DrawFilledRect(cx - 9 * S, cy + 2 * S, 18 * S, 10 * S, { 12, 12, 12, 255 }); break;
  }
}

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
  LabelW("Menu color"); ColorPickerW("accent", m.accent);
  Combo("DPI scale", m.dpi, { "100%", "125%", "150%" });
  if (ButtonW("Unload")) { g.menuOpen = false; v.rage.enabled = false; v.aa.enabled = false; Log(MkCol(255, 80, 80), "unloaded (not really)"); }
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
    if (clip) { std::error_code ec; std::filesystem::create_directories("cfg", ec); std::ofstream("cfg/hvhsense__clip.cfg") << clip; Config_Load("_clip"); Log(v.misc.accent, "Imported config from clipboard"); }
  }
  if (ButtonW("Export to clipboard")) { Config_Save("_clip"); std::ifstream f("cfg/hvhsense__clip.cfg"); std::stringstream ss; ss << f.rdbuf(); SetClipboardText(ss.str().c_str()); }
  LabelW("Configs are stored in cfg/hvhsense_<name>.cfg", TextDim());
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
  float W = 660 * S, H = 560 * S;
  s_mx = Clamp(s_mx, 0.f, std::max(0.f, GetScreenWidth() - W)); s_my = Clamp(s_my, 0.f, std::max(0.f, GetScreenHeight() - H));
  float x = s_mx, y = s_my;
  int alpha = (int)(255 * s_openAnim);
  (void)alpha;
  // dragging: anywhere on the frame that isn't a widget (top bar / borders / tab column background)
  bool overFrame = Hover(x, y, W, H);
  if (mPressed && openPopup == 0 && overFrame && (mouse.y < y + 14 * S || mouse.x < x + 8 || mouse.x > x + W - 8 || mouse.y > y + H - 8)) { s_drag = true; s_dragOff = { mouse.x - x, mouse.y - y }; }
  if (!mDown) s_drag = false;
  if (s_drag) { s_mx = mouse.x - s_dragOff.x; s_my = mouse.y - s_dragOff.y; x = s_mx; y = s_my; }
  // frame: gamesense nested borders
  Col borders[] = { MkCol(0, 0, 0), MkCol(60, 60, 60), MkCol(40, 40, 40), MkCol(40, 40, 40), MkCol(40, 40, 40), MkCol(60, 60, 60) };
  for (int i = 0; i < 6; i++) Outline(x + i, y + i, W - i * 2, H - i * 2, borders[i]);
  float ix = x + 6, iy = y + 6, iw = W - 12, ih = H - 12;
  Rect(ix, iy, iw, ih, MkCol(12, 12, 12));
  // subtle pattern
  for (float py = iy + 4; py < iy + ih; py += 4) for (float px = ix + 74 * S + ((int)((py - iy) / 4) % 2) * 2; px < ix + iw; px += 4) Rect(px, py, 1, 1, MkCol(18, 18, 18));
  // rainbow gradient bar
  float bw = iw / 2;
  surface()->DrawGradientH(ix + 1, iy + 1, bw, 2, { 55, 177, 218, 255 }, { 204, 70, 205, 255 });
  surface()->DrawGradientH(ix + 1 + bw, iy + 1, bw - 2, 2, { 204, 70, 205, 255 }, { 204, 227, 53, 255 });
  Rect(ix + 1, iy + 3, iw - 2, 1, MkCol(0, 0, 0));
  // tab column
  float tx = ix, ty = iy + 4, tw = 74 * S, th = 64 * S;
  Rect(tx, ty, tw, ih - 4, MkCol(12, 12, 12));
  Rect(tx + tw, ty, 1, ih - 4, MkCol(0, 0, 0)); Rect(tx + tw - 1, ty, 1, ih - 4, MkCol(48, 48, 48));
  float tabsY = ty + 14 * S;
  for (int t = 0; t < 6; t++) {
    float yy = tabsY + t * th;
    bool sel = s_tab == t, hv = openPopup == 0 && Hover(tx, yy, tw, th);
    if (sel) {
      Rect(tx, yy, tw + 1, th, MkCol(20, 20, 20));
      Rect(tx, yy, tw, 1, MkCol(0, 0, 0)); Rect(tx, yy + 1, tw, 1, MkCol(48, 48, 48));
      Rect(tx, yy + th - 1, tw, 1, MkCol(0, 0, 0)); Rect(tx, yy + th - 2, tw, 1, MkCol(48, 48, 48));
    }
    DrawTabIcon(t, tx + tw / 2, yy + th / 2, sel ? MkCol(210, 210, 210) : (hv ? MkCol(150, 150, 150) : MkCol(90, 90, 90)), S);
    if (hv && mPressed) { s_tab = t; openPopup = 0; EmitSound2D("UI.Click", 0.4f); }
  }
  // content
  float cx = tx + tw + 10 * S, cyy = ty + 22 * S, cw = ix + iw - cx - 10 * S, ch = iy + ih - cyy - 14 * S;
  inputBlocked = openPopup != 0 || s_drag;
  bool blockedBeforeDeferred = inputBlocked;
  deferred.clear();
  idSeed = s_tab;
  switch (s_tab) {
    case 0: PaintRage(cx - 10 * S, cyy, cw + 10 * S, ch); break;
    case 1: PaintAA(cx - 10 * S, cyy, cw + 10 * S, ch); break;
    case 2: PaintVisuals(cx - 10 * S, cyy, cw + 10 * S, ch); break;
    case 3: PaintMisc(cx - 10 * S, cyy, cw + 10 * S, ch); break;
    case 4: PaintPlayers(cx - 10 * S, cyy, cw + 10 * S, ch); break;
    case 5: PaintConfig(cx - 10 * S, cyy, cw + 10 * S, ch); break;
  }
  // popups on top (they receive input even though widgets are blocked)
  inputBlocked = false; gTop = 0; gh = 1e9f;
  auto dl = deferred; deferred.clear();
  for (auto& f : dl) f();
  (void)blockedBeforeDeferred;
  // footer: user name like gamesense
  // fade-in overlay
  if (s_openAnim < 1.f) Rect(x, y, W, H, MkCol(0, 0, 0, (int)(255 * (1 - s_openAnim))));
}

bool MenuWantsMouse() { return g.menuOpen; }
} // namespace cheat
