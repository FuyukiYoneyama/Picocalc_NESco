# Fonts

This directory is the canonical home for font assets used by `Picocalc_NESco`.

Current active menu font:

- `menu_font_pixelmplus.h`
  - Generated bitmap header used by the ROM menu at build time
  - Derived from the PixelMplus bitmap source
  - Source BDF: `../PixelMplus/src/bdf.d/mplus_j10r-iso-W5.bdf`
  - Local license copy: `LICENSE.txt`

Reference repository:

- PixelMplus: <https://github.com/itouhiro/PixelMplus>

Attribution:

- PixelMplus / M+ bitmap font derived data
- Copyright (C) 2002-2013 M+ FONTS PROJECT

Why this is embedded as a header:

- PicoCalc menu rendering uses a tiny bitmap font table directly from flash/RAM.
- Runtime BDF/TTF parsing is avoided on RP2040.
- Keeping the generated header in this directory makes the origin and the actual in-use asset visible in one place.
