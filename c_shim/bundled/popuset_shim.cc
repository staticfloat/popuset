#include <portaudio.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <deque>
#include <mutex>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#define minimum(x, y) ((x) < (y) ? (x) : (y))

extern "C" {

// Global behavioral switches
volatile int verbose = 0;
volatile int print_stats = 0;
volatile int mute = 0;

// Global statistics
typedef struct {
    uint64_t buffer_underruns;
    uint64_t total_consumed;
    uint8_t in_underrun;
    uint32_t buffers_queued;
} pa_shim_stats_t;
pa_shim_stats_t stats = {0, 0, 0, 0};

// This struct is shared between the Julia side and C
typedef struct {
    double samplerate;
    uint8_t channels;
    void (*output_callback)();
} pa_shim_info_t;

// This is an internal representation of an audio buffer, including
// how much of the buffer the audio callback has consumed.
typedef struct {
    void * data;
    uint32_t num_frames;
    uint32_t frames_consumed;
    uint8_t num_channels;
} buffer_t;


std::mutex list_lock;
std::deque<buffer_t> buffer_list;
std::deque<buffer_t> free_list;
double last_presentation_end = 0.0;

// Return the device-local timestamp of what we need to be mixing for.
double pa_shim_get_mix_horizon(pa_shim_info_t * info) {
    // Take last presentation time, add all the samples between us and the current horizon.
    uint32_t num_frames = 0;
    const std::lock_guard<std::mutex> lock(list_lock);
    for (auto buff : buffer_list) {
        num_frames += buff.num_frames - buff.frames_consumed;
    }
    return last_presentation_end + num_frames/info->samplerate;
}

void pa_shim_set_verbosity(int verbosity_level) {
    verbose = verbosity_level;
}

void pa_shim_set_print_stats(int new_print_stats) {
    print_stats = new_print_stats;
}

const pa_shim_stats_t * pa_shim_get_stats(void) {
    const std::lock_guard<std::mutex> lock(list_lock);
    stats.buffers_queued = (uint32_t)buffer_list.size();
    return &stats;
}
void pa_shim_set_mute(int new_mute) {
    mute = new_mute;
}

void free_buff(buffer_t & buff) {
    free(buff.data);
    buff.data = NULL;
    buff.num_frames = 0;
    buff.frames_consumed = 0;
    buff.num_channels = 0;
}

void pa_shim_queue(float * data, uint32_t nframes, uint8_t nchannels) {
    // First, check to see if we've got anything in the free list that could be repurposed
    // and not need to `malloc()` something new.
    buffer_t new_buff = {NULL, 0, 0};
    {
        const std::lock_guard<std::mutex> lock(list_lock);
        while (!free_list.empty()) {
            buffer_t buff = free_list.front();
            free_list.pop_front();

            // If it matches what we're looking for, use it
            if (buff.num_frames == nframes && buff.num_channels == nchannels) {
                new_buff = buff;
                new_buff.frames_consumed = 0;
                break;
            } else {
                free_buff(buff);
            }
        }
    }

    // If we didn't find anything in the free list, grudgingly allocate a new one
    if (new_buff.data == NULL) {
        new_buff.num_frames = nframes;
        new_buff.num_channels = nchannels;
        new_buff.frames_consumed = 0;
        new_buff.data = (void *) malloc(sizeof(float)*nframes*nchannels);
    }

    memcpy(new_buff.data, (const void *)data, sizeof(float)*nframes*nchannels);
    const std::lock_guard<std::mutex> lock(list_lock);
    buffer_list.push_back(new_buff);
    if (verbose >= 2) {
        fprintf(stderr, "\nQueue'ing %d frames with %d channels\n", new_buff.num_frames, new_buff.num_channels);
    }
}

// We calculate average RMS across channels
float * calc_rms(float * x, uint32_t len, uint8_t channels) {
    static float rms[255] = {0.0f};

    // Clear previous state
    for (uint8_t c_idx=0; c_idx < channels; ++c_idx) {
        rms[c_idx] = 0.0f;
    }

    // Calculate sum(x.^2) for each channel
    for (uint32_t idx=0; idx<len; ++idx) {
        for (uint8_t c_idx=0; c_idx < channels; ++c_idx) {
            float x_idx = x[idx*channels + c_idx];
            rms[c_idx] += x_idx*x_idx;
        }
    }

    // Final division by len, followed by sqrtf
    for (uint8_t c_idx=0; c_idx < channels; ++c_idx) {
        rms[c_idx] = sqrtf(rms[c_idx]/len);
    }
    return rms;
}

/*
 * This routine will be called by the PortAudio engine when audio is needed.
 * It may called at interrupt level on some machines so don't do anything that
 * could mess up the system like calling malloc() or free().
 */
int pa_shim_processcb(const void *input, void *output,
                      unsigned long frameCount,
                      const PaStreamCallbackTimeInfo* timeInfo,
                      PaStreamCallbackFlags statusFlags,
                      void *userData)
{
    pa_shim_info_t *info = (pa_shim_info_t *)userData;
    /* Record the ending time of this presentation. */
    last_presentation_end = timeInfo->outputBufferDacTime + frameCount/info->samplerate;
    unsigned long write_idx = 0;
    if (output != NULL) {
        float * float_output = (float *)output;

        // Shove buffers into its hungry maw until its sated
        while (write_idx < frameCount && !buffer_list.empty()) {
            buffer_t buff;
            {
                const std::lock_guard<std::mutex> lock(list_lock);
                buff = buffer_list.front();
                buffer_list.pop_front();
            }

            if (buff.num_channels != info->channels) {
                // Drop this buffer if it doesn't even match the stream in number of channels
                free_list.push_front(buff);
                if (verbose >= 1) {
                    fprintf(stderr, "\nBuffer channels (%d) != stream channels (%d)!\n", buff.num_channels, info->channels);
                }
                continue;
            }

            // Figure out how much of this buffer to consume
            unsigned long frames_to_consume = minimum(frameCount - write_idx, buff.num_frames - buff.frames_consumed);
            uint32_t bytes_to_write = sizeof(float)*frames_to_consume*buff.num_channels;
            if (!mute) {
                // Small nod to efficiency to not do unnecessary work if we're muted
                memcpy(float_output + write_idx*info->channels, (float *)buff.data + buff.frames_consumed*info->channels, bytes_to_write);
            }
            buff.frames_consumed += frames_to_consume;
            write_idx += frames_to_consume;
            stats.total_consumed += frames_to_consume;

            // If we consumed it all, push it onto the free list
            if (buff.frames_consumed >= buff.num_frames) {
                const std::lock_guard<std::mutex> lock(list_lock);
                free_list.push_front(buff);
            } else {
                const std::lock_guard<std::mutex> lock(list_lock);
                buffer_list.push_front(buff);
            }
        }

        // Taper in this buffer if we were in underrun, to avoid issues.
        if (stats.in_underrun && write_idx > 0) {
            // Taper by up to 4ms
            uint32_t taper_amnt = minimum(frameCount, (uint32_t)(info->samplerate/200));
            if (verbose >= 1) {
                fprintf(stderr, "easing in over %d samples...\n", taper_amnt);
            }
            for (int idx=0; idx<taper_amnt; ++idx) {
                for (int cidx=0; cidx<info->channels; cidx++) {
                    float_output[idx*info->channels + cidx] = idx*float_output[idx*info->channels + cidx]/taper_amnt;
                }
            }
        }

        // If we're now in underrun, zero out the rest of the buffer
        if (write_idx < frameCount) {
            stats.buffer_underruns += 1;
            stats.in_underrun = 1;
            if (verbose >= 1) {
                const std::lock_guard<std::mutex> lock(list_lock);
                fprintf(stderr, "\nunderrun by %llu samples, asked for %llu samples, %d buffers queued\n", frameCount - write_idx, frameCount, buffer_list.size());
            }
            memset(float_output + write_idx*info->channels, 0, (frameCount - write_idx)*sizeof(float)*info->channels);
        } else {
            stats.in_underrun = 0;
        }

        // If we're muted, just zero everything out.  We still do all the above work because
        // there are side-effects (such as increasing frames_consumed)
        if (mute) {
            memset(float_output, 0, frameCount*info->channels*sizeof(float));
        }
    }

    // Only print stats if the user has requested it
    if (print_stats) {
        float * rms = calc_rms((float *)output, frameCount, info->channels);
        char rms_str[128] = {0};
        int rms_offset = 0;
        for (int c_idx=0; c_idx<info->channels; ++c_idx) {
            rms_offset += sprintf(rms_str + rms_offset, "%.3f", rms[c_idx]);
            if (c_idx != info->channels - 1) {
                rms_offset += sprintf(rms_str + rms_offset, ", ");
            }
        }
        const std::lock_guard<std::mutex> lock(list_lock);
        fprintf(stderr, "\r\x1b[K[%d/%d] samples %d channels (rms %s), %lu underruns (%d), %llu consumed, %d buffers queued\r", write_idx, frameCount, info->channels, rms_str, stats.buffer_underruns, stats.in_underrun, stats.total_consumed, buffer_list.size());
    }

    // Notify that we just mixed
    info->output_callback();

    return paContinue;
}

} // extern "C"
