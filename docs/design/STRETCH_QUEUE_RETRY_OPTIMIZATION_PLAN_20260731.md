# Stretch LCD queue retry optimization plan

作成日: 2026-07-31

対象 branch: `perf/bg-tile-share-log`

実装起点 version: `1.1.29`

関連文書:

- `docs/design/BG_LINE_BUFFER_INDEX_REDESIGN_20260731.md`
- `docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md`
- `docs/project/CONVENTIONS.md`

## 結論

次の実装は2 commitに分ける。

1. `1.1.30`: 既存のframe pacing counterを`[CORE1_BASE]`へ出す計測baseline
2. `1.1.31`: LCD worker queue-full時のretry sleepを`100 us`から`10 us`へ短縮する候補

2つのUF2を先に作り、normal/stretch、3 ROMのA/Bと機能確認を1回の実機作業へまとめる。
queue depth、通知方式、COLMODはこのA/B結果が出るまで実装せず、versionも予約しない。

## 目的

- stretchのframe timeがLCDバス下限より2.6--5.7 ms遅い原因に、
  queue retryの100 us量子化が含まれるかを最小変更で確認する
- normalが60 fps上限へ到達した後も、小さな改善・悪化をpacing余裕から判定できるようにする
- queue depthやCOLMODのような変更範囲の大きい案へ進む前に、安価な候補の効果を分離して測る

## 現在確認できている事実

### stretchの実測

`1.1.29` log `/home/fuyuki/pico_dvl/codex/log/pico20260731_214523.log` の
追加実プレイ区間は次の値だった。固定A/B区間ではないため、次の採否baselineには流用しない。

| ROM | `frame_us_avg`中央値 | queue wait比率中央値 | 1 retryあたり |
|---|---:|---:|---:|
| LodeRunner | 29.02 ms | 37.89% | 100.45 us |
| Project_DART | 30.23 ms | 34.28% | 100.72 us |
| Xevious | 27.22 ms | 48.41% | 100.43 us |

stretch 320x300 RGB565の純転送下限は24.58 msである。現在値との差はLodeRunner 4.44 ms、
Project_DART 5.65 ms、Xevious 2.64 msであり、retry短縮で回収できる余地はある。
ただしqueue wait全体はLCD転送を待つ実時間も含むため、この差を回収できるとは事前に断定しない。

### retry loop

`platform/display.c`のqueue-full retryは2箇所にある。

- LINE itemのenqueue
- scanline 239後の`FRAME_END` itemのenqueue

どちらも失敗するたびに`lcd_queue_wait_count`を1増やし、`sleep_us(100)`を呼ぶ。
したがって現在の`lcd_queue_wait_count`は「queue-fullになったepisode数」ではなく
「失敗したpush試行数」である。`lcd_queue_wait_us / lcd_queue_wait_count`はretry 1回の
平均時間としてだけ読む。

`display_lcd_worker_stop_and_drain()`にも`sleep_us(100)`があるが、これはmode/menu遷移時の
drain待ちでありframe hot pathではない。今回変更しない。

### frame pacing counter

`display_perf_take_window()`は既に`frame_pacing_sleep_us`と
`frame_pacing_sleep_count`を返している。`perf_log_if_due()`は現在この2値を捨てている。

`frame_pacing_sleep_us`が記録するのは`sleep_us()`へ渡した要求時間であり、実際のsleep経過時間を
前後のtimerで測った値ではない。またframe intervalにはaudio ring待ちなども含まれ得る。
従って次式は診断用の**非pacing・非LCD queue時間の推定値**であり、純粋なcore0実働時間の
確定値とは呼ばない。

```text
estimated_nonwait_us_per_frame =
    frame_us_avg
    - lcd_queue_wait_us / frames
    - frame_pacing_sleep_us / frames
```

normalの主判定には式の差より、直接観測できる`frame_pacing_sleep_us / frames`の増減を優先する。

## Phase 0: pacing計測baseline (`1.1.30`)

### source変更

`infones/InfoNES.cpp`の`[CORE1_BASE]`末尾へ次の2 fieldを追加する。

```text
frame_pacing_sleep_us=N frame_pacing_sleep_count=N
```

- 既存fieldの名前と順序は変えない
- `view_mode`の後ろへ追加する
- `display_perf_take_window()`、accumulator、lock、take-and-zeroは変更しない
- hot pathへtimer callを追加しない
- `g_perf_frame_pacing_sleep_us` / `g_perf_frame_pacing_sleep_count`の未使用globalは、
  log出力に使わず削除する。正本は`display_perf_window_t`の値だけとする
- versionを`1.1.30`へ更新する

通常buildと`NESCO_CORE1_BASELINE_LOG=ON` buildを行う。計測artifactは
`build-stretch-retry-baseline/Picocalc_NESco.uf2`として保存し、banner、build ID、
ARM EABI5、size、SHA-256を記録する。

Phase 0だけの実機確認は依頼しない。Phase 1 artifact完成後に同じ作業で測る。

## Phase 1: retry 10 us候補 (`1.1.31`)

### source変更

`platform/display.c`のqueue定義付近へ、意味を限定した定数を1個置く。

```c
DISPLAY_LCD_WORKER_QUEUE_RETRY_US = 10,
```

LINE itemと`FRAME_END` itemのqueue-full loopにある2個の`sleep_us(100)`だけを、
この定数を使う形へ変更する。

変更しないもの:

- `DISPLAY_LCD_WORKER_QUEUE_DEPTH = 4`
- core1が空queueで使う`sleep_us(50)`
- `display_lcd_worker_stop_and_drain()`の`sleep_us(100)`
- queue lock、push/pop、palette snapshot protocol
- LCD packer、DMA、PIO、COLMOD

0 usのbusy pollingは、core0のlock試行増加と消費電力を上限なく増やすため最初の候補にしない。
通知方式は同期設計と遷移時のwake条件が増えるため、10 us候補の結果が必要性を示した場合だけ
別計画にする。

versionを`1.1.31`へ更新する。通常buildと`NESCO_CORE1_BASELINE_LOG=ON` buildを行い、
計測artifactは`build-stretch-retry-10us/Picocalc_NESco.uf2`とする。

## build順序

Phase 0をcommitしてbaseline artifactを作ってからPhase 1へ進む。Phase 1 sourceから
baselineを再生成しない。

```sh
cmake -S . -B build-stretch-retry-baseline \
  -DPICO_SDK_PATH=/home/fuyuki/pico/pico-sdk \
  -DCMAKE_C_COMPILER=/usr/bin/arm-none-eabi-gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/arm-none-eabi-g++ \
  -DCMAKE_ASM_COMPILER=/usr/bin/arm-none-eabi-gcc \
  -DNESCO_CORE1_BASELINE_LOG=ON \
  -DNESCO_PALETTE_SNAPSHOT_LOG=OFF \
  -DNESCO_BG_TILE_SHARE_LOG=OFF
cmake --build build-stretch-retry-baseline --clean-first -j4
```

Phase 1ではdirectory名だけ`build-stretch-retry-10us`へ変えて同じoptionを使う。
各configure前にcompilerと`PICO_SDK_PATH`をcacheで確認し、異なるcacheを再利用しない。

各Phaseで次を確認する。

- 通常buildと計測buildのclean ARM build
- ELF / UF2内のversionとbuild ID
- ARM EABI5、size、SHA-256
- `git diff --check`
- queue item 352 byte、queue depth 4、通常buildの`.bss=97544`を維持する。
  異なる場合はmap / `nm --size-sort`で差を説明してから実機へ進む
- Phase 0ではqueue retryが2箇所とも100 usのまま
- Phase 1ではframe hot pathの2箇所だけが10 us定数を使い、drainの100 usは残る

## 実機A/B手順

対象は`LodeRunner.nes`、`Project_DART_V1.0.nes`、`Xevious.nes`で、baseline 1.1.30、
candidate 1.1.31とも同じ順で行う。タイトル画面を使い、採用区間では操作しない。

各build・各ROMで次を行う。

1. ROMを開始し、最初の`[CORE1_BASE]` / `[FRAME_STATS]`対を遷移窓として捨てる
2. normalで少なくとも12個の連続した対を取る
3. stretchへ1回切り替える
4. 切替入力を含む窓と、その次の最初の全stretch窓を捨てる
5. stretchで少なくとも12個の連続した対を取る
6. selected windowは`input_events=0`、`palette_protocol_faults=0`、mode一致、対欠落なしとする

attract demoの開始位置がROM・buildでずれるため、単純な「開始後N窓」や任意の3窓を使わない。
各modeで、遷移窓を除いた候補から次を満たす**最初の10連続窓**を機械的に選ぶ。

- 10個の`frame_us_avg`がその10個の中央値に対して各`±0.5%`以内
- `max(frame_us_avg) / min(frame_us_avg) <= 1.015`

条件を満たす10連続窓がなければ、そのbuild・ROM・modeだけ取り直す。後ろの都合のよいplateauを
目視で選ばない。比較値は10窓の各fieldの中央値とする。

計測A/B後、candidateだけで3 ROMを短時間プレイし、normal/stretch切替、入力、音、sprite、
画面左端、menu復帰を確認する。この機能確認区間は性能比較へ混ぜない。

## 比較値

stretchの主判定:

- `frame_us_avg`中央値
- `p95_us`中央値

診断値:

- `lcd_queue_wait_us / frames`中央値
- `lcd_queue_wait_count / frames`中央値
- `lcd_queue_wait_us / lcd_queue_wait_count`中央値
- `lcd_empty_polls`中央値

normalの回帰判定:

- `p95_us`中央値
- `frame_pacing_sleep_us / frames`中央値
- `estimated_nonwait_us_per_frame`中央値（診断値のみ）

最大値は記録するが、単発値だけで採否を決めない。

## Phase 1の採用条件

次をすべて満たせば10 us候補を採用する。

1. stretch 3 ROMのうち2 ROM以上で`frame_us_avg`中央値がbaselineより**500 us以上短い**
2. stretchのどのROMも`frame_us_avg`中央値と`p95_us`中央値がbaselineより**1%超悪化しない**
3. candidateの`lcd_queue_wait_us / lcd_queue_wait_count`中央値がbaselineより明確に短く、
   3 ROMとも**30 us以下**である
4. normal 3 ROMの`p95_us`中央値が`16,700 us`以下を維持する
5. normalの`frame_pacing_sleep_us / frames`中央値が、どのROMもbaselineより
   **200 us/frame超減らない**
6. selected windowすべてで`palette_protocol_faults=0`
7. normal/stretchの表示、入力、音、menu復帰に回帰がない

条件1は効果が人間側の実機作業コストに見合う最低線、条件2と5は残り1 ROMとnormalの
非退行条件である。最大値は複数窓で再現した場合だけ原因調査へ加える。

不合格なら結果をHISTORYへ記録し、Phase 1 commitだけを`git revert`する。
Phase 0の計測fieldとversion 1.1.30は残す。

## A/B後の分岐

### 10 usが採用された場合

- stretchのframe timeが24.58 msのバス下限から1 ms以内なら、polling/depthの追加変更を止める
- 1 ms超の差と大きいqueue waitが残る場合だけ、depth 6の独立計画を作る
- depthを試す場合も、depth 4/6だけを変え、今回の10 usを共通baselineにする

### 10 usでframe timeが改善しない場合

- `lcd_queue_wait_us / lcd_queue_wait_count`だけ短く、`lcd_empty_polls`低下やp95悪化がある場合は、
  lock再試行の競合が疑われるため通知方式を別計画として検討できる
- retry時間もframe timeも変わらない場合はpolling系列を打ち切り、depthを自動的には実装しない
- stretchを40.7 fpsのRGB565バス上限より先へ進めるにはCOLMOD 12 bit/pixelが必須なので、
  次の主候補をLCD帯域計画のCOLMODへ移す

## 実装前の固定事項

- Phase 0 / 1を同じcommitへまとめない
- Phase 0の実機確認だけをユーザーへ依頼しない
- 既存`1.1.29`プレイlogを採否baselineにしない
- queue wait比率だけで採用しない。stretchのframe timeとp95を主判定にする
- normalとstretchの採否式を混ぜない
- 実測前にdepth、通知、COLMODを同時実装しない
