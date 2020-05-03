#include "receiver.h"
#include "math.hpp"

#define TIMESTAMP_MEMORY    100
uint64_t t_rxs[TIMESTAMP_MEMORY] = {0};
uint64_t t_tx_locals[TIMESTAMP_MEMORY] = {0};
uint64_t t_tx_remotes[TIMESTAMP_MEMORY] = {0};
uint8_t accept_mask[TIMESTAMP_MEMORY] = {0};
int num_timestamps = 0;

// Various temporary buffers we need to keep around
int64_t t_props[TIMESTAMP_MEMORY] = {0};
int64_t t_lags[TIMESTAMP_MEMORY] = {0};

// The parameter we most want to know. :)
double clock_offset = 0.0;


uint64_t gettime_ns() {
    struct timespec tp;
    int err = clock_gettime(CLOCK_REALTIME, &tp);
    if (err < 0) {
        perror("clock_gettime() failed!");
        exit(-1);
    }
    return ((uint64_t)tp.tv_sec) * 1000000000 + tp.tv_nsec;
}

uint64_t gethosttime_ns() {
    return (uint64_t)(gettime_ns() + clock_offset);
}

void settime_ns(uint64_t time_ns) {
    struct timespec tp;
    tp.tv_sec = time_ns / 1000000000;
    tp.tv_nsec = time_ns % 1000000000;
    int err = clock_settime(CLOCK_REALTIME, &tp);
    if (err < 0) {
        perror("clock_settime() failed!");
        exit(-1);
    }
}

void insert_timestamps(uint64_t t_tx_local, uint64_t t_tx_remote, uint64_t t_rx) {
    for (int idx=TIMESTAMP_MEMORY-1; idx>0; --idx) {
        t_tx_locals[idx] = t_tx_locals[idx-1];
        t_tx_remotes[idx] = t_tx_remotes[idx-1];
        t_rxs[idx] = t_rxs[idx-1];
    }
    t_tx_locals[0] = t_tx_local;
    t_tx_remotes[0] = t_tx_remote;
    t_rxs[0] = t_rx;

    if (num_timestamps < TIMESTAMP_MEMORY)
        num_timestamps++;
}


void timesync_update(uint64_t t_tx_local, uint64_t t_tx_remote, uint64_t t_rx) {
    printf("timesync_update()!\n");
    // Only do timestamp calculation if we have enough points stored up
    if (num_timestamps > 20) {
        // First, we want to estimate minimum round-trip delay.  We do so by looking at the
        // minimal (t_tx_local, t_rx) pairs, then dividing by two and using that as our Tprop
        // estimate, representing the amount of time it takes for a packet to get from the
        // transmitter to use lowly receivers:
        int64_t Tprop_mu = 0;
        uint64_t Tprop_var = 0;
        subtract(t_rxs, t_tx_locals, num_timestamps, t_props);
        build_min_mask(t_props, num_timestamps, accept_mask);
        masked_mean_var(t_props, accept_mask, num_timestamps, &Tprop_mu, &Tprop_var);
        Tprop_mu /= 2;
        Tprop_var /= 4;

        // Calculate linear clock skew trend
        double skew_slope = 0.0;
        double skew_offset = 0.0;
        double skew_var = 0.0;
        subtract(t_rxs, t_tx_remotes, num_timestamps, t_lags);
        masked_linreg(t_rxs, t_lags, accept_mask, num_timestamps, &skew_slope, &skew_offset, &skew_var);

        // Use these estimate of Tprop and our clock skew to update our estimate of our clock offset.
        // We do the typical IIR update with memory parameter alpha:
        double skew_estimate = ((t_rx - t_rxs[num_timestamps - 1])/1000000000.0)*skew_slope + skew_offset;
        double alpha = num_timestamps*.99/TIMESTAMP_MEMORY;
        
        double old_clock_offset = clock_offset;
        clock_offset = alpha * clock_offset + (1.0 - alpha) * (skew_estimate + Tprop_mu);
        double clock_delta = clock_offset - old_clock_offset;

        // Take last t_tx_remotes, check to see how well we've approximated:
        printf("[TR] - clock_offset: %.3fms (delta: %.1fus) \n", clock_offset/1000000.0, clock_delta/1000.0f);
        printf("[TR]   - Tprop estimate: %.3fms  (+- %.3fus, %d full)\n", Tprop_mu/1000000.0, sqrt(Tprop_var)/1000.0, num_timestamps);
        printf("[TR]   - Clock skew: %.3fms, (+- %.1fus)\n", skew_estimate/1000000.0, sqrt(skew_var)/1000.0);
    }

    // Store these new values into our previous bags:
    insert_timestamps(t_tx_local, t_tx_remote, t_rx);
}

void timesync_clear() {
    num_timestamps = 0;
    clock_offset = 0.0;
}