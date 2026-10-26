// srcfs.cpp - loose folders + VPK archives + bsp pakfile (zip)
#include "srcfs.hpp"
#include "convar.hpp"
#include "raylib.h"
#include <filesystem>
#include <unordered_map>
#include <map>
#include <memory>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <algorithm>
#include <cctype>

ConVar css_gamedir("css_gamedir", "", FCVAR_ARCHIVE, "Counter-Strike: Source (or CS:S Dedicated Server) folder - empty = auto-detect (./content/css, Steam)",
                   false, 0, false, 0, [](ConVar*, const char*) { srcfs::Invalidate(); });
ConVar css_content("css_content", "1", FCVAR_ARCHIVE, "Use Counter-Strike: Source maps/models/materials/sounds when available",
                   false, 0, false, 0, [](ConVar*, const char*) { srcfs::Invalidate(); });
// A local Counter-Strike: Global Offensive install (free on Steam). Its pak01_dir.vpk holds the real CS:GO
// materials, models (agents), sounds and maps, so nothing has to be redistributed with this game.
ConVar csgo_gamedir("csgo_gamedir", "", FCVAR_ARCHIVE, "Counter-Strike: Global Offensive folder (the one with pak01_dir.vpk) - empty = auto-detect (./content/csgo, Steam)",
                    false, 0, false, 0, [](ConVar*, const char*) { srcfs::Invalidate(); });

namespace fs = std::filesystem;

namespace srcfs {
namespace {
std::string KV_LowerStr(const std::string& s) { std::string o = s; for (char& c : o) c = (char)std::tolower((unsigned char)c); return o; }
struct VpkEntry { uint16_t archive = 0; uint32_t offset = 0, length = 0; std::vector<uint8_t> preload; };
struct Vpk {
  std::string base;                 // ".../cstrike_pak" (without _dir.vpk)
  std::string dirFile; uint32_t dataStart = 0;   // archive 0x7fff = inside the dir file
  std::unordered_map<std::string, VpkEntry> files;
};
struct ZipEntry { uint32_t offset = 0, csize = 0, usize = 0; uint16_t method = 0; };

struct State {
  bool scanned = false; std::string root;
  std::string goRoot;                                      // CS:GO install (pak01_dir.vpk) - overrides CS:S content
  std::vector<std::unique_ptr<Vpk>> goVpks;
  std::vector<std::string> looseDirs;                      // in priority order
  std::unordered_map<std::string, std::string> loose;      // normalized rel -> real path
  std::vector<std::unique_ptr<Vpk>> vpks;
  std::vector<uint8_t> pak; std::unordered_map<std::string, ZipEntry> pakFiles;
};
State& S() { static State s; return s; }

bool ReadFile(const std::string& p, std::vector<uint8_t>& out, size_t off = 0, size_t len = (size_t)-1) {
  FILE* f = fopen(p.c_str(), "rb"); if (!f) return false;
  if (len == (size_t)-1) { fseek(f, 0, SEEK_END); long n = ftell(f); if (n < 0) { fclose(f); return false; } len = (size_t)n - off; }
  out.resize(len);
  bool ok = fseek(f, (long)off, SEEK_SET) == 0 && fread(out.data(), 1, len, f) == len;
  fclose(f); return ok;
}

std::string ReadZ(const std::vector<uint8_t>& d, size_t& p) {
  std::string s; while (p < d.size() && d[p]) s += (char)d[p++]; p++; return s;
}

bool LoadVpk(const std::string& dirFile, Vpk& v) {
  std::vector<uint8_t> d; if (!ReadFile(dirFile, d) || d.size() < 12) return false;
  uint32_t sig, ver, treeSize; std::memcpy(&sig, &d[0], 4); std::memcpy(&ver, &d[4], 4); std::memcpy(&treeSize, &d[8], 4);
  if (sig != 0x55aa1234 || (ver != 1 && ver != 2)) return false;
  size_t hdr = ver == 1 ? 12 : 28, p = hdr;
  v.dirFile = dirFile; v.dataStart = (uint32_t)(hdr + treeSize);
  v.base = dirFile.substr(0, dirFile.size() - 8);     // strip "_dir.vpk"
  while (p < d.size()) {
    std::string ext = ReadZ(d, p); if (ext.empty()) break;
    for (;;) {
      std::string path = ReadZ(d, p); if (path.empty()) break;
      for (;;) {
        std::string name = ReadZ(d, p); if (name.empty()) break;
        if (p + 18 > d.size()) return !v.files.empty();
        VpkEntry e; uint16_t preload;
        std::memcpy(&preload, &d[p + 4], 2); std::memcpy(&e.archive, &d[p + 6], 2); std::memcpy(&e.offset, &d[p + 8], 4); std::memcpy(&e.length, &d[p + 12], 4);
        p += 18;
        if (preload) { if (p + preload > d.size()) return !v.files.empty(); e.preload.assign(d.begin() + p, d.begin() + p + preload); p += preload; }
        std::string rel = (path == " " ? "" : path + "/") + name + (ext == " " ? "" : "." + ext);
        v.files[Normalize(rel)] = std::move(e);
      }
    }
  }
  return true;
}

void IndexLoose(const std::string& dir) {
  State& s = S(); std::error_code ec;
  if (!fs::is_directory(dir, ec)) return;
  s.looseDirs.push_back(dir);
  // "svg_normal" holds the downloaded CS:GO weapon icons (content/extras/csgo_icons/svg_normal)
  static const char* subs[] = { "maps", "materials", "models", "sound", "scripts", "resource", "particles", "svg_normal", "svg_outline" };
  for (const char* sub : subs) {
    fs::path sp = fs::path(dir) / sub;
    // tolerate any case of the sub folder name
    if (!fs::is_directory(sp, ec)) {
      bool found = false;
      for (auto& e : fs::directory_iterator(dir, ec)) if (e.is_directory(ec) && KV_LowerStr(e.path().filename().string()) == sub) { sp = e.path(); found = true; break; }
      if (!found) continue;
    }
    for (auto it = fs::recursive_directory_iterator(sp, fs::directory_options::follow_directory_symlink, ec); it != fs::recursive_directory_iterator(); it.increment(ec)) {
      if (ec) break;
      if (!it->is_regular_file(ec)) continue;
      std::string rel = it->path().lexically_relative(dir).generic_string();   // lexical: symlinked folders keep their names
      std::string key = Normalize(rel);
      if (!s.loose.count(key)) s.loose[key] = it->path().string();
    }
  }
}

bool LooksLikeCsgoRoot(const std::string& r) {
  std::error_code ec;
  return fs::exists(fs::path(r) / "pak01_dir.vpk", ec) || fs::exists(fs::path(r) / "csgo" / "pak01_dir.vpk", ec) ||
         fs::exists(fs::path(r) / "game" / "csgo" / "pak01_dir.vpk", ec);
}
// the folder that actually holds pak01_dir.vpk
std::string CsgoContentDir(const std::string& r) {
  std::error_code ec;
  if (fs::exists(fs::path(r) / "pak01_dir.vpk", ec)) return r;
  if (fs::exists(fs::path(r) / "csgo" / "pak01_dir.vpk", ec)) return (fs::path(r) / "csgo").string();
  if (fs::exists(fs::path(r) / "game" / "csgo" / "pak01_dir.vpk", ec)) return (fs::path(r) / "game" / "csgo").string();
  return "";
}

bool LooksLikeRoot(const std::string& r) {
  std::error_code ec;
  return fs::exists(fs::path(r) / "cstrike" / "cstrike_pak_dir.vpk", ec) || fs::is_directory(fs::path(r) / "cstrike" / "maps", ec) ||
         fs::is_directory(fs::path(r) / "cstrike" / "materials", ec);
}

void Scan() {
  State& s = S();
  if (s.scanned) return;
  s.scanned = true; s.root.clear(); s.looseDirs.clear(); s.loose.clear(); s.vpks.clear();
  if (!css_content.GetBool()) return;
  // 1) a local CS:GO install first: it is the content this game is built around now
  {
    std::vector<std::string> goCands;
    std::string gu = csgo_gamedir.GetString();
    if (!gu.empty()) goCands.push_back(gu);
    goCands.push_back(".");
    goCands.push_back("content/csgo");
    goCands.push_back("../content/csgo");
    goCands.push_back("csgo");
    goCands.push_back("game/csgo");
#ifdef _WIN32
    for (const char* drive : { "C:", "D:", "E:" }) {
      for (const char* steam : { "/Program Files (x86)/Steam", "/Program Files/Steam", "/Steam", "/SteamLibrary" }) {
        goCands.push_back(std::string(drive) + steam + "/steamapps/common/Counter-Strike Global Offensive/game/csgo");
        goCands.push_back(std::string(drive) + steam + "/steamapps/common/Counter-Strike Global Offensive/csgo");
        goCands.push_back(std::string(drive) + steam + "/steamapps/common/Counter-Strike Global Offensive Dedicated Server/csgo");
        goCands.push_back(std::string(drive) + steam + "/steamapps/common/Counter-Strike Global Offensive Dedicated Server");
      }
    }
#else
    if (const char* home = getenv("HOME")) {
      goCands.push_back(std::string(home) + "/.steam/steam/steamapps/common/Counter-Strike Global Offensive/game/csgo");
      goCands.push_back(std::string(home) + "/.local/share/Steam/steamapps/common/Counter-Strike Global Offensive/game/csgo");
      goCands.push_back(std::string(home) + "/Steam/steamapps/common/Counter-Strike Global Offensive/game/csgo");
      goCands.push_back(std::string(home) + "/.steam/steam/steamapps/common/Counter-Strike Global Offensive Dedicated Server/csgo");
      goCands.push_back(std::string(home) + "/.local/share/Steam/steamapps/common/Counter-Strike Global Offensive Dedicated Server/csgo");
    }
#endif
    for (auto& c : goCands) {
      if (c.empty() || !LooksLikeCsgoRoot(c)) continue;
      std::string dir = CsgoContentDir(c);
      if (dir.empty()) continue;
      auto v = std::make_unique<Vpk>();
      if (LoadVpk(dir + "/pak01_dir.vpk", *v) && !v->files.empty()) { s.goRoot = dir; s.goVpks.push_back(std::move(v)); }
      else s.goRoot = dir;   // at least the loose folders
      break;
    }
    if (!s.goRoot.empty()) {
      std::error_code gec;
      if (fs::is_directory(s.goRoot, gec)) IndexLoose(s.goRoot);
    }
  }
  std::vector<std::string> cands;
  std::string user = css_gamedir.GetString();
  if (!user.empty()) {
    cands.push_back(user);
    std::error_code ec;
    // the user may point at the cstrike folder itself
    if (fs::path(user).filename() == "cstrike" || fs::exists(fs::path(user) / "cstrike_pak_dir.vpk", ec)) cands.insert(cands.begin(), fs::path(user).parent_path().string());
  }
  cands.push_back(DefaultInstallDir());
  cands.push_back("content/css");
  cands.push_back("../content/css");
#ifdef _WIN32
  for (const char* drive : { "C:", "D:", "E:" }) {
    for (const char* steam : { "/Program Files (x86)/Steam", "/Program Files/Steam", "/Steam", "/SteamLibrary" }) {
      cands.push_back(std::string(drive) + steam + "/steamapps/common/Counter-Strike Source");
      cands.push_back(std::string(drive) + steam + "/steamapps/common/Counter-Strike Source Dedicated Server");
    }
  }
#else
  if (const char* home = getenv("HOME")) {
    cands.push_back(std::string(home) + "/.steam/steam/steamapps/common/Counter-Strike Source");
    cands.push_back(std::string(home) + "/.local/share/Steam/steamapps/common/Counter-Strike Source");
    cands.push_back(std::string(home) + "/Steam/steamapps/common/Counter-Strike Source Dedicated Server");
  }
#endif
  for (auto& c : cands) if (!c.empty() && LooksLikeRoot(c)) { s.root = c; break; }
  // downloaded extras (content/extras/<name>/{maps,materials,models,sound}) - the CS:GO maps, the CS:GO weapon
  // models and the weapon icons. Indexed even when no Source install is found, so a CS:GO-only setup works.
  std::error_code exc;
  for (const char* ex : { "content/extras", "../content/extras", "assets/extras" })
    if (fs::is_directory(ex, exc))
      for (auto& e : fs::directory_iterator(ex, exc)) if (e.is_directory(exc)) IndexLoose(e.path().string());
  if (!s.goRoot.empty()) Msg("CS:GO content: %s (%d files in pak01)\n", s.goRoot.c_str(), (int)s.goVpks.size() ? (int)s.goVpks[0]->files.size() : 0);
  if (s.root.empty()) {
    static bool hinted = false;
    if (!hinted && s.loose.empty()) { hinted = true; Msg("No CS:GO content found - run csgo_download (free CS:GO Dedicated Server) or set csgo_gamedir; the maps are downloaded automatically\n"); }
    if (!s.loose.empty()) Msg("Downloaded content: %d files\n", (int)s.loose.size());
    return;
  }
  std::string cs = s.root + "/cstrike";
  IndexLoose(cs + "/download");
  std::error_code ec;
  if (fs::is_directory(cs + "/custom", ec))
    for (auto& e : fs::directory_iterator(cs + "/custom", ec)) {
      if (e.is_directory(ec)) IndexLoose(e.path().string());
      else if (e.path().extension() == ".vpk" && e.path().string().size() > 8 && e.path().string().find("_dir.vpk") != std::string::npos) {
        auto v = std::make_unique<Vpk>(); if (LoadVpk(e.path().string(), *v)) s.vpks.push_back(std::move(v));
      }
    }
  IndexLoose(cs);
  for (const char* f : { "/cstrike/cstrike_pak_dir.vpk", "/hl2/hl2_textures_dir.vpk", "/hl2/hl2_misc_dir.vpk", "/hl2/hl2_sound_misc_dir.vpk",
                         "/hl2/hl2_sound_vo_english_dir.vpk", "/platform/platform_misc_dir.vpk" }) {
    auto v = std::make_unique<Vpk>(); if (LoadVpk(s.root + f, *v)) s.vpks.push_back(std::move(v));
  }
  IndexLoose(s.root + "/hl2");
  size_t nv = 0; for (auto& v : s.vpks) nv += v->files.size();
  Msg("Source content: %s (%d loose files, %d VPK files in %d archives)\n", s.root.c_str(), (int)s.loose.size(), (int)nv, (int)s.vpks.size());
}

bool ReadPak(const std::string& key, std::vector<uint8_t>& out) {
  State& s = S(); auto it = s.pakFiles.find(key); if (it == s.pakFiles.end()) return false;
  const ZipEntry& z = it->second;
  if ((size_t)z.offset + 30 > s.pak.size()) return false;
  uint16_t nl, xl; std::memcpy(&nl, &s.pak[z.offset + 26], 2); std::memcpy(&xl, &s.pak[z.offset + 28], 2);
  size_t data = (size_t)z.offset + 30 + nl + xl;
  if (data + z.csize > s.pak.size()) return false;
  if (z.method == 0) { out.assign(s.pak.begin() + data, s.pak.begin() + data + z.csize); return true; }
  if (z.method == 8) {
    int outSize = 0;
    unsigned char* d = DecompressData(&s.pak[data], (int)z.csize, &outSize);
    if (!d) return false;
    out.assign(d, d + outSize); MemFree(d); return true;
  }
  return false;   // LZMA packed pakfiles (newer games) are not supported
}
} // namespace

std::string Normalize(const std::string& rel) {
  std::string o; o.reserve(rel.size());
  for (char c : rel) { if (c == '\\') c = '/'; o += (char)std::tolower((unsigned char)c); }
  while (o.rfind("./", 0) == 0) o.erase(0, 2);
  while (!o.empty() && o[0] == '/') o.erase(0, 1);
  // collapse "//"
  std::string r; r.reserve(o.size());
  for (size_t i = 0; i < o.size(); i++) { if (o[i] == '/' && !r.empty() && r.back() == '/') continue; r += o[i]; }
  return r;
}

bool Available() { Scan(); return !S().root.empty(); }
bool CsgoAvailable() { Scan(); return !S().goRoot.empty(); }
const std::string& CsgoRoot() { Scan(); return S().goRoot; }
const std::string& Root() { Scan(); return S().root; }
void Invalidate() { S().scanned = false; }

bool Read(const std::string& rel, std::vector<uint8_t>& out) {
  Scan(); State& s = S();
  std::string key = Normalize(rel);
  if (!s.pakFiles.empty() && ReadPak(key, out)) return true;
  auto readVpk = [&](std::vector<std::unique_ptr<Vpk>>& list) {
    for (auto& v : list) {
      auto f = v->files.find(key); if (f == v->files.end()) continue;
      const VpkEntry& e = f->second;
      out = e.preload;
      if (e.length) {
        std::vector<uint8_t> d; bool ok;
        if (e.archive == 0x7fff) ok = ReadFile(v->dirFile, d, v->dataStart + e.offset, e.length);
        else { char num[16]; snprintf(num, sizeof num, "_%03d.vpk", e.archive); ok = ReadFile(v->base + num, d, e.offset, e.length); }
        if (!ok) continue;
        out.insert(out.end(), d.begin(), d.end());
      }
      return true;
    }
    return false;
  };
  if (readVpk(s.goVpks)) return true;
  auto it = s.loose.find(key);
  if (it != s.loose.end() && ReadFile(it->second, out)) return true;
  return readVpk(s.vpks);
}

bool Exists(const std::string& rel) {
  Scan(); State& s = S(); std::string key = Normalize(rel);
  if (s.pakFiles.count(key) || s.loose.count(key)) return true;
  for (auto& v : s.goVpks) if (v->files.count(key)) return true;
  for (auto& v : s.vpks) if (v->files.count(key)) return true;
  return false;
}

std::string LoosePath(const std::string& rel) { Scan(); auto it = S().loose.find(Normalize(rel)); return it == S().loose.end() ? "" : it->second; }

std::vector<std::string> List(const std::string& relDir, const std::string& ext) {
  Scan(); State& s = S();
  std::string dir = Normalize(relDir); if (!dir.empty() && dir.back() != '/') dir += '/';
  std::string e = Normalize(ext);
  std::vector<std::string> out;
  auto consider = [&](const std::string& key) {
    if (key.rfind(dir, 0) != 0 || key.size() <= dir.size() + e.size()) return;
    if (key.compare(key.size() - e.size(), e.size(), e) != 0) return;
    std::string stem = key.substr(dir.size(), key.size() - dir.size() - e.size());
    if (stem.find('/') != std::string::npos) return;
    out.push_back(stem);
  };
  for (auto& [k, v] : s.loose) consider(k);
  for (auto& v : s.goVpks) for (auto& [k, f] : v->files) consider(k);
  for (auto& v : s.vpks) for (auto& [k, f] : v->files) consider(k);
  std::sort(out.begin(), out.end()); out.erase(std::unique(out.begin(), out.end()), out.end());
  return out;
}

void MountPak(std::vector<uint8_t> zip) {
  State& s = S(); s.pak = std::move(zip); s.pakFiles.clear();
  const auto& d = s.pak; if (d.size() < 22) return;
  // end of central directory
  size_t eocd = std::string::npos;
  for (size_t i = d.size() - 22 + 1; i-- > 0 && d.size() - i < 70000;) if (d[i] == 0x50 && d[i + 1] == 0x4b && d[i + 2] == 5 && d[i + 3] == 6) { eocd = i; break; }
  if (eocd == std::string::npos) return;
  uint16_t count; uint32_t cdOff; std::memcpy(&count, &d[eocd + 10], 2); std::memcpy(&cdOff, &d[eocd + 16], 4);
  size_t p = cdOff;
  for (int i = 0; i < count && p + 46 <= d.size(); i++) {
    uint32_t sig; std::memcpy(&sig, &d[p], 4); if (sig != 0x02014b50) break;
    ZipEntry z; uint16_t nl, xl, cl;
    std::memcpy(&z.method, &d[p + 10], 2); std::memcpy(&z.csize, &d[p + 20], 4); std::memcpy(&z.usize, &d[p + 24], 4);
    std::memcpy(&nl, &d[p + 28], 2); std::memcpy(&xl, &d[p + 30], 2); std::memcpy(&cl, &d[p + 32], 2); std::memcpy(&z.offset, &d[p + 42], 4);
    if (p + 46 + nl > d.size()) break;
    std::string name((const char*)&d[p + 46], nl);
    s.pakFiles[Normalize(name)] = z;
    p += 46 + nl + xl + cl;
  }
}
void UnmountPak() { State& s = S(); s.pak.clear(); s.pak.shrink_to_fit(); s.pakFiles.clear(); }

std::string DefaultInstallDir() { return "content/css"; }
} // namespace srcfs

// ------------------------------------------------------------------------------------------------
// console: download (Valve SteamCMD, anonymous) / rescan
namespace {
std::string FindTool(const char* name) {
  std::error_code ec;
  for (const char* dir : { "tools/", "../tools/" }) { std::string p = std::string(dir) + name; if (fs::exists(p, ec)) return fs::absolute(p, ec).string(); }
  return "";
}
}
static ConCommand css_download("css_download", [](const CCommand&) {
#if defined(PLATFORM_ANDROID)
  std::error_code ec;
  Msg("SteamCMD does not run on Android. Download the content on a PC (tools/get_css_content.bat), then copy the\n"
      "content/css folder (it contains cstrike/ and hl2/) to the phone into:\n  %s/content/css\n"
      "and type css_rescan (or restart the game).\n", fs::current_path(ec).string().c_str());
#else
#if defined(_WIN32)
  std::string tool = FindTool("get_css_content.bat");
  if (tool.empty()) { Warning("tools/get_css_content.bat not found\n"); return; }
  std::string cmd = "start \"CS:S content download\" cmd /c \"\"" + tool + "\"\"";
#else
  std::string tool = FindTool("get_css_content.sh");
  if (tool.empty()) { Warning("tools/get_css_content.sh not found\n"); return; }
  std::string cmd = "\"" + tool + "\" > content_download.log 2>&1 &";
#endif
  Msg("Downloading Counter-Strike: Source Dedicated Server content with Valve's SteamCMD (anonymous login, ~2 GB).\n"
      "When it is done type css_rescan (or restart the game) - maps, models, textures and sounds are then used automatically.\n");
  if (std::system(cmd.c_str()) != 0) Warning("could not start %s\n", tool.c_str());
#endif
}, "Download Counter-Strike: Source content (free CS:S Dedicated Server via SteamCMD) into content/css");
static ConCommand csgo_download("csgo_download", [](const CCommand&) {
#if defined(PLATFORM_ANDROID)
  std::error_code ec;
  Msg("SteamCMD does not run on Android. Download the CS:GO content on a PC (tools/get_csgo_content.bat), then copy the\n"
      "csgo folder (it contains pak01_dir.vpk) to the phone into:\n  %s/content/csgo\n"
      "and type csgo_rescan (or restart the game).\n", fs::current_path(ec).string().c_str());
#else
#if defined(_WIN32)
  std::string tool = FindTool("get_csgo_content.bat");
  if (tool.empty()) { Warning("tools/get_csgo_content.bat not found\n"); return; }
  std::string cmd = "start \"CS:GO content download\" cmd /c \"\"" + tool + "\"\"";
#else
  std::string tool = FindTool("get_csgo_content.sh");
  if (tool.empty()) { Warning("tools/get_csgo_content.sh not found\n"); return; }
  std::string cmd = "\"" + tool + "\" > content_download.log 2>&1 &";
#endif
  Msg("Downloading the free Counter-Strike: Global Offensive Dedicated Server with Valve's SteamCMD (anonymous login, ~2 GB).\n"
      "This is the real CS:GO content: agents, weapons, textures, sounds and every stock map.\n"
      "When it is done type csgo_rescan (or restart the game).\n");
  if (std::system(cmd.c_str()) != 0) Warning("could not start %s\n", tool.c_str());
#endif
}, "Download the real CS:GO content (free CS:GO Dedicated Server via SteamCMD) into content/csgo");
static ConCommand csgo_rescan("csgo_rescan", [](const CCommand&) {
  srcfs::Invalidate();
  if (srcfs::CsgoAvailable()) Msg("CS:GO content: %s - reload the map\n", srcfs::CsgoRoot().c_str());
  else Msg("no CS:GO content found (set csgo_gamedir or run csgo_download)\n");
}, "Look for CS:GO content again (after csgo_download finished)");

static ConCommand css_rescan("css_rescan", [](const CCommand&) {
  srcfs::Invalidate();
  if (srcfs::Available()) Msg("Source content: %s - reload the map (or snd_restart for sounds)\n", srcfs::Root().c_str());
}, "Look for Counter-Strike: Source content again (after css_download finished)");

