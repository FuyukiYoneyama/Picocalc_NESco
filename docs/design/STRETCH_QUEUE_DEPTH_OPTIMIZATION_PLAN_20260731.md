# Stretch LCD worker queue depth optimization plan

作成日: 2026-07-31

対象 branch: `perf/bg-tile-share-log`

baseline version: `1.1.30`

candidate version: `1.1.32`

関連文書:

- `docs/design/STRETCH_QUEUE_RETRY_OPTIMIZATION_PLAN_20260731.md`
- `docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md`
- `docs/project/CONVENTIONS.md`

## 結論

次に試す変更は、LCD worker queue depthを`4`から`8`へ増やすことだけとする。
depth 8はsource側の1 stripである8 lineと一致し、前のstripをDMA転送している間に
次の1 stripをqueueへ保持できる。retry幅、packer、DMA、LCD command、palette protocolは変えない。

`1.1.31`の10 us retry候補は実機A/Bで採用条件を満たさなかったため、実装前に
commit `8ba265f`だけを`git revert`する。Phase 0の計測commit `1f1c093`は残す。
`1.1.31`は不採用実験を識別するversionとして再利用せず、depth 8候補は`1.1.32`とする。

## 根拠

### 10 us retry実験で分かったこと

`1.1.30` baselineと`1.1.31` candidateの実機A/Bでは、retry 1回の実測時間は
約100.5 usから約10.1 usへ短縮し、retry回数は約10倍になった。一方、stretchの
queue wait/frameとframe timeはほぼ変わらなかった。

従ってqueue waitの大部分は、queueに空きができた後の余分なsleepではなく、core1が
LCD DMA完了を待っていて実際にqueueを消費できない時間である。polling幅の追加調整や
通知方式を次に試す根拠はない。

### depth 8を選ぶ理由

workerは8 source lineをpackするたびに1 stripをDMAへ渡す。

- normal: `256 x 8 x 2 = 4,096 byte`、純転送約524 us/strip
- stretch: `320 x 10 x 2 = 6,400 byte`、純転送約819 us/strip
- 1 frame: `240 / 8 = 30 strip`

現在のdepth 4では、core1が前stripのDMA完了を待つ間にcore0が先行保持できるのは
次stripの半分だけである。depth 8なら次strip 1個分を保持できるため、DMA完了後に
core1が8 lineを連続してpackし、次DMAを早く開始できる可能性がある。

これはLCDの純転送下限を変えない。改善できるのはstrip間のLCD idle時間と、core0/core1の
重なり損失だけである。stretchのRGB565純転送下限は引き続き24.58 ms/frameである。

### 回収可能幅

`1.1.30`のselected stretch中央値と24.58 msとの差は次である。この全量を回収できるという
意味ではなく、depth変更でframe timeに現れ得る絶対上限として扱う。

| ROM | baseline | 24.58 msとの差 |
|---|---:|---:|
| LodeRunner | 28.816 ms | 4.236 ms |
| Project_DART | 29.835 ms | 5.255 ms |
| Xevious | 26.283 ms | 1.703 ms |

## 実装範囲

### 前提復元

最初に次を行う。

1. `git revert 8ba265f`で10 us retry変更だけを戻す
2. `platform/version.h`が`1.1.30`へ戻ることを確認する
3. `DISPLAY_LCD_WORKER_QUEUE_RETRY_US`を使うhot path 2箇所が100 usであることを確認する
4. `lcd_queue_wait_episodes`とpacing fieldが残っていることを確認する

`1f1c093`はrevertしない。既存baseline artifactとlogを解釈するためにも、Phase 0の計測fieldを
以後のbuildへ残す。

### candidate変更

source変更は次の2点だけとする。

1. `platform/display.c`

   ```c
   DISPLAY_LCD_WORKER_QUEUE_DEPTH = 8,
   ```

   現在値`4`を`8`へ変更する。queue item、push/pop、lock、retry、strip heightは変更しない。

2. `platform/version.h`

   ```c
   #define PICOCALC_NESCO_VERSION "1.1.32"
   ```

変更しないもの:

- `STRIP_HEIGHT = 8`
- frame hot pathのqueue retry `100 us`
- drain待ち `100 us`
- core1空queue待機 `50 us`
- queue item 352 byte
- palette snapshot protocolと256-entry LUT
- normal/stretch packer
- LCD DMA、PIO、window設定、COLMOD `0x65`
- fallback描画経路

## RAM契約

現行queueは`352 byte x 4 = 1,408 byte`で、ELF symbolは`0x580`である。
depth 8では`352 byte x 8 = 2,816 byte = 0xb00`になる。

```text
queue増分 = 352 * (8 - 4) = 1,408 byte
通常build .bss期待値 = 97,548 + 1,408 = 98,956 byte
計測build .bss期待値 = 97,892 + 1,408 = 99,300 byte
```

これ以外のstatic領域は変えない。期待値と異なる場合はmapまたは
`arm-none-eabi-nm -S --size-sort`で差を説明してから実機へ進む。

## buildとartifact

baselineは実装済みの次を使う。

- version: `1.1.30`
- commit: `1f1c093`
- artifact: `build-stretch-retry-baseline/Picocalc_NESco.uf2`
- build ID: `Jul 31 2026 22:56:39`
- UF2 SHA-256: `f4588e39fa0ceb403e1d55f7c3c94765b6ebc2b8322227d2c804959096c04046`

candidateは通常buildと計測buildを作る。計測artifact directoryは
`build-stretch-depth8`とし、次のoptionを使う。

```sh
cmake -S . -B build \
  -DPICO_SDK_PATH=/home/fuyuki/pico/pico-sdk \
  -DCMAKE_C_COMPILER=/usr/bin/arm-none-eabi-gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/arm-none-eabi-g++ \
  -DCMAKE_ASM_COMPILER=/usr/bin/arm-none-eabi-gcc \
  -DNESCO_CORE1_BASELINE_LOG=OFF \
  -DNESCO_PALETTE_SNAPSHOT_LOG=OFF \
  -DNESCO_BG_TILE_SHARE_LOG=OFF
cmake --build build --clean-first -j4

cmake -S . -B build-stretch-depth8 \
  -DPICO_SDK_PATH=/home/fuyuki/pico/pico-sdk \
  -DCMAKE_C_COMPILER=/usr/bin/arm-none-eabi-gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/arm-none-eabi-g++ \
  -DCMAKE_ASM_COMPILER=/usr/bin/arm-none-eabi-gcc \
  -DNESCO_CORE1_BASELINE_LOG=ON \
  -DNESCO_PALETTE_SNAPSHOT_LOG=OFF \
  -DNESCO_BG_TILE_SHARE_LOG=OFF
cmake --build build-stretch-depth8 --clean-first -j4
```

build後に次を固定確認する。

- ARM EABI5
- banner/version/build ID
- ELF/UF2 SHA-256
- 通常buildと計測buildのsize
- queue symbol `0xb00`
- queue depth参照がすべて定数8を使うこと
- retry hot path 2箇所が100 us、drainが100 us、core1空queue待機が50 usであること
- `git diff --check`

## 実機A/B手順

測定手順と窓選択は、完了済みretry計画で実データに対して成立した規則をそのまま使う。
対象は`LodeRunner.nes`、`Project_DART_V1.0.nes`、`Xevious.nes`である。

1. baseline `1.1.30`を入れる
2. 各ROMを起動し、normalを最低30窓、stretchへ1回切り替えて最低30窓連続で取る
3. candidate `1.1.32`へ入れ替え、同じROM順・同じ手順で取る
4. candidateで3 ROMを短時間プレイし、normal/stretch切替、入力、音、sprite、左端、menu復帰を確認する

窓選択:

- ROM起動後の最初の窓を捨てる
- stretch切替入力を含む窓から連続3窓を捨てる
- `input_events=0`
- `palette_protocol_faults=0`
- mode一致、`[CORE1_BASE]` / `[FRAME_STATS]`対の欠落なし
- `max(frame_us_avg) / min(frame_us_avg) <= 1.015`を満たす最初の10連続窓
- 30窓で得られなければ、そのROM/modeだけ40窓へ延長または取り直す

`frames`や中央値`±0.5%`は窓選択条件に使わない。比較値はselected 10窓の各fieldの中央値とする。

## 比較値

stretch主判定:

- `frame_us_avg`
- `p95_us`

機構診断:

- `lcd_queue_wait_us / frames`
- `lcd_queue_wait_episodes / frames`
- `lcd_queue_wait_count / frames`
- `lcd_queue_wait_us / lcd_queue_wait_episodes`
- `lcd_empty_polls`

normal非退行:

- `frame_us_avg`
- `p95_us`
- `fps_x100`
- `frame_pacing_sleep_us / frames`は補助値

## 採用条件

次をすべて満たせばdepth 8を採用する。

1. stretch 3 ROMのうち2 ROM以上で`frame_us_avg`中央値がbaselineより500 us以上短い
2. stretchのどのROMも`frame_us_avg`と`p95_us`の中央値がbaselineより1%超悪化しない
3. normal 3 ROMの`frame_us_avg`と`p95_us`中央値が16,700 us以下、`fps_x100`中央値が6000以上
4. selected windowすべてで`palette_protocol_faults=0`
5. candidateのqueue symbolが`0xb00`で、実行logがversion `1.1.32`を示す
6. normal/stretchの表示、入力、音、menu復帰に回帰がない

queue waitの減少だけでは採用しない。主目的はframe timeとp95の改善である。
単発の最大値だけでも不採用にせず、selected中央値と再現性で判断する。

## 結果後の分岐

### depth 8を採用した場合

- depth 8を正本にし、それ以上のdepthは自動的に試さない
- stretchが24.58 ms下限から1 ms以内なら、queue/polling系列を終了する
- 1 ms超残っても、次はframe単位window設定を独立候補として測る

### depth 8が不採用の場合

- candidate commitだけをrevertし、`1.1.30`の100 us retryと計測fieldへ戻す
- queue depth、retry幅、通知方式の系列を終了する
- 次はframe単位window設定の専用計測へ進む

frame単位windowはST7365P仕様のData Transfer Pauseにより、画素byte境界でCSXを解除しても
次回CSX active時に中断位置から継続できる。ただし削れるcommand byteは29 window分の
319 byte、62.5 Mbpsで約40.8 us/frameに過ぎないため、depth 8より後に置く。

## 実装前の固定事項

- `1.1.31`を再利用しない
- depthとretry幅を同時に変えない
- depthとframe単位windowを同時に入れない
- queue itemやstrip heightを変更しない
- normalのpacing上限をstretch改善として数えない
- RGB565の24.58 msバス下限がdepth変更では動かないことを明記して結果を読む
- ST7365Pが対応しない`COLMOD=0x63`を実装候補へ戻さない

ST7365P仕様書は
`https://cn.display-lcd.com/data/upload/admin/202503/67e36677ac8c3.pdf`を参照する。
