// host.cpp - CHost
#include "host.hpp"
#include "convar.hpp"
#include "enginetrace.hpp"
#include "random.hpp"
#include "../game/shared/cs_player.hpp"
#include "../game/shared/weapon_csbase.hpp"
#include "../game/shared/cs_gamerules.hpp"
#include "../game/shared/cs_projectiles.hpp"
#include "../game/shared/lagcompensation.hpp"
#include "../game/shared/gameevents.hpp"
#include "../game/shared/mapinfo.hpp"
#include "../game/server/cs_bot.hpp"
#include "../game/server/nav_mesh.hpp"
#include <cmath>
#include <algorithm>

ConVar sv_cheats("sv_cheats", "1", FCVAR_NOTIFY | FCVAR_REPLICATED, "Allow cheats on server");
ConVar host_timescale("host_timescale", "1.0", FCVAR_CHEAT | FCVAR_REPLICATED, "Prescale the clock by this amount.", true, 0.05f, true, 10);
ConVar host_framerate("host_framerate", "0", FCVAR_CHEAT, "Set to lock per-frame time elapse.");
ConVar name("name", "Player", FCVAR_ARCHIVE, "Current user name");
ConVar sv_maxdtshift("sv_maxdtshift", "15", FCVAR_REPLICATED, "Maximum number of ticks a client can shift its tickbase (doubletap)", true, 0, true, 16);
extern ConVar bot_quota;
extern ConVar sv_maxusrcmdprocessticks;

bool CheatsAllowed() { return sv_cheats.GetBool(); }

// ------------------------------------------------------------------------------------------------
CCSPlayer* Host_CreateLocalPlayer() {
  auto* p = new CCSPlayer();
  g_EntityList.Add(p, 1);
  p->m_bIsLocalPlayer = true;
  p->m_szPlayerName = name.GetString();
  p->m_iAccount = FindConVar("mp_startmoney")->GetInt();
  p->m_iTeamNum = TEAM_UNASSIGNED;
  p->m_lifeState = LIFE_DEAD;
  p->m_iObserverMode = OBS_MODE_ROAMING;
  p->m_MoveType = MOVETYPE_NOCLIP;
  g_pLocalPlayer = p;
  return p;
}

bool Host_NewGame(const std::string& map) {
  Host_Disconnect();
  std::string path = ResolveMapFile(map);
  if (!g_MapInfo.Load(path)) { Warning("map load failed: %s\n", path.c_str()); return false; }
  g_EngineTrace.entities = &g_EntityList.traceList;
  gpGlobals->tickcount = 0; gpGlobals->curtime = 0;
  gpGlobals->mapname = g_MapInfo.name.c_str();
  g_Host.mapname = map; g_Host.active = true; g_Host.accumulator = 0;
  g_NavMesh.Build();
  CSGameRules()->Init();
  Bot_Init();
  CCSPlayer* lp = Host_CreateLocalPlayer();
  // spectator camera at a spawn
  if (!g_MapInfo.spawns.empty()) { lp->m_vecOrigin = g_MapInfo.spawns[0].origin + Vector(0, 0, 200); lp->v_angle = QAngle(20, g_MapInfo.spawns[0].yaw, 0); }
  CL_LevelInit();
  IGameEvent ev("game_newmap"); ev.SetString("mapname", map); gameeventmanager->FireEvent(ev);
  Msg("Map: %s\n", map.c_str());
  return true;
}

void Host_Disconnect() {
  if (!g_Host.active) return;
  CL_LevelShutdown();
  Bot_KickAll();
  g_EntityList.Clear();
  g_pLocalPlayer = nullptr; g_pPlantedC4 = nullptr;
  GetActiveSmokes().clear();
  g_Host.active = false;
}

// ------------------------------------------------------------------------------------------------
static void RunPlayerCommand(CCSPlayer* p, CUserCmd& cmd) {
  float saved = gpGlobals->curtime;
  gpGlobals->curtime = TICKS_TO_TIME(p->m_nTickBase);   // weapons run on the player's tickbase
  p->PlayerRunCommand(cmd, gpGlobals->interval_per_tick);
  gpGlobals->curtime = saved;
}

void Host_RunTick() {
  gpGlobals->tickcount++;
  gpGlobals->curtime = ServerTime();
  gpGlobals->frametime = gpGlobals->interval_per_tick;
  g_EntityList.RebuildTraceList();

  // bot quota
  {
    int bots = 0; for (CCSPlayer* p : UTIL_GetPlayers()) if (p->IsBot()) bots++;
    if (bots < bot_quota.GetInt() && (gpGlobals->tickcount % 8) == 0) Bot_AddBot(0);
    else if (bots > bot_quota.GetInt()) {
      for (CCSPlayer* p : UTIL_GetPlayers()) if (p->IsBot() && !p->IsAlive()) { p->RemoveAllItems(true); p->Remove(); break; }
    }
  }

  // local client
  if (CCSPlayer* lp = g_pLocalPlayer) {
    CUserCmd cmd;
    cmd.command_number = ++g_Host.lastLocalCmd;
    cmd.tick_count = gpGlobals->tickcount;
    cmd.random_seed = MD5_PseudoRandom(cmd.command_number) & 0x7fffffff;
    CL_CreateMove(cmd);
    int maxShift = std::min(sv_maxdtshift.GetInt(), sv_maxusrcmdprocessticks.GetInt() - 1);
    if (cmd.tickbaseShift < 0 && lp->IsAlive() && lp->m_nTickbaseShift < maxShift) {
      // recharge: the command is held back - the server doesn't simulate us at all, so we freeze in place
      // (even mid-air) while the tickbase "owes" us ticks
      lp->m_nTickbaseShift++;
      lp->m_LastCmd = cmd;
      // nothing was simulated this tick: kill the render interpolation too, otherwise the camera would keep
      // lerping last tick's movement every tick (= the "walks forward and back" jitter while recharging)
      lp->m_vecOldOrigin = lp->m_vecOrigin;
    } else {
      RunPlayerCommand(lp, cmd);
      if (cmd.tickbaseShift > 0 && lp->IsAlive()) {
        // release: all owed ticks are simulated at once with the current input -> we teleport n ticks ahead
        int n = std::min(cmd.tickbaseShift, lp->m_nTickbaseShift);
        CUserCmd extra = cmd;
        extra.buttons &= ~(IN_ATTACK | IN_ATTACK2);
        for (int i = 0; i < n; i++) {
          extra.command_number = ++g_Host.lastLocalCmd;
          extra.random_seed = MD5_PseudoRandom(extra.command_number) & 0x7fffffff;
          extra.bSendPacket = (i == n - 1);
          RunPlayerCommand(lp, extra);
        }
        lp->m_nTickbaseShift -= n;
      }
    }
  }
  // bots
  for (CCSPlayer* p : UTIL_GetPlayers()) {
    if (!p->IsBot() || !p->m_pBot) continue;
    CUserCmd cmd; p->m_pBot->Upkeep(cmd);
    RunPlayerCommand(p, cmd);
  }
  // world entities
  for (size_t i = 0; i < g_EntityList.All().size(); i++) {
    C_BaseEntity* e = g_EntityList.All()[i];
    if (!e || e->m_bMarkedForDeletion || e->IsPlayer()) continue;
    e->PhysicsSimulate();
    if (!e->m_bMarkedForDeletion) e->Think();
  }
  CSGameRules()->Think();
  g_LagCompensation.FrameUpdatePostEntityThink();
  g_NavMesh.DecayDanger(gpGlobals->interval_per_tick);
  // purge expired smokes
  auto& sm = GetActiveSmokes();
  sm.erase(std::remove_if(sm.begin(), sm.end(), [](const SmokeVolume& s) { return ServerTime() > s.end; }), sm.end());
  g_EntityList.CleanupDeleted();
  CL_OnTickEnd();
}

void Host_Frame(float realDt) {
  gpGlobals->realtime += realDt;
  gpGlobals->absoluteframetime = realDt;
  gpGlobals->framecount++;
  Cbuf_Execute();
  if (!g_Host.active) return;
  float dt = host_framerate.GetFloat() > 0 ? host_framerate.GetFloat() : realDt;
  dt *= host_timescale.GetFloat();
  if (g_Host.paused) dt = 0;
  g_Host.accumulator += dt;
  int ticks = 0;
  while (g_Host.accumulator >= gpGlobals->interval_per_tick && ticks < 8) {
    g_Host.accumulator -= gpGlobals->interval_per_tick;
    Host_RunTick();
    ticks++;
    if (!g_Host.active) return;
  }
  if (ticks == 8) g_Host.accumulator = std::min(g_Host.accumulator, (double)gpGlobals->interval_per_tick);
  g_Host.ticksThisFrame = ticks;
  g_Host.frameAlpha = (float)(g_Host.accumulator / gpGlobals->interval_per_tick);
  gpGlobals->interpolation_amount = g_Host.frameAlpha;
}

// ------------------------------------------------------------------------------------------------
// server commands
static CCSPlayer* LP() { return g_pLocalPlayer; }
static bool RequireCheats() { if (!CheatsAllowed()) { Msg("Can't use cheat command in multiplayer, unless the server has sv_cheats set to 1.\n"); return false; } return true; }

static ConCommand cmd_map("map", [](const CCommand& a) {
  if (a.ArgC() < 2) { Msg("Usage: map <mapname>\nAvailable maps:\n"); for (auto& m : ListMaps()) Msg("  %s  (%s)\n", m.name.c_str(), m.title.c_str()); return; }
  Host_NewGame(a.Arg(1));
}, "Start playing on specified map.");
static ConCommand cmd_changelevel("changelevel", [](const CCommand& a) { if (a.ArgC() >= 2) Host_NewGame(a.Arg(1)); }, "Change server to the specified map");
static ConCommand cmd_disconnect("disconnect", [](const CCommand&) { Host_Disconnect(); }, "Disconnect game from server.");
static ConCommand cmd_maps("maps", [](const CCommand&) { for (auto& m : ListMaps()) Msg("%-16s %s - %s\n", m.name.c_str(), m.title.c_str(), m.desc.c_str()); }, "Displays list of maps.");
static ConCommand cmd_pause("pause", [](const CCommand&) { g_Host.paused = !g_Host.paused; Msg(g_Host.paused ? "Paused\n" : "Unpaused\n"); }, "Toggle the server pause state.");
static ConCommand cmd_status("status", [](const CCommand&) {
  Msg("hostname: Counter-Strike: HvH listen server\nmap     : %s\nplayers : %d\n# userid name                 team  hp  money  k/d  ping\n", g_Host.mapname.c_str(), (int)UTIL_GetPlayers().size());
  for (CCSPlayer* p : UTIL_GetPlayers()) Msg("# %5d %-20s %-4s %3d %6d %2d/%-2d %s\n", p->entindex(), p->GetPlayerName(), p->m_iTeamNum == TEAM_CT ? "CT" : p->m_iTeamNum == TEAM_TERRORIST ? "T" : "SPEC", p->m_iHealth, p->m_iAccount, p->m_iKills, p->m_iDeaths, p->IsBot() ? "BOT" : "0");
}, "Display map and connection status.");
static ConCommand cmd_jointeam("jointeam", [](const CCommand& a) {
  CCSPlayer* p = LP(); if (!p) return;
  int team = a.ArgI(1, 0);
  if (team == 0) { int t = 0, ct = 0; for (CCSPlayer* o : UTIL_GetPlayers()) { if (o == p) continue; if (o->m_iTeamNum == TEAM_TERRORIST) t++; else if (o->m_iTeamNum == TEAM_CT) ct++; } team = t <= ct ? TEAM_TERRORIST : TEAM_CT; }
  if (team == TEAM_SPECTATOR) { if (p->IsAlive()) { CTakeDamageInfo i; i.m_flDamage = 999; p->Event_Killed(i); } p->m_iTeamNum = TEAM_SPECTATOR; p->m_iObserverMode = OBS_MODE_ROAMING; return; }
  CSGameRules()->PlayerJoinTeam(p, team);
  if (p->m_iObserverMode == OBS_MODE_ROAMING && !p->IsAlive()) p->m_iObserverMode = OBS_MODE_IN_EYE;
}, "Choose a new team (1 spectator, 2 terrorists, 3 counter-terrorists, 0 auto)");
static ConCommand cmd_kill("kill", [](const CCommand&) {
  CCSPlayer* p = LP(); if (!p || !p->IsAlive()) return;
  CTakeDamageInfo info; info.m_flDamage = 1000; info.m_hAttacker = p; info.m_bitsDamageType = DMG_GENERIC; info.m_iWeaponID = WEAPON_NONE;
  p->OnTakeDamage(info);
}, "Kills the player with generic damage");
static ConCommand cmd_explode("explode", [](const CCommand&) { CCSPlayer* p = LP(); if (!p || !p->IsAlive()) return; CTakeDamageInfo info; info.m_flDamage = 1000; info.m_bitsDamageType = DMG_BLAST; p->OnTakeDamage(info); }, "Kills the player with explosive damage");
static ConCommand cmd_noclip("noclip", [](const CCommand&) {
  if (!RequireCheats()) return; CCSPlayer* p = LP(); if (!p || !p->IsAlive()) return;
  if (p->m_MoveType == MOVETYPE_NOCLIP) { p->m_MoveType = MOVETYPE_WALK; Msg("noclip OFF\n"); } else { p->m_MoveType = MOVETYPE_NOCLIP; Msg("noclip ON\n"); }
}, "Toggle. Player becomes non-solid and flies.", FCVAR_CHEAT);
static ConCommand cmd_god("god", [](const CCommand&) { if (!RequireCheats()) return; CCSPlayer* p = LP(); if (!p) return; p->m_fFlags ^= FL_GODMODE; Msg((p->m_fFlags & FL_GODMODE) ? "godmode ON\n" : "godmode OFF\n"); }, "Toggle. Player becomes invulnerable.", FCVAR_CHEAT);
static ConCommand cmd_setpos("setpos", [](const CCommand& a) {
  if (!RequireCheats()) return; CCSPlayer* p = LP(); if (!p || a.ArgC() < 3) return;
  p->m_vecOrigin = Vector(a.ArgF(1), a.ArgF(2), a.ArgC() > 3 ? a.ArgF(3) : p->m_vecOrigin.z);
  p->m_vecOldOrigin = p->m_vecOrigin; p->m_vecVelocity.Init();
}, "Move player to specified origin (must have sv_cheats).", FCVAR_CHEAT);
static ConCommand cmd_setang("setang", [](const CCommand& a) { CCSPlayer* p = LP(); if (!p || a.ArgC() < 3) return; p->v_angle = QAngle(a.ArgF(1), a.ArgF(2), 0); extern void CL_SetViewAngles(const QAngle&); CL_SetViewAngles(p->v_angle); }, "Snap player eyes to specified pitch yaw <roll:optional>");
static ConCommand cmd_getpos("getpos", [](const CCommand&) { CCSPlayer* p = LP(); if (!p) return; Msg("setpos %.2f %.2f %.2f;setang %.2f %.2f %.2f\n", p->m_vecOrigin.x, p->m_vecOrigin.y, p->m_vecOrigin.z, p->v_angle.x, p->v_angle.y, 0.f); }, "dump position and angles to the console");
static ConCommand cmd_give("give", [](const CCommand& a) { if (!RequireCheats()) return; CCSPlayer* p = LP(); if (!p || !p->IsAlive() || a.ArgC() < 2) return; if (C_WeaponCSBase* w = p->GiveNamedItem(a.Arg(1))) p->Weapon_Switch(w); else Msg("Unknown item %s\n", a.Arg(1)); }, "Give item to player.", FCVAR_CHEAT);
static ConCommand cmd_buy("buy", [](const CCommand& a) { CCSPlayer* p = LP(); if (!p || a.ArgC() < 2) return; for (int i = 1; i < a.ArgC(); i++) p->BuyItem(a.Arg(i)); }, "Buy an item: buy <ak47|m4a1|awp|vesthelm|...>");
static ConCommand cmd_drop("drop", [](const CCommand&) { CCSPlayer* p = LP(); if (!p || !p->IsAlive()) return; C_WeaponCSBase* w = p->GetActiveCSWeapon(); if (w && w->GetWeaponType() != WEAPONTYPE_KNIFE) p->Weapon_Drop(w, true); }, "Drop the current weapon");
static ConCommand cmd_impulse("impulse", [](const CCommand& a) {
  CCSPlayer* p = LP(); if (!p || !p->IsAlive()) return;
  if (a.ArgI(1) == 101 && RequireCheats()) { p->m_iAccount = 16000; p->m_ArmorValue = 100; p->m_bHasHelmet = true; for (auto* w : p->m_hMyWeapons) if (w && w->IsGun()) { w->m_iClip1 = w->GetCSWpnData().clipSize; w->m_iPrimaryReserveAmmoCount = w->GetCSWpnData().reserveAmmo; } }
}, "");
static ConCommand cmd_bot_add("bot_add", [](const CCommand& a) { bot_quota.SetValue(bot_quota.GetInt() + 1); Bot_AddBot(0, a.ArgC() > 1 ? a.Arg(1) : nullptr); }, "Add a bot.");
static ConCommand cmd_bot_add_t("bot_add_t", [](const CCommand& a) { bot_quota.SetValue(bot_quota.GetInt() + 1); Bot_AddBot(TEAM_TERRORIST, a.ArgC() > 1 ? a.Arg(1) : nullptr); }, "Add a bot to the terrorist team.");
static ConCommand cmd_bot_add_ct("bot_add_ct", [](const CCommand& a) { bot_quota.SetValue(bot_quota.GetInt() + 1); Bot_AddBot(TEAM_CT, a.ArgC() > 1 ? a.Arg(1) : nullptr); }, "Add a bot to the counter-terrorist team.");
static ConCommand cmd_bot_kick("bot_kick", [](const CCommand&) { bot_quota.SetValue(0); Bot_KickAll(); }, "Kick all bots.");
static ConCommand cmd_bot_kill("bot_kill", [](const CCommand&) { for (CCSPlayer* p : UTIL_GetPlayers(true)) if (p->IsBot()) { CTakeDamageInfo i; i.m_flDamage = 1000; p->OnTakeDamage(i); } }, "Kill all bots.");
static ConCommand cmd_endround("endround", [](const CCommand&) { if (!RequireCheats()) return; CSGameRules()->TerminateRound(3.f, Round_Draw); }, "End the current round.", FCVAR_CHEAT);
static ConCommand cmd_quit("quit", [](const CCommand&) { g_Host.wantQuit = true; }, "Exit the engine.");
static ConCommand cmd_exit("exit", [](const CCommand&) { g_Host.wantQuit = true; }, "Exit the engine.");

void Host_Init() {
  gpGlobals->interval_per_tick = 1.f / 64.f;
  Cmd_ExecFile("cfg/config.cfg", true);
  Cmd_ExecFile("cfg/autoexec.cfg", true);
}
