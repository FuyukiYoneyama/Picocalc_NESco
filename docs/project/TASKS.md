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
- `[pending]` LCD 帯域削減に着手する前に、事前計測を 2 件取る
  - 目的:
    - 実装後に効果を測れる基準値を先に作る
    - `1.1.26` の実測 fps は 3 ROM とも normal のバス下限 `15.73 ms` を上回っており、
      normal が core0 律速か LCD バス律速かを実装前に確定させる
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
  - 参照値 (`1.1.26` / `20260719_174109.log`):
    - `Xevious.nes` `55.50 fps` (`18.02 ms`)、バス下限との差 `+2.29 ms`
    - `LodeRunner.nes` `47.99 fps` (`20.84 ms`)、バス下限との差 `+5.11 ms`
    - `Project_DART_V1.0.nes` `45.68 fps` (`21.89 ms`)、バス下限との差 `+6.16 ms`
  - 判断基準:
    - stretch が `40.7 fps` 付近なら COLMOD 12 bit/pixel を実装する
    - stretch が `35 fps` 前後で `lcd_queue_wait_us` も小さいなら、
      両表示とも core0 律速なので LCD 帯域側は着手しない
