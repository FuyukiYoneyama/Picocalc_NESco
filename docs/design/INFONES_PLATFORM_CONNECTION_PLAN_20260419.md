# InfoNES 移行と platform 接続計画 2026-04-19

## 目的

`Picocalc_NESco/infones`
と
PicoCalc プラットフォームを、
`../nes1/Picocalc_InfoNES`
の実績を参考にしつつ、
`nes2`
側の既存
`platform/`
/
`drivers/`
構成へどう接続するかを整理する。

この文書では、
ファイル内容、
検索結果、
既存文書、
ユーザー指示として確認できたものだけを
事実として書く。
そこから先は
`【推定】`
を付ける。

この文書は、
旧
`INFONES_MIGRATION_PLAN_20260418.md`
の上位方針と、
旧
`docs/design/INFONES_PLATFORM_CONNECTION_PLAN_20260419.md`
の接続設計を統合した正本とする。

## 方針転換と現在の前提

事実:
- ユーザー指示で、
  現在構築している
  `Picocalc_NESco/core`
  ベース路線は中止とされた。
- ユーザー指示で、
  同 workspace の `infones`
  全体をコピーしたものを
  `Picocalc_NESco`
  の NES エミュレーター本体にする方針へ変更された。
- ユーザー指示で、
  旧目標
  「PicoCalc 上で動作する MIT ライセンスの NES エミュレーター」
  は廃棄され、
  新目標は
  「PicoCalc 上で動く、公開可能な GPL ライセンスの NES エミュレーター」
  になった。
- ユーザー指示で、
  同 workspace の `InfoNES_devtxt`
  は参照可能ディレクトリとして扱う。
- この作業で、
  同 workspace の `infones`
  ディレクトリ全体を、
  使う使わないを選別せず
  `infones`
  へ丸ごとコピーした。

## 移行対象の確認結果

### 1. `infones`

事実:
- `Picocalc_NESco/infones`
  直下には少なくとも次がある。
  - `InfoNES.cpp`
  - `InfoNES.h`
  - `InfoNES_System.h`
  - `InfoNES_pAPU.cpp`
  - `K6502.cpp`
  - `InfoNES_Mapper.cpp`
  - `mapper/`
  - `doc/GPL2`
  - `doc/GPL2J`
  - `doc/readme.html`
  - `CMakeLists.txt`
- `Picocalc_NESco/infones/CMakeLists.txt`
  では、
  `InfoNES_Mapper.cpp`
  `InfoNES_StructuredLog.cpp`
  `InfoNES_pAPU.cpp`
  `InfoNES.cpp`
  `K6502.cpp`
  が
  `infones`
  ライブラリの source として列挙されている。
- `Picocalc_NESco/infones/InfoNES_System.h`
  には、
  platform 側で実装すべき関数として少なくとも次が宣言されている。
  - `InfoNES_Menu()`
  - `InfoNES_ReadRom()`
  - `InfoNES_ReleaseRom()`
  - `InfoNES_LoadFrame()`
  - `InfoNES_PadState()`
  - `InfoNES_SoundInit()`
  - `InfoNES_SoundOpen()`
  - `InfoNES_SoundClose()`
  - `InfoNES_SoundOutput()`
  - `InfoNES_GetSoundBufferSize()`
  - `InfoNES_MessageBox()`
  - `InfoNES_Error()`
  - `InfoNES_PreDrawLine()`
  - `InfoNES_PostDrawLine()`
  - `RomSelect_PreDrawLine()`
  - `getbuttons()`
- `Picocalc_NESco/infones/InfoNES.h`
  には
  `void InfoNES_Main();`
  が宣言されている。
- `Picocalc_NESco/infones/InfoNES.cpp`
  には
  `InfoNES_Main()`
  があり、
  その中で少なくとも
  `InfoNES_Menu()`
  `InfoNES_PreDrawLine()`
  `InfoNES_PostDrawLine()`
  `InfoNES_LoadFrame()`
  `InfoNES_PadState()`
  を呼んでいる。
- `Picocalc_NESco/infones/linux/InfoNES_System_Linux.cpp`
  には Linux 向けの system 実装があり、
  `main()`
  から
  `InfoNES_Main()`
  を起動し、
  menu、ROM 読込、画面転送、pad、sound の実装を持つ。
- `Picocalc_NESco/infones/doc/readme.html`
  には
  `GPL2 ( Gnu Public License version2 )`
  という記述がある。

### 2. `InfoNES_devtxt`

事実:
- `InfoNES_devtxt`
  には少なくとも次がある。
  - `todo.txt`
  - `history1.txt`
  - `history2.txt`
  - `thread_handoff_20260406.txt`
  - `nesco_mapper_cleanroom_spec.md`
  - `Mapper30.txt`
  - `mesen2_memo.txt`
- `todo.txt`
  には、
  旧
  `Picocalc_InfoNES`
  系の作業運用、
  DART 調査方針、
  version 運用、
  Mesen2 比較方針が記録されている。
- `thread_handoff_20260406.txt`
  には、
  旧作業対象が
  旧 `Picocalc_InfoNES`
  だったこと、
  および
  `history2.txt`
  `history1.txt`
  `todo.txt`
  を毎回読む運用が書かれている。

## `Picocalc_NESco` 側の現状と置換方針

事実:
- 現在の
  `CMakeLists.txt`
  には
  `MIT License`
  と書かれている。
- 同ファイルでは、
  現在の emulator 本体 source として
  `core/nes_globals.c`
  `core/cpu.c`
  `core/ppu.c`
  `core/apu.c`
  `core/mapper.c`
  `core/mapper0.c`
  `core/mapper30.c`
  `core/nes_main.c`
  を列挙している。
- 同ファイルでは、
  PicoCalc 側 platform source として少なくとも次を列挙している。
  - `platform/main.c`
  - `platform/display.c`
  - `platform/audio.c`
  - `platform/input.c`
  - `platform/input_lcd_test.c`
  - `platform/rom_menu.c`
  - `platform/rom_image.c`
  - `drivers/lcd_spi.c`
  - `drivers/fatfs_diskio.c`
  - `drivers/pwm_audio.c`
  - `drivers/i2c_kbd.c`
  - `drivers/sdcard.c`
  - `fatfs/ff.c`
  - `fatfs/ffunicode.c`
- `docs/project/Picocalc_NESco_HISTORY.md`
  には、
  PicoCalc 側で少なくとも
  LCD bring-up、
  キーボード入力、
  ROM menu / SD browser、
  FatFs 整理
  が進んでいた記録がある。

【推定】:
- PicoCalc 固有の価値があるため、
  次を優先的に残す。
  - `platform/`
  - `drivers/`
  - `fatfs/`
  - 既存の ROM menu / SD browser 実装
  - 既存の LCD / input / audio 出力経路
- 次を
  `infones`
  ベースへ置き換える。
  - `core/`
    前提の emulator 本体
  - `core`
    前提で書かれた高速化計画の active な実装方針
  - `core`
    前提の open questions の active 部分

## 最初の統合作業単位と非目標

事実:
- `infones`
  ディレクトリ全体のコピーは完了した。
- 旧 build では
  `Picocalc_NESco`
  target が
  RP2040 firmware として定義されている。
- `infones`
  側 Linux 実装は、
  `main()`
  を含む別 platform 実装である。
- `todo.txt`
  と
  `thread_handoff_20260406.txt`
  には、
  DART 比較、
  Mesen2 との不一致点調査、
  Mapper30 調査運用がある。

【推定】:
- 最初の実装単位は、
  「source の選別コピー」ではなく、
  「全量コピー済みの
    `infones`
    を前提にした
    ビルド境界の差し替え」
  とする。
- 最初のコード変更は次の 3 点に絞るのが安全。
  1. `Picocalc_NESco`
     へ
     `infones`
     source 群を受ける directory / source list を作る。
  2. `InfoNES_System.h`
     の要求に対応する
     PicoCalc 側 system 実装ファイル群を作る。
  3. `CMakeLists.txt`
     を
     `core/*.c`
     列挙から
     `infones`
     source 列挙へ差し替える。
- 最初のビルド目標は
  「PicoCalc 側の `main` から `InfoNES_Main()` を呼べるところまで」
  とする。
- この段階では、
  まず build 成功を優先し、
  menu / ROM load / frame transfer / pad / sound を stub でも接続可能な形にそろえる。
- その後に、
  既存 PicoCalc 実装へ各 hook を順次差し戻す。
- 最初の移行段階では次を非目標とする。
  - DART 個別不具合の再調査
  - `core`
    路線の高速化継続
  - 旧 `core`
    路線の未消化課題の個別消化
  - PicoCalc 実機での性能最適化

## 先に固定する契約

### 1. C / C++ 境界

事実:
- `InfoNES_System.h`
  には
  `extern "C"`
  がない。
- 同 header には
  `bool`
  を使う
  `InfoNES_PostDrawLine(int line, bool frommenu);`
  がある。
- `infones`
  本体は
  `.cpp`
  で構成されている。
- `nes2`
  側 platform は
  現在
  `.c`
  ファイルが中心である。

【推定】:
- `InfoNES_System.h`
  を直接 include して
  hook を実装するファイルは、
  C ではなく
  C++
  としてコンパイルする前提で固定すべきである。

【推定】:
- 本計画では、
  次のファイルを
  `.cpp`
  へ寄せる前提で進める。
  - `platform/display.c`
  - `platform/audio.c`
  - `platform/input.c`
  - `platform/rom_image.c`
- 追加する session adapter も
  `.cpp`
  とする。
- `platform/main.c`
  は
  hook を持たない entry point として
  C のまま残してよい。
- `platform/main.c`
  から
  `.cpp`
  側へ入る経路は、
  `extern "C"`
  で公開した bridge header 経由に限定する。
- `drivers/*`
  と
  `fatfs/*`
  は
  C のまま維持してよい。
- 理由は、
  `InfoNES_System.h`
  の宣言を無理に C 互換へねじらずに済むためである。

【推定】:
- 本文前半では、
  現在ワークツリーに存在する実ファイル名として
  `platform/rom_image.c`
  `platform/audio.c`
  `platform/display.c`
  `platform/input.c`
  のような現行名を使う。
- 変更すべき詳細設計以降では、
  C++
  化後の実装先を示す名前として
  `platform/rom_image.cpp`
  `platform/audio.cpp`
  `platform/display.cpp`
  `platform/input.cpp`
  を使う。
- つまり、
  説明上の旧名と実装後の新名が混在して見える箇所は、
  「現状参照」と「移行後責務」を書き分けていると読むのが正しい。

### 2. ROM 起動契約

事実:
- 現行
  `platform/rom_image.c`
  では、
  `InfoNES_ReadRom(NULL)`
  は flash backend 扱いである。
- `InfoNES_Main()`
  は内部で
  `InfoNES_Menu()`
  を呼ぶ。
- `InfoNES_Load()`
  は
  `InfoNES_ReleaseRom()`
  →
  `InfoNES_ReadRom()`
  →
  `InfoNES_Reset()`
  を実行する。

【推定】:
- 本計画では、
  `NULL`
  を
  「現在選択中 ROM」
  の意味へ変更しない。
- SD / flash の両方に対して、
  menu 選択結果は
  明示的な path 文字列として保持し、
  `InfoNES_Menu()`
  から
  `InfoNES_Load(selected_path)`
  を呼ぶ契約に固定する。
- これにより、
  flash backend の
  `NULL`
  意味論を壊さずに済む。

### 2.5 C から呼ぶ bridge API

事実:
- `platform/main.c`
  は C のまま残す前提である。
- `InfoNES_Main()`
  と
  `selected_path`
  管理は
  C++
  側へ寄せる前提である。

【推定】:
- `main.c`
  から C++
  側を呼ぶ入口は、
  `extern "C"`
  の薄い bridge API として固定する。
- bridge header の正式名は
  `platform/infones_bridge.h`
  に固定する。
- このヘッダは
  C から include 可能な専用ヘッダとし、
  `main.c`
  から
  `.cpp`
  側を見る唯一の窓口にする。
- 最低限必要なのは次の 2 つである。
  - `void run_infones_session(void);`
  - `void rom_image_set_selected_path(const char *path);`

【推定】:
- 役割は次のとおり。
  - `run_infones_session()`
    は
    `InfoNES_Main()`
    を呼ぶだけの
    C callable wrapper
  - `rom_image_set_selected_path(const char *path)`
    は
    menu 選択結果を
    `rom_image`
    側の
    `selected_path`
    状態へ保存する
    C callable setter

【推定】:
- 必要なら補助として次も追加してよい。
  - `const char *rom_image_get_selected_path(void);`
  - `void rom_image_clear_selected_path(void);`

【推定】:
- `main.c`
  は
  `InfoNES.h`
  や
  `InfoNES_System.h`
  を直接 include せず、
  この bridge header だけを見る形に固定する。

### 3. セッション owner

事実:
- `platform/main.c`
  は menu ループの owner になり得る。
- `InfoNES_Main()`
  は
  `InfoNES_Menu()`
  を呼んでから
  `InfoNES_Cycle()`
  を実行し、
  最後に
  `InfoNES_Fin()`
  を呼ぶ。

【推定】:
- セッション境界の owner は次のように固定する。
  - thread / process レベルの owner:
    `platform/main.c`
  - 1 ROM session の owner:
    `InfoNES_Main()`
  - ROM 起動準備の owner:
    `InfoNES_Menu()`
    から呼ばれる
    `InfoNES_Load(selected_path)`
- つまり、
  `main.c`
  は
  `InfoNES_Load()`
  や
  `InfoNES_Reset()`
  を直接呼ばない。

### 4. host build の扱い

事実:
- 現在のトップ
  `CMakeLists.txt`
  には
  Pico SDK なしの
  host build
  `nesco_host`
  がある。
- `infones/InfoNES.cpp`
  は
  `pico.h`
  と
  `pico/time.h`
  を include している。

【推定】:
- `infones`
  ベース移行の初段では、
  `nesco_host`
  を一時停止し、
  `infones`
  device build のみを正本に固定する。
- host stub を同時に設計しない。
- 理由は、
  platform 接続の主目的が PicoCalc 実機起動であり、
  host build 互換まで同時に背負うと
  C / C++
  境界整理と
  `pico.*`
  依存吸収が遅れるためである。
- host build の再設計は、
  device build が起動する段階の後続タスクとして扱う。

## 変更すべき詳細設計

この節は、
実装時にどのファイルへ何を移し、
何を消し、
何を合わせるかを
もう一段具体化したものである。

### 1. hook 割当表

事実:
- `InfoNES_System.h`
  が要求する hook は固定されている。
- `nes2`
  側には
  `platform/main.c`
  `platform/display.c`
  `platform/audio.c`
  `platform/input.c`
  `platform/rom_menu.c`
  `platform/rom_image.c`
  がすでにある。

【推定】:
- 割当は次の形に固定するのがよい。

| `InfoNES_System.h` hook | 主担当ファイル | 補助ファイル | 方針 |
| --- | --- | --- | --- |
| `InfoNES_Menu()` | `platform/infones_session.cpp` | `platform/rom_image.cpp` | すでに選択済みの `selected_path` を使って `InfoNES_Load(selected_path)` を呼ぶ薄い adapter。`rom_menu.c` はここから呼ばず、選択は `main.c` 側で完了済みとする |
| `InfoNES_ReadRom()` | `platform/rom_image.cpp` | なし | 引数 `path` を必ず読み、渡された ROM を iNES として解析し `ROM` / `VROM` / `SRAM` へ接続 |
| `InfoNES_ReleaseRom()` | `platform/rom_image.cpp` | なし | SD heap / flash backend の後始末 |
| `InfoNES_LoadFrame()` | `platform/display.cpp` | なし | LCD DMA 完了待ちと frame skip 更新 |
| `InfoNES_PadState()` | `platform/input.cpp` | なし | joypad bit と `PAD_SYS_*` の 3 引数返却 |
| `InfoNES_SoundInit()` | `platform/audio.cpp` | なし | ring buffer 初期化のみ |
| `InfoNES_SoundOpen()` | `platform/audio.cpp` | なし | サンプルレートで PWM/DMA 出力開始 |
| `InfoNES_SoundClose()` | `platform/audio.cpp` | なし | audio stop |
| `InfoNES_SoundOutput()` | `platform/audio.cpp` | なし | 5ch mix を ring へ投入 |
| `InfoNES_GetSoundBufferSize()` | `platform/audio.cpp` | なし | `infones` 側に渡す platform buffer size |
| `InfoNES_PreDrawLine()` | `platform/display.cpp` | なし | line buffer を `infones` へ渡す |
| `InfoNES_PostDrawLine()` | `platform/display.cpp` | なし | 1 scanline を LCD strip へ flush |
| `InfoNES_DebugPrint()` | `platform/infones_session.cpp` | なし | UART / `printf` へ流す最小実装 |
| `InfoNES_MessageBox()` | `platform/infones_session.cpp` | `platform/display.cpp` | 画面表示 + UART 出力の最小実装 |
| `InfoNES_Error()` | `platform/infones_session.cpp` | `platform/display.cpp` | 致命エラー表示と停止 |
| `RomSelect_PreDrawLine()` | `platform/display.cpp` | なし | menu 期間は no-op でよい |
| `getbuttons()` | `platform/input.cpp` | なし | 既存入力取得の薄い wrapper |

### 2. `platform/main.c` の変更点

事実:
- 現在の `main.c` は
  `InfoNES_Main(rom_path)`
  を呼んでいる。
- `infones`
  側の `InfoNES_Main`
  は引数なしである。

【推定】:
- `platform/main.c`
  で変更すべき点は次のとおり。
  1. `InfoNES_Main(rom_path)` 呼び出しをやめる。
  2. menu 選択結果は
     `rom_image_set_selected_path(rom_path)`
     で
     `rom_image`
     側の
     `selected_path`
     状態へ保存する。
  3. その後
     `run_infones_session()`
     を呼ぶ。
  4. `run_infones_session()`
     の内側で
     引数なし
     `InfoNES_Main()`
     を呼ぶ。
  5. `run_infones_session()`
     が
     `PAD_SYS_QUIT`
     により戻ったら、
     再び menu ループへ戻る。
  6. reset は
     `InfoNES_Cycle()`
     内で
     `InfoNES_Reset()`
     を実行して
     同一 session を継続する前提へ合わせる。

【推定】:
- つまり
  `main.c`
  は
  `InfoNES.h`
  を直接見ず、
  bridge header が公開する
  `rom_image_set_selected_path()`
  と
  `run_infones_session()`
  だけを呼ぶ方式へ変える必要がある。

### 3. `platform/rom_menu.c` と `platform/rom_image.c` の変更点

事実:
- `picocalc_rom_menu()`
  は
  現在
  `const char *`
  の path を返す。
- `rom_image.c`
  は
  すでに
  SD / Flash backend、
  iNES header 解析、
  `InfoNES_ReadRom()`
  /
  `InfoNES_ReleaseRom()`
  を持っている。
- 現在の
  `platform/rom_image.c`
  には
  `InfoNES_Error()`
  も実装されている。

【推定】:
- 実装前提として、
  `rom_menu`
  と
  `rom_image`
  の境界を次のようにそろえる。
  1. `rom_menu`
     は
     「どの entry を選んだか」
     だけを決める。
  2. `rom_image`
     は
     「現在選択中 ROM path / storage / status」
     を保持する。
  3. `main.c`
     は
     `picocalc_rom_menu()`
     が返した path を
     `rom_image_set_selected_path()`
     へ渡す。
  4. `InfoNES_Menu()`
     は
     menu UI を再実装せず、
     すでに選択済みの ROM があることを確認して
     `InfoNES_Load(selected_path)`
     を呼ぶだけの薄い関数にする。
  5. `InfoNES_Error()`
     は
     `rom_image`
     の責務に残さず、
     `platform/infones_session.cpp`
     へ移す。

【推定】:
- `InfoNES_ReadRom()`
  は
  state を暗黙参照しない。
- `InfoNES_Load(selected_path)`
  から渡された
  引数 `path`
  のみを入力契約とし、
  `platform/rom_image.cpp`
  はその path を使って load する。
- `selected_path`
  は
  `InfoNES_Menu()`
  が
  `InfoNES_Load()`
  を呼ぶための session state であり、
  `InfoNES_ReadRom()`
  の代替入力源ではない。
- `InfoNES_Menu()`
  は
  `rom_menu.c`
  を直接呼ばない。
- ROM 選択は
  `main.c`
  →
  `picocalc_rom_menu()`
  →
  `rom_image_set_selected_path()`
  の段階で完了済みとする。
- `platform/rom_image.cpp`
  に残すのは、
  `InfoNES_ReadRom()`
  `InfoNES_ReleaseRom()`
  と
  `selected_path`
  管理までとし、
  既存
  `InfoNES_Error()`
  は移設後に削除する。

【推定】:
- 追加で必要な helper は、
  大きくても次の程度に留めるべきである。
  - `rom_image_set_selected_path(const char *path)`
  - `const char *rom_image_get_selected_path(void)`
  - `void rom_image_clear_selected_path(void)`
- `InfoNES_Menu()`
  が
  `selected_path`
  を参照する経路は、
  実質的に
  `rom_image_get_selected_path()`
  へ一本化しておくのがよい。

### 4. `platform/input.c` の変更点

事実:
- 現在の `InfoNES_PadState()`
  は 2 引数版である。
- `infones`
  は 3 引数版を要求する。
- `infones/InfoNES.cpp`
  は VBlank で
  `InfoNES_PadState(&PAD1_Latch, &PAD2_Latch, &PAD_System);`
  を呼び、
  その後
  `PAD_SYS_QUIT`
  /
  `PAD_SYS_RESET`
  を参照している。

【推定】:
- `input.c`
  では次を変更する。
  1. 2 引数版
     `InfoNES_PadState`
     をやめて
     3 引数版へ合わせる。
  2. `input_poll()`
     も、
     joypad state と system state を分離して返せる形へ広げる。
  3. `Esc`
     などの menu 復帰入力は
     `PAD_SYS_QUIT`
     にのみ反映し、
     pad1 の bit 列へ混ぜない。
  4. reset は
     `PAD_SYS_RESET`
     を使って `infones`
     本来の reset 経路へ任せる。

【推定】:
- ここは構造変更が必要だが、
  新モジュールを作る必要はなく、
  `input.c`
  の返却形を整理し直せば足りる可能性が高い。

### 5. `platform/audio.c` の変更点

事実:
- 現在の `InfoNES_SoundOpen`
  は
  `void`
  戻り値で、
  第2引数名も
  `clock_per_sync`
  になっている。
- `InfoNES_System.h`
  では
  `int InfoNES_SoundOpen(int samples_per_sync, int sample_rate);`
  である。
- `InfoNES_pAPU.cpp`
  は
  `InfoNES_SoundInit()`
  と
  `InfoNES_GetSoundBufferSize()`
  も使う。

【推定】:
- `audio.c`
  では次を変更する。
  1. `InfoNES_SoundInit()` を追加する。
  2. `InfoNES_SoundOpen()` を
     `int`
     戻り値へ合わせる。
  3. 第2引数は
     `sample_rate`
     として扱う。
  4. `InfoNES_GetSoundBufferSize()` を追加する。
  5. 既存 ring buffer / PWM 出力実装は極力そのまま使う。

【推定】:
- ここは
  シグネチャ修正と不足 hook 補完が中心で、
  モジュール分割自体を変える必要は薄い。

### 6. `platform/display.c` の変更点

事実:
- `display.c`
  は
  `g_wScanLine`
  `PPU_LineBuf`
  `g_byFrameSkip`
  `g_NesPalette`
  など、
  旧 `core`
  路線の記号を前提にしている。
- `infones`
  側では
  `NesPalette`
  `FrameSkip`
  `InfoNES_SetLineBuffer()`
  が見えている。
- `infones/InfoNES.cpp`
  は
  `InfoNES_PreDrawLine()`
  の直後に
  `InfoNES_DrawLine()`
  を呼び、
  その出力 line を
  `InfoNES_PostDrawLine()`
  に渡す流れである。

【推定】:
- `display.c`
  では次を変更する。
  1. `g_NesPalette`
     のような旧名をやめ、
     `infones`
     が参照する
     `NesPalette`
     をそのまま使う。
  2. `InfoNES_PreDrawLine()`
     では
     `g_wScanLine = PPU_LineBuf`
     のような旧 `core`
     専用前提をやめ、
     `InfoNES_SetLineBuffer()`
     を通じて
     `infones`
     側 render line の受け口へ渡す。
  3. `InfoNES_PostDrawLine()`
     のシグネチャを
     `int line, bool frommenu`
     に合わせる。
  4. frame skip は
     `g_byFrameSkip`
     ではなく
     `FrameSkip`
     を更新する。

【推定】:
- display は
  接続の考え方自体は流用できるが、
  旧 `core`
  固有名から
  `infones`
  固有名へ置き換える作業が最も多い。
- ただし、
  LCD DMA / strip buffer の実体は使い回せるので、
  全面書き換えまでは不要である。

### 7. palette / frame-skip / line-buffer の所有者

事実:
- `infones`
  は
  `NesPalette`
  と
  `FrameSkip`
  を platform から見える前提で使っている。
- `nes2`
  側 display は
  `g_NesPalette`
  と
  `g_byFrameSkip`
  という別名で管理している。

【推定】:
- 実装時の所有権は次のように固定すべきである。
  - palette の公開名:
    `NesPalette`
    を platform 側で定義する
  - frame skip:
    `FrameSkip`
    は
    `infones`
    core 側の定義を
    `extern`
    参照して更新する
  - line buffer 接続 API:
    `InfoNES_SetLineBuffer()`
    は
    `infones`
    core 側の実装を
    platform から呼ぶ
- 旧 `core`
  名
  `g_NesPalette`
  `g_byFrameSkip`
  `g_wScanLine`
  に adapter を重ねるより、
  `infones`
  の公開名へ寄せたほうが後工程が軽い。
- つまり、
  `display.cpp`
  側で
  `FrameSkip`
  や
  `InfoNES_SetLineBuffer()`
  を再定義してはいけない。

### 8. `CMakeLists.txt` で変えるべき点

事実:
- 現在の `CMakeLists.txt`
  は
  `core/*.c`
  を emulator 本体 source として列挙している。
- `Picocalc_NESco/infones/CMakeLists.txt`
  は
  現在
  `add_library(infones INTERFACE)`
  になっている。
- 同ファイルでは
  `InfoNES.cpp`
  `K6502.cpp`
  `InfoNES_pAPU.cpp`
  `InfoNES_Mapper.cpp`
  `InfoNES_StructuredLog.cpp`
  を
  `infones`
  ライブラリ source として列挙している。

【推定】:
- `CMakeLists.txt`
  では次を行うべきである。
  1. `core/*.c`
     を emulator core source から外す。
  2. `add_subdirectory(infones)`
     を使って、
     ルートから
     `infones`
     を別 target として取り込む。
  3. PicoCalc 側 executable には
     `platform/*`
     `drivers/*`
     `fatfs/*`
     を残す。
  4. 実行 target は
     `infones`
     ライブラリへ link する構成にする。
  5. hook 実装を持つ
     `platform/*`
     は
     C++
     としてコンパイルされる前提を反映する。
  6. `nesco_host`
     は初段の
     `CMakeLists.txt`
     から target として外す。
  7. host build の再開は、
     PicoCalc 実機側の
     `infones`
     接続完了後に別タスクとして再設計する。

【推定】:
- `infones/CMakeLists.txt`
  は
  `INTERFACE`
  library のまま使わず、
  `InfoNES.cpp`
  `K6502.cpp`
  `InfoNES_pAPU.cpp`
  `InfoNES_Mapper.cpp`
  `InfoNES_StructuredLog.cpp`
  と必要な mapper 群を持つ
  `STATIC`
  library として作り直すのが最も自然である。
- `InfoNES_Mapper.cpp`
  は
  `mapper/InfoNES_Mapper_*.cpp`
  を
  `#include`
  で内包する構造なので、
  `STATIC`
  library の
  `target_sources`
  には
  `InfoNES_Mapper.cpp`
  だけを入れ、
  mapper 個別
  `.cpp`
  を重ねて列挙しない。
- 理由は、
  今回の移行では
  `infones`
  を emulator core 本体として切り出したいので、
  source 伝播だけの
  `INTERFACE`
  target より、
  実体を持つ
  `STATIC`
  library のほうが
  build 境界、
  include、
  compile option、
  今後の依存追加を管理しやすいためである。
- `InfoNES.cpp`
  と
  `InfoNES_Mapper.cpp`
  は
  `pico.h`
  や
  `pico/time.h`
  に依存するため、
  `STATIC`
  化した
  `infones`
  target 側にも
  Pico SDK
  include / link の
  usage requirements
  を与える前提で進める。
- `platform/*.cpp`
  は
  `InfoNES_System.h`
  hook 実装なので
  `infones`
  側ではなく実行 target 側へ残す。

### 9. 新規モジュールを作るなら最小で何か

【推定】:
- 新規モジュールは
  1 つだけ追加するのがよい。
- それは
  `platform/infones_session.cpp`
  のような
  薄い session adapter である。

【推定】:
- この adapter には次だけを置く。
  - `run_infones_session()`
  - `InfoNES_Menu()`
  - `InfoNES_DebugPrint()`
  - `InfoNES_MessageBox()`
  - `InfoNES_Error()`
- それ以外の hook は
  既存
  `platform/*`
  に分散する。
- 既存
  `platform/rom_image.c`
  にある
  `InfoNES_Error()`
  は、
  duplicate symbol を避けるため
  この adapter への移設後に削除する。

【推定】:
- この形なら、
  `main.c`
  を entry point 専用に保ったまま、
  session 契約だけを 1 箇所に集約できる。

### 10. 実装順

【推定】:
- 実装順は次で固定するのが安全である。
  1. C / C++ 境界と
     host build 方針を
     `CMakeLists.txt`
     上で確定する。
  2. `platform/infones_bridge.h`
     を正式名で追加し、
     bridge API と
     host build 停止方針を固定する。
  3. bridge header と
     `run_infones_session()`
     /
     `rom_image_set_selected_path()`
     を先に固定する。
  4. `main.c`
     の起動形を
     bridge API
     前提へ変える。
  5. `rom_menu`
     /
     `rom_image`
     に
     `selected_path`
     状態を作り、
     `InfoNES_Menu()`
     →
     `InfoNES_Load(selected_path)`
     契約を作る。
  6. `platform/infones_session.cpp`
     に
     `run_infones_session`
     `InfoNES_Menu`
     `InfoNES_DebugPrint`
     `InfoNES_MessageBox`
     `InfoNES_Error`
     を置く。
  7. `input.cpp`
     を
     3 引数
     `InfoNES_PadState`
     へ変える。
  8. `audio.cpp`
     に不足 hook を足す。
  9. `display.cpp`
     を
     `infones`
     名称系へ合わせて接続する。
  10. 周辺 hook の画面表示連携を埋める。
