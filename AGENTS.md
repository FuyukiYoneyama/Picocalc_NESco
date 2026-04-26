# AGENTS.md

この文書は、このプロジェクトで AI / Codex agent が作業するときの入口ルールです。

## 最初に読む文書

1. `README.md`
   - 公開向け概要、build、操作、制約。
2. `docs/project/TASKS.md`
   - 現在の未着手 / 進行中タスク。
3. `docs/project/ARCHITECTURE.md`
   - 現在の構成、責務境界、注意点。
4. `docs/project/CONVENTIONS.md`
   - 作業手順、build、version、確認ルール。
5. `docs/project/Picocalc_NESco_HISTORY.md`
   - 完了済み作業、採用 / 不採用判断、過去の経緯。

## 事実として報告してよいもの

事実として報告してよいのは、次のいずれかで確認できた内容だけです。

- ツール出力
- コマンド結果
- ファイル内容
- ユーザー提示内容

推論、要約、補完、記憶による内容を事実として断定しないでください。
未確認の内容を報告する場合は `【推定】` を付けてください。

## 作業時の基本姿勢

- 変更前に `git status --short` を確認する。
- ユーザーが明示的に求めた変更以外の refactor は避ける。
- 既存の未コミット変更を勝手に戻さない。
- 実機確認が必要な変更では、人間側の作業回数を減らす設計にする。
- 実機で時間がかかる作業には、画面またはログに開始、進行、終了が分かる表示を入れる。
- build 後は version と build id を生成物から確認する。
- 完了した作業は `docs/project/Picocalc_NESco_HISTORY.md` に移し、`docs/project/TASKS.md` から外す。

## 編集ルール

- 手作業のコード修正は `apply_patch` を使う。
- patch が失敗した場合は、対象コードを再読してから小さい編集単位でやり直す。
- 以前このスレッドで成功した編集単位や適用順がある場合は、それを優先して再現する。
- 大きな変更は、build 可能な単位に分ける。

## build と commit

- version は `platform/version.h` の `PICOCALC_NESCO_VERSION` を更新する。
- build ID は ELF / UF2 生成物から確認する。
- 作業完了時は、必要に応じて `README.md`、`docs/project/TASKS.md`、`docs/project/Picocalc_NESco_HISTORY.md` を更新する。
- ユーザーが明示的に不要と言わない限り、意味のある作業単位で commit する。
