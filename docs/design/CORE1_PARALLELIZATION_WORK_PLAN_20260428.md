# Core1 parallelization work plan 2026-04-28

## 目的

RP2040 の core1 を使って、Picocalc_NESco の次の 2 点を改善できるか検証する。

1. フレームレート、特に LCD 転送や stretch 表示時の余力を増やせるか。
2. 入力応答、保存中や描画中の待ち、操作感などの UX を改善できるか。

この計画では、NES CPU / PPU / APU / mapper の内部同期を core 間で分割しない。
初期段階では、NES emulation 本体は core0 に残し、core1 は入力 polling または LCD 後処理を担当する補助 core として扱う。

## 現在確認できている前提

この節は、本計画を進めるうえで確認済みの前提をまとめる。
計画作成時点では core0 のみで動作していたが、2026-04-28 の Phase 0 から Phase 1C までの作業で、
core1 keyboard polling までは実装済みになっている。

- `debug/dart-keyboard-phase1c` の `f0d19dd` を、Phase 1C 完了時点の基準 commit とする。
- `CMakeLists.txt` には `pico_multicore` が追加済みである。
- core1 worker は firmware 起動後に 1 回だけ起動し、service bit で処理を切り替える。
- game 中の keyboard polling は core1 が担当し、ROM menu 中は service bit を落として core0 direct I2C に戻す。
- core1 keyboard polling は、1ms polling と FIFO drain の元実装を基準にする。
- flash erase / program を行う処理は、core1 実行中に `multicore_lockout` で保護する必要がある。
- `platform/input.c` では `InfoNES_PadState()` が VBlank 時に `input_poll()` を呼び、I2C keyboard FIFO を読む。
- `platform/display.c` では `InfoNES_PreDrawLine()` / `InfoNES_PostDrawLine()` が 1 scanline ごとの描画後処理を担当している。
- `platform/display.c` の NES line buffer は `s_line_buffer[256]` の単一 buffer である。
- `InfoNES_PostDrawLine()` は normal / stretch pack、LCD DMA wait、LCD DMA kick に関わる。

したがって、core1 LCD worker 化には line buffer queue または strip buffer queue が必要になる。
単一の `s_line_buffer[256]` をそのまま core1 に渡す実装は禁止する。

## 基本方針

作業は UX 改善と FPS 改善を分けて進める。

最初に core1 keyboard polling を試す。
これは FPS を直接上げる施策ではないが、NES emulation と LCD 描画が重い場面でも入力を取り込める可能性があり、UX 改善として効果を測りやすい。

LCD worker 化はその後に行う。
LCD worker はフレームレート改善の本命だが、DMA、viewport、fullscreen UI、screenshot、BMP viewer、ROM menu との整合性が必要なので、入力 polling よりリスクが高い。

CPU / PPU / APU / mapper を core0 / core1 に分ける案は、初期計画では非目標にする。

## 非目標

- CPU core と PPU core を分けない。
- APU register state や mapper HSync を core1 へ移さない。
- ROM menu、help、BMP viewer、screenshot などの fullscreen UI を core1 LCD worker に直接描かせない。
- Phase 1 では keyboard worker だけを入れる。
- Phase 3 以降で LCD worker を試す場合も、keyboard worker を維持する。
- 最初から release 版へ入れる前提にしない。実験 branch で段階的に確認する。

## 作業 branch 方針

初期作業は `main` から専用 branch を切って行う。

推奨:

- UX 入力実験: `feature/core1-keyboard-polling`
- LCD worker 実験: `feature/core1-lcd-worker`

Phase 0 から Phase 1C までの keyboard polling 作業は
`debug/dart-keyboard-phase1c`
で実施済みである。
Phase 1C の内容を `main` に取り込んだ後、LCD worker 実験は新しい branch で開始する。

keyboard polling と LCD worker は、可能なら別 branch / 別 commit 系列に分ける。
理由は、入力改善だけ採用して LCD worker は見送る、という判断を可能にするため。

## 詳細計画の扱い

この文書は core1 活用全体の進め方だけでなく、
実装に必要な詳細作業も同じ文書内で管理する。
別の詳細計画書は原則として作らない。

理由:

- core1 化は input、I2C、LCD、DMA、ROM menu、screenshot にまたがるため、
  計画が複数文書に分かれると前提の食い違いが起きやすい。
- build / commit / 実機確認の切れ目を 1 箇所で管理した方が、
  手戻りしやすい。
- Phase 0 の計測結果と Phase 1 以降の採用判断を同じ文脈で追える。

このため、実装前に必要な詳細化は本書へ追記する。

本書内で詳細化する項目:

1. Phase 0 baseline 計測項目と log 形式
2. Phase 1A / 1B / 1C の `core1 keyboard polling` 詳細作業手順
3. Phase 3 / Phase 4 に進む場合の LCD worker 実験手順

core1 機能としての初回実装は `core1 keyboard polling` から始める。
ただし、その前に Phase 0 baseline 計測を行い、比較可能な数字を残す。

## Phase 0: baseline 計測

### 目的

core1 化の前に、現在どこで時間を使っているかを測る。
この phase では挙動を変えない。

### 実装内容

Phase 0 baseline log は次の形式に固定する。

- 有効化 macro: `NESCO_CORE1_BASELINE_LOG`
- 出力間隔: 1 秒ごと
- 出力先: UART runtime log
- 出力 prefix: `[CORE1_BASE]`

出力項目:

- `t_us`
- `frames`
- `fps_x100`
- `frame_us_avg`
- `frame_us_max`
- `lcd_wait_us`
- `lcd_flush_us`
- `pad_interval_us_avg`
- `pad_interval_us_max`
- `input_events`
- `view_mode`

log 形式:

```text
[CORE1_BASE] t_us=<us> frames=<n> fps_x100=<n> frame_us_avg=<us> frame_us_max=<us> lcd_wait_us=<us> lcd_flush_us=<us> pad_interval_us_avg=<us> pad_interval_us_max=<us> input_events=<n> view_mode=<normal|stretch>
```

ログは大量に UART へ流さない。
1 秒ごとの summary だけにする。

### build / commit

- 実装後に build する。
- 計測だけで挙動が変わらないことを確認できたら commit する。

### 実機確認

1 回。

確認 ROM:

- 軽い代表 ROM: `LodeRunner`
- stretch 表示の確認 ROM: `DragonQuest3`

ROM file は repository に含めない。
実施記録には、各 ROM について次を残す。

- ROM display name
- SD path
- SHA-256
- mapper number
- battery SRAM 有無

確認項目:

- normal 表示で summary が出る。
- stretch 表示で summary が出る。
- UART 出力でゲーム進行が破綻しない。
- 入力、音、ESC menu return が壊れていない。

### 合格条件

- core1 化前の baseline として比較可能な数字が残る。
- UART log によってゲーム体験が大きく壊れない。
- 計測を `#ifdef` などで無効化できる。

## Phase 1: core1 keyboard polling

### 目的

UX 改善を先に狙う。
ゲーム処理や LCD 描画が重い時でも、keyboard FIFO を core1 側で定期的に読む。

### 設計

core1:

- `i2c_kbd_read_key()` を 1 ms から 5 ms 程度の間隔で polling する。
- D-pad / A / B / Start / Select は level state として保持する。
- ESC / F1 / F5 / Shift+W などの system key は edge event として pending bit に保持する。

core0:

- `InfoNES_PadState()` では、core1 が作った level snapshot を読む。
- system key は read-and-clear で取得する。
- 初期版では game 実行中だけ core1 keyboard worker を動かす。
- ROM menu は従来どおり core0 側の入力処理を使う。

### 同期方針

初期版では shared state を小さく保ち、同期方式は
`critical_section_t`
に固定する。

core1 は keyboard FIFO を読み、次の共有状態を更新する。

- `pad1_level`
- `pad2_level`
- `system_pending_bits`
- debug counter

core0 は短い critical section 内で snapshot copy を行う。
`system_pending_bits`
は read-and-clear とし、F5 / Shift+W / F1 / ESC などの edge event を多重発火させない。

I2C keyboard の所有者は game 実行中は core1 に寄せる。
core0 と core1 が同時に `i2c_kbd_read_key()` を呼ぶ設計は禁止する。

### core1 worker lifecycle

`multicore_launch_core1()`
は firmware 起動後に 1 回だけ呼ぶ。
core1 は常駐 main loop として動かし、service bitmask を command/state で切り替える。
keyboard polling と LCD worker は排他的 mode にしない。

service bit:

- `CORE1_SERVICE_KEYBOARD`
- `CORE1_SERVICE_LCD`

初期状態:

- service bit なし
- core1 は idle loop で待機する

Phase 1:

- `CORE1_SERVICE_KEYBOARD`
  だけを有効にする。

Phase 3 以降:

- `CORE1_SERVICE_KEYBOARD`
  を維持したまま、
  LCD worker 実験時だけ
  `CORE1_SERVICE_LCD`
  を追加で有効にする。

ROM menu 中は service bit なしにする。
ROM menu から game へ入る直前に `CORE1_SERVICE_KEYBOARD` を有効にし、
ESC などで ROM menu へ戻る前に service bit なしへ戻す。
これにより、ROM menu 側の既存入力処理と core1 worker が同時に I2C keyboard を読まないようにする。

### Phase 1 の enable / disable 差し込み位置

Phase 1 では、keyboard worker の有効化 / 無効化位置を次に固定する。

有効化:

1. `platform/infones_session.cpp` の `InfoNES_Menu()` で ROM load を行う。
2. `InfoNES_Load(selected_path)` の戻り値を確認する。
3. `load_result == 0` の場合だけ `display_set_mode(DISPLAY_MODE_NES_VIEW)` を呼ぶ。
4. `display_set_mode(DISPLAY_MODE_NES_VIEW)` の直後に
   `core1_set_services(CORE1_SERVICE_KEYBOARD)` を呼ぶ。
5. `InfoNES_Menu()` から成功を返し、`InfoNES_Cycle()` に入る。

ROM load 失敗時:

1. `load_result != 0` の場合は
   `CORE1_SERVICE_KEYBOARD` を有効化しない。
2. service bit は ROM menu 用の状態、つまり service なしのまま維持する。
3. `display_set_mode(DISPLAY_MODE_NES_VIEW)` も呼ばず、
   従来の ROM load 失敗処理へ戻す。
4. これにより、load 失敗後に ROM menu 側の direct I2C input と
   core1 keyboard worker が競合しないようにする。

無効化:

1. `InfoNES_Cycle()` が ESC などで return する。
2. `run_infones_session()` wrapper 側で `InfoNES_Main()` が return した直後に、
   `core1_set_services(0)` を呼ぶ。
3. `core1_wait_idle_ack()` で core1 が keyboard polling を停止したことを確認する。
4. `core1_wait_idle_ack()` が成功してから ROM menu loop へ戻る。

初期版では、無効化位置を `run_infones_session()` の wrapper 側に固定する。
理由は、`InfoNES_Main()` の終了理由に関係なく、必ず session 終了後に service 停止を通せるため。

### keyboard worker 停止 handshake

`CORE1_SERVICE_KEYBOARD`
を無効化しただけで ROM menu へ戻ってはならない。
core1 が I2C keyboard access 中の可能性があるため、次の handshake を必須にする。

1. core0 が service bit から `CORE1_SERVICE_KEYBOARD` を落とす。
2. core1 は現在実行中の `i2c_kbd_read_key()` / `i2c_kbd_last_state()` の処理を抜ける。
3. core1 は shared state に `CORE1_STATUS_KEYBOARD_STOPPED` または `CORE1_STATUS_IDLE_ACK` を立てる。
4. core0 は `core1_wait_idle_ack()` で ack を確認する。
5. ack 確認後だけ、ROM menu 側が `i2c_kbd_read_key()` / `i2c_kbd_read_battery()` を呼んでよい。

`core1_wait_idle_ack()`
には timeout を設ける。
timeout 時は UART に短い error を出し、keyboard worker を無効扱いにして ROM menu へ戻る。
ただし、timeout が発生した build は合格にしない。

ROM menu 自体の入力を core1 化する場合は、別 phase として扱う。

LCD worker を採用する場合も、Phase 1 で得た keyboard polling の UX 改善を失わないことを原則にする。
もし LCD worker と keyboard polling の併用で core1 側の処理時間が不足する場合は、LCD worker 側を一時停止または不採用にし、keyboard polling を優先する。

### Phase 1A: core1 idle loop

目的:

- `pico_multicore` を追加しても boot / ROM menu が壊れないことを確認する。
- core1 を firmware 起動後 1 回だけ launch し、service bit なしの idle loop で待機させる。

実装内容:

- `CMakeLists.txt` に `pico_multicore` を追加する。
- core1 常駐 loop を追加する。
- 初期状態は service bit なしにする。
- core1 は I2C / LCD / shared input state に触らない。

build / commit:

- build する。
- 起動、ROM menu、既存 game 起動に影響がなければ commit する。

実機確認:

- 原則として Phase 1B の短時間実機確認へまとめる。
- ただし boot や ROM menu 到達に不安がある場合は、Phase 1A 単独で短時間確認する。

### Phase 1B: service bit / idle ack

目的:

- keyboard polling をまだ行わず、service bit と idle ack だけを確認する。
- ROM menu から game へ入る前後の enable / disable 経路を先に固める。

実装内容:

- `core1_set_services(...)`
  を追加する。
- `core1_wait_idle_ack()`
  を追加する。
- `InfoNES_Menu()` の ROM load 成功後、`display_set_mode(DISPLAY_MODE_NES_VIEW)` 直後に
  `CORE1_SERVICE_KEYBOARD`
  を有効化する。
- `run_infones_session()` wrapper 側で session 終了後に service bit を 0 にし、
  `core1_wait_idle_ack()`
  を待つ。
- この段階では core1 は I2C keyboard を読まない。

build / commit:

- build する。
- boot / ROM menu / ROM 起動 / ESC menu return が壊れていなければ commit する。

実機確認:

- 1 回の短時間確認を行う。
- 確認項目:
  - PicoCalc が起動する。
  - ROM menu に到達する。
  - ROM を 1 本起動できる。
  - ESC で ROM menu に戻れる。
  - UART に idle ack timeout が出ない。

### Phase 1C: keyboard polling

目的:

- core1 が game 実行中だけ I2C keyboard FIFO を polling する。
- Phase 1B で確認した service / ack 経路に、実際の入力処理を載せる。

実装内容:

- core1 が `CORE1_SERVICE_KEYBOARD` 有効中だけ `i2c_kbd_read_key()` を読む。
- D-pad / A / B / Start / Select を level state として共有する。
- F5 / Shift+W / F1 / ESC を `system_pending_bits` として共有する。
- shared state の同期は `critical_section_t` に固定する。

`input_poll()` の分岐方法:

- `CORE1_SERVICE_KEYBOARD`
  が有効な間は、core0 側の
  `input_poll()`
  は `i2c_kbd_read_key()` を直接呼ばない。
- その場合、
  `input_poll()`
  は core1 が更新した shared snapshot を
  `critical_section_t`
  内で copy する。
- `pad1`
  / `pad2`
  は level snapshot を返す。
- `system`
  は `system_pending_bits`
  を read-and-clear して返す。
- `CORE1_SERVICE_KEYBOARD`
  が無効な間は、従来どおり
  `input_poll()`
  が直接 `i2c_kbd_read_key()` / `i2c_kbd_last_state()` を読む。

system key の press / hold 互換:

- F5 screenshot は `KEY_STATE_PRESSED` のみで `PAD_SYS_SCREENSHOT` を立てる。
- Shift+W view toggle は `KEY_STATE_PRESSED` のみで `PAD_SYS_VIEW_TOGGLE` を立てる。
- ESC quit は `KEY_STATE_PRESSED` または `KEY_STATE_HOLD` で `PAD_SYS_QUIT` を立てる。
- F1 reset は `KEY_STATE_PRESSED` または `KEY_STATE_HOLD` で `PAD_SYS_RESET` を立てる。
- この press / hold 条件は現行 `platform/input.c` の挙動と同じにする。

build / commit:

- build する。
- game 入力と ROM menu 入力が破綻しなければ commit する。

### 実機確認

1 回。

確認項目:

- ROM menu の上下移動、決定、viewer、screenshot が壊れていない。
- game 中の D-pad / A / B / Start / Select が壊れていない。
- F5 screenshot が 1 回押しで 1 回だけ反応する。
- Shift+W が 1 回押しで 1 回だけ表示切替する。
- F1 reset / ESC menu return が暴発しない。
- ESC で ROM menu に戻った後、ROM menu の入力が壊れていない。
- キー押しっぱなし、短押し、連打で stuck key が出ない。

### 合格条件

- 入力の体感が悪化しない。
- system key の edge event が多重発火しない。
- `InfoNES_PadState()` の API 互換を保つ。

### Phase 1C 実施結果

Phase 1C は 2026-04-28 に実施済みである。

採用した状態:

- core1 keyboard polling は 1ms polling と FIFO drain の元実装を維持する。
- `multicore_lockout_victim_init()` を core1 worker に入れる。
- flash erase / program は `multicore_lockout_start_blocking()` /
  `multicore_lockout_end_blocking()` で保護する。
- SD から flash staging した ROM と、SYSTEM FLASH entry から起動した ROM の両方で同じ入力経路を使う。

実機で確認した内容:

- ユーザー確認により、DART と TOWER は SD から起動しても SYSTEM FLASH から起動してもキー入力を受け付ける。
- ユーザー確認により、ROM menu への ESC return 後も入力は壊れていない。
- 2026-04-29 に、DART と TOWER が SD からも flash からも動作することを再確認した。

不採用にした内容:

- 4ms polling / 1 回あたり最大 4 event の polling 調整は、flash lockout 修正後には不要と判断し、採用しない。

次の再開位置:

- Phase 1C 完了状態を `main` に取り込む。
- その後、LCD worker 実験は Phase 2 から新しい branch で開始する。

## Phase 2: LCD worker 前の所有権整理

### 目的

LCD worker を入れる前に、LCD を誰が触るかを明確にする。

### 方針

game 表示:

- この計画書内の Phase 3 / Phase 4 で LCD worker 化の対象にする。
- Phase 2 では、Phase 3 / Phase 4 へ進めるように LCD 所有権と drain 呼び出し位置だけを整理する。

fullscreen UI:

- ROM menu
- help
- screenshot viewer
- opening
- loading
- screenshot capture

これらは core0 が直接描く。
LCD worker が動作中の場合は、fullscreen UI に入る前に worker を stop / drain する。

### 実装内容

- LCD worker の状態 enum を先に定義する。
- 未実装の worker に対しても、start / stop / drain の呼び出し位置を整理する。
- この phase では、実際の core1 LCD 転送はまだ行わない。

### build / commit

- build する。
- 表示挙動が変わらないことを確認できたら commit する。

### 実機確認

Phase 1 の確認で表示系に不安がある場合のみ 1 回。
問題がなければ実機確認は Phase 3 へまとめる。

### Phase 2 実施結果

Phase 2 は 2026-04-29 に実装した。

実装した内容:

- `display_lcd_worker_state_t` を追加した。
- `display_lcd_worker_stop_and_drain()` を追加し、現時点では no-op の LCD worker stop と `lcd_dma_wait()` を行う。
- `display_lcd_worker_prepare_nes_view()` を追加し、将来の NES view worker 開始位置を固定した。
- fullscreen UI へ入る `display_set_mode(DISPLAY_MODE_FULLSCREEN)` で worker drain を行う。
- Shift+W の表示切替前に worker drain を行う。
- screenshot capture 前に worker drain を行う。
- この phase では `CORE1_SERVICE_LCD` は有効化していない。

build 結果:

- version: `1.0.11`
- build id: `Apr 29 2026 00:13:13`
- UF2 SHA-256: `480b8ef5f44246c0d08bacdc258a45390fab4ba4a4d419c26de7ad34b1d7c9c1`

次の再開位置:

- Phase 3 は、この Phase 2 の drain / ownership 接続点を使って normal 表示だけの LCD worker queue を実装する。

## Phase 3: core1 LCD worker normal 表示

### 目的

フレームレート改善の本命である LCD 後処理の core1 化を、normal 表示だけで試す。

### 設計

core0:

- PPU line render までは従来どおり行う。
- 描画済み line を queue へ submit する。
- core1 が読む前に同じ buffer を上書きしない。

core1:

- line queue から受け取る。
- RGB565 pack を行う。
- LCD DMA buffer へ詰める。
- strip 単位で `lcd_set_window()` / DMA kick を行う。

### buffer 方針

初期版は line buffer queue を使う。

候補:

- `WORD line_buffer[QUEUE_DEPTH][256]`
- `QUEUE_DEPTH` は 2 から 4 で開始する。

queue が詰まった場合、core0 は待つ。
この待ち時間を計測し、LCD worker 化の効果判定に使う。

### frame boundary / drain 方針

line descriptor には最低限、次を持たせる。

- `frame_id`
- `scanline`
- `display_mode`
- `viewport snapshot`
- `line buffer index`

frame の最後には、通常 line とは別に frame end marker を送る。
core1 は frame end marker を受け取った時点で、その frame の未送信 strip を flush し、
LCD DMA 完了まで `lcd_dma_wait()` する。

core0 が screenshot、ESC menu return、Shift+W mode 切替、fullscreen UI へ入る場合は、
explicit drain command を送る。
drain 完了条件は次の 3 点をすべて満たすことに固定する。

1. line queue が空である。
2. core1 が保持している未送信 strip がない。
3. LCD DMA が完了している。

drain 完了後だけ、core0 は LCD を fullscreen UI 用に直接操作してよい。

### 禁止事項

- `s_line_buffer[256]` の pointer だけを core1 へ渡さない。
- core1 が current viewport global state を直接読みに行かない。
- screenshot capture 中に LCD worker を動かし続けない。

### build / commit

- normal 表示だけで build する。
- normal 表示が壊れていないことを確認できたら commit する。

### 実機確認

1 回。

確認項目:

- normal 表示で画面崩れがない。
- input / audio が壊れていない。
- screenshot が壊れていない。
- ESC で ROM menu に戻れる。
- ROM menu から別 ROM を起動できる。
- baseline と比較して `lcd_wait_us` や frame time が改善しているか確認する。

### 合格条件

- normal 表示が安定している。
- core0 の LCD wait が減る。
- queue stall が frame time を悪化させない。

### Phase 3 実施結果

Phase 3 は 2026-04-29 に normal 表示のみ実装した。

実装した内容:

- normal 表示時だけ core0 が scanline を queue に copy し、core1 が RGB565 pack と LCD DMA kick を行う。
- stretch 表示は従来の core0 表示経路のまま残した。
- queue entry は scanline、viewport snapshot、line pixels を持つ。
- `s_line_buffer[256]` の pointer は core1 へ渡さない。
- core1 側の strip buffer は LCD driver の DMA buffer を `lcd_dma_acquire_buffer()` で取得して使う。
- queue が空でない間は core1 が LCD worker を連続処理し、空のときだけ短く待つ。
- keyboard polling は 1ms 周期を維持する。

実機で確認した内容:

- 起動直後の画面は正常である。
- normal 表示は初期版より改善した。
- screenshot は保存できる。
- ESC で ROM menu へ戻れる。
- 音は正常である。

build 結果:

- version: `1.0.14`
- build id: `Apr 29 2026 00:48:55`
- UF2 SHA-256: `67007f33bc0dada14ed2beec9fb432c54a84342b1fb7c146ecc5e5a224fee774`

次の再開位置:

- Phase 4 へ進む場合は、この normal LCD worker を基準に stretch 表示だけを追加実験する。
- 速度や安定性に問題が出る場合は、Phase 2 commit へ戻せる。

## Phase 4: core1 LCD worker stretch 表示

### 目的

stretch 表示の 256x240 から 320x300 への変換を core1 側へ移し、stretch 時の負荷改善を測る。

### 実装内容

- Phase 3 の normal queue を拡張する。
- line descriptor に display mode と scanline を含める。
- 320px pack と縦 repeat を core1 側で行う。
- mode 切替時は worker を drain してから viewport / mode snapshot を切り替える。

### build / commit

- build する。
- normal / stretch の両方が壊れていなければ commit する。

### 実機確認

1 回。

確認項目:

- normal 表示
- stretch 表示
- Shift+W 連続切替
- screenshot
- ROM menu return
- BMP viewer
- 音切れ
- input 遅延

### 合格条件

- normal / stretch の両方で画面崩れがない。
- mode 切替直後に古い viewport や古い DMA が残らない。
- baseline より stretch 時の frame time が改善する、または同等で UX が悪化しない。

## Phase 5: 採用判断

### 判断基準

keyboard worker:

- UX が良くなる。
- stuck key や system key 多重発火がない。
- 実装が小さく保てる。

LCD worker:

- frame time または stretch 表示の余力が実測で改善する。
- 画面崩れ、screenshot 破損、ROM menu 破損がない。
- queue / buffer による RAM 増加が Mapper30 などの RAM 余裕を壊さない。
- 実装複雑度に見合う効果がある。

### 採用しない条件

- FPS 改善がほぼない。
- queue stall で逆に遅くなる。
- DMA / LCD ownership バグが残る。
- 入力や menu の安定性が下がる。

採用しない場合でも、Phase 0 の計測結果と Phase 1 の keyboard worker は別判断にする。
LCD worker が不採用でも、keyboard worker だけ採用できる可能性を残す。

## 予定 build 回数

計画全体では最低 7 回。
2026-04-28 時点で Phase 0 から Phase 1C までは実施済みである。

1. Phase 0 計測 build
2. Phase 1A core1 idle loop build
3. Phase 1B service bit / idle ack build
4. Phase 1C keyboard polling build
5. Phase 2 LCD ownership 整理 build
6. Phase 3 normal LCD worker build
7. Phase 4 stretch LCD worker build

Phase 1C 完了後に開発を再開する場合、残りの予定 build は Phase 2 / Phase 3 / Phase 4 である。
Phase 3 / Phase 4 の前後では、必要に応じて clean build を行う。

## 予定実機確認回数

計画全体では最低 5 回。
2026-04-28 時点で Phase 0 から Phase 1C の実機確認は実施済みである。

1. Phase 0 baseline 計測
2. Phase 1B boot / ROM menu 到達 / idle ack 短時間確認
3. Phase 1C keyboard worker game 入力確認
4. Phase 3 normal LCD worker 確認
5. Phase 4 stretch LCD worker 総合確認

Phase 1C 完了後に開発を再開する場合、残りの実機確認は Phase 3 / Phase 4 である。
LCD worker を実装しない判断をした場合は、Phase 3 / Phase 4 の実機確認は不要。

## 主なリスク

### 入力 worker

- core0 / core1 が同時に I2C keyboard を読むと壊れる。
- system key を level 扱いにすると F5 / Shift+W が多重発火する。
- ROM menu と game で別々の入力経路が残ると、片方だけ直って片方が壊れる。
- core1 実行中に flash erase / program を行うと、core1 側が XIP flash access に巻き込まれる可能性がある。
- flash erase / program を行う処理は、必ず `multicore_lockout` で core1 を退避させる。
- 今後 flash へ書く処理を追加する場合も、ROM staging と同じ lockout 方針を適用する。

### LCD worker

- line buffer の寿命を誤ると画面が崩れる。
- LCD DMA buffer の所有権を誤ると不定期に崩れる。
- fullscreen UI と game LCD worker が同時に LCD を触ると壊れる。
- screenshot capture と worker が同時に framebuffer / LCD state を触ると壊れる。
- RAM 増加で Mapper30 や screenshot viewer の余裕を削る。
