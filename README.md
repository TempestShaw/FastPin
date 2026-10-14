# FastPin

Capture a piece of your screen, add a note, and keep it on your desktop.

[Download FastPin](https://github.com/TempestShaw/FastPin/releases) · [Quick start](GETTING_STARTED.md) · [Report an issue](https://github.com/TempestShaw/FastPin/issues)

## Capture, annotate, pin

1. Press **Ctrl+Shift+2** and drag to select a region. **Tab** switches between window and element detection.
2. Add a pen stroke, arrow, rectangle, or text directly on the image.
3. Press **Enter** to pin it. Move the image by dragging and zoom with the scroll wheel.

Pinned images start with their tools hidden. Hover over an image and press **Space** to show the tools; press Space again to hide them.

## A few useful shortcuts

| Action | Default shortcut |
| --- | --- |
| Capture a screenshot | Ctrl+Shift+2 |
| Pin an image from the clipboard | Ctrl+Shift+3 |
| Restore the last image | Ctrl+Shift+4 |
| Copy recognized text | O |
| Copy a pixel's color while capturing | C |

Change capture, paste, and restore shortcuts in **Preferences**. Click a field, press your preferred keys or mouse side buttons, then choose **Apply shortcuts**.

## Install

- **macOS 14+ / Apple Silicon:** download the DMG and drag FastPin into Applications. This preview is not notarized; follow [Apple's opening instructions](https://support.apple.com/en-us/102445) if the first launch is blocked.
- **Windows 10 2004+ / Windows 11 / x64:** download the ZIP, extract it to a permanent folder, enable Developer Mode, and follow the [quick start](GETTING_STARTED.md) to register the app and launch it from Start.

## Build from source

FastPin is a standalone C++17 application using Qt Widgets. See [build instructions](docs/BUILD.md).

## Credits and license

FastPin is derived from the native implementation developed in [ScreenPinKit](https://github.com/InterwovenCode/ScreenPinKit). Original attribution and the MIT license are preserved in [LICENSE.md](LICENSE.md) and [NOTICE.md](NOTICE.md). Qt and bundled library notices are included in `licenses/`.
