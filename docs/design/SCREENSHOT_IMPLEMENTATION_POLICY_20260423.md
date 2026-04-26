# スクリーンショット機能 実装方針書 2026-04-23

## 目的

`Picocalc_NESco` のスクリーンショット機能について、
初期版で採用する方式と採用しない方式を固定し、
実装判断のぶれを防ぐ。

この文書では、
コード内容、
既存設計メモ、
ユーザー指示として確認できたものだけを
事実として書く。
そこから先の設計提案や将来拡張は
`【推定】`
を付ける。

## 入力として確認できたこと

事実:
- ユーザー提示の
  `Picocalc_NESco_screenshot_design_memo_20260423.md`
  では、
  スクリーンショット中にゲームが一時停止することは許容とされている。
- 同メモでは、
  取得元は LCD コントローラの GRAM readback とする方針が示されている。
- 同メモでは、
  画面の整合性を優先し、
  速度は優先しない方針が示されている。
- `platform/display.c`
  と
  `drivers/lcd_spi.c`
  には
  `lcd_dma_wait()`
  が存在する。
- 確認した範囲のコードには
  `lcd_dma_is_idle()`
  は存在しない。
- `drivers/lcd_spi.c`
  には LCD 書き込み系 API があるが、
  GRAM readback 用の公開 API は確認できていない。
- `infones/InfoNES.cpp`
  では
  `SCAN_VBLANK_START`
  で `R2_IN_VBLANK`
  を立てている。
- `infones/InfoNES.h`
  で確認できる system bit は、
  `PAD_SYS_QUIT`
  `PAD_SYS_OK`
  `PAD_SYS_CANCEL`
  `PAD_SYS_UP`
  `PAD_SYS_DOWN`
  `PAD_SYS_LEFT`
  `PAD_SYS_RIGHT`
  `PAD_SYS_RESET`
  `PAD_SYS_VIEW_TOGGLE`
  までである。
- ユーザー指示で、
  スクリーンショット実行キーは
  `F5`
  に割り当てることが決定した。
- ユーザー指示で、
  `F5`
  の reset 割り当ては外すことが決定した。

## 初期版で固定する採用方針

### 1. capture 開始条件

初期版では、
次の条件を満たしたときに capture を開始する。

```text
if screenshot_pending
and in_vblank
then
    lcd_dma_wait()
    capture
```

採用理由:
- `lcd_dma_wait()`
  は既存コードにある。
- `lcd_dma_is_idle()`
  は現時点で確認できていない。
- `VBLANK` と DMA 完了待ちを組み合わせることで、
  LCD 側の転送途中を避けやすい。

### 2. 保存対象

初期版の保存対象は
`320x320`
全画面とする。

採用理由:
- 見えている画面をそのまま残しやすい。
- normal / stretch の両表示で扱いを分けなくてよい。
- UI hint や黒帯を含めて保存できる。

### 3. 保存形式

初期版の保存形式は
`BMP`
とする。

採用理由:
- PC でそのまま開きやすい。
- 実装が比較的単純で、
  デバッグ用途にも向く。

初期版の BMP 画素形式は
非圧縮
`24bpp`
とする。

採用理由:
- 一般的なビューアとの互換性を優先できる。
- `RGB565`
  専用の `BI_BITFIELDS`
  解釈へ依存しない。

### 4. 停止方式

capture 中は emulation を停止する。

採用理由:
- ユーザー提示メモで停止許容が前提になっている。
- readback 中の画面変化を避けやすい。
- 実装責務を分離しやすい。

### 5. 保存完了までの実行モデル

初期版では、
capture と BMP 保存を同一要求の中で同期的に完了させる。

【推定】:
- 初期版では、
  `pending -> capturing -> saving -> idle`
  の直列フローが最も扱いやすい。

### 6. 実行キー

初期版では、
ゲーム中のスクリーンショット実行キーを
`F5`
に固定する。

併せて、
reset は
`F1`
のみへ割り当てる前提とする。

### 7. screenshot 要求の入力経路

初期版では、
`F5`
  の screenshot 要求は
新しい system bit
`PAD_SYS_SCREENSHOT`
を追加して扱う。

採用理由:
- 現在の入力系は
  `PAD_System`
  を通して
  QUIT / RESET / VIEW_TOGGLE
  などの system 操作を渡している。
- screenshot も同じ系統の system 操作として扱うと、
  VBLANK 側での一元処理に寄せやすい。
- `input.c`
  から直接 screenshot 制御関数を叩く方式より、
  既存の system bit 契約を保ちやすい。

### 8. readback 用アドレス設定

初期版では、
readback に既存
`lcd_set_window()`
を使わない。

代わりに、
readback 用の
address-window helper
を新設して使う。

採用理由:
- 現在の
  `lcd_set_window()`
  は最後に
  `RAMWR`
  を送る write 用 helper である。
- readback では write モードへ入らない前提を固定したい。

## 初期版で採用しないこと

初期版では次をやらない。

- NES 表示領域だけの切り出し保存
- 非停止 capture
- `PNG` / `JPEG` 対応
- 非同期保存
- DMA 稼働状態のポーリング専用 API を前提にした制御
- `F5`
  を reset と screenshot の両方へ割り当てること
- readback に既存
  `lcd_set_window()`
  をそのまま流用すること

## 採用方式の全体像

```text
F5 pressed
  -> screenshot_pending = true
  -> 次の VBLANK
  -> lcd_dma_wait()
  -> emulation pause
  -> LCD GRAM readback
  -> 必要な色並び調整
  -> BMP 保存
  -> flag clear
  -> emulation resume
```

## 先送りする検討事項

【推定】:
- `320x320`
  全画面保存が安定したあとで、
  `256x240`
  切り出しや stretch 用切り出しを検討できる。
- readback API が安定したあとで、
  `lcd_dma_is_idle()`
  相当の補助 API 追加を検討できる。
- 保存時間が問題になった場合に限り、
  非同期保存や圧縮形式を検討できる。

## 実装着手時の判断ルール

- 初期版では整合性優先とする。
- 既存コードにある `lcd_dma_wait()` を優先的に再利用する。
- readback API が未実装でも、
  まずは LCD 側責務を独立させる。
- `F5`
  は screenshot 専用、
  `F1`
  は reset 専用として扱う。
- BMP は初期版では
  `24bpp`
  非圧縮を正とする。
- screenshot 要求は
  `PAD_SYS_SCREENSHOT`
  を新設して
  `PAD_System`
  経由で運ぶ。
- readback では
  `lcd_set_window()`
  を使わず、
  read 用 address-window helper を新設する。
- `busy`
  または `pending`
  中の追加要求は受理しない。
- 実装に入る前提として、
  `input.c`
  `rom_menu.c`
  `README.md`
  の旧キー説明を更新対象へ含める。
- 仕様追加は、
  この文書で不採用としている項目を崩さない範囲に限る。
