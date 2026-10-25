// worldtest.cpp - headless world render test: worldtest <map> <out_prefix> [x y z pitch yaw]...
#include "../src/engine/convar.hpp"
#include "../src/engine/materialsystem.hpp"
#include "../src/engine/enginetrace.hpp"
#include "../src/engine/worldrender.hpp"
#include "../src/engine/view.hpp"
#include "../src/game/shared/mapinfo.hpp"
#include "rlgl.h"
#include <cstdio>
#include <cstdlib>
#include <string>
#include <chrono>

bool CheatsAllowed() { return true; }

static void Save(const char* path) {
  Image img = LoadImageFromScreen();
  ImageFlipVertical(&img);
  // software framebuffer comes back BGR
  unsigned char* p = (unsigned char*)img.data;
  if (img.format == PIXELFORMAT_UNCOMPRESSED_R8G8B8A8) for (int i = 0; i < img.width * img.height; i++) std::swap(p[i * 4], p[i * 4 + 2]);
  ExportImage(img, path); UnloadImage(img);
}

int main(int argc, char** argv) {
  if (argc < 3) { printf("usage: worldtest map out [x y z pitch yaw]...\n"); return 1; }
  SetTraceLogLevel(LOG_WARNING);
  InitWindow(960, 540, "worldtest");
  g_MaterialSystem.Init();
  if (!g_MapInfo.Load(std::string("assets/maps/") + argv[1] + ".map")) return 1;
  g_WorldRender.Build(g_MapInfo);
  int shot = 0;
  for (int i = 3; i + 4 < argc; i += 5) {
    CViewSetup v; v.width = 960; v.height = 540; v.origin = { (float)atof(argv[i]), (float)atof(argv[i + 1]), (float)atof(argv[i + 2]) };
    v.angles = { (float)atof(argv[i + 3]), (float)atof(argv[i + 4]), 0 }; v.Compute();
    auto t0 = std::chrono::steady_clock::now();
    BeginDrawing(); ClearBackground(BLACK);
    Render_Begin3D(v.view, v.proj);
    g_WorldRender.DrawSky(v, 0);
    g_WorldRender.DrawWorld(v);
    Render_End3D();
    DrawText(TextFormat("%s  %.0f %.0f %.0f", argv[1], v.origin.x, v.origin.y, v.origin.z), 10, 10, 20, WHITE);
    EndDrawing();
    float ms = std::chrono::duration<float, std::milli>(std::chrono::steady_clock::now() - t0).count();
    std::string out = std::string(argv[2]) + std::to_string(shot++) + ".png";
    Save(out.c_str());
    printf("%s: %d chunks drawn, %.1f ms\n", out.c_str(), g_WorldRender.drawnChunks, ms);
  }
  // spawns
  for (auto& s : g_MapInfo.spawns) { printf("spawn team %d at %.0f %.0f %.0f yaw %.0f\n", s.team, s.origin.x, s.origin.y, s.origin.z, s.yaw); break; }
  for (auto& z : g_MapInfo.zones) printf("zone %d %.0f %.0f -> %.0f %.0f\n", z.type, z.mins.x, z.mins.y, z.maxs.x, z.maxs.y);
  CloseWindow();
  return 0;
}
