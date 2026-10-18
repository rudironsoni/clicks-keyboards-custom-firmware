/* Fail-closed access contract for the reviewed CK5200 BIN and connected host profile.
 * Register definitions: pinned OpenWCH EVT/EXAM/SRC/Peripheral/inc/ch32x035{,_usb}.h.
 * PFIC/SysTick/CSR definitions: pinned OpenWCH EVT/EXAM/SRC/Core/core_riscv.h.
 * Unlisted registers, widths, directions and write values are model failures.
 */
#include "gate.h"
#include "profile.h"
#include <string.h>

Gate g;
void gate_fail(const char *why, uint32_t detail) {
    g.failure = why; g.detail = detail;
    longjmp(g.abort, 1);
}
static void require(bool ok, const char *why, uint32_t detail) {
    if (!ok) gate_fail(why, detail);
}
static void progress_deadlines(ChgMachine *m) {
    if (g.fixture || g.fault_active) return; /* Fault tests use IWDG expiry. */
    require(m->cycles - g.last_systick_return <= GATE_PROGRESS_GAP,
            "SysTick-progress-timeout", g.pc);
    require(m->cycles - g.last_foreground <= GATE_PROGRESS_GAP,
            "foreground-progress-timeout", g.pc);
}
static bool inside(uint32_t a, unsigned n, uint32_t low, uint32_t high) {
    return a >= low && a < high && n <= high - a;
}
static void event(ChgMachine *m, const char *name) {
    if (g.trace) fprintf(g.trace, "{\"cycle\":%llu,\"pc\":%u,\"event\":\"%s\"}\n",
                         (unsigned long long)m->cycles, g.pc, name);
}
static uint64_t watchdog_period(uint32_t reload) {
    static const unsigned divisor[] = {4,8,16,32,64,128,256,256};
    return (uint64_t)(reload + 1) * 1024 * divisor[g.watchdog_pscr];
}
void gate_watchdog_reset(void) {
    g.watchdog_pscr=0; g.watchdog_rld=0xfff;
    g.watchdog_deadline=g.watchdog_enable_cycle=g.watchdog_pvu_until=g.watchdog_rvu_until=0;
    g.watchdog_enabled=g.watchdog_unlocked=false;
}
void gate_watchdog_expire(ChgMachine *m) {
    require(g.watchdog_enabled && g.watchdog_deadline && m->cycles>=g.watchdog_deadline,
            "watchdog-expiry-order",g.pc);
    require(g.watchdog_fault_mode,"unexpected-watchdog-reset",g.pc);
    m->cycles=g.watchdog_deadline;
    g.watchdog_resets++;
    event(m,"IWDG-expiry");
    uint32_t prior_flags=m->reset_flags;
    chg_reset(m,false); /* Retain RAM; execute the actual application startup next. */
    m->reset_flags=prior_flags | 0x20000000u; /* Sticky causes, plus IWDGRSTF. */
    gate_watchdog_reset();
    memcpy(g.noinit,m->ram+(P_NOINIT-CHG_RAM_BASE),sizeof g.noinit);
    uint32_t request[2]; memcpy(request,g.noinit,sizeof request);
    g.rescue=request[0]==0x43524d32u && request[1]==~0x43524d32u;
    g.stage=g.scan_stage=g.startup_returns=0;
    g.pending_return=g.pending_handler=false;
    g.irqs=g.returns=g.observed_returns=g.handler_entries=0;
    g.usb_irqs=g.usb_returns=g.usb_observed_returns=g.usb_handler_entries=0;
    g.foreground_after_irq=0;
    g.last_foreground=g.last_systick_return=m->cycles;
    g.fault_active=false;
    event(m,"IWDG-CPU-reset");
}
typedef struct { uint32_t address, mask; unsigned width, directions; } Register;
#define R(a,n) {a,0,n,1}
#define W(a,n,mask) {a,mask,n,2}
#define RW(a,n,mask) {a,mask,n,3}
/* The masks describe the supported profile, sometimes narrower than hardware.
 * Reads of a valid register retain its documented read-only bits. */
static const Register registers[] = {
    R(0x1ffff704,4),
    RW(0x40021000,4,0x0000fffb), /* HSI only. No PLL on X035. */
    RW(0x40021004,4,0x070000f0), /* HPRE/MCO, not legacy SW/SWS */
    RW(0x40021014,4,0x00021005), RW(0x40021018,4,0x00005a1d),
    RW(0x40021024,4,0xff000000),
    RW(0x40010018,4,0x0f0303cf), /* AFIO CTLR, comparator bits are RO */
    RW(0x40022000,4,3), /* latency only. All unlock/program/erase denied. */
    W(0xe000e100,4,0x1000), W(0xe000e104,4,0x00002000),
    W(0xe000e184,4,0x00002000), /* USBFS IRQ 45 */
    RW(0xe000e42d,1,0xff),
    RW(0xe000f000,4,0x0f), W(0xe000f004,4,0),
    RW(0xe000f008,4,0xffffffff), RW(0xe000f00c,4,0xffffffff),
    RW(0xe000f010,4,0xffffffff), RW(0xe000f014,4,0xffffffff),
    RW(0x40012c00,4,0x0081), /* TIM1 CEN/ARPE, no interrupts */
    RW(0x40012c18,4,0x6868), RW(0x40012c1c,4,0x0068),
    RW(0x40012c20,4,0x0111), RW(0x40012c28,4,0xffff),
    RW(0x40012c2c,4,0xffff), RW(0x40012c34,4,0xffff),
    RW(0x40012c38,4,0xffff), RW(0x40012c3c,4,0xffff),
    RW(0x40012c44,4,0x8000),
    RW(0x40023400,1,0x3f), RW(0x40023401,1,0x83),
    RW(0x40023402,1,0x17), RW(0x40023403,1,0xff),
    R(0x40023405,1), RW(0x40023406,1,0xff), R(0x40023407,1),
    R(0x40023408,2), RW(0x4002340c,1,0xdc), RW(0x4002340d,1,0xdd),
    RW(0x4002340e,1,0x3f),
    RW(0x40023410,4,0x20007ffc), RW(0x40023414,4,0x20007ffc),
    RW(0x40023418,4,0x20007ffc), RW(0x4002341c,4,0x20007ffc),
    RW(0x40023454,4,0x20007ffc), RW(0x40023458,4,0x20007ffc),
    RW(0x4002345c,4,0x20007ffc),
    RW(0x40023420,2,0x7f), RW(0x40023422,2,0xff),
    RW(0x40023424,2,0x7f), RW(0x40023426,2,0xff),
    RW(0x40023428,2,0x7f), RW(0x4002342a,2,0xff),
    RW(0x4002342c,2,0x7f), RW(0x4002342e,2,0xff),
    RW(0x40023430,2,0x7f), RW(0x40023432,2,0xff),
    RW(0x40023464,2,0x7f), RW(0x40023466,2,0xff),
    RW(0x40023468,2,0x7f), RW(0x4002346a,2,0xff),
    RW(0x4002346c,2,0x7f), RW(0x4002346e,2,0xff),
};
static void register_access(const Register *r, int n, bool write, uint32_t v) {
    require(n == (int)r->width, "register-width", r->address);
    if (n<4) v &= (1u<<(n*8))-1;
    require((r->directions & (write ? 2 : 1)) != 0, "register-direction", r->address);
    require(!write || !(v & ~r->mask), "register-value-model", r->address);
}
void gate_access(ChgMachine *m, uint32_t a, int n, bool write, uint32_t v) {
    if (g.watchdog_enabled && m->cycles>=g.watchdog_deadline)
        longjmp(g.watchdog_escape,1);
    require(n == 1 || n == 2 || n == 4, "memory-width", a);
    if (inside(a, n, CHG_RAM_BASE, CHG_RAM_BASE + CHG_RAM_SIZE)) return;
    if (inside(a, n, 0, CHG_FLASH_SIZE) ||
        inside(a, n, CHG_FLASH_ALIAS, CHG_FLASH_ALIAS + CHG_FLASH_SIZE)) {
        require(!write, "flash-write-denied", a);
        uint32_t off = a & 0xffff;
        require(inside(off, n, P_ENTRY, P_IMAGE_END) || inside(off,n,0xf600,0xf604),
                "unmodeled-flash-read", a);
        return;
    }
    if (a >= 0x40022000 && a < 0x40022400 && a != 0x40022000)
        gate_fail("flash-controller-denied", a);
    if (a >= 0x40003000 && a < 0x40003010) {
        require(n==4,"register-width",a); /* Vendor SDK uses 32-bit MMIO accesses. */
        require(!(a & 3),"unknown-memory-register",a);
        switch (a) {
        case 0x40003000:
            require(write,"register-direction",a);
            require(v==0x5555 || v==0xaaaa || v==0xcccc,"IWDG-key",v);
            break;
        case 0x40003004:
        case 0x40003008:
            require(a==0x40003004 ? m->cycles>=g.watchdog_pvu_until :
                                         m->cycles>=g.watchdog_rvu_until,
                    "IWDG-update-busy",a);
            require(!write || g.watchdog_unlocked,"IWDG-write-locked",a);
            require(!write || !(v & (a==0x40003004 ? ~7u : ~0xfffu)),
                    "register-value-model",a);
            break;
        case 0x4000300c:
            require(!write,"register-direction",a);
            break;
        }
        return;
    }
    for (unsigned i = 0; i < sizeof(registers)/sizeof(*registers); i++)
        if (registers[i].address == a) {
            register_access(&registers[i], n, write, v);
            if (write && a == 0x40021000) require(v & 1, "HSI-disable-unmodeled", a);
            return;
        }
    /* Three GPIO ports, each register checked separately, including WO set/reset. */
    for (unsigned port = 0; port < 3; port++) {
        uint32_t base = 0x40010800 + port * 0x400;
        static const Register gpio[] = {
            RW(0,4,0xffffffff), RW(4,4,0xffffffff), R(8,4), RW(12,4,0xffffff),
            W(16,4,0xffffffff), W(20,4,0xffffff), RW(28,4,0xffffffff), W(32,4,0x00ff00ff)
        };
        for (unsigned i = 0; i < sizeof(gpio)/sizeof(*gpio); i++) {
            Register r = gpio[i]; r.address += base;
            if (a == r.address) { register_access(&r,n,write,v); return; }
        }
    }
    (void)m;
    gate_fail("unknown-memory-register", a);
}
bool gate_bus(ChgMachine *m, uint32_t a, int n, bool write, uint32_t *v) {
    if (a==0x1ffff704 || a>=0x40000000) {
        if (write) g.mmio_writes++; else g.mmio_reads++;
    }
    if (a == 0x1ffff704) { *v = 0x03510601; return true; } /* synthetic C8T */
    if (g.usb_connected && a==0x40010808 && !write) {
        uint32_t pins=0;
        for (int pin=0;pin<24;pin++) if (gpio_pin(m,0,pin)) pins|=1u<<pin;
        unsigned rows=m->gpio[1].outdr&63;
        if (rows && !(rows&(rows-1))) {
            unsigned row=0;
            while (rows!=(1u<<row)) row++;
            unsigned row_cfg=(m->gpio[1].cfg[0]>>(4*row))&15;
            if ((m->rcc[6]&0x0c)==0x0c && (row_cfg&3) && gpio_pin(m,1,(int)row)) {
                for (unsigned col=0;col<6;col++) if (g.keys[row]&(1u<<col)) {
                    require(((m->gpio[0].cfg[0]>>(4*col))&15)==8 &&
                            !(m->gpio[0].outdr&(1u<<col)),"matrix-input-pulldown",a);
                    pins|=1u<<col;
                }
            }
        }
        *v=pins;
        return true;
    }
    if (a >= 0x40003000 && a < 0x40003010) {
        if (a==0x40003000) {
            if (*v==0x5555) g.watchdog_unlocked=true;
            else if (*v==0xaaaa) {
                g.watchdog_unlocked=false;
                if (g.watchdog_enabled && g.watchdog_mutant!=1) {
                    g.watchdog_feeds++;
                    g.watchdog_deadline=m->cycles+watchdog_period(g.watchdog_rld);
                }
                event(m,"IWDG-reload");
            } else {
                g.watchdog_unlocked=false;
                if (!g.watchdog_enabled) {
                    g.watchdog_enabled=true;
                    g.watchdog_enable_cycle=m->cycles;
                    /* RM does not state the internal count loaded by CCCC.
                     * Use the maximum 12-bit count until the explicit AAAA. */
                    g.watchdog_deadline=m->cycles+watchdog_period(0xfff);
                }
                event(m,"IWDG-enable");
                if (g.watchdog_fault_mode && g.fault_kind==3 &&
                    g.fault_injections<g.fault_limit) {
                    g.fault_active=true; g.fault_injections++;
                    m->stop_at=m->cycles; /* Stop the guest after this store. */
                    event(m,"injected-init-hang-after-IWDG-enable");
                }
            }
        } else if (a==0x40003004) {
            if (write) { g.watchdog_pscr=*v; g.watchdog_pvu_until=m->cycles+5; }
            else *v=g.watchdog_pscr;
        } else if (a==0x40003008) {
            if (write) { g.watchdog_rld=*v; g.watchdog_rvu_until=m->cycles+5; }
            else *v=g.watchdog_rld;
        } else {
            *v=(m->cycles<g.watchdog_pvu_until ? 1u : 0u) |
               (m->cycles<g.watchdog_rvu_until ? 2u : 0u);
            if (g.watchdog_fault_mode && g.fault_kind==8 && g.watchdog_enabled &&
                (g.fault_active || g.fault_injections<g.fault_limit)) {
                if (!g.fault_active) {
                    g.fault_active=true; g.fault_injections++;
                    event(m,"injected-post-enable-STATR-stuck");
                }
                *v |= 1u; /* Peripheral fault, separate from normal five-cycle update. */
            }
        }
        return true;
    }
    if (a >= 0x40023400 && a < 0x40023800) {
        if (g.usb_connected)
            require((m->rcc[5]&0x1000)!=0,"USB-clock-disabled",a);
        unsigned off = a - 0x40023400;
        if (!write) {
            *v = 0; memcpy(v, m->usb + off, (size_t)n);
            if (off == 5 || off == 6) *v |= 0x20; /* SIE idle, no host traffic */
            if (g.usb_model_mutant==2 && off==7 && ((*v>>4)&3)==0)
                *v |= 0x40; /* Wrong peripheral TOG_OK status for duplicate OUT. */
        } else if (off == 6) {
            if (g.usb_model_mutant!=1)
                m->usb[6] &= (uint8_t)~(*v & 0x1f); /* W1C, RO status untouched */
        } else {
            if (n == 4) {
                static const unsigned dma_offsets[]={0x10,0x14,0x18,0x1c,0x54,0x58,0x5c};
                unsigned extent=g.usb_connected && off==0x10 ? 192 : g.usb_connected ? 128 : 64;
                require(inside(*v,extent,CHG_RAM_BASE,CHG_RAM_BASE+CHG_RAM_SIZE),
                        "USB-DMA-range",*v);
                if (g.usb_connected) {
                    require(!(*v & 3),"USB-DMA-alignment",*v);
                    for (unsigned i=0;i<sizeof dma_offsets/sizeof *dma_offsets;i++) {
                        unsigned other=dma_offsets[i];
                        if (other==off) continue;
                        uint32_t base; memcpy(&base,m->usb+other,4);
                        if (!base) continue;
                        unsigned size=other==0x10 ? 192 : 128;
                        require(*v+extent<=base || base+size<=*v,
                                "USB-DMA-overlap",*v);
                    }
                }
            }
            memcpy(m->usb + off,v,(size_t)n);
            if (off == 0 && (*v & 6)) { m->usb[6] = 0; m->usb[7] = 0; }
        }
        if (g.usb_connected && write && (off==0 || off==2 || off==6))
            pfic_set_line(m,45,(m->usb[6] & m->usb[2] & 0x1f)!=0);
        return true;
    }
    /* X035 CFGR0 has no legacy SW/SWS fields. CTLR readiness follows enabled HSI. */
    if (a == 0x40021000) {
        if (write) m->rcc[0] = (*v & 0xf9) | (m->rcc[0] & 0xff00);
        else *v = m->rcc[0] | ((m->rcc[0] & 1) ? 2 : 0);
        return true;
    }
    if (a == 0x40021004) {
        if (write) m->rcc[1] = *v; else *v = m->rcc[1];
        return true;
    }
    return false;
}
void gate_csr(ChgMachine *m, uint32_t csr, bool write, uint32_t value) {
    uint32_t mask;
    switch (csr) {
    case 0x300: mask = 0x1888; break;
    case 0x305: case 0x341: mask = 0xffffffff; break;
    case 0x800: mask = 0x88; break;
    case 0x804: mask = 3; break;
    case 0xbc0: mask = 0x1f; break;
    default: gate_fail("unsupported-CSR", csr); return;
    }
    require(!write || !(value & ~mask), "CSR-value-model", csr);
    (void)m;
}
void gate_trap(ChgMachine *m, uint32_t pc, uint32_t cause, uint32_t tval, int irq) {
    g.pc=pc;
    require(!m->cpu.trap_depth &&
            ((irq==CHG_IRQ_SYSTICK && cause==0x8000000c) ||
             (g.usb_connected && irq==45 && cause==0x8000002d)),
            "unexpected-trap", cause);
    require(!g.pending_return, "nested-interrupt", pc);
    g.return_irq=(unsigned)irq;
    g.return_pc = pc; g.return_sp = m->cpu.x[2];
    g.saved_mstatus=m->cpu.mstatus;
    memcpy(g.saved_regs,m->cpu.x,sizeof g.saved_regs);
    memcpy(&g.timer_before,m->ram + (P_TIMER - CHG_RAM_BASE),4);
    if (irq==CHG_IRQ_SYSTICK) g.irqs++; else g.usb_irqs++;
    g.pending_return = g.pending_handler = true;
    event(m,irq==CHG_IRQ_SYSTICK ? "SysTick-interrupt" : "USBFS-interrupt");
    (void)tval;
}
void gate_mret(ChgMachine *m) {
    if (!m->cpu.trap_depth) {
        require(g.fixture || (g.pc == P_STARTUP_MRET && g.stage == 2),"unexpected-mret",g.pc);
        g.startup_returns++; event(m,"startup-mret");
    } else {
        require(g.pending_return && m->cpu.mepc == g.return_pc, "SysTick-return-target",g.pc);
        require(g.fixture || g.pc == (g.return_irq==CHG_IRQ_SYSTICK ?
                                     P_SYSTICK_MRET : P_USBFS_MRET),
                "interrupt-mret-site",g.pc);
        /* The next instruction hook checks the actual restored context. */
        if (g.return_irq==CHG_IRQ_SYSTICK) g.returns++; else g.usb_returns++;
    }
}
void gate_instruction(ChgMachine *m, uint32_t pc) {
    g.pc = pc;
    if (g.watchdog_enabled && m->cycles>=g.watchdog_deadline)
        longjmp(g.watchdog_escape,1);
    progress_deadlines(m);
    require(!(pc & 1) && (inside(pc,2,P_ENTRY,P_ENTRY+4) || inside(pc,2,P_TEXT,P_TEXT_END)),
            "execution-outside-code",pc);
    unsigned len = (m->flash[pc] & 3) == 3 ? 4 : 2;
    require(inside(pc,len,P_ENTRY,P_ENTRY+4) || inside(pc,len,P_TEXT,P_TEXT_END),
            "execution-crosses-code",pc);
    if (g.pending_handler) {
        require(m->cpu.trap_depth==1 && pc==(g.fixture ? 0x2300 :
                g.return_irq==CHG_IRQ_SYSTICK ? P_SYSTICK : P_USBFS),
                "interrupt-handler-target",pc);
        g.pending_handler=false;
        if (g.return_irq==CHG_IRQ_SYSTICK) g.handler_entries++; else g.usb_handler_entries++;
        event(m,g.return_irq==CHG_IRQ_SYSTICK ? "SysTick-handler-entry" : "USBFS-handler-entry");
    }
    if (g.pending_return && !m->cpu.trap_depth &&
        (g.return_irq==CHG_IRQ_SYSTICK ? g.returns==g.irqs : g.usb_returns==g.usb_irqs)) {
        require(pc == g.return_pc && m->cpu.x[2] == g.return_sp && m->cpu.mstatus==g.saved_mstatus &&
                !memcmp(m->cpu.x,g.saved_regs,sizeof g.saved_regs),"SysTick-context-return",pc);
        uint32_t timer; memcpy(&timer,m->ram + (P_TIMER - CHG_RAM_BASE),4);
        require(g.return_irq!=CHG_IRQ_SYSTICK || g.fixture || timer == g.timer_before +
                ((g.fault_active && g.fault_kind==6) ? 0u : 1u),
                "SysTick-timer-update",timer);
        g.pending_return = false;
        if (g.return_irq==CHG_IRQ_SYSTICK) {
            g.observed_returns++; g.last_systick_return=m->cycles;
            event(m,"SysTick-return-observed");
        } else {
            g.usb_observed_returns++; event(m,"USBFS-return-observed");
        }
        if (g.fault_active && g.fault_kind==1)
            longjmp(g.watchdog_escape,2); /* Pause before any foreground instruction. */
    }
    if (g.fault_active && g.fault_kind==6 && pc==P_SYSTICK_MRET) {
        memcpy(m->ram+(P_TIMER-CHG_RAM_BASE),&g.timer_before,4);
        g.fault_ticks_suppressed++;
        event(m,"injected-missing-SysTick-count");
    }
    if (g.fixture) return;
    if (g.usb_fault_kind && !g.usb_fault_injections && pc==P_HIDD_XFER_CB &&
        m->cpu.x[11]==0x81 && m->cpu.x[12]==0) {
        g.usb_fault_injections++;
        if (g.usb_fault_kind==1) {
            /* Synthetic lost class callback after TinyUSB has released EP1. */
            m->cpu.x[10]=1;
            m->cpu.pc=m->cpu.x[1];
            m->cpu.fetch_word=(m->cpu.pc>>2)-1;
            m->cpu.fetch_time=m->cycles;
            event(m,"injected-missing-HID-class-callback");
            longjmp(g.watchdog_escape,2);
        }
        /* Synthetic completion result failure, not a hardware-generated status. */
        m->cpu.x[12]=1;
        event(m,"injected-failed-HID-class-completion");
    }
    if (pc==P_EARLY_CRT) {
        require(g.stage==0 && g.watchdog_enabled && g.watchdog_feeds>=1,
                "CRT-before-IWDG-arm-and-feed",pc);
        require(m->cycles-g.watchdog_enable_cycle<watchdog_period(4000) &&
                m->cycles-g.watchdog_enable_cycle<watchdog_period(0xfff),
                "IWDG-first-feed-too-late",pc);
        event(m,"CRT-entered-after-IWDG-arm-and-feed");
    }
    if (g.fault_active && (g.fault_kind==4 || g.fault_kind==5) &&
        pc==P_WATCHDOG_FINISH) {
        uint8_t *progress=m->ram+(P_WATCHDOG_PROGRESS-CHG_RAM_BASE);
        *progress &= (uint8_t)~(g.fault_kind==4 ? 1u : 2u);
        g.fault_marks_removed++;
        event(m,g.fault_kind==4 ? "injected-missing-scan-mark" :
                                   "injected-missing-transport-mark");
    }
    require(!g.stage || inside(m->cpu.x[2]-1,1,P_STACK-P_STACK_SIZE,P_STACK),"stack-budget",m->cpu.x[2]);
    if (pc == P_RESET) { require(g.stage == 0,"reset-order",pc); event(m,"reset"); }
    if (pc == P_SYSTEM) {
        require(g.stage == 0,"SystemInit-order",pc);
        require(m->cpu.x[3] == P_GP && m->cpu.x[2] == P_STACK,"startup-gp-sp",pc);
        require(!memcmp(m->ram+(P_DATA-CHG_RAM_BASE),m->flash+P_DATA_LMA,P_DATA_END-P_DATA),
                "startup-data-copy",pc);
        for (uint32_t a=P_BSS;a<P_BSS_END;a++) require(!m->ram[a-CHG_RAM_BASE],"startup-bss",a);
        require(!memcmp(m->ram+(P_NOINIT-CHG_RAM_BASE),g.noinit,8),"startup-noinit",pc);
        g.stage=1; event(m,"SystemInit-data-bss-gp-sp-noinit-checked");
        if (g.watchdog_fault_mode && g.fault_kind==7 &&
            g.fault_injections<g.fault_limit) {
            g.fault_active=true; g.fault_injections++;
            event(m,"injected-pre-SystemInit-hang");
            longjmp(g.watchdog_escape,2); /* No SystemInit instruction executes. */
        }
    }
    if (pc == P_SYSTEM_RETURN) { require(g.stage==1,"SystemInit-return-order",pc); g.stage=2; event(m,"SystemInit-return"); }
    if (pc == P_MAIN) {
        require(g.stage==2 && g.startup_returns==1,"main-entry-order",pc);
        require(!memcmp(m->ram+(P_NOINIT-CHG_RAM_BASE),g.noinit,8),"main-noinit",pc);
        g.stage=3; event(m,"main");
    }
    if (pc == P_CONSUMED) {
        uint64_t request; memcpy(&request,m->ram+(P_NOINIT-CHG_RAM_BASE),8);
        require(g.stage==3 && !request && m->ram[P_RESCUE-CHG_RAM_BASE]==g.rescue,"rescue-consumption",pc);
        g.stage=4; event(m,"rescue-request-consumed");
    }
    if (pc == P_TRANSPORT_RETURN) {
        require(g.stage==4 && !g.rescue,"normal-transport-order",pc); g.stage=5; event(m,"transport-setup-return");
    }
    if (pc == P_LOOP) {
        require(!g.rescue && g.stage>=5 && g.scan_stage==0,"normal-loop-order",pc);
        g.stage=6; g.scan_stage=1; event(m,"normal-loop-head");
    }
    if (pc == P_KEYBOARD) {
        require(g.scan_stage==1 && (m->cpu.x[1]==P_KEYBOARD_RETURN ||
                (g.usb_connected && m->cpu.x[1]==P_KEYBOARD_CONNECTED_RETURN)),
                "keyboard-call-order",pc);
        g.keyboard_return_pc=m->cpu.x[1];
        g.scan_stage=2; g.rows=g.row_mask=0; event(m,"keyboard-task-enter");
    }
    if (pc == P_ROW_READ) {
        /* Exact BIN: row index a5 (x15); 0x614c reads GPIO,
         * 0x6164 bounds the index against six. */
        require(g.scan_stage==2 && m->cpu.x[15]==g.rows && g.rows<6,"matrix-row-order",pc);
        require((m->gpio[1].outdr&63)==(1u<<g.rows),"matrix-row-drive",pc);
        g.row_mask |= 1u << g.rows; g.rows++;
    }
    if (pc == P_SCAN_DONE) {
        require(g.scan_stage==2 && g.rows==6 && g.row_mask==63,"matrix-scan-order",pc);
        g.scan_stage=3; g.scans++; event(m,"six-matrix-rows-complete");
    }
    if (pc==g.keyboard_return_pc &&
        (pc==P_KEYBOARD_RETURN || (g.usb_connected && pc==P_KEYBOARD_CONNECTED_RETURN))) {
        require(g.scan_stage==3,"keyboard-return-order",pc);
        g.scan_stage=4; g.tasks++; event(m,"keyboard-task-return");
    }
    if (pc == P_LOOP_BACK) {
        require(g.scan_stage==4,"normal-loop-completion-order",pc); g.scan_stage=0; g.loops++;
        g.last_foreground = m->cycles;
        if (g.fault_active) g.fault_loops++;
        if (g.fault_active && g.watchdog_mutant==2) {
            g.watchdog_feeds++;
            g.watchdog_deadline=m->cycles+watchdog_period(g.watchdog_rld);
        }
        if (g.observed_returns>=3 && !g.pending_return) g.foreground_after_irq++;
        event(m,"normal-loop-complete");
    }
    if (pc == P_RESCUE_LOOP) {
        require(g.rescue && g.stage==4 && !g.scan_stage,"rescue-loop-order",pc); g.scan_stage=1; event(m,"rescue-transport-head");
    }
    if (pc == P_RESCUE_BACK) {
        require(g.rescue && g.scan_stage==1,"rescue-loop-completion-order",pc); g.scan_stage=0; g.rescue_loops++;
        g.last_foreground = m->cycles;
        if (g.fault_active) g.fault_loops++;
        if (g.observed_returns>=3 && !g.pending_return) g.foreground_after_irq++;
        event(m,"rescue-transport-complete");
    }
}
void gate_complete(void) {
    require(g.stage>=4 && g.handler_entries>=3 && g.observed_returns>=3 && g.foreground_after_irq>=1,
            "progress-timeout",g.pc);
    require(g.rescue ? (g.rescue_loops>=3 && !g.scans && !g.tasks && !g.loops) :
            (g.scans>=3 && g.tasks>=3 && g.loops>=3 && !g.rescue_loops),
            "foreground-progress-timeout",g.pc);
}
void gate_run(ChgMachine *m, uint64_t budget) {
    uint64_t end=m->cycles+budget;
    progress_deadlines(m);
    while (m->cycles<end) {
        if (g.watchdog_enabled && m->cycles>=g.watchdog_deadline) {
            gate_watchdog_expire(m);
            continue;
        }
        int escape=setjmp(g.watchdog_escape);
        if (escape) {
            if (escape==1) gate_watchdog_expire(m);
            continue; /* Foreground pause resumes at the next scheduling slice. */
        }
        require(!m->reset_request,"guest-reset-denied",m->cpu.pc);
        if (m->cycles>=m->next_event) bus_events(m);
        if (g.fault_active && (g.fault_kind==3 || g.fault_kind==7)) m->pfic.enabled=0;
        if (g.fault_active && (g.fault_kind==1 || g.fault_kind==3 || g.fault_kind==7) &&
            !m->cpu.trap_depth && !g.pending_return) m->cpu.wfi=true;
        cpu_take_interrupt(m); chg_schedule(m);
        if (g.watchdog_enabled && m->cycles>=g.watchdog_deadline) {
            gate_watchdog_expire(m);
            continue;
        }
        m->stop_at = m->next_event < end ? m->next_event : end;
        if (g.watchdog_enabled && m->stop_at>g.watchdog_deadline)
            m->stop_at=g.watchdog_deadline;
        if (!g.fixture && !g.fault_active) {
            uint64_t oldest = g.last_foreground < g.last_systick_return ?
                              g.last_foreground : g.last_systick_return;
            uint64_t deadline = oldest + GATE_PROGRESS_GAP + 1;
            if (m->stop_at > deadline) m->stop_at = deadline;
        }
        if (m->stop_at<=m->cycles) m->stop_at=m->cycles+1;
        if (m->cpu.wfi) m->cycles=m->stop_at; else cpu_exec(m);
        if (g.watchdog_enabled && m->cycles>=g.watchdog_deadline)
            gate_watchdog_expire(m);
        progress_deadlines(m);
    }
}
