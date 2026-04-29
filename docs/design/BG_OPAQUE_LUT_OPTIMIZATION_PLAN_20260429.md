# BG opaque LUT optimization plan

作成日: 2026-04-29

対象 branch: `feature/hot-path-metrics`

対象 version:

- 現在の軽量計測版: `1.1.8`
- 次の小改善候補: `1.1.9`

## 目的

`1.1.8` の軽量計測結果を基準に、background full tile path のうち `dst_opaque` 生成だけを小さく改善する。

今回の目的は、大きな構造変更ではない。

- full tile が多いという実測事実を使う
- 画面色や mapper 処理を触らない
- sprite 合成用の opaque buffer だけを安全に軽くできるか確認する
- 効果が小さければ不採用にできる切れ目を作る

## 根拠

`1.1.8` の実機ログ `/home/fuyuki/pico_dvl/codex/log/pico20260429_091111.log` で、次の事実を確認した。

- `1.1.5` / `1.1.6` の tile ごとの詳細 timing 計測は重すぎ、通常実行性能の判断には使いにくい
- `1.1.8` は tile ごとの `time_us_64()` を外した軽量計測版として採用する
- full tile 比率:
  - `LodeRunner.nes`: 96.16%
  - `Xevious.nes`: 96.97%
  - `Project_DART_V1.0.nes`: 95.20%

したがって、full tile path を主対象にすることは合理的である。

一方、`1.1.7` の direct full tile path 実験では、`ppu_bg_tile_build_us` は下がったが、fps / frame time は 3 ROM すべてで悪化した。
そのため、descriptor 回避のような構造変更は次の一手にはしない。

## 対象コード

対象は `infones/InfoNES.cpp` の `renderBgTileFull()` に限定する。

現在の full tile path は、背景 pixel 色を `dst[0..7]` に書いたあと、sprite 合成用の opaque 判定を `dst_opaque[0..7]` に byte store している。

現行の opaque 書き込み:

```cpp
dst_opaque[0] = (BYTE)((opaque_hi >> 3) & 0x01u);
dst_opaque[1] = (BYTE)((opaque_hi >> 2) & 0x01u);
dst_opaque[2] = (BYTE)((opaque_hi >> 1) & 0x01u);
dst_opaque[3] = (BYTE)(opaque_hi & 0x01u);
dst_opaque[4] = (BYTE)((opaque_lo >> 3) & 0x01u);
dst_opaque[5] = (BYTE)((opaque_lo >> 2) & 0x01u);
dst_opaque[6] = (BYTE)((opaque_lo >> 1) & 0x01u);
dst_opaque[7] = (BYTE)(opaque_lo & 0x01u);
```

## 採用する改善

`opaque_hi` / `opaque_lo` はそれぞれ 4 bit 相当の opaque mask である。
この 4 bit mask から 4 byte の `0 / 1` 配列を得る LUT を追加する。

初回実装では次を行う。

- `static BYTE g_bg_opaque4_lut[16][4]` を追加する
  - `const` は付けない
  - hot path から XIP flash 上の LUT を読むことを避けるため、RAM 配置を優先する
  - RAM 増加は 64 bytes と見込む
- `renderBgTileFull()` では `opaque_hi` / `opaque_lo` を index にして LUT を参照する
- LUT 参照は次の形に固定する
  - `const BYTE *hi = g_bg_opaque4_lut[opaque_hi];`
  - `const BYTE *lo = g_bg_opaque4_lut[opaque_lo];`
- `dst_opaque[0..3]` へ `hi[0..3]` を byte store する
- `dst_opaque[4..7]` へ `lo[0..3]` を byte store する
- byte store は維持し、`memcpy()` や 32bit store は使わない
- `renderPacked4()` と partial tile path は変更しない

## やらないこと

今回の小改善では、次は行わない。

- `dst_opaque` の 32bit store 化
- `memcpy()` による 4 byte copy
- `WORD` / `uint32_t` pointer cast
- `static const` による flash 配置 LUT
- palette 展開の LUT 化
- `renderBgTile()` の partial tile path 変更
- `emitBgTile()` の構造変更
- `MapperPPU()` の順序変更
- `BgTileDescriptor` の廃止

## 32bit store をまだ使わない理由

`dst_opaque` は byte buffer であり、常に 4 byte alignment されているとはこの計画書では確定しない。
32bit store は endian / alignment / partial tile 混入の確認が必要になる。

今回の目的は、安全に shift 計算を減らすことである。
そのため、まず byte store のまま LUT 化し、alignment リスクを持ち込まない。

## 予想される効果

効果見込みは小から中。

理由:

- full tile は 95% 以上なので、対象頻度は高い
- ただし変更対象は `dst_opaque` 8 byte の生成だけで、背景色の palette 展開はそのまま
- 大きな FPS 改善より、`ppu_bg_tile_us` が少し下がるかどうかを見る実験になる

予想:

- 表示意味が変わらなければ採用候補
- `ppu_bg_tile_us` / `frame_us_avg` がほぼ変わらなければ、採用してもよいが高速化効果は小さいと記録する
- 悪化する場合は revert する

## 失敗時に起きる症状

opaque buffer は背景色そのものではなく、sprite 合成の前後判定に使われる。
そのため、失敗しても背景が正常に見える可能性がある。

重点確認:

- sprite が背景の前に出るべき場面で消えない
- sprite が背景の後ろに隠れるべき場面で前に出すぎない
- sprite 周辺に不自然な抜けやちらつきがない
- normal 表示で崩れない

## 実装手順

1. `g_bg_opaque4_lut[16][4]` を追加する
2. `renderBgTileFull()` の `dst_opaque` 生成だけを LUT 参照に置き換える
3. version を `1.1.9` に更新する
4. build する
5. banner / `.bss` / UF2 SHA-256 / ELF SHA-256 を控える
6. 実機確認を 1 回行う
7. build 結果と実機結果を HISTORY に記録する
8. 採用 / 不採用を判断する
9. 採用する場合は、実装と HISTORY を 1 commit にまとめる
10. 不採用の場合は、実装を revert し、必要なら不採用結果だけ HISTORY に記録する

## build

最低 1 回:

- `1.1.9` build

確認項目:

- build 成功
- `strings build/Picocalc_NESco.elf | rg "PicoCalc NESco Ver|CORE1_BASE"` で banner と log format を確認する
- `.bss` 増加量を確認する

想定 RAM 増加:

- LUT 本体は RAM 側に `16 * 4 = 64 bytes`
- その他の増加は compiler 配置次第

## 実機確認

確認 ROM:

- `LodeRunner.nes`
- `Xevious.nes`
- `Project_DART_V1.0.nes`

確認内容:

- 起動できる
- 入力できる
- 音が出る
- normal 表示が崩れない
- sprite の前後関係が明らかに壊れていない
- `ESC` で ROM menu に戻れる
- `[ROM_START]` で ROM 名が紐づく
- `[CORE1_BASE]` が出る

比較項目:

- `fps_x100`
- `frame_us_avg`
- `draw_us`
- `ppu_bg_us`
- `ppu_bg_tile_us`
- `ppu_bg_tile_count`
- `ppu_bg_tile_full_count`
- `ppu_bg_tile_partial_count`
- `ppu_sprite_us`
- `cpu_us`
- `apu_us`

## 採用条件

採用候補:

- 3 ROM すべてで表示崩れがない
- sprite の前後関係が明らかに壊れていない
- `frame_us_avg` が悪化しない
- `ppu_bg_tile_us` が同等または改善する

不採用:

- sprite 前後関係が壊れる
- 背景表示が崩れる
- `frame_us_avg` または `ppu_bg_tile_us` が明らかに悪化する
- build size / RAM 増加に対して効果が見合わない

## 手戻り

この実験は 1 commit にまとめる。

不採用の場合は、その commit を revert する。

`1.1.8` は軽量計測の正本として維持する。
