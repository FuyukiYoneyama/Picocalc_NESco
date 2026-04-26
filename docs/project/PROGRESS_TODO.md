# Picocalc_NESco 進捗入口

この文書は互換性維持のために残す入口です。
現在の正本は、用途ごとに次の文書へ分離しています。

## 読む順序

1. `README.md`
   - 公開向け概要、build、操作、制約。
2. `docs/project/TASKS.md`
   - 現在の未着手 / 進行中タスク。
3. `docs/project/ARCHITECTURE.md`
   - 現在の構成、責務境界、互換性確認の基準。
4. `docs/project/CONVENTIONS.md`
   - 作業手順、build、version、確認ルール。
5. `docs/project/PLANS.md`
   - 現在参照すべき計画書、設計文書の索引。
6. `docs/project/Picocalc_NESco_HISTORY.md`
   - 完了済み作業、採用 / 不採用判断、過去の経緯。

## 運用

- 新しい作業予定は `docs/project/TASKS.md` に書く。
- 完了した作業は `docs/project/TASKS.md` から外し、結果つきで `docs/project/Picocalc_NESco_HISTORY.md` へ移す。
- 長い設計や作業計画は `docs/` 以下へ置き、`docs/project/PLANS.md` から参照する。
- 作業規約は `docs/project/CONVENTIONS.md` を正本とする。
- agent 向けの入口は `AGENTS.md` を正本とする。
