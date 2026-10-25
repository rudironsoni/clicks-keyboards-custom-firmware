#include "tusb.h"
#include "device/usbd_pvt.h"

void ck5200_vendor_tx_failed(uint8_t idx);

/* TinyUSB's vendor callback omits result and even runs on failed transfers.
 * Use its supported app-driver override to cancel ACK authority first. The
 * actual pinned vendor implementation still owns every transfer and endpoint. */
static bool vendor_xfer(uint8_t port, uint8_t ep, xfer_result_t result, uint32_t bytes) {
    if (result != XFER_RESULT_SUCCESS && (ep == 0x82 || ep == 0x83)) {
        ck5200_vendor_tx_failed((uint8_t)(ep - 0x82));
    }
    return vendord_xfer_cb(port, ep, result, bytes);
}

const usbd_class_driver_t *usbd_app_driver_get_cb(uint8_t *count) {
    static const usbd_class_driver_t driver = {
        .init = vendord_init,
        .deinit = vendord_deinit,
        .reset = vendord_reset,
        .open = vendord_open,
        .control_xfer_cb = vendord_control_xfer_cb,
        .xfer_cb = vendor_xfer,
    };
    *count = 1;
    return &driver;
}
