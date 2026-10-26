#!/usr/bin/env bash
# Counter-Strike: HvH - downloads Counter-Strike: Source content (maps, models, textures, sounds)
#
# Uses Valve's official SteamCMD with an ANONYMOUS login to install the free
# "Counter-Strike: Source Dedicated Server" (Steam app 232330, about 2 GB) into content/css.
# No Steam account and no game purchase are needed. Nothing is redistributed by this project.
# SteamCMD is 32-bit: on 64-bit Linux you may need lib32gcc-s1 (Debian/Ubuntu) or glibc.i686 (Fedora).
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
STEAMCMD="$ROOT/content/steamcmd"
TARGET="$ROOT/content/css"
mkdir -p "$STEAMCMD"
if [ ! -x "$STEAMCMD/steamcmd.sh" ]; then
  echo "[1/2] Downloading SteamCMD from Valve..."
  curl -fsSL https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz | tar -xz -C "$STEAMCMD"
fi
echo "[2/2] Installing Counter-Strike: Source Dedicated Server content into $TARGET (about 2 GB)..."
for i in 1 2 3; do
  [ -f "$TARGET/cstrike/cstrike_pak_dir.vpk" ] && break
  "$STEAMCMD/steamcmd.sh" +force_install_dir "$TARGET" +login anonymous +app_update 232330 validate +quit || true
done
if [ -f "$TARGET/cstrike/cstrike_pak_dir.vpk" ]; then
  echo "Done. Start Counter-Strike: HvH - CS:S content is used automatically."
else
  echo "The download did not finish. Run this script again (it resumes)."; exit 1
fi
