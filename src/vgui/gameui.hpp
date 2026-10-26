// gameui.hpp - GameUI: main menu (mainmenu.cpp, CS:GO style) and the console
#pragma once
namespace gameui {
void Init();
void Shutdown();
void Paint();                // draw active UI (main menu + dialogs + console)
bool IsMenuActive();         // main menu visible
void Show();
void Hide();
void ForceHide();            // hide even before the map is loaded (starting a game)
void ToggleConsole();
bool IsConsoleVisible();
bool WantsKeyboard();        // a text entry / key capture has focus
bool WantsMouse();
void OnEscape();
}
