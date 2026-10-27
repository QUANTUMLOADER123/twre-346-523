// mapoverview.cpp - CS:GO / CS:S overview radars straight out of the map file.
#include "mapoverview.hpp"
#include "dds.hpp"
#include "keyvalues.hpp"
#include "srcfs.hpp"
#include <algorithm>

namespace mapoverview {
namespace {
// every place a map's inner file can be: the mounted pakfile (map loaded) or the map's bsp zip on disk
bool ReadInner(const std::string& map, const std::string& entry, std::vector<uint8_t>& out) {
  if (srcfs::Read(entry, out)) return true;
  std::string bsp = srcfs::LoosePath("maps/" + map + ".bsp");
  if (bsp.empty()) return false;
  return srcfs::ReadZipEntryFile(bsp, entry, out);
}

bool DecodeData(const std::vector<uint8_t>& d, std::vector<uint8_t>& rgba, int& w, int& h) {
  if (d.size() >= 128 && d[0] == 'D' && d[1] == 'D' && d[2] == 'S' && d[3] == ' ') return dds::Decode(d.data(), d.size(), rgba, w, h, true);
  // fallback: an uncompressed BMP/tga radar shipped by a few custom maps (raylib handles those)
  return false;
}
} // namespace

bool Load(const std::string& map, std::vector<uint8_t>& rgba, int& w, int& h, Info& info) {
  info = Info();
  std::vector<uint8_t> d;
  if (!ReadInner(map, "resource/overviews/" + map + "_radar.dds", d) || !DecodeData(d, rgba, w, h)) return false;
  std::vector<uint8_t> txt;
  if (ReadInner(map, "resource/overviews/" + map + ".txt", txt) && !txt.empty()) {
    auto kv = KeyValues::Parse((const char*)txt.data(), txt.size());
    const KeyValues* root = kv && !kv->children.empty() ? kv->children[0].get() : nullptr;
    if (root) {
      info.posX = root->GetFloat("pos_x"); info.posY = root->GetFloat("pos_y"); info.scale = root->GetFloat("scale");
      info.rotated = root->GetInt("rotate") != 0;
      info.scriptFound = info.scale > 0;
    }
  }
  return true;
}

bool Has(const std::string& map) {
  std::vector<uint8_t> d;
  return ReadInner(map, "resource/overviews/" + map + "_radar.dds", d);
}
} // namespace mapoverview
