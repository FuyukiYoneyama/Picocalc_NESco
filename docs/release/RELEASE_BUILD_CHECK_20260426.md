# Release Build Check

この文書は、公開準備作業後の build 確認結果です。

## build

- command: `cmake --build build -j4`
- result: success

## generated files

- `build/Picocalc_NESco.elf`
  - size: `2118408` bytes
  - SHA-256: `e4071f4e1138a4f42d41ca741699b1cc27f7c3a0ce59b7665ad5577c480d48a8`
- `build/Picocalc_NESco.uf2`
  - size: `527872` bytes
  - SHA-256: `44744f0e29e6c1ab8b1aa2958e1a443ac1c2e00f3dbb6d861d544fdf3f5eae07`

## size

`arm-none-eabi-size build/Picocalc_NESco.elf`:

- `text=266220`
- `data=0`
- `bss=92772`
- `dec=358992`
- `hex=57a50`

## embedded banner

`PicoCalc NESco Ver. 0.4.5 Build Apr 26 2026 12:45:49`

## file mix-in check

tracked ROM / save / compressed ROM candidates:

```sh
git ls-files | rg -in '\.(nes|fds|srm|m30|zip|7z|rar)$' || true
```

result: no matches.

tracked images:

- `docs/images/mapper30_tower_normal.png`
- `docs/images/mapper30_tower_stretch.png`
- `docs/images/readme_candidates/.gitkeep`
- `docs/images/rom_menu.png`

## note

This is a build check for the current `0.4.5` source state after public
preparation cleanup. It is not a `1.0.0` release artifact.
