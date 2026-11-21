#!/bin/bash
# Full test run. Every command, its output, and its exit code are recorded.
set -u
LOG=/DATA/AppData/dsh-raw/home/Testing/hi75c/test-run.log
: > "$LOG"
step() { printf "\n$ %s\n" "$*" | tee -a "$LOG"; }
run()  { "$@" 2>&1 | tee -a "$LOG"; printf "[exit %s]\n" "${PIPESTATUS[0]}" | tee -a "$LOG"; }

ZIG=/tmp/zig/zig-linux-x86_64-0.13.0/zig
export WINEPREFIX=/DATA/AppData/dsh-raw/home/.wineprefix WINEDEBUG=-all
export LD_LIBRARY_PATH=/tmp/wine/lib:/tmp/wine/lib64 WINEDLLOVERRIDES="winex11.drv="

printf "Hi75C Pro — full test run\n" | tee -a "$LOG"
printf "started %s\n" "$(date -Iseconds)" | tee -a "$LOG"

step "build the shipped binary"
run python3 build.py

step "version comparison suite (update path)"
run $ZIG cc -target x86_64-linux-gnu -O1 -I. -o /tmp/vertest tests/version_test.c
run /tmp/vertest

step "protocol suite (compiles the same proto.c the exe links)"
run $ZIG cc -target x86_64-linux-gnu -O1 -I. -o /tmp/prototest tests/proto_test.c proto.c
run /tmp/prototest

step "expression engine (drives imported effect files)"
run $ZIG cc -target x86_64-linux-gnu -O1 -I. -o /tmp/exprtest tests/expr_test.c expr.c -lm
run /tmp/exprtest

step "expression rejection suite (malformed formulas must not silently evaluate)"
run $ZIG cc -target x86_64-linux-gnu -O1 -I. -o /tmp/rejects tests/expr_reject_test.c expr.c -lm
run /tmp/rejects

step "shipped self-test — the binary users actually run"
run /tmp/wine/bin/wine64 ./Hi75CPro.exe --selftest

step "artefact audit — the file that gets downloaded"
run python3 - <<'PY'
import hashlib, struct, zipfile
p = "/DATA/AppData/dsh-raw/home/Testing/hi75c/Hi75CPro.exe"
d = open(p, "rb").read()
pe = struct.unpack_from("<I", d, 0x3c)[0]
nsec = struct.unpack_from("<H", d, pe+6)[0]; optsz = struct.unpack_from("<H", d, pe+20)[0]
so = pe+24+optsz
end = max(struct.unpack_from("<II", d, so+i*40+16)[1] + struct.unpack_from("<II", d, so+i*40+16)[0] for i in range(nsec))
print(f"exe        : {len(d):,} bytes")
print(f"PE header  : {'ok' if d[:2]==b'MZ' else 'BAD'}   subsystem {struct.unpack_from('<H', d, pe+24+68)[0]} (2=GUI)")
print(f"overlay    : {len(d)-end} bytes")
print(f"sha256     : {hashlib.sha256(d).hexdigest()}")
setup = "/DATA/AppData/dsh-raw/home/Testing/hi75c/Hi75CPro-Setup.exe"
sd = open(setup, "rb").read()
spe = struct.unpack_from("<I", sd, 0x3c)[0]
print(f"setup      : {len(sd):,} bytes  PE {'ok' if sd[:2]==b'MZ' else 'BAD'}  sha256 {hashlib.sha256(sd).hexdigest()[:16]}")
dmn = "/DATA/AppData/dsh-raw/home/Testing/hi75c/hi75c-daemon.exe"
dd = open(dmn, "rb").read()
print(f"daemon     : {len(dd):,} bytes  PE {'ok' if dd[:2]==b'MZ' else 'BAD'}  sha256 {hashlib.sha256(dd).hexdigest()[:16]}")
PY

printf "\nfinished %s\n" "$(date -Iseconds)" | tee -a "$LOG"
echo
echo "log: $LOG"
