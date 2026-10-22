#ifndef CK5200_KEYMAP_SERIALIZE_H
#define CK5200_KEYMAP_SERIALIZE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ck5200_keymap_engine.h"

/* Serialize non-empty actions of `map` into `out` (capacity `cap`).
 * Returns the record length, or 0 if the buffer is too small. */
size_t ck_keymap_serialize(const ck_keymap_t *map, uint8_t *out, size_t cap);

/* Load a serialized record into `map`, replacing its contents. Returns 0
 * on success; negative on any malformed input (magic, bounds, page,
 * activation, layer target, trailing bytes). Fail-closed: a rejected
 * record leaves `map` zeroed, never partially loaded. */
int ck_keymap_deserialize(const uint8_t *buf, size_t len, ck_keymap_t *map);

#endif /* CK5200_KEYMAP_SERIALIZE_H */
