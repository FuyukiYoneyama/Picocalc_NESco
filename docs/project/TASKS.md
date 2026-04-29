# TASKS.md

この文書は `Picocalc_NESco` の現在タスクの正本です。
完了した項目は結果つきで `docs/project/Picocalc_NESco_HISTORY.md` へ移し、この文書から外します。

## 互換性・未確認機能

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
