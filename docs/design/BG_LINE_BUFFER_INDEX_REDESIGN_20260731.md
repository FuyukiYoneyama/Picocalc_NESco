# BG line buffer palette index redesign

作成日: 2026-07-31

対象 branch: `perf/bg-tile-share-log`

比較基準 version: `1.1.27`

実装起点 version: 段階 1 合格済みの `1.1.28`

段階 2 A/B baseline: 同一条件で新規計測する `1.1.28`

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
後述の固定した A/B 比較基準で決める。段階 0 時点の `lcd_queue_wait_*` は
計測窓ごとにリセットされない累積値だったため、queue depth の判断には使わない。
段階 1 で `display_perf_take_window()` による窓単位値へ修正済みであり、段階 2 の
無操作 30 窓を depth 4 の基準にも使う。

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

### producer が出す index の固定範囲

scanline producer が書いてよい値は `0x00..0x1f` と black 予約値 `0x20` だけとする。

- background: `palette_base` (`0x00`, `0x04`, `0x08`, `0x0c`) と 2 bit pixel index の OR
- sprite: `0x10 | (v & 0x0f)`
- screen off / clip / 非描画 line: `0x20`

`WorkLine[256]` は `InfoNES_PostDrawLine()` までに必ず全画素をこの範囲で初期化する。
core1 hot path は 256 entry LUT を `src[x]` で直接引き、画素ごとの mask / 範囲 branch は
追加しない。producer 契約から外れた byte が混入しても LUT 外を読まないよう、
`0x21..0xff` はすべて black とする。producer がこの範囲を生成してよいという意味ではない。

### core1 側の変換

core1 は 256 entry のローカル LUT を使う。`0x00..0x1f` は snapshot からコピーした
`PalTable`、`0x20..0xff` は RGB565 black (`0x0000`) に固定する。snapshot に載せるのは
従来どおり前半 32 entry (64 byte) だけである。LUT を 64 entry に縮めず、producer の
取りこぼしが RP2040 上で隣接 `.bss` の色化けとして現れる余地を構造的に除く。

worker を使わない fallback では現在の `PalTable[32]` から stack 上に一時 256 entry LUT を作り、
後半 224 entry を `0x0000` にして worker と同じ packer を呼ぶ。palette version は持ち回らない。

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
| `InfoNES_HSync()` の上下 4 line | `InfoNES_DrawLine()` を呼ばない black line | `0x20` |
| `InfoNES_DrawLine()` の `!R1_SHOW_SCR` | screen off | `0x20` |
| background 左端 8 px clip | clip black | `0x20` |
| 上下 clip | clip black | `0x20` |
| sprite 左端 8 px clip | clip black | `0x20` |

これらで index `0` を書いてはならない。`PalTable[0]` は backdrop 色であり、black とは
限らないためである。`0x20 & 3 == 0` なので、この予約 index は background opaque 判定では
透明として扱われ、現在の clear と同じ sprite 合成結果になる。

## 削減されるもの

| 項目 | `1.1.27` | 段階 1 (`1.1.28`) | 段階 2 (`1.1.29`) |
|---|---|---|---|
| `pal[]` からの load | 8 / tile | 8 / tile | 0 |
| opaque LUT からの load | 2 / tile | 2 / tile | 0 |
| 画素 store 幅 | halfword x 8 | halfword x 8 | byte x 8 |
| `dst_opaque` への store | 8 / tile | 8 / tile | 0 |
| `BackgroundOpaqueLine` の clear | 256 byte / scanline | 256 byte / scanline | 0 |
| queue line traffic (snapshot なし) | 512 + 540 = 1,052 byte | 512 + 608 = 1,120 byte | 256 + 352 = 608 byte |
| queue line traffic (snapshot あり) | 対象外 | 512 + 64 + 608 = 1,184 byte | 256 + 64 + 352 = 672 byte |
| 非表示 line の clear | 512 byte | 512 byte | 256 byte |

上表の queue traffic は、line buffer から item への pixel copy、snapshot がある場合の
`PalTable` copy、queue slot への item copy を合計した論理 byte 数である。段階 2 の直接の親は
段階 1 なので、clean line は `1,120 -> 608 byte`、snapshot line は `1,184 -> 672 byte` となる。

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
| queue line traffic の削減 | 444 byte / line。cycle は compiler / copy 展開依存のため独立には見積もらない |
| 小計 | **約 1.69 ms / frame + queue traffic 削減分** |

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

`1.1.27` の `display_lcd_worker_item_t` は 540 byte だった。

```text
type 4 + scanline 4 + viewport_x/y/w/h 16 + scale_mode 4 + pixels 512 = 540
540 x 4 slot = 2,160 byte
```

段階 1 (`1.1.28`) では snapshot field を追加したため、現在の item は 608 byte、
queue 4 slot と 64 entry core1 LUT の合計は `608 x 4 + 128 = 2,560 byte` である。

段階 2 で画素を byte 化すると画素部は 256 byteになり、item は 352 byteになる。
同時に LUT を安全な 256 entry へ拡張する。depth 4 の queue と core1 LUT の合計は
`352 x 4 + 512 = 1,920 byte` で、段階 1 から `-640 byte` となる。

| 構成 | 計算 | `1.1.27` queue 2,160 byte との差 | 段階 1 の 2,560 byte との差 |
|---|---|---|---|
| depth 4 + item 内 snapshot + 256 entry core1 LUT | 352 x 4 + 512 = 1,920 | **-240** | **-640** |
| depth 6 + item 内 snapshot + 256 entry core1 LUT | 352 x 6 + 512 = 2,624 | **+464** | **+64** |
| depth 8 + item 内 snapshot + 256 entry core1 LUT | 352 x 8 + 512 = 3,328 | **+1,168** | **+768** |
| (参考) depth 6 + version/valid 付き item + 外部 ring 6 段 + core1 LUT | 288 x 6 + 384 + 512 = 2,624 | +464 | +64 |

外部 ring 方式でも item 側に version/valid が必要なため、depth 6 の RAM は item 内方式と同じである。
**RAM 上の利点がないのに競合の危険を抱える理由がない**ため、item 内方式を採る。

depth 6 は条件付きの将来段階とする。queue 関連だけなら `1.1.27` 比 +464 byte だが、段階 2 で
`s_line_buffer` が -256 byte、`BackgroundOpaqueLine` が -256 byte、
`g_bg_tile_pair_opaque4` が -256 byte になるため、列挙した主要 static 領域の小計は
`1.1.27` 比 -304 byte、段階 1 比 -704 byte である。depth 4 基準で窓単位の queue wait を取り直し、
frame time に対して無視できないと確認できた場合だけ depth 6 を実装する。
depth 8 を検討するのは、depth 6 後も前述の窓ごとの queue wait 比率中央値が 1 ROM でも 1% 以上で、
depth 6 の `p95_us` 中央値が depth 4 より改善する場合だけである。そのとき queue 関連
`+1,168 byte` を許容する。

`.bss` には余裕がある (`1.0.15` 時点で静的領域末尾から heap limit まで 122,328 byte)
ため depth 8 の queue 関連 `+1,168 byte`（段階 2 の 3 配列削減込みでは `1.1.27` 比 `+400 byte`、
段階 1 比 `0 byte`）自体は問題にならないが、
「RAM 増なし」と書くのは誤りなので、増分を明示して判断する。

段階 2 depth 4 で実際に変わる `.bss` は、queue `-1,024`、core1 LUT `+384`、
`s_line_buffer -256`、`BackgroundOpaqueLine -256`、`g_bg_tile_pair_opaque4 -256` の
合計 `-1,408 byte` である。段階 1 通常 build の `bss=98952` から、段階 2 通常 build の
期待値を `97544` と固定する。
新しい mutable static buffer は追加しない。実測が一致しなければ map / `nm --size-sort` で差を説明してから
実機へ進む。`black_lut[256]` は `static const` とし `.bss` へ置かない。

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
item 全体の memset を入れない。

queue への構造体 copy は invalid item の `palette[32]` も運ぶが、これは保存先で読まれない
payload である。GCC が `-Wmaybe-uninitialized` を出しても、全 item の memset を復活させない。
必要なら「`palette_valid==1` のときだけ palette を copy する」push/pop へ局所化して解消する。

`1.1.27` の clean line traffic は 1,052 byte、段階 1 実装後は 1,120 byte、
段階 2 完了後は 608 byte である。snapshot line は段階 1 が 1,184 byte、段階 2 が
672 byte となる。内訳は前節「削減されるもの」の表を正本とする。

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
frame-end ごとに handoff された値である。段階 1 で `display_perf_snapshot()` を
`display_perf_take_window()` へ置換し、これを snapshot log と通常の `[CORE1_BASE]` の
唯一の受け渡し経路にする。別の palette 専用 handoff は作らない。

core1 は `FRAME_END` 時だけ、`palette_applied`、`palette_protocol_faults`、`empty_polls` を
既存 queue lock 下で共有 accumulator へ加算する。core0 の `display_perf_take_window()` は、
**core1 から handoff されたこの 3 field だけ**を同じ queue lock 下で copy-and-zero する。
core0 専用の `wait_us`、`flush_us`、`queue_wait_*`、frame pacing は lock を取らずに
copy-and-zero する。line ごとの lock や 64 bit の無保護な cross-core read は行わない。

`perf_log_if_due()` は window を 1 回だけ取得し、通常の `[CORE1_BASE]` と、
`NESCO_PALETTE_SNAPSHOT_LOG` 時の `[PALETTE_SNAPSHOT]` が同じ window 値を出す。

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
案 A の tile/opaque 小計 約 1.69 ms に対して 1 桁小さい。

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

実装状態 (2026-07-31): **完了・実機合格**。
`/home/fuyuki/pico_dvl/codex/log/pico20260731_203816.log` の 202 窓すべてで
`protocol_faults=0`、snapshot/applied 合計 `1739/1739`、開始・reset・表示切替の
各境界で forced snapshot を確認した。実機の表示回帰もなかったため段階 2 へ進む。

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
- index 描画は通常経路へ直接入れ、`NESCO_BG_INDEX` のような feature toggle は追加しない。
  A/B は source 変更前に新規取得する `1.1.28` baseline と段階 2 commit を、後述の無操作 30 窓で
  比較する。戻す場合は commit 単位で revert する
- source 変更前に `build-bg-index-baseline` を作り、`1.1.28` の計測 artifact を保存する
- `display_perf_reset()` を `InfoNES_Init()` / `InfoNES_Reset()` から呼ぶ。`perf_reset()` 内には入れない
- `renderBgTileFull()` / `renderPacked4()` を index 書き込みへ変更する
- `BackgroundOpaqueLine` を廃止し、`compositeSpriteRange()` の opaque 判定を
  buffer 自身からの導出へ変更する
- sprite 合成を index 書き込みへ変更する
- 画面 off / clip 時の 5 clear を byte 幅の **black reserved index `0x20`** へ変更する
- `WorkLine` の型と `InfoNES_SetLineBuffer()` の契約を `BYTE*` へ変更する
- `InfoNES_HSync()` の上下 4 line clear を `InfoNES_MemorySet(WorkLine, 0x20, NES_DISP_WIDTH)`
  へ変え、直上の「`NES_DISP_WIDTH << 1 = 512`」という byte 数コメントを削除または 256 byte に更新する
- **queue item を `BYTE pixels[256]` へ変更する。depth は 4 のまま据え置く**
- core1 の packing に、ローカル palette による index から色への変換を追加する
- **worker を使わない fallback packing も index 入力へ変更する** (後述)

段階 1 の palette protocol は保持する。ここで初めて core1 packer が
`core1_palette[index]` を使う。protocol fault 時は前節の規約どおり zero palette で pack し、
line/strip を欠かさない。

#### InfoNES 側の書き換え契約（固定）

型と API は次へ変更する。`size` は byte 数ではなく従来どおり pixel 数であり、256 を渡す。

```cpp
BYTE *WorkLine = nullptr;
void InfoNES_SetLineBuffer(BYTE *p, WORD size);
```

`InfoNES.h` の宣言、`InfoNES.cpp` の定義、`platform/display.c` の `s_line_buffer` を同時に変え、
途中で `reinterpret_cast` や `void*` を挟まない。
`platform/display.c` / `display.h` 冒頭の integration comment も RGB565 line ではなく
palette index line を渡す説明へ更新する。

background renderer は次の形に統一する。

```cpp
struct BgTileDescriptor {
  const BYTE *pattern_row;
  BYTE palette_base;       // 0x00, 0x04, 0x08, 0x0c
  BYTE *dst;
  BYTE clip_left;
  BYTE clip_right;
};
```

- `resolveBgPal()` は `resolveBgPaletteBase()` へ変え、attribute の 2 bit 値を 2 bit 左へ
  shift した `BYTE` を返す。`PalTable` pointer は返さない
- `buildBgTile()` / `emitBgTile()` / `renderBgTile()` / `renderBgTileFull()` /
  `renderPacked4()` の `WORD *pal`、`WORD *dst`、`BYTE *dst_opaque`、opaque mask 引数を削除する
- full tile と partial tile はどちらも `palette_base | two_bit_index` を `BYTE *dst` へ書く
- `pPoint` と clip 用 `pPointTop` はすべて `BYTE*` にする。`pOpaquePoint` とその加算は削除する

opaque 専用物は次をすべて同じ commit で削除する。

- `BackgroundOpaqueLine[256]` の定義と全 clear / pointer / 引数
- `g_bg_tile_pair_opaque4[256]` の定義、`initBgTileRenderLut()` 内の `opaque` 構築と代入、
  renderer 内の `opaque_hi` / `opaque_lo`
- 未使用の旧 `compositeSprite()` と、active call の下に残る `#else` の旧 RGB565 合成経路

active sprite 合成は次の 1 signature だけを残す。

```cpp
void compositeSpriteRange(const BYTE *spr, BYTE *buf, int begin, int end);
```

各 pixel は、`v != 0` かつ `((v & 0x80) != 0 || (buf[i] & 3) == 0)` のときだけ
`buf[i] = (BYTE)(0x10 | (v & 0x0f))` とする。priority 判定で読む `buf[i]` は同じ pixel へ
sprite index を書く前の background/black index である。

5 箇所の clear は前節の表どおりすべて `InfoNES_MemorySet(..., 0x20, pixel_count)` とする。
`InfoNES_HSync()` の上下 line は 256 byte、screen off と上下 clip は 256 byte、
background / sprite の左端 clip は各 8 byte である。`NES_DISP_WIDTH << 1` と `8 << 1` は残さない。

#### display / packer 側の書き換え契約（固定）

producer と queue の型は次へ変更し、depth は 4 のままにする。

```c
static BYTE s_line_buffer[256];
BYTE pixels[256];                    /* display_lcd_worker_item_t */
static bool display_lcd_worker_submit_line(int scanline, const BYTE *src);
```

LINE item は段階 1 と同じく field を明示初期化し、item 全体の memset はしない。
変更後に `sizeof(display_lcd_worker_item_t) == 352` を compile-time assertion で固定する。
`FRAME_END` の zero 初期化はそのままでよい。

palette と packer は次の共通契約にする。

```c
static void display_build_palette_lut256(WORD dst[256], const WORD src[32]);
static void display_pack_line_normal(BYTE *dst, const BYTE *src, const WORD lut[256]);
static void display_pack_line_stretch_320(BYTE *dst, const BYTE *src, const WORD lut[256]);
```

`display_build_palette_lut256()` は前半 32 entry を copy し、後半 224 entry を `0x0000` にする。
core1 の snapshot apply と fallback の一時 LUT 作成は必ずこの helper を使う。
normal / stretch と worker / fallback で別の index-to-color 規則を実装しない。

- normal packer は `WORD px = lut[src[x]]` を取り、従来どおり high byte、low byte の順で LCD
  DMA buffer へ書く
- stretch packer は source 4 pixel を LUT 変換した後、従来どおり 5 pixel
  (`s0,s1,s2,s3,s3`) へ展開する
- `display_lcd_worker_pack_stretch_line()` も `const BYTE *src` と `const WORD *lut` を受ける
- core1 の正常 LINE は `s_lcd_worker_core1_palette` を packer へ渡す
- fallback は submit 失敗後に stack 上の `WORD fallback_lut[256]` を現在の `PalTable` から作り、
  worker と同じ normal / stretch packer を呼ぶ。fallback 専用の色変換 loop は作らない

protocol fault 用に全 entry 0 の `static const WORD black_lut[256]` を持つ。LINE 受信時に
palette snapshot を適用できた、または clean item の version が一致した場合だけ core1 LUT を選び、
それ以外は fault を加算して `black_lut` を packer へ渡す。**段階 1 で入れた
`memset(item.pixels, 0, sizeof(item.pixels))` は削除する。** index 0 は backdrop なので、これを
残して黒 line の代用にしてはならない。fault 時も元の index line と strip 行数は保持する。

#### source / RAM の静的合格条件

実装後、次の検索は 0 件でなければならない。

```sh
rg -n "BackgroundOpaqueLine|g_bg_tile_pair_opaque4|WORD \*WorkLine|InfoNES_SetLineBuffer\(WORD|static WORD s_line_buffer|WORD pixels\[256\]|const WORD \*src" \
  infones/InfoNES.cpp infones/InfoNES.h platform/display.c platform/display.h
```

加えて次を確認する。

- compile-time assertion が item `352 byte` を保証する
- `arm-none-eabi-nm -S --size-sort` で `s_lcd_worker_queue` が `0x580` (1,408 byte)、
  `s_line_buffer` が `0x100` (256 byte)、core1 palette LUT が `0x200` (512 byte) である
- `black_lut[256]` は `0x200` (512 byte) だが read-only section にあり `.bss` を消費しない
- 通常 build の `.bss` が期待値 `97544` である。異なる場合は実機前に全差分を説明する
- LINE item の full memset がなく、`FRAME_END` の full memset だけが残る
- normal / stretch の worker と fallback が共通 packer を呼び、index-to-color loop が重複していない
- `InfoNES.cpp` の `display_perf_reset()` 呼び出しは `InfoNES_Init()` / `InfoNES_Reset()` の 2 箇所だけで、
  `perf_reset()` の関数本体には存在しない

queue item の byte 化を将来の queue depth 変更と分けるのは、
段階 2 の測定に queue traffic 削減が含まれるかどうかを曖昧にしないためである。
これにより段階 2 は「描画の削減 + queue traffic 削減」、
段階 3 候補は「polling / queue depthだけの効果」に分かれ、
理論小計 (tile 内側 1.54 ms + clear 0.15 ms = 約 1.69 ms) と、独立には cycle 化しない
queue traffic 削減分を含む段階 2 の実測を比較できる。
実測ではnormalが60 fps上限へ到達したため、この候補は実装しなかった。

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
index 化後は前節の固定契約どおり、現在の `PalTable` から一時 LUT を作り、worker と共通の
`const BYTE *src` packer を呼ぶ。

**fallback では palette version を持ち回す必要はない。**
`InfoNES_PostDrawLine()` は `InfoNES_DrawLine()` の直後に
同じ core0 上で呼ばれ、その間に CPU emulation は進まない。
したがって `PalTable` はその scanline を描画した時点のものであり、
現在値をそのまま使えばよい。

build 対象に fallback を強制する呼び出しはないため、この段階では専用の force option を追加しない。
fallback が worker と共通 packerを使用し、一時 LUT の前半/後半が copy/zero されることを
source review と build で確認する。将来 fallback の実行契機を追加した場合は実機項目へ昇格する。

ここで計測 A と同条件・同 build 設定で frame time を取り、削減量を確認する。

#### 段階 2 の最小 core1 健全性確認

core1 の index lookup 増分は、1 px あたり多めに 4 cycle と見積もっても、normal では
8 source line x 256 px で約 33 µs、stretch では 10 LCD line x 320 px で約 51 µs である。
対応する DMA 時間は各 524 µs / 819 µs なので、ともに約 6% に収まる。Xevious stretch の
実測 27.3 ms/frame も 24.58 ms のバス下限より遅く、現時点で core1 packer を新たな律速と
みなす根拠はない。

したがって line ごとの `time_us_64()` 計測や別 build は追加しない。worker が RUNNING 中に
item を pop できなかった回数 `empty_polls` だけを core1 ローカルで数え、`FRAME_END` 時に
queue lock 下で共有 accumulator へ handoff する。これは段階 1 の
`display_perf_take_window()` が同じ lock 下で take-and-zero し、`[CORE1_BASE]` へ
`lcd_empty_polls=E palette_protocol_faults=F` として出す。

`core1_worker` は空 queue を引くと外側 loop を抜けて `sleep_us(50)` するため、
`empty_polls` の絶対値は core1 の余裕量ではない。各 1 秒窓で十分大きい
（idle 時は概ね 2 万回程度）なら worker が core0 を待てており、ほぼ 0 なら core1 側が
継続して仕事を持つ状態として調査する。窓間の単純な増減は判定に使わない。

`wait_us` と `flush_us` は fallback packer 専用の legacy counter で、worker 稼働中は 0 のまま
である。`display_perf_take_window()` は窓の整合のためこれらも core0 lock なしで reset するが、
worker の性能指標や LCD 帯域分析には使わない。

`display_perf_reset()` は段階 2 で `InfoNES_Init()` と `InfoNES_Reset()` から明示的に呼び、
ROM/reset 境界で core0 counter と core1 の published accumulator を初期化する。
毎秒の窓終了にも使う `perf_reset()` の中へは入れない。そこへ入れると
`display_perf_take_window()` の unlock 後に core1 が publish した次窓分を消す競合が生じるためである。
core1 の frame 内 local accumulator は core0 から触らないため、遷移直後の最初の窓には最大 1 frame
未満の持ち越しがあり得る。後述の A/B 手順はこの最初の窓を必ず捨てる。

#### 段階 2 の A/B 比較と打ち切り

比較 build は段階 1 `1.1.28` と段階 2 `1.1.29` の双方で
`NESCO_CORE1_BASELINE_LOG=ON` **のみ**とし、
`NESCO_PALETTE_SNAPSHOT_LOG=OFF`、`NESCO_BG_TILE_SHARE_LOG=OFF` とする。build directory は
段階 1 baseline を `build-bg-index-baseline`、段階 2 を `build-bg-index` に固定し、host compiler build と
区別する。段階 2 の source 変更前に baseline artifact と map/size 出力を保存する。configure は
directory 名だけを切り替えて次を使う。

各 build directory が既に存在する場合は、configure 前に `CMakeCache.txt` の
`CMAKE_C_COMPILER=/usr/bin/arm-none-eabi-gcc`、
`CMAKE_CXX_COMPILER=/usr/bin/arm-none-eabi-g++`、
`PICO_SDK_PATH=/home/fuyuki/pico/pico-sdk` を確認する。1 つでも異なる cache は再利用せず、
削除対象を該当する build directory だけに限定して作り直す。

```sh
NESCO_BUILD_DIR=build-bg-index-baseline # 段階 2 では build-bg-index に置き換える
cmake -S . -B "$NESCO_BUILD_DIR" \
  -DPICO_SDK_PATH=/home/fuyuki/pico/pico-sdk \
  -DCMAKE_C_COMPILER=/usr/bin/arm-none-eabi-gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/arm-none-eabi-g++ \
  -DCMAKE_ASM_COMPILER=/usr/bin/arm-none-eabi-gcc \
  -DNESCO_CORE1_BASELINE_LOG=ON \
  -DNESCO_PALETTE_SNAPSHOT_LOG=OFF \
  -DNESCO_BG_TILE_SHARE_LOG=OFF
cmake --build "$NESCO_BUILD_DIR" --clean-first -j4
```

段階 1 baseline は banner `1.1.28`、段階 2 計測版と通常版 `build/` は banner `1.1.29`、
すべての ELF が ARM EABI5 であること、`git diff --check` が通ることを確認する。
`build-bg-index-baseline/Picocalc_NESco.uf2` と `build-bg-index/Picocalc_NESco.uf2` を順に実機へ渡し、
`LodeRunner.nes`、`Project_DART_V1.0.nes`、`Xevious.nes` の normal view を双方で次の固定手順で測る。

1. ROM を開始して normal view のまま操作しない
2. `ROM_START` 後、最初の `[CORE1_BASE]` / `[FRAME_STATS]` 対を遷移窓として捨てる
3. 続く **30 個の連続した対**を採用する。30 窓すべて `input_events=0` とする。
   途中で input event、reset、表示 mode 変更、UART 欠落が
   あった場合は、その ROM の計測を最初から取り直す
4. ROM ごとに、30 窓の `[CORE1_BASE] frame_us_avg` の中央値と
   `[FRAME_STATS] p95_us` の中央値を比較値にする

段階 1 baseline は `pico20260731_212613.log` で取得済みである。3 ROM とも最初の 1 窓を除く
30 対が normal、`input_events=0`、`palette_protocol_faults=0`、ログ欠落なしだった。

| ROM | `frame_us_avg` 中央値 | 段階 2 合格上限 (-3%) | `p95_us` 中央値 | 段階 2 許容上限 (+1%) | queue wait 比率中央値 | `lcd_empty_polls` 中央値 |
|---|---:|---:|---:|---:|---:|---:|
| LodeRunner | 19,114.0 | 18,540.58 | 26,817.5 | 27,085.67 | 0.0000% | 23,320.5 |
| Project_DART | 20,183.0 | 19,577.51 | 25,584.0 | 25,839.84 | 0.0211% | 23,214.0 |
| Xevious | 16,909.5 | 16,402.22 | 17,346.0 | 17,519.46 | 8.9331% | 13,153.5 |

段階 2 `1.1.29` は `pico20260731_214523.log` で同じ30窓を取得し、実機合格した。

| ROM | `frame_us_avg` 中央値 | baseline 比 | `p95_us` 中央値 | baseline 比 | queue wait 比率中央値 | `lcd_empty_polls` 中央値 |
|---|---:|---:|---:|---:|---:|---:|
| LodeRunner | 16,597.0 | -13.17% | 16,668.0 | -37.85% | 19.8120% | 7,381.5 |
| Project_DART | 16,590.5 | -17.80% | 16,668.0 | -34.85% | 15.6850% | 8,140.5 |
| Xevious | 16,596.0 | -1.85% | 16,668.0 | -3.91% | 34.2722% | 6,017.5 |

全90採用窓と、追加のnormal/stretchプレイを含むログ全412窓で
`palette_protocol_faults=0` だった。実機プレイでも色、左端clip、sprite優先度、normal/stretchに
問題は見られなかった。

窓や plateau を目視で選ばない。attract demo の場面によって LodeRunner の frame time が
約 7% 移動し、3 窓では採用閾値 3% より位相差が大きいためである。既存 `1.1.27` / `1.1.28`
ログには 30 窓中の操作入力が多数あり、この新規約の baseline には流用しない。
次をすべて満たせば採用する。

新しい無操作 Xevious normal baseline は 16.91 ms/frame で、60 fps frame pacing の
16.667 ms に対して3%短縮値は16.402 msとなり、実現不能な条件だった。baseline更新時に
旧18.02 ms用の条件を補正しなかった設計上の誤りなので、上限到達条件を次へ明記する。

1. 各 ROM の `[CORE1_BASE] frame_us_avg` 中央値が baseline より **3% 以上短い**、または
   `frame_us_avg` 中央値と `[FRAME_STATS] p95_us` 中央値がともに **16,700 us以下**で
   60 fps frame pacing 上限へ到達している
2. 各 ROM の `[FRAME_STATS] p95_us` 中央値が baseline より **1% 超悪化しない**
3. 全 90 窓で `[CORE1_BASE] palette_protocol_faults=0`
4. sprite 優先度、左端 clip、palette 途中変更で目視回帰がない

最大値は UART 等で揺れる診断値であり、単独では採否に使わない。複数窓で再現する悪化は
原因調査の対象とする。条件 1--4 のいずれかを満たさなければ、結果を
`docs/project/Picocalc_NESco_HISTORY.md` と `docs/project/TASKS.md` に記録してから、
`git revert <段階2のcommit>`、続けて `git revert <段階1のcommit>` を実行する。
段階 3 へは進まない。

同じ 30 窓で normal 3 ROM の `lcd_empty_polls` と queue wait も記録する。
stretch は段階 2 の採否にも段階 3 の trigger にも使わず、追加の core1 専用 build と
line 単位時刻計測は作らない。

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

### 段階 3: queue full wait の追加最適化（polling/depth、条件付き、未計画）

この段階の trigger と採否は **normal 3 ROM のみ**で決め、stretch の値を混ぜない。
段階 2 の A/B で得た各 30 窓について、窓ごとに
`lcd_queue_wait_us / (frames * frame_us_avg)` を計算し、ROM ごとの中央値を depth 4 基準とする。
ただし `frame_us_avg` と `p95_us` の両中央値が16,700 us以下なら、queue wait は60 fps pacing中の
待ちでありnormalの高速化余地を示さないため、polling/depth段階を実装しない。
3 ROM すべてが 1% 未満なら、この段階は実装せず version も予約しない。いずれかが 1% 以上なら、
queue-full loop の polling 粒度を先に調べる。`lcd_queue_wait_us / lcd_queue_wait_count` が
約 100 us なら `sleep_us(100)` の量子化が支配しているため、depth 6 を自動的には実装せず、
polling 幅の比較を先に独立計画する。それでも queue wait が 1% 以上なら、depth 6 を
別 commit・次の patch version で試す。

- `DISPLAY_LCD_WORKER_QUEUE_DEPTH` を 4 から **6** へ変更する
  （段階 2 完了時から `.bss +704 byte`、期待値 `97544 -> 98248`。
  列挙した主要 static 領域の小計は `1.1.27` 比 -304 byte、段階 1 比 -704 byte）
- `.bss` の増減と p95 を depth 4 と同条件で比較する

段階 2 で queue item は既に byte 化されているため、
この段階の変更は depth のみである。したがって測定結果は
queue を深くしたことの効果だけを表す。

段階 1 で導入済みの `display_perf_take_window()` により `lcd_queue_wait_us/count` を 1 秒窓ごとに
take-and-zero し、上記 30 窓をそのまま depth 4 基準にする。
その後 depth 6 を同じ手順で比較する。既存ログの累積値はこの判断に使わない。
queue depth を 8 にするのは、depth 6 でも窓ごとの queue wait 比率中央値が 1 ROM でも 1% 以上で、
かつ depth 6 の `p95_us` 中央値が depth 4 より改善する場合だけとする。
併せて `lcd_empty_polls` を記録し、queue を深くしたことで worker の挙動が変わっていないことを確認する。

stretch は別課題である。段階 1 log `pico20260731_203816.log` の LodeRunner stretch は、
遷移直後の 1 窓を除く 35 窓の集計で queue wait が frame time の約 **22.1%**、
`lcd_queue_wait_us / lcd_queue_wait_count` が約 **101.2 us** だった。これは Xevious ではない。
100 us sleep を使う queue-full loop の量子化とほぼ一致するため、stretch を最適化するときは
queue depth を増やす前に sleep 幅または通知方式を独立に比較する。queue wait から
「4.5 ms の overlap を必ず回収できる」とは断定せず、現時点では改善候補として記録する。
同じ 100 us 量子化は新しい baseline の Xevious normal でも確認され、queue wait 比率中央値は
8.9331%、1 wait は約 102.6 us だった。したがって normal でも上記の polling-first 規約を使う。
段階 2 後は3 ROMすべてが平均・p95とも16,700 us以下になったため、normal向け段階 3 は実装しない。
stretchの約27--30 ms/frameは別課題として残す。

### 段階 4 以降 (任意、別課題)

- LCD COLMOD 12 bit/pixel 化
  (`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md` の候補 1)
- `InfoNES_SetLineBuffer()` へ queue slot を直接渡して copy を廃止する
- 4 px 1 word store 化

段階 4 以降は、段階 2 の実測と別課題のstretch結果を見て個別に判断する。

## 未確認事項

- `1.1.27` における background tile の draw 占有率（採否には使わない参考値）
- `g_perf_ppu_bg_tile_us` の区間のうち `renderBgTileFull()` が占める割合
  (本案が削るのはこの内側だけなので、占有率がそのまま削減率にはならない)
- 段階 2 の後、sprite 合成が core0 に残る形で問題がないか
  (index 化により sprite 合成も byte 操作になるため軽くなる見込みだが未計測)
- 段階 2 の LUT lookup 追加後も core1 が空 queue を継続して観測するか
  （段階 1 では normal 約 2 万回/窓、stretch 約 8--9 千回/窓を確認済み）
- 1 frame 内で連続する scanline に対して、
  queue depth を超える回数の palette 変更が起きる ROM があるか

## この文書の改訂

### 第 9 版から第 10 版へ (`perf/bg-tile-share-log`)

- 段階 2 の固定30窓、追加プレイ、normal/stretchの実機合格結果を記録した
- Xevious baselineに3%を適用すると60 fps pacing上限より速い値を要求する誤りを訂正し、
  平均・p95とも16,700 us以下を上限到達として採用できる条件を追加した
- 段階 2 後はnormal 3 ROMすべてが上限到達したため、normal向け段階 3を実装しないと決定した

### 第 8 版から第 9 版へ (`perf/bg-tile-share-log`)

- 無操作30窓の `1.1.28` baseline log を合格確認し、ROM別の中央値と段階 2 合格上限を固定した
- Xevious normal でも queue wait 比率 8.9331%、1 wait 約 102.6 us を確認したため、
  段階 3 は depth 変更より先に polling 粒度を比較する規約へ変更した

### 第 7 版から第 8 版へ (`perf/bg-tile-share-log`)

段階 1 合格後のログ再レビューを反映し、段階 2 の比較手順と境界 reset を固定した。

- attract demo の位相差が 3% の採用閾値より大きいため、3 窓比較を廃止し、段階 1 / 2 双方で
  ROM 開始後の最初の窓を捨てた無操作の連続 30 窓を新規取得して中央値を比べる規約に固定した
- `display_perf_reset()` は毎秒呼ばれる `perf_reset()` 内ではなく、`InfoNES_Init()` と
  `InfoNES_Reset()` からだけ呼ぶ契約にした。遷移直後の窓は比較から除外する
- hot path の branch を増やさず不正 byte の LUT 外 read を防ぐため、core1 / fallback / fault の
  LUT を 256 entry へ拡張し、`.bss` 期待値を `97544` へ更新した
- 段階 3 の判定範囲を normal 3 ROM に限定した。LodeRunner stretch の queue wait 約 22.1% と
  100 us polling 量子化は、queue depth と分離した将来課題として記録した

### 第 6 版から第 7 版へ (`perf/bg-tile-share-log`)

段階 1 実装・実機合格後の再照合を反映し、段階 2 で判断が残らないよう固定した。

- `WorkLine`、background descriptor、sprite 合成、queue item、normal/stretch packer の
  最終 signature と削除対象を列挙した
- producer の有効 index を `0x00..0x20` に限定し、全画素初期化と 64 entry LUT の直接 lookup
  契約を明記した
- worker、fallback、snapshot apply が同じ LUT 構築 helper と packer を使うよう固定した
- 段階 1 の fault 時 pixel zero clear は index 0 が backdrop になるため段階 2 で削除し、
  zero palette を packer へ渡す方式に固定した
- `g_bg_tile_pair_opaque4` も削除対象へ加え、段階 1 直後を起点に RAM と queue traffic を再計算した
- ARM compiler、SDK、build directory、CMake option を含む比較 build command を固定した
- 改訂履歴に残っていた `NESCO_BG_INDEX_METRICS` と段階 3 version 予約が現在は無効であることを明記した

### 第 5 版から第 6 版へ (`perf/bg-tile-share-log`)

再レビューを反映した。

- clear 表の関数名を `InfoNES_HSync()` へ訂正し、段階 2 で 256 byte clear とコメントを直すことを追加した
- `display_perf_take_window()` を段階 1 で導入する唯一の handoff 経路にした。core1 由来の
  `palette_applied` / `palette_protocol_faults` / `empty_polls` だけは queue lock 下で take-and-zero し、
  core0 専用 counter は lock を取らない
- `empty_polls` は 50 µs sleep 周期に左右されるため、窓間の増減でなく「十分大きい / ほぼ 0」で読む規約にした
- invalid palette payload の構造体 copy で警告が出ても全 item memset を戻さないことを明記した
- queue traffic の cycle 値を再導出できないため、理論値を tile/opaque の約 1.69 ms 小計と
  traffic 削減分へ分けた

### 第 4 版から第 5 版へ (`perf/bg-tile-share-log`)

実装前レビューを反映した。

- **RGB565 black 専用の予約 index `0x20` を追加した。**
  screen off と 4 種の clip clear が palette index 0 ではなく `0x20` を書き、
  core1/fallback が black を出す契約にした。これにより backdrop 色への回帰を防ぐ
- **palette 規約を LINE item 限定にした。** `FRAME_END` の zero 初期化 palette field は
  検査しないため、正常な frame marker が protocol fault を発生させない
- **`K6502_rw.h` を読む 2 翻訳単位を明記した。** `K6502.cpp` と `InfoNES_pAPU.cpp` の両方で
  `display.h` を先に include するため、dirty API の未宣言参照を作らない
- **全 item の zero 初期化を禁止し、明示代入にした。** clean line の余計な item memset を
  避け、`1.1.27` から段階 2 への queue line traffic の比較を 1,052 byte から 608 byte に訂正した
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
  この版で検討した `NESCO_BG_INDEX_METRICS` は第 5 版で撤回済みであり、現在は追加しない。
  健全性確認は baseline log の `lcd_empty_polls` と `palette_protocol_faults` だけを使う
- **`display_perf_snapshot()` を窓単位の `display_perf_take_window()` へ置換することを決めた。**
  既存の累積 `lcd_queue_wait_*` は depth 判断に使わず、段階 3 の直前に depth 4 基準を取り直す
- **段階 1 / 2 の version を 1.1.28 / 1.1.29 と固定した。**
  当時予約した段階 3 の 1.1.30 は第 5 版で撤回済みであり、queue wait が条件を満たすまで version を予約しない

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
- 段階 2 に core1 側の計測を追加した（第 5 版で line 単位計測は撤回済み）
- 比較の基準を fps 比から `frame_us` (平均、中央値、95 percentile、最大) へ変更した
- 代替案 (NES master color 方式) との比較を追加した
