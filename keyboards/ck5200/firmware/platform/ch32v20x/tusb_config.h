#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_MCU                 OPT_MCU_CH32V20X
#define CFG_TUSB_OS                  OPT_OS_NONE
#define CFG_TUSB_DEBUG               0

#define CFG_TUD_ENABLED              1
#define CFG_TUD_MAX_SPEED            OPT_MODE_FULL_SPEED
#define CFG_TUD_ENDPOINT0_SIZE       64

#define CFG_TUD_HID                  1
#define CFG_TUD_HID_EP_BUFSIZE       64

#define CFG_TUD_VENDOR               1
#define CFG_TUD_VENDOR_RX_BUFSIZE    0
#define CFG_TUD_VENDOR_TX_BUFSIZE    0
#define CFG_TUD_VENDOR_TXRX_BUFFERED 0

#define CFG_TUD_CDC                  0
#define CFG_TUD_MSC                  0
#define CFG_TUD_MIDI                 0
#define CFG_TUD_AUDIO                0
#define CFG_TUD_DFU                  0
#define CFG_TUD_DFU_RUNTIME          0

#ifdef __cplusplus
}
#endif
