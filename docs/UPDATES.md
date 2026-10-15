# macOS settings and updates

Settings, Shortcuts and About use AppKit controls in a native toolbar settings window. Open it from **Settings…** in the control panel or menu bar, or launch FastPin with `--settings`. Capture, pinning, OCR and Windows controls retain their existing Qt implementation. Shortcuts and image recovery use the existing FastPin settings and storage paths.

macOS builds bundle Sparkle 2.10.0, pinned by SHA-256 in CMake. The About pane controls automatic checks and background downloads. Manual checks use Sparkle's native download/install interface. A verified background download exposes **Update and Restart** and also installs when FastPin quits. Disabling automatic checks stops scheduled checks; an already prepared update may still install on quit.

Sparkle authenticates update archives against `SUPublicEDKey` in Info.plist. The FastPin private key is stored under the Keychain account `io.github.tempestshaw.fastpin`. Keep this key across releases. Never commit or print it. For CI, store the exported key as the repository secret `SPARKLE_PRIVATE_KEY`.

## Publishing

The public feed is `https://raw.githubusercontent.com/TempestShaw/FastPin/main/updates/appcast.xml`. It supports preview releases without relying on GitHub's stable-only latest-release redirect. An empty initial feed advertises no update; it is not a published updater-enabled release.

Push a new `v*` tag after the change is merged to main. The build workflow sets the display version from the tag and the increasing build version from the workflow run number. Both platform builds and all tests must pass. CI downloads the previous published macOS ZIP and lets Sparkle generate a signed delta for that version. Only generated deltas that Sparkle considers worthwhile are published; unmatched versions or failed delta application fall back to the full archive. Both full archives and deltas are checksummed. CI then signs the macOS ZIP with the Sparkle key, publishes the packages and appcast, and commits the updated feed to main. Tag builds fail if the signing key is missing. The workflow needs permission to push the feed commit to main.

For a manual release, configure both `FASTPIN_VERSION` and an increasing `FASTPIN_BUILD_VERSION`, package/sign the app, then run:

```sh
./make-update-feed.sh build path/to/FastPin-macOS-arm64.zip v0.1.0-preview.8 build/update-feed
```

Upload the ZIP and any generated `.delta` files under the exact versioned release URLs in the generated feed, then commit `build/update-feed/appcast.xml` as `updates/appcast.xml`. Do not publish an update before its download is available. `release-macos.sh` handles Developer ID signing/notarization; CI development previews remain ad hoc signed and may require regranting macOS permissions after replacement. Sparkle signatures authenticate updates but do not replace notarization or a stable app signing identity.

Older FastPin previews contain no updater. Users need one manual installation of the first updater-enabled build; subsequent signed builds can update in place. Windows updates remain manual.

## Verification

`ctest --test-dir build --output-on-failure` includes the native AppKit settings lifecycle and an isolated updater probe. The updater probe uses the production Sparkle delegate, temporary CryptoKit keys, a loopback feed, a separate bundle identity and temporary settings. It proves signed download, 1 → 2 replacement/relaunch, settings retention, and rejection of an invalid archive signature. Python 3 and the Xcode Swift toolchain are needed for these macOS tests. Test keys never enter the login Keychain.
