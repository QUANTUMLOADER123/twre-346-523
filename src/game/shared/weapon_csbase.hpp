// weapon_csbase.hpp - C_BaseCombatWeapon -> C_WeaponCSBase -> guns, CKnife, CBaseCSGrenade, CC4
#pragma once
#include "baseentity.hpp"
#include "weapon_parse.hpp"

enum WeaponState { WEAPON_NOT_CARRIED = 0, WEAPON_IS_CARRIED_BY_PLAYER, WEAPON_IS_ACTIVE };
// viewmodel "activities" (procedural viewmodel animation)
enum ViewModelActivity { ACT_VM_IDLE = 0, ACT_VM_DRAW, ACT_VM_PRIMARYATTACK, ACT_VM_SECONDARYATTACK, ACT_VM_RELOAD, ACT_VM_PULLPIN, ACT_VM_THROW, ACT_VM_HOLSTER, ACT_VM_ATTACH_SILENCER, ACT_VM_DETACH_SILENCER, ACT_VM_PLANT };

class C_BaseCombatWeapon : public C_BaseEntity {
public:
  C_BaseCombatWeapon() { m_iEntClass = ENTCLASS_WEAPON; m_vecMins = { -6, -6, 0 }; m_vecMaxs = { 6, 6, 6 }; }
  int m_iClip1 = -1, m_iClip2 = -1;
  int m_iPrimaryReserveAmmoCount = 0, m_iSecondaryReserveAmmoCount = 0;
  float m_flNextPrimaryAttack = 0, m_flNextSecondaryAttack = 0, m_flTimeWeaponIdle = 0;
  int m_iState = WEAPON_NOT_CARRIED;
  CCSPlayer* m_hOwner = nullptr;
  bool m_bInReload = false;
  float m_flReloadStartTime = 0, m_flReloadFinishTime = 0;
  int m_iViewModelIndex = 0, m_iWorldModelIndex = 0;
  int m_iPrimaryAmmoType = 0;
  bool m_bFiresUnderwater = false;
  bool IsWeapon() const override { return true; }
};

class C_WeaponCSBase : public C_BaseCombatWeapon {
public:
  explicit C_WeaponCSBase(int id);
  int m_iWeaponID = 0;
  const CCSWeaponInfo* m_pWeaponInfo = nullptr;
  float m_flAccuracyPenalty = 0;
  float m_fLastShotTime = -100;
  float m_flRecoilIndex = 0;
  int m_zoomLevel = 0;
  bool m_bSilencerOn = false;
  bool m_bBurstMode = false;
  int m_iBurstShotsRemaining = 0;
  float m_fNextBurstShot = 0;
  float m_flPostponeFireReadyTime = 0; // revolver
  bool m_bReloadVisuallyComplete = true;
  float m_flDroppedTime = -100;
  float m_flDeployTime = 0;
  float m_flDoneSwitchingSilencer = 0;
  CCSPlayer* m_hPrevOwner = nullptr;
  bool m_bOnGround = false;
  // procedural viewmodel animation
  int m_iViewActivity = ACT_VM_IDLE;
  float m_flViewActivityStart = 0;
  float m_flViewActivityDuration = 0.5f;
  int m_iViewShotSide = 0;

  const CCSWeaponInfo& GetCSWpnData() const { return *m_pWeaponInfo; }
  int GetCSWeaponID() const { return m_iWeaponID; }
  int GetWeaponType() const { return m_pWeaponInfo->type; }
  bool IsGun() const { int t = GetWeaponType(); return t != WEAPONTYPE_KNIFE && t != WEAPONTYPE_GRENADE && t != WEAPONTYPE_C4; }
  bool IsFullAuto() const { return m_pWeaponInfo->fullAuto; }
  bool HasAmmo() const { return m_iClip1 > 0 || m_iClip1 < 0; }
  bool IsReloading() const { return m_bInReload; }
  bool IsZoomed() const { return m_zoomLevel > 0; }
  void SendViewActivity(int act, float duration);
  float GetViewActivityCycle() const;

  virtual void Deploy();
  virtual void Holster();
  virtual void ItemPostFrame();
  virtual void PrimaryAttack();
  virtual void SecondaryAttack();
  virtual bool Reload();
  virtual void FinishReload();
  virtual void WeaponIdle() {}
  virtual float GetInaccuracy() const;
  virtual float GetSpread() const;
  virtual float GetMaxSpeed() const;
  virtual bool CanDeploy() const { return true; }
  virtual bool IsAutoRepeat() const { return IsFullAuto(); }
  virtual void UpdateAccuracyPenalty();
  virtual void OnDropped();
  void PhysicsSimulate() override; // dropped weapon physics
  void Think() override;
  bool CanAttack() const;
  void DrawModel(const CViewSetup& view) override;
  int GetZoomFOV(int level) const;
  void ResetZoom();
};

class C_WeaponCSBaseGun : public C_WeaponCSBase {
public:
  using C_WeaponCSBase::C_WeaponCSBase;
  void PrimaryAttack() override;
  void SecondaryAttack() override;
  bool CSBaseGunFire(float cycleTime, int mode);
};

class CKnife : public C_WeaponCSBase {
public:
  CKnife() : C_WeaponCSBase(WEAPON_KNIFE) {}
  void PrimaryAttack() override;
  void SecondaryAttack() override;
  bool SwingOrStab(bool stab);
  float GetMaxSpeed() const override { return 250.f; }
};

class CBaseCSGrenade : public C_WeaponCSBase {
public:
  using C_WeaponCSBase::C_WeaponCSBase;
  bool m_bPinPulled = false;
  bool m_bRedraw = false;
  float m_fThrowTime = 0;
  float m_flThrowStrength = 1.f;
  void PrimaryAttack() override;
  void SecondaryAttack() override;
  void ItemPostFrame() override;
  void Deploy() override;
  void Holster() override;
  void ThrowGrenade();
  bool Reload() override { return false; }
};
class CHEGrenade : public CBaseCSGrenade { public: CHEGrenade() : CBaseCSGrenade(WEAPON_HEGRENADE) {} };
class CFlashbang : public CBaseCSGrenade { public: CFlashbang() : CBaseCSGrenade(WEAPON_FLASHBANG) {} };
class CSmokeGrenade : public CBaseCSGrenade { public: CSmokeGrenade() : CBaseCSGrenade(WEAPON_SMOKEGRENADE) {} };

class CC4 : public C_WeaponCSBase {
public:
  CC4() : C_WeaponCSBase(WEAPON_C4) {}
  bool m_bStartedArming = false;
  float m_fArmedTime = 0;
  bool m_bBombPlacedAnimation = false;
  void PrimaryAttack() override;
  void ItemPostFrame() override;
  void Holster() override;
  bool Reload() override { return false; }
  void AbortArming();
};

C_WeaponCSBase* CreateWeapon(int weaponId);
class CStudioModel;
CStudioModel* GetWeaponWorldModelById(int id);
bool KnifeIsKarambit();                          // cl_knife_model 1: karambit (reverse grip)
