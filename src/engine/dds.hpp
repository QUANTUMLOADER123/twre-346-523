// dds.hpp - DirectDraw Surface reader (the format of the CS:GO / CS:S overview radars).
// CS:GO stores every map preview as resource/overviews/<map>_radar.dds inside the map itself (DXT1/DXT5),
// so the map picker and the radar both need this before anything else can decode them.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace dds {
// rgba = RGBA8, top-down rows. Uncompressed 24/32 bit, DXT1, DXT3 and DXT5 are supported.
// blackIsTransparent: the overview radars paint the "outside the map" area solid black -
// turn those pixels transparent so the radar/menu background shows through.
bool Decode(const uint8_t* d, size_t size, std::vector<uint8_t>& rgba, int& w, int& h, bool blackIsTransparent = false);
// reads "resource/overviews/<map>_radar.dds" style paths through srcfs
bool Load(const std::string& rel, std::vector<uint8_t>& rgba, int& w, int& h, bool blackIsTransparent = false);
} // namespace dds
