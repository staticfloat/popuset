#include "receiver.h"

int listen_multicast(const uint16_t port, const char * group) {
    // Set up socket
    int sock = socket(AF_INET6, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("Unable to open socket");
        exit(1);
    }
    // bind to port
    struct sockaddr_in6 address = {AF_INET6, htons(port)};
    if (bind(sock, (struct sockaddr*)&address, sizeof(address)) < 0) {      
        perror("Unable to bind");
        exit(1);
    }
    struct ipv6_mreq group_struct;
    group_struct.ipv6mr_interface = 0;
    inet_pton(AF_INET6, group, &group_struct.ipv6mr_multiaddr);
    if (setsockopt(sock, IPPROTO_IPV6, IPV6_ADD_MEMBERSHIP, &group_struct, sizeof(group_struct)) < 0) {
        perror("Unable to join membership");
        exit(1);
    }

    //printf("Bound and listening on [%s]:%d\n", group, port);
    return sock;
}

uint64_t receive_time_packet(int sock) {
    struct sockaddr_in6 src_addr = {0};
    socklen_t addrlen = sizeof(src_addr);
    uint64_t recv_timestamp = 0;
    int bytes_read = recvfrom(sock, &recv_timestamp, sizeof(uint64_t), 0, (struct sockaddr *)&src_addr, &addrlen);
    if (bytes_read != sizeof(recv_timestamp)) {
        // IF we get EAGAIN, we're usually in the middle of graceful exit.
        // Return the sentinel value 0 and call it a day.
        if (errno == EAGAIN) {
            return 0;
        }

        // Otherwise, scream out.
        perror("unable to recvfrom()");
        exit(1);
    }

    // Immediately, as fast as we can, respond on port 1554:
    /*
    src_addr.sin6_port = htons(1554);
    uint64_t response_timestamps[2];
    response_timestamps[0] = recv_timestamp;
    response_timestamps[1] = gettime_ns();
    int bytes_sent = sendto(sock, &response_timestamps[0], sizeof(response_timestamps), 0, (struct sockaddr *)&src_addr, addrlen);
    if (bytes_sent != sizeof(response_timestamps)) {
        perror("Unable to sendto()");
        exit(1);
    }*/

    return recv_timestamp;
}

char recv_buffer[MAX_PACKET_SIZE];
struct popuset_packet_t packet;
struct popuset_packet_t * receive_audio_packet(int sock, uint8_t channel_idx) {
    int bytes_read = read(sock, recv_buffer, MAX_PACKET_SIZE);
    if (bytes_read == 0) {
        return NULL;
    }

    // Unpack first a timestamp as a `uint64_t`
    packet.timestamp = *((uint64_t *)&recv_buffer[0]);

    // Take a look at the channels given:
    uint8_t channels_in_packet = *((uint8_t *)&recv_buffer[sizeof(uint64_t)]);
    if (channel_idx > channels_in_packet) {
        printf("Assigned to channel %d, but only received %d\n");
        return NULL;
    }

    // Determine the offset to our data and our data length
    uint16_t * data_lens = (uint16_t *)(&recv_buffer[sizeof(uint64_t) + sizeof(uint8_t)]);
    uint16_t our_data_offset = sizeof(uint64_t) + sizeof(uint8_t) + sizeof(uint16_t)*channels_in_packet;
    for( uint8_t c = 0; c < channel_idx; ++c ) {
        our_data_offset += data_lens[c];
    }

    // Next, we extract the channel we attend to
    packet.decoded_data = decode_frame(recv_buffer + our_data_offset, data_lens[channel_idx]);
    return &packet;
}