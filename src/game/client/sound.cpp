// sound.cpp - CSoundEmitterSystem: game sounds. Only real recorded sounds are played: Counter-Strike: Source (content
// downloaded with tools/get_css_content / css_download, resolved through CS:S's own soundscripts), else Counter-Strike
// 1.6 .wav files (own install via hl_gamedir, or ./sound/). There are no synthesized placeholder sounds.
#include "../../engine/hlfs.hpp"
#include "../../engine/srcfs.hpp"
#include "../../engine/keyvalues.hpp"
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
ConVar snd_css("snd_css", "1", FCVAR_ARCHIVE, "Use Counter-Strike: Source sounds when the CS:S content is available");
ConVar snd_hitsound("snd_hitsound", "0", FCVAR_ARCHIVE, "Play a hit sound when you damage an enemy (0 off, 1 bell, 2 click)");

static constexpr int SR = 22050;
static bool s_ready = false;
static Vector s_listener; static Vector s_right(0, -1, 0);

// ------------------------------------------------------------------------------------------------
// Counter-Strike: Source sounds: the game's own soundscripts (scripts/game_sounds*.txt) map sound names like
// "Weapon_AK47.Single" to .wav files; our sound names follow the same scheme, a few are translated below.
namespace {
std::unordered_map<std::string, std::vector<std::string>> s_scripts;   // lower-case name -> waves (relative to sound/)
bool s_scriptsLoaded = false;

std::string Lower(std::string x) { for (auto& c : x) c = (char)std::tolower((unsigned char)c); return x; }

void LoadSoundScripts() {
  if (s_scriptsLoaded) return;
  s_scriptsLoaded = true; s_scripts.clear();
  if (!srcfs::Available()) return;
  std::vector<std::string> files;
  std::vector<uint8_t> buf;
  if (srcfs::Read("scripts/game_sounds_manifest.txt", buf)) {
    auto kv = KeyValues::Parse((const char*)buf.data(), buf.size());
    if (kv) for (auto& root : kv->children) for (auto& c : root->children) if (!c->isBlock && c->value.find(".txt") != std::string::npos) files.push_back(c->value);
  }
  for (const char* f : { "scripts/game_sounds.txt", "scripts/game_sounds_weapons.txt", "scripts/game_sounds_physics.txt", "scripts/game_sounds_radio.txt", "scripts/game_sounds_world.txt" })
    if (std::find(files.begin(), files.end(), f) == files.end()) files.push_back(f);
  for (const std::string& f : files) {
    if (!srcfs::Read(f, buf)) continue;
    auto kv = KeyValues::Parse((const char*)buf.data(), buf.size());
    if (!kv) continue;
    for (auto& e : kv->children) {
      if (!e->isBlock) continue;
      std::vector<std::string> waves;
      auto add = [&](std::string w) {
        // strip Source's sound flags (")weapons/..", "^", "*", "#", "@", "<", ">", "!", "?", "~")
        while (!w.empty() && std::strchr(")(^*#@<>!?~&$`+%", w[0])) w.erase(0, 1);
        for (auto& c : w) if (c == '\\') c = '/';
        if (!w.empty()) waves.push_back(w);
      };
      for (auto& c : e->children) {
        std::string k = Lower(c->key);
        if (k == "wave" && !c->isBlock) add(c->value);
        else if (k == "rndwave" && c->isBlock) for (auto& w : c->children) if (Lower(w->key) == "wave") add(w->value);
      }
      if (!waves.empty()) s_scripts[Lower(e->key)] = waves;
    }
  }
  Msg("Sound: %d CS:S sound scripts entries\n", (int)s_scripts.size());
}

// our name -> CS:S soundscript names / files ("file:" prefix) to try, in order
std::vector<std::string> CssNames(const std::string& name) {
  static const std::unordered_map<std::string, std::vector<std::string>> alias = {
    { "Weapon_m4a1_silencer.Silenced", { "Weapon_M4A1.Silenced" } }, { "Weapon_m4a1_silencer.Single", { "Weapon_M4A1.Single" } },
    { "Weapon_usp_silencer.Silenced", { "Weapon_USP.SilencedShot" } }, { "Weapon_usp_silencer.Single", { "Weapon_USP.Single" } },
    { "Weapon_revolver.Single", { "Weapon_DEagle.Single" } }, { "Weapon_p250.Single", { "Weapon_P228.Single" } },
    { "Weapon_ssg08.Single", { "Weapon_Scout.Single" } }, { "Weapon_scar20.Single", { "Weapon_SG550.Single" } },
    { "Weapon_sg556.Single", { "Weapon_SG552.Single" } }, { "Weapon_galilar.Single", { "Weapon_Galil.Single" } },
    { "Weapon_mp9.Single", { "Weapon_TMP.Single" } }, { "Weapon_nova.Single", { "Weapon_M3.Single" } },
    { "Weapon_mp7.Single", { "Weapon_MP5Navy.Single" } }, { "Weapon_tec9.Single", { "Weapon_Glock.Single" } },
    { "Weapon_cz75a.Single", { "Weapon_FiveSeven.Single" } }, { "Weapon_hkp2000.Single", { "Weapon_USP.Single" } },
    { "Weapon_bizon.Single", { "Weapon_MP5Navy.Single" } }, { "Weapon_negev.Single", { "Weapon_M249.Single" } },
    { "Weapon_mag7.Single", { "Weapon_M3.Single" } }, { "Weapon_sawedoff.Single", { "Weapon_M3.Single" } },
    { "Weapon.Reload", { "Weapon_AK47.Clipout" } }, { "Weapon.ClipOut", { "Weapon_AK47.Clipout" } }, { "Weapon.ClipIn", { "Weapon_AK47.Clipin" } },
    { "Weapon.BoltPull", { "Weapon_AK47.BoltPull" } }, { "Weapon.DryFire", { "Default.ClipEmpty_Rifle", "file:weapons/clipempty_rifle.wav" } },
    { "Weapon.Deploy", { "Player.PickupWeapon", "file:items/itempickup.wav" } }, { "Buy.Item", { "Player.PickupWeapon", "file:items/itempickup.wav" } },
    { "Buy.Fail", { "file:buttons/weapon_cant_buy.wav", "Player.UseDeny" } },
    { "Weapon_Shotgun.InsertShell", { "Weapon_M3.Insertshell", "Weapon_XM1014.InsertShell" } },
    { "Weapon_Silencer.On", { "Weapon_M4A1.Silencer_On", "Weapon_USP.AttachSilencer" } }, { "Weapon_Silencer.Off", { "Weapon_M4A1.Silencer_Off", "Weapon_USP.DetachSilencer" } },
    { "Weapon_Revolver.Prepare", { "Weapon_DEagle.Slideback" } },
    { "Weapon_Grenade.PullPin", { "Default.PullPin_Grenade", "file:weapons/pinpull.wav" } },
    { "Weapon_Grenade.Throw", { "file:weapons/knife/knife_slash1.wav" } },
    { "BaseSmokeEffect.Sound", { "file:weapons/smokegrenade/sg_explode.wav" } },
    { "c4.beep", { "file:weapons/c4/c4_beep1.wav" } }, { "c4.keypress", { "c4.click" } },
    { "Player.StepConcrete", { "Concrete.StepLeft", "Concrete.StepRight" } }, { "Player.StepSand", { "Sand.StepLeft", "Sand.StepRight" } },
    { "Player.StepMetal", { "SolidMetal.StepLeft", "SolidMetal.StepRight" } }, { "Player.StepTile", { "Tile.StepLeft", "Tile.StepRight" } },
    { "Player.StepWood", { "Wood.StepLeft", "Wood.StepRight" } }, { "Player.StepDirt", { "Dirt.StepLeft", "Dirt.StepRight" } },
    { "Player.StepGrass", { "Grass.StepLeft", "Grass.StepRight" } }, { "Player.StepGravel", { "Gravel.StepLeft", "Gravel.StepRight" } },
    { "Player.Land", { "Concrete.StepLeft" } },
    { "Player.DamageBody", { "Flesh.BulletImpact" } },
    { "Player.FlashRing", { "file:weapons/flashbang/flashbang_explode2.wav" } },
    { "Radio.CTWin", { "file:radio/ctwin.wav" } }, { "Radio.TerWin", { "file:radio/terwin.wav" } }, { "Radio.RoundDraw", { "file:radio/rounddraw.wav" } },
    { "Radio.BombPlanted", { "file:radio/bombpl.wav" } }, { "Radio.BombDefused", { "file:radio/bombdef.wav" } }, { "Radio.TargetBombed", { "file:radio/terwin.wav" } },
    { "Radio.LetsGo", { "file:radio/letsgo.wav", "file:radio/locknload.wav", "file:radio/moveout.wav", "file:radio/com_go.wav" } },
    { "UI.Click", { "file:ui/buttonclick.wav" } }, { "UI.Rollover", { "file:ui/buttonrollover.wav" } },
    { "Hud.Hint", { "Hud.Hint", "file:ui/hint.wav" } },
    // aimwhere hit sounds: real samples instead of beeps
    { "Cheat.Hitsound", { "file:buttons/bell1.wav" } }, { "Cheat.Hitsound2", { "file:player/kevlar1.wav" } },
  };
  auto it = alias.find(name);
  if (it != alias.end()) return it->second;
  std::vector<std::string> out{ name };
  // unknown gun: closest CS:S gun of the same class
  if (name.rfind("Weapon_", 0) == 0 && name.find(".Single") != std::string::npos) {
    const CCSWeaponInfo* wi = GetWeaponInfo(AliasToWeaponID(name.substr(7, name.find('.') - 7)));
    int type = wi ? wi->type : WEAPONTYPE_PISTOL;
    out.push_back(type == WEAPONTYPE_RIFLE ? "Weapon_AK47.Single" : type == WEAPONTYPE_SNIPER_RIFLE ? "Weapon_AWP.Single" : type == WEAPONTYPE_SHOTGUN ? "Weapon_M3.Single" :
                  type == WEAPONTYPE_SUBMACHINEGUN ? "Weapon_MP5Navy.Single" : type == WEAPONTYPE_MACHINEGUN ? "Weapon_M249.Single" : "Weapon_P228.Single");
  }
  return out;
}

// CS:S wave files for one of our sound names
std::vector<std::string> CssFiles(const std::string& name) {
  std::vector<std::string> out;
  if (!snd_css.GetBool() || !srcfs::Available()) return out;
  LoadSoundScripts();
  for (const std::string& n : CssNames(name)) {
    if (n.rfind("file:", 0) == 0) { if (srcfs::Exists("sound/" + n.substr(5))) out.push_back(n.substr(5)); continue; }
    auto it = s_scripts.find(Lower(n)); if (it == s_scripts.end()) continue;
    for (auto& w : it->second) if (srcfs::Exists("sound/" + w)) out.push_back(w);
    if (!out.empty() && name.rfind("Player.Step", 0) != 0) break;   // first script that has files (steps merge left + right)
  }
  return out;
}
} // namespace

// ------------------------------------------------------------------------------------------------
// original CS 1.6 sounds (bring your own): sound name -> candidate files (relative to <root>/sound/), every file that
// exists becomes a random variant. Valve's sounds are not shipped with this game (copyright) - point snd_gamedir at
// your own Half-Life / Counter-Strike 1.6 folder or copy its cstrike/sound + valve/sound folders into ./sound/.
ConVar snd_gamedir("snd_gamedir", "", FCVAR_ARCHIVE, "Old name of hl_gamedir (still works): your Half-Life folder");
ConVar snd_original("snd_original", "1", FCVAR_ARCHIVE, "Use original CS 1.6 .wav files when they are available (and CS:S sounds are not)");

// ./sound (loose copies) first, then <cstrike|valve>/sound of the CS 1.6 install (see engine/hlfs)
static std::vector<std::string> SoundRoots() {
  std::vector<std::string> out; std::error_code ec;
  if (std::filesystem::is_directory("sound", ec)) out.push_back("sound");
  for (const std::string& d : hlfs::GameDirs()) { std::string r = hlfs::ResolveCase(d + "/sound"); if (!r.empty()) out.push_back(r); }
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
  std::string key = std::string(hl_gamedir.GetString()) + "|" + snd_gamedir.GetString() + "|" + hl_content.GetString();
  if (rootsFor != key) { hlfs::Invalidate(); roots = SoundRoots(); rootsFor = key; }
  for (const std::string& f : it->second)
    for (const std::string& r : roots) { std::string p = hlfs::ResolveCase(r + "/" + f); if (!p.empty()) { found.push_back(p); break; } }
  return found;
}

struct SoundEntry { std::vector<Sound> variants; std::vector<Sound> aliases; int next = 0; float maxDist = 1500; };
static std::unordered_map<std::string, SoundEntry> s_sounds;

static SoundEntry* GetSound(const std::string& name) {
  auto it = s_sounds.find(name);
  if (it != s_sounds.end()) return &it->second;
  SoundEntry e;
  // 1. Counter-Strike: Source  2. Counter-Strike 1.6  3. nothing (no synthesized placeholder sounds)
  for (const std::string& f : CssFiles(name)) {
    std::vector<uint8_t> data; if (!srcfs::Read("sound/" + f, data) || data.size() < 44) continue;
    std::string ext = f.size() > 4 ? Lower(f.substr(f.size() - 4)) : ".wav";
    Wave w = LoadWaveFromMemory(ext.c_str(), data.data(), (int)data.size());
    if (w.frameCount == 0) continue;
    Sound snd = LoadSoundFromWave(w); UnloadWave(w);
    if (snd.frameCount > 0) e.variants.push_back(snd);
  }
  if (e.variants.empty())
    for (const std::string& f : OriginalFiles(name)) { Sound snd = LoadSound(f.c_str()); if (snd.frameCount > 0) e.variants.push_back(snd); }
  if (e.variants.empty()) DevMsg("Sound: no file for %s\n", name.c_str());
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
  s_scriptsLoaded = false; srcfs::Invalidate();
  std::vector<std::string> r = SoundRoots();
  if (srcfs::Available()) Msg("Sound: CS:S content %s\n", srcfs::Root().c_str());
  Msg("Sound: %d CS 1.6 sound folder(s) found%s\n", (int)r.size(), (r.empty() && !srcfs::Available()) ? " - no sound files: run tools/get_css_content or css_download" : "");
  for (auto& d : r) Msg("  %s\n", d.c_str());
  int have = 0, haveCss = 0;
  for (const char* n : { "Weapon_ak47.Single", "Weapon_awp.Single", "Player.StepConcrete", "Radio.CTWin" }) { have += !OriginalFiles(n).empty(); haveCss += !CssFiles(n).empty(); }
  Msg("Sound: test sounds (ak47, awp, footsteps, radio) found: CS:S %d/4, CS 1.6 %d/4\n", haveCss, have);
}, "Reload all sounds (after setting snd_gamedir or copying original .wav files into ./sound/)");

static void OnVolumeChanged(ConVar* v, const char*) { if (s_ready) SetMasterVolume(v->GetFloat()); }

void S_Init() {
#ifndef CS_HEADLESS
  InitAudioDevice();
  s_ready = IsAudioDeviceReady();
  if (s_ready) {
    SetMasterVolume(volume.GetFloat());
    // preload the common sounds so the first shot doesn't hitch
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
