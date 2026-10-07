#include "ck5200_update_protocol.h"

static uint32_t read_be32(const uint8_t *p) {
    return ((uint32_t)p[0] << 24) |
           ((uint32_t)p[1] << 16) |
           ((uint32_t)p[2] << 8) |
           (uint32_t)p[3];
}

static void write_be32(uint8_t *p, uint32_t value) {
    p[0] = (uint8_t)(value >> 24);
    p[1] = (uint8_t)(value >> 16);
    p[2] = (uint8_t)(value >> 8);
    p[3] = (uint8_t)value;
}

static size_t reply(uint8_t command, bool ok, uint32_t value, uint8_t response[8]) {
    response[0] = 0x08;
    response[1] = 0x02;
    response[2] = command;
    response[3] = ok ? 0x00 : 0x01;
    write_be32(&response[4], value);
    return 8;
}

size_t ck5200_update_handle(
    const uint8_t *request,
    size_t request_len,
    uint8_t response[8],
    const ck5200_update_ops_t *ops
) {
    if (!request || !ops || request_len < 2 || request[0] != request_len) {
        return 0;
    }

    const uint8_t command = request[1];
    switch (command) {
        case 0xA0:
            if (request_len != 2 || !ops->reboot) return 0;
            ops->reboot();
            return 0;

        case 0xA1: {
            if (request_len != 6 || !ops->begin) return 0;
            const uint32_t size = read_be32(&request[2]);
            uint32_t maximum = CK5200_UPDATE_MAX_IMAGE;
            const bool ok = size > 0 && size <= CK5200_UPDATE_MAX_IMAGE && ops->begin(size, &maximum);
            return reply(command, ok, maximum, response);
        }

        case 0xA2: {
            if (request_len < 7 || request_len > CK5200_UPDATE_MAX_CHUNK + 6 || !ops->write) return 0;
            const uint32_t offset = read_be32(&request[2]);
            const uint8_t length = (uint8_t)(request_len - 6);
            uint32_t accepted = offset;
            const bool ok = ops->write(offset, &request[6], length, &accepted);
            return reply(command, ok, accepted, response);
        }

        case 0xA3: {
            if (request_len != 6 || !ops->finish) return 0;
            const uint32_t size = read_be32(&request[2]);
            uint32_t committed = 0;
            const bool ok = ops->finish(size, &committed);
            return reply(command, ok, committed, response);
        }

        default:
            return 0;
    }
}
