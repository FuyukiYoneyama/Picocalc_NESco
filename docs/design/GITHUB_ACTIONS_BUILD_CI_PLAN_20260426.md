# GitHub Actions Build CI 設計 2026-04-26

## 目的

`Picocalc_NESco`
に、
最小限の
GitHub Actions
based CI
を追加し、
push / pull request
のたびに
「少なくとも clean build が通る」
ことを
自動確認できる状態へ持っていく。

今回の初期導入では、
実機確認、
release 作成、
tag 発行、
release publish
までは扱わない。

## この文書で事実として扱う範囲

この文書では、
repo 内ファイル内容、
コマンド出力、
ユーザー提示内容、
公式ドキュメント確認結果として
明示的に確認できたものだけを
事実として書く。
未確認の見通しや選択理由は
`【推定】`
を付ける。

## 現状の事実

### 1. 現在 workflow は存在しない

事実:
- `docs/`
  配下には
  release / design / test / project 文書がある。
- 今回の確認では
  `.github/workflows/*.yml`
  は存在しない。

### 2. build は Pico SDK 前提で、host build は無効

事実:
- top-level
  `CMakeLists.txt`
  では、
  `PICO_SDK_PATH`
  が未定義なら
  `PICO_BUILD OFF`
  とし、
  その後
  `message(FATAL_ERROR "... Host build is disabled ...")`
  で停止する。
- 同 file では、
  `project(Picocalc_NESco C CXX ASM)`
  を宣言している。

### 3. CI で build したい target と生成物は明確

事実:
- `CMakeLists.txt`
  では
  `add_executable(Picocalc_NESco ...)`
  を使っている。
- 同 file では
  `pico_add_extra_outputs(Picocalc_NESco)`
  を呼んでいる。
- `README.md`
  の build 節では、
  想定生成物を次としている。
  - `build/Picocalc_NESco.elf`
  - `build/Picocalc_NESco.uf2`

### 4. 現在の手動 build 手順は README に書かれている

事実:
- `README.md`
  の build 節では、
  初回 build 例として次を案内している。
  - `cmake -S . -B build -DPICO_SDK_PATH=/path/to/pico-sdk`
  - `cmake --build build -j4`
- `docs/project/CONVENTIONS.md`
  では、
  `CMakeLists.txt`
  や target 構成変更時は
  `cmake -S . -B build`
  を先に実行するとしている。

### 5. build 後に size / banner を確認する流れがすでにある

事実:
- `CMakeLists.txt`
  には
  `POST_BUILD`
  custom command
  があり、
  `arm-none-eabi-size`
  と
  `strings ... | grep 'PicoCalc NESco Ver\\.'`
  を実行する。
- `README.md`
  でも、
  build 後に
  `arm-none-eabi-size`
  と
  version / build id banner
  を自動表示すると書いている。

### 6. GitHub Actions artifact の保存は公式に可能

事実:
- GitHub Docs
  の
  "Store and share data with workflow artifacts"
  では、
  `actions/upload-artifact@v4`
  を使って
  file / directory
  を保存できる。
- 同 doc では、
  artifact に
  `retention-days`
  を指定できる。

## 初期導入の範囲

今回の CI でやること:

- Ubuntu runner で build 環境を作る
- Pico SDK を workflow 内で取得する
- `cmake -S . -B build`
  を clean 実行する
- `cmake --build build -j4`
  を実行する
- `build/Picocalc_NESco.elf`
  と
  `build/Picocalc_NESco.uf2`
  の存在を確認する
- build 生成物を artifact 保存する

今回の CI でやらないこと:

- 実機 smoke
- screenshot 動作確認
- save / restore 動作確認
- release note 生成
- tag 作成
- release publish
- 複数 board matrix

## 設計方針

### A. 最初の CI は「壊れていないか」の確認に限定する

方針:
- この phase の成功条件は
  「clean checkout から build が通る」
  ことに置く。
- 実機依存の確認は
  CI 成功条件に入れない。

理由:
- 現在の repo は
  PicoCalc 専用 firmware
  であり、
  実際の画面・入力・音・save は
  hardware 依存が大きい。
- 一方で、
  include 漏れ、
  CMake 設定破損、
  link failure
  は CI だけで早期検出できる。

### B. local build 手順を CI 上でそのまま再現する

方針:
- CI 用に独自 shell script を新設せず、
  まずは
  `README.md`
  に近い command
  を workflow にそのまま書く。
- configure と build を
  2 step
  に分ける。

理由:
- local 手順と CI 手順が分かれると、
  どちらが正本か曖昧になりやすい。
- まずは
  「README に書いた build が GitHub 上でも通る」
  ことを担保する方が価値が高い。

### C. Pico SDK は workflow 内 checkout で供給する

方針:
- CI job 内で
  `actions/checkout`
  により repo 自体を取得する。
- 別 step で
  `raspberrypi/pico-sdk`
  を checkout し、
  `PICO_SDK_PATH`
  をその path に向ける。
- `pico-sdk`
  checkout では
  submodule も必ず取得する。
  実装時は
  `actions/checkout@v4`
  に
  `repository: raspberrypi/pico-sdk`
  `ref: 2.2.0`
  `path: pico-sdk`
  `submodules: recursive`
  を指定する。
- 最初の CI では
  `pico-sdk`
  ref を
  `2.2.0`
  に固定する。
- ローカルで現在使っている
  `pico-sdk`
  は
  `2.2.0`
  であり、
  commit は
  `a1438dff1d38bd9c65dbd693f0e5db4b9ae91779`
  である。

【推定】:
- submodule 化よりも、
  現在の repo 変更量を増やさず導入できる。
- tag
  `2.2.0`
  を使う方が、
  commit SHA
  だけを書くより実装者が意図を読みやすい。

### D. artifact は UF2 / ELF のみ保存する

方針:
- build 成功時の artifact は
  次に限定する。
  - `build/Picocalc_NESco.uf2`
  - `build/Picocalc_NESco.elf`
- `build/`
  directory 全体は保存しない。

理由:
- 必要な確認対象は
  firmware binary
  本体だけである。
- object file
  や
  CMake cache
  を含めると、
  artifact が大きくなりすぎる。

### E. trigger は `push` / `pull_request` / `workflow_dispatch` に絞る

方針:
- まずは
  `main`
  への
  `push`
  と
  `pull_request`
  で発火させる。
- `workflow_dispatch`
  も初期導入に含める。

理由:
- 継続開発で必要な基本線は
  push / PR
  自動確認で足りる。
- 初回導入時は、
  workflow を手動再実行できる方が、
  空 commit や不要な push を増やさずに調整しやすい。

## 実装ステップ設計

### Step 1. workflow 前提条件を固定する

やること:
- CI target を
  `Picocalc_NESco`
  1 本に固定する。
- runner を
  `ubuntu-latest`
  に固定する。
- `pico-sdk`
  の取得先を
  `raspberrypi/pico-sdk`
  に固定する。
- `pico-sdk`
  ref を
  `2.2.0`
  に固定する。

成果物:
- workflow header
- 使用 action version
- `pico-sdk`
  ref:
  `2.2.0`

### Step 2. build 環境 step を設計する

やること:
- checkout step を置く。
- toolchain / build tool の install step を置く。
- `pico-sdk`
  checkout step を置き、
  submodule を recursive に初期化する。
- `PICO_SDK_PATH`
  を後続 step で使えるよう渡す。

install package は最初の実装では次に固定する:

- `cmake`
- `ninja-build`
- `gcc-arm-none-eabi`
- `libnewlib-arm-none-eabi`
- `libstdc++-arm-none-eabi-newlib`
- `build-essential`
- `python3`
- `git`

理由:
- この repo は
  `project(Picocalc_NESco C CXX ASM)`
  で C++ を含む。
- 一部の
  `platform/*.c`
  は
  CMake の
  `LANGUAGE CXX`
  指定により C++ として build する。
- そのため、
  C++ link に必要な
  `libstdc++-arm-none-eabi-newlib`
  を最初から含める。

参考:
- Raspberry Pi Pico SDK
  の quick-start 系手順では、
  Ubuntu package として
  `cmake`
  `ninja-build`
  `gcc-arm-none-eabi`
  `libnewlib-arm-none-eabi`
  `libstdc++-arm-none-eabi-newlib`
  などを使う。

【推定】:
- runner の preinstall 状態次第で
  一部 package は省略可能だが、
  初期導入では明示 install の方が安定する。

### Step 3. clean configure step を設計する

やること:
- `build/`
  を使い回さない。
- `cmake -S . -B build -DPICO_SDK_PATH=...`
  を毎回 clean 実行する。
- generator は
  `Ninja`
  に固定する。

具体式:
- `cmake -S . -B build -G Ninja -DPICO_SDK_PATH="$GITHUB_WORKSPACE/pico-sdk"`

### Step 4. build step を設計する

やること:
- `cmake --build build -j4`
  を実行する。
- build failure 時は workflow failed とする。

補足:
- 現在の
  `POST_BUILD`
  command により、
  size と banner は log に出る。
- そのため最初の workflow では、
  size 専用 command を別に足さなくてもよい。

### Step 5. 生成物検査 step を設計する

やること:
- 次の存在確認を shell で行う。
  - `build/Picocalc_NESco.elf`
  - `build/Picocalc_NESco.uf2`
- banner の文字列確認を入れる。

確認対象:
- CI では
  `PicoCalc NESco Ver.`
  prefix
  の存在だけを確認する。
- version 値そのものは CI では固定しない。

具体式:
- `strings build/Picocalc_NESco.elf | grep 'PicoCalc NESco Ver\\.' | head -n 1`
  が 1 行以上返ることを確認する。
- `grep '1.0.0'`
  のような固定 version check は入れない。

理由:
- version 固定チェックまで workflow に入れると、
  将来 version 更新時に毎回 workflow 編集が要る。
- CI と release gate の責務は分ける。

### Step 6. artifact upload step を設計する

やること:
- `actions/upload-artifact@v4`
  で
  `.uf2`
  と
  `.elf`
  を保存する。
- artifact 名を固定する。

案:
- `picocalc-nesco-firmware`

upload path:
- `build/Picocalc_NESco.uf2`
- `build/Picocalc_NESco.elf`

付加方針:
- retention は短めでよい。
- 【推定】`7`
  日程度で十分。

### Step 7. 文書整備 step を設計する

やること:
- `README.md`
  の build 節に、
  「GitHub Actions では build 自動確認のみ」
  を短く追記する。
- `docs/project/TASKS.md`
  から未完タスクとして参照できるようにする。
- `docs/project/PLANS.md`
  へ設計書索引を追加する。

## workflow 構成案

file:
- `.github/workflows/build.yml`

job:
- `build-firmware`

step 順:
1. checkout repository with `actions/checkout@v4`
2. install toolchain / build packages
3. checkout `pico-sdk` with `actions/checkout@v4`
   - `repository: raspberrypi/pico-sdk`
   - `ref: 2.2.0`
   - `path: pico-sdk`
   - `submodules: recursive`
4. configure
5. build
6. verify outputs
7. upload artifact

## 初回実装時の確認項目

実装後に確認すること:

1. `pull_request` で workflow が起動する
2. `main` への push で workflow が起動する
3. `workflow_dispatch` で workflow を手動実行できる
4. configure が clean checkout から通る
5. build が通る
6. `Picocalc_NESco.elf`
   と
   `Picocalc_NESco.uf2`
   が artifact に入る
7. job log に size / banner が出る

## 不合格条件

次のいずれかに当たる場合は、
初期 CI 導入は未完成と判断する。

- `PICO_SDK_PATH`
  解決に失敗し configure が落ちる
- runner 上で必要 package が不足し build できない
- `Picocalc_NESco.uf2`
  が生成されない
- artifact upload まで到達しない
- `README`
  と CI の build 手順が乖離する

## 今回やらない拡張

今回の設計からは外すが、
将来的に分離して検討する項目:

- warning を error 化する job
- release tag 時だけ artifact 名を release 用に変える job
- `README`
  の version 記述と
  `platform/version.h`
  の整合チェック
- `clang-format`
  や
  static analysis
- self-hosted runner
  での実機 smoke

## 設計結論

最初に入れるべき CI は、
「Ubuntu runner 上で Pico SDK を取得し、
clean configure / build を行い、
UF2 / ELF を artifact 保存する」
1 workflow
で十分である。

これにより、
実機がなくても
継続開発中の build breakage
を自動検出できる。
一方で、
実機依存の互換性や操作確認は
従来どおり人手 smoke
を正本として扱う。
