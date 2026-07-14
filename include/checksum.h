#ifndef CHECKSUM_H
#define CHECKSUM_H

#include <stddef.h>
#include <stdint.h>

/* RFC 1071 Internet checksum (16-bit ones-complement sum). */
uint16_t checksum16(const void *data, size_t len);

#endif
