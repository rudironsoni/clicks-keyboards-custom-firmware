/* Host integration only. This does not contain the original stock installer. */
#ifndef STOCK_MANAGER_BRIDGE_H
#define STOCK_MANAGER_BRIDGE_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Call once per process. A second call returns false without changing state.
 * Use a fresh test process for each scenario, so all production statics reset.
 * 0: stock-reference simulation, NOT real stock firmware C. Only version bytes
 *    change. Update behavior remains the production QM implementation.
 * 1: production QM entry and identity. 2: production RS startup token and entry.
 * Returns true after the production main loop completes eight USB task calls. */
bool sm_boot(int mode);
/* Mount, attach, and parse AA02, AA04, EA00 through real EP2 RX. The auth chip
 * is modeled. No session flags are assigned by this bridge. */
bool sm_authenticate(void);
/* response must hold 8 bytes. Returns 0 or 8. Calls real EP3 RX, services the
 * transport, then completes the USB TX. A0 returns NO ACK in every mode.
 * sm_reboot_count() records accepted reset requests, not a new firmware boot. */
size_t sm_send(const uint8_t *bytes, size_t length, uint8_t response[8]);
/* Parse EA01, then unmount. Explicit host teardown also aborts staging in QM.
 * Production automatically aborts on session loss only in RS. */
void sm_disconnect(void);
/* These use the real parser/watchdog. They do not add QM cancellation gates. */
void sm_close_session(void);
void sm_advance_time(uint32_t milliseconds);
/* Fail the nth subsequent page-write attempt before erase, once. Includes A1
 * invalidation and A3 commit. nth <= 0 disables the fault. Counts attempts. */
void sm_fail_write(int nth);
uint32_t sm_write_count(void);
uint32_t sm_reboot_count(void);
uint32_t sm_marker(void);
bool sm_staged_matches(const uint8_t *bytes, size_t length);
/* Checks installer [0,0x2000) and all bytes [0xF500,0x10000), including config
 * and serial. Also checks the application until the host installer runs. */
bool sm_protected_intact(void);
/* HOST INSTALLER MODEL ONLY. Requires a successful A3, a committed 18648-byte
 * marker, and the official V122 SHA-256 over staged flash. Copies exact bytes
 * to modeled application flash at 0x2000 and checks protected regions.
 * Changes subsequent version bytes to stock. Does not execute stock code,
 * reboot production statics, clear the marker, or prove hardware recovery. */
bool sm_model_install(void);
#endif
