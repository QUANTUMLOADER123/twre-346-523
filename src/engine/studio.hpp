// studio.hpp - studio models: glTF loading (cgltf), skeleton, procedural posing, IK, hitboxes, CPU skinning + lighting
#pragma once
#include "mathlib.hpp"
#include "raylib.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <array>
#include <cstdint>

enum StudioOrient { STUDIO_ORIENT_CHARACTER = 0, STUDIO_ORIENT_WEAPON = 1 };

struct mstudionode_t {
  std::string name;
  int parent = -1;
  matrix3x4_t bindToModel;  // rigid bind frame in model space
  matrix3x4_t relToParent;  // inverse(parent bind) * bind
  Vector bindPos() const { return bindToModel.GetOrigin(); }
};

struct mstudioprim_t {
  std::vector<Vector> pos, nrm;                 // bind pose, model space
  std::vector<std::array<uint16_t, 4>> joints;  // node indices
  std::vector<std::array<float, 4>> weights;
  std::vector<uint32_t> indices;
  std::vector<uint32_t> armIndices;             // triangles dominated by arm/hand bones (first person arms)
  Color color{ 200, 200, 200, 255 };
  int node = -1;            // for rigid prims
  bool skinned = false;
  std::string matName;
};

// pose: rigid node->model transforms
struct CStudioPose { std::vector<matrix3x4_t> nodes; };
// a model-space rotation applied around the joint pivot (plus optional translation)
struct BoneDelta { bool active = false; matrix3x4_t rot; Vector translate; float stretch = 1.f; }; // stretch scales the offset from the parent joint

class CStudioModel {
public:
  bool Load(const std::string& path, int orient, float targetSize, float brighten = 1.f);
  void BuildProcedural(const std::string& name); // simple meshes made in code (knife, grenades, c4)
  void AddBox(const Vector& mins, const Vector& maxs, Color c); // procedural helpers (rigid)
  void AddWedge(const Vector& a, const Vector& b, const Vector& c, const Vector& d, float thick, Color col);
  int FindNode(const std::string& n) const { auto it = m_nameToNode.find(n); return it == m_nameToNode.end() ? -1 : it->second; }
  void BindPose(CStudioPose& out) const;
  void BuildPose(const std::vector<BoneDelta>& deltas, CStudioPose& out) const;
  void SolveTwoBoneIK(std::vector<BoneDelta>& deltas, int upper, int lower, int end, const Vector& target, const Vector& pole) const;
  std::string name;
  std::vector<mstudionode_t> nodes;
  std::vector<int> order; // topological
  std::vector<mstudioprim_t> prims;
  Vector mins, maxs;
  bool skinned = false, loaded = false;
  int numVerts = 0, numTris = 0;
private:
  std::unordered_map<std::string, int> m_nameToNode;
};

// rendering ------------------------------------------------------------------------------------
struct StudioLight { Vector ambient{ 0.5f, 0.5f, 0.5f }; Vector direct{ 0.6f, 0.6f, 0.6f }; Vector dir{ 0, 0, 1 }; };
enum StudioDrawFlags {
  STUDIO_FLAT_COLOR = 1,   // ignore lighting, use colorOverride (chams flat)
  STUDIO_TINT = 2,         // multiply material color by colorOverride
  STUDIO_NO_DEPTH = 4,     // draw through walls
  STUDIO_ARMS_ONLY = 8,    // only arm triangles (viewmodel)
  STUDIO_WIREFRAME = 16,
  STUDIO_GLOW = 32,        // rim/fresnel-ish highlight
  STUDIO_NO_CULL = 64,
};
struct StudioDrawParams {
  const CStudioPose* pose = nullptr;   // nullptr = bind pose / rigid
  matrix3x4_t modelToWorld;
  StudioLight light;
  int flags = 0;
  Color colorOverride{ 255, 255, 255, 255 };
  Vector eye;                           // for glow
};
void StudioRender_DrawModel(const CStudioModel& mdl, const StudioDrawParams& p);

// model cache
class CModelCache {
public:
  CStudioModel* Get(const std::string& path, int orient, float size, float brighten = 1.f);
  CStudioModel* GetProcedural(const std::string& name);
  void Clear() { for (auto& [k, v] : m_models) delete v; m_models.clear(); }
private:
  std::unordered_map<std::string, CStudioModel*> m_models;
};
inline CModelCache g_ModelCache;
inline CModelCache* modelcache = &g_ModelCache;

// helpers
matrix3x4_t RotationBetween(const Vector& from, const Vector& to);
matrix3x4_t AxisAngleMatrix(const Vector& axis, float deg);
inline matrix3x4_t IdentityMatrix() { return matrix3x4_t(); }
matrix3x4_t MatrixMul(const matrix3x4_t& a, const matrix3x4_t& b);
matrix3x4_t RigidInvert(const matrix3x4_t& m);
