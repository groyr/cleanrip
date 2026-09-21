/*
 * usb-exi-adapter - exi_slave.c
 *
 * EXI 1bit モードのスレーブ実装。
 *   既定は PIO 版（EXI_SLAVE_USE_PIO=1）。PIO が使えない/検証したい場合は
 *   0 にすると CPU ビットバン版に切り替わる。
 *
 * 重要（要ロジアナ検証）:
 *   データのサンプル／駆動エッジは実機で確認すること。
 *   EXI は MSB ファースト。ここでは
 *     - 受信: CLK 立ち上がりでサンプル
 *     - 送信: CLK 立ち下がりで駆動
 *   としている。逆だった場合は exi.pio の wait 極性を反転させる。
 *
 * ライセンス: MIT
 */

#include "exi_slave.h"

#include "pico/stdlib.h"
#include "hardware/gpio.h"

#ifndef EXI_SLAVE_USE_PIO
#define EXI_SLAVE_USE_PIO 1
#endif

bool exi_slave_cs_asserted(void) {
	return gpio_get(EXI_PIN_CS) == 0;
}

static inline void exi_data_input(void) {
	gpio_set_dir(EXI_PIN_DATA, GPIO_IN);
}

/* ================================================================
 * PIO 版
 * ================================================================ */
#if EXI_SLAVE_USE_PIO

#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "exi.pio.h"

/* exi.pio にリテラルで埋め込んだ CLK ピン番号（変更時は両方を更新） */
#define EXI_PIO_CLK_PIN 3

#if (EXI_PIN_CLK != EXI_PIO_CLK_PIN)
#error "exi.pio の CLK ピン定義(3)と exi_slave.h の EXI_PIN_CLK が一致していません"
#endif

#define EXI_PIO        pio0
#define EXI_PIO_SM_IN  0
#define EXI_PIO_SM_OUT 1

static uint s_off_in, s_off_out;

void exi_slave_init(void) {
	/* CS は CPU がポーリングする */
	gpio_init(EXI_PIN_CS);
	gpio_set_dir(EXI_PIN_CS, GPIO_IN);
	gpio_pull_up(EXI_PIN_CS);

	/* CLK / DATA は PIO に割り当てる */
	gpio_init(EXI_PIN_CLK);
	gpio_set_dir(EXI_PIN_CLK, GPIO_IN);
	gpio_pull_up(EXI_PIN_CLK);
	gpio_init(EXI_PIN_DATA);
	gpio_set_dir(EXI_PIN_DATA, GPIO_IN);
	gpio_pull_up(EXI_PIN_DATA);

	pio_gpio_init(EXI_PIO, EXI_PIN_CLK);
	pio_gpio_init(EXI_PIO, EXI_PIN_DATA);

	s_off_in  = pio_add_program(EXI_PIO, &exi_in_program);
	s_off_out = pio_add_program(EXI_PIO, &exi_out_program);

	pio_sm_claim(EXI_PIO, EXI_PIO_SM_IN);
	pio_sm_claim(EXI_PIO, EXI_PIO_SM_OUT);

	/* 受信 SM */
	pio_sm_config cin = exi_in_program_get_default_config(s_off_in);
	sm_config_set_in_pins(&cin, EXI_PIN_DATA);
	sm_config_set_set_pins(&cin, EXI_PIN_DATA, 1);
	sm_config_set_in_shift(&cin, false /* shift left */, true /* autopush */, 8);
	pio_sm_init(EXI_PIO, EXI_PIO_SM_IN, s_off_in, &cin);

	/* 送信 SM */
	pio_sm_config cout = exi_out_program_get_default_config(s_off_out);
	sm_config_set_out_pins(&cout, EXI_PIN_DATA, 1);
	sm_config_set_set_pins(&cout, EXI_PIN_DATA, 1);
	sm_config_set_out_shift(&cout, false /* shift left */, true /* autopull */, 8);
	pio_sm_init(EXI_PIO, EXI_PIO_SM_OUT, s_off_out, &cout);
}

static void exi_pio_read(exi_xfer_t *x) {
	/* DATA は入力のまま */
	pio_sm_set_pindirs_with_mask(EXI_PIO, EXI_PIO_SM_IN,
	                             0, 1u << EXI_PIN_DATA);
	pio_sm_clear_fifos(EXI_PIO, EXI_PIO_SM_IN);
	pio_sm_restart(EXI_PIO, EXI_PIO_SM_IN);
	pio_sm_set_enabled(EXI_PIO, EXI_PIO_SM_IN, true);

	while (exi_slave_cs_asserted()) {
		while (!pio_sm_is_rx_fifo_empty(EXI_PIO, EXI_PIO_SM_IN)) {
			uint32_t w = pio_sm_get(EXI_PIO, EXI_PIO_SM_IN);
			if (x->rxlen < x->rxmax)
				x->rx[x->rxlen++] = (uint8_t)(w & 0xFF);
		}
		tight_loop_contents();
	}

	pio_sm_set_enabled(EXI_PIO, EXI_PIO_SM_IN, false);
	/* 残っていれば回収 */
	while (!pio_sm_is_rx_fifo_empty(EXI_PIO, EXI_PIO_SM_IN)) {
		uint32_t w = pio_sm_get(EXI_PIO, EXI_PIO_SM_IN);
		if (x->rxlen < x->rxmax)
			x->rx[x->rxlen++] = (uint8_t)(w & 0xFF);
	}
}

static void exi_pio_write(exi_xfer_t *x) {
	int i;

	/* DATA を出力に切り替える */
	pio_sm_set_pindirs_with_mask(EXI_PIO, EXI_PIO_SM_OUT,
	                             1u << EXI_PIN_DATA, 1u << EXI_PIN_DATA);
	pio_sm_clear_fifos(EXI_PIO, EXI_PIO_SM_OUT);
	pio_sm_restart(EXI_PIO, EXI_PIO_SM_OUT);
	pio_sm_set_enabled(EXI_PIO, EXI_PIO_SM_OUT, true);

	/* MSB ファーストなので上位バイトに詰めて送る */
	for (i = 0; i < x->txlen; i++)
		pio_sm_put_blocking(EXI_PIO, EXI_PIO_SM_OUT,
		                    ((uint32_t)x->tx[i]) << 24);

	while (exi_slave_cs_asserted())
		tight_loop_contents();

	pio_sm_set_enabled(EXI_PIO, EXI_PIO_SM_OUT, false);
	/* DATA を入力に戻す */
	pio_sm_set_pindirs_with_mask(EXI_PIO, EXI_PIO_SM_OUT,
	                             0, 1u << EXI_PIN_DATA);
}

void exi_slave_run_once(exi_xfer_t *x) {
	x->rxlen = 0;
	while (!exi_slave_cs_asserted())
		tight_loop_contents();

	if (x->drive)
		exi_pio_write(x);
	else
		exi_pio_read(x);

	/* CS デアサートを待って終了 */
	while (exi_slave_cs_asserted())
		tight_loop_contents();
}

/* ================================================================
 * CPU ビットバン版（フォールバック / 検証用）
 * ================================================================ */
#else

#define EXI_SAMPLE_ON_RISING 1
#define EXI_DRIVE_ON_RISING  0

static void exi_wait_cs(void) {
	while (!exi_slave_cs_asserted())
		tight_loop_contents();
}

static bool exi_wait_edge(bool rising, bool *last) {
	for (;;) {
		if (!exi_slave_cs_asserted())
			return false;
		bool level = gpio_get(EXI_PIN_CLK) != 0;
		if (level != *last) {
			*last = level;
			if (level == rising)
				return true;
		}
		tight_loop_contents();
	}
}

static void exi_xfer_write(exi_xfer_t *x) {
	bool last = gpio_get(EXI_PIN_CLK) != 0;
	uint32_t acc = 0;
	int bits = 0;

	exi_data_input();
	while (exi_wait_edge(EXI_SAMPLE_ON_RISING, &last)) {
		acc = (acc << 1) | (gpio_get(EXI_PIN_DATA) ? 1u : 0u);
		if (++bits == 8) {
			if (x->rxlen < x->rxmax)
				x->rx[x->rxlen++] = (uint8_t)acc;
			acc = 0;
			bits = 0;
		}
	}
}

static void exi_xfer_read(exi_xfer_t *x) {
	bool last = gpio_get(EXI_PIN_CLK) != 0;
	int idx = 0;
	int bit = 7;
	bool have = x->txlen > 0;
	uint8_t cur = have ? x->tx[0] : 0;

	gpio_set_dir(EXI_PIN_DATA, GPIO_OUT);
	gpio_put(EXI_PIN_DATA, have ? ((cur >> 7) & 1) : 0);

	while (exi_wait_edge(EXI_DRIVE_ON_RISING, &last)) {
		if (have) {
			if (bit == 0) {
				idx++;
				bit = 7;
				have = idx < x->txlen;
				cur = have ? x->tx[idx] : 0;
			} else {
				bit--;
			}
		}
		gpio_put(EXI_PIN_DATA, have ? ((cur >> bit) & 1) : 0);
	}
	exi_data_input();
}

void exi_slave_init(void) {
	gpio_init(EXI_PIN_CS);
	gpio_set_dir(EXI_PIN_CS, GPIO_IN);
	gpio_pull_up(EXI_PIN_CS);

	gpio_init(EXI_PIN_CLK);
	gpio_set_dir(EXI_PIN_CLK, GPIO_IN);
	gpio_pull_up(EXI_PIN_CLK);

	gpio_init(EXI_PIN_DATA);
	gpio_set_dir(EXI_PIN_DATA, GPIO_IN);
	gpio_pull_up(EXI_PIN_DATA);
}

void exi_slave_run_once(exi_xfer_t *x) {
	x->rxlen = 0;
	exi_wait_cs();
	if (x->drive)
		exi_xfer_read(x);
	else
		exi_xfer_write(x);
	while (exi_slave_cs_asserted())
		tight_loop_contents();
}

#endif /* EXI_SLAVE_USE_PIO */
