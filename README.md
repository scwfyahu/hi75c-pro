# Hi75C Pro controller

Open-source controller for the LEOBOG Hi75C / Hi75C Pro keyboard.

- single 56 KB binary, UI compiled in (WebView2)
- falls back to your browser if WebView2 is unavailable
- stored lighting effects, colour, brightness, speed
- "Restore keymap" rewrites the factory keymap (fixes dead keys)

## Build

```
python3 build.py      # app.c + proto.c + index.html -> Hi75CPro.exe
./run-tests.sh        # full test run, writes test-run.log
```

## Tests

| suite | what it proves |
|---|---|
| `tests/proto_test.c` | framing, checksums, 10/27/36-packet reads, per-packet ACKs, effect maths, factory keymap (19 checks) |
| `Hi75CPro.exe --selftest` | real Windows APIs under Wine: HID enumeration, keymap build, shortcut creation (13 checks) |
| Chromium UI run | every control sends the right command, state renders (15 checks) |

## Protocol

Report `0x13`, 20-byte frames, checksum at byte 19 = sum(0..18). Every written
packet must be acknowledged by the device before the next is sent. Config is
128 bytes with `5A A5` at 126/127 and the effect id at byte 10.
