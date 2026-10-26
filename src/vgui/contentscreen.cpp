// contentscreen.cpp - first-start loading screen: downloads the CS:GO maps + the shared Source assets (engine/download.hpp)
#include "contentscreen.hpp"
#include "csgo_style.hpp"
#include "../engine/download.hpp"
#include "../engine/srcfs.hpp"
#include "../engine/convar.hpp"
#include "raylib.h"
#include <cmath>
#include <cstdio>
#include <string>

namespace {
bool Button(const char* label, float x, float y, float w, float h, Color base, Color hover) {
  auto* s = vgui::surface();
  bool hv = csgo::Hover(x, y, w, h);
  s->DrawFilledRect(x, y, w, h, hv ? hover : base);
  s->DrawTextCentered(FONT_DEFAULT_BOLD, label, x + w / 2, y + (h - s->GetFontTall(FONT_DEFAULT_BOLD)) / 2, csgo::Text);
  return hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}
}

bool ContentScreen_Run(bool force) {
  if (!force && !download::ContentMissing()) return true;
  const bool coreMissing = download::CoreContentMissing();
  download::Start();
  auto* s = vgui::surface();
  bool skipped = false;
  while (!WindowShouldClose()) {
    download::Progress p = download::Get();
    if (p.state == download::DONE) break;
    if (p.state == download::FAILED && !coreMissing) { Msg("Optional content download failed: %s\n", p.error.c_str()); break; }   // extras only: never block
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight(), t = (float)GetTime();
    BeginDrawing();
    ClearBackground(csgo::Bg);
    s->DrawGradientV(0, 0, W, H, Color{ 30, 36, 46, 255 }, csgo::Bg);
    // logo
    float ly = H * 0.16f;
    s->DrawTextCentered(FONT_LOGO, "COUNTER-STRIKE", W / 2 - s->GetTextWide(FONT_LOGO, " HvH") / 2, ly, csgo::Text);
    s->DrawText(FONT_LOGO, "HvH", W / 2 + s->GetTextWide(FONT_LOGO, "COUNTER-STRIKE") / 2 - s->GetTextWide(FONT_LOGO, " HvH") / 2 + s->GetTextWide(FONT_LOGO, " "), ly, csgo::Gold);
    // panel
    float pw = std::min(W - 40.f, 620.f), ph = 230, px = (W - pw) / 2, py = H * 0.40f;
    s->DrawFilledRect(px, py, pw, ph, csgo::Panel);
    s->DrawFilledRect(px, py, pw, 3, csgo::Accent);
    float x = px + 24, y = py + 20;
    s->DrawText(FONT_MENU_TITLE, coreMissing ? "Downloading CS:GO content" : "Downloading game content", x, y, csgo::Text);
    y += 34;
    int npacks = 0; const download::Pack* packs = download::Packs(npacks);
    char line[256];
    if (p.state == download::RUNNING) {
      snprintf(line, sizeof(line), "%d / %d  -  %s", p.pack + 1, std::max(1, p.packCount), packs[std::min(p.pack, npacks - 1)].title);
      s->DrawText(FONT_UI, line, x, y, csgo::TextDim);
    } else if (p.state == download::FAILED) {
      s->DrawText(FONT_UI, ("Error: " + p.error).c_str(), x, y, csgo::Red);
    } else if (p.state == download::CANCELLED) {
      s->DrawText(FONT_UI, "Cancelled. Already downloaded parts are kept.", x, y, csgo::TextDim);
    }
    y += 28;
    // progress bar (the archives have no size header: approximate total)
    float frac = p.totalExpectedMB > 0 ? (float)std::min(0.995, p.totalMB / p.totalExpectedMB) : 0.f;
    float bw = pw - 48;
    s->DrawFilledRect(x, y, bw, 12, csgo::PanelLight);
    s->DrawFilledRect(x, y, bw * frac, 12, p.state == download::FAILED ? csgo::Red : csgo::Green);
    if (p.state == download::RUNNING) {   // moving shine
      float sx = std::fmod(t * 0.6f, 1.f) * bw * frac;
      s->DrawGradientH(x + std::max(0.f, sx - 60), y, std::min(60.f, sx), 12, Color{ 255, 255, 255, 0 }, Color{ 255, 255, 255, 60 });
    }
    y += 22;
    snprintf(line, sizeof(line), "%.0f / ~%.0f MB     %.1f MB/s     %d files", p.totalMB, p.totalExpectedMB, p.speedMBs, p.files);
    s->DrawText(FONT_UI, line, x, y, csgo::Text);
    y += 22;
    std::string cur = p.current; if (cur.size() > 70) cur = "..." + cur.substr(cur.size() - 67);
    s->DrawText(FONT_UI, cur.c_str(), x, y, csgo::TextFaint);
    // buttons
    float by = py + ph - 52, bh = 36;
    if (p.state == download::RUNNING) {
      s->DrawText(FONT_UI, coreMissing ? "Optional: real CS:GO agents / weapons / textures come from csgo_download." : "Only needed once.", x, by + 10, csgo::TextDim);
      if (Button("CANCEL", px + pw - 24 - 140, by, 140, bh, csgo::PanelLight, csgo::PanelHover)) download::Cancel();
      if (!coreMissing && Button("SKIP", px + pw - 24 - 290, by, 140, bh, csgo::PanelLight, csgo::PanelHover)) { download::Cancel(); skipped = true; }
    } else if (p.state == download::FAILED || p.state == download::CANCELLED) {
      if (Button("RETRY", px + pw - 24 - 140, by, 140, bh, csgo::Green, csgo::GreenHover)) download::Start();
      if (Button("SKIP", px + pw - 24 - 290, by, 140, bh, csgo::PanelLight, csgo::PanelHover)) skipped = true;
    }
    // spinner
    if (p.state == download::RUNNING)
      for (int i = 0; i < 8; i++) {
        float a = t * 4.f + i * 0.785f;
        s->DrawFilledCircle(px + pw - 40 + std::cos(a) * 9, py + 30 + std::sin(a) * 9, 2.2f, csgo::Alpha(csgo::Text, (i + 1) / 8.f));
      }
    s->DrawTextCentered(FONT_UI, coreMissing ? "No CS:GO content found: run csgo_download (free CS:GO Dedicated Server) or set csgo_gamedir"
                                              : "Maps and CS:GO extras are downloaded from public GitHub mirrors", W / 2, H - 30, csgo::TextFaint);
    EndDrawing();
    if (skipped) { if (coreMissing) download::DismissCoreHint(); break; }
  }
  download::Shutdown();
  srcfs::Invalidate();
  return !skipped;
}
