// baseentity.hpp - C_BaseEntity, CTakeDamageInfo, CBaseEntityList
#pragma once
#include "../../engine/mathlib.hpp"
#include "../../engine/globals.hpp"
#include "../../engine/enginetrace.hpp"
#include "../../engine/view.hpp"
#include "../../engine/convar.hpp"
#include <string>
#include <vector>

class C_BaseEntity;
class CCSPlayer;
class C_WeaponCSBase;

enum DamageType { DMG_GENERIC = 0, DMG_CRUSH = 1, DMG_BULLET = 2, DMG_SLASH = 4, DMG_BURN = 8, DMG_FALL = 32, DMG_BLAST = 64, DMG_CLUB = 128, DMG_SHOCK = 256, DMG_HEADSHOT = 1 << 30 };

class CTakeDamageInfo {
public:
  C_BaseEntity* m_hInflictor = nullptr;
  C_BaseEntity* m_hAttacker = nullptr;
  int m_iWeaponID = 0;
  float m_flDamage = 0;
  float m_flMaxDamage = 0;
  Vector m_vecDamageForce;
  Vector m_vecDamagePosition;
  Vector m_vecReportedPosition;
  int m_bitsDamageType = DMG_GENERIC;
  int m_iHitGroup = 0;
  int m_iHitbox = -1;
  int m_iPenetrationCount = 0;
  bool m_bWallbang = false;
  bool m_bNoScope = false;
  bool m_bThroughSmoke = false;
  bool m_bAttackerBlind = false;
};

enum EntityClassType { ENTCLASS_BASE = 0, ENTCLASS_PLAYER, ENTCLASS_WEAPON, ENTCLASS_GRENADE_PROJECTILE, ENTCLASS_PLANTED_C4, ENTCLASS_RAGDOLL, ENTCLASS_HOSTAGE };
enum CollisionGroup_t { COLLISION_GROUP_NONE = 0, COLLISION_GROUP_DEBRIS, COLLISION_GROUP_WEAPON, COLLISION_GROUP_PLAYER, COLLISION_GROUP_PROJECTILE };

class C_BaseEntity : public IHandleEntity {
public:
  C_BaseEntity();
  virtual ~C_BaseEntity() = default;

  // --- networked / shared state (names follow the Source netvars) ---
  int entindex() const { return m_iIndex; }
  int m_iIndex = -1;
  std::string m_iClassname = "baseentity";
  int m_iEntClass = ENTCLASS_BASE;
  Vector m_vecOrigin;            // abs origin
  Vector m_vecOldOrigin;         // last tick (render interpolation)
  Vector m_vecVelocity;
  bool IsDormant() const { return m_bDormant; }   // not sent to the local client (out of its PVS) - sv_transmit.cpp
  Vector m_vecBaseVelocity;
  Vector m_vecAbsVelocity;
  QAngle m_angRotation;
  QAngle m_angOldRotation;
  Vector m_vecMins{ -4, -4, -4 }, m_vecMaxs{ 4, 4, 4 };
  int m_fFlags = 0;
  int m_fEFlags = 0;
  MoveType_t m_MoveType = MOVETYPE_NONE;
  int m_CollisionGroup = COLLISION_GROUP_NONE;
  int m_iTeamNum = TEAM_UNASSIGNED;
  int m_iHealth = 100;
  int m_iMaxHealth = 100;
  int m_lifeState = LIFE_ALIVE;
  int m_takedamage = 0;
  float m_flGravity = 1.f;
  float m_flFriction = 1.f;
  float m_flElasticity = 0.45f;
  float m_flSimulationTime = 0;
  float m_flOldSimulationTime = 0;
  float m_flAnimTime = 0;
  float m_flCreateTime = 0;
  float m_flNextThink = -1;
  bool m_bDormant = false;
  bool m_bMarkedForDeletion = false;
  bool m_bSpotted = false;
  int m_nRenderMode = 0;
  Color32 m_clrRender;
  C_BaseEntity* m_hOwnerEntity = nullptr;
  C_BaseEntity* m_hGroundEntity = nullptr;

  // --- virtual interface ---
  virtual void Spawn() {}
  virtual void Precache() {}
  virtual void Think() {}
  virtual void PhysicsSimulate() {}           // per tick
  virtual void DrawModel(const CViewSetup&) {} // client render
  virtual void DrawTranslucent(const CViewSetup&) {}
  virtual int OnTakeDamage(const CTakeDamageInfo&) { return 0; }
  virtual void Event_Killed(const CTakeDamageInfo&) {}
  virtual bool IsPlayer() const { return false; }
  virtual bool IsWeapon() const { return false; }
  virtual bool IsHostage() const { return false; }
  virtual bool IsAlive() const { return m_lifeState == LIFE_ALIVE; }
  virtual Vector EyePosition() const { return m_vecOrigin; }
  virtual Vector WorldSpaceCenter() const { return m_vecOrigin + (m_vecMins + m_vecMaxs) * 0.5f; }
  virtual const char* GetClassname() const { return m_iClassname.c_str(); }

  // IHandleEntity
  bool IsSolidForTrace(unsigned mask) const override;
  void GetCollisionBounds(Vector& mn, Vector& mx) const override { mn = m_vecOrigin + m_vecMins; mx = m_vecOrigin + m_vecMaxs; }
  C_BaseEntity* GetBaseEntity() override { return this; }

  // helpers
  const Vector& GetAbsOrigin() const { return m_vecOrigin; }
  void SetAbsOrigin(const Vector& v) { m_vecOrigin = v; }
  const QAngle& GetAbsAngles() const { return m_angRotation; }
  int GetTeamNumber() const { return m_iTeamNum; }
  int GetHealth() const { return m_iHealth; }
  int GetFlags() const { return m_fFlags; }
  void Remove() { m_bMarkedForDeletion = true; }
  Vector GetRenderOrigin(float alpha) const { return VectorLerp(m_vecOldOrigin, m_vecOrigin, alpha); }
};

// --------------------------------------------------------------------------------------------
class CBaseEntityList {
public:
  static constexpr int MAX_PLAYERS = 32;
  void Clear();
  int Add(C_BaseEntity* e, int forceIndex = -1);
  C_BaseEntity* Get(int idx) const { return (idx >= 0 && idx < (int)m_ents.size()) ? m_ents[idx] : nullptr; }
  CCSPlayer* GetPlayer(int idx) const;
  int GetHighestEntityIndex() const { return (int)m_ents.size() - 1; }
  void CleanupDeleted();
  template <class F> void ForEach(F&& f) { for (auto* e : m_ents) if (e && !e->m_bMarkedForDeletion) f(e); }
  std::vector<C_BaseEntity*>& All() { return m_ents; }
  std::vector<IHandleEntity*> traceList; // rebuilt every tick for enginetrace
  void RebuildTraceList();
private:
  std::vector<C_BaseEntity*> m_ents;
};
inline CBaseEntityList g_EntityList;
inline CBaseEntityList* entitylist = &g_EntityList;

// UTIL ------------------------------------------------------------------------------------------
CCSPlayer* UTIL_PlayerByIndex(int idx);
std::vector<CCSPlayer*> UTIL_GetPlayers(bool aliveOnly = false);
void UTIL_TraceLine(const Vector& a, const Vector& b, unsigned mask, const IHandleEntity* ignore, trace_t* tr);
void UTIL_TraceHull(const Vector& a, const Vector& b, const Vector& mn, const Vector& mx, unsigned mask, const IHandleEntity* ignore, trace_t* tr);
