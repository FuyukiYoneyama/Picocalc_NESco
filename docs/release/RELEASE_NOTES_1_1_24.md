# Picocalc_NESco 1.1.24 Release Notes

`Picocalc_NESco 1.1.24` は、`1.1.0` 公開後の開発成果をまとめた release candidate です。

## 概要

- PicoCalc 向け NES emulator firmware です。
- 現在の実装は `infones` ベースです。
- ROM file は同梱していません。利用者自身が合法的に用意した ROM を使用してください。
- PicoCalc debug console の UART baud rate は `921600 bps` です。
- release build では、起動時の version / build id banner 1 行を除き、verbose runtime log は無効です。

## 主な変更

- core1 keyboard polling / core1 LCD worker を採用しました。
  - 入力応答と LCD 表示処理を補助します。
- ROM menu から screenshot viewer を開けるようになりました。
  - ROM menu / help / game 中の screenshot 保存に加え、保存済み BMP を PicoCalc 上で確認できます。
- `F4` で screenshot viewer に入る操作に変更しました。
- ROM menu のページ移動を調整しました。
  - 上下移動時の画面ちらつきと、ページ境界での見え方を改善しています。
- PPU / BG / sprite 周辺の hot path を一部最適化しました。
  - BG full tile path の整理
  - sprite composite range 最適化
  - 計測用詳細 log は release build から外しています。
- UART log baud rate を `921600 bps` に変更しました。
- Mapper152 を追加しました。
  - `Arkanoid 2` で起動確認済みです。

## 追加確認済み ROM

起動確認まで実施済みです。

- `Dragon Quest II`
  - Mapper2
- `Takeshi no Sengoku Fuuunji`
  - Mapper33
- `Pro Yakyuu Family Stadium`
  - Mapper206
- `Arkanoid 2`
  - Mapper152

長時間 gameplay / save / screenshot / ESC 復帰などの詳細確認は、必要に応じて別途確認します。

## 既知の不具合

- Mapper7 / AxROM
  - `Solstice (Japan)`
    で nametable / background
    崩れを確認しています。
  - one-screen mirroring
    反転実験では改善しませんでした。
  - 現時点では不具合ありとして保留しています。
- Mapper9 / MMC2
  - `Punch-Out!! (USA)`
    で CHR / background
    崩れを確認しています。
  - MMC2 latch trigger
    範囲修正実験では改善しませんでした。
  - 対象 title が限定的なため、現時点では不具合ありとして保留しています。

## 未確認事項

- Mapper30 の `*.m30` PRG flash overlay 保存 / 復元
  - ROM 起動と表示は確認済みです。
  - `*.m30` 保存 / 復元の実ゲーム運用は未確認です。
- Dynamic 化済み mapper の実機確認
  - `Map19`
  - `Map185`
  - `Map6`
  - `Map188`
  - `Map235`
- Mapper87 / Choplifter 系の追加確認
  - 別の Mapper87 ROM 入手後に再開予定です。

## Build

- version: `1.1.24`
- build id: `May  2 2026 08:00:10`
- commit: `c8bfca0 Add 1.1.24 release notes`
- UF2: `Picocalc_NESco-1.1.24.uf2`
- UF2 SHA-256:
  `054f9a4c71b109057d6c53869a32de677a9b764cbafa65bf13cc70db75a9f5d2`
- ELF SHA-256:
  `31227c0b56c7671db4f7a9e2b511d74949e8c94c1210091c9ee54d81c69a859d`

## Release build options

- `NESCO_RUNTIME_LOGS=OFF`
- `NESCO_INPUT_IO_LOGS=OFF`
- `NESCO_BOKOSUKA_STATE_LOGS=OFF`
- `NESCO_CORE1_BASELINE_LOG=OFF`
- `NESCO_CORE1_KEYBOARD_LOG=OFF`

## 補足

- `1.1.23` は Mapper7 / Mapper9 の不採用実験 build に使われたため、公開候補は `1.1.24` としました。
- Source archive は GitHub の release tag 由来のものを使用します。
