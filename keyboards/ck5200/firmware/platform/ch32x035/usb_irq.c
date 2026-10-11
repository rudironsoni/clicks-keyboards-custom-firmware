#include "tusb.h"
#include "ch32x035.h"

/* The X035 USBFS interrupt is IRQ 45 (vector slot 45 in the X035 startup
 * table); TinyUSB's ch32x035 port drives root port 0 (the V203 build used
 * port 1, which does not exist on this family). */
__attribute__((interrupt)) __attribute__((used)) void USBFS_IRQHandler(void) {
    tud_int_handler(0);
}

__attribute__((interrupt)) __attribute__((used)) void USBFSWakeUp_IRQHandler(void) {
    tud_int_handler(0);
}
