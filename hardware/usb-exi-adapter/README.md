# usb-exi-adapter（RP2040 ファームウェア）

GameCube の EXI（メモリカード Slot A/B）に接続する「USB ホストアダプタ」の
ファームウェア。GC 上の CleanRip から外付け USB 光学ドライブを読み出す。

仕様の詳細は `../../docs/USB-EXI-ADAPTER.md` を参照。

## 役割

- core0: TinyUSB ホスト（mass storage / BOT）。`tuh_msc_scsi_command()` で
  **任意の SCSI CDB**（`0xE7` を含む）を送る。
- core1: EXI スレーブ。GC からのコマンドを解釈し、USB 転送を仲介する。

## ビルド

```sh
# pico-sdk を用意
git clone https://github.com/raspberrypi/pico-sdk
export PICO_SDK_PATH=$(pwd)/pico-sdk

cmake -S hardware/usb-exi-adapter -B hardware/usb-exi-adapter/build
cmake --build hardware/usb-exi-adapter/build -j
# → build/usb_exi_adapter.uf2
```

BOOTSEL を押しながら Pico を接続し、`.uf2` をコピーして書き込む。

## ピン（既定）

| 信号 | Pico |
|---|---|
| CS   | GP2 |
| CLK  | GP3 |
| DATA | GP4 |

変更する場合は `src/exi_slave.h` の `EXI_PIN_*` を編集する。

## 電源

- Pico は GC の 3.3V を **VSYS** へ供給する（USB ポートはホストとして使うため給電不可）。
- USB ホストとして **VBUS 5V** を提示する必要がある。ドライブがセルフパワーでも
  検出のため低電流の 5V を昇圧モジュール等から VBUS へ供給する。

## 状態

- [x] プロトコル実装（EXI スレーブ / USB ホスト）
- [ ] 実機ブリングアップ（EXI ID → INQUIRY → READ CAPACITY → READ(10) → 0xE7）
- [ ] エッジ位相の確定（ロジアナ）
- [ ] PIO 化による高速化（任意）
