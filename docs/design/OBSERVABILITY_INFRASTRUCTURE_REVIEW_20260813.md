# 観測基盤の共通化 改造依頼 レビュー記録

作成日: 2026-08-13
対象: `Picocalc_NESco`
関連依頼: `docs/design/OBSERVABILITY_INFRASTRUCTURE_PLAN_20260813.md`
状態: **レビュー記録。実装・ビルド・コミットは未実施。**

## 1. 結論

今回の依頼は、Mapper 19 / Mapper 30 の調査ごとに専用診断を作る構造を改め、mapperに依存しない観測基盤を本体へ追加する要求である。

要求の方向性は妥当である。特に次の問題を共通の仕組みで解消できる。

- mapper内部状態を外部から確認できない
- ROM取り違えを実行後まで検出できない
- 通常ビルドでは致命的エラーが無言になる
- 計測中のUART出力と診断用printfが衝突する
- Mapper 19の調査で作ったframe計測をMapper 30でも再利用できない

ただし、依頼書は要求レベルの文書であり、実装前にAPIと出力契約を追加で固定する必要がある。

## 2. 依頼内容の整理

依頼書が求めている構造は次の4つである。

### 2.1 mapper状態ダンプ

mapperの関数群に状態ダンプ用のcallbackを追加する。

```cpp
void (*MapperDump)(void);
```

出力は次のような `[TAG] k=v` 形式とし、Mapper 19 / 30 のバンク、ラッチ、IRQ、flash状態などを1行で確認できるようにする。

```text
[MAP] mapper=19 ...
[MAP] mapper=30 ...
```

未実装mapperはno-opまたは`nullptr`でよいとされている。

### 2.2 ROM起動バナー

ROM起動時に、build識別、ROM名、mapper、submapper、PRG/CHRサイズ、CHR-RAMサイズ、確保成否を常時1行出力する。

```text
[BOOT] build=<commit>[-dirty] flags=<...> rom=<name> mapper=<N> sub=<S> prg=<KiB> chr=<KiB> chrram=<KiB> alloc=<ok|fail>
```

### 2.3 fatal errorレジストリ

`InfoNES_Error()`やmapperの致命的エラーを発生地点では出力せず、固定長のレジストリへコード・回数・最初のframeを登録する。チェックポイントまたはscenario終了時にまとめて出力する。

### 2.4 frame同期checkpoint

```cpp
void nesco_checkpoint(uint32_t frame);
```

emulated frameに同期して、共通カウンタ、mapper状態、エラーレジストリを定点出力する。Mapper 19の固定frame計測から汎用化することが想定されている。

## 3. 現行コードとの照合結果

### 3.1 mapper callbackの構造

現行の [`MapperTable_tag`](../../infones/InfoNES_Mapper.h) は、実際には`nMapperNo`と`pMapperInit`だけを持つ。`MapperWrite`、`MapperApu`、`MapperHSync`などは、各mapperの`Init()`が設定するグローバルcallbackである。

したがって、「mapper関数テーブルへ追加する」という依頼文の表現は、現行実装とは少し一致しない。

実装案としては、`MapperTable_tag`へ任意の第3フィールドとして`pMapperDump`を追加し、既存entryの省略初期値を`nullptr`にする方法が最も変更範囲が小さい。`InfoNES_Reset()`で現在のmapper用callbackを選択すれば、未実装mapperの初期化関数を大量に変更せずに済む。

### 3.2 既存の起動出力

`platform/main.c`はすでにversionと`__DATE__ / __TIME__`によるbuild日時を無条件出力している。しかし、次の情報は出していない。

- git commit hash
- dirty状態
- ROM名、mapper、submapper
- ROMサイズ
- mapperの確保成否

ROM情報は`InfoNES_Load()`後に初めて確定するため、ファームウェア起動時のバナーとROM起動時のバナーは分ける必要がある。依頼書のROM情報付き`[BOOT]`は、`InfoNES_Menu()`でload/reset完了後に出すのが適切である。

### 3.3 エラー出力と確保失敗

[`platform/infones_session.cpp`](../../platform/infones_session.cpp) の`InfoNES_Error()`は、現在`NESCO_RUNTIME_LOGS`が有効な場合だけUART出力する。通常ビルドでは、Mapper 19 / 30の確保失敗やMapper 30のoverlay上限超過が無言になる。

Mapper 19 / 30の初期化関数は確保失敗時に`InfoNES_Error()`を呼び、mapperの`Init()`から戻るが、現行の`InfoNES_Reset()`はその失敗を共通の戻り値として判定していない。したがって、`alloc=fail`を正しく出すには、エラーレジストリだけでなく初期化失敗状態の伝達も必要である。

### 3.4 Mapper 30の既存診断

`infones/mapper/InfoNES_Mapper_030.cpp`には、`Map30_DebugGetCount()`、`Map30_DebugGetAddr()`、`Map30_DebugGetData()`と16件のdebug記録領域が存在する。ただし、`INFONES_MAPPER30_ENABLE_DIAGNOSTICS`を有効にするCMake optionは現行の`CMakeLists.txt`に存在せず、getterの呼び出し元もない。

この既存リングバッファを完成させるより、共通`MapperDump`へ移行する方が依頼の目的に合う。ただし、既存scenarioがこの診断を直接要求していないかを確認し、移行完了までは不用意に削除しない。

### 3.5 frameカウンタ

現行の`g_perf_frames`は性能ログ用のカウンタであり、`FrameCnt`によるframe skipの影響を受ける。すべてのemulated frameを数える依頼のcheckpoint用カウンタとしては不適切である。

Mapper 19検証資料が要求している`metric_emu_frame`を独立カウンタとして実装し、ROM load/resetで0に戻し、`InfoNES_HSync()`のframe境界で`FrameCnt`に関係なく進める必要がある。

## 4. 実装前に決めるべき事項

### 4.1 `MapperDump`の出力単位

依頼書は`MapperDump(void)`が1行を出すことと、checkpointがmapper状態を含む1行を出すことを同時に要求している。この2つは、そのままでは両立しない。

次のどちらかを選ぶ必要がある。

1. `[CHECKPOINT]`と`[MAP]`を別行で出す
2. mapperが文字列バッファまたはkey/value writerへ状態を書き込み、checkpointが1行に統合する

実装の単純さを優先するなら1、ログを機械処理する契約を優先するなら2が適切である。

### 4.2 fatal errorのコード体系

現行`InfoNES_Error()`はformat文字列を受けるだけで、数値コードを持たない。`NescoErrorSlot`へ記録するには、次のいずれかが必要である。

- `InfoNES_ErrorCode(code, format, ...)`を追加する
- 既存のerror call siteへ固定IDを割り当てる
- format文字列からhashを作る

hashはbuild間で意味が変わる可能性があり、scenarioの判定には向かない。固定enumを定義する案を推奨する。さらに、slot数、未知コードの扱い、startup allocation errorとruntime overlay errorの区別を決める必要がある。

### 4.3 `alloc`の意味

`alloc=ok|fail`が次のどちらを意味するかを固定する必要がある。

- mapper初期化時の確保だけ
- ROM loader、mapper、audio、displayを含むROM起動全体

依頼内容からは、mapperのstartup allocationを含むROM実行準備の成否と解釈するのが自然である。その場合、`InfoNES_Reset()`の戻り値へ反映する必要がある。

### 4.4 build識別とflags

現行build識別は日時のみで、CMakeにgit hash注入処理はない。`git rev-parse --short HEAD`、dirty判定、git unavailable時のfallback、CMake cache更新時の再取得方法を決める必要がある。

`flags=`についても、どの`NESCO_*`を列挙するか、`OFF`を列挙するか、文字列の区切り文字を決める必要がある。scenarioの`uart_contains`で判定するため、空白や順序を安定させるべきである。

### 4.5 checkpointの出力周期

依頼書には、どのframeでcheckpointを出すかが定義されていない。測定中のUART出力を避けるため、次のいずれかを明示する必要がある。

- CMakeでcheckpoint frameを指定する
- 起動時の固定frameだけ出す
- scenario終了時だけ出す
- checkpoint APIは提供するが、呼び出し位置は各検証計画で指定する

既存のMapper 19計測契約ではframe 600--1199の間にUARTを出さないため、checkpointを有効にしたbuildを正式性能測定へ使用しないことも明記する必要がある。

## 5. 現在の作業ツリーとの関係

レビュー時点のgit状態は次のとおりである。

- branch: `main`
- `HEAD`: `aa72e7f` (`Request shared observability structures for mapper validation`)
- `origin/main`より7 commit先行
- 未コミット変更: 6ファイル
  - `infones/InfoNES.cpp`
  - `infones/InfoNES_Mapper.h`
  - `infones/K6502.cpp`
  - `infones/K6502.h`
  - `infones/K6502_rw.h`
  - `infones/mapper/InfoNES_Mapper_019.cpp`
- 未コミット差分はMapper 19のCPU-cycle IRQ hookとfixture診断に関する変更
- `git diff --check`は通過

この差分は共通観測基盤とは別の機能・検証変更であるため、戻したり混ぜたりせず、観測基盤の実装はcleanなbranchまたはworktreeで進めるべきである。

なお、Mapper 19外部検証資料ではR0の固定frame計測を完了扱いとしている一方、現在の作業ツリーにはCPU hook候補の未コミット差分が残っている。R0/R1のどの成果を共通checkpointへ引き上げるかは、実装開始前に資料とsourceの対応を整理する必要がある。

## 6. 推奨する実装順序

1. 現在の6ファイルの差分を保存・隔離し、観測基盤用のclean branch/worktreeを用意する。
2. `metric_emu_frame`、error code、checkpointの出力形式を先に仕様化する。
3. 常時有効のerror registryとROM起動バナーを実装する。
4. `MapperTable`へoptionalな`pMapperDump`を追加し、まずMapper 19 / 30だけ実装する。
5. 固定frame checkpointを追加し、Mapper 19の既存`M19_WORK`計測と重複しないよう整理する。
6. Mapper 19 / 30の既存専用診断をscenarioで置き換えられることを確認してから、不要な専用経路を整理する。
7. 通常build、観測build、性能測定buildを分離してbuild確認する。

機能変更と観測基盤変更は別commitにする。特にMapper 19のIRQ動作変更を観測基盤のcommitへ含めない。

## 7. レビュー時点の判定

依頼は採用候補として妥当だが、現時点では実装開始条件を満たしていない。

先に固定すべき最小仕様は次の4点である。

1. `MapperDump`を別行にするかcheckpointへ統合するか
2. fatal errorの固定ID一覧
3. `alloc`の判定範囲
4. checkpointのframe周期と性能測定時の無出力区間

これらを決めれば、依頼書の4構造は現行architectureへ無理なく追加できる見込みである。
