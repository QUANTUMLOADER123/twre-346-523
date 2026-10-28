// nav_mesh.hpp - CNavMesh: walkable node graph + A* pathfinding (bots)
//
// Built maps (assets/maps/*.map): one node per walkable grid cell.
// Original CS 1.6 maps (BSP): the level is sampled in 32 unit columns with the real player hull; every floor
// found in a column becomes a node (several per column: bridges, tunnels, catwalks), and the links to the 8
// neighbouring columns are checked with hull traces (walk / ramp, jump, crouch-jump, drop).
#pragma once
#include "../../engine/mathlib.hpp"
#include <vector>
#include <string>

enum NavEdgeFlags { NAV_WALK = 0, NAV_JUMP = 1, NAV_CROUCHJUMP = 2, NAV_DROP = 4 };

struct CNavArea {
  Vector center;            // floor position (feet)
  float z = 0;
  bool walkable = false;
  bool obstacleTop = false; // crate / low wall top
  int place = -1;
  bool reachable = false;   // connected to the spawns (both directions)
  int column = -1;          // lookup column
  int link[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };     // neighbour node per direction
  signed char linkFlags[8] = { -1, -1, -1, -1, -1, -1, -1, -1 };
};

class CNavMesh {
public:
  void Build();
  bool IsBuilt() const { return m_built; }
  int GetAreaIndex(const Vector& pos) const;          // nearest reachable node (or -1)
  int GetAreaIndexRaw(const Vector& pos) const;       // nearest walkable node
  const CNavArea* GetArea(int idx) const { return (idx >= 0 && idx < (int)m_areas.size()) ? &m_areas[idx] : nullptr; }
  int NumAreas() const { return (int)m_areas.size(); }
  // A* path; returns world points (node floor positions) and per point edge flags
  bool ComputePath(const Vector& from, const Vector& to, std::vector<Vector>& out, std::vector<int>* flags = nullptr, float avoidDanger = 0.f);
  Vector GetRandomWalkablePoint(const Vector& near, float radius) const;
  int GetEdgeFlags(int a, int b) const;               // -1 when not linked
  float CellSize() const { return m_cell; }
  // column grid (radar fallback for maps without an overview image)
  void GetGrid(float& x0, float& y0, float& cell, int& w, int& h) const { x0 = m_x0; y0 = m_y0; cell = m_cell; w = m_cw; h = m_ch; }
  const std::vector<int>& Column(int i) const { static const std::vector<int> none; return (i >= 0 && i < (int)m_columns.size()) ? m_columns[i] : none; }
  std::vector<float> danger;                          // per area danger (recent deaths) for bots
  void AddDanger(const Vector& pos, float amount);
  void DecayDanger(float dt);
private:
  void BuildFromGrid();
  void BuildFromBsp();
  void FinishBuild();
  int Lookup(const Vector& p, bool reachableOnly, int maxRing) const;
  std::vector<CNavArea> m_areas;
  // column lookup grid: column (cx, cy) covers [x0 + cx*cell, x0 + (cx+1)*cell) x [y0 + cy*cell, ...)
  float m_x0 = 0, m_y0 = 0, m_cell = 64.f;
  int m_cw = 0, m_ch = 0;
  std::vector<std::vector<int>> m_columns;
  bool m_built = false;
};
inline CNavMesh g_NavMesh;
inline CNavMesh* TheNavMesh = &g_NavMesh;
