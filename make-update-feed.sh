#!/bin/bash
# Sign an already packaged macOS archive; the private key stays in Keychain or CI secrets.
set -euo pipefail
if [[ $# != 4 ]]; then
    echo 'Usage: make-update-feed.sh build-directory app-archive release-tag new-output-directory' >&2
    exit 1
fi
build=$1
archive=$2
tag=$3
output=$4
[[ "$tag" =~ ^v[0-9][A-Za-z0-9.+-]*$ ]] || { echo 'Expected a version tag beginning with v.' >&2; exit 1; }
script_dir=$(cd "$(dirname "$0")" && pwd)
tools="$build/_deps/sparkle-src/bin"
mkdir "$output"
cp "$archive" "$output/"
cp "$script_dir/updates/appcast.xml" "$output/appcast.xml"
args=(--maximum-deltas 0 --download-url-prefix "https://github.com/TempestShaw/FastPin/releases/download/$tag/"
      --link "https://github.com/TempestShaw/FastPin/releases/tag/$tag")
if [[ -n "${SPARKLE_PRIVATE_KEY:-}" ]]; then
    printf '%s' "$SPARKLE_PRIVATE_KEY" | "$tools/generate_appcast" --ed-key-file - "${args[@]}" "$output"
else
    "$tools/generate_appcast" --account io.github.tempestshaw.fastpin "${args[@]}" "$output"
fi
