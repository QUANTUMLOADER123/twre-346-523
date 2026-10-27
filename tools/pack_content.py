#!/usr/bin/env python3
"""Builds the first-start content packs as three zip archives (release assets).

The game itself downloads a few thousand small files on first start, which is slow and fragile on a bad
connection. Instead, CI builds one archive per pack here, and the game then fetches a single file per pack
(falling back to the single-file list if the archive is not there).

    tools/pack_content.py dist

writes
    dist/cshvh-content-base.zip      materials/, sound/, scripts/, models/player/  (map textures, agents, sounds)
    dist/cshvh-content-weapons.zip   models/weapons/csgo/**, materials/models/csgo/**  (CS:GO weapon models)
    dist/cshvh-content-maps.zip      maps/*.bsp  (the old map versions)
    dist/cshvh-content-props.zip     models/props*, materials/models/props*  (the static props of those maps)
"""
import concurrent.futures as cf
import os
import re
import sys
import time
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BASE_URL = "https://raw.githubusercontent.com/bouletmarc/css_content/master/"
WEAPON_URL = "https://raw.githubusercontent.com/Seulyy/ARC9-GSR/5e16cf9baa37348fc82ba0c824d842dbcb44ab18/"
MAP_URL = "https://raw.githubusercontent.com/wiktorxxiii/csgo-fastdl/HEAD/csgo/"
R8_URL = ("https://raw.githubusercontent.com/EntranceJew/gmod-addons/"
          "48675a7f3a5a06d826bebba97078e24d0124f728/weapon_ttt_csgo_r8revolver/gamemodes/terrortown/content/")
UA = {"User-Agent": "cshvh-content-builder"}
# on CI an authenticated token lifts the contents-API limit from 60 to 5000 requests per hour
TOKEN = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN") or ""


def quoted(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return re.findall(r'"([^"]+)"', f.read())


def file_entries(array):
    """a `const char* const s_XFiles[]` block of download.cpp -> the relative file names it lists"""
    src = open(os.path.join(ROOT, "src/engine/download.cpp"), encoding="utf-8").read()
    start = src.index(f"const char* const {array}[]")
    end = src.index("nullptr };", start)
    out = []
    for line in src[start:end].splitlines():
        if line.strip().startswith("//"):
            continue
        out += re.findall(r'"([^"]+)"', line)
    return out


def map_entries(array="s_csgoMapFiles"):
    """the s_csgoMapFiles[] / s_portMapFiles[] arrays: bare paths (mirror + path) or full urls (any mirror)"""
    src = open(os.path.join(ROOT, "src/engine/download.cpp"), encoding="utf-8").read()
    start = src.index(f"const char* const {array}[]")
    end = src.index("nullptr };", start)
    out = []
    for line in src[start:end].splitlines():
        if line.strip().startswith("//"):
            continue
        out += re.findall(r'"([^"]+)"', line)
    return out


def fetch(url, tries=5, headers=None):
    for i in range(tries):
        try:
            hdr = dict(UA); hdr.update(headers or {})
            with urllib.request.urlopen(urllib.request.Request(url, headers=hdr), timeout=120) as r:
                return r.read()
        except Exception as e:  # noqa: BLE001 - a dead mirror is expected, retried and reported
            last = e
            time.sleep(1.5 * (i + 1) ** 1.5)
    raise last


def auth_headers(extra=None):
    hdr = {}
    if TOKEN:
        hdr["Authorization"] = "Bearer " + TOKEN
    hdr.update(extra or {})
    return hdr


def api_of(url):
    """raw.githubusercontent.com/<owner>/<repo>/<ref>/<path> -> the contents API (a second chance, and https-only)"""
    m = re.match(r"https://raw\.githubusercontent\.com/([^/]+)/([^/]+)/([^/]+)/(.+)$", url)
    if not m:
        return None
    return f"https://api.github.com/repos/{m.group(1)}/{m.group(2)}/contents/{m.group(4)}?ref={m.group(3)}"


RAW_MISSES = [0]      # consecutive failures of the raw CDN: switch to the contents API for the rest
PREFER_API = [False]


def download(items, label, workers=8):
    """items: (entry, url) pairs -> dict entry -> bytes; every failure is retried, then reported"""
    data, failed = {}, []

    def api_try(entry, url):
        alt = api_of(url)
        if not alt:
            return None
        try:
            return entry, fetch(alt, headers=auth_headers({"Accept": "application/vnd.github.raw"}), tries=6)
        except Exception as e:  # noqa: BLE001
            return entry, e

    def raw_try(entry, url):
        try:
            data = fetch(url, tries=3)
            RAW_MISSES[0] = 0
            return entry, data
        except Exception:  # noqa: BLE001
            RAW_MISSES[0] += 1
            if RAW_MISSES[0] >= 5:
                PREFER_API[0] = True
            return None

    def one(it):
        entry, url = it
        if entry in PORT_CACHE:
            return entry, PORT_CACHE[entry]
        first, second = (api_try, raw_try) if PREFER_API[0] else (raw_try, api_try)
        res = first(entry, url)
        if res is not None:
            return res
        res = second(entry, url)
        if res is not None:
            return res
        return entry, False

    t0 = time.time()
    with cf.ThreadPoolExecutor(max_workers=workers) as ex:
        for i, (entry, res) in enumerate(ex.map(one, items), 1):
            if isinstance(res, (bytes, bytearray)):
                data[entry] = res
            else:
                failed.append((entry, str(res) if res is not False else "download failed"))
            if i % 100 == 0:
                print(f"  {label}: {i}/{len(items)} ({time.time() - t0:.0f}s)", flush=True)
    return data, failed


def build(name, items, out_dir):
    """items: list of (zip entry name, url)"""
    data, failed = download(items, name)
    if failed:
        print(f"  {name}: {len(failed)} file(s) could not be downloaded, e.g. {[f[0] for f in failed[:5]]}", flush=True)
    report.append((name, len(items), len(data), sorted(f[0] for f in failed)))
    if not data:
        raise SystemExit("nothing downloaded")
    if failed and len(data) < len(items) * 0.9:
        raise SystemExit(f"only {len(data)} of {len(items)} files downloaded")
    if failed:
        # a few dead/renamed files are not a reason to drop a whole pack: the game marks an unpacked archive as
        # done, so a missing texture only means one surface falls back. A larger hole means the mirror is down.
        limit = max(3, len(items) // 50)
        if len(failed) > limit:
            raise SystemExit(f"{len(failed)} of {len(items)} files are unreachable")
        print(f"  {name}: shipping without {len(failed)} file(s)", flush=True)

    path = os.path.join(out_dir, name)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for entry in sorted(data):
            info = zipfile.ZipInfo(entry, date_time=(2024, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_STORED if entry.endswith(".bsp") else zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data[entry])
    print(f"  {name}: {len(data)} files, {os.path.getsize(path) / 1048576:.1f} MB "
          f"({sum(len(v) for v in data.values()) / 1048576:.1f} MB raw)", flush=True)


report = []



# ------------------------------------------------------------------------------------------------
# what the map files themselves ask for: brush materials, static prop models, skyboxes. The maps carry a
# "staticprop_dict" listing the prop models (CS:S-era paths) and a string table with every material name; the CS:S
# content repository has most of them, so the archive is extended with whatever it really provides.
ICON_URL = ("https://raw.githubusercontent.com/atiksoftware/csgo_memory_hacking_examples/"
            "4afd922fd7ae95c8a9b393d423944540182235f8/csgo_weapon_icons/")


def bsp_lumps(d):
    import struct
    if d[:4] != b"VBSP":
        return None
    return [struct.unpack("<iiii", d[8 + i * 16:24 + i * 16]) for i in range(64)]


def bsp_map_assets(d):
    """(material names, static prop model paths, sky name) of a .bsp"""
    import struct
    lumps = bsp_lumps(d)
    if not lumps:
        return [], [], None
    _, tlen, _, _ = lumps[44]
    dataoff, datalen, _, _ = lumps[43]
    names = []
    if tlen and datalen:
        table = d[lumps[44][0]:lumps[44][0] + tlen]
        blob = d[dataoff:dataoff + datalen]
        for i in range(0, tlen - 3, 4):
            off = struct.unpack("<i", table[i:i + 4])[0]
            if 0 <= off < len(blob):
                end = blob.find(b"\0", off)
                if end > off:
                    names.append(blob[off:end].decode("ascii", "replace").lower())
    props = []
    goff, glen, _, _ = lumps[35]
    if glen >= 4:
        g = d[goff:goff + glen]
        ng = struct.unpack("<i", g[0:4])[0]
        for i in range(ng):
            h = 4 + i * 16          # game lump header: id, flags/version, fileofs, filelen
            if h + 16 > len(g):
                break
            gid = struct.unpack("<i", g[h:h + 4])[0]
            ofs, ln = struct.unpack("<ii", g[h + 8:h + 16])
            if gid != 0x73707270 or ofs <= 0 or ofs + ln > len(d):   # file offsets, not lump offsets
                continue
            blob = d[ofs:ofs + ln]
            try:
                nd = struct.unpack("<i", blob[0:4])[0]
                dict_ = [blob[4 + k * 128:4 + (k + 1) * 128].split(b"\0")[0].decode("ascii", "replace").lower().replace("\\", "/")
                         for k in range(nd)]
                p = 4 + nd * 128
                nleaf = struct.unpack("<i", blob[p:p + 4])[0]
                p += 4 + nleaf * 2
                np = struct.unpack("<i", blob[p:p + 4])[0]
                p += 4
                size = (len(blob) - p) // np if np > 0 else 0
                if size < 56:
                    continue
                for k in range(np):
                    idx = struct.unpack("<H", blob[p + k * size + 24:p + k * size + 26])[0]
                    if idx < len(dict_):
                        props.append(dict_[idx])
            except (struct.error, IndexError, ZeroDivisionError):
                pass
    sky = None
    eoff, elen, _, _ = lumps[0]
    ent = d[eoff:eoff + elen].decode("ascii", "replace") if elen else ""
    m = re.search(r'"skyname"\s+"([^"]+)"', ent)
    if m:
        sky = m.group(1).lower()
    return names, props, sky


def css_tree():
    """every file path of the CS:S content repository"""
    try:
        d = fetch("https://api.github.com/repos/bouletmarc/css_content/git/trees/master?recursive=1", headers=auth_headers())
        import json
        return {t["path"] for t in json.loads(d)["tree"] if t["type"] == "blob"}
    except Exception as e:  # noqa: BLE001 - best effort, the pack is still built
        print(f"  map assets: can't list the CS:S repository ({e})", flush=True)
        return set()


def icon_items():
    """the buy menu / HUD weapon icons (svg) the engine looks up as svg_normal/<name>.svg"""
    src = open(os.path.join(ROOT, "src/engine/download.cpp"), encoding="utf-8").read()
    start = src.index("const char* const s_iconFiles[]")
    end = src.index("nullptr };", start)
    out = []
    for n in re.findall(r'ICON\("([a-z0-9_]+)"\)', src[start:end]):
        entry = f"svg_normal/{n}.svg"
        out.append((entry, ICON_URL + entry))
    return out


PORT_CACHE = {}   # zip entry -> bytes of a map that is downloaded packed and shipped unpacked


def port_map_items():
    """the s_portMapFiles[] maps (CS:GO -> CS:S ports on a fastdl mirror, .bz2): unpack into maps/<name>.bsp.

    A port has its own materials and props inside the pakfile, so all it needs is to be on disk; the runtime map
    downloader only speaks plain http files, hence the unpacking happens here.
    """
    import bz2
    out = []
    for url in map_entries("s_portMapFiles"):
        name = url.rsplit("/", 1)[-1]
        if name.endswith(".bz2"):
            name = name[:-4]
        entry = "maps/" + name
        try:
            # raw first, the contents API as the fallback (raw is unreachable from some CI networks)
            try:
                data = fetch(url, tries=2)
            except Exception:  # noqa: BLE001
                alt = api_of(url)
                if not alt:
                    raise
                data = fetch(alt, headers=auth_headers({"Accept": "application/vnd.github.raw"}), tries=6)
            if url.endswith(".bz2"):
                data = bz2.decompress(data)
            if not data.startswith(b"VBSP"):
                raise ValueError("not a bsp")
        except Exception as e:  # noqa: BLE001 - a dead mirror must not kill the release
            print(f"  ported map {name}: {e}", flush=True)
            continue
        PORT_CACHE[entry] = data
        PORT_CACHE[url] = data      # map_asset_items() looks the payload up by its source url
        out.append((entry, url))
        print(f"  ported map {name}: {len(data) / 1048576:.1f} MB", flush=True)
    return out


def map_asset_items(tree, entries=None):
    """the materials / prop models / skyboxes the shipped maps reference, as far as the CS:S repository has them"""
    have = tree
    out = []
    for e in (entries if entries is not None else map_entries()):
        url = e if e.startswith(("http://", "https://")) else MAP_URL + e
        try:
            if e in PORT_CACHE:
                data = PORT_CACHE[e]
            else:
                data = fetch(url)
        except Exception as ex:  # noqa: BLE001 - a dead mirror is expected
            print(f"  map assets: {e}: {ex}", flush=True)
            continue
        mats, props, sky = bsp_map_assets(data)
        added = 0
        for m in mats:
            for cand in (f"materials/{m}.vmt", f"materials/{m}.vtf"):
                if cand in have:
                    out.append((cand, BASE_URL + cand))
                    added += 1
        for p in props:
            folder = p.rsplit("/", 1)[0]
            for ext in (".mdl", ".vvd", ".dx90.vtx", ".phy"):
                cand = p[:-4] + ext
                if cand in have:
                    out.append((cand, BASE_URL + cand))
            matdir = "materials/" + folder[len("models/"):].lower() + "/"
            for cand in have:
                if cand.startswith(matdir) and cand.endswith((".vmt", ".vtf")):
                    out.append((cand, BASE_URL + cand))
        if sky:
            for suf in ("rt", "bk", "lf", "ft", "up", "dn"):
                for ext in (".vmt", ".vtf"):
                    cand = f"materials/skybox/{sky}{suf}{ext}"
                    if cand in have:
                        out.append((cand, BASE_URL + cand))
                        added += 1
        print(f"  map assets: {e.rsplit('/', 1)[-1]}: {len(mats)} materials, {len(props)} prop models, "
              f"sky {sky}, {added} files found", flush=True)
    return out


def main():
    """Build ONE archive with all the content: models, materials, maps and sounds.

    A single deflate stream over every file compresses better than a dozen small archives, and the player
    downloads (and the game unpacks) exactly one file.
    """
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "dist"
    os.makedirs(out_dir, exist_ok=True)

    def repo_path(url):
        """https://raw.githubusercontent.com/<owner>/<repo>/<ref>/<path> -> <path>"""
        rest = url.split("raw.githubusercontent.com/", 1)[1]
        return rest.split("/", 3)[3]

    items = []   # (zip entry, url)

    # CS:S player models + their materials, the stock textures, sounds and sound scripts
    items += [(p, BASE_URL + p) for p in quoted("src/engine/content_base.inc")]
    # CS:GO weapon models, their materials and the weapon sound samples; the CS:S view models of the same file come
    # from the CS:S content repository (they are the ones drawn with hands)
    def weapon_base(path):
        for pre in ("models/weapons/csgo/", "materials/models/csgo/", "materials/models/weapons/v_models/csgo/",
                    "sound/weapons/csgo/"):
            if path.startswith(pre):
                return WEAPON_URL
        return BASE_URL
    items += [(p, weapon_base(p) + p) for p in quoted("src/engine/content_weapons.inc")]
    # the CS:S era textures our maps reference
    items += [(repo_path(u), u) for u in quoted("src/engine/content_maps_textures.inc")]
    # the CS:S player models (4 CT + 4 T) with their animations and materials
    items += [(repo_path(u), u) for u in quoted("src/engine/content_agents.inc")]
    # static props (doors, crates, furniture)
    items += [(repo_path(u), u) for u in quoted("src/engine/content_props.inc")]
    # the CS:GO extras that have their own mirror: the R8 revolver (view + world model, textures, sounds) and the
    # SCAR-20 / SSG 08 / molotov (models, scopes, sounds). These arrays used to be downloaded file-by-file, which the
    # single archive has no path for - the revolver had no view model at all without them.
    items += [(pth, R8_URL + pth) for pth in file_entries("s_r8Files")]
    items += [(pth, WEAPON_URL + pth) for pth in file_entries("s_csgoFiles")]
    # the buy menu weapon icons
    items += icon_items()
    # the CS:GO -> CS:S ports: unpacked here (the game has no bz2 decoder) and shipped as maps/<name>.bsp
    ports = port_map_items()
    items += ports
    # materials, prop models and skyboxes of the maps themselves (best effort: whatever the CS:S repo really has)
    try:
        # the plain .bsp dumps reference textures that live in the CS:GO vpk, the ports carry their own inside the
        # pakfile: only the plain ones need the CS:S library pulled in
        items += map_asset_items(css_tree())
    except Exception as e:  # noqa: BLE001 - content is best effort, the release must still ship
        print(f"  map assets: skipped ({e})", flush=True)
    # the maps themselves (their own materials ride inside each map's pakfile lump)
    for e in map_entries():
        if e.startswith(("http://", "https://")):
            path = repo_path(e)
            if path.endswith(".bsp") and not path.startswith("maps/"):
                path = "maps/" + path.rsplit("/", 1)[-1]
            items.append((path, e))
        else:
            items.append((e, MAP_URL + e))

    # one entry per zip path (the same texture can be listed by two packs)
    seen, uniq = set(), []
    for entry, url in items:
        if entry in seen:
            continue
        seen.add(entry)
        uniq.append((entry, url))

    name = "cshvh-content-all.zip"
    try:
        build(name, uniq, out_dir)
    except SystemExit as e:
        print(f"  {name}: failed ({e})", flush=True)
        raise
    with open(os.path.join(out_dir, "pack-report.txt"), "w", encoding="utf-8") as f:
        for n, want, got, failed in report:
            f.write(f"{n}: {got}/{want} files" + (f", missing {failed}" if failed else ", complete") + "\n")


if __name__ == "__main__":
    main()
