# LCD bus bandwidth analysis

作成日: 2026-07-31

対象 branch: `perf/bg-tile-share-log`

初回分析対象 version: `1.1.26`

実測更新 version: `1.1.31`

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

`0x3A`はCOLMODである。ST7365P SPEC V1.0 (2023/02) section 9.2.32では、
control interfaceの`D2:D0`は`101=16 bit/pixel`、`110=18 bit/pixel`、
`111=24 bit/pixel`だけを定義している。現在値`0x65`の`D2:D0=101`はRGB565である。

参照した仕様書:
`https://cn.display-lcd.com/data/upload/admin/202503/67e36677ac8c3.pdf`

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

### 破棄: COLMOD 12 bit/pixel

旧版では`0x3A <- 0x63`をcontrol interfaceの12 bit/pixelと解釈していたが、
これはST7365P仕様と一致しない。

- `0x63`の`D2:D0=011`は未定義であり、12 bit/pixelではない
- control interfaceで定義されるのは16/18/24 bit/pixelだけである
- section 10.5のData Transfer Modeにも12 bit転送はない

NES paletteの元情報がRGB444相当であることは事実だが、それだけではpanelへの12 bit転送を
可能にしない。`0x63`候補、2 px = 3 byte packer、RGB444 DMAは実装しない。
旧版の25%削減、normal 11.80 ms、stretch 18.43 msという値は、対応していない転送形式を
仮定した机上値なので採用判断から削除する。

### 候補 1: frame 単位の window 設定

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

ST7365P仕様section 10.4 Data Transfer Pauseは、frame memory dataを1 byte完了した後に
CSXを解除した場合、driverが待機し、次にCSXをactiveにしたとき中断位置から転送を継続すると
明記している。現在のDMAはbyte単位で完了してからCSを解除するため、strip間でCSを
deassertしてもGRAM pointerを保持できることは仕様上確認済みである。

引き換えになる点:

- 現在のstrip単位windowは毎strip自己再同期する
- frame単位では1 stripが欠けると同じframe内の後続位置がずれる
- 毎frame先頭でCASET/RASET/RAMWRを再発行し、次frameでは必ず自己復旧させる必要がある

既存の `s_perf_lcd_wait_us` / `s_perf_lcd_flush_us` は fallback packer だけを計測するため、
通常の worker 動作では 0 であり、この候補の根拠には使えない。
この候補に着手する場合は先に、workerが実行する`lcd_set_window()`内の
`lcd_wait_idle()` を driver 層で計測し、frame-end handoff で core0 へ渡す専用 counter を追加する。
その実測から frame あたり 180 回の drain 費用を判定する。

削減できるcommand byte自体は29 window分の`29 x 11 = 319 byte`、62.5 Mbpsで
約40.8 us/frameに留まる。queue depth 8より期待効果が小さいため、次工程にはしない。

### 候補 2: DMA 転送単位を 32 bit にする

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

### 候補 3: 縦 224 line への crop

NES の実画面は上下 8 line が表示外扱いになることが多く、
多くの emulator は 224 line 表示を default にしている。

256x224 にすれば転送量は 114,688 byte、`14.68 ms`、予算比 88.1% になる。

ただしこの候補だけは表示内容が実際に変わる。
上下 8 line を使うタイトルでは見え方が変わるため、
無条件変更ではなく設定項目にする必要がある。

stretch view は 224 x 1.25 = 280 line となり、320x280 になる。

### 候補 4: vreg build option

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
| stretch 320x300 RGB565 (現状) | 192,000 | 24.58 ms | 147.5% |
| stretch 320x280 RGB565 | 179,200 | 22.94 ms | 137.6% |

縦cropの削減はnormal 1.05 ms、stretch 1.64 msである。ただし表示内容が変わる。
現行ST7365Pの対応形式と62.5 Mbpsを維持する限り、表示差なしで画素byte数を25%減らす案はない。

## LCD帯域候補の順序

| 順 | 候補 | 効果 | 主なリスク | 表示変化 |
|---|---|---|---|---|
| 1 | frame単位window | command 40.8 us + drain費用 | 同一frame内のstrip落ちで表示ずれ | なし |
| 2 | DMA 32 bit化 | core1/SRAM負荷減 | byte順、PIO/DMA同時変更 | なし |
| 3 | 縦224 crop | normal -1.05 ms、stretch -1.64 ms | 表示範囲減少 | あり (設定項目化が必要) |
| 4 | vreg build option | OC安定性 | 消費電力と発熱 | なし |

frame windowとDMA 32 bitは画素byte数を変えないので、15.73/24.58 msの純転送下限は動かない。
cropは表示内容が変わるため、無条件最適化ではなく設定項目として扱う。

実際の次工程はLCD driver変更やdepth変更ではなく、
`docs/design/STRETCH_QUEUE_DEPTH_OPTIMIZATION_PLAN_20260731.md`を正本とする。

1. 不採用の`1.1.31` 10 us retryだけをrevertし、`1.1.30`計測fieldを残す
2. `1.1.32`ではdepth 4のまま、strip flushのDMA waitとwindow設定時間を計測する
3. DMA waitがbus idle仮説を支持した場合だけ、`1.1.33`でdepth 8をA/Bする
4. 同じPhase 0で測るwindow設定全体の削減上限が250 us/frame以上の場合だけ、
   frame単位windowを後続の独立候補にする

COLMOD `0x63`は未定義なので候補順へ戻さない。

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

その後、BG palette index化を行った`1.1.29`ではnormal 3 ROMすべてが
`frame_us_avg` / `p95_us` とも `16,700 us` 以下へ到達した。
従って次のdepth A/Bはstretch改善を主目的とし、normalは非退行確認だけを行う。

### 予測される効果

- normalは60 fps pacing上限へ達しているため、平均fps改善は期待しない
- stretchはRGB565バス上限40.7 fpsが物理天井であり、depth/window/DMA変更でもこの天井は上がらない
- depth 8はstrip間の重なり損失、frame windowはcommand/drain、DMA 32 bitはcore1/SRAM負荷だけを削る
- stretchを60 fpsへ到達させるには、表示範囲、画素形式、バス速度のいずれかを変える必要がある。
  現panelでは12 bit形式がないため、表示差なし・現バス速度のまま60 fpsへ届く計画はない

## 実測状況

`1.1.30` / `1.1.31`の固定A/Bを次のlogで行った。

- `/home/fuyuki/pico_dvl/codex/log/pico20260731_230528.log`
- `/home/fuyuki/pico_dvl/codex/log/pico20260731_231422.log`

| ROM | baseline stretch | 10 us candidate | 差 |
|---|---:|---:|---:|
| LodeRunner | 28.816 ms | 28.804 ms | -0.013 ms |
| Project_DART | 29.835 ms | 29.894 ms | +0.060 ms |
| Xevious | 26.283 ms | 26.242 ms | -0.041 ms |

retry 1回は約100.5 usから約10.1 usへ短縮したが、queue wait/frameとframe timeは
ほぼ変わらなかった。従ってpolling量子化は主因ではなく、10 us候補は不採用とした。
全log 606対でpalette protocol fault 0、実プレイでも問題はなかった。

## 未確認事項

以下は実機確認が必要である。

- depth 4でcore1がstrip flush時に前DMAを待つ実時間
- `lcd_set_window()` 30回/frameの実費用
  - `1.1.32`診断buildでcommand byte、5回/windowの`lcd_wait_idle()`、GPIO/関数費用をまとめて測る
- frame単位windowの実機表示が仕様どおりframe境界で自己復旧するか
