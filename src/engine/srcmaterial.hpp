// srcmaterial.hpp - Source materials (.vmt) and textures (.vtf) from the CS:S content (see srcfs.hpp)
#pragma once
#include "mathlib.hpp"
#include "raylib.h"
#include <string>
#include <vector>
#include <cstdint>

// decode a .vtf to RGBA8 (largest mip that fits maxDim, frame 0, face 0)
bool VTF_Decode(const std::vector<uint8_t>& file, std::vector<uint8_t>& rgba, int& w, int& h, int maxDim = 4096, int* fullW = nullptr, int* fullH = nullptr);
bool VTF_Load(const std::string& rel, std::vector<uint8_t>& rgba, int& w, int& h, int maxDim = 4096);   // "materials/x.vtf" via srcfs

struct SrcMaterial {
  std::string name, shader;
  bool ok = false;                 // vmt found
  Texture2D tex{}, tex2{};         // $basetexture, $basetexture2 (WorldVertexTransition blends with vertex alpha)
  int width = 64, height = 64;     // full resolution of $basetexture (texture coordinates are in these texels)
  bool translucent = false, alphatest = false, additive = false, nocull = false, vertexalpha = false;
  bool nodraw = false, sky = false, water = false, tool = false, unlit = false;
  float alpha = 1.f, alphaRef = 0.5f;
  Vector color{ 1, 1, 1 };
  Vector reflectivity{ 0.5f, 0.5f, 0.5f };
  std::string surfaceprop;         // "concrete", "metal", ...
};

// "de_dust/stonewall02" (no materials/ prefix, no .vmt). Cached until SrcMat_Clear().
const SrcMaterial& SrcMat_Find(const std::string& name, bool model = false);
// model materials: tries every $cdmaterials directory
const SrcMaterial& SrcMat_FindInDirs(const std::string& name, const std::vector<std::string>& dirs);
void SrcMat_Clear();
int SrcMat_Generation();                 // changes on every SrcMat_Clear (for caches of material lookups)
// surfaceprop name -> engine surface (SURFACE_*)
int SrcMat_SurfaceFromProp(const std::string& prop, const std::string& matName);
