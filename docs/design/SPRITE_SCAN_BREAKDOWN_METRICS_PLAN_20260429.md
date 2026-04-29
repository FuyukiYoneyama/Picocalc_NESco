# Sprite scan breakdown metrics plan

作成日: 2026-04-29

対象 branch: `feature/hot-path-metrics`

対象 version:

- 現在の採用版: `1.1.12`
- 次の計測候補: `1.1.13`

## 目的

`1.1.12` で `ppu_sprite_comp_us` は大きく下がった。
次は相対的に大きく残った `ppu_sprite_scan_us` の内訳を確認し、次に触るべき hot path を根拠つきで決める。

今回の目的は最適化ではなく、`sprite scan / pattern expansion / sprite buffer write` のどこが重いかを粗く測ることである。

## 根拠

`1.1.12` の実機ログ `/home/fuyuki/pico_dvl/codex/log/pico20260429_103111.log` で、次の事実を確認した。

- build: `PicoCalc NESco Ver. 1.1.12 Build Apr 29 2026 10:29:01`
- 確認 ROM:
  - `LodeRunner.nes`: mapper 0
  - `Xevious.nes`: mapper 0
  - `Project_DART_V1.0.nes`: mapper 30
- user 報告で画面の乱れはなかった
- `1.1.11` と比べて `ppu_sprite_comp_us` は約 91〜93% 減った
- 一方で `ppu_sprite_scan_us` は残り、場面によっては sprite 側の主要項目になった

したがって、次の最適化候補は `compositeSpriteRange()` ではなく、sprite scan loop 内にある。

## 現状の scan loop

`infones/InfoNES.cpp` の `InfoNES_DrawLine()` では、`PPU_R1 & R1_SHOW_SP` が有効なときに次を行う。

1. `SPRRAM` を 63 番 sprite から 0 番 sprite へ走査する
2. `nY` と `PPU_Scanline` で scanline に載る sprite だけを選ぶ
3. visible sprite について属性、Y flip、8x8 / 8x16、CHR bank、pattern address を計算する
4. `PPUBANK[bank]` から pattern plane を読み、`pat0` / `pat1` を作る
5. `nX = pSPRRAM[SPR_X]` の後、`pSprBuf` へ最大 8 pixel 分を書き込む

現在の `ppu_sprite_scan_us` は、上記をまとめて 1 つの値として測っている。

## 追加する計測

UART log の負荷を増やしすぎないため、初回は 3 fields だけ追加する。

- `ppu_sprite_scan_oam_us`
- `ppu_sprite_scan_fetch_us`
- `ppu_sprite_scan_write_us`

意味:

- `ppu_sprite_scan_oam_us`
  - visible sprite の属性 / Y 座標処理、8x8 / 8x16 bank 判定まで
  - `PPUBANK[bank]` から pattern byte を読む前まで
- `ppu_sprite_scan_fetch_us`
  - `PPUBANK[bank] + addrOfs` の算出、`pl0` / `pl1` load、`pat0` / `pat1` 作成
- `ppu_sprite_scan_write_us`
  - `nAttr` priority 反転、palette slot 作成、`nX` 取得、range 更新、H flip / non flip の `dst[0..7]` 書き込み

`ppu_sprite_scan_us` は従来どおり残す。
追加 3 fields の合計は `ppu_sprite_scan_us` と完全一致しなくてよい。
理由は、計測点を増やすこと自体の overhead と、loop 制御部分がどこかに寄るためである。

## 計測位置

初回実装では、visible sprite だけを対象に内訳を測る。

### OAM / setup

次の範囲を `ppu_sprite_scan_oam_us` に入れる。

```cpp
nAttr = pSPRRAM[SPR_ATTR];
nYBit = PPU_Scanline - nY;
nYBit = (nAttr & SPR_ATTR_V_FLIP) ? (PPU_SP_Height - nYBit - 1) : nYBit;
const int yOfsModSP = nYBit;
nYBit <<= 3;

int ch = pSPRRAM[SPR_CHR];
int bankOfs;
if (PPU_R0 & R0_SP_SIZE) { ... } else { ... }
const int bank = (ch >> 6) + bankOfs;
const int addrOfs = ...;
```

ここには `if (nY > PPU_Scanline || ...) continue;` の non-visible 判定自体は含めない。
したがって、`ppu_sprite_scan_oam_us` は 64 件全体の OAM scan 時間ではなく、visible sprite だけの setup 時間を表す。

non-visible 64 件走査の固定費と loop 制御 overhead は、今回の計測では次の差分として扱う。

```text
ppu_sprite_scan_us - (ppu_sprite_scan_oam_us + ppu_sprite_scan_fetch_us + ppu_sprite_scan_write_us)
```

この差分が大きい場合だけ、次の計測版で non-visible OAM scan 固定費を別途測る。

### Pattern fetch / expand

次の範囲を `ppu_sprite_scan_fetch_us` に入れる。

```cpp
const auto data = PPUBANK[bank] + addrOfs;
const uint32_t pl0 = data[0];
const uint32_t pl1 = data[8];
const auto pat0 = ((pl0 & 0x55) << 24) | ((pl1 & 0x55) << 25);
const auto pat1 = ((pl0 & 0xaa) << 23) | ((pl1 & 0xaa) << 24);
```

### Sprite buffer write

次の範囲を `ppu_sprite_scan_write_us` に入れる。

```cpp
nAttr ^= SPR_ATTR_PRI;
bySprCol = (nAttr & (SPR_ATTR_COLOR | SPR_ATTR_PRI)) << 2;
nX = pSPRRAM[SPR_X];
// sprite range update
const auto dst = pSprBuf + nX;
// H flip / non flip write
```

`nX` 取得と range 更新は、`1.1.12` で追加した composite range 最適化の一部なので write 側へ含める。

## 実装方針

`kPerfLogToSerial` が有効なときだけ計測する。

追加する accumulator:

```cpp
uint64_t g_perf_ppu_sprite_scan_oam_us;
uint64_t g_perf_ppu_sprite_scan_fetch_us;
uint64_t g_perf_ppu_sprite_scan_write_us;
```

`perf_reset()` で 0 clear する。

`[CORE1_BASE]` の末尾ではなく、既存 sprite fields の近くへ追加する。

推奨順:

```text
ppu_sprite_scan_us
ppu_sprite_scan_oam_us
ppu_sprite_scan_fetch_us
ppu_sprite_scan_write_us
ppu_sprite_comp_us
```

## 計測 overhead への対策

今回の計測は hot loop 内に `time_us_64()` を増やすため、実行速度そのものを多少乱す。

そのため、採用する判断は次のようにする。

- `1.1.13` は計測版として扱う
- `1.1.13` の fps や frame time は、最適化済み通常版の性能として扱わない
- 次の最適化対象を選ぶ根拠は、追加 3 fields の相対的な大小で見る
- 追加 3 fields は短い区間を測るため、絶対値ではなく ROM 間 / 区間間の大まかな傾向として読む
- 追加 3 fields の差が小さい場合は判断保留にし、次の最適化へ進まない
- 追加計測を入れたまま release しない

## 予想

予想では、`ppu_sprite_scan_write_us` が最も大きい可能性が高い。

理由:

- visible sprite ごとに最大 8 回の conditional store がある
- H flip / non flip で分岐している
- `pat0` / `pat1` から 2bit pixel を取り出す shift が複数ある

次点候補は `ppu_sprite_scan_fetch_us`。

理由:

- `PPUBANK[bank]` 参照と pattern plane load が visible sprite ごとに発生する
- 8x16 sprite では bank / addr 計算が少し増える

`ppu_sprite_scan_oam_us` は、visible sprite 数が少ない場面では軽い可能性がある。
ただし sprite 64 件の non-visible 判定は今回の追加 3 fields に含めないため、`ppu_sprite_scan_us - (oam + fetch + write)` が大きい場合は、次に non-visible 走査固定費を測る。

## やらないこと

今回の計測版では、次は行わない。

- sprite 表示ロジックの変更
- `pSprBuf` 形式変更
- range 最適化の変更
- sprite pattern LUT 化
- H flip / non flip write の最適化
- OAM scan の algorithm 変更
- release 用 version としての採用

## build

最低 1 回:

- `1.1.13` build

確認項目:

- build 成功
- banner
- UF2 SHA-256
- ELF SHA-256
- `text / data / bss`
- `strings build/Picocalc_NESco.elf | rg "PicoCalc NESco Ver|ppu_sprite_scan_oam_us|CORE1_BASE"` で banner と log format を確認する

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
- F5 screenshot が撮れる
- `ESC` で ROM menu に戻れる
- `[ROM_START]` で ROM 名が紐づく
- `[CORE1_BASE]` に追加 fields が出る

比較項目:

- `ppu_sprite_scan_us`
- `ppu_sprite_scan_oam_us`
- `ppu_sprite_scan_fetch_us`
- `ppu_sprite_scan_write_us`
- `ppu_sprite_comp_us`
- `ppu_sprite_us`
- `fps_x100`
- `frame_us_avg`

## 判断

この計測版で採用 / 不採用を判断する対象は、計測結果そのものではなく、次の最適化対象である。

判断例:

- `ppu_sprite_scan_write_us` が最大なら、sprite buffer write の分岐 / shift 削減を計画する
- `ppu_sprite_scan_fetch_us` が最大なら、pattern fetch / expand の LUT または前処理を計画する
- 追加 3 fields が小さく、差分が大きいなら、non-visible OAM scan 固定費を別途測る

## 手戻り

この計測版は 1 commit にまとめる。

計測 overhead が大きすぎる場合は、その commit を revert する。

次の最適化へ進む前に、計測 fields を残すか外すかを判断する。
