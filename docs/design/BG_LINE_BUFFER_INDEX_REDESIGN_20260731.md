# BG line buffer palette index redesign

作成日: 2026-07-31

対象 branch: `perf/bg-tile-share-log`

対象 version: `1.1.27` 基準

関連文書: `docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md`

## 目的

scanline buffer が保持する内容を、RGB565 の色から palette RAM index の byte へ変える。
色への変換は core1 の LCD packing 時に行う。

狙いは core0 の PPU background 描画を軽くすることである。
`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md` で確認したとおり、
normal view の fps を決めているのは LCD バスではなく core0 であり、
fps を上げるには core0 側を削るしかない。

この文書は設計と、実装前に取る計測、判断基準、実装手順までを範囲とする。

## 実装開始判断（2026-07-31）

段階 0 の実機計測を完了した。実測の `bg_tile_us_per_frame` は次のとおりである。

| ROM / 表示 | `bg_tile_us_per_frame` |
|---|---:|
| LodeRunner / normal | 6.862 ms |
| Project_DART_V1.0 / normal | 6.692 ms |
| Xevious / normal | 6.676 ms |
| Xevious / stretch | 6.633 ms |

いずれも実装判定の 4--8 ms 帯に入ったため、本案を実装する。段階 2 の採否は、
後述の固定した A/B 比較基準で決める。`lcd_queue_wait_*` は現在のログでは
計測窓ごとにリセットされない累積値だったため、queue depth の判断には使わない。
この点は段階 2 の計測 API で修正し、段階 3 の直前に基準値を取り直す。

## 根拠と、根拠の限界

### 対象を background に置く理由

`1.1.5` の内訳計測では、`draw_us/frame 27.5 ms` のうち
`ppu_bg_tile_us/frame 19.7 ms` であり、background tile 描画が draw 時間の約 7 割だった。

### この数字をそのまま使えない理由

`1.1.5` の計測は `2026-04-29` 時点であり、その後に入った
background tile render LUT 化による `fps +78%` 級の改善より **前** である。

したがって `1.1.27` 現在、background tile が draw に占める割合は不明である。
ただし段階 0 で絶対時間は 6.6--6.9 ms/frame と取り直せた。

この文書の効果見積もりはすべてこの比率に依存する。
実装判断には比率でなく上記の絶対時間を使う。後述の計測 3 は完了済みである。

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

scanline buffer を `WORD`(RGB565) から `BYTE` へ変える。値 `0x00..0x1f` は
palette RAM index、`0x20` は RGB565 black 専用の予約 index とする。

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

core1 は 64 entry のローカル LUT を使う。`0x00..0x1f` は snapshot からコピーした
`PalTable`、`0x20..0x3f` は RGB565 black (`0x0000`) に固定する。snapshot に載せるのは
従来どおり前半 32 entry (64 byte) だけである。

worker を使わない fallback packer も同じ規則にする。`idx < 0x20` では現在の
`PalTable[idx]`、`idx >= 0x20` では `0x0000` を出力する。

## 出力が変わらないことの根拠

core1 が引く `PalTable` は、現在 core0 が `pal[]` 経由で引いているものと同一の配列である。
したがって同じ scanline に対して同じ値が出る。

palette index 0 の扱いも一致する。
`$3f00` / `$3f10` 書き込みは `PalTable[0x00]` `[0x04]` `[0x08]` `[0x0c]` `[0x10]`
`[0x14]` `[0x18]` `[0x1c]` へ mirror されるため、
`base | 0` すなわち `PalTable[0]` `[4]` `[8]` `[12]` は backdrop を返す。
これは現在 `pal[0]` が返す値と同じである。

### RGB565 black を書く clear の扱い

現行の次の 5 箇所は palette 色ではなく RGB565 の `0x0000` (black) を明示的に書く。

| 箇所 | 現在の意味 | index 化後に書く値 |
|---|---|---:|
| `InfoNES_DrawFrame()` の上下 4 line | `InfoNES_DrawLine()` を呼ばない black line | `0x20` |
| `InfoNES_DrawLine()` の `!R1_SHOW_SCR` | screen off | `0x20` |
| background 左端 8 px clip | clip black | `0x20` |
| 上下 clip | clip black | `0x20` |
| sprite 左端 8 px clip | clip black | `0x20` |

これらで index `0` を書いてはならない。`PalTable[0]` は backdrop 色であり、black とは
限らないためである。`0x20 & 3 == 0` なので、この予約 index は background opaque 判定では
透明として扱われ、現在の clear と同じ sprite 合成結果になる。

## 削減されるもの

| 項目 | 現在 | 変更後 |
|---|---|---|
| `pal[]` からの load | 8 / tile | 0 |
| opaque LUT からの load | 2 / tile | 0 |
| 画素 store 幅 | halfword x 8 | byte x 8 |
| `dst_opaque` への store | 8 / tile | 0 |
| `BackgroundOpaqueLine` の clear | 256 byte / scanline | 0 |
| queue line traffic (snapshot なし) | 1,052 byte / scanline | 608 byte / scanline |
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
| tile 内側 loop | 32 tile x 232 line x 52 cycle = 約 0.39 M cycle = 1.54 ms |
| `BackgroundOpaqueLine` の clear 廃止 | 約 0.04 M cycle = 0.15 ms |
| queue line traffic の削減 | 最大 約 0.25 ms |
| 合計 | **最大 約 1.9 ms / frame** |

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
  (ただし strip 単位の机上見積もりでは DMA 時間の約 6% に収まる)
- 上の 1.54 ms は `InfoNES_DrawLine()` が走る 232 line だけを数えた
  `renderBgTileFull()` 内側のモデルである。scroll に伴う左右の部分 tile は
  `renderPacked4()` を通るため、この式に含まれない
- frame time は background 以外の処理にも律速される

frame 全体への効果は background tile が draw に占める割合にも比例するが、
その割合は `1.1.27` でも不明である。採否は段階 2 の frame time 実測で決める。

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

画素を byte 化すると画素部は 256 byte になる。
snapshot 64 byte と `palette_version` / `palette_valid` の 4 byte を加えて、
1 item は 352 byte になる。

| 構成 | 計算 | 現在の queue 2,160 byte との差 |
|---|---|---|
| depth 6 + item 内 snapshot + 64 entry core1 LUT | 352 x 6 + 128 = 2,240 | **+80** |
| depth 8 + item 内 snapshot + 64 entry core1 LUT | 352 x 8 + 128 = 2,944 | **+784** |
| (参考) depth 6 + 外部 ring 6 段 + 64 entry core1 LUT | 284 x 6 + 384 + 128 = 2,216 | +56 |

snapshot を item 内に持つ方式は、外部 ring 方式と RAM がほぼ同じである。
depth 6 で比べて 40 byte の差しかない。
**RAM 上の利点がないのに競合の危険を抱える理由がない**ため、item 内方式を採る。

depth 6 は条件付きの将来段階とする。queue 関連だけなら +80 byte だが、段階 2 で
`s_line_buffer` が -256 byte、`BackgroundOpaqueLine` が -256 byte になるため、
全 static 領域では現在比 -432 byte である。depth 4 基準で窓単位の queue wait を取り直し、
frame time に対して無視できないと確認できた場合だけ depth 6 を実装する。
depth 8 を検討するのは、depth 6 後も queue wait の平均中央値が frame time の 1% 以上で、
p95 が改善する場合だけである。そのとき queue 関連 `+784 byte` を許容する。

`.bss` には余裕がある (`1.0.15` 時点で静的領域末尾から heap limit まで 122,328 byte)
ため depth 8 の queue 関連 `+784 byte`（段階 2 の配列削減込みでは現在比 `+272 byte`）自体は問題にならないが、
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

### 外部 ring 方式は現在の pop 実装では安全でない

初版では snapshot を queue item の外の version 付き ring に置く方式を採った。
これは現在の worker 実装と組み合わせると競合する。

`platform/display.c` の `display_lcd_worker_pop_item()`:

```c
display_lcd_worker_lock();
if (s_lcd_worker_queue_count > 0) {
    *item = s_lcd_worker_queue[s_lcd_worker_queue_head];
    s_lcd_worker_queue_head = (s_lcd_worker_queue_head + 1u) % DISPLAY_LCD_WORKER_QUEUE_DEPTH;
    s_lcd_worker_queue_count--;
    popped = true;
}
display_lcd_worker_unlock();
```

item を core1 ローカルへ copy した直後に count を減らして unlock し、
packing は lock の外で行う。したがって次が起こりうる。

```text
1. core1 が version V の item を pop する
2. count が減り、core0 は新しい line を enqueue できるようになる
3. core0 が最大 depth 本の line を積む
4. その過程で version V+depth の snapshot が ring slot V を上書きする
5. core1 は上書き後の ring を読んで version V の line を packing する
```

**pop 済みで packing 中の 1 item が、ring の生存数計算から抜けている。**
ring 段数を queue depth と同数にしても防げない。

### 採用する方式: snapshot を queue item 内に置く

上記を踏まえ、snapshot は **queue item 内**に持つ。

`pop_item()` は item 全体を lock 保持下で core1 ローカルへ copy するため、
snapshot も一緒に copy される。**この方式は構造上安全であり、
ring の寿命解析そのものが不要になる。** 危険を管理するのではなく消す。

RAM も外部 ring と同等である (後述)。

### snapshot の実装契約（固定）

この節は段階 1 の実装仕様である。ここにない共有状態・暗黙の同期を追加しない。

#### 所有者と API

palette の dirty/version は **core0 専用**の `platform/display.c` 静的状態とする。
`PalTable` の所有者を display に移すものではなく、display は「次に enqueue する
line に snapshot が必要か」だけを所有する。

`platform/display.h` に次の C API を宣言する。`K6502_rw.h` を取り込む翻訳単位は
`K6502.cpp` と `InfoNES_pAPU.cpp` の 2 つである。**両方**で `display.h` を
`K6502_rw.h` より前に include し、API を宣言済みにする。

```c
void display_lcd_worker_palette_mark_dirty(void);
void display_lcd_worker_palette_force_snapshot(void);
```

- `mark_dirty()` は `s_palette_dirty = true` だけを行う。version は進めない。
- `force_snapshot()` は `s_palette_dirty = true`、`s_palette_force = true`、
  `s_palette_version = 0` を設定する。core1 側の変数には書かない。
- 呼び出しはすべて core0 であり、これらの API 自体には lock を使わない。
  queue item を介して初めて core1 へ渡す。

`K6502_rw.h` の palette 書き込み 2 分岐では、`PalTable` への全代入を終えた直後に
`display_lcd_worker_palette_mark_dirty()` を 1 回だけ呼ぶ。mirror の 8 代入ごとに
呼んではならない。

#### item の内容と version の意味

`display_lcd_worker_item_t` へ次を追加する。

```c
WORD     palette[32];
uint16_t palette_version;
uint8_t  palette_valid;
```

`display_lcd_worker_submit_line()` は item 全体を zero 初期化しない。既存の表示属性、
pixels、`palette_valid`、`palette_version` を必ず明示代入してから `push_item()` する。
`palette_valid == 0` の `palette[32]` は読まない。これにより clean line に不要な
352 byte memset を入れない。

現在の line traffic は pixel `memcpy` 512 byte と queue item copy 540 byte の計 1,052 byte、
変更後は 256 byte と 352 byte の計 608 byte である。snapshot がある line だけはこれに
palette copy 64 byte が加わる。

1. `s_palette_dirty == false` なら `palette_valid = 0` とし、現在の
   `s_palette_version` を item に入れる。
2. dirty かつ `s_palette_force == false` なら、`s_palette_version` を 1 増やす。
3. dirty なら `PalTable[32]` を item 内 `palette` へ copy し、
   `palette_valid = 1`、その version を item に入れる。
4. copy 後に `s_palette_dirty = false`、`s_palette_force = false` とする。

従って起動・reset 後の最初の snapshot は version 0、以後の palette 変更を含む
snapshot は version 1, 2, ... となる。複数回の palette 書き込みが同じ scanline の
前に起きても snapshot は 1 個だけである。`uint16_t` の wrap は許容し、core1 は
「前の version + 1」ではなく item とローカル version の**一致**だけを検査する。

#### core1 の受信規則と異常時の表示

core1 は worker 内だけで `WORD core1_palette[64]`、`uint16_t core1_palette_version`、
`bool core1_palette_valid` を持つ。core0 はこれらへ直接書かない。

- 規則は **`DISPLAY_LCD_WORKER_ITEM_LINE` にだけ適用する**。`FRAME_END` は palette を
  持たない同期 marker であり、palette field を検査せず、protocol fault も増やさない。
- `palette_valid == 1` の LINE item は version の値に関係なく `core1_palette[0..31]` へ copy
  し、`core1_palette[32..63]` を black に設定して有効化する。これが reset 後に古い palette を
  使わせない同期点である。
- `palette_valid == 0` の LINE item は、ローカルが有効で、かつ version が一致するときだけ
  ローカル LUT を使う。
- それ以外は protocol fault として数え、**line を捨てず**、全 entry が 0 の一時
  palette でその line を pack する。strip の行数を保ち、問題を黒い line とログで
  可視化するためである。

初回 snapshot を queue item に入れるため、reset 時に core1 のローカル状態を共有変数で
無効化する必要はない。この設計は core0/core1 間の無保護な bool 書き換えを排除する。

#### 強制 snapshot の正確な配置

`display_lcd_worker_palette_force_snapshot()` を次の 3 箇所に置く。

| 箇所 | 呼ぶ位置 | カバーする契機 |
|---|---|---|
| `InfoNES_Reset()` | `InfoNES_MemorySet(PalTable, 0, sizeof PalTable)` の直後 | reset、`InfoNES_Load()` 経由の ROM 切替 |
| `display_lcd_worker_prepare_nes_view()` | queue reset の直後、worker を RUNNING にする前 | NES view 開始、ROM menu からの復帰、stretch 切替後 |
| `display_lcd_worker_stop_and_drain()` | DMA wait と queue reset の後、STOPPED にする前 | fullscreen UI、screenshot、menu 遷移、stretch 切替前 |

`InfoNES_Load()` へ別途呼び出しを足さない。成功時に必ず `InfoNES_Reset()` を通るためである。
3 箇所とも冪等であり、連続して呼ばれても最初の次 line に version 0 の snapshot が
載るだけである。

#### 検証用ログ

段階 1 では `NESCO_PALETTE_SNAPSHOT_LOG` CMake option を追加し、
`NESCO_CORE1_BASELINE_LOG` を含意させる。通常 build ではこの計測を入れない。
1 秒ごとに core0 が次の 1 行を出す。

```text
[PALETTE_SNAPSHOT] frames=N line_items=N snapshots=N forced=N applied=N protocol_faults=N version=V
```

`forced` は version 0 の snapshot 数、`applied` と `protocol_faults` は core1 から
frame-end ごとに handoff された値である。handoff は core1 が `FRAME_END` を処理した
時だけ既存 queue lock 下で共有 accumulator へ加算し、core0 は同じ lock 下で swap-and-zero
して読む。line ごとの lock や 64 bit の無保護な cross-core read は行わない。

合格条件は、通常プレイ・reset・stretch 往復・menu へ戻って別 ROM を開始する一連の操作で
`protocol_faults=0`、各遷移後に `forced>=1`、表示差分なしである。snapshot 数や version の
増分は ROM の palette 更新頻度に依存するため、固定値を期待しない。

強制 snapshot が最も重要である。これがないと、
palette 書き込みが起きなかった起動直後の scanline で、
core1 が未初期化の、あるいは前の ROM の palette を使う。
`InfoNES()` の初期化は `InfoNES_MemorySet(PalTable, 0, sizeof PalTable)` で
`PalTable` を 0 埋めするため、この穴は「黒画面」ではなく
「前 ROM の色が残る」形で出る可能性がある。

### 外部 ring を採る場合の必須条件

将来 RAM の都合で外部 ring へ移す場合は、次を満たさなければならない。

- **queue lock を保持したまま**、item 取得と同時に
  ring slot から core1 専用バッファへ palette を copy し、そのあとで count を減らす
- ring slot には snapshot 本体と**完全な version 番号**を持たせ、
  `slot.version == item.palette_version` を検証する。
  未初期化だけでなく上書き済み version も検出対象とする
- version が一致する限り copy は省略してよい

この条件を満たさない外部 ring 方式は採用しない。

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
案 A の最大約 1.9 ms に対して 1 桁小さい。

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

段階 0 の計測を完了してから実装する。理由は 2 つある。

- 効果見積もりが background tile の占有率に依存しており、その値が `1.1.27` でも不明である
- 実装後に効果を測るための基準値が必要である

### 計測は既存 build では取れない。計測用のコード変更が先に要る

`[FPS_SUMMARY]` の `cpu_us` `ppu_us` `apu_us` `other_us` という 4 分類では、
PPU 処理のうち background tile が何割かは分からない。この 4 分類から推定してはならない。

必要な値の一部はカウンタとして存在する。`infones/InfoNES.cpp`:

```c
uint64_t g_perf_ppu_bg_us = 0;        /* line 1839 で加算 */
uint64_t g_perf_ppu_bg_tile_us = 0;   /* line 1800 で加算 */
uint64_t g_perf_ppu_sprite_us = 0;    /* line 2219 で加算 */
uint64_t g_perf_frame_us_total = 0;   /* line 309 で加算 */
uint64_t g_perf_frame_us_max = 0;     /* line 312 で更新 */
```

`g_perf_ppu_bg_tile_us` は `bg_tile_start_us` (line 1724) から
加算点 (line 1800) までで、**この区間に `time_us_64()` は 1 つも入っていない**。
すなわち tile ごとではなく scanline ごとの計測であり、
`1.1.5` / `1.1.6` で重すぎるとされた tile ごと計測とは別物である。
tile ごと計測は `1.1.8` で既に除去されている。

これは段階 0 開始前の不足点だった。段階 0 で `[BG_SHARE]` と `[FRAME_STATS]`、
frame time sample 列を実装し、未使用の `g_perf_draw_us` は削除済みである。
現在このシンボルは存在しない。したがって `g_perf_ppu_bg_tile_us / g_perf_draw_us` を
使う設計には戻さない。

また出力の gate は 2 段になっている。

```c
constexpr bool kPerfLogToSerial =
#if defined(NESCO_CORE1_BASELINE_LOG)
    true;
#else
    false;
#endif
constexpr bool kDetailedPerfLogToSerial = false;
```

`perf_log_if_due()` は先頭で `if (!kPerfLogToSerial) return;` する。
`kDetailedPerfLogToSerial` はカウンタの加算を有効にするだけで、出力は有効にしない。
**計測 build では両方が必要である。**

### 段階 0 で加えた計測 build の変更（完了）

計測は専用の compile option で行う。
`kDetailedPerfLogToSerial=true` は scanline あたり約 22 回の `time_us_64()` を有効にし、
frame あたり約 0.4 ms から 0.5 ms の負荷が乗る。
必要なのは background tile 区間だけなので、既存の 22 回すべてを有効にする必要はない。

`NESCO_BG_TILE_SHARE_LOG` option を追加済みであり、有効時に次を行う。

- `NESCO_CORE1_BASELINE_LOG` を含意する (出力 gate のため)
- `g_perf_ppu_bg_tile_us` の加算だけを有効にする (scanline あたり `time_us_64()` 2 回)
- frame time の sample を保持する

```c
uint32_t g_perf_frame_us_samples[64];   /* 1 秒 = 最大約 60 frame */
uint32_t g_perf_frame_us_sample_count;
```

- 計測窓の終了時に sort し、中央値と 95 percentile を出す
- 次の 2 行を 1 秒ごとに出力する

```text
[BG_SHARE]    frames=N bg_tile_us=... bg_us=... sprite_us=...
              bg_tile_us_per_frame=... bg_tile_pct_x100=...
[FRAME_STATS] avg_us=... median_us=... p95_us=... max_us=...
```

**出力 frame が最大値を汚染しないようにする。**
シリアル出力は数百 µs 単位で frame を伸ばすため、次のどちらかを行う。

- 出力を行った直後の 1 frame を次の統計から除外する
- 統計を RAM に貯め、計測終了時にまとめて出力する

これを決めずに「最大値が悪化したら不採用」とすると、
log 出力による単発の遅延で判定が変わる。

### 必要な build は 2 つである

| build | 設定 | 取る値 |
|---|---|---|
| build 1 | `NESCO_CORE1_BASELINE_LOG=ON` のみ | 計測 A、計測 1、計測 2 |
| build 2 | `NESCO_BG_TILE_SHARE_LOG=ON` | 計測 3 |

実機作業は 1 回の session で両方を流せる。

### 順序

計測 3 を最初に置く。これが不発なら以降は不要になるためである。

1. **計測 3: `1.1.27` の background tile 実時間** (build 2、完了)
   - 主判定に使うのは **1 frame あたりの `bg_tile_us` 絶対値**である
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

### 判定に比率ではなく絶対値を使う理由

比率を使うと分母の選び方で結論が動く。

`kDetailedPerfLogToSerial` を全面的に有効にした場合、
計測負荷は draw 全体には乗るが `g_perf_ppu_bg_tile_us` の区間内には乗らない。
したがって比率は分母だけが膨らみ、**background の占有率が実態より低く出る**。
40% / 60% のような境界付近では判断が変わりうる。

専用 option で `bg_tile` 区間だけを測り、
1 frame あたりの絶対 µs で判定すれば、この偏りを受けない。

比率は参考値として `g_perf_frame_us_total` を分母に併記するが、判定には使わない。

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
散発的な処理を増やすため、95 percentile の確認が要る。

**最大値は診断値として記録するだけで、単独の不採用理由にはしない。**
最大値は UART log、SD access、入力処理、単発の割り込み、
計測開始直後の状態にも強く影響される。

- 主判定は平均と 95 percentile とする
- 最大値は記録し、悪化が複数の計測窓で再現した場合にだけ不採用理由に加える
- 各 ROM について同じ場面を複数の計測窓で取り、中央値どうしを比較する

### 判断基準

計測 3 の結果で分岐する。

理論値のうち tile 内側 loop の削減分は約 1.6 ms / frame である。
これが `bg_tile_us` に対して占める比率が現実的な範囲に収まるかで判断する。

| 1 frame あたりの `bg_tile_us` | 理論削減 1.6 ms の位置づけ | 判断 |
|---|---|---|
| 8 ms 以上 | 20% 以下。妥当な範囲 | 本案を実装する |
| 4 ms 以上 8 ms 未満 | 20% から 40%。やや強気 | 実装するが、段階 2 の実測で打ち切り判断を行う |
| 4 ms 未満 | 40% 超。理論モデルが成立していない | 本案は見送る |

`bg_tile_us` が 4 ms 未満の場合、削減の見込みが小さいだけでなく、
`renderBgTileFull()` の外 (fetch、loop 制御) が
区間の大半を占めていることを意味する。その場合は
`g_perf_ppu_bg_us` と `g_perf_ppu_sprite_us` を見て対象を選び直す。

計測 B が大きい場合 (frame 時間に対して無視できない場合) は、
本案とは独立に queue depth を増やす価値があるため、
本案を見送る場合でも queue item の byte 化だけを行う選択肢が残る。

## 実装方法

`AGENTS.md` の「大きな変更は build 可能な単位に分ける」に従い、
各段階が単独で build でき、単独で実機確認できる形に割る。

各段階で `platform/version.h` の `PICOCALC_NESCO_VERSION` を更新する。
実機未確認の段階を `main` へ push しない。

### 段階 0: 計測用コードの追加（完了、`1.1.27`）

- `NESCO_BG_TILE_SHARE_LOG` option、`g_perf_ppu_bg_tile_us` の加算、`[BG_SHARE]` 出力を追加済み
- frame time sample、中央値、95 percentile、`[FRAME_STATS]` 出力を追加済み
- 出力 frame を次の統計から除外し、最大値を汚染しない措置を実装済み
- `g_perf_draw_us` は削除済み

この段階は計測専用であり、通常 build の動作を変えない。

### 段階 1: palette snapshot 機構 (動作不変)

- version は **`1.1.28`** に更新する
- 前節「snapshot の実装契約（固定）」の API、状態、呼び出し箇所、handoff をそのまま実装する
- `infones/K6502_rw.h` の 2 分岐へ `mark_dirty()` を各 1 回追加する
- item に `palette[32]`、`palette_version`、`palette_valid` を追加する
- この段階では `pixels` と既存 packer は `WORD` のままとし、core1 ローカル palette を
  描画に使わない。protocol の受信・一致検査・ログだけを有効にする
- `NESCO_PALETTE_SNAPSHOT_LOG=ON` build を作り、通常 build とコード経路を混ぜない

この段階では色は従来どおり core0 が焼くため、表示は変わらない。

実機確認の順序は固定する。

1. `LodeRunner.nes` を開始し、安定状態の `[PALETTE_SNAPSHOT]` を 3 窓取る
2. emulator reset を 1 回実行し、直後の 3 窓を取る
3. stretch を 1 回切り替え、normal に戻し、それぞれ 3 窓を取る
4. menu へ戻って `Project_DART_V1.0.nes` を開始し、3 窓を取る

全窓で `protocol_faults=0`、各操作の直後の窓で `forced>=1`、見た目の回帰なしを確認する。
不合格なら段階 1 commit を `git revert` し、段階 2 へ進まない。

段階 1 だけで snapshot の寿命管理と並行動作を検証できる。

### 段階 2: index 描画への切り替え (効果測定ポイント)

- version は **`1.1.29`** に更新する
- `renderBgTileFull()` / `renderPacked4()` を index 書き込みへ変更する
- `BackgroundOpaqueLine` を廃止し、`compositeSpriteRange()` の opaque 判定を
  buffer 自身からの導出へ変更する
- sprite 合成を index 書き込みへ変更する
- 画面 off / clip 時の 5 clear を byte 幅の **black reserved index `0x20`** へ変更する
- `WorkLine` の型と `InfoNES_SetLineBuffer()` の契約を `BYTE*` へ変更する
- **queue item を `BYTE pixels[256]` へ変更する。depth は 4 のまま据え置く**
- core1 の packing に、ローカル palette による index から色への変換を追加する
- **worker を使わない fallback packing も index 入力へ変更する** (後述)

段階 1 の palette protocol は保持する。ここで初めて core1 packer が
`core1_palette[index]` を使う。protocol fault 時は前節の規約どおり zero palette で pack し、
line/strip を欠かさない。

queue item の byte 化を将来の queue depth 変更と分けるのは、
段階 2 の測定に queue traffic 削減が含まれるかどうかを曖昧にしないためである。
これにより段階 2 は「描画の削減 + queue traffic 削減」、
段階 3 は「純粋に queue depth の効果」に分かれ、
理論値 (tile 内側 1.54 ms + clear 0.15 ms + queue traffic 最大 0.25 ms = 約 1.9 ms) と
段階 2 の実測を直接比較できる。

### fallback 経路も index 化が必要である

`InfoNES_PostDrawLine()` には worker を使わない経路がある。

```c
void InfoNES_PostDrawLine(int scanline, bool frommenu) {
    const WORD *src = s_line_buffer;
    BYTE *strip = lcd_dma_acquire_buffer();

    if (!frommenu && display_lcd_worker_submit_line(scanline, src)) {
        return;
    }
    /* 以降は core0 が直接 pack する */
```

build 対象にある `InfoNES_PostDrawLine()` 呼び出しは `frommenu=false` だけである。
したがって実機で確認する fallback 契機は、worker が `RUNNING` でないとき、または表示 mode が
NES view でないときに `display_lcd_worker_submit_line()` が `false` を返す場合だけである。

現在の fallback packer (`display_pack_line_normal()` /
`display_pack_line_stretch_320()`) は `const WORD *src` を前提にしている。
index 化後はこの経路も index 入力へ変更しなければならない。

**fallback では palette version を持ち回す必要はない。**
`InfoNES_PostDrawLine()` は `InfoNES_DrawLine()` の直後に
同じ core0 上で呼ばれ、その間に CPU emulation は進まない。
したがって `PalTable` はその scanline を描画した時点のものであり、
現在値をそのまま使えばよい。

確認項目に「worker 非 RUNNING または NES view 外で fallback が NES index line を正しく pack
するか」を含める。

ここで計測 A と同条件・同 build 設定で frame time を取り、削減量を確認する。

#### 段階 2 の最小 core1 健全性確認

core1 の index lookup 増分は、1 px あたり多めに 4 cycle と見積もっても、normal では
8 source line x 256 px で約 33 µs、stretch では 10 LCD line x 320 px で約 51 µs である。
対応する DMA 時間は各 524 µs / 819 µs なので、ともに約 6% に収まる。Xevious stretch の
実測 27.3 ms/frame も 24.58 ms のバス下限より遅く、現時点で core1 packer を新たな律速と
みなす根拠はない。

したがって line ごとの `time_us_64()` 計測や別 build は追加しない。worker が RUNNING 中に
item を pop できなかった回数 `empty_polls` だけを core1 ローカルで数え、`FRAME_END` 時に
queue lock 下で共有 accumulator へ handoff する。`display_perf_take_window()` は既存 LCD
counter と同時にこの値と `palette_protocol_faults` を take-and-zero し、`[CORE1_BASE]` へ
`lcd_empty_polls=E palette_protocol_faults=F` として出す。0 を要求せず、連続する計測窓で増加していれば worker が
core0 を待てており、core1 過負荷の兆候ではないと判断する。

既存の core0 側 LCD 計測も同じ窓単位に直す。`display_perf_snapshot()` を
`display_perf_take_window()` に置換し、引数と戻り値の並びは維持したまま、値を copy した後に
`wait_us`、`flush_us`、`queue_wait_us`、`queue_wait_count`、frame pacing、`empty_polls`、
`palette_protocol_faults` の
全 accumulator を zero にする。`perf_log_if_due()` はこの API だけを呼ぶ。これにより
`[CORE1_BASE]` の `lcd_queue_wait_us/count`、`lcd_empty_polls`、`palette_protocol_faults` は
必ず直前の 1 秒窓の値となる。
`display_perf_reset()` は ROM/reset 境界での初期化専用として残す。

#### 段階 2 の A/B 比較と打ち切り

比較 build は `NESCO_CORE1_BASELINE_LOG=ON` **のみ**とする。段階 0 で取った
`1.1.27` baseline と同一条件で、`LodeRunner.nes`、`Project_DART_V1.0.nes`、
`Xevious.nes` の normal view をそれぞれ安定状態で 3 計測窓取る。
ROM ごとに 3 窓の `frame_us` 平均の中央値と p95 の中央値を用い、次をすべて満たせば採用する。

Xevious normal の baseline は 18.02 ms/frame で、LCD バス下限 15.73 ms までの余地は
2.29 ms しかない。core0 の削減が LCD 下限へ近づくほど frame time 改善は頭打ちになるため、
Xevious の結果はこの上限を踏まえて読む。ただし 3% 条件はこの余地より十分小さく、
本案の採否基準は変えない。

1. 各 ROM の `frame_us` 平均中央値が baseline より **3% 以上短い**
2. 各 ROM の p95 中央値が baseline より **1% 超悪化しない**
3. `protocol_faults=0`
4. sprite 優先度、左端 clip、palette 途中変更で目視回帰がない

最大値は UART 等で揺れる診断値であり、単独では採否に使わない。複数窓で再現する悪化は
原因調査の対象とする。条件 1--4 のいずれかを満たさなければ、結果を
`docs/project/Picocalc_NESco_HISTORY.md` と `docs/project/TASKS.md` に記録してから、
`git revert <段階2のcommit>`、続けて `git revert <段階1のcommit>` を実行する。
段階 3 へは進まない。

normal 3 ROM と Xevious stretch の同じ baseline-log build で `lcd_empty_polls` を記録する。
これ以外の core1 専用 build と時刻計測は作らない。

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

### 段階 3: queue depth の変更（条件付き、未計画）

段階 2 採用後に、修正済みの窓単位 `lcd_queue_wait_us/count` で depth 4 基準を 3 ROM normal
から取り直す。queue full 待ちの平均中央値が frame time の 1% 未満なら、この段階は実装しない。
version も予約しない。

1% 以上なら、そこで初めて別 commit・次の patch version で次を実装する。

- `DISPLAY_LCD_WORKER_QUEUE_DEPTH` を 4 から **6** へ変更する
  （段階 2 完了時から +704 byte、変更前全体比では `.bss -432 byte`）
- `.bss` の増減と p95 を depth 4 と同条件で比較する

段階 2 で queue item は既に byte 化されているため、
この段階の変更は depth のみである。したがって測定結果は
queue を深くしたことの効果だけを表す。

段階 2 の `display_perf_take_window()` で `lcd_queue_wait_us/count` を 1 秒窓ごとに
take-and-zero するよう修正してから、同条件の depth 4 基準を 3 ROM normal で取り直す。
その後 depth 6 を同じ手順で比較する。既存ログの累積値はこの判断に使わない。
queue depth を 8 にするのは、depth 6 でも queue wait の平均中央値が frame_us の 1% 以上で、
かつ p95 が改善する場合だけとする。
併せて `lcd_empty_polls` を記録し、queue を深くしたことで worker の挙動が変わっていないことを確認する。

### 段階 4 以降 (任意、別課題)

- LCD COLMOD 12 bit/pixel 化
  (`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md` の候補 1)
- `InfoNES_SetLineBuffer()` へ queue slot を直接渡して copy を廃止する
- 4 px 1 word store 化

段階 4 以降は、段階 3 までの実測を見てから個別に判断する。

## 未確認事項

- `1.1.27` における background tile の draw 占有率（採否には使わない参考値）
- `g_perf_ppu_bg_tile_us` の区間のうち `renderBgTileFull()` が占める割合
  (本案が削るのはこの内側だけなので、占有率がそのまま削減率にはならない)
- 段階 2 の後、sprite 合成が core0 に残る形で問題がないか
  (index 化により sprite 合成も byte 操作になるため軽くなる見込みだが未計測)
- core1 が空 queue を継続して観測するか（`lcd_empty_polls` で確認する）
- palette snapshot の発生頻度が実 ROM でどの程度か
- 1 frame 内で連続する scanline に対して、
  queue depth を超える回数の palette 変更が起きる ROM があるか

## この文書の改訂

### 第 4 版から第 5 版へ (`perf/bg-tile-share-log`)

実装前レビューを反映した。

- **RGB565 black 専用の予約 index `0x20` を追加した。**
  screen off と 4 種の clip clear が palette index 0 ではなく `0x20` を書き、
  core1/fallback が black を出す契約にした。これにより backdrop 色への回帰を防ぐ
- **palette 規約を LINE item 限定にした。** `FRAME_END` の zero 初期化 palette field は
  検査しないため、正常な frame marker が protocol fault を発生させない
- **`K6502_rw.h` を読む 2 翻訳単位を明記した。** `K6502.cpp` と `InfoNES_pAPU.cpp` の両方で
  `display.h` を先に include するため、dirty API の未宣言参照を作らない
- **全 item の zero 初期化を禁止し、明示代入にした。** clean line の余計な 352 byte memset を
  避け、queue line traffic の比較を 1,052 byte から 608 byte に訂正した
- **理論値を最大約 1.9 ms/frame に更新した。** `DrawLine()` 実行の 232 line を使い、
  部分 tile がこのモデル外であることを明記した
- **core1 計測を `lcd_empty_polls` のみへ縮小した。** 机上で pack 増分は strip DMA の約 6% と
  確認できるため、line 単位時刻計測用 build は追加しない
- **queue depth は条件付きに戻した。** 窓単位 queue wait が frame time の 1% 以上のときだけ
  depth 6 を別 commit として計画し、version を先取りしない
- **段階 0 の完了状態と fallback 実機確認条件を更新した。** `g_perf_draw_us` は削除済みであり、
  build 対象に `frommenu=true` 呼び出しはない
- **64 entry core1 LUT を含めて RAM 計算を更新した。**

### 第 3 版から第 4 版へ (`perf/bg-tile-share-log`)

実機計測の完了後、実装時に判断を残さないために次を固定した。

- **段階 0 の実測結果と実装開始判断を冒頭へ反映した。**
  normal 3 ROM と Xevious stretch の `bg_tile_us_per_frame` は 6.6--6.9 ms であり、
  4--8 ms の実装帯に入ったため段階 1 へ進む
- **palette snapshot の所有者、2 API、version の意味、core1 受信規則を定義した。**
  特に reset 時に core1 の共有 bool を書き換えず、version 0 の強制 snapshot を queue item で
  渡す方式へ明確化した。protocol fault 時も line を落とさず zero palette で pack する
- **強制 snapshot の配置を実コードの 3 箇所へ限定した。**
  `InfoNES_Reset()`、`display_lcd_worker_prepare_nes_view()`、
  `display_lcd_worker_stop_and_drain()` であり、ROM load は Reset 経由なので追加呼び出し不要である
- **段階 1 と段階 2 のログ、build option、実機確認順、採否を数値で固定した。**
  段階 2 の採用条件は normal 3 ROM の平均 `frame_us` 中央値が各 3% 以上改善、
  p95 中央値が各 1% 超悪化なし、protocol fault 0 とした
- **core1 計測の区間と handoff を定義し、比較 build と分離した。**
  `NESCO_BG_INDEX_METRICS` は健全性確認専用であり、line ごとの計測負荷を含むため A/B 比較には使わない
- **`display_perf_snapshot()` を窓単位の `display_perf_take_window()` へ置換することを決めた。**
  既存の累積 `lcd_queue_wait_*` は depth 判断に使わず、段階 3 の直前に depth 4 基準を取り直す
- **各段階の version を 1.1.28、1.1.29、1.1.30 と固定した。**

### 第 2 版 (`f02db9e`) から第 3 版へ

外部レビュー第 2 回の指摘による。

- **外部 palette ring 方式に競合があったため、snapshot を queue item 内へ戻した。**
  `display_lcd_worker_pop_item()` は item を copy 後 count を減らして unlock し、
  packing は lock の外で行う。pop 済みで packing 中の 1 item が
  ring の生存数計算から抜けており、ring 段数を queue depth と同数にしても防げない。
  item 内方式は pop が item 全体を lock 下で copy するため構造上安全で、
  RAM も depth 6 で 40 byte しか違わない。
  外部 ring を採る場合の必須条件も併記した
- **`g_perf_draw_us` は宣言のみで加算も参照もされていないことが分かった。**
  第 2 版で判定式に採用した `g_perf_ppu_bg_tile_us / g_perf_draw_us` は
  分母が常に 0 になる。判定を 1 frame あたりの `bg_tile_us` 絶対値へ変更した。
  これは比率の分母が計測負荷で膨らむ偏りも同時に回避する
- **計測用のコード追加を段階 0 として明記した。**
  出力 gate は `NESCO_CORE1_BASELINE_LOG` が必要で、
  `kDetailedPerfLogToSerial` だけでは出力されない。
  `g_perf_ppu_bg_tile_us` の出力処理は存在せず、
  中央値と 95 percentile の材料 (sample 列) も存在しない。
  専用 option `NESCO_BG_TILE_SHARE_LOG` を追加し、
  出力 frame が最大値を汚染しない措置も入れる
- **段階 1 の記述を、本文で決めた方式と一致させた**
- **queue item の byte 化を段階 3 から段階 2 へ移した。**
  段階 2 の測定に queue copy 半減が含まれるかを曖昧にしないため。
  段階 3 は depth 変更のみになった
- **fallback packing 経路の index 化を段階 2 に追加した。**
  `InfoNES_PostDrawLine()` の worker 非使用経路は `const WORD *src` 前提である。
  なお この経路は `InfoNES_DrawLine()` の直後に同じ core0 で走るため、
  palette version を持ち回る必要はなく現在の `PalTable` を使えばよい
- **最大値を単独の不採用理由から外した。** 主判定は平均と 95 percentile とし、
  最大値は複数の計測窓で再現した場合にだけ理由に加える

### 初版 (`554a630`) から第 2 版へ

外部レビュー第 1 回の指摘による。

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
