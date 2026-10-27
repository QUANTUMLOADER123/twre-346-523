// mapinfo.hpp - map loading: parses assets/maps/*.map (ASCII cell layout) and compiles brushes
#pragma once
#include "../../engine/mathlib.hpp"
#include <string>
#include <vector>

enum MapZoneType { ZONE_BOMBSITE_A = 0, ZONE_BOMBSITE_B, ZONE_BUY_T, ZONE_BUY_CT };
struct MapSpawn { Vector origin; float yaw = 0; int team = 0; };
struct MapZone { Vector mins, maxs; int type = 0; bool Contains(const Vector& p) const { return p.x >= mins.x && p.x <= maxs.x && p.y >= mins.y && p.y <= maxs.y && p.z >= mins.z - 8 && p.z <= maxs.z; } Vector Center() const { return (mins + maxs) * 0.5f; } };
struct MapLight { Vector pos; Vector color{ 1, 0.85f, 0.6f }; float radius = 400; };
struct MapPlace { std::string name; Vector mins, maxs; }; // callout names ("Long A", "Mid"...)

struct CMapInfo {
  std::string name, title, mode = "bomb", file;
  std::string desc;
  float cell = 64.f, wallHeight = 320.f;
  Vector skyTop{ 0.36f, 0.56f, 0.82f }, skyHorizon{ 0.85f, 0.80f, 0.72f };
  Vector sunDir{ -0.4f, 0.3f, 0.85f }, sunColor{ 1.f, 0.93f, 0.8f }, ambient{ 0.40f, 0.40f, 0.42f };
  Vector fogColor{ 0.8f, 0.76f, 0.7f };
  bool snow = false;
  bool isBsp = false;            // original GoldSrc map from the CS 1.6 install (g_Bsp)
  std::vector<MapSpawn> spawns;
  std::vector<MapZone> zones;
  std::vector<MapLight> lights;
  std::vector<MapPlace> places;
  Vector mins, maxs;
  // walkability grid (used by nav generation & radar)
  int gw = 0, gh = 0; Vector gridOrigin;
  std::vector<float> floorZ;     // per cell walk height, NAN = blocked
  std::vector<uint8_t> cellType; // 0 void 1 floor 2 wall 3 ramp 4 obstacle
  std::vector<int> cellMat;      // floor/wall material (radar colors)

  bool Load(const std::string& path); // parse + compile into enginetrace (.bsp -> LoadBsp)
  bool LoadBsp(const std::string& path);
  bool LoadSrc(const std::string& mapName);   // Counter-Strike: Source map (g_SrcBsp), path "css:<name>"
  bool isSrc = false;            // CS:S map (also sets isBsp)
  const char* GetPlaceName(const Vector& p) const;
  const MapZone* FindZone(int type) const { for (auto& z : zones) if (z.type == type) return &z; return nullptr; }
  bool InZone(const Vector& p, int type) const { for (auto& z : zones) if (z.type == type && z.Contains(p)) return true; return false; }
  Vector CellCenter(int cx, int cy) const;
};
inline CMapInfo g_MapInfo;

struct MapListEntry { std::string name, title, mode, desc, file; };
std::vector<MapListEntry> ListMaps();
// "de_nuke_ve" -> "Nuke" (the display name of a map, both for the picker and the server list)
std::string PrettyMapName(const std::string& name);
// "de_dust2" -> maps/de_dust2.bsp from the CS 1.6 install when available (hl_content 1), else assets/maps/de_dust2.map
std::string ResolveMapFile(const std::string& name);
