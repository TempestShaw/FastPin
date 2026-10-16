# FastPin public design assets

Flat-only palette: charcoal `#151B1C`, mint `#19D98A`, stone `#E9EDEB`, white `#FFFFFF`. No gradients.

## macOS
- `app-icon.svg` — vector master (1024 square). Convert to `.icns` for Qt/macOS packaging.
- `dmg/dmg-background.png` — 540×380 DMG background, artwork deliberately leaves space for app and Applications icons.
- `dmg/dmg-background@2x.png` — 1080×760 Retina background.
- `dmg/install-preview.svg` — promotional drag-to-Applications walkthrough, **not** the DMG Finder window background.

Suggested DMG icon positions: app center (130, 220), Applications link center (410, 220). Adjust if the packaging script uses different icon sizes.

## Windows
The repository README currently documents a ZIP + Developer Mode + registration workflow, NOT a setup wizard.
- `windows/extract-zip.svg`, `windows/developer-mode.svg`, `windows/ready-to-launch.svg` — accurate installation walkthrough illustrations.
- `windows/installer-sidebar.bmp` (164×314) and `windows/installer-header.bmp` (150×57) — **optional future NSIS wizard** assets, not in use by the documented ZIP install.

PNG previews accompany SVGs. To wire these assets into the build, adjust the current Qt/CMake and DMG packaging scripts separately. Assets alone do not modify installer behavior.

## Integration
The native application icon exports are in `../assets/`. DMG geometry is configured in `../packaging/macos/dmg-settings.py`; the Retina background retains 144 DPI metadata. Install walkthroughs appear in `../GETTING_STARTED.md` and ship with the application. The site uses the same icon and walkthrough SVGs. Future wizard art remains unused.
