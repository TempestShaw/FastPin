# Native application icons

The SVG files are the editable sources for the native application's capture-and-pin mark. The mark is used by the FastPin application and installers.

`app-icon.png` is the 1024 px application/header image. `tray-icon.png` is a 72 px transparent black template for the macOS menu bar; the system supplies its appearance in light and dark mode.

The macOS ICNS contains 16–1024 px representations. The Windows ICO contains 16, 24, 32, 48, 64, 128 and 256 px PNG representations. Rasterize from the SVG sources when changing the artwork, then rebuild these platform containers. The checked-in outputs require no SVG renderer or new dependency at application runtime.

`dmg/background.svg` is the editable 660×440 pt installation artwork. `background@2x.png` is its 1320×880 px rendition with 144 DPI metadata, so Finder displays it at the intended size. Icon positions are in `../dmg-settings.py`. Rendering tools and dmgbuild are packaging-only dependencies.
