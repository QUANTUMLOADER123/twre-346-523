// contentscreen.cpp - first-start loading screen: downloads the maps + the shared Source assets (engine/download.hpp)
// CS:GO (Panorama) look: one dark panel, a real progress bar with the percentage, the transfer speed and the file
// being written. No console commands are ever shown - the player only needs the two buttons.
#include "contentscreen.hpp"
#include "csgo_style.hpp"
#include "../engine/download.hpp"
#include "../engine/srcfs.hpp"
#include "../engine/convar.hpp"
#include "raylib.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace {
// 0.94 GB -> "0.94 GB", 512 MB -> "512 MB": the panel is narrow, MB is exact enough below 1 GB
void SizeStr(char* out, size_t n, double mb) {
  if (mb < 0) { snprintf(out, n, "--"); return; }
  if (mb >= 1024.0) snprintf(out, n, "%.2f GB", mb / 1024.0);
  else if (mb >= 100.0) snprintf(out, n, "%.0f MB", mb);
  else snprintf(out, n, "%.1f MB", mb);
}

bool Button(const char* label, float x, float y, float w, float h, Color base, Color hover, bool primary = false) {
  auto* s = vgui::surface();
  bool hv = csgo::Hover(x, y, w, h);
  s->DrawFilledRect(x, y, w, h, hv ? hover : base);
  if (primary) s->DrawFilledRect(x, y, w, 2, csgo::Alpha(csgo::GreenHover, 0.9f));
  s->DrawTextCentered(FONT_GO_BOLD, label, x + w / 2, y + (h - s->GetFontTall(FONT_GO_BOLD)) / 2, csgo::Text);
  return hv && IsMouseButtonReleased(MOUSE_BUTTON_LEFT);
}
}  // namespace

bool ContentScreen_Run(bool force) {
  if (!force && !download::ContentMissing()) return true;
  const bool coreMissing = download::CoreContentMissing();
  download::Start();
  auto* s = vgui::surface();
  bool skipped = false;
  double shownFrac = 0;   // the bar eases towards the real value, a jump between two packs looks like a glitch
  while (!WindowShouldClose()) {
    download::Progress p = download::Get();
    if (p.state == download::DONE) break;
    if (p.state == download::FAILED && !coreMissing) { Msg("Optional content download failed: %s\n", p.error.c_str()); break; }
    const float W = (float)GetScreenWidth(), H = (float)GetScreenHeight(), t = (float)GetTime();
    BeginDrawing();
    ClearBackground(csgo::Bg);
    s->DrawGradientV(0, 0, W, H, Color{ 26, 31, 40, 255 }, csgo::Bg);
    // logo
    float ly = H * 0.15f;
    const char* head = "COUNTER-STRIKE";
    float hw = (float)s->GetTextWide(FONT_LOGO, head), sw = (float)s->GetTextWide(FONT_LOGO, " HvH");
    s->DrawText(FONT_LOGO, head, W / 2 - (hw + sw) / 2, ly, csgo::Text);
    s->DrawText(FONT_LOGO, " HvH", W / 2 - (hw + sw) / 2 + hw, ly, csgo::Gold);

    // ---- the panel
    float pw = std::min(W - 48.f, 760.f), ph = 268, px = (W - pw) / 2, py = H * 0.36f;
    s->DrawFilledRect(px + 6, py + 8, pw, ph, Color{ 0, 0, 0, 90 });        // soft drop shadow
    s->DrawFilledRect(px, py, pw, ph, csgo::Panel);
    s->DrawFilledRect(px, py, pw, 4, csgo::Accent);
    float x = px + 28, y = py + 26;

    const bool failed = p.state == download::FAILED, cancelled = p.state == download::CANCELLED;
    s->DrawText(FONT_GO_TITLE, failed ? "Download failed" : cancelled ? "Download cancelled" : "Downloading content", x, y, csgo::Text);

    // real fraction: the archive's Content-Length when the mirror sends one, the shipped estimate otherwise
    double total = p.totalExpectedMB > 0 ? p.totalExpectedMB : 0;
    double frac = total > 0 ? std::min(1.0, p.totalMB / total) : 0.0;
    if (p.state == download::DONE) frac = 1.0;
    shownFrac += (frac - shownFrac) * 0.18;
    char pct[16]; snprintf(pct, sizeof(pct), "%d%%", (int)std::lround((p.state == download::RUNNING ? frac : shownFrac) * 100));
    s->DrawTextRight(FONT_GO_TITLE, pct, px + pw - 28, y, failed ? csgo::Red : csgo::Text);

    y += 44;
    const float bw = pw - 56, bh = 16;
    s->DrawFilledRect(x, y, bw, bh, Color{ 16, 18, 22, 255 });
    float fill = (float)(p.state == download::RUNNING ? frac : shownFrac) * bw;
    if (fill > 1) {
      s->DrawGradientV(x, y, fill, bh, failed ? Color{ 168, 52, 48, 255 } : Color{ 112, 182, 84, 255 },
                       failed ? csgo::Red : Color{ 78, 138, 58, 255 });
      // travelling highlight, the same trick the Panorama progress bar uses
      if (p.state == download::RUNNING) {
        float sx = x + std::fmod(t * 260.f, std::max(1.f, bw));
        float w2 = std::min(90.f, std::max(0.f, x + fill - sx));
        if (w2 > 1) s->DrawGradientH(sx, y, w2, bh, Color{ 255, 255, 255, 0 }, Color{ 255, 255, 255, 46 });
      }
    }
    for (int i = 1; i < 10; i++) s->DrawFilledRect(x + bw * i / 10.f, y, 1, bh, Color{ 0, 0, 0, 60 });   // ticks
    s->DrawOutlinedRect(x, y, bw, bh, Color{ 255, 255, 255, 16 });

    // ---- numbers: transferred / total on the left, speed on the right
    y += bh + 14;
    char have[32], want[32], right[64];
    SizeStr(have, sizeof(have), p.totalMB);
    SizeStr(want, sizeof(want), total);
    if (p.state == download::FAILED || p.state == download::CANCELLED) snprintf(right, sizeof(right), "%s of %s", have, want);
    else if (p.speedMBs > 0.05) snprintf(right, sizeof(right), "%s / %s      %.1f MB/s", have, want, p.speedMBs);
    else snprintf(right, sizeof(right), "%s / %s", have, want);
    s->DrawText(FONT_GO, right, x, y, csgo::Text);
    if (p.files > 0) {
      char left[48]; snprintf(left, sizeof(left), "%d files", p.files);
      s->DrawTextRight(FONT_GO, left, px + pw - 28, y, csgo::TextDim);
    }

    // ---- what is happening right now
    y += 30;
    s->DrawText(FONT_UI, "INSTALLING", x, y, csgo::TextFaint);
    y += 18;
    std::string line = p.state == download::FAILED ? p.error
                     : cancelled ? "The parts already downloaded are kept - press RETRY to continue."
                     : p.current;
    if (p.current.empty() && p.state == download::RUNNING) line = "Contacting the download mirror...";
    if (line.size() > 96) line = "..." + line.substr(line.size() - 93);
    s->DrawText(FONT_UI, line.c_str(), x, y, failed ? csgo::Red : csgo::TextDim);

    // ---- footer of the panel: one calm hint instead of console commands
    float by = py + ph - 58, bh2 = 34;
    s->DrawFilledRect(x, py + ph - 86, bw, 1, csgo::Alpha(csgo::Line, 0.6f));
    s->DrawText(FONT_UI, "Downloaded once and cached locally - every later start is instant.", x, by + 10, csgo::TextFaint);
    if (p.state == download::RUNNING) {
      if (Button("CANCEL", px + pw - 28 - 132, by, 132, bh2, csgo::PanelLight, csgo::PanelHover)) download::Cancel();
      if (!coreMissing && Button("SKIP", px + pw - 28 - 132 - 8 - 116, by, 116, bh2, csgo::PanelLight, csgo::PanelHover)) { download::Cancel(); skipped = true; }
    } else if (failed || cancelled) {
      if (Button("RETRY", px + pw - 28 - 132, by, 132, bh2, csgo::Green, csgo::GreenHover, true)) download::Start();
      if (Button("SKIP", px + pw - 28 - 132 - 8 - 116, by, 116, bh2, csgo::PanelLight, csgo::PanelHover)) skipped = true;
    }

    // spinner next to the title while the mirror is being contacted
    if (p.state == download::RUNNING && p.totalMB < 0.01)
      for (int i = 0; i < 8; i++) {
        float a = t * 4.f + (float)i * 0.785f;
        s->DrawFilledCircle(px + pw - 60 + std::cos(a) * 8, py + 34 + std::sin(a) * 8, 2.2f, csgo::Alpha(csgo::Text, (i + 1) / 8.f));
      }
    EndDrawing();
    if (skipped) { if (coreMissing) download::DismissCoreHint(); break; }
  }
  download::Shutdown();
  srcfs::Invalidate();
  return !skipped;
}
