#include "receiver.h"

// Open a blocking PCM device with samplerate 48KHz, 
snd_pcm_t * open_device(const char * device) {
    snd_pcm_t * handle = NULL;
    int err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        printf("Playback open error: %s\n", snd_strerror(err));
        exit(1);
    }

    err = snd_pcm_set_params(
        handle,
        SND_PCM_FORMAT_FLOAT_LE,
        SND_PCM_ACCESS_RW_INTERLEAVED,
        1,
        SAMPLERATE,
        0,
        BUFFSIZE*1000000/SAMPLERATE
    );
    if (err < 0) {
        printf("Playback set format: %s\n", snd_strerror(err));
        exit(1);
    }

    err = snd_pcm_prepare(handle);
    if (err < 0) {
        printf("PCM preparation: %s\n", snd_strerror(err));
        exit(1);
    }
    return handle;
}

void close_device(snd_pcm_t * handle) {
    snd_pcm_close(handle);
    handle = NULL;
}

long commit_buffer(snd_pcm_t * handle, const float * buffer) {
    int frames_written = snd_pcm_writei(handle, buffer, BUFFSIZE);
    if (frames_written < 0) {
        frames_written = snd_pcm_recover(handle, frames_written, 0);
    }
    if (frames_written < 0) {
        printf("snd_pcm_writei(): %s\n", snd_strerror(frames_written));
    } else if (frames_written != BUFFSIZE) {
        printf("Only wrote %d frames!\n");
    }
    return get_delay(handle);
}

long get_delay(snd_pcm_t * handle) {
    int err;
    long delay = 0;
    
    err = snd_pcm_delay(handle, &delay);
    if (err < 0) {
        printf("Get delay: %s\n", snd_strerror(err));
        return 0;
    }
    return delay;
}

// Calculate the root mean square of a buffer
float rms(float * buffer) {
    float accum = 0.0f;
    for (int i=0; i<BUFFSIZE; ++i) {
        accum += buffer[i]*buffer[i];
    }
    return sqrtf(accum/BUFFSIZE);
}