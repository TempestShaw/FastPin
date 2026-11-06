"""Exercise the deployed OCR worker without the build machine's Qt on PATH."""
import json
import os
from pathlib import Path
import struct
import subprocess
import sys
import time
import zlib

package = Path(sys.argv[1]).resolve()
for relative in ("FastPin.exe", "Qt6Core.dll", "Qt6Gui.dll", "Qt6Network.dll", "Qt6Widgets.dll", "qt.conf",
                 "plugins/platforms/qwindows.dll", "plugins/tls/qschannelbackend.dll",
                 "msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
    if not (package / relative).is_file():
        raise SystemExit(f"Missing deployment dependency: {relative}")


def chunk(kind, data):
    return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))


png = (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 6, 0, 0, 0))
       + chunk(b"IDAT", zlib.compress(b"\0\xff\xff\xff\xff")) + chunk(b"IEND", b""))
env = os.environ.copy()
windows = os.environ["SystemRoot"]
env["PATH"] = os.pathsep.join((str(Path(windows) / "System32"), windows))
for key in ("QTDIR", "QT_PLUGIN_PATH", "QT_QPA_PLATFORM", "QT_QPA_PLATFORM_PLUGIN_PATH"):
    env.pop(key, None)
result = subprocess.run([str(package / "FastPin.exe"), "--ocr-worker"], input=png,
                        capture_output=True, timeout=15, env=env, cwd=package, check=True)
reply = json.loads(result.stdout)
if "MSIX" not in reply.get("error", ""):
    raise SystemExit(f"Unexpected worker reply: {reply}")

# The worker never loads a platform plugin, so also start the real app. A second
# process exits 0 only after the running primary answers on its local socket.
primary = subprocess.Popen([str(package / "FastPin.exe"), "--background"], env=env, cwd=package)
try:
    time.sleep(5)
    if primary.poll() is not None:
        raise SystemExit(f"FastPin exited during startup with code {primary.returncode}")
    second = subprocess.run([str(package / "FastPin.exe"), "--background"], capture_output=True,
                            timeout=15, env=env, cwd=package)
    if second.returncode != 0:
        raise SystemExit(f"The running app did not answer a second launch (code {second.returncode})")
    if primary.poll() is not None:
        raise SystemExit(f"FastPin exited after a second launch with code {primary.returncode}")
finally:
    primary.kill()
    primary.wait(timeout=10)
print("Deployment smoke passed: app-local DLLs load without Qt on PATH; unpackaged OCR explains the MSIX requirement;"
      " the app stays running on the Windows platform plugin and answers a second launch.")
