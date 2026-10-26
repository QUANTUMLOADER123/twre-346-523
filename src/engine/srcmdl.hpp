// srcmdl.hpp - Source engine studio models (MDL v44-v48 + VVD + VTX), Counter-Strike: Source content.
//
// Geometry of LOD 0 (bodygroup 0 of every body part), materials via $cdmaterials + skin families, the bone
// hierarchy and the sequence / animation data (RLE compressed Source animations) so the same loader serves static
// props, viewmodels and player models.
#pragma once
#include <unordered_map>
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
  bool looping = false, delta = false;
  int paramIndex[2] = { -1, -1 };        // pose parameter of each blend axis
  float paramStart[2] = { 0, 0 }, paramEnd[2] = { 1, 1 };
  std::vector<float> weights;            // per bone layer weight ($weightlist)
};
struct SrcMdlPoseParam { std::string name; int flags = 0; float start = 0, end = 1, loop = 0; };
struct SrcMdlAttachment { std::string name; int bone = 0; matrix3x4_t local; };
struct SrcMdlHitbox { int bone = 0, group = 0; Vector mins, maxs; };

class CSrcModel;
struct SrcSeqRef {                       // a sequence of this model or of one of its $includemodel libraries
  const CSrcModel* src = nullptr; int seq = -1; int include = -1;   // include: index into includes (-1 = own)
  explicit operator bool() const { return src && seq >= 0; }
};
struct SrcPoseValues {                   // pose parameter values by name (degrees / -1..1, as in the model)
  float move_x = 0, move_y = 0, move_yaw = 0, body_yaw = 0, body_pitch = 0;
  float Get(const std::string& n) const;
};

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
  std::vector<SrcMdlPoseParam> poseParams;
  std::vector<std::string> includeModels;  // $includemodel (animation libraries, e.g. cs_player_shared.mdl)
  std::vector<uint8_t> data;             // the .mdl file (animation data is decoded on demand)
  std::vector<const CSrcModel*> includes;            // loaded $includemodel libraries
  std::vector<std::vector<int>> includeBoneMap;      // [include][our bone] -> bone of the include (-1)

  SrcSeqRef FindSeq(const std::string& label) const;                 // own sequences first, then includes
  std::vector<SrcSeqRef> FindSeqsByActivity(const std::string& act) const;
  float SeqFps(const SrcSeqRef& r) const;
  int SeqFrames(const SrcSeqRef& r) const;
  float SeqDuration(const SrcSeqRef& r) const { int n = SeqFrames(r); return n > 1 ? (n - 1) / std::max(1.f, SeqFps(r)) : 0.f; }
  // sample a sequence (cycle 0..1, blend grid from pose values) into OUR skeleton's local transforms.
  // delta sequences give identity for bones they don't touch. weights (per our bone) = the sequence's $weightlist
  void SampleSeq(const SrcSeqRef& r, float cycle, const SrcPoseValues& pv, std::vector<Vector>& pos, std::vector<Quat>& q, std::vector<float>* weights = nullptr) const;
  // bone-to-model matrices -> skinning matrices (bone * poseToBone)
  void SkinMatrices(const std::vector<matrix3x4_t>& bones, const matrix3x4_t& modelToWorld, std::vector<matrix3x4_t>& out) const;
  Vector AttachmentPos(int idx, const std::vector<matrix3x4_t>& bones, const matrix3x4_t& modelToWorld) const;

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
  // per-frame lookups are cached: material of each texture slot (by value, invalidated by SrcMat_Clear),
  // lower-case sequence / bone name -> index
  struct MatCache { Texture2D tex{}; bool translucent = false, alphatest = false, valid = false; };
  mutable std::vector<MatCache> m_matCache; mutable int m_matGen = -1;
  mutable std::unordered_map<std::string, int> m_seqIndex, m_boneIndex;   // query string (as passed) -> index
  int FindNamed(const std::string& q, bool bone) const;
  void ExtractAnimBone(const uint8_t* animBase, int bone, int frame, float t, Vector& pos, Quat& q, int animFlags) const;
};

// draw a skinned model: bones = bone-to-model matrices (BuildBones), params like StudioRender_DrawModel
struct StudioDrawParams;
// boneMask (optional): only triangles whose vertices are skinned to bones with a nonzero mask are drawn (c_arms style
// view model hands cut out of a player model)
void SrcMdl_Draw(const CSrcModel& m, const std::vector<matrix3x4_t>& bones, const matrix3x4_t& modelToWorld, const StudioDrawParams& p, int skin = 0, const std::vector<uint8_t>* boneMask = nullptr);

// cache (cleared on map change)
CSrcModel* SrcMdl_Get(const std::string& path);
void SrcMdl_Clear();
