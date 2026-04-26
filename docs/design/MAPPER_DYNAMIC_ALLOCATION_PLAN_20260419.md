# Mapper 動的確保計画 2026-04-19

## 目的

`Picocalc_NESco`
の
mapper 用一時 RAM を、
現在の
`g_MapperSharedRam`
中心の
静的 union 方式から、
mapper ごとの
`new[] / delete[]`
管理へ段階的に移行する。

同時に、
`Map30`
だけが持っている
個別 release を
たたき台として、
全 mapper に対して
解放漏れのない
共通 release 経路を設計する。

この文書では、
ファイル内容、
検索結果、
最終 ELF、
ユーザー確認内容として
明示的に確認できたものだけを
事実として書く。
そこから先は
`【推定】`
を付ける。

## 現状の事実

### 1. 現在の shared union

事実:
- `infones/InfoNES_Mapper.cpp`
  には
  `union MapperSharedRam`
  があり、
  現在の内容は次である。
  - `BYTE dram[DRAM_SIZE];`
  - `BYTE map6_chr_ram[MAP6_CHR_RAM_SIZE];`
  - `BYTE map19_chr_ram[0x2000];`
  - `BYTE map185_dummy_chr_rom[0x0400];`
  - `BYTE map188_dummy[0x2000];`
- 同ファイルでは、
  それぞれを
  次の global pointer へ割り当てている。
  - `DRAM`
  - `Map6_Chr_Ram`
  - `Map19_Chr_Ram`
  - `Map185_Dummy_Chr_Rom`
  - `Map188_Dummy`
- 最終 ELF では
  `g_MapperSharedRam`
  は
  `0xA000 = 40960`
  bytes
  である。

### 2. 現在有効な mapper

事実:
- `infones/InfoNES_Mapper.cpp`
  の
  `MapperTable`
  には少なくとも次が有効で入っている。
  - `{6, Map6_Init}`
  - `{19, Map19_Init}`
  - `{185, Map185_Init}`
  - `{188, Map188_Init}`
  - `{235, Map235_Init}`
- 最終 ELF でも
  `Map6_Init`
  `Map19_Init`
  `Map185_Init`
  `Map188_Init`
  `Map235_Init`
  は残っている。

### 3. 各 shared 資源の現在の使用先

事実:
- `DRAM`
  は、
  現在の tree では
  `InfoNES_Mapper_235.cpp`
  だけが使っている。
- `Map6_Chr_Ram`
  は
  `InfoNES_Mapper_006.cpp`
  で使われている。
- `Map19_Chr_Ram`
  は
  `InfoNES_Mapper_019.cpp`
  で使われている。
- `Map185_Dummy_Chr_Rom`
  は
  `InfoNES_Mapper_185.cpp`
  で使われている。
- `Map188_Dummy`
  は
  `InfoNES_Mapper_188.cpp`
  で使われている。

### 4. Map30 はすでに個別動的確保を持つ

事実:
- `InfoNES_Mapper_030.cpp`
  には
  `Map30_Init()`
  と
  `Map30_Release()`
  があり、
  現在
  `overlay-pool`
  `chr-ram`
  `flash-id`
  を
  `new (std::nothrow) BYTE[]`
  /
  `delete[]`
  で管理している。
- `InfoNES_ReleaseRom()`
  からは、
  現在
  `Map30_Release()`
  だけが特別に呼ばれている。

### 5. `infones` 側は C++ 翻訳単位である

事実:
- `InfoNES_Mapper.cpp`
  は C++ 翻訳単位であり、
  `mapper/*.cpp`
  を
  `#include`
  している。
- `InfoNES_Mapper_030.cpp`
  も C++ として build されている。

【推定】:
- mapper ごとの一時 RAM 管理を
  `infones`
  側へ寄せるなら、
  `malloc/free`
  より
  `new[]/delete[]`
  へ統一する方が
  コードスタイルとして自然である。

## 目標設計

### A. shared union を最終的に撤去する

事実:
- 現在の shared union は
  `Map30`
  以外の mapper 用一時 RAM を抱えている。

目標:
- `g_MapperSharedRam`
  を最終的に撤去する。
- 各 mapper は、
  自分の一時 RAM を
  自分の `.cpp`
  内で持つ。

### B. mapper ごとに `Init()` / `Release()` を対にする

目標:
- 少なくとも次を揃える。
  - `Map6_Release()`
  - `Map19_Release()`
  - `Map30_Release()`
  - `Map185_Release()`
  - `Map188_Release()`
  - `Map235_Release()`

【推定】:
- 動的確保を持たない mapper は
  空実装 release でもよい。
- ただし、
  API 形状だけは全 mapper で揃える方が、
  disable 中 mapper を戻す時に安全である。

### C. release 呼び出しは共通入口 1 本へ集約する

目標:
- `InfoNES_Mapper_ReleaseCurrent()`
  のような共通入口を追加する。
- `InfoNES_ReleaseRom()`
  は
  `Map30_Release()`
  の個別直呼びをやめて、
  この共通入口だけを呼ぶ。

期待する効果:
- mapper ごとの解放漏れを防ぐ
- disable 中 mapper を戻しても
  release 経路を追加し忘れにくくする

### D. 必要な RAM は mapper 起動時にまとめて確保する

事実:
- ユーザー指示として、
  「mapper 内で必要になったとき malloc する」のではなく、
  「起動時にすべて一括して malloc する」
  方針が示されている。

目標:
- 各 mapper は
  `Init()`
  の責務として、
  その mapper で必要な一時 RAM を
  起動時にまとめて確保する。

【推定】:
- ただし
  `Map30 overlay`
  のように、
  実際には起動継続に必須でない領域がある場合は、
  将来的に
  「必須領域」
  と
  「縮退可能領域」
  を分ける余地がある。
- まずは現在の挙動を壊さないことを優先し、
  既存 `Map30`
  相当の粒度へ揃える。

## 具体的な移行対象

### 1. Mapper 185

事実:
- `Map185_Dummy_Chr_Rom`
  は
  `0x0400 = 1024`
  bytes
  である。
- `Map185_Init()`
  で
  `0xff`
  埋めして使っている。

【推定】:
- 最も安全な移行対象である。
- `Map185_Init()`
  で
  `new[]`
  し、
  `Map185_Release()`
  で
  `delete[]`
  すればよい。

### 2. Mapper 188

事実:
- `Map188_Dummy`
  は
  `0x2000 = 8192`
  bytes
  である。
- `Map188_Init()`
  では
  `SRAMBANK = Map188_Dummy`
  とし、
  `Map188_Dummy[0] = 0x03`
  を書いている。

【推定】:
- 8KB で単純なため、
  初期移行対象として扱いやすい。

### 3. Mapper 19

事実:
- `Map19_Chr_Ram`
  は
  `0x2000 = 8192`
  bytes
  である。
- `InfoNES_Mapper_019.cpp`
  では
  `Map19_VROMPAGE(a)`
  を通して参照している。

【推定】:
- 8KB で、
  CHR-RAM 的な使い方も単純であり、
  `Map188`
  と同程度の難度である。

### 4. Mapper 6

事実:
- `Map6_Chr_Ram`
  は
  `0x8000 = 32768`
  bytes
  である。
- `InfoNES_Mapper_006.cpp`
  では
  PPU bank を
  `Map6_Chr_Ram`
  へ張っている。

【推定】:
- 32KB と大きいが、
  使い方は比較的単純である。
- 8KB 級より後で移すのが安全である。

### 5. Mapper 235

事実:
- `DRAM`
  は現在
  `DRAM_SIZE = 0xA000 = 40960`
  bytes
である。
- `DRAM`
  の実利用先は
  現在の tree では
  `Mapper 235`
  だけである。
- `InfoNES_Mapper_235.cpp`
  の
  `Map235_Init()`
  では、
  `DRAM[0..0x1fff]`
  を
  `0xFF`
  埋めしている。
- 同ファイルの
  `Map235_Write()`
  では、
  `byBus`
  条件時に
  `ROMBANK0..3 = DRAM`
  としている。
- 現在確認できるコード上では、
  `Map235`
  が
  `DRAM`
  の
  `0x2000`
  より先を直接読む / 書く箇所は
  見えていない。

【推定】:
- `DRAM_SIZE = 0xA000`
  は
  現在の shared union サイズ都合と
  旧設計の名残を含んでいる可能性がある。
- 現時点では、
  `Map235`
  を
  直ちに
  `40KB`
  動的確保対象と決め打ちしない。
- まず
  `Map235`
  が本当に必要とする最小サイズを
  再確認し、
  少なくとも
  `0x2000`
  /
  `0x8000`
  /
  `0xA000`
  のどれが妥当かを
  事実ベースで整理してから
  実装に入る。
- そのため
  `Map235`
  は、
  大きい mapper の最終段に置くが、
  実装前にサイズ確認 phase を挟む。

## phase 設計上の注意

### 1. 各 phase は単体で build 可能にする

事実:
- 現在
  `InfoNES_Mapper.h`
  には
  `extern BYTE *DRAM;`
  `extern BYTE *Map6_Chr_Ram;`
  `extern BYTE *Map19_Chr_Ram;`
  `extern BYTE *Map188_Dummy;`
  がある。
- 各 mapper は
  それらの識別子を直接参照している。

方針:
- phase 単位で
  build を壊さないことを優先する。
- そのため、
  各 mapper を移す phase では
  「mapper ローカル static pointer へ全面置換」
  を一気にやるのではなく、
  当面は
  既存 global symbol を
  alias / forwarding pointer
  として残す。

【推定】:
- たとえば
  `Map19_Chr_Ram`
  を mapper ローカル確保へ移す場合も、
  その phase では
  `InfoNES_Mapper.cpp`
  側の global pointer を
  残し、
  `Map19_Init()`
  がその pointer を張り替える形にすると、
  phase 単位で自己完結しやすい。
- shared union 撤去は、
  全 mapper の移行が終わった後に
  まとめて行う。

### 2. alloc failure の共通ルールを先に固定する

事実:
- 現在の
  `Map30_Init()`
  は
  段階別失敗ログを出し、
  失敗時には
  `Map30_Release()`
  を呼んでから
  `return`
  している。

方針:
- 以後の mapper 動的化も、
  すべて
  `Map30`
  と同じルールに揃える。

共通ルール:
1. `Init()`
   の先頭で
   既存 release を呼ぶ
2. 各確保は
   `new (std::nothrow) BYTE[]`
   を使う
3. どの段階で失敗したか
   分かる
   `InfoNES_Error`
   を出す
4. 途中失敗時は
   必ず
   `Release()`
   を呼んで
   partial init を巻き戻す
5. `Release()`
   は
   `delete[]`
   後に pointer を `nullptr`
   へ戻す
6. `Init()`
   が途中失敗した mapper では、
   `PPUBANK`
   `SRAMBANK`
   `ROMBANK`
   の最終設定前に
   return する

## 作業順

### フェーズ 1. 共通 release 入口を作る

やること:
- `InfoNES_Mapper.h`
  に
  共通 release 関数を宣言する
- `InfoNES_Mapper.cpp`
  に
  `InfoNES_Mapper_ReleaseCurrent()`
  を追加する
- `InfoNES_ReleaseRom()`
  から
  `Map30_Release()`
  直呼びを外し、
  共通入口を呼ぶ

完了条件:
- 現在の `Map30`
  が
  既存どおり release される
- release 呼び出し箇所が 1 箇所にまとまる

### フェーズ 2. 小さい mapper を移す

対象:
- `Map185`
- `Map188`
- `Map19`

やること:
- shared union 参照をやめる
- mapper ローカル確保へ寄せる
  ただし
  既存 global symbol は
  alias / forwarding pointer
  として残す
- `Init()` で `new[]`
- `Release()` で `delete[]`

完了条件:
- 3 mapper とも
  起動 / 戻り / 再起動で
  解放漏れがない

### フェーズ 3. 大きい mapper を移す

対象:
- `Map6`
- `Map235`

やること:
- `Map6_Chr_Ram`
  32KB を
  `Map6_Init()`
  で確保する
- `Map235`
  については、
  まず必要サイズを
  再確認する
- `Map235`
  の必要サイズが確定したあとで、
  そのサイズを
  `Map235_Init()`
  で確保する
- `Release()`
  を追加する

完了条件:
- `Map6`
  と
  `Map235`
  の
  shared union 依存が消える

### フェーズ 4. shared union を撤去する

やること:
- `MapperSharedRam`
  と
  `g_MapperSharedRam`
  を削除する
- `DRAM`
  `Map6_Chr_Ram`
  `Map19_Chr_Ram`
  `Map185_Dummy_Chr_Rom`
  `Map188_Dummy`
  の
  global 共有ポインタを整理する

完了条件:
- `InfoNES_Mapper.cpp`
  から
  shared union が消える
- mapper 一時 RAM が
  全て mapper ローカル管理へ移る

## 実装ルール

1. 1 phase ごとに build を通す
2. `Init()` / `Release()` は必ず対で入れる
3. `delete[]`
   後は pointer を `nullptr` へ戻す
4. release 失念を防ぐため、
   個別直呼びを増やさず
   共通入口へ集約する
5. 失敗ログは
   「どの確保が落ちたか」
   が分かる粒度を維持する

## 未解決事項

【推定】:
- disable 中の mapper を全部戻す最終形では、
  `Map30`
  と同じく
  「起動必須領域」
  と
  「縮退可能領域」
  を分ける mapper が
  他にも出るかもしれない。
- ただし現時点では、
  まず
  shared union 依存をなくし、
  release 共通化を先に完成させる方が
  優先度は高い。
