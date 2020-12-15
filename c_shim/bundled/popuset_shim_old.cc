#include <portaudio.h>
#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <deque>
#define __STDC_FORMAT_MACROS
#include <inttypes.h>

#include "timely_mixer.h"

#define minimum(x, y) ((x) < (y) ? (x) : (y))

extern "C" {

// This struct is shared between the Julia side and C
typedef struct {
    double samplerate;
    int channels;
} pa_shim_info_t;


std::deque<TimestampedBuffer> buffer_list;
uint64_t last_presentation_end = 0;
uint64_t global_clock_offset = 0;


uint64_t seconds_to_samples(PaTime timestamp, double samplerate, uint64_t offset = global_clock_offset) {
    return (uint64_t)llround(timestamp*samplerate) + offset;
}

double samples_to_seconds(uint64_t samples, double samplerate, uint64_t offset = global_clock_offset) {
    return (samples - offset)/samplerate;
}

void pa_shim_synchronize_clocks(PaStream * stream, double samplerate, uint64_t curr_time_in_samples) {
    PaTime stream_time = Pa_GetStreamTime(stream);
    global_clock_offset = curr_time_in_samples - seconds_to_samples(stream_time, samplerate, 0);
    fprintf(stderr, "[sync] 0x%llx - %f*%f (0x%llx) == %lld\n", curr_time_in_samples, stream_time, samplerate, seconds_to_samples(stream_time, samplerate, 0), global_clock_offset);
}



void pa_shim_queue(float * data, int nframes, int nchannels, double samplerate, uint64_t presentation_time_in_samples) {
    // Don't bother to queue things that are behind the last presentation end:
    if (presentation_time_in_samples + nframes < last_presentation_end) {
        fprintf(stderr, "dropping %.1fs (horizon %.1fs, diff: %.1fs seconds)\n", samples_to_seconds(presentation_time_in_samples, samplerate), samples_to_seconds(last_presentation_end, samplerate), samples_to_seconds(last_presentation_end - presentation_time_in_samples, samplerate, 0));
        return;
    }

    // Copy data in to new TimestampedBuffer object
    float * new_data = (float *) malloc(sizeof(float)*nframes*nchannels);
    memcpy(new_data, data, sizeof(float)*nframes*nchannels);

    TimestampedBuffer buff = {new_data, (uint32_t)nframes, (uint8_t)nchannels, presentation_time_in_samples};
    buffer_list.push_back(buff);

    // Take this opportunity to cleanup any old buffers that we're not going to use anymore:
    while (buffer_list[0].timestamp + buffer_list[0].nframes < last_presentation_end) {
        TimestampedBuffer del_buff = buffer_list[0];
        buffer_list.pop_front();
        free(del_buff.data);
    }
}

int num_underruns = 0;
int num_consumed = 0;
bool in_underrun = true;

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
    uint64_t presentation_start = seconds_to_samples(timeInfo->outputBufferDacTime, info->samplerate);
    if(output) {
        uint64_t write_idx = 0;
        last_presentation_end = presentation_start + frameCount;

        // Find buffers to read from
        for (auto buff : buffer_list) {
            // If this buffer starts after our last presentation ending, skip out
            if (buff.timestamp >= last_presentation_end)
                break;
            // If this buffer falls anywhere within our presentation window, use it!
            if ((buff.timestamp + buff.nframes) >= (presentation_start + write_idx) && buff.timestamp < (presentation_start + frameCount)) {
                num_consumed += 1;
                uint64_t buff_offset = (presentation_start + write_idx) - buff.timestamp;
                int read_len = minimum(buff.nframes - buff_offset, last_presentation_end - (presentation_start + write_idx));
                //fprintf(stderr, "[+%llu] use buffer (%d/%d #%d): 0x%llx\n", buff_offset, read_len, buff.nframes, info->channels, buff.timestamp);

                // Copy and transpose
                for (int idx=0; idx<read_len; idx++ ) {
                    for (int cidx=0; cidx<info->channels; ++cidx ) {
                        ((float *)output)[(write_idx + idx)*info->channels + cidx] = buff.data[cidx*buff.nframes + buff_offset + idx];
                    }
                }
                //memcpy((float *)output + write_idx*info->channels, buff.data + buff_offset*info->channels, read_len*sizeof(float)*info->channels);
                write_idx += read_len;
            }
            if (write_idx == frameCount)
                break;
        }

        // Taper in this buffer if we were in underrun, to avoid issues.
        if (in_underrun && write_idx > 0) {
            fprintf(stderr, "easing in over %d samples...\n", frameCount);
            float * p = (float *)output;
            for (int idx=0; idx<frameCount; ++idx) {
                for (int cidx=0; cidx<info->channels; cidx++) {
                    ((float *)output)[idx*info->channels + cidx] = idx*((float *)output)[idx*info->channels + cidx]/frameCount;
                }
            }
        }
        if (write_idx < frameCount) {
            num_underruns += 1;
            in_underrun = true;
            //fprintf(stderr, "\runderrun by %" PRIu64 " samples, asked for %" PRIu64 " samples, %d buffers queued       \r", frameCount - write_idx, frameCount, buffer_list.size());
            memset((float *)output + write_idx*info->channels, 0, (frameCount - write_idx)*sizeof(float)*info->channels);
        } else {
            in_underrun = false;
        }
    }
    fprintf(stderr, "\r\x1b[K[%d] samples, %d consumed, %d underruns (%d), %d buffers, horizon: %.1f\r", frameCount, num_consumed, num_underruns, in_underrun, buffer_list.size(), samples_to_seconds(last_presentation_end, info->samplerate));
    return paContinue;
}

} // extern "C"
