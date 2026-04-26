# Release Gate 1.0.0

この文書は、`Picocalc_NESco` を `1.0.0` として公開するかどうかを判定する
release gate の正本です。

## 方針

- `0.4.5` で実施済みの実機 smoke 結果は、AS-IS の確認根拠として採用する。
- `1.0.0` への変更が version 表記、release artifact、文書、git 状態の整理に限られる場合、
  同じ実機 smoke を再実施することは必須にしない。
- `1.0.0` 判定では、build / artifact / 文書 / git 状態の整合性を中心に確認する。
- version 変更と同時に実装変更を入れた場合は、この gate だけでは足りない。
  変更内容に応じて追加の build / 実機確認を行う。

## AS-IS 根拠として採用する `0.4.5` smoke

根拠文書:

- `docs/release/README_IMPLEMENTATION_FINAL_AUDIT_20260426.md`

確認済み build:

- version: `0.4.5`
- build id: `Apr 26 2026 08:41:10`
- banner: `PicoCalc NESco Ver. 0.4.5 Build Apr 26 2026 08:41:10`

AS-IS 根拠として採用する項目:

- PicoCalc で UF2 が起動する。
- ROM menu が表示される。
- SD ROM 一覧が表示される。
- `SYSTEM FLASH` entry が表示される。
- `H` または `?` で help が開く。
- ROM menu で `F5` screenshot が保存される。
- help 画面で `F5` screenshot が保存される。
- help 画面 screenshot 後、意図せず ROM menu へ戻らない。
- screenshot 保存中に押した key が保存後の操作として残らない。
- `DragonQuest3` が起動する。
- `DragonQuest3` で入力できる。
- `DragonQuest3` で音が出る。
- 通常表示が出る。
- `Shift+W` で stretch 表示へ切り替わる。
- もう一度 `Shift+W` で通常表示へ戻る。
- game 中に `F5` screenshot が保存される。
- screenshot BMP が PC で開ける。
- representative `*.srm` が作成 / 更新される。
- 再起動または再ロード後に representative `*.srm` が復元される。
- `ESC` で ROM menu に戻れる。

`DragonQuest3` の確認に使った ROM 識別子:

- iNES header: `4e 45 53 1a 10 00 12 00 00 00 00 00 00 00 00 00`
- PRG: `16 x 16KB = 256KB`
- CHR: `0`
- mapper: `1`
- battery SRAM: `true`
- trainer: `false`

## `1.0.0` 公開前に必須確認する項目

### version / 文書

- `platform/version.h` の `PICOCALC_NESCO_VERSION` が `1.0.0` である。
- `README.md` の現在 version 記述が `1.0.0` である。
- `docs/project/Picocalc_NESco_HISTORY.md` に `1.0.0` build / artifact 結果が記録されている。
- `docs/project/TASKS.md` に完了済み release 作業が残っていない。
- `docs/project/PLANS.md` が現在必要な計画と完了済み結果を正しく指している。

### build / artifact

`docs/release/RELEASE_ARTIFACT_PROCEDURE.md` に従って確認する。

必須確認:

- `cmake --build build -j4` が成功する。
- `build/` がない場合、または CMake cache が現行環境と合わない場合は、
  `cmake -S . -B build` の後に build する。
- `build/Picocalc_NESco.elf` が存在する。
- `build/Picocalc_NESco.uf2` が存在する。
- `strings build/Picocalc_NESco.elf | grep 'PicoCalc NESco Ver\\.' | head -n 1`
  で `PicoCalc NESco Ver. 1.0.0 Build ...` が確認できる。
- `arm-none-eabi-size build/Picocalc_NESco.elf` の結果を記録する。
- `sha256sum build/Picocalc_NESco.uf2 build/Picocalc_NESco.elf` の結果を記録する。
- release 用 UF2 を `Picocalc_NESco-1.0.0.uf2` として作成する。
- `Picocalc_NESco-1.0.0.uf2` の SHA-256 を記録する。

### 混入チェック

tracked file に ROM / save / 圧縮 ROM 候補がないこと:

```sh
git ls-files | rg -in '\.(nes|fds|srm|m30|zip|7z|rar)$' || true
```

合格条件:

- 上記 command が match しない。
- README 用正式画像だけが tracked である。
- `docs/images/readme_candidates/.gitkeep` は候補 directory 維持用として許可する。
- `docs/images/readme_candidates/*.png` などの候補画像は tracked にしない。

### git / tag

- `git status --short` が clean である。
- release commit が決まっている。
- tag は release commit に付ける。
- tag 名は `v1.0.0` を基本とする。
- GitHub Release の artifact と tag の commit が対応している。

### release note

GitHub Release note には最低限次を含める。

- version: `1.0.0`
- release commit
- build id
- `Picocalc_NESco-1.0.0.uf2` の SHA-256
- source archive の扱い
- `0.4.5` smoke 結果を AS-IS 根拠として採用したこと
- ROM は同梱しないこと
- 未確認事項として残す項目

## `1.0.0` 公開 blocker にしない項目

次の項目は未確認事項として残すが、`1.0.0` 公開前 blocker にはしない。

- Mapper 動的確保済み mapper の実機確認
  - `Map6`
  - `Map19`
  - `Map185`
  - `Map188`
  - `Map235`
- Mapper30 の `*.m30` 保存 / 復元を実ゲームで確認すること。
  - ROM 起動と表示は実機確認済み。
  - 未確認なのは PRG flash overlay の書き込み / 復元。
- Mapper87 / Choplifter 系の確認。
  - 別の Mapper87 ROM 入手後に再開する。
- audio ring size を `4096` から `2048` へ下げられるかの再評価。
- 音量調整 redesign。
- 256 表示で平均 60fps を目指す追加高速化。

## 合格条件

`1.0.0` 公開可と判定する条件:

- version / README / HISTORY / TASKS / PLANS が `1.0.0` 公開状態として整合している。
- build が成功している。
- ELF banner から `1.0.0` と build id が確認できる。
- UF2 artifact が `Picocalc_NESco-1.0.0.uf2` として作成されている。
- SHA-256 が記録されている。
- ROM / save / 圧縮 ROM 候補が tracked file に混入していない。
- `git status --short` が clean である。
- release tag と artifact の commit が対応している。
- version 変更以外の新しい実装変更を入れていない、または入れた場合は追加確認を済ませている。

## 不合格条件

次のいずれかに該当する場合は、`1.0.0` 公開を止める。

- build に失敗する。
- ELF banner が `1.0.0` になっていない。
- README と `platform/version.h` の version が食い違う。
- release 用 UF2 が作成されていない。
- SHA-256 が記録されていない。
- tracked file に ROM / save / 圧縮 ROM 候補が混入している。
- `git status --short` が clean ではない。
- tag と artifact の commit が食い違う。
- version 変更以外の新しい実装変更により、`0.4.5` AS-IS smoke 根拠をそのまま使えなくなっている。
