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
ConVar bot_debug("bot_debug", "0", FCVAR_NONE, "Print bot decisions (drops, grenades, saves, peeks)");
ConVar bot_debug_cmd("bot_debug_cmd", "", FCVAR_NONE, "Print every usercmd of the bot with this name");
ConVar bot_show_nav("bot_show_nav", "0", FCVAR_CHEAT, "Draw bot paths");

static std::map<CCSPlayer*, CCSBot*> s_bots;
static int s_roundSerial = 0;
CCSBot* GetBot(CCSPlayer* p) { auto it = s_bots.find(p); return it == s_bots.end() ? nullptr : it->second; }

const char* BotStateName(int s) {
  static const char* n[] = { "Idle", "Buy", "MoveTo", "Attack", "Plant", "Defuse", "Hold", "Hunt", "FetchBomb", "Investigate", "Retreat" };
  return (s >= 0 && s <= BOT_RETREAT) ? n[s] : "?";
}

static ConCommand cmd_bot_status("bot_status", [](const CCommand&) {
  for (auto& [p, b] : s_bots) {
    int area = g_NavMesh.GetAreaIndex(p->m_vecOrigin);
    static const char* roles[] = { "-", "entry", "lurk", "awp", "rotator" };
    C_WeaponCSBase* w = p->GetActiveCSWeapon();
    Msg("%-10s team %d hp %3d $%-5d %-14s pos %6.0f %6.0f %5.0f  state %-11s role %-7s site %c path %3d spd %3.0f%s%s%s%s nav %d\n", p->GetPlayerName(), p->m_iTeamNum, p->m_iHealth, p->m_iAccount,
        w ? w->GetCSWpnData().printName : "-", p->m_vecOrigin.x, p->m_vecOrigin.y, p->m_vecOrigin.z, BotStateName(b->m_iState), roles[Clamp(b->m_iRole, 0, 4)], 'A' + b->m_iTargetSite,
        (int)b->m_Path.size(), p->m_vecVelocity.Length2D(), b->m_bWalk ? " walk" : "", b->m_bBhopping ? " bhop" : "", b->m_bSaving ? " SAVE" : "", b->m_iThrowPhase ? " nade" : "", area);
    if (b->m_bHvH) {
      static const char* prof[] = { "fakelag", "doubletap", "hideshots", "mixed" }; static const char* aa[] = { "jitter", "static", "3-way", "sideways" };
      Msg("           hvh: %s (choke %d) aa %s jitter %.0f desync %.0f charge %d\n", prof[b->m_iHvHProfile & 3], b->m_iFakelag, aa[b->m_iAAMode & 3], b->m_flJitter, b->m_flDesync, p->m_nTickbaseShift);
    }
  }
}, "Print bot positions, states and paths");

static const char* s_botNames[] = { "Albert", "Allen", "Bert", "Bob", "Cecil", "Clarence", "Elliot", "Elmer", "Ernie", "Eugene", "Fergus", "Ferris", "Frank", "Frasier", "Fred", "George", "Graham", "Harvey", "Irwin", "Lester", "Marvin", "Neil", "Niles", "Oliver", "Opie", "Ringo", "Rock", "Ulric", "Vladimir", "Wade", "Yanni", "Zach" };
// hvh bots: ordinary people's names / nicknames (no real cheat names)
static const char* s_hvhNames[] = { "Artyom", "Dima", "Kirill", "Maxim", "Nikita", "Egor", "Sasha", "Vanya", "Misha", "Lyosha", "Denis", "Roma", "Pasha", "Ilya", "Timur", "Anton",
                                    "Jake", "Tom", "Lucas", "Mateo", "Oskar", "Jonas", "Felix", "Leon", "Marek", "Tomas", "Kevin", "Ryan", "Daniel", "Victor", "Hugo", "Emil" };

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
    if (!strcmp(n, "round_start")) s_roundSerial++;
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
// team knowledge: round plan (economy + strategy), callouts (enemy positions seen by teammates)
struct BotTeamPlan {
  int serial = -1;
  int buy = BOTBUY_FULL;
  int site = 0;                  // T: site to take
  bool rush = false;
  bool executing = false;
  float roundStart = 0, executeTime = 0;
  int awper = 0, lurker = 0;     // entindex
  int role[65] = {}, botSite[65] = {};
  bool dropFor[65] = {};
  int smokes = 0, flashes = 0;
  Vector enemyPos[65]; float enemySeen[65];
  int rotateSite = -1; float rotateTime = -100; int rotateSerial = 0;
  BotTeamPlan() { for (float& f : enemySeen) f = -100; }
};
static BotTeamPlan s_plan[4];
struct BotMapCache { Vector stage[2], entrance[2], tSpawn, ctSpawn; bool ok[2] = { false, false }; int serial = -1; };
static BotMapCache s_map;

const char* BotBuyName(int b) { static const char* n[] = { "pistol", "eco", "force", "half", "full" }; return (b >= 0 && b <= BOTBUY_FULL) ? n[b] : "?"; }
static int TeamIdx(int t) { return t == TEAM_CT ? 3 : 2; }
static int PriceOf(const char* n) { const CCSWeaponInfo* wi = GetWeaponInfoByName(n); return wi ? wi->price : 99999; }
static bool WorldLOS(const Vector& a, const Vector& b) {
  trace_t tr; Ray_t ray; ray.Init(a, b); CTraceFilterWorldOnly f;
  g_EngineTrace.TraceRay(ray, MASK_VISIBLE, &f, &tr);
  return tr.fraction >= 0.99f && !LineGoesThroughSmoke(a, b);
}
static Vector TeamSpawnCenter(int team) {
  Vector c; int n = 0;
  for (auto& s : g_MapInfo.spawns) if (s.team == team) { c += s.origin; n++; }
  return n ? c * (1.f / n) : Vector();
}
static Vector PointFromPathEnd(const std::vector<Vector>& path, float distFromEnd) {
  if (path.empty()) return Vector();
  float acc = 0;
  for (int i = (int)path.size() - 1; i > 0; i--) {
    float d = path[i].DistTo(path[i - 1]);
    if (d > 0.1f && acc + d >= distFromEnd) return path[i] + (path[i - 1] - path[i]) * ((distFromEnd - acc) / d);
    acc += d;
  }
  return path.front();
}
static const MapZone* SiteZone(int s) {
  const MapZone* z = g_MapInfo.FindZone(s == 0 ? ZONE_BOMBSITE_A : ZONE_BOMBSITE_B);
  return z ? z : g_MapInfo.FindZone(s == 0 ? ZONE_BOMBSITE_B : ZONE_BOMBSITE_A);
}
// staging point (T waits here before the execute) and the site entrance CTs hold, per site
static void UpdateMapCache() {
  if (s_map.serial == s_roundSerial) return;
  s_map.serial = s_roundSerial;
  s_map.tSpawn = TeamSpawnCenter(TEAM_TERRORIST); s_map.ctSpawn = TeamSpawnCenter(TEAM_CT);
  for (int i = 0; i < 2; i++) {
    s_map.ok[i] = false;
    const MapZone* z = SiteZone(i); if (!z) continue;
    std::vector<Vector> path;
    if (!g_NavMesh.ComputePath(s_map.tSpawn, z->Center(), path, nullptr, 0.f) || path.size() < 2) continue;
    s_map.stage[i] = PointFromPathEnd(path, 1000.f);
    s_map.entrance[i] = PointFromPathEnd(path, 550.f);
    s_map.ok[i] = true;
  }
}

static BotTeamPlan& GetPlan(int team) {
  BotTeamPlan& p = s_plan[TeamIdx(team)];
  if (p.serial == s_roundSerial) return p;
  p = BotTeamPlan();
  p.serial = s_roundSerial; p.roundStart = ServerTime();
  CCSGameRules* gr = CSGameRules();
  const bool ct = team == TEAM_CT;
  // ---- economy: the whole team buys the same way, like real teams do
  ConVar* cvMax = FindConVar("mp_maxrounds"); ConVar* cvHalf = FindConVar("mp_halftime"); ConVar* cvFreeze = FindConVar("mp_freezetime");
  int maxr = cvMax ? cvMax->GetInt() : 30, half = maxr / 2, played = gr->m_iTotalRoundsPlayed;
  bool halftime = cvHalf ? cvHalf->GetBool() : true;
  std::vector<CCSPlayer*> mates; int sum = 0;
  for (CCSPlayer* o : UTIL_GetPlayers()) if (o->m_iTeamNum == team) { mates.push_back(o); sum += o->m_iAccount; }
  float avg = mates.empty() ? 0.f : (float)sum / mates.size();
  int streak = ct ? gr->m_iNumConsecutiveCTLoses : gr->m_iNumConsecutiveTerroristLoses;
  float fullCost = ct ? 5000.f : 4500.f;                 // rifle + helmet + utility
  float nextLoss = (float)std::min(3400, 1400 + 500 * streak);
  bool pistolRound = played == 0 || (halftime && half > 0 && played == half);
  bool lastOfHalf = (halftime && played == half - 1) || (maxr > 0 && played == maxr - 1);
  int myWins = ct ? gr->m_iNumCTWins : gr->m_iNumTerroristWins, theirWins = ct ? gr->m_iNumTerroristWins : gr->m_iNumCTWins;
  bool matchPoint = maxr > 0 && (myWins == half || theirWins == half);
  bool afterPistolWin = (played == 1 || (halftime && played == half + 1)) && streak == 0;
  if (gr->IsDeathmatch() || gr->IsWarmupPeriod()) p.buy = BOTBUY_FULL;
  else if (pistolRound) p.buy = BOTBUY_PISTOL;
  else if (avg >= fullCost) p.buy = BOTBUY_FULL;
  else if (afterPistolWin) p.buy = BOTBUY_HALF;          // anti-eco SMGs after winning the pistol round
  else if (lastOfHalf || matchPoint) p.buy = BOTBUY_FORCE; // money is worthless after this round
  else if (avg + nextLoss >= fullCost * 0.95f || avg < 3200.f) p.buy = BOTBUY_ECO;   // save for a full buy
  else p.buy = BOTBUY_HALF;
  // ---- roles
  std::vector<CCSPlayer*> bots; for (CCSPlayer* o : mates) if (o->IsBot()) bots.push_back(o);
  int richest = -1;
  for (CCSPlayer* o : bots) {
    C_WeaponCSBase* pr = o->Weapon_GetSlot(WEAPON_SLOT_RIFLE);
    if (pr && pr->GetCSWeaponID() == WEAPON_AWP) { p.awper = o->entindex(); break; }
    if (o->m_iAccount >= 6200 && (richest < 0 || o->m_iAccount > richest)) { richest = o->m_iAccount; p.awper = o->entindex(); }
  }
  if (p.buy != BOTBUY_FULL && !(p.awper && bots.size() && UTIL_PlayerByIndex(p.awper) && UTIL_PlayerByIndex(p.awper)->Weapon_GetSlot(WEAPON_SLOT_RIFLE))) p.awper = 0;
  const MapZone* zA = g_MapInfo.FindZone(ZONE_BOMBSITE_A); const MapZone* zB = g_MapInfo.FindZone(ZONE_BOMBSITE_B);
  bool twoSites = zA && zB;
  if (!ct) {
    p.site = twoSites ? RandomInt(0, 1) : (zA ? 0 : 1);
    p.rush = (p.buy == BOTBUY_ECO || p.buy == BOTBUY_PISTOL) ? RandomInt(0, 2) == 0 : RandomInt(0, 4) == 0;
    p.executeTime = p.roundStart + (cvFreeze ? cvFreeze->GetFloat() : 0.f) + RandomFloat(22.f, 42.f);
    if (bots.size() >= 4 && twoSites && !p.rush) { CCSPlayer* l = bots[RandomInt(0, (int)bots.size() - 1)]; if (l->entindex() != p.awper) p.lurker = l->entindex(); }
    for (CCSPlayer* o : bots) { int i = o->entindex() & 63; p.botSite[i] = p.site; p.role[i] = o->entindex() == p.awper ? BOTROLE_AWP : o->entindex() == p.lurker ? BOTROLE_LURK : BOTROLE_DEFAULT; }
    if (p.lurker) p.botSite[p.lurker & 63] = 1 - p.site;
  } else {
    // split the bots between the sites, one rotator in the middle with 5+
    int k = RandomInt(0, 1);
    for (size_t j = 0; j < bots.size(); j++) {
      int i = bots[j]->entindex() & 63;
      p.botSite[i] = twoSites ? (int)((j + k) & 1) : (zA ? 0 : 1);
      p.role[i] = bots[j]->entindex() == p.awper ? BOTROLE_AWP : BOTROLE_DEFAULT;
    }
    if (bots.size() >= 5 && twoSites) { CCSPlayer* r = bots[RandomInt(0, (int)bots.size() - 1)]; if (r->entindex() != p.awper) p.role[r->entindex() & 63] = BOTROLE_ROTATOR; }
  }
  return p;
}

// nearest enemy position the team knows about (seen by any teammate within maxAge seconds)
static bool RecentIntel(const BotTeamPlan& p, float maxAge, const Vector& near, float radius, Vector* out, int* count) {
  float now = ServerTime(), best = radius; bool found = false; int n = 0;
  for (int i = 1; i < 65; i++) {
    if (now - p.enemySeen[i] > maxAge) continue;
    CCSPlayer* o = UTIL_PlayerByIndex(i); if (!o || !o->IsAlive()) continue;
    float d = p.enemyPos[i].DistTo(near);
    if (d > radius) continue;
    n++;
    if (d <= best) { best = d; if (out) *out = p.enemyPos[i]; found = true; }
  }
  if (count) *count = n;
  return found;
}

static ConCommand cmd_bot_plan("bot_plan", [](const CCommand&) {
  for (int t : { TEAM_TERRORIST, TEAM_CT }) {
    BotTeamPlan& p = GetPlan(t);
    Msg("%s: buy %s  site %c  %s  awper %d lurker %d  smokes %d flashes %d rotate %d\n", t == TEAM_CT ? "CT" : "T", BotBuyName(p.buy), 'A' + p.site,
        t == TEAM_CT ? "defend" : p.rush ? "RUSH" : p.executing ? "EXECUTING" : "default", p.awper, p.lurker, p.smokes, p.flashes, p.rotateSite);
  }
}, "Print the bot team plans (economy, strategy)");

// ------------------------------------------------------------------------------------------------
CCSBot::CCSBot(CCSPlayer* p) : me(p) {
  m_iDifficulty = bot_difficulty.GetInt();
  m_bHvH = bot_hvh.GetBool();
  m_flAggression = RandomFloat(0.15f, 0.95f);
  m_flBhopSkill = RandomFloat(0.f, 1.f);
  PickHvHProfile();
}
void CCSBot::SetState(int s) { if (m_iState != s) { m_iState = s; m_flStateStart = gpGlobals->curtime; } }

void CCSBot::OnSpawn() {
  m_Path.clear(); m_iPathIndex = 0; m_pEnemy = nullptr; m_flEnemyLastSeen = -100; m_bEnemyVisible = false;
  m_bBoughtThisRound = false; m_flBuyTime = ServerTime() + RandomFloat(0.3f, 1.6f);
  m_angView = me->v_angle; m_iState = BOT_IDLE; m_flRepathTime = 0; m_flStuckTime = 0;
  m_iTargetSite = RandomInt(0, 1);
  m_vecNoise = Vector(); m_flNoiseTime = -100; m_flHoldUntil = 0; m_vecHoldSpot = Vector();
  m_vecLastPos = me->m_vecOrigin; m_flLastProgress = ServerTime();
  m_iDifficulty = bot_difficulty.GetInt();
  m_bHvH = bot_hvh.GetBool();
  m_iPlanSerial = -1; m_bRotating = false; m_iSeenRotate = -1; m_bSaving = false; m_bDropped = false; m_bBuyWaited = false;
  m_bBhopping = false; m_iPeek = PEEK_NONE; m_iFightPhase = 0; m_flRetreatUntil = 0;
  m_iThrowPhase = 0; m_flNextThrow = ServerTime() + 3.f; m_vecStageSpot = Vector(); m_flUnstickUntil = 0;
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
  if (!strcmp(name, "player_hurt") && src == me) {
    if (m_bHvH) m_iDesyncSide = -m_iDesyncSide;      // got hit: switch desync side like a real hvh cheat
    CCSPlayer* a = UTIL_PlayerByIndex(attacker);
    if (a && a != me && a->m_iTeamNum != me->m_iTeamNum && (!m_pEnemy || !m_bEnemyVisible)) {
      m_vecNoise = a->m_vecOrigin; m_flNoiseTime = ServerTime();
      // hit from behind: turn around and fight
      m_pEnemy = a; m_vecEnemyLastPos = a->m_net.origin; m_flEnemyLastSeen = ServerTime(); m_iPeek = PEEK_NONE;
    }
  }
}

bool CCSBot::IsVisible(CCSPlayer* o, Vector* aimPoint) const {
  if (!o || !o->IsAlive() || !o->m_net.valid) return false;
  Vector eye = me->EyePosition();
  const HitboxSet& hb = o->m_net.hitboxes;
  int order[] = { HITBOX_HEAD, HITBOX_UPPER_CHEST, HITBOX_STOMACH, HITBOX_PELVIS, HITBOX_LEFT_FOREARM, HITBOX_RIGHT_FOREARM, HITBOX_LEFT_CALF, HITBOX_RIGHT_CALF };
  for (int h : order) {
    Vector p = (hb[h].a + hb[h].b) * 0.5f;
    if (WorldLOS(eye, p)) { if (aimPoint) *aimPoint = p; return true; }
  }
  return false;
}

static float AngleDistance(const QAngle& a, const QAngle& b) { return std::sqrt(AngleDiff(a.x, b.x) * AngleDiff(a.x, b.x) + AngleDiff(a.y, b.y) * AngleDiff(a.y, b.y)); }

void CCSBot::UpdatePerception() {
  float now = ServerTime();
  Vector eye = me->EyePosition();
  Vector fwd; AngleVectors(me->v_angle, &fwd);
  CCSPlayer* best = nullptr; float bestScore = 1e9f;
  bool blind = me->IsBlind();
  BotTeamPlan& plan = GetPlan(me->m_iTeamNum);
  for (CCSPlayer* o : UTIL_GetPlayers(true)) {
    if (!me->IsEnemy(o)) continue;
    Vector to = o->m_net.origin + Vector(0, 0, 40) - eye;
    float dist = to.NormalizeInPlace();
    float dot = fwd.Dot(to);
    bool inFov = dot > (m_bHvH ? -1.f : 0.25f) || dist < 200.f || o == m_pEnemy;
    if (!inFov || blind) continue;
    if (!IsVisible(o, nullptr)) continue;
    // callout for the team
    plan.enemyPos[o->entindex() & 63] = o->m_net.origin; plan.enemySeen[o->entindex() & 63] = now;
    float score = dist * (1.5f - dot) * (o == m_pEnemy ? 0.5f : 1.f);
    if (score < bestScore) { bestScore = score; best = o; }
  }
  // CTs: two or more Ts seen near a site -> rotate there
  if (me->m_iTeamNum == TEAM_CT && best && !CSGameRules()->IsDeathmatch()) {
    for (int s = 0; s < 2; s++) {
      const MapZone* z = SiteZone(s); if (!z) continue;
      int cnt = 0; RecentIntel(plan, 3.f, z->Center(), 1300.f, nullptr, &cnt);
      if (cnt >= 2 && (plan.rotateSite != s || now - plan.rotateTime > 10.f)) { plan.rotateSite = s; plan.rotateTime = now; plan.rotateSerial++; }
    }
  }
  m_bEnemyVisible = best != nullptr;
  if (best) {
    if (best != m_pEnemy || now - m_flEnemyLastSeen > 1.f) {
      static const float reaction[4] = { 0.55f, 0.35f, 0.22f, 0.14f };
      m_flEnemyFirstSeen = now;
      float r = m_bHvH ? 0.03f : reaction[Clamp(m_iDifficulty, 0, 3)] * RandomFloat(0.8f, 1.3f);
      m_angAimError = QAngle(RandomFloat(-4, 4), RandomFloat(-6, 6), 0) * (float)(4 - m_iDifficulty) * 0.5f;
      // pre-aimed (crosshair already on the angle he came from): much faster and more precise
      if (AngleDistance(me->v_angle, CalcAngle(eye, best->m_net.origin + Vector(0, 0, 56))) < 12.f) { r *= 0.55f; m_angAimError = m_angAimError * 0.4f; }
      m_flReactionEnd = now + r;
      m_iFightPhase = (m_iDifficulty >= 2 && RandomFloat(0, 1) < 0.5f) ? 0 : 1; m_flPhaseEnd = now + RandomFloat(0.1f, 0.25f);
      m_bCrouchSpray = m_iDifficulty >= 2 && RandomFloat(0, 1) < 0.4f;
      m_iStrafeDir = RandomInt(0, 1) ? 1 : -1;
    }
    m_pEnemy = best; m_flEnemyLastSeen = now; m_vecEnemyLastPos = best->m_net.origin; m_iPeek = PEEK_NONE;
  } else if (m_pEnemy) {
    bool forget = !m_pEnemy->IsAlive() || (m_iPeek == PEEK_NONE ? now - m_flEnemyLastSeen > 3.f : now > m_flPeekUntil);
    if (forget) { m_pEnemy = nullptr; m_iPeek = PEEK_NONE; }
  }
}

void CCSBot::EquipBestWeapon() {
  if (m_iThrowPhase) return;
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
  if (m_pEnemy && m_bEnemyVisible && act == rifle && rifle && rifle->m_iClip1 == 0 && pistol && pistol->m_iClip1 > 0) want = pistol;
  if (m_pEnemy && act == pistol && rifle && rifle->m_iClip1 == 0 && act && act->m_iClip1 > 0) want = pistol;   // don't flip back mid fight
  if (want && want != act) me->Weapon_Switch(want);
}

// ------------------------------------------------------------------------------------------------
// economy
void CCSBot::Buy() {
  if (m_bBoughtThisRound || !me->m_bInBuyZone || ServerTime() < m_flBuyTime) return;
  m_bBoughtThisRound = true;
  const bool ct = me->m_iTeamNum == TEAM_CT;
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
  if (bot_snipers_only.GetBool()) { if (me->m_iAccount >= 4750) me->BuyItem("awp", true); else me->BuyItem("ssg08", true); me->BuyItem("vest", true); EquipBestWeapon(); return; }
  BotTeamPlan& plan = GetPlan(me->m_iTeamNum);
  const int idx = me->entindex() & 63;
  int buy = plan.buy;
  C_WeaponCSBase* prim = me->Weapon_GetSlot(WEAPON_SLOT_RIFLE);
  if (buy == BOTBUY_ECO && me->m_iAccount >= 7500) buy = BOTBUY_FULL;          // nothing left to save for
  if (prim && buy != BOTBUY_PISTOL) buy = BOTBUY_FULL;                         // kept a gun: just top up
  // kept only a cheap gun (SMG / shotgun) on a full buy round: upgrade it
  if (prim && plan.buy == BOTBUY_FULL && prim->GetCSWpnData().price < 2500 && me->m_iAccount >= PriceOf(ct ? "m4a1" : "ak47") + 1000) prim = nullptr;
  const char* rifle = ct ? (RandomInt(0, 2) ? "m4a1" : "m4a1_silencer") : "ak47";
  const int rifleCost = PriceOf(rifle);
  auto money = [&]() { return me->m_iAccount; };
  auto nades = [&](int maxSpend) {
    int start = money();
    auto spend = [&]() { return start - money(); };
    if (spend() + 300 <= maxSpend && money() >= 300) me->BuyItem("smokegrenade", true);
    if (spend() + 200 <= maxSpend && money() >= 200) me->BuyItem("flashbang", true);
    if (spend() + 300 <= maxSpend && money() >= 300) me->BuyItem("hegrenade", true);
    if (spend() + 200 <= maxSpend && money() >= 200 && RandomInt(0, 1)) me->BuyItem("flashbang", true);
  };
  auto armor = [&](bool helmet) {
    if (helmet && money() >= (me->m_ArmorValue >= 100 ? 350 : 1000)) me->BuyItem("vesthelm", true);
    else if (me->m_ArmorValue < 70 && money() >= 650) me->BuyItem("vest", true);
  };
  if (bot_pistols_only.GetBool() && buy != BOTBUY_ECO) buy = BOTBUY_PISTOL;
  switch (buy) {
  case BOTBUY_PISTOL: {
    int r = RandomInt(0, 3);
    if (r == 0) armor(false);
    else if (r == 1) { me->BuyItem(ct ? "fiveseven" : "tec9", true) || me->BuyItem("p250", true); me->BuyItem("flashbang", true); }
    else if (r == 2) { me->BuyItem("p250", true); if (ct) me->BuyItem("defuser", true); else me->BuyItem("hegrenade", true); }
    else { if (ct && RandomInt(0, 1)) me->BuyItem("defuser", true); me->BuyItem("smokegrenade", true); me->BuyItem("flashbang", true); me->BuyItem("flashbang", true); }
    break;
  }
  case BOTBUY_ECO:
    // save; at most a cheap pistol upgrade
    if (money() >= 2400 && RandomInt(0, 2) == 0) me->BuyItem(RandomInt(0, 1) ? "p250" : (ct ? "fiveseven" : "tec9"), true);
    break;
  case BOTBUY_HALF: {
    static const char* const ctS[] = { "mp9", "ump45", "mp7", "mp5sd" }, * const tS[] = { "mac10", "ump45", "mp7", "bizon" };
    if (!prim) { const char* smg = (ct ? ctS : tS)[RandomInt(0, 3)]; if (!me->BuyItem(smg, true)) me->BuyItem(ct ? "mp9" : "mac10", true); }
    armor(money() >= 1600);
    nades(std::max(0, money() - 1000));          // keep some money for next round
    break;
  }
  case BOTBUY_FORCE:
    armor(false);
    if (!prim) {
      if (money() >= PriceOf(ct ? "famas" : "galilar") + 300) me->BuyItem(ct ? "famas" : "galilar", true);
      else if (money() >= 1250) me->BuyItem(ct ? "mp9" : "mac10", true);
      else if (money() >= 700) me->BuyItem("deagle", true) || me->BuyItem("p250", true);
      else if (money() >= 300) me->BuyItem("p250", true);
    }
    armor(true);
    nades(money());
    break;
  case BOTBUY_FULL: default: {
    bool awper = plan.role[idx] == BOTROLE_AWP;
    if (!prim) {
      // poor: wait a moment for a teammate's drop
      if (money() < rifleCost + 1000 && !m_bBuyWaited && !CSGameRules()->IsDeathmatch()) { m_bBuyWaited = true; m_bBoughtThisRound = false; m_flBuyTime = ServerTime() + 2.2f; return; }
      // rich: buy a rifle for the poorest teammate without a gun and throw it to him
      if (!m_bDropped && !CSGameRules()->IsDeathmatch()) {
        CCSPlayer* poorest = nullptr;
        for (CCSPlayer* o : UTIL_GetPlayers(true)) {
          if (o == me || o->m_iTeamNum != me->m_iTeamNum || o->Weapon_GetSlot(WEAPON_SLOT_RIFLE) || plan.dropFor[o->entindex() & 63]) continue;
          const char* theirRifle = ct ? "m4a1" : "ak47";
          if (o->m_iAccount >= PriceOf(theirRifle) + 650 || o->m_vecOrigin.DistTo(me->m_vecOrigin) > 700.f) continue;
          if (!poorest || o->m_iAccount < poorest->m_iAccount) poorest = o;
        }
        int ownCost = (awper ? PriceOf("awp") : rifleCost) + 1000;
        if (poorest && money() >= ownCost + rifleCost + 300) {
          if (me->BuyItem(rifle, true)) {
            if (C_WeaponCSBase* gift = me->Weapon_GetSlot(WEAPON_SLOT_RIFLE)) {
              QAngle saved = me->v_angle;
              Vector to = poorest->m_vecOrigin - me->m_vecOrigin;
              me->v_angle = QAngle(-Clamp(to.Length2D() / 25.f, 0.f, 30.f), CalcAngle(me->m_vecOrigin, poorest->m_vecOrigin).y, 0);
              me->Weapon_Drop(gift, true);
              me->v_angle = saved;
              plan.dropFor[poorest->entindex() & 63] = true; m_bDropped = true;
              if (bot_debug.GetBool()) Msg("[bot] %s drops %s for %s ($%d)\n", me->GetPlayerName(), rifle, poorest->GetPlayerName(), poorest->m_iAccount);
            }
          }
        }
      }
      if (plan.dropFor[idx]) { /* a teammate threw us a rifle - pick it up after the freeze time */ }
      else if (awper && money() >= PriceOf("awp") + 650) me->BuyItem("awp", true);
      else if (money() >= rifleCost + 650) me->BuyItem(rifle, true);
      else if (money() >= PriceOf(ct ? "famas" : "galilar") + 650) me->BuyItem(ct ? "famas" : "galilar", true);
      else if (money() >= 1900) me->BuyItem(ct ? "mp9" : "mac10", true);
    }
    armor(true);
    if (ct && money() >= 400) me->BuyItem("defuser", true);
    nades(money());
    if (money() >= 1200 && (awper || !me->Weapon_GetSlot(WEAPON_SLOT_RIFLE))) me->BuyItem(RandomInt(0, 1) ? "deagle" : "p250", true);
    break;
  }
  }
  EquipBestWeapon();
}

bool CCSBot::SetGoal(const Vector& goal, bool force) {
  float now = ServerTime();
  if (!force && now < m_flUnstickUntil && !m_Path.empty()) return true;   // walking out of a stuck spot: keep that detour
  if (!force && (goal - m_vecGoal).Length2D() < 48.f && !m_Path.empty() && now < m_flRepathTime) return true;
  m_vecGoal = goal;
  m_flRepathTime = now + RandomFloat(2.5f, 4.f);
  bool ok = g_NavMesh.ComputePath(me->m_vecOrigin, goal, m_Path, &m_PathFlags, m_iDifficulty >= 2 ? 2.f : 0.f);
  m_iPathIndex = m_Path.size() > 1 ? 1 : 0;
  return ok;
}

// spot near 'center' with a clear view of 'lookAt' (a real angle to hold), spread from teammates
Vector CCSBot::PickAngleSpot(const Vector& center, float radius, const Vector& lookAt, float minDist) const {
  Vector best = g_NavMesh.GetRandomWalkablePoint(center, radius); float bestScore = -1e9f;
  for (int i = 0; i < 14; i++) {
    Vector p = g_NavMesh.GetRandomWalkablePoint(center, radius);
    float d = p.DistTo(lookAt);
    if (d < minDist) continue;
    if (!WorldLOS(p + Vector(0, 0, 64), lookAt + Vector(0, 0, 48))) continue;
    float score = RandomFloat(0, 300) - std::fabs(d - 700.f) * 0.3f;
    for (CCSPlayer* o : UTIL_GetPlayers(true)) {
      if (o == me || o->m_iTeamNum != me->m_iTeamNum) continue;
      if (o->m_vecOrigin.DistTo(p) < 150.f) score -= 250.f;
      if (CCSBot* b = GetBot(o)) if (b->m_vecHoldSpot.DistTo(p) < 150.f) score -= 250.f;
    }
    if (score > bestScore) { bestScore = score; best = p; }
  }
  return best;
}

bool CCSBot::FindCover(const Vector& threat, Vector& out) const {
  float best = 1e9f; bool found = false;
  float myDist = me->m_vecOrigin.DistTo(threat);
  for (int i = 0; i < 16; i++) {
    Vector p = g_NavMesh.GetRandomWalkablePoint(me->m_vecOrigin, 450.f);
    if (p.DistTo(threat) < myDist - 60.f) continue;            // never run toward him
    if (WorldLOS(threat, p + Vector(0, 0, 60))) continue;
    float d = p.DistTo(me->m_vecOrigin);
    if (d < best) { best = d; out = p; found = true; }
  }
  return found;
}

// grab a dropped primary (a teammate's drop, a dead player's rifle, an AWP upgrade)
bool CCSBot::ThinkPickup() {
  if (m_bHvH || bot_knives_only.GetBool() || bot_pistols_only.GetBool()) return false;
  C_WeaponCSBase* prim = me->Weapon_GetSlot(WEAPON_SLOT_RIFLE);
  int myPrice = prim ? prim->GetCSWpnData().price : 0;
  C_WeaponCSBase* best = nullptr; float bestD = 1e9f;
  g_EntityList.ForEach([&](C_BaseEntity* e) {
    if (!e->IsWeapon()) return;
    auto* w = static_cast<C_WeaponCSBase*>(e);
    if (w->m_iState != WEAPON_NOT_CARRIED || w->GetCSWpnData().slot != WEAPON_SLOT_RIFLE) return;
    if (w->m_iClip1 + w->m_iPrimaryReserveAmmoCount <= 0) return;
    if (w->m_hPrevOwner == me && ServerTime() < w->m_flDroppedTime + 5.f) return;
    if (bot_snipers_only.GetBool() && w->GetWeaponType() != WEAPONTYPE_SNIPER_RIFLE) return;
    float d = w->m_vecOrigin.DistTo(me->m_vecOrigin);
    float maxD = !prim ? 750.f : (w->GetCSWpnData().price >= myPrice + 1200 ? 320.f : 0.f);
    if (d < maxD && d < bestD && std::fabs(w->m_vecOrigin.z - me->m_vecOrigin.z) < 120.f) { bestD = d; best = w; }
  });
  if (!best) return false;
  if (prim && bestD < 80.f) me->Weapon_Drop(prim, false);     // swap for the better gun
  SetState(BOT_MOVE_TO); SetGoal(best->m_vecOrigin);
  return true;
}

// lost round / outnumbered with an expensive gun: save it
void CCSBot::ThinkSave() {
  CCSGameRules* gr = CSGameRules();
  if (m_bSaving || m_bHvH || gr->IsDeathmatch() || m_iDifficulty < 1) return;
  float now = ServerTime(), left = gr->GetRoundRemainingTime();
  bool ct = me->m_iTeamNum == TEAM_CT;
  if (ct && g_pPlantedC4 && !g_pPlantedC4->m_bBombDefused && !g_pPlantedC4->m_bExploded) {
    float blow = g_pPlantedC4->m_flC4Blow - now;
    // travel time along our actual path when it leads to the bomb, otherwise a rough guess
    float travel = me->m_vecOrigin.DistTo(g_pPlantedC4->m_vecOrigin) * 1.25f;
    if (!m_Path.empty() && m_vecGoal.DistTo(g_pPlantedC4->m_vecOrigin) < 100.f) {
      travel = 0; Vector prev = me->m_vecOrigin;
      for (size_t i = m_iPathIndex; i < m_Path.size(); i++) { travel += prev.DistTo(m_Path[i]); prev = m_Path[i]; }
    }
    float need = (me->m_bHasDefuser ? 5.f : 10.f) + travel / 235.f + 0.3f;
    if (blow < need) { m_bSaving = true; if (bot_debug.GetBool()) Msg("[bot] %s saves: no time to defuse (%.1f s left, needs %.1f) state %s dist %.0f enemy %d peek %d path %d\n", me->GetPlayerName(), blow, need, BotStateName(m_iState), me->m_vecOrigin.DistTo(g_pPlantedC4->m_vecOrigin), m_pEnemy != nullptr, m_iPeek, (int)m_Path.size()); }
    return;
  }
  if (!ct && !g_pPlantedC4) {
    if (me->HasC4()) { const MapZone* z = SiteZone(m_iTargetSite); if (z && left < me->m_vecOrigin.DistTo(z->Center()) / 240.f + 4.f) m_bSaving = true; }
    else if (left < 7.f) {
      bool carrierOnSite = false;
      for (CCSPlayer* o : UTIL_GetPlayers(true)) if (o->m_iTeamNum == TEAM_TERRORIST && o->HasC4() && o->m_bInBombZone) carrierOnSite = true;
      if (!carrierOnSite) m_bSaving = true;
    }
  }
  int mates = 0, enemies = 0;
  for (CCSPlayer* o : UTIL_GetPlayers(true)) { if (o == me) continue; if (o->m_iTeamNum == me->m_iTeamNum) mates++; else if (me->IsEnemy(o)) enemies++; }
  C_WeaponCSBase* prim = me->Weapon_GetSlot(WEAPON_SLOT_RIFLE);
  bool bombCase = ct ? g_pPlantedC4 != nullptr : g_pPlantedC4 == nullptr && me->HasC4();
  if (mates == 0 && enemies >= 3 && prim && prim->GetCSWpnData().price >= 2700 && left < 35.f && !bombCase && m_flAggression < 0.8f) m_bSaving = true;
  if (m_bSaving && bot_debug.GetBool()) Msg("[bot] %s saves\n", me->GetPlayerName());
}

void CCSBot::ThinkGoals() {
  CCSGameRules* gr = CSGameRules();
  float now = ServerTime();
  bool ct = me->m_iTeamNum == TEAM_CT;
  if (gr->IsDeathmatch() || !SiteZone(0)) {
    // hunt the nearest enemy
    CCSPlayer* nearest = nullptr; float nd = 1e9f;
    for (CCSPlayer* o : UTIL_GetPlayers(true)) if (me->IsEnemy(o)) { float d = o->m_vecOrigin.DistTo(me->m_vecOrigin); if (d < nd) { nd = d; nearest = o; } }
    if (nearest) { SetState(BOT_HUNT); SetGoal(nearest->m_net.origin); }
    m_bRushing = true;
    return;
  }
  UpdateMapCache();
  BotTeamPlan& plan = GetPlan(me->m_iTeamNum);
  const int idx = me->entindex() & 63;
  if (m_iPlanSerial != plan.serial) {
    m_iPlanSerial = plan.serial;
    m_iRole = plan.role[idx]; m_iTargetSite = plan.botSite[idx];
    m_vecStageSpot = Vector(); m_flHoldUntil = 0;
  }
  const MapZone* site = SiteZone(m_iTargetSite);
  m_bRushing = false;
  // saving: go back home and hide
  if (m_bSaving) {
    Vector home = ct ? s_map.ctSpawn : s_map.tSpawn;
    if (g_pPlantedC4) { Vector away = (me->m_vecOrigin - g_pPlantedC4->m_vecOrigin); away.z = 0; if (me->m_vecOrigin.DistTo(g_pPlantedC4->m_vecOrigin) < 1500.f) home = me->m_vecOrigin + away.Normalized() * 900.f; home = g_NavMesh.GetRandomWalkablePoint(home, 200.f); }
    if (m_vecHoldSpot.DistTo(home) > 600.f) m_vecHoldSpot = g_NavMesh.GetRandomWalkablePoint(home, 300.f);
    if (me->m_vecOrigin.DistTo(m_vecHoldSpot) > 64.f) { SetState(BOT_MOVE_TO); SetGoal(m_vecHoldSpot); } else { SetState(BOT_HOLD); m_vecHoldLook = me->m_vecOrigin + (me->m_vecOrigin - home) * 2.f; }
    return;
  }
  if (now - plan.roundStart < 25.f && ThinkPickup()) return;
  // the bomb is down
  if (g_pPlantedC4 && !g_pPlantedC4->m_bBombDefused && !g_pPlantedC4->m_bExploded) {
    Vector bomb = g_pPlantedC4->m_vecOrigin;
    if (ct) {
      float d = me->m_vecOrigin.DistTo(bomb);
      if (d < 60.f && (!m_pEnemy || now - m_flEnemyLastSeen > 1.5f)) { SetState(BOT_DEFUSE); return; }
      m_bRushing = g_pPlantedC4->m_flC4Blow - now < 20.f || d > 900.f;
      SetState(BOT_MOVE_TO); SetGoal(bomb);
    } else {
      // post plant: hold angles on the bomb from cover
      if (m_flHoldUntil < now || (m_vecHoldSpot - bomb).Length2D() > 800.f) {
        m_vecHoldSpot = PickAngleSpot(bomb, 650.f, bomb, 250.f); m_flHoldUntil = now + 25.f;
        m_vecHoldLook = bomb + Vector(0, 0, 30);
      }
      m_bRushing = me->m_vecOrigin.DistTo(bomb) > 1500.f;
      if (me->m_vecOrigin.DistTo(m_vecHoldSpot) > 64.f) { SetState(BOT_MOVE_TO); SetGoal(m_vecHoldSpot); } else SetState(BOT_HOLD);
    }
    return;
  }
  if (!site) return;
  const Vector siteC = site->Center();
  if (!ct) {
    float left = gr->GetRoundRemainingTime();
    // execute: enough of us at the staging point or the timer says go
    if (!plan.executing && !plan.rush) {
      int alive = 0, staged = 0;
      for (CCSPlayer* o : UTIL_GetPlayers(true)) if (o->m_iTeamNum == TEAM_TERRORIST) {
        alive++;
        if (s_map.ok[plan.site] && o->m_vecOrigin.DistTo(s_map.stage[plan.site]) < 500.f) staged++;
      }
      if (now >= plan.executeTime || left < 50.f || (staged >= std::max(2, (int)(alive * 0.7f)) && now - plan.roundStart > 15.f)) plan.executing = true;
    }
    bool go = plan.rush || plan.executing;
    if (me->HasC4()) {
      if (me->m_bInBombZone && site->Contains(me->m_vecOrigin)) { SetState(BOT_PLANT); return; }
      if (go || left < 40.f || !s_map.ok[m_iTargetSite]) { m_bRushing = true; SetState(BOT_MOVE_TO); SetGoal(siteC); return; }
    }
    // fetch a dropped bomb
    if (gr->m_bBombDropped) {
      C_WeaponCSBase* c4 = nullptr;
      g_EntityList.ForEach([&](C_BaseEntity* e) { if (e->IsWeapon() && static_cast<C_WeaponCSBase*>(e)->GetCSWeaponID() == WEAPON_C4 && static_cast<C_WeaponCSBase*>(e)->m_iState == WEAPON_NOT_CARRIED) c4 = static_cast<C_WeaponCSBase*>(e); });
      if (c4) {
        CCSPlayer* closest = nullptr; float cd = 1e9f;
        for (CCSPlayer* o : UTIL_GetPlayers(true)) if (o->m_iTeamNum == TEAM_TERRORIST && o->IsBot()) { float d = o->m_vecOrigin.DistTo(c4->m_vecOrigin); if (d < cd) { cd = d; closest = o; } }
        if (closest == me) { m_bRushing = true; SetState(BOT_FETCH_BOMB); SetGoal(c4->m_vecOrigin); return; }
      }
    }
    // aggressive players go for sounds they hear (not during the execute)
    if (!go && m_vecNoise.LengthSqr() > 0 && now - m_flNoiseTime < 4.f && m_flAggression > 0.6f && me->m_vecOrigin.DistTo(m_vecNoise) < 900.f) { SetState(BOT_INVESTIGATE); SetGoal(m_vecNoise); return; }
    // lurker: wait quietly at the other site, join late
    if (m_iRole == BOTROLE_LURK && left > 25.f && s_map.ok[m_iTargetSite] && m_iTargetSite != plan.site) {
      if (m_vecStageSpot.IsZero()) m_vecStageSpot = g_NavMesh.GetRandomWalkablePoint(s_map.stage[m_iTargetSite], 200.f);
      m_vecHoldLook = siteC + Vector(0, 0, 40);
      if (me->m_vecOrigin.DistTo(m_vecStageSpot) > 64.f) { SetState(BOT_MOVE_TO); SetGoal(m_vecStageSpot); } else SetState(BOT_HOLD);
      return;
    }
    if (m_iRole == BOTROLE_LURK && m_iTargetSite != plan.site) { m_iTargetSite = plan.site; return; }
    if (!go && s_map.ok[plan.site]) {
      // default: take map control at the staging point and wait for the team
      if (m_vecStageSpot.IsZero()) m_vecStageSpot = g_NavMesh.GetRandomWalkablePoint(s_map.stage[plan.site], 260.f);
      m_vecHoldLook = SiteZone(plan.site)->Center() + Vector(0, 0, 40);
      if (me->m_vecOrigin.DistTo(m_vecStageSpot) > 64.f) { SetState(BOT_MOVE_TO); SetGoal(m_vecStageSpot); } else SetState(BOT_HOLD);
      return;
    }
    // go in, then take spots on the site looking toward the CTs
    m_bRushing = true;
    if (me->m_vecOrigin.DistTo(siteC) < 320.f) {
      if (m_flHoldUntil < now) {
        Vector ctDir = s_map.ctSpawn - siteC; ctDir.z = 0;
        Vector look = siteC + ctDir.Normalized() * 600.f;
        m_vecHoldSpot = PickAngleSpot(siteC, 380.f, look, 200.f); m_vecHoldLook = look + Vector(0, 0, 40); m_flHoldUntil = now + RandomFloat(6, 14);
      }
      if (me->m_vecOrigin.DistTo(m_vecHoldSpot) > 64.f) { SetState(BOT_MOVE_TO); SetGoal(m_vecHoldSpot); } else SetState(BOT_HOLD);
      return;
    }
    SetState(BOT_MOVE_TO); SetGoal(siteC);
  } else {
    // rotate on callouts
    if (plan.rotateSite >= 0 && now - plan.rotateTime < 12.f && m_iSeenRotate != plan.rotateSerial) {
      m_iSeenRotate = plan.rotateSerial;
      if (plan.rotateSite != m_iTargetSite && (m_iRole == BOTROLE_ROTATOR || RandomFloat(0, 1) < 0.45f)) { m_iTargetSite = plan.rotateSite; m_bRotating = true; m_flHoldUntil = 0; }
    }
    if (m_vecNoise.LengthSqr() > 0 && now - m_flNoiseTime < 4.f && m_flAggression > 0.55f && me->m_vecOrigin.DistTo(m_vecNoise) < 700.f) { SetState(BOT_INVESTIGATE); SetGoal(m_vecNoise); return; }
    if (m_flHoldUntil < now || m_vecHoldSpot.IsZero()) {
      Vector entrance = s_map.ok[m_iTargetSite] ? s_map.entrance[m_iTargetSite] : siteC + (s_map.tSpawn - siteC).Normalized() * 500.f;
      Vector center = siteC; float radius = 450.f;
      if (m_iRole == BOTROLE_ROTATOR && !m_bRotating && SiteZone(0) && SiteZone(1)) {
        center = (SiteZone(0)->Center() + SiteZone(1)->Center()) * 0.5f; radius = 500.f;
        entrance = s_map.ok[0] && s_map.ok[1] ? (s_map.stage[0] + s_map.stage[1]) * 0.5f : s_map.tSpawn;
      }
      m_vecHoldSpot = PickAngleSpot(center, radius, entrance, 250.f);
      m_vecHoldLook = entrance + Vector(0, 0, 48);
      m_flHoldUntil = now + RandomFloat(15, 35);
    }
    m_bRushing = m_bRotating && me->m_vecOrigin.DistTo(m_vecHoldSpot) > 900.f;
    if (me->m_vecOrigin.DistTo(m_vecHoldSpot) > 64.f) { SetState(BOT_MOVE_TO); SetGoal(m_vecHoldSpot); } else { SetState(BOT_HOLD); m_bRotating = false; }
  }
}

// ------------------------------------------------------------------------------------------------
// movement
void CCSBot::CounterStrafe(CUserCmd& cmd, const QAngle& view) const {
  cmd.forwardmove = cmd.sidemove = 0;
  Vector v = me->m_vecVelocity; v.z = 0;
  float spd = v.Length();
  if (spd < 30.f || !(me->m_fFlags & FL_ONGROUND)) return;
  v = v * (1.f / spd);
  float yaw = DEG2RADF(view.y);
  Vector f(std::cos(yaw), std::sin(yaw), 0), r(std::sin(yaw), -std::cos(yaw), 0);
  cmd.forwardmove = -v.Dot(f) * 450.f; cmd.sidemove = -v.Dot(r) * 450.f;     // tap the opposite keys
}

// bunny hop on long straight runs + air strafe (view follows the velocity, A/D toward the path)
void CCSBot::Bhop(CUserCmd& cmd, QAngle& view, const Vector& target) {
  bool onGround = (me->m_fFlags & FL_ONGROUND) != 0;
  Vector vel = me->m_vecVelocity; vel.z = 0;
  float spd = vel.Length();
  if (!onGround) {
    if (!m_bBhopping || spd < 100.f) return;
    float velYaw = RAD2DEGF(std::atan2(vel.y, vel.x));
    float want = CalcAngle(me->m_vecOrigin, target).y;
    float diff = AngleDiff(want, velYaw);
    int side;
    if (std::fabs(diff) > 4.f) side = diff > 0 ? -1 : 1;
    else { if (++m_iAirSideTicks > 6) { m_iAirSide = -m_iAirSide; m_iAirSideTicks = 0; } side = m_iAirSide; }
    view.y = velYaw;                     // wish direction perpendicular to the velocity = maximum gain
    view.x = ApproachAngle(5.f, view.x, 90.f * gpGlobals->interval_per_tick);
    cmd.forwardmove = 0; cmd.sidemove = 450.f * side;
    return;
  }
  m_bBhopping = false;
  if (spd < 200.f || m_iPathIndex >= m_Path.size()) return;
  // straight, flat, plain walk links ahead?
  Vector dir = vel * (1.f / spd);
  float straight = 0; Vector prev = me->m_vecOrigin;
  for (size_t k = m_iPathIndex; k < m_Path.size() && straight < 320.f; k++) {
    if (m_PathFlags.size() > k && m_PathFlags[k] != NAV_WALK) break;
    Vector seg = m_Path[k] - prev; float zd = seg.z; seg.z = 0;
    float len = seg.Length(); if (len < 1.f) continue;
    if (std::fabs(zd) > 28.f || dir.Dot(seg * (1.f / len)) < 0.9f) break;
    straight += len; prev = m_Path[k];
  }
  if (straight < 320.f) return;
  trace_t tr; g_EngineTrace.TraceHull(me->m_vecOrigin + Vector(0, 0, 20), me->m_vecOrigin + Vector(0, 0, 20) + dir * 160.f, Vector(-16, -16, 0), Vector(16, 16, 50), MASK_PLAYERSOLID_BRUSHONLY, me, &tr);
  if (tr.fraction < 1.f) return;
  if (!(m_LastCmd.buttons & IN_JUMP)) { cmd.buttons |= IN_JUMP; m_bBhopping = true; m_iAirSide = RandomInt(0, 1) ? 1 : -1; m_iAirSideTicks = 0; }
}

void CCSBot::MoveTowardsGoal(CUserCmd& cmd, QAngle& view) {
  float now = ServerTime();
  if (m_iState == BOT_HOLD || m_iState == BOT_PLANT || m_iState == BOT_DEFUSE) { m_flLastProgress = now; m_vecLastPos = me->m_vecOrigin; if (m_iState == BOT_HOLD) CounterStrafe(cmd, view); return; }
  if (m_Path.empty() || m_iPathIndex >= m_Path.size()) {
    // no path but not at the goal: wander a bit and retry
    if (m_vecGoal.DistTo(me->m_vecOrigin) > 96.f && now - m_flLastProgress > 1.f) {
      SetGoal(g_NavMesh.GetRandomWalkablePoint(me->m_vecOrigin, 400.f), true);
      m_flLastProgress = now;
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
  if ((flag == NAV_JUMP || flag == NAV_CROUCHJUMP) && dist2 < 72.f && onGround && target.z > me->m_vecOrigin.z + 16) { cmd.buttons |= IN_JUMP; m_bBhopping = false; }
  if (!onGround && !m_bBhopping && (flag == NAV_CROUCHJUMP || me->m_vecVelocity.z > 0)) cmd.buttons |= IN_DUCK;
  // bunny hop when it's safe and the path is straight
  bool canHop = !m_bWalk && !m_pEnemy && !m_iThrowPhase && m_iDifficulty >= 1 && m_flBhopSkill > (m_bRushing ? 0.25f : 0.55f) && !bot_crouch.GetBool() &&
                (m_iState == BOT_MOVE_TO || m_iState == BOT_FETCH_BOMB || m_iState == BOT_HUNT || m_iState == BOT_INVESTIGATE) && flag == NAV_WALK;
  if (canHop || (m_bBhopping && !onGround)) {
    Vector far = m_Path[std::min(m_Path.size() - 1, m_iPathIndex + 1)];
    Bhop(cmd, view, far);
  } else m_bBhopping = false;
  // stuck detection
  if (me->m_vecOrigin.DistTo(m_vecLastPos) > 24.f) { m_vecLastPos = me->m_vecOrigin; m_flLastProgress = now; }
  else if (now - m_flLastProgress > 0.8f) {
    // jump + duck out of it; also when wedged in the air (re-press every other tick so the jump re-triggers)
    if (onGround || (gpGlobals->tickcount & 1)) cmd.buttons |= IN_JUMP | IN_DUCK;
    if (now - m_flLastProgress > 1.6f) { cmd.sidemove = (me->entindex() & 1 ? 1 : -1) * 450.f; }
    if (now - m_flLastProgress > 2.5f) { m_flRepathTime = 0; SetGoal(g_NavMesh.GetRandomWalkablePoint(me->m_vecOrigin, 300.f), true); m_flLastProgress = now; m_flUnstickUntil = now + 1.5f; }
  }
}

// ------------------------------------------------------------------------------------------------
// combat
bool CCSBot::PressFire(CUserCmd& cmd, C_WeaponCSBase* w) {
  float now = ServerTime();
  if (!w || w->m_iClip1 <= 0 || bot_dont_shoot.GetBool()) return false;
  bool ready = now >= w->m_flNextPrimaryAttack && now >= me->m_flNextAttack;
  if (w->IsRevolver() || w->IsFullAuto() || w->GetWeaponType() == WEAPONTYPE_KNIFE) { cmd.buttons |= IN_ATTACK; if (ready) m_flLastShot = now; return ready; }
  if (!(m_LastCmd.buttons & IN_ATTACK) && ready) { cmd.buttons |= IN_ATTACK; m_flLastShot = now; return true; }   // semi-auto: tap
  return false;
}

void CCSBot::Fight(CUserCmd& cmd, QAngle& view) {
  float now = ServerTime();
  C_WeaponCSBase* w = me->GetActiveCSWeapon();
  Vector aimPoint;
  if (!IsVisible(m_pEnemy, &aimPoint)) return;
  // aim point: head for harder bots, chest otherwise
  const HitboxSet& hb = m_pEnemy->m_net.hitboxes;
  bool headAim = m_iDifficulty >= 2 ? RandomInt(0, 9) < 6 + m_iDifficulty : RandomInt(0, 9) < 3;
  Vector head = (hb[HITBOX_HEAD].a + hb[HITBOX_HEAD].b) * 0.5f, chest = (hb[HITBOX_CHEST].a + hb[HITBOX_CHEST].b) * 0.5f;
  Vector eye = me->EyePosition();
  bool headVis = WorldLOS(eye, head), chestVis = WorldLOS(eye, chest);
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
  k = std::min(1.f, k * (len > 10 ? 1.f : 0.6f + 0.04f * len));   // ease in
  view.x += dp * k; view.y += dy * k;
  if (!w) return;
  float dist = eye.DistTo(target);
  int type = w->GetWeaponType();
  const bool retreating = now < m_flRetreatUntil;
  // out of ammo: pistol (EquipBestWeapon) or back off to reload behind cover
  if (w->IsGun() && w->m_iClip1 == 0) {
    cmd.buttons |= IN_RELOAD;
    if (!retreating && dist > 250.f && w->m_iPrimaryReserveAmmoCount > 0 && FindCover(m_pEnemy->m_net.origin + Vector(0, 0, 64), m_vecCover)) m_flRetreatUntil = now + 3.f;
    return;
  }
  // hurt and outgunned: fall back once per engagement
  if (!retreating && now - m_flEnemyFirstSeen < 0.05f && me->m_iHealth < 35 && dist > 600.f && m_flAggression < 0.6f) {
    C_WeaponCSBase* ew = m_pEnemy->GetActiveCSWeapon();
    if (ew && ew->GetCSWpnData().price > w->GetCSWpnData().price + 1000 && FindCover(m_pEnemy->m_net.origin + Vector(0, 0, 64), m_vecCover)) m_flRetreatUntil = now + 2.5f;
  }
  const float spd = me->m_vecVelocity.Length2D();
  const bool accurate = (me->m_fFlags & FL_ONGROUND) && spd < w->GetMaxSpeed() * 0.34f;
  float tolerance = std::max(0.8f, RAD2DEGF(std::atan2(10.f, dist))) * (1.f + 0.3f * (3 - m_iDifficulty));
  QAngle realAim = view; realAim.x += me->m_aimPunchAngle.x * 2; realAim.y += me->m_aimPunchAngle.y * 2;
  bool onTarget = AngleDistance(realAim, CalcAngle(eye, target)) < tolerance;
  bool canShoot = now >= m_flReactionEnd && !bot_dont_shoot.GetBool();
  // knife: run at him
  if (!w->IsGun()) {
    if (!retreating) { cmd.forwardmove = 450.f; cmd.sidemove = (now > m_flStrafeChange ? 0 : 1) * 200.f * m_iStrafeDir; }
    if (dist < 70.f && canShoot) PressFire(cmd, w);
    return;
  }
  // snipers: stop, scope, shoot, unpeek (awp jiggle) while the bolt cycles
  if (type == WEAPONTYPE_SNIPER_RIFLE) {
    if (w->GetCSWpnData().zoomLevels > 0 && !w->IsZoomed() && now > w->m_flNextSecondaryAttack) cmd.buttons |= IN_ATTACK2;
    if (retreating) return;
    bool cycling = now < w->m_flNextPrimaryAttack && now - m_flLastShot < 1.2f && !w->IsFullAuto();
    if (cycling && m_iDifficulty >= 2) { cmd.forwardmove = 0; cmd.sidemove = 450.f * m_iStrafeDir; return; }
    if (cycling == false && now - m_flLastShot < 1.5f && m_iDifficulty >= 2 && spd > 60.f) m_iStrafeDir = -m_iStrafeDir;   // swing back out
    CounterStrafe(cmd, view);
    if (canShoot && onTarget && accurate && (w->IsZoomed() || w->GetCSWpnData().zoomLevels == 0 || dist < 200.f)) { if (PressFire(cmd, w)) m_iStrafeDir = -m_iStrafeDir; }
    return;
  }
  // close range with SMGs / shotguns / LMGs: run and gun with ADAD
  if (dist < 380.f && (type == WEAPONTYPE_SUBMACHINEGUN || type == WEAPONTYPE_SHOTGUN || type == WEAPONTYPE_MACHINEGUN)) {
    if (!retreating && m_iDifficulty >= 1) {
      if (now > m_flStrafeChange) { m_iStrafeDir = -m_iStrafeDir; m_flStrafeChange = now + RandomFloat(0.2f, 0.45f); }
      cmd.sidemove = 450.f * m_iStrafeDir; cmd.forwardmove = 0;
      if (m_bCrouchSpray && me->m_iShotsFired > 4) cmd.buttons |= IN_DUCK;
    }
    if (canShoot && onTarget) PressFire(cmd, w);
    return;
  }
  // rifles / pistols: strafe -> counter-strafe -> shoot (burst) -> strafe the other way (ADAD peeking)
  if (retreating) { if (canShoot && onTarget && accurate) PressFire(cmd, w); return; }
  if (m_iFightPhase == 0) {
    if (m_iDifficulty < 2 || now >= m_flPhaseEnd) { m_iFightPhase = 1; m_flPhaseEnd = now; }
    else { cmd.forwardmove = 0; cmd.sidemove = 450.f * m_iStrafeDir; }
  }
  if (m_iFightPhase == 1) {
    CounterStrafe(cmd, view);
    if (accurate && now >= m_flPhaseEnd) {
      m_iFightPhase = 2; m_iPhaseShots = 0;
      m_iPhaseShotsMax = type == WEAPONTYPE_PISTOL ? RandomInt(1, 3) : dist < 450.f ? 30 : dist < 1000.f ? RandomInt(3, 7) : RandomInt(1, 3);
    }
  }
  if (m_iFightPhase == 2) {
    CounterStrafe(cmd, view);
    if (m_bCrouchSpray && (type == WEAPONTYPE_RIFLE || type == WEAPONTYPE_MACHINEGUN) && dist > 300.f) cmd.buttons |= IN_DUCK;
    if (dist > 1100.f && type == WEAPONTYPE_RIFLE && m_iDifficulty >= 2) cmd.buttons |= IN_DUCK;
    if (canShoot && onTarget && PressFire(cmd, w)) m_iPhaseShots++;
    if (m_iPhaseShots >= m_iPhaseShotsMax) {
      bool adad = m_iDifficulty >= 2 && (type == WEAPONTYPE_PISTOL || dist > 450.f) && RandomFloat(0, 1) < 0.35f + 0.5f * m_flAggression;
      if (adad) { m_iFightPhase = 0; m_iStrafeDir = -m_iStrafeDir; m_flPhaseEnd = now + RandomFloat(0.12f, 0.3f); }
      else { m_iFightPhase = 1; m_flPhaseEnd = now + (dist > 1000.f ? 0.4f : 0.22f); }   // let the recoil reset
    }
  }
}

// the enemy went out of sight: hold the angle, wide swing him, jiggle (AWP) or pop-flash first
bool CCSBot::EnemyLost(CUserCmd& cmd, QAngle& view) {
  float now = ServerTime();
  Vector lastHead = m_vecEnemyLastPos + Vector(0, 0, 62);
  C_WeaponCSBase* w = me->GetActiveCSWeapon();
  if (m_iPeek == PEEK_NONE) {
    bool weak = me->m_iHealth < 40 || !w || !w->IsGun() || w->m_iClip1 < w->GetCSWpnData().clipSize * 0.3f || w->m_bInReload;
    bool awper = w && w->GetWeaponType() == WEAPONTYPE_SNIPER_RIFLE;
    C_WeaponCSBase* ew = m_pEnemy->GetActiveCSWeapon();
    bool enemyAwp = ew && ew->GetCSWeaponID() == WEAPON_AWP;
    bool defending = me->m_iTeamNum == TEAM_CT ? g_pPlantedC4 == nullptr : g_pPlantedC4 != nullptr;
    float holdChance = (weak ? 0.75f : 0.15f) + (awper ? 0.45f : 0.f) + (defending ? 0.25f : 0.f) - m_flAggression * 0.35f;
    if (m_bSaving) holdChance = 1.f;
    if (m_iDifficulty < 1) holdChance = 0.3f;
    if (RandomFloat(0, 1) < holdChance) { m_iPeek = PEEK_HOLD; m_flPeekUntil = now + RandomFloat(1.5f, 4.f); }
    else if (m_iDifficulty >= 2 && m_vecEnemyLastPos.DistTo(me->m_vecOrigin) > 300.f && StartThrow(WEAPON_FLASHBANG, lastHead + Vector(0, 0, 40), 260.f, true)) { m_iPeek = PEEK_FLASH; m_flPeekUntil = now + 6.f; }
    else if (enemyAwp && m_iDifficulty >= 2) { m_iPeek = PEEK_JIGGLE; m_flPeekPhase = now; m_iJiggle = 0; m_flPeekUntil = now + 7.f; }
    else { m_iPeek = PEEK_SWING; m_flPeekUntil = now + 5.f; }
    if (bot_debug.GetBool()) { static const char* pk[] = { "-", "hold", "swing", "jiggle", "flash" }; Msg("[bot] %s lost %s: %s\n", me->GetPlayerName(), m_pEnemy->GetPlayerName(), pk[m_iPeek]); }
  }
  // pre-aim where he was
  QAngle aim = CalcAngle(me->EyePosition(), lastHead);
  view.x = ApproachAngle(aim.x, view.x, 400.f * gpGlobals->interval_per_tick);
  view.y = ApproachAngle(aim.y, view.y, 600.f * gpGlobals->interval_per_tick);
  switch (m_iPeek) {
  case PEEK_HOLD:
    SetState(BOT_HOLD);
    if (w && w->IsGun() && w->m_iClip1 < w->GetCSWpnData().clipSize * 0.6f && w->m_iPrimaryReserveAmmoCount > 0) cmd.buttons |= IN_RELOAD;
    if (now > m_flPeekUntil - 0.05f && m_flAggression > 0.5f && !m_bSaving) { m_iPeek = PEEK_SWING; m_flPeekUntil = now + 5.f; }
    CounterStrafe(cmd, view);
    return false;
  case PEEK_FLASH:
    if (!m_iThrowPhase) { m_iPeek = PEEK_SWING; m_flPeekUntil = now + 5.f; }
    return false;
  case PEEK_JIGGLE:
    if (now - m_flPeekPhase > 1.3f) { m_iPeek = PEEK_SWING; m_flPeekUntil = now + 5.f; }
    [[fallthrough]];
  default:
    SetState(BOT_HUNT);
    SetGoal(m_vecEnemyLastPos);
    if (me->m_vecOrigin.DistTo(m_vecEnemyLastPos) < 90.f) { m_pEnemy = nullptr; m_iPeek = PEEK_NONE; }
    return true;
  }
}

// ------------------------------------------------------------------------------------------------
// grenades: solve the pitch with the real trajectory simulation, then pull the pin and release
bool CCSBot::StartThrow(int id, const Vector& target, float maxError, bool requireHidden) {
  float now = ServerTime();
  if (m_bHvH || m_iThrowPhase || now < m_flNextThrow || m_iDifficulty < 1 || CSGameRules()->IsFreezePeriod()) return false;
  if (!me->Weapon_OwnsThisType(id)) return false;
  Vector eye = me->EyePosition();
  float yaw = CalcAngle(eye, target).y;
  float bestErr = 1e9f; QAngle best;
  GrenadePath path;
  Vector savedVel = me->m_vecVelocity; me->m_vecVelocity = Vector();   // we stop before releasing
  for (float pitch = -62.f; pitch <= 28.f; pitch += 3.f) {
    QAngle a(pitch, yaw, 0); Vector src, vel;
    GetGrenadeThrowParams(me, a, 1.f, src, vel);
    PredictGrenadePath(id, src, vel, path, me);
    float e = path.detonate.DistTo(target);
    if (e >= bestErr) continue;
    if (requireHidden && WorldLOS(eye, path.detonate)) continue;      // don't flash ourselves
    bestErr = e; best = a;
  }
  me->m_vecVelocity = savedVel;
  m_flNextThrow = now + 1.5f;
  if (bestErr > maxError) return false;
  m_iThrowPhase = 1; m_iThrowNade = id; m_angThrow = best; m_flThrowPhaseTime = now; m_flNextThrow = now + 5.f;
  if (bot_debug.GetBool()) Msg("[bot] %s throws %s at %.0f %.0f %.0f (pitch %.1f, predicted miss %.0f)\n", me->GetPlayerName(), WeaponIdToAlias(id), target.x, target.y, target.z, best.x, bestErr);
  return true;
}

bool CCSBot::RunThrow(CUserCmd& cmd, QAngle& view) {
  if (!m_iThrowPhase) return false;
  float now = ServerTime();
  C_WeaponCSBase* nade = me->Weapon_OwnsThisType(m_iThrowNade);
  C_WeaponCSBase* act = me->GetActiveCSWeapon();
  if (now - m_flThrowPhaseTime > 3.f) { m_iThrowPhase = 0; EquipBestWeapon(); return false; }
  view.x = ApproachAngle(m_angThrow.x, view.x, 450.f * gpGlobals->interval_per_tick);
  view.y = ApproachAngle(m_angThrow.y, view.y, 600.f * gpGlobals->interval_per_tick);
  CounterStrafe(cmd, view);
  if (m_iThrowPhase == 1) {
    if (!nade) { m_iThrowPhase = 0; return false; }
    if (act != nade) me->Weapon_Switch(nade);
    else if (now >= nade->m_flNextPrimaryAttack) { m_iThrowPhase = 2; m_flThrowPhaseTime = now; cmd.buttons |= IN_ATTACK; }
  } else if (m_iThrowPhase == 2) {
    if (act != nade || !nade) { m_iThrowPhase = 0; return false; }
    auto* g = static_cast<CBaseCSGrenade*>(nade);
    bool aimed = std::fabs(AngleDiff(view.x, m_angThrow.x)) < 0.3f && std::fabs(AngleDiff(view.y, m_angThrow.y)) < 0.3f;
    bool still = me->m_vecVelocity.Length2D() < 8.f && (me->m_fFlags & FL_ONGROUND);
    if (g->m_bPinPulled && now >= g->m_flNextPrimaryAttack && aimed && still) { m_iThrowPhase = 3; m_flThrowPhaseTime = now; view = m_angThrow; }
    else cmd.buttons |= IN_ATTACK;
  } else {
    view = m_angThrow;                                     // the projectile spawns 0.1 s after the release with our view angles
    if (now - m_flThrowPhaseTime > 0.3f) { m_iThrowPhase = 0; EquipBestWeapon(); return false; }
  }
  return true;
}

void CCSBot::ThinkUtility() {
  float now = ServerTime();
  if (m_bHvH || m_iDifficulty < 1 || now < m_flNextThrow || m_iThrowPhase || CSGameRules()->IsDeathmatch() || CSGameRules()->IsFreezePeriod()) return;
  BotTeamPlan& plan = GetPlan(me->m_iTeamNum);
  bool ct = me->m_iTeamNum == TEAM_CT;
  // T execute: smoke off the CT side, flash the site
  if (!ct && (plan.executing || plan.rush) && !g_pPlantedC4) {
    if (const MapZone* z = SiteZone(plan.site)) {
      Vector c = z->Center(); float d = me->m_vecOrigin.DistTo(c);
      if (d < 1400.f && d > 450.f) {
        Vector ctDir = s_map.ctSpawn - c; ctDir.z = 0;
        Vector smoke = c + ctDir.Normalized() * 380.f;
        if (plan.smokes < 2 && me->Weapon_OwnsThisType(WEAPON_SMOKEGRENADE) && StartThrow(WEAPON_SMOKEGRENADE, smoke, 280.f, false)) { plan.smokes++; return; }
        if (plan.flashes < 3 && me->Weapon_OwnsThisType(WEAPON_FLASHBANG) && StartThrow(WEAPON_FLASHBANG, c + Vector(0, 0, 100), 320.f, true)) { plan.flashes++; return; }
      }
    }
  }
  // enemies the team knows about nearby but we can't see: HE / flash them, CTs smoke the entrance
  Vector ip; int cnt = 0;
  if (RecentIntel(plan, 2.5f, me->m_vecOrigin, 1300.f, &ip, &cnt) && ip.DistTo(me->m_vecOrigin) > 380.f && !WorldLOS(me->EyePosition(), ip + Vector(0, 0, 50))) {
    if (me->Weapon_OwnsThisType(WEAPON_HEGRENADE) && StartThrow(WEAPON_HEGRENADE, ip, 170.f, false)) return;
    if (cnt >= 2 && me->Weapon_OwnsThisType(WEAPON_FLASHBANG) && StartThrow(WEAPON_FLASHBANG, ip + Vector(0, 0, 90), 260.f, true)) return;
    if (ct && !g_pPlantedC4 && cnt >= 2 && s_map.ok[m_iTargetSite] && plan.smokes < 2 && me->Weapon_OwnsThisType(WEAPON_SMOKEGRENADE) &&
        me->m_vecOrigin.DistTo(s_map.entrance[m_iTargetSite]) < 1300.f && StartThrow(WEAPON_SMOKEGRENADE, s_map.entrance[m_iTargetSite], 260.f, false)) { plan.smokes++; return; }
  }
}

// ------------------------------------------------------------------------------------------------
// HvH bot: ragebot + anti-aim
void CCSBot::HvHAim(CUserCmd& cmd, QAngle& view) {
  C_WeaponCSBase* w = me->GetActiveCSWeapon();
  if (!w || !w->IsGun() || !m_pEnemy || bot_dont_shoot.GetBool()) return;
  float now = ServerTime(), wt = TICKS_TO_TIME(me->m_nTickBase);   // weapons run on our tickbase (doubletap shifts it)
  if (now < m_flReactionEnd) return;
  CCSPlayer* t = m_pEnemy;
  if (t->m_LagRecords.empty()) return;
  const CLagRecord& rec = t->m_LagRecords.front();
  // resolver: brute force the desync side by missed shots (we don't know the server's real feet yaw)
  static const float sides[4] = { 1.f, -1.f, 0.5f, 0.f };
  float guess = AngleNormalize(rec.eyeAngles.y + sides[t->m_iMissedShots % 4] * rec.maxDesync);
  PlayerPoseInput in; in.origin = rec.origin; in.footYaw = guess; in.eyeAngles = rec.eyeAngles; in.duckAmount = rec.duckAmount;
  in.moveCycle = rec.moveCycle; in.moveSpeed = rec.moveSpeed; in.moveYaw = rec.moveYaw; in.onGround = rec.onGround; in.weaponId = rec.weaponId;
  PlayerPoseOutput out; BuildPlayerPose(t, in, out, true);
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
  if (w->GetCSWpnData().zoomLevels > 0 && !w->IsZoomed() && wt > w->m_flNextSecondaryAttack && w->GetWeaponType() == WEAPONTYPE_SNIPER_RIFLE) { cmd.buttons |= IN_ATTACK2; return; }
  if (wt < w->m_flNextPrimaryAttack || w->m_iClip1 <= 0 || wt < me->m_flNextAttack) { cmd.forwardmove = cmd.sidemove = 0; return; }
  if (w->IsRevolver() && !w->RevolverCocked(wt)) { cmd.forwardmove = cmd.sidemove = 0; return; }   // auto revolver still pulling
  cmd.forwardmove = 0; cmd.sidemove = 0;
  // hitchance: inaccuracy small enough for the distance
  float dist = eye.DistTo(best);
  float inacc = w->GetInaccuracy() + w->GetSpread();
  if (inacc * dist > 9.f && me->m_vecVelocity.Length2D() > 5.f) return;
  QAngle aim = CalcAngle(eye, best);
  aim.x -= me->m_aimPunchAngle.x * 2.f; aim.y -= me->m_aimPunchAngle.y * 2.f;
  view = aim;                 // silent: only this command carries the real aim
  cmd.buttons |= IN_ATTACK; cmd.buttons &= ~IN_ATTACK2;
  m_flLastHvHShot = now;
  cmd.tick_count = rec.tick; // shoot at the record we resolved
  t->m_iMissedShots++;       // reset on hit by the event listener (approximation)
}

// auto revolver, same scheme as the cheat (see cheat/ragebot.cpp AutoRevolver): hold attack until the hammer is
// back, keep it there with attack2, release for a tick when the attack2 window runs out
bool CCSBot::HvHRevolver(CUserCmd& cmd) {
  C_WeaponCSBase* w = me->GetActiveCSWeapon();
  if (!w || !w->IsRevolver()) return false;
  const float now = TICKS_TO_TIME(me->m_nTickBase), tick = gpGlobals->interval_per_tick;
  const bool usable = !w->IsReloading() && w->m_iClip1 > 0 && !CSGameRules()->IsFreezePeriod() && !me->m_bIsDefusing && !me->m_bIsPlanting && now >= me->m_flNextAttack;
  cmd.buttons &= ~(IN_ATTACK | IN_ATTACK2);
  if (!usable || now < w->m_flNextPrimaryAttack) return false;
  if (!w->m_bRevolverHaulback || !(w->m_flPostponeFireReadyTime < now)) { cmd.buttons |= IN_ATTACK; return false; }
  if (now + tick * 0.5f < w->m_flNextSecondaryAttack) cmd.buttons |= IN_ATTACK2;
  return true;
}

void CCSBot::PickHvHProfile() {
  // cycle through the profiles so every team gets a mix (not everyone on max fakelag)
  static int s_next = 0;
  m_iHvHProfile = (s_next++ + RandomInt(0, 1) * 4) % 4;
  switch (m_iHvHProfile) {
    case 0: m_iFakelag = RandomInt(12, 14); break;          // classic max fakelag
    case 1: m_iFakelag = 1; break;                          // doubletap (fakelag off, it eats the shift)
    case 2: m_iFakelag = 2; break;                          // hide shots
    default: m_iFakelag = RandomInt(5, 9); break;           // mixed, lower fakelag
  }
  m_iAAMode = RandomInt(0, 3);
  m_flJitter = m_iAAMode == 1 ? 0.f : RandomFloat(8.f, 35.f);
  m_flDesync = RandomFloat(45.f, 120.f);
  m_iDesyncSide = RandomInt(0, 1) ? 1 : -1;
}

void CCSBot::ApplyHvHAntiAim(CUserCmd& cmd, const QAngle& view, bool shooting) {
  float now = ServerTime();
  if (shooting) {
    // hide shots: the shot command is choked, others only see the anti-aim angles
    cmd.bSendPacket = m_iHvHProfile != 2; m_nChoke = cmd.bSendPacket ? 0 : m_nChoke + 1; return;
  }
  // fakelag - sends every N ticks (profile dependent)
  int limit = me->m_vecVelocity.Length2D() > 10.f ? m_iFakelag : std::min(m_iFakelag, 2);
  m_nChoke++;
  cmd.bSendPacket = m_nChoke >= limit;
  if (cmd.bSendPacket) m_nChoke = 0;
  // yaw: at target + 180 with jitter, pitch down
  float base = view.y;
  if (m_pEnemy) base = CalcAngle(me->m_vecOrigin, m_pEnemy->m_vecOrigin).y;
  else { float best = 1e9f; for (CCSPlayer* o : UTIL_GetPlayers(true)) if (me->IsEnemy(o)) { float d = o->m_vecOrigin.DistTo(me->m_vecOrigin); if (d < best) { best = d; base = CalcAngle(me->m_vecOrigin, o->m_vecOrigin).y; } } }
  m_bJitter = !m_bJitter;
  float yaw = base + 180.f;
  switch (m_iAAMode) {
    case 0: yaw += m_bJitter ? m_flJitter : -m_flJitter; break;
    case 1: break;
    case 2: { static const float w3[3] = { -1.f, 0.f, 1.f }; if (cmd.bSendPacket) m_iAAStep = (m_iAAStep + 1) % 3; yaw += w3[m_iAAStep] * m_flJitter; break; }
    default: yaw += 90.f * m_iDesyncSide + (m_bJitter ? m_flJitter * 0.3f : 0.f); break;
  }
  if (now > m_flNextSideSwitch) { m_flNextSideSwitch = now + RandomFloat(1.5f, 4.f); if (RandomInt(0, 2) == 0) m_iDesyncSide = -m_iDesyncSide; }
  // desync: choked commands carry the opposite yaw so the server's feet yaw ends up offset
  if (!cmd.bSendPacket) yaw += m_flDesync * m_iDesyncSide;
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
  CCSGameRules* gr = CSGameRules();
  const bool frozen = gr->IsFreezePeriod();
  if (me->m_fFlags & FL_ONGROUND) m_bBhopping = false;
  UpdatePerception();
  if (frozen || !gr->IsBuyTimeElapsed()) Buy();
  QAngle view = m_angView;
  bool doMove = true;
  const bool throwing = RunThrow(cmd, view);
  if (throwing) {
    doMove = false;
    if (m_pEnemy && m_bEnemyVisible && m_iThrowPhase == 1) { m_iThrowPhase = 0; EquipBestWeapon(); }   // surprised while drawing the nade
  } else if (m_pEnemy && !bot_zombie.GetBool()) {
    SetState(BOT_ATTACK);
    EquipBestWeapon();
    if (m_bHvH) {
      view = CalcAngle(me->EyePosition(), m_pEnemy->m_net.origin + Vector(0, 0, 50));
    } else if (m_bEnemyVisible) {
      Fight(cmd, view);
      doMove = now < m_flRetreatUntil;
    } else doMove = EnemyLost(cmd, view);
  } else {
    if (!frozen) EquipBestWeapon();
    ThinkSave();
    ThinkGoals();
    // where to look: the angle we hold, where the team saw enemies (pre-aim), sounds, or along the path
    BotTeamPlan& plan = GetPlan(me->m_iTeamNum);
    Vector intel; bool haveIntel = !m_bHvH && m_iDifficulty >= 1 && RecentIntel(plan, 4.f, me->m_vecOrigin, 1500.f, &intel, nullptr);
    bool noise = m_vecNoise.LengthSqr() > 0 && now - m_flNoiseTime < 3.f;
    if (m_iState == BOT_HOLD) {
      Vector look = m_vecHoldLook;
      if (haveIntel) look = intel + Vector(0, 0, 62); else if (noise) look = m_vecNoise + Vector(0, 0, 56);
      if (now > m_flLookAroundTime) { m_flLookAroundTime = now + RandomFloat(1.f, 3.f); m_flLookYaw = (haveIntel || noise) ? RandomFloat(-6, 6) : RandomFloat(-30, 30); }
      QAngle want = CalcAngle(me->EyePosition(), look); want.y += m_flLookYaw; want.x = Clamp(want.x, -15.f, 20.f);
      view.x = ApproachAngle(want.x, view.x, 120.f * gpGlobals->interval_per_tick);
      view.y = ApproachAngle(want.y, view.y, 220.f * gpGlobals->interval_per_tick);
    } else if (!m_Path.empty() && m_iPathIndex < m_Path.size() && !m_bBhopping) {
      Vector look = m_Path[std::min(m_Path.size() - 1, m_iPathIndex + 1)] + Vector(0, 0, 60);
      bool preaim = false;
      if (haveIntel && intel.DistTo(me->m_vecOrigin) > 150.f) { look = intel + Vector(0, 0, 62); preaim = true; }
      else if (noise) { look = m_vecNoise + Vector(0, 0, 56); preaim = true; }
      QAngle want = CalcAngle(me->EyePosition(), look); want.x = preaim ? Clamp(want.x, -20.f, 20.f) : Clamp(want.x * 0.3f, -10.f, 10.f);
      view.x = ApproachAngle(want.x, view.x, 90.f * gpGlobals->interval_per_tick);
      view.y = ApproachAngle(want.y, view.y, (preaim ? 360.f : 260.f) * gpGlobals->interval_per_tick);
    }
    // reload when safe
    C_WeaponCSBase* w = me->GetActiveCSWeapon();
    if (w && w->IsGun() && !w->m_bInReload && w->m_iClip1 < w->GetCSWpnData().clipSize * 0.4f && w->m_iPrimaryReserveAmmoCount > 0 && now - m_flEnemyLastSeen > 2.f) cmd.buttons |= IN_RELOAD;
    if (!frozen) ThinkUtility();
  }
  // falling back to cover (reload / low hp)
  if (now < m_flRetreatUntil && !throwing && !m_bHvH) {
    C_WeaponCSBase* w = me->GetActiveCSWeapon();
    if (w && w->IsGun() && w->m_iClip1 < w->GetCSWpnData().clipSize && w->m_iPrimaryReserveAmmoCount > 0) cmd.buttons |= IN_RELOAD;
    if (w && w->IsGun() && !w->m_bInReload && w->m_iClip1 >= w->GetCSWpnData().clipSize && now - m_flEnemyLastSeen > 0.5f) m_flRetreatUntil = 0;
    else {
      if (m_iState != BOT_ATTACK) SetState(BOT_RETREAT);
      SetGoal(m_vecCover); doMove = true;
      if (me->m_vecOrigin.DistTo(m_vecCover) < 40.f) doMove = false;
    }
  }
  // objectives
  if (m_iState == BOT_PLANT) {
    if (C_WeaponCSBase* c4 = me->Weapon_OwnsThisType(WEAPON_C4)) { if (me->GetActiveCSWeapon() != c4) me->Weapon_Switch(c4); cmd.buttons |= IN_ATTACK; view.x = 60; }
  } else if (m_iState == BOT_DEFUSE && g_pPlantedC4) {
    view = CalcAngle(me->EyePosition(), g_pPlantedC4->m_vecOrigin);
    cmd.buttons |= IN_USE | IN_DUCK;
  }
  // shift-walk when enemies may be close (no footsteps), run when it's urgent
  {
    BotTeamPlan& plan = GetPlan(me->m_iTeamNum);
    bool threat = RecentIntel(plan, 6.f, me->m_vecOrigin, 1100.f, nullptr, nullptr) || (m_vecNoise.LengthSqr() > 0 && now - m_flNoiseTime < 5.f && m_vecNoise.DistTo(me->m_vecOrigin) < 1000.f);
    bool urgent = m_bRushing || now < m_flRetreatUntil || m_iPeek == PEEK_SWING || m_iPeek == PEEK_JIGGLE || m_bSaving || gr->IsDeathmatch();
    m_bWalk = !m_bHvH && m_iDifficulty >= 1 && !urgent && !m_bEnemyVisible && (threat || m_iRole == BOTROLE_LURK);
  }
  if (!bot_freeze.GetBool() && doMove && !(m_iState == BOT_ATTACK && m_bHvH)) {
    MoveTowardsGoal(cmd, view);
    if (m_iPeek == PEEK_JIGGLE && m_pEnemy && !m_bEnemyVisible) {
      // jiggle: in and out of the corner to bait the AWP shot, sometimes a jump peek
      if ((int)((now - m_flPeekPhase) / 0.2f) % 2 == 1) { cmd.forwardmove = -cmd.forwardmove; cmd.sidemove = -cmd.sidemove; }
      if (m_iJiggle == 0 && (me->m_fFlags & FL_ONGROUND) && now - m_flPeekPhase > 0.5f) { m_iJiggle = 1; if (RandomInt(0, 1)) cmd.buttons |= IN_JUMP; }
    }
    if (m_bWalk && (me->m_fFlags & FL_ONGROUND) && !m_bBhopping) cmd.buttons |= IN_SPEED;
  } else if (m_bHvH && m_iState == BOT_ATTACK && !bot_freeze.GetBool()) {
    // hvh bots peek: keep strafing
    if (now > m_flStrafeChange) { m_iStrafeDir = -m_iStrafeDir; m_flStrafeChange = now + RandomFloat(0.4f, 1.2f); }
    cmd.sidemove = 450.f * m_iStrafeDir;
  } else if (bot_freeze.GetBool()) { cmd.forwardmove = cmd.sidemove = 0; }
  if (bot_crouch.GetBool()) cmd.buttons |= IN_DUCK;
  view.x = Clamp(view.x, -89.f, 89.f); view.y = AngleNormalize(view.y); view.z = 0;
  m_angView = view;
  cmd.viewangles = view;
  if (m_bHvH) {
    C_WeaponCSBase* aw = me->GetActiveCSWeapon();
    const bool isRev = aw && aw->IsRevolver();
    HvHRevolver(cmd);   // the hammer is pulled / kept back even without a target
    const float shotBefore = m_flLastHvHShot;
    if (m_pEnemy) { QAngle aim = view; HvHAim(cmd, aim); if (m_flLastHvHShot != shotBefore) cmd.viewangles = aim; }
    // a revolver pull is not a shot (anti-aim stays on); other attacks (knife etc.) keep the old behaviour
    bool shooting = isRev ? m_flLastHvHShot != shotBefore : (cmd.buttons & IN_ATTACK) != 0;
    const bool realShot = shooting && aw && aw->IsGun();
    if (!shooting) ApplyHvHAntiAim(cmd, view, false);
    else { QAngle keep = cmd.viewangles; ApplyHvHAntiAim(cmd, view, true); cmd.viewangles = keep; }
    // doubletap: recharge (hang ~15 ticks) when calm, dump the charge on a shot -> instant second shot
    if (m_iHvHProfile == 1 && !bot_freeze.GetBool()) {
      static ConVar* maxdt = FindConVar("sv_maxdtshift"); static ConVar* maxproc = FindConVar("sv_maxusrcmdprocessticks");
      int maxShift = std::min(maxdt ? maxdt->GetInt() : 15, maxproc ? maxproc->GetInt() - 1 : 15);
      if (realShot && me->m_nTickbaseShift > 0) cmd.tickbaseShift = me->m_nTickbaseShift;
      else if (!shooting && !(cmd.buttons & (IN_ATTACK | IN_ATTACK2)) && me->m_nTickbaseShift < maxShift && now - m_flLastHvHShot > 0.35f && !CSGameRules()->IsFreezePeriod())
        cmd.tickbaseShift = -1;
    }
  }
  if (*bot_debug_cmd.GetString() && me->m_szPlayerName == bot_debug_cmd.GetString())
    Msg("[cmd] t%d %s doMove %d fwd %.0f side %.0f btn %x pos %.0f %.0f %.0f vel %.0f path %d/%d tgt %.0f %.0f %.0f retreat %.1f throw %d walk %d hop %d fl %x vz %.0f\n", gpGlobals->tickcount, BotStateName(m_iState), doMove, cmd.forwardmove, cmd.sidemove, cmd.buttons,
        me->m_vecOrigin.x, me->m_vecOrigin.y, me->m_vecOrigin.z, me->m_vecVelocity.Length2D(), (int)m_iPathIndex, (int)m_Path.size(),
        m_iPathIndex < m_Path.size() ? m_Path[m_iPathIndex].x : 0.f, m_iPathIndex < m_Path.size() ? m_Path[m_iPathIndex].y : 0.f, m_iPathIndex < m_Path.size() ? m_Path[m_iPathIndex].z : 0.f,
        m_flRetreatUntil - now, m_iThrowPhase, m_bWalk, m_bBhopping, me->m_fFlags, me->m_vecVelocity.z);
  m_LastCmd = cmd;
}

static ConCommand cmd_bot_navtest("bot_navtest", [](const CCommand&) {
  UpdateMapCache();
  for (int s = 0; s < 2; s++) {
    const MapZone* z = SiteZone(s); if (!z) continue;
    for (int t : { TEAM_TERRORIST, TEAM_CT }) {
      std::vector<Vector> path; Vector from = t == TEAM_CT ? s_map.ctSpawn : s_map.tSpawn;
      bool ok = g_NavMesh.ComputePath(from, z->Center(), path, nullptr, 0.f);
      float len = 0; for (size_t i = 1; i < path.size(); i++) len += path[i].DistTo(path[i - 1]);
      Msg("site %c from %s spawn: ok %d nodes %d length %.0f straight %.0f\n", 'A' + s, t == TEAM_CT ? "CT" : "T", ok, (int)path.size(), len, from.DistTo(z->Center()));
      if (len > 3.f * from.DistTo(z->Center())) { Msg("  from %.0f %.0f %.0f:", from.x, from.y, from.z); for (size_t i = 0; i < path.size(); i += 8) Msg(" (%.0f %.0f %.0f)", path[i].x, path[i].y, path[i].z); Msg("\n"); }
    }
  }
  Msg("cell %.0f areas %d\n", g_NavMesh.CellSize(), g_NavMesh.NumAreas());
}, "Print bot path lengths spawn -> bomb sites");
static ConCommand cmd_bot_navline("bot_navline", [](const CCommand& a) {
  // floor profile + nav nodes along a straight line: bot_navline x0 y0 x1 y1
  if (a.ArgC() < 5) return;
  Vector p0(atof(a.Arg(1)), atof(a.Arg(2)), 0), p1(atof(a.Arg(3)), atof(a.Arg(4)), 0);
  int n = (int)(p0.DistTo(p1) / 32.f);
  for (int i = 0; i <= n; i++) {
    Vector p = p0 + (p1 - p0) * (i / (float)std::max(1, n));
    trace_t tr; float zt = a.ArgC() > 5 ? atof(a.Arg(5)) : 200.f; g_EngineTrace.TraceHull(Vector(p.x, p.y, zt), Vector(p.x, p.y, -1000), Vector(-16, -16, 0), Vector(16, 16, 72), MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
    int ai = g_NavMesh.GetAreaIndexRaw(tr.endpos); const CNavArea* ar = g_NavMesh.GetArea(ai);
    std::string links; if (ar) for (int k = 0; k < 8; k++) links += ar->link[k] >= 0 ? (char)('0' + ar->linkFlags[k]) : '.';
    Msg("%4d  %6.0f %6.0f  floor %6.0f n %.2f  node z %6.0f reach %d links %s\n", i, p.x, p.y, tr.endpos.z, tr.plane.normal.z, ar ? ar->z : -9999.f, ar ? ar->reachable : 0, links.c_str());
    if (a.ArgC() > 6) {
      float x0, y0, cell; int cw, ch; g_NavMesh.GetGrid(x0, y0, cell, cw, ch);
      int cx = (int)std::floor((p.x - x0) / cell), cy = (int)std::floor((p.y - y0) / cell);
      for (int ni : g_NavMesh.Column(cy * cw + cx)) {
        const CNavArea* A = g_NavMesh.GetArea(ni); std::string l;
        for (int k = 0; k < 8; k++) { const CNavArea* B = g_NavMesh.GetArea(A->link[k]); l += B ? std::to_string((int)B->z) + "/" + std::to_string(A->linkFlags[k]) + " " : "- "; }
        Msg("        node z %.0f reach %d -> %s\n", A->z, A->reachable, l.c_str());
      }
    }
  }
}, "debug: floor profile along a line");
