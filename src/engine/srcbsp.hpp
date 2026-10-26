// srcbsp.hpp - Source engine BSP (VBSP v19-v21, Counter-Strike: Source maps)
//
// Loaded from the CS:S content (srcfs). Provides:
//   - render geometry: faces (with texture + lightmap coordinates), displacements (terrain, blended textures)
//   - collision: every solid brush (with Valve's bevel planes) goes straight into g_EngineTrace - CS:S collision
//     is brush based exactly like our trace code; displacements become thin triangle prisms
//   - entities (spawns, buy zones, bomb sites, lights), static props (game lump "sprp"), sky name
//   - the map's pakfile is mounted into srcfs while the map is loaded
#pragma once
#include "mathlib.hpp"
#include "bsp.hpp"          // BspEntity
#include <string>
#include <vector>
#include <cstdint>

struct SrcFaceVert { Vector pos, normal; float u = 0, v = 0; float lu = 0, lv = 0; float alpha = 1.f; };

struct SrcFace {
  int material = -1;                  // index into CSrcBsp::materials
  int model = 0;                      // 0 = world
  int texinfo = 0;
  int lightofs = -1; int lmMins[2] = { 0, 0 }, lmSize[2] = { 0, 0 };   // lmSize in luxels (= texture size - 1)
  uint8_t styles[4] = { 255, 255, 255, 255 };
  bool bump = false, disp = false;
  std::vector<SrcFaceVert> verts;     // triangle list (lightmap uv in luxels relative to the face, filled later)
  Vector normal;
};

struct SrcStaticProp {
  std::string model;                  // "models/props/de_dust/..."
  Vector origin; QAngle angles;
  int solid = 0, skin = 0; uint8_t flags = 0;
  float fadeMin = 0, fadeMax = 0;
  Vector lightingOrigin; bool useLightingOrigin = false;
};

struct SrcBrushEnt { int model = -1; int entity = -1; std::string classname; bool solid = true, visible = true; Vector origin; QAngle angles; int rendermode = 0; float renderamt = 255; };

class CSrcBsp {
public:
  bool Load(const std::string& mapName);   // "de_dust2" (maps/de_dust2.bsp via srcfs)
  void Unload();
  bool loaded = false;
  int version = 0;
  std::string name, skyName;
  std::vector<BspEntity> entities;
  std::vector<std::string> materials;      // texdata names
  std::vector<int> matWidth, matHeight;
  std::vector<SrcFace> faces;
  std::vector<SrcStaticProp> props;
  std::vector<SrcBrushEnt> brushEnts;
  std::vector<uint8_t> lighting;           // ColorRGBExp32 samples
  Vector worldMins, worldMaxs;
  Vector sunDir{ -0.4f, 0.3f, 0.85f }, sunColor{ 1, 0.95f, 0.8f }, ambientColor{ 0.4f, 0.4f, 0.45f };
  int numCollisionBrushes = 0, numDispTris = 0, numPropHulls = 0;
  std::vector<std::pair<Vector, Vector>> modelBounds;   // brush model index -> bounds

  // PVS (LUMP_VISIBILITY, compiled by vvis): which leaf clusters can possibly see each other.
  // Used by the server to decide which players are sent to a client (dormancy, sv_transmit.cpp).
  bool HasVis() const { return m_numClusters > 0; }
  int PointCluster(const Vector& p) const;              // -1 = solid / outside / no vis data
  bool ClusterVisible(int from, int to) const;          // true when unknown (no vis, invalid cluster)

  // light at a point (lightmap of the face below, linear 0..n) for models
  bool LightPoint(const Vector& p, Vector& color) const;
  static Vector DecodeLuxel(const uint8_t* rgbe);     // ColorRGBExp32 -> linear

private:
  struct Plane { Vector n; float d; };
  struct Node { int plane; int children[2]; int firstFace, numFaces; };
  struct TexInfo { float tv[2][4]; float lv[2][4]; int flags; int texdata; };
  struct FaceRaw { int plane; int side; int firstEdge, numEdges; int texinfo, dispinfo; uint8_t styles[4]; int lightofs; int lmMins[2], lmSize[2]; };
  std::vector<Plane> m_planes; std::vector<Node> m_nodes; std::vector<TexInfo> m_texinfo; std::vector<FaceRaw> m_rawFaces;
  std::vector<Vector> m_verts;
  std::vector<int16_t> m_leafCluster;                   // leaf -> cluster
  int m_numClusters = 0;
  std::vector<int> m_pvsOfs;                            // cluster -> byte offset of its compressed PVS row
  std::vector<uint8_t> m_visData;
  mutable std::vector<std::vector<uint8_t>> m_pvsRows;  // decompressed lazily
  bool RecursiveLight(int node, const Vector& a, const Vector& b, Vector& color) const;
  bool AddConvexHull(const std::vector<Vector>& pts, int surf);
  void LoadPhyHulls(const std::string& model, std::vector<std::vector<Vector>>& out);
};
inline CSrcBsp g_SrcBsp;
