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
adb shell wm size 1080x2400; adb shell wm density 420
adb shell settings put system accelerometer_rotation 0
adb logcat -c
adb shell am start -n com.cshvh.game/android.app.NativeActivity
sleep 25
shot menu
PID=$(adb shell pidof com.cshvh.game | tr -d '\r')
note "alive after 25s (menu)" "pid=${PID:-DEAD}"
# second launch straight into a match: autoexec.cfg is executed at startup
adb shell am force-stop com.cshvh.game
D=/sdcard/Android/data/com.cshvh.game/files
printf 'bot_quota 4\nmp_freezetime 1\nmap de_mirage\n' > /tmp/autoexec.cfg
adb push /tmp/autoexec.cfg $D/cfg/autoexec.cfg
adb shell am start -n com.cshvh.game/android.app.NativeActivity
sleep 30
shot map
adb shell input tap 1200 540; sleep 3; shot tap
adb shell input swipe 300 800 300 500 2500; sleep 1
adb shell input swipe 1800 600 1500 600 800; sleep 4
shot moved
PID=$(adb shell pidof com.cshvh.game | tr -d '\r')
note "alive in game" "pid=${PID:-DEAD}"
LOG=$(adb logcat -d | grep -E " (cshvh|raylib|DEBUG|libc|AndroidRuntime) *:|FATAL|Fatal signal" | grep -v "Access denied finding\|ro.build.fingerprint" | tail -60)
note "logcat" "$LOG"
[ -n "$PID" ] || exit 1
