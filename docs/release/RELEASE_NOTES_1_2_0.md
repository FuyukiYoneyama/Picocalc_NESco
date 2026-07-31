# Picocalc_NESco 1.2.0 Release Notes

`Picocalc_NESco 1.2.0` は、`1.1.26` 公開後の描画 pipeline 高速化をまとめた release です。

## 概要

- PicoCalc 向け `infones` ベースの NES emulator firmware です。
- ROM file は同梱しません。利用者自身が合法的に用意した ROM を使用してください。
- release build は起動時の version / build ID banner 1 行だけを出し、計測用 performance log は出力しません。
- normal `256x240` と stretch `320x300` の表示切替、ROM menu、screenshot、SRAM save の既存操作を維持します。

## 主な変更

### normal 表示の高速化

- PPU の描画ラインと core1 LCD worker queue を、RGB565 `WORD[256]` から palette index `BYTE[256]` へ変更しました。
- palette RAM の変更時だけ、version 付き palette snapshot を LCD worker へ渡します。
- background の opaque 判定を palette index の下位 2 bit から導出し、専用配列と LUT を削除しました。
- LCDへ渡す直前に、core1またはfallback packerが共通の256-entry LUTでRGB565へ変換します。
- line traffic は `1,052` から `608 byte/line` へ約42%減少しました。
- 通常 build の `.bss` は `1.1.26` の `98,548 byte` から約1,000 byte減少しました。

固定30窓の実機比較では次の結果でした。比較基準の`1.1.28` palette snapshot段階は、
`1.1.27`に対して測定限界以下の差であることを確認済みです。

| ROM | 比較基準 `frame_us_avg` | `1.1.29`以降 | 改善 |
|---|---:|---:|---:|
| LodeRunner | 19,114.0 us | 16,597.0 us | -13.17% |
| Project_DART | 20,183.0 us | 16,590.5 us | -17.80% |
| Xevious | 16,909.5 us | 16,596.0 us | -1.85% |

3 ROMすべてでnormal表示の平均とp95が約60fpsのframe pacing上限へ到達しました。
Xeviousは比較基準の時点で上限に近かったため、観測できる改善幅が小さくなっています。

### stretch 表示について

- `320x300` stretch表示の見た目と操作は維持しています。
- stretchはRGB565のLCD転送下限が約24.58ms/frameであり、normalと同じ60fpsには到達しません。
- queue retry短縮は実機A/Bで効果がなくrevert済みです。
- queue depth 8はDMA待ちの直接計測結果から実装していません。
- 上記の不採用実験と長い診断logはrelease buildへ含めません。

## 実機確認済み範囲

- LodeRunner、Project_DART、Xeviousのnormal/stretch計測と実プレイ
- palette snapshot / palette index protocol fault 0
- sprite優先度、左端8px clip、palette途中変更
- ROM開始、reset、表示切替、ROM menu復帰
- screenshot保存
- 入力と音に体感上の問題なし

公開artifactには、計測logを無効にした通常buildを使用します。

## 既知の不具合

- Mapper7 / AxROM
  - nametable / background崩れを確認しており、未解決です。
- Mapper9 / MMC2
  - CHR / background崩れを確認しており、未解決です。

## 未確認事項

- Mapper30の`*.m30` PRG flash overlay保存 / 復元の実ゲーム運用
- dynamic化済み`Map6`、`Map19`、`Map185`、`Map188`、`Map235`の対象ROM実機確認
- Mapper87 / Choplifter系の追加確認

## Build

- version: `1.2.0`
- build id: `Aug  1 2026 08:33:30`
- local / SD card UF2: `build-release/Picocalc_NESco.uf2`
- GitHub Release UF2: `Picocalc_NESco-1.2.0.uf2`
- UF2 SHA-256:
  `9413821218af6cadb65102f4dbeac6f06bc1d5dadab225b3fc0ca7786b40b582`
- ELF SHA-256:
  `41a5639a6b7b9fafe202f087bbc396c7c573090f1e0f729c7a4eaad71c3bdf6a`
- size: `text=278844 data=0 bss=97548`
- commit: `v1.2.0` tag対象commit

## Release build options

- `NESCO_RUNTIME_LOGS=OFF`
- `NESCO_INPUT_IO_LOGS=OFF`
- `NESCO_BOKOSUKA_STATE_LOGS=OFF`
- `NESCO_CORE1_BASELINE_LOG=OFF`
- `NESCO_BG_TILE_SHARE_LOG=OFF`
- `NESCO_PALETTE_SNAPSHOT_LOG=OFF`
- `NESCO_SPRITE_ACTIVE_LIST_METRICS=OFF`
- `NESCO_SPRITE_ACTIVE_LIST=ON`

Source archiveはGitHubの`v1.2.0` tagから自動生成されるものを使用します。
