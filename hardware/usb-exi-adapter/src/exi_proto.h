/*
 * usb-exi-adapter - exi_proto.h
 *
 * GC 側（CleanRip の include/usb_exi.h）と共有するプロトコル定義。
 * ※ GC 側とは別ビルドのため、ここでは値を再定義する。変更時は両方を同期すること。
 *
 * ライセンス: MIT
 */

#ifndef EXI_PROTO_H
#define EXI_PROTO_H

#include <stdint.h>

/* プロトコル識別（PING 応答の上位 2 バイト） */
#define EXI_MAGIC0            'U'
#define EXI_MAGIC1            'D'
#define EXI_PROTO_VERSION     0x01

/* コマンド語 = [op:8][arg:24] */
#define CMD_PING              0x01
#define CMD_SET_LEN           0x10
#define CMD_SET_LBA_LO        0x11
#define CMD_SET_LBA_HI        0x12
#define CMD_SEND_CDB          0x20
#define CMD_READ_DATA         0x21
#define CMD_WRITE_DATA        0x22
#define CMD_STATUS            0x30
#define CMD_CAPACITY          0x40
#define CMD_USB_INFO          0x41

/* ステータスビット */
#define ST_READY              0x00000001u
#define ST_BUSY               0x00000002u
#define ST_ERROR              0x00000004u
#define ST_USB_ATTACHED       0x00000008u
#define ST_USB_MOUNTED        0x00000010u
#define ST_CDB_OK             0x00000020u
#define ST_CDB_FAIL           0x00000040u
#define ST_MEDIA_PRESENT      0x00000080u

/* 転送方向 */
#define DIR_IN                0
#define DIR_OUT               1

/* データバッファ長（GC raw 1 ブロック = 16 * 2064 = 33024） */
#define EXI_MAX_DATA          33024
#define EXI_MAX_CDB           16

#endif /* EXI_PROTO_H */
