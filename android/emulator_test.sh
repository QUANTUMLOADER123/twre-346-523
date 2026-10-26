#!/usr/bin/env bash
# emulator_test.sh <apk> - smoke test on a running emulator: install, start, check it survives, collect logcat + screenshots.
# Results are printed as GitHub annotations (screenshots as tiny base64 JPEGs) so they can be read through the API.
set -u
APK="$1"
note() { echo "::notice title=$1::$(printf '%s' "$2" | sed -e 's/%/%25/g' | awk '{printf "%s%%0A", $0}')"; }
shot() {  # shot <name>
  adb exec-out screencap -p > "/tmp/$1.png" 2>/dev/null || return
  python3 - "$1" <<'PY'
import sys, base64, io
from PIL import Image
n = sys.argv[1]
im = Image.open(f"/tmp/{n}.png").convert("RGB")
if im.height > im.width: im = im.rotate(90, expand=True)
im.thumbnail((320, 180))
b = io.BytesIO(); im.save(b, "JPEG", quality=55)
print(f"::notice title=screenshot {n}::" + base64.b64encode(b.getvalue()).decode())
PY
}
adb install -r -g "$APK" 2>&1 | tail -2
adb shell settings put system accelerometer_rotation 0
adb logcat -c
adb shell am start -n com.cshvh.game/android.app.NativeActivity
sleep 25
shot menu
PID=$(adb shell pidof com.cshvh.game | tr -d '\r')
note "alive after 25s" "pid=${PID:-DEAD}"
# start a game from the console-less path: tap through the main menu is resolution dependent, so use an intent-free
# approach: send Back (closes nothing at the menu), then check again
sleep 5
LOG=$(adb logcat -d | grep -E "cshvh|raylib|INFO: |WARNING: |ERROR: |FATAL|DEBUG   :|AndroidRuntime|libc    :" | tail -80)
note "logcat" "$LOG"
[ -n "$PID" ] || exit 1
