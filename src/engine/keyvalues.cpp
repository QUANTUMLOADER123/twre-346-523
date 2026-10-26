// keyvalues.cpp
#include "keyvalues.hpp"
#include <cctype>
#include <cstdlib>
#include <cstring>

std::string KV_Lower(std::string s) { for (char& c : s) c = (char)std::tolower((unsigned char)c); return s; }

namespace {
struct Lexer {
  const char* p; const char* e;
  // returns false at end; quoted tells whether the token was in quotes
  bool Next(std::string& tok, bool& quoted) {
    for (;;) {
      while (p < e && (unsigned char)*p <= ' ') p++;
      if (p >= e) return false;
      if (p + 1 < e && p[0] == '/' && p[1] == '/') { while (p < e && *p != '\n') p++; continue; }
      break;
    }
    quoted = false; tok.clear();
    if (*p == '{' || *p == '}') { tok = *p++; return true; }
    if (*p == '"') {
      quoted = true; p++;
      while (p < e && *p != '"') {
        if (*p == '\\' && p + 1 < e && (p[1] == '"' || p[1] == '\\')) { tok += p[1]; p += 2; continue; }
        tok += *p++;
      }
      if (p < e) p++;
      return true;
    }
    while (p < e && (unsigned char)*p > ' ' && *p != '{' && *p != '}' && *p != '"') {
      if (p + 1 < e && p[0] == '/' && p[1] == '/') break;
      tok += *p++;
    }
    return true;
  }
};

void ParseBlock(Lexer& lx, KeyValues& parent, int depth) {
  std::string tok, val; bool q = false, qv = false;
  while (lx.Next(tok, q)) {
    if (!q && tok == "}") return;
    if (!q && tok == "{") { // anonymous block
      KeyValues* c = parent.AddChild("", "", true);
      if (depth < 64) ParseBlock(lx, *c, depth + 1);
      continue;
    }
    std::string key = tok;
    if (!lx.Next(val, qv)) { parent.AddChild(key, "", false); return; }
    // conditionals like [$WIN32] after a value/key are skipped
    if (!qv && val.size() > 1 && val[0] == '[') { if (!lx.Next(val, qv)) return; }
    if (!qv && val == "{") {
      KeyValues* c = parent.AddChild(key, "", true);
      if (depth < 64) ParseBlock(lx, *c, depth + 1);
    } else if (!qv && val == "}") { parent.AddChild(key, "", false); return; }
    else {
      parent.AddChild(key, val, false);
      // optional trailing conditional: "key" "value" [$X360]
      const char* save = lx.p; std::string t2; bool q2;
      if (lx.Next(t2, q2) && !q2 && t2.size() > 1 && t2[0] == '[') {} else lx.p = save;
    }
  }
}
} // namespace

std::unique_ptr<KeyValues> KeyValues::Parse(const char* text, size_t len) {
  auto root = std::make_unique<KeyValues>(); root->isBlock = true;
  Lexer lx{ text, text + len };
  // skip UTF-8 BOM
  if (len >= 3 && (unsigned char)text[0] == 0xEF && (unsigned char)text[1] == 0xBB && (unsigned char)text[2] == 0xBF) lx.p += 3;
  ParseBlock(lx, *root, 0);
  return root;
}

static bool IEq(const std::string& a, const std::string& b) {
  if (a.size() != b.size()) return false;
  for (size_t i = 0; i < a.size(); i++) if (std::tolower((unsigned char)a[i]) != std::tolower((unsigned char)b[i])) return false;
  return true;
}
const KeyValues* KeyValues::Find(const std::string& k) const { for (auto& c : children) if (IEq(c->key, k)) return c.get(); return nullptr; }
KeyValues* KeyValues::Find(const std::string& k) { for (auto& c : children) if (IEq(c->key, k)) return c.get(); return nullptr; }
const char* KeyValues::GetString(const std::string& k, const char* def) const { const KeyValues* c = Find(k); return c && !c->isBlock ? c->value.c_str() : def; }
float KeyValues::GetFloat(const std::string& k, float def) const { const KeyValues* c = Find(k); return c && !c->isBlock && !c->value.empty() ? (float)std::atof(c->value.c_str()) : def; }
KeyValues* KeyValues::AddChild(const std::string& k, const std::string& v, bool block) {
  auto c = std::make_unique<KeyValues>(); c->key = k; c->value = v; c->isBlock = block;
  children.push_back(std::move(c)); return children.back().get();
}
void KeyValues::MergeFrom(const KeyValues& o, bool overwrite) {
  for (auto& c : o.children) {
    KeyValues* mine = Find(c->key);
    if (mine && c->isBlock && mine->isBlock) { mine->MergeFrom(*c, overwrite); continue; }
    if (mine) { if (overwrite) { mine->value = c->value; mine->isBlock = c->isBlock; mine->children.clear(); if (c->isBlock) mine->MergeFrom(*c, true); } continue; }
    KeyValues* n = AddChild(c->key, c->value, c->isBlock);
    if (c->isBlock) n->MergeFrom(*c, true);
  }
}
