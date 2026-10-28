#if defined(PLATFORM_ANDROID)
#include <android/log.h>
#endif
#include "convar.hpp"
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <fstream>
#include <sstream>
#include <chrono>
#include <cctype>
#include <utility>

static double Plat_Time() {
  using namespace std::chrono;
  static auto t0 = steady_clock::now();
  return duration<double>(steady_clock::now() - t0).count();
}

//------------------------------------------------------------------------------ CCommand
CCommand::CCommand(const std::string& line) {
  size_t i = 0, n = line.size();
  bool first = true;
  while (i < n) {
    while (i < n && (line[i] == ' ' || line[i] == '\t')) i++;
    if (i >= n) break;
    if (!first && m_argS.empty()) m_argS = line.substr(i);
    std::string tok;
    if (line[i] == '"') { i++; while (i < n && line[i] != '"') tok += line[i++]; if (i < n) i++; }
    else { while (i < n && line[i] != ' ' && line[i] != '\t') tok += line[i++]; }
    m_args.push_back(tok);
    first = false;
  }
  // strip surrounding quotes of argS when it is a single quoted token
  if (m_argS.size() >= 2 && m_argS.front() == '"' && m_argS.back() == '"' && m_args.size() == 2) m_argS = m_args[1];
}
float CCommand::ArgF(int i, float def) const { return i < ArgC() ? (float)atof(Arg(i)) : def; }
int CCommand::ArgI(int i, int def) const { return i < ArgC() ? atoi(Arg(i)) : def; }

//------------------------------------------------------------------------------ registry
CCvar& CCvar::Get() { static CCvar s; return s; }
void CCvar::Register(ConCommandBase* c) { m_cmds[c->GetName()] = c; }
ConCommandBase* CCvar::Find(const char* name) {
  std::string s(name); for (auto& ch : s) ch = (char)tolower(ch);
  auto it = m_cmds.find(s); return it == m_cmds.end() ? nullptr : it->second;
}
ConVar* CCvar::FindVar(const char* name) { auto* c = Find(name); return (c && !c->IsCommand()) ? static_cast<ConVar*>(c) : nullptr; }
std::vector<std::string> CCvar::Complete(const std::string& partial, int max) {
  std::vector<std::string> out;
  for (auto& [k, v] : m_cmds) {
    if (v->IsFlagSet(FCVAR_HIDDEN)) continue;
    if (k.compare(0, partial.size(), partial) == 0) { out.push_back(k); if ((int)out.size() >= max) break; }
  }
  return out;
}
ConVar* FindConVar(const char* name) { return CCvar::Get().FindVar(name); }

ConCommandBase::ConCommandBase(const char* name, const char* help, int flags) : m_name(name), m_help(help), m_flags(flags) { CCvar::Get().Register(this); }

ConVar::ConVar(const char* name, const char* def, int flags, const char* help, bool hasMin, float fMin, bool hasMax, float fMax, ChangeCallback cb)
  : ConCommandBase(name, help, flags), m_default(def), m_hasMin(hasMin), m_hasMax(hasMax), m_fMin(fMin), m_fMax(fMax), m_cb(nullptr) {
  SetValue(def); m_cb = cb;
}
void ConVar::SetValue(const char* v) {
  std::string old = m_sValue;
  float f = (float)atof(v);
  bool numeric = v && (isdigit((unsigned char)v[0]) || v[0] == '-' || v[0] == '.');
  if (numeric) {
    if (m_hasMin && f < m_fMin) f = m_fMin;
    if (m_hasMax && f > m_fMax) f = m_fMax;
    if ((m_hasMin && atof(v) < m_fMin) || (m_hasMax && atof(v) > m_fMax)) {
      char buf[64]; snprintf(buf, sizeof buf, "%g", f); m_sValue = buf;
    } else m_sValue = v;
  } else m_sValue = v ? v : "";
  m_fValue = f; m_nValue = (int)f;
  if (m_cb && old != m_sValue) m_cb(this, old.c_str());
}
void ConVar::SetValue(float v) { char buf[64]; snprintf(buf, sizeof buf, "%g", v); SetValue(buf); }
void ConVar::SetValue(int v) { char buf[64]; snprintf(buf, sizeof buf, "%d", v); SetValue(buf); }

ConCommand::ConCommand(const char* name, Callback cb, const char* help, int flags) : ConCommandBase(name, help, flags), m_cb(std::move(cb)) {}

//------------------------------------------------------------------------------ output
std::vector<ConsoleLine>& ConsoleHistory() { static std::vector<ConsoleLine> v; return v; }
std::vector<ConsoleLine>& ConsoleNotify() { static std::vector<ConsoleLine> v; return v; }
static void PushLine(const Color32& c, const char* text, bool notify) {
#if defined(PLATFORM_ANDROID)
  __android_log_write(ANDROID_LOG_INFO, "cshvh", text);   // console mirrored to logcat (adb logcat -s cshvh)
#endif
  auto& h = ConsoleHistory();
  // split by newline; append to last line if it did not end with newline
  static bool lastOpen = false;
  const char* p = text;
  while (*p) {
    const char* e = strchr(p, '\n');
    std::string seg = e ? std::string(p, e - p) : std::string(p);
    if (lastOpen && !h.empty()) h.back().text += seg; else h.push_back({ seg, c, Plat_Time() });
    if (notify && (!seg.empty())) { ConsoleNotify().push_back({ seg, c, Plat_Time() }); if (ConsoleNotify().size() > 8) ConsoleNotify().erase(ConsoleNotify().begin()); }
    lastOpen = (e == nullptr);
    if (!e) break; p = e + 1;
  }
  if (h.size() > 2000) h.erase(h.begin(), h.begin() + 500);
  fputs(text, stdout);
}
static ConVar developer("developer", "0", FCVAR_NONE, "Show developer messages");
static ConVar con_notifytime("con_notifytime", "8", FCVAR_ARCHIVE, "How long to display recent console text to the upper part of the game window");
#define FMT_BUF char buf[4096]; va_list ap; va_start(ap, fmt); vsnprintf(buf, sizeof buf, fmt, ap); va_end(ap);
void Msg(const char* fmt, ...) { FMT_BUF PushLine({ 216, 222, 211, 255 }, buf, false); }
void DevMsg(const char* fmt, ...) { FMT_BUF if (developer.GetInt()) PushLine({ 180, 180, 180, 255 }, buf, true); else fputs(buf, stdout); }
void Warning(const char* fmt, ...) { FMT_BUF PushLine({ 255, 100, 100, 255 }, buf, developer.GetInt() > 0); }
void ConColorMsg(const Color32& c, const char* fmt, ...) { FMT_BUF PushLine(c, buf, false); }

//------------------------------------------------------------------------------ Cbuf
static std::deque<std::string> s_cbuf;
static int s_waitFrames = 0;
static void SplitCommands(const std::string& text, std::vector<std::string>& out) {
  std::string cur; bool quote = false;
  for (size_t i = 0; i < text.size(); i++) {
    char c = text[i];
    if (c == '"') quote = !quote;
    // "//" starts a comment only at the start of a token, so urls (https://...) survive
    if (!quote && c == '/' && i + 1 < text.size() && text[i + 1] == '/' && (cur.empty() || cur.back() == ' ' || cur.back() == '\t')) {
      while (i < text.size() && text[i] != '\n') i++; c = '\n';
    }
    if (!quote && (c == ';' || c == '\n' || c == '\r')) { if (!cur.empty()) out.push_back(cur); cur.clear(); continue; }
    cur += c;
  }
  if (!cur.empty()) out.push_back(cur);
}
void Cbuf_AddText(const std::string& text) { std::vector<std::string> v; SplitCommands(text, v); for (auto& s : v) s_cbuf.push_back(s); }
void Cbuf_InsertText(const std::string& text) { std::vector<std::string> v; SplitCommands(text, v); for (auto it = v.rbegin(); it != v.rend(); ++it) s_cbuf.push_front(*it); }
void Cbuf_Execute() {
  if (s_waitFrames > 0) { s_waitFrames--; return; }
  int guard = 0;
  while (!s_cbuf.empty() && guard++ < 4096) {
    std::string line = s_cbuf.front(); s_cbuf.pop_front();
    CCommand c(line);
    if (c.ArgC() && !strcmp(c[0], "wait")) { s_waitFrames = c.ArgC() > 1 ? std::max(0, c.ArgI(1) - 1) : 0; return; }
    Cmd_ExecuteString(line);
  }
}
void Cmd_ExecuteString(const std::string& line) {
  CCommand c(line);
  if (!c.ArgC()) return;
  ConCommandBase* base = CCvar::Get().Find(c[0]);
  if (!base) { Msg("Unknown command \"%s\"\n", c[0]); return; }
  if (base->IsFlagSet(FCVAR_CHEAT) && !CheatsAllowed()) {
    if (base->IsCommand() || c.ArgC() > 1) { Msg("Can't use cheat %s %s unless sv_cheats 1.\n", base->IsCommand() ? "command" : "cvar", c[0]); return; }
  }
  if (base->IsCommand()) { static_cast<ConCommand*>(base)->Dispatch(c); return; }
  ConVar* v = static_cast<ConVar*>(base);
  if (c.ArgC() == 1) {
    Msg("\"%s\" = \"%s\" ( def. \"%s\" )%s%s\n", v->GetName(), v->GetString(), v->GetDefault(),
        v->IsFlagSet(FCVAR_CHEAT) ? " cheat" : "", v->IsFlagSet(FCVAR_ARCHIVE) ? " archive" : "");
    if (v->GetHelpText() && *v->GetHelpText()) Msg(" - %s\n", v->GetHelpText());
    return;
  }
  v->SetValue(c.ArgS().c_str());
}
bool Cmd_ExecFile(const std::string& path, bool silent) {
  std::ifstream f(path);
  if (!f) { if (!silent) Msg("exec: couldn't exec %s\n", path.c_str()); return false; }
  std::stringstream ss; ss << f.rdbuf();
  if (!silent) Msg("execing %s\n", path.c_str());
  Cbuf_InsertText(ss.str());
  return true;
}
void Host_WriteConfig(const std::string& path) {
  std::ofstream f(path);
  if (!f) return;
  f << "// generated by Counter-Strike: HvH, do not modify\n";
  for (auto& [k, v] : CCvar::Get().All()) {
    if (v->IsCommand() || !v->IsFlagSet(FCVAR_ARCHIVE)) continue;
    f << k << " \"" << static_cast<ConVar*>(v)->GetString() << "\"\n";
  }
}

//------------------------------------------------------------------------------ builtin commands
static ConCommand cmd_echo("echo", [](const CCommand& c) { Msg("%s\n", c.ArgS().c_str()); }, "Echo text to console.");
static ConCommand cmd_exec("exec", [](const CCommand& c) {
  if (c.ArgC() < 2) { Msg("exec <filename>: execute a script file\n"); return; }
  std::string p = c[1]; if (p.find(".cfg") == std::string::npos) p += ".cfg";
  if (!Cmd_ExecFile("cfg/" + p, true) && !Cmd_ExecFile(p)) {}
}, "Execute script file.");
static ConCommand cmd_find("find", [](const CCommand& c) {
  if (c.ArgC() < 2) { Msg("Usage: find <string>\n"); return; }
  std::string s = c[1];
  for (auto& [k, v] : CCvar::Get().All()) {
    std::string h = v->GetHelpText() ? v->GetHelpText() : "";
    if (k.find(s) != std::string::npos || h.find(s) != std::string::npos) Msg("\"%s\" - %s\n", k.c_str(), h.c_str());
  }
}, "Find concommands with the specified string in their name/help text.");
static ConCommand cmd_cvarlist("cvarlist", [](const CCommand&) {
  int n = 0;
  for (auto& [k, v] : CCvar::Get().All()) {
    if (v->IsFlagSet(FCVAR_HIDDEN)) continue;
    if (v->IsCommand()) Msg("%-36s : cmd   : %s\n", k.c_str(), v->GetHelpText());
    else Msg("%-36s : %-8s : %s\n", k.c_str(), static_cast<ConVar*>(v)->GetString(), v->GetHelpText());
    n++;
  }
  Msg("--------------\n%d total convars/concommands\n", n);
}, "Show the list of convars/concommands.");
static ConCommand cmd_toggle("toggle", [](const CCommand& c) {
  ConVar* v = c.ArgC() > 1 ? FindConVar(c[1]) : nullptr; if (!v) { Msg("toggle <cvar> [v1 v2...]\n"); return; }
  if (c.ArgC() <= 3) { v->SetValue(v->GetBool() ? 0 : 1); return; }
  int i = 2; for (; i < c.ArgC(); i++) if (!strcmp(v->GetString(), c[i])) break;
  v->SetValue(c[(i + 1 < c.ArgC()) ? i + 1 : 2]);
}, "Toggles a convar on or off, or cycles through a set of values.");
static ConCommand cmd_incrementvar("incrementvar", [](const CCommand& c) {
  if (c.ArgC() < 5) { Msg("incrementvar <cvar> <min> <max> <delta>\n"); return; }
  ConVar* v = FindConVar(c[1]); if (!v) return;
  float lo = c.ArgF(2), hi = c.ArgF(3), d = c.ArgF(4), x = v->GetFloat() + d;
  if (x > hi) x = lo; if (x < lo) x = hi; v->SetValue(x);
}, "Increment specified convar value.");
static ConCommand cmd_clear("clear", [](const CCommand&) { ConsoleHistory().clear(); }, "Clear all console output.");
static ConCommand cmd_help("help", [](const CCommand& c) {
  if (c.ArgC() < 2) { Msg("Usage: help <cvarname>\n"); return; }
  auto* b = CCvar::Get().Find(c[1]); if (!b) { Msg("help: no cvar or command named %s\n", c[1]); return; }
  Msg("\"%s\" - %s\n", b->GetName(), b->GetHelpText());
}, "Find help about a convar/concommand.");
