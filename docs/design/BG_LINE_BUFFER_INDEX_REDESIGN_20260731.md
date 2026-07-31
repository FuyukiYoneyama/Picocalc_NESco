# BG line buffer palette index redesign

作成日: 2026-07-31

対象 branch: 未作成

対象 version: `1.1.26` 基準

関連文書: `docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md`

## 目的

scanline buffer が保持する内容を、RGB565 の色から palette RAM index の byte へ変える。
色への変換は core1 の LCD packing 時に行う。

狙いは core0 の PPU background 描画を軽くすることである。
`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md` で確認したとおり、
normal view の fps を決めているのは LCD バスではなく core0 であり、
fps を上げるには core0 側を削るしかない。

この文書は設計と、実装前に取る計測、判断基準、実装手順までを範囲とする。

## 根拠と、根拠の限界

### 対象を background に置く理由

`1.1.5` の内訳計測では、`draw_us/frame 27.5 ms` のうち
`ppu_bg_tile_us/frame 19.7 ms` であり、background tile 描画が draw 時間の約 7 割だった。

### この数字をそのまま使えない理由

`1.1.5` の計測は `2026-04-29` 時点であり、その後に入った
background tile render LUT 化による `fps +78%` 級の改善より **前** である。

したがって `1.1.26` 現在、background tile が draw に占める割合は不明である。
7 割のままかもしれないし、大きく下がっているかもしれない。

この文書の効果見積もりはすべてこの比率に依存する。
実装判断の前に `1.1.26` で内訳を取り直す必要がある。後述の計測 D がこれにあたる。

### 既に不採用になっている案との違い

background 周辺では以下が実験済みである。

- `1.1.9` background opaque LUT 実験 (不採用)
- `1.1.7` background full tile direct path 実験 (不採用)
- `1.1.6` `renderBgTile()` full tile path 小改善

いずれも `dst_opaque` の生成を安くする、または descriptor の渡し方を変える方向であり、
line buffer のデータ形式そのものは変えていない。

本案は `dst_opaque` を安くするのではなく、配列ごと消す。
方向が異なるため、過去の不採用は本案の否定材料にはならない。
ただし「この周辺の小改善は効かなかった」という事実は、
本案も構造変更として成立しなければ効かない可能性が高いことを示している。

## 現状のデータ構造

### background 描画

`infones/InfoNES.cpp` の `renderBgTileFull()`:

```cpp
dst[0] = pal[(packed_hi >> 6) & 0x03u];
dst[1] = pal[(packed_hi >> 4) & 0x03u];
/* ... dst[7] まで、WORD store x 8 ... */

dst_opaque[0] = (BYTE)((opaque_hi >> 3) & 0x01u);
/* ... dst_opaque[7] まで、BYTE store x 8 ... */
```

8 px tile あたり、halfword store 8 回、byte store 8 回、`pal[]` からの load 8 回、
opaque LUT からの load 2 回である。

### palette

`pal` の実体は `PalTable[32]` の連続 4 entry である。

```cpp
pPalTbl = &PalTable[(((attrBase[attrGroup] >> (attrHalf + nY4)) & 3) << 2)];
```

`PalTable` は RGB565 を保持する。書き込みは `infones/K6502_rw.h` の 2 か所だけである。

```cpp
/* 0x3f00 / 0x3f10 書き込み: backdrop を 8 entry へ mirror */
PalTable[0x00] = PalTable[0x04] = PalTable[0x08] = PalTable[0x0c] =
    PalTable[0x10] = PalTable[0x14] = PalTable[0x18] = PalTable[0x1c] =
        InfoNES_Palette444ToRgb565(NesPalette[vramData]);

/* それ以外 */
PalTable[addr & 0x1f] = InfoNES_Palette444ToRgb565(NesPalette[vramData]);
```

RGB444 から RGB565 への変換は描画時ではなく palette 書き込み時に行われている。

### opaque

`BackgroundOpaqueLine[256]` の参照は全部で 6 か所である。

- 定義 1 か所
- `InfoNES_DrawLine()` 内の書き込み / clear 4 か所
- 読み出しは `compositeSpriteRange()` へ渡す 1 か所のみ

sprite 0 hit 判定 (`InfoNES_GetSprHitY()`) はこの配列を参照していない。
したがって読み手は sprite 合成だけであり、配列を廃止できる。

### core1 への受け渡し

`platform/display.c`:

- `InfoNES_PreDrawLine()` が `InfoNES_SetLineBuffer(s_line_buffer, 256)` を呼ぶ
- `InfoNES_PostDrawLine()` が `s_line_buffer` を queue item へ copy する
- queue item は `WORD pixels[256]`、queue depth は `DISPLAY_LCD_WORKER_QUEUE_DEPTH = 4`

## 提案する設計

scanline buffer を `WORD`(RGB565) から `BYTE`(palette RAM index 0..31) へ変える。

### background 描画

```cpp
const BYTE base = pal_group << 2;          /* 0, 4, 8, 12 */
dst[0] = (BYTE)(base | ((packed_hi >> 6) & 3));
/* ... dst[7] まで、BYTE store x 8 ... */
```

`pal[]` からの load が消える。store 幅が halfword から byte になる。

### opaque

`BackgroundOpaqueLine` を廃止し、`(dst[i] & 3) != 0` で判定する。

`compositeSpriteRange()` は同じ index `i` について
`bgOpaque[i]` の read が `buf[i]` の write より前にあり、
sprite は `pSprBuf` へ事前に merge されてから 1 パスで合成されるため、
1 px あたり write は 1 回だけである。したがって buffer 自身からの導出で安全である。

### sprite 合成

```cpp
buf[i] = (BYTE)(0x10 | (v & 0x0f));
```

### core1 側の変換

core1 の packing で `PalTable[idx]` を引く。

## 出力が変わらないことの根拠

core1 が引く `PalTable` は、現在 core0 が `pal[]` 経由で引いているものと同一の配列である。
したがって同じ scanline に対して同じ値が出る。

palette index 0 の扱いも一致する。
`$3f00` / `$3f10` 書き込みは `PalTable[0x00]` `[0x04]` `[0x08]` `[0x0c]` `[0x10]`
`[0x14]` `[0x18]` `[0x1c]` へ mirror されるため、
`base | 0` すなわち `PalTable[0]` `[4]` `[8]` `[12]` は backdrop を返す。
これは現在 `pal[0]` が返す値と同じである。

## 削減されるもの

| 項目 | 現在 | 変更後 |
|---|---|---|
| `pal[]` からの load | 8 / tile | 0 |
| opaque LUT からの load | 2 / tile | 0 |
| 画素 store 幅 | halfword x 8 | byte x 8 |
| `dst_opaque` への store | 8 / tile | 0 |
| `BackgroundOpaqueLine` の clear | 256 byte / scanline | 0 |
| queue への copy | 512 byte / scanline | 256 byte / scanline |
| 非表示 line の clear | 512 byte | 256 byte |

### 削減量は「幅を狭めること」からは出ない

Cortex-M0+ では `LDRB` `LDRH` `STRB` `STRH` がいずれも 2 cycle である。
したがって画素 store を halfword から byte へ狭めても、**store 自体の cycle は減らない**。

削減は幅ではなく、操作を消すことから出る。tile あたりで消えるのは次である。

| 消える操作 | 回数 / tile | cycle |
|---|---|---|
| `pal[]` からの load | 8 | 16 |
| `dst_opaque` への store | 8 | 16 |
| opaque LUT からの load | 2 | 4 |
| opaque の shift / mask | 8 組 | 約 16 |

合計で約 52 cycle / tile である。

### 理論値

上記から、除去される命令とメモリ操作を cycle 換算すると次になる。

| 項目 | 概算 |
|---|---|
| tile 内側 loop | 32 tile x 240 line x 52 cycle = 約 0.40 M cycle = 1.60 ms |
| `BackgroundOpaqueLine` の clear 廃止 | 約 0.04 M cycle = 0.15 ms |
| queue copy の半減 | 約 0.06 M cycle = 0.25 ms |
| 合計 | **約 2.0 ms / frame** |

**この値は理論値であり、実効値の予測ではない。** 実効値は段階 2 で測定する。

理論値と実効値がずれる要因は次のとおりである。

- `g_perf_ppu_bg_tile_us` が測る区間には、attribute / name table / CHR の fetch、
  `g_bg_tile_pair_idx4` の load、pointer 更新、loop 制御が含まれる。
  本案が削るのは `renderBgTileFull()` の中だけなので、
  `bg_tile` 時間が同じ比率で減るわけではない
- 型変更後の命令列は compiler 次第である
- core0 と core1 の SRAM bank 競合は cycle 単純計算に乗らない
- palette snapshot の判定と copy が新たに加わる
- core0 から消えた palette lookup は core1 へ移るため、frame 全体では相殺され得る
  (後述の core1 側計測が必要な理由)
- frame time は background 以外の処理にも律速される

frame 全体への効果は background tile が draw に占める割合にも比例するが、
その割合は `1.1.26` で不明である。計測 3 の結果を得てから見積もる。

## 副次効果

### LCD の COLMOD 12 bit/pixel が安く入る

`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md` の候補 1 は、
core1 の packer を 2 px = 3 byte 詰めへ書き換える必要があった。

本案を入れると core1 は元から index から色への LUT を引くため、
LUT 出力を RGB565 にするか RGB444 にするかの違いだけになる。
`InfoNES_Palette444ToRgb565()` が変換の単一箇所であることも確認済みである。

同分析で挙げた「stretch が core1 律速へ移る可能性」も、
core0 から core1 へ渡るデータ量が半分になるぶん緩む。

### queue depth を増やせる (RAM 増なしとは限らない)

queue item が `WORD pixels[256]` から `BYTE pixels[256]` になるため、
同じ RAM でより深い queue を取れる。
これは core0 が `PostDrawLine()` で queue full を待つ時間を直接減らす。
LCD 帯域分析で「normal view で期待できるのは二次効果のみ」と結論した、
その二次効果を大きくする側の変更でもある。

ただし palette snapshot を載せるため、**depth 8 は RAM 増なしでは成立しない**。

`display_lcd_worker_item_t` の現在の実サイズは 540 byte である。

```text
type 4 + scanline 4 + viewport_x/y/w/h 16 + scale_mode 4 + pixels 512 = 540
540 x 4 slot = 2,160 byte   (HISTORY の .bss 実測と一致)
```

画素を byte 化すると 1 item は 284 byte になる。

| 構成 | 計算 | 現在 2,160 byte との差 |
|---|---|---|
| depth 6 + palette ring 4 x 64 | 284 x 6 + 256 = 1,960 | **-200** |
| depth 8 + palette ring 4 x 64 | 284 x 8 + 256 = 2,528 | **+368** |
| depth 8 + snapshot を item 内に持つ | (284 + 64) x 8 = 2,784 | +624 |

採用する構成は **palette snapshot を queue item の外に出し、
version 付きの独立 ring として持つ形**とする。理由は 3 つある。

- snapshot 個数を queue depth から切り離せる。
  palette 変更は frame あたり 0 回から数回なので、ring 4 本で足りる
- depth を変えても snapshot の RAM が増えない
- version 番号による受け渡しが自然になり、後述の初期化規則を書きやすい

まず **depth 6 で始める**。この構成は現在より 200 byte 少なく、RAM 増がない。
depth 8 へ上げるのは、計測 2 の `lcd_queue_wait_us` が
frame 時間に対して無視できない場合に限る。そのとき `+368 byte` を許容する。

`.bss` には余裕がある (`1.0.15` 時点で静的領域末尾から heap limit まで 122,328 byte)
ため `+368 byte` 自体は問題にならないが、
「RAM 増なし」と書くのは誤りなので、増分を明示して判断する。

### copy 自体を消せる可能性

`InfoNES_SetLineBuffer()` に queue slot の buffer を直接渡せば、
`PostDrawLine()` の copy が不要になる。queue が深くなるほど成立しやすい。

これは本案の必須部分ではない。効果を確認してから任意で行う。

## リスクと対策

### frame 途中の palette 変更

本案の唯一の実質的なリスクである。

現在は描画時点の色が scanline buffer へ焼き込まれる。
変換を core1 へ移すと、queue に滞留している scanline に対して
後からの palette 変更が遡って適用される。
status bar の色替えなど、frame 途中の palette 書き換えは珍しくない。

対策は `PalTable` の snapshot である。

なお queue item は既に viewport と scale mode を per-line で snapshot している。

```c
/*
 * Viewport and scale are snapshotted with each queued line.
 * This prevents core1 from reading globals that may change when the user
 * toggles stretch mode, opens the ROM menu, or starts screenshot capture.
 */
```

palette snapshot は同じ理由で同じことを palette に対して行うだけであり、
このコードベースで確立済みの手法である。

### snapshot の受け渡し規則

dirty flag だけでは足りない。次を規則として定める。

1. **snapshot は version 付きの独立 ring に置く。** queue item は version 番号だけを持つ
2. **`PalTable` 書き込みで dirty flag を立てる。** 書き込み箇所は
   `infones/K6502_rw.h` の 2 か所だけである
3. **dirty のとき、`PostDrawLine()` で ring の次の枠へ copy し、version を進める。**
   copy を完了させてから queue item を publish する。
   publish 後に copy すると core1 が中途の palette を読む
4. **dirty でない scanline は、直近の version 番号をそのまま持つ**
5. **初回 snapshot を強制する。** 以下の時点で dirty flag を立て、
   最初の scanline が必ず snapshot を伴うようにする
   - reset 後
   - ROM 切り替え後
   - queue 再初期化 (`drain`) 後
   - 表示モード切替後
   - ROM menu からの復帰後
6. **上記の時点で version をリセットする**
7. **core1 は queue item の version で ring を引く。**
   version が未初期化を指す場合は、その scanline を描画しない

規則 5 が最も重要である。これがないと、
palette 書き込みが起きなかった起動直後の scanline で、
core1 が未初期化の、あるいは前の ROM の palette を使う。
`InfoNES()` の初期化は `InfoNES_MemorySet(PalTable, 0, sizeof PalTable)` で
`PalTable` を 0 埋めするため、この穴は「黒画面」ではなく
「前 ROM の色が残る」形で出る可能性がある。

通常 frame で palette 書き換えは 0 回から数回程度なので、copy 費用は無視できる。

### ring 段数

palette 変更は稀だが、queue に滞留している scanline の数だけ
異なる version が同時に生きうる。したがって ring 段数は queue depth 以上あれば安全である。
depth 6 に対して ring 4 本で足りるかは、
1 frame 内で 4 回を超える palette 変更が連続 scanline で起きないことに依存する。

安全側に倒し、**ring 段数は queue depth と同数**とする。
depth 6 なら 6 x 64 = 384 byte、depth 8 なら 512 byte である。
上の RAM 表は ring 4 本で計算しているため、この分を足すと
depth 6 で `284 x 6 + 384 = 2,088` (現在比 -72)、
depth 8 で `284 x 8 + 512 = 2,784` (現在比 +624) になる。
depth 6 が RAM 増なしである結論は変わらない。

### queue depth を増やすことの副作用

queue が深くなると、core0 が core1 より先行できる scanline 数が増える。
これは palette snapshot の必要性を強めるが、上記対策で吸収できる。

mid-frame の mapper CHR 切り替えは scanline buffer の内容に影響しないため、
snapshot の対象外である。

## 代替案との比較

scanline buffer へ何を入れるかには 2 案ある。

- **案 A**: palette RAM index (0..31) を入れる。本文書の提案
- **案 B**: その時点で palette から引いた NES master color (0..63) を入れる

案 B は snapshot が不要になる。描画時点で色が確定するため、
frame 途中の palette 変更が遡らない。core1 は固定 64 色の LUT を引くだけでよい。
実装は明らかに単純で安全である。

しかし案 B は、本案が狙っている削減のほとんどを取り逃す。

| 削減対象 | 案 A | 案 B |
|---|---|---|
| `pal[]` からの load 8 / tile | 消える | **残る** (`LDRH` が `LDRB` になるだけ) |
| `dst_opaque` への store 8 / tile | 消える | **残る** |
| opaque LUT からの load 2 / tile | 消える | **残る** |
| `BackgroundOpaqueLine` の clear | 消える | **残る** |
| queue copy の半減 | あり | あり |

案 B で `BackgroundOpaqueLine` を消せない理由は、
opaque 判定が「tile pattern の 2 bit index が 0 でないこと」だからである。
master color を書いてしまうと、
backdrop として書かれた色と、たまたま同じ色の不透明画素とを区別できない。
案 A は下位 2 bit がそのまま pattern index なので `(v & 3) != 0` で判定できる。

Cortex-M0+ では `LDRB` と `LDRH` がどちらも 2 cycle なので、
load の幅が狭くなること自体には価値がない。

結果として案 B の削減は queue copy の半減、
すなわち理論値で **約 0.25 ms / frame** にとどまる。
案 A の約 2.0 ms に対して 1 桁小さい。

したがって **案 A を採用し、snapshot の複雑さを引き受ける**。
この比較は、snapshot 機構がなぜ払う価値のあるコストなのかの根拠でもある。

段階 2 の打ち切り基準に触れた場合、案 B へ退避するのではなく、
本案そのものを不採用として記録する。
案 B 単独では改善幅が段階 3 の queue 化と重複しており、独立した価値が小さい。

## 発展案 (本案とは分けて扱う)

index が byte になると、`g_bg_tile_pair_idx4[256]` を `uint32_t` 版へ拡張し、
4 px を 1 word store にまとめられる。tile あたり store 8 回が 2 回になる。

ただし Cortex-M0+ は非アラインアクセスをサポートしない。
tile の書き込み先は水平 scroll 量だけずれるため 4 byte 境界に乗る保証がない。
33 tile 分のアライン済み scratch へ描いてからずらす追加 copy が必要になる。

本案の効果を測ってから、別課題として判断する。

## 実装前に取る計測

計測せずに実装しない。理由は 2 つある。

- 効果見積もりが background tile の占有率に依存しており、その値が `1.1.26` で不明である
- 実装後に効果を測るための基準値が必要である

### 必要な build は 2 つである

`[CORE1_SUMMARY]` の `cpu_us` `ppu_us` `apu_us` `other_us` という 4 分類では、
PPU 処理のうち background tile が何割かは分からない。この 4 分類から推定してはならない。

必要な値は既にカウンタとして存在する。`infones/InfoNES.cpp`:

```c
uint64_t g_perf_draw_us = 0;
uint64_t g_perf_ppu_bg_us = 0;
uint64_t g_perf_ppu_bg_tile_us = 0;
uint64_t g_perf_ppu_sprite_us = 0;
```

`g_perf_ppu_bg_tile_us` は `bg_tile_start_us` (line 1724) から
加算点 (line 1800) までで、**この区間に `time_us_64()` は 1 つも入っていない**。
すなわち tile ごとではなく scanline ごとの計測であり、
`1.1.5` / `1.1.6` で重すぎるとされた tile ごと計測とは別物である。
tile ごと計測は `1.1.8` で既に除去されている。

判断に使う値を次と定義する。

```text
background tile 占有率 = g_perf_ppu_bg_tile_us / g_perf_draw_us
```

これらは `kDetailedPerfLogToSerial` (`infones/InfoNES.cpp:217`、現在 `false`) で
有効になる。有効時の `time_us_64()` は scanline あたり約 22 回、
frame あたり約 5,280 回で、**約 0.4 ms から 0.5 ms / frame の計測負荷**が乗る。

したがって、この build の fps を基準値に使ってはならない。**build を 2 つ用意する。**

| build | 設定 | 取る値 |
|---|---|---|
| build 1 | `kDetailedPerfLogToSerial=false`、`NESCO_CORE1_BASELINE_LOG=ON` | 計測 A、計測 1、計測 2 |
| build 2 | `kDetailedPerfLogToSerial=true` | 計測 3 の **比率のみ** |

build 2 からは比率だけを取り、fps と frame time は使わない。
実機作業は 1 回の session で両方を流せる。

### 順序

計測 3 を最初に置く。これが不発なら以降は不要になるためである。

1. **計測 3: `1.1.26` の draw 内訳** (build 2)
   - `g_perf_ppu_bg_tile_us / g_perf_draw_us` を 1 秒ごとに出力する
   - 併せて `g_perf_ppu_bg_us` `g_perf_ppu_sprite_us` も出し、
     background 以外が支配的だった場合に次の対象を選べるようにする
   - 対象: `LodeRunner.nes` `Xevious.nes` `Project_DART_V1.0.nes`

2. **計測 A: 基準 frame time** (build 1)
   - 実装後の比較基準を作る
   - normal 3 ROM に加えて `Xevious.nes` の stretch も取る
     (これは LCD 帯域分析の計測 1 を兼ねる)

3. **計測 B: `lcd_queue_wait_us` / `lcd_queue_wait_count`** (build 1)
   - queue depth 4 が core0 をどれだけ止めているかを知る
   - LCD 帯域分析の計測 2 と同一
   - 本案の queue depth 化の効果見積もりにも使う

### 比較は fps ではなく frame time で行う

fps 比では削減された実時間が分からない。
`47.99 fps` の 3% と `55.50 fps` の 3% では削減 µs が異なる。
最適化の効果は 1 frame あたり何 µs 減ったかで判断する。

計測 A では次を記録する。実装後も同じ項目で比較する。

- `frame_us` 平均
- `frame_us` 中央値
- `frame_us` 95 percentile
- `frame_us` 最大
- `lcd_queue_wait_us` / `lcd_queue_wait_count`

平均だけでは、引っかかりの改善または悪化が見えない。
本案は core0 の平均負荷を下げる一方で snapshot copy という
散発的な処理を増やすため、最大値と 95 percentile の確認が要る。

### 判断基準

計測 3 の結果で分岐する。

| background tile の draw 占有率 | 判断 |
|---|---|
| 60% 以上 | 本案を実装する |
| 40% 以上 60% 未満 | 実装するが、段階 2 の実測で打ち切り判断を行う |
| 40% 未満 | 本案は見送る。draw の内訳で最大の項目を対象に検討し直す |

計測 B が大きい場合 (frame 時間に対して無視できない場合) は、
本案とは独立に queue depth を増やす価値があるため、
本案を見送る場合でも queue item の byte 化だけを行う選択肢が残る。

## 実装方法

`AGENTS.md` の「大きな変更は build 可能な単位に分ける」に従い、
各段階が単独で build でき、単独で実機確認できる形に割る。

各段階で `platform/version.h` の `PICOCALC_NESCO_VERSION` を更新する。
実機未確認の段階を `main` へ push しない。

### 段階 1: palette snapshot 機構 (動作不変)

- `infones/K6502_rw.h` の `PalTable` 書き込み 2 か所に dirty flag を追加する
- queue item に `PalTable` snapshot 領域と有効フラグを追加する
- `InfoNES_PostDrawLine()` で dirty のときだけ snapshot を copy し、flag を落とす
- core1 側は snapshot を受け取るが、まだ使わない

この段階では色は従来どおり core0 が焼くため、表示は変わらない。
snapshot が意図した頻度でしか発生しないことを log で確認する。

### 段階 2: index 描画への切り替え (効果測定ポイント)

- `renderBgTileFull()` / `renderPacked4()` を index 書き込みへ変更する
- `BackgroundOpaqueLine` を廃止し、`compositeSpriteRange()` の opaque 判定を
  buffer 自身からの導出へ変更する
- sprite 合成を index 書き込みへ変更する
- 画面 off / clip 時の clear を byte 幅へ変更する
- core1 の packing に `PalTable` snapshot による index から色への変換を追加する
- `WorkLine` の型と `InfoNES_SetLineBuffer()` の契約を `BYTE*` へ変更する

ここで計測 A と同条件・同 build 設定で frame time を取り、削減量を確認する。

**core1 側の負荷も同時に測る。** 本案は core0 の palette lookup を core1 へ移すため、
core0 を軽くした代わりに core1 が次の律速になりうる。
normal view は LCD 転送の余裕が小さく (`Xevious.nes` で 2.29 ms)、
core0 の queue full 待ちだけでは
**core1 が間に合わず queue が枯れる側の問題を検出できない**。

段階 2 で追加する計測項目:

- core1 の 1 line packing 時間 (平均)
- core1 の 1 line packing 時間 (最大)
- queue occupancy の最小値
- core1 が LCD DMA 完了を待った時間

250 MHz で 1 画素あたり load を 1 回増やす程度なので間に合う公算は高いが、
机上で断定せず測る。

打ち切り基準: 3 ROM すべてで `frame_us` 平均の改善が 3% 未満なら、
本案を不採用として記録し、
`docs/project/Picocalc_NESco_HISTORY.md` へ経緯を残して段階 1 ごと revert する。

`frame_us` 95 percentile または最大が悪化した場合は、
平均が改善していても採用しない。snapshot copy が原因なら
ring 段数または copy 契機を見直す。

確認項目:

- frame 途中で palette を変える ROM で色化けが出ないこと
  (status bar のある ROM を選ぶ)
- sprite と background の優先順位が変わらないこと
- 画面左端 8 px clip の挙動が変わらないこと
- ROM menu / help / screenshot viewer は index を使わない経路なので影響がないこと

screenshot は影響を受けない。
`platform/screenshot.c` は scanline buffer ではなく
`lcd_readback_rect_rgb565()` で LCD panel から読み戻しているため、
core0 側の buffer 形式とは独立である。
ただし段階 4 で COLMOD を 12 bit/pixel へ変更する場合は、
読み戻し側の形式も合わせる必要がある。

### 段階 3: queue item の byte 化と depth 化 (効果測定ポイント)

- queue item を `BYTE pixels[256]` へ変更する
- `DISPLAY_LCD_WORKER_QUEUE_DEPTH` を 4 から **6** へ変更する
  - この構成は現在より `.bss` が 72 byte 少ない
  - depth 8 へ上げるのは、計測 B の `lcd_queue_wait_us` が
    frame 時間に対して無視できない場合に限る。そのとき `+624 byte` を許容する
- palette ring 段数を queue depth と同数にする
- `.bss` の増減を実測し、上記の計算と一致することを確認する

ここで `lcd_queue_wait_us` を計測 B と同条件で取り直し、減少を確認する。
併せて段階 2 で追加した core1 側の計測も取り、
queue を深くしたことで core1 が枯れていないことを確認する。

### 段階 4 以降 (任意、別課題)

- LCD COLMOD 12 bit/pixel 化
  (`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md` の候補 1)
- `InfoNES_SetLineBuffer()` へ queue slot を直接渡して copy を廃止する
- 4 px 1 word store 化

段階 4 以降は、段階 3 までの実測を見てから個別に判断する。

## 未確認事項

- `1.1.26` における background tile の draw 占有率
- `g_perf_ppu_bg_tile_us` の区間のうち `renderBgTileFull()` が占める割合
  (本案が削るのはこの内側だけなので、占有率がそのまま削減率にはならない)
- 段階 2 の後、sprite 合成が core0 に残る形で問題がないか
  (index 化により sprite 合成も byte 操作になるため軽くなる見込みだが未計測)
- core1 の 1 line packing 時間が、index から色への変換を足しても
  strip DMA の時間内に収まるか
- palette snapshot の発生頻度が実 ROM でどの程度か
- 1 frame 内で連続する scanline に対して、
  queue depth を超える回数の palette 変更が起きる ROM があるか

## この文書の改訂

初版 (`554a630`) から次を修正した。外部レビューの指摘による。

- 計測 3 の方法を `[CORE1_SUMMARY]` の 4 分類から
  `g_perf_ppu_bg_tile_us / g_perf_draw_us` へ変更した。
  4 分類では background tile の割合は分からない。
  併せて、計測負荷のため build を 2 つに分ける必要があることを明記した
- 「RAM 増なしで depth 8」は誤りだったので訂正した。
  snapshot 込みでは depth 6 が RAM 中立、depth 8 は増加になる
- 約 1.1 ms / frame という削減量を理論値として位置づけ直し、
  内訳と、実効値がずれる要因を明記した。
  併せて Cortex-M0+ では store 幅を狭めても cycle が減らないことを反映し、
  理論値を約 2.0 ms へ見直した
- palette snapshot の受け渡し規則を追加した。
  特に reset / ROM 切替時の初回 snapshot 強制が初版では抜けていた
- 段階 2 に core1 側の計測を追加した
- 比較の基準を fps 比から `frame_us` (平均、中央値、95 percentile、最大) へ変更した
- 代替案 (NES master color 方式) との比較を追加した
