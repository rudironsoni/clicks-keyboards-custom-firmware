#include QMK_KEYBOARD_H

/*
 * Physical layout recovered from the stock V122 firmware (2026-10-10).
 *
 * The stock maps each electrical crossing inline in its HID builder
 * (stock 0x2b1c..0x3010); the full decode is recorded in
 * .stock/audit/NOTES.md. Our matrix scan (PB row high, read PA0..PA5)
 * uses the same crossing indexing as stock, so K<row><col> below matches
 * the stock crossings exactly. One crossing, (2, bit 3), carries no key.
 *
 * Layer 0 is the stock base layout. Layer 1 carries the stock SYM values
 * (the letters map to their row numbers: q..p -> 1..0; the rest are the
 * stock punctuation). Layer 2 puts the stock cursor-mode arrows on a
 * momentary layer, reached by holding the Clicks key (stock made it a
 * runtime mode). Layer 3 stays reserved for the keymap editor.
 *
 * The sticky key at (5,4 bit 0x10) is one-shot layer 1: tap it, type one
 * symbol, and the keyboard returns to layer 0 (the 9981-style sticky
 * layer). Holding the Clicks key at (5,4) reaches layer 2 on the fly.
 */

enum custom_keycodes {
    /* Stock consumer usages, decoded from the stock HID descriptor
     * (three bits, usages 0x01AE / 0x029D) plus the Eject usage the
     * 9981-style keyboards use to toggle the iOS on-screen keyboard. */
    CK_CONS_A = SAFE_RANGE, /* stock consumer usage 0x01AE, semantics unverified */
    CK_CONS_B,              /* stock consumer usage 0x029D, semantics unverified */
    CK_IOS_KB,              /* consumer Eject 0x00B8: iOS on-screen keyboard */
};

void ck5200_consumer_key(uint16_t usage, bool pressed);

bool process_record_user(uint16_t keycode, keyrecord_t *record) {
    switch (keycode) {
    case CK_CONS_A:
        ck5200_consumer_key(0x01AE, record->event.pressed);
        return false;
    case CK_CONS_B:
        ck5200_consumer_key(0x029D, record->event.pressed);
        return false;
    case CK_IOS_KB:
        ck5200_consumer_key(0x00B8, record->event.pressed);
        return false;
    default:
        return true;
    }
}

const uint16_t PROGMEM keymaps[][MATRIX_ROWS][MATRIX_COLS] = {
    [0] = LAYOUT_matrix(
        KC_Q,    KC_U,    KC_K,    KC_BSPC, KC_ENT,  CK_CONS_A,
        KC_W,    KC_I,    KC_D,    KC_L,    KC_V,    CK_CONS_B,
        KC_E,    KC_O,    KC_F,    KC_NO,   KC_B,    KC_LGUI,
        KC_R,    KC_P,    KC_G,    KC_Z,    KC_N,    KC_SPC,
        KC_T,    KC_A,    KC_H,    KC_X,    KC_M,    KC_LCTL,
        KC_Y,    KC_S,    LT(2, KC_J), KC_C, OSL(1), CK_IOS_KB
    ),
    /* SYM layer: the stock alternate values. */
    [1] = LAYOUT_matrix(
        KC_1,    KC_7,    KC_QUOTE, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_2,    KC_8,    KC_SCLN,  KC_QUOTE, KC_SLASH, KC_TRNS,
        KC_3,    KC_9,    KC_SCLN,  KC_NO,   KC_1,    KC_TRNS,
        KC_4,    KC_0,    KC_9,     KC_TRNS, KC_COMMA, KC_TRNS,
        KC_5,    KC_MINUS, KC_0,    KC_7,    KC_DOT,  KC_TRNS,
        KC_6,    KC_SLASH, KC_TRNS, KC_2,    KC_TRNS,  KC_TRNS
    ),
    /* Cursor layer: the stock cursor-mode arrows, on hold. */
    [2] = LAYOUT_matrix(
        KC_TRNS, KC_TRNS, KC_DOWN, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_UP,   KC_UP,   KC_RIGHT, KC_RIGHT, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_NO,   KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_LEFT, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_DOWN, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS
    ),
    /* Reserved for the keymap editor. */
    [3] = LAYOUT_matrix(
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS,
        KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS, KC_TRNS
    )
};
