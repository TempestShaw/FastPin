"""Exercise the deployed OCR worker without the build machine's Qt on PATH."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import zlib

package = Path(sys.argv[1]).resolve()
for relative in ("FastPin.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Widgets.dll", "qt.conf",
                 "plugins/platforms/qwindows.dll", "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
    if not (package / relative).is_file():
        raise SystemExit(f"Missing deployment dependency: {relative}")


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(b"\0\xff\xff\xff\xff")) + chunk(b"IEND", b""))
env = os.environ.copy()
windows = os.environ["SystemRoot"]
env["PATH"] = os.pathsep.join((str(Path(windows) / "System32"), windows))
for key in ("QTDIR", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM_PLUGIN_PATH"):
    env.pop(key, None)
result = subprocess.run([str(package / "FastPin.exe"), "--ocr-worker"], input=png,
                        capture_output=True, timeout=15, env=env, cwd=package, check=True)
reply = json.loads(result.stdout)
if "MSIX" not in reply.get("error", ""):
    raise SystemExit(f"Unexpected worker reply: {reply}")
print("Deployment smoke passed: app-local DLLs load without Qt on PATH; unpackaged OCR explains the MSIX requirement.")
