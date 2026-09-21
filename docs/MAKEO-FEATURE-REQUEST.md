# makeo（USB Dolphin）への要望文面

`makeo/usb-dolphin` の Discord へ送るための下書き。
そのまま貼れるように英語版と日本語版を用意する。

---

## English（Discord 貼り付け用）

> **Feature request: raw SCSI passthrough for USB optical drives**
>
> Hi! I'd love to use USB Dolphin to dump GameCube discs from an external USB optical
> drive (e.g. a GCC-4240N in a USB enclosure) on real GC hardware.
>
> Two things block this today:
>
> 1. **Block size**: the firmware only supports 512-byte block devices. Optical drives
>    report 2048-byte logical blocks (and GC raw frames are 2064 bytes), so they never mount
>    (the red LED blinks slowly).
> 2. **No raw CDB path**: on the GC side the device looks like an SD card, so there is no way
>    to send a raw/vendor SCSI CDB. These drives need vendor opcode **`0xE7`** to read
>    GameCube raw sectors (plus plain `READ(10)`/`READ(12)` for the fast path).
>
> Would you consider adding:
>
> - an EXI opcode for **raw SCSI CDB passthrough** (CBW-like: `CDB[16]`, data length,
>   direction), and
> - support for **2048/2064-byte block size** and peripheral device type `0x05` (optical)?
>
> Even just the bare raw-CDB opcode (without FAT support) would already unblock this.
>
> To make it easy, I can provide:
> - a GC-side test app that runs `INQUIRY` / `READ CAPACITY` / `READ(10)` / `0xE7`, and
> - the exact EXI spec I would need (opcode + status layout).
>
> I'm happy to test and contribute. Thanks for the great hardware!

## 日本語（要点メモ）

- 目的: USB Dolphin 経由で **外付け USB 光学ドライブ**から GC ディスクを吸い出したい。
- 阻害要因:
  1. **512B ブロック固定** → 光学ドライブ（2048B）がマウント不可（赤 LED 点滅）
  2. GC 側が **SD エミュレーション**のため **生 SCSI CDB を送れない**。
     GC raw 読みには vendor コマンド **`0xE7`** が必要（通常 `READ(10)` も）。
- 要望:
  - **生 CDB パススルー**用の EXI コマンド（`CDB[16]` + データ長 + 方向）
  - **2048/2064B ブロック**と **peripheral type 0x05(optical)** の許容
- 提供可能: GC 側テストアプリ（`INQUIRY`/`READ CAPACITY`/`READ(10)`/`0xE7`）と必要仕様
- 補足: 生 CDB だけでも（FAT 無しでも）用途は成立する

---

## 送信先

- Discord: `https://discord.gg/YtA9aU3BKZ`（`makeo/usb-dolphin` README 記載）
  - 投稿先は **`dev`**（無ければ `usb dolphin general`）。`support`/`showcase` は用途違い
- GitHub Issue（作成済み）: https://github.com/makeo/usb-dolphin/issues/1
- 参考: 自作アダプタの実装は `docs/USB-EXI-ADAPTER.md` を参照
