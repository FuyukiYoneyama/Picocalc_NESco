# スクリーンショット機能 インターフェース仕様書 2026-04-23

## 目的

スクリーンショット機能のモジュール境界で必要になる
関数インターフェース、
データ形式、
状態遷移上の約束を固定する。

この文書では、
コード内容、
既存設計メモ、
ユーザー指示として確認できたものだけを
事実として書く。
未実装 API や戻り値設計は
`【推定】`
を付ける。

## 現状の確認結果

事実:
- `drivers/lcd_spi.c`
  には
  `lcd_set_window()`
  `lcd_dma_write_rgb565_async()`
  `lcd_dma_write_bytes_async()`
  `lcd_dma_wait()`
  がある。
- `platform/display.c`
  では
  `lcd_dma_wait()`
  を利用して描画 strip の完了待ちを行っている。
- 確認した範囲のコードには、
  LCD readback API は存在しない。
- 確認した範囲のコードには、
  screenshot 保存用 API は存在しない。
- ユーザー指示で、
  スクリーンショット実行キーは
  `F5`
  とすることが決定した。
- ユーザー指示で、
  `F5`
  の reset 割り当ては外し、
  reset は
  `F1`
  のみへ残すことが決定した。
- `drivers/lcd_spi.c`
  の
  `lcd_set_window()`
  は、
  確認した範囲では最後に
  `0x2C`
  を送っている。

## 状態インターフェース

初期版では、
外部から観測可能な screenshot 状態を次に固定する。

### `idle`

- 未要求
- busy ではない

### `pending`

- 要求済み
- 次の VBLANK 待ち

### `capturing`

- LCD DMA 完了待ちを終え、
  readback 中

### `saving`

- BMP 保存中

### `error`

- 失敗後の記録状態
- 初期版では内部状態のみでもよい

## emulation 制御 I/F

入力契約:
- `F5`
  は screenshot 要求入力として扱う。
- `F1`
  は reset 入力として扱う。

初期版の system bit 契約:
- `PAD_SYS_SCREENSHOT`
  を新設し、
  `F5`
  をそこへ割り当てる。
- screenshot 要求は
  `PAD_System`
  経由で VBLANK 側へ渡す。

【推定】:

```c
bool nesco_request_screenshot(void);
```

意味:
- screenshot 要求を 1 件受け付ける。

呼び出し前提:
- game 実行中に呼ぶ。

戻り値:
- `true`:
  pending を受理した。
- `false`:
  busy または pending 中で受理しなかった。

ブロッキング:
- 非ブロッキング。

```c
void nesco_maybe_start_screenshot_on_vblank(void);
```

意味:
- VBLANK で pending 要求を消化する。

呼び出し前提:
- VBLANK 境界からのみ呼ぶ。

ブロッキング:
- 初期版ではブロッキング可。

## LCD readback I/F

### DMA wait

事実:
- `lcd_dma_wait()`
  は既に存在する。

```c
void lcd_dma_wait(void);
```

意味:
- 進行中の LCD DMA 転送が完了するまで待つ。

初期版での扱い:
- screenshot 開始直前に必ず呼ぶ。

### readback API

【推定】:

```c
bool lcd_readback_rect_rgb565(int x, int y, int w, int h, WORD *dst_pixels);
```

意味:
- LCD GRAM から矩形領域を readback し、
  `WORD`
  配列へ RGB565 として格納する。

呼び出し前提:
- `lcd_dma_wait()`
  完了後であること。
- `dst_pixels`
  は
  `w * h`
  要素以上を持つこと。

戻り値:
- `true`:
  成功
- `false`:
  readback 失敗

ブロッキング:
- ブロッキング。

初期版の実使用:

```c
lcd_readback_rect_rgb565(0, 0, 320, 320, dst_pixels)
```

### readback 手順の固定

初期版では、
`lcd_readback_rect_rgb565()`
  の内部手順を次で固定する。

```text
1. validate arguments
2. lcd_dma_wait()
3. switch LCD read clock to safe slow setting
4. set read window by lcd_set_read_address_window(x, y, w, h)
5. issue RAMRD command
6. discard required dummy read bytes
7. read w * h pixels from LCD over MISO
8. normalize byte order into RGB565 WORD array
9. restore normal write clock setting
10. return success / failure
```

### readback 実装条件

初期版では次を仕様として固定する。

- `RAMRD`
  コマンドを使って LCD GRAM を読む
- 読み出し前に必ず
  `lcd_dma_wait()`
  を実行する
- readback の address 設定には
  `lcd_set_read_address_window()`
  を使う
- 既存
  `lcd_set_window()`
  は write 専用 helper として扱い、
  readback では使わない
- read 時は write 時より安全側の低速クロックへ切り替える
- 読み出しには
  `MISO`
  を使う
- dummy read の吸収は
  `lcd_readback_rect_rgb565()`
  の内部責務とする
- 失敗時は
  `false`
  を返し、
  部分成功データは正とみなさない

### driver 変更点の固定

【推定】:
- `drivers/lcd_spi.c`
  で
  `MISO`
  を readback 用に有効化する初期化が必要になる。
- `lcd_set_read_address_window(int x, int y, int w, int h)`
  を新設し、
  これは address range 設定だけを行い
  `RAMWR`
  を送らない。
- `lcd_set_read_address_window()`
  の宣言と実装は
  `drivers/lcd_spi.c`
  に閉じ、
  外部公開しない。
- 外部から使うのは
  `lcd_readback_rect_rgb565()`
  のみとし、
  read helper は driver 内部 helper として扱う。
- readback 用の SPI / PIO read 経路を追加し、
  write 専用前提を崩す変更は driver 層に閉じ込める。
- readback 中の clock 切替 API を driver 内部 helper として持つ。

### readback 失敗条件

【推定】:
- 引数不正
- read window 設定不能
- 想定 byte 数を最後まで読めない
- dummy read 後のデータ長が不足する
- read クロック復帰に失敗する

のいずれかで
`false`
を返す。

### readback 生データの扱い

事実:
- ユーザー提示メモでは、
  dummy read の吸収と byte order 調整が必要とされている。

【推定】:
- dummy read の吸収は
  `lcd_readback_rect_rgb565()`
  の内部責務とする。
- 呼び出し側へは整形後の RGB565 を返す。

## BMP 保存 I/F

【推定】:

```c
bool storage_save_screenshot_bmp(const char *path,
                                 int width,
                                 int height,
                                 const WORD *rgb565_pixels);
```

意味:
- RGB565 画素から BMP を生成し、
  指定 path に保存する。
- 初期版の出力 BMP は
  非圧縮
  `24bpp`
  とする。

呼び出し前提:
- `path`
  が有効であること。
- `rgb565_pixels`
  は
  `width * height`
  要素を持つこと。

戻り値:
- `true`:
  保存成功
- `false`:
  保存失敗

ブロッキング:
- ブロッキング。

## データ形式

### readback 出力形式

【推定】:
- 初期版の readback 出力は RGB565 とする。
- 理由は、
  現在の LCD 書き込みパスが RGB565 ベースであり、
  既存コードとの整合を取りやすいため。

### byte order

事実:
- `lcd_dma_write_rgb565_async()`
  は
  `WORD`
  画素を上位 byte、
  下位 byte の順で DMA バッファへ展開している。

【推定】:
- readback 側でも、
  呼び出し側へ返す最終画素は host 側で扱いやすい RGB565 `WORD`
  へ正規化する。

### BMP 側画素順

【推定】:
- BMP 書き込みモジュールで、
  行順は BMP 仕様に合わせて bottom-up に並べる。
- 将来 top-down BMP を採る余地はあるが、
  初期版では標準的な bottom-up を優先する。

### BMP 側画素形式

初期版では、
BMP 出力を
非圧縮
`24bpp`
に固定する。

理由:
- 一般的なビューアとの互換性を優先するため。
- `RGB565`
  の `BI_BITFIELDS`
  対応有無に依存しないため。

### 色変換責務

【推定】:
- LCD readback API は RGB565 正規化まで担当する。
- BMP 保存 API は RGB565 から 24bpp BMP への変換、
  BMP header 作成、
  行順整形まで担当する。

### BMP 保存手順の固定

初期版では、
`storage_save_screenshot_bmp()`
  の内部手順を次で固定する。

```text
1. validate arguments
2. compute row_stride = width * 3
3. compute row_padding to 4-byte alignment
4. build BMP file header + info header
5. for each source row from bottom to top:
       convert RGB565 -> 24bpp BGR
       append row padding bytes
6. write headers and pixel rows to file
7. return success / failure
```

### BMP 変換仕様

初期版では次を仕様として固定する。

- BMP の各行は
  `4 byte`
  境界へ padding する
- 画素並びは
  `B, G, R`
  の
  `24bpp`
  とする
- 行順は bottom-up とする
- RGB565 から BGR24 への変換責務は
  `storage_save_screenshot_bmp()`
  内部に持たせる
- 呼び出し側は
  RGB565 バッファだけを渡す

## エラーコード I/F

【推定】:

```c
typedef enum nesco_screenshot_result_t {
    NESCO_SCREENSHOT_OK = 0,
    NESCO_SCREENSHOT_BUSY,
    NESCO_SCREENSHOT_DMA_TIMEOUT,
    NESCO_SCREENSHOT_READBACK_FAILED,
    NESCO_SCREENSHOT_SD_OPEN_FAILED,
    NESCO_SCREENSHOT_SD_WRITE_FAILED,
} nesco_screenshot_result_t;
```

使い方:
- 内部記録またはログ出力に使う。
- 初期版でユーザー向け表示がなくてもよい。

## busy 中再要求の契約

初期版では次を契約とする。

【推定】:
- `busy` または `pending` 中の追加要求は受理しない。
- 受理しない場合は `false`
  または `NESCO_SCREENSHOT_BUSY`
  を返す。

## モジュール境界の要約

- emulation 側:
  pending / busy 制御と VBLANK 起点判定
- LCD 側:
  DMA 完了待ちと readback 正規化
- storage 側:
  BMP 化と SD 保存

この分担を越えて責務を混ぜないことを、
初期版のインターフェース原則とする。
