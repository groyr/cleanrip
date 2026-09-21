/*
 * usb-exi-adapter - main.c
 *
 * RP2040 製「USB → EXI」アダプタのファームウェア。
 *   - core0: TinyUSB ホスト（mass storage / BOT）を実行し、任意の SCSI CDB を送る
 *   - core1: EXI スレーブとして GC からのコマンドを解釈する
 *
 * 目的: GC 上の CleanRip から USB 光学ドライブ（GCC-4240N 等）を読み出す。
 *       vendor コマンド 0xE7 を含む任意 CDB を送れる点が既製アダプタとの違い。
 *
 * ライセンス: MIT
 */

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "pico/sync.h"
#include "hardware/sync.h"
#include "tusb.h"

#include "exi_slave.h"
#include "exi_proto.h"

/*----------------------------------------------------------------
 * 共有状態（core0: USB / core1: EXI）
 *----------------------------------------------------------------*/
static critical_section_t g_cs;

static volatile uint32_t g_status = ST_READY;

static volatile uint8_t  g_cdb[EXI_MAX_CDB];
static volatile uint8_t  g_cdb_len = 16;
static volatile uint8_t  g_dir = DIR_IN;
static volatile uint32_t g_len = 0;

static volatile uint8_t  g_data[EXI_MAX_DATA];

/* SEND_CDB を受けたら core0 に処理させる */
static volatile bool     g_cdb_request = false;

static uint8_t  g_dev_addr = 0;
static uint8_t  g_lun = 0;
static uint32_t g_block_count = 0;
static uint32_t g_block_size = 0;

/*----------------------------------------------------------------
 * ビッグエンディアン書き込み
 *----------------------------------------------------------------*/
static void put_be32(uint8_t *p, uint32_t v) {
	p[0] = (uint8_t)(v >> 24);
	p[1] = (uint8_t)(v >> 16);
	p[2] = (uint8_t)(v >> 8);
	p[3] = (uint8_t)(v);
}

static uint32_t get_be32(const uint8_t *p) {
	return ((uint32_t)p[0] << 24) | ((uint32_t)p[1] << 16) |
	       ((uint32_t)p[2] << 8) | (uint32_t)p[3];
}

/*----------------------------------------------------------------
 * core1: EXI スレーブ
 *----------------------------------------------------------------*/
enum exi_state {
	EXPECT_CMD = 0,
	EXPECT_CDB,
	EXPECT_DATA_OUT,
	OUTPUT_DATA,
};

static uint32_t s_out_len = 0;   /* OUTPUT_DATA で送る長さ */
static uint8_t  s_rxbuf[EXI_MAX_DATA]; /* 受信バッファ（data-out を含む） */

static void core1_main(void) {
	enum exi_state state = EXPECT_CMD;
	exi_slave_init();

	for (;;) {
		exi_xfer_t x;
		memset(&x, 0, sizeof(x));

		if (state == OUTPUT_DATA) {
			x.drive = true;
			x.tx = (const uint8_t *)g_data;
			x.txlen = (int)s_out_len;
		} else {
			x.drive = false;
			x.rx = s_rxbuf;
			x.rxmax = sizeof(s_rxbuf);
		}

		exi_slave_run_once(&x);

		switch (state) {
		case EXPECT_CMD: {
			if (x.rxlen < 4)
				break;
			uint32_t w = get_be32(s_rxbuf);
			uint8_t op = (uint8_t)(w >> 24);
			uint32_t arg = w & 0xFFFFFF;

			switch (op) {
			case CMD_PING:
				critical_section_enter_blocking(&g_cs);
				g_data[0] = EXI_MAGIC0;
				g_data[1] = EXI_MAGIC1;
				g_data[2] = EXI_PROTO_VERSION;
				g_data[3] = 0;
				s_out_len = 4;
				critical_section_exit(&g_cs);
				state = OUTPUT_DATA;
				break;

			case CMD_STATUS:
				critical_section_enter_blocking(&g_cs);
				put_be32((uint8_t *)g_data, g_status);
				s_out_len = 4;
				critical_section_exit(&g_cs);
				state = OUTPUT_DATA;
				break;

			case CMD_CAPACITY:
				critical_section_enter_blocking(&g_cs);
				put_be32((uint8_t *)g_data, g_block_count);
				put_be32((uint8_t *)g_data + 4, g_block_size);
				s_out_len = 8;
				critical_section_exit(&g_cs);
				state = OUTPUT_DATA;
				break;

			case CMD_USB_INFO:
				critical_section_enter_blocking(&g_cs);
				put_be32((uint8_t *)g_data, g_dev_addr);
				put_be32((uint8_t *)g_data + 4, g_lun);
				put_be32((uint8_t *)g_data + 8, g_block_size);
				put_be32((uint8_t *)g_data + 12, 0);
				s_out_len = 16;
				critical_section_exit(&g_cs);
				state = OUTPUT_DATA;
				break;

			case CMD_SET_LEN:
				critical_section_enter_blocking(&g_cs);
				g_len = arg;
				critical_section_exit(&g_cs);
				break;

			case CMD_SET_LBA_LO:
			case CMD_SET_LBA_HI:
				/* LBA は GC 側が CDB に埋め込むため未使用 */
				break;

			case CMD_SEND_CDB:
				critical_section_enter_blocking(&g_cs);
				g_dir = (uint8_t)(arg & 0xFF);
				g_cdb_len = (uint8_t)((arg >> 8) & 0xFF);
				if (g_cdb_len == 0 || g_cdb_len > EXI_MAX_CDB)
					g_cdb_len = EXI_MAX_CDB;
				g_status &= ~(ST_CDB_OK | ST_CDB_FAIL);
				critical_section_exit(&g_cs);
				state = EXPECT_CDB;
				break;

			case CMD_READ_DATA:
				/* SEND_CDB で既にステージ済みのデータを返す */
				critical_section_enter_blocking(&g_cs);
				s_out_len = g_len > EXI_MAX_DATA ? EXI_MAX_DATA : g_len;
				critical_section_exit(&g_cs);
				state = OUTPUT_DATA;
				break;

			case CMD_WRITE_DATA:
				state = EXPECT_DATA_OUT;
				break;

			default:
				break;
			}
			break;
		}

		case EXPECT_CDB: {
			if (x.rxlen >= EXI_MAX_CDB) {
				critical_section_enter_blocking(&g_cs);
				memcpy((void *)g_cdb, s_rxbuf, EXI_MAX_CDB);
				/* data-in はここで USB 転送を要求。data-out は
				 * WRITE_DATA 受信後に要求する。 */
				if (g_dir == DIR_IN)
					g_cdb_request = true;
				critical_section_exit(&g_cs);
			}
			state = EXPECT_CMD;
			break;
		}

		case EXPECT_DATA_OUT: {
			critical_section_enter_blocking(&g_cs);
			uint32_t n = g_len > EXI_MAX_DATA ? EXI_MAX_DATA : g_len;
			if (x.rxlen > 0 && (uint32_t)x.rxlen < n)
				n = (uint32_t)x.rxlen;
			memcpy((void *)g_data, s_rxbuf, n);
			g_len = n;
			g_cdb_request = true; /* data-out の CDB を実行 */
			critical_section_exit(&g_cs);
			state = EXPECT_CMD;
			break;
		}

		case OUTPUT_DATA:
			state = EXPECT_CMD;
			break;
		}
	}
}

/*----------------------------------------------------------------
 * core0: TinyUSB ホスト
 *----------------------------------------------------------------*/
static bool msc_complete_cb(uint8_t dev_addr, const tuh_msc_complete_data_t *cb) {
	(void)dev_addr;
	uint8_t sw = cb->csw ? cb->csw->status : 1;

	critical_section_enter_blocking(&g_cs);
	if (sw == 0)
		g_status |= ST_CDB_OK;
	else
		g_status |= ST_CDB_FAIL;
	g_status &= ~ST_BUSY;
	critical_section_exit(&g_cs);
	return true;
}

static void issue_pending_cdb(void) {
	static msc_cbw_t cbw;
	uint8_t cdb[16];
	uint8_t cdblen;
	uint8_t dir;
	uint32_t len;

	critical_section_enter_blocking(&g_cs);
	if (!g_cdb_request) {
		critical_section_exit(&g_cs);
		return;
	}
	memcpy(cdb, (const void *)g_cdb, 16);
	cdblen = g_cdb_len;
	dir = g_dir;
	len = g_len > EXI_MAX_DATA ? EXI_MAX_DATA : g_len;
	g_cdb_request = false;
	g_status |= ST_BUSY;
	critical_section_exit(&g_cs);

	if (g_dev_addr == 0)
		return;

	memset(&cbw, 0, sizeof(cbw));
	cbw.signature   = MSC_CBW_SIGNATURE;
	cbw.tag         = 0x54555342; /* "TUSB"（cbw_init と同じ） */
	cbw.total_bytes = len;
	cbw.dir         = (dir == DIR_IN) ? 0x80 : 0x00;
	cbw.lun         = g_lun;
	cbw.cmd_len     = cdblen;
	memcpy(cbw.command, cdb, cdblen);

	/* 任意 CDB を送る（0xE7 等の vendor コマンドを含む） */
	tuh_msc_scsi_command(g_dev_addr, &cbw, (void *)g_data, msc_complete_cb, 0);
	(void)len;
}

/*----------------------------------------------------------------
 * TinyUSB コールバック
 *----------------------------------------------------------------*/
void tuh_msc_mount_cb(uint8_t dev_addr) {
	critical_section_enter_blocking(&g_cs);
	g_dev_addr = dev_addr;
	g_lun = 0;
	g_block_count = tuh_msc_get_block_count(dev_addr, 0);
	g_block_size  = tuh_msc_get_block_size(dev_addr, 0);
	g_status |= ST_USB_ATTACHED | ST_USB_MOUNTED;
	if (g_block_count > 0)
		g_status |= ST_MEDIA_PRESENT;
	critical_section_exit(&g_cs);
}

void tuh_msc_umount_cb(uint8_t dev_addr) {
	critical_section_enter_blocking(&g_cs);
	if (g_dev_addr == dev_addr) {
		g_dev_addr = 0;
		g_block_count = 0;
		g_block_size = 0;
		g_status &= ~(ST_USB_ATTACHED | ST_USB_MOUNTED | ST_MEDIA_PRESENT);
	}
	critical_section_exit(&g_cs);
}

/*----------------------------------------------------------------
 * main
 *----------------------------------------------------------------*/
int main(void) {
	stdio_init_all();
	critical_section_init(&g_cs);

	multicore_launch_core1(core1_main);

	tusb_init();

	for (;;) {
		tuh_task();
		issue_pending_cdb();
	}
}
