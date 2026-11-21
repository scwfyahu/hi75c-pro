# LEOBOG Studio — Hi75C Pro lighting controller

**Idea** — The board is the page: one 620 px live stage holds the effect name, the only keycap preview and a pinned running summary; a 328 px inspector stays beside it, so effect, colour, brightness, speed and presets change in view, with no scroll or hunt.
**Layout** — 56 px title bar over 188 px rail / 664 px stage (620 px card) / 328 px inspector = 1180 × 900; the rail carries sections and device status, the inspector is the only control surface.
**Palette** — `#0A0C10` stage, `#0C0F14` rail and console, `#11151B` controls, `#1C2028` keycaps, `#232833` hairlines, `#F2F4F8` text, `#98A1AF` secondary, `#596372` micro labels, `#FF4D8D` live accent / Rose, `#5BD6A2` connected. Colour is content: chrome stays neutral so the backlight reads first.
**Type** — Segoe UI Variable; 30/600 effect title, 23/600 page H1, 11–12.5 UI, 9.5 control labels, 8.5 uppercase micro (0.16 em); Cascadia Mono for hex, %, fps and time, tabular throughout.
**Spacing** — 4-based rhythm of 6 / 9 / 14 / 18 / 22; 20 px rail and inspector insets, 22 px stage inset; 30 px controls, 6 px chip gap, 2 px keycap gap.
**Radii & elevation** — 16 card, 10 rail cards, 8 buttons and presets, 6 effect chips, 5 keycaps, 3 inline bars; depth is a 1 px light border plus inset top highlight and a deep soft shadow; glow belongs to the board, the selected swatch and the live dots.
**Active states** — rose 2 px rail tick; selected effect = tinted fill, rose border and live pin; selected swatch = page-colour gap plus warm-white ring; selected preset = rose border and check; sliders = rose fill and white thumb; everything else stays neutral until hover.
**Omissions** — Keymap and About are rail destinations only; no key editor, macros or per-key painting; controls are drawn states with no JS or HID; window chrome is inert; the board uses the Hi75C's real 75% matrix (F-row, arrows, knob) because that is the layout the required keys describe.
