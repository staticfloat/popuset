#ifndef WAVFILE_H
#define WAVFILE_H

#include <stdio.h>
#include <stdint.h>

class WAVFile {
public:
    WAVFile(const char * filename, uint16_t num_channels, uint32_t samplerate);
    ~WAVFile();

    void closeFile();

    void writeData(const float * data, unsigned int num_samples);
private:
    void initHeader();
    void finalizeHeader();

    void writeInt(uint32_t val, uint8_t len);

    int fd;

    uint16_t num_channels;
    uint32_t samplerate;
    uint64_t total_samples;
};

#endif //WAVFIlE_H