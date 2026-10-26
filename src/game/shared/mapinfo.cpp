// mapinfo.cpp - ".map" text format parser and brush compiler
//
// A map is a grid of 64 unit cells. Row 0 is the north edge (+Y), column 0 the west edge (-X).
// Default legend (can be overridden / extended in the file):
//   # % & @   walls (materials set by 'wall' lines)
//   . , ;     floors at height 0 (materials set by 'floor' lines)
//   0-9       floor at digit*32 units (default floor material)
//   > < ^ v   ramps rising east / west / north / south (heights taken from the cells around)
//   c C       crate 64 / crate stack 128        b  barrel
//   w W       low wall (40, crouch cover) / mid wall (96)
//   t L       covered floor (tunnel, ceiling 128 above floor) / same with a lamp
//   T K       terrorist / counter-terrorist spawn     A B   bombsite floor
#include "mapinfo.hpp"
#include "../../engine/worldrender.hpp"
#include "../../engine/enginetrace.hpp"
#include "../../engine/materialsystem.hpp"
#include "../../engine/convar.hpp"
#include "../../engine/bsp.hpp"
#include "../../engine/hlfs.hpp"
#include "../../engine/srcfs.hpp"
#include "../../engine/srcbsp.hpp"
#include "../../engine/srcmaterial.hpp"
#include "../../engine/srcmdl.hpp"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <map>
#include <cstring>
#include <filesystem>

namespace {
enum CellType { CT_VOID = 0, CT_FLOOR, CT_WALL, CT_RAMP, CT_OBJ };
enum ObjType { OBJ_NONE = 0, OBJ_CRATE, OBJ_CRATE2, OBJ_BARREL, OBJ_LOWWALL, OBJ_MIDWALL, OBJ_CRATE_SMALL };
enum Marker { MK_NONE = 0, MK_SPAWN_T, MK_SPAWN_CT, MK_SITE_A, MK_SITE_B };
struct LegendEntry {
  int type = CT_FLOOR; float height = 0; bool hasHeight = true; int mat = MAT_SAND_FLOOR; float top = -1;
  int obj = OBJ_NONE; int marker = MK_NONE; float ceil = -1; bool lamp = false; int rampDir = -1;
};
struct Cell {
  int type = CT_VOID; float floor = 0; bool floorKnown = false; int floorMat = MAT_SAND_FLOOR; int wallMat = MAT_SANDSTONE_WALL; float wallTop = -1;
  float ceil = -1; int obj = OBJ_NONE; int objMat = MAT_CRATE; int marker = MK_NONE; bool lamp = false; int rampDir = -1; float rampLo = 0, rampHi = 0;
};
std::string trim(const std::string& s) { size_t a = s.find_first_not_of(" \t\r"), b = s.find_last_not_of(" \t\r"); return a == std::string::npos ? "" : s.substr(a, b - a + 1); }
std::vector<std::string> tokens(const std::string& line) {
  std::vector<std::string> out; std::string cur; bool q = false;
  for (char c : line) {
    if (c == '"') { q = !q; continue; }
    if (!q && (c == ' ' || c == '\t')) { if (!cur.empty()) { out.push_back(cur); cur.clear(); } continue; }
    cur += c;
  }
  if (!cur.empty()) out.push_back(cur);
  return out;
}
float F(const std::vector<std::string>& t, size_t i, float d = 0) { return i < t.size() ? (float)atof(t[i].c_str()) : d; }
} // namespace

Vector CMapInfo::CellCenter(int cx, int cy) const { return { gridOrigin.x + (cx + 0.5f) * cell, gridOrigin.y - (cy + 0.5f) * cell, 0 }; }

const char* CMapInfo::GetPlaceName(const Vector& p) const {
  for (auto& pl : places) if (p.x >= pl.mins.x && p.x <= pl.maxs.x && p.y >= pl.mins.y && p.y <= pl.maxs.y) return pl.name.c_str();
  return "";
}

bool CMapInfo::Load(const std::string& path) {
  // the renderer holds pointers into the model/material caches that are about to be freed -
  // drop it first, so a failed load can never leave it drawing a half-destroyed world
  g_WorldRender.Unload();
  if (path.rfind("css:", 0) == 0) return LoadSrc(path.substr(4));
  g_SrcBsp.Unload(); SrcMdl_Clear(); SrcMat_Clear();
  if (path.size() > 4 && hlfs::Lower(path.substr(path.size() - 4)) == ".bsp") return LoadBsp(path);
  g_Bsp.Unload();
  std::ifstream f(path);
  if (!f) { Warning("Map %s not found\n", path.c_str()); return false; }
  *this = CMapInfo();
  file = path;
  std::map<char, LegendEntry> legend;
  // defaults
  auto floorE = [](int mat, float h) { LegendEntry e; e.type = CT_FLOOR; e.mat = mat; e.height = h; return e; };
  auto wallE = [](int mat) { LegendEntry e; e.type = CT_WALL; e.mat = mat; return e; };
  legend['.'] = floorE(MAT_SAND_FLOOR, 0); legend[','] = floorE(MAT_SAND_FLOOR2, 0); legend[';'] = floorE(MAT_TILE_FLOOR, 0);
  for (char d = '0'; d <= '9'; d++) legend[d] = floorE(MAT_SAND_FLOOR, (d - '0') * 32.f);
  legend['#'] = wallE(MAT_SANDSTONE_WALL); legend['%'] = wallE(MAT_SANDSTONE_BLOCKS); legend['&'] = wallE(MAT_PLASTER_WHITE); legend['@'] = wallE(MAT_STONE_DARK);
  auto objE = [](int obj, int mat) { LegendEntry e; e.type = CT_OBJ; e.obj = obj; e.mat = mat; e.hasHeight = false; return e; };
  legend['c'] = objE(OBJ_CRATE, MAT_CRATE); legend['C'] = objE(OBJ_CRATE2, MAT_CRATE); legend['b'] = objE(OBJ_BARREL, MAT_BARREL);
  legend['k'] = objE(OBJ_CRATE_SMALL, MAT_CRATE_DARK);
  legend['w'] = objE(OBJ_LOWWALL, MAT_SANDSTONE_TRIM); legend['W'] = objE(OBJ_MIDWALL, MAT_SANDSTONE_BLOCKS);
  { LegendEntry e; e.type = CT_FLOOR; e.hasHeight = false; e.ceil = 128; e.mat = -1; legend['t'] = e; e.lamp = true; legend['L'] = e; }
  { LegendEntry e; e.type = CT_FLOOR; e.hasHeight = false; e.mat = -1; e.marker = MK_SPAWN_T; legend['T'] = e; e.marker = MK_SPAWN_CT; legend['K'] = e;
    e.marker = MK_SITE_A; legend['A'] = e; e.marker = MK_SITE_B; legend['B'] = e; }
  { LegendEntry e; e.type = CT_RAMP; e.hasHeight = false; e.mat = MAT_SAND_FLOOR; e.rampDir = 0; legend['>'] = e; e.rampDir = 1; legend['<'] = e; e.rampDir = 2; legend['^'] = e; e.rampDir = 3; legend['v'] = e; }

  std::vector<std::string> rows;
  std::string line; bool inGrid = false;
  struct PendingLight { float cx, cy, h, r, g, b, rad; }; std::vector<PendingLight> plights;
  struct PendingPlace { std::string n; float x0, y0, x1, y1; }; std::vector<PendingPlace> pplaces;
  struct PendingSpawnYaw { int team; float yaw; }; std::vector<PendingSpawnYaw> spawnYaw;
  int defaultFloorMat = MAT_SAND_FLOOR;
  while (std::getline(f, line)) {
    if (!line.empty() && line.back() == '\r') line.pop_back();
    if (inGrid) { if (trim(line) == "end") { inGrid = false; continue; } rows.push_back(line); continue; }
    std::string tl = trim(line);
    if (tl.empty() || tl[0] == '/' || tl[0] == '!') continue;
    auto t = tokens(tl);
    const std::string& k = t[0];
    if (k == "grid") { inGrid = true; continue; }
    if (k == "name" && t.size() > 1) name = t[1];
    else if (k == "title" && t.size() > 1) title = t[1];
    else if (k == "desc" && t.size() > 1) desc = t[1];
    else if (k == "mode" && t.size() > 1) mode = t[1];
    else if (k == "cell") cell = F(t, 1, 64);
    else if (k == "wallheight") wallHeight = F(t, 1, 320);
    else if (k == "sky") { skyTop = { F(t, 1), F(t, 2), F(t, 3) }; skyHorizon = { F(t, 4), F(t, 5), F(t, 6) }; }
    else if (k == "sun") { sunDir = Vector(F(t, 1), F(t, 2), F(t, 3)).Normalized(); sunColor = { F(t, 4, 1), F(t, 5, 1), F(t, 6, 1) }; }
    else if (k == "ambient") ambient = { F(t, 1), F(t, 2), F(t, 3) };
    else if (k == "fog") fogColor = { F(t, 1), F(t, 2), F(t, 3) };
    else if (k == "snow") snow = true;
    else if (k == "floor" && t.size() > 2) { LegendEntry e = floorE(MaterialFromName(t[2]), F(t, 3, 0)); legend[t[1][0]] = e; if (t[1][0] == '.') { defaultFloorMat = e.mat; for (char d = '0'; d <= '9'; d++) legend[d].mat = e.mat; } }
    else if (k == "wall" && t.size() > 2) { LegendEntry e = wallE(MaterialFromName(t[2])); if (t.size() > 3) e.top = F(t, 3); legend[t[1][0]] = e; }
    else if (k == "obj" && t.size() > 3) {
      int o = t[2] == "crate" ? OBJ_CRATE : t[2] == "crate2" ? OBJ_CRATE2 : t[2] == "barrel" ? OBJ_BARREL : t[2] == "lowwall" ? OBJ_LOWWALL : t[2] == "midwall" ? OBJ_MIDWALL : OBJ_CRATE_SMALL;
      legend[t[1][0]] = objE(o, MaterialFromName(t[3]));
    }
    else if (k == "covered" && t.size() > 3) { LegendEntry e; e.type = CT_FLOOR; e.mat = MaterialFromName(t[2]); e.height = F(t, 3); e.hasHeight = t[3] != "auto"; e.ceil = F(t, 4, 128); e.lamp = t.size() > 5 && t[5] == "lamp"; legend[t[1][0]] = e; }
    else if (k == "rampmat" && t.size() > 1) { int m = MaterialFromName(t[1]); for (char c : { '>', '<', '^', 'v' }) legend[c].mat = m; }
    else if (k == "light") plights.push_back({ F(t, 1), F(t, 2), F(t, 3, 96), F(t, 4, 1), F(t, 5, 0.85f), F(t, 6, 0.6f), F(t, 7, 400) });
    else if (k == "place" && t.size() > 5) pplaces.push_back({ t[1], F(t, 2), F(t, 3), F(t, 4), F(t, 5) });
    else if (k == "spawnyaw" && t.size() > 2) spawnYaw.push_back({ t[1] == "T" ? 2 : 3, F(t, 2) });
  }
  if (rows.empty()) { Warning("Map %s has no grid\n", path.c_str()); return false; }
  if (name.empty()) name = std::filesystem::path(path).stem().string();
  if (title.empty()) title = name;

  gh = (int)rows.size(); gw = 0; for (auto& r : rows) gw = std::max(gw, (int)r.size());
  gridOrigin = { -gw * cell * 0.5f, gh * cell * 0.5f, 0 };
  std::vector<Cell> cells(gw * gh);
  auto C = [&](int x, int y) -> Cell& { return cells[y * gw + x]; };
  auto inb = [&](int x, int y) { return x >= 0 && y >= 0 && x < gw && y < gh; };
  for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
    char ch = x < (int)rows[y].size() ? rows[y][x] : ' ';
    Cell& c = C(x, y);
    auto it = legend.find(ch);
    if (ch == ' ' || it == legend.end()) { c.type = CT_VOID; continue; }
    const LegendEntry& e = it->second;
    c.type = e.type;
    if (e.type == CT_WALL) { c.wallMat = e.mat; c.wallTop = e.top; continue; }
    if (e.hasHeight) { c.floor = e.height; c.floorKnown = true; }
    if (e.mat >= 0) { if (e.type == CT_OBJ) c.objMat = e.mat; else c.floorMat = e.mat; }
    c.obj = e.obj; c.marker = e.marker; c.ceil = e.ceil; c.lamp = e.lamp; c.rampDir = e.rampDir;
    if (e.type == CT_OBJ || (e.type == CT_FLOOR && e.mat < 0)) c.floorMat = -1;
  }
  // resolve unknown floor heights/materials of objects/markers from neighbours (flood)
  for (int pass = 0; pass < 64; pass++) {
    bool changed = false;
    for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
      Cell& c = C(x, y);
      if (c.type == CT_WALL || c.type == CT_VOID || c.type == CT_RAMP) continue;
      if (c.floorKnown && c.floorMat >= 0) continue;
      static const int dx[4] = { -1, 1, 0, 0 }, dy[4] = { 0, 0, -1, 1 };
      for (int d = 0; d < 4; d++) {
        int nx = x + dx[d], ny = y + dy[d]; if (!inb(nx, ny)) continue;
        Cell& n = C(nx, ny);
        if (n.type == CT_WALL || n.type == CT_VOID || n.type == CT_RAMP || !n.floorKnown) continue;
        if (!c.floorKnown) { c.floor = n.floor; c.floorKnown = true; changed = true; }
        if (c.floorMat < 0 && n.floorMat >= 0) { c.floorMat = n.floorMat; changed = true; }
        if (c.floorKnown && c.floorMat >= 0) break;
      }
    }
    if (!changed) break;
  }
  for (auto& c : cells) { if (c.type != CT_WALL && c.type != CT_VOID && c.type != CT_RAMP) { if (!c.floorKnown) { c.floor = 0; c.floorKnown = true; } if (c.floorMat < 0) c.floorMat = defaultFloorMat; } }
  // ramps
  auto floorOf = [&](int x, int y, float def) { if (!inb(x, y)) return def; Cell& c = C(x, y); if (c.type == CT_WALL || c.type == CT_VOID || c.type == CT_RAMP) return def; return c.floor; };
  for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
    Cell& c = C(x, y); if (c.type != CT_RAMP) continue;
    int dxr = 0, dyr = 0; // rising direction in grid space
    switch (c.rampDir) { case 0: dxr = 1; break; case 1: dxr = -1; break; case 2: dyr = -1; break; default: dyr = 1; break; }
    int sx = x, sy = y; while (inb(sx - dxr, sy - dyr) && C(sx - dxr, sy - dyr).type == CT_RAMP && C(sx - dxr, sy - dyr).rampDir == c.rampDir) { sx -= dxr; sy -= dyr; }
    int ex = x, ey = y; while (inb(ex + dxr, ey + dyr) && C(ex + dxr, ey + dyr).type == CT_RAMP && C(ex + dxr, ey + dyr).rampDir == c.rampDir) { ex += dxr; ey += dyr; }
    float lo = floorOf(sx - dxr, sy - dyr, 0), hi = floorOf(ex + dxr, ey + dyr, lo + 64);
    int n = std::abs(ex - sx) + std::abs(ey - sy) + 1, idx = std::abs(x - sx) + std::abs(y - sy);
    c.rampLo = lo + (hi - lo) * idx / n; c.rampHi = lo + (hi - lo) * (idx + 1) / n; c.floor = c.rampLo; c.floorKnown = true;
    if (c.rampHi < c.rampLo) { std::swap(c.rampLo, c.rampHi); c.rampDir ^= 1; }
  }

  // compile -----------------------------------------------------------------
  CEngineTrace& et = g_EngineTrace;
  et.Clear();
  float minFloor = 0, maxTop = wallHeight;
  for (auto& c : cells) if (c.type != CT_WALL && c.type != CT_VOID) minFloor = std::min(minFloor, c.floor);
  const float bottom = minFloor - 64.f;
  auto X0 = [&](int x) { return gridOrigin.x + x * cell; };
  auto Y1 = [&](int y) { return gridOrigin.y - y * cell; };
  auto surfOf = [](int mat) { return GetMaterialDef(mat).surfaceProp; };
  // greedy rectangle merge
  auto greedy = [&](auto keyFn, auto emit) {
    std::vector<char> used(gw * gh, 0);
    for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
      if (used[y * gw + x]) continue;
      long long k = keyFn(x, y); if (k < 0) continue;
      int x1 = x; while (x1 + 1 < gw && !used[y * gw + x1 + 1] && keyFn(x1 + 1, y) == k) x1++;
      int y1 = y; bool ok = true;
      while (ok && y1 + 1 < gh) { for (int xx = x; xx <= x1; xx++) if (used[(y1 + 1) * gw + xx] || keyFn(xx, y1 + 1) != k) { ok = false; break; } if (ok) y1++; }
      for (int yy = y; yy <= y1; yy++) for (int xx = x; xx <= x1; xx++) used[yy * gw + xx] = 1;
      emit(x, y, x1, y1, k);
    }
  };
  // floors (non-ramp walkable cells)
  greedy([&](int x, int y) -> long long { Cell& c = C(x, y); if (c.type == CT_WALL || c.type == CT_VOID || c.type == CT_RAMP) return -1; return (long long)(c.floor + 4096) * 1000 + c.floorMat; },
    [&](int x0, int y0, int x1, int y1, long long) { Cell& c = C(x0, y0);
      et.AddBox({ X0(x0), Y1(y1 + 1), bottom }, { X0(x1 + 1), Y1(y0), c.floor }, CONTENTS_SOLID, surfOf(c.floorMat), c.floorMat); });
  // ramps: base box + wedge, merged perpendicular to the slope
  greedy([&](int x, int y) -> long long { Cell& c = C(x, y); if (c.type != CT_RAMP) return -1;
      long long along = (c.rampDir < 2) ? x : y; return ((long long)(c.rampLo + 4096) * 10000 + (long long)(c.rampHi + 4096)) * 100000 + c.rampDir * 10000 + along * 10 + 1; },
    [&](int x0, int y0, int x1, int y1, long long) { Cell& c = C(x0, y0);
      Vector mn{ X0(x0), Y1(y1 + 1), bottom }, mx{ X0(x1 + 1), Y1(y0), c.rampLo };
      if (c.rampLo - bottom > 1) et.AddBox(mn, mx, CONTENTS_SOLID, surfOf(c.floorMat), c.floorMat);
      int dir = c.rampDir; // 0 +x, 1 -x, 2 +y(north), 3 -y
      if (c.rampHi - c.rampLo > 0.5f) et.AddRamp({ mn.x, mn.y, c.rampLo }, { mx.x, mx.y, c.rampHi }, dir, CONTENTS_SOLID, surfOf(c.floorMat), c.floorMat);
    });
  // walls
  greedy([&](int x, int y) -> long long { Cell& c = C(x, y); if (c.type != CT_WALL) return -1; float top = c.wallTop > 0 ? c.wallTop : wallHeight; return (long long)top * 1000 + c.wallMat; },
    [&](int x0, int y0, int x1, int y1, long long) { Cell& c = C(x0, y0); float top = c.wallTop > 0 ? c.wallTop : wallHeight; maxTop = std::max(maxTop, top);
      et.AddBox({ X0(x0), Y1(y1 + 1), bottom }, { X0(x1 + 1), Y1(y0), top }, CONTENTS_SOLID, surfOf(c.wallMat), c.wallMat); });
  // ceilings (tunnels): block from floor+ceil to the wall height
  greedy([&](int x, int y) -> long long { Cell& c = C(x, y); if (c.ceil <= 0 || c.type == CT_WALL) return -1; return (long long)(c.floor + c.ceil + 4096); },
    [&](int x0, int y0, int x1, int y1, long long) { Cell& c = C(x0, y0); float z0 = c.floor + c.ceil, z1 = std::max(wallHeight, z0 + 32);
      et.AddBox({ X0(x0), Y1(y1 + 1), z0 }, { X0(x1 + 1), Y1(y0), z1 }, CONTENTS_SOLID, SURFACE_SANDSTONE, MAT_SANDSTONE_WALL); });
  // objects
  for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
    Cell& c = C(x, y);
    if (c.type != CT_OBJ) continue;
    float x0 = X0(x), x1 = X0(x + 1), y0 = Y1(y + 1), y1 = Y1(y), z = c.floor;
    int sp = surfOf(c.objMat);
    switch (c.obj) {
      case OBJ_CRATE: et.AddBox({ x0 + 1, y0 + 1, z }, { x1 - 1, y1 - 1, z + 64 }, CONTENTS_SOLID, sp, c.objMat); break;
      case OBJ_CRATE_SMALL: et.AddBox({ x0 + 12, y0 + 12, z }, { x1 - 12, y1 - 12, z + 40 }, CONTENTS_SOLID, sp, c.objMat); break;
      case OBJ_CRATE2:
        et.AddBox({ x0 + 1, y0 + 1, z }, { x1 - 1, y1 - 1, z + 64 }, CONTENTS_SOLID, sp, c.objMat);
        et.AddBox({ x0 + 4, y0 + 4, z + 64 }, { x1 - 4, y1 - 4, z + 124 }, CONTENTS_SOLID, sp, c.objMat == MAT_CRATE ? MAT_CRATE_DARK : MAT_CRATE);
        break;
      case OBJ_LOWWALL: et.AddBox({ x0, y0, z }, { x1, y1, z + 40 }, CONTENTS_SOLID, sp, c.objMat); break;
      case OBJ_MIDWALL: et.AddBox({ x0, y0, z }, { x1, y1, z + 96 }, CONTENTS_SOLID, sp, c.objMat); break;
      case OBJ_BARREL: {
        float cx = (x0 + x1) * 0.5f, cy = (y0 + y1) * 0.5f, r = 15.f, h = 44.f;
        std::vector<cplane_t> pl;
        for (int i = 0; i < 8; i++) { float a = i * (M_PI_F / 4); cplane_t p; p.normal = { std::cos(a), std::sin(a), 0 }; p.dist = p.normal.Dot(Vector(cx, cy, 0)) + r; pl.push_back(p); }
        cplane_t top; top.normal = { 0, 0, 1 }; top.dist = z + h; pl.push_back(top);
        cplane_t bot; bot.normal = { 0, 0, -1 }; bot.dist = -z; pl.push_back(bot);
        // axial bevels
        for (int i = 0; i < 4; i++) { cplane_t p; p.normal.Init(); p.normal[i / 2] = (i & 1) ? -1.f : 1.f; float cc = i / 2 == 0 ? cx : cy; p.dist = (i & 1) ? -(cc - r) : (cc + r); pl.push_back(p); }
        et.AddBrush(pl, { cx - r, cy - r, z }, { cx + r, cy + r, z + h }, CONTENTS_SOLID, SURFACE_METAL, MAT_BARREL);
        break;
      }
    }
  }
  // world bounds: invisible clip ceiling (keeps players in the map)
  Vector wmn{ gridOrigin.x - 64, gridOrigin.y - gh * cell - 64, bottom }, wmx{ gridOrigin.x + gw * cell + 64, gridOrigin.y + 64, maxTop + 256 };
  et.AddBox({ wmn.x, wmn.y, maxTop + 200 }, { wmx.x, wmx.y, maxTop + 232 }, CONTENTS_PLAYERCLIP, SURFACE_DEFAULT, MAT_NONE, false);
  et.AddBox({ wmn.x - 32, wmn.y, bottom }, { wmn.x, wmx.y, maxTop + 232 }, CONTENTS_PLAYERCLIP, SURFACE_DEFAULT, MAT_NONE, false);
  et.AddBox({ wmx.x, wmn.y, bottom }, { wmx.x + 32, wmx.y, maxTop + 232 }, CONTENTS_PLAYERCLIP, SURFACE_DEFAULT, MAT_NONE, false);
  et.AddBox({ wmn.x, wmn.y - 32, bottom }, { wmx.x, wmn.y, maxTop + 232 }, CONTENTS_PLAYERCLIP, SURFACE_DEFAULT, MAT_NONE, false);
  et.AddBox({ wmn.x, wmx.y, bottom }, { wmx.x, wmx.y + 32, maxTop + 232 }, CONTENTS_PLAYERCLIP, SURFACE_DEFAULT, MAT_NONE, false);
  et.Finalize();
  mins = wmn; maxs = wmx;

  // grid info, spawns & zones ---------------------------------------------------
  floorZ.assign(gw * gh, NAN); cellType.assign(gw * gh, 0); cellMat.assign(gw * gh, 0);
  Vector siteMin[2] = { Vector(1e9f, 1e9f, 1e9f), Vector(1e9f, 1e9f, 1e9f) }, siteMax[2] = { Vector(-1e9f, -1e9f, -1e9f), Vector(-1e9f, -1e9f, -1e9f) };
  Vector buyMin[2] = { Vector(1e9f, 1e9f, 1e9f), Vector(1e9f, 1e9f, 1e9f) }, buyMax[2] = { Vector(-1e9f, -1e9f, -1e9f), Vector(-1e9f, -1e9f, -1e9f) };
  for (int y = 0; y < gh; y++) for (int x = 0; x < gw; x++) {
    Cell& c = C(x, y); int i = y * gw + x;
    switch (c.type) {
      case CT_WALL: cellType[i] = 2; cellMat[i] = c.wallMat; break;
      case CT_VOID: cellType[i] = 0; break;
      case CT_RAMP: cellType[i] = 3; floorZ[i] = (c.rampLo + c.rampHi) * 0.5f; cellMat[i] = c.floorMat; break;
      case CT_OBJ: cellType[i] = 4; cellMat[i] = c.objMat;
        if (c.obj == OBJ_LOWWALL) floorZ[i] = c.floor + 40; else if (c.obj == OBJ_CRATE) floorZ[i] = c.floor + 64; else if (c.obj == OBJ_CRATE_SMALL) floorZ[i] = c.floor + 40; break;
      default: cellType[i] = 1; floorZ[i] = c.floor; cellMat[i] = c.floorMat; break;
    }
    Vector cc = CellCenter(x, y); cc.z = c.floor;
    Vector cmn{ X0(x), Y1(y + 1), c.floor }, cmx{ X0(x + 1), Y1(y), c.floor + 128 };
    if (c.marker == MK_SPAWN_T || c.marker == MK_SPAWN_CT) {
      int team = c.marker == MK_SPAWN_T ? 2 : 3;
      spawns.push_back({ cc + Vector(0, 0, 1), 0.f, team });
      buyMin[team - 2] = VecMin(buyMin[team - 2], cmn); buyMax[team - 2] = VecMax(buyMax[team - 2], cmx);
    }
    if (c.marker == MK_SITE_A || c.marker == MK_SITE_B) { int s = c.marker == MK_SITE_A ? 0 : 1; siteMin[s] = VecMin(siteMin[s], cmn); siteMax[s] = VecMax(siteMax[s], cmx); }
    if (c.lamp) lights.push_back({ cc + Vector(0, 0, c.ceil > 0 ? c.ceil - 12 : 112), { 1.0f, 0.78f, 0.5f }, 360 });
  }
  for (int s = 0; s < 2; s++) if (siteMin[s].x < 1e8f) zones.push_back({ siteMin[s] - Vector(16, 16, 0), siteMax[s] + Vector(16, 16, 0), s == 0 ? ZONE_BOMBSITE_A : ZONE_BOMBSITE_B });
  for (int t = 0; t < 2; t++) if (buyMin[t].x < 1e8f) zones.push_back({ buyMin[t] - Vector(160, 160, 64), buyMax[t] + Vector(160, 160, 64), t == 0 ? ZONE_BUY_T : ZONE_BUY_CT });
  // spawn yaw: face the enemy spawn / map center unless given
  Vector centerT, centerCT; int nT = 0, nCT = 0;
  for (auto& s : spawns) { if (s.team == 2) { centerT += s.origin; nT++; } else { centerCT += s.origin; nCT++; } }
  if (nT) centerT /= (float)nT; if (nCT) centerCT /= (float)nCT;
  for (auto& s : spawns) {
    Vector target = s.team == 2 ? (nCT ? centerCT : Vector()) : (nT ? centerT : Vector());
    s.yaw = RAD2DEGF(std::atan2(target.y - s.origin.y, target.x - s.origin.x));
    for (auto& sy : spawnYaw) if (sy.team == s.team) s.yaw = sy.yaw;
  }
  for (auto& pl : plights) {
    Vector c = CellCenter((int)pl.cx, (int)pl.cy); c.x += (pl.cx - (int)pl.cx) * cell; c.y -= (pl.cy - (int)pl.cy) * cell;
    lights.push_back({ { c.x, c.y, pl.h }, { pl.r, pl.g, pl.b }, pl.rad });
  }
  for (auto& pp : pplaces) {
    Vector a = { X0((int)pp.x0), Y1((int)pp.y1 + 1), -1e4f }, b = { X0((int)pp.x1 + 1), Y1((int)pp.y0), 1e4f };
    places.push_back({ pp.n, VecMin(a, b), VecMax(a, b) });
  }
  Msg("Map %s: %dx%d cells, %d brushes, %d spawns, %d zones, %d lights\n", name.c_str(), gw, gh, (int)et.brushes.size(), (int)spawns.size(), (int)zones.size(), (int)lights.size());
  return true;
}

// ------------------------------------------------------------------------------------------------
// original CS 1.6 maps
std::string ResolveMapFile(const std::string& name) {
  if (name.size() > 4 && hlfs::Lower(name.substr(name.size() - 4)) == ".bsp") return name;
  if (name.rfind("css:", 0) == 0) return name;
  std::string local = "assets/maps/" + name + ".map";
  // Counter-Strike: Source first, then CS 1.6, then the built-in remakes
  if (srcfs::Available() && srcfs::Exists("maps/" + name + ".bsp")) return "css:" + name;
  if (hl_content.GetBool()) {
    std::string p = hlfs::Find("maps/" + name + ".bsp");
    if (!p.empty()) return p;
  }
  return local;
}

bool CMapInfo::LoadBsp(const std::string& path) {
  *this = CMapInfo();
  file = path; isBsp = true;
  name = hlfs::Lower(std::filesystem::path(path).stem().string()); title = name;
  g_EngineTrace.Clear();
  if (!g_Bsp.Load(path, name)) { Warning("BSP %s failed to load\n", path.c_str()); return false; }
  const CBspWorld& W = g_Bsp;
  mins = W.worldMins; maxs = W.worldMaxs;
  g_EngineTrace.worldMins = mins; g_EngineTrace.worldMaxs = maxs;
  sunDir = W.sunDir; sunColor = W.sunColor;
  wallHeight = maxs.z - mins.z;
  bool hasBomb = false, hasHostages = false;
  int site = 0;
  // GoldSrc spawn origins are hull centers: drop them onto the floor (feet position)
  auto dropToFloor = [&](Vector o) {
    BspTrace tr; g_Bsp.Trace(1, o, o - Vector(0, 0, 256), tr, true);
    if (!tr.startsolid && tr.fraction < 1.f) return tr.endpos - Vector(0, 0, 36);
    return o - Vector(0, 0, 36);
  };
  for (const BspEntity& e : W.entities) {
    std::string cls = e.Class();
    if (cls == "info_player_start" || cls == "info_player_deathmatch") {
      MapSpawn s; s.team = cls == "info_player_start" ? 3 /*CT*/ : 2 /*T*/;
      s.origin = dropToFloor(e.GetVector("origin"));
      s.yaw = e.Has("angles") ? e.GetVector("angles").y : e.GetFloat("angle");
      spawns.push_back(s);
    } else if (cls == "func_bomb_target" || cls == "info_bomb_target") {
      hasBomb = true;
      MapZone z; z.type = site++ == 0 ? ZONE_BOMBSITE_A : ZONE_BOMBSITE_B;
      int mi = e.ModelIndex();
      if (mi > 0 && mi < (int)W.models.size()) { z.mins = W.models[mi].mins; z.maxs = W.models[mi].maxs; }
      else { Vector o = e.GetVector("origin"); z.mins = o - Vector(128, 128, 64); z.maxs = o + Vector(128, 128, 128); }
      if (site <= 2) zones.push_back(z);
    } else if (cls == "func_buyzone") {
      int mi = e.ModelIndex(); if (mi <= 0 || mi >= (int)W.models.size()) continue;
      int team = e.GetInt("team", 0);
      MapZone z; z.mins = W.models[mi].mins; z.maxs = W.models[mi].maxs;
      if (team == 0 || team == 1) { z.type = ZONE_BUY_T; zones.push_back(z); }
      if (team == 0 || team == 2) { z.type = ZONE_BUY_CT; zones.push_back(z); }
    } else if (cls == "hostage_entity" || cls == "func_hostage_rescue" || cls == "info_hostage_rescue") hasHostages = true;
    else if (cls == "light_environment") {
      Vector c = e.GetVector("_light", Vector(255, 255, 230));
      sunColor = Vector(c.x, c.y, c.z) / 255.f;
    }
  }
  // maps without buy zones: CS 1.6 puts one around each team's spawn points
  for (int team = 2; team <= 3; team++) {
    int type = team == 2 ? ZONE_BUY_T : ZONE_BUY_CT;
    if (FindZone(type)) continue;
    MapZone z; z.type = type; z.mins = Vector(1e9f, 1e9f, 1e9f); z.maxs = Vector(-1e9f, -1e9f, -1e9f); bool any = false;
    for (auto& s : spawns) if (s.team == team) { z.mins = VecMin(z.mins, s.origin); z.maxs = VecMax(z.maxs, s.origin); any = true; }
    if (!any) continue;
    z.mins -= Vector(200, 200, 64); z.maxs += Vector(200, 200, 128);
    zones.push_back(z);
  }
  mode = hasBomb ? "bomb" : (hasHostages ? "hostage" : "elimination");
  desc = "CS 1.6";
  Msg("Map %s (CS 1.6 BSP): %d spawns, %d zones, %d brush entities, %d textures missing\n", name.c_str(), (int)spawns.size(), (int)zones.size(), (int)W.brushEnts.size(), W.texturesMissing);
  return true;
}

// ------------------------------------------------------------------------------------------------
// Counter-Strike: Source maps
bool CMapInfo::LoadSrc(const std::string& mapName) {
  *this = CMapInfo();
  name = hlfs::Lower(mapName); title = name; file = "css:" + name; isBsp = true; isSrc = true;
  g_Bsp.Unload();
  // materials / models of the previous map (its pakfile may have overridden them)
  SrcMdl_Clear(); SrcMat_Clear();
  g_EngineTrace.Clear();
  if (!g_SrcBsp.Load(name)) { Warning("CS:S map %s failed to load\n", name.c_str()); g_EngineTrace.Finalize(); return false; }
  const CSrcBsp& W = g_SrcBsp;
  mins = W.worldMins; maxs = W.worldMaxs;
  g_EngineTrace.worldMins = mins; g_EngineTrace.worldMaxs = maxs;
  g_EngineTrace.Finalize();
  sunDir = W.sunDir; sunColor = W.sunColor;
  wallHeight = maxs.z - mins.z;
  bool hasBomb = false, hasHostages = false;
  auto modelBox = [&](const BspEntity& e, Vector& a, Vector& b) {
    int mi = e.ModelIndex(); if (mi <= 0 || mi >= (int)W.modelBounds.size()) return false;
    Vector o = e.GetVector("origin");
    a = W.modelBounds[mi].first + o; b = W.modelBounds[mi].second + o; return true;
  };
  // Source player origins are already at the feet; settle them on the floor in case they float / clip
  auto dropToFloor = [&](Vector o) {
    trace_t tr; g_EngineTrace.TraceHull(o + Vector(0, 0, 16), o - Vector(0, 0, 128), Vector(-16, -16, 0), Vector(16, 16, 72), MASK_PLAYERSOLID_BRUSHONLY, nullptr, &tr);
    if (!tr.startsolid && tr.fraction < 1.f) return tr.endpos;
    return o;
  };
  std::vector<MapZone> sites;
  for (const BspEntity& e : W.entities) {
    std::string cls = e.Class();
    if (cls == "info_player_terrorist" || cls == "info_player_counterterrorist") {
      MapSpawn s; s.team = cls == "info_player_terrorist" ? 2 : 3;
      s.origin = dropToFloor(e.GetVector("origin"));
      s.yaw = e.Has("angles") ? e.GetVector("angles").y : e.GetFloat("angle");
      spawns.push_back(s);
    } else if (cls == "func_bomb_target") {
      hasBomb = true; MapZone z;
      if (!modelBox(e, z.mins, z.maxs)) continue;
      sites.push_back(z);
    } else if (cls == "info_bomb_target") {
      hasBomb = true; MapZone z; Vector o = e.GetVector("origin"); z.mins = o - Vector(128, 128, 64); z.maxs = o + Vector(128, 128, 128); sites.push_back(z);
    } else if (cls == "func_buyzone") {
      MapZone z; if (!modelBox(e, z.mins, z.maxs)) continue;
      int team = e.GetInt("TeamNum", e.GetInt("teamnum", 0));
      if (team == 0 || team == 2) { z.type = ZONE_BUY_T; zones.push_back(z); }
      if (team == 0 || team == 3) { z.type = ZONE_BUY_CT; zones.push_back(z); }
    } else if (cls == "hostage_entity" || cls == "func_hostage_rescue" || cls == "info_hostage_rescue") hasHostages = true;
  }
  // bomb sites in entity order: first = A, second = B
  for (size_t i = 0; i < sites.size() && i < 2; i++) { sites[i].type = i == 0 ? ZONE_BOMBSITE_A : ZONE_BOMBSITE_B; zones.push_back(sites[i]); }
  for (int team = 2; team <= 3; team++) {
    int type = team == 2 ? ZONE_BUY_T : ZONE_BUY_CT;
    if (FindZone(type)) continue;
    MapZone z; z.type = type; z.mins = Vector(1e9f, 1e9f, 1e9f); z.maxs = Vector(-1e9f, -1e9f, -1e9f); bool any = false;
    for (auto& s : spawns) if (s.team == team) { z.mins = VecMin(z.mins, s.origin); z.maxs = VecMax(z.maxs, s.origin); any = true; }
    if (!any) continue;
    z.mins -= Vector(200, 200, 64); z.maxs += Vector(200, 200, 128);
    zones.push_back(z);
  }
  mode = hasBomb ? "bomb" : (hasHostages ? "hostage" : "elimination");
  desc = "Counter-Strike: Source";
  Msg("Map %s (CS:S VBSP v%d): %d spawns, %d zones, %d faces, %d brushes, %d disp tris, %d props (%d prop hulls)\n", name.c_str(), W.version,
      (int)spawns.size(), (int)zones.size(), (int)W.faces.size(), W.numCollisionBrushes, W.numDispTris, (int)W.props.size(), W.numPropHulls);
  return true;
}

std::vector<MapListEntry> ListMaps() {
  std::vector<MapListEntry> out;
  std::error_code ec;
  for (auto& e : std::filesystem::directory_iterator("assets/maps", ec)) {
    if (e.path().extension() != ".map") continue;
    MapListEntry m; m.file = e.path().string(); m.name = e.path().stem().string(); m.title = m.name; m.mode = "bomb";
    std::ifstream f(m.file); std::string line;
    while (std::getline(f, line)) {
      auto t = tokens(trim(line)); if (t.empty()) continue;
      if (t[0] == "grid") break;
      if (t[0] == "title" && t.size() > 1) m.title = t[1];
      if (t[0] == "mode" && t.size() > 1) m.mode = t[1];
      if (t[0] == "desc" && t.size() > 1) m.desc = t[1];
    }
    out.push_back(m);
  }
  // original maps from the CS 1.6 install replace the built-in remakes of the same name
  if (hl_content.GetBool()) {
    for (const std::string& n : hlfs::List("maps", ".bsp")) {
      std::string file = hlfs::Find("maps/" + n + ".bsp"); if (file.empty()) continue;
      auto it = std::find_if(out.begin(), out.end(), [&](const MapListEntry& m) { return m.name == n; });
      MapListEntry m; m.name = n; m.title = n; m.file = file; m.desc = "CS 1.6";
      m.mode = n.rfind("de_", 0) == 0 ? "bomb" : (n.rfind("cs_", 0) == 0 ? "hostage" : "elimination");
      if (it != out.end()) { m.title = it->title; *it = m; } else out.push_back(m);
    }
  }
  // Counter-Strike: Source maps win over both
  if (srcfs::Available()) {
    for (const std::string& n0 : srcfs::List("maps", ".bsp")) {
      std::string n = hlfs::Lower(n0);
      if (n.rfind("test_", 0) == 0) continue;
      auto it = std::find_if(out.begin(), out.end(), [&](const MapListEntry& m) { return m.name == n; });
      MapListEntry m; m.name = n; m.title = n; m.file = "css:" + n; m.desc = "CS:S";
      m.mode = n.rfind("de_", 0) == 0 ? "bomb" : (n.rfind("cs_", 0) == 0 ? "hostage" : "elimination");
      if (it != out.end()) { m.title = it->title; *it = m; } else out.push_back(m);
    }
  }
  std::sort(out.begin(), out.end(), [](auto& a, auto& b) { return a.name < b.name; });
  return out;
}
