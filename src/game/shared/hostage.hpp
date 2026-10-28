// hostage.hpp - CHostage: the hostage rescue objective (pick up, carry, rescue, round win)
#pragma once
#include "baseentity.hpp"

class CCSPlayer;

// A hostage is a neutral character the CTs can pick up (use key) and carry to a rescue zone.
// Rescuing every hostage wins the round for the CTs; the timer expiring with hostages still out
// wins it for the Ts. Hostages are solid to bullets / knife so the Ts can kill them (100 hp).
class CHostage : public C_BaseEntity {
public:
  CHostage();
  void Spawn() override;
  void Think() override;                          // follow the carrier / idle
  void DrawModel(const CViewSetup& view) override; // client render
  int OnTakeDamage(const CTakeDamageInfo& info) override;
  void Event_Killed(const CTakeDamageInfo& info) override;
  bool IsHostage() const override { return true; }
  bool IsSolidForTrace(unsigned mask) const override;   // blocks bullets while standing
  bool IsAlive() const override { return m_lifeState == LIFE_ALIVE; }

  int m_iVariant = 0;        // 0..3 hostage model
  bool m_bCarried = false;
  CCSPlayer* m_hCarrier = nullptr;
  bool m_bRescued = false;
  float m_flPhase = 0;        // idle breath / walk cycle

  static const char* ModelPath(int variant);
  static CHostage* SpawnAt(const Vector& origin, float yaw, int variant);
  static void ResetRound();                          // remove the old ones, spawn the new
  static int CountTotal();
  static int CountRescued();
  static int CountRemaining();                       // alive and not carried-to-safety
  static CHostage* FindNearest(CCSPlayer* p, float radius);
  static void OnCarrierKilled(CCSPlayer* carrier);   // drop the hostage when the carrier dies
};
CHostage* ToHostage(C_BaseEntity* e);
