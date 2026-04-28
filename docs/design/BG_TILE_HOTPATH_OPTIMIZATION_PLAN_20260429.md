# BG tile hot path optimization plan

作成日: 2026-04-29

対象 branch: `feature/hot-path-metrics`

対象 version:

- 現在の計測版: `1.1.5`
- 次の改善版候補: `1.1.6`

## 目的

`Picocalc_NESco` の frame rate 改善のため、`InfoNES_DrawLine()` の background tile hot path を、実機計測に基づいてピンポイントに改善する。

今回の方針は、推測で広く書き換えることではない。

1. `1.1.5` の詳細計測で重い箇所を確認する
2. 最も効果がありそうな箇所だけを小さく変更する
3. 同じ ROM で再計測し、改善 / 悪化を判断する

## 既に確認した事実

`1.1.5` の実機ログ `/home/fuyuki/pico_dvl/codex/log/pico20260429_082720.log` で、次の ROM を確認した。

- `LodeRunner.nes`
- `Xevious.nes`
- `Project_DART_V1.0.nes`

ログの banner:

- `PicoCalc NESco Ver. 1.1.5 Build Apr 29 2026 08:25:16`

`1.1.5` で追加した主な計測項目:

- `ppu_bg_tile_us`
- `ppu_bg_tile_pal_us`
- `ppu_bg_tile_build_us`
- `ppu_bg_tile_render_us`
- `ppu_bg_mapperppu_us`
- `ppu_bg_tile_count`

ROM ごとの主な結果:

| ROM | fps | `draw_us/frame` | `ppu_bg_tile_us/frame` | `render` | `palette` | `build` | `MapperPPU` |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| `LodeRunner.nes` | 21.0 | 27.5ms | 19.7ms | 5.8ms | 2.5ms | 1.9ms | 1.4ms |
| `Xevious.nes` | 24.9 | 27.8ms | 19.6ms | 5.7ms | 2.5ms | 1.9ms | 1.4ms |
| `Project_DART_V1.0.nes` | 20.3 | 27.1ms | 18.9ms | 5.7ms | 2.3ms | 1.8ms | 1.3ms |

注意:

- `1.1.5` は tile ごとに複数回 `time_us_64()` を呼ぶため、通常版より明らかに遅い
- `1.1.5` の fps は性能比較用ではなく、内訳確認用として扱う
- `MapperPPU()` は約 1.3ms から 1.4ms / frame で、最初の改善対象にはしない

## 過去文書との関係

過去に、次の設計文書で background hot path を分析している。

- `docs/design/BG_DRAWLINE_HOTPATH_REDESIGN_20260421.md`
- `docs/design/BG_TILE_RENDER_LUT_REDESIGN_20260421.md`
- `docs/design/BG_TILE_PREPROCESS_REUSE_REDESIGN_20260421.md`

これらの文書で検討された内容のうち、以下は現在の `InfoNES.cpp` に既に入っている。

- `BgTileDescriptor`
- `renderBgTile()`
- 4 pixel 単位の LUT:
  - `g_bg_tile_pair_idx4`
  - `g_bg_tile_pair_opaque4`
- `resolveBgPal()` による attribute / palette base reuse
- `emitBgTile()` への背景 tile 処理集約

したがって、今回の作業では過去文書をそのまま再実装しない。
現在残っている hot path を見て、さらに小さく改善する。

## 現在の hot path

現在の `emitBgTile()` は概ね次の処理を行う。

1. `resolveBgPal()`
2. `buildBgTile()`
3. `renderBgTile()`
4. `MapperPPU(PATTBL(...))`

`renderBgTile()` の full tile path は、`clip_left == 0 && clip_right == 8` の場合に使われる。
多くの tile は full tile path になるはずなので、最初の改善対象はここに限定する。

## 採用する改善方針

### Phase 0: 計測結果の正本化

この Phase は、この計画書を確定した直後に最初に行う。
現時点では `1.1.5` の build 結果は HISTORY にあるが、`pico20260429_082720.log` の ROM 別集計結果はまだ HISTORY に固定していない。

作業:

- `1.1.5` の計測結果を HISTORY に追記する
- `ppu_bg_tile_us` の内訳を、改善判断の baseline として固定する
- `1.1.5` の計測 overhead が大きいことも明記する

合格条件:

- `LodeRunner` / `Xevious` / `DART` の `ppu_bg_tile_*` 内訳が HISTORY に残っている
- 次の改善版がこの結果と比較できる

build:

- 不要

commit:

- Phase 0 の文書整理だけで commit する

### Phase 1: `renderBgTile()` full tile path の小改善

目的:

- `renderBgTile()` の full tile path を小さく軽くする
- partial tile path は変更しない
- `MapperPPU()` の順序は変更しない

変更候補:

1. `desc.pal` を local pointer に保持し、full tile path の palette 参照を簡潔にする
2. full tile path 専用 helper を追加する
3. `dst_opaque` の 8 byte 書き込みを、可能なら 32bit store 2 回にまとめる
4. `packed_hi` / `packed_lo` の展開を、現在より少ない式で行う

初回実装では、次を採用する。

- `renderBgTileFull()` を追加する
- `renderBgTileFull()` は `static inline` かつ `always_inline` 指定にする
- `renderBgTileFull()` は既存の `renderBgTile()` と同じく `__not_in_flash_func` を付ける
- 実装形は `static inline void __not_in_flash_func(renderBgTileFull)(...) __attribute__((always_inline))` 相当とする
- full tile path だけ `renderBgTileFull()` に分離する
- `renderBgTileFull()` の先頭で `const WORD *pal = desc.pal` を local に保持し、`desc.pal[...]` の繰り返し参照を避ける
- `renderBgTile()` 側では、full tile 判定後すぐに `renderBgTileFull(desc, packed_hi, packed_lo, opaque_hi, opaque_lo)` を呼ぶ
- partial tile path は現状維持する
- `dst_opaque` については、まず安全な byte store のままにする

理由:

- 最初から 32bit store にすると、alignment と endian の確認が増える
- 初回は、`always_inline` と `pal` local 化により、full tile path の palette 参照と分岐後のコードを少しでも軽くする
- 画面崩れリスクを最小化する

この変更で効果が出ない場合:

- 次は `BgTileDescriptor` を経由しない direct full tile path を検討する
- 具体的には `emitBgTile()` 側で full tile 用の `pattern_row` / `pal` / `dst` / `dstOpaque` を直接渡し、descriptor 構築と参照の overhead を減らす
- ただし、この direct path は Phase 1 には含めない

合格条件:

- build 成功
- `strings build/Picocalc_NESco.elf | rg "PicoCalc NESco Ver|CORE1_BASE"` で banner と log format が確認できる
- full tile path と partial tile path の意味が変わらない
- `MapperPPU()` の呼び出し順が変わらない

build:

- `1.1.6` に version 更新して build

commit:

- build 成功後に commit

手戻り:

- 画面崩れ、build 失敗、明らかな fps 悪化があれば Phase 1 commit を revert する

### Phase 2: 実機確認と比較

確認 ROM:

- `LodeRunner.nes`
- `Xevious.nes`
- `Project_DART_V1.0.nes`

確認内容:

- 起動する
- 入力できる
- 音が出る
- normal 表示が崩れない
- `ESC` で ROM menu に戻れる
- `[CORE1_BASE]` が出る
- `[ROM_START]` で ROM 名が紐づく

比較する項目:

- `fps_x100`
- `frame_us_avg`
- `draw_us`
- `ppu_bg_us`
- `ppu_bg_tile_us`
- `ppu_bg_tile_render_us`
- `ppu_bg_tile_count`
- `cpu_us`
- `apu_us`

判断:

- `ppu_bg_tile_render_us` が下がり、画面崩れがなければ採用候補
- `ppu_bg_tile_render_us` が変わらず、fps も改善しなければ不採用候補
- 画面崩れがあれば即不採用

実機確認回数:

- 1 回

理由:

- 変更範囲が `renderBgTile()` full tile path に限定されるため
- すでに同じ 3 ROM で baseline があるため

### Phase 3: 次の改善候補判断

Phase 2 の結果で判断する。

候補 A: `renderBgTileFull()` が効いた場合

- full tile path をさらに改善する
- `dst_opaque` の 32bit store 化を検討する
- palette 展開 LUT の追加を検討する

候補 B: `renderBgTileFull()` が効かなかった場合

- `resolveBgPal()` / `buildBgTile()` の前処理をまとめる
- `BgTileDescriptor` を作らず、full tile path では direct 引数で描画する
- `emitBgTile()` の lambda / descriptor 周辺 overhead を減らす

候補 C: 計測 overhead が大きすぎて判断不能な場合

- `1.1.5` の tile 内訳計測を外した軽量比較版を作る
- `ppu_bg_tile_us` と fps のみで比較する

## やらないこと

今回の初回改善では、次は行わない。

- `MapperPPU()` の呼び出し順変更
- CPU / PPU / APU の並列化
- LCD worker の追加変更
- partial tile path の大幅変更
- pattern data cache の導入
- mapper ごとの特殊分岐

## 予定 build 回数

最低 1 回:

1. Phase 1 build: `1.1.6`

必要なら追加:

- Phase 1 の小修正 build
- Phase 3 の追加実験 build

## 予定実機確認回数

最低 1 回:

1. Phase 2: `LodeRunner` / `Xevious` / `DART` 比較

## 手戻りポイント

- Phase 0 commit: 計測結果の文書化のみ
- Phase 1 commit: `renderBgTile()` full tile path 小改善

Phase 1 で画面崩れや改善なしが確認された場合、Phase 1 commit だけを戻す。

## 成功条件

次をすべて満たした場合に採用する。

- `LodeRunner` / `Xevious` / `DART` が起動する
- normal 表示が崩れない
- 入力、音、`ESC` menu return が壊れない
- `ppu_bg_tile_render_us` または `ppu_bg_tile_us` が改善する
- fps または `frame_us_avg` が悪化しない

## 注意

`1.1.5` の細分化計測は overhead が大きい。
したがって、`1.1.6` の改善判断では、単純な fps だけでなく、同じ計測条件での相対比較を見る。

もし `1.1.6` でも tile ごとの計測 overhead が大きすぎて改善量が読めない場合は、次に軽量計測版を作る。
