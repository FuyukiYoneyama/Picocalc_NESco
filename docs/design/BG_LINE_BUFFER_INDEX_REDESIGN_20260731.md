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

tile あたりのメモリ操作は概算で 28 回から 10 回になる。

Cortex-M0+ の `LDR` / `STR` は 2 cycle である。
32 tile x 240 line = 7,680 tile/frame として、
メモリ操作だけで約 0.28 M cycle、250 MHz で **約 1.1 ms/frame** の削減になる。
shift / mask 側の削減も同程度あるため、内側 loop はおおむね半減する見込みである。

frame 全体への効果は background tile が draw に占める割合に比例する。
その割合が `1.1.26` で不明であるため、ここでは frame 全体の削減量を見積もらない。
計測 D の結果を得てから見積もる。

## 副次効果

### LCD の COLMOD 12 bit/pixel が安く入る

`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md` の候補 1 は、
core1 の packer を 2 px = 3 byte 詰めへ書き換える必要があった。

本案を入れると core1 は元から index から色への LUT を引くため、
LUT 出力を RGB565 にするか RGB444 にするかの違いだけになる。
`InfoNES_Palette444ToRgb565()` が変換の単一箇所であることも確認済みである。

同分析で挙げた「stretch が core1 律速へ移る可能性」も、
core0 から core1 へ渡るデータ量が半分になるぶん緩む。

### queue depth を RAM 増なしで倍にできる

queue item が `WORD pixels[256]` から `BYTE pixels[256]` になるため、
同じ RAM で `DISPLAY_LCD_WORKER_QUEUE_DEPTH` を 4 から 8 へ増やせる。

これは core0 が `PostDrawLine()` で queue full を待つ時間を直接減らす。
LCD 帯域分析で「normal view で期待できるのは二次効果のみ」と結論した、
その二次効果を大きくする側の変更でもある。

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

- `PalTable` への書き込みは `infones/K6502_rw.h` の 2 か所だけなので、そこで dirty flag を立てる
- dirty のときだけ、queue item へ `PalTable` 32 entry を copy する
- core1 は queue item に snapshot があればそれを、なければ直近の snapshot を使う

通常 frame で palette 書き換えは 0 回から数回程度なので、copy 費用は無視できる。
必要な RAM は snapshot 1 個 64 byte x queue slot 数である。

なお snapshot を `WORD` のまま持つ場合 64 byte、
RGB444 化と同時に行う場合も 64 byte で変わらない。

### queue depth を増やすことの副作用

queue が深くなると、core0 が core1 より先行できる scanline 数が増える。
これは palette snapshot の必要性を強めるが、上記対策で吸収できる。

mid-frame の mapper CHR 切り替えは scanline buffer の内容に影響しないため、
snapshot の対象外である。

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

LCD 帯域側の計測 (`docs/project/TASKS.md` の計測 1 / 計測 2) と同じ session で取る。
build を分けると比較できなくなる。

### 順序

計測 D を最初に置く。これが不発なら以降は不要になるためである。

1. **計測 D: `1.1.26` の draw 内訳**
   - 目的: background tile が draw 時間に占める割合を現在値で確定する
   - 方法: `NESCO_CORE1_BASELINE_LOG=ON` の `[CORE1_SUMMARY]` で
     `cpu_us` `ppu_us` `apu_us` `other_us` の比率を取る
   - `1.1.5` 相当の tile ごと `time_us_64()` は計測負荷が高すぎるため使わない
     (`1.1.8` でその理由により軽量版へ移行した経緯がある)
   - 対象: `LodeRunner.nes` `Xevious.nes` `Project_DART_V1.0.nes`

2. **計測 A: 基準 fps**
   - 目的: 実装後の比較基準を作る
   - 計測 D と同じ build、同じ session、同じ場面で取る
   - normal 3 ROM に加えて `Xevious.nes` の stretch も取る
     (これは LCD 帯域分析の計測 1 を兼ねる)

3. **計測 B: `lcd_queue_wait_us` / `lcd_queue_wait_count`**
   - 目的: queue depth 4 が core0 をどれだけ止めているかを知る
   - LCD 帯域分析の計測 2 と同一
   - 本案の queue depth 8 化の効果見積もりにも使う

同一 build で 3 件とも取れるため、実機作業は 1 回で済む。

### 判断基準

計測 D の結果で分岐する。

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

ここで計測 A と同条件の fps を取り、削減量を確認する。

打ち切り基準: 3 ROM すべてで fps 改善が 3% 未満なら、本案を不採用として記録し、
`docs/project/Picocalc_NESco_HISTORY.md` へ経緯を残して段階 1 ごと revert する。

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

### 段階 3: queue item の byte 化と depth 8 化 (効果測定ポイント)

- queue item を `BYTE pixels[256]` へ変更する
- `DISPLAY_LCD_WORKER_QUEUE_DEPTH` を 4 から 8 へ変更する
- `.bss` の増減を確認する

ここで `lcd_queue_wait_us` を計測 B と同条件で取り直し、減少を確認する。

### 段階 4 以降 (任意、別課題)

- LCD COLMOD 12 bit/pixel 化
  (`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md` の候補 1)
- `InfoNES_SetLineBuffer()` へ queue slot を直接渡して copy を廃止する
- 4 px 1 word store 化

段階 4 以降は、段階 3 までの実測を見てから個別に判断する。

## 未確認事項

- `1.1.26` における background tile の draw 占有率
- 段階 2 の後、sprite 合成が core0 に残る形で問題がないか
  (index 化により sprite 合成も byte 操作になるため軽くなる見込みだが未計測)
- palette snapshot の発生頻度が実 ROM でどの程度か
