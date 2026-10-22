/*
 * CK-5200 stock-extension report-builder shim implementation v1.
 * See the header for the contract, the ABI, and the pinned open items.
 */
#include "ck5200_extension_shim.h"

#include <string.h>

void ck_shim_process(ck_engine_t *e, const uint8_t matrix[CK_ROWS],
                     uint32_t now_ms, ck_frame_t *out)
{
    uint8_t mods, usages[CK_REPORT_KEYS], count;
    ck_engine_tick(e, matrix, now_ms, &mods, usages, &count);

    /* Frame core from the decoded OEM constants: type 0x020B, then the
     * modifier byte, then the usages. Field [2] and the exact length
     * formula are pinned open items (header) and are zero here. */
    memset(out, 0, sizeof(*out));
    out->bytes[0] = CK_FRAME_TYPE0;
    out->bytes[1] = CK_FRAME_TYPE1;
    out->bytes[2] = 0x00;      /* unresolved field: see header open items */
    out->bytes[3] = mods;
    for (uint8_t i = 0; i < count && i < CK_REPORT_KEYS; i++)
        out->bytes[4 + i] = usages[i];
    out->len = (size_t)(4 + count);
}

#ifdef CK_SHIM_HOST

uint32_t ck_shim_next_deadline(const ck_engine_t *e)
{
    return ck_engine_next_deadline(e);
}

#else /* device build */

#include "ck5200_keymap_serialize.h"

/* OEM symbols provided by the research linker script. The send routine
 * address is proven (V122 0x278C); the tick global is UNPINNED, so the
 * device build uses a per-call placeholder counter marked here. */
extern int oem_send(const void *frame, int len);

/* CK_TIME_UNPINNED: placeholder time source pending the OEM 1 ms tick
 * RAM address. It advances monotonically per callback; tap/one-shot
 * windows therefore measure callback counts on the device build until
 * the tick global is pinned. The host harness uses injected time and
 * is the timing-verification path. */
static uint32_t shim_time_placeholder = 0;

/* The default keymap record, embedded serialized in the image. */
extern const uint8_t ck_default_keymap_record[];
extern const unsigned int ck_default_keymap_record_len;

static ck_engine_t engine_storage;

void ck_extension_builder(void *state_obj, const uint8_t *matrix, int size)
{
    (void)state_obj;
    if (size != CK_ROWS)
        return;
    if (engine_storage.map == 0) {
        static ck_keymap_t loaded;
        if (ck_keymap_deserialize(ck_default_keymap_record,
                                  ck_default_keymap_record_len,
                                  &loaded) != 0) {
            /* Fail closed: an invalid embedded record must never produce
             * reports from zeroed memory; leave the engine disabled. */
            return;
        }
        ck_engine_init(&engine_storage, &loaded);
    }
    shim_time_placeholder += 1;   /* CK_TIME_UNPINNED placeholder */
    ck_frame_t frame;
    ck_shim_process(&engine_storage, matrix, shim_time_placeholder, &frame);
    oem_send(frame.bytes, (int)frame.len);
}

#endif /* CK_SHIM_HOST */
