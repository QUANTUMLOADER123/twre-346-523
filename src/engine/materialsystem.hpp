// materialsystem.hpp - procedural world materials (IMaterialSystem-ish)
#pragma once
#include <string>
#include <vector>
#include <cstdint>
#include "raylib.h"

enum MaterialId {
  MAT_NONE = 0,
  MAT_SAND_FLOOR, MAT_SAND_FLOOR2, MAT_SANDSTONE_WALL, MAT_SANDSTONE_BLOCKS, MAT_SANDSTONE_TRIM, MAT_PLASTER_WHITE, MAT_PLASTER_YELLOW,
  MAT_PLASTER_BLUE, MAT_BRICK_RED, MAT_CONCRETE, MAT_CONCRETE_DARK, MAT_TILE_FLOOR, MAT_TILE_TERRACOTTA, MAT_COBBLE, MAT_ASPHALT,
  MAT_WOOD_PLANKS, MAT_WOOD_DOOR, MAT_CRATE, MAT_CRATE_DARK, MAT_METAL, MAT_METAL_DOOR, MAT_METAL_RUST, MAT_ROOF_TILES,
  MAT_DEV_GRAY, MAT_DEV_ORANGE, MAT_DEV_BLUE, MAT_ICE, MAT_SNOW, MAT_GRASS, MAT_BARREL, MAT_STONE_DARK,
  MAT_COUNT
};

struct MaterialInfo {
  const char* name;
  int surfaceProp;   // SurfaceProp for bullets / footsteps
  float texWorldSize; // world units covered by one texture repeat
  Texture2D tex{};
  Color avg{ 128, 128, 128, 255 }; // average color (radar, impacts)
};

// static per-material data available without a GL context (map compiler, server)
struct MaterialDef { const char* shortName; int surfaceProp; };
const MaterialDef& GetMaterialDef(int id);
int MaterialFromName(const std::string& shortName); // e.g. "sand_floor" -> MAT_SAND_FLOOR

class CMaterialSystem {
public:
  void Init();          // generate all textures (needs a GL context)
  void Shutdown();
  MaterialInfo& Get(int id) { return m_mats[(id > 0 && id < MAT_COUNT) ? id : MAT_DEV_GRAY]; }
  int FindByName(const std::string& n) const;
  Texture2D white{};
  Texture2D smoke{}, spark{}, flare{}, decalHole{}, glowDot{}, cloud{};
  bool initialized = false;
private:
  MaterialInfo m_mats[MAT_COUNT];
};
inline CMaterialSystem g_MaterialSystem;
inline CMaterialSystem* materials = &g_MaterialSystem;
