#ifndef COMMON_H
#define COMMON_H

#include <netinet/in.h>
#include <stddef.h>
#include "protocol.h"

/* Creates a UDP socket. Exits the process on failure. */
int make_udp_socket(void);

/*
 * Sends a packet. Caller fills seq/ack/flags/length/payload in host order;
 * this computes the checksum and puts header fields on the wire in network
 * order. Returns bytes sent, or -1 on error.
 */
ssize_t send_pkt(int sockfd, const struct sockaddr_in *addr, packet_t *pkt);

/*
 * Waits up to timeout_ms for a packet. Packets are randomly dropped with
 * probability drop_prob (simulated loss, [0.0, 1.0)) and packets that fail
 * checksum verification are silently discarded, both counting against the
 * remaining timeout budget -- exactly like real network loss/corruption.
 *
 * On success, pkt is filled in host order and payload length is returned
 * (0 is a valid success result -- ACK/SYN/FIN packets carry no payload).
 * Returns -1 on timeout, -2 on hard socket error.
 */
int recv_pkt(int sockfd, struct sockaddr_in *addr, socklen_t *addrlen,
             packet_t *pkt, int timeout_ms, double drop_prob);

#endif
