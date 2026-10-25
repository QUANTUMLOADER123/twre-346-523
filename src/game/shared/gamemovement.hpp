// gamemovement.hpp - CGameMovement (Source movement: friction/accelerate/airaccelerate/stepmove/duck/jump)
#pragma once
#include "../../engine/mathlib.hpp"
#include "../../engine/globals.hpp"
#include "../../engine/enginetrace.hpp"

class CCSPlayer;

class CMoveData {
public:
  bool m_bFirstRunOfFunctions = true;
  bool m_bGameCodeMovedPlayer = false;
  QAngle m_vecViewAngles;
  QAngle m_vecAbsViewAngles;
  int m_nButtons = 0;
  int m_nOldButtons = 0;
  float m_flForwardMove = 0;
  float m_flSideMove = 0;
  float m_flUpMove = 0;
  float m_flMaxSpeed = 0;
  float m_flClientMaxSpeed = 0;
  Vector m_vecVelocity;
  QAngle m_vecAngles;
  QAngle m_vecOldAngles;
  float m_outStepHeight = 0;
  Vector m_outWishVel;
  Vector m_outJumpVel;
  Vector m_vecAbsOrigin;
};

class CGameMovement {
public:
  void ProcessMovement(CCSPlayer* player, CMoveData* mv, float frametime);
  // queries usable by cheats / bots
  static Vector GetPlayerMins(bool ducked) { return ducked ? Vector(-16, -16, 0) : Vector(-16, -16, 0); }
  static Vector GetPlayerMaxs(bool ducked) { return ducked ? Vector(16, 16, 54) : Vector(16, 16, 72); }
  static Vector GetPlayerViewOffset(bool ducked) { return ducked ? Vector(0, 0, 46) : Vector(0, 0, 64); }
  static constexpr float VEC_DUCK_DIFF = 18.f;

protected:
  void PlayerMove();
  void FullWalkMove();
  void FullNoClipMove(float factor, float maxacceleration);
  void WalkMove();
  void AirMove();
  void Friction();
  void Accelerate(const Vector& wishdir, float wishspeed, float accel);
  void AirAccelerate(const Vector& wishdir, float wishspeed, float accel);
  void StepMove(const Vector& dest, trace_t& trace);
  int TryPlayerMove(Vector* firstDest = nullptr, trace_t* firstTrace = nullptr);
  int ClipVelocity(const Vector& in, const Vector& normal, Vector& out, float overbounce);
  bool CheckJumpButton();
  void CheckVelocity();
  void StartGravity();
  void FinishGravity();
  void CategorizePosition();
  void SetGroundEntity(trace_t* pm);
  void StayOnGround();
  void Duck();
  void FinishDuck();
  void FinishUnDuck();
  bool CanUnduck();
  void CheckFalling();
  void PlayerRoughLandingEffects(float fvol);
  void TracePlayerBBox(const Vector& start, const Vector& end, unsigned mask, trace_t& pm);
  Vector GetMins() const;
  Vector GetMaxs() const;
  void ReduceTimers();

  CCSPlayer* player = nullptr;
  CMoveData* mv = nullptr;
  float m_flFrameTime = 0;
  Vector m_vecForward, m_vecRight, m_vecUp;
  float m_flSurfaceFriction = 1.f;
};
inline CGameMovement g_GameMovement;
inline CGameMovement* gamemovement = &g_GameMovement;
