// convar.hpp - ConVar / ConCommand / command buffer (Cbuf) and console output (Msg/Warning/ConColorMsg)
#pragma once
#include <string>
#include <vector>
#include <functional>
#include <map>
#include <cstdint>

enum {
  FCVAR_NONE = 0,
  FCVAR_CHEAT = 1 << 0,      // requires sv_cheats 1
  FCVAR_ARCHIVE = 1 << 1,    // saved to config.cfg
  FCVAR_REPLICATED = 1 << 2,
  FCVAR_NOTIFY = 1 << 3,
  FCVAR_HIDDEN = 1 << 4,
  FCVAR_GAMEDLL = 1 << 5,
  FCVAR_CLIENTDLL = 1 << 6,
};

struct Color32 { uint8_t r = 255, g = 255, b = 255, a = 255; };

class CCommand {
public:
  explicit CCommand(const std::string& line = "");
  int ArgC() const { return (int)m_args.size(); }
  const char* Arg(int i) const { return i < (int)m_args.size() ? m_args[i].c_str() : ""; }
  const char* operator[](int i) const { return Arg(i); }
  const std::string& ArgS() const { return m_argS; } // everything after the command name
  float ArgF(int i, float def = 0.f) const;
  int ArgI(int i, int def = 0) const;
private:
  std::vector<std::string> m_args; std::string m_argS;
};

class ConCommandBase {
public:
  ConCommandBase(const char* name, const char* help, int flags);
  virtual ~ConCommandBase() = default;
  virtual bool IsCommand() const = 0;
  const char* GetName() const { return m_name; }
  const char* GetHelpText() const { return m_help; }
  int GetFlags() const { return m_flags; }
  bool IsFlagSet(int f) const { return (m_flags & f) != 0; }
protected:
  const char* m_name; const char* m_help; int m_flags;
};

class ConVar : public ConCommandBase {
public:
  using ChangeCallback = void (*)(ConVar* var, const char* oldValue);
  ConVar(const char* name, const char* def, int flags = 0, const char* help = "", bool hasMin = false, float fMin = 0, bool hasMax = false, float fMax = 0, ChangeCallback cb = nullptr);
  bool IsCommand() const override { return false; }
  float GetFloat() const { return m_fValue; }
  int GetInt() const { return m_nValue; }
  bool GetBool() const { return m_nValue != 0; }
  const char* GetString() const { return m_sValue.c_str(); }
  const char* GetDefault() const { return m_default; }
  void SetValue(const char* v);
  void SetValue(float v);
  void SetValue(int v);
  void Revert() { SetValue(m_default); }
  bool HasMin() const { return m_hasMin; } bool HasMax() const { return m_hasMax; }
  float GetMin() const { return m_fMin; } float GetMax() const { return m_fMax; }
private:
  const char* m_default; std::string m_sValue; float m_fValue = 0; int m_nValue = 0;
  bool m_hasMin, m_hasMax; float m_fMin, m_fMax; ChangeCallback m_cb;
};

class ConCommand : public ConCommandBase {
public:
  using Callback = std::function<void(const CCommand&)>;
  ConCommand(const char* name, Callback cb, const char* help = "", int flags = 0);
  bool IsCommand() const override { return true; }
  void Dispatch(const CCommand& c) { if (m_cb) m_cb(c); }
private:
  Callback m_cb;
};

// registry (ICvar)
class CCvar {
public:
  static CCvar& Get();
  void Register(ConCommandBase* c);
  ConCommandBase* Find(const char* name);
  ConVar* FindVar(const char* name);
  const std::map<std::string, ConCommandBase*>& All() const { return m_cmds; }
  std::vector<std::string> Complete(const std::string& partial, int max = 32);
private:
  std::map<std::string, ConCommandBase*> m_cmds;
};
inline CCvar* g_pCVar = nullptr; // set in engine init
ConVar* FindConVar(const char* name);

// console output
struct ConsoleLine { std::string text; Color32 color; double time; };
void Msg(const char* fmt, ...);
void DevMsg(const char* fmt, ...);
void Warning(const char* fmt, ...);
void ConColorMsg(const Color32& c, const char* fmt, ...);
std::vector<ConsoleLine>& ConsoleHistory();
// notify area (top-left developer lines)
std::vector<ConsoleLine>& ConsoleNotify();

// command buffer
void Cbuf_AddText(const std::string& text);
void Cbuf_InsertText(const std::string& text);
void Cbuf_Execute();                 // runs until empty or a 'wait'
void Cmd_ExecuteString(const std::string& line);
bool Cmd_ExecFile(const std::string& path, bool silent = false);
void Host_WriteConfig(const std::string& path);
bool CheatsAllowed();
