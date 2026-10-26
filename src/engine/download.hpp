// download.hpp - first-start content download (Counter-Strike: Source maps, models, materials, sounds).
//
// The game ships without any Valve files. When no CS:S content is found (no Steam CS:S / CS:S Dedicated Server,
// nothing in ./content/css), the loading screen downloads it once from GitHub mirrors of the CS:S content
// (zip archives straight from codeload.github.com) and unpacks it while downloading into content/css/cstrike/.
//
// Extras (any install): the CS:GO R8 Revolver (view / world model, textures, sounds) from a Garry's Mod addon on GitHub,
// as single files into content/extras/csgo_r8/ (indexed by srcfs like a cstrike/custom folder).
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
