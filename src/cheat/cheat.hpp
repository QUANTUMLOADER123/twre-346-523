// cheat.hpp - internal "HvH" cheat interface (hooks called by the client)
#pragma once
#include <vector>
#include "../engine/globals.hpp"
#include "../engine/view.hpp"
#include "../engine/studio.hpp"

class CCSPlayer;
struct PlayerPoseOutput;

namespace cheat {
void Init();
void Shutdown();
void LevelInit();
void CreateMove(CUserCmd& cmd);                 // ClientModeShared::CreateMove hook
void OverrideView(CViewSetup& view);            // thirdperson / fov
void FrameStageNotify();                        // FRAME_RENDER_START: resolver + animation fix for remote players
// DrawModelExecute hook: return true if the cheat already drew the player (chams replace the material)
bool DrawPlayer(CCSPlayer* p, const PlayerPoseOutput& pose, const StudioLight& light, const CViewSetup& view);
void DrawWorld3D(const CViewSetup& view);       // grenade prediction, bullet tracers, etc (after entities)
void Paint();                                   // PaintTraverse: ESP, indicators, watermark
void PaintMenu();                               // the menu itself (on top of everything)
bool IsMenuOpen();
void ToggleMenu();
bool WantsMouse();                              // menu captures the mouse
bool IsKeyBindActive(const char* name);         // e.g. "thirdperson"
// phones: binds can be put on screen as circles (tap the bind field in the menu); touch.cpp draws them
struct TouchBind { int slot; const char* label; bool active; };
void GetTouchBinds(std::vector<TouchBind>& out);
// queries used by the client
bool RemoveScope();
bool RemoveVisualRecoil();
bool RemoveFlash();
bool RemoveSmoke();
float NightmodeAmount();
float ViewmodelFov();
bool ThirdpersonActive();
bool ShouldDrawLocalInThirdperson();
bool LocalBodyYawActive();                      // anti-aim desync on: draw the local model with the sent (AA) yaw, the ghost shows the desync
float GetRealYaw();                             // local real (server) foot yaw for AA indicator
} // namespace cheat
