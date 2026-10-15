#pragma once

#include <stdbool.h>

/* Application-level only. No guarantee before main(), or if USB/iAP2 fails. */
bool ck5200_rescue_active(void);
void ck5200_request_rescue(void);
void ck5200_transport_setup(void);
void ck5200_transport_task(void);
