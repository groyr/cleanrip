/*
 * usb-exi-adapter - tusb_config.h
 *
 * TinyUSB の設定。RP2040 を USB ホスト（mass storage）として使う。
 * ライセンス: MIT
 */

#ifndef TUSB_CONFIG_H
#define TUSB_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_MCU                OPT_MCU_RP2040
#define CFG_TUSB_OS                 OPT_OS_PICO
#define CFG_TUSB_DEBUG              0

/* rhport0 をホスト（フルスピード）として使用 */
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_HOST | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUH_ENABLED
#define CFG_TUH_ENABLED             1
#endif

/* mass storage のみ。ハブは今のところ非対応 */
#define CFG_TUH_MSC                 1
#define CFG_TUH_HUB                 0
#define CFG_TUH_DEVICE_MAX          1
#define CFG_TUH_MSC_MAXLUN          1

#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

#ifdef __cplusplus
}
#endif

#endif /* TUSB_CONFIG_H */
