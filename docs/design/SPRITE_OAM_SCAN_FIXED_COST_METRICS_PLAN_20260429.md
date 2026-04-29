# Sprite OAM scan fixed cost metrics plan

作成日: 2026-04-29

対象 branch: `feature/hot-path-metrics`

対象 version:

- 現在の計測版: `1.1.13`
- 次の計測候補: `1.1.14`

## 目的

`1.1.13` で、visible sprite の setup / fetch / write は `ppu_sprite_scan_us` の主因ではないことが分かった。
次は、`ppu_sprite_scan_us` に大きく残っている差分が、64 sprite の Y 判定 loop / non-visible OAM scan 固定費なのかを確認する。

今回も最適化ではなく計測である。

## 根拠

`1.1.13` の実機ログ `/home/fuyuki/pico_dvl/codex/log/pico20260429_104245.log` で、次の事実を確認した。

- build: `PicoCalc NESco Ver. 1.1.13 Build Apr 29 2026 10:44:07`
- 確認 ROM:
  - `LodeRunner.nes`: mapper 0
  - `Xevious.nes`: mapper 0
  - `Project_DART_V1.0.nes`: mapper 30
- `ppu_sprite_scan_us` に対して、`oam + fetch + write` の合計は小さかった
  - `LodeRunner.nes`: 差分約 89.1%
  - `Xevious.nes`: 差分約 81.3%
  - `Project_DART_V1.0.nes`: 差分約 88.4%

したがって、次は visible sprite 内部処理ではなく、64 件の scan loop 側を測る。

## 追加する計測

UART log の負荷を増やしすぎないため、初回は新規 field を 1 つだけ追加する。

- `ppu_sprite_scan_skip_count`

意味:

- `ppu_sprite_scan_skip_count`
  - `if (nY > PPU_Scanline || nY + PPU_SP_Height <= PPU_Scanline) continue;`
    で skip された sprite 件数

## 計測方針

`1.1.13` の `ppu_sprite_scan_oam_us` / `fetch_us` / `write_us` は維持する。

既存 fields と追加 field により、次を見る。

```text
scan_unaccounted =
  ppu_sprite_scan_us
  - (ppu_sprite_scan_oam_us + ppu_sprite_scan_fetch_us + ppu_sprite_scan_write_us)
```

さらに:

```text
ppu_sprite_scan_skip_count
ppu_sprite_visible_count
```

を比較し、差分が skip 数に比例するかを見る。

ROM 間または log interval 間で比較するときは、累積値をそのまま比べない。
既存 log の `frames` を使い、次の正規化値を主に見る。

```text
skip_per_frame = ppu_sprite_scan_skip_count / frames
visible_per_frame = ppu_sprite_visible_count / frames
scan_unaccounted_per_frame = scan_unaccounted / frames
```

`frames == 0` の log 行は比較対象にしない。

## 実装位置

現行の sprite scan loop:

```cpp
for (pSPRRAM = SPRRAM + (63 << 2); pSPRRAM >= SPRRAM; pSPRRAM -= 4)
{
  nY = pSPRRAM[SPR_Y] + 1;
  if (nY > PPU_Scanline || nY + PPU_SP_Height <= PPU_Scanline)
    continue;

  ++nSprCnt;
  ...
}
```

追加する local counter:

```cpp
uint32_t sprite_scan_skip_count = 0;
```

実装条件:

- `continue`
  の直前で
  `if constexpr (kPerfLogToSerial) { ++sprite_scan_skip_count; }`
  する
- loop 後に global accumulator
  へ加算する
- 既存の
  `g_perf_ppu_sprite_visible_count`
  は維持する

`ppu_sprite_scan_loop_us` は追加しない。
既存の `ppu_sprite_scan_us` が scan loop 全体に近い時間として既に使えるため、同じ区間を別名で出して UART log と解釈を増やさない。

## 実装方針

`kPerfLogToSerial` が有効なときだけ計測する。

追加する accumulator:

```cpp
uint32_t g_perf_ppu_sprite_scan_skip_count;
```

`perf_reset()` で 0 clear する。

`[CORE1_BASE]` では既存 sprite scan fields の近くへ追加する。

推奨順:

```text
ppu_sprite_scan_us
ppu_sprite_scan_oam_us
ppu_sprite_scan_fetch_us
ppu_sprite_scan_write_us
ppu_sprite_visible_count
ppu_sprite_scan_skip_count
ppu_sprite_comp_us
```

## 計測 overhead への対策

今回の追加は `time_us_64()` を増やさず、counter 加算を中心にする。

したがって、`1.1.13` より計測 overhead は大きく増えない想定である。
ただし counter 加算そのものの overhead はあるため、`1.1.14` も release 用ではなく計測版として扱う。

## 予想

`ppu_sprite_scan_skip_count` が大きく、`scan_unaccounted` も大きい場合:

- non-visible sprite の Y 判定 loop が主要候補
- 次は scanline ごとの active sprite list / OAM prefilter のような構造変更を検討する

既存 `ppu_sprite_visible_count` が大きい場面でだけ `scan_unaccounted` が大きい場合:

- visible sprite 内部処理のうち、今回測れていない loop 制御や branch overhead が残っている可能性がある

## やらないこと

今回の計測版では、次は行わない。

- sprite 表示ロジックの変更
- OAM scan algorithm の変更
- active sprite list の導入
- `pSprBuf` 形式変更
- sprite pattern LUT 化
- release 用 version としての採用

## build

最低 1 回:

- `1.1.14` build

確認項目:

- build 成功
- banner
- UF2 SHA-256
- ELF SHA-256
- `text / data / bss`
- `strings build/Picocalc_NESco.elf | rg "PicoCalc NESco Ver|ppu_sprite_scan_skip_count|CORE1_BASE"` で banner と log format を確認する

## 実機確認

確認 ROM:

- `LodeRunner.nes`
- `Xevious.nes`
- `Project_DART_V1.0.nes`

確認内容:

- 起動できる
- 入力できる
- 音が出る
- sprite 表示が崩れない
- `ESC` で ROM menu に戻れる
- `[ROM_START]` で ROM 名が紐づく
- `[CORE1_BASE]` に追加 fields が出る

比較項目:

- `ppu_sprite_scan_us`
- `ppu_sprite_scan_oam_us`
- `ppu_sprite_scan_fetch_us`
- `ppu_sprite_scan_write_us`
- `ppu_sprite_visible_count`
- `ppu_sprite_scan_skip_count`
- `ppu_sprite_us`
- `fps_x100`
- `frame_us_avg`

## 判断

この計測版で採用 / 不採用を判断する対象は、計測結果そのものではなく、次の最適化対象である。

判断例:

- skip count が支配的なら、active sprite list / scanline prefilter の計画を作る
- visible count が支配的なら、visible sprite 内部処理を改めて見る
- 計測結果が曖昧なら、最適化へ進まず計測方法を見直す

## 手戻り

この計測版は 1 commit にまとめる。

計測 overhead が大きすぎる場合は、その commit を revert する。

次の最適化へ進む前に、`1.1.13` / `1.1.14` の計測 fields を残すか外すかを判断する。
