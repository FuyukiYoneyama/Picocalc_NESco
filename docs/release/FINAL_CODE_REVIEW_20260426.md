# Final Code Review

この文書は、公開準備方針計画に基づく latest `main` の最終コードレビュー結果です。

## 対象

- `platform/main.c`
- `platform/infones_session.cpp`
- `platform/display.h`
- `platform/display.c`
- `platform/input.c`
- `platform/audio.c`
- `platform/rom_menu.c`
- `platform/rom_image.c`
- `platform/screenshot.h`
- `platform/screenshot.c`
- `platform/screenshot_storage.c`
- `platform/sram_store.cpp`
- `infones/InfoNES.cpp`
- `infones/InfoNES_Mapper.cpp`
- `infones/mapper/InfoNES_Mapper_030.cpp`
- `infones/K6502_rw.h`
- `infones/K6502.cpp`

## 確認観点

- 初期化順
- 起動直後
- ROM menu から game への遷移
- game から ROM menu への復帰
- mode 切替直後
- save / load
- screenshot
- mapper release / 再起動
- hot path
- 常時 RAM 使用量
- release blocker の有無

## 確認結果

- 起動順は `main.c` で display、input、audio、opening screen、ROM image、ROM menu loop の順に整理されている。
- game 起動は `rom_image_set_selected_path()` で選択 path を保持し、`run_infones_session()` から `InfoNES_Load()` へ入る。
- ROM load 失敗時は `InfoNES_Load()` 側で `sram_store_clear_session()` を呼び、session 状態を残さない。
- game 終了時は `InfoNES_ReleaseRom()` で save flush、mapper release、Mapper0 RAM buffer release、VROM buffer release、session clear を行う。
- `*.srm` restore / flush は `sram_store.cpp` にまとまっている。
- Mapper30 の `*.m30` restore / flush 実装は存在するが、実ゲームでの保存 / 復元は未確認事項として扱う。
- screenshot は game 中の pending / vblank path と、ROM menu の immediate path が分かれている。
- screenshot chunk buffer は capture 中に `malloc()` し、終了時に `free()` する。
- ROM menu screenshot は busy indicator sound と key discard を挟み、保存中 key event が保存後操作として残りにくい形になっている。
- normal / stretch 表示切替は `PAD_SYS_VIEW_TOGGLE` から `display_toggle_nes_view_scale()` へ入り、viewport と frame pacing を reset する。
- display hot path は scanline ごとに line buffer を pack し、8 source lines ごとに LCD DMA strip を送る。
- default runtime log は `NESCO_RUNTIME_LOGS` で抑制されている。`rom_image.c` と `rom_menu.c` の直接 `printf()` は default では macro により無効化される。
- BokosukaWars trace / structured log 系の直接出力は compile-time flag 側に閉じている。
- 常時 RAM を増やす変更は今回行っていない。

## Finding

release blocker は確認していない。

## 残す未確認事項

- `Map6`
- `Map19`
- `Map185`
- `Map188`
- `Map235`
- Mapper30 の `*.m30` 書き込み / 復元
- Mapper30 の実ゲーム save / restore 運用
- Mapper87 / Choplifter 系の追加確認
