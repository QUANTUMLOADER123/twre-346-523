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
    m_flMoveCycle += dt * m_flVelocityLengthXY / 76.f;
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
void StudioBoneMap::Init(const CStudioModel& m) {
  hips = m.FindNode("Hips"); abdomen = m.FindNode("Abdomen"); torso = m.FindNode("Torso"); chest = m.FindNode("Chest");
  neck = m.FindNode("Neck"); head = m.FindNode("Head"); headEnd = m.FindNode("Head_end");
  const char* side[2] = { "L", "R" };
  for (int s = 0; s < 2; s++) {
    std::string S = std::string(".") + side[s];
    shoulder[s] = m.FindNode("Shoulder" + S); upperArm[s] = m.FindNode("UpperArm" + S); lowerArm[s] = m.FindNode("LowerArm" + S);
    hand[s] = m.FindNode("Hand" + S); middle2[s] = m.FindNode("Middle2" + S);
    upperLeg[s] = m.FindNode("UpperLeg" + S); lowerLeg[s] = m.FindNode("LowerLeg" + S); foot[s] = m.FindNode("Foot" + S); footEnd[s] = m.FindNode("Foot" + S + "_end");
    for (const char* f : { "Index", "Middle", "Ring", "Pinky" }) for (int k = 2; k <= 4; k++) { int n = m.FindNode(std::string(f) + std::to_string(k) + S); if (n >= 0) fingers[s].push_back(n); }
  }
  ok = hips >= 0 && chest >= 0 && head >= 0 && upperArm[0] >= 0 && upperArm[1] >= 0 && upperLeg[0] >= 0 && upperLeg[1] >= 0;
}
const StudioBoneMap& GetBoneMap(const CStudioModel& m) {
  static std::unordered_map<const CStudioModel*, StudioBoneMap> cache;
  auto it = cache.find(&m); if (it != cache.end()) return it->second;
  StudioBoneMap b; b.Init(m); return cache[&m] = b;
}

static void SetDelta(std::vector<BoneDelta>& d, int i, const matrix3x4_t& r, const Vector& t = Vector()) {
  if (i < 0) return;
  if (d[i].active) { d[i].rot = MatrixMul(r, d[i].rot); d[i].translate += t; }
  else { d[i].active = true; d[i].rot = r; d[i].translate = t; }
}
static matrix3x4_t RotOnly(const matrix3x4_t& m) { matrix3x4_t r = m; r.SetOrigin(Vector()); return r; }
static matrix3x4_t Transpose3(const matrix3x4_t& m) { matrix3x4_t r; for (int i = 0; i < 3; i++) for (int j = 0; j < 3; j++) r[i][j] = m[j][i]; r.SetOrigin(Vector()); return r; }

void BuildPlayerPose(const CStudioModel& mdl, const CStudioModel* wmdl, const PlayerPoseInput& in, PlayerPoseOutput& out, bool computeHitboxes) {
  const StudioBoneMap& B = GetBoneMap(mdl);
  // model -> world
  matrix3x4_t m2w; AngleMatrix(QAngle(0, in.footYaw, 0), in.origin, m2w);
  float death = in.deathCycle >= 0 ? SimpleSpline(Clamp(in.deathCycle, 0.f, 1.f)) : -1.f;
  if (death >= 0) {
    float rel = DEG2RADF(in.deathYaw - in.footYaw);
    Vector fallDir(std::cos(rel), std::sin(rel), 0);
    matrix3x4_t fall = AxisAngleMatrix(Vector(0, 0, 1).Cross(fallDir), 84.f * death);
    fall.SetOrigin(Vector(0, 0, 5.f * death));
    m2w = MatrixMul(m2w, fall);
  }
  out.modelToWorld = m2w;
  if (!B.ok) { mdl.BindPose(out.pose); out.hasWeapon = false; if (computeHitboxes) ComputeHitboxes(mdl, out.pose, m2w, out.hitboxes); return; }

  std::vector<BoneDelta> d(mdl.nodes.size());
  // the low-poly characters have short arms: stretch them to human proportions
  for (int s = 0; s < 2; s++) { if (B.lowerArm[s] >= 0) d[B.lowerArm[s]].stretch = 1.5f; if (B.hand[s] >= 0) d[B.hand[s]].stretch = 1.5f; }
  const float duck = Clamp(in.duckAmount, 0.f, 1.f);
  const float speed = Clamp(in.moveSpeed, 0.f, 1.f);
  const float phase = in.moveCycle * 2.f * M_PI_F;
  const bool alive = death < 0;
  // hips: crouch drop + bob + landing compression
  float hipDrop = duck * 19.f + std::fabs(std::sin(phase)) * 1.3f * speed * (in.onGround ? 1.f : 0.f) + in.landAmount * 6.f;
  if (!in.onGround) hipDrop -= 2.f;
  if (alive) SetDelta(d, B.hips, AxisAngleMatrix(Vector(1, 0, 0), in.leanRoll), Vector(0, 0, -hipDrop));
  // spine twist toward eye yaw and pitch
  float relYaw = alive ? Clamp(AngleDiff(in.eyeAngles.y, in.footYaw), -100.f, 100.f) : 0.f;
  float pitch = alive ? Clamp(AngleNormalize(in.eyeAngles.x), -89.f, 89.f) : 0.f;
  float crouchLean = duck * 12.f;
  const float twistFrac[4] = { 0.2f, 0.25f, 0.35f, 0.2f };
  int spine[4] = { B.abdomen, B.torso, B.chest, B.neck };
  float yawAcc = 0;
  for (int i = 0; i < 4; i++) {
    yawAcc += relYaw * twistFrac[i];
    Vector lateral(-std::sin(DEG2RADF(yawAcc)), std::cos(DEG2RADF(yawAcc)), 0);
    float p = (i == 0 ? crouchLean : 0.f) + pitch * (i == 0 ? 0.1f : i == 1 ? 0.2f : i == 2 ? 0.25f : 0.15f);
    if (i == 1) p -= crouchLean * 0.5f;
    if (!alive) continue;
    SetDelta(d, spine[i], MatrixMul(AxisAngleMatrix(lateral, p), AxisAngleMatrix(Vector(0, 0, 1), relYaw * twistFrac[i])));
  }
  if (alive) { Vector lateral(-std::sin(DEG2RADF(relYaw)), std::cos(DEG2RADF(relYaw)), 0); SetDelta(d, B.head, AxisAngleMatrix(lateral, pitch * 0.3f)); }

  CStudioPose pose;
  mdl.BuildPose(d, pose);
  // --- legs: two-bone IK to foot targets ---
  for (int s = 0; s < 2; s++) {
    if (B.foot[s] < 0 || B.lowerLeg[s] < 0) continue;
    Vector bindFoot = mdl.nodes[B.foot[s]].bindPos();
    float ph = phase + (s == 1 ? M_PI_F : 0.f);
    Vector mdir(std::cos(DEG2RADF(in.moveYaw)), std::sin(DEG2RADF(in.moveYaw)), 0);
    Vector t = bindFoot;
    t.y *= 1.0f + duck * 0.35f;
    if (in.onGround && alive) {
      float stride = 15.f * speed * (1.f - duck * 0.45f);
      t += mdir * (std::sin(ph) * stride);
      t.z += std::max(0.f, std::cos(ph)) * 6.f * std::min(1.f, speed * 2.5f);
    } else if (alive) { t.z += 12.f + (s ? 4.f : 0.f); t.x -= 3.f; }
    if (!alive) { t.z += 3.f; t.x += 4.f; }
    mdl.SolveTwoBoneIK(d, B.upperLeg[s], B.lowerLeg[s], B.foot[s], t, Vector(1, 0, 0.1f));
  }
  mdl.BuildPose(d, pose);
  // keep feet flat
  for (int s = 0; s < 2; s++) if (B.foot[s] >= 0) {
    matrix3x4_t want = RotOnly(mdl.nodes[B.foot[s]].bindToModel), cur = RotOnly(pose.nodes[B.foot[s]]);
    SetDelta(d, B.foot[s], MatrixMul(want, Transpose3(cur)));
  }
  // --- weapon placement + arm IK ---
  out.hasWeapon = false;
  const CCSWeaponInfo* wi = GetWeaponInfo(in.weaponId);
  Vector shoulderR = pose.nodes[B.upperArm[1]].GetOrigin(), shoulderL = pose.nodes[B.upperArm[0]].GetOrigin();
  Vector chestP = pose.nodes[B.chest].GetOrigin();
  Vector aimDir; AngleVectors(QAngle(pitch, relYaw, 0), &aimDir);
  Vector aimRight, aimUp; { Vector f; AngleVectors(QAngle(pitch, relYaw, 0), &f, &aimRight, &aimUp); }
  if (alive && in.weaponId != 0 && !in.planting && !in.defusing) {
    matrix3x4_t wrot; AngleMatrix(QAngle(pitch, relYaw, 0), wrot);
    float kick = in.fireAmount;
    Vector gripLocal, foreLocal; bool twoHands = true;
    Vector mn = wmdl ? wmdl->mins : Vector(-5, -1, -2), mx = wmdl ? wmdl->maxs : Vector(5, 1, 2);
    float L = mx.x - mn.x, H = mx.z - mn.z, cz = (mn.z + mx.z) * 0.5f;
    Vector gripTarget;
    switch (wi->type) {
      case WEAPONTYPE_PISTOL:
        gripLocal = Vector(mn.x + 0.22f * L, 0, mn.z + 0.32f * H); foreLocal = gripLocal + Vector(-0.5f, 1.2f, -0.6f);
        gripTarget = chestP + Vector(0, 0, -2.5f) + aimDir * 17.f - aimRight * 1.5f;
        break;
      case WEAPONTYPE_KNIFE: case WEAPONTYPE_GRENADE: case WEAPONTYPE_C4:
        gripLocal = Vector(mn.x + 0.25f * L, 0, cz); twoHands = wi->type == WEAPONTYPE_C4;
        foreLocal = gripLocal + Vector(0, 3, 0);
        gripTarget = chestP + Vector(0, 0, -9.f) + aimDir * 12.f + aimRight * 5.f;
        break;
      default: {
        gripLocal = Vector(mn.x + 0.34f * L, 0, cz - 0.18f * H);
        foreLocal = Vector(mn.x + 0.63f * L, 0, cz - 0.08f * H);
        Vector stockLocal(mn.x, 0, cz + 0.05f * H);
        Vector stockTarget = shoulderR - aimRight * 2.0f + aimDir * 2.0f - aimUp * 1.0f;
        gripTarget = stockTarget + VectorRotate(gripLocal - stockLocal, wrot);
        break;
      }
    }
    if (in.viewmodel && wi->type != WEAPONTYPE_C4) {
      // first person: place the grip relative to the eyes (CS v_ models sit lower-right of the view)
      Vector eyeM = pose.nodes[B.head].GetOrigin() + Vector(1.5f, 0, 3.5f);
      if (wi->type == WEAPONTYPE_PISTOL) gripTarget = eyeM + aimDir * 21.f - aimUp * 8.f + aimRight * 4.6f;
      else if (wi->type == WEAPONTYPE_KNIFE) gripTarget = eyeM + aimDir * 18.f - aimUp * 8.5f + aimRight * 6.5f;
      else if (wi->type == WEAPONTYPE_GRENADE) gripTarget = eyeM + aimDir * 18.f - aimUp * 8.f + aimRight * 6.f;
    }
    if (wi->type == WEAPONTYPE_KNIFE && in.viewmodel) // blade tilted up/inwards like v_knife
      wrot = MatrixMul(AxisAngleMatrix(aimUp, 12.f), MatrixMul(AxisAngleMatrix(aimRight, 18.f), wrot));
    if (in.reloadCycle >= 0) { // tilt the gun while reloading
      float r = std::sin(Clamp(in.reloadCycle, 0.f, 1.f) * M_PI_F);
      wrot = MatrixMul(AxisAngleMatrix(aimDir, -35.f * r), MatrixMul(AxisAngleMatrix(aimRight, -20.f * r), wrot));
      gripTarget -= aimUp * (3.f * r);
    }
    gripTarget -= aimDir * (2.5f * kick);
    if (kick > 0) wrot = MatrixMul(AxisAngleMatrix(aimRight, -6.f * kick), wrot);
    matrix3x4_t w2m = wrot; w2m.SetOrigin(gripTarget - VectorRotate(gripLocal, wrot));
    out.weaponToWorld = MatrixMul(m2w, w2m);
    out.hasWeapon = true;
    Vector rHand = VectorTransform(gripLocal, w2m), lHand = VectorTransform(foreLocal, w2m);
    if (in.reloadCycle >= 0) { float r = std::sin(Clamp(in.reloadCycle, 0.f, 1.f) * M_PI_F); lHand = VectorLerp(lHand, VectorTransform(Vector((mn.x + mx.x) * 0.5f, 0, mn.z - 4.f), w2m), r); }
    mdl.SolveTwoBoneIK(d, B.upperArm[1], B.lowerArm[1], B.hand[1], rHand, Vector(-0.2f, -0.8f, -1.f));
    if (twoHands) mdl.SolveTwoBoneIK(d, B.upperArm[0], B.lowerArm[0], B.hand[0], lHand, Vector(-0.2f, 0.8f, -1.f));
    else { // free left arm hangs a bit forward
      mdl.SolveTwoBoneIK(d, B.upperArm[0], B.lowerArm[0], B.hand[0], shoulderL + Vector(4, 3, -22), Vector(-0.3f, 0.6f, 0));
    }
    mdl.BuildPose(d, pose);
    // orient hands along the weapon and curl fingers
    Vector gunFwd = VectorRotate(Vector(1, 0, 0), wrot), gunUp = VectorRotate(Vector(0, 0, 1), wrot), gunLat = VectorRotate(Vector(0, 1, 0), wrot);
    for (int s = 0; s < 2; s++) {
      if (B.hand[s] < 0 || B.middle2[s] < 0) continue;
      if (s == 0 && !twoHands) continue;
      Vector cur = pose.nodes[B.middle2[s]].GetOrigin() - pose.nodes[B.hand[s]].GetOrigin();
      Vector want = s == 1 ? (gunFwd * 0.55f - gunUp * 0.85f) : (gunFwd * 0.8f - gunUp * 0.3f + gunLat * -0.4f);
      SetDelta(d, B.hand[s], RotationBetween(cur, want));
    }
    mdl.BuildPose(d, pose);
    for (int s = 0; s < 2; s++) for (int f : B.fingers[s]) SetDelta(d, f, AxisAngleMatrix(gunLat, s == 1 ? 38.f : 30.f));
  } else {
    // relaxed arms (no weapon / dead / planting)
    float down = alive ? 72.f : 40.f;
    if (in.planting || in.defusing) {
      mdl.SolveTwoBoneIK(d, B.upperArm[1], B.lowerArm[1], B.hand[1], chestP + aimDir * 16.f + Vector(0, -4, -26), Vector(0, -0.6f, -1));
      mdl.SolveTwoBoneIK(d, B.upperArm[0], B.lowerArm[0], B.hand[0], chestP + aimDir * 16.f + Vector(0, 4, -26), Vector(0, 0.6f, -1));
    } else {
      SetDelta(d, B.upperArm[0], AxisAngleMatrix(Vector(1, 0, 0), -down));
      SetDelta(d, B.upperArm[1], AxisAngleMatrix(Vector(1, 0, 0), down));
      if (!alive) { SetDelta(d, B.lowerArm[0], AxisAngleMatrix(Vector(0, 1, 0), -30.f)); SetDelta(d, B.lowerArm[1], AxisAngleMatrix(Vector(0, 1, 0), -30.f)); }
    }
  }
  mdl.BuildPose(d, out.pose);
  // eye position: between head joint and head top
  Vector headP = out.pose.nodes[B.head].GetOrigin();
  out.eyePos = VectorTransform(headP + Vector(1.5f, 0, 3.5f), m2w);
  if (computeHitboxes) ComputeHitboxes(mdl, out.pose, m2w, out.hitboxes);
}

// ------------------------------------------------------------------------------------------------
static const int s_hitgroup[19] = { HITGROUP_HEAD, HITGROUP_HEAD, HITGROUP_STOMACH, HITGROUP_STOMACH, HITGROUP_CHEST, HITGROUP_CHEST, HITGROUP_CHEST,
  HITGROUP_RIGHTLEG, HITGROUP_LEFTLEG, HITGROUP_RIGHTLEG, HITGROUP_LEFTLEG, HITGROUP_RIGHTLEG, HITGROUP_LEFTLEG,
  HITGROUP_RIGHTARM, HITGROUP_LEFTARM, HITGROUP_RIGHTARM, HITGROUP_RIGHTARM, HITGROUP_LEFTARM, HITGROUP_LEFTARM };
static const char* s_hbname[19] = { "head", "neck", "pelvis", "stomach", "lower chest", "chest", "upper chest", "right thigh", "left thigh", "right calf", "left calf",
  "right foot", "left foot", "right hand", "left hand", "right upper arm", "right forearm", "left upper arm", "left forearm" };
int HitboxToHitgroup(int hb) { return (hb >= 0 && hb < 19) ? s_hitgroup[hb] : HITGROUP_GENERIC; }
const char* HitboxName(int hb) { return (hb >= 0 && hb < 19) ? s_hbname[hb] : "generic"; }

void ComputeHitboxes(const CStudioModel& mdl, const CStudioPose& pose, const matrix3x4_t& m2w, HitboxSet& out) {
  const StudioBoneMap& B = GetBoneMap(mdl);
  auto P = [&](int n) { return n >= 0 ? VectorTransform(pose.nodes[n].GetOrigin(), m2w) : m2w.GetOrigin(); };
  auto set = [&](int i, const Vector& a, const Vector& b, float r) { out[i].a = a; out[i].b = b; out[i].radius = r; out[i].group = s_hitgroup[i]; };
  if (!B.ok) { Vector o = m2w.GetOrigin(); for (int i = 0; i < 19; i++) set(i, o + Vector(0, 0, 10), o + Vector(0, 0, 60), 10); return; }
  Vector head = P(B.head), headEnd = B.headEnd >= 0 ? P(B.headEnd) : head + Vector(0, 0, 8);
  Vector hu = (headEnd - head);
  set(HITBOX_HEAD, head + hu * 0.35f, head + hu * 0.62f, 4.1f);
  set(HITBOX_NECK, P(B.neck), head + hu * 0.1f, 3.0f);
  Vector hipL = P(B.upperLeg[0]), hipR = P(B.upperLeg[1]);
  set(HITBOX_PELVIS, hipL + (P(B.hips) - hipL) * 0.3f, hipR + (P(B.hips) - hipR) * 0.3f, 5.6f);
  set(HITBOX_STOMACH, P(B.abdomen), P(B.torso), 6.0f);
  Vector chest = P(B.chest), neck = P(B.neck);
  set(HITBOX_LOWER_CHEST, P(B.torso), chest, 6.2f);
  set(HITBOX_CHEST, chest, chest + (neck - chest) * 0.45f, 6.6f);
  set(HITBOX_UPPER_CHEST, chest + (neck - chest) * 0.45f, neck - (neck - chest) * 0.15f, 6.0f);
  for (int s = 0; s < 2; s++) {
    int R = s == 1;
    set(R ? HITBOX_RIGHT_THIGH : HITBOX_LEFT_THIGH, P(B.upperLeg[s]), P(B.lowerLeg[s]), 4.4f);
    set(R ? HITBOX_RIGHT_CALF : HITBOX_LEFT_CALF, P(B.lowerLeg[s]), P(B.foot[s]), 3.6f);
    set(R ? HITBOX_RIGHT_FOOT : HITBOX_LEFT_FOOT, P(B.foot[s]), B.footEnd[s] >= 0 ? P(B.footEnd[s]) : P(B.foot[s]) + Vector(4, 0, 0), 2.4f);
    set(R ? HITBOX_RIGHT_HAND : HITBOX_LEFT_HAND, P(B.hand[s]), B.middle2[s] >= 0 ? P(B.middle2[s]) : P(B.hand[s]), 2.1f);
    set(R ? HITBOX_RIGHT_UPPER_ARM : HITBOX_LEFT_UPPER_ARM, P(B.upperArm[s]), P(B.lowerArm[s]), 3.0f);
    set(R ? HITBOX_RIGHT_FOREARM : HITBOX_LEFT_FOREARM, P(B.lowerArm[s]), P(B.hand[s]), 2.6f);
  }
}

int IntersectHitboxes(const HitboxSet& hb, const Vector& start, const Vector& delta, float& tOut) {
  int best = -1; float bt = 2.f;
  for (int i = 0; i < 19; i++) {
    float t;
    if (IntersectSegmentCapsule(start, delta, hb[i].a, hb[i].b, hb[i].radius, t) && t < bt) { bt = t; best = i; }
  }
  if (best >= 0) tOut = bt;
  return best;
}
