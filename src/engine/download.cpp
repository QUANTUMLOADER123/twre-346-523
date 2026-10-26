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

ConVar cl_content_url_maps("cl_content_url_maps", "https://codeload.github.com/bouletmarc/css_maps_content/zip/refs/heads/master", FCVAR_ARCHIVE, "Zip archive with the CS:S maps (maps/*.bsp) for the first-start download");
ConVar cl_content_url_main("cl_content_url_main", "https://codeload.github.com/bouletmarc/css_content/zip/refs/heads/master", FCVAR_ARCHIVE, "Zip archive with the CS:S models / materials / sounds for the first-start download");

namespace download {
namespace {
const char* kDest = "content/css";

std::mutex s_mtx;
Progress s_prog;
std::atomic<bool> s_cancel{ false }, s_busy{ false };
std::thread s_thread;

Pack s_packs[2] = {
  { "maps", "CS:S maps", nullptr, 105 },
  { "main", "CS:S models, textures and sounds", nullptr, 700 },
};

void SetError(const std::string& e) { std::lock_guard<std::mutex> l(s_mtx); s_prog.error = e; }

// ---------------------------------------------------------------------------------------------
// streaming zip extractor: local file headers carry the sizes in codeload archives (no data descriptors)
class ZipStream {
public:
  explicit ZipStream(std::string dest) : m_dest(std::move(dest)) {}
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
  std::string m_dest, m_name, m_err;
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
    size_t slash = rel.find('/'); if (slash == std::string::npos) return true;
    rel = rel.substr(slash + 1);
    if (rel.empty() || rel.find("..") != std::string::npos) return true;
    std::string top = rel.substr(0, rel.find('/'));
    static const char* allowed[] = { "maps", "materials", "models", "sound", "particles", "scripts", "resource" };
    bool ok = false; for (const char* a : allowed) if (top == a) ok = true;
    if (!ok) return true;
    fs::path path = fs::path(m_dest) / "cstrike" / rel;
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
bool HttpGet(const std::string& url, const Sink& sink, std::string& err) {
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
    jint code = env->CallIntMethod(conn, env->GetMethodID(hc, "getResponseCode", "()I"));
    if (env->ExceptionCheck()) { err = JavaError(env); env->PopLocalFrame(nullptr); break; }
    if (code != 200) { err = "HTTP " + std::to_string(code); env->PopLocalFrame(nullptr); break; }
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
bool HttpGet(const std::string& url, const Sink& sink, std::string& err) {
  std::wstring w(url.begin(), url.end());
  URL_COMPONENTS uc{}; uc.dwStructSize = sizeof(uc);
  wchar_t host[256] = {}, path[2048] = {};
  uc.lpszHostName = host; uc.dwHostNameLength = 255; uc.lpszUrlPath = path; uc.dwUrlPathLength = 2047;
  if (!WinHttpCrackUrl(w.c_str(), 0, 0, &uc)) { err = "bad url"; return false; }
  HINTERNET s = WinHttpOpen(L"cshvh", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
  if (!s) { err = "WinHttpOpen failed"; return false; }
  WinHttpSetTimeouts(s, 20000, 20000, 60000, 60000);
  HINTERNET c = WinHttpConnect(s, host, uc.nPort, 0);
  HINTERNET r = c ? WinHttpOpenRequest(c, L"GET", path, nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, uc.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0) : nullptr;
  bool ok = false;
  if (r && WinHttpSendRequest(r, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0) && WinHttpReceiveResponse(r, nullptr)) {
    DWORD code = 0, len = sizeof(code);
    WinHttpQueryHeaders(r, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX, &code, &len, WINHTTP_NO_HEADER_INDEX);
    if (code != 200) err = "HTTP " + std::to_string(code);
    else {
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
bool HttpGet(const std::string& url, const Sink& sink, std::string& err) {
  std::string cmd = "curl -fsSL --connect-timeout 20 '" + url + "'";
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

bool PackDone(const Pack& p) { std::error_code ec; return fs::exists(fs::path(kDest) / (std::string(".pack_") + p.id), ec); }

void Worker() {
  s_packs[0].url = cl_content_url_maps.GetString();
  s_packs[1].url = cl_content_url_main.GetString();
  double expected = 0; for (auto& p : s_packs) if (!PackDone(p)) expected += p.approxMB;
  { std::lock_guard<std::mutex> l(s_mtx); s_prog = Progress(); s_prog.state = RUNNING; s_prog.packCount = 2; s_prog.totalExpectedMB = expected; }
  std::error_code ec; fs::create_directories(fs::path(kDest) / "cstrike", ec);
  double doneMB = 0;
  bool ok = true;
  for (int i = 0; i < 2 && ok; i++) {
    const Pack& p = s_packs[i];
    if (PackDone(p)) continue;
    { std::lock_guard<std::mutex> l(s_mtx); s_prog.pack = i; s_prog.packMB = 0; s_prog.packTotalMB = p.approxMB; }
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
    std::ofstream(fs::path(kDest) / (std::string(".pack_") + p.id)) << zip.files << "\n";
    doneMB += bytes / 1048576.0;
  }
  {
    std::lock_guard<std::mutex> l(s_mtx);
    s_prog.state = s_cancel ? CANCELLED : ok ? DONE : FAILED;
  }
  s_busy = false;
}
} // namespace

const Pack* Packs(int& count) { count = 2; return s_packs; }

bool ContentMissing() {
  if (!srcfs::Available()) return true;
  std::error_code ec;
  fs::path root = fs::path(srcfs::Root());
  if (fs::exists(root / "cstrike" / "cstrike_pak_dir.vpk", ec)) return false;          // Steam / SteamCMD install
  if (fs::weakly_canonical(root, ec) != fs::weakly_canonical(fs::path(kDest), ec)) return false;   // user folder
  for (auto& p : s_packs) if (!PackDone(p)) return true;                                 // our download, unfinished
  return false;
}

void Start() {
  if (s_busy) return;
  if (s_thread.joinable()) s_thread.join();
  s_cancel = false; s_busy = true;
  { std::lock_guard<std::mutex> l(s_mtx); s_prog.state = RUNNING; s_prog.error.clear(); }
  s_thread = std::thread(Worker);
}
void Cancel() { s_cancel = true; }
bool Busy() { return s_busy; }
void Shutdown() { if (s_busy) s_cancel = true; if (s_thread.joinable()) { if (s_busy) s_thread.detach(); else s_thread.join(); } }
Progress Get() { std::lock_guard<std::mutex> l(s_mtx); return s_prog; }
} // namespace download

static ConCommand cmd_content_download("content_download", [](const CCommand&) {
  download::Start(); Msg("Downloading CS:S content into content/css ...\n");
}, "Download the Counter-Strike: Source content (maps, models, textures, sounds) from GitHub");
