# udp-reliable-transfer

A reliable file-transfer protocol implemented from scratch on top of raw UDP
sockets in C — essentially a small, custom reimplementation of the core
reliability mechanisms TCP provides, plus a mode to simulate real network
loss so the reliability can be demonstrated (not just assumed).

## What it demonstrates

- Raw BSD socket programming (`socket`, `bind`, `sendto`/`recvfrom`) with a
  hand-rolled application-layer packet format, serialized in network byte
  order (`htonl`/`ntohl`).
- **Go-Back-N ARQ**: sequence numbers, a sliding send window (`WINDOW_SIZE`
  packets in flight), cumulative ACKs, and a single retransmission timer for
  the oldest unacked packet — if it fires, the whole window is resent.
- **Corruption detection**: an RFC 1071-style 16-bit Internet checksum over
  header + payload; a failed checksum is treated identically to a dropped
  packet.
- **Simulated network loss**: both client and server accept a `-d <prob>`
  drop probability, randomly discarding received packets before they're
  processed. This is what actually proves the retransmission logic works —
  transfers succeed and files come out byte-identical even at high loss
  rates.
- A three-way-ish handshake (SYN / SYN-ACK / ACK) carrying the filename, and
  a FIN / FIN-ACK teardown.

## Layout

```
include/protocol.h   wire format: packet header, flags, constants
include/checksum.h   checksum16()
include/common.h     send_pkt/recv_pkt (byte-order + checksum handled here)
src/server.c         receiver: in-order-only acceptance, cumulative ACKs
src/client.c         sender: sliding window, timeout -> go-back-N resend
test/run_test.sh     end-to-end transfer under induced loss, sha256 verify
```

## Build

```
make
```

## Run

Terminal 1:
```
./bin/server -p 9099 -d 0.1 -o /tmp/recv
```

Terminal 2:
```
./bin/client -h 127.0.0.1 -p 9099 -d 0.1 -f somefile.bin
```

`-d` is the simulated packet-loss probability (0.0–1.0) applied independently
on each side.

## Test

```
./test/run_test.sh [drop_prob] [size_kb]
# e.g.
./test/run_test.sh 0.2 1024
```

Generates a random file, transfers it through both endpoints with the given
loss rate, and asserts the sha256 of input and output match.

## Design notes / things to talk about

- **Why Go-Back-N over Selective Repeat**: GBN needs no receiver-side
  reordering buffer — the receiver only ever accepts the next expected
  sequence number and discards anything else, re-ACKing the last good one.
  Simpler to get correct in a weekend; the cost is resending the whole
  window on a single loss instead of just the missing packet.
- **ACK semantics**: `ack` field means "next sequence number I expect" (same
  convention as TCP), not "last one I got" — avoids an off-by-one/underflow
  special case at sequence 0.
- **Where loss is simulated**: after `recvfrom`, before checksum validation —
  so it's indistinguishable from real network loss to the rest of the logic,
  and corrupted/dropped packets are handled by the exact same code path
  (the sender's timeout).
- **Known limitations / stretch goals**: fixed window size (no congestion
  control / slow start), single in-flight connection (no multiplexing), no
  encryption or authentication of peers.
