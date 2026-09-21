/**
 * CleanRip - disc_source.c
 * Copyright (C) 2010-2026 emu_kidid
 *
 * ディスク読み出し元の抽象化レイヤー。
 * 内蔵 DVD ドライブ (DI) と外部 USB 光学ドライブ（USB-EXI アダプタ）を
 * 同じ API で扱う。外部ドライブ側の実装は disc_source_usb_scsi.c を参照。
 *
 * このファイルは GPLv2 で配布される（LICENSE を参照）。
 **/

#include <gccore.h>
#include <stdbool.h>
#include "disc_source.h"
#include "gc_dvd.h"
#include "usb_exi.h"

/*--------------------------------------------------------------
 * 内蔵 DVD ドライブ (DI) ソース
 *--------------------------------------------------------------*/
static bool gc_di_available(void) {
	/* 内蔵 DI はハードウェアとして常に存在する。
	 * メディアの有無は init 時に判定する。 */
	return true;
}

static int gc_di_init(void) {
	return init_dvd();
}

static int gc_di_read(void *dst, u32 len, u64 offset) {
	return DVD_LowRead64(dst, len, offset);
}

disc_source_t disc_source_gc_di = {
	.name        = "内蔵 DVD ドライブ",
	.short_name  = "DVD",
	.is_physical = true,
	.available   = gc_di_available,
	.init        = gc_di_init,
	.read        = gc_di_read,
	.sector_size = 2048,
	.sector_count = 0,
};

/*--------------------------------------------------------------
 * 外部 USB 光学ドライブ（USB-EXI アダプタ）ソース
 *   実体は disc_source_usb_scsi.c（Wii では常に未対応）
 *--------------------------------------------------------------*/
static int usb_scsi_init(void) {
	int ret = usb_exi_disc_init();
	if (ret == 0) {
		disc_source_usb_scsi.sector_size  = usb_exi_disc_sector_size();
		disc_source_usb_scsi.sector_count = usb_exi_disc_sector_count();
	}
	return ret;
}

static int usb_scsi_read(void *dst, u32 len, u64 offset) {
	return usb_exi_disc_read(dst, len, offset);
}

disc_source_t disc_source_usb_scsi = {
	.name        = "外付け USB ドライブ",
	.short_name  = "USB",
	.is_physical = true,
	.available   = usb_exi_present,
	.init        = usb_scsi_init,
	.read        = usb_scsi_read,
	.sector_size = 0,
	.sector_count = 0,
};

/*--------------------------------------------------------------
 * IDE-EXI 接続ドライブ（将来実装枠）
 *   ハードウェアは未検証のため常に未対応。
 *--------------------------------------------------------------*/
static bool ide_exi_available(void) {
	return false;
}

static int ide_exi_init(void) {
	return -1;
}

static int ide_exi_read(void *dst, u32 len, u64 offset) {
	(void)dst; (void)len; (void)offset;
	return -1;
}

disc_source_t disc_source_ide_exi = {
	.name        = "IDE-EXI ドライブ",
	.short_name  = "IDE",
	.is_physical = true,
	.available   = ide_exi_available,
	.init        = ide_exi_init,
	.read        = ide_exi_read,
	.sector_size = 2048,
	.sector_count = 0,
};

/*--------------------------------------------------------------
 * 登録テーブル
 *--------------------------------------------------------------*/
static disc_source_t *disc_sources[] = {
	&disc_source_gc_di,
	&disc_source_usb_scsi,
	&disc_source_ide_exi,
};

int disc_source_count(void) {
	return (int)(sizeof(disc_sources) / sizeof(disc_sources[0]));
}

const disc_source_t *disc_source_get(int index) {
	if (index < 0 || index >= disc_source_count())
		return NULL;
	return disc_sources[index];
}

const disc_source_t *disc_source_default(void) {
	return &disc_source_gc_di;
}
