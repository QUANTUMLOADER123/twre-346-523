// bsp.hpp - GoldSrc (Half-Life / CS 1.6) BSP v30 world.
//
// Loads an original map from the player's CS 1.6 install (see hlfs.hpp): entities, the BSP tree and the three
// clipping hulls (player collision exactly like CS 1.6, including clip brushes), faces with their lightmaps and
// textures (embedded mip textures or WAD3 files listed in worldspawn), brush entities (func_wall, func_breakable,
// func_illusionary, doors...) and the sky name. Rendering lives in bsprender.cpp, the engine trace uses
// CBspWorld::Trace for every world trace while a BSP map is loaded.
#pragma once
#include "mathlib.hpp"
#include <string>
#include <vector>
#include <map>
#include <cstdint>

// GoldSrc leaf contents
enum {
  BSP_CONTENTS_EMPTY = -1, BSP_CONTENTS_SOLID = -2, BSP_CONTENTS_WATER = -3, BSP_CONTENTS_SLIME = -4, BSP_CONTENTS_LAVA = -5,
  BSP_CONTENTS_SKY = -6, BSP_CONTENTS_ORIGIN = -7, BSP_CONTENTS_CLIP = -8, BSP_CONTENTS_TRANSLUCENT = -15, BSP_CONTENTS_LADDER = -16,
};

struct BspPlane { Vector normal; float dist = 0; int type = 0; };
struct BspClipNode { int plane = 0; int children[2] = { 0, 0 }; };   // child < 0: contents
struct BspNode { int plane = 0; int children[2] = { 0, 0 }; int firstFace = 0, numFaces = 0; Vector mins, maxs; };
struct BspLeaf { int contents = 0; int visofs = -1; Vector mins, maxs; int firstMark = 0, numMarks = 0; };
struct BspTexInfo { float vecs[2][4] = {}; int miptex = 0; int flags = 0; };
struct BspModel { Vector mins, maxs, origin; int headnode[4] = {}; int firstFace = 0, numFaces = 0; };

struct BspFace {
  int plane = 0; bool side = false; int texinfo = 0;
  uint8_t styles[4] = { 255, 255, 255, 255 }; int lightofs = -1;
  int firstVert = 0, numVerts = 0;      // into CBspWorld::faceVerts
  int texMins[2] = { 0, 0 }, extents[2] = { 0, 0 };   // lightmap space (GoldSrc: 16 texels per luxel)
  int lmW = 0, lmH = 0;
  int model = 0;                         // owning brush model (0 = world)
  Vector normal;                         // facing normal (plane normal, flipped for side 1)
};

struct BspTexture {
  std::string name;
  int w = 0, h = 0;
  std::vector<uint8_t> rgba;            // decoded mip 0 (empty if not found -> checkerboard)
  bool alphaTest = false;               // '{' textures: palette index 255 is transparent
  bool sky = false, water = false, nodraw = false;
  Vector avgColor{ 0.5f, 0.5f, 0.5f };
};

struct BspEntity {
  std::map<std::string, std::string> kv;
  const char* Get(const char* k, const char* def = "") const { auto it = kv.find(k); return it == kv.end() ? def : it->second.c_str(); }
  bool Has(const char* k) const { return kv.count(k) != 0; }
  float GetFloat(const char* k, float def = 0) const;
  int GetInt(const char* k, int def = 0) const { return (int)GetFloat(k, (float)def); }
  Vector GetVector(const char* k, const Vector& def = Vector()) const;
  std::string Class() const { return Get("classname"); }
  int ModelIndex() const;               // "*N" -> N, else -1
};

// a brush entity placed in the world (static: doors are drawn open and not solid)
struct BspBrushEntity {
  int model = 0; int entity = -1;
  std::string classname;
  bool solid = true, visible = true;
  int rendermode = 0; float renderamt = 255; Vector rendercolor{ 255, 255, 255 };
  matrix3x4_t xform;                    // model -> world
  Vector origin;                        // translation part (for collision; rotation is not used for collision)
  Vector absMins, absMaxs;
  // breakables
  float health = 0; int material = 0; bool breakable = false, broken = false;
};

struct BspTrace {
  bool allsolid = true, startsolid = false, inopen = false, inwater = false;
  float fraction = 1.f;
  Vector endpos, normal; float dist = 0;
  int hitNode = -1;                     // node/clipnode whose plane was hit
  int hitContents = 0;                  // contents behind the hit plane (SOLID or SKY)
  int brushEntity = -1;                 // index into brushEnts when a brush entity was hit
};

class CBspWorld {
public:
  bool Load(const std::string& realPath, const std::string& mapName);
  void Unload();
  bool loaded = false;
  std::string name, skyName;
  std::vector<BspEntity> entities;
  std::vector<BspPlane> planes;
  std::vector<BspNode> nodes;
  std::vector<BspClipNode> hull0;       // nodes converted to clip nodes (same indices as `nodes`)
  std::vector<BspClipNode> clipnodes;   // hulls 1-3
  std::vector<BspLeaf> leaves;
  std::vector<BspModel> models;
  std::vector<BspTexInfo> texinfo;
  std::vector<BspTexture> textures;
  std::vector<BspFace> faces;
  std::vector<Vector> faceVerts;
  std::vector<uint8_t> lighting;        // RGB luxels
  std::vector<uint16_t> markSurfaces;
  std::vector<BspBrushEntity> brushEnts;
  std::vector<int> modelToBrushEnt;     // model index -> brushEnts index (-1)
  Vector worldMins, worldMaxs;
  Vector sunDir{ -0.4f, 0.3f, 0.85f }, sunColor{ 1, 0.95f, 0.8f };
  int texturesMissing = 0;

  // collision. hull: 0 point, 1 standing (32x32x72), 2 large, 3 crouched (32x32x36); boxes are centered.
  static const Vector& HullMins(int hull);
  static const Vector& HullMaxs(int hull);
  int PointContents(const Vector& p, int hull = 0, int model = 0) const;
  // traces the world and the solid brush entities
  void Trace(int hull, const Vector& start, const Vector& end, BspTrace& tr, bool skyIsSolid) const;
  void TraceModel(int hull, int model, const Vector& start, const Vector& end, BspTrace& tr, bool skyIsSolid) const;
  // face under a hull 0 impact (texture lookup); -1 when unknown
  int FaceAtImpact(const BspTrace& tr) const;
  int FindFace(const Vector& p, const Vector& dir, float maxDist = 16.f) const;   // short hull 0 trace -> face
  // lightmap colour at a point on a face (0..1, style 0)
  Vector SampleFaceLight(int face, const Vector& p) const;
  // light for models: trace down to the floor and read its lightmap (GoldSrc R_LightPoint)
  bool LightPoint(const Vector& p, Vector& color) const;
  // texture name -> CS material char from sound/materials.txt ('C' concrete, 'M' metal, ...)
  char TextureMaterial(const std::string& texName) const;
  const BspTexture* FaceTexture(int face) const;

  void BreakEntity(int brushEnt);

private:
  bool RecursiveHullCheck(const BspClipNode* nodes, int headnode, int num, float p1f, float p2f, const Vector& p1, const Vector& p2, BspTrace& tr, bool skySolid) const;
  int HullPointContents(const BspClipNode* nodes, int num, const Vector& p) const;
  void LoadTextures(const uint8_t* lump, size_t len);
  void LoadMaterials();
  void SetupBrushEntities();
  std::map<std::string, char> m_materials;
};
inline CBspWorld g_Bsp;
