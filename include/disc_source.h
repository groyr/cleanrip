/**
 * CleanRip - disc_source.h
 * Copyright (C) 2010-2026 emu_kidid
 *
 * ディスク読み出し元（ソース）の抽象インターフェース。
 * 内蔵 DVD ドライブ (DI) と、外部 USB 光学ドライブ（USB-EXI アダプタ）を
 * 同一の API で扱えるようにするための薄いレイヤー。
 *
 * このファイルは GPLv2 で配布される（LICENSE を参照）。
 *
 * 元の CleanRip: https://github.com/emukidid/cleanrip
 **/

#ifndef DISC_SOURCE_H
#define DISC_SOURCE_H

#include <gccore.h>
#include <stdbool.h>
#include <stdint.h>

/* ディスクソース 1 つ分の定義。
 * - available(): そのソースが現在利用可能か（デバイス接続やメディア有無の判定）
 * - init():      読み出し前の初期化。0 で成功、負値で失敗
 * - read():      生セクター読み出し。offset はバイト単位、len はセクター境界
 */
typedef struct disc_source {
	const char *name;        /* 表示用の正式名 */
	const char *short_name;  /* UI ボタン等に使う短い名前 */
	bool        is_physical; /* 物理ディスクを読むソースか（内蔵 DI / 外部ドライブ） */

	bool (*available)(void);
	int  (*init)(void);
	int  (*read)(void *dst, u32 len, u64 offset);

	/* init() 後に確定する値（未初期化時は 0） */
	u32 sector_size;   /* セクター長（通常 2048、GC raw は 2064 もあり得る） */
	u32 sector_count;  /* 総セクター数（不明なら 0） */
} disc_source_t;

/* 登録済みソースの数 */
int disc_source_count(void);

/* index 番目のソースを返す（範囲外は NULL） */
const disc_source_t *disc_source_get(int index);

/* 既定のソース（内蔵 DI）を返す */
const disc_source_t *disc_source_default(void);

/* 内蔵 DI のソース（gc_dvd.c をラップ） */
extern disc_source_t disc_source_gc_di;
/* 外部 USB 光学ドライブ（USB-EXI アダプタ経由）のソース */
extern disc_source_t disc_source_usb_scsi;
/* IDE-EXI 接続ドライブ（将来実装枠。現状は未対応） */
extern disc_source_t disc_source_ide_exi;

#endif /* DISC_SOURCE_H */
