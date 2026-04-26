# BG tile 前処理再利用 再設計計画 (2026-04-21)

この文書は、`Picocalc_NESco` の背景描画 hot path の次段として、
tile ごとに繰り返している

- attribute 解決
- palette base 決定
- pattern row pointer 決定

の前処理を再利用するための正本設計です。

対象:

- `infones/InfoNES.cpp`

目的:

- `0.1.74` までに整理した
  `emitBgTile() -> renderBgTile() -> MapperPPU()`
  の逐次処理を壊さず、
  背景 tile 描画の前処理コストを下げる
- `MapperPPU()` の tile 順呼び出し、
  `BackgroundOpaqueLine` の line-start clear、
  partial clip の意味を維持する

## 1. 現在確認できている事実

### 1.1 `0.1.74` では tile renderer 内の bit 展開は LUT 化済み

- `renderBgTile()` は
  `pattern_row[0]`
  `pattern_row[8]`
  から
  LUT を 2 回引いて描画する形へ変わっている。
- したがって、次に残る背景描画コストは
  renderer の外側にある
  tile ごとの前処理である。

### 1.2 現在の `emitBgTile()` は tile ごとに同種の前処理を解いている

現在の 1 tile ごとの前処理は、
少なくとも

- `ch = *nameTablePtr`
- `bank = (ch >> 6) + bankOfsBG`
- `addrOfs = ((ch & 63) << 4) + yOfsModBG`
- `pattern_row = PPUBANK[bank] + addrOfs`
- `attrBase[tileX >> 2]`
- attribute shift
- `pal = &PalTable[...]`

である。

このうち、

- attribute byte は 4 tile ごとに共通
- palette base も 4 tile ごとに共通
- `yOfsModBG`
  と
  `bankOfsBG`
  は line 内で共通

である。

### 1.3 `MapperPPU()` の順序は変えてはいけない

現行の設計では、

- tile descriptor を解く
- その tile を描く
- 直後に `MapperPPU(PATTBL(...))`

という tile 順逐次処理を守っている。

したがって、
可視 tile 全体を先に展開してから後で描く設計にはしない。

## 2. 候補と効果見積もり

### 2.1 候補 A: attribute / palette base を 4-tile 単位で再利用する

内容:

- `attrBase[tileX >> 2]`
- shift
- `&PalTable[...]`

を tile ごとに計算せず、
4 tile ごとに 1 回だけ解く。

【推定】効果:

- 背景描画 hot path に対して
  **5%〜10%**
- `draw_us` 全体では
  **2%〜4%**

理由:

- 4 tile ごとに完全に同じ attribute byte を共有する
- renderer 内ほど密度は高くないが、可視 33 tile 全部で効く

### 2.2 候補 B: pattern row pointer 決定を table 化する

内容:

- `ch`
  から
  `bank`
  `addrOfs`
  `pattern_row`
  を取る処理を、
  line 内共通条件
  (`bankOfsBG`
  `yOfsModBG`)
  を前提に table 化する。

【推定】効果:

- 背景描画 hot path に対して
  **3%〜8%**
- `draw_us` 全体では
  **1%〜3%**

理由:

- 各 tile で確実に走るが、
  attribute 再利用ほどの共有性はない
- table 導入コストと RAM 増加の割に、
  効果は中程度に留まる可能性がある

### 2.3 候補 C: 4-tile block descriptor を導入する

内容:

- 1 tile descriptor ではなく
  4 tile block descriptor
  を導入し、
  共通 palette 情報を block 単位で持つ。

【推定】効果:

- 背景描画 hot path に対して
  **4%〜9%**
- `draw_us` 全体では
  **2%〜4%**

理由:

- palette 再利用と tile 逐次処理の折衷案になる
- ただし descriptor 構造が広がり、設計の複雑さが増す

## 3. 採用方針

次の本命は **候補 A** とする。

理由:

- もっとも単純で安全
- `MapperPPU()` の順序に影響しない
- `renderBgTile()` の LUT 化に続く次の削減点として自然
- 4 tile ごとの再利用というハードウェア都合に沿っている

## 4. 採用する具体設計

### 4.1 attribute state を 4-tile block 単位で持つ

`InfoNES_DrawLine()` の背景描画部分に、
現在の line 内で有効な

- current attribute byte
- current palette base pointer
- current attribute block index

を持つ local state を追加する。

保持単位:

- 4 tile block ごと

更新条件:

- `tileX >> 2`
  が変わったときだけ再計算

### 4.2 `emitBgTile()` は palette 解決済み state を受ける

現在の
`emitBgTile()`
は

- `attrBase`
- `tileX`

から内部で `pal` を解いている。

これを、

- 事前に解決済みの `pal`

を渡す形へ縮める。

ただし、
`MapperPPU()`
順序維持のため、
tile ごとの逐次呼び出しはそのままにする。

### 4.3 pattern row pointer は現段階では tile ごと計算のまま残す

`pattern_row`
まで同時に table 化すると範囲が広がるため、
この段では

- attribute / palette base 再利用

だけを対象にする。

pattern row pointer の table 化は、
この段の実測結果を見て必要なら次段に回す。

## 5. 変更しないもの

- `renderBgTile()` の LUT 方式
- `MapperPPU()` の tile 順呼び出し
- `BackgroundOpaqueLine` の scanline 先頭全 0 初期化
- BG 左端 clip / 上下 clip 時の clear
- partial tile と full tile の扱い

## 6. 実装単位

1. line 内 local の attribute reuse state を追加
2. `tileX >> 2` が変わったときだけ palette base を再計算
3. `emitBgTile()` は解決済み `pal` を受ける形に縮める
4. build
5. 実機で
   - `Xevious`
   - `LodeRunner`
   の表示確認
6. `frames`
   `draw_us`
   を `0.1.74` と比較

## 7. 完了条件

- `0.1.74` と同じ表示意味を維持する
- 背景崩れや sprite priority 崩れを出さない
- 【推定】`draw_us` が小幅でも改善する
