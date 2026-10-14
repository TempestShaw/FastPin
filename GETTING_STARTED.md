# FastPin — quick start

Capture a piece of your screen, add a note, and keep it on your desktop.

## Install

**macOS preview:** macOS 14 or newer, Apple Silicon. Open the DMG and drag FastPin into Applications. This preview is not notarized. If macOS blocks its first launch, follow [Apple's app-opening instructions](https://support.apple.com/en-us/102445). Grant Screen Recording when prompted. Element detection and mouse shortcuts also use Accessibility permission. Follow the first-run guide; reopen it with Getting started.

**Windows developer preview:** Windows 10 version 2004 or newer / Windows 11, x64. Extract the Windows ZIP into a permanent folder. Enable Developer Mode in Windows Settings, then open PowerShell in the extracted folder and run:

```powershell
Add-AppxPackage -Register "$PWD\AppxManifest.xml"
```

Launch FastPin from Start. Keep the extracted folder in place while installed. OCR uses installed Windows OCR language packs; add your language in Windows Settings if needed. See [Microsoft's loose-file registration guide](https://learn.microsoft.com/en-us/windows/apps/develop/testing/loose-file-registration). To uninstall, use Windows Settings → Apps, then remove the extracted folder.

## Capture, annotate, pin

1. Choose Capture screenshot from the menu bar/tray, or press Ctrl+Shift+2. Drag to select a region. Tab switches between Window and Element detection; click a detected target to select it.
2. Use the floating toolbar: P for pen, A for arrow, R for rectangle, T for text. With T selected, click the image and type. Enter finishes text, Shift+Enter adds a line, and Escape cancels text. Undo/redo uses your platform's usual shortcuts.
3. Press Enter to pin the selected image. Its tools start hidden. Drag the pin to move it; use the wheel to zoom. Hover over the image and press Space to show its tools; press Space again to hide them. Escape closes it.

Choose Restore last image, or press Ctrl+Shift+4, to bring back the most recent capture/pin with its annotations. Paste as pin / Ctrl+Shift+3 pins an image from the clipboard.

## More useful controls

| Task | Control |
| --- | --- |
| Copy selected image | Cmd+C on macOS / Ctrl+C on Windows |
| Save selected image | Cmd+S on macOS / Ctrl+S on Windows |
| Copy recognized text | O, or OCR on the toolbar |
| Copy a pixel's color | C while capturing |
| Switch HEX / RGB | Shift while capturing |
| Sample the next physical pixel | Arrow keys |
| Reset a pin's zoom | 0 |
| Restore mouse input for click-through pins | Menu bar/tray → Restore mouse input for all pins |

Change capture, paste, and restore bindings in Preferences. Click a shortcut field, press a key combination or side-button gesture, release it, then choose Apply shortcuts. Clear a field to disable that shortcut.

Close or hide the control panel to leave FastPin in the menu bar/tray. Quit from its menu when finished.

[Download FastPin](https://github.com/TempestShaw/FastPin/releases) · [Report an issue](https://github.com/TempestShaw/FastPin/issues)
