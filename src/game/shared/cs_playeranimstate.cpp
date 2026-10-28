#include "cs_playeranimstate.hpp"
#include "weapon_parse.hpp"
#include "../../engine/globals.hpp"
#include "../../engine/convar.hpp"
#include <unordered_map>
#include <cmath>


// ------------------------------------------------------------------------------------------------
// CCSGOPlayerAnimState
void CCSGOPlayerAnimState::Reset(float yaw) {
  *this = CCSGOPlayerAnimState();
  m_flFootYaw = m_flEyeYaw = m_flLowerBodyYawTarget = m_flFootYawLast = AngleNormalize(yaw);
}
float CCSGOPlayerAnimState::GetMaxDesync() const {
  float speedFrac = Clamp(m_flSpeedAsPortionOfRunTopSpeed, 0.f, 1.f);
  float yawModifier = (((m_flWalkToRunTransition * -0.3f) - 0.2f) * speedFrac) + 1.0f;
  if (m_flDuckAmount > 0) yawModifier += (m_flDuckAmount * Clamp(m_flSpeedAsPortionOfCrouchTopSpeed, 0.f, 1.f)) * (0.5f - yawModifier);
  return 58.f * yawModifier;
}
void CCSGOPlayerAnimState::Update(const QAngle& eye, const Vector& vel, bool onGround, float duck, float maxSpeed, float dt, float curtime) {
  if (dt <= 0) dt = gpGlobals->interval_per_tick;
  m_flEyeYaw = AngleNormalize(eye.y);
  m_flEyePitch = Clamp(AngleNormalize(eye.x), -89.f, 89.f);
  m_flDuckAmount = Clamp(duck, 0.f, 1.f);
  m_flVelocityLengthXY = std::min(vel.Length2D(), 260.f);
  float maxRun = std::max(1.f, maxSpeed);
  m_flSpeedAsPortionOfRunTopSpeed = Clamp(m_flVelocityLengthXY / maxRun, 0.f, 1.f);
  m_flSpeedAsPortionOfWalkTopSpeed = m_flVelocityLengthXY / (maxRun * 0.52f);
  m_flSpeedAsPortionOfCrouchTopSpeed = m_flVelocityLengthXY / (maxRun * 0.34f);
  // walk to run transition
  if (m_flSpeedAsPortionOfWalkTopSpeed > 1.f) m_flWalkToRunTransition = std::min(1.f, m_flWalkToRunTransition + dt * 2.f);
  else m_flWalkToRunTransition = std::max(0.f, m_flWalkToRunTransition - dt * 2.f);
  bool moving = m_flVelocityLengthXY > 0.1f;
  if (moving) { m_flTimeSinceStartedMoving += dt; m_flTimeSinceStoppedMoving = 0; } else { m_flTimeSinceStoppedMoving += dt; m_flTimeSinceStartedMoving = 0; }

  // --- feet yaw (the CS:GO desync logic) ---
  m_flFootYawLast = m_flFootYaw;
  float maxYaw = GetMaxDesync(), minYaw = -maxYaw;
  float delta = AngleDiff(m_flEyeYaw, m_flFootYaw);
  if (delta > maxYaw) m_flFootYaw = m_flEyeYaw - std::fabs(maxYaw);
  else if (delta < minYaw) m_flFootYaw = m_flEyeYaw + std::fabs(minYaw);
  m_flFootYaw = AngleNormalize(m_flFootYaw);
  if (moving || std::fabs(vel.z) > 100.f) m_flFootYaw = ApproachAngle(m_flEyeYaw, m_flFootYaw, dt * (30.f + 20.f * m_flWalkToRunTransition));
  else m_flFootYaw = ApproachAngle(m_flLowerBodyYawTarget, m_flFootYaw, dt * 100.f);

  // --- lower body yaw target (lby) ---
  m_bLbyUpdated = false;
  if (onGround) {
    if (moving) { m_flLowerBodyYawTarget = m_flEyeYaw; m_flNextLbyUpdateTime = curtime + 0.22f; }
    else if (curtime > m_flNextLbyUpdateTime) {
      if (std::fabs(AngleDiff(m_flFootYaw, m_flEyeYaw)) > 35.f) { m_flLowerBodyYawTarget = m_flEyeYaw; m_bLbyUpdated = true; }
      m_flNextLbyUpdateTime = curtime + 1.1f;
    }
  }
  // --- movement cycle / direction ---
  if (moving) {
    float velYaw = RAD2DEGF(std::atan2(vel.y, vel.x));
    m_flMoveYaw = AngleDiff(velYaw, m_flFootYaw);
    float stride = duck > 0.5f ? 68.f : m_flVelocityLengthXY <= 130.f ? 139.f : 139.f + std::min(1.f, (m_flVelocityLengthXY - 130.f) / 120.f) * 28.f;
    m_flMoveCycle += dt * m_flVelocityLengthXY / stride;
    m_flMoveCycle -= std::floor(m_flMoveCycle);
  }
  // --- air / landing ---
  if (!onGround) { m_flAirTime += dt; m_bLanding = false; }
  else { if (!m_bOnGround && m_flAirTime > 0.15f) { m_bLanding = true; m_flLandAmount = Clamp(m_flAirTime * 1.5f, 0.3f, 1.f); } m_flAirTime = 0; }
  m_flLandAmount = std::max(0.f, m_flLandAmount - dt * 3.f);
  m_bOnGround = onGround;
  // --- lean (strafe acceleration) ---
  Vector accel = (vel - m_vecVelocityLast) / dt; m_vecVelocityLast = vel;
  Vector right(std::sin(DEG2RADF(m_flFootYaw)), -std::cos(DEG2RADF(m_flFootYaw)), 0);
  float targetLean = Clamp(accel.Dot(right) / 2000.f, -1.f, 1.f) * 8.f;
  m_flLeanAmount = Approach(targetLean, m_flLeanAmount, dt * 30.f);
  m_flLastUpdateTime = curtime;
  m_bFirstUpdate = false;
}


// ------------------------------------------------------------------------------------------------
static const int s_hitgroup[19] = { HITGROUP_HEAD, HITGROUP_HEAD, HITGROUP_STOMACH, HITGROUP_STOMACH, HITGROUP_CHEST, HITGROUP_CHEST, HITGROUP_CHEST,
  HITGROUP_RIGHTLEG, HITGROUP_LEFTLEG, HITGROUP_RIGHTLEG, HITGROUP_LEFTLEG, HITGROUP_RIGHTLEG, HITGROUP_LEFTLEG,
  HITGROUP_RIGHTARM, HITGROUP_LEFTARM, HITGROUP_RIGHTARM, HITGROUP_RIGHTARM, HITGROUP_LEFTARM, HITGROUP_LEFTARM };
static const char* s_hbname[19] = { "head", "neck", "pelvis", "stomach", "lower chest", "chest", "upper chest", "right thigh", "left thigh", "right calf", "left calf",
  "right foot", "left foot", "right hand", "left hand", "right upper arm", "right forearm", "left upper arm", "left forearm" };
int HitboxToHitgroup(int hb) { return (hb >= 0 && hb < 19) ? s_hitgroup[hb] : HITGROUP_GENERIC; }
const char* HitboxName(int hb) { return (hb >= 0 && hb < 19) ? s_hbname[hb] : "generic"; }


int IntersectHitboxes(const HitboxSet& hb, const Vector& start, const Vector& delta, float& tOut) {
  int best = -1; float bt = 2.f;
  for (int i = 0; i < 19; i++) {
    float t;
    if (IntersectSegmentCapsule(start, delta, hb[i].a, hb[i].b, hb[i].radius, t) && t < bt) { bt = t; best = i; }
  }
  if (best >= 0) tOut = bt;
  return best;
}
