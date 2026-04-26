# FatFs

This directory is the canonical home for FatFs-related assets used by `Picocalc_NESco`.

Current active files:

- `ffconf.h`
  - The actual configuration header used by the build
- `ff.h`, `diskio.h`
  - Vendored public headers used by the build
- `ff.c`, `ffunicode.c`
  - Vendored FatFs core sources compiled into the firmware
- `LICENSE.txt`
  - Local copy of the ChanFatFS/FatFs license text for attribution visibility

Reference repository:

- ChanFatFS: <https://github.com/rhapsodyv/ChanFatFS>

Actual implementation in the current build:

- The firmware is built from the vendored FatFs sources in this directory.
- The concrete source files compiled into `Picocalc_NESco` are:
  - `fatfs/ff.c`
  - `fatfs/ffunicode.c`
- Public headers come from:
  - `fatfs/ff.h`
  - `fatfs/diskio.h`

Attribution and license trail:

- Upstream license text is copied locally into `LICENSE.txt`
- This project-specific configuration in `ffconf.h` is for the FatFs code developed by ChaN
- In the words of the upstream license note:
  - `FatFs has being developped as a personal project of the author, ChaN. It is free from the code anyone else wrote at current release.`

Why this directory exists:

- It makes the actual in-use FatFs integration visible inside `Picocalc_NESco`
- It keeps the licensing trail next to the configuration that affects the shipped build
- It keeps the build self-contained inside `Picocalc_NESco`
