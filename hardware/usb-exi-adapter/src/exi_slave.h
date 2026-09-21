/*
 * usb-exi-adapter - exi_slave.h
 *
 * EXI スレーブ（RP2040）の低レベル実装。
 * GC が EXI マスタとして CLK/CS を駆動し、本アダプタは 1bit モードで
 * データを入出力する。プロトコル解釈は呼び出し側（main.c）が行う。
 *
 * ライセンス: MIT
 */

#ifndef EXI_SLAVE_H
#define EXI_SLAVE_H

#include <stdint.h>
#include <stdbool.h>

/*
 * EXI ピン割り当て（変更可）
 *   CS   : GC → アダプタ（チップセレクト、Low アクティブ）
 *   CLK  : GC → アダプタ（クロック）
 *   DATA : 双方向（1bit モードの D0）
 */
#define EXI_PIN_CS    2
#define EXI_PIN_CLK   3
#define EXI_PIN_DATA  4

/*
 * 1 トランザクション（CS アサート〜デアサート）の入出力記述。
 *   drive = true  : アダプタが DATA を駆動（GC からの読み出し）
 *   drive = false : アダプタが DATA をサンプル（GC からの書き込み）
 */
typedef struct {
	bool           drive;
	const uint8_t *tx;     /* drive=true のとき送るデータ */
	int            txlen;
	uint8_t       *rx;     /* drive=false のとき受けるバッファ */
	int            rxmax;
	int            rxlen;  /* 実際に受信したバイト数（出力） */
} exi_xfer_t;

/* ピン初期化 */
void exi_slave_init(void);

/* CS がアサートされるまで待ち、1 トランザクション分を処理する */
void exi_slave_run_once(exi_xfer_t *x);

/* CS が現在アサートされているか */
bool exi_slave_cs_asserted(void);

#endif /* EXI_SLAVE_H */
