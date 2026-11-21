# Hi75C Pro controller — spec

**Idea** — A near-black instrument panel where colour is data, not decoration: chrome stays grayscale and hue appears only in the keycaps, the light-status chip and the seven swatches that drive them.
**Palette (chrome)** — `#08090A` canvas · `#0A0B0C` rail · `#0D0E10` card · `#141519` raised/active · `#1A1C1F` hairline · `#ECEDEF` primary text · `#9A9DA3` secondary · `#66696F` meta.
**Palette (light)** — seven board presets `#FF2B2B` red · `#FF8C1A` orange · `#FFE11A` yellow · `#2EE65C` green · `#1AE5FF` cyan · `#2E6BFF` blue · `#B833FF` purple; the selected one (`#FF8C1A` here) becomes the single accent on light-bearing elements only.
**Type** — Inter → SF Pro → Segoe UI system stack, mono for values; 22/600 title, 12/500 nav and copy, 11.5/600 section titles, 11/500 labels, 10.5/500 effect names, 9.5/400 meta, 8.5 mono eyebrows (8px indices), cap legends 8.5–10.5/500; 400–600 only.
**Spacing** — 4px base; 8/12/16/20/28 working steps, 28px main frame, 20px gutters, 16px card padding; 46px key pitch with a 5px cap gap.
**Radius / elevation** — 10px cards, 8px summary and swatches, 6px controls/tiles, 5px keycaps; elevation is 1px hairlines plus one soft shadow under the keyboard plate; caps get inset bevels, nothing else casts a shadow.
**Selected / active** — surface lift + 1px lighter border + 600 weight + near-white text; the active swatch gets a 1px grayscale ring offset 3px; the accent never marks selection.
**Left out** — save/apply ceremony (writes are immediate), import/restore dialogs, profile management, custom-colour and per-key painting, remap editing, responsive/light variants, OS window chrome, decorative illustration, looping animation.
