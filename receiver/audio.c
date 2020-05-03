#include "receiver.h"

/*
snd_pcm_t * open_device(const char * device) {
    snd_pcm_t * handle = NULL;
    int err = snd_pcm_open(&handle, device, SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        printf("Playback open error: %s\n", snd_strerror(err));
        exit(1);
    }

    const int device_bufflens = 4;
    err = snd_pcm_set_params(
        handle,
        SND_PCM_FORMAT_FLOAT_LE,
        SND_PCM_ACCESS_RW_INTERLEAVED,
        1,
        SAMPLERATE,
        0,
        device_bufflens*(BUFFSIZE*1000000/SAMPLERATE)
    );
    if (err < 0) {
        printf("Playback set format: %s\n", snd_strerror(err));
        exit(1);
    }

    snd_async_handler_t *pcm_callback;
    err = snd_async_add_pcm_handler(&pcm_callback, handle, (snd_async_callback_t)&mixer_callback, NULL);
    if (err < 0) {
        printf("PCM handler: %s\n", snd_strerror(err));
        exit(1);
    }

    // Write some initial zeros to get things started
    commit_zerobuff(handle);
    commit_zerobuff(handle);

    err = snd_pcm_start(handle);
    if (err < 0) {
        printf("PCM start: %s\n", snd_strerror(err));
        exit(1);
    }
    return handle;
}*/

// Initialize Port streams for the given device
PaStream * open_device() {
    PaError err = Pa_Initialize();
    if (err != paNoError) {
        fprintf(stderr, "Could not initialize portaudio: %s\n", Pa_GetErrorText(err));
        exit(1);
    }

    PaStream * stream = NULL;
    err = Pa_OpenDefaultStream(
        &stream,
        0,
        1,
        paFloat32,
        SAMPLERATE,
        BUFFSIZE,
        &mixer_callback,
        NULL
    );

    if (err != paNoError) {
        fprintf(stderr, "Could not open stream: %s\n", Pa_GetErrorText(err));
        exit(1);
    }

    // Start the stream, spawning off a thread to run the callbacks from.
    err = Pa_StartStream(stream);
    if (err != paNoError) {
        fprintf(stderr, "Could not start stream: %s\n", Pa_GetErrorText(err));
        exit(1);
    }

    return stream;
}

void close_device(PaStream * stream) {
    Pa_CloseStream(stream);
    stream = NULL;
}

/*
long get_committed_samples(snd_pcm_t * handle) {
    int err;
    long num_samples = 0;
    
    err = snd_pcm_delay(handle, &num_samples);
    if (err < 0 && err != -EPIPE) {
        printf("Get delay: %s\n", snd_strerror(err));
        return 0;
    }
    return num_samples;
}

/*
void commit_buffer(snd_pcm_t * handle, const float * buffer) {
    int written = 0;
    while (written < BUFFSIZE) {
        int frames_written = snd_pcm_writei(handle, buffer + written, BUFFSIZE - written);
        // If -EAGAIN, then just try again
        if (frames_written == -EAGAIN)
            continue;
        if (frames_written == -EPIPE) {
            // underrun
            int err = snd_pcm_prepare(handle);
            if (err < 0) {
                printf("PCM recovery preparation: %s\n", snd_strerror(err));
                exit(1);
            }
            continue;
        }
        if (frames_written < 0) {
            printf("snd_pcm_writei(): %s\n", snd_strerror(frames_written));
            return;
        }
        written += frames_written;
    }
}
*/

/*
void commit_buffer(snd_pcm_t * handle, const float * buffer) {
    int written = 0;
    while (written < BUFFSIZE) {
        int frames_written = snd_pcm_writei(handle, buffer + written, BUFFSIZE - written);
        if (frames_written < 0)
            frames_written = snd_pcm_recover(handle, frames_written, 0);
        if (frames_written < 0) {
            printf("snd_pcm_writei(): %s\n", snd_strerror(frames_written));
            return;
        }
        written += frames_written;
    }
}

const float zero_buff[BUFFSIZE] = {0.0};
void commit_zerobuff(snd_pcm_t * handle) {
    commit_buffer(handle, zero_buff);
}
*/

OpusDecoder *dec;
void create_decoder(void) {
    int error = 0;
    dec = opus_decoder_create(SAMPLERATE, 1, &error);
    if (error < 0) {
        fprintf(stderr, "failed to create decoder: %s\n", opus_strerror(error));
        exit(1);
    }
}

int decode_frame(const unsigned char * encoded_data, unsigned int encoded_data_len, float * decoded_data, int fec) {
    int samples_decoded = opus_decode_float(dec, encoded_data, encoded_data_len, decoded_data, BUFFSIZE, fec);
    if (samples_decoded != BUFFSIZE) {
        printf("samples_decoded was %d, instead of expected %d!\n", samples_decoded, BUFFSIZE);
    }
    return samples_decoded;
}