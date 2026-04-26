# ARCHITECTURE.md

この文書は `Picocalc_NESco` の現在の構成と、作業時に守る責務境界をまとめます。

## 目標

現在の目標は、PicoCalc 上で動く、公開可能な GPL ライセンスの NES エミュレーターです。

旧 `core` 路線は中止し、現在は `infones` を本体として PicoCalc platform へ接続する方針です。
接続設計の正本は `docs/design/INFONES_PLATFORM_CONNECTION_PLAN_20260419.md` です。

## 構成

- `infones/`
  - NES emulator 本体。
  - mapper、CPU、PPU、APU の基礎実装を持つ。
- `platform/`
  - PicoCalc 向け接続層。
  - ROM menu、display、input、audio、save、screenshot、InfoNES bridge を持つ。
- `drivers/`
  - LCD SPI、PWM audio、SD card、I2C keyboard などの低レベル driver。
- `fatfs/`
  - SD card file system。
- `font/`
  - ROM menu / status 表示用 font。
- `docs/`
  - 設計文書、audio 方針、画像、test spec。
- `core/`
  - 現在の active target source には入っていない旧系統。
  - 経緯確認用として残す。
  - active source tree や clean-room emulator core として再利用する場合は、別途確認する。
  - 詳細は `core/README.md` と `docs/project/Picocalc_NESco_HISTORY.md` を参照する。

## 起動と ROM 実行の流れ

1. `platform/main.c` が entry point として起動する。
2. LCD、input、audio、ROM image 管理を初期化する。
3. `picocalc_rom_menu()` で ROM を選ぶ。
4. `rom_image_set_selected_path()` で選択 ROM path を保存する。
5. `run_infones_session()` で InfoNES 実行へ入る。
6. `platform/infones_session.cpp` の `InfoNES_Menu()` adapter が `InfoNES_Load(selected_path)` を呼ぶ。

## 重要な責務境界

- `main.c` は C の entry point として残す。
- C++ 側へは `platform/infones_bridge.h` が公開する bridge API だけで入る。
- `InfoNES_Menu()` は `platform/infones_session.cpp` に置く薄い adapter とする。
- `rom_menu.c` は `InfoNES_Menu()` から直接呼ばない。
- `InfoNES_ReadRom()` は暗黙状態ではなく、`InfoNES_Load(selected_path)` から渡された `path` を読む契約で扱う。
- `InfoNES_Error()` の所有先は `platform/infones_session.cpp` に一本化する。
- `FrameSkip` と `InfoNES_SetLineBuffer()` は `infones` core 側の所有物として扱う。
- `infones` は `STATIC` library として組み込む。
- `InfoNES_Mapper.cpp` は mapper 個別 `.cpp` を `#include` で内包しているため、`mapper/*.cpp` を別途 `target_sources` に重ねて列挙しない。
- `platform/display.c`、`platform/audio.c`、`platform/input.c`、`platform/rom_image.c`、`platform/screenshot.c`、`platform/screenshot_storage.c` は、拡張子は `.c` のまま、CMake の `LANGUAGE CXX` 指定で C++ として build する。
- 上記 file は PicoCalc platform と C driver / C++ bridge の境界にあるため、公開前には `.cpp` へ改名しない。

## 領域別の確認観点

- `PPU / DrawLine`
  - `WorkLine` の設定位置。
  - 背景 / sprite 合成順。
  - 背景不透明情報の初期化。
  - palette 形式変換位置。
- `LCD / Display`
  - viewport。
  - 転送サイズ。
  - stretch 時の幅 / 高さ / strip。
  - `lcd_dma_acquire_buffer()` への書き込み範囲。
  - `lcd_dma_wait()` の位置。
- `Input`
  - 押下 edge だけ反応すべきキーか、HOLD でも反応すべきキーか。
  - menu と game の意味衝突がないか。
- `Mapper`
  - init / release の対称性。
  - `malloc/new` 失敗時処理。
  - `SRAMBANK / PPUBANK / ROMBANK` 更新漏れ。
  - ROM 再起動時の状態残留。
- `SRAM / ROM / Menu`
  - menu 用確保の解放順。
  - SRAM restore / flush 順序。
  - flash / RAM 両 backend で壊れないか。

## 互換性確認の基準

互換性確認の最小セットは次を基準にします。

- 実ゲーム:
  - `LodeRunner.nes`
  - `Xevious.nes`
- 補助 test ROM:
  - `ppu_vbl_nmi/02-vbl_set_time.nes`
  - `ppu_vbl_nmi/03-vbl_clear_time.nes`
  - `ppu_vbl_nmi/05-nmi_timing.nes`

小さな速度実験の主ゲートには使わず、互換性変更の非退行確認に使います。
