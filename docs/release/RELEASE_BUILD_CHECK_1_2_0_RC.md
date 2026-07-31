# Release Build Check 1.2.0 RC

Date: 2026-08-01

この文書は最終公開前の実機smokeに渡すrelease candidate buildの確認記録です。
tagへ添付するfinal artifactは、smoke合格とrelease commit確定後に改めて作成します。

## Version

- `platform/version.h`: `PICOCALC_NESCO_VERSION "1.2.0"`
- README current embedded version: `1.2.0`

## Configure

```sh
cmake -S . -B build-release \
  -DCMAKE_BUILD_TYPE=Release \
  -DPICO_SDK_PATH=/home/fuyuki/pico/pico-sdk \
  -DNESCO_RUNTIME_LOGS=OFF \
  -DNESCO_INPUT_IO_LOGS=OFF \
  -DNESCO_BOKOSUKA_STATE_LOGS=OFF \
  -DNESCO_CORE1_BASELINE_LOG=OFF \
  -DNESCO_BG_TILE_SHARE_LOG=OFF \
  -DNESCO_PALETTE_SNAPSHOT_LOG=OFF \
  -DNESCO_SPRITE_ACTIVE_LIST_METRICS=OFF \
  -DNESCO_SPRITE_ACTIVE_LIST=ON
cmake --build build-release --clean-first -j4
```

Result: success

Banner:

```text
PicoCalc NESco Ver. 1.2.0 Build Aug  1 2026 08:33:30
```

Size:

```text
   text    data     bss     dec     hex
 278844       0   97548  376392   5be48
```

## Artifacts

- `build-release/Picocalc_NESco.elf`
  - file size: `2,272,760 byte`
  - SHA-256: `41a5639a6b7b9fafe202f087bbc396c7c573090f1e0f729c7a4eaad71c3bdf6a`
- `build-release/Picocalc_NESco.uf2`
  - file size: `553,472 byte`
  - SHA-256: `9413821218af6cadb65102f4dbeac6f06bc1d5dadab225b3fc0ca7786b40b582`

## Measurement log exclusion

ELFの`strings`を検索し、次がすべて存在しないことを確認した。

- `[CORE1_BASE]`
- `[FRAME_STATS]`
- `[BG_SHARE]`
- `[PALETTE_SNAPSHOT]`
- `[SPR_ACTIVE]`

起動時のversion / build ID bannerは残す。

## Public file checks

- tracked ROM / save / compressed ROM candidate: no matches
- tracked README images:
  - `docs/images/mapper30_tower_normal.png`
  - `docs/images/mapper30_tower_stretch.png`
  - `docs/images/readme_candidates/.gitkeep`
  - `docs/images/rom_menu.png`
- `git diff --check`: pass

## Release decision

- ユーザーが`build-release/Picocalc_NESco.uf2`をrelease artifactとして承認した
- GitHub Releaseには同一byte列を`Picocalc_NESco-1.2.0.uf2`として添付する
- ローカル／SD card用basenameは`Picocalc_NESco.uf2`のまま変更しない
