#include "receiver.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

int listen_udp(const uint16_t port) {
    // Set up socket
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Unable to open socket");
        exit(1);
    }
    // bind to port
    struct sockaddr_in6 address = {AF_INET6, htons(port)};
    inet_pton(AF_INET6, "fe80::216:3eff:fec3:3c22", (void *)&socket_struct.sin6_addr.s6_addr);
    if (bind(sock, (struct sockaddr*)&address, sizeof(address)) < 0) {      
        perror("Unable to bind");
        exit(1);
    }
    return sock;
}

int listen_multicast(const uint16_t port, const char * group) {
    // Open a normal UDP socket bound to a port
    int sock = listen_udp(port);

    // Add on ipv6 multicast membership
    struct ipv6_mreq group_struct;
    group_struct.ipv6mr_interface = 0;
    inet_pton(AF_INET6, group, &group_struct.ipv6mr_multiaddr);
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &group_struct, sizeof(group_struct)) < 0) {
        perror("Unable to join membership");
        exit(1);
    }

    // Disable multiast loopback
    int opt = 0;
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_MULTICAST_LOOP, &opt, sizeof(opt)) < 0) {
        perror("Unable to disable multicast loopback");
        exit(1);
    }

    return sock;
}

void set_recv_timeout(int sock, uint64_t timeout_ns) {
    // Set recv timeout to 500ms
    struct timeval tv;
    tv.tv_sec = timeout_ns/(1000*1000*1000);
    tv.tv_usec = (timeout_ns/1000) % (1000*1000);
    if (setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) < 0) {
        perror("Unable to set receive timeout!");
        exit(1);
    }
}

uint64_t send_time_packet(int sock, const char * addr, uint16_t port) {
    struct sockaddr_in6 dst = {AF_INET6, htons(port)};
    inet_pton(AF_INET6, addr, &dst.sin6_addr);

    uint64_t t_tx = gettime_ns();
    if (sendto(sock, &t_tx, sizeof(uint64_t), 0, (sockaddr*)&dst, sizeof(dst)) < 0) {
        perror("unable to sendto()");
        exit(1);
    }
    return t_tx;
}

int receive_time_packet(int sock, uint64_t * t_tx_local, uint64_t * t_tx_remote, uint64_t * t_rx) {
    struct sockaddr_in6 src_addr = {0};
    socklen_t addrlen = sizeof(src_addr);
    uint64_t recv_timestamps[2];
    int bytes_read = recvfrom(sock, &recv_timestamps[0], sizeof(uint64_t)*2, 0, (struct sockaddr *)&src_addr, &addrlen);
    uint64_t _t_rx = gettime_ns();
    if (bytes_read != sizeof(recv_timestamps)) {
        // IF we get EAGAIN, we're usually in the middle of graceful exit.
        // Return the sentinel value 0 and call it a day.
        if (errno == EAGAIN) {
            return 0;
        }

        // Otherwise, scream out.
        perror("unable to recvfrom()");
        exit(1);
    }

    *t_tx_local = recv_timestamps[1];
    *t_tx_remote = recv_timestamps[0];
    *t_rx = _t_rx;
    return 1;
}

// We'll just assume none of our popuset packets compress so terribly that they are over 2048 bytes.
// Typical usage shows them to be ~120 bytes on average.
struct popuset_packet_header_t {
    uint64_t timestamp;
    uint16_t channels_included;
    uint16_t channel_offset;
};

// Ridiculously large receive buffer; most usage is <1kb, but let's be ready for huge numbers of channels in the future. ;)
uint8_t recv_buffer[64000];
void receive_audio_packet(int sock, uint8_t channel_idx) {
    int bytes_read = read(sock, recv_buffer, sizeof(recv_buffer));
    if (bytes_read <= 0) {
        return;
    }

    // First, unpack the header:
    popuset_packet_header_t * packet_header = (popuset_packet_header_t *)recv_buffer;

    // Check that our channel is included in the packet:
    if ((channel_idx < packet_header->channel_offset) ||
        (channel_idx >= (packet_header->channel_offset + packet_header->channels_included))) {
        return;
    }

    // Determine the offset to our data and our data length
    uint16_t * data_lens = (uint16_t *)(&recv_buffer[sizeof(popuset_packet_header_t)]);
    uint16_t our_data_offset = sizeof(popuset_packet_header_t) + sizeof(uint16_t)*packet_header->channels_included;
    for (uint16_t c = 0; c < (channel_idx - packet_header->channel_offset); ++c) {
        our_data_offset += data_lens[c];
    }

    // Uncomment this for debugging of data going missing/getting duplicated.  :P
    //uint32_t enc_hash = 0;
    //crc32(recv_buffer + our_data_offset, data_lens[channel_idx], &enc_hash);
    //printf("[R] Received packet %llx with payload length %d, payload hash %x\n", timestamp, data_lens[channel_idx], enc_hash);

    // Next, we extract the channel we attend to and queue a packet
    queue_packet(timestamp, recv_buffer + our_data_offset, data_lens[channel_idx]);
}

void request_timestamps(int sock, const char * addr, uint16_t port, uint64_t * timestamps, uint8_t num_timestamps) {
    struct sockaddr_in6 dst = {AF_INET6, htons(port)};
    inet_pton(AF_INET6, addr, &dst.sin6_addr);

    for (int idx=0; idx<num_timestamps; ++idx) {
        if (sendto(sock, &timestamps[idx], sizeof(uint64_t), 0, (sockaddr*)&dst, sizeof(dst)) < 0) {
            perror("unable to sendto()");
            exit(1);
        }
    }
}