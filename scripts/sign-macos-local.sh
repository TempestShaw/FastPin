#!/bin/bash
# Persistent local development identity; not a Developer ID distribution signature.
set -euo pipefail
umask 077

if [[ $# != 1 || ! -d "$1/Contents" ]]; then
    echo "Usage: $0 /path/to/FastPin.app" >&2
    exit 1
fi
app=$1
bundle_id=$(/usr/libexec/PlistBuddy -c 'Print :CFBundleIdentifier' "$app/Contents/Info.plist")
[[ "$bundle_id" == io.github.tempestshaw.fastpin ]] || { echo "Unexpected bundle identifier: $bundle_id" >&2; exit 1; }
identity='FastPin Local Development'
state="$HOME/Library/Application Support/FastPin/signing"
mkdir -p "$state"
chmod 700 "$state"
temp=$(mktemp -d "$state/.setup.XXXXXX")
trap 'rm -rf "$temp"' EXIT

if [[ -f "$state/keychain-path" ]]; then
    keychain=$(cat "$state/keychain-path")
else
    keychain=$(/usr/bin/security default-keychain -d user | sed -E 's/^[[:space:]]*"([^"]+)"$/\1/')
fi

if [[ ! -f "$state/certificate.pem" ]]; then
    # Recover an already imported identity rather than silently replacing its key.
    if ! /usr/bin/security find-certificate -c "$identity" -p "$keychain" > "$temp/certificate.pem" 2>/dev/null; then
        cat > "$temp/openssl.cnf" <<'EOF'
[req]
prompt = no
distinguished_name = subject
x509_extensions = extensions
[subject]
CN = FastPin Local Development
OU = Local Development
[extensions]
basicConstraints = critical,CA:false
keyUsage = critical,digitalSignature
extendedKeyUsage = critical,codeSigning
subjectKeyIdentifier = hash
EOF
        /usr/bin/openssl req -new -x509 -newkey rsa:2048 -sha256 -nodes -days 3650 \
            -config "$temp/openssl.cnf" -keyout "$temp/key.pem" -out "$temp/certificate.pem" 2> "$temp/openssl.log"
        /usr/bin/openssl pkcs12 -export -inkey "$temp/key.pem" -in "$temp/certificate.pem" \
            -name "$identity" -out "$temp/identity.p12" -passout pass:fastpin-import
        # Only codesign gets key access. The imported key cannot be exported.
        /usr/bin/security import "$temp/identity.p12" -k "$keychain" -P fastpin-import -x -T /usr/bin/codesign
    fi
    cp "$temp/certificate.pem" "$state/certificate.pem"
    printf '%s\n' "$keychain" > "$state/keychain-path"
fi

fingerprint=$(/usr/bin/openssl x509 -in "$state/certificate.pem" -noout -fingerprint -sha1 | cut -d= -f2 | tr -d ':')
# Confirm the original identity is still installed. Never generate a replacement
# for a missing key/certificate: replacing it would invalidate existing grants.
/usr/bin/security find-certificate -c "$identity" -Z "$keychain" | /usr/bin/grep -F "$fingerprint" > /dev/null || {
    echo 'The saved signing certificate is missing from its keychain. Restore it; do not regenerate it.' >&2
    exit 1
}
sign() {
    /usr/bin/codesign --force --sign "$fingerprint" --keychain "$keychain" --timestamp=none "$@"
}
# Sign nested libraries before their containing frameworks and app. No --deep signing.
while IFS= read -r -d '' library; do sign "$library"; done < <(find "$app/Contents" -type f -name '*.dylib' -print0)
while IFS= read -r -d '' helper; do sign "$helper"; done < <(find "$app/Contents/Frameworks" -type f -name Autoupdate -print0)
while IFS= read -r -d '' helper; do sign "$helper"; done < <(find "$app/Contents/Frameworks" -depth -type d \( -name '*.xpc' -o -name '*.app' \) -print0)
while IFS= read -r -d '' framework; do sign "$framework"; done < <(find "$app/Contents/Frameworks" -depth -type d -name '*.framework' -print0)
requirement="designated => identifier \"$bundle_id\" and anchor H\"$fingerprint\""
sign --requirements "=$requirement" "$app"
/usr/bin/codesign --verify --deep --strict "$app"
/usr/bin/codesign --display --requirements - "$app"
