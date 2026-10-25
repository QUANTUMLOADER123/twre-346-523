// enginetrace.hpp - collision world made of convex brushes + IEngineTrace style API
// (trace_t / Ray_t / ITraceFilter / CEngineTrace), surface properties for penetration.
#pragma once
#include "mathlib.hpp"
#include <vector>
#include <string>

// contents
enum {
  CONTENTS_EMPTY = 0, CONTENTS_SOLID = 0x1, CONTENTS_WINDOW = 0x2, CONTENTS_GRATE = 0x8, CONTENTS_WATER = 0x20,
  CONTENTS_MOVEABLE = 0x4000, CONTENTS_PLAYERCLIP = 0x10000, CONTENTS_MONSTERCLIP = 0x20000,
  CONTENTS_MONSTER = 0x2000000, CONTENTS_DEBRIS = 0x4000000, CONTENTS_HITBOX = 0x40000000,
};
constexpr unsigned MASK_ALL = 0xFFFFFFFF;
constexpr unsigned MASK_SOLID = CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_WINDOW | CONTENTS_MONSTER | CONTENTS_GRATE;
constexpr unsigned MASK_PLAYERSOLID = CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_PLAYERCLIP | CONTENTS_WINDOW | CONTENTS_MONSTER | CONTENTS_GRATE;
constexpr unsigned MASK_PLAYERSOLID_BRUSHONLY = CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_WINDOW | CONTENTS_PLAYERCLIP | CONTENTS_GRATE;
constexpr unsigned MASK_SHOT = CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_MONSTER | CONTENTS_WINDOW | CONTENTS_DEBRIS | CONTENTS_HITBOX;
constexpr unsigned MASK_SHOT_HULL = CONTENTS_SOLID | CONTENTS_MOVEABLE | CONTENTS_MONSTER | CONTENTS_WINDOW | CONTENTS_DEBRIS | CONTENTS_GRATE;
constexpr unsigned MASK_VISIBLE = CONTENTS_SOLID | CONTENTS_MOVEABLE;
constexpr unsigned MASK_OPAQUE = CONTENTS_SOLID | CONTENTS_MOVEABLE;
constexpr unsigned MASK_NPCWORLDSTATIC = CONTENTS_SOLID | CONTENTS_WINDOW | CONTENTS_MONSTERCLIP | CONTENTS_GRATE;

// surface flags
enum { SURF_LIGHT = 0x1, SURF_SKY2D = 0x2, SURF_SKY = 0x4, SURF_NODRAW = 0x80, SURF_HITBOX = 0x8000 };

// game material chars (decals.h / CHAR_TEX_*)
enum { CHAR_TEX_CONCRETE = 'C', CHAR_TEX_METAL = 'M', CHAR_TEX_DIRT = 'D', CHAR_TEX_VENT = 'V', CHAR_TEX_GRATE = 'G',
       CHAR_TEX_TILE = 'T', CHAR_TEX_WOOD = 'W', CHAR_TEX_GLASS = 'Y', CHAR_TEX_FLESH = 'F', CHAR_TEX_SAND = 'N',
       CHAR_TEX_PLASTIC = 'L', CHAR_TEX_CARDBOARD = 'U', CHAR_TEX_PLASTER = 'O' };

struct surfacedata_t {
  const char* name;
  char gameMaterial;
  float penetrationModifier; // CS:GO "penetrationmodifier"
  float damageModifier;      // CS:GO "damagemodifier"
  float friction;
  int stepSound;             // footstep sound class
  uint8_t impactR, impactG, impactB;
};
enum SurfaceProp {
  SURFACE_DEFAULT = 0, SURFACE_CONCRETE, SURFACE_SANDSTONE, SURFACE_PLASTER, SURFACE_BRICK, SURFACE_METAL, SURFACE_METALGRATE,
  SURFACE_WOOD, SURFACE_WOOD_CRATE, SURFACE_DIRT, SURFACE_SAND, SURFACE_TILE, SURFACE_GLASS, SURFACE_FLESH, SURFACE_CARDBOARD,
  SURFACE_COUNT
};
const surfacedata_t* GetSurfaceData(int idx);

struct cplane_t { Vector normal; float dist = 0; uint8_t type = 0, signbits = 0; };
struct csurface_t { const char* name = "**empty**"; short surfaceProps = 0; unsigned short flags = 0; };

class C_BaseEntity;
class IHandleEntity;

class CGameTrace {
public:
  Vector startpos, endpos;
  cplane_t plane;
  float fraction = 1.f;
  int contents = 0;
  unsigned short dispFlags = 0;
  bool allsolid = false, startsolid = false;
  float fractionleftsolid = 0;
  csurface_t surface;
  int hitgroup = 0;
  short physicsbone = 0;
  unsigned short worldSurfaceIndex = 0;
  C_BaseEntity* m_pEnt = nullptr;
  int hitbox = -1;
  int brushIndex = -1;
  bool DidHit() const { return fraction < 1.f || allsolid || startsolid; }
  bool DidHitWorld() const { return DidHit() && m_pEnt == nullptr; }
  bool DidHitNonWorldEntity() const { return m_pEnt != nullptr; }
};
using trace_t = CGameTrace;

struct Ray_t {
  Vector m_Start, m_Delta, m_StartOffset, m_Extents;
  bool m_IsRay = true, m_IsSwept = true;
  void Init(const Vector& start, const Vector& end) {
    m_Delta = end - start; m_IsSwept = m_Delta.LengthSqr() != 0; m_Extents.Init(); m_IsRay = true; m_StartOffset.Init(); m_Start = start;
  }
  void Init(const Vector& start, const Vector& end, const Vector& mins, const Vector& maxs) {
    m_Delta = end - start; m_IsSwept = m_Delta.LengthSqr() != 0;
    m_Extents = (maxs - mins) * 0.5f; m_IsRay = m_Extents.LengthSqr() < 1e-6f;
    m_StartOffset = (mins + maxs) * 0.5f; m_Start = start + m_StartOffset; m_StartOffset = -m_StartOffset;
  }
};

enum TraceType_t { TRACE_EVERYTHING = 0, TRACE_WORLD_ONLY, TRACE_ENTITIES_ONLY, TRACE_EVERYTHING_FILTER_PROPS };
class ITraceFilter {
public:
  virtual ~ITraceFilter() = default;
  virtual bool ShouldHitEntity(IHandleEntity* ent, int contentsMask) = 0;
  virtual TraceType_t GetTraceType() const { return TRACE_EVERYTHING; }
};
class CTraceFilter : public ITraceFilter {
public:
  explicit CTraceFilter(const IHandleEntity* skip = nullptr) : pSkip(skip) {}
  bool ShouldHitEntity(IHandleEntity* ent, int) override { return ent != pSkip; }
  const IHandleEntity* pSkip;
};
class CTraceFilterSkipTwoEntities : public ITraceFilter {
public:
  CTraceFilterSkipTwoEntities(const IHandleEntity* a, const IHandleEntity* b) : pA(a), pB(b) {}
  bool ShouldHitEntity(IHandleEntity* ent, int) override { return ent != pA && ent != pB; }
  const IHandleEntity *pA, *pB;
};
class CTraceFilterWorldOnly : public ITraceFilter {
public:
  bool ShouldHitEntity(IHandleEntity*, int) override { return false; }
  TraceType_t GetTraceType() const override { return TRACE_WORLD_ONLY; }
};

// entities that take part in traces implement this (C_BaseEntity does)
class IHandleEntity {
public:
  virtual ~IHandleEntity() = default;
  virtual bool IsSolidForTrace(unsigned mask) const = 0;     // hull collision (players block players)
  virtual void GetCollisionBounds(Vector& absMins, Vector& absMaxs) const = 0;
  virtual bool UsesHitboxesForRays() const { return false; }
  // ray vs hitboxes (fills hitgroup/hitbox/fraction/endpos when better than tr->fraction)
  virtual bool TestHitboxes(const Ray_t& ray, unsigned mask, trace_t& tr) { (void)ray; (void)mask; (void)tr; return false; }
  virtual C_BaseEntity* GetBaseEntity() = 0;
};

struct cbrush_t {
  Vector mins, maxs;
  int firstPlane = 0, numPlanes = 0;
  int contents = CONTENTS_SOLID;
  short surfaceProp = SURFACE_CONCRETE;
  short material = 0;       // render material
  unsigned short surfFlags = 0;
  int checkcount = 0;
  bool render = true;
  float texScale = 1.f;
  float texRot = 0.f;
  Vector tint{ 1, 1, 1 };
};

class CEngineTrace {
public:
  void Clear();
  int AddBrush(const std::vector<cplane_t>& planes, const Vector& mins, const Vector& maxs, int contents, int surfaceProp, int material, bool render = true);
  int AddBox(const Vector& mins, const Vector& maxs, int contents, int surfaceProp, int material, bool render = true);
  // wedge (ramp) inside box: slope rises along +axis (axis 0:+x 1:-x 2:+y 3:-y)
  int AddRamp(const Vector& mins, const Vector& maxs, int riseDir, int contents, int surfaceProp, int material);
  void Finalize(); // build acceleration grid

  void TraceRay(const Ray_t& ray, unsigned mask, ITraceFilter* filter, trace_t* tr);
  void TraceRayWorld(const Ray_t& ray, unsigned mask, trace_t* tr);
  void ClipRayToEntity(const Ray_t& ray, unsigned mask, IHandleEntity* ent, trace_t* tr);
  int GetPointContents(const Vector& p, unsigned mask = MASK_ALL);
  bool IsPointInsideBrush(const Vector& p, int brushIndex, float epsilon = 0.f) const;
  int GetBrushAtPoint(const Vector& p, unsigned mask) const;

  // convenience wrappers
  void TraceLine(const Vector& a, const Vector& b, unsigned mask, const IHandleEntity* skip, trace_t* tr);
  void TraceHull(const Vector& a, const Vector& b, const Vector& mins, const Vector& maxs, unsigned mask, const IHandleEntity* skip, trace_t* tr);
  bool IsVisible(const Vector& a, const Vector& b); // world-only, opaque

  std::vector<cbrush_t> brushes;
  std::vector<cplane_t> planes;
  std::vector<IHandleEntity*>* entities = nullptr; // set by game (entity list)
  Vector worldMins, worldMaxs;
  int traceCount = 0;

private:
  struct TraceWork { Vector start, end, mins, maxs, extents, offsetStart; bool isPoint; unsigned mask; };
  void TraceToBrush(const TraceWork& tw, int brushIndex, trace_t& tr);
  void TraceAgainstBox(const TraceWork& tw, const Vector& bmins, const Vector& bmaxs, trace_t& tr);
  static constexpr float GRID = 256.f;
  int m_gw = 0, m_gh = 0; float m_gx0 = 0, m_gy0 = 0;
  std::vector<std::vector<int>> m_cells;
  int m_checkcount = 0;
};
inline CEngineTrace g_EngineTrace;
inline CEngineTrace* enginetrace = &g_EngineTrace;
