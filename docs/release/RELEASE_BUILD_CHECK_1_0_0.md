# Release Build Check 1.0.0

Date: 2026-04-26

## Version

- `platform/version.h`: `PICOCALC_NESCO_VERSION "1.0.0"`
- `README.md`: current embedded version `1.0.0`

## Build

Command:

```sh
cmake --build build -j4
```

Result:

- success
- banner: `PicoCalc NESco Ver. 1.0.0 Build Apr 26 2026 13:14:29`

Size:

```text
   text    data     bss     dec     hex filename
 266220       0   92772  358992   57a50 build/Picocalc_NESco.elf
```

## Artifacts

Files:

- `build/Picocalc_NESco.elf`
  - size: `2118408`
  - SHA-256: `689465028f22e18803e4eff916f6f7b38a7d8d4ebb45d32d68f12376fefd0270`
- `build/Picocalc_NESco.uf2`
  - size: `527872`
  - SHA-256: `50f6b2aafeadcdf473c45d1515345bea6b98ba3668a659350aa92362bd41fd1e`
- `Picocalc_NESco-1.0.0.uf2`
  - size: `527872`
  - SHA-256: `50f6b2aafeadcdf473c45d1515345bea6b98ba3668a659350aa92362bd41fd1e`

## Public File Checks

Tracked ROM / save / compressed ROM candidate check:

```sh
git ls-files | rg -in '\.(nes|fds|srm|m30|zip|7z|rar)$' || true
```

Result:

- no matches

Tracked README images:

```text
docs/images/mapper30_tower_normal.png
docs/images/mapper30_tower_stretch.png
docs/images/readme_candidates/.gitkeep
docs/images/rom_menu.png
```

Notes:

- `Picocalc_NESco-1.0.0.uf2` is a release artifact and is ignored by `.gitignore`.
- Source archive should use the GitHub tag archive by default.
- If a manual source archive is needed, use `git archive` as described in `docs/release/RELEASE_ARTIFACT_PROCEDURE.md`.
