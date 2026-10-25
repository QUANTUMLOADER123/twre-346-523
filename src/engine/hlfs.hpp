// hlfs.hpp - read-only access to the player's own Counter-Strike 1.6 / Half-Life install (GoldSrc content).
//
// Nothing from Valve is shipped with this game. If the player owns CS 1.6, its maps (BSP), textures (WAD),
// models (MDL), sprites and sounds are loaded straight from that folder:
//   1. hl_gamedir  - console variable, the "Half-Life" folder (contains cstrike/ and valve/)
//   2. ./cstrike, ./valve next to the game (a copied cstrike folder works too)
//   3. the usual Steam library locations
// Lookups are case-insensitive (GoldSrc content is full of mixed-case names) and cached.
#pragma once
#include "convar.hpp"
#include <string>
#include <vector>
#include <cstdint>

extern ConVar hl_gamedir;
extern ConVar hl_content;   // 0 = never use original content

namespace hlfs {
// game directories in priority order (cstrike_downloads, cstrike, valve for every install found)
const std::vector<std::string>& GameDirs();
// the first install that has cstrike/ ("" if none)
std::string InstallDir();
bool Available();                                  // any cstrike folder found (and hl_content 1)
// relative GoldSrc path ("maps/de_dust2.bsp") -> real path, "" if missing
std::string Find(const std::string& rel);
bool Read(const std::string& rel, std::vector<uint8_t>& out);
// case-insensitive lookup of an arbitrary path (used for absolute paths, too)
std::string ResolveCase(const std::string& path);
// list files in a GoldSrc directory with the given extension (".bsp"), names without extension, unique
std::vector<std::string> List(const std::string& relDir, const std::string& ext);
void Invalidate();                                 // forget caches (hl_gamedir changed)
std::string Lower(std::string s);
} // namespace hlfs
