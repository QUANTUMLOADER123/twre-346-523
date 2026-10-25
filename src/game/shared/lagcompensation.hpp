// lagcompensation.hpp - CLagCompensationManager: rewinds players to the tick the shooter saw (cmd.tick_count)
#pragma once
#include "cs_player.hpp"

class CLagCompensationManager {
public:
  void FrameUpdatePostEntityThink(); // trim old records
  void StartLagCompensation(CCSPlayer* player, const CUserCmd* cmd);
  void FinishLagCompensation(CCSPlayer* player);
  bool IsCurrentlyDoingLagCompensation() const { return m_bActive; }
  // find the record that would be used for 'target' when 'tickCount' is requested (nullptr = current)
  const CLagRecord* FindRecord(CCSPlayer* target, int tickCount) const;
  static bool IsTickValid(int tickCount);
  static float GetLerpTime();
  int m_nLastRestoredCount = 0;
  int m_nLastBrokenLC = 0;
private:
  bool m_bActive = false;
  CCSPlayer* m_pCurrentPlayer = nullptr;
};
inline CLagCompensationManager g_LagCompensation;
inline CLagCompensationManager* lagcompensation = &g_LagCompensation;
