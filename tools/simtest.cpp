// simtest - headless server simulation test (bots only), prints round results
#include "../src/engine/host.hpp"
#include "../src/engine/convar.hpp"
#include "../src/game/shared/cs_player.hpp"
#include "../src/game/shared/weapon_csbase.hpp"
#include "../src/game/shared/cs_projectiles.hpp"
#include "../src/game/shared/cs_gamerules.hpp"
#include "../src/game/shared/gameevents.hpp"
#include "../src/game/server/cs_bot.hpp"
#include "../src/game/server/nav_mesh.hpp"
#include "../src/game/shared/mapinfo.hpp"
#include <cstdio>
#include <cstring>
#include <chrono>
#include <map>
// client stubs
void CL_CreateMove(CUserCmd& cmd) { cmd.viewangles = g_pLocalPlayer ? g_pLocalPlayer->v_angle : QAngle(); }
void CL_LevelInit() {}
void CL_LevelShutdown() {}
void CL_OnTickEnd() {}
void CL_SetViewAngles(const QAngle&) {}
void EmitSound(const char*, const Vector&, int, float, float) {}
void EmitSound2D(const char*, float, float) {}
void CCSPlayer::DrawModel(const CViewSetup&) {}
void CCSPlayer::DrawTranslucent(const CViewSetup&) {}
void C_WeaponCSBase::DrawModel(const CViewSetup&) {}
void CBaseCSGrenadeProjectile::DrawModel(const CViewSetup&) {}
void CBaseCSGrenadeProjectile::DrawTranslucent(const CViewSetup&) {}
void CSmokeGrenadeProjectile::DrawTranslucent(const CViewSetup&) {}
void CPlantedC4::DrawModel(const CViewSetup&) {}

int main(int argc, char** argv) {
  float seconds = argc > 1 ? atof(argv[1]) : 120.f;
  const char* map = argc > 2 ? argv[2] : "de_dust2";
  int hvh = argc > 3 ? atoi(argv[3]) : 0;
  Host_Init();
  Cmd_ExecuteString("bot_quota 10");
  Cmd_ExecuteString(std::string("bot_hvh ") + std::to_string(hvh));
  Cmd_ExecuteString("mp_freezetime 2");
  std::map<std::string, int> counts;
  gameeventmanager->AddListener(nullptr, [&](const IGameEvent& e) {
    counts[e.GetName()]++;
    if (e.IsEvent("player_death")) {
      CCSPlayer* v = UTIL_PlayerByIndex(e.GetInt("userid")); CCSPlayer* a = UTIL_PlayerByIndex(e.GetInt("attacker"));
      printf("[%7.2f] %s killed %s with %s%s%s\n", ServerTime(), a ? a->GetPlayerName() : "world", v ? v->GetPlayerName() : "?", e.GetString("weapon"), e.GetBool("headshot") ? " (HS)" : "", e.GetInt("penetrated") ? " (wallbang)" : "");
    }
    if (e.IsEvent("bomb_planted") || e.IsEvent("bomb_defused") || e.IsEvent("bomb_exploded")) printf("[%7.2f] %s\n", ServerTime(), e.GetName());
  });
  if (!Host_NewGame(map)) return 1;
  if (getenv("NAVDBG")) {
    float a[6]; sscanf(getenv("NAVDBG"), "%f,%f,%f,%f,%f,%f", &a[0], &a[1], &a[2], &a[3], &a[4], &a[5]);
    std::vector<Vector> path; std::vector<int> fl;
    int s = g_NavMesh.GetAreaIndex(Vector(a[0], a[1], a[2])), g = g_NavMesh.GetAreaIndex(Vector(a[3], a[4], a[5]));
    bool ok = g_NavMesh.ComputePath(Vector(a[0], a[1], a[2]), Vector(a[3], a[4], a[5]), path, &fl, 0);
    printf("start area %d goal area %d ok %d len %zu\n", s, g, ok, path.size());
    for (size_t i = 0; i < path.size(); i++) printf("  %.0f %.0f %.0f f%d\n", path[i].x, path[i].y, path[i].z, fl[i]);
    int w = g_MapInfo.gw; int cx = s % w, cy = s / w;
    for (int y = cy - 4; y <= cy + 4; y++) { for (int x = cx - 6; x <= cx + 6; x++) { int i = y * w + x; printf("%c%4.0f ", g_NavMesh.GetArea(i)->walkable ? (i == s ? '*' : ' ') : '#', g_NavMesh.GetArea(i)->z); } printf("   types:"); for (int x = cx - 6; x <= cx + 6; x++) printf("%d", g_MapInfo.cellType[y * w + x]); printf("\n"); }
    return 0;
  }
  auto t0 = std::chrono::steady_clock::now();
  int ticks = (int)(seconds * 64);
  for (int i = 0; i < ticks; i++) {
    Host_RunTick();
    if (getenv("C4DBG") && i % (64 * 10) == 0) for (CCSPlayer* p : UTIL_GetPlayers(true)) if (p->HasC4()) printf("  [c4] t=%.1f %s at %.0f %.0f %.0f state %s path %zu/%zu goal %.0f %.0f inzone %d\n", ServerTime(), p->GetPlayerName(), p->m_vecOrigin.x, p->m_vecOrigin.y, p->m_vecOrigin.z, BotStateName(p->m_pBot->m_iState), p->m_pBot->m_iPathIndex, p->m_pBot->m_Path.size(), p->m_pBot->m_vecGoal.x, p->m_pBot->m_vecGoal.y, p->m_bInBombZone);
  }
  double el = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
  printf("\nsimulated %.0fs in %.2fs (%.1fx realtime)\n", seconds, el, seconds / el);
  printf("score CT %d : %d T, rounds %d\n", CSGameRules()->m_iNumCTWins, CSGameRules()->m_iNumTerroristWins, CSGameRules()->m_iTotalRoundsPlayed);
  for (CCSPlayer* p : UTIL_GetPlayers()) printf("  %-16s %-2s k %2d d %2d hs %2d $%5d hp %3d state %s wpn %s\n", p->GetPlayerName(), p->m_iTeamNum == TEAM_CT ? "CT" : "T", p->m_iKills, p->m_iDeaths, p->m_iHeadshots, p->m_iAccount, p->m_iHealth,
    p->m_pBot ? BotStateName(p->m_pBot->m_iState) : "-", p->GetActiveCSWeapon() ? p->GetActiveCSWeapon()->GetCSWpnData().printName : "none");
  for (auto& [k, v] : counts) printf("  event %-22s %d\n", k.c_str(), v);
  return 0;
}
