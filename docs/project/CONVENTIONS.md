# CONVENTIONS.md

この文書は `Picocalc_NESco` の作業規約です。

## 文書の役割

- `README.md`
  - 公開向けの正面入口。
- `docs/project/TASKS.md`
  - 現在の未着手 / 進行中タスク。
- `docs/project/PLANS.md`
  - 現在参照すべき計画書、設計文書の索引。
- `docs/project/ARCHITECTURE.md`
  - 現在の構成と責務境界。
- `docs/project/CONVENTIONS.md`
  - 作業規約。
- `docs/project/Picocalc_NESco_HISTORY.md`
  - 完了済み作業、採用 / 不採用判断、過去の経緯。
- `docs/project/PROGRESS_TODO.md`
  - 入口リンク集。詳細は上記文書を正本とする。

## build

- `git` コマンドは `Picocalc_NESco` ディレクトリを worktree root として実行する。
- build ID を報告するときは生成物から確認する。
- 既存 build dir を再利用する通常ビルドは、`build/` で `make clean && make -j4` を使う。
- `CMakeLists.txt` や target 構成を変更した phase では、先に `cmake -S . -B build` で build graph を再生成する。
- その後、`cmake --build build -j4` または `build/` での `make -j4` を使う。
- 一部の `platform/*.c` は、CMake の `LANGUAGE CXX` 指定により C++ として build する。
  - 対象は `display.c`、`audio.c`、`input.c`、`rom_image.c`、`screenshot.c`、`screenshot_storage.c`。
  - 公開前は `.cpp` へ改名せず、現行の明示的な CMake 指定を正本とする。

## version

- version は `platform/version.h` の `PICOCALC_NESCO_VERSION` を更新する。
- build 後、ELF / UF2 生成物から version と build id を確認する。
- ユーザーに build 結果を伝えるときは、version と build id の両方を示す。
- version は `MAJOR.MINOR.PATCH` として扱う。
- `MAJOR` は、大きな構成変更や互換性に影響する変更で上げる。
  - 例: emulator core を `infones` 以外へ置き換える。
  - 例: save / ROM / firmware の扱いが従来と大きく変わる。
- `MINOR` は、既存の基本動作を保ったまま機能追加したときに上げる。
  - 例: screenshot、ROM menu 機能、対応 mapper、表示 mode などの追加。
- `PATCH` は、既存機能の bug fix、調整、文書修正で上げる。
- `1.0.0` は公開用の最初の基準版として扱う。
  - `0.4.5` で通した実機 smoke は AS-IS の確認根拠として採用する。
  - `1.0.0` へ version を変えるだけの場合、同じ実機 smoke を繰り返すことは必須にしない。
  - これは販売製品ではなく趣味 project であるため、公開直前の確認は build / artifact / 文書 / git 状態の整合性を中心にする。
- release version 更新時の順序:
  1. `platform/version.h` の `PICOCALC_NESCO_VERSION` を更新する。
  2. build する。
  3. 生成物から version と build id を確認する。
  4. `README.md` の現在 version 記述を必要に応じて更新する。
  5. `docs/project/Picocalc_NESco_HISTORY.md` に結果を記録する。
  6. 完了した項目を `docs/project/TASKS.md` から外す。
  7. commit する。
  8. release tag が必要な場合は、公開対象 commit に tag を付ける。

## branch 運用

- 公開済み `main` は、利用者が見に来てもよい安定版として扱う。
- 新機能、bug fix、実機確認が必要な変更は、原則として `main` から短命の作業 branch を作って進める。
- 常設の `develop` branch は当面作らない。
  - ひとり開発では、作業ごとの短命 branch の方が状態を追いやすい。
- branch 名は内容が分かる短い名前にする。
  - 例: `fix/rom-menu-flicker`
  - 例: `feature/screenshot-viewer`
  - 例: `mapper/mapper87-choplifter`
  - 例: `ci/build-check`
- 作業開始時の基本手順:
  1. `git switch main`
  2. `git pull`
  3. `git switch -c <branch-name>`
- 作業 branch 上で実装、build、必要な文書更新を行う。
- 実機確認用 UF2 は試作品名にする。
  - 例: `Picocalc_NESco-test.uf2`
  - 実機確認前に release 名の UF2 として扱わない。
- 現在進行中の実機確認用 UF2 は `build/` に置いてよい。
- release 作業や特別な実機試験で使い終わった UF2 は、
  project root 直下へ散らばらせず `local_uf2_archive/` に集約する。
  - `local_uf2_archive/` は管理外の退避場所であり、公開 artifact ではない。
- 実機確認が必要な変更では、確認完了前に `main` へ merge / push しない。
- 実機確認で問題があれば、同じ作業 branch 上で修正し、build と確認を繰り返す。
- 実機確認と CI が通った後に、必要なら version、README、HISTORY、TASKS を最終更新する。
- `main` への取り込み手順:
  1. `git switch main`
  2. `git pull`
  3. `git merge --no-ff <branch-name>`
  4. `git push`
- release が必要な場合だけ、`main` の公開対象 commit に tag を付ける。
- tag / GitHub Release / release UF2 添付は、実機確認済みの変更だけを対象にする。

## 実装前確認

実装前に、その変更がどの領域に属するかを分類します。

- 描画 hot path
- LCD 転送
- APU
- mapper
- 入力
- ROM
- SRAM / save
- 文書

あわせて、主目的と悪化しうる副作用を書き出します。

- 速度改善
- RAM 削減
- 見た目改善
- 互換性改善
- 操作性改善

## 実機検証コスト

- 実機検証 1 回は、人間側に約 10 分の作業コストがある前提で計画する。
- 実機検証を依頼する前に、その 1 回で何を判断できるかを明記する。
- 1 回の実機検証では、最低でも 3〜5 個の有効な判断材料を取れるようにする。
  - 例: 速度改善量、画面崩れ有無、入力 / ESC 復帰、音、ROM 差、fallback 発生数。
- 高速化や互換性リスクのある実験では、細かい中間 build ごとに実機検証を依頼しない。
  - build は小さく刻んでよいが、実機検証は複数の判断をまとめて取れる build に集約する。
- 実験 build には、可能な範囲で比較 mode、ROM 名、mode 名、判断に必要な counter を log に入れる。
- 実機検証を依頼する前に、期待リターン、リスク、手戻りコスト、採用 / 不採用の判断基準を書く。
  - 例: `500 us/frame` 以上改善なら継続、`200 us/frame` 未満なら不採用寄り、など。
- 危険な実装ほど、実機検証回数と手戻り回数が増える前提で見積もる。
  - 互換性に触れる変更では、fallback や無効化手段を用意してから実機確認へ進む。

## 実装時の注意

- 新しい LUT、配列、状態変数を追加したら、最初の使用前に必ず初期化されるか確認する。
- 初期化位置が、起動時 / ROM ロード時 / frame 開始時 / scanline 開始時のどこかを明示する。
- 初期化を log / perf / 条件付きコードに依存させない。
- 毎 pixel / 毎 scanline の処理では、分岐、関数呼び出し、メモリアクセス、バッファ境界を増やしていないか確認する。
- RAM を常時消費する静的 buffer を追加する場合は、Mapper30 起動と screenshot への影響を先に見積もる。
- ROM menu や screenshot など補助機能がゲーム起動条件を壊す場合は、補助機能側を抑制または動的確保へ移す。

## 実装後確認

実装後レビューでは、正常系だけでなく次を先に疑います。

- 起動直後
- 初回 frame
- mode 切替直後
- menu から game への遷移
- game から menu への復帰
- ROM 再選択
- reset

最低限確認は次を基準にします。

1. build
2. 起動直後
3. ROM menu
4. ROM 起動
5. 代表 ROM 1 本で画面 / 入力 / 音
6. 戻れるなら menu 戻りと再起動

## TODO と HISTORY

- `docs/project/TASKS.md` には未着手または進行中の作業予定だけを書く。
- 完了した項目は `docs/project/TASKS.md` から外し、結果とともに `docs/project/Picocalc_NESco_HISTORY.md` へ移す。
- 完了済みの履歴は `docs/project/TASKS.md` に残さない。
- 履歴、経緯、過去版比較は `docs/project/Picocalc_NESco_HISTORY.md` に書く。
