#include "tusb.h"
#include "ch32v20x.h"

__attribute__((interrupt)) __attribute__((used)) void USBFS_IRQHandler(void) {
    tud_int_handler(1);
}

__attribute__((interrupt)) __attribute__((used)) void USBFSWakeUp_IRQHandler(void) {
    tud_int_handler(1);
}
