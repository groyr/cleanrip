# USB-EXI アダプタ 仕様（自作・RP2040）

GameCube から外付け USB 光学ドライブ（GCC-4240N 等）を読み出すための
自作アダプタの設計・プロトコル・結線・検証手順をまとめる。

## 背景

- 既製の **USB Dolphin**（makeo）は GC 側では **SD 互換デバイス**として振る舞い、
  ファームは **512B ブロック固定**・**生 SCSI CDB（`0xE7`）を送る口が無い**。
  → 光学ドライブの読み出しには使えない。
- そこで **RP2040 で同等のアダプタを自作**し、GC 側ドライバと合わせて
  「任意 CDB を送れる」経路を作る。`0xE7` も通常の `READ(10)` も送れる。

## 対象ハード

| 項目 | 内容 |
|---|---|
| コンソール | DOL-101（SP2 無し／メモカ Slot A/B あり／SP1 あり） |
| 入力 | 自作アダプタ（本ドキュメント） |
| 出力 | USB Dolphin Slot A/B 版（USB HDD/SSD）または SD Gecko |
| 接続 | 自作アダプタ = **Slot A (EXI ch0 dev0)**、USB Dolphin = Slot B (ch1 dev0) |

> Slot A/B は別 EXI チャネルなので同時動作できる。SP1 は ch0 dev2（今回は未使用）。

## 結線（Pico ↔ GC メモカ Slot）

GC のメモリカードスロットは EXI 1bit モード。

| 信号 | 方向 | Pico ピン（既定） |
|---|---|---|
| CS   | GC → Pico | GP2 |
| CLK  | GC → Pico | GP3 |
| DATA | 双方向（D0） | GP4 |
| GND  | — | GND |
| 3.3V | — | VSYS（Pico 電源） |

- 3.3V 同士なのでレベルシフト不要。各信号線に **直列 33〜100Ω** を入れる。
- 信号取り出しは **メモカ延長ケーブル**（またはジャンクメモカのコネクタ流用）。
- Pico の USB ポートは **ホスト**として使う（OTG アダプタで USB-A メス化）。
  そのため Pico への給電は USB ではなく **VSYS（GC 3.3V）** から行う。
- USB ホストは VBUS 5V を提示する必要がある。ドライブ側がセルフパワーでも
  **VBUS 5V（低電流）**を昇圧モジュール等から供給する。
- ピン番号は `src/exi_slave.h` の `EXI_PIN_*` で変更可能。

## EXI プロトコル

1 トランザクション = 1 回の CS アサート〜デアサート。データは **ビッグエンディアン**。

### コマンド語

```
[op:8][arg:24]   (32bit)
```

| op | 名前 | arg | 説明 |
|---|---|---|---|
| 0x01 | PING | 0 | 応答: `'U','D',ver,0`（4B） |
| 0x10 | SET_LEN | 転送バイト長 | 後続データ相の長さ |
| 0x11 | SET_LBA_LO | LBA[23:0] | 予約（GC は CDB に埋め込む） |
| 0x12 | SET_LBA_HI | LBA[47:24] | 予約 |
| 0x20 | SEND_CDB | `(cdb_len<<8) \| 方向` | 続く別トランザクションで CDB を 16B（余白 0 詰め）で書く |
| 0x21 | READ_DATA | 0 | ステージ済みデータを `SET_LEN` バイト読む |
| 0x22 | WRITE_DATA | 0 | `SET_LEN` バイトを書く（data-out） |
| 0x30 | STATUS | 0 | 状態語（4B） |
| 0x40 | CAPACITY | 0 | 総ブロック数・ブロック長（8B） |
| 0x41 | USB_INFO | 0 | dev_addr/lun/block_size（16B） |

### ステータスビット

| bit | 名前 | 意味 |
|---|---|---|
| 0 | READY | コマンド受付可 |
| 1 | BUSY | USB 転送中 |
| 2 | ERROR | 内部エラー |
| 3 | USB_ATTACHED | USB 機器検出 |
| 4 | USB_MOUNTED | mass storage マウント済み |
| 5 | CDB_OK | 直前の CDB 成功 |
| 6 | CDB_FAIL | 直前の CDB 失敗 |
| 7 | MEDIA_PRESENT | メディア有り |

### シーケンス（data-in の例: READ(10)）

```
GC: [SEND_CDB, DIR_IN]           (トランザクション1)
GC: CDB 16B                       (トランザクション2)
    → アダプタが USB へ CBW を発行（非同期）
GC: [STATUS] × n                  (BUSY が消えるまでポーリング)
GC: [SET_LEN = nbytes]
GC: [READ_DATA]
GC: データ nbytes を読む           (トランザクション)
```

data-out の場合は `SEND_CDB` の後 `WRITE_DATA` でデータを送ってから CDB を実行する。

## ブリングアップ手順（検証ステージ）

1. **EXI ID 読み**: `PING` が `'U','D'` を返す
2. **USB INQUIRY**: ドライブの INQUIRY 応答が取れる
3. **READ CAPACITY**: ブロック長 512/2048/2064 を確認
4. **通常 DVD READ(10)**: データ DVD を読み、PC と MD5 一致
5. **`0xE7`（GC ディスク）**: method12 相当で GC raw を取得
6. **全ディスクダンプ**: ISO/RAW の MD5 を PC `friidump` と一致

## GC raw 読み出し（method12 相当）

GC ディスクは通常 READ(10) のデータが「ドライブの逆スクランブル鍵」で XOR されている
ため、そのままでは使えない。`disc_source_usb_scsi.c` は friidump の method12 を移植し、
以下で解除する（`source/disc_scramble.c` にスクランブル解除と EDC 検証を実装）。

1. `READ(12)` streaming を **2 回**発行（1 回目は捨てる）→ host データ `rd`（32768B）
2. `E7` で各セクタの生フレーム先頭 12B と末尾 10B を取得
3. `raw[12:2060] = rd XOR drive_cipher12[blk%16]`
4. 16 セクタをまとめてスクランブル解除し **EDC 検証**
5. `drive_cipher12` は代表 16 ブロック（16..31）から校正（`0xE7` 全ブロック読み）

- `E7` のベースアドレスはドライブ依存。**GCC-4160N/4240N は `0xA13000`**
  （`usb_exi_set_mem_base()` で変更可。他機種は今後 `drive_profile` 化）
- シードはブルートフォースで探索し、16 ブロック周期でキャッシュする
- 速度は `0xE7` 律速（~0.3MB/s）。全ディスクで 60〜90 分を見込む

## 既知の要検証点

- **エッジ位相**: `exi_slave.c` の `EXI_SAMPLE_ON_RISING` / `EXI_DRIVE_ON_RISING` は
  実機でロジアナ確認して確定する（メモカ/SD の位相に合わせる）。
- **USB 速度**: RP2040 のネイティブ USB は **フルスピード(12Mbps)** のみ。
  一部ブリッジが FS で動かない場合は HS 対応 MCU（STM32F723 等）へ切り替える。
  EXI プロトコルは MCU 非依存なので GC 側ドライバはそのまま使える。
- **EXI 帯域と所要時間**: 現状は CPU ビットバン + `EXI_SPEED4MHZ`(3.375MHz ≒ 0.42MB/s)。
  1 ブロックあたり約 65KB を EXI で運ぶため、そのままでは律速になる。

  | EXI 設定 | 実効 | 全ディスク概算 |
  |---|---|---|
  | 4MHz（現既定・ビットバン） | ~0.42MB/s | ~1.9 時間 |
  | 16MHz（**要 PIO**） | ~1.7MB/s | ~45 分 |
  | 32MHz（**要 PIO**） | ~3.4MB/s | ~25 分 |

  → 速度はドライブ側 `0xE7` 律速（~0.3MB/s）に合わせられれば十分なので、
  **PIO 化で 16MHz 以上**に上げるのが次の作業。まずは 4MHz で正しさを確認する。
- **速度**: `0xE7` は転送律速（~0.3MB/s）。ドライブ側だけで 60〜90 分を見込む。
- **PIO 化**: 現状は CPU ビットバン。EXI クロックを低く（1〜4MHz）運用する前提。
  高速化する場合は PIO 実装に置き換える。

## 参考

- WiiBrew: Hardware/External Interface（EXI チャネル/CS 割り当て）
- YAGCD ch.10 EXI Devices
- GC-Forever Wiki: IDE-EXI（レジスタ露出型の手本）
- `friidump` フォーク `PATCHES.md`（`0xE7` / method12 の知見）
