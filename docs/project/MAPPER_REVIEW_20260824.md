# Mapper implementation review — 2026-08-24

この文書は、`Picocalc_NESco` の Mapper 関連更新に対するコードレビュー記録です。
レビュー対象は `main` の `57b38a6` (`Clarify project license classification`) 時点で、特に直近で追加・更新された Mapper 1 / 2 / 3 / 5 と、それらのために変更された CPU / PPU / save path を確認しています。

この文書はレビュー所見の記録であり、現在タスクの正本ではありません。採用する修正項目は必要に応じて `docs/project/TASKS.md` へ移してください。

## 総合評価

**A- / 8.8 相当。今回の Mapper 更新は採用してよい品質です。**

今回の変更は単なる「対応 Mapper 数の追加」ではありません。Mapper 固有の bank switch だけで処理せず、6502 の RMW bus write、NES 2.0 submapper、bus conflict、banked PRG-RAM、battery persistence、MMC5 の ExRAM / nametable / IRQ / PCM / OAM DMA まで、カートリッジから実際に観測される CPU / PPU 挙動へ踏み込んでいます。

特に Mapper 1 のために CPU core 側の RMW write を修正した点は重要です。従来の「変更後の値を1回だけ write」では MMC1 の consecutive-cycle write suppression を正しく再現できません。Mapper 側だけで帳尻を合わせず、CPU bus-visible behavior を修正した方針を高く評価します。

一方、Mapper 1 の一部 SxROM variant と Mapper 5 の nametable / IRQ 境界には、互換性上の修正候補が残っています。

## Mapper 別評価

| Mapper | 評価 | 所見 |
|---|---:|---|
| `1` / MMC1 | A- | RMW、連続 write、PRG-RAM、SUROM、NES 2.0 submapper 5 まで広く対応。SxROM variant に小さな穴あり |
| `2` / UxROM | A | NES 2.0 submapper による bus conflict 分離が簡潔で互換性方針も妥当 |
| `3` / CNROM | A | Mapper 2 と同様。CHR-ROM 前提の防御も入り、旧実装より安全 |
| `5` / MMC5 | B+〜A- | 実装範囲は非常に広く実用水準。nametable write と IRQ 境界に修正候補あり |

## 良かった点

### 1. Mapper 1 のために 6502 RMW bus behavior を直した

`infones/K6502.cpp` に `RMW_WRITE()` を導入し、`DEC` / `INC` / `ASL` / `LSR` / `ROL` / `ROR` と、対応する stable unofficial opcode で、未変更値と変更後値の2回 write を発生させています。

これは MMC1 の「直前の mapper write の次の CPU cycle に行われた write を無視する」挙動に必要です。Mapper 1 側も `getCurrentClocks32()` を使って consecutive write を判定し、D7 reset write は suppression の対象外にしています。

この組み合わせは、Mapper だけを局所修正するより設計として正しいです。

関連箇所:

- `infones/K6502.cpp`
- `infones/mapper/InfoNES_Mapper_001.cpp`
- commit `cc5506f` (`Implement MMC1 Mapper 1 core and PRG RAM`)

### 2. Mapper 2 / 3 の bus conflict を NES 2.0 submapper で限定した

Mapper 2 / 3 ともに、NES 2.0 submapper 2 の場合だけ CPU data と現在 PRG-ROM が駆動している byte を AND し、legacy iNES は従来の no-conflict path に残しています。

古い iNES image には board variant を識別する情報がないため、すべての legacy ROM に conflict を強制しない判断は compatibility-first として妥当です。

関連箇所:

- `infones/mapper/InfoNES_Mapper_002.cpp`
- `infones/mapper/InfoNES_Mapper_003.cpp`
- commit `d302551` (`Implement UxROM Mapper 2 bus conflict submapper`)
- commit `829f6fd` (`Implement CNROM Mapper 3 bus conflict submapper`)

### 3. Mapper 5 を bank switch だけで終わらせていない

Mapper 5 は以下まで統合されています。

- PRG mode / CHR mode
- banked WRAM
- CHR-ROM / CHR-RAM
- ExRAM
- nametable select / fill mode
- extended attribute
- vertical split
- scanline IRQ
- multiplication registers
- pulse expansion audio
- PCM DAC / PCM read mode / PCM IRQ latch
- `$6000` bank mapping を考慮した OAM DMA
- NMI vector fetch による scanline detector reset
- battery WRAM / ExRAM persistence (`*.m5s`)

また、RP2040 の SRAM 制約を前提に WRAM / CHR-RAM を dynamic allocation とし、PRG-RAM resident size を 64 KiB に制限して failure path も用意しています。PicoCalc 専用 firmware というプロジェクト条件に対して、ハードウェア制約を隠さず設計へ反映できています。

関連箇所:

- `infones/mapper/InfoNES_Mapper_005.cpp`
- `infones/K6502_rw.h`
- `infones/InfoNES.cpp`
- `platform/sram_store.cpp`
- commit `6556d7e` (`Implement Mapper 5 core and battery paths`)
- merge commit `0f0b827` (`Merge Mapper 5 MMC5 implementation`)

### 4. Mapper 5 の OAM DMA / PCM / IRQ を CPU read/write path へ接続した

MMC5 の `$5113` は `$6000-$7FFF` の WRAM page を変えるため、generic SRAM memcpy では OAM DMA source が誤ります。現実装では Mapper 5 の場合だけ `Map5_ReadSram()` 経由で DMA source を読みます。

同様に PCM read mode と NMI vector `$FFFA/$FFFB` を `K6502_Read()` 側で Mapper 5 に通知しています。

「Mapper ファイルの register state だけを正しくする」のではなく、実際に mapper が観測する bus access point を core に作っているのは良い設計です。

### 5. 公開文書の互換性表現が実装の成熟度に合っている

README / HISTORY は Mapper 5 を「完全互換」としておらず、「多分動く」とし、64 KiB PRG-RAM resident cap、scanline 境界ベースの IRQ、PCM IRQ 比較の制約を明記しています。

実装範囲は広いですが、未検証部分まで保証しない書き方になっており、公開プロジェクトの記録として適切です。

## 修正候補

以下はレビュー時点で見つけた修正候補です。優先順に並べています。

### P1: Mapper 5 — fill / empty nametable への `$2007` write を遮断する

**重要度: 高**

`Map5_Sync_Nametable()` は `$5105` に応じて `PPUBANK[8..15]` に以下を割り当てています。

- CIRAM page 0 / 1
- `Map5_Ex_Ram`
- mode 2 / 3 用の `Map5_Empty_Nam`
- fill mode 用の `Map5_Ex_Nam`

一方、common `$2007` write path (`infones/K6502_rw.h`) は nametable address に対して、Mapper 5 でも最終的に

```cpp
PPUBANK[addr >> 10][addr & 0x3ff] = vramData;
PPUBANK[(addr ^ 0x1000) >> 10][addr & 0x3ff] = vramData;
```

を実行します。

そのため、`Map5_Empty_Nam` や fill-mode backing buffer が CPU `$2007` write で書き換えられます。

NESdev の MMC5 documentation では、`$5105 = 2` かつ ExRAM mode 2 / 3 の nametable は all-zero read とされ、`$5105 = 3` は `$5106/$5107` で生成される fill-mode data です。common PPU write によってその backing state が変わらないようにする必要があります。

参考:

- <https://www.nesdev.org/wiki/MMC5#Nametable_mapping_($5105)>

#### 修正案

`Map5_NotePpuNametableWrite()` を dirty notification だけに使うのではなく、例えば次のような mapper-owned write path に昇格させる方法がきれいです。

```cpp
bool Map5_WritePpuNametable(WORD addr, BYTE data);
```

Mapper 5 が write を処理・遮断した場合は common write を行わず、CIRAM / writable ExRAM の場合だけ実際の backing store を更新します。

最低限の修正なら、common path 側で `PPUBANK` が `Map5_Empty_Nam` / `Map5_Ex_Nam` を指している場合の write を無視しても構いません。

### P2: Mapper 1 — SZROM の PRG-RAM bank select bit を判別する

**重要度: 中**

現在の `Map1_Current_Prg_Ram_Bank()` は、16 KiB PRG-RAM の場合に CHR0 register の bit 3 を bank select として使います。

これは SOROM には合いますが、SZROM は 16 KiB PRG-RAM を **CHR bank bit 4** で切り替えます。

NESdev では SZROM を、8 KiB PRG-RAM + 8 KiB PRG-NVRAM と 16 KiB 以上の CHR を持つ構成として識別できるとしています。

参考:

- <https://www.nesdev.org/wiki/INES_Mapper_001#SZROM>

#### 修正案

16 KiB RAM だから一律 bit 3 とせず、CHR size / NES 2.0 RAM topology から SOROM と SZROM を分けます。

概念上は次の区別です。

- SOROM: CHR bit 3
- SZROM: CHR bit 4
- SXROM 32 KiB: CHR bits 3:2

既知 SZROM title は少ないため P1 より優先度は下げてよいですが、Mapper 1 variant coverage を明確にするなら修正対象です。

### P3: Mapper 5 — visible frame 終了時の pending scanline IRQ clear を再確認する

**重要度: 中**

現在の `Map5_ScanlineStart()` は rendering inactive の場合、in-frame bit (`0x40`) と scanline counter を clear しますが、pending IRQ bit (`0x80`) はその場では clear しません。

一方、NMI vector `$FFFA/$FFFB` read では `Map5_IRQ_Status &= ~0xc0` として in-frame / pending の両方を clear しています。

通常の NMI 使用 title では NMI vector fetch が後段で状態を消すため問題が表面化しにくいですが、NMI に依存しないコードを含め、frame-end detection と pending acknowledge の関係は fixture で確認しておく価値があります。

#### 修正方針

すぐに断定修正するより、MMC5 IRQ fixture で次を比較してから決めるのが安全です。

- visible scanline 239 → post-render 240 の transition
- `$5204` 未読の pending IRQ
- NMI disabled の場合
- `$4014` OAM DMA reset を挟む場合

現状の scanline-granular core という制約とは別に、frame-end state transition 自体は合わせられる可能性があります。

### P4: Mapper 1 — legacy iNES の PRG-RAM size fallback を 32 KiB compatibility policy にする

**重要度: 中〜低**

現在の `Map1_Decode_Ram_Size()` は legacy iNES で byte 8 が 0 の場合、conventional default として 8 KiB を確保します。

通常の SNROM には十分ですが、legacy dump では SOROM / SXROM の RAM topology を header から復元できない場合があります。

NESdev は Mapper 1 について、NES 2.0 がない場合は PRG-RAM size を仮定する必要があり、**32 KiB で既知 title すべてとの互換性に十分**としています。

参考:

- <https://www.nesdev.org/wiki/INES_Mapper_001>

#### 修正案

legacy iNES Mapper 1 で RAM size が不明な場合は 32 KiB backing を用意し、board wiring / mirroring 側で見える bank を制限する compatibility-first policy を検討します。

ただし save file size / battery topology への影響があるため、単純に `size = 0x8000` とするだけでなく、既存 `*.srm` compatibility を含めて決めるべきです。

## 小さな設計改善

### `getCurrentClocks32()` を正式 API として扱う

`infones/K6502.h` の宣言コメントは現在も Mapper 4 用の temporary timing probe という表現ですが、実際には Mapper 1 の consecutive write correctness に使われています。

現状では削除すると MMC1 が壊れるため、temporary probe ではありません。

例えば以下のように役割を明確化することを推奨します。

```cpp
int K6502_GetCurrentClock();
```

または関数名を維持する場合でも、コメントを「現在の CPU bus-visible clock を返す mapper timing API」へ変更するとよいです。

## 今後の推奨順序

今回のレビュー所見を実装へ反映する場合、次の順序を推奨します。

1. Mapper 5 fill / empty nametable write protection
2. Mapper 1 SZROM bank select
3. Mapper 5 frame-end / pending IRQ fixture と必要なら修正
4. Mapper 1 legacy PRG-RAM 32 KiB fallback policy
5. `getCurrentClocks32()` の正式 API 化 / コメント整理

1 と 2 は局所的で、既存実装を大きく崩さず compatibility を上げられます。
3 と 4 は timing / save compatibility の確認を含むため、fixture を追加してから採否を決める方が安全です。

## 結論

今回の Mapper 更新は、既存 InfoNES の mapper implementation を単に増補したものではなく、CPU / PPU / persistence を含めて cartridge-visible behavior を合わせる方向へ進んでいます。

特に Mapper 1 の RMW 対応と Mapper 5 の core integration は、今後ほかの timing-sensitive mapper を扱う際の基盤としても価値があります。

レビュー時点では Mapper 1 / 2 / 3 はかなり実用性が高く、Mapper 5 も PicoCalc の SRAM / scanline-granular core という制約を明記したうえで「多分動く」とする現在の判定は妥当です。

上記 P1〜P4 を追加で整理すれば、今回の Mapper 系更新はさらに安定した基盤になります。
