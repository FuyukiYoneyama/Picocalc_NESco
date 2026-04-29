# Mapper152 implementation plan

作成日: 2026-04-29

対象 project: `Picocalc_NESco`

想定 branch: `mapper/mapper152-bandai74161`

## 目的

未実装の iNES Mapper 152 を追加し、Mapper152 ROM が `Mapper #152 is unsupported.` で止まらないようにする。

Mapper152 は Bandai / Taito 系の discrete mapper で、代表例は `Arkanoid 2 (J)`、`Gegege no Kitarou 2` である。

## 調査で確認した事実

### 現行 Picocalc_NESco

- `infones/InfoNES_Mapper.cpp` の `MapperTable` に mapper `152` は登録されていない。
- `infones/InfoNES_Mapper.h` に `Map152_Init()` / `Map152_Write()` 宣言はない。
- `infones/mapper/InfoNES_Mapper_152.cpp` は存在しない。
- そのため、現状では mapper 152 ROM は unsupported mapper として扱われる。

### NESdev 仕様

Mapper152 の register は `$8000-$FFFF` write の 1 byte である。

```text
$8000-$FFFF: [MPPP CCCC]
M    = mirroring
       0: one-screen A
       1: one-screen B
PPP  = PRG bank, 16KB at $8000
CCCC = CHR bank, 8KB at $0000
```

PRG layout:

```text
$8000-$BFFF: selected 16KB PRG bank
$C000-$FFFF: last 16KB PRG bank fixed
```

CHR layout:

```text
$0000-$1FFF: selected 8KB CHR bank
```

Mapper152 has bus conflicts according to NESdev.

### 参考実装

- `Map70` は 16KB PRG bank + 8KB CHR bank の切り替え実装が近い。
- Mesen2 は mapper 152 を `Bandai74161_7432(true)` として扱い、mapper 70/152 共通実装の mirroring control 有効版にしている。
- fixNES の `m152_setParams()` は次の形である。
  - PRG: `(val >> 4) & 7`
  - CHR: `val & 0x0F`
  - mirroring: bit7 で single lower / single upper

## 実装方針

初回実装は `Map70` をベースにした小さな mapper 追加に限定する。

追加するもの:

- `infones/mapper/InfoNES_Mapper_152.cpp`
- `Map152_Init()`
- `Map152_Write(WORD wAddr, BYTE byData)`
- `InfoNES_Mapper.h` への宣言追加
- `InfoNES_Mapper.cpp` の mapper include list に `InfoNES_Mapper_152.cpp` 追加
- `InfoNES_Mapper.cpp` の `MapperTable` に `{152, Map152_Init}` 追加

変更しないもの:

- CPU / PPU / APU timing
- Mapper70 の挙動
- Mapper78 / Mapper151 の挙動
- ROM loader
- save / SRAM
- input / display

## 初回実装での register 解釈

`Map152_Write()` は `$8000-$FFFF` write data を次のように解釈する。

```cpp
BYTE byChrBank = byData & 0x0f;
BYTE byPrgBank = (byData & 0x70) >> 4;
bool screen_b = (byData & 0x80) != 0;
```

PRG:

- `byPrgBank` は 16KB PRG bank number として扱う。
- `ROMBANK0 = ROMPAGE(byPrgBank * 2)`
- `ROMBANK1 = ROMPAGE(byPrgBank * 2 + 1)`
- `ROMBANK2 = ROMLASTPAGE(1)`
- `ROMBANK3 = ROMLASTPAGE(0)`
- page index は既存 mapper と同じく `(NesHeader.byRomSize << 1)` で modulo する。

CHR:

- `byChrBank` は 8KB CHR bank number として扱う。
- `byChrBank <<= 3`
- `PPUBANK[0..7] = VROMPAGE(byChrBank + 0..7)`
- page index は `(NesHeader.byVRomSize << 3)` で modulo する。
- `InfoNES_SetupChr()` を呼ぶ。

Mirroring:

- `byData & 0x80` が 0 なら one-screen A。
- `byData & 0x80` が 1 なら one-screen B。
- 現行 `InfoNES_Mirroring()` は `2 = One Screen 0x2400`、`3 = One Screen 0x2000` である。
- 初回実装では、Mapper152 の bit7 を次のように固定する。
  - `bit7 == 0`: one-screen A / lower / `0x2000` として `InfoNES_Mirroring(3)` を呼ぶ。
  - `bit7 == 1`: one-screen B / upper / `0x2400` として `InfoNES_Mirroring(2)` を呼ぶ。
- この対応は、`Map70` の bit7 true -> `InfoNES_Mirroring(2)`、false -> `InfoNES_Mirroring(3)` の既存 convention とそろえる。

## bus conflict の扱い

NESdev では Mapper152 は bus conflict ありとされる。

初回実装では bus conflict を再現しない。

理由:

- 現行 `Map70` も bus conflict を明示再現していない。
- Picocalc_NESco の mapper 実装は、まず既存 InfoNES の粒度に合わせて mapper 対応を増やす方が安全である。
- bus conflict 再現は ROM ごとの不具合が確認された場合に別 patch とする。

HISTORY には、Mapper152 初回実装では bus conflict 未再現であることを明記する。

## version 方針

Mapper 対応追加は利用可能 ROM を増やす機能追加である。

ただし今回の変更は小規模 mapper 追加であり、現行 `1.1.x` 系の継続作業として扱う。

実装時は `platform/version.h` を次の patch version へ更新する。

- 現在が `1.1.21` なら `1.1.22`

README の現在 version も合わせる。

## 実装手順

1. `main` から `mapper/mapper152-bandai74161` branch を作る。
2. `InfoNES_Mapper.h` に `Map152_Init()` / `Map152_Write()` 宣言を追加する。
3. `infones/mapper/InfoNES_Mapper_152.cpp` を追加する。
   - 既存 `InfoNES_Mapper_070.cpp` の形式を踏襲する。
   - `Map152_Init()` では callback を `Map70` と同じ基本形にする。
   - 初期 PRG は `0, 1, last-1, last` にする。
   - CHR ROM がある場合は初期 `PPUBANK[0..7]` を `0..7` にする。
4. `infones/InfoNES_Mapper.cpp` の mapper include list に `#include "mapper/InfoNES_Mapper_152.cpp"` を追加する。
   - 位置は `#include "mapper/InfoNES_Mapper_151.cpp"` の直後、`#include "mapper/InfoNES_Mapper_160.cpp"` の前にする。
   - 現行 project は mapper cpp を `InfoNES_Mapper.cpp` から直接 include する構成なので、`CMakeLists.txt` の変更は不要。
5. `InfoNES_Mapper.cpp` の `MapperTable` に `{152, Map152_Init}` を追加する。
   - 位置は `{151, Map151_Init}` の直後、`{160, Map160_Init}` の前にする。
6. `platform/version.h` と `README.md` を `1.1.22` に更新する。
7. build する。
8. build artifact から banner / build id / UF2 SHA-256 / ELF SHA-256 / size を確認する。
9. 可能なら mapper 152 ROM で実機確認する。
   - ROM が手元にない場合は、build と mapper table 登録確認までを完了扱いにし、実機確認は TASKS に残す。
10. `docs/project/Picocalc_NESco_HISTORY.md` に結果を記録する。
11. `docs/project/TASKS.md` に未確認事項があれば追記する。
12. commit する。

## build 確認

最低 1 回:

```sh
cmake --build build -j4
```

確認項目:

- build 成功
- `strings build/Picocalc_NESco.elf | rg "PicoCalc NESco Ver"`
- `rg "\{152, Map152_Init\}" infones/InfoNES_Mapper.cpp`
- `rg "InfoNES_Mapper_152.cpp" infones/InfoNES_Mapper.cpp`
- `arm-none-eabi-size build/Picocalc_NESco.elf`
- `sha256sum build/Picocalc_NESco.uf2 build/Picocalc_NESco.elf`

## 実機確認

Mapper152 ROM がある場合、1 回の実機確認で次を見る。

- ROM menu から mapper 152 ROM を起動できる。
- unsupported mapper error が出ない。
- title / gameplay 画面が表示される。
- 入力できる。
- 音が出る。
- F5 screenshot が撮れる。
- ESC で ROM menu に戻れる。
- `[ROM_START]` に mapper `152` が出る。

Mapper152 ROM がない場合:

- 実機確認は未実施として記録する。
- TASKS に `[pending] Mapper152 ROM 実機確認` を追加する。
- 可能なら ROM header parser などで mapper 152 ROM の header 確認だけ行う。

## 合格条件

Mapper152 ROM がある場合:

- unsupported mapper error が消える。
- 起動 / 表示 / 入力 / 音 / ESC 復帰に明らかな破綻がない。
- mapper 0 / 30 など既知正常 ROM の起動を壊していない。

Mapper152 ROM がない場合:

- build が通る。
- `MapperTable` に `152` が登録されている。
- 既存 mapper の build に影響がない。
- 実機未確認であることが HISTORY / TASKS に明記されている。

## 手戻り

この変更は 1 commit にまとめる。

問題が出た場合はその commit を revert する。

bus conflict や mirroring の解釈違いが疑われる場合は、Mapper152 実装 commit は維持したまま、別 commit で補正するか、実機確認結果に応じて revert する。

## リスク

- mirroring の A/B 対応が逆の場合、画面が崩れる可能性がある。
- bus conflict 未再現により、一部 ROM が実機と違う bank value を使う可能性がある。
- CHR RAM ROM には基本対応しない前提だが、Mapper152 代表 ROM は CHR ROM 系なので初回範囲では問題になりにくい。

## 今回やらない追加調査

- bus conflict の汎用再現 helper 化
- Mapper70 / 152 の共通 helper 化
- NES 2.0 submapper 対応
- Mesen2 DB にある全 Mapper152 ROM の個別確認
