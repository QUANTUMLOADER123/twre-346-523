// modeltest.cpp - headless studio model test
#include "../src/engine/convar.hpp"
#include "../src/engine/materialsystem.hpp"
#include "../src/engine/studio.hpp"
#include "../src/engine/view.hpp"
#include "../src/game/shared/cs_playeranimstate.hpp"
#include "../src/game/shared/weapon_parse.hpp"
#include "rlgl.h"
#include <cstdio>
#include <string>

bool CheatsAllowed() { return true; }
static void Save(const char* path) {
  Image img = LoadImageFromScreen(); ImageFlipVertical(&img);
  unsigned char* p = (unsigned char*)img.data;
  for (int i = 0; i < img.width * img.height; i++) std::swap(p[i * 4], p[i * 4 + 2]);
  ExportImage(img, path); UnloadImage(img);
}
int main(int argc, char** argv) {
  SetTraceLogLevel(LOG_WARNING);
  InitWindow(960, 540, "modeltest");
  FindConVar("developer")->SetValue(1);
  g_MaterialSystem.Init();
  const char* chars[3] = { "assets/models/player/ct_swat.glb", "assets/models/player/t_guerilla.glb", "assets/models/player/t_punk.glb" };
  CStudioModel* m[3]; for (int i = 0; i < 3; i++) m[i] = g_ModelCache.Get(chars[i], STUDIO_ORIENT_CHARACTER, 72.f);
  CStudioModel* ak = g_ModelCache.Get("assets/models/weapons/w_ak47.glb", STUDIO_ORIENT_WEAPON, 34.f, 1.6f);
  CStudioModel* dg = g_ModelCache.Get("assets/models/weapons/w_deagle.glb", STUDIO_ORIENT_WEAPON, 10.f, 1.6f);
  int ul = m[0]->FindNode("UpperArm.L"), hd = m[0]->FindNode("Head"), ft = m[0]->FindNode("Foot.L");
  printf("UpperArm.L %.1f %.1f %.1f  Head %.1f %.1f %.1f  Foot.L %.1f %.1f %.1f\n", m[0]->nodes[ul].bindPos().x, m[0]->nodes[ul].bindPos().y, m[0]->nodes[ul].bindPos().z,
    m[0]->nodes[hd].bindPos().x, m[0]->nodes[hd].bindPos().y, m[0]->nodes[hd].bindPos().z, m[0]->nodes[ft].bindPos().x, m[0]->nodes[ft].bindPos().y, m[0]->nodes[ft].bindPos().z);

  for (int view = 0; view < 3; view++) {
    CViewSetup v; v.width = 960; v.height = 540; v.fov = 70;
    if (view == 0) { v.origin = Vector(150, -60, 50); v.angles = { 5, 160, 0 }; }
    else if (view == 1) { v.origin = Vector(30, 150, 50); v.angles = { 5, -100, 0 }; }
    else { v.origin = Vector(-120, -40, 60); v.angles = { 10, 15, 0 }; }
    v.Compute();
    BeginDrawing(); ClearBackground({ 90, 100, 110, 255 });
    Render_Begin3D(v.view, v.proj);
    struct Cfg { int mdl; int wpn; float duck, speed, cycle, death; float pitch, relyaw; bool ground; };
    Cfg cfg[5] = { { 0, WEAPON_AK47, 0, 0, 0, -1, 0, 0, true }, { 1, WEAPON_DEAGLE, 1, 0, 0, -1, 10, 0, true }, { 2, WEAPON_AWP, 0, 1, 0.3f, -1, -20, 30, true },
                   { 0, WEAPON_M4A1, 0, 1, 0.8f, -1, 0, -45, false }, { 1, WEAPON_GLOCK, 0, 0, 0, 1, 0, 0, true } };
    for (int i = 0; i < 5; i++) {
      PlayerPoseInput in; in.origin = Vector(0, (i - 2) * 45.f, 0); in.footYaw = 0; in.eyeAngles = { cfg[i].pitch, cfg[i].relyaw, 0 };
      in.duckAmount = cfg[i].duck; in.moveSpeed = cfg[i].speed; in.moveCycle = cfg[i].cycle; in.onGround = cfg[i].ground; in.weaponId = cfg[i].wpn; in.deathCycle = cfg[i].death; in.deathYaw = 180;
      const CCSWeaponInfo* wi = GetWeaponInfo(cfg[i].wpn);
      CStudioModel* wm = g_ModelCache.Get(wi->modelPath, STUDIO_ORIENT_WEAPON, wi->modelLength, 1.6f);
      PlayerPoseOutput out; BuildPlayerPose(*m[cfg[i].mdl], wm, in, out);
      StudioDrawParams p; p.pose = &out.pose; p.modelToWorld = out.modelToWorld;
      p.light.dir = Vector(0.5f, 0.3f, 0.8f); p.light.ambient = { 0.55f, 0.55f, 0.6f }; p.light.direct = { 0.6f, 0.55f, 0.5f };
      StudioRender_DrawModel(*m[cfg[i].mdl], p);
      if (out.hasWeapon) { StudioDrawParams g = p; g.pose = nullptr; g.modelToWorld = out.weaponToWorld; StudioRender_DrawModel(*wm, g); }
      if (view == 2) { // hitboxes
        rlBegin(RL_LINES); rlColor4ub(255, 255, 0, 255);
        for (auto& h : out.hitboxes) { rlVertex3f(h.a.x, h.a.y, h.a.z); rlVertex3f(h.b.x, h.b.y, h.b.z); }
        rlEnd();
      }
    }
    Render_End3D();
    EndDrawing();
    Save(TextFormat("/tmp/wt/pose%d.png", view));
  }
  CloseWindow();
}
