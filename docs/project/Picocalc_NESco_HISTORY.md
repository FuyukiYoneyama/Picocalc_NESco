# Picocalc_NESco 履歴

この文書は、`Picocalc_NESco` の採用 / 不採用判断、実装経緯、完了した作業結果を残す履歴の正本です。

- 役割:
  - 進行中の作業予定は `docs/project/TASKS.md` を正本とする
  - 完了した作業は `docs/project/TASKS.md` から外し、結果つきでこの文書へ移す
  - 要点は `git note` にも残すが、長文の経緯はこの文書を正本とする
- 注意:
  - ここには `HEAD` に残っている変更と、あとで戻した実験の両方を書く
  - 戻した実験は「現在の採用状態ではない」と明記する

## 1.1.7 background full tile direct path 実験ブランチ (2026-04-29)

- `feature/hot-path-metrics` で、background full tile path の descriptor 経由を一部避ける実験を追加した
- 目的は、`1.1.6` の `renderBgTileFull()` 分離だけでは効果が小さかったため、full tile で `BgTileDescriptor` を構築せずに描画へ進めるかを確認すること
- 変更内容:
  - `renderBgTileFullDirect()` を追加した
  - full tile の場合は `emitBgTile()` 側で `patternRow` を直接求め、`renderBgTileFullDirect(patternRow, pal, dst, dstOpaque)` を呼ぶ
  - partial tile path は従来どおり `BgTileDescriptor` と `renderBgTile()` を使う
  - `MapperPPU()` の呼び出し順は変更していない
- system version を `1.1.7` に更新した
- build 確認:
  - `NESCO_CORE1_BASELINE_LOG=ON`
  - banner: `PicoCalc NESco Ver. 1.1.7 Build Apr 29 2026 08:58:58`
  - UF2 SHA-256: `0aa02fee28756a76b5626ce54d2a1453c004ec8ce232f9b3e9e2a5f074bd331e`
  - ELF SHA-256: `10cf7370aaae610aabb0a673ac8c0875f75112ddf8597471189bea8ed4be3c99`
  - `.bss = 97308`
  - `1.1.6` から `.bss` は変化なし

## 1.1.6 `renderBgTile()` full tile path 小改善ブランチ (2026-04-29)

- `feature/hot-path-metrics` で、`renderBgTile()` の full tile path を小さく分離した
- 目的は、`1.1.5` の詳細計測で重かった background tile render 部分を、画面意味を変えずに少し軽くできるか確認すること
- 変更内容:
  - `renderBgTileFull()` を追加した
  - `renderBgTileFull()` は `__not_in_flash_func` 付き、`always_inline` 指定
  - full tile path だけ `renderBgTileFull()` に分離した
  - `renderBgTileFull()` 内で `desc.pal` を local `pal` pointer に保持するようにした
  - partial tile path と `MapperPPU()` 呼び出し順は変更していない
- system version を `1.1.6` に更新した
- build 確認:
  - `NESCO_CORE1_BASELINE_LOG=ON`
  - banner: `PicoCalc NESco Ver. 1.1.6 Build Apr 29 2026 08:47:06`
  - UF2 SHA-256: `6791ffdb710f5a123c13f7ee7ba994b1b30ec1e6ab85079d8ea7f749f5cf180f`
  - ELF SHA-256: `721b5076d53ae758c18787fe89616e26e8cf81582a66d1eaf488fe0f1b00eee8`
  - `.bss = 97308`
  - `1.1.5` から `.bss` は変化なし
- 実機比較:
  - log: `/home/fuyuki/pico_dvl/codex/log/pico20260429_085008.log`
  - user 操作: `LodeRunner`、`Xevious`、`Project_DART_V1.0` を順に play
  - `[ROM_START]` で確認した ROM:
    - `LodeRunner.nes`: mapper 0
    - `Xevious.nes`: mapper 0
    - `Project_DART_V1.0.nes`: mapper 30
  - `1.1.5` からの比較:
    - `LodeRunner.nes`: fps +0.41%、`frame_us_avg` -0.33%、`ppu_bg_tile_render_us` -0.96%
    - `Xevious.nes`: fps +0.33%、`frame_us_avg` -0.40%、`ppu_bg_tile_render_us` +0.13%
    - `Project_DART_V1.0.nes`: fps +0.16%、`frame_us_avg` -0.20%、`ppu_bg_tile_render_us` -0.23%
  - 判断:
    - 画面意味を変えない小変更としては build 上問題ない
    - ただし高速化効果は測定誤差程度で、単独では有効な改善とは判断しない
    - 次は `BgTileDescriptor` を経由しない full tile direct path を確認する

## 1.1.5 PPU background tile 内訳計測ブランチ (2026-04-29)

- `feature/hot-path-metrics` の `[CORE1_BASE]` に、`ppu_bg_tile_us` の内訳メトリクスを追加した
- 目的は、背景 tile hot path のうち、palette 解決、tile descriptor 構築、tile render、`MapperPPU()` のどこが主な時間を使っているかを実機ログで切り分けること
- 追加した項目:
  - `ppu_bg_tile_pal_us`
  - `ppu_bg_tile_build_us`
  - `ppu_bg_tile_render_us`
  - `ppu_bg_mapperppu_us`
  - `ppu_bg_tile_count`
- この計測は `emitBgTile()` 内で tile ごとに `time_us_64()` を呼ぶため、通常実行より計測 overhead が増える
- system version を `1.1.5` に更新した
- build 確認:
  - `NESCO_CORE1_BASELINE_LOG=ON`
  - banner: `PicoCalc NESco Ver. 1.1.5 Build Apr 29 2026 08:25:16`
  - UF2 SHA-256: `2ad7c44fe35d5a76ccf710861c893b197706268fc7ede1b10c42caeaeb445f3a`
  - ELF SHA-256: `dfb1fa1354fd390a7731a23e0d58ea532ba8a65984850792865cbc7c2a4ee880`
  - `.bss = 97308`
  - `1.1.4` から `.bss` は 36 bytes 増加した
- 実機計測 baseline:
  - log: `/home/fuyuki/pico_dvl/codex/log/pico20260429_082720.log`
  - user 操作: `LodeRunner`、`Xevious`、`Project_DART_V1.0` を順に play
  - `[ROM_START]` で確認した ROM:
    - `LodeRunner.nes`: mapper 0
    - `Xevious.nes`: mapper 0
    - `Project_DART_V1.0.nes`: mapper 30
  - ROM 別平均:
    - `LodeRunner.nes`: fps 21.0、`draw_us/frame` 27.5ms、`ppu_bg_tile_us/frame` 19.7ms、`ppu_bg_tile_render_us/frame` 5.8ms、`ppu_bg_tile_pal_us/frame` 2.5ms、`ppu_bg_tile_build_us/frame` 1.9ms、`ppu_bg_mapperppu_us/frame` 1.4ms
    - `Xevious.nes`: fps 24.9、`draw_us/frame` 27.8ms、`ppu_bg_tile_us/frame` 19.6ms、`ppu_bg_tile_render_us/frame` 5.7ms、`ppu_bg_tile_pal_us/frame` 2.5ms、`ppu_bg_tile_build_us/frame` 1.9ms、`ppu_bg_mapperppu_us/frame` 1.4ms
    - `Project_DART_V1.0.nes`: fps 20.3、`draw_us/frame` 27.1ms、`ppu_bg_tile_us/frame` 18.9ms、`ppu_bg_tile_render_us/frame` 5.7ms、`ppu_bg_tile_pal_us/frame` 2.3ms、`ppu_bg_tile_build_us/frame` 1.8ms、`ppu_bg_mapperppu_us/frame` 1.3ms
  - 判断:
    - `MapperPPU()` は約 1.3ms から 1.4ms / frame で、最初の改善対象にはしない
    - 最初の改善対象は `renderBgTile()` full tile path とする
    - `1.1.5` は tile ごとの `time_us_64()` 呼び出しが多いため、fps は通常性能ではなく内訳確認用として扱う

## 1.1.4 PPU background breakdown 計測ブランチ (2026-04-29)

- `feature/hot-path-metrics` の `[CORE1_BASE]` に、`ppu_bg_us` の内訳メトリクスを追加した
- 目的は、背景描画の主な時間が mapper 切替、clear、setup、tile render、clip のどこにあるかを実機ログで切り分けること
- 追加した項目:
  - `ppu_bg_mapper_us`
  - `ppu_bg_clear_us`
  - `ppu_bg_setup_us`
  - `ppu_bg_tile_us`
  - `ppu_bg_clip_us`
- 計測は scanline 内の大きな区間単位で行い、tile 1 個ごとの計測は避けた
- system version を `1.1.4` に更新した
- build 確認:
  - `NESCO_CORE1_BASELINE_LOG=ON`
  - banner: `PicoCalc NESco Ver. 1.1.4 Build Apr 29 2026 08:12:20`
  - UF2 SHA-256: `7f38030b3a0a0feaf178bdc0626e58cb53b2ad67aeb17d81179a3a11a2ea8730`
  - ELF SHA-256: `6c7e8d579b04a13fa5b11d39ca3d86be9aae595a025dfe15b4f2a383be03c9cd`
  - `.bss = 97272`
  - `1.1.3` から `.bss` は 40 bytes 増加した

## 1.1.3 ROM start log 計測ブランチ (2026-04-29)

- `feature/hot-path-metrics` の計測ログに、game 開始時 1 回だけ `[ROM_START]` を出す処理を追加した
- 目的は、複数 ROM を連続して実機確認した UART log でも、後から `[CORE1_BASE]` の計測区間を ROM 名と紐づけられるようにすること
- `InfoNES_Load()` 成功後、`DISPLAY_MODE_NES_VIEW` へ切り替えた直後に次の形式で出力する
  - `[ROM_START] name=... path=... mapper=... prg16=... chr8=... battery=... trainer=...`
- `InfoNES_Load()` 失敗時は `[ROM_START]` を出さず、core1 service も有効化しない
- system version を `1.1.3` に更新した
- build 確認:
  - `NESCO_CORE1_BASELINE_LOG=ON`
  - banner: `PicoCalc NESco Ver. 1.1.3 Build Apr 29 2026 08:02:42`
  - UF2 SHA-256: `ce4e96ed88d7ae28887fe5ebe9c3d852ddcca903d5c078b0a36f635575591474`
  - ELF SHA-256: `31e9696c1505b7adf8f7fbfef0e64aa8692d29199e155f63d64a544150d322ef`
  - `.bss = 97232`

## 1.1.2 hot path metrics 計測ブランチ (2026-04-29)

- `feature/hot-path-metrics` を作成し、frame rate 改善調査用の hot path メトリクスを追加した
- `1.1.1` の待ち時間メトリクスに加え、`NESCO_CORE1_BASELINE_LOG=ON` の `[CORE1_BASE]` に次の項目を追加した
  - `cpu_us`
  - `apu_us`
  - `draw_us`
  - `ppu_bg_us`
  - `ppu_sprite_us`
  - `mapper_hsync_us`
  - `mapper_vsync_us`
  - `load_frame_us`
  - `tail_us`
- 目的は、FPS 低下の主因が CPU emulation、PPU background / sprite rendering、mapper hook、frame submit 周辺のどこにあるかを実機ログで切り分けること
- system version を `1.1.2` に更新した
- build 確認:
  - `NESCO_CORE1_BASELINE_LOG=ON`
  - banner: `PicoCalc NESco Ver. 1.1.2 Build Apr 29 2026 07:58:08`
  - UF2 SHA-256: `96579841092fd61fc55228b4d4543fee652b1ada6b4324de04f0e9f4e6317e75`
  - ELF SHA-256: `51374db69cef796961afcb9ce5c694f1e9b0e14750373e06c1a4cd84e866db2c`
  - `.bss = 97232`

## 1.1.1 frame wait metrics 計測ブランチ (2026-04-29)

- `feature/frame-wait-metrics` を作成し、frame rate 改善調査用の待ち時間メトリクスを追加した
- 目的は、無駄に待っている箇所があるかを実機ログで切り分けること
- `NESCO_CORE1_BASELINE_LOG=ON` の `[CORE1_BASE]` に次の項目を追加した
  - `lcd_queue_wait_us`
  - `lcd_queue_wait_count`
  - `frame_pacing_sleep_us`
  - `frame_pacing_sleep_count`
  - `audio_wait_us`
  - `audio_wait_count`
- `display_perf_snapshot()` を拡張し、LCD worker queue full 待ちと frame pacing sleep を取得できるようにした
- `audio_perf_reset()` / `audio_perf_snapshot()` を追加し、audio ring full 待ちを取得できるようにした
- system version を `1.1.1` に更新した
- build 確認:
  - `NESCO_CORE1_BASELINE_LOG=ON`
  - banner: `PicoCalc NESco Ver. 1.1.1 Build Apr 29 2026 02:07:16`
  - UF2 SHA-256: `b6c72449baa4bffb581bf2b65e2f3aa9ac376dac73e6c461a04adff35b56ed77`
  - ELF SHA-256: `0aa89ceb48693ea85e68472078059b399d19bb94be2159939bf1de6571261f2b`
  - `.bss = 97192`

## 1.1.0 screenshot viewer / core1 LCD worker 採用版 (2026-04-29)

- BMP screenshot viewer と core1 LCD worker 採用をまとめた節目として、system version を `1.1.0` に更新した
- `1.1.0` は機能追加版として扱う
- 含める主な内容:
  - ROM menu から `0:/screenshots/*.BMP` を選択して表示する screenshot viewer
  - core1 keyboard polling
  - core1 LCD worker normal 表示
  - core1 LCD worker stretch 表示
- `1.0.15` で確認した core1 LCD worker 採用判断を `1.1.0` の根拠にする
- README の現在 version と特徴欄を `1.1.0` に合わせて更新した
- build 確認:
  - banner: `PicoCalc NESco Ver. 1.1.0 Build Apr 29 2026 01:28:53`
  - UF2 SHA-256: `2d5cdf93fd64c2cc729ed445c804daa3330a21aa174f10c51e223ae7ef5395fc`
  - ELF SHA-256: `de1aca323d91e7af22a54c5c62c3e75fcfa362f0810b0f5195d762162f3be515`
  - `.bss = 97096`

## 1.0.15 core1 LCD worker 採用判断 (2026-04-29)

- `feature/core1-lcd-worker` の Phase 2 / Phase 3 / Phase 4 実装を採用する判断を行った
- 採用する内容:
  - game 中は core1 keyboard polling を維持する
  - normal 表示では core0 が scanline を queue へ copy し、core1 が RGB565 pack と LCD DMA kick を行う
  - stretch 表示では core1 側で 320px pack と縦 repeat を行う
  - fullscreen UI、screenshot、mode 切替、ROM menu return の入口では LCD worker を drain する
- 実機確認:
  - DART、LodeRunner、Xevious、Xevious stretch で計測を行った
  - normal / stretch とも表示は正常
  - Shift+W による normal / stretch 切替は正常
  - F5 screenshot は normal / stretch の両方で保存できる
  - ESC で ROM menu へ戻れる
  - 音と入力の悪化は確認されなかった
- 計測結果:
  - DART normal: 平均 `39.32 fps`
  - LodeRunner normal: 平均 `42.73 fps`
  - Xevious normal: 平均 `48.47 fps`
  - Xevious stretch: 平均 `36.34 fps`
- 静的 RAM 確認:
  - `main` / `0289e7d`: `.bss = 94904`
  - `feature/core1-lcd-worker` / `1.0.15` / `NESCO_CORE1_BASELINE_LOG=OFF`: `.bss = 97096`
  - 差分は `.bss +2192 bytes`
  - 主因は `s_lcd_worker_queue` の `2160 bytes`
  - `core1_stack` は main 側にも存在するため、今回の LCD worker 追加による増加分ではない
  - `__end__ = 0x20022228`、`__HeapLimit = 0x20040000` で、静的領域末尾から heap limit までは計算上 `122328 bytes` 残る
- 通常 build:
  - `NESCO_CORE1_BASELINE_LOG=OFF`
  - banner: `PicoCalc NESco Ver. 1.0.15 Build Apr 29 2026 01:16:00`

## 1.0.15 core1 LCD worker stretch 表示実験 (2026-04-29)

- `docs/design/CORE1_PARALLELIZATION_WORK_PLAN_20260428.md` の Phase 4 に従い、core1 LCD worker を stretch 表示にも拡張した
- queue entry に `scale_mode` を持たせ、core1 側で normal / stretch の pack を分岐するようにした
- stretch 表示では core1 側で 320px pack と縦 repeat を行う
- Shift+W による mode 切替前には worker drain を行う
- 実機確認:
  - normal 表示は正常
  - Shift+W で stretch 表示へ切り替わる
  - stretch 表示で画面崩れはない
  - もう一度 Shift+W で normal 表示へ戻れる
  - F5 screenshot は normal / stretch の両方で撮れる
  - ESC で ROM menu へ戻れる
  - 音切れや入力遅延は悪化していない
- build 確認:
  - banner: `PicoCalc NESco Ver. 1.0.15 Build Apr 29 2026 00:54:58`
  - UF2 SHA-256: `9b4e6d6cac7efae4707ad87967580f5bef3f13a7a11dae6dcc2df82a7c67dc47`

## 1.0.14 core1 LCD worker normal 表示実験 (2026-04-29)

- `docs/design/CORE1_PARALLELIZATION_WORK_PLAN_20260428.md` の Phase 3 に従い、normal 表示だけを core1 LCD worker 経由にした
- core0 は scanline を queue に copy し、core1 が RGB565 pack と LCD DMA kick を行う
- stretch 表示は従来の core0 表示経路のまま残した
- 初期実装では起動直後の表示が不安定だったため、LCD driver の DMA buffer を `lcd_dma_acquire_buffer()` で取得して使うよう修正した
- 初期実装では遅かったため、LCD queue に処理がある間は core1 が連続処理し、queue が空のときだけ短く待つよう調整した
- keyboard polling は 1ms 周期を維持した
- 実機確認:
  - 起動直後の画面は正常
  - normal 表示は初期版より改善
  - screenshot は保存できる
  - ESC で ROM menu へ戻れる
  - 音は正常
- build 確認:
  - banner: `PicoCalc NESco Ver. 1.0.14 Build Apr 29 2026 00:48:55`
  - UF2 SHA-256: `67007f33bc0dada14ed2beec9fb432c54a84342b1fb7c146ecc5e5a224fee774`

## 1.0.11 core1 LCD worker Phase 2 ownership 整理 (2026-04-29)

- `docs/design/CORE1_PARALLELIZATION_WORK_PLAN_20260428.md` の Phase 2 に従い、LCD worker 実験前の所有権整理を行った
- `display_lcd_worker_state_t`、`display_lcd_worker_stop_and_drain()`、`display_lcd_worker_prepare_nes_view()` を追加した
- fullscreen UI、Shift+W 表示切替、screenshot capture の入口で LCD worker drain を通す接続点を追加した
- この段階では実際の core1 LCD 転送は行わず、`CORE1_SERVICE_LCD` も有効化していない
- build 確認:
  - banner: `PicoCalc NESco Ver. 1.0.11 Build Apr 29 2026 00:13:13`
  - UF2 SHA-256: `480b8ef5f44246c0d08bacdc258a45390fab4ba4a4d419c26de7ad34b1d7c9c1`
- 実機確認は Phase 3 の normal LCD worker 実験時へまとめる

## 1.0.10 core1 keyboard polling / flash staging 安定化 (2026-04-28)

- core1 keyboard polling 導入後、DART / TOWER の SD ファイル起動時にキー入力が効かない問題を調査した
- 実機確認により、同じ ROM でも `SYSTEM FLASH` から起動した場合はキー入力が効き、SD ファイルから選んだ場合だけ問題が出ることを確認した
- 原因は Mapper30 固有処理ではなく、SD から大きな ROM を flash staging する経路で、flash erase / program 中に core1 が動作し続けることだった可能性が高い
- `pico_multicore` の `multicore_lockout` を使い、flash erase / program 中は core1 を lockout するようにした
- core1 worker 側では `multicore_lockout_victim_init()` を呼ぶようにした
- 既に flash に staging 済みの ROM と、選択された SD ファイルの `source_path` / size / mapper が一致する場合は、再 staging せず既存 flash ROM を再利用するようにした
- 一時的に core1 keyboard polling を `4ms` 周期 / 最大 4 events に抑える案も試したが、flash lockout 後は元の `1ms` 周期 / FIFO 全 drain でも DART と TOWER が動くことを実機確認したため、polling cadence 変更は採用しなかった
- 実機確認:
  - DART: SD ファイル起動 / SYSTEM FLASH 起動の両方でキー入力が効く
  - TOWER: SD ファイル起動 / SYSTEM FLASH 起動の両方でキー入力が効く
- 2026-04-29 に、DART と TOWER が SD からも flash からも動作することを再確認した

## 0.5 文書整理メモ (2026-04-19)

- `docs/project/PROGRESS_TODO.md`
  は入口文書として整理し、
  再開基準、注意点、現在の TODO のみを残す形へ縮約した
- そのため、
  `docs/project/PROGRESS_TODO.md`
  に以前あった
  `方針転換`
  `現在の基準点`
  `過去の優先順`
  `現在の状態`
  `完了`
  `進行中`
  `更新履歴`
  相当の履歴は、
  以後この
  `docs/project/Picocalc_NESco_HISTORY.md`
  を正本として読む
- 旧
  `core`
  路線の経緯も、
  `infones`
  移行判断の前史としてこの文書に保持する

## 0.6 `infones` 移行開始メモ (2026-04-19)

- `docs/project/PROGRESS_TODO.md`
  と
  `docs/design/INFONES_PLATFORM_CONNECTION_PLAN_20260419.md`
  に従って、
  実際の移行作業を開始した
- Phase 0 として、
  `Picocalc_NESco`
  配下が git 管理下であること、
  既存
  `build/`
  が Pico SDK 付きで存在すること、
  現在のトップ
  `CMakeLists.txt`
  が
  `core/*.c`
  と
  `nesco_host`
  前提であることを確認した
- Phase 1 として、
  ルート
  `CMakeLists.txt`
  から emulator core source の
  `core/*.c`
  列挙を外し、
  `add_subdirectory(infones)`
  と
  `infones`
  link を追加した
- 同時に
  `infones/CMakeLists.txt`
  は
  `INTERFACE`
  から
  `STATIC`
  library へ変更し、
  `InfoNES_Mapper.cpp`
  だけを source に含める形へ切り替えた
- `cmake -S . -B build`
  は成功し、
  Phase 1 後の最初の build では
  `platform/main.c`
  の
  旧
  `InfoNES_Main(rom_path)`
  呼び出しだけが
  undefined reference になった
- Phase 2 の最小実装として、
  `platform/infones_bridge.h`
  と
  `platform/infones_session.cpp`
  を追加し、
  `run_infones_session()`
  と
  `InfoNES_Menu()`
  などの薄い adapter を入れた
- `platform/main.c`
  は
  `InfoNES_Main(rom_path)`
  直呼びをやめ、
  `rom_image_set_selected_path()`
  と
  `run_infones_session()`
  を使う形へ変更した
- `platform/rom_image.c`
  には
  `selected_path`
  管理の最小 helper を追加した
- この時点の build failure は、
  `InfoNES_ReadRom()`
  `InfoNES_ReleaseRom()`
  `InfoNES_LoadFrame()`
  `InfoNES_PadState()`
  `InfoNES_PreDrawLine()`
  `InfoNES_PostDrawLine()`
  `InfoNES_Sound*`
  `NesPalette`
  `micromenu`
  など、
  未接続の
  `InfoNES_System.h`
  hook と
  `infones`
  側公開記号へ整理された
- したがって、
  次段は
  `input`
  `audio`
  `display`
  `rom_image`
  を
  C++
  側契約へ寄せる作業に進む

## 0.17 `InfoNES` 接続作業を一旦完了扱いにした (2026-04-19)

- `0.1.8`
  時点で、
  `Picocalc_NESco`
  に対する
  `infones`
  の接続作業は一旦完了扱いとした
- 完了基準は、
  `infones`
  core の組み込み、
  PicoCalc
  側
  platform との接続、
  ROM menu →
  game 起動 →
  実機表示までが成立したことである
- 色、
  ゲーム表示位置、
  ROM menu 残像、
  メモリ構成、
  `Mapper 235`
  一時 disable、
  実時間 pacing、
  可変 `FrameSkip`
  までを含めて、
  接続作業として必要な境界は通したと整理した
- 今後の作業は、
  接続そのものではなく、
  ゲームのフィール、
  音質、
  互換性、
  実機での細部調整として扱う
- 未完了項目は
  `docs/project/PROGRESS_TODO.md`
  側で、
  接続後の runtime tuning /
  session 検証 /
  互換性確認として継続管理する

## 0.18 `0.1.8` 接続後の session 動作確認が進んだ (2026-04-19)

- `0.1.8`
  の実機ログでは、
  `cpu_pct`
  は
  おおむね
  `100%`
  /
  `101%`
  付近、
  `frames`
  は
  `44`
  から
  `49`
  付近で推移している
- ゲーム終了後に
  ROM menu
  へ戻り、
  直前に読んだ
  `*.nes`
  のディレクトリを初期表示できることを確認した
- 終了 →
  ROM 選択 →
  再起動の流れが成立し、
  `F1`
  reset によるゲーム再起動も確認できた
- したがって、
  `quit`
  /
  `reset`
  /
  再起動の session 遷移は一旦確認済みと整理した
- 残る主な項目は、
  ゲームのフィール、
  音質、
  最小互換性セットを含む runtime tuning /
  compatibility 側である

## 0.19 `0.1.9` build 推定 heap 領域を起動時に表示するようにした (2026-04-19)

- system version を
  `0.1.9`
  へ更新した
- `platform/rom_image.c`
  に、
  linker symbol
  `__end__`
  と
  `__HeapLimit`
  を使って、
  build 時点で推定される heap 範囲を表示する
  `rom_image_log_heap_estimate()`
  を追加した
- `platform/main.c`
  では、
  `rom_image_init`
  後、
  ROM menu
  へ入る前に
  この推定 heap 範囲を一度表示するようにした
- 目的は、
  `mallinfo()`
  の
  `arena/free`
  だけでは見えにくい
  RAM /
  heap 構造を、
  その build の linker 結果に近い形で実機ログへ出すことである
- この変更後も
  `cmake --build build -j4`
  は成功している

## 0.20 `0.1.10` `PICO_HEAP_SIZE` を少し広げて挙動を確認する (2026-04-19)

- system version を
  `0.1.10`
  へ更新した
- `CMakeLists.txt`
  で
  `PICO_HEAP_SIZE=0x1000`
  を定義し、
  Pico SDK
  本体を触らずに
  linker 予約 heap を少し広げる試行を入れた
- 目的は、
  `malloc`
  の最大値に対して
  `CMakeLists.txt`
  側の調整が効くかを、
  小さい変更で観測することである
- この build では、
  map の
  `.heap`
  と
  実機ログの
  `heap-est`
  /
  `mallinfo`
  を比較して評価する

## 0.21 `0.1.11` 最大連続 `malloc` probe を追加した (2026-04-19)

- system version を
  `0.1.11`
  へ更新した
- `heap-est`
  は、
  `__end__`
  から
  `__HeapLimit`
  までの
  gap だけでなく、
  `PICO_HEAP_SIZE`
  に基づく
  reserve end /
  reserve bytes も同時に表示する形へ広げた
- あわせて、
  起動直後に一度だけ、
  最大連続
  `malloc`
  を二分探索する
  `heap-probe`
  を追加した
- この probe を安全に行うため、
  project 側の build 定義として
  `PICO_MALLOC_PANIC=0`
  を入れ、
  allocation failure を panic ではなく
  `NULL`
  で観測できるようにした
- 目的は、
  linker 由来の推定値と、
  実際に通る最大連続
  `malloc`
  を突き合わせることである

## 0.22 `0.1.12` `PICO_HEAP_SIZE` を極小にして逆方向を試す (2026-04-19)

- system version を
  `0.1.12`
  へ更新した
- `CMakeLists.txt`
  の
  `PICO_HEAP_SIZE`
  を
  `0x1000`
  から
  `0x100`
  へ下げた
- 目的は、
  `PICO_HEAP_SIZE`
  が
  実効
  `malloc`
  ceiling の上限に効くなら、
  極小化したときに
  `heap-probe`
  の
  `max_malloc`
  も下がるはずかを確認することである
- この build では、
  map の
  `.heap`
  と
  実機ログの
  `heap-est`
  /
  `heap-probe`
  /
  `mallinfo`
  を比較して評価する

## 0.37 `0.1.28` small ROM の RAM 読み込みを `malloc()` に戻す (2026-04-19)

- system version を
  `0.1.28`
  へ更新した
- `platform/rom_image.c`
  の
  small ROM RAM 経路で、
  `s_rom_buf`
  を static 配列から
  `BYTE *`
  へ戻した
- `rom_release_mapper0_buffer()`
  は
  `free(s_rom_buf)`
  を行う実装に戻し、
  `InfoNES_ReleaseRom()`
  から small ROM 用 buffer を解放できるようにした
- small ROM 読み込み前に、
  `heap-est gap`
  と
  `file_size`
  から
  `remain = gap - file_size`
  を計算し、
  `heap calc before RAM copy`
  としてログへ出すようにした
- build は成功し、
  Build ID は
  `PicoCalc NESco Ver. 0.1.28 Build Apr 19 2026 19:01:09`
  だった
- `bss`
  は
  `175424`
  bytes
  で、
  `0.1.27`
  から増えていない

## 0.38 `0.1.29` 通常起動から `heap-probe` を外す (2026-04-19)

- system version を
  `0.1.29`
  へ更新した
- `platform/main.c`
  では、
  boot 後の
  `rom_image_log_malloc_probe("build")`
  呼び出しを削除し、
  通常起動では
  `heap-probe`
  を走らせないようにした
- 事実として、
  実コード上で
  `heap-probe`
  を呼んでいた箇所は
  `platform/main.c`
  の
  この1箇所だけだった
- 目的は、
  診断用の
  `malloc/free`
  ループが
  ROM menu 前に
  allocator 状態を変えてしまう副作用を、
  通常起動経路から外すこと

## 0.39 `0.1.30` ROM menu 用領域を一括 `malloc()` / `free()` にまとめる (2026-04-19)

- system version を
  `0.1.30`
  へ更新した
- `platform/rom_image.c`
  では
  `rom_menu_storage_t`
  を追加し、
  `s_menu_entries`
  `s_menu_labels`
  `s_menu_paths`
  `s_menu_details`
  の背後領域を
  `s_menu_storage`
  1 個でまとめて保持するようにした
- `rom_menu_storage_prepare()`
  は
  menu 用 4 配列を
  個別に
  `malloc()`
  するのではなく、
  `sizeof(*s_menu_storage)`
  の
  1 回の
  `malloc()`
  で確保するようにした
- `rom_menu_storage_release()`
  も
  `free(s_menu_storage)`
  の
  1 回だけに整理した
- 目的は、
  ROM menu の
  prepare / release
  で
  allocator に
  4 本の別 block を作らないようにし、
  分断要因になりうる箇所を減らすこと
- build は成功し、
  `bss`
  は
  `133944`
  bytes
  だった

## 0.40 `0.1.31` boot 直後に allocator 実験を入れる (2026-04-19)

- system version を
  `0.1.31`
  へ更新した
- `platform/rom_image.c`
  では
  `rom_image_log_allocator_experiment()`
  を追加し、
  boot 直後に
  `heap-probe`
  を
  3 回連続で回した前後の
  `mallinfo`
  と、
  `24592`
  /
  `40976`
  bytes の
  `malloc()`
  /
  `free()`
  前後の
  `mallinfo`
  を出すようにした
- `platform/main.c`
  では
  `rom_image_log_heap_estimate("build")`
  の直後に
  `rom_image_log_allocator_experiment("boot")`
  を呼ぶようにした
- 目的は、
  `heap-probe`
  や
  既知サイズの
  `malloc/free`
  が
  allocator 状態をどう変えるかを、
  `Picocalc_NESco`
  の起動条件そのもので観測すること

## 0.41 `0.1.32` allocator 実験に `24592 -> 40976` 連続確保を追加する (2026-04-19)

- system version を
  `0.1.32`
  へ更新した
- `platform/rom_image.c`
  の
  `rom_image_log_allocator_experiment()`
  に、
  `malloc(24592)`
  を保持したまま
  続けて
  `malloc(40976)`
  を試す順番ケースを追加した
- それぞれの成否と、
  `24592`
  確保後
  /
  `40976`
  確保後
  /
  解放後の
  `mallinfo`
  をログへ出す
- 目的は、
  `LodeRunner`
  →
  `Xevious`
  相当の
  順番依存を、
  boot 実験だけで
  どこまで再現できるかを見ること

## 0.42 `0.1.33` Mapper 0 RAM 読み込みを `41488 bytes` 固定 `malloc()` にする (2026-04-19)

- system version を
  `0.1.33`
  へ更新した
- `platform/rom_image.c`
  の
  Mapper 0 RAM 経路では、
  `malloc(file_size)`
  をやめて
  `malloc(ROM_RAM_THRESHOLD_BYTES)`
  に統一した
- `ROM_RAM_THRESHOLD_BYTES`
  は
  `16-byte header + 512-byte trainer + 32 KB PRG-ROM + 8 KB CHR-ROM`
  の合計で、
  `41488`
  bytes
  である
- `heap before RAM copy`
  と
  `heap calc before RAM copy`
  の
  `request`
  も、
  実ファイルサイズではなく
  固定確保サイズ
  `41488`
  を出すようにした
- 目的は、
  `LodeRunner`
  →
  `Xevious`
  のような
  `Mapper 0`
  RAM 読み込みの
  順番依存を、
  可変長確保ではなく
  固定長確保へ揃えて
  消せるかを見ること

## 0.43 `0.1.34` boot allocator 実験を外して通常起動へ戻す (2026-04-19)

- system version を
  `0.1.34`
  へ更新した
- `platform/main.c`
  から
  `rom_image_log_allocator_experiment("boot")`
  呼び出しを削除した
- `platform/rom_image.h`
  から
  allocator 実験用宣言を削除した
- `platform/rom_image.c`
  から
  `heap-probe`
  と
  allocator 実験用の
  関数群を削除した
- 目的は、
  実験のために差し込んだ
  独自の
  `malloc/free`
  ループと
  boot ログを外し、
  `0.1.33`
  の
  `Mapper 0`
  固定長確保版を
  通常起動条件で確認できる状態へ戻すこと

## 0.44 `0.1.35` Map30 の静的共用 RAM をやめて起動時一括 `malloc()` に戻す (2026-04-19)

- system version を
  `0.1.35`
  へ更新した
- `infones/mapper/InfoNES_Mapper_030.cpp`
  では、
  `ChrRam`
  `FlashIdBank`
  `PrgOverlay[2]`
  を
  `Map30_Init()`
  の中で
  1 本の連続領域として
  `malloc()`
  し、
  その中を
  各用途へ
  切り分けるようにした
- `Map30_EnsurePrgOverlay()`
  は
  遅延 `new`
  をやめて、
  起動時に確保済みの
  `PrgOverlay`
  領域を使うようにした
- `Map30_Release()`
  は
  個別 `delete[]`
  をやめて、
  起動時に確保した
  1 本の連続領域を
  `free()`
  する形へ揃えた
- `infones/InfoNES_Mapper.cpp`
  /
  `infones/InfoNES_Mapper.h`
  から
  `Map30_WorkRam`
  の
  static union 参照を削除した
- build は成功し、
  `bss`
  は
  `133948`
  bytes
  だった

## 0.45 `0.1.36` Map30 の起動時一括確保を 3 回の `malloc()` に分割する (2026-04-19)

- system version を
  `0.1.36`
  へ更新した
- `infones/mapper/InfoNES_Mapper_030.cpp`
  では、
  `Map30_AllocBase`
  1 本での
  起動時一括確保をやめて、
  `ChrRam`
  `FlashIdBank`
  `OverlayPool`
  を
  それぞれ別に
  `malloc()`
  する形へ分割した
- `Map30_Release()`
  は
  3 本の確保領域を
  順に
  `free()`
  する形へ更新した
- 目的は、
  `0.1.35`
  の
  約 73KB
  1 発確保が通らなかった条件でも、
  分割した
  `malloc()`
  なら通るかを確認すること
- build は成功し、
  `bss`
  は
  `133956`
  bytes
  だった

## 0.46 `0.1.37` Map30 の 3 分割確保失敗ログへ段階番号を追加する (2026-04-19)

- system version を
  `0.1.37`
  へ更新した
- `infones/mapper/InfoNES_Mapper_030.cpp`
  の
  `Map30_Init()`
  では、
  `CHR-RAM`
  `flash-id`
  `overlay-pool`
  の
  3 本の確保失敗時に、
  `1/3`
  `2/3`
  `3/3`
  の段階番号と
  サイズを
  `InfoNES_Error`
  へ出すようにした
- 目的は、
  `0.1.36`
  の実機失敗時に、
  最初・2回目・3回目の
  どこで落ちたかを
  ログから即座に判別できるようにすること

## 0.47 `0.1.38` Map30 の 3 分割確保を大きい順へ並べ替える (2026-04-19)

- system version を
  `0.1.38`
  へ更新した
- `infones/mapper/InfoNES_Mapper_030.cpp`
  では、
  起動時 3 分割確保の順序を
  `overlay-pool(32KB)`
  →
  `chr-ram`
  →
  `flash-id(8KB)`
  に並べ替えた
- 段階番号付き
  `InfoNES_Error`
  も、
  この新しい順序へ合わせて
  `1/3`
  `2/3`
  `3/3`
  を更新した
- 目的は、
  `0.1.36`
  /
  `0.1.37`
  で見えた
  `overlay-pool`
  失敗に対して、
  大きい領域から先に確保した方が
  通るかを確認すること

## 0.48 `0.1.39` Map30 の 3 分割確保を `new[]/delete[]` に切り替える (2026-04-19)

- system version を
  `0.1.39`
  へ更新した
- `infones/mapper/InfoNES_Mapper_030.cpp`
  では、
  `overlay-pool`
  `chr-ram`
  `flash-id`
  の
  3 本の起動時確保を
  `new (std::nothrow) BYTE[]`
  に切り替えた
- `Map30_Release()`
  も
  対応して
  `delete[]`
  へ更新した
- 目的は、
  `0.1.38`
  までの
  `malloc()`
  ベース確保で
  通らなかった条件に対して、
  `new[]`
  経路で差が出るかを確認すること

## 0.36 `0.1.27` LCD DMA buffer を 4KB x 2 の double buffer にする (2026-04-19)

- system version を
  `0.1.27`
  へ更新した
- `platform/display.h`
  の
  `STRIP_HEIGHT`
  を
  `16`
  から
  `8`
  へ下げた
- `drivers/lcd_spi.c`
  の
  `s_lcd_dma_buf`
  を
  `BYTE[2][256 * STRIP_HEIGHT * 2]`
  として、
  `4KB x 2`
  の
  double buffer
  に切り替えた
- build は成功した
- total の静的 RAM は
  `8KB`
  のままなので、
  `bss`
  は
  `175424`
  bytes
  で据え置きだった
- ただしこの段階では
  `rom_menu.c`
  側の広い
  `lcd_dma_write_rgb565_async()`
  呼び出しが
  4KB staging
  を前提にしておらず、
  SD カード内容が表示されない回帰が出た
- そこで
  `drivers/lcd_spi.c`
  の
  `lcd_dma_write_rgb565_async()`
  を
  `LCD_DMA_MAX_PIXELS`
  単位の
  chunk 送信へ変更し、
  4KB buffer
  に収まらない menu 描画でも順次 DMA 転送できるようにした
- 実機ログ
  `pico20260419_160935.log`
  では
  `scan complete, 15 entries total`
  まで進み、
  `0:/nes`
  配下の
  `LodeRunner.nes`
  `DragonQuest.nes`
  `Xevious.nes`
  などが列挙されていて、
  SD カード内容表示の回帰は解消した
- `0.1.27`
  として再 build し、
  Build ID は
  `PicoCalc NESco Ver. 0.1.27 Build Apr 19 2026 16:17:18`
  だった
- `bss`
  は
  `175424`
  bytes
  で、
  `0.1.26`
  から増えていない
- 実機ログ
  `pico20260419_162812.log`
  では
  `PERF`
  `62`
  本ぶんで
  `frames`
  平均
  `46.65`
  `cpu_pct`
  平均
  `100.6`
  `draw_us`
  平均
  `673719.74`
  `lcd_wait_us`
  平均
  `4201.61`
  `lcd_flush_us`
  平均
  `4864.77`
  だった
- ユーザー確認では
  「よくなったのでこれ採用」
  となったため、
  LCD DMA staging は
  `4KB x 2`
  double buffer +
  chunk 送信を採用とする

## 0.35 `0.1.25` LCD DMA buffer を single buffer 化する (2026-04-19)

- system version を
  `0.1.25`
  へ更新した
- `drivers/lcd_spi.c`
  の
  `s_lcd_dma_buf`
  を
  `BYTE[2][256 * 16 * 2]`
  から
  `BYTE[256 * 16 * 2]`
  へ変更し、
  LCD DMA staging を
  single buffer
  化した
- `lcd_dma_acquire_buffer()`
  で
  `lcd_dma_wait()`
  を呼び、
  同一 buffer の再利用前に
  DMA 完了を保証する形へした
- build は成功した
- 最終 ELF では
  `s_lcd_dma_buf`
  は
  `0x2000`
  bytes
  で残った
- 実機ログ
  `pico20260419_155949.log`
  では
  起動は通った
- 同ログでは
  `frames`
  が
  平均
  `24.55`
  まで下がり、
  体感フレームレート低下が見えた
- 同ログの平均では
  `draw_us`
  `684961`
  に対し
  `lcd_wait_us`
  `53`
  /
  `lcd_flush_us`
  `1319`
  で、
  DMA 待ちは依然として小さい
- つまり
  single buffer
  化は成立するが、
  速度面では不利と判断できる

## 0.34 `0.1.24` LCD DMA 待ち時間の計測を入れる (2026-04-19)

- system version を
  `0.1.24`
  へ更新した
- `platform/display.c`
  に
  `s_perf_lcd_wait_us`
  と
  `s_perf_lcd_flush_us`
  を追加し、
  strip flush ごとの
  `lcd_dma_wait()`
  と
  `lcd_set_window() + lcd_dma_write_bytes_async()`
  の時間を積算するようにした
- `display_perf_reset()`
  /
  `display_perf_snapshot()`
  を追加して、
  LCD 側の積算値を
  emulator 側から読めるようにした
- `infones/InfoNES.cpp`
  の
  `[PERF]`
  ログに
  `lcd_wait_us`
  と
  `lcd_flush_us`
  を追加した
- build は成功した
- 次は実機ログで
  `draw_us`
  に対する
  `lcd_wait_us`
  /
  `lcd_flush_us`
  の比率を確認し、
  `s_lcd_dma_buf`
  削減可否の判断材料にする

## 0.33 `0.1.23` RGB444→RGB565 の 8KB LUT を小 LUT へ置き換える (2026-04-19)

- system version を
  `0.1.23`
  へ更新した
- `platform/display.c`
  の
  `g_rgb444_to_rgb565`
  `4096`
  要素
  `8KB`
  LUT を廃止し、
  RGB444 の
  R/G/B
  nibble ごとの
  `16`
  要素
  `3`
  本の小 LUT へ置き換えた
- `convert_infones_color()`
  は
  12-bit
  RGB444
  を
  R/G/B
  nibble に分解し、
  小 LUT
  の OR 合成で
  RGB565
  へ変換する形に変わった
- build は成功した
- 最終 ELF では
  `g_rgb444_to_rgb565`
  は消え、
  `s_rgb4_to_rgb565_r/g/b`
  が各
  `0x20`
  bytes
  で残った
- `bss`
  は
  `191680`
  bytes →
  `183584`
  bytes
  へ減った
- 実機ログ
  `pico20260419_153351.log`
  で
  起動確認が通り、
  色化けや
  表示異常は見えなかった

## 0.32 `0.1.22` Mapper 6 の 32KB を静的共用へ寄せて有効化する (2026-04-19)

- system version を
  `0.1.22`
  へ更新した
- `infones/InfoNES_Mapper.cpp`
  の mapper 共用 union に
  `Map6_Chr_Ram`
  `32KB`
  を追加し、
  `infones/mapper/InfoNES_Mapper_006.cpp`
  の独立静的配列を
  共用領域参照へ差し替えた
- `MapperTable`
  で
  Mapper 6 を再有効化し、
  `InfoNES_Mapper.cpp`
  の
  `#include`
  も戻した
- build は成功した
- 最終 ELF では
  `Map6_Init()`
  が残り、
  `Map6_Chr_Ram`
  は
  mapper 共用 union
  参照へ変わった
- `bss`
  は
  `191672`
  bytes →
  `191680`
  bytes
  の
  `+8`
  bytes に収まった
- 次は実機で
  起動確認が取れるかを確認する

## 0.31 `0.1.21` audio ring buffer を 4KB へ縮小する (2026-04-19)

- system version を
  `0.1.21`
  へ更新した
- `platform/audio.h`
  の
  `AUDIO_RING_SIZE`
  を
  `8192`
  から
  `4096`
  へ下げ、
  `g_audio_ring`
  の静的 RAM を
  `8KB`
  から
  `4KB`
  へ縮小した
- build は成功した
- `bss`
  は
  `195768`
  bytes →
  `191672`
  bytes へ減った
- 実機ログ
  `pico20260419_150702.log`
  では、
  `heap-probe build max_malloc=19216`
  を確認した
- 同じく
  `LodeRunner.nes`
  と
  `BokosukaWars.nes`
  は起動し、
  ゲーム終了後の
  ROM 選択 menu
  復帰も通った
- 次は
  `g_audio_ring`
  を
  `2048`
  まで下げられるかを検討する

## 0.30 `0.1.20` Mapper 30 の 40KB を静的共用へ寄せる (2026-04-19)

- system version を
  `0.1.20`
  へ更新した
- `infones/InfoNES_Mapper.cpp`
  の mapper 共用 union に
  `Map30`
  用
  `0xA000`
  bytes を追加し、
  `infones/mapper/InfoNES_Mapper_030.cpp`
  の
  `ChrRam`
  最大
  `32KB`
  と
  `FlashIdBank`
  `8KB`
  を
  その静的共用領域へ寄せた
- `PrgOverlay`
  最大
  `32KB`
  は、
  ひとまず動的確保のまま残した
- `platform/rom_image.c`
  の
  `InfoNES_ReleaseRom()`
  から
  `Map30_Release()`
  を呼ぶようにし、
  Mapper 30 の overlay /
  CHR-RAM /
  flash-id bank
  の解放経路を追加した
- build は成功した

## 0.29 `0.1.19` flash metadata sector を正しく erase する (2026-04-19)

- system version を
  `0.1.19`
  へ更新した
- `platform/rom_image.c`
  の
  `rom_stage_file_to_flash()`
  では、
  metadata 導入後も erase 開始位置が
  `XIP_ROM_DATA_OFFSET`
  のままだったため、
  `XIP_ROM_OFFSET`
  から
  metadata + ROM data
  全体を erase するように修正した
- build は成功した
- 実機ログ
  `pico20260419_141113.log`
  では、
  `DragonQuest.nes`
  を
  mapper `#3`
  として flash staging 後に起動し、
  続いて
  `flash:/DragonQuest.nes`
  built-in entry からの再起動も通った
- ユーザー確認として、
  `DragonQuest3`
  も問題なく動き、
  file 名は flash 表示領域へ反映された
- 一方でユーザー確認として、
  `DART`
  や
  `TOWR`
  のような Mapper 30 系はまだ動かず、
  そこから戻ったあとに
  ROM 選択 menu が出ない件は別課題へ切り出した

## 0.28 `0.1.18` flash slot を広げて staged ROM metadata を永続化する (2026-04-19)

- system version を
  `0.1.18`
  へ更新した
- `platform/rom_image.c`
  の flash staging 開始位置を
  `0x00100000`
  から
  `0x00080000`
  へ前倒しし、
  metadata 1 sector +
  ROM data 本体
  の構成へ変更した
- 同じく
  `platform/rom_image.c`
  では、
  flash slot 先頭 sector に
  `magic / version / rom_size / mapper_no / file_name`
  を書き、
  起動時にその metadata を読んで
  built-in flash entry を復元するようにした
- build は成功した
- build map では
  `__flash_binary_end`
  は
  `0x1003bc7c`
  で、
  新しい flash staging 開始
  `0x10080000`
  までまだ余白がある

## 0.27 `0.1.17` built-in flash entry に現在の file 名を表示する (2026-04-19)

- system version を
  `0.1.17`
  へ更新した
- `platform/rom_image.c`
  では、
  `BUILTIN.NES`
  固定の built-in entry をやめて、
  flash slot 用の label /
  path /
  detail を持つ動的 entry へ変更した
- 大きい
  `sd:/.../*.nes`
  を flash staging したあと、
  built-in entry には
  その file の basename を表示するようにした
- build は成功した
- build 結果では
  `bss`
  は
  `195768`
  bytes のまま変わらない

## 0.26 `0.1.16` 大きい ROM の flash staging と verify を有効化する (2026-04-19)

- system version を
  `0.1.16`
  へ更新した
- `platform/rom_image.c`
  の
  menu entry 判定では、
  valid header を持つ
  flash 対象
  `*.nes`
  を
  `enabled=1`
  にして選択可能にした
- `platform/rom_image.c`
  の
  `rom_stage_file_to_flash()`
  では、
  SD file を flash へ書いたあと
  file を先頭へ戻して、
  XIP 上の flash 内容と比較する
  verify を追加した
- build は成功した
- build 結果では
  `bss`
  は
  `195768`
  bytes のまま変わらない

## 0.25 `0.1.15` Mapper 235 を再有効化する (2026-04-19)

- system version を
  `0.1.15`
  へ更新した
- `infones/InfoNES_Mapper.h`
  の
  `PICOCALC_ENABLE_MAPPER235`
  を
  `1`
  に戻し、
  `Mapper 235`
  と
  `DRAM_SIZE=0xA000`
  を再有効化した
- build は成功した
- build 結果では
  `bss`
  が
  `163000`
  bytes から
  `195768`
  bytes へ増えた
- 実機確認では、
  `pico20260419_134026.log`
  の確認結果として、
  この版でも
  ROM menu は落ちず、
  起動確認まで通った

## 0.24 `0.1.14` Mapper 185 dummy CHR ROM を共用 union へ寄せる (2026-04-19)

- system version を
  `0.1.14`
  へ更新した
- `infones/mapper/InfoNES_Mapper_185.cpp`
  に独立してあった
  `Map185_Dummy_Chr_Rom[0x400]`
  は削除し、
  `infones/InfoNES_Mapper.cpp`
  の
  `g_MapperSharedRam`
  に
  `map185_dummy_chr_rom`
  を追加して共有させた
- この変更により、
  mapper ごとの排他利用方針を保ったまま、
  独立常駐していた
  `1KB`
  を削減した
- build 結果では
  `bss`
  が
  `164024`
  bytes から
  `163000`
  bytes へ減った
- 実機確認では、
  `pico20260419_133136.log`
  の確認結果として、
  `Mapper 235`
  を再有効化した構成でも
  ROM menu は落ちず、
  起動確認までは通った

## 0.23 `0.1.13` LCD 転送用 byte buffer へ直接色変換する (2026-04-19)

- system version を
  `0.1.13`
  へ更新した
- `platform/display.c`
  は、
  RGB444 →
  RGB565
  変換結果を
  `g_nes_rgb565_strip`
  にいったん持たず、
  LCD driver
  が持つ DMA 用 byte buffer へ直接書く形へ変更した
- `drivers/lcd_spi.c`
  は、
  RGB565 配列を受けて内部で byte 化する既存 API を残しつつ、
  active DMA buffer を取得する関数と、
  raw byte buffer をそのまま非同期送信する関数を追加した
- 目的は、
  表示の一次出力後にあった
  RGB565 strip buffer と
  LCD DMA byte buffer
  の二重 staging を減らし、
  静的 RAM を削ることである
- build 結果では
  `bss`
  が
  `172216`
  bytes から
  `164024`
  bytes へ減った
- 実機確認では、
  `pico20260419_131905.log`
  の確認結果として
  「問題なく動いた」
  と評価された

## 0.16 `0.1.8` 可変 `FrameSkip` を戻して描画だけを間引く (2026-04-19)

- system version を
  `0.1.8`
  へ更新した
- `platform/display.c`
  の
  `InfoNES_LoadFrame()`
  で、
  実時間 pacing は維持したまま、
  前回の skip 数ぶん deadline を進める形へ整理した
- これにより、
  `FrameSkip`
  を
  `0`
  /
  `1`
  /
  `2`
  の可変値として使っても、
  draw frame ごとにしか pacing が掛からないことによる
  進行速度の暴走を避ける意図である
- 次の draw で使う
  `FrameSkip`
  は、
  deadline に対する遅れ量で選び、
  遅れが大きいときだけ描画を追加で間引く
- 目的は、
  `0.1.7`
  の
  pacing /
  audio back-pressure を維持したまま、
  描画負荷だけを下げて
  `cpu_pct`
  を
  `100%`
  前後へ寄せることである
- この変更後も
  `cmake --build build -j4`
  は成功している

## 0.15 `0.1.7` 実時間 pacing と audio ring back-pressure を追加 (2026-04-19)

- system version を
  `0.1.7`
  へ更新した
- `platform/display.c`
  の
  `InfoNES_LoadFrame()`
  に、
  1 フレーム
  約
  `16667us`
  の deadline を使う単純な実時間 pacing を追加した
- この pacing は、
  実時間より早いときだけ
  `sleep_us()`
  で待ち、
  実時間より遅いときは待たずにそのまま進める方針である
- `platform/audio.c`
  の
  `InfoNES_SoundOutput()`
  には、
  `nes1/Picocalc_InfoNES`
  と同系の
  audio ring 空き待ちを追加し、
  APU が再生系より先行しすぎるときは短い
  `sleep_us(50)`
  で back-pressure をかけるようにした
- 目的は、
  描画調整を再検討する前に、
  ゲーム進行を実時間へ寄せ、
  BGM とゲーム速度の乖離を減らすことである
- この変更後も
  `cmake --build build -j4`
  は成功している

## 0.14 `0.1.6` 色変換とゲーム表示位置を `nes1` 寄りへ補正 (2026-04-19)

- system version を
  `0.1.6`
  へ更新した
- `platform/display.c`
  の
  `NesPalette`
  を
  `nes1/Picocalc_InfoNES`
  と同じ値へ合わせた
- `InfoNES_PostDrawLine()`
  の色変換は、
  `nes1`
  と同様に
  `__builtin_bswap16(color)`
  を通してから
  `g_rgb444_to_rgb565`
  へ引く形へ直した
- ゲーム表示の viewport は
  `x=32`
  `y=24`
  `w=256`
  `h=240`
  に修正し、
  以前の
  `y=40`
  から
  `nes1`
  寄りの位置へ戻した
- `platform/infones_session.cpp`
  ではゲーム開始前に
  fullscreen で黒クリアしてから
  `DISPLAY_MODE_NES_VIEW`
  へ戻すようにし、
  ROM 選択画面が後ろに残りにくいようにした
- この変更後も
  `cmake --build build -j4`
  は成功している

## 0.13 `0.1.5` Mapper 235 を一時 disable して共有 mapper RAM を縮小 (2026-04-19)

- system version を
  `0.1.5`
  へ更新した
- `infones/InfoNES_Mapper.h`
  に
  `PICOCALC_ENABLE_MAPPER235`
  を追加し、
  `0`
  の間は
  `DRAM_SIZE`
  を
  `0xA000`
  ではなく
  `0x2000`
  とするようにした
- `infones/InfoNES_Mapper.cpp`
  では、
  mapper table の
  `{235, Map235_Init}`
  を同じ switch で包み、
  後で
  `PICOCALC_ENABLE_MAPPER235`
  を
  `1`
  へ戻せば元に戻せる形にした
- これにより、
  `g_MapperSharedRam`
  の最大メンバが
  `0xA000`
  から
  `0x2000`
  へ縮み、
  `Mapper 19`
  と
  `Mapper 188`
  に必要な分だけを残す構成になった
- 目的は
  `Mapper 0`
  起動確認を優先して、
  menu 用 RAM と
  `*.nes`
  静的バッファの両立余地を増やすことである
- この変更後も
  `cmake --build build -j4`
  は成功している

## 0.12 `0.1.4` `*.nes` 用 Mapper 0 バッファを静的 RAM へ戻す (2026-04-19)

- system version を
  `0.1.4`
  へ更新した
- `platform/rom_image.c`
  の
  `Mapper 0`
  用
  `*.nes`
  バッファを、
  容量固定 `malloc`
  から
  `static BYTE s_rom_buf[MAPPER0_ROM_IMAGE_MAX_SIZE]`
  へ戻した
- これにより、
  ROM menu 用 RAM は動的管理のまま残しつつ、
  `*.nes`
  読み込み側は
  `nes1/Picocalc_InfoNES`
  と同じ方向の静的固定領域へ寄せた
- `InfoNES_ReleaseRom()`
  からは、
  `Mapper 0`
  バッファの
  `free()`
  が不要になった
- ROM 読み込み前の heap ログは残してあり、
  実機では
  `malloc`
  ではなく
  静的 RAM へ載って起動可否を見られる状態になった
- この変更後も
  `cmake --build build -j4`
  は成功している

## 0.11 `0.1.3` nes1 近似の簡易 audio mix へ戻す (2026-04-19)

- system version を
  `0.1.3`
  へ更新した
- `platform/audio.c`
  の
  `s_tnd_mix_lut[16 * 16 * 128]`
  と
  `s_pulse_mix_lut`
  を削除し、
  `nes1/Picocalc_InfoNES`
  に近い単純加算ベースの
  `InfoNES_SoundOutput()`
  へ戻した
- 新しい mix は
  `pulse1 + pulse2 + triangle + noise*4 + dpcm`
  を 8-bit へ正規化する形である
- これにより、
  `infones`
  由来ではない静的 RAM のうち、
  特に大きかった音声 LUT 32KB を削減した
- この変更後も
  `cmake --build build -j4`
  は成功している
- 次に見るべき事実は、
  簡易 mix 化後の heap ログと、
  実機での起動 / 音の成立である

## 0.10 `0.1.2` menu 前 heap 観測点追加 (2026-04-19)

- system version を
  `0.1.2`
  へ更新した
- `ROM`
  選択画面に入る前の free RAM / heap 量を確認するため、
  `platform/main.c`
  と
  `platform/rom_image.c`
  に
  `rom_image_log_heap()`
  を追加した
- 観測点は
  `before rom_menu loop`
  `before menu alloc`
  `after menu alloc`
  の 3 か所である
- この変更後も
  `cmake --build build -j4`
  は成功している
- 次に見るべき事実は、
  実機ログ上で
  ROM menu へ入る前の heap がどれだけあり、
  menu 確保でどれだけ減るかである

## 0.9 `0.1.1` menu RAM 動的化と Mapper 0 容量固定 `malloc` (2026-04-19)

- system version を
  `0.1.1`
  へ更新した
- `platform/rom_image.c`
  の ROM menu 用静的領域をやめ、
  menu 開始時に `malloc()`
  し、
  menu 終了時に解放する形へ切り替えた
- menu の初期ディレクトリは、
  前回選択した `*.nes`
  のディレクトリを優先し、
  前回がなければ
  `0:/nes`
  、
  それもなければ
  `0:/`
  を開く形へ切り替えた
- `Mapper 0`
  の RAM 読み込みは、
  ROM ごとの可変サイズ `malloc`
  ではなく、
  最大 `40976`
  bytes の容量固定 `malloc`
  バッファを 1 本使う形へ切り替えた
- ゲーム終了後に ROM menu へ戻るときは、
  `*.nes`
  用バッファを解放してから menu 用 RAM を確保し直す構成になった
- この変更後も
  `cmake --build build -j4`
  は成功している
- `arm-none-eabi-size`
  では
  `bss`
  が
  `196300`
  bytes となり、
  menu 静的領域を常駐させていた以前より小さくなった

## 0.8 `0.1.0` デバッグ導入点 (2026-04-19)

- system version を
  `0.1.0`
  へ更新した
- 今回の
  `0.1.0`
  は、
  runtime 検証とメモリ不足切り分けに入るためのデバッグ版区切りとして付与した
- `Xevious.nes`
  起動時の
  `Out of memory`
  切り分けのため、
  `platform/rom_image.c`
  の SD RAM 読み込み経路に heap 診断ログを追加した
- 追加した診断は、
  `malloc`
  の直前に
  `arena`
  `used`
  `free`
  `keep`
  `request`
  を出し、
  失敗時にも同じ指標を再度出す形である
- この変更後も
  `cmake --build build -j4`
  は成功している
- したがって、
  現在の到達点は
  「device build は通る。
  次は実機ログで heap 空きと要求量を照合して
  RAM 読み込み失敗の実量を確認する」
  段階である

## 0.7 `infones` 接続 compile 通過点 (2026-04-19)

- `platform/display.c`
  `platform/audio.c`
  `platform/input.c`
  `platform/rom_image.c`
  を
  `CMakeLists.txt`
  上で
  `LANGUAGE CXX`
  として扱う形へ変更した
- `platform/input.c`
  は
  3 引数
  `InfoNES_PadState()`
  と
  `getbuttons()`
  を持つ形へ更新した
- `platform/audio.c`
  は
  `InfoNES_SoundInit()`
  `InfoNES_GetSoundBufferSize()`
  `int InfoNES_SoundOpen(int, int)`
  を持つ形へ更新した
- `platform/display.c`
  は
  `NesPalette`
  と
  `micromenu`
  を定義し、
  `InfoNES_SetLineBuffer()`
  と
  `FrameSkip`
  を使う
  `infones`
  契約へ寄せた
- `platform/rom_image.c`
  は
  `InfoNES.h`
  /
  `InfoNES_System.h`
  側の
  `ROM`
  `VROM`
  `NesHeader`
  を使う形へ寄せ、
  `InfoNES_Error()`
  の重複実装は削除した
- `platform/display.c`
  `platform/audio.c`
  `platform/input.c`
  から C driver を呼ぶ箇所は、
  driver 側が
  C
  でビルドされることに合わせて
  `extern "C"`
  宣言へそろえた
- これにより、
  `cmake --build build -j4`
  は成功し、
  続けて
  `build/`
  での
  `make -j4`
  も成功した
- したがって、
  現在の到達点は
  「`infones` ベースへ切り替えた device build が compile / link を通過した」
  段階である
- 次段の主作業は、
  実機での
  ROM 起動、
  quit/reset、
  画面/入力/音声の runtime 検証である

## 0. 方針転換メモ (2026-04-18)

事実:
- このスレッドで、
  `Picocalc_NESco`
  の目標変更指示が出た。
- 現在構築している
  `core`
  配下ベースの
  NES エミュレーター路線は
  discontinue とする。
- 以後の本線は
  同 workspace の `infones`
  全体をコピーして
  `Picocalc_NESco`
  の NES エミュレーター本体へ移す方針である。
- 旧目標
  「PicoCalc 上で動作する MIT ライセンスの NES エミュレーター」
  は廃棄する。
- 新目標は
  「PicoCalc 上で動く、公開可能な GPL ライセンスの NES エミュレーター」
  である。
- この追記時点では
  ソースコードは未変更で、
  文書更新のみ実施している。

読み方:
- この文書の後続内容は、
  `core`
  路線で進めていた時期の履歴として保持する。
- 以後の
  `infones`
  ベース移行作業は、
  別の節として追加していく。

## 1. 現在の基準点

この文書を書いた時点の基準は次のとおり。

- 現在の system version: `0.1.0`
- 現在の `HEAD`: `0.1.0` デバッグ導入前の旧 `core` 路線末期基準メモ
- 直近 Build ID: `PicoCalc NESco Ver. 0.1.0 Build Apr 19 2026`
- 作業ツリー: 変更あり

重要:

- この文書の冒頭基準は `0.1.0` に更新した
- ただし本文下部には、かなり前半の bring-up からの古い経緯もそのまま残っている
- そのため、「現在の採用状態」と「過去の履歴メモ」を分けて読むこと

### 1.1 いま採用している状態

- 第2世代 sprite では
  - `tile_has_sprite`
  - resolve 本体で使う `opaque_mask`
  - `color`
  を新経路へ寄せた状態が通っている
- `opaque_mismatch = 0`
  と
  `color_mismatch = 0`
  は確認済み
- `sprite 0 hit`
  の整理は
  `0.0.230`
  で一度進めたが、
  `pico20260418_122345.log`
  では
  `draw_us`
  と sprite 総コストが
  `0.0.229`
  より悪化したため、
  `0.0.231`
  で
  `0.0.229`
  相当へ戻している
- したがって現行の採用版は、
  `sprite 0 hit`
  比較込みの
  `0.0.231`
  を描画基準として保ったまま、
  `0.0.232`
  では
  高速化用
  `PERF`
  表示だけを止めて
  audio 比較へ移る

## 2. かなり前半の進捗

以下は、初期 bring-up から ROM 起動までの大きな流れ。

### 2.1 クリーンルーム前提の整理

- `cleanroom_rebuild/docs` を読み、仕様化してから実装する方針を確認
- `nesco_mapper_cleanroom_spec.md` から:
  - Mapper 0 / 30 だけで閉じない
  - 実装優先順は対象 ROM の mapper 番号で決める
  - flash/write/IRQ は公開仕様と実トレースで検証する

### 2.2 PicoCalc 側 bring-up の入口

- `Picocalc_NESco` の build 停止要因を洗い出した
- 初期には以下が問題だった
  - Pico SDK include の不整合
  - `InfoNES_SetupScr()` の重複定義
  - LCD / keyboard / audio driver 未実装
- ここを最小修正して、まず build を通す段階まで持っていった

### 2.3 LCD bring-up

経緯:

- 最初は hardware SPI 前提で LCD を動かそうとした
- 表示は不安定で、赤固定や黒画面が出た
- 色フォーマットや init sequence を何度か見直した
- 最終的に、PicoCalc 実機では「最小 PIO SPI-like path + wait_idle」で安定した

この段階で判明したこと:

- `hardware SPI` より、最小 PIO 転送の方が安定した
- `CS/DC` は CPU 側で制御し、PIO は最小ビット転送に留める方がよかった
- 色確認では `赤 → 緑 → 青 → 白 → 黒` が正しく出る状態まで到達した
- 全画面 UI と NES viewport の両方を扱える表示モード切替を入れた

重要な判断:

- 18-bit 送信ではなく、NES 用途では `RGB565 2 byte/pixel` を採用する
- `STRIP_HEIGHT` を使った strip 転送で LCD の command overhead を減らす

### 2.4 キーボード bring-up

- PicoCalc keyboard controller の I2C slave と key code を確認
- menu navigation と in-game input が通るようになった
- 実機向けキー割り当てとして、方向キー以外を次へ変更した
  - `` ` `` = Select
  - `-` = Start
  - `[` = B
  - `]` = A
- `Esc` で ROM 選択へ戻る機能も実装した
- `F1` を NES reset に割り当てた

### 2.5 ROM menu / SD browser

経緯:

- 最初は「FLASH ROM / SD CARD」を選ぶ source menu 的な画面だった
- これは仕様意図とズレていたため、`*.nes` ファイル選択画面へ変更した
- その後、SD mount、directory scan、directory navigation を実装した

この段階で達成したこと:

- root だけでなく subdirectory もたどれる
- parent directory は `[..]` 表記
- `*.nes` と directory が区別して表示される
- menu font は `PixelMplus` 由来のフル ASCII に切り替えた
- 画面の最大表示件数を制限し、ページング/スクロールで下にはみ出さないようにした

### 2.6 外部資産整理

フォント:

- `PixelMplus` 由来 font を `font/` で管理するよう整理した
- generated header の出どころと license 導線を repo 内へ残した

FatFs:

- `fatfs/` は config と attribution を置く場所として整理した
- 実体は `ChanFatFS` であることを README に明記した
- 参照元 URL も repo 内の README へ書いた

この整理で学んだこと:

- 公開前提の文書にローカル絶対パスは書かない
- `/nes2` より外のローカル環境情報は書かない
- MIT でも attribution / license を消してよいわけではない

## 3. Mapper 0 実行まで

### 3.1 小さい ROM の方針

- 小さい `*.nes` は RAM に読み込む
- 大きい ROM は flash/XIP 側へ回す想定
- ただし当面の immediate target は:
  - `Mapper 0`
  - `48KB 未満`
  - SD から RAM 読み込み

### 3.2 Xevious 起動

このスレッド中で、`Xevious.nes` を

- `mapper=0`
- `mode=RAM`
- `enabled=1`

として判定し、SD から読み込んで実行開始するところまで到達した。

初期症状:

- メニュー残骸がゲーム画面左に残る
- 背景が崩れる
- ゲーム画面は「なんとなく動いている」が正常ではない

## 4. PPU / 背景崩れの修正経緯

Xevious 背景崩れについては、かなり重要なので詳細を残す。

### 4.1 初期の疑い

背景乱れで疑った項目:

- `$2006` 二度書き
- `$2007` の自動 increment
- `PPU_CTRL bit2`
- mirroring
- `$2005` の fine/coarse scroll

### 4.2 実際に効いた修正

背景乱れに対して最も効いたのは、rendering off 中の loopy scroll 更新を止めた修正だった。

観測された症状:

- `$2006` で `3F00` を設定した直後なのに
- `$2007` 書き込み先が `0B01 / 1B01 / 2B01 / 3B01` のように化ける

これは、CPU が明示設定した `PPU_Addr` を、描画側が別経路で進めてしまっていたことが本命だった。

採用した修正の要点:

- rendering off のとき、`PPU_DrawLine()` で loopy scroll state を進めない
- visible line でも背景色だけ描き、`PPU_Addr / PPU_Temp` を壊さない

結果:

- `0.0.41` 時点で Xevious 背景が正常に動くようになった

### 4.3 fixNES との未解決差分

背景は直ったが、まだ `fixNES` と完全一致ではないと認識している。

保留項目:

- `$2007` palette read buffer の扱い
- `$2007` read/write 後の shadow read / prefetch
- rendering 中 `$2007` write path の細かい一致

判断:

- いまは背景が正常に動いているため、差分は「不具合が出たときに再検討する保留項目」
- 背景正常化後は、むやみにここを触らない方が安全

## 5. 音の経緯

音まわりは、かなり試行錯誤が多かったので分けて残す。

### 5.1 最初の状態

- APU 側では何か生成しているが、物理出力 driver が stub のままで無音

### 5.2 PWM 音出しの第一段階

- PicoCalc の音声出力へ PWM driver を接続して、とにかく音を出すところまで到達
- しかし、最初は
  - ノイズが非常に大きい
  - 音楽として成立しない

### 5.3 効いた修正

このスレッドで「効いた」と確認できたのは次。

#### `0.0.54` 付近

- 音声出力の二重初期化を止めた

#### `0.0.55` 付近

- APU timer を sample 単位でなく CPU-cycle 基準へ寄せた

#### `0.0.56` 付近

- PWM carrier と sample update を分離した

#### `0.0.57` 付近

- HSync 側で frame sequencer を進めるようにした

#### `0.0.58` 付近

- 出力を PWM midpoint へ寄せる DC correction を入れた

#### `0.0.59`

- `audio_ring_available()` を見て low/high water で hysteresis mute を入れた
- これは明確に効いた
- ユーザー確認:
  - `0.0.59 で無音は達成した`

重要:

- つまり「無音であるべき時間にノイズが鳴る」症状に対しては
- APU よりも先に「出力側の供給の連続性」を直すことが効いた

### 5.4 その後の迷走と反省点

その後、速度問題に対して音側でいくつか実験した。

#### `0.0.60`

- audio diagnostic logging を追加

#### `0.0.61`

- ring buffer hot path の軽量化

#### `0.0.62`

- サンプルレートを 22.05kHz に下げた

#### `0.0.64`

- APU timing log を追加
- 1秒ごとに UART へ次を出す
  - `q` = quarter-frame 回数
  - `h` = half-frame 回数
  - `samples` = その1秒の生成サンプル数
  - `rate` = 現在の設定サンプルレート
- 目的:
  - BGMだけテンポが遅い原因が
    - frame sequencer 側か
    - sample generation 側か
    を切り分けること

#### `0.0.65`

- APU register write を「即時適用」から「時刻つき queue replay」へ変更
- `ApuEvent { clocks, addr, data }` を本当に使うようにした
- `InfoNES_pAPUHsync()` の sample loop 内で、sample 時刻までに到達した
  event だけを順に適用するようにした
- scanline をまたぐ event は queue に残し、次 scanline 向けに clock を rebasing する
- 目的:
  - BGM だけテンポが遅い問題について
  - APU register write timing の崩れを本命として潰しにいくこと

#### `0.0.66`

- APU timing log を止めた
- 代わりに PPU 側で 1秒ごとの軽い perf log を追加した
- UART へ次を出す
  - `frames`
  - `scan`
  - `cpu_hz`
  - `cpu_pct`
- 目的:
  - APU ログの負荷を避けつつ
  - 6502 本体が実時間の何割で動いているかを先に測ること

#### `0.0.67`

- perf log をさらに分解した
- 1秒ごとに UART へ次を出す
  - `cpu_us`
  - `apu_us`
  - `draw_us`
  - `tail_us`
- 目的:
  - 本体速度低下の主犯が
    - CPU step
    - APU
    - 描画
    - その他 scanline 後半
    のどこかを切ること

#### `0.0.68`

- `lcd_spi.c` の固定コスト削減を開始
- 最初の一手として
  - `lcd_write_command()` / `lcd_write_data()` の不要な idle wait を削減
  - DMA config を初期化時に一度だけ組み、strip ごとに再利用
- 表示仕様や `lcd_set_window()` の意味はまだ変えていない
- 目的:
  - まず安全に `draw_us` の固定費を落とすこと

ここで重要な学び:

- これは「本当の高速化」ではなく、音質を落として処理量を減らしただけ
- ユーザーから明確に、
  - これは高速化ではない
  - こういう方向は筋が違う
という指摘を受けた

会話上ではさらに次の実験もした:

- PWM slice を sample clock に使う実験
- 11.025kHz 化

ただし:

- 現在の `HEAD` にはこれら後半の実験は残っていない
- しかも、この方向はゲーム速度の本質解決にならず、画面乱れも起こしやすかった

結論:

- 音質を犠牲にする方向は「高速化」とは呼ばない
- 今後、速度改善は emulator 本体と表示転送を対象にすべき

## 6. 表示高速化の実験

### 6.1 効いた修正

- LCD を DMA-backed strip transfer に変えたことはかなり効いた
- これで「実用になる」とユーザー評価が出た段階があった

### 6.2 失敗した修正

失敗例として、必ず引き継ぐべきもの:

- `STRIP_HEIGHT=32`

これは:

- 表示乱れを起こした
- テンポ遅れの根本解決にもならなかった

このため、会話上ではいったん `0.0.67` として

- 最後の表示実験だけ戻す
- つまり `STRIP_HEIGHT=16` へ戻す

という整理をした。

ただし現時点の `HEAD` は `0.0.62` であり、会話上で一度作った `0.0.67` 系の commit は今の branch には残っていない。

この点は、次の作業者が混乱しやすいので注意。

## 7. 引き継ぎ上の重要ポイント

### 7.1 いま確実に言えること

- LCD / keyboard / SD browser / ROM menu / small Mapper 0 RAM launch は大筋で成立した
- Xevious 背景崩れは、rendering off 中の loopy scroll 更新停止で直った
- 音は出る
- `0.0.59` 相当の hysteresis mute は「無音であるべき時間を無音にする」のに効いた

### 7.2 いま触ると危険なポイント

- 背景が直った後の PPU scroll/address 系を、根拠なくいじること
- 音のサンプルレートを下げることを「高速化」と呼んで進めること
- `STRIP_HEIGHT=32` を安易に再採用すること

### 7.3 進捗管理の反省

ユーザーから強く指摘された点:

- 毎回コミットしていても、経緯を書かなければ引き継げない
- `TODO` だけでは不十分
- 何が成功し、何が失敗し、何を戻したかを書く必要がある

今後の最低ルールとして残す:

- 版番号を上げた変更は、build + commit に加えて「経緯」を残す
- `TODO` だけでなく `History / Handover` を更新する
- 「戻した変更」も記録する

## 8. 現在の次アクション候補

次の人が着手するときの候補はこの順が安全。

1. 現在の `HEAD` が `0.0.69` 基準であることを確認する
2. 背景正常化済みの PPU 状態を壊さない
3. 音については `0.0.59` 相当の mute が効いた、という事実を重視する
4. 高速化は「音を粗くする」以外の方向で考える
5. 進捗文書だけでなく、この handover 文書も更新する

## 9. 2026-04-12 追記

### 9.1 `0.0.68` の結果

- `lcd_spi.c` の wait 削減と DMA 設定使い回しは、表示を壊さず入れられた
- ただし `draw_us` はほぼ変わらず、効果は小さかった
- ここから、描画側の本命は `lcd_spi.c` の小さい固定費だけではなく、
  `display.c` 側の line 変換コストも含むと判断した

### 9.2 `0.0.69` の狙い

- `InfoNES_PostDrawLine()` の RGB444→RGB565 変換を 4pixel 展開へ変更
- 目的は、`draw_us` のうち CPU 側の scanline 変換部分を少しでも削ること
- strip 戦略や window 再設定にはまだ踏み込まない

### 9.3 `0.0.70` の狙い

- `compositeSprites()` の scanline 固定費を下げる
- visible sprite が 1 枚もない line では `sprite_buf[256]` を初期化しない
- その場合、`PPU_DrawLine()` 側でも sprite overlay 判定を完全に飛ばす

### 9.4 `0.0.71` の狙い

- `PPU_DrawLine()` の背景 palette lookup を scanline 単位へ前寄せする
- backdrop と BG palette 0..3 を line 先頭で `WORD[16]` に展開し、
  毎ピクセルの `g_PalRAM / g_NesPalette` 参照を減らす
- sprite が無い行では overlay 分岐も完全に外す

### 9.5 `0.0.72` の狙い

- `PPU_DrawLine()` の通常ケースを fast path 化する
- `BG 有効・左端 clip なし` のとき、毎ピクセルの `bg_enabled/bg_clip`
  判定を外して背景 bit 展開だけに寄せる
- clip や BG disable を含む副作用の大きい経路は従来の分岐を残す

### 9.6 `0.0.73` の狙い

- `0.0.72` の fast path は効果が不安定だったため、そこを正にしない
- `PPU_DrawLine()` の背景タイル取得で、nametable byte と attribute byte を
  同一 page pointer から引く形に整理し、`PPUBANK` の bank 計算を減らす
- palette 前展開の効果は維持しつつ、背景 tile fetch の固定費を少し下げる

### 9.7 `0.0.74` の狙い

- 背景タイル inner loop を通常タイルだけ 8pixel 展開する
- fine-X を含む最初のタイルは従来の bit loop を維持し、通常タイルだけ
  bit 展開と palette lookup をまとめて処理する
- まず sprite 無し経路から効きを見て、表示崩れがないか確認する

### 9.8 `0.0.75` の狙い

- sprite あり経路にも通常タイルの 8pixel 展開を追加する
- 背景 8pixel ぶんの値を先に作ってから、sprite overlay を同じ 8pixel
  ブロック内で後段適用する
- sprite 無し経路だけ速い状態から一歩進めて、通常プレイ時の `draw_us`
  をさらに下げられるかを見る

### 9.9 `0.0.76` の狙い

- `compositeSprites()` の sprite palette 参照を scanline 単位へ前寄せする
- sprite pixel ごとの `g_PalRAM[0x10 + pal_idx]` 参照を 16-entry の local cache
  に置き換え、sprite 合成 hot path のメモリアクセスを減らす
- 左端 clip 判定も `bool` 化して、細かい固定費を削る

### 9.10 `0.0.77` の狙い

- 6502 の命令フェッチ hot path を PRG ROM 専用 helper へ寄せる
- `PC >= $8000` の opcode / operand fetch は汎用 `K6502_Read()` を通さず、
  `ROMBANK` を直接読む
- まずは opcode と PC 相対 operand の fetch だけを置き換え、挙動を変えずに
  CPU 側の分岐コストを減らす

### 9.11 `0.0.78` の狙い

- `g_wCurrentClocks` は reset 時以外に consumer が見当たらないため、
  `K6502_Step()` の hot path 更新を除去する
- 毎命令の単調増加カウンタ更新を外して、CPU 固定費がどこまで下がるかを見る

### 9.12 `0.0.79` の狙い

- 6502 の absolute/indirect 系 operand read も PRG ROM fast path へ寄せる
- `RD_ABS/RD_ABX/RD_ABY/RD_IZX/RD_IZY` と `read16()` を `read_cpu_data()`
  経由にし、`$8000-$FFFF` の ROM 参照まで汎用 `K6502_Read()` の分岐を
  通さないようにする
- opcode fetch だけでなく ROM 上の定数テーブルや immediate/absolute operand
  読み出しも軽くして、`cpu_us` をさらに下げられるかを見る

### 9.13 `0.0.80` の狙い

- `PPU_DrawLine()` の sprite あり 8pixel 展開経路で、実際は sprite が 1 つも
  乗っていないタイルを fast path で抜ける
- `sprite_buf[pixel_x..pixel_x+7]` を先に OR して、opaque sprite が無ければ
  背景色 8pixel の書き込みだけで終える
- sprite ありフレームでも「その tile には sprite が無い」ケースの固定費を減らし、
  `draw_us` をもう一段下げられるかを見る

### 9.14 `0.0.82` の狙い

- `0.0.80` の「empty sprite overlay block」最適化は効かなかったので採らない
- sprite 合成では最終 NES color index を抱え込まず、sprite palette slot を
  `sprite_buf` に保持する
- `PPU_DrawLine()` 側に 16-entry の `sprite_palette_rgb` を作り、
  sprite overlay 時の `g_NesPalette[...]` 参照を per-pixel から per-line
  へ前寄せして、sprite あり経路の色変換固定費を減らす

### 9.15 `0.0.83` の狙い

- `0.0.80` の empty sprite block fast path も、`0.0.82` の sprite RGB line cache も
  `PERF` 上で効果が見えなかったため採らない
- draw 側は一旦 `0.0.79` 相当の状態へ戻し、CPU 側の fast path が効いている
  基準へ復帰する
- 次の draw 最適化は、背景側でまだ pixel 単位に残っている経路を別筋で詰める

### 9.16 `0.0.84` の狙い

- `PPU_DrawLine()` の最初の半端タイルだけを本体ループから分離する
- `PPU_Scr_H_Bit != 0` のときは先頭タイルを先に slow path で処理し、
  その後の本体ループは `start_bit == 0` 前提で回せるようにする
- 背景側に残っている pixel 単位経路を少しでも減らし、通常タイル列の分岐を
  軽くする

### 9.17 `0.0.85` の狙い

- `bg_enabled && !bg_clip` を `bg_fast_path` として loop 外で一度だけ計算する
- 本体ループの fast path 条件から小さい論理判定を外し、通常ケースでの
  分岐コストをもう一段減らす
- `0.0.84` の「最初の半端タイル分離」の延長として、通常タイル列をさらに
  単純な条件で回せるようにする

### 9.18 `0.0.86` の狙い

- `bg_clip` が有効なときの左端 clip 領域を、本体ループの外で先に処理する
- clip の残り 8pixel だけを専用 slow path で処理し、それ以降のタイル列は
  `pixel_x >= 8` 前提で fast path に乗せやすくする
- `0.0.84` の「半端タイル分離」を、左端 clip ケースへもう一段広げる

### 9.19 `0.0.87` の狙い

- sprite あり通常タイル列で、背景 8pixel の生成と sprite overlay を段階的に分離する
- 背景 8pixel を先にまとめて作り、その後に sprite overlay だけを当てる形へ寄せる
- sprite 有りでも背景 bit 展開側の fast path を保ちやすくする

### 9.20 `0.0.88` の狙い

- sprite あり通常タイル列の 8pixel 処理を `static inline` helper へ切り出す
- 背景 8pixel 生成と sprite overlay 適用の境界を固定し、
  block 単位処理の形を崩さずに次の比較ができるようにする

### 9.21 `0.0.90` の狙い

- `0.0.88` の helper 化は perf 改善が見えなかったため採らない
- sprite あり通常タイル列の内容を `0.0.87` 相当に戻し、
  helper 化だけを外した戻し版を固定する

### 9.22 `0.0.91` の狙い

- `script/mesen2_memo.txt` を `/nes2` 前提の備忘録へ整理する
- `nes1` 固定パスや古い説明を外し、Mesen2 実行バイナリ、ROM 置き場、
  headless 実行例、Windows/WSL 利用時の注意点をいまの運用に合わせて残す

### 9.23 `0.0.92` の狙い

- `Lode Runner` の画面乱れについて、Mesen2 はデバッガ用途だけで使って
  `$2000/$2005/$2006/$2007` の挙動を観測した
- `mapper0` の縦ミラー設定そのものではなく、`$2007` write 時の
  rendering 中 loopy-style increment が本命と判断した
- `0.0.92` では `PPU_RegisterWrite($2007)` を `fixNES` と同じ
  `R0_INC_ADDR ? +32 : +1` の単純加算へ戻し、mid-frame nametable fill の
  書き込み先が崩れないかを確認する

### 9.24 `0.0.93` の狙い

- `0.0.92` で変化が無かったため、次は `PPU_UpDown_Clip` に注目した
- 現状は上端/下端の clip 行で `PPU_DrawLine()` が即 return しており、
  loopy の X reload と Y increment まで止まっていた
- `0.0.93` では rendering 有効時、clip 行でも loopy 更新だけは進めるようにし、
  platform 都合の border clip が scroll/nametable 基準を崩さないか確認する

### 9.25 `0.0.94` の狙い

- `Lode Runner` は gameplay は正常だが title/menu だけ壊れているため、
  draw hot path ではなく title/menu 初期化時の PPU 制御を本命と見直した
- Mesen2 をデバッガとして観測すると、`$2002` を長くポーリングした後に
  rendering off のまま palette / nametable を大量更新していた
- 現行実装は VBlank 開始/終了を scanline 終端で反映しており、CPU から見る
  VRAM 転送窓が 1 scanline 分短い可能性がある
- `0.0.94` では VBlank 開始/終了を scanline 開始側へ寄せて、
  title/menu の VRAM upload 窓不足が原因かどうかを確認する

### 9.26 `0.0.95` の狙い

- `Lode Runner` の title/menu が壊れるだけでなく、gameplay 後の title に
  gameplay 画面の背景が残る、別 ROM 後の title に前のゲーム背景が残る、
  という観測が出た
- これは PPU レジスタ時序より、`PPURAM` の nametable 内容が session を跨いで
  残っている症状と強く整合する
- `0.0.95` では `PPU_Init()` で `PPURAM` 全体を clear し、
  title/menu 背景が前のゲームの VRAM 残骸に引きずられていないか確認する

### 9.27 `0.0.96` の狙い

- `0.0.95` で session 跨ぎの残骸は整理できても、起動直後の title 自体は
  依然として壊れている
- 残る大きい差分として、`$2007` read の palette 特例、read buffer 更新、
  increment 後の shadow read が `fixNES` とずれていた
- `0.0.96` では `PPU_RegisterRead($2007)` を `fixNES` 寄りにして、
  title/menu 初期化で使われる PPUDATA read path の差を切り分ける

### 9.28 `0.0.97` の狙い

- `パワーオン → START → リセット → ESC` の実ログから、速度や ROM load は
  正常でも、reset と quit の分岐が見えていないことが分かった
- `0.0.97` では `InfoNES_Main()` と `main()` に最小の UART ログを足して、
  `PAD_SYS_RESET` / `PAD_SYS_QUIT` / session end / menu re-entry を明示する
- 狙いは、画面残りが `NES reset` と `menu return` のどちらで起きるかを
  まず因果関係として切り分けること

### 9.29 `0.0.98` の狙い

- `0.0.97` のログで、`RESET` は同一 session 内の `InfoNES_Reset()`、
  `ESC` は session end 後の menu return だと切り分けられた
- つまり gameplay 後 title の背景残りは、同一 ROM 内 reset 後に title が
  全面再描画しない部分へ gameplay 画面が残っている可能性が高い
- `0.0.98` では `PAD_SYS_RESET` 直後に NES viewport を黒クリアし、
  reset 後の残像問題を起動直後 title の恒常的な壊れと分離する

### 9.30 `0.0.98` 後の診断メモ

- `0.0.98` でも `Lode Runner` の title/menu は変化しなかった
- ここまでで効かなかったものは、
  - `$2007` write increment の単純化
  - clip 行での loopy 更新維持
  - VBlank 開始/終了位置の変更
  - `$2007` read buffer / shadow read の `fixNES` 寄せ
  - `PAD_SYS_RESET` 直後の viewport 黒クリア
- 一方で `0.0.95` の `PPURAM` clear は効いており、
  session 跨ぎ / ROM 跨ぎの背景残骸は減った
- ただし初回 title 自体は依然として壊れており、
  「単純な未初期化メモリ」だけが主犯ではない
- `PPU_Init()` は `PPU_R0/R1/R2/R3/R7`、`PPU_Addr`、`PPU_Temp`、
  `PPU_Scr_H_Bit`、`PPU_Latch`、`PPURAM`、`g_PalRAM`、`SPRRAM`
  を明示的に 0 初期化している
- そのため、次の本命は「値の初期化漏れ」より、
  `Mesen2` では暗黙に成立している
  `$2002` / VBlank / NMI / latch まわりの状態機械差と判断した
- 今後の調査対象:
  - `$2002` 読みでの VBlank flag clear のタイミング
  - VBlank 開始/終了と NMI の関係
  - title/menu 初期化中の `rendering off` 前提での PPU 内部状態遷移

### 9.31 `0.0.99` の狙い

- `Lode Runner` では palette/attribute そのものより、
  `$2002/$2005/$2006/$2007` と `w/t/v/x` の状態遷移差が本命と再整理した
- `0.0.99` では挙動変更を止め、`$2002` read と `$2001` write の
  `addr/temp/fineX/latch` ログを少数回だけ追加する
- Mesen2 正常系の title/menu 初期化シーケンスと比較し、
  `w` のクリアや rendering on/off 境界での PPU レジスタ状態が
  どこでずれているかを先に事実で確定する

### 9.32 `0.0.100` の狙い

- `0.0.99` のログでは `$2002` read が大量に並ぶだけで、
  最初の `VBlank=1` 後に起きる `$2005/$2006/$2007` を取り切れていない
- `0.0.100` では最初の `VBlank=1` 検出を capture 開始点にして、
  その後の少数回だけを集中的に記録する
- Mesen2 側の trace script も同じ capture 条件へ寄せて、
  `w/t/v/x` の比較対象をそのまま並べられる形にする

### 9.33 `0.0.101` の狙い

- `0.0.100` でも capture 窓の大半が `$2002` read に埋まり、
  比較したい最初の `$2001/$2005/$2006/$2007` が十分に見えなかった
- `0.0.101` では `$2002` を完全に capture 開始トリガ専用にし、
  以後は原則ログしない
- capture 窓も 48～64 イベントへ縮めて、
  `Lode Runner` title/menu 初期化直後の scroll/address 設定列だけを
  Mesen2 正常系と直接比較しやすくする

### 9.34 `0.0.102` の狙い

- `0.0.101` では `$2001` の連打が capture 窓を占有し、
  比較したい `$2005/$2006/$2007` にまだ届かなかった
- `0.0.102` では `$2001` の記録も最初の 4 件に制限し、
  scroll/address 設定列を優先して取る

### 9.35 `0.0.103` の狙い

- Mesen2 正常系では最初の `R2002(VBlank=1)` の直後に
  `W2001=00 -> W2006 -> W2005 -> W2000 -> W2007...` へ進む
- Picocalc 側は同じ capture 条件でも `$2001` の on/off だけで止まり、
  title 初期化の VRAM 更新列へ入れていない
- `fixNES` を見ると `R2002` 後の VBlank/NMI には
  `VBlankFlagCleared / VBlankClearCycle / NMIallowed` があり、
  読み取った VBlank を同一 VBlank 中の NMI にそのまま食わせない
- `0.0.103` では coarse scanline 実装の範囲で、
  `$2002` read 時の pending NMI 抑止と、VBlank 開始直後の NMI を
  1 scanline 遅らせる最小修正を試す

### 9.36 `0.0.104` の狙い

- `0.0.103` は変化なし
- ここで観測コードを見直すと、`$2005/$2006/$2007` の capture ログが
  `NESCO_DIAGNOSTICS` 条件の内側に残っていた
- そのため `0.0.101`〜`0.0.103` の通常ビルドでは、
  比較したかった scroll/address 設定列が最初から出ていなかった
- `0.0.104` では挙動変更は止めて、
  まず `$2005/$2006/$2007` の capture ログを通常ビルドでも出るように直す

### 9.37 `0.0.105` の狙い

- `0.0.104` で Mesen2 と同じ title 初期化列
  (`$2001 -> $2006 -> $2005 -> $2007...`) が Picocalc 側にも見えた
- これで「Picocalc だけが R2002 後に VRAM 更新列へ入れていない」仮説は外れた
- 次の本命は PPU 列そのものではなく、
  `R2002` 直後に CPU が見ている `PC / A / P(flags)` の差
- `0.0.105` では Picocalc と Mesen2 の両方で
  `CAPTURE_START` に `PC / A / P` を載せて比較できるようにする

### 9.38 `0.0.106` の狙い

- `0.0.105` の比較では `R2002` の瞬間の `PC=C2B0` は一致した
- ただし CPU 状態は一致せず、
  Picocalc は `$2002=80 A=00 P=26`、
  Mesen2 は `R2002=90 A=10 P=04` だった
- ここで差として残ったのは `PPUSTATUS` の low bits と、
  write-only PPU register read の返り値
- 現行実装は `$2002` を `PPU_R2` 直返し、write-only read は常に `0x00`
  だが、これは Mesen2 の `R2002=90` と合わない
- `0.0.106` では PPU open bus を最小実装し、
  `$2002` の low 5 bits と write-only read をその bus 由来にする

### 9.39 `0.0.107` の狙い

- `0.0.106` で `R2002=90` と `A=10` は Mesen2 に揃った
- 残る観測差は `P(flags)` と、`C2B0` 周辺の分岐へ効く zero-page 状態
- `C2A8..C2B0` 付近のコードは `LDA $2002` ポーリングの後、
  `$02/$10/$11` を使って title 初期化の分岐へ進む
- `0.0.107` では `CAPTURE_START` に `$00/$01/$02/$10/$11` を載せて、
  Picocalc と Mesen2 の zero-page を直接比較できるようにする

### 9.40 `0.0.108` の狙い

- `0.0.107` の比較では zero-page も Mesen2 と一致した
- 残る観測差は `P(flags)` だけだった
- `fixNES` の `ppuInit()` は初期化直後に `PPUSTATUS.VBlank` を立てている
- `0.0.108` では他は触らず、`PPU_Init()` 直後の
  `PPUSTATUS.VBlank` 初期値だけを `fixNES` に合わせる

### 9.41 `0.0.109` の狙い

- `0.0.108` の `PPUSTATUS.VBlank` 初期値は採用する
- ただし `fixNES` との差として、`$2002` read 抑止中でも
  こちらは VBlank 開始で `PPUSTATUS.VBlank` を毎回立てていた
- `0.0.109` では `ReadReg2` 相当の最小差分として、
  抑止中は VBlank 開始で `PPUSTATUS.VBlank` も立てない

### 9.42 `0.0.110` の狙い

- `0.0.109` の次段として、`fixNES` の `ReadReg2` にさらに寄せる
- `0.0.110` では `$2002` read があったこと自体を 1 段保持する
  `ReadReg2` 相当フラグを追加し、
  その VBlank 開始では `PPUSTATUS.VBlank` と NMI 判定の両方を抑止する

### 9.43 `0.0.111` の狙い

- `0.0.110` はグレー画面でハングしたため不採用
- `0.0.111` では `ReadReg2` 相当フラグ追加だけを戻し、
  `0.0.109` 相当の PPU 状態へ戻す

### 9.44 `0.0.112` の狙い

- `0.0.108` の `PPUSTATUS.VBlank` 初期値は維持する
- その次の 1 手として、初回フレームの開始位置だけを見直す
- `0.0.112` では `PPU_Init()` の `g_nScanLine` を `261` にして、
  最初の `PPU_Cycle()` が pre-render line から始まるようにする

### 9.45 `0.0.113` の狙い

- NESdev の power-up/reset 挙動に合わせる 1 手として、
  reset 後しばらく `$2000/$2001/$2005/$2006` write を無視する
- `0.0.113` では最初の VBlank が来るまでこの 4 register への write を
  PPU 状態へ反映しない

### 9.46 `0.0.114` の狙い

- NESdev の `PPU power up state` では `PPUSTATUS` bit7 は
  reset 後 unchanged であり、固定 1 前提ではない
- `0.0.114` では `PPU_Init()` の `PPU_R2` 初期値を 0 に戻し、
  `PPUSTATUS.VBlank` を power/reset 直後から固定で立てない

### 9.47 `0.0.115` の狙い

- `0.0.92` 以降の Lode Runner 初期画面デバッグでは、
  `0.0.95` の `PPURAM` clear 以外は明確な修正効果が確認できなかった
- 一方で `0.0.104`〜`0.0.107` の比較で、
  title 初期化列や `R2002` 周辺には Mesen2 との差が大きくないことも分かった
- `0.0.115` ではいったんコードを `0.0.95` 相当まで切り戻し、
  効果未確認の PPU 初期化/`$2002` 実験を外した状態へ戻す

### 9.48 現在の PPU 初期化契約

- `PPU_Init()` は `PPU_R2=0` から開始する
- `$2002` read の最小副作用は
  - status return
  - VBlank clear
  - write toggle reset
  に固定する
- scroll / address 系は reset で
  `PPU_Addr=0`、`PPU_Temp=0`、`PPU_Scr_H_Bit=0`、`PPU_Latch=0`
  を明示する
- `PPURAM`、`g_PalRAM`、`SPRRAM` は reset で clear する
- この契約は、Lode Runner の個別対策ではなく
  PPU の常識的な reset/power-on 初期化として固定して扱う

### 9.49 `0.0.116` の狙い

- 挙動を変えず、reset 後初回フレームの PPU 初期状態を事実として残す
- `0.0.116` では最初の `$2002` read だけ
  `PPU_R2`、`g_nScanLine`、`PPU_Addr`、`PPU_Temp`、`PPU_Latch`、`fineX`
  を 1 回だけ出す

### 9.50 `0.0.117` の狙い

- `cfxnes` と同じく、PPU の開始 scanline を `261` にする
- `PPU_R2=0` や `$2002` の最小副作用は変えず、初回 frame 進行の開始位置だけを比較する

### 9.51 `0.0.118` の狙い

- `ppu_vbl_nmi` の VBlank period / set time / clear time の失敗を受けて、
  1 scanline あたりの CPU 実行量を `114` 固定から `113,114,114` の平均 `341/3` に寄せる
- VBlank や `$2002` の副作用そのものではなく、まず frame 全体の CPU-PPU 進行量を公開仕様寄りにする

### 9.52 `0.0.119` の狙い

- `0.0.118` で `01-vbl_basics` と `03-vbl_clear_time` は通ったが、
  `02-vbl_set_time` と `06-suppression` はまだ落ちている
- そこで今回は VBlank set edge 近傍だけに絞り、
  scanline 240 終端の `$2002` read で次の VBlank set を suppress し、
  scanline 241 冒頭の `$2002` read で pending NMI を cancel する
- frame 全体の進行量や `$2002` の最小副作用は変えず、
  set edge / suppression の窓だけを 1 手で比較する

### 9.53 `0.0.120` の狙い

- `0.0.119` では前scanline終端 read にまで suppress を広げた結果、
  `02-vbl_set_time` の `--` 窓が広すぎる形になった
- そこで `0.0.120` ではその suppress を外し、
  scanline 241 冒頭の `$2002` read だけを特別扱いする
- 具体的には、scanline 241 かつ `g_wPassedClocks==0` の read では
  返す `status` から VBlank bit を落とし、pending NMI も cancel する
- さらに `g_wPassedClocks<=1` までは pending NMI の cancel だけを維持し、
  `VB_START` / `VB_START2` に近い最小モデルを比較する

### 9.54 `0.0.121` の狙い

- `0.0.120` では `03-vbl_clear_time` が再び壊れたため不採用
- そこで `0.0.121` では `0.0.119` 相当へ戻した上で、
  前scanline suppress の条件だけを `>=` から `==` に狭める
- 狙いは `02-vbl_set_time` で広がりすぎた `--` 窓を縮めつつ、
  `01-vbl_basics` と `03-vbl_clear_time` の通過を維持すること

### 9.55 `0.0.122` の狙い

- `0.0.121` でも `02-vbl_set_time` と `06-suppression` は変わらなかった
- そこで `0.0.122` では、`scanline 241` 開始で即 VBlank set する形をやめる
- `241` 行目ではまず pending 状態に置き、
  CPU 側の命令境界と `$2002` read 直前で VBlank edge を進める
- VBlank set は `g_wPassedClocks>=1`、
  NMI は `g_wPassedClocks>=2` で発火させる最小モデルを比較する

### 9.56 `0.0.123` の狙い

- 方針を切り替え、`ppu_vbl_nmi` ではなく
  `blargg_ppu_tests_2005.09.15b/vram_access.nes` を優先する
- 現行の `$2007` read は palette read 時に immediate value を返すだけで、
  underlying nametable address を delayed read buffer に入れていなかった
- そこで `0.0.123` では、
  palette read のとき `PPU_R7 = PPU_VRamRead(addr & 0x2FFF)` にして
  hidden nametable data を buffer へ入れる

### 9.57 `0.0.124` の狙い

- `blargg` の結果画面は記号表示で、写真だけでは failure code を
  毎回一意に読みにくい
- `vram_access.nes` と `vbl_clear_time.nes` の ROM 本体を確認すると、
  `result` 変数は zero-page `$00F0` に置かれている
- そこで `0.0.124` では CPU RAM write 経路で `$00F0` への書き込みだけを
  UART に出し、画面キャプチャに頼らず failure code を事実として確認する

### 9.58 `0.0.125` の狙い

- `0.0.124` の UART ログで、
  `vram_access.nes` は最後に `result=01` まで進み、
  `vbl_clear_time.nes` は `result=03` で止まることが確認できた
- `result=03` は blargg の readme では
  `VBL flag cleared too late` を意味する
- そこで `0.0.125` では、
  pre-render 行の開始で clear していた VBlank bit を
  1 行前 (`scanline 260`) の終端側で落とす最小修正だけを比較する

### 9.59 `0.0.126` の狙い

- `0.0.125` でも `vbl_clear_time.nes` は `result=03` のままで、
  clear edge を前に寄せても変化がなかった
- 許可済み参照先では、`fixNES` は VBlank flag を `L241+2`、
  NMI 許可を `L241+4` に置いている
- 現行 `Picocalc_NESco` は VBlank set 後 `g_wPassedClocks>=2` で
  NMI を立てているため、`0.0.126` ではそこだけ `>=4` に後ろへ寄せて比較する

### 9.60 `0.0.127` の狙い

- Lode Runner の現症状は、ロゴや文字よりも背景を連続で埋める区間の
  `$2006/$2007` と `PPU_Addr` 進行が本命と再整理した
- そこで `0.0.127` では、既存の `$2005/$2006/$2007` 診断を流用し、
  最初の `$2001=00` を起点にした短い capture を追加する
- 狙いは、title 初期化中の背景 upload 区間だけを短く取り、
  `PPU_Addr/PPU_Temp/PPU_Latch` が CPU の意図どおり進んでいるかを見ること

### 9.61 `0.0.128` の狙い

- Lode Runner のタイトル崩れについて、まず VRAM 自体が壊れているかを確定する方針に切り替えた
- `0.0.127` の背景 upload capture は発火しておらず、`$2006/$2007` の進行比較へまだ入れていない
- そこで `0.0.128` では、最初の `$2001=00` を起点に
  `$2000-$23FF`, `$2400-$27FF`, `$3F00-$3F1F` の VRAM ダンプを 1 回だけ UART に出す
- 狙いは、崩れフレームで
  - VRAM の中身自体が壊れているのか
  - VRAM は正しいが描画結果だけが壊れているのか
  を先に分岐させること

### 9.62 `0.0.129` の狙い

- `0.0.128` では VRAM ダンプ自体は取れたが、最初の `$2001=00` 直後で早すぎて、背景 upload 後の VRAM 状態を見られていない
- そこで `0.0.129` では、最初の `$2001=00` 以後の `$2007` 書き込み回数を数え、
  1回目・256回目・1024回目で VRAM ダンプを 3 回取る
- 狙いは、背景 upload の最初・中頃・後半で
  VRAM の中身がどう変化しているかを事実で比較すること

### 9.63 `0.0.130` の狙い

- `0.0.129` では VRAM ダンプ自体は複数回取れたが、`$2001=00` が来るたびに
  書き込みカウンタが再スタートしていて、背景 upload の後半まで到達できていない
- そこで `0.0.130` では、最初の `$2001=00` で 1 回だけ arm し、
  `$2007` 書き込み回数 `1024 / 1536 / 2048` で VRAM ダンプを 3 回取る
- 狙いは、1 面ぶんの nametable+attribute 量を越えた後の VRAM 状態を取り、
  崩れた完成画面に近い段階の内容を比較すること

### 9.64 `0.0.131` の狙い

- `0.0.130` の VRAM ダンプは、rendering on に戻った後の `$2007` まで数えてしまい、完成画面表示後の書き込みが混ざっていた
- そこで `0.0.131` では、最初の `$2001=00` で window を開き、
  `$2001!=00` で window を閉じるようにして、その間だけ `$2007` を数える
- 狙いは、rendering off 中の背景 upload だけを対象に
  `1024 / 1536 / 2048` 時点の VRAM を取ること

### 9.65 `0.0.132` の狙い

- `0.0.131` は rendering off 中の `$2007` だけを見る形だったが、
  背景表示が壊れる瞬間を見るには狭すぎる可能性がある
- そこで `0.0.132` では `$2007` 書き込み回数ベースをやめ、
  最初の `$2001=00` 後に最初に表示へ入る `frame0` と、
  その 1 フレーム後の `frame1` 開始時点で VRAM を 2 回ダンプする
- 狙いは、0フレーム目と1フレーム完成後の VRAM を比較して、
  背景が最初から壊れているか、1フレーム進行で壊れるかを切り分けること

### 9.66 `0.0.133` の狙い

- `0.0.132` の `frame0 / frame1` ダンプは 2 組出ていて、
  最初の `$2001=00` だけでなく後続の `$2001=00` でも再 arm していた
- そこで `0.0.133` では、最初の `$2001=00` で 1 回だけ arm し、
  `frame0 / frame1` の 1 組だけを取るようにする
- 狙いは、同じタイトル初期化の途中経過が混ざらない形で
  0フレーム目と1フレーム完成後の VRAM を比較すること

### 9.67 `0.0.134` の狙い

- `0.0.133` では最初の `$2001=00` から見る方式だったため、
  依然としてタイトル構築には早すぎる VRAM を見てしまっていた
- そこで `0.0.134` では `$2001=00` 起点をやめ、
  rendering off 中に `PPU_Addr` が `23C0-23FF` の属性領域へ入った瞬間を
  1 回目のダンプにする
- さらに、その後 rendering on に戻った最初のフレーム開始を
  2 回目のダンプにして、属性更新後と表示再開後を比較する

### 9.68 `0.0.135` の狙い

- Mesen2 観測では、`23C0-23FF` の属性更新だけではまだ途中で、
  その後 `27C0-27FF` 側の属性更新も通ってから `W2001=08` へ戻っていた
- `0.0.134` の `23C0` トリガは早すぎて、
  取れた VRAM もほぼ初期段階のままだった
- そこで `0.0.135` では、rendering off 中の `$2007` が `27FF` を書いた直後を
  1 回目のダンプにして、両方の属性更新を通過した後の VRAM を取る
- 2 回目のダンプはそのまま rendering on 復帰後の最初のフレーム開始で取り、
  属性更新完了後と表示再開後を比較する

### 9.69 `0.0.136` の狙い

- Mesen2 実測では、2 回目の参照点は単なるフレーム開始ではなく、
  rendering on 復帰後に `PPU_Addr=2800` へ進んだ時点だった
- そこで `0.0.136` では 1 回目の `27FF` トリガは維持しつつ、
  2 回目のダンプを `PPU_R1!=0`, `PPU_Addr=2800`, `PPU_Latch=0`,
  `fineX=0` の複合条件へ寄せる
- 狙いは、Picocalc 側でも Mesen2 と同じ内部状態点で
  VRAM を比較できるようにすること

### 9.70 `0.0.137` の狙い

- Mesen2 の `W2001/W2006` ログを取り直した結果、
  タイトル背景の本番 upload は最初の off/on 区間ではなく、
  2 回目の `PPUMASK=00` 以降に始まっていた
- そこで `0.0.137` では、最初の `$2001=00` は無視し、
  2 回目の `$2001=00` で初めて VRAM ダンプを arm する
- その後の `27FF` と `2800` の 2 点を取り、
  Mesen2 と同じ本番 upload 区間の VRAM を比較できるようにする

### 9.71 `0.0.138` の狙い

- Build ID: `Apr 15 2026 21:58:45`
- Mesen2 の 2 回目 off 区間 VRAM ダンプを `256 / 512 / 1024 / 1152 / 1280 / 1408 / end`
  で取り直した結果、`256 / 512 / 1024` はまだ `20` 埋めだけで、
  `1152` からタイトル本体タイルが出始めることを確認した
- そこで `0.0.138` では Picocalc 側の VRAM ダンプ条件を
  `2 回目の $2001=00` 以降の `$2007` 書き込み `1152` 回時点と、
  その off 区間終了時の `end_window` の 2 点へ切り替える
- 狙いは、Mesen2 と同じ「意味のある背景 upload 途中状態」と
  off 区間完了状態だけを比較対象にすること

### 9.72 `0.0.139` の狙い

- Build ID: `Apr 15 2026 22:09:34`
- `0.0.138` では `cp1152` の 1 点固定が Picocalc 側で捕まらなかった
- そこで `0.0.139` では方針を変え、
  2 回目 off 区間の `1024` write 以降に来る `$2006` 再設定ごとに
  `mid_.._w.._a....` ラベル付きの VRAM ダンプを複数出す
- 狙いは、Picocalc 側で Mesen2 の `cp1152 / cp1280 / cp1408` に対応する
  途中状態を後から特定できるようにすること

### 9.73 `0.0.140` の狙い

- Build ID: `Apr 15 2026 22:15:05`
- `0.0.139` では途中ダンプをまだ `21xx-23xx` 帯へ絞っていた
- そこで `0.0.140` ではその帯域制限も外し、
  2 回目 off 区間の `1024` write 以降に来る `$2006` 2 回目書き込みを
  ほぼ全部 `mid_.._w.._a....` ラベル付きでダンプ対象にする
- 狙いは、Picocalc 側でどのアドレス再設定列が実際に来ているかを
  取りこぼさず観測すること

### 9.74 `0.0.141` の狙い

- 書いた日時: `2026-04-15 22:34 JST`
- Mesen2 の 2 回目 off 区間後半を改めて観測すると、
  `cp1152` 近傍では内部 `videoRamAddr` が
  `218A/21AA/218C/21AC/.../2196/21B6` の帯を進んでいた
- 一方で Picocalc 側は `$2006` 再設定フックでは途中ダンプが 1 件も出ていない
- そこで `0.0.141` では、
  2 回目 off 区間中の内部 `PPU_Addr=2180-21BF` を一度ずつ
  `mid_.._w.._a....` ラベル付きでダンプ対象に切り替える
- 狙いは、Mesen2 の `cp1152` 近傍と同じ内部アドレス帯に
  Picocalc が実際に入っているかを直接観測すること

### 9.75 `0.0.141` の観測結果

- 書いた日時: `2026-04-15 22:40 JST`
- `pico20260415_222922.log` では、
  `mid_01_w225_a2188` から `mid_16_w260_a21B6` までの途中ダンプと
  `end_window` が出た
- つまり Picocalc 側でも、2 回目 off 区間中に
  内部 `PPU_Addr=2188..21B6` の帯へ入っていることは確認できた
- ただし、その時点の VRAM 内容は Mesen2 の `cp1152` と一致しない
- 具体的には、
  Picocalc では `NT0 20A0` の先頭 2 byte が `00 00`、
  `NT0 2140` の先頭 8 byte が `00`、
  `NT0 2180` と `NT1 2400-27FF` が広く `00` のままだった
- 一方 Mesen2 の `cp1152` では、
  `NT0 20A0` 先頭が `20 20`、
  `NT0 2140` 先頭 8 byte も `20`、
  `NT0 2180` に `20` が入り、
  `NT1 2400-27FF` も `20` 埋めされていた
- さらに write 数も
  Picocalc 側は `225..260`、
  Mesen2 側は `1152..1179` で大きく違う
- ここから、Picocalc は Mesen2 と同じ内部アドレス帯に入っても、
  その時点までの VRAM 更新進行量が大きく遅れていることが確認できた

### 9.76 `0.0.142` の狙い

- 書いた日時: `2026-04-15 22:48 JST`
- `0.0.141` の途中ダンプ比較で、問題はレンダリング結果ではなく
  VRAM 内容そのものの不一致だとさらに絞れた
- Picocalc は Mesen2 と同じ `PPU_Addr=2188..21B6` 帯に入っても、
  その時点の `NT0/NT1` がまだ広く `00` のままで、
  write 数も `225..260` と Mesen2 の `1152..1179` から大きくずれていた
- そこで `0.0.142` では、2 回目 off 区間の最初の 320 回の `$2007` write を
  `w, pc, val, before, after, inc, r1, scan, clocks` 付きでそのままログする
- 狙いは、`20` 埋めの連続転送が Pico 側でどの書き込みから崩れるか、
  そして `addr_before/after` がどこで Mesen2 と違い始めるかを直接見ること

### 9.77 `0.0.143` の狙い

- 書いた日時: `2026-04-15 22:56 JST`
- `0.0.142` では `$2007` write ログに上限 `320` を置いていた
- しかし今回の目的は、「その範囲で取れるか」ではなく、
  2 回目 off 区間の relevant `$2007` write を確実に全部取ること
- そこで `0.0.143` では上限を外し、
  2 回目 off 区間の relevant `$2007` write を終了まで全部
  `w, pc, val, before, after, inc, r1, scan, clocks`
  付きでログする
- 狙いは、観測範囲不足による取りこぼしをなくし、
  Pico 側で `20` 埋めが崩れる最初の書き込みを必ず捕まえること

### 9.78 `0.0.144` の狙い

- 書いた日時: `2026-04-15 23:06 JST`
- `0.0.143` の全 `$2007` ログで、
  Pico 側は `w=1..104` を `2000..2067` へ `20` で埋めたあと、
  `w=105` でいきなり `before=20A8 val=D0` へ切り替わることが確認できた
- 次に必要なのは、
  その `w=104 -> 105` 境界の直前に
  どの `$2006` 再設定が入って `20A8` を作っているかを事実で見ること
- そこで `0.0.144` では、
  同じ 2 回目 off 区間の `$2006` 1 回目/2 回目書き込みも
  `n, w, pc, phase, data, temp, addr, r1, scan, clocks`
  付きで全部ログする
- 狙いは、`$2007` 列だけでは見えないアドレス再設定の原因を
  同じ観測窓の中で直接確認すること

### 9.79 `0.0.145` の狙い

- 書いた日時: `2026-04-15 23:11 JST`
- Mesen2 実測では、
  `DD5D / DD60` が `20A8` を作ること自体は正常系でも起きている
- したがって本命は `$2007` increment や `$2006` アドレス生成そのものではなく、
  その直後の `DD63` 周辺が作る値列へ移った
- 次に必要なのは、
  Pico 側でも 2 回目 off 区間中の
  `DD5A / DD5D / DD60 / DD63`
  実行時だけを抜き、
  `a/x/y/p/sp` と `PPU_Addr / PPU_Temp / latch / fineX`
  を Mesen2 と並べること
- そこで `0.0.145` では、
  2 回目 off 区間中の上記 4 点に限って
  `pc, op, a/x/y/p/sp, addr, temp, latch, fineX, r1, scan, clocks`
  を出す
- 狙いは、`20A8` 以降で最初に値生成が食い違う瞬間を
  CPU 側状態として直接比較できるようにすること

### 9.80 `0.0.146` の狙い

- 書いた日時: `2026-04-15 23:33 JST`
- `0.0.145` では `CPU_LR` ログ自体は追加したが、
  実機ログには出ていなかった
- これは `CPU_LR` が `NESCO_DIAGNOSTICS` 条件内にあり、
  通常ビルドでは有効になっていなかったため
- そこで `0.0.146` では、
  `CPU_LR` だけは `PICO_BUILD` 条件で出すようにして、
  2 回目 off 区間の
  `DD5A / DD5D / DD60 / DD63`
  実行時状態を通常ビルドでも必ず観測できるようにする
- 狙いは、Mesen2 側で保存済みの `DD5D/DD60/DD63` 実測ログと、
  Pico 側の CPU 状態を確実に直接比較できるようにすること

### 9.81 `0.0.147` の整理

- 書いた日時: `2026-04-16 20:05 JST`
- プレイ中の UART 出力でゲーム進行が止まりやすくなっていたため、
  実機観測用の重いランタイムログを一括で切れるように整理した
- `CMakeLists.txt` に `NESCO_RUNTIME_LOGS` オプションを追加し、
  既定値は `OFF` にした
- `core/ppu.c` の
  `PPU_INIT` / `PPU_DUMP` / `PPU_W2006` / `PPU_W2007` / `PERF`
  などのランタイムログは
  `NESCO_RUNTIME_LOGS` 定義時だけ有効になるように変更した
- `core/cpu.c` の `CPU_LR` も同じく
  `NESCO_RUNTIME_LOGS` 定義時だけ有効になるように変更した
- これにより、通常ビルドではログ出力なしでプレイ可能、
  必要なときだけ `NESCO_RUNTIME_LOGS=ON` で同じ観測を復活できる形にした
- `build/` で `make clean && make -j4` を実行し、
  `warning:` / `error:` が出ないことを確認した

### 9.82 `0.0.148` の小粒 timing 修正

- 書いた日時: `2026-04-16 20:43 JST`
- 現行 core の仕様差分のうち、
  まず副作用が小さいものから詰める方針で
  controller / OAM DMA / PPU timing の局所修正を入れた
- `core/cpu.c` では、
  標準コントローラ前提で `$4016/$4017` の 8bit 読み出し後は
  `1` を返すように変更した
- `core/cpu.c` では、
  `$4014` OAM DMA の停止サイクルを
  `g_wPassedClocks` の parity を見て `513/514` で分岐させた
- `core/ppu.c` では、
  rendered odd frame の終端で CPU scanline phase を 1 つ進める
  最小の odd-frame 補正を追加した
- `core/ppu.c` では、
  VBlank 中に `$2000` の NMI enable を 0→1 にしたとき
  pending NMI を立てる最小修正を追加した
- `docs/project/PROGRESS_TODO.md` に今回の変更点と次の観測対象を追記した
- `build/` で `make -j4` を実行し、ビルド成功を確認した

### 9.83 `0.0.149` のタイトル初期化ログ整列

- 書いた日時: `2026-04-16 20:59 JST`
- Lode Runner の比較で使っていた Pico 側の `PPU_INIT` は、
  「最初の `$2002` read 全体」を捕まえていた
- 一方、保存済みの Mesen2 基準
  `mesen_loderunner_title_regs.log`
  は「最初の VBlank=1 の `$2002` read」から
  以後の register write 列を採っていた
- このままではログ開始点がずれており、
  Pico 側で最初に `2000` が見えることを
  そのまま emulation 差分とは断定できない
- そこで `0.0.149` では、
  Pico 側も最初の `VBlank=1` な `$2002` read を起点にして
  `W2001 / W2006 / W2005 / W2000 / W2007`
  を短く採る軽い runtime log を追加した
- 既存の 2 回目 `PPUMASK=00` 窓向け
  `PPU_W2006 / PPU_W2007 / PPU_DUMP`
  系ログはそのまま残している
- `build/` で `cmake -S . -B build -DNESCO_RUNTIME_LOGS=ON`
  の後に `make -j4` を実行し、
  `PicoCalc NESco Ver. 0.0.149 Build Apr 16 2026 20:57:26`
  を生成した
- 次はこの build を実機へ入れ、
  `first_vblank_$2002` と
  `PPU_TITLE_W2001/W2006/W2005/W2000/W2007`
  を Mesen2 側の title init ログと直接並べて、
  最初の不一致点を 1 箇所に絞る

### 9.84 `0.0.150` の second-window scroll/control 追跡

- 書いた日時: `2026-04-16 21:05 JST`
- `0.0.149` の比較で、
  タイトル初期化先頭の
  `R2002 -> W2001 -> W2006 -> W2005 -> W2000 -> W2007`
  列は Pico 側でも Mesen2 と揃っていることを確認できた
- 一方、Lode Runner の崩れ本体は
  2 回目 `PPUMASK=00` 窓の後半に残っており、
  `DD5D/DD74` 系の `$2006` 再設定まわりが引き続き主戦場
- ここでは `$2006/$2007` だけでなく、
  直前後の `$2005/$2000` と
  `PPU_Addr/PPU_Temp/R2`
  も並べないと、loopy state のズレ開始点を切りにくい
- そこで `0.0.150` では、
  second-window 中かつ `PPU_R1==00` のときだけ
  `PPU_WIN_W2005` と `PPU_WIN_W2000`
  を追加で出すようにした
- 既存の `PPU_W2006/PPU_W2007/PPU_DUMP` と組み合わせることで、
  Pico 側でも
  `W2005/W2000/W2006/W2007`
  のまとまりを直接比較できる
- `build/` で `cmake -S . -B build -DNESCO_RUNTIME_LOGS=ON`
  の後に `make -j4` を実行し、
  `PicoCalc NESco Ver. 0.0.150 Build Apr 16 2026 21:03:50`
  を生成した
- 次はこの build を実機へ入れ、
  second-window 中の
  `PPU_WIN_W2005/PPU_WIN_W2000/PPU_W2006`
  を `mesen_nmi_timing.log` と突き合わせて、
  `$2006` 再設定直前の state 差分を確認する

### 9.85 `0.0.151` の post-window loopy probe

- 書いた日時: `2026-04-16 21:27 JST`
- `0.0.150` の比較では、
  second-window 後半の
  `W2006 / W2005 / W2000`
  列と再設定先アドレスは Pico 側でもかなり揃っていた
- そのため、残る本命を
  register write 列そのものではなく
  render 側の loopy 更新
  (`reload_x / coarse_x / fine_y`)
  と見て次へ進めることにした
- `0.0.151` では、
  `end_window` 後に rendering が戻った最初の 8 scanline だけ
  `PPU_LOOPY_BEFORE_RELOAD`
  `PPU_LOOPY_AFTER_RELOAD`
  `PPU_LOOPY_TILE_INC`
  `PPU_LOOPY_BEFORE_FINEY`
  `PPU_LOOPY_AFTER_FINEY`
  を出す軽い probe を追加した
- 既存の second-window 用
  `PPU_W2006 / PPU_W2007 / PPU_WIN_W2005 / PPU_WIN_W2000`
  はそのまま残している
- `build/` で `cmake -S . -B build -DNESCO_RUNTIME_LOGS=ON`
  の後に `make -j4` を実行し、
  `PicoCalc NESco Ver. 0.0.151 Build Apr 16 2026 21:26:22`
  を生成した
- 次はこの build を実機へ入れ、
  `PPU_LOOPY_*` を含む UART ログを取り、
  Mesen2 側の
  `mesen_loderunner_second_window_loopy.log`
  と直接並べて、
  loopy 更新のどの段で差分が出るかを確認する

### 9.86 `0.0.152` の post-window phase 修正

- 書いた日時: `2026-04-16 21:45 JST`
- `pico20260416_212200.log` を確認すると、
  `end_window` dump は 2 回出ている一方で
  `PPU_LOOPY_*`
  は 0 件だった
- 現行 `0.0.151` の `core/ppu.c` では、
  `$2001 != 0` の write が
  `phase==1`
  で `phase=2` を開始した直後、
  もう一度 nonzero write が来ると
  `phase==2`
  でも `phase=0`
  に戻す実装になっていた
- `0.0.152` ではこの phase 管理を最小修正し、
  `phase==1`
  の最初の nonzero write だけで
  `end_window` と
  post-window probe を開始し、
  `phase==2`
  中の追加 nonzero write では
  probe を止めないようにした
- 目的は、
  `PPU_DrawLine()`
  に入る前に
  `PPU_LOOPY_*`
  が無効化される空振りを防ぐこと
- 次はこの build を実機へ入れ、
  `PPU_LOOPY_*`
  が実際に出るかを確認する

### 9.87 `0.0.153` の post-window 観測窓拡張

- 書いた日時: `2026-04-16 21:53 JST`
- `0.0.152` の `pico20260416_212858.log` では、
  `PPU_LOOPY_*`
  は出るようになった
- 一方で、
  その開始は
  `scan=4`
  で、
  `addr=0000 temp=0000`
  から始まっており、
  second-window 直後を直接捉えているとはまだ言えなかった
- `0.0.153` では、
  loopy 本体には触れず、
  post-window probe の観測窓だけを
  `8 scanline`
  から
  `32 scanline`
  に拡張した
- あわせて
  `PPU_LOOPY_START`
  を 1 行追加し、
  probe を開始した瞬間の
  `scan/addr/temp/latch/r0/r1/r2`
  をすぐ見えるようにした
- 目的は、
  second-window 復帰から
  `PPU_DrawLine()`
  による loopy 観測開始までのズレが
  どこにあるかを先に確定すること
- 次はこの build を実機へ入れ、
  `PPU_LOOPY_START`
  と
  `PPU_LOOPY_*`
  の開始位置を確認する

### 9.88 `0.0.154` の VBlank 開始順序整理

- 書いた日時: `2026-04-16 22:10 JST`
- 現行 `0.0.153` では、
  `scanline 241`
  開始時に
  `s_ppu_vblank_pending`
  `s_ppu_nmi_pending`
  `InfoNES_LoadFrame()`
  `InfoNES_PadState()`
  `MapperVSync()`
  をまとめて実行していた
- 今回の狙いは、
  副作用を減らすことではなく、
  `scanline 241`
  で CPU から見える順序を
  少し実機寄りに整理すること
- `0.0.154` では、
  `VBlank flag`
  と
  `NMI pending`
  は従来どおり
  CPU がその scanline を実行する前に立てつつ、
  `InfoNES_LoadFrame()`
  `InfoNES_PadState()`
  `MapperVSync()`
  は
  `K6502_Step()`
  の直後に遅らせた
- これで、
  `scanline 241`
  の CPU 実行中に見える
  `VBlank`
  と、
  そのフレーム末尾 side effect の順序が分離される
- 次はこの build を実機へ入れ、
  Lode Runner のタイトル崩れと
  `PPU_LOOPY_START`
  / `PPU_LOOPY_*`
  の開始位置がどう変わるかを確認する

### 9.89 `0.0.155` の PPU_DrawLine 入口観測

- 書いた日時: `2026-04-16 22:17 JST`
- `0.0.154` の結果では、
  `PPU_LOOPY_START`
  は
  `scan=241`
  に出る一方、
  実際の
  `PPU_LOOPY_*`
  は
  依然として
  `scan=4`
  から始まっていた
- つまり、
  post-window probe 自体は開始できているが、
  second-window 復帰直後の
  `PPU_DrawLine()`
  がどう扱われているかはまだ不明だった
- `0.0.155` では、
  loopy 本体には触れず、
  post-window probe 中だけ
  `PPU_DrawLine()`
  の入口と early return を
  `PPU_DRAWLINE_ENTER`
  `PPU_DRAWLINE_BORDER`
  `PPU_DRAWLINE_MASKED`
  として記録するようにした
- 目的は、
  `scan=242..3`
  で
  `PPU_DrawLine()`
  自体が呼ばれていないのか、
  border/clip 条件で戻っているのかを先に確定すること
- 次はこの build を実機へ入れ、
  `PPU_DRAWLINE_*`
  と
  `PPU_LOOPY_*`
  の関係を確認する

### 9.90 `0.0.156` の pre-render vertical copy probe

- 書いた日時: `2026-04-16 22:24 JST`
- `0.0.155` の結果では、
  `scan=0..3`
  の
  `PPU_DrawLine()`
  自体は走っており、
  そこで
  `PPU_DRAWLINE_BORDER`
  に落ちるため
  `PPU_LOOPY_*`
  が
  `scan=4`
  から始まっていることが確認できた
- この時点で、
  そのズレ自体は
  `PPU_UpDown_Clip=4`
  で説明できる
- 次の本命は、
  pre-render での vertical copy が
  `scan=0`
  前後の
  `PPU_Addr/PPU_Temp`
  をどう動かしているか
  に移る
- `0.0.156` では、
  `VBLANK_END_SCANLINE`
  で行っている
  `PPU_Addr = (PPU_Addr & 0x041F) | (PPU_Temp & ~0x041F)`
  の前後を
  `PPU_PRERENDER_COPY_BEFORE`
  `PPU_PRERENDER_COPY_AFTER`
  として記録するようにした
- 目的は、
  `scan=0`
  に入る前に
  `PPU_Addr/PPU_Temp`
  がどこで
  `0000`
  系へ寄っているのかを確認すること
- 次はこの build を実機へ入れ、
  `PPU_PRERENDER_COPY_*`
  と
  `PPU_DRAWLINE_*`
  のつながりを確認する

### 9.91 `0.0.157` の pre-render vertical copy 遅延

- 書いた日時: `2026-04-16 22:31 JST`
- ここまでは probe を重ねて、
  `scan=0..3`
  は border clipping で説明できること、
  そして次の本命が
  pre-render の vertical copy 位置であることを確認した
- `0.0.157` では、
  予定していた実挙動変更として、
  pre-render の vertical copy
  (`PPU_Addr = (PPU_Addr & 0x041F) | (PPU_Temp & ~0x041F)`)
  を
  `VBLANK_END_SCANLINE`
  開始時の即時反映から外し、
  その pre-render scanline の後半相当として
  `PPU_Cycle()`
  終端へ遅らせた
- `VBlank clear`
  自体は pre-render 開始時に維持している
- 目的は、
  loopy vertical copy の位置を
  NESdev の
  pre-render 後半
  に少し寄せ、
  `scan=0`
  前後での
  `PPU_Addr/PPU_Temp`
  の単純化を抑えられるかを見ること
- 次はこの build を実機へ入れ、
  Lode Runner の背景崩れと
  `PPU_PRERENDER_COPY_*`
  / `PPU_DRAWLINE_*`
  の変化を確認する

### 9.92 `0.0.158` の pre-render copy 遅延取り下げ

- 書いた日時: `2026-04-16 22:40 JST`
- `0.0.157` では、
  pre-render の vertical copy を
  pre-render scanline 後半へ遅らせる変更を入れた
- しかし
  `pico20260416_221832.log`
  では、
  `PPU_PRERENDER_COPY_AFTER`
  の直後に
  `addr=0408`
  などへ強く寄る状態が大量に出ており、
  実機では画面が真っ暗になって表示不能になった
- まずは表示復帰を優先し、
  `0.0.158`
  でこの変更を取り下げた
- pre-render vertical copy は、
  いったん従来どおり
  `VBLANK_END_SCANLINE`
  開始時の即時反映へ戻している
- 次はこの build を実機へ入れ、
  画面表示が復帰するかを確認する

### 9.93 `0.0.159` の pre-render vertical bits 限定

- 書いた日時: `2026-04-16 22:30 JST`
- `0.0.157`
  で pre-render copy の位置を大きく動かすと、
  黒画面になるほど強く効くことが分かった
- 一方、
  `0.0.158`
  で即時反映へ戻しても、
  ユーザー確認では
  「完全には戻っていない」
  状態が残った
- そこで
  `0.0.159`
  では、
  pre-render vertical copy の
  タイミングは動かさず、
  コピー対象だけを狭めた
- 具体的には
  `PPU_Addr = (PPU_Addr & 0x041F) | (PPU_Temp & ~0x041F)`
  ではなく、
  loopy の vertical bits
  (`fine Y / coarse Y / NT-Y`)
  に相当する
  `0x7BE0`
  だけを
  `PPU_Temp`
  から移す形に変更した
- 目的は、
  pre-render copy の感度が高い点を保ったまま、
  horizontal 系や非 loopy bit を巻き込む副作用を減らし、
  より小さい調整幅で
  Lode Runner 背景崩れの改善余地を探ること
- 次はこの build を実機へ入れ、
  画面が維持されるかと、
  背景崩れが少しでも改善するかを確認する

### 9.94 `0.0.160` の `0.0.156` 基準復帰

- 書いた日時: `2026-04-16 22:37 JST`
- ユーザー判断として、
  `0.0.158`
  は完全には戻っておらず、
  `0.0.159`
  もその影響を受けている可能性があるため、
  いったん
  `0.0.156`
  を基準に戻す方針へ切り替えた
- そこで
  `0.0.160`
  では、
  pre-render vertical copy の式を
  `0.0.156`
  と同じ
  `PPU_Addr = (PPU_Addr & 0x041F) | (PPU_Temp & ~0x041F)`
  に戻した
- その上で、
  完全な据え置きにはせず、
  この vertical copy 自体を
  `R1_RENDERING`
  が有効なときだけ実行する
  小さい条件追加を行った
- 目的は、
  `0.0.157-0.0.159`
  の影響を切り離しつつ、
  NESdev に近い
  「rendering 中のみ pre-render vertical copy」
  という条件だけを安全に足して、
  Lode Runner の背景崩れに変化が出るかを見ること
- 次はこの build を実機へ入れて、
  表示が
  `0.0.156`
  系の状態へ戻るかと、
  背景崩れに変化が出るかを確認する

### 9.95 `0.0.161` の `0.0.160` 取り下げ

- 書いた日時: `2026-04-16 22:40 JST`
- `pico20260416_223749.log`
  では、
  `0.0.160`
  でも黒画面が継続した
- ログ上は、
  pre-render copy 自体は
  引き続き実行されており、
  `R1_RENDERING`
  ガード追加は
  黒画面回避には効いていないことが確認できた
- そこで
  `0.0.161`
  では、
  `0.0.160`
  の小調整を取り下げ、
  pre-render vertical copy を
  コード上も完全に
  `0.0.156`
  と同じ
  `PPU_Addr = (PPU_Addr & 0x041F) | (PPU_Temp & ~0x041F)`
  へ戻した
- 目的は、
  まず表示状態を
  `0.0.156`
  基準へ確実に戻し、
  そこから別の論点を触るための
  安定した基準点を取り戻すこと
- 次はこの build を実機へ入れて、
  少なくとも表示が
  `0.0.156`
  基準へ戻るかを確認する

### 9.96 `0.0.162` の `0.0.155` 基準 checkout

- 書いた日時: `2026-04-16 22:43 JST`
- ユーザーから、
  手修正ではなく
  git から正確に戻すよう指定があった
- そこで
  `0.0.162`
  では、
  `core/ppu.c`
  を
  `git checkout f417d2c -- core/ppu.c`
  で
  `0.0.155`
  の内容へ戻した
- 今回は
  `ppu.c`
  の挙動を
  `0.0.155`
  基準へ戻すことを最優先にし、
  余分な手修正は加えていない
- 次はこの build を実機へ入れて、
  少なくとも
  `0.0.155`
  系の表示状態へ戻るかを確認する

### 9.97 `0.0.163` の `$2006` 2回目 side effect 縮小

- 書いた日時: `2026-04-16 22:53 JST`
- `0.0.155`
  基準へ戻したうえで、
  予定どおり
  `$2006`
  2回目直後の副作用を見直した
- 現行コードで
  `PPU_RegisterWrite($2006)`
  の
  2回目直後に起きている実副作用を確認すると、
  挙動に効いている追加処理は
  pattern-table address 時の
  `PPU_R7 = PPU_VRamRead(PPU_Addr)`
  だけだった
- `InfoNES_SetupScr()`
  は
  platform 側で no-op なので、
  今回の変更対象から外した
- そこで
  `0.0.163`
  では、
  `$2006`
  2回目直後の
  `PPU_R7`
  即時 priming を削除し、
  `PPU_Temp -> PPU_Addr`
  の反映だけを残した
- 目的は、
  実ゲームの
  VBlank 中 bulk upload に関係しない可能性が高い
  余計な read-buffer side effect を減らし、
  Lode Runner で変化が出るかを
  小さい差分で確認すること
- 次はこの build を実機へ入れて、
  Lode Runner の表示変化を確認する

- 動画
  `IMG_8231.MOV`
  と
  UART log
  `pico20260416_230124.log`
  を見直すと、
  初回のゲーム画面が出た時点で
  すでに背景が壊れていることが確認できた
- そのため、
  second-window / post-window
  の後半観測より前に、
  first visible frame
  手前の title-init / 初回表示直前後へ
  調査の重心を戻す方針へ切り替えた
- `0.0.164`
  では、
  `0.0.162`
  基準の挙動は変えずに、
  title capture は残したまま
  second-window / post-window
  の runtime log arm と出力を無効化した
- 目的は、
  後半ログを止めて
  初回表示時点の崩れに集中できるようにすること

- その後
  `0.0.164`
  の Pico log と
  Mesen2 title-init log
  を比較すると、
  少なくとも観測した
  register write 列は
  ほぼ一致していた
- そこで
  次は
  first visible frame
  に近い
  VRAM 最終像の比較へ進めるため、
  Pico 側でも
  `ppu_diag_dump_title_vram()`
  を
  `title_palette_done`
  `title_fill_early`
  `frame_on`
  の 3 点で呼ぶようにした
- `0.0.165`
  では、
  title-init 途中の dump と
  最初の rendering on 時の dump を取り、
  Mesen2 の
  `attr2`
  / `frame_on`
  dump と直接比較しやすくした

- `0.0.165`
  の dump を見ると、
  `title_fill_early`
  では一部入っていた
  `20`
  埋めが、
  `frame_on`
  では
  再び
  `00`
  優勢になっていた
- このため、
  単純な
  CPU 窓不足
  だけでなく、
  first visible frame
  までの後続書き込み列で
  nametable 内容が変わっている可能性が高くなった
- `0.0.166`
  では、
  title capture の観測窓を
  `96`
  から
  `224`
  へ広げ、
  `frame_on`
  までの
  `$2007`
  列をそのまま追えるようにした

- `0.0.167`
  では、
  `scanline 261`
  開始時の
  pre-render vertical copy
  を
  無条件実行から外し、
  `R1_RENDERING`
  が立っているときだけ
  vertical bits の
  `t -> v`
  copy を行うようにした
- 根拠は、
  NESdev
  が pre-render scanline の
  dots `280-304`
  を
  `rendering is enabled`
  条件付きで説明していること、
  そして
  `fixNES`
  でも
  `PPU_PRE`
  では
  `updateBGVertAddress()`
  を呼ぶが
  `PPU_PRE_OFF`
  では呼ばないことを確認したこと
- 目的は、
  `PPUMASK=00`
  の title-init bulk upload 中に
  `203C -> 001D`
  へ飛ぶ誤った
  `PPU_Addr`
  更新を止め、
  first visible frame
  まで
  `20xx`
  の連続書き込みを維持すること

- `0.0.168`
  では、
  `0.0.167`
  で修正が効いたあと、
  ゲーム本編の確認を優先するため
  title-init / frame-on 用の
  runtime log と
  VRAM dump を停止した
- `s_ppu_enable_title_logs`
  を
  `0`
  にして、
  `PPU_INIT`
  `PPU_TITLE_*`
  `PPU_DUMP`
  の診断出力を止めている
- 挙動変更は無く、
  診断出力の整理のみ

- `0.0.169`
  では、
  次の調整前に
  現在の速度を再確認するため、
  以前の
  `[PERF]`
  1秒集計だけを通常 Pico build でも出すよう戻した
- title-init / frame-on 用の詳細 runtime log は停止のままとし、
  perf 出力だけを分離して有効化している
- 目的は、
  `cpu_us / apu_us / draw_us / tail_us`
  を再観測し、
  次の調整前の比較元を取ること

- `0.0.170`
  では、
  `[PERF]`
  に軽い描画統計を追加した
- 追加項目は
  `vis / border / masked / sp_en / sp_lines / sp_sum / sp_max / fineX / bgclip`
  で、
  sprite 合成が多い秒なのか、
  fine-X や clip で fast path を外している秒なのかを
  ログだけで見分けやすくするのが目的
- title-init / frame-on の詳細 runtime log は引き続き停止のまま

- `0.0.171`
  では、
  `compositeSprites()`
  が
  実際に opaque sprite が存在する
  `sprite_min_x / sprite_max_x`
  を返すようにした
- `PPU_DrawLine()`
  では、
  その範囲外の pixel で
  `sprite_buf`
  を見ないようにし、
  8-pixel fast path も
  tile 全体が
  sprite 範囲外なら
  背景だけで確定できるようにした
- 目的は、
  `sp_max`
  が低いまま
  `sp_lines / sp_sum`
  が多い秒で
  `draw_us`
  が伸びる
  1〜2枚/line の常用ケースを
  小さい変更で軽くすること

- `0.0.172`
  では、
  `line_sprite_count <= 2`
  の常用ケース向けに、
  `compositeSprites()`
  が
  2本までの
  sprite span
  を返すようにした
- `PPU_DrawLine()`
  では、
  pixel 単位と
  8-pixel tile 単位の
  overlay 判定に
  その span を使い、
  離れた 2 枚の間にある
  背景 pixel/tile で
  `sprite_buf`
  を見ないようにした
- 3本目以上の sprite がある行では、
  前回の
  `sprite_min_x / sprite_max_x`
  判定へ自然に戻す
  形にしてある

- `0.0.173`
  では、
  `0.0.172`
  の
  2-span 最適化で
  画面/キャラの
  ちらつきが出たため、
  `core/ppu.c`
  を
  `0.0.171`
  の内容へ
  `git`
  から戻した
- 採用状態としては
  `0.0.171`
  の
  `sprite_min_x / sprite_max_x`
  最適化を維持し、
  2-span 専用判定は
  不採用とする

- `0.0.174`
  では、
  `0.0.173`
  の描画経路を保ったまま、
  `compositeSprites()`
  の無駄仕事を少し減らした
- 具体的には、
  sprite palette の
  16色展開を
  「最初に visible sprite が見つかった時」まで遅らせ、
  `R1_CLIP_SP`
  が有効で
  `x=0..7`
  に完全に収まる sprite は
  pattern fetch 前に
  行ごとスキップする
- 目的は、
  ちらつきのような
  描画差分を出さずに、
  `compositeSprites()`
  の固定費を小さく削ること

- `0.0.175`
  では、
  `0.0.174`
  の方向を保ったまま、
  `compositeSprites()`
  の内側 loop を少し整理した
- 具体的には、
  `plane0 | plane1 == 0`
  の完全透明 row を
  丸ごとスキップし、
  pixel loop の
  左右境界
  `px_start / px_end`
  を先に計算して
  毎 pixel の
  範囲判定を減らした
- 目的は、
  描画結果を変えにくいまま
  sprite row ごとの
  固定費をさらに削ること

- `0.0.176`
  では、
  `0.0.175`
  が
  体感でも perf log でも
  効いていないと判断し、
  `core/ppu.c`
  を
  `0.0.174`
  の内容へ
  `git`
  から戻した
- 採用状態としては
  `0.0.174`
  の
  `compositeSprites()`
  固定費削減までを維持し、
  `plane0 | plane1 == 0`
  の row skip と
  `px_start / px_end`
  最適化は
  不採用とする

- `0.0.177`
  では、
  `PPU_DrawLine()`
  の
  8-pixel background fast path に
  「その tile に実際に
  sprite pixel があるか」
  の判定を追加した
- `sprite_min_x / sprite_max_x`
  の範囲内でも、
  その 8-pixel tile に
  `SPR_OPAQUE`
  が 1つも無ければ
  overlay 合成をせず
  背景だけで確定する
- 目的は、
  `sp_max=6〜8`
  の重い秒で、
  sprite 範囲が広いだけで
  不要な tile overlay に入る回数を
  減らすこと

- `0.0.178`
  では、
  sprite 系の重さを
  分離して見るため、
  `[PERF]`
  に
  `spr_comp_us`
  `spr_ovl_us`
  `ovl_tiles`
  を追加した
- `compositeSprites()`
  の時間と、
  `PPU_DrawLine()`
  の
  sprite overlay 分岐へ
  実際に入った時間・tile 数を
  別々に集計する
- 目的は、
  「sprite が重い」
  から一歩進めて、
  展開側と overlay 側の
  どちらが主因かを
  次のログ 1 本で切ること

- `0.0.179`
  では、
  `compositeSprites()`
  の中を
  一発で広く見るため、
  `[PERF]`
  に
  `spr_fetch_us`
  `spr_store_us`
  `spr_skip_rows`
  `spr_write_px`
  を追加した
- あわせて、
  `plane0 | plane1 == 0`
  の完全透明 row は
  pixel loop に入る前に
  skip するようにした
- 目的は、
  `compositeSprites()`
  の重さを
  fetch/setup 側と
  `sprite_buf`
  書き込み側に分け、
  次のログ 1 本で
  どちらを先に詰めるか
  決めること

- `0.0.180`
  では、
  `compositeSprites()`
  を
  `collect`
  と
  `expand`
  の
  二段に分けた
- まず
  scanline にかかる sprite row の
  最小情報だけを
  `rows[8]`
  に集め、
  その後で
  palette 展開と
  `sprite_buf`
  書き込みを
  一度だけ行う形にした
- 目的は、
  走査中の固定費と
  分岐を減らし、
  `compositeSprites()`
  の主因である
  fetch 以外のコストを
  少しでも下げること

- `0.0.181`
  では、
  `compositeSprites()`
  の
  store 側で、
  row ごとの
  `px_start / px_end`
  を先に決めるようにした
- あわせて、
  `hflip`
  あり / なし を
  ループごとに分け、
  1 pixel ごとの
  境界判定と
  `hflip`
  分岐を減らした
- 目的は、
  `spr_store_us`
  を下げつつ、
  `collect/expand`
  二段化で減った
  固定費の上に
  store 側の軽量化を
  上乗せすること

- `0.0.182`
  では、
  `compositeSprites()`
  の
  collect 側で、
  まず `Y`
  だけを読んで
  row 判定を行い、
  scanline 非該当 sprite では
  `tile/attr/x`
  をまだ読まない形にした
- 目的は、
  64 本走査そのものは維持しつつ、
  collect 側の
  OAM 読み出し固定費を
  少しでも減らすこと

- `0.0.183`
  では、
  速度調査を
  いったん終えるため、
  `[PERF]`
  出力を
  通常ビルドでは
  出さないように戻した
- 目的は、
  UART ログを静かにして
  次の音関連調査へ
  進みやすくすること

- `0.0.184`
  では、
  PWM 出力側の
  急な mute / unmute をやめ、
  ring underrun 時は
  `128`
  に戻す代わりに
  直前 sample を保持する形にした
- あわせて、
  audio mix の
  centered 出力に
  小さな gain を追加し、
  BGM 音量を
  少し持ち上げた
- 目的は、
  小さすぎる音量と
  mute 切替由来の
  プチプチノイズを
  同時に減らすこと

- `0.0.185`
  では、
  audio mix を
  単純な線形加算から
  pulse / triangle / noise / DPCM
  の LUT 配分へ切り替えた
- あわせて、
  `0.0.184`
  で追加した
  centered 出力の
  extra gain は外した
- 目的は、
  音割れを抑えつつ
  channel balance を
  崩さずに
  BGM と効果音の
  聞こえ方を
  立て直すこと

- `0.0.186`
  では、
  音関連の
  切り分け用に、
  `AUDIO_MIX`
  ログを追加した
- 内容は、
  ring overrun と
  mix / noise / DPCM の
  peak を
  1 秒ごとに
  出す最小観測
- 目的は、
  出力リング詰まりと
  noise / DPCM の
  聞こえ方の弱さを
  次の一手の前に
  切り分けること

- `0.0.187`
  では、
  `audio_debug_poll()`
  を
  scanline 進行側から
  毎フレーム継続して
  呼ぶようにした
- 目的は、
  `0.0.186`
  で追加した
  `AUDIO`
  / `AUDIO_MIX`
  観測を
  実際の UART ログへ
  出せるようにすること

- `0.0.188`
  では、
  `AUDIO_SAMPLE_RATE`
  を
  `22050`
  から
  `16000`
  へ下げた
- 目的は、
  継続的に出ている
  ring underrun を
  まず減らし、
  ガビガビした歪みの
  主因候補である
  出力供給切れを
  先に弱めること

- `0.0.189`
  では、
  PWM consumer 側に
  小さい
  playback chunk
  を追加した
- 内容は、
  ring から
  1 sample ずつ
  直接引く代わりに、
  最大 `64`
  sample を
  まとめて取り込んでから
  再生する形
- 目的は、
  短い供給ゆらぎで
  ring が
  `0/1`
  に落ちるたびに
  直ちに underrun へ
  入るのを避けること

- `0.0.190`
  では、
  `core/apu.c`
  に
  channel 生成観測を追加した
- 内容は、
  `APU_CH`
  として
  `noise_nz`
  `dpcm_nz`
  `noise_lines`
  `dpcm_lines`
  `dpcm_bytes_max`
  を
  1 秒ごとに
  出す最小集計
- 目的は、
  爆発音不在の原因が
  生成段階か
  出力経路かを
  mix 手前で
  直接切ること

- `0.0.191`
  では、
  `core/apu.c`
  の
  envelope 再始動を
  最小修正した
- 内容は、
  pulse / noise に
  `env_start`
  を追加し、
  `$4003`
  `$4007`
  `$400F`
  で
  restart flag を立て、
  quarter-frame 側で
  `decay=15`
  と
  divider 再装填を
  行う形へ寄せた
- 目的は、
  `noise_lines > 0`
  なのに
  `noise_nz = 0`
  が続く状態を
  まず解消し、
  爆発音不在が
  envelope 再始動欠落に
  起因するかを
  切ること

- `0.0.192`
  では、
  `platform/audio.c`
  の
  audio ring を
  shared count 方式から
  single-producer /
  single-consumer の
  `write/read`
  差分方式へ
  切り替えた
- 内容は、
  `s_ring_count`
  を廃止し、
  producer は
  `next_write != s_ring_read`
  で full 判定、
  consumer は
  `s_ring_read == s_ring_write`
  で empty 判定、
  `audio_ring_available()`
  も
  pointer 差分で
  算出する形へ
  変更した
- 目的は、
  timer IRQ と
  foreground が
  同時更新していた
  shared occupancy を
  なくし、
  偽 underrun /
  二重に聞こえる感じの
  候補を
  まず減らすこと

- `0.0.193`
  では、
  audio / APU の
  1 秒診断ログを
  実行経路から外した
- 内容は、
  `PPU_Cycle()`
  からの
  `audio_debug_poll()`
  呼び出しを止め、
  `core/apu.c`
  の
  `APU_CH`
  出力も
  compile 時に
  無効化した
- 目的は、
  診断で得たい事実は
  もう十分集まったため、
  UART 出力負荷なしの
  純粋な実音で
  残る
  メロディーの
  ビビリ感を
  再確認すること

- `0.0.194`
  では、
  audio sample rate を
  `16000`
  から
  `18000`
  へ
  小さく戻した
- 内容は、
  `platform/audio.h`
  の
  `AUDIO_SAMPLE_RATE`
  だけを
  調整し、
  それ以外の
  mix / ring / PWM
  経路は
  維持した
- 目的は、
  だいぶ改善した後に
  残っている
  音の濁りを
  まず軽くしつつ、
  `22050`
  まで戻す前に
  `underrun`
  の再悪化が
  許容範囲かを
  中間点で
  見ること

- `0.0.195`
  では、
  sprite path 再設計の
  1 段目として
  `scanline sprite cache`
  の土台を
  導入し、
  `PERF`
  計測も
  再び有効化した
- 内容は、
  `core/ppu.c`
  に
  `ScanlineSpriteCache`
  を追加し、
  `compositeSprites()`
  が
  cache を
  build / reuse
  しながら
  expand する形へ
  切り替えた
- 失効条件は、
  `SPRRAM` write
  (`$2004`),
  `$4014` DMA,
  `PPUCTRL`
  の
  `R0_SP_SIZE`
  変更
  を
  明示的に
  反映した
- `PERF`
  には
  `spr_cache_us`,
  `spr_cache_rebuilds`,
  `spr_cache_invalid`
  を追加し、
  cache 導入の
  固定費と
  再生成回数を
  観測できるようにした
- 目的は、
  設計した
  3 ステップの
  第 1 段として、
  まず invalidation と
  collect 再利用の
  正しさを
  固めること

- `0.0.196`
  では、
  3 ステップ案の
  第 2 段として、
  sprite cache の
  build を
  `PPU_DrawLine()`
  の外へ
  出した
- 内容は、
  visible scanline の
  draw 前に
  `PPU_Cycle()`
  から
  `ppu_prepare_sprite_scanline_cache()`
  を呼び、
  `compositeSprites()`
  側は
  cache を
  consume するだけの
  形へ
  寄せた
- safety のため、
  cache が
  想定外に
  invalid な場合だけ
  fallback build を
  残し、
  `spr_cache_miss`
  で
  その回数を
  観測できるようにした
- 目的は、
  step 1 で
  固めた
  invalidation 条件を
  保ったまま、
  draw path から
  collect build を
  切り離すこと

- `0.0.197`
  では、
  3 ステップ案の
  第 3 段として、
  `sprite_buf[256]`
  の
  全面初期化を
  stamp 管理へ
  置き換えた
- 内容は、
  `sprite_buf`
  と別に
  `sprite stamp`
  世代を持ち、
  現 scanline の
  stamp 一致時だけ
  sprite pixel を
  有効とみなす
  形へ
  切り替えた
- これにより
  `memset(sprite_buf, 0, 256)`
  を外しつつ、
  stale byte を
  overlay 側が
  参照しない保証を
  入れた
- `sprite_tile_has_opaque()`
  と
  overlay 側の
  `SPR_OPAQUE`
  判定も
  stamp aware に
  そろえた
- 目的は、
  `sprite_buf`
  全面前提の
  固定費を
  意味論を
  崩さずに
  下げること

- `0.0.198`
  では、
  CPU hot path の
  安全な範囲だけを
  先に詰めた
- 内容は、
  stack push/pop を
  internal RAM 直読みに
  置き換え、
  zero page 読み
  と
  official opcode の
  zero page / zero page,X
  RMW 読みも
  `RAM[]`
  直読みに
  そろえた
- 目的は、
  `K6502_Read()`
  を通さなくてよい
  hot path を
  先に削って、
  CPU 側の
  固定費を
  小さくすること

- `0.0.199`
  では、
  `PERF`
  表示を
  CPU 評価向けに
  絞り直した
- 内容は、
  `cpu.c`
  に
  `prg_fast_reads`
  `stack_ops`
  `zp_reads`
  `zp_ptr_reads`
  `zp_rmw_reads`
  の
  1 秒カウンタを
  追加し、
  `ppu.c`
  の
  `[PERF]`
  も
  それらを
  主表示に
  切り替えた
- 目的は、
  `0.0.198`
  で入れた
  CPU hot path
  最適化の
  通過量を
  直接見ながら、
  次の
  CPU 側改善を
  判断しやすく
  すること

- `0.0.200`
  では、
  CPU hot path の
  safe domain 拡張として、
  zero page 書き込み側を
  直書きに
  寄せた
- 内容は、
  official opcode の
  `STA/STX/STY`
  の
  zero page /
  indexed zero page write と、
  zero page /
  zero page,X
  RMW の
  writeback を
  `K6502_Write()`
  から
  `RAM[]`
  直書きに
  切り替えた
- あわせて
  `cpu_zp_w`
  カウンタを
  `PERF`
  へ追加し、
  書き込み側 hot path の
  通過量も
  観測できるようにした
- 目的は、
  `Step 2`
  の
  safe internal RAM /
  zero page 経路を
  小さい塊で
  広げること

- `0.0.201`
  では、
  `Step 2`
  の継続として、
  unofficial opcode の
  safe zero page 経路も
  同じ方針で
  直読みに
  寄せた
- 内容は、
  `SLO/RLA/SRE/RRA/DCP/ISB`
  の
  zero page /
  zero page,X
  ケースと、
  `SAX`
  の
  zero page /
  zero page,Y
  書き込み、
  `LAX`
  の
  zero page /
  zero page,Y
  読みを
  `K6502_Read/Write()`
  から
  `RAM[]`
  直読書きに
  切り替えた
- 目的は、
  official opcode 側で
  効果が小さくても
  積めた
  safe domain を、
  unofficial opcode の
  zero page 側へも
  同じ条件で
  広げること

- `0.0.202`
  では、
  `Step 2`
  の
  もう 1 回分として、
  unofficial opcode の
  safe zero page 経路を
  追加で
  直読みに寄せた
- 内容は、
  `SLO/RLA/SRE/RRA/DCP/ISB`
  の
  `ZP/ZPX`
  と、
  `SAX`
  の
  `ZP/ZPY`、
  `LAX`
  の
  `ZP/ZPY`
  を
  official 側と同じ
  `read_zp_* / write_zp_ram()`
  にそろえた
- 目的は、
  `Step 2`
  を
  もう一段だけ
  safe domain 内で
  広げてから、
  次に
  `Step 3`
  へ進むかを
  判断すること

- `0.0.203`
  では、
  `Step 3`
  の最初の一手として、
  `read_cpu_data()`
  と
  `K6502_Read()`
  の境界を
  少し広げた
- 内容は、
  `read_cpu_data()`
  に
  internal RAM
  (`$0000-$1FFF`)
  の
  fast path を
  追加し、
  その通過量を
  `cpu_ram_fast`
  として
  `PERF`
  に出すようにした
- 目的は、
  `PRG`
  だけでなく
  internal RAM 読みも
  `K6502_Read()`
  を経由しない
  hot path として
  切り出し、
  `Step 3`
  の
  効果と
  通過量を
  先に
  観測できるように
  すること

- `0.0.204`
  では、
  `0.0.203`
  の
  `read_cpu_data()`
  への
  internal RAM fast path を
  git から
  `0.0.202`
  基準へ戻した
- 事実として、
  `0.0.203`
  のログでは
  `cpu_ram_fast`
  は通っていたが、
  `cpu_hz`
  と
  `cpu_pct`
  は
  悪化した
- そのため、
  `Step 3`
  は
  `read_cpu_data()`
  全体を広げる方向では
  続けず、
  より限定的な
  fetch/helper 再設計へ
  切り替える前提で
  基準を戻した

- `0.0.205`
  では、
  `Step 3`
  を
  `PC`
  fetch/helper
  の
  最小単位へ
  再設計して
  再開した
- 内容は、
  `fetch_pc_byte()`
  で
  `PC >= $8000`
  の
  opcode/immediate fetch を
  `read_prg_rom()`
  直読みにし、
  `EA_ABS()`
  には
  新しい
  `fetch_pc_word()`
  を
  使うようにした
- 目的は、
  `read_cpu_data()`
  全体を
  広げずに、
  命令 fetch と
  absolute operand fetch の
  hot path だけを
  局所的に
  `K6502_Read()`
  から外し、
  `Step 3`
  を
  より安全な
  fetch/helper
  再設計として
  進めること

- `0.0.206`
  では、
  `Step 3`
  の
  narrow helper
  方針を
  継続して、
  `read16()`
  と
  `read16_wrap()`
  の
  `PRG`
  側だけを
  `read_prg_rom()`
  直読みにした
- 内容は、
  `addr >= $8000`
  の
  little-endian word 読みを
  `read16()`
  で
  fast path にし、
  `read16_wrap()`
  でも
  wrap 後の
  high byte address を
  先に計算して、
  両方が
  `PRG`
  領域なら
  直読みにする形へ
  そろえた
- 目的は、
  reset/IRQ vector 読みと
  `JMP (indirect)`
  helper の
  `K6502_Read()`
  依存を
  局所的に
  減らし、
  `read_cpu_data()`
  全体を
  広げないまま
  `Step 3`
  を
  helper 単位で
  前に進めること
- 事実として、
  `pico20260418_073133.log`
  では
  `0.0.205`
  比で
  `cpu_hz`
  平均が
  `1614865 -> 1618309`
  へ、
  `cpu_pct`
  平均が
  `89.71% -> 89.90%`
  へ
  少し良化した
- 一方で
  `cpu_us`
  は
  `476685 -> 480390`
  と
  少し増えており、
  CPU helper
  単独での
  劇的改善ではない
- ただし
  `draw_us`
  `spr_comp_us`
  `spr_cache_us`
  は
  下がっており、
  `spr_cache_miss`
  も
  引き続き
  `0`
  なので、
  `0.0.206`
  は
  narrow
  `Step 3`
  の
  採用候補として
  維持する

- ここまでの
  sprite 高速化を
  まとめると、
  `0.0.180`〜`0.0.182`
  の
  `compositeSprites()`
  collect 側整理は
  効いたが、
  `0.0.195`〜`0.0.197`
  の
  scanline sprite cache /
  draw path 外 build /
  stamp 化は
  正しさ確認には
  成功した一方で、
  重さの本体を
  消し切れてはいない
- 事実として、
  `0.0.206`
  の
  sprite がある秒では
  `spr_comp_us`
  が
  おおむね
  `6k〜14k`
  で、
  `spr_cache_us`
  が
  おおむね
  `29k〜32k`
  前後に
  残っている
- また、
  `spr_cache_miss`
  は
  引き続き
  `0`
  なので、
  cache の
  意味論確認は
  ここで
  いったん
  完了とみなせる
- したがって、
  次の
  sprite 側主戦場は
  cache の
  微調整ではなく、
  `expand / overlay`
  の
  形そのものの
  見直しである

- `0.0.207`
  では、
  次段の
  step 1 として
  `expand / overlay`
  の
  分離計測を
  追加した
- 内容は、
  既存の
  `spr_store_us`
  を
  `spr_expand_us`
  として
  表示し直し、
  `spr_overlay_us`
  と
  `ovl_tiles`
  に加えて
  `ovl_pixels`
  を
  `PERF`
  に出すようにした
- `ovl_pixels`
  は、
  sprite overlay に
  実際に入った
  pixel 数で、
  fine-X/border 側の
  pixel loop と
  8px tile overlay の
  両方を
  合算している
- 目的は、
  cache 以後の
  `expand`
  と
  `overlay`
  の
  どちらが
  次の主戦場かを、
  8px 単位の
  overlay 侵入量まで含めて
  切れるようにすること

- `0.0.208`
  では、
  次段 step 2 の
  最小実装として、
  `sprite_tile_has_opaque()`
  を
  8 回の
  `sprite_pixel_has_opaque()`
  参照から
  tile mask 参照へ
  置き換えた
- 内容は、
  `compositeSprites()`
  で
  `s_sprite_tile_mask[33]`
  を
  並行生成し、
  各 opaque sprite pixel ごとに
  対応 tile の bit を立てる
  だけに留めている
- `sprite_buf/stamp`
  と
  overlay 本体の
  意味論は
  まだ変えていない
- 目的は、
  `sprite_tile_has_opaque()`
  の
  前段 8 回参照を
  低リスクで除き、
  tile-oriented buffer
  への移行を
  小さく始めること

- `0.0.209`
  では、
  `0.0.208`
  の
  tile mask precheck を
  採らず、
  `core/ppu.c`
  を
  git から
  `0.0.207`
  基準へ戻した
- 事実として、
  `pico20260418_075745.log`
  では
  `0.0.207`
  比で
  `cpu_hz`
  平均が
  `1603159 -> 1559265`
  へ、
  `draw_us`
  平均が
  `387489 -> 403184`
  へ
  悪化した
- `spr_comp_us`
  `spr_expand_us`
  `spr_overlay_us`
  `spr_cache_us`
  `ovl_tiles`
  `ovl_pixels`
  も
  いずれも
  上振れしており、
  tile mask による
  precheck だけでは
  効果が見えなかった
- したがって、
  次の主戦場は
  precheck の微調整ではなく、
  overlay 本体の
  直線化か
  `sprite_buf`
  表現そのものの
  見直しへ移す

- `0.0.210`
  では、
  次段の
  low-risk な
  overlay 本体見直しとして、
  fast 8px overlay branch の
  sprite 参照を
  tile 単位で
  いったん
  `opaque_mask`
  にまとめてから
  解く形へ変えた
- 事実として、
  変更は
  `sprite_buf/stamp`
  の
  意味論を
  変えず、
  `sprite_tile_opaque_mask()`
  を追加し、
  `sprite_tile_has_opaque()`
  と
  fast overlay branch の
  両方で
  tile 内 8 pixel の
  opaque 判定を
  一度だけ集約する
  ものに留めている
- 目的は、
  `0.0.208`
  のような
  precheck 単独ではなく、
  overlay 本体側で
  同一 tile への
  `sprite_pixel_has_opaque()`
  反復参照を
  減らせるかを
  小さく確かめること

- `0.0.211`
  では、
  `0.0.210`
  の
  overlay 本体見直しを
  もう一段だけ
  直線化し、
  fast 8px branch で
  `opaque_mask`
  を
  一度だけ計算してから
  `sprite 無し tile`
  と
  `overlay tile`
  を
  分ける形へ寄せた
- 事実として、
  変更は
  以前の
  `!sprite_tile_has_opaque()`
  分岐を外し、
  同じ tile に対する
  `opaque_mask`
  の再計算を
  避けるものに留めている
- 目的は、
  `0.0.210`
  と同じ
  low-risk 方針のまま、
  fast path 内での
  tile 単位判定を
  1 回へまとめること

- `0.0.212`
  では、
  `0.0.211`
  の
  fast overlay branch 整理を
  採らず、
  `core/ppu.c`
  を
  git から
  `0.0.210`
  基準へ戻した
- 事実として、
  ユーザー確認では
  `0.0.211`
  で
  画面に
  横線の
  ちらつきが入った
- したがって、
  今回の
  `opaque_mask`
  再利用による
  fast branch 再編は
  不採用とし、
  次の作業は
  `0.0.210`
  を基準に
  続ける

- `0.0.213`
  では、
  第2世代
  `expand / overlay`
  再設計の
  `Step 1`
  として、
  観測追加だけを
  入れる
- 事実として、
  今回の
  変更対象は
  `core/ppu.c`
  の
  `PERF`
  集計だけで、
  新しく
  `spr_mask_us`
  と
  `spr_resolve_us`
  を
  出す
- `spr_mask_us`
  は
  fast 8px branch での
  `sprite_tile_has_opaque()`
  前段判定に
  使った時間、
  `spr_resolve_us`
  は
  実際に
  sprite overlay を
  解いた部分の
  時間を
  それぞれ
  1 秒集計する
- 目的は、
  第2世代の
  主戦場である
  `expand / overlay`
  のうち、
  `tile precheck`
  と
  `overlay resolve`
  の
  どちらが
  本当に重いかを
  もう一段
  正確に切ること
- 事実として、
  今回は
  表示結果や
  sprite 意味論を
  変える変更は
  入れない

- `0.0.214`
  では、
  第2世代
  `expand / overlay`
  再設計の
  `Step 2`
  として、
  8px 中間表現の
  並行生成だけを
  入れる
- 事実として、
  今回は
  `sprite_buf/stamp`
  の
  旧経路を
  残したまま、
  `sprite_tile_mask[33]`
 、
  `sprite_tile_behind_mask[33]`
 、
  `sprite_tile_color[33][8]`
  を
  scanline ごとに
  並行生成する
- 生成位置は
  `compositeSprites()`
  の
  既存 pixel 書き込みと
  同じ条件の中に
  置き、
  旧 `sprite_buf`
  に
  書かれた pixel と
  同じ front-most sprite
  情報だけを
  tile-oriented 表現へ
  反映する
- 目的は、
  次の
  `overlay`
  差し替え段階へ向けて、
  旧表示結果を
  変えずに
  `mask / behind / color`
  を
  そろえること
- 事実として、
  今回は
  render 側の
  参照先は
  まだ切り替えず、
  観測と
  正しさ確認の
  土台だけを
  足す

- `0.0.215`
  では、
  第2世代
  `expand / overlay`
  再設計の
  `Step 3`
  として、
  fast 8px overlay branch だけを
  新しい
  tile-oriented 表現へ
  切り替える
- 事実として、
  今回は
  `sprite_buf/stamp`
  の
  旧経路を
  残したまま、
  fast 8px branch では
  `s_sprite_tile_mask`
 、
  `s_sprite_tile_behind_mask`
 、
  `s_sprite_tile_color`
  を
  参照して
  `opaque`
 、
  `behind`
 、
  色解決を
  行う
- 旧 `sprite_buf`
  参照は
  fallback pixel branch と
  そのほかの
  非 fast 経路に
  まだ残す
- 目的は、
  旧経路を
  温存したまま
  fast 8px overlay の
  main resolve を
  直線化し、
  `spr_overlay_us`
  と
  `spr_resolve_us`
  が
  下がるかを
  まず確かめること
- 事実として、
  今回は
  `sprite 0 hit`
  や
  fallback pixel branch の
  判定は
  まだ分離しない

- `0.0.216`
  では、
  `0.0.215`
  の
  fast 8px overlay branch 切替を
  不採用として、
  `core/ppu.c`
  を
  git から
  `0.0.214`
  基準へ戻す
- 事実として、
  ユーザー確認では
  `0.0.215`
  で
  Lode Runner の
  キャラクタが
  ちらついた
- したがって、
  tile-oriented 表現を
  fast overlay branch へ
  直接入れる
  今回の first cut は
  採らない
- 次の作業は、
  `0.0.214`
  を基準に
  もっと小さい単位で
  `expand / overlay`
  再設計を
  詰め直す
- `0.0.228`
  では、
  表示結果と
  `R2_SP_HIT`
  の
  現行意味論を
  変えずに、
  旧
  `spr0_hit_x`
  判定と
  新しい
  `s_sprite0_tile_mask`
  の
  一致を
  `PERF`
  で
  観測する
- 事実として、
  今回は
  `PPU_DrawLine()`
  で
  - `spr0_match`
  - `spr0_mismatch`
  - `spr0_old_only`
  - `spr0_new_only`
  を
  1 秒集計で
  出す
- 目的は、
  `sprite 0 hit`
  を
  main resolve から
  切り離す前に、
  現行の
  `spr0_hit_x >= 0`
  と
  tile-oriented
  補助情報が
  同じ line を
  指しているかを
  固定すること
- `0.0.229`
  では、
  `0.0.228`
  の
  比較で
  `spr0_match`
  が
  完全一致だったことを
  前提に、
  `PPU_DrawLine()`
  の
  `R2_SP_HIT`
  入口条件だけを
  `spr0_hit_x >= 0`
  から
  `spr0_tile_hit`
  へ
  切り替える
- 事実として、
  今回は
  `sprite 0 hit`
  の
  最小責務切替であり、
  `compositeSprites()`
  の
  `hit_x`
  生成と
  比較観測は
  まだ残す
- 目的は、
  `sprite 0 hit`
  の
  line 判定を
  main resolve から
  tile-oriented
  補助情報へ
  1 段だけ
  寄せ、
  見た目と
  `PERF`
  を
  崩さずに
  次段へ
  進めること
- `0.0.230`
  では、
  `0.0.229`
  の
  `sprite 0 hit`
  最小責務切替が
  通ったことを
  前提に、
  旧
  `spr0_hit_x`
  比較経路を
  外して
  `s_sprite0_tile_mask`
  ベースへ
  整理する
- 事実として、
  今回は
  `compositeSprites()`
  の
  `hit_x`
  追跡と、
  `PPU_DrawLine()`
  の
  `spr0_match`
  系比較観測を
  削除し、
  `R2_SP_HIT`
  の
  line 判定は
  `spr0_tile_hit`
  のみを
  使う
- 目的は、
  `sprite 0 hit`
  を
  main resolve から
  さらに切り離し、
  旧経路の
  余分な比較と
  追跡コストを
  減らすこと

- `0.0.231`
  では、
  `0.0.230`
  を
  不採用として、
  `0.0.229`
  相当へ
  戻す
- 事実として、
  `pico20260418_122345.log`
  では
  `draw_us`
  と
  sprite 総コストが
  `0.0.229`
  より
  悪化した
- 今回の戻しは、
  `sprite 0 hit`
  の
  `spr0_hit_x`
  比較経路と
  `spr0_match`
  系観測を
  復元し、
  `0.0.229`
  の
  安全な
  比較込み状態へ
  戻す
- 目的は、
  `sprite 0 hit`
  整理を
  いったん
  打ち切り、
  採用版を
  `0.0.229`
  系へ
  固定すること

## 0.49 `0.1.40` boot 時に newlib の trim 閾値を `0x200` へ下げる実験を追加する (2026-04-19)

- system version を
  `0.1.40`
  へ更新した
- `platform/main.c`
  では、
  banner 出力直後に
  `__malloc_trim_threshold`
  を
  `0x200`
  へ上書きするようにした
- 同じ箇所で、
  現在の
  `trim_threshold`
  と
  `top_pad`
  を
  boot ログへ
  出すようにした
- 目的は、
  これまで
  実体確認できた
  `0x20000`
  の
  trim 閾値を
  強く下げることで、
  `free()`
  後の
  arena 残留と
  `Map30`
  /
  `Mapper 0`
  の
  順序依存が
  どう変わるかを
  実機で確認すること

## 0.50 `0.1.41` mapper 動的確保設計の Phase 1 として release dispatcher を追加する (2026-04-19)

- system version を
  `0.1.41`
  へ更新した
- `InfoNES_Mapper.h`
  に
  `InfoNES_Mapper_ReleaseCurrent()`
  を追加した
- `InfoNES_Mapper.cpp`
  では
  `MapperNo`
  ベースの
  `switch`
  で
  current mapper の
  release を呼ぶ
  dispatcher を追加した
- 現時点では
  `Map30`
  だけが実体 release を持つので、
  `case 30`
  で
  `Map30_Release()`
  を呼び、
  `Map6`
  `Map19`
  `Map185`
  `Map188`
  `Map235`
  は
  no-op
  とした
- `platform/rom_image.c`
  の
  `InfoNES_ReleaseRom()`
  は、
  `Map30_Release()`
  直呼びをやめて
  `InfoNES_Mapper_ReleaseCurrent()`
  だけを呼ぶようにした
- 目的は、
  mapper ごとの
  release 呼び出し箇所を
  1 箇所へ集約し、
  次段の
  `Map185`
  `Map188`
  `Map19`
  `Map6`
  `Map235`
  動的化へ
  安全に進めるための
  足場を先に作ること
- ユーザー実機確認として、
  `DART`
  と
  `TOWAR`
  は
  起動し、
  どちらからも
  ROM menu
  へ戻れた
- 同じ build で、
  `DragonQuest`
  と
  `LodeRunner`
  も
  起動確認が通った

## 0.51 `0.1.42` mapper 動的化の最初の対象として `Map185` を `new[]/delete[]` 化する (2026-04-19)

- system version を
  `0.1.42`
  へ更新した
- `Map185`
  について、
  `InfoNES_Mapper.cpp`
  側の
  global symbol
  `Map185_Dummy_Chr_Rom`
  は残したまま、
  実体確保だけを
  `Map185_Init()`
  /
  `Map185_Release()`
  へ寄せた
- `Map185_Init()`
  では
  先頭で
  `Map185_Release()`
  を呼び、
  `new (std::nothrow) BYTE[0x0400]`
  を試すようにした
- 確保失敗時は
  `InfoNES_Error("Mapper 185 startup alloc failed [1/1 dummy-chr size=1024]")`
  を出し、
  `Map185_Release()`
  で
  partial init を巻き戻して
  return する
- `InfoNES_Mapper_ReleaseCurrent()`
  では
  `case 185`
  を追加し、
  共通 release dispatcher
  から
  `Map185_Release()`
  を呼ぶようにした
- build は成功した
- 実機確認はまだ取っていないので、
  次は
  `Map185`
  該当 ROM
  がある場合に
  起動 /
  戻りを確認する

## 0.52 `0.1.43` `Map188` と `Map19` を `new[]/delete[]` 化して release dispatcher へつなぐ (2026-04-19)

- system version を
  `0.1.43`
  へ更新した
- `Map188`
  について、
  `Map188_Dummy`
  の
  global symbol
  は残したまま、
  実体確保だけを
  `Map188_Init()`
  /
  `Map188_Release()`
  へ寄せた
- `Map188_Init()`
  では
  先頭で
  `Map188_Release()`
  を呼び、
  `new (std::nothrow) BYTE[0x2000]`
  を試すようにした
- 確保失敗時は
  `InfoNES_Error("Mapper 188 startup alloc failed [1/1 dummy size=%u]", 0x2000)`
  を出し、
  `Map188_Release()`
  で
  partial init を巻き戻して
  return する
- `Map19`
  についても、
  `Map19_Chr_Ram`
  の
  global symbol
  は残したまま、
  実体確保だけを
  `Map19_Init()`
  /
  `Map19_Release()`
  へ寄せた
- `Map19_Init()`
  では
  先頭で
  `Map19_Release()`
  を呼び、
  `new (std::nothrow) BYTE[0x2000]`
  を試すようにした
- 確保失敗時は
  `InfoNES_Error("Mapper 19 startup alloc failed [1/1 chr-ram size=%u]", 0x2000)`
  を出し、
  `Map19_Release()`
  で
  partial init を巻き戻して
  return する
- `InfoNES_Mapper_ReleaseCurrent()`
  では
  `case 188`
  と
  `case 19`
  を追加し、
  共通 release dispatcher
  から
  `Map188_Release()`
  /
  `Map19_Release()`
  を呼ぶようにした
- `InfoNES_Mapper.cpp`
  へ
  `<new>`
  を追加し、
  mapper 個別
  `.cpp`
  を
  `#include`
  している
  翻訳単位全体で
  `std::nothrow`
  を使えるようにした
- build は成功した
- `arm-none-eabi-size`
  では
  `text=260296`
  `data=0`
  `bss=134000`
  だった
- 生成物文字列から確認した
  Build ID banner は
  `PicoCalc NESco Ver. 0.1.43`
  だった
- 実機確認はまだ取っていないので、
  次は
  `Map188`
  /
  `Map19`
  該当 ROM
  がある場合に
  起動 /
  戻りを確認する

## 0.53 `0.1.44` `Map6` を `new[]/delete[]` 化して小さい mapper 群の build 移行をそろえる (2026-04-19)

- system version を
  `0.1.44`
  へ更新した
- `Map6`
  について、
  `Map6_Chr_Ram`
  の
  global symbol
  は残したまま、
  実体確保だけを
  `Map6_Init()`
  /
  `Map6_Release()`
  へ寄せた
- `Map6_Init()`
  では
  先頭で
  `Map6_Release()`
  を呼び、
  `new (std::nothrow) BYTE[0x8000]`
  を試すようにした
- 確保失敗時は
  `InfoNES_Error("Mapper 6 startup alloc failed [1/1 chr-ram size=%u]", 0x8000)`
  を出し、
  `Map6_Release()`
  で
  partial init を巻き戻して
  return する
- `InfoNES_Mapper_ReleaseCurrent()`
  では
  `case 6`
  を追加し、
  共通 release dispatcher
  から
  `Map6_Release()`
  を呼ぶようにした
- build は成功した
- `arm-none-eabi-size`
  では
  `text=260456`
  `data=0`
  `bss=134004`
  だった
- 生成物文字列から確認した
  Build ID banner は
  `PicoCalc NESco Ver. 0.1.44`
  だった
- 実機確認はまだ取っていないので、
  次は
  `Map6`
  該当 ROM
  がある場合に
  起動 /
  戻りを確認する

## 0.54 `0.1.45` `Map235` を現コード準拠の `0x2000` bytes `new[]/delete[]` へ移す (2026-04-19)

- system version を
  `0.1.45`
  へ更新した
- `InfoNES_Mapper.cpp`
  の
  shared union
  から
  `dram`
  領域を外し、
  global symbol
  `DRAM`
  は
  `nullptr`
  初期化へ切り替えた
- `Map235`
  については、
  現コードで確認できる
  `DRAM`
  参照が
  `0x2000`
  bytes
  初期化と
  `ROMBANK0..3 = DRAM`
  だけだったため、
  `Map235_Init()`
  /
  `Map235_Release()`
  の中で
  `new (std::nothrow) BYTE[DRAM_SIZE]`
  /
  `delete[]`
  を行う形へ寄せた
- `DRAM_SIZE`
  は
  `0x2000`
  へ整理した
- 確保失敗時は
  `InfoNES_Error("Mapper 235 startup alloc failed [1/1 dram size=%u]", DRAM_SIZE)`
  を出し、
  `Map235_Release()`
  で
  partial init を巻き戻して
  return する
- `InfoNES_Mapper_ReleaseCurrent()`
  では
  `case 235`
  を
  no-op
  ではなく
  `Map235_Release()`
  呼び出しへ差し替えた
- build は成功した
- `arm-none-eabi-size`
  では
  `text=260624`
  `data=0`
  `bss=125820`
  だった
- 生成物文字列から確認した
  Build ID banner は
  `PicoCalc NESco Ver. 0.1.45`
  だった
- 実機確認はまだ取っていないので、
  次は
  `Map235`
  該当 ROM
  がある場合に
  起動 /
  戻りを確認する
- 実機ログ
  `pico20260420_001803.log`
  では、
  対象 mapper
  直撃 ROM は
  手元にないものの、
  `LodeRunner.nes`
  `BokosukaWars.nes`
  `DragonQuest.nes`
  は
  起動して
  menu 戻りまで進んでおり、
  少なくとも
  `0.1.45`
  で
  現状を壊していないことは確認した

## 0.55 `0.1.46` `Map6` / `Map19` / `Map185` / `Map188` の shared union alias を撤去する (2026-04-20)

- system version を
  `0.1.46`
  へ更新した
- `InfoNES_Mapper.cpp`
  から
  `g_MapperSharedRam`
  を削除し、
  `Map6_Chr_Ram`
  `Map19_Chr_Ram`
  `Map185_Dummy_Chr_Rom`
  `Map188_Dummy`
  の
  global pointer は
  `nullptr`
  初期化へ切り替えた
- これにより
  `Map6`
  `Map19`
  `Map185`
  `Map188`
  は、
  それぞれの
  `Init()`
  /
  `Release()`
  だけが
  実体メモリの確保 /
  解放を持つ形になった
- build は成功した
- `arm-none-eabi-size`
  では
  `text=260608`
  `data=0`
  `bss=93068`
  だった
- `nm`
  では
  `g_MapperSharedRam`
  は
  最終 ELF
  から消えていた
- 次は、
  対象 mapper
  直撃 ROM
  があるものから
  実機で
  起動 /
  menu 戻り /
  再起動
  を確認する

## 0.56 `0.1.47` ROM 選択 menu の help 画面を復旧する (2026-04-20)

- system version を
  `0.1.47`
  へ更新した
- `platform/rom_menu.c`
  に
  help overlay
  表示を追加した
- ROM menu
  では
  `H`
  /
  `?`
  /
  `F1`
  で
  help を開閉し、
  `ESC`
  /
  `ENTER`
  /
  `X`
  でも
  閉じられるようにした
- help 画面には
  ROM menu
  操作と、
  in-game
  の
  `ESC`
  /
  `F1/F5`
  /
  `` ` ``
  /
  `-`
  /
  `[`
  /
  `]`
  の案内を載せた
- footer も
  `H/F1 HELP`
  を出す形に更新した
- build は成功した

## 0.57 `0.1.48` ROM menu help から `F1` を外して game reset と競合しないようにする (2026-04-20)

- system version を
  `0.1.48`
  へ更新した
- ROM menu help の開閉キーから
  `F1`
  を外し、
  `H`
  /
  `?`
  だけで開閉するようにした
- footer と
  help overlay
  の案内文も
  `H/?`
  表記へ合わせて更新した

## 0.58 `0.1.49` ROM menu で keyboard battery register 読み出しテストを追加する (2026-04-20)

- system version を
  `0.1.49`
  へ更新した
- `drivers/i2c_kbd.c`
  に
  `i2c_kbd_read_battery()`
  を追加し、
  keyboard controller
  の
  battery register
  `0x0B`
  を
  1 byte
  読めるようにした
- `platform/rom_menu.c`
  では
  ROM menu
  中に
  `B`
  /
  `b`
  を押すと
  battery register
  を読み、
  raw 値、
  `bit7`
  の charging flag、
  `0x7F`
  mask の percent
  を
  UART log
  へ出すテストを追加した

## 0.59 `0.1.50` ROM menu の battery テスト結果を画面上でも見えるようにする (2026-04-20)

- system version を
  `0.1.50`
  へ更新した
- `platform/rom_menu.c`
  では
  `B`
  /
  `b`
  押下時に、
  battery register
  の結果を
  UART log
  へ出すだけでなく
  menu 上部の
  status 行にも
  `BAT xx% CHG`
  /
  `BAT xx% IDLE`
  /
  `BAT READ FAILED`
  として表示するようにした
- これで
  USB serial
  が使えない条件でも
  画面上で
  battery 読み結果を確認できる

## 0.60 `0.1.51` battery register 読み出しを `pico_multi_booter` の既存実装に合わせる (2026-04-20)

- system version を
  `0.1.51`
  へ更新した
- `PicoCalc/Code/pico_multi_booter/sd_boot/i2ckbd/i2ckbd.c`
  の
  `read_battery()`
  実装では、
  battery register
  `0x0B`
  を
  2 byte
  読み、
  上位 byte
  を
  battery payload
  として扱っていた
- `drivers/i2c_kbd.c`
  の
  `i2c_kbd_read_battery()`
  も、
  1 byte
  読みではなく
  2 byte
  読んで
  `raw[1]`
  を返す形へ合わせた
- これで、
  `0.1.49`
  ログで見えていた
  `raw=0x0B`
  が、
  register 番号側を見てしまっている可能性を避ける

## 0.61 `0.1.52` ROM menu の title 行を大きくし、右上へ battery 残量を常時表示する (2026-04-20)

- system version を
  `0.1.52`
  へ更新した
- `platform/rom_menu.c`
  では、
  title bar
  を
  20px
  から
  28px
  へ広げ、
  既存
  PixelMplus
  を
  `2x`
  scale
  で描いて
  banner
  を
  1 段大きく表示するようにした
- 同じ
  title bar
  の右上には、
  `i2c_kbd_read_battery()`
  で読んだ値から
  `xx%`
  または
  `xx%+`
  を描き、
  battery 残量を常時見えるようにした
- `pico_multi_booter`
  の
  battery 読み出し実装と整合するように、
  charging 時は
  `+`
  を付ける簡易表記にした

## 0.62 `0.1.53` 起動直後に opening screen を追加し、ROM menu の title は元サイズへ戻す (2026-04-20)

- system version を
  `0.1.53`
  へ更新した
- `platform/display.c`
  /
  `platform/display.h`
  に
  `display_show_opening_screen()`
  を追加し、
  fullscreen で
  `PicoCalc NESco`
  /
  `Ver. x.x.xx`
  /
  `Loading...`
  を表示する
  opening screen
  を描けるようにした
- `platform/main.c`
  では
  `display_init()`
  の直後に
  opening screen
  を表示し、
  約
  `900ms`
  待ってから
  input /
  ROM 初期化へ進むようにした
- `platform/rom_menu.c`
  の title bar
  は、
  `0.1.52`
  の
  `2x`
  banner
  をやめて
  元のサイズへ戻した
- battery 残量の右上表示は維持し、
  title 行の密度だけを元の見た目に戻した

## 0.63 `0.1.54` opening screen を 3 秒へ延長し、任意キーで即スキップできるようにする (2026-04-20)

- system version を
  `0.1.54`
  へ更新した
- `platform/main.c`
  では
  `input_init()`
  を
  opening screen
  の前へ移し、
  keyboard
  を読める状態で
  opening
  を表示するようにした
- opening 表示中は
  最大
  `3000ms`
  待つが、
  `i2c_kbd_read_key()`
  で
  何らかのキー入力を受けたら
  即座に
  ROM 選択画面へ進むようにした

## 0.64 `0.1.55` opening screen から `Loading...` を外し、ROM 読み込み直前だけ loading 画面を出す (2026-04-20)

- system version を
  `0.1.55`
  へ更新した
- `platform/display.c`
  /
  `platform/display.h`
  では
  `display_show_loading_screen()`
  を追加し、
  ROM 読み込み中に
  fullscreen で
  `Loading...`
  を表示できるようにした
- `display_show_opening_screen()`
  からは
  `Loading...`
  表示を外し、
  opening には
  title と
  version
  だけを残した
- `platform/rom_menu.c`
  では
  ROM 起動確定時に
  `display_show_loading_screen()`
  を呼んでから
  menu を閉じるようにした

## 0.65 `0.1.56` ROM menu help を多画面化し、version / license summary を追加する (2026-04-20)

- system version を
  `0.1.56`
  へ更新した
- `platform/rom_menu.c`
  の help overlay は
  1 画面固定ではなく、
  `HELP`
  →
  `VERSION`
  →
  `LICENSE 1/2`
  →
  `LICENSE 2/2`
  を
  `LEFT/RIGHT`
  で循環する形へ変えた
- `VERSION`
  画面では、
  opening screen
  と同じ
  `PicoCalc NESco`
  /
  `Ver. x.x.xx`
  系の表示を
  menu overlay
  内で見られるようにした
- `LICENSE`
  画面では、
  tree 内の確認結果に合わせて
  `infones = GPL v2`
  `fatfs = FatFs open-source / BSD-style`
  `font = M+ / PixelMplus free software`
  `project root / platform / drivers = MIT`
  と、
  `core`
  については
  source header 上は
  `MIT`
  だが
  project note 上は
  status 未統一であることを
  summary 表示するようにした

## 0.66 `0.1.57` Loading 画面を実際の ROM 読み込み区間へ移す (2026-04-20)

- system version を
  `0.1.57`
  へ更新した
- `platform/rom_menu.c`
  では
  file 選択確定時の
  `display_show_loading_screen()`
  呼び出しを外した
- 代わりに
  `platform/infones_session.cpp`
  の
  `InfoNES_Menu()`
  で、
  `selected_path`
  確認後、
  `InfoNES_Load(selected_path)`
  の直前に
  `display_show_loading_screen()`
  を呼ぶようにした
- これで
  ROM menu
  を閉じた直後に
  loading が消えるのではなく、
  実際の ROM 読み込み区間で
  `Loading...`
  が出続けるようにした

## 0.67 `0.1.58` ROM menu header に battery icon を追加する (2026-04-20)

- system version を
  `0.1.58`
  へ更新した
- `platform/rom_menu.c`
  の
  `menu_draw_battery_header()`
  へ、
  `%`
  文字列の左に出す
  小さい矩形 battery icon
  描画を追加した
- icon は
  外枠
  /
  端子
  /
  残量 fill
  の簡易形で、
  読み出した
  battery percent
  に応じて内部 fill 幅を変える
- charging 時は
  既存の
  `%+`
  表記に加えて、
  icon 内にも簡易 mark を重ねて
  状態差が見えるようにした

## 0.68 `0.1.59` Loading 画面後に NES viewport モードへ戻し、game 表示位置を復旧する (2026-04-20)

- system version を
  `0.1.59`
  へ更新した
- `platform/infones_session.cpp`
  の
  `InfoNES_Menu()`
  では、
  `display_show_loading_screen()`
  の直後に
  `display_set_mode(DISPLAY_MODE_NES_VIEW)`
  を呼ぶようにした
- これで
  loading 画面を fullscreen で出したあとでも、
  実際の game 描画は
  `32,24`
  起点の
  NES viewport
  に戻るようにした

## 0.69 `0.1.60` root LICENSE を複数ライセンス構成の overview へ更新する (2026-04-20)

- system version を
  `0.1.60`
  へ更新した
- root の
  `LICENSE`
  は、
  これまでの
  MIT 本文のみ
  ではなく、
  current tree
  の実態に合わせた
  multi-license overview
  へ更新した
- `infones`
  `fatfs`
  `font`
  `core`
  `platform / drivers`
  それぞれの扱いを
  tree 内の確認結果ベースで
  明記し、
  project-owned 部分向けの
  MIT 本文は
  そのまま残した

## 0.70 `0.1.61` SRAM save/restore の初段を追加する (2026-04-20)

- system version を
  `0.1.61`
  へ更新した
- `platform/sram_store.cpp`
  と
  `platform/sram_store.h`
  を追加し、
  `SRAMwritten`
  を dirty 判定に使う
  raw
  `*.srm`
  save/restore
  の初段を実装した
- `InfoNES_Load()`
  では
  `InfoNES_ReleaseRom()`
  のあとに
  `sram_store_begin_rom()`
  を呼び、
  `InfoNES_Reset()`
  成功後に
  `sram_store_restore_for_current_rom()`
  を呼ぶようにした
- `platform/rom_image.c`
  の
  `InfoNES_ReleaseRom()`
  冒頭では
  `sram_store_flush_current_rom()`
  を呼ぶようにし、
  ROM 解放後は
  `sram_store_clear_session()`
  で
  session 状態を消すようにした
- save path は
  `sd:/`
  ROM では
  ROM 隣の
  `*.srm`
  を使い、
  `flash:/`
  ROM では
  `0:/saves/*.srm`
  を使う
- `fatfs/ffconf.h`
  の
  `FF_FS_READONLY`
  は
  `0`
  へ下げ、
  save 書き込みに必要な
  `f_write()`
  と
  `f_mkdir()`
  がリンクされるようにした

## 0.71 `0.1.62` flash staged ROM の save path を元の `sd:/` 読み込み元へ揃える (2026-04-20)

- system version を
  `0.1.62`
  へ更新した
- `platform/rom_image.c`
  の
  flash metadata
  へ
  `source_path`
  を追加し、
  staged ROM
  書き込み時に
  元の
  `sd:/`
  path
  を保存するようにした
- `rom_image_get_flash_source_path()`
  を追加し、
  version 2
  metadata
  のときは
  元の
  `sd:/`
  path
  を platform 側から参照できるようにした
- `platform/sram_store.cpp`
  では
  `flash:/`
  起動時に
  上記 metadata
  の
  `source_path`
  を save path
  解決の基準に使うようにした
- metadata
  更新前の
  旧 staged ROM
  については、
  `source_path`
  がないため
  fallback として
  `0:/saves/*.srm`
  を使う

## 0.72 `0.1.63` ROM menu detail に save file の有無を表示する (2026-04-20)

- system version を
  `0.1.63`
  へ更新した
- `platform/sram_store.cpp`
  へ
  `sram_store_has_save_for_rom()`
  を追加し、
  指定 ROM path
  に対応する
  `*.srm`
  が存在するかを
  `f_stat()`
  で判定できるようにした
- `platform/rom_image.c`
  では
  menu entry
  detail
  生成時に
  save file
  が見つかった ROM
  へ
  ` SAVE`
  を付けるようにした
- flash built-in entry
  についても
  `FLASH RESIDENT`
  `FLASH STAGED`
  `Mxx FLASH ...`
  の各 detail
  へ
  同じ
  ` SAVE`
  表示を付けるようにした

## 0.73 `0.1.65` ROM menu から SD 直下への write test を追加する (2026-04-21)

- system version を
  `0.1.65`
  へ更新した
- `platform/rom_menu.c`
  へ
  `menu_run_write_test()`
  を追加し、
  `0:/nes/__write_test.tmp`
  へ
  小さい test file
  を
  `FA_CREATE_ALWAYS | FA_WRITE`
  で書く経路を入れた
- ROM menu
  では
  `W`
  キーで
  上記 write test
  を呼び、
  成否を
  status 行
  と
  UART log
  の両方へ出すようにした
- 目的は、
  `SRAM`
  flush 失敗時に
  save path
  や
  extension
  ではなく、
  `FatFs`
  の低層 write
  自体が通るかを
  最小条件で切り分けること

## 0.74 `0.1.66` SD write 経路を `nes1` の成功実装ベースでつなぐ (2026-04-21)

- system version を
  `0.1.66`
  へ更新した
- `drivers/sdcard.h`
  に
  `sdcard_write_sectors()`
  を追加した
- `drivers/sdcard.c`
  では
  `CMD24`
  を使う
  single-block write
  ループを追加し、
  `sdcard_write_sectors()`
  から
  `512`
  byte sector
  を順に書けるようにした
- `drivers/fatfs_diskio.c`
  の
  `disk_write()`
  は
  これまでの
  常時
  `RES_WRPRT`
  返しをやめて、
  `sdcard_write_sectors()`
  へ接続した
- 目的は、
  `nes1/Picocalc_InfoNES`
  で成功している
  SD write
  の系統へ寄せて、
  `FatFs`
  の通常 file write
  と
  `SRAM`
  flush
  の両方を
  通すこと

## 0.75 `0.1.67` 一時的な ROM menu write test を削除する (2026-04-21)

- system version を
  `0.1.67`
  へ更新した
- `platform/rom_menu.c`
  から
  `W`
  キーの
  `write test`
  分岐と
  `0:/nes/__write_test.tmp`
  への
  一時書き込み helper
  を削除した
- footer の
  `W TEST`
  表示も元に戻した
- `0.1.66`
  の実機ログで
  通常 file write
  と
  `SRAM`
  flush / restore
  が通ったため、
  切り分け用の一時 UI
  は役目を終えたものとして外した

## 0.76 `0.1.68` Mapper 30 の PRG flash overlay を別 persist へ追加する (2026-04-21)

- system version を
  `0.1.68`
  へ更新した
- `infones/mapper/InfoNES_Mapper_030.cpp`
  に
  overlay dirty flag と
  overlay slot の
  読み出し /
  復元 API を追加した
- `Map30_FlashWriteByte()`
  と
  `Map30_FlashEraseSector()`
  で
  overlay 更新時に
  dirty を立てるようにした
- `platform/sram_store.cpp`
  では
  generic
  `*.srm`
  に加えて、
  `Mapper 30`
  かつ
  flashable ROM
  のときだけ
  `*.m30`
  を
  restore / flush
  するようにした
- `*.m30`
  には
  overlay slot
  2 本ぶんの
  used / bank / 16KB data
  を固定長で保存し、
  restore 後に
  現在 latch へ
  再適用する
  初段とした

## 0.77 `0.1.69` 特殊 `MapperSram` の保存対象を点検し、generic `*.srm` 判定を `ROM_SRAM` へ戻す (2026-04-21)

- system version を
  `0.1.69`
  へ更新した
- `Map34`
  `Map41`
  `Map45`
  `Map46`
  `Map47`
  `Map49`
  `Map51`
  `Map80`
  `Map82`
  `Map86`
  `Map87`
  `Map91`
  `Map93`
  `Map114`
  `Map115`
  `Map122`
  `Map140`
  `Map193`
  `Map246`
  `Map248`
  `Map251`
  の
  `Map*_Sram()`
  を点検した
- 現コード上で確認できた範囲では、
  これらの多くは
  `$6000-$7fff`
  を
  mapper 制御レジスタとして使っているだけで、
  `Mapper 30`
  のような
  別 persist
  を要する追加データ領域は見つからなかった
- `Map246`
  だけは
  一部アドレスで
  `SRAMBANK[wAddr&0x1FFF] = byData`
  を持つが、
  generic
  `$6000-$7fff`
  書き込み経路の
  `SRAM[]`
  と同じ窓へ入れているだけで、
  追加の別 persist
  は入れていない
- この点検結果を踏まえ、
  `platform/sram_store.cpp`
  の
  generic
  `*.srm`
  対象判定は
  `ROM_SRAM || SRAMBANK == SRAM`
  ではなく
  `ROM_SRAM`
  のみへ戻した
- 目的は、
  `$6000-$7fff`
  を制御レジスタとして使う mapper で、
  実 save ではない書き込みまで
  `*.srm`
  dirty 扱いにしないようにすること

## 0.78 `0.1.71` PPU -> LCD Phase B として Pico 側 palette を RGB565 実体へ寄せる (2026-04-21)

- system version を
  `0.1.71`
  へ更新した
- `infones/InfoNES_System.h`
  の
  `NesPalette`
  宣言を
  `const`
  なしへ変更し、
  Pico 側の active path で
  runtime 初期化される palette 実体を許可した
- `platform/display.c`
  では
  旧 RGB444-like 定数表を
  `s_nes_palette_rgb444`
  として保持しつつ、
  `display_init()`
  で
  `NesPalette[64]`
  を
  RGB565
  へ一括変換して構築するようにした
- これにより
  `K6502_rw.h`
  から
  `PalTable`
  へ入る色も
  RGB565
  になり、
  `InfoNES_PostDrawLine()`
  は
  1 pixel ごとの
  palette 変換を行わず、
  line buffer
  をそのまま
  LCD byte buffer
  へ pack するだけの経路になった
- 今回は
  以前破綻した
  `WORD`
  直書き staging
  には戻さず、
  byte pack
  自体は従来の安全な形を維持した

## 0.79 `0.1.72` `NesPalette` 契約は元に戻し、`PalTable` 局所変換へ絞り直す (2026-04-21)

- system version を
  `0.1.72`
  へ更新した
- `0.1.71`
  では
  `NesPalette`
  自体を
  RGB565
  実体へ寄せたが、
  実機で
  一部背景破綻が残った
- そこで
  `platform/display.c`
  と
  `InfoNES_System.h`
  /
  `platform/display.h`
  の
  `NesPalette`
  契約は
  元の
  RGB444-like
  source data
  へ戻した
- その代わり
  `infones/K6502_rw.h`
  に
  `InfoNES_Palette444ToRgb565()`
  を追加し、
  `PPU`
  palette write
  の時点で
  `PalTable`
  へ入る値だけを
  RGB565
  化する形へ絞り直した
- これにより
  `InfoNES_PostDrawLine()`
  は引き続き
  line buffer
  の raw pack
  のまま維持しつつ、
  `NesPalette`
  契約の変更範囲を
  Pico active path
  の
  `PalTable`
  局所変換に限定した

## 0.80 `0.1.73` 背景描画 hot path を `BgTileDescriptor + renderBgTile()` へ集約する初段を実装 (2026-04-21)

- system version を
  `0.1.73`
  へ更新した
- 正本設計は
  `docs/design/BG_DRAWLINE_HOTPATH_REDESIGN_20260421.md`
  とした
- `infones/InfoNES.cpp`
  の
  `InfoNES_DrawLine()`
  について、
  背景描画の
  left partial /
  main tiles /
  right partial
  の
  3 経路に分かれていた
  tile 展開を、
  `BgTileDescriptor`
  と
  `renderBgTile()`
  を使う
  単一 tile renderer
  へ寄せた
- `MapperPPU()`
  の呼び順は
  現行どおり
  tile 順で維持し、
  各 tile について
  descriptor を解く
  →
  その tile を描く
  →
  直後に
  `MapperPPU(PATTBL(...))`
  を呼ぶ
  逐次処理とした
- `BackgroundOpaqueLine`
  の
  scanline 先頭全 0 初期化と、
  BG 左端クリップ /
  上下クリップ時の
  clear は
  renderer の外側で維持した
- 目的は、
  `0.1.72`
  で回復した表示意味を保ったまま、
  背景描画 hot path の
  三重実装を整理し、
  次段の実機確認と
  追加最適化の土台をそろえること
- 実機ログ
  `pico20260421_211331.log`
  では、
  `Xevious.nes`
  の起動と
  menu 復帰、
  その後の
  `LodeRunner.nes`
  起動まで通った
- 同ログの
  `[PERF]`
  150 本を集計すると、
  平均
  `frames=52.95`
  `cpu_pct=100.83`
  `draw_us=662522.97`
  `lcd_wait_us=29253.61`
  `lcd_flush_us=5575.20`
  だった
- 少なくとも
  この run では
  `mapper0 RAM malloc failed`
  や
  menu 再入場失敗は出ておらず、
  背景描画 hot path 集約の初段で
  致命的な回帰は見えていない

## 0.81 `0.1.74` `renderBgTile()` の 8 pixel bit 展開を LUT 化する初段を実装 (2026-04-21)

- system version を
  `0.1.74`
  へ更新した
- 正本設計は
  `docs/design/BG_TILE_RENDER_LUT_REDESIGN_20260421.md`
  とした
- `infones/InfoNES.cpp`
  に
  RAM 常駐の
  background tile LUT
  を追加し、
  `InfoNES_Init()`
  で初期化するようにした
- LUT は
  4 pixel
  分の
  2-bit index packed 値と
  opaque mask
  を持ち、
  `renderBgTile()`
  は
  plane byte
  の
  high nibble /
  low nibble
  から
  2 回引いて描画する形へ変えた
- full tile
  (`clip_left=0`
  `clip_right=8`)
  では
  8 pixel
  の fast path
  を通し、
  partial tile
  は
  同じ renderer
  内の clip path
  で扱う
- `MapperPPU()`
  の
  tile 順呼び出し、
  `BackgroundOpaqueLine`
  の
  scanline 先頭全 0 初期化、
  clip 意味は
  変更していない
- 実機ログ
  `pico20260421_212101.log`
  では、
  `Xevious.nes`
  実行中に
  色や形の崩れは見えていない、
  というユーザー確認が取れた
- 同ログの
  `[PERF]`
  173 本を集計すると、
  平均
  `frames=52.69`
  `cpu_pct=100.86`
  `draw_us=649051.07`
  `lcd_wait_us=32011.29`
  `lcd_flush_us=5532.02`
  だった
- `0.1.73`
  の
  `pico20260421_211331.log`
  と比べると、
  【推定】
  `frames`
  はほぼ同等で、
  `draw_us`
  は
  小幅改善の可能性がある

## 0.82 `0.1.75` 背景 tile の attribute / palette base 解決を 4-tile 単位で再利用する初段を実装 (2026-04-21)

- system version を
  `0.1.75`
  へ更新した
- 正本設計は
  `docs/design/BG_TILE_PREPROCESS_REUSE_REDESIGN_20260421.md`
  とした
- `infones/InfoNES.cpp`
  の
  `InfoNES_DrawLine()`
  背景描画について、
  tile ごとに毎回行っていた
  `attrBase[tileX >> 2]`
  の参照と
  palette base
  解決を、
  line 内 local state
  で
  4-tile 単位に再利用する形へ変えた
- 具体的には、
  current
  `attrBase`
  と
  current
  `tileX >> 2`
  を持ち、
  その組が変わったときだけ
  `pPalTbl`
  を再計算する
  形にした
- `emitBgTile()`
  は
  解決済み `pal`
  を受ける
  `buildBgTile()`
  へ流す形に縮めた
- `renderBgTile()`
  の LUT 方式、
  `MapperPPU()`
  の tile 順呼び出し、
  `BackgroundOpaqueLine`
  の line-start clear、
  partial clip の意味は
  変更していない

## 0.83 `0.1.76` attribute / palette base 再利用を 2-tile 単位へ修正する (2026-04-21)

- system version を
  `0.1.76`
  へ更新した
- `0.1.75`
  では
  palette base
  再利用キーを
  `attrBase`
  と
  `tileX >> 2`
  だけで持っていたため、
  1 attribute byte
  内の
  左右 2 tile quadrant
  をまたいで
  同じ palette
  を使ってしまい、
  実機で
  背景崩れ /
  色ずれが出た
- そこで
  `infones/InfoNES.cpp`
  の
  cache key
  に
  `tileX & 2`
  を追加し、
  palette base
  再利用を
  4-tile 単位ではなく
  **2-tile 単位**
  へ縮めた
- これにより、
  attribute byte
  の
  左右 quadrant
  ごとの差を維持しつつ、
  同一 quadrant
  内だけで
  palette base
  を再利用する形に戻した
- 実機ログ
  `pico20260421_220241.log`
  では、
  ユーザー確認として
  **崩れや色違いはなくなった**
  との結果が得られた
- 同ログの
  `[PERF]`
  210 本を集計すると、
  平均
  `frames=53.10`
  `cpu_pct=100.92`
  `draw_us=647920.21`
  `lcd_wait_us=35955.67`
  `lcd_flush_us=5573.45`
  だった
- `0.1.74`
  の
  `pico20260421_212101.log`
  と比べると、
  【推定】
  `frames`
  と
  `draw_us`
  は
  ごく小幅だが改善寄りで、
  表示も正常化したため
  `0.1.76`
  は
  採用候補と判断できる

## 0.84 `0.2.00` 標準 `256x240` と stretch `320x300` の runtime 切替初段を実装 (2026-04-21)

- system version を
  `0.2.00`
  へ更新した
- 正本設計は
  `docs/design/NES_STRETCH_VIEW_TOGGLE_REDESIGN_20260421.md`
  とした
- `platform/display.h`
  `platform/display.c`
  では
  NES view 専用の内部 scale mode を追加し、
  標準
  `256x240`
  と
  stretch
  `320x300`
  の
  2 つの viewport を切り替えられるようにした
- `InfoNES_PostDrawLine()`
  には
  固定 `5:4`
  stretch path を追加し、
  横方向は
  4 pixel
  を
  5 pixel
  へ、
  縦方向は
  4 line
  を
  5 line
  へ伸ばす専用表示経路を入れた
- `drivers/lcd_spi.c`
  では
  LCD staging buffer を
  最大
  `320`
  幅 /
  `10`
  line
  前提へ広げた
- `platform/input.c`
  と
  `infones/InfoNES.h`
  `infones/InfoNES.cpp`
  では、
  `Shift+W`
  を
  `PAD_SYS_VIEW_TOGGLE`
  として追加し、
  `KEY_STATE_PRESSED`
  の押下 edge
  でだけ
  表示切替が発火するようにした
- build は成功し、
  生成物は
  `PicoCalc NESco Ver. 0.2.00`
  だった
- `arm-none-eabi-size`
  では
  `text=289936`
  `data=0`
  `bss=120916`
  だった
- 実機確認で、
  stretch 表示から標準
  `256x240`
  へ戻した直後に、
  以前の広い viewport の周囲が残ることが分かった
- これに対して
  `display_toggle_nes_view_scale()`
  で切替時に一度
  `320x320`
  全体を clear してから、
  新しい viewport を張り直す修正を追加した
- 修正後の build は成功し、
  生成物は
  `PicoCalc NESco Ver. 0.2.00`
  Build Apr 21 2026 22:39:22
  だった
- 修正後の
  `arm-none-eabi-size`
  では
  `text=289976`
  `data=0`
  `bss=120916`
  だった
- 実機ログ
  `pico20260421_231737.log`
  で、
  stretch 表示 →
  標準表示
  へ戻した後も問題なさそうだという
  ユーザー確認を得た
- あわせて
  `platform/rom_menu.c`
  では
  `Shift+W`
  を help へ追記し、
  ROM 選択画面の起動キーを
  `ENTER/x`
  から
  `ENTER/-`
  へ統一した
- footer と help 文言も
  `ENTER/-`
  表記へ更新し、
  ユーザー確認では
  「よくなりました」
  との評価を得た

## 0.85 `0.2.01` stretch まわりの表示と ROM menu 表記を整理 (2026-04-21)

- system version を
  `0.2.01`
  へ更新した
- `platform/display.c`
  では、
  NES view に入る時と
  stretch から標準表示へ戻る時の共通経路で、
  一度
  `320x320`
  全体を clear した後に
  viewport 外側へ
  `Shift+W Stretch Screen`
  を描くようにした
- これにより、
  ROM menu 側では
  `Shift+W`
  の表示を持たず、
  実際にゲーム画面へ入った時だけ
  切替案内が見える形へ寄せた
- `platform/rom_menu.c`
  では
  現在カーソル位置を
  `n/m`
  で出す表示を追加した
- ROM menu 側の
  `Shift+W`
  表記は削除し、
  起動キーは
  `ENTER/-`
  表記へ統一したまま維持した
- `platform/rom_image.c`
  では、
  一番上の本体フラッシュ常駐 entry が分かりやすいよう、
  detail を
  `SYSTEM FLASH`
  表記へそろえた
- build は成功し、
  生成物は
  `PicoCalc NESco Ver. 0.2.01`
  Build Apr 21 2026 23:42:06
  だった
- `arm-none-eabi-size`
  では
  `text=290216`
  `data=0`
  `bss=120916`
  だった

## 0.86 `0.2.02` audio output gain 比較のため正規化係数を定数化する (2026-04-22)

- system version を
  `0.2.02`
  へ更新した
- `platform/audio.c`
  では
  `InfoNES_SoundOutput()`
  の
  正規化式
  `((mixed * 255u) + 640u) / 1280u`
  を、
  直接式ではなく
  named constant で表す形へ整理した
- 今回追加した定数は
  `AUDIO_MIX_NOISE_WEIGHT`
  `AUDIO_MIX_OUTPUT_SCALE`
  `AUDIO_MIX_DIVISOR`
  `AUDIO_MIX_ROUND_BIAS`
  である
- この段では
  `noise*4`
  の比率も
  `1280`
  の実効ゲインも変えておらず、
  音の意味は変えずに
  次段の係数比較をしやすくすることだけを目的とした
- build は成功し、
  生成物は
  `PicoCalc NESco Ver. 0.2.02`
  の banner を出した
- build 時の
  `arm-none-eabi-size`
  と
  build id
  は
  CMake の post-build 出力で確認する運用に統一した

## 0.87 `0.2.03` active path で audio debug poll を有効化する (2026-04-22)

- system version を
  `0.2.03`
  へ更新した
- 実機ログ
  `pico20260422_184209.log`
  では、
  `[PERF]`
  は長時間出ている一方で
  `[AUDIO]`
  と
  `[AUDIO_MIX]`
  の周期ログが 1 行も出ていなかった
- コード確認の結果、
  現在の本体経路
  `infones/InfoNES.cpp`
  の
  `InfoNES_HSync()`
  末尾には
  `audio_debug_poll()`
  が入っておらず、
  旧
  `core/ppu.c`
  側にだけ呼び出しが残っていた
- そこで
  `infones/InfoNES.cpp`
  に
  `audio.h`
  を追加し、
  `InfoNES_HSync()`
  末尾で
  `audio_debug_poll()`
  を呼ぶようにした
- 目的は、
  係数比較へ進む前に
  active path
  で
  `[AUDIO]`
  と
  `[AUDIO_MIX]`
  の周期ログを取得できる状態へ戻すこと

## 0.88 `0.2.04` audio output gain の最初の比較段として divisor を 1152 へ下げる (2026-04-22)

- system version を
  `0.2.04`
  へ更新した
- `platform/audio.c`
  では
  `AUDIO_MIX_DIVISOR`
  を
  `1280`
  から
  `1152`
  へ変更した
- `AUDIO_MIX_ROUND_BIAS`
  は
  `AUDIO_MIX_DIVISOR / 2`
  のままなので、
  丸めの意味は維持したまま
  全体ゲインだけを 1 段上げる変更である
- `noise*4`
  は今回も固定し、
  チャンネル比は変えていない
- 今回の目的は、
  `0.2.03`
  を基準に
  `[AUDIO]`
  の
  `out_peak`
  /
  `out_rms`
  /
  `underruns`
  と
  聴感を比較するための最初の候補値を入れることである

## 0.89 `0.2.05` audio output gain の次段として divisor を 1120 へ下げる (2026-04-22)

- system version を
  `0.2.05`
  へ更新した
- `platform/audio.c`
  では
  `AUDIO_MIX_DIVISOR`
  を
  `1152`
  から
  `1120`
  へ変更した
- `AUDIO_MIX_ROUND_BIAS`
  は
  `AUDIO_MIX_DIVISOR / 2`
  のままなので、
  丸めの意味を保ったまま
  全体ゲインだけを
  さらに小刻みに上げる変更である
- `noise*4`
  は今回も固定し、
  チャンネル比は変えていない
- `0.2.04`
  実機確認では、
  片チャンネル修正後に
  十分大きく感じられ、
  聴感上の音割れも目立たなかった
  一方で、
  一部窓で
  `clipped`
  が非ゼロになったため、
  今回は
  `1024`
  へ一気に進めず
  中間の
  `1120`
  を次候補にした
- 今回の目的は、
  `0.2.04`
  を基準に
  `[AUDIO]`
  の
  `out_peak`
  /
  `out_rms`
  /
  `underruns`
  を主指標として、
  `clipped`
  と聴感悪化の有無を比較することである

## 0.90 `0.2.06` game 開始時のブチ音を抑える startup ramp を追加する (2026-04-22)

- system version を
  `0.2.06`
  へ更新した
- `platform/audio.c`
  に
  `AUDIO_STARTUP_RAMP_SAMPLES`
  と
  `s_startup_ramp_pos`
  を追加し、
  audio open 後の最初の数百 sample だけ
  出力振幅を
  `128`
  中心へ
  緩やかに立ち上げるようにした
- 目的は、
  menu → game
  切替直後の
  PWM 出力立ち上がりを急変させず、
  game 開始時の
  ブチ音を軽減することである
- 音量調整の本体方針
  `AUDIO_MIX_DIVISOR=1120`
  と
  `noise*4`
  の比率は
  この段では変更していない

## 0.91 `0.2.07` game 開始直後の短時間無音を追加してブチ音を再抑制する (2026-04-22)

- system version を
  `0.2.07`
  へ更新した
- `0.2.06`
  の
  startup ramp
  だけでは
  game 開始時の
  ブチ音が残った
- そこで
  `drivers/pwm_audio.c`
  に
  `PICO_AUDIO_STARTUP_SILENCE_SAMPLES`
  と
  `s_startup_silence_samples`
  を追加し、
  audio open
  直後の短時間だけ
  DMA half-buffer
  全体へ
  `128`
  の完全無音を流すようにした
- 目的は、
  menu → game
  切替直後の
  PWM / DMA
  立ち上がり段差そのものを吸収し、
  ブチ音をさらに抑えることである
- 音量調整の本体方針
  `AUDIO_MIX_DIVISOR=1120`
  と
  `noise*4`
  の比率は
  この段でも変更していない

## 0.92 `0.2.08` audio hardware を power-on init 化して open/close を論理操作へ寄せる (2026-04-22)

- system version を
  `0.2.08`
  へ更新した
- `infones/InfoNES_pAPU.h`
  に
  `INFONES_AUDIO_DEFAULT_SAMPLE_RATE`
  を追加し、
  boot 時 hardware init の基準 sample rate を
  `22050`
  へ固定した
- `platform/main.c`
  では
  電源 ON 時の peripheral init に
  `audio_init()`
  を追加し、
  ROM 開始前に PWM / DMA を 1 回だけ立ち上げるようにした
- `platform/audio.c`
  では
  `audio_reset_runtime_state()`
  を追加し、
  ring buffer /
  startup ramp /
  mix 側統計の reset を
  hardware init と分離した
- `InfoNES_SoundOpen()`
  は
  論理 open として
  runtime state reset のみを担当し、
  hardware sample rate が一致する限り
  `pwm_audio_init()`
  を呼ばない形へ変えた
- ただし
  `sample_rate`
  が現在の hardware 設定値と異なる場合だけは、
  例外的に再初期化する経路を残した
- `InfoNES_SoundClose()`
  は
  `pwm_audio_close()`
  へ進まず、
  silent idle 用の runtime state reset のみを行う形へ変えた
- `drivers/pwm_audio.c`
  には
  `pwm_audio_reset_stats()`
  を追加し、
  hardware 常駐のまま
  ROM 開始ごとに
  `[AUDIO]`
  /
  `[AUDIO_MIX]`
  の比較窓を切り直せるようにした
- この段の目的は、
  `menu -> game`
  切替時の
  PWM / DMA 再初期化を避け、
  ROM 開始時の
  ブチ音を減らすための本命構造へ寄せることである

## 0.93 `0.2.09` game 開始時の startup ramp を撤去する (2026-04-22)

- system version を
  `0.2.09`
  へ更新した
- ユーザー確認では、
  power-on init 化後の
  電源 ON 時の
  `ブチ`
  は運用上あまり気にならず、
  一方で
  game 開始時の
  slow start により
  BGM 先頭が途切れる方が気になるとの判断だった
- そこで
  `platform/audio.c`
  から
  startup ramp
  用の
  `AUDIO_STARTUP_RAMP_SAMPLES`
  と
  `s_startup_ramp_pos`
  を撤去し、
  `InfoNES_SoundOutput()`
  での
  gradual fade-in
  をやめた
- これにより、
  power-on init /
  silent idle /
  startup silence
  の構造は維持したまま、
  game 開始直後の
  BGM 先頭欠けだけを除去する方針へ寄せた

## 0.94 `0.2.10` game 開始時の ring 全面無音化を撤去する (2026-04-22)

- system version を
  `0.2.10`
  へ更新した
- ユーザー判断では、
  game 開始時に残っていた
  `BGM`
  先頭欠けの原因として、
  `audio_reset_runtime_state()`
  が
  `g_audio_ring`
  全体を
  `128`
  で埋め直している点が不要寄りだと整理した
- そこで
  `platform/audio.c`
  の
  `audio_reset_runtime_state()`
  から
  `memset(g_audio_ring, 128, AUDIO_RING_SIZE)`
  を削除し、
  ring 全面無音化だけをやめた
- `s_ring_write`
  /
  `s_ring_read`
  /
  `s_mix_dc_estimate`
  /
  各種統計 reset はそのまま残し、
  目的を
  game 開始時の
  不要な無音区間除去に限定した
- ユーザー確認では、
  `menu -> game`
  の
  ブチ音はなくなった
- 一方で、
  game 冒頭で音が乱れることはあるが、
  電源 ON 時の
  ブチ音は運用上あまり気にならないため、
  この段階でいったん採用とした

## 0.95 `0.2.11` runtime log を banner 以外 default disable にする (2026-04-22)

- system version を
  `0.2.11`
  へ更新した
- `CMakeLists.txt`
  の
  `NESCO_RUNTIME_LOGS`
  を
  `OFF`
  前提で再 configure し、
  runtime log を
  default では出さない構成へ戻した
- `platform/runtime_log.h`
  を使う箇所と、
  file-local suppress を入れた箇所を含む
  通常経路の log を見直し、
  起動 banner 以外を
  default silence
  に寄せた
- ユーザー提示ログ
  `pico20260422_205730.log`
  では、
  `PicoCalc NESco Ver. 0.2.11 Build Apr 22 2026 20:56:18`
  の
  banner だけが出て、
  それ以外の runtime log は出ていないことを確認した

## 0.96 `0.3.00` screenshot capture を実装する (2026-04-23)

- system version を
  `0.3.00`
  へ更新した
- screenshot 系の設計文書として
  `docs/design/SCREENSHOT_DETAILED_DESIGN_20260423.md`
  `docs/design/SCREENSHOT_INTERFACE_SPEC_20260423.md`
  `docs/design/SCREENSHOT_IMPLEMENTATION_POLICY_20260423.md`
  `docs/design/SCREENSHOT_IMPLEMENTATION_PROCEDURE_20260423.md`
  `docs/test/SCREENSHOT_TEST_SPEC_20260423.md`
  を追加し、
  初期版の正本を整理した
- 実装として
  `platform/screenshot.c`
  `platform/screenshot.h`
  `platform/screenshot_storage.c`
  `platform/screenshot_storage.h`
  を追加し、
  `F5`
  screenshot 要求、
  `VBLANK`
  起点の capture、
  `BMP`
  保存、
  復帰処理までを独立責務で実装した
- 入力系は
  `platform/input.c`
  `infones/InfoNES.h`
  `infones/InfoNES.cpp`
  を更新し、
  `F1`
  reset /
  `F5`
  screenshot
  に役割を分離した
- 表示系は
  `drivers/lcd_spi.c`
  に
  LCD readback
  を追加した
- readback 成立条件の探索では、
  複数の失敗パターンを経て、
  最終的に
  `seq=hold`
  `edge=fall`
  `shift=0`
  `delay=1us`
  `dummy=1`
  `bpp=2`
  `swap=0`
  を採用条件とした
- 初期の
  `320x320`
  全面バッファ確保は
  RAM
  的に不適切だったため不採用とし、
  最終的には
  chunk
  単位の readback と
  chunk
  単位の
  BMP
  書き込みへ切り替えた
- `platform/audio.c`
  `drivers/pwm_audio.c`
  `platform/display.c`
  を更新し、
  screenshot 中は
  audio
  の生成 / 出力と
  frame pacing
  を適切に止め、
  復帰後に
  高速再生 /
  高速進行
  しないよう調整した
- `README.md`
  `platform/rom_menu.c`
  も更新し、
  key help
  と screenshot 機能の説明を現状へ合わせた
- ユーザー確認では、
  ノーマル表示 /
  ストレッチ表示の両方で screenshot が保存でき、
  capture 後に
  ゲーム速度が速くなる問題も解消した
- その後、
  runtime log は再び
  banner
  以外
  default disable
  に戻し、
  screenshot 実運用時に誤解を招く
  `LCDRD`
  生ログや起動時総当たりログは外した

## 0.97 `0.3.00` clean build warning を解消する (2026-04-23)

- `make clean ; make`
  時に出ていた warning を見直した
- 自前コード側は、
  `platform/main.c`
  `platform/audio.c`
  `platform/rom_image.c`
  `drivers/pwm_audio.c`
  `drivers/lcd_spi.c`
  の未使用変数を、
  `#if`
  または
  `(void)x;`
  で解消した
- `fatfs/ff.c`
  については
  `-Wno-stringop-overflow`
  のような compile option suppress は採らず、
  `gen_numname()`
  の 16 進展開ループ側を最小修正して warning を消した
- 最終的に
  `PicoCalc NESco Ver. 0.3.00 Build Apr 23 2026 23:19:28`
  の
  `make clean ; make`
  は warning なしで完了した

## 0.98 `0.3.01` PSRAM / audio 整合性実験ビルドを作成する (2026-04-25)

- system version を
  `0.3.01`
  へ更新した
- PicoCalc 回路図と
  ESP-PSRAM64H
  データシート確認により、
  PicoCalc
  搭載 PSRAM は
  `ESP-PSRAM64H`
  であり、
  チップ自体の最大 clock は
  `133MHz`
  と整理した
- ただし、
  `Page Size`
  節の記述により、
  1KB page boundary
  をまたぐ burst は
  最大
  `84MHz`
  と扱う必要があるため、
  本番ログ実装では
  1KB 境界をまたぐ連続転送を避ける方針とした
- `drivers/psram_probe.c`
  `drivers/psram_probe.h`
  を追加し、
  PSRAM
  の短距離 bring-up
  と速度条件比較を
  boot-time probe
  として実装した
- 実験 case は、
  `133MHz / clkdiv=1.0 / 4KB`
  を基準点とし、
  `250MHz / clkdiv=1.0 / 4KB`
  `250MHz / clkdiv=1.0 / 64KB`
  `250MHz / clkdiv=1.2 / 4KB`
  `250MHz / fudge=false / 4KB`
  を比較できる形に整理した
- `drivers/pwm_audio.c`
  には
  `NESCO_PSRAM_PROBE`
  有効時だけ出る
  `[AUDIO_PWM_STEP]`
  ログを追加し、
  `pwm_audio_init()`
  の
  DMA channel claim、
  DMA timer claim、
  DMA fraction 計算、
  IRQ enable、
  PWM start
  のどこで停止したかを
  実機ログから読めるようにした
- `platform/main.c`
  では
  `[TASK_BEGIN]`
  `[CASE_BEGIN]`
  `[CASE_END]`
  `[PROBE_PHASE]`
  `[TASK_END]`
  を出力し、
  人間側が
  ハングか作業中かを判断できる終了条件を明示した
- `drivers/psram_trace.c`
  `drivers/psram_trace.h`
  を追加し、
  BokosukaWars
  調査用の固定長 event を
  PSRAM
  へ記録する初期実装を入れた
- build は
  `cmake -S . -B build -DNESCO_PSRAM_PROBE=ON -DNESCO_RUNTIME_LOGS=OFF`
  と
  `cmake --build build -j$(nproc)`
  で成功した
- 生成物は
  `build/Picocalc_NESco.uf2`
  であり、
  build 時の banner は
  `PicoCalc NESco Ver. 0.3.01`
  系として扱う
- この版は
  PSRAM / audio 整合性を見るための実験ビルドであり、
  実機確認結果を見てから
  PSRAM trace
  を本番ログ取得へ進める

## 0.99 `0.3.02` PSRAM 250MHz clkdiv と audio IRQ 停止位置を追加確認する (2026-04-25)

- system version を
  `0.3.02`
  へ更新した
- `0.3.01`
  の実機ログ
  `pico20260425_070044.log`
  では、
  `case 0`
  `133MHz / clkdiv=1.0 / 4KB`
  は成功し、
  `case 1`
  `250MHz / clkdiv=1.0 / 4KB`
  は
  `READ8`
  段階で
  `fail=00000002 exp=00000002 got=00000003`
  となった
- 同ログでは、
  `display_init()`
  と
  `input_init()`
  は通過し、
  `pwm_audio_init()`
  は
  `[AUDIO_PWM_STEP] before_irq_enable`
  で停止しており、
  `[TASK_END]`
  は出ていなかった
- `0.3.02`
  では、
  PSRAM
  の pre-init case に
  `250MHz / clkdiv=1.2 / 4KB`
  と
  `250MHz / clkdiv=1.5 / 4KB`
  を追加し、
  `clkdiv=1.0`
  以外で 250MHz 運用の余地があるかを同じ実機検証で確認できるようにした
- audio 側は
  `before_irq_enable`
  をさらに分解し、
  `dma_channel_set_irq0_enabled()`
  `irq_set_exclusive_handler()`
  `irq_set_enabled()`
  のどこで停止するかをログから読めるようにした
- audio 初期化後の PSRAM case は、
  `250MHz / clkdiv=1.2 / 4KB`
  と
  `250MHz / clkdiv=1.5 / 64KB`
  に絞り、
  本番候補の低速化条件だけを確認する構成にした

## 1.00 `0.3.03` DMA IRQ enable 直後停止への対策を入れる (2026-04-25)

- system version を
  `0.3.03`
  へ更新した
- `0.3.02`
  の実機ログ
  `pico20260425_071920.log`
  では、
  `case 3`
  `250MHz / clkdiv=1.5 / 4KB`
  が成功し、
  `clkdiv=1.0`
  と
  `clkdiv=1.2`
  は
  `READ8`
  段階で失敗した
- 同ログでは、
  audio 初期化は
  `dma_channel_set_irq0_enabled()`
  と
  `irq_set_exclusive_handler()`
  を通過し、
  `irq_set_enabled(DMA_IRQ_0, true)`
  の直前で停止した
- `pwm_audio_dma_irq0_handler()`
  は、
  旧実装では
  `s_audio_live`
  が false のときに
  DMA IRQ
  を acknowledge せず return していたため、
  IRQ 有効化直後に pending IRQ が入ると抜けられない可能性があった
- `0.3.03`
  では handler 冒頭で
  DMA IRQ0 status
  が立っていれば先に acknowledge し、
  その後で
  `s_audio_live`
  を見る順序へ変更した
- `irq_set_enabled()`
  前には
  `dma_channel_acknowledge_irq0()`
  と
  `irq_clear(DMA_IRQ_0)`
  を実行し、
  pending IRQ
  を明示的に落としてから有効化するようにした
- さらに
  `s_audio_live = true`
  を
  `irq_set_enabled()`
  の直前へ移動し、
  IRQ 有効化直後に handler が呼ばれても通常 path へ入れるようにした
- 実機ログでは、
  `[AUDIO_PWM_STEP] before_irq_clear`
  `[AUDIO_PWM_STEP] after_irq_clear`
  と、
  `after_irq_set_enabled`
  以降へ進むかを確認する

## 1.01 `0.3.04` PSRAM trace 初期化条件を成功条件へ合わせる (2026-04-25)

- system version を
  `0.3.04`
  へ更新した
- `0.3.03`
  の実機ログ
  `pico20260425_074307.log`
  では、
  `[TASK_END] psram_probe completed=1 passed=1`
  まで到達した
- 同ログでは、
  `case 0`
  `133MHz / clkdiv=1.0 / 4KB`
  が成功し、
  `case 1`
  `250MHz / clkdiv=1.0 / 4KB`
  と
  `case 2`
  `250MHz / clkdiv=1.2 / 4KB`
  は
  `READ8`
  段階で失敗した
- 同ログでは、
  `case 3`
  `250MHz / clkdiv=1.5 / 4KB`
  と
  `case 5`
  `250MHz / clkdiv=1.5 / 64KB`
  が成功した
- audio 初期化は
  `[AUDIO_PROBE] audio_init end ready=1 rate=22050`
  まで到達し、
  `0.3.02`
  で止まっていた
  `irq_set_enabled(DMA_IRQ_0, true)`
  後の停止は解消した
- `psram_trace`
  初期化は旧実装で
  `clkdiv=1.0`
  を指定していたため、
  実機ログで成功した
  `clkdiv=1.5`
  へ変更した
- `[PSRAM_TRACE]`
  ログには、
  実際に使う trace 初期化条件を確認できるよう
  `clkdiv_x100=150`
  を追加した
- 次の確認では、
  `0.3.04`
  起動ログで
  `[PSRAM_TRACE] ready=1 clkdiv_x100=150`
  と
  `[TASK_END]`
  が出ることを確認する
- 追加観察として、
  BokosukaWars 起動後に
  上方向または A/B 相当の入力が押しっぱなしのように見え、
  主人公が上へ移動して画面上に張り付き、
  主人公 / ナイト / 兵士の選択が連続で切り替わる症状が
  ユーザーから報告された
- 既存ログ
  `pico20260425_074307.log`
  の
  `BOKO_STATE`
  では、
  `p1=00000020`
  `p1=00000040`
  `p1=00000050`
  `p1=00000060`
  `p1=000000A0`
  が確認できたが、
  A/B bit
  `0x01`
  `0x02`
  はこのログ範囲では確認できなかった
- そのため、
  次の切り分けでは
  `BOKO_STATE`
  だけでなく、
  PicoCalc keyboard event の
  `key/state`
  と
  `InfoNES_PadState()`
  直後の
  `PAD1_Latch`
  を対応づけて記録する

## 1.02 `0.3.05` BokosukaWars 押しっぱなし疑いの入力 summary を追加 (2026-04-25)

- system version を
  `0.3.05`
  へ更新した
- `NESCO_PSRAM_PROBE`
  ビルドでは、
  `INFONES_ENABLE_INPUT_IO_LOG`
  も自動で有効にするようにした
- これにより、
  `$4016`
  `$4017`
  の serial read は
  既存の
  `[IN_SUM]`
  で
  latch,
  8bit 読み取り結果、
  extra read
  を確認できる
- PicoCalc keyboard 入力側には
  `[KEY_SUM]`
  を追加した
- `[KEY_SUM]`
  は、
  keyboard event があった時、
  `s_pad1_state`
  が変化した時、
  または非ゼロ入力が約 60 poll 継続した時だけ出す
- これにより、
  物理 keyboard controller から
  press / hold / release
  が来ているか、
  `InfoNES_PadState()`
  の
  `PAD1_Latch`
  がどう変わったか、
  さらに
  `$4016`
  serial read でゲーム側へどう見えているかを同じ実機ログで対応づける

## 1.03 `0.3.06` runtime PSRAM trace write を安全弁つきにする (2026-04-25)

- system version を
  `0.3.06`
  へ更新した
- `0.3.05`
  の実機ログ
  `pico20260425_075345.log`
  では、
  2 回の起動とも
  `[TASK_END] psram_probe completed=1 passed=1`
  まで到達した
- 同ログでは、
  `[PSRAM_TRACE] ready=1 clkdiv_x100=150 capacity=262144 count=0`
  も確認できた
- 同ログでは、
  BokosukaWars 開始前後に
  `[KEY_SUM]`
  と
  `[IN_SUM]`
  が出ており、
  入力 summary 自体は出力できている
- ただし 2 回とも、
  BokosukaWars 開始直後の
  `[BOKO_2C]`
  が最後のログ行になった
- コード上、
  `[BOKO_2C]`
  出力直後には
  `nesco_psram_trace_record_boko2c()`
  が呼ばれ、
  その内部で
  `psram_write()`
  を実行していた
- そのため、
  `NESCO_PSRAM_TRACE_WRITES`
  option を追加し、
  明示的に ON しない限り
  runtime PSRAM trace write
  は行わないようにした
- `[PSRAM_TRACE]`
  ログには
  `writes=0/1`
  を追加し、
  実機ログから runtime PSRAM write が有効かどうかを確認できるようにした
- 次の実機確認では、
  `writes=0`
  の状態で
  `[BOKO_2C]`
  直後停止が消えるか、
  ESC
  が効くかを確認する

## 1.04 `0.3.06` runtime PSRAM write 無効化後に BokosukaWars 症状を再現 (2026-04-25)

- `0.3.06`
  の実機ログ
  `pico20260425_080242.log`
  では、
  `[PSRAM_TRACE] ready=1 writes=0 clkdiv_x100=150 capacity=262144 count=0`
  を確認した
- 同ログでは、
  `[TASK_END] psram_probe completed=1 passed=1`
  まで到達した
- `writes=0`
  にしたことで、
  `0.3.05`
  で発生していた
  `[BOKO_2C]`
  直後停止は消え、
  `BOKO_8827`
  `BOKO_888E`
  の後続ログも出るようになった
- ユーザー観察では、
  ゲームは途切れ途切れながら進行し、
  最後に木へぶつかったあと動けなくなった
- 同観察では、
  木接触後、
  途切れ途切れだった BGM が途切れなくなった
- 同ログの最後には、
  `[KEY_SUM] seq=4324 events=1 first=B1/1 last=B1/1 before=00000000 after=00000000 last=00000000 sys=00000001 hold=0`
  が出ており、
  ESC key press は keyboard polling 層まで届いている
- このため、
  現時点では
  CPU 全体の hard hang
  ではなく、
  入力 polling と audio は動き続けたまま、
  BokosukaWars のゲーム進行側が停止または同一状態に入った可能性がある
- 次の切り分けでは、
  木接触後も
  frame,
  PC,
  scanline,
  `PAD1_Latch`,
  `RAM[0x002C]`,
  `RAM[0x003B]`,
  `RAM[0x003C]`,
  `RAM[0x009D]`
  を低頻度 heartbeat として出し、
  CPU が回っているか、
  同じ PC 付近を回っているか、
  ゲーム state が固定されたかを見る

## 1.05 `0.3.07` BokosukaWars heartbeat を追加 (2026-04-25)

- system version を
  `0.3.07`
  へ更新した
- `0.3.06`
  で木接触後の停止症状を再現でき、
  ESC key press は keyboard polling 層まで届くことが分かったため、
  次は CPU / game state の進行有無を見る段階へ進んだ
- `InfoNES_PadState()`
  直後に
  `[BOKO_HB]`
  を追加した
- `[BOKO_HB]`
  は通常 60 VBlank ごとに出し、
  `PAD_System`
  が非ゼロの場合は即時に出す
- 出力項目は
  heartbeat sequence,
  `PC`,
  `PPU_Scanline`,
  `PAD1_Latch`,
  `PAD2_Latch`,
  `PAD_System`,
  `RAM[0x002C]`,
  `RAM[0x003B]`,
  `RAM[0x003C]`,
  `RAM[0x009D]`,
  `A`
  とした
- 次の実機確認では、
  木接触後も
  `[BOKO_HB]`
  が継続するか、
  `PC`
  や代表 RAM が固定されるかを見る

## 1.06 `0.3.08` PSRAM trace ring の freeze / dump 経路を追加 (2026-04-25)

- system version を
  `0.3.08`
  へ更新した
- PSRAM trace はリングバッファなので、
  現象を捕まえた時点で記録を freeze しないと、
  重要な直前ログが後続イベントで上書きされる
- `nesco_psram_trace_freeze()`
  を追加し、
  freeze 後は
  `nesco_psram_trace_record_*()`
  が新規 event を書かないようにした
- `nesco_psram_trace_dump_recent(max_events)`
  を追加し、
  現在の ring 末尾から直近最大
  `max_events`
  件だけを
  `[PSRAM_DUMP_EVENT]`
  として UART へ出せるようにした
- dump は
  `[PSRAM_DUMP_BEGIN]`
  と
  `[PSRAM_DUMP_END]`
  で開始 / 終了を明示する
- BokosukaWars debug build では、
  ESC
  入力時に一度だけ
  freeze
  と
  最大 64 件 dump
  を行う
- 現在の確認ビルドは
  `NESCO_PSRAM_TRACE_WRITES=OFF`
  のため、
  まずは
  `writes=0`
  の空 dump が安全に終わることを確認する
- `[PSRAM_TRACE]`
  には
  `frozen=0/1`
  を追加した

## 1.07 `0.3.08` PSRAM trace 空 dump 経路の実機確認 (2026-04-25)

- `0.3.08`
  の実機ログ
  `pico20260425_081001.log`
  では、
  `PicoCalc NESco Ver. 0.3.08 Build Apr 25 2026 08:15:06`
  を確認した
- 同ログでは、
  `[PSRAM_TRACE] ready=1 writes=0 frozen=0 clkdiv_x100=150 capacity=262144 count=0`
  を確認した
- 同ログでは、
  `[TASK_END] psram_probe completed=1 passed=1`
  まで到達した
- ESC
  入力後に
  `[BOKO_HB] seq=776 pc=E353 sl=241 p1=00000000 p2=00000000 sys=00000001 r2c=B9 r3b=56 r3c=DD r9d=50 a=0F`
  が出ており、
  ESC
  は heartbeat 層まで届いた
- その直後に
  `[PSRAM_DUMP_BEGIN] ready=1 frozen=1 count=0 requested=64 dump=0`
  と
  `[PSRAM_DUMP_END] dumped=0 count=0 head=0 frozen=1`
  が出ており、
  `writes=0`
  の空 dump 経路は実機で完了した
- 同ログでは、
  `BOKO_HB`
  が 60 VBlank ごとに出ており、
  `pc=E350`
  `pc=E355`
  `pc=E353`
  など、
  木接触後も CPU 側は heartbeat へ到達している
- 次は、
  人間が ESC を押すまでの遅延で ring が上書きされないよう、
  自動 freeze 条件、
  SD カード dump、
  またはその両方を設計する

## 1.08 `0.3.19` BokosukaWars XIP flash trace を追加 (2026-04-25)

- system version を
  `0.3.19`
  へ更新した
- PSRAM
  を使うログ保存方針は終了し、
  BokosukaWars
  解析ログは
  RP2040
  の XIP flash
  後半 1MB
  へ一時保存する方針へ切り替えた
- trace
  は
  `drivers/boko_flash_trace.c`
  / `.h`
  に分離した
- trace
  は
  BokosukaWars
  名を含む ROM path
  のときだけ有効化し、
  それ以外の ROM では
  `[TRACE_SKIP]`
  を出して何もしない
- ROM load
  後、
  emulator reset
  前に
  trace 用 flash 領域をまとめて erase
  し、
  `[TRACE_ERASE_BEGIN]`
  `[TRACE_ERASE_END]`
  `[TRACE_READY]`
  を出す
- ゲーム中は
  64 bytes
  固定長 event
  を SRAM
  の 4KB page buffer
  に積み、
  page
  が埋まった時点で
  erase 済み flash
  へ 4KB 単位で書く
- flash
  書き込み時は
  `[TRACE_FLASH_WRITE_BEGIN]`
  `[TRACE_FLASH_WRITE_END]`
  に
  page,
  offset,
  used,
  events,
  elapsed_us
  を出す
- 保存対象は
  `[BOKO_2C]`
  `[BOKO_STATE]`
  `[BOKO_8827]`
  `[BOKO_888E]`
  `[BOKO_HB]`
  `[BOKO_AUTO_FREEZE]`
  に対応する binary event
  とした
- `BOKO_AUTO_FREEZE`
  検出時には freeze event
  を追加し、
  現在の 4KB page
  を flash
  へ flush
  する
- ESC
  入力時には
  終端 event
  を追加して残り page
  を flush
  し、
  flash trace
  全体を
  `0:/traces/BOKO_XXXX.BIN`
  へ SD dump
  する
- build
  は
  `PicoCalc NESco Ver. 0.3.19 Build Apr 25 2026 10:08:52`
  で成功した
- BSS
  は
  `164752`
  bytes
  だった

## 1.09 `0.3.19` BokosukaWars XIP flash trace 実機確認 (2026-04-25)

- 実機ログ
  `pico20260425_101209.log`
  と
  SD dump
  `BOKO_0005.BIN`
  を確認した
- 実機ログでは
  `PicoCalc NESco Ver. 0.3.19 Build Apr 25 2026 10:08:52`
  を確認した
- trace
  用 XIP flash erase
  は
  `[TRACE_ERASE_BEGIN] backend=xip_flash offset=0x00100000 bytes=1048576 page=4096`
  から始まり、
  `[TRACE_ERASE_END] ok=1 elapsed_us=3801816`
  で完了した
- `TRACE_READY`
  は
  `capacity=1048576`
  `page=4096`
  `event=64`
  を出した
- ゲーム中の 4KB flash write
  は
  page 0 から page 6
  まで発生した
- page 0 から page 4
  の 4KB write
  は約
  `14.2 ms`
  で完了した
- freeze
  検出時の部分 page
  flush
  は
  `used=832`
  `events=333`
  `elapsed_us=10359`
  だった
- ESC
  入力後の終端 page
  flush
  は
  `used=256`
  `events=337`
  `elapsed_us=9695`
  だった
- SD dump
  は
  `[TRACE_SD_END] ok=1 path=0:/traces/BOKO_0005.BIN bytes=28672 events=337 fr=0`
  で完了した
- 添付された
  `BOKO_0005.BIN`
  は
  `28672`
  bytes
  で、
  4KB page
  7 枚分だった
- binary parse
  では
  非 `0xFF`
  record
  が
  `337`
  件あり、
  tag
  別では
  `HDR0=1`
  `HB00=16`
  `STAT=3`
  `2C00=17`
  `8827=22`
  `888E=276`
  `FRZE=1`
  `END0=1`
  だった
- `BOKO_AUTO_FREEZE`
  は
  `t_us=23174598`
  `pc=E355`
  `r2c=B9`
  `r3b=56`
  `r3c=DD`
  `r9d=50`
  で発生した
- ESC
  は
  `BOKO_HB`
  の
  `seq=911`
  `sys=00000001`
  として記録され、
  その後 SD dump
  まで完了した
- 結論として、
  4KB SRAM page buffer
  から XIP flash
  へ退避し、
  ESC
  で SD
  に回収する経路は実機で成立した

## 1.10 `0.3.20` `$4015` read を length counter status へ修正 (2026-04-25)

- system version を
  `0.3.20`
  へ更新した
- BokosukaWars
  の停止解析で、
  実機 trace
  は
  `PC=E350/E355`
  の
  `$4015`
  bit 2
  待ちループに入っていることを示した
- NESdev
  の
  `$4015`
  仕様と
  `fixNES`
  の
  `apuGetReg15()`
  実装を確認し、
  `$4015`
  read
  の bit 0-4
  は
  `$4015`
  への write latch
  ではなく、
  pulse 1,
  pulse 2,
  triangle,
  noise
  の length counter status
  と DMC active status
  を返すものと整理した
- これに従い、
  `infones/K6502_rw.h`
  の
  `$4015`
  read
  は
  `APU_Reg[0x15]`
  をそのまま返値の base
  に使わず、
  `ApuC1Atl`
  `ApuC2Atl`
  `ApuC3Atl`
  `ApuC4Atl`
  `ApuC5DmaLength`
  から status bit
  を合成する形へ修正した
- triangle
  の status bit
  は
  NESdev
  / fixNES
  に合わせ、
  linear counter
  ではなく
  `ApuC3Atl > 0`
  を見る
- `ApuC5DmaLength`
  を
  `InfoNES_pAPU.h`
  へ extern
  追加し、
  DMC active bit
  も length/status 合成に含めた
- local `nes2/infones/K6502_rw.h`
  と照合した結果、
  同じ
  `byRet = APU_Reg[0x15]`
  起点の実装が元
  `nes2/infones`
  側にも存在した
- `Picocalc_NESco`
  の
  `git blame`
  では当該箇所は
  `1c0acde Replace NESco core plan with InfoNES migration baseline`
  由来であり、
  今回の trace
  実装や PicoCalc
  移植中の後続作業で新規混入したものではなく、
  元々の
  `infones`
  実装に関わるバグだった可能性が高い
- build
  は
  `PicoCalc NESco Ver. 0.3.20 Build Apr 25 2026 10:34:09`
  で成功した
- build size
  は
  `text=272480`
  `data=0`
  `bss=164752`
  だった

## 1.12 `0.3.21` BokosukaWars 実機確認で停止が解消した可能性を確認 (2026-04-25)

- 実機ログ
  `pico20260425_103626.log`
  を確認した
- 実機ログでは
  `PicoCalc NESco Ver. 0.3.21 Build Apr 25 2026 12:00:58`
  を確認した
- trace
  erase
  は
  `[TRACE_ERASE_BEGIN] backend=xip_flash offset=0x00102000 bytes=524288 page=4096`
  で始まり、
  `[TRACE_READY] backend=xip_flash offset=0x00102000 capacity=524288 page=4096 event=64`
  まで到達した
- trace
  書き込みは page 0
  の
  `0x00102000`
  から始まり、
  page 127
  の
  `0x00181000`
  まで進んだ
- trace
  は
  `[TRACE_FULL] committed=524288 events=8256`
  に到達したため、
  512KB
  の trace
  領域は最後まで使い切った
- この実機ログでは
  `BOKO_AUTO_FREEZE`
  は出ていない
- heartbeat
  は
  `seq=12984`
  まで継続し、
  最後は
  `sys=00000001`
  の ESC
  入力を検出した
- ESC
  後は
  `[TRACE_SD_END] ok=1 path=0:/traces/BOKO_0006.BIN bytes=524288 events=8256 fr=0`
  まで到達した
- ユーザー報告では
  「とりあえずとまらなくなりました」
  とのこと
- 結論として、
  `$4015`
  read
  を length counter status
  へ修正した
  `0.3.21`
  で、少なくとも今回の実機操作では BokosukaWars
  の停止現象は再現しなかった
- 補足として、
  長時間プレイでは 512KB
  trace
  が満杯になることも確認したため、
  今後 trace
  を継続利用する場合は、
  取得対象を絞るか、
  full
  後の挙動を目的に合わせて決める

## 1.13 `0.3.23` BokosukaWars debug trace を通常ビルドから無効化 (2026-04-25)

- system version を
  `0.3.23`
  へ更新した
- BokosukaWars
  の停止原因は
  `$4015`
  read
  修正後の実機確認で再現しなくなったため、
  正常品向けに debug trace
  と focused log
  を通常ビルドから外した
- `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  は通常の CMake option
  で
  `OFF`
  のままとした
- `drivers/boko_flash_trace.c`
  は
  `NESCO_BOKOSUKA_STATE_LOGS`
  が
  `ON`
  の場合だけ
  `PLATFORM_SOURCES`
  へ追加するようにした
- これにより通常ビルドでは
  BokosukaWars
  trace
  用の XIP flash erase / program
  は実行されない
- いったん
  `0.3.22`
  で build
  したが、
  `build/CMakeCache.txt`
  に
  `NESCO_BOKOSUKA_STATE_LOGS=ON`
  が残っていたため、
  ELF
  内に
  `boko_flash_trace`
  symbols
  と
  `[TRACE_*]`
  / `[BOKO_*]`
  strings
  が残っていることを確認した
- CMake cache
  を
  `NESCO_BOKOSUKA_STATE_LOGS=OFF`
  `NESCO_INPUT_IO_LOGS=OFF`
  `NESCO_RUNTIME_LOGS=OFF`
  で再 configure
  してから rebuild
  した
- `0.3.23`
  build
  では
  `nm -C build/Picocalc_NESco.elf`
  で
  `boko_flash_trace`
  symbol
  が出ないこと、
  `strings build/Picocalc_NESco.elf`
  で
  `[TRACE_*]`
  / `[BOKO_*]`
  文字列が出ないことを確認した
- build
  は
  `PicoCalc NESco Ver. 0.3.23 Build Apr 25 2026 12:21:59`
  で成功した
- build size
  は
  `text=265404`
  `data=0`
  `bss=160604`
  だった

## 1.14 `0.3.24` 通常ビルドから audio probe log を削除 (2026-04-25)

- system version を
  `0.3.24`
  へ更新した
- ユーザー提示ログ
  `pico20260425_122733.log`
  では、
  `0.3.23`
  の起動時に
  `[AUDIO_PROBE]`
  が残っていた
- `platform/audio.c`
  の
  `audio_init()`
  から、
  常時出力されていた
  `[AUDIO_PROBE]`
  ログを削除した
- `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  は
  `CMakeCache.txt`
  上ですべて
  `OFF`
  であることを確認した
- `strings`
  で
  `AUDIO_PROBE`
  `TRACE_`
  `BOKO_`
  `IN_SUM`
  `EVT`
  `[BOOT]`
  が出ないことを確認した
- `nm`
  で
  `boko_flash_trace`
  `nesco_boko`
  `psram_trace`
  の symbol
  が出ないことを確認した
- build
  は
  `PicoCalc NESco Ver. 0.3.24 Build Apr 25 2026 12:31:36`
  で成功した
- build size
  は
  `text=265188`
  `data=0`
  `bss=160604`
  だった

## 1.15 `0.3.25` ROM menu screenshot を追加 (2026-04-25)

- system version を
  `0.3.25`
  へ更新した
- ROM 選択画面でも
  `F5`
  で screenshot
  を保存できるようにした
- ROM menu screenshot
  の保存名は固定 stem
  `NESCO`
  を使い、
  `0:/screenshots/NESCO_0001.BMP`
  から空き番号を探す
- 既存のゲーム中 screenshot
  は従来通り ROM 名 stem
  を使う
- `platform/screenshot_storage.c`
  に
  `storage_build_screenshot_path_for_stem(...)`
  を追加し、
  既存の
  `storage_build_screenshot_path(...)`
  は ROM 名 stem
  を作って新 API
  へ委譲する形にした
- `platform/screenshot.c`
  では capture 本体を
  `screenshot_capture_and_save_with_stem(const char *stem_or_null, bool pause_audio)`
  へ分け、
  `stem_or_null == NULL`
  のときは従来保存名、
  `stem_or_null != NULL`
  のときは固定 stem
  保存名を使う
- ROM menu 用 API
  `nesco_take_screenshot_now_with_stem(const char *stem)`
  を追加した
- 即時 API
  は
  `s_screenshot_pending`
  または
  `s_screenshot_busy`
  が立っている場合に
  `NESCO_SCREENSHOT_BUSY`
  を返し、
  capture 中だけ
  `s_screenshot_busy`
  を立てる
- ゲーム中 screenshot
  は
  `pause_audio=true`
  のまま、
  ROM menu screenshot
  は
  `pause_audio=false`
  とし、
  ROM menu
  側で audio / display pacing
  の副作用を増やさないようにした
- ROM menu
  の help
  に
  `F5 : SAVE SCREENSHOT`
  を追加した
- help
  画面表示中に
  `F5`
  を押した場合は、
  help
  画面そのものを保存してから通常 menu
  へ戻り、
  status line
  に
  `SCREENSHOT SAVED`
  または
  `SCREENSHOT FAILED`
  を表示する
- README
  の ROM menu key
  に
  `F5 : screenshot`
  を追加した
- build
  は
  `PicoCalc NESco Ver. 0.3.25 Build Apr 25 2026 12:56:04`
  で成功した
- build size
  は
  `text=265860`
  `data=0`
  `bss=160604`
  だった
- `CMakeCache.txt`
  では
  `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  がすべて
  `OFF`
  であることを確認した

## 1.16 `0.3.26` ROM menu screenshot の help 遷移と保存中表示を修正 (2026-04-25)

- system version を
  `0.3.26`
  へ更新した
- ユーザー実機確認で、
  help
  画面中に
  `F5`
  screenshot
  を実行すると ROM 選択画面へ戻ることが報告された
- 同じ報告で、
  ROM 選択画面では screenshot
  保存中の停止状態をユーザーが検知しにくいことも指摘された
- `F5`
  を help
  画面の遷移操作から外し、
  help
  画面中に
  `F5`
  を押しても help
  画面へ留まるようにした
- ROM menu / help
  のどちらでも、
  capture
  前に画面へ
  `SAVING SCREENSHOT...`
  を表示してから保存処理へ入るようにした
- 保存完了後は同じ画面種別のまま
  `SCREENSHOT SAVED`
  または
  `SCREENSHOT FAILED`
  を表示する
- help
  画面は status text
  を footer
  に表示できるよう
  `menu_render_help(...)`
  を拡張した
- `H/?`
  で help
  を開閉した場合や、
  help
  内で左右ページ移動した場合は古い status
  を引きずらないようにした
- build
  は
  `PicoCalc NESco Ver. 0.3.26 Build Apr 25 2026 13:05:17`
  で成功した
- build size
  は
  `text=265932`
  `data=0`
  `bss=160604`
  だった
- `CMakeCache.txt`
  では
  `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  がすべて
  `OFF`
  であることを確認した

## 1.17 `0.3.27` ROM menu screenshot を音で通知するよう修正 (2026-04-25)

- system version を
  `0.3.27`
  へ更新した
- `0.3.26`
  は capture
  前に
  `SAVING SCREENSHOT...`
  を表示していたため、
  その文字が screenshot
  に写り込む問題があった
- ROM menu / help
  の screenshot
  では、capture
  前に画面へ文字を描かないように戻した
- ROM menu / help
  の screenshot
  通知として、
  audio ring
  に短い UI tone
  を積む
  `audio_play_ui_tone(...)`
  と
  `audio_play_ui_silence(...)`
  を追加した
- `F5`
  押下時は短い高音、
  capture
  中は短い低めの音を 4 回、
  capture
  完了時は成功なら低めの短音、
  失敗ならさらに低い長めの音を鳴らす
- capture
  前後に
  `menu_discard_pending_keys()`
  を呼び、
  保存中に入った key event
  が後続の menu 操作として残りにくいようにした
- help
  画面中の
  `F5`
  は引き続き help
  画面に留まる
- build
  は
  `PicoCalc NESco Ver. 0.3.27 Build Apr 25 2026 13:12:40`
  で成功した
- build size
  は
  `text=266908`
  `data=0`
  `bss=160604`
  だった
- `strings build/Picocalc_NESco.elf`
  で
  `SAVING SCREENSHOT`
  が出ないことを確認した
- `CMakeCache.txt`
  では
  `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  がすべて
  `OFF`
  であることを確認した

## 1.18 `0.3.28` 保存中通知音を DMA 自走式に変更 (2026-04-25)

- system version を
  `0.3.28`
  へ更新した
- `0.3.27`
  の保存中通知は audio ring
  に短い tone
  を先詰めしていたため、
  早く鳴り切った後に保存完了まで無音になることが実機で報告された
- 保存中の通知音を
  `drivers/pwm_audio.c`
  の DMA refill
  側で生成する方式へ変更した
- `audio_start_ui_busy_indicator()`
  で busy indicator
  を開始し、
  `audio_stop_ui_busy_indicator()`
  で停止する
- busy indicator
  は CPU
  が screenshot 保存処理で塞がっていても、
  DMA IRQ
  が動く限り約 500ms
  ごとに短い tone
  を生成する
- `F5`
  押下時の短い高音と保存終了音は従来通り audio ring
  へ積む
- build
  は
  `PicoCalc NESco Ver. 0.3.28 Build Apr 25 2026 13:19:30`
  で成功した
- build size
  は
  `text=268284`
  `data=0`
  `bss=160608`
  だった
- `strings build/Picocalc_NESco.elf`
  で
  `SAVING SCREENSHOT`
  が出ないことを確認した
- `CMakeCache.txt`
  では
  `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  がすべて
  `OFF`
  であることを確認した

## 1.19 `0.4.0` ROM menu screenshot 対応を release 区切りへ昇格 (2026-04-25)

- system version を
  `0.4.0`
  へ更新した
- `0.3.25`
  から
  `0.3.28`
  までで実装した
  ROM menu / help
  screenshot 対応を、
  実機確認で良好と判断できたため
  `0.4.0`
  の区切りにした
- ROM menu / help
  では
  `F5`
  で
  `NESCO_####`
  prefix
  の screenshot
  を保存する
- 保存中の画面には通知文字を描画せず、
  screenshot
  画像へ通知文字が写り込まない方針を維持する
- ROM menu / help
  の保存中通知は、
  画面表示ではなく audio cue
  で行う
- 保存中は DMA refill
  側の busy indicator
  により、
  約 500ms
  ごとに短い tone
  を鳴らす
- build
  は
  `PicoCalc NESco Ver. 0.4.0 Build Apr 25 2026 13:31:54`
  で成功した
- build size
  は
  `text=268284`
  `data=0`
  `bss=160608`
  だった
- `strings build/Picocalc_NESco.elf`
  で
  `SAVING SCREENSHOT`
  が出ないことを確認した
- `CMakeCache.txt`
  では
  `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  がすべて
  `OFF`
  であることを確認した

## 1.20 `0.4.1` ROM menu 選択行の下側欠けを修正 (2026-04-25)

- system version を
  `0.4.1`
  へ更新した
- ROM menu
  の選択行で、
  青い highlight
  の下側が左右だけ黒く欠けて見える問題を修正した
- 原因は、
  選択行の青い矩形が
  `22px`
  高で描画されている一方、
  detail
  行の text span
  がそれより下まで背景色を描くため、
  中央だけ青背景が下へ伸びて左右が黒く残ることだった
- 選択行の青い矩形を
  `25px`
  高へ広げ、
  label
  と detail
  の背景範囲を覆うようにした
- build
  は
  `PicoCalc NESco Ver. 0.4.1 Build Apr 25 2026 13:37:38`
  で成功した
- build size
  は
  `text=268284`
  `data=0`
  `bss=160608`
  だった
- `CMakeCache.txt`
  では
  `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  がすべて
  `OFF`
  であることを確認した

## 1.21 README の host build 方針を PicoCalc 専用として明確化 (2026-04-25)

- README
  の現在 version
  を
  `0.4.1`
  へ更新した
- この folder
  は PicoCalc 専用 firmware
  を対象にしているため、
  PicoCalc
  向け以外の host build
  は未検証として明示的に無効化している、と README
  に記載した
- `infones`
  側にある他環境向け build
  は、
  この folder
  では責任範囲外として扱う方針を README
  に記載した
- `docs/project/PROGRESS_TODO.md`
  の保留項目から
  host build
  再開を外した

## 1.22 `0.4.2` ROM 読み込み中の `Loading...` 表示を復旧 (2026-04-25)

- system version を
  `0.4.2`
  へ更新した
- 大きな ROM
  の読み込み中に
  `Loading...`
  が見えなくなっていた問題を修正した
- 原因は、
  `display_show_loading_screen()`
  の直後に
  `display_set_mode(DISPLAY_MODE_NES_VIEW)`
  を呼んでおり、
  NES view
  準備処理が全画面を黒で塗り直して
  `Loading...`
  を消していたことだった
- `InfoNES_Load(selected_path)`
  を
  `Loading...`
  表示後に実行し、
  ROM load
  完了後に
  `DISPLAY_MODE_NES_VIEW`
  へ戻す順序へ変更した
- README
  の現在 version
  も
  `0.4.2`
  へ更新した
- build
  は
  `PicoCalc NESco Ver. 0.4.2 Build Apr 25 2026 14:05:07`
  で成功した
- build size
  は
  `text=268292`
  `data=0`
  `bss=160608`
  だった
- `strings build/Picocalc_NESco.elf`
  で
  `Loading...`
  が入っていることを確認した
- `CMakeCache.txt`
  では
  `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  がすべて
  `OFF`
  であることを確認した

## 1.23 `0.4.3` flash staging 直後起動の安定化 (2026-04-25)

- system version を
  `0.4.3`
  へ更新した
- ユーザー観察として、
  大きな
  `*.nes`
  を初回に flash
  へ staging
  した直後の起動でハングする場合があり、
  `ESC`
  では ROM menu
  に戻れ、
  2 回目は正常に起動することが報告された
- 初回起動と 2 回目の差分は、
  初回が
  `sd:/...`
  から staging
  した直後に
  `s_flash_rom`
  を読み始める経路で、
  2 回目は menu
  の
  `flash:/...`
  entry
  から最初から flash backend
  として読む経路である
- flash staging
  の順序を、
  metadata 先行から、
  ROM 本体 program →
  file / flash verify →
  metadata 確定
  へ変更した
- staging
  完了後に
  `flash_flush_cache()`
  を明示的に呼び、
  staging 直後に XIP
  から読む状態を安定化させる
- README
  の現在 version
  も
  `0.4.3`
  へ更新した
- build
  は
  `PicoCalc NESco Ver. 0.4.3 Build Apr 25 2026 14:15:19`
  で成功した
- build size
  は
  `text=268388`
  `data=0`
  `bss=160608`
  だった
- `CMakeCache.txt`
  では
  `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  がすべて
  `OFF`
  であることを確認した

## 1.24 `0.4.4` screenshot chunk buffer を動的確保へ変更 (2026-04-26)

- system version を
  `0.4.4`
  へ更新した
- Mapper30
  起動時の heap
  余裕を増やすため、
  screenshot
  用 chunk buffer
  を静的確保から動的確保へ変更した
- 対象は
  `platform/screenshot.c`
  の
  `s_chunk_pixels`
  と
  `s_chunk_bmp_buf`
  で、
  screenshot
  capture
  開始時に
  `malloc()`
  し、
  終了時または error return
  前に
  `free()`
  する
- chunk buffer
  が確保できない場合は
  `NESCO_SCREENSHOT_NO_MEMORY`
  を返す
- build
  は
  `PicoCalc NESco Ver. 0.4.4 Build Apr 26 2026 07:42:14`
  で成功した
- build size
  は
  `text=268476`
  `data=0`
  `bss=135008`
  だった
- linker symbol
  から確認した heap gap
  は
  `__end__=0x2002bce0`
  `__HeapLimit=0x20040000`
  で、
  `82720`
  bytes
  約
  `80.8 KiB`
  になった
- 変更前の
  `0.4.3`
  では heap gap
  が
  `57120`
  bytes
  約
  `55.8 KiB`
  だったため、
  常時 heap
  余裕は
  `25600`
  bytes
  増えた
- README
  の現在 version
  も
  `0.4.4`
  へ更新した
- `CMakeCache.txt`
  では
  `NESCO_RUNTIME_LOGS`
  `NESCO_INPUT_IO_LOGS`
  `NESCO_BOKOSUKA_STATE_LOGS`
  がすべて
  `OFF`
  であることを確認した

## 1.25 `0.4.5` text drawing buffer の常時 RAM 消費を削減 (2026-04-26)

- system version を
  `0.4.5`
  へ更新した
- Mapper30
  起動用 heap
  余裕をさらに増やすため、
  text drawing
  用の常時 RAM
  消費を削減した
- `platform/display.c`
  の
  `display_draw_text_span_scaled()`
  と
  `display_draw_text_span_scaled_cropped()`
  から、
  それぞれ
  `14080`
  bytes
  の静的
  `pixels`
  buffer
  を削除した
- `Loading...`
  `Shift+W Stretch Screen`
  `Shift+W Normal Screen`
  などの固定文言表示は、
  320 pixel
  幅の行 buffer
  を stack
  上で使って 1 行ずつ LCD
  へ転送するようにした
- `platform/rom_menu.c`
  の ROM menu
  用 text buffer
  は静的配列をやめ、
  ROM menu
  入場時に
  `malloc()`
  し、
  ROM 起動で menu
  を抜ける直前に
  `free()`
  するようにした
- build
  は
  `PicoCalc NESco Ver. 0.4.5 Build Apr 26 2026 08:41:10`
  で成功した
- build size
  は
  `text=266220`
  `data=0`
  `bss=92772`
  だった
- linker symbol
  から確認した heap gap
  は
  `__end__=0x200217e4`
  `__HeapLimit=0x20040000`
  で、
  `124956`
  bytes
  約
  `122.0 KiB`
  になった
- 変更前の
  `0.4.4`
  では heap gap
  が
  `82720`
  bytes
  約
  `80.8 KiB`
  だったため、
  常時 heap
  余裕は
  `42236`
  bytes
  増えた
- `arm-none-eabi-nm`
  で、
  旧
  `pixels`
  静的 buffer
  が残っていないこと、
  ROM menu
  側は
  `s_menu_text_pixels`
  pointer
  のみになっていることを確認した
- README
  の現在 version
  も
  `0.4.5`
  へ更新した

## 1.26 README 画像追加と公開向け文言整理、TODO 圧縮 (2026-04-26)

- README
  に ROM menu
  と Mapper30
  動作例の画像を追加した
- README
  掲載用の正式画像は
  `docs/images/rom_menu.png`
  `docs/images/mapper30_tower_normal.png`
  `docs/images/mapper30_tower_stretch.png`
  とした
- 候補画像置き場として
  `docs/images/readme_candidates/`
  を作成し、
  候補 PNG
  は
  `.gitignore`
  対象にした
- README
  には ROM
  ファイルを同梱しないこと、
  利用者自身が合法的に用意した ROM
  を使うことを明記した
- README
  の
  `save persistence`
  や
  `persist`
  表現を、
  `セーブデータ保存 / 復元`
  や
  `*.m30 保存 / 復元`
  へ置き換えた
- README
  の Mapper30
  説明は、
  ROM 起動と表示は実機確認済み、
  `*.m30`
  の実ゲームでの書き込み / 復元確認は未完、
  という粒度に整理した
- ユーザー実機確認として、
  `0.4.5`
  で Mapper30
  のゲーム中 screenshot
  が撮れることを確認した
- README
  から
  `公開前`
  `責任範囲`
  `folder`
  など、公開後の読者に誤解を与えやすい表現を外した
- `docs/project/PROGRESS_TODO.md`
  の
  `現在の TODO`
  から完了済みの長い履歴を外し、
  未着手 / 進行中 / 保留項目だけを残す形へ圧縮した

## 1.27 ルート文書を役割別に分離 (2026-04-26)

- 作業時の入口として
  `AGENTS.md`
  を追加した
- 現在タスクの正本として
  `docs/project/TASKS.md`
  を追加し、
  旧
  `PROGRESS_TODO.md`
  の現在タスクを移した
- 構成と責務境界の正本として
  `docs/project/ARCHITECTURE.md`
  を追加し、
  旧
  `PROGRESS_TODO.md`
  の現在の目標、注意点、互換性確認基準を移した
- 作業規約の正本として
  `docs/project/CONVENTIONS.md`
  を追加し、
  build、version、実装前後確認、TODO / HISTORY
  運用を移した
- 計画書索引として
  `docs/project/PLANS.md`
  を追加し、
  既存の
  `docs/design/`
  と今後作る release gate
  系計画への入口をまとめた
- `PROGRESS_TODO.md`
  は互換性維持の入口リンク集へ縮小した
- README
  の関連文書一覧を、
  新しい文書分担へ合わせて更新した

## 1.28 運用文書を `docs/project/` へ移動 (2026-04-26)

- 直下に置く文書を
  `README.md`
  と
  `AGENTS.md`
  に絞った
- `TASKS.md`
  `ARCHITECTURE.md`
  `CONVENTIONS.md`
  `PLANS.md`
  `PROGRESS_TODO.md`
  `Picocalc_NESco_HISTORY.md`
  を
  `docs/project/`
  へ移動した
- README
  と
  AGENTS
  からの参照を
  `docs/project/...`
  に更新した
- `PROGRESS_TODO.md`
  は
  `docs/project/PROGRESS_TODO.md`
  として互換用の入口リンク集を維持した

## 1.29 README と実装状態の最終照合計画を追加 (2026-04-26)

- 公開前に README
  の機能記述と実装状態を照合する計画として
  `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_PLAN_20260426.md`
  を追加した
- 照合対象を
  README、
  `platform/`、
  `infones/`、
  `drivers/`、
  `CMakeLists.txt`
  に固定した
- README
  の各主張を、
  実装根拠、
  build 根拠、
  実機確認根拠、
  未確認注記に分けて確認する方針にした
- 実機確認は原則 1 回の smoke test
  に集約し、
  Mapper30
  の
  `*.m30`
  保存 / 復元は別計画扱いにした
- `docs/project/PLANS.md`
  と
  `docs/project/TASKS.md`
  からこの計画へ辿れるようにした

## 1.30 README 最終照合計画の検証粒度を具体化 (2026-04-26)

- `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_PLAN_20260426.md`
  の成果物に、
  照合表の必須列を追加した
- README
  の各主張について、
  実装根拠、
  build 根拠、
  最新 build
  での実機確認根拠、
  過去 HISTORY
  根拠、
  README 修正要否を分けて記録する形にした
- help 画面での
  `F5`
  screenshot、
  意図しない画面遷移が起きないこと、
  保存中 key event
  が保存後に残らないことを
  smoke test
  に追加した
- `実機確認済み`
  と README
  に書く項目は最新 build
  の実機 smoke
  を第一根拠にし、
  HISTORY
  のみを根拠にする項目は
  `history confirmed`
  として分ける方針にした
- ROM 非同梱、
  画像の意図、
  `persist`
  などの表現、
  公開文書として伝わりにくい英語混じり表現も、
  最終照合で確認する観点に追加した

## 1.31 README 最終照合計画の具体物確認を追加 (2026-04-26)

- `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_PLAN_20260426.md`
  に、
  ROM / save 系 file
  が意図せず tracked
  になっていないか確認する
  `git ls-files`
  手順を追加した
- README
  が参照する正式画像として
  `docs/images/rom_menu.png`
  `docs/images/mapper30_tower_normal.png`
  `docs/images/mapper30_tower_stretch.png`
  の存在確認を追加した
- `docs/images/readme_candidates/`
  は候補置き場であり、
  公開 README
  が参照する正式画像ではないことを計画へ明記した
- 最新 build
  で README
  の
  `DragonQuest3`
  `*.srm`
  実機確認済み表現を維持する場合、
  smoke test
  の代表 ROM
  を
  `DragonQuest3`
  に固定する方針にした
- ROM menu / help / game
  の screenshot
  は、
  `F5`
  操作だけでなく
  `0:/screenshots/*.BMP`
  の作成まで確認する方針にした

## 1.32 README 最終照合計画の検査時識別子を補強 (2026-04-26)

- `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_PLAN_20260426.md`
  の ROM / save
  系 tracked file
  チェックを、
  大文字拡張子も拾う
  `rg -in`
  に変更した
- `DragonQuest3`
  で
  `*.srm`
  save / restore
  を再確認する場合は、
  照合結果へ
  `ROM display name`
  `SD path`
  `SHA-256`
  `header mapper`
  `battery SRAM 有無`
  を記録する方針にした
- `docs/images/readme_candidates/.gitkeep`
  は候補置き場維持用の例外として
  tracked
  を許可し、
  それ以外の候補画像は tracked
  しない方針を明記した

## 1.33 README 最終照合計画の build / 同梱確認を補強 (2026-04-26)

- `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_PLAN_20260426.md`
  の build
  手順に、
  `build/`
  がない場合または
  CMake cache
  が現行環境と合わない場合は
  `cmake -S . -B build`
  を先に実行する条件を追加した
- CMakeLists.txt
  や target
  構成に差分がある場合も、
  build graph
  を再生成してから build
  する方針にした
- ROM / save
  系 tracked file
  チェックに
  `zip`
  `7z`
  `rar`
  を追加した
- 圧縮 file
  が一致した場合は、
  file 名だけで判断せず、
  中身に ROM / save
  系 file
  が含まれていないか確認する方針にした

## 1.34 README と実装状態の最終照合を完了 (2026-04-26)

- `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_20260426.md`
  を作成し、
  README
  の機能記述と実装 / build / 実機状態を照合した
- build
  は
  `cmake --build build -j4`
  で成功した
- build
  生成物は
  `build/Picocalc_NESco.uf2`
  と
  `build/Picocalc_NESco.elf`
  を確認した
- build banner
  は
  `PicoCalc NESco Ver. 0.4.5 Build Apr 26 2026 08:41:10`
  だった
- build size
  は
  `text=266220`
  `data=0`
  `bss=92772`
  だった
- tracked file
  に
  ROM / save / compressed ROM
  系 file
  が含まれていないことを確認した
- README
  掲載画像
  `docs/images/rom_menu.png`
  `docs/images/mapper30_tower_normal.png`
  `docs/images/mapper30_tower_stretch.png`
  が存在することを確認した
- 実機 smoke
  で、
  起動 banner、
  ROM menu、
  SD ROM list、
  `SYSTEM FLASH`
  entry、
  help、
  ROM menu / help screenshot、
  screenshot
  後の意図しない画面遷移なし、
  保存中 key event
  の残留なしを確認した
- `DragonQuest3`
  で、
  boot、
  input、
  audio、
  normal view、
  stretch view、
  normal view
  復帰、
  game screenshot、
  `ESC`
  による ROM menu
  復帰を確認した
- `DragonQuest3`
  の
  `*.srm`
  作成 / 更新と restore
  を確認した
- `DragonQuest3`
  確認 ROM
  の iNES header
  は
  `4e 45 53 1a 10 00 12 00 00 00 00 00 00 00 00 00`
  で、
  mapper
  は
  `1`、
  battery SRAM
  は
  `true`
  と確認した
- SRAM save file
  は
  `8192`
  bytes
  だった
- 実機 screenshot
  として
  `NESCO_0004.BMP`
  から
  `NESCO_0007.BMP`
  および
  `DragonQuest3_0001.BMP`
  から
  `DragonQuest3_0007.BMP`
  の copied evidence
  を確認した
- 静的照合、build 照合、実機 smoke
  の範囲では、
  README
  を修正すべき矛盾は確認しなかった
- 完了したため、
  `docs/project/TASKS.md`
  から最新 main
  smoke test
  と README
  最終照合の項目を外した

## 1.35 version 運用ルールを明文化 (2026-04-26)

- `docs/project/CONVENTIONS.md`
  の
  `version`
  節を更新し、
  version
  を
  `MAJOR.MINOR.PATCH`
  として扱う方針を明文化した
- `MAJOR`
  は大きな構成変更や互換性に影響する変更で上げる
  - 例:
    emulator core
    を
    `infones`
    以外へ置き換える
  - 例:
    save / ROM / firmware
    の扱いが従来と大きく変わる
- `MINOR`
  は既存の基本動作を保ったまま機能追加したときに上げる
- `PATCH`
  は既存機能の bug fix、調整、文書修正で上げる
- `1.0.0`
  は公開用の最初の基準版として扱うことにした
- `0.4.5`
  で通した実機 smoke
  は AS-IS
  の確認根拠として採用し、
  `1.0.0`
  へ version
  を変えるだけの場合は、
  同じ実機 smoke
  の再実施を必須にしない方針にした
- release version
  更新時は、
  `platform/version.h`
  更新、
  build、
  生成物からの version / build id
  確認、
  README / HISTORY / TASKS
  更新、
  commit、
  必要時 tag
  の順に進めることを固定した
- 完了したため、
  `docs/project/TASKS.md`
  から
  バージョン運用ルール明文化の項目を外した

## 1.36 公開準備方針計画を追加 (2026-04-26)

- `docs/release/PUBLIC_RELEASE_PREP_PLAN_20260426.md`
  を追加し、
  公開前に実施する作業と未確認事項として残す作業を分けた
- clean build / release artifact
  手順は別途手順書を作る方針とし、
  GitHub Release
  には
  `Picocalc_NESco-1.0.0.uf2`
  と source
  archive
  を置く想定にした
- 最新
  `main`
  の最終 code review
  は、実機確認ではなく release blocker
  を探す code review
  として実施する方針にした
- 公開向け comment
  整理は、
  `display.h`
  / `display.c`
  の viewport / stretch / buffer
  周りを中心に行う方針にした
- `Map6`
  `Map19`
  `Map185`
  `Map188`
  `Map235`
  の実機確認と、
  Mapper30
  の
  `*.m30`
  保存 / 復元は、
  `1.0.0`
  公開前の blocker
  ではなく未確認事項として残す方針にした
- `core/`
  は残し、
  現在の active build
  対象ではない旧系統として扱う方針にした
- `core/`
  については、
  MIT license
  の独自 emulator core
  を目指したが、
  `infones`
  仕様書に固有名詞などが残っていたことで clean-room
  性に疑義が出たため discontinued
  扱いにした経緯を HISTORY
  と必要に応じて
  `core/README.md`
  に残す方針にした
- `platform/*.c`
  は今回
  `.cpp`
  へ改名せず、
  CMake
  の
  `LANGUAGE CXX`
  による C++ build
  運用で固定する方針にした
- review
  を受けて、
  `core/README.md`
  は公開前に追加する必須作業として統一した
- source archive
  は GitHub
  の tag
  由来 archive
  を基本とし、
  手動添付する場合は
  `git archive`
  由来に限定して未追跡 file
  を含めない方針にした
- 手動 source archive
  では
  `build/`
  ROM / save
  系 file、
  圧縮 ROM / save
  候補、
  local log / copied evidence、
  README
  候補画像を混入させない方針にした
- 公開準備の実行順は、
  `core/`
  と
  `platform/*.c`
  の文書化、
  公開向け comment
  整理、
  final code review、
  release artifact
  手順書、
  最終 build / artifact
  作成の順へ整理した
- `docs/project/PLANS.md`
  では README
  最終照合を完了済み計画 / 結果へ移し、
  現在必要な計画から外した

## 1.37 公開準備方針計画に従って文書化と確認を実施 (2026-04-26)

- `core/README.md`
  を追加し、
  `core/`
  が現在の build
  対象ではないこと、
  active source tree
  ではないこと、
  clean-room emulator core
  として再利用する場合は別途確認が必要であることを明記した
- `docs/project/ARCHITECTURE.md`
  の
  `core/`
  項目を更新し、
  `core/`
  は経緯確認用として残す旧系統であることを明記した
- `docs/project/ARCHITECTURE.md`
  と
  `docs/project/CONVENTIONS.md`
  に、
  一部の
  `platform/*.c`
  は CMake
  の
  `LANGUAGE CXX`
  指定により C++ として build
  する運用で固定することを明記した
- `platform/display.h`
  と
  `platform/display.c`
  の viewport / normal view / stretch view / strip transfer
  周りの comment
  を現状に合わせて整理した
- `platform/main.c`
  と
  `platform/rom_image.c`
  の冒頭 comment
  も、現在の ROM menu loop
  と ROM loading strategy
  に合わせて修正した
- `docs/release/RELEASE_ARTIFACT_PROCEDURE.md`
  を追加し、
  clean build、
  UF2
  生成確認、
  version / build id
  確認、
  SHA-256
  記録、
  GitHub Release
  用 artifact
  名、
  source archive
  の扱い、
  ROM / save
  系 file
  の混入確認を固定した
- `docs/release/FINAL_CODE_REVIEW_20260426.md`
  を追加し、
  latest
  `main`
  の初期化順、
  menu / game
  遷移、
  save / load、
  screenshot、
  mapper release、
  hot path、
  RAM
  使用量の観点で review
  した
- final code review
  では release blocker
  は確認しなかった
- build
  は
  `cmake --build build -j4`
  で成功した
- build banner
  は
  `PicoCalc NESco Ver. 0.4.5 Build Apr 26 2026 12:45:49`
  だった
- build size
  は
  `text=266220`
  `data=0`
  `bss=92772`
  だった
- `build/Picocalc_NESco.uf2`
  の SHA-256
  は
  `44744f0e29e6c1ab8b1aa2958e1a443ac1c2e00f3dbb6d861d544fdf3f5eae07`
  だった
- `build/Picocalc_NESco.elf`
  の SHA-256
  は
  `e4071f4e1138a4f42d41ca741699b1cc27f7c3a0ce59b7665ad5577c480d48a8`
  だった
- tracked file
  に ROM / save / compressed ROM
  候補が含まれていないことを確認した
- 完了したため、
  `docs/project/TASKS.md`
  から clean build / release artifact
  手順、
  最新
  `main`
  最終 code review、
  公開向け comment
  整理、
  `core/`
  扱い決定、
  `platform/*.c`
  の C++ build
  運用決定を外した

## 1.38 公開準備結果文書の review 指摘を反映 (2026-04-26)

- `docs/release/RELEASE_ARTIFACT_PROCEDURE.md`
  と
  `docs/release/RELEASE_BUILD_CHECK_20260426.md`
  の ROM / save
  混入チェック command
  を修正した
- 修正後の command
  は
  `git ls-files | rg -in '\.(nes|fds|srm|m30|zip|7z|rar)$' || true`
  とした
- 修正後の command
  で tracked ROM / save / compressed ROM
  候補がないことを確認した
- test input
  で
  `.nes`
  `.srm`
  `.zip`
  が拾われることも確認した
- `core/README.md`
  から HISTORY
  への参照を、
  `core/`
  から見た相対 path
  `../docs/project/Picocalc_NESco_HISTORY.md`
  に修正した
- `docs/project/PLANS.md`
  では、
  公開準備方針を現在必要な計画から外し、
  完了済み計画 / 結果へ移した

## 1.39 `1.0.0` release gate を固定 (2026-04-26)

- `docs/release/RELEASE_GATE_1_0_0.md`
  を追加し、
  `1.0.0`
  公開判定用の release gate
  を 1 箇所に固定した
- `0.4.5`
  の README
  最終照合 smoke
  を AS-IS
  の確認根拠として採用する範囲を明記した
- AS-IS
  根拠には、
  PicoCalc
  起動、
  ROM menu、
  SD ROM
  一覧、
  `SYSTEM FLASH`
  entry、
  help、
  ROM menu / help / game
  screenshot、
  stretch
  切替、
  `DragonQuest3`
  の起動 / 入力 / 音 / save / restore / menu
  戻りを含めた
- `1.0.0`
  では、
  version / README / HISTORY / TASKS / PLANS、
  build、
  artifact、
  SHA-256、
  ROM / save
  混入チェック、
  git
  状態、
  tag / artifact
  対応を中心に確認する方針を固定した
- `Map6`
  `Map19`
  `Map185`
  `Map188`
  `Map235`
  の実機確認、
  Mapper30
  の
  `*.m30`
  保存 / 復元、
  Mapper87 / Choplifter
  系の追加確認は未確認事項として残し、
  `1.0.0`
  公開前 blocker
  にはしないことを明記した
- 完了したため、
  `docs/project/TASKS.md`
  から
  `1.0.0`
  公開判定用 release gate
  作成項目を外した
- `docs/project/PLANS.md`
  では、
  release gate
  を現在必要な計画から外し、
  完了済み計画 / 結果へ移した

## 1.40 `infones/linux/Makefile` を公開対象に含める (2026-04-26)

- `infones/linux/Makefile`
  がローカルには存在するが、
  `.gitignore`
  の汎用
  `Makefile`
  rule
  により ignored untracked
  になっていることを確認した
- `infones/linux/Makefile`
  は CMake
  生成物ではなく、
  InfoNES
  Linux
  版の build script
  であることを確認した
- PicoCalc_NESco
  として Linux
  build
  を support
  するという意味ではなく、
  `infones/`
  由来資産の保持と公開物の完全性のため、
  `infones/linux/Makefile`
  は tracked
  にする方針にした
- `.gitignore`
  に
  `!infones/linux/Makefile`
  を追加し、
  `infones/linux/Makefile`
  を公開対象に戻した

## 1.41 `1.0.0` 公開直前 build / artifact を作成 (2026-04-26)

- `platform/version.h`
  の
  `PICOCALC_NESCO_VERSION`
  を
  `1.0.0`
  に更新した
- `README.md`
  の現在埋め込み version
  記述を
  `1.0.0`
  に更新した
- build
  は
  `cmake --build build -j4`
  で成功した
- build banner
  は
  `PicoCalc NESco Ver. 1.0.0 Build Apr 26 2026 13:14:29`
  だった
- build size
  は
  `text=266220`
  `data=0`
  `bss=92772`
  だった
- `build/Picocalc_NESco.uf2`
  の SHA-256
  は
  `50f6b2aafeadcdf473c45d1515345bea6b98ba3668a659350aa92362bd41fd1e`
  だった
- release
  添付用に
  `Picocalc_NESco-1.0.0.uf2`
  を作成した
- `Picocalc_NESco-1.0.0.uf2`
  の SHA-256
  は
  `50f6b2aafeadcdf473c45d1515345bea6b98ba3668a659350aa92362bd41fd1e`
  だった
- `build/Picocalc_NESco.elf`
  の SHA-256
  は
  `689465028f22e18803e4eff916f6f7b38a7d8d4ebb45d32d68f12376fefd0270`
  だった
- tracked file
  に ROM / save / compressed ROM
  候補が含まれていないことを確認した
- README
  用正式画像は
  `docs/images/rom_menu.png`
  `docs/images/mapper30_tower_normal.png`
  `docs/images/mapper30_tower_stretch.png`
  であり、
  `docs/images/readme_candidates/.gitkeep`
  以外の候補画像が tracked
  になっていないことを確認した
- `docs/release/RELEASE_BUILD_CHECK_1_0_0.md`
  を追加し、
  `1.0.0`
  build / artifact
  結果を記録した

## 1.42 公開前に local evidence details を削除 (2026-04-26)

- 公開前確認として、
  `docs/`
  と project
  文書から local path、
  copied evidence
  path、
  手持ち ROM / save
  の SHA-256
  が残っていないか確認した
- `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_20260426.md`
  から local log / copied ROM / copied save
  path
  と ROM / save
  SHA-256
  を削除し、
  local evidence
  であることだけが分かる表現へ変更した
- `docs/release/RELEASE_GATE_1_0_0.md`
  から確認 ROM
  の SHA-256
  を削除した
- `docs/project/Picocalc_NESco_HISTORY.md`
  に残っていた local absolute path
  と ROM / save
  SHA-256
  を削除した
- 検索対象
  `docs`
  `README.md`
  `platform`
  `LICENSE`
  `AGENTS.md`
  で、
  local path、
  credential-like
  な文字列、
  手持ち ROM / save
  SHA-256
  が残っていないことを確認した
- この変更は文書のみで、
  firmware
  source
  は変更していないため、
  `1.0.0`
  UF2
  artifact
  の SHA-256
  は
  `50f6b2aafeadcdf473c45d1515345bea6b98ba3668a659350aa92362bd41fd1e`
  のままと確認した

## 1.43 GitHub Actions 最小 build CI を導入 (2026-04-26)

- `docs/design/GITHUB_ACTIONS_BUILD_CI_PLAN_20260426.md`
  を追加し、
  GitHub Actions
  による最小 build CI
  の設計を固定した
- `.github/workflows/build.yml`
  を追加した
- workflow
  は
  `push`
  `pull_request`
  `workflow_dispatch`
  で起動する
- `pico-sdk`
  は
  `raspberrypi/pico-sdk`
  の
  `2.2.0`
  を
  `actions/checkout@v6`
  で取得し、
  submodule
  は
  recursive
  に取得する
- configure
  は
  `cmake -S . -B build -G Ninja -DPICO_SDK_PATH="$GITHUB_WORKSPACE/pico-sdk"`
  を使う
- build
  は
  `cmake --build build -j4`
  を使う
- CI
  では
  `build/Picocalc_NESco.elf`
  と
  `build/Picocalc_NESco.uf2`
  の生成を確認し、
  artifact
  `picocalc-nesco-firmware`
  として保存する
- `actions/upload-artifact@v7`
  で
  UF2 / ELF
  のみを artifact
  として保存する
- banner
  確認は
  `PicoCalc NESco Ver.`
  prefix
  の存在確認に限定し、
  version 値そのものは固定しない
- CI
  は build
  自動確認のみを目的とし、
  実機 smoke、
  release 作成、
  tag 発行、
  release publish
  は対象外とした
- `README.md`
  の build
  節へ、
  GitHub Actions
  は clean configure / build
  と生成物確認のみで、
  実機確認を含まないことを追記した
- `docs/project/TASKS.md`
  から CI 導入 task
  を外し、
  `docs/project/PLANS.md`
  へ完了済み計画として移した

## 1.44 `1.0.1` ROM menu 選択移動の再描画範囲を縮小 (2026-04-26)

- system version を
  `1.0.1`
  へ更新した
- ROM menu
  で上下キーを押すたびに
  `menu_render()`
  が全画面を消去して再描画していた
- 選択行の移動で scroll
  が発生しない場合は、
  旧選択行、
  新選択行、
  index、
  status、
  debug code
  だけを再描画するようにした
- scroll
  位置が変わる場合、
  directory
  移動、
  help、
  screenshot
  後などは従来どおり full render
  を使う
- 目的は ROM menu
  のカーソル移動時のチラつき低減

## 1.45 `1.0.2` ROM menu key release 時の full render を抑制 (2026-04-26)

- system version を
  `1.0.2`
  へ更新した
- `1.0.1`
  では上下キーの
  press
  時は部分再描画になったが、
  key release
  イベントが最後の共通
  `menu_render()`
  に落ち、
  ボタンを離した後に full render
  が走っていた
- `KEY_STATE_PRESSED`
  以外のイベントでは full render
  を行わず、
  通常 menu
  では右下 debug code
  のみ更新して抜けるようにした
- 実機検証で ROM menu
  の上下移動時のチラつき低減を確認した

## 1.46 `1.0.4` ROM menu 上下端移動をページ送り型に変更 (2026-04-27)

- system version を
  `1.0.4`
  へ更新した
- ROM menu
  の選択カーソルが表示中 list
  の最下行にある状態で下矢印を押したとき、
  1 行ずつ scroll
  するのではなく、
  次の項目を新しい表示ページの先頭に出すようにした
- 表示中 list
  の最上行にある状態で上矢印を押したときも、
  前の項目を新しい表示ページの最下行に出すようにした
- 先頭項目
  `SYSTEM FLASH`
  から上矢印で末尾へ wrap
  する場合も、
  最終ページを表示してカーソルが見えるようにした
- 上下端から先頭 / 末尾へ wrap
  する場合は共通の
  `menu_clamp_first_visible()`
  を通さず、ページ境界を維持して full render
  するようにした
- 最終ページの項目数が
  `MENU_LIST_MAX_VISIBLE`
  未満の場合は、
  ページ送り後の空行あり layout
  を通常移動中も維持するようにした
- page
  境界を
  `MENU_LIST_MAX_VISIBLE`
  件単位に固定し、
  上下どちらから page
  をまたいでも同じ表示範囲になるようにした
- 通常の上下移動は部分再描画を維持し、
  page scroll
  が必要な場合だけ full render
  する
- 目的は ROM menu
  の 1 行 scroll
  連発によるチラつき低減
- この変更は
  `feature/screenshot-viewer`
  branch
  上の先行 bug fix
  として扱う

## 1.47 `1.0.5` screenshot viewer Phase 2: BMP 一覧表示を追加 (2026-04-28)

- system version を
  `1.0.5`
  へ更新した
- ROM menu
  から
  `S VIEW`
  で入る screenshot viewer
  に、
  `0:/screenshots/*.BMP`
  の一覧表示を追加した
- screenshot viewer
  の entry
  は viewer mode
  開始時に動的確保し、
  `ESC`
  で ROM menu
  へ戻るときに解放する
- entry
  ごとに
  `name`
  と
  `path`
  を固定長 buffer
  に保持し、
  FatFs
  の一時 file name pointer
  は保持しない
- screenshot viewer
  内の上下移動は ROM menu
  と同じ page
  移動 helper
  を使う
- `Enter`
  または
  `-`
  は Phase 2
  の確認用として選択中 BMP
  の path
  を status line
  に表示する
- Phase 3
  では、この Phase 2
  を戻り先として BMP
  header parser
  と表示処理を追加する

## 1.48 `1.0.6` screenshot viewer に index 表示を追加 (2026-04-28)

- system version を
  `1.0.6`
  へ更新した
- screenshot viewer
  の header
  に ROM menu
  と同じ
  `x/n`
  表示を追加した
- BMP
  が 0 件の場合は
  `0/0`
  と表示するようにした
- 実機確認で BMP menu
  は設計どおり表示されていることを確認した
- malloc
  成功を示す UART
  ログは出していないが、失敗時は画面 status
  に
  `NO MEMORY`
  を出す設計であるため、
  一覧表示経路では viewer entry
  確保に成功していると扱う

## 1.49 `1.0.7` screenshot viewer Phase 3: BMP 表示処理を追加 (2026-04-28)

- system version を
  `1.0.7`
  へ更新した
- screenshot viewer
  の一覧で
  `Enter`
  または
  `-`
  を押すと、選択中 BMP
  を fullscreen
  で表示する処理を追加した
- 初期対応 BMP
  は
  `320x320`
  `24bit`
  `BI_RGB`
  で、
  top-down
  と bottom-up
  の両方を扱う
- BMP
  header
  と file size
  を検証し、対応外または破損が疑われる場合は
  `UNSUPPORTED BMP`
  または
  `READ FAILED`
  を status
  に出して一覧へ戻る
- 表示は row buffer
  方式で行い、full image buffer
  は確保しない
- LCD DMA
  は非同期転送なので、各 row
  送信後に
  `lcd_dma_wait()`
  してから line buffer
  を再利用する
- BMP
  表示中は debug code
  などの text
  を画像上に描かない
- clean build
  は
  `cmake --build build --target clean && cmake --build build -j8`
  で成功した
- build
  は
  `PicoCalc NESco Ver. 1.0.7 Build Apr 28 2026 20:36:25`
  だった
- build size
  は
  `text=273388`
  `data=0`
  `bss=92772`
  だった
- UF2 SHA-256
  は
  `a94e962e962171937dfb108e37b3c3842aca04db033d41feab9ea1d5090f549b`
  だった
- 実機確認で screenshot BMP
  を表示できた
- これにより、ROM menu
  から過去に撮った screenshot BMP
  を選択して PicoCalc
  本体で確認する初期機能は完了扱いにした

## 1.50 `1.0.8` screenshot viewer の mode 識別色を追加 (2026-04-28)

- system version を
  `1.0.8`
  へ更新した
- screenshot viewer
  の header
  文字と上下区切り線を cyan
  系の専用 accent
  に変更した
- 選択 cursor
  の青は ROM menu
  と同じ意味を保つため変更していない

## 1.51 `1.0.9` screenshot viewer 入口を F4 に変更 (2026-04-28)

- system version を
  `1.0.9`
  へ更新した
- ROM menu
  から screenshot viewer
  へ入る key
  を
  `S`
  から
  `F4`
  に変更した
- ROM menu
  footer
  と help
  の表示も
  `F4`
  に合わせた
- build
  は
  `PicoCalc NESco Ver. 1.0.9 Build Apr 28 2026 20:53:22`
  で成功した
- build size
  は
  `text=273652`
  `data=0`
  `bss=92772`
  だった
- UF2 SHA-256
  は
  `5e0cba43c49d184531a42bc48a9ef4875171818f9449346564e6bae48802a0cb`
  だった
- 実機最終確認で、F4
  から screenshot viewer
  に入り、BMP
  一覧と表示が動作することを確認した
- F4
  化後の screenshot viewer
  初期機能は完了扱いにした

## 1.11 `0.3.21` BokosukaWars trace 領域を uf2loader 保護域から退避 (2026-04-25)

- system version を
  `0.3.21`
  へ更新した
- `pelrun/uf2loader`
  の README
  を確認し、
  RP2040
  では flash
  最後部 16KB
  を bootloader / Pico W bluetooth stack
  用にアプリが上書きしない前提であることを確認した
- 旧 trace
  配置は
  `PICO_FLASH_SIZE_BYTES - 1MB`
  起点だったため、
  2MB flash
  では
  `0x00100000-0x001FFFFF`
  を erase / program
  し、
  uf2loader
  の top 16KB
  を含んでいた
- 実際に今回確認済みの大きな ROM
  `Project_DART_V1.0.nes`
  と
  `TheTowerOfTurmoil_1.03.nes`
  はどちらも
  `524304`
  bytes
  であり、
  現行 XIP ROM
  配置では erase
  範囲が
  `0x00080000-0x00101FFF`
  に収まる
- この確認に基づき、
  BokosukaWars
  用 trace
  は大容量 ROM
  機能との同時利用を前提にせず、
  既知最大級 ROM
  領域の直後へ移した
- trace
  容量は
  `1MB`
  から
  `512KB`
  に縮小した
- trace
  開始 offset
  は
  `0x00102000`
  固定とし、
  trace
  範囲は
  `0x00102000-0x00181FFF`
  になった
- これにより、
  2MB flash
  での uf2loader
  保護域
  `0x001FC000-0x001FFFFF`
  との間に余裕を残す
- build
  は
  `PicoCalc NESco Ver. 0.3.21 Build Apr 25 2026 12:00:58`
  で成功した
- build size
  は
  `text=272480`
  `data=0`
  `bss=164752`
  だった
