# README と実装状態の最終照合結果

この文書は `README.md` の記述と、2026-04-26 時点の実装 / build / 実機確認状態を照合した結果です。

## 実施状態

- 静的照合: 完了
- build 照合: 完了
- 実機 smoke: 完了
- README 修正判定: 修正不要

## build evidence

- build command: `cmake --build build -j4`
- result: success
- generated files:
  - `build/Picocalc_NESco.elf`
  - `build/Picocalc_NESco.uf2`
- `build/Picocalc_NESco.uf2` size: `527872` bytes
- `arm-none-eabi-size build/Picocalc_NESco.elf`:
  - `text=266220`
  - `data=0`
  - `bss=92772`
- embedded banner from ELF:
  - `PicoCalc NESco Ver. 0.4.5 Build Apr 26 2026 08:41:10`

## static checks

- `git status --short`: clean before static/build work.
- tracked ROM / save / compressed files:
  - command: `git ls-files | rg -in "\\.(nes|fds|srm|m30|zip|7z|rar)$" || true`
  - result: no matches.
- README images:
  - `docs/images/rom_menu.png`: exists / tracked
  - `docs/images/mapper30_tower_normal.png`: exists / tracked
  - `docs/images/mapper30_tower_stretch.png`: exists / tracked
  - `docs/images/readme_candidates/.gitkeep`: tracked exception
  - other candidate images: no tracked files observed.
- related documents:
  - `docs/project/TASKS.md`: exists
  - `docs/project/CONVENTIONS.md`: exists
  - `docs/project/ARCHITECTURE.md`: exists
  - `docs/project/PLANS.md`: exists
  - `AGENTS.md`: exists
  - `docs/project/Picocalc_NESco_HISTORY.md`: exists
  - `docs/design/INFONES_PLATFORM_CONNECTION_PLAN_20260419.md`: exists
  - `docs/design/MAPPER_DYNAMIC_ALLOCATION_PLAN_20260419.md`: exists
  - `LICENSE`: exists

## latest-device evidence

ユーザー提示結果:

- PicoCalc で UF2 起動: 確認
- 起動画面に `PicoCalc NESco Ver. 0.4.5`: 確認
- build id `Apr 26 2026 08:41:10`: file / UART log で確認
- ROM menu: 確認
- SD ROM 一覧: 確認
- `SYSTEM FLASH` entry: 確認
- `H` または `?` help: 確認
- ROM menu screenshot: 確認
- help screenshot: 確認
- help screenshot 後に意図しない ROM menu 遷移がないこと: 確認
- screenshot 保存中に押した key が保存後の操作として残らないこと: 確認
- `DragonQuest3` boot: 確認
- input: 確認
- audio: 確認
- normal view: 確認
- `Shift+W` stretch view: 確認
- `Shift+W` normal view return: 確認
- game screenshot: 確認
- BMP open on PC: 確認
- representative `*.srm` create / update: 確認
- save restore after reboot / reload: 確認
- `ESC` return to ROM menu: 確認

UART log / file evidence:

- local UART log copy contains `PicoCalc NESco Ver. 0.4.5 Build Apr 26 2026 08:41:10`.
- `NESCO_0004.BMP` through `NESCO_0007.BMP` exist and are BMP files.
- `DragonQuest3_0001.BMP` through `DragonQuest3_0007.BMP` exist and are BMP files.
- All checked BMP files are reported by `file` as `PC bitmap, Windows 3.x format, 320 x -320 x 24`.
- representative `*.srm` evidence exists, size `8192` bytes.

`DragonQuest3` ROM check:

- ROM display name: `DragonQuest3`
- iNES header: `4e 45 53 1a 10 00 12 00 00 00 00 00 00 00 00 00`
- PRG ROM: `16 x 16KB = 256KB`
- CHR ROM: `0 x 8KB = 0KB`
- header mapper: `1`
- battery SRAM: `true`
- trainer: `false`

Representative `*.srm` evidence:

- size: `8192` bytes

## audit table

| README line | claim | implementation evidence | build evidence | latest-device evidence | history evidence | status | README action | task action |
| --- | --- | --- | --- | --- | --- | --- | --- | --- |
| 3-4 | PicoCalc 向け NES emulator firmware で、LCD / I2C keyboard / PWM audio / SD / flash ROM menu に接続している。 | `CMakeLists.txt:42-61` lists platform sources and drivers. `platform/rom_menu.c:499` has `picocalc_rom_menu`. | build success. | PicoCalc boot / ROM menu / DragonQuest3 smoke confirmed by user. | none | latest-device confirmed | 変更なし | なし |
| 6 | embedded version は `0.4.5`。 | `platform/version.h:11` defines `PICOCALC_NESCO_VERSION "0.4.5"`. | ELF strings include `PicoCalc NESco Ver. 0.4.5 Build Apr 26 2026 08:41:10`. | UART log and user report confirm `0.4.5`. | none | latest-device confirmed | 変更なし | なし |
| 7-9 | PicoCalc 専用 firmware で、PicoCalc 向け以外の build は未検証 / 無効。 | `CMakeLists.txt:79-80` issues fatal error when `PICO_SDK_PATH` is unavailable. | build uses Pico SDK path through existing cache. | PicoCalc target boot confirmed. | none | latest-device confirmed | 変更なし | なし |
| 13-14 | 日常的に試せる build。特殊 mapper ROM は継続確認中。 | Multiple feature evidence rows below. | build success. | ROM menu, screenshots, DragonQuest3 boot/input/audio/view/save/restore confirmed. | none | latest-device confirmed | 変更なし | なし |
| 16-23 | README images are examples; ROM files are not bundled. | tracked image files exist. ROM / save / compressed tracked check has no matches. | n/a | n/a | none | latest-device confirmed | 変更なし | なし |
| 27 | `infones` based NES emulation connected as PicoCalc firmware. | `CMakeLists.txt:83` adds `infones`; `CMakeLists.txt:122-134` links Pico / hardware libs. | build success. | DragonQuest3 gameplay smoke confirmed. | none | latest-device confirmed | 変更なし | なし |
| 28-29 | ROM menu can select ROMs, supports SD ROM loading and flash resident entry. | `platform/rom_menu.c:499`, `platform/rom_image.c:734`, `platform/rom_image.c:93` / `100` show `SYSTEM FLASH`. | build success. | SD ROM list and `SYSTEM FLASH` confirmed by user. | none | latest-device confirmed | 変更なし | なし |
| 30 | `ESC` returns from game to menu. | `platform/input.c:183` / `191` handle ESC; `platform/main.c:105-111` loops menu / session. | build success. | ESC return to ROM menu confirmed by user. | none | latest-device confirmed | 変更なし | なし |
| 31-33 | `256x240` normal and `320x300` stretch; `Shift+W` toggles. | `platform/input.c:182` sets `PAD_SYS_VIEW_TOGGLE`; `infones/InfoNES.cpp:1112` handles toggle; `platform/display.c:83-86` defines stretch viewport. | build success. | normal / stretch / return to normal confirmed by user. | none | latest-device confirmed | 変更なし | なし |
| 34 / 129-132 | `*.srm` save / restore supported; `DragonQuest3` confirmed. | `platform/sram_store.cpp:111-118` builds `.srm` / `.m30` paths; `platform/sram_store.cpp:121-124` uses `ROM_SRAM`. | build success. | representative `*.srm` evidence exists, size `8192`; save / restore confirmed by user. | none | latest-device confirmed | 変更なし | なし |
| 35 / 131-132 / 136 | Mapper30 ROM boot/display confirmed; `.m30` save / restore unverified. | `infones/mapper/InfoNES_Mapper_030.cpp` implements Map30; `platform/sram_store.cpp:157-278` implements `.m30` restore / flush. | build success. | not part of latest smoke. | previous user reports confirmed Mapper30 boot/display before this audit. | history confirmed | 変更なし | `*.m30` save/restore は TASKS に残す |
| 36 / 137 | Map6 / Map19 / Map185 / Map188 / Map235 dynamic 化済み、対象 ROM 実機確認未完。 | `infones/InfoNES_Mapper.cpp:37-58` releases dynamic mapper resources; each mapper has `Map*_Release`. | build success. | 未実施 | none | implemented unverified | 変更不要見込み | TASKS に残す |
| 37 / 112-113 | runtime log default は banner 1 行目以外 disable。 | `CMakeLists.txt:24` has `NESCO_RUNTIME_LOGS OFF`; `CMakeLists.txt:108-112` only defines runtime logs when option is on. | build success. | UART log contains banner only in provided log. | none | latest-device confirmed | 変更なし | なし |
| 39-50 | build UF2, place in `pico1-apps`, put `.nes` on SD, start from ROM menu; ROM not bundled. | `.uf2` generated by `pico_add_extra_outputs` at `CMakeLists.txt:136-137`; ROM tracked check has no matches. | `build/Picocalc_NESco.uf2` exists. | UF2 boot and ROM menu start confirmed by user. | none | latest-device confirmed | 変更なし | なし |
| 54-59 | ROM menu keys: Up/Down, Enter/-, H/?, F5 screenshot. | `platform/rom_menu.c:576-593` handles F5; help handling begins at `platform/rom_menu.c:593`. | build success. | ROM menu / help / F5 screenshots / no unwanted help transition confirmed. | none | latest-device confirmed | 変更なし | なし |
| 61-71 | in-game controls including ESC, F1, F5, Shift+W. | `platform/input.c` maps controls; screenshot request is in `infones/InfoNES.cpp:1083`; view toggle in `infones/InfoNES.cpp:1112`. | build success. | input, F5 screenshot, Shift+W, ESC confirmed by user. | none | latest-device confirmed | 変更なし | なし |
| 73-108 | CMake + Pico SDK build; target / UF2 outputs. | `CMakeLists.txt:8-20`, `79-80`, `88-91`, `136-145`. | build success; UF2 exists. | n/a | none | latest-device confirmed | 変更不要見込み | なし |
| 119-125 | save path rules for SD and flash staged ROMs. | `platform/sram_store.cpp:126-134` resolves flash source path; fallback save dir support appears at `platform/sram_store.cpp:143-155`. | build success. | representative `*.srm` creation / update confirmed by user; exact SD-side save path was not separately captured in copied evidence. | none | latest-device confirmed | 変更なし | なし |
| 142-154 | license summary. | `LICENSE`, `infones/doc/GPL2`, `fatfs/LICENSE.txt`, `font/LICENSE.txt` exist. | n/a | n/a | none | latest-device confirmed | 変更なし | なし |
| 156-165 | related document paths. | all listed documents exist. | n/a | n/a | none | latest-device confirmed | 変更不要見込み | なし |

## required latest-device smoke inputs

- UF2 used:
  - artifact: `build/Picocalc_NESco.uf2`
  - version: `0.4.5`
  - build id: `Apr 26 2026 08:41:10`
- ROM menu:
  - SD ROM list: confirmed by user
  - `SYSTEM FLASH` entry: confirmed by user
  - help: confirmed by user
  - ROM menu screenshot file: `NESCO_0004.BMP` through `NESCO_0007.BMP` copied evidence
  - help screenshot file: `NESCO_0004.BMP` through `NESCO_0007.BMP` copied evidence
  - key discard after screenshot: confirmed by user
- `DragonQuest3` ROM identity:
  - ROM display name: `DragonQuest3`
  - header mapper: `1`
  - battery SRAM: `true`
- `DragonQuest3` smoke:
  - boot: confirmed by user
  - input: confirmed by user
  - audio: confirmed by user
  - normal view: confirmed by user
  - stretch view: confirmed by user
  - return to normal: confirmed by user
  - game screenshot file: `DragonQuest3_0001.BMP` through `DragonQuest3_0007.BMP` copied evidence
  - ESC to menu: confirmed by user
  - `*.srm` save evidence: copied evidence, size `8192` bytes
  - `*.srm` restore: confirmed by user

## provisional conclusion

静的照合、build 照合、実機 smoke の範囲では、README を修正すべき矛盾は確認していない。
README は現状維持でよい。
