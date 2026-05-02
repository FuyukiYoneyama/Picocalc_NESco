# Compile-time logging cleanup plan (2026-05-02)

## 目的

現在の Picocalc_NESco では、計測や不具合調査のために
`#if` / `#ifdef` と `printf()` が複数箇所に散らばっている。

ゲーム実行速度を最優先にしつつ、ソースを読みやすくするため、
ログ出力と計測用 counter 更新を compile-time macro に集約する。

## 最優先方針

- release build ではログ処理、計測処理、format 文字列、counter 更新を可能な限りコンパイル対象から消す。
- hot path に runtime log level 判定を入れない。
- hot path に logger 関数呼び出し、function pointer、callback を残さない。
- source 本文に `#if NESCO_*LOG*` を増やさず、ログ用 header に閉じ込める。
- 既存挙動を変えず、ログ整理だけを小さい単位で進める。

## 現状確認

確認済みのログ系要素:

- `platform/runtime_log.h`
  - 既存の `NESCO_LOGF()` / `NESCO_LOG_PUTS()`
  - `NESCO_RUNTIME_LOGS` 無効時は空 macro
- `CMakeLists.txt`
  - `NESCO_RUNTIME_LOGS`
  - `NESCO_INPUT_IO_LOGS`
  - `NESCO_BOKOSUKA_STATE_LOGS`
  - `NESCO_CORE1_BASELINE_LOG`
- `infones/InfoNES_StructuredLog.h`
  - `INFONES_ENABLE_PPU2006_EVT_LOG`
  - `INFONES_ENABLE_INITIAL_SEQUENCE_LOG`
  - `INFONES_ENABLE_INPUT_IO_LOG`
  - `INFONES_ENABLE_BOKOSUKA_STATE_LOG`
- `platform/rom_menu.c` / `platform/rom_image.c`
  - `NESCO_RUNTIME_LOGS` 無効時に局所的に `printf(...)` を潰している
- `infones/InfoNES.cpp`
  - `kPerfLogToSerial`
  - `NESCO_CORE1_BASELINE_LOG` による FPS / PPU 計測 log

## 採用する設計

既存の `platform/runtime_log.h` を拡張し、
新規に大きな logging subsystem は作らない。

初期版で追加する macro:

```c
#if defined(NESCO_RUNTIME_LOGS)
#define NESCO_LOG_RUNTIME(...) NESCO_LOGF(__VA_ARGS__)
#define NESCO_RUNTIME_ONLY(stmt) do { stmt; } while (0)
#else
#define NESCO_LOG_RUNTIME(...) do { } while (0)
#define NESCO_RUNTIME_ONLY(stmt) do { } while (0)
#endif

#if defined(NESCO_CORE1_BASELINE_LOG)
#define NESCO_LOG_PERF(...) printf(__VA_ARGS__)
#define NESCO_PERF_ONLY(stmt) do { stmt; } while (0)
#else
#define NESCO_LOG_PERF(...) do { } while (0)
#define NESCO_PERF_ONLY(stmt) do { } while (0)
#endif
```

`NESCO_LOG_PERF(...)` は速度優先の計測用 macro として、
明示的に `fflush(stdout)` しない。
既存の perf log と同じく、必要な改行を呼び出し側が含める。

必要に応じて後段で追加する macro:

```c
#if defined(NESCO_INPUT_IO_LOGS)
#define NESCO_INPUT_IO_ONLY(stmt) do { stmt; } while (0)
#else
#define NESCO_INPUT_IO_ONLY(stmt) do { } while (0)
#endif

#if defined(NESCO_BOKOSUKA_STATE_LOGS)
#define NESCO_BOKO_ONLY(stmt) do { stmt; } while (0)
#else
#define NESCO_BOKO_ONLY(stmt) do { } while (0)
#endif
```

ただし、`INFONES_ENABLE_*` 系は既存の
`InfoNES_StructuredLog.h`
の構造と密接なので、初回では無理に統合しない。

## 実装範囲

### Phase 1: logging macro 基盤の追加

対象:

- `platform/runtime_log.h`

作業:

- `NESCO_LOG_RUNTIME(...)`
- `NESCO_RUNTIME_ONLY(stmt)`
- `NESCO_LOG_PERF(...)`
- `NESCO_PERF_ONLY(stmt)`

を追加する。

この段階では呼び出し側の大量置換はしない。

合格条件:

- release build が通る
- `NESCO_RUNTIME_LOGS=OFF`
  で既存 banner 以外の追加 log が増えない
- `NESCO_CORE1_BASELINE_LOG=OFF`
  で perf log / perf counter 更新が残らない方針が明確になる

build:

- 1 回

commit:

- Phase 1 単独 commit

### Phase 2: platform runtime log の置換

初回対象:

- `platform/sram_store.cpp`

初回対象外:

- `platform/main.c`
- `platform/core1_worker.c`
- `platform/audio.c`
- `platform/screenshot_storage.c`

作業:

- `platform/sram_store.cpp` の局所 `NESCO_CPP_LOGF` を廃止し、
  `runtime_log.h` の共通 macro へ寄せる。
- 既存 `NESCO_LOGF()` / `NESCO_LOG_PUTS()` を使っている箇所は、
  初回では原則維持する。
- `platform/main.c` / `platform/core1_worker.c` / `platform/audio.c` /
  `platform/screenshot_storage.c`
  は、Phase 2 初回では触らない。
  追加整理が必要なら別 Phase または別計画に分ける。

対象外:

- `platform/rom_menu.c` / `platform/rom_image.c`
  の `#define printf(...) ((void)0)` は、この Phase では触らない。
  影響範囲が広いため Phase 3 で扱う。

合格条件:

- release build が通る
- `git diff` 上で log 条件が source 本文から減っている
- release build で文字列や処理が増えない

build:

- 1 回

commit:

- Phase 2 単独 commit

### Phase 3: `rom_menu.c` / `rom_image.c` の局所 printf 抑制を置換

対象:

- `platform/rom_menu.c`
- `platform/rom_image.c`

現状:

```c
#if !defined(NESCO_RUNTIME_LOGS)
#define printf(...) ((void)0)
#endif
```

方針:

- ファイル単位で `printf` を潰す方式をやめる。
- runtime log は `NESCO_LOG_RUNTIME(...)` へ置換する。
- user-visible text 用の `snprintf()` は触らない。

注意:

- `printf` macro の削除は影響範囲が広いため、
  `rg -n '(^|[^A-Za-z0-9_])printf[[:space:]]*\\(' platform/rom_menu.c platform/rom_image.c`
  で裸の `printf()` だけを確認してから進める。
- 置換対象は debug / runtime log のみ。
- ROM menu 表示、status text、Loading 表示、screenshot viewer 表示には手を入れない。

合格条件:

- release build が通る
- 置換後に
  `rg -n '(^|[^A-Za-z0-9_])printf[[:space:]]*\\(' platform/rom_menu.c platform/rom_image.c`
  を実行し、debug / runtime log 用の `printf()` が残っていないことを確認する
  - 残ってよいのは、必要性を個別に説明できるものだけ
  - 原則として `rom_menu.c` / `rom_image.c` の debug log は
    `NESCO_LOG_RUNTIME(...)` に置換済みであること
- ROM menu が起動する
- SD ROM 一覧が出る
- file load / flash load の表示が壊れない

build:

- 1 回

実機確認:

- 1 回
  - ROM menu 起動
  - SD ROM 一覧
  - `SYSTEM FLASH`
  - ROM 起動
  - ESC menu return

commit:

- Phase 3 単独 commit

### Phase 4: perf / measurement counter の compile-time 消去を明確化

対象候補:

- `infones/InfoNES.cpp`
- `infones/K6502_rw.h`
- `infones/K6502.cpp`

初回の実装対象:

- `infones/InfoNES.cpp` を第一対象にする。
- `K6502_rw.h` / `K6502.cpp` は、既存の
  `INFONES_ENABLE_*` / `if constexpr (kPerfLogToSerial)`
  で release build から消えている箇所は無理に置換しない。
- 初回では `K6502_rw.h` に `platform/runtime_log.h`
  などの platform 依存 include を追加しない。
  header への platform 依存追加は影響範囲が広いため、別判断にする。

方針:

- hot path の計測 counter 更新は
  `NESCO_PERF_ONLY(...)`
  または既存 `if constexpr (kPerfLogToSerial)` に統一する。
- `NESCO_PERF_ONLY(...)` を使う場合は、対象 `.cpp` / `.c` が
  すでに platform include path を持つことを確認した上で
  `platform/runtime_log.h` を include する。
- header file では、初回は既存の compile-time 条件を優先し、
  新しい platform logging header への依存を増やさない。
- release build で counter 更新が残らないことを最優先にする。
- `INFONES_ENABLE_*` 系の structured log は、
  既存の `InfoNES_StructuredLog.h`
  の static inline no-op を活かし、無理に書き換えない。

合格条件:

- release build が通る
- `NESCO_CORE1_BASELINE_LOG=OFF`
  で perf counter 更新が消える
- `NESCO_CORE1_BASELINE_LOG=ON`
  で従来の計測 log が出せる

build:

- release build 1 回
- perf build 1 回

実機確認:

- 通常は不要
- perf log の出力形式を変えた場合のみ 1 回

commit:

- Phase 4 単独 commit

## 実機確認回数

最低 1 回。

- Phase 3 後に ROM menu / ROM 起動 / ESC 復帰だけ確認する。

Phase 4 で perf log 出力形式を変更した場合は追加 1 回。

合計:

- 最低 1 回
- 最大 2 回

## build 回数

最低:

1. Phase 1 build
2. Phase 2 build
3. Phase 3 build
4. Phase 4 release build

必要時:

5. Phase 4 perf build

合計:

- 最低 4 回
- 最大 5 回

## 手戻り方針

- Phase ごとに commit を分ける。
- どの Phase でも速度低下、ROM menu 破損、log 消し忘れが出たら、その Phase の commit だけ revert する。
- Phase 3 は `printf` 抑制方式を変えるため、もっとも注意して実施する。

## 採用しない方法

- runtime log level
- global logger function
- function pointer / callback logger
- hot path に残る `if (log_enabled)`
- release build でも counter を常時更新し、最後だけ出力する方式

理由:

ゲーム実行速度を最優先するため。

## 期待される効果

- release build の実行速度を落とさずに source の `#if` を減らせる。
- log category の入口が `runtime_log.h` に集まり、今後の計測版を作りやすくなる。
- `printf` を file-local macro で潰す方式を減らし、意図しない副作用を避けやすくなる。

## リスク

- Phase 3 で debug log 以外の `printf` を誤って置換すると表示や挙動が壊れる。
- Phase 4 で perf counter の有効条件を間違えると、計測結果の比較が壊れる。
- macro 名を増やしすぎると、逆に読みにくくなる。

対策:

- Phase 3 は `rom_menu.c` / `rom_image.c` の `printf` を全件確認してから進める。
- Phase 4 は `NESCO_CORE1_BASELINE_LOG=ON/OFF`
  の両方で build する。
- 初回は macro 数を最小にする。
