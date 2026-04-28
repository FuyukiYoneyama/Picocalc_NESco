# Screenshot BMP viewer work plan 2026-04-27

## 目的

ROM menu から、過去に保存した screenshot BMP を PicoCalc 本体だけで確認できるようにする。

この機能は、復活の呪文、high score、動作確認 screenshot などを、PC へ SD card を移さなくても確認できるようにするための補助機能として扱う。

## 現在確認できている前提

- screenshot 保存先は `0:/screenshots/`。
- ROM menu / help 画面の screenshot は `NESCO_0001.BMP` のような prefix で保存される。
- game 中の screenshot は ROM stem を prefix にして保存される。
- 現在の screenshot BMP writer は `320x320`、`24bit BGR`、top-down BMP を出力する。
- ROM menu の text buffer は `menu_text_buffer_begin()` で動的確保し、menu 終了時に解放している。
- screenshot 保存側の chunk buffer は必要時に動的確保し、保存後に解放している。
- 作業 branch は `feature/screenshot-viewer`。

## 方針

ROM 一覧に screenshot BMP を混ぜない。

ROM menu 上で `F4` キーを押すと、`0:/screenshots/` 専用の screenshot viewer mode に入る。
この mode では `.BMP` ファイルだけを一覧表示し、`Enter` / `-` で選択中の BMP を表示する。
`ESC` で viewer mode を閉じて通常 ROM menu に戻る。

この方式を採用する理由:

- ROM 起動用の一覧を screenshot で圧迫しない。
- ROM 起動と screenshot 閲覧を明確に分けられる。
- 将来、viewer 専用の操作や status 表示を追加しやすい。

## 非目標

- JPEG / PNG viewer は実装しない。
- 任意サイズ BMP viewer は初期版の対象外にする。
- screenshot 以外の BMP 表示は保証しない。
- game 実行中に screenshot viewer を開く機能は実装しない。
- slideshow、削除、rename、thumbnail 表示は初期版では実装しない。

## 対象ファイル

主な実装対象:

- `platform/rom_menu.c`
- `platform/screenshot_storage.c`
- `platform/screenshot_storage.h`

必要に応じて追加:

- `platform/screenshot_viewer.c`
- `platform/screenshot_viewer.h`

初期実装では、BMP 読み込み処理が大きくなる場合は `screenshot_viewer.*` に分離する。
ROM menu 内に置くのは viewer mode の呼び出しと状態遷移だけにする。
`platform/screenshot_viewer.c`
を追加する場合は、
`CMakeLists.txt`
の
`PLATFORM_SOURCES`
にも追加する。

## UI / 操作

通常 ROM menu:

- `F4`: screenshot viewer mode に入る
- footer 表示は `F4 VIEW` に固定する

Screenshot viewer mode:

- `Up` / `Down`: BMP 一覧の選択移動
- `Enter` / `-`: 選択中 BMP を表示
- `ESC`: ROM menu に戻る
- `H` / `?`: 初期版では help には入れず、無視または status 表示だけにする

BMP 表示中:

- `ESC`: screenshot viewer list に戻る
- `Enter` / `-`: screenshot viewer list に戻る
- `Up` / `Down`: 初期版では無視する

## 一覧仕様

`0:/screenshots/` を開き、拡張子 `.BMP` / `.bmp` の通常 file だけを列挙する。

一覧 entry は最大 `MENU_MAX_ENTRIES` 件に制限する。
初期版では file name 昇順 sort は必須にしない。
FatFs の列挙順をそのまま使う。

ただし、後で見やすさに問題が出た場合は、次段階で file name sort を追加する。

entry の保存方法:

- screenshot viewer 専用の軽量構造体を追加する。
- 各 entry は
  `char name[64]`
  と
  `char path[128]`
  を持つ。
- `FILINFO.fname`
  の pointer は保持しない。
  必ず entry 内の固定 buffer へ copy する。
- `path`
  は列挙時に
  `0:/screenshots/<name>`
  として組み立てて固定 buffer に保存する。
- file name / path が buffer に収まらない場合は、その file を一覧に入れず skip する。
- entry 配列は stack に置かない。
  viewer mode 開始時に
  `malloc(sizeof(entry) * MENU_MAX_ENTRIES)`
  で確保し、viewer mode 終了時に必ず
  `free()`
  する。
- 確保失敗時は
  `NO MEMORY`
  を表示し、`ESC`
  で通常 ROM menu に戻れるようにする。

空の場合:

- 1 行だけ `NO SCREENSHOTS`
- detail は `0:/screenshots/*.BMP`
- `Enter` / `-` は無効

SD mount / directory open 失敗の場合:

- 1 行だけ `SCREENSHOT DIR FAILED`
- status line に FatFs 失敗状態を短く表示する
- `ESC` で通常 ROM menu に戻れること

## BMP 読み込み仕様

初期版で対応する BMP:

- signature: `BM`
- DIB header size: `40`
- width: `320`
- height: `-320` または `320`
- planes: `1`
- bit count: `24`
- compression: `BI_RGB` / `0`

`height < 0` の top-down BMP は、pixel data の先頭 row から LCD 上端へ描画する。
`height > 0` の bottom-up BMP は、表示 row ごとに
`f_lseek()`
で対応する file offset へ移動して読み、
LCD 上では上下反転して表示する。

row offset は次の式に固定する。

- top-down:
  `row_offset = pixel_offset + display_y * row_stride`
- bottom-up:
  `row_offset = pixel_offset + (height - 1 - display_y) * row_stride`

ここで
`display_y`
は LCD 上の
`0..319`
の行番号とする。

header 検証では、
`f_size()`
で file size を取得し、
`pixel_offset + row_stride * abs(height)`
が file size 以下であることを確認する。
範囲外の場合は破損 BMP とみなし、
`UNSUPPORTED BMP`
または
`READ FAILED`
で viewer list に戻る。

表示色:

- BMP の BGR888 を LCD 用 RGB565 に変換する。

読み込み buffer:

- 初期版は row buffer 方式にする。
- `row_stride = ((width * 3 + 3) / 4) * 4`
- `row_stride` 分だけ `malloc()` する。
- 表示終了時、または読み込み失敗時に必ず `free()` する。

理由:

- `320x320x2` の full RGB565 buffer は約 200KB になり、Mapper30 などの RAM 余裕を壊す可能性が高い。
- row buffer 方式なら 1KB 程度で済む。

## 描画方式

BMP viewer は fullscreen UI として扱う。

表示開始時:

1. `display_set_mode(DISPLAY_MODE_FULLSCREEN)`
2. `lcd_dma_wait()`
3. `lcd_set_window(0, 0, 320, 320)`
4. BMP を row 単位で読み込み、RGB565 line buffer へ変換して LCD へ送る
5. 各 row 送信後に
   `lcd_dma_wait()`
   してから同じ line buffer を再利用する
6. 最終 row 送信後にも
   `lcd_dma_wait()`
   する

line buffer は `WORD line[320]` を関数内 stack に持つ。
これは `display_clear_rgb565()` と同程度の stack 使用量であり、初期版では許容する。
`lcd_dma_write_rgb565_async()`
は非同期転送なので、line buffer を再利用する前に必ず
`lcd_dma_wait()`
する。

画像上に操作説明 text は描かない。
理由は、screenshot 内容をそのまま確認したいため。

## メモリ設計

常時確保:

- なし

viewer list 中:

- ROM menu 既存の text buffer
- screenshot file 一覧配列
  - `malloc(sizeof(entry) * MENU_MAX_ENTRIES)`
    で viewer mode 中だけ確保する
  - viewer mode 終了時に
    `free()`
    する

BMP 表示中:

- row buffer: 約 960 bytes
- LCD 送信用 line: `320 * sizeof(WORD)` = 640 bytes

禁止:

- `320x320` full image buffer の常時確保
- viewer 用 large static buffer の追加

## 実装フェーズ

各 phase の完了時に build し、そこで機能が破綻していないことを確認してから commit を切る。
手戻り可能な切れ目は次に固定する。

- Phase 1 完了後:
  `cmake --build build -j4`
  を実行し、viewer mode 入口だけの commit を作る。
- Phase 2 完了後:
  `cmake --build build -j4`
  を実行し、BMP 一覧表示までの commit を作る。
  ここを Phase 3 失敗時の戻り先にする。
- Phase 3 完了後:
  `make clean && make -j8`
  または同等の clean build
  で検証用 UF2 を作り、BMP 表示実装の commit を作る。

### Phase 1: viewer mode の入口だけ作る

- ROM menu の通常 footer に `F4 VIEW` を追加する。
- `F4` key code を追加する。
- `F4` で viewer mode に入り、`ESC` で ROM menu に戻る。
- viewer mode では仮の `NO SCREENSHOTS` 画面だけ表示する。

合格条件:

- ROM menu の通常 ROM 起動動線が変わらない。
- `F4` で viewer mode へ入る。
- `ESC` で ROM menu へ戻る。

### Phase 2: `0:/screenshots` の BMP 一覧を表示する

- `0:/screenshots` を `f_opendir()` する。
- `.BMP` / `.bmp` file だけを一覧へ入れる。
- `Up` / `Down` の移動は現在の ROM menu と同じページ送り規則を使う。
- page 移動 helper は初期版では viewer 側に小さく複製する。
  `rom_menu.c`
  の static helper を無理に共通化しない。
  将来、同じ処理が増えた時点で共通 module 化を検討する。
- `Enter` / `-` はまだ preview しない。
  選択中 entry の
  `path`
  を status line に表示する。

合格条件:

- `NESCO_*.BMP` と `<ROM_STEM>_*.BMP` が一覧に出る。
- BMP 以外の file は出ない。
- `Enter` / `-`
  で選択中 BMP の path が status line に出る。
- 0 件 / SD 失敗でも hang しない。

### Phase 3: BMP header parser と row 表示を追加する

- `320x320 24bit BI_RGB` BMP header を検証する。
- top-down / bottom-up の両方を扱う。
- row buffer を `malloc()` し、終了時に必ず `free()` する。
- `Enter` / `-` で選択 BMP を fullscreen 表示する。
- BMP 表示中は `ESC` または `Enter` / `-` で list へ戻る。

合格条件:

- ROM menu screenshot が表示できる。
- game screenshot が表示できる。
- unsupported BMP では viewer list に戻り、status line に `UNSUPPORTED BMP` を出す。
- 読み込み失敗時に ROM menu 操作へ戻れる。

### Phase 4: 実機検証と調整

実機検証は 2 回に分ける。
実機作業の回数は増えるが、Phase 3 の BMP parser / LCD 描画で問題が出た場合に Phase 2 の一覧表示まで戻れるようにする。

#### 実機検証 1: Phase 2 完了後の短時間確認

- ROM menu 起動
- `F4` で viewer mode に入る
- `NESCO_*.BMP` が一覧に出る
- game screenshot BMP が一覧に出る
- `Up` / `Down` で一覧移動できる
- `Enter` / `-`
  で選択中 BMP の path が status line に出る
- list から `ESC` で ROM menu に戻る
- ROM 起動が従来どおりできる

#### 実機検証 2: Phase 3 完了後の総合確認

- ROM menu 起動
- `F4` で viewer mode に入る
- `NESCO_*.BMP` が一覧に出る
- game screenshot BMP が一覧に出る
- ROM menu screenshot を表示できる
- game screenshot を表示できる
- BMP 表示から `ESC` で list に戻る
- list から `ESC` で ROM menu に戻る
- ROM 起動が従来どおりできる
- Mapper30 ROM 起動に必要な heap 余裕を壊していない

各実機検証前に version を patch bump する。
検証用 UF2 は clean build で作成し、banner と SHA-256 を記録する。

## 失敗時の切り分け

viewer mode に入れない:

- `F4` key code が PicoCalc keyboard から来ているかを debug code または UART で確認する。

一覧が出ない:

- `rom_image_ensure_sd_mount()` の成功可否を確認する。
- `f_opendir("0:/screenshots")` の `FRESULT` を一時的に status line へ出す。

BMP が表示できない:

- header の `width / height / bit_count / compression / pixel_offset` を一時的に status line または UART に出す。
- まず既存 screenshot writer が作った BMP だけで確認する。

表示が上下反転する:

- `biHeight` の符号処理を確認する。
- screenshot writer は top-down BMP を作っているため、まず `height < 0` の経路を正とする。

RAM 不足:

- full image buffer を使っていないか確認する。
- row buffer 確保失敗時は `NO MEMORY` を出して list に戻る。

## 実装時の注意

- viewer は ROM 起動機能ではないため、`rom_image_set_selected_path()` を呼ばない。
- viewer から通常 ROM menu に戻ったとき、ROM menu の選択位置と page は維持する。
- BMP 表示中にキー入力が溜まる場合は、表示終了時に `menu_discard_pending_keys()` を呼ぶ。
- screenshot 保存中の busy 音や F5 動作は変更しない。
- main へ merge / push する前に実機確認を行う。

## 完了条件

- 計画書レビューで重大指摘がない。
- build が通る。
- 実機で viewer mode、BMP 表示、ROM 起動復帰を確認する。
- `docs/project/Picocalc_NESco_HISTORY.md` に結果を記録する。
- `docs/project/TASKS.md` から active task を完了扱いへ移せる状態にする。
