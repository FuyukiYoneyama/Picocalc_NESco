# TASKS.md

この文書は `Picocalc_NESco` の現在タスクの正本です。
完了した項目は結果つきで `docs/project/Picocalc_NESco_HISTORY.md` へ移し、この文書から外します。

## 互換性・未確認機能

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

## 保留中の改善候補

- `[deferred]` audio ring size を `4096` から `2048` へ下げられるか再評価する
  - 現時点では RAM に余裕があるため、今すぐの課題ではない
- `[deferred]` 音量調整は `docs/audio/AUDIO_OUTPUT_GAIN_REDESIGN_20260422.md` を正本として必要時に再開する
- `[deferred]` LCD側追加高速化は、新しい実測根拠がある場合だけ再計画する
  - queue depth / retry / 通知の系列は`1.1.32` Phase 0で終了した。depth 8は不実施
  - frame単位window設定の削減上限は最大112.6 us/frameで、250 us/frame gate未満だった
  - DMA 32 bit化はLCDバス下限を変えずcore1/SRAM負荷だけを下げるため、実測根拠が出た場合だけ行う
  - 224-line cropは表示内容が変わるため、必要なら設定項目として別計画にする
  - ST7365PのCOLMODはcontrol interfaceで16/18/24 bitだけを定義し、`0x63`の12 bitは未対応。
    実装候補へ戻さない
  - 同一stretch stripの再送省略とframe境界のDMA重なりは未計測の別候補とし、`1.2.0`には含めない
  - LCD側の分析は`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md`を正本とする
