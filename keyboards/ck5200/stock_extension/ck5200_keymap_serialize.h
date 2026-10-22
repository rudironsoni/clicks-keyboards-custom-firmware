#ifndef CK5200_KEYMAP_SERIALIZE_H
#define CK5200_KEYMAP_SERIALIZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ck5200_keymap_engine.h"

/* Six-byte header plus 108 entries, each at most nine bytes. */
#define CK_KEYMAP_RECORD_MAX (6u + CK_LAYERS * CK_KEYS * 9u)

/* Serialize actions of `map` into `out` (capacity `cap`).
 * Returns the record length, or 0 for an invalid map or short buffer. */
size_t ck_keymap_serialize(const ck_keymap_t *map, uint8_t *out, size_t cap);

/* Load a serialized record into `map`, replacing its contents. Returns 0
 * on success; negative on any malformed input (magic, bounds, page,
 * activation, layer target, trailing bytes). Fail-closed: a rejected
 * record leaves `map` unchanged. Input and destination must not overlap. */
int ck_keymap_deserialize(const uint8_t *buf, size_t len, ck_keymap_t *map);

#endif /* CK5200_KEYMAP_SERIALIZE_H */
