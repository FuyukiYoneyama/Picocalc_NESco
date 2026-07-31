# TASKS.md

この文書は `Picocalc_NESco` の現在タスクの正本です。
完了した項目は結果つきで `docs/project/Picocalc_NESco_HISTORY.md` へ移し、この文書から外します。

## 互換性・未確認機能

- `[pending]` Mapper7 / AxROM の画面崩れを不具合ありとして保留する
  - Mapper7 ROM
    で nametable / background
    崩れを確認した
  - Mapper7 one-screen mirroring
    の 0x2000 / 0x2400
    反転実験では改善しなかった
  - いったん追加追跡は止め、必要時に CHR RAM / PPU nametable 更新タイミング側から再調査する
- `[pending]` Mapper9 / MMC2 の画面崩れを不具合ありとして保留する
  - Mapper9 ROM
    で CHR / background
    崩れを確認した
  - MMC2 latch trigger
    範囲を `$0FD8/$0FE8`
    と `$1FD8-$1FDF/$1FE8-$1FEF`
    に合わせる実験では改善しなかった
  - 次に調査する場合は、BG fetch
    だけでなく sprite fetch
    時にも MapperPPU latch
    更新が必要かを重点的に見る
- `[pending]` Mapper 動的確保済み mapper の実機確認を進める
  - 優先確認:
    - `Map19`
      - Namco 163 系で、市販タイトル確認対象として優先度が高い
    - `Map185`
      - CNROM copy protection 系で、市販タイトル確認対象として優先度が高い
  - 低優先 / 特殊枠:
    - `Map6`
      - FFE / copier 系寄りとして扱う
    - `Map188`
      - 正規市販タイトル確認対象としては優先度低めとして扱う
    - `Map235`
      - multicart mapper として扱い、通常の市販単体タイトル確認とは別枠にする
- `[pending]` Mapper30 の `*.m30` 保存 / 復元を実ゲームで確認する
  - ROM 起動と表示は実機確認済み
  - 未確認なのは PRG flash overlay の書き込み / 復元
- `[pending]` Mapper87 / Choplifter 系の確認を、別の Mapper87 ROM 入手後に再開する

## 保留中の改善候補

- `[deferred]` audio ring size を `4096` から `2048` へ下げられるか再評価する
  - 現時点では RAM に余裕があるため、今すぐの課題ではない
- `[deferred]` 音量調整は `docs/audio/AUDIO_OUTPUT_GAIN_REDESIGN_20260422.md` を正本として必要時に再開する
- `[deferred]` 256 表示で平均 60fps を目指す追加高速化は、難度が高いため独立課題として扱う
  - LCD バス帯域側の分析は `docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md` を正本とする
  - 現状は normal 256x240 で 60fps 予算の 94.4%、stretch 320x300 で 147.5% を転送が占める
  - 削減候補は効果順に COLMOD 12 bit/pixel、frame 単位 window、DMA 32 bit 化、縦 224 crop
  - 着手する場合は COLMOD 12 bit/pixel から始める
    - `NesPalette` が RGB444 のため、色情報を落とさずに転送量を 25% 減らせる
    - panel 側の色展開が一致するかの実機確認が前提になる
- `[in_progress]` 256 表示の追加高速化を、事前計測結果に基づいて進める
  - 段階 0 の計測用コード追加と実機計測は完了した。結果の正本は
    `docs/project/Picocalc_NESco_HISTORY.md` の `1.1.27 計測 build 段階0` を参照する
  - BG line buffer index 化は実装へ進める
    - 3 ROM の `bg_tile_us_per_frame` は概ね `6.6`〜`6.9 ms`
    - 判断基準の `4 ms 以上 8 ms 未満` に該当するため、段階 2 の実測で打ち切り判断を行う
  - LCD queue wait の raw counter は 1 秒窓ごとに reset されず累積して見える
    - queue depth を判断する前に計測を修正し、baseline build だけを再計測する
    - BG line buffer 実装の着手条件ではない
  - 目的:
    - 実装後に効果を測れる基準値を先に作る
    - `1.1.26` の実測 fps は 3 ROM とも normal のバス下限 `15.73 ms` を上回っており、
      normal が core0 律速か LCD バス律速かを実装前に確定させる
    - core0 側の対象を決めるため、draw 内訳の現在値を得る
  - 計測 build は 2 つ作成済みである
    - baseline: `NESCO_CORE1_BASELINE_LOG=ON`
      - log: `/home/fuyuki/pico_dvl/codex/log/pico20260731_192451.log`
    - BG share: `NESCO_BG_TILE_SHARE_LOG=ON`
      - log: `/home/fuyuki/pico_dvl/codex/log/pico20260731_193201.log`
      - `NESCO_CORE1_BASELINE_LOG` を含意し、`[BG_SHARE]` も出力する
  - 実機計測は baseline → BG share の順で実施した
  - 計測 3: background tile 実時間 — **完了**
    - 正本は `docs/design/BG_LINE_BUFFER_INDEX_REDESIGN_20260731.md`
    - 主判定は **1 frame あたりの `bg_tile_us` 絶対値**とする
      - 比率は分母の選び方で結論が動くため判定に使わない
      - `[FPS_SUMMARY]` の `cpu_us` `ppu_us` `apu_us` `other_us` の 4 分類からは
        background tile の割合は分からない。ここから推定してはならない
      - `g_perf_ppu_bg_tile_us` は scanline ごとの計測であり、
        `1.1.5` / `1.1.6` で重すぎるとされた tile ごと計測とは別物である
        (tile ごと計測は `1.1.8` で既に除去済み)
    - 併せて `g_perf_ppu_bg_us` `g_perf_ppu_sprite_us` も出し、
      background 以外が支配的だった場合に次の対象を選べるようにする
    - `1.1.5` の内訳 (`draw 27.5 ms` 中 `bg_tile 19.7 ms`、約 7 割) は
      background tile render LUT 化による `fps +78%` より前の値であり、現在値は不明である
    - 判断 (理論削減 1.6 ms/frame との比):
      - `bg_tile_us` が 8 ms 以上なら実装する
      - 4 ms 以上 8 ms 未満なら実装するが、段階 2 の実測で打ち切り判断を行う
      - 4 ms 未満なら見送り、`bg_us` / `sprite_us` を見て対象を選び直す
    - 実測値: 3 ROM で概ね `6.6`〜`6.9 ms/frame`。実装するが、段階 2 で
      3 ROM すべての平均 `frame_us` 改善が `3%` 未満なら不採用とする
  - 計測 1: stretch 実測 fps — **完了**
    - 対象は `Xevious.nes` stretch
    - 最後の stretch 実測は `1.0.15` の `36.34 fps` で、`1.1.26` の値が存在しない
    - stretch のバス上限は `40.7 fps` なので、天井に貼り付いているかどうかで
      COLMOD 12 bit/pixel の効果見積もりが変わる
    - 実測値: `Xevious.nes` stretch は約 `36.7 fps` (`27.3 ms/frame`)。
      `40.7 fps` のバス上限に貼り付いていないため、COLMOD 12 bit/pixel は BG 実装より後に判断する
  - 計測 2: `lcd_queue_wait_us` / `lcd_queue_wait_count` — **再計測待ち**
    - `NESCO_CORE1_BASELINE_LOG=ON` の `[CORE1_BASE]` から取得する
    - 計測機構は `1.1.1` で追加済みだが、実測値が履歴に残っていない
    - LCD worker queue depth は 4 scanline のため、core1 の strip DMA 待ちが
      core0 の `PostDrawLine` を止めている量がここに出る
    - normal 側で期待できる二次効果の大きさがこれで決まる
    - BG line buffer index 化における queue depth 8 化の効果見積もりも兼ねる
  - 計測 A: 基準 frame time (build 1) — **normal 3 ROM は完了**
    - 対象は `LodeRunner.nes` `Xevious.nes` `Project_DART_V1.0.nes` の normal と
      `Xevious.nes` の stretch
    - 実装後の比較はこの値に対して行う
    - 記録するのは `frame_us` の平均 / 中央値 / 95 percentile / 最大 と
      `lcd_queue_wait_us` / `lcd_queue_wait_count`
    - fps 比では削減された実時間が分からないため、比較は `frame_us` で行う
      - `47.99 fps` の 3% と `55.50 fps` の 3% では削減 µs が異なる
    - 平均だけでは引っかかりの改善 / 悪化が見えないため、
      95 percentile と最大も必ず取る
    - 主判定は平均と 95 percentile とする
      - 最大値は UART log / SD access / 単発の割り込みにも影響されるため、
        単独の不採用理由にはしない。複数の計測窓で再現した場合にだけ理由に加える
      - 各 ROM について同じ場面を複数の計測窓で取り、中央値どうしを比較する
  - 参照値 (`1.1.26` / `20260719_174109.log`):
    - `Xevious.nes` `55.50 fps` (`18.02 ms`)、バス下限との差 `+2.29 ms`
    - `LodeRunner.nes` `47.99 fps` (`20.84 ms`)、バス下限との差 `+5.11 ms`
    - `Project_DART_V1.0.nes` `45.68 fps` (`21.89 ms`)、バス下限との差 `+6.16 ms`
  - 判断基準:
    - stretch が `40.7 fps` 付近なら COLMOD 12 bit/pixel を実装する
    - stretch が `35 fps` 前後で `lcd_queue_wait_us` も小さいなら、
      両表示とも core0 律速なので LCD 帯域側は着手しない
    - core0 側の判断基準は上記の計測 3 に記載する
- `[pending]` BG line buffer を palette index の byte 化する案を検討する
  - 正本は `docs/design/BG_LINE_BUFFER_INDEX_REDESIGN_20260731.md`
  - 着手条件は上記の事前計測、特に計測 3 の結果である
  - 狙いは core0 の PPU background 描画を軽くすることで、
    LCD 帯域側と違い normal view の fps に直接効く
  - scanline buffer を RGB565 の色から palette RAM index の byte へ変え、
    色への変換を core1 の packing 時に行う
  - 同時に `BackgroundOpaqueLine` を廃止できる
    - 読み手は `compositeSpriteRange()` の 1 か所だけであることを確認済み
    - sprite 0 hit 判定は参照していない
  - 主なリスクは frame 途中の palette 変更で、`PalTable` snapshot で対処する
    - `PalTable` の書き込みは `infones/K6502_rw.h` の 2 か所だけであることを確認済み
    - snapshot は queue item 内に持つ
      - `display_lcd_worker_pop_item()` は item 全体を lock 保持下で copy するため、
        item 内方式は構造上安全である
      - 独立 ring 方式は競合する。pop 済みで packing 中の 1 item が
        ring の生存数計算から抜けるため、ring 段数を depth と同数にしても防げない
    - reset / ROM 切替 / drain / 表示モード切替では初回 snapshot を強制する
      - これがないと起動直後の scanline で前 ROM の palette を使う
  - 副次効果:
    - queue item が小さくなるため queue depth を増やせる
      - snapshot 込みでは depth 6 が RAM 中立 (`-48 byte`)、
        depth 8 は `+656 byte` である。「RAM 増なしで depth 8」は成立しない
      - まず depth 6 で始め、計測 2 の結果次第で depth 8 を検討する
    - core1 が index から色への LUT を引く形になるため、
      LCD の COLMOD 12 bit/pixel 化が LUT 出力の差し替えだけで済むようになる
  - 削減量の理論値は約 `2.0 ms/frame` だが、これは実効値の予測ではない
    - Cortex-M0+ では store 幅を狭めても cycle は減らないため、
      削減は幅ではなく `pal[]` load と `dst_opaque` store を消すことから出る
    - core0 から消えた palette lookup は core1 へ移るため、frame 全体では相殺され得る
  - 実装は 4 段階に分ける
    - 段階 0: 計測用コードの追加 (通常 build の動作は変えない)
    - 段階 1: palette snapshot 機構 (動作不変)
    - 段階 2: index 描画 + queue item の byte 化 (効果測定ポイント、打ち切り判断あり)
      - queue item の byte 化をここに含めるのは、
        段階 2 の測定に queue copy 半減が含まれるかを曖昧にしないため
      - `InfoNES_PostDrawLine()` の worker 非使用 fallback 経路も index 化する
        - 現在は `const WORD *src` 前提であり、対応しないと表示が壊れる
        - この経路は `InfoNES_DrawLine()` の直後に同じ core0 で走るため、
          palette version の持ち回りは不要で現在の `PalTable` を使えばよい
      - core1 の packing 時間と queue occupancy 最小値も測る
      - core0 の queue full 待ちだけでは、core1 が枯れる側の問題を検出できない
    - 段階 3: queue depth の変更のみ (効果測定ポイント)
