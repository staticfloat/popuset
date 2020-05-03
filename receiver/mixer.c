#include "receiver.h"

#define NUM_PACKETS         100

popuset_packet_t * packets[NUM_PACKETS];
int packet_read_idx = 0;
int packet_write_idx = 0;

void init_mixer() {
    // Initialize our set of packet objecfts
    for (int idx=0; idx<NUM_PACKETS; ++idx) {
        packets[idx] = (popuset_packet_t *) malloc(sizeof(popuset_packet_t));
    }
}

int packets_queued() {
    int val = (packet_write_idx - packet_read_idx)%NUM_PACKETS;
    while (val < 0)
        val += NUM_PACKETS;
    return val;
}

uint64_t last_timestamp = 0;
popuset_packet_t * queue_packet(uint64_t timestamp, const uint8_t * enc_data, const int enc_datalen) {
    // We check for the space of two packets here, since we might need to insert an FEC'ed packet.
    if (packets_queued() >= NUM_PACKETS - 2) {
        printf("DROP %lld\n", timestamp);
        return NULL;
    }
    
    // If there is a timestamp jump, then let's try and recover from packet loss by invoking fec decode
    if (timestamp != last_timestamp + 10000000) {
        printf("Detected jump of %u ns (%d packets); invoking FEC to reclaim at least one!\n", (unsigned int)(timestamp - last_timestamp), (int)((timestamp - last_timestamp - 5000000)/10000000));
        popuset_packet_t * new_packet = packets[packet_write_idx];
        packet_write_idx = (packet_write_idx + 1)%NUM_PACKETS;
        new_packet->timestamp = timestamp - 10000000;
        decode_frame(enc_data, enc_datalen, &new_packet->decoded_data[0], 1);
    }

    // Now, decode the actual packet
    popuset_packet_t * new_packet = packets[packet_write_idx];
    packet_write_idx = (packet_write_idx + 1)%NUM_PACKETS;
    new_packet->timestamp = timestamp;
    decode_frame(enc_data, enc_datalen, &new_packet->decoded_data[0]);
    
    //printf("[A] - Queued a packet with timestamp 0x%llx\n", timestamp);
    last_timestamp = new_packet->timestamp;
    return new_packet;
}

const popuset_packet_t * next_packet() {
    if (packets_queued() == 0) {
        return NULL;
    }
    const popuset_packet_t * p = packets[packet_read_idx];
    uint64_t curr_time = gethosttime_ns();
    uint64_t buff_time = BUFFSIZE*(1000000000UL/SAMPLERATE);

    // If we're too far ahead of the presentation time of the first packet, then return NULL
    if (curr_time < p->timestamp - buff_time ) {
        printf("[0x%llx] Waiting, %.1fms ahead (thresh: %.1fms)\n", p->timestamp, (p->timestamp - curr_time)/1000000.0f, buff_time/1000000.0f);
        return NULL;
    }

    // We consume this packet.
    packet_read_idx = (packet_read_idx + 1)%NUM_PACKETS;

    // If we're too far behind the presentation time of the first packet, dump it, and recurse:
    if (curr_time > p->timestamp + buff_time ) {
        printf("[0x%llx] Dropping, %.1fms behind (thresh: %.1fms)\n", p->timestamp, (curr_time - p->timestamp)/1000000.0f, buff_time/1000000.0f);
        return next_packet();
    }
    return p;
}

int in_underrun = 1;
int mixer_callback( const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData ) {
    if (framesPerBuffer != BUFFSIZE) {
        printf("framesPerBuffer: %d != %d!\n", framesPerBuffer, BUFFSIZE);
        exit(1);
    }

    float * out = (float *)outputBuffer;

    // Try to get next packet; if we can't, underrun.
    const popuset_packet_t * p = next_packet();
    if (p == NULL) {
        if (in_underrun != 1) {
            printf(" ---> UNDERRUN! <----\n");
        }
        in_underrun = 1;
        memset(out, 0, sizeof(float)*BUFFSIZE);
    } else {
        in_underrun = 0;
        //printf("[MX] - mixing with %d buffers in reserve\r", packets_queued());
        //fflush(stdout);
        for (int idx=0; idx<BUFFSIZE; ++idx ) {
            out[idx] = p->decoded_data[idx];
        }
    }
    return 0;
}