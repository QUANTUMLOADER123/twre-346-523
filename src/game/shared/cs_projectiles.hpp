// cs_projectiles.hpp - CBaseCSGrenadeProjectile (HE / flashbang / smoke) and CPlantedC4
#pragma once
#include "baseentity.hpp"
#include "weapon_parse.hpp"

class CBaseCSGrenadeProjectile : public C_BaseEntity {
public:
  CBaseCSGrenadeProjectile(int weaponId, CCSPlayer* thrower, const Vector& pos, const Vector& vel);
  int m_iWeaponID = 0;
  CCSPlayer* m_hThrower = nullptr;
  float m_flDetonateTime = 0;
  float m_flSpawnTime = 0;
  int m_nBounces = 0;
  Vector m_vecInitialVelocity;
  QAngle m_angSpin;
  bool m_bDetonated = false;
  bool m_bStopped = false;
  float m_flStoppedTime = 0;
  std::vector<Vector> m_Trail;          // client trail
  void PhysicsSimulate() override;
  virtual void Detonate();
  void DrawModel(const CViewSetup& view) override;
  void DrawTranslucent(const CViewSetup& view) override;
  bool IsSolidForTrace(unsigned) const override { return false; }
};

class CHEGrenadeProjectile : public CBaseCSGrenadeProjectile {
public:
  using CBaseCSGrenadeProjectile::CBaseCSGrenadeProjectile;
  void Detonate() override;
};
class CFlashbangProjectile : public CBaseCSGrenadeProjectile {
public:
  using CBaseCSGrenadeProjectile::CBaseCSGrenadeProjectile;
  void Detonate() override;
};
class CSmokeGrenadeProjectile : public CBaseCSGrenadeProjectile {
public:
  using CBaseCSGrenadeProjectile::CBaseCSGrenadeProjectile;
  bool m_bDidSmokeEffect = false;
  float m_flSmokeStart = 0;
  Vector m_vecSmokeCenter;
  void PhysicsSimulate() override;
  void Detonate() override;
  void DrawTranslucent(const CViewSetup& view) override;
  float GetSmokeRadius() const;    // current (grows in / fades out)
};
// smoke volumes for visibility tests (bots, ESP "visible" checks, kill feed "through smoke")
bool LineGoesThroughSmoke(const Vector& a, const Vector& b);
// the smoke system keeps active volumes
struct SmokeVolume { Vector center; float start = 0; float end = 0; };
std::vector<SmokeVolume>& GetActiveSmokes();

// predicted grenade path (for the cheat "grenade prediction" and bots)
struct GrenadePath { std::vector<Vector> points; std::vector<Vector> bounces; Vector detonate; float time = 0; };
void PredictGrenadePath(int weaponId, const Vector& src, const Vector& vel, GrenadePath& out, const IHandleEntity* skip);
void GetGrenadeThrowParams(CCSPlayer* player, const QAngle& eyeAngles, float strength, Vector& outSrc, Vector& outVel);

class CPlantedC4 : public C_BaseEntity {
public:
  CPlantedC4(CCSPlayer* planter, const Vector& pos, float yaw);
  bool m_bBombTicking = true;
  int m_nBombSite = 0;
  float m_flC4Blow = 0;
  float m_flTimerLength = 40.f;
  float m_flNextBeep = 0;
  float m_flNextGlow = 0;
  float m_flDefuseLength = 10.f;
  float m_flDefuseCountDown = 0;
  bool m_bBombDefused = false;
  bool m_bExploded = false;
  CCSPlayer* m_hBombDefuser = nullptr;
  CCSPlayer* m_hPlanter = nullptr;
  float m_flYaw = 0;
  void Think() override;
  void PhysicsSimulate() override;
  void Explode();
  void StartDefuse(CCSPlayer* p);
  void AbortDefuse();
  float GetTimeRemaining() const;
  void DrawModel(const CViewSetup& view) override;
  bool IsSolidForTrace(unsigned) const override { return false; }
};
inline CPlantedC4* g_pPlantedC4 = nullptr;

// dropped C4 (when the carrier dies) is a normal dropped CC4 weapon entity
void RadiusDamage(const CTakeDamageInfo& info, const Vector& src, float radius, C_BaseEntity* ignore);
