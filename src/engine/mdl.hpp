// mdl.hpp - GoldSrc studio models (MDL v10) from the player's CS 1.6 install.
//
// Bones, sequences with RLE-compressed animations (seqgroup 0 plus external "<name>01.mdl" groups), body parts,
// 8-bit paletted textures (also from "<name>T.mdl"), attachments. Vertices are skinned on the CPU every frame
// (one bone per vertex, like GoldSrc) and drawn through rlgl. Used for the first-person view models (v_*.mdl).
#pragma once
#include "mathlib.hpp"
#include "raylib.h"
#include <string>
#include <vector>
#include <memory>
#include <cstdint>

struct StudioLight;

class CGoldSrcModel {
public:
  struct Bone { std::string name; int parent = -1; float value[6] = {}; float scale[6] = {}; };
  struct Sequence {
    std::string name; float fps = 30; int flags = 0; int activity = 0; int numFrames = 1;
    int numBlends = 1; int animOffset = 0; int group = 0;
  };
  struct Attachment { int bone = 0; Vector org; };
  struct Texture { std::string name; int flags = 0, w = 0, h = 0; Texture2D tex{}; };
  struct Tri { int vert, norm; float s, t; };
  struct Mesh { int skinref = 0; std::vector<Tri> tris; };    // triangle list (strips / fans expanded)
  struct SubModel { std::vector<Vector> verts, norms; std::vector<uint8_t> vertBone, normBone; std::vector<Mesh> meshes; };
  struct BodyPart { std::string name; int base = 1; std::vector<SubModel> models; };

  bool Load(const std::string& realPath);
  void Unload();
  bool loaded = false;
  std::string name;
  std::vector<Bone> bones;
  std::vector<Sequence> seqs;
  std::vector<Attachment> attachments;
  std::vector<Texture> textures;
  std::vector<short> skins;   // [family][skinref]
  int numSkinRef = 0;
  std::vector<BodyPart> bodyparts;

  int FindSequence(const char* name) const;                // exact, case-insensitive (-1)
  // bone matrices (model space) for a sequence at a frame (fractional)
  void SetupBones(int seq, float frame, std::vector<matrix3x4_t>& out) const;
  float SequenceDuration(int seq) const { return seq >= 0 && seq < (int)seqs.size() ? std::max(1, seqs[seq].numFrames - 1) / std::max(1.f, seqs[seq].fps) : 0.f; }
  bool SequenceLoops(int seq) const { return seq >= 0 && seq < (int)seqs.size() && (seqs[seq].flags & 1); }
  // draws with the given bones; body = GoldSrc body index, light from the world
  void Draw(const std::vector<matrix3x4_t>& boneToWorld, const matrix3x4_t& modelToWorld, const StudioLight& light, int body = 0, int skin = 0) const;
  Vector AttachmentPos(int idx, const std::vector<matrix3x4_t>& bonesModel, const matrix3x4_t& modelToWorld) const;

private:
  std::vector<uint8_t> m_data;                   // main file
  std::vector<std::vector<uint8_t>> m_groups;    // seqgroup files (index 0 unused)
  void CalcBone(const uint8_t* animBase, int bone, float frame, int numFrames, Vector& pos, Quat& q) const;
};

// cache: "models/v_ak47.mdl" -> loaded model from the CS 1.6 install (nullptr when missing)
CGoldSrcModel* GoldSrc_GetModel(const std::string& rel);
void GoldSrc_FlushModels();
