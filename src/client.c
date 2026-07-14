#include <arpa/inet.h>
#include <libgen.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include "common.h"
#include "protocol.h"

/* Sender: Go-Back-N sliding window with a single retransmission timer
 * for the oldest unacked packet (base of the window). On timeout, the
 * whole window is resent -- classic GBN, simple and easy to reason about. */
int main(int argc, char **argv) {
    const char *server_ip = NULL;
    int port = DEFAULT_PORT;
    double drop_prob = 0.0;
    const char *filepath = NULL;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-h") == 0 && i + 1 < argc) {
            server_ip = argv[++i];
        } else if (strcmp(argv[i], "-p") == 0 && i + 1 < argc) {
            port = atoi(argv[++i]);
        } else if (strcmp(argv[i], "-d") == 0 && i + 1 < argc) {
            drop_prob = atof(argv[++i]);
        } else if (strcmp(argv[i], "-f") == 0 && i + 1 < argc) {
            filepath = argv[++i];
        } else {
            fprintf(stderr,
                    "usage: %s -h server_ip [-p port] [-d drop_prob] -f file\n",
                    argv[0]);
            return 1;
        }
    }
    if (!server_ip || !filepath) {
        fprintf(stderr,
                "usage: %s -h server_ip [-p port] [-d drop_prob] -f file\n",
                argv[0]);
        return 1;
    }

    srand((unsigned)time(NULL) ^ (unsigned)getpid());

    FILE *f = fopen(filepath, "rb");
    if (!f) {
        perror("fopen");
        return 1;
    }
    struct stat st;
    stat(filepath, &st);
    size_t filesize = (size_t)st.st_size;

    int sockfd = make_udp_socket();
    struct sockaddr_in server_addr = {0};
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);
    inet_pton(AF_INET, server_ip, &server_addr.sin_addr);
    socklen_t server_len = sizeof(server_addr);

    /* --- handshake: SYN carries the basename of the file --- */
    char pathbuf[1024];
    strncpy(pathbuf, filepath, sizeof(pathbuf) - 1);
    pathbuf[sizeof(pathbuf) - 1] = '\0';
    const char *fname = basename(pathbuf);

    packet_t syn = {0};
    syn.hdr.flags = FLAG_SYN;
    syn.hdr.length = (uint16_t)strlen(fname);
    memcpy(syn.payload, fname, syn.hdr.length);

    packet_t pkt;
    int handshake_done = 0;
    for (int tries = 0; tries < MAX_RETRIES && !handshake_done; tries++) {
        send_pkt(sockfd, &server_addr, &syn);
        int n = recv_pkt(sockfd, &server_addr, &server_len, &pkt, TIMEOUT_MS,
                          drop_prob);
        if (n >= 0 && (pkt.hdr.flags & FLAG_SYN) && (pkt.hdr.flags & FLAG_ACK)) {
            handshake_done = 1;
        }
    }
    if (!handshake_done) {
        fprintf(stderr, "[client] handshake failed, is the server running?\n");
        return 1;
    }
    packet_t finalack = {0};
    finalack.hdr.flags = FLAG_ACK;
    send_pkt(sockfd, &server_addr, &finalack);
    printf("[client] handshake complete, sending %s (%zu bytes)\n", fname,
           filesize);

    /* --- load whole file into memory and chop into MAX_PAYLOAD chunks --- */
    uint32_t total_chunks = (uint32_t)((filesize + MAX_PAYLOAD - 1) / MAX_PAYLOAD);
    if (total_chunks == 0) total_chunks = 0; /* empty file: send FIN only */

    uint8_t *filebuf = malloc(filesize > 0 ? filesize : 1);
    fread(filebuf, 1, filesize, f);
    fclose(f);

    uint32_t base = 0;
    uint32_t next_seq = 0;
    int retransmits = 0;

    while (base < total_chunks) {
        /* send everything the window currently allows */
        while (next_seq < base + WINDOW_SIZE && next_seq < total_chunks) {
            packet_t dp = {0};
            dp.hdr.flags = FLAG_DATA;
            dp.hdr.seq = next_seq;
            size_t offset = (size_t)next_seq * MAX_PAYLOAD;
            size_t chunklen = filesize - offset < MAX_PAYLOAD
                                   ? filesize - offset
                                   : MAX_PAYLOAD;
            dp.hdr.length = (uint16_t)chunklen;
            memcpy(dp.payload, filebuf + offset, chunklen);
            send_pkt(sockfd, &server_addr, &dp);
            next_seq++;
        }

        int n = recv_pkt(sockfd, &server_addr, &server_len, &pkt, TIMEOUT_MS,
                          drop_prob);
        if (n >= 0 && (pkt.hdr.flags & FLAG_ACK)) {
            if (pkt.hdr.ack > base) {
                base = pkt.hdr.ack;
            }
        } else {
            /* timeout, error, or non-ACK noise: go back N and resend the
             * whole outstanding window rather than assume progress. */
            retransmits++;
            next_seq = base;
        }
    }

    /* --- teardown --- */
    packet_t fin = {0};
    fin.hdr.flags = FLAG_FIN;
    int fin_acked = 0;
    for (int tries = 0; tries < MAX_RETRIES && !fin_acked; tries++) {
        send_pkt(sockfd, &server_addr, &fin);
        int n = recv_pkt(sockfd, &server_addr, &server_len, &pkt, TIMEOUT_MS,
                          drop_prob);
        if (n >= 0 && (pkt.hdr.flags & FLAG_FIN) && (pkt.hdr.flags & FLAG_ACK)) {
            fin_acked = 1;
        }
    }

    printf("[client] transfer complete: %u chunks, %d window retransmits\n",
           total_chunks, retransmits);

    free(filebuf);
    close(sockfd);
    return 0;
}
