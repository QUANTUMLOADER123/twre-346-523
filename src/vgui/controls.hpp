// controls.hpp - vgui2 controls (immediate mode) in the Steam 2004 olive scheme:
// Frame, Button, CheckButton, Slider, ComboBox, TextEntry, ListPanel, PropertySheet, Label
#pragma once
#include "surface.hpp"
#include <string>
#include <vector>
#include <functional>

namespace vgui {

struct InputState {
  Vector2 mouse{};
  bool mouseDown = false, mousePressed = false, mouseReleased = false, mouseRightPressed = false;
  bool doubleClick = false;
  float wheel = 0;
  std::vector<int> chars;     // typed codepoints this frame
  std::vector<int> keys;      // raylib keys pressed this frame (with repeat)
  bool KeyPressed(int k) const { for (int x : keys) if (x == k) return true; return false; }
};
InputState& Input();
void BeginInput();            // poll raylib once per frame (call at the start of the frame)
void ConsumeKey(int k);
void InjectKey(int k);        // queue a key press for the next frame (touch buttons, Android Back)
void ConsumeChars();

class Frame {
public:
  Frame(const char* title, float w, float h, bool sizable = false) : title(title), w(w), h(h), minW(w * 0.6f), minH(h * 0.6f), sizable(sizable) {}
  std::string title;
  float x = 100, y = 100, w, h, minW, minH;
  bool visible = false;
  bool sizable;
  bool closeButton = true;
  int id = 0;
  void Activate();                  // show + bring to front
  void Close() { visible = false; }
  void Center();
  // draw chrome and process dragging; returns false if hidden. content area = (cx, cy, cw, ch)
  bool Begin();
  void End();
  float cx = 0, cy = 0, cw = 0, ch = 0;
  bool IsActive() const;            // topmost
  bool dragging = false, resizing = false; Vector2 dragOff{};
};

// window manager: z-order and hit testing
void WM_BeginFrame();               // decides which frame receives mouse input
void WM_Register(Frame* f);
bool WM_MouseInCurrent();           // true if the mouse is over the frame currently being drawn and it's the hot one
bool WM_AnyFrameUnderMouse();
Frame* WM_Current();
void WM_SetNoFrame();               // widgets drawn outside of frames (main menu)
void WM_DrawOrder(std::vector<Frame*>& out);  // back to front
extern int g_KeyboardFocus;         // id of the focused text entry (0 none)
bool WM_PopupOpen();

// widgets: return true when activated
bool Button(const char* label, float x, float y, float w, float h, bool enabled = true, bool isDefault = false);
bool CheckButton(const char* label, bool* value, float x, float y, bool enabled = true);
bool Slider(const char* label, float* value, float mn, float mx, float x, float y, float w, const char* fmt = "%.2f");
bool ComboBox(int id, const std::vector<std::string>& items, int* sel, float x, float y, float w);
bool TextEntry(int id, std::string& text, float x, float y, float w, float h = 22, bool password = false, int maxLen = 255);
void TextEntryCursorToEnd();      // move the caret of the focused entry to the end (console Tab / history recall)
void Label(const char* text, float x, float y, Color c = scheme::Text, int font = FONT_UI);
void Divider(float x, float y, float w);
void GroupBox(const char* title, float x, float y, float w, float h);
int PropertySheet(const std::vector<std::string>& tabs, int* active, float x, float y, float w, float h); // draws tabs + page border
struct ListColumn { std::string name; float width; };
struct ListPanelState { int selected = -1; float scroll = 0; int sortColumn = -1; bool sortAsc = true; double lastClick = 0; int lastClickRow = -1; };
// returns the row that was double-clicked (or -1)
int ListPanel(ListPanelState& st, const std::vector<ListColumn>& cols, const std::vector<std::vector<std::string>>& rows, float x, float y, float w, float h, std::function<Color(int row, int col)> colorFn = nullptr);
void ScrollBar(float* scroll, float content, float view, float x, float y, float h);
void DrawPopups();                  // combo box drop downs (call last)
void PlayClick();
} // namespace vgui
