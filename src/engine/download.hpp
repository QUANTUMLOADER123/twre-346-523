// download.hpp - first-start content download (Counter-Strike: Source maps, models, materials, sounds).
//
// The game ships without any Valve files. When no CS:S content is found (no Steam CS:S / CS:S Dedicated Server,
// nothing in ./content/css), the loading screen downloads it once from GitHub mirrors of the CS:S content
// (zip archives straight from codeload.github.com) and unpacks it while downloading into content/css/cstrike/.
//
// HTTP: Android - java.net.HttpURLConnection through JNI, Windows - WinHTTP, others - curl.
#pragma once
#include <string>
#include <cstdint>

namespace download {
struct Pack {
  const char* id;        // marker file content/css/.pack_<id>
  const char* title;     // shown on the loading screen
  const char* url;
  double approxMB;       // the archives are streamed without Content-Length
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

bool ContentMissing();      // nothing usable installed (or an earlier download did not finish)
void Start();               // background thread; packs already finished are skipped
void Cancel();
Progress Get();
bool Busy();
void Shutdown();           // stop waiting for the worker (cancel + detach)
}
