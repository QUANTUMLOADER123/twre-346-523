#!/usr/bin/env bash
# Counter-Strike: HvH - Android APK build (native C++ / raylib NativeActivity, no Java)
#
#   android/build_apk.sh            -> bin/cshvh.apk  (arm64-v8a + armeabi-v7a)
#
# Needs the Android SDK (build-tools + a platform) and NDK:
#   ANDROID_SDK_ROOT / ANDROID_HOME, ANDROID_NDK_ROOT / ANDROID_NDK_LATEST_HOME (GitHub ubuntu runners have all of them)
# Signing: ANDROID_KEYSTORE_B64 + ANDROID_KEYSTORE_PASS (PKCS12) if set, otherwise android/debug.p12 (password "android").
# Environment: ABIS="arm64-v8a armeabi-v7a", JOBS=4, VERSION_CODE=1, VERSION_NAME=1.0
set -euo pipefail
cd "$(dirname "$0")/.."

SDK="${ANDROID_SDK_ROOT:-${ANDROID_HOME:-}}"
[ -d "$SDK" ] || { echo "ANDROID_SDK_ROOT not set"; exit 1; }
NDK="${ANDROID_NDK_ROOT:-${ANDROID_NDK_LATEST_HOME:-${ANDROID_NDK_HOME:-}}}"
[ -d "$NDK" ] || NDK=$(ls -d "$SDK"/ndk/* 2>/dev/null | sort -V | tail -1)
[ -d "$NDK" ] || { echo "Android NDK not found"; exit 1; }
BT=$(ls -d "$SDK"/build-tools/* | sort -V | tail -1)
PLATFORM_JAR=$(ls -d "$SDK"/platforms/android-*/android.jar | sort -V | tail -1)
TC="$NDK/toolchains/llvm/prebuilt/linux-x86_64/bin"
API=24
ABIS="${ABIS:-arm64-v8a armeabi-v7a}"
JOBS="${JOBS:-$(nproc)}"
VERSION_CODE="${VERSION_CODE:-$(git rev-list --count HEAD 2>/dev/null || echo 1)}"
VERSION_NAME="${VERSION_NAME:-1.0.$VERSION_CODE}"
echo ">> SDK $SDK"; echo ">> NDK $NDK"; echo ">> build-tools $BT"; echo ">> platform $PLATFORM_JAR"

RL=third_party/raylib/src
GLUE="$NDK/sources/android/native_app_glue"
OUT=build/android
STAGE=$OUT/apk
rm -rf "$STAGE"; mkdir -p "$STAGE" bin

GAME_SRC=$(ls src/engine/*.cpp src/game/*/*.cpp src/vgui/*.cpp src/cheat/*.cpp src/main.cpp)
COMMON="-DPLATFORM_ANDROID -D__ANDROID_API__=$API -DGRAPHICS_API_OPENGL_ES3 -fPIC -ffunction-sections -fdata-sections -funwind-tables -fstack-protector-strong -I$RL -I$GLUE"

for ABI in $ABIS; do
  case $ABI in
    arm64-v8a)   TRIPLE=aarch64-linux-android$API;     ARCHF="-mfix-cortex-a53-835769" ;;
    armeabi-v7a) TRIPLE=armv7a-linux-androideabi$API;  ARCHF="-march=armv7-a -mfloat-abi=softfp -mfpu=neon -mthumb" ;;
    x86_64)      TRIPLE=x86_64-linux-android$API;      ARCHF="" ;;
    *) echo "unknown ABI $ABI"; exit 1 ;;
  esac
  CC="$TC/$TRIPLE-clang"; CXX="$TC/$TRIPLE-clang++"
  O=$OUT/obj/$ABI; mkdir -p "$O"
  echo ">> [$ABI] compiling"
  export CC CXX O COMMON ARCHF
  {
    for m in rcore rshapes rtextures rtext rmodels raudio; do echo "c $RL/$m.c"; done
    echo "c $GLUE/android_native_app_glue.c"
    for f in $GAME_SRC; do echo "cpp $f"; done
  } | xargs -P "$JOBS" -L 1 bash -c '
    lang=$0; src=$1; obj="$O/$(echo "$src" | tr "/" "_").o"
    if [ -f "$obj" ] && [ "$obj" -nt "$src" ] && [ -z "${FORCE:-}" ] && [ "$lang" = c ]; then exit 0; fi
    if [ "$lang" = c ]; then "$CC" -O2 -std=gnu11 -w $COMMON $ARCHF -I. -c "$src" -o "$obj" > "$obj.log" 2>&1
    else "$CXX" -O2 -std=c++17 $COMMON $ARCHF -c "$src" -o "$obj" > "$obj.log" 2>&1; fi
    rc=$?; cat "$obj.log"
    if [ $rc -ne 0 ]; then
      echo "FAILED: $src"
      android/annotate.sh "compile $src" "$obj.log"
      exit 255
    fi'
  echo ">> [$ABI] linking libmain.so"
  mkdir -p "$STAGE/lib/$ABI"
  if ! "$CXX" -shared -o "$STAGE/lib/$ABI/libmain.so" "$O"/*.o \
    -u ANativeActivity_onCreate -Wl,--no-undefined -Wl,--gc-sections -Wl,-z,max-page-size=16384 -s \
    -static-libstdc++ -landroid -llog -lEGL -lGLESv3 -lOpenSLES -lm -ldl > $OUT/link.log 2>&1; then
    cat $OUT/link.log
    android/annotate.sh "link $ABI" $OUT/link.log
    exit 1
  fi
  ls -la "$STAGE/lib/$ABI/libmain.so"
done

echo ">> assets"
ASSETS=$OUT/assets
rm -rf "$ASSETS"; mkdir -p "$ASSETS"
cp -r assets "$ASSETS/assets"
{
  echo "$VERSION_NAME-$(git rev-parse --short HEAD 2>/dev/null || date +%s)"
  (cd "$ASSETS" && find assets -type f | sort)
} > "$ASSETS/apk_files.txt"
echo "   $(($(wc -l < "$ASSETS/apk_files.txt") - 1)) files"

echo ">> packaging"
rm -rf $OUT/res.zip $OUT/unsigned.apk $OUT/aligned.apk
"$BT/aapt2" compile --dir android/res -o $OUT/res.zip
"$BT/aapt2" link -o $OUT/unsigned.apk -I "$PLATFORM_JAR" --manifest android/AndroidManifest.xml -R $OUT/res.zip -A "$ASSETS" \
  --min-sdk-version $API --target-sdk-version 34 --version-code "$VERSION_CODE" --version-name "$VERSION_NAME" --auto-add-overlay
(cd "$STAGE" && zip -q -r ../unsigned.apk lib)
"$BT/zipalign" -p -f 4 $OUT/unsigned.apk $OUT/aligned.apk

KS=android/debug.p12; KSPASS=android
if [ -n "${ANDROID_KEYSTORE_B64:-}" ]; then
  echo "$ANDROID_KEYSTORE_B64" | base64 -d > $OUT/release.p12; KS=$OUT/release.p12; KSPASS="${ANDROID_KEYSTORE_PASS:?}"
fi
"$BT/apksigner" sign --ks "$KS" --ks-type PKCS12 --ks-pass "pass:$KSPASS" --out bin/cshvh.apk $OUT/aligned.apk
"$BT/apksigner" verify --print-certs bin/cshvh.apk | head -3
ls -la bin/cshvh.apk
