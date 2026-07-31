# LCD bus bandwidth analysis

作成日: 2026-07-31

対象 branch: `docs/lcd-bus-bandwidth-analysis`

初回分析対象 version: `1.1.26`

実測更新 version: `1.1.29`

## 目的

現在の LCD 転送が 60fps 予算に対してどれだけ余裕がないかを、コードから確認できる事実だけで数値化する。

そのうえで、転送量を減らす候補を効果順に並べ、`docs/project/TASKS.md` の
`[deferred] stretch 表示の追加高速化` を再開するときの判断材料にする。

この文書は分析と候補整理までを範囲とする。実装と実機確認は含まない。

## 確認済みの事実

以下はすべて現在のソースで確認した内容である。推定は含まない。

### バス速度

`drivers/lcd_spi.c`:

```c
static const float LCD_PIO_CLKDIV = 2.0f; /* 250 MHz sysclk -> 62.5 MHz SPI-equivalent */
```

`drivers/lcd_spi.pio` の転送ループは 1 bit につき 2 命令である。

```
.wrap_target
    out pins, 1    side 0
    nop            side 1
.wrap
```

`platform/main.c`:

```c
set_sys_clock_khz(250000, true);
```

したがって PIO clock は `250 MHz / 2.0 = 125 MHz`、1 bit は 2 cycle なので
bit rate は `62.5 Mbps`、1 byte あたり `128 ns` である。

### 画素形式

`drivers/lcd_spi.c` の init sequence:

```c
lcd_write_command1(0x3Au, 0x65u);
```

`0x3A` は COLMOD で、`0x65` は MCU interface 16 bit/pixel、つまり RGB565 である。

### viewport

`platform/display.c`:

```c
NES_VIEW_NORMAL_W = 256,
NES_VIEW_NORMAL_H = 240,
NES_VIEW_STRETCH_W = 320,
NES_VIEW_STRETCH_H = 300,
```

### strip 分割

`platform/display.h`:

```c
#define STRIP_HEIGHT  8
```

normal view は 240 / 8 = 30 strip/frame である。

### 電圧設定

`vreg_set_voltage()` の呼び出しはソース全体に存在しない。
クロック設定は `set_sys_clock_khz(250000, true)` のみである。

### SD card のバス

`drivers/sdcard.c`:

```c
static spi_inst_t *const SD_SPI = spi0;
```

SD card は hardware SPI (PL022) を使用している。
pico-sdk の `set_sys_clock_pll()` は clk_sys 設定後に clk_peri を clk_sys へ張り替えるため、
現在 PL022 の SSPCLK は 250 MHz である。

## 帯域計算

60fps の 1 frame 予算は `16.667 ms`。1 byte は `128 ns`。

| 表示 | 1 frame の byte 数 | 転送時間 | 上限 fps | 60fps 予算比 |
|---|---|---|---|---|
| normal 256x240 RGB565 | 122,880 | 15.73 ms | 63.6 | 94.4% |
| stretch 320x300 RGB565 | 192,000 | 24.58 ms | 40.7 | 147.5% |

normal view は 1 frame のうち `15.73 ms` が純粋な画素転送で埋まっている。

これは core0 の負荷ではない。転送は DMA と PIO が行うため、core0 の emulation 時間は
`16.667 ms` を丸ごと使える。埋まっているのは LCD バスと core1 側の非重複時間である。

core1 の packing は double buffer で DMA と重なっている
(`lcd_dma_acquire_buffer()` が未使用側 buffer を返す) ため、
core1 に残る非重複の余裕は 1 frame あたり `0.94 ms` となる。

stretch view の `147.5%` は帯域から必然的にそうなる。
`README.md` の「常時 60fps は保証していません」は実装上の制約ではなく、
現在の画素形式とバス速度における物理的な帰結である。

## 規格に対する超過

現在の構成は 2 か所で定格を超えている。

- ST7365P の書込み最小周期 66 ns (15.15 MHz) に対し、62.5 MHz は 4.12 倍
- RP2040 の定格 133 MHz に対し、sysclk 250 MHz は 1.88 倍

加えて、`vreg_set_voltage()` を呼んでいないため、RP2040 は default の 1.10 V のまま
250 MHz で動作している。手元個体で動作している事実はログで確認できているが、
これは個体差と温度に依存する領域であり、配布 binary の前提としては弱い。

## PIO 採用理由についての訂正

「PL022 の SSPCLK が定格 133 MHz なので、clk_peri を 250 MHz で使えない。
だから PIO でないとコア OC とバス速度を独立に決められない」という説明は成立しない。
理由は 2 つある。

1. 上記のとおり、本プロジェクトは既に clk_peri 250 MHz で PL022 を SD card に使っている。
   PIO 採用によって PL022 の規格外動作を回避できている事実はない。
2. clk_peri を PLL_SYS 分周で 125 MHz (SSPCLK 定格内) にすれば、
   CPSDVSR = 2 で `125 / 2 = 62.5 MHz` が得られる。clk_sys 250 MHz は維持できる。
   hardware SPI でも規格内で同じバス速度に到達できる。

PIO を採用する妥当な理由として挙げられるのは、以下である。

- clk_peri は SD card (spi0) と UART 921600 が共有している。
  LCD 速度を clk_peri で決めると、SD baudrate と UART 分周の再導出が必要になる。
  PIO の clkdiv は state machine ローカルなので、LCD バス速度を単独のノブにできる。
- PIO の clkdiv は fractional である。
  `2.0 / 2.5 / 3.0` (62.5 / 50 / 41.7 MHz) を他の周辺に影響させずに掃引できる。
  PL022 の CPSDVSR は偶数整数のみで、この粒度は出せない。
  個体差と温度の実機評価を行う場合、この差は実用的に効く。
- 将来 DC を side-set または 9-bit frame でバス内に取り込む経路がある。
  これは後述の drain 削減に直結する。PL022 では原理的に不可能である。

## 転送量削減の候補

帯域そのもの (62.5 Mbps) は規格超過側にあるため、これ以上上げる方向は取らない。
削減対象は転送量とする。

### 候補 1: COLMOD 12 bit/pixel

現在 LCD へ送っている RGB565 は、情報量として 12 bit しか持っていない。

`platform/display.c` の `NesPalette[64]` は RGB444 encoding であり、
`display_init()` はこれを nibble ごとに bit 複製して RGB565 へ展開している。

```c
int r5 = (i << 1) | (i >> 3);
int g6 = (i << 2) | (i >> 2);
int b5 = (i << 1) | (i >> 3);
```

下位 bit はすべて bit 複製で生成した冗長分である。
したがって COLMOD を `0x3A <- 0x63` (MCU interface 12 bit/pixel) に変更し、
2 画素 3 byte で詰めれば、転送量が 25% 減り、送出する色情報は減らない。

これは他の候補と違い、表示内容が変わらない削減である。

必要な変更点:

- `drivers/lcd_spi.c` の COLMOD parameter
- `platform/display.c` の `display_pack_line_normal()` / `display_pack_line_stretch_320()`
  を 2 px = 3 byte 詰めに変更する
- `lcd_dma_write_bytes_async()` の `n_bytes > s_window_pixels * 2` 上限計算
- ROM menu / help / screenshot viewer は任意の RGB565 を使うため、
  mode 切替時に COLMOD を戻す

### 候補 2: frame 単位の window 設定

`lcd_set_window()` は CASET / RASET / RAMWR の 3 command を発行する。
`lcd_write_command()` と `lcd_write_data()` はそれぞれ `lcd_wait_idle()` を呼ぶため、
1 window あたり PIO の完全 drain が 5 回、直前 DMA の待ちが 1 回で計 6 回になる。
normal view は 30 strip なので 1 frame あたり 180 回である。

バス上の byte 数は 1 window で 11 byte、30 window で 330 byte = 42 µs。
drain 1 回あたりの実費用は未計測である。

normal view の 30 strip は `y = 24..263` の連続領域へ順に書いているだけであり、
LCD controller は window 内で GRAM address を自動 increment する。
したがって frame 先頭で 1 回だけ window を設定し、
以降は DC = 1 のまま画素を流し込めば、29 回分の window 設定を削除できる。

引き換えになる点:

- 現在の strip 単位 window は自己再同期する。
  frame 単位にすると、strip が 1 つ落ちた場合に以降の表示がずれる。
- `lcd_dma_wait()` は strip 間で CS を deassert している。
  RAMWR 途中の CS deassert で GRAM address pointer が保持されるかは、
  ST7365P の実機確認が必要である。【推定】多くの ST77xx 系では保持されるが、保証はできない。

既存の `s_perf_lcd_wait_us` / `s_perf_lcd_flush_us` は fallback packer だけを計測するため、
通常の worker 動作では 0 であり、この候補の根拠には使えない。
候補 2 に着手する場合は先に、worker が実行する `lcd_set_window()` 内の
`lcd_wait_idle()` を driver 層で計測し、frame-end handoff で core0 へ渡す専用 counter を追加する。
その実測から frame あたり 180 回の drain 費用を判定する。

### 候補 3: DMA 転送単位を 32 bit にする

現在の DMA 設定:

```c
channel_config_set_transfer_data_size(&s_lcd_dma_cfg, DMA_SIZE_8);
```

PIO 側は `sm_config_set_out_shift(&c, false, true, 8)` で autopull threshold 8 である。
このため normal view 1 frame で 122,880 回の DMA transaction が発生している。

autopull threshold を 32、DMA を `DMA_SIZE_32` にすれば 30,720 回に減る。
同時に packer 側も 4 byte を 1 store で書けるようになり、
`display_pack_line_normal()` の store 数が 4 px あたり 8 回から 2 回になる。

バス時間は 62.5 Mbps のままで変わらない。
効くのは core1 の非重複時間 `0.94 ms` と、DMA による SRAM 競合の側である。

byte 順は MSB first なので、buffer 上は `(hi0 << 24) | (lo0 << 16) | (hi1 << 8) | lo1`
の配置が必要になる。

### 候補 4: 縦 224 line への crop

NES の実画面は上下 8 line が表示外扱いになることが多く、
多くの emulator は 224 line 表示を default にしている。

256x224 にすれば転送量は 114,688 byte、`14.68 ms`、予算比 88.1% になる。

ただしこの候補だけは表示内容が実際に変わる。
上下 8 line を使うタイトルでは見え方が変わるため、
無条件変更ではなく設定項目にする必要がある。

stretch view は 224 x 1.25 = 280 line となり、320x280 になる。

### 候補 5: vreg build option

`vreg_set_voltage(VREG_VOLTAGE_1_15)` を `set_sys_clock_khz()` の前に置けば、
250 MHz 動作の余裕は増える。

ただし PicoCalc は電池駆動であり、vreg を上げると消費電力と発熱が増える。
無条件に有効化せず、build option にして
「安定しない個体では有効化」と README 側に書くのが妥当である。

## 効果一覧

60fps 予算 `16.667 ms` に対する比率。

| 構成 | byte/frame | 転送時間 | 予算比 |
|---|---|---|---|
| normal 256x240 RGB565 (現状) | 122,880 | 15.73 ms | 94.4% |
| normal 256x224 RGB565 | 114,688 | 14.68 ms | 88.1% |
| normal 256x240 RGB444 | 92,160 | 11.80 ms | 70.8% |
| normal 256x224 RGB444 | 86,016 | 11.01 ms | 66.1% |
| stretch 320x300 RGB565 (現状) | 192,000 | 24.58 ms | 147.5% |
| stretch 320x300 RGB444 | 144,000 | 18.43 ms | 110.6% |
| stretch 320x280 RGB444 | 134,400 | 17.20 ms | 103.2% |

縦 crop 単独の削減は `1.05 ms`、COLMOD 12 bit 単独の削減は `3.93 ms` である。
stretch view を 60fps に近づけられるのは COLMOD 12 bit のみで、
縦 crop だけでは stretch は 137.6% にとどまり届かない。

## LCD帯域候補の順序

| 順 | 候補 | 効果 | 主なリスク | 表示変化 |
|---|---|---|---|---|
| 1 | COLMOD 12 bit/pixel | -3.93 ms | panel 側の展開結果の実機確認 | なし |
| 2 | frame 単位 window | 未計測 | strip 落ちで表示ずれ | なし |
| 3 | DMA 32 bit 化 | core1 負荷減 | byte 順 | なし |
| 4 | 縦 224 crop | -1.05 ms | 低 | あり (設定項目化が必要) |
| 5 | vreg build option | 安定性 | 消費電力と発熱 | なし |

1 と 2 を入れた場合、normal view は予算比 70% 前後、stretch view は 110% 前後になる。
4 は表示内容が変わるため最後に置き、設定項目として実装する。

ただし`1.1.29`のstretch実測では、queue-full retry 1回が現行の
`sleep_us(100)`とほぼ一致した。上表はLCD帯域側へ着手した後の順序であり、実際の次工程は
`docs/design/STRETCH_QUEUE_RETRY_OPTIMIZATION_PLAN_20260731.md`を正本とする。

1. `1.1.30`でpacing sleepをbaseline logへ追加する
2. `1.1.31`でframe hot pathのqueue retryだけを100 usから10 usへ変更してA/Bする
3. 結果に応じてdepth、通知方式、COLMODのどれを計画するか決める

計測baselineとcandidateを先に作り、実機確認はnormal/stretch、3 ROMを1回にまとめる。

なお 1 を入れて予算比 70% まで下がれば、
sysclk を 200 MHz へ落として規格超過を一段解消する選択肢が現実的になる。
これは予算比 94.4% の現状では取れない手である。

## 現在の律速はどちらか

上記の削減は、LCD バスが律速である場合にしか fps に現れない。
`1.1.26` の実機 A/B (`/home/fuyuki/pico_dvl/codex/log/20260719_174109.log`) では次の値である。

| ROM | 実測 fps | frame time | normal バス下限 15.73 ms との差 |
|---|---|---|---|
| `Xevious.nes` | 55.50 | 18.02 ms | +2.29 ms |
| `LodeRunner.nes` | 47.99 | 20.84 ms | +5.11 ms |
| `Project_DART_V1.0.nes` | 45.68 | 21.89 ms | +6.16 ms |

これは `1.1.26` 時点の値であり、3 ROM とも frame time がバス下限を上回っていた。
したがって normal view の平均 fps を決めているのは core0 の emulation であり、
LCD バスではない。バス下限を下げても、上に乗っている 18〜22 ms は動かない。

`0.1.74` 期のログでも `lcd_wait_us=29253.61` / `frames=52.95` すなわち
1 frame あたり `552 µs` であり、core0 が LCD を待つ時間は frame の 3% 程度である。

これは transfer が DMA と PIO で非同期に走っているためで、設計どおりの結果である。

その後、BG palette index化を行った `1.1.29` ではnormal 3 ROMすべてが
`frame_us_avg` / `p95_us` とも `16,700 us` 以下へ到達した。
したがってnormal向けのpolling変更、queue depth変更、LCD帯域変更は行わない。

### 予測される効果

- normal view: 平均 fps はほぼ変わらない
  - 期待できるのは二次効果のみで、LCD worker queue depth が 4 scanline しかないため
    (`platform/display.c` の `DISPLAY_LCD_WORKER_QUEUE_DEPTH`)、
    core1 の strip DMA 待ちで queue が埋まると core0 が `PostDrawLine` で止まる
  - core1 の非重複余裕は Xevious 換算で `2.29 ms` から `6.22 ms` に増えるため、
    平均 fps ではなく最低 fps 側が改善する可能性がある
  - ただし `Xevious.nes` の余裕は `2.29 ms` しかなく、重い場面では
    現在も LCD バスに当たっている可能性が残る
- stretch view: ここが本命である
  - バス上限 `40.7 fps` は確実に効く位置にある
  - `1.1.27` の Xevious stretch 実測は `36.7 fps` で、天井には貼り付いていない
  - COLMOD 導入時の上限は `40.7 fps` から `54.3 fps` へ上がるが、現状は core0 最適化を優先する
- 将来の core0 最適化に対する天井
  - 現在の天井は `63.6 fps` で、`Xevious.nes` の `55.50 fps` との差は 8 fps しかない
  - COLMOD 12 bit/pixel を入れると天井は `84.8 fps` になる

### stretch で新たに律速になり得る箇所

stretch の core1 は 1 frame で 300 line x 320 px を pack する
(`platform/display.c` の `display_pack_line_stretch_320()`)。

バスが `24.58 ms` から `18.43 ms` に縮むと、core1 はより短い時間で、
かつ 1 byte あたりより複雑な詰め方をすることになる。
RGB565 は 2 px = 4 byte で境界が揃うが、RGB444 は 2 px = 3 byte で揃わない。
stretch が core1 律速へ移る可能性がある。

緩和策は本文書の候補内にある。

- 候補 3 (DMA 32 bit 化) を stretch では同時に入れる。
  packer の store 数が 4 px あたり 8 回から 2 回に減り、12 bit 化の増分を相殺できる
- palette LUT を RGB444 のまま出す。
  BG line buffer index 化の後は `s_line_buffer` 自体は palette index のままにし、
  core1 の palette LUT 出力だけを RGB565 から RGB444 へ差し替える。packer は shift だけで済み、
  InfoNES hot path を触らない。
  InfoNES 側が scanline buffer の値に対して monochrome bit や
  color emphasis のような色演算をしていないことは確認済みである。
  `R1_MONOCHROME` は定義だけで参照されず、scanline buffer へ色演算を加える経路はない

## 実測状況

`1.1.29` の実機log `/home/fuyuki/pico_dvl/codex/log/pico20260731_214523.log` で、
normal/stretchの追加実プレイを含む窓を取得した。目視・プレイ上の問題はなく、
全412窓でpalette protocol fault 0だった。stretch値は固定A/B区間ではなく、
入力を含む追加プレイ区間の参考値である。現在タスクの正本は `docs/project/TASKS.md` とする。

| ROM | stretch窓数 | `frame_us_avg` 中央値 | queue wait比率中央値 | 1 waitあたり |
|---|---:|---:|---:|---:|
| LodeRunner | 46 | 29.02 ms | 37.89% | 100.45 us |
| Project_DART | 37 | 30.23 ms | 34.28% | 100.72 us |
| Xevious | 28 | 27.22 ms | 48.41% | 100.43 us |

queue waitは十分大きいが、1 waitあたりが全ROMで約100 usであり、
queue-full loopの`sleep_us(100)`量子化と一致する。queue depthを増やす前に、
10 us retry候補の効果を独立して測る。採否手順は
`docs/design/STRETCH_QUEUE_RETRY_OPTIMIZATION_PLAN_20260731.md`を正本とする。

## 未確認事項

以下は実機確認が必要である。

- COLMOD 12 bit/pixel での panel 側の色展開が、現在の bit 複製展開と一致するか
- RAMWR 途中の CS deassert で GRAM address pointer が保持されるか
- `lcd_wait_idle()` 1 回あたりの実費用と、1 frame 180 回の合計
  - 既存 `wait_us` では取れない。候補 2 着手時に worker/driver 側の専用 counter を追加する
- stretchでpolling幅短縮または通知方式がframe time / p95 / queue waitへ与える効果
