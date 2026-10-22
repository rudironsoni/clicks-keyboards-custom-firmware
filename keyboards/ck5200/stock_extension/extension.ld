/* Linker script for the CK-5200 stock-extension research image.
 *
 * Layout: the patched V122 image at its native base 0x2000, the extension
 * (engine + shim + serialized keymap) placed at 0x6900, inside the unused
 * application capacity [0x68D8, 0x8A00). V122 statics end at 0x200006B4.
 * The extension reserves [0x200006C0,0x20002000), below a 12 KiB stack
 * allowance. This static separation still needs execution-path checks.
 *
 * RESEARCH ARTIFACT ONLY: never installed; the R3 recovery gate is
 * unchanged. The OEM report-framing open items (shim header) are pinned
 * before any executable claim.
 */
ENTRY(ck_v122_start)
PROVIDE(ck_v122_start = 0x2000);
PROVIDE(oem_transport = 0x62CC);
PROVIDE(oem_hid_send = 0x3CBE);
PROVIDE(oem_ep_submit = 0x3E28);
PROVIDE(oem_system_init = 0x3BBA);
PROVIDE(oem_tick = 0x22DA);
PROVIDE(oem_builder = 0x2796);
PROVIDE(oem_layer = 0x2958);

MEMORY {
    FLASH (rx) : ORIGIN = 0x2000, LENGTH = 0x6A00
    RAM (rw) : ORIGIN = 0x200006C0, LENGTH = 0x1940
}

SECTIONS
{
    .v122blob 0x2000 : {
        KEEP(*(.v122blob))
    } > FLASH

    .text 0x6900 : {
        KEEP(*(.text.ck_extension_builder))
        *(.text .text.*)
    } > FLASH
    ASSERT(ck_extension_builder == 0x6900,
           "CK-5200 callback entry is not at 0x6900")
    .rodata : {
        *(.rodata .rodata.*)
    } > FLASH
    .data : {
        __extension_data_start = .;
        *(.data .data.* .sdata .sdata.*)
        __extension_data_end = .;
    } > RAM AT> FLASH
    __extension_data_load = LOADADDR(.data);
    .bss (NOLOAD) : {
        __extension_bss_start = .;
        *(.bss .bss.* .sbss .sbss.*)
        *(COMMON)
        __extension_bss_end = .;
    } > RAM
    ASSERT(__extension_bss_end <= 0x20002000, "extension exceeds SRAM reservation")

    /DISCARD/ : {
        *(.eh_frame*)
        *(.comment)
        *(.riscv.attributes)
    }
}
