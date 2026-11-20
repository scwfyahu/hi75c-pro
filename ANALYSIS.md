# Verification analysis

Everything below was executed on this machine (Linux, no Windows, no display).
Windows code ran under Wine 9.22 inside a Debian container with Xvfb; the GUI
was captured from the virtual display with ImageMagick.

## What was executed

| # | What | How | Result |
|---|---|---|---|
| 1 | protocol core | `tests/proto_test.c` compiled and run natively | **19/19 pass** |
| 2 | the shipped exe on Windows APIs | `wine64 Hi75CPro.exe --selftest` | **13/13 pass** |
| 3 | native UI, every control | clicks injected with `xdotool`, screen captured | effects, colour, brightness, speed, restore, rescan all respond |
| 4 | the app as a window | screenshot of the real binary under Wine | renders, no browser engine |
| 5 | embedded installer | `Hi75CPro-Setup.exe --silent` under Wine | exit 0; `Hi75CPro.exe` + `installed.txt` + Start Menu shortcut created |

## Measured

| Metric | Value | Note |
|---|---|---|
| exe size | 153,600 bytes | native GDI+, no WebView2 |
| private working set under Wine | 24.1 MB | Wine inflates this; Windows not measurable here |
| hard DLL dependencies | kernel32, user32, gdi32, gdiplus, ole32, shell32, advapi32, setupapi, hid | no browser loader |
| frames per config write | 10 | each acknowledged by the device |

## Not verified

- **On real Windows.** No Windows host exists here. Wine is close but not identical.
- **Against a real keyboard.** No device attached during these runs, so the HID
  paths report "keyboard not found" honestly rather than being exercised.
- **The GitHub bootstrapper.** It builds (21 KB) but Wine's TLS stack cannot
  reach api.github.com, so the download path is unverified and is **not shipped**.
  The installer that ships carries the verified build instead.

## Rule applied

Anything that could not be executed here was not shipped as a load-bearing part.
The bootstrap installer exists in the tree but is not the release installer.
