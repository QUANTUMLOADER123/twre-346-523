// download.hpp - first-start content download (CS:GO maps, models, textures, sounds).
//
// Nothing from Valve is shipped with the game. The loading screen downloads, once:
//   * CS:GO maps (real VBSP v21 .bsp files from public CS:GO FastDL mirrors, one file each) into
//     content/extras/csgo_maps/maps/ - together with the materials the maps carry in their own pakfile lump;
//   * the shared Source assets (player / weapon models, textures, sounds) as a zip from codeload.github.com
//     into content/css/cstrike/ - the open source texture library the maps borrow from;
//   * a few CS:GO extras (R8 Revolver, SCAR-20 / SSG 08, molotov, weapon icons).
//
// Users who own CS:GO (free on Steam) can skip all of it: csgo_gamedir points at the CS:GO folder and the engine
// reads the real CS:GO materials, models, sounds and maps straight out of pak01_dir.vpk (see srcfs).
//
// HTTP: Android - java.net.HttpURLConnection through JNI, Windows - WinHTTP, others - curl.
#pragma once
#include <string>
#include <cstdint>

namespace download {
struct Pack {
  const char* id;        // marker file <dest>/.pack_<id>
  const char* title;     // shown on the loading screen
  const char* url;       // zip archive - or, with `files`, the base url of single files
  double approxMB;       // the archives are streamed without Content-Length
  const char* const* files = nullptr;   // single files (nullptr-terminated, relative to url) instead of a zip
  const char* dest = nullptr;           // folder of single-file packs (zip packs unpack into content/css/cstrike)
  bool core = true;      // part of the CS:S content (not needed with a Steam / user install)
  const char* api = nullptr;            // single-file packs: fallback url template (%s = file), GitHub contents API
};
const Pack* Packs(int& count);

enum State { IDLE, RUNNING, DONE, FAILED, CANCELLED };
struct Progress {
  State state = IDLE;
  int pack = 0, packCount = 0;     // current pack (0-based)
  double packMB = 0, packTotalMB = 0;   // downloaded / expected of the current pack
  double totalMB = 0, totalExpectedMB = 0;
  int files = 0;                   // files written so far
  double speedMBs = 0;
  std::string current;             // file being unpacked
  std::string error;
};

bool ContentMissing();      // nothing usable installed (or an earlier download did not finish), or an extra is missing
bool CoreContentMissing();  // the CS:S content itself is missing (the extras are optional)
void Start();               // background thread; packs already finished are skipped
void Cancel();
Progress Get();
bool Busy();
void Shutdown();           // stop waiting for the worker (cancel + detach)
}
