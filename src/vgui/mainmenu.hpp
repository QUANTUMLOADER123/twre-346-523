// mainmenu.hpp - Counter-Strike: Global Offensive (Panorama) style main menu
//
//   top bar      : quit / settings icons, PLAY - AGENTS - SERVERS tabs, resume / disconnect in game, profile
//   home         : your agent standing on the background map (cl_render.cpp CL_RenderMenuBackground), news,
//                  big PLAY button, agent switcher
//   PLAY         : map tiles (CS:S menu thumbnails), game mode, bots and match settings, GO
//   AGENTS       : pick the CT / T agent you play with (cl_agent_ct / cl_agent_t) - shown in the menu scene
//   SERVERS      : community server browser (offline: connecting starts a local server with bots)
//   SETTINGS     : every option of the game (game, crosshair, HUD, audio, video, mouse, keyboard, touch, content)
#pragma once

namespace mainmenu {
void Paint();            // draw + handle input (called by gameui::Paint while the menu is active)
bool OnEscape();         // close the open panel / popup; false when nothing was open
bool CapturingKey();     // the keyboard settings wait for a key (Esc cancels)
void OpenSettings();
}
