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