#include "nav_mesh.hpp"
#include "../shared/mapinfo.hpp"
#include "../../engine/enginetrace.hpp"
#include "../../engine/bsp.hpp"
#include "../../engine/srcbsp.hpp"
#include "../../engine/convar.hpp"
#include "../../engine/random.hpp"
#include <queue>
#include <cmath>
#include <algorithm>
#include <utility>
#include <chrono>

namespace {
// 8 directions in column space (y = world y)
const int DX[8] = { 1, -1, 0, 0, 1, 1, -1, -1 }, DY[8] = { 0, 0, 1, -1, 1, -1, 1, -1 };
int Opposite(int k) { static const int o[8] = { 1, 0, 3, 2, 7, 6, 5, 4 }; return o[k]; }
}

void CNavMesh::Build() {
  m_areas.clear(); m_columns.clear(); m_built = false;
  if (g_MapInfo.isBsp && (g_Bsp.loaded || g_SrcBsp.loaded)) BuildFromBsp(); else BuildFromGrid();
  FinishBuild();
}

// ------------------------------------------------------------------------------------------------
// built-in maps: one node per walkable grid cell
void CNavMesh::BuildFromGrid() {
  const CMapInfo& m = g_MapInfo;
  m_cell = m.cell; m_cw = m.gw; m_ch = m.gh;
  m_x0 = m.gridOrigin.x; m_y0 = m.gridOrigin.y - m.gh * m.cell;
  m_columns.assign((size_t)m_cw * m_ch, {});
  std::vector<int> cellNode((size_t)m_cw * m_ch, -1);
  for (int y = 0; y < m.gh; y++) for (int x = 0; x < m.gw; x++) {
    int i = y * m.gw + x;
    float z = m.floorZ[i]; uint8_t t = m.cellType[i];
    if (std::isnan(z) || t == 0 || t == 2) continue;
    CNavArea a; a.center = m.CellCenter(x, y); a.center.z = z; a.z = z;
    a.walkable = true; a.obstacleTop = t == 4;
    // head room: the standing hull must fit on top
    trace_t tr; g_EngineTrace.TraceHull(a.center + Vector(0, 0, 20), a.center + Vector(0, 0, 2), Vector(-12, -12, 0), Vector(12, 12, 54), MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
    if (tr.startsolid) continue;
    a.center.z = a.z = tr.endpos.z;
    int col = (m.gh - 1 - y) * m_cw + x; a.column = col;
    cellNode[i] = (int)m_areas.size(); m_columns[col].push_back((int)m_areas.size());
    m_areas.push_back(a);
  }
  auto edge = [&](int ia, int ib, int ca, int cb) {
    float dz = m_areas[ib].z - m_areas[ia].z;
    if (dz > 62.f) return -1;
    if (dz > 36.f) return (int)NAV_CROUCHJUMP;
    if (dz > 20.f) return (m.cellType[ca] == 3 || m.cellType[cb] == 3) && dz < 36.f ? (int)NAV_WALK : (int)NAV_JUMP;
    if (dz < -24.f) return dz < -260.f ? -1 : (int)NAV_DROP;
    return (int)NAV_WALK;
  };
  for (int y = 0; y < m.gh; y++) for (int x = 0; x < m.gw; x++) {
    int ci = y * m.gw + x, a = cellNode[ci]; if (a < 0) continue;
    for (int k = 0; k < 8; k++) {
      // grid y runs opposite to world y
      int nx = x + DX[k], ny = y - DY[k];
      if (nx < 0 || ny < 0 || nx >= m.gw || ny >= m.gh) continue;
      int cn = ny * m.gw + nx, b = cellNode[cn]; if (b < 0) continue;
      if (k >= 4) { // diagonal: both orthogonal neighbours must be walkable and level
        int o1 = cellNode[y * m.gw + nx], o2 = cellNode[ny * m.gw + x];
        if (o1 < 0 || o2 < 0) continue;
        if (std::fabs(m_areas[o1].z - m_areas[a].z) > 20 || std::fabs(m_areas[o2].z - m_areas[a].z) > 20) continue;
      }
      int f = edge(a, b, ci, cn); if (f < 0) continue;
      m_areas[a].link[k] = b; m_areas[a].linkFlags[k] = (signed char)f;
    }
  }
}

// ------------------------------------------------------------------------------------------------
// original maps: sample floors with the GoldSrc standing hull (hull 1, centered: feet = center - 36)
void CNavMesh::BuildFromBsp() {
  auto t0 = std::chrono::steady_clock::now();
  const CBspWorld& W = g_Bsp;
  const bool src = g_SrcBsp.loaded;   // CS:S map: brush collision in g_EngineTrace instead of GoldSrc hulls
  const Vector wmins = src ? g_SrcBsp.worldMins : W.worldMins, wmaxs = src ? g_SrcBsp.worldMaxs : W.worldMaxs;
  // standing hull trace between hull *centers* (GoldSrc hull 1 convention: center = feet + 36)
  struct HTr { bool allsolid = false, startsolid = false; float fraction = 1; Vector endpos, normal; bool sky = false; };
  auto T1 = [&](const Vector& a, const Vector& b) {
    HTr r;
    if (src) {
      trace_t tr; g_EngineTrace.TraceHull(a, b, Vector(-16, -16, -36), Vector(16, 16, 36), MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
      r.allsolid = tr.allsolid; r.startsolid = tr.startsolid; r.fraction = tr.fraction; r.endpos = tr.endpos; r.normal = tr.plane.normal; r.sky = (tr.surface.flags & SURF_SKY) != 0;
    } else {
      BspTrace tr; W.Trace(1, a, b, tr, true);
      r.allsolid = tr.allsolid; r.startsolid = tr.startsolid; r.fraction = tr.fraction; r.endpos = tr.endpos; r.normal = tr.normal; r.sky = tr.hitContents == BSP_CONTENTS_SKY;
    }
    return r;
  };
  m_cell = 32.f;
  m_x0 = std::floor(wmins.x / m_cell) * m_cell; m_y0 = std::floor(wmins.y / m_cell) * m_cell;
  m_cw = (int)std::ceil((wmaxs.x - m_x0) / m_cell) + 1; m_ch = (int)std::ceil((wmaxs.y - m_y0) / m_cell) + 1;
  if ((long long)m_cw * m_ch > 4000000) { Warning("Nav: map too large (%dx%d columns)\n", m_cw, m_ch); m_cw = m_ch = 0; return; }
  m_columns.assign((size_t)m_cw * m_ch, {});
  const float top = wmaxs.z - 40.f, bottom = wmins.z + 1.f;
  auto solid1 = [&](const Vector& c) {
    if (src) return T1(c, c).startsolid;
    int pc = W.PointContents(c, 1, 0); return pc == BSP_CONTENTS_SOLID || pc == BSP_CONTENTS_SKY;
  };
  for (int cy = 0; cy < m_ch; cy++) for (int cx = 0; cx < m_cw; cx++) {
    float x = m_x0 + (cx + 0.5f) * m_cell, y = m_y0 + (cy + 0.5f) * m_cell;
    float z = top;
    int guard = 0;
    while (z > bottom && guard++ < 64) {
      Vector c(x, y, z);
      // skip through solid space (hull 1 is the expanded world, so walls and floors are thick)
      if (solid1(c)) { z -= src ? 16.f : 8.f; continue; }
      HTr tr = T1(c, Vector(x, y, bottom));
      if (tr.allsolid || tr.fraction >= 1.f) break;
      float hitZ = tr.endpos.z;
      if (tr.normal.z >= 0.7f && !tr.sky) {
        Vector feet(x, y, hitZ - 36.f);
        // water / slime / lava floors are not used
        int pc = src ? BSP_CONTENTS_EMPTY : W.PointContents(feet + Vector(0, 0, 2), 0, 0);
        if (pc != BSP_CONTENTS_SLIME && pc != BSP_CONTENTS_LAVA) {
          CNavArea a; a.center = feet; a.z = feet.z; a.walkable = true; a.column = cy * m_cw + cx;
          m_columns[a.column].push_back((int)m_areas.size()); m_areas.push_back(a);
        }
      }
      z = hitZ - 2.f;
    }
  }
  // links: test hull movement towards each of the 8 neighbouring columns
  auto clear = [&](const Vector& a, const Vector& b) { HTr tr = T1(a, b); return !tr.startsolid && tr.fraction >= 1.f; };
  auto floorAt = [&](float x, float y, float fromZ, float& out) {
    Vector s(x, y, fromZ + 36.f); if (solid1(s)) return false;
    HTr tr = T1(s, Vector(x, y, fromZ - 120.f));
    if (tr.fraction >= 1.f || tr.startsolid) return false; out = tr.endpos.z - 36.f; return true;
  };
  const int n = (int)m_areas.size();
  for (int i = 0; i < n; i++) {
    CNavArea& A = m_areas[i];
    int cx = A.column % m_cw, cy = A.column / m_cw;
    for (int k = 0; k < 8; k++) {
      int nx = cx + DX[k], ny = cy + DY[k];
      if (nx < 0 || ny < 0 || nx >= m_cw || ny >= m_ch) continue;
      if (k >= 4) { // diagonals only when both orthogonal moves exist
        if (A.link[k == 4 || k == 5 ? 0 : 1] < 0 || A.link[k == 4 || k == 6 ? 2 : 3] < 0) continue;
      }
      int best = -1, bestFlag = -1; float bestDz = 1e9f;
      for (int j : m_columns[ny * m_cw + nx]) {
        const CNavArea& B = m_areas[j];
        float dz = B.z - A.z;
        if (dz > 58.f || dz < -260.f) continue;
        float H = std::max(A.z, B.z) + 2.f;
        int flag;
        if (dz > 0) { if (!clear(A.center + Vector(0, 0, 37), Vector(A.center.x, A.center.y, H + 36.f))) continue; }
        if (!clear(Vector(A.center.x, A.center.y, H + 36.f), Vector(B.center.x, B.center.y, H + 36.f))) continue;
        if (std::fabs(dz) <= 18.f) flag = NAV_WALK;
        else {
          // ramp or stairs: the floor half way is in between
          float mz;
          bool ramp = floorAt((A.center.x + B.center.x) * 0.5f, (A.center.y + B.center.y) * 0.5f, H, mz) && std::fabs(mz - (A.z + B.z) * 0.5f) < std::fabs(dz) * 0.35f + 2.f && std::fabs(dz) < 34.f;
          if (ramp) flag = NAV_WALK;
          else if (dz > 0) flag = dz <= 40.f ? NAV_JUMP : NAV_CROUCHJUMP;
          else flag = NAV_DROP;
        }
        if (std::fabs(dz) < bestDz) { bestDz = std::fabs(dz); best = j; bestFlag = flag; }
      }
      if (best >= 0) { A.link[k] = best; A.linkFlags[k] = (signed char)bestFlag; }
    }
  }
  Msg("Nav (BSP): %d nodes sampled in %.2fs\n", n, std::chrono::duration<float>(std::chrono::steady_clock::now() - t0).count());
}

void CNavMesh::FinishBuild() {
  const int n = (int)m_areas.size();
  danger.assign(n, 0.f);
  m_built = true;
  // reachability: an area counts when it can be reached from a spawn AND a spawn can be reached from it
  std::vector<std::vector<int>> rev(n);
  for (int i = 0; i < n; i++) for (int k = 0; k < 8; k++) if (m_areas[i].link[k] >= 0) rev[m_areas[i].link[k]].push_back(i);
  auto flood = [&](bool reverse) {
    std::vector<uint8_t> seen(n, 0); std::vector<int> stack;
    for (const auto& sp : g_MapInfo.spawns) { int i = GetAreaIndexRaw(sp.origin); if (i >= 0 && !seen[i]) { seen[i] = 1; stack.push_back(i); } }
    while (!stack.empty()) {
      int cur = stack.back(); stack.pop_back();
      if (!reverse) { for (int k = 0; k < 8; k++) { int nb = m_areas[cur].link[k]; if (nb >= 0 && !seen[nb]) { seen[nb] = 1; stack.push_back(nb); } } }
      else for (int nb : rev[cur]) if (!seen[nb]) { seen[nb] = 1; stack.push_back(nb); }
    }
    return seen;
  };
  auto fw = flood(false), bw = flood(true);
  int reach = 0;
  for (int i = 0; i < n; i++) { m_areas[i].reachable = fw[i] && bw[i]; reach += m_areas[i].reachable; }
  Msg("Nav mesh: %d areas (%dx%d columns of %.0f units), %d reachable\n", n, m_cw, m_ch, m_cell, reach);
}

// ------------------------------------------------------------------------------------------------
int CNavMesh::Lookup(const Vector& p, bool reachableOnly, int maxRing) const {
  if (m_columns.empty()) return -1;
  int cx = (int)std::floor((p.x - m_x0) / m_cell), cy = (int)std::floor((p.y - m_y0) / m_cell);
  int best = -1; float bestD = 1e18f;
  for (int r = 0; r <= maxRing && best < 0; r++) {
    for (int dy = -r; dy <= r; dy++) for (int dx = -r; dx <= r; dx++) {
      if (std::max(std::abs(dx), std::abs(dy)) != r) continue;
      int x = cx + dx, y = cy + dy;
      if (x < 0 || y < 0 || x >= m_cw || y >= m_ch) continue;
      for (int i : m_columns[y * m_cw + x]) {
        const CNavArea& a = m_areas[i];
        if (reachableOnly ? !a.reachable : !a.walkable) continue;
        float dz = a.z - p.z;
        if (dz > 72.f) dz *= 3.f;            // prefer floors below the point (feet / falling objects)
        float d = (a.center - p).Length2DSqr() + dz * dz * 4.f;
        if (d < bestD) { bestD = d; best = i; }
      }
    }
  }
  return best;
}
int CNavMesh::GetAreaIndexRaw(const Vector& p) const { return Lookup(p, false, m_cell < 48.f ? 6 : 3); }
int CNavMesh::GetAreaIndex(const Vector& p) const { if (!m_built) return -1; return Lookup(p, true, m_cell < 48.f ? 12 : 6); }

int CNavMesh::GetEdgeFlags(int a, int b) const {
  if (a < 0 || b < 0 || a >= (int)m_areas.size()) return -1;
  for (int k = 0; k < 8; k++) if (m_areas[a].link[k] == b) return m_areas[a].linkFlags[k];
  return -1;
}

bool CNavMesh::ComputePath(const Vector& from, const Vector& to, std::vector<Vector>& out, std::vector<int>* flags, float avoidDanger) {
  out.clear(); if (flags) flags->clear();
  int s = GetAreaIndex(from), g = GetAreaIndex(to);
  if (s < 0 || g < 0) return false;
  int n = (int)m_areas.size();
  std::vector<float> gcost(n, 1e30f);
  std::vector<int> parent(n, -1);
  std::vector<uint8_t> closed(n, 0);
  using QE = std::pair<float, int>;
  std::priority_queue<QE, std::vector<QE>, std::greater<QE>> open;
  auto h = [&](int i) { return (m_areas[i].center - m_areas[g].center).Length(); };
  gcost[s] = 0; open.push({ h(s), s });
  int expanded = 0, limit = m_cell < 48.f ? 80000 : 20000;
  const float dangerScale = m_cell / 64.f;
  while (!open.empty()) {
    int cur = open.top().second; open.pop();
    if (closed[cur]) continue;
    closed[cur] = 1;
    if (cur == g) break;
    if (++expanded > limit) break;
    const CNavArea& A = m_areas[cur];
    for (int k = 0; k < 8; k++) {
      int ni = A.link[k]; if (ni < 0 || closed[ni]) continue;
      int ef = A.linkFlags[k];
      float step = (m_areas[ni].center - A.center).Length();
      if (ef == NAV_JUMP) step += 64.f; else if (ef == NAV_CROUCHJUMP) step += 160.f; else if (ef == NAV_DROP) step += 24.f;
      if (m_areas[ni].obstacleTop) step += 96.f;
      if (avoidDanger > 0) step += danger[ni] * avoidDanger * dangerScale;
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
  const float R = 128.f;
  int cx = m_areas[i].column % m_cw, cy = m_areas[i].column / m_cw, rr = (int)std::ceil(R / m_cell);
  for (int dy = -rr; dy <= rr; dy++) for (int dx = -rr; dx <= rr; dx++) {
    int x = cx + dx, y = cy + dy; if (x < 0 || y < 0 || x >= m_cw || y >= m_ch) continue;
    for (int j : m_columns[y * m_cw + x]) {
      if (std::fabs(m_areas[j].z - m_areas[i].z) > 96.f) continue;
      float d = (m_areas[j].center - m_areas[i].center).Length2D() / 64.f;
      danger[j] += amount / (1.f + d * d);
    }
  }
}
void CNavMesh::DecayDanger(float dt) { float k = std::exp(-dt / 30.f); for (float& d : danger) d *= k; }
