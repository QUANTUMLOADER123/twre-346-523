// cheat_internal.hpp - shared state of the internal HvH cheat ("gamesense"-style)
#pragma once
#include "cheat.hpp"
#include "../engine/mathlib.hpp"
#include "../engine/convar.hpp"
#include "raylib.h"
#include <string>
#include <vector>
#include <deque>

class CCSPlayer;
class C_WeaponCSBase;
struct CNetPlayerState;
class IGameEvent;

namespace cheat {

struct Col { unsigned char r = 255, g = 255, b = 255, a = 255; operator Color() const { return { r, g, b, a }; } Col Alpha(int na) const { Col c = *this; c.a = (unsigned char)Clamp(na, 0, 255); return c; } };
inline Col MkCol(int r, int g, int b, int a = 255) { Col c; c.r = (unsigned char)r; c.g = (unsigned char)g; c.b = (unsigned char)b; c.a = (unsigned char)a; return c; }

enum KeyMode { KEYMODE_ALWAYS = 0, KEYMODE_HOLD, KEYMODE_TOGGLE, KEYMODE_OFFHOTKEY };
struct KeyBind {
  int key = 0;            // raylib key or K_MOUSE*; 0 = none
  int mode = KEYMODE_HOLD;
  bool toggled = false;
  bool Active() const;
};

struct Vars {
  struct {
    bool enabled = true; KeyBind key{ 0, KEYMODE_ALWAYS };
    int target = 0;                          // 0 crosshair 1 distance 2 health 3 cycle
    int hitboxes = 0b11111101;               // head neck chest stomach pelvis arms legs feet
    int multipoint = 0b00011101;             // which hitboxes get multipoints
    float headScale = 70, bodyScale = 60;
    bool autoFire = true, silent = true, autowall = true, autoscope = true;
    int quickStop = 1;                       // 0 off 1 full stop 2 slide (walk)
    bool quickStopBetweenShots = true;
    float hitchance = 55;
    float minDamage = 25;                    // > 100 = hp + (x - 100)
    KeyBind minDamageOverrideKey; float minDamageOverride = 1;
    int preferBaim = 1;                      // 0 off 1 lethal 2 always 3 after misses
    KeyBind forceBaimKey;
    bool safePoint = false; KeyBind forceSafeKey;
    float fov = 180;
    bool removeRecoil = true, compensateSpread = false;
    bool backtrack = true;                   // pick the best lag record
    bool delayShot = false;
    bool doubletap = true; KeyBind doubletapKey{ KEY_X, KEYMODE_TOGGLE }; int doubletapMode = 0; // 0 offensive 1 defensive
    bool hideshots = false; KeyBind hideshotsKey{ 0, KEYMODE_TOGGLE };
    bool logMisses = true;
    bool knifebot = true, zeusbot = false;
    int resolver = 1;                        // 0 off 1 default 2 brute only
  } rage;
  struct {
    bool enabled = true;
    int pitch = 1;                           // off down up zero random fake-up
    int yawBase = 1;                         // local view / at targets
    int yaw = 1;                             // off 180 spin static 180z
    float yawAdd = 0;
    int jitter = 2;                          // off offset center random
    float jitterRange = 24;
    float spinSpeed = 30;
    int bodyYaw = 1;                         // off static jitter opposite
    KeyBind inverter{ 0, KEYMODE_TOGGLE };
    float fakeLimit = 58;
    bool freestandBody = true;
    KeyBind freestandKey;
    bool edgeYaw = false;
    KeyBind manualLeft{ 0, KEYMODE_TOGGLE }, manualRight{ 0, KEYMODE_TOGGLE }, manualBack{ 0, KEYMODE_TOGGLE };
    int lbyMode = 1;                         // off / opposite / sway
    bool fakelag = true; int fakelagMode = 0; // 0 dynamic 1 maximum 2 fluctuate
    float fakelagLimit = 14, fakelagVariance = 20;
    bool fakelagOnShot = false;
    KeyBind fakeDuckKey;
    KeyBind slowWalkKey; float slowWalkSpeed = 34;
    int legMovement = 1;                     // off / always slide / never slide
    bool onShotAA = false;
  } aa;
  struct {
    bool teammates = false;
    bool box = true; Col boxCol = MkCol(255, 255, 255, 200);
    bool name = true; Col nameCol = MkCol(255, 255, 255, 220);
    bool health = true; bool weapon = true; bool ammo = true; Col ammoCol = MkCol(80, 140, 250);
    bool flags = true; bool skeleton = false; Col skeletonCol = MkCol(255, 255, 255, 160);
    bool oof = true; Col oofCol = MkCol(255, 255, 255, 200); float oofRadius = 180;
    bool hitboxOnShot = true; Col hitboxCol = MkCol(150, 200, 60, 140);
    bool glow = false; Col glowCol = MkCol(180, 60, 120, 150);
    bool radarReveal = true;
    // chams
    bool chams = true; Col chamsCol = MkCol(150, 200, 60);
    bool chamsXqz = true; Col chamsXqzCol = MkCol(60, 120, 180);
    int chamsMaterial = 1;                   // default flat glow wireframe
    bool chamsTeam = false; Col chamsTeamCol = MkCol(90, 160, 255);
    bool chamsBacktrack = true; Col chamsBacktrackCol = MkCol(255, 255, 255, 60);
    bool chamsLocal = false; Col chamsLocalCol = MkCol(255, 255, 255, 255);
    bool chamsFake = true; Col chamsFakeCol = MkCol(255, 255, 255, 70);
    float scopeBlend = 40;                   // local model transparency in scope
    // world/view
    bool removeFlash = true, removeSmoke = true, removeScope = true, removeRecoil = true;
    float nightmode = 0;
    KeyBind thirdpersonKey{ 0, KEYMODE_TOGGLE }; float thirdpersonDist = 140; bool thirdpersonDead = true;
    bool forceCrosshair = true; bool penCrosshair = true;
    bool tracers = true; Col tracerCol = MkCol(150, 130, 255, 220);
    bool impacts = false;
    bool grenadePrediction = true; Col grenadeCol = MkCol(150, 200, 60, 220);
    bool spreadCircle = false;
    float fov = 0, zoomFov = 0, viewmodelFov = 68;
    bool hitmarker3d = true; bool damageMarker = true;
    bool indicators = true; bool aaArrows = true; Col arrowCol = MkCol(150, 200, 60);
    bool watermark = true; bool keybindList = true; bool spectatorList = true;
    bool droppedWeapons = false; bool bombEsp = true; bool grenadeEsp = true;
  } vis;
  struct {
    bool bhop = true; int autostrafe = 2; // off legit directional rage
    KeyBind edgeJumpKey;
    bool fastStop = true;
    int buyPrimary = 1;  // off scar20/g3sg1 ssg08 awp ak47/m4
    int buySecondary = 1; // off deagle/r8 duals p250
    bool buyArmor = true, buyNades = true, buyKit = true;
    bool hitsound = true; int hitsoundType = 0;
    bool killsay = false;
    bool logHits = true, logHurt = true, logPurchases = false;
    bool clantag = false;
    Col accent = MkCol(150, 200, 60);
    KeyBind menuKey{ KEY_INSERT, KEYMODE_TOGGLE };
    int dpi = 0; // 100 125 150
  } misc;
};
extern Vars v;

// player list overrides (index = entindex)
struct PlayerSettings { int resolverOverride = 0; bool forceBaim = false; bool whitelist = false; bool highPriority = false; };
extern PlayerSettings g_PlayerList[65];

// shared runtime state
struct ShotRecord { int target; int hitbox; Vector aimPoint; float time; int tick; bool resolved; float resolvedYaw; int side; bool acked; bool hit; float damage; int hitchance; int backtrackTicks; bool safe; };
struct RuntimeState {
  bool sendPacket = true;
  int choked = 0;
  QAngle realAngles, fakeAngles, viewAngles; // last cmd angles
  float realFootYaw = 0, fakeFootYaw = 0;
  bool doubletapping = false, hideshots = false, fakeducking = false, slowwalking = false;
  int manualSide = 0;                      // 0 none -1 left 1 right 2 back
  int desyncSide = 1;
  bool freestandingActive = false;
  CCSPlayer* target = nullptr; int targetHitbox = -1; Vector targetPoint; bool targetValid = false;
  std::deque<ShotRecord> shots;
  float lastShotTime = -100;
  int dtCharge = 0; bool dtReady = false;
  bool menuOpen = false;
};
extern RuntimeState g;

// log line (top-left "gamesense" event log)
void Log(Col c, const char* fmt, ...);
void DrawLogs();

// menu
void Menu_Paint();
void Menu_Init();
bool Menu_IsCapturingKey();
void Config_Save(const std::string& name);
bool Config_Load(const std::string& name);
std::vector<std::string> Config_List();

// features
void Rage_CreateMove(CUserCmd& cmd, CCSPlayer* lp, const QAngle& viewAngles);

void Rage_FrameStage();
void Rage_OnEvent(const IGameEvent& ev);
void Misc_OnEvent(const IGameEvent& ev);
void Visuals_OnEvent(const IGameEvent& ev);
void Rage_LevelInit();
void Visuals_LevelInit();
void Resolver_Update(CCSPlayer* p);
float Resolver_GetYaw(CCSPlayer* p, const CNetPlayerState& rec, int* sideOut);
void AA_CreateMove(CUserCmd& cmd, CCSPlayer* lp, const QAngle& viewAngles, bool shooting);
void AA_FakeLag(CUserCmd& cmd, CCSPlayer* lp, bool shooting);
void Misc_CreateMove(CUserCmd& cmd, CCSPlayer* lp, const QAngle& viewAngles);

void Misc_AutoBuy();
void Visuals_Paint();
void Visuals_DrawWorld3D(const CViewSetup& view);

void FixMovement(CUserCmd& cmd, const QAngle& wishAngles);
bool InGameInput();   // keyboard currently goes to the game (not chat/console/menus)

} // namespace cheat
