#include "net.h"
#include "util.h"
#include <zmq.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <string>

// Our zmq context object
void * zmq_ctx;

// Helper function to create sockets with default options
void * create_sock(int sock_type, int hwm) {
    void * sock = zmq_socket(zmq_ctx, sock_type);
    if( sock == NULL )
        return NULL;

    // Set common socket options
    int linger = 0;
    zmq_setsockopt(sock, ZMQ_RCVHWM, &hwm, sizeof(int));
    zmq_setsockopt(sock, ZMQ_SNDHWM, &hwm, sizeof(int));
    zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(int));
    return sock;
}




Broker::Broker() {
    zmq_ctx = zmq_ctx_new();

    // Initialize world ROUTER socket, and give it a (hopefully) unique identity
    this->world_sock = create_sock(ZMQ_ROUTER, 5);
    std::string ip6_addr = this->get_link_local_ip6();
    if( ip6_addr != "" )
        zmq_setsockopt(this->world_sock, ZMQ_IDENTITY, ip6_addr.c_str(), ip6_addr.size() + 1);

    // Bind to the desired port
    char bind_addr[14];
    snprintf(bind_addr, 14, "tcp://*:%d", opts.port);
    zmq_bind(this->world_sock, bind_addr);

    // Initialize audio output PUB socket
    this->output_sock = create_sock(ZMQ_PUB);
    zmq_bind(this->output_sock, "inproc://broker.output");

    // Initialize audio input ROUTER socket
    this->input_sock = create_sock(ZMQ_ROUTER);
    zmq_bind(this->input_sock, "inproc://broker.input");

    // Initialize command channel ROUTER socket
    this->cmd_sock = create_sock(ZMQ_ROUTER);
    zmq_bind(this->cmd_sock, "inproc://broker.cmd");

    // Allocate temp buff for receiving audio; we allocate enough space for all the channels at once:
    int max_channels = 0;
    for( auto device : opts.devices )
        max_channels = max(max_channels, device.num_channels);

    temp_buff = new float[max_channels*AUDIO_BUFF_LEN];
    temp_buff_len = max_channels*AUDIO_BUFF_LEN;

    this->client_list_dirty = false;
    this->last_clean = time_ms();
}

Broker::~Broker() {
    delete[] temp_buff;

    audio_device_command shutdown_cmd = {CMD_SHUTDOWN, 0, NULL};
    sendCommand(this->cmd_sock, shutdown_cmd);

    zmq_close(this->cmd_sock);
    zmq_close(this->input_sock);
    zmq_close(this->output_sock);
    zmq_close(this->world_sock);
    zmq_term(zmq_ctx);
}

void Broker::process() {
    // Build up a pollitem_t group from our sockets
    zmq_pollitem_t items[2];

    // First up, world_sock!
    items[0].socket = this->world_sock;
    items[0].events = ZMQ_POLLIN;

    // Next, audio input!
    items[1].socket = this->input_sock;
    items[1].events = ZMQ_POLLIN;

    // Wait for an event
    int rc = zmq_poll(items, 2, -1);
    if( rc > 0 ) {
        if( items[0].revents & ZMQ_POLLIN ) {
            // Handle world message
            handle_world();
        }
        if( items[1].revents & ZMQ_POLLIN ) {
            // Handle audio input message by just sending it out to our audio outputs
            handle_input();
        }
    }

    // Search for dead clients every 5 seconds
    double curr_time = time_ms();
    if( curr_time - this->last_clean > 5*1000.0f ) {
        for( auto itty : this->inbound ) {
            // If we haven't heard from somebody since the last clean, clean them!
            if( this->last_clean > itty.second )
                this->inbound.erase(itty.first);
        }
        this->last_clean = curr_time;
    }

    // If we need to update our poor device thread, do so!
    if( this->client_list_dirty ) {
        // Calculate total length
        int cl_len = 0;
        for( auto itty : this->inbound )
            cl_len += itty.first.size();

        // Allocate enough for our identities, NULLs separating each, and the extra NULL at the end
        char * client_list = new char[cl_len + this->inbound.size() + 1];

        // Copy each in, paying special attention to copying NULL characters as well
        int idx = 0;
        for( auto itty : this->inbound ) {
            memcpy(client_list + idx, itty.first.c_str(), itty.first.size()+1);
            idx += itty.first.size() + 1;
        }
        client_list[cl_len - 1] = NULL;

        // Finally, send it over, identifying it as a client list update!
        audio_device_command cl_cmd = {CMD_CLIENTLIST, cl_len, client_list};
        sendCommand(this->cmd_sock, cl_cmd);
    }
    this->client_list_dirty = false;
}

void Broker::connect(std::string addr) {
    this->outbound.insert(addr);
}

void Broker::disconnect(std::string addr) {
    this->outbound.erase(addr);
}

void Broker::handle_world() {
    // First, get the identity of the client talking to us:
    char client_tmp[INET6_ADDRSTRLEN];
    int client_len = zmq_recv(this->world_sock, &client_tmp[0], INET6_ADDRSTRLEN, 0);

    // Get the decoded audio length:
    int audio_len;
    zmq_recv(this->world_sock, &audio_len, sizeof(int), 0);
    audio_len = ntohl(audio_len);

    // If it's longer than our current scratch space, then increase the scratch space!
    if( audio_len > this->temp_buff_len ) {
        delete[] this->temp_buff;
        this->temp_buff_len = audio_len;
        this->temp_buff = new float[this->temp_buff_len];
    }

    // Next, get the data they're sending us:
    int datalen = zmq_recv(this->world_sock, this->encoded_buff, MAX_DATA_PACKET_LEN, 0);

    // Decode it into our (possibly newly-widened) temp_buff
    int dec_len = opus_decode_float(device.decoder, this->encoded_data, datalen, this->temp_buff, this->temp_buff_len, 0);
    if( dec_len == 0 || dec_len%AUDIO_BUFF_LEN != 0 ) {
        printf("ERROR: dec_len (%d) is not an integer multiple of AUDIO_BUFF_LEN (%d)!\n", dec_len, AUDIO_BUFF_LEN);
        return;
    }

    // send it to device threads, tagging it as originating from this client
    zmq_send(this->output_sock, &client_tmp[0], client_len, ZMQ_SENDMORE);
    zmq_send(this->output_sock, this->temp_buff, dec_len);

    // Add this to our inbound list, if it doesn't alread exist and timestamp it
    bool new_inbound = this->inbound.find(&client_tmp[0]) == this->inbound.end();
    this->inbound[&client_tmp[0]] = time_ms();

    // Set the client list as dirty if we just added something new into it
    this->client_list_dirty = new_inbound;
}

void Broker::handle_input() {
    // First, get the identity of the audio device sending to us:
    audio_device * device;
    zmq_recv(this->input_sock, &device, sizeof(audio_device *), 0);

    // Next, get the data!
    zmq_msg_t msg;
    zmq_msg_init(&msg);
    zmq_recvmsg(this->input_sock, &msg, 0);
    int datalen = zmq_msg_size(msg);

    // Ensure we have someone to send this data to:
    if( outbound.find(device) == outbound.end() )
        return;

    // Encode it:
    int enc_len = opus_encode_float(encoder, this->temp_buff, AUDIO_BUFF_LEN, this->encoded_data, MAX_DATA_PACKET_LEN );

    // Loop over all outbound clients
    for( auto client_addr : outbound[device] ) {
        // First, direct the message at this client
        zmq_send(this->world_sock, client_addr.c_str(), client_addr.size()+1, ZMQ_SENDMORE);

        // Send the decoded length
        zmq_send(this->world_sock, htonl(datalen), sizeof(int), ZMQ_SENDMORE);

        // Next, send the audio!
        zmq_send(this->world_sock, this->encoded_data, enc_len, 0);
    }
}

std::string Broker::get_link_local_ip6() {
    struct ifaddrs * ifAddrStruct = NULL, * ifa = NULL;

    getifaddrs(&ifAddrStruct);
    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        if( ifa->ifa_addr->sa_family == AF_INET6 ) {
            char addr[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &((struct sockaddr_in *)ifa->ifa_addr)->sin_addr, addr, INET6_ADDRSTRLEN);
            //printf("%s IP Address %s\n", ifa->ifa_name, addr);

            unsigned int addr_len = strlen(addr);
            char * ll_prefix = strstr(addr, "fe80::");
            if( ll_prefix != NULL && (ll_prefix - addr) < addr_len - 6 ) {
                freeifaddrs(ifAddrStruct);
                return std::string(addr);
            }
        }
    }
    if (ifAddrStruct != NULL)
        freeifaddrs(ifAddrStruct);
    return "";
}

void sendCommand( void * sock, audio_device_command cmd ) {
    cmd.datalen = htons(cmd.datalen);
    zmq_send(sock, cmd, sizeof(char) + sizeof(unsigned short), ZMQ_SENDMORE | ZMQ_DONTWAIT);
    if( cmd->datalen )
        zmq_send(sock, cmd->data, cmd->datalen, ZMQ_DONTWAIT);
}

int readCommand( void * sock, audio_device_command * cmd ) {
    // First, read in the type:
    if( zmq_recv(sock, cmd, sizeof(char) + sizeof(unsigned short), ZMQ_DONTWAIT) < 1 ) {
        cmd->type = CMD_INVALID;
        cmd->dataLen = 0;
        cmd->data = NULL;
        return errno;
    }
    cmd->datalen = ntohs(cmd->datalen);

    // If we successfully read in the type, let's construct a command object out of it
    cmd->data = new char[cmd->datalen];
    zmq_recv(sock, cmd->data, cmd->data, 0);
    return 0;
}