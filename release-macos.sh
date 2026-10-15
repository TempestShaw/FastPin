#!/bin/bash
# Public distribution: no development-signing fallback.
set -euo pipefail
if [[ $# != 4 ]]; then
    echo 'Usage: release-macos.sh build-directory new-output-directory "Developer ID Application: ..." notary-keychain-profile' >&2
    exit 1
fi
build=$1
output=$2
identity=$3
profile=$4
script_dir=$(cd "$(dirname "$0")" && pwd)
[[ "$identity" == 'Developer ID Application: '* ]] || { echo 'A Developer ID Application identity is required.' >&2; exit 1; }
"${FASTPIN_DMG_PYTHON:-python3}" -c 'import dmgbuild'
mkdir "$output"
output=$(cd "$output" && pwd)
cmake --install "$build" --config Release --prefix "$output"
app="$output/FastPin.app"
sign() { /usr/bin/codesign --force --sign "$identity" --options runtime --timestamp "$@"; }
while IFS= read -r -d '' library; do sign "$library"; done < <(find "$app/Contents" -type f -name '*.dylib' -print0)
while IFS= read -r -d '' helper; do sign "$helper"; done < <(find "$app/Contents/Frameworks" -type f -name Autoupdate -print0)
while IFS= read -r -d '' helper; do sign "$helper"; done < <(find "$app/Contents/Frameworks" -depth -type d \( -name '*.xpc' -o -name '*.app' \) -print0)
while IFS= read -r -d '' framework; do sign "$framework"; done < <(find "$app/Contents/Frameworks" -depth -type d -name '*.framework' -print0)
sign "$app"
/usr/bin/codesign --verify --deep --strict "$app"
archive="$output/FastPin-macOS-$(uname -m).zip"
ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
xcrun notarytool submit "$archive" --keychain-profile "$profile" --wait --output-format plist > "$output/notarization.plist"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :status' "$output/notarization.plist")" == Accepted ]] || { echo 'Notarization was not accepted.' >&2; exit 1; }
xcrun stapler staple "$app"
xcrun stapler validate "$app"
/usr/sbin/spctl --assess --type execute --verbose=2 "$app"
rm "$archive"
ditto -c -k --sequesterRsrc --keepParent "$app" "$archive"
dmg="$output/FastPin-macOS-$(uname -m).dmg"
"$script_dir/package-dmg.sh" "$app" "$dmg"
/usr/bin/codesign --force --sign "$identity" --timestamp "$dmg"
xcrun notarytool submit "$dmg" --keychain-profile "$profile" --wait --output-format plist > "$output/dmg-notarization.plist"
[[ "$(/usr/libexec/PlistBuddy -c 'Print :status' "$output/dmg-notarization.plist")" == Accepted ]] || { echo 'DMG notarization was not accepted.' >&2; exit 1; }
xcrun stapler staple "$dmg"
xcrun stapler validate "$dmg"
/usr/sbin/spctl --assess --type open --context context:primary-signature --verbose=2 "$dmg"
shasum -a 256 "$archive" "$dmg" > "$output/SHA256SUMS.txt"
