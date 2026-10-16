#!/bin/bash
set -euo pipefail
if [[ $# != 2 ]]; then
    echo 'Usage: package-dmg.sh signed-app-bundle new-output.dmg' >&2
    exit 1
fi
repo_dir=$(cd "$(dirname "$0")/.." && pwd)
app=$(cd "$1" && pwd)
output=$2
[[ ! -e "$output" ]] || { echo 'Output already exists.' >&2; exit 1; }
/usr/bin/codesign --verify --deep --strict "$app"
"${FASTPIN_DMG_PYTHON:-python3}" -c 'import dmgbuild, sys; dmgbuild.build_dmg(sys.argv[1], "FastPin", settings_file=sys.argv[2], defines={"app": sys.argv[3], "background": sys.argv[4], "icon": sys.argv[5]})' \
    "$output" "$repo_dir/packaging/macos/dmg-settings.py" "$app" "$repo_dir/assets/dmg/background@2x.png" "$repo_dir/assets/app-icon.icns"
mount=$(mktemp -d)
trap 'hdiutil detach "$mount" >/dev/null 2>&1 || true; rmdir "$mount"' EXIT
hdiutil attach "$output" -readonly -nobrowse -noautoopen -mountpoint "$mount" >/dev/null
/usr/bin/codesign --verify --deep --strict "$mount/FastPin.app"
hdiutil detach "$mount" >/dev/null
rmdir "$mount"
trap - EXIT
hdiutil verify "$output"
