# Release Artifact Procedure

この文書は、GitHub Release に置く成果物を作る手順です。

## 対象成果物

- `Picocalc_NESco-<version>.uf2`
- GitHub tag 由来の source archive
- 必要な場合のみ `git archive` 由来の `Picocalc_NESco-<version>-source.zip`
- release note
- 必要に応じて SHA-256 一覧

## 前提

- worktree が clean であること。
- `platform/version.h` の `PICOCALC_NESCO_VERSION` が release version と一致していること。
- `README.md` の現在 version 記述が release version と一致していること。
- `docs/project/Picocalc_NESco_HISTORY.md` と `docs/project/TASKS.md` が更新済みであること。
- release tag を付ける commit が決まっていること。

## clean build

通常 build:

```sh
cmake --build build -j4
```

`build/` がない場合、または CMake cache が現行環境と合わない場合:

```sh
cmake -S . -B build
cmake --build build -j4
```

`CMakeLists.txt` や target 構成を変更した場合も、先に `cmake -S . -B build` を実行する。

## build 確認

確認する生成物:

```sh
test -f build/Picocalc_NESco.elf
test -f build/Picocalc_NESco.uf2
```

version / build id:

```sh
strings build/Picocalc_NESco.elf | grep 'PicoCalc NESco Ver\\.' | head -n 1
```

size:

```sh
arm-none-eabi-size build/Picocalc_NESco.elf
```

hash:

```sh
sha256sum build/Picocalc_NESco.uf2 build/Picocalc_NESco.elf
```

## UF2 artifact

Release 用 UF2 は、build 出力を version 付きの名前でコピーする。

```sh
cp build/Picocalc_NESco.uf2 Picocalc_NESco-<version>.uf2
sha256sum Picocalc_NESco-<version>.uf2
```

例:

```sh
cp build/Picocalc_NESco.uf2 Picocalc_NESco-1.0.0.uf2
sha256sum Picocalc_NESco-1.0.0.uf2
```

## local UF2 archive

Release 作業や特別な実機試験で使い終わった UF2 は、
project root 直下や各所に残さず `local_uf2_archive/` に集約する。

`local_uf2_archive/` は `.gitignore` 対象であり、GitHub Release や source archive には含めない。

現在進行中の build / 実機試験で使っている UF2 は、従来どおり `build/` に置いてよい。
使い終わった時点で、必要なら `local_uf2_archive/` へ移動する。

例:

```sh
mkdir -p local_uf2_archive
mv Picocalc_NESco-<old-version>.uf2 local_uf2_archive/
mv Picocalc_NESco-test-*.uf2 local_uf2_archive/
```

## source archive

基本は GitHub が release tag から自動生成する source archive を使う。

手動で source archive を添付する場合は、必ず `git archive` 由来にする。
worktree の未追跡 file を zip してはいけない。

```sh
git archive --format=zip --prefix=Picocalc_NESco-<version>/ -o Picocalc_NESco-<version>-source.zip HEAD
sha256sum Picocalc_NESco-<version>-source.zip
```

## 混入チェック

tracked file に ROM / save / 圧縮 ROM 候補がないこと:

```sh
git ls-files | rg -in '\.(nes|fds|srm|m30|zip|7z|rar)$' || true
```

README 用正式画像と候補置き場:

```sh
git ls-files docs/images
```

合格条件:

- `docs/images/rom_menu.png`
- `docs/images/mapper30_tower_normal.png`
- `docs/images/mapper30_tower_stretch.png`
- `docs/images/readme_candidates/.gitkeep`

上記以外の `docs/images/readme_candidates/*` が tracked になっていないこと。

手動 source archive を作る場合は、次を含めない:

- `build/`
- `local_uf2_archive/`
- `*.nes`
- `*.fds`
- `*.srm`
- `*.m30`
- `*.zip`
- `*.7z`
- `*.rar`
- local log
- copied evidence
- README 候補画像

## release tag 対応

release note には最低限次を記録する。

- release tag
- commit hash
- version
- build id
- UF2 file name
- UF2 SHA-256
- source archive の扱い

## GitHub Release に置くもの

- `Picocalc_NESco-<version>.uf2`
- GitHub tag 由来 source archive
- 手動 source archive を添付する場合は `Picocalc_NESco-<version>-source.zip`
- release note
- 必要に応じて SHA-256 一覧
