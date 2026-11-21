# Hi75C Pro · Lighting — design spec
**Idea** — A machined control surface, not a dashboard: cool graphite plates, engraved micro-labels, hairline seams and knurled geometry, with one orange signal that always means “live”. The 82-key board is the hero.
**Structure** — 172px rail (sections + connection) / 960px workspace; 348px live preview + 348px output card; full-width 17-effect matrix (4×4 + per-key bar); 54px summary/actions.
**Palette** — #0A0D0F chassis · #0E1214 canvas · #151A1D card · #1B2124 raised/keys · #252D31 hairline · #EDF2F4 ink · #8A969C secondary · #626E75 muted · #FF5A2B signal (active, status, light).
**Content colour** — #FFC15A · #C3F04E · #58E3C2 · #5FB8FF · #9C82FF · #EEF3F5 are swatch/LED colours only, never chrome.
**Type** — Segoe UI Variable: 24px title / 11.5px nav / 11px option / 10px keycap / 9.5px data / 8.5px card label; Cascadia–Consolas 7–9px for numerals, hex and engraved hardware text; labels tracked +0.16em.
**Spacing** — 4px base with 9–11px optical rhythm inside cards; 16px card padding and gutters; 22–24px frame inset; 648 + 16 + 296 top grid on a 960px workspace.
**Radii/elevation** — 4px window, 3px cards and keycaps, 2px controls, 1px tags; 1px hairlines and inset top highlights instead of drop shadows; only the recessed deck blooms orange.
**Active state** — 1px #FF5A2B edge + 2px inset signal bar + lit 4px LED; selected swatches get a 2px offset ring; identical grammar on nav, effects and presets.
**Omissions** — Keymap/About content, import and restore dialogs, per-key editor, tooltips/hover copy, scroll and error states, responsive behaviour, light theme.
