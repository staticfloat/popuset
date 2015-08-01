#include <stdint.h>
#include <stdio.h>

/*
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/resource.h>
#include <getopt.h>
#include <math.h>
#include <ctype.h>
#include <unistd.h>
*/

#include "popuset.h"
#include "util.h"
#include "audio.h"

// Flag used to signify when we should bail out of main loop
bool shouldRun = true;

char * new_strdup(const char * input) {
    int len = strlen(input);
    char * data = new char[len];
    memcpy(data, input, len);
    return data;
}


audio_device * parseDevice(const char * optarg) {
    // Parse the device string
    const char *inout = NULL, *nameid = NULL, *channels = NULL;

    // Assume we've got at least one separator
    nameid = strstr(optarg, ":");
    if( nameid == NULL ) {
        // Nope, we don't assume they gave us only a name or id
        nameid = optarg;
    } else {
        // So we have at least an input/output and a channels!
        inout = optarg;
        nameid[0] = NULL;
        nameid++;

        // Now let's look for a channel specification
        channels = strstr(nameid, ":");
        if( channels != NULL ) {
            channels[0] = NULL;
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
        device->name = new_strdup(optarg);
        device->id = getDeviceId(device->name);
    }

    // Grab the channel numbers, we'll need them!
    int outchan = Pa_GetDeviceInfo(device->id)->maxOutputChannels;
    int inchan = Pa_GetDeviceInfo(device->id)->maxInputChannels;

    // Next, let's see if we've got an input or output specified:
    if( inout != NULL ) {
        if( matchBeginnings(inout, "input") )
            device.direction = INPUT;
        else if( matchBeginnings(inout, "output") )
            device.direction = OUTPUT;
        else {
            fprintf(stderr, "Invalid input/output specifier \"%s\"\n", inout);
            delete device->name;
            delete device;
            return NULL;
        } 
    } else {
        // If we don't let's see if we can guess which way to go with this device:
        if( outchan == 0 && inchan != 0 )
            device.direction = INPUT;
        else if( inchan == 0 & outchan != 0 )
            device.direction = OUTPUT;
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

        if( device.direction == INPUT && device->num_channels > inchan ) {
            fprintf(stderr, "Unable to request %d input channels for device \"%s\" (%d), maximum is %d\n", device->num_channels, device->name, device->id, inchan );
            delete device->name;
            delete device;
            return NULL;
        }
        if( device.direction == OUTPUT && device->num_channels > outchan ) {
            fprintf(stderr, "Unable to request %d output channels for device \"%s\" (%d), maximum is %d\n", device->num_channels, device->name, device->id, outchan );
            delete device->name;
            delete device;
            return NULL;
        }
    } else {
        // If it's not specified, do the min(2, maxchan) dance!
        if( device.direction == INPUT ) {
            device->num_channels = min(2, inchan);
        }
        if( device.direction == OUTPUT ) {
            device->num_channels = min(2, outchan);
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
        default_output->num_channels = min(2, Pa_GetDeviceInfo(default_output->id)->maxOutputChannels);
        
        // Default input
        audio_device * default_input = new audio_device();
        default_input->id = Pa_GetDefaultInputDevice();
        default_input->name = new_strdup(Pa_GetDeviceInfo(default_input->id)->name);
        default_input->num_channels = min(2, Pa_GetDeviceInfo(default_Input->id)->maxInputChannels);

        opts.devices->push_back(default_output);
        opts.devices->push_back(default_input);
    }    

    if( optind < argc ) {
        printf("Unrecognized extra arguments: ");
        while( optind < argc )
            printf("%s ", argv[optind++]);
        printf("\n");
    }
}



void sigint_handler(int dummy=0) {
    shouldRun = false;
    // Undo our signal handling here, so mashing CTRL-C will definitely kill us
    signal(SIGINT, SIG_DFL);
}


int main( int argc, char ** argv ) {
    // Parse command-line options
    parseOptions( argc, argv );

    // Setup CTRL-C signal handler and make ourselves feel important
    signal(SIGINT, sigint_handler);
    setpriority(PRIO_PROCESS, 0, -10);

    // Start the long haul loop
    printf("Use CTRL-C to gracefully shutdown...\n");

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

    printf("\nCleaned up gracefully!\n");
    return 0;
}