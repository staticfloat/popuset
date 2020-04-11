#include "receiver.h"


struct popuset_packet_t sinpack;
float sindata[BUFFSIZE];
struct popuset_packet_t * generate_sin_packet() {
    sinpack.timestamp = 0;
    sinpack.decoded_data = &sindata[0];
    return &sinpack;
}

void init_sindata(float freq) {
    for (int i=0; i<BUFFSIZE; ++i) {
        sindata[i] = 0.8f*sinf(freq*i*2*M_PI/480.0f);
    }
}

// Setup signal handling
void catch_signal(void (*callback)(int), int signum) {
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = callback;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(signum, &sigIntHandler, NULL);
}

// Semaphore utility functions
sem_t continue_running;
void init_semaphore(sem_t * s) {
    if (sem_init(s, 0, 0) < 0) {
        perror("IUnable to initialize semaphore!");
        exit(-1);
    }
}
void post_semaphore(sem_t * s) {
    if (sem_post(s) < 0) {
        perror("Unable to post semaphore!");
        exit(-1);
    }
}
int should_continue() {
    if (sem_trywait(&continue_running) < 0) {
        // Tell them to continue if the semaphore is still held
        return errno == EAGAIN;
    } else {
        // If the semaphore is not held, then don't continue, but also allow others
        // to see that the semaphore is not held!
        post_semaphore(&continue_running);
        return 0;
    }
}