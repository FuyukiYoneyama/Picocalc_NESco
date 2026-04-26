# NES stretch view 切替計画 (2026-04-21)

この文書は、`Picocalc_NESco` で通常の `256x240` 表示と、
RPG 向けの `320x300` ストレッチ表示を runtime で切り替えるための正本設計です。

対象:

- `platform/display.h`
- `platform/display.c`
- `platform/input.h`
- `platform/input.c`

目的:

- 通常表示 `256x240` と、拡大表示 `320x300` を動的に切り替える
- 切替 hotkey は `Shift+W`
- フレームレート低下は許容するが、表示意味は壊さない
- 任意スケーラではなく、固定の `5:4` ストレッチ専用 path として実装する

## 1. 現在確認できている事実

### 1.1 LCD と NES の現在の表示サイズ

- LCD は `320x320 RGB565` である
  - `platform/display.h`
  - `drivers/lcd_spi.c`
- NES 出力は `256x240` である
  - `infones/InfoNES.h`
- 現在の NES viewport は
  - `x=32`
  - `y=24`
  - `w=256`
  - `h=240`
  で設定されている
  - `platform/display.c`

### 1.2 現在の表示 path は 256 固定

- `InfoNES_PreDrawLine()` は `s_line_buffer[256]` を `InfoNES` 側へ渡している
  - `platform/display.c`
- `InfoNES_PostDrawLine()` は `256` pixel 固定で pack している
  - `platform/display.c`
- LCD staging buffer も `256 * STRIP_HEIGHT * 2` bytes 前提である
  - `drivers/lcd_spi.c`
- strip flush 時の `lcd_set_window()` も `w=256` 前提である
  - `platform/display.c`

### 1.3 今回の倍率は固定 `5:4`

- 横:
  - `320 / 256 = 1.25 = 5 / 4`
- 縦:
  - `300 / 240 = 1.25 = 5 / 4`

したがって、今回必要なのは任意倍率スケーラではなく、
**固定 5:4 ストレッチ専用の表示 path** である。

### 1.4 keyboard では大文字キーを取得している実績がある

- ROM menu では `H/h`、`B/b`、`?` を区別して使っている
  - `platform/rom_menu.c`
- `Shift+W` が raw key として `'W'` で来る可能性は高い
  - ただしゲーム中 input path では現在 `W` を特別扱いしていない
  - `platform/input.c`

## 2. 問題の整理

今回の課題は、viewport を変えるだけでは解決しない。

必要なのは次の 2 点である。

1. **表示 path の切替**
- 通常 `256x240`
- ストレッチ `320x300`

2. **ゲーム中 hotkey の切替**
- `Shift+W`

特に本質的なのは 1 である。
現在の `InfoNES_PostDrawLine()` は 256 固定の line pack なので、
stretch 表示では

- 横方向 256 → 320
- 縦方向 240 → 300

の両方を、`InfoNES_PostDrawLine()` の中か、その直後の staging path で処理する必要がある。

## 3. 採用する設計方針

### 3.1 表示モードを 2 つに分ける

新しく NES view 専用の内部モードを持つ。

- `NES_VIEW_NORMAL`
  - `256x240`
  - viewport `x=32 y=24 w=256 h=240`
- `NES_VIEW_STRETCH_320x300`
  - `320x300`
  - viewport `x=0 y=10 w=320 h=300`

これは `DISPLAY_MODE_FULLSCREEN / DISPLAY_MODE_NES_VIEW` とは別の、
**NES 表示内部モード**として持つ。

理由:

- menu / opening / loading の full-screen UI とは責務が違う
- `display_set_mode()` の意味を壊さずに NES だけ切り替えたい

### 3.2 scaling は `InfoNES_PostDrawLine()` 内で行う

正本は、
**PPU 出力 `256x240` はそのまま維持し、表示 path だけで stretch する**
とする。

理由:

- `InfoNES_DrawLine()` や PPU の内部意味に手を入れない
- 既存の `0.1.76` 表示基準を壊しにくい
- ストレッチは表示要求であって、PPU の論理解像度変更ではない

### 3.3 scaling は固定 5:4 の専用 path とする

stretch 時の変換規則:

- 横:
  - 4 pixel を 5 pixel に拡大
- 縦:
  - 4 line を 5 line に拡大

【推定】実装としては、もっとも単純なのは

- 横:
  - source x を `dst_x * 4 / 5` で逆参照
- 縦:
  - source scanline を `dst_y * 4 / 5` で逆参照

だが、scanline 単位 pipeline なので、
正本では **line duplication pattern** を使う。

採用する縦方向規則:

- 正本は
  **4 source line -> 5 destination line**
  の固定対応表とする
- 1 block 内の対応は次で固定する
  - source line `0` -> destination line `0`
  - source line `1` -> destination line `1`
  - source line `2` -> destination line `2`
  - source line `3` -> destination line `3, 4`
- つまり、source line index `n` に対して
  - `n % 4 != 3` の line は 1 本だけ出す
  - `n % 4 == 3` の line は 2 本出す

この規則を固定する理由:

- scanline 単位 pipeline のまま実装できる
- source line index から destination line 数が一意に決まる
- `STRIP_HEIGHT = 8`
  のとき、
  8 source lines は常に
  10 destination lines
  になる
- したがって、
  stretch 時の strip flush 高さも
  **常に 10 line**
  と固定できる

## 4. 変更箇所

### 4.1 `platform/display.h`

- NES view scale mode enum を追加
- getter / setter を追加
  - `display_toggle_nes_view_scale()`
  - `display_get_nes_view_scale()`

### 4.2 `platform/display.c`

- current NES view scale state を static で保持
- `display_set_mode(DISPLAY_MODE_NES_VIEW)` 時に、
  現在の scale mode に応じて viewport を設定する
- `InfoNES_PostDrawLine()` は
  - normal path
  - stretch path
  の 2 系統を持つ
- stretch path では
  - source `256` pixel line から
  - destination `320` pixel line を作る
- strip flush も stretch 時は
  - `w=320`
  - `h=scaled strip height`
  を使う

重要:

- 既存の `s_line_buffer[256]` はそのまま使う
- `InfoNES_PreDrawLine()` の契約は変えない
- `InfoNES_DrawLine()` 側には入らない

### 4.3 `drivers/lcd_spi.c`

- staging buffer は現在 `256 * STRIP_HEIGHT * 2`
  なので、
  stretch 時の `320` 幅に足りない
- したがって、staging buffer の設計を
  **最大幅 320 前提**へ広げる必要がある

これは今回の設計で必須。

### 4.4 `platform/input.h` / `platform/input.c`

- `Shift+W` を system side hotkey として取る
- NES pad bit には混ぜず、
  **display toggle 専用の system flag**
  として扱う

候補:

- `PAD_SYS_VIEW_TOGGLE`

もしくは input 層だけの一時フラグでもよいが、
正本は system flag 化する。

重要:

- toggle は reset / quit と違って
  **押下 edge で 1 回だけ**
  発火させる
- 現在の `input_poll()` は
  `PAD_SYS_QUIT`
  と
  `PAD_SYS_RESET`
  を
  `KEY_STATE_PRESSED`
  だけでなく
  `KEY_STATE_HOLD`
  でも立てているが、
  `PAD_SYS_VIEW_TOGGLE`
  はこれと分ける
- 正本では
  `Shift+W`
  を
  `KEY_STATE_PRESSED`
  のときだけ検出し、
  `KEY_STATE_HOLD`
  では再発火させない

### 4.5 `InfoNES_HSync()` 周辺

- 既存では `PAD_SYS_QUIT` / `PAD_SYS_RESET` を見ている
- ここへ `PAD_SYS_VIEW_TOGGLE` を追加して、
  検出時に `display_toggle_nes_view_scale()` を呼ぶ

重要:

- reset / quit と違って emulator session は継続する
- 表示 mode のみをその場で切り替える

## 5. 実装単位

途中の中間状態を増やさないため、次の単位でまとめて実装する。

1. NES view scale state と viewport 切替 API を追加
2. LCD staging buffer を最大 `320` 幅へ拡張
3. `InfoNES_PostDrawLine()` に stretch path を追加
4. `Shift+W` hotkey を system flag として追加
5. `InfoNES_HSync()` で toggle を処理
6. build
7. 実機で
   - 通常表示
   - `Shift+W`
   - stretch 表示
   - もう一度 `Shift+W`
   を end-to-end 確認

## 6. 完了条件

- ゲーム中に `Shift+W` で
  `256x240 <-> 320x300`
  を切り替えられる
- 通常表示では `0.1.76` と同じ見た目を維持する
- stretch 表示では
  - 全体が 320x300 に広がる
  - 大きな崩れがない
  - frame rate 低下は許容

## 7. 不採用とする進め方

- PPU の論理解像度そのものを変える
- `InfoNES_DrawLine()` 側で 320 幅を直接描かせる
- 任意倍率スケーラを先に作る

今回必要なのは固定 `5:4` の表示切替だけなので、
これらは採用しない。
