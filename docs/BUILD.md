# Building FastPin

Requires CMake 3.24+, a C++17 compiler, and Qt 6.8+ with Widgets and Test.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=/path/to/Qt
cmake --build build --config Release --parallel
ctest --test-dir build -C Release --output-on-failure
cmake --install build --config Release --prefix build/package
```

On macOS, the application is `build/FastPin.app`. On Windows, it is `build/FastPin.exe` (or under `build/Release` with a multi-configuration generator). macOS uses Vision for OCR; Windows uses Windows.Media.Ocr and requires package identity.

## Packaging

The GitHub workflow builds and packages both platforms with Qt 6.8.3. Python is used only by installer-building and verification helpers; it is not an application dependency.

For a local macOS development build, `./package-macos.sh build build/new-package` signs with the persistent FastPin development identity. Public macOS distribution uses `./release-macos.sh` with a Developer ID and a configured notary profile. The development preview is not notarized.

For Windows, run `./package-windows.ps1 -PackageDirectory build/package -Output build/FastPin-x64.msix`. A public MSIX requires a trusted signature matching the publisher identity. The ZIP preview uses loose-file registration with Developer Mode as described in GETTING_STARTED.md.

Application identifiers are `io.github.tempestshaw.fastpin` on macOS and `TempestShaw.FastPin` on Windows. FastPin has its own settings, saved-image storage, and permission grants.
