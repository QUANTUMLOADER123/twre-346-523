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
    if failed:
        # the game marks an unpacked archive as complete, so never ship a partial one: the single-file
        # download (resumable, retried on the next start) handles the pack instead
        raise SystemExit(f"{len(failed)} of {len(items)} files are unreachable")

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
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "dist"
    os.makedirs(out_dir, exist_ok=True)

    packs = {}


    def base_pack():
        # map textures, agents, sounds, sound scripts (unpacked into content/css/cstrike)
        return [(p, BASE_URL + p) for p in quoted("src/engine/content_base.inc")]

    def weapon_pack():
        # CS:GO weapon models + their materials (content/extras/csgo_weapons)
        return [(p, WEAPON_URL + p) for p in quoted("src/engine/content_weapons.inc")]

    def repo_path(url):
        """https://raw.githubusercontent.com/<owner>/<repo>/<ref>/<path> -> <path>"""
        rest = url.split("raw.githubusercontent.com/", 1)[1]
        return rest.split("/", 3)[3]

    def map_pack():
        # the maps and the shared textures they reference, in one archive
        out = []
        for e in map_entries():
            if e.startswith(("http://", "https://")):
                path = repo_path(e)
                if path.endswith(".bsp") and not path.startswith("maps/"):
                    path = "maps/" + path.rsplit("/", 1)[-1]
                out.append((path, e))
            else:
                out.append((e, MAP_URL + e))
        for u in quoted("src/engine/content_maps_textures.inc"):
            out.append((repo_path(u), u))
        return out

    packs["cshvh-content-base.zip"] = base_pack
    packs["cshvh-content-weapons.zip"] = weapon_pack
    packs["cshvh-content-maps.zip"] = map_pack

    built = 0
    for name, make in packs.items():
        try:
            build(name, make(), out_dir)
            built += 1
        except SystemExit as e:  # a mirror is down: skip this pack, the game falls back to single files
            print(f"  {name}: skipped ({e})", flush=True)
        except Exception as e:  # noqa: BLE001
            print(f"  {name}: skipped ({e})", flush=True)
    with open(os.path.join(out_dir, "pack-report.txt"), "w", encoding="utf-8") as f:
        for name, want, got, failed in report:
            f.write(f"{name}: {got}/{want} files" + (f", missing {failed}" if failed else ", complete") + "\n")
    if not built:
        raise SystemExit("no content pack could be built")


if __name__ == "__main__":
    main()
