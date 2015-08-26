#include "audio.h"
#include "util.h"
#include <string.h>
#include <fcntl.h>
#include <sys/types.h>
#include <ifaddrs.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <zmq.h>

#define IDENT_LEN   INET6_ADDRSTRLEN + 8

void * zmq_ctx;

void print_peak_level(const float * data, int num_samples, int num_channels) {
    float peak_level[16];
    for( int i=0; i<16; ++i )
        peak_level[i] = 0.0f;

    for( int i=0; i<num_samples; ++i ) {
        for( int k=0; k<num_channels; ++k ) {
            peak_level[k] = fmax(fabs(data[i*num_channels + k]), peak_level[k]);
        }
    }
    for( int k=0; k<num_channels; ++k )
        printf("%2.2f ", peak_level[k]);
    printf("                 \r");
    fflush(stdout);
}

static int pa_callback( const void *inputBuffer, void *outputBuffer, unsigned long framesPerBuffer, const PaStreamCallbackTimeInfo* timeInfo, PaStreamCallbackFlags statusFlags, void *userData ) {
    // First, disable unused variable warnings 
    (void) statusFlags;
    (void) timeInfo;

    // Grab our audio_device, which has important things in it
    audio_device * device = (audio_device *)userData;

    // If we've got input data, send it out!
    if( inputBuffer != NULL ) {
        printf("INPUT: ");
        print_peak_level((const float *)inputBuffer, framesPerBuffer, device->num_channels);

        //if( device->input_log != NULL )
        //    device->input_log->writeData((const float *)inputBuffer, framesPerBuffer);
        zmq_send(device->raw_audio_in, inputBuffer, framesPerBuffer*device->num_channels*sizeof(float), 0);
    }

    // If we're expecting output data, let's not disappoint!
    if( outputBuffer != NULL ) {
        int samples_readable = device->out_buff->getMaxReadable();
        //printf("Have %d readable\n", device->out_buff->getMaxReadable());
        if( samples_readable < framesPerBuffer*device->num_channels ) {
            printf("UNDERRUN! (%d < %d)\n", samples_readable, framesPerBuffer*device->num_channels);
            memset(outputBuffer, 0, sizeof(float)*framesPerBuffer*device->num_channels);
        }
        else {
            device->out_buff->read(framesPerBuffer*device->num_channels, (float *)outputBuffer);
            /**/
            float power = 0.0f;
            for( int i=0; i<framesPerBuffer*device->num_channels; ++i ) {
                power += fabs(((const float *)outputBuffer)[i]);
            }
            if( power == 0.0f ) {
                printf("DETECTED UNDERRUN when we thought we had %d samples to read!\n", samples_readable);
            }
            /**/
            if( device->output_log != NULL )
                device->output_log->writeData((const float *)outputBuffer, framesPerBuffer);
        }
        
        //printf("OUTPUT: ");
        //print_peak_level((const float *)outputBuffer, framesPerBuffer, device->num_channels);
        //printf("\n");
    }

    // The show must go on
    return paContinue;
}



// Right now, only covers mono -> multichannel and multichannel -> mono
void mixdown_channels( float * in_data, float * out_data, unsigned int num_samples, unsigned int in_channels, unsigned int out_channels ) {
    if( in_channels == out_channels ) {
        memcpy(out_data, in_data, num_samples*in_channels*sizeof(float));
        return;
    }
    switch( in_channels ) {
        case 1:
            // Just copy input channel to all output channels
            for( int i=0; i<num_samples; ++i ) {
                for( int k=0; k<out_channels; ++k )
                    out_data[i*out_channels + k] = in_data[i];
            }
            break;
        default:
            // If output is mono, just mix all input channels together
            if( out_channels == 1 ) {
                for( int i=0; i<num_samples; ++i ) {
                    out_data[i] = 0.0f;
                    for( int k=0; k<in_channels; ++k )
                        out_data[i] += in_data[i*in_channels + k];
                    out_data[i] /= in_channels;
                }
            }

            // If output is not mono, I don't know what to do!
            printf("ERROR: Cannot mixdown %d -> %d!\n", in_channels, out_channels);
            break;
    }
}


bool bind_darnit(void * sock, const char * addr) {
    int err = zmq_bind(sock, addr);
    if( err != 0 ) {
        fprintf(stderr, "Could not bind to %s; %s\n", addr, strerror(errno));
        return false;
    }
    return true;
}

// Initialize Opus {de,en}coders for the given device
bool initOpus( audio_device * device ) {
    int err;
    if( device->direction != INPUT || true ) {
        device->decoder = opus_decoder_create(SAMPLE_RATE, device->num_channels, &err);
        if (err != OPUS_OK) {
            fprintf(stderr, "Could not create Opus decoder with %d channels for %s.\n", device->num_channels, device->name);
            return false;
        }
        printf("Created a decoder for %d channels!\n", device->num_channels);
    }
    if( device->direction != OUTPUT ) {
        device->encoder = opus_encoder_create(SAMPLE_RATE, device->num_channels, OPUS_APPLICATION_AUDIO, &err);
        if (err != OPUS_OK) {
            fprintf(stderr, "Could not create Opus encoder with %d channels for %s.\n", device->num_channels, device->name);
            return false;
        }
        printf("Created an encoder for %d channels!\n", device->num_channels);
    }
    return true;
}

bool initSocks( audio_device * device ) {
    // Create command channel listener
    device->cmd_sock = create_sock(ZMQ_DEALER, 10);
    if( device->cmd_sock == NULL ) {
        fprintf(stderr, "Could not create command socket for device %s", device->name);
        return false;
    }
    zmq_setsockopt(device->cmd_sock, ZMQ_IDENTITY, &device, sizeof(audio_device *));
    // Set subscription preferences and connect command channel
    if( zmq_connect(device->cmd_sock, "inproc://broker_cmd") != 0 )
        fprintf(stderr, "zmq_connect() failed: %s\n", strerror(errno));
    
    // Create channel to send recorded audio data out to broker on
    device->input_sock = create_sock(ZMQ_DEALER);
    if( device->input_sock == NULL ) {
        fprintf(stderr, "Could not create input socket for device %s", device->name);
        return false;
    }
    // Do the same for the input channel!
    zmq_setsockopt(device->input_sock, ZMQ_IDENTITY, &device, sizeof(audio_device *));
    zmq_connect(device->input_sock, "inproc://broker_input");

    // Create channel to receive raw audio from audio device.
    device->raw_audio_out = create_sock(ZMQ_PULL);
    if( device->raw_audio_out == NULL ) {
        fprintf(stderr, "Could not create raw_audio_out socket for device %s", device->name);
        return false;
    }
    char addr[20];
    sprintf(&addr[0], "inproc://dev%2d_raw", device->id);
    zmq_connect(device->raw_audio_out, addr);
    return true;
}

// Initialize Port streams for the given device
bool initPortAudio( audio_device * device ) {
    PaStreamParameters parameters;
    parameters.device = device->id;
    parameters.channelCount = device->num_channels;
    parameters.sampleFormat = paFloat32;
    parameters.suggestedLatency = Pa_GetDeviceInfo( parameters.device )->defaultLowInputLatency;
    parameters.hostApiSpecificStreamInfo = NULL;

    // Are we doing input, output, or both?
    PaStreamParameters *inparams = NULL, *outparams = NULL;
    if( device->direction != OUTPUT )
        inparams = &parameters;
    if( device->direction != INPUT )
        outparams = &parameters;

    // Actually try to open the stream
    printf("Opening \"%s\" (%d) with %d channels...\n", device->name, device->id, device->num_channels);
    PaError err;
    err = Pa_OpenStream( &device->stream, inparams, outparams, SAMPLE_RATE, (10*SAMPLE_RATE)/1000, 0, &pa_callback, (void *)device );

    if( err != paNoError ) {
        fprintf(stderr, "Could not open stream %d - %s\n", device->id, device->name);
        return false;
    }

    // Start the stream, spawning off a thread to run the callbacks from.
    err = Pa_StartStream( device->stream );
    if( err != paNoError ) {
        fprintf(stderr, "Could not start stream %d - %s\n", device->id, device->name);
        return false;
    }

    return true;
}


void * audio_thread(void * device_ptr) {
    // Grab our device from the device_ptr passed in to this thread
    audio_device * device = (audio_device *)device_ptr;

    // Initialize sockets
    initSocks(device);

    // Initialize Opus
    initOpus(device);

    // Initialize Port
    initPortAudio(device);

    WAVFile * input_log = NULL;
    WAVFile * output_log = NULL;
    device->out_buff = NULL;
    if( device->direction != INPUT ) {
        // Give ourselves a maximum of 40ms buffer time on the QARB
        device->out_buff = new QueueingAdditiveRingBuffer(40*device->num_channels*SAMPLE_RATE/1000);

        // Create output log
        std::string filename = opts.logprefix + "." + std::to_string(device->id) + "-out.wav";
        output_log = new WAVFile(filename.c_str(), device->num_channels, SAMPLE_RATE);
    }
    if( device->direction != OUTPUT ) {
        // Create input log
        std::string filename = opts.logprefix + "." + std::to_string(device->id) + "-in.wav";
        input_log = new WAVFile(filename.c_str(), device->num_channels, SAMPLE_RATE);
    }
    device->input_log = input_log;
    device->output_log = output_log;

    // I think it's pretty probable that we'll need at least 10ms for scratch space; let's see if I'm right!
    float * temp_buff = new float[2*10*SAMPLE_RATE/1000];
    unsigned int temp_buff_len = 2*10*SAMPLE_RATE/1000;
    float * mix_buff = new float[device->num_channels*10*SAMPLE_RATE/1000];
    unsigned int mix_buff_len = device->num_channels*10*SAMPLE_RATE/1000;

    // Scratch space for encoded data
    unsigned char * encoded_data = new unsigned char[MAX_DATA_PACKET_LEN];

    // Our client identity list, mapping to output sockets.  These sockets go:
    // Broker [PUB] -> Audio thread [SUB]
    std::map<std::string, void *> clientSocks;

    // Build up a pollitem_t group from our sockets
    zmq_pollitem_t items[66];
    items[0].socket = device->cmd_sock;
    items[1].socket = device->raw_audio_out;
    
    // We only deal in ZMQ_POLLIN events, so set those up first
    for( int i=0; i<66; ++i ) {
        items[i].fd = 0;
        items[i].revents = 0;
        items[i].events = ZMQ_POLLIN;
    }

    // Let's listen for ZMQ events, and mix some wicked sick beats
    bool keepRunning = true;
    while( keepRunning ) {
        // Wait for an event
        //printf("[0x%x] Waiting for events from %d sockets...\n", device, 2 + clientSocks.size() );
        int rc = zmq_poll(&items[0], 2 + clientSocks.size(), -1);
        
        if( rc <= 0 ) {
            fprintf(stderr, "zmq_poll() == %d: %s\n", rc, strerror(errno) );
            keepRunning = false;
            break;
        }

        // Are we receiving a command?
        if( items[0].revents & ZMQ_POLLIN ) {
            audio_device_command cmd;
            readCommand(device->cmd_sock, &cmd);
            printf("[0x%llx] Got a command (%d)\n", (unsigned long long)device, cmd.type);

            switch( cmd.type ) {
                case CMD_CLIENTLIST: {
                    std::unordered_set<std::string> clients;
                    printf("Rebuilding clientlist\n...");

                    // The data field of cmd now holds a NULL-separated list of client identities,
                    // ending with an entry of zero length (an extra NULL at the end of the last id)
                    unsigned int idx = 0;
                    while( cmd.data[idx] != 0 ) {
                        unsigned int identity_len = strlen(cmd.data + idx);

                        const char * identity = cmd.data + idx;
                        // Add this string to our client set:
                        clients.insert(identity);

                        // If such a client does not already exist in clientSocks, create one!
                        if( clientSocks.find(identity) == clientSocks.end() ) {
                            // Create a socket to listen for data coming from this client:
                            void * sock = zmq_socket(zmq_ctx, ZMQ_SUB);
                            zmq_connect(sock, "inproc://broker_output");
                            zmq_setsockopt(sock, ZMQ_SUBSCRIBE, identity, identity_len);
                            clientSocks[identity] = sock;
                            printf("We are ready to receive from %s on socket 0x%llx\n", identity, (unsigned long long) sock);
                        }
                        idx += identity_len + 1;
                    }

                    // Now go through all the clients we already have and ensure they're still on the list
                    for( auto& kv : clientSocks ) {
                        if( clients.count(kv.first) == 0 ) {
                            // Let's cleanup this client!
                            device->out_buff->clearClient(kv.first);
                            zmq_close(kv.second);
                            clientSocks.erase(kv.first);
                        }
                    }

                    // Finally, rebuild items:
                    int i = 2;
                    for( auto& kv : clientSocks ) {
                        items[i].socket = kv.second;
                        i++;
                    }

                    // We can't really continue on in this loop I don't think, so let's continue from here;
                    continue;
                }   break;
                case CMD_SHUTDOWN:
                    // The ultimate surrender
                    keepRunning = false;
                    break;
                case CMD_INVALID:
                default:
                    break;
            }
        }

        // Did we just get audio from the device?
        if( items[1].revents & ZMQ_POLLIN ) {
            // Read it in
            zmq_msg_t msg;
            zmq_msg_init(&msg);
            zmq_recvmsg(device->raw_audio_out, &msg, 0);
            int dec_len = zmq_msg_size(&msg);
            int num_samples = dec_len/(sizeof(float)*device->num_channels);

            //printf("[0x%x] Got %d samples of %d channels from device!\n", device, num_samples, device->num_channels);
            //print_peak_level((const float *)zmq_msg_data(&msg), num_samples, device->num_channels);

            //if( input_log != NULL )
            //    input_log->writeData((const float *)zmq_msg_data(&msg), num_samples);

            // Encode it:
            //printf("msg size: %d, num_channels: %d\n", zmq_msg_size(&msg), device->num_channels);
            int enc_len = opus_encode_float(device->encoder, (const float *)zmq_msg_data(&msg), num_samples, encoded_data, MAX_DATA_PACKET_LEN );
            if( enc_len < 0 ) {
                fprintf(stderr, "opus_encode_float() error: %d\n", enc_len);
            } else {
                //printf("Encoded %d samples down to %d/%d bytes\n", num_samples, enc_len, MAX_DATA_PACKET_LEN);
                /**/
                // Send the decoded length
                dec_len = htonl(dec_len);
                zmq_send(device->input_sock, &dec_len, sizeof(int), ZMQ_SNDMORE);

                // Send number of channels
                int num_channels = htonl(device->num_channels);
                zmq_send(device->input_sock, &num_channels, sizeof(int), ZMQ_SNDMORE);

                // Next, send the encoded audio!
                zmq_send(device->input_sock, encoded_data, enc_len, 0);

                // Small amount of cleanup
                zmq_msg_close(&msg);
                /**/

                // NABIL: DELETE ME?
                /**/
                if( input_log != NULL ) {
                    memset(temp_buff, 0, temp_buff_len*sizeof(float));
                    // Decode the data, expanding temp_buff if we need to:
                    if( temp_buff_len < num_samples*device->num_channels ) {
                        delete[] temp_buff;
                        temp_buff_len = num_samples*device->num_channels;
                        temp_buff = new float[temp_buff_len];
                    }
                    int dl = opus_decode_float(device->decoder, encoded_data, enc_len, temp_buff, num_samples, 0);
                    if( dl != num_samples )
                        printf("AWWOOOOGAH! (%d != %d)\n", dl, num_samples);
                    if( dl > 0 )
                        input_log->writeData((const float *)temp_buff, num_samples);
                }
                /**/
            }
        }

        // Did we just get audio from a client?
        for( int i=0; i<clientSocks.size(); ++i ) {
            //printf("Checking user socket %d (0x%llx)...\n", i, items[i+2].socket);
            if( items[i + 2].revents & ZMQ_POLLIN ) {
                // Get the identity first
                char client_ident[IDENT_LEN];
                if( zmq_recv(items[i+2].socket, &client_ident[0], IDENT_LEN, 0) == -1 )
                    printf("[0x%llx] zmq_recv failed; %s\n", items[i+2].socket, strerror(errno));
    
                int dec_len, num_channels;
                // Read in the audio from this client; first decoded length
                zmq_recv(items[i+2].socket, &dec_len, sizeof(int), 0);
                dec_len = ntohl(dec_len);

                // Then number of channels
                zmq_recv(items[i+2].socket, &num_channels, sizeof(int), 0);
                num_channels = ntohl(num_channels);

                // Finally, the audio itself
                int enc_len = zmq_recv(items[i+2].socket, encoded_data, MAX_DATA_PACKET_LEN, 0);

                //printf("Got a %d dec_len, %d num_channels, and %d enc_len from %s\n", dec_len, num_channels, enc_len, &client_ident[0]);

                // Decode the data, expanding temp_buff if we need to:
                if( temp_buff_len < dec_len/sizeof(float) ) {
                    delete[] temp_buff;
                    temp_buff_len = dec_len/sizeof(float);
                    temp_buff = new float[temp_buff_len];
                }

                // Decode it into our (possibly newly-widened) temp_buff
                int actually_dec_len = opus_decode_float(device->decoder, encoded_data, enc_len, temp_buff, temp_buff_len, 0);

                // Calculate the expected number of samples
                int num_samples = dec_len/(sizeof(float)*num_channels);
                if( actually_dec_len != num_samples ) {
                    fprintf(stderr, "ERROR: actually_dec_len (%d) != num_samples (%d)\n", actually_dec_len, num_samples);
                    break;
                }

                //printf("Just decoded %d samples from client %s!\n", actually_dec_len, &client_ident[0]);
                
                // Mix the audio down, expanding mix_buff if we need to:
                if( mix_buff_len < num_samples*device->num_channels ) {
                    delete[] mix_buff;
                    mix_buff_len = num_samples*device->num_channels;
                    mix_buff = new float[mix_buff_len];
                }

                // Mix the audio down for output onto our device
                mixdown_channels(temp_buff, mix_buff, num_samples, num_channels, device->num_channels);

                //printf("DECODED: ");
                //print_peak_level((const float *)temp_buff, num_samples, num_channels);
                //printf("\nMIXED DOWN: ");
                //print_peak_level((const float *)mix_buff, num_samples, device->num_channels);

                //if( output_log != NULL ) {
                    //output_log->writeData(mix_buff, num_samples);
                //}

                // Write it into our circular buffer! First, lookup the offset this particular client's got:
                device->out_buff->write(num_samples*device->num_channels, &client_ident[0], mix_buff);
            }
        }
    }

    // CLEANUP TIME! Let's blow this popsicle stand!
    printf("[%d] Cleaning up thread\n", device->id);

    // Stop the stream
    Pa_CloseStream(device->stream);

    // Cleanup encoder/decoder
    if( device->decoder != NULL )
        opus_decoder_destroy(device->decoder);
    if( device->encoder != NULL )
        opus_encoder_destroy(device->encoder);

    // Cleanup logfiles, if they exist
    if( output_log != NULL )
        delete output_log;
    if( input_log != NULL )
        delete input_log;

    // Close client socks
    while( !clientSocks.empty() ) {
        auto kv = clientSocks.begin();
        zmq_close(kv->second);
        clientSocks.erase(kv->first);
    }

    // Close sockets we 
    zmq_close(device->cmd_sock);
    zmq_close(device->input_sock);
    zmq_close(device->raw_audio_out);

    // Cleanup top-tier stuff!
    delete[] device->name;
    delete[] temp_buff;
    delete[] mix_buff;
    delete[] encoded_data;

    return NULL;
}



AudioEngine::AudioEngine(std::vector<audio_device *> & devices) : devices(devices) {
    // Attempt to initialize PortAudip
    if (Pa_Initialize() != paNoError) {
        fprintf(stderr, "Error: Could not initialize PortAudio.\n");
        throw "Error: Could not initialize PortAudio";
    }

    // Initialize broker...
    this->initBroker();

    // Start audio device threads
    for( auto device : this->devices ) {
        // Create channel to send raw audio out from audio device
        device->raw_audio_in = create_sock(ZMQ_PUSH);
        if( device->raw_audio_in == NULL ) {
            fprintf(stderr, "Could not create raw_audio_in socket for device %s", device->name);
        }
        char addr[20];
        sprintf(&addr[0], "inproc://dev%2d_raw", device->id);
        zmq_bind(device->raw_audio_in, addr);

        if( pthread_create(&device->thread, NULL, audio_thread, (void *)device) != 0 ) {
            fprintf(stderr, "pthread_create() failed!\n");
            throw "Error: Could not create thread!";
        }
    }

    // Initialize encoded_data as well
    this->encoded_data = new unsigned char[MAX_DATA_PACKET_LEN];
}

AudioEngine::~AudioEngine() {
    // Send CMD_SHUTDOWN to everybody
    for( auto device : this->devices ) {
        zmq_send(this->cmd_sock, &device, sizeof(audio_device *), ZMQ_SNDMORE | ZMQ_DONTWAIT);
        audio_device_command cmd = {CMD_SHUTDOWN, 0};
        sendCommand(this->cmd_sock, cmd);
    }
    
    // Join all threads
    for( auto device : this->devices ) {
        zmq_close(device->raw_audio_in);
        pthread_join(device->thread, NULL);
    }

    // No more Port Audio for us.  :(
    Pa_Terminate();

    // Close all broker sockets
    zmq_close(this->cmd_sock);
    zmq_close(this->input_sock);
    zmq_close(this->output_sock);
    zmq_close(this->world_sock);

    // Finally, terminate zmq!
    zmq_term(zmq_ctx);
}


void AudioEngine::initBroker() {
    zmq_ctx = zmq_ctx_new();

    // Initialize world ROUTER socket, and give it a (hopefully) unique identity
    this->world_sock = create_sock(ZMQ_ROUTER, 5);
    this->identity = get_link_local_ip6();
    if( this->identity != "" ) {
        this->identity = "[" + this->identity + "]:" + std::to_string(opts.port);
        printf("Setting world_sock identity to %s\n", this->identity.c_str());
        zmq_setsockopt(this->world_sock, ZMQ_IDENTITY, this->identity.c_str(), this->identity.size() + 1);
    }

    // Bind to the desired port
    char bind_addr[14];
    snprintf(bind_addr, 14, "tcp://*:%d", opts.port);
    bind_darnit(this->world_sock, bind_addr);

    // Initialize audio output PUB socket
    this->output_sock = create_sock(ZMQ_PUB);
    bind_darnit(this->output_sock, "inproc://broker_output");
    
    // Initialize audio input ROUTER socket
    this->input_sock = create_sock(ZMQ_ROUTER);
    bind_darnit(this->input_sock, "inproc://broker_input");

    // Initialize command channel PUB socket
    this->cmd_sock = create_sock(ZMQ_ROUTER);
    bind_darnit(this->cmd_sock, "inproc://broker_cmd");
    
    // Client list accounting
    this->client_list_dirty = false;
    this->last_clean = time_ms();
}

void AudioEngine::connect(std::string addr) {
    // Let's find out the identity of this peer:
    std::string tcp_addr = "tcp://" + addr;
    void * ident_sock = zmq_socket(zmq_ctx, ZMQ_REQ);
    zmq_setsockopt(ident_sock, ZMQ_IDENTITY, this->identity.c_str(), this->identity.size() + 1);
    zmq_connect(ident_sock, tcp_addr.c_str() );

    // This is an identity request
    zmq_send(ident_sock, 0, 0, 0);
    
    // Now, we wait for a response, max wait time; 1 second
    zmq_pollitem_t item = {ident_sock, 0, ZMQ_POLLIN, 0};
    int rc = zmq_poll(&item, 1, 2000);
    if( rc <= 0 || !(item.revents & ZMQ_POLLIN) ) {
        printf("ERROR: Could not connect and learn identity of %s [%d]\n", tcp_addr.c_str(), rc);
        zmq_close(ident_sock);
        return;
    }

    // Otherwise, let's get the identity:
    //printf("Waiting for response...\n");
    char client_ident[IDENT_LEN];
    int ident_len = zmq_recv(ident_sock, &client_ident[0], IDENT_LEN, 0);
    zmq_close(ident_sock);

    //printf("Got a response! %s...\n", client_ident);
    
    // Insert the identity into outbound, and connect our world_sock!
    this->outbound.insert(client_ident);
    printf("Connected to %s (%s)\n", addr.c_str(), client_ident);
    zmq_connect( this->world_sock, tcp_addr.c_str() );
}

void AudioEngine::disconnect(std::string addr) {
    this->outbound.erase(addr);
}


void AudioEngine::processBroker() {
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
        // Did we get a message from the world?
        if( items[0].revents & ZMQ_POLLIN ) {
            // First, get the identity of the client talking to us:
            char client_tmp[IDENT_LEN];
            int client_len = zmq_recv(this->world_sock, &client_tmp[0], IDENT_LEN, 0);

            //printf("Received a world message from %s!\n", &client_tmp[0]);

            // Get the decoded audio length, number of channels, and encoded audio:
            int audio_len, num_channels;
            // If this was an empty message, not an audio length at all, that's an ident request
            if( zmq_recv(this->world_sock, &audio_len, sizeof(int), 0) == 0 ) {
                // Clear empty message as well
                zmq_recv(this->world_sock, &audio_len, sizeof(int), 0);

                //printf("Returning identity %s to %s\n", this->identity.c_str(), &client_tmp[0]);
                zmq_send(this->world_sock, &client_tmp[0], client_len, ZMQ_SNDMORE);
                zmq_send(this->world_sock, 0, 0, ZMQ_SNDMORE);
                zmq_send(this->world_sock, this->identity.c_str(), this->identity.size()+1, 0);
            } else {
                // Now, receive the num_channels, if we can.
                zmq_recv(this->world_sock, &num_channels, sizeof(int), 0);

                int datalen = zmq_recv(this->world_sock, this->encoded_data, MAX_DATA_PACKET_LEN, 0);

                // send all pieces on to device threads, tagging it as originating from this client
                zmq_send(this->output_sock, &client_tmp[0], client_len, ZMQ_SNDMORE);
                zmq_send(this->output_sock, &audio_len, sizeof(int), ZMQ_SNDMORE);
                zmq_send(this->output_sock, &num_channels, sizeof(int), ZMQ_SNDMORE);
                zmq_send(this->output_sock, this->encoded_data, datalen, 0);

                // Add this client to our inbound list, if it doesn't alread exist and timestamp it
                bool new_inbound = this->inbound.find(&client_tmp[0]) == this->inbound.end();
                this->inbound[&client_tmp[0]] = time_ms();

                // Set the client list as dirty if we just added something new into it
                if( new_inbound ) {
                    printf("Let's take a minute to welcome %s to the party\n", &client_tmp[0]);
                    this->client_list_dirty = true;
                }
            }
        }

        // Did we get input from our device threads?
        if( items[1].revents & ZMQ_POLLIN ) {
            //printf("Sending a message out to the world!\n");
            // First, get the identity of the audio device sending to us:
            audio_device * device;
            zmq_recv(this->input_sock, &device, sizeof(audio_device *), 0);

            // Next, get decoded audio len, number of channels, and actual encoded data:
            int audio_len, num_channels;
            zmq_recv(this->input_sock, &audio_len, sizeof(int), 0);
            zmq_recv(this->input_sock, &num_channels, sizeof(int), 0);
            int enc_len = zmq_recv(this->input_sock, this->encoded_data, MAX_DATA_PACKET_LEN, 0);

            // Loop over all outbound clients
            for( auto client_addr : outbound ) {
                // First, direct the message at this client
                zmq_send(this->world_sock, client_addr.c_str(), client_addr.size()+1, ZMQ_SNDMORE);

                // Send the decoded length, number of channels, and actual encoded data
                zmq_send(this->world_sock, &audio_len, sizeof(int), ZMQ_SNDMORE);
                zmq_send(this->world_sock, &num_channels, sizeof(int), ZMQ_SNDMORE);
                zmq_send(this->world_sock, this->encoded_data, enc_len, 0);
            }
        }
    }

    // Search for dead clients every 5 seconds
    double curr_time = time_ms();
    if( curr_time - this->last_clean > 5*1000.0f ) {
        for( auto itty : this->inbound ) {
            // If we haven't heard from somebody since the last clean, clean them!
            if( this->last_clean > itty.second ) {
                printf("Culling %s since they've been silent for a while...\n", itty.first.c_str());
                this->inbound.erase(itty.first);
            }
        }
        this->last_clean = curr_time;
    }

    // If we need to update our poor device threads, do so!
    if( this->client_list_dirty ) {
        // Calculate total length
        unsigned short cl_len = 0;
        for( auto itty : this->inbound )
            cl_len += itty.first.size();

        // Allocate enough for our identities, NULLs separating each, and the extra NULL at the end
        char * client_list = new char[cl_len + this->inbound.size() + 1];

        // Copy each in, paying special attention to copying NULL characters as well
        int idx = 0;
        for( auto itty : this->inbound ) {
            memcpy(client_list + idx, itty.first.c_str(), itty.first.size()+1);
            idx += itty.first.size()+1;
        }
        // The final NULL in the coffin
        client_list[cl_len + this->inbound.size()] = 0;

        // Send it over to audio threads, identifying it as a client list update!
        for( auto device : this->devices ) {
            // First, direct this message to the appropriate device:
            zmq_send(this->cmd_sock, &device, sizeof(audio_device *), ZMQ_SNDMORE);

            // Next, create the necessary command and blast it out onto the socket!
            audio_device_command cl_cmd = {CMD_CLIENTLIST, (unsigned short )(cl_len + this->inbound.size() + 1), client_list};
            sendCommand(this->cmd_sock, cl_cmd);
        }

        // We are no longer dirty!
        this->client_list_dirty = false;
    }
}



// Return the device ID matching this name, or -1 if not found (case-insensitive)
int getDeviceId( const char * name ) {
    int numDevices = Pa_GetDeviceCount();
    if( numDevices < 0 ) {
        fprintf( stderr, "ERROR: Pa_GetDeviceCount() returned 0x%x\n", numDevices );
        return -1;
    }

    const PaDeviceInfo * di;
    const PaDeviceInfo * di_choice;
    int choice = -1;
    for( int i=0; i<numDevices; i++ ) {
        di = Pa_GetDeviceInfo(i);
        if( strcasestr(di->name, name) != NULL ) {
            if( choice == -1 ) {
                choice = i;
                di_choice = di;
            } else {
                fprintf( stderr, "Ambiguous device name \"%s\"; choosing deivce \"%s\" (%d)\n", name, di_choice->name, choice);
                break;
            }
        }
    }

    if( choice == -1 )
        fprintf( stderr, "ERROR: Could not find device \"%s\"\n", name);
    return choice;
}


// Only return true if all of buffer is 0.0f
bool is_silence( float * buffer, unsigned int len ) {
    for( int i=0; i<len; ++i ) {
        if( buffer[i] != 0.0f )
            return false;
    }
    return true;
}

// Helper function to create sockets with default options
void * create_sock(int sock_type, int hwm) {
    void * sock = zmq_socket(zmq_ctx, sock_type);
    if( sock == NULL ) {
        fprintf(stderr, "zmq_socket() failed: %s\n", strerror(errno) );
        return NULL;
    }

    // Set common socket options
    int linger = 0;
    zmq_setsockopt(sock, ZMQ_RCVHWM, &hwm, sizeof(int));
    zmq_setsockopt(sock, ZMQ_SNDHWM, &hwm, sizeof(int));
    zmq_setsockopt(sock, ZMQ_LINGER, &linger, sizeof(int));
    return sock;
}

std::string get_link_local_ip6() {
    struct ifaddrs * ifAddrStruct = NULL, * ifa = NULL;

    getifaddrs(&ifAddrStruct);
    for (ifa = ifAddrStruct; ifa != NULL; ifa = ifa->ifa_next) {
        // Skip loopback devices
        if( ifa->ifa_name[0] == 'l' && ifa->ifa_name[1] == 'o' )
            continue;

        if( ifa->ifa_addr->sa_family == AF_INET6 ) {
            char addr[IDENT_LEN];
            inet_ntop(AF_INET6, &((struct sockaddr_in6 *)ifa->ifa_addr)->sin6_addr, addr, IDENT_LEN);

            // If this is a link-local address, let's goooooo
            char * ll_prefix = strstr(addr, "fe80::");
            if( ll_prefix != NULL ) {
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
    //printf("Sending command of size %d...\n", cmd.datalen);
    zmq_send(sock, &cmd.type, sizeof(char), ZMQ_SNDMORE | ZMQ_DONTWAIT);
    if( cmd.datalen > 0 ) {
        unsigned short datalen = htons(cmd.datalen);
        zmq_send(sock, &datalen, sizeof(unsigned short), ZMQ_SNDMORE | ZMQ_DONTWAIT);
        zmq_send(sock, cmd.data, cmd.datalen, ZMQ_DONTWAIT);
    } else {
        zmq_send(sock, &cmd.type, sizeof(char), ZMQ_SNDMORE | ZMQ_DONTWAIT);
        zmq_send(sock, &cmd.datalen, sizeof(unsigned short), ZMQ_DONTWAIT);
    }
}

int readCommand( void * sock, audio_device_command * cmd ) {
    // First, read in the type:
    char type;
    int rc = zmq_recv(sock, &type, sizeof(char), ZMQ_DONTWAIT);
    if( rc < 1 ) {
        cmd->type = CMD_INVALID;
        cmd->datalen = 0;
        cmd->data = NULL;
        return errno;
    }
    cmd->type = type;

    // Next, read in the datalen
    unsigned short datalen;
    zmq_recv(sock, &datalen, sizeof(unsigned short), ZMQ_DONTWAIT);
    cmd->datalen = ntohs(datalen);

    // If we successfully read in the type, let's construct a command object out of it
    if( cmd->datalen > 0 ) {
        cmd->data = new char[cmd->datalen];
        zmq_recv(sock, cmd->data, cmd->datalen, 0);
    }
    return 0;
}
