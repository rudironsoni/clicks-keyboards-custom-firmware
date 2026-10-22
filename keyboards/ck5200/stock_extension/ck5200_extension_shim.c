/*
 * CK-5200 stock-extension report-builder shim implementation.
 * See the header for the contract, the ABI, and the pinned open items.
 */
#include "ck5200_extension_shim.h"

#include <string.h>

void ck_shim_process(ck_engine_t *e, const uint8_t matrix[CK_ROWS],
                     uint32_t now_ms, ck_frame_t *out)
{
    uint8_t mods, usages[CK_REPORT_KEYS], count;
    ck_engine_tick(e, matrix, now_ms, &mods, usages, &count);

    /* V122 0x27A8-0x27B8 zeroes the payload and sets 0x020B.
     * The boot keyboard report is eight bytes at frame+3: modifiers,
     * reserved byte, then six usages. 0x2856-0x2860 submits 11 bytes
     * without the optional four-byte consumer record. */
    memset(out, 0, sizeof(*out));
    out->bytes[0] = CK_FRAME_TYPE0;
    out->bytes[1] = CK_FRAME_TYPE1;
    out->bytes[2] = 0x00;
    out->bytes[3] = mods;
    for (uint8_t i = 0; i < count && i < CK_REPORT_KEYS; i++)
        out->bytes[5 + i] = usages[i];
    out->len = 11;
}

#ifdef CK_SHIM_HOST

uint32_t ck_shim_next_deadline(const ck_engine_t *e)
{
    return ck_engine_next_deadline(e);
}

#else /* device build */

#include "ck5200_keymap_serialize.h"

/* Separate companion transport and USB HID submission, as called by the
 * original V122 builder at 0x2864 and 0x286C. Queue/completion behavior
 * remains an execution-profile obligation, not a host-test claim. */
extern int oem_send(const void *frame, int len);
extern int oem_hid_send(const void *report, int len);
extern void oem_system_init(void);
extern void oem_tick(void);
extern void oem_builder(void *state_obj, const uint8_t *matrix, int size);

/* No configuration channel exists yet. Start in exact stock delegation.
 * The offline execution fixture can enable the research remapping path
 * through this RAM byte. This is not a persistent settings operation. */
volatile uint8_t ck_remapping_enabled;
ck_keymap_t ck_extension_keymap;

/* V122 has countdowns, not a located free-running millisecond global.
 * The separate 0x3AEE hook increments this counter once per SysTick
 * service, never once per builder invocation. Nominal period: 1 ms.
 * Delayed/coalesced interrupts remain a timing limitation. */
static uint32_t shim_ms;
static uint8_t last_matrix[CK_ROWS];

/* EP1's OEM submit routine writes DMA immediately. It has no queue or
 * busy check (0x3E28-0x3EAA). Retain distinct reports until EP1 returns
 * to NAK, which the real IN-completion handler sets at 0x40CA-0x40D6. */
#define CK_REPORT_QUEUE 8
static ck_frame_t pending[CK_REPORT_QUEUE];
static uint8_t pending_head, pending_count;
static ck_frame_t last_enqueued;
static bool queue_fault, fault_release_sent;

static bool hid_ready(void)
{
    return (*(volatile uint16_t *)0x40023426u & 3u) == 2u;
}

static void flush_report(void)
{
    if (!hid_ready())
        return;
    if (pending_count) {
        ck_frame_t *frame = &pending[pending_head];
        oem_send(frame->bytes, (int)frame->len);
        oem_hid_send(frame->bytes + 3, 8);
        pending_head = (pending_head + 1) % CK_REPORT_QUEUE;
        pending_count--;
    } else if (queue_fault && !fault_release_sent) {
        /* Overflow mutes remaps, drains accepted reports, then emits a
         * terminal all-up report. It does not claim lossless overflow. */
        ck_frame_t empty = {.bytes = {0x0B, 2}, .len = 11};
        oem_send(empty.bytes, (int)empty.len);
        oem_hid_send(empty.bytes + 3, 8);
        fault_release_sent = true;
    }
}

static void enqueue_report(const ck_frame_t *frame)
{
    if (last_enqueued.len && !memcmp(last_enqueued.bytes, frame->bytes, frame->len))
        return;
    if (pending_count == CK_REPORT_QUEUE) {
        queue_fault = true;
        return;
    }
    pending[(pending_head + pending_count) % CK_REPORT_QUEUE] = *frame;
    pending_count++;
    last_enqueued = *frame;
}

extern uint8_t __extension_data_start[], __extension_data_end[];
extern const uint8_t __extension_data_load[];
extern uint8_t __extension_bss_start[], __extension_bss_end[];

/* Replaces the startup SystemInit call, after OEM data/BSS setup and
 * before OEM timer enable. Never depend on pre-existing RAM contents. */
void ck_extension_init(void)
{
    const uint8_t *src = __extension_data_load;
    for (uint8_t *p = __extension_data_start; p < __extension_data_end; p++)
        *p = *src++;
    for (uint8_t *p = __extension_bss_start; p < __extension_bss_end; p++)
        *p = 0;
    oem_system_init();
}

/* The default keymap record, embedded serialized in the image. */
extern const uint8_t ck_default_keymap_record[];
extern const unsigned int ck_default_keymap_record_len;

static ck_engine_t engine_storage;

void ck_extension_builder(void *state_obj, const uint8_t *matrix, int size)
{
    if (size != CK_ROWS)
        return;
    if (!ck_remapping_enabled) {
        oem_builder(state_obj, matrix, size);
        return;
    }
    if (engine_storage.map == 0) {
        if (ck_keymap_deserialize(ck_default_keymap_record,
                                   ck_default_keymap_record_len,
                                   &ck_extension_keymap) != 0) {
            /* Fail closed: an invalid embedded record must never produce
             * reports from zeroed memory; leave the engine disabled. */
            return;
        }
        ck_engine_init(&engine_storage, &ck_extension_keymap);
    }
    memcpy(last_matrix, matrix, CK_ROWS);
    if (queue_fault)
        return;
    ck_frame_t frame;
    ck_shim_process(&engine_storage, matrix, shim_ms, &frame);
    enqueue_report(&frame);
    flush_report();
}

/* Replaces only the call at 0x3AEE. The OEM interrupt entry/return stays
 * intact. Run OEM scan work first, then service a due engine deadline
 * even when the scanner is idle because all keys are released. */
void ck_extension_tick(void)
{
    shim_ms++;
    oem_tick();
    if (ck_remapping_enabled && engine_storage.map) {
        flush_report();
        if (queue_fault) {
            uint8_t held = 0;
            for (unsigned row = 0; row < CK_ROWS; row++)
                held |= last_matrix[row] & CK_ROW_MASK;
            if (!held && fault_release_sent && hid_ready()) {
                queue_fault = fault_release_sent = false;
                last_enqueued.len = 0;
                ck_engine_init(&engine_storage, &ck_extension_keymap);
            }
            return;
        }
        uint32_t next = ck_engine_next_deadline(&engine_storage);
        if (next != CK_NO_DEADLINE && shim_ms >= next)
            ck_extension_builder(0, last_matrix, CK_ROWS);
    }
}

#endif /* CK_SHIM_HOST */
