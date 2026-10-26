#!/usr/bin/env bash
# Counter-Strike: HvH - downloads the REAL CS:GO content (agents, weapons, textures, sounds, all stock maps)
#
# Uses Valve's official SteamCMD with an ANONYMOUS login to install the free
# "Counter-Strike: Global Offensive Dedicated Server" (Steam app 740, about 2 GB) into content/csgo.
# No Steam account and no game purchase are needed. Nothing is redistributed by this project:
# everything is read straight out of <content/csgo>/pak01_dir.vpk at runtime.
# SteamCMD is 32-bit: on 64-bit Linux you may need lib32gcc-s1 (Debian/Ubuntu) or glibc.i686 (Fedora).
set -e
cd "$(dirname "$0")/.."
ROOT="$(pwd)"
STEAMCMD="$ROOT/content/steamcmd"
TARGET="$ROOT/content/csgo"
mkdir -p "$STEAMCMD"
if [ ! -x "$STEAMCMD/steamcmd.sh" ]; then
  echo "[1/2] Downloading SteamCMD from Valve..."
  curl -fsSL https://steamcdn-a.akamaihd.net/client/installer/steamcmd_linux.tar.gz | tar -xz -C "$STEAMCMD"
fi
echo "[2/2] Installing the CS:GO Dedicated Server content into $TARGET (about 2 GB)..."
for i in 1 2 3; do
  { [ -f "$TARGET/pak01_dir.vpk" ] || [ -f "$TARGET/csgo/pak01_dir.vpk" ]; } && break
  "$STEAMCMD/steamcmd.sh" +force_install_dir "$TARGET" +login anonymous +app_update 740 validate +quit || true
done
if [ -f "$TARGET/pak01_dir.vpk" ] || [ -f "$TARGET/csgo/pak01_dir.vpk" ]; then
  echo "Done. Start Counter-Strike: HvH - the CS:GO content (agents, weapons, textures, maps) is used automatically."
else
  echo "The download did not finish. Run this script again (it resumes)."; exit 1
fi
