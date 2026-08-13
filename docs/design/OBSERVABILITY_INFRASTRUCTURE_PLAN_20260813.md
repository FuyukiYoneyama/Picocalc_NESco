# 観測基盤の共通化 改造依頼

作成日: 2026-08-13
起票: Codex
対象: `Picocalc_NESco` 本体
状態: **依頼のみ。** 実装・ビルド・コミットは未実施。

## 1. 目的

`Picocalc_emu` を通して `Picocalc_NESco` の内部状態を観測するための基盤を、**特定 mapper の調査ごとに作り直さない形**へ変える。

本書は特定 mapper の対応ではない。Mapper 19 と Mapper 30 の検証で同じ不足が繰り返し出たため、共通構造として起票する。

## 2. 現状の問題

### 2.1 mapper に状態を報告する口がない

mapper インターフェース（`infones/InfoNES_Mapper.h`）は `pMapperInit` と、Init 内で代入される `MapperWrite` / `MapperSram` / `MapperApu` / `MapperReadApu` / `MapperVSync` / `MapperHSync` / `MapperPPU` / `MapperRenderScreen` からなる。**すべて振る舞いであり、状態を外へ出す口が 1 つもない。**

各 mapper の内部状態（バンク、ラッチ、IRQ カウンタ、flash 状態）は file-static または無名 namespace にあり、外部から観測できない。

その結果、mapper ごとに専用の診断を作っている。

| mapper | 診断 | 状態 |
|---|---|---|
| 19 | `NESCO_MAPPER19_IRQ_DIAGNOSTICS`、`InfoNES.cpp` の `[M19_IRQ_DIAG]` printf、`RAM[$F9]` 読み出し経路 | 完成（commit `ce67aa7`） |
| 30 | `INFONES_MAPPER30_ENABLE_DIAGNOSTICS`、リングバッファ 16 件、`Map30_DebugGetCount/GetAddr/GetData` | **未完成。CMake option が無く、getter の呼び出し元も無い** |

**同じものを mapper ごとに作り直しており、片方は完成していない。** Mapper 30 の `Map30_FlashState` / `Map30_FlashIdMode` / `Map30_FlashTargetBank` は無名 namespace 内でアクセサもなく、現状では外から一切観測できない。

### 2.2 起動時に「何が動いているか」を出していない

起動時のビルド識別・ROM 識別を無条件に出す経路がない。`platform/rom_image.c` の `[ROM]` 系ログはすべて `NESCO_LOG_RUNTIME` 依存で、通常ビルドでは無言である。

**実害が出ている。** Mapper 19 の Phase 0B-P で、Project_DART を測るつもりで直前に上書きされた Xevious の embedded image を走らせ、1 invocation を無効として捨てた。再発防止として二段階 manifest と起動前 hash 照合を導入したが、**起動バナー 1 行があれば scenario の `uart_contains` がその場で弾ける**性質の事故だった。

### 2.3 致命的エラーが通常ビルドで無言

```c
void InfoNES_Error(const char *pszMsg, ...) {
#if defined(NESCO_RUNTIME_LOGS)
    ...
#endif
}
```

`platform/infones_session.cpp` の実装は `NESCO_RUNTIME_LOGS` に依存する。したがって通常ビルドでは、

- Mapper 30 の `"Mapper 30 PRG-RAM > 32KB"`（flash overlay 溢れ）
- Mapper 30 の `"Mapper 30 startup alloc failed"`
- Mapper 19 の startup alloc 失敗

がいずれも**何も出さずに失敗する**。「動かない」の原因が利用者にも開発者にも分からない直接の理由になっている。

### 2.4 計測と診断が原理的に衝突している

現在の診断は「イベント発生地点で printf」方式である。一方、性能計測は UART 出力の混入を許さない。Mapper 19 の再計画は測定区間 frame 600-1199 で UART を一切出さないことを要求し、commit `73bb233`「Make Mapper 19 work scenarios runtime-log independent」もこの衝突への対処だった。

**診断を足すほど計測が難しくなる構造**になっている。

### 2.5 汎用インフラが個別調査の中で作られている

Mapper 19 の R0 では `metric_emu_frame`、固定 frame 区間の窓選択、`[M19_WORK] start= end= work_frames= core0_total_us= …` を、1 つの mapper の調査の中で構築している。これは mapper に依存しない汎用機構であり、Mapper 30 でも同じものが必要になる。

## 3. 依頼する 4 つの構造

### 3.1 mapper 状態ダンプの共通フック（最優先）

mapper 関数テーブルへ 1 本追加する。

```c
void (*MapperDump)(void);
```

各 mapper は自分の状態を 1 行で出す。形式は Mapper 19 が確立した `[TAG] k=v` に揃える。

```
[MAP] mapper=19 latch=.. prg0=.. prg1=.. chr=.. irq_cnt=.. irq_en=.. pending=..
[MAP] mapper=30 latch=.. prg=.. chr=.. nt=.. flash_state=.. flash_bank=.. id_mode=.. overlay=..
```

未実装の mapper は `nullptr` のままでよい（`Map0_*` と同様に no-op 既定を置く）。

**これが入れば、CMake option 1 つで全 mapper が観測可能になる。** mapper 側の追加実装は 1 関数のみで、専用の option もリングバッファも printf 散在も不要になる。

### 3.2 起動識別バナー（常時出力）

起動時に**無条件で** 1 行出す。

```
[BOOT] build=<commit>[-dirty] flags=<有効なNESCO_*> rom=<name> mapper=<N> sub=<S> prg=<KiB> chr=<KiB> chrram=<KiB> alloc=<ok|fail>
```

- ROM 取り違えを scenario 側で検出できる
- mapper の起動時確保の成否が、追加コードなしで判る
- どのビルドの結果かが evidence に自動で残る

出力は 1 行・起動時のみなので、計測区間へ影響しない。

### 3.3 致命エラーの常時レジストリ

常時コンパイルされる小さな構造を置き、`InfoNES_Error` と mapper のエラーを登録する。

```c
struct NescoErrorSlot { uint16_t code; uint32_t count; uint32_t first_frame; };
```

- 発生地点では**出力せず**カウントのみ（計測区間を汚さない）
- チェックポイントまたは scenario 終了時に一括で吐く
- `NESCO_RUNTIME_LOGS` に依存させない

**無言の失敗を、診断可能な失敗にする。**

### 3.4 フレーム同期チェックポイント

```c
void nesco_checkpoint(uint32_t frame);
```

指定 emulated frame で、カウンタ・`MapperDump`・エラーレジストリを 1 行にまとめて出す。

Mapper 19 の R0 が構築中の固定 frame 区間の仕組みと重複するため、**R0 の成果をここへ引き上げる形が望ましい**（第 5 節）。

## 4. 設計上の制約

- **既定は OFF。** 通常ビルドの挙動と性能を変えない。ただし 3.2 の起動バナーと 3.3 のエラーレジストリは常時有効とする（1 行・低頻度のため）
- **「黙って溜めて定点で吐く」方式にする。** イベント地点での printf を増やさない。これが第 2.4 節の衝突への対処である
- 出力形式は `[TAG] k=v` に統一する。scenario の `uart_contains` が assert できること
- 機能変更と診断基盤は別 commit にする

## 5. 既存作業との関係

**Mapper 19 の R0 が現在進行中で、3.4 と範囲が重なる。** working tree に `infones/InfoNES.cpp`、`InfoNES_Mapper.h`、`K6502*.{cpp,h}`、`mapper/InfoNES_Mapper_019.cpp` の変更がある。

順序として次を推奨する。

1. R0 の計測機構が固まるのを待つ
2. その成果を 3.4 として汎用化する
3. 3.1〜3.3 を追加する

**3.1 と 3.3 が先に入れば、Mapper 30 検証計画の P0a（診断基盤構築）はほぼ不要になる。** mapper 30 側は `MapperDump` を 1 つ実装するだけで済む。逆に Mapper 30 を先に進めると、また mapper 専用の診断を作ることになる。

## 6. 範囲外

- 特定 mapper の状態定義（各 mapper の `MapperDump` 実装は各 mapper の作業）
- `Picocalc_emu` 側の機能追加（SD イメージの load/save、`flash_range_*` の emulation）。これらは別途 `Picocalc_emu` へ起票が必要
- LCD スナップショット、audio 出力の観測

## 7. 参考

- Mapper 19 検証: `nes2/mapper_check/mapper19_validation/`
- Mapper 30 検証: `nes2/mapper_check/mapper30_validation/`
- mapper 診断の要求記述: `nes2/InfoNES_devtxt/nesco_mapper_cleanroom_spec.md` 第 8 節。同書は `NESCO_MAPPER30_TRACE` / `NESCO_IRQ_TRACE` / `NESCO_BANK_TRACE` を挙げるが、いずれも未実装である
