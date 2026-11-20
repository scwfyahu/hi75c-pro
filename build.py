#!/usr/bin/env python3
"""Build the single-binary controller: app.c + index.html -> Hi75CPro.exe."""
import pathlib, subprocess, sys

ZIG = "/tmp/zig/zig-linux-x86_64-0.13.0/zig"
WV2 = pathlib.Path("/tmp/wv2")          # WebView2.h + WebView2Loader.dll
HERE = pathlib.Path(__file__).resolve().parent

def keys_json() -> str:
    import re
    src = (HERE / "board.h").read_text()
    rows = re.findall(r'\{ *(-?\d+), *([\d.]+)f, *([\d.]+)f, *([\d.]+)f, *([\d.]+)f, *0x[0-9a-fA-F]+U, *"([^"]*)" *\}', src)
    items = [f'{{"x":{x},"y":{y},"w":{w},"h":{h},"label":"{label}"}}' for _, x, y, w, h, label in rows]
    return "window.__KEYS__=[" + ",".join(items) + "];"


def embed() -> int:
    page = (HERE / "index.html").read_text()
    page = page.replace("/*__KEYS__*/", keys_json())
    (HERE / "ui_built.html").write_text(page)   # inspection copy, not shipped
    data = page.encode()
    chunks = []
    for i in range(0, len(data), 2048):
        block = data[i:i+2048]
        chunks.append('"' + "".join("\\x%02x" % b for b in block) + '"')
    (HERE / "ui_embedded.h").write_text(
        "/* generated from index.html */\n"
        f"#define UI_HTML_LEN {len(data)}\n"
        "static const char UI_HTML[] =\n" + "\n".join(chunks) + ";\n")
    return len(data)

def main() -> int:
    size = embed()
    print(f"embedded UI: {size} bytes")
    out = HERE / "Hi75CPro.exe"
    subprocess.run([
        ZIG, "cc", "-target", "x86_64-windows-gnu", "-Os", "-s",
        "-Wl,--subsystem,windows", "-I", str(WV2), "-I", str(HERE),
        "-o", str(out), str(HERE / "app.c"), str(HERE / "proto.c"), str(WV2 / "WebView2Loader.dll"),
        "-lsetupapi", "-lhid", "-lole32", "-loleaut32", "-luser32",
        "-lshlwapi", "-lgdi32", "-lshell32", "-luuid", "-loleacc",
    ], check=True)
    (HERE / "ui_embedded.h").unlink(missing_ok=True)
    print(f"wrote {out.name}: {out.stat().st_size:,} bytes")
    return 0

if __name__ == "__main__":
    sys.exit(main())
