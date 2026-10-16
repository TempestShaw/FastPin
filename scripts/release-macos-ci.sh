#!/bin/bash
# Tag releases must never fall back to ad-hoc or local development signing.
set -euo pipefail
for name in MACOS_CERTIFICATE_P12_BASE64 MACOS_CERTIFICATE_PASSWORD MACOS_SIGNING_IDENTITY MACOS_TEAM_ID APPLE_ID APPLE_APP_PASSWORD; do
    [[ -n "${!name:-}" ]] || { echo "Release requires $name; no unsigned fallback is permitted." >&2; exit 1; }
done
[[ "$MACOS_SIGNING_IDENTITY" == "Developer ID Application: "*" ($MACOS_TEAM_ID)" ]] || {
    echo 'Signing identity must belong to the configured Apple team.' >&2; exit 1;
}
work=$(mktemp -d)
keychain="$work/release.keychain-db"
cleanup() { security delete-keychain "$keychain" >/dev/null 2>&1 || true; rm -rf "$work"; }
trap cleanup EXIT
keychain_password=$(openssl rand -hex 24)
printf '%s' "$MACOS_CERTIFICATE_P12_BASE64" | base64 --decode > "$work/certificate.p12"
security create-keychain -p "$keychain_password" "$keychain"
security set-keychain-settings -lut 21600 "$keychain"
security unlock-keychain -p "$keychain_password" "$keychain"
security import "$work/certificate.p12" -k "$keychain" -P "$MACOS_CERTIFICATE_PASSWORD" -T /usr/bin/codesign
security set-key-partition-list -S apple-tool:,apple:,codesign: -s -k "$keychain_password" "$keychain" >/dev/null
security list-keychains -d user -s "$keychain" "$HOME/Library/Keychains/login.keychain-db"
xcrun notarytool store-credentials fastpin-release --keychain "$keychain"     --apple-id "$APPLE_ID" --team-id "$MACOS_TEAM_ID" --password "$APPLE_APP_PASSWORD" >/dev/null
python3 -m venv build/dmg-tools
build/dmg-tools/bin/python -m pip install dmgbuild==1.6.7
FASTPIN_DMG_PYTHON="$PWD/build/dmg-tools/bin/python"     ./scripts/release-macos.sh build build/signed-release "$MACOS_SIGNING_IDENTITY" fastpin-release
cp build/signed-release/FastPin-macOS-arm64.zip build/FastPin-macOS-arm64.zip
cp build/signed-release/FastPin-macOS-arm64.dmg build/FastPin-macOS.dmg
# Artifact app must be the same signed bundle as the update archive.
rm -rf build/package/FastPin.app
ditto build/signed-release/FastPin.app build/package/FastPin.app
