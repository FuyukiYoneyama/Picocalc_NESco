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
- `[deferred]` stretch 表示の追加高速化を独立課題として再開する
  - BG palette index 化 `1.1.29` は採用済み。結果の正本は
    `docs/project/Picocalc_NESco_HISTORY.md` と
    `docs/design/BG_LINE_BUFFER_INDEX_REDESIGN_20260731.md` とする
  - normal 3 ROM は平均・p95 とも `16,700 us` 以下へ到達したため、
    normal 向けの polling / queue depth 変更は行わない
  - `1.1.29` の実プレイを含む stretch 参考値:
    - LodeRunner: frame 平均中央値 `29.02 ms`、queue wait 比率中央値 `37.89%`
    - Project_DART: frame 平均中央値 `30.23 ms`、queue wait 比率中央値 `34.28%`
    - Xevious: frame 平均中央値 `27.22 ms`、queue wait 比率中央値 `48.41%`
    - 1 wait あたりは全 ROM とも約 `100.4`〜`100.7 us` で、現行の `sleep_us(100)` と一致する
  - 最初に `sleep_us(100)` の polling 幅短縮または通知方式を A/B 比較し、
    その後も queue wait と p95 が改善余地を示す場合だけ queue depth を検討する
  - COLMOD 12 bit/pixel は polling 改善の結果後に判断する。LCD 側の正本は
    `docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md` とする
  - 再開時に独立した計測契約と version を決める。現時点では version を予約しない
