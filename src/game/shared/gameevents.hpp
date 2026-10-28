// gameevents.hpp - IGameEvent / CGameEventManager (player_death, player_hurt, weapon_fire, bullet_impact, round_*...)
#pragma once
#include <string>
#include <unordered_map>
#include <vector>
#include <functional>
#include <cstdlib>
#include <utility>

class IGameEvent {
public:
  explicit IGameEvent(const char* name) : m_name(name) {}
  const char* GetName() const { return m_name.c_str(); }
  bool IsEvent(const char* n) const { return m_name == n; }
  int GetInt(const char* k, int def = 0) const { auto it = m_kv.find(k); return it == m_kv.end() ? def : std::atoi(it->second.c_str()); }
  float GetFloat(const char* k, float def = 0) const { auto it = m_kv.find(k); return it == m_kv.end() ? def : (float)std::atof(it->second.c_str()); }
  bool GetBool(const char* k, bool def = false) const { return GetInt(k, def ? 1 : 0) != 0; }
  const char* GetString(const char* k, const char* def = "") const { auto it = m_kv.find(k); return it == m_kv.end() ? def : it->second.c_str(); }
  IGameEvent& SetInt(const char* k, int v) { m_kv[k] = std::to_string(v); return *this; }
  IGameEvent& SetFloat(const char* k, float v) { m_kv[k] = std::to_string(v); return *this; }
  IGameEvent& SetBool(const char* k, bool v) { m_kv[k] = v ? "1" : "0"; return *this; }
  IGameEvent& SetString(const char* k, const std::string& v) { m_kv[k] = v; return *this; }
private:
  std::string m_name;
  std::unordered_map<std::string, std::string> m_kv;
};

class CGameEventManager {
public:
  using Listener = std::function<void(const IGameEvent&)>;
  int AddListener(const char* name, Listener fn) { m_listeners.push_back({ name ? name : "", std::move(fn), ++m_nextId }); return m_nextId; }
  void RemoveListener(int id) { for (size_t i = 0; i < m_listeners.size(); i++) if (m_listeners[i].id == id) { m_listeners.erase(m_listeners.begin() + i); return; } }
  void FireEvent(const IGameEvent& ev) {
    // copy: listeners may add listeners
    auto ls = m_listeners;
    for (auto& l : ls) if (l.name.empty() || l.name == ev.GetName()) l.fn(ev);
  }
private:
  struct Entry { std::string name; Listener fn; int id; };
  std::vector<Entry> m_listeners;
  int m_nextId = 0;
};
inline CGameEventManager g_GameEventManager;
inline CGameEventManager* gameeventmanager = &g_GameEventManager;
