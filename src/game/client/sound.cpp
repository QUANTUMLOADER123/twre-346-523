// sound.cpp - CSoundEmitterSystem: game sounds. Original Counter-Strike 1.6 .wav files are used when the player provides
// them (own CS 1.6 install via snd_gamedir, or copied into ./sound/), everything else is synthesized procedurally.
#include "cl_main.hpp"
#include "../shared/soundemitter.hpp"
#include "../shared/weapon_parse.hpp"
#include "../shared/cs_player.hpp"
#include "../../engine/random.hpp"
#include "raylib.h"
#include <unordered_map>
#include <vector>
#include <string>
#include <cmath>
#include <cstring>
#include <algorithm>
#include <utility>
#include <filesystem>
#include <cstdlib>

ConVar volume("volume", "0.6", FCVAR_ARCHIVE, "Sound volume", true, 0, true, 1);
ConVar snd_musicvolume("snd_musicvolume", "0.5", FCVAR_ARCHIVE, "Music volume", true, 0, true, 1);
ConVar snd_footsteps("snd_footsteps", "1", FCVAR_ARCHIVE, "Play footstep sounds");
ConVar snd_hitsound("snd_hitsound", "0", FCVAR_ARCHIVE, "Play a hit sound when you damage an enemy (0 off, 1 bell, 2 click)");

static constexpr int SR = 22050;
static bool s_ready = false;
static Vector s_listener; static Vector s_right(0, -1, 0);

// ------------------------------------------------------------------------------------------------
// tiny synthesizer
struct Synth {
  std::vector<float> s;
  explicit Synth(float seconds) : s((size_t)(seconds * SR), 0.f) {}
  size_t N() const { return s.size(); }
  static float t(size_t i) { return (float)i / SR; }
  uint32_t rng = 1234567;
  float noise() { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return (rng & 0xFFFFFF) / 8388608.f - 1.f; }
  // white noise burst with exponential decay
  void Noise(float amp, float decay, float start = 0, float attack = 0.002f) {
    for (size_t i = (size_t)(start * SR); i < N(); i++) { float tt = t(i) - start; float env = (tt < attack ? tt / attack : 1.f) * std::exp(-tt * decay); s[i] += noise() * amp * env; }
  }
  // sine with exponential frequency sweep f0 -> f1 and decay
  void Tone(float amp, float f0, float f1, float decay, float start = 0, float dur = 1e9f, int wave = 0) {
    double ph = 0;
    for (size_t i = (size_t)(start * SR); i < N(); i++) {
      float tt = t(i) - start; if (tt > dur) break;
      float f = f0 * std::pow(f1 / f0, std::min(1.f, tt / std::max(0.001f, std::min(dur, 1.f))));
      ph += 2.0 * M_PI * f / SR;
      float v = wave == 0 ? (float)std::sin(ph) : wave == 1 ? (std::sin(ph) > 0 ? 1.f : -1.f) : (float)(2.0 * (ph / (2 * M_PI) - std::floor(ph / (2 * M_PI) + 0.5)));
      float env = std::min(1.f, tt / 0.002f) * std::exp(-tt * decay);
      s[i] += v * amp * env;
    }
  }
  void LowPass(float cutoff, size_t from = 0) { float rc = 1.f / (2 * (float)M_PI * cutoff), a = (1.f / SR) / (rc + 1.f / SR); float y = 0; for (size_t i = from; i < N(); i++) { y += a * (s[i] - y); s[i] = y; } }
  void HighPass(float cutoff) { float rc = 1.f / (2 * (float)M_PI * cutoff), a = rc / (rc + 1.f / SR); float y = 0, px = 0; for (size_t i = 0; i < N(); i++) { float x = s[i]; y = a * (y + x - px); px = x; s[i] = y; } }
  void BandPass(float lo, float hi) { HighPass(lo); LowPass(hi); }
  void Distort(float drive) { for (auto& v : s) v = std::tanh(v * drive) / std::tanh(drive); }
  void Echo(float delay, float fb, int taps) { size_t d = (size_t)(delay * SR); for (int k = 0; k < taps; k++) for (size_t i = N() - 1; i >= d; i--) s[i] += s[i - d] * fb; }
  void Normalize(float peak = 0.9f) { float m = 0; for (float v : s) m = std::max(m, std::fabs(v)); if (m > 1e-6f) for (auto& v : s) v *= peak / m; }
  void FadeOut(float secs) { size_t n = std::min(N(), (size_t)(secs * SR)); for (size_t i = 0; i < n; i++) s[N() - 1 - i] *= (float)i / n; }
  void Mix(const Synth& o, float g, float start = 0) { size_t off = (size_t)(start * SR); for (size_t i = 0; i < o.N() && i + off < N(); i++) s[i + off] += o.s[i] * g; }
};

// ------------------------------------------------------------------------------------------------
// gunshot designer
static Synth Gunshot(int weaponId, bool silenced, uint32_t seed) {
  const CCSWeaponInfo* wi = GetWeaponInfo(weaponId);
  int type = wi ? wi->type : WEAPONTYPE_PISTOL;
  float len = 0.45f, body = 180, crack = 2500, decay = 18, tail = 6, boom = 0.6f;
  switch (type) {
    case WEAPONTYPE_PISTOL: len = 0.35f; body = 240; crack = 3200; decay = 26; tail = 9; boom = 0.45f; break;
    case WEAPONTYPE_SUBMACHINEGUN: len = 0.3f; body = 220; crack = 3000; decay = 30; tail = 11; boom = 0.4f; break;
    case WEAPONTYPE_RIFLE: len = 0.55f; body = 150; crack = 2400; decay = 20; tail = 6; boom = 0.75f; break;
    case WEAPONTYPE_SHOTGUN: len = 0.8f; body = 95; crack = 1500; decay = 12; tail = 4; boom = 1.f; break;
    case WEAPONTYPE_SNIPER_RIFLE: len = 1.4f; body = 90; crack = 1800; decay = 10; tail = 2.5f; boom = 1.f; break;
    case WEAPONTYPE_MACHINEGUN: len = 0.5f; body = 140; crack = 2200; decay = 20; tail = 6; boom = 0.8f; break;
  }
  if (weaponId == WEAPON_DEAGLE || weaponId == WEAPON_REVOLVER) { len = 0.7f; body = 120; crack = 2200; decay = 16; tail = 4; boom = 0.85f; }
  if (weaponId == WEAPON_AK47) { body = 130; crack = 2000; }
  if (weaponId == WEAPON_M4A1 || weaponId == WEAPON_FAMAS) { body = 170; crack = 2800; }
  if (weaponId == WEAPON_SSG08) { len = 1.0f; body = 130; crack = 2600; tail = 3.5f; }
  Synth s(len); s.rng = seed;
  if (silenced) {
    Synth n(0.25f); n.rng = seed; n.Noise(1.f, 30.f); n.BandPass(300, 1400);
    s = Synth(0.3f); s.Mix(n, 1.f); s.Tone(0.5f, 200, 90, 40); s.Normalize(0.5f);
    return s;
  }
  Synth c(len); c.rng = seed; c.Noise(1.f, decay * 2.5f); c.HighPass(crack * 0.4f); c.LowPass(crack * 2.f);        // mechanical crack
  Synth b(len); b.rng = seed * 7 + 1; b.Noise(1.f, decay); b.LowPass(body * 3.f); b.Distort(2.5f);                   // muzzle blast
  Synth tl(len); tl.rng = seed * 13 + 5; tl.Noise(0.6f, tail, 0.01f, 0.03f); tl.LowPass(700);                         // reverb tail
  s.Mix(c, 0.55f); s.Mix(b, 1.f * boom + 0.4f); s.Mix(tl, 0.5f);
  s.Tone(0.9f * boom, body * 1.4f, body * 0.5f, decay * 0.8f);                                                         // low thump
  s.Echo(0.09f, 0.18f, 2);
  s.Distort(1.6f); s.Normalize(0.95f); s.FadeOut(0.05f);
  return s;
}

static Synth Generate(const std::string& name, uint32_t seed) {
  auto starts = [&](const char* p) { return name.rfind(p, 0) == 0; };
  auto has = [&](const char* p) { return name.find(p) != std::string::npos; };
  if (starts("Weapon_") && (has(".Single") || has(".Silenced"))) {
    std::string alias = name.substr(7, name.find('.') - 7);
    int id = AliasToWeaponID(alias);
    return Gunshot(id ? id : WEAPON_GLOCK, has(".Silenced"), seed);
  }
  Synth s(0.3f); s.rng = seed;
  if (starts("Player.Step")) {
    s = Synth(0.14f); s.rng = seed;
    if (has("Sand")) { s.Noise(0.8f, 40.f, 0, 0.01f); s.BandPass(500, 3500); }
    else if (has("Metal")) { s.Noise(0.6f, 50.f); s.Tone(0.35f, 900 + (seed % 200), 850, 25); s.Tone(0.2f, 2300, 2200, 30); s.BandPass(300, 5000); }
    else if (has("Wood")) { s.Noise(0.7f, 45.f); s.Tone(0.6f, 160, 120, 40); s.LowPass(1500); }
    else if (has("Tile")) { s.Noise(0.8f, 70.f); s.Tone(0.3f, 1600, 1500, 60); s.BandPass(600, 6000); }
    else { s.Noise(0.8f, 55.f); s.Tone(0.4f, 140, 100, 45); s.BandPass(150, 2500); }
    s.Normalize(0.6f); return s;
  }
  if (name == "Player.Land" || name == "Player.FallDamage") { s = Synth(0.3f); s.Noise(1.f, 25.f); s.Tone(1.f, 110, 60, 15); s.LowPass(900); s.Normalize(name == "Player.Land" ? 0.5f : 0.9f); return s; }
  if (name == "Player.DamageHelmet") { s = Synth(0.5f); s.Noise(0.5f, 60.f); s.Tone(0.7f, 2900, 2800, 9); s.Tone(0.45f, 4300, 4200, 12); s.Tone(0.3f, 1700, 1650, 8); s.Normalize(0.85f); return s; }
  if (name == "Player.DamageHeadShot") { s = Synth(0.35f); s.Noise(1.f, 22.f); s.BandPass(250, 2600); s.Tone(0.5f, 240, 90, 18); s.Distort(2.f); s.Normalize(0.9f); return s; }
  if (name == "Player.DamageKevlar") { s = Synth(0.25f); s.Noise(1.f, 30.f); s.BandPass(400, 3000); s.Tone(0.4f, 300, 200, 30); s.Normalize(0.7f); return s; }
  if (name == "Player.DamageBody") { s = Synth(0.25f); s.Noise(1.f, 28.f); s.LowPass(900); s.Tone(0.7f, 150, 80, 22); s.Normalize(0.75f); return s; }
  if (name == "Player.Death") { s = Synth(0.6f); s.Tone(0.6f, 260, 120, 5, 0, 0.5f, 2); s.Noise(0.4f, 8.f); s.LowPass(1100); s.Normalize(0.6f); return s; }
  if (name == "Player.PickupWeapon" || name == "Buy.Item" || name == "Weapon.Deploy") { s = Synth(0.25f); s.Noise(0.8f, 60.f); s.BandPass(800, 6000); s.Noise(0.8f, 60.f, 0.09f); s.BandPass(600, 7000); s.Tone(0.3f, 1200, 1100, 50, 0.09f); s.Normalize(0.55f); return s; }
  if (name == "Buy.Fail") { s = Synth(0.25f); s.Tone(0.5f, 220, 210, 6, 0, 0.2f, 1); s.LowPass(1200); s.Normalize(0.4f); return s; }
  if (name == "Weapon.DryFire" || name == "Default.Zoom" || name == "c4.click") { s = Synth(0.08f); s.Noise(1.f, 120.f); s.BandPass(1500, 8000); s.Normalize(0.5f); return s; }
  if (name == "Weapon.Reload" || name == "Weapon.ClipOut" || name == "Weapon.ClipIn") { s = Synth(0.3f); s.Noise(0.8f, 50.f); s.Tone(0.4f, 700, 600, 40); s.BandPass(400, 5000); s.Normalize(0.5f); return s; }
  if (name == "Weapon.BoltPull") { s = Synth(0.4f); s.Noise(0.8f, 30.f); s.BandPass(900, 6000); s.Noise(0.9f, 70.f, 0.22f); s.BandPass(500, 6000); s.Normalize(0.55f); return s; }
  if (name == "Weapon_Shotgun.InsertShell") { s = Synth(0.15f); s.Noise(0.8f, 60.f); s.Tone(0.5f, 500, 400, 50); s.BandPass(300, 4000); s.Normalize(0.5f); return s; }
  if (name == "Weapon_Revolver.Prepare") { s = Synth(0.2f); s.Noise(0.8f, 70.f); s.Tone(0.5f, 1800, 1700, 40); s.BandPass(800, 7000); s.Normalize(0.5f); return s; }
  if (starts("Weapon_Silencer")) { s = Synth(0.5f); for (int i = 0; i < 5; i++) s.Noise(0.4f, 80.f, i * 0.08f); s.BandPass(1200, 7000); s.Normalize(0.4f); return s; }
  if (name == "Weapon_Knife.Slash") { s = Synth(0.3f); s.Noise(1.f, 9.f, 0, 0.08f); s.BandPass(1500, 5000); s.Normalize(0.5f); return s; }
  if (name == "Weapon_Knife.Hit" || name == "Weapon_Knife.Stab") { s = Synth(0.35f); s.Noise(1.f, 25.f); s.LowPass(1200); s.Tone(0.8f, 180, 70, 14); s.Distort(2.f); s.Normalize(0.8f); return s; }
  if (name == "Weapon_Knife.HitWall") { s = Synth(0.4f); s.Noise(0.6f, 40.f); s.Tone(0.6f, 2400, 2300, 14); s.Tone(0.4f, 3700, 3600, 18); s.Normalize(0.6f); return s; }
  if (name == "Weapon_Knife.Deploy") { s = Synth(0.4f); s.Noise(0.5f, 10.f, 0, 0.1f); s.BandPass(3000, 8000); s.Tone(0.3f, 3200, 3600, 8); s.Normalize(0.4f); return s; }
  if (name == "Weapon_Grenade.PullPin") { s = Synth(0.25f); s.Noise(0.6f, 50.f); s.Tone(0.5f, 2600, 2500, 25); s.BandPass(1000, 8000); s.Normalize(0.45f); return s; }
  if (name == "Weapon_Grenade.Throw") { s = Synth(0.3f); s.Noise(1.f, 12.f, 0, 0.06f); s.BandPass(600, 3000); s.Normalize(0.4f); return s; }
  if (has(".Bounce")) { s = Synth(0.2f); s.Noise(0.6f, 60.f); s.Tone(0.6f, 1400 + (seed % 300), 1300, 30); s.Tone(0.3f, 2900, 2800, 35); s.Normalize(0.55f); return s; }
  if (name == "BaseGrenade.Explode" || name == "c4.explode") {
    float len = name == "c4.explode" ? 3.f : 2.f;
    s = Synth(len); s.Noise(1.f, 3.f, 0, 0.004f); s.LowPass(500); s.Distort(3.f);
    Synth c(len); c.rng = seed; c.Noise(1.f, 14.f); c.BandPass(600, 4000); s.Mix(c, 0.6f);
    s.Tone(1.f, 70, 30, 2.5f); s.Echo(0.17f, 0.3f, 3); s.Normalize(1.f); s.FadeOut(0.3f); return s;
  }
  if (name == "Flashbang.Explode") { s = Synth(1.2f); s.Noise(1.f, 20.f, 0, 0.002f); s.BandPass(800, 9000); s.Distort(3.f); s.Tone(0.5f, 120, 50, 8); s.Echo(0.12f, 0.25f, 2); s.Normalize(1.f); return s; }
  if (name == "Player.FlashRing") { s = Synth(3.f); s.Tone(0.5f, 3500, 3480, 0.9f); s.Normalize(0.35f); s.FadeOut(1.5f); return s; }
  if (name == "BaseSmokeEffect.Sound") { s = Synth(2.5f); s.Noise(1.f, 1.2f, 0, 0.05f); s.BandPass(1500, 7000); s.Normalize(0.5f); s.FadeOut(1.f); return s; }
  if (name == "c4.beep") { s = Synth(0.14f); s.Tone(0.8f, 2100, 2100, 6, 0, 0.12f); s.Normalize(0.55f); s.FadeOut(0.02f); return s; }
  if (name == "c4.plant" || name == "c4.disarmstart" || name == "c4.disarmfinish") { s = Synth(0.3f); s.Noise(0.6f, 40.f); s.Tone(0.5f, 1600, 1500, 20); s.Tone(0.4f, 900, 900, 20, 0.12f); s.BandPass(400, 7000); s.Normalize(0.55f); return s; }
  if (name == "c4.keypress") { s = Synth(0.09f); s.Tone(0.6f, 1300 + (seed % 5) * 150.f, 1300 + (seed % 5) * 150.f, 25); s.Normalize(0.4f); return s; }
  if (starts("Radio.")) {
    // radio squelch + a short melodic cue (there are no voice samples)
    s = Synth(1.1f); s.Noise(0.7f, 25.f); s.BandPass(800, 4000);
    float base = has("CTWin") ? 520.f : has("TerWin") ? 392.f : has("BombPlanted") ? 660.f : has("BombDef") ? 587.f : has("LetsGo") || has("Go") ? 494.f : 440.f;
    s.Tone(0.35f, base, base, 3.f, 0.12f, 0.18f, 1); s.Tone(0.35f, base * 1.25f, base * 1.25f, 3.f, 0.32f, 0.18f, 1); s.Tone(0.35f, base * (has("TerWin") || has("BombPlanted") ? 0.84f : 1.5f), base * 1.5f, 3.f, 0.52f, 0.3f, 1);
    s.BandPass(300, 3200); s.Noise(0.5f, 30.f, 0.9f); s.Normalize(0.5f); return s;
  }
  if (name == "UI.Click" || name == "UI.Rollover") { s = Synth(0.05f); s.Tone(0.5f, name == "UI.Click" ? 1500.f : 2400.f, 1200, 60); s.Normalize(name == "UI.Click" ? 0.35f : 0.15f); return s; }
  if (name == "Cheat.Hitsound") { s = Synth(0.35f); s.Tone(0.6f, 2000, 2000, 14); s.Tone(0.4f, 3000, 3000, 18); s.Tone(0.2f, 5100, 5100, 24); s.Normalize(0.6f); return s; } // "bell"
  if (name == "Cheat.Hitsound2") { s = Synth(0.08f); s.Noise(0.7f, 90.f); s.Tone(0.6f, 900, 700, 60); s.Normalize(0.5f); return s; }
  if (name == "Hud.Hint") { s = Synth(0.3f); s.Tone(0.4f, 880, 880, 10); s.Tone(0.3f, 1320, 1320, 10, 0.1f); s.Normalize(0.3f); return s; }
  // fallback: soft click
  s = Synth(0.06f); s.Noise(0.6f, 100.f); s.Normalize(0.3f); return s;
}

// ------------------------------------------------------------------------------------------------
// original CS 1.6 sounds (bring your own): sound name -> candidate files (relative to <root>/sound/), every file that
// exists becomes a random variant. Valve's sounds are not shipped with this game (copyright) - point snd_gamedir at
// your own Half-Life / Counter-Strike 1.6 folder or copy its cstrike/sound + valve/sound folders into ./sound/.
ConVar snd_gamedir("snd_gamedir", "", FCVAR_ARCHIVE, "Your Counter-Strike 1.6 install (the 'Half-Life' folder) - original sounds are loaded from cstrike/sound and valve/sound");
ConVar snd_original("snd_original", "1", FCVAR_ARCHIVE, "Use original CS 1.6 .wav files when they are available (0 = always synthesized)");

static std::vector<std::string> SoundRoots() {
  std::vector<std::string> roots = { "sound" };
  auto addInstall = [&](const std::string& d) {
    if (d.empty()) return;
    for (const char* sub : { "/cstrike/sound", "/cstrike_downloads/sound", "/valve/sound", "/sound" }) roots.push_back(d + sub);
  };
  addInstall(snd_gamedir.GetString());
  // usual Steam locations
  addInstall("C:/Program Files (x86)/Steam/steamapps/common/Half-Life");
  addInstall("C:/Program Files/Steam/steamapps/common/Half-Life");
  addInstall("D:/SteamLibrary/steamapps/common/Half-Life");
  if (const char* h = std::getenv("HOME")) { addInstall(std::string(h) + "/.steam/steam/steamapps/common/Half-Life"); addInstall(std::string(h) + "/.local/share/Steam/steamapps/common/Half-Life"); }
  std::vector<std::string> out; std::error_code ec;
  for (auto& r : roots) if (std::filesystem::is_directory(r, ec)) out.push_back(r);
  return out;
}

static std::vector<std::string> OriginalFiles(const std::string& name) {
  static const std::unordered_map<std::string, std::vector<std::string>> map = {
    // guns (CS:GO-only guns use the closest CS 1.6 sound)
    { "Weapon_ak47.Single", { "weapons/ak47-1.wav", "weapons/ak47-2.wav" } },
    { "Weapon_m4a1.Single", { "weapons/m4a1_unsil-1.wav", "weapons/m4a1_unsil-2.wav" } },
    { "Weapon_m4a1_silencer.Single", { "weapons/m4a1_unsil-1.wav", "weapons/m4a1_unsil-2.wav" } },
    { "Weapon_m4a1_silencer.Silenced", { "weapons/m4a1-1.wav" } },
    { "Weapon_usp_silencer.Silenced", { "weapons/usp1.wav", "weapons/usp2.wav" } },
    { "Weapon_usp_silencer.Single", { "weapons/usp_unsil-1.wav" } },
    { "Weapon_glock.Single", { "weapons/glock18-2.wav", "weapons/glock18-1.wav" } },
    { "Weapon_deagle.Single", { "weapons/deagle-1.wav", "weapons/deagle-2.wav" } },
    { "Weapon_revolver.Single", { "weapons/deagle-2.wav" } },
    { "Weapon_p250.Single", { "weapons/p228-1.wav" } },
    { "Weapon_awp.Single", { "weapons/awp1.wav" } },
    { "Weapon_ssg08.Single", { "weapons/scout_fire-1.wav" } },
    { "Weapon_g3sg1.Single", { "weapons/g3sg1-1.wav" } },
    { "Weapon_scar20.Single", { "weapons/sg550-1.wav" } },
    { "Weapon_aug.Single", { "weapons/aug-1.wav" } },
    { "Weapon_sg556.Single", { "weapons/sg552-1.wav", "weapons/sg552-2.wav" } },
    { "Weapon_famas.Single", { "weapons/famas-1.wav", "weapons/famas-2.wav" } },
    { "Weapon_galilar.Single", { "weapons/galil-1.wav", "weapons/galil-2.wav" } },
    { "Weapon_mac10.Single", { "weapons/mac10-1.wav" } },
    { "Weapon_mp9.Single", { "weapons/tmp-1.wav", "weapons/tmp-2.wav" } },
    { "Weapon_ump45.Single", { "weapons/ump45-1.wav" } },
    { "Weapon_p90.Single", { "weapons/p90-1.wav" } },
    { "Weapon_nova.Single", { "weapons/m3-1.wav" } },
    { "Weapon_xm1014.Single", { "weapons/xm1014-1.wav" } },
    // weapon handling
    { "Weapon.Reload", { "weapons/clipout1.wav" } }, { "Weapon.ClipOut", { "weapons/clipout1.wav" } }, { "Weapon.ClipIn", { "weapons/clipin1.wav" } },
    { "Weapon.BoltPull", { "weapons/boltpull1.wav" } }, { "Weapon.DryFire", { "weapons/dryfire_rifle.wav", "weapons/dryfire_pistol.wav" } },
    { "Weapon.Deploy", { "items/gunpickup2.wav" } }, { "Player.PickupWeapon", { "items/gunpickup2.wav" } }, { "Buy.Item", { "items/gunpickup2.wav" } },
    { "Default.Zoom", { "weapons/zoom.wav" } }, { "Weapon_Shotgun.InsertShell", { "weapons/m3_insertshell.wav", "weapons/xm1014_insertshell.wav" } },
    { "Weapon_Silencer.On", { "weapons/m4a1_silencer_on.wav", "weapons/usp_silencer_on.wav" } }, { "Weapon_Silencer.Off", { "weapons/m4a1_silencer_off.wav", "weapons/usp_silencer_off.wav" } },
    { "Weapon_Revolver.Prepare", { "weapons/slideback1.wav" } },
    // knife
    { "Weapon_Knife.Slash", { "weapons/knife_slash1.wav", "weapons/knife_slash2.wav" } },
    { "Weapon_Knife.Hit", { "weapons/knife_hit1.wav", "weapons/knife_hit2.wav", "weapons/knife_hit3.wav", "weapons/knife_hit4.wav" } },
    { "Weapon_Knife.Stab", { "weapons/knife_stab.wav" } }, { "Weapon_Knife.HitWall", { "weapons/knife_hitwall1.wav" } }, { "Weapon_Knife.Deploy", { "weapons/knife_deploy1.wav" } },
    // grenades / bomb
    { "Weapon_Grenade.PullPin", { "weapons/pinpull.wav" } },
    { "HEGrenade.Bounce", { "weapons/he_bounce-1.wav", "weapons/grenade_hit1.wav", "weapons/grenade_hit2.wav", "weapons/grenade_hit3.wav" } },
    { "Flashbang.Bounce", { "weapons/grenade_hit1.wav", "weapons/grenade_hit2.wav", "weapons/grenade_hit3.wav" } },
    { "SmokeGrenade.Bounce", { "weapons/grenade_hit1.wav", "weapons/grenade_hit2.wav", "weapons/grenade_hit3.wav" } },
    { "BaseGrenade.Explode", { "weapons/hegrenade-1.wav", "weapons/hegrenade-2.wav", "weapons/explode3.wav", "weapons/explode4.wav", "weapons/explode5.wav" } },
    { "Flashbang.Explode", { "weapons/flashbang-1.wav", "weapons/flashbang-2.wav" } },
    { "BaseSmokeEffect.Sound", { "weapons/sg_explode.wav" } },
    { "c4.beep", { "weapons/c4_beep1.wav" } }, { "c4.plant", { "weapons/c4_plant.wav" } }, { "c4.click", { "weapons/c4_click.wav" } },
    { "c4.disarmstart", { "weapons/c4_disarm.wav" } }, { "c4.disarmfinish", { "weapons/c4_disarmed.wav" } }, { "c4.explode", { "weapons/c4_explode1.wav" } },
    { "c4.keypress", { "weapons/c4_click.wav" } },
    // player
    { "Player.StepConcrete", { "player/pl_step1.wav", "player/pl_step2.wav", "player/pl_step3.wav", "player/pl_step4.wav" } },
    { "Player.StepSand", { "player/pl_dirt1.wav", "player/pl_dirt2.wav", "player/pl_dirt3.wav", "player/pl_dirt4.wav" } },
    { "Player.StepMetal", { "player/pl_metal1.wav", "player/pl_metal2.wav", "player/pl_metal3.wav", "player/pl_metal4.wav" } },
    { "Player.StepTile", { "player/pl_tile1.wav", "player/pl_tile2.wav", "player/pl_tile3.wav", "player/pl_tile4.wav", "player/pl_tile5.wav" } },
    { "Player.StepWood", { "player/pl_step1.wav", "player/pl_step2.wav", "player/pl_step3.wav", "player/pl_step4.wav" } },
    { "Player.Land", { "player/pl_jumpland2.wav" } }, { "Player.FallDamage", { "player/pl_fallpain3.wav", "player/pl_fallpain2.wav" } },
    { "Player.DamageHelmet", { "player/bhit_helmet-1.wav" } }, { "Player.DamageKevlar", { "player/bhit_kevlar-1.wav" } },
    { "Player.DamageHeadShot", { "player/headshot1.wav", "player/headshot2.wav", "player/headshot3.wav" } },
    { "Player.DamageBody", { "player/bhit_flesh-1.wav", "player/bhit_flesh-2.wav", "player/bhit_flesh-3.wav" } },
    { "Player.Death", { "player/die1.wav", "player/die2.wav", "player/die3.wav", "player/death6.wav" } },
    // radio / ui
    { "Radio.CTWin", { "radio/ctwin.wav" } }, { "Radio.TerWin", { "radio/terwin.wav" } }, { "Radio.RoundDraw", { "radio/rounddraw.wav" } },
    { "Radio.BombPlanted", { "radio/bombpl.wav" } }, { "Radio.BombDefused", { "radio/bombdef.wav" } }, { "Radio.TargetBombed", { "radio/terwin.wav" } },
    { "Radio.LetsGo", { "radio/letsgo.wav", "radio/locknload.wav", "radio/moveout.wav", "radio/com_go.wav" } },
    { "Buy.Fail", { "common/wpn_denyselect.wav" } }, { "UI.Click", { "common/wpn_hudoff.wav", "common/wpn_select.wav" } },
  };
  std::vector<std::string> found;
  if (!snd_original.GetBool()) return found;
  auto it = map.find(name); if (it == map.end()) return found;
  static std::vector<std::string> roots; static std::string rootsFor = "\x01";
  if (rootsFor != snd_gamedir.GetString()) { roots = SoundRoots(); rootsFor = snd_gamedir.GetString(); }
  std::error_code ec;
  for (const std::string& f : it->second)
    for (const std::string& r : roots) { std::string p = r + "/" + f; if (std::filesystem::is_regular_file(p, ec)) { found.push_back(p); break; } }
  return found;
}

struct SoundEntry { std::vector<Sound> variants; std::vector<Sound> aliases; int next = 0; float maxDist = 1500; };
static std::unordered_map<std::string, SoundEntry> s_sounds;

static Sound MakeSound(const Synth& syn) {
  Wave w{}; w.frameCount = (unsigned)syn.N(); w.sampleRate = SR; w.sampleSize = 16; w.channels = 1;
  short* data = (short*)MemAlloc((unsigned)(syn.N() * sizeof(short)));
  for (size_t i = 0; i < syn.N(); i++) data[i] = (short)(Clamp(syn.s[i], -1.f, 1.f) * 32000.f);
  w.data = data;
  Sound snd = LoadSoundFromWave(w);
  UnloadWave(w);
  return snd;
}

static SoundEntry* GetSound(const std::string& name) {
  auto it = s_sounds.find(name);
  if (it != s_sounds.end()) return &it->second;
  SoundEntry e;
  for (const std::string& f : OriginalFiles(name)) { Sound snd = LoadSound(f.c_str()); if (snd.frameCount > 0) e.variants.push_back(snd); }
  if (e.variants.empty()) {
    int variants = (name.rfind("Player.Step", 0) == 0) ? 4 : (name.find(".Bounce") != std::string::npos ? 2 : 1);
    for (int v = 0; v < variants; v++) e.variants.push_back(MakeSound(Generate(name, 1234u + v * 7919u + (uint32_t)std::hash<std::string>()(name))));
  }
  for (auto& base : e.variants) for (int k = 0; k < 5; k++) e.aliases.push_back(LoadSoundAlias(base));
  if (name.find(".Single") != std::string::npos) e.maxDist = 3200;
  if (name.find("Explode") != std::string::npos || name.find("explode") != std::string::npos) e.maxDist = 4000;
  if (name.find("AWP") != std::string::npos || name.find("awp") != std::string::npos) e.maxDist = 4500;
  if (name.rfind("Player.Step", 0) == 0) e.maxDist = 1100;
  if (name.find(".Silenced") != std::string::npos) e.maxDist = 900;
  if (name == "c4.beep") e.maxDist = 2000;
  return &(s_sounds[name] = std::move(e));
}

static void PlayEntry(SoundEntry* e, float vol, float pan, float pitch) {
  if (e->aliases.empty()) return;
  int per = (int)(e->aliases.size() / e->variants.size());
  int variant = e->variants.size() > 1 ? RandomInt(0, (int)e->variants.size() - 1) : 0;
  // round robin inside the variant's alias group
  for (int k = 0; k < per; k++) {
    Sound& s = e->aliases[variant * per + (e->next + k) % per];
    if (!IsSoundPlaying(s) || k == per - 1) {
      SetSoundVolume(s, Clamp(vol, 0.f, 1.f)); SetSoundPan(s, Clamp(pan, -1.f, 1.f)); SetSoundPitch(s, pitch);
      PlaySound(s); e->next = (e->next + k + 1) % per; return;
    }
  }
}

void EmitSound(const char* name, const Vector& origin, int entindex, float vol, float pitch) {
  if (!s_ready || !name) return;
  std::string n = name;
  if (n.rfind("Player.Step", 0) == 0 && !snd_footsteps.GetBool()) return;
  SoundEntry* e = GetSound(n);
  CCSPlayer* lp = g_pLocalPlayer;
  bool local = lp && entindex == lp->entindex() && lp->IsAlive();
  float v = vol, pan = 0;
  if (!local) {
    Vector d = origin - s_listener; float dist = d.Length();
    float att = Clamp(1.f - dist / e->maxDist, 0.f, 1.f); att *= att;
    if (att <= 0.003f) return;
    v *= att;
    if (dist > 1) pan = Clamp(d.Normalized().Dot(s_right), -1.f, 1.f) * 0.7f;
  }
  float p = pitch * (n.rfind("Player.Step", 0) == 0 ? RandomFloat(0.9f, 1.1f) : RandomFloat(0.97f, 1.03f));
  PlayEntry(e, v, pan, p);
}
void EmitSound2D(const char* name, float vol, float pitch) { if (!s_ready || !name) return; PlayEntry(GetSound(name), vol, 0, pitch); }

static ConCommand cmd_play("play", [](const CCommand& a) { if (a.ArgC() > 1) EmitSound2D(a.Arg(1)); }, "Play a sound.");
static void S_FlushSounds() {
  for (auto& [k, e] : s_sounds) { for (auto& al : e.aliases) { StopSound(al); UnloadSoundAlias(al); } for (auto& v : e.variants) UnloadSound(v); }
  s_sounds.clear();
}
static ConCommand cmd_snd_restart("snd_restart", [](const CCommand&) {
  if (s_ready) S_FlushSounds();
  std::vector<std::string> r = SoundRoots();
  Msg("Sound: %d sound folder(s) found%s\n", (int)r.size(), r.empty() ? " - using synthesized sounds" : "");
  for (auto& d : r) Msg("  %s\n", d.c_str());
  int have = 0; for (const char* n : { "Weapon_ak47.Single", "Weapon_awp.Single", "Player.StepConcrete", "Radio.CTWin" }) have += !OriginalFiles(n).empty();
  Msg("Sound: original files found for %d/4 test sounds (ak47, awp, footsteps, radio)\n", have);
}, "Reload all sounds (after setting snd_gamedir or copying original .wav files into ./sound/)");

static void OnVolumeChanged(ConVar* v, const char*) { if (s_ready) SetMasterVolume(v->GetFloat()); }

void S_Init() {
#ifndef CS_HEADLESS
  InitAudioDevice();
  s_ready = IsAudioDeviceReady();
  if (s_ready) {
    SetMasterVolume(volume.GetFloat());
    // pre-generate the common sounds so the first shot doesn't hitch
    const char* pre[] = { "Weapon_ak47.Single", "Weapon_m4a1.Single", "Weapon_deagle.Single", "Weapon_awp.Single", "Weapon_glock.Single", "Weapon_usp_silencer.Silenced",
      "Player.StepConcrete", "Player.StepSand", "Player.DamageHelmet", "Player.DamageHeadShot", "Player.DamageBody", "Player.DamageKevlar", "Weapon.Deploy", "UI.Click", "UI.Rollover" };
    for (auto* n : pre) GetSound(n);
  } else Warning("Sound: no audio device\n");
#endif
  (void)OnVolumeChanged;
}
void S_Shutdown() {
  if (!s_ready) return;
  for (auto& [k, e] : s_sounds) { for (auto& a : e.aliases) UnloadSoundAlias(a); for (auto& v : e.variants) UnloadSound(v); }
  s_sounds.clear();
  CloseAudioDevice(); s_ready = false;
}
void S_Update(const Vector& listener, const QAngle& angles) {
  s_listener = listener; Vector f, r, u; AngleVectors(angles, &f, &r, &u); s_right = r;
  static float lastVol = -1; if (s_ready && volume.GetFloat() != lastVol) { lastVol = volume.GetFloat(); SetMasterVolume(lastVol); }
}
void S_StopAll() { if (!s_ready) return; for (auto& [k, e] : s_sounds) for (auto& a : e.aliases) StopSound(a); }
