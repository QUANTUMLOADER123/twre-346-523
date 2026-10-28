#include "gamemovement.hpp"
#include "cs_player.hpp"
#include "../../engine/convar.hpp"
#include <cmath>

ConVar sv_gravity("sv_gravity", "800", FCVAR_REPLICATED | FCVAR_NOTIFY, "World gravity");
ConVar sv_friction("sv_friction", "5.2", FCVAR_REPLICATED | FCVAR_NOTIFY, "World friction");
ConVar sv_stopspeed("sv_stopspeed", "80", FCVAR_REPLICATED | FCVAR_NOTIFY, "Minimum stopping speed when on ground");
ConVar sv_accelerate("sv_accelerate", "5.5", FCVAR_REPLICATED | FCVAR_NOTIFY, "Linear acceleration amount");
ConVar sv_airaccelerate("sv_airaccelerate", "12", FCVAR_REPLICATED | FCVAR_NOTIFY, "Air acceleration");
ConVar sv_air_max_wishspeed("sv_air_max_wishspeed", "46", FCVAR_REPLICATED, "Max wish speed in air (air strafe gain per tick; 46 -> about 250 to 400 u/s in one jump)");
ConVar sv_maxspeed("sv_maxspeed", "320", FCVAR_REPLICATED | FCVAR_NOTIFY, "Maximum speed");
ConVar sv_maxvelocity("sv_maxvelocity", "3500", FCVAR_REPLICATED, "Maximum speed any ballistically moving object is allowed to attain per axis");
ConVar sv_stepsize("sv_stepsize", "18", FCVAR_REPLICATED, "Step height");
ConVar sv_jump_impulse("sv_jump_impulse", "301.993377", FCVAR_REPLICATED, "Initial upward velocity for player jumps");
ConVar sv_enablebunnyhopping("sv_enablebunnyhopping", "1", FCVAR_REPLICATED | FCVAR_NOTIFY, "Allow player speed to exceed maximum running speed when jumping");
ConVar sv_bhop_reset("sv_bhop_reset", "1", FCVAR_REPLICATED | FCVAR_NOTIFY, "Touching the ground resets the horizontal speed to the running speed (air strafe speed has to be built up again every jump)");
ConVar sv_autobunnyhopping("sv_autobunnyhopping", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "Players automatically re-jump while holding jump button");
ConVar sv_falldamage_scale("sv_falldamage_scale", "1", FCVAR_REPLICATED, "Fall damage scale");
ConVar sv_noclipspeed("sv_noclipspeed", "5", FCVAR_ARCHIVE, "Noclip speed factor");
ConVar sv_duck_speed("sv_duck_speed", "6", FCVAR_REPLICATED, "Duck amount change per second");

static constexpr float NON_JUMP_VELOCITY = 140.f;
static constexpr float DIST_EPSILON = 0.03125f;
static constexpr float PLAYER_MAX_SAFE_FALL_SPEED = 580.f;
static constexpr float PLAYER_FATAL_FALL_SPEED = 1024.f;
static constexpr float PLAYER_MIN_BOUNCE_SPEED = 200.f;
static constexpr int MAX_CLIP_PLANES = 5;

Vector CGameMovement::GetMins() const { return GetPlayerMins(player->m_bDucked); }
Vector CGameMovement::GetMaxs() const { return GetPlayerMaxs(player->m_bDucked); }

void CGameMovement::TracePlayerBBox(const Vector& start, const Vector& end, unsigned mask, trace_t& pm) {
  Ray_t ray; ray.Init(start, end, GetMins(), GetMaxs());
  CTraceFilter filter(player);
  g_EngineTrace.TraceRay(ray, mask, &filter, &pm);
}

void CGameMovement::ProcessMovement(CCSPlayer* pl, CMoveData* m, float ft) {
  player = pl; mv = m; m_flFrameTime = ft;
  mv->m_vecVelocity = player->m_vecVelocity;
  mv->m_vecAbsOrigin = player->m_vecOrigin;
  m_flSurfaceFriction = player->m_surfaceFriction;
  PlayerMove();
  player->m_vecVelocity = mv->m_vecVelocity;
  player->m_vecOrigin = mv->m_vecAbsOrigin;
  player->m_vecMins = GetMins(); player->m_vecMaxs = GetMaxs();
}

void CGameMovement::ReduceTimers() {
  if (player->m_flStamina > 0) player->m_flStamina = std::max(0.f, player->m_flStamina - 1000.f * m_flFrameTime / 1000.f * 60.f);
}

void CGameMovement::PlayerMove() {
  // CheckParameters
  if (player->m_MoveType != MOVETYPE_NOCLIP) {
    float spd = mv->m_flForwardMove * mv->m_flForwardMove + mv->m_flSideMove * mv->m_flSideMove + mv->m_flUpMove * mv->m_flUpMove;
    if (spd != 0 && spd > mv->m_flMaxSpeed * mv->m_flMaxSpeed) {
      float ratio = mv->m_flMaxSpeed / std::sqrt(spd);
      mv->m_flForwardMove *= ratio; mv->m_flSideMove *= ratio; mv->m_flUpMove *= ratio;
    }
  }
  if ((player->m_fFlags & FL_FROZEN) || (!player->IsAlive() && player->m_iObserverMode != OBS_MODE_ROAMING)) { mv->m_flForwardMove = mv->m_flSideMove = mv->m_flUpMove = 0; }
  ReduceTimers();
  AngleVectors(mv->m_vecViewAngles, &m_vecForward, &m_vecRight, &m_vecUp);
  if (!(player->m_fFlags & FL_ONGROUND)) player->m_flFallVelocity = -mv->m_vecVelocity.z;
  if (player->m_MoveType != MOVETYPE_NOCLIP && player->IsAlive()) Duck();
  switch (player->m_MoveType) {
    case MOVETYPE_NOCLIP: FullNoClipMove(sv_noclipspeed.GetFloat(), 5.f); break;
    case MOVETYPE_NONE: break;
    default: FullWalkMove(); break;
  }
}

void CGameMovement::FullNoClipMove(float factor, float maxacceleration) {
  Vector wishvel = m_vecForward * mv->m_flForwardMove + m_vecRight * mv->m_flSideMove;
  wishvel.z += mv->m_flUpMove;
  Vector wishdir = wishvel; float wishspeed = wishdir.NormalizeInPlace();
  float maxspeed = sv_maxspeed.GetFloat() * factor;
  if (mv->m_nButtons & IN_SPEED) maxspeed *= 0.3f;
  if (wishspeed > maxspeed) wishspeed = maxspeed;
  if (maxacceleration > 0) {
    Accelerate(wishdir, wishspeed, maxacceleration);
    float spd = mv->m_vecVelocity.Length();
    if (spd < 1.f) mv->m_vecVelocity.Init();
    else { float friction = sv_friction.GetFloat() * 0.5f; float control = spd < maxspeed / 4 ? maxspeed / 4 : spd; float drop = control * friction * m_flFrameTime; float ns = std::max(0.f, spd - drop); mv->m_vecVelocity *= ns / spd; }
  } else mv->m_vecVelocity = wishdir * wishspeed;
  mv->m_vecAbsOrigin += mv->m_vecVelocity * m_flFrameTime;
  if (wishspeed == 0 && maxacceleration <= 0) mv->m_vecVelocity.Init();
  player->m_fFlags &= ~FL_ONGROUND;
}

void CGameMovement::StartGravity() {
  float g = player->m_flGravity ? player->m_flGravity : 1.f;
  mv->m_vecVelocity.z -= g * sv_gravity.GetFloat() * 0.5f * m_flFrameTime;
  mv->m_vecVelocity.z += player->m_vecBaseVelocity.z * m_flFrameTime;
  player->m_vecBaseVelocity.z = 0;
  CheckVelocity();
}
void CGameMovement::FinishGravity() {
  float g = player->m_flGravity ? player->m_flGravity : 1.f;
  mv->m_vecVelocity.z -= g * sv_gravity.GetFloat() * m_flFrameTime * 0.5f;
  CheckVelocity();
}
void CGameMovement::CheckVelocity() {
  float mx = sv_maxvelocity.GetFloat();
  for (int i = 0; i < 3; i++) {
    if (std::isnan(mv->m_vecVelocity[i])) mv->m_vecVelocity[i] = 0;
    if (std::isnan(mv->m_vecAbsOrigin[i])) mv->m_vecAbsOrigin[i] = 0;
    mv->m_vecVelocity[i] = Clamp(mv->m_vecVelocity[i], -mx, mx);
  }
}

// Source CGameMovement::CheckStuck: the box ended up a hair inside the world (slope seams, displacement edges) - every
// trace then starts solid and the player freezes. Nudge it out to the nearest free spot (up first, then sideways)
bool CGameMovement::CheckStuck() {
  trace_t t; TracePlayerBBox(mv->m_vecAbsOrigin, mv->m_vecAbsOrigin, MASK_PLAYERSOLID, t);
  if (!t.startsolid) return false;
  static const float kUp[] = { 0.125f, 0.25f, 0.5f, 1.f, 2.f, 4.f };
  static const float kSide[] = { 0.125f, 0.5f, 1.f, 2.f };
  const Vector org = mv->m_vecAbsOrigin;
  auto tryAt = [&](const Vector& p) { trace_t s; TracePlayerBBox(p, p, MASK_PLAYERSOLID, s); if (s.startsolid) return false; mv->m_vecAbsOrigin = p; return true; };
  for (float u : kUp) if (tryAt(org + Vector(0, 0, u))) return true;
  for (float d : kSide)
    for (int dx = -1; dx <= 1; dx++)
      for (int dy = -1; dy <= 1; dy++) {
        if (!dx && !dy) continue;
        for (float u : { 0.f, 0.5f, 2.f }) if (tryAt(org + Vector(dx * d, dy * d, u))) return true;
      }
  return true;   // really stuck (inside a brush / player) - leave it, the slide code handles the rest
}

void CGameMovement::FullWalkMove() {
  CheckStuck();
  StartGravity();
  if (mv->m_nButtons & IN_JUMP) CheckJumpButton();
  else mv->m_nOldButtons &= ~IN_JUMP;
  if (player->m_fFlags & FL_ONGROUND) { mv->m_vecVelocity.z = 0; Friction(); }
  CheckVelocity();
  if (player->m_fFlags & FL_ONGROUND) WalkMove(); else AirMove();
  CategorizePosition();
  CheckVelocity();
  FinishGravity();
  if (player->m_fFlags & FL_ONGROUND) mv->m_vecVelocity.z = 0;
  CheckFalling();
}

void CGameMovement::Friction() {
  Vector& vel = mv->m_vecVelocity;
  float speed = vel.Length();
  if (speed < 0.1f) return;
  float drop = 0;
  if (player->m_fFlags & FL_ONGROUND) {
    float friction = sv_friction.GetFloat() * m_flSurfaceFriction;
    float control = speed < sv_stopspeed.GetFloat() ? sv_stopspeed.GetFloat() : speed;
    drop += control * friction * m_flFrameTime;
  }
  float newspeed = std::max(0.f, speed - drop);
  if (newspeed != speed) vel *= newspeed / speed;
}

void CGameMovement::Accelerate(const Vector& wishdir, float wishspeed, float accel) {
  if (!player->IsAlive()) return;
  float currentspeed = mv->m_vecVelocity.Dot(wishdir);
  float addspeed = wishspeed - currentspeed;
  if (addspeed <= 0) return;
  // CS:GO: acceleration is based on a minimum goal speed so walking/ducking accelerates at the normal rate
  float goal = std::max(wishspeed, 250.f * (mv->m_nButtons & (IN_SPEED | IN_DUCK) ? 0.52f : 1.f));
  goal = std::min(goal, std::max(wishspeed, 1.f) * 2.6f);
  float accelspeed = accel * m_flFrameTime * goal * m_flSurfaceFriction;
  if (accelspeed > addspeed) accelspeed = addspeed;
  mv->m_vecVelocity += wishdir * accelspeed;
}

void CGameMovement::AirAccelerate(const Vector& wishdir, float wishspeed, float accel) {
  if (!player->IsAlive()) return;
  float wishspd = std::min(wishspeed, sv_air_max_wishspeed.GetFloat());
  float currentspeed = mv->m_vecVelocity.Dot(wishdir);
  float addspeed = wishspd - currentspeed;
  if (addspeed <= 0) return;
  float accelspeed = accel * wishspeed * m_flFrameTime * m_flSurfaceFriction;
  if (accelspeed > addspeed) accelspeed = addspeed;
  mv->m_vecVelocity += wishdir * accelspeed;
  mv->m_outWishVel += wishdir * accelspeed;
}

void CGameMovement::WalkMove() {
  Vector forward = m_vecForward, right = m_vecRight;
  float fmove = mv->m_flForwardMove, smove = mv->m_flSideMove;
  forward.z = 0; right.z = 0; forward.NormalizeInPlace(); right.NormalizeInPlace();
  Vector wishvel = forward * fmove + right * smove; wishvel.z = 0;
  Vector wishdir = wishvel; float wishspeed = wishdir.NormalizeInPlace();
  if (wishspeed != 0 && wishspeed > mv->m_flMaxSpeed) { wishvel *= mv->m_flMaxSpeed / wishspeed; wishspeed = mv->m_flMaxSpeed; }
  mv->m_vecVelocity.z = 0;
  Accelerate(wishdir, wishspeed, sv_accelerate.GetFloat());
  mv->m_vecVelocity.z = 0;
  // CS:GO: clamp ground speed to max speed (prevents ground strafe speed gain)
  float spd2 = mv->m_vecVelocity.Length2D();
  if (spd2 > mv->m_flMaxSpeed && mv->m_flMaxSpeed > 0) { float k = mv->m_flMaxSpeed / spd2; mv->m_vecVelocity.x *= k; mv->m_vecVelocity.y *= k; }
  mv->m_vecVelocity += player->m_vecBaseVelocity;
  float spd = mv->m_vecVelocity.Length();
  if (spd < 1.f) { mv->m_vecVelocity.Init(); mv->m_vecVelocity -= player->m_vecBaseVelocity; return; }
  Vector dest = mv->m_vecAbsOrigin + mv->m_vecVelocity * m_flFrameTime; dest.z = mv->m_vecAbsOrigin.z;
  trace_t pm; TracePlayerBBox(mv->m_vecAbsOrigin, dest, MASK_PLAYERSOLID, pm);
  mv->m_outWishVel += wishdir * wishspeed;
  if (pm.fraction == 1) {
    mv->m_vecAbsOrigin = pm.endpos;
    mv->m_vecVelocity -= player->m_vecBaseVelocity;
    StayOnGround();
    return;
  }
  if (!(player->m_fFlags & FL_ONGROUND)) { mv->m_vecVelocity -= player->m_vecBaseVelocity; return; }
  StepMove(dest, pm);
  mv->m_vecVelocity -= player->m_vecBaseVelocity;
  StayOnGround();
}

void CGameMovement::AirMove() {
  Vector forward = m_vecForward, right = m_vecRight;
  float fmove = mv->m_flForwardMove, smove = mv->m_flSideMove;
  forward.z = 0; right.z = 0; forward.NormalizeInPlace(); right.NormalizeInPlace();
  Vector wishvel = forward * fmove + right * smove; wishvel.z = 0;
  Vector wishdir = wishvel; float wishspeed = wishdir.NormalizeInPlace();
  if (wishspeed != 0 && wishspeed > mv->m_flMaxSpeed) { wishvel *= mv->m_flMaxSpeed / wishspeed; wishspeed = mv->m_flMaxSpeed; }
  AirAccelerate(wishdir, wishspeed, sv_airaccelerate.GetFloat());
  mv->m_vecVelocity += player->m_vecBaseVelocity;
  TryPlayerMove();
  mv->m_vecVelocity -= player->m_vecBaseVelocity;
}

void CGameMovement::StepMove(const Vector& vecDestination, trace_t& trace) {
  Vector vecEndPos = vecDestination;
  Vector vecPos = mv->m_vecAbsOrigin, vecVel = mv->m_vecVelocity;
  // slide move
  TryPlayerMove(&vecEndPos, &trace);
  Vector vecDownPos = mv->m_vecAbsOrigin, vecDownVel = mv->m_vecVelocity;
  // reset, move up a stair height
  mv->m_vecAbsOrigin = vecPos; mv->m_vecVelocity = vecVel;
  vecEndPos = mv->m_vecAbsOrigin; vecEndPos.z += sv_stepsize.GetFloat() + DIST_EPSILON;
  TracePlayerBBox(mv->m_vecAbsOrigin, vecEndPos, MASK_PLAYERSOLID, trace);
  if (!trace.startsolid && !trace.allsolid) mv->m_vecAbsOrigin = trace.endpos;
  TryPlayerMove();
  // move down a stair
  vecEndPos = mv->m_vecAbsOrigin; vecEndPos.z -= sv_stepsize.GetFloat() + DIST_EPSILON;
  TracePlayerBBox(mv->m_vecAbsOrigin, vecEndPos, MASK_PLAYERSOLID, trace);
  if (!trace.startsolid && !trace.allsolid) mv->m_vecAbsOrigin = trace.endpos;
  if (trace.fraction != 1.f && trace.plane.normal.z < 0.7f && GroundInQuadrants(mv->m_vecAbsOrigin, sv_stepsize.GetFloat() + DIST_EPSILON)) {
    // the step-down landed on a steep seam plane of a slope / brush edge, but part of the box stands on walkable ground
    // (Source's TryTouchGroundInQuadrants): keep the stepped position instead of freezing at the seam
    trace.plane.normal.z = 1.f;
  }
  if (trace.fraction != 1.f && trace.plane.normal.z < 0.7f) { mv->m_vecAbsOrigin = vecDownPos; mv->m_vecVelocity = vecDownVel; return; }
  Vector vecUpPos = mv->m_vecAbsOrigin;
  float flDownDist = (vecDownPos.x - vecPos.x) * (vecDownPos.x - vecPos.x) + (vecDownPos.y - vecPos.y) * (vecDownPos.y - vecPos.y);
  float flUpDist = (vecUpPos.x - vecPos.x) * (vecUpPos.x - vecPos.x) + (vecUpPos.y - vecPos.y) * (vecUpPos.y - vecPos.y);
  if (flDownDist > flUpDist) { mv->m_vecAbsOrigin = vecDownPos; mv->m_vecVelocity = vecDownVel; }
  else mv->m_vecVelocity.z = vecDownVel.z;
  float stepDist = mv->m_vecAbsOrigin.z - vecPos.z;
  if (stepDist > 0) mv->m_outStepHeight += stepDist;
}

int CGameMovement::ClipVelocity(const Vector& in, const Vector& normal, Vector& out, float overbounce) {
  float angle = normal.z; int blocked = 0;
  if (angle > 0) blocked |= 1;
  if (!angle) blocked |= 2;
  float backoff = in.Dot(normal) * overbounce;
  for (int i = 0; i < 3; i++) out[i] = in[i] - normal[i] * backoff;
  float adjust = out.Dot(normal);
  if (adjust < 0.f) out -= normal * adjust;
  return blocked;
}

bool CGameMovement::GroundInQuadrants(const Vector& origin, float depth) {
  Vector mins = GetMins(), maxs = GetMaxs(), end = origin; end.z -= depth;
  const Vector q[4][2] = { { mins, Vector(0, 0, maxs.z) }, { Vector(0, 0, mins.z), maxs }, { Vector(mins.x, 0, mins.z), Vector(0, maxs.y, maxs.z) }, { Vector(0, mins.y, mins.z), Vector(maxs.x, 0, maxs.z) } };
  for (int i = 0; i < 4; i++) {
    Ray_t ray; ray.Init(origin, end, q[i][0], q[i][1]); CTraceFilter f(player); trace_t t;
    g_EngineTrace.TraceRay(ray, MASK_PLAYERSOLID, &f, &t);
    if (!t.startsolid && t.fraction < 1.f && t.plane.normal.z >= 0.7f) return true;
  }
  return false;
}

int CGameMovement::TryPlayerMove(Vector* pFirstDest, trace_t* pFirstTrace) {
  const Vector startOrigin = mv->m_vecAbsOrigin, startVel = mv->m_vecVelocity;
  int r = TryPlayerMoveInner(pFirstDest, pFirstTrace);
  // on the ground and not moved at all although we wanted to (the trace reports several planes at fraction 0 on seams of
  // slopes / displacement edges): lift the box by a hair and slide once more instead of freezing in place
  if ((player->m_fFlags & FL_ONGROUND) && mv->m_vecAbsOrigin == startOrigin && startVel.Length2D() > 1.f && mv->m_vecVelocity.Length2D() < 1.f) {
    Vector lifted = startOrigin; lifted.z += DIST_EPSILON * 4.f;
    trace_t t; TracePlayerBBox(lifted, lifted, MASK_PLAYERSOLID, t);
    if (!t.startsolid && !t.allsolid) {
      mv->m_vecAbsOrigin = lifted; mv->m_vecVelocity = startVel;
      r = TryPlayerMoveInner(nullptr, nullptr);
      if (mv->m_vecAbsOrigin == lifted) { mv->m_vecAbsOrigin = startOrigin; mv->m_vecVelocity.Init(); }
    }
  }
  return r;
}

int CGameMovement::TryPlayerMoveInner(Vector* pFirstDest, trace_t* pFirstTrace) {
  int bumpcount, numbumps = 4, blocked = 0, numplanes = 0;
  Vector planes[MAX_CLIP_PLANES];
  Vector original_velocity = mv->m_vecVelocity, primal_velocity = mv->m_vecVelocity, new_velocity;
  float allFraction = 0, time_left = m_flFrameTime;
  trace_t pm;
  for (bumpcount = 0; bumpcount < numbumps; bumpcount++) {
    if (mv->m_vecVelocity.Length() == 0.0f) break;
    Vector end = mv->m_vecAbsOrigin + mv->m_vecVelocity * time_left;
    if (pFirstDest && end == *pFirstDest) pm = *pFirstTrace;
    else TracePlayerBBox(mv->m_vecAbsOrigin, end, MASK_PLAYERSOLID, pm);
    allFraction += pm.fraction;
    if (pm.allsolid) { mv->m_vecVelocity.Init(); return 4; }
    if (pm.fraction > 0) {
      if (numbumps > 0 && pm.fraction == 1) {
        // verify we're not stuck at the end position
        trace_t stuck; TracePlayerBBox(pm.endpos, pm.endpos, MASK_PLAYERSOLID, stuck);
        if (stuck.startsolid || stuck.fraction != 1.0f) { mv->m_vecVelocity.Init(); break; }
      }
      mv->m_vecAbsOrigin = pm.endpos;
      original_velocity = mv->m_vecVelocity;
      numplanes = 0;
    }
    if (pm.fraction == 1) break;
    if (pm.plane.normal.z > 0.7f) blocked |= 1;
    if (!pm.plane.normal.z) blocked |= 2;
    time_left -= time_left * pm.fraction;
    if (numplanes >= MAX_CLIP_PLANES) { mv->m_vecVelocity.Init(); break; }
    planes[numplanes++] = pm.plane.normal;
    if (numplanes == 1 && player->m_MoveType == MOVETYPE_WALK && !(player->m_fFlags & FL_ONGROUND)) {
      for (int i = 0; i < numplanes; i++) {
        if (planes[i].z > 0.7f) ClipVelocity(original_velocity, planes[i], new_velocity, 1);
        else ClipVelocity(original_velocity, planes[i], new_velocity, 1.0f + sv_friction.GetFloat() * 0.0f * (1 - m_flSurfaceFriction));
      }
      mv->m_vecVelocity = new_velocity; original_velocity = new_velocity;
    } else {
      int i, j;
      for (i = 0; i < numplanes; i++) {
        ClipVelocity(original_velocity, planes[i], mv->m_vecVelocity, 1);
        for (j = 0; j < numplanes; j++) if (j != i && mv->m_vecVelocity.Dot(planes[j]) < 0) break;
        if (j == numplanes) break;
      }
      if (i == numplanes) {
        if (numplanes != 2) { mv->m_vecVelocity.Init(); break; }
        Vector dir = planes[0].Cross(planes[1]); dir.NormalizeInPlace();
        float d = dir.Dot(mv->m_vecVelocity);
        mv->m_vecVelocity = dir * d;
      }
      if (mv->m_vecVelocity.Dot(primal_velocity) <= 0) { mv->m_vecVelocity.Init(); break; }
    }
  }
  if (allFraction == 0) mv->m_vecVelocity.Init();
  return blocked;
}

bool CGameMovement::CheckJumpButton() {
  if (!player->IsAlive()) { mv->m_nOldButtons |= IN_JUMP; return false; }
  if (!(player->m_fFlags & FL_ONGROUND)) { mv->m_nOldButtons |= IN_JUMP; return false; }
  if ((mv->m_nOldButtons & IN_JUMP) && !sv_autobunnyhopping.GetBool()) return false;
  if (player->m_fFlags & FL_FROZEN) return false;
  // PreventBunnyJumping
  if (!sv_enablebunnyhopping.GetBool()) {
    float maxscaledspeed = 1.1f * player->m_flMaxspeed;
    if (maxscaledspeed > 0) { float spd = mv->m_vecVelocity.Length(); if (spd > maxscaledspeed) mv->m_vecVelocity *= maxscaledspeed / spd; }
  }
  if (sv_bhop_reset.GetBool()) {
    // reset to the RUNNING speed of the weapon: m_flMaxspeed is the cropped ground speed (duck / walk), using it made a
    // crouch bhop (duck held while landing) drop to ~85 u/s every jump
    const float runSpeed = player->GetPlayerMaxSpeed();
    float spd = mv->m_vecVelocity.Length2D();
    if (runSpeed > 0 && spd > runSpeed) { float k = runSpeed / spd; mv->m_vecVelocity.x *= k; mv->m_vecVelocity.y *= k; }
  }
  player->m_fFlags &= ~FL_ONGROUND;
  player->m_hGroundEntity = nullptr;
  float startz = mv->m_vecVelocity.z;
  mv->m_vecVelocity.z = sv_jump_impulse.GetFloat();
  (void)startz;
  FinishGravity();
  mv->m_outJumpVel.z += mv->m_vecVelocity.z - startz;
  mv->m_outStepHeight += 0.15f;
  mv->m_nOldButtons |= IN_JUMP;
  player->m_flStamina = std::min(player->m_flStamina + 0.0f, 80.f);
  return true;
}

void CGameMovement::SetGroundEntity(trace_t* pm) {
  C_BaseEntity* newGround = pm ? pm->m_pEnt : nullptr;
  bool hit = pm != nullptr;
  if (hit) {
    if (!(player->m_fFlags & FL_ONGROUND)) { // landing
      mv->m_vecVelocity.z = 0;
    }
    player->m_fFlags |= FL_ONGROUND;
    player->m_hGroundEntity = newGround;
    m_flSurfaceFriction = pm->surface.surfaceProps >= 0 ? Clamp(GetSurfaceData(pm->surface.surfaceProps)->friction, 0.1f, 1.f) : 1.f;
    player->m_surfaceFriction = m_flSurfaceFriction;
    player->m_chTextureType = GetSurfaceData(pm->surface.surfaceProps)->gameMaterial;
  } else {
    player->m_fFlags &= ~FL_ONGROUND;
    player->m_hGroundEntity = nullptr;
    m_flSurfaceFriction = 1.f;
  }
}

void CGameMovement::CategorizePosition() {
  if (player->m_MoveType == MOVETYPE_NOCLIP) { SetGroundEntity(nullptr); return; }
  Vector point = mv->m_vecAbsOrigin; point.z -= 2.f;
  float zvel = mv->m_vecVelocity.z;
  bool movingUpRapidly = zvel > NON_JUMP_VELOCITY;
  if (movingUpRapidly) { SetGroundEntity(nullptr); return; }
  trace_t pm; TracePlayerBBox(mv->m_vecAbsOrigin, point, MASK_PLAYERSOLID, pm);
  if (pm.fraction == 1.f || pm.plane.normal.z < 0.7f) {
    // try a slightly smaller box (quadrants) to stand on edges like Source's TryTouchGroundInQuadrants
    if (pm.fraction < 1.f && pm.plane.normal.z < 0.7f) {
      bool found = false;
      Vector mins = GetMins(), maxs = GetMaxs();
      const Vector q[4][2] = { { mins, Vector(0, 0, maxs.z) }, { Vector(0, 0, mins.z), maxs }, { Vector(mins.x, 0, mins.z), Vector(0, maxs.y, maxs.z) }, { Vector(0, mins.y, mins.z), Vector(maxs.x, 0, maxs.z) } };
      for (int i = 0; i < 4 && !found; i++) {
        Ray_t ray; ray.Init(mv->m_vecAbsOrigin, point, q[i][0], q[i][1]); CTraceFilter f(player); trace_t t2;
        g_EngineTrace.TraceRay(ray, MASK_PLAYERSOLID, &f, &t2);
        if (t2.fraction < 1.f && t2.plane.normal.z >= 0.7f) { pm = t2; found = true; }
      }
      if (found) { SetGroundEntity(&pm); return; }
      // wedged in a crease of steep faces (velocity gets cleared every tick instead of sliding): after a few ticks let
      // it count as ground so the player can walk / jump off. Only when sliding down the face is really blocked -
      // pressing into a single steep wall / slope near the jump apex used to "stick" the player to it (he became
      // grounded, vz was zeroed every tick and he hung there)
      bool wedged = false;
      if (mv->m_vecVelocity.z <= 0.f && mv->m_vecVelocity.z > -20.f && pm.plane.normal.z > 0.05f) {
        const Vector n = pm.plane.normal;
        Vector slide = Vector(0, 0, -1) - n * (-n.z); slide.NormalizeInPlace();   // gravity projected onto the face
        trace_t st; TracePlayerBBox(mv->m_vecAbsOrigin, mv->m_vecAbsOrigin + slide * 2.f, MASK_PLAYERSOLID, st);
        trace_t dn; TracePlayerBBox(mv->m_vecAbsOrigin, mv->m_vecAbsOrigin + Vector(0, 0, -2.f), MASK_PLAYERSOLID, dn);
        wedged = !st.startsolid && st.fraction < 0.1f && dn.fraction < 0.1f;
      }
      player->m_nWedgedTicks = wedged ? player->m_nWedgedTicks + 1 : 0;
      if (player->m_nWedgedTicks > 3) { SetGroundEntity(&pm); return; }
    } else player->m_nWedgedTicks = 0;
    SetGroundEntity(nullptr);
  } else {
    player->m_nWedgedTicks = 0;
    SetGroundEntity(&pm);
  }
}

void CGameMovement::StayOnGround() {
  Vector start = mv->m_vecAbsOrigin, end = mv->m_vecAbsOrigin;
  start.z += 2; end.z -= sv_stepsize.GetFloat();
  trace_t trace; TracePlayerBBox(mv->m_vecAbsOrigin, start, MASK_PLAYERSOLID, trace);
  start = trace.endpos;
  TracePlayerBBox(start, end, MASK_PLAYERSOLID, trace);
  if (trace.fraction > 0.0f && trace.fraction < 1.0f && !trace.startsolid && trace.plane.normal.z >= 0.7f) {
    float delta = std::fabs(mv->m_vecAbsOrigin.z - trace.endpos.z);
    if (delta > 0.5f * DIST_EPSILON) mv->m_vecAbsOrigin = trace.endpos;
  }
}

void CGameMovement::CheckFalling() {
  if (!(player->m_fFlags & FL_ONGROUND) || player->m_flFallVelocity <= 0) return;
  if (player->m_flFallVelocity >= PLAYER_MAX_SAFE_FALL_SPEED && player->IsAlive()) {
    float dmg = (player->m_flFallVelocity - PLAYER_MAX_SAFE_FALL_SPEED) * (100.f / (PLAYER_FATAL_FALL_SPEED - PLAYER_MAX_SAFE_FALL_SPEED)) * sv_falldamage_scale.GetFloat();
    if (dmg > 0) { CTakeDamageInfo info; info.m_flDamage = dmg; info.m_bitsDamageType = DMG_FALL; info.m_vecDamagePosition = player->m_vecOrigin; player->OnTakeDamage(info); }
    PlayerRoughLandingEffects(1.f);
  } else if (player->m_flFallVelocity > PLAYER_MIN_BOUNCE_SPEED) PlayerRoughLandingEffects(0.5f);
  player->m_flFallVelocity = 0;
}
void CGameMovement::PlayerRoughLandingEffects(float fvol) {
  player->m_viewPunchAngle.x += fvol * 2.f;
  extern void Game_OnPlayerLanded(CCSPlayer*, float);
  Game_OnPlayerLanded(player, fvol);
}

bool CGameMovement::CanUnduck() {
  Vector newOrigin = mv->m_vecAbsOrigin;
  if (!(player->m_fFlags & FL_ONGROUND)) newOrigin.z -= VEC_DUCK_DIFF;
  bool saved = player->m_bDucked; player->m_bDucked = false;
  trace_t trace; TracePlayerBBox(mv->m_vecAbsOrigin, newOrigin, MASK_PLAYERSOLID, trace);
  player->m_bDucked = saved;
  return !trace.startsolid && trace.fraction == 1.0f;
}
void CGameMovement::FinishDuck() {
  if (player->m_bDucked) return;
  player->m_bDucked = true; player->m_fFlags |= FL_DUCKING;
  if (!(player->m_fFlags & FL_ONGROUND)) { mv->m_vecAbsOrigin.z += VEC_DUCK_DIFF; player->m_bInDuckJump = true; }
  CategorizePosition();
}
void CGameMovement::FinishUnDuck() {
  if (!player->m_bDucked) return;
  if (!(player->m_fFlags & FL_ONGROUND)) mv->m_vecAbsOrigin.z -= VEC_DUCK_DIFF;
  player->m_bDucked = false; player->m_fFlags &= ~FL_DUCKING; player->m_bInDuckJump = false;
  CategorizePosition();
}
void CGameMovement::Duck() {
  bool wantDuck = (mv->m_nButtons & IN_DUCK) != 0;
  bool onGround = (player->m_fFlags & FL_ONGROUND) != 0;
  float rate = sv_duck_speed.GetFloat() * m_flFrameTime;
  if (wantDuck) {
    player->m_flDuckAmount = std::min(1.f, player->m_flDuckAmount + rate);
    if (!player->m_bDucked && (!onGround || player->m_flDuckAmount >= 1.f)) FinishDuck();
    player->m_bDucking = player->m_flDuckAmount < 1.f;
  } else if (player->m_flDuckAmount > 0 || player->m_bDucked) {
    if (!player->m_bDucked || CanUnduck()) {
      player->m_flDuckAmount = std::max(0.f, player->m_flDuckAmount - rate);
      if (player->m_bDucked && (!onGround || player->m_flDuckAmount < 1.f)) FinishUnDuck();
      if (!player->m_bDucked && !onGround) player->m_flDuckAmount = 0;
    }
    player->m_bDucking = false;
  }
  // view offset
  float duck = player->m_bDucked ? (onGround ? player->m_flDuckAmount : 1.f) : (onGround ? player->m_flDuckAmount : 0.f);
  if (player->m_bDucked && !onGround) player->m_vecViewOffset = GetPlayerViewOffset(true);
  else player->m_vecViewOffset.z = 64.f - VEC_DUCK_DIFF * SimpleSpline(duck);
}
