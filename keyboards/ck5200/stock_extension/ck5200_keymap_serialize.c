/*
 * Packed serialization for CK-5200 keymap records (research artifact).
 *
 * Format version 3 (little-endian):
 *   magic    3 bytes  'C','K','M'
 *   version  1 byte   0x03
 *   count    2 bytes  entry count N
 *   entries  N times:
 *     pos          1 byte   0..35
 *     layer        1 byte   0..2
 *     flags        1 byte   bit0-1 kind (0 plain, 1 layer, 2 NO, 3 transparent),
 *                           bit2 has-mods, bit3 has-tap-usage, bit4 has-tap-ms
 *                           bit5 OEM policy (only with kind 2, no payload)
 *     [kind 0] usage      2 bytes
 *     [kind 1] act|layer 1 byte   activation<<4 | layer_switch
 *     [has-mods]       mods       1 byte
 *     [has-tap-usage]  tap usage  2 bytes
 *     [has-tap-ms]     tap ms     2 bytes
 *
 * Zero actions are omitted (NO). Transparency is always explicit.
 * Deserialize validates every field (positions, layers, usages <= 0xFF,
 * layer targets < 3) and rejects the whole record on any violation:
 * a malformed device record can never load partially.
 */
#include "ck5200_keymap_serialize.h"

#include <string.h>

#define CKM_MAGIC0 'C'
#define CKM_MAGIC1 'K'
#define CKM_MAGIC2 'M'
#define CKM_VERSION 3

static bool action_is_empty(const ck_action_t *a)
{
    return a->kind == CK_ACTION_KEY && a->usage == 0 && a->mods == 0
        && a->activation == CK_ACT_NONE;
}

static bool rd_u16(const uint8_t *buf, size_t len, size_t *off, uint16_t *out)
{
    if (*off + 2 > len)
        return false;
    *out = (uint16_t)(buf[*off] | (buf[*off + 1] << 8));
    *off += 2;
    return true;
}

static bool rd_u8(const uint8_t *buf, size_t len, size_t *off, uint8_t *out)
{
    if (*off + 1 > len)
        return false;
    *out = buf[(*off)++];
    return true;
}

static bool wr_u16(uint8_t *buf, size_t cap, size_t *off, uint16_t v)
{
    if (*off + 2 > cap)
        return false;
    buf[*off] = (uint8_t)(v & 0xFF);
    buf[*off + 1] = (uint8_t)(v >> 8);
    *off += 2;
    return true;
}

static bool wr_u8(uint8_t *buf, size_t cap, size_t *off, uint8_t v)
{
    if (*off + 1 > cap)
        return false;
    buf[(*off)++] = v;
    return true;
}

size_t ck_keymap_serialize(const ck_keymap_t *map, uint8_t *out, size_t cap)
{
    if (ck_keymap_validate(map))
        return 0;
    /* First pass: count non-empty actions. */
    size_t count = 0;
    for (int layer = 0; layer < CK_LAYERS; layer++)
        for (int pos = 0; pos < CK_KEYS; pos++)
            if (!action_is_empty(&map->action[layer][pos]))
                count++;
    if (count > 0xFFFF)
        return 0;

    size_t off = 0;
    if (!wr_u8(out, cap, &off, CKM_MAGIC0)) return 0;
    if (!wr_u8(out, cap, &off, CKM_MAGIC1)) return 0;
    if (!wr_u8(out, cap, &off, CKM_MAGIC2)) return 0;
    if (!wr_u8(out, cap, &off, CKM_VERSION)) return 0;
    if (!wr_u16(out, cap, &off, (uint16_t)count)) return 0;

    for (int layer = 0; layer < CK_LAYERS; layer++) {
        for (int pos = 0; pos < CK_KEYS; pos++) {
            const ck_action_t *a = &map->action[layer][pos];
            if (action_is_empty(a))
                continue;
            uint8_t kind = a->activation == CK_ACT_NONE ? 0 : 1;
            if (a->kind != CK_ACTION_KEY)
                kind = a->kind == CK_ACTION_TRANSPARENT ? 3 : 2;
            uint8_t flags = (uint8_t)(kind & 0x03);
            if (a->kind == CK_ACTION_OEM)
                flags |= 0x20;
            if (a->mods != 0)
                flags |= 0x04;
            if (kind == 1 && a->tap_usage != 0)
                flags |= 0x08;
            if (kind == 1 && a->tap_ms != 0)
                flags |= 0x10;
            if (!wr_u8(out, cap, &off, (uint8_t)pos)) return 0;
            if (!wr_u8(out, cap, &off, (uint8_t)layer)) return 0;
            if (!wr_u8(out, cap, &off, flags)) return 0;
            if (kind == 0) {
                if (!wr_u16(out, cap, &off, a->usage)) return 0;
            } else if (kind == 1) {
                uint8_t packed = (uint8_t)((a->activation << 4)
                                           | (a->layer_switch & 0x0F));
                if (!wr_u8(out, cap, &off, packed)) return 0;
            }
            if (flags & 0x04) {
                if (!wr_u8(out, cap, &off, a->mods)) return 0;
            }
            if (flags & 0x08) {
                if (!wr_u16(out, cap, &off, a->tap_usage)) return 0;
            }
            if (flags & 0x10) {
                if (!wr_u16(out, cap, &off, a->tap_ms)) return 0;
            }
        }
    }
    return off;
}

/* First validate without writes, then repeat the same parser to apply.
 * This avoids a second full map on the device stack. */
static int decode(const uint8_t *buf, size_t len, ck_keymap_t *map)
{
    if (len < 6)
        return -1;
    if (buf[0] != CKM_MAGIC0 || buf[1] != CKM_MAGIC1 || buf[2] != CKM_MAGIC2
        || buf[3] != CKM_VERSION)
        return -2;

    size_t off = 4;
    uint16_t count;
    if (!rd_u16(buf, len, &off, &count))
        return -3;
    if (count > CK_LAYERS * CK_KEYS || len > CK_KEYMAP_RECORD_MAX)
        return -3;

    uint8_t seen[CK_LAYERS][CK_KEYS] = {{0}};
    for (uint16_t i = 0; i < count; i++) {
        uint8_t pos, layer, flags;
        if (!rd_u8(buf, len, &off, &pos)) return -4;
        if (!rd_u8(buf, len, &off, &layer)) return -4;
        if (!rd_u8(buf, len, &off, &flags)) return -4;
        if (pos >= CK_KEYS || layer >= CK_LAYERS)
            return -5;
        if (seen[layer][pos]++ || (flags & ~0x3F))
            return -5;
        ck_action_t action = {0};
        ck_action_t *a = &action;
        uint8_t kind = flags & 0x03;
        if (kind == 0) {
            uint16_t usage;
            if (!rd_u16(buf, len, &off, &usage)) return -6;
            if (usage > 0xFF)
                return -7;              /* keyboard-page only */
            a->usage = usage;
        } else if (kind == 1) {
            uint8_t packed;
            if (!rd_u8(buf, len, &off, &packed)) return -6;
            a->activation = (uint8_t)(packed >> 4);
            a->layer_switch = (uint8_t)(packed & 0x0F);
            if (a->activation < CK_ACT_TAP || a->activation > CK_ACT_ONESHOT)
                return -7;
            if (a->layer_switch >= CK_LAYERS)
                return -7;
        } else {
            a->kind = kind == 2 ? CK_ACTION_NO : CK_ACTION_TRANSPARENT;
            if (flags == 0x22)
                a->kind = CK_ACTION_OEM;
            else if (flags != kind)
                return -7;
        }
        if (kind < 2 && (flags & 0x20))
            return -7;
        if (kind == 0 && (flags & 0x18))
            return -7;
        if (flags & 0x04) {
            if (!rd_u8(buf, len, &off, &a->mods)) return -6;
            if (a->mods & 0xF0) return -7;
        }
        if (flags & 0x08) {
            if (!rd_u16(buf, len, &off, &a->tap_usage)) return -6;
            if (a->tap_usage > 0xFF)
                return -7;
        }
        if (flags & 0x10) {
            if (!rd_u16(buf, len, &off, &a->tap_ms)) return -6;
        }
        if (map)
            map->action[layer][pos] = action;
    }
    if (off != len)
        return -8;                        /* trailing garbage rejected */
    return 0;
}

int ck_keymap_deserialize(const uint8_t *buf, size_t len, ck_keymap_t *map)
{
    int rc = decode(buf, len, NULL);
    if (rc)
        return rc;
    memset(map, 0, sizeof(*map));
    return decode(buf, len, map);
}
