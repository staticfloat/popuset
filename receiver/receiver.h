#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>

#define NUM_PACKETS         20
#define BUFFSIZE            480
#define MAX_ENC_LEN         1024
#define SAMPLERATE          48000
#define STRINGIFY(x)        #x


struct popuset_packet_t {
    // Timestamp of when to present first sample, expressed in ns since the unix epoch
    uint64_t timestamp;

    // decoded PCM audio data
    uint8_t encoded_data[MAX_ENC_LEN];
    uint16_t encoded_datalen;
    float decoded_data[BUFFSIZE];

    // If this packet was decoded through FEC, we clone the decoder just before FEC decode,
    // and if we have the opportunity to replace it, we do so with this decoder state.
    uint8_t fec;
    void * decoder_state;
};

// PortAudio/Opus stuff
void * open_device();
void close_device(void * handle);
void create_decoder(void * dec);
int decode_frame(void * dec, const unsigned char * encoded_data, unsigned int encoded_data_len, float * decoded_data, int fec=0);
void snapshot_decoder(void * src, void * dst);

// mixing
void init_mixer();
int packets_queued();
void queue_packet(uint64_t timestamp, const uint8_t * enc_data, const int enc_datalen);
const popuset_packet_t * next_packet();
int mixer_callback( const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const void* timeInfo, unsigned long statusFlags, void *userData);
uint8_t scan_fec_packets(uint64_t * fec_timestamps);
void gc_packets();

// network stuff
int listen_udp(const uint16_t port);
int listen_multicast(const uint16_t port, const char * group);
void set_recv_timeout(int sock, uint64_t timeout_ns);
uint64_t send_time_packet(int sock, const char * addr, uint16_t port);
int receive_time_packet(int sock, uint64_t * t_tx_local, uint64_t * t_tx_remote, uint64_t * t_rx);
void receive_audio_packet(int socket, uint8_t channel_idx);

// Time stuff
uint64_t gettime_ns();
uint64_t gethosttime_ns();
void settime_ns(uint64_t time_ns);
void timesync_update(uint64_t t_tx_local, uint64_t t_tx_remote, uint64_t t_rx);
void timesync_clear();

// Utilities
void catch_signal(void (*callback)(int), int signum);
void handle_sigterm(int signum);
void handle_sigint(int signum);

extern sem_t continue_running;
void init_semaphore(sem_t * s);
void post_semaphore(sem_t * s);
int should_continue();