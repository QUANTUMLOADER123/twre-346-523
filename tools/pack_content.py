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
UA = {"User-Agent": "cshvh-content-builder"}
# on CI an authenticated token lifts the contents-API limit from 60 to 5000 requests per hour
TOKEN = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN") or ""


def quoted(path):
    with open(os.path.join(ROOT, path), encoding="utf-8") as f:
        return re.findall(r'"([^"]+)"', f.read())


def map_entries():
    """the s_csgoMapFiles[] array: bare paths (mirror + path) or full urls (any mirror)"""
    src = open(os.path.join(ROOT, "src/engine/download.cpp"), encoding="utf-8").read()
    start = src.index("const char* const s_csgoMapFiles[]")
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
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data[entry])
    print(f"  {name}: {len(data)} files, {os.path.getsize(path) / 1048576:.1f} MB "
          f"({sum(len(v) for v in data.values()) / 1048576:.1f} MB raw)", flush=True)


report = []


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
    # CS:GO weapon models, their materials and the weapon sound samples
    items += [(p, WEAPON_URL + p) for p in quoted("src/engine/content_weapons.inc")]
    # the CS:S era textures our maps reference
    items += [(repo_path(u), u) for u in quoted("src/engine/content_maps_textures.inc")]
    # the CS:S player models (4 CT + 4 T) with their animations and materials
    items += [(repo_path(u), u) for u in quoted("src/engine/content_agents.inc")]
    # static props (doors, crates, furniture)
    items += [(repo_path(u), u) for u in quoted("src/engine/content_props.inc")]
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
