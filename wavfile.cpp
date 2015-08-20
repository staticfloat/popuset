#include "wavfile.h"
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>


WAVFile::WAVFile(const char * filename, uint16_t num_channels, uint32_t samplerate) {
    this->fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if( this->fd == -1 ) {
        fprintf(stderr, "Could not open \"%s\"; %s\n", filename, strerror(errno));
        throw "Could not open file";
    }

    this->num_channels = num_channels;
    this->samplerate = samplerate;
    this->total_samples = 0;

    // Let's initialize as much of the header as we can
    this->initHeader();
}

WAVFile::~WAVFile() {
    this->closeFile();
}

void WAVFile::closeFile() {
    this->finalizeHeader();
    close(this->fd);
}

void WAVFile::writeInt(uint32_t val, uint8_t len) {
    switch( len ) {
        case 1: {
            uint8_t x = (uint8_t)val;
            write(this->fd, &x, len);
        }   break;
        case 2: {
            uint16_t x = (uint16_t)val;
            write(this->fd, &x, len);
        }   break;
        case 4:
            write(this->fd, &val, len);
            break;
        default:
            fprintf(stderr, "Ummm.... what are you trying to do here?\n");
            break;
    }
}

void WAVFile::initHeader() {
    // Write out the WAV file header, even though total_samples is total_crap; we'll fix it later
    write(this->fd, "RIFF", 4);
    writeInt(0, 4);
    write(this->fd, "WAVE", 4);

    // Write format chunk
    write(this->fd, "fmt ", 4);
    writeInt(16, 4);
    writeInt(3, 2);
    writeInt(this->num_channels, 2);
    writeInt(this->samplerate, 4);
    writeInt((this->samplerate*4*this->num_channels)/8, 4);
    writeInt((this->num_channels*4)/8, 2);
    writeInt(32, 2);

    // Write data chunk
    write(this->fd, "data", 4);
    writeInt(0, 4);
}

void WAVFile::finalizeHeader() {
    lseek(this->fd, 4, SEEK_SET);
    writeInt(36 + this->total_samples*this->num_channels*4, 4);
    lseek(this->fd, 40, SEEK_SET);
    writeInt(this->total_samples*this->num_channels*4, 4);
}

void WAVFile::writeData(const float * data, unsigned int num_samples) {
    write(this->fd, data, 4*num_samples*this->num_channels);
    this->total_samples += num_samples;
}