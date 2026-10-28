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
im.thumbnail((256, 144))
b = io.BytesIO(); im.save(b, "JPEG", quality=45)
d = base64.b64encode(b.getvalue()).decode()
parts = [d[i:i + 4000] for i in range(0, len(d), 4000)][:2]   # annotations are cut at 4096 chars, max 10 per step
for k, p in enumerate(parts): print(f"::notice title=screenshot {n} {k}/{len(parts)}::" + p)
PY
}
adb install -r -g "$APK" 2>&1 | tail -2
adb shell wm size 1080x2400; adb shell wm density 420
adb shell settings put system accelerometer_rotation 0
adb logcat -c
adb shell am start -n com.cshvh.game/android.app.NativeActivity
sleep 25
shot menu
PID1=$(adb shell pidof com.cshvh.game | tr -d '\r')
# second launch straight into a match: autoexec.cfg is executed at startup
adb shell am force-stop com.cshvh.game
D=/sdcard/Android/data/com.cshvh.game/files
printf 'bot_quota 4\nmp_freezetime 1\nmap de_mirage\njointeam 2\njoinclass 1\n' > /tmp/autoexec.cfg
PUSH=$(adb push /tmp/autoexec.cfg $D/cfg/autoexec.cfg 2>&1; adb shell ls -la $D $D/cfg 2>&1)
adb shell am start -n com.cshvh.game/android.app.NativeActivity
sleep 30
shot map
adb shell input tap 1200 540; sleep 3
adb shell input swipe 300 800 300 500 2500; sleep 1
adb shell input swipe 1800 600 1500 600 800; sleep 4
shot moved
PID=$(adb shell pidof com.cshvh.game | tr -d '\r')
note "push" "$PUSH"
note "alive" "menu: pid=${PID1:-DEAD}, in game: pid=${PID:-DEAD}"
LOG=$(adb logcat -d | grep -E " (cshvh|raylib|DEBUG|libc) *:|FATAL|Fatal signal" | grep -v -e "Access denied finding" -e "ro.build.fingerprint" -e "Loaded model" | cut -c 19- | tail -45)
note "logcat" "$LOG"
[ -n "$PID" ] || exit 1
