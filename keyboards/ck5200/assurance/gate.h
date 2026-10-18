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
    uint32_t detail, pc, return_pc, return_sp, timer_before, saved_mstatus, keyboard_return_pc;
    uint32_t saved_regs[32];
    uint64_t last_foreground, last_systick_return;
    unsigned stage, scan_stage, scans, tasks, loops, irqs, returns, observed_returns, handler_entries;
    unsigned return_irq, usb_irqs, usb_returns, usb_observed_returns, usb_handler_entries;
    unsigned usb_fault_kind, usb_fault_injections, usb_model_mutant;
    uint64_t usb_resume_clear_cycle, usb_resume_first_cnt_cycle, usb_resume_mis_cycle;
    unsigned usb_resume_clear_count, usb_resume_cnt_reads, usb_resume_mis_reads;
    bool usb_resume_probe, usb_resume_later_uif, usb_resume_later_uif_preserved;
    unsigned rescue_loops, foreground_after_irq, startup_returns;
    unsigned rows, row_mask, mmio_reads, mmio_writes;
    uint32_t watchdog_pscr, watchdog_rld, watchdog_feeds, watchdog_resets;
    uint64_t watchdog_deadline, watchdog_enable_cycle, watchdog_pvu_until, watchdog_rvu_until;
    unsigned fault_kind, fault_loops, fault_injections, fault_limit, fault_marks_removed, fault_ticks_suppressed;
    unsigned watchdog_mutant; /* Fixture only: ignored feed or unconditional feed. */
    bool watchdog_enabled, watchdog_unlocked, watchdog_fault_mode, fault_active;
    bool fixture, rescue, pending_return, pending_handler, usb_connected, usb_host_attached;
    uint8_t keys[6]; /* Host's modeled row/column electrical closures. */
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
