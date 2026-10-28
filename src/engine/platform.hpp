// platform.hpp - per-OS startup (Android: unpack the APK assets and run from the app's files folder)
#pragma once
void Platform_PreInit();    // before anything touches files
void Platform_PostInit();   // after InitWindow
// Window size to request. Android: a ~720p-tall surface with the display's aspect ratio; the system scaler stretches
// it to the full screen (UI is laid out in pixels, native 1080p+ would make it tiny and slow). Desktop: unchanged.
void Platform_WindowSize(int& w, int& h);
