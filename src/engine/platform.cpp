// platform.cpp - per-OS startup glue
//
// Android: the game reads its data with std::ifstream / std::filesystem, which cannot see inside the APK.
// On first start (and after every update) the files listed in assets/apk_files.txt are copied from the
// APK to the app's external files folder:
//     /sdcard/Android/data/com.cshvh.game/files/
// and the game runs from there (cfg/ and screenshots land there too). Put Counter-Strike: Source content
// into  .../files/content/css/  (cstrike/ + hl2/ copied from a PC) to play the real CS:S maps.
#include "platform.hpp"
#include "convar.hpp"

#if defined(PLATFORM_ANDROID)
#include <android/asset_manager.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/window.h>
#include <android_native_app_glue.h>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unistd.h>

extern "C" struct android_app* GetAndroidApp(void);
namespace fs = std::filesystem;

static bool ReadAsset(AAssetManager* am, const std::string& name, std::string& out) {
  AAsset* a = AAssetManager_open(am, name.c_str(), AASSET_MODE_STREAMING);
  if (!a) return false;
  out.clear(); char buf[65536]; int n;
  while ((n = AAsset_read(a, buf, sizeof buf)) > 0) out.append(buf, n);
  AAsset_close(a);
  return true;
}

void Platform_PreInit() {
  android_app* app = GetAndroidApp();
  if (!app || !app->activity) return;
  std::string root = app->activity->externalDataPath ? app->activity->externalDataPath : "";
  std::error_code ec;
  if (root.empty() || (!fs::exists(root, ec) && !fs::create_directories(root, ec))) root = app->activity->internalDataPath;
  fs::create_directories(root, ec);
  AAssetManager* am = app->activity->assetManager;
  std::string list;
  if (ReadAsset(am, "apk_files.txt", list)) {
    // first line: build id. Re-extract when it changes (game update)
    std::istringstream ss(list); std::string version; std::getline(ss, version);
    std::string have; { std::ifstream f(root + "/.apk_version"); std::getline(f, have); }
    if (have != version) {
      __android_log_print(ANDROID_LOG_INFO, "cshvh", "extracting assets (%s) to %s", version.c_str(), root.c_str());
      std::string line, data; int n = 0;
      while (std::getline(ss, line)) {
        if (line.empty()) continue;
        if (!ReadAsset(am, line, data)) { __android_log_print(ANDROID_LOG_WARN, "cshvh", "missing asset %s", line.c_str()); continue; }
        fs::path dst = fs::path(root) / line;
        fs::create_directories(dst.parent_path(), ec);
        std::ofstream o(dst, std::ios::binary); o.write(data.data(), (std::streamsize)data.size()); n++;
      }
      std::ofstream(root + "/.apk_version") << version << "\n";
      __android_log_print(ANDROID_LOG_INFO, "cshvh", "extracted %d files", n);
    }
  }
  fs::create_directories(fs::path(root) / "cfg", ec);
  fs::create_directories(fs::path(root) / "content", ec);
  if (chdir(root.c_str()) != 0) __android_log_print(ANDROID_LOG_ERROR, "cshvh", "chdir %s failed", root.c_str());
}

void Platform_PostInit() {
  android_app* app = GetAndroidApp();
  if (app && app->activity) ANativeActivity_setWindowFlags(app->activity, AWINDOW_FLAG_KEEP_SCREEN_ON | AWINDOW_FLAG_FULLSCREEN, 0);
}
#else
void Platform_PreInit() {}
void Platform_PostInit() {}
#endif
