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
- `[pending]` 高速化に着手する前に、事前計測を 1 回の実機 session でまとめて取る
  - 目的:
    - 実装後に効果を測れる基準値を先に作る
    - `1.1.26` の実測 fps は 3 ROM とも normal のバス下限 `15.73 ms` を上回っており、
      normal が core0 律速か LCD バス律速かを実装前に確定させる
    - core0 側の対象を決めるため、draw 内訳の現在値を得る
  - 3 件とも `NESCO_CORE1_BASELINE_LOG=ON` の同一 build で取れる
    - build を分けると比較できなくなるため、必ず同一 session で取る
  - 実施順は 計測 3 → 計測 1 → 計測 2 とする
    - 計測 3 が不発なら core0 側の設計は不要になるため先に置く
  - 計測 3: `1.1.26` の draw 内訳
    - 正本は `docs/design/BG_LINE_BUFFER_INDEX_REDESIGN_20260731.md`
    - `[CORE1_SUMMARY]` の `cpu_us` `ppu_us` `apu_us` `other_us` の比率を取る
    - background tile が draw に占める割合を確定させる
    - `1.1.5` の内訳 (`draw 27.5 ms` 中 `bg_tile 19.7 ms`、約 7 割) は
      background tile render LUT 化による `fps +78%` より前の値であり、現在値は不明である
    - `1.1.5` 相当の tile ごと `time_us_64()` は計測負荷が高すぎるため使わない
    - 判断:
      - 60% 以上なら BG line buffer の index 化を実装する
      - 40% 以上 60% 未満なら実装するが、段階 2 の実測で打ち切り判断を行う
      - 40% 未満なら見送り、draw 内訳で最大の項目を対象に検討し直す
  - 計測 1: `1.1.26` の stretch 実測 fps
    - 対象は `Xevious.nes` stretch
    - 最後の stretch 実測は `1.0.15` の `36.34 fps` で、`1.1.26` の値が存在しない
    - stretch のバス上限は `40.7 fps` なので、天井に貼り付いているかどうかで
      COLMOD 12 bit/pixel の効果見積もりが変わる
  - 計測 2: `lcd_queue_wait_us` / `lcd_queue_wait_count`
    - `NESCO_CORE1_BASELINE_LOG=ON` の `[CORE1_BASE]` から取得する
    - 計測機構は `1.1.1` で追加済みだが、実測値が履歴に残っていない
    - LCD worker queue depth は 4 scanline のため、core1 の strip DMA 待ちが
      core0 の `PostDrawLine` を止めている量がここに出る
    - normal 側で期待できる二次効果の大きさがこれで決まる
    - BG line buffer index 化における queue depth 8 化の効果見積もりも兼ねる
  - 計測 A として、上記と同一 build / 同一場面の fps も記録する
    - 対象は `LodeRunner.nes` `Xevious.nes` `Project_DART_V1.0.nes` の normal と
      `Xevious.nes` の stretch
    - 実装後の比較はこの値に対して行う
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
  - 副次効果:
    - queue item が半分になるため、RAM 増なしで queue depth を 4 から 8 へ増やせる
    - core1 が index から色への LUT を引く形になるため、
      LCD の COLMOD 12 bit/pixel 化が LUT 出力の差し替えだけで済むようになる
  - 実装は 3 段階に分ける
    - 段階 1: palette snapshot 機構 (動作不変)
    - 段階 2: index 描画への切り替え (効果測定ポイント、打ち切り判断あり)
    - 段階 3: queue item の byte 化と depth 8 化 (効果測定ポイント)
