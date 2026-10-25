#pragma once

#include <stdbool.h>

void ck5200_watchdog_begin(void);
void ck5200_watchdog_note_scan(void);
void ck5200_watchdog_note_transport(void);
void ck5200_watchdog_finish(bool rescue);
