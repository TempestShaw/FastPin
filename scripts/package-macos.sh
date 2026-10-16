#!/bin/bash
set -euo pipefail
if [[ $# != 2 ]]; then
    echo "Usage: $0 build-directory new-package-directory" >&2
    exit 1
fi
script_dir=$(cd "$(dirname "$0")" && pwd)
build=$1
output=$2
# Deployment rewrites binaries, so always install into a new directory.
mkdir "$output"
output=$(cd "$output" && pwd)
cmake --install "$build" --config Release --prefix "$output"
"$script_dir/sign-macos-local.sh" "$output/FastPin.app"
ditto -c -k --sequesterRsrc --keepParent "$output/FastPin.app" "$output/FastPin-macOS-$(uname -m).zip"
