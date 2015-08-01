#include "util.h"

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

// Return time in miliseconds
const double time_ms() {
    timeval t;
    gettimeofday(&t, NULL);
    return t.tv_sec*1000.0 + t.tv_usec/1000.0f;
}

// Return true if the given string is only whitespace and digits
bool is_number(const char * str) {
    for( int i=0; i<strlen(str); i++ ) {
        if( !isdigit(str[i]) && !isspace(str[i]) )
            return false;
    }
    return true;
}

void printUsage(char * prog_name) {
    Pa_Initialize();
    int default_input = Pa_GetDefaultInputDevice();
    const char * input_name = Pa_GetDeviceInfo(default_input)->name;
    int input_channels = Pa_GetDeviceInfo(default_input)->maxInputChannels;

    int default_output = Pa_GetDefaultOutputDevice();
    const char * output_name = Pa_GetDeviceInfo(default_output)->name;
    int output_channels = Pa_GetDeviceInfo(default_output)->maxOutputChannels;
    

    printf("Usage: %s <options> where options is zero or more of:\n", prog_name);
    printf("\t--device/-d:   Device name/ID to open, with optional channel and direction specifiers.\n");
    printf("\t--target/-t:   Address of peer to send audio to.\n");
    printf("\t--meter/-m:    Display a wicked-sick live audio meter, right here in your terminal.\n");
    printf("\t--port/-p:     Port to listen on, only valid if input devices selected.\n");
    printf("\t--help/-h:     Print this help message, along with a device listing.\n\n");

    printf("Device strings conform to: <input/output>:<name/numeric id>:<channels>\n");
    printf("Defaults: listen on port 5040, open default input/output devices with up to two channels:\n");
    printf("\t%s -p 5040 -d \"input:%s:%d\" -d \"output:%s:%d\n\n\"", prog_name, input_name, input_channels, output_name, output_channels );

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

// Print the level meter thingy that is so awesome and unnecessary
void print_level_meter( float * buffer ) {
    // First, calculate sum(x^2) for x = each channel
    memset(levels, 0, sizeof(float)*opts.num_channels);
    for( int i=0; i<opts.bframes; ++i ) {
        for( int k=0; k<opts.num_channels; ++k ) {
            levels[k] = fmin(1.0, fmax(levels[k], buffer[i*opts.num_channels + k]));
        }
    }

    for( int k=0; k<opts.num_channels; ++k ) {
        peak_levels[k] = .995*peak_levels[k];
        peak_levels[k] = fmax(peak_levels[k], levels[k]);
    }

    // Next, output the level of each channel:
    int max_space = 75;
    printf("                                                                                          \r");
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
    printf("] %.2f kb/s\r", bytes_last_second/1024.0f);
    fflush(stdout);
}

void gen_random_addr(char * addr, unsigned int max_len) {
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    
    sprintf(addr, "inproc://");
    for (int i = 9; i < max_len - 1; ++i)
        addr[i] = alphanum[rand() % (sizeof(alphanum) - 1)];

    this->addr[max_len - 1] = 0;
}

bool matchBeginnings(const char * x, const char * y) {
    for( int i=0; i<min(strlen(x), strlen(y)); ++i ) {
        if( x[i] != y[i] )
            return false;
    }
    return true;
}
