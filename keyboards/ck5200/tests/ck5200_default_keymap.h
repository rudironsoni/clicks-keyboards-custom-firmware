/*
 * Shared default-keymap fixture for the engine and shim test binaries.
 * The stock ledger transcription with the corrected Left Shift position.
 * Included by tests; not compiled standalone.
 */
#ifndef CK5200_DEFAULT_KEYMAP_H
#define CK5200_DEFAULT_KEYMAP_H

#include <string.h>

#if defined(__GNUC__)
#define CK_FIXTURE_FN static __attribute__((unused))
#else
#define CK_FIXTURE_FN static
#endif

CK_FIXTURE_FN void set_plain_any(ck_keymap_t *m, int layer, int row, int col,
                                 uint16_t usage, uint8_t mods)
{
    ck_action_t *a = &m->action[layer][ck_pos(row, col)];
    memset(a, 0, sizeof(*a));
    a->usage = usage;
    a->mods = mods;
}

CK_FIXTURE_FN void set_plain_nav(ck_keymap_t *m, int row, int col, uint16_t usage)
{
    set_plain_any(m, CK_LAYER_NAV, row, col, usage, 0);
}

CK_FIXTURE_FN void stock_keymap(ck_keymap_t *m)
{
    memset(m, 0, sizeof(*m));
    for (int layer = 1; layer < CK_LAYERS; layer++)
        for (int pos = 0; pos < CK_KEYS; pos++)
            m->action[layer][pos].kind = CK_ACTION_TRANSPARENT;
#define SET(l, r, c, u) set_plain_any(m, l, r, c, u, 0)
    /* Base layer (consumer positions 0/05, 1/05, 5/05 emit nothing in
     * this keyboard-page model; 2/03 is Left Shift). */
    SET(0, 0, 0, 0x14); SET(0, 0, 1, 0x18); SET(0, 0, 2, 0x0E);
    SET(0, 0, 3, 0x2A); SET(0, 0, 4, 0x28);
    SET(0, 1, 0, 0x1A); SET(0, 1, 1, 0x0C); SET(0, 1, 2, 0x07);
    SET(0, 1, 3, 0x0F); SET(0, 1, 4, 0x19);
    SET(0, 2, 0, 0x08); SET(0, 2, 1, 0x12); SET(0, 2, 2, 0x09);
    set_plain_any(m, 0, 2, 3, 0, CK_MOD_LSHIFT);
    set_plain_any(m, 1, 2, 3, 0, CK_MOD_LSHIFT);
    set_plain_any(m, 2, 2, 3, 0, CK_MOD_LSHIFT);
    SET(0, 2, 4, 0x05);
    set_plain_any(m, 0, 2, 5, 0, CK_MOD_LGUI);
    SET(0, 3, 0, 0x15); SET(0, 3, 1, 0x13); SET(0, 3, 2, 0x0A);
    SET(0, 3, 3, 0x1D); SET(0, 3, 4, 0x11); SET(0, 3, 5, 0x2C);
    SET(0, 4, 0, 0x17); SET(0, 4, 1, 0x04); SET(0, 4, 2, 0x0B);
    SET(0, 4, 3, 0x1B); SET(0, 4, 4, 0x10);
    set_plain_any(m, 0, 4, 5, 0, CK_MOD_LCTL);
    SET(0, 5, 0, 0x1C); SET(0, 5, 1, 0x16); SET(0, 5, 2, 0x0D);
    SET(0, 5, 3, 0x06);
    {
        ck_action_t *a = &m->action[0][ck_pos(5, 4)];
        memset(a, 0, sizeof(*a));
        a->activation = CK_ACT_ONESHOT;
        a->layer_switch = CK_LAYER_SYM;
    }
    /* SYM layer. */
    SET(1, 0, 0, 0x1E); SET(1, 0, 1, 0x24); SET(1, 0, 2, 0x34);
    SET(1, 1, 0, 0x1F); SET(1, 1, 1, 0x25); SET(1, 1, 2, 0x33);
    SET(1, 1, 3, 0x34); SET(1, 1, 4, 0x38);
    SET(1, 2, 0, 0x20); SET(1, 2, 1, 0x26); SET(1, 2, 2, 0x33);
    SET(1, 2, 4, 0x1E);
    SET(1, 3, 0, 0x21); SET(1, 3, 1, 0x27); SET(1, 3, 2, 0x26);
    SET(1, 3, 3, 0x34); SET(1, 3, 4, 0x36);
    SET(1, 4, 0, 0x22); SET(1, 4, 1, 0x2D); SET(1, 4, 2, 0x27);
    SET(1, 4, 3, 0x24); SET(1, 4, 4, 0x37);
    SET(1, 5, 0, 0x23); SET(1, 5, 1, 0x38); SET(1, 5, 3, 0x1F);
    /* NAV layer (stock cursor column). */
    SET(2, 0, 2, 0x51);
    SET(2, 1, 0, 0x52); SET(2, 1, 1, 0x52); SET(2, 1, 2, 0x4F);
    SET(2, 1, 3, 0x4F);
    SET(2, 4, 1, 0x50);
    SET(2, 5, 1, 0x51); SET(2, 5, 2, 0x50);
#undef SET
}

#endif /* CK5200_DEFAULT_KEYMAP_H */
