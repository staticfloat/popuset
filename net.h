#ifndef NET_H
#define NET_H

#include "popuset.h"
#include <unordered_set>
#include <map>

// Our zmq context object which is used by errybody
extern void * zmq_ctx;

// The broker is in charge of communicating with the outside world and routing to internal devices
class Broker {
	Broker();
	~Broker();

	// This blocks until a ZMQ event (internal or external) occurs
	void process();

	// Connect to a client, add them to outbound, and if they start sending to us, add them to inbound!
	void connect(std::string addr);
	void disconnect(std::string addr);
private:
	std::map<std::string, double> inbound;
	std::unordered_set<std::string> outbound;
	double last_clean;
	bool client_list_dirty;

	// Get link-local IPv6 address; used to set socket identities...
	std::string get_link_local_ip6();

	void handle_world();
	void handle_input();

	// How we communicate to the outside world.  It's a ROUTER socket, of course.
	void * world_sock;

	// The sockets for output and input of audio data, PUB and ROUTER, respectively.
	void * output_sock;
	void * input_sock;

	// The socket for telling audio threads what to do
	void * cmd_sock;

	// Temp buffers for dealing with audio
	float * temp_buff;
	int temp_buff_len;
	unsigned char * encoded_data;
};

// Helper function to create a socket, set high water marks, etc...
void * create_sock(int sock_type, int hwm = 2);

// Helper function to read a command, etc....
audio_device_command * readCommand( void * sock );
#endif //NET_H