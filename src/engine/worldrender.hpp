// worldrender.hpp - brush world mesh builder, light baker and renderer (IVRenderView-ish)
#pragma once
#include "mathlib.hpp"
#include "view.hpp"
#include "raylib.h"
#include <vector>
#include <unordered_map>

struct CMapInfo;

struct LightSample { Vector ambient; Vector direct; Vector dir; }; // for models: ambient + directional

class CWorldRenderer {
public:
  void Build(const CMapInfo& map);
  void Unload();
  void DrawSky(const CViewSetup& view, float time);
  void DrawWorld(const CViewSetup& view);
  LightSample SampleLight(const Vector& pos);          // cached lighting probe (for players, weapons, props)
  Vector ComputeLightAt(const Vector& p, const Vector& n, int aoRays, int sunRays); // full lighting evaluation
  bool built = false;
  bool bsp = false;                                     // original GoldSrc map (bsprender.cpp)
  bool src = false;                                     // Counter-Strike: Source map (srcbsprender.cpp)
  int numTris = 0, numVerts = 0, numChunks = 0, drawnChunks = 0;
  float bakeSeconds = 0;
  const CMapInfo* map = nullptr;
private:
  // GoldSrc BSP maps (bsprender.cpp)
  void BuildBsp();
  void UnloadBsp();
  void DrawBsp(const CViewSetup& view);
  void DrawBspSky(const CViewSetup& view);
  LightSample SampleLightBsp(const Vector& pos);
  // Source maps (srcbsprender.cpp)
  void BuildSrc();
  void UnloadSrc();
  void DrawSrc(const CViewSetup& view);
  void DrawSrcSky(const CViewSetup& view);
  LightSample SampleLightSrc(const Vector& pos);
  struct SrcPropInst { struct CSrcModel* model = nullptr; Matrix xform{}; Color tint{ 255, 255, 255, 255 }; Vector mins, maxs; int skin = 0; float fadeMax = 0; };
  std::vector<SrcPropInst> m_srcProps;
  Texture2D m_srcSky[6]{};
  enum BspPass { BSPPASS_OPAQUE = 0, BSPPASS_LIGHTMAP, BSPPASS_ALPHATEST, BSPPASS_TRANSLUCENT, BSPPASS_ADDITIVE, BSPPASS_BLEND2 };
  struct BspChunk { int pass = 0; Mesh mesh{}; Material material{}; Vector mins, maxs; float dist = 0; };
  std::vector<BspChunk> m_bspChunks;
  std::vector<Texture2D> m_bspTextures;
  Texture2D m_lightmap{}; Texture2D m_sky[6]{}; bool m_hasSky = false;
  struct Chunk { int mat; Mesh mesh{}; Material material{}; Vector mins, maxs; };
  std::vector<Chunk> m_chunks;
  std::unordered_map<long long, LightSample> m_probeCache;
  float m_maxTop = 512;
};
inline CWorldRenderer g_WorldRender;

// frustum helper
struct Frustum {
  float planes[6][4];
  void FromMatrix(const Matrix& viewProj);
  bool CullBox(const Vector& mins, const Vector& maxs) const; // true = outside
};
