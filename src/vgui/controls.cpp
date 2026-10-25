// controls.cpp - vgui2 controls in the Steam olive scheme
#include "controls.hpp"
#include "../game/shared/soundemitter.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <cstdio>

namespace vgui {
using namespace scheme;
static InputState s_input;
InputState& Input() { return s_input; }
int g_KeyboardFocus = 0;

void BeginInput() {
  InputState& in = s_input;
  in.mouse = GetMousePosition();
  in.mouseDown = IsMouseButtonDown(MOUSE_BUTTON_LEFT);
  in.mousePressed = IsMouseButtonPressed(MOUSE_BUTTON_LEFT);
  in.mouseReleased = IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
  in.mouseRightPressed = IsMouseButtonPressed(MOUSE_BUTTON_RIGHT);
  in.wheel = GetMouseWheelMove();
  static double lastClick = 0; static Vector2 lastPos{};
  in.doubleClick = false;
  if (in.mousePressed) { double t = GetTime(); if (t - lastClick < 0.35 && std::fabs(lastPos.x - in.mouse.x) < 4 && std::fabs(lastPos.y - in.mouse.y) < 4) in.doubleClick = true; lastClick = t; lastPos = in.mouse; }
  in.chars.clear(); in.keys.clear();
  for (int c = GetCharPressed(); c; c = GetCharPressed()) in.chars.push_back(c);
  for (int k = GetKeyPressed(); k; k = GetKeyPressed()) in.keys.push_back(k);
  // key repeat for editing keys
  static const int rep[] = { KEY_BACKSPACE, KEY_DELETE, KEY_LEFT, KEY_RIGHT, KEY_UP, KEY_DOWN, KEY_PAGE_UP, KEY_PAGE_DOWN };
  for (int k : rep) if (IsKeyPressedRepeat(k)) in.keys.push_back(k);
}
void ConsumeKey(int k) { auto& v = s_input.keys; v.erase(std::remove(v.begin(), v.end(), k), v.end()); }
void ConsumeChars() { s_input.chars.clear(); }
void PlayClick() { EmitSound2D("UI.Click"); }

// ------------------------------------------------------------------------------------------------
// window manager
static std::vector<Frame*> s_frames;     // z order: back -> front
static Frame* s_hot = nullptr;           // frame under the mouse (topmost)
static Frame* s_current = nullptr;
static int s_nextId = 1;
static bool s_popupOpen = false;
struct Popup { int id; std::vector<std::string> items; int* sel; float x, y, w; bool changed = false; };
static Popup s_popup; static int s_popupOwner = 0; static bool s_popupJustOpened = false;
bool WM_PopupOpen() { return s_popupOwner != 0; }

void WM_Register(Frame* f) { if (!f->id) f->id = s_nextId++; if (std::find(s_frames.begin(), s_frames.end(), f) == s_frames.end()) s_frames.push_back(f); }
void Frame::Activate() { WM_Register(this); visible = true; s_frames.erase(std::remove(s_frames.begin(), s_frames.end(), this), s_frames.end()); s_frames.push_back(this); }
void Frame::Center() { x = std::floor((GetScreenWidth() - w) / 2); y = std::floor((GetScreenHeight() - h) / 2); }
bool Frame::IsActive() const { for (auto it = s_frames.rbegin(); it != s_frames.rend(); ++it) if ((*it)->visible) return *it == this; return false; }
void WM_BeginFrame() {
  s_hot = nullptr;
  if (s_popupOwner) return; // popup steals the mouse
  for (auto it = s_frames.rbegin(); it != s_frames.rend(); ++it) {
    Frame* f = *it; if (!f->visible) continue;
    if (s_input.mouse.x >= f->x && s_input.mouse.x < f->x + f->w && s_input.mouse.y >= f->y && s_input.mouse.y < f->y + f->h) { s_hot = f; break; }
  }
  // click brings to front
  if (s_hot && s_input.mousePressed && s_frames.back() != s_hot) s_hot->Activate();
}
bool WM_AnyFrameUnderMouse() { return s_hot != nullptr || s_popupOwner != 0; }
Frame* WM_Current() { return s_current; }
void WM_SetNoFrame() { s_current = nullptr; }
bool WM_MouseInCurrent() {
  if (s_popupOwner) return false;
  if (!s_current) return s_hot == nullptr;
  return s_hot == s_current;
}
void WM_DrawOrder(std::vector<Frame*>& out) { out = s_frames; }

static bool Hover(float x, float y, float w, float h) { return WM_MouseInCurrent() && s_input.mouse.x >= x && s_input.mouse.x < x + w && s_input.mouse.y >= y && s_input.mouse.y < y + h; }

bool Frame::Begin() {
  if (!visible) return false;
  WM_Register(this);
  s_current = this;
  ISurface* s = surface();
  bool active = IsActive();
  // drag / resize
  if (dragging) { if (s_input.mouseDown) { x = s_input.mouse.x - dragOff.x; y = s_input.mouse.y - dragOff.y; } else dragging = false; }
  if (resizing) { if (s_input.mouseDown) { w = std::max(minW, s_input.mouse.x - x + dragOff.x); h = std::max(minH, s_input.mouse.y - y + dragOff.y); } else resizing = false; }
  x = Clamp(x, -w + 60, (float)GetScreenWidth() - 60); y = Clamp(y, 0.f, (float)GetScreenHeight() - 30);
  // body
  s->DrawFilledRect(x, y, w, h, Bg);
  s->DrawBevel(x, y, w, h, BorderLight, Border);
  // title bar
  Color tc = active ? Text : TextDim;
  s->DrawText(FONT_DEFAULT_BOLD, title.c_str(), x + 10, y + 8, tc);
  float tbh = 28;
  if (Hover(x, y, w - 30, tbh) && s_input.mousePressed) { dragging = true; dragOff = { s_input.mouse.x - x, s_input.mouse.y - y }; }
  if (closeButton) {
    float bx = x + w - 24, by = y + 6, bs = 16;
    bool hv = Hover(bx, by, bs, bs);
    s->DrawFilledRect(bx, by, bs, bs, hv ? ButtonBgArmed : ButtonBg);
    s->DrawBevel(bx, by, bs, bs, BorderLight, Border, hv && s_input.mouseDown);
    s->DrawLine(bx + 4, by + 4, bx + bs - 4, by + bs - 4, tc, 1.5f); s->DrawLine(bx + bs - 4, by + 4, bx + 4, by + bs - 4, tc, 1.5f);
    if (hv && s_input.mouseReleased) { visible = false; PlayClick(); }
  }
  if (sizable) {
    float gx = x + w - 12, gy = y + h - 12;
    for (int i = 0; i < 3; i++) s->DrawLine(gx + i * 4, gy + 10, gx + 10, gy + i * 4, BorderLight);
    if (Hover(gx - 2, gy - 2, 14, 14) && s_input.mousePressed) { resizing = true; dragOff = { x + w - s_input.mouse.x, y + h - s_input.mouse.y }; }
  }
  cx = x + 10; cy = y + tbh + 4; cw = w - 20; ch = h - tbh - 14;
  return true;
}
void Frame::End() { s_current = nullptr; }

// ------------------------------------------------------------------------------------------------
bool Button(const char* label, float x, float y, float w, float h, bool enabled, bool isDefault) {
  ISurface* s = surface();
  bool hv = enabled && Hover(x, y, w, h);
  bool down = hv && s_input.mouseDown;
  s->DrawFilledRect(x, y, w, h, down ? ButtonBgDepressed : hv ? ButtonBgArmed : ButtonBg);
  s->DrawBevel(x, y, w, h, BorderLight, Border, down);
  if (isDefault) s->DrawOutlinedRect(x - 1, y - 1, w + 2, h + 2, Border);
  Color tc = !enabled ? Disabled : hv ? TextSelected : Text;
  Vector2 ts = s->GetTextSize(FONT_UI, label);
  s->DrawText(FONT_UI, label, x + (w - ts.x) / 2 + (down ? 1 : 0), y + (h - ts.y) / 2 + (down ? 1 : 0), tc);
  bool clicked = hv && s_input.mouseReleased;
  if (clicked) PlayClick();
  return clicked;
}

bool CheckButton(const char* label, bool* v, float x, float y, bool enabled) {
  ISurface* s = surface();
  float bs = 14;
  float tw = s->GetTextSize(FONT_UI, label).x;
  bool hv = enabled && Hover(x, y, bs + 8 + tw, bs + 2);
  s->DrawFilledRect(x, y, bs, bs, BgDark);
  s->DrawBevel(x, y, bs, bs, BorderLight, Border, true);
  if (*v) { Color c = enabled ? TextSelected : Disabled; s->DrawLine(x + 3, y + 7, x + 6, y + 10, c, 2); s->DrawLine(x + 6, y + 10, x + 11, y + 3, c, 2); }
  s->DrawText(FONT_UI, label, x + bs + 8, y, !enabled ? Disabled : hv ? TextSelected : Text);
  if (hv && s_input.mouseReleased) { *v = !*v; PlayClick(); return true; }
  return false;
}

static int s_activeSlider = 0; static const void* s_activeSliderPtr = nullptr;
bool Slider(const char* label, float* v, float mn, float mx, float x, float y, float w, const char* fmt) {
  ISurface* s = surface();
  s->DrawText(FONT_UI, label, x, y, Text);
  char buf[64]; snprintf(buf, sizeof(buf), fmt, *v);
  s->DrawTextRight(FONT_UI, buf, x + w, y, TextDim);
  float ty = y + 22, th = 4;
  s->DrawFilledRect(x, ty, w, th, BgDark); s->DrawBevel(x, ty, w, th, BorderLight, Border, true);
  // ticks
  for (int i = 0; i <= 10; i++) s->DrawFilledRect(x + w * i / 10.f, ty + 8, 1, 4, TextDim);
  float t = (mx > mn) ? Clamp((*v - mn) / (mx - mn), 0.f, 1.f) : 0;
  float kx = x + t * w - 5, ky = ty - 6, kw = 10, kh = 16;
  bool hv = Hover(x - 5, ky, w + 10, kh);
  if (hv && s_input.mousePressed) s_activeSliderPtr = v;
  bool changed = false;
  if (s_activeSliderPtr == v) {
    if (s_input.mouseDown) { float nt = Clamp((s_input.mouse.x - x) / w, 0.f, 1.f); float nv = mn + nt * (mx - mn); if (nv != *v) { *v = nv; changed = true; } }
    else s_activeSliderPtr = nullptr;
  }
  s->DrawFilledRect(kx, ky, kw, kh, hv || s_activeSliderPtr == v ? ButtonBgArmed : ButtonBg);
  s->DrawBevel(kx, ky, kw, kh, BorderLight, Border);
  (void)s_activeSlider;
  return changed;
}

bool ComboBox(int id, const std::vector<std::string>& items, int* sel, float x, float y, float w) {
  ISurface* s = surface();
  float h = 22;
  bool hv = Hover(x, y, w, h);
  s->DrawFilledRect(x, y, w, h, BgDark);
  s->DrawBevel(x, y, w, h, BorderLight, Border, true);
  const char* txt = (*sel >= 0 && *sel < (int)items.size()) ? items[*sel].c_str() : "";
  s->PushClip((int)x, (int)y, (int)(w - 20), (int)h);
  s->DrawText(FONT_UI, txt, x + 6, y + 4, Text);
  s->PopClip();
  // arrow button
  float bx = x + w - 19;
  s->DrawFilledRect(bx, y + 2, 17, h - 4, hv ? ButtonBgArmed : ButtonBg); s->DrawBevel(bx, y + 2, 17, h - 4, BorderLight, Border);
  s->DrawTriangle({ bx + 4, y + 9 }, { bx + 13, y + 9 }, { bx + 8.5f, y + 14 }, Text);
  bool changed = false;
  if (s_popupOwner == id && s_popup.changed) { s_popup.changed = false; changed = true; s_popupOwner = 0; }
  if (hv && s_input.mouseReleased && s_popupOwner == 0) {
    s_popupOwner = id; s_popup = { id, items, sel, x, y + h, w }; s_popupJustOpened = true; PlayClick();
  }
  return changed;
}

void DrawPopups() {
  if (!s_popupOwner) return;
  ISurface* s = surface();
  Popup& p = s_popup;
  float ih = 20; int n = (int)p.items.size(); int maxVis = 14;
  float h = std::min(n, maxVis) * ih + 4;
  float y = p.y; if (y + h > GetScreenHeight()) y = p.y - 22 - h;
  static float scroll = 0;
  s->DrawFilledRect(p.x, y, p.w, h, BgDark);
  s->DrawOutlinedRect(p.x, y, p.w, h, Border);
  bool inside = s_input.mouse.x >= p.x && s_input.mouse.x < p.x + p.w && s_input.mouse.y >= y && s_input.mouse.y < y + h;
  if (inside && s_input.wheel != 0) scroll = Clamp(scroll - s_input.wheel, 0.f, (float)std::max(0, n - maxVis));
  int first = (int)scroll;
  s->PushClip((int)p.x, (int)y, (int)p.w, (int)h);
  for (int i = first; i < std::min(n, first + maxVis); i++) {
    float iy = y + 2 + (i - first) * ih;
    bool hv = s_input.mouse.x >= p.x && s_input.mouse.x < p.x + p.w && s_input.mouse.y >= iy && s_input.mouse.y < iy + ih;
    if (hv) s->DrawFilledRect(p.x + 1, iy, p.w - 2, ih, Selection);
    s->DrawText(FONT_UI, p.items[i].c_str(), p.x + 6, iy + 3, hv ? Text : (i == *p.sel ? TextSelected : Text));
    if (hv && s_input.mouseReleased && !s_popupJustOpened) { *p.sel = i; p.changed = true; PlayClick(); }
  }
  s->PopClip();
  if (!inside && (s_input.mousePressed || s_input.KeyPressed(KEY_ESCAPE)) && !s_popupJustOpened) { s_popupOwner = 0; if (s_input.KeyPressed(KEY_ESCAPE)) ConsumeKey(KEY_ESCAPE); }
  if (p.changed) { /* owner picks it up next frame */ }
  s_popupJustOpened = false;
  if (p.changed && s_popupOwner) { /* keep owner so ComboBox returns true */ }
}

static int s_cursor = 0; static int s_focusId = 0;
bool TextEntry(int id, std::string& text, float x, float y, float w, float h, bool password, int maxLen) {
  ISurface* s = surface();
  bool hv = Hover(x, y, w, h);
  if (s_input.mousePressed) { if (hv) { g_KeyboardFocus = id; s_cursor = (int)text.size(); } else if (g_KeyboardFocus == id && WM_MouseInCurrent()) g_KeyboardFocus = 0; }
  bool focused = g_KeyboardFocus == id;
  if (focused && s_focusId != id) { s_focusId = id; s_cursor = (int)text.size(); }
  s->DrawFilledRect(x, y, w, h, BgDark);
  s->DrawBevel(x, y, w, h, BorderLight, Border, true);
  bool enter = false;
  if (focused) {
    s_cursor = Clamp(s_cursor, 0, (int)text.size());
    for (int c : s_input.chars) { if (c < 32 || (int)text.size() >= maxLen) continue; int n = 0; const char* u = CodepointToUTF8(c, &n); text.insert(s_cursor, u, n); s_cursor += n; }
    s_input.chars.clear();
    for (int k : s_input.keys) {
      if (k == KEY_BACKSPACE && s_cursor > 0) { int st = s_cursor - 1; while (st > 0 && (text[st] & 0xC0) == 0x80) st--; text.erase(st, s_cursor - st); s_cursor = st; }
      else if (k == KEY_DELETE && s_cursor < (int)text.size()) { int e = s_cursor + 1; while (e < (int)text.size() && (text[e] & 0xC0) == 0x80) e++; text.erase(s_cursor, e - s_cursor); }
      else if (k == KEY_LEFT && s_cursor > 0) { s_cursor--; while (s_cursor > 0 && (text[s_cursor] & 0xC0) == 0x80) s_cursor--; }
      else if (k == KEY_RIGHT && s_cursor < (int)text.size()) { s_cursor++; while (s_cursor < (int)text.size() && (text[s_cursor] & 0xC0) == 0x80) s_cursor++; }
      else if (k == KEY_HOME) s_cursor = 0;
      else if (k == KEY_END) s_cursor = (int)text.size();
      else if (k == KEY_ENTER || k == KEY_KP_ENTER) enter = true;
      else if (k == KEY_V && (IsKeyDown(KEY_LEFT_CONTROL) || IsKeyDown(KEY_RIGHT_CONTROL))) { const char* cb = GetClipboardText(); if (cb) { std::string t = cb; t.erase(std::remove(t.begin(), t.end(), '\n'), t.end()); text.insert(s_cursor, t); s_cursor += (int)t.size(); } }
    }
    // consume editing keys
    for (int k : { KEY_BACKSPACE, KEY_DELETE, KEY_LEFT, KEY_RIGHT, KEY_HOME, KEY_END, KEY_ENTER, KEY_KP_ENTER }) ConsumeKey(k);
  }
  std::string shown = password ? std::string(text.size(), '*') : text;
  s->PushClip((int)x + 2, (int)y, (int)w - 4, (int)h);
  float tw = s->GetTextSize(FONT_UI, shown.substr(0, std::min((size_t)s_cursor, shown.size())).c_str()).x;
  float off = focused ? std::max(0.f, tw - (w - 12)) : 0;
  s->DrawText(FONT_UI, shown.c_str(), x + 5 - off, y + (h - 13) / 2 - 1, Text);
  if (focused && std::fmod(GetTime(), 1.0) < 0.55) s->DrawFilledRect(x + 5 + tw - off, y + 4, 1, h - 8, Text);
  s->PopClip();
  return enter;
}

void Label(const char* text, float x, float y, Color c, int font) { surface()->DrawText(font, text, x, y, c); }
void Divider(float x, float y, float w) { surface()->DrawFilledRect(x, y, w, 1, Border); surface()->DrawFilledRect(x, y + 1, w, 1, BorderLight); }
void GroupBox(const char* title, float x, float y, float w, float h) {
  ISurface* s = surface();
  float tw = s->GetTextSize(FONT_UI, title).x;
  s->DrawOutlinedRect(x, y + 7, w, h - 7, Border); s->DrawOutlinedRect(x + 1, y + 8, w - 2, h - 9, BorderLight);
  s->DrawFilledRect(x + 8, y, tw + 8, 14, Bg);
  s->DrawText(FONT_UI, title, x + 12, y, Text);
}

int PropertySheet(const std::vector<std::string>& tabs, int* active, float x, float y, float w, float h) {
  ISurface* s = surface();
  float tx = x, th = 24;
  for (int i = 0; i < (int)tabs.size(); i++) {
    float tw = s->GetTextSize(FONT_UI, tabs[i].c_str()).x + 22;
    bool sel = i == *active; bool hv = Hover(tx, y, tw, th);
    float ty = sel ? y : y + 2;
    s->DrawFilledRect(tx, ty, tw, th + (sel ? 2 : -2), sel ? Bg : BgDark);
    s->DrawFilledRect(tx, ty, tw, 1, BorderLight); s->DrawFilledRect(tx, ty, 1, th, BorderLight); s->DrawFilledRect(tx + tw - 1, ty, 1, th, Border);
    s->DrawText(FONT_UI, tabs[i].c_str(), tx + 11, ty + 5, sel ? TextSelected : hv ? Text : TextDim);
    if (hv && s_input.mouseReleased && !sel) { *active = i; PlayClick(); }
    tx += tw + 1;
  }
  // page border
  float py = y + th;
  s->DrawFilledRect(x, py, w, 1, BorderLight);
  s->DrawBevel(x, py, w, h - th, BorderLight, Border);
  return *active;
}

void ScrollBar(float* scroll, float content, float view, float x, float y, float h) {
  ISurface* s = surface();
  float w = 16;
  s->DrawFilledRect(x, y, w, h, BgDark);
  if (content <= view) return;
  float maxScroll = content - view;
  float bh = std::max(20.f, h * view / content);
  float by = y + (h - bh) * (*scroll / maxScroll);
  static float* drag = nullptr; static float dragOff = 0;
  bool hv = Hover(x, by, w, bh);
  if (hv && s_input.mousePressed) { drag = scroll; dragOff = s_input.mouse.y - by; }
  if (drag == scroll) { if (s_input.mouseDown) { float ny = Clamp(s_input.mouse.y - dragOff, y, y + h - bh); *scroll = (ny - y) / (h - bh) * maxScroll; } else drag = nullptr; }
  s->DrawFilledRect(x + 1, by, w - 2, bh, hv ? ButtonBgArmed : ButtonBg);
  s->DrawBevel(x + 1, by, w - 2, bh, BorderLight, Border);
}

int ListPanel(ListPanelState& st, const std::vector<ListColumn>& cols, const std::vector<std::vector<std::string>>& rows, float x, float y, float w, float h, std::function<Color(int, int)> colorFn) {
  ISurface* s = surface();
  float hh = 22, rh = 18;
  s->DrawFilledRect(x, y, w, h, BgDark);
  s->DrawBevel(x, y, w, h, BorderLight, Border, true);
  // header
  float cx = x + 1;
  for (size_t c = 0; c < cols.size(); c++) {
    float cw = c + 1 == cols.size() ? (x + w - 17 - cx) : cols[c].width;
    bool hv = Hover(cx, y + 1, cw, hh);
    s->DrawFilledRect(cx, y + 1, cw, hh, hv ? ButtonBgArmed : ButtonBg);
    s->DrawBevel(cx, y + 1, cw, hh, BorderLight, Border);
    s->DrawText(FONT_UI, cols[c].name.c_str(), cx + 6, y + 5, Text);
    if ((int)c == st.sortColumn) s->DrawTriangle({ cx + cw - 14, y + (st.sortAsc ? 14.f : 9.f) }, { cx + cw - 6, y + (st.sortAsc ? 14.f : 9.f) }, { cx + cw - 10, y + (st.sortAsc ? 9.f : 14.f) }, TextDim);
    if (hv && s_input.mouseReleased) { if (st.sortColumn == (int)c) st.sortAsc = !st.sortAsc; else { st.sortColumn = (int)c; st.sortAsc = true; } PlayClick(); }
    cx += cw;
  }
  float by = y + hh + 2, bh = h - hh - 3;
  float content = rows.size() * rh;
  if (Hover(x, by, w, bh) && s_input.wheel != 0) st.scroll = Clamp(st.scroll - s_input.wheel * rh * 3, 0.f, std::max(0.f, content - bh));
  st.scroll = Clamp(st.scroll, 0.f, std::max(0.f, content - bh));
  int dbl = -1;
  s->PushClip((int)x + 1, (int)by, (int)w - 18, (int)bh);
  int first = (int)(st.scroll / rh);
  for (int r = first; r < (int)rows.size(); r++) {
    float ry = by + r * rh - st.scroll; if (ry > by + bh) break;
    bool sel = r == st.selected; bool hv = Hover(x + 1, ry, w - 18, rh) && ry + rh > by;
    if (sel) s->DrawFilledRect(x + 1, ry, w - 18, rh, Selection);
    float cx2 = x + 1;
    for (size_t c = 0; c < cols.size() && c < rows[r].size(); c++) {
      float cw = c + 1 == cols.size() ? (x + w - 17 - cx2) : cols[c].width;
      Color tc = colorFn ? colorFn(r, (int)c) : Text; if (sel) tc = Text;
      s->PushClip((int)cx2, (int)ry, (int)cw - 4, (int)rh);
      s->DrawText(FONT_UI, rows[r][c].c_str(), cx2 + 6, ry + 2, tc);
      s->PopClip();
      cx2 += cw;
    }
    if (hv && s_input.mousePressed) { st.selected = r; if (s_input.doubleClick) dbl = r; }
  }
  s->PopClip();
  ScrollBar(&st.scroll, content, bh, x + w - 17, by, bh);
  return dbl;
}
} // namespace vgui
