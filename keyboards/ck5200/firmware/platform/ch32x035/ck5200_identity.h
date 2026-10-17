#pragma once
#include <stdint.h>

/* V122 0x2276 reads this device-owned word. Both USB and Identify use ten
 * decimal digits, including leading zeroes. Never write the serial page. */
static inline void ck5200_serial(char out[11]) {
#ifdef CK5200_IDENTITY_HOST_TEST
    extern uint32_t ck5200_test_serial_word;
    uint32_t value = ck5200_test_serial_word;
#else
    uint32_t value = *(volatile const uint32_t *)(uintptr_t)0x0800F600u;
#endif
    out[10] = '\0';
    for (unsigned i = 10; i != 0; ) {
        out[--i] = (char)('0' + value % 10u);
        value /= 10u;
    }
}
