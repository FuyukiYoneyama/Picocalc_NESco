# Sprite composite range optimization plan

作成日: 2026-04-29

対象 branch: `feature/hot-path-metrics`

対象 version:

- 現在の計測版: `1.1.11`
- 次の小改善候補: `1.1.12`

## 目的

`1.1.11` の sprite 内訳計測結果を根拠に、`compositeSprite()` の走査範囲を必要最小限へ近づける。

今回の目的は、sprite 描画の意味を変えずに、sprite が存在しない horizontal range の合成チェックを減らせるか確認することである。

## 根拠

`1.1.11` の実機ログ `/home/fuyuki/pico_dvl/codex/log/pico20260429_101240.log` で、次の事実を確認した。

- build: `PicoCalc NESco Ver. 1.1.11 Build Apr 29 2026 10:10:50`
- UART: `921600`
- 確認 ROM:
  - `LodeRunner.nes`: mapper 0
  - `Xevious.nes`: mapper 0
  - `Project_DART_V1.0.nes`: mapper 30
- `ppu_sprite_us` の内訳では、`ppu_sprite_comp_us` が最大項目だった
  - `LodeRunner.nes`: sprite 時間の約 52.6%
  - `Xevious.nes`: sprite 時間の約 53.3%
  - `Project_DART_V1.0.nes`: sprite 時間の約 54.0%
- 次に大きいのは `ppu_sprite_scan_us` で、約 24〜28%
- `ppu_sprite_mapper_us` と `ppu_sprite_clip_us` は小さい

したがって、次に触る候補としては `compositeSprite()` が最も根拠が強い。

## 現状

`infones/InfoNES.cpp` の `compositeSprite()` は、`spr` / `bgOpaque` / `buf` を 4 pixel ずつ進めながら `NES_DISP_WIDTH` 全体を走査する。

現行の呼び出し:

```cpp
compositeSprite(PalTable + 0x10, pSprBuf, BackgroundOpaqueLine, pPoint);
```

現行の特徴:

- sprite がまったくない scanline でも、`PPU_R1 & R1_SHOW_SP` が有効なら composite が呼ばれる
- sprite が画面の一部だけにある scanline でも、256 pixel 全体を確認する
- sprite buffer は `BYTE pSprBuf[NES_DISP_WIDTH + 7]` で、右端付近の sprite 書き込み余裕を持っている

## 採用する改善

sprite scan 中に、その scanline で実際に sprite pixel を書き込む可能性がある horizontal range を記録する。

初回実装では、厳密な non-zero pixel 範囲ではなく、sprite object の X 範囲を使う。

- `sprite_min_x`
- `sprite_max_x_exclusive`

を `InfoNES_DrawLine()` 内の local 変数として追加する。

初期値:

```cpp
int sprite_min_x = NES_DISP_WIDTH;
int sprite_max_x_exclusive = 0;
```

scanline に sprite が載った時点で、sprite object の X 範囲を反映する。

現行コードでは sprite の X 座標は `nX = pSPRRAM[SPR_X];` で代入され、その直後に `const auto dst = pSprBuf + nX;` を作っている。
range 更新は必ず `nX = pSPRRAM[SPR_X];` の直後、`const auto dst = pSprBuf + nX;` の前に置く。
`nX` 代入前には range を更新しない。

```cpp
nX = pSPRRAM[SPR_X];
const int sprite_left = nX;
const int sprite_right = nX + 8;
if (sprite_left < sprite_min_x) sprite_min_x = sprite_left;
if (sprite_right > sprite_max_x_exclusive) sprite_max_x_exclusive = sprite_right;
const auto dst = pSprBuf + nX;
```

範囲は composite 前に画面内へ clamp する。

```cpp
if (sprite_min_x < 0) sprite_min_x = 0;
if (sprite_max_x_exclusive > NES_DISP_WIDTH) sprite_max_x_exclusive = NES_DISP_WIDTH;
```

ただし現行の `nX` は `BYTE` 由来で 0〜255 として扱われるため、初回実装では負値は想定しない。それでも clamp は安全策として入れる。

sprite が 1 つも scanline に載らなかった場合:

- `sprite_min_x >= sprite_max_x_exclusive`
- `compositeSpriteRange()` を呼ばない
- ただし sprite block 全体は skip しない
- `!(PPU_R1 & R1_CLIP_SP)` の左 8px clipping と、`nSprCnt >= 8` の `PPU_R2_MAX_SP` 判定は従来どおり実行する

sprite がある場合:

- `compositeSpriteRange()` を呼ぶ
- 4 pixel unroll を維持するため、range を 4 pixel 境界へ広げる

```cpp
const int comp_begin = sprite_min_x & ~3;
const int comp_end = (sprite_max_x_exclusive + 3) & ~3;
```

`comp_begin` / `comp_end` も `0..NES_DISP_WIDTH` に clamp する。

## 実装形

既存の `compositeSprite()` を直接壊さず、初回実装では helper を追加する。

```cpp
void __not_in_flash_func(compositeSpriteRange)(const uint16_t *pal,
                                               const uint8_t *spr,
                                               const uint8_t *bgOpaque,
                                               uint16_t *buf,
                                               int begin,
                                               int end)
```

内部では:

- `spr += begin`
- `bgOpaque += begin`
- `buf += begin`
- `sprEnd = spr + (end - begin)`
- 現行と同じ 4 pixel unroll の `proc(0..3)` を使う

初回実装では、既存 `compositeSprite()` は残す。

- range 版が不採用になった場合に戻しやすくする
- 比較時に full range fallback へ戻しやすくする

## 追加する計測

`1.1.11` の sprite 内訳 fields は維持する。

今回追加する計測は最小限にする。

追加候補:

- `ppu_sprite_comp_px`

ただし、UART log 長を増やしすぎないため、初回実装では新 field を増やさず、既存の `ppu_sprite_comp_us` と `ppu_sprite_visible_count` で評価する。

必要になった場合だけ、次の計測版で range 幅を追加する。

## やらないこと

今回の小改善では、次は行わない。

- sprite pattern expansion の LUT 化
- sprite scan loop の構造変更
- `pSprBuf` の形式変更
- sprite priority 判定の変更
- `BackgroundOpaqueLine` の形式変更
- sprite 0 hit 判定の変更
- `MapperRenderScreen(0)` の順序変更
- `PPU_R2_MAX_SP` の判定変更
- 8x16 sprite の仕様変更

## 予想される効果

効果見込みは中。

理由:

- `ppu_sprite_comp_us` は sprite 時間の約 53〜54% を占めている
- sprite が少ない scanline では、256 pixel 全域 composite を避けられる
- sprite が画面全体に広がる場面では効果は小さい

予想:

- `ppu_sprite_comp_us` が下がる可能性が高い
- `ppu_sprite_scan_us` はほぼ変わらない
- `ppu_sprite_us` 全体も改善する可能性がある
- `fps_x100` / `frame_us_avg` は ROM と場面依存

## 失敗時に起きる症状

range 計算を誤ると、sprite が描かれない、または一部だけ欠ける。

重点確認:

- sprite の左端 / 右端が欠けない
- 画面左端付近の sprite が欠けない
- 画面右端付近の sprite が欠けない
- sprite がない scanline で背景が壊れない
- sprite clipping が現行と同じ見た目になる

## 実装手順

1. `compositeSpriteRange()` を追加する
2. `InfoNES_DrawLine()` の sprite scan 前に `sprite_min_x` / `sprite_max_x_exclusive` を初期化する
3. scanline に sprite が載った時点で range を更新する
4. composite 前に range を clamp / 4 pixel align する
5. range が空なら `compositeSpriteRange()` 呼び出しだけを skip する
6. range が空でなければ `compositeSpriteRange()` を呼ぶ
7. sprite clipping と `PPU_R2_MAX_SP` 判定は composite skip と無関係に従来どおり実行する
8. version を `1.1.12` に更新する
9. build する
10. 実機確認を 1 回行う
11. build 結果と実機結果を HISTORY に記録する
12. 採用 / 不採用を判断する
13. 採用する場合は、実装と HISTORY を 1 commit にまとめる
14. 不採用の場合は、実装を revert し、必要なら不採用結果だけ HISTORY に記録する

## build

最低 1 回:

- `1.1.12` build

確認項目:

- build 成功
- banner
- UF2 SHA-256
- ELF SHA-256
- `text / data / bss`
- `strings build/Picocalc_NESco.elf | rg "PicoCalc NESco Ver|ppu_sprite_comp_us|CORE1_BASE"` で banner と log format を確認する

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
- sprite の端が欠けない
- sprite の前後関係が明らかに壊れていない
- F5 screenshot が撮れる
- `ESC` で ROM menu に戻れる
- `[ROM_START]` で ROM 名が紐づく
- `[CORE1_BASE]` が出る

比較項目:

- `fps_x100`
- `frame_us_avg`
- `frame_us_max`
- `draw_us`
- `ppu_sprite_us`
- `ppu_sprite_comp_us`
- `ppu_sprite_scan_us`
- `ppu_sprite_clear_us`
- `ppu_sprite_visible_count`
- `ppu_bg_us`
- `ppu_bg_tile_us`
- `cpu_us`
- `apu_us`

## 採用条件

採用候補:

- 3 ROM すべてで sprite 欠けがない
- 3 ROM すべてで game 操作に問題がない
- `ppu_sprite_comp_us` が同等または改善する
- `ppu_sprite_us` が同等または改善する
- `frame_us_avg` が明らかに悪化しない

不採用:

- sprite が欠ける
- sprite 前後関係が壊れる
- `frame_us_avg` または `ppu_sprite_us` が明らかに悪化する
- 改善が誤差程度で、コード複雑化に見合わない

## 手戻り

この実験は 1 commit にまとめる。

不採用の場合は、その commit を revert する。

`1.1.11` は sprite 内訳計測の正本として維持する。
