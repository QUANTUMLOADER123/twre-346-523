// hud_menus.hpp - CS 1.6 VGUI in-game menus: scoreboard, team select, buy menu, chat input
#pragma once
void HUD_DrawScoreboard();
void HUD_DrawTeamMenu();
void HUD_DrawBuyMenu();
bool HUD_MenusWantInput();         // team/buy menu or chat open -> show cursor, no game input
bool HUD_MenuKeyPressed(int key);  // raylib key; returns true if consumed
void HUD_ChatChar(int codepoint);
