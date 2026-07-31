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

## 次に実装する高速化

- `[next]` stretch LCD worker queue depth 4/8をA/Bする
  - 詳細計画の正本は
    `docs/design/STRETCH_QUEUE_DEPTH_OPTIMIZATION_PLAN_20260731.md` とする
  - 前提として、不採用になった10 us retryのcommit `8ba265f`だけをrevertする
    - Phase 0 commit `1f1c093`のepisode/pacing計測fieldは残す
    - `1.1.31`は不採用実験のversionとして再利用しない
  - candidate `1.1.32`では`DISPLAY_LCD_WORKER_QUEUE_DEPTH`だけを`4`から`8`へ変更する
    - 8-line stripを、前stripのDMA中に丸ごと1個先行保持できる構成にする
    - retryは100 us、strip heightは8、queue itemは352 byteのまま
    - queue symbol期待値は`0x580 -> 0xb00`
    - 通常build `.bss`期待値は`97548 -> 98956`
  - baselineは既存`1.1.30`計測artifactを使い、candidateの通常/計測buildを作る
  - 実機は3 ROM、normal/stretch各最低30窓、最初の適格10連続窓を比較する
  - stretch 2/3 ROMで500 us以上改善し、全modeで非退行・fault 0・機能回帰なしなら採用する
  - queue waitの減少だけでは採用せず、frame timeとp95を主判定にする

## 保留中の改善候補

- `[deferred]` audio ring size を `4096` から `2048` へ下げられるか再評価する
  - 現時点では RAM に余裕があるため、今すぐの課題ではない
- `[deferred]` 音量調整は `docs/audio/AUDIO_OUTPUT_GAIN_REDESIGN_20260422.md` を正本として必要時に再開する
- `[deferred]` depth 8 A/B後のLCD側追加高速化を結果に応じて再計画する
  - frame単位window設定をdepthとは混ぜず、独立候補とする
    - ST7365P仕様は画素byte境界でCSXを解除したData Transfer Pauseからの継続を保証する
    - 削減できるcommand byteは約40.8 us/frame相当なので、depth 8より後に置く
  - DMA 32 bit化はLCDバス下限を変えずcore1/SRAM負荷だけを下げるため、実測根拠が出た場合だけ行う
  - 224-line cropは表示内容が変わるため、必要なら設定項目として別計画にする
  - ST7365PのCOLMODはcontrol interfaceで16/18/24 bitだけを定義し、`0x63`の12 bitは未対応。
    実装候補へ戻さない
  - LCD側の分析は`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md`を正本とする
