LEOBOG Hi75C Pro — single-binary controller
===========================================

Run Hi75CPro.exe. Keep WebView2Loader.dll beside it.

First launch creates a Start Menu shortcut, so you only do this once.

Requires the Microsoft Edge WebView2 Runtime (already present on Windows 11
and updated Windows 10). If it is missing the app says so instead of failing
silently.

What it does
  - detects the keyboard over USB (258a:010c) or the 2.4 GHz dongle (3554:fa09)
  - applies the 16 stored lighting effects, colour, brightness and speed
  - Restore keymap writes the factory keymap back (fixes dead keys)
  - Rescan re-opens the device after replugging

Notes
  - Reads only work while the keyboard is awake; press a key if a write fails.
  - The window says "not connected" when no board is found.

Files
  Hi75CPro.exe        53 KB   the application, UI included
  WebView2Loader.dll 162 KB   Microsoft redistributable loader
