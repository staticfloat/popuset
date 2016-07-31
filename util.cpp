#include "util.h"
#include <sys/time.h>
#include <math.h>
#include <zmq.h>
#include <unistd.h>
#include <fcntl.h>

// Format seconds into a string
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

// duplicate a string, but using new instead of malloc()
char * new_strdup(const char * input) {
    int len = strlen(input);
    char * data = new char[len+1];
    memcpy(data, input, len+1);
    return data;
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



// Print the level meter thingy that is so awesome and unnecessary
void print_level_meter( float * buffer, unsigned int num_frames, unsigned int num_channels, unsigned int bytes_per_second ) {
    // AWWWWWW YEEEAAAAAHHHHH
    static float * levels = NULL;
    static float * peak_levels = NULL;

    // Get storage area for our channels
    if( levels == NULL ) {
        levels = new float[num_channels];
        peak_levels = new float[num_channels];
        memset(peak_levels, 0, sizeof(float)*num_channels);
    }
    memset(levels, 0, sizeof(float)*num_channels);

    // First, calculate sum(x^2) for x = each channel
    for( int i=0; i<num_frames; ++i ) {
        for( int k=0; k<num_channels; ++k ) {
            levels[k] = fmin(1.0, fmax(levels[k], buffer[i*num_channels + k]));
        }
    }

    for( int k=0; k<num_channels; ++k ) {
        peak_levels[k] = .995*peak_levels[k];
        peak_levels[k] = fmax(peak_levels[k], levels[k]);
    }

    // Next, output the level of each channel:
    int max_space = 75;
    printf("                                                                                          \r");
    int level_divisions = max_space/num_channels;

    printf("[");
    for( int k=0; k<num_channels; ++k ) {
        if( k > 0 )
            printf("|");
        // Discretize levels[k] into level_divisions divisions
        int discrete_level = (int)fmin(levels[k]*level_divisions, level_divisions);
        int discrete_peak_level = (int)fmin(peak_levels[k]*level_divisions, level_divisions);

        // Next, output discrete_level "=" signs radiating outward from the "|"
        if( k < num_channels/2 ) {
            for( int i=discrete_peak_level; i<max_space/num_channels; ++i )
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
            for( int i=discrete_peak_level; i<max_space/num_channels; ++i )
                printf(" ");
        }

    }
    printf("] %.2f kb/s\r", bytes_per_second/1024.0f);
    fflush(stdout);
}

void gen_random_addr(char * addr, unsigned int max_len) {
    static const char alphanum[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

    sprintf(addr, "inproc://");
    for (int i = 9; i < max_len - 1; ++i)
        addr[i] = alphanum[rand() % (sizeof(alphanum) - 1)];

    addr[max_len - 1] = 0;
}

bool matchBeginnings(const char * x, const char * y) {
    for( int i=0; i<fmin(strlen(x), strlen(y)); ++i ) {
        if( x[i] != y[i] )
            return false;
    }
    return true;
}

int get_monitor_event (void *monitor, int *value, char **address) {
    // First frame in message contains event number and value
    zmq_msg_t msg;
    zmq_msg_init (&msg);
    if (zmq_msg_recv (&msg, monitor, 0) == -1)
       return -1; // Interruped, presumably

    uint8_t *data = (uint8_t *) zmq_msg_data (&msg);
    uint16_t event = *(uint16_t *) (data);
    if (value)
        *value = *(uint32_t *) (data + 2);

    // Second frame in message contains event address
    zmq_msg_init (&msg);
    if (zmq_msg_recv (&msg, monitor, 0) == -1)
        return -1; // Interruped, presumably

    if (address) {
        uint8_t *data = (uint8_t *) zmq_msg_data (&msg);
        size_t size = zmq_msg_size (&msg);
        *address = (char *) malloc (size + 1);
        memcpy (*address, data, size);
        (*address)[size] = 0;
    }
    return event;
}


// REP socket monitor thread
void * socket_monitor_thread(void *ctx) {
    void *s = zmq_socket(ctx, ZMQ_PAIR);
    int rc = zmq_connect(s, "inproc://monitor");
    while( true ) {
        int value = 0;
        char * address = NULL;
        int event = get_monitor_event(s, &value, &address);

        switch( event ) {
            case ZMQ_EVENT_CONNECTED:
                printf("EVENT: Connected! [%s]\n", address);
                break;
            case ZMQ_EVENT_CONNECT_DELAYED:
                printf("EVENT: Connect delayed! Waiting %dms... [%s]\n", value, address);
                break;
            case ZMQ_EVENT_CONNECT_RETRIED:
                printf("EVENT: Connect retried! [%s]\n", address);
                break;
            case ZMQ_EVENT_ACCEPTED:
                printf("EVENT: Accepted! [%s]\n", address);
                break;
            case ZMQ_EVENT_CLOSED:
                printf("EVENT: Closed! [%s]\n", address);
                break;
            case ZMQ_EVENT_DISCONNECTED:
                printf("EVENT: Disconnected! [%s]\n", address);
                break;
            default:
                printf("EVENT: [%d] [%d] [%s]\n", event, value, address);
                break;
        }
    }
    zmq_close(s);
    return NULL;
}


int old_stderr = -1;
int dev_null = -1;
void squelch_stderr()
{
    if( old_stderr == -1)
        old_stderr = dup(STDERR_FILENO);
    if( dev_null == -1 )
        dev_null = open("/dev/null", O_WRONLY);
    dup2(dev_null, STDERR_FILENO);
}

void restore_stderr()
{
    dup2(old_stderr, STDERR_FILENO);
}
