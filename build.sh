#!/usr/bin/env bash
# Counter-Strike: HvH - build script
#
#   ./build.sh            -> linux    (bin/cshvh, needs: g++ / gcc, libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libgl1-mesa-dev)
#   ./build.sh windows    -> windows  (bin/cshvh.exe, cross-compiled with zig: `pip install ziglang` or zig in PATH, or mingw-w64)
#   ./build.sh headless   -> software renderer, no window (bin/cshvh_headless) - used for automated screenshots/tests
#   ./build.sh clean
#
# raylib 6.2 sources are vendored in third_party/raylib (downloaded from github.com/raysan5/raylib if missing).
# Environment: JOBS=8 (parallel jobs), RAYLIB_DIR=/path/to/raylib (use an existing checkout)
set -euo pipefail
cd "$(dirname "$0")"

TARGET="${1:-linux}"
JOBS="${JOBS:-$(nproc 2>/dev/null || echo 4)}"
RAYLIB_VERSION=6.2
if [ -z "${RAYLIB_DIR:-}" ]; then
  if [ -f third_party/raylib/src/raylib.h ]; then RAYLIB_DIR=third_party/raylib   # vendored copy (also used by the Visual Studio solution)
  else RAYLIB_DIR=third_party/raylib-$RAYLIB_VERSION; fi
fi

if [ "$TARGET" = clean ]; then rm -rf build bin; echo "cleaned"; exit 0; fi

fetch_raylib() {
  if [ -f "$RAYLIB_DIR/src/raylib.h" ]; then return; fi
  echo ">> downloading raylib $RAYLIB_VERSION"
  mkdir -p third_party
  curl -fsSL "https://codeload.github.com/raysan5/raylib/tar.gz/refs/tags/$RAYLIB_VERSION" -o third_party/raylib.tgz
  tar -xzf third_party/raylib.tgz -C third_party
  rm -f third_party/raylib.tgz
}

find_zig() {
  if command -v zig >/dev/null 2>&1; then echo "zig"; return; fi
  for py in python3 python; do
    if command -v $py >/dev/null 2>&1 && $py -c "import ziglang" >/dev/null 2>&1; then echo "$py -m ziglang"; return; fi
  done
  echo ""
}

fetch_raylib
RL="$RAYLIB_DIR/src"
OBJ="build/$TARGET"
mkdir -p "$OBJ/raylib" bin

RL_SRC="rcore rshapes rtextures rtext rmodels raudio"
GAME_SRC=$(ls src/engine/*.cpp src/game/*/*.cpp src/vgui/*.cpp src/cheat/*.cpp src/main.cpp)
CXXFLAGS="-std=c++17 -O2 -I$RL"
CFLAGS="-O2 -I. -I$RL -I$RL/external/glfw/include"   # -I. : rlsw.h does #include __FILE__ (repo-relative path)

case "$TARGET" in
  linux)
    CC="${CC:-gcc}"; CXX="${CXX:-g++}"
    RL_SRC="$RL_SRC rglfw"
    CFLAGS="$CFLAGS -DPLATFORM_DESKTOP_GLFW -DGRAPHICS_API_OPENGL_33"
    CXXFLAGS="$CXXFLAGS -DPLATFORM_DESKTOP_GLFW"
    LIBS="-lGL -lm -lpthread -ldl -lrt -lX11"
    OUT=bin/cshvh ;;
  windows)
    ZIG="$(find_zig)"
    if [ -n "$ZIG" ]; then CC="$ZIG cc -target x86_64-windows-gnu"; CXX="$ZIG c++ -target x86_64-windows-gnu";
    elif command -v x86_64-w64-mingw32-g++ >/dev/null 2>&1; then CC=x86_64-w64-mingw32-gcc; CXX=x86_64-w64-mingw32-g++;
    else echo "need zig (pip install ziglang) or mingw-w64"; exit 1; fi
    RL_SRC="$RL_SRC rglfw"
    CFLAGS="$CFLAGS -DPLATFORM_DESKTOP_GLFW -DGRAPHICS_API_OPENGL_33"
    CXXFLAGS="$CXXFLAGS -DPLATFORM_DESKTOP_GLFW"
    LIBS="-lopengl32 -lgdi32 -lwinmm -lshell32 -luser32 -static -Wl,--subsystem,windows"
    OUT=bin/cshvh.exe ;;
  headless)
    CC="${CC:-gcc}"; CXX="${CXX:-g++}"
    CFLAGS="$CFLAGS -DPLATFORM_MEMORY -DGRAPHICS_API_OPENGL_SOFTWARE -DRLSW_MAX_TEXTURES=8192"   # CS:S maps use hundreds of textures
    CXXFLAGS="$CXXFLAGS -DCS_HEADLESS"
    LIBS="-lm -lpthread -ldl"
    OUT=bin/cshvh_headless ;;
  *) echo "unknown target '$TARGET' (linux | windows | headless | clean)"; exit 1 ;;
esac

# compile one file if the object is older than the source (or any header for C++)
NEWEST_HDR=$(find src -name '*.hpp' -printf '%T@\n' 2>/dev/null | sort -n | tail -1 | cut -d. -f1)
jobs_file=$(mktemp)
for f in $RL_SRC; do
  o="$OBJ/raylib/$f.o"
  [ -f "$o" ] && [ "$o" -nt "$RL/$f.c" ] && continue
  echo "$CC $CFLAGS -c $RL/$f.c -o $o" >> "$jobs_file"
done
for f in $GAME_SRC; do
  o="$OBJ/$(echo "$f" | tr / _ | sed 's/\.cpp$/.o/')"
  if [ -f "$o" ] && [ "$o" -nt "$f" ] && [ "$(stat -c %Y "$o" 2>/dev/null || echo 0)" -ge "${NEWEST_HDR:-0}" ]; then continue; fi
  echo "$CXX $CXXFLAGS -c $f -o $o" >> "$jobs_file"
done
n=$(wc -l < "$jobs_file")
echo ">> $TARGET: compiling $n file(s) with $JOBS jobs"
if [ "$n" -gt 0 ]; then
  tr '\n' '\0' < "$jobs_file" | xargs -0 -P "$JOBS" -I{} sh -c 'echo "   $(echo "{}" | grep -o "\-c [^ ]*" | cut -c4-)"; {}'
fi
rm -f "$jobs_file"

echo ">> linking $OUT"
$CXX $OBJ/*.o $OBJ/raylib/*.o $LIBS -o "$OUT"
echo ">> done: $OUT   (run it from the repository folder or next to assets/)"
