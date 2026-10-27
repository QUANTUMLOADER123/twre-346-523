// mapoverview.hpp - the real overview data of a Source map: resource/overviews/<map>.txt (pos_x, pos_y, scale,
// rotate) and resource/overviews/<map>_radar.dds. Both live *inside* the map file (its pakfile zip), so this
// also works for the map picker, before any map is loaded.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mapoverview {
struct Info {
  float posX = 0, posY = 0, scale = 0;   // world = pos + pixel * scale
  bool rotated = false;
  bool scriptFound = false;              // the overview .txt was there (scale/pos are real, not guessed)
};
// rgba (RGBA8, black background already transparent), image size and the overview script values.
bool Load(const std::string& map, std::vector<uint8_t>& rgba, int& w, int& h, Info& info);
// true when the map has an overview at all (cheap: does not decode the image)
bool Has(const std::string& map);
} // namespace mapoverview
