// download.cpp - see download.hpp
#include "download.hpp"
#include "srcfs.hpp"
#include "convar.hpp"
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

#if defined(PLATFORM_ANDROID)
#include <android_native_app_glue.h>
#include <jni.h>
extern "C" struct android_app* GetAndroidApp(void);
#elif defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOGDI
#define NOUSER
#include <windows.h>
#include <winhttp.h>
#endif

extern "C" int sinflate(void* out, int cap, const void* in, int size);   // raylib (external/sinfl.h)

namespace fs = std::filesystem;

// CS:GO maps: raw .bsp files of a public CS:GO FastDL mirror (fast path) with the GitHub contents API as the fallback.
// The maps are the real VBSP v21 CS:GO builds, so their own materials / cubemaps that are packed inside the bsp
// (lump 40) come along with them.
ConVar cl_content_url_maps("cl_content_url_maps", "https://raw.githubusercontent.com/wiktorxxiii/csgo-fastdl/HEAD/csgo/", FCVAR_ARCHIVE, "Base url of the CS:GO maps (maps/*.bsp)");
// real CS:GO maps (official builds with their materials / props packed inside)
ConVar cl_content_url_maps_go("cl_content_url_maps_go", "https://raw.githubusercontent.com/wiktorxxiii/csgo-fastdl/HEAD/csgo/", FCVAR_ARCHIVE, "Base url of the CS:GO map mirror (bare maps/x.bsp entries)");
ConVar cl_content_url_maps_go_api("cl_content_url_maps_go_api", "https://api.github.com/repos/wiktorxxiii/csgo-fastdl/contents/csgo/%s?ref=HEAD", FCVAR_ARCHIVE, "Fallback url template (%s = file) of the CS:GO maps");
ConVar cl_content_url_maps_api("cl_content_url_maps_api", "https://api.github.com/repos/wiktorxxiii/csgo-fastdl/contents/csgo/%s?ref=HEAD", FCVAR_ARCHIVE, "Fallback url template (%s = file) of the CS:GO maps");

// The shared Source asset library that provides the agents (4 CT + 4 T), the weapon models, the sound effects and the
// sound scripts. Only the files listed in content_base.inc are downloaded (~84 MB instead of the whole 1.3 GB library).
ConVar cl_content_url_base("cl_content_url_base", "https://raw.githubusercontent.com/bouletmarc/css_content/master/", FCVAR_ARCHIVE, "Base url of the shared Source agents / weapons / sounds");
ConVar cl_content_url_base_api("cl_content_url_base_api", "https://api.github.com/repos/bouletmarc/css_content/contents/%s?ref=master", FCVAR_ARCHIVE, "Fallback url template (%s = file) of the shared Source agents / weapons / sounds");

// CS:GO R8 Revolver from a Garry's Mod addon (pinned commit): raw.githubusercontent.com first, the GitHub contents API
// (Accept: application/vnd.github.raw) as the fallback
ConVar cl_content_url_r8("cl_content_url_r8", "https://raw.githubusercontent.com/EntranceJew/gmod-addons/48675a7f3a5a06d826bebba97078e24d0124f728/weapon_ttt_csgo_r8revolver/gamemodes/terrortown/content/", FCVAR_ARCHIVE, "Base url of the CS:GO R8 Revolver files (view / world model, textures, sounds)");
ConVar cl_content_url_r8_api("cl_content_url_r8_api", "https://api.github.com/repos/EntranceJew/gmod-addons/contents/weapon_ttt_csgo_r8revolver/gamemodes/terrortown/content/%s?ref=48675a7f3a5a06d826bebba97078e24d0124f728", FCVAR_ARCHIVE, "Fallback url template (%s = file) of the R8 Revolver files");

// CS:GO SCAR-20 / SSG 08 view models, molotov + incendiary (models, sounds) from the ARC9 GSR Garry's Mod addon (pinned commit)
ConVar cl_content_url_csgo("cl_content_url_csgo", "https://raw.githubusercontent.com/Seulyy/ARC9-GSR/5e16cf9baa37348fc82ba0c824d842dbcb44ab18/", FCVAR_ARCHIVE, "Base url of the CS:GO model / sound extras");
ConVar cl_content_url_csgo_api("cl_content_url_csgo_api", "https://api.github.com/repos/Seulyy/ARC9-GSR/contents/%s?ref=5e16cf9baa37348fc82ba0c824d842dbcb44ab18", FCVAR_ARCHIVE, "Fallback url template (%s = file) of the CS:GO extras");
// CS:GO weapon / equipment icons (SVG, rasterized at load time)
ConVar cl_content_url_icons("cl_content_url_icons", "https://raw.githubusercontent.com/atiksoftware/csgo_memory_hacking_examples/4afd922fd7ae95c8a9b393d423944540182235f8/csgo_weapon_icons/", FCVAR_ARCHIVE, "Base url of the CS:GO weapon icons (SVG)");
ConVar cl_content_url_icons_api("cl_content_url_icons_api", "https://api.github.com/repos/atiksoftware/csgo_memory_hacking_examples/contents/csgo_weapon_icons/%s?ref=4afd922fd7ae95c8a9b393d423944540182235f8", FCVAR_ARCHIVE, "Fallback url template (%s = file) of the CS:GO weapon icons");

namespace download {
namespace {
// release assets of this repository: a stable url, the newest release always carries the content packs
#define REPO_ASSETS "https://github.com/QUANTUMLOADER123/twre-346-523/releases/latest/download/"
// where the prebuilt content archives live (a mirror can be used instead of github)
ConVar cl_content_url_packs("cl_content_url_packs", REPO_ASSETS, FCVAR_ARCHIVE,
                            "Base url of the prebuilt content archives (one zip per pack instead of thousands of files)");
const char* kDest = "content/css";
const char* kExtras = "content/extras";

// curated CS:GO map set for HvH / deathmatch. Relative entries come from cl_content_url_maps, absolute ones from
// other public CS:GO FastDL mirrors (a map is a single .bsp file, so every map can live on its own repo).
// The map list: the popular maps people actually play, old (classic) versions, nothing else - no 1v1 arenas,
// no lego/minecraft maps. A bare path is fetched from cl_content_url_maps_go, a full url from its own mirror.
// Every one of these .bsp carries its own materials in the pakfile lump; the ones that reference the shared
// CS:S era textures are covered by content_maps_textures.inc (packed into the same archive).
const char* const s_csgoMapFiles[] = {
  // dust2 (the classic CS:GO "old" one), nuke, train, aztec, seaside, the classics: plain .bsp files, downloaded at
  // the first start
  "https://raw.githubusercontent.com/M1namia/CSGOServerFastdl/HEAD/maps/de_dust2_old.bsp",
  "maps/de_nuke_ve.bsp",
  "https://raw.githubusercontent.com/Roblox90864/CSGOMapArchive/HEAD/maps/de_train2014.bsp",
  "maps/de_aztec_proper.bsp", "maps/de_seaside_b2.bsp",
  // the classics
  "https://raw.githubusercontent.com/Lesona-Systems/csgo-achievement-maps/HEAD/de_dust/de_dust.bsp",
  "https://raw.githubusercontent.com/Lesona-Systems/csgo-achievement-maps/HEAD/cs_office/cs_office.bsp",
  nullptr };

// The CS:GO maps that were ported to CS:S and therefore carry their own materials AND static props inside the pakfile
// lump - they look complete on their own, unlike the plain .bsp dumps that reference the CS:GO vpk. They sit on the
// mirror as .bz2, so they cannot be downloaded file-by-file at runtime: tools/pack_content.py unpacks them into
// maps/<name>.bsp inside cshvh-content-all.zip, and the archive puts them on disk on the first start.
const char* const s_portMapFiles[] = {
  // vertigo (the CS:GO remake - the construction-site one), inferno and mirage
  "https://raw.githubusercontent.com/GOD10/Source_FastDL/HEAD/cstrike/maps/de_vertigo_csgo_new.bsp.bz2",
  "https://raw.githubusercontent.com/GOD10/Source_FastDL/HEAD/cstrike/maps/de_inferno_csgo_new.bsp.bz2",
  "https://raw.githubusercontent.com/GOD10/Source_FastDL/HEAD/cstrike/maps/de_mirage_csgo_new.bsp.bz2",
  nullptr };

const char* const s_r8Files[] = {
  "models/weapons/v_models/csgo/v_pist_revolver.mdl", "models/weapons/v_models/csgo/v_pist_revolver.vvd", "models/weapons/v_models/csgo/v_pist_revolver.dx90.vtx",
  "models/weapons/w_models/csgo/w_pist_revolver.mdl", "models/weapons/w_models/csgo/w_pist_revolver.vvd", "models/weapons/w_models/csgo/w_pist_revolver.dx90.vtx",
  "materials/models/weapons/v_models/csgo/pist_revolver/pist_revolver.vmt", "materials/models/weapons/v_models/csgo/pist_revolver/pist_revolver.vtf",
  "materials/models/weapons/v_models/csgo/pist_revolver/pist_revolver_exponent.vtf",
  "materials/csgo/models/weapons/w_models/w_pist_revolver/pist_revolver.vmt", "materials/csgo/models/weapons/w_models/w_pist_revolver/pist_revolver.vtf",
  "materials/csgo/models/weapons/w_models/w_pist_revolver/pist_revolver_exponent.vtf",
  "sound/weapons/csgo/revolver/revolver-1_01.wav", "sound/weapons/csgo/revolver/revolver_prepare.wav", "sound/weapons/csgo/revolver/revolver_draw.wav",
  "sound/weapons/csgo/revolver/revolver_clipout.wav", "sound/weapons/csgo/revolver/revolver_clipin.wav", "sound/weapons/csgo/revolver/revolver_sideback.wav",
  "sound/weapons/csgo/revolver/revolver_siderelease.wav",
  nullptr };

const char* const s_csgoFiles[] = {
  "models/weapons/csgo/c_snip_scar20.mdl", "models/weapons/csgo/c_snip_scar20.vvd", "models/weapons/csgo/c_snip_scar20.dx90.vtx",
  "models/weapons/csgo/c_snip_ssg08.mdl", "models/weapons/csgo/c_snip_ssg08.vvd", "models/weapons/csgo/c_snip_ssg08.dx90.vtx",
  "models/weapons/csgo/v_eq_molotov.mdl", "models/weapons/csgo/v_eq_molotov.vvd", "models/weapons/csgo/v_eq_molotov.dx90.vtx",
  "models/weapons/csgo/v_eq_incediary.mdl", "models/weapons/csgo/v_eq_incediary.vvd", "models/weapons/csgo/v_eq_incediary.dx90.vtx",
  "models/weapons/csgo/w_eq_molotov.mdl", "models/weapons/csgo/w_eq_molotov.vvd", "models/weapons/csgo/w_eq_molotov.dx90.vtx",
  "models/weapons/csgo/w_eq_incendiary.mdl", "models/weapons/csgo/w_eq_incendiary.vvd", "models/weapons/csgo/w_eq_incendiary.dx90.vtx",
  "materials/models/csgo/scar20/snip_scar20.vmt", "materials/models/csgo/scar20/snip_scar20_body.vmt", "materials/models/csgo/scar20/snip_scar202.vtf",
  "materials/models/csgo/ssg08/snip_ssg08.vmt", "materials/models/csgo/ssg08/snip_ssg08_body.vmt", "materials/models/csgo/ssg08/snip_ssg08_scope.vmt",
  "materials/models/csgo/ssg08/snip_ssg082.vtf", "materials/models/csgo/ssg08/snip_ssg08_scope2.vtf",
  "models/weapons/csgo/atts/scopes/scope_scar.mdl", "models/weapons/csgo/atts/scopes/scope_scar.vvd", "models/weapons/csgo/atts/scopes/scope_scar.dx90.vtx",
  "models/weapons/csgo/atts/scopes/scope_ssg08.mdl", "models/weapons/csgo/atts/scopes/scope_ssg08.vvd", "models/weapons/csgo/atts/scopes/scope_ssg08.dx90.vtx",
  "materials/models/csgo/scar20/scope_scar20.vmt", "materials/models/csgo/shared/scope.vtf",
  "materials/models/csgo/shared/scope_lens_dirt.vmt", "materials/models/csgo/shared/scope_lens_dirt.vtf",
  "materials/models/csgo/molotov/v_eq_molotov_bottle.vmt", "materials/models/csgo/molotov/v_eq_molotov_rag.vmt",
  "materials/models/csgo/molotov/v_eq_molotov_lighter_flame.vmt", "materials/models/csgo/molotov/v_eq_molotov_bottle.vtf",
  "materials/models/csgo/molotov/w_eq_molotov_bottle.vmt", "materials/models/csgo/molotov/w_eq_molotov_rag.vmt",
  "materials/models/csgo/smoke/incendiary_grenade.vmt", "materials/models/csgo/smoke/incendiary_grenade.vtf",
  "sound/weapons/csgo/molotov/molotov_detonate_1.wav", "sound/weapons/csgo/molotov/fire_loop_1.wav", "sound/weapons/csgo/molotov/molotov_extinguish.wav",
  "sound/weapons/csgo/molotov/grenade_throw.wav", "sound/weapons/csgo/molotov/lighter_open.wav",
  "sound/weapons/csgo/incgrenade/inc_grenade_detonate_1.wav", "sound/weapons/csgo/incgrenade/inc_grenade_bounce-1.wav",
  nullptr };
#include "content_base.inc"
#include "content_weapons.inc"
#include "content_maps_textures.inc"
#include "content_agents.inc"

#define ICON(n) "svg_normal/" n ".svg"
const char* const s_iconFiles[] = {
  ICON("weapon_ak47"), ICON("weapon_aug"), ICON("weapon_awp"), ICON("weapon_bizon"), ICON("weapon_c4"), ICON("weapon_cz75a"), ICON("weapon_deagle"),
  ICON("weapon_elite"), ICON("weapon_famas"), ICON("weapon_fiveseven"), ICON("weapon_flashbang"), ICON("weapon_g3sg1"), ICON("weapon_galilar"),
  ICON("weapon_glock"), ICON("weapon_hegrenade"), ICON("weapon_hkp2000"), ICON("weapon_incgrenade"), ICON("weapon_knife"), ICON("weapon_knife_t"),
  ICON("weapon_m249"), ICON("weapon_m4a1"), ICON("weapon_m4a1_silencer"), ICON("weapon_mac10"), ICON("weapon_mag7"), ICON("weapon_molotov"),
  ICON("weapon_mp7"), ICON("weapon_mp9"), ICON("weapon_negev"), ICON("weapon_nova"), ICON("weapon_p250"), ICON("weapon_p90"), ICON("weapon_revolver"),
  ICON("weapon_sawedoff"), ICON("weapon_scar20"), ICON("weapon_sg556"), ICON("weapon_smokegrenade"), ICON("weapon_ssg08"), ICON("weapon_tec9"),
  ICON("weapon_ump45"), ICON("weapon_usp_silencer"), ICON("weapon_xm1014"), ICON("weapon_decoy"), ICON("weapon_taser"),
  ICON("item_assaultsuit"), ICON("item_defuser"), ICON("item_kevlar"),
  nullptr };
#undef ICON

std::mutex s_mtx;
Progress s_prog;
std::atomic<bool> s_cancel{ false }, s_busy{ false };
std::thread s_thread;

constexpr int kPacks = 1;
// ONE archive for everything: every model, texture, map and sound the game needs. It is far smaller than the sum of
// the per-asset archives (the deflate window sees all the textures at once) and the player downloads it once.
Pack s_packs[kPacks] = {
  { "all", "Game content", nullptr, 900, nullptr, "content/extras/all", true, nullptr, "cshvh-content-all.zip" },
};
bool s_needCore = true;   // decided on the main thread in Start()
bool s_haveCsgo = false;  // a local CS:GO install (pak01) - decided on the main thread, read by the worker

void SetError(const std::string& e) { std::lock_guard<std::mutex> l(s_mtx); s_prog.error = e; }

// ---------------------------------------------------------------------------------------------
// streaming zip extractor: local file headers carry the sizes in codeload archives (no data descriptors)
class ZipStream {
public:
  // codeloadLayout: github's /zip/ archive (a "repo-branch/" prefix, unpacked into dest/cstrike)
  // otherwise the entries are taken as they are and unpacked straight into dest
  explicit ZipStream(std::string dest, bool codeloadLayout = true) : m_dest(std::move(dest)), m_codeload(codeloadLayout) {}
  // returns false on a fatal error (m_err)
  bool Feed(const uint8_t* p, size_t n) {
    while (n > 0 && !m_end) {
      if (m_state == HEADER) {
        size_t need = 30 - m_buf.size(); size_t k = std::min(need, n);
        m_buf.insert(m_buf.end(), p, p + k); p += k; n -= k;
        if (m_buf.size() < 30) return true;
        uint32_t sig = U32(0);
        if (sig == 0x02014b50 || sig == 0x06054b50 || sig == 0x06064b50) { m_end = true; return true; }   // central directory
        if (sig != 0x04034b50) { m_err = "bad zip data"; return false; }
        m_flags = U16(6); m_method = U16(8); m_csize = U32(18); m_usize = U32(22);
        m_nameLen = U16(26); m_extraLen = U16(28);
        if (m_flags & 1) { m_err = "encrypted zip"; return false; }
        if (m_flags & 8) { m_err = "zip with data descriptors is not supported"; return false; }
        m_state = NAME; m_buf.clear();
      } else if (m_state == NAME) {
        size_t need = (size_t)m_nameLen + m_extraLen - m_buf.size(); size_t k = std::min(need, n);
        m_buf.insert(m_buf.end(), p, p + k); p += k; n -= k;
        if (m_buf.size() < (size_t)m_nameLen + m_extraLen) return true;
        m_name.assign((const char*)m_buf.data(), m_nameLen);
        if (m_csize == 0xFFFFFFFFu || m_usize == 0xFFFFFFFFu) ParseZip64();
        m_buf.clear(); m_data.clear(); m_data.reserve((size_t)m_csize);
        m_state = DATA;
        if (m_csize == 0) { if (!Finish()) return false; m_state = HEADER; }
      } else {
        size_t need = (size_t)(m_csize - m_data.size()); size_t k = std::min(need, n);
        m_data.insert(m_data.end(), p, p + k); p += k; n -= k;
        if (m_data.size() < m_csize) return true;
        if (!Finish()) return false;
        m_state = HEADER; m_buf.clear();
      }
    }
    return true;
  }
  bool Ended() const { return m_end; }
  const std::string& Error() const { return m_err; }
  int files = 0;
  std::string lastName;
private:
  enum { HEADER, NAME, DATA } m_state = HEADER;
  std::string m_dest, m_name, m_err; bool m_codeload = true;
  std::vector<uint8_t> m_buf, m_data, m_out;
  uint16_t m_flags = 0, m_method = 0, m_nameLen = 0, m_extraLen = 0;
  uint64_t m_csize = 0, m_usize = 0;
  bool m_end = false;
  uint16_t U16(size_t o) const { return (uint16_t)(m_buf[o] | (m_buf[o + 1] << 8)); }
  uint32_t U32(size_t o) const { return (uint32_t)m_buf[o] | ((uint32_t)m_buf[o + 1] << 8) | ((uint32_t)m_buf[o + 2] << 16) | ((uint32_t)m_buf[o + 3] << 24); }
  void ParseZip64() {
    size_t o = m_nameLen, end = (size_t)m_nameLen + m_extraLen;
    while (o + 4 <= end) {
      uint16_t id = U16(o), sz = U16(o + 2);
      if (id == 1) {
        size_t q = o + 4;
        auto U64 = [&](size_t a) { uint64_t v = 0; for (int i = 7; i >= 0; i--) v = (v << 8) | m_buf[a + i]; return v; };
        if (m_usize == 0xFFFFFFFFu && q + 8 <= end) { m_usize = U64(q); q += 8; }
        if (m_csize == 0xFFFFFFFFu && q + 8 <= end) { m_csize = U64(q); }
        return;
      }
      o += 4 + sz;
    }
  }
  // "repo-master/materials/x.vmt" -> dest/cstrike/materials/x.vmt (only game folders)
  bool Finish() {
    std::string rel = m_name;
    if (rel.find("..") != std::string::npos || (!rel.empty() && (rel[0] == '/' || rel[0] == '\\'))) return true;
    fs::path path;
    if (m_codeload) {
      size_t slash = rel.find('/'); if (slash == std::string::npos) return true;
      rel = rel.substr(slash + 1);
      if (rel.empty()) return true;
      std::string top = rel.substr(0, rel.find('/'));
      static const char* allowed[] = { "maps", "materials", "models", "sound", "particles", "scripts", "resource" };
      bool ok = false; for (const char* a : allowed) if (top == a) ok = true;
      if (!ok) return true;
      path = fs::path(m_dest) / "cstrike" / rel;
    } else {
      if (rel.empty()) return true;
      path = fs::path(m_dest) / rel;
    }
    std::error_code ec;
    if (rel.back() == '/') { fs::create_directories(path, ec); return true; }
    const uint8_t* data = m_data.data(); size_t size = m_data.size();
    if (m_method == 8) {
      m_out.resize((size_t)m_usize + 1);
      int got = sinflate(m_out.data(), (int)m_usize, m_data.data(), (int)m_data.size());
      if (got != (int)m_usize) { m_err = "unpack failed: " + rel; return false; }
      data = m_out.data(); size = (size_t)m_usize;
    } else if (m_method != 0) { m_err = "unsupported zip method in " + rel; return false; }
    // already there (an earlier interrupted download): keep it
    if (fs::exists(path, ec) && fs::file_size(path, ec) == size) { files++; lastName = rel; return true; }
    fs::create_directories(path.parent_path(), ec);
    std::string tmp = path.string() + ".part";
    { std::ofstream o(tmp, std::ios::binary); if (!o) { m_err = "cannot write " + path.string(); return false; } o.write((const char*)data, (std::streamsize)size); if (!o) { m_err = "disk full? (" + rel + ")"; return false; } }
    fs::rename(tmp, path, ec);
    if (ec) { m_err = "cannot write " + path.string(); return false; }
    files++; lastName = rel;
    return true;
  }
};

// ---------------------------------------------------------------------------------------------
// HTTP GET streaming: sink returns false to stop
using Sink = std::function<bool(const uint8_t*, size_t)>;

// total size of the body being streamed (Content-Length), -1 when the server does not say. The loading screen
// needs it for a real progress bar instead of a guess: the content archive has no size in its url.
static std::atomic<double> s_httpTotal{ -1 };

#if defined(PLATFORM_ANDROID)
std::string JavaError(JNIEnv* env) {
  jthrowable ex = env->ExceptionOccurred(); env->ExceptionClear();
  if (!ex) return "java error";
  jclass c = env->FindClass("java/lang/Throwable");
  jmethodID ts = env->GetMethodID(c, "toString", "()Ljava/lang/String;");
  jstring s = (jstring)env->CallObjectMethod(ex, ts);
  if (env->ExceptionCheck()) { env->ExceptionClear(); return "java error"; }
  const char* u = env->GetStringUTFChars(s, nullptr); std::string r = u ? u : "java error"; env->ReleaseStringUTFChars(s, u);
  return r;
}
bool HttpGet(const std::string& url, const Sink& sink, std::string& err, const char* accept = nullptr) {
  s_httpTotal.store(-1.0);
  android_app* app = GetAndroidApp();
  if (!app || !app->activity) { err = "no activity"; return false; }
  JavaVM* vm = app->activity->vm; JNIEnv* env = nullptr;
  if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) { err = "JNI attach failed"; return false; }
  bool ok = false;
  do {
    env->PushLocalFrame(64);
    jclass urlCls = env->FindClass("java/net/URL");
    jobject u = env->NewObject(urlCls, env->GetMethodID(urlCls, "<init>", "(Ljava/lang/String;)V"), env->NewStringUTF(url.c_str()));
    if (env->ExceptionCheck()) { err = JavaError(env); env->PopLocalFrame(nullptr); break; }
    jobject conn = env->CallObjectMethod(u, env->GetMethodID(urlCls, "openConnection", "()Ljava/net/URLConnection;"));
    if (env->ExceptionCheck()) { err = JavaError(env); env->PopLocalFrame(nullptr); break; }
    jclass hc = env->FindClass("java/net/HttpURLConnection");
    env->CallVoidMethod(conn, env->GetMethodID(hc, "setInstanceFollowRedirects", "(Z)V"), JNI_TRUE);
    env->CallVoidMethod(conn, env->GetMethodID(hc, "setConnectTimeout", "(I)V"), 20000);
    env->CallVoidMethod(conn, env->GetMethodID(hc, "setReadTimeout", "(I)V"), 60000);
    env->CallVoidMethod(conn, env->GetMethodID(hc, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V"), env->NewStringUTF("User-Agent"), env->NewStringUTF("cshvh"));
    if (accept) env->CallVoidMethod(conn, env->GetMethodID(hc, "setRequestProperty", "(Ljava/lang/String;Ljava/lang/String;)V"), env->NewStringUTF("Accept"), env->NewStringUTF(accept));
    jint code = env->CallIntMethod(conn, env->GetMethodID(hc, "getResponseCode", "()I"));
    if (env->ExceptionCheck()) { err = JavaError(env); env->PopLocalFrame(nullptr); break; }
    if (code != 200) { err = "HTTP " + std::to_string(code); env->PopLocalFrame(nullptr); break; }
    jint total = env->CallIntMethod(conn, env->GetMethodID(hc, "getContentLength", "()I"));
    if (env->ExceptionCheck()) { env->ExceptionClear(); total = -1; }
    s_httpTotal.store(total > 0 ? (double)total : -1.0);
    jobject is = env->CallObjectMethod(conn, env->GetMethodID(hc, "getInputStream", "()Ljava/io/InputStream;"));
    if (env->ExceptionCheck()) { err = JavaError(env); env->PopLocalFrame(nullptr); break; }
    jclass isc = env->FindClass("java/io/InputStream");
    jmethodID rd = env->GetMethodID(isc, "read", "([B)I");
    const int N = 256 * 1024;
    jbyteArray jb = env->NewByteArray(N);
    std::vector<uint8_t> buf(N);
    ok = true;
    for (;;) {
      jint n = env->CallIntMethod(is, rd, jb);
      if (env->ExceptionCheck()) { err = JavaError(env); ok = false; break; }
      if (n < 0) break;
      if (n == 0) continue;
      env->GetByteArrayRegion(jb, 0, n, (jbyte*)buf.data());
      if (!sink(buf.data(), (size_t)n)) { ok = false; break; }
    }
    env->CallVoidMethod(is, env->GetMethodID(isc, "close", "()V"));
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->CallVoidMethod(conn, env->GetMethodID(hc, "disconnect", "()V"));
    if (env->ExceptionCheck()) env->ExceptionClear();
    env->PopLocalFrame(nullptr);
  } while (false);
  vm->DetachCurrentThread();
  return ok;
}
#elif defined(_WIN32)
bool HttpGet(const std::string& url, const Sink& sink, std::string& err, const char* accept = nullptr) {
  s_httpTotal.store(-1.0);
  std::wstring w(url.begin(), url.end());
  URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
  wchar_t host[256] = {}, path[2048] = {};
  uc.lpszHostName = host; uc.dwHostNameLength = 255; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
  if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) { err = "bad url"; return false; }
  HINTERNET s = WinHttpOpen(L"cshvh", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) { err = "WinHttpOpen failed"; return false; }
  // github release assets redirect to a CDN host - WinHTTP does not follow redirects unless told to
  DWORD policy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
  WinHttpSetOption(s, WINHTTP_OPTION_REDIRECT_POLICY, &policy, sizeof(policy));
  WinHttpSetTimeouts(s, 20000, 20000, 60000, 60000);
  HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
  HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
  bool ok = false;
  std::wstring hdr; if (accept) { std::string a = std::string("Accept: ") + accept; hdr.assign(a.begin(), a.end()); }
  if (r && WinHttpSendRequest(r, hdr.empty() ? WINHTTP_NO_ADDITIONAL_HEADERS : hdr.c_str(), hdr.empty() ? 0 : (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
    DWORD code = 0, len = sizeof(code);
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
    if (code != 200) err = "HTTP " + std::to_string(code);
    else {
      DWORD total = 0, tlen = sizeof(total);
      if (WinHttpQueryHeaders(r, WINHTTP_QUERY_CONTENT_LENGTH | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                              &total, &tlen, WINHTTP_NO_HEADER_INDEX) && total > 0)
        s_httpTotal.store((double)total);
      ok = true;
      std::vector<uint8_t> buf(256 * 1024);
      for (;;) {
        DWORD got = 0;
        if (!WinHttpReadData(r, buf.data(), (DWORD)buf.size(), &got)) { err = "connection lost (" + std::to_string(GetLastError()) + ")"; ok = false; break; }
        if (got == 0) break;
        if (!sink(buf.data(), got)) { ok = false; break; }
      }
    }
  } else err = "connection failed (" + std::to_string(GetLastError()) + ")";
  if (r) WinHttpCloseHandle(r);
  if (c) WinHttpCloseHandle(c);
  WinHttpCloseHandle(s);
  return ok;
}
#else
bool HttpGet(const std::string& url, const Sink& sink, std::string& err, const char* accept = nullptr) {
  s_httpTotal.store(-1.0);
  std::string cmd = "curl -fsSL --connect-timeout 10 --retry 2 --speed-limit 4096 --speed-time 60 " + (accept ? std::string("-H 'Accept: ") + accept + "' " : std::string()) + "'" + url + "'";
  FILE* f = popen(cmd.c_str(), "r");
  if (!f) { err = "curl not found"; return false; }
  std::vector<uint8_t> buf(256 * 1024);
  bool ok = true;
  size_t n;
  while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) if (!sink(buf.data(), n)) { ok = false; break; }
  int rc = pclose(f);
  if (ok && rc != 0) { err = "download failed (curl " + std::to_string(rc) + ")"; ok = false; }
  return ok;
}
#endif

// the archive packs unpack into their own destination folder, so their marker lives there too
// bump this whenever the content of the archive changes: an install that already unpacked an older archive would
// otherwise never see the new files (the marker says "done")
constexpr int kContentVersion = 2;
fs::path PackMarker(const Pack& p) { return fs::path(p.zipAsset || !p.files ? (p.dest ? p.dest : kDest) : kExtras) / (std::string(".pack_") + p.id); }
bool PackDone(const Pack& p) {
  std::error_code ec;
  std::ifstream f(PackMarker(p));
  if (!f) return false;
  int version = 0;
  if (!(f >> version)) return false;                 // an older marker ("ok") - the archive has changed since
  return version == kContentVersion;
}
bool PackWanted(const Pack& p) {
  if (PackDone(p)) return false;
  // a local CS:GO / CS:S install already ships every model, map, sound and texture this archive carries
  if (s_haveCsgo) return false;
  // a local CS:GO install already has every stock map and every texture, so both packs are not needed then
  if (p.files == s_csgoMapFiles && s_haveCsgo) return false;
  if (p.files == s_mapTextureFiles && s_haveCsgo) return false;
  if (p.files == s_baseFiles && s_haveCsgo) return false;   // a CS:GO install brings its own agents, weapons and sounds
  if (p.files == s_weaponFiles && s_haveCsgo) return false; // ... and its own CS:GO view models
  if (p.files == s_agentFiles && s_haveCsgo) return false;  // ... and its own CS:GO agent models
  if (std::strcmp(p.id, "props") == 0 && s_haveCsgo) return false;
  return !p.core || s_needCore;
}

// drop the maps that are not in the current list any more (an older, bigger map pack left them behind)
// drop the maps that are not in the current list any more (an older, bigger map pack left them behind)
void PruneMapsIn(const char* path) {
  std::error_code ec;
  fs::path dir(path);
  if (!fs::is_directory(dir, ec)) return;
  auto lower = [](std::string v) { for (char& c : v) c = (char)std::tolower((unsigned char)c); return v; };
  std::vector<std::string> want;
  for (const char* const* list : { s_csgoMapFiles, s_portMapFiles }) {
    for (const char* const* f = list; *f; f++) {
      std::string n = *f; size_t slash = n.find_last_of('/');
      std::string stem = slash == std::string::npos ? n : n.substr(slash + 1);
      if (stem.size() > 8 && stem.compare(stem.size() - 8, 8, ".bsp.bz2") == 0) stem.resize(stem.size() - 8);
      else if (stem.size() > 4) stem.resize(stem.size() - 4);                   // ".bsp"
      want.push_back(lower(stem));
    }
  }
  for (auto it = fs::directory_iterator(dir, ec); it != fs::directory_iterator(); it.increment(ec)) {
    if (ec) break;
    if (!it->is_regular_file(ec) || it->path().extension() != ".bsp") continue;
    std::string stem = lower(it->path().stem().string());
    bool keep = false; for (auto& w : want) if (w == stem) { keep = true; break; }
    if (keep) continue;
    std::error_code rec; fs::remove(it->path(), rec);
    if (!rec) Msg("content: removed the stale map %s\n", it->path().filename().string().c_str());
  }
}
void PruneStaleMaps() { for (const char* d : { "content/extras/all/maps", "content/extras/csgo_maps/maps" }) PruneMapsIn(d); }

bool WriteFileAtomic(const fs::path& path, const std::vector<uint8_t>& data, std::string& err) {
  std::error_code ec; fs::create_directories(path.parent_path(), ec);
  std::string tmp = path.string() + ".part";
  { std::ofstream o(tmp, std::ios::binary); if (!o) { err = "cannot write " + path.string(); return false; } o.write((const char*)data.data(), (std::streamsize)data.size()); if (!o) { err = "disk full?"; return false; } }
  fs::rename(tmp, path, ec);
  if (ec) { err = "cannot write " + path.string(); return false; }
  return true;
}

// https://raw.githubusercontent.com/<owner>/<repo>/<ref>/<path>  ->  https://cdn.jsdelivr.net/gh/<owner>/<repo>@<ref>/<path>
std::string JsDelivrUrl(const std::string& rawUrl) {
  const std::string kRaw = "https://raw.githubusercontent.com/";
  if (rawUrl.rfind(kRaw, 0) != 0) return std::string();
  std::string rest = rawUrl.substr(kRaw.size());
  size_t s1 = rest.find('/'); if (s1 == std::string::npos) return std::string();
  size_t s2 = rest.find('/', s1 + 1); if (s2 == std::string::npos) return std::string();
  size_t s3 = rest.find('/', s2 + 1); if (s3 == std::string::npos) return std::string();
  return "https://cdn.jsdelivr.net/gh/" + rest.substr(0, s2) + "@" + rest.substr(s2 + 1, s3 - s2 - 1) + rest.substr(s3);
}

// single-file pack: every file from <url><file>, falling back to the API template
// s_weaponFiles holds two families: the CS:GO models / sounds of the ARC9 port and the CS:S view models (the ones
// that are drawn with hands) that live in the CS:S content repository
bool IsCsgoWeaponAsset(const char* f) {
  for (const char* pre : { "models/weapons/csgo/", "materials/models/csgo/", "materials/models/weapons/v_models/csgo/", "sound/weapons/csgo/" })
    if (std::strncmp(f, pre, std::strlen(pre)) == 0) return true;
  return false;
}

bool DownloadFiles(const Pack& p, double doneMB, int filesBefore, std::string& err) {
  size_t bytes = 0; int files = 0, failed = 0, failStreak = 0;
  for (const char* const* f = p.files; *f; f++) {
    if (s_cancel) return false;
    const bool absolute = std::strncmp(*f, "http://", 7) == 0 || std::strncmp(*f, "https://", 8) == 0;
    std::string name = *f;
    if (absolute) {
      // https://raw.githubusercontent.com/<owner>/<repo>/<ref>/<path> -> <path>
      const std::string kRaw = "https://raw.githubusercontent.com/";
      name = name.rfind(kRaw, 0) == 0 ? name.substr(kRaw.size()) : name;
      size_t a = name.find('/'), b = a == std::string::npos ? a : name.find('/', a + 1);
      size_t c = b == std::string::npos ? b : name.find('/', b + 1);
      if (c != std::string::npos) name = name.substr(c + 1); else name = name.substr(name.rfind('/') + 1);
      bool bsp = name.size() > 4 && name.compare(name.size() - 4, 4, ".bsp") == 0;
      if (bsp && name.rfind("maps/", 0) != 0) name = "maps/" + name.substr(name.rfind('/') + 1);
    }
    fs::path dst = fs::path(p.dest) / name;
    std::error_code ec;
    if (fs::exists(dst, ec) && fs::file_size(dst, ec) > 0) { files++; continue; }   // an earlier interrupted run
    std::vector<uint8_t> data; std::string e1, e2;
    auto sink = [&](const uint8_t* d, size_t n) {
      if (s_cancel) return false;
      data.insert(data.end(), d, d + n); bytes += n;
      std::lock_guard<std::mutex> l(s_mtx);
      s_prog.packMB = bytes / 1048576.0; s_prog.totalMB = doneMB + s_prog.packMB; s_prog.current = *f;
      return true;
    };
    // raw.githubusercontent.com/<owner>/<repo>/<ref>/<path>  ->  api.github.com/repos/<owner>/<repo>/contents/<path>
    std::string apiUrl;
    if (absolute) {
      const std::string kRaw = "https://raw.githubusercontent.com/";
      if (std::string(*f).rfind(kRaw, 0) == 0) {
        std::string rest = std::string(*f).substr(kRaw.size());
        size_t s1 = rest.find('/'); size_t s2 = s1 == std::string::npos ? s1 : rest.find('/', s1 + 1);
        size_t s3 = s2 == std::string::npos ? s2 : rest.find('/', s2 + 1);
        if (s3 != std::string::npos)
          apiUrl = "https://api.github.com/repos/" + rest.substr(0, s2) + "/contents/" + rest.substr(s3 + 1) + "?ref=HEAD";
      }
    }
    const bool cssAsset = p.files == s_weaponFiles && !IsCsgoWeaponAsset(*f);
    const std::string rawUrl = absolute ? std::string(*f) : (cssAsset ? cl_content_url_base.GetString() : std::string(p.url)) + *f;
    // mirrors, in order: the jsDelivr CDN (no rate limit and reachable where raw.githubusercontent.com is blocked),
    // raw.githubusercontent.com, then the GitHub contents API
    std::vector<std::string> cands;
    { std::string jd = JsDelivrUrl(rawUrl); if (!jd.empty()) cands.push_back(jd); }
    cands.push_back(rawUrl);
    {
      char url[1024];
      if (!apiUrl.empty()) cands.push_back(apiUrl);
      else if (!absolute) { snprintf(url, sizeof(url), p.api ? p.api : cl_content_url_r8_api.GetString(), *f); cands.push_back(url); }
    }
    bool ok = false;
    for (size_t ci = 0; ci < cands.size() && !ok && !s_cancel; ci++) {
      if (ci) data.clear();
      if (cands[ci].rfind("https://api.github.com/", 0) == 0) ok = HttpGet(cands[ci], sink, e2, "application/vnd.github.raw");
      else ok = HttpGet(cands[ci], sink, e1);
    }
    if (!ok || data.empty()) {
      // one dead mirror must not kill the whole pack: remember it, keep going, retry on the next start
      if (err.empty()) err = std::string(*f) + ": " + (e2.empty() ? e1 : e2);
      failed++; failStreak++;
      // no mirror answers at all: stop early with a clear reason instead of grinding through thousands of files
      if (failStreak >= 16 && files == 0) {
        s_prog.error = "GitHub is not reachable from this network (" + (e2.empty() ? e1 : e2) + ")";
        return false;
      }
      if (failed > 64 && failed > files) { s_prog.error = "GitHub keeps dropping the connection (" + (e2.empty() ? e1 : e2) + ")"; return false; }
      continue;
    }
    failStreak = 0;
    if (!WriteFileAtomic(dst, data, err)) return false;
    files++;
    std::lock_guard<std::mutex> l(s_mtx); s_prog.files = filesBefore + files;
  }
  return failed == 0;
}

// a whole pack as one release-asset zip: a single HTTP request instead of thousands of single files
bool DownloadZipPack(const Pack& p, const std::string& url, double doneMB, std::string& err) {
  ZipStream zip(p.dest, false);
  size_t bytes = 0;
  auto tl = std::chrono::steady_clock::now();
  auto sink = [&](const uint8_t* d, size_t n) {
    if (s_cancel) return false;
    bytes += n;
    auto now = std::chrono::steady_clock::now();
    double body = s_httpTotal.load();
    if (body > 0) { std::lock_guard<std::mutex> l(s_mtx); s_prog.packTotalMB = body / 1048576.0; s_prog.totalExpectedMB = doneMB + s_prog.packTotalMB; }
    if (std::chrono::duration<double>(now - tl).count() >= 0.25) {
      tl = now;
      std::lock_guard<std::mutex> l(s_mtx);
      s_prog.packMB = bytes / 1048576.0; s_prog.totalMB = doneMB + s_prog.packMB;
    }
    return zip.Feed(d, n);
  };
  { std::lock_guard<std::mutex> l(s_mtx); s_prog.current = url; }
  Msg("content: downloading the archive %s\n", url.c_str());
  if (!HttpGet(url, sink, err)) return false;
  if (s_cancel) return false;
  if (!zip.Ended()) { err = "truncated archive"; return false; }
  if (zip.files <= 0) { err = zip.Error().empty() ? "empty archive" : zip.Error(); return false; }
  { std::lock_guard<std::mutex> l(s_mtx); s_prog.files += zip.files; s_prog.packMB = bytes / 1048576.0; s_prog.totalMB = doneMB + s_prog.packMB; }
  Msg("content: %s: %d files unpacked from the archive\n", p.title, zip.files);
  return true;
}

void Worker() {
  PruneStaleMaps();
  // the mirror of every single-file pack, matched by pack id (the pack list order may change)
  const struct { const char* id; ConVar* url; ConVar* api; } kUrls[] = {
    { "maps_go", &cl_content_url_maps_go, &cl_content_url_maps_go_api },
    { "mapstex", &cl_content_url_maps_go, &cl_content_url_maps_go_api },
    { "base", &cl_content_url_base, &cl_content_url_base_api },
    { "weapons", &cl_content_url_csgo, &cl_content_url_csgo_api },
    { "r8", &cl_content_url_r8, &cl_content_url_r8_api },
    { "csgo", &cl_content_url_csgo, &cl_content_url_csgo_api },
    { "icons", &cl_content_url_icons, &cl_content_url_icons_api },
    { "agents", &cl_content_url_maps_go, &cl_content_url_maps_go_api },
    { "props", &cl_content_url_maps_go, &cl_content_url_maps_go_api },
  };
  for (auto& p : s_packs) {
    p.url = cl_content_url_base.GetString(); p.api = cl_content_url_base_api.GetString();
    for (auto& u : kUrls) if (std::strncmp(p.id, u.id, std::strlen(u.id)) == 0) { p.url = u.url->GetString(); p.api = u.api->GetString(); break; }
  }
  double expected = 0; for (auto& p : s_packs) if (PackWanted(p)) expected += p.approxMB;
  { std::lock_guard<std::mutex> l(s_mtx); s_prog = Progress(); s_prog.state = RUNNING; s_prog.packCount = kPacks; s_prog.totalExpectedMB = expected; }
  std::error_code ec;
  if (s_needCore) fs::create_directories(fs::path(kDest) / "cstrike", ec);
  double doneMB = 0;
  bool ok = true;
  for (int i = 0; i < kPacks && ok; i++) {
    const Pack& p = s_packs[i];
    if (!PackWanted(p)) continue;
    { std::lock_guard<std::mutex> l(s_mtx); s_prog.pack = i; s_prog.packMB = 0; s_prog.packTotalMB = p.approxMB; }
    if (p.zipAsset) {
      std::string zerr;
      const std::string zipUrl = std::string(cl_content_url_packs.GetString()) + p.zipAsset;
      if (DownloadZipPack(p, zipUrl, doneMB, zerr)) {
        fs::create_directories(kExtras, ec);
        std::ofstream(PackMarker(p)) << kContentVersion << "\n";
        { std::lock_guard<std::mutex> l(s_mtx); doneMB += s_prog.packMB; }
        continue;
      }
      if (s_cancel) { ok = false; break; }
      Msg("content: %s: archive not available (%s), downloading single files instead\n", p.title, zerr.c_str());
      { std::lock_guard<std::mutex> l(s_mtx); s_prog.packMB = 0; s_prog.totalMB = doneMB; }
    }
    if (p.files) {
      int filesBefore; { std::lock_guard<std::mutex> l(s_mtx); filesBefore = s_prog.files; }
      std::string err;
      if (!DownloadFiles(p, doneMB, filesBefore, err)) {
        Msg("content: %s: %s (kept for the next try)\n", p.title, err.c_str());
        if (s_cancel) { ok = false; break; }
        if (p.core) { SetError(err); ok = false; break; }
        SetError(err);
        continue;                      // no marker: the missing files are retried next time, the rest continues
      }
      fs::create_directories(kExtras, ec);
      std::ofstream(PackMarker(p)) << kContentVersion << "\n";
      { std::lock_guard<std::mutex> l(s_mtx); doneMB += s_prog.packMB; }
      continue;
    }
    ZipStream zip(kDest);
    int filesBefore; { std::lock_guard<std::mutex> l(s_mtx); filesBefore = s_prog.files; }
    size_t bytes = 0;
    auto t0 = std::chrono::steady_clock::now(); auto tl = t0; size_t bl = 0;
    std::string err;
    bool got = HttpGet(p.url, [&](const uint8_t* d, size_t n) {
      if (s_cancel) return false;
      bytes += n;
      if (!zip.Feed(d, n)) return false;
      auto now = std::chrono::steady_clock::now();
      double dt = std::chrono::duration<double>(now - tl).count();
      std::lock_guard<std::mutex> l(s_mtx);
      s_prog.packMB = bytes / 1048576.0; s_prog.totalMB = doneMB + s_prog.packMB;
      s_prog.files = filesBefore + zip.files; s_prog.current = zip.lastName;
      if (dt > 0.5) { s_prog.speedMBs = (bytes - bl) / 1048576.0 / dt; tl = now; bl = bytes; }
      return true;
    }, err);
    { std::lock_guard<std::mutex> l(s_mtx); s_prog.files = filesBefore + zip.files; }
    if (s_cancel) { ok = false; break; }
    if (!zip.Error().empty()) { SetError(zip.Error()); ok = false; break; }
    if (!got) { SetError(err.empty() ? "download failed" : err); ok = false; break; }
    if (zip.files == 0) { SetError("the archive was empty"); ok = false; break; }
    std::ofstream(PackMarker(p)) << kContentVersion << "\n";
    doneMB += bytes / 1048576.0;
  }
  {
    std::lock_guard<std::mutex> l(s_mtx);
    s_prog.state = s_cancel ? CANCELLED : ok ? DONE : FAILED;
  }
  s_busy = false;
}
} // namespace

const Pack* Packs(int& count) { count = kPacks; return s_packs; }

bool CoreContentMissing() { return !srcfs::CsgoAvailable() && !srcfs::Available(); }   // no real game content at all
bool ContentMissing() {
  for (auto& p : s_packs) if (!PackDone(p)) return true;
  if (CoreContentMissing() && !fs::exists("content/.no_content_ok")) return true;   // tell the player once how to get CS:GO
  return false;
}
void DismissCoreHint() { std::error_code ec; fs::create_directories("content", ec); std::ofstream("content/.no_content_ok") << "ok\n"; }

void Start() {
  if (s_busy) return;
  if (s_thread.joinable()) s_thread.join();
  s_haveCsgo = srcfs::CsgoAvailable();
  s_needCore = CoreContentMissing();
  s_cancel = false; s_busy = true;
  { std::lock_guard<std::mutex> l(s_mtx); s_prog.state = RUNNING; s_prog.error.clear(); }
  s_thread = std::thread(Worker);
}
void Cancel() { s_cancel = true; }
bool Busy() { return s_busy; }
void Shutdown() {
  if (s_busy) {
    s_cancel = true;
    for (int i = 0; i < 100 && s_busy; i++) std::this_thread::sleep_for(std::chrono::milliseconds(50));   // up to 5 s
  }
  if (s_thread.joinable()) { if (s_busy) s_thread.detach(); else s_thread.join(); }
}
Progress Get() { std::lock_guard<std::mutex> l(s_mtx); return s_prog; }
} // namespace download

static ConCommand cmd_content_download("content_download", [](const CCommand&) {
  download::Start(); Msg("Downloading the CS:GO maps and the base Source assets (textures, models, sounds) ...\n");
}, "Download the CS:GO maps and the base Source assets (textures, models, sounds) from GitHub");
