// nav_mesh.hpp - CNavMesh: walkability grid built from the map cells + A* pathfinding (bots)
#pragma once
#include "../../engine/mathlib.hpp"
#include <vector>
#include <string>

struct CNavArea {           // one grid cell
  Vector center;
  float z = 0;
  bool walkable = false;
  bool obstacleTop = false; // crate / low wall top
  int place = -1;
  bool reachable = false;   // connected to the spawns (both directions)
};

enum NavEdgeFlags { NAV_WALK = 0, NAV_JUMP = 1, NAV_CROUCHJUMP = 2, NAV_DROP = 4 };

class CNavMesh {
public:
  void Build();
  bool IsBuilt() const { return m_built; }
  int GetAreaIndex(const Vector& pos) const;          // nearest reachable cell (or -1)
  int GetAreaIndexRaw(const Vector& pos) const;       // nearest walkable cell
  const CNavArea* GetArea(int idx) const { return (idx >= 0 && idx < (int)m_areas.size()) ? &m_areas[idx] : nullptr; }
  // A* path; returns world points (cell centers at walk height) and per point edge flags
  bool ComputePath(const Vector& from, const Vector& to, std::vector<Vector>& out, std::vector<int>* flags = nullptr, float avoidDanger = 0.f);
  Vector GetRandomWalkablePoint(const Vector& near, float radius) const;
  int GetEdgeFlags(int a, int b) const;
  int Width() const { return m_w; }
  int Height() const { return m_h; }
  std::vector<float> danger;                          // per area danger (recent deaths) for bots
  void AddDanger(const Vector& pos, float amount);
  void DecayDanger(float dt);
private:
  std::vector<CNavArea> m_areas;
  int m_w = 0, m_h = 0;
  bool m_built = false;
};
inline CNavMesh g_NavMesh;
inline CNavMesh* TheNavMesh = &g_NavMesh;
