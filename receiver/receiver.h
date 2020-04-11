#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <unistd.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/net_tstamp.h>
#include <alsa/asoundlib.h>
#include <opus/opus.h>
#include <time.h>
#include <math.h>
#include <pthread.h>
#include <signal.h>
#include <semaphore.h>

#define BUFFSIZE            480
#define SAMPLERATE          48000
#define MAX_PACKET_SIZE     2048

struct popuset_packet_t {
    // Timestamp of when to transmit first sample, expressed in us since the unix epoch
    uint64_t timestamp;

    // pointer to decoded data
    float * decoded_data;
};

// ALSA stuff
snd_pcm_t * open_device(const char * device);
void close_device(snd_pcm_t * handle);
long commit_buffer(snd_pcm_t * handle, const float * buffer);
long get_delay(snd_pcm_t * handle);
float rms(float * buffer);

// network stuff
int listen_multicast(const uint16_t port, const char * group);
uint64_t receive_time_packet(int socket);
struct popuset_packet_t * receive_audio_packet(int socket, uint8_t channel_idx);

// opus stuff
void create_decoder(void);
float * decode_frame(const char * encoded_data, unsigned int encoded_data_len);

// Time stuff
uint64_t gettime_ns();
void settime_ns(uint64_t time_ns);

// Utilities
struct popuset_packet_t * generate_sin_packet();
void init_sindata(float freq);
void catch_signal(void (*callback)(int), int signum);

extern sem_t continue_running;
void init_semaphore(sem_t * s);
void post_semaphore(sem_t * s);
int should_continue();