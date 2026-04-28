# PLANS.md

この文書は、現在参照すべき計画書と設計文書の索引です。
新しい長い計画をここへ直接書くのではなく、詳細計画は `docs/` 以下へ置き、この文書から参照します。

## 正本計画

1. `docs/design/INFONES_PLATFORM_CONNECTION_PLAN_20260419.md`
   - `infones` ベース移行の正本計画。
   - `infones` と PicoCalc platform の接続設計。
2. `docs/design/MAPPER_DYNAMIC_ALLOCATION_PLAN_20260419.md`
   - mapper 動的確保移行の正本計画。
   - 共用 union 撤去と release 共通化の設計。
3. `docs/design/PPU_LCD_PIPELINE_REDESIGN_20260421.md`
   - `PPU` 出力から `LCD` までの高速化設計。
4. `docs/design/BG_DRAWLINE_HOTPATH_REDESIGN_20260421.md`
   - `InfoNES_DrawLine()` 背景描画高頻度経路の再設計。
5. `docs/design/BG_TILE_RENDER_LUT_REDESIGN_20260421.md`
   - `renderBgTile()` の bit 展開 LUT 化設計。
6. `docs/design/BG_TILE_PREPROCESS_REUSE_REDESIGN_20260421.md`
   - attribute / palette base 解決の再利用設計。
7. `docs/design/NES_STRETCH_VIEW_TOGGLE_REDESIGN_20260421.md`
   - `256x240` と `320x300` の実行時切替設計。
8. `docs/audio/AUDIO_OUTPUT_GAIN_REDESIGN_20260422.md`
   - 音量調整の正本設計。
9. `docs/audio/AUDIO_POP_SUPPRESSION_POWERON_INIT_PLAN_20260422.md`
   - 電源 ON 時 1 回 init による音声 pop 抑制設計。
10. `docs/design/GITHUB_ACTIONS_BUILD_CI_PLAN_20260426.md`
   - GitHub Actions による最小 build CI 導入設計。

## 現在必要な計画

- Core1 活用
  - 計画: `docs/design/CORE1_PARALLELIZATION_WORK_PLAN_20260428.md`
  - Pico の core1 を使った UX 改善と LCD worker 化の進め方。
  - 初回実装候補は `core1 keyboard polling`。

## 完了済み計画 / 結果

- ROM menu screenshot BMP viewer
  - 計画: `docs/design/SCREENSHOT_VIEWER_WORK_PLAN_20260427.md`
  - ROM menu から `0:/screenshots/*.BMP` を選択して表示する機能。
  - 2026-04-28 に `main` へ取り込み済み。
- GitHub Actions 最小 build CI 導入
  - 設計: `docs/design/GITHUB_ACTIONS_BUILD_CI_PLAN_20260426.md`
  - 実装: `.github/workflows/build.yml`
  - clean configure / build / UF2・ELF artifact 保存までを自動確認対象とする。
  - 実機 smoke、release 作成、tag 発行、release publish は対象外。
  - 2026-04-26 に実装。
- `1.0.0` release gate
  - 判定基準: `docs/release/RELEASE_GATE_1_0_0.md`
  - `0.4.5` smoke 結果を AS-IS 根拠として採用し、
    `1.0.0` では build / artifact / 文書 / git 状態の整合性を中心に確認する。
  - 2026-04-26 に作成済み。
- 公開準備方針
  - 計画: `docs/release/PUBLIC_RELEASE_PREP_PLAN_20260426.md`
  - 結果:
    - `core/README.md`
    - `docs/release/FINAL_CODE_REVIEW_20260426.md`
    - `docs/release/RELEASE_ARTIFACT_PROCEDURE.md`
    - `docs/release/RELEASE_BUILD_CHECK_20260426.md`
  - 2026-04-26 に完了済み。
- README 機能記述と実装状態の最終照合
  - 計画: `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_PLAN_20260426.md`
  - 結果: `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_20260426.md`
  - 2026-04-26 に完了済み。
- clean build / release artifact 手順
  - 手順: `docs/release/RELEASE_ARTIFACT_PROCEDURE.md`
  - build 確認: `docs/release/RELEASE_BUILD_CHECK_20260426.md`
  - `1.0.0` build 確認: `docs/release/RELEASE_BUILD_CHECK_1_0_0.md`
  - 2026-04-26 に手順書作成と current build 確認を完了。
- 最新 `main` の最終コードレビュー
  - 結果: `docs/release/FINAL_CODE_REVIEW_20260426.md`
  - 2026-04-26 に完了済み。

## 計画を追加するときのルール

- 実装が複数 phase に分かれる場合は、先に `docs/design/` か適切な `docs/` subdir に計画書を作る。
- 計画書には、実装範囲、検証条件、実機確認の回数、失敗時の切り分けを含める。
- 完了した計画の結果は `docs/project/Picocalc_NESco_HISTORY.md` へ移す。
- `docs/project/PLANS.md` には索引と現在有効な計画だけを残す。
