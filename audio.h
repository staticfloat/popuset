#ifndef AUDIO_H
#define AUDIO_H

#include "popuset.h"



class AudioEngine {
/*****************
* INITIALIZATION *
*****************/
public:
	// Initialize audio streams using global options struct
	AudioEngine();
	// Cleanup
	~AudioEngine();

protected:
	// Initialize opus encoder/decoder and port input/output for each device configured
	bool initOpus(audio_device & device);
	bool initPort(audio_device & device);
	bool initSocks(audio_device & device);
};


// Return the device ID matching this name, or -1 if not found (case-insensitive)
int getDeviceId( const char * name );

// Only return true if all of buffer is 0.0f
bool is_silence( float * buffer, unsigned int len );

#endif //AUDIO_H