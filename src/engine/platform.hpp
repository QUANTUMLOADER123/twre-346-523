// platform.hpp - per-OS startup (Android: unpack the APK assets and run from the app's files folder)
#pragma once
void Platform_PreInit();    // before anything touches files
void Platform_PostInit();   // after InitWindow
