// srcfs.hpp - Source engine file system for Counter-Strike: Source content.
//
// Content comes from a Counter-Strike: Source (or CS:S Dedicated Server) folder - the dedicated server is free and
// is CS:GO first (free Dedicated Server, tools/get_csgo_content.* / the "csgo_download" command), then CS:S.
// Nothing from Valve is shipped with this game.
//
// Search order (first hit wins), like the real engine:
//   1. the current map's pakfile lump (zip inside the .bsp)
//   2. loose files:  <root>/cstrike/{download,custom/*,.}
//   3. VPKs:         <csgo root>/pak01_dir.vpk (a local CS:GO install), <root>/cstrike/cstrike_pak_dir.vpk, <root>/hl2/hl2_*_dir.vpk, <root>/platform/platform_misc_dir.vpk
// <root> is css_gamedir, or auto-detected (./content/css, Steam default folders).
#pragma once
#include <string>
#include <vector>
#include <cstdint>

class ConVar;
extern ConVar css_gamedir;
extern ConVar css_content;
extern ConVar csgo_gamedir;

namespace srcfs {
bool Available();                                   // a CS:S content root was found (and css_content is 1)
bool CsgoAvailable();                               // a local CS:GO install (pak01_dir.vpk) was found - it wins over CS:S
bool AnyContent();                                  // any real content at all (CS:GO or CS:S) - use this to gate assets
const std::string& CsgoRoot();                      // CS:GO content folder ("" if none)
const std::string& Root();                          // detected root folder ("" if none)
void Invalidate();                                  // re-scan on next access (after css_gamedir changes / download)
bool Read(const std::string& rel, std::vector<uint8_t>& out);   // "materials/de_dust/stone.vtf" (any case, / or \)
bool Exists(const std::string& rel);
std::string LoosePath(const std::string& rel);      // real path of a loose file ("" if only in a VPK / missing)
std::vector<std::string> List(const std::string& relDir, const std::string& ext);   // file stems in a directory
// map pakfile
void MountPak(std::vector<uint8_t> zip);
int PakFileCount();                                  // files inside the mounted bsp pakfile (diagnostics)
void UnmountPak();
std::string Normalize(const std::string& rel);      // lower case, forward slashes, no leading slash, no "./"
// where SteamCMD installs the dedicated server by default
std::string DefaultInstallDir();
}
