# FastPin install artwork

Flat palette: charcoal #151B1C, mint #19D98A, stone #E9EDEB. No gradients.

- app-icon.svg: vector master.
- dmg/dmg-background.svg: standard 540×380 Finder DMG background; icon positions about (130,220) and (410,220). Rasterize to PNG for dmgbuild/packaging.
- dmg/install-preview.svg: install walkthrough, not Finder DMG background.
- windows/extract-zip.svg, developer-mode.svg, ready-to-launch.svg: illustrations for the CURRENT ZIP + registration installation flow.
- windows/installer-sidebar.svg and installer-header.svg: optional future NSIS wizard art. Windows currently ships ZIP, not an installer wizard.

All artwork has accompanying ready-to-use PNG and BMP exports in the downloadable asset bundle provided in chat. Integration with CMake/Qt and packaging scripts remains separate.
