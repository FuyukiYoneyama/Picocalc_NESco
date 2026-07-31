# Stretch LCD queue retry optimization plan

作成日: 2026-07-31

対象 branch: `perf/bg-tile-share-log`

実装起点 version: `1.1.29`

関連文書:

- `docs/design/BG_LINE_BUFFER_INDEX_REDESIGN_20260731.md`
- `docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md`
- `docs/project/CONVENTIONS.md`

## 結論

この実験は完了し、`1.1.31`の10 us候補は**不採用**と判定した。

実装は2 commitに分けて行った。

1. `1.1.30`: frame pacing counterとqueue閉塞episode数を`[CORE1_BASE]`へ出す計測baseline
2. `1.1.31`: LCD worker queue-full時のretry sleepを`100 us`から`10 us`へ短縮する候補

2つのUF2を先に作り、normal/stretch、3 ROMのA/Bと機能確認を1回の実機作業へまとめた。
retry 1回は約100.5 usから約10.1 usへ短縮したが、stretchのframe timeは3 ROMとも
500 us以上改善しなかった。Phase 0の計測fieldは残し、Phase 1 commit `8ba265f`だけを
次実装の開始時にrevertする。

## 実装状況（2026-07-31）

Phase 0とPhase 1の実装・build、実機A/B、採否判定まで完了した。

- Phase 0 commit: `1f1c093` (`Add stretch queue retry baseline metrics`)
- Phase 1 commit: `8ba265f` (`Shorten LCD queue retry interval`)
- 両PhaseともARM EABI5の通常buildと計測buildが成功
- 通常buildは両Phaseとも`text=278844 data=0 bss=97548`
- 計測buildは両Phaseとも`text=283776 data=0 bss=97892`
- queue item 352 byte、queue depth 4を維持

| build | version / build ID | ELF SHA-256 | UF2 SHA-256 |
|---|---|---|---|
| Phase 0 normal検証 | `1.1.30` / `Jul 31 2026 22:56:03` | `403a8cb58eac8caa445c9dd83825a3bfb909c0dbec4348ed57b4b490e27c2b8b` | `9770de51e82e9acc07e02bd801ad36735def0b8b8dc85114316ff12096a29428` |
| Phase 0計測baseline | `1.1.30` / `Jul 31 2026 22:56:39` | `21ec7aa8aebfbea2792237306bf1ac3fa3c611c6112c562a72eed9ad56abeae0` | `f4588e39fa0ceb403e1d55f7c3c94765b6ebc2b8322227d2c804959096c04046` |
| Phase 1 normal検証 | `1.1.31` / `Jul 31 2026 22:58:36` | `f22b05d263638d487d7b88de429587d33c2f728d134d16b928e8e57e348e96f5` | `181968f5cecfe093815feb9636f560ba9ad31d7d8ae9c2d33fe866d63d701507` |
| Phase 1計測candidate | `1.1.31` / `Jul 31 2026 22:59:16` | `d3781bbb8f36e6c30048daa5a5a6bd5f814f5e251cb69c50c95972737b4c274b` | `767a914c32add132edaa2a7c2b42fe9fa390a5a128eb0e4375fc5ec3b70ac2bc` |

実機へ渡す計測artifactは次の2つである。

- baseline: `build-stretch-retry-baseline/Picocalc_NESco.uf2`
- candidate: `build-stretch-retry-10us/Picocalc_NESco.uf2`

逆アセンブルでは、baselineのqueue hot path 2箇所が即値100、candidateの同じ2箇所だけが
即値10になっている。drainは両方で100、core1の空queue待機は両方で50を維持している。

## 実機A/B結果（2026-07-31）

使用log:

- baseline `1.1.30`: `/home/fuyuki/pico_dvl/codex/log/pico20260731_230528.log`
- candidate `1.1.31`: `/home/fuyuki/pico_dvl/codex/log/pico20260731_231422.log`

計画どおり、各ROM/modeで適格な最初の10連続窓を機械選択できた。baseline 323対、
candidate 283対、合計606対の全logで`palette_protocol_faults=0`だった。
candidateの実プレイでも表示、入力、音、normal/stretch切替、menu復帰に問題は見られなかった。

### stretch主判定

| ROM | baseline avg | candidate avg | 差 | baseline p95 | candidate p95 | p95差 |
|---|---:|---:|---:|---:|---:|---:|
| LodeRunner | 28,816.0 us | 28,803.5 us | -12.5 us (-0.04%) | 30,309.5 us | 30,231.0 us | -0.26% |
| Project_DART | 29,834.5 us | 29,894.0 us | +59.5 us (+0.20%) | 31,151.0 us | 31,056.0 us | -0.30% |
| Xevious | 26,282.5 us | 26,241.5 us | -41.0 us (-0.16%) | 27,532.0 us | 27,447.5 us | -0.31% |

3 ROMとも採用条件の`-500 us`へ届かず、条件1を満たしたROMは0本だった。
p95悪化や機能回帰はないが、速度効果が実機作業と実装維持に見合わないため不採用とする。

### normal非退行

| ROM | baseline avg | candidate avg | baseline p95 | candidate p95 | pacing余裕差 |
|---|---:|---:|---:|---:|---:|
| LodeRunner | 16,581.0 us | 16,582.0 us | 16,668 us | 16,668 us | +59.7 us/frame |
| Project_DART | 16,575.5 us | 16,576.0 us | 16,668 us | 16,668 us | +65.7 us/frame |
| Xevious | 16,581.0 us | 16,581.5 us | 16,668 us | 16,668 us | +60.3 us/frame |

全ROMが`fps_x100`約6035、平均・p95とも16,700 us以下を維持した。normalの回帰はない。

### 機構上の結論

- `lcd_queue_wait_us / lcd_queue_wait_count`は約100.43--100.57 usから
  約10.09--10.11 usへ短縮し、source変更は実機で効いている
- retry/frameはLodeRunner約111から1112、Project_DART約110から1088、
  Xevious約145から1444へ約10倍になった
- それでもqueue wait/frameはLodeRunner約11.14から11.22 ms、Project_DART約11.02から
  11.00 ms、Xevious約14.59から14.56 msで、実質的に変わらなかった
- episode/frameも約29--31のままで、1 stripごとに閉塞する構造は変わらなかった

従って事前モデルの「各episodeの最後に平均50 us余分にsleepする」という仮定は、
frame timeを説明する主因ではなかった。queue waitの大部分はcore1がLCD DMA完了を待っている
実時間であり、polling量子だけを短くしても転送とproducer/consumerの重なりは改善しない。

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

retry幅を変えても意味が変わらない比較値を得るため、Phase 0で
`lcd_queue_wait_episodes`を追加する。LINE / `FRAME_END`それぞれのloopで
`queue_waited`がfalseからtrueになるときだけ1増やし、1回の連続した閉塞期間を1 episodeと数える。
これにより次を区別できる。

- `lcd_queue_wait_count / frames`: push失敗試行数。10 us化では増えるのが正常
- `lcd_queue_wait_episodes / frames`: 閉塞期間数。retry幅を変えても意味が同じ
- `lcd_queue_wait_count / lcd_queue_wait_episodes`: 1閉塞あたりのretry数
- `lcd_queue_wait_us / lcd_queue_wait_episodes`: 1閉塞あたりの実待ち時間

`display_lcd_worker_stop_and_drain()`にも`sleep_us(100)`があるが、これはmode/menu遷移時の
drain待ちでありframe hot pathではない。今回変更しない。

### 効果の予測

1回の閉塞期間では、最後以外のretryはqueueがまだfullなので必要な待ちである。
queueに空きができた後も寝ている過剰分は最後のretry 1回にだけ生じ、解除時刻がsleep内で
一様なら平均過剰sleepはretry幅の半分になる。

stretchは8 source lineごとに1 stripをflushするため、1 frameは`240 / 8 = 30 strip`である。
`1.1.29`の参考区間では次のretry数だった。

| ROM | retry/frame | 30 stripで割ったretry/strip |
|---|---:|---:|
| LodeRunner | 109.4 | 3.6 |
| Project_DART | 103.8 | 3.5 |
| Xevious | 131.1 | 4.4 |

閉塞が概ねstripごとに1回というモデルと整合する。従って机上期待値は次になる。

```text
100 us retryの平均過剰sleep = 30 episodes/frame * 50 us  = 1,500 us/frame
 10 us retryの平均過剰sleep = 30 episodes/frame *  5 us  =   150 us/frame
期待回収量                                         = 1,350 us/frame
```

採用閾値500 usは期待値の約37%である。実測が約1.35 msから大きく外れた場合は、
`lcd_queue_wait_episodes`、retry/episode、`lcd_empty_polls`から、閉塞回数モデルとlock競合の
どちらが外れたかを調べる。retry回数へ90 usを掛けて効果を見積もってはならない。

実効retry量子を`q us`とすると、同じモデルでの回収量は次になる。

```text
回収量 ≒ 30 episodes/frame * (100 - q) / 2
回収量 >= 500 us/frame となる条件: q <= 66.7 us
```

従って`10 us`要求の実効値がalarm登録などのoverheadで40--60 usになっても、
600--900 us/frameの回収が見込め、実験は成立する。`lcd_queue_wait_us / lcd_queue_wait_count`
へ30 usの上限を課すと、frame timeを500 us以上改善した候補を誤って不採用にし得る。
この値は定数変更と実効量子を確認する診断値に留め、採否はframe timeと非退行条件で決める。

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

normalのpacing余裕を記録するときは式の差より、直接観測できる
`frame_pacing_sleep_us / frames`の増減を優先する。ただしretry短縮ではこの値が増えるのが自然なので、
Phase 1のnormal非退行主判定は`frame_us_avg`、p95、fpsとし、pacing sleepは補助確認にする。

## Phase 0: pacing計測baseline (`1.1.30`)

### source変更

`[CORE1_BASE]`末尾へ次の3 fieldを追加する。

```text
lcd_queue_wait_episodes=N frame_pacing_sleep_us=N frame_pacing_sleep_count=N
```

- 既存fieldの名前と順序は変えない
- `view_mode`の後ろへ追加する
- `platform/display.c`へcore0専用の`uint32_t s_perf_lcd_queue_wait_episodes`を追加する
- LINE / `FRAME_END`の両loopで、最初に`queue_waited=true`へ変える箇所だけepisodeを1増やす
- `display_perf_window_t`へ`lcd_queue_wait_episodes`を追加し、`display_perf_reset()`と
  `display_perf_take_window()`で他のcore0 counterと同じくlockなしでtake-and-zeroする
- core1 handoff、queue lock、既存counterの意味は変更しない
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

使用中のpico-sdkでは`PICO_TIME_DEFAULT_ALARM_POOL_DISABLED=0`、
`PICO_TIME_SLEEP_OVERHEAD_ADJUST_US=6`がdefaultである。実際の呼び出し経路は
`sleep_us()`の`#if !PICO_TIME_DEFAULT_ALARM_POOL_DISABLED`側から`sleep_until()`へ進む。
alarm pool無効時の`#else`側はこのprojectの経路ではない。

`sleep_until()`は目標時刻の6 us前を`t_before`とし、まだ未来なら`add_alarm_at()`でalarmを登録して
WFE loopへ入り、最後に無条件で`busy_wait_until()`を呼ぶ。従って10 us要求では約4 us先のalarmを
retryごとに登録し、最後の6 usは必ずbusy waitになる。関数・spin lock・alarm pool挿入・hardware
timer設定のoverheadが要求時間と同程度になり、10 usの大半がbusy waitになる可能性がある。
ただし末尾の`busy_wait_until()`はbaselineの100 us要求にも同じく存在するため、A/Bで新たに加わる
処理ではない。10 us候補は0 us pollingより上限があるものの、完全な低消費電力sleepではなく、
retry数とqueue lock取得試行は約10倍へ増え得る。

実装時に、使用するSDKの`src/common/pico_time/time.c`にある`sleep_us()`と`sleep_until()`、
および生成configで上記経路を再確認する。実機では
`lcd_queue_wait_us / lcd_queue_wait_count`のbaselineからの短縮量、`lcd_empty_polls`、p95で、
実効量子とlock競合の影響を診断する。実効量子へ30 usの採用上限は置かない。

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
- queue item 352 byte、queue depth 4を維持する
- episode counter 4 byteの追加により、通常buildの`.bss`期待値を`97544 -> 97548`とする。
  異なる場合はmap / `nm --size-sort`で差を説明してから実機へ進む
- 削除対象の`g_perf_frame_pacing_sleep_us` / `g_perf_frame_pacing_sleep_count`は無名namespace内の
  未使用globalで、現行ELFでは既に除去されている。削除しても`.bss`は減らず、上の差はepisode
  counterの`+4 byte`だけである
- Phase 0ではqueue retryが2箇所とも100 usのまま
- Phase 1ではframe hot pathの2箇所だけが10 us定数を使い、drainの100 usは残る

## 実機A/B手順

対象は`LodeRunner.nes`、`Project_DART_V1.0.nes`、`Xevious.nes`で、baseline 1.1.30、
candidate 1.1.31とも同じ順で行う。タイトル画面を使い、採用区間では操作しない。

各build・各ROMで次を行う。

1. ROMを開始し、最初の`[CORE1_BASE]` / `[FRAME_STATS]`対を遷移窓として捨てる
2. normalで少なくとも30個の連続した対を取る
3. stretchへ1回切り替える
4. 切替入力を含む窓を1窓目として、切替後の連続3窓を捨てる
5. その後、stretchで少なくとも30個の連続した対を取る
6. selected windowは`input_events=0`、`palette_protocol_faults=0`、mode一致、対欠落なしとする

最低30窓は既存logへ`max/min <= 1.015`を適用した参考結果から安全側に固定した。
normal行は連続測定から得た値だが、stretch行は既存の短いstretch episodeを連結して得た参考値であり、
stretchを30窓連続で無操作測定する今回の条件はまだ実行されていない。既存logで最初の安定10窓を
含めるために必要だった総窓数は次のとおりである。

| 測定 | 安定10窓の開始 | 必要だった総窓数 |
|---|---:|---:|
| LodeRunner normal | w2 | 11 |
| Project_DART normal | w11 | 20 |
| Xevious normal | w2 | 11 |
| LodeRunner stretch | w2 | 11 |
| Project_DART stretch | w16 | 25 |
| Xevious stretch | w5 | 14 |

stretch行は連続30窓で再確認した値ではないため、必要窓数の保証には使わない。特に
Project_DART stretchは参考値でも25窓を必要とし、30窓に対する余裕が5窓しかない。
30窓に安定10窓がなければ異常と決めず、まず同じ測定を40窓まで延長または取り直す。

切替後3窓の固定破棄はmode遷移処理を性能比較から遠ざけるための保守的な余白として残す。
以前根拠にした`frame_us_avg=21,947 us`は安定したstretch窓ではなくmode遷移中の窓であり、
「stretch切替後3窓目」という帰属は誤りだった。既存logで確認したmode遷移窓は
`input_events != 0`であり、selected windowの既存条件で除外される。

attract demoの開始位置がROM・buildでずれるため、単純な「開始後N窓」や任意の3窓を使わない。
次をすべて満たす窓だけを候補とし、不適格な窓は連続性を切る。その候補から条件を満たす
**最初の10連続窓**を機械的に選ぶ。

- `input_events=0`
- `palette_protocol_faults=0`
- mode一致、`[CORE1_BASE]` / `[FRAME_STATS]`対の欠落なし
- `max(frame_us_avg) / min(frame_us_avg) <= 1.015`

`frames`の中央値や分布は診断値として記録するが、selected windowの適格条件には使わない。
窓の実時間がほぼ一定なら`frames`は`frame_us_avg`とほぼ同じ情報であり、30窓全体の中央値から
狭く切ると、安定域とattract demo域をまたいだXevious normalのような二峰分布で両方を誤って
排除し得るためである。実際、`1.1.28` Xevious normalでは最初の12窓の`frames=61`安定域が、
30窓全体の中央値59.5に対する`±1`条件ではすべて除外される。

中央値からの`±0.5%`条件は使わない。Project_DART normalは約20,044 usと20,185 usの
2値振動を持ち、全体のmax/minは約1.007でも片方が中央値から0.5%を超えるためである。
実ログではmax/minだけならDART normalは安定後に合格する。

30窓の中に条件を満たす10連続窓がなければ、そのbuild・ROM・modeだけ40窓へ延長または取り直す。
後ろの都合のよいplateauを目視で選ばない。比較値は選ばれた10窓の各fieldの中央値とする。

計測A/B後、candidateだけで3 ROMを短時間プレイし、normal/stretch切替、入力、音、sprite、
画面左端、menu復帰を確認する。この機能確認区間は性能比較へ混ぜない。

## 比較値

stretchの主判定:

- `frame_us_avg`中央値
- `p95_us`中央値

診断値:

- `lcd_queue_wait_us / frames`中央値
- `lcd_queue_wait_episodes / frames`中央値
- `lcd_queue_wait_count / frames`中央値
- `lcd_queue_wait_count / lcd_queue_wait_episodes`中央値
- `lcd_queue_wait_us / lcd_queue_wait_episodes`中央値
- `lcd_queue_wait_us / lcd_queue_wait_count`中央値
- `lcd_empty_polls`中央値
- `frames`中央値と分布（診断値のみ。窓選択には使わない）

normalの回帰判定:

- `p95_us`中央値
- `frame_pacing_sleep_us / frames`中央値
- `estimated_nonwait_us_per_frame`中央値（診断値のみ）

最大値は記録するが、単発値だけで採否を決めない。

## Phase 1の採用条件

次をすべて満たせば10 us候補を採用する。

1. stretch 3 ROMのうち2 ROM以上で`frame_us_avg`中央値がbaselineより**500 us以上短い**
2. stretchのどのROMも`frame_us_avg`中央値と`p95_us`中央値がbaselineより**1%超悪化しない**
3. normal 3 ROMの`frame_us_avg`中央値と`p95_us`中央値が`16,700 us`以下、
   `fps_x100`中央値が`6000`以上を維持する
4. normalの`frame_pacing_sleep_us / frames`中央値が、どのROMもbaselineより
   **200 us/frame超減らない**
5. selected windowすべてで`palette_protocol_faults=0`
6. normal/stretchの表示、入力、音、menu復帰に回帰がない

条件1は約1.35 msの期待回収量に対して、効果が人間側の実機作業コストに見合う最低線である。
条件2と3が残り1 ROMとnormalの実質的な非退行条件になる。retry短縮ではnormalのqueue waitが
pacing sleepへ移るため、条件4は通常自動的に通る補助的な整合確認であり、core0の小さな劣化を
単独で検出する安全網とは扱わない。最大値は複数窓で再現した場合だけ原因調査へ加える。

`lcd_queue_wait_us / lcd_queue_wait_count`は採用条件ではない。candidateでbaselineより明確に短いかを
build健全性として確認し、実効量子が30 usを超えても、それだけでは不採用にしない。約67 usを超えた
場合は500 us回収モデルとの不整合を調査するが、最終判断は条件1--6の実測結果で行う。

不合格なら結果をHISTORYへ記録し、Phase 1 commitだけを`git revert`する。
Phase 0の計測fieldとversion 1.1.30は残す。

## A/B後の分岐

実測結果は「10 usでframe timeが改善しない場合」に確定した。

- retry時間だけが短くなり、frame timeとqueue waitは変わらなかったため、polling幅と通知方式の
  系列を打ち切る
- ST7365P仕様では`COLMOD=0x63`は12 bit/pixelを意味せず未定義なので、COLMOD候補も破棄する
- 次は`docs/design/STRETCH_QUEUE_DEPTH_OPTIMIZATION_PLAN_20260731.md`を正本として、
  depth 4のDMA waitを直接測る。strip間bus idle仮説が支持された場合だけdepth 8を独立A/Bする

## 実装前の固定事項

- Phase 0 / 1を同じcommitへまとめない
- Phase 0の実機確認だけをユーザーへ依頼しない
- 既存`1.1.29`プレイlogを採否baselineにしない
- queue wait比率だけで採用しない。stretchのframe timeとp95を主判定にする
- normalとstretchの採否式を混ぜない
- 実測前にdepth、通知、COLMODを同時実装しない
