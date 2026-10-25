// cs_bot.cpp - CCSBot
#include "cs_bot.hpp"
#include "nav_mesh.hpp"
#include "../shared/weapon_csbase.hpp"
#include "../shared/cs_gamerules.hpp"
#include "../shared/cs_projectiles.hpp"
#include "../shared/gameevents.hpp"
#include "../shared/mapinfo.hpp"
#include "../shared/lagcompensation.hpp"
#include "../../engine/random.hpp"
#include <cmath>
#include <algorithm>
#include <map>

ConVar bot_quota("bot_quota", "9", FCVAR_NOTIFY, "Determines the total number of bots in the game", true, 0, true, 31);
ConVar bot_difficulty("bot_difficulty", "2", FCVAR_NOTIFY, "Defines the skill of bots joining the game. Values are: 0=easy, 1=normal, 2=hard, 3=expert.", true, 0, true, 3);
ConVar bot_hvh("bot_hvh", "0", FCVAR_NOTIFY, "Bots use anti-aim, desync, fakelag and a ragebot (HvH bots)");
ConVar bot_stop("bot_stop", "0", FCVAR_CHEAT, "If nonzero, immediately stops all bot processing.");
ConVar bot_freeze("bot_freeze", "0", FCVAR_CHEAT, "Bots don't move");
ConVar bot_dont_shoot("bot_dont_shoot", "0", FCVAR_CHEAT, "If nonzero, bots will not fire weapons (for debugging).");
ConVar bot_zombie("bot_zombie", "0", FCVAR_CHEAT, "If nonzero, bots will stay in idle mode and not attack.");
ConVar bot_crouch("bot_crouch", "0", FCVAR_CHEAT, "Bots crouch");
ConVar bot_mimic("bot_mimic", "0", FCVAR_CHEAT, "Bot uses usercmd of player by index.");
ConVar bot_join_team("bot_join_team", "any", FCVAR_NONE, "Determines the team bots will join into. Allowed values: 'any', 'T', or 'CT'.");
ConVar bot_knives_only("bot_knives_only", "0", FCVAR_NOTIFY, "Bots only use knives");
ConVar bot_pistols_only("bot_pistols_only", "0", FCVAR_NOTIFY, "Bots only use pistols");
ConVar bot_snipers_only("bot_snipers_only", "0", FCVAR_NOTIFY, "Bots only use sniper rifles");
ConVar bot_show_nav("bot_show_nav", "0", FCVAR_CHEAT, "Draw bot paths");

static std::map<CCSPlayer*, CCSBot*> s_bots;
CCSBot* GetBot(CCSPlayer* p) { auto it = s_bots.find(p); return it == s_bots.end() ? nullptr : it->second; }

const char* BotStateName(int s) {
  static const char* n[] = { "Idle", "Buy", "MoveTo", "Attack", "Plant", "Defuse", "Hold", "Hunt", "FetchBomb", "Investigate", "Retreat" };
  return (s >= 0 && s <= BOT_RETREAT) ? n[s] : "?";
}

static const char* s_botNames[] = { "Albert", "Allen", "Bert", "Bob", "Cecil", "Clarence", "Elliot", "Elmer", "Ernie", "Eugene", "Fergus", "Ferris", "Frank", "Frasier", "Fred", "George", "Graham", "Harvey", "Irwin", "Lester", "Marvin", "Neil", "Niles", "Oliver", "Opie", "Ringo", "Rock", "Ulric", "Vladimir", "Wade", "Yanni", "Zach" };
static const char* s_hvhNames[] = { "gamesense.pub", "neverlose.cc", "onetap.su", "fatality.win", "aimware.net", "nixware", "primordial", "skeet.cc", "pandora", "ev0lve", "rifk7", "ot v4 crack", "spirthack", "legendware", "airflow", "weave.su" };

void Bot_Init() {
  static bool listening = false;
  if (listening) return; listening = true;
  gameeventmanager->AddListener(nullptr, [](const IGameEvent& ev) {
    const char* n = ev.GetName();
    bool interesting = !strcmp(n, "weapon_fire") || !strcmp(n, "player_death") || !strcmp(n, "player_footstep") || !strcmp(n, "player_hurt") || !strcmp(n, "round_start") || !strcmp(n, "bomb_planted");
    if (!interesting) return;
    int uid = ev.GetInt("userid"), att = ev.GetInt("attacker");
    CCSPlayer* src = UTIL_PlayerByIndex(uid);
    Vector pos = src ? src->m_vecOrigin : Vector();
    if (!strcmp(n, "player_death") && src) g_NavMesh.AddDanger(src->m_vecOrigin, 40.f);
    for (auto& [p, b] : s_bots) b->OnEvent(n, uid, att, pos);
  });
}

void Bot_AddBot(int team, const char* name) {
  if (team != TEAM_CT && team != TEAM_TERRORIST) {
    int t = 0, ct = 0;
    for (CCSPlayer* p : UTIL_GetPlayers()) { if (p->m_iTeamNum == TEAM_TERRORIST) t++; else if (p->m_iTeamNum == TEAM_CT) ct++; }
    std::string jt = bot_join_team.GetString();
    if (jt == "T" || jt == "t") team = TEAM_TERRORIST; else if (jt == "CT" || jt == "ct") team = TEAM_CT;
    else team = t <= ct ? TEAM_TERRORIST : TEAM_CT;
  }
  auto* p = new CCSPlayer();
  if (g_EntityList.Add(p) < 0) { delete p; Warning("Server is full\n"); return; }
  p->m_bIsBot = true; p->m_fFlags |= FL_FAKECLIENT;
  // unique name
  std::string nm;
  if (name && *name) nm = name;
  else {
    bool hvh = bot_hvh.GetBool();
    const char** list = hvh ? s_hvhNames : s_botNames; int count = hvh ? (int)(sizeof(s_hvhNames) / sizeof(*s_hvhNames)) : (int)(sizeof(s_botNames) / sizeof(*s_botNames));
    for (int tries = 0; tries < 100; tries++) {
      nm = list[RandomInt(0, count - 1)];
      bool used = false; for (CCSPlayer* o : UTIL_GetPlayers()) if (o != p && o->m_szPlayerName == nm) used = true;
      if (!used) break;
      if (tries > 50) nm += "(1)";
    }
  }
  p->m_szPlayerName = nm;
  p->m_iAccount = FindConVar("mp_startmoney")->GetInt();
  CCSBot* b = new CCSBot(p);
  p->m_pBot = b;
  s_bots[p] = b;
  p->m_iPing = 0;
  CSGameRules()->PlayerJoinTeam(p, team);
  IGameEvent ev("player_connect"); ev.SetInt("userid", p->entindex()).SetString("name", nm).SetBool("bot", true); gameeventmanager->FireEvent(ev);
}

void Bot_KickAll() {
  for (auto& [p, b] : s_bots) { p->RemoveAllItems(true); p->Remove(); delete b; }
  s_bots.clear();
  g_EntityList.CleanupDeleted();
}

void Bot_RunAll() {}

// ------------------------------------------------------------------------------------------------
CCSBot::CCSBot(CCSPlayer* p) : me(p) {
  m_iDifficulty = bot_difficulty.GetInt();
  m_bHvH = bot_hvh.GetBool();
}
void CCSBot::SetState(int s) { if (m_iState != s) { m_iState = s; m_flStateStart = gpGlobals->curtime; } }

void CCSBot::OnSpawn() {
  m_Path.clear(); m_iPathIndex = 0; m_pEnemy = nullptr; m_flEnemyLastSeen = -100;
  m_bBoughtThisRound = false; m_flBuyTime = ServerTime() + RandomFloat(0.3f, 2.0f);
  m_angView = me->v_angle; m_iState = BOT_IDLE; m_flRepathTime = 0; m_flStuckTime = 0;
  m_iTargetSite = RandomInt(0, 1);
  m_vecNoise = Vector(); m_flNoiseTime = -100; m_flHoldUntil = 0;
  m_vecLastPos = me->m_vecOrigin; m_flLastProgress = ServerTime();
  m_iDifficulty = bot_difficulty.GetInt();
  m_bHvH = bot_hvh.GetBool();
}

void CCSBot::OnEvent(const char* name, int userid, int attacker, const Vector& pos) {
  if (!strcmp(name, "round_start")) { OnSpawn(); return; }
  if (!me->IsAlive()) return;
  CCSPlayer* src = UTIL_PlayerByIndex(userid);
  if (!strcmp(name, "weapon_fire") || !strcmp(name, "player_footstep")) {
    if (!src || src == me || src->m_iTeamNum == me->m_iTeamNum) return;
    float range = !strcmp(name, "weapon_fire") ? 2000.f : 900.f;
    if (pos.DistTo(me->m_vecOrigin) < range) { m_vecNoise = pos; m_flNoiseTime = ServerTime(); }
  }
  if (!strcmp(name, "player_hurt") && src == me && m_bHvH) {
    // got hit: switch desync side like a real hvh cheat
    m_iDesyncSide = -m_iDesyncSide;
    CCSPlayer* a = UTIL_PlayerByIndex(attacker);
    if (a && a != me && a->m_iTeamNum != me->m_iTeamNum && !m_pEnemy) { m_vecNoise = a->m_vecOrigin; m_flNoiseTime = ServerTime(); }
  }
  if (!strcmp(name, "player_hurt") && src == me && !m_bHvH) {
    CCSPlayer* a = UTIL_PlayerByIndex(attacker);
    if (a && a != me && a->m_iTeamNum != me->m_iTeamNum) { m_vecNoise = a->m_vecOrigin; m_flNoiseTime = ServerTime(); }
  }
}

bool CCSBot::IsVisible(CCSPlayer* o, Vector* aimPoint) const {
  if (!o || !o->IsAlive() || !o->m_net.valid) return false;
  Vector eye = me->EyePosition();
  const HitboxSet& hb = o->m_net.hitboxes;
  int order[] = { HITBOX_HEAD, HITBOX_UPPER_CHEST, HITBOX_STOMACH, HITBOX_PELVIS, HITBOX_LEFT_FOREARM, HITBOX_RIGHT_FOREARM, HITBOX_LEFT_CALF, HITBOX_RIGHT_CALF };
  for (int h : order) {
    Vector p = (hb[h].a + hb[h].b) * 0.5f;
    trace_t tr; Ray_t ray; ray.Init(eye, p); CTraceFilterWorldOnly f;
    g_EngineTrace.TraceRay(ray, MASK_VISIBLE, &f, &tr);
    if (tr.fraction >= 0.99f && !LineGoesThroughSmoke(eye, p)) { if (aimPoint) *aimPoint = p; return true; }
  }
  return false;
}

void CCSBot::UpdatePerception() {
  float now = ServerTime();
  Vector eye = me->EyePosition();
  Vector fwd; AngleVectors(me->v_angle, &fwd);
  CCSPlayer* best = nullptr; float bestScore = 1e9f;
  bool blind = me->IsBlind();
  for (CCSPlayer* o : UTIL_GetPlayers(true)) {
    if (!me->IsEnemy(o)) continue;
    Vector to = o->m_net.origin + Vector(0, 0, 40) - eye;
    float dist = to.NormalizeInPlace();
    float dot = fwd.Dot(to);
    bool inFov = dot > (m_bHvH ? -1.f : 0.25f) || dist < 200.f || o == m_pEnemy;
    if (!inFov || blind) continue;
    if (!IsVisible(o, nullptr)) continue;
    float score = dist * (1.5f - dot) * (o == m_pEnemy ? 0.5f : 1.f);
    if (score < bestScore) { bestScore = score; best = o; }
  }
  if (best) {
    if (best != m_pEnemy || now - m_flEnemyLastSeen > 1.f) {
      static const float reaction[4] = { 0.55f, 0.35f, 0.22f, 0.14f };
      m_flEnemyFirstSeen = now;
      m_flReactionEnd = now + (m_bHvH ? 0.03f : reaction[Clamp(m_iDifficulty, 0, 3)] * RandomFloat(0.8f, 1.3f));
      m_angAimError = QAngle(RandomFloat(-4, 4), RandomFloat(-6, 6), 0) * (float)(4 - m_iDifficulty) * 0.5f;
    }
    m_pEnemy = best; m_flEnemyLastSeen = now; m_vecEnemyLastPos = best->m_net.origin;
  } else if (m_pEnemy && (!m_pEnemy->IsAlive() || now - m_flEnemyLastSeen > 3.f)) m_pEnemy = nullptr;
}

void CCSBot::EquipBestWeapon() {
  C_WeaponCSBase* act = me->GetActiveCSWeapon();
  if (act && (act->GetCSWeaponID() == WEAPON_C4 && m_iState == BOT_PLANT)) return;
  if (bot_knives_only.GetBool()) { me->SelectSlot(WEAPON_SLOT_KNIFE); return; }
  C_WeaponCSBase* want = nullptr;
  C_WeaponCSBase* rifle = me->Weapon_GetSlot(WEAPON_SLOT_RIFLE);
  C_WeaponCSBase* pistol = me->Weapon_GetSlot(WEAPON_SLOT_PISTOL);
  if (rifle && (rifle->m_iClip1 > 0 || rifle->m_iPrimaryReserveAmmoCount > 0)) want = rifle;
  else if (pistol && (pistol->m_iClip1 > 0 || pistol->m_iPrimaryReserveAmmoCount > 0)) want = pistol;
  else want = me->Weapon_GetSlot(WEAPON_SLOT_KNIFE);
  // in a fight with an empty rifle switch to the pistol (faster than reloading)
  if (m_pEnemy && act == rifle && rifle && rifle->m_iClip1 == 0 && pistol && pistol->m_iClip1 > 0) want = pistol;
  if (want && want != act) me->Weapon_Switch(want);
}

void CCSBot::Buy() {
  if (m_bBoughtThisRound || !me->m_bInBuyZone || ServerTime() < m_flBuyTime) return;
  m_bBoughtThisRound = true;
  int money = me->m_iAccount;
  bool ct = me->m_iTeamNum == TEAM_CT;
  if (bot_knives_only.GetBool()) return;
  if (m_bHvH) {
    // hvh meta: auto sniper + heavy pistol + full armor
    me->BuyItem("vesthelm", true);
    if (!bot_pistols_only.GetBool()) me->BuyItem(ct ? "scar20" : "g3sg1", true);
    me->BuyItem(RandomInt(0, 2) == 0 ? "revolver" : "deagle", true);
    me->BuyItem("hegrenade", true);
    EquipBestWeapon();
    return;
  }
  bool haveRifle = me->Weapon_GetSlot(WEAPON_SLOT_RIFLE) != nullptr;
  if (bot_snipers_only.GetBool()) { if (money >= 4750) me->BuyItem("awp", true); else me->BuyItem("ssg08", true); me->BuyItem("vest", true); EquipBestWeapon(); return; }
  if (!haveRifle && !bot_pistols_only.GetBool()) {
    bool awper = (me->entindex() % 5) == 0;
    if (awper && money >= 5750) { me->BuyItem("awp", true); }
    else if (money >= 4100) me->BuyItem(ct ? (RandomInt(0, 1) ? "m4a1" : "m4a1_silencer") : "ak47", true);
    else if (money >= 3100) me->BuyItem(ct ? "famas" : "galilar", true);
    else if (money >= 2350 && RandomInt(0, 2) == 0) me->BuyItem(ct ? "mp9" : "mac10", true);
  }
  money = me->m_iAccount;
  if (money >= 1000) me->BuyItem("vesthelm", true); else if (money >= 650) me->BuyItem("vest", true);
  if (!me->Weapon_GetSlot(WEAPON_SLOT_RIFLE) && me->m_iAccount >= 700) me->BuyItem(RandomInt(0, 2) ? "deagle" : "p250", true);
  if (ct && me->m_iAccount >= 400 && RandomInt(0, 1)) me->BuyItem("defuser", true);
  if (me->m_iAccount >= 300 && RandomInt(0, 1)) me->BuyItem("hegrenade", true);
  if (me->m_iAccount >= 200 && RandomInt(0, 1)) me->BuyItem("flashbang", true);
  if (me->m_iAccount >= 300 && RandomInt(0, 2) == 0) me->BuyItem("smokegrenade", true);
  EquipBestWeapon();
}

bool CCSBot::SetGoal(const Vector& goal, bool force) {
  float now = ServerTime();
  if (!force && (goal - m_vecGoal).Length2D() < 48.f && !m_Path.empty() && now < m_flRepathTime) return true;
  m_vecGoal = goal;
  m_flRepathTime = now + RandomFloat(2.5f, 4.f);
  bool ok = g_NavMesh.ComputePath(me->m_vecOrigin, goal, m_Path, &m_PathFlags, m_iDifficulty >= 2 ? 2.f : 0.f);
  m_iPathIndex = m_Path.size() > 1 ? 1 : 0;
  return ok;
}

void CCSBot::ThinkGoals() {
  CCSGameRules* gr = CSGameRules();
  float now = ServerTime();
  bool ct = me->m_iTeamNum == TEAM_CT;
  const MapZone* siteA = g_MapInfo.FindZone(ZONE_BOMBSITE_A);
  const MapZone* siteB = g_MapInfo.FindZone(ZONE_BOMBSITE_B);
  const MapZone* site = m_iTargetSite == 0 ? siteA : siteB;
  if (!site) site = siteA ? siteA : siteB;
  if (gr->IsDeathmatch() || !site) {
    // hunt the nearest enemy
    CCSPlayer* nearest = nullptr; float nd = 1e9f;
    for (CCSPlayer* o : UTIL_GetPlayers(true)) if (me->IsEnemy(o)) { float d = o->m_vecOrigin.DistTo(me->m_vecOrigin); if (d < nd) { nd = d; nearest = o; } }
    if (nearest) { SetState(BOT_HUNT); SetGoal(nearest->m_net.origin); }
    return;
  }
  // the bomb
  if (g_pPlantedC4 && !g_pPlantedC4->m_bBombDefused && !g_pPlantedC4->m_bExploded) {
    Vector bomb = g_pPlantedC4->m_vecOrigin;
    if (ct) {
      float d = me->m_vecOrigin.DistTo(bomb);
      if (d < 60.f && (!m_pEnemy || now - m_flEnemyLastSeen > 1.5f)) { SetState(BOT_DEFUSE); return; }
      SetState(BOT_MOVE_TO); SetGoal(bomb);
    } else {
      if (m_flHoldUntil < now || (m_vecHoldSpot - bomb).Length2D() > 600.f) {
        m_vecHoldSpot = g_NavMesh.GetRandomWalkablePoint(bomb, 400.f); m_flHoldUntil = now + 15.f;
        m_vecHoldLook = bomb + (Vector(0, 0, 0) - bomb).Normalized() * 400.f;
      }
      if (me->m_vecOrigin.DistTo(m_vecHoldSpot) > 64.f) { SetState(BOT_MOVE_TO); SetGoal(m_vecHoldSpot); } else SetState(BOT_HOLD);
    }
    return;
  }
  if (!ct) {
    if (me->HasC4()) {
      if (me->m_bInBombZone && site->Contains(me->m_vecOrigin)) { SetState(BOT_PLANT); return; }
      SetState(BOT_MOVE_TO); SetGoal(site->Center()); return;
    }
    // fetch a dropped bomb
    if (gr->m_bBombDropped) {
      C_WeaponCSBase* c4 = nullptr;
      g_EntityList.ForEach([&](C_BaseEntity* e) { if (e->IsWeapon() && static_cast<C_WeaponCSBase*>(e)->GetCSWeaponID() == WEAPON_C4 && static_cast<C_WeaponCSBase*>(e)->m_iState == WEAPON_NOT_CARRIED) c4 = static_cast<C_WeaponCSBase*>(e); });
      if (c4) {
        CCSPlayer* closest = nullptr; float cd = 1e9f;
        for (CCSPlayer* o : UTIL_GetPlayers(true)) if (o->m_iTeamNum == TEAM_TERRORIST && o->IsBot()) { float d = o->m_vecOrigin.DistTo(c4->m_vecOrigin); if (d < cd) { cd = d; closest = o; } }
        if (closest == me) { SetState(BOT_FETCH_BOMB); SetGoal(c4->m_vecOrigin); return; }
      }
    }
    // escort / push the site
    if (m_vecNoise.LengthSqr() > 0 && now - m_flNoiseTime < 4.f && m_iDifficulty >= 1) { SetState(BOT_INVESTIGATE); SetGoal(m_vecNoise); return; }
    Vector dest = site->Center();
    if (me->m_vecOrigin.DistTo(dest) < 300.f) {
      if (m_flHoldUntil < now) { m_vecHoldSpot = g_NavMesh.GetRandomWalkablePoint(dest, 350.f); m_flHoldUntil = now + RandomFloat(4, 9); }
      SetState(BOT_HOLD); SetGoal(m_vecHoldSpot); return;
    }
    SetState(BOT_MOVE_TO); SetGoal(dest);
  } else {
    if (m_vecNoise.LengthSqr() > 0 && now - m_flNoiseTime < 5.f && me->m_vecOrigin.DistTo(m_vecNoise) < 1600.f) { SetState(BOT_INVESTIGATE); SetGoal(m_vecNoise); return; }
    // defend the chosen site: pick a spot looking toward the T side
    if (m_flHoldUntil < now || m_vecHoldSpot.IsZero()) {
      m_vecHoldSpot = g_NavMesh.GetRandomWalkablePoint(site->Center(), 300.f); m_flHoldUntil = now + RandomFloat(8, 20);
      Vector tspawn; int n = 0; for (auto& s : g_MapInfo.spawns) if (s.team == TEAM_TERRORIST) { tspawn += s.origin; n++; }
      if (n) tspawn /= (float)n;
      m_vecHoldLook = m_vecHoldSpot + (tspawn - m_vecHoldSpot).Normalized() * 500.f;
    }
    if (me->m_vecOrigin.DistTo(m_vecHoldSpot) > 64.f) { SetState(BOT_MOVE_TO); SetGoal(m_vecHoldSpot); } else SetState(BOT_HOLD);
  }
}

void CCSBot::MoveTowardsGoal(CUserCmd& cmd, const QAngle& view) {
  float now = ServerTime();
  if (m_iState == BOT_HOLD || m_iState == BOT_PLANT || m_iState == BOT_DEFUSE) { m_flLastProgress = now; m_vecLastPos = me->m_vecOrigin; return; }
  if (m_Path.empty() || m_iPathIndex >= m_Path.size()) {
    // no path but not at the goal: wander a bit and retry
    if (m_vecGoal.DistTo(me->m_vecOrigin) > 96.f && now - m_flLastProgress > 1.f) {
      SetGoal(g_NavMesh.GetRandomWalkablePoint(me->m_vecOrigin, 400.f), true);
      Vector g = m_vecGoal; m_flLastProgress = now; (void)g;
      m_flRepathTime = now + 0.6f;   // try the real goal again soon
    }
    return;
  }
  Vector target = m_Path[m_iPathIndex];
  Vector to = target - me->m_vecOrigin;
  float dist2 = to.Length2D();
  // advance along the path (look ahead when the straight line is free)
  while (dist2 < 28.f && m_iPathIndex + 1 < m_Path.size()) { m_iPathIndex++; target = m_Path[m_iPathIndex]; to = target - me->m_vecOrigin; dist2 = to.Length2D(); }
  if (m_iPathIndex + 1 < m_Path.size()) {
    size_t ahead = std::min(m_Path.size() - 1, m_iPathIndex + 2);
    for (size_t k = ahead; k > m_iPathIndex; k--) {
      if (m_PathFlags.size() > k && m_PathFlags[k] != NAV_WALK) continue;
      if (std::fabs(m_Path[k].z - me->m_vecOrigin.z) > 20) continue;
      trace_t tr; g_EngineTrace.TraceHull(me->m_vecOrigin + Vector(0, 0, 18), m_Path[k] + Vector(0, 0, 18), Vector(-16, -16, 0), Vector(16, 16, 36), MASK_PLAYERSOLID_BRUSHONLY, me, &tr);
      if (tr.fraction >= 1.f) { m_iPathIndex = k; target = m_Path[k]; to = target - me->m_vecOrigin; dist2 = to.Length2D(); break; }
    }
  }
  if (m_iPathIndex + 1 >= m_Path.size() && dist2 < 24.f) { m_Path.clear(); return; }
  to.z = 0; to.NormalizeInPlace();
  float yaw = DEG2RADF(view.y);
  Vector f(std::cos(yaw), std::sin(yaw), 0), r(std::sin(yaw), -std::cos(yaw), 0);
  float speed = 450.f;
  cmd.forwardmove = to.Dot(f) * speed; cmd.sidemove = to.Dot(r) * speed;
  // jumps
  int flag = m_PathFlags.size() > m_iPathIndex ? m_PathFlags[m_iPathIndex] : NAV_WALK;
  bool onGround = (me->m_fFlags & FL_ONGROUND) != 0;
  if ((flag == NAV_JUMP || flag == NAV_CROUCHJUMP) && dist2 < 72.f && onGround && target.z > me->m_vecOrigin.z + 16) cmd.buttons |= IN_JUMP;
  if (!onGround && (flag == NAV_CROUCHJUMP || me->m_vecVelocity.z > 0)) cmd.buttons |= IN_DUCK;
  // stuck detection
  if (me->m_vecOrigin.DistTo(m_vecLastPos) > 24.f) { m_vecLastPos = me->m_vecOrigin; m_flLastProgress = now; }
  else if (now - m_flLastProgress > 0.8f) {
    if (onGround) cmd.buttons |= IN_JUMP | IN_DUCK;
    if (now - m_flLastProgress > 1.6f) { cmd.sidemove = (me->entindex() & 1 ? 1 : -1) * 450.f; }
    if (now - m_flLastProgress > 2.5f) { m_flRepathTime = 0; SetGoal(g_NavMesh.GetRandomWalkablePoint(me->m_vecOrigin, 300.f), true); m_flLastProgress = now; }
  }
}

static float AngleDistance(const QAngle& a, const QAngle& b) { return std::sqrt(AngleDiff(a.x, b.x) * AngleDiff(a.x, b.x) + AngleDiff(a.y, b.y) * AngleDiff(a.y, b.y)); }

void CCSBot::Fight(CUserCmd& cmd, QAngle& view) {
  float now = ServerTime();
  C_WeaponCSBase* w = me->GetActiveCSWeapon();
  Vector aimPoint;
  bool visible = IsVisible(m_pEnemy, &aimPoint);
  if (!visible) return;
  // aim point: head for harder bots, chest otherwise
  const HitboxSet& hb = m_pEnemy->m_net.hitboxes;
  bool headAim = m_iDifficulty >= 2 ? RandomInt(0, 9) < 6 + m_iDifficulty : RandomInt(0, 9) < 3;
  Vector head = (hb[HITBOX_HEAD].a + hb[HITBOX_HEAD].b) * 0.5f, chest = (hb[HITBOX_CHEST].a + hb[HITBOX_CHEST].b) * 0.5f;
  Vector eye = me->EyePosition();
  trace_t tr; UTIL_TraceLine(eye, head, MASK_VISIBLE, me, &tr);
  bool headVis = tr.fraction > 0.99f;
  UTIL_TraceLine(eye, chest, MASK_VISIBLE, me, &tr);
  bool chestVis = tr.fraction > 0.99f;
  Vector target = (headAim && headVis) || !chestVis ? (headVis ? head : aimPoint) : chest;
  QAngle want = CalcAngle(eye, target);
  // recoil compensation (better bots compensate more)
  float comp = 0.4f + 0.2f * m_iDifficulty;
  want.x -= me->m_aimPunchAngle.x * 2.f * comp; want.y -= me->m_aimPunchAngle.y * 2.f * comp;
  // aim error that shrinks while tracking
  float track = Clamp((now - m_flEnemyFirstSeen) / 0.6f, 0.f, 1.f);
  QAngle err = m_angAimError * (1.f - track * 0.85f);
  want.x += err.x; want.y += err.y;
  // turn speed
  static const float turn[4] = { 220.f, 380.f, 600.f, 900.f };
  float maxStep = turn[Clamp(m_iDifficulty, 0, 3)] * gpGlobals->interval_per_tick;
  float dp = AngleDiff(want.x, view.x), dy = AngleDiff(want.y, view.y);
  float len = std::sqrt(dp * dp + dy * dy);
  float k = len > maxStep ? maxStep / len : 1.f;
  // ease in
  k = std::min(1.f, k * (len > 10 ? 1.f : 0.6f + 0.04f * len));
  view.x += dp * k; view.y += dy * k;
  if (now < m_flReactionEnd || bot_dont_shoot.GetBool() || !w) return;
  float dist = eye.DistTo(target);
  float tolerance = std::max(0.8f, RAD2DEGF(std::atan2(10.f, dist))) * (1.f + 0.3f * (3 - m_iDifficulty));
  QAngle realAim = view; realAim.x += me->m_aimPunchAngle.x * 2; realAim.y += me->m_aimPunchAngle.y * 2;
  bool onTarget = AngleDistance(realAim, CalcAngle(eye, target)) < tolerance;
  int type = w->GetWeaponType();
  // snipers scope in
  if (w->GetCSWpnData().zoomLevels > 0 && !w->IsZoomed() && type == WEAPONTYPE_SNIPER_RIFLE && now > w->m_flNextSecondaryAttack) { cmd.buttons |= IN_ATTACK2; return; }
  // stop to shoot accurately (rifles/snipers at range)
  bool needStop = (type == WEAPONTYPE_SNIPER_RIFLE || (type == WEAPONTYPE_RIFLE && dist > 500.f)) && m_iDifficulty >= 1;
  if (needStop) { cmd.forwardmove = 0; cmd.sidemove = 0; if (me->m_vecVelocity.Length2D() > w->GetMaxSpeed() * 0.34f) return; }
  else if (m_iDifficulty >= 2) {
    // strafe while shooting pistols/smgs at close range
    if (now > m_flStrafeChange) { m_iStrafeDir = -m_iStrafeDir; m_flStrafeChange = now + RandomFloat(0.25f, 0.7f); }
    cmd.sidemove = 450.f * m_iStrafeDir; cmd.forwardmove = 0;
  }
  if (dist > 900.f && type == WEAPONTYPE_RIFLE && m_iDifficulty >= 2) cmd.buttons |= IN_DUCK;
  if (!onTarget) return;
  if (w->m_iClip1 == 0) return;
  // bursts at range, spray up close
  bool automatic = w->IsFullAuto();
  if (automatic) {
    int maxBurst = dist < 400.f ? 30 : dist < 900.f ? 6 : 3;
    if (me->m_iShotsFired >= maxBurst) { if (now - m_flLastShot > 0.35f) { /* release */ } else return; }
    else { cmd.buttons |= IN_ATTACK; m_flLastShot = now; }
  } else {
    // semi-auto: tap (release between shots)
    if (!(m_LastCmd.buttons & IN_ATTACK) && now >= w->m_flNextPrimaryAttack) { cmd.buttons |= IN_ATTACK; m_flLastShot = now; }
  }
}

// ------------------------------------------------------------------------------------------------
// HvH bot: ragebot + anti-aim
void CCSBot::HvHAim(CUserCmd& cmd, QAngle& view) {
  C_WeaponCSBase* w = me->GetActiveCSWeapon();
  if (!w || !w->IsGun() || !m_pEnemy || bot_dont_shoot.GetBool()) return;
  float now = ServerTime();
  if (now < m_flReactionEnd) return;
  CCSPlayer* t = m_pEnemy;
  if (t->m_LagRecords.empty()) return;
  const CLagRecord& rec = t->m_LagRecords.front();
  // resolver: brute force the desync side by missed shots (we don't know the server's real feet yaw)
  static const float sides[4] = { 1.f, -1.f, 0.5f, 0.f };
  float guess = AngleNormalize(rec.eyeAngles.y + sides[t->m_iMissedShots % 4] * rec.maxDesync);
  CStudioModel* mdl = t->GetModel();
  if (!mdl) return;
  PlayerPoseInput in; in.origin = rec.origin; in.footYaw = guess; in.eyeAngles = rec.eyeAngles; in.duckAmount = rec.duckAmount;
  in.moveCycle = rec.moveCycle; in.moveSpeed = rec.moveSpeed; in.moveYaw = rec.moveYaw; in.onGround = rec.onGround; in.weaponId = rec.weaponId;
  PlayerPoseOutput out; BuildPlayerPose(*mdl, t->GetWeaponWorldModel(rec.weaponId), in, out, true);
  Vector eye = me->EyePosition();
  int hitboxes[] = { HITBOX_HEAD, HITBOX_CHEST, HITBOX_STOMACH, HITBOX_PELVIS };
  Vector best; bool found = false;
  for (int h : hitboxes) {
    Vector p = (out.hitboxes[h].a + out.hitboxes[h].b) * 0.5f;
    trace_t tr; UTIL_TraceLine(eye, p, MASK_VISIBLE, me, &tr);
    if (tr.fraction > 0.99f) { best = p; found = true; break; }
  }
  if (!found) return;
  // autoscope / autostop
  if (w->GetCSWpnData().zoomLevels > 0 && !w->IsZoomed() && now > w->m_flNextSecondaryAttack && w->GetWeaponType() == WEAPONTYPE_SNIPER_RIFLE) { cmd.buttons |= IN_ATTACK2; return; }
  if (now < w->m_flNextPrimaryAttack || w->m_iClip1 <= 0 || now < me->m_flNextAttack) { cmd.forwardmove = cmd.sidemove = 0; return; }
  cmd.forwardmove = 0; cmd.sidemove = 0;
  // hitchance: inaccuracy small enough for the distance
  float dist = eye.DistTo(best);
  float inacc = w->GetInaccuracy() + w->GetSpread();
  if (inacc * dist > 9.f && me->m_vecVelocity.Length2D() > 5.f) return;
  QAngle aim = CalcAngle(eye, best);
  aim.x -= me->m_aimPunchAngle.x * 2.f; aim.y -= me->m_aimPunchAngle.y * 2.f;
  view = aim;                 // silent: only this command carries the real aim
  cmd.buttons |= IN_ATTACK;
  cmd.tick_count = rec.tick; // shoot at the record we resolved
  t->m_iMissedShots++;       // reset on hit by the event listener (approximation)
}

void CCSBot::ApplyHvHAntiAim(CUserCmd& cmd, const QAngle& view, bool shooting) {
  float now = ServerTime();
  if (shooting) { cmd.bSendPacket = true; m_nChoke = 0; return; }
  // fakelag (fluctuate 6..14) - sends every N ticks
  int maxChoke = 13;
  int limit = me->m_vecVelocity.Length2D() > 10.f ? maxChoke : 2;
  m_nChoke++;
  cmd.bSendPacket = m_nChoke >= limit;
  if (cmd.bSendPacket) m_nChoke = 0;
  // yaw: at target + 180 with jitter, pitch down
  float base = view.y;
  if (m_pEnemy) base = CalcAngle(me->m_vecOrigin, m_pEnemy->m_vecOrigin).y;
  else { float best = 1e9f; for (CCSPlayer* o : UTIL_GetPlayers(true)) if (me->IsEnemy(o)) { float d = o->m_vecOrigin.DistTo(me->m_vecOrigin); if (d < best) { best = d; base = CalcAngle(me->m_vecOrigin, o->m_vecOrigin).y; } } }
  m_bJitter = !m_bJitter;
  float yaw = base + 180.f + (m_bJitter ? 12.f : -12.f);
  if (now > m_flNextSideSwitch) { m_flNextSideSwitch = now + RandomFloat(1.5f, 4.f); if (RandomInt(0, 2) == 0) m_iDesyncSide = -m_iDesyncSide; }
  // desync: choked commands carry the opposite yaw so the server's feet yaw ends up offset
  if (!cmd.bSendPacket) yaw += 120.f * m_iDesyncSide;
  cmd.viewangles = QAngle(89.f, AngleNormalize(yaw), 0);
  // micro movement to keep the lby from flicking while standing
  if (std::fabs(cmd.forwardmove) < 1 && std::fabs(cmd.sidemove) < 1 && (me->m_fFlags & FL_ONGROUND)) cmd.forwardmove = (gpGlobals->tickcount & 1) ? 3.3f : -3.3f;
  // movement must be rotated because we changed the yaw
  float delta = DEG2RADF(AngleDiff(cmd.viewangles.y, view.y));
  float f = cmd.forwardmove, s = cmd.sidemove;
  cmd.forwardmove = std::cos(delta) * f - std::sin(delta) * s;
  cmd.sidemove = std::sin(delta) * f + std::cos(delta) * s;
}

// ------------------------------------------------------------------------------------------------
void CCSBot::Upkeep(CUserCmd& cmd) {
  cmd.Reset();
  cmd.command_number = gpGlobals->tickcount;
  cmd.tick_count = gpGlobals->tickcount;
  cmd.random_seed = MD5_PseudoRandom(cmd.command_number) & 0x7fffffff;
  cmd.bSendPacket = true;
  if (bot_stop.GetBool() || !me->IsAlive()) { cmd.viewangles = me->v_angle; m_LastCmd = cmd; return; }
  if (bot_mimic.GetInt() > 0) {
    CCSPlayer* src = UTIL_PlayerByIndex(bot_mimic.GetInt());
    if (src) { cmd = src->m_LastCmd; cmd.bSendPacket = true; cmd.tickbaseShift = 0; m_LastCmd = cmd; return; }
  }
  float now = ServerTime();
  m_bHvH = bot_hvh.GetBool();
  UpdatePerception();
  if (CSGameRules()->IsFreezePeriod() || !CSGameRules()->IsBuyTimeElapsed()) Buy();
  if (!m_pEnemy) EquipBestWeapon();
  QAngle view = m_angView;
  if (m_pEnemy && !bot_zombie.GetBool()) {
    SetState(BOT_ATTACK);
    EquipBestWeapon();
    if (m_bHvH) {
      QAngle aimDir = CalcAngle(me->EyePosition(), m_pEnemy->m_net.origin + Vector(0, 0, 50));
      view = aimDir;
    } else Fight(cmd, view);
    // chase if far
    if (!m_bHvH && me->m_vecOrigin.DistTo(m_pEnemy->m_net.origin) > 1200.f) SetGoal(m_pEnemy->m_net.origin);
  } else {
    ThinkGoals();
    // look where we go / hold angle / at noise
    if (m_iState == BOT_HOLD) {
      Vector look = m_vecHoldLook;
      if (m_vecNoise.LengthSqr() > 0 && now - m_flNoiseTime < 3.f) look = m_vecNoise + Vector(0, 0, 50);
      if (now > m_flLookAroundTime) { m_flLookAroundTime = now + RandomFloat(1.f, 3.f); m_flLookYaw = RandomFloat(-35, 35); }
      QAngle want = CalcAngle(me->EyePosition(), look); want.y += m_flLookYaw; want.x = Clamp(want.x, -10.f, 15.f);
      view.x = ApproachAngle(want.x, view.x, 120.f * gpGlobals->interval_per_tick);
      view.y = ApproachAngle(want.y, view.y, 200.f * gpGlobals->interval_per_tick);
    } else if (!m_Path.empty() && m_iPathIndex < m_Path.size()) {
      Vector look = m_Path[std::min(m_Path.size() - 1, m_iPathIndex + 1)] + Vector(0, 0, 60);
      if (m_vecNoise.LengthSqr() > 0 && now - m_flNoiseTime < 2.f) look = m_vecNoise + Vector(0, 0, 50);
      QAngle want = CalcAngle(me->EyePosition(), look); want.x = Clamp(want.x * 0.3f, -10.f, 10.f);
      view.x = ApproachAngle(want.x, view.x, 90.f * gpGlobals->interval_per_tick);
      view.y = ApproachAngle(want.y, view.y, 260.f * gpGlobals->interval_per_tick);
    }
    // reload when safe
    C_WeaponCSBase* w = me->GetActiveCSWeapon();
    if (w && w->IsGun() && !w->m_bInReload && w->m_iClip1 < w->GetCSWpnData().clipSize * 0.4f && w->m_iPrimaryReserveAmmoCount > 0 && now - m_flEnemyLastSeen > 2.f) cmd.buttons |= IN_RELOAD;
  }
  // objectives
  if (m_iState == BOT_PLANT) {
    if (C_WeaponCSBase* c4 = me->Weapon_OwnsThisType(WEAPON_C4)) { if (me->GetActiveCSWeapon() != c4) me->Weapon_Switch(c4); cmd.buttons |= IN_ATTACK; view.x = 60; }
  } else if (m_iState == BOT_DEFUSE && g_pPlantedC4) {
    view = CalcAngle(me->EyePosition(), g_pPlantedC4->m_vecOrigin);
    cmd.buttons |= IN_USE | IN_DUCK;
  }
  if (!bot_freeze.GetBool() && !(m_iState == BOT_ATTACK && m_bHvH)) MoveTowardsGoal(cmd, view);
  else if (m_bHvH && m_iState == BOT_ATTACK && !bot_freeze.GetBool()) {
    // hvh bots peek: keep strafing
    if (now > m_flStrafeChange) { m_iStrafeDir = -m_iStrafeDir; m_flStrafeChange = now + RandomFloat(0.4f, 1.2f); }
    cmd.sidemove = 450.f * m_iStrafeDir;
  }
  if (bot_crouch.GetBool()) cmd.buttons |= IN_DUCK;
  view.x = Clamp(view.x, -89.f, 89.f); view.y = AngleNormalize(view.y); view.z = 0;
  m_angView = view;
  cmd.viewangles = view;
  if (m_bHvH && m_pEnemy) {
    QAngle aim = view;
    HvHAim(cmd, aim);
    bool shooting = (cmd.buttons & IN_ATTACK) != 0;
    if (shooting) cmd.viewangles = aim;
    else ApplyHvHAntiAim(cmd, view, false);
  } else if (m_bHvH) {
    ApplyHvHAntiAim(cmd, view, false);
  }
  m_LastCmd = cmd;
}
