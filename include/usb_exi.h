/**
 * CleanRip - usb_exi.h
 * Copyright (C) 2010-2026 emu_kidid
 *
 * 自作 USB-EXI アダプタ（RP2040）との通信プロトコル定義と、
 * GC 側クライアント API。仕様の詳細は docs/USB-EXI-ADAPTER.md を参照。
 *
 * このファイルは GPLv2 で配布される（LICENSE を参照）。
 **/

#ifndef USB_EXI_H
#define USB_EXI_H

#include <gccore.h>
#include <stdbool.h>
#include <stdint.h>

/*---------------------------------------------------------------
 * EXI 物理層の設定
 *   Slot A = EXI ch0 dev0 / Slot B = EXI ch1 dev0
 *   まずは低クロック（3.375MHz）で立ち上げ、検証後に上げる
 *---------------------------------------------------------------*/
#define USB_EXI_DEFAULT_CHANNEL  EXI_CHANNEL_0
#define USB_EXI_DEFAULT_DEVICE   EXI_DEVICE_0
#define USB_EXI_DEFAULT_SPEED    EXI_SPEED4MHZ

/*---------------------------------------------------------------
 * プロトコル定数
 *   1 トランザクション = 「32bit コマンド語 (+ 必要ならデータ語列)」
 *   コマンド語 = [op:8][arg:24]（ビッグエンディアンで転送）
 *---------------------------------------------------------------*/
#define USB_EXI_MAGIC_0        'U'
#define USB_EXI_MAGIC_1        'D'
#define USB_EXI_PROTO_VERSION  0x01

/* コマンド */
#define USB_EXI_CMD_PING        0x01 /* arg=0。読みで 1 語の ID を返す */
#define USB_EXI_CMD_SET_LEN     0x10 /* arg=転送バイト長 */
#define USB_EXI_CMD_SET_LBA_LO  0x11 /* arg=LBA[23:0] */
#define USB_EXI_CMD_SET_LBA_HI  0x12 /* arg=LBA[47:24] */
#define USB_EXI_CMD_SEND_CDB    0x20 /* arg=(cdb_len<<8)|方向。続けて CDB を書く */
#define USB_EXI_CMD_READ_DATA   0x21 /* ステージ済みデータを len バイト読む */
#define USB_EXI_CMD_WRITE_DATA  0x22 /* len バイトを書き込む（データアウト） */
#define USB_EXI_CMD_STATUS      0x30 /* 読みで 1 語のステータスを返す */
#define USB_EXI_CMD_CAPACITY    0x40 /* 読みで 2 語（総ブロック数, ブロック長） */
#define USB_EXI_CMD_USB_INFO    0x41 /* 読みで 4 語（vid,pid,class,proto） */

/* ステータスビット */
#define USB_EXI_ST_READY        0x00000001 /* コマンド受付可 */
#define USB_EXI_ST_BUSY         0x00000002 /* 処理中 */
#define USB_EXI_ST_ERROR        0x00000004 /* 内部エラー */
#define USB_EXI_ST_USB_ATTACHED 0x00000008 /* USB 機器検出済み */
#define USB_EXI_ST_USB_MOUNTED  0x00000010 /* mass storage マウント済み */
#define USB_EXI_ST_CDB_OK       0x00000020 /* 直前の CDB 成功 */
#define USB_EXI_ST_CDB_FAIL     0x00000040 /* 直前の CDB 失敗 */
#define USB_EXI_ST_MEDIA_PRESENT 0x00000080 /* メディア有り */

/* 転送方向 */
#define USB_EXI_DIR_IN   0
#define USB_EXI_DIR_OUT  1

/*---------------------------------------------------------------
 * 内部バッファ長
 *   GC raw は 1 ブロック = 16 セクタ = 16 * 2064 = 33024 バイト。
 *   まずは 1 セクタずつ扱い、後でまとめ読みに拡張する。
 *---------------------------------------------------------------*/
#define USB_EXI_MAX_DATA  33024

/*---------------------------------------------------------------
 * GC 側クライアント API（disc_source_usb_scsi.c で実装）
 *---------------------------------------------------------------*/

/* アダプタが接続されているか（PING 応答を確認） */
bool usb_exi_present(void);

/* アダプタの EXI 設定を変更する（0 で既定値に戻す）。init 前に呼ぶ */
void usb_exi_configure(int channel, int device, int speed);

/* アダプタ経由でディスクを初期化する。0 で成功 */
int  usb_exi_disc_init(void);

/* アダプタ経由で生セクターを読む。0 で成功 */
int  usb_exi_disc_read(void *dst, u32 len, u64 offset);

/* init 後に確定する情報 */
u32  usb_exi_disc_sector_size(void);
u32  usb_exi_disc_sector_count(void);

/* 直近のエラー文字列（日本語） */
const char *usb_exi_last_error(void);

/* 低レベル: CDB を送ってデータ相を処理する（CDB 長は 16 固定） */
int usb_exi_send_cdb(const u8 cdb[16], u32 data_len, int direction,
                     void *data, u32 *transferred);

/* CDB 長を指定できる版（READ(12)/READ(10)/0xE7 は 12 バイト長） */
int usb_exi_send_cdb_ex(const u8 *cdb, u8 cdb_len, u32 data_len, int direction,
                        void *data, u32 *transferred);

/* E7 のベースアドレス（ドライブ依存。GCC-4240N 等の MN103S は 0xA13000） */
void usb_exi_set_mem_base(u32 base);
u32  usb_exi_get_mem_base(void);

/*---------------------------------------------------------------
 * GC raw 読み出し（method12 相当。disc_source_usb_scsi.c で実装）
 *---------------------------------------------------------------*/
/* GC ディスク（raw モード）かどうか */
bool usb_exi_is_raw_mode(void);
/* raw をマスターとして扱うか（将来用。現状は ISO のみ返す） */
void usb_exi_set_keep_raw(bool keep);

#endif /* USB_EXI_H */
