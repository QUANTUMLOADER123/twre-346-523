#!/usr/bin/env python3
"""Regenerate src/engine/content_maps_textures.inc.

    python3 tools/gen_map_materials.py maps/*.bsp

CS:GO stock maps keep their base textures inside pak01_*.vpk, so they cannot be shipped. The classic maps
reuse the CS:S era texture set, which lives in loose files in bouletmarc/css_content - this tool reads the
texdata string table of every .bsp, resolves each material name against that repo (exact name first, then the
CS:GO _top/_middle/_bottom wall variants and finally the closest name in the same folder) and writes the list
of vmt/vtf files the map pack has to carry. Requires gh (for the token) and curl.
"""
import json, os, re, struct, subprocess, sys, urllib.parse

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TOKEN = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN") or subprocess.run(["gh", "auth", "token"], capture_output=True, text=True).stdout.strip()
API = "https://api.github.com"
CSS_REPO = "bouletmarc/css_content"
CSS_REF = "master"
CSS_RAW = f"https://raw.githubusercontent.com/{CSS_REPO}/{CSS_REF}/"
CACHE = os.path.join(ROOT, "dist", ".matcache")
os.makedirs(CACHE, exist_ok=True)


def api_get(url, binary=False, tries=4):
    key = os.path.join(CACHE, re.sub(r"[^A-Za-z0-9_.-]", "_", url)[-180:])
    if os.path.exists(key) and os.path.getsize(key) > 0:
        return open(key, "rb").read() if binary else open(key, "r", encoding="utf-8").read()
    for attempt in range(tries):
        p = subprocess.run(["curl", "-sS", "-m", "120", "-H", "Authorization: Bearer " + TOKEN,
                            "-H", "Accept: application/vnd.github.raw", url], capture_output=True)
        if p.returncode == 0 and p.stdout and not p.stdout.startswith(b'{"message"'):
            with open(key, "wb") as f:
                f.write(p.stdout)
            return p.stdout if binary else p.stdout.decode("utf-8", "latin1")
    return None


def contents(path):
    url = f"{API}/repos/{CSS_REPO}/contents/{urllib.parse.quote(path)}?ref={CSS_REF}"
    d = api_get(url)
    return json.loads(d) if d else None


def tree_map():
    d = json.loads(api_get(f"{API}/repos/{CSS_REPO}/git/trees/{CSS_REF}?recursive=1"))
    return {e["path"].lower(): e["path"] for e in d["tree"] if e["type"] == "blob"}


def map_materials(bsp_path):
    """texdata string table of a VBSP v21 map -> material names"""
    d = open(bsp_path, "rb").read()

    def lump(i):
        ofs, ln = struct.unpack_from("<2i", d, 8 + 16 * i)
        return d[ofs:ofs + ln]
    tbl, data = lump(44), lump(43)
    names = []
    for k in range(len(tbl) // 4):
        off = struct.unpack_from("<i", tbl, 4 * k)[0]
        if off >= len(data):
            continue
        names.append(data[off:data.find(b"\0", off)].decode("latin1"))
    return names


KV = re.compile(r'"([^"]+)"\s+"([^"]*)"')


def vmt_refs(text):
    refs = []
    for k, v in KV.findall(text):
        k = k.lower()
        if k in ("$basetexture", "$basetexture2", "$bumpmap", "$bumpmap2", "$detail", "$blendmodulatetexture",
                 "$phongexponenttexture", "$phongmask", "$envmapmask", "$normalmap", "$texture2"):
            if v and not v.startswith("vgui/") and not v.startswith("#"):
                refs.append(v)
    return refs


SUFFIXES = ("_top", "_middle", "_mid", "_bottom", "_left", "_right", "_up", "_down", "_outer", "_inner",
            "_a", "_b", "_c", "_d")


def resolve(tmap, by_dir, mat):
    """CS:GO wall variants (SITEBWALL05_TOP) and renamed CS:S textures -> the closest CS:S file"""
    low = mat.lower()
    p = "materials/" + low + ".vmt"
    if p in tmap:
        return tmap[p]
    base = low
    for suf in SUFFIXES:
        if base.endswith(suf) and len(base) - len(suf) > 4:
            cand = base[:-len(suf)]
            p = "materials/" + cand + ".vmt"
            if p in tmap:
                return tmap[p]
    # longest prefix in the same folder, then a trimmed name without trailing digits
    folder, _, stem = low.rpartition("/")
    if folder:
        best = ""
        for cand in by_dir.get(folder, ()):
            cstem = cand.rsplit("/", 1)[-1][:-4]
            if cstem and stem.startswith(cstem) and len(cstem) > len(best):
                best = cstem
        if best:
            return tmap[f"materials/{folder}/{best}.vmt"]
        trimmed = re.sub(r"[0-9]+[a-z]?$", "", stem)
        if trimmed and trimmed != stem:
            for cand in by_dir.get(folder, ()):
                if cand.rsplit("/", 1)[-1][:-4].startswith(trimmed):
                    return tmap[cand]
    return None


def main():
    maps = sys.argv[1:]
    tmap = tree_map()
    by_dir = {}
    for path in tmap.values():
        if not path.startswith("materials/") or not path.endswith(".vmt"):
            continue
        folder = path[len("materials/"):].rsplit("/", 1)[0]
        by_dir.setdefault(folder, []).append(path)
    print(f"css_content: {len(tmap)} files")
    want_vmt, want_vtf = {}, {}
    stats = []
    for m in maps:
        name = os.path.basename(m)
        mats = map_materials(m)
        real = [x for x in mats if not x.lower().startswith("tools/")]
        hit = 0
        for mat in real:
            found = resolve(tmap, by_dir, mat)
            if not found:
                continue
            hit += 1
            want_vmt[found.lower()] = found
            txt = api_get(f"{API}/repos/{CSS_REPO}/contents/{urllib.parse.quote(found)}?ref={CSS_REF}")
            if not txt:
                continue
            for ref in vmt_refs(txt):
                base = ref[:-4] if ref.lower().endswith(".vtf") else ref
                p = "materials/" + base.lower() + ".vtf"
                if p in tmap:
                    want_vtf[p] = tmap[p]
                p2 = "materials/" + base.lower() + ".vmt"
                if p2 in tmap and p2 not in want_vmt:
                    pass   # blend materials are pulled by the engine through their own vmt
        stats.append((name, len(real), hit))
        print(f"{name}: {hit}/{len(real)} materials resolved")

    files = [f for f in sorted(want_vmt.values()) + sorted(want_vtf.values()) if "{" not in f and "}" not in f]
    print(f"total: {len(want_vmt)} vmt + {len(want_vtf)} vtf = {len(files)} files")
    lines = ['// Generated by tools/gen_map_materials.py - the CS:S-era textures the CS:GO maps in s_csgoMapFiles[]',
             '// reference (the CS:GO ones live inside pak01 and cannot be shipped). Paths are repo-relative, so the',
             '// engine and the zip builder both keep materials/... as the destination.',
             'const char* const s_mapTextureFiles[] = {']
    row = []
    for f in files:
        row.append(f'  "{CSS_RAW}{f}",')
        if len(row) == 3:
            lines.append(" ".join(row))
            row = []
    if row:
        lines.append(" ".join(row))
    lines.append("  nullptr };")
    out = os.path.join(ROOT, "src/engine/content_maps_textures.inc")
    open(out, "w").write("\n".join(lines) + "\n")
    print("wrote", out)


main()
