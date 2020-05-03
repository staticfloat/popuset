#include "receiver.h"

// Eventually these should be configured, not hardcoded
#define SPEAKER_GROUP_IDX       0
#define CHANNEL_IDX             0
#define AUDIO_PORT              5040
#define TIMESYNC_PORT           1554
#define SPEAKER_GROUP_ADDR(idx) "ff12:5041::1337:" STRINGIFY(idx)

uint64_t last_t_tx = 0;
void * time_ping_thread(void * sock_ptr) {
    printf("[TX] - STARTING\n");
    int sock = *(int *)sock_ptr;

    while (should_continue()) {
        uint64_t t_local = gettime_ns();

        // Send ping to time beacon on multicast group, once every 500ms or so
        if (last_t_tx + 500*1000*1000 < t_local) {
            // Let `send_time_packet()` give us back a time as close to the network as possible
            last_t_tx = send_time_packet(sock, SPEAKER_GROUP_ADDR(SPEAKER_GROUP_IDX), TIMESYNC_PORT);
            //printf("[TX] - Transmitted 0x%llx\n", last_t_tx);
        }

        // Sleep for a (slightly) random amount of time, so that we are never packet storming the master
        usleep((195 + rand()%10)*1000);
    }
    printf("[TX] - CLOSING!\n");
    return NULL;
}

void * time_pong_thread(void * sock_ptr) {
    // We're going to sit and listen until we get a time announcement packet,
    // then we're going to set the system time to that, since our system time
    // is probably wacked out.  Even if we're off by a few ms, that's fine, we
    // just want to be mostly correct.  We will maintain an internal time estimate
    // that is much more precise.
    printf("[TR] - STARTING\n");
    int sock = *(int *)sock_ptr;

    while (should_continue()) {
        uint64_t t_tx_local = 0, t_tx_remote = 0, t_rx = 0;
        if (receive_time_packet(sock, &t_tx_local, &t_tx_remote, &t_rx) == 0) {
            printf("[TR] can't receive time packet!\n");
            continue;
        }

        // Ignore out-of-order packets
        if (t_tx_local != last_t_tx) {
            printf("[TR] %d != %d\n", t_tx_local, last_t_tx);
            continue;
        }

        // Do our statistical update of our estimated temporal drift
        timesync_update(t_tx_local, t_tx_remote, t_rx);
    }
    printf("[TR] - CLOSING\n");
    return NULL;
}

void * audio_thread(void * sock_ptr) {
    printf("[A] - STARTING\n");
    int sock = *(int *)sock_ptr;
    PaStream * audio_device = open_device();

    while (should_continue()) {
        // Receive the next audio packet
        struct popuset_packet_t * packet = receive_audio_packet(sock, CHANNEL_IDX);
        if (packet == NULL)
            continue;
    }

    close_device(audio_device);
    printf("[A] - CLOSING\n");
    return NULL;
}

int main(void) {
    // Setup CTRL-C catching, before we launch threads:
    catch_signal(&handle_sigint, SIGINT);
    catch_signal(&handle_sigterm, SIGTERM);
    init_semaphore(&continue_running);
    create_decoder();
    init_mixer();

    // Open sockets (we do so here, so that we can close 'em on the thread's stupid faces)
    printf("[M] - Opening sockets on multicast group %s\n", SPEAKER_GROUP_ADDR(SPEAKER_GROUP_IDX));
    int time_socket = listen_udp(TIMESYNC_PORT);
    int audio_socket = listen_multicast(AUDIO_PORT, SPEAKER_GROUP_ADDR(SPEAKER_GROUP_IDX));

    // Launch off those beautiful, gorgeous threads.
    pthread_t time_ping_thread_handle;
    pthread_create(&time_ping_thread_handle, NULL, &time_ping_thread, &time_socket);
    pthread_t time_pong_thread_handle;
    pthread_create(&time_pong_thread_handle, NULL, &time_pong_thread, &time_socket);
    pthread_t audio_thread_handle;
    pthread_create(&audio_thread_handle, NULL, &audio_thread, &audio_socket);
    //pthread_t mix_thread_handle;
    // /pthread_create(&mix_thread_handle, NULL, &mix_thread, NULL);
    
    // Wait for someone to give us a SIGINT
    sem_wait(&continue_running);
    printf("[M] - Wrapping up tasks...\n");

    // Close up shop
    shutdown(time_socket, SHUT_RDWR);
    shutdown(audio_socket, SHUT_RDWR);

    pthread_join(time_ping_thread_handle, NULL);
    pthread_join(time_pong_thread_handle, NULL);
    pthread_join(audio_thread_handle, NULL);
    //pthread_join(mix_thread_handle, NULL);
    printf("[M] - All tasks finished!\n");
    return 0;
}