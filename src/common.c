#define _POSIX_C_SOURCE 200809L

#include "common.h"
#include "checksum.h"

#include <arpa/inet.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

int make_udp_socket(void) {
    int fd = socket(AF_INET, SOCK_DGRAM, 0);
    if (fd < 0) {
        perror("socket");
        exit(1);
    }
    return fd;
}

static void hton_hdr(pkt_header_t *h) {
    h->seq = htonl(h->seq);
    h->ack = htonl(h->ack);
    h->flags = htons(h->flags);
    h->length = htons(h->length);
    h->checksum = htons(h->checksum);
}

static void ntoh_hdr(pkt_header_t *h) {
    h->seq = ntohl(h->seq);
    h->ack = ntohl(h->ack);
    h->flags = ntohs(h->flags);
    h->length = ntohs(h->length);
    h->checksum = ntohs(h->checksum);
}

ssize_t send_pkt(int sockfd, const struct sockaddr_in *addr, packet_t *pkt) {
    packet_t wire = *pkt;
    wire.hdr.checksum = 0;
    wire.hdr.checksum = checksum16(&wire, PKT_HEADER_SIZE + pkt->hdr.length);
    hton_hdr(&wire.hdr);

    size_t total = PKT_HEADER_SIZE + pkt->hdr.length;
    return sendto(sockfd, &wire, total, 0, (const struct sockaddr *)addr,
                   sizeof(*addr));
}

static long elapsed_ms(struct timespec *start) {
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (now.tv_sec - start->tv_sec) * 1000 +
           (now.tv_nsec - start->tv_nsec) / 1000000;
}

int recv_pkt(int sockfd, struct sockaddr_in *addr, socklen_t *addrlen,
             packet_t *pkt, int timeout_ms, double drop_prob) {
    struct timespec start;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (;;) {
        long left = timeout_ms - elapsed_ms(&start);
        if (left <= 0) return -1; /* timeout */

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(sockfd, &fds);
        struct timeval tv;
        tv.tv_sec = left / 1000;
        tv.tv_usec = (left % 1000) * 1000;

        int r = select(sockfd + 1, &fds, NULL, NULL, &tv);
        if (r < 0) {
            perror("select");
            return -2;
        }
        if (r == 0) return -1; /* timeout */

        packet_t wire;
        ssize_t n = recvfrom(sockfd, &wire, sizeof(wire), 0,
                             (struct sockaddr *)addr, addrlen);
        if (n < (ssize_t)PKT_HEADER_SIZE) continue;

        /* Simulated network loss: drop before we even look at it. */
        if (drop_prob > 0.0 &&
            (double)rand() / (double)RAND_MAX < drop_prob) {
            continue;
        }

        ntoh_hdr(&wire.hdr);
        uint16_t received_chk = wire.hdr.checksum;
        wire.hdr.checksum = 0;
        uint16_t computed = checksum16(&wire, PKT_HEADER_SIZE + wire.hdr.length);
        if (computed != received_chk) {
            continue; /* corrupt packet, treat as lost */
        }

        *pkt = wire;
        return wire.hdr.length;
    }
}
