# BG tile render LUT 再設計計画 (2026-04-21)

この文書は、`Picocalc_NESco` の背景描画 hot path の次段として、
`renderBgTile()` 内の 8 pixel 展開を LUT 化するための正本設計です。

対象:

- `infones/InfoNES.cpp`

目的:

- `0.1.73` で集約した背景 tile renderer の意味を壊さず、
  tile 内 8 pixel 展開コストを下げる
- `MapperPPU()` の呼び順、
  `BackgroundOpaqueLine` の line-start clear、
  left/right partial clip の意味を維持する

## 1. 現在確認できている事実

### 1.1 `0.1.73` では背景 tile renderer が 1 本化されている

- `InfoNES_DrawLine()` は
  `emitBgTile()`
  から
  `renderBgTile()`
  を呼び、
  その直後に
  `MapperPPU(PATTBL(...))`
  を呼ぶ
  逐次処理になっている。
- `renderBgTile()` は
  `pattern_row[0]`
  と
  `pattern_row[8]`
  を読み、
  8 pixel 分について
  `idx`
  を組み立てて
  `pal[idx]`
  と
  `idx != 0`
  を
  `dst`
  /
  `dst_opaque`
  へ書いている。

### 1.2 現在の `renderBgTile()` は pixel ごとの bit 展開をしている

現在の 1 pixel あたりの仕事は、
少なくとも

- `pl0 >> (7 - sx)` と mask
- `pl1 >> (7 - sx)` と mask
- 2 bit index 合成
- `pal[idx]`
- `idx != 0`

である。

したがって、1 tile あたりでは
8 回この組み立てを行っている。

### 1.3 clip があるので「常に 8 pixel 固定 memcpy」にはできない

- left partial tile は
  `clip_left = PPU_Scr_H_Bit`
- right partial tile は
  `clip_right = PPU_Scr_H_Bit`

で描画範囲が狭まる。

したがって、
「tile 1 枚を常に 8 pixel 丸ごと展開してそのまま書く」
だけでは不十分で、
partial clip を扱える必要がある。

## 2. 候補と効果見積もり

ここでは、現在の背景描画 path の中で次に削れる候補を 3 つに分ける。

### 2.1 候補 A: `renderBgTile()` の 8 pixel 展開を LUT 化する

内容:

- `pl0`
- `pl1`

の 2 plane byte から、
8 pixel 分の 2-bit index 列を LUT で引く。

方法候補:

1. 8 pixel 全体を 1 エントリで引く
2. 4 pixel + 4 pixel の half-tile LUT に分ける

【推定】効果:

- 背景描画 hot path の中では最大候補
- `InfoNES_DrawLine()` 背景部分に対して
  **15%〜30%**
  程度の削減余地
- `draw_us` 全体では
  **5%〜12%**
  程度の改善候補

理由:

- 可視 tile は scanline あたり最大 33
- 各 tile で 8 回の bit 展開をしている
- すでに tile descriptor 化で前処理は整理済みなので、
  次に残る密度の高い処理はここである

### 2.2 候補 B: attribute / palette base 解決を 4-tile 単位でまとめる

内容:

- `attrBase[tileX >> 2]`
- shift
- `&PalTable[...]`

を tile ごとではなく、4 tile 単位で再利用する

【推定】効果:

- 背景描画 hot path に対して
  **5%〜10%**
- `draw_us` 全体では
  **2%〜4%**

理由:

- 4 tile ごとに同じ attribute byte を使うので再利用余地はある
- ただし pixel 展開よりは計算密度が低い

### 2.3 候補 C: partial tile と full tile を別 renderer に分ける

内容:

- full tile 専用の高速版
- partial tile 専用の安全版

へ分ける

【推定】効果:

- 背景描画 hot path に対して
  **3%〜8%**
- `draw_us` 全体では
  **1%〜3%**

理由:

- 大半の tile は full tile なので clip 分岐を外せる
- ただし renderer 分岐が増え、設計の複雑さが戻る

## 3. 採用方針

次の本命は **候補 A** とする。

理由:

- もっとも計算密度が高い場所を直接削れる
- `0.1.73` で tile renderer が 1 本化済みなので適用しやすい
- `MapperPPU()` 順序や clip 意味を変えずに内側だけ速くできる

## 4. 採用する具体設計

### 4.1 8 pixel の 2-bit index 列を LUT 化する

新しく file-scope 内部で、
pattern 2 plane byte から 8 pixel index 列を引く LUT を持つ。

採用案:

- `half-tile` 単位
- 4 pixel 分ずつ

具体的には:

- high nibble 側用
- low nibble 側用

の 2 回参照で、
各 4 pixel の index 列を得る。

理由:

- 256 x 256 の full LUT より小さい
- RP2040 の RAM 消費を抑えやすい
- partial clip でも 4 pixel 単位なら使い回しやすい

配置方針:

- 正本は
  **RAM 常駐 LUT**
  とする
- 具体的には、
  `renderBgTile()`
  と同じ translation unit の
  file-scope static table として持つが、
  `const`
  で XIP flash 常駐させる前提ではなく、
  hot path 評価の初回は
  RAM 参照として扱う
- 理由は、
  `renderBgTile()`
  自体が
  `__not_in_flash_func`
  の SRAM 実行 hot path であり、
  LUT を flash 側へ置くと
  bit 展開削減ぶんを
  XIP 参照レイテンシで食う可能性があるため
- 比較実験として
  flash 常駐版を後から試すのはよいが、
  性能評価の基準値は
  RAM 常駐版を正とする

### 4.2 LUT の内容

各 LUT entry は 4 pixel 分について、

- `idx0`
- `idx1`
- `idx2`
- `idx3`

を 2 bit ごとに pack した
8 bit または 16 bit 値とする。

この packed 値から、
`pal[idx]`
と
`idx != 0`
を書き込む。

### 4.3 `renderBgTile()` の役割

`renderBgTile()` は残す。

ただし中では

1. `pl0`
   `pl1`
   から
   high nibble / low nibble 用の LUT entry を取る
2. 4 pixel ごとの packed index を使って
   `dst`
   と
   `dst_opaque`
   へ書く

だけにする。

### 4.4 partial clip の扱い

partial clip は現在どおり

- `clip_left`
- `clip_right`

で表す。

実装方針:

- full tile (`0..8`) は最短経路
- partial tile は clip 範囲だけ書く

ただし、
別 renderer には分けず、
1 本の renderer 内で
`clip_left == 0 && clip_right == 8`
の fast path を持つ。

## 5. 変更しないもの

- `MapperPPU()` の tile 順呼び出し
- `BackgroundOpaqueLine` の scanline 先頭全 0 初期化
- BG 左端 clip / 上下 clip 時の clear
- `emitBgTile()` の呼び順
- `PalTable` / `WorkLine` の意味

## 6. 実装単位

1. LUT 構築関数と static table を追加
2. `renderBgTile()` を LUT 利用版へ差し替え
3. full tile fast path と partial path を同一関数内で持つ
4. build
5. 実機で
   - `Xevious`
   - `LodeRunner`
   の表示確認
6. `frames`
   `draw_us`
   を `0.1.73` と比較

## 7. 完了条件

- `0.1.73` と同じ表示意味を維持する
- 背景崩れや sprite priority 崩れを出さない
- 【推定】`draw_us` が有意に改善する
