#include "gate.h"
#include "profile.h"
#include <stdlib.h>
#include <string.h>

static ChgMachine machine;
static uint8_t original_flash[CHG_FLASH_SIZE];
static unsigned selected_usb_mutant;
static struct { uint64_t cycle; unsigned ep, len; uint8_t bytes[64]; } host_in_trace[128];
static unsigned host_in_count;
static void check(bool ok, const char *why) { if (!ok) gate_fail(why, g.pc); }
static void put32(uint32_t a, uint32_t v) { memcpy(machine.flash+a,&v,4); }
static void fixture_reset(void) {
    memset(&g,0,sizeof g); g.fixture=true;
    chg_init(&machine); machine.entry=P_TEXT; cpu_reset(&machine);
    gate_watchdog_reset();
}
static void step(void) {
    machine.stop_at=machine.cycles+1; cpu_exec(&machine);
}
static void rewind_pc(uint32_t pc) {
    machine.cpu.pc=pc;
    machine.cpu.fetch_word=(pc>>2)-1;
    machine.cpu.fetch_time=machine.cycles;
}
static void write_register(uint32_t a, uint32_t v) {
    int wait=0; bus_write_slow(&machine,a,v,4,&wait);
}
static uint32_t read_register(uint32_t a) {
    int wait=0; return bus_read_slow(&machine,a,4,&wait);
}
static bool iwdg_negative(const char *name, uint32_t address, int width,
                          bool write, uint32_t value, const char *expected) {
    fixture_reset();
    if (!setjmp(g.abort)) {
        gate_access(&machine,address,width,write,value);
        fprintf(stderr,"accepted IWDG %s\n",name); return false;
    }
    if (strcmp(g.failure,expected)) {
        fprintf(stderr,"IWDG %s: expected %s got %s\n",name,expected,g.failure);
        return false;
    }
    return true;
}
static bool iwdg_fixtures(void) {
    fixture_reset();
    if (setjmp(g.abort)) { fprintf(stderr,"IWDG positive: %s\n",g.failure); return false; }
    check(read_register(0x4000300c)==0,"IWDG-status-reset");
    write_register(0x40003000,0x5555);
    write_register(0x40003004,3);
    write_register(0x40003008,4000);
    check(read_register(0x4000300c)==3,"IWDG-update-flags");
    machine.cycles+=5;
    check(read_register(0x4000300c)==0 && read_register(0x40003004)==3 &&
          read_register(0x40003008)==4000,"IWDG-update-completion");
    write_register(0x40003000,0xaaaa);
    check(!g.watchdog_deadline,"IWDG-disabled-counter");
    machine.cycles+=48000;
    write_register(0x40003000,0xcccc);
    uint64_t deadline=g.watchdog_deadline;
    check(g.watchdog_enabled && deadline-machine.cycles==134217728ull,
          "IWDG-first-count-conservative-bound");
    machine.cycles+=48000;
    write_register(0x40003000,0xcccc);
    check(g.watchdog_deadline==deadline,"IWDG-enable-is-one-way");
    machine.cycles=deadline-1;
    check(g.watchdog_enabled && machine.cycles<g.watchdog_deadline,"IWDG-before-expiry");
    write_register(0x40003000,0xaaaa);
    check(g.watchdog_feeds==1 && g.watchdog_deadline==machine.cycles+131104768ull,
          "IWDG-feed-reloads-counter");
    for (unsigned p=6;p<=7;p++) {
        fixture_reset(); write_register(0x40003000,0x5555);
        write_register(0x40003004,p); machine.cycles+=5;
        write_register(0x40003000,0xcccc);
        check(g.watchdog_deadline-machine.cycles==4096ull*1024*256,
              "IWDG-prescale-256");
    }
    fixture_reset();
    write_register(0x40003000,0xcccc); /* Earliest reset-handler arm. */
    check(g.watchdog_enabled && g.watchdog_deadline-machine.cycles==16777216ull,
          "IWDG-reset-default-period");
    write_register(0x40003000,0x5555);
    write_register(0x40003004,3);
    write_register(0x40003008,4000);
    machine.cycles+=5;
    write_register(0x40003000,0xaaaa);
    check(g.watchdog_feeds==1 && g.watchdog_deadline-machine.cycles==131104768ull,
          "IWDG-early-arm-configured-feed");
    const struct { const char *name; uint32_t a,v; int width; bool write; const char *error; } bad[] = {
        {"width",0x40003000,0x5555,2,true,"register-width"},
        {"control-read",0x40003000,0,4,false,"register-direction"},
        {"status-write",0x4000300c,0,4,true,"register-direction"},
        {"key",0x40003000,0x1111,4,true,"IWDG-key"},
        {"locked-prescale",0x40003004,3,4,true,"IWDG-write-locked"},
        {"locked-reload",0x40003008,4000,4,true,"IWDG-write-locked"},
        {"unaligned",0x40003001,0,4,false,"unknown-memory-register"},
    };
    for (unsigned i=0;i<sizeof bad/sizeof *bad;i++)
        if (!iwdg_negative(bad[i].name,bad[i].a,bad[i].width,bad[i].write,
                           bad[i].v,bad[i].error)) return false;
    fixture_reset();
    write_register(0x40003000,0x5555);
    if (!setjmp(g.abort)) {
        gate_access(&machine,0x40003004,4,true,8);
        fprintf(stderr,"accepted IWDG prescale reserved bits\n"); return false;
    }
    if (strcmp(g.failure,"register-value-model")) return false;
    fixture_reset(); write_register(0x40003000,0x5555);
    if (!setjmp(g.abort)) {
        gate_access(&machine,0x40003008,4,true,0x1000);
        fprintf(stderr,"accepted IWDG reload reserved bits\n"); return false;
    }
    if (strcmp(g.failure,"register-value-model")) return false;
    fixture_reset(); write_register(0x40003000,0x5555);
    write_register(0x40003004,3);
    if (!setjmp(g.abort)) {
        gate_access(&machine,0x40003004,4,false,0);
        fprintf(stderr,"accepted IWDG busy read\n"); return false;
    }
    if (strcmp(g.failure,"IWDG-update-busy")) return false;
    fixture_reset(); write_register(0x40003000,0x5555);
    write_register(0x40003008,4000);
    if (!setjmp(g.abort)) {
        gate_access(&machine,0x40003008,4,true,4000);
        fprintf(stderr,"accepted IWDG busy write\n"); return false;
    }
    if (strcmp(g.failure,"IWDG-update-busy")) return false;
    fixture_reset();
    write_register(0x40003000,0x5555);
    write_register(0x40003004,3);
    write_register(0x40003008,4000);
    machine.cycles+=5;
    write_register(0x40003000,0xcccc);
    g.watchdog_fault_mode=true;
    machine.reset_flags=0x18000000u; /* Sticky PORRSTF | SFTRSTF seed. */
    uint8_t retained[8]; memcpy(retained,machine.ram+(P_NOINIT-CHG_RAM_BASE),8);
    machine.cycles=g.watchdog_deadline;
    gate_watchdog_expire(&machine);
    if (machine.reset_flags!=0x38000000u) {
        fprintf(stderr,"IWDG sticky reset flags: got %08x, expected 38000000\n",machine.reset_flags);
        return false;
    }
    gate_access(&machine,0x40021024,4,true,0x01000000u);
    write_register(0x40021024,0x01000000u); /* RMVF explicitly clears sticky causes. */
    return g.watchdog_resets==1 && !g.watchdog_enabled && !g.watchdog_deadline &&
           !machine.reset_flags && machine.cpu.pc==P_TEXT &&
           !memcmp(retained,machine.ram+(P_NOINIT-CHG_RAM_BASE),8);
}
static unsigned register_number(const char *name) {
    static const char *abi[]={"zero","ra","sp","gp","tp","t0","t1","t2","s0","s1",
        "a0","a1","a2","a3","a4","a5","a6","a7","s2","s3","s4","s5","s6","s7",
        "s8","s9","s10","s11","t3","t4","t5","t6"};
    for (unsigned i=0;i<32;i++) if (!strcmp(name,abi[i])) return i;
    gate_fail("unexpected-XW-register",0); return 0;
}
static unsigned xw_execution(const char *path) {
    FILE *f=fopen(path,"r"); if (!f) return 0;
    unsigned count=0, raw, offset; char line[256], mnemonic[16], reg[8], base[8];
    while (fgets(line,sizeof line,f)) {
        if (sscanf(line,"%x %15s %7[^,],%u(%7[^)])",&raw,mnemonic,reg,&offset,base)!=5) continue;
        fixture_reset();
        if (setjmp(g.abort)) { fprintf(stderr,"XW execution: %s %s",g.failure,line); fclose(f); return 0; }
        unsigned base_reg=register_number(base), data_reg=register_number(reg);
        check(base_reg!=data_reg,"overlapping-XW-fixture-registers");
        unsigned n=strchr(mnemonic,'h') ? 2 : 1;
        bool store=mnemonic[0]=='s';
        put32(P_TEXT,raw);
        /* Repeat without cache invalidation to exercise cached execution too. */
        for (unsigned pass=0;pass<2;pass++) {
            uint32_t value=pass ? 0x9876 : 0xfedc;
            uint32_t mask=n==1 ? 0xff : 0xffff;
            machine.cpu.x[base_reg]=CHG_RAM_BASE+0x100;
            machine.cpu.x[data_reg]=store ? value : 0;
            memset(machine.ram+0x100,0,128);
            if (!store) memcpy(machine.ram+0x100+offset,&value,n);
            rewind_pc(P_TEXT); step();
            uint32_t got=0;
            if (store) memcpy(&got,machine.ram+0x100+offset,n); else got=machine.cpu.x[data_reg];
            check(got==(value&mask) && machine.cpu.pc==P_TEXT+2,"XW-result");
        }
        count++;
    }
    fclose(f); return count;
}
static bool cpu_roundtrip(void) {
    fixture_reset();
    if (setjmp(g.abort)) { fprintf(stderr,"CPU roundtrip: %s\n",g.failure); return false; }
    const unsigned csr[]={0xbc0,0x804,0x300,0x305,0x341};
    const uint32_t values[]={31,3,0x88,0x2203,0x2140};
    for (unsigned i=0;i<5;i++) {
        machine.cpu.x[10+i]=values[i];
        put32(P_TEXT+4*i,(csr[i]<<20)|((10+i)<<15)|0x1073);
    }
    put32(P_TEXT+20,0x30200073); /* startup mret */
    put32(0x2140,0x0000006f); /* foreground loop */
    put32(0x2200+12*4,0x2300); /* PFIC vector fixture */
    put32(0x2300,0x07b00513); /* addi a0,zero,123, HPE must restore */
    put32(0x2304,0xe000f2b7); /* lui t0,0xe000f */
    put32(0x2308,0x0002a223); /* sw zero,4(t0), clear SysTick */
    put32(0x230c,0x30200073);
    write_register(0xe000e100,0x1000);
    write_register(0xe000f010,479);
    write_register(0xe000f000,15);
    gate_run(&machine,5000);
    check(g.handler_entries>=3 && g.irqs==g.observed_returns && !g.pending_return && g.startup_returns==1,
          "CPU-roundtrip-counts");
    check(machine.cpu.pc==0x2140 && machine.cpu.x[10]==31 && machine.cpu.intsyscr==3 &&
          machine.cpu.corecfgr==31 && machine.cpu.mtvec==0x2203,"CPU-roundtrip-state");
    return true;
}
typedef struct { const char *name; uint32_t instruction,address; const char *failure; bool cached; } Negative;
static const Negative negatives[]={
    {"wrong-family-read",0x00052583,0x40005c00,"unknown-memory-register",false},
    {"wrong-family-write",0x00b52023,0x40005c00,"unknown-memory-register",false},
    {"unknown-register-in-known-block",0x00052583,0x40021008,"unknown-memory-register",false},
    {"wrong-register-width",0x00052583,0x40023406,"register-width",false},
    {"wrong-register-direction",0x00b52023,0x40010808,"register-direction",false},
    {"legacy-PLL-enable",0x00b52023,0x40021000,"register-value-model",false},
    {"unknown-CSR",0x777515f3,0,"unsupported-CSR",false},
    {"unknown-CSR-cached",0x777515f3,0,"unsupported-CSR",true},
    {"illegal-instruction",0xffffffff,0,"unexpected-trap",false},
    {"illegal-fence",0xffffffff & ~0x70u,0,"unexpected-trap",true},
    {"ecall",0x00000073,0,"unexpected-trap",false},
    {"ebreak",0x00100073,0,"unexpected-trap",false},
    {"compressed-ebreak",0x00009002,0,"unexpected-trap",true},
    {"invalid-read",0x00052583,0x20005000,"unknown-memory-register",false},
    {"invalid-write-cached",0x00b52023,0x20005000,"unknown-memory-register",true},
    {"cross-RAM-end-cached",0x00052583,0x20004ffe,"unknown-memory-register",true},
    {"unknown-system-memory",0x00052583,0x1ffff700,"unknown-memory-register",false},
    {"flash-linked-write",0x00b52023,0x2000,"flash-write-denied",false},
    {"flash-physical-write",0x00b52023,0x08002000,"flash-write-denied",true},
    {"flash-unlock",0x00b52023,0x40022004,"flash-controller-denied",false},
    {"flash-program-erase",0x00b52023,0x40022010,"flash-controller-denied",false},
};
static bool negative(const Negative *n) {
    fixture_reset(); put32(P_TEXT,n->instruction);
    machine.cpu.x[10]=n->address; machine.cpu.x[11]=1u<<25;
    if (n->cached) cpu_decode(P_TEXT,n->instruction,&machine.dc_flash[P_TEXT/2],true);
    if (!setjmp(g.abort)) { step(); fprintf(stderr,"accepted %s\n",n->name); return false; }
    if (strcmp(g.failure,n->failure)) { fprintf(stderr,"%s: expected %s got %s\n",n->name,n->failure,g.failure); return false; }
    return true;
}
static bool execution_bounds(uint32_t pc) {
    fixture_reset(); machine.cpu.pc=pc;
    if (pc<CHG_FLASH_SIZE) cpu_decode(pc,0x0001,&machine.dc_flash[pc/2],true);
    if (pc==CHG_RAM_BASE) cpu_decode(pc,0x0001,&machine.dc_ram[0],false);
    if (!setjmp(g.abort)) { step(); fprintf(stderr,"accepted execution %08x\n",pc); return false; }
    if (strcmp(g.failure,"execution-outside-code")) {
        fprintf(stderr,"execution %08x failed with %s\n",pc,g.failure); return false;
    }
    return true;
}
static bool stalled(bool pll) {
    fixture_reset();
    if (setjmp(g.abort)) {
        bool ok=!strcmp(g.failure,"progress-timeout") && machine.cycles>=4800000;
        if (!ok) fprintf(stderr,"stall fixture %d: %s %08x at %08x\n",pll,g.failure,g.detail,g.pc);
        return ok;
    }
    machine.cpu.x[10]=0x40021000; machine.cpu.x[12]=1u<<25;
    if (pll) {
        put32(P_TEXT,0x00052583); /* lw a1,0(a0), CTLR */
        put32(P_TEXT+4,0x00c5f5b3); /* and a1,a1,a2, legacy PLLRDY */
        put32(P_TEXT+8,0xfe058ce3); /* beq a1,zero,-8 */
    } else put32(P_TEXT,0x0000006f);
    gate_run(&machine,4800000);
    /* Use the production obligation check, not an instruction-count oracle. */
    gate_complete();
    fprintf(stderr,"stall fixture unexpectedly passed\n"); return false;
}
static bool usb_semantics(void) {
    fixture_reset(); if (setjmp(g.abort)) return false;
    int wait=0; machine.usb[6]=0x17;
    bus_write_slow(&machine,0x40023406,0xff,1,&wait);
    check(bus_read_slow(&machine,0x40023406,1,&wait)==0x20,"USB-W1C");
    check(!machine.pfic.pending && !machine.pfic.line,"USB-no-completion");
    return true;
}
static int fixtures(const char *golden) {
    unsigned xw=xw_execution(golden);
    if (!xw || !cpu_roundtrip() || !usb_semantics() || !iwdg_fixtures()) { fprintf(stderr,"positive model fixture failed\n"); return 1; }
    for (unsigned i=0;i<sizeof negatives/sizeof *negatives;i++) if (!negative(&negatives[i])) return 1;
    const uint32_t pcs[]={0,0x2004,P_TEXT_END,CHG_RAM_BASE,0x08002100,0x2101};
    for (unsigned i=0;i<sizeof pcs/sizeof *pcs;i++) if (!execution_bounds(pcs[i])) return 1;
    if (!stalled(true) || !stalled(false)) return 1;
    printf("{\"passed\":true,\"xw_executed_uncached_and_cached\":%u,\"startup_csr_mret_systick_context\":true,\"usb_W1C_no_completion\":true,\"IWDG_register_controls\":true,\"negative_controls\":[",xw);
    for (unsigned i=0;i<sizeof negatives/sizeof *negatives;i++) printf("%s\"%s\"",i?",":"",negatives[i].name);
    printf("],\"execution_bounds_cases\":6,\"legacy_PLLRDY_wait\":\"progress-timeout at 100 ms\",\"forced_stall\":\"progress-timeout at 100 ms\"}\n");
    return 0;
}
static void prepare_case(unsigned kind) {
    memset(&g,0,sizeof g); g.rescue=kind==2;
    machine.cycles=machine.instret=0;
    chg_reset(&machine,false);
    gate_watchdog_reset();
    machine.reset_flags=kind==0 ? 0x08000000 : 0x10000000;
    if (!kind) for (unsigned i=0;i<CHG_RAM_SIZE;i++) machine.ram[i]=(uint8_t)(1+(i*73u)%255);
    if (kind>=2) {
        uint32_t request[]={0x43524d32,kind==2 ? ~0x43524d32u : 0};
        memcpy(machine.ram+(P_NOINIT-CHG_RAM_BASE),request,8);
    }
    memcpy(g.noinit,machine.ram+(P_NOINIT-CHG_RAM_BASE),8);
}
static bool late_stall(bool interrupt) {
    prepare_case(0);
    volatile uint64_t frozen=0;
    volatile unsigned returns=0, loops=0;
    if (setjmp(g.abort)) {
        const char *expected=interrupt ? "SysTick-progress-timeout" : "foreground-progress-timeout";
        bool ok=frozen && !strcmp(g.failure,expected) && machine.cycles-frozen<=GATE_PROGRESS_GAP+16 &&
                g.loops==loops && (interrupt ? (g.pending_return && g.observed_returns==returns) :
                                              (g.observed_returns>returns)) &&
                !memcmp(machine.flash,original_flash,sizeof original_flash) && !machine.flash_written;
        printf("{\"name\":\"%s\",\"passed\":%s,\"failure\":\"%s\",\"freeze_cycle\":%llu,\"rejection_cycle\":%llu}",
               interrupt ? "late-SysTick-stall" : "late-foreground-stall", ok?"true":"false",g.failure,
               (unsigned long long)frozen,(unsigned long long)machine.cycles);
        return ok;
    }
    gate_run(&machine,2400000);
    gate_complete(); /* Establish that cumulative early-progress checks pass. */
    while (g.pending_return != interrupt) gate_run(&machine,1);
    frozen=machine.cycles; returns=g.observed_returns; loops=g.loops;
    if (interrupt) {
        /* Host-only fault injection. Stop inside the handler without changing flash. */
        machine.cpu.wfi=true; machine.pfic.enabled=0;
        gate_run(&machine,4800000-machine.cycles);
    } else {
        while (machine.cycles<4800000) {
            /* Stop foreground work, but allow interrupt entry and validated return. */
            if (!machine.cpu.trap_depth && !g.pending_return) machine.cpu.wfi=true;
            gate_run(&machine,1);
        }
    }
    fprintf(stderr,"late stall unexpectedly passed\n"); return false;
}
static bool run_case(unsigned kind, const char *directory) {
    static const char *names[]={"cold-seeded","warm-retained","warm-valid-rescue","warm-invalid-single-word"};
    prepare_case(kind);
    char path[4096]; snprintf(path,sizeof path,"%s/%s.trace.jsonl",directory,names[kind]);
    g.trace=fopen(path,"w"); if (!g.trace) return false;
    if (!setjmp(g.abort)) {
        gate_run(&machine,4800000);
        gate_complete();
        check(!memcmp(machine.flash,original_flash,sizeof original_flash) && !machine.flash_written,"guest-flash-changed");
    }
    fclose(g.trace); g.trace=NULL;
    printf("%s{\"name\":\"%s\",\"passed\":%s,\"reset_flags\":%u,\"cycles\":%llu,\"stage\":%u,\"scans\":%u,\"task_returns\":%u,\"normal_loops\":%u,\"rescue_loops\":%u,\"SysTick_entries\":%u,\"SysTick_returns\":%u,\"foreground_after_irq\":%u,\"mmio_reads\":%u,\"mmio_writes\":%u,\"last_foreground_cycle\":%llu,\"last_SysTick_return_cycle\":%llu,\"failure\":\"%s\",\"pc\":%u,\"detail\":%u}",
           kind?",":"",names[kind],g.failure?"false":"true",machine.reset_flags,(unsigned long long)machine.cycles,
            g.stage,g.scans,g.tasks,g.loops,g.rescue_loops,g.handler_entries,g.observed_returns,g.foreground_after_irq,g.mmio_reads,g.mmio_writes,
            (unsigned long long)g.last_foreground,(unsigned long long)g.last_systick_return,
           g.failure?g.failure:"",g.pc,g.detail);
    if (g.failure) fprintf(stderr,"%s: %s at PC %08x detail %08x\n",names[kind],g.failure,g.pc,g.detail);
    return !g.failure;
}
static bool watchdog_case(unsigned kind) {
    static const char *names[]={"healthy-normal-over-timeout","healthy-rescue-over-timeout",
        "late-foreground-IWDG-reset","stuck-SysTick-IWDG-reset","post-arm-pre-CRT-IWDG-reset",
        "missing-scan-mark-IWDG-reset","missing-transport-mark-IWDG-reset",
        "rescue-missing-transport-mark-IWDG-reset","repeated-pre-CRT-reset-loop",
        "missing-SysTick-count-IWDG-reset","pre-SystemInit-IWDG-reset",
        "pre-SystemInit-valid-rescue-IWDG-reset",
        "post-enable-STATR-stuck-IWDG-reset"};
    prepare_case(kind==1 || kind==7 || kind==11 ? 2 : 0);
    g.watchdog_fault_mode=true;
    volatile bool passed=false;
    if (!setjmp(g.abort)) {
        if (kind<=1) {
            gate_run(&machine,131104768ull+4800000ull);
            gate_complete();
            check(g.watchdog_enabled && !g.watchdog_resets && g.watchdog_feeds>=1000 &&
                  machine.cycles>g.watchdog_enable_cycle+131104768ull,
                  "healthy-IWDG-feed-or-duration");
        } else {
            g.fault_kind=kind==2 ? 1 : kind==3 ? 2 : kind==4 || kind==8 ? 3 :
                         kind==5 ? 4 : kind==9 ? 6 : kind==12 ? 8 : kind>=10 ? 7 : 5;
            g.fault_limit=kind==8 ? 2 : 1;
            if (g.fault_kind==8) {
                gate_run(&machine,480000);
                check(g.fault_active && g.watchdog_enabled && g.stage==0 &&
                      !g.loops && !g.rescue_loops,
                      "post-enable-STATR-fault-not-injected");
            } else if (g.fault_kind==7) {
                gate_run(&machine,480000);
                check(g.fault_active && g.stage==1 && !g.loops && !g.rescue_loops,
                      "pre-SystemInit-fault-not-injected");
            } else if (g.fault_kind==3) {
                gate_run(&machine,4800000);
                check(g.fault_active && g.watchdog_enabled && g.stage==0 &&
                      !g.loops && !g.rescue_loops,"init-hang-after-arm");
            } else {
                gate_run(&machine,2400000);
                gate_complete();
                check(g.watchdog_enabled && g.watchdog_feeds>0,"fault-prior-health");
                if (g.fault_kind==2) {
                    while (!g.pending_return) gate_run(&machine,1);
                    machine.cpu.wfi=true;
                    machine.pfic.enabled=0;
                }
                g.fault_active=true;
            }
            const uint64_t first_deadline=g.watchdog_enabled ? g.watchdog_deadline :
                                          machine.cycles+131104768ull;
            const unsigned prior_loops=g.loops+g.rescue_loops;
            const unsigned prior_feeds=g.watchdog_feeds;
            gate_run(&machine,first_deadline-machine.cycles+4800000);
            check(g.watchdog_resets==1,kind>=10 ? "pre-main-IWDG-reset-missing" :
                                                 "first-IWDG-reset-missing");
            if (kind==8) {
                check(g.fault_active && g.fault_injections==2,"repeat-fault-not-injected");
                const uint64_t second_deadline=g.watchdog_deadline;
                gate_run(&machine,second_deadline-machine.cycles+4800000);
                check(g.watchdog_resets==2 && g.stage>=4 && !g.fault_active,
                      "reset-loop-not-recorded");
            } else {
                check(g.stage>=4 && g.startup_returns==1 && !g.fault_active &&
                      g.loops+g.rescue_loops>prior_loops && g.watchdog_feeds>prior_feeds,
                      "IWDG-reset-startup-recovery");
                if (kind==7) check(!g.rescue && g.loops>0,"rescue-reset-must-reenter-normal");
                if (kind==11) {
                    uint32_t request[2];
                    memcpy(request,machine.ram+(P_NOINIT-CHG_RAM_BASE),sizeof request);
                    check(g.rescue && g.rescue_loops>0 && !request[0] && !request[1],
                          "pre-main-rescue-request-lost");
                }
                if (kind==3) check(g.observed_returns>=3,"ISR-observer-not-reinitialized");
                if (kind==5 || kind==6 || kind==7)
                    check(g.fault_marks_removed>0 && g.fault_loops>0,
                          "busy-loop-progress-fault-not-observed");
                if (kind==9) check(g.fault_ticks_suppressed>0 && g.fault_loops>0,
                                   "missing-tick-fault-not-observed");
            }
            check(!memcmp(machine.flash,original_flash,sizeof original_flash) &&
                  !machine.flash_written,"guest-flash-changed");
        }
        passed=true;
    }
    printf("%s{\"name\":\"%s\",\"passed\":%s,\"cycles\":%llu,\"IWDG_feeds\":%u,\"IWDG_resets\":%u,\"fault_loops\":%u,\"fault_marks_removed\":%u,\"fault_ticks_suppressed\":%u,\"startup_returns\":%u,\"normal_loops\":%u,\"rescue_loops\":%u,\"failure\":\"%s\"}",
           kind?",":"",names[kind],passed?"true":"false",(unsigned long long)machine.cycles,
           g.watchdog_feeds,g.watchdog_resets,g.fault_loops,g.fault_marks_removed,g.fault_ticks_suppressed,
           g.startup_returns,g.loops,g.rescue_loops,g.failure?g.failure:"");
    if (!passed) fprintf(stderr,"%s: %s at PC %08x detail %08x\n",names[kind],
                         g.failure?g.failure:"case invariant failed",g.pc,g.detail);
    return passed;
}
static bool watchdog_mutant_controls(void) {
    prepare_case(0);
    g.watchdog_mutant=1;
    bool ignored_feed_rejected=false;
    if (setjmp(g.abort)) {
        ignored_feed_rejected=!strcmp(g.failure,"CRT-before-IWDG-arm-and-feed") &&
                              g.watchdog_enabled && !g.watchdog_feeds &&
                              machine.cycles<g.watchdog_deadline;
    } else gate_run(&machine,131104768ull+4800000ull);

    prepare_case(0);
    g.watchdog_mutant=2; g.watchdog_fault_mode=true; g.fault_kind=4;
    bool unconditional_feed_rejected=false;
    if (setjmp(g.abort)) {
        unconditional_feed_rejected=!strcmp(g.failure,"first-IWDG-reset-missing") &&
                                    g.fault_marks_removed>0 && g.fault_loops>0 &&
                                    !g.watchdog_resets;
    } else {
        gate_run(&machine,2400000);
        gate_complete();
        g.fault_active=true;
        uint64_t deadline=g.watchdog_deadline;
        gate_run(&machine,deadline-machine.cycles+4800000);
        check(g.watchdog_resets==1,"first-IWDG-reset-missing");
    }
    bool passed=ignored_feed_rejected && unconditional_feed_rejected;
    printf("{\"passed\":%s,\"ignored_feed_rejected\":%s,\"unconditional_feed_rejected\":%s}\n",
           passed?"true":"false",ignored_feed_rejected?"true":"false",
           unconditional_feed_rejected?"true":"false");
    return passed;
}
static uint32_t usb_dma(unsigned ep) {
    uint32_t base=0;
    check(ep<=7,"host-USB-endpoint-number");
    memcpy(&base,machine.usb+(ep==4 ? 0x10 : ep<4 ? 0x10+4*ep : 0x54+4*(ep-5)),4);
    check(base>=CHG_RAM_BASE && base+64<=CHG_RAM_BASE+CHG_RAM_SIZE,"host-USB-DMA");
    return base;
}
static void host_raise_irq(unsigned flag, unsigned status, unsigned rx_len) {
    check(g.usb_host_attached,"host-USB-token-while-detached");
    check(flag==1 || flag==2 || flag==4,"host-USB-event-unsupported");
    if (flag==2) {
        unsigned token=(status>>4)&3;
        check(token==0 || token==2 || token==3,"host-USB-token-unsupported");
        check((status&15)<=7 && rx_len<=64,"host-USB-packet-oversize");
        check(token==0 || token==3 || rx_len==0,"host-IN-RX-length");
        check(token!=3 || ((status&15)==0 && rx_len==8),"host-SETUP-shape");
    }
    check((machine.usb[6]&0x1f)==0 && !(machine.pfic.line&(1ull<<45)),
          "host-USB-pending-IRQ");
    check((machine.usb[0]&0x39)==0x39 && (machine.usb[1]&0x81)==0x81 &&
          (machine.usb[2]&flag),"host-USB-controller-disabled");
    check(machine.rcc[5]&0x1000,"host-USB-clock-disabled");
    check((machine.rcc[6]&0x11)==0x11,"host-USB-port-clocks-disabled");
    check(machine.afio_ctlr&0x40,"host-USB-PHY-disabled");
    check(machine.afio_ctlr&0x80,"host-USB-IO-disabled");
    check((machine.afio_ctlr&0x0f)==0x0c,"host-USB-pullup-disabled");
    check((machine.gpio[2].cfg[2]&0xff)==0x84 &&
          (machine.gpio[2].outdr&(1u<<17)),"host-USB-pin-mode");
    machine.usb[7]=(uint8_t)status;
    machine.usb[8]=(uint8_t)rx_len;
    machine.usb[9]=(uint8_t)(rx_len>>8);
    machine.usb[6]|=(uint8_t)flag;
    if (g.usb_model_mutant!=3) pfic_set_line(&machine,45,true);
}
static void host_irq(unsigned flag, unsigned status, unsigned rx_len) {
    unsigned before=g.usb_observed_returns;
    host_raise_irq(flag,status,rx_len);
    gate_run(&machine,480000);
    check(g.usb_observed_returns==before+1 && !(machine.usb[6]&flag) &&
          !(machine.pfic.line&(1ull<<45)),"host-USB-IRQ-return");
}
static void host_setup(const uint8_t setup[8]) {
    /* SETUP starts a new control transfer even when EP0 OUT data is NAK. */
    memcpy(machine.ram+(usb_dma(0)-CHG_RAM_BASE),setup,8);
    host_irq(2,0x30,8);
}
static unsigned host_in(unsigned ep, uint8_t packet[64]) {
    check(ep<=1 && (ep==0 || (machine.usb[0x0c]&0x40)),
          "host-IN-endpoint-disabled");
    unsigned off=ep<5 ? 0x20+4*ep : 0x64+4*(ep-5);
    check((machine.usb[off+2]&3)==0,"host-IN-not-ACK");
    unsigned len=machine.usb[off] | ((unsigned)machine.usb[off+1]<<8);
    check(len<=64,"host-IN-packet-oversize");
    memcpy(packet,machine.ram+(usb_dma(ep)-CHG_RAM_BASE)+(ep?64:0),len);
    check(host_in_count<sizeof host_in_trace/sizeof *host_in_trace,"host-IN-trace-overflow");
    host_in_trace[host_in_count].cycle=machine.cycles;
    host_in_trace[host_in_count].ep=ep;
    host_in_trace[host_in_count].len=len;
    memcpy(host_in_trace[host_in_count].bytes,packet,len);
    host_in_count++;
    host_irq(2,0x20|ep,0);
    return len;
}
static void host_out0(const uint8_t *packet, unsigned len) {
    check(len<=64 && ((machine.usb[0x22]>>2)&3)==0,"host-OUT-not-ACK");
    if (len) memcpy(machine.ram+(usb_dma(0)-CHG_RAM_BASE),packet,len);
    host_irq(2,0x40,len);
}
static void host_out1(uint8_t value, bool toggle_ok) {
    check(machine.usb[0x0c]&0x80,"host-OUT-endpoint-disabled");
    check(((machine.usb[0x26]>>2)&3)==0,"host-EP1-OUT-not-ACK");
    machine.ram[usb_dma(1)-CHG_RAM_BASE]=value;
    host_irq(2,1u | (toggle_ok ? 0x40u : 0u),1);
}
static unsigned host_control_read(uint8_t type, uint8_t request, uint16_t value,
                                  uint16_t index, uint16_t wanted, uint8_t data[256]) {
    const uint8_t setup[]={type,request,(uint8_t)value,(uint8_t)(value>>8),
                           (uint8_t)index,(uint8_t)(index>>8),
                           (uint8_t)wanted,(uint8_t)(wanted>>8)};
    check(wanted<=256,"host-control-read-length");
    host_setup(setup);
    unsigned total=0, len;
    do {
        uint8_t packet[64];
        len=host_in(0,packet);
        check(total+len<=wanted,"host-control-overrun");
        memcpy(data+total,packet,len);
        total+=len;
    } while (len==64 && total<wanted);
    host_out0(NULL,0);
    return total;
}
static void host_control_no_data(uint8_t type, uint8_t request, uint16_t value,
                                 uint16_t index) {
    const uint8_t setup[]={type,request,(uint8_t)value,(uint8_t)(value>>8),
                           (uint8_t)index,(uint8_t)(index>>8),0,0};
    uint8_t status[64];
    host_setup(setup);
    check(host_in(0,status)==0,"host-control-status-not-ZLP");
}
static void host_hid_report(unsigned size, uint8_t key, uint16_t consumer) {
    uint8_t packet[64]={0};
    check(host_in(1,packet)==size,"host-HID-report-length");
    for (unsigned i=0;i<size;i++) {
        uint8_t expected=i==2 ? key : i==8 ? (uint8_t)consumer :
                         i==9 ? (uint8_t)(consumer>>8) : 0;
        check(packet[i]==expected,"host-HID-report-bytes");
    }
}
static void host_enumerate(void) {
    unsigned before=g.usb_irqs;
    host_irq(1,0,0); /* Host full-speed bus reset. */
    check(g.usb_irqs==before+1 && g.usb_handler_entries==before+1 &&
          g.usb_returns==before+1 && g.usb_observed_returns==before+1 &&
          !(machine.usb[6]&1) && !(machine.pfic.line & (1ull<<45)) &&
          machine.usb[3]==0,"USB-bus-reset-IRQ45");
    check(g.scans>=3 && g.observed_returns>=3,
          "USB-reset-foreground-progress");
    uint8_t desc[256];
    unsigned len=host_control_read(0x80,6,0x0100,0,18,desc);
    check(len==18 && desc[0]==18 && desc[1]==1 && desc[7]==64 &&
          desc[8]==0x2e && desc[9]==0x35 && desc[10]==0x06 && desc[11]==0x23,
          "USB-device-descriptor");
    const uint8_t set_address[]={0,5,5,0,0,0,0,0};
    host_setup(set_address);
    check(machine.usb[3]==0,"USB-address-before-status");
    uint8_t packet[64];
    check(host_in(0,packet)==0 && machine.usb[3]==5,"USB-address-after-status");
    len=host_control_read(0x80,6,0x0200,0,96,desc);
    check(len==96 && desc[0]==9 && desc[1]==2 && desc[2]==96 &&
          desc[4]==3 && desc[20]==0x11 && desc[27]==7 && desc[29]==0x81,
          "USB-configuration-descriptor");
    len=host_control_read(0x80,6,0x0300,0,4,desc);
    check(len==4 && desc[0]==4 && desc[1]==3 && desc[2]==9 && desc[3]==4,
          "USB-language-descriptor");
    len=host_control_read(0x80,6,0x0303,0x0409,22,desc);
    check(len==22 && desc[0]==22 && desc[1]==3 && desc[2]=='0' && desc[3]==0 &&
          desc[20]=='6' && desc[21]==0,"USB-serial-descriptor");
    static const struct { uint8_t index; const char *text; } strings[]={
        {1,"Clicks Technology Limited"}, {2,"Clicks Creator Keyboard"},
        {4,"iAP2 Interface"}, {5,"com.clickscompanion.protocol"}
    };
    for (unsigned i=0;i<sizeof strings/sizeof *strings;i++) {
        len=host_control_read(0x80,6,0x0300|strings[i].index,0x0409,255,desc);
        size_t characters=strlen(strings[i].text);
        check(len==2+2*characters && desc[0]==len && desc[1]==3,
              "USB-string-descriptor-length");
        for (size_t j=0;j<characters;j++)
            check(desc[2+2*j]==(uint8_t)strings[i].text[j] && !desc[3+2*j],
                  "USB-string-descriptor-bytes");
    }
    host_control_no_data(0,9,1,0);
    /* usbd_device_t starts with a 20-byte control state; cfg_num is at +24. */
    check(machine.usb[3]==5 && machine.ram[P_USBD_DEV-CHG_RAM_BASE+24]==1,
          "USB-configuration-mount");
    len=host_control_read(0x81,6,0x2200,2,255,desc);
    check(len>64 && desc[0]==5 && desc[1]==1 && desc[len-1]==0xc0,
          "USB-HID-report-descriptor");
}
static void fixture_usb_ready(void) {
    machine.rcc[5]=0x1000; /* RCC_AHBPCENR.USBFS. */
    machine.rcc[6]=0x11; /* RCC_APB2PCENR.AFIO and GPIOC. */
    machine.afio_ctlr=0xcc; /* PHY_V33, USB_IOEN, D+ pull-up. */
    machine.gpio[2].cfg[2]=0x84; /* PC16 floating input, PC17 pull-up input. */
    machine.gpio[2].outdr=1u<<17;
    machine.usb[0]=0x39; machine.usb[1]=0x81; machine.usb[2]=7;
}
static bool usb_negative(unsigned id, const char *expected) {
    fixture_reset();
    g.usb_connected=g.usb_host_attached=true;
    fixture_usb_ready();
    if (!setjmp(g.abort)) {
        uint8_t packet[64];
        switch (id) {
        case 0: gate_access(&machine,0x40023406,4,false,0); break;
        case 1: gate_access(&machine,0x40023407,1,true,1); break;
        case 2: write_register(0x40023410,0x20005000); break;
        case 3:
            write_register(0x40023410,0x20000000);
            write_register(0x40023414,0x20000080);
            break;
        case 4: gate_access(&machine,0x40023414,4,true,0x20000001); break;
        case 5:
            machine.usb[0x0c]=0x40; machine.usb[0x26]=0;
            machine.usb[0x24]=65; (void)host_in(1,packet);
            break;
        case 6:
            machine.usb[0x26]=0; machine.usb[0x24]=10;
            (void)host_in(1,packet);
            break;
        case 7:
            machine.usb[0x0c]=0x40; machine.usb[0x26]=2;
            (void)host_in(1,packet);
            break;
        case 8:
            machine.usb[6]=2; pfic_set_line(&machine,45,true);
            host_irq(2,0x20,0);
            break;
        case 9: host_irq(2,0x10,0); break;
        case 10: host_irq(2,0x40,65); break;
        case 11: machine.rcc[5]=0; host_raise_irq(1,0,0); break;
        case 12: machine.afio_ctlr &= ~0x40u; host_raise_irq(1,0,0); break;
        case 13: machine.afio_ctlr &= ~0x80u; host_raise_irq(1,0,0); break;
        case 14: machine.afio_ctlr &= ~0x0cu; host_raise_irq(1,0,0); break;
        case 15: machine.rcc[6]=0; host_raise_irq(1,0,0); break;
        case 16: machine.gpio[2].cfg[2]=0; host_raise_irq(1,0,0); break;
        case 17:
            machine.rcc[5]=0;
            uint32_t value=0; (void)gate_bus(&machine,0x40023400,1,false,&value);
            break;
        case 18: machine.usb[0]&=(uint8_t)~0x20u; host_raise_irq(1,0,0); break;
        case 19: host_raise_irq(2,0x30,7); break;
        case 20: host_raise_irq(2,0x31,8); break;
        default: return false;
        }
        fprintf(stderr,"USB negative %u was accepted\n",id);
        return false;
    }
    if (strcmp(g.failure,expected)) {
        fprintf(stderr,"USB negative %u: expected %s got %s\n",id,expected,g.failure);
        return false;
    }
    return true;
}
static bool usb_row_drive_fixture(void) {
    fixture_reset();
    g.usb_connected=true;
    g.keys[0]=1;
    machine.rcc[6]=0x0c; /* RCC_APB2PCENR.GPIOA/B. */
    machine.gpio[0].cfg[0]=8; /* PA0 input pull-down. */
    machine.gpio[1].outdr=1; /* PB0 latch high, pin still analog/input. */
    if (setjmp(g.abort)) {
        fprintf(stderr,"USB row drive fixture: %s\n",g.failure);
        return false;
    }
    uint32_t pins=0;
    check(gate_bus(&machine,0x40010808,4,false,&pins) && !(pins&1),
          "USB-row-latch-without-output-drive");
    machine.gpio[1].cfg[0]=3; /* PB0 push-pull output, physically high. */
    for (unsigned clocks=0;clocks<3;clocks++) {
        machine.rcc[6]=clocks==0 ? 0 : clocks==1 ? 0x04 : 0x08;
        check(gate_bus(&machine,0x40010808,4,false,&pins) && !(pins&1),
              "USB-row-without-GPIO-clocks");
    }
    machine.rcc[6]=0x0c;
    check(gate_bus(&machine,0x40010808,4,false,&pins) && (pins&1),
          "USB-row-output-drive-missing");
    machine.gpio[1].ext_low=1; /* External fault pulls PB0 low. */
    check(gate_bus(&machine,0x40010808,4,false,&pins) && !(pins&1),
          "USB-row-electrical-low-observed-as-press");
    return true;
}
static bool usb_fixtures(void) {
    static const char *expected[]={"register-width","register-direction",
        "USB-DMA-range","USB-DMA-overlap","register-value-model",
        "host-IN-packet-oversize","host-IN-endpoint-disabled",
        "host-IN-not-ACK","host-USB-pending-IRQ",
        "host-USB-token-unsupported","host-USB-packet-oversize",
        "host-USB-clock-disabled","host-USB-PHY-disabled",
        "host-USB-IO-disabled","host-USB-pullup-disabled",
        "host-USB-port-clocks-disabled","host-USB-pin-mode",
        "USB-clock-disabled","host-USB-controller-disabled",
        "host-SETUP-shape","host-SETUP-shape"};
    bool passed=true;
    for (unsigned i=0;i<sizeof expected/sizeof *expected;i++)
        if (!usb_negative(i,expected[i])) passed=false;
    bool row_ok=usb_row_drive_fixture();
    if (!row_ok) passed=false;
    printf("{\"passed\":%s,\"negative_controls\":%zu,\"row_drive_control\":%s}\n",
           passed?"true":"false",sizeof expected/sizeof *expected,
           row_ok?"true":"false");
    return passed;
}
static bool connected_reset_case(unsigned scenario, bool emit) {
    prepare_case(0);
    host_in_count=0;
    g.usb_connected=true;
    g.usb_host_attached=true;
    g.usb_model_mutant=selected_usb_mutant;
    volatile bool passed=false;
    if (!setjmp(g.abort)) {
        gate_run(&machine,4800000);
        gate_complete();
        check((machine.usb[0] & 0x39)==0x39 && (machine.usb[1] & 0x81)==0x81 &&
              (machine.usb[2] & 3)==3 &&
              (machine.pfic.enabled & (1ull<<45)),"USB-controller-not-ready");
        check(!g.usb_irqs && !g.usb_observed_returns && !g.watchdog_resets,
              "USB-interrupt-without-host");
        host_enumerate();
        uint8_t packet[64];
        unsigned len;
        check(machine.ram[P_KEYBOARD_LEDS-CHG_RAM_BASE]==0,"USB-LED-initial");
        host_out1(3,false); /* Repeated DATA PID: TOG_OK=0, DCD must drop it. */
        check(machine.ram[P_KEYBOARD_LEDS-CHG_RAM_BASE]==0,
              "USB-duplicate-OUT-rejected");
        host_out1(3,true);
        check(machine.ram[P_KEYBOARD_LEDS-CHG_RAM_BASE]==3,
              "USB-accepted-OUT-updates-LEDs");
        if ((machine.usb[0x26]&3)==0) {
            len=host_in(1,packet);
            check(len==10 && !packet[2] && !packet[8] && !packet[9],
                  "USB-initial-idle-report");
        }
        check((machine.usb[0x26]&3)==2,"USB-idle-IN-NAK");
        unsigned before_nak=g.usb_observed_returns;
        gate_run(&machine,480000);
        check((machine.usb[0x26]&3)==2 && g.usb_observed_returns==before_nak,
              "USB-NAK-no-fabricated-completion");
        g.keys[0]=1; /* PB0 high connects PA0: L0(0,0) = KC_Q. */
        gate_run(&machine,960000); /* More than the QMK 5 ms debounce interval. */
        check((machine.usb[0x26]&3)==0,"USB-Q-IN-armed");
        uint8_t pending_q[10];
        memcpy(pending_q,machine.ram+(usb_dma(1)-CHG_RAM_BASE)+64,10);
        unsigned before_delayed_ack=g.usb_observed_returns;
        gate_run(&machine,1440000); /* Host withholds ACK for 30 ms. */
        check((machine.usb[0x26]&3)==0 && g.usb_observed_returns==before_delayed_ack &&
              !memcmp(pending_q,machine.ram+(usb_dma(1)-CHG_RAM_BASE)+64,10),
              "USB-delayed-ACK-no-fabricated-completion");
        len=host_in(1,packet);
        check(len==10 && packet[0]==0 && packet[1]==0 && packet[2]==0x14 &&
              packet[3]==0 && packet[8]==0 && packet[9]==0,
              "USB-Q-press-report");
        g.keys[0]=0;
        gate_run(&machine,960000);
        len=host_in(1,packet);
        check(len==10 && packet[0]==0 && packet[2]==0 &&
              packet[8]==0 && packet[9]==0,"USB-Q-release-report");
        g.keys[0]=1u<<5; /* CK_CONS_A: consumer usage 0x00CF. */
        gate_run(&machine,960000);
        host_hid_report(10,0,0x00cf);
        g.keys[0]|=1u; /* Q and consumer in one 10-byte report. */
        gate_run(&machine,960000);
        host_hid_report(10,0x14,0x00cf);
        g.keys[0]&=(uint8_t)~1u;
        gate_run(&machine,960000);
        host_hid_report(10,0,0x00cf);
        g.keys[0]=0;
        gate_run(&machine,960000);
        host_hid_report(10,0,0);
        g.keys[5]=1u<<5; /* CK_IOS_KB: consumer Eject 0x00B8. */
        gate_run(&machine,960000);
        host_hid_report(10,0,0x00b8);
        g.keys[5]=0;
        gate_run(&machine,960000);
        host_hid_report(10,0,0);
        host_control_no_data(0x21,11,0,2); /* HID SET_PROTOCOL boot. */
        check(machine.ram[P_KEYBOARD_PROTOCOL-CHG_RAM_BASE]==0,"USB-boot-protocol-state");
        g.keys[0]=1;
        gate_run(&machine,960000);
        host_hid_report(8,0x14,0);
        g.keys[0]=0;
        gate_run(&machine,960000);
        host_hid_report(8,0,0);
        host_control_no_data(0x21,11,1,2); /* Restore report protocol. */
        check(machine.ram[P_KEYBOARD_PROTOCOL-CHG_RAM_BASE]==1,"USB-report-protocol-state");
        g.keys[0]=1u<<5;
        gate_run(&machine,960000);
        host_hid_report(10,0,0x00cf);
        g.keys[0]=0;
        gate_run(&machine,960000);
        host_hid_report(10,0,0);
        if (scenario==2 || scenario==3) {
            g.usb_fault_kind=scenario==2 ? 1u : 2u;
            g.keys[0]=1;
            gate_run(&machine,960000);
            host_hid_report(10,0x14,0);
            check(g.usb_fault_injections==1 &&
                  machine.ram[P_HID_COUNT-CHG_RAM_BASE]==1 &&
                  (scenario!=2 || machine.ram[P_KB_INFLIGHT-CHG_RAM_BASE]==1),
                  "USB-synthetic-completion-fault-state");
            g.keys[5]=1u<<5; /* Eject tap while Q completion is unresolved. */
            gate_run(&machine,960000);
            g.keys[5]=0;
            gate_run(&machine,960000);
            check(machine.ram[P_HID_COUNT-CHG_RAM_BASE]==3,
                  "USB-pending-consumer-edges-retained");
            gate_run(&machine,1440000); /* Missing callback grace or failed-result retry. */
            host_hid_report(10,0x14,0);
            host_hid_report(10,0x14,0x00b8);
            host_hid_report(10,0x14,0);
            check(machine.ram[P_HID_COUNT-CHG_RAM_BASE]==0 &&
                  machine.ram[P_KB_INFLIGHT-CHG_RAM_BASE]==0,
                  "USB-completion-retry-and-consumer-order");
            g.keys[0]=0;
            gate_run(&machine,960000);
            host_hid_report(10,0,0);
            gate_run(&machine,960000);
            check((machine.usb[0x26]&3)==2 &&
                  machine.ram[P_HID_COUNT-CHG_RAM_BASE]==0,
                  "USB-no-completed-press-on-idle");
        }
        if (scenario==4) {
            check((machine.usb[0x26]&3)==2,"USB-offline-start-idle");
            unsigned irq_before=g.usb_observed_returns;
            unsigned feeds_before=g.watchdog_feeds;
            g.usb_host_attached=false; /* Host detach: no tokens, no invented DCD callback. */
            g.keys[0]=1;
            gate_run(&machine,960000);
            g.keys[0]=0;
            gate_run(&machine,960000);
            g.keys[5]=1u<<5;
            gate_run(&machine,960000);
            g.keys[5]=0;
            gate_run(&machine,960000);
            check(g.usb_observed_returns==irq_before && g.watchdog_feeds>feeds_before &&
                  (machine.usb[0x26]&3)==0,
                  "USB-offline-typing-no-host-completion");
            g.usb_host_attached=true;
            host_enumerate(); /* Actual host bus reset and full control sequence. */
            if ((machine.usb[0x26]&3)==0) host_hid_report(10,0,0);
            gate_run(&machine,960000);
            check((machine.usb[0x26]&3)==2,"USB-offline-press-replayed");
            g.keys[0]=1;
            gate_run(&machine,960000);
            host_hid_report(10,0x14,0);
            g.keys[0]=0;
            gate_run(&machine,960000);
            host_hid_report(10,0,0);
        }
        if (scenario==5) {
            unsigned feeds_before=g.watchdog_feeds, scans_before=g.scans;
            unsigned usb_before=g.usb_observed_returns;
            g.keys[0]=1;
            gate_run(&machine,960000);
            check((machine.usb[0x26]&3)==0,"USB-long-backpressure-not-armed");
            uint8_t pending[10];
            memcpy(pending,machine.ram+(usb_dma(1)-CHG_RAM_BASE)+64,10);
            uint64_t deadline=g.watchdog_deadline;
            gate_run(&machine,131104768ull+4800000ull);
            check(machine.cycles>deadline && !g.watchdog_resets &&
                  g.watchdog_feeds>feeds_before+1000 && g.scans>scans_before+1000 &&
                  g.usb_observed_returns==usb_before && (machine.usb[0x26]&3)==0 &&
                  !memcmp(pending,machine.ram+(usb_dma(1)-CHG_RAM_BASE)+64,10),
                  "USB-long-backpressure-watchdog-progress");
            host_hid_report(10,0x14,0);
            g.keys[0]=0;
            gate_run(&machine,960000);
            host_hid_report(10,0,0);
        }
        if (scenario==6) {
            g.watchdog_fault_mode=true;
            unsigned loops_before=g.loops;
            host_raise_irq(1,0,0);
            uint64_t irq_entry_limit=machine.cycles+480000;
            while ((!g.pending_return || g.return_irq!=45 || !machine.cpu.trap_depth) &&
                   machine.cycles<irq_entry_limit)
                gate_run(&machine,1);
            check(g.usb_irqs>0 && g.pending_return && machine.cpu.trap_depth,
                  "USB-stall-IRQ45-not-entered");
            g.fault_active=true; g.fault_kind=2;
            machine.cpu.wfi=true; machine.pfic.enabled=0;
            uint64_t deadline=g.watchdog_deadline;
            gate_run(&machine,deadline-machine.cycles+4800000);
            check(g.watchdog_resets==1 && !g.pending_return && g.stage>=4 &&
                  g.startup_returns==1 && g.loops>loops_before &&
                  g.watchdog_feeds>0 && !g.fault_active,
                  "USB-stalled-IRQ45-IWDG-recovery");
            host_enumerate();
            check(machine.ram[P_USBD_DEV-CHG_RAM_BASE+24]==1,
                  "USB-post-IWDG-reenumeration");
        }
        if (scenario==7) {
            unsigned feeds_before=g.watchdog_feeds, scans_before=g.scans;
            machine.usb[5]|=4u; /* Stable X035 suspended MIS_ST. */
            host_irq(4,0,0);
            check(machine.ram[P_USBD_DEV-CHG_RAM_BASE+22]==1,
                  "USB-suspend-state");
            gate_run(&machine,131104768ull+4800000ull);
            check(machine.ram[P_USBD_DEV-CHG_RAM_BASE+22]==1 &&
                  !g.watchdog_resets && g.watchdog_feeds>feeds_before+1000 &&
                  g.scans>scans_before+1000,
                  "USB-healthy-suspend-IWDG-progress");
        }
        if (scenario==1) {
            machine.usb[5]|=4u; /* X035 MIS_ST.SUSPEND = 1. */
            host_irq(4,0,0);
            check(machine.ram[P_USBD_DEV-CHG_RAM_BASE+22]==1,
                  "USB-suspend-state");
            g.keys[0]=1; /* Offline Q and Eject taps must not replay. */
            gate_run(&machine,960000);
            g.keys[0]=0;
            gate_run(&machine,960000);
            g.keys[5]=1u<<5;
            gate_run(&machine,960000);
            g.keys[5]=0;
            gate_run(&machine,960000);
            check((machine.usb[0x26]&3)==2,"USB-suspended-IN-not-NAK");
            machine.usb[5]&=(uint8_t)~4u; /* Bus active; same suspend IRQ fires. */
            write_register(0xe000f000,machine.systick.ctlr & ~2u);
            write_register(0xe000f008,47700); /* Cross the 48000-count reload during settle. */
            const uint64_t wrap_cycle=machine.cycles+300;
            unsigned before=g.usb_observed_returns;
            g.usb_resume_probe=true;
            host_raise_irq(4,0,0);
            gate_run(&machine,4000);
            g.usb_resume_probe=false;
            check(g.usb_observed_returns==before+2 && !(machine.usb[6]&4u) &&
                  !(machine.pfic.line&(1ull<<45)),"USB-resume-IRQ-return");
            check(g.usb_resume_clear_count==2 && g.usb_resume_cnt_reads>=2 &&
                  g.usb_resume_mis_reads>=1 &&
                  g.usb_resume_clear_cycle<=g.usb_resume_first_cnt_cycle &&
                  g.usb_resume_first_cnt_cycle<wrap_cycle &&
                  g.usb_resume_mis_cycle>=wrap_cycle &&
                  g.usb_resume_mis_cycle-g.usb_resume_first_cnt_cycle>=480,
                  "USB-resume-10us-wrap-settle");
            check(g.usb_resume_later_uif_preserved,
                  "USB-later-UIF-not-serviced-after-return");
            check(machine.systick.sr&1u,"USB-resume-SysTick-UIF-erased");
            write_register(0xe000f000,machine.systick.ctlr | 2u);
            check(machine.ram[P_USBD_DEV-CHG_RAM_BASE+22]==0,
                  "USB-resume-state");
            if ((machine.usb[0x26]&3)==0) host_hid_report(10,0,0);
            gate_run(&machine,960000);
            if ((machine.usb[0x26]&3)==0) host_hid_report(10,0,0);
            gate_run(&machine,480000);
            check((machine.usb[0x26]&3)==2,"USB-offline-taps-replayed-after-resume");
            g.keys[0]=1;
            gate_run(&machine,960000);
            host_hid_report(10,0x14,0);
            g.keys[0]=0;
            gate_run(&machine,960000);
            host_hid_report(10,0,0);
            gate_run(&machine,960000);
            check((machine.usb[0x26]&3)==2,"USB-post-resume-idle-replay");
        }
        if (scenario==8) {
            machine.usb[5]|=4u;
            host_irq(4,0,0);
            check(machine.ram[P_USBD_DEV-CHG_RAM_BASE+22]==1,
                  "USB-suspend-state");
            machine.usb[5]&=(uint8_t)~4u;
            write_register(0xe000f000,machine.systick.ctlr & ~1u);
            g.watchdog_fault_mode=g.fault_active=true;
            g.fault_kind=9; /* Synthetic stopped CNT, HSI/IWDG keeps running. */
            unsigned before=g.usb_observed_returns;
            host_raise_irq(4,0,0);
            gate_run(&machine,48000);
            check(g.usb_irqs>0 && g.usb_observed_returns==before &&
                  machine.cpu.trap_depth,"USB-stopped-CNT-resume-did-not-stall");
            uint64_t deadline=g.watchdog_deadline;
            gate_run(&machine,deadline-machine.cycles+4800000);
            check(g.watchdog_resets==1 && g.stage>=4 && g.startup_returns==1 &&
                  !g.fault_active && g.loops>0 && g.watchdog_feeds>0,
                  "USB-stopped-CNT-IWDG-reset");
        }
        passed=true;
    }
    if (emit) printf("{\"name\":\"%s\",\"passed\":%s,\"USBFS_entries\":%u,\"USBFS_returns\":%u,\"normal_loops\":%u,\"IWDG_resets\":%u,\"resume_settle_cycles\":%llu,\"later_USB_UIF_preserved\":%s,\"failure\":\"%s\",\"IN_packets\":[",
           scenario==1?"X035-resume-counterexample":
           scenario==2?"synthetic-missing-HID-callback":
           scenario==3?"synthetic-failed-HID-completion":
           scenario==4?"host-detach-reenumeration":
           scenario==5?"connected-backpressure-over-IWDG":
           scenario==6?"stalled-USB-IRQ45-IWDG-reset":
           scenario==7?"healthy-USB-suspend-over-IWDG":
           scenario==8?"stopped-CNT-resume-IWDG-reset":"connected-Q-report",
           passed?"true":"false",g.usb_handler_entries,g.usb_observed_returns,g.loops,
           g.watchdog_resets,
           (unsigned long long)(g.usb_resume_mis_reads ?
               g.usb_resume_mis_cycle-g.usb_resume_first_cnt_cycle : 0),
           g.usb_resume_later_uif_preserved?"true":"false",g.failure?g.failure:"");
    if (emit) {
        for (unsigned i=0;i<host_in_count;i++) {
            printf("%s{\"cycle\":%llu,\"ep\":%u,\"length\":%u,\"hex\":\"",
                   i?",":"",(unsigned long long)host_in_trace[i].cycle,
                   host_in_trace[i].ep,host_in_trace[i].len);
            for (unsigned j=0;j<host_in_trace[i].len;j++)
                printf("%02x",host_in_trace[i].bytes[j]);
            printf("\"}");
        }
        printf("]}\n");
    }
    if (!passed && emit) fprintf(stderr,"connected: %s at PC %08x detail %08x EP1_CTL=%02x EP1_LEN=%u\n",
                         g.failure?g.failure:"case invariant failed",g.pc,g.detail,
                         machine.usb[0x26],machine.usb[0x24]);
    return passed;
}
static bool usb_mutant_controls(void) {
    static const char *expected[]={"unexpected-trap",
        "USB-duplicate-OUT-rejected","host-USB-IRQ-return"};
    bool passed=true;
    for (unsigned i=0;i<3;i++) {
        selected_usb_mutant=i+1;
        if (connected_reset_case(0,false) || !g.failure || strcmp(g.failure,expected[i])) {
            fprintf(stderr,"USB mutant %u expected %s got %s\n",i+1,expected[i],
                    g.failure?g.failure:"passed");
            passed=false;
        }
    }
    selected_usb_mutant=0;
    printf("{\"passed\":%s,\"rejected_model_mutants\":3,\"mutants\":[\"stuck-INT_FG-W1C\",\"forced-INT_ST-TOG_OK\",\"missing-IRQ45-line\"]}\n",
           passed?"true":"false");
    return passed;
}
int main(int argc, char **argv) {
    if (argc!=3) return 2;
    if (!strcmp(argv[1],"--fixtures")) return fixtures(argv[2]);
    if (!strcmp(argv[1],"--usb-fixtures")) return usb_fixtures()?0:1;
    bool stalls=!strcmp(argv[1],"--stall-controls");
    bool watchdog=!strcmp(argv[1],"--watchdog");
    bool mutants=!strcmp(argv[1],"--watchdog-mutants");
    bool connected=!strcmp(argv[1],"--connected");
    bool resume=!strcmp(argv[1],"--resume-counterexample");
    bool missing=!strcmp(argv[1],"--missing-callback");
    bool failed=!strcmp(argv[1],"--failed-completion");
    bool reconnect=!strcmp(argv[1],"--disconnect-reconnect");
    bool backpressure=!strcmp(argv[1],"--connected-backpressure");
    bool irq_stall=!strcmp(argv[1],"--stalled-usb-irq");
    bool suspend_idle=!strcmp(argv[1],"--connected-suspend-idle");
    bool stopped_cnt=!strcmp(argv[1],"--stopped-cnt-resume");
    bool usb_mutants=!strcmp(argv[1],"--usb-mutants");
    FILE *f=fopen(argv[stalls || watchdog || mutants || connected || resume || missing || failed || reconnect || backpressure || irq_stall || suspend_idle || stopped_cnt || usb_mutants ? 2 : 1],"rb"); if (!f) return 2;
    chg_init(&machine); machine.entry=P_ENTRY;
    size_t size=fread(machine.flash+P_ENTRY,1,P_IMAGE_END-P_ENTRY,f);
    bool exact=size==P_IMAGE_END-P_ENTRY && fgetc(f)==EOF; fclose(f);
    if (!exact) return 2;
    /* Explicit synthetic serial fixture. No bootloader or F700 metadata added. */
    put32(0xf600,0x12345678);
    memcpy(original_flash,machine.flash,sizeof original_flash);
    if (stalls) {
        printf("["); bool normal=late_stall(false); printf(","); bool irq=late_stall(true);
        printf("]\n"); return normal && irq ? 0 : 1;
    }
    if (watchdog) {
        printf("["); bool passed=true;
        for (unsigned i=0;i<13;i++) if (!watchdog_case(i)) passed=false;
        printf("]\n"); return passed?0:1;
    }
    if (mutants) return watchdog_mutant_controls()?0:1;
    if (usb_mutants) return usb_mutant_controls()?0:1;
    if (connected || resume || missing || failed || reconnect || backpressure || irq_stall || suspend_idle || stopped_cnt)
        return connected_reset_case(resume ? 1u : missing ? 2u : failed ? 3u : reconnect ? 4u :
                                    backpressure ? 5u : irq_stall ? 6u : suspend_idle ? 7u : stopped_cnt ? 8u : 0u,true)?0:1;
    printf("["); bool passed=true;
    for (unsigned i=0;i<4;i++) { if (!run_case(i,argv[2])) { passed=false; break; } }
    printf("]\n"); return passed?0:1;
}
