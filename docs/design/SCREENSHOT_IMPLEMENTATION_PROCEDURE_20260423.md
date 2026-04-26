# スクリーンショット機能 実装手順書 2026-04-23

## 目的

スクリーンショット機能の実装を、
どの順で着手し、
各段階で何を完了条件とし、
どこを先に固定し、
どこを後段のリスク項目として切り分けるか、
実装作業手順として固定する。

この文書では、
コード内容、
既存設計文書、
ユーザー指示として確認できたものだけを
事実として書く。
そこから先の作業分割や実装補助案は
`【推定】`
を付ける。

## 現時点で確認できていること

事実:
- `platform/input.c`
  では現在
  `F1`
  と
  `F5`
  の両方が
  `PAD_SYS_RESET`
  に割り当てられている。
- `infones/InfoNES.h`
  には現在
  `PAD_SYS_RESET`
  と
  `PAD_SYS_VIEW_TOGGLE`
  までが定義されており、
  `PAD_SYS_SCREENSHOT`
  は未定義である。
- `infones/InfoNES.cpp`
  の
  `SCAN_VBLANK_START`
  では、
  `InfoNES_PadState(...)`
  の呼び出し位置が確認できる。
- `drivers/lcd_spi.c`
  には
  `lcd_dma_wait()`
  `lcd_set_window()`
  `lcd_dma_write_rgb565_async()`
  `lcd_dma_write_bytes_async()`
  がある。
- `drivers/lcd_spi.c`
  の
  `lcd_set_window()`
  は最後に
  `0x2C`
  を送る。
- `platform/rom_image.c`
  では
  `f_mount()`
  と
  `f_open()`
  を使う既存の FatFs 利用実績がある。
- `drivers/lcd_spi.pio`
  は確認した範囲で
  MOSI / SCK の送信エンジンであり、
  LCD readback 用の受信経路は実装されていない。
- screenshot 状態管理、
  screenshot 制御 API、
  LCD readback API、
  BMP 保存 API、
  `SS_*`
  ログは現状未実装である。

## 実装の基本方針

事実:
- 既存文書では、
  screenshot は
  `F5`
  で要求し、
  次の
  `VBLANK`
  で
  `lcd_dma_wait()`
  後に開始する方針で固定されている。
- 既存文書では、
  初期版の保存対象は
  `320x320`
  全画面、
  保存形式は
  非圧縮
  `24bpp BMP`
  とされている。

【推定】:
- 実装は
  「低リスク部分を先に通す段階」
  と
  「LCD readback を導入する段階」
  に分けるのが最も安全である。
- readback はこの機能で唯一、
  現在の write 専用寄り driver から構造変更が必要な箇所なので、
  後段の独立フェーズとして扱う。

## 推奨フェーズ分割

初期版の実装順は、
次の 6 フェーズで固定する。

1. system bit / 入力変更
2. screenshot 状態管理と VBLANK 起点接続
3. screenshot debug log 追加
4. BMP 保存モジュール追加
5. LCD readback 実装
6. 文言更新と総合確認

## フェーズ 1: system bit / 入力変更

目的:
- `F5`
  を reset から分離し、
  screenshot 要求の入力経路を作る。

変更対象:
- `infones/InfoNES.h`
- `platform/input.c`

実装手順:
1. `infones/InfoNES.h`
   に
   `PAD_SYS_SCREENSHOT`
   を追加する。
2. 既存 bit 配置と衝突しない値を選ぶ。
3. `platform/input.c`
   のコメントを実装内容に合わせて更新する。
4. `platform/input.c`
   で
   `KEY_STATE_PRESSED`
   の
   `KEY_F5`
   を
   `PAD_SYS_SCREENSHOT`
   に割り当てる。
5. `platform/input.c`
   で
   `KEY_F1`
   は
   `PAD_SYS_RESET`
   のまま残す。
6. `KEY_STATE_HOLD`
   と
   `KEY_STATE_RELEASED`
   では
   `PAD_SYS_SCREENSHOT`
   を立てない。

完了条件:
- `F5`
  が
  `PAD_SYS_RESET`
  を出さない。
- `F1`
  が引き続き
  `PAD_SYS_RESET`
  を出す。
- `PAD_SYS_SCREENSHOT`
  が
  `KEY_STATE_PRESSED`
  でのみ 1 回立つ。

## フェーズ 2: screenshot 状態管理と VBLANK 起点接続

目的:
- screenshot 要求を
  VBLANK
  起点で消化する流れを先に成立させる。

変更対象:
- `infones/InfoNES.cpp`
- `platform/`
  配下の新規 screenshot 制御モジュール
  または
  既存 platform ファイル

【推定】候補ファイル:
- `platform/screenshot.c`
- `platform/screenshot.h`

実装手順:
1. screenshot 状態として
   `pending`
   と
   `busy`
   を保持する。
2. 必要なら最後の失敗理由を保持する内部結果コードを追加する。
3. `nesco_request_screenshot()`
   を実装し、
   idle 時のみ pending を受理する。
4. `busy`
   または
   `pending`
   中の追加要求は拒否する。
5. `infones/InfoNES.cpp`
   の
   `SCAN_VBLANK_START`
   内、
   `InfoNES_PadState(...)`
   の直後に
   `PAD_SYS_SCREENSHOT`
   の判定を追加する。
6. `PAD_PUSH(PAD_System, PAD_SYS_SCREENSHOT)`
   のとき
   `nesco_request_screenshot()`
   を呼ぶ。
7. 続けて
   `nesco_maybe_start_screenshot_on_vblank()`
   を呼ぶ。
8. この段階では、
   実 capture をまだ行わず、
   pending / busy / log だけでもよい。

完了条件:
- screenshot 要求が VBLANK まで遅延される。
- 通常フレーム時の追加コストが軽い分岐だけに留まる。
- busy / pending 中の連打でも多重開始しない。

## フェーズ 3: screenshot debug log 追加

目的:
- テスト仕様で定義された観測点を先に固定する。

変更対象:
- screenshot 制御モジュール
- `drivers/lcd_spi.c`
  または
  readback 実装部
- BMP 保存部

事実:
- 既存コードには
  `NESCO_LOGF(...)`
  の runtime log 経路がある。

実装手順:
1. screenshot 用 debug log helper を追加する。
2. 少なくとも次のログキーを出せるようにする。
   - `SS_REQ`
   - `SS_VBLANK`
   - `SS_DMA_WAIT_BEGIN`
   - `SS_DMA_WAIT_DONE`
   - `SS_CAPTURE_BEGIN`
   - `SS_CAPTURE_DONE`
   - `SS_SAVE_BEGIN`
   - `SS_SAVE_DONE`
   - `SS_ERROR`
3. default では常時表示しない設計にする。
4. テスト時にだけ有効化できるようにする。

完了条件:
- 正常系と異常系の流れをログ順で追える。
- ログ追加だけで機能責務が driver / storage / emulation 間に漏れない。

## フェーズ 4: BMP 保存モジュール追加

目的:
- LCD readback 完了後に保存できる出口を先に用意する。

変更対象:
- `platform/`
  配下の新規保存モジュール
- 必要なら header 追加

【推定】候補ファイル:
- `platform/screenshot_storage.c`
- `platform/screenshot_storage.h`

実装手順:
1. 既存の FatFs mount 利用実績を参照し、
   screenshot 保存でも
   `0:`
   を使う。
2. 保存先 directory を決める。
3. directory が未作成なら作成を試みる。
4. ファイル名を一意に生成する。
5. `storage_save_screenshot_bmp(...)`
   を実装する。
6. 入力は
   RGB565 `WORD`
   配列とし、
   内部で
   BGR24
   へ変換する。
7. BMP header、
   info header、
   row padding、
   bottom-up 行順を実装する。
8. `f_open()`
   `f_write()`
   `f_close()`
   の失敗時は
   `false`
   を返す。

完了条件:
- ダミー RGB565 バッファからでも
  `320x320`
  `24bpp BMP`
  を生成できる。
- 生成ファイルを PC ビューアで開ける。
- SD 書き込み失敗時に呼び出し側へ失敗を返せる。

## フェーズ 5: LCD readback 実装

目的:
- LCD GRAM から
  `320x320`
  を読み出す。

変更対象:
- `drivers/lcd_spi.c`
- 必要なら
  `drivers/lcd_spi.pio`
- screenshot 制御モジュール

事実:
- 現在の
  `lcd_set_window()`
  は write 用 helper である。
- 現在の
  `drivers/lcd_spi.pio`
  では readback 経路は確認できていない。

このフェーズは、
以下の順で固定する。

### 5-1. readback 前提の driver 内部整理

実装手順:
1. write helper と read helper を分離する。
2. readback 用に
   address range 設定だけを行う内部 helper を追加する。
3. readback で
   `lcd_set_window()`
   を使わないことをコード上でも固定する。

### 5-2. MISO / read クロック / read 経路の導入

【推定】実装手順:
1. LCD readback に必要な
   MISO
   入力設定を driver 初期化へ追加する。
2. read 時クロックを write 時より安全側へ下げる helper を追加する。
3. readback 用の byte 受信関数を追加する。
4. 既存 PIO を拡張するか、
   別 read 経路を設けるかをここで決める。

### 5-3. `lcd_readback_rect_rgb565()` の実装

実装手順:
1. 引数検証を入れる。
2. `lcd_dma_wait()`
   を先頭で必ず実行する。
3. read 用 window を設定する。
4. `RAMRD`
   を送る。
5. dummy read を吸収する。
6. 受信した byte 列を
   RGB565 `WORD`
   配列へ正規化する。
7. 失敗時は部分成功扱いにしない。
8. read クロックを通常値へ戻す。

### 5-4. screenshot フローへの接続

実装手順:
1. `nesco_maybe_start_screenshot_on_vblank()`
   内で
   `SS_DMA_WAIT_BEGIN`
   を出す。
2. `lcd_dma_wait()`
   実行後に
   `SS_DMA_WAIT_DONE`
   を出す。
3. `screenshot_busy = true`
   にする。
4. `SS_CAPTURE_BEGIN`
   を出して
   readback を呼ぶ。
5. 成功時のみ保存へ進む。
6. 失敗時は
   `SS_ERROR`
   を記録して復帰する。

完了条件:
- `lcd_dma_wait()`
  完了後にのみ readback が始まる。
- `320x320`
  RGB565 バッファが取得できる。
- readback 失敗時に保存へ進まず復帰できる。

## フェーズ 6: 文言更新と総合確認

目的:
- ユーザー向け表示と文書を実装内容へ追従させる。

変更対象:
- `platform/rom_menu.c`
- `README.md`
- 必要なら help / status 文言

実装手順:
1. help 表示の
   `F1/F5 : RESET GAME`
   を更新する。
2. `README.md`
   のキー説明を更新する。
3. screenshot 保存先や挙動を必要最小限追記する。

完了条件:
- 実装と表示が食い違わない。
- `F5`
  screenshot、
  `F1`
  reset がユーザー向け文言でも一致する。

## 実装順の stop / go 判定

この機能では、
各フェーズの終了時に次の判定を行う。

### go 判定

- フェーズ 1 終了時:
  `F5`
  reset 退避が成立している
- フェーズ 2 終了時:
  VBLANK 起点の pending 消化が成立している
- フェーズ 4 終了時:
  BMP writer 単体が動く
- フェーズ 5 終了時:
  readback が最低 1 枚分成立する

### stop 判定

- `F5`
  分離後も reset が誤発火する
- VBLANK 位置の変更で既存 reset / quit / view toggle が壊れる
- BMP writer 単体が壊れて保存系の検証が進められない
- LCD readback が成立せず、
  `RAMRD`
  経路の根拠が得られない

## 実装時の判断ルール

- screenshot 制御、
  LCD readback、
  BMP 保存の責務を混ぜない。
- `lcd_dma_wait()`
  は capture 開始前に必ず通す。
- readback では既存
  `lcd_set_window()`
  を使わない。
- `busy`
  と
  `pending`
  中の再要求は受理しない。
- まず
  「入力経路」
  「VBLANK 起点」
  「保存出口」
  を先に固め、
  readback は最後に入れる。

## この手順書が必要とする補助確認

【推定】:
- LCD controller の
  `RAMRD`
  挙動、
  dummy read 数、
  read 時安全クロックは、
  driver 実装前に別紙またはメモで固定すると手戻りが減る。
- それ以外の部分は、
  既存設計文書とこの手順書で着手に十分である。

## 総合判断

【推定】:
- 全体を新たに再設計する必要はない。
- この手順書を追加すれば、
  実装着手に必要な順序固定としては十分である。
- 実装は
  フェーズ 1
  から開始し、
  フェーズ 5
  の LCD readback だけを高リスク項目として個別管理するのが妥当である。
