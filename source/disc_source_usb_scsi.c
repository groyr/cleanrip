/**
 * CleanRip - disc_source_usb_scsi.c
 * Copyright (C) 2010-2026 emu_kidid
 *
 * 自作 USB-EXI アダプタ（RP2040）経由で外付け USB 光学ドライブを読む
 * GC 側クライアント実装。EXI プロトコルの詳細は docs/USB-EXI-ADAPTER.md。
 *
 * 読み出し方式（外付けドライブ = GCC-4240N 系 MN103S）:
 *   - 通常の READ(10) は汎用データ DVD 用（ブリングアップ用）
 *   - GC ディスクは raw 方式（friidump method12 相当）で読む:
 *       1) READ(12) streaming を 2 回（1 回目は捨てる）→ host データ
 *       2) E7 メモリダンプで生フレームの先頭/末尾を取得
 *       3) drive_cipher12[位相] で raw[12:2060] を復元
 *       4) 16 セクタまとめてスクランブル解除 + EDC 検証
 *     （補正テーブルは代表 16 ブロック 16..31 から校正する）
 *
 * 接続: Slot A (EXI ch0 dev0) を既定とする。Slot B は ch1 dev0。
 * Wii では未対応スタブになる。
 *
 * このファイルは GPLv2 で配布される（LICENSE を参照）。
 **/

#include <gccore.h>
#include <stdbool.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include "usb_exi.h"

#if defined(HW_DOL)

#include <ogc/exi.h>
#include <malloc.h>
#include "disc_scramble.h"
#include "main.h"   /* print_gecko */

/*----------------------------------------------------------------
 * 内部状態
 *----------------------------------------------------------------*/
static int  s_channel = USB_EXI_DEFAULT_CHANNEL;
static int  s_device  = USB_EXI_DEFAULT_DEVICE;
static int  s_speed   = USB_EXI_DEFAULT_SPEED;
static bool s_probed  = false;

static u32  s_sector_size  = 0;
static u32  s_sector_count = 0;

/* E7 のメモリベース（ドライブ依存。MN103S 系 GCC-4160N/4240N は 0xA13000） */
static u32  s_mem_base = 0xA13000;

/* GC ディスクを raw 方式で読むか */
static bool s_raw_mode = false;
static bool s_keep_raw = false;

/* 補正テーブル（16 位相 × 2048B）と校正済みフラグ */
static u8 __attribute__((aligned(32))) s_cipher12[16][DISC_SECTOR_SIZE];
static bool s_calibrated = false;

/* ブロックキャッシュ（1 ブロック = 32768B の解除済みデータ） */
static u8  __attribute__((aligned(32))) s_cache_iso[DISC_BLOCK_SIZE];
static u32 s_cache_blk = 0xFFFFFFFF;
static bool s_cache_valid = false;

/* 作業バッファ */
static u8 __attribute__((aligned(32))) s_rd[DISC_BLOCK_SIZE];        /* host データ */
static u8 __attribute__((aligned(32))) s_rawbuf[DISC_RAW_BLOCK_SIZE];/* 生フレーム */
static u8 __attribute__((aligned(32))) s_head[12];
static u8 __attribute__((aligned(32))) s_tail[10];

static char s_errbuf[128];

static void set_err(const char *msg) {
	snprintf(s_errbuf, sizeof(s_errbuf), "%s", msg);
}

const char *usb_exi_last_error(void) {
	return s_errbuf[0] ? s_errbuf : "エラーなし";
}

void usb_exi_configure(int channel, int device, int speed) {
	s_channel = channel;
	s_device  = device;
	s_speed   = speed;
	s_probed  = false;
}

void usb_exi_set_mem_base(u32 base) {
	s_mem_base = base;
}

u32 usb_exi_get_mem_base(void) {
	return s_mem_base;
}

bool usb_exi_is_raw_mode(void) {
	return s_raw_mode;
}

void usb_exi_set_keep_raw(bool keep) {
	s_keep_raw = keep;
}

/*----------------------------------------------------------------
 * ビッグエンディアン変換（EXI は MSB ファースト）
 *----------------------------------------------------------------*/
static void put_be32(u8 *p, u32 v) {
	p[0] = (u8)(v >> 24);
	p[1] = (u8)(v >> 16);
	p[2] = (u8)(v >> 8);
	p[3] = (u8)(v);
}

static u32 get_be32(const u8 *p) {
	return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | (u32)p[3];
}

/*----------------------------------------------------------------
 * EXI 低レベル
 *----------------------------------------------------------------*/
static bool exi_begin(void) {
	if (!s_probed) {
		if (EXI_ProbeEx(s_channel) <= 0) {
			set_err("EXI デバイスを検出できません");
			return false;
		}
		s_probed = true;
	}
	if (EXI_Select(s_channel, s_device, s_speed) <= 0) {
		set_err("EXI セレクトに失敗しました");
		return false;
	}
	return true;
}

static void exi_end(void) {
	EXI_Deselect(s_channel);
}

static bool exi_write(const void *buf, u32 len) {
	if (EXI_ImmEx(s_channel, (void *)buf, len, EXI_WRITE) <= 0)
		return false;
	EXI_Sync(s_channel);
	return true;
}

static bool exi_read(void *buf, u32 len) {
	if (EXI_ImmEx(s_channel, buf, len, EXI_READ) <= 0)
		return false;
	EXI_Sync(s_channel);
	return true;
}

static int exi_send_cmd(u8 op, u32 arg) {
	u8 w[4];
	put_be32(w, ((u32)op << 24) | (arg & 0xFFFFFF));
	if (!exi_begin())
		return -1;
	bool ok = exi_write(w, sizeof(w));
	exi_end();
	return ok ? 0 : -1;
}

static u32 usb_exi_status(void) {
	u8 w[4], r[4];
	u32 st = 0;

	put_be32(w, ((u32)USB_EXI_CMD_STATUS << 24));
	if (!exi_begin())
		return 0;
	if (exi_write(w, sizeof(w)) && exi_read(r, sizeof(r)))
		st = get_be32(r);
	exi_end();
	return st;
}

/*----------------------------------------------------------------
 * 接続確認（PING）
 *----------------------------------------------------------------*/
bool usb_exi_present(void) {
	u8 w[4], r[4];
	u32 id;
	bool ok;

	put_be32(w, ((u32)USB_EXI_CMD_PING << 24));
	if (!exi_begin())
		return false;
	ok = exi_write(w, sizeof(w));
	if (ok)
		ok = exi_read(r, sizeof(r));
	exi_end();
	if (!ok)
		return false;

	id = get_be32(r);
	return ((id >> 24) & 0xFF) == USB_EXI_MAGIC_0 &&
	       ((id >> 16) & 0xFF) == USB_EXI_MAGIC_1;
}

/*----------------------------------------------------------------
 * CDB 送信 + データ相
 *----------------------------------------------------------------*/
int usb_exi_send_cdb_ex(const u8 *cdb, u8 cdb_len, u32 data_len, int direction,
                        void *data, u32 *transferred) {
	u8 stage[16];
	u32 st;
	int i;
	bool ok;

	if (cdb_len == 0 || cdb_len > 16) {
		set_err("CDB 長が不正です");
		return -1;
	}

	if (exi_send_cmd(USB_EXI_CMD_SET_LEN, data_len & 0xFFFFFF) < 0) {
		set_err("SET_LEN に失敗しました");
		return -1;
	}
	if (exi_send_cmd(USB_EXI_CMD_SEND_CDB,
	                 ((u32)cdb_len << 8) | (u32)(direction & 0xFF)) < 0) {
		set_err("SEND_CDB に失敗しました");
		return -1;
	}

	/* CDB は 16 バイト固定で転送（余りは 0） */
	memset(stage, 0, sizeof(stage));
	memcpy(stage, cdb, cdb_len);
	if (!exi_begin())
		return -1;
	ok = exi_write(stage, 16);
	exi_end();
	if (!ok) {
		set_err("CDB の転送に失敗しました");
		return -1;
	}

	/* 完了待ち（STATUS をポーリング） */
	st = 0;
	for (i = 0; i < 5000; i++) {
		st = usb_exi_status();
		if (st & (USB_EXI_ST_CDB_OK | USB_EXI_ST_CDB_FAIL))
			break;
		usleep(1000);
	}
	if (!(st & USB_EXI_ST_CDB_OK)) {
		set_err("デバイスが CDB を拒否しました");
		return -1;
	}

	if (data_len != 0 && data != NULL) {
		if (direction == USB_EXI_DIR_IN) {
			if (exi_send_cmd(USB_EXI_CMD_READ_DATA, 0) < 0) {
				set_err("READ_DATA に失敗しました");
				return -1;
			}
			if (!exi_begin())
				return -1;
			ok = exi_read(data, data_len);
			exi_end();
			if (!ok) {
				set_err("データ読み出しに失敗しました");
				return -1;
			}
		} else {
			if (exi_send_cmd(USB_EXI_CMD_WRITE_DATA, 0) < 0) {
				set_err("WRITE_DATA に失敗しました");
				return -1;
			}
			if (!exi_begin())
				return -1;
			ok = exi_write(data, data_len);
			exi_end();
			if (!ok) {
				set_err("データ書き込みに失敗しました");
				return -1;
			}
		}
	}

	if (transferred)
		*transferred = data_len;
	return 0;
}

int usb_exi_send_cdb(const u8 cdb[16], u32 data_len, int direction,
                     void *data, u32 *transferred) {
	return usb_exi_send_cdb_ex(cdb, 16, data_len, direction, data, transferred);
}

/*----------------------------------------------------------------
 * ドライブ操作ヘルパ（CDB 生成）
 *----------------------------------------------------------------*/
/* READ(12) streaming: 16 セクタ（32768B）を host データとして読む */
static int dvd_read_streaming_block(u32 lba, u8 *buf, u32 len) {
	u8 cdb[12];
	memset(cdb, 0, sizeof(cdb));
	cdb[0]  = 0xA8; /* READ(12) */
	cdb[2]  = (u8)(lba >> 24);
	cdb[3]  = (u8)(lba >> 16);
	cdb[4]  = (u8)(lba >> 8);
	cdb[5]  = (u8)(lba);
	cdb[9]  = 0x10; /* 16 ブロック */
	cdb[10] = 0x80; /* STREAMING ビット */
	return usb_exi_send_cdb_ex(cdb, 12, len, USB_EXI_DIR_IN, buf, NULL);
}

/* E7: ドライブ内メモリをダンプする（s_mem_base + offset, len バイト） */
static int hitachi_memdump(u32 offset, u32 len, u8 *buf) {
	u8 cdb[12];
	u32 addr = s_mem_base + offset;

	if (len == 0 || len > 65535)
		return -1;

	memset(cdb, 0, sizeof(cdb));
	cdb[0]  = 0xE7;
	cdb[1]  = 'H';
	cdb[2]  = 'I';
	cdb[3]  = 'T';
	cdb[4]  = 0x01;
	cdb[6]  = (u8)(addr >> 24);
	cdb[7]  = (u8)(addr >> 16);
	cdb[8]  = (u8)(addr >> 8);
	cdb[9]  = (u8)(addr);
	cdb[10] = (u8)(len >> 8);
	cdb[11] = (u8)(len);

	return usb_exi_send_cdb_ex(cdb, 12, len, USB_EXI_DIR_IN, buf, NULL);
}

/*----------------------------------------------------------------
 * raw ブロック読み出し（method12 の生フレーム取得部）
 *   READ(12)×2 + E7 全ブロック → セクタ番号検証 → 解除（EDC）
 *----------------------------------------------------------------*/
static bool raw_read_whole_block(u32 blk) {
	static u8 rawcopy[DISC_RAW_BLOCK_SIZE] __attribute__((aligned(32)));
	u32 lba = blk * DISC_SECTORS_PER_BLOCK;
	u32 k;

	/* trigger READ（1 回目は吸収される。データは捨てる） */
	if (dvd_read_streaming_block(lba, s_rd, DISC_BLOCK_SIZE) < 0)
		return false;
	if (dvd_read_streaming_block(lba, s_rd, DISC_BLOCK_SIZE) < 0)
		return false;
	if (hitachi_memdump(0, DISC_RAW_BLOCK_SIZE, s_rawbuf) < 0)
		return false;

	/* セクタ番号検証 */
	for (k = 0; k < DISC_SECTORS_PER_BLOCK; k++) {
		u32 sn = ((u32)s_rawbuf[k * DISC_RAW_SECTOR_SIZE + 1] << 16) |
		         ((u32)s_rawbuf[k * DISC_RAW_SECTOR_SIZE + 2] << 8) |
		         (u32)s_rawbuf[k * DISC_RAW_SECTOR_SIZE + 3];
		if (sn != lba + k + 0x30000)
			return false;
	}

	/* 解除は入力を書き換えるためコピーに対して行う */
	memcpy(rawcopy, s_rawbuf, DISC_RAW_BLOCK_SIZE);
	if (!disc_unscramble_block(lba, rawcopy, s_cache_iso))
		return false;

	s_cache_blk = blk;
	s_cache_valid = true;
	return true;
}

/*----------------------------------------------------------------
 * method12: 補正テーブルの校正（代表 16 ブロック 16..31）
 *----------------------------------------------------------------*/
static bool calibrate12(void) {
	int m, k, i;

	if (s_calibrated)
		return true;

	print_gecko("校正中（16 ブロック）...\r\n");
	for (m = 0; m < 16; m++) {
		u32 blk = 16 + m;
		if (!raw_read_whole_block(blk)) {
			print_gecko("校正失敗: block %u\r\n", blk);
			set_err("校正に失敗しました");
			return false;
		}
		for (k = 0; k < DISC_SECTORS_PER_BLOCK; k++)
			for (i = 0; i < DISC_SECTOR_SIZE; i++)
				s_cipher12[m][i] = s_rd[k * DISC_SECTOR_SIZE + i] ^
				                   s_rawbuf[k * DISC_RAW_SECTOR_SIZE + 12 + i];
	}
	s_calibrated = true;
	print_gecko("校正完了\r\n");
	return true;
}

/*----------------------------------------------------------------
 * method12 本体: 1 ブロックを解除済みデータにする
 *----------------------------------------------------------------*/
static bool method12_load_block(u32 blk) {
	u32 lba = blk * DISC_SECTORS_PER_BLOCK;
	int m = (int)(blk % 16);
	int retry, k, i;

	if (blk == 0)
		return raw_read_whole_block(0);

	if (!calibrate12())
		return false;

	for (retry = 0; retry < 3; retry++) {
		bool ok = true;

		if (dvd_read_streaming_block(lba, s_rd, DISC_BLOCK_SIZE) < 0)
			continue;
		if (dvd_read_streaming_block(lba, s_rd, DISC_BLOCK_SIZE) < 0)
			continue;

		memset(s_rawbuf, 0, DISC_RAW_BLOCK_SIZE);
		for (k = 0; k < DISC_SECTORS_PER_BLOCK; k++) {
			u32 sn;

			if (hitachi_memdump(k * DISC_RAW_SECTOR_SIZE, 12, s_head) < 0) { ok = false; break; }
			sn = ((u32)s_head[1] << 16) | ((u32)s_head[2] << 8) | s_head[3];
			if (sn != lba + k + 0x30000) { ok = false; break; }

			if (hitachi_memdump(k * DISC_RAW_SECTOR_SIZE + 2054, 10, s_tail) < 0) { ok = false; break; }

			memcpy(s_rawbuf + k * DISC_RAW_SECTOR_SIZE, s_head, 12);
			for (i = 0; i < DISC_SECTOR_SIZE; i++)
				s_rawbuf[k * DISC_RAW_SECTOR_SIZE + 12 + i] =
					s_rd[k * DISC_SECTOR_SIZE + i] ^ s_cipher12[m][i];
			memcpy(s_rawbuf + k * DISC_RAW_SECTOR_SIZE + 2054, s_tail, 10);
		}
		if (!ok) {
			print_gecko("再試行 %d: block %u（E7/セクタ）\r\n", retry + 1, blk);
			continue;
		}

		if (!disc_unscramble_block(lba, s_rawbuf, s_cache_iso)) {
			print_gecko("再試行 %d: block %u（EDC）\r\n", retry + 1, blk);
			continue;
		}

		s_cache_blk = blk;
		s_cache_valid = true;
		return true;
	}

	print_gecko("raw 全ブロック読みにフォールバック: block %u\r\n", blk);
	return raw_read_whole_block(blk);
}

/*----------------------------------------------------------------
 * 汎用 READ(10)（通常データ DVD 用）
 *----------------------------------------------------------------*/
static int read_generic(void *dst, u32 len, u64 offset) {
	u32 done = 0;

	while (done < len) {
		u32 n = len - done;
		if (n > USB_EXI_MAX_DATA)
			n = USB_EXI_MAX_DATA;
		n -= (n % s_sector_size);
		if (n == 0)
			break;

		u32 lba    = (u32)((offset + done) / s_sector_size);
		u32 blocks = n / s_sector_size;
		u8  cdb[10];

		memset(cdb, 0, sizeof(cdb));
		cdb[0] = 0x28; /* READ(10) */
		cdb[2] = (u8)(lba >> 24);
		cdb[3] = (u8)(lba >> 16);
		cdb[4] = (u8)(lba >> 8);
		cdb[5] = (u8)(lba);
		cdb[7] = (u8)(blocks >> 8);
		cdb[8] = (u8)(blocks);

		if (usb_exi_send_cdb_ex(cdb, 10, n, USB_EXI_DIR_IN,
		                        (u8 *)dst + done, NULL) < 0)
			return -1;
		done += n;
	}
	return 0;
}

/*----------------------------------------------------------------
 * ディスク初期化
 *----------------------------------------------------------------*/
static bool detect_raw_mode(void) {
	u8 hdr[DISC_SECTOR_SIZE];

	if (!raw_read_whole_block(0))
		return false;

	memcpy(hdr, s_cache_iso, DISC_SECTOR_SIZE);
	if (get_be32(hdr + 0x1C) != NGC_MAGIC)   /* main.h の NGC_MAGIC */
		return false;

	/* GC ディスク。sector 0 はキャッシュ済み */
	s_raw_mode = true;
	print_gecko("GC ディスクを検出（raw モード）\r\n");
	return true;
}

int usb_exi_disc_init(void) {
	u8 w[4], r[8];
	u32 st;
	int i;

	s_sector_size  = 0;
	s_sector_count = 0;
	s_raw_mode     = false;
	s_calibrated   = false;
	s_cache_valid  = false;
	s_cache_blk    = 0xFFFFFFFF;

	disc_scramble_init();

	if (!usb_exi_present()) {
		set_err("USB-EXI アダプタが見つかりません");
		return -1;
	}

	for (i = 0; i < 5000; i++) {
		st = usb_exi_status();
		if (st & USB_EXI_ST_READY)
			break;
		usleep(1000);
	}
	if (!(st & USB_EXI_ST_READY)) {
		set_err("アダプタが READY になりません");
		return -1;
	}
	if (!(st & USB_EXI_ST_USB_MOUNTED)) {
		set_err("USB ドライブがマウントされていません");
		return -1;
	}

	put_be32(w, ((u32)USB_EXI_CMD_CAPACITY << 24));
	if (!exi_begin())
		return -1;
	bool ok = exi_write(w, sizeof(w));
	if (ok)
		ok = exi_read(r, sizeof(r));
	exi_end();
	if (!ok) {
		set_err("容量の取得に失敗しました");
		return -1;
	}

	s_sector_count = get_be32(r);
	s_sector_size  = get_be32(r + 4);
	if (s_sector_size == 0) {
		set_err("ブロック長が 0 です");
		return -1;
	}

	/* GC ディスクなら raw 方式に切り替える（失敗しても汎用として続行） */
	detect_raw_mode();

	return 0;
}

u32 usb_exi_disc_sector_size(void) {
	return s_sector_size ? s_sector_size : DISC_SECTOR_SIZE;
}

u32 usb_exi_disc_sector_count(void) {
	return s_sector_count;
}

/*----------------------------------------------------------------
 * 生セクター読み出し（CleanRip からは 2048B セクタとして見える）
 *----------------------------------------------------------------*/
int usb_exi_disc_read(void *dst, u32 len, u64 offset) {
	if (s_sector_size == 0) {
		set_err("ディスクが初期化されていません");
		return -1;
	}

	if (!s_raw_mode)
		return read_generic(dst, len, offset);

	/* raw モード: 32768B ブロック単位で解除して返す */
	u32 done = 0;
	u8 *out = (u8 *)dst;

	while (done < len) {
		u64 pos = offset + done;
		u32 blk = (u32)(pos / DISC_BLOCK_SIZE);
		u32 in  = (u32)(pos % DISC_BLOCK_SIZE);
		u32 n   = DISC_BLOCK_SIZE - in;

		if (n > len - done)
			n = len - done;

		if (!s_cache_valid || s_cache_blk != blk) {
			if (!method12_load_block(blk)) {
				set_err("raw 読み出しに失敗しました");
				return -1;
			}
		}
		memcpy(out + done, s_cache_iso + in, n);
		done += n;
	}
	return 0;
}

#else /* !HW_DOL -------------------------------------------------- */
/* Wii では未対応。リンク用のスタブ。 */

static char s_errbuf_wii[64] = "Wii では未対応です";

const char *usb_exi_last_error(void) { return s_errbuf_wii; }
void  usb_exi_configure(int c, int d, int s) { (void)c; (void)d; (void)s; }
void  usb_exi_set_mem_base(u32 b) { (void)b; }
u32   usb_exi_get_mem_base(void) { return 0; }
bool  usb_exi_is_raw_mode(void) { return false; }
void  usb_exi_set_keep_raw(bool k) { (void)k; }
bool  usb_exi_present(void) { return false; }
int   usb_exi_disc_init(void) { return -1; }
int   usb_exi_disc_read(void *dst, u32 len, u64 offset) {
	(void)dst; (void)len; (void)offset;
	return -1;
}
u32   usb_exi_disc_sector_size(void) { return 0; }
u32   usb_exi_disc_sector_count(void) { return 0; }
int   usb_exi_send_cdb(const u8 cdb[16], u32 dl, int dir, void *d, u32 *t) {
	(void)cdb; (void)dl; (void)dir; (void)d; (void)t;
	return -1;
}
int   usb_exi_send_cdb_ex(const u8 *cdb, u8 cl, u32 dl, int dir, void *d, u32 *t) {
	(void)cdb; (void)cl; (void)dl; (void)dir; (void)d; (void)t;
	return -1;
}

#endif /* HW_DOL */
