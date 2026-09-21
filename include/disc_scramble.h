/**
 * CleanRip - disc_scramble.h
 * Copyright (C) 2010-2026 emu_kidid
 *
 * GameCube ディスクのスクランブル解除と EDC 検証。
 * friidump（GPLv2, Arep / xt5）の unscrambler を GC 向けに移植したもの。
 *
 * このファイルは GPLv2 で配布される（LICENSE を参照）。
 **/

#ifndef DISC_SCRAMBLE_H
#define DISC_SCRAMBLE_H

#include <gccore.h>
#include <stdbool.h>

/* ディスクのジオメトリ */
#define DISC_RAW_SECTOR_SIZE    2064                               /* スクランブル済み 1 セクタ */
#define DISC_SECTOR_SIZE        2048                               /* 解除後 1 セクタ */
#define DISC_SECTORS_PER_BLOCK  16
#define DISC_RAW_BLOCK_SIZE     (DISC_RAW_SECTOR_SIZE * DISC_SECTORS_PER_BLOCK) /* 33024 */
#define DISC_BLOCK_SIZE         (DISC_SECTOR_SIZE * DISC_SECTORS_PER_BLOCK)     /* 32768 */

/* 生フレームの EDC 位置（フレーム末尾 4 バイト） */
#define DISC_EDC_LENGTH         (DISC_RAW_SECTOR_SIZE - 4)         /* 2060 */

/* シードキャッシュの初期化（アプリ起動時に 1 回） */
void disc_scramble_init(void);

/* EDC 計算 */
u32 disc_edc_calc(u32 edc, const u8 *ptr, u32 len);

/*
 * 16 セクタの生フレーム（DISC_RAW_BLOCK_SIZE）を 2048B×16 に解除する。
 * EDC 検証に成功したら true。inbuf は Nintendo 方式の際に
 * 末尾 6 バイトが書き換わるため非 const（呼び出し側で raw を退避すること）。
 */
bool disc_unscramble_block(u32 sector_no, u8 *inbuf, u8 *outbuf);

#endif /* DISC_SCRAMBLE_H */
