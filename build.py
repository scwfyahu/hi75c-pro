#!/usr/bin/env python3
"""Build what actually ships.

Hi75CPro.exe  - native Win32 + GDI+ controller (app.c, proto.c, ui_native.cpp, expr.c)
hi75c-daemon  - standalone background effect runner (daemon.c, no CRT, no GDI)
"""
import pathlib, subprocess, sys

ZIG = "/tmp/zig/zig-linux-x86_64-0.13.0/zig"
HERE = pathlib.Path(__file__).resolve().parent
TARGET = "x86_64-windows-gnu"

BASE = [ZIG, "cc", "-target", TARGET, "-Os", "-I", str(HERE)]
GUI_LIBS = ["-lgdiplus", "-lgdi32", "-luser32", "-lshell32", "-lole32", "-ladvapi32",
            "-lsetupapi", "-lhid", "-luuid", "-lcomdlg32", "-lwinhttp", "-lurlmon"]
DAEMON_LIBS = ["-lhid", "-lsetupapi", "-luser32"]


def run(args, what):
    print(f"  {what}")
    subprocess.run(args, check=True)


def main() -> int:
    gui = HERE / "Hi75CPro.exe"
    daemon = HERE / "hi75c-daemon.exe"

    print("app (C)", flush=True)
    run(BASE + ["-c", str(HERE / "app.c"), "-o", "/tmp/build_app.o"], "app.c")
    run(BASE + ["-c", str(HERE / "proto.c"), "-o", "/tmp/build_proto.o"], "proto.c")
    run(BASE + ["-c", str(HERE / "expr.c"), "-o", "/tmp/build_expr.o"], "expr.c")
    print("ui (C++)", flush=True)
    run([ZIG, "c++", "-target", TARGET, "-Os", "-I", str(HERE), "-fno-exceptions",
         "-c", str(HERE / "ui_native.cpp"), "-o", "/tmp/build_ui.o"], "ui_native.cpp")
    print("link", flush=True)
    run([ZIG, "c++", "-target", TARGET, "-Os", "-s", "-Wl,--subsystem,windows",
         "-o", str(gui), "/tmp/build_app.o", "/tmp/build_proto.o", "/tmp/build_ui.o",
         "/tmp/build_expr.o", "-fno-exceptions"] + GUI_LIBS, "Hi75CPro.exe")

    print("daemon", flush=True)
    run([ZIG, "cc", "-target", TARGET, "-Os", "-s", "-Wl,--subsystem,windows",
         "-o", str(daemon), str(HERE / "daemon.c")] + DAEMON_LIBS, "hi75c-daemon.exe")

    print(f"\nHi75CPro.exe      {gui.stat().st_size:,} bytes")
    print(f"hi75c-daemon.exe  {daemon.stat().st_size:,} bytes")
    return 0


if __name__ == "__main__":
    sys.exit(main())
