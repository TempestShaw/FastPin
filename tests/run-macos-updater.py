#!/usr/bin/env python3
"""Signed 1 -> 2 replacement/relaunch and invalid-signature rejection, using the production delegate."""
import base64
import functools
import http.server
import json
from pathlib import Path
import plistlib
import shutil
import subprocess
import sys
import tempfile
import threading
import time
import uuid
import xml.etree.ElementTree as ET


def run(*args):
    return subprocess.run(args, check=True, capture_output=True, text=True).stdout


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, *args):
        pass


def main():
    executable, sparkle = map(lambda p: Path(p).resolve(), sys.argv[1:3])
    results = {}
    with tempfile.TemporaryDirectory(prefix="fastpin-updater-") as folder:
        root = Path(folder)
        # CryptoKit ships with the macOS SDK; test keys never enter the login Keychain.
        run("swift", "-e", """
import CryptoKit
import Foundation
let key = Curve25519.Signing.PrivateKey()
try key.rawRepresentation.base64EncodedString().write(toFile: CommandLine.arguments[1], atomically: true, encoding: .utf8)
try key.publicKey.rawRepresentation.base64EncodedString().write(toFile: CommandLine.arguments[2], atomically: true, encoding: .utf8)
""", str(root / "private-key"), str(root / "public-key"))
        public_key = (root / "public-key").read_text()
        for mode in ("restart", "quit", "reject"):
            reject = mode == "reject"
            case = root / mode
            serve = case / "feed"
            serve.mkdir(parents=True)
            result = case / "result.txt"
            identifier = "io.github.tempestshaw.fastpin.updater-test." + uuid.uuid4().hex
            server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), functools.partial(QuietHandler, directory=str(serve)))
            threading.Thread(target=server.serve_forever, daemon=True).start()

            def bundle(parent, version):
                app = parent / "FastPinUpdateTest.app"
                contents = app / "Contents"
                (contents / "MacOS").mkdir(parents=True)
                (contents / "Frameworks").mkdir()
                shutil.copy2(executable, contents / "MacOS" / "FastPinUpdateTest")
                shutil.copytree(sparkle / "Sparkle.framework", contents / "Frameworks" / "Sparkle.framework", symlinks=True)
                info = dict(CFBundleExecutable="FastPinUpdateTest", CFBundleIdentifier=identifier,
                            CFBundleName="FastPinUpdateTest", CFBundlePackageType="APPL", CFBundleVersion=version,
                            CFBundleShortVersionString=version + ".0", LSMinimumSystemVersion="14.0", LSUIElement=True,
                            SUFeedURL=f"http://127.0.0.1:{server.server_port}/appcast.xml", SUPublicEDKey=public_key,
                            SUEnableAutomaticChecks=True, SUAutomaticallyUpdate=True, SUEnableSystemProfiling=False,
                            SmokeQuitNormally=mode == "quit",
                            SmokeResultPath=str(result), SmokeSettingsPath=str(case / "settings.ini"),
                            NSAppTransportSecurity={"NSAllowsLocalNetworking": True})
                with (contents / "Info.plist").open("wb") as file:
                    plistlib.dump(info, file)
                run("codesign", "--force", "--deep", "--sign", "-", str(app))
                return app

            installed = bundle(case / "installed", "1")
            candidate = bundle(case / "candidate", "2")
            # Distinct filenames keep generate_appcast's extraction cache independent between cases.
            archive = serve / (identifier + ".zip")
            run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(candidate), str(archive))
            run(str(sparkle / "bin" / "generate_appcast"), "--ed-key-file", str(root / "private-key"),
                "--maximum-deltas", "0", "--download-url-prefix", f"http://127.0.0.1:{server.server_port}/", str(serve))
            if reject:
                feed = ET.parse(serve / "appcast.xml")
                feed.find("./channel/item/enclosure").set("{http://www.andymatuschak.org/xml-namespaces/sparkle}edSignature", base64.b64encode(bytes(64)).decode())
                feed.write(serve / "appcast.xml", encoding="utf-8", xml_declaration=True)
            log = (case / "stderr.log").open("w")
            process = subprocess.Popen([str(installed / "Contents" / "MacOS" / "FastPinUpdateTest")], stdout=log, stderr=log)
            try:
                deadline = time.monotonic() + 55
                while time.monotonic() < deadline:
                    lines = result.read_text().splitlines() if result.exists() else []
                    if (reject and any(line.startswith("rejected ") for line in lines)) or "settings preserved" in lines:
                        break
                    if mode == "quit" and process.poll() == 0:
                        info = installed / "Contents" / "Info.plist"
                        if info.exists() and plistlib.loads(info.read_bytes())["CFBundleVersion"] == "2":
                            assert lines == ["launched 1", "verified download"], lines
                            run(str(installed / "Contents" / "MacOS" / "FastPinUpdateTest"))
                            break
                    if "timeout" in lines or (process.poll() not in (None, 0)):
                        break
                    time.sleep(0.1)
                lines = result.read_text().splitlines() if result.exists() else []
                with (installed / "Contents" / "Info.plist").open("rb") as file:
                    version = plistlib.load(file)["CFBundleVersion"]
                if reject:
                    assert any(line.startswith("rejected ") for line in lines) and version == "1" and "verified download" not in lines, (lines, version)
                else:
                    assert lines == ["launched 1", "verified download", "launched 2", "settings preserved"] and version == "2", (lines, version)
                results[mode] = lines
            except Exception:
                print((case / "stderr.log").read_text(), file=sys.stderr)
                raise
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)
                log.close()
                server.shutdown()
                server.server_close()
    print(json.dumps(results, indent=2))


if __name__ == "__main__":
    main()
