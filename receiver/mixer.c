#include "receiver.h"
#include <portaudio.h>
#include <opus/opus.h>

popuset_packet_t * freelist[NUM_PACKETS+1] = {NULL};
popuset_packet_t * packets[NUM_PACKETS+1] = {NULL};
void * latest_decoder = NULL;

void init_mixer() {
    // Initialize our set of packet objects, but all initially in the freelist.
    // Each packet has a location for an opus decoder state as well.
    const size_t opus_dec_size = opus_decoder_get_size(1);
    for (int idx=0; idx<NUM_PACKETS; ++idx) {
        freelist[idx] = (popuset_packet_t *) malloc(sizeof(popuset_packet_t));
        memset(freelist[idx], 0, sizeof(popuset_packet_t));
        freelist[idx]->decoder_state = malloc(opus_dec_size);
        memset(freelist[idx]->decoder_state, 0, opus_dec_size);
    }

    // Initialize a single, progenitor decoder that will be copied by literally everything.
    latest_decoder = malloc(opus_dec_size);
    create_decoder(latest_decoder);
}

int packets_queued() {
    for (int idx=0; idx<NUM_PACKETS; ++idx) {
        if (packets[idx] == NULL) {
            return idx;
        }
    }
    return NUM_PACKETS;
}

static void shift_down(popuset_packet_t ** list, int start_idx) {
    for (int idx=start_idx; idx<NUM_PACKETS-1; ++idx) {
        list[idx] = list[idx+1];
    }
    list[NUM_PACKETS-1] = NULL;
}
static void shift_up(popuset_packet_t ** list, int start_idx) {
    for (int idx=NUM_PACKETS; idx>start_idx; --idx) {
        list[idx] = list[idx-1];
    }
}

static popuset_packet_t * pop_freelist() {
    popuset_packet_t * retval = freelist[0];
    int idx = 0;
    for (; idx<NUM_PACKETS; ++idx) {
        freelist[idx] = freelist[idx+1];
    }
    return retval;
}

int find_insertion_point(popuset_packet_t ** packets, uint64_t timestamp) {
    int best_idx = 0;
    for(int insert_idx=0; insert_idx<NUM_PACKETS; ++insert_idx) {
        // If we've reached the end of the list, break! This is where we'll insert.
        if (packets[insert_idx] == NULL)
            return insert_idx;

        // If the next timestamp is going to be greater than us, return this one!
        if (packets[insert_idx+1] != NULL && packets[insert_idx+1]->timestamp > timestamp)
            return insert_idx;
    }
    return NUM_PACKETS;
}

int insert_packet(popuset_packet_t ** packets, int insert_idx, uint64_t timestamp,
                  const uint8_t * enc_data, const int enc_datalen, void * dec, int fec) {

    // Construct a new packet, decoding it as FEC from this current packet.
    popuset_packet_t * p = pop_freelist();

    // If we don't have any free space, FREAK OUT!
    if (p == NULL) {
        //printf("DROP %llx\n", timestamp);
        return insert_idx;
    }

    // Scooch anything currently in packets up (will transparently drop excess packets)
    shift_up(packets, insert_idx);
    
    p->timestamp = timestamp;
    p->fec = fec;
    p->encoded_datalen = enc_datalen;
    memcpy(p->encoded_data, enc_data, enc_datalen);

    snapshot_decoder(dec, p->decoder_state);
    decode_frame(dec, p->encoded_data, p->encoded_datalen, p->decoded_data, p->fec);

    packets[insert_idx] = p;
    printf("[Q] Inserted %llx at %d\n", p->timestamp, insert_idx);
    return insert_idx + 1;
}

static void dump_packets() {
    printf("packets:\n");
    for(int idx=0; idx<NUM_PACKETS; ++idx) {
        if (packets[idx] == NULL)
            break;

        printf(" - [%d] %llx\n", idx, packets[idx]->timestamp);
    }
}

uint64_t last_played_timestamp = 0;
void queue_packet(uint64_t timestamp, const uint8_t * enc_data, const int enc_datalen) {
    // This new packet is going to fit somewhere in our list of packets, find the point:
    int insert_idx = find_insertion_point(packets, timestamp);
    if (insert_idx == NUM_PACKETS) {
        printf("OOM DROP %lld\n", timestamp);
        return;
    }

    // Compare timestamps; if we've already received this packet, check to see if it was a previously
    // FEC-decoded one; if so, decode it (and all subsequent FEC'ed frames) afresh with the stored
    // decoder state!  If it wasn't FEC-decoded, just, drop it as it contains no new information.
    if (packets[insert_idx] != NULL && packets[insert_idx]->timestamp == timestamp) {
        popuset_packet_t * p = packets[insert_idx];
        if (p->fec == 0)
            return;

        // Overwrite the encoded data with new encoded data, set this packet as non-FEC.
        memcpy(p->encoded_data, enc_data, enc_datalen);
        p->encoded_datalen = enc_datalen;
        p->fec = 0;

        // re-decode this frame (and all subsequent frames), since we now have it fresh
        void * dec = p->decoder_state;
        for (int idx=insert_idx; idx<NUM_PACKETS; ++idx) {
            p = packets[insert_idx];
            if (p == NULL)
                break;
            
            snapshot_decoder(dec, p->decoder_state);
            decode_frame(dec, p->encoded_data, p->encoded_datalen, p->decoded_data, p->fec);
        }

        // We're done re-decoding, just return.
        return;
    }

    // If we haven't received this packet yet, let's figure out how many packets we've missed
    uint64_t last_timestamp = last_played_timestamp;
    if (insert_idx > 0 && packets[insert_idx-1] != NULL)
        last_timestamp = packets[insert_idx-1]->timestamp;
    int num_skipped_packets = (int)((timestamp - last_timestamp - 5000000)/10000000) - 1;

    // For each skipped packet, use FEC to decode a best-effort packet, but we'll request a resend.
    if (last_timestamp != 0 && num_skipped_packets > 0) {
        printf("Detected jump of %d packets; invoking FEC to reclaim them!\n", num_skipped_packets);
        // If there was a packet here before, use that state as the starting point.  Otherwise,
        // default to the latest decoder state.
        void * dec = latest_decoder;
        if (packets[insert_idx] != NULL)
            dec = packets[insert_idx]->decoder_state;

        for (int idx=0; idx<num_skipped_packets; ++idx) {
            // Insert a new packet with FEC using the calculated appropriate timestamp.
            uint64_t fec_timestamp = last_timestamp + ((uint64_t)idx*10000000);
            insert_idx = insert_packet(packets, insert_idx, fec_timestamp, enc_data, enc_datalen, dec, 1);
        }

        dump_packets();
    }

    // Finally, decode this actual packet. We do an initial decode using the current decoder, but because
    // Opus is a stateful codec, and we request old packets when we miss them, we might want to re-decode
    // this packet.  We get the best of both worlds by storing the current decoder state, (in case we need
    // to re-decode the _next_ packet) and decoding now, but allowing a re-decode in the future.
    insert_packet(packets, insert_idx, timestamp, enc_data, enc_datalen, latest_decoder, 0);
}

void gc_packets() {
    // clean up old packets by pushing everything before the last played timestamp onto the freelist:
    int num_packets_gced = 0;
    while (packets[0] != NULL && packets[0]->timestamp <= last_played_timestamp) {
        shift_up(freelist, 0);
        freelist[0] = packets[0];
        shift_down(packets, 0);
        num_packets_gced += 1;
    }
    //if (num_packets_gced > 0) {
    //    printf("[GC] Cleared %d packets before timestamp %llx\n", num_packets_gced, last_played_timestamp);
    //}
}

const popuset_packet_t * next_packet() {
    uint64_t curr_time = gethosttime_ns();
    uint64_t buff_time = BUFFSIZE*(5500000000UL/SAMPLERATE);
    popuset_packet_t * best_packet = NULL;
    for (int idx=0; idx<NUM_PACKETS; ++idx) {
        popuset_packet_t * p = packets[idx];

        // We ran out of packets.  :(
        if (p == NULL)
            break;

        // If we've already played up to this point, just skip ahead immediately.  We never re-play a buffer.
        if (p->timestamp <= last_played_timestamp)
            continue;

        // If we've entered a zone where the next packet's presentation time is too far ahead, break out.
        if (curr_time < p->timestamp - buff_time) {
            printf("[MX] [0x%llx] Waiting on 0x%llx, %.1fms ahead (thresh: %.1fms)\n", curr_time, p->timestamp, (p->timestamp - curr_time)/1000000.0f, buff_time/1000000.0f);
            dump_packets();
            break;
        }

        // If we're within the goldilocks zone, return it immediately.
        if (curr_time < p->timestamp + buff_time) {
            best_packet = p;
            break;
        }

        // Otherwise, we have a packet that is a little bit too far into the past. Keep track of it, better to play
        // old audio than nothing at all, (but of course, only if it's newer than the last played timestamp)
        if (curr_time > p->timestamp + buff_time) {
            best_packet = p;
            //printf("[MX] [0x%llx] Skipping, %.1fms behind (thresh: %.1fms)\n", p->timestamp, (curr_time - p->timestamp)/1000000.0f, buff_time/1000000.0f);
            continue;
        }

        // Otherwise, let's return this packet!
        best_packet = p;
        break;
    }

    //if (best_packet != NULL) {
    //    printf("[MX] [%llx] playing %llx\n", curr_time, best_packet->timestamp);
    //}
    return best_packet;
}

uint8_t scan_fec_packets(uint64_t * fec_timestamps) {
    uint8_t num_fec_packets = 0;
    for (int idx=0; idx<NUM_PACKETS; ++idx) {
        if (packets[idx] == NULL)
            break;
        if (packets[idx]->fec == 1) {
            fec_timestamps[num_fec_packets] = packets[idx]->timestamp;
            num_fec_packets +=1;
        }
    }
    return num_fec_packets;
}

int in_underrun = 1;
int mixer_callback( const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const void* timeInfo, unsigned long statusFlags, void *userData ) {
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
        memset(out, 0, sizeof(float)*BUFFSIZE);
        if (in_underrun == 1) {
            // If we've been in underrun for two buffers in a row, there's probably
            // something super wrong and we should reset last_played_timestamp so that
            // we can sync up nicely again in queue_packet().
            last_played_timestamp = 0;
        }
        in_underrun = 1;
    } else {
        in_underrun = 0;
        //printf("[MX] - [%llx] with %d buffers in reserve\n", p->timestamp, packets_queued());
        //fflush(stdout);
        for (int idx=0; idx<BUFFSIZE; ++idx ) {
            out[idx] = p->decoded_data[idx];
        }
        if (p->timestamp - last_played_timestamp != 10000000) {
            printf("[MX] Jump in timestamps: %lld\n", p->timestamp - last_played_timestamp);
        }
        // Set the last played timestamp to this one:
        last_played_timestamp = p->timestamp;
    }
    return 0;
}