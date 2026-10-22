/* Linker script for the CK-5200 stock-extension research image.
 *
 * Layout: the patched V122 image at its native base 0x2000, the extension
 * (engine + shim + serialized keymap) placed at 0x6900, inside the unused
 * application capacity [0x68D8, 0x8A00). oem_send is the V122 send routine
 * at linked address 0x278C (proven by the feasibility work).
 *
 * RESEARCH ARTIFACT ONLY: never installed; the R3 recovery gate is
 * unchanged. The OEM report-framing open items (shim header) are pinned
 * before any executable claim.
 */
ENTRY(ck_extension_builder)
PROVIDE(oem_send = 0x278C);

SECTIONS
{
    . = 0x2000;
    .v122blob : {
        KEEP(*(.v122blob))
    }

    . = 0x6900;
    .text : {
        *(.text .text.*)
    }
    .rodata : {
        *(.rodata .rodata.*)
    }
    .data : {
        *(.data .data.*)
    }
    .bss (NOLOAD) : {
        *(.bss .bss.*)
        *(COMMON)
    }

    /DISCARD/ : {
        *(.eh_frame*)
        *(.comment)
        *(.riscv.attributes)
    }
}
