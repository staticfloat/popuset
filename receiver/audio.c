#include "receiver.h"
#include <opus/opus.h>
#include <portaudio.h>

// Initialize Port streams for the given device
void * open_device() {
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
        (PaStreamCallback *)&mixer_callback,
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

    return (void *)stream;
}

void close_device(void * stream) {
    Pa_CloseStream((PaStream *)stream);
}

void create_decoder(void * dec) {
    int error = opus_decoder_init((OpusDecoder *)dec, SAMPLERATE, 1);
    if (error < 0) {
        fprintf(stderr, "failed to create decoder: %s\n", opus_strerror(error));
        exit(1);
    }
}

void snapshot_decoder(void * src, void * dst) {
    // There are a few times where this might happen (such as during FEC re-decoding),
    // so let's make life easy on ourselves and not invoke undefined behavior.
    if (src == dst)
        return;
    memcpy(dst, src, opus_decoder_get_size(1));
}

int decode_frame(void * dec, const unsigned char * encoded_data, unsigned int encoded_data_len, float * decoded_data, int fec) {
    int samples_decoded = opus_decode_float((OpusDecoder *)dec, encoded_data, encoded_data_len, decoded_data, BUFFSIZE, fec);
    if (samples_decoded != BUFFSIZE) {
        printf("samples_decoded was %d, instead of expected %d!\n", samples_decoded, BUFFSIZE);
    }
    return samples_decoded;
}