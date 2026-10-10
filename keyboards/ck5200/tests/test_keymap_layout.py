"""Keymap layout versus the decoded stock table.

Parses the flashed keymap (firmware/keyboard/keymaps/diagnostic/keymap.c)
and asserts every electrical crossing on every layer against the
36-crossing table decoded from the stock V122 image (stock
0x2b1c..0x3010; the full decode with stock addresses lives in
docs/STOCK_DECODE.md and can be re-derived with
tools/stock_audit/extract_keymap.py). This closes the gap where a typo
in the keymap would ship silently: the dispatcher tests never looked at
the keymap contents.
"""

import re
from pathlib import Path

KEYMAP = Path(__file__).resolve().parents[1] / "firmware/keyboard/keymaps/diagnostic/keymap.c"

# Ground truth from the stock decode. Crossing = (matrix row, PA bit).
# fmt: off
BASE = {
    (0, 0): "KC_Q",    (0, 1): "KC_U",    (0, 2): "KC_K",    (0, 3): "KC_BSPC",
    (0, 4): "KC_ENT",  (0, 5): "CK_CONS_A",
    (1, 0): "KC_W",    (1, 1): "KC_I",    (1, 2): "KC_D",    (1, 3): "KC_L",
    (1, 4): "KC_V",    (1, 5): "CK_CONS_B",
    (2, 0): "KC_E",    (2, 1): "KC_O",    (2, 2): "KC_F",    (2, 3): "KC_NO",
    (2, 4): "KC_B",    (2, 5): "KC_LGUI",
    (3, 0): "KC_R",    (3, 1): "KC_P",    (3, 2): "KC_G",    (3, 3): "KC_Z",
    (3, 4): "KC_N",    (3, 5): "KC_SPC",
    (4, 0): "KC_T",    (4, 1): "KC_A",    (4, 2): "KC_H",    (4, 3): "KC_X",
    (4, 4): "KC_M",    (4, 5): "KC_LCTL",
    (5, 0): "KC_Y",    (5, 1): "KC_S",    (5, 2): "LT(2, KC_J)",
    (5, 3): "KC_C",    (5, 4): "OSL(1)",  (5, 5): "CK_IOS_KB",
}

SYM = {  # stock SYM values (the stock maps letters to their row numbers)
    (0, 0): "KC_1",    (0, 1): "KC_7",    (0, 2): "KC_QUOTE",
    (1, 0): "KC_2",    (1, 1): "KC_8",    (1, 2): "KC_SCLN",   (1, 3): "KC_QUOTE",
    (1, 4): "KC_SLASH",
    (2, 0): "KC_3",    (2, 1): "KC_9",    (2, 2): "KC_SCLN",   (2, 3): "KC_NO",
    (2, 4): "KC_1",
    (3, 0): "KC_4",    (3, 1): "KC_0",    (3, 2): "KC_9",      (3, 4): "KC_COMMA",
    (4, 0): "KC_5",    (4, 1): "KC_MINUS", (4, 2): "KC_0",     (4, 3): "KC_7",
    (4, 4): "KC_DOT",
    (5, 0): "KC_6",    (5, 1): "KC_SLASH", (5, 3): "KC_2",
}

CURSOR = {  # stock cursor-mode arrows
    (0, 2): "KC_DOWN",
    (1, 0): "KC_UP",   (1, 1): "KC_UP",   (1, 2): "KC_RIGHT",  (1, 3): "KC_RIGHT",
    (4, 1): "KC_LEFT",
    (5, 1): "KC_DOWN",
}

SPARE = {(2, 3)}
TRANSPARENT = "KC_TRNS"
# fmt: on


def parse_layers(text: str) -> dict:
    """Returns {layer_index: [[key, ...] per row]} from the keymap source."""
    layers = {}
    for m in re.finditer(r"\[(\d+)\]\s*=\s*LAYOUT_matrix\(", text):
        idx = int(m.group(1))
        # Scan to the matching close paren: keycodes like LT(2, KC_J)
        # nest, so plain regex splitting would cut the list short.
        depth, i = 1, m.end()
        while depth:
            c = text[i]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            i += 1
        # Split at top-level commas only (LT/OSL arguments nest).
        keys, depth, start = [], 0, m.end()
        for j in range(m.end(), i):
            c = text[j]
            if c == "(":
                depth += 1
            elif c == ")":
                depth -= 1
            elif c == "," and depth == 0:
                keys.append(text[start:j].strip())
                start = j + 1
        keys.append(text[start : i - 1].strip())
        assert len(keys) == 36, f"layer {idx} has {len(keys)} keys"
        layers[idx] = [keys[r * 6 : (r + 1) * 6] for r in range(6)]
    assert set(layers) == {0, 1, 2, 3}, f"unexpected layers: {sorted(layers)}"
    return layers


def test_keymap_matches_stock_decode():
    layers = parse_layers(KEYMAP.read_text())

    for (row, col), want in sorted(BASE.items()):
        got = layers[0][row][col]
        assert got == want, f"L0 ({row},{col}): {got} != decoded {want}"
    for (row, col) in SPARE:
        assert layers[0][row][col] == "KC_NO", f"L0 spare ({row},{col}) must stay empty"

    for (row, col), want in sorted(SYM.items()):
        got = layers[1][row][col]
        assert got == want, f"L1 ({row},{col}): {got} != decoded {want}"

    for (row, col), want in sorted(CURSOR.items()):
        got = layers[2][row][col]
        assert got == want, f"L2 ({row},{col}): {got} != decoded {want}"

    # Everything not in a layer's decode stays transparent.
    for layer, table in ((1, SYM), (2, CURSOR)):
        for row in range(6):
            for col in range(6):
                if (row, col) not in table and (row, col) not in SPARE:
                    assert layers[layer][row][col] == TRANSPARENT, (
                        f"L{layer} ({row},{col}) should be transparent, is {layers[layer][row][col]}"
                    )
                if (row, col) in SPARE:
                    assert layers[layer][row][col] == "KC_NO"

    # Layer 3 stays fully reserved.
    for row in range(6):
        for col in range(6):
            assert layers[3][row][col] == TRANSPARENT, f"L3 ({row},{col}) must be transparent"


def test_layout_macro_matches_matrix_geometry():
    """LAYOUT_matrix must stay a plain row-major 6x6 pass-through, since
    the decoded crossings index our scan (PB row, PA bit) directly."""
    text = (KEYMAP.parents[2] / "ck5200.h").read_text()
    start = text.find("#define LAYOUT_matrix")
    assert start >= 0, "LAYOUT_matrix macro not found in ck5200.h"
    body = text[start:]
    for row in range(6):
        for col in range(6):
            assert f"K{row}{col}" in body, f"LAYOUT_matrix loses K{row}{col}"
    for row in range(6):
        # every row block lists K<r>0..K<r>5 in column order, one group
        assert re.search(r"\{K%d0[^\n]*\}" % row, body), f"row {row} not one brace group"
