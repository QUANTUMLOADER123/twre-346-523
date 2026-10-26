// host.hpp - CHost: listen server main loop (64 tick), map loading, server commands
#pragma once
#include "globals.hpp"
#include <string>

class CCSPlayer;

struct HostState {
  bool active = false;          // a map is loaded
  std::string mapname;
  double accumulator = 0;
  float frameAlpha = 0;         // render interpolation between ticks
  int ticksThisFrame = 0;
  bool paused = false;
  float timescale = 1.f;
  int lastLocalCmd = 0;
  float fps = 0;
  bool wantQuit = false;
};
inline HostState g_Host;

void Host_Init();
void Host_ExecAutoexec();   // cfg/autoexec.cfg, after the menu is up
bool Host_NewGame(const std::string& map);   // loads map, creates players
void Host_Disconnect();
void Host_Frame(float realDt);               // runs ticks
void Host_RunTick();
CCSPlayer* Host_CreateLocalPlayer();

// client hooks (implemented in the client dll part)
void CL_CreateMove(CUserCmd& cmd);           // input + cheat CreateMove
void CL_LevelInit();                         // build render data for the map
void CL_LevelShutdown();
void CL_OnTickEnd();
