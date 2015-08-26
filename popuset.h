#ifndef POPUSET_H
#define POPUSET_H

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <list>
#include <string>
#include <map>
#include <vector>
#include <opus/opus.h>
#include <portaudio.h>
#include <pthread.h>
#include <semaphore.h>

#include "ringbuffer.h"
#include "qarb.h"
#include "wavfile.h"

enum device_direction {
    INPUT,
    OUTPUT
};


// A device we read from/write to.  Note that things like id, name, etc...
// are filled out during parameter parsing, but things like encoders and decoders
// are initialized much later, by the AudioEngine after initializing opus/pulse
struct audio_device {
    // Which device we're reading from/writing to (integer index and string name)
    // Note that we provide one or the other on the command line, and AudioEngine
    // fills in the other, if possible.
    int id;
    const char * name;

    // How many channels we read/write  (Note this is limited by opus)
    unsigned short num_channels;
    
    // which direction we're using this device in; reading, writing, or both?
    device_direction direction;

    // Broker [PUB] -> Audio thread [SUB], commands (client list, etc...)
    void * cmd_sock;

    // Audio thread [DEALER] -> Broker [ROUTER], data
    void * input_sock;

    // Audio thread [PAIR] -> Audio device [PAIR]
    void * mixed_audio_out; // PAIR (thread side)
    void * mixed_audio_in;  // PAIR (device side)


    // Device [PUSH] -> Audio thread [PULL]
    void * raw_audio_in;    // PUSH
    void * raw_audio_out;   // PULL

    // Audio heading into the device, and the decoder that supplies it
    OpusDecoder * decoder;

    // Audio coming out of the device, and the encoder that will consume it
    OpusEncoder * encoder;

    // The pulse stream object, used mostly for cleaning up audio devices
    PaStream * stream;

    // The thread object
    pthread_t thread;

    // If we're logging input/output, these are the files we write to
    WAVFile *output_log, *input_log;
};


// This structure stores the user-configurable options
struct opts_struct {
    // The port we communicate on
    unsigned short port;

    // The device we're reading from/writing to.  Note that things like id, name, etc...
    // are filled out during parameter parsing, but things like encoders and decoders are initialized
    // much later, by the AudioEngine.
    std::vector<audio_device *> devices;

    // The targets we should connect to
    std::vector<std::string> targets;

    // The prefix of logging files (e.g. .wav files) to write to
    std::string logprefix;

    // Should we show the meter thing?
    bool meter;
};

extern opts_struct opts;


enum {
    CMD_INVALID = 0,
    CMD_SHUTDOWN,
    CMD_CLIENTLIST,
};

struct audio_device_command {
    // The type of command we're sending
    char type;

    // The data that has been sent alongside the command (if any)
    unsigned short datalen;
    char * data;
};



// I am pretty much locked in to 48 KHz sample rate, so let's just define that here.
#define SAMPLE_RATE             48000

// I am also locked in to 10ms buffers, for better or worse.
#define SAMPLES_IN_BUFFER       ((10*SAMPLE_RATE)/1000)

// We'll just keep this for funsies; this is the maximum packet size for opus
// We've set it to an optimistic estimate of the MTU, we'll see if that's actually reasonable?
#define MAX_DATA_PACKET_LEN     (1500 - 10 - 4)



#endif //POPUSET_H