/*
 * Tests for the CK-5200 extension shim (host harness) and the packed
 * keymap serialization. Offline research artifact for the
 * correctness-and-integration milestone.
 *
 * Compile and run (host):
 *   cc -std=c99 -Wall -Werror -Os -DCK_SHIM_HOST -I stock_extension \
 *      stock_extension/ck5200_keymap_engine.c \
 *      stock_extension/ck5200_keymap_serialize.c \
 *      stock_extension/ck5200_extension_shim.c \
 *      tests/ck5200_extension_shim_test.c -o /tmp/shim_test
 *   /tmp/shim_test
 */
#include "ck5200_extension_shim.h"
#include "ck5200_keymap_serialize.h"

#include <stdio.h>
#include <string.h>

static int failures = 0;
static int passes = 0;
#define CHECK(cond, what)                                                  \
    do {                                                                   \
        if (cond) {                                                        \
            passes++;                                                      \
            printf("PASS %s\n", what);                                     \
        } else {                                                           \
            failures++;                                                    \
            printf("FAIL %s (line %d)\n", what, __LINE__);                 \
        }                                                                  \
    } while (0)

/* The default stock transcription, duplicated from the engine test so
 * this binary stays self-contained. */
#include "ck5200_default_keymap.h"

static void test_shim_default_report(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    ck_engine_t e;
    ck_engine_init(&e, &map);
    uint8_t matrix[CK_ROWS] = {0};
    ck_frame_t f;

    matrix[0] = 0x01;   /* q down */
    ck_shim_process(&e, matrix, 10, &f);
    CHECK(f.len == 5 && f.bytes[0] == 0x0B && f.bytes[1] == 0x02
          && f.bytes[3] == 0 && f.bytes[4] == 0x14,
          "shim: q frame = type 020B, no mods, usage q");
    matrix[0] = 0x00;   /* q up */
    ck_shim_process(&e, matrix, 20, &f);
    CHECK(f.len == 4 && f.bytes[3] == 0,
          "shim: release frame carries no usages");

    /* The shim produces exactly one frame per tick: replacement, never
     * OEM-plus-extension double emission. */
    matrix[0] = 0x02;   /* u down */
    ck_shim_process(&e, matrix, 30, &f);
    CHECK(f.len == 5 && f.bytes[4] == 0x18,
          "shim: single frame per tick (no double emission)");
    matrix[0] = 0x00;
    ck_shim_process(&e, matrix, 40, &f);
    CHECK(f.len == 4, "shim: clean release frame");
}

static void test_shim_altered_key_replaces_oem_action(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    /* NAV remap: n (3/04) NAV -> Esc. Activator: hold on Backspace. */
    set_plain_nav(&map, 3, 4, 0x29);
    {
        ck_action_t *a = &map.action[0][ck_pos(0, 3)];
        memset(a, 0, sizeof(*a));
        a->activation = CK_ACT_HOLD;
        a->layer_switch = CK_LAYER_NAV;
    }
    ck_engine_t e;
    ck_engine_init(&e, &map);
    uint8_t matrix[CK_ROWS] = {0};
    ck_frame_t f;

    /* Base n -> usage n (the OEM action is replaced by the engine's
     * stock-equivalent, not emitted alongside it). */
    matrix[3] = 0x10;
    ck_shim_process(&e, matrix, 10, &f);
    CHECK(f.len == 5 && f.bytes[4] == 0x11,
          "shim remap: base n is n (engine replaces OEM action)");
    matrix[3] = 0x00;
    ck_shim_process(&e, matrix, 20, &f);

    /* Under NAV, the same physical key emits Esc. */
    matrix[0] = 0x08;   /* hold NAV */
    ck_shim_process(&e, matrix, 30, &f);
    CHECK(f.len == 4, "shim remap: activator emits no usage");
    matrix[3] = 0x10;
    ck_shim_process(&e, matrix, 40, &f);
    CHECK(f.len == 5 && f.bytes[4] == 0x29,
          "shim remap: NAV n emits Esc");
    matrix[3] = 0x00;
    ck_shim_process(&e, matrix, 50, &f);
    matrix[0] = 0x00;
    ck_shim_process(&e, matrix, 60, &f);
    CHECK(f.len == 4, "shim remap: clean end state");

    /* Time-driven release: a quick TAP on the activator with tap usage
     * Esc must clear WITHOUT any further matrix event. */
    {
        ck_action_t *t = &map.action[0][ck_pos(0, 3)];
        t->activation = CK_ACT_TAP;
        t->tap_usage = 0x29;
        t->tap_ms = 200;
    }
    ck_engine_init(&e, &map);
    matrix[0] = 0x08;
    ck_shim_process(&e, matrix, 100, &f);
    matrix[0] = 0x00;
    ck_shim_process(&e, matrix, 150, &f);
    CHECK(f.len == 5 && f.bytes[4] == 0x29,
          "shim tap: quick release emits Esc frame");
    uint32_t d = ck_shim_next_deadline(&e);
    CHECK(d != CK_NO_DEADLINE, "shim tap: release tick is scheduled");
    ck_shim_process(&e, matrix, d + 1, &f);
    CHECK(f.len == 4,
          "shim tap: release frame arrives with no further key event");
}

static void test_serialization_roundtrip_and_size(void)
{
    ck_keymap_t map;
    stock_keymap(&map);
    uint8_t buf[512];
    size_t len = ck_keymap_serialize(&map, buf, sizeof(buf));
    CHECK(len > 0 && len < 512, "ser: record fits the buffer");
    printf("     (serialized default record: %zu bytes)\n", len);

    ck_keymap_t loaded;
    int rc = ck_keymap_deserialize(buf, len, &loaded);
    CHECK(rc == 0, "ser: default record deserializes");
    CHECK(memcmp(&loaded, &map, sizeof(map)) == 0,
          "ser: round-trip is byte-identical");

    /* Corruptions must be rejected fail-closed. */
    ck_keymap_t sink;
    CHECK(ck_keymap_deserialize(buf, len - 1, &sink) < 0,
          "ser: truncated record rejected");
    uint8_t bad[8] = {'X', 'K', 'M', 1, 0, 0};
    CHECK(ck_keymap_deserialize(bad, sizeof(bad), &sink) < 0,
          "ser: bad magic rejected");

    /* A record with an out-of-page usage must be rejected at load. */
    uint8_t over[16] = {'C', 'K', 'M', 1, 1, 0, /* count=1 */
                        0, 0, 0x00, 0xFF, 0x01}; /* pos0 layer0 usage 0x1FF */
    CHECK(ck_keymap_deserialize(over, sizeof(over), &sink) < 0,
          "ser: out-of-page usage rejected at load");

    /* Trailing bytes are rejected. */
    uint8_t padded[512];
    memcpy(padded, buf, len);
    padded[len] = 0x00;
    CHECK(ck_keymap_deserialize(padded, len + 1, &sink) < 0,
          "ser: trailing bytes rejected");

    /* A custom delta record is small: only the remapped actions. */
    ck_keymap_t custom;
    stock_keymap(&custom);
    set_plain_nav(&custom, 3, 4, 0x29);
    set_plain_nav(&custom, 4, 4, 0x1D);
    custom.action[CK_LAYER_NAV][ck_pos(4, 4)].mods = CK_MOD_LGUI;
    size_t clen = ck_keymap_serialize(&custom, buf, sizeof(buf));
    CHECK(clen > 0, "ser: custom delta record serializes");
    printf("     (serialized delta record: %zu bytes)\n", clen);
    ck_keymap_t cloaded;
    CHECK(ck_keymap_deserialize(buf, clen, &cloaded) == 0
          && memcmp(&cloaded, &custom, sizeof(custom)) == 0,
          "ser: delta round-trip is byte-identical");
}

int main(void)
{
    test_shim_default_report();
    test_shim_altered_key_replaces_oem_action();
    test_serialization_roundtrip_and_size();
    printf(failures == 0 ? "ok\n" : "==== %d failure(s) ====\n", failures);
    return failures == 0 ? 0 : 1;
}
