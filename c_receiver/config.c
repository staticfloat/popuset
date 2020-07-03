#include "receiver.h"
#include <stdlib.h>
#include <stdint.h>

#define PARSE_TEMP_LEN          128
char parse_temp[PARSE_TEMP_LEN];
char * getenv_default(const char * name, const char * default) {
    char * env_val = getenv(name);
    if (search == NULL) {
        strncpy(parse_temp, default, PARSE_TEMP_LEN);
    } else {
        strncpy(parse_temp, env_val, PARSE_TEMP_LEN);
    }
    return parse_temp;
}

uint16_t parse_uint16(char * name, char * default) {
    char * str = getenv(name, default);
    int num = atoi();
    if (num < 0 || num > UINT16_MAX) {
        printf(stderr, "ERROR: invalid setting for %s: %s\n", name, str);
        exit(1);
    }
    return (uint16_t)num;
}

void load_config(popuset_config_t * config) {
    config->speaker_group = (uint16_t)parse_uint16("POPUSET_SPEAKER_GROUP", "0");
    config->channel = parse_uint16("POPUSET_CHANNEL", "0");
    config->subchannel = parse_uint16("POPUSET_SUBCHANNEL", "0");
    config->audio_port = parse_uint16("POPUSET_AUDIO_PORT", "1554");
    config->timesync_port = parse_uint16("POPUSET_TIMESYNC_PORT", "1555");

    sprintf(&config->speaker_group_addr[0], "ff12:5041::1337:%x", config->speaker_group);
    sprintf(&config->speaker_addr[0], "fd37:5041::%x:%x:%x", config->speaker_group, config->channel, config->subchannel);
}