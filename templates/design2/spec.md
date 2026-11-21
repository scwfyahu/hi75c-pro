**Idea:** A dark, instrument-like Hi75C Pro controller where the board's coral light spills into the chrome, rail and active controls — premium and calm rather than RGB-gamer loud.
**Palette:** `#08090C` app void · `#0C0D11` panel · `#F4EFE9` primary ink · `#8C867F` secondary · `#FF7A45` live coral accent · `#63E3A6` connected · `#1C1D23` keycap core · swatch accents `#FFB84D` / `#B8E36B` / `#5BE0C2` / `#5AB8FF` / `#A58BFF` / `#FF70A6`.
**Type:** system Segoe UI stack, Cascadia/Consolas for technical labels; 21px page title / 19px hero effect / 12–13px device and running state / 9.5–11px controls, tiles, keycaps / 7.5–8.5px uppercase kickers and mono hex; tabular numerals.
**Spacing:** 4px base; 8px effect gaps; 11–16px card padding; 16px gutters; 18–24px page margins; 44/48px rail rows.
**Radii & elevation:** 15px hero, 14px panels, 7–11px controls/tiles/rows, 5px keycaps, full pills; 1px hairlines, inset top highlight, deep 26–44px shadows, coral bloom only on hero, selected and running surfaces.
**Active state:** selected effect gets a 2px coral left edge, coral-tinted fill, warm 1px border and restrained glow; active nav mirrors it; selected colour swatch is ringed by a 2px dark gap plus a 1px white ring.
**Motion:** 3.2s pulse on live/connected dots; 150–160ms hover and selection transitions; disabled under `prefers-reduced-motion`.
**Build:** one 1180×900 HTML file; inline CSS and inline SVG icons only; system fonts; no scripts, raster images, CDNs or external assets.
**Left out:** Keymap and About page content, the per-key editor, import/restore dialogs, error/loading states, responsive layouts and other window sizes.
