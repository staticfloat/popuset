#include "receiver.h"

OpusDecoder *dec;
float decoded_data[BUFFSIZE];
void create_decoder(void) {
    int error;
    
    dec = opus_decoder_create(SAMPLERATE, 1, &error);
    if (error < 0) {
        fprintf(stderr, "failed to create decoder: %s\n", opus_strerror(error));
        exit(1);
    }
}

float * decode_frame(const char * encoded_data, unsigned int encoded_data_len) {
    int samples_decoded = opus_decode_float(dec, encoded_data, encoded_data_len, decoded_data, BUFFSIZE, 0);
    if (samples_decoded != BUFFSIZE) {
        printf("samples_decoded = %d!\n", samples_decoded);
    }
    return decoded_data;
}