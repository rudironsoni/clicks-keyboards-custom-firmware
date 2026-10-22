/* Separate V122 bounded profile. No QMK profile or guest instruction is
 * replaced. Synthetic handoff stops before SystemInit. Scanner setup,
 * GPIO scanning, both builders and endpoint submission execute on CPU.
 * This is not enumeration, iAP authentication, or full startup evidence. */
#include "gate.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../ck5200_keymap_engine.h"

static ChgMachine machine;
static uint32_t image_end, pc;
static uint8_t keys[6];
static uint16_t tx_len, ep_ctrl = 2;
static uint32_t exti[4]; /* INTENR, EVENR, RTENR, FTENR. No EXTI IRQs injected. */
static unsigned callbacks, submissions, packets, tick_ms;
static unsigned host_period = 1;
static unsigned currency_hits[5], layout_hits[2], sticky_hits, cursor_hits, engine_calls;
static unsigned mode_probes, sticky_mode_probes, gui_mode_probes;
static uint32_t builder_state;
static uint32_t min_sp = 0x20005000;
static FILE *trace;
static uint32_t remap_address, keymap_address;

static void fail(const char *why, uint32_t value)
{
    fprintf(stderr, "V122 profile: %s at pc=0x%x value=0x%x\n", why, pc, value);
    exit(1);
}
static bool inside(uint32_t a, unsigned n, uint32_t lo, uint32_t hi)
{
    return a >= lo && a < hi && n <= hi - a;
}
static void bytes_event(const char *name, uint32_t a, unsigned len)
{
    if (!inside(a, len, CHG_RAM_BASE, CHG_RAM_BASE + CHG_RAM_SIZE))
        fail("trace buffer outside RAM", a);
    fprintf(trace, "{\"ms\":%u,\"pc\":%u,\"event\":\"%s\",\"hex\":\"", tick_ms, pc, name);
    for (unsigned i = 0; i < len; i++)
        fprintf(trace, "%02x", machine.ram[a - CHG_RAM_BASE + i]);
    fprintf(trace, "\"}\n");
}
static uint32_t ram32(uint32_t address)
{
    uint32_t value;
    if (!inside(address, 4, CHG_RAM_BASE, CHG_RAM_BASE + CHG_RAM_SIZE))
        fail("mode pointer outside SRAM", address);
    memcpy(&value, machine.ram + (address - CHG_RAM_BASE), 4);
    return value;
}
void gate_access(ChgMachine *m, uint32_t a, int n, bool write, uint32_t v)
{
    (void)m; (void)v;
    if (n != 1 && n != 2 && n != 4) fail("unsupported width", n);
    if (inside(a, n, CHG_RAM_BASE, CHG_RAM_BASE + CHG_RAM_SIZE)) {
        if (write && pc < 0x6900 && a >= 0x200006B4 && a < 0x20002000
            && !(pc >= 0x2160 && pc < 0x2208)
            && !(pc >= 0x6416 && pc < 0x64C8))
            fail("OEM write into extension reservation", a);
        return;
    }
    uint32_t off = a >= CHG_FLASH_ALIAS ? a - CHG_FLASH_ALIAS : a;
    if (inside(off, n, 0x2000, image_end)) {
        if (write) fail("guest flash write denied", a);
        return;
    }
    if ((a == 0x40010808 && n == 4 && !write)
        || ((a == 0x40010800 || a == 0x4001080C
             || a == 0x40010C00 || a == 0x40010C0C) && n == 4)
        || (a == 0x40010008 && n == 4)
        || ((a == 0x40010400 || a == 0x40010404
             || a == 0x40010408 || a == 0x4001040C) && n == 4)
        || (a == 0x40010C10 && n == 4 && write)
        || (a == 0x40023424 && n == 2 && write)
        || (a == 0x40023426 && n == 2)
        || (a == 0x40012C20 && n == 2)
        || (a == 0xE000F004 && n == 4 && write && v == 0))
        return;
    fail("unsupported memory access", a);
}
bool gate_bus(ChgMachine *m, uint32_t a, int n, bool write, uint32_t *v)
{
    (void)n;
    if (a == 0x40010400 || a == 0x40010404 || a == 0x40010408 || a == 0x4001040C) {
        unsigned i = (a - 0x40010400) / 4;
        if (write) exti[i] = *v; else *v = exti[i];
        return true;
    }
    if (a == 0x40010808 && !write) {
        *v = 0;
        for (int row = 0; row < 6; row++)
            if (m->gpio[1].outdr & (1u << row)) *v |= keys[row];
        return true;
    }
    if (a == 0x40023424) {
        if ((ep_ctrl & 3) == 0) fail("EP1 overwrite before completion", *v);
        tx_len = *v;
        return true;
    }
    if (a == 0x40023426) {
        if (write) ep_ctrl = *v; else *v = ep_ctrl;
        return true;
    }
    if (a == 0xE000F004) return true;
    return false;
}
void gate_instruction(ChgMachine *m, uint32_t address)
{
    pc = address;
    if (pc == 0x2796) builder_state = m->cpu.x[10];
    if (pc == 0x2A84) {
        uint32_t context = ram32(builder_state + 0x40);
        uint32_t matrix = m->cpu.x[11];
        if (!inside(matrix, 6, CHG_RAM_BASE, CHG_RAM_BASE + CHG_RAM_SIZE))
            fail("mode matrix outside SRAM", matrix);
        uint8_t flags = m->ram[context - CHG_RAM_BASE + 0x120];
        uint8_t latch = m->ram[builder_state - CHG_RAM_BASE + 0x0C];
        uint8_t gui = m->ram[matrix - CHG_RAM_BASE + 2] & 0x20;
        uint8_t physical_sym = m->ram[matrix - CHG_RAM_BASE + 5] & 0x10;
        bool expected = (flags & 1) && !gui ? (latch & 2) != 0 : physical_sym != 0;
        if ((m->cpu.x[15] != 0) != expected) fail("OEM Symbols selector does not match audited state", m->cpu.x[15]);
        mode_probes++;
        if (expected && !physical_sym) sticky_mode_probes++;
        if ((latch & 2) && gui && !physical_sym && !expected) gui_mode_probes++;
    }
    if (pc == CK_ENGINE_PROCESS) engine_calls++;
    if (pc == 0x2A7E) sticky_hits++;
    if (pc == 0x2B40) cursor_hits++;
    if (pc == 0x2F7C && m->ram[0x1CB] < 5) currency_hits[m->ram[0x1CB]]++;
    if ((pc == 0x2DF8 || pc == 0x2FAA || pc == 0x2FC2)
        && m->ram[0x1CA] >= 0x20 && m->ram[0x1CA] <= 0x21)
        layout_hits[m->ram[0x1CA] - 0x20]++;
    if (m->cpu.x[2] && m->cpu.x[2] < min_sp) min_sp = m->cpu.x[2];
    if (!inside(pc, 2, 0x2000, image_end)) fail("PC outside image", pc);
    if (m->cpu.x[2] && m->cpu.x[2] < 0x20002000)
        fail("stack entered extension reservation", m->cpu.x[2]);
    if (pc == 0x2796 || pc == 0x6900) {
        callbacks++;
        bytes_event(pc == 0x2796 ? "OEM-builder" : "extension-builder", m->cpu.x[11], 6);
    }
    if (pc == 0x25A6) {
        uint32_t want = image_end > 0x68D8 ? 0x6900 : 0x2796;
        if (m->cpu.x[15] != want || m->cpu.x[12] != 6)
            fail("registered builder or ABI mismatch", m->cpu.x[15]);
        bytes_event("registered-builder-call", m->cpu.x[11], 6);
    }
    if (pc == 0x62CC) bytes_event("companion-submit", m->cpu.x[11], m->cpu.x[12]);
    if (pc == 0x3E28 && m->cpu.x[10] == 1) {
        submissions++;
        bytes_event("HID-submit", m->cpu.x[11], m->cpu.x[12]);
    }
}
void gate_trap(ChgMachine *m, uint32_t p, uint32_t cause, uint32_t value, int irq)
{
    (void)m; (void)p; (void)cause; (void)value; (void)irq;
    fail("unexpected trap", cause);
}
void gate_mret(ChgMachine *m) { (void)m; }
void gate_csr(ChgMachine *m, uint32_t csr, bool write, uint32_t value)
{
    (void)m; (void)write; (void)value;
    if (csr != 0x300 && csr != 0x305 && csr != 0x341 && csr != 0x804 && csr != 0xBC0)
        fail("unsupported CSR", csr);
}
static void step(void)
{
    machine.stop_at = machine.cycles + 1;
    cpu_exec(&machine);
}
static void until(uint32_t target)
{
    unsigned limit = 200000;
    while (machine.cpu.pc != target && limit--) step();
    if (machine.cpu.pc != target) fail("instruction budget exceeded", target);
}
static void call(uint32_t target, uint32_t a0, uint32_t a1)
{
    machine.cpu.pc = target;
    machine.cpu.x[1] = 0x2A52; /* existing RET, stop before executing it */
    machine.cpu.x[10] = a0;
    machine.cpu.x[11] = a1;
    until(0x2A52);
}
static void put16(unsigned off, unsigned value)
{
    machine.ram[off] = value;
    machine.ram[off + 1] = value >> 8;
}
static void scan(unsigned ms)
{
    while (ms--) {
        tick_ms++;
        /* The real interrupt's JAL at 0x3AEE supplies the selected timer
         * driver. Follow that JAL, but do not fabricate an IRQ handoff. */
        uint32_t word;
        memcpy(&word, machine.flash + 0x3AEE, 4);
        int32_t imm = ((word >> 31) << 20) | (((word >> 12) & 255) << 12)
            | (((word >> 20) & 1) << 11) | (((word >> 21) & 1023) << 1);
        imm = (imm ^ 0x100000) - 0x100000;
        call(0x3AEE + imm, 0, 0);
        /* Synthetic ready host consumes the actual OEM EP1 buffer after
         * each timer service. It is not a USB controller IRQ model. */
        if ((ep_ctrl & 3) == 0 && tx_len && tick_ms % host_period == 0) {
            if (tx_len != 8) fail("unexpected EP1 length", tx_len);
            bytes_event("EP1-host-consume", 0x20000214, tx_len);
            packets++;
            ep_ctrl = (ep_ctrl & ~3u) | 2;
        }
    }
}

static void layer_check_pair(const char *name, unsigned usage, unsigned usage2, unsigned mods)
{
    if (machine.ram[0x1CC] != 0xA7 || machine.ram[0x1CA] != 0x21)
        fail("native policy view changed OEM settings", machine.ram[0x1CC]);
    fprintf(trace, "{\"event\":\"layer-check\",\"name\":\"%s\",\"usage\":%u,\"usage2\":%u,\"mods\":%u,\"native_latch\":%u,\"native_cursor\":%u}\n",
            name, usage, usage2, mods, machine.ram[0x74], machine.ram[0xAC] & 0x10);
}
static void layer_check(const char *name, unsigned usage, unsigned mods)
{
    layer_check_pair(name, usage, 0, mods);
}

static void layer_map(ck_keymap_t *map)
{
    memcpy(machine.ram + (keymap_address - CHG_RAM_BASE), map, sizeof(*map));
}

static void layer_scenario(const char *name)
{
    ck_keymap_t map;
    memcpy(&map, machine.ram + (keymap_address - CHG_RAM_BASE), sizeof(map));
    map.action[CK_LAYER_SYM][0] = (ck_action_t){.usage = 0x29};
    map.action[CK_LAYER_NAV][0] = (ck_action_t){.usage = 0x2B};
    map.action[CK_LAYER_NAV][ck_pos(4,4)] = (ck_action_t){.usage = 0x1D, .mods = 8};
    bool sym_off = !strcmp(name, "layers-sym-off");
    bool nav_off = !strcmp(name, "layers-nav-off");
    if (sym_off) map.native_mask &= ~(1u << CK_LAYER_SYM);
    if (nav_off) map.native_mask &= ~(1u << CK_LAYER_NAV);
    if (!strcmp(name, "layers-replace")) {
        for (int l = 0; l < CK_LAYERS; l++)
            map.action[l][ck_pos(5,4)] = (ck_action_t){.activation = CK_ACT_HOLD, .layer_switch = CK_LAYER_NAV};
        layer_map(&map);
        keys[0] = 1; keys[5] = 0x10; scan(20);
        layer_check("replacement-wins-coalesced", 0x2B, 0);
        keys[5] = 0; scan(20); layer_check("replacement-held-snapshot", 0x2B, 0);
        keys[0] = 0; scan(20); layer_check("replacement-release", 0, 0);
        keys[0] = 1; scan(20); layer_check("replacement-no-native-replay", 0x14, 0);
        keys[0] = 0; scan(20);
        for (int l = 0; l < CK_LAYERS; l++)
            map.action[l][ck_pos(5,4)] = (ck_action_t){.usage = 0x29};
        layer_map(&map);
        keys[5] = 0x10; scan(20); layer_check("symbols-replaced-by-key", 0x29, 0);
        if (machine.ram[0x74] & 2) fail("replaced Symbols key activated native latch", machine.ram[0x74]);
        keys[5] = 0; scan(20);
        for (int l = 0; l < CK_LAYERS; l++) {
            map.action[l][ck_pos(5,4)] = (ck_action_t){.kind = CK_ACTION_OEM};
            map.action[l][ck_pos(2,5)] = (ck_action_t){.usage = 0x2B};
        }
        layer_map(&map);
        keys[2] = 0x20; keys[5] = 0x10; scan(20);
        layer_check("gui-replaced-without-cursor", 0x2B, 0);
        if (machine.ram[0xAC] & 0x10) fail("replaced GUI key activated Cursor", machine.ram[0xAC]);
        keys[2] = 0; keys[5] = 0; scan(20); layer_check("replacement-final-all-up", 0, 0);
        /* OEM U must keep its custom Base projection after its hold key
         * releases. A later OEM I owns custom NAV at the same time. */
        for (int l = 0; l < CK_LAYERS; l++) {
            map.action[l][ck_pos(0,3)] = (ck_action_t){.activation = CK_ACT_HOLD, .layer_switch = CK_LAYER_BASE};
            map.action[l][ck_pos(3,5)] = (ck_action_t){.activation = CK_ACT_HOLD, .layer_switch = CK_LAYER_NAV};
        }
        layer_map(&map);
        keys[0] = 0x0A; scan(20); layer_check("oem-custom-base-snapshot", 0x18, 0);
        keys[0] = 2; scan(20); layer_check("oem-snapshot-after-hold-release", 0x18, 0);
        keys[3] = 0x20; scan(20); keys[1] = 1; scan(20);
        layer_check_pair("overlapping-oem-projections", 0x18, 0x52, 0);
        keys[3] = 0; scan(20); layer_check_pair("overlap-after-activator-release", 0x18, 0x52, 0);
        keys[0] = 0; scan(20); layer_check("overlap-first-key-release", 0x52, 0);
        keys[1] = 0; scan(20); layer_check("overlap-all-up", 0, 0);
        for (int l = 0; l < CK_LAYERS; l++) {
            map.action[l][ck_pos(0,3)].layer_switch = CK_LAYER_NAV;
            map.action[l][ck_pos(2,5)] = (ck_action_t){.kind = CK_ACTION_OEM};
            map.action[l][ck_pos(5,4)] = (ck_action_t){.kind = CK_ACTION_OEM};
        }
        map.action[CK_LAYER_NAV][ck_pos(5,4)] = (ck_action_t){.usage = 0x29};
        layer_map(&map);
        keys[0] = 8; keys[5] = 0x10; scan(20);
        layer_check("custom-layer-replaces-native-key-coalesced", 0x29, 0);
        keys[0] = 0; keys[5] = 0; scan(20);
        map.action[CK_LAYER_BASE][ck_pos(5,4)] = (ck_action_t){.usage = 0x29};
        map.action[CK_LAYER_NAV][ck_pos(5,4)] = (ck_action_t){.kind = CK_ACTION_OEM};
        layer_map(&map);
        keys[0] = 8; scan(20); keys[0] = 0; keys[5] = 0x10; scan(20);
        layer_check("release-precedes-native-key-classification", 0x29, 0);
        if (machine.ram[0x74] & 2) fail("custom replacement activated Symbols after layer release", machine.ram[0x74]);
        keys[5] = 0; scan(20); layer_check("ownership-final-all-up", 0, 0);
        return;
    }
    if (!strcmp(name, "layers-modes")) {
        const unsigned usage[] = {0x14, 0x29, 0x2B};
        for (int mode = CK_ACT_TAP; mode <= CK_ACT_ONESHOT; mode++)
            for (int target = 0; target < CK_LAYERS; target++) {
                for (int l = 0; l < CK_LAYERS; l++)
                    map.action[l][ck_pos(0,3)] = (ck_action_t){.activation = mode,
                        .layer_switch = target, .tap_usage = 0x28, .tap_ms = 200};
                layer_map(&map);
                keys[5] = 0x10; scan(20);
                keys[0] = 9; scan(20); layer_check("custom-mode-target", usage[target], 0);
                keys[0] = 8; scan(20); keys[0] = 0; scan(20);
                if (mode == CK_ACT_TOGGLE) { keys[0] = 8; scan(20); keys[0] = 0; scan(20); }
                keys[0] = 1; scan(20); layer_check("custom-mode-native-fallback", 0x29, 0);
                keys[0] = 0; scan(20); keys[5] = 0; scan(20); layer_check("custom-mode-all-up", 0, 0);
            }
        for (int l = 0; l < CK_LAYERS; l++) {
            map.action[l][ck_pos(0,3)] = (ck_action_t){.activation = CK_ACT_HOLD, .layer_switch = CK_LAYER_SYM};
            map.action[l][ck_pos(3,5)] = (ck_action_t){.activation = CK_ACT_HOLD, .layer_switch = CK_LAYER_NAV};
        }
        map.action[CK_LAYER_SYM][ck_pos(3,5)] = (ck_action_t){.usage = 0x28};
        layer_map(&map);
        keys[0] = 9; keys[3] = 0x20; scan(20); layer_check("same-scan-activator-target-snapshots", 0x2B, 0);
        keys[3] = 0; scan(20); layer_check("overlap-keeps-key-snapshot", 0x2B, 0);
        keys[0] = 8; scan(20); keys[0] = 9; scan(20); layer_check("overlap-falls-back-to-held-symbols", 0x29, 0);
        keys[0] = 0; scan(20); layer_check("overlap-activators-all-up", 0, 0);
        return;
    }
    int sweep = !strcmp(name, "layers-base-sweep") ? CK_LAYER_BASE
        : !strcmp(name, "layers-sym-sweep") ? CK_LAYER_SYM
        : !strcmp(name, "layers-nav-sweep") ? CK_LAYER_NAV : -1;
    if (sweep >= 0)
        for (int p = 0; p < CK_KEYS; p++) map.action[sweep][p] = (ck_action_t){.usage = 4 + p};
    layer_map(&map);
    if (sweep >= 0) {
        for (int p = 0; p < CK_KEYS; p++) {
            if (sweep == CK_LAYER_NAV) {
                keys[2] = 0x20; keys[5] = 0x10; scan(20);
                keys[2] = 0; keys[5] = 0; scan(20);
            }
            if (sweep == CK_LAYER_SYM) { keys[5] = 0x10; scan(20); keys[5] = 0; scan(20); }
            keys[p / CK_COLS] = 1u << (p % CK_COLS); scan(20);
            layer_check("all-position-press", 4 + p, 0);
            keys[p / CK_COLS] = 0; scan(20); layer_check("all-position-release", 0, 0);
        }
        return;
    }
    keys[5] = 0x10; scan(20); keys[0] = 1; scan(20);
    layer_check("native-symbols", sym_off ? 0x14 : 0x29, 0);
    keys[5] = 0; scan(20); layer_check("native-symbols-held-key-snapshot", sym_off ? 0x14 : 0x29, 0);
    keys[0] = 0; scan(20); layer_check("native-symbols-release", 0, 0);
    keys[5] = 0x10; scan(20); keys[5] = 0; scan(20); keys[0] = 1; scan(20);
    layer_check("native-sticky-symbols-with-key-up", sym_off ? 0x14 : 0x29, 0);
    keys[0] = 0; scan(20); keys[0] = 1; scan(20); layer_check("native-sticky-consumed", 0x14, 0);
    keys[0] = 0; scan(20);
    if (!strcmp(name, "layers-sync")) {
        keys[5] = 0x10; scan(20); keys[5] = 0; scan(20);
        keys[2] = 0x20; scan(20); keys[0] = 1; scan(20);
        /* 0x28A2-0x28DC removes the saved GUI crossing on this next press. */
        layer_check("gui-suppresses-sticky-symbols", 0x14, 0);
        keys[0] = 0; keys[2] = 0; scan(20);
    }
    keys[2] = 0x20; keys[5] = 0x10; scan(20); /* genuine OEM Cursor gesture */
    keys[2] = 0; scan(20); keys[0] = 1; scan(20);
    layer_check("native-cursor", nav_off ? 0x29 : 0x2B, 0);
    keys[0] = 0; scan(20); keys[5] = 0; scan(20);
    if (!nav_off) {
        /* OEM 0x2810-0x2846 exits Cursor on a non-arrow action. Enter
         * again before checking another programmable NAV position. */
        keys[2] = 0x20; keys[5] = 0x10; scan(20);
        keys[2] = 0; keys[5] = 0; scan(20);
        keys[4] = 0x10; scan(20); layer_check("unused-nav-position", 0x1D, 8);
        keys[4] = 0; scan(20);
    }
    for (int l = 0; l < CK_LAYERS; l++)
        map.action[l][ck_pos(0,3)] = (ck_action_t){.activation = CK_ACT_HOLD, .layer_switch = CK_LAYER_BASE};
    layer_map(&map);
    keys[0] = 9; scan(20); layer_check("custom-base-priority", 0x14, 0);
    keys[0] = 8; scan(20); keys[0] = 0; scan(20); layer_check("final-all-up", 0, 0);
}
int main(int argc, char **argv)
{
    if (argc < 3 || argc > 6) return 2;
    bool compatibility = (argc == 4 && !strcmp(argv[3], "compat"))
        || (argc == 6 && !strcmp(argv[5], "compat"));
    bool mixed = (argc == 4 && !strcmp(argv[3], "mixed"))
        || (argc == 6 && !strcmp(argv[5], "mixed"));
    bool layers = argc == 6 && !strncmp(argv[5], "layers-", 7);
    bool native_active = argc == 6 && (!strcmp(argv[5], "native-active") || compatibility || mixed || layers);
    if (argc == 6 && !strcmp(argv[5], "delayed-host")) host_period = 17;
    chg_init(&machine);
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 2;
    size_t len = fread(machine.flash + 0x2000, 1, 0x6A01, f);
    fclose(f);
    if (len < 18648 || len > 0x6A00) return 2;
    image_end = 0x2000 + len;
    trace = fopen(argv[2], "w");
    if (!trace) return 2;
    memset(machine.ram, 0xA5, sizeof(machine.ram));
    machine.entry = 0x2000;
    cpu_reset(&machine);
    until(0x3BBA); /* execute genuine CRT and extension init, not SystemInit */
    if (memcmp(machine.ram, machine.flash + 0x6880, 0x58))
        fail("OEM data initialization mismatch", 0x6880);
    for (unsigned i = 0x58; i < 0x6B4; i++)
        if (machine.ram[i]) fail("OEM BSS not cleared", i);
    if (image_end > 0x68D8)
        for (unsigned i = CK_BSS_START - CHG_RAM_BASE; i < CK_BSS_END - CHG_RAM_BASE; i++)
            if (machine.ram[i]) fail("extension BSS not cleared", i);
    fprintf(trace, "{\"event\":\"CRT-to-SystemInit\",\"sp\":%u,\"gp\":%u}\n",
            machine.cpu.x[2], machine.cpu.x[3]);
    if (machine.cpu.x[2] > 0x20005000 || machine.cpu.x[2] < 0x20004F00
        || machine.cpu.x[3] != 0x20000840)
        fail("unexpected OEM GP/SP", machine.cpu.x[3]);
    /* Stop-before-SystemInit leaves extension init's call frame active.
     * Restore SP for the separately bounded scanner entry fixture. */
    machine.cpu.x[2] = 0x20005000;
    machine.ram[0x1CC] = 0xA7; /* stock flags, synthetic settings */
    machine.ram[0x1CA] = 0x21; /* stock layout */
    put16(0x1D0, 5000);
    put16(0x1D2, 600);
    call(0x2A22, 0x20000068, 0x200000AC);
    if (argc >= 5) {
        remap_address = strtoul(argv[3], 0, 0);
        keymap_address = strtoul(argv[4], 0, 0);
        if (!inside(remap_address, 1, 0x200006C0, 0x20002000)
            || !inside(keymap_address, sizeof(ck_keymap_t), 0x200006C0, 0x20002000))
            fail("fixture keymap address outside extension SRAM", keymap_address);
        if (native_active) machine.ram[remap_address - CHG_RAM_BASE] = 1;
    }
    scan(12);
    keys[0] = 1; scan(20);  /* Q press */
    keys[0] = 0; scan(20);  /* Q release, no later physical key */
    if (layers) {
        layer_scenario(argv[5]);
    } else if (argc >= 5 && !native_active) {
        machine.ram[remap_address - CHG_RAM_BASE] = 1;
        keys[0] = 1; scan(20); keys[0] = 0; scan(20);
        /* Synthetic configuration fixture, not a USB configuration command. */
        ck_keymap_t map;
        memcpy(&map, machine.ram + (keymap_address - CHG_RAM_BASE), sizeof(map));
        map.action[0][ck_pos(0, 3)] =
            (ck_action_t){.activation = CK_ACT_HOLD, .layer_switch = CK_LAYER_NAV};
        map.action[2][ck_pos(2, 3)] = (ck_action_t){.usage = 0x29};
        map.action[2][ck_pos(3, 4)] = (ck_action_t){.usage = 0x2B};
        map.action[2][ck_pos(4, 4)] = (ck_action_t){.usage = 0x1D, .mods = 8};
        map.action[2][ck_pos(5, 3)] = (ck_action_t){.usage = 0x1D, .mods = 10};
        map.action[2][ck_pos(1, 0)] = (ck_action_t){.usage = 0x52};
        memcpy(machine.ram + (keymap_address - CHG_RAM_BASE), &map, sizeof(map));
        fprintf(trace, "{\"event\":\"synthetic-NAV-configuration\"}\n");
        keys[0] = 8; scan(20);
        const unsigned positions[][2] = {{2,3}, {3,4}, {4,4}, {5,3}, {1,0}};
        for (unsigned i = 0; i < sizeof positions / sizeof *positions; i++) {
            keys[positions[i][0]] |= 1u << positions[i][1]; scan(20);
            keys[positions[i][0]] &= ~(1u << positions[i][1]); scan(20);
        }
        keys[0] = 0; scan(20);
        /* Four keyboard slots plus the native consumer bit. The fourth
         * custom usage must not overwrite the descriptor's consumer byte. */
        keys[0] = 0x2E; keys[2] = 8; keys[4] = 1; scan(20);
        keys[0] = 8; keys[2] = 0; keys[4] = 0; scan(20);
        keys[0] = 0; scan(20);
        /* Tap release while the OEM scanner goes idle on an empty matrix. */
        map.action[0][ck_pos(0,3)].activation = CK_ACT_TAP;
        map.action[0][ck_pos(0,3)].tap_usage = 0x29;
        map.action[0][ck_pos(0,3)].tap_ms = 200;
        memcpy(machine.ram + (keymap_address - CHG_RAM_BASE), &map, sizeof(map));
        fprintf(trace, "{\"event\":\"synthetic-TAP-configuration\"}\n");
        keys[0] = 8; scan(20); keys[0] = 0; scan(40);
    } else {
        if (native_active && mixed) {
            ck_keymap_t map;
            memcpy(&map, machine.ram + (keymap_address - CHG_RAM_BASE), sizeof(map));
            for (unsigned layer = 0; layer < CK_LAYERS; layer++)
                map.action[layer][0] = (ck_action_t){.usage = 0x29};
            memcpy(machine.ram + (keymap_address - CHG_RAM_BASE), &map, sizeof(map));
        }
        if (mixed) {
            fprintf(trace, "{\"event\":\"mixed-mapping-scenario\"}\n");
            keys[2] = 8; scan(20); keys[2] = 0; scan(20);
            keys[0] = 1; scan(20); keys[0] = 0; scan(20);
            keys[0] = 2; scan(20); keys[0] = 0; scan(20);
            keys[0] = 0x20; scan(20); keys[0] |= 1; scan(20);
            keys[0] &= ~1u; scan(20); keys[0] = 0; scan(20);
            keys[0] = 1; scan(20); keys[2] = 8; scan(20);
            keys[2] = 0; scan(20); keys[0] = 0; scan(20);
            keys[0] = 2; scan(20); keys[0] = 0; scan(20);
        }
        /* Exact stock comparison covers each position, sticky state, and
         * consumer crossings. It is broader than the keyboard-page model. */
        for (unsigned row = 0; !mixed && row < 6; row++)
            for (unsigned col = 0; col < 6; col++) {
                if (compatibility && row == 0 && col == 3) continue;
                keys[row] |= 1u << col; scan(20);
                keys[row] &= ~(1u << col); scan(20);
            }
        if (compatibility) {
            const uint8_t flags[] = {0, 1, 2, 3, 4, 8, 0x10, 0x20, 0x40, 0x80, 0xA7, 0xE7, 0xFF};
            for (unsigned fi = 0; fi < sizeof(flags); fi++)
                for (unsigned layout = 0x20; layout <= 0x21; layout++)
                    for (unsigned currency = 0; currency < 5; currency++) {
                        machine.ram[0x1CC] = flags[fi];
                        machine.ram[0x1CA] = layout;
                        machine.ram[0x1CB] = currency;
                        fprintf(trace, "{\"event\":\"synthetic-OEM-settings\",\"flags\":%u,\"layout\":%u,\"currency\":%u}\n",
                                flags[fi], layout, currency);
                        keys[2] = 8; scan(20); keys[2] = 0; scan(20); /* sticky Shift */
                        keys[0] |= 1; scan(20); keys[0] &= ~1u; scan(20);
                        keys[5] = 0x10; scan(20); /* held Symbols */
                        keys[3] = 8; scan(20); keys[3] = 0; scan(20); /* layout-sensitive Z */
                        keys[5] |= 4; scan(20); keys[5] &= ~4u; scan(20); /* currency/Clicks */
                        keys[1] = 0x20; scan(20); keys[1] = 0; scan(20); /* consumer chord */
                        keys[2] = 0x20; scan(20); keys[2] = 0; scan(20); /* cursor gesture */
                        keys[1] = 1; scan(20); keys[1] = 0; scan(20);
                        keys[5] = 0; scan(20);
                        keys[3] = 0x20; scan(20); keys[3] = 0; scan(20); /* space */
                        keys[0] |= 0x10; scan(20); keys[0] &= ~0x10u; scan(20); /* enter */
                    }
        }
    }
    if (!callbacks || submissions < 2 || packets < 2)
        fail("missing scanner or HID path", packets);
    fprintf(trace, "{\"event\":\"summary\",\"callbacks\":%u,\"submissions\":%u,\"packets\":%u,\"min_sp\":%u,\"engine_calls\":%u,\"mode_probes\":%u,\"sticky_mode_probes\":%u,\"gui_mode_probes\":%u,"
            "\"sticky_hits\":%u,\"cursor_hits\":%u,\"currency_hits\":[%u,%u,%u,%u,%u],\"layout_hits\":[%u,%u]}\n",
            callbacks, submissions, packets, min_sp, engine_calls, mode_probes, sticky_mode_probes, gui_mode_probes, sticky_hits, cursor_hits,
            currency_hits[0], currency_hits[1], currency_hits[2], currency_hits[3], currency_hits[4],
            layout_hits[0], layout_hits[1]);
    fclose(trace);
    printf("V122 bounded scanner: %u callbacks, %u submissions, %u packets\n", callbacks, submissions, packets);
    return 0;
}
