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
static bool real_irq;
static unsigned irq_entries, irq_returns, preview_writes;
static uint8_t usb_flags, usb_status;
static bool deferred_usb;
static unsigned usb_entries, nested_entries;
static uint8_t command_reply[64], host_report[8];
static unsigned command_reply_len;
static uint8_t usb_regs[0x40];
static bool nested_usb;
static bool last_queue_fault;
static uint64_t mask_start, max_mask_cycles;
static unsigned pwm_writes, pwm_suppressed;
static uint32_t copy_state, copy_context;
static bool inject_reset;
static uint32_t min_sp = 0x20005000;
static FILE *trace;
static uint32_t remap_address, keymap_address;
static bool startup_probe, receive_probe, corrupt_callback;

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
    if (!strcmp(name, "EP1-host-consume") && len == 8)
        memcpy(host_report, machine.ram + (a - CHG_RAM_BASE), 8);
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
    if (startup_probe && write)
        fprintf(trace, "{\"event\":\"startup-write\",\"pc\":%u,\"address\":%u,\"width\":%d,\"value\":%u}\n", pc, a, n, v);
    if (write && copy_state && pc >= 0x26D8 && pc < 0x3100) {
        if (inside(a, n, copy_state, copy_state + 0x44) && a + n > copy_state + 0x20)
            fail("OEM policy wrote outside copied-back state prefix", a);
        if (inside(a, n, copy_context, copy_context + 0x128) && a != copy_context) {
            if (a != copy_context + 0x120 || n != 1 || ((v ^ m->ram[a - CHG_RAM_BASE]) & ~4u))
                fail("OEM policy wrote an uncommitted context field", a);
        }
    }
    if (write && callbacks && image_end > 0x68D8
        && m->ram[CK_PREVIEWING - CHG_RAM_BASE]) {
        if (!inside(a, n, m->cpu.x[2], 0x20005000)
            && !inside(a, n, CK_PREVIEW_FRAME, CK_PREVIEW_FRAME + 20) /* RV32 frame: 16 + 4 */
            && !(a == CK_CAPTURE && n == 1) && !(a == CK_PREVIEWING && n == 1))
            fail("preview write outside stack or owned capture state", a);
        fprintf(trace, "{\"event\":\"preview-write\",\"pc\":%u,\"address\":%u,\"width\":%d,\"value\":%u}\n", pc, a, n, v);
        preview_writes++;
    }
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
        || inside(a, n, 0x40023400, 0x40023440)
        || ((a == 0x40010800 || a == 0x4001080C
             || a == 0x40010C00 || a == 0x40010C0C) && n == 4)
        || (a == 0x40010008 && n == 4)
        || ((a == 0x40010400 || a == 0x40010404
             || a == 0x40010408 || a == 0x4001040C) && n == 4)
        || (a == 0x40010C10 && n == 4 && write)
        || (a == 0x40023424 && n == 2 && write)
        || (a == 0x40023426 && n == 2)
        || ((a == 0x40023406 || a == 0x40023407) && n == 1)
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
    if (a == 0x40023406) {
        if (write) usb_flags &= ~*v; else *v = usb_flags;
        return true;
    }
    if (a == 0x40023407 && !write) { *v = usb_status; return true; }
    if (inside(a, n, 0x40023400, 0x40023440)) {
        if (write) memcpy(usb_regs + (a - 0x40023400), v, n);
        else { *v = 0; memcpy(v, usb_regs + (a - 0x40023400), n); }
        return true;
    }
    return false;
}
void gate_instruction(ChgMachine *m, uint32_t address)
{
    pc = address;
    if (startup_probe || (receive_probe && (pc == 0x4776 || pc == 0x629E
            || pc == 0x62BC || pc == 0x2208 || pc == 0x2218
            || pc == 0x715C || pc == 0x3216 || pc == 0x62CC)))
        fprintf(trace, "{\"event\":\"instruction\",\"pc\":%u,\"ra\":%u,\"sp\":%u,\"a0\":%u,\"a1\":%u,\"a2\":%u}\n",
                pc, m->cpu.x[1], m->cpu.x[2], m->cpu.x[10], m->cpu.x[11], m->cpu.x[12]);
    if (pc == 0x2796 || pc == 0x2958) {
        copy_state = image_end > 0x68D8 && m->cpu.x[10] != 0x20000068
            && !m->ram[CK_PREVIEWING - CHG_RAM_BASE] ? m->cpu.x[10] : 0;
        if (copy_state) copy_context = ram32(copy_state + 0x40);
    }
    if ((pc == 0x4F0E || pc == 0x4F1C) && image_end > 0x68D8
        && m->ram[CK_CAPTURE - CHG_RAM_BASE] && !m->ram[CK_PREVIEWING - CHG_RAM_BASE]) pwm_writes++;
    if (pc == CK_CAPTURE_PWM && image_end > 0x68D8 && m->ram[CK_PREVIEWING - CHG_RAM_BASE]) pwm_suppressed++;
    if (callbacks && image_end > 0x68D8) {
        bool fault = m->ram[CK_QUEUE_FAULT - CHG_RAM_BASE] != 0;
        if (fault != last_queue_fault) {
            fprintf(trace, "{\"event\":\"queue-fault-transition\",\"fault\":%s,\"pending\":%u}\n",
                    fault ? "true" : "false", m->ram[CK_PENDING_COUNT - CHG_RAM_BASE]);
            last_queue_fault = fault;
        }
    }
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
    if (pc == 0x62CC) {
        bytes_event("companion-submit", m->cpu.x[11], m->cpu.x[12]);
        if (m->cpu.x[12] <= sizeof(command_reply)) {
            memcpy(command_reply, m->ram + (m->cpu.x[11] - CHG_RAM_BASE), m->cpu.x[12]);
            command_reply_len = m->cpu.x[12];
        }
    }
    if (image_end > 0x68D8 && (pc == 0x62CC || pc == 0x3E28)
        && m->ram[CK_PREVIEWING - CHG_RAM_BASE])
        fail("preview reached a real transport", pc);
    if (pc == 0x3E28 && m->cpu.x[10] == 1) {
        submissions++;
        bytes_event("HID-submit", m->cpu.x[11], m->cpu.x[12]);
    }
    if (pc == 0x3E28 && m->cpu.x[10] == 3)
        bytes_event("companion-EP3-submit", m->cpu.x[11], m->cpu.x[12]);
}
void gate_trap(ChgMachine *m, uint32_t p, uint32_t cause, uint32_t value, int irq)
{
    (void)m; (void)p; (void)value;
    if (!real_irq || (irq != 12 && irq != 45) || cause != (0x80000000u | (unsigned)irq))
        fail("unexpected trap", cause);
    irq_entries++;
    if (irq == 45) {
        if (image_end > 0x68D8 && m->ram[CK_PREVIEWING - CHG_RAM_BASE])
            fail("USB IRQ entered during preview", p);
        usb_entries++;
        if (m->cpu.trap_depth) nested_entries++;
    }
}
void gate_mret(ChgMachine *m) { (void)m; if (real_irq) irq_returns++; }
void gate_csr(ChgMachine *m, uint32_t csr, bool write, uint32_t value)
{
    if (csr == 0x300 && write) {
        if ((m->cpu.mstatus & 8) && !(value & 8)) mask_start = m->cycles;
        if (!(m->cpu.mstatus & 8) && (value & 8) && mask_start) {
            uint64_t elapsed = m->cycles - mask_start;
            if (elapsed > max_mask_cycles) max_mask_cycles = elapsed;
            mask_start = 0;
        }
    }
    if (csr != 0x300 && csr != 0x305 && csr != 0x341 && csr != 0x804 && csr != 0xBC0)
        fail("unsupported CSR", csr);
}
static void step(void)
{
    if (real_irq) {
        if (inject_reset && callbacks && image_end > 0x68D8 && machine.ram[CK_PREVIEWING - CHG_RAM_BASE]) {
            inject_reset = false;
            if (machine.cpu.mstatus & 8) fail("reset could enter during builder mutation", pc);
            usb_flags = 1; usb_status = 0;
            machine.pfic.pending |= 1ull << 45;
            fprintf(trace, "{\"event\":\"reset-deferred-during-preview\"}\n");
        }
        if (!nested_usb && machine.cpu.trap_depth == 1 && machine.cpu.pc == 0x3AE6) {
            nested_usb = true;
            usb_flags = 2; usb_status = 0x21;
            machine.pfic.pending |= 1ull << 45;
        }
        if (!deferred_usb && callbacks && image_end > 0x68D8 && machine.ram[CK_PREVIEWING - CHG_RAM_BASE]) {
            deferred_usb = true;
            usb_flags = 2; usb_status = 0x23; /* Unrelated companion completion, not an unconsumed EP1 packet. */
            machine.pfic.pending |= 1ull << 45;
            if (machine.cpu.mstatus & 8) fail("preview did not mask interrupts", pc);
        }
        cpu_take_interrupt(&machine);
    }
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
static void usb_irq(uint8_t flags, uint8_t status)
{
    uint32_t saved[32];
    memcpy(saved, machine.cpu.x, sizeof(saved));
    usb_flags = flags; usb_status = status;
    machine.pfic.pending |= 1ull << 45;
    machine.cpu.pc = 0x2A52;
    if (!cpu_take_interrupt(&machine)) fail("USB IRQ not accepted", tick_ms);
    until(0x2A52);
    if (memcmp(saved, machine.cpu.x, sizeof(saved)) || machine.cpu.trap_depth || usb_flags)
        fail("USB IRQ did not preserve context or clear flags", usb_flags);
}
static void consume_ep3(void)
{
    uint16_t len;
    memcpy(&len, usb_regs + 0x2C, 2);
    if (!(usb_regs[0x2E] & 3) && len) {
        bytes_event("EP3-host-consume", 0x20000294, len);
        if (len <= sizeof(command_reply) && machine.ram[0x296] == 0x94) {
            memcpy(command_reply, machine.ram + 0x294, len);
            command_reply_len = len;
        }
        usb_irq(2, 0x23);
    }
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
        if (real_irq) {
            uint32_t saved[32];
            machine.cpu.pc = 0x2A52;
            machine.cpu.mstatus |= 8;
            memcpy(saved, machine.cpu.x, sizeof(saved));
            machine.pfic.pending |= 1ull << 12;
            if (!cpu_take_interrupt(&machine)) fail("SysTick IRQ not accepted", tick_ms);
            until(0x2A52);
            if (memcmp(saved, machine.cpu.x, sizeof(saved)) || machine.cpu.trap_depth)
                fail("IRQ return did not preserve registers", tick_ms);
        } else {
            call(0x3AEE + imm, 0, 0);
        }
        /* Synthetic ready host consumes the actual OEM EP1 buffer after
         * each timer service. It is not a USB controller IRQ model. */
        if ((ep_ctrl & 3) == 0 && tx_len && tick_ms % host_period == 0
            && (!real_irq || (machine.ram[0x444] && !(machine.ram[0x448] & 8)))) {
            if (tx_len != 8) fail("unexpected EP1 length", tx_len);
            bytes_event("EP1-host-consume", 0x20000214, tx_len);
            packets++;
            if (real_irq) {
                usb_irq(2, 0x21);
                if ((ep_ctrl & 3) != 2 || usb_flags) fail("OEM USB completion did not acknowledge EP1", ep_ctrl);
            } else {
                ep_ctrl = (ep_ctrl & ~3u) | 2;
            }
        }
        if (real_irq && machine.ram[0x444] && !(machine.ram[0x448] & 8)
            && tick_ms % host_period == 0) consume_ep3();
    }
}

static void layer_check_pair(const char *name, unsigned usage, unsigned usage2, unsigned mods)
{
    if (machine.ram[0x1CC] != 0xA7 || machine.ram[0x1CA] != 0x21)
        fail("native policy view changed OEM settings", machine.ram[0x1CC]);
    fprintf(trace, "{\"event\":\"layer-check\",\"name\":\"%s\",\"usage\":%u,\"usage2\":%u,\"mods\":%u,\"native_latch\":%u,\"native_cursor\":%u}\n",
            name, usage, usage2, mods, machine.ram[0x74], machine.ram[0xAC] & 0x10);
}

/* Offline byte link for the iOS protocol tests. Commands enter the actual
 * OEM companion adapter, not a native C copy of the configuration handler. */
static void config_server(void)
{
    char line[160];
    machine.ram[0x462] = 3; /* Synthetic authenticated companion session. */
    uint32_t completion = 0x3C80;
    memcpy(machine.ram + 0x44C, &completion, 4);
    uint32_t reset = 0x3C4C;
    memcpy(machine.ram + 0x41C, &reset, 4); /* Original reset callback clears OEM EP3 progress and iAP2 state. */
    usb_regs[0x2E] = 2;
    puts("ready"); fflush(stdout);
    while (fgets(line, sizeof(line), stdin)) {
        if (line[0] == 'q') break;
        uint8_t bytes[64]; unsigned n = 0, value;
        size_t length = strcspn(line + 2, "\r\n");
        if (length & 1 || length > 128) fail("invalid offline frame", length);
        for (size_t i = 0; i < length; i += 2) {
            if (sscanf(line + 2 + i, "%2x", &value) != 1) fail("invalid offline byte", i);
            bytes[n++] = value;
        }
        if (line[0] == 'p') {
            memcpy(machine.ram + 0x2400, bytes, n);
            bytes_event("configuration-request", 0x20002400, n);
            command_reply_len = 0;
            if (receive_probe) {
                call(0x4776, 0x2208, 0); /* Original receive-callback registration. */
                if (ram32(0x20000524) != 0x2208) fail("wrong receive callback", ram32(0x20000524));
                if (corrupt_callback) memset(machine.ram + 0x524, 0, 4);
                fprintf(trace, "{\"event\":\"receive-fixture\",\"callback\":%u,\"authentication\":%u}\n",
                        ram32(0x20000524), machine.ram[0x462]);
                machine.cpu.x[12] = n;
                call(0x629E, 0x20000460, 0x20002400);
            } else call(0x2208, 0x20002400, n);
            command_reply_len = 0;
            consume_ep3();
            printf("R ");
            for (unsigned i = 0; i < command_reply_len; i++) printf("%02x", command_reply[i]);
        } else if (line[0] == 'k' && n == 6) {
            memcpy(keys, bytes, 6); scan(40);
            printf("H ");
            for (unsigned i = 0; i < 8; i++) printf("%02x", host_report[i]);
        } else if (line[0] == 's' && n == 1) {
            usb_regs[5] = bytes[0] ? 4 : 0;
            usb_irq(4, 0);
            scan(40);
            printf("U %02x", machine.ram[0x448]);
        } else if (line[0] == 'r' && n == 0) {
            usb_irq(1, 0); scan(40); printf("U %02x", machine.ram[0x444]);
        } else if (line[0] == 'c' && n == 0) {
            machine.ram[0x462] = 3; /* Authentication remains a modeled handoff. */
            machine.ram[0x444] = 1; scan(40); printf("U %02x", machine.ram[0x444]);
        } else if (line[0] == 'j' && n == 0) {
            inject_reset = true; printf("U armed");
        } else if (line[0] == 'b' && n == 1) {
            put16(0x458, 0); put16(0x45A, bytes[0] ? 1 : 0);
            fprintf(trace, "{\"event\":\"companion-backpressure\",\"blocked\":%s}\n", bytes[0] ? "true" : "false");
            printf("U %02x", bytes[0]);
        } else fail("unknown offline command", line[0]);
        putchar('\n'); fflush(stdout);
    }
    fprintf(trace, "{\"event\":\"configuration-summary\",\"irq_entries\":%u,\"irq_returns\":%u,\"max_mask_cycles\":%llu,\"preview_writes\":%u,\"min_sp\":%u}\n",
            irq_entries, irq_returns, (unsigned long long)max_mask_cycles, preview_writes, min_sp);
    fclose(trace);
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
        for (int l = 0; l < CK_LAYERS; l++) {
            map.action[l][ck_pos(0,3)].layer_switch = CK_LAYER_BASE;
            map.action[l][ck_pos(3,5)].layer_switch = CK_LAYER_NAV;
            map.action[l][ck_pos(5,4)] = (ck_action_t){.kind = CK_ACTION_OEM};
        }
        layer_map(&map);
        keys[2] = 0x20; keys[5] = 0x10; scan(20);
        keys[2] = 0; keys[5] = 0; scan(20);
        keys[0] = 0x28; scan(20);
        fprintf(trace, "{\"event\":\"projected-consumer-check\",\"consumer\":4,\"cursor_exit\":true}\n");
        keys[0] = 0x20; scan(20);
        keys[3] = 0x20; scan(20); keys[1] = 0x20; scan(20);
        fprintf(trace, "{\"event\":\"projected-consumer-check\",\"consumer\":6,\"cursor_exit\":false}\n");
        keys[3] = 0; scan(20);
        fprintf(trace, "{\"event\":\"projected-consumer-check\",\"consumer\":6,\"cursor_exit\":false}\n");
        keys[0] = 0; scan(20);
        fprintf(trace, "{\"event\":\"projected-consumer-check\",\"consumer\":2,\"cursor_exit\":false}\n");
        keys[1] = 0; scan(20);
        fprintf(trace, "{\"event\":\"projected-consumer-check\",\"consumer\":0,\"cursor_exit\":false}\n");
        const uint8_t masks[] = {CK_NATIVE_LAYERS, 1u << CK_LAYER_SYM};
        for (unsigned mi = 0; mi < sizeof(masks); mi++) {
            unsigned mask = masks[mi];
            map.native_mask = mask;
            for (int l = 0; l < CK_LAYERS; l++)
                map.action[l][ck_pos(3,5)].layer_switch = CK_LAYER_SYM;
            layer_map(&map);
            machine.ram[0x1CC] = 0xA7;
            unsigned before = pwm_writes, suppressed = pwm_suppressed;
            keys[5] = 0x10; scan(20); keys[3] = 0x20; scan(20);
            keys[1] = 0x20; scan(20);
            fprintf(trace, "{\"event\":\"backlight-check\",\"native_mask\":%u,\"pwm_writes\":%u,\"suppressed\":%u,\"flags\":%u}\n",
                    mask, pwm_writes - before, pwm_suppressed - suppressed, machine.ram[0x1CC]);
            keys[1] = 0; keys[3] = 0; keys[5] = 0; scan(20);
        }
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
    startup_probe = argc == 4 && !strcmp(argv[3], "startup-probe");
    receive_probe = argc == 4 && (!strcmp(argv[3], "receive-probe") || !strcmp(argv[3], "receive-corrupt-callback"));
    corrupt_callback = receive_probe && !strcmp(argv[3], "receive-corrupt-callback");
    if (argc == 6 && !strcmp(argv[5], "delayed-host")) host_period = 17;
    real_irq = (argc == 6 && !strcmp(argv[5], "real-irq"))
        || (argc == 4 && !strcmp(argv[3], "config-server")) || receive_probe;
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
    if (startup_probe) {
        /* No synthetic USB, authentication, settings, SP, callback or MMIO changes. */
        for (unsigned i = 0; i < 200000; i++) step();
        fail("startup instruction budget exceeded", machine.cpu.pc);
    }
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
    machine.ram[0x444] = 1; /* Synthetic configured host, not enumeration. */
    if (real_irq) {
        if (machine.cpu.mtvec != 0x2007 || machine.cpu.intsyscr != 3)
            fail("OEM CRT interrupt configuration mismatch", machine.cpu.mtvec);
        machine.pfic.enabled = (1ull << 12) | (1ull << 45);
        machine.pfic.prio[12] = 0x80;
        machine.pfic.prio[45] = 0;
    }
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
    if ((argc == 4 && !strcmp(argv[3], "config-server")) || receive_probe) {
        config_server();
        return 0;
    }
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
    fprintf(trace, "{\"event\":\"summary\",\"irq_entries\":%u,\"irq_returns\":%u,\"usb_entries\":%u,\"nested_entries\":%u,\"deferred_usb\":%s,\"preview_writes\":%u,\"callbacks\":%u,\"submissions\":%u,\"packets\":%u,\"min_sp\":%u,\"engine_calls\":%u,\"mode_probes\":%u,\"sticky_mode_probes\":%u,\"gui_mode_probes\":%u,"
            "\"sticky_hits\":%u,\"cursor_hits\":%u,\"currency_hits\":[%u,%u,%u,%u,%u],\"layout_hits\":[%u,%u]}\n",
            irq_entries, irq_returns, usb_entries, nested_entries, deferred_usb ? "true" : "false", preview_writes, callbacks, submissions, packets, min_sp, engine_calls, mode_probes, sticky_mode_probes, gui_mode_probes, sticky_hits, cursor_hits,
            currency_hits[0], currency_hits[1], currency_hits[2], currency_hits[3], currency_hits[4],
            layout_hits[0], layout_hits[1]);
    fclose(trace);
    printf("V122 bounded scanner: %u callbacks, %u submissions, %u packets\n", callbacks, submissions, packets);
    return 0;
}
