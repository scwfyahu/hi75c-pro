# LEOBOG Hi75C Pro — native Windows driver

Controls the RGB lighting and the keymap of a LEOBOG Hi75C Pro over HID.
No WebView, no Electron, no driver service, no installer framework: one
Win32/GDI+ executable that talks to the keyboard directly.

## What it does

- **Lighting** — 17 firmware effects, 7 colours, brightness and speed, applied to the board.
  The window shows a live per-key preview of what the board is doing, dimmed and labelled
  when no keyboard is attached.
- **Keymap** — click any key, type a HID usage code, press Enter. Base and FN layers are
  editable; a usage-code reference sits on the page.
- **Background driver** — `hi75c-daemon.exe` runs an animated effect without the window
  (about 34 KB, no CRT, no GDI). Only one instance can run; the window asks it to exit
  before writing.

## Files

| file | size | what it is |
|---|---|---|
| `Hi75CPro.exe` | 212,480 B | the controller (Win32 + GDI+) |
| `Hi75CPro-Setup.exe` | 24,576 B | bootstrapper: fetches the latest binary from the GitHub release, verifies it is a PE, copies it, makes a shortcut, registers an uninstall entry |
| `hi75c-daemon.exe` | 34,816 B | standalone background effect runner |

Build both with:

```
python3 build.py            # all three artifacts, reproducible byte-for-byte
```

## Protocol

Reports are 20 bytes: `[0x13][cmd][count][index][(sub<<4)|len][14 bytes payload][checksum]`,
where `checksum = sum(bytes 0..18) & 0xFF`. Commands: `0x01`/`0x41` keymap write/read,
`0x02`/`0x42` RGB profile, `0x04`/`0x44` config, `0x09`/`0x49` palette.

Every written packet must be acknowledged by the device echo before the next is sent, and
every reply is checksum-validated before it is trusted. The keymap is 126 entries × 4 bytes
in column-major order; the config carries the effect id at byte 10, the custom flag at 9 and
the effect slots at `58 + (id-1)*2`.

## Tests

```
./run-tests.sh              # builds everything, audits the artifacts, runs the suites
```

Suites: protocol (20 checks, simulated device incl. corrupted replies), expression engine
(20 checks), version comparison (9 checks). The GUI is exercised by a click harness that
reads the app's own hotspot dump, converts logical→physical coordinates for the current
display scale, and screenshots each control.

## Limits worth knowing

- Effect previews are an approximation of the firmware animations, not a pixel-accurate
  mirror; the board is the source of truth.
- Settings persist to `%LOCALAPPDATA%\Hi75C Pro\state.ini`; the loader validates every
  field, so a corrupted file cannot push invalid values to the keyboard.
- The installer is unsigned, so Windows SmartScreen will warn on first run.
