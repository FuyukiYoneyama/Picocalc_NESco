# PPU-LCD パイプライン再設計計画 (2026-04-21)

この文書は、`Picocalc_NESco` の PPU 出力から LCD 表示までの経路を再設計して高速化するための正本設計です。

目的:

- `InfoNES_PostDrawLine()` の冗長な全画素再変換を減らす
- 途中の不安定な中間状態を増やさず、表示意味を壊さない
- 実装は分けても、確認は end-to-end でまとめて行う

## 1. 現在確認できている事実

### 1.1 現在の表示経路

- `InfoNES_PreDrawLine()` は `s_line_buffer` を `InfoNES` 側へ渡している。
  - `platform/display.c`
- `InfoNES_DrawLine()` は `WorkLine` に scanline 256px を描いている。
  - `infones/InfoNES.cpp`
- `InfoNES_PostDrawLine()` は `s_line_buffer` を再走査し、`convert_infones_color()` を通して RGB565 byte 列へ詰め替えている。
  - `platform/display.c`

### 1.2 `WorkLine` は純粋な色バッファではない

- `compositeSprite()` は `buf[i] & 0xF0` を見て背景側の優先判定をしている。
  - `infones/InfoNES.cpp`
- 同じ関数内で `buf[i] |= 0xF0` を行い、その判定用ビットを立て直している。
  - `infones/InfoNES.cpp`
- `K6502_rw.h` では palette mirror 書き込み時に
  `PalTable[...] = NesPalette[vramData] & 0xFF0F`
  を使っている。
  - `infones/K6502_rw.h`

このことから、`WorkLine` / `PalTable` の現在の値には、表示色以外の合成用意味が混ざっている。

### 1.3 `STRIP_HEIGHT` の比較だけでは本命を説明できていない

- 現在の tree は `STRIP_HEIGHT = 8` である。
  - `platform/display.h`
- `0.1.70` では `STRIP_HEIGHT = 16` の実験 build を作成し、実機ログ
  `pico20260421_194541.log`
  を取得した。
- その run では平均 `frames = 45.62` で、明確な改善とは言えなかった。

### 1.4 `PostDrawLine()` 単独最適化は表示破壊を起こした

- `0.1.71` の実験では `convert_infones_color()` を LUT 化し、`PostDrawLine()` 側を書き換えた。
- 実機ログ `pico20260421_200329.log` では【推定】フレームレート改善が見えた。
- しかし画像
  - `IMG_8260.JPG`
  - `IMG_8261.JPG`
  - `IMG_8262.JPG`
  では色化けが出た。
- 色 nibble の読み位置を修正した後も、画像
  `IMG_8263.JPG`
  では地形崩れが残った。

## 2. 問題の整理

現在の本質的な問題は、`WorkLine` が

- 背景色
- 背景の不透明 / 優先判定情報

を同じ `WORD` 値の中に持っていることです。

この状態では、

- `PostDrawLine()` だけを先に高速化する
- `WorkLine` だけ先に RGB565 に寄せる

のような部分変更が壊れやすいです。

## 3. 採用する設計方針

### 3.1 方針

最初に **背景不透明マスクを色値から分離**し、その後で色経路を最終形式へ寄せる。

つまり、

1. `WorkLine` の「色 + マーカー混在」をやめる
2. 背景不透明情報を別の scanline buffer として持つ
3. その後で `WorkLine` 側の色表現を RGB565 に近づける

という順で進める。

### 3.2 新しく追加するもの

- `BackgroundOpaqueLine[256]`
  - 型は `BYTE` または `bool` 相当
  - 1 pixel ごとに
    - `0`: 背景 palette index 0
    - `1`: 背景 palette index 1..3
    を保持する
  - `WorkLine` と同じ scanline 単位で毎回更新し、
    stale 値を次の line / frame へ持ち越さない

### 3.3 変更する箇所

#### フェーズ A: 表示意味を壊さない分離

- `InfoNES_DrawLine()` の背景描画で、
  `pPoint[x] = pal[...]`
  と同時に `BackgroundOpaqueLine[x]` も設定する
- `R1_SHOW_SCR` 無効時の全 line clear では、
  `WorkLine` だけでなく `BackgroundOpaqueLine` も全 0 にする
- BG 左端クリップ時は、
  `WorkLine[0..7]` の clear と同じ範囲の
  `BackgroundOpaqueLine[0..7]` も 0 にする
- 上下クリップで scanline 全体を消す分岐でも、
  `BackgroundOpaqueLine[0..255]` を全 0 にする
- `compositeSprite()` は `buf[i] & 0xF0` を見ず、
  `BackgroundOpaqueLine[i]` を使う
- `buf[i] |= 0xF0` は削除する
- `PalTable[...] = NesPalette[...] & 0xFF0F` のような marker 前提を不要化する

完了条件:

- `WorkLine` は純粋に色だけを持つ
- `0.1.69` と同じ表示になる
- 地形崩れや色化けがない

#### フェーズ B: 色経路の高速化

- 対象は Pico の active path に限定する
  - `platform/display.c`
  - `infones/K6502_rw.h`
  - `infones/InfoNES.cpp`
- `InfoNES_System.h` の公開シンボルとしての `NesPalette` 契約自体は、
  Linux / Win32 / PPC 系 backend を巻き込んで変更しない
- Pico build で実際に参照される `NesPalette` / `PalTable` / `WorkLine`
  の色表現を RGB565 前提に寄せる
- `InfoNES_PostDrawLine()` の `convert_infones_color()` を不要化する
- `PostDrawLine()` は
  - memcpy 相当
  - または 16bit copy + byte write
  だけへ縮小する

完了条件:

- `PostDrawLine()` に palette 変換ロジックが残らない
- `draw_us` が改善する
- 画像崩れがない

#### フェーズ C: 追加の最適化

- 必要なら LCD staging buffer との形式差をさらに詰める
- ただし `STRIP_HEIGHT` や DMA 戦略の再調整はこの後

## 4. 実装単位

今回の反省を踏まえ、途中の実機確認を細かく挟まず、次の単位でまとめて実装する。

1. `BackgroundOpaqueLine` 追加
2. `InfoNES_DrawLine()` 背景描画全箇所で opaque 設定
3. `compositeSprite()` を mask 参照へ変更
4. `0xF0` marker 依存コード削除
5. build
6. 実機で
   - `Xevious`
   - `LodeRunner`
   を end-to-end で確認

その後に、

7. RGB565 直接化
8. build
9. 同じ ROM で end-to-end 確認

## 5. 実機確認項目

フェーズ A:

- 色化けなし
- 地形崩れなし
- sprite priority 崩れなし
- `frames` / `draw_us` の基準値取得

フェーズ B:

- 上記 4 項目を維持
- `draw_us` の改善
- `cpu_pct` の悪化なし

## 6. 不採用とする進め方

- `PostDrawLine()` だけを先に変更する
- `WorkLine` を marker 混在のまま RGB565 化する
- `STRIP_HEIGHT` 変更を本命として先に触る

これらは、今回の実験結果から正本設計としては採用しない。
