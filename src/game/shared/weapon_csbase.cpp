// weapon_csbase.cpp - C_WeaponCSBase and derived weapons
#include "weapon_csbase.hpp"
#include "cs_player.hpp"
#include "cs_gamerules.hpp"
#include "cs_projectiles.hpp"
#include "hostage.hpp"
#include "gameevents.hpp"
#include "soundemitter.hpp"
#include "mapinfo.hpp"
#include "../../engine/random.hpp"
#include <cmath>
#include <algorithm>
#include <cstdio>

extern ConVar weapon_recoil_scale;
extern ConVar weapon_recoil_view_punch_extra;
extern ConVar sv_infinite_ammo;
ConVar weapon_recoil_suppression_shots("weapon_recoil_suppression_shots", "4", FCVAR_REPLICATED | FCVAR_CHEAT, "Number of shots before weapon uses full recoil");
ConVar weapon_recoil_suppression_factor("weapon_recoil_suppression_factor", "0.75", FCVAR_REPLICATED | FCVAR_CHEAT, "Initial recoil suppression factor (first suppressed shot will use this factor * standard recoil, lerping to 1 for later shots");
ConVar weapon_auto_cleanup_time("weapon_auto_cleanup_time", "0", FCVAR_NONE, "If set to non-zero, weapons will delete themselves after the specified time (in seconds) if no players are near.");
ConVar mp_c4timer("mp_c4timer", "40", FCVAR_REPLICATED | FCVAR_NOTIFY, "How long from when the C4 is armed until it blows", true, 10, true, 90);

C_WeaponCSBase* CreateWeapon(int id) {
  switch (id) {
    case WEAPON_KNIFE: return new CKnife();
    case WEAPON_HEGRENADE: return new CHEGrenade();
    case WEAPON_FLASHBANG: return new CFlashbang();
    case WEAPON_SMOKEGRENADE: return new CSmokeGrenade();
    case WEAPON_MOLOTOV: return new CMolotovGrenade();
    case WEAPON_INCGRENADE: return new CIncendiaryGrenade();
    case WEAPON_C4: return new CC4();
    default: return new C_WeaponCSBaseGun(id);
  }
}

C_WeaponCSBase::C_WeaponCSBase(int id) {
  m_iWeaponID = id;
  m_pWeaponInfo = GetWeaponInfo(id);
  m_iClassname = m_pWeaponInfo->className;
  m_iClip1 = m_pWeaponInfo->clipSize;
  m_iPrimaryReserveAmmoCount = m_pWeaponInfo->reserveAmmo;
  if (m_pWeaponInfo->type == WEAPONTYPE_GRENADE) m_iClip1 = 1;
  m_bSilencerOn = m_pWeaponInfo->silencer;
  m_CollisionGroup = COLLISION_GROUP_WEAPON;
}

// ------------------------------------------------------------------------------------------------
// recoil pattern tables (deterministic per weapon, like CS:GO's recoil seed tables)
struct RecoilTable { float angle[64], mag[64]; bool init = false; };
static RecoilTable s_recoil[WEAPON_MAX];
static void GetRecoilOffsets(const CCSWeaponInfo& wi, float index, float& outAngle, float& outMag) {
  RecoilTable& t = s_recoil[wi.id];
  if (!t.init) {
    CUniformRandomStream rnd; rnd.SetSeed(wi.id * 1337 + 7);
    float oldAngle = 0, oldMag = 0;
    for (int j = 0; j < 64; j++) {
      float a = wi.recoilAngle + rnd.RandomFloat(-wi.recoilAngleVariance, wi.recoilAngleVariance);
      float m = wi.recoilMagnitude + rnd.RandomFloat(-wi.recoilMagnitudeVariance, wi.recoilMagnitudeVariance);
      if (j > 0 && wi.fullAuto) { a = oldAngle + (a - oldAngle) * 0.55f; m = oldMag + (m - oldMag) * 0.55f; }
      t.angle[j] = a; t.mag[j] = m; oldAngle = a; oldMag = m;
    }
    t.init = true;
  }
  int i0 = Clamp((int)index, 0, 63), i1 = std::min(63, i0 + 1);
  float f = index - std::floor(index);
  outAngle = t.angle[i0] + (t.angle[i1] - t.angle[i0]) * f;
  outMag = t.mag[i0] + (t.mag[i1] - t.mag[i0]) * f;
  int supp = weapon_recoil_suppression_shots.GetInt();
  if (wi.fullAuto && supp > 1 && index < supp) {
    float k = weapon_recoil_suppression_factor.GetFloat();
    outMag *= k + (1.f - k) * (index / (float)supp);
  }
}

void C_WeaponCSBase::SendViewActivity(int act, float duration) {
  m_iViewActivity = act; m_flViewActivityStart = gpGlobals->curtime; m_flViewActivityDuration = std::max(0.05f, duration);
  if (act == ACT_VM_PRIMARYATTACK) m_iViewShotSide ^= 1;
}
float C_WeaponCSBase::GetViewActivityCycle() const {
  return Clamp((gpGlobals->curtime - m_flViewActivityStart) / m_flViewActivityDuration, 0.f, 1.f);
}

void C_WeaponCSBase::Deploy() {
  CCSPlayer* owner = m_hOwner;
  float t = gpGlobals->curtime + m_pWeaponInfo->deployTime;
  m_flNextPrimaryAttack = std::max(m_flNextPrimaryAttack, t);
  m_flNextSecondaryAttack = std::max(m_flNextSecondaryAttack, t);
  m_flDeployTime = gpGlobals->curtime;
  m_zoomLevel = 0; m_iBurstShotsRemaining = 0; m_bRevolverHaulback = false; m_flPostponeFireReadyTime = 1e30f;
  m_flAccuracyPenalty = m_pWeaponInfo->inaccuracyStand / 1000.f;
  if (owner) { owner->m_flNextAttack = t; owner->m_iShotsFired = 0; owner->m_bIsScoped = false; owner->m_iFOV = 0; }
  SendViewActivity(ACT_VM_DRAW, m_pWeaponInfo->deployTime);
  if (owner) EmitSound(GetWeaponType() == WEAPONTYPE_KNIFE ? "Weapon_Knife.Deploy" : "Weapon.Deploy", owner->m_vecOrigin, owner->entindex(), 0.5f);
}
void C_WeaponCSBase::Holster() {
  m_bInReload = false; m_iBurstShotsRemaining = 0; m_bRevolverHaulback = false; m_flPostponeFireReadyTime = 1e30f;
  ResetZoom();
}
void C_WeaponCSBase::ResetRevolverHaulback() {
  m_flPostponeFireReadyTime = 1e30f;
  if (m_bRevolverHaulback) { m_bRevolverHaulback = false; if (m_iViewActivity == ACT_VM_HAULBACK) SendViewActivity(ACT_VM_IDLE, 0.1f); }
}
void C_WeaponCSBase::ResetZoom() {
  m_zoomLevel = 0;
  if (m_hOwner) { m_hOwner->m_bIsScoped = false; m_hOwner->m_iFOV = 0; }
}
int C_WeaponCSBase::GetZoomFOV(int level) const {
  if (level <= 0) return 0;
  return (int)(level == 1 ? m_pWeaponInfo->zoomFov1 : m_pWeaponInfo->zoomFov2);
}
bool C_WeaponCSBase::CanAttack() const {
  CCSPlayer* o = m_hOwner;
  if (!o || !o->IsAlive()) return false;
  if (o->m_fFlags & FL_FROZEN) return false;
  if (o->m_bIsDefusing) return false;
  if (gpGlobals->curtime < o->m_flNextAttack) return false;
  return true;
}

float C_WeaponCSBase::GetMaxSpeed() const {
  return IsZoomed() ? m_pWeaponInfo->maxSpeedAlt : m_pWeaponInfo->maxSpeed;
}
float C_WeaponCSBase::GetSpread() const {
  extern ConVar weapon_accuracy_nospread;
  if (weapon_accuracy_nospread.GetBool()) return 0;
  return (IsZoomed() ? m_pWeaponInfo->spreadAlt : m_pWeaponInfo->spread) / 1000.f;
}
float C_WeaponCSBase::GetInaccuracy() const {
  extern ConVar weapon_accuracy_nospread;
  CCSPlayer* o = m_hOwner;
  if (!o || weapon_accuracy_nospread.GetBool()) return 0;
  const CCSWeaponInfo& wi = *m_pWeaponInfo;
  float flInaccuracy = m_flAccuracyPenalty;
  float flMaxSpeed = std::max(1.f, GetMaxSpeed());
  float speed = o->m_vecVelocity.Length2D();
  float moveScale = RemapValClamped(speed, flMaxSpeed * CS_PLAYER_SPEED_DUCK_MODIFIER, flMaxSpeed * 0.95f, 0.f, 1.f);
  if (moveScale > 0) {
    if (!(o->m_fFlags & FL_ONGROUND) == false) flInaccuracy += moveScale * wi.inaccuracyMove / 1000.f;
  }
  if (!(o->m_fFlags & FL_ONGROUND)) {
    float vz = std::fabs(o->m_vecVelocity.z);
    float jumpScale = RemapValClamped(vz, 0.f, 300.f, 0.4f, 1.f);
    flInaccuracy += wi.inaccuracyJump / 1000.f * jumpScale + moveScale * wi.inaccuracyMove / 1000.f * 0.5f;
  }
  return std::min(flInaccuracy, 1.0f);
}
void C_WeaponCSBase::UpdateAccuracyPenalty() {
  CCSPlayer* o = m_hOwner; if (!o) return;
  const CCSWeaponInfo& wi = *m_pWeaponInfo;
  float ft = gpGlobals->interval_per_tick;
  bool zoomed = IsZoomed();
  float fNewPenalty = 0;
  if (o->m_fFlags & FL_ONGROUND) {
    bool ducked = o->m_flDuckAmount > 0.5f;
    fNewPenalty += (ducked ? (zoomed ? wi.inaccuracyCrouchAlt : wi.inaccuracyCrouch) : (zoomed ? wi.inaccuracyStandAlt : wi.inaccuracyStand)) / 1000.f;
  } else fNewPenalty += wi.inaccuracyStand / 1000.f;
  if (o->m_PlayerAnimState.m_bLanding && o->m_PlayerAnimState.m_flLandAmount > 0.25f) fNewPenalty += wi.inaccuracyLand / 1000.f * o->m_PlayerAnimState.m_flLandAmount;
  if (m_flAccuracyPenalty < fNewPenalty) m_flAccuracyPenalty = fNewPenalty;
  else {
    float recovery = (o->m_flDuckAmount > 0.5f ? wi.recoveryTimeCrouch : wi.recoveryTimeStand);
    if (recovery <= 0) recovery = 0.3f;
    float decay = std::log(10.f) / recovery;
    m_flAccuracyPenalty = fNewPenalty + (m_flAccuracyPenalty - fNewPenalty) * std::exp(ft * -decay);
  }
  // recoil index decay when not shooting
  if (gpGlobals->curtime > m_fLastShotTime + wi.cycleTime * 1.2f) {
    m_flRecoilIndex = std::max(0.f, m_flRecoilIndex - ft / std::max(0.05f, wi.cycleTime) * 0.6f);
  }
}

void C_WeaponCSBase::ItemPostFrame() {
  CCSPlayer* o = m_hOwner; if (!o) return;
  int buttons = o->m_nButtons;
  float now = gpGlobals->curtime;
  UpdateAccuracyPenalty();
  if (m_bInReload && now >= m_flReloadFinishTime) FinishReload();
  // burst continuation
  if (m_iBurstShotsRemaining > 0 && now >= m_fNextBurstShot) {
    C_WeaponCSBaseGun* gun = dynamic_cast<C_WeaponCSBaseGun*>(this);
    if (gun && m_iClip1 > 0) { gun->CSBaseGunFire(0.05f, 2); m_iBurstShotsRemaining--; m_fNextBurstShot = now + 0.05f; if (m_iBurstShotsRemaining == 0) { m_flNextPrimaryAttack = now + (m_iWeaponID == WEAPON_GLOCK ? 0.5f : 0.55f); } }
    else m_iBurstShotsRemaining = 0;
  }
  if (IsRevolver()) {
    // CS:GO CWeaponCSBase::ItemPostFrame order for the R8: primary first, then secondary, reload, and the hammer only
    // drops (ItemPostFrame_RevolverResetHaulback) when none of the attack buttons is held. Holding attack2 while
    // m_flNextSecondaryAttack is still in the future does nothing at all - the hammer stays back.
    if (!m_bInReload && (buttons & IN_ATTACK) && now >= m_flNextPrimaryAttack && CanAttack()) PrimaryAttack();
    else if (!m_bInReload && (buttons & IN_ATTACK2) && now >= m_flNextSecondaryAttack && CanAttack()) SecondaryAttack();
    else if ((buttons & IN_RELOAD) && !m_bInReload && now >= m_flNextPrimaryAttack) { ResetRevolverHaulback(); Reload(); }
    else if (!(buttons & (IN_ATTACK | IN_ATTACK2))) { ResetRevolverHaulback(); o->m_iShotsFired = 0; }
  } else if (!m_bInReload && (buttons & IN_ATTACK2) && now >= m_flNextSecondaryAttack && CanAttack()) {
    SecondaryAttack();
  } else if (!m_bInReload && (buttons & IN_ATTACK) && now >= m_flNextPrimaryAttack && CanAttack()) {
    if (IsGun() && !IsAutoRepeat() && o->m_iShotsFired > 0 && m_iWeaponID != WEAPON_REVOLVER) { /* semi-auto: wait for release */ }
    else PrimaryAttack();
  } else if ((buttons & IN_RELOAD) && !m_bInReload && IsGun() && now >= m_flNextPrimaryAttack) {
    Reload();
  }
  if (!(buttons & IN_ATTACK) && !IsRevolver()) {
    if (o->m_iShotsFired > 0 && !IsAutoRepeat()) o->m_iShotsFired = 0;
    else if (IsAutoRepeat()) o->m_iShotsFired = 0;
  }
  // auto reload on empty
  if (IsGun() && !m_bInReload && m_iClip1 == 0 && m_iPrimaryReserveAmmoCount > 0 && now >= m_flNextPrimaryAttack) Reload();
  // re-zoom after a bolt action shot
  if (o->m_bResumeZoom && now >= m_flNextPrimaryAttack && !m_bInReload) {
    o->m_bResumeZoom = false;
    if (m_iClip1 > 0) { m_zoomLevel = 1; o->m_bIsScoped = true; o->m_iFOV = GetZoomFOV(1); }
  }
  if (m_iViewActivity != ACT_VM_IDLE && now > m_flViewActivityStart + m_flViewActivityDuration) m_iViewActivity = ACT_VM_IDLE;
}

void C_WeaponCSBase::PrimaryAttack() {}
void C_WeaponCSBase::SecondaryAttack() {}

bool C_WeaponCSBase::Reload() {
  CCSPlayer* o = m_hOwner; if (!o) return false;
  if (m_bInReload || m_iClip1 >= m_pWeaponInfo->clipSize || m_iPrimaryReserveAmmoCount <= 0 || m_iClip1 < 0) return false;
  m_bInReload = true;
  m_flReloadStartTime = gpGlobals->curtime;
  m_flReloadFinishTime = gpGlobals->curtime + m_pWeaponInfo->reloadTime;
  bool shotgun = GetWeaponType() == WEAPONTYPE_SHOTGUN;
  if (!shotgun) o->m_flNextAttack = m_flReloadFinishTime;
  m_flNextPrimaryAttack = m_flReloadFinishTime;
  ResetZoom(); o->m_bResumeZoom = false;
  SendViewActivity(ACT_VM_RELOAD, m_pWeaponInfo->reloadTime);
  m_bReloadVisuallyComplete = false;
  char snd[64]; snprintf(snd, sizeof(snd), "Weapon_%s.Reload", WeaponIdToAlias(m_iWeaponID));
  EmitSound(snd, o->m_vecOrigin, o->entindex(), 0.6f);
  IGameEvent ev("weapon_reload"); ev.SetInt("userid", o->entindex()); gameeventmanager->FireEvent(ev);
  return true;
}
void C_WeaponCSBase::FinishReload() {
  CCSPlayer* o = m_hOwner;
  if (GetWeaponType() == WEAPONTYPE_SHOTGUN) {
    // shell by shell
    if (m_iPrimaryReserveAmmoCount > 0 && m_iClip1 < m_pWeaponInfo->clipSize) { m_iClip1++; m_iPrimaryReserveAmmoCount--; }
    if (m_iClip1 < m_pWeaponInfo->clipSize && m_iPrimaryReserveAmmoCount > 0 && !(o && (o->m_nButtons & IN_ATTACK) && m_iClip1 > 0)) {
      m_flReloadStartTime = gpGlobals->curtime; m_flReloadFinishTime = gpGlobals->curtime + m_pWeaponInfo->reloadTime;
      m_flNextPrimaryAttack = std::min(m_flNextPrimaryAttack, gpGlobals->curtime);
      SendViewActivity(ACT_VM_RELOAD, m_pWeaponInfo->reloadTime);
      if (o) EmitSound("Weapon_Shotgun.InsertShell", o->m_vecOrigin, o->entindex(), 0.5f);
      return;
    }
    m_bInReload = false; m_bReloadVisuallyComplete = true; m_flNextPrimaryAttack = gpGlobals->curtime + 0.3f;
    return;
  }
  int need = m_pWeaponInfo->clipSize - m_iClip1;
  int take = std::min(need, m_iPrimaryReserveAmmoCount);
  m_iClip1 += take; if (!sv_infinite_ammo.GetBool()) m_iPrimaryReserveAmmoCount -= take;
  m_bInReload = false; m_bReloadVisuallyComplete = true;
  if (o) o->m_iShotsFired = 0;
}

void C_WeaponCSBase::OnDropped() { m_bOnGround = false; }

// dropped weapon physics
void C_WeaponCSBase::PhysicsSimulate() {
  if (m_iState != WEAPON_NOT_CARRIED) return;
  float ft = gpGlobals->interval_per_tick;
  m_vecOldOrigin = m_vecOrigin;
  if (m_bOnGround && m_vecVelocity.LengthSqr() < 1.f) return;
  m_vecVelocity.z -= 800.f * ft;
  Vector end = m_vecOrigin + m_vecVelocity * ft;
  trace_t tr; Ray_t ray; ray.Init(m_vecOrigin, end, Vector(-3, -3, 0), Vector(3, 3, 3));
  CTraceFilterWorldOnly f; g_EngineTrace.TraceRay(ray, MASK_SOLID, &f, &tr);
  if (tr.startsolid) { m_vecOrigin.z += 2.f; m_vecVelocity.Init(); return; }
  m_vecOrigin = tr.endpos;
  if (tr.fraction < 1.f) {
    Vector n = tr.plane.normal;
    float back = m_vecVelocity.Dot(n) * 1.3f;
    m_vecVelocity -= n * back;
    if (n.z > 0.7f) {
      m_vecVelocity *= 0.5f;
      if (m_vecVelocity.Length() < 30.f) { m_vecVelocity.Init(); m_bOnGround = true; }
    }
    if (!m_bOnGround && m_vecVelocity.Length() > 80.f) EmitSound("Weapon.ImpactSoft", m_vecOrigin, entindex(), 0.4f);
  }
  m_angRotation.y += m_bOnGround ? 0 : 360.f * ft;
}

void C_WeaponCSBase::Think() {
  if (m_iState != WEAPON_NOT_CARRIED) return;
  float now = ServerTime();
  // pickup by touching
  for (CCSPlayer* p : UTIL_GetPlayers(true)) {
    if (p->m_vecOrigin.DistTo(m_vecOrigin) > 44.f || std::fabs(p->m_vecOrigin.z + 20 - m_vecOrigin.z) > 60.f) continue;
    if (p == m_hPrevOwner && now < m_flDroppedTime + 1.5f) continue;
    if (now < m_flDroppedTime + 0.4f) continue;
    const CCSWeaponInfo& wi = *m_pWeaponInfo;
    if (m_iWeaponID == WEAPON_C4 && p->m_iTeamNum != TEAM_TERRORIST) continue;
    if (wi.slot == WEAPON_SLOT_GRENADES) { if (p->Weapon_OwnsThisType(m_iWeaponID) || p->GetGrenadeCount() >= 4) continue; }
    else if (p->Weapon_GetSlot(wi.slot)) continue;
    // pick up
    m_iState = WEAPON_IS_CARRIED_BY_PLAYER; m_hOwner = p; m_hOwnerEntity = p; m_iTeamNum = p->m_iTeamNum;
    for (auto*& s : p->m_hMyWeapons) if (!s) { s = this; break; }
    EmitSound("Player.PickupWeapon", p->m_vecOrigin, p->entindex(), 0.7f);
    if (m_iWeaponID == WEAPON_C4) { CSGameRules()->m_bBombDropped = false; IGameEvent ev("bomb_pickup"); ev.SetInt("userid", p->entindex()); gameeventmanager->FireEvent(ev); }
    C_WeaponCSBase* act = p->GetActiveCSWeapon();
    if (!act || (act->GetWeaponType() == WEAPONTYPE_KNIFE && wi.slot <= WEAPON_SLOT_PISTOL) || (wi.slot == WEAPON_SLOT_RIFLE && act->GetCSWpnData().slot == WEAPON_SLOT_PISTOL)) p->Weapon_Switch(this);
    IGameEvent ev("item_pickup"); ev.SetInt("userid", p->entindex()).SetString("item", WeaponIdToAlias(m_iWeaponID)); gameeventmanager->FireEvent(ev);
    return;
  }
}

// ------------------------------------------------------------------------------------------------
// guns
bool C_WeaponCSBaseGun::CSBaseGunFire(float cycleTime, int mode) {
  CCSPlayer* o = m_hOwner; if (!o) return false;
  const CCSWeaponInfo& wi = *m_pWeaponInfo;
  float now = gpGlobals->curtime;
  if (m_iClip1 <= 0) {
    EmitSound("Weapon.DryFire", o->m_vecOrigin, o->entindex(), 0.6f);
    m_flNextPrimaryAttack = now + 0.2f;
    if (m_iPrimaryReserveAmmoCount > 0) Reload();
    return false;
  }
  o->m_iShotsFired++;
  if (!sv_infinite_ammo.GetBool()) m_iClip1--;
  m_fLastShotTime = now;
  o->m_flLastFireTime = now;
  o->m_bShotThisChoke = true;
  Vector eye = o->EyePosition();
  QAngle ang = o->v_angle;
  ang.x += o->m_aimPunchAngle.x * weapon_recoil_scale.GetFloat();
  ang.y += o->m_aimPunchAngle.y * weapon_recoil_scale.GetFloat();
  float inacc = GetInaccuracy(), spread = GetSpread();
  int seed = o->m_pCurrentCommand ? o->m_pCurrentCommand->random_seed : RandomInt(0, 0x7fffffff);
  o->FX_FireBullets(m_iWeaponID, eye, ang, seed & 255, inacc, spread, o->m_pCurrentCommand);
  char snd[64];
  snprintf(snd, sizeof(snd), m_bSilencerOn && wi.silencer ? "Weapon_%s.Silenced" : "Weapon_%s.Single", WeaponIdToAlias(m_iWeaponID));
  EmitSound(snd, eye, o->entindex());
  IGameEvent ev("weapon_fire"); ev.SetInt("userid", o->entindex()).SetString("weapon", WeaponIdToAlias(m_iWeaponID)).SetBool("silenced", m_bSilencerOn && wi.silencer)
    .SetFloat("inaccuracy", inacc).SetInt("seed", seed & 255);
  gameeventmanager->FireEvent(ev);
  // accuracy penalty and recoil
  m_flAccuracyPenalty += (mode == 1 && IsZoomed() ? wi.inaccuracyFire * 0.5f : wi.inaccuracyFire) / 1000.f;
  m_flAccuracyPenalty = std::min(m_flAccuracyPenalty, 1.f);
  float fAngle, fMag;
  GetRecoilOffsets(wi, m_flRecoilIndex, fAngle, fMag);
  QAngle vel(-std::cos(DEG2RADF(fAngle)) * fMag, std::sin(DEG2RADF(fAngle)) * fMag, 0);
  o->m_aimPunchAngleVel.x += vel.x; o->m_aimPunchAngleVel.y += vel.y;
  o->m_viewPunchAngle.x -= fMag * weapon_recoil_view_punch_extra.GetFloat();
  m_flRecoilIndex += 1.f;
  m_flNextPrimaryAttack = m_flNextSecondaryAttack = now + cycleTime;
  if (m_iWeaponID == WEAPON_REVOLVER) m_flNextSecondaryAttack = now + (mode == 1 ? wi.cycleTimeAlt : cycleTime);
  m_flTimeWeaponIdle = now + 2.f;
  SendViewActivity(ACT_VM_PRIMARYATTACK, std::min(0.35f, std::max(0.12f, cycleTime)));
  // bolt action snipers un-zoom after each shot
  if ((m_iWeaponID == WEAPON_AWP || m_iWeaponID == WEAPON_SSG08) && IsZoomed()) { o->m_bResumeZoom = true; ResetZoom(); }
  return true;
}

void C_WeaponCSBaseGun::PrimaryAttack() {
  CCSPlayer* o = m_hOwner; if (!o) return;
  const CCSWeaponInfo& wi = *m_pWeaponInfo;
  if (m_iWeaponID == WEAPON_REVOLVER) {
    // CS:GO ItemPostFrame_ProcessPrimaryAttack: no rapid fire in the middle of a haul back hold
    const float now = gpGlobals->curtime;
    m_flNextSecondaryAttack = now + 0.25f;
    if (!m_bRevolverHaulback) {
      // start pulling the hammer back; the haul back animation event arms the shot 0.234s (15 ticks) later
      m_bRevolverHaulback = true; m_flPostponeFireReadyTime = now + 0.234375f - 0.001f;
      SendViewActivity(ACT_VM_HAULBACK, 999.f);
      EmitSound("Weapon_Revolver.Prepare", o->m_vecOrigin + Vector(0, 0, 50), o->entindex(), 0.5f);
      return;
    }
    if (!(m_flPostponeFireReadyTime < now)) return;
    if (m_iClip1 <= 0) { ResetRevolverHaulback(); m_flNextPrimaryAttack = m_flNextSecondaryAttack = now + 0.5f; }
    m_bRevolverHaulback = false; m_flPostponeFireReadyTime = 1e30f;
    if (CSBaseGunFire(wi.cycleTime, 0)) m_flNextSecondaryAttack = now + wi.cycleTimeAlt * 1.7f;   // cool-off before fanning
    return;
  }
  if (m_bBurstMode && (m_iWeaponID == WEAPON_GLOCK || m_iWeaponID == WEAPON_FAMAS)) {
    if (!CSBaseGunFire(0.05f, 2)) return;
    m_iBurstShotsRemaining = 2; m_fNextBurstShot = gpGlobals->curtime + 0.05f;
    m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f;
    return;
  }
  CSBaseGunFire(wi.cycleTime, IsZoomed() ? 1 : 0);
}

void C_WeaponCSBaseGun::SecondaryAttack() {
  CCSPlayer* o = m_hOwner; if (!o) return;
  const CCSWeaponInfo& wi = *m_pWeaponInfo;
  float now = gpGlobals->curtime;
  if (wi.zoomLevels > 0) {
    m_zoomLevel = (m_zoomLevel + 1) % (wi.zoomLevels + 1);
    o->m_bIsScoped = m_zoomLevel > 0; o->m_iFOV = GetZoomFOV(m_zoomLevel);
    o->m_bResumeZoom = false;
    EmitSound("Default.Zoom", o->m_vecOrigin, o->entindex(), 0.4f);
    m_flNextSecondaryAttack = now + 0.3f;
    IGameEvent ev("weapon_zoom"); ev.SetInt("userid", o->entindex()); gameeventmanager->FireEvent(ev);
  } else if (wi.silencer) {
    m_bSilencerOn = !m_bSilencerOn;
    SendViewActivity(m_bSilencerOn ? ACT_VM_ATTACH_SILENCER : ACT_VM_DETACH_SILENCER, 2.0f);
    m_flNextPrimaryAttack = m_flNextSecondaryAttack = now + 2.0f;
    m_flDoneSwitchingSilencer = now + 2.0f;
    EmitSound(m_bSilencerOn ? "Weapon_Silencer.On" : "Weapon_Silencer.Off", o->m_vecOrigin, o->entindex(), 0.5f);
  } else if (m_iWeaponID == WEAPON_GLOCK || m_iWeaponID == WEAPON_FAMAS) {
    m_bBurstMode = !m_bBurstMode;
    if (o->IsLocalPlayer()) Msg(m_bBurstMode ? "Switched to Burst-Fire mode\n" : (m_iWeaponID == WEAPON_GLOCK ? "Switched to semi-automatic\n" : "Switched to automatic fire\n"));
    m_flNextSecondaryAttack = now + 0.3f;
  } else if (m_iWeaponID == WEAPON_REVOLVER) {
    // CS:GO ItemPostFrame_ProcessSecondaryAttack: attack2 lowers a pulled-back hammer first (no shot this tick),
    // and fanning is not automatic - shots fired resets when the buttons are released
    if (m_bRevolverHaulback) { ResetRevolverHaulback(); return; }
    m_flPostponeFireReadyTime = 1e30f;
    if (o->m_iShotsFired > 0) return;
    CSBaseGunFire(wi.cycleTimeAlt, 1);
    m_flAccuracyPenalty += wi.inaccuracyFire / 1000.f;
    m_flNextPrimaryAttack = now + wi.cycleTimeAlt;
  } else {
    m_flNextSecondaryAttack = now + 0.5f;
  }
}

// ------------------------------------------------------------------------------------------------
// knife
void CKnife::PrimaryAttack() { SwingOrStab(false); }
void CKnife::SecondaryAttack() { SwingOrStab(true); }
bool CKnife::SwingOrStab(bool stab) {
  CCSPlayer* o = m_hOwner; if (!o) return false;
  float now = gpGlobals->curtime;
  float range = stab ? 32.f : 48.f;
  Vector fwd; AngleVectors(o->v_angle, &fwd);
  Vector src = o->EyePosition(), end = src + fwd * range;
  trace_t tr;
  Ray_t ray; ray.Init(src, end);
  CTraceFilter filter(o);
  g_EngineTrace.TraceRay(ray, MASK_SHOT, &filter, &tr);
  if (tr.fraction >= 1.f) {
    Ray_t hull; hull.Init(src, end, Vector(-16, -16, -18), Vector(16, 16, 18));
    g_EngineTrace.TraceRay(hull, MASK_SOLID, &filter, &tr);
  }
  bool hit = tr.fraction < 1.f;
  bool first = now > m_fLastShotTime + 0.4f;
  m_fLastShotTime = now;
  o->m_flLastFireTime = now; o->m_bShotThisChoke = true;
  if (stab) { m_flNextPrimaryAttack = m_flNextSecondaryAttack = now + (hit ? 1.1f : 1.0f); }
  else { float c = hit ? 0.5f : (first ? 0.4f : 0.35f); m_flNextPrimaryAttack = now + c; m_flNextSecondaryAttack = now + 0.5f; }
  SendViewActivity(stab ? ACT_VM_SECONDARYATTACK : ACT_VM_PRIMARYATTACK, stab ? 0.9f : 0.4f);
  IGameEvent ev("weapon_fire"); ev.SetInt("userid", o->entindex()).SetString("weapon", "knife"); gameeventmanager->FireEvent(ev);
  if (!hit) { EmitSound("Weapon_Knife.Slash", src, o->entindex(), 0.6f); return false; }
  if (CCSPlayer* victim = ToCSPlayer(tr.m_pEnt)) {
    Vector vfwd; AngleVectors(QAngle(0, victim->m_net.eyeAngles.y, 0), &vfwd);
    Vector toVictim = victim->m_vecOrigin - o->m_vecOrigin; toVictim.z = 0; toVictim.NormalizeInPlace();
    bool backstab = vfwd.Dot(toVictim) > 0.475f;
    float dmg = stab ? (backstab ? 180.f : 65.f) : (backstab ? 90.f : (first ? 40.f : 25.f));
    CTakeDamageInfo info; info.m_hAttacker = o; info.m_hInflictor = this; info.m_iWeaponID = WEAPON_KNIFE; info.m_flDamage = dmg;
    info.m_bitsDamageType = DMG_SLASH; info.m_iHitGroup = tr.hitgroup ? tr.hitgroup : HITGROUP_CHEST; info.m_iHitbox = tr.hitbox; info.m_vecDamagePosition = tr.endpos; info.m_vecDamageForce = fwd * 300.f;
    victim->OnTakeDamage(info);
    EmitSound(stab ? "Weapon_Knife.Stab" : "Weapon_Knife.Hit", tr.endpos, o->entindex());
  } else if (CHostage* h = ToHostage(tr.m_pEnt)) {
    if (h->IsAlive()) {
      CTakeDamageInfo info; info.m_hAttacker = o; info.m_hInflictor = this; info.m_iWeaponID = WEAPON_KNIFE;
      info.m_flDamage = stab ? 130.f : 55.f;
      info.m_bitsDamageType = DMG_SLASH; info.m_iHitGroup = HITGROUP_CHEST; info.m_vecDamagePosition = tr.endpos; info.m_vecDamageForce = fwd * 300.f;
      h->OnTakeDamage(info);
      EmitSound(stab ? "Weapon_Knife.Stab" : "Weapon_Knife.Hit", tr.endpos, o->entindex());
    }
  } else {
    EmitSound("Weapon_Knife.HitWall", tr.endpos, o->entindex(), 0.7f);
    IGameEvent im("bullet_impact"); im.SetInt("userid", o->entindex()).SetFloat("x", tr.endpos.x).SetFloat("y", tr.endpos.y).SetFloat("z", tr.endpos.z).SetInt("surf", tr.surface.surfaceProps)
      .SetFloat("nx", tr.plane.normal.x).SetFloat("ny", tr.plane.normal.y).SetFloat("nz", tr.plane.normal.z).SetInt("knife", 1);
    gameeventmanager->FireEvent(im);
  }
  return true;
}

// ------------------------------------------------------------------------------------------------
// grenades
void CBaseCSGrenade::Deploy() { C_WeaponCSBase::Deploy(); m_bPinPulled = false; m_fThrowTime = 0; m_bRedraw = false; }
void CBaseCSGrenade::Holster() { m_bPinPulled = false; m_fThrowTime = 0; C_WeaponCSBase::Holster(); }
void CBaseCSGrenade::PrimaryAttack() {
  if (m_bPinPulled || m_fThrowTime > 0) return;
  m_bPinPulled = true; m_flThrowStrength = 1.f;
  SendViewActivity(ACT_VM_PULLPIN, 0.4f);
  m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime + 0.3f;
  if (m_hOwner) EmitSound(m_iWeaponID == WEAPON_MOLOTOV ? "Molotov.Lighter" : "Weapon_Grenade.PullPin", m_hOwner->m_vecOrigin, m_hOwner->entindex(), 0.4f);
}
void CBaseCSGrenade::SecondaryAttack() {
  if (m_bPinPulled || m_fThrowTime > 0) return;
  m_bPinPulled = true; m_flThrowStrength = 0.f;
  SendViewActivity(ACT_VM_PULLPIN, 0.4f);
  m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime + 0.3f;
}
void CBaseCSGrenade::ItemPostFrame() {
  CCSPlayer* o = m_hOwner; if (!o) return;
  int b = o->m_nButtons;
  float now = gpGlobals->curtime;
  if (m_bPinPulled) {
    // both buttons = medium throw (jumpthrow binds rely on releasing)
    if ((b & IN_ATTACK) && (b & IN_ATTACK2)) m_flThrowStrength = 0.5f;
    else if (b & IN_ATTACK) m_flThrowStrength = std::max(m_flThrowStrength, 1.f);
    if (!(b & (IN_ATTACK | IN_ATTACK2)) && now >= m_flNextPrimaryAttack) {
      SendViewActivity(ACT_VM_THROW, 0.45f);
      m_bPinPulled = false; m_fThrowTime = now + 0.1f;
    }
  } else if (m_fThrowTime > 0 && now >= m_fThrowTime) {
    m_fThrowTime = 0;
    ThrowGrenade();
    return; // weapon may be deleted
  } else if (m_bRedraw && now >= m_flNextPrimaryAttack) {
    m_bRedraw = false;
    if (m_iClip1 > 0) Deploy();
  } else C_WeaponCSBase::ItemPostFrame();
}
void CBaseCSGrenade::ThrowGrenade() {
  CCSPlayer* o = m_hOwner; if (!o) return;
  Vector src, vel;
  QAngle ang = o->v_angle;
  GetGrenadeThrowParams(o, ang, m_flThrowStrength, src, vel);
  CBaseCSGrenadeProjectile* proj = nullptr;
  if (m_iWeaponID == WEAPON_HEGRENADE) proj = new CHEGrenadeProjectile(m_iWeaponID, o, src, vel);
  else if (m_iWeaponID == WEAPON_FLASHBANG) proj = new CFlashbangProjectile(m_iWeaponID, o, src, vel);
  else if (IsFireGrenade(m_iWeaponID)) proj = new CMolotovProjectile(m_iWeaponID, o, src, vel);
  else proj = new CSmokeGrenadeProjectile(m_iWeaponID, o, src, vel);
  g_EntityList.Add(proj);
  o->m_flLastFireTime = gpGlobals->curtime; o->m_bShotThisChoke = true;
  EmitSound(IsFireGrenade(m_iWeaponID) ? "Molotov.Throw" : "Weapon_Grenade.Throw", src, o->entindex(), 0.6f);
  IGameEvent ev("grenade_thrown"); ev.SetInt("userid", o->entindex()).SetString("weapon", WeaponIdToAlias(m_iWeaponID)); gameeventmanager->FireEvent(ev);
  m_iClip1--;
  m_flNextPrimaryAttack = m_flNextSecondaryAttack = gpGlobals->curtime + 0.6f;
  if (m_iClip1 <= 0) {
    // used up: remove and switch to the last weapon
    CCSPlayer* owner = o;
    for (auto*& s : owner->m_hMyWeapons) if (s == this) s = nullptr;
    owner->m_hActiveWeapon = nullptr;
    Remove();
    C_WeaponCSBase* next = owner->m_hLastWeapon && owner->m_hLastWeapon != this ? owner->m_hLastWeapon : nullptr;
    if (!next) next = owner->Weapon_GetSlot(WEAPON_SLOT_RIFLE);
    if (!next) next = owner->Weapon_GetSlot(WEAPON_SLOT_PISTOL);
    if (!next) next = owner->Weapon_GetSlot(WEAPON_SLOT_KNIFE);
    owner->m_hLastWeapon = nullptr;
    if (next) owner->Weapon_Switch(next);
  } else m_bRedraw = true;
}

// ------------------------------------------------------------------------------------------------
// C4
void CC4::PrimaryAttack() {
  CCSPlayer* o = m_hOwner; if (!o) return;
  if (m_bStartedArming) return;
  if (!CSGameRules()->m_bRoundInProgress || CSGameRules()->m_bFreezePeriod) { m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f; return; }
  if (!o->m_bInBombZone) {
    if (o->IsLocalPlayer()) ConColorMsg({ 255, 180, 60, 255 }, "C4 must be planted at a bomb site.\n");
    m_flNextPrimaryAttack = gpGlobals->curtime + 1.f; return;
  }
  if (!(o->m_fFlags & FL_ONGROUND)) {
    if (o->IsLocalPlayer()) ConColorMsg({ 255, 180, 60, 255 }, "You must be standing on the ground to plant the C4.\n");
    m_flNextPrimaryAttack = gpGlobals->curtime + 1.f; return;
  }
  m_bStartedArming = true; m_fArmedTime = gpGlobals->curtime + 3.0f;
  o->m_bIsPlanting = true; o->m_flPlantStartTime = gpGlobals->curtime; o->SetProgressBarTime(3);
  SendViewActivity(ACT_VM_PLANT, 3.0f);
  EmitSound("c4.disarmstart", o->m_vecOrigin, o->entindex(), 0.5f);
  IGameEvent ev("bomb_beginplant"); ev.SetInt("userid", o->entindex()); gameeventmanager->FireEvent(ev);
}
void CC4::AbortArming() {
  m_bStartedArming = false;
  if (m_hOwner) { m_hOwner->m_bIsPlanting = false; m_hOwner->SetProgressBarTime(0); }
  m_flNextPrimaryAttack = gpGlobals->curtime + 0.5f;
  IGameEvent ev("bomb_abortplant"); ev.SetInt("userid", m_hOwner ? m_hOwner->entindex() : 0); gameeventmanager->FireEvent(ev);
}
void CC4::Holster() { if (m_bStartedArming) AbortArming(); C_WeaponCSBase::Holster(); }
void CC4::ItemPostFrame() {
  CCSPlayer* o = m_hOwner; if (!o) return;
  if (m_bStartedArming) {
    if (!(o->m_nButtons & IN_ATTACK) || !o->m_bInBombZone || !(o->m_fFlags & FL_ONGROUND) || !CSGameRules()->m_bRoundInProgress) { AbortArming(); return; }
    if (gpGlobals->curtime >= m_fArmedTime) {
      m_bStartedArming = false;
      o->m_bIsPlanting = false; o->SetProgressBarTime(0);
      // plant at the feet
      trace_t tr; UTIL_TraceLine(o->m_vecOrigin + Vector(0, 0, 8), o->m_vecOrigin - Vector(0, 0, 64), MASK_SOLID, o, &tr);
      auto* bomb = new CPlantedC4(o, tr.endpos, o->v_angle.y);
      g_EntityList.Add(bomb);
      for (auto*& s : o->m_hMyWeapons) if (s == this) s = nullptr;
      o->m_hActiveWeapon = nullptr;
      Remove();
      C_WeaponCSBase* next = o->Weapon_GetSlot(WEAPON_SLOT_RIFLE); if (!next) next = o->Weapon_GetSlot(WEAPON_SLOT_PISTOL); if (!next) next = o->Weapon_GetSlot(WEAPON_SLOT_KNIFE);
      o->m_hLastWeapon = nullptr;
      if (next) o->Weapon_Switch(next);
      CSGameRules()->OnBombPlanted(o);
    }
    return;
  }
  C_WeaponCSBase::ItemPostFrame();
}

