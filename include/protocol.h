#ifndef PROTOCOL_H
#define PROTOCOL_H

#include <stdint.h>

#define MAX_PAYLOAD   1024
#define DEFAULT_PORT  9099
#define WINDOW_SIZE   8
#define TIMEOUT_MS    300
#define MAX_RETRIES   20

#define FLAG_SYN  0x1
#define FLAG_ACK  0x2
#define FLAG_FIN  0x4
#define FLAG_DATA 0x8

typedef struct __attribute__((packed)) {
    uint32_t seq;
    uint32_t ack;
    uint16_t flags;
    uint16_t length;   /* payload length in bytes */
    uint16_t checksum; /* over header (checksum field zeroed) + payload */
    uint16_t reserved; /* padding, keeps header 16-byte aligned */
} pkt_header_t;

typedef struct __attribute__((packed)) {
    pkt_header_t hdr;
    uint8_t payload[MAX_PAYLOAD];
} packet_t;

#define PKT_HEADER_SIZE sizeof(pkt_header_t)

#endif
