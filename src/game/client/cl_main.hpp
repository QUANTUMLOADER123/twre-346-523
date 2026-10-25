// cl_main.hpp - client.dll state: input, view, rendering, effects, sounds, HUD
#pragma once
#include "../../engine/globals.hpp"
#include "../../engine/view.hpp"
#include "../../engine/convar.hpp"
#include <string>
#include <vector>
#include <functional>

class CCSPlayer;
class C_BaseEntity;

// --- input (in_main / in_mouse) ---------------------------------------------------------------
struct kbutton_t { int down[2] = { 0, 0 }; int state = 0; };
enum KeyDest { KEYDEST_GAME = 0, KEYDEST_CONSOLE, KEYDEST_MENU, KEYDEST_MESSAGE, KEYDEST_CHEATMENU };
void IN_Init();
void IN_Frame(float dt);                        // poll raylib, dispatch binds, accumulate mouse
bool IN_KeyDown(int key, bool down);            // handle one key event (binds); returns true if consumed
const char* Key_KeynumToString(int key);
int Key_StringToKeynum(const char* str);
const char* Key_GetBinding(int key);
void Key_SetBinding(int key, const char* cmd);
int Key_FindBinding(const char* cmd);           // first key bound to cmd, -1 if none
void Key_WriteBindings(std::string& out);
void Key_ClearStates();
bool IN_IsButtonDown(kbutton_t& b);
extern kbutton_t in_attack, in_attack2, in_jump, in_duck, in_forward, in_back, in_moveleft, in_moveright, in_speed, in_use, in_reload, in_score, in_left, in_right;
void CL_SetViewAngles(const QAngle& a);
QAngle CL_GetViewAngles();
int KEY_MOUSE1_CODE();
constexpr int K_MOUSE1 = 1001, K_MOUSE2 = 1002, K_MOUSE3 = 1003, K_MOUSE4 = 1004, K_MOUSE5 = 1005, K_MWHEELUP = 1010, K_MWHEELDOWN = 1011;

// --- client state ------------------------------------------------------------------------------
struct ClientState {
  QAngle viewangles;           // client view angles (what the mouse controls)
  bool thirdperson = false;
  bool showScores = false;
  bool buyMenuOpen = false, teamMenuOpen = false, chatOpen = false, chatTeam = false;
  std::string chatBuffer;
  float mouseDx = 0, mouseDy = 0;
  int keyDest = KEYDEST_GAME;
  bool mouseCaptured = false;
  // view
  CViewSetup view;
  Vector viewOrigin;
  QAngle viewAngles;           // final render angles (with punch)
  float fov = 90;
  float zoomFov = 90;
  float frameAlpha = 0;
  bool firstPerson = true;
  CCSPlayer* viewEntity = nullptr;  // whose eyes we're looking through
  // stats
  float fps = 0, frameMs = 0;
  int lastHitTick = -100;
  float hitmarkerTime = -100; int hitmarkerDmg = 0; bool hitmarkerHead = false;
  float lastDamageTakenTime = -100; Vector lastDamageDir;
  float roundStartRealtime = 0;
  // spectating
  int specTarget = 0;
  bool nightvision = false;
  // cheat-controlled render overrides (set by the cheat every frame)
  bool removeScope = false, removeVisualRecoil = false, removeFlash = false, removeSmoke = false;
  float viewmodelFov = 68.f;
  float overrideFov = 0;       // 0 = default
  float worldBrightness = 1.f;  // nightmode
  bool forceCrosshair = false;
  float thirdpersonDist = 120.f;
};
inline ClientState cl;

void CL_Init();
void CL_Shutdown();
void CL_WriteConfig();
void CL_Frame(float dt);             // per render frame client logic (after Host_Frame)
void CL_RenderFrame();               // 3D + 2D
void CL_RenderMenuBackground(float time);
void CL_ComputeView(CViewSetup& view);
void CL_DrawEntities(const CViewSetup& view);
void CL_DrawViewModel(const CViewSetup& view);
CCSPlayer* CL_GetViewPlayer();
bool CL_IsInGame();

// remote player interpolation: lerped lag-record state at the client's render time
struct InterpState { Vector origin; QAngle eyeAngles; float footYaw = 0, duck = 0, moveCycle = 0, moveSpeed = 0, moveYaw = 0; bool onGround = true; int weaponId = 0; float fireAmount = 0, reloadCycle = -1, land = 0; bool valid = false; };
bool CL_GetInterpolatedState(CCSPlayer* p, InterpState& out);
float CL_GetRenderTime();            // server time being rendered (with interpolation)

// --- sounds (synthesized) ----------------------------------------------------------------------
void S_Init();
void S_Shutdown();
void S_Update(const Vector& listener, const QAngle& angles);
void S_StopAll();

// --- effects -----------------------------------------------------------------------------------
void FX_Init();
void FX_Clear();
void FX_Update(float dt);
void FX_DrawOpaque(const CViewSetup& v);
void FX_DrawTranslucent(const CViewSetup& v);
void FX_Tracer(const Vector& start, const Vector& end, Color c, float width = 1.f, float life = 0.12f);
void FX_BeamTracer(const Vector& start, const Vector& end, Color c, float life); // cheat bullet tracers
void FX_Impact(const Vector& pos, const Vector& normal, int surfaceProp, bool player);
void FX_MuzzleFlash(const Vector& pos, const Vector& dir, float scale);
void FX_Explosion(const Vector& pos);
void FX_Blood(const Vector& pos, const Vector& dir);
void FX_SmokeCloud(const Vector& center, float start, float duration);
void FX_Spark(const Vector& pos, int count);
void FX_DebugBox(const Vector& mins, const Vector& maxs, Color c, float life);
void FX_HitboxCapsules(CCSPlayer* p, const void* hitboxSet, Color c, float life); // cheat "shot capsules"
void FX_DrawWorldText(const CViewSetup& v);

// --- HUD ---------------------------------------------------------------------------------------
void HUD_Init();
void HUD_LevelInit();
void HUD_Paint();                    // all HUD elements
void HUD_AddChat(const std::string& who, const std::string& text, int team, bool teamOnly, bool dead);
void HUD_CenterPrint(const std::string& text, float time = 3.f);
void HUD_HintText(const std::string& text);
bool HUD_ShouldDraw();

// viewmodel state
void VM_Reset();
void VM_Update(float dt);

// level
void CL_LevelInitRender(); // builds world render data
extern std::function<void()> g_CheatPaintHook;
