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
#include <android/configuration.h>
#include <jni.h>
#include <algorithm>
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
#include <cstdio>

extern "C" struct android_app* GetAndroidApp(void);
// raylib's optional APK fopen wrapper (-Wl,--wrap=fopen) calls __real_fopen. We don't wrap fopen (the data is
// unpacked to the files folder instead), so __real_fopen is just fopen.
extern "C" FILE* __real_fopen(const char* name, const char* mode) { return fopen(name, mode); }
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

static constexpr int kAndroidRenderHeight = 600;   // like CS at 800x600; the system scaler stretches it to the screen

// Real display size in pixels via JNI: activity.getWindowManager().getDefaultDisplay().getRealMetrics(dm)
static bool RealDisplaySize(int& w, int& h) {
  android_app* app = GetAndroidApp();
  if (!app || !app->activity || !app->activity->vm) return false;
  JavaVM* vm = app->activity->vm; JNIEnv* env = nullptr;
  if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK || !env) return false;
  bool ok = false;
  jobject act = app->activity->clazz;
  jclass actCls = env->GetObjectClass(act);
  jmethodID getWM = env->GetMethodID(actCls, "getWindowManager", "()Landroid/view/WindowManager;");
  jobject wm = getWM ? env->CallObjectMethod(act, getWM) : nullptr;
  jclass wmCls = env->FindClass("android/view/WindowManager");
  jmethodID getDD = (wm && wmCls) ? env->GetMethodID(wmCls, "getDefaultDisplay", "()Landroid/view/Display;") : nullptr;
  jobject disp = getDD ? env->CallObjectMethod(wm, getDD) : nullptr;
  jclass dmCls = env->FindClass("android/util/DisplayMetrics");
  jclass dCls = env->FindClass("android/view/Display");
  if (disp && dmCls && dCls && !env->ExceptionCheck()) {
    jobject dm = env->NewObject(dmCls, env->GetMethodID(dmCls, "<init>", "()V"));
    jmethodID getReal = env->GetMethodID(dCls, "getRealMetrics", "(Landroid/util/DisplayMetrics;)V");
    if (dm && getReal) {
      env->CallVoidMethod(disp, getReal, dm);
      if (!env->ExceptionCheck()) {
        w = env->GetIntField(dm, env->GetFieldID(dmCls, "widthPixels", "I"));
        h = env->GetIntField(dm, env->GetFieldID(dmCls, "heightPixels", "I"));
        ok = w > 0 && h > 0;
      }
    }
  }
  if (env->ExceptionCheck()) env->ExceptionClear();
  vm->DetachCurrentThread();
  return ok;
}

void Platform_WindowSize(int& w, int& h) {
  float longPx = 0, shortPx = 0;
  int rw = 0, rh = 0;
  if (RealDisplaySize(rw, rh)) { longPx = (float)std::max(rw, rh); shortPx = (float)std::min(rw, rh); }
  else if (android_app* app = GetAndroidApp(); app && app->config) {
    int wdp = AConfiguration_getScreenWidthDp(app->config), hdp = AConfiguration_getScreenHeightDp(app->config);
    int dens = AConfiguration_getDensity(app->config);
    if (dens <= 0 || dens == ACONFIGURATION_DENSITY_ANY || dens == ACONFIGURATION_DENSITY_NONE) dens = 160;
    if (wdp > 0 && hdp > 0) { longPx = std::max(wdp, hdp) * dens / 160.f; shortPx = std::min(wdp, hdp) * dens / 160.f; }
  }
  if (shortPx <= 0 || kAndroidRenderHeight >= shortPx) w = h = 0;   // unknown or small display: native size
  else { h = kAndroidRenderHeight; w = (int)(h * longPx / shortPx + 0.5f) & ~1; }
  __android_log_print(ANDROID_LOG_INFO, "cshvh", "display %.0fx%.0f px, rendering %dx%d (0 = native)", longPx, shortPx, w, h);
}
#else
void Platform_PreInit() {}
void Platform_PostInit() {}
void Platform_WindowSize(int&, int&) {}
#endif
