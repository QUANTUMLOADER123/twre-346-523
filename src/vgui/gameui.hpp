// gameui.hpp - GameUI: main menu, Create Server / Find Servers / Options dialogs, console
#pragma once
namespace gameui {
void Init();
void Shutdown();
void Paint();                // draw active UI (main menu + dialogs + console)
bool IsMenuActive();         // main menu visible
void Show();
void Hide();
void ToggleConsole();
bool IsConsoleVisible();
bool WantsKeyboard();        // a text entry / key capture has focus
bool WantsMouse();
void OnEscape();
void OpenNewGame();
}
