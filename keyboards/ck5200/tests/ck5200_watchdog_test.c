#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    volatile uint32_t CTLR, PSCR, RLDR, STATR;
} IWDG_TypeDef;
static IWDG_TypeDef watchdog_registers;
#define IWDG (&watchdog_registers)
#define CK5200_WATCHDOG_HOST_TEST
#include "../firmware/platform/ch32x035/platform.c"

static void expect_no_feed(bool rescue) {
    IWDG->CTLR = 0;
    ck5200_watchdog_finish(rescue);
    assert(IWDG->CTLR == 0);
}

int main(void) {
    /* The reset handler configures these registers before C startup. */
    IWDG->CTLR = 0xCCCC;
    IWDG->PSCR = 3;
    IWDG->RLDR = 4000;
    assert(IWDG->PSCR == 3 && IWDG->RLDR == 4000 && IWDG->CTLR == 0xCCCC);
    ck5200_watchdog_note_scan(); /* Initialization is outside the first loop. */
    ck5200_watchdog_begin();

    /* Neither an ISR tick nor repeated foreground returns can feed alone. */
    ++timer_count;
    expect_no_feed(false);
    ck5200_watchdog_note_transport();
    expect_no_feed(false);
    ck5200_watchdog_note_scan();
    expect_no_feed(false);

    ck5200_watchdog_note_scan();
    ck5200_watchdog_note_transport();
    ck5200_watchdog_begin();
    expect_no_feed(false); /* A new loop discards prior-loop marks. */
    ck5200_watchdog_note_scan();
    ck5200_watchdog_note_transport();
    ck5200_watchdog_finish(false);
    assert(IWDG->CTLR == 0xAAAA);
    expect_no_feed(false); /* Old marks must not excuse a skipped pass. */

    ck5200_watchdog_note_scan();
    ck5200_watchdog_note_transport();
    expect_no_feed(false); /* SysTick must move after the last feed. */
    ++timer_count;
    expect_no_feed(false); /* Marks from the prior pass were consumed. */

    ck5200_watchdog_note_transport();
    ck5200_watchdog_finish(true);
    assert(IWDG->CTLR == 0xAAAA); /* Rescue has no matrix scan. */
    expect_no_feed(true);
    ++timer_count;
    expect_no_feed(true); /* Tick alone does not prove transport work. */

    puts("watchdog register writes and fresh progress gates pass");
}
