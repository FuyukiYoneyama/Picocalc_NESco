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

## 計測基盤

- `[next]` frame pacing sleep を `[CORE1_BASE]` へ出し、normal 上限到達後の計測基準を作る
  - 背景:
    - `1.1.29` で normal 3 ROM が 60 fps pacing 上限 (`frame_target_us = 16667`) へ到達した
    - このため normal view では `frame_us` が pacer で決まり、
      以降の core0 改善も小さな劣化も検出できない
    - `display_perf_take_window()` は既に `frame_pacing_sleep_us` /
      `frame_pacing_sleep_count` を返しているが、
      `infones/InfoNES.cpp` の `perf_log_if_due()` が `(void)` で捨てている
  - 作業:
    - `[CORE1_BASE]` へ `frame_pacing_sleep_us` と `frame_pacing_sleep_count` を追加する
    - 出力 field を増やすだけで、accumulator と窓の take-and-zero は変更しない
    - 既存 field の並びは変えず末尾へ追加する
    - version は `PATCH` を 1 つ上げる
  - これで判定できること:
    - core0 実働時間 = `frame_us_avg - queue wait/frame - pacing sleep/frame`
    - 上限到達 ROM でも改善量と劣化量を数値で追える
    - `1.1.29` で下限しか出せなかった core0 削減量を確定できる
      (下限は `docs/project/Picocalc_NESco_HISTORY.md` の `1.1.29` 合格後レビューを正本とする)
  - 実機検証は単独で依頼しない。次の高速化課題の A/B と同じ 1 回にまとめる
- `[next]` 上限到達 ROM を含む場合の採用条件を、実装着手前に書き換える
  - `1.1.29` の旧条件「各 ROM の `frame_us_avg` 中央値が `3%` 以上短い」は、
    baseline が既に pacing 上限だった Xevious では原理的に満たせなかった
  - 事後に明文化した「平均・p95 とも `16,700 us` 以下なら上限到達合格」は結論としては正しいが、
    上限へぎりぎり届いた実装と余裕を持って届いた実装を区別できない
  - 次の normal 側課題では、着手前に次の 2 段構えで書く
    1. baseline が上限未到達の ROM: 従来どおり `frame_us_avg` 中央値の改善率で判定する
    2. baseline が上限到達済みの ROM: `frame_us_avg` ではなく
       `frame_pacing_sleep_us` の増加、または core0 実働時間の減少で判定する
  - 判定 ROM が 1 本でも上限に達している場合は、着手前にどちらの条件を使うか明記する

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
