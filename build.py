#!/usr/bin/env python3
"""Build what actually ships.

Hi75CPro.exe  - native Win32 + GDI+ controller (app.c, proto.c, ui_native.cpp, expr.c)
hi75c-daemon  - standalone background effect runner (daemon.c, no CRT, no GDI)
"""
import pathlib, struct, subprocess, sys

ZIG = "/tmp/zig/zig-linux-x86_64-0.13.0/zig"
HERE = pathlib.Path(__file__).resolve().parent
TARGET = "x86_64-windows-gnu"

BASE = [ZIG, "cc", "-target", TARGET, "-Os", "-I", str(HERE)]
GUI_LIBS = ["-lgdiplus", "-lgdi32", "-luser32", "-lshell32", "-lole32", "-ladvapi32",
            "-lsetupapi", "-lhid", "-luuid", "-lcomdlg32", "-lbcrypt", "-lwinhttp", "-lurlmon"]
DAEMON_LIBS = ["-lhid", "-lsetupapi", "-luser32"]


def normalise_pe(path):
    """Zero the timestamps so two builds of the same source are identical.

    Without this the only difference between successive builds is the PE
    timestamp, which makes "does this download match the source?" impossible
    to answer by hash.
    """
    d = bytearray(path.read_bytes())
    pe = struct.unpack_from("<I", d, 0x3C)[0]
    struct.pack_into("<I", d, pe + 8, 0)                       # COFF time stamp
    nsec = struct.unpack_from("<H", d, pe + 6)[0]
    optsz = struct.unpack_from("<H", d, pe + 20)[0]
    so = pe + 24 + optsz
    for i in range(nsec):
        struct.pack_into("<I", d, so + i * 40 + 8, 0)          # section time stamp
    struct.pack_into("<I", d, pe + 24 + 64, 0)                 # header checksum
    path.write_bytes(d)


def run(args, what):
    print(f"  {what}")
    subprocess.run(args, check=True)


def main() -> int:
    gui = HERE / "Hi75CPro.exe"
    daemon = HERE / "hi75c-daemon.exe"

    print("app (C)", flush=True)
    run(BASE + ["-c", str(HERE / "app.c"), "-o", "/tmp/build_app.o"], "app.c")
    run(BASE + ["-c", str(HERE / "proto.c"), "-o", "/tmp/build_proto.o"], "proto.c")
    run(BASE + ["-c", str(HERE / "verify.c"), "-o", "/tmp/build_verify.o"], "verify.c")
    run(BASE + ["-c", str(HERE / "expr.c"), "-o", "/tmp/build_expr.o"], "expr.c")
    print("ui (C++)", flush=True)
    run([ZIG, "c++", "-target", TARGET, "-Os", "-I", str(HERE), "-fno-exceptions",
         "-c", str(HERE / "ui_native.cpp"), "-o", "/tmp/build_ui.o"], "ui_native.cpp")
    print("link", flush=True)
    run([ZIG, "c++", "-target", TARGET, "-Os", "-s", "-Wl,--subsystem,windows",
         "-o", str(gui), "/tmp/build_app.o", "/tmp/build_proto.o", "/tmp/build_ui.o",
         "/tmp/build_expr.o", "/tmp/build_verify.o", "-fno-exceptions"] + GUI_LIBS, "Hi75CPro.exe")

    print("installer", flush=True)
    setup = HERE / "Hi75CPro-Setup.exe"
    run([ZIG, "cc", "-target", TARGET, "-Os", "-municode", "-I", str(HERE),
         "-c", str(HERE / "setup2.c"), "-o", "/tmp/build_setup.o"], "setup2.c")
    run([ZIG, "cc", "-target", TARGET, "-Os", "-s", "-municode",
         "-Wl,--subsystem,windows", "-o", str(setup), "/tmp/build_setup.o", "/tmp/build_verify.o",
         "-lwinhttp", "-lurlmon", "-lole32", "-luuid", "-lshell32", "-ladvapi32", "-luser32", "-lbcrypt"],
        "Hi75CPro-Setup.exe")

    print("daemon", flush=True)
    run([ZIG, "cc", "-target", TARGET, "-Os", "-s", "-Wl,--subsystem,windows",
         "-o", str(daemon), str(HERE / "daemon.c")] + DAEMON_LIBS, "hi75c-daemon.exe")

    normalise_pe(gui)
    normalise_pe(daemon)
    normalise_pe(setup)

    print(f"\nHi75CPro.exe      {gui.stat().st_size:,} bytes")
    print(f"hi75c-daemon.exe  {daemon.stat().st_size:,} bytes")
    print(f"Hi75CPro-Setup.exe {setup.stat().st_size:,} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
