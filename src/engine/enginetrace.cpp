#include "enginetrace.hpp"
#include <cfloat>

static const surfacedata_t s_surfaces[SURFACE_COUNT] = {
  //  name           mat               pen   dmg   fric step  impact color
  { "default",      CHAR_TEX_CONCRETE, 1.0f, 0.5f, 0.8f, 0, 150, 150, 150 },
  { "concrete",     CHAR_TEX_CONCRETE, 0.5f, 0.5f, 0.8f, 0, 160, 160, 150 },
  { "sandstone",    CHAR_TEX_CONCRETE, 0.55f, 0.5f, 0.8f, 1, 215, 185, 130 },
  { "plaster",      CHAR_TEX_PLASTER,  0.7f, 0.5f, 0.8f, 0, 220, 215, 200 },
  { "brick",        CHAR_TEX_CONCRETE, 0.47f, 0.5f, 0.8f, 0, 170, 90, 70 },
  { "metal",        CHAR_TEX_METAL,    0.3f, 0.5f, 0.8f, 2, 120, 120, 130 },
  { "metalgrate",   CHAR_TEX_GRATE,    0.99f, 0.99f, 0.8f, 2, 110, 110, 110 },
  { "wood",         CHAR_TEX_WOOD,     0.9f, 0.6f, 0.8f, 3, 150, 110, 60 },
  { "wood_crate",   CHAR_TEX_WOOD,     0.9f, 0.6f, 0.8f, 3, 170, 130, 70 },
  { "dirt",         CHAR_TEX_DIRT,     0.6f, 0.3f, 0.8f, 4, 120, 95, 60 },
  { "sand",         CHAR_TEX_SAND,     0.3f, 0.25f, 0.8f, 5, 220, 190, 140 },
  { "tile",         CHAR_TEX_TILE,     0.65f, 0.3f, 0.8f, 6, 200, 200, 200 },
  { "glass",        CHAR_TEX_GLASS,    0.99f, 0.99f, 0.8f, 7, 200, 230, 255 },
  { "flesh",        CHAR_TEX_FLESH,    0.9f, 0.9f, 0.8f, 0, 160, 0, 0 },
  { "cardboard",    CHAR_TEX_CARDBOARD, 0.9f, 0.9f, 0.8f, 3, 190, 160, 110 },
};
const surfacedata_t* GetSurfaceData(int idx) { return &s_surfaces[(idx >= 0 && idx < SURFACE_COUNT) ? idx : 0]; }

static constexpr float DIST_EPSILON = 0.03125f;

void CEngineTrace::Clear() { brushes.clear(); planes.clear(); m_cells.clear(); m_gw = m_gh = 0; worldMins = Vector(1e9f, 1e9f, 1e9f); worldMaxs = Vector(-1e9f, -1e9f, -1e9f); }

int CEngineTrace::AddBrush(const std::vector<cplane_t>& pl, const Vector& mins, const Vector& maxs, int contents, int surfaceProp, int material, bool render) {
  cbrush_t b;
  b.mins = mins; b.maxs = maxs; b.firstPlane = (int)planes.size(); b.numPlanes = (int)pl.size();
  b.contents = contents; b.surfaceProp = (short)surfaceProp; b.material = (short)material; b.render = render;
  for (auto p : pl) {
    p.signbits = 0; for (int i = 0; i < 3; i++) if (p.normal[i] < 0) p.signbits |= 1 << i;
    planes.push_back(p);
  }
  brushes.push_back(b);
  worldMins = VecMin(worldMins, mins); worldMaxs = VecMax(worldMaxs, maxs);
  return (int)brushes.size() - 1;
}
int CEngineTrace::AddBox(const Vector& mins, const Vector& maxs, int contents, int surfaceProp, int material, bool render) {
  std::vector<cplane_t> pl(6);
  pl[0].normal = { 1, 0, 0 }; pl[0].dist = maxs.x;
  pl[1].normal = { -1, 0, 0 }; pl[1].dist = -mins.x;
  pl[2].normal = { 0, 1, 0 }; pl[2].dist = maxs.y;
  pl[3].normal = { 0, -1, 0 }; pl[3].dist = -mins.y;
  pl[4].normal = { 0, 0, 1 }; pl[4].dist = maxs.z;
  pl[5].normal = { 0, 0, -1 }; pl[5].dist = -mins.z;
  return AddBrush(pl, mins, maxs, contents, surfaceProp, material, render);
}
int CEngineTrace::AddRamp(const Vector& mins, const Vector& maxs, int riseDir, int contents, int surfaceProp, int material) {
  // axial bevels (bounding box) + slope plane
  std::vector<cplane_t> pl(7);
  pl[0].normal = { 1, 0, 0 }; pl[0].dist = maxs.x;
  pl[1].normal = { -1, 0, 0 }; pl[1].dist = -mins.x;
  pl[2].normal = { 0, 1, 0 }; pl[2].dist = maxs.y;
  pl[3].normal = { 0, -1, 0 }; pl[3].dist = -mins.y;
  pl[4].normal = { 0, 0, 1 }; pl[4].dist = maxs.z;
  pl[5].normal = { 0, 0, -1 }; pl[5].dist = -mins.z;
  float h = maxs.z - mins.z;
  Vector n; Vector pt;
  switch (riseDir) {
    case 0: { float L = maxs.x - mins.x; n = Vector(-h, 0, L); pt = Vector(mins.x, 0, mins.z); break; } // rises toward +x
    case 1: { float L = maxs.x - mins.x; n = Vector(h, 0, L); pt = Vector(maxs.x, 0, mins.z); break; }
    case 2: { float L = maxs.y - mins.y; n = Vector(0, -h, L); pt = Vector(0, mins.y, mins.z); break; }
    default: { float L = maxs.y - mins.y; n = Vector(0, h, L); pt = Vector(0, maxs.y, mins.z); break; }
  }
  n.NormalizeInPlace(); pl[6].normal = n; pl[6].dist = n.Dot(pt);
  return AddBrush(pl, mins, maxs, contents, surfaceProp, material, true);
}

void CEngineTrace::Finalize() {
  if (brushes.empty()) return;
  m_gx0 = worldMins.x - GRID; m_gy0 = worldMins.y - GRID;
  m_gw = (int)((worldMaxs.x - m_gx0) / GRID) + 2; m_gh = (int)((worldMaxs.y - m_gy0) / GRID) + 2;
  m_cells.assign(m_gw * m_gh, {});
  for (int i = 0; i < (int)brushes.size(); i++) {
    auto& b = brushes[i];
    int x0 = Clamp((int)((b.mins.x - m_gx0) / GRID), 0, m_gw - 1), x1 = Clamp((int)((b.maxs.x - m_gx0) / GRID), 0, m_gw - 1);
    int y0 = Clamp((int)((b.mins.y - m_gy0) / GRID), 0, m_gh - 1), y1 = Clamp((int)((b.maxs.y - m_gy0) / GRID), 0, m_gh - 1);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) m_cells[y * m_gw + x].push_back(i);
  }
}

void CEngineTrace::TraceToBrush(const TraceWork& tw, int bi, trace_t& tr) {
  const cbrush_t& b = brushes[bi];
  float enterFrac = -1.f, leaveFrac = 1.f;
  const cplane_t* clip = nullptr;
  bool getout = false, startout = false;
  for (int i = 0; i < b.numPlanes; i++) {
    const cplane_t& p = planes[b.firstPlane + i];
    float dist;
    if (tw.isPoint) dist = p.dist;
    else {
      Vector ofs(p.normal.x < 0 ? tw.maxs.x : tw.mins.x, p.normal.y < 0 ? tw.maxs.y : tw.mins.y, p.normal.z < 0 ? tw.maxs.z : tw.mins.z);
      dist = p.dist - ofs.Dot(p.normal);
    }
    float d1 = tw.start.Dot(p.normal) - dist, d2 = tw.end.Dot(p.normal) - dist;
    if (d2 > 0) getout = true;
    if (d1 > 0) startout = true;
    if (d1 > 0 && (d2 >= DIST_EPSILON || d2 >= d1)) return;
    if (d1 <= 0 && d2 <= 0) continue;
    if (d1 > d2) { float f = (d1 - DIST_EPSILON) / (d1 - d2); if (f < 0) f = 0; if (f > enterFrac) { enterFrac = f; clip = &p; } }
    else { float f = (d1 + DIST_EPSILON) / (d1 - d2); if (f > 1) f = 1; if (f < leaveFrac) leaveFrac = f; }
  }
  if (!startout) {
    tr.startsolid = true; tr.contents = b.contents; tr.brushIndex = bi;
    if (!getout) { tr.allsolid = true; tr.fraction = 0; }
    return;
  }
  if (enterFrac < leaveFrac && enterFrac > -1 && enterFrac < tr.fraction && clip) {
    if (enterFrac < 0) enterFrac = 0;
    tr.fraction = enterFrac; tr.plane = *clip; tr.contents = b.contents; tr.brushIndex = bi; tr.m_pEnt = nullptr;
    tr.surface.surfaceProps = b.surfaceProp; tr.surface.flags = b.surfFlags; tr.surface.name = GetSurfaceData(b.surfaceProp)->name;
  }
}

void CEngineTrace::TraceAgainstBox(const TraceWork& tw, const Vector& bmins, const Vector& bmaxs, trace_t& tr) {
  // same algorithm as TraceToBrush for an axis aligned box
  float enterFrac = -1.f, leaveFrac = 1.f; int clipAxis = -1; float clipSign = 0;
  bool getout = false, startout = false;
  for (int i = 0; i < 6; i++) {
    int ax = i >> 1; float sign = (i & 1) ? -1.f : 1.f;
    float pd = (i & 1) ? -bmins[ax] : bmaxs[ax];
    float ofs = tw.isPoint ? 0 : (sign < 0 ? tw.maxs[ax] : tw.mins[ax]) * sign;
    float dist = pd - ofs;
    float d1 = tw.start[ax] * sign - dist, d2 = tw.end[ax] * sign - dist;
    if (d2 > 0) getout = true;
    if (d1 > 0) startout = true;
    if (d1 > 0 && (d2 >= DIST_EPSILON || d2 >= d1)) return;
    if (d1 <= 0 && d2 <= 0) continue;
    if (d1 > d2) { float f = (d1 - DIST_EPSILON) / (d1 - d2); if (f < 0) f = 0; if (f > enterFrac) { enterFrac = f; clipAxis = ax; clipSign = sign; } }
    else { float f = (d1 + DIST_EPSILON) / (d1 - d2); if (f > 1) f = 1; if (f < leaveFrac) leaveFrac = f; }
  }
  if (!startout) { tr.startsolid = true; if (!getout) { tr.allsolid = true; tr.fraction = 0; } return; }
  if (enterFrac < leaveFrac && enterFrac > -1 && enterFrac < tr.fraction && clipAxis >= 0) {
    if (enterFrac < 0) enterFrac = 0;
    tr.fraction = enterFrac; tr.plane.normal.Init(); tr.plane.normal[clipAxis] = clipSign;
    tr.plane.dist = clipSign > 0 ? bmaxs[clipAxis] : -bmins[clipAxis];
    tr.contents = CONTENTS_MONSTER;
  }
}

void CEngineTrace::TraceRayWorld(const Ray_t& ray, unsigned mask, trace_t* ptr) {
  trace_t& tr = *ptr;
  tr = trace_t();
  traceCount++;
  TraceWork tw;
  tw.mins = -ray.m_Extents; tw.maxs = ray.m_Extents;
  // our Ray_t stores the start at the box center; trace with centered extents
  tw.start = ray.m_Start; tw.end = ray.m_Start + ray.m_Delta;
  tw.isPoint = ray.m_IsRay; tw.mask = mask;
  tr.startpos = tw.start;
  if (m_cells.empty()) { tr.endpos = tw.end; tr.startpos = tw.start + ray.m_StartOffset; tr.endpos = tw.end + ray.m_StartOffset; return; }

  float len = ray.m_Delta.Length();
  int segs = std::max(1, (int)std::ceil(len / 384.f));
  // per-thread mailbox so the lightmap baker can trace from many threads
  thread_local std::vector<int> tl_mail; thread_local int tl_count = 0; thread_local const void* tl_owner = nullptr;
  if (tl_mail.size() != brushes.size() || tl_owner != (const void*)brushes.data()) { tl_mail.assign(brushes.size(), 0); tl_count = 0; tl_owner = brushes.data(); }
  if (++tl_count == 0x7fffffff) { std::fill(tl_mail.begin(), tl_mail.end(), 0); tl_count = 1; }
  for (int s = 0; s < segs; s++) {
    float f0 = (float)s / segs, f1 = (float)(s + 1) / segs;
    if (tr.fraction < f0) break;
    Vector a = tw.start + ray.m_Delta * f0, b = tw.start + ray.m_Delta * f1;
    Vector mn = VecMin(a, b) + tw.mins - Vector(1, 1, 1), mx = VecMax(a, b) + tw.maxs + Vector(1, 1, 1);
    int x0 = Clamp((int)((mn.x - m_gx0) / GRID), 0, m_gw - 1), x1 = Clamp((int)((mx.x - m_gx0) / GRID), 0, m_gw - 1);
    int y0 = Clamp((int)((mn.y - m_gy0) / GRID), 0, m_gh - 1), y1 = Clamp((int)((mx.y - m_gy0) / GRID), 0, m_gh - 1);
    for (int y = y0; y <= y1; y++) for (int x = x0; x <= x1; x++) {
      for (int bi : m_cells[y * m_gw + x]) {
        cbrush_t& br = brushes[bi];
        if (tl_mail[bi] == tl_count) continue;
        tl_mail[bi] = tl_count;
        if (!(br.contents & mask)) continue;
        // quick aabb reject against the full swept box
        Vector smn = VecMin(tw.start, tw.end) + tw.mins, smx = VecMax(tw.start, tw.end) + tw.maxs;
        if (br.mins.x > smx.x + 1 || br.maxs.x < smn.x - 1 || br.mins.y > smx.y + 1 || br.maxs.y < smn.y - 1 || br.mins.z > smx.z + 1 || br.maxs.z < smn.z - 1) continue;
        TraceToBrush(tw, bi, tr);
        if (tr.allsolid) { tr.endpos = tw.start + ray.m_StartOffset; tr.startpos = tw.start + ray.m_StartOffset; return; }
      }
    }
  }
  tr.endpos = tw.start + ray.m_Delta * tr.fraction + ray.m_StartOffset;
  tr.startpos = tw.start + ray.m_StartOffset;
}

void CEngineTrace::ClipRayToEntity(const Ray_t& ray, unsigned mask, IHandleEntity* ent, trace_t* tr) {
  trace_t t = *tr;
  if (ent->UsesHitboxesForRays() && (mask & CONTENTS_HITBOX) && ray.m_IsRay) {
    if (ent->TestHitboxes(ray, mask, t)) { *tr = t; }
    return;
  }
  if (!ent->IsSolidForTrace(mask)) return;
  Vector mn, mx; ent->GetCollisionBounds(mn, mx);
  TraceWork tw; tw.start = ray.m_Start; tw.end = ray.m_Start + ray.m_Delta; tw.mins = -ray.m_Extents; tw.maxs = ray.m_Extents; tw.isPoint = ray.m_IsRay; tw.mask = mask;
  float before = t.fraction; bool wasStartSolid = t.startsolid;
  t.startsolid = false; t.allsolid = false;
  TraceAgainstBox(tw, mn, mx, t);
  if (t.fraction < before || (t.startsolid && !wasStartSolid)) {
    t.m_pEnt = ent->GetBaseEntity(); t.hitgroup = 0;
    t.endpos = ray.m_Start + ray.m_Delta * t.fraction + ray.m_StartOffset;
    *tr = t;
  } else { tr->startsolid = wasStartSolid || tr->startsolid; }
}

void CEngineTrace::TraceRay(const Ray_t& ray, unsigned mask, ITraceFilter* filter, trace_t* tr) {
  TraceType_t tt = filter ? filter->GetTraceType() : TRACE_EVERYTHING;
  if (tt != TRACE_ENTITIES_ONLY) TraceRayWorld(ray, mask, tr);
  else { *tr = trace_t(); tr->startpos = ray.m_Start + ray.m_StartOffset; tr->endpos = ray.m_Start + ray.m_Delta + ray.m_StartOffset; }
  if (tt == TRACE_WORLD_ONLY || !entities || tr->allsolid) return;
  Vector smn = VecMin(ray.m_Start, ray.m_Start + ray.m_Delta) - ray.m_Extents, smx = VecMax(ray.m_Start, ray.m_Start + ray.m_Delta) + ray.m_Extents;
  for (IHandleEntity* e : *entities) {
    if (!e) continue;
    if (filter && !filter->ShouldHitEntity(e, (int)mask)) continue;
    Vector mn, mx; e->GetCollisionBounds(mn, mx);
    mn -= Vector(16, 16, 16); mx += Vector(16, 16, 16); // hitboxes may poke out of the hull
    if (mn.x > smx.x || mx.x < smn.x || mn.y > smx.y || mx.y < smn.y || mn.z > smx.z || mx.z < smn.z) continue;
    ClipRayToEntity(ray, mask, e, tr);
  }
}

int CEngineTrace::GetPointContents(const Vector& p, unsigned mask) {
  int c = 0;
  if (m_cells.empty()) return 0;
  int x = Clamp((int)((p.x - m_gx0) / GRID), 0, m_gw - 1), y = Clamp((int)((p.y - m_gy0) / GRID), 0, m_gh - 1);
  for (int bi : m_cells[y * m_gw + x]) if ((brushes[bi].contents & mask) && IsPointInsideBrush(p, bi)) c |= brushes[bi].contents;
  return c;
}
int CEngineTrace::GetBrushAtPoint(const Vector& p, unsigned mask) const {
  if (m_cells.empty()) return -1;
  int x = Clamp((int)((p.x - m_gx0) / GRID), 0, m_gw - 1), y = Clamp((int)((p.y - m_gy0) / GRID), 0, m_gh - 1);
  for (int bi : m_cells[y * m_gw + x]) if ((brushes[bi].contents & mask) && IsPointInsideBrush(p, bi)) return bi;
  return -1;
}
bool CEngineTrace::IsPointInsideBrush(const Vector& p, int bi, float eps) const {
  const cbrush_t& b = brushes[bi];
  for (int i = 0; i < b.numPlanes; i++) { const cplane_t& pl = planes[b.firstPlane + i]; if (p.Dot(pl.normal) - pl.dist > -eps) return false; }
  return true;
}

void CEngineTrace::TraceLine(const Vector& a, const Vector& b, unsigned mask, const IHandleEntity* skip, trace_t* tr) {
  Ray_t r; r.Init(a, b); CTraceFilter f(skip); TraceRay(r, mask, &f, tr);
}
void CEngineTrace::TraceHull(const Vector& a, const Vector& b, const Vector& mins, const Vector& maxs, unsigned mask, const IHandleEntity* skip, trace_t* tr) {
  Ray_t r; r.Init(a, b, mins, maxs); CTraceFilter f(skip); TraceRay(r, mask, &f, tr);
}
bool CEngineTrace::IsVisible(const Vector& a, const Vector& b) {
  Ray_t r; r.Init(a, b); trace_t tr; TraceRayWorld(r, MASK_VISIBLE, &tr); return tr.fraction >= 0.999f;
}
