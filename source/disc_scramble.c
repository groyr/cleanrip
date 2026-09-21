/**
 * CleanRip - disc_scramble.c
 * Copyright (C) 2010-2026 emu_kidid
 *
 * GameCube ディスクのスクランブル解除と EDC 検証。
 * friidump の unscrambler（Arep 版）と ecma-267 を GC 向けに移植したもの。
 * 原典: Arep による unscrambler 0.4（Victor Muñoz, GPLv2+）を経由。
 *
 * このファイルは GPLv2 で配布される（LICENSE を参照）。
 **/

#include <string.h>
#include "disc_scramble.h"
#include "main.h"   /* print_gecko */

/*--------------------------------------------------------------
 * EDC（誤り検出符号）テーブルと計算
 *--------------------------------------------------------------*/
static const u32 edc_table[256] = {
	0x00000000, 0x80000011, 0x80000033, 0x00000022, 0x80000077, 0x00000066, 0x00000044, 0x80000055,
	0x800000FF, 0x000000EE, 0x000000CC, 0x800000DD, 0x00000088, 0x80000099, 0x800000BB, 0x000000AA,
	0x800001EF, 0x000001FE, 0x000001DC, 0x800001CD, 0x00000198, 0x80000189, 0x800001AB, 0x000001BA,
	0x00000110, 0x80000101, 0x80000123, 0x00000132, 0x80000167, 0x00000176, 0x00000154, 0x80000145,
	0x800003CF, 0x000003DE, 0x000003FC, 0x800003ED, 0x000003B8, 0x800003A9, 0x8000038B, 0x0000039A,
	0x00000330, 0x80000321, 0x80000303, 0x00000312, 0x80000347, 0x00000356, 0x00000374, 0x80000365,
	0x00000220, 0x80000231, 0x80000213, 0x00000202, 0x80000257, 0x00000246, 0x00000264, 0x80000275,
	0x800002DF, 0x000002CE, 0x000002EC, 0x800002FD, 0x000002A8, 0x800002B9, 0x8000029B, 0x0000028A,
	0x8000078F, 0x0000079E, 0x000007BC, 0x800007AD, 0x000007F8, 0x800007E9, 0x800007CB, 0x000007DA,
	0x00000770, 0x80000761, 0x80000743, 0x00000752, 0x80000707, 0x00000716, 0x00000734, 0x80000725,
	0x00000660, 0x80000671, 0x80000653, 0x00000642, 0x80000617, 0x00000606, 0x00000624, 0x80000635,
	0x8000069F, 0x0000068E, 0x000006AC, 0x800006BD, 0x000006E8, 0x800006F9, 0x800006DB, 0x000006CA,
	0x00000440, 0x80000451, 0x80000473, 0x00000462, 0x80000437, 0x00000426, 0x00000404, 0x80000415,
	0x800004BF, 0x000004AE, 0x0000048C, 0x8000049D, 0x000004C8, 0x800004D9, 0x800004FB, 0x000004EA,
	0x800005AF, 0x000005BE, 0x0000059C, 0x8000058D, 0x000005D8, 0x800005C9, 0x800005EB, 0x000005FA,
	0x00000550, 0x80000541, 0x80000563, 0x00000572, 0x80000527, 0x00000536, 0x00000514, 0x80000505,
	0x80000F0F, 0x00000F1E, 0x00000F3C, 0x80000F2D, 0x00000F78, 0x80000F69, 0x80000F4B, 0x00000F5A,
	0x00000FF0, 0x80000FE1, 0x80000FC3, 0x00000FD2, 0x80000F87, 0x00000F96, 0x00000FB4, 0x80000FA5,
	0x00000EE0, 0x80000EF1, 0x80000ED3, 0x00000EC2, 0x80000E97, 0x00000E86, 0x00000EA4, 0x80000EB5,
	0x80000E1F, 0x00000E0E, 0x00000E2C, 0x80000E3D, 0x00000E68, 0x80000E79, 0x80000E5B, 0x00000E4A,
	0x00000CC0, 0x80000CD1, 0x80000CF3, 0x00000CE2, 0x80000CB7, 0x00000CA6, 0x00000C84, 0x80000C95,
	0x80000C3F, 0x00000C2E, 0x00000C0C, 0x80000C1D, 0x00000C48, 0x80000C59, 0x80000C7B, 0x00000C6A,
	0x80000D2F, 0x00000D3E, 0x00000D1C, 0x80000D0D, 0x00000D58, 0x80000D49, 0x80000D6B, 0x00000D7A,
	0x00000DD0, 0x80000DC1, 0x80000DE3, 0x00000DF2, 0x80000DA7, 0x00000DB6, 0x00000D94, 0x80000D85,
	0x00000880, 0x80000891, 0x800008B3, 0x000008A2, 0x800008F7, 0x000008E6, 0x000008C4, 0x800008D5,
	0x8000087F, 0x0000086E, 0x0000084C, 0x8000085D, 0x00000808, 0x80000819, 0x8000083B, 0x0000082A,
	0x8000096F, 0x0000097E, 0x0000095C, 0x8000094D, 0x00000918, 0x80000909, 0x8000092B, 0x0000093A,
	0x00000990, 0x80000981, 0x800009A3, 0x000009B2, 0x800009E7, 0x000009F6, 0x000009D4, 0x800009C5,
	0x80000B4F, 0x00000B5E, 0x00000B7C, 0x80000B6D, 0x00000B38, 0x80000B29, 0x80000B0B, 0x00000B1A,
	0x00000BB0, 0x80000BA1, 0x80000B83, 0x00000B92, 0x80000BC7, 0x00000BD6, 0x00000BF4, 0x80000BE5,
	0x00000AA0, 0x80000AB1, 0x80000A93, 0x00000A82, 0x80000AD7, 0x00000AC6, 0x00000AE4, 0x80000AF5,
	0x80000A5F, 0x00000A4E, 0x00000A6C, 0x80000A7D, 0x00000A28, 0x80000A39, 0x80000A1B, 0x00000A0A
};

u32 disc_edc_calc(u32 edc, const u8 *ptr, u32 len) {
	while (len--)
		edc = edc_table[((edc >> 24) ^ *ptr++) & 0xFF] ^ (edc << 8);
	return edc;
}

/*--------------------------------------------------------------
 * LFSR（DVD スクランブル用）
 *--------------------------------------------------------------*/
static u16 lfsr;

static void lfsr_init(u16 seed) {
	lfsr = seed;
}

static int lfsr_tick(void) {
	int ret = lfsr >> 14;
	int n = ret ^ ((lfsr >> 10) & 1);
	lfsr = (u16)(((lfsr << 1) | n) & 0x7FFF);
	return ret;
}

static u8 lfsr_byte(void) {
	u8 ret = 0;
	int i;
	for (i = 0; i < 8; i++)
		ret = (u8)((ret << 1) | lfsr_tick());
	return ret;
}

/*--------------------------------------------------------------
 * シードキャッシュ
 *   同一シードは 16 セクタ（=1 ブロック）連続で使われる。
 *   ブロック位置 (block mod 16) ごとにシードを保持する。
 *--------------------------------------------------------------*/
#define MAX_SEEDS 4

typedef struct {
	int seed;
	u8  streamcipher[DISC_SECTOR_SIZE];
} t_seed;

static t_seed seed_cache[(MAX_SEEDS + 1) * 16];

void disc_scramble_init(void) {
	int i, j;
	for (i = 0; i < 16; i++) {
		for (j = 0; j < MAX_SEEDS; j++)
			seed_cache[i * MAX_SEEDS + j].seed = -1;
		/* 各位置の末尾を終端マーカーにする（原典と同じ挙動） */
		seed_cache[i * MAX_SEEDS + j].seed = -2;
	}
}

/* シードのストリーム暗号を計算してキャッシュに追加 */
static t_seed *add_seed(t_seed *seeds, u16 seed) {
	int i;

	if (seeds->seed == -2)
		return NULL;

	seeds->seed = seed;
	lfsr_init(seed);
	for (i = 0; i < DISC_SECTOR_SIZE; i++)
		seeds->streamcipher[i] = lfsr_byte();

	return seeds;
}

/* 生フレーム先頭セクタの EDC からシードを検証する */
static bool test_seed(const u8 *buf, int seed) {
	u8 tmp[DISC_RAW_SECTOR_SIZE];
	u32 edc_calculated, edc_correct;
	int i;

	memcpy(tmp, buf, DISC_RAW_SECTOR_SIZE);

	lfsr_init((u16)seed);
	for (i = 12; i < DISC_EDC_LENGTH; i++)
		tmp[i] ^= lfsr_byte();

	edc_calculated = disc_edc_calc(0x00000000, tmp, DISC_EDC_LENGTH);
	edc_correct = ((u32)tmp[DISC_EDC_LENGTH] << 24) |
	              ((u32)tmp[DISC_EDC_LENGTH + 1] << 16) |
	              ((u32)tmp[DISC_EDC_LENGTH + 2] << 8) |
	              (u32)tmp[DISC_EDC_LENGTH + 3];

	return edc_calculated == edc_correct;
}

/* キャッシュ済みシードで 16 セクタを解除する（EDC 検証つき） */
static bool unscramble_frame(t_seed *seed, u8 *in, u8 *out) {
	int j, i;
	bool ok = true;
	u8 tmp[DISC_RAW_SECTOR_SIZE];

	for (j = 0; j < DISC_SECTORS_PER_BLOCK; j++) {
		u8 *bin  = in  + DISC_RAW_SECTOR_SIZE * j;
		u8 *bout = out + DISC_SECTOR_SIZE * j;
		u32 edc_calculated, edc_correct;

		memcpy(tmp, bin, DISC_RAW_SECTOR_SIZE);

		/* スクランブル解除はバイト 12 から 2048 バイト分の XOR */
		for (i = 0; i < DISC_SECTOR_SIZE; i++)
			tmp[12 + i] ^= seed->streamcipher[i];

		/* Nintendo 方式: CPR_MAI を含めた 2048 バイトをコピー */
		memcpy(bout, tmp + 6, DISC_SECTOR_SIZE);
		memcpy(&bin[2054], &tmp[2054], 6);

		edc_calculated = disc_edc_calc(0x00000000, tmp, DISC_EDC_LENGTH);
		edc_correct = ((u32)tmp[DISC_EDC_LENGTH] << 24) |
		              ((u32)tmp[DISC_EDC_LENGTH + 1] << 16) |
		              ((u32)tmp[DISC_EDC_LENGTH + 2] << 8) |
		              (u32)tmp[DISC_EDC_LENGTH + 3];

		if (edc_calculated != edc_correct)
			ok = false;
	}
	return ok;
}

bool disc_unscramble_block(u32 sector_no, u8 *inbuf, u8 *outbuf) {
	t_seed *seeds = &seed_cache[((sector_no / 16) & 0x0F) * MAX_SEEDS];
	t_seed *current = NULL;
	int j;

	/* キャッシュ済みシードを試す */
	while (!current && seeds->seed >= 0) {
		if (test_seed(inbuf, seeds->seed))
			current = seeds;
		else
			seeds++;
	}

	/* 未キャッシュならブルートフォースで探索 */
	if (!current) {
		for (j = 0; !current && j < 0x7FFF; j++) {
			if (test_seed(inbuf, j))
				current = add_seed(seeds, (u16)j);
		}
		if (!current) {
			print_gecko("シードが見つかりません (block=%u)\r\n", (unsigned)(sector_no / 16));
			return false;
		}
	}

	return unscramble_frame(current, inbuf, outbuf);
}
