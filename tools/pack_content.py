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


def fetch(url, tries=4):
    for i in range(tries):
        try:
            with urllib.request.urlopen(urllib.request.Request(url, headers=UA), timeout=60) as r:
                return r.read()
        except Exception as e:  # noqa: BLE001 - a dead mirror is expected, retried and reported
            last = e
            time.sleep(1.0 + 1.5 * i)
    raise last


def build(name, items, out_dir):
    """items: list of (zip entry name, url)"""
    data = {}
    failed = []

    def work(it):
        entry, url = it
        try:
            return entry, fetch(url)
        except Exception as e:  # noqa: BLE001
            failed.append((entry, str(e)))
            return None

    t0 = time.time()
    with cf.ThreadPoolExecutor(max_workers=24) as ex:
        for i, res in enumerate(ex.map(work, items), 1):
            if res:
                data[res[0]] = res[1]
            if i % 200 == 0:
                print(f"  {name}: {i}/{len(items)} ({time.time() - t0:.0f}s)", flush=True)

    if failed:
        print(f"  {name}: {len(failed)} file(s) missing, e.g. {failed[:3]}", flush=True)
        if len(failed) > max(3, len(items) // 50):
            raise SystemExit(f"{name}: too many missing files ({len(failed)}/{len(items)}), not shipping a broken pack")

    path = os.path.join(out_dir, name)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for entry in sorted(data):
            info = zipfile.ZipInfo(entry, date_time=(2024, 1, 1, 0, 0, 0))
            info.compress_type = zipfile.ZIP_DEFLATED
            info.external_attr = 0o644 << 16
            z.writestr(info, data[entry])
    print(f"  {name}: {len(data)} files, {os.path.getsize(path) / 1048576:.1f} MB "
          f"({sum(len(v) for v in data.values()) / 1048576:.1f} MB raw)", flush=True)


def main():
    out_dir = sys.argv[1] if len(sys.argv) > 1 else "dist"
    os.makedirs(out_dir, exist_ok=True)

    # base: map textures, agents, sounds, sound scripts (unpacked into content/css/cstrike)
    base = [(p, BASE_URL + p) for p in quoted("src/engine/content_base.inc")]
    build("cshvh-content-base.zip", base, out_dir)

    # CS:GO weapon models + their materials (content/extras/csgo_weapons)
    weapons = [(p, WEAPON_URL + p) for p in quoted("src/engine/content_weapons.inc")]
    build("cshvh-content-weapons.zip", weapons, out_dir)

    # maps (content/extras/csgo_maps)
    maps = []
    for e in map_entries():
        if e.startswith("http://") or e.startswith("https://"):
            maps.append(("maps/" + e.rsplit("/", 1)[-1], e))
        else:
            maps.append((e, MAP_URL + e))
    build("cshvh-content-maps.zip", maps, out_dir)


if __name__ == "__main__":
    main()
