// cs_player.cpp - CCSPlayer shared/server logic: spawn, usercmd processing, network snapshots (choke/desync),
// damage & death, inventory, buy menu backend.
#include "cs_player.hpp"
#include "cs_loadout.hpp"
#include "weapon_csbase.hpp"
#include "gamemovement.hpp"
#include "cs_gamerules.hpp"
#include "cs_projectiles.hpp"
#include "hostage.hpp"
#include "lagcompensation.hpp"
#include "gameevents.hpp"
#include "soundemitter.hpp"
#include "mapinfo.hpp"
#include "../../engine/random.hpp"
#include <cmath>
#include <algorithm>
#include <iterator>

ConVar mp_friendlyfire("mp_friendlyfire", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "Allows team members to injure other members of their team");
ConVar mp_buy_anyteam("mp_buy_anyteam", "1", FCVAR_REPLICATED | FCVAR_NOTIFY, "1 = both teams can buy every weapon (team-specific guns, defuse kit)");
ConVar mp_buytime("mp_buytime", "20", FCVAR_REPLICATED | FCVAR_NOTIFY, "How many seconds after round start players can buy items for");
ConVar mp_buy_anywhere("mp_buy_anywhere", "0", FCVAR_REPLICATED | FCVAR_NOTIFY, "When set, players can buy anywhere, not only in buyzones");
ConVar mp_maxmoney("mp_maxmoney", "16000", FCVAR_REPLICATED | FCVAR_NOTIFY, "Maximum amount of money allowed in a player's account");
ConVar sv_maxusrcmdprocessticks("sv_maxusrcmdprocessticks", "16", FCVAR_REPLICATED | FCVAR_NOTIFY, "Maximum number of client-issued usrcmd ticks that can be replayed (max choke + 1)", true, 1, true, 62);
ConVar weapon_recoil_decay2_exp("weapon_recoil_decay2_exp", "8", FCVAR_REPLICATED | FCVAR_CHEAT, "Decay factor exponential term");
ConVar weapon_recoil_decay2_lin("weapon_recoil_decay2_lin", "18", FCVAR_REPLICATED | FCVAR_CHEAT, "Decay factor (linear term)");
ConVar weapon_recoil_vel_decay("weapon_recoil_vel_decay", "4.5", FCVAR_REPLICATED | FCVAR_CHEAT, "Decay factor for recoil velocity");
ConVar weapon_recoil_view_punch_extra("weapon_recoil_view_punch_extra", "0.055", FCVAR_REPLICATED | FCVAR_CHEAT, "Additional (non-aim) punch added to view from recoil");
ConVar weapon_recoil_scale("weapon_recoil_scale", "2.0", FCVAR_REPLICATED | FCVAR_CHEAT, "Overall scale factor for recoil");
ConVar sv_infinite_ammo("sv_infinite_ammo", "0", FCVAR_REPLICATED | FCVAR_CHEAT, "Player's active weapon will never run out of ammo");
ConVar god("god_mode_all", "0", FCVAR_CHEAT, "Nobody takes damage");


CCSPlayer* ToCSPlayer(C_BaseEntity* e) { return (e && e->IsPlayer()) ? static_cast<CCSPlayer*>(e) : nullptr; }
CCSPlayer* GetLocalPlayer() { return g_pLocalPlayer; }

CCSPlayer::CCSPlayer() {
  m_iEntClass = ENTCLASS_PLAYER; m_iClassname = "player";
  m_MoveType = MOVETYPE_WALK; m_vecMins = { -16, -16, 0 }; m_vecMaxs = { 16, 16, 72 };
  m_lifeState = LIFE_DEAD; m_iHealth = 0; m_takedamage = 1;
  m_fFlags = FL_CLIENT;
}
CCSPlayer::~CCSPlayer() = default;


bool CCSPlayer::IsEnemy(const CCSPlayer* o) const {
  if (!o || o == this) return false;
  if (CSGameRules()->IsDeathmatch() && mp_friendlyfire.GetBool()) return true;
  return o->m_iTeamNum != m_iTeamNum;
}

// ------------------------------------------------------------------------------------------------
void CCSPlayer::Reset(bool keepMoney) {
  RemoveAllItems(true);
  if (!keepMoney) m_iAccount = FindConVar("mp_startmoney") ? FindConVar("mp_startmoney")->GetInt() : 800;
  m_iKills = m_iDeaths = m_iAssists = m_iMVPs = m_iScore = m_iHeadshots = 0;
}

void CCSPlayer::Spawn() {
  Vector origin; float yaw = 0;
  CSGameRules()->SelectSpawnPoint(this, origin, yaw);
  m_vecOrigin = m_vecOldOrigin = m_vecRenderOrigin = origin;
  m_vecVelocity.Init(); m_vecBaseVelocity.Init();
  v_angle = m_angEyeAngles = QAngle(0, yaw, 0);
  m_angRotation = QAngle(0, yaw, 0);
  m_iHealth = 100; m_iMaxHealth = 100; m_lifeState = LIFE_ALIVE; m_takedamage = 1;
  m_fFlags = (m_fFlags & (FL_CLIENT | FL_FAKECLIENT)) | FL_ONGROUND;
  m_MoveType = MOVETYPE_WALK; m_iObserverMode = OBS_MODE_NONE; m_hObserverTarget = nullptr;
  m_bDucked = m_bDucking = m_bInDuckJump = false; m_flDuckAmount = 0; m_vecViewOffset = { 0, 0, 64 };
  m_vecMins = { -16, -16, 0 }; m_vecMaxs = { 16, 16, 72 };
  m_aimPunchAngle.Init(); m_aimPunchAngleVel.Init(); m_viewPunchAngle.Init();
  m_flVelocityModifier = 1.f; m_flFallVelocity = 0; m_flStamina = 0;
  m_flFlashDuration = 0; m_flFlashStartTime = -100; m_flFlashMaxAlpha = 0;
  m_iShotsFired = 0; m_bIsScoped = false; m_bIsDefusing = false; m_bIsPlanting = false; m_bResumeZoom = false;
  m_iNumRoundKills = m_iNumRoundKillsHeadshots = 0;
  m_flSpawnTime = gpGlobals->curtime; m_bHasMovedSinceSpawn = false;
  m_flNextAttack = gpGlobals->curtime;
  m_nTickBase = gpGlobals->tickcount;
  m_iProgressBarDuration = 0;
  m_hLastAttacker = nullptr; std::fill(std::begin(m_iDamageDealt), std::end(m_iDamageDealt), 0);
  m_bKilledByHeadshot = false; m_flDeathTime = 0;
  m_PlayerAnimState.Reset(yaw);
  m_ClientAnimState.Reset(yaw);
  m_flClientFootYaw = yaw; m_flLowerBodyYawTarget = yaw;
  m_LagRecords.clear(); m_pActiveHitboxes = nullptr;
  m_nChokedCommands = 0; m_bShotThisChoke = false; m_iMissedShots = 0;
  m_nTickbaseShift = 0;
  // inventory: everyone keeps a knife + team pistol
  if (!Weapon_OwnsThisType(WEAPON_KNIFE)) GiveWeapon(WEAPON_KNIFE);
  if (!Weapon_GetSlot(WEAPON_SLOT_PISTOL)) GiveWeapon(Loadout_StartPistol(m_iTeamNum, m_bIsBot));
  // refill ammo
  for (auto* w : m_hMyWeapons) if (w && w->IsGun()) { w->m_iClip1 = w->GetCSWpnData().clipSize; w->m_iPrimaryReserveAmmoCount = w->GetCSWpnData().reserveAmmo; w->m_bInReload = false; w->ResetZoom(); }
  C_WeaponCSBase* best = Weapon_GetSlot(WEAPON_SLOT_RIFLE); if (!best) best = Weapon_GetSlot(WEAPON_SLOT_PISTOL); if (!best) best = Weapon_OwnsThisType(WEAPON_KNIFE);
  m_hActiveWeapon = nullptr; if (best) Weapon_Switch(best);
  CNetPlayerState s; BuildNetSnapshot(s, nullptr); m_net = m_netPrev = s;
  m_LagRecords.push_front(s);
  CSGameRules()->PlayerSpawn(this);
  IGameEvent ev("player_spawn"); ev.SetInt("userid", entindex()); ev.SetInt("teamnum", m_iTeamNum); gameeventmanager->FireEvent(ev);
}

void CCSPlayer::RoundRespawn() { Spawn(); }

float CCSPlayer::GetPlayerMaxSpeed() const {
  float speed = CS_PLAYER_SPEED_RUN;
  if (C_WeaponCSBase* w = GetActiveCSWeapon()) speed = w->GetMaxSpeed();
  if (CSGameRules()->IsFreezePeriod() && !m_bCanMoveDuringFreezePeriod) return 1.f;
  if (m_bIsDefusing || m_bIsPlanting) return 1.f;
  return speed * m_flVelocityModifier;
}

Vector CCSPlayer::GetNetEyePosition() const { return m_net.headPos.IsZero() ? m_net.origin + Vector(0, 0, 64 - 18 * m_net.duckAmount) : m_net.origin + Vector(0, 0, 64 - 18 * m_net.duckAmount); }

// ------------------------------------------------------------------------------------------------
// usercmd processing (server side; also runs for the local player - we are the listen server)
static void DecayAimPunch(CCSPlayer* p, float ft) {
  Vector punch(p->m_aimPunchAngle.x, p->m_aimPunchAngle.y, p->m_aimPunchAngle.z);
  float len = punch.NormalizeInPlace();
  len *= std::exp(-weapon_recoil_decay2_exp.GetFloat() * ft);
  len -= weapon_recoil_decay2_lin.GetFloat() * ft;
  if (len < 0) len = 0;
  punch *= len;
  Vector vel(p->m_aimPunchAngleVel.x, p->m_aimPunchAngleVel.y, p->m_aimPunchAngleVel.z);
  punch += vel * (ft * 0.5f);
  vel *= std::exp(ft * -weapon_recoil_vel_decay.GetFloat());
  punch += vel * (ft * 0.5f);
  p->m_aimPunchAngle = QAngle(punch.x, punch.y, punch.z);
  p->m_aimPunchAngleVel = QAngle(vel.x, vel.y, vel.z);
  // view punch: critically damped spring back to zero
  float k = std::exp(-10.f * ft);
  p->m_viewPunchAngle.x *= k; p->m_viewPunchAngle.y *= k; p->m_viewPunchAngle.z *= k;
}

void CCSPlayer::PreThink() {
  // zones
  m_bInBuyZone = mp_buy_anywhere.GetBool() || g_MapInfo.InZone(m_vecOrigin, m_iTeamNum == TEAM_CT ? ZONE_BUY_CT : ZONE_BUY_T);
  m_bInBombZone = g_MapInfo.InZone(m_vecOrigin, ZONE_BOMBSITE_A) || g_MapInfo.InZone(m_vecOrigin, ZONE_BOMBSITE_B);
  // the CT buy zone doubles as the hostage rescue zone
  m_bInHostageRescueZone = m_iTeamNum == TEAM_CT && g_MapInfo.InZone(m_vecOrigin, ZONE_BUY_CT);
  if (CSGameRules()->IsFreezePeriod() && !m_bCanMoveDuringFreezePeriod) m_fFlags |= FL_FROZEN; else m_fFlags &= ~FL_FROZEN;
}

void CCSPlayer::PlayerRunCommand(CUserCmd& cmd, float ft) {
  m_pCurrentCommand = &cmd;
  m_vecOldOrigin = m_vecOrigin;
  // server clamps angles (anti-untrusted)
  QAngle va = cmd.viewangles; va.x = Clamp(AngleNormalize(va.x), -89.f, 89.f); va.y = AngleNormalize(va.y); va.z = 0;
  cmd.viewangles = va;
  cmd.forwardmove = Clamp(cmd.forwardmove, -450.f, 450.f); cmd.sidemove = Clamp(cmd.sidemove, -450.f, 450.f);
  v_angle = va; m_angEyeAngles = va;
  m_afButtonLast = m_nButtons;
  m_nButtons = cmd.buttons;
  int changed = m_afButtonLast ^ m_nButtons;
  m_afButtonPressed = changed & m_nButtons; m_afButtonReleased = changed & ~m_nButtons;

  if (!IsAlive()) {
    if (m_iObserverMode == OBS_MODE_ROAMING) { // free-fly spectator camera
      CMoveData mv; mv.m_vecViewAngles = va; mv.m_nButtons = cmd.buttons;
      mv.m_flForwardMove = cmd.forwardmove; mv.m_flSideMove = cmd.sidemove; mv.m_flUpMove = cmd.upmove;
      mv.m_flMaxSpeed = mv.m_flClientMaxSpeed = 520.f;
      MoveType_t old = m_MoveType; m_MoveType = MOVETYPE_NOCLIP;
      g_GameMovement.ProcessMovement(this, &mv, ft);
      m_MoveType = old;
    }
    m_nTickBase++; m_LastCmd = cmd; m_pCurrentCommand = nullptr;
    return;
  }
  PreThink();
  if (cmd.weaponselect > 0) {
    if (cmd.weaponselect >= 1 && cmd.weaponselect <= 5) SelectSlot(cmd.weaponselect - 1);
    else if (cmd.weaponselect >= 100) { if (auto* w = Weapon_OwnsThisType(cmd.weaponselect - 100)) Weapon_Switch(w); }
  }
  // movement
  m_bIsWalking = (cmd.buttons & IN_SPEED) != 0;
  CMoveData mv;
  mv.m_vecViewAngles = va; mv.m_nButtons = cmd.buttons; mv.m_nOldButtons = m_LastCmd.buttons & IN_JUMP ? IN_JUMP : 0;
  if (!(m_afButtonLast & IN_JUMP)) mv.m_nOldButtons &= ~IN_JUMP;
  mv.m_flForwardMove = cmd.forwardmove; mv.m_flSideMove = cmd.sidemove; mv.m_flUpMove = cmd.upmove;
  if (cmd.bFakeDuck && (m_fFlags & FL_ONGROUND)) mv.m_nButtons &= ~IN_DUCK; // real hull stays standing
  float maxspeed = GetPlayerMaxSpeed();
  if (m_fFlags & FL_ONGROUND) {
    if (mv.m_nButtons & IN_SPEED) maxspeed *= CS_PLAYER_SPEED_WALK_MODIFIER;
    maxspeed *= 1.f - (1.f - CS_PLAYER_SPEED_DUCK_MODIFIER) * m_flDuckAmount;
  }
  m_flMaxspeed = maxspeed;
  mv.m_flMaxSpeed = mv.m_flClientMaxSpeed = maxspeed;
  if (m_fFlags & FL_FROZEN) { mv.m_flForwardMove = mv.m_flSideMove = 0; mv.m_nButtons &= ~IN_JUMP; }
  bool wasOnGround = (m_fFlags & FL_ONGROUND) != 0;
  g_GameMovement.ProcessMovement(this, &mv, ft);
  if (!wasOnGround && (m_fFlags & FL_ONGROUND)) { /* landed */ }
  if (wasOnGround && !(m_fFlags & FL_ONGROUND) && m_vecVelocity.z > 200) {
    IGameEvent ev("player_jump"); ev.SetInt("userid", entindex()); gameeventmanager->FireEvent(ev);
  }
  if (m_vecVelocity.Length2D() > 1.f) m_bHasMovedSinceSpawn = true;

  // weapons (ItemPostFrame)
  if (C_WeaponCSBase* w = GetActiveCSWeapon()) {
    if (gpGlobals->curtime >= m_flNextAttack || !(cmd.buttons & (IN_ATTACK | IN_ATTACK2))) w->ItemPostFrame();
    else w->ItemPostFrame();
  }
  PostThink(cmd, ft);
  m_nTickBase++;
  m_LastCmd = cmd;
  m_pCurrentCommand = nullptr;
}

void Game_OnPlayerLanded(CCSPlayer* p, float vol) {
  EmitSound(vol >= 1.f ? "Player.FallDamage" : "Player.Land", p->m_vecOrigin, p->entindex(), vol);
}

void CCSPlayer::PostThink(const CUserCmd& cmd, float ft) {
  DecayAimPunch(this, ft);
  m_flVelocityModifier = std::min(1.f, m_flVelocityModifier + ft * 0.4f);
  // footsteps (silent when walking or ducking, like CS)
  float spd = m_vecVelocity.Length2D();
  if ((m_fFlags & FL_ONGROUND) && spd > 135.f && !(cmd.buttons & IN_SPEED) && !m_bDucked && gpGlobals->curtime >= m_flNextFootstep) {
    m_flNextFootstep = gpGlobals->curtime + Clamp(100.f / spd * 1.1f, 0.25f, 0.45f);
    const char* snd = "Player.StepConcrete";
    switch (m_chTextureType) { case CHAR_TEX_SAND: case CHAR_TEX_DIRT: snd = "Player.StepSand"; break; case CHAR_TEX_METAL: case CHAR_TEX_GRATE: case CHAR_TEX_VENT: snd = "Player.StepMetal"; break; case CHAR_TEX_WOOD: snd = "Player.StepWood"; break; case CHAR_TEX_TILE: snd = "Player.StepTile"; break; default: break; }
    EmitSound(snd, m_vecOrigin, entindex(), 0.7f);
    IGameEvent ev("player_footstep"); ev.SetInt("userid", entindex()); gameeventmanager->FireEvent(ev);
  }
  // defusing (use key on the planted bomb)
  if (g_pPlantedC4 && m_iTeamNum == TEAM_CT && !g_pPlantedC4->m_bBombDefused && !g_pPlantedC4->m_bExploded) {
    Vector eye = EyePosition();
    bool near = eye.DistTo(g_pPlantedC4->m_vecOrigin) < 72.f;
    bool wantUse = (cmd.buttons & IN_USE) && near && (m_fFlags & FL_ONGROUND);
    if (wantUse && !m_bIsDefusing && !g_pPlantedC4->m_hBombDefuser) g_pPlantedC4->StartDefuse(this);
    else if (!wantUse && m_bIsDefusing && g_pPlantedC4->m_hBombDefuser == this) g_pPlantedC4->AbortDefuse();
  } else m_bIsDefusing = false;

  // hostage rescue: the use key picks up / drops a hostage, carrying it to the rescue zone completes it
  if (m_iTeamNum == TEAM_CT && IsAlive() && (m_fFlags & FL_ONGROUND)) {
    if (m_hCarriedHostage) {
      if (m_bInHostageRescueZone) {
        CHostage* h = m_hCarriedHostage;
        h->m_bRescued = true;
        m_iAccount = std::min(mp_maxmoney.GetInt(), m_iAccount + 1000);
        m_iScore += 2;
        g_GameRules.m_iHostagesRescued++;
        ConColorMsg({ 120, 230, 150, 255 }, "Hostage rescued! (%d/%d)\n", g_GameRules.m_iHostagesRescued, g_GameRules.m_iHostagesTotal);
        EmitSound2D("Radio.HostageRescued");
        IGameEvent ev("hostage_rescued"); ev.SetInt("userid", entindex()); gameeventmanager->FireEvent(ev);
        h->Remove();
        m_hCarriedHostage = nullptr;
        m_bIsGrabbingHostage = false;
      } else if (m_afButtonPressed & IN_USE) {
        m_hCarriedHostage->m_bCarried = false;
        m_hCarriedHostage->m_hCarrier = nullptr;
        m_hCarriedHostage = nullptr;
        m_bIsGrabbingHostage = false;
      }
    } else if (m_afButtonPressed & IN_USE) {
      CHostage* h = CHostage::FindNearest(this, 72.f);
      if (h) {
        h->m_bCarried = true; h->m_hCarrier = this;
        m_hCarriedHostage = h;
        m_bIsGrabbingHostage = true;
        ConColorMsg({ 120, 200, 255, 255 }, "Picked up a hostage - carry it to the rescue zone.\n");
        EmitSound2D("Radio.HostagePickup");
      }
    }
  }

  // doors: use key opens func_door_rotating nearby (placeholder - full implementation needs server-side door logic)
  if (cmd.buttons & IN_USE) {
    Vector eye = EyePosition();
    Vector forward; AngleVectors(v_angle, &forward);
    Vector traceEnd = eye + forward * 96.f;
    trace_t tr; UTIL_TraceLine(eye, traceEnd, MASK_SOLID, this, &tr);
    if (tr.m_pEnt && tr.fraction < 1.f) {
      const char* cls = tr.m_pEnt->GetClassname();
      if (cls && (!strcmp(cls, "func_door_rotating") || !strcmp(cls, "func_door"))) {
        // TODO: implement door opening (needs server-side door entity with Use method)
      }
    }
  }

  // animation state (server): every processed command
  float animDuck = cmd.bFakeDuck && (m_fFlags & FL_ONGROUND) ? (cmd.bSendPacket ? 1.f : 0.f) : m_flDuckAmount;
  m_PlayerAnimState.Update(v_angle, m_vecVelocity, (m_fFlags & FL_ONGROUND) != 0, animDuck, std::max(1.f, GetPlayerMaxSpeed() / std::max(0.01f, m_flVelocityModifier)), ft, gpGlobals->curtime);
  m_flLowerBodyYawTarget = m_PlayerAnimState.m_flLowerBodyYawTarget;

  // networking: choked commands are not sent to the other clients
  m_nChokedCommands++;
  int maxChoke = std::max(0, sv_maxusrcmdprocessticks.GetInt() - 2);
  bool send = cmd.bSendPacket || m_nChokedCommands > maxChoke;
  if (send) { UpdateNetworkState(cmd); m_nChokedCommands = 0; }
}

void CCSPlayer::BuildNetSnapshot(CNetPlayerState& s, const CUserCmd* cmd) {
  s.origin = m_vecOrigin; s.velocity = m_vecVelocity;
  s.eyeAngles = v_angle;
  s.footYaw = m_PlayerAnimState.m_flFootYaw;
  s.duckAmount = (cmd && cmd->bFakeDuck && (m_fFlags & FL_ONGROUND)) ? 1.f : m_flDuckAmount;
  s.moveCycle = m_PlayerAnimState.m_flMoveCycle;
  s.moveSpeed = m_PlayerAnimState.m_flSpeedAsPortionOfRunTopSpeed;
  s.moveYaw = m_PlayerAnimState.m_flMoveYaw;
  s.landAmount = m_PlayerAnimState.m_flLandAmount;
  s.onGround = (m_fFlags & FL_ONGROUND) != 0;
  s.flags = m_fFlags;
  C_WeaponCSBase* w = GetActiveCSWeapon();
  s.weaponId = w ? w->GetCSWeaponID() : 0;
  s.fireAmount = Clamp(1.f - (gpGlobals->curtime - m_flLastFireTime) / 0.15f, 0.f, 1.f);
  s.reloadCycle = (w && w->m_bInReload && w->m_flReloadFinishTime > w->m_flReloadStartTime) ? Clamp((gpGlobals->curtime - w->m_flReloadStartTime) / (w->m_flReloadFinishTime - w->m_flReloadStartTime), 0.f, 1.f) : -1.f;
  s.simTime = ServerTime();
  s.tick = gpGlobals->tickcount;
  s.lby = m_PlayerAnimState.m_flLowerBodyYawTarget;
  s.maxDesync = m_PlayerAnimState.GetMaxDesync();
  s.lbyUpdated = m_PlayerAnimState.m_bLbyUpdated;
  s.shot = m_bShotThisChoke;
  s.chokedTicks = m_nChokedCommands;
  s.valid = IsAlive();
  if (IsAlive()) {
    {
      PlayerPoseInput in;
      in.origin = s.origin; in.footYaw = s.footYaw; in.eyeAngles = s.eyeAngles; in.duckAmount = s.duckAmount;
      in.moveCycle = s.moveCycle; in.moveSpeed = s.moveSpeed; in.moveYaw = s.moveYaw; in.onGround = s.onGround; in.landAmount = s.landAmount;
      in.weaponId = s.weaponId; in.fireAmount = s.fireAmount; in.reloadCycle = s.reloadCycle;
      in.planting = m_bIsPlanting; in.defusing = m_bIsDefusing;
      PlayerPoseOutput out;
      BuildPlayerPose(this, in, out, true);
      s.hitboxes = out.hitboxes;
      s.headPos = (out.hitboxes[HITBOX_HEAD].a + out.hitboxes[HITBOX_HEAD].b) * 0.5f;
    }
  }
}

void CCSPlayer::UpdateNetworkState(const CUserCmd& cmd) {
  float prevSim = m_net.simTime;
  m_netPrev = m_net;
  BuildNetSnapshot(m_net, &cmd);
  m_bShotThisChoke = false;
  m_flSimulationTime = m_net.simTime; m_flOldSimulationTime = prevSim;
  m_angLastSentAngles = cmd.viewangles;
  m_flLastSentFootYaw = m_net.footYaw;
  m_flNetUpdateRealTime = gpGlobals->realtime;
  if (m_net.valid) {
    m_LagRecords.push_front(m_net);
    while (m_LagRecords.size() > 72) m_LagRecords.pop_back();
  } else m_LagRecords.clear();
  // what other clients see: they re-run the animstate with only the received data
  float dt = std::max(gpGlobals->interval_per_tick, m_net.simTime - prevSim);
  if (dt > 1.f) { m_ClientAnimState.Reset(m_net.eyeAngles.y); dt = gpGlobals->interval_per_tick; }
  m_ClientAnimState.Update(m_net.eyeAngles, m_net.velocity, m_net.onGround, m_net.duckAmount, std::max(1.f, GetPlayerMaxSpeed()), dt, m_net.simTime);
  m_flClientFootYaw = m_ClientAnimState.m_flFootYaw;
  m_flFakeFootYaw = m_flClientFootYaw;
  m_bClientPoseValid = false;
}

// ------------------------------------------------------------------------------------------------
bool CCSPlayer::IsSolidForTrace(unsigned mask) const {
  if (!IsAlive() || m_iObserverMode != OBS_MODE_NONE) return false;
  return (mask & CONTENTS_MONSTER) != 0;
}
bool CCSPlayer::TestHitboxes(const Ray_t& ray, unsigned mask, trace_t& tr) {
  (void)mask;
  if (!IsAlive()) return false;
  const HitboxSet& hb = GetActiveHitboxes();
  float t;
  int h = IntersectHitboxes(hb, ray.m_Start, ray.m_Delta, t);
  if (h < 0 || t >= tr.fraction) return false;
  tr.fraction = t; tr.endpos = ray.m_Start + ray.m_Delta * t;
  tr.m_pEnt = this; tr.hitbox = h; tr.hitgroup = HitboxToHitgroup(h);
  tr.contents = CONTENTS_HITBOX; tr.surface.surfaceProps = SURFACE_FLESH; tr.surface.flags = SURF_HITBOX; tr.surface.name = "flesh";
  Vector n = -ray.m_Delta; n.NormalizeInPlace(); tr.plane.normal = n;
  tr.startsolid = false; tr.allsolid = false;
  return true;
}

// ------------------------------------------------------------------------------------------------
int CCSPlayer::OnTakeDamage(const CTakeDamageInfo& inputInfo) {
  if (!IsAlive() || (m_fFlags & FL_GODMODE) || god.GetBool()) return 0;
  CTakeDamageInfo info = inputInfo;
  CCSPlayer* attacker = ToCSPlayer(info.m_hAttacker);
  if (attacker && attacker != this && !CSGameRules()->FPlayerCanTakeDamage(this, attacker)) return 0;
  if (CSGameRules()->IsRoundOver() && false) return 0;
  float flDamage = info.m_flDamage;
  int hitgroup = info.m_iHitGroup;
  bool armored = false, helmetHit = false;
  // armor (CS:GO): legs never protected, head only with helmet
  if (m_ArmorValue > 0 && (info.m_bitsDamageType & (DMG_BULLET | DMG_BLAST | DMG_SLASH))) {
    bool protect = true;
    if (hitgroup == HITGROUP_LEFTLEG || hitgroup == HITGROUP_RIGHTLEG) protect = false;
    if (hitgroup == HITGROUP_HEAD && !m_bHasHelmet) protect = false;
    if (info.m_bitsDamageType & DMG_BLAST) protect = true;
    if (protect) {
      const CCSWeaponInfo* wi = GetWeaponInfo(info.m_iWeaponID);
      float ratio = (wi && (info.m_bitsDamageType & (DMG_BULLET | DMG_SLASH))) ? wi->armorRatio : 0.5f;
      if (info.m_bitsDamageType & DMG_BLAST) ratio = 0.5f;
      const float bonus = 0.5f;
      float toHealth = flDamage * ratio;
      float toArmor = (flDamage - toHealth) * bonus;
      if (toArmor > m_ArmorValue) { toArmor = (float)m_ArmorValue; toHealth = flDamage - toArmor / bonus; }
      m_ArmorValue -= (int)toArmor; if (m_ArmorValue <= 0) { m_ArmorValue = 0; m_bHasHelmet = false; }
      flDamage = toHealth; armored = true; helmetHit = hitgroup == HITGROUP_HEAD;
    }
  }
  int dmgInt = std::max(1, (int)flDamage);
  int oldHealth = m_iHealth;
  m_iHealth -= dmgInt;
  if (m_iHealth < 0) m_iHealth = 0;
  m_iLastDamageAmount = dmgInt; m_LastHitGroup = hitgroup;
  m_flLastHurtTime = gpGlobals->curtime; m_flTimeOfLastInjury = gpGlobals->curtime;
  if (attacker) { m_hLastAttacker = attacker; if (attacker != this && attacker->entindex() < 33) attacker->m_iDamageDealt[entindex()] += std::min(oldHealth, dmgInt); }
  // tagging (bullet hits slow you down on the ground)
  if ((info.m_bitsDamageType & DMG_BULLET) && (m_fFlags & FL_ONGROUND)) {
    const CCSWeaponInfo* wi = GetWeaponInfo(info.m_iWeaponID);
    float mod = wi ? wi->flinchVelocityModifierLarge : 0.5f;
    if (dmgInt < 30) mod = std::max(mod, 0.65f);
    m_flVelocityModifier = std::min(m_flVelocityModifier, mod);
  }
  if (info.m_bitsDamageType & DMG_BULLET) {
    m_aimPunchAngle.x -= helmetHit ? 1.5f : (hitgroup == HITGROUP_HEAD ? 3.f : 1.f);
    m_viewPunchAngle.x -= hitgroup == HITGROUP_HEAD ? 4.f : 1.5f;
  }
  const char* snd = "Player.DamageBody";
  if (hitgroup == HITGROUP_HEAD) snd = helmetHit ? "Player.DamageHelmet" : "Player.DamageHeadShot";
  else if (armored) snd = "Player.DamageKevlar";
  if (info.m_bitsDamageType & DMG_FALL) snd = "Player.FallDamage";
  EmitSound(snd, m_vecOrigin + Vector(0, 0, 40), entindex());
  IGameEvent ev("player_hurt");
  ev.SetInt("userid", entindex()).SetInt("attacker", attacker ? attacker->entindex() : 0).SetInt("health", m_iHealth).SetInt("armor", m_ArmorValue)
    .SetInt("dmg_health", std::min(oldHealth, dmgInt)).SetInt("dmg_armor", 0).SetInt("hitgroup", hitgroup).SetInt("hitbox", info.m_iHitbox)
    .SetString("weapon", WeaponIdToAlias(info.m_iWeaponID)).SetFloat("x", info.m_vecDamagePosition.x).SetFloat("y", info.m_vecDamagePosition.y).SetFloat("z", info.m_vecDamagePosition.z)
    .SetInt("type", info.m_bitsDamageType);
  gameeventmanager->FireEvent(ev);
  if (m_iHealth <= 0) Event_Killed(info);
  return dmgInt;
}

void CCSPlayer::Event_Killed(const CTakeDamageInfo& info) {
  if (!IsAlive()) return;
  m_lifeState = LIFE_DEAD; m_iHealth = 0;
  m_flDeathTime = gpGlobals->curtime;
  m_bKilledByHeadshot = info.m_iHitGroup == HITGROUP_HEAD;
  m_iDeathWeapon = info.m_iWeaponID;
  Vector force = info.m_vecDamageForce;
  if (force.Length2D() < 1.f && info.m_hAttacker) force = m_vecOrigin - info.m_hAttacker->m_vecOrigin;
  m_flDeathYaw = force.Length2D() > 0.1f ? RAD2DEGF(std::atan2(force.y, force.x)) : m_angEyeAngles.y + 180.f;
  m_iDeaths++;
  m_bIsDefusing = false; m_bIsPlanting = false; m_bIsScoped = false;
  if (g_pPlantedC4 && g_pPlantedC4->m_hBombDefuser == this) g_pPlantedC4->AbortDefuse();
  if (m_hCarriedHostage) CHostage::OnCarrierKilled(this);   // the hostage is dropped where the carrier fell
  m_hCarriedHostage = nullptr; m_bIsGrabbingHostage = false;
  // drop weapons: best gun + C4 (+ one grenade)
  C_WeaponCSBase* drop = Weapon_GetSlot(WEAPON_SLOT_RIFLE); if (!drop) drop = Weapon_GetSlot(WEAPON_SLOT_PISTOL);
  if (drop) Weapon_Drop(drop, false);
  if (HasC4()) DropC4();
  for (int id : { WEAPON_HEGRENADE, WEAPON_MOLOTOV, WEAPON_INCGRENADE, WEAPON_FLASHBANG, WEAPON_SMOKEGRENADE })   // the best grenade
    if (C_WeaponCSBase* g = Weapon_OwnsThisType(id)) { Weapon_Drop(g, false); break; }
  m_bHasDefuser = false;
  // the dead keep their remaining weapons? no: CS strips everything except what was dropped
  RemoveAllItems(false);
  m_iObserverMode = OBS_MODE_DEATHCAM;
  m_LagRecords.clear();
  m_net.valid = false;
  m_netPrev = m_net;
  // final snapshot for the ragdoll / corpse
  m_net.origin = m_vecOrigin; m_net.eyeAngles = v_angle; m_net.footYaw = m_PlayerAnimState.m_flFootYaw;
  CSGameRules()->PlayerKilled(this, info);
}

// ------------------------------------------------------------------------------------------------
// inventory
C_WeaponCSBase* CCSPlayer::Weapon_GetSlot(int slot) const {
  for (auto* w : m_hMyWeapons) if (w && w->GetCSWpnData().slot == slot) return w;
  return nullptr;
}
C_WeaponCSBase* CCSPlayer::Weapon_OwnsThisType(int id) const {
  for (auto* w : m_hMyWeapons) if (w && w->GetCSWeaponID() == id) return w;
  return nullptr;
}
bool CCSPlayer::HasC4() const { return Weapon_OwnsThisType(WEAPON_C4) != nullptr; }
int CCSPlayer::GetGrenadeCount() const {
  int n = 0; for (auto* w : m_hMyWeapons) if (w && w->GetWeaponType() == WEAPONTYPE_GRENADE) n += std::max(1, w->m_iClip1);
  return n;
}

C_WeaponCSBase* CCSPlayer::GiveNamedItem(const char* name) {
  const CCSWeaponInfo* wi = GetWeaponInfoByName(name);
  if (!wi) return nullptr;
  return GiveWeapon(wi->id);
}

C_WeaponCSBase* CCSPlayer::GiveWeapon(int id) {
  const CCSWeaponInfo* wi = GetWeaponInfo(id);
  if (!wi || id == WEAPON_NONE) return nullptr;
  if (IsGrenade(id)) {
    if (C_WeaponCSBase* have = Weapon_OwnsThisType(id)) {
      int maxCount = id == WEAPON_FLASHBANG ? 2 : 1;
      if (have->m_iClip1 < maxCount && GetGrenadeCount() < 4) { have->m_iClip1++; return have; }
      return nullptr;
    }
    if (GetGrenadeCount() >= 4) return nullptr;
    if (IsFireGrenade(id) && (Weapon_OwnsThisType(WEAPON_MOLOTOV) || Weapon_OwnsThisType(WEAPON_INCGRENADE))) return nullptr;   // one fire grenade
  } else if (wi->slot == WEAPON_SLOT_RIFLE || wi->slot == WEAPON_SLOT_PISTOL || wi->slot == WEAPON_SLOT_KNIFE || wi->slot == WEAPON_SLOT_C4) {
    if (C_WeaponCSBase* old = Weapon_GetSlot(wi->slot)) {
      if (old->GetCSWeaponID() == id && wi->slot != WEAPON_SLOT_C4) { old->m_iClip1 = wi->clipSize; old->m_iPrimaryReserveAmmoCount = wi->reserveAmmo; return old; }
      if (wi->slot == WEAPON_SLOT_KNIFE || wi->slot == WEAPON_SLOT_C4) return old;
      Weapon_Drop(old, true);
    }
  }
  C_WeaponCSBase* w = CreateWeapon(id);
  g_EntityList.Add(w);
  w->m_hOwner = this; w->m_hOwnerEntity = this; w->m_iState = WEAPON_IS_CARRIED_BY_PLAYER;
  w->m_iTeamNum = m_iTeamNum;
  w->m_vecOrigin = m_vecOrigin;
  for (auto*& slot : m_hMyWeapons) if (!slot) { slot = w; break; }
  EmitSound("Player.PickupWeapon", m_vecOrigin, entindex(), 0.6f);
  return w;
}

bool CCSPlayer::Weapon_Switch(C_WeaponCSBase* w) {
  if (!w || w == m_hActiveWeapon) return false;
  if (!w->CanDeploy()) return false;
  if (m_hActiveWeapon) { m_hActiveWeapon->Holster(); m_hLastWeapon = m_hActiveWeapon; m_hActiveWeapon->m_iState = WEAPON_IS_CARRIED_BY_PLAYER; }
  m_hActiveWeapon = w; w->m_iState = WEAPON_IS_ACTIVE;
  w->Deploy();
  m_bIsScoped = false; m_iFOV = 0;
  return true;
}

void CCSPlayer::Weapon_Drop(C_WeaponCSBase* w, bool thrown) {
  if (!w) return;
  bool wasActive = w == m_hActiveWeapon;
  if (wasActive) { w->Holster(); m_hActiveWeapon = nullptr; }
  for (auto*& slot : m_hMyWeapons) if (slot == w) slot = nullptr;
  if (m_hLastWeapon == w) m_hLastWeapon = nullptr;
  // grenades with count > 1: drop only one
  w->m_iState = WEAPON_NOT_CARRIED; w->m_hPrevOwner = this; w->m_hOwner = nullptr; w->m_hOwnerEntity = nullptr;
  w->m_flDroppedTime = gpGlobals->curtime; w->m_bOnGround = false; w->m_bInReload = false; w->ResetZoom();
  Vector fwd; AngleVectors(v_angle, &fwd);
  w->m_vecOrigin = EyePosition() + fwd * 8.f - Vector(0, 0, 10);
  if (!IsAlive()) w->m_vecOrigin = m_vecOrigin + Vector(0, 0, 36);
  w->m_vecVelocity = m_vecVelocity + (thrown ? fwd * 300.f + Vector(0, 0, 80) : Vector(RandomFloat(-40, 40), RandomFloat(-40, 40), 60));
  w->m_angRotation = QAngle(0, v_angle.y, 0);
  w->OnDropped();
  if (w->GetCSWeaponID() == WEAPON_C4) { CSGameRules()->m_bBombDropped = true; IGameEvent ev("bomb_dropped"); ev.SetInt("userid", entindex()); gameeventmanager->FireEvent(ev); }
  if (wasActive && IsAlive()) {
    C_WeaponCSBase* next = m_hLastWeapon && m_hLastWeapon != w ? m_hLastWeapon : nullptr;
    if (!next) next = Weapon_GetSlot(WEAPON_SLOT_RIFLE);
    if (!next) next = Weapon_GetSlot(WEAPON_SLOT_PISTOL);
    if (!next) next = Weapon_GetSlot(WEAPON_SLOT_KNIFE);
    if (next) Weapon_Switch(next);
  }
}

void CCSPlayer::DropC4() { if (C_WeaponCSBase* c4 = Weapon_OwnsThisType(WEAPON_C4)) Weapon_Drop(c4, IsAlive()); }

void CCSPlayer::SelectSlot(int slot) {
  if (slot == WEAPON_SLOT_GRENADES) {
    // cycle through grenades
    std::vector<C_WeaponCSBase*> nades;
    for (int id : { WEAPON_HEGRENADE, WEAPON_FLASHBANG, WEAPON_SMOKEGRENADE, WEAPON_MOLOTOV, WEAPON_INCGRENADE }) if (auto* g = Weapon_OwnsThisType(id)) nades.push_back(g);
    if (nades.empty()) return;
    size_t cur = 0; for (size_t i = 0; i < nades.size(); i++) if (nades[i] == m_hActiveWeapon) cur = i + 1;
    Weapon_Switch(nades[cur % nades.size()]);
    return;
  }
  if (C_WeaponCSBase* w = Weapon_GetSlot(slot)) Weapon_Switch(w);
}
void CCSPlayer::SelectLastItem() { if (m_hLastWeapon) Weapon_Switch(m_hLastWeapon); }
void CCSPlayer::SelectNextItem(int dir) {
  static const int order[] = { WEAPON_SLOT_RIFLE, WEAPON_SLOT_PISTOL, WEAPON_SLOT_KNIFE, WEAPON_SLOT_GRENADES, WEAPON_SLOT_C4 };
  std::vector<C_WeaponCSBase*> list;
  for (int s : order) for (auto* w : m_hMyWeapons) if (w && w->GetCSWpnData().slot == s) list.push_back(w);
  if (list.empty()) return;
  int cur = 0; for (size_t i = 0; i < list.size(); i++) if (list[i] == m_hActiveWeapon) cur = (int)i;
  int n = (int)list.size();
  Weapon_Switch(list[((cur + dir) % n + n) % n]);
}
void CCSPlayer::RemoveAllItems(bool removeSuit) {
  for (auto*& w : m_hMyWeapons) if (w) { w->Remove(); w = nullptr; }
  m_hActiveWeapon = nullptr; m_hLastWeapon = nullptr;
  if (removeSuit) { m_ArmorValue = 0; m_bHasHelmet = false; m_bHasDefuser = false; }
}
void CCSPlayer::ClearWeapons() { RemoveAllItems(false); }

bool CCSPlayer::CanPlayerBuy(bool display) const {
  if (!IsAlive()) return false;
  if (!m_bInBuyZone) { if (display) ConColorMsg({ 255, 180, 0, 255 }, "You are not in a buy zone.\n"); return false; }
  if (CSGameRules()->IsBuyTimeElapsed()) { if (display) ConColorMsg({ 255, 180, 0, 255 }, "%d seconds have passed. You can't buy anything now.\n", mp_buytime.GetInt()); return false; }
  return true;
}

bool CCSPlayer::BuyItem(const char* alias, bool silent) {
  if (!CanPlayerBuy(!silent)) return false;
  std::string a = alias;
  auto charge = [&](int price) -> bool {
    if (m_iAccount < price) { if (!silent) ConColorMsg({ 255, 120, 80, 255 }, "You have insufficient funds!\n"); EmitSound2D("Buy.Fail"); return false; }
    m_iAccount -= price; return true;
  };
  auto fire = [&](const char* what) { IGameEvent ev("item_purchase"); ev.SetInt("userid", entindex()).SetInt("team", m_iTeamNum).SetString("weapon", what); gameeventmanager->FireEvent(ev); if (!m_bIsBot) EmitSound2D("Buy.Item"); };
  if (a == "vest" || a == "kevlar") {
    if (m_ArmorValue >= 100) { if (!silent) Msg("You already have Kevlar.\n"); return false; }
    if (!charge(650)) return false; m_ArmorValue = 100; fire("item_kevlar"); return true;
  }
  if (a == "vesthelm" || a == "assaultsuit") {
    if (m_ArmorValue >= 100 && m_bHasHelmet) { if (!silent) Msg("You already have Kevlar and a helmet.\n"); return false; }
    int price = m_ArmorValue >= 100 ? 350 : 1000;
    if (!charge(price)) return false; m_ArmorValue = 100; m_bHasHelmet = true; fire("item_assaultsuit"); return true;
  }
  if (a == "defuser" || a == "defusekit") {
    if ((m_iTeamNum != TEAM_CT && !mp_buy_anyteam.GetBool()) || m_bHasDefuser) return false;
    if (!charge(400)) return false; m_bHasDefuser = true; fire("item_defuser"); return true;
  }
  if (a == "primammo" || a == "secammo") {
    C_WeaponCSBase* w = Weapon_GetSlot(a == "primammo" ? WEAPON_SLOT_RIFLE : WEAPON_SLOT_PISTOL);
    if (w) w->m_iPrimaryReserveAmmoCount = w->GetCSWpnData().reserveAmmo;
    return w != nullptr;
  }
  const CCSWeaponInfo* wi = GetWeaponInfoByName(a);
  if (!wi || wi->id == WEAPON_KNIFE || wi->id == WEAPON_C4) { if (!silent) Msg("Unknown item '%s'\n", alias); return false; }
  if (wi->team != 0 && wi->team != m_iTeamNum && !mp_buy_anyteam.GetBool()) { if (!silent) Msg("%s is not available for your team.\n", wi->printName); return false; }
  if (C_WeaponCSBase* have = Weapon_OwnsThisType(wi->id)) {
    if (!IsGrenade(wi->id)) { if (!silent) Msg("You already own that weapon.\n"); return false; }
    int maxCount = wi->id == WEAPON_FLASHBANG ? 2 : 1;
    if (have->m_iClip1 >= maxCount) { if (!silent) Msg("You cannot carry any more.\n"); return false; }
  }
  if (IsGrenade(wi->id) && GetGrenadeCount() >= 4) { if (!silent) Msg("You cannot carry any more grenades.\n"); return false; }
  if (IsFireGrenade(wi->id) && (Weapon_OwnsThisType(WEAPON_MOLOTOV) || Weapon_OwnsThisType(WEAPON_INCGRENADE))) { if (!silent) Msg("You cannot carry any more.\n"); return false; }
  if (!charge(wi->price)) return false;
  C_WeaponCSBase* w = GiveWeapon(wi->id);
  if (!w) { m_iAccount += wi->price; return false; }
  if (!IsGrenade(wi->id) || !m_hActiveWeapon || m_hActiveWeapon->GetWeaponType() == WEAPONTYPE_KNIFE) {
    if (wi->slot == WEAPON_SLOT_RIFLE || (wi->slot == WEAPON_SLOT_PISTOL && !Weapon_GetSlot(WEAPON_SLOT_RIFLE))) Weapon_Switch(w);
  }
  fire(wi->className);
  return true;
}

void CCSPlayer::BuyDefaultLoadout() {
  // used by bots / autobuy
  if (m_iAccount >= 3700) {
    if (m_iTeamNum == TEAM_TERRORIST) BuyItem("ak47", true); else BuyItem("m4a1", true);
  }
}

// ------------------------------------------------------------------------------------------------
void CCSPlayer::Blind(float holdTime, float fadeTime, float startingAlpha) {
  float now = gpGlobals->curtime;
  // stack with an existing flash like CS:GO does (take the longer one)
  float remaining = (m_flFlashStartTime + m_flFlashDuration) - now;
  if (remaining < holdTime + fadeTime || startingAlpha > GetFlashAlpha()) {
    m_flFlashStartTime = now; m_flFlashDuration = holdTime + fadeTime; m_flFlashMaxAlpha = startingAlpha;
    m_flFlashHoldTime = holdTime;
  }
  IGameEvent ev("player_blind"); ev.SetInt("userid", entindex()).SetFloat("blind_duration", holdTime + fadeTime); gameeventmanager->FireEvent(ev);
}
float CCSPlayer::GetFlashAlpha() const {
  float t = gpGlobals->curtime - m_flFlashStartTime;
  if (t < 0 || t > m_flFlashDuration || m_flFlashDuration <= 0) return 0;
  float hold = std::min(m_flFlashHoldTime, m_flFlashDuration);
  if (t < hold) return m_flFlashMaxAlpha;
  float fade = m_flFlashDuration - hold;
  return fade > 0 ? m_flFlashMaxAlpha * (1.f - (t - hold) / fade) : 0;
}
bool CCSPlayer::IsBlind() const { return GetFlashAlpha() > 180.f; }
void CCSPlayer::SetProgressBarTime(int seconds) { m_iProgressBarDuration = seconds; m_flProgressBarStartTime = gpGlobals->curtime; }
void CCSPlayer::KickBack(float up, float lateral) { m_aimPunchAngle.x -= up; m_aimPunchAngle.y += lateral; }

Vector CCSPlayer::GetRenderOrigin(float alpha) const { return VectorLerp(m_vecOldOrigin, m_vecOrigin, alpha); }
