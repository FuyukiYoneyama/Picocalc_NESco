# Stretch LCD worker queue depth optimization plan

作成日: 2026-07-31

更新日: 2026-08-01

対象 branch: `perf/bg-tile-share-log`

既存性能baseline: `1.1.30`

Phase 0診断version: `1.1.32`

Phase 1候補version: `1.1.33`

状態: **完了。Phase 0はdepth 8仮説を不支持とし、Phase 1は実装しない。**

関連文書:

- `docs/design/STRETCH_QUEUE_RETRY_OPTIMIZATION_PLAN_20260731.md`
- `docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md`
- `docs/project/CONVENTIONS.md`

## 結論

depth 8を先に実装しない。まずdepth 4のまま、strip flush時にcore1が前のLCD DMAを
実際にどれだけ待っているかを計測する。

1. `1.1.31`の不採用10 us retryだけをrevertし、`1.1.30`相当へ戻す
2. `1.1.32`でDMA waitとwindow設定時間を計測する。queue depthは4のまま
3. 計測がstrip間のLCD idle仮説を支持した場合だけ、`1.1.33`でdepthを4から8へ増やす

既存`1.1.30`性能logは再取得しない。新しい`1.1.32`実機作業は、既存logにない
`lcd_dma_wait_*`と`lcd_window_set_*`を取る診断作業である。

## 実測結果 (2026-08-01)

実機logは`/home/fuyuki/pico_dvl/codex/log/pico20260801_075307.log`である。
3 ROMのnormal/stretchを各30窓以上取得し、入力を含む窓と直後3窓を除いた最初の安定10窓を選定した。
全選定窓で`palette_protocol_faults=0`、DMA wait / window設定のcountは30.0回/frameだった。

| stretch ROM | `frame_us_avg` | DMA wait / frame | window設定 / frame | `removable_window_us` | `depth_recovery_upper_us` |
|---|---:|---:|---:|---:|---:|
| LodeRunner | 28,888.5 us | 14,900.9 us | 91.3 us | 88.3 us | 4,221.2 us |
| Project_DART | 30,051.5 us | 14,852.4 us | 116.5 us | 112.6 us | 5,359.0 us |
| Xevious | 26,371.0 us | 16,177.5 us | 91.2 us | 88.2 us | 1,703.8 us |

3 ROMすべてのDMA waitが`5,000 us/frame`を大幅に超えた。core1は次stripを準備済みのまま
前DMAの完了を待っており、queue depthを4から8へ増やしても次DMAの開始時刻は早まらない。
従って**Phase 1 / version `1.1.33`は実装しない**。depth、retry、通知の最適化系列はここで終了する。

window設定の削減上限も最大112.6 us/frameで250 us/frame gate未満だったため、frame単位window設定は
後続候補にしない。`depth_recovery_upper_us`は非pixel時間から得る上限に過ぎず、DMA wait直接計測により
その大部分がcore1到着遅れではないことを確認した。

診断buildのstretch `frame_us_avg`は既存`1.1.30`比較値に対し`+0.25% / +0.73% / +0.34%`で、
いずれも1%未満だった。一方normalでは`p95_us`が約16,668 usのまま各1秒窓に約10 msの単発maxが入った。
追加した長い`[CORE1_BASE]`のserial logging負荷と判断し、診断buildを通常性能のA/B baselineには使わない。
Phase 1は不実施なので、この制約はdepth候補の比較へ影響しない。

## retry実験から確定したこと

`1.1.30`と`1.1.31`の実機A/Bでは、retry 1回が約100.5 usから約10.1 usへ短縮し、
retry回数は約10倍になった。一方、stretchのqueue wait/frameとframe timeはほぼ変わらなかった。

従って100 us polling量子は主な損失原因ではなかった。ただしcore0側のqueue counterだけでは、
次の2状態を区別できない。

- core1が次stripを組み終え、前stripのDMA完了を待っている
- 前stripのDMAは終わっているが、core1へ次stripが届かずLCD busがidleになっている

旧計画の「queue waitの大部分はLCD DMA完了を実際に待つ時間」という記述は、retry結果からの
事後推論であり、直接計測ではなかった。Phase 0でこの不足を解消する。

## depth 8仮説

workerは8 source lineをpackするたびに1 stripをDMAへ渡す。

- normal: `256 x 8 x 2 = 4,096 byte`、純転送約524 us/strip
- stretch: `320 x 10 x 2 = 6,400 byte`、純転送約819 us/strip
- 1 frame: `240 / 8 = 30 strip`

現在のdepth 4では、core1が前stripのDMA完了を待つ間にqueueへ保持できるのは次stripの半分である。
depth 8なら次strip 1個分を保持できる。ただし、core1が既にstripを組み終えてDMA完了を長く待って
いるなら、queueを深くしても次DMAの開始時刻は早まらない。

depth変更はLCDの純転送下限を変えない。stretch RGB565の下限は24.58 ms/frameのままである。

## 既存baselineの流用

性能baselineは次を流用する。

- version/build ID: `1.1.30` / `Jul 31 2026 22:56:39`
- artifact: `build-stretch-retry-baseline/Picocalc_NESco.uf2`
- UF2 SHA-256: `f4588e39fa0ceb403e1d55f7c3c94765b6ebc2b8322227d2c804959096c04046`
- log: `/home/fuyuki/pico_dvl/codex/log/old/pico20260731_230528.log`

このlogにはLodeRunner、Project_DART、Xeviousのnormal/stretchが各30窓を超えて含まれ、
選定済みのstretch中央値は28,816.0 / 29,834.5 / 26,282.5 usである。

commit `8ba265f`が変更したsourceは`platform/display.c`と`platform/version.h`だけで、
後続`8226cbf`と`4055734`は文書変更だけである。revert直後に次を確認する。

```sh
git diff --exit-code 1f1c093 -- platform/display.c platform/version.h
```

差が0ならsourceは既存baselineと同一である。再buildしたUF2のSHA-256一致は要求しない。
`PICOCALC_NESCO_BUILD_ID`が`__DATE__ " " __TIME__`なので、同一sourceでも再build日時により
ELF/UF2 hashは変わるためである。旧artifact自身のhashは上記既知値との一致を確認する。

## Phase 0: depth 4 bus診断 (`1.1.32`)

### 前提復元

1. `git revert 8ba265f`で10 us retry変更だけを戻す
2. 上記`git diff --exit-code`で`1f1c093`とのsource一致を確認する
3. hot path 2箇所のretryが100 usであることを確認する
4. `lcd_queue_wait_episodes`とpacing fieldが残っていることを確認する

Phase 0計測commit `1f1c093`はrevertしない。`1.1.31`は不採用実験の識別versionとして再利用しない。

### 追加counter

`NESCO_CORE1_BASELINE_LOG=ON`のときだけ、
`display_lcd_worker_flush_normal_strip()`と`display_lcd_worker_flush_stretch_strip()`で次を測る。

```text
lcd_dma_wait_us
lcd_dma_wait_count
lcd_window_set_us
lcd_window_set_count
```

計測区間:

```c
t0 = time_us_64();
lcd_dma_wait();
dma_wait_us += time_us_64() - t0;
dma_wait_count++;

t0 = time_us_64();
lcd_set_window(...);
window_set_us += time_us_64() - t0;
window_set_count++;
```

- timer callは4回/strip、120回/frameである
- `FRAME_END`の最終DMA完了待ちはstrip間idle仮説の対象外なので、このcounterへ含めない
- 最初のstripにあるactive DMAなしの`lcd_dma_wait()`はcountへ含める。正常なら約30 count/frameになる
- window計測は11 command/data byteと5回の`lcd_wait_idle()`、GPIO/関数overheadをまとめて含む
- drain 1回を分離せず、frame window化で実際に削除できる`lcd_set_window()`全体を直接測る

### handoffとlog契約

既存`display_lcd_worker_core1_window_t`のlocal/published機構を使う。

- core1 localへ加算する
- `FRAME_END`処理時に既存queue lock下でpublishedへ加算し、localをzeroにする
- core0の`display_perf_take_window()`は同じlock下でtake-and-zeroする
- `[CORE1_BASE]`の既存field順を変えず、末尾へ4 fieldを追加する

```text
lcd_dma_wait_us=N lcd_dma_wait_count=N lcd_window_set_us=N lcd_window_set_count=N
```

`display_lcd_worker_core1_window_t`は64 bit値を先に置いて40 byteに固定し、
`static_assert(sizeof(display_lcd_worker_core1_window_t) == 40)`を置く。現行12 byteから
local/published各28 byte、計測buildの`.bss`は合計56 byte増える。

### versionとRAM

- versionを`1.1.32`へ更新する
- queue depthは4、queue symbolは`0x580`のまま
- 通常buildは計測codeが無効なので`.bss=97548`を維持する
- 計測buildは`.bss=97892 + 56 = 97948`を期待値とする

期待値と異なる場合はmapまたは`arm-none-eabi-nm -S --size-sort`で差を説明する。

### build

通常buildと診断buildを作る。実機へ渡すのは診断buildである。

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

cmake -S . -B build-stretch-depth-diagnostic \
  -DPICO_SDK_PATH=/home/fuyuki/pico/pico-sdk \
  -DCMAKE_C_COMPILER=/usr/bin/arm-none-eabi-gcc \
  -DCMAKE_CXX_COMPILER=/usr/bin/arm-none-eabi-g++ \
  -DCMAKE_ASM_COMPILER=/usr/bin/arm-none-eabi-gcc \
  -DNESCO_CORE1_BASELINE_LOG=ON \
  -DNESCO_PALETTE_SNAPSHOT_LOG=OFF \
  -DNESCO_BG_TILE_SHARE_LOG=OFF
cmake --build build-stretch-depth-diagnostic --clean-first -j4
```

build後にARM EABI5、version/build ID、size、ELF/UF2 SHA-256、queue symbol、log format、
retry 100 us、`git diff --check`を確認する。

## Phase 0実機診断

既存`1.1.30`を再度flash・測定しない。`1.1.32`診断buildだけで、3 ROMを同じ順に測る。

1. ROM起動後の最初の窓を捨てる
2. normalを最低30窓取る
3. stretchへ1回切り替え、切替入力を含む窓から3窓捨てる
4. stretchを最低30窓取る
5. 30窓で安定10窓が得られないROM/modeだけ40窓へ延長する

窓選択は次の条件を満たす最初の10連続窓とする。

- `input_events=0`
- `palette_protocol_faults=0`
- mode一致、`[CORE1_BASE]` / `[FRAME_STATS]`対の欠落なし
- `max(frame_us_avg) / min(frame_us_avg) <= 1.015`

`frames`と中央値`±0.5%`は選択条件に使わない。

### 診断値

selected窓ごとに次を計算し、ROM/modeごとの中央値を取る。

```text
dma_wait_us_per_frame     = lcd_dma_wait_us / frames
dma_wait_us_per_flush     = lcd_dma_wait_us / lcd_dma_wait_count
dma_wait_count_per_frame  = lcd_dma_wait_count / frames
window_set_us_per_frame   = lcd_window_set_us / frames
window_set_count_per_frame= lcd_window_set_count / frames
removable_window_us       = window_set_us_per_frame * 29 / 30
stretch_nonpixel_us       = frame_us_avg - 24,576
depth_recovery_upper_us   = max(0, stretch_nonpixel_us - window_set_us_per_frame)
```

`dma_wait_count_per_frame`と`window_set_count_per_frame`は約30を期待する。

`24,576 us`はstretch 1 frameのpixel DMA時間`192,000 byte x 128 ns`である。
定常状態ではbounded queueによりcore0のframe生成率とLCDの消費率が長期平均で一致するため、
`stretch_nonpixel_us`はpixel送出以外に使われたframe時間の近似値になる。

ただしこれは厳密なbus idleだけではない。window command byte、`lcd_wait_idle()`、GPIO、DMA開始処理、
packer、frame境界、core0/core1の到着ずれを含む。従って`depth_recovery_upper_us`はdepth 8の
**回収可能量の上限**であり、期待改善量や保証値ではない。depth 8が回収できるのは、この上限のうち
queue先行保持不足に由来する部分だけである。

既存`1.1.30`の参考値では、stretchのnonpixel時間は次になる。Phase 0では`1.1.32`の同一selected窓で
`window_set_us_per_frame`を差し引き、ROMごとの上限を確定する。

| ROM | `frame_us_avg` | `stretch_nonpixel_us` | 1 stripあたり |
|---|---:|---:|---:|
| LodeRunner | 28,816.0 us | 4,240 us | 141 us |
| Project_DART | 29,834.5 us | 5,258.5 us | 175 us |
| Xevious | 26,282.5 us | 1,706.5 us | 57 us |

計測整合性は次で確認する。

- `window_set_us_per_frame`が`stretch_nonpixel_us`を測定誤差以上に超えたら、計測位置または
  frame/LCD throughput対応の仮定が崩れているためPhase 1へ進まない
- window command列はnormal/stretchで同じ11 byte x 30回なので、`window_set_us_per_frame`は
  両modeで概ね同じ値を期待する。10%超の差が継続する場合は原因を調べる
- `lcd_empty_polls * 50 us / frames`はqueue枯渇sleepの参考値として上限と桁を比較するが、
  一致条件にはしない。空queue sleepはpixel DMA実行中にも起こり得るためである

### depth 8実装gate

- `depth_recovery_upper_us >= 500 us`のROMが2本未満なら、Phase 1の採用条件を構造上満たせないため
  depth 8を実装しない
- stretch 3 ROMすべてで`dma_wait_us_per_frame >= 5,000 us`なら、core1は各frameで長時間、
  次stripを準備済みのまま前DMAを待っている。depth 8仮説を不支持としてPhase 1を実装しない
- stretch 3 ROMのうち2 ROM以上で`dma_wait_us_per_frame <= 1,000 us`かつ上限500 us以上なら、
  前DMA完了後のcore1到着遅れを減らす余地があるためPhase 1へ進む
- 1,000--5,000 usの中間またはROM間で分かれても、上限500 us以上が2本あればPhase 1へ進む。
  この場合はROMごとの`depth_recovery_upper_us`を上限予測として先に記録し、回収率未知のA/Bとする
- `1.1.32`のframe time/p95が既存`1.1.30`より1%超悪化した場合、timer計測負荷を調査し、
  診断buildをそのままA/B baselineに使わない

5,000/1,000 usはframe改善の予測値ではなく、明確なバス待ちとほぼ無待ちを分離する診断境界である。
Phase 0から算出する`depth_recovery_upper_us`も上限であり、depth 8の改善量そのものとは扱わない。

### frame window候補のgate

`removable_window_us`は、30回のwindow設定を1回へ減らした場合の直接的な上限見積もりである。

- 2 ROM以上で250 us/frame以上なら、depth結果後にframe windowを独立計画する価値がある
- 250 us/frame未満なら、人間側の実機検証コストに見合わない候補として保留する

この値には既知のcommand byte削減約40.8 us/frameと、未計測だったdrain/GPIO/関数費用が含まれる。
計測用timer call自身のoverheadもわずかに含むため、厳密な保証値ではなく安全側の上限として読む。

## Phase 1: depth 8候補 (`1.1.33`、gate通過時のみ)

Phase 0と同じ計測を残したまま、次の2点だけを変更する。

1. `DISPLAY_LCD_WORKER_QUEUE_DEPTH = 4`を`8`へ変更する
2. versionを`1.1.33`へ更新する

retry、strip height、queue item、push/pop、lock、palette protocol、packer、DMA、PIO、window、
COLMOD、fallback経路は変更しない。

### RAMとheap

```text
queue: 352 * 4 = 1,408 byte (0x580)
       352 * 8 = 2,816 byte (0xb00)
増分:             1,408 byte
通常build .bss: 97,548 -> 98,956
計測build .bss: 97,948 -> 99,356
```

historicalなheap gap 122,328 byteからstatic 1,408 byteを引くと120,920 byte残る。
Project_DARTで使うMapper30の最大動的確保はoverlay 32,768 + CHR RAM 32,768 + flash ID 8,192 =
73,728 byteで、差引47,192 byte残る。screenshot chunk bufferは10,240 + 15,360 = 25,600 byteなので、
Mapper30確保中に同時確保しても単純計算で21,592 byte残る。

従って1,408 byte増分はMapper30起動とscreenshotを直ちに壊す規模ではない。ただしcandidate実機では
Project_DART起動、screenshot保存成功、menu復帰を機能条件として確認する。

### artifact

計測artifactは`build-stretch-depth8/Picocalc_NESco.uf2`とする。Phase 0と同じbuild optionを使い、
queue symbol `0xb00`、通常/計測`.bss`、version/build ID、hashを確認する。

## Phase 1実機A/B

Phase 0の`1.1.32`診断logをdepth 4 baselineとして流用する。baselineを再測定しない。
candidate `1.1.33`だけを同じROM順・同じ手順で測り、同一counterを比較する。

stretch主判定:

- `frame_us_avg`
- `p95_us`
- `dma_wait_us_per_frame`
- `lcd_queue_wait_us / frames`

normal非退行:

- `frame_us_avg <= 16,700 us`
- `p95_us <= 16,700 us`
- `fps_x100 >= 6000`
- pacing sleep/frameは補助値

採用条件:

1. stretch 3 ROMのうち2 ROM以上で`frame_us_avg`中央値が500 us以上短い
2. stretchのどのROMも`frame_us_avg`と`p95_us`が1%超悪化しない
3. normal 3 ROMが上記60 fps非退行条件を維持する
4. selected windowすべてでpalette protocol fault 0
5. queue symbol `0xb00`、version `1.1.33`、counter約30回/frameが確認できる
6. 3 ROMの表示、入力、音、normal/stretch切替、menu復帰に回帰がない
7. Project_DART起動とscreenshot保存が成功する

queue waitやDMA waitの減少だけでは採用しない。frame timeとp95を主判定にする。

## 結果後の分岐

- depth 8採用時も、それ以上のdepthは自動的に試さない
- depth 8不採用時はcandidateだけをrevertし、queue/retry/通知系列を終了する
- frame windowは40.8 usのcommand byteだけで1 ms残差を埋める候補ではない
- frame windowへ進むかはPhase 0で測った`removable_window_us`が250 us/frame以上かで判断する
- ST7365Pが対応しない`COLMOD=0x63`を候補へ戻さない

ST7365P仕様書は
`https://cn.display-lcd.com/data/upload/admin/202503/67e36677ac8c3.pdf`を参照する。
