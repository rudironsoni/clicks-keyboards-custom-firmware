#ifndef CK5200_GATE_H
#define CK5200_GATE_H
#include "machine.h"
#include <setjmp.h>
#include <stdio.h>
#define GATE_PROGRESS_GAP 480000u /* 10 ms at the modeled 48 MHz. */
typedef struct {
    jmp_buf abort;
    jmp_buf watchdog_escape;
    const char *failure;
    uint32_t detail, pc, return_pc, return_sp, timer_before, saved_mstatus;
    uint32_t saved_regs[32];
    uint64_t last_foreground, last_systick_return;
    unsigned stage, scan_stage, scans, tasks, loops, irqs, returns, observed_returns, handler_entries;
    unsigned rescue_loops, foreground_after_irq, startup_returns;
    unsigned rows, row_mask, mmio_reads, mmio_writes;
    uint32_t watchdog_pscr, watchdog_rld, watchdog_feeds, watchdog_resets;
    uint64_t watchdog_deadline, watchdog_enable_cycle, watchdog_pvu_until, watchdog_rvu_until;
    unsigned fault_kind, fault_loops, fault_injections, fault_limit, fault_marks_removed, fault_ticks_suppressed;
    unsigned watchdog_mutant; /* Fixture only: ignored feed or unconditional feed. */
    bool watchdog_enabled, watchdog_unlocked, watchdog_fault_mode, fault_active;
    bool fixture, rescue, pending_return, pending_handler;
    uint8_t noinit[8];
    FILE *trace;
} Gate;
extern Gate g;
void gate_fail(const char *, uint32_t);
void gate_access(ChgMachine *, uint32_t, int, bool, uint32_t);
bool gate_bus(ChgMachine *, uint32_t, int, bool, uint32_t *);
void gate_instruction(ChgMachine *, uint32_t);
void gate_trap(ChgMachine *, uint32_t, uint32_t, uint32_t, int);
void gate_mret(ChgMachine *);
void gate_csr(ChgMachine *, uint32_t, bool, uint32_t);
void gate_run(ChgMachine *, uint64_t);
void gate_complete(void);
void gate_watchdog_reset(void);
void gate_watchdog_expire(ChgMachine *);
#endif
