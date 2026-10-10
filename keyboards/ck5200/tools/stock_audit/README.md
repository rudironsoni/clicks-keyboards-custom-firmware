# Stock audit tooling

These programs re-derive every generated artifact of the stock-firmware
decode from the vendor images archived in `.stock/` (gitignored; hashes in
[docs/STOCK_DECODE.md](../../docs/STOCK_DECODE.md)).

```sh
# 1. Wrap the .bin in an ELF and disassemble with WCH encodings decoded.
python3 mkelf.py ../../../.stock/iKeyboard_CK-5200_V122_120.bin v122.elf
/opt/homebrew/opt/llvm/bin/llvm-objdump -d --triple=riscv32 \
    --mattr=+m,+a,+c,+xwchc v122.dis_input > v122.dis
# rg.py prints an address range from a .dis file:
python3 rg.py 5bec 5cd6 v122.dis

# 2. Regenerate the Identify parameter table.
python3 extract_ident.py ../../../.stock/iKeyboard_CK-5200_V122_120.bin \
    ../../firmware/platform/ch32v20x/iap2_ident_data.h

# 3. Re-extract the keymap table (prints crossing -> values rows).
python3 extract_keymap.py v122.dis
```

The generated `iap2_ident_data.h` and the keymap in
`firmware/keyboard/keymaps/diagnostic/keymap.c` must match these outputs;
`tests/test_keymap_layout.py` asserts the keymap side.
