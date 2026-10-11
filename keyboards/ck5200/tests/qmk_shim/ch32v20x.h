/* Host shim: minimal WCH surface needed by the host QMK harnesses.
 * The flash model (host_flash_model.c) reproduces the physical CH32
 * flash semantics that matter to the QMK wear-leveling core:
 *   - erased memory reads 0xFFFFFFFF (all ones)
 *   - programming can only clear bits (write is AND)
 *   - fast page erase sets a 256-byte page back to all ones
 * Addresses are the absolute flash addresses used by the production
 * backing store (0x0800F800..0x0800FC00); the model maps them into the
 * host region.
 */
#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Host shim: the custom wait.h uses the WCH clock global. */
#define SystemCoreClock 144000000u

/* Minimal GPIO surface so the custom gpio.h parses on the host. */
typedef struct { int _unused; } GPIO_TypeDef;
extern GPIO_TypeDef *GPIOA, *GPIOB;

/* Host flash model (implemented in host_flash_model.c). */
void     host_flash_reset(void);
int      host_flash_page_erase_calls(void);
int      host_flash_word_writes(void);

/* WCH SDK surface used by the production backing store (host model).
 * Absolute flash addresses, exactly like the real SDK. */
typedef enum { FLASH_COMPLETE = 0, FLASH_ERROR = 1 } FLASH_Status;

void        FLASH_Unlock(void);
void        FLASH_Lock(void);
void        FLASH_Unlock_Fast(void);
void        FLASH_Lock_Fast(void);
FLASH_Status FLASH_ErasePage_Fast(uint32_t address);
FLASH_Status FLASH_ProgramWord(uint32_t address, uint32_t value);
void        NVIC_SystemReset(void);
