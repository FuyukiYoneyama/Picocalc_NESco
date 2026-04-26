# Picocalc_NESco 電源 ON 初期化による音声ポップ抑制

日付: 2026-04-22  
対象プロジェクト: `Picocalc_NESco`

## 目的

ROM 開始時の `ブチ` 音を減らすため、
**音声ハードの初期化をゲーム開始時ではなく電源 ON 時へ寄せる**
方針を整理する。

今回の目的は、

- `menu -> game` 切替時の PWM/DMA 再初期化を避ける
- Pico 側だけで実施できる対策に限定する
- 既に採用済みの音量設定を壊さずにポップ音だけを減らす

ことである。

## 現状の事実

- `platform/audio.c` の `audio_init()` は `pwm_audio_init()` を呼ぶ
- 同じく `InfoNES_SoundOpen()` でも `pwm_audio_init()` を呼ぶ
- `InfoNES_SoundClose()` は `audio_close()` を経由して `pwm_audio_close()` を呼ぶ
- `platform/audio.h` には `audio_init()` 宣言がある
- 現在の active path では `audio_init()` を呼んでいる箇所は確認できていない
- `platform/main.c` では現状 `display_init()`、`input_init()`、`rom_image_init()` は呼ばれているが、`audio_init()` は呼ばれていない
- `drivers/pwm_audio.c` は ring buffer が空のとき、中心値 `128` の無音サンプルで DMA half-buffer を埋める
- `platform/audio.c` には startup ramp が入っている
- `drivers/pwm_audio.c` には startup silence が入っている
- それでもユーザー確認では `ブチ音は消えない`

## 切り分け済みのこと

- ハード回路上には `PA_EN` が存在し、音声アンプの `EN` に接続されている
- ただし現行ファームでは、その `PA_EN` はキーボード MCU 側で制御されている
- Pico 側から使っているキーボード I2C レジスタには、`PA_EN` を操作するコマンドは無い
- STM32 側の `Serial1` は確認できた範囲ではデバッグ出力用であり、UART 受信コマンド処理は実装されていない

したがって、
**STM32 側を変更しない前提では、Pico 側からアンプ mute/unmute を直接制御する手段は現状ない**
と整理する。

## 基本方針

### 1. 音声ハード初期化は電源 ON 時の 1 回を基本にする

最も有力な対策は、
PWM/DMA の初期化を `audio_init()` 側へ寄せ、
ゲーム開始時には再初期化しない構造へ変えることである。

狙い:

- PWM 出力開始時の段差を減らす
- DMA 開始時の状態変化を `menu -> game` 切替から切り離す
- 既存の silence fill をそのまま活かす

### 2. `InfoNES_SoundOpen()` は「論理 open」に縮小する

`InfoNES_SoundOpen()` は今後、

- ring buffer の初期化
- 統計値の初期化
- startup ramp などソフト側状態のリセット

のみを担当し、
`pwm_audio_init()` は呼ばない。

ただし、
`sample_rate` が電源 ON 時に初期化した値と異なる場合は別扱いとする。

boot 時に platform が使う初期サンプルレートは、
platform 固有定数ではなく
**InfoNES 側で定義した標準サンプルレートを参照する**
方針とする。

この標準サンプルレートの定義場所は、
`infones/InfoNES_pAPU.h`
に固定する。

symbol も計画上ここで固定し、
**`INFONES_AUDIO_DEFAULT_SAMPLE_RATE`**
を新設して platform 側から参照する方針とする。

つまり、

- `InfoNES_pAPU.h` に `INFONES_AUDIO_DEFAULT_SAMPLE_RATE` を定義する
- `audio_init()` はその値で hardware init する
- `InfoNES_SoundOpen()` はその値と一致する限り再初期化しない

という構成を基本とする。

そのうえで、
将来の `ApuQuality` 変更や別モードを考えると、
**異なる `sample_rate` が来た場合だけ hardware を再初期化する例外経路**
を残す方が安全である。

### 3. `InfoNES_SoundClose()` では PWM/DMA を止めない

`InfoNES_SoundClose()` は hardware close ではなく、

- ring buffer を無音状態へ戻す
- 必要な統計だけリセットする
- 再生入力を止めても出力系は生かしたままにする

方針とする。

これにより、
menu 中も 8bit center 値 `128` を継続出力し、
スピーカ出力の連続性を保つ。

## 実装方針

### フェーズ A: `audio_init()` と `audio_close()` の役割整理

`platform/audio.c`
で、

- `audio_init()` は電源 ON 時の 1 回だけ hardware init
- `audio_close()` はアプリ終了時の本当の close 専用

という役割へ整理する。

必要なら、
hardware が初期化済みかを示すフラグを導入する。

あわせて、
`platform/main.c`
で電源 ON 時に `audio_init()` を 1 回呼ぶようにする。

これは今回の方針における必須条件であり、
ここを追加しないまま `InfoNES_SoundOpen()` から `pwm_audio_init()` を外すと、
音声 hardware が一度も立ち上がらず無音化する可能性がある。

boot 時に使うサンプルレートは、
`InfoNES_pAPU.h`
で定義した
`INFONES_AUDIO_DEFAULT_SAMPLE_RATE`
を `platform/audio.c` から参照する。
これにより、
boot 時 init と実行時 open の基準レートを一致させる。

### フェーズ B: `InfoNES_SoundOpen()` から hardware init を外す

`InfoNES_SoundOpen()` では

- `s_open_samples_per_sync`
- `s_open_clock_per_sync`
- ring buffer
- startup ramp
- ログ用統計

をリセットするだけにする。

`pwm_audio_init()` は呼ばない。

ただし、
`sample_rate` が現在の hardware 設定値と一致しない場合だけは、
例外的に `pwm_audio_init()` を伴う再初期化を許可する。

この比較基準に使う「標準サンプルレート」も、
`InfoNES_pAPU.h`
の
`INFONES_AUDIO_DEFAULT_SAMPLE_RATE`
を正とする。

### フェーズ C: `InfoNES_SoundClose()` を無音待機化する

`InfoNES_SoundClose()` は
`pwm_audio_close()` に進まず、
出力系を止めずに無音待機状態へ戻す。

`drivers/pwm_audio.c` は既に ring 欠乏時へ `128` を補充するため、
この方針と相性がよい。

また、
driver 側の周期ログ統計は現在 `pwm_audio_init()` 時に初期化されているため、
hardware 再初期化をやめる場合は扱いを明示する必要がある。

候補は次のどちらかとする。

- `InfoNES_SoundOpen()` 時に driver 側統計を明示 reset する
- 比較対象は ROM 開始直後ではなく、次の periodic log 窓から採る

実装の明快さを優先するなら、
`pwm_audio_reset_stats()` のような論理 open 用 reset を追加する方が望ましい。

### フェーズ D: 必要なら startup silence の意味を見直す

hardware init が電源 ON 時 1 回になると、
現在の startup silence は
「ROM 開始時の対策」ではなく
「電源 ON 時の対策」へ役割が寄る。

そのため、
必要なら後段で

- 長さの見直し
- `InfoNES_SoundOpen()` 時には適用しない整理

を行う。

## 変更対象

主対象:

- `platform/main.c`
- `platform/audio.c`

副対象:

- `drivers/pwm_audio.c`

【推定】
`drivers/pwm_audio.c` は大きく変えずに済む可能性が高い。
まずは `platform/audio.c` 側の open/close 責務整理を優先する。

## 確認項目

### 実装確認

- 電源 ON 後に `[AUDIO] pwm init ...` が 1 回だけ出る
- `menu -> game` 切替時に新たな `pwm init` が出ない
- ROM 終了後も PWM/DMA は止まらず、無音継続になる
- boot 時 hardware init は InfoNES 側標準サンプルレートを使う
- `sample_rate` が同一のときは hardware 再初期化が起きない
- `sample_rate` が異なる場合だけ例外的に再初期化される

### 実機確認

- `menu -> game` 直後の `ブチ` 音が減るか
- 音量感が採用済み状態から悪化しないか
- 左右出力が引き続き両方から鳴るか
- `[AUDIO] out_peak / out_rms / underruns` が悪化しないか
- `[AUDIO_MIX]` 周期ログが引き続き出るか
- driver 側統計は reset 後の窓、または次の periodic log 窓から比較する

## 成功条件

- `menu -> game` 時のブチ音が明確に減る、または消える
- ROM 開始時に hardware init ログが増えない
- 通常再生中の音量・歪み・左右出力が維持される
- `underruns` や `overrun` が目立って悪化しない

## 失敗条件

- ブチ音が改善しない
- menu 中または ROM 切替後に無音維持が崩れる
- 音が出なくなる、または左右どちらかが欠ける
- `underruns` や `clipped` が明確に悪化する

## 非目標

今回やらないこと:

- STM32 側ファーム変更
- `PA_EN` 制御用の I2C/UART プロトコル追加
- 音量設計の再調整
- ハード回路変更

## 結論

STM32 側を触らずに進める場合、
ROM 開始時ポップ音の本命対策は
**Pico 側で PWM/DMA を常駐化し、ゲーム開始時の再初期化をやめること**
である。

このため、
次の実装は

- `audio_init()` でのみ hardware init
- `InfoNES_SoundOpen()` は論理 open のみ
- `InfoNES_SoundClose()` は silent idle のみ

という構成へ進める。
