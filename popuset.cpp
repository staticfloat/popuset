#include <stdint.h>
#include <stdio.h>
#include <portaudio.h>
#include <opus/opus.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <getopt.h>
#include <math.h>
#include <ctype.h>
#include <zmq.h>
#include <unistd.h>

// These are things that maybe should probably be changed to be configurable maybe probably
#define SAMPLE_RATE         48000
#define RING_BUFFER_LEN     (10*opts.bframes*opts.num_channels)
#define MAX_DATA_PACKET_LEN 1500

// Recording/playback ring buffer.  The ring buffer will always have readIdx <= writeIdx.
// If writeIdx does not move, readIdx will not exceed it. writeIdx similarly will not wrap
// all the way back around to readIdx; it will wait patiently one buffer behind so as not
// to create an ambiguity by lapping
float * ringBuffer;
int readIdx, writeIdx;

// I can't believe I'm actually dedicating global memory to niceties like this
float * levels;
float * peak_levels;
int packet_count = 0;

// Encoder/decoder objects
OpusEncoder * encoder;
OpusDecoder * decoder;
unsigned char * encoded_data;

// Flag used to signify when we should bail out of main loop
bool shouldRun = true;
bool lastBufferSilent = false;

// Our zmq context object and socket object
void * zmq_ctx;
void * sock;

const char * formatSeconds(float seconds) {
    if( seconds < 0.001f )
        return "  0s";

    static char buff[32];
    if( seconds < 1.0f )
        sprintf(buff, "%3dms", (int)(seconds*1000));
    else
        sprintf(buff, "%.1fs", seconds);
    return (const char *)buff;
}


void printUsage(char * prog_name) {
    int default_input = Pa_GetDefaultInputDevice();
    int default_output = Pa_GetDefaultOutputDevice();

    printf("Usage: %s <options> where options is zero or more of:\n", prog_name);
    printf("\t--device/-d:   Device name/ID you wish to capture from/output to.\n");
    printf("\t--channels/-c: Number of channels to limit capture/playback to.\n");
    printf("\t--remote/-r:   Address of the remote server to send captured audio to.\n");
    printf("\t--port/-p:     Port you wish to listen on/connect to.\n");
    printf("\t--bufflen/-b:  Length of transmission buffers in milliseconds.\n");
    printf("\t               Must be one of 2.5, 5, 10, 20, 40, 60 or 120.\n");
    printf("\t--help/-h:     Print this help message, along with a device listing.\n\n");

    printf("Default: listen, port 5040, 10ms buffers, default output, two channels:\n");
    printf("\t%s -p 5040 -d \"%s\" -b 10 -c %d\n\n", prog_name, Pa_GetDeviceInfo(default_output)->name, Pa_GetDeviceInfo(default_output)->maxOutputChannels );
    
    printf("Device listing:\n");
    int numDevices = Pa_GetDeviceCount();
    if( numDevices < 0 ) {
        fprintf( stderr, "ERROR: Pa_GetDeviceCount() returned 0x%x\n", numDevices );
        return;
    }

    const PaDeviceInfo * di;
    for( int i=0; i<numDevices; i++ ) {
        di = Pa_GetDeviceInfo(i);
        printf( "[%2d] %-33.33s -> [%3d in, %3d out] (latency: %s) ",
                i, di->name, di->maxInputChannels, di->maxOutputChannels,
                formatSeconds(fmax(di->defaultLowInputLatency, di->defaultLowOutputLatency)));

        if( i == default_input && i == default_output )
            printf("<>");
        else {
            if( i == default_input )
                printf("<");
            if( i == default_output )
                printf(">");
        }
        printf("\n");
    }
    exit(0);
}

// Return true if the given string is only whitespace and digits
bool is_number(const char * str) {
    for( int i=0; i<strlen(str); i++ ) {
        if( !isdigit(str[i]) && !isspace(str[i]) )
            return false;
    }
    return true;
}

// Return the device ID matching this name, or -1 if not found (case-insensitive)
int getDeviceId( const char * name ) {
    int numDevices = Pa_GetDeviceCount();
    if( numDevices < 0 ) {
        fprintf( stderr, "ERROR: Pa_GetDeviceCount() returned 0x%x\n", numDevices );
        return -1;
    }

    const PaDeviceInfo * di;
    for( int i=0; i<numDevices; i++ ) {
        di = Pa_GetDeviceInfo(i);
        if( strcasestr(di->name, name) != NULL )
            return i;
    }
    fprintf( stderr, "ERROR: Could not find device \"%s\"\n", name);
    return -1;
}

// This structure stores the user-configurable options
struct opts_struct {
    // Which device we're reading from/writing to (integer index)
    int device_id;
    // Which device we're reading from/writing to (string name)
    const char * device_name;
    // How many channels we read/write  (Note this is limited by opus)
    int num_channels;
    // If we are a client, what's the remote server we connect to?
    const char * remote_address;
    // What port do we do all our business on?
    int port;
    // How many miliseconds of audio do we send in a buffer?
    float bufflen;
    // How many frames of audio do we send in a bufffer?
    int bframes;
    // Should we show the meter thing?
    bool meter;
} opts;

void parseOptions( int argc, char ** argv ) {
    static struct option long_options[] = {
        {"device",  required_argument, 0, 'd'},
        {"channels", required_argument, 0, 'c'},
        {"remote", required_argument, 0, 'r'},
        {"port", required_argument, 0, 'p'},
        {"help", no_argument, 0, 'h'},
        {"meter", no_argument, 0, 'm'},
        {"bufflen", required_argument, 0, 'b'},
        {0, 0, 0, 0}
    };

    // Set defaults, which is all channels on default output device, listening on 5040
    opts.device_name = NULL;
    opts.device_id = -1;
    opts.num_channels = -1;
    opts.remote_address = NULL;
    opts.bufflen = 10.0f;
    opts.port = -1;
    opts.meter = false;

    int option_index = 0;
    int c;
    while( (c = getopt_long( argc, argv, "d:c:r:p:b:mh", long_options, &option_index)) != -1 ) {
        switch( c ) {
            case 'd':
                if( is_number(optarg) ) {
                    opts.device_id = atoi(optarg);
                    opts.device_name = Pa_GetDeviceInfo(opts.device_id)->name;
                } else {
                    opts.device_id = getDeviceId(optarg);
                    if( opts.device_id == -1 )
                        exit(1);
                    opts.device_name = Pa_GetDeviceInfo(opts.device_id)->name;
                }
                break;
            case 'c':
                opts.num_channels = atoi(optarg);
                if( opts.num_channels == 0 ) {
                    fprintf(stderr, "ERROR: Invalid number of channels (%d)!\n", opts.num_channels);
                    exit(1);
                }
                break;
            case 'r':
                opts.remote_address = strdup(optarg);
                break;
            case 'p':
                opts.port = atoi(optarg);
                break;
            case 'm':
                opts.meter = true;
                break;
            case 'b':
                opts.bufflen = atof(optarg);
                if( opts.bufflen != 2.5f && opts.bufflen != 5.0f && opts.bufflen != 10.0f &&
                    opts.bufflen != 20.0f && opts.bufflen != 40.0f && opts.bufflen != 60.0f) {
                    fprintf(stderr, "ERROR: Buffer length must be one of [2.5, 5, 10, 20, 40 60] milliseconds!\n");
                    exit(1);
                }
                break;
            case 'h':
                printUsage(argv[0]);
                break;
        }
    }


    // If remote_address is NULL, then we're dealing with output devices
    if( opts.remote_address == NULL ) {
        // If we haven't been given a device_id, use the default
        if( opts.device_name == NULL ) {
            opts.device_id = Pa_GetDefaultOutputDevice();
            opts.device_name = strdup(Pa_GetDeviceInfo(opts.device_id)->name);
        }

        // If we haven't been given a number of channels, use the default
        if( opts.num_channels == -1 )
            opts.num_channels = Pa_GetDeviceInfo(opts.device_id)->maxOutputChannels;
    } else {
        // If we haven't been given a device_id, use the default
        if( opts.device_name == NULL ) {
            opts.device_id = Pa_GetDefaultInputDevice();
            opts.device_name = strdup(Pa_GetDeviceInfo(opts.device_id)->name);
        }

        // If we haven't been given a number of channels, use the default
        if( opts.num_channels == -1 )
            opts.num_channels = Pa_GetDeviceInfo(opts.device_id)->maxInputChannels;
    }

    // If we haven't been given a port, use the default
    if( opts.port == -1 )
        opts.port = 5040;

    // Check to make sure we have enough channels
    if( opts.num_channels == 0 ) {
        fprintf(stderr, "ERROR: Device \"%s\" (%d) has no suitable channels!\n", opts.device_name, opts.device_id );
        exit(1);
    }
    
    // Calculate how many frames per buffer we're gonna be sending
    opts.bframes = (opts.bufflen * SAMPLE_RATE)/1000;

    if( optind < argc ) {
        printf("Unrecognized extra arguments: ");
        while( optind < argc )
            printf("%s ", argv[optind++]);
        printf("\n");
    }
}




// Initialize our data stuffage
void initData( void ) {
    // Initialize 3 buffers worth of audio space for our ring buffer
    ringBuffer = (float *) malloc(RING_BUFFER_LEN*sizeof(float));
    memset(ringBuffer, 0, RING_BUFFER_LEN*sizeof(float));

    // Start off our ring buffer indices
    readIdx = 0;
    writeIdx = 0;

    // Initialize encoded data buffer
    encoded_data = (unsigned char *) malloc(MAX_DATA_PACKET_LEN);
    memset(encoded_data, 0, MAX_DATA_PACKET_LEN);

    // Initialize peak levels holders
    peak_levels = (float *)malloc(sizeof(float)*opts.num_channels);
    memset( peak_levels, 0, sizeof(float)*opts.num_channels);
    levels = (float *)malloc(sizeof(float)*opts.num_channels);
    memset( levels, 0, sizeof(float)*opts.num_channels);
}

void initZMQ( void ) {
    zmq_ctx = zmq_ctx_new();
    int sock_type;
    if( opts.remote_address == NULL )
        sock_type = ZMQ_PULL;
    else
        sock_type = ZMQ_PUSH;

    // Create socket and set common options
    sock = zmq_socket(zmq_ctx, sock_type);
    int hwm = 5;
    zmq_setsockopt(sock, ZMQ_RCVHWM, &hwm, sizeof(int));
    zmq_setsockopt(sock, ZMQ_SNDHWM, &hwm, sizeof(int));
    int linger = 0;
    zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(int));

    if( opts.remote_address == NULL ) {
        // We're a listening socket
        char bind_addr[14];
        snprintf(bind_addr, 14, "tcp://*:%d", opts.port);
        zmq_bind( sock, bind_addr);
    } else {
        // We're a connecting socket
        char connect_addr[128];
        snprintf(connect_addr, 128, "tcp://%s:%d", opts.remote_address, opts.port);
        zmq_connect( sock, connect_addr );
    }
}

bool initOpus( void ) {
    int err;
    if( opts.remote_address == NULL ) {
        decoder = opus_decoder_create(SAMPLE_RATE, opts.num_channels, &err);
        if (err != OPUS_OK) {
            fprintf(stderr, "Could not create Opus decoder.\n");
            return false;
        }
    } else {
        encoder = opus_encoder_create(SAMPLE_RATE, opts.num_channels, OPUS_APPLICATION_AUDIO, &err);
        if (err != OPUS_OK) {
            fprintf(stderr, "Could not create Opus encoder.\n");
            return false;
        }
    }
    return true;
}


// Only return true if readIndex is at least num_samples ahead of writeIndex
bool ringBufferWritable(int num_samples ) {
    int readIdx_adjusted = readIdx;
    // Note that this is <= because if the two indexes are equal, we can write!
    if( readIdx_adjusted <= writeIdx )
        readIdx_adjusted += RING_BUFFER_LEN;

    return readIdx_adjusted - writeIdx > num_samples;
}

// Only return true if writeIndex is at least num_samples ahead of readIndex
bool ringBufferReadable( int num_samples ) {
    int writeIdx_adjusted = writeIdx;
    if( writeIdx_adjusted < readIdx )
        writeIdx_adjusted += RING_BUFFER_LEN;

    return writeIdx_adjusted - readIdx > num_samples;
}

bool ringBufferRead(int num_samples, float * outputBuff) {
    if( !ringBufferReadable(num_samples) ) {
        //printf("[%d] Won't read %d from ringbuffer, (W: %d, R: %d)\n", packet_count, num_samples, writeIdx, readIdx);
        return false;
    }

    // If we have to wraparound to service this request, then do so by invoking ourselves twice
    if( readIdx + num_samples > RING_BUFFER_LEN ) {
        int first_batch = RING_BUFFER_LEN - readIdx;
        ringBufferRead(first_batch, outputBuff);
        ringBufferRead(num_samples - first_batch, outputBuff + first_batch);
    } else {
        memcpy(outputBuff, ringBuffer + readIdx, num_samples*sizeof(float));
        memset(ringBuffer + readIdx, 0, num_samples*sizeof(float));
        readIdx = (readIdx + num_samples)%RING_BUFFER_LEN;
    }
    return true;
}

bool ringBufferWrite(int num_samples, float * inputBuff) {
    if( !ringBufferWritable(num_samples) ) {
        //printf("[%d] Won't write %d to ringbuffer, (W: %d, R: %d)\n", packet_count, num_samples, writeIdx, readIdx);
        return false;
    }
    
    if( writeIdx + num_samples > RING_BUFFER_LEN ) {
        int first_batch = RING_BUFFER_LEN - writeIdx;
        ringBufferWrite(first_batch, inputBuff);
        ringBufferWrite(num_samples - first_batch, inputBuff + first_batch);
    } else {
        memcpy(ringBuffer + writeIdx, inputBuff, num_samples*sizeof(float));
        writeIdx = (writeIdx + num_samples)%RING_BUFFER_LEN;
    }
    return true;
}

static int output_callback( const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData ) {
    // First, disable unused variable warnings 
    (void) userData;
    (void) statusFlags;
    (void) timeInfo;
    (void) inputBuffer;

    // Do that ring buffer magic!
    if( outputBuffer != NULL ) {
        if( !ringBufferRead(framesPerBuffer*opts.num_channels, (float *)outputBuffer) )
            memset(outputBuffer, 0, sizeof(float)*framesPerBuffer*opts.num_channels);
    }
    return paContinue;
}

static int input_callback(  const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData ) {
    // First, disable unused variable warnings 
    (void) userData;
    (void) statusFlags;
    (void) timeInfo;
    (void) outputBuffer;

    // Quit out if we don't have an inputBuffer to read from, or if we don't have an entire buffer's worth of space to write to
    if( inputBuffer != NULL )
        ringBufferWrite(framesPerBuffer*opts.num_channels, (float *)inputBuffer);
    return paContinue;
}

PaStream * openAudio( void ) {
    PaStreamParameters parameters;
    PaStream *stream;
    PaError err;

    parameters.device = opts.device_id;
    parameters.channelCount = opts.num_channels;
    parameters.sampleFormat = paFloat32;
    parameters.suggestedLatency = Pa_GetDeviceInfo( parameters.device )->defaultLowInputLatency;
    parameters.hostApiSpecificStreamInfo = NULL;

    // Are we doing input or output?
    if( opts.remote_address == NULL )
        err = Pa_OpenStream( &stream, NULL, &parameters, SAMPLE_RATE, opts.bframes, 0, &output_callback, NULL );
    else
        err = Pa_OpenStream( &stream, &parameters, NULL, SAMPLE_RATE, opts.bframes, 0, &input_callback, NULL );
    if( err != paNoError ) {
        fprintf(stderr, "Could not open stream.\n");
        return NULL;
    }

    err = Pa_StartStream( stream );
    if( err != paNoError ) {
        fprintf(stderr, "Could not start stream.\n");
        return NULL;
    }

    return stream;
}

void sigint_handler(int dummy=0) {
    shouldRun = false;
    zmq_close(sock);
    // Undo our signal handling here, so mashing CTRL-C will definitely kill us
    signal(SIGINT, SIG_DFL);
}

bool is_silence( float * buffer ) {
    for( int i=0; i<opts.bframes*opts.num_channels; ++i ) {
        if( buffer[i] != 0.0f )
            return false;
    }
    return true;
}

void print_level_meter( float * buffer ) {
    float curr_levels[opts.num_channels];
    memset(curr_levels, 0, sizeof(float)*opts.num_channels);

    // First, calculate sum(x^2) for x = each channel
    for( int i=0; i<opts.bframes; ++i ) {
        for( int k=0; k<opts.num_channels; ++k ) {
            levels[k] = fmin(1.0, fmax(levels[k], fabs(buffer[i*opts.num_channels + k])));
        }
    }
    for( int k=0; k<opts.num_channels; ++k ) {
        levels[k] = 0.9*levels[k] + 0.1*curr_levels[k];

        peak_levels[k] = .995*peak_levels[k];
        peak_levels[k] = fmax(peak_levels[k], levels[k]);
    }

    // Next, output the level of each channel:
    int max_space = 60;
    printf("                                                                                \r");
    int level_divisions = max_space/opts.num_channels;

    printf("[");
    for( int k=0; k<opts.num_channels; ++k ) {
        if( k > 0 )
            printf("|");
        // Discretize levels[k] into level_divisions divisions
        int discrete_level = (int)fmin(levels[k]*level_divisions, level_divisions);
        int discrete_peak_level = (int)fmin(peak_levels[k]*level_divisions, level_divisions);

        // Next, output discrete_level "=" signs radiating outward from the "|"
        if( k < opts.num_channels/2 ) {
            for( int i=discrete_peak_level; i<max_space/opts.num_channels; ++i )
                printf(" ");
            printf("{");
            for( int i=discrete_level; i<discrete_peak_level - 1; ++i )
                printf(" ");
            for( int i=0; i<discrete_level - (int)(discrete_peak_level == discrete_level); i++ )
                printf("=");
        } else {
            for( int i=0; i<discrete_level - (int)(discrete_peak_level == discrete_level); i++ )
                printf("=");
            for( int i=discrete_level; i<discrete_peak_level - 1; ++i )
                printf(" ");
            printf("}");
            for( int i=discrete_peak_level; i<max_space/opts.num_channels; ++i )
                printf(" ");
        }
        
    }
    printf("] ");

    for( int k=0; k<opts.num_channels-1; ++k ) {
        printf("%.3f, ", levels[k]);
    }
    printf("%.3f\r", levels[opts.num_channels-1]);
    fflush(stdout);
}


int main( int argc, char ** argv ) {
    if (Pa_Initialize() != paNoError) {
        fprintf(stderr, "Error: Could not initialize PortAudio.\n");
        return 1;
    }

    // Parse command-line options
    parseOptions( argc, argv );

    // Print out option choices
    if( opts.remote_address == NULL ) {
        printf("Opening device \"%s\" (%d) for %d-channel output\n", opts.device_name, opts.device_id, opts.num_channels);
        printf("Listening on port %d for connections...\n", opts.port );
    } else {
        printf("Opening device \"%s\" (%d) for %d-channel input\n", opts.device_name, opts.device_id, opts.num_channels);
        printf("Connecting to %s:%d...\n", opts.remote_address, opts.port);
    }

    initZMQ();
    initData();
    if( !initOpus() )
        return 1;
    PaStream * stream = openAudio();
    if (stream == NULL)
        return 1;

    // Setup CTRL-C signal handler and make ourselves feel important
    signal(SIGINT, sigint_handler);
    setpriority(PRIO_PROCESS, 0, -10);

    // Start the long haul loop
    printf("Use CTRL-C to gracefully shutdown...\n");
    float * buffer = (float *)malloc(opts.bframes*opts.num_channels*sizeof(float));
    
    if( opts.remote_address == NULL ) {
        while( shouldRun ) {
            // If we're a listening kind of guy, listen for audio!
            int data_len = zmq_recv(sock, (char *)encoded_data, MAX_DATA_PACKET_LEN, 0 );
            packet_count++;
            if( data_len > 0 ) {
                int dec_len = opus_decode_float( decoder, encoded_data, data_len, buffer, opts.bframes, 0 );
                //while( !ringBufferWritable(dec_len*opts.num_channels) )
                //    usleep(1000);
                ringBufferWrite(dec_len*opts.num_channels, buffer);

                if( opts.meter )
                    print_level_meter(buffer);
            }
        }
    } else {
        while( shouldRun ) {
            while( !ringBufferReadable(opts.bframes*opts.num_channels) )
                usleep(1000);
            ringBufferRead(opts.bframes*opts.num_channels, buffer);

            // Check to make sure we've got something to say
            bool thisBufferSilent = is_silence(buffer);

            if( opts.meter )
                print_level_meter(buffer);

            // Only send something if we've got something to say.  We need to process at least one
            // buffer of complete silence before stopping so that our encoder doesn't see discontinuities
            if( !(thisBufferSilent && lastBufferSilent) ) {
                int data_len = opus_encode_float( encoder, buffer, opts.bframes, encoded_data, MAX_DATA_PACKET_LEN );
                zmq_send(sock, encoded_data, data_len, ZMQ_DONTWAIT);
                packet_count++;
            }

            lastBufferSilent = thisBufferSilent;
        }
    }
    free(buffer);
    zmq_close(sock);
    zmq_ctx_destroy(zmq_ctx);
    Pa_CloseStream(stream);
    Pa_Terminate();
    printf("\nCleaned up gracefully!\n");
    return 0;
}