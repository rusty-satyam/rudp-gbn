#include "checksum.h"

uint16_t checksum16(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t *)data;
    uint32_t sum = 0;

    for (size_t i = 0; i + 1 < len; i += 2) {
        uint16_t word = (uint16_t)(bytes[i] << 8) | bytes[i + 1];
        sum += word;
    }
    if (len & 1) {
        sum += (uint16_t)(bytes[len - 1] << 8);
    }

    while (sum >> 16) {
        sum = (sum & 0xFFFF) + (sum >> 16);
    }

    return (uint16_t)(~sum);
}
