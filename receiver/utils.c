#include "receiver.h"

// Setup signal handling
void catch_signal(void (*callback)(int), int signum) {
    struct sigaction sigIntHandler;
    sigIntHandler.sa_handler = callback;
    sigemptyset(&sigIntHandler.sa_mask);
    sigIntHandler.sa_flags = 0;
    sigaction(signum, &sigIntHandler, NULL);
}

void handle_sigterm(int signum) {
    printf("_ Shutting down forcefully...\n");
    exit(1);
}

void handle_sigint(int signum) {
    // If someone tries to do this again, exit immediately
    catch_signal(&handle_sigterm, SIGINT);

    printf("_ Shutting down gracefully...\n");
    post_semaphore(&continue_running);
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

// This CRC32 code gratefully cribbed from http://home.thep.lu.se/~bjorn/crc/
uint32_t crc32_for_byte(uint32_t r) {
  for(int j = 0; j < 8; ++j)
    r = (r & 1? 0: (uint32_t)0xEDB88320L) ^ r >> 1;
  return r ^ (uint32_t)0xFF000000L;
}

void crc32(const void *data, size_t n_bytes, uint32_t* crc) {
  static uint32_t table[0x100];
  if(!*table)
    for(size_t i = 0; i < 0x100; ++i)
      table[i] = crc32_for_byte(i);
  for(size_t i = 0; i < n_bytes; ++i)
    *crc = table[(uint8_t)*crc ^ ((uint8_t*)data)[i]] ^ *crc >> 8;
}