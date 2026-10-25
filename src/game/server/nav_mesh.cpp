#include "nav_mesh.hpp"
#include "../shared/mapinfo.hpp"
#include "../../engine/enginetrace.hpp"
#include "../../engine/convar.hpp"
#include "../../engine/random.hpp"
#include <queue>
#include <cmath>
#include <algorithm>

void CNavMesh::Build() {
  const CMapInfo& m = g_MapInfo;
  m_w = m.gw; m_h = m.gh;
  m_areas.assign(m_w * m_h, CNavArea());
  danger.assign(m_w * m_h, 0.f);
  int walk = 0;
  for (int y = 0; y < m_h; y++) for (int x = 0; x < m_w; x++) {
    int i = y * m_w + x;
    CNavArea& a = m_areas[i];
    a.center = m.CellCenter(x, y);
    float z = m.floorZ[i];
    uint8_t t = m.cellType[i];
    if (std::isnan(z) || t == 0 || t == 2) continue;
    a.z = z; a.center.z = z;
    a.walkable = true; a.obstacleTop = t == 4;
    // verify there is head room (standing hull fits on top)
    trace_t tr; g_EngineTrace.TraceHull(a.center + Vector(0, 0, 20), a.center + Vector(0, 0, 2), Vector(-12, -12, 0), Vector(12, 12, 54), MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
    if (tr.startsolid) { a.walkable = false; continue; }
    a.center.z = tr.endpos.z; a.z = tr.endpos.z;
    walk++;
  }
  m_built = true;
  // reachability: forward flood fill from every spawn; an area counts when it can be reached from a spawn
  // AND a spawn can be reached from it (no one-way pits)
  auto flood = [&](bool reverse) {
    std::vector<uint8_t> seen(m_w * m_h, 0);
    std::vector<int> stack;
    for (const auto& sp : m.spawns) { int i = GetAreaIndexRaw(sp.origin); if (i >= 0 && !seen[i]) { seen[i] = 1; stack.push_back(i); } }
    static const int DX[4] = { 1, -1, 0, 0 }, DY[4] = { 0, 0, 1, -1 };
    while (!stack.empty()) {
      int cur = stack.back(); stack.pop_back();
      int cx = cur % m_w, cy = cur / m_w;
      for (int k = 0; k < 4; k++) {
        int nx = cx + DX[k], ny = cy + DY[k];
        if (nx < 0 || ny < 0 || nx >= m_w || ny >= m_h) continue;
        int ni = ny * m_w + nx;
        if (!m_areas[ni].walkable || seen[ni]) continue;
        if ((reverse ? GetEdgeFlags(ni, cur) : GetEdgeFlags(cur, ni)) < 0) continue;
        seen[ni] = 1; stack.push_back(ni);
      }
    }
    return seen;
  };
  auto fw = flood(false), bw = flood(true);
  int reach = 0;
  for (int i = 0; i < m_w * m_h; i++) { m_areas[i].reachable = m_areas[i].walkable && fw[i] && bw[i]; reach += m_areas[i].reachable; }
  Msg("Nav mesh: %d x %d grid, %d walkable areas, %d reachable\n", m_w, m_h, walk, reach);
}

int CNavMesh::GetAreaIndexRaw(const Vector& p) const {
  
  const CMapInfo& m = g_MapInfo;
  int cx = (int)std::floor((p.x - m.gridOrigin.x) / m.cell), cy = (int)std::floor((m.gridOrigin.y - p.y) / m.cell);
  int best = -1; float bestD = 1e18f;
  for (int r = 0; r <= 3 && best < 0; r++) {
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
      if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
      int x = cx + dx, y = cy + dy;
      if (x < 0 || y < 0 || x >= m_w || y >= m_h) continue;
      const CNavArea& a = m_areas[y * m_w + x];
      if (!a.walkable) continue;
      float dz = std::fabs(a.z - p.z);
      float d = (a.center - p).Length2DSqr() + dz * dz * 4.f;
      if (d < bestD) { bestD = d; best = y * m_w + x; }
    }
  }
  return best;
}

int CNavMesh::GetAreaIndex(const Vector& p) const {
  if (!m_built) return -1;
  const CMapInfo& m = g_MapInfo;
  int cx = (int)std::floor((p.x - m.gridOrigin.x) / m.cell), cy = (int)std::floor((m.gridOrigin.y - p.y) / m.cell);
  int best = -1; float bestD = 1e18f;
  for (int r = 0; r <= 6 && best < 0; r++) {
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
      if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
      int x = cx + dx, y = cy + dy;
      if (x < 0 || y < 0 || x >= m_w || y >= m_h) continue;
      const CNavArea& a = m_areas[y * m_w + x];
      if (!a.reachable) continue;
      float dz = std::fabs(a.z - p.z);
      float d = (a.center - p).Length2DSqr() + dz * dz * 4.f;
      if (d < bestD) { bestD = d; best = y * m_w + x; }
    }
  }
  return best;
}

int CNavMesh::GetEdgeFlags(int a, int b) const {
  float dz = m_areas[b].z - m_areas[a].z;
  if (dz > 62.f) return -1;            // too high
  if (dz > 36.f) return NAV_CROUCHJUMP;
  if (dz > 20.f) return (g_MapInfo.cellType[a] == 3 || g_MapInfo.cellType[b] == 3) && dz < 36.f ? NAV_WALK : NAV_JUMP;
  if (dz < -24.f) return dz < -260.f ? -1 : NAV_DROP;
  return NAV_WALK;
}

bool CNavMesh::ComputePath(const Vector& from, const Vector& to, std::vector<Vector>& out, std::vector<int>* flags, float avoidDanger) {
  out.clear(); if (flags) flags->clear();
  int s = GetAreaIndex(from), g = GetAreaIndex(to);
  if (s < 0 || g < 0) return false;
  int n = m_w * m_h;
  std::vector<float> gcost(n, 1e30f);
  std::vector<int> parent(n, -1);
  std::vector<uint8_t> closed(n, 0);
  using QE = std::pair<float, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
  auto h = [&](int i) { return (m_areas[i].center - m_areas[g].center).Length2D(); };
  gcost[s] = 0; open.push({ h(s), s });
  static const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
  int expanded = 0;
  while (!open.empty()) {
    int cur = open.top().second; open.pop();
    if (closed[cur]) continue;
    closed[cur] = 1;
    if (cur == g) break;
    if (++expanded > 20000) break;
    int cx = cur % m_w, cy = cur / m_w;
    for (int k = 0; k < 8; k++) {
      int nx = cx + DX[k], ny = cy + DY[k];
      if (nx < 0 || ny < 0 || nx >= m_w || ny >= m_h) continue;
      int ni = ny * m_w + nx;
      if (!m_areas[ni].walkable || closed[ni]) continue;
      if (k >= 4) { // diagonal: both orthogonal neighbours must be walkable and level
        int o1 = cy * m_w + nx, o2 = ny * m_w + cx;
        if (!m_areas[o1].walkable || !m_areas[o2].walkable) continue;
        if (std::fabs(m_areas[o1].z - m_areas[cur].z) > 20 || std::fabs(m_areas[o2].z - m_areas[cur].z) > 20) continue;
      }
      int ef = GetEdgeFlags(cur, ni);
      if (ef < 0) continue;
      float step = (k >= 4 ? 1.414f : 1.f) * g_MapInfo.cell;
      if (ef == NAV_JUMP) step += 64.f; else if (ef == NAV_CROUCHJUMP) step += 160.f;
      if (m_areas[ni].obstacleTop) step += 96.f;
      if (avoidDanger > 0) step += danger[ni] * avoidDanger;
      float ng = gcost[cur] + step;
      if (ng < gcost[ni]) { gcost[ni] = ng; parent[ni] = cur; open.push({ ng + h(ni), ni }); }
    }
  }
  if (parent[g] < 0 && g != s) return false;
  std::vector<int> cells;
  for (int c = g; c >= 0; c = parent[c]) { cells.push_back(c); if (c == s) break; }
  std::reverse(cells.begin(), cells.end());
  for (size_t i = 0; i < cells.size(); i++) {
    out.push_back(m_areas[cells[i]].center);
    if (flags) flags->push_back(i == 0 ? NAV_WALK : GetEdgeFlags(cells[i - 1], cells[i]));
  }
  if (!out.empty()) { out.back() = Vector(to.x, to.y, out.back().z); }
  return true;
}

Vector CNavMesh::GetRandomWalkablePoint(const Vector& near, float radius) const {
  for (int tries = 0; tries < 64; tries++) {
    Vector p = near + Vector(RandomFloat(-radius, radius), RandomFloat(-radius, radius), 0);
    int i = GetAreaIndex(p);
    if (i >= 0 && !m_areas[i].obstacleTop) return m_areas[i].center;
  }
  return near;
}

void CNavMesh::AddDanger(const Vector& pos, float amount) {
  int i = GetAreaIndex(pos); if (i < 0) return;
  int cx = i % m_w, cy = i / m_w;
  for (int dy = -2; dy <= 2; dy++) for (int dx = -2; dx <= 2; dx++) {
    int x = cx + dx, y = cy + dy; if (x < 0 || y < 0 || x >= m_w || y >= m_h) continue;
    danger[y * m_w + x] += amount / (1.f + dx * dx + dy * dy);
  }
}
void CNavMesh::DecayDanger(float dt) { float k = std::exp(-dt / 30.f); for (float& d : danger) d *= k; }
