# Bundled fonts

These fonts are baked into the building signage (the `TEXTURE_LOGOS` atlas) at
startup by `RenderLoadFonts()` in `Render.cpp`. They are loaded **by file** from
this directory (resolved next to the executable, then the CWD) so the signage
looks identical on every machine instead of depending on whatever fontconfig
happens to substitute for the original Windows font names.

All are licensed under the SIL Open Font License 1.1 (see `licenses/`):

| File | Family | Character |
|------|--------|-----------|
| Anton-Regular.ttf        | Anton         | ultra-heavy grotesque (Impact-like) |
| ArchivoBlack-Regular.ttf | Archivo Black | heavy grotesque (Arial Black-like)  |
| BebasNeue-Regular.ttf    | Bebas Neue    | tall condensed caps                 |
| RussoOne-Regular.ttf     | Russo One     | bold squared display                |
| Rajdhani-Bold.ttf        | Rajdhani      | squared techno bold                 |
| SairaCondensed-Bold.ttf  | Saira Cond.   | condensed bold                      |
| Audiowide-Regular.ttf    | Audiowide     | retro-neon display                  |

To change the signage typefaces, drop a `.ttf` here and edit the `fonts[]` table
in `Render.cpp`.
