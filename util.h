#ifndef UI_H
#define UI_H

#include "popuset.h"

// Format a float as seconds or milliseconds in a string
const char * formatSeconds(float seconds);

// duplicate a string, but using new instead of malloc()
char * new_strdup(const char * input);

// Return time in miliseconds
const double time_ms();

// Return true if the given string is only whitespace and digits
bool is_number(const char * str);

// Print the level meter thingy that is so awesome and sooooooo unnecessary
void print_level_meter( float * buffer );

void gen_random_addr(char * addr, unsigned int max_len);
bool matchBeginnings(const char * x, const char * y);

void * socket_monitor_thread(void * ctx);

void squelch_stderr();
void restore_stderr();
#endif //UI_H
