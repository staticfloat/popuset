#include "receiver.h"

// Eventually these should be configured, not hardcoded
#define STRINGIFY(x) #x
#define SPEAKER_GROUP_IDX   0
#define CHANNEL_IDX         0
#define AUDIO_PORT          5040
#define TIMESYNC_PORT       1554
#define SPEAKER_GROUP_ADDR(idx)  "ff12:5040::1337:" STRINGIFY(idx)

#define TIMESTAMP_MEMORY    100
uint64_t t_rxs[TIMESTAMP_MEMORY] = {0};
uint64_t t_txs[TIMESTAMP_MEMORY] = {0};
uint8_t reject_mask[TIMESTAMP_MEMORY] = {0};
uint16_t num_timestamps = 0;

void insert_timestamps(uint64_t t_tx, uint64_t t_rx) {
    for (int idx=TIMESTAMP_MEMORY-1; idx>0; --idx) {
        t_rxs[idx] = t_rxs[idx-1];
        t_txs[idx] = t_txs[idx-1];
    }
    t_rxs[0] = t_rx;
    t_txs[0] = t_tx;

    if (num_timestamps < TIMESTAMP_MEMORY)
        num_timestamps++;
}

int meandiff_reject_outliers(uint64_t * x, uint16_t len, int64_t * mu_out, uint64_t * var_out) {
    if (len < 10) {
        // If we don't have enough data, then we have CERTAINTY on our answer
        *mu_out = 0;
        *var_out = 0;
        return 0;
    }

    // First, calculate initial mu and variance
    int64_t mu = 0;
    uint64_t var = 0;
    for (int idx=1; idx<len; ++idx) {
        mu += x[idx-1] - x[idx];
    }
    mu /= (len - 1);
    for (int idx=1; idx<len; ++idx) {
        int64_t d = (x[idx-1] - x[idx]) - mu;
        var += d*d;
    }
    var /= (len - 2);

    // Next, use these initial values to reject outliers; we define outliers as
    // anything that contributes more than 10x its expected variance ("expected"
    // variance is sample variance / number of samples)
    uint64_t var_thresh = 10*(var / (len - 1));
    int num_rejected = 0;
    for (int idx=1; idx<len; ++idx) {
        int64_t d = (x[idx-1] - x[idx]) - mu;
        if (d*d <= var_thresh) {
            //printf("[%03d] d^2: 0x%llx (<= 0x%llx)\n", idx, d*d, var_thresh);
            reject_mask[idx] = 0;
        } else {
            //printf("[%03d] d^2: 0x%llx (>  0x%llx)\n", idx, d*d, var_thresh);
            reject_mask[idx] = 1;
            num_rejected += 1;
        }
    }
    if (num_rejected >= len - 3) {
        printf("WE REJECTED TOO MANY?!\n");
        *mu_out = 0;
        *var_out = 0;
        return 0;
    }

    int64_t mu_clean = 0;
    uint64_t var_clean = 0;
    for (int idx=1; idx<len; ++idx) {
        if (reject_mask[idx] == 0) {
            mu_clean += x[idx-1] - x[idx];
        }
    }
    mu_clean /= (len - 1 - num_rejected);

    for (int idx=1; idx<len; ++idx) {
        int64_t d = (x[idx-1] - x[idx]) - mu_clean;
        if (reject_mask[idx] == 0) {
            var_clean += d*d;
        }
    }
    var_clean /= (len - 2 - num_rejected);

    // Finally, store into mu_ptr and var_ptr:
    *mu_out = mu_clean;
    *var_out = var_clean;
    return num_rejected;
}

void timesync_update(uint64_t t_tx, uint64_t t_rx) {
    // We model the relationship between the tx and rx timestamps as follows:
    //     t_rx[N] = t_tx[N] + clock_offset[N] + channel_delay[N]
    //
    // clock_offset: a slowly-moving variable that can be positive or negative
    // channel_delay: a quickly-moving variable that is lower-bounded by physics
    //
    //
    // We assume that t_tx increments in steps of (close to) T:
    //     t_tx[N+1] - t_tx[N] = T + T_gc (bimodal) + T_err (T_err small, gaussianish)
    //
    // We take T_mu = mean(diff(t_tx)) to get an estimate of T, (throwing out outliers),
    // giving us a cheap prediction of t_tx
    //     t_tx_hat[N] = t_tx[N-1] + T_mu

    // Next, calculate T estimate:
    int64_t T_mu;
    uint64_t T_var;
    int num_rejected = meandiff_reject_outliers(t_txs, num_timestamps, &T_mu, &T_var);
    //printf("[T] T period estimate: %.3fms +- %.3fus (used %d/%d)\n", T_mu/1000000.0f, sqrt(T_var)/1000.0f, num_timestamps - num_rejected, num_timestamps);

    // Take last t_txs, check to see how well we've approximated:
    uint64_t t_tx_hat = T_mu + t_txs[0];
    int64_t t_err = (int64_t)(t_tx_hat - t_tx);
    printf("[T] - t_tx estimate error: %.3fus  (t_tx_hat certainty: %.3fus, used %d/%d)\n", t_err/1000.0f, sqrt(T_var)/1000.0f, num_timestamps - num_rejected, num_timestamps);

    // Store these new values into our previous bags:
    insert_timestamps(t_tx, t_rx);
}

void * time_thread(void * sock_ptr){
    // We're going to sit and listen until we get a time announcement packet,
    // then we're going to set the system time to that, since our system time
    // is probably wacked out.  Even if we're off by a few ms, that's fine, we
    // just want to be mostly correct.  We will maintain an internal time estimate
    // that is much more precise.
    printf("[T] - STARTING\n");
    int sock = *(int *)sock_ptr;

    uint64_t last_time_set = 0;
    while (should_continue()) {
        uint64_t t_tx = receive_time_packet(sock);
        uint64_t t_rx = gettime_ns();
        if (t_tx == 0)
            continue;

        // We are going to set the local clock once every 4 hours.  This is to
        // ensure that our clock drift isn't out of hand, since without a
        // hardware clock these things walk around by like 100ms/hour.
        int64_t set_tdiff = (int64_t)(t_tx - last_time_set);
        if (set_tdiff > (uint64_t)4*60*60*1000000000) {
            settime_ns(t_tx);
            last_time_set = t_tx;
            printf("[T] - Set local clock (time since last set: 0x%llx)\n", set_tdiff);
        }

        timesync_update(t_tx, t_rx);

        //double tdiff = ((int64_t)(gettime_ns() - t_tx))/1000000.0;
        //printf("[T] - Received timestamp [%llx], diff: %.1fms\n", t_tx, tdiff);
    }
    printf("[T] - CLOSING\n");
    return NULL;
}

void * audio_thread(void * sock_ptr) {
    printf("[A] - STARTING\n");
    int sock = *(int *)sock_ptr;
    snd_pcm_t * audio_device = open_device("default");
    create_decoder();

    while (should_continue()) {
        struct popuset_packet_t * packet = receive_audio_packet(sock, CHANNEL_IDX);
        if (packet == NULL)
            continue;

        long delay = commit_buffer(audio_device, packet->decoded_data);
        uint64_t curr_time = gettime_ns();
        printf("[A] - Got a packet with timestamp 0x%llx (diff 0x%llx), rms %.3f, committed with delay %ld\n", packet->timestamp, curr_time - packet->timestamp, rms(packet->decoded_data), delay);
    }

    printf("[A] - CLOSING\n");
    close_device(audio_device);
    return NULL;
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

int main(void) {
    // Setup CTRL-C catching, before we launch threads:
    catch_signal(&handle_sigint, SIGINT);
    catch_signal(&handle_sigterm, SIGTERM);
    init_semaphore(&continue_running);

    // Open sockets (we do so here, so that we can close 'em on the thread's stupid faces)
    printf("[M] - Opening sockets on multicast group %s\n", SPEAKER_GROUP_ADDR(SPEAKER_GROUP_IDX));
    int time_socket = listen_multicast(TIMESYNC_PORT, SPEAKER_GROUP_ADDR(SPEAKER_GROUP_IDX));
    int audio_socket = listen_multicast(AUDIO_PORT, SPEAKER_GROUP_ADDR(SPEAKER_GROUP_IDX));

    // Launch off those beautiful, gorgeous threads.
    pthread_t time_thread_handle;
    pthread_create(&time_thread_handle, NULL, &time_thread, &time_socket);
    pthread_t audio_thread_handle;
    pthread_create(&audio_thread_handle, NULL, &audio_thread, &audio_socket);
    
    // Wait for someone to give us a SIGINT
    sem_wait(&continue_running);
    printf("[M] - Wrapping up tasks...\n");

    // Close up shop
    shutdown(time_socket, SHUT_RDWR);
    shutdown(audio_socket, SHUT_RDWR);

    pthread_join(time_thread_handle, NULL);
    pthread_join(audio_thread_handle, NULL);
    printf("[M] - All tasks finished!\n");
    return 0;
}