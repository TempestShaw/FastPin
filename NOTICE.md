FastPin grew out of the native C++ implementation developed in ScreenPinKit.
Original ScreenPinKit source and image assets: copyright InterwovenCode / ScreenPinKit contributors, MIT license (LICENSE.md).
Upstream: https://github.com/InterwovenCode/ScreenPinKit

This application dynamically links Qt Core, Gui and Widgets, and uses Qt platform/image/style plugins.
Qt: copyright The Qt Company Ltd. and contributors; GNU LGPL version 3.
Copies of LGPL version 3 and GPL version 3 accompany this package in licenses/.
Qt source and third-party notices: https://code.qt.io/cgit/qt/qtbase.git/ and https://doc.qt.io/qt-6/licenses-used-in-qt.html

Release previews use Qt 6.8.3: macOS 14+ / Apple Silicon and Windows 10 version 2004+ / x64. Local Homebrew builds may use Qt 6.11.2.
Homebrew's Qt package links additional libraries including ICU, HarfBuzz, FreeType, GLib, DBus and image/compression libraries. The local package's Qt SPDX bill of materials is included when available. Public distribution requires carrying the applicable notices for the actual dependencies in that distribution.

The preview uses an ad-hoc macOS signature and is not notarized. The Windows preview is an unpacked development layout, registered with Developer Mode. Standard macOS distribution uses Developer ID and notarization; standard Windows MSIX distribution uses a trusted publisher signature. These signing requirements do not replace the license obligations above.

Third-party notices, source locations, and Qt library replacement instructions accompany the preview in licenses/third-party/. This software is based in part on the work of the FreeType Team. The application permits modification and reverse engineering for debugging modifications to LGPL libraries; no additional restriction is imposed by the application license.
