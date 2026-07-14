#include <arpa/inet.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "protocol.h"

/* Receiver: Go-Back-N, only accepts in-order data, ACKs cumulatively. */
int main(int argc, char **argv) {
    int port = DEFAULT_PORT;
    double drop_prob = 0.0;
    const char *outdir = ".";

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            drop_prob = atof(argv[++i]);
        } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            outdir = argv[++i];
        } else {
            fprintf(stderr,
                    "usage: %s [-p port] [-d drop_prob] [-o outdir]\n",
                    argv[0]);
            return 1;
        }
    }

    srand((unsigned)time(NULL));

    int sockfd = make_udp_socket();
    struct sockaddr_in my_addr = {0};
    my_addr.sin_family = AF_INET;
    my_addr.sin_addr.s_addr = INADDR_ANY;
    my_addr.sin_port = htons(port);
    if (bind(sockfd, (struct sockaddr *)&my_addr, sizeof(my_addr)) < 0) {
        perror("bind");
        return 1;
    }
    printf("[server] listening on UDP port %d (drop_prob=%.2f)\n", port,
           drop_prob);

    struct sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    packet_t pkt;

    /* --- handshake: wait for SYN carrying the filename --- */
    char filename[MAX_PAYLOAD] = {0};
    for (;;) {
        int n = recv_pkt(sockfd, &client_addr, &client_len, &pkt, 5000,
                          drop_prob);
        if (n > 0 && (pkt.hdr.flags & FLAG_SYN)) {
            memcpy(filename, pkt.payload, pkt.hdr.length);
            filename[pkt.hdr.length] = '\0';
            break;
        }
    }
    printf("[server] SYN received, incoming file: %s\n", filename);

    packet_t synack = {0};
    synack.hdr.flags = FLAG_SYN | FLAG_ACK;
    synack.hdr.length = 0;
    send_pkt(sockfd, &client_addr, &synack);

    /* wait for the client's final ACK of the handshake */
    for (;;) {
        int n = recv_pkt(sockfd, &client_addr, &client_len, &pkt, 2000,
                          drop_prob);
        if (n >= 0 && (pkt.hdr.flags & FLAG_ACK) && !(pkt.hdr.flags & FLAG_SYN)) {
            break;
        }
        if (n == -1) { /* client's ACK got lost, resend SYN-ACK and keep waiting */
            send_pkt(sockfd, &client_addr, &synack);
        }
    }
    printf("[server] handshake complete\n");

    char outpath[1024];
    snprintf(outpath, sizeof(outpath), "%s/%s", outdir, filename);
    FILE *f = fopen(outpath, "wb");
    if (!f) {
        perror("fopen");
        return 1;
    }

    uint32_t expected_seq = 0;
    size_t total_bytes = 0;
    int done = 0;

    while (!done) {
        int n = recv_pkt(sockfd, &client_addr, &client_len, &pkt, 5000,
                          drop_prob);
        if (n == -1) {
            printf("[server] timed out waiting for data, aborting\n");
            break;
        }
        if (n == -2) break; /* hard socket error */

        if (pkt.hdr.flags & FLAG_FIN) {
            packet_t finack = {0};
            finack.hdr.flags = FLAG_FIN | FLAG_ACK;
            finack.hdr.ack = expected_seq;
            send_pkt(sockfd, &client_addr, &finack);
            done = 1;
            break;
        }

        if (pkt.hdr.seq == expected_seq) {
            fwrite(pkt.payload, 1, pkt.hdr.length, f);
            total_bytes += pkt.hdr.length;
            expected_seq++;
        }
        /* Go-Back-N: ack.ack means "next sequence number I expect", i.e.
         * cumulative ACK of everything before it -- same semantics as TCP.
         * Sent whether this packet was the one we needed or a future one
         * we had to discard, so the sender always knows where to resume. */
        packet_t ack = {0};
        ack.hdr.flags = FLAG_ACK;
        ack.hdr.ack = expected_seq;
        send_pkt(sockfd, &client_addr, &ack);
    }

    fclose(f);
    close(sockfd);
    printf("[server] done, wrote %zu bytes to %s\n", total_bytes, outpath);
    return 0;
}
