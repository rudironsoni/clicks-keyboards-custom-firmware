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
     * The descriptor defines eight bytes at frame+3: four left modifier
     * bits, padding, four usages, then consumer bits and padding.
     * 0x2856-0x2860 submits 11 bytes
     * without the optional four-byte consumer record. */
    memset(out, 0, sizeof(*out));
    out->bytes[0] = CK_FRAME_TYPE0;
    out->bytes[1] = CK_FRAME_TYPE1;
    out->bytes[2] = 0x00;
    out->bytes[3] = mods;
    if (count > CK_FRAME_KEYS)
        memset(out->bytes + 5, 1, CK_FRAME_KEYS);
    else
        for (uint8_t i = 0; i < count; i++)
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
#include "ck5200_irq.h"

/* Separate companion transport and USB HID submission, as called by the
 * original V122 builder at 0x2864 and 0x286C. Queue/completion behavior
 * remains an execution-profile obligation, not a host-test claim. */
extern int oem_transport(void *context, const void *frame, int len);
extern int oem_hid_send(const void *report, int len);
extern int oem_ep_submit(int endpoint, const void *report, int len);
extern void oem_system_init(void);
extern void oem_tick(void);
extern void oem_builder(void *state_obj, const uint8_t *matrix, int size);
extern void oem_layer(void *state_obj, const uint8_t *matrix, int size);
extern int oem_command(void *context, const uint8_t *request, int size);
extern void oem_pwm(void *timer, int channel, int enabled);

/* Start in exact stock delegation. The extension-only RAM protocol can
 * enable remapping after a complete validated apply. No flash write or
 * physical configuration is authorized. Legacy fixtures also use RAM. */
volatile uint8_t ck_remapping_enabled;
ck_keymap_t ck_extension_keymap;

/* V122 has countdowns, not a located free-running millisecond global.
 * The separate 0x3AEE hook increments this counter once per SysTick
 * service, never once per builder invocation. Nominal period: 1 ms.
 * Delayed/coalesced interrupts remain a timing limitation. */
static uint32_t shim_ms;
static uint8_t last_matrix[CK_ROWS];
static uint8_t native_matrix[CK_ROWS];
static void *last_state;
static ck_frame_t native_frame;
static ck_frame_t preview_frame, last_custom;
static bool capturing_native, native_fresh;
static bool previewing;
static void enqueue_report(const ck_frame_t *frame, bool force);

static int oem_send(const void *frame, int len)
{
    /* Original 0x4788 supplies GP-0x3E0 to this three-argument function. */
    return oem_transport((void *)0x20000460u, frame, len);
}

/* The two OEM submission entries are redirected here. Capture the
 * complete OEM packet only while constructing native output. Outside
 * that call, retain the original companion and EP1 submission ABIs. */
int ck_capture_companion(const uint8_t *frame, int len)
{
    if (!capturing_native)
        return oem_send(frame, len);
    if ((len != 11 && len != 15) || frame[0] != 11 || frame[1] != 2) {
        if (previewing) return len;
        if (len == 4 && frame[0] == 4 && frame[1] == 4) {
            ck_frame_t special = {.len = 4};
            memcpy(special.bytes, frame, 4);
            enqueue_report(&special, true);
            return len;
        }
        return oem_send(frame, len);
    }
    ck_frame_t *out = previewing ? &preview_frame : &native_frame;
    memcpy(out->bytes, frame, (size_t)len);
    out->len = (size_t)len;
    if (previewing)
        return len;
    native_fresh = true;
    return len;
}

int ck_capture_hid(const void *report, int len)
{
    return capturing_native ? len : oem_ep_submit(1, report, len);
}

void ck_capture_pwm(void *timer, int channel, int enabled)
{
    if (!previewing) oem_pwm(timer, channel, enabled);
}

/* EP1's OEM submit routine writes DMA immediately. It has no queue or
 * busy check (0x3E28-0x3EAA). Retain distinct reports until EP1 returns
 * to NAK, which the real IN-completion handler sets at 0x40CA-0x40D6. */
#define CK_REPORT_QUEUE 8
static ck_frame_t pending[CK_REPORT_QUEUE];
static uint8_t pending_head, pending_count;
static uint8_t pending_hid;
static ck_frame_t last_enqueued;
static bool queue_fault, fault_release_sent, fault_companion_sent;

static bool hid_ready(void)
{
    return *(volatile uint8_t *)0x20000444u
        && !(*(volatile uint16_t *)0x20000448u & 8u)
        && (*(volatile uint16_t *)0x40023426u & 3u) == 2u;
}

static __attribute__((noinline)) bool companion_send(const ck_frame_t *frame)
{
    return oem_send(frame->bytes, (int)frame->len) != 0
        || *(volatile uint8_t *)0x20000462u != 3;
}

static void flush_report(void)
{
    if (!hid_ready())
        return;
    if (pending_count) {
        ck_frame_t *frame = &pending[pending_head];
        bool delivered = companion_send(frame);
        if (pending_hid < pending_count) {
            ck_frame_t *hid = &pending[(pending_head + pending_hid) % CK_REPORT_QUEUE];
            if (hid->len >= 11) oem_hid_send(hid->bytes + 3, 8);
            pending_hid++;
        }
        if (delivered && pending_hid) {
            pending_head = (pending_head + 1) % CK_REPORT_QUEUE;
            pending_count--; pending_hid--;
        }
    }
    if (queue_fault && pending_hid == pending_count
        && (!fault_release_sent || !fault_companion_sent) && hid_ready()) {
        /* Overflow mutes remaps, drains accepted reports, then emits a
         * terminal all-up report. It does not claim lossless overflow. */
        ck_frame_t empty = {.bytes = {0x0B, 2}, .len = 11};
        if (!fault_companion_sent)
            fault_companion_sent = companion_send(&empty);
        if (!fault_release_sent) {
            oem_hid_send(empty.bytes + 3, 8);
            fault_release_sent = true;
        }
    }
}

static void enqueue_report(const ck_frame_t *frame, bool force)
{
    if (!force && last_enqueued.len == frame->len
        && !memcmp(last_enqueued.bytes, frame->bytes, frame->len))
        return;
    if (pending_count == CK_REPORT_QUEUE) {
        queue_fault = true;
        return;
    }
    pending[(pending_head + pending_count) % CK_REPORT_QUEUE] = *frame;
    pending_count++;
    if (frame->len >= 11) last_enqueued = *frame;
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

static ck_engine_t engine_storage;
static int last_projection;

static bool ensure_engine(void)
{
    if (engine_storage.map) return true;
    memset(&ck_extension_keymap, 0, sizeof(ck_extension_keymap));
    ck_extension_keymap.native_mask = CK_NATIVE_LAYERS;
    for (int layer = 0; layer < CK_LAYERS; layer++)
        for (int pos = 0; pos < CK_KEYS; pos++)
            ck_extension_keymap.action[layer][pos].kind = CK_ACTION_OEM;
    ck_engine_init(&engine_storage, &ck_extension_keymap);
    return true;
}

/* Extension-only command 0x94, protocol 1. All fields after the framing
 * bytes are little-endian. No command writes flash or resets hardware.
 * A complete packed record is staged before an all-up atomic apply. */
static uint8_t config_stage[CK_KEYMAP_RECORD_MAX];
static uint16_t config_size, config_written, config_generation;
static uint32_t config_nonce;

/* Continue the pinned OEM reset routine after its stolen LUI. */
__asm__(".section .text.ck_oem_usb_reset,\"ax\",@progbits\n"
        ".global ck_oem_usb_reset\nck_oem_usb_reset:\n"
        "lui a5,0x40023\nj oem_usb_reset_tail\n.previous\n");
extern void ck_oem_usb_reset(void);

void ck_extension_usb_reset(void)
{
    config_nonce = config_size = config_written = 0;
    pending_head = pending_count = pending_hid = 0;
    queue_fault = ck_remapping_enabled != 0;
    fault_release_sent = false;
    fault_companion_sent = false;
    ck_oem_usb_reset();
}

static uint16_t config_u16(const uint8_t *p) { return p[0] | (p[1] << 8); }

int ck_extension_command(void *context, const uint8_t *request, int size)
{
    if (!request || size < 2) return 0;
    if (request[1] != 0x94) return oem_command(context, request, size);
    if (size < 12 || size > 36 || request[0] != size) return 0;
    uint8_t reply[36] = {12, 2, 0x94, 0, 1, request[3]};
    uint8_t record[CK_KEYMAP_RECORD_MAX];
    uint32_t nonce;
    memcpy(&nonce, request + 4, 4);
    memcpy(reply + 6, &nonce, 4);
    uint32_t prior = ck_irq_save();
    uint16_t offset = config_u16(request + 10);
    uint16_t expected = config_u16(request + 8);
    bool valid = ensure_engine() && request[2] == 1 && nonce;
    uint16_t record_len = valid && request[3] <= 1
        ? ck_keymap_serialize(&ck_extension_keymap, record, sizeof(record)) : 0;
    if (!valid) reply[3] = 3;
    else if (request[3] == 0 && size == 12) {
        config_nonce = nonce;
        config_size = config_written = 0;
        const uint8_t caps[] = {'C','K','X','1',4,3,6,6,0xD2,3,24,1};
        memcpy(reply + 12, caps, sizeof(caps));
        memcpy(reply + 24, &record_len, 2);
        reply[26] = ck_remapping_enabled;
        reply[0] = 27;
    } else if (nonce != config_nonce) reply[3] = 3;
    else if (expected != config_generation || config_generation == UINT16_MAX) reply[3] = 4;
    else switch (request[3]) {
    case 1: { /* READ: one byte count, byte offset in the packed record. */
        if (size != 13 || !request[12] || request[12] > 24
            || offset > record_len || request[12] > record_len - offset) { reply[3] = 1; break; }
        memcpy(reply + 12, record + offset, request[12]);
        reply[0] += request[12];
        break;
    }
    case 2: /* BEGIN: offset declares complete record size. */
        if (size != 12 || offset < 6 || offset > sizeof(config_stage)) { reply[3] = 1; break; }
        config_size = offset; config_written = 0;
        break;
    case 3: /* WRITE: bounded, ordered chunks. */
        if (!config_size || size == 12 || offset != config_written
            || size - 12 > config_size - config_written) { reply[3] = 1; break; }
        memcpy(config_stage + offset, request + 12, size - 12);
        config_written += size - 12;
        break;
    case 4: { /* APPLY: explicit remapping enable byte, never automatic. */
        if (size != 13 || request[12] > 1 || offset || !config_size
            || config_written != config_size) { reply[3] = 1; break; }
        uint8_t held = 0;
        for (unsigned row = 0; row < CK_ROWS; row++) held |= last_matrix[row];
        if (held || pending_count || queue_fault || !hid_ready()
            || ck_engine_next_deadline(&engine_storage) != CK_NO_DEADLINE) { reply[3] = 2; break; }
        if (ck_keymap_deserialize(config_stage, config_size, &ck_extension_keymap)) { reply[3] = 1; break; }
        ck_engine_init(&engine_storage, &ck_extension_keymap);
        memset(native_matrix, 0, sizeof(native_matrix));
        memset(&native_frame, 0, sizeof(native_frame));
        memset(&last_custom, 0, sizeof(last_custom));
        last_enqueued.len = 0;
        last_projection = -1;
        ck_remapping_enabled = request[12];
        config_generation++;
        config_size = config_written = 0;
        break;
    }
    case 5: /* ABORT: discard staged bytes without changing the active map. */
        if (size != 12 || offset) { reply[3] = 1; break; }
        config_size = config_written = 0;
        break;
    default: reply[3] = 1;
    }
    memcpy(reply + 10, &config_generation, 2);
    ck_irq_restore(prior);
    oem_send(reply, reply[0]);
    return 0; /* Reply already submitted. Skip the OEM response buffer. */
}

/* Original latch handler treats any ordinary key as activity. A custom
 * key must consume the latch too, but must not inherit the native role
 * of its former physical position (for example Shift remapped to Esc).
 * The Q crossing is an activity marker only. Its native usage is removed
 * using a read-only preview of the original builder below. */
static bool native_input(uint8_t matrix[CK_ROWS])
{
    ck_engine_oem_matrix(&engine_storage, matrix);
    bool custom = false;
    for (int pos = 0; pos < CK_KEYS; pos++) {
        const ck_held_action_t *h = &engine_storage.held[pos];
        if (engine_storage.held_active[pos] && !h->oem && !h->is_layer_action
            && (h->usage || h->mods))
            custom = true;
    }
    /* These are the native ordinary-key masks at 0x2AEE-0x2B12. */
    uint8_t ordinary = (matrix[0] & 7) | (matrix[1] & 0x1F)
        | (matrix[2] & 0x17) | (matrix[3] & 0x1F)
        | (matrix[4] & 0x1F) | (matrix[5] & 0x0F);
    if (custom && !ordinary) {
        matrix[0] |= 1;
        return true;
    }
    return false;
}

/* Exact native selector used at 0x2A62-0x2AE4 and 0x2C28-0x2C38.
 * Sticky Symbols comes from the OEM latch, except while native GUI is
 * held or sticky Symbols is disabled. Cursor comes from context bit 4,
 * which the OEM gesture branch sets, not from a guessed held-key chord. */
uint8_t ck_native_modes(void *state_obj, const uint8_t matrix[CK_ROWS])
{
    uint8_t *state = state_obj;
    uint8_t *context;
    memcpy(&context, state + 0x40, sizeof(context));
    bool sym = (context[0x120] & 1) && !(matrix[2] & 0x20)
        ? (state[0x0C] & 2) != 0 : (matrix[5] & 0x10) != 0;
    return ((sym ? 1u << CK_LAYER_SYM : 0)
            | ((context[0] & 0x10) ? 1u << CK_LAYER_NAV : 0))
        & engine_storage.map->native_mask;
}

static void native_candidates(const uint8_t matrix[CK_ROWS], uint8_t out[CK_ROWS])
{
    memset(out, 0, CK_ROWS);
    int layer = ck_engine_layer(&engine_storage);
    for (int row = 0; row < CK_ROWS; row++)
        for (int col = 0; col < CK_COLS; col++) {
            uint8_t bit = 1u << col;
            if (!(matrix[row] & bit)) continue;
            int pos = ck_pos(row, col);
            bool native = engine_storage.prev_matrix[row] & bit
                ? engine_storage.held_active[pos] && engine_storage.held[pos].oem
                : ck_engine_resolve_action(engine_storage.map, layer, pos)->kind == CK_ACTION_OEM;
            if (native) out[row] |= bit;
        }
}

/* Keep underlying native tracking separate from an explicit custom layer.
 * Disabled native modes and output projections use private state/context
 * views. A real tracking call commits its state prefix, status byte, and
 * real backlight toggle. Temporary native-mode mask bits never persist. */
static void native_call(void *state_obj, const uint8_t matrix[CK_ROWS], bool layer, int projection)
{
    uint8_t mask = engine_storage.map->native_mask;
    bool copy = previewing || projection >= 0 || mask != CK_NATIVE_LAYERS;
    uint8_t state[0x44] __attribute__((aligned(4)));
    uint8_t context[0x128] __attribute__((aligned(4)));
    uint8_t input[CK_ROWS];
    uint8_t *original_context;
    if (!copy) {
        if (layer) oem_layer(state_obj, matrix, CK_ROWS);
        else oem_builder(state_obj, matrix, CK_ROWS);
        return;
    }
    memcpy(state, state_obj, sizeof(state));
    memcpy(&original_context, state + 0x40, sizeof(original_context));
    memcpy(context, original_context, sizeof(context));
    memcpy(input, matrix, CK_ROWS);
    if (!(mask & (1u << CK_LAYER_SYM))) {
        memset(state + 0x0C, 0, 4);
        context[0x120] &= ~1u;
        /* Cursor activation remains independent when Symbols is disabled. */
        bool cursor_gesture = (mask & (1u << CK_LAYER_NAV)) && (context[0x120] & 0x80)
            && !input[0] && !input[1] && input[2] == 0x20 && !input[3] && !input[4] && input[5] == 0x10;
        if (!cursor_gesture) input[5] &= ~0x10u;
    }
    if (!(mask & (1u << CK_LAYER_NAV))) {
        context[0x120] &= ~0x80u;
        context[0] &= ~0x10u;
    }
    if (projection >= 0) {
        context[0x120] &= ~0x81u;
        context[0] = (context[0] & ~0x10u) | (projection == CK_LAYER_NAV ? 0x10 : 0);
        input[5] &= ~0x10u;
        if (projection == CK_LAYER_SYM) {
            context[0x120] |= 1;
            state[0x0C] |= 2;
            state[0x0C] &= ~0x10u;
            input[5] |= 0x10;
        }
    }
    void *copy_context = context;
    memcpy(state + 0x40, &copy_context, sizeof(copy_context));
    if (layer) oem_layer(state, input, CK_ROWS);
    else oem_builder(state, input, CK_ROWS);
    if (!previewing && projection < 0) {
        memcpy(state_obj, state, 0x20);
        original_context[0] = context[0];
        /* OEM 0x2900 toggles the backlight bit even in a native-disable
         * policy view. Commit that real gesture, not temporary mode masks. */
        original_context[0x120] = (original_context[0x120] & ~4u) | (context[0x120] & 4u);
    }
}

static void preview_native(void *state_obj, const uint8_t matrix[CK_ROWS], int projection)
{
    /* The builder critical section prevents unrelated IRQ submissions. */
    memset(&preview_frame, 0, sizeof(preview_frame));
    previewing = capturing_native = true;
    native_call(state_obj, matrix, false, projection);
    previewing = capturing_native = false;
}

static void merge_keys(ck_frame_t *out, const ck_frame_t *in)
{
    out->bytes[3] |= in->bytes[3];
    out->bytes[9] |= in->bytes[9];
    for (unsigned i = 5; i < 5 + CK_FRAME_KEYS && in->bytes[i]; i++) {
        unsigned slot = 5;
        while (slot < 5 + CK_FRAME_KEYS && out->bytes[slot] && out->bytes[slot] != in->bytes[i])
            slot++;
        if (slot == 5 + CK_FRAME_KEYS) {
            memset(out->bytes + 5, 1, CK_FRAME_KEYS);
            return;
        }
        out->bytes[slot] = in->bytes[i];
    }
}

/* An OEM action selected by a custom layer keeps that projection until
 * release, including after one-shot consumption or activator release.
 * Render separate groups when simultaneous presses own different layers. */
static bool preview_held_native(void *state_obj, ck_frame_t *out)
{
    bool projected = false;
    for (int p = 0; p < CK_KEYS; p++)
        if (engine_storage.held_active[p] && engine_storage.held[p].oem
            && engine_storage.held[p].native_projection >= 0)
            projected = true;
    if (!projected) return false;
    memset(out, 0, sizeof(*out));
    for (int layer = -1; layer < CK_LAYERS; layer++) {
        uint8_t group[CK_ROWS] = {0};
        bool any = false;
        for (int p = 0; p < CK_KEYS; p++)
            if (engine_storage.held_active[p] && engine_storage.held[p].oem
                && engine_storage.held[p].native_projection == layer) {
                group[p / CK_COLS] |= 1u << (p % CK_COLS);
                any = true;
            }
        if (any) {
            preview_native(state_obj, group, layer);
            merge_keys(out, &preview_frame);
        }
    }
    return true;
}

void ck_extension_builder(void *state_obj, const uint8_t *matrix, int size)
{
    if (size != CK_ROWS)
        return;
    uint32_t prior = ck_irq_save();
    last_state = state_obj;
    memcpy(last_matrix, matrix, CK_ROWS);
    if (!ck_remapping_enabled) {
        oem_builder(state_obj, matrix, size);
        goto done;
    }
    if (!ensure_engine()) goto done;
    if (queue_fault) {
        /* Mute new presses, but let real releases reach native latch
         * tracking. Stale snapshots must not look held after reconnect. */
        for (int row = 0; row < CK_ROWS; row++)
            for (int col = 0; col < CK_COLS; col++)
                engine_storage.held_active[ck_pos(row, col)] &= (matrix[row] & (1u << col)) != 0;
        goto done;
    }
    if (engine_storage.oneshot_armed && shim_ms >= engine_storage.oneshot_deadline) {
        engine_storage.oneshot_armed = false;
        engine_storage.oneshot_from = -1;
    }
    uint8_t candidates[CK_ROWS];
    native_candidates(matrix, candidates);
    /* Preserve pre-activation ownership of the two native gesture keys. */
    if (!ck_engine_custom_layer(&engine_storage)) {
        engine_storage.native_owners[5] = candidates[5] & 0x10 & ~engine_storage.prev_matrix[5];
        engine_storage.native_owners[2] = candidates[2] & 0x20 & ~engine_storage.prev_matrix[2];
    }
    engine_storage.native_layers = ck_native_modes(state_obj, candidates);
    ck_frame_t frame;
    ck_shim_process(&engine_storage, matrix, shim_ms, &frame);
    uint8_t filtered[CK_ROWS], activity[CK_ROWS];
    ck_engine_oem_matrix(&engine_storage, filtered);
    bool marker = native_input(activity);
    bool custom_changed = memcmp(frame.bytes + 3, last_custom.bytes + 3, 8) != 0;
    last_custom = frame;
    native_fresh = false;
    int projection = ck_engine_custom_layer(&engine_storage) ? ck_engine_layer(&engine_storage) : -1;
    bool native_changed = memcmp(native_matrix, activity, CK_ROWS) || custom_changed;
    bool projection_changed = projection != last_projection;
    if (native_changed || projection_changed) {
        uint8_t projected_mods = 0;
        bool render = marker || projection >= 0 || projection_changed;
        if (render) {
            preview_native(state_obj, activity, projection);
            projected_mods = preview_frame.bytes[3];
            if (marker)
                preview_native(state_obj, filtered, projection);
        }
        ck_frame_t held_native;
        bool held_projection = preview_held_native(state_obj, &held_native);
        if (held_projection) {
            preview_frame = held_native;
            projected_mods = held_native.bytes[3];
            render = true;
        }
        if (native_changed) {
            capturing_native = true;
            native_call(state_obj, activity, false, -1);
            capturing_native = false;
        }
        if (render && (native_fresh || projection_changed)) {
            memcpy(native_frame.bytes + 5, preview_frame.bytes + 5, CK_FRAME_KEYS);
            native_frame.bytes[3] = projected_mods;
            if (held_projection || projection >= 0)
                native_frame.bytes[9] = preview_frame.bytes[9];
        }
        memcpy(native_matrix, activity, CK_ROWS);
        last_projection = projection;
    }
    if (!native_frame.len) {
        native_frame.bytes[0] = 11;
        native_frame.bytes[1] = 2;
        native_frame.len = 11;
    }
    ck_frame_t merged = native_frame;
    merge_keys(&merged, &frame);
    enqueue_report(&merged, native_fresh);
    flush_report();
done:
    ck_irq_restore(prior);
}

/* The scanner invokes the builder before this layer/backlight callback.
 * Give both OEM functions the same filtered physical-key history. */
void ck_extension_layer(void *state_obj, const uint8_t *matrix, int size)
{
    uint32_t prior = ck_irq_save();
    if (ck_remapping_enabled && engine_storage.map) {
        uint8_t filtered[CK_ROWS];
        native_input(filtered);
        native_call(state_obj, filtered, true, -1);
        uint8_t native[CK_ROWS];
        ck_engine_oem_matrix(&engine_storage, native);
        engine_storage.native_layers = ck_native_modes(state_obj, native);
    } else {
        oem_layer(state_obj, matrix, size);
    }
    ck_irq_restore(prior);
}

/* Replaces only the call at 0x3AEE. The OEM interrupt entry/return stays
 * intact. Run OEM scan work first, then service a due engine deadline
 * even when the scanner is idle because all keys are released. */
void ck_extension_tick(void)
{
    shim_ms++;
    oem_tick();
    uint32_t prior = ck_irq_save();
    if (ck_remapping_enabled && engine_storage.map) {
        flush_report();
        if (queue_fault) {
            uint8_t held = 0;
            for (unsigned row = 0; row < CK_ROWS; row++)
                held |= last_matrix[row] & CK_ROW_MASK;
            if (!held && fault_release_sent && fault_companion_sent && hid_ready()) {
                queue_fault = fault_release_sent = fault_companion_sent = false;
                last_enqueued.len = 0;
                pending_head = pending_count = pending_hid = 0;
                memset(native_matrix, 0, sizeof(native_matrix));
                memset(&native_frame, 0, sizeof(native_frame));
                memset(&last_custom, 0, sizeof(last_custom));
                ck_engine_init(&engine_storage, &ck_extension_keymap);
            }
            goto done;
        }
        uint32_t next = ck_engine_next_deadline(&engine_storage);
        if (next != CK_NO_DEADLINE && shim_ms >= next) {
            ck_extension_builder(last_state, last_matrix, CK_ROWS);
            ck_extension_layer(last_state, last_matrix, CK_ROWS);
        }
    }
done:
    ck_irq_restore(prior);
}

#endif /* CK_SHIM_HOST */
