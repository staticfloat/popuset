#include <stdint.h>
#include <stdio.h>
#include <portaudio.h>
#include <opus/opus.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <math.h>
#include <ctype.h>
#include <zmq.h>

#define SAMPLE_RATE         48000
#define FRAMES_PER_BUFFER   (int)(10*(SAMPLE_RATE/1000))
#define MAX_DATA_PACKET_LEN 1500

// Recording/playback buffer
float * buffer;

// Encoder/decoder objects
OpusEncoder * encoder;
OpusDecoder * decoder;
unsigned char * encoded_data;

// Flag used to signify when we should bail out of main loop
bool shouldRun = true;

// Our zmq context object and socket object
void * zmq_ctx;
void * sock;


void printUsage(char * prog_name) {
    int default_input = Pa_GetDefaultInputDevice();
    int default_output = Pa_GetDefaultOutputDevice();

    printf("Usage: %s <options> where options is zero or more of:\n", prog_name);
    printf("\t--device/-d:   The device name/ID you wish to capture from/output to.\n");
    printf("\t--channels/-c: The number of channels to limit capture/playback to.\n");
    printf("\t--remote/-r:   The address of the remote server to send captured audio to.\n");
    printf("\t--port/-p:     The port you wish to listen on/connect to.\n");
    printf("\t--help/-h:     Print this help message, along with a device listing.\n\n");

    printf("Default behavior is to listen on port 5040, sending to default output with max channels:\n");
    printf("\t%s -p 5040 -d \"%s\" -c %d\n\n", prog_name, Pa_GetDeviceInfo(default_output)->name, Pa_GetDeviceInfo(default_output)->maxOutputChannels );
    
    printf("Device listing:\n");
    int numDevices = Pa_GetDeviceCount();
    if( numDevices < 0 ) {
        fprintf( stderr, "ERROR: Pa_GetDeviceCount() returned 0x%x\n", numDevices );
        return;
    }

    const PaDeviceInfo * di;
    for( int i=0; i<numDevices; i++ ) {
        di = Pa_GetDeviceInfo(i);
        printf( "[%2d] %-27s -> [%2d in, %2d out]  (latency: %4.0fms)  ",
                i, di->name, di->maxInputChannels, di->maxOutputChannels,
                fmax(di->defaultLowInputLatency, di->defaultLowOutputLatency)*1000);

        if( i == default_input && i == default_output )
            printf("[default in/out]");
        else {
            if( i == default_input )
                printf("[default in]");
            if( i == default_output )
                printf("[default out]");
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

struct opts_struct {
    int device_id;
    const char * device_name;
    int num_channels;
    const char * remote_address;
    int port;
} opts;

void parseOptions( int argc, char ** argv ) {
    static struct option long_options[] = {
        {"device",  required_argument, 0, 'd'},
        {"channels", required_argument, 0, 'c'},
        {"remote", required_argument, 0, 'r'},
        {"port", required_argument, 0, 'p'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    // Set defaults, which is all channels on default output device, listening on 5040
    opts.device_name = NULL;
    opts.device_id = -1;
    opts.num_channels = -1;
    opts.remote_address = NULL;
    opts.port = -1;

    int option_index = 0;
    int c;
    while( (c = getopt_long( argc, argv, "d:c:r:p:lh", long_options, &option_index)) != -1 ) {
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
                    fprintf(stderr, "ERROR: Invalid number of channels (%d)!", opts.num_channels);
                }
                break;
            case 'r':
                opts.remote_address = strdup(optarg);
                break;
            case 'p':
                opts.port = atoi(optarg);
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

    if( optind < argc ) {
        printf("Unrecognized extra arguments: ");
        while( optind < argc )
            printf("%s ", argv[optind++]);
        printf("\n");
    }
}




// Initialize our threading semaphores
void initData( void ) {
    //sema_init(&sema, 0);

    buffer = (float *) malloc(FRAMES_PER_BUFFER*opts.num_channels*sizeof(float));
    memset(buffer, 0, FRAMES_PER_BUFFER*opts.num_channels*sizeof(float));
    encoded_data = (unsigned char *) malloc(MAX_DATA_PACKET_LEN);
    memset(encoded_data, 0, MAX_DATA_PACKET_LEN);
}

void initZMQ( void ) {
    zmq_ctx = zmq_ctx_new();
    sock = zmq_socket(zmq_ctx, ZMQ_PAIR);
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

    int conflate = 1;
    zmq_setsockopt(sock, ZMQ_CONFLATE, &conflate, sizeof(int));
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
        err = Pa_OpenStream( &stream, NULL, &parameters, SAMPLE_RATE, FRAMES_PER_BUFFER, 0, NULL, NULL );
    else
        err = Pa_OpenStream( &stream, &parameters, NULL, SAMPLE_RATE, FRAMES_PER_BUFFER, 0, NULL, NULL );
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
    // Undo our signal handling here
    signal(SIGINT, SIG_DFL);
}

bool is_silence( void ) {
    for( int i=0; i<FRAMES_PER_BUFFER*opts.num_channels; ++i ) {
        if( buffer[i] != 0.0f )
            return false;
    }
    return true;
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
    initOpus();
    PaStream * stream = openAudio();
    if (stream == NULL)
        return 1;

    // Setup CTRL-C signal handler
    signal(SIGINT, sigint_handler);

    printf("Use CTRL-C to gracefully shutdown...\n");
    while( shouldRun ) {
        if( opts.remote_address == NULL ) {
            // If we're a listening kind of guy, listen for audio!
            int data_len = zmq_recv(sock, (char *)encoded_data, MAX_DATA_PACKET_LEN, 0 );
            if( data_len > 0 ) {
                int dec_len = opus_decode_float( decoder, encoded_data, data_len, buffer, FRAMES_PER_BUFFER, 0 );
                if( dec_len != FRAMES_PER_BUFFER ) {
                    fprintf(stderr, "WARNING: dec_len (%d) != %d\n", dec_len, FRAMES_PER_BUFFER);
                }
                Pa_WriteStream( stream, buffer, FRAMES_PER_BUFFER );
            }
        } else {
            // If we're a talking kind of guy, then TALK FOR HEAVEN'S SAKE!
            Pa_ReadStream( stream, buffer, FRAMES_PER_BUFFER );

            // We need to keep on encoding so that encoder state doesn't get confused.  :/
            int data_len = opus_encode_float( encoder, buffer, FRAMES_PER_BUFFER, encoded_data, MAX_DATA_PACKET_LEN );

            // But only actually send this stuff if we've got something to say!
            if( !is_silence() ) {
                zmq_send(sock, encoded_data, data_len, ZMQ_DONTWAIT);
            }
        }
    }
    zmq_close(sock);
    zmq_ctx_destroy(zmq_ctx);
    Pa_CloseStream(stream);
    Pa_Terminate();
    printf("\nCleaned up gracefully!\n");
    return 0;
}