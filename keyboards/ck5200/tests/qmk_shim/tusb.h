/* Host-test stub for the TinyUSB surface used by protocol.c.
 * Deliberately models TinyUSB 0.21 behavior relevant to the CK-5200:
 *  - tud_hid_report(instance, report_id=0, buf, len) copies len bytes to
 *    the endpoint buffer and returns true (accepted).
 *  - On transfer completion TinyUSB calls tud_hid_report_complete_cb with
 *    the endpoint buffer and xferred_bytes == len (10 for this firmware).
 */
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef enum { HID_REPORT_TYPE_INVALID = 0, HID_REPORT_TYPE_INPUT, HID_REPORT_TYPE_OUTPUT, HID_REPORT_TYPE_FEATURE } hid_report_type_t;

bool tud_mounted(void);
bool tud_suspended(void);
bool tud_hid_ready(void);
/* TinyUSB 0.21 public API: instance-0 wrapper (4-arg form is tud_hid_n_report). */
bool tud_hid_report(uint8_t report_id, void const *report, uint16_t len);
void tud_task(void);
uint32_t tud_vendor_n_write(uint8_t itf, const void *buf, uint32_t len);
void tud_init(uint8_t rhport);

/* Harness hooks (not part of TinyUSB): state + captured submissions. */
extern bool stub_mounted, stub_suspended, stub_hid_ready;
extern uint8_t stub_epin_buf[64];
extern long stub_submit_count;
