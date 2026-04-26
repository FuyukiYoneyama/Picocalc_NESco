# 公開準備方針計画

この文書は、`Picocalc_NESco` を公開へ進める前に残っている作業の扱いを固定する計画です。

## 目的

- 公開前に必要な作業と、未確認事項として残す作業を分ける。
- `1.0.0` 公開時に、過度な再実機確認を要求しない。
- GitHub Release に置く成果物と、その確認方法を固定する。
- `core/` と `platform/*.c` の扱いを、公開前に迷わない形にする。

## 前提

- `0.4.5` の README 最終照合 smoke は完了済み。
- 結果は `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_20260426.md` を根拠にする。
- `1.0.0` へ version を変えるだけの場合、同じ実機 smoke を再実施することは必須にしない。
- この project は販売製品ではなく趣味 project であるため、公開前確認は build / artifact / 文書 / git 状態の整合性を中心にする。

## 公開前にこちらで実施できる作業

### 1. clean build / release artifact 手順を固める

`docs/release/` に release artifact 手順書を作る。

含める内容:

- clean checkout または clean build 相当の開始条件。
- `build/` がない場合、または CMake cache が現行環境と合わない場合の `cmake -S . -B build`。
- `cmake --build build -j4` による build。
- `build/Picocalc_NESco.uf2` の生成確認。
- ELF / UF2 からの version / build id 確認。
- UF2 の SHA-256 記録。
- GitHub Release 用 artifact 名。
  - 例: `Picocalc_NESco-1.0.0.uf2`
- source archive の作り方。
  - 基本は GitHub が release tag から自動生成する source archive を使う。
  - 手動で添付する場合は `git archive` 由来に限定し、worktree の未追跡 file を含めない。
  - 手動 archive 名の例: `Picocalc_NESco-1.0.0-source.zip`
- 手動 source archive で除外または混入確認するもの。
  - `build/`
  - ROM / save 系 file: `*.nes`, `*.fds`, `*.srm`, `*.m30`
  - 圧縮 ROM / save 候補: `*.zip`, `*.7z`, `*.rar`
  - local log / copied evidence
  - `docs/images/readme_candidates/` の候補画像
- release tag と artifact の対応。

GitHub Release に置く想定物:

- `Picocalc_NESco-1.0.0.uf2`
- GitHub tag 由来の source archive、または `git archive` 由来の `Picocalc_NESco-1.0.0-source.zip`
- release note
- 必要に応じて SHA-256 一覧

### 2. 最新 `main` の最終コードレビュー

実機確認ではなく、コード上の release blocker を探す review として実施する。

確認観点:

- 初期化順
- 起動直後
- ROM menu から game への遷移
- game から ROM menu への復帰
- mode 切替直後
- save / load
- screenshot
- mapper release / 再起動
- hot path
- 常時 RAM 使用量
- release blocker の有無

成果物:

- finding があれば inline review または修正 plan。
- finding がなければ、HISTORY に「最終コードレビューで release blocker は確認されなかった」と記録する。

### 3. 公開向けコメント整理

`display.h` / `display.c` を中心に、古いコメントや現状と食い違うコメントを整理する。

対象:

- viewport
- normal / stretch 表示
- line buffer
- DMA buffer
- screenshot / readback との関係
- hot path に関係する注意

方針:

- 実装を変えず、コメントと用語を整える。
- 公開 README の説明と矛盾しない表現にする。
- コメント整理で挙動変更は行わない。

## 未確認事項として残す作業

次の項目は、`1.0.0` 公開前の blocker にしない。
README / release note / TASKS では未確認事項として扱う。

### Mapper 動的確保済み mapper の実機確認

- `Map6`
- `Map19`
- `Map185`
- `Map188`
- `Map235`

扱い:

- 実装済みだが、対応 ROM の実機確認は未完了。
- 公開時は未確認 mapper として残す。

### Mapper30 の `*.m30` 保存 / 復元

確認済み:

- Mapper30 ROM の起動。
- Mapper30 ROM の normal / stretch 表示。

未確認:

- PRG flash overlay の `*.m30` 書き込み。
- `*.m30` 復元。
- 実ゲームでの save / restore 運用。

扱い:

- `1.0.0` 公開前の blocker にしない。
- 未確認事項として `TASKS.md` に残す。

## `core/` の扱い

方針:

- `core/` は残す。
- 現在の active build 対象ではない。
- 経緯確認用の旧系統として扱う。
- 公開前に `core/README.md` を追加する。

経緯として HISTORY に残す内容:

- `core/` は、MIT license の独自 emulator core を目指していた初期実装である。
- 当時は `infones` の仕様書を起こし、機能のみを記述した clean-room 開発を意図していた。
- しかし仕様書に `infones` 固有名詞などが残っていたことが判明した。
- そのため、仕様書が本当に機能だけを記述したものだったか疑義が出た。
- 結果として、`core/` の source が必ずしも clean-room から書き起こされたとは言い切れなくなった。
- この理由により、`core/` 系統の開発は discontinued 扱いにした。
- 現在の active emulator core は `infones` である。

`core/README.md` に書く内容:

- `core/` は現在の build 対象ではない。
- active source tree ではない。
- 再利用可能な clean-room emulator core として扱う場合は、別途確認が必要。
- 詳細な経緯は `docs/project/Picocalc_NESco_HISTORY.md` を参照する。

## `platform/*.c` の C++ 扱い

方針:

- 今回は `.cpp` へ改名しない。
- 現行の `LANGUAGE CXX` 運用で固定する。

対象:

- `platform/display.c`
- `platform/audio.c`
- `platform/input.c`
- `platform/rom_image.c`
- `platform/screenshot.c`
- `platform/screenshot_storage.c`

理由:

- CMake で C++ として build することは明示済み。
- 既に `extern "C"` など C++ 前提の記述がある。
- 公開直前に `.cpp` 改名を行うと、変更範囲が不要に広がる。
- ファイル名変更は挙動変更を伴わないが、履歴追跡と review 範囲を広げる。

公開前に行うこと:

- `docs/project/ARCHITECTURE.md` または `docs/project/CONVENTIONS.md` に、`platform/*.c` の一部は CMake の `LANGUAGE CXX` により C++ として build する運用であることを明記する。
- `.cpp` 改名は、公開後の整理候補として扱う。

## 実行順

1. この計画を `docs/project/PLANS.md` へ登録する。
2. `core/README.md` を追加し、HISTORY に経緯を記録する。
3. `platform/*.c` の `LANGUAGE CXX` 運用を `ARCHITECTURE.md` / `CONVENTIONS.md` に明記する。
4. 公開向けコメント整理を行う。
5. 最新 `main` の最終コードレビューを実施する。
6. release artifact 手順書を作る。
7. 最終 build / artifact 作成を行う。
8. `TASKS.md` から完了した項目を外し、HISTORY に結果を移す。

## 合格条件

- 公開前に実施する作業と、未確認事項として残す作業が分離されている。
- `core/` の扱いが文書化されている。
- `platform/*.c` の C++ build 運用が文書化されている。
- release artifact の作成手順が文書化されている。
- 未確認 mapper / `*.m30` は release blocker ではなく、未確認事項として残っている。
