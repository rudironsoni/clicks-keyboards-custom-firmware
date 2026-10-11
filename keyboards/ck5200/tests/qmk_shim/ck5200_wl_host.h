/* Backing-region stand-in for the wear-leveling host build
 * (CK5200_WL_HOST_TEST in wear_leveling_ch32v20x.c). */
#pragma once

/* Host relocation of the flash backing region at 0x0800F800,
 * sized by WEAR_LEVELING_BACKING_SIZE (1024). */
extern uint8_t ck5200_wl_host_region[];
