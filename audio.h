#ifndef AUDIO_H
#define AUDIO_H

#include "popuset.h"
#include <unordered_set>

// Our zmq context object which is used by errybody
extern void * zmq_ctx;


class AudioEngine {
/*****************
* INITIALIZATION *
*****************/
public:
	// Initialize audio streams using global options struct
	AudioEngine(std::vector<audio_device *> & devices);
	// Cleanup
	~AudioEngine();

	void processBroker();

	// Connect to a client, add them to outbound
	void connect(std::string addr);
	void disconnect(std::string addr);
protected:
	// Initialize network broker thingy
	void initBroker();

	// Initialize opus encoder/decoder and port input/output for each device configured
	bool initOpus(audio_device * device);
	bool initPortAudio(audio_device * device);
	bool initSocks(audio_device * device);

	// How we communicate to the outside world.  It's a ROUTER socket, of course.
	void * world_sock;

	// The sockets for output and input of audio data to the audio threads, PUB and ROUTER, respectively.
	void * output_sock;
	void * input_sock;

	// The socket for telling audio threads what to do
	void * cmd_sock;

	// Temp buffer for dealing with audio
	unsigned char * encoded_data;

	// Keeping track of who's with us, and who's against us
	std::map<std::string, double> inbound;
	std::unordered_set<std::string> outbound;
	double last_clean;
	bool client_list_dirty;

	// Our devices
	std::vector<audio_device *> & devices;
};


// Return the device ID matching this name, or -1 if not found (case-insensitive)
int getDeviceId( const char * name );

// Only return true if all of buffer is 0.0f
bool is_silence( float * buffer, unsigned int len );

// Helper function to create a socket, set high water marks, etc...
void * create_sock(int sock_type, int hwm = 2);

// Get link-local IPv6 address; used to set socket identities...
std::string get_link_local_ip6();

// Helper function to read/write commands
void sendCommand( void * sock, audio_device_command cmd );
int readCommand( void * sock, audio_device_command * cmd );

#endif //AUDIO_H