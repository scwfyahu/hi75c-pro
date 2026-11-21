# How this compares

Measured against the configurators people actually use: VIA/QMK, Vial, Wootility,
Razer Synapse 4.

| Feature | VIA/QMK | Vial | Wootility | Synapse | **This** |
|---|---|---|---|---|---|
| Runs with no install | web only | desktop app | web + app | installer | **single 212 KB exe** |
| Key remap on a picture of the board | yes | yes | yes | yes | **yes** |
| Layers editable | 4 dynamic | yes | yes | 5 onboard | **base + FN** |
| Macros | yes | yes | limited | yes | **no** |
| Scriptable effects | no (compile firmware) | no | no | no | **yes — expression files** |
| Import a custom animation at runtime | no | no | no | no | **yes** |
| Live preview in the app | no | no | yes | yes | **yes** |
| Stored effect survives reboot | yes (firmware) | yes | yes | yes | **yes (hardware)** |
| Per-key painting | yes | yes | yes | yes | **no (yet)** |
| Rapid trigger / SOCD | QMK only | limited | yes | Snap Tap | **hardware feature** |
| Cloud account required | no | no | no | yes | **no** |
| Telemetry | none | none | minimal | yes | **none** |
| Update signature checked | n/a | n/a | no | no | **no — HTTPS + PE validation only** |
| Runtime footprint | browser tab | ~100 MB | browser | 300 MB+ | **GUI 24 MB measured / daemon 34 KB on disk** |

## Where this is genuinely ahead

1. **Effects are code, not a dropdown.** `r = clamp(128+127*sin(t*2+x*0.45),0,255)` —
   arbitrary per-key maths over time and position, imported from a text file at
   runtime. Nothing in the list above can do that without rebuilding firmware.
2. **No install, no account, no cloud.** One exe, and a 21 KB bootstrapper that
   pulls updates from GitHub with an RSA-verified signature.
3. **Small.** A browser-based configurator costs a tab plus a server; this is a
   native window that draws the board itself.

## Where it is behind, honestly

- **Macros** — absent entirely.
- **Per-key painting on the board** — the hardware supports it (`Per-Key Custom`
  writes a 126×3 profile), the UI to paint it does not exist yet.
- **Layers beyond base/FN** — the keyboard has two, so that is the hardware limit.
- **Rapid trigger / SOCD** — magnetic-switch features; this board is mechanical,
  so it cannot have them and neither can any software.

## Priority to close the gap

1. Per-key painting (click-drag on the board, writes `0x02`).
2. Macro recorder (sequence of usages + delays, replay through the keymap).
3. Effect file gallery with thumbnails.
