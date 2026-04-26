# BG DrawLine hot path 再設計計画 (2026-04-21)

この文書は、`Picocalc_NESco` の背景描画 hot path を再設計して高速化するための正本設計です。

対象:

- `infones/InfoNES.cpp`

目的:

- `0.1.72` で回復した表示意味を維持したまま、`InfoNES_DrawLine()` の背景描画コストを下げる
- `left partial / main tiles / right partial` の三重実装を整理し、同時に最適化する
- 中間状態を増やさず、確認は end-to-end でまとめて行う

## 1. 現在確認できている事実

### 1.1 現在の表示基準は `0.1.72`

- `0.1.71` では一部背景破綻が出た。
- `0.1.72` では、
  - `NesPalette` 契約は元の RGB444-like source data に戻し
  - `PalTable` に入る時だけ RGB565 化する
  形へ絞り直した。
- ユーザー確認では、`pico20260421_205607.log` の時点で
  「直ったようです」と報告されている。

したがって、以後の背景描画高速化は **`0.1.72` の表示を壊さないこと** を前提にする。

### 1.2 現在の背景描画には三重実装がある

`InfoNES_DrawLine()` の背景描画は、現在次の 3 経路に分かれている。

1. 左端 partial block
2. main tiles (`putBG` lambda)
3. 右端 partial block

いずれも本質的には

- attribute から palette base を決める
- pattern table から 2 plane byte を読む
- 8 pixel 分の色を `WorkLine` に書く
- 8 pixel 分の不透明マスクを `BackgroundOpaqueLine` に書く

という同じ仕事をしている。

### 1.3 main tiles と partial block で展開方法が違う

main tiles の `putBG()` は

- `palAddr`
- `readPal()`
- `pat0` / `pat1`

を使う 8 pixel 展開になっている。

一方、left/right partial block は

- 似た pattern 展開を別の式で作り
- `switch` のフォールスルーで部分書き込みしている

ため、意味の同期が難しい。

### 1.4 背景描画 hot path は依然として重い

`0.1.72` のログ `pico20260421_205607.log`
では、表示は直ったが `draw_us` は依然として大きい。

この時点で、`PostDrawLine()` 側の無駄はかなり減っているので、
次の本命は `InfoNES_DrawLine()` の背景展開そのものと考えるのが自然である。

## 2. 問題の整理

現在の背景描画 hot path の問題は 3 つある。

### 2.1 同じ意味のコードが 3 箇所に分かれている

- left partial
- main tiles
- right partial

で実装が分かれているため、

- 最適化するときに 1 箇所だけ速くなる
- 1 箇所だけ壊れる
- `BackgroundOpaqueLine` の更新規則がずれる

リスクが高い。

### 2.2 1 tile ごとの前処理がその場で何度も解かれている

各 tile でその場で行っているのは、

- `pAttrBase[nX >> 2]` の読み
- attribute shift
- `pal = &PalTable[...]`
- `ch` から `bank` / `addrOfs` の計算
- `data[0]` / `data[8]` の読み

である。

このうち、左端・中央・右端で同じ tile を扱う条件でも、似た計算を別々にしている。

### 2.3 partial block のためだけに別アルゴリズムを持っている

partial block は表示境界の都合だが、
現在は「tile の一部だけを書く」という境界処理のために、
tile 展開そのものまで別実装になっている。

## 3. 採用する設計方針

### 3.1 基本方針

背景描画は、

1. **scanline 上の可視 tile 列を先に解く**
2. **tile を描く関数は 1 種類にそろえる**
3. **左端 / 右端 clip は tile renderer の外側パラメータで処理する**

という構造に変える。

つまり、現在の

- left partial 実装
- main 実装
- right partial 実装

を別々に最適化するのではなく、
**「1 tile を描く正本実装」へ寄せてから速くする**。

### 3.2 導入する内部概念

新しく scanline 内部用として、次の概念を持つ。

#### `BgTileDescriptor`

1 tile 分の背景描画に必要な情報をまとめた内部 descriptor。

含める内容:

- `const BYTE *pattern_row`
  - その tile の現在 row に対応する pattern data 先頭
- `const WORD *pal`
  - `PalTable` 上の 4 色 palette base
- `int clip_left`
  - tile 左から何 pixel 飛ばすか
- `int clip_right`
  - tile 右端まで何 pixel 書くか

保持単位:

- 可視 scanline あたり最大 33 tile

これは file-scope の static 配列ではなく、
`InfoNES_DrawLine()` 内の local 配列として持つ。

理由:

- stale state を持ち込まない
- mapper / scroll 状態の current line 依存を毎回明示的に計算する

### 3.3 背景描画を 2 段に分ける

#### フェーズ C1: descriptor build

scanline 先頭で、

- 現在の scroll / nametable / row / attribute 状態から
- 可視 33 tile 分の `BgTileDescriptor`

を **tile 順に逐次** 解く。

ここで処理するのは、

- horizontal nametable wrap
- vertical row select
- attr から palette base
- pattern row pointer 決定
- first tile / last tile の clip 計算

まで。

重要:

- `MapperPPU()` は現行と同じ tile 順で呼ぶ
- descriptor build では、mapper callback により変化しうる tile data を先取りしない
- つまり「可視 tile 全部を先に解いてから後でまとめて描く」形にはしない
- 正本は
  - tile の descriptor を解く
  - その tile を描く
  - その直後に `MapperPPU(PATTBL(...))` を呼ぶ
  という逐次処理である

#### フェーズ C2: tile render

`BgTileDescriptor` を 0..N まで順に回して、
1 種類の tile renderer で

- `WorkLine`
- `BackgroundOpaqueLine`

へ書く。

tile renderer の責務は、

- `pattern_row[0]` / `pattern_row[8]` を読む
- 8 pixel 分の色と opaque を作る
- `clip_left..clip_right` だけ書く

だけに限定する。

`BackgroundOpaqueLine` については、

- scanline 先頭の
  `InfoNES_MemorySet(BackgroundOpaqueLine, 0, NES_DISP_WIDTH)`
  は renderer の外で維持する
- renderer は「今回描く pixel 範囲だけ」を 1/0 で上書きする責務に限定する

これにより、partial 未描画部分や clip 領域で stale な opaque bit を残さない。

### 3.4 tile renderer の最適化方針

tile renderer の中では、palette 選択を都度 pointer 追跡せず、
あらかじめ

- `c0 = pal[0]`
- `c1 = pal[1]`
- `c2 = pal[2]`
- `c3 = pal[3]`

を local register に取る。

また opaque は、

- `idx != 0`

だけなので、色選択と同時に作る。

この設計により、現在の

- `readPal()` 関数風アクセス
- `switch` フォールスルー partial 展開

をどちらも不要にする。

## 4. 実装単位

今回の反省を踏まえ、次は次の単位でまとめて実装する。

1. `BgTileDescriptor[33]` を `InfoNES_DrawLine()` の local 配列として追加
2. 現在の left/main/right の前処理を descriptor build へ集約
3. `renderBgTile()` を 1 本追加
4. left/main/right の個別 pixel 展開コードを削除
5. `BackgroundOpaqueLine` 更新を `renderBgTile()` へ一本化するが、
   scanline 先頭の全 0 初期化は renderer の外で維持する
6. build
7. 実機で
   - `Xevious`
   - `LodeRunner`
   を end-to-end で確認

## 5. 完了条件

- `InfoNES_DrawLine()` 背景描画の tile 展開実装が 1 箇所に集約されている
- `0.1.72` と同じ見た目を維持する
- `BackgroundOpaqueLine` の更新規則が partial / main / clip でずれない
- `frames` と `draw_us` が悪化しない

## 6. 不採用とする進め方

- left/main/right を別々にいじる
- partial block だけ別ロジックのまま残す
- `putBG()` だけを先に速くして、partial block を後回しにする

これらは再び「中間状態の一部だけ壊れる」進め方になるので採用しない。
