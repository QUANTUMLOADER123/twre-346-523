#include "hlfs.hpp"
#include "convar.hpp"
#include <filesystem>
#include <fstream>
#include <unordered_map>
#include <algorithm>
#include <cstdlib>
#include <mutex>

namespace fs = std::filesystem;

static void OnGameDirChanged(ConVar*, const char*) { hlfs::Invalidate(); }
ConVar hl_gamedir("hl_gamedir", "", FCVAR_ARCHIVE, "Your Counter-Strike 1.6 install (the 'Half-Life' folder with cstrike/ and valve/). Original maps, models and sounds are loaded from there", false, 0, false, 0, OnGameDirChanged);
ConVar hl_content("hl_content", "1", FCVAR_ARCHIVE, "Use original CS 1.6 content (maps, models, sounds) when an install is found", true, 0, true, 1, OnGameDirChanged);

namespace hlfs {
std::string Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }
namespace {
std::recursive_mutex g_mx;
bool g_valid = false;
std::vector<std::string> g_dirs;
std::string g_install;
std::unordered_map<std::string, std::string> g_cache;

std::string Slashes(std::string s) { for (char& c : s) if (c == '\\') c = '/'; return s; }

void Build() {
  if (g_valid) return;
  g_valid = true; g_dirs.clear(); g_install.clear(); g_cache.clear();
  if (!hl_content.GetBool()) return;
  std::vector<std::string> installs;
  auto add = [&](std::string d) {
    if (d.empty()) return;
    d = Slashes(d); while (d.size() > 1 && d.back() == '/') d.pop_back();
    // pointing at the cstrike folder itself is fine too
    std::string base = Lower(fs::path(d).filename().string());
    if (base == "cstrike" || base == "cstrike_downloads" || base == "valve") d = fs::path(d).parent_path().string();
    if (d.empty()) d = ".";
    for (auto& i : installs) if (i == d) return;
    installs.push_back(d);
  };
  add(hl_gamedir.GetString());
  if (ConVar* s = FindConVar("snd_gamedir")) add(s->GetString());
  add(".");   // ./cstrike next to the game
  add("C:/Program Files (x86)/Steam/steamapps/common/Half-Life");
  add("C:/Program Files/Steam/steamapps/common/Half-Life");
  add("D:/SteamLibrary/steamapps/common/Half-Life");
  add("E:/SteamLibrary/steamapps/common/Half-Life");
  add("D:/Steam/steamapps/common/Half-Life");
  add("C:/Games/Counter-Strike 1.6");
  add("D:/Games/Counter-Strike 1.6");
  if (const char* h = std::getenv("HOME")) { add(std::string(h) + "/.steam/steam/steamapps/common/Half-Life"); add(std::string(h) + "/.local/share/Steam/steamapps/common/Half-Life"); }
  std::error_code ec;
  std::vector<std::string> seen;   // canonical cstrike paths (the same install can be reached through different spellings)
  for (auto& inst : installs) {
    std::string cs = ResolveCase(inst + "/cstrike");
    if (cs.empty() || !fs::is_directory(cs, ec)) continue;
    std::string canon = fs::weakly_canonical(cs, ec).string();
    if (std::find(seen.begin(), seen.end(), canon) != seen.end()) continue;
    seen.push_back(canon);
    if (g_install.empty()) g_install = inst;
    for (const char* sub : { "/cstrike_downloads", "/cstrike", "/valve" }) {
      std::string d = ResolveCase(inst + sub);
      if (!d.empty() && fs::is_directory(d, ec)) g_dirs.push_back(d);
    }
  }
  if (!g_dirs.empty()) {
    Msg("CS 1.6 content: %s\n", g_install.c_str());
  }
}
} // namespace

std::string ResolveCase(const std::string& pathIn) {
  std::string path = Slashes(pathIn);
  std::error_code ec;
  if (fs::exists(path, ec)) return path;
  // walk the components, matching each one case-insensitively
  fs::path p(path);
  fs::path cur = p.has_root_path() ? p.root_path() : fs::path(".");
  bool first = true;
  for (const auto& comp : p.relative_path()) {
    std::string c = comp.string();
    if (c.empty() || c == ".") continue;
    fs::path tryp = (first && !p.has_root_path()) ? fs::path(c) : cur / c;
    if (fs::exists(tryp, ec)) { cur = tryp; first = false; continue; }
    std::string lc = Lower(c); bool found = false;
    fs::path dir = (first && !p.has_root_path()) ? fs::path(".") : cur;
    for (auto& de : fs::directory_iterator(dir, ec)) {
      if (Lower(de.path().filename().string()) == lc) { cur = (first && !p.has_root_path()) ? de.path().filename() : de.path(); found = true; break; }
    }
    if (!found) return "";
    first = false;
  }
  return cur.string();
}

const std::vector<std::string>& GameDirs() { std::lock_guard<std::recursive_mutex> l(g_mx); Build(); return g_dirs; }
std::string InstallDir() { std::lock_guard<std::recursive_mutex> l(g_mx); Build(); return g_install; }
bool Available() { return !GameDirs().empty(); }
void Invalidate() { std::lock_guard<std::recursive_mutex> l(g_mx); g_valid = false; g_cache.clear(); }

std::string Find(const std::string& relIn) {
  std::lock_guard<std::recursive_mutex> l(g_mx);
  Build();
  if (g_dirs.empty()) return "";
  std::string rel = Slashes(relIn);
  while (!rel.empty() && rel[0] == '/') rel.erase(0, 1);
  std::string key = Lower(rel);
  auto it = g_cache.find(key); if (it != g_cache.end()) return it->second;
  std::string found;
  for (auto& d : g_dirs) { std::string r = ResolveCase(d + "/" + rel); if (!r.empty()) { found = r; break; } }
  g_cache[key] = found;
  return found;
}

bool Read(const std::string& rel, std::vector<uint8_t>& out) {
  std::string p = Find(rel); if (p.empty()) return false;
  std::ifstream f(p, std::ios::binary); if (!f) return false;
  f.seekg(0, std::ios::end); size_t n = (size_t)f.tellg(); f.seekg(0);
  out.resize(n); f.read((char*)out.data(), (std::streamsize)n);
  return (bool)f || f.eof();
}

std::vector<std::string> List(const std::string& relDir, const std::string& ext) {
  std::vector<std::string> out; std::error_code ec;
  std::string le = Lower(ext);
  for (auto& d : GameDirs()) {
    std::string dir = ResolveCase(d + "/" + relDir); if (dir.empty()) continue;
    for (auto& de : fs::directory_iterator(dir, ec)) {
      if (Lower(de.path().extension().string()) != le) continue;
      std::string n = Lower(de.path().stem().string());
      if (std::find(out.begin(), out.end(), n) == out.end()) out.push_back(n);
    }
  }
  std::sort(out.begin(), out.end());
  return out;
}
} // namespace hlfs

static ConCommand cmd_hl_find("hl_find", [](const CCommand& c) {
  if (c.ArgC() < 2) { Msg("usage: hl_find <path>   (e.g. hl_find maps/de_dust2.bsp)\n"); return; }
  if (!hlfs::Available()) { Msg("no CS 1.6 install found (set hl_gamedir)\n"); return; }
  std::string p = hlfs::Find(c.Arg(1));
  Msg("%s -> %s\n", c.Arg(1), p.empty() ? "(not found)" : p.c_str());
}, "Locate a file in the CS 1.6 install");
static ConCommand cmd_hl_path("hl_path", [](const CCommand&) {
  const auto& d = hlfs::GameDirs();
  if (d.empty()) { Msg("no CS 1.6 install found (set hl_gamedir)\n"); return; }
  for (auto& s : d) Msg("  %s\n", s.c_str());
}, "Show the CS 1.6 content search path");
