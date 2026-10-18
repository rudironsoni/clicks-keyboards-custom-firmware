#include "gate.h"
#include "profile.h"
#include <stdlib.h>
#include <string.h>

static ChgMachine machine;
static uint8_t original_flash[CHG_FLASH_SIZE];
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
int main(int argc, char **argv) {
    if (argc!=3) return 2;
    if (!strcmp(argv[1],"--fixtures")) return fixtures(argv[2]);
    bool stalls=!strcmp(argv[1],"--stall-controls");
    bool watchdog=!strcmp(argv[1],"--watchdog");
    bool mutants=!strcmp(argv[1],"--watchdog-mutants");
    FILE *f=fopen(argv[stalls || watchdog || mutants ? 2 : 1],"rb"); if (!f) return 2;
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
    printf("["); bool passed=true;
    for (unsigned i=0;i<4;i++) { if (!run_case(i,argv[2])) { passed=false; break; } }
    printf("]\n"); return passed?0:1;
}
