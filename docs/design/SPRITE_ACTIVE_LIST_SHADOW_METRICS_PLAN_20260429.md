# Sprite active list shadow metrics plan

作成日: 2026-04-29

対象 branch: `feature/hot-path-metrics`

対象 version:

- 現在の計測版: `1.1.14`
- 次の計測候補: `1.1.15`

## 目的

`1.1.14` で、sprite scan の残り時間は non-visible sprite の skip 判定が支配的である可能性が高くなった。
次は、active sprite list / scanline prefilter を導入した場合に、構築コストと削減見込みが釣り合うかを測る。

今回も最適化ではなく計測である。
描画には active list を使わず、従来の sprite scan / composite をそのまま動かす。

## 根拠

`1.1.14` の実機ログ `/home/fuyuki/pico_dvl/codex/log/pico20260429_110452.log` で、次の事実を確認した。

- build: `PicoCalc NESco Ver. 1.1.14 Build Apr 29 2026 11:03:15`
- 確認 ROM:
  - `LodeRunner.nes`: mapper 0
  - `Xevious.nes`: mapper 0
  - `Project_DART_V1.0.nes`: mapper 30
- sprite scan が出ている sample では、`skip_per_frame` が概ね 1.4 万前後だった
  - `LodeRunner.nes`: 約 `14132`
  - `Xevious.nes`: 約 `14513`
  - `Project_DART_V1.0.nes`: 約 `13945`
- これは 240 scanlines x 64 sprites = `15360` に近い

したがって、毎 scanline で 64 sprite を見る固定費を減らす方向が有力である。

## 計測方針

active sprite list を本実装する前に、shadow build のコストだけを測る。

shadow build とは:

- frame ごとに SPRRAM 64 sprites を 1 回だけ走査する
- 各 sprite の Y 範囲を visible scanline 範囲へ clamp する
- scanline ごとの active sprite 件数だけを数える
- 実際の描画や sprite composite には使わない

これにより、次を比較する。

```text
current_scan_unaccounted_per_frame
  = (ppu_sprite_scan_us
     - (ppu_sprite_scan_oam_us + ppu_sprite_scan_fetch_us + ppu_sprite_scan_write_us))
    / frames

shadow_active_build_us_per_frame
  = ppu_sprite_active_build_us / frames

active_entries_per_frame
  = ppu_sprite_active_entries / frames

active_lines_per_frame
  = ppu_sprite_active_lines / frames
```

`shadow_active_build_us_per_frame` が `current_scan_unaccounted_per_frame` より十分小さければ、active sprite list 実装へ進む根拠になる。

## 追加する計測

初回は次の 4 fields を追加する。

- `ppu_sprite_active_build_us`
- `ppu_sprite_active_entries`
- `ppu_sprite_active_lines`
- `ppu_sprite_active_max_per_line`

意味:

- `ppu_sprite_active_build_us`
  - shadow active list を 1 frame 分構築する時間
- `ppu_sprite_active_entries`
  - active list に入る sprite-line entry の総数
  - 1 sprite が 8 scanlines に出るなら最大 8 entries として数える
- `ppu_sprite_active_lines`
  - active sprite が 1 件以上ある scanline 数
- `ppu_sprite_active_max_per_line`
  - log interval 中に観測した 1 scanline あたり active sprite 件数の最大値
  - 将来の固定配列容量を検討する材料にする
  - sprite overflow flag、描画優先順位、sprite 0 hit の変更判断には使わない

## 実装位置

shadow build は frame 単位で 1 回だけ実行する。

差し込み候補:

- `InfoNES_HSync()`
- `case SCAN_TOP_OFF_SCREEN:`
- `FrameCnt == 0`
- `PPU_R1 & R1_SHOW_SP`
  が真のとき

理由:

- frame 先頭時点の SPRRAM snapshot を仮に読む
- これは現行描画に使う正しい list ではなく、構築コストを見るためだけの shadow 計測である
- `InfoNES_DrawLine()` の hot path へ直接は入れない
- 実描画の scanline ごとの処理には影響させない

実装イメージ:

```cpp
if constexpr (kPerfLogToSerial)
{
  if (FrameCnt == 0 && (PPU_R1 & R1_SHOW_SP))
  {
    measureSpriteActiveListShadow();
  }
}
```

## shadow build の内容

`measureSpriteActiveListShadow()` は `kPerfLogToSerial` 有効時だけ使う。

処理:

```cpp
BYTE line_counts[NES_DISP_HEIGHT] = {};

for each sprite in SPRRAM[0..63]:
  y0 = SPRRAM[i * 4 + SPR_Y] + 1;
  y1 = y0 + PPU_SP_Height;
  clamp to [0, NES_DISP_HEIGHT)
  if empty:
    continue
  for y in [clamped_y0, clamped_y1):
    ++line_counts[y]
    ++entries

active_lines = count(line_counts[y] > 0)
max_per_line = max(line_counts[y])
```

注意:

- これは計測用であり、描画には使わない
- `line_counts` は stack local とし、静的 RAM を増やさない
- stack 増加は 240 bytes 程度に抑える
- `time_us_64()` は shadow build 全体の前後 2 回だけにする

## OAM mid-frame update について

現行実装は scanline ごとに SPRRAM を読むため、mid-frame OAM write があればその影響を受ける。
active sprite list を frame 先頭で固定すると、mid-frame OAM write の扱いが変わる可能性がある。

今回の shadow build は採用判断のためのコスト計測であり、互換性判断は行わない。
`SCAN_TOP_OFF_SCREEN` で作る shadow list は、その後の OAM DMA / `$2004` write を反映しない。
したがって、この計測結果を「その frame の描画に正しく使える active list」とは扱わない。
active sprite list を本実装する場合は、別途 OAM write / DMA による invalidation または fallback 方針を決める。

## 実装方針

追加 accumulator:

```cpp
uint64_t g_perf_ppu_sprite_active_build_us;
uint32_t g_perf_ppu_sprite_active_entries;
uint32_t g_perf_ppu_sprite_active_lines;
uint32_t g_perf_ppu_sprite_active_max_per_line;
```

`perf_reset()` で 0 clear する。

`[CORE1_BASE]` では sprite scan fields の近くへ追加する。

推奨順:

```text
ppu_sprite_scan_us
ppu_sprite_scan_oam_us
ppu_sprite_scan_fetch_us
ppu_sprite_scan_write_us
ppu_sprite_visible_count
ppu_sprite_scan_skip_count
ppu_sprite_active_build_us
ppu_sprite_active_entries
ppu_sprite_active_lines
ppu_sprite_active_max_per_line
ppu_sprite_comp_us
```

## 計測 overhead への対策

- `time_us_64()` は shadow build 全体の前後だけ
- line ごとの timing は取らない
- per sprite / per line の printf は出さない
- 今回も release 用 version ではなく計測版として扱う

## 予想

`ppu_sprite_active_build_us / frames` が `scan_unaccounted_per_frame` より十分小さい場合:

- active sprite list / scanline prefilter を実装する価値が高い
- 次は実際に scanline ごとの sprite index list を作り、`InfoNES_DrawLine()` の 64 sprite scan を置き換える計画を作る

`ppu_sprite_active_build_us / frames` が大きい場合:

- active sprite list は割に合わない可能性がある
- 64 sprite scan loop の局所最適化へ戻る

`ppu_sprite_active_max_per_line` が 8 を大きく超える場面が多い場合:

- 固定配列容量や overflow storage の見積もり材料にする
- この計測版では、NES の sprite overflow flag、描画優先順位、sprite 0 hit の動作変更判断には使わない
- 8 件制限や overflow 方針を変更する場合は、別計画で扱う

## やらないこと

今回の計測版では、次は行わない。

- active sprite list を描画に使う
- frame 先頭 snapshot を正しい描画 list として扱う
- sprite 表示順の変更
- sprite overflow flag の変更
- `active_entries` / `active_max_per_line` を overflow 動作変更の根拠にする
- OAM write invalidation 実装
- `pSprBuf` 形式変更
- release 用 version としての採用

## build

最低 1 回:

- `1.1.15` build

確認項目:

- build 成功
- banner
- UF2 SHA-256
- ELF SHA-256
- `text / data / bss`
- `strings build/Picocalc_NESco.elf | rg "PicoCalc NESco Ver|ppu_sprite_active_build_us|CORE1_BASE"` で banner と log format を確認する

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
- `ppu_sprite_active_build_us`
- `ppu_sprite_active_entries`
- `ppu_sprite_active_lines`
- `ppu_sprite_active_max_per_line`
- `ppu_sprite_us`
- `fps_x100`
- `frame_us_avg`

## 判断

この計測版で採用 / 不採用を判断する対象は、active sprite list 実装へ進むかどうかである。

判断例:

- active build が十分軽ければ、active sprite list 実装計画を作る
- active build が重ければ、64 sprite scan loop の局所最適化へ戻る
- OAM mid-frame update の懸念が大きい場合は、OAM write invalidation 計画を先に作る

## 手戻り

この計測版は 1 commit にまとめる。

計測 overhead が大きすぎる場合は、その commit を revert する。

次の最適化へ進む前に、`1.1.13` 以降の計測 fields を残すか外すかを判断する。
