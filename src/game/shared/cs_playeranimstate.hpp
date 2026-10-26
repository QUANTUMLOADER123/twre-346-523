// cs_playeranimstate.hpp - CCSGOPlayerAnimState (feet yaw / desync / lby) + procedural pose builder + hitboxes
#pragma once
#include "../../engine/mathlib.hpp"
#include "../../engine/studio.hpp"
#include <array>

class CCSPlayer;
class CSrcModel;
#include <vector>

struct mstudiobbox_t {      // world-space capsule hitbox
  Vector a, b;
  float radius = 0;
  int group = 0;
};
using HitboxSet = std::array<mstudiobbox_t, 19>;

class CCSGOPlayerAnimState {
public:
  void Reset(float yaw);
  // called once per processed usercmd (server) with that command's angles
  void Update(const QAngle& eyeAngles, const Vector& velocity, bool onGround, float duckAmount, float maxSpeed, float dt, float curtime);
  float GetMaxDesync() const;   // current max body yaw (degrees)

  float m_flFootYaw = 0;        // goal feet yaw (abs yaw of the model)
  float m_flFootYawLast = 0;
  float m_flEyeYaw = 0;
  float m_flEyePitch = 0;
  float m_flLowerBodyYawTarget = 0;
  float m_flNextLbyUpdateTime = 0;
  bool m_bLbyUpdated = false;   // true on the tick the lby flicked (for resolvers/logs)
  float m_flVelocityLengthXY = 0;
  float m_flSpeedAsPortionOfRunTopSpeed = 0;
  float m_flSpeedAsPortionOfWalkTopSpeed = 0;
  float m_flSpeedAsPortionOfCrouchTopSpeed = 0;
  float m_flWalkToRunTransition = 0;
  float m_flDuckAmount = 0;
  float m_flMoveCycle = 0;      // walk/run cycle (0..1)
  float m_flMoveYaw = 0;        // movement direction relative to feet
  float m_flTimeSinceStartedMoving = 0, m_flTimeSinceStoppedMoving = 0;
  bool m_bOnGround = true, m_bLanding = false;
  float m_flAirTime = 0, m_flLandAmount = 0;
  float m_flLeanAmount = 0;
  Vector m_vecVelocityLast;
  float m_flLastUpdateTime = 0;
  bool m_bFirstUpdate = true;
};

// everything needed to pose a player model for one frame / one lag record
struct PlayerPoseInput {
  Vector origin;
  float footYaw = 0;
  QAngle eyeAngles;
  float duckAmount = 0;
  float moveCycle = 0;
  float moveSpeed = 0;   // 0..1 portion of run speed
  float moveYaw = 0;     // movement direction relative to feet (deg)
  bool onGround = true;
  float landAmount = 0;
  int weaponId = 0;
  float fireAmount = 0;  // 1 right after a shot -> 0
  float reloadCycle = -1; // 0..1 while reloading
  float deathCycle = -1;  // 0..1 while dying / 1 dead
  float deathYaw = 0;
  float deathTime = 0;    // seconds since death
  float leanRoll = 0;
  float animTime = -1;    // clock for idle animations (-1 = gpGlobals->curtime); the main menu agent uses real time
  bool planting = false, defusing = false;
  bool viewmodel = false; // first-person arms: hold knife/pistol/grenade in front of the eyes
};

struct PlayerPoseOutput {
  CStudioPose pose;
  matrix3x4_t modelToWorld;
  matrix3x4_t weaponToWorld;
  bool hasWeapon = false;
  HitboxSet hitboxes;
  Vector eyePos;
  // Counter-Strike: Source model (client only, see css_models.hpp)
  const CSrcModel* css = nullptr; std::vector<matrix3x4_t> cssBones; matrix3x4_t cssToWorld;
  const CSrcModel* cssWeapon = nullptr; std::vector<matrix3x4_t> cssWeaponBones;
  Vector muzzle; bool hasMuzzle = false;   // carried weapon muzzle (world)
};

// poses the player's Counter-Strike: Source model (bones, bone-merged weapon, hitboxes from the model's hitbox set);
// a pure function of the input, used by the server (hitboxes / lag records), the cheat and the renderer
// (implemented in client/css_models.cpp)
void BuildPlayerPose(CCSPlayer* p, const PlayerPoseInput& in, PlayerPoseOutput& out, bool computeHitboxes = true);
int HitboxToHitgroup(int hitbox);
const char* HitboxName(int hitbox);
// ray vs hitbox set; returns hitbox index or -1, t in [0,1]
int IntersectHitboxes(const HitboxSet& hb, const Vector& start, const Vector& delta, float& tOut);

