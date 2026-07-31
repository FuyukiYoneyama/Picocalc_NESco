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

- `[next]` stretch LCD queue retryの100 us量子化をA/Bする
  - 詳細計画の正本は
    `docs/design/STRETCH_QUEUE_RETRY_OPTIMIZATION_PLAN_20260731.md` とする
  - Phase 0 (`1.1.30`): 既存の`frame_pacing_sleep_us/count`と、新設する
    `lcd_queue_wait_episodes`を`[CORE1_BASE]`末尾へ出す計測baselineを作る
    - episodeは連続したqueue閉塞期間の開始だけを数え、retry回数と分離する
    - 4 byte counter追加後の通常build `.bss`期待値は`97548`
      - 削除する未使用pacing globalは現行ELFで既に除去されているため、`.bss`差はepisodeの`+4`だけ
  - Phase 1 (`1.1.31`): frame hot pathのqueue-full retry 2箇所だけを
    `sleep_us(100)`から`10 us`定数へ変更する
  - Phase 0単独の実機確認は行わず、2つのUF2を先に作ってnormal/stretch、3 ROMの
    A/Bと機能確認を1回へまとめる
    - 各ROM/modeを最低30窓取り、`max(frame_us_avg) / min(frame_us_avg) <= 1.015`を満たす
      最初の10連続窓を比較する。中央値`±0.5%`条件は2値振動を排除するため使わない
    - `frames`はframe timeとほぼ従属する診断値に留め、窓選択には使わない。遷移窓は固定破棄、
      `input_events=0`、mode一致、log対の整合で除外する
    - Project_DART stretchなどで安定10窓が得られなければ、異常と決めず40窓へ延長する
    - stretch連続30窓は既存の短いepisodeを連結した解析とは異なる新しい測定条件である
  - stretchはframe timeとp95、normalはframe time・p95・fpsで非退行を判定し、
    pacing sleep/frameは余裕量の補助確認に使う
    - `frame_us - queue wait - pacing sleep`はaudio waitなどを含み得る診断用推定値であり、
      純粋なcore0実働時間の確定値とは扱わない
    - `wait_us / wait_count`は実効retry量子の診断値であり、30 usを超えただけでは不採用にしない。
      500 us回収モデルの境界は約67 usで、採否はframe timeと非退行条件で決める
  - queue depth、通知方式、COLMODはA/B結果が出るまで実装せず、versionも予約しない

## 保留中の改善候補

- `[deferred]` audio ring size を `4096` から `2048` へ下げられるか再評価する
  - 現時点では RAM に余裕があるため、今すぐの課題ではない
- `[deferred]` 音量調整は `docs/audio/AUDIO_OUTPUT_GAIN_REDESIGN_20260422.md` を正本として必要時に再開する
- `[deferred]` stretch retry A/B後の追加高速化を結果に応じて再計画する
  - depth 6、通知方式、COLMOD 12 bit/pixelを同時実装しない
  - retry 10 us採用後もLCDバス下限24.58 msとの差が1 ms超残り、queue waitも大きい場合だけ
    depth 6を独立計画する
  - retry短縮がframe timeへ効かない場合、通知方式はlock競合の根拠があるときだけ検討し、
    それ以外はCOLMODを次の主候補にする
  - LCD側の分析は`docs/design/LCD_BUS_BANDWIDTH_ANALYSIS_20260731.md`を正本とする
