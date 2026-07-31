# Release Gate 1.2.0

この文書は、`Picocalc_NESco 1.2.0` release candidateを公開するか判定する正本です。

状態: **合格。release artifact承認済み、GitHub Release公開作業へ進む。**

## release対象

- `1.1.26`で採用済みのsprite active list
- `1.1.28`で検証したpalette snapshot protocol
- `1.1.29`で採用したBG palette index pipeline
- `1.1.30`以降に追加した、通常buildへ出力を入れないcompile-time診断基盤

次はrelease対象に含めない。

- `1.1.31`の10us queue retry実験。効果がなくrevert済み
- depth 8。`1.1.32`診断結果により未実装
- 計測用`[CORE1_BASE]`、`[FRAME_STATS]`、`[BG_SHARE]`、`[PALETTE_SNAPSHOT]`出力

## 公開前の必須条件

### versionと文書

- `platform/version.h`が`1.2.0`
- READMEの現在versionが`1.2.0`
- release notes、HISTORY、TASKS、PLANSが現在状態と一致する
- stretchを60fps化したという記述をしない
- Mapper7 / Mapper9の既知不具合をrelease notesへ記載する

### release build

- `CMAKE_BUILD_TYPE=Release`
- 全計測 / runtime log optionを明示的にOFF
- `NESCO_SPRITE_ACTIVE_LIST=ON`
- clean configure / clean build成功
- ELF bannerが`PicoCalc NESco Ver. 1.2.0 Build ...`
- ELFに次の文字列が存在しない
  - `[CORE1_BASE]`
  - `[FRAME_STATS]`
  - `[BG_SHARE]`
  - `[PALETTE_SNAPSHOT]`
- ELF / UF2のsizeとSHA-256を記録する
- 実機smoke用artifactは`build-release/Picocalc_NESco.uf2`とし、別名copyを作らない
- smoke合格後、GitHub Release添付用にだけ`Picocalc_NESco-1.2.0.uf2`を作る
- version付きGitHub assetはSD card上の実機確認には使わない

### 混入確認

- tracked fileにROM、save、圧縮ROM候補がない
- build directory、local log、実験artifactをsourceへ含めない
- GitHubのsource archiveはrelease tag由来とする

### 最終実機smoke

実機確認は1回にまとめ、次を確認する。

1. 起動bannerが`1.2.0`である
2. ROM menuとhelpを表示できる
3. LodeRunner、Project_DART、Xeviousが起動する
4. normal表示に色化け、sprite優先度、左端clipの回帰がない
5. `Shift+W`でstretchへ切り替え、再度normalへ戻せる
6. 入力と音に問題がない
7. `F1` reset後も表示と操作が戻る
8. `F5` screenshotが保存できる
9. `ESC`でROM menuへ戻れる
10. UARTは起動banner以外の計測logを出さない

既存の長時間計測で性能とpalette protocolは確認済みなので、最終smokeで30窓の性能logは取らない。

## 公開を止める条件

- release buildに計測log文字列が残る
- version、README、artifact名が一致しない
- clean buildに失敗する
- palette、sprite優先度、clip、normal/stretch切替に回帰がある
- 入力、音、reset、screenshot、menu復帰のいずれかが失敗する
- tag対象commitとartifactを作ったsourceが一致しない

## 公開手順

1. release candidateのbuild / artifact検証結果をHISTORYとbuild checkへ記録する
2. release preparationをcommitしてremote branchへpushする
3. release UF2で最終実機smokeを行う
4. smoke結果を記録してcommitする
5. mainへ統合し、CI成功を確認する
6. mainのrelease commitへ`v1.2.0` tagを付ける
7. `Picocalc_NESco-1.2.0.uf2`をGitHub Releaseへ添付する
8. tag、commit、build ID、SHA-256がrelease notesと一致することを確認する

## release承認 (2026-08-01)

- ユーザーが`build-release/Picocalc_NESco.uf2`を確認し、必要fileのpushとGitHub Release作成を承認した
- 公開には確認済みUF2と同一byte列を使用し、公開直前の再buildでは差し替えない
- ローカル／SD card側は`Picocalc_NESco.uf2`、GitHub Release assetだけ
  `Picocalc_NESco-1.2.0.uf2`とする
