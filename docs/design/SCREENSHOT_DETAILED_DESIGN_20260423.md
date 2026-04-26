# スクリーンショット機能 詳細設計書 2026-04-23

## 目的

スクリーンショット機能を、
どのモジュールに何を持たせ、
どの順序で呼び、
どこで状態を切り替えるか、
実装単位まで落として整理する。

この文書では、
コード内容、
既存設計メモ、
ユーザー指示として確認できたものだけを
事実として書く。
そこから先の API 名や分割案は
`【推定】`
を付ける。

## 現状の前提

事実:
- `platform/display.c`
  には
  `InfoNES_PreDrawLine()`
  `InfoNES_PostDrawLine()`
  `InfoNES_LoadFrame()`
  がある。
- `InfoNES_PostDrawLine()`
  は scanline ごとの出力を strip 単位で LCD DMA バッファへ詰め、
  strip 完了時に DMA 転送を開始する。
- `platform/display.c`
  と
  `drivers/lcd_spi.c`
  には
  `lcd_dma_wait()`
  がある。
- `drivers/lcd_spi.c`
  には
  `lcd_set_window()`
  と書き込み系 DMA API がある。
- 確認した範囲では、
  LCD readback 用 API、
  スクリーンショット状態管理、
  BMP 保存 API は未実装である。
- `infones/InfoNES.cpp`
  では
  `SCAN_VBLANK_START`
  で VBLANK 遷移処理が行われる。
- ユーザー指示で、
  スクリーンショット実行キーは
  `F5`
  とすることが決定した。
- ユーザー指示で、
  `F5`
  の reset 割り当ては外し、
  reset は
  `F1`
  のみへ残す方針が決定した。

## 実装対象の分割

初期版では、
責務を次の 3 層に分ける。

### emulation 側

担当:
- screenshot 要求受付
- 次の VBLANK での capture 開始判定
- capture 中の進行停止
- 完了後の再開

### display / LCD 側

担当:
- LCD DMA 完了待ち
- LCD GRAM readback
- readback 生データの整形

### storage 側

担当:
- 保存ファイル名の決定
- BMP header 作成
- SD への書き込み

## 状態設計

初期版では少なくとも次の状態を持つ。

### 永続フラグ

`screenshot_pending`
- ユーザー要求が入ったが、
  まだ capture を開始していない状態。

`screenshot_busy`
- capture または保存処理中であることを示す状態。

### 【推定】補助状態

`screenshot_error_code`
- 最後の失敗理由を持つ。

`capture_requested_frame`
- 要求受付フレームを記録したい場合に使う。
- 初期版では必須ではない。

## 状態遷移

初期版の標準遷移は次とする。

```text
idle
  -> pending
  -> capturing
  -> saving
  -> idle
```

異常時は次を許容する。

```text
pending
  -> capturing
  -> error
  -> idle
```

## 制御フロー

初期版の capture フローは次の順とする。

1. hotkey 入力を受ける
2. `screenshot_pending = true` を立てる
3. emulation 側で次の VBLANK 到達を検出する
4. `lcd_dma_wait()` を呼び、
   LCD への未完了転送を完了させる
5. `screenshot_busy = true` にする
6. emulation 進行を停止状態にする
7. LCD GRAM から `320x320` を読み出す
8. 必要な byte order / dummy read 調整を行う
9. `storage_save_screenshot_bmp(...)`
   へ RGB565 バッファを渡して保存する
10. `screenshot_pending = false`
    `screenshot_busy = false`
    に戻す
11. emulation を再開する

初期版の入力前提:
- `F5`
  で screenshot 要求を受け付ける
- `F1`
  は reset のままとする

初期版の入力経路:
- `PAD_SYS_SCREENSHOT`
  の system bit 定義は
  `infones/InfoNES.h`
  を正本として追加する
- `input.c`
  で
  `F5`
  を
  新設の
  `PAD_SYS_SCREENSHOT`
  へ割り当てる
- `PAD_SYS_SCREENSHOT`
  は
  `KEY_STATE_PRESSED`
  のときだけ
  1 回立てる
- `KEY_STATE_HOLD`
  と
  `KEY_STATE_RELEASED`
  では
  `PAD_SYS_SCREENSHOT`
  を立てない
- `InfoNES_PadState(...)`
  により
  `PAD_System`
  へ screenshot 要求が渡る
- `SCAN_VBLANK_START`
  の
  `InfoNES_PadState(...)`
  直後で
  `PAD_PUSH(PAD_System, PAD_SYS_SCREENSHOT)`
  を見て
  `nesco_request_screenshot()`
  を呼ぶ
- その後
  `nesco_maybe_start_screenshot_on_vblank()`
  が
  `screenshot_pending`
  を見て capture 開始判定を行う

## VBLANK 起点の固定位置

事実:
- `infones/InfoNES.cpp`
  の
  `SCAN_VBLANK_START`
  ケースでは、
  少なくとも次の順で処理が並んでいる。
  1. `FrameCnt` 更新
  2. `PPU_R2 |= R2_IN_VBLANK`
  3. `InfoNES_pAPUVsync()`
  4. `MapperVSync()`
  5. `InfoNES_PadState(&PAD1_Latch, &PAD2_Latch, &PAD_System)`
  6. NMI on V-Blank 判定

初期版では、
スクリーンショット開始判定の挿入位置を次で固定する。

- `InfoNES.cpp`
  の
  `case SCAN_VBLANK_START:`
  内
- `InfoNES_PadState(...)`
  の直後
- NMI 判定より前

固定理由:
- 入力として
  `F5`
  を取り込んだ直後に screenshot 要求を判定できる。
- `KEY_STATE_PRESSED`
  の 1-shot 要求を
  VBLANK 側で 1 回だけ pending 化しやすい。
- `R2_IN_VBLANK`
  が既に立っている。
- NMI 前に capture を開始する位置として文書内の解釈を一本化できる。

初期版の呼び出し順は次を正とする。

```text
case SCAN_VBLANK_START:
    FrameCnt update
    set R2_IN_VBLANK
    InfoNES_pAPUVsync()
    MapperVSync()
    InfoNES_PadState(...)
    if PAD_SYS_SCREENSHOT pushed:
        nesco_request_screenshot()
    nesco_maybe_start_screenshot_on_vblank()
    NMI on V-Blank
```

【推定】:
- `nesco_maybe_start_screenshot_on_vblank()`
  の内部で
  `screenshot_pending`
  を見て、
  必要時のみ
  `lcd_dma_wait()`
  と capture シーケンスへ進む。
- pending がない通常フレームでは、
  追加コストは最小の分岐だけに留める。

## 推奨モジュール責務

### 1. emulation 制御モジュール

【推定】責務:
- hotkey からの screenshot 要求受付
- `pending`
  判定
- VBLANK 起点の start 判定
- busy 中の再要求拒否
- pause / resume の一元管理

【推定】候補配置:
- `platform/`
  配下に screenshot 制御ファイルを追加
- または
  `platform/input.c`
  と
  `platform/display.c`
  の間を仲介する小モジュールを追加

### 2. LCD readback モジュール

【推定】責務:
- readback 前の DMA 完了待ち
- readback 対象 window 設定
- `RAMRD`
  実行
- dummy read 吸収
- 生データの出力

【推定】候補配置:
- `drivers/lcd_spi.c`
  に readback API を追加
- 宣言は `drivers/` もしくは `platform/display.h` 近傍へ追加

### 3. BMP 保存モジュール

【推定】責務:
- ファイル名生成
- BMP header 作成
- pixel rows の順序調整
- SD / FatFs への書き込み

【推定】候補配置:
- `platform/`
  に `screenshot_storage.c`
  相当を追加

## API / 関数単位の責務

事実:
- 現在のコードベースで存在確認できた screenshot API はない。

【推定】初期版の候補関数:

`nesco_request_screenshot()`
- `F5`
  に対応する
  `PAD_SYS_SCREENSHOT`
  から呼ぶ。
- idle 時のみ pending を立てる。
- `KEY_STATE_PRESSED`
  に対応する単発要求だけを受ける前提とする。

`nesco_maybe_start_screenshot_on_vblank()`
- VBLANK 到達時に呼ぶ。
- `pending == true`
  のときだけ capture シーケンスへ入る。
- 呼び出し位置は
  `InfoNES.cpp`
  の
  `SCAN_VBLANK_START`
  ケース内で、
  `InfoNES_PadState(...)`
  の直後、
  NMI 判定より前に固定する。

`nesco_pause_for_screenshot()`
- capture 開始前に emulation 進行を止める。

`nesco_resume_after_screenshot()`
- 保存完了後に再開する。

`lcd_readback_320x320_rgb565(...)`
- LCD 全画面を readback する。
- 出力形式は初期版では RGB565 を第一候補とする。
- readback 用の address-window helper を使い、
  既存
  `lcd_set_window()`
  は使わない。

`storage_save_screenshot_bmp(...)`
- BMP header と pixel data をまとめて保存する。
- 初期版では
  `24bpp`
  非圧縮 BMP を出力する。
- RGB565 から 24bpp BGR への変換と行整形も内部で完結する。

## pause / resume の扱い

初期版では、
capture 開始から保存完了まで emulation を停止する。

【推定】:
- 停止の実装は、
  VBLANK フック内で同期的に処理を完了させる形が最も単純である。
- audio については、
  初期版では無音ギャップが短時間生じても許容とする設計が現実的である。

## バッファ設計

【推定】:
- `320x320 x 16bpp`
  の raw 画素は
  `204800 bytes`
  である。
- 初期版は 1 枚分の readback バッファと、
  BMP 書き出し用の行バッファを分けると責務が明確になる。
- メモリ圧迫が大きい場合は、
  全画面一括変換ではなく、
  readback 後の行単位保存へ寄せる余地がある。

## ファイル命名

【推定】:
- 保存先は ROM save と衝突しにくい専用ディレクトリを優先する。
- 例:
  `0:/screenshots/`
- ファイル名は連番または日時風識別子を使う。
- 例:
  `NESCO_0001.BMP`

## エラー時の扱い

初期版で扱うべき異常系は次とする。

### SD 書き込み失敗

期待動作:
- error を記録する
- busy を解除する
- emulation は再開する

### readback 失敗

期待動作:
- 保存へ進まない
- pending を落とす
- error を記録して再開する

### DMA wait timeout

【推定】:
- timeout を設ける場合は readback を中止し、
  error 扱いで復帰する。

### busy 中再要求

期待動作:
- 新規要求は受理しない。
- pending の追加キューは持たない。
- 多重 capture はしない。

## 実装順の推奨

1. screenshot 状態管理を追加する
2. `PAD_SYS_SCREENSHOT` を追加する
3. `input.c` で `F5` を `PAD_SYS_SCREENSHOT` へ割り当てる
4. VBLANK 起点で screenshot 要求受付と `lcd_dma_wait()` までつなぐ
5. read 用 address-window helper を追加する
6. LCD readback API を追加する
7. BMP 保存 API を追加する
8. `README.md` と help 表示のキー説明を更新する
9. 正常系を通す
10. 異常系と busy 再要求を詰める
