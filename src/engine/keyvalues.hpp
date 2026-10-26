// keyvalues.hpp - Valve KeyValues text format (VMT materials, soundscripts, overview files, entity lumps).
//
//   "Root" { "key" "value"  $flag 1  "Sub" { ... } }   // comments, quotes optional, keys case-insensitive
#pragma once
#include <string>
#include <vector>
#include <memory>

struct KeyValues {
  std::string key, value;                    // value is empty for blocks
  std::vector<std::unique_ptr<KeyValues>> children;
  bool isBlock = false;

  // parse a whole buffer; the result is a synthetic root whose children are the top level entries
  static std::unique_ptr<KeyValues> Parse(const char* text, size_t len);
  static std::unique_ptr<KeyValues> Parse(const std::string& s) { return Parse(s.data(), s.size()); }

  const KeyValues* Find(const std::string& k) const;           // case-insensitive, direct children only
  KeyValues* Find(const std::string& k);
  const char* GetString(const std::string& k, const char* def = "") const;
  float GetFloat(const std::string& k, float def = 0) const;
  int GetInt(const std::string& k, int def = 0) const { return (int)GetFloat(k, (float)def); }
  KeyValues* AddChild(const std::string& k, const std::string& v, bool block);
  void MergeFrom(const KeyValues& other, bool overwrite);       // used by VMT "patch" materials
};

std::string KV_Lower(std::string s);
