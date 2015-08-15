/*
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
*/

#include "popuset.h"
#include "util.h"
#include "audio.h"
#include <getopt.h>

// Our almighty options struct
opts_struct opts;

// Flag used to signify when we should bail out of main loop
bool shouldRun = true;

void printUsage(char * prog_name) {
    Pa_Initialize();
    int default_input = Pa_GetDefaultInputDevice();
    const char * input_name = Pa_GetDeviceInfo(default_input)->name;
    int input_channels = Pa_GetDeviceInfo(default_input)->maxInputChannels;

    int default_output = Pa_GetDefaultOutputDevice();
    const char * output_name = Pa_GetDeviceInfo(default_output)->name;
    int output_channels = Pa_GetDeviceInfo(default_output)->maxOutputChannels;
    
    printf("Usage: %s <options> where options is zero or more of:\n", prog_name);
    printf("\t--device/-d:   Device name/ID to open, with optional channel and direction.\n");
    printf("\t--target/-t:   Address of peer to send audio to.\n");
    printf("\t--meter/-m:    Display a wicked-sick live audio meter.\n");
    printf("\t--port/-p:     Port to listen on, only valid if input devices selected.\n");
    printf("\t--help/-h:     Print this help message, along with a device listing.\n\n");

    printf("Device strings conform to: <input/output>:<name/numeric id>:<channels>\n");
    printf("Defaults: listen on port 5040, open default input/output devices with up to two channels:\n");
    printf("  %s -p 5040 -d \"input:%s:%d\" -d \"output:%s:%d\"\n\n", prog_name, input_name, input_channels, output_name, output_channels );

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
    Pa_Terminate();
}


audio_device * parseDevice(char * optarg) {
    // Parse the device string
    char *inout = NULL, *nameid = NULL, *channels = NULL;

    // Assume we've got at least one separator
    nameid = strstr(optarg, ":");
    if( nameid == NULL ) {
        // Nope, we don't assume they gave us only a name or id
        nameid = optarg;
    } else {
        // So we have at least an input/output and a channels!
        inout = optarg;
        nameid[0] = 0;
        nameid++;

        // Now let's look for a channel specification
        channels = strstr(nameid, ":");
        if( channels != NULL ) {
            channels[0] = 0;
            channels++;
        }
    }

    // Let's start building this device!
    audio_device * device = new audio_device();

    // First, figure out if we've got a device name or id:
    if( is_number(nameid) ) {
        device->id = atoi(nameid);
        device->name = new_strdup(Pa_GetDeviceInfo(device->id)->name);
    } else {
        device->id = getDeviceId(nameid);
        device->name = new_strdup(Pa_GetDeviceInfo(device->id)->name);
    }

    // Grab the channel numbers, we'll need them!
    int outchan = Pa_GetDeviceInfo(device->id)->maxOutputChannels;
    int inchan = Pa_GetDeviceInfo(device->id)->maxInputChannels;

    // Next, let's see if we've got an input or output specified:
    if( inout != NULL ) {
        if( matchBeginnings(inout, "input") )
            device->direction = INPUT;
        else if( matchBeginnings(inout, "output") )
            device->direction = OUTPUT;
        else {
            fprintf(stderr, "Invalid input/output specifier \"%s\"\n", inout);
            delete device->name;
            delete device;
            return NULL;
        } 
    } else {
        // If we don't let's see if we can guess which way to go with this device:
        if( outchan == 0 && inchan != 0 )
            device->direction = INPUT;
        else if( inchan == 0 & outchan != 0 )
            device->direction = OUTPUT;
        else {
            fprintf(stderr, "Ambiguous direction for device \"%s\" (%d)\n", device->name, device->id );
            delete device->name;
            delete device;
            return NULL;
        }
    }

    // Finally, let's figure out how many channels we need;
    if( channels != NULL ) {
        if( !is_number(channels) ) {
            fprintf(stderr, "Invalid channel specifier \"%s\"\n", channels);
            delete device->name;
            delete device;
            return NULL;
        }
        device->num_channels = atoi(channels);

        if( device->direction == INPUT && device->num_channels > inchan ) {
            fprintf(stderr, "Unable to request %d input channels for device \"%s\" (%d), maximum is %d\n", device->num_channels, device->name, device->id, inchan );
            delete device->name;
            delete device;
            return NULL;
        }
        if( device->direction == OUTPUT && device->num_channels > outchan ) {
            fprintf(stderr, "Unable to request %d output channels for device \"%s\" (%d), maximum is %d\n", device->num_channels, device->name, device->id, outchan );
            delete device->name;
            delete device;
            return NULL;
        }
    } else {
        // If it's not specified, do the fmin(2, maxchan) dance!
        if( device->direction == INPUT ) {
            device->num_channels = fmin(2, inchan);
        }
        if( device->direction == OUTPUT ) {
            device->num_channels = fmin(2, outchan);
        }
        if( device->num_channels == 0 ) {
            fprintf(stderr, "Unable to auto-detect correct number of channels for device \"%s\" (%d)\n", device->name, device->id );
            delete device->name;
            delete device;
            return NULL;
        }
    }

    // Finally, return this device!
    return device;
}


void parseOptions( int argc, char ** argv ) {
    Pa_Initialize();
    static struct option long_options[] = {
        {"device", required_argument, 0, 'd'},
        {"target", required_argument, 0, 't'},
        {"meter", no_argument, 0, 'm'},
        {"port", required_argument, 0, 'p'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    // Set defaults, which is all channels on default output device, listening on 5040
    opts.port = 5040;
    opts.meter = false;

    int option_index = 0;
    int c;
    while( (c = getopt_long( argc, argv, "d:t:p:mh", long_options, &option_index)) != -1 ) {
        switch( c ) {
            case 'd': {
                audio_device * d = parseDevice(optarg);
                if( d != NULL ) {
                    opts.devices.push_back(d);
                }
            }   break;
            case 't' : {
                opts.targets.push_back(optarg);
            }   break;
            case 'p':
                opts.port = atoi(optarg);
                break;
            case 'm':
                opts.meter = true;
                break;
            case 'h':
                printUsage(argv[0]);
                exit(0);
                break;
        }
    }

    // If we haven't been given any devices, add the defaults:
    if( opts.devices.size() == 0 ) {
        // Default output
        audio_device * default_output = new audio_device();
        default_output->id = Pa_GetDefaultOutputDevice();
        default_output->name = new_strdup(Pa_GetDeviceInfo(default_output->id)->name);
        default_output->num_channels = fmin(2, Pa_GetDeviceInfo(default_output->id)->maxOutputChannels);
        default_output->direction = OUTPUT;
        
        // Default input
        audio_device * default_input = new audio_device();
        default_input->id = Pa_GetDefaultInputDevice();
        default_input->name = new_strdup(Pa_GetDeviceInfo(default_input->id)->name);
        default_input->num_channels = fmin(2, Pa_GetDeviceInfo(default_input->id)->maxInputChannels);
        default_input->direction = INPUT;

        // Add them to opts.devices so they get initialized by the AudioEngine
        opts.devices.push_back(default_output);
        opts.devices.push_back(default_input);
    }    

    if( optind < argc ) {
        printf("Unrecognized extra arguments: ");
        while( optind < argc )
            printf("%s ", argv[optind++]);
        printf("\n");
    }
    Pa_Terminate();
}



void sigint_handler(int dummy=0) {
    shouldRun = false;
    // Undo our signal handling here, so mashing CTRL-C will definitely kill us
    signal(SIGINT, SIG_DFL);
    printf("Signal received, shutting down...\n");
}


int main( int argc, char ** argv ) {
    // Parse command-line options
    parseOptions(argc, argv);

    // Setup CTRL-C signal handler and make ourselves feel important
    signal(SIGINT, sigint_handler);
    setpriority(PRIO_PROCESS, 0, -10);

    // Initialize AudioEngine and all its little thready things
    AudioEngine * ae = new AudioEngine(opts.devices);

    // Initiate connections to our targets
    for( auto target : opts.targets )
        ae->connect(target);

    // Start the long haul loop
    printf("Use CTRL-C to gracefully shutdown...\n");

    while( shouldRun ) {
        ae->processBroker();
    }

/*
    // If we haven't updated our bandwidth estimate for more than a second, do so!
    if( time_ms() - bandwidth_time > 250.0f ) {
        bandwidth_time = time_ms();
        bytes_last_second = bytes_this_second*4;
        bytes_this_second = 0;
    }

    if( opts.meter )
        print_level_meter(buffer);
*/

    delete ae;

    printf("\nCleaned up gracefully!\n");
    return 0;
}