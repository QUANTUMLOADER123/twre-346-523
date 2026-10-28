// cs_player.hpp - C_BaseAnimating -> C_BaseCombatCharacter -> C_BasePlayer -> CCSPlayer
#pragma once
#include "baseentity.hpp"
#include "cs_playeranimstate.hpp"
#include "weapon_parse.hpp"
#include <deque>
#include <array>

class C_WeaponCSBase;
class CCSBot;

// --------------------------------------------------------------------------------------------
class C_BaseAnimating : public C_BaseEntity {
public:
  CStudioModel* m_pStudioHdr = nullptr; // model
  int m_nSkin = 0, m_nBody = 0;
  int m_nSequence = 0;
  float m_flCycle = 0;
  float m_flPlaybackRate = 1;
  float m_flModelScale = 1;
  CStudioPose m_CachedBoneData;
  matrix3x4_t m_rgflCoordinateFrame;
  int m_iMostRecentModelBoneCounter = 0;
  bool m_bClientSideAnimation = true;
  bool m_bClientSideRagdoll = false;
};

class C_BaseCombatCharacter : public C_BaseAnimating {
public:
  static constexpr int MAX_WEAPONS = 48;
  C_WeaponCSBase* m_hMyWeapons[MAX_WEAPONS] = {};
  C_WeaponCSBase* m_hActiveWeapon = nullptr;
  C_WeaponCSBase* m_hLastWeapon = nullptr;
  float m_flNextAttack = 0;
  int m_iAmmo[32] = {};
  int m_LastHitGroup = 0;
  float m_flTimeOfLastInjury = 0;
  bool m_bGlowEnabled = false;
};

// what other players / the server see (updated only when the player's client sends a packet)
struct CNetPlayerState {
  Vector origin, velocity;
  QAngle eyeAngles;
  float footYaw = 0, duckAmount = 0, moveCycle = 0, moveSpeed = 0, moveYaw = 0, landAmount = 0;
  bool onGround = true;
  int flags = 0;
  int weaponId = 0;
  float fireAmount = 0, reloadCycle = -1;
  float simTime = 0;
  int tick = 0;
  float lby = 0;
  float maxDesync = 58;
  bool lbyUpdated = false;
  bool shot = false;          // fired during the choke cycle (on-shot)
  int chokedTicks = 0;
  HitboxSet hitboxes;
  Vector headPos;
  bool valid = false;
};
using CLagRecord = CNetPlayerState;

class C_BasePlayer : public C_BaseCombatCharacter {
public:
  // local data
  QAngle v_angle;                   // view angles (pl.v_angle)
  QAngle m_angEyeAngles;            // networked eye angles
  Vector m_vecViewOffset{ 0, 0, 64 };
  QAngle m_viewPunchAngle;
  QAngle m_aimPunchAngle;
  QAngle m_aimPunchAngleVel;
  float m_flFallVelocity = 0;
  float m_flDuckAmount = 0;
  float m_flDuckSpeed = 8.f;
  bool m_bDucked = false, m_bDucking = false, m_bInDuckJump = false;
  float m_flDucktime = 0;
  float m_flStepSoundTime = 0;
  float m_flMaxspeed = 260;
  int m_nTickBase = 0;
  int m_nButtons = 0, m_afButtonLast = 0, m_afButtonPressed = 0, m_afButtonReleased = 0;
  int m_iFOV = 90, m_iDefaultFOV = 90, m_iFOVStart = 90;
  float m_flFOVTime = 0;
  int m_iObserverMode = OBS_MODE_NONE;
  C_BaseEntity* m_hObserverTarget = nullptr;
  float m_flDeathTime = 0;
  float m_flNextDecalTime = 0;
  bool m_bAllowAutoMovement = true;
  float m_surfaceFriction = 1.f;
  int m_chTextureType = 0;
  Vector m_vecLadderNormal;
  int m_iBonusProgress = 0;
  float m_flLaggedMovementValue = 1.f;
  CUserCmd m_LastCmd;
  CUserCmd* m_pCurrentCommand = nullptr;
  bool IsPlayer() const override { return true; }
};

// --------------------------------------------------------------------------------------------
class CCSPlayer : public C_BasePlayer {
public:
  CCSPlayer();
  ~CCSPlayer() override;

  // identity
  std::string m_szPlayerName = "Player";
  bool m_bIsBot = false;
  bool m_bIsLocalPlayer = false;
  CCSBot* m_pBot = nullptr;
  int m_iPlayerModel = 0;          // which character model
  int m_iClass = 0;

  // gameplay
  int m_iAccount = 800;            // money
  int m_ArmorValue = 0;
  bool m_bHasHelmet = false;
  bool m_bHasHeavyArmor = false;
  bool m_bHasDefuser = false;
  bool m_bIsDefusing = false;
  bool m_bIsGrabbingHostage = false;
  class CHostage* m_hCarriedHostage = nullptr;   // the hostage this player is carrying (use key)
  bool m_bIsScoped = false;
  bool m_bIsWalking = false;
  bool m_bResumeZoom = false;
  bool m_bInBuyZone = false;
  bool m_bInBombZone = false;
  bool m_bInHostageRescueZone = false;
  bool m_bIsPlanting = false;
  float m_flPlantStartTime = 0;
  bool m_bWaitForNoAttack = false;
  bool m_bGunGameImmunity = false;
  bool m_bHasMovedSinceSpawn = false;
  bool m_bMadeFinalGunGameProgressiveKill = false;
  bool m_bCanMoveDuringFreezePeriod = false;
  int m_iShotsFired = 0;
  int m_iNumRoundKills = 0, m_iNumRoundKillsHeadshots = 0;
  int m_iKills = 0, m_iDeaths = 0, m_iAssists = 0, m_iMVPs = 0, m_iScore = 0, m_iHeadshots = 0;
  int m_iPing = 5;
  float m_flVelocityModifier = 1.f;
  float m_flStamina = 0;
  float m_flFlashDuration = 0, m_flFlashMaxAlpha = 255, m_flFlashStartTime = -100, m_flFlashHoldTime = 0;
  float m_flProgressBarStartTime = 0; int m_iProgressBarDuration = 0;
  float m_flLowerBodyYawTarget = 0;
  float m_flThirdpersonRecoil = 0;
  float m_flLastFireTime = -100;
  float m_flLastHurtTime = -100;
  float m_flSpawnTime = 0;
  float m_flNextBuyTime = 0;
  float m_flNextFootstep = 0;
  int m_iLastDamageAmount = 0;
  C_BaseEntity* m_hLastAttacker = nullptr;
  int m_iDamageDealt[33] = {};     // per victim this round (assists)
  float m_flDeathYaw = 0;
  int m_iDeathWeapon = 0;
  bool m_bKilledByHeadshot = false;
  int m_iSpectatorTarget = -1;
  int m_iDefuseProgressPct = 0;
  float m_flDefuseStartTime = 0;

  // animation / desync
  CCSGOPlayerAnimState m_PlayerAnimState;
  PlayerPoseOutput m_RenderPose;   // last pose built for rendering
  float m_flLastPoseTime = -1;
  bool m_bRenderPoseValid = false;

  // network simulation (choke / fakelag / desync / lag compensation)
  CNetPlayerState m_net;           // current snapshot (what others see)
  CNetPlayerState m_netPrev;       // previous snapshot (render interpolation)
  float m_flNetUpdateRealTime = 0;
  std::deque<CLagRecord> m_LagRecords;
  const HitboxSet* m_pActiveHitboxes = nullptr; // set during lag compensation
  int m_nChokedCommands = 0;
  bool m_bShotThisChoke = false;
  QAngle m_angLastSentAngles;      // for the local "fake" visualisation
  float m_flLastSentFootYaw = 0;
  float m_flFakeFootYaw = 0;       // client side "fake" model yaw (what others would see)
  int m_nTickbaseShift = 0;
  float m_flDoubletapRecharge = 0;

  // client-side view of this player (other clients re-animate from received snapshots only,
  // so their feet yaw differs from the server's -> desync; resolvers try to guess m_net.footYaw)
  CCSGOPlayerAnimState m_ClientAnimState;
  float m_flClientFootYaw = 0;
  bool m_bResolverActive = false;
  float m_flResolverFootYaw = 0;    // set by the cheat resolver
  int m_iResolverSide = 0;          // -1 left, 0 none, 1 right (for display)
  int m_iMissedShots = 0;           // tracked by the local ragebot
  float m_flLastClientAnimTime = 0;
  PlayerPoseOutput m_ClientPose;    // last client pose (render + hitboxes visible to the local client)
  bool m_bClientPoseValid = false;
  float m_flSpottedTime = -100;     // radar
  int m_nWedgedTicks = 0;               // gamemovement: stuck in the air on a steep face
  float m_flTeamSpottedTime = -100; // really seen by a living teammate of the local player (not radar reveal)

  // dormancy (sv_transmit.cpp): the last state the local client received before the player went dormant
  float m_flLastTransmitTime = -100;
  Vector m_vecLastKnownOrigin; float m_flLastKnownTime = -100;
  int m_iLastKnownHealth = 100, m_iLastKnownWeapon = 0; float m_flLastKnownDuck = 0;
  Vector m_vecSoundOrigin; float m_flSoundTime = -100;   // heard while dormant (shots, footsteps, hits)

  // live render interpolation
  Vector m_vecRenderOrigin;

  // --- methods ---
  void Spawn() override;
  void RoundRespawn();
  void Reset(bool keepMoney);
  void PlayerRunCommand(CUserCmd& cmd, float frametime);
  void PreThink();
  void PostThink(const CUserCmd& cmd, float frametime);
  void UpdateNetworkState(const CUserCmd& cmd);   // called when a packet is "sent"
  void BuildNetSnapshot(CNetPlayerState& s, const CUserCmd* cmd);
  int OnTakeDamage(const CTakeDamageInfo& info) override;
  void Event_Killed(const CTakeDamageInfo& info) override;
  bool IsAlive() const override { return m_lifeState == LIFE_ALIVE; }
  Vector EyePosition() const override { return m_vecOrigin + m_vecViewOffset; }
  Vector GetNetEyePosition() const;
  float GetPlayerMaxSpeed() const;
  bool IsDucked() const { return (m_fFlags & FL_DUCKING) != 0; }
  bool IsBot() const { return m_bIsBot; }
  bool IsLocalPlayer() const { return m_bIsLocalPlayer; }
  bool IsEnemy(const CCSPlayer* o) const;
  const char* GetPlayerName() const { return m_szPlayerName.c_str(); }
  int GetObserverMode() const { return m_iObserverMode; }

  // weapons
  C_WeaponCSBase* GetActiveCSWeapon() const { return m_hActiveWeapon; }
  C_WeaponCSBase* GiveNamedItem(const char* name);
  C_WeaponCSBase* GiveWeapon(int weaponId);
  C_WeaponCSBase* Weapon_GetSlot(int slot) const;
  C_WeaponCSBase* Weapon_OwnsThisType(int id) const;
  bool Weapon_Switch(C_WeaponCSBase* w);
  void Weapon_Drop(C_WeaponCSBase* w, bool thrown = true);
  void SelectSlot(int slot);
  void SelectLastItem();
  void SelectNextItem(int dir);
  void RemoveAllItems(bool removeSuit);
  void DropC4();
  bool HasC4() const;
  int GetGrenadeCount() const;
  bool BuyItem(const char* alias, bool silent = false); // "ak47", "vest", "vesthelm", "defuser", "hegrenade"...
  bool CanPlayerBuy(bool display) const;
  void BuyDefaultLoadout();
  void ClearWeapons();

  // combat
  void FireBullet(const Vector& src, const QAngle& shootAngles, float distance, float penetration, int penetrationCount, int bulletType, int damage,
                  float rangeModifier, C_WeaponCSBase* weapon, int weaponId, bool isTracer, const CUserCmd* cmd);
  void FX_FireBullets(int weaponId, const Vector& origin, const QAngle& angles, int seed, float inaccuracy, float spread, const CUserCmd* cmd);
  void KickBack(float up, float lateral);
  void Blind(float holdTime, float fadeTime, float startingAlpha);
  bool IsBlind() const;
  float GetFlashAlpha() const;
  void SetProgressBarTime(int seconds);

  // hitboxes
  bool UsesHitboxesForRays() const override { return true; }
  bool TestHitboxes(const Ray_t& ray, unsigned mask, trace_t& tr) override;
  bool IsSolidForTrace(unsigned mask) const override;
  const HitboxSet& GetActiveHitboxes() const { return m_pActiveHitboxes ? *m_pActiveHitboxes : m_net.hitboxes; }
  void BuildRenderPose(float alpha, bool useNet);

  // rendering (client)
  void DrawModel(const CViewSetup& view) override;
  void DrawTranslucent(const CViewSetup& view) override;
  Vector GetRenderOrigin(float alpha) const;
};

// helpers
CCSPlayer* ToCSPlayer(C_BaseEntity* e);
float CS_SimulateBulletDamage(const CCSPlayer* shooter, const Vector& src, const Vector& end, int weaponId, const CCSPlayer* target, int hitgroup, int* outWalls = nullptr);
CCSPlayer* GetLocalPlayer();
inline CCSPlayer* g_pLocalPlayer = nullptr;
