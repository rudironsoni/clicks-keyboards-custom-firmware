#ifndef CK5200_IRQ_H
#define CK5200_IRQ_H
#include <stdint.h>

/* Preserve the caller's MIE bit, including calls made from an ISR. */
static inline uint32_t ck_irq_save(void)
{
    uint32_t prior;
    __asm__ volatile ("csrrc %0, mstatus, %1" : "=r"(prior) : "r"(8u) : "memory");
    return prior;
}
static inline void ck_irq_restore(uint32_t prior)
{
    if (prior & 8u)
        __asm__ volatile ("csrs mstatus, %0" :: "r"(8u) : "memory");
}
#endif
