// input.cpp - keyboard/mouse input, key bindings (keys.cpp) and CInput::CreateMove (in_main.cpp)
#include "cl_main.hpp"
#include "hud_menus.hpp"
#include "touch.hpp"
#include "../../engine/host.hpp"
#include "../shared/cs_player.hpp"
#include "../shared/weapon_csbase.hpp"
#include "../shared/cs_gamerules.hpp"
#include "../shared/gameevents.hpp"
#include "../../cheat/cheat.hpp"
#include "raylib.h"
#include "../../vgui/controls.hpp"
#include <map>
#include <cstring>
#include <strings.h>
#include <cmath>
#include <algorithm>
#include <cctype>

ConVar sensitivity("sensitivity", "2.5", FCVAR_ARCHIVE, "Mouse sensitivity.", true, 0.0001f, true, 100);
ConVar m_yaw("m_yaw", "0.022", FCVAR_ARCHIVE, "Mouse yaw factor.");
ConVar m_pitch("m_pitch", "0.022", FCVAR_ARCHIVE, "Mouse pitch factor.");
ConVar m_rawinput("m_rawinput", "1", FCVAR_ARCHIVE, "Use Raw Input for mouse input.");
ConVar m_filter("m_filter", "0", FCVAR_ARCHIVE, "Mouse filtering (set this to 1 to average the mouse over 2 frames).");
ConVar zoom_sensitivity_ratio("zoom_sensitivity_ratio_mouse", "1.0", FCVAR_ARCHIVE, "Additional mouse sensitivity scale factor applied when FOV is zoomed in.");
ConVar cl_forwardspeed("cl_forwardspeed", "450", FCVAR_ARCHIVE | FCVAR_CHEAT, "");
ConVar cl_sidespeed("cl_sidespeed", "450", FCVAR_ARCHIVE | FCVAR_CHEAT, "");
ConVar cl_upspeed("cl_upspeed", "320", FCVAR_ARCHIVE | FCVAR_CHEAT, "");
ConVar cl_yawspeed("cl_yawspeed", "210", FCVAR_ARCHIVE, "");
ConVar cl_pitchspeed("cl_pitchspeed", "225", FCVAR_ARCHIVE, "");
ConVar cl_pitchup("cl_pitchup", "89", FCVAR_CHEAT, "");
ConVar cl_pitchdown("cl_pitchdown", "89", FCVAR_CHEAT, "");
ConVar m_invert("m_invert", "0", FCVAR_ARCHIVE, "Invert mouse");  // (m_pitch < 0 in Source)
ConVar hud_fastswitch("hud_fastswitch", "1", FCVAR_ARCHIVE, "Weapon selection switches immediately");

kbutton_t in_attack, in_attack2, in_jump, in_duck, in_forward, in_back, in_moveleft, in_moveright, in_speed, in_use, in_reload, in_score, in_left, in_right, in_lookup, in_lookdown, in_moveup, in_movedown;
static int s_pendingWeaponSelect = 0;
static int s_pendingImpulse = 0;

// --- kbutton -----------------------------------------------------------------------------------
static void KeyDown(kbutton_t* b, const CCommand& args) {
  int k = args.ArgC() > 1 ? atoi(args.Arg(1)) : -1;
  if (k == b->down[0] || k == b->down[1]) return;
  if (!b->down[0]) b->down[0] = k; else if (!b->down[1]) b->down[1] = k; else return;
  if (b->state & 1) return;
  b->state |= 1 + 2; // down + impulse down
}
static void KeyUp(kbutton_t* b, const CCommand& args) {
  if (args.ArgC() < 2) { b->down[0] = b->down[1] = 0; b->state = 4; return; }
  int k = atoi(args.Arg(1));
  if (b->down[0] == k) b->down[0] = 0; else if (b->down[1] == k) b->down[1] = 0; else return;
  if (b->down[0] || b->down[1]) return;
  if (!(b->state & 1)) return;
  b->state &= ~1; b->state |= 4;
}
bool IN_IsButtonDown(kbutton_t& b) { return (b.state & 1) != 0; }

#define KBUTTON(name, var) \
  static ConCommand cmd_p_##var("+" name, [](const CCommand& a) { KeyDown(&var, a); }, ""); \
  static ConCommand cmd_m_##var("-" name, [](const CCommand& a) { KeyUp(&var, a); }, "");
KBUTTON("attack", in_attack) KBUTTON("attack2", in_attack2) KBUTTON("jump", in_jump) KBUTTON("duck", in_duck)
KBUTTON("forward", in_forward) KBUTTON("back", in_back) KBUTTON("moveleft", in_moveleft) KBUTTON("moveright", in_moveright)
KBUTTON("speed", in_speed) KBUTTON("use", in_use) KBUTTON("reload", in_reload) KBUTTON("left", in_left) KBUTTON("right", in_right)
KBUTTON("lookup", in_lookup) KBUTTON("lookdown", in_lookdown) KBUTTON("moveup", in_moveup) KBUTTON("movedown", in_movedown)
static ConCommand cmd_p_score("+showscores", [](const CCommand& a) { KeyDown(&in_score, a); cl.showScores = true; }, "");
static ConCommand cmd_m_score("-showscores", [](const CCommand& a) { KeyUp(&in_score, a); cl.showScores = false; }, "");

// weapon selection
static ConCommand cmd_slot1("slot1", [](const CCommand&) { s_pendingWeaponSelect = 1; }, "");
static ConCommand cmd_slot2("slot2", [](const CCommand&) { s_pendingWeaponSelect = 2; }, "");
static ConCommand cmd_slot3("slot3", [](const CCommand&) { s_pendingWeaponSelect = 3; }, "");
static ConCommand cmd_slot4("slot4", [](const CCommand&) {
  // pressing 4 repeatedly cycles grenades
  CCSPlayer* p = g_pLocalPlayer; if (!p) return;
  C_WeaponCSBase* cur = p->GetActiveCSWeapon();
  const int order[5] = { WEAPON_HEGRENADE, WEAPON_FLASHBANG, WEAPON_SMOKEGRENADE, WEAPON_MOLOTOV, WEAPON_INCGRENADE };
  int start = 0; if (cur && cur->GetWeaponType() == WEAPONTYPE_GRENADE) for (int i = 0; i < 5; i++) if (order[i] == cur->GetCSWeaponID()) start = i + 1;
  for (int k = 0; k < 5; k++) { int id = order[(start + k) % 5]; if (p->Weapon_OwnsThisType(id)) { s_pendingWeaponSelect = 100 + id; return; } }
}, "");
static ConCommand cmd_slot5("slot5", [](const CCommand&) { s_pendingWeaponSelect = 5; }, "");
static ConCommand cmd_lastinv("lastinv", [](const CCommand&) { if (g_pLocalPlayer && g_pLocalPlayer->m_hLastWeapon) s_pendingWeaponSelect = 100 + g_pLocalPlayer->m_hLastWeapon->GetCSWeaponID(); }, "");
static ConCommand cmd_invnext("invnext", [](const CCommand&) { if (g_pLocalPlayer) g_pLocalPlayer->SelectNextItem(1); }, "");
static ConCommand cmd_invprev("invprev", [](const CCommand&) { if (g_pLocalPlayer) g_pLocalPlayer->SelectNextItem(-1); }, "");
static ConCommand cmd_use("use", [](const CCommand& a) { if (a.ArgC() > 1) { int id = AliasToWeaponID(a.Arg(1)); if (id) s_pendingWeaponSelect = 100 + id; } }, "Use a weapon: use weapon_knife");
static ConCommand cmd_thirdperson("thirdperson", [](const CCommand&) { cl.thirdperson = true; }, "Switch to thirdperson camera.");
static ConCommand cmd_firstperson("firstperson", [](const CCommand&) { cl.thirdperson = false; }, "Switch to firstperson camera.");
static ConCommand cmd_buymenu("buymenu", [](const CCommand& a) {
  CCSPlayer* p = g_pLocalPlayer; if (!p) return;
  if (cl.buyMenuOpen && a.ArgC() < 2) { cl.buyMenuOpen = false; return; }
  if (p->CanPlayerBuy(true)) { cl.buyMenuOpen = true; cl.teamMenuOpen = false; HUD_BuyMenuSetCategory(a.ArgC() >= 2 ? atoi(a.Arg(1)) - 1 : -1); }
}, "Show the buy menu (optional: category 1-6 = pistols, heavy, SMGs, rifles, gear, grenades)");
static ConCommand cmd_chooseteam("chooseteam", [](const CCommand&) { if (CL_IsInGame()) { cl.teamMenuOpen = !cl.teamMenuOpen; cl.buyMenuOpen = false; } }, "Choose a new team");
static ConCommand cmd_messagemode("messagemode", [](const CCommand&) { if (CL_IsInGame()) { cl.chatOpen = true; cl.chatTeam = false; cl.chatBuffer.clear(); } }, "");
static ConCommand cmd_messagemode2("messagemode2", [](const CCommand&) { if (CL_IsInGame()) { cl.chatOpen = true; cl.chatTeam = true; cl.chatBuffer.clear(); } }, "");
static ConCommand cmd_nightvision("nightvision", [](const CCommand&) { cl.nightvision = !cl.nightvision; }, "");
static ConCommand cmd_spec_next("spec_next", [](const CCommand&) { cl.specTarget++; }, "");
static ConCommand cmd_spec_prev("spec_prev", [](const CCommand&) { cl.specTarget--; }, "");
static ConCommand cmd_spec_mode("spec_mode", [](const CCommand&) {
  CCSPlayer* p = g_pLocalPlayer; if (!p || p->IsAlive()) return;
  p->m_iObserverMode = p->m_iObserverMode == OBS_MODE_IN_EYE ? OBS_MODE_CHASE : p->m_iObserverMode == OBS_MODE_CHASE ? OBS_MODE_ROAMING : OBS_MODE_IN_EYE;
  if (p->m_iObserverMode == OBS_MODE_ROAMING && cl.viewEntity && cl.viewEntity != p) { p->m_vecOrigin = cl.viewOrigin; p->m_vecOldOrigin = p->m_vecOrigin; p->m_vecVelocity.Init(); }
}, "");
static ConCommand cmd_say("say", [](const CCommand& a) {
  CCSPlayer* p = g_pLocalPlayer; if (!p || a.ArgS().empty()) return;
  std::string t = a.ArgS(); if (t.size() > 1 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
  HUD_AddChat(p->GetPlayerName(), t, p->m_iTeamNum, false, !p->IsAlive());
  IGameEvent ev("player_say"); ev.SetInt("userid", p->entindex()).SetString("text", t); gameeventmanager->FireEvent(ev);
}, "Display player message");
static ConCommand cmd_say_team("say_team", [](const CCommand& a) {
  CCSPlayer* p = g_pLocalPlayer; if (!p || a.ArgS().empty()) return;
  std::string t = a.ArgS(); if (t.size() > 1 && t.front() == '"' && t.back() == '"') t = t.substr(1, t.size() - 2);
  HUD_AddChat(p->GetPlayerName(), t, p->m_iTeamNum, true, !p->IsAlive());
}, "Display player message to team");
static ConCommand cmd_rebuy("rebuy", [](const CCommand&) { Cbuf_AddText("buy vesthelm ak47 m4a1 deagle hegrenade flashbang smokegrenade defuser\n"); }, "");
static ConCommand cmd_autobuy("autobuy", [](const CCommand&) { Cbuf_AddText("buy vesthelm ak47 m4a1 deagle defuser\n"); }, "");

// --- keys --------------------------------------------------------------------------------------
struct KeyName { int key; const char* name; };
static const KeyName s_keyNames[] = {
  { KEY_TAB, "TAB" }, { KEY_ENTER, "ENTER" }, { KEY_ESCAPE, "ESCAPE" }, { KEY_SPACE, "SPACE" }, { KEY_BACKSPACE, "BACKSPACE" },
  { KEY_UP, "UPARROW" }, { KEY_DOWN, "DOWNARROW" }, { KEY_LEFT, "LEFTARROW" }, { KEY_RIGHT, "RIGHTARROW" },
  { KEY_LEFT_ALT, "ALT" }, { KEY_RIGHT_ALT, "RALT" }, { KEY_LEFT_CONTROL, "CTRL" }, { KEY_RIGHT_CONTROL, "RCTRL" }, { KEY_LEFT_SHIFT, "SHIFT" }, { KEY_RIGHT_SHIFT, "RSHIFT" },
  { KEY_F1, "F1" }, { KEY_F2, "F2" }, { KEY_F3, "F3" }, { KEY_F4, "F4" }, { KEY_F5, "F5" }, { KEY_F6, "F6" }, { KEY_F7, "F7" }, { KEY_F8, "F8" },
  { KEY_F9, "F9" }, { KEY_F10, "F10" }, { KEY_F11, "F11" }, { KEY_F12, "F12" },
  { KEY_INSERT, "INS" }, { KEY_DELETE, "DEL" }, { KEY_PAGE_DOWN, "PGDN" }, { KEY_PAGE_UP, "PGUP" }, { KEY_HOME, "HOME" }, { KEY_END, "END" },
  { KEY_CAPS_LOCK, "CAPSLOCK" }, { KEY_PAUSE, "PAUSE" },
  { KEY_KP_0, "KP_INS" }, { KEY_KP_1, "KP_END" }, { KEY_KP_2, "KP_DOWNARROW" }, { KEY_KP_3, "KP_PGDN" }, { KEY_KP_4, "KP_LEFTARROW" }, { KEY_KP_5, "KP_5" },
  { KEY_KP_6, "KP_RIGHTARROW" }, { KEY_KP_7, "KP_HOME" }, { KEY_KP_8, "KP_UPARROW" }, { KEY_KP_9, "KP_PGUP" }, { KEY_KP_ENTER, "KP_ENTER" },
  { KEY_KP_DIVIDE, "KP_SLASH" }, { KEY_KP_MULTIPLY, "KP_MULTIPLY" }, { KEY_KP_SUBTRACT, "KP_MINUS" }, { KEY_KP_ADD, "KP_PLUS" }, { KEY_KP_DECIMAL, "KP_DEL" },
  { KEY_GRAVE, "`" }, { KEY_MINUS, "-" }, { KEY_EQUAL, "=" }, { KEY_LEFT_BRACKET, "[" }, { KEY_RIGHT_BRACKET, "]" }, { KEY_BACKSLASH, "\\" },
  { KEY_SEMICOLON, "SEMICOLON" }, { KEY_APOSTROPHE, "'" }, { KEY_COMMA, "," }, { KEY_PERIOD, "." }, { KEY_SLASH, "/" },
  { K_MOUSE1, "MOUSE1" }, { K_MOUSE2, "MOUSE2" }, { K_MOUSE3, "MOUSE3" }, { K_MOUSE4, "MOUSE4" }, { K_MOUSE5, "MOUSE5" },
  { K_MWHEELUP, "MWHEELUP" }, { K_MWHEELDOWN, "MWHEELDOWN" },
};
static std::map<int, std::string> s_binds;
static bool s_keyDown[1100];

const char* Key_KeynumToString(int key) {
  if (key >= KEY_A && key <= KEY_Z) { static char b[2][2]; static int i = 0; i ^= 1; b[i][0] = (char)('A' + key - KEY_A); b[i][1] = 0; return b[i]; }
  if (key >= KEY_ZERO && key <= KEY_NINE) { static char b[2][2]; static int i = 0; i ^= 1; b[i][0] = (char)('0' + key - KEY_ZERO); b[i][1] = 0; return b[i]; }
  for (auto& k : s_keyNames) if (k.key == key) return k.name;
  return "<UNKNOWN>";
}
int Key_StringToKeynum(const char* s) {
  if (!s || !*s) return -1;
  if (!s[1]) {
    char c = (char)toupper(s[0]);
    if (c >= 'A' && c <= 'Z') return KEY_A + (c - 'A');
    if (c >= '0' && c <= '9') return KEY_ZERO + (c - '0');
  }
  for (auto& k : s_keyNames) if (!strcasecmp(k.name, s)) return k.key;
  return -1;
}
const char* Key_GetBinding(int key) { auto it = s_binds.find(key); return it == s_binds.end() ? "" : it->second.c_str(); }
void Key_SetBinding(int key, const char* cmd) { if (key < 0) return; if (!cmd || !*cmd) s_binds.erase(key); else s_binds[key] = cmd; }
int Key_FindBinding(const char* cmd) { for (auto& [k, v] : s_binds) if (!strcasecmp(v.c_str(), cmd)) return k; return -1; }
void Key_WriteBindings(std::string& out) {
  out += "unbindall\n";
  for (auto& [k, v] : s_binds) { std::string kn = Key_KeynumToString(k); if (kn == "\\") kn = "\\\\"; out += "bind \"" + kn + "\" \"" + v + "\"\n"; }
}
void Key_ClearStates() {
  for (int k = 0; k < 1100; k++) if (s_keyDown[k]) { IN_KeyDown(k, false); }
}

static ConCommand cmd_bind("bind", [](const CCommand& a) {
  if (a.ArgC() < 2) { Msg("bind <key> [command] : attach a command to a key\n"); return; }
  int k = Key_StringToKeynum(a.Arg(1));
  if (k < 0) { Msg("\"%s\" isn't a valid key\n", a.Arg(1)); return; }
  if (a.ArgC() == 2) { const char* b = Key_GetBinding(k); if (*b) Msg("\"%s\" = \"%s\"\n", a.Arg(1), b); else Msg("\"%s\" is not bound\n", a.Arg(1)); return; }
  std::string cmd; for (int i = 2; i < a.ArgC(); i++) { if (i > 2) cmd += " "; cmd += a.Arg(i); }
  Key_SetBinding(k, cmd.c_str());
}, "Bind a key.");
static ConCommand cmd_unbind("unbind", [](const CCommand& a) { int k = Key_StringToKeynum(a.Arg(1)); if (k >= 0) Key_SetBinding(k, ""); }, "Unbind a key.");
static ConCommand cmd_unbindall("unbindall", [](const CCommand&) { s_binds.clear(); }, "Unbind all keys.");
static ConCommand cmd_key_listboundkeys("key_listboundkeys", [](const CCommand&) { for (auto& [k, v] : s_binds) Msg("\"%s\" = \"%s\"\n", Key_KeynumToString(k), v.c_str()); }, "List bound keys with bindings.");

static void DefaultBinds() {
  const char* defs[][2] = {
    { "w", "+forward" }, { "s", "+back" }, { "a", "+moveleft" }, { "d", "+moveright" }, { "SPACE", "+jump" }, { "CTRL", "+duck" }, { "SHIFT", "+speed" },
    { "e", "+use" }, { "r", "+reload" }, { "g", "drop" }, { "b", "buymenu" }, { "m", "chooseteam" }, { "TAB", "+showscores" },
    { "1", "slot1" }, { "2", "slot2" }, { "3", "slot3" }, { "4", "slot4" }, { "5", "slot5" }, { "q", "lastinv" },
    { "MOUSE1", "+attack" }, { "MOUSE2", "+attack2" }, { "MWHEELUP", "invprev" }, { "MWHEELDOWN", "invnext" },
    { "y", "messagemode" }, { "u", "messagemode2" }, { "n", "nightvision" }, { "F1", "autobuy" }, { "F2", "rebuy" }, { "F5", "screenshot" },
    { "UPARROW", "+forward" }, { "DOWNARROW", "+back" }, { "LEFTARROW", "+left" }, { "RIGHTARROW", "+right" }, { "PAUSE", "pause" },
  };
  for (auto& d : defs) Key_SetBinding(Key_StringToKeynum(d[0]), d[1]);
}

bool IN_KeyDown(int key, bool down) {
  if (key < 0 || key >= 1100) return false;
  bool wasDown = s_keyDown[key];
  s_keyDown[key] = down;
  const char* b = Key_GetBinding(key);
  if (!*b) return false;
  if (b[0] == '+') {
    std::string cmd = b;
    if (down && !wasDown) Cbuf_AddText(cmd + " " + std::to_string(key) + "\n");
    else if (!down && wasDown) { cmd[0] = '-'; Cbuf_AddText(cmd + " " + std::to_string(key) + "\n"); }
    return true;
  }
  if (down && !wasDown) Cbuf_AddText(std::string(b) + "\n");
  return true;
}

void IN_Init() { DefaultBinds(); }

void CL_SetViewAngles(const QAngle& a) { cl.viewangles = a; }
QAngle CL_GetViewAngles() { return cl.viewangles; }

// polled every frame by the main loop when the game has input focus
void IN_Frame(float dt) {
  bool gameFocus = cl.keyDest == KEYDEST_GAME;
  static const int pollKeys[] = {
    KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I, KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R, KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z,
    KEY_ZERO, KEY_ONE, KEY_TWO, KEY_THREE, KEY_FOUR, KEY_FIVE, KEY_SIX, KEY_SEVEN, KEY_EIGHT, KEY_NINE,
    KEY_TAB, KEY_ENTER, KEY_SPACE, KEY_BACKSPACE, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_LEFT_ALT, KEY_RIGHT_ALT, KEY_LEFT_CONTROL, KEY_RIGHT_CONTROL,
    KEY_LEFT_SHIFT, KEY_RIGHT_SHIFT, KEY_F1, KEY_F2, KEY_F3, KEY_F4, KEY_F5, KEY_F6, KEY_F7, KEY_F8, KEY_F9, KEY_F10, KEY_F11, KEY_F12,
    KEY_DELETE, KEY_PAGE_DOWN, KEY_PAGE_UP, KEY_HOME, KEY_END, KEY_CAPS_LOCK, KEY_PAUSE,
    KEY_KP_0, KEY_KP_1, KEY_KP_2, KEY_KP_3, KEY_KP_4, KEY_KP_5, KEY_KP_6, KEY_KP_7, KEY_KP_8, KEY_KP_9, KEY_KP_ENTER, KEY_KP_ADD, KEY_KP_SUBTRACT,
    KEY_MINUS, KEY_EQUAL, KEY_LEFT_BRACKET, KEY_RIGHT_BRACKET, KEY_BACKSLASH, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_COMMA, KEY_PERIOD, KEY_SLASH,
  };
  Touch_Frame(dt);
  const bool touch = Touch_Active();   // fingers drive look / fire: ignore the emulated mouse
  if (gameFocus) {
    for (int k : pollKeys) { bool d = IsKeyDown(k); if (d != s_keyDown[k]) IN_KeyDown(k, d); }
    static const int mb[5] = { MOUSE_BUTTON_LEFT, MOUSE_BUTTON_RIGHT, MOUSE_BUTTON_MIDDLE, MOUSE_BUTTON_SIDE, MOUSE_BUTTON_EXTRA };
    bool menuMouse = cheat::WantsMouse() || touch;
    for (int i = 0; i < 5; i++) { bool d = IsMouseButtonDown(mb[i]) && !menuMouse; if (d != s_keyDown[K_MOUSE1 + i]) IN_KeyDown(K_MOUSE1 + i, d); }
    float wheel = menuMouse ? 0.f : vgui::Input().wheel;
    if (wheel > 0) { IN_KeyDown(K_MWHEELUP, true); IN_KeyDown(K_MWHEELUP, false); }
    if (wheel < 0) { IN_KeyDown(K_MWHEELDOWN, true); IN_KeyDown(K_MWHEELDOWN, false); }
  } else {
    // release everything that is held so we don't keep running when a menu opens
    for (int k = 0; k < 1100; k++) if (s_keyDown[k]) IN_KeyDown(k, false);
  }
  // mouse look
  if (gameFocus && cl.mouseCaptured && !cheat::WantsMouse() && !touch) {
    Vector2 d = GetMouseDelta();
    static float oldx = 0, oldy = 0;
    float mx = d.x, my = d.y;
    if (m_filter.GetBool()) { float nx = (mx + oldx) * 0.5f, ny = (my + oldy) * 0.5f; oldx = mx; oldy = my; mx = nx; my = ny; }
    float sens = sensitivity.GetFloat();
    CCSPlayer* lp = g_pLocalPlayer;
    // only a real zoom slows the mouse down: ratio of the FOV actually rendered to the unzoomed one (fov_desired or the
    // cheat's FOV) - changing the FOV itself, or a scope with the zoom removed, keeps the sensitivity
    if (C_WeaponCSBase* w = lp && lp->IsAlive() ? lp->GetActiveCSWeapon() : nullptr; w && w->IsZoomed()) {
      static ConVar* fovDesired = FindConVar("fov_desired");
      const float base = cl.unzoomedFov > 0 ? cl.unzoomedFov : (fovDesired ? fovDesired->GetFloat() : 90.f);
      const float k = cl.view.fov / std::max(1.f, base);
      if (k < 0.99f) sens *= k * zoom_sensitivity_ratio.GetFloat();
    }
    cl.viewangles.y -= mx * sens * m_yaw.GetFloat();
    cl.viewangles.x += my * sens * m_pitch.GetFloat() * (m_invert.GetBool() ? -1.f : 1.f);
    cl.mouseDx += mx; cl.mouseDy += my;
  }
  // keyboard turning
  if (gameFocus) {
    float speed = dt * (IN_IsButtonDown(in_speed) ? 0.5f : 1.f);
    if (IN_IsButtonDown(in_left)) cl.viewangles.y += speed * cl_yawspeed.GetFloat();
    if (IN_IsButtonDown(in_right)) cl.viewangles.y -= speed * cl_yawspeed.GetFloat();
    if (IN_IsButtonDown(in_lookup)) cl.viewangles.x -= speed * cl_pitchspeed.GetFloat();
    if (IN_IsButtonDown(in_lookdown)) cl.viewangles.x += speed * cl_pitchspeed.GetFloat();
  }
  cl.viewangles.x = Clamp(cl.viewangles.x, -cl_pitchup.GetFloat(), cl_pitchdown.GetFloat());
  cl.viewangles.y = AngleNormalize(cl.viewangles.y);
  cl.viewangles.z = 0;
}

static int GetButtonBits() {
  int bits = 0;
  auto chk = [&](kbutton_t& b, int bit) { if (b.state & 3) bits |= bit; b.state &= 1; };  // keep "down", clear impulses
  chk(in_attack, IN_ATTACK); chk(in_attack2, IN_ATTACK2); chk(in_jump, IN_JUMP); chk(in_duck, IN_DUCK);
  chk(in_forward, IN_FORWARD); chk(in_back, IN_BACK); chk(in_moveleft, IN_MOVELEFT); chk(in_moveright, IN_MOVERIGHT);
  chk(in_speed, IN_SPEED); chk(in_use, IN_USE); chk(in_reload, IN_RELOAD); chk(in_score, IN_SCORE);
  chk(in_left, IN_LEFT); chk(in_right, IN_RIGHT);
  return bits;
}

// CInput::CreateMove: build the usercmd for this tick
void CL_CreateMove(CUserCmd& cmd) {
  CCSPlayer* lp = g_pLocalPlayer;
  cmd.viewangles = cl.viewangles;
  bool gameFocus = cl.keyDest == KEYDEST_GAME && !cl.buyMenuOpen && !cl.teamMenuOpen;
  int bits = gameFocus ? GetButtonBits() : 0;
  if (!gameFocus) { GetButtonBits(); }
  cmd.buttons = bits;
  if (bits & IN_FORWARD) cmd.forwardmove += cl_forwardspeed.GetFloat();
  if (bits & IN_BACK) cmd.forwardmove -= cl_forwardspeed.GetFloat();
  if (bits & IN_MOVERIGHT) cmd.sidemove += cl_sidespeed.GetFloat();
  if (bits & IN_MOVELEFT) cmd.sidemove -= cl_sidespeed.GetFloat();
  // touch stick (analog)
  if (gameFocus) {
    float tf, ts; Touch_GetMove(tf, ts);
    if (tf != 0 || ts != 0) {
      cmd.forwardmove += tf * cl_forwardspeed.GetFloat(); cmd.sidemove += ts * cl_sidespeed.GetFloat();
      if (tf > 0.3f) cmd.buttons |= IN_FORWARD; if (tf < -0.3f) cmd.buttons |= IN_BACK;
      if (ts > 0.3f) cmd.buttons |= IN_MOVERIGHT; if (ts < -0.3f) cmd.buttons |= IN_MOVELEFT;
    }
  }
  if (IN_IsButtonDown(in_moveup)) cmd.upmove += cl_upspeed.GetFloat();
  if (IN_IsButtonDown(in_movedown)) cmd.upmove -= cl_upspeed.GetFloat();
  if (lp && !lp->IsAlive() && lp->m_iObserverMode == OBS_MODE_ROAMING) { if (bits & IN_JUMP) cmd.upmove += cl_upspeed.GetFloat(); if (bits & IN_DUCK) cmd.upmove -= cl_upspeed.GetFloat(); }
  cmd.weaponselect = s_pendingWeaponSelect; s_pendingWeaponSelect = 0;
  cmd.impulse = (unsigned char)s_pendingImpulse; s_pendingImpulse = 0;
  cmd.mousedx = (short)cl.mouseDx; cmd.mousedy = (short)cl.mouseDy; cl.mouseDx = cl.mouseDy = 0;
  cmd.bSendPacket = true;
  // spectator controls
  if (lp && !lp->IsAlive() && lp->m_iTeamNum >= TEAM_TERRORIST && gameFocus) {
    static int lastBits = 0; int pressed = bits & ~lastBits; lastBits = bits;
    if (pressed & IN_ATTACK) cl.specTarget++;
    if (pressed & IN_ATTACK2) cl.specTarget--;
    if ((pressed & IN_JUMP) && lp->m_iObserverMode != OBS_MODE_ROAMING && lp->m_iObserverMode != OBS_MODE_DEATHCAM) Cmd_ExecuteString("spec_mode");
  }
  // cheat hook (ClientModeShared::CreateMove)
  cheat::CreateMove(cmd);
  // the client view keeps the mouse angles; silent aim changes only the cmd
}

// writes cfg/config.cfg: archived cvars followed by the key bindings
#include <fstream>
#include <filesystem>
void CL_WriteConfig() {
  std::error_code ec; std::filesystem::create_directories("cfg", ec);
  Host_WriteConfig("cfg/config.cfg");
  std::string binds; Key_WriteBindings(binds);
  std::ofstream f("cfg/config.cfg", std::ios::app);
  f << binds;
}
static ConCommand cmd_host_writeconfig("host_writeconfig", [](const CCommand&) { CL_WriteConfig(); }, "Store current settings to config.cfg.");
