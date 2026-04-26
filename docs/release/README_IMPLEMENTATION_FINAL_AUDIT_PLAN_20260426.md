# README と実装状態の最終照合計画

この文書は、公開前に `README.md` の記述と実装状態を最終照合するための作業計画です。

目的は、README に書いた機能が次のいずれの状態なのかを、読者に誤解が出ない粒度で固定することです。

- 実装済み、かつ実機確認済み
- 実装済み、ただし実機確認は未完
- 実装上は存在するが、公開 README では制約として書くべきもの
- README から削るべき、または表現を弱めるべきもの

## 対象

- `README.md`
- `platform/`
- `infones/`
- `drivers/`
- `CMakeLists.txt`
- `docs/project/TASKS.md`
- `docs/project/Picocalc_NESco_HISTORY.md`

対象外:

- 新機能の実装
- mapper 互換性そのものの追加検証
- host build の復旧
- `1.0.0` tag 作成

## 前提

- この照合は、最新 `main` の clean な作業ツリーで行う。
- 事実として扱うのは、ファイル内容、build 出力、実機ログ、ユーザーが提示した実機結果だけに限定する。
- 推論で補う場合は、照合結果側で `推定` と明記する。
- README の記述を強くするのは、実装根拠と確認根拠が両方そろった場合だけにする。
- 実装根拠はあるが実機確認がない項目は、README では `未確認` または `確認未完` と書く。

## 成果物

1. `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_YYYYMMDD.md`
   - README の主張ごとの照合表。
   - 照合表は次の列を必須にする。

     | column | 内容 |
     | --- | --- |
     | `README line` | README の該当行。複数行の場合は範囲を書く。 |
     | `claim` | README が読者へ伝えている主張。 |
     | `implementation evidence` | 実装根拠。file path と必要なら symbol 名を書く。 |
     | `build evidence` | build 出力、version、build id、生成物の根拠。 |
     | `latest-device evidence` | 最新 build での実機確認結果。未実施なら `未実施` と書く。 |
     | `history evidence` | 過去の HISTORY やユーザー提示結果を根拠にする場合の参照。 |
     | `status` | `latest-device confirmed` / `history confirmed` / `implemented unverified` / `unsupported` / `needs README change` のいずれか。 |
     | `README action` | `変更なし`、`表現を弱める`、`制約へ移す`、`削除`、`追記` のいずれか。 |
     | `task action` | `なし`、`TASKS へ残す`、`別計画へ分離` のいずれか。 |
2. 必要な場合のみ `README.md` の修正 patch。
3. 必要な場合のみ `docs/project/TASKS.md` の残項目更新。
4. 必要な場合のみ `docs/project/Picocalc_NESco_HISTORY.md` の履歴追記。
5. commit。

## Phase 1: README 主張の抽出

`README.md` を上から読み、機能・状態・制約・手順を次の粒度に分ける。

| 区分 | 例 | 照合方法 |
| --- | --- | --- |
| version / build | 埋め込み version、build id banner | `platform/version.h`、build 出力、生成物文字列 |
| 対象環境 | PicoCalc 専用、host build 無効 | `CMakeLists.txt`、README 記述 |
| ROM menu | SD ROM 選択、flash 常駐 entry、キー操作 | `platform/rom_menu.c`、`platform/rom_image.c`、実機 smoke |
| 表示 | `256x240`、`320x300`、`Shift+W` 切替 | `platform/display.c`、`platform/input.c`、実機 smoke |
| screenshot | game / ROM menu / help で `F5` 保存 | `platform/screenshot.c`、`platform/screenshot_storage.c`、`platform/rom_menu.c`、実機 smoke |
| save | `*.srm` 保存 / 復元 | `platform/sram_store.cpp`、実機 smoke |
| Mapper30 | 起動 / 表示、`*.m30` 保存 / 復元 | `infones/mapper/InfoNES_Mapper_030.cpp`、`platform/sram_store.cpp`、既存実機記録 |
| dynamic mapper | `Map6` `Map19` `Map185` `Map188` `Map235` dynamic 化 | `infones/InfoNES_Mapper.cpp` と各 mapper file |
| runtime log | default で verbose log 無効 | `CMakeLists.txt`、log macro 設定、build 出力 |
| license / ROM | ROM 非同梱、各 license 概要 | `LICENSE`、README、repo 内ファイル |
| 関連文書 | docs path | `test -f` または `rg` |

抽出結果には、README の行番号、主張、照合対象ファイル、必要な実機確認を必ず書く。

## Phase 2: 静的照合

次をコマンドで確認し、照合表へ転記する。

```bash
git status --short
rg -n "PICOCALC_NESCO_VERSION|PICOCALC_NESCO_BANNER" platform/version.h platform/display.c platform/rom_menu.c
rg -n "PICO_SDK_PATH|Host build is disabled|pico_add_extra_outputs|NESCO_RUNTIME_LOGS" CMakeLists.txt
rg -n "picocalc_rom_menu|SYSTEM FLASH|rom_image_menu_entries|rom_image_load" platform/rom_menu.c platform/rom_image.c
rg -n "Shift\\+W|PAD_SYS_VIEW_TOGGLE|display_set_stretch|NES_VIEW_STRETCH|NES_VIEW_W" platform/input.c platform/display.c
rg -n "nesco_request_screenshot|nesco_take_screenshot_now_with_stem|storage_build_screenshot_path|screenshots" platform/screenshot.c platform/screenshot_storage.c platform/rom_menu.c infones/InfoNES.cpp
rg -n "\\.srm|\\.m30|Map30" platform/sram_store.cpp infones/mapper/InfoNES_Mapper_030.cpp
rg -n "Map6_Release|Map19_Release|Map185_Release|Map188_Release|Map235_Release|Map30_Release" infones/InfoNES_Mapper.cpp infones/mapper
git ls-files | rg -in "\\.(nes|fds|srm|m30|zip|7z|rar)$" || true
git ls-files docs/images
test -f docs/images/rom_menu.png
test -f docs/images/mapper30_tower_normal.png
test -f docs/images/mapper30_tower_stretch.png
```

判定:

- ファイル内容で実装を確認できたものは `実装根拠あり` とする。
- `git ls-files | rg -in "\\.(nes|fds|srm|m30|zip|7z|rar)$"` が一致した場合は、ROM / save 系ファイル、または ROM / save を含みうる圧縮ファイルが意図せず tracked になっていないか確認し、README の ROM 非同梱記述と矛盾しないか判定する。
- 圧縮ファイルが一致した場合は、ファイル名だけで判断せず、中身に ROM / save 系 file が含まれていないか確認する。
- `docs/images/readme_candidates/` は候補置き場なので、公開 README が参照する正式画像は `docs/images/rom_menu.png`、`docs/images/mapper30_tower_normal.png`、`docs/images/mapper30_tower_stretch.png` に限定する。
- `docs/images/readme_candidates/.gitkeep` は候補置き場を維持するための例外として tracked を許可する。
- README が `実機確認済み` と書いている項目は、今回の最新 build での実機 smoke 結果を第一根拠にする。
- 過去の HISTORY またはユーザー提示結果だけを根拠にする項目は、照合表の `status` を `history confirmed` にし、README では必要に応じて `過去に確認済み` または `最新 build では未再確認` と読める表現へ弱める。
- 実装根拠しかないものは、README 上では `実装済みだが確認未完` より強く書かない。

## Phase 3: build 照合

通常 build を 1 回行う。

`build/` が存在し、CMake cache が現行環境と合っている場合:

```bash
cmake --build build -j4
```

`build/` が存在しない場合、または CMake cache が現行環境と合わない場合:

```bash
cmake -S . -B build
cmake --build build -j4
```

CMakeLists.txt や target 構成に差分がある場合も、先に `cmake -S . -B build` で build graph を再生成してから build する。

確認項目:

- build が成功すること
- `arm-none-eabi-size` が表示されること
- build 後に version と build id banner が表示されること
- `Picocalc_NESco.uf2` が生成されること
- README の埋め込み version と `platform/version.h` が一致すること

この phase では version を変更しない。

## Phase 4: 実機 smoke

実機確認は原則 1 回にまとめる。

1 回の実機確認で見る項目:

1. 起動時 banner
   - version
   - build id
2. ROM menu
   - SD ROM 一覧
   - `SYSTEM FLASH` entry 表示
   - `H / ?` help
   - ROM menu で `F5` screenshot
   - `0:/screenshots/NESCO_*.BMP` が増えること
   - help 画面で `F5` screenshot
   - help 画面で `F5` screenshot 後も、意図しない画面遷移が起きないこと
   - help 画面 screenshot 後も `0:/screenshots/NESCO_*.BMP` が増えること
   - ROM menu / help screenshot 保存中に押したキーが、保存後の操作として残らないこと
3. ROM 起動
   - 最新 build の `*.srm` save / restore 確認を兼ねる場合は、代表 ROM を `DragonQuest3` に固定する
   - `DragonQuest3` の ROM 識別子として、照合結果に `ROM display name`、`SD path`、`SHA-256`、`header mapper`、`battery SRAM 有無` を記録する
   - `DragonQuest3` を起動できない場合は、照合結果を `history confirmed` または `implemented unverified` に落とし、README の `DragonQuest3` 実機確認済み表現を Phase 5 で見直す
   - 入力
   - 音
   - 通常表示
4. view toggle
   - `Shift+W` で stretch 表示へ切替
   - もう一度 `Shift+W` で通常表示へ復帰
5. game 中 screenshot
   - 通常表示または stretch 表示で `F5`
   - 起動中 ROM 名 stem の `0:/screenshots/<ROM_STEM>_*.BMP` が増えること
   - 保存された BMP が PC または PicoCalc 側で開けること
6. menu 復帰
   - `ESC` で ROM menu へ戻る
7. save / restore
   - README の `DragonQuest3` 実機確認済み表現を維持する場合は、`DragonQuest3` で `*.srm` の保存 / 復元を確認する
   - `DragonQuest3` の `*.srm` が ROM と同じ場所、または README に書いた保存先規則どおりに作成 / 更新されること
   - README で `実機確認済み` と書き続ける場合は、原則として今回の最新 build で再確認する
   - 今回再確認しない場合は、照合結果では `history confirmed` として記録し、README で `実機確認済み` と断定し続けてよいかを Phase 5 で判断する

Mapper30 の `*.m30` 保存 / 復元は、README では未確認扱いなので、この最終照合の必須実機項目には入れない。
ただし、起動と表示の画像掲載根拠として Mapper30 ROM の起動 / 通常表示 / stretch 表示を再確認できるなら、同じ実機確認内で追加してよい。

## Phase 5: README 修正判定

照合表を見て、README を次の基準で修正する。

- 実装も実機確認もある
  - `対応しています`、`実機確認済み` と書いてよい。
- 実装はあるが実機確認がない
  - `実装済みですが、実機確認は未完です` と書く。
- 実装はあるが制約が大きい
  - `既知の制約` に移す。
- 実装根拠が見つからない
  - README から削るか、`確認中` へ落とす。
- 画像や文言が機能範囲を誤解させる
  - 画像キャプションまたは前後の文を修正する。

特に確認する表現:

- `日常的に試せる build`
- `基本的なセーブデータ保存 / 復元`
- `Mapper30` の起動 / 表示と `*.m30` 保存 / 復元の分離
- `dynamic 化済み` と `実機確認未完` の分離
- `PicoCalc 向け以外の build は未検証なので無効化`
- ROM 非同梱と合法的に用意した ROM の利用
- README に掲載している画像が動作例であり、ROM 同梱や公式サンプルを意味しないこと
- `persist`、`active path` など、公開文書として伝わりにくい英語混じり表現が残っていないこと
- `runtime log` の default 状態

## Phase 6: 文書更新と commit

README に修正が出た場合:

1. `README.md` を修正する。
2. 完了した照合結果を `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_YYYYMMDD.md` に保存する。
3. `docs/project/TASKS.md` から完了分を外す。
4. `docs/project/Picocalc_NESco_HISTORY.md` に結果を追記する。
5. `git diff --check` を行う。
6. commit する。

README 修正が不要だった場合:

1. 照合結果だけを保存する。
2. `docs/project/TASKS.md` と HISTORY に、README 最終照合を完了したことを書く。
3. `git diff --check` を行う。
4. commit する。

## 合格条件

- README の各機能記述に、実装根拠または未確認注記がある。
- `実機確認済み` と書いた項目は、最新 build の実機 smoke で裏取りできる。
- HISTORY だけを根拠にした項目は、README 上で最新 build 確認済みと誤読されない。
- 未確認 mapper / 未確認 save は、README で未確認と明示されている。
- README 内の画像が、ROM 同梱や Mapper30 固有機能を誤解させない。
- README が参照する画像 file が存在し、正式画像だけが tracked である。
- `docs/images/readme_candidates/.gitkeep` 以外の候補画像が tracked になっていない。
- ROM / save 系 file、または ROM / save を含む圧縮 file が意図せず tracked になっていない。
- ROM menu / help / game 中 screenshot は、操作だけでなく BMP file の作成まで確認できている。
- 関連文書への path がすべて存在する。
- build 手順が現行 CMake 構成と一致している。
- `git status --short` が、意図した文書差分だけを示している。

## 実機検証回数

- 必須: 1 回
- 任意: 1 回
  - Mapper30 の起動 / 表示画像の根拠を今回再採取する場合だけ追加する。
  - `*.m30` 保存 / 復元を確認する場合は別計画に分ける。

## 失敗時の切り分け

- build 失敗
  - README では `すぐ使うには` と build 手順に関わるため、release blocker として扱う。
- 実機 smoke 失敗
  - 失敗項目だけを README で `確認中` または `既知の制約` に落とす。
  - 原因修正はこの計画に含めず、別タスクに切り出す。
- README と実装が食い違う
  - 原則 README を現状へ合わせる。
  - 公開に必要な機能が欠けている場合だけ、release blocker として `docs/project/TASKS.md` に残す。
