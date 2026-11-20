#!/usr/bin/env python3
import pathlib, subprocess
ZIG = "/tmp/zig/zig-linux-x86_64-0.13.0/zig"
WV2 = pathlib.Path("/tmp/wv2")
H = pathlib.Path(__file__).resolve().parent
def blob(name, data):
    chunks = [f'"{ "".join("\\x%02x" % b for b in data[i:i+2048]) }"' for i in range(0, len(data), 2048)]
    return f"static const unsigned char {name}[] =\n" + "\n".join(chunks) + ";\n"
app = (H / "Hi75CPro.exe").read_bytes()
dll = (WV2 / "WebView2Loader.dll").read_bytes()
(H / "setup_payload.h").write_text(blob("APP_BYTES", app) + blob("DLL_BYTES", dll) +
    f"#define APP_LEN {len(app)}\n#define DLL_LEN {len(dll)}\n")
subprocess.run([ZIG, "cc", "-target", "x86_64-windows-gnu", "-Os", "-s",
                "-Wl,--subsystem,windows", "-o", str(H / "Hi75CPro-Setup.exe"),
                str(H / "setup.c"), "-lole32", "-luuid", "-lshell32", "-ladvapi32", "-luser32"], check=True)
(H / "setup_payload.h").unlink(missing_ok=True)
print("wrote Hi75CPro-Setup.exe:", (H / "Hi75CPro-Setup.exe").stat().st_size, "bytes")
