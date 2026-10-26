// srcmdl.hpp - Source engine studio models (MDL v44-v48 + VVD + VTX), Counter-Strike: Source content.
//
// Geometry of LOD 0 (bodygroup 0 of every body part), materials via $cdmaterials + skin families, the bone
// hierarchy and the sequence / animation data (RLE compressed Source animations) so the same loader serves static
// props, viewmodels and player models.
#pragma once
#include "mathlib.hpp"
#include "raylib.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct SrcMdlMesh {
  int skinRef = 0;                       // index into the skin table (-> texture)
  std::vector<float> pos, normal, uv;    // reference pose, model space
  std::vector<uint8_t> bone;             // 3 per vertex
  std::vector<float> weight;             // 3 per vertex
  std::vector<uint16_t> idx;             // triangle list (counter-clockwise seen from the front)
  Mesh gpu{}; bool uploaded = false;
};

struct SrcMdlBone {
  std::string name; int parent = -1;
  Vector pos; Quat quat; Vector rot;   // reference pose (local)
  Vector posScale, rotScale;
  matrix3x4_t poseToBone;
};

struct SrcMdlAnim {                      // one animation (mstudioanimdesc_t)
  std::string name; float fps = 30; int flags = 0; int numFrames = 1;
  int animOffset = 0;                    // absolute offset of the first mstudioanim_t in the file (block 0)
  int sectionFrames = 0; int sectionOffset = 0; int descOffset = 0;
  bool valid = false;
};
struct SrcMdlSeq {
  std::string label, activity; int actWeight = 0; int flags = 0;
  std::vector<int> anims;                // blend grid (we use the first entry)
  int groupSize[2] = { 1, 1 };
  float fadeIn = 0.2f, fadeOut = 0.2f;
  bool looping = false;
};
struct SrcMdlAttachment { std::string name; int bone = 0; matrix3x4_t local; };
struct SrcMdlHitbox { int bone = 0, group = 0; Vector mins, maxs; };

class CSrcModel {
public:
  bool Load(const std::string& path);    // "models/weapons/v_rif_ak47.mdl"
  void Unload();
  std::string name;
  bool loaded = false;
  int version = 0;
  Vector hullMin, hullMax, eyePos;
  std::vector<std::string> cdmaterials, textures;
  std::vector<std::vector<int16_t>> skins;   // [family][skinref] -> texture
  std::vector<SrcMdlMesh> meshes;
  std::vector<SrcMdlBone> bones;
  std::vector<SrcMdlAnim> anims;
  std::vector<SrcMdlSeq> seqs;
  std::vector<SrcMdlAttachment> attachments;
  std::vector<SrcMdlHitbox> hitboxes;
  std::vector<uint8_t> data;             // the .mdl file (animation data is decoded on demand)

  // texture of a mesh for a skin family
  Texture2D MeshTexture(const SrcMdlMesh& m, int skin = 0, bool* translucent = nullptr, bool* alphatest = nullptr) const;
  int FindSequence(const std::string& label) const;      // case-insensitive, -1
  int FindBone(const std::string& name) const;
  // local bone transforms of an animation frame (pos + quaternion per bone)
  void SampleAnim(int anim, float frame, std::vector<Vector>& pos, std::vector<Quat>& q) const;
  // full pose -> bone-to-model matrices
  void BuildBones(const std::vector<Vector>& pos, const std::vector<Quat>& q, std::vector<matrix3x4_t>& out) const;
  void UploadStatic();                   // GPU meshes in the reference pose
private:
  void ExtractAnimBone(const uint8_t* animBase, int bone, int frame, float t, Vector& pos, Quat& q, int animFlags) const;
};

// cache (cleared on map change)
CSrcModel* SrcMdl_Get(const std::string& path);
void SrcMdl_Clear();
