# Third-party notices and library sources

FastPin is MIT-licensed and dynamically links Qt. The application allows replacement of LGPL libraries and reverse engineering for debugging such modifications. No extra license restriction is imposed.

Release previews use official Qt 6.8.3 binaries on macOS and Windows. Local macOS Homebrew builds may use Qt 6.11.2. `macOS/SOURCES.json` lists the bundled Homebrew libraries, original source URLs, versions and SHA-256 values. The exact Qt source archive accompanies release downloads. GLib and gettext archives accompany builds that bundle those Homebrew libraries. The saved Homebrew build formulae are in `macOS/build-formulae/`.

The Windows x64 preview uses unmodified Qt 6.8.3 MSVC 2022 x64 DLLs. Its Qt source archive is also provided in the release. MSVC app-local runtime DLLs are Microsoft's redistributable runtime, not application source: https://learn.microsoft.com/en-us/cpp/windows/determining-which-dlls-to-redistribute

Qt license texts and third-party attributions are included for both Qtbase source versions. These directories include notices for the whole source module; not every listed component is present in the application binary. The macOS FreeType license is the FreeType License. This software is based in part on the work of the FreeType Team. DBus is used under its alternative Academic Free License 2.1.

## Rebuild and replace

The application source and build instructions are in https://github.com/TempestShaw/FastPin/tree/v0.1.0-preview.1 . Qt Core, Gui, Widgets and the platform/image/style plugins are shared libraries. Build a binary-compatible version of the same Qt release with the same architecture, and replace the corresponding DLLs beside the Windows executable or frameworks/plugins in the macOS bundle. Rebuild the application against your modified Qt using CMake's `CMAKE_PREFIX_PATH` when ABI compatibility changes.

On macOS, work on a copy of the app, update library install paths if needed, then re-sign the modified bundle and nested code with your own identity or ad-hoc signature. Screen Recording/Accessibility may require a new grant after its signature changes. No original signing key or license key is needed to rebuild or replace these libraries. On Windows, remove development registration before replacing the layout, then register the modified manifest again. The runtime folders contain `qt.conf` for relative plugin paths.

Qt 6.11.2 source: https://download.qt.io/archive/qt/6.11/6.11.2/submodules/qtbase-everywhere-src-6.11.2.tar.xz
Qt 6.8.3 source: https://download.qt.io/archive/qt/6.8/6.8.3/submodules/qtbase-everywhere-src-6.8.3.tar.xz
